/*
  compiler.inference.cpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

     * Neither the name of the copyright holder nor the names of its
       contributors may be used to endorse or promote products derived from
       this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


// The inference of types (of functions whose parameter and result types are inferred, of global variables) and the
// entry point that top-level statements form

#include <neolib/neolib.hpp>
#include <neos/bytecode/bytecode.hpp>
#include <neos/i_context.hpp>
#include <neos/language/compiler.hpp>
#include <neos/language/emit.hpp>

namespace neos::language
{
    using namespace emitter;
    using bytecode::opcode;

    namespace
    {
        std::string value_type_name(type aType)
        {
            switch (aType)
            {
            case type::String: return "string";
            case type::Boolean: return "boolean";
            case type::Void: return "undefined";
            default: return "number";
            }
        }
    }

    void compiler::add_language_compiler(i_language_compiler& aLanguageCompiler)
    {
        iLanguageCompilers.push_back(neolib::ref_ptr<i_language_compiler>{ aLanguageCompiler });
        aLanguageCompiler.attach(*this);
    }

    i_language_compiler* compiler::language() const
    {
        return iCompilationStateStack.empty() ? nullptr : state().language;
    }

    void compiler::set_language_options(std::string const& aLanguage, std::string const& aOptions)
    {
        if (auto* const languageCompiler = language(aLanguage))
            languageCompiler->set_options(neolib::string{ aOptions });
    }

    bool compiler::compiles_at_run_time(std::string const& aLanguage) const
    {
        auto const* const languageCompiler = language(aLanguage);
        return languageCompiler != nullptr && languageCompiler->compiles_at_run_time();
    }

    i_language_compiler* compiler::language(std::string const& aLanguage) const
    {
        for (auto const& languageCompiler : iLanguageCompilers)
            if (languageCompiler->language().to_std_string_view() == aLanguage)
                return &*languageCompiler;
        return nullptr;
    }

    void compiler::complete_signature(i_function_signature& aSignature, neolib::i_string_view const& aSource)
    {
        if (auto* const languageCompiler = language(); languageCompiler != nullptr && languageCompiler->complete_signature(aSignature, aSource))
            return;
        if (!state().pendingInferred)
            return;
        // (a function declared after top-level statements is not part of the entry point that they form)
        if (state().function && current_function().implicitEntry)
            suspend_entry();
        if (state().function)
            throw std::runtime_error("nested functions are not supported");
        std::string name = current_scope().qualified_name().to_std_string() + "::" + aSignature.name().to_std_string();
        while (name.starts_with("::"))
            name.erase(0, 2);
        if (aSignature.parameters().has_value())
        {
            auto const facts = iInference.parameters.find(name);
            for (std::size_t i = 0u; i < aSignature.parameters().value().size(); ++i)
            {
                auto& parameter = aSignature.parameters().value()[i];
                if (parameter.parameter_type() != type::UNKNOWN)
                    continue;
                auto t = facts != iInference.parameters.end() && i < facts->second.size() ? facts->second[i] : type::F64;
                // (the language's type of the parameter)
                if (auto* const languageCompiler = language())
                    t = languageCompiler->parameter_type(neolib::string_view{ name }, static_cast<std::uint32_t>(i), t);
                parameter.set_parameter_type(t);
            }
        }
        auto const result = iInference.results.find(name);
        aSignature.set_return_type(result != iInference.results.end() ? result->second : type::Void);
    }

    void compiler::suspend_entry()
    {
        state().suspendedEntry.emplace(*state().function, state().scopeStack.back());
        state().scopeStack.pop_back();
        state().function = std::nullopt;
    }

    void compiler::resume_entry()
    {
        auto const entry = *state().suspendedEntry;
        state().suspendedEntry = std::nullopt;
        state().scopeStack.push_back(entry.second);
        state().function = entry.first;
    }

    bool compiler::is_global(i_symbol_table_entry const& aSymbol) const
    {
        auto const info = iSymbolInfo.find(&aSymbol);
        return info != iSymbolInfo.end() && info->second.global;
    }

    i_symbol_table_entry& compiler::define_global(std::string const& aName, type aType)
    {
        std::uint32_t index = 0u;
        while (index < iInference.globals.size() && iInference.globals[index].first != aName)
            ++index;
        if (index == iInference.globals.size())
            iInference.globals.emplace_back(aName, aType);
        auto& symbols = state().program->symbolTable[&state().program->scope];
        auto& entry = symbols.insert(neolib::string{ aName }, symbol_table_entry{ aType, index + 1u })->second();
        auto& info = iSymbolInfo[&entry];
        info.global = true;
        info.sequence = ++iSymbolSequence;
        return entry;
    }

    void compiler::declare_globals()
    {
        // globals (found by the previous pass) can be used by functions declared before them
        if (iGlobalsDeclared)
            return;
        iGlobalsDeclared = true;
        auto const globals = iInference.globals;
        for (auto const& global : globals)
            if (!iInference.entryGlobals.contains(global.first))
                define_global(global.first, global.second);
    }

    void compiler::infer(std::string const& aWhat, std::optional<type> const& aOld, type aNew)
    {
        if (aOld != aNew)
        {
            iInference.changed = true;
            if (trace() >= 1)
                iContext.cout() << "Inferred type of " << aWhat << ": " << value_type_name(aNew) << std::endl;
        }
    }
}
