/*
  i_code_generator.hpp

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

#pragma once

#include <neos/neos.hpp>
#include <neolib/core/i_string.hpp>
#include <neolib/core/i_string_view.hpp>
#include <neolib/core/i_vector.hpp>
#include <neos/language/i_compiler.hpp>

namespace neos::language
{
    // The compiler's services that a language compiler (i_language_compiler) uses: its passes, operand stack, values, 
    // functions, symbols and runtime functions. Objects are passed as interfaces: those it returns (by reference) are its
    // own, valid until its next call (a language compiler copies them).
    class i_code_generator : public i_compiler
    {
        // passes
    public:
        virtual bool declaration_pass() const = 0;              ///< (pass 1: declarations only)
        virtual std::uint32_t inference_pass() const = 0;       ///< (pass 2's repetitions: 1, 2, ...)
        virtual void set_changed() = 0;                         ///< (a type that this pass inferred changed: another pass)
        virtual void trace_output(neolib::i_string const& aText) = 0;
        virtual bool bigint_promotion() const = 0;              ///< (integers of any size (type::Integer))
        virtual bool overflow_traps() const = 0;                ///< (fixed size integer arithmetic traps on overflow)
        // the operand stack (bottom: 0)
    public:
        virtual std::size_t operand_count() const = 0;
        virtual std::size_t operand_base() const = 0;           ///< (the current function's first operand)
        virtual i_operand_type const& operand(std::size_t aIndex) const = 0;
        virtual void set_operand(std::size_t aIndex, i_operand_type const& aOperand) = 0;
        virtual void insert_operand(std::size_t aIndex, i_operand_type const& aOperand) = 0;
        virtual void erase_operands(std::size_t aIndex, std::size_t aCount = 1u) = 0;
        // values
    public:
        virtual i_fragment const& materialize_operand(i_operand_type const& aOperand) = 0;
        virtual i_fragment const& materialize_operand(i_operand_type const& aOperand, type aTargetType) = 0;
        virtual i_fragment const& coerce_value(i_fragment const& aValue, type aTarget) = 0;
        virtual void pop_fragments_into(neolib::i_string_view const& aSource, fragment_kind aKind, neolib::i_vector<i_fragment>& aFragments) = 0;
        virtual std::uint32_t temporary(type aType) = 0;
        virtual std::uint32_t string_literal(neolib::i_string const& aValue) = 0;
        virtual std::uint32_t runtime_function(neolib::i_string const& aName, neolib::i_vector<type> const& aParameters, type aReturnType) = 0;
        virtual bool runtime_plugin_available(neolib::i_string const& aNamespace) const = 0; ///< (a runtime plugin provides the namespace's functions)
        // source fragments
    public:
        virtual void import_fragment(neolib::i_string const& aSourceFilePath, neolib::i_string const& aSource) = 0; ///< (a fragment that the program 
                                                                ///< imports (once): compiled after the fragments before it)
        // functions
    public:
        virtual bool in_function() const = 0;
        virtual std::uint32_t current_function_index() const = 0;
        virtual void ensure_function() = 0;                     ///< (top-level statements: the entry point they form)
        virtual void suspend_entry() = 0;                       ///< (the entry point that top-level statements form, while a function is compiled)
        virtual void set_pending_inferred() = 0;                ///< (the function being declared has inferred parameter and result types)
        virtual neolib::i_string const& function_name(std::uint32_t aFunction) const = 0;
        virtual i_function_signature const& signature_of(std::uint32_t aFunction) const = 0;
        virtual bool function_inferred(std::uint32_t aFunction) const = 0;
        virtual bool function_implicit_entry(std::uint32_t aFunction) const = 0;
        virtual std::size_t forward_declaration_count() const = 0; ///< (the functions that the declaration pass found)
        virtual neolib::i_string_view const& forward_declaration_name(std::size_t aIndex) const = 0;
        virtual i_function_signature const& forward_declaration_signature(std::size_t aIndex) const = 0;
        virtual bool forward_declaration_stub(std::size_t aIndex) const = 0;
        virtual bool forward_declaration_inferred(std::size_t aIndex) const = 0;
        virtual std::size_t function_count() const = 0;
        virtual bool function_imported(std::uint32_t aFunction) const = 0;
        virtual bool function_stub(std::uint32_t aFunction) const = 0;
        virtual bool function_intrinsic(std::uint32_t aFunction) const = 0;
        virtual bool function_entry(std::uint32_t aFunction) const = 0;
        virtual i_fragment const& function_body(std::uint32_t aFunction) const = 0;    ///< (a finished function's code and relocations)
        virtual void set_function_body(std::uint32_t aFunction, i_fragment const& aBody) = 0;
        virtual std::uint32_t deferred_call(neolib::i_string const& aName, neolib::i_vector<type> const& aParameters) = 0; ///< (a call of a 
                                                                ///< function defined later: the relocation (DeferredCall))
        virtual type inferred_parameter(neolib::i_string_view const& aFunction, std::uint32_t aIndex) const = 0; ///< (UNKNOWN: none)
        virtual void set_inferred_parameter(neolib::i_string_view const& aFunction, std::uint32_t aIndex, std::uint32_t aCount, type aType) = 0;
        virtual void infer(neolib::i_string const& aWhat, type aOld, type aNew) = 0; ///< (aOld UNKNOWN: none)
        // symbols
    public:
        virtual i_scope const& global_scope() const = 0;
        virtual i_symbol_table_entry* find_symbol(i_scope const& aScope, neolib::i_string_view const& aName, bool aThisScopeOnly = false) const = 0;
        virtual i_symbol_table_entry& define_global(neolib::i_string const& aName, type aType) = 0;
        virtual void declare_globals() = 0;
        virtual bool is_global(i_symbol_table_entry const& aSymbol) const = 0;
        virtual bool is_constant(i_symbol_table_entry const& aSymbol) const = 0;
        virtual void set_constant(i_symbol_table_entry const& aSymbol, bool aConstant) = 0;
        virtual bool is_block_scoped(i_symbol_table_entry const& aSymbol) const = 0; ///< (it goes out of scope at the end of its block)
        virtual void set_block_scoped(i_symbol_table_entry const& aSymbol, bool aBlockScoped) = 0;
        virtual void retire_symbols(neolib::i_string_view const& aSource) = 0;
        // indented constructs being compiled (outermost: 0)
    public:
        virtual std::size_t indented_count() const = 0;
        virtual indented_construct indented_kind(std::size_t aIndex) const = 0;
        virtual bool indented_try_else(std::size_t aIndex) const = 0;
        virtual void set_loop_epilogue(i_fragment const& aEpilogue) = 0; ///< (the innermost (a while loop): code that follows the loop)
        virtual void finalize_suspended(std::size_t aIndent) = 0;       ///< (constructs (e.g. if statements) that an indentation ends)
        // global variables (found by the previous pass)
    public:
        virtual std::size_t global_count() const = 0;
        virtual neolib::i_string const& global_name(std::size_t aIndex) const = 0;
        virtual type global_type(std::size_t aIndex) const = 0;
        virtual void add_global(neolib::i_string const& aName, type aType) = 0;
        virtual void set_global_type(std::size_t aIndex, type aType) = 0;
        virtual bool entry_global(neolib::i_string_view const& aName) const = 0; ///< (only top-level code uses it: a local of the entry point)
        virtual void function_global(neolib::i_string const& aName) = 0;     ///< (a function uses it)
    };
}
