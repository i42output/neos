/*
  function.hpp

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
#include <neolib/core/variant.hpp>
#include <neolib/core/vector.hpp>
#include <neolib/core/optional.hpp>
#include <neonumeric/integer.hpp>
#include <neonumeric/real.hpp>
#include <neos/language/symbols.hpp>

namespace neos
{
    namespace language
    {
        struct i_function_parameter
        {
            virtual neolib::i_string const& parameter_name() const = 0;
            virtual void set_parameter_name(neolib::i_string_view const& aParameterName) = 0;
            virtual neos::language::type parameter_type() const = 0;
            virtual void set_parameter_type(neos::language::type aParameterType) = 0;
        };

        struct function_parameter : i_function_parameter
        {
            using abstract_type = i_function_parameter;

            neolib::string parameterName;
            neos::language::type parameterType = neos::language::type::UNKNOWN;

            neolib::i_string const& parameter_name() const final
            {
                return parameterName;
            }
            void set_parameter_name(neolib::i_string_view const& aParameterName) final
            {
                parameterName = aParameterName;
            }
            neos::language::type parameter_type() const final
            {
                return parameterType;
            }
            void set_parameter_type(neos::language::type aParameterType) final
            {
                parameterType = aParameterType;
            }

            function_parameter() = default;
            function_parameter(i_function_parameter const& other) :
                parameterName{ other.parameter_name() }, parameterType{ other.parameter_type() }
            {}
        };

        using i_function_parameters = neolib::i_vector<i_function_parameter>;
        using function_parameters = neolib::vector<function_parameter>;

        using i_optional_parameter_list = neolib::i_optional<i_function_parameters>;

        struct i_function_signature
        {
            virtual neolib::i_string const& function_name() const = 0;
            virtual void set_function_name(neolib::i_string const& aFunctionName) = 0;
            virtual i_optional_parameter_list const& parameters() const = 0;
            virtual i_optional_parameter_list& parameters() = 0;
            virtual neos::language::type return_type() const = 0;
            virtual void set_return_type(neos::language::type aFunctionReturnType) = 0;
        };

        using optional_parameter_list = neolib::optional<neolib::vector<function_parameter>>;

        struct function_signature : i_function_signature
        {
            neolib::string functionName;
            optional_parameter_list functionParameters;
            neos::language::type functionReturnType;
            neolib::i_string const& function_name() const final
            {
                return functionName;
            }
            void set_function_name(neolib::i_string const& aFunctionName) final
            {
                functionName = aFunctionName;
            }
            optional_parameter_list const& parameters() const final
            {
                return functionParameters;
            }
            optional_parameter_list& parameters() final
            {
                return functionParameters;
            }
            neos::language::type return_type() const final
            {
                return functionReturnType;
            }
            void set_return_type(neos::language::type aFunctionReturnType) final
            {
                functionReturnType = aFunctionReturnType;
            }
        };
    }
}