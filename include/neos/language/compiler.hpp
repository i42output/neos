/*
  compiler.hpp

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

#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <neos/neos.hpp>
#include <neolib/core/optional.hpp>
#include <neolib/core/string.hpp>
#include <neolib/core/string_view.hpp>
#include <neos/fwd.hpp>
#include <neos/language/schema.hpp>
#include <neos/language/ast.hpp>
#include <neos/language/javascript.hpp>
#include <neos/language/python.hpp>
#include <neos/language/semantic_concept.hpp>
#include <neos/language/i_concept_library.hpp>
#include <neos/language/i_compiler.hpp>
#include <neos/language/scope.hpp>
#include <neos/language/symbol_table.hpp>

namespace neos::language
{
    class source_fragment : public i_source_fragment
    {
    public:
        source_fragment(const optional_source_path_t& aSourceFilePath = optional_source_path_t{}, const source_t& aSource = source_t{}) :
            iSourceFilePath{ aSourceFilePath }, iSource{ aSource }, iImported{ false }, iStatus{ compilation_status::Pending }
        {
        }
        source_fragment(const i_source_fragment& aFragment) :
            iSourceFilePath{ aFragment.source_file_path() }, iSource{ aFragment.source() }, iImported{ aFragment.imported() }, iStatus{ aFragment.status() }
        {
        }
    public:
        const i_optional_source_path_t& source_file_path() const final
        { 
            return iSourceFilePath;
        }
        i_optional_source_path_t& source_file_path() final
        { 
            return iSourceFilePath;
        }
        i_optional_source_path_t const& source_directory_path() const final
        {
            thread_local optional_source_path_t result;
            result = std::nullopt;
            if (source_file_path().has_value())
            {
                result = std::filesystem::path{ source_file_path().value().to_std_string() }.parent_path().string();
            }
            return result;
        }
        const i_source_t& source() const final
        { 
            iSourceView = iSource;
            return iSourceView;
        }
        void set_source(i_source_t const& aSource) final
        {
            iSource = neolib::string{ aSource };
        }
        bool imported() const final
        {
            return iImported;
        }
        void set_imported() final
        {
            iImported = true;
        }
        compilation_status status() const final
        { 
            return iStatus; 
        }
        void set_status(compilation_status aStatus) final
        {
            iStatus = aStatus;
        }
    public:
        const_source_iterator cbegin() const final
        { 
            return source().cbegin(); 
        }
        const_source_iterator cend() const final
        { 
            return source().cend(); 
        }
        const_source_iterator begin() const final
        { 
            return source().begin();
        }
        const_source_iterator end() const final
        { 
            return source().end();
        }
        source_iterator begin() final
        { 
            return source().begin();
        }
        source_iterator end() final
        { 
            return source().end();
        }
    private:
        optional_source_path_t iSourceFilePath;
        neolib::string iSource;
        mutable source_t iSourceView;
        bool iImported;
        mutable compilation_status iStatus;
    };

    typedef std::shared_ptr<schema> schema_pointer_t;
    typedef std::list<source_fragment> source_fragments_t;

    struct source_fragment_not_found : std::logic_error { source_fragment_not_found() : std::logic_error("neos::language::source_fragment_not_found") {} };

    struct translation_unit
    {
        schema_pointer_t schema;
        source_fragments_t fragments;
        ast ast;
        const source_fragment& fragment(const_source_iterator aSource) const
        {
            for (auto const& f : fragments)
                if (aSource >= f.begin() && aSource <= f.end())
                    return f;
            throw source_fragment_not_found();
        }
        source_fragment& fragment(const_source_iterator aSource)
        {
            return const_cast<source_fragment&>(const_cast<const translation_unit*>(this)->fragment(aSource));
        }
        bool pending() const
        {
            for (auto const& f : fragments)
                if (f.status() == compilation_status::Pending)
                    return true;
            return false;
        }
        bool compiling() const
        {
            for (auto const& f : fragments)
                if (f.status() == compilation_status::Compiling)
                    return true;
            return false;
        }
        bool compiled() const
        {
            return !pending() && !compiling();
        }
    };

    typedef std::vector<translation_unit> translation_units_t;

    struct program
    {
        translation_units_t translationUnits;
        scope<> scope;
        symbol_table symbolTable;
        text text;
    };

    using fold_stack = neolib::vector<neolib::ref_ptr<i_ast_node>>;

    class compiler : public i_compiler
    {
    private:
        using source_iterator = const_source_iterator;
        struct compilation_state
        {
            program* program;
            translation_unit* unit;
            i_source_fragment* fragment;
            std::uint32_t level = 0u;
            fold_stack foldStack = {};
            std::vector<neolib::ref_ptr<i_scope>> scopeStack;
            std::vector<operand_type> operandStack;
            std::vector<operator_type> operatorStack;
            std::optional<std::uint32_t> pendingFunction;
            std::optional<std::uint32_t> function;
            bool pendingEntry = false;
            struct indented
            {
                indented_construct kind;
                std::size_t indent;
                std::size_t base;
                std::size_t bodyBase;
                bool suspended = false;
                bool implicit = false;
                std::optional<std::size_t> bodyIndent;
                std::vector<language::fragment> conditions;
                std::vector<language::fragment> arms;
                std::optional<language::fragment> elseArm;
                std::optional<language::fragment> loopCondition;
                i_symbol_table_entry* loopVariable = nullptr;
                std::vector<language::fragment> range;
                std::optional<language::fragment> epilogue;   ///< (a while loop) code that follows the loop
                // (try: the handlers' exception types (conditions; none: any) and the names they bind, and finally's body)
                std::vector<std::optional<language::fragment>> handlerTypes;
                std::vector<std::optional<std::string>> handlerNames;
                std::optional<language::fragment> finallyArm;
                bool tryElse = false;   ///< (else follows the handlers)
            };
            std::vector<indented> indentStack;
            bool implicitFunctionFinished = false;
            // JavaScript
            std::optional<javascript::construct> jsDeclaration;  ///< the kind of declaration being compiled
            bool pendingInferred = false;               ///< the function being declared has inferred types
            std::optional<std::pair<std::uint32_t, neolib::ref_ptr<i_scope>>> suspendedEntry; ///< the implicit entry point while a function is compiled
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
            // Python
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
        struct symbol_info
        {
            char const* position = nullptr; ///< definition position (nullptr if defined at function level)
            std::uint64_t sequence = 0u;
            bool retired = false;
            bool global = false;                        ///< a global variable (index is that of the WASM global)
            bool constant = false;
            char const* declaration = nullptr;          ///< declared without a value (its type is that of the value first assigned to it)
            char const* site = nullptr;                 ///< (JavaScript) its declaration
        };
        struct forward_declaration
        {
            std::string name;
            function_signature signature;
            bool stub = false;
            bool inferred = false;
        };
        struct wasm_function
        {
            std::string name;
            bool imported = false;
            bool fromImportedFragment = false;
            bool entry = false;
            bool compiled = false;
            bool stub = false;
            bool inferred = false;                      ///< parameter and return types are inferred
            bool implicitEntry = false;                 ///< the entry point formed by top-level statements
            std::string importNamespace;                ///< (an import of a stub) the runtime plugin's namespace
            std::optional<std::uint32_t> intrinsic;     ///< (a stub that is a string intrinsic) the string header field it reads
            i_function_scope const* scope = nullptr;
            function_signature signature;
            std::vector<type> locals;
            std::vector<std::uint8_t> body;
            std::vector<std::pair<std::uint32_t, std::uint32_t>> relocations;
            std::size_t operandBase = 0u;
            // (Python)
            std::vector<std::uint32_t> pendingObjects;  ///< object temporaries of the statement being compiled (released at its end)
            std::vector<std::uint32_t> freeObjects;     ///< object temporaries that can be reused
            std::vector<std::uint32_t> objectVariables; ///< locals and parameters that hold objects (released when the function returns)
            bool returns = false;                       ///< a return statement has been compiled
            std::set<std::uint32_t> objectTemporaries;  ///< (Python) all the locals used as object temporaries
        };
        struct wasm_module
        {
            std::vector<wasm_function> functions;
            std::vector<std::uint8_t> data;
            std::map<std::string, std::uint32_t> strings;
            std::vector<std::pair<std::string, std::vector<type>>> deferredCalls; ///< calls to functions defined later (resolved at emission)
        };
        struct interval
        {
            std::int64_t low;
            std::int64_t high;
            bool contains(interval const& aOther) const { return aOther.low >= low && aOther.high <= high; }
            interval join(interval const& aOther) const { return { std::min(low, aOther.low), std::max(high, aOther.high) }; }
            bool operator==(interval const&) const = default;
        };
        using range_key = std::pair<char const*, std::string>;
        // types inferred from the code (each compilation pass uses the types found by the previous one; passes are 
        // repeated until they no longer change)
        struct inference
        {
            std::map<std::string, std::vector<type>> parameters;    ///< function -> types of its arguments
            std::map<std::string, type> results;                    ///< function -> type of the values it returns
            std::vector<std::pair<std::string, type>> globals;     ///< global variables (in order of declaration)
            std::map<char const*, type> declarations;               ///< variable declared without a value -> type of the value assigned to it
            std::map<std::string, std::vector<std::optional<type>>> arguments; ///< (this pass)
            std::map<std::string, type> returned;                   ///< (this pass)
            std::set<std::string> functionGlobals;                  ///< (this pass) global variables used by functions
            std::set<std::string> entryGlobals;                     ///< global variables only used by top-level code (locals of the entry point)
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
            std::uint32_t pass = 0u;
            std::map<std::string, bool> declaredGlobals;            ///< (this pass) top-level declarations (true: var)
            // (Python)
            std::map<std::string, std::map<std::string, type>> pyLocals;  ///< function -> variables declared at its start (variables
                                                                    ///< that hold objects or whose type changes)
            std::set<std::string> pyFree;                           ///< names that functions use but do not bind
            std::set<std::string> pyModuleNames;                    ///< names that top-level statements bind
            std::map<std::string, std::set<std::string>> pyGlobals; ///< function -> names it declares global
            bool changed = false;
        };
    public:
        static constexpr std::uint32_t BranchTarget = 0x40000000u;  ///< relocation: break (or continue) out of the enclosing loop
        static constexpr std::uint32_t ContinueTarget = 0x20000000u;
    private:
        static constexpr std::uint32_t DeferredCall = 0x80000000u;
        using compilation_state_stack_t = std::vector<std::unique_ptr<compilation_state>>;
    public:
        compiler(i_context& aContext);
    public:
        bool compile(program& aProgram);
        bool compile(program& aProgram, translation_unit& aUnit);
        bool compile(program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment);
        bool compile(const i_source_fragment& aFragment) final;
        i_source_fragment const& current_fragment() const final;
        i_scope const& current_scope() const final;
        i_scope& enter_scope(scope_type aScopeType, neolib::i_string const& aScopeName) final;
        void leave_scope(scope_type aScopeType) final;
        i_operand_type const& lhs_operand() const final;
        i_operand_type const& rhs_operand() const final;
        void push_operand(i_operand_type const& aOperand) final;
        void pop_operand(i_data_type& aOperand) final;
        void push_operator(i_operator_type const& aOperator) final;
        void pop_operator(i_operator_type& aOperator) final;
        void define_symbol(i_symbol_name const& aName, symbol_type aType, neolib::i_string_view const& aSource) final;
        void find_identifier(neolib::i_string_view const& aIdentifier, neolib::i_optional<i_data_type>& aResult) const final;
    public:
        void declare_import(i_function_signature const& aSignature) final;
        void set_entry_point() final;
        void emit_operation(operation aOperation) final;
        void emit_statement(neolib::i_string_view const& aSource) final;
        void emit_block(neolib::i_string_view const& aSource) final;
        void emit_return(neolib::i_string_view const& aSource) final;
        void emit_assignment(neolib::i_string_view const& aTarget, neolib::i_string_view const& aSource) final;
        void emit_argument(neolib::i_string_view const& aSource) final;
        void emit_call(neolib::i_string_view const& aFunctionName, neolib::i_string_view const& aSource) final;
        void emit_if(neolib::i_string_view const& aSource) final;
        void emit_else_if(neolib::i_string_view const& aSource) final;
        void emit_else(neolib::i_string_view const& aSource) final;
        void emit_loop(loop_type aLoopType, neolib::i_string_view const& aSource) final;
        void indentation(neolib::i_string_view const& aLineBreak) final;
        void begin_indented(indented_construct aConstruct, neolib::i_string_view const& aSource) final;
        void emit_indented_statement(neolib::i_string_view const& aSource) final;
        void end_indented() final;
        void emit_binding(neolib::i_string_view const& aTarget, neolib::i_string_view const& aSource) final;
        void emit_compound_assignment(operation aOperation, neolib::i_string_view const& aTarget, neolib::i_string_view const& aSource) final;
        void declare_loop_variable(neolib::i_string_view const& aName, neolib::i_string_view const& aSource) final;
        void declare_stub() final;
    public:
        void emit_language_construct(neolib::i_string_view const& aLanguage, std::uint32_t aConstruct, neolib::i_string_view const& aSource) final;
        void complete_signature(i_function_signature& aSignature, neolib::i_string_view const& aSource) final;
    public:
        void throw_error(source_iterator aSourcePos, neolib::i_string const& aError, neolib::i_string const& aErrorType = "error"_s) final;
        std::uint32_t trace() const final;
        const std::optional<std::string>& trace_filter() const;
        void set_trace(std::uint32_t aTrace, const std::optional<std::string>& aFilter = {});
        const std::chrono::steady_clock::time_point& start_time() const;    
        const std::chrono::steady_clock::time_point& end_time() const;
        std::chrono::steady_clock::duration preparse_time() const; ///< (the time the last compilation spent in preparsing)
        std::chrono::steady_clock::duration parse_time() const;    ///< (the time the last compilation spent parsing)
        bool show_parse_times() const;                              ///< (each parse's time is output; off by default)
        void set_show_parse_times(bool aShowParseTimes);
        bool preparse() const;                                      ///< (a language's own pass over a source before it is parsed
        void set_preparse(bool aPreparse);                          ///< (e.g. Python's joining of lines within brackets); on by default)
    private:
        const compilation_state& state() const;
        compilation_state& state();
        fold_stack& fold_stack();
        i_symbol_table_entry* find_symbol(i_scope const& aScope, neolib::i_string_view const& aName, bool aThisScopeOnly = false) const;
        wasm_function& current_function();
        std::uint32_t add_function(std::string const& aName, bool aImported);
        std::uint32_t runtime_function(std::string const& aName, std::vector<type> const& aParameters, type aReturnType);
        void finish_function();
        fragment materialize(operand_type const& aOperand, std::optional<type> aTargetType = {});
        fragment pop_value();
        std::vector<fragment> pop_fragments(neolib::i_string_view const& aSource, fragment_kind aKind);
        std::uint32_t string_literal(std::string const& aValue);
        void emit_module(program& aProgram);
        void retire_symbols(neolib::i_string_view const& aSource);
        std::uint32_t temporary(type aType);
        void check_rebinding(i_symbol_table_entry const& aVariable, neolib::i_string_view const& aName, type aValueType) const;
        std::optional<std::size_t> line_indent(neolib::i_string_view const& aSource) const;
        void close_indented(std::size_t aIndent);
        void finalize_suspended(std::size_t aIndent);
        void finalize_indented();
        void ensure_function();
        fragment take_body(std::size_t aBodyBase);
        void wrap_expression_body(neolib::i_string_view const& aSource);
        // JavaScript
        void emit_js(javascript::construct aConstruct, neolib::i_string_view const& aSource);
        void emit_js_operation(javascript::construct aOperation);
        type js_type(operand_type const& aOperand) const;
        operand_type js_to_number(operand_type const& aOperand);
        operand_type js_to_string(operand_type const& aOperand, bool aInspect = false);
        fragment js_to_boolean(operand_type const& aOperand);
        fragment js_to_int32(operand_type const& aOperand);
        operand_type pop_operand_value();
        void js_declarator(neolib::i_string_view const& aSource);
        void js_assignment(javascript::construct aConstruct, neolib::i_string_view const& aSource);
        void js_call(neolib::i_string_view const& aSource);
        void js_member(javascript::construct aConstruct, neolib::i_string_view const& aSource);
        bool js_builtin(std::string const& aName, std::optional<operand_type> const& aReceiver, std::vector<fragment>& aArguments, neolib::i_string_view const& aSource);
        void js_if(neolib::i_string_view const& aSource);
        void js_loop(javascript::construct aConstruct, neolib::i_string_view const& aSource);
        void js_conditional(neolib::i_string_view const& aSource);
        void js_variable(neolib::i_string_view const& aSource);
        void js_global_statement(neolib::i_string_view const& aSource);
        void js_statement(neolib::i_string_view const& aSource);
        void js_branch(javascript::construct aConstruct, neolib::i_string_view const& aSource);
        void js_case(javascript::construct aConstruct, neolib::i_string_view const& aSource);
        void js_switch(neolib::i_string_view const& aSource);
        void declare_globals();
        i_symbol_table_entry& define_global(std::string const& aName, type aType);
        void infer(std::string const& aWhat, std::optional<type> const& aOld, type aNew);
        void emit_call_to(fragment& aTarget, std::uint32_t aFunction);
        std::uint32_t js_runtime(std::string const& aName, std::vector<type> const& aParameters, type aReturnType);
        bool is_global(i_symbol_table_entry const& aSymbol) const;
        bool inline_call(fragment& aCall, wasm_function const& aCallee, std::size_t aParameterCount);
        bool strip_call(fragment& aFragment, std::string_view aVmFunction);
        std::optional<interval> integer_range(operand_type const& aOperand) const;
        std::optional<range_key> range_key_of(i_symbol_table_entry const& aSymbol, std::string const& aName) const;
        void observe_range(range_key const& aKey, std::optional<interval> const& aRange);
        void update_ranges();
        void js_loop_test(neolib::i_string_view const& aSource);
        std::optional<type> integer_type(operand_type const& aOperand) const;
        void suspend_entry();
        void resume_entry();
        void coerce(fragment& aValue, type aTarget);
        // Python
        bool is_python() const;
        void emit_python(python::construct aConstruct, neolib::i_string_view const& aSource);
        void py_join_lines(i_source_fragment& aFragment);
        void py_complete_signature(i_function_signature& aSignature, neolib::i_string_view const& aSource);
        operand_type py_pop();
        type py_type(operand_type const& aOperand) const;
        fragment py_object(operand_type const& aOperand);
        fragment py_owned(fragment aValue);
        fragment py_flush();
        fragment py_release_variables(bool aTemporaries);
        void py_coerce(fragment& aValue, type aTarget);
        fragment py_name(std::string const& aName);
        fragment py_runtime_call(std::string const& aName, std::vector<type> const& aParameters, type aResult, std::vector<fragment> const& aArguments);
        fragment py_string(std::string const& aValue);
        std::optional<fragment> py_big_literal(operand_type const& aOperand);
        void py_operation(python::construct aConstruct, bool aInplace = false, neolib::i_string_view const* aSource = nullptr);
        void py_bind(compilation_state::py_target const& aTarget, operand_type const& aValue, fragment& aStatement);
        void py_bind_name(std::string const& aName, operand_type const& aValue, fragment& aStatement);
        i_symbol_table_entry& py_variable(std::string const& aName, type aType);
        void py_kind_change(std::string const& aName);
        void py_call(neolib::i_string_view const& aSource);
        std::optional<std::vector<fragment>> py_bind_arguments(std::string const& aFunction, std::vector<fragment> const& aArguments,
            std::vector<std::optional<std::string>> const& aKeywords);
        void py_assign(python::construct aConstruct, neolib::i_string_view const& aSource);
        void py_import(python::construct aConstruct, neolib::i_string_view const& aSource);
        void py_return(neolib::i_string_view const& aSource);
        void py_for_in(neolib::i_string_view const& aSource);
        void py_function_start();
        void py_comprehension(neolib::i_string_view const& aSource, bool aDict = false);
        std::uint32_t py_try_depth(bool aLoop);
        fragment py_function_end(bool aImplicit);
        void py_try(compilation_state::indented& aConstruct, fragment& aStatement);
        void py_with(compilation_state::indented& aConstruct, fragment& aStatement);
        void py_error_checks(wasm_function& aFunction);
        std::uint32_t py_error_global();
        bool fold();
        bool fold2();
        static std::string location(const translation_unit& aUnit, const i_source_fragment& aFragment, source_iterator aSourcePos, bool aShowFragmentFilePath = true);
    private:
        i_context& iContext;
        std::uint32_t iTrace;
        std::optional<std::string> iTraceFilter;
        std::chrono::steady_clock::time_point iStartTime;
        std::chrono::steady_clock::time_point iEndTime;
        std::chrono::steady_clock::duration iPreparseTime = {};
        std::chrono::steady_clock::duration iParseTime = {};
        bool iShowParseTimes = false;
        bool iPreparse = true;
        compilation_state_stack_t iCompilationStateStack;
        wasm_module iModule;
        std::unordered_map<i_symbol_table_entry const*, symbol_info> iSymbolInfo;
        std::uint64_t iSymbolSequence = 0u;
        bool iDeclarationPass = false;
        std::vector<forward_declaration> iForwardDeclarations;
        // (Python: the parameters of the program's functions)
        struct py_parameter
        {
            enum kind_t { Positional, KeywordOnly, VarArgs, VarKeywords } kind = Positional;
            std::string name;
            bool hasDefault = false;
        };
        std::map<std::string, std::vector<py_parameter>> iPyParameters;
        std::vector<forward_declaration> iDeclaredFunctions;    ///< (as found by the declaration pass)
        inference iInference;
        bool iGlobalsDeclared = false;
        std::map<void const*, std::pair<bool, std::string>> iParsed; ///< parser -> source of its last successful parse
        std::optional<std::pair<range_key, interval>> iRangeBound;  ///< (a loop counter's range when it is updated)
    };
}