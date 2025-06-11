/*
  symbols.hpp

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
#include <neos/language/scope.hpp>

namespace neos
{
    namespace language
    {
        enum class symbol_type : std::uint32_t
        {
            Function,
            Data
        };

        using symbol_name = neolib::string;

        struct i_symbol_key
        {
            virtual neolib::abstract_t<scope_name> const& scope() const = 0;
            virtual symbol_type type() const = 0;
            virtual neolib::abstract_t<symbol_name> const& name() const = 0;

            std::strong_ordering operator<=>(i_symbol_key const& rhs) const noexcept
            {
                return std::forward_as_tuple(scope(), type(), name()) <=> std::forward_as_tuple(rhs.scope(), rhs.type(), rhs.name());
            }
        };

        struct symbol_key : i_symbol_key
        {
            using abstract_type = i_symbol_key;

            scope_name s;
            symbol_type t;
            symbol_name n;

            symbol_key(i_symbol_key const& other) :
                s{ other.scope() }, t{ other.type() }, n{ other.name() }
            {}

            scope_name const& scope() const final { return s; }
            symbol_type type() const final { return t; }
            symbol_name const& name() const final { return n; }

            std::strong_ordering operator<=>(symbol_key const& rhs) const noexcept
            {
                return std::forward_as_tuple(scope(), type(), name()) <=> std::forward_as_tuple(rhs.scope(), rhs.type(), rhs.name());
            }
        };
        using symbol_reference = void const*;
        using symbol_table = neolib::map<symbol_key, symbol_reference>;
        using symbol_table_pointer = symbol_table::abstract_value_type*;
    }
}