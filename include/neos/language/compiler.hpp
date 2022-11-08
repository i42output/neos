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
#include <neolib/core/optional.hpp>
#include <neolib/core/string.hpp>
#include <neos/fwd.hpp>
#include <neos/language/schema.hpp>
#include <neos/language/symbols.hpp>
#include <neos/language/ast.hpp>
#include <neos/language/concept.hpp>
#include <neos/language/i_concept_library.hpp>
#include <neos/language/i_compiler.hpp>

namespace neos::language
{
    typedef neolib::string source_file_path_t;
    typedef neolib::optional<source_file_path_t> optional_source_file_path_t;
    typedef neolib::string source_t;
    typedef source_t::const_iterator const_source_iterator;
    typedef source_t::iterator source_iterator;


    class source_fragment : public i_source_fragment
    {
    public:
        source_fragment(const optional_source_file_path_t& aSourceFilePath = optional_source_file_path_t{}, const source_t& aSource = source_t{}) :
            iSourceFilePath{ aSourceFilePath }, iSource{ aSource }, iImported{ false }, iStatus{ compilation_status::Pending }
        {
        }
        source_fragment(const i_source_fragment& aFragment) :
            iSourceFilePath{ aFragment.source_file_path() }, iSource{ aFragment.source() }, iImported{ aFragment.imported() }, iStatus{ aFragment.status() }
        {
        }
        source_fragment(i_source_fragment&& aFragment) :
            iSourceFilePath{ std::move(aFragment.source_file_path()) }, iSource{ std::move(aFragment.source()) }, iImported{ aFragment.imported() }, iStatus{ aFragment.status() }
        {
        }
    public:
        const i_optional_source_file_path_t& source_file_path() const override 
        { 
            return iSourceFilePath;
        }
        i_optional_source_file_path_t& source_file_path() override 
        { 
            return iSourceFilePath;
        }
        const i_source_t& source() const override 
        { 
            return iSource; 
        }
        i_source_t& source() override 
        { 
            return iSource; 
        }
        bool imported() const override
        {
            return iImported;
        }
        void set_imported() override
        {
            iImported = true;
        }
        compilation_status status() const override 
        { 
            return iStatus; 
        }
        void set_status(compilation_status aStatus) override
        {
            iStatus = aStatus;
        }
    public:
        const_source_iterator cbegin() const override
        { 
            return source().cbegin(); 
        }
        const_source_iterator cend() const override
        { 
            return source().cend(); 
        }
        const_source_iterator begin() const override
        { 
            return source().begin();
        }
        const_source_iterator end() const override
        { 
            return source().end();
        }
        source_iterator begin() override
        { 
            return source().begin();
        }
        source_iterator end() override
        { 
            return source().end();
        }
    private:
        optional_source_file_path_t iSourceFilePath;
        source_t iSource;
        bool iImported;
        mutable compilation_status iStatus;
    };

    typedef std::shared_ptr<language::schema> schema_pointer_t;
    typedef std::list<source_fragment> source_fragments_t;

    struct source_fragment_not_found : std::logic_error { source_fragment_not_found() : std::logic_error("neos::language::source_fragment_not_found") {} };

    struct translation_unit
    {
        schema_pointer_t schema;
        source_fragments_t fragments;
        language::ast ast;
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
        symbol_table_t symbolTable;
        text_t text;
    };

    class compiler : public i_compiler
    {
    private:
        typedef neos::language::const_source_iterator source_iterator;
        typedef std::optional<source_iterator> optional_source_iterator;
        struct parse_result
        {
            source_iterator sourceStart;
            source_iterator sourceParsed;
            enum action_e
            {
                Consumed,
                Drain,
                Done,
                ForNext,
                Continue,
                Recurse,
                Ignored,
                NoMatch,
                Error
            } action;
            const i_atom* atom;
            parse_result(source_iterator aSourceParsed, action_e aAction = Consumed, const i_atom* aAtom = nullptr) :
                sourceStart{ aSourceParsed }, sourceParsed{ aSourceParsed }, action{ aAction }, atom{ aAtom }
            {
            }
            parse_result(source_iterator aSourceStart, source_iterator aSourceParsed, action_e aAction = Consumed, const i_atom* aAtom = nullptr) :
                sourceStart{ aSourceStart }, sourceParsed{ aSourceParsed }, action{ aAction }, atom{ aAtom }
            {
            }
            parse_result with(source_iterator aSourceParsed, action_e aAction = Consumed) const
            {
                return parse_result{ sourceStart, aSourceParsed, aAction, atom };
            }
            parse_result with(action_e aAction) const
            {
                return parse_result{ sourceStart, sourceParsed, aAction, atom };
            }
            parse_result with(const i_atom& aAtom) const
            {
                return parse_result{ sourceStart, sourceParsed, action, &aAtom };
            }
            parse_result with_if(const i_atom& aAtom) const
            {
                return parse_result{ sourceStart, sourceParsed, action, atom == nullptr ? &aAtom : atom};
            }
            parse_result with_if(const i_atom* aAtom) const
            {
                return parse_result{ sourceStart, sourceParsed, action, atom == nullptr ? aAtom : atom };
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
            translation_unit* unit;
            i_source_fragment const* fragment;
            uint32_t level;
            const i_concept* concept_;
            source_iterator sourceStart;
            source_iterator sourceEnd;
            neolib::ref_ptr<i_concept> foldedConcept;
            concept_stack_entry(
                translation_unit* unit,
                i_source_fragment const* fragment,
                uint32_t level,
                const i_concept* concept_,
                source_iterator sourceStart,
                source_iterator sourceEnd
            ) :
                unit{ unit },
                fragment{ fragment },
                level{ level },
                concept_{ concept_ },
                sourceStart{ sourceStart },
                sourceEnd{ sourceEnd }
            {
            }
            bool can_fold() const
            {
                return foldedConcept ? foldedConcept->can_fold() : concept_->can_fold();
            }
            bool can_fold(const concept_stack_entry& rhs) const
            {
                return foldedConcept ?
                    foldedConcept->can_fold(rhs.foldedConcept ? *rhs.foldedConcept : *rhs.concept_) :
                    concept_->can_fold(rhs.foldedConcept ? *rhs.foldedConcept : *rhs.concept_);
            }
            void instantiate_if_needed()
            {
                if (!foldedConcept)
                    foldedConcept = concept_->instantiate(sourceStart, sourceEnd);
            }
            void fold(i_context& aContext)
            {
                instantiate_if_needed();
                foldedConcept = foldedConcept->fold(aContext);
            }
            void fold(i_context& aContext, concept_stack_entry& rhs)
            {
                if (rhs.can_fold())
                    rhs.fold(aContext);
                instantiate_if_needed();
                if (rhs.foldedConcept)
                    foldedConcept = foldedConcept->fold(aContext, *rhs.foldedConcept);
                else
                    foldedConcept = foldedConcept->fold(aContext, concept_instance_proxy{ *rhs.concept_, rhs.sourceStart, rhs.sourceEnd });
                if (fragment == rhs.fragment)
                {
                    sourceStart = std::min(sourceStart, rhs.sourceStart);
                    sourceEnd = std::max(sourceEnd, rhs.sourceEnd);
                    foldedConcept->update_source(sourceStart, sourceEnd);
                }
            }
            std::string trace() const
            {
                return foldedConcept ? foldedConcept->trace() : concept_->trace();
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
            template <typename Char, typename CharT>
            void trace(const translation_unit& aUnit, bool aTraceOperators, const std::optional<std::string>& aFilter, std::basic_ostream<Char, CharT>& aOutput) const
            {
                std::ostringstream result;
                result << "'";
                auto const count = std::min<std::ptrdiff_t>(20, std::distance(source, aUnit.fragment(source).end()));
                for (auto c : std::string_view(&*source, count))
                    if (c >= 32)
                        result << c;
                    else
                        result << ' ';
                result << "' " << std::string(20 - count, ' ');
                result << "[" << atom->symbol() << "]    ";
                if (aTraceOperators)
                    for (auto const& o : operations)
                        result << " (" << o << ")";
                result << std::endl;
                if (!aFilter || result.str().find(*aFilter) != std::string::npos)
                    aOutput << result.str();
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
            program* program;
            translation_unit* unit;
            std::optional<deepest_probe> iDeepestProbe;
            stack_trace_t iStackTrace;
            concept_stack_t iParseStack;
            concept_stack_t iPostfixOperationStack;
            concept_stack_t iFoldStack;
            uint32_t iLevel;
        };
        typedef std::vector<std::unique_ptr<compilation_state>> compilation_state_stack_t;
    public:
        compiler(i_context& aContext);
    public:
        void compile(program& aProgram);
        void compile(program& aProgram, translation_unit& aUnit);
        void compile(program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment);
        void compile(const i_source_fragment& aFragment) override;
        uint32_t trace() const;
        const std::optional<std::string>& trace_filter() const;
        void set_trace(uint32_t aTrace, const std::optional<std::string>& aFilter = {});
        const std::chrono::steady_clock::time_point& start_time() const;    
        const std::chrono::steady_clock::time_point& end_time() const;
    private:
        const compilation_state& state() const;
        compilation_state& state();
        parse_result parse(compiler_pass aPass, program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment, const i_schema_node_atom& aAtom, source_iterator aSourceStart, source_iterator aSourceParsed);
        parse_result parse_tokens(compiler_pass aPass, program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment, const i_schema_node_atom& aAtom, const expected& aExpected, source_iterator aSourceStart, source_iterator aSourceParsed);
        parse_result parse_token_match(compiler_pass aPass, program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment, const i_schema_node_atom& aAtom, const i_atom& aMatchResult, const parse_result& aResult, bool aConsumeMatchResult = true, bool aSelf = false);
        parse_result parse_token(compiler_pass aPass, program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment, const i_schema_node_atom& aAtom, const i_atom& aToken, const parse_result& aResult);
        parse_result consume_token(compiler_pass aPass, program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment, const i_atom& aToken, const parse_result& aResult);
        parse_result consume_concept_token(compiler_pass aPass, program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment, const i_concept& aConcept, const parse_result& aResult);
        parse_result consume_concept_atom(compiler_pass aPass, program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment, const i_atom& aAtom, const i_concept& aConcept, const parse_result& aResult);
        parse_result consume_concept_atom(compiler_pass aPass, program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment, const i_atom& aAtom, const i_concept& aConcept, const parse_result& aResult, concept_stack_t& aConceptStack);
        bool fold();
        bool fold1();
        bool fold2();
        concept_stack_t& parse_stack();
        concept_stack_t& postfix_operation_stack();
        concept_stack_t& fold_stack();
        void display_probe_trace(translation_unit& aUnit, const i_source_fragment& aFragment);
        static bool is_finished(const compiler::parse_result& aResult);
        static bool finished(compiler::parse_result& aResult, bool aConsumeErrors = false);
        static std::string location(const translation_unit& aUnit, const i_source_fragment& aFragment, source_iterator aSourcePos, bool aShowFragmentFilePath = true);
        static void throw_error(const translation_unit& aUnit, const i_source_fragment& aFragment, source_iterator aSourcePos, const std::string& aError);
    private:
        i_context& iContext;
        uint32_t iTrace;
        std::optional<std::string> iTraceFilter;
        std::chrono::steady_clock::time_point iStartTime;
        std::chrono::steady_clock::time_point iEndTime;
        compilation_state_stack_t iCompilationStateStack;
    };
}