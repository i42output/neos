/*
  compiler.hpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

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

#include <deque>
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
#include <neos/language/semantic_concept.hpp>
#include <neos/language/i_concept_library.hpp>
#include <neos/language/i_compiler.hpp>
#include <neos/language/i_code_generator.hpp>
#include <neos/language/i_language_compiler.hpp>
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

    // (a program's semantic concepts, in the order they are folded: most are folded (and erased) near its start, so it is a deque)
    using fold_stack = std::deque<neolib::ref_ptr<i_ast_node>>;

    class compiler : public i_code_generator
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
                std::string loopVariableName;
                std::vector<language::fragment> range;
                std::optional<language::fragment> epilogue;   ///< (a while loop) code that follows the loop
                // (try: the handlers' exception types (conditions; none: any) and the names they bind, and finally's body)
                std::vector<std::optional<language::fragment>> handlerTypes;
                std::vector<std::optional<std::string>> handlerNames;
                std::optional<language::fragment> finallyArm;
                bool tryElse = false;   ///< (else follows the handlers)
                // (the lines of its header and of its conditions (if and elif: see bytecode::LineMarker))
                std::uint32_t line = 0u;
                std::vector<std::uint32_t> conditionLines;
            };
            std::vector<indented> indentStack;
            bool implicitFunctionFinished = false;
            i_language_compiler* language = nullptr;    ///< the language compiler of the unit's language (if it has one)
            bool pendingInferred = false;               ///< the function being declared has inferred types
            std::optional<std::pair<std::uint32_t, neolib::ref_ptr<i_scope>>> suspendedEntry; ///< the implicit entry point while a function is compiled
        };
        struct symbol_info
        {
            char const* position = nullptr; ///< definition position (nullptr if defined at function level)
            std::uint64_t sequence = 0u;
            bool retired = false;
            bool global = false;                        ///< a global variable (index is that of the WASM global)
            bool constant = false;
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
            // (its frame (see bytecode::FramesSection): its name (none: it has no frame), its source's name and its first line)
            std::optional<std::string> frameName;
            std::string frameSource;
            std::uint32_t firstLine = 0u;
        };
        struct wasm_module
        {
            std::vector<wasm_function> functions;
            std::vector<std::uint8_t> data;
            std::map<std::string, std::uint32_t> strings;
            std::vector<std::pair<std::string, std::vector<type>>> deferredCalls; ///< calls to functions defined later (resolved at emission)
            std::vector<std::pair<std::vector<type>, type>> indirectSignatures;  ///< the signatures of call_indirect's functions (their types: at emission)
        };
        struct interval
        {
            std::int64_t low;
            std::int64_t high;
            bool contains(interval const& aOther) const { return aOther.low >= low && aOther.high <= high; }
            interval join(interval const& aOther) const { return { std::min(low, aOther.low), std::max(high, aOther.high) }; }
            bool operator==(interval const&) const = default;
        };
        // types inferred from the code (each compilation pass uses the types found by the previous one; passes are 
        // repeated until they no longer change)
        struct inference
        {
            std::map<std::string, std::vector<type>> parameters;    ///< function -> types of its arguments
            std::map<std::string, type> results;                    ///< function -> type of the values it returns
            std::vector<std::pair<std::string, type>> globals;     ///< global variables (in order of declaration)
            std::map<std::string, type> returned;                   ///< (this pass)
            std::set<std::string> functionGlobals;                  ///< (this pass) global variables used by functions
            std::set<std::string> entryGlobals;                     ///< global variables only used by top-level code (locals of the entry point)
            std::uint32_t pass = 0u;
            bool changed = false;
        };
    private:
        using compilation_state_stack_t = std::vector<std::unique_ptr<compilation_state>>;
    public:
        compiler(i_context& aContext);
    public:
        bool compile(program& aProgram);
        void set_language_options(std::string const& aLanguage, std::string const& aOptions);
        bool compiles_at_run_time(std::string const& aLanguage) const;    ///< (the program compiled last, in the language: see i_language_compiler)
        bool parsable_source(std::string const& aLanguage, std::string_view aSource, std::string& aResult, std::vector<std::uint32_t>& aPositions) const; ///< (see 
                                                                        ///< i_language_compiler; false: the source as written)
        void set_quiet(bool aQuiet) { iQuiet = aQuiet; }    ///< (syntax errors are not reported on the standard error stream: only thrown)
        std::set<std::string> const& source_files() const { return iSourceFiles; } ///< (the files of the source fragments that the program 
                                                                        ///< compiled last is compiled from (e.g. its packages): see neos --cache)
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
        compiler_error compile_error(source_iterator aSourcePos, neolib::i_string const& aError, neolib::i_string const& aErrorType);
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
        bool trap_on_overflow() const;                              ///< (integer arithmetic whose result overflows its (fixed size) type 
        void set_trap_on_overflow(bool aTrapOnOverflow);            ///< traps; off by default: it wraps)
                                                                    ///< (bigint_promotion: a language's integers are of any size: a result that overflows 
        std::optional<bool> bigint_promotion_override() const;      ///< is promoted to an arbitrary precision integer (e.g. Python's int); 
        void set_bigint_promotion_override(std::optional<bool> aBigintPromotion); ///< the schema's bigint.promotion, unless overridden)
        void add_language_compiler(i_language_compiler& aLanguageCompiler); ///< (a compiler plugin's: the code generation of its language)
        // i_code_generator
    public:
        bool declaration_pass() const final;
        std::uint32_t inference_pass() const final;
        void set_changed() final;
        void trace_output(neolib::i_string const& aText) final;
        bool bigint_promotion() const final;
        bool overflow_traps() const final;
        std::size_t operand_count() const final;
        std::size_t operand_base() const final;
        i_operand_type const& operand(std::size_t aIndex) const final;
        void set_operand(std::size_t aIndex, i_operand_type const& aOperand) final;
        void insert_operand(std::size_t aIndex, i_operand_type const& aOperand) final;
        void erase_operands(std::size_t aIndex, std::size_t aCount = 1u) final;
        i_fragment const& materialize_operand(i_operand_type const& aOperand) final;
        i_fragment const& materialize_operand(i_operand_type const& aOperand, type aTargetType) final;
        i_fragment const& coerce_value(i_fragment const& aValue, type aTarget) final;
        void pop_fragments_into(neolib::i_string_view const& aSource, fragment_kind aKind, neolib::i_vector<i_fragment>& aFragments) final;
        std::uint32_t temporary(type aType) final;
        std::uint32_t string_literal(neolib::i_string const& aValue) final;
        std::uint32_t runtime_function(neolib::i_string const& aName, neolib::i_vector<type> const& aParameters, type aReturnType) final;
        std::uint32_t indirect_signature(neolib::i_vector<type> const& aParameters, type aReturnType) final;
        bool runtime_plugin_available(neolib::i_string const& aNamespace) const final;
        void import_fragment(neolib::i_string const& aSourceFilePath, neolib::i_string const& aSource) final;
        bool in_function() const final;
        std::uint32_t current_function_index() const final;
        void ensure_function() final;
        void suspend_entry() final;
        void set_pending_inferred() final;
        neolib::i_string const& function_name(std::uint32_t aFunction) const final;
        i_function_signature const& signature_of(std::uint32_t aFunction) const final;
        bool function_inferred(std::uint32_t aFunction) const final;
        bool function_implicit_entry(std::uint32_t aFunction) const final;
        std::size_t forward_declaration_count() const final;
        neolib::i_string_view const& forward_declaration_name(std::size_t aIndex) const final;
        i_function_signature const& forward_declaration_signature(std::size_t aIndex) const final;
        bool forward_declaration_stub(std::size_t aIndex) const final;
        bool forward_declaration_inferred(std::size_t aIndex) const final;
        std::size_t function_count() const final;
        bool function_imported(std::uint32_t aFunction) const final;
        bool function_stub(std::uint32_t aFunction) const final;
        bool function_intrinsic(std::uint32_t aFunction) const final;
        bool function_entry(std::uint32_t aFunction) const final;
        i_fragment const& function_body(std::uint32_t aFunction) const final;
        void set_function_body(std::uint32_t aFunction, i_fragment const& aBody) final;
        std::uint32_t deferred_call(neolib::i_string const& aName, neolib::i_vector<type> const& aParameters) final;
        void set_function_locals(std::uint32_t aFunction, neolib::i_vector<type> const& aLocals) final;
        std::uint32_t define_function(neolib::i_string const& aName, i_function_signature const& aSignature, neolib::i_vector<type> const& aLocals,
            neolib::i_string const& aFrameName, std::uint32_t aFirstLine) final;
        type inferred_parameter(neolib::i_string_view const& aFunction, std::uint32_t aIndex) const final;
        void set_inferred_parameter(neolib::i_string_view const& aFunction, std::uint32_t aIndex, std::uint32_t aCount, type aType) final;
        void infer(neolib::i_string const& aWhat, type aOld, type aNew) final;
        i_scope const& global_scope() const final;
        i_symbol_table_entry* find_symbol(i_scope const& aScope, neolib::i_string_view const& aName, bool aThisScopeOnly = false) const final;
        i_symbol_table_entry& define_global(neolib::i_string const& aName, type aType) final;
        void declare_globals() final;
        bool is_global(i_symbol_table_entry const& aSymbol) const final;
        bool is_constant(i_symbol_table_entry const& aSymbol) const final;
        void set_constant(i_symbol_table_entry const& aSymbol, bool aConstant) final;
        bool is_block_scoped(i_symbol_table_entry const& aSymbol) const final;
        void set_block_scoped(i_symbol_table_entry const& aSymbol, bool aBlockScoped) final;
        void retire_symbols(neolib::i_string_view const& aSource) final;
        std::size_t indented_count() const final;
        indented_construct indented_kind(std::size_t aIndex) const final;
        bool indented_try_else(std::size_t aIndex) const final;
        void set_loop_epilogue(i_fragment const& aEpilogue) final;
        void finalize_suspended(std::size_t aIndent) final;
        std::size_t global_count() const final;
        neolib::i_string const& global_name(std::size_t aIndex) const final;
        type global_type(std::size_t aIndex) const final;
        void add_global(neolib::i_string const& aName, type aType) final;
        void set_global_type(std::size_t aIndex, type aType) final;
        bool entry_global(neolib::i_string_view const& aName) const final;
        void function_global(neolib::i_string const& aName) final;
    private:
        const compilation_state& state() const;
        compilation_state& state();
        fold_stack& fold_stack();
        wasm_function& current_function();
        std::uint32_t add_function(std::string const& aName, bool aImported);
        std::uint32_t runtime_function(std::string const& aName, std::vector<type> const& aParameters, type aReturnType);
        std::uint32_t string_literal(std::string const& aValue);
        void finish_function();
        fragment materialize(operand_type const& aOperand, std::optional<type> aTargetType = {});
        i_language_compiler* language() const;                  ///< (that of the fragment being compiled)
        i_language_compiler* language(std::string const& aLanguage) const;
        fragment pop_value();
        std::vector<fragment> pop_fragments(neolib::i_string_view const& aSource, fragment_kind aKind);
        void emit_module(program& aProgram);
        void check_rebinding(i_symbol_table_entry const& aVariable, neolib::i_string_view const& aName, type aValueType) const;
        std::optional<std::size_t> line_indent(neolib::i_string_view const& aSource) const;
        void close_indented(std::size_t aIndent);
        void finalize_indented();
        fragment language_construct(compilation_state::indented const& aConstruct);
        fragment take_body(std::size_t aBodyBase);
        void wrap_expression_body(neolib::i_string_view const& aSource);
        i_symbol_table_entry& define_global(std::string const& aName, type aType);
        void infer(std::string const& aWhat, std::optional<type> const& aOld, type aNew);
        bool inline_call(fragment& aCall, wasm_function const& aCallee, std::size_t aParameterCount);
        bool strip_call(fragment& aFragment, std::string_view aVmFunction);
        std::uint32_t source_line(char const* aPosition);       ///< (the line of a position in the fragment being compiled: 0 if it is not in it)
        std::string source_name() const;                        ///< (the name of the fragment being compiled, for frames)
        void mark_line(fragment& aStatement, neolib::i_string_view const& aSource); ///< (see bytecode::LineMarker)
        void mark_line(fragment& aStatement, std::uint32_t aLine);
        void put_line(fragment& aCode, std::uint32_t aLine);    ///< (a line marker at the end of the code)
        void resume_entry();
        void coerce(fragment& aValue, type aTarget);
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
        bool iTrapOnOverflow = false;
        bool iQuiet = false;
        std::set<std::string> iSourceFiles;
        std::optional<bool> iBigintPromotion;
        compilation_state_stack_t iCompilationStateStack;
        wasm_module iModule;
        std::unordered_map<std::string, std::uint32_t> iRuntimeFunctions;  ///< (runtime_function's: names -> indices that they probably have)
        std::unordered_map<i_symbol_table_entry const*, symbol_info> iSymbolInfo;
        std::uint64_t iSymbolSequence = 0u;
        bool iDeclarationPass = false;
        std::vector<forward_declaration> iForwardDeclarations;
        std::vector<forward_declaration> iDeclaredFunctions;    ///< (as found by the declaration pass)
        inference iInference;
        bool iGlobalsDeclared = false;
        // (the sources that compilation passes parsed (a pass parses those of the previous one again: e.g. a prelude's, whose fragment is made 
        // again): the AST of each (its values refer to the text at base))
        struct parsed_source
        {
            void const* parser = nullptr;   ///< (the last stage's)
            std::string source;
            char const* base = nullptr;
            std::shared_ptr<parser::ast_node> ast;
        };
        std::deque<parsed_source> iParsed;
        std::vector<neolib::ref_ptr<i_language_compiler>> iLanguageCompilers;
        fragment iResult;                                       ///< (i_code_generator: the fragment returned)
        struct line_starts
        {
            char const* source = nullptr;
            std::size_t size = 0u;
            std::vector<std::size_t> starts;                    ///< (the positions where its lines begin)
        } iLineStarts;                                          ///< (of the fragment whose lines were counted last)
    };
}