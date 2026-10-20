/*
  compiler.hpp

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
    // (nested functions, closures and decorated functions: the module's source rewritten (closures.cpp))
    std::string rewrite_nested_functions(std::string const& aText, bool& aChanged, std::map<std::string, std::vector<std::pair<std::string, std::string>>>* aAnnotations = nullptr,
        std::vector<std::size_t>* aLines = nullptr);   ///< (aLines: the line of aText (0-based) that each line of the result is)
    std::string rewrite_module_names(std::string const& aText, std::string const& aPrefix, std::string const& aGetter, std::vector<std::string>& aNames,
        std::optional<std::vector<std::string>>& aAll, std::set<std::string> const& aBuiltins);
    // (compound statements that are compiled as others (e.g. a loop's else clause): the module's source rewritten (statements.cpp))
    std::string rewrite_statements(std::string const& aText, bool& aChanged);
    // (class statements: functions that return their namespaces (statements.cpp))
    std::string rewrite_classes(std::string const& aText, bool& aChanged);
    std::string rewrite_class_namespaces(std::string const& aText, bool& aChanged);
    // (comprehensions: their variables are their own (statements.cpp))
    std::string rewrite_comprehensions(std::string const& aText, bool& aChanged, bool aBuiltins = true);
    std::string rewrite_int_loops(std::string const& aText, bool& aChanged, std::vector<std::size_t>& aLines);

    // (a syntax error as CPython reports it (syntax.cpp): its stage (the first error of a stage is reported: the tokenizer's and parser's (in 
    // source order), then the symbol table's, then the compiler's), type, message and location (a line and a character offset, 1-based))
    struct syntax_issue
    {
        enum stage_t { Tokenizer, Parser, Symtable, Compiler } stage = Parser;
        std::string type = "SyntaxError";   ///< (or IndentationError, TabError)
        std::string message;
        std::size_t position = 0u;          ///< (in the source)
        std::size_t endPosition = 0u;
        std::size_t order = 0u;             ///< (where the tokenizer finds it: its position (or the end of the source))
        std::uint32_t line = 1u;
        std::int64_t offset = 0;
        std::uint32_t endLine = 0u;
        std::int64_t endOffset = 0;
        bool lineText = true;               ///< (its text is its line with its newline (false: the tokenizer's: without))
    };
    // (a source's syntax errors (aExpression: eval's): the tokenizer's first (if any: only it), else those of its statements)
    std::vector<syntax_issue> syntax_issues(std::string const& aSource, bool aExpression);
    // (the parser's error at a position: CPython's message for it)
    syntax_issue parse_failure_issue(std::string const& aSource, std::size_t aPosition, bool aExpression);
    std::string source_line(std::string const& aSource, std::uint32_t aLine);
    std::string join_bracketed_lines(std::string const& aSource);
    std::string syntax_error_report(std::string const& aFile, std::string const& aSource, syntax_issue const& aIssue);

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
                enum class kind_t { Name, Item, Attribute, Group } kind = kind_t::Name;
                std::string name;                           ///< (Name: the variable; Attribute: the attribute)
                std::optional<language::fragment> receiver; ///< (Item and Attribute)
                std::optional<language::fragment> key;      ///< (Item)
                char const* position = nullptr;             ///< (its source)
                bool starred = false;                       ///< (*name: a list of the values that the other targets do not take)
                std::vector<py_target> children;            ///< (Group: its targets ("(a, b), c = ..."))
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
            i_source_fragment const* source = nullptr;      ///< the fragment being compiled
            std::optional<std::size_t> module;              ///< (an imported module's fragment: the module (iPyModules))
            std::optional<std::map<std::string, std::size_t, std::less<>>> pyIdentifiers; ///< (see py_observed) the number of times each
                                                                    ///< identifier occurs in the fragment's source (none: not counted yet)
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
            bool parametersRegistered = false;          ///< its parameters are in objectVariables and integerVariables (see 
                                                        ///< py_register_parameters)
            std::set<std::uint32_t> objectTemporaries;  ///< all the locals used as object temporaries
            std::map<std::uint32_t, std::uint32_t> pyScratch; ///< locals that code uses only between its own instructions (by role)
            std::map<std::vector<std::uint8_t>, py_integer_expression> pyIntegers; ///< int expressions (by their code)
            std::vector<std::uint32_t> consumedObjects; ///< object temporaries of int expressions that other code took over
                                                        ///< (reusable after the statement)
            std::map<std::vector<std::uint8_t>, fragment> pyIntegerStrings; ///< str() of an int: the int (by the str's code)
            std::map<std::vector<std::uint8_t>, std::array<fragment, 3u>> pyComparisons; ///< comparisons of objects: the operator and 
                                                        ///< the operands (by the comparison's code)
            std::map<std::uint32_t, std::pair<std::int64_t, std::int64_t>> pyCounters; ///< int variables that only for loops over 
                                                        ///< ranges bind (small integers): their values' range
            std::vector<std::string> pyBindingOrder;    ///< the names that the function binds (in the order of their first bindings)
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
            std::map<std::string, std::set<std::string>> pyBound;   ///< function -> the names it binds (its local variables)
            std::map<std::string, std::set<std::string>> pyDeleted; ///< function -> the names it deletes (del name: unbound)
            std::set<std::string> pyDeletedGlobals;                 ///< (the module's names that are deleted)
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
            bool positionalOnly = false;    ///< (a parameter before "/")
        };
    public:
        compiler();
        // i_language_compiler
    public:
        void set_options(neolib::i_string const& aOptions) override;
        bool compiles_at_run_time() const override;
        void begin_compilation() override;
        void begin_pass() override;
        void end_pass() override;
        void begin_fragment(i_source_fragment& aFragment) override;
        void end_fragment() override;
        void preparse(i_source_fragment& aFragment) override;
        void parsed(i_source_fragment& aFragment) override;
        void diagnose(i_source_fragment const& aFragment, std::uint32_t& aLine, std::uint32_t& aColumn, std::uint32_t& aEndLine, std::uint32_t& aEndColumn,
            neolib::i_string& aType, neolib::i_string& aMessage, neolib::i_string& aReport) override;
        void emit_construct(std::uint32_t aConstruct, neolib::i_string_view const& aSource) override;
        bool complete_signature(i_function_signature& aSignature, neolib::i_string_view const& aSource) override;
        bool late_binding() const override;
        bool functions_follow_statements() const override;
        void fragment_function(i_source_fragment const& aFragment, neolib::i_string& aName) override;
        i_fragment const* literal(i_operand_type const& aOperand) override;
        i_fragment const* unbound_name(neolib::i_string_view const& aName) override;
        i_fragment const* void_value(i_fragment const& aStatement) override;
        i_fragment const* variable_value(i_fragment const& aValue, neolib::i_string_view const& aName, i_symbol_table_entry const& aVariable) override;
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
        std::uint32_t source_line(i_source_fragment const& aFragment, std::size_t aPosition) override;
        bool frame_name(std::uint32_t aFunction, neolib::i_string& aName) override;
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
        fragment py_unpack_call(fragment const& aValue, std::vector<fragment_state::py_target> const& aTargets);
        fragment py_owned(fragment aValue);
        fragment py_flush();
        fragment py_release_temporaries(fragment const& aCode, std::optional<std::uint32_t> aExcept = {});
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
        void py_bind_targets(std::vector<fragment_state::py_target> const& aTargets, operand_type const& aValue, fragment& aStatement);
        void py_declare_targets(std::vector<fragment_state::py_target> const& aTargets);
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
        bool py_observed(std::string const& aName);
        void py_function_start();
        void py_register_parameters();
        void py_comprehension(neolib::i_string_view const& aSource, bool aDict = false, bool aSet = false);
        std::uint32_t py_try_depth(bool aLoop);
        fragment py_function_end(bool aImplicit);
        void py_try(i_indented_construct const& aConstruct, fragment& aStatement);
        void py_with(i_indented_construct const& aConstruct, fragment& aStatement);
        void py_error_checks(std::uint32_t aFunction);
        std::uint32_t py_error_global();
        std::uint32_t py_depth_global();
        std::optional<fragment> py_constant_str(fragment const& aValue);
        fragment py_made_once(std::string const& aName, fragment const& aMake);
    private:
        std::vector<fragment_state> iFragments;
        std::map<std::uint32_t, function_state> iFunctions;
        inference iInference;
        std::map<std::string, std::vector<py_parameter>> iPyParameters;
        std::map<std::string, std::string> iPyDocstrings;    ///< (function -> its docstring)
        std::map<std::string, std::string> iPyFrameNames;    ///< (a function that a rewrite made -> the name of its frame (its closure's, generator's, ...))
        std::map<std::string, std::vector<std::uint32_t>> iPyLines; ///< (a source as rewritten -> the line (1-based; 0: none) of the source as written 
                                                            ///< that each of its lines is)
        struct line_cache
        {
            char const* source = nullptr;
            std::size_t size = 0u;
            std::vector<std::uint32_t> const* lines = nullptr;
            std::vector<std::size_t> starts;
        } iPyLineCache;                                     ///< (source_line's: of the source it was last asked about)
        std::set<std::string> iPyPackageFunctions;           ///< (the functions of the language's packages (builtins.py): as values, the builtins)
        std::map<std::string, char> iPyFlavours;             ///< (function -> G: a generator function, C: a coroutine function, A: an async generator function)
        std::map<std::string, std::vector<std::pair<std::string, std::string>>> iPyAnnotations; ///< (a function of a module -> the sources of its annotations)
        std::set<std::string> iPyPreparsed;                 ///< (the sources that preparse has made: not rewritten again (a later pass))
        struct py_fragment_syntax
        {
            std::string original;                           ///< (its source, as written)
            std::string file;                               ///< (as CPython names it in a report)
            bool expression = false;                        ///< (eval's: its source is wrapped (an offset of its first character))
            std::size_t first = 0u;
            std::vector<syntax_issue> issues;
        };
        std::map<std::string, py_fragment_syntax> iPySyntax; ///< (the fragments' syntax errors: by their (preparsed) sources)
        std::optional<syntax_issue> iPyReporting;           ///< (the syntax error being reported (thrown))
        // the modules of source files that the program imports (each is a fragment of the program: its names are the 
        // program's variables _MN_name (N: its number); see closures.cpp's rewrite_module_names)
        struct py_module
        {
            std::string name;                               ///< (e.g. "a.b")
            std::string path;                               ///< (its source file)
            bool package = false;                           ///< (a package's __init__.py)
            std::string source;                             ///< (its source: once preparsed, the preparsed source)
            bool preparsed = false;
            std::vector<std::string> names;                 ///< (its names, by the indexes of its getter)
            std::optional<std::vector<std::string>> all;    ///< (its __all__)
            std::string prefix() const;                     ///< (_MN_)
            std::string init() const;                       ///< (the function of its top-level statements: _NN_init)
            std::string getter() const;                     ///< (_NN_getattr)
            std::size_t number = 0u;
        };
        std::vector<py_module> iPyModules;
        std::string iPyMode;                                ///< (the compilation's: "exec" or "eval" (source that a program compiles while it 
                                                            ///< runs: its names are those of its namespaces), or empty (a program))
        std::vector<std::string> iPyExecDefs;
        bool iPyCompilesAtRunTime = false;                  ///< (the program calls exec, eval or compile: see compiles_at_run_time)
        std::set<std::string> iPyIntrospective;             ///< (the modules that use their namespaces (globals(), ...))               ///< (exec: the functions that its top level defines)
        bool py_exec_mode() const { return !iPyMode.empty(); }
        std::optional<std::string> iPyScriptDirectory;     ///< (the directory of the program's source file (sys.path[0]))
        std::optional<std::size_t> py_module_of(std::string const& aName);    ///< (the module of a source file that a name is (found if not yet))
        std::optional<std::size_t> py_current_module() const;
        std::string py_module_name() const;                 ///< (the module being compiled: __main__ or its name)
        std::string py_absolute_module(std::string const& aName, std::size_t aLevel) const; ///< (a relative import's module)
        void py_find_imports(std::string const& aText);     ///< (the modules of source files that a module's import statements import)
        std::string py_global_name(std::string const& aName) const; ///< (a name of the module being compiled as the program's variable)
        fragment py_module_namespace();                     ///< (globals())
        fragment py_locals();                               ///< (locals() in a function)
        fragment iResult;
    };
}
