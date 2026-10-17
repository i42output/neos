/*
  compiler.cpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

  This program is free software: you can redistribute it and / or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <neolib/neolib.hpp>

#include <iostream>
#include <boost/lexical_cast.hpp>

#include <neolib/core/scoped.hpp>
#include <neolib/core/recursion.hpp>
#include <neolib/core/string_utf.hpp>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <limits>
#include <cmath>
#include <cstddef>
#include <neos/bytecode/bytecode.hpp>
#include <neos/bytecode/opcodes.hpp>
#include <neos/bytecode/text.hpp>
#include <neos/i_context.hpp>
#include <neos/language/compiler.hpp>
#include <neos/language/emit.hpp>

namespace neos::language
{
    compiler::compiler(i_context& aContext) :
        iContext{ aContext }, iTrace { 0u }, iStartTime{ std::chrono::steady_clock::now() }, iEndTime{ std::chrono::steady_clock::now() }
    {
    }

    std::uint32_t compiler::trace() const
    {
        return iTrace;
    }

    const std::optional<std::string>& compiler::trace_filter() const
    {
        return iTraceFilter;
    }

    void compiler::set_trace(std::uint32_t aTrace, const std::optional<std::string>& aFilter)
    {
        iTrace = aTrace;
        iTraceFilter = aFilter;
    }

    const std::chrono::steady_clock::time_point& compiler::start_time() const
    {
        return iStartTime;
    }

    const std::chrono::steady_clock::time_point& compiler::end_time() const
    {
        return iEndTime;
    }

    std::chrono::steady_clock::duration compiler::preparse_time() const
    {
        return iPreparseTime;
    }

    std::chrono::steady_clock::duration compiler::parse_time() const
    {
        return iParseTime;
    }

    bool compiler::show_parse_times() const
    {
        return iShowParseTimes;
    }

    void compiler::set_show_parse_times(bool aShowParseTimes)
    {
        iShowParseTimes = aShowParseTimes;
    }

    bool compiler::preparse() const
    {
        return iPreparse;
    }

    void compiler::set_preparse(bool aPreparse)
    {
        iPreparse = aPreparse;
    }

    bool compiler::trap_on_overflow() const
    {
        return iTrapOnOverflow;
    }

    bool compiler::overflow_traps() const
    {
        // (integer arithmetic of fixed size types traps on overflow)
        return iTrapOnOverflow;
    }

    bool compiler::bigint_promotion() const
    {
        if (iBigintPromotion)
            return *iBigintPromotion;
        return !iCompilationStateStack.empty() && state().unit != nullptr && state().unit->schema->meta().bigintPromotion;
    }

    std::optional<bool> compiler::bigint_promotion_override() const
    {
        return iBigintPromotion;
    }

    void compiler::set_bigint_promotion_override(std::optional<bool> aBigintPromotion)
    {
        iBigintPromotion = aBigintPromotion;
    }

    void compiler::set_trap_on_overflow(bool aTrapOnOverflow)
    {
        iTrapOnOverflow = aTrapOnOverflow;
    }

    bool compiler::compile(program& aProgram)
    {
        iStartTime = std::chrono::steady_clock::now();
        iPreparseTime = {};
        iParseTime = {};

        bool ok = true;

        auto reset = [&]()
        {
            for (auto& unit : aProgram.translationUnits)
                for (auto fragment = unit.fragments.begin(); fragment != unit.fragments.end();)
                    if (fragment->imported())
                        fragment = unit.fragments.erase(fragment);
                    else
                        (fragment++)->set_status(compilation_status::Pending);
            iModule = {};
            iSymbolInfo.clear();
            iSymbolSequence = 0u;
            aProgram.text.clear();
            aProgram.symbolTable.clear();
            aProgram.scope.clear();
            iCompilationStateStack.clear();
            iGlobalsDeclared = false;
            iInference.returned.clear();
            iInference.functionGlobals.clear();
            iInference.changed = false;
            for (auto& languageCompiler : iLanguageCompilers)
                languageCompiler->begin_pass();
        };
        // (the language compiler of the program's language)
        auto* const programLanguage = aProgram.translationUnits.empty() ? nullptr : language(aProgram.translationUnits.front().schema->meta().language);

        try
        {
            // pass 1: declarations only (collects function signatures so that functions can be called before they are defined)
            iInference = {};
            for (auto& languageCompiler : iLanguageCompilers)
                languageCompiler->begin_compilation();
            reset();
            iForwardDeclarations.clear();
            iDeclarationPass = true;
            for (auto& unit : aProgram.translationUnits)
                if (ok)
                    ok = compile(aProgram, unit);
            iDeclarationPass = false;
            if (ok)
            {
                iDeclaredFunctions.clear();
                for (auto const& function : iModule.functions)
                    iDeclaredFunctions.push_back(forward_declaration{ function.stub ? function.name.substr(0u, function.name.find('(')) : function.name,
                        function.scope != nullptr ? function_signature{ function.scope->function_signature() } : function.signature, function.stub, 
                        function.inferred });
                // pass 2: code generation (repeated while the types that are inferred change: each pass uses those found by 
                // the previous one; an error is only an error if they did not change)
                std::uint32_t constexpr MaxPasses = 32u;
                for (std::uint32_t pass = 1u;; ++pass)
                {
                    iForwardDeclarations = iDeclaredFunctions;
                    for (auto& declaration : iForwardDeclarations)
                        if (declaration.inferred)
                        {
                            auto& signature = declaration.signature;
                            if (signature.parameters().has_value())
                                for (std::size_t i = 0u; i < signature.parameters().value().size(); ++i)
                                {
                                    auto const facts = iInference.parameters.find(declaration.name);
                                    auto t = facts != iInference.parameters.end() && i < facts->second.size() ? facts->second[i] : type::F64;
                                    // (the language's type of the parameter)
                                    if (programLanguage != nullptr)
                                        t = programLanguage->parameter_type(neolib::string_view{ declaration.name }, static_cast<std::uint32_t>(i), t);
                                    signature.parameters().value()[i].set_parameter_type(t);
                                }
                            auto const result = iInference.results.find(declaration.name);
                            signature.set_return_type(result != iInference.results.end() ? result->second : type::Void);
                        }
                    reset();
                    iInference.pass = pass;
                    try
                    {
                        for (auto& unit : aProgram.translationUnits)
                            if (ok)
                                ok = compile(aProgram, unit);
                        // global variables that no function uses can be locals of the entry point
                        std::set<std::string> entryGlobals;
                        for (auto const& global : iInference.globals)
                            if (!iInference.functionGlobals.contains(global.first))
                                entryGlobals.insert(global.first);
                        if (ok && entryGlobals != iInference.entryGlobals)
                        {
                            iInference.entryGlobals = entryGlobals;
                            iInference.changed = true;
                        }
                        // (the facts that the language compilers found)
                        if (ok)
                            for (auto& languageCompiler : iLanguageCompilers)
                                languageCompiler->end_pass();
                    }
                    catch (...)
                    {
                        if (!iInference.changed || pass == MaxPasses)
                            throw;
                    }
                    if (!ok || !iInference.changed)
                        break;
                    if (pass == MaxPasses)
                        throw std::runtime_error("types could not be inferred");
                }
            }
            if (ok)
                emit_module(aProgram);
        }
        catch(...)
        {
            iDeclarationPass = false;
            iEndTime = std::chrono::steady_clock::now();
            throw;
        }

        iEndTime = std::chrono::steady_clock::now();

        return ok;
    }

    bool compiler::compile(program& aProgram, translation_unit& aUnit)
    {
        bool ok = true;

        for (auto& fragment : aUnit.fragments)
            if (ok && fragment.status() == compilation_status::Pending)
                ok = compile(aProgram, aUnit, fragment);

        return ok;
    }

    namespace
    {
        void walk_ast(i_context& context, ast& ast, fold_stack& foldStack, parser::ast_node const& parserAstNode, i_ast_node& astNode)
        {
            for (auto const& childParserNode : parserAstNode.children)
            {
                astNode.children().push_back(neolib::make_ref<ast_node>(std::monostate{}, astNode));
                auto& childNode = *astNode.children().back();
                walk_ast(context, ast, foldStack, *childParserNode, childNode);
            }

            neolib::string_view const conceptName{ parserAstNode.c.value() };
            neolib::string_view const conceptValue{ parserAstNode.value };

            auto c = context.find_concept(conceptName.to_std_string_view());
            if (c)
            {
                auto const semanticConcept = c->instantiate(context, conceptValue);
                astNode.value() = semanticConcept;
                if (conceptName != "language.keyword")
                {
                    foldStack.push_back(neolib::ref_ptr<i_ast_node>{ astNode });
                    if (context.compiler().trace() >= 2)
                        context.cout() << "Fold stack add: " << conceptName << " [" <<
                        neolib::to_escaped_string(conceptValue.to_std_string_view(), 32u, true) << "]" << std::endl;
                }
            }
            else
                throw concept_not_found(conceptName.to_std_string_view(), conceptValue.begin());
        };

        template <std::size_t Index = 1u>
        data_type make_symbol_data(i_symbol_table_entry& aSymbol)
        {
            if constexpr (Index < std::variant_size_v<data_type::std_type>)
            {
                using alternative = std::variant_alternative_t<Index, data_type::std_type>;
                if (static_cast<std::size_t>(aSymbol.type()) == Index)
                {
                    if constexpr (std::is_default_constructible_v<alternative>)
                    {
                        alternative symbolData;
                        symbolData.symbol() = &aSymbol;
                        return symbolData;
                    }
                }
                else
                    return make_symbol_data<Index + 1u>(aSymbol);
            }
            throw std::runtime_error("unsupported symbol type: " + type_name(aSymbol.type()));
        }
    }

    bool compiler::compile(program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment)
    {
        if (aFragment.status() != compilation_status::Pending)
            return false;

        iCompilationStateStack.push_back(std::make_unique<compilation_state>(&aProgram, &aUnit, &aFragment));
        state().language = language(aUnit.schema->meta().language);
        if (state().language != nullptr)
            state().language->begin_fragment(aFragment);

        aFragment.set_status(compilation_status::Compiling);

        // (a language's own pass over the source before it is parsed)
        if (iPreparse && state().language != nullptr)
        {
            auto const preparseStart = std::chrono::steady_clock::now();
            state().language->preparse(aFragment);
            iPreparseTime += std::chrono::steady_clock::now() - preparseStart;
        }

        if (!aFragment.imported() && !iDeclarationPass)
            declare_globals();

        if (!aFragment.imported())
            for (auto const& package : aUnit.schema->meta().prelude)
            {
                source_fragment prelude{ neolib::string{ package } };
                prelude.set_imported();
                iContext.load_fragment(prelude);
                if (!compile(prelude))
                    throw std::runtime_error("failed to compile prelude package '" + package + "'");
            }

        bool ok = false;

        for (auto const& stage : aUnit.schema->pipeline())
        {
            bool const last = (stage == aUnit.schema->pipeline().back());
            auto& parser = *stage->parser;
            for (auto const& discard : *stage->discard)
                parser.ignore(stage->symbolMap->at(discard));
            parser.set_debug_output(std::cerr, false, false);
            parser.set_debug_timing(iShowParseTimes);
            // (a source parsed by the previous compilation pass need not be parsed again)
            auto& parsed = iParsed[&parser];
            if (parsed.first && parsed.second == aFragment.source().to_std_string_view())
                ok = true;
            else
            {
                parsed = {};
                if (stage->root)
                    ok = parser.parse(stage->symbolMap->at(stage->root.value()), aFragment.source().to_std_string_view());
                else
                    ok = parser.parse(aFragment.source().to_std_string_view());
                iParseTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(parser.parse_time());
                if (ok && last)
                    parser.create_ast();
                if (ok)
                    parsed = { true, aFragment.source().to_std_string() };
            }
            if (!ok)
                break;
            if (last)
            {
                auto& unitRoot = *aUnit.ast.root();
                unitRoot.children().push_back(neolib::make_ref<ast_node>(std::monostate{}, unitRoot)); // each fragment gets its own root
                walk_ast(iContext, aUnit.ast, fold_stack(), parser.ast(), *unitRoot.children().back());
            }
        }
            
        if (ok)
        {
            if (!fold())
            {
                aFragment.set_status(compilation_status::Error);

                if (state().language != nullptr)
                    state().language->end_fragment();
                iCompilationStateStack.pop_back();

                throw std::runtime_error("Failed to fold semantic concepts");
            }

            aFragment.set_status(compilation_status::Compiled);
        }
        else
            aFragment.set_status(compilation_status::Error);

        if (state().language != nullptr)
            state().language->end_fragment();
        iCompilationStateStack.pop_back();

        return ok;
    }

    bool compiler::compile(const i_source_fragment& aFragment)
    {
        auto& program = *state().program;
        auto& unit = *state().unit;
        if (aFragment.source_file_path().has_value())
            for (auto const& existing : unit.fragments)
                if (existing.source_file_path() == aFragment.source_file_path())
                    return true; // already compiled (package imported more than once)
        auto& fragment = *unit.fragments.emplace(unit.fragments.end(), aFragment);
        return compile(program, unit, fragment);
    }

    i_source_fragment const& compiler::current_fragment() const
    {
        return *state().fragment;
    }

    i_scope const& compiler::current_scope() const
    {
        if (!state().scopeStack.empty())
            return *state().scopeStack.back();
        return state().program->scope;
    }

    i_scope& compiler::enter_scope(scope_type aScopeType, neolib::i_string const& aScopeName)
    {
        if (state().scopeStack.empty())
            state().scopeStack.push_back(state().program->scope.create_child(aScopeName, aScopeType));
        else
            state().scopeStack.push_back(state().scopeStack.back()->create_child(aScopeName, aScopeType));
        if (aScopeType == scope_type::Function)
        {
            if (!state().pendingFunction)
                throw std::logic_error("neos::language::compiler::enter_scope: no function defined");
            auto& function = iModule.functions[*state().pendingFunction];
            function.scope = static_cast<i_function_scope const*>(&*state().scopeStack.back());
            function.operandBase = state().operandStack.size();
            function.fromImportedFragment = state().fragment->imported();
            function.entry = state().pendingEntry;
            state().pendingEntry = false;
            state().function = state().pendingFunction;
            state().pendingFunction = std::nullopt;
        }
        return *state().scopeStack.back();
    }

    void compiler::leave_scope(scope_type aScopeType)
    {
        switch (aScopeType)
        {
        case scope_type::Namespace:
            if (!state().scopeStack.empty() && state().scopeStack.back()->type() == scope_type::Namespace)
                state().scopeStack.pop_back();
            else
                throw std::runtime_error("Unmatched namespace scopes");
            break;
        case scope_type::Class:
            if (!state().scopeStack.empty() && state().scopeStack.back()->type() == scope_type::Class)
                state().scopeStack.pop_back();
            else
                throw std::runtime_error("Unmatched class scopes");
            break;
        case scope_type::Function:
            if (!state().scopeStack.empty() && state().scopeStack.back()->type() == scope_type::Function)
            {
                finish_function();
                state().scopeStack.pop_back();
                if (state().suspendedEntry)
                    resume_entry();
            }
            else
                throw std::runtime_error("Unmatched function scopes");
            break;
        case scope_type::Block:
            if (!state().scopeStack.empty() && state().scopeStack.back()->type() == scope_type::Block)
                state().scopeStack.pop_back();
            else
                throw std::runtime_error("Unmatched block scopes");
            break;
        }
    }

    i_operand_type const& compiler::lhs_operand() const
    {
        if (state().operandStack.size() == 1)
            return *(state().operandStack.rbegin());
        else if (state().operandStack.size() >= 2)
            return *std::next(state().operandStack.rbegin());
        throw std::logic_error("neos::language::compiler::lhs_operand");
    }

    i_operand_type const& compiler::rhs_operand() const
    {
        if (state().operandStack.size() >= 2)
            return *(state().operandStack.rbegin());
        throw std::logic_error("neos::language::compiler::rhs_operand");
    }

    void compiler::push_operand(i_operand_type const& aOperand)
    {
        if (auto* const languageCompiler = language())
            if (auto const* value = languageCompiler->literal(aOperand))
            {
                // (the language's value of a literal (e.g. an int literal beyond 64 bits: an int object))
                state().operandStack.push_back(operand_type{ fragment{ *value } });
                return;
            }
        state().operandStack.push_back(aOperand);
        if (trace() >= 2)
            iContext.cout() << "Pushed operand: " << aOperand << std::endl;
    }

    void compiler::pop_operand(i_data_type& aOperand)
    {
        if (!state().operandStack.empty())
        {
            auto const& operand = state().operandStack.back();
            if (operand.holds_alternative<i_symbol_reference>())
            {
                auto const& reference = operand.get<i_symbol_reference>();
                auto* const symbol = find_symbol(reference.scope(), neolib::string_view{ reference.name() });
                if (symbol == nullptr)
                    throw std::runtime_error("undefined identifier '" + reference.name().to_std_string() + "'");
                aOperand = make_symbol_data(*symbol);
            }
            else if (operand.holds_alternative<i_fragment>())
                throw std::logic_error("neos::language::compiler::pop_operand: operand is not data");
            else
                aOperand = operand.get<i_data_type>();
            state().operandStack.pop_back();
            if (trace() >= 2)
                iContext.cout() << "Popped operand: " << aOperand << std::endl;
        }
        else
            throw std::runtime_error("No operand");
    }

    void compiler::push_operator(i_operator_type const& aOperator)
    {
        state().operatorStack.push_back(aOperator);
        if (trace() >= 2)
            iContext.cout() << "Pushed operator: " << aOperator << std::endl;
    }

    void compiler::pop_operator(i_operator_type& aOperator)
    {
        if (!state().operatorStack.empty())
        {
            aOperator = state().operatorStack.back();
            state().operatorStack.pop_back();
            if (trace() >= 2)
                iContext.cout() << "Popped operator: " << aOperator << std::endl;
        }
        else
            throw std::runtime_error("No operator");
    }

    void compiler::define_symbol(i_symbol_name const& aName, symbol_type aType, neolib::i_string_view const& aSource)
    {
        // (a function declared after top-level statements is not part of the entry point that they form)
        if (aType == symbol_type::Function && state().function && current_function().implicitEntry)
            suspend_entry();
        auto& symbols = state().program->symbolTable[&current_scope()];
        i_symbol_table_entry* existing = nullptr;
        for (auto& symbol : symbols)
            if (symbol.first() == aName && !iSymbolInfo[&symbol.second()].retired)
                if (existing == nullptr || iSymbolInfo[&symbol.second()].sequence > iSymbolInfo[existing].sequence)
                    existing = &symbol.second();
        if (existing != nullptr)
        {
            bool allowed = false;
            if (aType == symbol_type::Function && existing->type() == symbol_type::Function)
                allowed = true; // overload
            else if (aType != symbol_type::Function && existing->type() != symbol_type::Function && state().function && !aSource.empty())
            {
                // a block-level local may shadow an enclosing local, but not a parameter
                bool parameter = false;
                auto const& signature = current_function().scope->function_signature();
                if (signature.parameters().has_value())
                    for (auto const& p : signature.parameters().value())
                        if (p.parameter_name() == aName)
                            parameter = true;
                allowed = !parameter;
            }
            if (!allowed)
                throw std::runtime_error("redefinition of '" + aName.to_std_string() + "'");
        }
        std::uint32_t index = 0u;
        if (aType == symbol_type::Function)
        {
            std::string qualifiedName = current_scope().qualified_name().to_std_string() + "::" + aName.to_std_string();
            while (qualifiedName.starts_with("::"))
                qualifiedName.erase(0, 2);
            index = add_function(qualifiedName, false);
            state().pendingFunction = index;
            iModule.functions[index].inferred = state().pendingInferred;
            state().pendingInferred = false;
        }
        else if (state().function)
        {
            auto& function = current_function();
            auto const& signature = function.scope->function_signature();
            std::optional<std::uint32_t> parameterIndex;
            std::uint32_t inParameters = 0u;
            if (signature.parameters().has_value())
                for (auto const& parameter : signature.parameters().value())
                {
                    if (parameter.direction() == parameter_direction::Out)
                        continue;
                    if (parameter.parameter_name() == aName && aSource.empty())
                        parameterIndex = inParameters;
                    ++inParameters;
                }
            if (parameterIndex)
                index = *parameterIndex;
            else
            {
                index = inParameters + static_cast<std::uint32_t>(function.locals.size());
                function.locals.push_back(aType);
            }
        }
        else
            throw std::runtime_error("global variables are not supported: '" + aName.to_std_string() + "'");
        auto& entry = symbols.insert(aName, symbol_table_entry{ aType, index })->second();
        auto& info = iSymbolInfo[&entry];
        info.position = aSource.empty() ? nullptr : aSource.to_std_string_view().data();
        info.sequence = ++iSymbolSequence;
        if (trace() >= 2)
            iContext.cout() << "Defined symbol: " << current_scope().qualified_name() << "::" << aName << " : " << type_name(aType) << std::endl;
    }

    void compiler::retire_symbols(neolib::i_string_view const& aSource)
    {
        // symbols defined within a block go out of scope at the end of the block
        auto const symbols = state().program->symbolTable.find(&current_scope());
        if (symbols == state().program->symbolTable.end())
            return;
        auto const block = aSource.to_std_string_view();
        for (auto& symbol : symbols->second())
        {
            auto& info = iSymbolInfo[&symbol.second()];
            if (info.position != nullptr && info.position >= block.data() && info.position < block.data() + block.size())
                info.retired = true;
        }
    }

    void compiler::find_identifier(neolib::i_string_view const& aIdentifier, neolib::i_optional<i_data_type>& aResult) const
    {
        auto* const symbol = find_symbol(current_scope(), aIdentifier);
        if (symbol != nullptr)
            aResult = make_symbol_data(*symbol);
        else
            aResult = std::nullopt;
    }

    i_symbol_table_entry* compiler::find_symbol(i_scope const& aScope, neolib::i_string_view const& aName, bool aThisScopeOnly) const
    {
        neolib::string const name{ aName };
        for (i_scope const* scope = &aScope;; scope = &scope->parent())
        {
            auto const symbols = state().program->symbolTable.find(scope);
            if (symbols != state().program->symbolTable.end())
            {
                i_symbol_table_entry* best = nullptr;
                std::uint64_t bestSequence = 0u;
                for (auto& symbol : symbols->second())
                {
                    if (symbol.first() != name)
                        continue;
                    auto const info = iSymbolInfo.find(&symbol.second());
                    if (info != iSymbolInfo.end() && info->second.retired)
                        continue;
                    auto const sequence = info != iSymbolInfo.end() ? info->second.sequence : 0u;
                    if (best == nullptr || sequence > bestSequence)
                    {
                        best = &symbol.second();
                        bestSequence = sequence;
                    }
                }
                if (best != nullptr)
                    return best;
            }
            if (aThisScopeOnly || !scope->has_parent())
                return nullptr;
        }
    }

    // --- WASM code generation

    using namespace emitter;

    compiler::wasm_function& compiler::current_function()
    {
        if (!state().function)
            throw std::runtime_error("not in a function");
        return iModule.functions[*state().function];
    }

    std::uint32_t compiler::add_function(std::string const& aName, bool aImported)
    {
        iModule.functions.emplace_back();
        iModule.functions.back().name = aName;
        iModule.functions.back().imported = aImported;
        return static_cast<std::uint32_t>(iModule.functions.size() - 1u);
    }

    std::uint32_t compiler::runtime_function(std::string const& aName, std::vector<type> const& aParameters, type aReturnType)
    {
        for (std::uint32_t id = 0u; id < iModule.functions.size(); ++id)
            if (iModule.functions[id].imported && iModule.functions[id].name == aName)
                return id;
        auto const id = add_function(aName, true);
        auto& function = iModule.functions[id];
        function.signature.set_name(neolib::string{ aName });
        function_parameters parameters;
        for (auto const& p : aParameters)
        {
            function_parameter parameter;
            parameter.set_parameter_type(p);
            parameters.push_back(parameter);
        }
        function.signature.set_parameters(parameters);
        function.signature.set_return_type(aReturnType);
        return id;
    }

    std::uint32_t compiler::string_literal(std::string const& aValue)
    {
        auto existing = iModule.strings.find(aValue);
        if (existing != iModule.strings.end())
            return existing->second;
        while (iModule.data.size() % 4u != 0u)
            iModule.data.push_back(0u);
        auto const address = DataBase + static_cast<std::uint32_t>(iModule.data.size());
        // (see bytecode::string_header)
        auto const header = bytecode::string_header_of(aValue);
        put_raw(iModule.data, header.bytes);
        put_raw(iModule.data, header.utf16Units);
        put_raw(iModule.data, header.codePoints);
        iModule.data.insert(iModule.data.end(), aValue.begin(), aValue.end());
        iModule.data.push_back(0u); // (strings are also NUL terminated, so runtime plugins can use them in place)
        iModule.strings[aValue] = address;
        return address;
    }

    fragment compiler::materialize(operand_type const& aOperand, std::optional<type> aTargetType)
    {
        using bytecode::opcode;
        if (iDeclarationPass)
        {
            fragment dummy;
            dummy.valueType = aTargetType.value_or(type::I32);
            if (aOperand.holds_alternative<i_fragment>())
                dummy.fragmentKind = aOperand.get<i_fragment>().kind() == fragment_kind::Argument ? fragment_kind::Argument : fragment_kind::Value;
            return dummy;
        }
        if (aOperand.holds_alternative<i_fragment>())
        {
            fragment result{ aOperand.get<i_fragment>() };
            if (result.kind() != fragment_kind::Value && result.kind() != fragment_kind::Argument)
                throw std::runtime_error("expression expected");
            if (aTargetType && result.valueType != *aTargetType)
                coerce(result, *aTargetType);
            return result;
        }
        fragment result;
        auto& code = result.fragmentCode.as_std_vector();
        i_symbol_table_entry* symbol = nullptr;
        if (aOperand.holds_alternative<i_symbol_reference>())
        {
            auto const& reference = aOperand.get<i_symbol_reference>();
            bool const late = language() != nullptr && language()->late_binding();
            if (late && !state().function)
                ensure_function(); // (top-level statements: the variables of the entry point are declared)
            // (a late bound name is that of the function being compiled)
            symbol = find_symbol(late ? current_scope() : reference.scope(), neolib::string_view{ reference.name() });
            if (late && (symbol == nullptr || symbol->type() == type::Function))
                if (auto const* value = language()->unbound_name(neolib::string_view{ reference.name() }))
                {
                    // (a name that is not a variable: the language's (e.g. a global, a builtin, ...))
                    result = fragment{ *value };
                    if (aTargetType && result.valueType != *aTargetType)
                        coerce(result, *aTargetType);
                    return result;
                }
            if (symbol == nullptr)
                throw std::runtime_error("undefined identifier '" + reference.name().to_std_string() + "'");
        }
        else
        {
            std::visit([&](auto const& d)
                {
                    using vt = std::decay_t<decltype(d)>;
                    if constexpr (!std::is_same_v<vt, std::monostate>)
                        if (d.symbol().has_value())
                            symbol = d.symbol().value();
                }, std::get<data_type>(aOperand));
        }
        if (symbol != nullptr)
        {
            if (symbol->type() == type::Function)
                throw std::runtime_error("function used as a value");
            put_op(code, is_global(*symbol) ? opcode::GlobalGet : opcode::LocalGet);
            put_u32(code, symbol->index());
            result.valueType = symbol->type();
            result.fragmentLvalue = symbol;
        }
        else
        {
            auto const value = get_immediate(aOperand);
            type const target = aTargetType.value_or(
                value.t == type::Ibig ? (value.i >= std::numeric_limits<std::int32_t>::min() && value.i <= std::numeric_limits<std::int32_t>::max() ? type::I32 : type::I64) :
                value.t == type::Fbig ? type::F64 : value.t);
            if (value.t == type::String)
            {
                if (target != type::String)
                    throw std::runtime_error("cannot convert string to " + type_name(target));
                put_op(code, opcode::I32Const);
                put_s64(code, static_cast<std::int32_t>(string_literal(value.s)));
            }
            else if (target == type::String)
                throw std::runtime_error("cannot convert " + type_name(value.t) + " to string");
            else if (target == type::F32)
            {
                put_op(code, opcode::F32Const);
                put_raw(code, static_cast<float>(is_float(value.t) ? value.f : static_cast<double>(value.i)));
            }
            else if (target == type::F64 || target == type::Fbig)
            {
                put_op(code, opcode::F64Const);
                put_raw(code, is_float(value.t) ? value.f : static_cast<double>(value.i));
            }
            else if (target == type::Integer)
            {
                // (a small integer: shifted left one bit)
                std::int64_t const i = is_float(value.t) ? static_cast<std::int64_t>(value.f) : value.t == type::Boolean ? (value.i != 0 ? 1 : 0) : value.i;
                if (i < -(std::int64_t{ 1 } << 62) || i >= (std::int64_t{ 1 } << 62))
                    throw std::runtime_error("integer literal out of range: " + std::to_string(i));
                put_op(code, opcode::I64Const);
                put_s64(code, static_cast<std::int64_t>(static_cast<std::uint64_t>(i) << 1u));
            }
            else
            {
                std::int64_t const i = is_float(value.t) ? static_cast<std::int64_t>(value.f) : value.i;
                if (is_64(target))
                {
                    put_op(code, opcode::I64Const);
                    put_s64(code, i);
                }
                else
                {
                    put_op(code, opcode::I32Const);
                    put_s64(code, target == type::Boolean ? ((is_float(value.t) ? value.f != 0.0 : value.i != 0) ? 1 : 0) : static_cast<std::int32_t>(i));
                }
            }
            result.valueType = target;
            return result;
        }
        if (aTargetType && result.valueType != *aTargetType)
            coerce(result, *aTargetType);
        return result;
    }

    void compiler::coerce(fragment& aValue, type aTarget)
    {
        // a value converted to another type
        if (aValue.valueType == aTarget)
            return;
        auto* const languageCompiler = language();
        if (auto const* converted = languageCompiler != nullptr ? languageCompiler->convert_value(aValue, aTarget) : nullptr)
            aValue = fragment{ *converted };
        else
            convert(aValue.fragmentCode.as_std_vector(), aValue.valueType, aTarget);
        aValue.valueType = aTarget;
    }

    fragment compiler::pop_value()
    {
        if (state().operandStack.empty())
            throw std::runtime_error("expression expected");
        auto const operand = state().operandStack.back();
        if (operand.holds_alternative<i_fragment>() && operand.get<i_fragment>().kind() != fragment_kind::Value)
            throw std::runtime_error("expression expected");
        state().operandStack.pop_back();
        return materialize(operand);
    }

    std::vector<fragment> compiler::pop_fragments(neolib::i_string_view const& aSource, fragment_kind aKind)
    {
        std::vector<fragment> result;
        auto& stack = state().operandStack;
        while (stack.size() > (state().function ? current_function().operandBase : 0u) &&
            stack.back().holds_alternative<i_fragment>() &&
            stack.back().get<i_fragment>().kind() == aKind &&
            within(stack.back().get<i_fragment>().source(), aSource))
        {
            result.insert(result.begin(), fragment{ stack.back().get<i_fragment>() });
            stack.pop_back();
        }
        return result;
    }

    void compiler::declare_import(i_function_signature const& aSignature)
    {
        define_symbol(aSignature.name(), type::Function, neolib::string_view{});
        auto& function = iModule.functions[*state().pendingFunction];
        function.imported = true;
        function.signature = aSignature;
        function.fromImportedFragment = state().fragment->imported();
        state().pendingFunction = std::nullopt;
    }

    void compiler::set_entry_point()
    {
        if (state().function)
            current_function().entry = true;
        else
            state().pendingEntry = true;
    }

    void compiler::emit_language_construct(neolib::i_string_view const& aLanguage, std::uint32_t aConstruct, neolib::i_string_view const& aSource)
    {
        if (auto* const languageCompiler = language(aLanguage.to_std_string()))
            languageCompiler->emit_construct(aConstruct, aSource);
        else
            throw std::runtime_error("no code generation for constructs of language '" + aLanguage.to_std_string() + "' (no compiler plugin)");
    }

    void compiler::emit_operation(operation aOperation)
    {
        using bytecode::opcode;
        bool const unary = (aOperation == operation::Negate || aOperation == operation::LogicalNot);
        if (state().operandStack.size() < (unary ? 1u : 2u))
            throw std::runtime_error("operand expected");
        auto const rhs = state().operandStack.back();
        state().operandStack.pop_back();
        std::optional<operand_type> lhs;
        if (!unary)
        {
            lhs = state().operandStack.back();
            state().operandStack.pop_back();
        }
        if (iDeclarationPass)
        {
            fragment dummy;
            dummy.valueType = type::I32;
            push_operand(operand_type{ dummy });
            return;
        }
        auto operand_type_of = [&](operand_type const& aOperand) -> type
        {
            if (aOperand.holds_alternative<i_fragment>())
                return aOperand.get<i_fragment>().value_type();
            if (aOperand.holds_alternative<i_symbol_reference>())
            {
                auto const& reference = aOperand.get<i_symbol_reference>();
                auto const* symbol = find_symbol(reference.scope(), neolib::string_view{ reference.name() });
                if (symbol == nullptr)
                    throw std::runtime_error("undefined identifier '" + reference.name().to_std_string() + "'");
                return symbol->type();
            }
            if (is_immediate(aOperand))
                return get_immediate(aOperand).t;
            return static_cast<type>(aOperand.get<i_data_type>().index());
        };
        bool const relational = (aOperation >= operation::Equal && aOperation <= operation::GreaterThanOrEqual);
        bool const logical = (aOperation >= operation::LogicalAnd && aOperation <= operation::LogicalNot);
        bool const floored = (aOperation == operation::Modulo || aOperation == operation::FloorDivide || aOperation == operation::TrueDivide);
        // constant folding (not of a floored division by zero, which is an error at run time, nor if the result is 
        // negative zero, infinite or NaN, which a literal cannot represent)
        auto const foldable = [&]()
        {
            if (unary || !is_immediate(*lhs))
                return true;
            auto const l = get_immediate(*lhs);
            auto const r = get_immediate(rhs);
            if (floored && l.t != type::String && r.t != type::String)
                return is_float(r.t) ? r.f != 0.0 : r.i != 0;
            return true;
        };
        if (is_immediate(rhs) && (unary || is_immediate(*lhs)) && foldable())
        {
            auto const r = get_immediate(rhs);
            immediate result;
            if (unary)
            {
                result = r;
                if (aOperation == operation::LogicalNot)
                {
                    result.t = type::Boolean;
                    result.i = (r.t == type::Fbig ? r.f == 0.0 : r.i == 0) ? 1 : 0;
                }
                else if (r.t == type::String || r.t == type::Boolean)
                    throw std::runtime_error("invalid operand for negation");
                else
                {
                    result.i = -r.i;
                    result.f = -r.f;
                }
            }
            else
            {
                auto const l = get_immediate(*lhs);
                if (l.t == type::String || r.t == type::String)
                {
                    if (l.t != r.t)
                        throw std::runtime_error("invalid string operation");
                    int const c = l.s.compare(r.s);
                    result.t = relational ? type::Boolean : type::String;
                    switch (aOperation)
                    {
                    case operation::Add: result.s = l.s + r.s; break;
                    case operation::Equal: result.i = c == 0; break;
                    case operation::NotEqual: result.i = c != 0; break;
                    case operation::LessThan: result.i = c < 0; break;
                    case operation::GreaterThan: result.i = c > 0; break;
                    case operation::LessThanOrEqual: result.i = c <= 0; break;
                    case operation::GreaterThanOrEqual: result.i = c >= 0; break;
                    default: throw std::runtime_error("invalid string operation");
                    }
                }
                else if (logical)
                {
                    result.t = type::Boolean;
                    bool const lb = is_float(l.t) ? l.f != 0.0 : l.i != 0;
                    bool const rb = is_float(r.t) ? r.f != 0.0 : r.i != 0;
                    result.i = (aOperation == operation::LogicalAnd ? (lb && rb) : (lb || rb)) ? 1 : 0;
                }
                else if (is_float(l.t) || is_float(r.t))
                {
                    double const lf = is_float(l.t) ? l.f : static_cast<double>(l.i);
                    double const rf = is_float(r.t) ? r.f : static_cast<double>(r.i);
                    result.t = relational ? type::Boolean : type::Fbig;
                    switch (aOperation)
                    {
                    case operation::Add: result.f = lf + rf; break;
                    case operation::Subtract: result.f = lf - rf; break;
                    case operation::Multiply: result.f = lf * rf; break;
                    case operation::Divide: 
                    case operation::TrueDivide: result.f = lf / rf; break;
                    case operation::FloorDivide: result.f = floored_divmod(lf, rf).first; break;
                    case operation::Modulo: result.f = floored_divmod(lf, rf).second; break;
                    case operation::Equal: result.i = lf == rf; break;
                    case operation::NotEqual: result.i = lf != rf; break;
                    case operation::LessThan: result.i = lf < rf; break;
                    case operation::GreaterThan: result.i = lf > rf; break;
                    case operation::LessThanOrEqual: result.i = lf <= rf; break;
                    case operation::GreaterThanOrEqual: result.i = lf >= rf; break;
                    default: break;
                    }
                }
                else if (aOperation == operation::TrueDivide)
                {
                    result.t = type::Fbig;
                    result.f = static_cast<double>(l.i) / static_cast<double>(r.i);
                }
                else
                {
                    result.t = relational ? type::Boolean : type::Ibig;
                    if ((aOperation == operation::Divide || aOperation == operation::FloorDivide || aOperation == operation::Modulo) && r.i == 0)
                        throw std::runtime_error("division by zero");
                    switch (aOperation)
                    {
                    case operation::FloorDivide: 
                        result.i = l.i / r.i - ((l.i % r.i != 0 && ((l.i < 0) != (r.i < 0))) ? 1 : 0); 
                        break;
                    case operation::Modulo: 
                        result.i = l.i % r.i; 
                        if (result.i != 0 && ((result.i < 0) != (r.i < 0))) 
                            result.i += r.i; 
                        break;
                    case operation::Add:
                    case operation::Subtract:
                    case operation::Multiply:
                        {
                            // (wrapping; trap_on_overflow: overflow (of 64 bits) is an error)
                            auto const ul = static_cast<std::uint64_t>(l.i);
                            auto const ur = static_cast<std::uint64_t>(r.i);
                            result.i = static_cast<std::int64_t>(aOperation == operation::Add ? ul + ur : aOperation == operation::Subtract ? ul - ur : ul * ur);
                            bool overflow;
                            if (aOperation == operation::Add)
                                overflow = ((l.i ^ result.i) & (r.i ^ result.i)) < 0;
                            else if (aOperation == operation::Subtract)
                                overflow = ((l.i ^ r.i) & (l.i ^ result.i)) < 0;
                            else
                                overflow = l.i != 0 && ((l.i == -1 && r.i == std::numeric_limits<std::int64_t>::min()) || 
                                    (r.i == -1 && l.i == std::numeric_limits<std::int64_t>::min()) || result.i / l.i != r.i);
                            if (overflow && overflow_traps())
                                throw std::runtime_error("integer overflow");
                        }
                        break;
                    case operation::Divide:
                        if (r.i == 0)
                            throw std::runtime_error("division by zero");
                        result.i = l.i / r.i; 
                        break;
                    case operation::Equal: result.i = l.i == r.i; break;
                    case operation::NotEqual: result.i = l.i != r.i; break;
                    case operation::LessThan: result.i = l.i < r.i; break;
                    case operation::GreaterThan: result.i = l.i > r.i; break;
                    case operation::LessThanOrEqual: result.i = l.i <= r.i; break;
                    case operation::GreaterThanOrEqual: result.i = l.i >= r.i; break;
                    default: break;
                    }
                }
            }
            if (!(is_float(result.t) && (!std::isfinite(result.f) || (result.f == 0.0 && std::signbit(result.f)))))
            {
                push_operand(operand_type{ make_immediate(result) });
                return;
            }
        }
        fragment result;
        auto& code = result.fragmentCode.as_std_vector();
        if (unary)
        {
            type const t = operand_type_of(rhs);
            if (aOperation == operation::LogicalNot)
            {
                append(result, materialize(rhs, type::Boolean));
                put_op(code, opcode::I32Eqz);
                result.valueType = type::Boolean;
            }
            else if (is_float(t))
            {
                append(result, materialize(rhs, t));
                put_op(code, t == type::F32 ? opcode::F32Neg : opcode::F64Neg);
                result.valueType = t;
            }
            else if (t == type::String || t == type::Boolean)
                throw std::runtime_error("invalid operand for negation");
            else if (overflow_traps() && (t == type::I32 || t == type::I64))
            {
                // (trap_on_overflow: checked negation, the VM's neos::checked_neg_<type> (the least integer has none))
                append(result, materialize(rhs, t));
                auto const id = runtime_function(is_64(t) ? "neos::checked_neg_i64" : "neos::checked_neg_i32", { t }, t);
                put_op(code, opcode::CallFunction);
                result.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), id });
                code.resize(code.size() + 5u);
                result.valueType = t;
            }
            else
            {
                put_op(code, is_64(t) ? opcode::I64Const : opcode::I32Const);
                put_s64(code, 0);
                append(result, materialize(rhs, t));
                put_op(code, is_64(t) ? opcode::I64Sub : opcode::I32Sub);
                if (!is_64(t))
                    convert(code, type::I32, t); // re-narrow (i8/u8/i16/u16)
                result.valueType = t;
            }
            push_operand(operand_type{ result });
            return;
        }
        type const lt = operand_type_of(*lhs);
        type const rt = operand_type_of(rhs);
        if (logical)
        {
            append(result, materialize(*lhs, type::Boolean));
            put_op(code, opcode::If);
            code.push_back(0x7Fu);
            if (aOperation == operation::LogicalAnd)
            {
                append(result, materialize(rhs, type::Boolean));
                put_op(code, opcode::Else);
                put_op(code, opcode::I32Const);
                put_s64(code, 0);
            }
            else
            {
                put_op(code, opcode::I32Const);
                put_s64(code, 1);
                put_op(code, opcode::Else);
                append(result, materialize(rhs, type::Boolean));
            }
            put_op(code, opcode::End);
            result.valueType = type::Boolean;
            push_operand(operand_type{ result });
            return;
        }
        if (lt == type::String || rt == type::String)
        {
            if (lt != rt)
                throw std::runtime_error("cannot combine " + type_name(lt) + " and " + type_name(rt));
            append(result, materialize(*lhs, type::String));
            auto right = materialize(rhs, type::String);
            // (a string followed by an integer formatted in decimal: the integer is formatted into the result)
            bool const integer = (aOperation == operation::Add && strip_call(right, "string_from_i64"));
            append(result, right);
            if (aOperation == operation::Add)
            {
                auto const id = integer ? runtime_function("neos::string_concat_i64", { type::String, type::I64 }, type::String) :
                    runtime_function("neos::string_concat", { type::String, type::String }, type::String);
                put_op(code, opcode::CallFunction);
                result.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), id });
                code.resize(code.size() + 5u);
                result.valueType = type::String;
            }
            else if (relational)
            {
                auto const id = runtime_function("neos::string_compare", { type::String, type::String }, type::I32);
                put_op(code, opcode::CallFunction);
                result.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), id });
                code.resize(code.size() + 5u);
                put_op(code, opcode::I32Const);
                put_s64(code, 0);
                static std::map<operation, opcode> const sCompare = {
                    { operation::Equal, opcode::I32Eq }, { operation::NotEqual, opcode::I32Ne },
                    { operation::LessThan, opcode::I32LtS }, { operation::GreaterThan, opcode::I32GtS },
                    { operation::LessThanOrEqual, opcode::I32LeS }, { operation::GreaterThanOrEqual, opcode::I32GeS } };
                put_op(code, sCompare.at(aOperation));
                result.valueType = type::Boolean;
            }
            else
                throw std::runtime_error("invalid string operation");
            push_operand(operand_type{ result });
            return;
        }
        if (!is_numeric(lt) || !is_numeric(rt))
            throw std::runtime_error("invalid operands: " + type_name(lt) + " and " + type_name(rt));
        type common;
        if (is_flexible(lt) && !is_flexible(rt))
            common = (lt == type::Fbig && !is_float(rt)) ? type::F64 : rt;
        else if (!is_flexible(lt) && is_flexible(rt))
            common = (rt == type::Fbig && !is_float(lt)) ? type::F64 : lt;
        else if (is_flexible(lt) && is_flexible(rt))
            common = (lt == type::Fbig || rt == type::Fbig) ? type::F64 : type::I32;
        else
            common = rank(rt) > rank(lt) ? rt : lt;
        if (common == type::Boolean && !relational)
            common = type::I32;
        // widen if an integer literal does not fit the other operand's type
        for (operand_type const* operand : { static_cast<operand_type const*>(&*lhs), &rhs })
            if (is_immediate(*operand) && get_immediate(*operand).t == type::Ibig && !is_float(common) && !is_64(common))
            {
                auto const i = get_immediate(*operand).i;
                bool const fits = is_unsigned(common) ? 
                    (i >= 0 && i <= static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) :
                    (i >= std::numeric_limits<std::int32_t>::min() && i <= std::numeric_limits<std::int32_t>::max());
                if (!fits)
                    common = type::I64;
            }
        if (rank(common) < 3u)
            common = is_unsigned(common) ? type::U32 : type::I32;
        if (aOperation == operation::TrueDivide || (floored && is_float(common)))
            common = type::F64;
        // (division by zero: the language's (e.g. an exception; aKind selects its message); otherwise a trap)
        auto const zero_division = [&](std::int32_t aKind)
        {
            auto& out = result.fragmentCode.as_std_vector();
            if (auto const* code = language() != nullptr ? language()->division_by_zero(aKind) : nullptr)
                append(result, *code);
            else
                put_op(out, opcode::Unreachable);
        };
        bool const floatOperand = is_float(lt) || is_float(rt);
        if (floored && aOperation != operation::TrueDivide)
        {
            // floor division / modulo with the sign of the divisor: operands are needed more than once so are held in temporaries
            bool const f = is_float(common);
            bool const w = is_64(common);
            bool const u = is_unsigned(common);
            if (!f && !u && is_immediate(rhs) && get_immediate(rhs).t != type::String && !is_float(get_immediate(rhs).t) && get_immediate(rhs).i > 0)
            {
                // a positive constant divisor: the remainder (of truncating division) only needs adjusting when it is negative
                std::int64_t const divisor = get_immediate(rhs).i;
                auto const constant = [&](std::int64_t aValue) { put_op(code, w ? opcode::I64Const : opcode::I32Const); put_s64(code, aValue); };
                std::int64_t const signShift = w ? 63 : 31;
                if ((divisor & (divisor - 1)) == 0)
                {
                    // a power of two: an arithmetic shift floors; masking gives the (non-negative) modulo
                    std::int64_t shift = 0;
                    while ((std::int64_t{ 1 } << shift) != divisor)
                        ++shift;
                    append(result, materialize(*lhs, common));
                    if (aOperation == operation::FloorDivide)
                    {
                        constant(shift);
                        put_op(code, w ? opcode::I64ShrS : opcode::I32ShrS);
                    }
                    else
                    {
                        constant(divisor - 1);
                        put_op(code, w ? opcode::I64And : opcode::I32And);
                    }
                }
                else
                {
                    auto const a = temporary(common);
                    auto const get = [&](std::uint32_t aLocal) { put_op(code, opcode::LocalGet); put_u32(code, aLocal); };
                    append(result, materialize(*lhs, common));
                    if (aOperation == operation::FloorDivide)
                    {
                        // (a / d) + ((a % d) >> sign): one less when the remainder is negative
                        put_op(code, opcode::LocalTee);
                        put_u32(code, a);
                        constant(divisor);
                        put_op(code, w ? opcode::I64DivS : opcode::I32DivS);
                        get(a);
                        constant(divisor);
                        put_op(code, w ? opcode::I64RemS : opcode::I32RemS);
                        constant(signShift);
                        put_op(code, w ? opcode::I64ShrS : opcode::I32ShrS);
                        put_op(code, w ? opcode::I64Add : opcode::I32Add);
                    }
                    else
                    {
                        // r + ((r >> sign) & d): d is added when the remainder is negative
                        constant(divisor);
                        put_op(code, w ? opcode::I64RemS : opcode::I32RemS);
                        put_op(code, opcode::LocalTee);
                        put_u32(code, a);
                        get(a);
                        constant(signShift);
                        put_op(code, w ? opcode::I64ShrS : opcode::I32ShrS);
                        constant(divisor);
                        put_op(code, w ? opcode::I64And : opcode::I32And);
                        put_op(code, w ? opcode::I64Add : opcode::I32Add);
                    }
                }
                result.valueType = common;
                push_operand(operand_type{ result });
                return;
            }
            auto const a = temporary(common);
            auto const b = temporary(common);
            append(result, materialize(*lhs, common));
            put_op(code, opcode::LocalSet);
            put_u32(code, a);
            append(result, materialize(rhs, common));
            put_op(code, opcode::LocalSet);
            put_u32(code, b);
            auto get = [&](std::uint32_t aLocal) { put_op(code, opcode::LocalGet); put_u32(code, aLocal); };
            if (f)
            {
                // float floor division / modulo (based on fmod, which is exact; as CPython)
                auto const set = [&](std::uint32_t aLocal) { put_op(code, opcode::LocalSet); put_u32(code, aLocal); };
                auto const constant = [&](double aValue) { put_op(code, opcode::F64Const); put_raw(code, aValue); };
                auto const m = temporary(type::F64);
                auto const d = temporary(type::F64);
                // division by zero
                get(b); constant(0.0); put_op(code, opcode::F64Eq);
                put_op(code, opcode::If);
                code.push_back(0x40u);
                zero_division(aOperation == operation::FloorDivide ? 2 : 3);
                put_op(code, opcode::End);
                // m = fmod(a, b); d = (a - m) / b
                auto const fmod = runtime_function("neos::fmod", { type::F64, type::F64 }, type::F64);
                get(a); get(b);
                put_op(code, opcode::CallFunction);
                result.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), fmod });
                code.resize(code.size() + 5u);
                set(m);
                get(a); get(m); put_op(code, opcode::F64Sub); get(b); put_op(code, opcode::F64Div); set(d);
                // if m != 0 then (if the signs of m and b differ then m += b, d -= 1) else m = copysign(0, b)
                get(m); constant(0.0); put_op(code, opcode::F64Ne);
                put_op(code, opcode::If);
                code.push_back(0x40u);
                get(b); constant(0.0); put_op(code, opcode::F64Lt);
                get(m); constant(0.0); put_op(code, opcode::F64Lt);
                put_op(code, opcode::I32Ne);
                put_op(code, opcode::If);
                code.push_back(0x40u);
                get(m); get(b); put_op(code, opcode::F64Add); set(m);
                get(d); constant(1.0); put_op(code, opcode::F64Sub); set(d);
                put_op(code, opcode::End);
                put_op(code, opcode::Else);
                constant(0.0); get(b); put_op(code, opcode::F64CopySign); set(m);
                put_op(code, opcode::End);
                if (aOperation == operation::FloorDivide)
                {
                    // if d != 0 then floor(d) (+1 if d - floor(d) > 0.5) else copysign(0, a / b)
                    get(d); constant(0.0); put_op(code, opcode::F64Ne);
                    put_op(code, opcode::If);
                    code.push_back(0x7Cu);
                    get(d); put_op(code, opcode::F64Floor); set(m);
                    get(m);
                    get(d); get(m); put_op(code, opcode::F64Sub); constant(0.5); put_op(code, opcode::F64Gt);
                    put_op(code, opcode::F64UConvertI32);
                    put_op(code, opcode::F64Add);
                    put_op(code, opcode::Else);
                    constant(0.0); get(a); get(b); put_op(code, opcode::F64Div); put_op(code, opcode::F64CopySign);
                    put_op(code, opcode::End);
                }
                else
                    get(m);
            }
            else if (u)
            {
                get(b); put_op(code, w ? opcode::I64Eqz : opcode::I32Eqz);
                put_op(code, opcode::If);
                code.push_back(0x40u);
                zero_division(aOperation == operation::FloorDivide ? 4 : 5);
                put_op(code, opcode::End);
                get(a); get(b);
                put_op(code, aOperation == operation::FloorDivide ? (w ? opcode::I64DivU : opcode::I32DivU) : (w ? opcode::I64RemU : opcode::I32RemU));
            }
            else
            {
                auto const zero = [&]() { put_op(code, w ? opcode::I64Const : opcode::I32Const); put_s64(code, 0); };
                auto const r = temporary(common);
                get(b); put_op(code, w ? opcode::I64Eqz : opcode::I32Eqz);
                put_op(code, opcode::If);
                code.push_back(0x40u);
                zero_division(aOperation == operation::FloorDivide ? 4 : 5);
                put_op(code, opcode::End);
                get(a); get(b);
                put_op(code, w ? opcode::I64RemS : opcode::I32RemS);
                put_op(code, opcode::LocalSet);
                put_u32(code, r);
                // adjust = (r != 0) && ((r < 0) != (b < 0))
                auto const adjust = [&]()
                {
                    get(r); zero(); put_op(code, w ? opcode::I64Ne : opcode::I32Ne);
                    get(r); get(b); put_op(code, w ? opcode::I64Xor : opcode::I32Xor); zero(); put_op(code, w ? opcode::I64LtS : opcode::I32LtS);
                    put_op(code, opcode::I32And);
                    if (w)
                        put_op(code, opcode::I64UConvertI32);
                };
                if (aOperation == operation::FloorDivide)
                {
                    get(a); get(b);
                    put_op(code, w ? opcode::I64DivS : opcode::I32DivS);
                    adjust();
                    put_op(code, w ? opcode::I64Sub : opcode::I32Sub);
                }
                else
                {
                    get(r);
                    adjust();
                    get(b);
                    put_op(code, w ? opcode::I64Mul : opcode::I32Mul);
                    put_op(code, w ? opcode::I64Add : opcode::I32Add);
                }
            }
            result.valueType = common;
            push_operand(operand_type{ result });
            return;
        }
        append(result, materialize(*lhs, common));
        append(result, materialize(rhs, common));
        if (aOperation == operation::TrueDivide)
        {
            // division by zero is an error (not infinity)
            auto const divisor = temporary(type::F64);
            put_op(code, opcode::LocalTee);
            put_u32(code, divisor);
            put_op(code, opcode::F64Const);
            put_raw(code, 0.0);
            put_op(code, opcode::F64Eq);
            put_op(code, opcode::If);
            code.push_back(0x40u);
            zero_division(floatOperand ? 1 : 0);
            put_op(code, opcode::End);
            put_op(code, opcode::LocalGet);
            put_u32(code, divisor);
        }
        bool const f = is_float(common);
        bool const w = is_64(common);
        bool const u = is_unsigned(common);
        if (!f && overflow_traps() && (aOperation == operation::Add || aOperation == operation::Subtract || aOperation == operation::Multiply))
        {
            // (trap_on_overflow: checked arithmetic, the VM's neos::checked_<op>_<type>)
            std::string const name = std::string{ "neos::checked_" } + (aOperation == operation::Add ? "add_" : aOperation == operation::Subtract ? "sub_" : "mul_") +
                (u ? "u" : "i") + (w ? "64" : "32");
            auto const id = runtime_function(name, { common, common }, common);
            put_op(code, opcode::CallFunction);
            result.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), id });
            code.resize(code.size() + 5u);
            result.valueType = common;
            push_operand(operand_type{ result });
            return;
        }
        opcode op = opcode::Nop;
        switch (aOperation)
        {
        case operation::Add: op = f ? (common == type::F32 ? opcode::F32Add : opcode::F64Add) : (w ? opcode::I64Add : opcode::I32Add); break;
        case operation::Subtract: op = f ? (common == type::F32 ? opcode::F32Sub : opcode::F64Sub) : (w ? opcode::I64Sub : opcode::I32Sub); break;
        case operation::Multiply: op = f ? (common == type::F32 ? opcode::F32Mul : opcode::F64Mul) : (w ? opcode::I64Mul : opcode::I32Mul); break;
        case operation::Divide: 
        case operation::TrueDivide: op = f ? (common == type::F32 ? opcode::F32Div : opcode::F64Div) : (w ? (u ? opcode::I64DivU : opcode::I64DivS) : (u ? opcode::I32DivU : opcode::I32DivS)); break;
        case operation::Equal: op = f ? (common == type::F32 ? opcode::F32Eq : opcode::F64Eq) : (w ? opcode::I64Eq : opcode::I32Eq); break;
        case operation::NotEqual: op = f ? (common == type::F32 ? opcode::F32Ne : opcode::F64Ne) : (w ? opcode::I64Ne : opcode::I32Ne); break;
        case operation::LessThan: op = f ? (common == type::F32 ? opcode::F32Lt : opcode::F64Lt) : (w ? (u ? opcode::I64LtU : opcode::I64LtS) : (u ? opcode::I32LtU : opcode::I32LtS)); break;
        case operation::GreaterThan: op = f ? (common == type::F32 ? opcode::F32Gt : opcode::F64Gt) : (w ? (u ? opcode::I64GtU : opcode::I64GtS) : (u ? opcode::I32GtU : opcode::I32GtS)); break;
        case operation::LessThanOrEqual: op = f ? (common == type::F32 ? opcode::F32Le : opcode::F64Le) : (w ? (u ? opcode::I64LeU : opcode::I64LeS) : (u ? opcode::I32LeU : opcode::I32LeS)); break;
        case operation::GreaterThanOrEqual: op = f ? (common == type::F32 ? opcode::F32Ge : opcode::F64Ge) : (w ? (u ? opcode::I64GeU : opcode::I64GeS) : (u ? opcode::I32GeU : opcode::I32GeS)); break;
        default: break;
        }
        put_op(code, op);
        result.valueType = relational ? type::Boolean : common;
        push_operand(operand_type{ result });
    }

    void compiler::emit_statement(neolib::i_string_view const& aSource)
    {
        auto& stack = state().operandStack;
        if (stack.size() <= (state().function ? current_function().operandBase : 0u))
            return;
        auto const& top = stack.back();
        if (top.holds_alternative<i_fragment>() && top.get<i_fragment>().kind() != fragment_kind::Value)
        {
            if (top.get<i_fragment>().kind() == fragment_kind::Statement && top.get<i_fragment>().source().empty())
            {
                fragment statement{ top.get<i_fragment>() };
                statement.fragmentSource = aSource;
                stack.back() = operand_type{ statement };
            }
            return;
        }
        auto value = pop_value();
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        append(statement, value);
        if (value.valueType != type::Void)
            put_op(statement.fragmentCode.as_std_vector(), bytecode::opcode::Drop);
        push_operand(operand_type{ statement });
    }

    void compiler::emit_block(neolib::i_string_view const& aSource)
    {
        auto block = make_fragment(fragment_kind::Statement, type::Void, aSource);
        for (auto const& statement : pop_fragments(aSource, fragment_kind::Statement))
            append(block, statement);
        push_operand(operand_type{ block });
        retire_symbols(aSource);
    }

    void compiler::emit_return(neolib::i_string_view const& aSource)
    {
        using bytecode::opcode;
        auto& function = current_function();
        auto const& signature = function.scope->function_signature();
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        std::string_view text = aSource.to_std_string_view();
        while (!text.empty() && (std::isspace(static_cast<unsigned char>(text.back())) || text.back() == ';'))
            text.remove_suffix(1u);
        bool const bare = (text == "return");
        if (function.inferred)
        {
            // the type of a function's result is that of the values it returns
            if (iDeclarationPass)
            {
                if (!bare && !iInference.results.contains(function.name))
                    iInference.results[function.name] = type::F64;
                if (!bare)
                    state().operandStack.pop_back();
                push_operand(operand_type{ statement });
                return;
            }
            auto const t = bare ? type::Void : language() != nullptr ? language()->inferred_type(state().operandStack.back()) : 
                state().operandStack.back().holds_alternative<i_fragment>() ? state().operandStack.back().get<i_fragment>().value_type() : type::UNKNOWN;
            auto const returned = iInference.returned.find(function.name);
            auto const describe = [](type aType) -> std::string
            {
                return aType == type::Void ? "no value" : aType == type::String ? "a string" : aType == type::Boolean ? "a boolean" : "a number";
            };
            if (returned != iInference.returned.end() && returned->second != t)
                throw std::runtime_error("'" + function.name + "' returns " + describe(returned->second) + " and " + describe(t));
            iInference.returned[function.name] = t;
            auto const result = iInference.results.find(function.name);
            if (result == iInference.results.end() || result->second != t)
            {
                infer("result of '" + function.name + "'", result != iInference.results.end() ? std::optional<type>{ result->second } : std::nullopt, t);
                iInference.results[function.name] = t;
            }
            if (signature.return_type() != t)
            {
                // (code compiled with types that have since changed is replaced by the next compilation pass)
                if (!bare)
                    state().operandStack.pop_back();
                put_op(statement.fragmentCode.as_std_vector(), opcode::Unreachable);
                push_operand(operand_type{ statement });
                return;
            }
        }
        if (signature.return_type() == type::Void && !bare)
            throw std::runtime_error("procedure cannot return a value");
        if (signature.return_type() != type::Void && bare)
            throw std::runtime_error("function must return a value");
        if (!bare)
        {
            if (state().operandStack.empty())
                throw std::runtime_error("expression expected");
            auto const operand = state().operandStack.back();
            state().operandStack.pop_back();
            append(statement, materialize(operand, signature.return_type()));
        }
        auto& code = statement.fragmentCode.as_std_vector();
        if (signature.parameters().has_value())
            for (auto const& parameter : signature.parameters().value())
                if (parameter.direction() != parameter_direction::In)
                {
                    put_op(code, opcode::LocalGet);
                    put_u32(code, find_symbol(current_scope(), neolib::string_view{ parameter.parameter_name() })->index());
                }
        put_op(code, opcode::Return);
        push_operand(operand_type{ statement });
    }

    void compiler::emit_assignment(neolib::i_string_view const& aTarget, neolib::i_string_view const& aSource)
    {
        auto* const symbol = find_symbol(current_scope(), aTarget);
        if (symbol == nullptr)
            throw std::runtime_error("undefined identifier '" + aTarget.to_std_string() + "'");
        if (symbol->type() == type::Function)
            throw std::runtime_error("cannot assign to function '" + aTarget.to_std_string() + "'");
        if (state().operandStack.empty())
            throw std::runtime_error("expression expected");
        auto const operand = state().operandStack.back();
        state().operandStack.pop_back();
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        if (iSymbolInfo[symbol].constant)
            throw std::runtime_error("assignment to constant variable '" + aTarget.to_std_string() + "'");
        append(statement, materialize(operand, symbol->type()));
        put_op(statement.fragmentCode.as_std_vector(), is_global(*symbol) ? bytecode::opcode::GlobalSet : bytecode::opcode::LocalSet);
        put_u32(statement.fragmentCode.as_std_vector(), symbol->index());
        push_operand(operand_type{ statement });
    }

    void compiler::emit_argument(neolib::i_string_view const& aSource)
    {
        if (state().operandStack.empty())
            throw std::runtime_error("argument expected");
        auto const operand = state().operandStack.back();
        if (operand.holds_alternative<i_fragment>() && operand.get<i_fragment>().kind() != fragment_kind::Value)
            throw std::runtime_error("argument expected");
        state().operandStack.pop_back();
        // (an integer argument's range (that the language knows))
        std::int64_t low = 0, high = 0;
        bool const range = language() != nullptr && language()->value_range(operand, low, high);
        // (the language's type of the argument (e.g. an int literal is an integer of any size (type::Integer)))
        auto const argumentType = language() != nullptr ? language()->argument_type(operand) : type::UNKNOWN;
        auto argument = argumentType != type::UNKNOWN ? materialize(operand, argumentType) : materialize(operand);
        argument.fragmentKind = fragment_kind::Argument;
        argument.hasRange = range;
        argument.rangeLow = range ? low : 0;
        argument.rangeHigh = range ? high : 0;
        argument.fragmentSource = aSource;
        push_operand(operand_type{ argument });
    }

    namespace
    {
        // -1: not viable; otherwise the sum over arguments of 3 (same type), 2 (same category), 1 (numeric conversion)
        std::int32_t overload_score(i_function_signature const& aSignature, std::vector<fragment> const& aArguments)
        {
            std::size_t const parameterCount = aSignature.parameters().has_value() ? aSignature.parameters().value().size() : 0u;
            if (parameterCount != aArguments.size())
                return -1;
            std::int32_t score = 0;
            // (an integer of any size (type::Integer) is a number, an integer)
            auto const numeric = [](type t) { return is_numeric(t) || t == type::Integer; };
            for (std::size_t i = 0u; i < parameterCount; ++i)
            {
                auto const p = aSignature.parameters().value()[i].parameter_type();
                auto const a = aArguments[i].valueType;
                if (p == a)
                    score += 3;
                else if (p == type::Object || a == type::Object)
                    ; // (an object parameter: the argument is converted to an object; an object argument: it is converted)
                else if (p == type::String || a == type::String)
                    return -1;
                else if (numeric(p) && numeric(a))
                    score += (is_float(p) == is_float(a) && (p == type::Boolean) == (a == type::Boolean)) ? 2 : 1;
                else
                    return -1;
            }
            return score;
        }
    }

    void compiler::emit_call(neolib::i_string_view const& aFunctionName, neolib::i_string_view const& aSource)
    {
        using bytecode::opcode;
        auto arguments = pop_fragments(aSource, fragment_kind::Argument);
        if (iDeclarationPass)
        {
            fragment dummy;
            dummy.valueType = type::I32;
            push_operand(operand_type{ dummy });
            return;
        }
        std::vector<std::string> path;
        {
            std::string name = aFunctionName.to_std_string();
            name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }), name.end());
            std::size_t pos = 0u;
            for (auto next = name.find("::"); next != std::string::npos; next = name.find("::", pos))
            {
                path.push_back(name.substr(pos, next - pos));
                pos = next + 2u;
            }
            path.push_back(name.substr(pos));
        }
        auto signature_of = [&](wasm_function const& aFunction) -> i_function_signature const&
        {
            return aFunction.scope != nullptr ? aFunction.scope->function_signature() : static_cast<i_function_signature const&>(aFunction.signature);
        };
        std::string joined;
        for (auto const& part : path)
            joined += (joined.empty() ? "" : "::") + part;
        std::optional<std::uint32_t> chosen;
        std::optional<function_signature> forwardSignature;
        for (i_scope const* scope = &current_scope(); scope != nullptr && !chosen; scope = scope->has_parent() ? &scope->parent() : nullptr)
        {
            std::optional<std::uint32_t> best;
            std::int32_t bestScore = -1;
            // functions already defined in this scope
            i_scope const* target = scope;
            for (std::size_t i = 0u; target != nullptr && i + 1u < path.size(); ++i)
            {
                i_scope const* next = nullptr;
                for (auto const& child : target->children())
                    if (child->type() == scope_type::Namespace && child->name().to_std_string() == path[i])
                        next = &*child;
                target = next;
            }
            auto const symbols = target != nullptr ? state().program->symbolTable.find(target) : state().program->symbolTable.end();
            if (symbols != state().program->symbolTable.end())
                for (auto const& symbol : symbols->second())
                {
                    if (symbol.first().to_std_string() != path.back() || symbol.second().type() != type::Function)
                        continue;
                    auto const score = overload_score(signature_of(iModule.functions[symbol.second().index()]), arguments);
                    // (a function defined in the program is preferred to a host function with the same parameters)
                    if (score > bestScore || (score == bestScore && score >= 0 && best && 
                        iModule.functions[*best].stub && !iModule.functions[symbol.second().index()].stub))
                    {
                        best = symbol.second().index();
                        bestScore = score;
                    }
                }
            // functions declared in this scope (collected by the declaration pass) that are a better match, i.e. defined later
            std::string prefix = scope->qualified_name().to_std_string();
            while (prefix.starts_with("::"))
                prefix.erase(0, 2);
            std::string const qualified = prefix.empty() ? joined : prefix + "::" + joined;
            std::optional<std::size_t> forward;
            for (std::size_t i = 0u; i < iForwardDeclarations.size(); ++i)
            {
                auto const& declaration = iForwardDeclarations[i];
                if (declaration.name != qualified)
                    continue;
                auto const score = overload_score(declaration.signature, arguments);
                if (score > bestScore || (score == bestScore && score >= 0 && !forward && best && 
                    iModule.functions[*best].stub && !declaration.stub))
                {
                    forward = i;
                    bestScore = score;
                }
            }
            if (forward)
            {
                auto const& declaration = iForwardDeclarations[*forward];
                forwardSignature = declaration.signature;
                std::vector<type> parameterTypes;
                if (declaration.signature.parameters().has_value())
                    for (auto const& p : declaration.signature.parameters().value())
                        parameterTypes.push_back(p.parameter_type());
                chosen = DeferredCall | static_cast<std::uint32_t>(iModule.deferredCalls.size());
                iModule.deferredCalls.emplace_back(declaration.name, parameterTypes);
            }
            else if (best)
                chosen = best;
        }
        if (!chosen)
            throw std::runtime_error("no matching function for call to '" + aFunctionName.to_std_string() + "' with " + std::to_string(arguments.size()) + " argument(s)");
        auto const& signature = forwardSignature ? static_cast<i_function_signature const&>(*forwardSignature) : signature_of(iModule.functions[*chosen]);
        type const returnType = signature.return_type();
        auto call = make_fragment(returnType == type::Void ? fragment_kind::Statement : fragment_kind::Value, returnType, returnType == type::Void ? neolib::string_view{ aSource } : neolib::string_view{});
        std::vector<std::pair<i_symbol_table_entry*, type>> outputs;
        for (std::size_t i = 0u; i < arguments.size(); ++i)
        {
            auto const& parameter = signature.parameters().value()[i];
            auto& argument = arguments[i];
            if (parameter.direction() != parameter_direction::In)
            {
                if (argument.fragmentLvalue == nullptr)
                    throw std::runtime_error("argument " + std::to_string(i + 1u) + " of '" + aFunctionName.to_std_string() + "' must be a variable");
                outputs.emplace_back(argument.fragmentLvalue, parameter.parameter_type());
            }
            if (parameter.direction() != parameter_direction::Out)
            {
                if (argument.valueType != parameter.parameter_type())
                    coerce(argument, parameter.parameter_type());
                append(call, argument);
            }
        }
        auto& code = call.fragmentCode.as_std_vector();
        if (outputs.empty() && (*chosen & DeferredCall) == 0u && inline_call(call, iModule.functions[*chosen], arguments.size()))
        {
            push_operand(operand_type{ call });
            return;
        }
        if ((*chosen & DeferredCall) == 0u && iModule.functions[*chosen].intrinsic)
        {
            // (a string intrinsic: its header's field)
            put_op(code, opcode::I32LoadMem);
            put_u32(code, 2u);
            put_u32(code, *iModule.functions[*chosen].intrinsic);
            convert(code, type::I32, returnType);
            push_operand(operand_type{ call });
            return;
        }
        put_op(code, opcode::CallFunction);
        call.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), *chosen });
        code.resize(code.size() + 5u);
        for (auto o = outputs.rbegin(); o != outputs.rend(); ++o)
        {
            convert(code, o->second, o->first->type());
            put_op(code, opcode::LocalSet);
            put_u32(code, o->first->index());
        }
        push_operand(operand_type{ call });
    }

    bool compiler::strip_call(fragment& aFragment, std::string_view aVmFunction)
    {
        // if a fragment's code ends with a call to the VM's function neos::aVmFunction, remove that call
        auto& code = aFragment.fragmentCode.as_std_vector();
        auto& relocations = aFragment.fragmentRelocations;
        if (relocations.empty() || code.size() < 6u || relocations.back().first() != code.size() - 5u || code[code.size() - 6u] != 0x10u)
            return false;
        auto const id = relocations.back().second();
        if ((id & (DeferredCall | BranchTarget)) != 0u || id >= iModule.functions.size())
            return false;
        auto const& function = iModule.functions[id];
        bool const match = function.imported && (function.name == "neos::" + std::string{ aVmFunction } ||
            (function.importNamespace == "neos" && function.name.starts_with("__neos_" + std::string{ aVmFunction } + "(")));
        if (!match)
            return false;
        code.resize(code.size() - 6u);
        relocations.pop_back();
        return true;
    }

    bool compiler::inline_call(fragment& aCall, wasm_function const& aCallee, std::size_t aParameterCount)
    {
        // a function whose body is its parameters, in order, each used once, then code that uses no locals and does not 
        // branch (e.g. a function that calls another with the same arguments) is inlined: its arguments are already on the 
        // stack, so the call is replaced by that code
        if (!aCallee.compiled || aCallee.imported || aCallee.intrinsic)
            return false;
        auto const& body = aCallee.body;
        std::size_t p = 0u;
        auto const leb = [&]() -> std::optional<std::uint64_t>
        {
            std::uint64_t value = 0u;
            for (std::uint32_t shift = 0u; p < body.size() && shift < 64u; shift += 7u)
            {
                auto const b = body[p++];
                value |= static_cast<std::uint64_t>(b & 0x7Fu) << shift;
                if ((b & 0x80u) == 0u)
                    return value;
            }
            return {};
        };
        for (std::size_t i = 0u; i < aParameterCount; ++i)
        {
            if (p >= body.size() || body[p++] != 0x20u || leb() != i)
                return false;
        }
        auto const tail = p;
        std::size_t end = body.size();
        std::size_t calls = 0u;
        while (p < body.size())
        {
            auto const op = body[p++];
            if (op == 0x10u)
            {
                // (a call: its index is a relocation)
                p += 5u;
                ++calls;
            }
            else if (op == 0x28u)
            {
                // (i32.load)
                if (!leb() || !leb())
                    return false;
            }
            else if (op == 0x0Fu)
            {
                // (return: the end, possibly followed by unreachable)
                end = p - 1u;
                if (p < body.size() && body[p] == 0x00u)
                    ++p;
                if (p != body.size())
                    return false;
            }
            else if (!(op >= 0x45u && op <= 0xC4u))
                return false;
        }
        if (calls != aCallee.relocations.size())
            return false;
        auto& code = aCall.fragmentCode.as_std_vector();
        auto const base = static_cast<std::uint32_t>(code.size());
        code.insert(code.end(), body.begin() + static_cast<std::ptrdiff_t>(tail), body.begin() + static_cast<std::ptrdiff_t>(end));
        for (auto const& r : aCallee.relocations)
        {
            if (r.first < tail || r.first + 5u > end)
                return false;
            aCall.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ base + static_cast<std::uint32_t>(r.first - tail), r.second });
        }
        return true;
    }

    void compiler::wrap_expression_body(neolib::i_string_view const& aSource)
    {
        // a body consisting of a single expression statement whose statement node was merged into its parent
        auto& stack = state().operandStack;
        if (stack.size() <= (state().function ? current_function().operandBase : 0u))
            return;
        auto const& top = stack.back();
        if (top.holds_alternative<i_fragment>() && top.get<i_fragment>().kind() != fragment_kind::Value)
            return;
        emit_statement(aSource);
    }

    void compiler::emit_if(neolib::i_string_view const& aSource)
    {
        using bytecode::opcode;
        auto elseArm = pop_fragments(aSource, fragment_kind::Else);
        auto elseIfArms = pop_fragments(aSource, fragment_kind::ElseIf);
        wrap_expression_body(aSource);
        auto body = pop_fragments(aSource, fragment_kind::Statement);
        if (body.size() != 1u)
            throw std::runtime_error("if statement body expected");
        if (state().operandStack.empty())
            throw std::runtime_error("if condition expected");
        auto const condition = state().operandStack.back();
        state().operandStack.pop_back();
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        append(statement, materialize(condition, type::Boolean));
        put_op(statement.fragmentCode.as_std_vector(), opcode::If);
        statement.fragmentCode.push_back(0x40u);
        append(statement, body[0]);
        for (auto const& arm : elseIfArms)
        {
            put_op(statement.fragmentCode.as_std_vector(), opcode::Else);
            append(statement, arm);
        }
        if (!elseArm.empty())
        {
            put_op(statement.fragmentCode.as_std_vector(), opcode::Else);
            append(statement, elseArm[0]);
        }
        for (std::size_t i = 0u; i <= elseIfArms.size(); ++i)
            put_op(statement.fragmentCode.as_std_vector(), opcode::End);
        push_operand(operand_type{ statement });
    }

    void compiler::emit_else_if(neolib::i_string_view const& aSource)
    {
        using bytecode::opcode;
        wrap_expression_body(aSource);
        auto body = pop_fragments(aSource, fragment_kind::Statement);
        if (body.size() != 1u)
            throw std::runtime_error("else if statement body expected");
        if (state().operandStack.empty())
            throw std::runtime_error("else if condition expected");
        auto const condition = state().operandStack.back();
        state().operandStack.pop_back();
        auto arm = make_fragment(fragment_kind::ElseIf, type::Void, aSource);
        append(arm, materialize(condition, type::Boolean));
        put_op(arm.fragmentCode.as_std_vector(), opcode::If);
        arm.fragmentCode.push_back(0x40u);
        append(arm, body[0]);
        push_operand(operand_type{ arm });
    }

    void compiler::emit_else(neolib::i_string_view const& aSource)
    {
        wrap_expression_body(aSource);
        auto arm = make_fragment(fragment_kind::Else, type::Void, aSource);
        for (auto const& statement : pop_fragments(aSource, fragment_kind::Statement))
            append(arm, statement);
        push_operand(operand_type{ arm });
    }

    void compiler::emit_loop(loop_type aLoopType, neolib::i_string_view const& aSource)
    {
        using bytecode::opcode;
        auto pop_statement = [&]()
        {
            auto statements = pop_fragments(aSource, fragment_kind::Statement);
            if (statements.empty())
                throw std::runtime_error("loop statement expected");
            for (std::size_t i = 0u; i + 1u < statements.size(); ++i)
                push_operand(operand_type{ statements[i] });
            return statements.back();
        };
        auto pop_condition = [&]()
        {
            if (state().operandStack.empty())
                throw std::runtime_error("loop condition expected");
            auto const condition = state().operandStack.back();
            state().operandStack.pop_back();
            return materialize(condition, type::Boolean);
        };
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        auto& code = statement.fragmentCode.as_std_vector();
        auto begin_loop = [&]()
        {
            put_op(code, opcode::Block);
            code.push_back(0x40u);
            put_op(code, opcode::Loop);
            code.push_back(0x40u);
        };
        auto end_loop = [&]()
        {
            put_op(code, opcode::End);
            put_op(code, opcode::End);
        };
        auto exit_unless = [&](fragment const& aCondition)
        {
            append(statement, aCondition);
            put_op(code, opcode::I32Eqz);
            put_op(code, opcode::BrIf);
            put_u32(code, 1u);
        };
        switch (aLoopType)
        {
        case loop_type::While:
            {
                auto body = pop_statement();
                auto condition = pop_condition();
                begin_loop();
                exit_unless(condition);
                append(statement, body);
                put_op(code, opcode::Br);
                put_u32(code, 0u);
                end_loop();
            }
            break;
        case loop_type::DoWhile:
            {
                auto condition = pop_condition();
                auto body = pop_statement();
                begin_loop();
                append(statement, body);
                append(statement, condition);
                put_op(code, opcode::BrIf);
                put_u32(code, 0u);
                end_loop();
            }
            break;
        case loop_type::For:
            {
                auto body = pop_statement();
                auto step = pop_statement();
                auto condition = pop_condition();
                auto init = pop_fragments(aSource, fragment_kind::Statement);
                for (auto const& i : init)
                    append(statement, i);
                begin_loop();
                exit_unless(condition);
                append(statement, body);
                append(statement, step);
                put_op(code, opcode::Br);
                put_u32(code, 0u);
                end_loop();
            }
            break;
        }
        push_operand(operand_type{ statement });
        retire_symbols(aSource);
    }

    std::uint32_t compiler::temporary(type aType)
    {
        ensure_function();
        auto& function = current_function();
        std::uint32_t inParameters = 0u;
        auto const& signature = function.scope->function_signature();
        if (signature.parameters().has_value())
            for (auto const& parameter : signature.parameters().value())
                if (parameter.direction() != parameter_direction::Out)
                    ++inParameters;
        auto const index = inParameters + static_cast<std::uint32_t>(function.locals.size());
        function.locals.push_back(aType);
        return index;
    }

    // --- scoping by significant whitespace

    std::optional<std::size_t> compiler::line_indent(neolib::i_string_view const& aSource) const
    {
        // the column of a construct that starts a line (nullopt if something precedes it on its line)
        auto const fragmentSource = current_fragment().source().to_std_string_view();
        char const* const begin = fragmentSource.data();
        char const* position = aSource.to_std_string_view().data();
        if (position < begin || position > begin + fragmentSource.size())
            return std::nullopt;
        std::size_t column = 0u;
        char const* lineStart = position;
        while (lineStart > begin && lineStart[-1] != '\n')
            --lineStart;
        for (char const* p = lineStart; p != position; ++p)
        {
            if (*p == ' ')
                ++column;
            else if (*p == '\t')
                column = (column / 8u + 1u) * 8u;
            else if (*p != '\r')
                return std::nullopt;
        }
        return column;
    }

    fragment compiler::take_body(std::size_t aBodyBase)
    {
        auto& stack = state().operandStack;
        fragment body = make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{});
        for (auto i = aBodyBase; i < stack.size(); ++i)
        {
            if (!iDeclarationPass && (!stack[i].holds_alternative<i_fragment>() || stack[i].get<i_fragment>().kind() != fragment_kind::Statement))
                throw std::runtime_error("unexpected expression in indented block");
            if (stack[i].holds_alternative<i_fragment>())
                append(body, stack[i].get<i_fragment>());
        }
        stack.resize(std::min(aBodyBase, stack.size()));
        return body;
    }

    void compiler::finalize_indented()
    {
        using bytecode::opcode;
        auto construct = std::move(state().indentStack.back());
        state().indentStack.pop_back();
        if (construct.kind == indented_construct::Function)
        {
            if (language() != nullptr && !iDeclarationPass && state().function)
                if (auto const* end = language()->function_end(construct.implicit); end != nullptr && !end->code().empty())
                    push_operand(operand_type{ fragment{ *end } });
            leave_scope(scope_type::Function); // (finish_function collects the body)
            if (construct.implicit)
                state().implicitFunctionFinished = true;
            return;
        }
        auto statement = make_fragment(fragment_kind::Statement, type::Void, neolib::string_view{});
        auto& code = statement.fragmentCode.as_std_vector();
        if (construct.kind == indented_construct::With)
        {
            construct.arms.push_back(take_body(construct.bodyBase));
            if (!iDeclarationPass)
                statement = language_construct(construct);
            auto& stack = state().operandStack;
            stack.insert(stack.begin() + static_cast<std::ptrdiff_t>(std::min(construct.base, stack.size())), operand_type{ statement });
            return;
        }
        if (construct.kind == indented_construct::Try || construct.kind == indented_construct::Except || construct.kind == indented_construct::Finally ||
            (construct.kind == indented_construct::Else && construct.tryElse))
        {
            if (!construct.suspended)
            {
                if (construct.kind == indented_construct::Finally)
                    construct.finallyArm = take_body(construct.bodyBase);
                else if (construct.kind == indented_construct::Else)
                    construct.elseArm = take_body(construct.bodyBase);
                else
                    construct.arms.push_back(take_body(construct.bodyBase));
            }
            if (construct.kind == indented_construct::Try)
                throw std::runtime_error("expected 'except' or 'finally' block");
            if (!iDeclarationPass)
                statement = language_construct(construct);
            auto& stack = state().operandStack;
            stack.insert(stack.begin() + static_cast<std::ptrdiff_t>(std::min(construct.base, stack.size())), operand_type{ statement });
            return;
        }
        if (!construct.suspended && construct.kind != indented_construct::Else)
        {
            if (construct.kind == indented_construct::While || construct.kind == indented_construct::For)
                construct.arms.push_back(take_body(construct.bodyBase));
            else
                construct.arms.push_back(take_body(construct.bodyBase));
        }
        else if (!construct.suspended)
            construct.elseArm = take_body(construct.bodyBase);
        switch (construct.kind)
        {
        case indented_construct::If:
        case indented_construct::ElseIf:
        case indented_construct::Else:
            for (std::size_t i = 0u; i < construct.conditions.size(); ++i)
            {
                if (i != 0u)
                    put_op(code, opcode::Else);
                append(statement, construct.conditions[i]);
                put_op(code, opcode::If);
                code.push_back(0x40u);
                append_nested(statement, construct.arms[i], static_cast<std::uint32_t>(i + 1u));
            }
            if (construct.elseArm)
            {
                put_op(code, opcode::Else);
                append_nested(statement, *construct.elseArm, static_cast<std::uint32_t>(construct.conditions.size()));
            }
            for (std::size_t i = 0u; i < construct.conditions.size(); ++i)
                put_op(code, opcode::End);
            break;
        case indented_construct::While:
            put_op(code, opcode::Block);
            code.push_back(0x40u);
            put_op(code, opcode::Loop);
            code.push_back(0x40u);
            append(statement, *construct.loopCondition);
            put_op(code, opcode::I32Eqz);
            put_op(code, opcode::BrIf);
            put_u32(code, 1u);
            // (break: out of the block; continue: to the loop, which tests the condition again)
            resolve_branches(construct.arms[0], 1u, 0u);
            append(statement, construct.arms[0]);
            put_op(code, opcode::Br);
            put_u32(code, 0u);
            put_op(code, opcode::End);
            put_op(code, opcode::End);
            if (construct.epilogue)
                append(statement, *construct.epilogue);
            break;
        case indented_construct::For:
            if (!iDeclarationPass)
            {
                // for variable in range([start,] stop[, step])
                // (range arguments are evaluated once, before the loop; a hidden counter drives the iteration and is copied 
                // into the loop variable at the top of each iteration so the variable keeps its last value after the loop 
                // and assignments to it in the body do not affect the iteration)
                auto const variable = construct.loopVariable->index();
                auto const variableType = construct.loopVariable->type();
                // (an object variable is assigned the counter as an int object; an integer of any size (Python's int) is
                // counted as a small integer: shifted left one bit, so its step and constants are doubled)
                auto const t = variableType == type::Object ? type::I64 : variableType;
                bool const global = is_global(*construct.loopVariable);
                bool const w = is_64(t);
                std::int64_t const unit = t == type::Integer ? 2 : 1;
                auto const counter = temporary(t);
                auto const stop = temporary(t);
                auto const step = temporary(t);
                auto const next = temporary(t);
                auto const get = [&](std::uint32_t aLocal) { put_op(code, opcode::LocalGet); put_u32(code, aLocal); };
                auto const set = [&](std::uint32_t aLocal) { put_op(code, opcode::LocalSet); put_u32(code, aLocal); };
                auto const set_variable = [&]() { put_op(code, global ? opcode::GlobalSet : opcode::LocalSet); put_u32(code, variable); };
                auto const constant = [&](std::int64_t aValue) { put_op(code, w ? opcode::I64Const : opcode::I32Const); put_s64(code, aValue); };
                auto& range = construct.range;
                for (auto& argument : range)
                    if (argument.valueType != t)
                        coerce(argument, t == type::Object ? type::I64 : t);
                // a constant step (the default, 1, or a literal) needs one comparison per test rather than a test of its sign
                std::optional<std::int64_t> constantStep;
                if (range.size() < 3u)
                    constantStep = unit;
                else if (range[2].fragmentRelocations.empty())
                {
                    auto const& bytes = range[2].fragmentCode.as_std_vector();
                    if (!bytes.empty() && bytes[0] == (w ? 0x42u : 0x41u))
                    {
                        std::int64_t value = 0;
                        std::uint32_t shift = 0u;
                        std::size_t i = 1u;
                        std::uint8_t b = 0x80u;
                        while (i < bytes.size() && (b & 0x80u) != 0u)
                        {
                            b = bytes[i++];
                            value |= static_cast<std::int64_t>(b & 0x7Fu) << shift;
                            shift += 7u;
                        }
                        if ((b & 0x80u) == 0u && i == bytes.size())
                        {
                            if (shift < 64u && (b & 0x40u) != 0u)
                                value |= -(std::int64_t{ 1 } << shift);
                            if (value != 0)
                                constantStep = value;
                        }
                    }
                }
                if (range.size() == 1u)
                    constant(0);
                else
                    append(statement, range[0]);
                set(counter);
                append(statement, range.size() == 1u ? range[0] : range[1]);
                set(stop);
                if (constantStep)
                    ; // (the step is used as a constant)
                else if (range.size() == 3u)
                {
                    append(statement, range[2]);
                    set(step);
                    // range() arg 3 must not be zero
                    get(step); put_op(code, w ? opcode::I64Eqz : opcode::I32Eqz);
                    put_op(code, opcode::If);
                    code.push_back(0x40u);
                    put_op(code, opcode::Unreachable);
                    put_op(code, opcode::End);
                }
                else
                {
                    constant(unit);
                    set(step);
                }
                if (t == type::Integer)
                {
                    if (auto const* check = language() != nullptr ? language()->small_integers(
                        neolib::vector<std::uint32_t>{ counter, stop, range.size() == 3u && !constantStep ? step : counter }) : nullptr)
                        append(statement, *check);
                    // (the range of the values it binds: start to stop, if they are constants)
                    auto const constant_value = [&](fragment const& aValue) -> std::optional<std::int64_t>
                    {
                        auto const& bytes = aValue.fragmentCode.as_std_vector();
                        if (!aValue.fragmentRelocations.empty() || bytes.empty() || bytes[0] != 0x42u)
                            return {};
                        std::int64_t value = 0;
                        std::uint32_t shift = 0u;
                        std::size_t i = 1u;
                        std::uint8_t b = 0x80u;
                        while (i < bytes.size() && (b & 0x80u) != 0u)
                        {
                            b = bytes[i++];
                            value |= static_cast<std::int64_t>(b & 0x7Fu) << shift;
                            shift += 7u;
                        }
                        if ((b & 0x80u) != 0u || i != bytes.size())
                            return {};
                        if (shift < 64u && (b & 0x40u) != 0u)
                            value |= -(std::int64_t{ 1 } << shift);
                        return value >> 1;
                    };
                    std::int64_t constexpr Low = -(std::int64_t{ 1 } << 62);
                    std::int64_t constexpr High = (std::int64_t{ 1 } << 62) - 1;
                    auto const first = range.size() == 1u ? std::optional<std::int64_t>{ 0 } : constant_value(range[0]);
                    auto const last = constant_value(range.size() == 1u ? range[0] : range[1]);
                    interval values{ Low, High };
                    if (constantStep && *constantStep > 0)
                        values = { first ? *first : Low, last ? *last - 1 : High };
                    else if (constantStep && *constantStep < 0)
                        values = { last ? *last + 1 : Low, first ? *first : High };
                    if (values.low > values.high)
                        values = { 0, 0 }; // (no iterations: the variable is not bound)
                    values = values.join({ 0, 0 }); // (before it is bound: zero)
                    if (!global && language() != nullptr)
                        language()->loop_variable_range(neolib::string_view{ construct.loopVariableName }, values.low, values.high);
                }
                if (variableType == type::Integer && !global && !sets_local(construct.arms[0], variable))
                    if (auto const* release = language() != nullptr ? language()->release_integer(variable, global) : nullptr)
                        append(statement, *release);
                put_op(code, opcode::Block);
                code.push_back(0x40u);
                put_op(code, opcode::Loop);
                code.push_back(0x40u);
                if (constantStep)
                {
                    get(counter); get(stop); put_op(code, *constantStep > 0 ? (w ? opcode::I64LtS : opcode::I32LtS) : (w ? opcode::I64GtS : opcode::I32GtS));
                }
                else
                {
                    get(step); constant(0); put_op(code, w ? opcode::I64GtS : opcode::I32GtS);
                    put_op(code, opcode::If);
                    code.push_back(0x7Fu);
                    get(counter); get(stop); put_op(code, w ? opcode::I64LtS : opcode::I32LtS);
                    put_op(code, opcode::Else);
                    get(counter); get(stop); put_op(code, w ? opcode::I64GtS : opcode::I32GtS);
                    put_op(code, opcode::End);
                }
                put_op(code, opcode::I32Eqz);
                put_op(code, opcode::BrIf);
                put_u32(code, 1u);
                if (variableType == type::Object)
                {
                    fragment value;
                    put_op(value.fragmentCode.as_std_vector(), opcode::LocalGet);
                    put_u32(value.fragmentCode.as_std_vector(), counter);
                    value.valueType = t;
                    coerce(value, type::Object);
                    append(statement, value);
                    put_op(code, global ? opcode::GlobalGet : opcode::LocalGet);
                    put_u32(code, variable);
                    auto const release = runtime_function("neos::object_release", { type::Object }, type::Void);
                    put_op(code, opcode::CallFunction);
                    statement.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), release });
                    code.resize(code.size() + 5u);
                    set_variable();
                }
                else
                {
                    // (an integer of any size: the integer it held is released; only once, before the loop, if it is a local that 
                    // the body does not set (it then holds the counter's small integer after the first iteration))
                    if (variableType == type::Integer && (global || sets_local(construct.arms[0], variable)))
                        if (auto const* release = language() != nullptr ? language()->release_integer(variable, global) : nullptr)
                            append(statement, *release);
                    get(counter); set_variable();
                }
                // (break: out of the loop's block; continue: to the end of the body, which is a block of its own)
                bool const continues = has_continue(construct.arms[0]);
                if (continues)
                {
                    resolve_branches(construct.arms[0], 2u, 0u);
                    put_op(code, opcode::Block);
                    code.push_back(0x40u);
                }
                else
                    resolve_branches(construct.arms[0], 1u, 0u);
                append(statement, construct.arms[0]);
                if (continues)
                    put_op(code, opcode::End);
                // next = counter + step; the loop ends if that overflows
                if (constantStep)
                {
                    get(counter); constant(*constantStep); put_op(code, w ? opcode::I64Add : opcode::I32Add); set(next);
                    get(next); get(counter); put_op(code, *constantStep > 0 ? (w ? opcode::I64LtS : opcode::I32LtS) : (w ? opcode::I64GtS : opcode::I32GtS));
                }
                else
                {
                    get(counter); get(step); put_op(code, w ? opcode::I64Add : opcode::I32Add); set(next);
                    get(step); constant(0); put_op(code, w ? opcode::I64GtS : opcode::I32GtS);
                    put_op(code, opcode::If);
                    code.push_back(0x7Fu);
                    get(next); get(counter); put_op(code, w ? opcode::I64LtS : opcode::I32LtS);
                    put_op(code, opcode::Else);
                    get(next); get(counter); put_op(code, w ? opcode::I64GtS : opcode::I32GtS);
                    put_op(code, opcode::End);
                }
                put_op(code, opcode::BrIf);
                put_u32(code, 1u);
                get(next); set(counter);
                put_op(code, opcode::Br);
                put_u32(code, 0u);
                put_op(code, opcode::End);
                put_op(code, opcode::End);
            }
            break;
        default:
            break;
        }
        auto& stack = state().operandStack;
        stack.insert(stack.begin() + static_cast<std::ptrdiff_t>(std::min(construct.base, stack.size())), operand_type{ statement });
    }

    void compiler::finalize_suspended(std::size_t aIndent)
    {
        auto& constructs = state().indentStack;
        while (!constructs.empty() && constructs.back().suspended && constructs.back().indent >= aIndent)
            finalize_indented();
    }

    void compiler::close_indented(std::size_t aIndent)
    {
        auto& constructs = state().indentStack;
        while (!constructs.empty())
        {
            auto& top = constructs.back();
            if (top.implicit)
                break;
            if (top.suspended)
            {
                if (top.indent > aIndent)
                {
                    finalize_indented();
                    continue;
                }
                break;
            }
            if (top.indent < aIndent)
                break;
            if ((top.kind == indented_construct::If || top.kind == indented_construct::ElseIf) && top.indent == aIndent)
            {
                // the arm ends but an elif/else may follow at this indentation
                top.arms.push_back(take_body(top.bodyBase));
                top.suspended = true;
                break;
            }
            if ((top.kind == indented_construct::Try || top.kind == indented_construct::Except || (top.kind == indented_construct::Else && top.tryElse)) &&
                top.indent == aIndent)
            {
                // (an except, else or finally may follow)
                if (top.kind == indented_construct::Else)
                    top.elseArm = take_body(top.bodyBase);
                else
                    top.arms.push_back(take_body(top.bodyBase));
                top.suspended = true;
                break;
            }
            finalize_indented();
        }
    }

    void compiler::ensure_function()
    {
        if (state().function)
            return;
        if (state().implicitFunctionFinished)
            throw std::runtime_error("top-level statements must follow function definitions");
        // top-level statements form an implicit entry point function
        neolib::string const name{ "__main__" };
        define_symbol(name, type::Function, neolib::string_view{});
        auto& scope = static_cast<i_function_scope&>(enter_scope(scope_type::Function, name));
        function_signature signature;
        signature.set_name(name);
        scope.set_function_signature(signature);
        current_function().entry = true;
        current_function().implicitEntry = true;
        if (language() != nullptr)
            language()->function_start(); // (e.g. variables declared at its start)
        // operands of the statement being compiled (e.g. when a temporary is needed mid-expression) belong to the implicit function
        auto const& stack = state().operandStack;
        auto base = stack.size();
        while (base > 0u && !(stack[base - 1u].holds_alternative<i_fragment>() && stack[base - 1u].get<i_fragment>().kind() == fragment_kind::Statement))
            --base;
        current_function().operandBase = base;
        typename compilation_state::indented construct{ indented_construct::Function, std::numeric_limits<std::size_t>::max(), base, base };
        construct.implicit = true;
        state().indentStack.insert(state().indentStack.begin(), construct);
    }

    void compiler::indentation(neolib::i_string_view const& aLineBreak)
    {
        auto const text = aLineBreak.to_std_string_view();
        auto const lastLineBreak = text.rfind('\n');
        std::size_t column = 0u;
        for (auto i = (lastLineBreak == std::string_view::npos ? 0u : lastLineBreak + 1u); i < text.size(); ++i)
            if (text[i] == ' ')
                ++column;
            else if (text[i] == '\t')
                column = (column / 8u + 1u) * 8u;
        close_indented(column);
        // the next line must continue the innermost open block (or start the body of a block just opened)
        auto& constructs = state().indentStack;
        if (constructs.empty() || constructs.back().implicit)
        {
            if (column != 0u)
                throw std::runtime_error("unexpected indent");
        }
        else if (!constructs.back().suspended)
        {
            auto& top = constructs.back();
            if (!top.bodyIndent)
                top.bodyIndent = column;
            else if (column != *top.bodyIndent)
                throw std::runtime_error(column > *top.bodyIndent ? "unexpected indent" : "unindent does not match any outer indentation level");
        }
    }

    void compiler::begin_indented(indented_construct aConstruct, neolib::i_string_view const& aSource)
    {
        auto const column = line_indent(aSource).value_or(0u);
        auto& stack = state().operandStack;
        // statements following the colon on the header's own line form (the start of) the body
        auto inlineBody = pop_fragments(aSource, fragment_kind::Statement);
        typename compilation_state::indented construct{ aConstruct, column, 0u, 0u };
        switch (aConstruct)
        {
        case indented_construct::If:
        case indented_construct::ElseIf:
        case indented_construct::While:
            {
                if (stack.empty())
                    throw std::runtime_error("condition expected");
                auto const condition = stack.back();
                stack.pop_back();
                auto conditionCode = materialize(condition, type::Boolean);
                if (aConstruct == indented_construct::While)
                    construct.loopCondition = conditionCode;
                else
                    construct.conditions.push_back(conditionCode);
            }
            break;
        case indented_construct::For:
            {
                construct.range = pop_fragments(aSource, fragment_kind::Argument);
                if (construct.range.empty() || construct.range.size() > 3u)
                    throw std::runtime_error("range() takes 1 to 3 arguments");
                if (!iDeclarationPass)
                    for (auto const& argument : construct.range)
                        if ((is_float(argument.valueType) || !is_numeric(argument.valueType)) && argument.valueType != type::Object && argument.valueType != type::Integer)
                            throw std::runtime_error("range() arguments must be integers");
                auto const text = aSource.to_std_string_view();
                auto const begin = text.find_first_not_of(" \t", 3u);
                auto const end = text.find_first_of(" \t", begin);
                neolib::string const variable{ std::string{ text.substr(begin, end - begin) } };
                declare_loop_variable(neolib::string_view{ variable }, aSource);
                construct.loopVariable = find_symbol(current_scope(), neolib::string_view{ variable }, !(language() != nullptr && language()->late_binding()));
                construct.loopVariableName = variable.to_std_string();
            }
            break;
        case indented_construct::With:
            {
                // with manager [as name]:
                if (stack.empty())
                    throw std::runtime_error("context manager expected");
                auto const manager = stack.back();
                stack.pop_back();
                if (!iDeclarationPass)
                    construct.conditions.push_back(materialize(manager));
                auto const text = aSource.to_std_string_view();
                auto const colon = text.rfind(':');
                auto head = text.substr(0u, colon);
                auto const as = head.rfind(" as ");
                std::optional<std::string> name;
                if (as != std::string_view::npos)
                {
                    auto n = head.substr(as + 4u);
                    while (!n.empty() && (n.front() == ' ' || n.front() == '\t')) n.remove_prefix(1u);
                    while (!n.empty() && (n.back() == ' ' || n.back() == '\t')) n.remove_suffix(1u);
                    name = std::string{ n };
                    if (language() != nullptr && !iDeclarationPass)
                        language()->bind_name(neolib::string_view{ *name }, type::Object);
                }
                construct.handlerNames.push_back(name);
            }
            break;
        default:
            break;
        }
        if (aConstruct == indented_construct::Except || aConstruct == indented_construct::Finally ||
            (aConstruct == indented_construct::Else && !state().indentStack.empty() && state().indentStack.back().suspended &&
                (state().indentStack.back().kind == indented_construct::Try || state().indentStack.back().kind == indented_construct::Except)))
        {
            // (a handler, else or finally of a try statement)
            auto& constructs = state().indentStack;
            if (constructs.empty() || !constructs.back().suspended || constructs.back().indent != column ||
                !(constructs.back().kind == indented_construct::Try || constructs.back().kind == indented_construct::Except ||
                    (constructs.back().kind == indented_construct::Else && constructs.back().tryElse)) ||
                (aConstruct == indented_construct::Except && constructs.back().kind == indented_construct::Else))
                throw std::runtime_error(aConstruct == indented_construct::Except ? "except without try" : aConstruct == indented_construct::Finally ? "finally without try" : "else without if");
            auto& top = constructs.back();
            if (aConstruct == indented_construct::Except)
            {
                // except [type [as name]]:
                auto const text = aSource.to_std_string_view();
                auto const colon = text.rfind(':');
                auto head = text.substr(6u, colon == std::string_view::npos ? std::string_view::npos : colon - 6u);
                std::optional<std::string> name;
                auto const as = head.rfind(" as ");
                if (as != std::string_view::npos)
                {
                    auto n = head.substr(as + 4u);
                    while (!n.empty() && (n.front() == ' ' || n.front() == '\t')) n.remove_prefix(1u);
                    while (!n.empty() && (n.back() == ' ' || n.back() == '\t')) n.remove_suffix(1u);
                    name = std::string{ n };
                    head = head.substr(0u, as);
                }
                bool const bare = head.find_first_not_of(" \t") == std::string_view::npos;
                std::optional<fragment> type;
                if (!bare)
                {
                    if (stack.empty())
                        throw std::runtime_error("exception type expected");
                    auto const t = stack.back();
                    stack.pop_back();
                    if (!iDeclarationPass)
                        type = materialize(t);
                }
                top.handlerTypes.push_back(type);
                top.handlerNames.push_back(name);
                if (name && language() != nullptr && !iDeclarationPass)
                {
                    // (the name is bound before the handler's body is compiled)
                    language()->bind_name(neolib::string_view{ *name }, type::Object);
                }
                top.kind = indented_construct::Except;
            }
            else if (aConstruct == indented_construct::Else)
            {
                top.kind = indented_construct::Else;
                top.tryElse = true;
            }
            else
                top.kind = indented_construct::Finally;
            top.suspended = false;
            top.bodyBase = stack.size();
        }
        else if (aConstruct == indented_construct::ElseIf || aConstruct == indented_construct::Else)
        {
            auto& constructs = state().indentStack;
            if (constructs.empty() || !constructs.back().suspended || constructs.back().indent != column ||
                (constructs.back().kind != indented_construct::If && constructs.back().kind != indented_construct::ElseIf))
                throw std::runtime_error(aConstruct == indented_construct::Else ? "else without if" : "elif without if");
            auto& top = constructs.back();
            if (aConstruct == indented_construct::ElseIf)
                top.conditions.push_back(construct.conditions.back());
            top.kind = aConstruct;
            top.suspended = false;
            top.bodyBase = stack.size();
        }
        else
        {
            finalize_suspended(column);
            if (aConstruct == indented_construct::Function)
            {
                // (functions that can follow top-level statements)
                bool const followsStatements = (state().indentStack.size() == 1u && state().indentStack[0].implicit);
                if (state().indentStack.size() > 1u || (followsStatements && !(language() != nullptr && language()->functions_follow_statements())))
                    throw std::runtime_error("function definitions must precede top-level statements and cannot be nested");
                construct.base = construct.bodyBase = current_function().operandBase;
            }
            else
            {
                ensure_function();
                construct.base = construct.bodyBase = stack.size();
            }
            state().indentStack.push_back(construct);
        }
        for (auto const& statement : inlineBody)
            push_operand(operand_type{ statement });
    }

    void compiler::emit_indented_statement(neolib::i_string_view const& aSource)
    {
        auto& stack = state().operandStack;
        emit_statement(aSource);
        auto const column = line_indent(aSource);
        if (!column)
            return; // inline statement (the body of a header on the same line)
        std::optional<operand_type> statement;
        if (!stack.empty() && stack.back().holds_alternative<i_fragment>() &&
            stack.back().get<i_fragment>().kind() == fragment_kind::Statement &&
            within(stack.back().get<i_fragment>().source(), aSource))
        {
            statement = stack.back();
            stack.pop_back();
        }
        finalize_suspended(*column);
        ensure_function();
        if (statement)
            stack.push_back(*statement);
    }

    void compiler::end_indented()
    {
        // a module (other than an imported one) without top-level statements has an empty entry point
        if (!current_fragment().imported() && !iDeclarationPass && !state().function && !state().implicitFunctionFinished)
            ensure_function();
        close_indented(0u);
        finalize_suspended(0u);
        while (!state().indentStack.empty())
            finalize_indented();
    }

    void compiler::emit_binding(neolib::i_string_view const& aTarget, neolib::i_string_view const& aSource)
    {
        if (state().operandStack.empty())
            throw std::runtime_error("expression expected");
        auto const value = state().operandStack.back();
        state().operandStack.pop_back();
        ensure_function();
        auto const* existing = find_symbol(current_scope(), aTarget, true);
        {
            // the first binding defines the variable (in the function's scope); its type is inferred from the value
            type t = type::I64;
            if (value.holds_alternative<i_fragment>())
                t = value.get<i_fragment>().value_type();
            else if (value.holds_alternative<i_symbol_reference>())
            {
                auto const& reference = value.get<i_symbol_reference>();
                if (auto const* symbol = find_symbol(reference.scope(), neolib::string_view{ reference.name() }))
                    t = symbol->type();
            }
            else if (is_immediate(value))
            {
                t = get_immediate(value).t;
                if (t == type::Ibig)
                    t = type::I64;
                else if (t == type::Fbig)
                    t = type::F64;
            }
            else
                t = static_cast<type>(value.get<i_data_type>().index());
            if (t == type::Void || t == type::UNKNOWN)
                throw std::runtime_error("cannot infer type of '" + aTarget.to_std_string() + "'");
            if (existing == nullptr)
                define_symbol(neolib::string{ aTarget }, t, aSource);
            else
                check_rebinding(*existing, aTarget, t);
        }
        push_operand(value);
        emit_assignment(aTarget, aSource);
    }

    void compiler::emit_compound_assignment(operation aOperation, neolib::i_string_view const& aTarget, neolib::i_string_view const& aSource)
    {
        if (state().operandStack.empty())
            throw std::runtime_error("expression expected");
        auto const value = state().operandStack.back();
        state().operandStack.pop_back();
        push_operand(operand_type{ symbol_reference{ current_scope(), neolib::string{ aTarget } } });
        push_operand(value);
        emit_operation(aOperation);
        auto const* existing = find_symbol(current_scope(), aTarget, true);
        auto const& result = state().operandStack.back();
        if (existing != nullptr && result.holds_alternative<i_fragment>())
            check_rebinding(*existing, aTarget, result.get<i_fragment>().value_type());
        emit_assignment(aTarget, aSource);
    }

    void compiler::check_rebinding(i_symbol_table_entry const& aVariable, neolib::i_string_view const& aName, type aValueType) const
    {
        // a variable's type is fixed by its first binding so a value of another kind (int, float, bool, str) cannot be 
        // bound to it (which would silently change the value's behaviour, e.g. how it is printed)
        if (iDeclarationPass)
            return; // (value types are not known)
        auto const kind = [](type aType) -> std::string
        {
            if (aType == type::Boolean)
                return "bool";
            if (aType == type::String)
                return "str";
            if (is_float(aType))
                return "float";
            if (is_numeric(aType))
                return "int";
            return type_name(aType);
        };
        if (kind(aValueType) != kind(aVariable.type()))
            throw std::runtime_error("cannot bind a value of type " + kind(aValueType) + " to '" + aName.to_std_string() + 
                "' (of type " + kind(aVariable.type()) + "): variables keep the type of their first binding");
    }

    void compiler::declare_loop_variable(neolib::i_string_view const& aName, neolib::i_string_view const& aSource)
    {
        // a for loop's variable is bound (as an integer) before its body is compiled
        ensure_function();
        auto const* symbol = find_symbol(current_scope(), aName, true);
        // (the language's loop variable)
        if (language() != nullptr && language()->declare_loop_variable(aName, symbol))
            return;
        if (symbol == nullptr)
            define_symbol(neolib::string{ aName }, type::I64, aSource);
        else if (!is_numeric(symbol->type()) || is_float(symbol->type()) || symbol->type() == type::Boolean)
            throw std::runtime_error("loop variable '" + aName.to_std_string() + "' must be an integer");
    }

    void compiler::declare_stub()
    {
        current_function().stub = true;
    }

    void compiler::finish_function()
    {
        using bytecode::opcode;
        // (e.g. the imports that the language's checks of the function use: imported before references to the module's functions are taken)
        if (language() != nullptr && !iDeclarationPass)
            language()->finishing_function(current_function().entry);
        auto& function = current_function();
        auto const& signature = function.scope->function_signature();
        auto& stack = state().operandStack;
        if (function.stub)
        {
            // "..." makes the function a stub only when it is the entire body (elsewhere it is an empty statement)
            auto const sole = [&]() -> std::string_view
            {
                if (stack.size() != function.operandBase + 1u || !stack.back().holds_alternative<i_fragment>())
                    return {};
                auto text = stack.back().get<i_fragment>().source().to_std_string_view();
                while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
                    text.remove_prefix(1u);
                while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
                    text.remove_suffix(1u);
                return text;
            };
            function.stub = (sole() == "...");
        }
        fragment body;
        for (auto i = function.operandBase; i < stack.size(); ++i)
        {
            if (iDeclarationPass)
                break;
            if (!stack[i].holds_alternative<i_fragment>() || stack[i].get<i_fragment>().kind() != fragment_kind::Statement)
                throw std::runtime_error("unexpected expression in body of '" + function.name + "'");
            append(body, stack[i].get<i_fragment>());
        }
        stack.resize(std::min(function.operandBase, stack.size()));
        auto& code = body.fragmentCode.as_std_vector();
        if (signature.return_type() != type::Void)
            put_op(code, opcode::Unreachable);
        else if (signature.parameters().has_value())
            for (auto const& parameter : signature.parameters().value())
                if (parameter.direction() != parameter_direction::In)
                {
                    put_op(code, opcode::LocalGet);
                    put_u32(code, find_symbol(current_scope(), neolib::string_view{ parameter.parameter_name() })->index());
                }
        function.body = code;
        function.relocations.clear();
        for (auto const& r : body.relocations())
        {
            if ((r.second() & BranchTarget) != 0u && (r.second() & DeferredCall) == 0u)
                throw std::runtime_error(std::string{ (r.second() & ContinueTarget) != 0u ? "continue" : "break" } + " outside a loop");
            function.relocations.emplace_back(r.first(), r.second());
        }
        function.signature = signature;
        if (language() != nullptr && !iDeclarationPass && !function.imported && !function.stub)
            language()->function_finished(*state().function);
        function.compiled = true;
        if (function.stub)
        {
            // a stub named __neos_string_bytes, __neos_string_utf16_units or __neos_string_code_points (taking a string and 
            // returning an integer) is a string's length as its header records it (see bytecode::string_header): a call to it 
            // is a load (it is not imported)
            static std::map<std::string, std::uint32_t> const sIntrinsics =
            {
                { "__neos_string_bytes", static_cast<std::uint32_t>(offsetof(bytecode::string_header, bytes)) },
                { "__neos_string_utf16_units", static_cast<std::uint32_t>(offsetof(bytecode::string_header, utf16Units)) },
                { "__neos_string_code_points", static_cast<std::uint32_t>(offsetof(bytecode::string_header, codePoints)) }
            };
            // (any other stub named __neos_f is the VM's function neos::f; see emit_module)
            if (auto const intrinsic = sIntrinsics.find(function.name); intrinsic != sIntrinsics.end())
            {
                if (!signature.parameters().has_value() || signature.parameters().value().size() != 1u ||
                    signature.parameters().value()[0].parameter_type() != type::String || !is_numeric(signature.return_type()) ||
                    is_float(signature.return_type()) || signature.return_type() == type::Boolean)
                    throw std::runtime_error("'" + function.name + "' takes a string and returns an integer");
                function.intrinsic = intrinsic->second;
            }
            // a function whose body is a stub ("...") is provided by the language's runtime plugin: import it under a name that 
            // encodes its parameter types
            function.imported = true;
            function.importNamespace = function.name.starts_with("__neos_") ? "neos" : state().unit->schema->meta().language;
            function.name += "(";
            if (signature.parameters().has_value())
                for (std::size_t i = 0u; i < signature.parameters().value().size(); ++i)
                    function.name += (i == 0u ? "" : ",") + type_name(signature.parameters().value()[i].parameter_type());
            function.name += ")";
            function.body.clear();
            function.relocations.clear();
        }
        state().function = std::nullopt;
    }

    void compiler::emit_module(program& aProgram)
    {
        using bytecode::opcode;
        // function index space: imports first
        std::vector<std::uint32_t> order;
        for (std::uint32_t id = 0u; id < iModule.functions.size(); ++id)
            if (iModule.functions[id].imported && !iModule.functions[id].intrinsic)
                order.push_back(id);
        std::uint32_t const importCount = static_cast<std::uint32_t>(order.size());
        for (std::uint32_t id = 0u; id < iModule.functions.size(); ++id)
            if (!iModule.functions[id].imported)
            {
                if (!iModule.functions[id].compiled)
                    throw std::runtime_error("function '" + iModule.functions[id].name + "' has no body");
                order.push_back(id);
            }
        std::vector<std::uint32_t> finalIndex(iModule.functions.size());
        for (std::uint32_t i = 0u; i < order.size(); ++i)
            finalIndex[order[i]] = i;
        // types
        std::vector<bytes> types;
        std::vector<std::uint32_t> typeOf(iModule.functions.size());
        for (auto id : order)
        {
            auto const& signature = iModule.functions[id].signature;
            bytes functionType;
            functionType.push_back(0x60u);
            bytes parameters, results;
            std::uint32_t parameterCount = 0u, resultCount = 0u;
            if (auto const r = wasm_type(signature.return_type()))
            {
                results.push_back(*r);
                ++resultCount;
            }
            if (signature.parameters().has_value())
                for (auto const& parameter : signature.parameters().value())
                {
                    if (parameter.direction() != parameter_direction::Out)
                    {
                        parameters.push_back(*wasm_type(parameter.parameter_type()));
                        ++parameterCount;
                    }
                    if (parameter.direction() != parameter_direction::In)
                    {
                        results.push_back(*wasm_type(parameter.parameter_type()));
                        ++resultCount;
                    }
                }
            put_u32(functionType, parameterCount);
            functionType.insert(functionType.end(), parameters.begin(), parameters.end());
            put_u32(functionType, resultCount);
            functionType.insert(functionType.end(), results.begin(), results.end());
            auto existing = std::find(types.begin(), types.end(), functionType);
            typeOf[id] = static_cast<std::uint32_t>(existing - types.begin());
            if (existing == types.end())
                types.push_back(functionType);
        }
        // entry point
        std::optional<std::uint32_t> entry;
        for (auto id : order)
            if (iModule.functions[id].entry && !iModule.functions[id].fromImportedFragment)
                entry = id;
        if (!entry)
            for (auto id : order)
                if (!iModule.functions[id].imported && iModule.functions[id].name == "main" && !iModule.functions[id].fromImportedFragment)
                    entry = id;
        if (!entry)
            for (auto id : order)
                if (iModule.functions[id].entry)
                    entry = id;
        for (auto const& global : iInference.globals)
            if (global.second == type::String)
                string_literal(""); // (the initial value of a string global)
        std::uint32_t const heapBase = (DataBase + static_cast<std::uint32_t>(iModule.data.size()) + 7u) & ~7u;
        bytes module = { 0x00u, 0x61u, 0x73u, 0x6Du, 0x01u, 0x00u, 0x00u, 0x00u };
        {
            bytes section;
            put_u32(section, types.size());
            for (auto const& t : types)
                section.insert(section.end(), t.begin(), t.end());
            put_section(module, 1u, section);
        }
        if (importCount != 0u)
        {
            bytes section;
            put_u32(section, importCount);
            for (std::uint32_t i = 0u; i < importCount; ++i)
            {
                put_name(section, "env");
                auto const& function = iModule.functions[order[i]];
                if (function.importNamespace == "neos" && function.name.starts_with("__neos_"))
                    put_name(section, "neos::" + function.name.substr(7u, function.name.find('(') - 7u)); // (the VM's own function)
                else
                    put_name(section, function.importNamespace.empty() ? function.name : function.importNamespace + "::" + function.name);
                section.push_back(0x00u);
                put_u32(section, typeOf[order[i]]);
            }
            put_section(module, 2u, section);
        }
        {
            bytes section;
            put_u32(section, order.size() - importCount);
            for (std::uint32_t i = importCount; i < order.size(); ++i)
                put_u32(section, typeOf[order[i]]);
            put_section(module, 3u, section);
        }
        {
            bytes section;
            put_u32(section, 1u);
            section.push_back(0x00u);
            put_u32(section, heapBase / 65536u + 1u);
            put_section(module, 5u, section);
        }
        {
            // __heap_base and (mutable) global variables
            bytes section;
            put_u32(section, 1u + iInference.globals.size());
            section.push_back(0x7Fu);
            section.push_back(0x00u);
            put_op(section, opcode::I32Const);
            put_s64(section, static_cast<std::int32_t>(heapBase));
            put_op(section, opcode::End);
            for (auto const& global : iInference.globals)
            {
                auto const t = *wasm_type(global.second);
                section.push_back(t);
                section.push_back(0x01u);
                switch (t)
                {
                case 0x7Cu: put_op(section, opcode::F64Const); put_raw(section, 0.0); break;
                case 0x7Du: put_op(section, opcode::F32Const); put_raw(section, 0.0f); break;
                case 0x7Eu: put_op(section, opcode::I64Const); put_s64(section, 0); break;
                default: put_op(section, opcode::I32Const); put_s64(section, global.second == type::String ? static_cast<std::int32_t>(string_literal("")) : 0); break;
                }
                put_op(section, opcode::End);
            }
            put_section(module, 6u, section);
        }
        {
            bytes section;
            // (a global named __neos_error is exported: the VM stores the errors of runtime plugins in it)
            std::optional<std::uint32_t> errorGlobal;
            for (std::uint32_t g = 0u; g < iInference.globals.size(); ++g)
                if (iInference.globals[g].first == "__neos_error")
                    errorGlobal = g + 1u;
            put_u32(section, (entry ? 3u : 2u) + (errorGlobal ? 1u : 0u));
            put_name(section, "memory");
            section.push_back(0x02u);
            put_u32(section, 0u);
            put_name(section, "__heap_base");
            section.push_back(0x03u);
            put_u32(section, 0u);
            if (errorGlobal)
            {
                put_name(section, "__neos_error");
                section.push_back(0x03u);
                put_u32(section, *errorGlobal);
            }
            if (entry)
            {
                put_name(section, "main");
                section.push_back(0x00u);
                put_u32(section, finalIndex[*entry]);
            }
            put_section(module, 7u, section);
        }
        {
            bytes section;
            put_u32(section, order.size() - importCount);
            for (std::uint32_t i = importCount; i < order.size(); ++i)
            {
                auto const& function = iModule.functions[order[i]];
                bytes body;
                std::vector<std::pair<std::uint32_t, std::uint8_t>> runs;
                for (auto const& local : function.locals)
                {
                    auto const t = *wasm_type(local);
                    if (!runs.empty() && runs.back().second == t)
                        ++runs.back().first;
                    else
                        runs.emplace_back(1u, t);
                }
                put_u32(body, runs.size());
                for (auto const& r : runs)
                {
                    put_u32(body, r.first);
                    body.push_back(r.second);
                }
                auto code = function.body;
                for (auto const& r : function.relocations)
                {
                    std::uint32_t target = r.second;
                    if (target & DeferredCall)
                    {
                        auto const& deferred = iModule.deferredCalls[target & ~DeferredCall];
                        std::optional<std::uint32_t> resolved;
                        for (std::uint32_t id = 0u; id < iModule.functions.size() && (!resolved || iModule.functions[*resolved].stub); ++id)
                        {
                            auto const& candidate = iModule.functions[id];
                            std::string name = candidate.name;
                            if (candidate.stub)
                                name = name.substr(0u, name.find('('));
                            if (name != deferred.first)
                                continue;
                            std::vector<type> parameterTypes;
                            if (candidate.signature.parameters().has_value())
                                for (auto const& p : candidate.signature.parameters().value())
                                    parameterTypes.push_back(p.parameter_type());
                            if (parameterTypes == deferred.second && (!resolved || !candidate.stub))
                                resolved = id; // (preferring a function defined in the program to a host function)
                        }
                        if (!resolved)
                            throw std::runtime_error("unresolved function '" + deferred.first + "'");
                        target = *resolved;
                    }
                    put_u32_padded(code, r.first, finalIndex[target]);
                }
                body.insert(body.end(), code.begin(), code.end());
                put_op(body, opcode::End);
                put_u32(section, body.size());
                section.insert(section.end(), body.begin(), body.end());
            }
            put_section(module, 10u, section);
        }
        if (!iModule.data.empty())
        {
            bytes section;
            put_u32(section, 1u);
            section.push_back(0x00u);
            put_op(section, opcode::I32Const);
            put_s64(section, static_cast<std::int32_t>(DataBase));
            put_op(section, opcode::End);
            put_u32(section, iModule.data.size());
            section.insert(section.end(), iModule.data.begin(), iModule.data.end());
            put_section(module, 11u, section);
        }
        aProgram.text.clear();
        for (auto b : module)
            aProgram.text.push_back(std::byte{ b });
        if (trace() >= 1)
            iContext.cout() << "Emitted WASM module: " << aProgram.text.size() << " bytes, " << order.size() << " function(s)" << std::endl;
    }

    const compiler::compilation_state& compiler::state() const
    {
        return *iCompilationStateStack.back();
    }

    compiler::compilation_state& compiler::state()
    {
        return *iCompilationStateStack.back();
    }

    bool compiler::fold()
    {
        while (fold2())
            ;
        return fold_stack().empty();
    }

    bool compiler::fold2()
    {
        if (fold_stack().empty())
            return false;

        auto trace_out = [&](std::string const& op, i_ast_node const& lhs, std::optional<neolib::ref_ptr<i_ast_node>> const& rhs = {}, const std::optional<neolib::ref_ptr<i_ast_node>> result = {})
            {
                std::ostringstream traceOutput;
                if (trace() >= 1)
                {
                    if (rhs)
                        traceOutput << op << ": " << 
                            lhs.trace() << " <- " << (**rhs).trace() <<
                            (result ? " = " + (**result).trace() : "") <<
                            std::endl;
                    else
                        traceOutput << op << ": " << 
                            lhs.trace() << " <-|" <<
                            (result ? " = " + (**result).trace() : "") <<
                            std::endl;
                }
                if (!trace_filter() || traceOutput.str().find(*trace_filter()) != std::string::npos)
                    iContext.cout() << traceOutput.str() << std::flush;
            };

        bool didSome = false;
        try
        {
            for (auto ilhs = fold_stack().begin(); !didSome && ilhs != fold_stack().end();)
            {
                auto& lhs = **ilhs;
                if (lhs.can_fold())
                {
                    trace_out("Folding", lhs);
                    auto const result = lhs.fold(iContext);
                    trace_out("Folded", lhs, {}, result);
                    ilhs = fold_stack().erase(ilhs);
                    if (!result->is_empty())
                        ilhs = fold_stack().insert(ilhs, result);
                    didSome = true;
                }
                else if (std::next(ilhs) != fold_stack().end())
                {
                    auto irhs = std::next(ilhs);
                    auto& rhs = **irhs;
                    bool const unrelated = !lhs.is_sibling(rhs) && !lhs.is_child(rhs) && !rhs.is_child(lhs);
                    if (rhs.can_fold())
                    {
                        trace_out("Folding", rhs);
                        auto const result = rhs.fold(iContext);
                        trace_out("Folded", rhs, {}, result);
                        irhs = fold_stack().erase(irhs);
                        if (!result->is_empty())
                            irhs = fold_stack().insert(irhs, result);
                        didSome = true;
                    }
                    else if (unrelated && !(lhs.unstructured() && rhs.unstructured()))
                    {
                        if (trace() >= 3)
                            trace_out("Skipping", lhs, rhs);
                        ++ilhs;
                    }
                    else if (lhs.name() == rhs.name() && lhs.has_ghosts())
                    {
                        if (lhs.holds_data() && !rhs.holds_data())
                        {
                            if (lhs.is_child(rhs))
                                fold_stack().erase(irhs);
                            else
                            {
                                rhs = lhs;
                                ilhs = fold_stack().erase(ilhs);
                            }
                            didSome = true;
                        }
                        else if (!lhs.holds_data() && rhs.holds_data())
                        {
                            if (lhs.is_child(rhs))
                            {
                                lhs = rhs;
                                fold_stack().erase(irhs);
                            }
                            else
                                ilhs = fold_stack().erase(ilhs);
                            didSome = true;
                        }
                    }
                    else if (lhs.can_fold(rhs))
                    {
                        trace_out(!unrelated ? "Folding" : "Folding (unstructured)", lhs, rhs);
                        auto const result = lhs.fold(iContext, rhs);
                        trace_out(!unrelated ? "Folded" : "Folded (unstructured)", lhs, rhs, result);
                        ilhs = fold_stack().erase(ilhs);
                        ilhs = fold_stack().erase(ilhs);
                        if (!result->is_empty())
                            ilhs = fold_stack().insert(ilhs, result);
                        didSome = true;
                    }
                    else if (rhs.can_fold(lhs))
                    {
                        trace_out(!unrelated ? "Folding" : "Folding (unstructured)", rhs, lhs);
                        auto const result = rhs.fold(iContext, lhs);
                        trace_out(!unrelated ? "Folded" : "Folded (unstructured)", rhs, lhs, result);
                        ilhs = fold_stack().erase(ilhs);
                        ilhs = fold_stack().erase(ilhs);
                        if (!result->is_empty())
                            ilhs = fold_stack().insert(ilhs, result);
                        didSome = true;
                    }
                    else if (lhs.has_parent())
                    {
                        if (lhs.can_fold(lhs.parent()))
                        {
                            trace_out("Folding", lhs, lhs.parent());
                            auto const result = lhs.fold(iContext, lhs.parent());
                            trace_out("Folded", lhs, lhs.parent(), result);
                            bool const insert = (!result->is_empty() && result != &lhs.parent());
                            ilhs = fold_stack().erase(ilhs);
                            if (insert)
                                ilhs = fold_stack().insert(ilhs, result);
                            didSome = true;
                        }
                        else if (lhs.parent().can_fold(lhs))
                        {
                            trace_out("Folding", lhs.parent(), lhs);
                            auto const result = lhs.parent().fold(iContext, lhs);
                            trace_out("Folded", lhs.parent(), lhs, result);
                            bool const insert = (!result->is_empty() && result != &lhs.parent());
                            ilhs = fold_stack().erase(ilhs);
                            if (insert)
                                ilhs = fold_stack().insert(ilhs, result);
                            didSome = true;
                        }
                        else
                            break;
                    }
                    else
                        break;
                }
                else
                    break;
            }
        }
        catch (compiler_error const&)
        {
            throw;
        }
        catch (concept_not_found const& ex)
        {
            throw_error(ex.sourcePos.value(), neolib::string{ ex.what() });
        }
        catch (std::exception const& ex)
        {
            throw_error(fold_stack().front()->source().begin(),
                "failed to fold semantic concept: "_s + ex.what());
        }
        if (!didSome)
        {
            if (trace() >= 3)
            {
                for (auto const& e : fold_stack())
                    iContext.cout() << "Fold stack: " << e->name() << " [" <<
                    neolib::to_escaped_string(e->source().to_std_string_view(), 32u, true) << "]" << std::endl;
            }
            if (fold_stack().size() == 1)
                throw_error(fold_stack().front()->source().begin(),
                    "failed to fold semantic concept: "_s +
                    fold_stack().front()->trace());
            else
                throw_error(fold_stack().front()->source().begin(),
                    "failed to fold semantic concepts: "_s +
                    (**fold_stack().begin()).trace() + " <- " + (**std::next(fold_stack().begin())).trace());
        }
        return didSome;
    }

    fold_stack& compiler::fold_stack()
    {
        return state().foldStack;
    }

    std::string compiler::location(const translation_unit& aUnit, const i_source_fragment& aFragment, source_iterator aSourcePos, bool aShowFragmentFilePath)
    {
        std::uint32_t line = 1;
        std::uint32_t col = 1;
        for (auto pos = aFragment.begin(); pos != aSourcePos; ++pos)
        {
            if (*pos == '\n')
            {
                ++line;
                col = 1;
            }
            else
                ++col;
        }
        std::vector<std::string> lines;
        std::istringstream iss{ aFragment.source().to_std_string() };
        std::string nextLine;
        while (std::getline(iss, nextLine))
            lines.push_back(nextLine);
        std::size_t numberWidth = std::to_string(lines.size()).size();
        std::uint32_t lineNumber = 1u;
        std::ostringstream oss;
        for (auto const& outputLine : lines)
        {
            if (std::abs(static_cast<int>(line - lineNumber)) <= 5)
                oss << std::setw(numberWidth) << lineNumber << (lineNumber == line ? ">" : "|") << outputLine << std::endl;
            ++lineNumber;
        }
        oss << std::string(col + numberWidth, '-') << "^" << std::endl;
        std::string info = (aShowFragmentFilePath && aFragment.source_file_path() != std::nullopt ? aFragment.source_file_path()->to_std_string() : "") + 
            "(" + boost::lexical_cast<std::string>(line) + "," + boost::lexical_cast<std::string>(col) + ")";
        oss << info;
        return oss.str();
    }

    void compiler::throw_error(source_iterator aSourcePos, neolib::i_string const& aError, neolib::i_string const& aErrorType)
    {
        throw compiler_error((location(*state().unit, *state().fragment, aSourcePos) + ": " + aErrorType + ": " + aError).to_std_string());
    }
}
