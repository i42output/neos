/*
  compiler.hpp

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

// The JavaScript (ECMAScript) compiler plugin's language compiler (see compiler.cpp)

#include <neos/neos.hpp>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <neos/language/language_compiler.hpp>
#include <neos/language/javascript.hpp>

namespace neos::compilers::javascript
{
    using namespace neos::language;
    using language::javascript::construct;

    class compiler : public language_compiler
    {
    private:
        struct interval
        {
            std::int64_t low;
            std::int64_t high;
            bool contains(interval const& aOther) const { return aOther.low >= low && aOther.high <= high; }
            interval join(interval const& aOther) const { return { std::min(low, aOther.low), std::max(high, aOther.high) }; }
            bool operator==(interval const&) const = default;
        };
        using range_key = std::pair<char const*, std::string>;
        // (a source fragment being compiled)
        struct fragment_state
        {
            std::optional<construct> jsDeclaration;     ///< the kind of declaration being compiled
            std::unordered_map<char const*, language::fragment> jsStatementForms; ///< assignments and updates whose values are unused
            struct counter_loop
            {
                char const* test = nullptr;             ///< the loop's test
                char const* counter = nullptr;          ///< the variable (declaration) the test bounds
                std::optional<std::int64_t> maximum;    ///< the counter's largest value while the test is true
                std::optional<std::int64_t> minimum;    ///< the counter's smallest value while the test is true
            };
            std::vector<counter_loop> counterLoops;     ///< the loops being compiled (innermost last)
            struct counter_update
            {
                char const* counter;
                char const* test;                       ///< the loop whose test bounds it
                char const* position;
            };
            std::vector<counter_update> counterUpdates; ///< updates whose loop has not been compiled yet
        };
        // the facts inferred from the code (each compilation pass uses the facts found by the previous one)
        struct inference
        {
            std::map<char const*, type> declarations;               ///< variable declared without a value -> type of the value assigned to it
            std::map<std::string, std::vector<std::optional<type>>> arguments; ///< (this pass)
            // integer ranges: a number variable (declaration) or parameter ("function#index") whose values are 32-bit integers
            // (never -0) is an i32; the ranges of the values assigned to it must be within its range (else its range grows)
            std::map<range_key, interval> ranges;
            std::set<range_key> notInt32;                           ///< assigned a number that is not a 32-bit integer
            std::map<range_key, std::uint32_t> grown;               ///< (widened after growing once)
            std::set<range_key> narrowed;
            std::set<char const*> notCounters;                      ///< variables that are not loop counters
            std::map<range_key, interval> observed;                 ///< (this pass) ranges of the values assigned
            std::map<char const*, std::uint32_t> assignments;       ///< (this pass) number of assignments to a variable
            std::set<char const*> counters;                         ///< (this pass) variables updated as loop counters
            std::map<std::string, bool> declaredGlobals;            ///< (this pass) top-level declarations (true: var)
        };
        // (a variable's declaration)
        struct symbol_info
        {
            char const* declaration = nullptr;          ///< declared without a value (its type is that of the value first assigned to it)
            char const* site = nullptr;                 ///< its declaration
        };
    public:
        compiler();
        // i_language_compiler
    public:
        void begin_compilation() override;
        void begin_pass() override;
        void end_pass() override;
        void begin_fragment(i_source_fragment& aFragment) override;
        void end_fragment() override;
        void emit_construct(std::uint32_t aConstruct, neolib::i_string_view const& aSource) override;
        type inferred_type(i_operand_type const& aOperand) override;
        type parameter_type(neolib::i_string_view const& aFunction, std::uint32_t aIndex, type aType) override;
        bool value_range(i_operand_type const& aOperand, std::int64_t& aLow, std::int64_t& aHigh) override;
    private:
        fragment_state& js();
        void emit_js(construct aConstruct, neolib::i_string_view const& aSource);
        void emit_js_operation(construct aOperation);
        type js_type(operand_type const& aOperand) const;
        operand_type js_to_number(operand_type const& aOperand);
        operand_type js_to_string(operand_type const& aOperand, bool aInspect = false);
        fragment js_to_boolean(operand_type const& aOperand);
        fragment js_to_int32(operand_type const& aOperand);
        void js_declarator(neolib::i_string_view const& aSource);
        void js_assignment(construct aConstruct, neolib::i_string_view const& aSource);
        void js_call(neolib::i_string_view const& aSource);
        void js_member(construct aConstruct, neolib::i_string_view const& aSource);
        bool js_builtin(std::string const& aName, std::optional<operand_type> const& aReceiver, std::vector<fragment>& aArguments, neolib::i_string_view const& aSource);
        void js_if(neolib::i_string_view const& aSource);
        void js_loop(construct aConstruct, neolib::i_string_view const& aSource);
        void js_conditional(neolib::i_string_view const& aSource);
        void js_variable(neolib::i_string_view const& aSource);
        void js_global_statement(neolib::i_string_view const& aSource);
        void js_statement(neolib::i_string_view const& aSource);
        void js_branch(construct aConstruct, neolib::i_string_view const& aSource);
        void js_case(construct aConstruct, neolib::i_string_view const& aSource);
        void js_switch(neolib::i_string_view const& aSource);
        void js_loop_test(neolib::i_string_view const& aSource);
        std::uint32_t js_runtime(std::string const& aName, std::vector<type> const& aParameters, type aReturnType);
        std::optional<type> integer_type(operand_type const& aOperand) const;
        std::optional<interval> integer_range(operand_type const& aOperand) const;
        std::optional<range_key> range_key_of(i_symbol_table_entry const& aSymbol, std::string const& aName) const;
        void observe_range(range_key const& aKey, std::optional<interval> const& aRange);
        void update_ranges();
    private:
        std::vector<fragment_state> iFragments;
        inference iInference;
        std::unordered_map<i_symbol_table_entry const*, symbol_info> iSymbolInfo;
        std::optional<std::pair<range_key, interval>> iRangeBound;  ///< (a loop counter's range when it is updated)
    };
}
