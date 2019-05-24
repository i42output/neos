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
            const i_atom* atom;
            parse_result with(source_iterator aSourceParsed, action_e aAction = Consumed) const
            {
                return parse_result{ aSourceParsed, aAction, atom };
            }
            parse_result with(action_e aAction) const
            {
                return parse_result{ sourceParsed, aAction, atom };
            }
            parse_result with(const i_atom& aAtom) const
            {
                return parse_result{ sourceParsed, action, &aAtom };
            }
            parse_result with_if(const i_atom& aAtom) const
            {
                return parse_result{ sourceParsed, action, atom == nullptr ? &aAtom : atom};
            }
            parse_result with_if(const i_atom* aAtom) const
            {
                return parse_result{ sourceParsed, action, atom == nullptr ? aAtom : atom };
            }
        };
        struct expected
        {
            const i_atom* what;
            const i_atom* context;
            bool consumed;
        };
        struct concept_stack_entry
        {
            translation_unit const* unit;
            uint32_t level;
            const i_concept* concept;
            source_iterator sourceStart;
            source_iterator sourceEnd;
        };
        typedef std::vector<concept_stack_entry> concept_stack_t;
        class scoped_concept_stack
        {
        public:
            scoped_concept_stack(compiler& aCompiler, compiler_pass aPass);
            scoped_concept_stack(compiler& aCompiler, compiler_pass aPass, concept_stack_t& aStack);
            ~scoped_concept_stack();
        public:
            void move_to_fold_stack();
            void move_to(concept_stack_t& aOtherStack);
        private:
            concept_stack_t& stack();
            void push(const concept_stack_entry& aEntry);
        private:
            compiler& iCompiler;
            compiler_pass iPass;
            concept_stack_t& iStack;
            concept_stack_t::size_type iScopeStart;
        };
    public:
        compiler();
    public:
        void compile(program& aProgram);
        bool trace() const;
        void set_trace(bool aTrace);
        bool trace_emits() const;
        void set_trace_emits(bool aTraceConcepts);
        const std::chrono::steady_clock::time_point& start_time() const;    
        const std::chrono::steady_clock::time_point& end_time() const;
    private:
        parse_result parse(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, source_iterator aSource);
        parse_result parse_tokens(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, const expected& aExpected, source_iterator aSource);
        parse_result parse_token_match(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, const i_atom& aMatchResult, const parse_result& aResult, bool aConsumeMatchResult = true, bool aSelf = false);
        parse_result parse_token(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_schema_node_atom& aAtom, const i_atom& aToken, const parse_result& aResult);
        parse_result consume_token(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_atom& aToken, const parse_result& aResult);
        parse_result consume_concept_token(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_concept& aConcept, const parse_result& aResult);
        parse_result consume_concept_atom(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_atom& aAtom, const i_concept& aConcept, const parse_result& aResult);
        parse_result consume_concept_atom(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const i_atom& aAtom, const i_concept& aConcept, const parse_result& aResult, concept_stack_t& aConceptStack);
        void fold_concepts();
        concept_stack_t& parse_stack();
        concept_stack_t& postfix_operation_stack();
        concept_stack_t& fold_stack();
        static bool is_finished(const compiler::parse_result& aResult);
        static bool finished(compiler::parse_result& aResult, bool aConsumeErrors = false);
        static std::string location(const translation_unit& aUnit, source_iterator aSourcePos);
        static void throw_error(const translation_unit& aUnit, source_iterator aSourcePos, const std::string& aError);
    private:
        bool iTrace;
        bool iTraceEmits;
        optional_source_iterator iDeepestProbe;
        concept_stack_t iParseStack;
        concept_stack_t iPostfixOperationStack;
        concept_stack_t iFoldStack;
        uint32_t iLevel;
        std::chrono::steady_clock::time_point iStartTime;
        std::chrono::steady_clock::time_point iEndTime;
    };
}