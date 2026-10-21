/*
  compiler.generator.cpp

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

// The compiler's services for language compilers (i_code_generator)

#include <neolib/neolib.hpp>
#include <iostream>
#include <neos/i_context.hpp>
#include <neos/language/compiler.hpp>
#include <neos/bytecode/vm/runtime_plugins.hpp>

namespace neos::language
{
    // --- passes

    bool compiler::declaration_pass() const
    {
        return iDeclarationPass;
    }

    std::uint32_t compiler::inference_pass() const
    {
        return iInference.pass;
    }

    void compiler::set_changed()
    {
        iInference.changed = true;
    }

    void compiler::trace_output(neolib::i_string const& aText)
    {
        iContext.cout() << aText.to_std_string_view();
    }

    // --- the operand stack

    std::size_t compiler::operand_count() const
    {
        return state().operandStack.size();
    }

    std::size_t compiler::operand_base() const
    {
        return state().function ? iModule.functions[*state().function].operandBase : 0u;
    }

    i_operand_type const& compiler::operand(std::size_t aIndex) const
    {
        return state().operandStack.at(aIndex);
    }

    void compiler::set_operand(std::size_t aIndex, i_operand_type const& aOperand)
    {
        state().operandStack.at(aIndex) = aOperand;
    }

    void compiler::insert_operand(std::size_t aIndex, i_operand_type const& aOperand)
    {
        auto& stack = state().operandStack;
        // (a statement of the language's: its line (see bytecode::LineMarker))
        if (aOperand.holds_alternative<i_fragment>() && aOperand.get<i_fragment>().kind() == fragment_kind::Statement && 
            !aOperand.get<i_fragment>().source().empty())
        {
            fragment statement{ aOperand.get<i_fragment>() };
            mark_line(statement, statement.fragmentSource);
            stack.insert(std::next(stack.begin(), aIndex), operand_type{ statement });
            return;
        }
        stack.insert(std::next(stack.begin(), aIndex), operand_type{ aOperand });
    }

    void compiler::erase_operands(std::size_t aIndex, std::size_t aCount)
    {
        auto& stack = state().operandStack;
        stack.erase(std::next(stack.begin(), aIndex), std::next(stack.begin(), aIndex + aCount));
    }

    // --- values

    i_fragment const& compiler::materialize_operand(i_operand_type const& aOperand)
    {
        auto result = materialize(operand_type{ aOperand });
        iResult = result;
        return iResult;
    }

    i_fragment const& compiler::materialize_operand(i_operand_type const& aOperand, type aTargetType)
    {
        auto result = materialize(operand_type{ aOperand }, aTargetType);
        iResult = result;
        return iResult;
    }

    i_fragment const& compiler::coerce_value(i_fragment const& aValue, type aTarget)
    {
        fragment result{ aValue };
        coerce(result, aTarget);
        iResult = result;
        return iResult;
    }

    void compiler::pop_fragments_into(neolib::i_string_view const& aSource, fragment_kind aKind, neolib::i_vector<i_fragment>& aFragments)
    {
        for (auto const& f : pop_fragments(aSource, aKind))
            aFragments.push_back(f);
    }

    std::uint32_t compiler::string_literal(neolib::i_string const& aValue)
    {
        return string_literal(aValue.to_std_string());
    }

    bool compiler::runtime_plugin_available(neolib::i_string const& aNamespace) const
    {
        return bytecode::vm::runtime::plugin_available(aNamespace.to_std_string());
    }

    void compiler::import_fragment(neolib::i_string const& aSourceFilePath, neolib::i_string const& aSource)
    {
        auto& unit = *state().unit;
        for (auto const& existing : unit.fragments)
            if (existing.source_file_path().has_value() && existing.source_file_path().value() == aSourceFilePath)
                return;
        auto& fragment = *unit.fragments.emplace(unit.fragments.end(), source_fragment{ neolib::string{ aSourceFilePath }, neolib::string{ aSource } });
        fragment.set_imported();
    }

    std::uint32_t compiler::runtime_function(neolib::i_string const& aName, neolib::i_vector<type> const& aParameters, type aReturnType)
    {
        return runtime_function(aName.to_std_string(), std::vector<type>{ aParameters.begin(), aParameters.end() }, aReturnType);
    }

    // --- functions

    bool compiler::in_function() const
    {
        return state().function.has_value();
    }

    std::uint32_t compiler::current_function_index() const
    {
        if (!state().function)
            throw std::runtime_error("not in a function");
        return *state().function;
    }

    void compiler::set_pending_inferred()
    {
        state().pendingInferred = true;
    }

    neolib::i_string const& compiler::function_name(std::uint32_t aFunction) const
    {
        thread_local neolib::string result;
        result = iModule.functions.at(aFunction).name;
        return result;
    }

    i_function_signature const& compiler::signature_of(std::uint32_t aFunction) const
    {
        auto const& function = iModule.functions.at(aFunction);
        if (function.scope != nullptr)
            return function.scope->function_signature();
        return function.signature;
    }

    bool compiler::function_inferred(std::uint32_t aFunction) const
    {
        return iModule.functions.at(aFunction).inferred;
    }

    bool compiler::function_implicit_entry(std::uint32_t aFunction) const
    {
        return iModule.functions.at(aFunction).implicitEntry;
    }

    std::size_t compiler::forward_declaration_count() const
    {
        return iForwardDeclarations.size();
    }

    neolib::i_string_view const& compiler::forward_declaration_name(std::size_t aIndex) const
    {
        thread_local neolib::string_view result;
        result = neolib::string_view{ iForwardDeclarations.at(aIndex).name };
        return result;
    }

    i_function_signature const& compiler::forward_declaration_signature(std::size_t aIndex) const
    {
        return iForwardDeclarations.at(aIndex).signature;
    }

    bool compiler::forward_declaration_stub(std::size_t aIndex) const
    {
        return iForwardDeclarations.at(aIndex).stub;
    }

    bool compiler::forward_declaration_inferred(std::size_t aIndex) const
    {
        return iForwardDeclarations.at(aIndex).inferred;
    }

    std::size_t compiler::function_count() const
    {
        return iModule.functions.size();
    }

    bool compiler::function_imported(std::uint32_t aFunction) const
    {
        return iModule.functions.at(aFunction).imported;
    }

    bool compiler::function_stub(std::uint32_t aFunction) const
    {
        return iModule.functions.at(aFunction).stub;
    }

    bool compiler::function_intrinsic(std::uint32_t aFunction) const
    {
        return iModule.functions.at(aFunction).intrinsic.has_value();
    }

    bool compiler::function_entry(std::uint32_t aFunction) const
    {
        return iModule.functions.at(aFunction).entry;
    }

    i_fragment const& compiler::function_body(std::uint32_t aFunction) const
    {
        thread_local fragment result;
        auto const& function = iModule.functions.at(aFunction);
        result = fragment{};
        result.fragmentKind = fragment_kind::Statement;
        result.fragmentCode.as_std_vector() = function.body;
        for (auto const& r : function.relocations)
            result.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ r.first, r.second });
        return result;
    }

    void compiler::set_function_body(std::uint32_t aFunction, i_fragment const& aBody)
    {
        auto& function = iModule.functions.at(aFunction);
        function.body.assign(aBody.code().begin(), aBody.code().end());
        function.relocations.clear();
        for (auto const& r : aBody.relocations())
            function.relocations.emplace_back(r.first(), r.second());
    }

    std::uint32_t compiler::deferred_call(neolib::i_string const& aName, neolib::i_vector<type> const& aParameters)
    {
        auto const relocation = DeferredCall | static_cast<std::uint32_t>(iModule.deferredCalls.size());
        iModule.deferredCalls.emplace_back(aName.to_std_string(), std::vector<type>{ aParameters.begin(), aParameters.end() });
        return relocation;
    }

    void compiler::set_function_locals(std::uint32_t aFunction, neolib::i_vector<type> const& aLocals)
    {
        iModule.functions.at(aFunction).locals.assign(aLocals.begin(), aLocals.end());
    }

    std::uint32_t compiler::define_function(neolib::i_string const& aName, i_function_signature const& aSignature, neolib::i_vector<type> const& aLocals,
        neolib::i_string const& aFrameName, std::uint32_t aFirstLine)
    {
        auto const id = add_function(aName.to_std_string(), false);
        auto& function = iModule.functions[id];
        function.signature = function_signature{ aSignature };
        function.locals.assign(aLocals.begin(), aLocals.end());
        function.compiled = true;
        if (!aFrameName.empty())
            function.frameName = aFrameName.to_std_string();
        function.frameSource = source_name();
        function.firstLine = aFirstLine;
        return id;
    }

    type compiler::inferred_parameter(neolib::i_string_view const& aFunction, std::uint32_t aIndex) const
    {
        auto const facts = iInference.parameters.find(aFunction.to_std_string());
        if (facts == iInference.parameters.end() || aIndex >= facts->second.size())
            return type::UNKNOWN;
        return facts->second[aIndex];
    }

    void compiler::set_inferred_parameter(neolib::i_string_view const& aFunction, std::uint32_t aIndex, std::uint32_t aCount, type aType)
    {
        auto& facts = iInference.parameters[aFunction.to_std_string()];
        facts.resize(std::max<std::size_t>(facts.size(), aCount), type::F64);
        facts.at(aIndex) = aType;
    }

    void compiler::infer(neolib::i_string const& aWhat, type aOld, type aNew)
    {
        infer(aWhat.to_std_string(), aOld == type::UNKNOWN ? std::optional<type>{} : std::optional<type>{ aOld }, aNew);
    }

    // --- symbols

    i_scope const& compiler::global_scope() const
    {
        return state().program->scope;
    }

    i_symbol_table_entry& compiler::define_global(neolib::i_string const& aName, type aType)
    {
        return define_global(aName.to_std_string(), aType);
    }

    bool compiler::is_constant(i_symbol_table_entry const& aSymbol) const
    {
        auto const info = iSymbolInfo.find(&aSymbol);
        return info != iSymbolInfo.end() && info->second.constant;
    }

    void compiler::set_constant(i_symbol_table_entry const& aSymbol, bool aConstant)
    {
        iSymbolInfo[&aSymbol].constant = aConstant;
    }

    bool compiler::is_block_scoped(i_symbol_table_entry const& aSymbol) const
    {
        auto const info = iSymbolInfo.find(&aSymbol);
        return info != iSymbolInfo.end() && info->second.position != nullptr;
    }

    void compiler::set_block_scoped(i_symbol_table_entry const& aSymbol, bool aBlockScoped)
    {
        // (not block scoped: it is not retired at the end of the block that declares it)
        if (!aBlockScoped)
            iSymbolInfo[&aSymbol].position = nullptr;
    }

    // --- indented constructs

    std::size_t compiler::indented_count() const
    {
        return state().indentStack.size();
    }

    indented_construct compiler::indented_kind(std::size_t aIndex) const
    {
        return state().indentStack.at(aIndex).kind;
    }

    bool compiler::indented_try_else(std::size_t aIndex) const
    {
        return state().indentStack.at(aIndex).tryElse;
    }

    void compiler::set_loop_epilogue(i_fragment const& aEpilogue)
    {
        state().indentStack.back().epilogue = fragment{ aEpilogue };
    }

    namespace
    {
        // (an indented construct as a language compiler sees it)
        template <typename Indented>
        class indented_view : public i_indented_construct
        {
        public:
            indented_view(Indented const& aConstruct) : iConstruct{ aConstruct }
            {
                for (auto const& name : aConstruct.handlerNames)
                    iNames.push_back(name ? std::optional<neolib::string>{ neolib::string{ *name } } : std::nullopt);
            }
        public:
            indented_construct kind() const final { return iConstruct.kind; }
            bool try_else() const final { return iConstruct.tryElse; }
            std::size_t condition_count() const final { return iConstruct.conditions.size(); }
            i_fragment const& condition(std::size_t aIndex) const final { return iConstruct.conditions.at(aIndex); }
            std::size_t arm_count() const final { return iConstruct.arms.size(); }
            i_fragment const& arm(std::size_t aIndex) const final { return iConstruct.arms.at(aIndex); }
            i_fragment const* else_arm() const final { return iConstruct.elseArm ? &*iConstruct.elseArm : nullptr; }
            i_fragment const* finally_arm() const final { return iConstruct.finallyArm ? &*iConstruct.finallyArm : nullptr; }
            std::size_t handler_type_count() const final { return iConstruct.handlerTypes.size(); }
            i_fragment const* handler_type(std::size_t aIndex) const final { auto const& t = iConstruct.handlerTypes.at(aIndex); return t ? &*t : nullptr; }
            std::size_t handler_name_count() const final { return iNames.size(); }
            neolib::i_string const* handler_name(std::size_t aIndex) const final { auto const& n = iNames.at(aIndex); return n ? &*n : nullptr; }
        private:
            Indented const& iConstruct;
            std::vector<std::optional<neolib::string>> iNames;
        };
    }

    fragment compiler::language_construct(compilation_state::indented const& aConstruct)
    {
        auto const* statement = language() != nullptr ? language()->finish_construct(indented_view<compilation_state::indented>{ aConstruct }) : nullptr;
        if (statement == nullptr)
            throw std::runtime_error("statement not supported by the language");
        return fragment{ *statement };
    }

    // --- global variables

    std::size_t compiler::global_count() const
    {
        return iInference.globals.size();
    }

    neolib::i_string const& compiler::global_name(std::size_t aIndex) const
    {
        thread_local neolib::string result;
        result = iInference.globals.at(aIndex).first;
        return result;
    }

    type compiler::global_type(std::size_t aIndex) const
    {
        return iInference.globals.at(aIndex).second;
    }

    void compiler::add_global(neolib::i_string const& aName, type aType)
    {
        iInference.globals.emplace_back(aName.to_std_string(), aType);
    }

    void compiler::set_global_type(std::size_t aIndex, type aType)
    {
        iInference.globals.at(aIndex).second = aType;
    }

    bool compiler::entry_global(neolib::i_string_view const& aName) const
    {
        return iInference.entryGlobals.contains(aName.to_std_string());
    }

    void compiler::function_global(neolib::i_string const& aName)
    {
        iInference.functionGlobals.insert(aName.to_std_string());
    }
}
