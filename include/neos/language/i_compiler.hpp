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
#include <neos/language/operator.hpp>
#include <neos/language/scope.hpp>

using namespace neolib::string_literals;

namespace neos::language
{
    using i_source_path_t = neolib::i_string;
    using i_optional_source_path_t = neolib::i_optional<i_source_path_t>;
    using i_source_t = neolib::i_string_view;

    using source_path_t = neolib::string;
    using optional_source_path_t = neolib::optional<source_path_t>;
    using source_t = neolib::string_view;
    using const_source_iterator = source_t::const_iterator;
    using source_iterator = source_t::iterator;

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
        virtual i_scope& enter_scope(scope_type aScopeType, neolib::i_string const& aScopeName) = 0;
        virtual void leave_scope(scope_type aScopeType) = 0;
        virtual language::i_data_type const& lhs_operand() const = 0;
        virtual language::i_data_type const& rhs_operand() const = 0;
        virtual void push_operand(language::i_data_type const& aOperand) = 0;
        virtual void pop_operand(language::i_data_type& aOperand) = 0;
        virtual void push_operator(language::i_operator_type const& aOperator) = 0;
        virtual void pop_operator(language::i_operator_type& aOperator) = 0;
        virtual void find_identifier(neolib::i_string_view const& aIdentifier, neolib::i_optional<language::i_data_type>& aResult) const = 0;
    public:
        virtual void throw_error(source_iterator aSourcePos, neolib::i_string const& aError, neolib::i_string const& aErrorType = "error"_s) = 0;
        virtual std::uint32_t trace() const = 0;
        // helpers
    public:
        void enter_namespace(neolib::i_string const& aNamespace)
        {
            enter_scope(scope_type::Namespace, aNamespace);
        }
        void leave_namespace()
        {
            leave_scope(scope_type::Namespace);
        }
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
        template <typename OperatorT>
        void push_operator(neolib::ref_ptr<OperatorT> const& aOperator)
        {
            push_operator(language::operator_type{ aOperator });
        }
        language::operator_type pop_operator()
        {
            language::operator_type op;
            pop_operator(op);
            return op;
        }
        neolib::optional<language::data_type> find_identifier(neolib::i_string_view const& aIdentifier) const
        {
            neolib::optional<language::data_type> result;
            find_identifier(aIdentifier, result);
            return result;
        }
    };
}