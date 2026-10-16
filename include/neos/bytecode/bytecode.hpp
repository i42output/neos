/*
  bytecode.hpp

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
#include <cstdint>
#include <string_view>
#include <neolib/core/variant.hpp>
#include <neolib/core/vector.hpp>
#include <neolib/core/optional.hpp>
#include <neonumeric/integer.hpp>
#include <neonumeric/real.hpp>
#include <neos/language/symbol_table.hpp>

namespace neos
{
    namespace bytecode
    {
        // a string in linear memory: its length in bytes (less than 2^31), in UTF-16 code units and in code points (all u32), 
        // its characters (UTF-8) and a NUL (so that runtime plugins can use it in place)
        struct string_header
        {
            std::uint32_t bytes;
            std::uint32_t utf16Units;
            std::uint32_t codePoints;
        };
        inline constexpr std::uint32_t StringHeaderSize = static_cast<std::uint32_t>(sizeof(string_header));

        inline string_header string_header_of(std::string_view aText)
        {
            // (a four byte sequence is a surrogate pair in UTF-16)
            string_header header{ static_cast<std::uint32_t>(aText.size()), 0u, 0u };
            for (auto c : aText)
            {
                auto const b = static_cast<unsigned char>(c);
                if ((b & 0xC0u) != 0x80u)
                {
                    ++header.codePoints;
                    header.utf16Units += (b >= 0xF0u) ? 2u : 1u;
                }
            }
            return header;
        }
    }
}