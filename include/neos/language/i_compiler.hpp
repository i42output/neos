/*
  i_compiler.hpp

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
#include <neolib/core/i_string.hpp>
#include <neolib/core/i_string_view.hpp>
#include <neolib/core/i_optional.hpp>
#include <neos/language/type.hpp>

namespace neos::language
{
    typedef neolib::i_string i_source_path_t;
    typedef neolib::i_optional<i_source_path_t> i_optional_source_path_t;
    typedef neolib::i_string_view i_source_t;

    enum class compilation_status
    {
        Pending,
        Compiling,
        Compiled,
        Error
    };

    class i_source_fragment
    {
    public:
        typedef i_source_t::const_iterator const_iterator;
        typedef i_source_t::iterator iterator;
    public:
        virtual ~i_source_fragment() {}
    public:
        virtual i_optional_source_path_t const& source_file_path() const = 0;
        virtual i_optional_source_path_t& source_file_path() = 0;
        virtual i_optional_source_path_t const& source_directory_path() const = 0;
        virtual i_source_t const& source() const = 0;
        virtual void set_source(i_source_t const& aSource) = 0;
        virtual bool imported() const = 0;
        virtual void set_imported() = 0;
        virtual compilation_status status() const = 0;
        virtual void set_status(compilation_status aStatus) = 0;
    public:
        virtual const_iterator cbegin() const = 0;
        virtual const_iterator cend() const = 0;
        virtual const_iterator begin() const = 0;
        virtual const_iterator end() const = 0;
        virtual iterator begin() = 0;
        virtual iterator end() = 0;
    };

    struct concept_not_found : std::runtime_error
    {
        concept_not_found(std::string_view const& aConceptName) : std::runtime_error{ std::string{ "Concept '" } + std::string{ aConceptName } + "' not found" } {}
    };

    class i_compiler
    {
    public:
        virtual ~i_compiler() {}
    public:
        virtual bool compile(i_source_fragment const& aFragment) = 0;
        virtual void enter_namespace(neolib::i_string const& aNamespace) = 0;
        virtual void leave_namespace() = 0;
        virtual void push_operand(language::i_data_type const& aOperand) = 0;
        virtual void pop_operand(language::i_data_type& aOperand) = 0;
    public:
        virtual std::uint32_t trace() const = 0;
        // helpers
    public:
        template <typename DataT>
        void push_operand(DataT const& aOperand, std::enable_if_t<!std::is_base_of_v<language::i_data_type, DataT>, neolib::sfinae> = {})
        {
            push_operand(language::data_type{ aOperand });
        }
        language::data_type pop_operand()
        {
            language::data_type operand;
            pop_operand(operand);
            return operand;
        }
    };
}