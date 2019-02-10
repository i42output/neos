/*
  text.hpp

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
#include "registers.hpp"
#include "opcodes.hpp"
#include "vm.hpp"

namespace neos
{
    namespace bytecode
    {
        template <typename DataType> struct immediate_opcode_modifiers;
        template <> struct immediate_opcode_modifiers<u8> { static constexpr opcode m = static_cast<opcode>(opcode_type::Immediate | opcode_type::D8); };
        template <> struct immediate_opcode_modifiers<u16> { static constexpr opcode m = static_cast<opcode>(opcode_type::Immediate | opcode_type::D16); };
        template <> struct immediate_opcode_modifiers<u32> { static constexpr opcode m = static_cast<opcode>(opcode_type::Immediate | opcode_type::D32); };
        template <> struct immediate_opcode_modifiers<u64> { static constexpr opcode m = static_cast<opcode>(opcode_type::Immediate | opcode_type::D64); };
        template <> struct immediate_opcode_modifiers<i8> { static constexpr opcode m = static_cast<opcode>(opcode_type::Immediate | opcode_type::D8 | opcode_type::Signed); };
        template <> struct immediate_opcode_modifiers<i16> { static constexpr opcode m = static_cast<opcode>(opcode_type::Immediate | opcode_type::D16 | opcode_type::Signed); };
        template <> struct immediate_opcode_modifiers<i32> { static constexpr opcode m = static_cast<opcode>(opcode_type::Immediate | opcode_type::D32 | opcode_type::Signed); };
        template <> struct immediate_opcode_modifiers<i64> { static constexpr opcode m = static_cast<opcode>(opcode_type::Immediate | opcode_type::D64 | opcode_type::Signed); };
        template <> struct immediate_opcode_modifiers<f32> { static constexpr opcode m = static_cast<opcode>(opcode_type::Immediate | opcode_type::D32 | opcode_type::Float); };
        template <> struct immediate_opcode_modifiers<f64> { static constexpr opcode m = static_cast<opcode>(opcode_type::Immediate | opcode_type::D64 | opcode_type::Float); };

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

        template <typename DataType>
        inline uint64_t emit(text_t& aText, opcode aOpcode, DataType aImmediate)
        {
            auto pos = aText.size();
            if constexpr (sizeof(DataType) > 1)
            {
                to_bytes(aText, to_integer(aOpcode | immediate_opcode_modifiers<DataType>::m));
                to_bytes(aText, aImmediate);
            }
            else
                to_bytes(aText, to_integer(aOpcode | immediate_opcode_modifiers<DataType>::m | static_cast<opcode>(static_cast<uint8_t>(aImmediate))));
            return pos;
        }

        template <typename DataType>
        inline uint64_t emit(text_t& aText, opcode aOpcode, reg aRegister, DataType aImmediate)
        {
            auto pos = aText.size();
            if constexpr (sizeof(DataType) > 1)
            {
                to_bytes(aText, to_integer(aOpcode | immediate_opcode_modifiers<DataType>::m | static_cast<opcode>(static_cast<opcode_base_t>(aRegister) << REG1_SHIFT)));
                to_bytes(aText, aImmediate);
            }
            else
                to_bytes(aText, to_integer(aOpcode | immediate_opcode_modifiers<DataType>::m | static_cast<opcode>(static_cast<uint8_t>(aImmediate)) | static_cast<opcode>(static_cast<opcode_base_t>(aRegister) << REG1_SHIFT)));
            return pos;
        }
    }
}