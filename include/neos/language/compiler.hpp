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
#include <neos/language/semantic_concept.hpp>
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
        struct semantic_concept_stack_entry
        {
            translation_unit* unit;
            i_source_fragment const* fragment;
            std::uint32_t level;
            const i_semantic_concept* semanticConcept;
            source_iterator sourceStart;
            source_iterator sourceEnd;
            neolib::ref_ptr<i_semantic_concept> foldedConcept;

            semantic_concept_stack_entry(
                translation_unit* unit,
                i_source_fragment const* fragment,
                std::uint32_t level,
                const i_semantic_concept* semanticConcept,
                source_iterator sourceStart,
                source_iterator sourceEnd);
            bool can_fold() const;
            bool can_fold(const semantic_concept_stack_entry& rhs) const;
            void instantiate_if_needed(i_context& aContext);
            void fold(i_context& aContext);
            void fold(i_context& aContext, semantic_concept_stack_entry& rhs);
            std::string trace() const;
        };
        typedef std::vector<semantic_concept_stack_entry> semantic_concept_stack_t;
        class scoped_semantic_concept_folder
        {
        public:
            scoped_semantic_concept_folder(compiler& aCompiler);
            scoped_semantic_concept_folder(compiler& aCompiler, semantic_concept_stack_t& aStack);
            ~scoped_semantic_concept_folder();
        public:
            void fold();
            void move_to(semantic_concept_stack_t& aOtherStack);
        private:
            semantic_concept_stack_t& stack();
        private:
            compiler& iCompiler;
            semantic_concept_stack_t& iStack;
            semantic_concept_stack_t::size_type iScopeStart;
        };
        struct compilation_state
        {
            program* program;
            translation_unit* unit;
            semantic_concept_stack_t iParseStack;
            semantic_concept_stack_t iPostfixOperationStack;
            semantic_concept_stack_t iFoldStack;
            std::uint32_t iLevel;
        };
        typedef std::vector<std::unique_ptr<compilation_state>> compilation_state_stack_t;
    public:
        compiler(i_context& aContext);
    public:
        void compile(program& aProgram);
        void compile(program& aProgram, translation_unit& aUnit);
        void compile(program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment);
        void compile(const i_source_fragment& aFragment) override;
        std::uint32_t trace() const;
        const std::optional<std::string>& trace_filter() const;
        void set_trace(std::uint32_t aTrace, const std::optional<std::string>& aFilter = {});
        const std::chrono::steady_clock::time_point& start_time() const;    
        const std::chrono::steady_clock::time_point& end_time() const;
    private:
        const compilation_state& state() const;
        compilation_state& state();
        bool fold();
        bool fold1();
        bool fold2();
        semantic_concept_stack_t& parse_stack();
        semantic_concept_stack_t& postfix_operation_stack();
        semantic_concept_stack_t& fold_stack();
        void display_probe_trace(translation_unit& aUnit, const i_source_fragment& aFragment);
        static std::string location(const translation_unit& aUnit, const i_source_fragment& aFragment, source_iterator aSourcePos, bool aShowFragmentFilePath = true);
        static void throw_error(const translation_unit& aUnit, const i_source_fragment& aFragment, source_iterator aSourcePos, const std::string& aError);
    private:
        i_context& iContext;
        std::uint32_t iTrace;
        std::optional<std::string> iTraceFilter;
        std::chrono::steady_clock::time_point iStartTime;
        std::chrono::steady_clock::time_point iEndTime;
        compilation_state_stack_t iCompilationStateStack;
    };
}