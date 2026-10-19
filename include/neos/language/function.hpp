/*
  function.hpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

     * Neither the name of the copyright holder nor the names of its
       contributors may be used to endorse or promote products derived from
       this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
            virtual i_function_parameters const& locals() const = 0;
            virtual i_function_parameters& locals() = 0;
            virtual neos::language::type return_type() const = 0;
            virtual void set_return_type(neos::language::type aFunctionReturnType) = 0;
        };

        using optional_parameter_list = neolib::optional<neolib::vector<function_parameter>>;

        struct function_signature : i_function_signature
        {
            neolib::string functionName;
            optional_parameter_list functionParameters;
            function_parameters functionLocals;
            neos::language::type functionReturnType = neos::language::type::Void;

            function_signature() = default;

            function_signature(i_function_signature const& aOther) :
                functionName{ aOther.name() },
                functionParameters{ aOther.parameters() },
                functionLocals{ aOther.locals() },
                functionReturnType{ aOther.return_type() } {}

            function_signature& operator=(i_function_signature const& aOther)
            {
                functionName = aOther.name();
                functionParameters = aOther.parameters();
                functionLocals = aOther.locals();
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
            function_parameters const& locals() const final
            {
                return functionLocals;
            }
            function_parameters& locals() final
            {
                return functionLocals;
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