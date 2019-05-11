/*
  compiler.hpp

  Copyright (c) 2019 Leigh Johnston.  All Rights Reserved.

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
#include <string>
#include <neos/language/schema.hpp>
#include <neos/language/symbols.hpp>
#include <neos/language/ast.hpp>
#include <neos/language/i_concept_library.hpp>

namespace neos::language
{
    struct translation_unit
    {
        std::shared_ptr<language::schema> schema;
        typedef std::string source_t;
        source_t source;
        language::ast ast;
    };

    struct program
    {
        symbol_table_t symbolTable;
        std::vector<translation_unit> translationUnits;
        text_t text;
    };

    class compiler
    {
    private:
        typedef translation_unit::source_t::const_iterator source_iterator;
        typedef std::optional<source_iterator> optional_source_iterator;
        struct parse_result
        {
            source_iterator sourceParsed;
            enum action_e
            {
                Consumed,
                Drain,
                Done,
                ForNext,
                Continue,
                Ignored,
                NoMatch,
                Error
            } action;
        };
        struct expected
        {
            const i_atom* what;
            const i_atom* context;
            bool consumed;
        };
        struct emit
        {
            uint32_t level;
            const i_concept* concept;
            source_iterator sourceStart;
            source_iterator sourceEnd;
        };
        typedef std::vector<emit> emit_stack_t;
        class emitter
        {
        public:
            emitter(compiler& aCompiler, compiler_pass aPass);
            emitter(compiler& aCompiler, compiler_pass aPass, emit_stack_t& aEmitStack);
            ~emitter();
        public:
            void emit();
            void move_to(emit_stack_t& aOtherStack);
        private:
            emit_stack_t & emit_stack();
            void emit(const compiler::emit& aEmit);
        private:
            compiler& iCompiler;
            compiler_pass iPass;
            emit_stack_t& iEmitStack;
            emit_stack_t::size_type iEmitFrom;
        };
    public:
        compiler();
    public:
        void compile(program& aProgram);
        bool trace() const;
        void set_trace(bool aTrace);
        bool trace_emits() const;
        void set_trace_emits(bool aTraceEmits);
        const std::chrono::steady_clock::time_point& start_time() const;    
        const std::chrono::steady_clock::time_point& end_time() const;
    private:
        parse_result parse(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, source_iterator aSource);
        parse_result parse_tokens(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, const expected& aExpected, source_iterator aSource);
        parse_result parse_token_match(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, const i_atom& aMatchResult, source_iterator aSource, bool aConsumeMatchResult = true, bool aSelf = false);
        parse_result parse_token(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, const i_atom& aToken, source_iterator aSource);
        parse_result consume_token(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_atom& aToken, const parse_result& aResult);
        parse_result consume_concept_token(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_concept& aConcept, const parse_result& aResult);
        parse_result consume_concept_atom(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_atom& aAtom, const i_concept& aConcept, const parse_result& aResult);
        parse_result consume_concept_atom(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_atom& aAtom, const i_concept& aConcept, const parse_result& aResult, emit_stack_t& aEmitStack);
        emit_stack_t& emit_stack();
        emit_stack_t& postfix_operation_stack();
        static bool is_finished(const compiler::parse_result& aResult, source_iterator aSource);
        static bool finished(compiler::parse_result& aResult, source_iterator aSource, bool aConsumeErrors = false);
        static void throw_error(const translation_unit& aUnit, source_iterator aSourcePos, const std::string& aError);
    private:
        bool iTrace;
        bool iTraceEmits;
        optional_source_iterator iDeepestProbe;
        emit_stack_t iEmitStack;
        emit_stack_t iPostfixOperationStack;
        uint32_t iLevel;
        std::chrono::steady_clock::time_point iStartTime;
        std::chrono::steady_clock::time_point iEndTime;
    };
}