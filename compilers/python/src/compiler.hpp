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

// The Python compiler plugin's language compiler (see compiler.cpp)

#include <neos/neos.hpp>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <neos/language/language_compiler.hpp>
#include <neos/language/python.hpp>

namespace neos::compilers::python
{
    using namespace neos::language;
    using language::python::construct;

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
        // (a source fragment being compiled)
        struct fragment_state
        {
            struct py_target
            {
                enum class kind_t { Name, Item, Attribute } kind = kind_t::Name;
                std::string name;                           ///< (Name: the variable; Attribute: the attribute)
                std::optional<language::fragment> receiver; ///< (Item and Attribute)
                std::optional<language::fragment> key;      ///< (Item)
                char const* position = nullptr;             ///< (its source)
            };
            std::vector<py_target> pyTargets;               ///< assignment targets (the assignment that follows them pops them)
            struct py_access
            {
                language::fragment receiver;
                std::optional<language::fragment> key;      ///< (a subscript)
                std::string attribute;                      ///< (an attribute)
            };
            std::map<char const*, py_access> pyAccesses;    ///< subscripts and attributes (by their source), which can be targets
            std::map<char const*, std::vector<language::fragment>> pyTuples; ///< tuple displays (by their source): their items
            std::vector<std::size_t> pyComprehensions;      ///< comprehensions being compiled: the number of the statement's temporaries before them
            struct py_clause
            {
                std::vector<py_target> targets;             ///< (for)
                language::fragment value;                   ///< the iterable (for) or the condition (if)
                char const* position = nullptr;
            };
            std::vector<py_clause> pyClauses;               ///< the for and if clauses of comprehensions
            std::map<char const*, std::pair<std::uint32_t, type>> pyChains; ///< comparisons (by their source) that a chained comparison follows: 
                                                                    ///< the local that holds their right operand
        };
        // an int expression (see py_integer_operation): a fast version (inline, without calls or blocks: a value that is not
        // a small integer, or a result that would not be one, branches out of the block that encloses it (br_if 0)) and a slow
        // version (calls of the runtime plugin: its result is a new reference)
        struct py_integer_expression
        {
            fragment prefix;                    ///< code that runs first (operands' snapshots: calls)
            fragment fast;
            fragment slow;
            fragment zeroFast;                  ///< (if any) a cheaper fast version, whose value is zero if and only if the fast version's is
            bool bails = false;                 ///< the fast version can branch out
            bool stable = false;                ///< its versions read no variable (only temporaries that its prefix sets): other code 
                                                ///< that runs after its prefix does not change its value
            std::int64_t low = 0;               ///< the fast version's result is within [low, high]
            std::int64_t high = 0;
            std::set<std::uint32_t> small;      ///< the locals that the fast version checks are small integers
            std::uint32_t height = 1u;          ///< (the slow version's temporaries are those of its height)
            std::optional<std::uint32_t> holder; ///< the object temporary of the expression's value (as a whole)
        };
        // (a function being compiled)
        struct function_state
        {
            std::vector<std::uint32_t> pendingObjects;  ///< object temporaries of the statement being compiled (released at its end)
            std::vector<std::uint32_t> freeObjects;     ///< object temporaries that can be reused
            std::vector<std::uint32_t> objectVariables; ///< locals and parameters that hold objects (released when the function returns)
            std::vector<std::uint32_t> integerVariables; ///< locals and parameters that hold integers of any size (an integer object's 
                                                        ///< reference is released when the function returns)
            bool returns = false;                       ///< a return statement has been compiled
            std::set<std::uint32_t> objectTemporaries;  ///< all the locals used as object temporaries
            std::map<std::uint32_t, std::uint32_t> pyScratch; ///< locals that code uses only between its own instructions (by role)
            std::map<std::vector<std::uint8_t>, py_integer_expression> pyIntegers; ///< int expressions (by their code)
            std::vector<std::uint32_t> consumedObjects; ///< object temporaries of int expressions that other code took over
                                                        ///< (reusable after the statement)
            std::map<std::vector<std::uint8_t>, fragment> pyIntegerStrings; ///< str() of an int: the int (by the str's code)
            std::map<std::uint32_t, std::pair<std::int64_t, std::int64_t>> pyCounters; ///< int variables that only for loops over 
                                                        ///< ranges bind (small integers): their values' range
            std::vector<std::uint32_t> pendingIntegers; ///< integer temporaries of the statement being compiled
            std::vector<std::uint32_t> freeIntegers;    ///< integer temporaries that can be reused
        };
        // facts inferred from the code (each compilation pass uses the facts found by the previous one)
        struct inference
        {
            std::map<std::string, std::map<std::string, type>> pyLocals;  ///< function -> variables declared at its start (variables
                                                                    ///< that hold objects or whose type changes)
            std::set<std::string> pyFree;                           ///< names that functions use but do not bind
            std::set<std::string> pyModuleNames;                    ///< names that top-level statements bind
            std::map<std::string, std::set<std::string>> pyGlobals; ///< function -> names it declares global
            std::map<std::string, std::map<std::string, interval>> pyCounters; ///< function -> its int variables that only for loops over
                                                                    ///< ranges bind -> the range of their values (found by the 
                                                                    ///< previous pass)
            std::map<std::string, std::map<std::string, std::optional<interval>>> pyCounterBindings; ///< (this pass) function -> variable ->
                                                                    ///< the range of the values for loops over ranges bind (none: 
                                                                    ///< something else binds it)
        };
        // (the parameters of the program's functions)
        struct py_parameter
        {
            enum kind_t { Positional, KeywordOnly, VarArgs, VarKeywords } kind = Positional;
            std::string name;
            bool hasDefault = false;
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
        void preparse(i_source_fragment& aFragment) override;
        void emit_construct(std::uint32_t aConstruct, neolib::i_string_view const& aSource) override;
        bool complete_signature(i_function_signature& aSignature, neolib::i_string_view const& aSource) override;
        bool late_binding() const override;
        bool functions_follow_statements() const override;
        i_fragment const* literal(i_operand_type const& aOperand) override;
        i_fragment const* unbound_name(neolib::i_string_view const& aName) override;
        i_fragment const* convert_value(i_fragment const& aValue, type aTarget) override;
        i_fragment const* division_by_zero(std::int32_t aKind) override;
        type argument_type(i_operand_type const& aOperand) override;
        void function_start() override;
        i_fragment const* function_end(bool aImplicit) override;
        void finishing_function(bool aEntry) override;
        void function_finished(std::uint32_t aFunction) override;
        i_fragment const* finish_construct(i_indented_construct const& aConstruct) override;
        bool bind_name(neolib::i_string_view const& aName, type aType) override;
        bool declare_loop_variable(neolib::i_string_view const& aName, i_symbol_table_entry const* aSymbol) override;
        i_fragment const* small_integers(neolib::i_vector<std::uint32_t> const& aLocals) override;
        void loop_variable_range(neolib::i_string_view const& aName, std::int64_t aLow, std::int64_t aHigh) override;
        i_fragment const* release_integer(std::uint32_t aVariable, bool aGlobal) override;
    private:
        fragment_state& state();
        function_state& py_function();
        function_state& py_function(std::uint32_t aFunction);
        i_fragment const* result(fragment const& aFragment);
        void emit_python(construct aConstruct, neolib::i_string_view const& aSource);
        void py_join_lines(i_source_fragment& aFragment);
        void py_complete_signature(i_function_signature& aSignature, neolib::i_string_view const& aSource);
        operand_type py_pop();
        type py_type(operand_type const& aOperand) const;
        fragment py_object(operand_type const& aOperand);
        fragment py_owned(fragment aValue);
        fragment py_flush();
        fragment py_release_variables(bool aTemporaries, std::optional<std::uint32_t> aExcept = {});
        void py_coerce(fragment& aValue, type aTarget);
        fragment py_name(std::string const& aName);
        fragment py_runtime_call(std::string const& aName, std::vector<type> const& aParameters, type aResult, std::vector<fragment> const& aArguments);
        fragment py_string(std::string const& aValue);
        std::optional<fragment> py_big_literal(operand_type const& aOperand);
        fragment py_small_integers(std::vector<std::uint32_t> const& aLocals);
        fragment py_release_integer(std::uint32_t aVariable, bool aGlobal);
        fragment py_retain_integer(std::uint32_t aLocal);
        fragment py_park_integer(fragment aValue);
        std::uint32_t py_scratch(std::uint32_t aRole, type aType);
        bool py_big_integers() const;
        void py_bound(std::string const& aName, std::optional<interval> const& aCounter = {});
        void py_update_counters();
        std::optional<std::pair<std::int64_t, std::int64_t>> py_counter(std::uint32_t aLocal);
        void py_coerce_integer(fragment& aValue, type aTarget);
        void py_integer_operation(construct aConstruct, std::optional<operand_type> const& aLhs, operand_type const& aRhs);
        void py_integer_divide_or_power(construct aConstruct, std::optional<operand_type> const& aLhs, operand_type const& aRhs);
        std::optional<py_integer_expression> py_integer_expression_of(fragment const& aValue);
        fragment py_integer_value(py_integer_expression const& aExpression, bool aHeld);
        fragment py_integer_fragment(py_integer_expression aExpression);
        std::uint32_t py_integer_temporary();
        void py_operation(construct aConstruct, bool aInplace = false, neolib::i_string_view const* aSource = nullptr);
        void py_bind(fragment_state::py_target const& aTarget, operand_type const& aValue, fragment& aStatement);
        void py_bind_name(std::string const& aName, operand_type const& aValue, fragment& aStatement);
        i_symbol_table_entry& py_variable(std::string const& aName, type aType);
        void py_kind_change(std::string const& aName);
        void py_call(neolib::i_string_view const& aSource);
        std::optional<std::vector<fragment>> py_bind_arguments(std::string const& aFunction, std::vector<fragment> const& aArguments,
            std::vector<std::optional<std::string>> const& aKeywords);
        void py_assign(construct aConstruct, neolib::i_string_view const& aSource);
        void py_import(construct aConstruct, neolib::i_string_view const& aSource);
        void py_return(neolib::i_string_view const& aSource);
        void py_for_in(neolib::i_string_view const& aSource);
        void py_function_start();
        void py_comprehension(neolib::i_string_view const& aSource, bool aDict = false);
        std::uint32_t py_try_depth(bool aLoop);
        fragment py_function_end(bool aImplicit);
        void py_try(i_indented_construct const& aConstruct, fragment& aStatement);
        void py_with(i_indented_construct const& aConstruct, fragment& aStatement);
        void py_error_checks(std::uint32_t aFunction);
        std::uint32_t py_error_global();
    private:
        std::vector<fragment_state> iFragments;
        std::map<std::uint32_t, function_state> iFunctions;
        inference iInference;
        std::map<std::string, std::vector<py_parameter>> iPyParameters;
        fragment iResult;
    };
}
