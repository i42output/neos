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
#include <neos/language/symbols.hpp>

namespace neos::language
{
    typedef neolib::string source_file_path_t;
    typedef neolib::optional<source_file_path_t> optional_source_file_path_t;
    typedef neolib::string_view source_t;
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
    public:
        const i_optional_source_file_path_t& source_file_path() const final
        { 
            return iSourceFilePath;
        }
        i_optional_source_file_path_t& source_file_path() final
        { 
            return iSourceFilePath;
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
        optional_source_file_path_t iSourceFilePath;
        neolib::string iSource;
        mutable source_t iSourceView;
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
        scope scope;
        symbol_table symbolTable;
        text text;
    };

    using fold_stack = std::vector<neolib::ref_ptr<i_semantic_concept>>;

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
            std::vector<neolib::weak_ref_ptr<i_scope>> namespaceScope;
        };
        using compilation_state_stack_t = std::vector<std::unique_ptr<compilation_state>>;
    public:
        compiler(i_context& aContext);
    public:
        bool compile(program& aProgram);
        bool compile(program& aProgram, translation_unit& aUnit);
        bool compile(program& aProgram, translation_unit& aUnit, i_source_fragment& aFragment);
        bool compile(const i_source_fragment& aFragment) final;
        void enter_namespace(neolib::i_string const& aNamespace) final;
        void leave_namespace() final;
        std::uint32_t trace() const;
        const std::optional<std::string>& trace_filter() const;
        void set_trace(std::uint32_t aTrace, const std::optional<std::string>& aFilter = {});
        const std::chrono::steady_clock::time_point& start_time() const;    
        const std::chrono::steady_clock::time_point& end_time() const;
    private:
        const compilation_state& state() const;
        compilation_state& state();
        fold_stack& fold_stack();
        bool fold();
        bool fold2();
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