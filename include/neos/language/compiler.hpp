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
            };
            std::vector<indented> indentStack;
            bool implicitFunctionFinished = false;
        };
        struct symbol_info
        {
            char const* position = nullptr; ///< definition position (nullptr if defined at function level)
            std::uint64_t sequence = 0u;
            bool retired = false;
        };
        struct forward_declaration
        {
            std::string name;
            function_signature signature;
            bool stub = false;
        };
        struct wasm_function
        {
            std::string name;
            bool imported = false;
            bool fromImportedFragment = false;
            bool entry = false;
            bool compiled = false;
            bool stub = false;
            i_function_scope const* scope = nullptr;
            function_signature signature;
            std::vector<type> locals;
            std::vector<std::uint8_t> body;
            std::vector<std::pair<std::uint32_t, std::uint32_t>> relocations;
            std::size_t operandBase = 0u;
        };
        struct wasm_module
        {
            std::vector<wasm_function> functions;
            std::vector<std::uint8_t> data;
            std::map<std::string, std::uint32_t> strings;
            std::vector<std::pair<std::string, std::vector<type>>> deferredCalls; ///< calls to functions defined later (resolved at emission)
        };
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
        void throw_error(source_iterator aSourcePos, neolib::i_string const& aError, neolib::i_string const& aErrorType = "error"_s) final;
        std::uint32_t trace() const final;
        const std::optional<std::string>& trace_filter() const;
        void set_trace(std::uint32_t aTrace, const std::optional<std::string>& aFilter = {});
        const std::chrono::steady_clock::time_point& start_time() const;    
        const std::chrono::steady_clock::time_point& end_time() const;
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
        bool fold();
        bool fold2();
        static std::string location(const translation_unit& aUnit, const i_source_fragment& aFragment, source_iterator aSourcePos, bool aShowFragmentFilePath = true);
    private:
        i_context& iContext;
        std::uint32_t iTrace;
        std::optional<std::string> iTraceFilter;
        std::chrono::steady_clock::time_point iStartTime;
        std::chrono::steady_clock::time_point iEndTime;
        compilation_state_stack_t iCompilationStateStack;
        wasm_module iModule;
        std::unordered_map<i_symbol_table_entry const*, symbol_info> iSymbolInfo;
        std::uint64_t iSymbolSequence = 0u;
        bool iDeclarationPass = false;
        std::vector<forward_declaration> iForwardDeclarations;
    };
}