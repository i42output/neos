/*
  text.cpp

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

#include <neos/neos.hpp>
#include <neolib/vecarray.hpp>
#include "text.hpp"

namespace neos
{
    namespace bytecode
    {
        namespace
        {
            template <typename Enum>
            inline std::underlying_type_t<Enum> to_integer(Enum aEnum)
            {
                return static_cast<std::underlying_type_t<Enum>>(aEnum);
            }

            template <typename ByteContainer, typename Integer>
            inline void to_bytes(ByteContainer& aContainer, Integer aInteger)
            {
                aContainer.insert(aContainer.end(), reinterpret_cast<const std::byte*>(&aInteger), reinterpret_cast<const std::byte*>(&aInteger) + sizeof(Integer));
            }
        }

        uint64_t emit(text_t& aText, opcode aOpcode, uint64_t aImmediate)
        {
            auto pos = aText.size();
            to_bytes(aText, to_integer(aOpcode | opcode_type::Immediate | opcode_type::D64));
            to_bytes(aText, aImmediate);
            return pos;
        }

        uint64_t emit(text_t& aText, opcode aOpcode, reg aRegister, uint64_t aImmediate)
        {
            auto pos = aText.size();
            to_bytes(aText, to_integer(aOpcode | opcode_type::Immediate | opcode_type::D64 | static_cast<opcode_type>(static_cast<opcode_base_t>(aRegister) << REG1_SHIFT)));
            to_bytes(aText, aImmediate);
            return pos;
        }
    }
}