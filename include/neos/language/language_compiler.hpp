/*
  language_compiler.hpp

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

#pragma once

// The base of a language compiler (i_language_compiler): the hooks that do nothing and the compiler's services 
// (i_code_generator) as the language's code generation uses them

#include <neos/neos.hpp>
#include <algorithm>
#include <optional>
#include <string>
#include <vector>
#include <neolib/core/reference_counted.hpp>
#include <neolib/core/string.hpp>
#include <neolib/core/vector.hpp>
#include <neos/language/i_language_compiler.hpp>
#include <neos/language/i_code_generator.hpp>
#include <neos/language/emit.hpp>
#include <neos/bytecode/bytecode.hpp>

namespace neos::language
{
    class language_compiler : public neolib::reference_counted<i_language_compiler>
    {
    public:
        // the compiler's operand stack (as a vector)
        class operand_stack
        {
        public:
            explicit operand_stack(i_code_generator& aGenerator) : iGenerator{ aGenerator } {}
        public:
            std::size_t size() const { return iGenerator.operand_count(); }
            bool empty() const { return size() == 0u; }
            i_operand_type const& operator[](std::size_t aIndex) const { return iGenerator.operand(aIndex); }
            i_operand_type const& back() const { return iGenerator.operand(size() - 1u); }
            void set(std::size_t aIndex, i_operand_type const& aOperand) { iGenerator.set_operand(aIndex, aOperand); }
            void set_back(i_operand_type const& aOperand) { iGenerator.set_operand(size() - 1u, aOperand); }
            void push_back(i_operand_type const& aOperand) { iGenerator.insert_operand(size(), aOperand); }
            void pop_back() { iGenerator.erase_operands(size() - 1u); }
            void insert(std::size_t aIndex, i_operand_type const& aOperand) { iGenerator.insert_operand(aIndex, aOperand); }
            void erase(std::size_t aIndex, std::size_t aCount = 1u) { iGenerator.erase_operands(aIndex, aCount); }
            void resize(std::size_t aSize) { if (aSize < size()) erase(aSize, size() - aSize); }
        private:
            i_code_generator& iGenerator;
        };
    public:
        language_compiler(std::string const& aLanguage) : iLanguage{ aLanguage } {}
        // i_language_compiler
    public:
        neolib::i_string const& language() const override { return iLanguage; }
        void attach(i_code_generator& aGenerator) override { iGenerator = &aGenerator; }
        void set_options(neolib::i_string const&) override {}
        bool compiles_at_run_time() const override { return false; }
        void parsed(i_source_fragment&) override {}
        void diagnose(i_source_fragment const&, std::uint32_t&, std::uint32_t&, std::uint32_t&, std::uint32_t&, neolib::i_string&, neolib::i_string&, 
            neolib::i_string&) override {}
        void begin_compilation() override {}
        void begin_pass() override {}
        void end_pass() override {}
        void begin_fragment(i_source_fragment&) override {}
        void end_fragment() override {}
        void preparse(i_source_fragment&) override {}
        bool parsable_source(neolib::i_string_view const&, neolib::i_string&, neolib::i_vector<std::uint32_t>&) const override { return false; }
        bool complete_signature(i_function_signature&, neolib::i_string_view const&) override { return false; }
        type inferred_type(i_operand_type const& aOperand) override { return operand_type_of(operand_type{ aOperand }); }
        type parameter_type(neolib::i_string_view const&, std::uint32_t, type aType) override { return aType; }
        bool value_range(i_operand_type const&, std::int64_t&, std::int64_t&) override { return false; }
        bool late_binding() const override { return false; }
        bool functions_follow_statements() const override { return false; }
        void fragment_function(i_source_fragment const&, neolib::i_string& aName) override { aName.clear(); }
        i_fragment const* literal(i_operand_type const&) override { return nullptr; }
        i_fragment const* unbound_name(neolib::i_string_view const&) override { return nullptr; }
        i_fragment const* void_value(i_fragment const&) override { return nullptr; }
        i_fragment const* variable_value(i_fragment const&, neolib::i_string_view const&, i_symbol_table_entry const&) override { return nullptr; }
        i_fragment const* convert_value(i_fragment const&, type) override { return nullptr; }
        i_fragment const* division_by_zero(std::int32_t) override { return nullptr; }
        type argument_type(i_operand_type const&) override { return type::UNKNOWN; }
        void function_start() override {}
        i_fragment const* function_end(bool) override { return nullptr; }
        void finishing_function(bool) override {}
        void function_finished(std::uint32_t) override {}
        i_fragment const* finish_construct(i_indented_construct const&) override { return nullptr; }
        bool bind_name(neolib::i_string_view const&, type) override { return false; }
        bool declare_loop_variable(neolib::i_string_view const&, i_symbol_table_entry const*) override { return false; }
        i_fragment const* small_integers(neolib::i_vector<std::uint32_t> const&) override { return nullptr; }
        void loop_variable_range(neolib::i_string_view const&, std::int64_t, std::int64_t) override {}
        i_fragment const* release_integer(std::uint32_t, bool) override { return nullptr; }
        std::uint32_t source_line(i_source_fragment const&, std::size_t) override { return 0u; }
        bool frame_name(std::uint32_t, neolib::i_string&) override { return false; }
        // the compiler's services
    protected:
        i_code_generator& generator() const { return *iGenerator; }
        operand_stack operands() const { return operand_stack{ *iGenerator }; }
        bool declaration_pass() const { return iGenerator->declaration_pass(); }
        std::uint32_t trace() const { return iGenerator->trace(); }
        void trace_output(std::string const& aText) const { iGenerator->trace_output(neolib::string{ aText }); }
        void set_changed() { iGenerator->set_changed(); }
        // values
        fragment materialize(operand_type const& aOperand, std::optional<type> aTargetType = {})
        {
            return aTargetType ? fragment{ iGenerator->materialize_operand(aOperand, *aTargetType) } : fragment{ iGenerator->materialize_operand(aOperand) };
        }
        void coerce(fragment& aValue, type aTarget)
        {
            if (aValue.valueType != aTarget)
                aValue = fragment{ iGenerator->coerce_value(aValue, aTarget) };
        }
        void push_operand(operand_type const& aOperand) { iGenerator->push_operand(aOperand); }
        void emit_operation(operation aOperation) { iGenerator->emit_operation(aOperation); }
        operand_type pop_operand_value()
        {
            // (the value at the top of the stack)
            auto stack = operands();
            if (stack.empty() || stack.size() <= iGenerator->operand_base())
                throw std::runtime_error("expression expected");
            operand_type operand{ stack.back() };
            if (operand.holds_alternative<i_fragment>() && operand.get<i_fragment>().kind() != fragment_kind::Value)
                throw std::runtime_error("expression expected");
            stack.pop_back();
            return operand;
        }
        fragment pop_value()
        {
            return materialize(pop_operand_value());
        }
        std::vector<fragment> pop_fragments(neolib::i_string_view const& aSource, fragment_kind aKind)
        {
            neolib::vector<fragment> popped;
            iGenerator->pop_fragments_into(aSource, aKind, popped);
            return std::vector<fragment>{ popped.begin(), popped.end() };
        }
        std::uint32_t temporary(type aType) { return iGenerator->temporary(aType); }
        std::uint32_t string_literal(std::string const& aValue) { return iGenerator->string_literal(neolib::string{ aValue }); }
        std::uint32_t runtime_function(std::string const& aName, std::vector<type> const& aParameters, type aReturnType)
        {
            return iGenerator->runtime_function(neolib::string{ aName }, neolib::vector<type>{ aParameters.begin(), aParameters.end() }, aReturnType);
        }
        void emit_call_to(fragment& aTarget, std::uint32_t aFunction)
        {
            auto& code = aTarget.fragmentCode.as_std_vector();
            emitter::put_op(code, bytecode::opcode::CallFunction);
            aTarget.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), aFunction });
            code.resize(code.size() + 5u);
        }
        std::uint32_t indirect_signature(std::vector<type> const& aParameters, type aReturnType)
        {
            return iGenerator->indirect_signature(neolib::vector<type>{ aParameters.begin(), aParameters.end() }, aReturnType);
        }
        // (a call_indirect of a function of the signature (see indirect_signature): its arguments, then its index (an i32), are the operands)
        void emit_call_indirect(fragment& aTarget, std::uint32_t aSignature)
        {
            auto& code = aTarget.fragmentCode.as_std_vector();
            emitter::put_op(code, bytecode::opcode::CallIndirect);
            aTarget.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), IndirectSignature | aSignature });
            code.resize(code.size() + 5u);
            code.push_back(0x00u);   // (the table: the functions)
        }
        // (the position of the call (or call_indirect) that a fragment's code ends with (its last: aAll, all of its calls): a position 
        // marker before it (see bytecode::PositionMarker))
        void mark_position(fragment& aCode, std::uint64_t aPosition, bool aAll = false)
        {
            if (aPosition == 0u)
                return;
            auto& code = aCode.fragmentCode.as_std_vector();
            thread_local std::vector<std::uint32_t> tCalls; // (scratch kept for reuse (one per call compiled))
            tCalls.clear();
            for (auto const& r : aCode.fragmentRelocations)
                if ((r.second() & BranchTarget) == 0u && r.first() >= 1u && code[r.first() - 1u] == static_cast<std::uint8_t>(
                    (r.second() & IndirectSignature) != 0u && (r.second() & DeferredCall) == 0u ? bytecode::opcode::CallIndirect : bytecode::opcode::CallFunction))
                    tCalls.push_back(r.first() - 1u);
            if (tCalls.empty())
                return;
            std::sort(tCalls.begin(), tCalls.end());
            if (!aAll)
                tCalls.erase(tCalls.begin(), std::prev(tCalls.end()));
            thread_local emitter::bytes tMarker;
            tMarker.clear();
            emitter::put_op(tMarker, bytecode::opcode::I64Const);
            emitter::put_s64(tMarker, static_cast<std::int64_t>(bytecode::PositionMarker | aPosition));
            emitter::put_op(tMarker, bytecode::opcode::Drop);
            // (from the last: the earlier ones do not move)
            for (auto c = tCalls.rbegin(); c != tCalls.rend(); ++c)
            {
                code.insert(code.begin() + *c, tMarker.begin(), tMarker.end());
                for (auto& r : aCode.fragmentRelocations)
                    if (r.first() >= *c)
                        r.first() = r.first() + static_cast<std::uint32_t>(tMarker.size());
            }
        }
        // functions
        bool in_function() const { return iGenerator->in_function(); }
        std::uint32_t current_function() const { return iGenerator->current_function_index(); }
        std::size_t operand_base() const { return iGenerator->operand_base(); }
        bool implicit_entry() const { return in_function() && iGenerator->function_implicit_entry(current_function()); }
        std::string current_function_name() const { return iGenerator->function_name(current_function()).to_std_string(); }
        void ensure_function() { iGenerator->ensure_function(); }
        void infer(std::string const& aWhat, std::optional<type> const& aOld, type aNew) { iGenerator->infer(neolib::string{ aWhat }, aOld.value_or(type::UNKNOWN), aNew); }
        // symbols
        i_scope const& current_scope() const { return iGenerator->current_scope(); }
        i_symbol_table_entry* find_symbol(i_scope const& aScope, neolib::i_string_view const& aName, bool aThisScopeOnly = false) const
        {
            return iGenerator->find_symbol(aScope, aName, aThisScopeOnly);
        }
        i_symbol_table_entry& define_global(std::string const& aName, type aType) { return iGenerator->define_global(neolib::string{ aName }, aType); }
        bool is_global(i_symbol_table_entry const& aSymbol) const { return iGenerator->is_global(aSymbol); }
        void retire_symbols(neolib::i_string_view const& aSource) { iGenerator->retire_symbols(aSource); }
        // helpers
        type operand_type_of(operand_type const& aOperand) const
        {
            if (aOperand.holds_alternative<i_fragment>())
                return aOperand.get<i_fragment>().value_type();
            if (aOperand.holds_alternative<i_symbol_reference>())
            {
                auto const& reference = aOperand.get<i_symbol_reference>();
                auto const* symbol = find_symbol(reference.scope(), neolib::string_view{ reference.name() });
                return symbol != nullptr ? symbol->type() : type::UNKNOWN;
            }
            return static_cast<type>(aOperand.get<i_data_type>().index());
        }
    private:
        neolib::string iLanguage;
        i_code_generator* iGenerator = nullptr;
    };
}
