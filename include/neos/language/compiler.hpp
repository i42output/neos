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
    typedef std::string source_file_path_t;
    typedef std::optional<source_file_path_t> optional_source_file_path_t;
    typedef std::string source_t;

    enum class compilation_status
    {
        Pending,
        Compiling,
        Compiled
    };

    struct source_fragment
    {
        optional_source_file_path_t filePath;
        source_t source;
        mutable compilation_status status;
        source_t::const_iterator begin() const { return source.begin(); }
        source_t::const_iterator end() const { return source.end(); }
        source_t::const_iterator cbegin() const { return source.begin(); }
        source_t::const_iterator cend() const { return source.end(); }
        source_t::iterator cbegin() { return source.begin(); }
        source_t::iterator cend() { return source.end(); }
    };

    typedef std::shared_ptr<language::schema> schema_pointer_t;
    typedef std::vector<source_fragment> source_fragments_t;

    struct source_fragment_not_found : std::logic_error { source_fragment_not_found() : std::logic_error("neos::language::source_fragment_not_found") {} };

    struct translation_unit
    {
        schema_pointer_t schema;
        source_fragments_t fragments;
        language::ast ast;
        const source_fragment& fragment(source_t::const_iterator aSource) const
        {
            for (auto const& f : fragments)
                if (aSource >= f.source.begin() && aSource <= f.source.end())
                    return f;
            throw source_fragment_not_found();
        }
        source_fragment& fragment(source_t::const_iterator aSource)
        {
            return const_cast<source_fragment&>(const_cast<const translation_unit*>(this)->fragment(aSource));
        }
        bool pending() const
        {
            for (auto const& f : fragments)
                if (f.status == compilation_status::Pending)
                    return true;
            return false;
        }
        bool compiling() const
        {
            for (auto const& f : fragments)
                if (f.status == compilation_status::Compiling)
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
        symbol_table_t symbolTable;
        text_t text;
    };

    class compiler
    {
    private:
        typedef source_t::const_iterator source_iterator;
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
            source_fragment const* fragment;
            uint32_t level;
            const i_concept* concept;
            source_iterator sourceStart;
            source_iterator sourceEnd;
            neolib::ref_ptr<i_concept> foldedConcept;
            bool can_fold() const
            {
                return foldedConcept ? foldedConcept->can_fold() : concept->can_fold();
            }
            bool can_fold(const concept_stack_entry& rhs) const
            {
                return foldedConcept ?
                    foldedConcept->can_fold(rhs.foldedConcept ? *rhs.foldedConcept : *rhs.concept) :
                    concept->can_fold(rhs.foldedConcept ? *rhs.foldedConcept : *rhs.concept);
            }
            void instantiate_if_needed()
            {
                if (!foldedConcept)
                    foldedConcept = concept->instantiate(sourceStart, sourceEnd);
            }
            void fold()
            {
                instantiate_if_needed();
                foldedConcept = foldedConcept->fold();
            }
            void fold(concept_stack_entry& rhs)
            {
                if (rhs.can_fold())
                    rhs.fold();
                instantiate_if_needed();
                if (rhs.foldedConcept)
                    foldedConcept = foldedConcept->fold(*rhs.foldedConcept);
                else
                    foldedConcept = foldedConcept->fold(*rhs.concept, rhs.sourceStart, rhs.sourceEnd);
            }
            std::string trace() const
            {
                return foldedConcept ? foldedConcept->trace() : concept->trace();
            }
        };
        typedef std::vector<concept_stack_entry> concept_stack_t;
        class scoped_concept_folder
        {
        public:
            scoped_concept_folder(compiler& aCompiler, compiler_pass aPass);
            scoped_concept_folder(compiler& aCompiler, compiler_pass aPass, concept_stack_t& aStack);
            ~scoped_concept_folder();
        public:
            void fold();
            void move_to(concept_stack_t& aOtherStack);
        private:
            concept_stack_t& stack();
        private:
            compiler& iCompiler;
            compiler_pass iPass;
            concept_stack_t& iStack;
            concept_stack_t::size_type iScopeStart;
        };
        struct stack_trace_entry
        {
            source_iterator source;
            const i_atom* atom;
            std::vector<std::string> operations;
            bool operator==(const stack_trace_entry& rhs) const 
            { 
                return atom == rhs.atom && operations == rhs.operations; 
            }
            void trace(const translation_unit& aUnit, bool aTraceOperators) const
            {
                std::cerr << "'";
                auto const count = std::min<std::ptrdiff_t>(20, std::distance(source, aUnit.fragment(source).end()));
                for (auto c : std::string_view(&*source, count))
                    if (c >= 32)
                        std::cerr << c;
                    else
                        std::cerr << ' ';
                std::cerr << "' " << std::string(20 - count, ' ');
                std::cerr << "[" << atom->symbol() << "]    ";
                if (aTraceOperators)
                    for (auto const& o : operations)
                        std::cerr << " (" << o << ")";
                std::cerr << std::endl;
            }
        };
        typedef std::deque<stack_trace_entry> stack_trace_t;
        class scoped_stack_trace
        {
        public:
            scoped_stack_trace(compiler& aParent, const i_atom& aAtom, source_iterator aSource, const std::string& aWhat)
                : iParent{aParent}
            {
                if (iParent.trace() >= 3)
                {
                    if (iParent.state().iStackTrace.empty() || iParent.state().iStackTrace.back().atom != &aAtom || iParent.state().iStackTrace.back().source != aSource)
                        iParent.state().iStackTrace.push_back(stack_trace_entry{ aSource, &aAtom, { aWhat } });
                    else
                        iParent.state().iStackTrace.back().operations.push_back(aWhat);
                }
            }
            ~scoped_stack_trace()
            {
                if (iParent.trace() >= 3)
                {
                    iParent.state().iStackTrace.back().operations.pop_back();
                    if (iParent.state().iStackTrace.back().operations.empty())
                        iParent.state().iStackTrace.pop_back();
                }
            }
        private:
            compiler& iParent;
        };
        struct deepest_probe
        {
            source_iterator source;
            std::vector<stack_trace_t> stacks;
        };
        struct compilation_state
        {
            std::optional<deepest_probe> iDeepestProbe;
            stack_trace_t iStackTrace;
            concept_stack_t iParseStack;
            concept_stack_t iPostfixOperationStack;
            concept_stack_t iFoldStack;
            uint32_t iLevel;
        };
    public:
        compiler();
    public:
        void compile(program& aProgram);
        void compile(program& aProgram, const translation_unit& aUnit);
        void compile(program& aProgram, const translation_unit& aUnit, const source_fragment& aFragment);
        uint32_t trace() const;
        void set_trace(uint32_t aTrace);
        const std::chrono::steady_clock::time_point& start_time() const;    
        const std::chrono::steady_clock::time_point& end_time() const;
    private:
        const compilation_state& state() const;
        compilation_state& state();
        parse_result parse(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const source_fragment& aFragment, const i_schema_node_atom& aAtom, source_iterator aSource);
        parse_result parse_tokens(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const source_fragment& aFragment, const i_schema_node_atom& aAtom, const expected& aExpected, source_iterator aSource);
        parse_result parse_token_match(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const source_fragment& aFragment, const i_schema_node_atom& aAtom, const i_atom& aMatchResult, const parse_result& aResult, bool aConsumeMatchResult = true, bool aSelf = false);
        parse_result parse_token(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const source_fragment& aFragment, const i_schema_node_atom& aAtom, const i_atom& aToken, const parse_result& aResult);
        parse_result consume_token(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const source_fragment& aFragment, const i_atom& aToken, const parse_result& aResult);
        parse_result consume_concept_token(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const source_fragment& aFragment, const i_concept& aConcept, const parse_result& aResult);
        parse_result consume_concept_atom(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const source_fragment& aFragment, const i_atom& aAtom, const i_concept& aConcept, const parse_result& aResult);
        parse_result consume_concept_atom(compiler_pass aPass, program& aProgram, const translation_unit& aUnit, const source_fragment& aFragment, const i_atom& aAtom, const i_concept& aConcept, const parse_result& aResult, concept_stack_t& aConceptStack);
        bool fold();
        bool fold1();
        bool fold2();
        concept_stack_t& parse_stack();
        concept_stack_t& postfix_operation_stack();
        concept_stack_t& fold_stack();
        void display_probe_trace(const translation_unit& aUnit, const source_fragment& aFragment);
        static bool is_finished(const compiler::parse_result& aResult);
        static bool finished(compiler::parse_result& aResult, bool aConsumeErrors = false);
        static std::string location(const translation_unit& aUnit, const source_fragment& aFragment, source_iterator aSourcePos, bool aShowFragmentFilePath = true);
        static void throw_error(const translation_unit& aUnit, const source_fragment& aFragment, source_iterator aSourcePos, const std::string& aError);
    private:
        uint32_t iTrace;
        std::chrono::steady_clock::time_point iStartTime;
        std::chrono::steady_clock::time_point iEndTime;
        std::vector<compilation_state> iCompilationStateStack;
    };
}