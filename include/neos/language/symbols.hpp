/*
  symbols.hpp

  Copyright (c) 2019 Leigh Johnston.  All Rights Reserved.

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
#include <string>
#include <map>

namespace neos
{
    namespace language
    {
        enum class symbol_type : std::uint32_t
        {
            Function,
            Data
        };
        typedef std::string symbol_name_t;
        typedef std::pair<symbol_type, symbol_name_t> symbol_key_t;
        typedef void const* symbol_reference_t;
        typedef std::map<symbol_key_t, symbol_reference_t> symbol_table_t;
    }
}