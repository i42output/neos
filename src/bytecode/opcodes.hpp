/*
  opcodes.hpp

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

#include <neolib/neolib.hpp>

namespace neos
{
    namespace bytecode
    {
        /// @brief Opcode types
        enum class opcode_type : uint16_t
        {
            // Masks
            DATA_MASK       = 0b0000000011100000,
            CLASS_MASK      = 0b0000001100000000,
            LINK_MASK       = 0b0000010000000000,
            COND_MASK       = 0b0000100000000000,
            COND_OP_MASK    = 0b1111000000000000,
            // Data
            D8              = 0b0000000000000000,
            D16             = 0b0000000000100000,
            D32             = 0b0000000001000000,
            D64             = 0b0000000001100000,
            Integer         = 0b0000000000000000,
            Float           = 0b0000000010000000,
            // Class                    
            Branch          = 0b0000000000000000,
            Data            = 0b0000000100000000,
            Memory          = 0b0000001000000000,
            Priviledged     = 0b0000001100000000,
            // Link register                
            Link            = 0b0000010000000000,
            // Conditional execute
            Cond            = 0b0000100000000000,
            // Conditions
            CondEQ          = 0b0000000000000000,
            CondNE          = 0b0001000000000000,
            CondLT          = 0b0010000000000000,
            CondLTU         = 0b0011000000000000,
            CondLTE         = 0b0100000000000000,
            CondLTEU        = 0b0101000000000000,
            CondGT          = 0b0110000000000000,
            CondGTU         = 0b0111000000000000,
            CondGTE         = 0b1000000000000000,
            CondGTEU        = 0b1001000000000000,
            CondNG          = 0b1010000000000000,
            CondPS          = 0b1011000000000000,
            CondVS          = 0b1100000000000000,
            CondVC          = 0b1101000000000000,
        };

        inline constexpr opcode_type operator|(opcode_type lhs, opcode_type rhs)
        {
            return static_cast<opcode_type>(static_cast<uint16_t>(lhs) | static_cast<uint16_t>(rhs));
        }

        inline constexpr uint16_t operator|(uint16_t lhs, opcode_type rhs)
        {
            return lhs | static_cast<uint16_t>(rhs);
        }

        inline constexpr opcode_type operator&(opcode_type lhs, opcode_type rhs)
        {
            return static_cast<opcode_type>(static_cast<uint16_t>(lhs) & static_cast<uint16_t>(rhs));
        }

        /// @brief Opcodes
        enum class opcode : uint16_t
        {
            B       = 0b00000000 | opcode_type::Branch,
            BL      = 0b00000000 | opcode_type::Branch | opcode_type::Link,
            BEQ     = 0b00000000 | opcode_type::Branch | opcode_type::CondEQ,
            BNE     = 0b00000000 | opcode_type::Branch | opcode_type::CondNE,
            BLT     = 0b00000000 | opcode_type::Branch | opcode_type::CondLT,
            BLTU    = 0b00000000 | opcode_type::Branch | opcode_type::CondLTU,
            BGE     = 0b00000000 | opcode_type::Branch | opcode_type::CondGTE,
            BGEU    = 0b00000000 | opcode_type::Branch | opcode_type::CondGTEU,
            MOV     = 0b00000001 | opcode_type::Data,
            LDR     = 0b00000010 | opcode_type::Memory,
            STR     = 0b00000011 | opcode_type::Memory,
            CMP     = 0b00000100 | opcode_type::Data,
            ADD     = 0b00000101 | opcode_type::Data,
            ADDF    = 0b00000101 | opcode_type::Data | opcode_type::Float,
            ADC     = 0b00000110 | opcode_type::Data,
            SUB     = 0b00000111 | opcode_type::Data,
            SUBF    = 0b00000111 | opcode_type::Data | opcode_type::Float,
            SBC     = 0b00001000 | opcode_type::Data,
            MUL     = 0b00001001 | opcode_type::Data,
            MULF    = 0b00001001 | opcode_type::Data | opcode_type::Float,
            DIV     = 0b00001010 | opcode_type::Data,
            DIVF    = 0b00001010 | opcode_type::Data | opcode_type::Float,
            AND     = 0b00001011 | opcode_type::Data,
            OR      = 0b00001100 | opcode_type::Data,
            XOR     = 0b00001101 | opcode_type::Data,
            TEQ     = 0b00001110 | opcode_type::Data,
            TST     = 0b00001111 | opcode_type::Data,
            EPRIV   = 0b00010000 | opcode_type::Priviledged,
            LPRIV   = 0b00011000 | opcode_type::Priviledged
        };
    }
}