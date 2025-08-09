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
#include <neos/language/type.hpp>

namespace neos
{
    namespace language
    {
        using i_optional_parameter_list = neolib::i_optional<i_function_parameters>;

        struct i_function_signature
        {
            virtual neolib::i_string const& name() const = 0;
            virtual void set_name(neolib::i_string const& aFunctionName) = 0;
            virtual i_optional_parameter_list const& parameters() const = 0;
            virtual i_optional_parameter_list& parameters() = 0;
            virtual void set_parameters(i_function_parameters const& aFunctionParameters) = 0;
            virtual neos::language::type return_type() const = 0;
            virtual void set_return_type(neos::language::type aFunctionReturnType) = 0;
        };

        using optional_parameter_list = neolib::optional<neolib::vector<function_parameter>>;

        struct function_signature : i_function_signature
        {
            neolib::string functionName;
            optional_parameter_list functionParameters;
            neos::language::type functionReturnType;

            function_signature() = default;

            function_signature(i_function_signature const& aOther) :
                functionName{ aOther.name() },
                functionParameters{ aOther.parameters() },
                functionReturnType{ aOther.return_type() } {}

            function_signature& operator=(i_function_signature const& aOther)
            {
                functionName = aOther.name();
                functionParameters = aOther.parameters();
                functionReturnType = aOther.return_type();
                return *this;
            }

            neolib::i_string const& name() const final
            {
                return functionName;
            }
            void set_name(neolib::i_string const& aFunctionName) final
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
            void set_parameters(i_function_parameters const& aFunctionParameters) final
            {
                functionParameters = aFunctionParameters;
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