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
#include <neolib/core/unordered_map.hpp>
#include <neos/language/type.hpp>
#include <neos/language/symbol.hpp>

namespace neos
{
    namespace language
    {
        class i_scope;

        class i_symbol_table_entry
        {
        public:
            using abstract_type = i_symbol_table_entry;
        public:
            virtual ~i_symbol_table_entry() = default;
        public:
            virtual symbol_type type() const = 0;
            virtual std::uint32_t index() const = 0;
        };

        class symbol_table_entry : public i_symbol_table_entry
        {
        public:
            symbol_table_entry(symbol_type aType, std::uint32_t aIndex = 0u) : 
                iType{ aType },
                iIndex{ aIndex }
            {}
            symbol_table_entry(i_symbol_table_entry const& aOther) :
                iType{ aOther.type() },
                iIndex{ aOther.index() }
            {}
            symbol_table_entry& operator=(i_symbol_table_entry const& aOther)
            { 
                iType = aOther.type();
                iIndex = aOther.index();
                return *this; 
            }
        public:
            symbol_type type() const final
            {
                return iType;
            }
            std::uint32_t index() const final
            {
                return iIndex;
            }
        private:
            symbol_type iType;
            std::uint32_t iIndex;
        };

        using symbol_table = neolib::unordered_map<i_scope const*, neolib::unordered_multimap<symbol_name, symbol_table_entry>>;
        using i_symbol_table = neolib::maybe_abstract_t<symbol_table>;
    }
}