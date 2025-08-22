/*
  symbol.hpp

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