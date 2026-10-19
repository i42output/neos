/*
  symbol.hpp

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
#include <neolib/core/string.hpp>
#include <neolib/core/map.hpp>
#include <neos/language/type.hpp>
#include <neos/language/scope.hpp>

namespace neos
{
    namespace language
    {
        using symbol_type = type;

        using i_symbol_name = neolib::i_string;
        using symbol_name = neolib::string;

        struct i_symbol_reference
        {
            using abstract_type = i_symbol_reference;

            virtual ~i_symbol_reference() = default;

            virtual i_scope const& scope() const = 0;
            virtual neolib::i_string const& name() const = 0;
        };

        struct symbol_reference : i_symbol_reference
        {
            neolib::ref_ptr<i_scope const> symbolScope;
            neolib::string symbolName;

            symbol_reference() = default;
            symbol_reference(i_symbol_reference const& other) :
                symbolScope{ other.scope() }, symbolName{ other.name() } {}
            symbol_reference(i_scope const& scope, neolib::string const& name) :
                symbolScope{ scope }, symbolName{ name } {}

            i_scope const& scope() const final { return *symbolScope; }
            neolib::i_string const& name() const final { return symbolName; }
        };

        inline std::ostream& operator<<(std::ostream& stream, i_symbol_reference const& operand)
        {
            stream << "@" << "(" << operand.scope().name() << ")::" << operand.name();
            return stream;
        }

        using i_symbol_name = neolib::i_string;
        using symbol_name = neolib::string;
    }
}