/*
  compiler.generator.cpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

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

// The compiler's services for language compilers (i_code_generator)

#include <neolib/neolib.hpp>
#include <iostream>
#include <neos/i_context.hpp>
#include <neos/language/compiler.hpp>

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
