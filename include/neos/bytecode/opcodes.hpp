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

#pragma once

#include <neos/neos.hpp>
#include "registers.hpp"

namespace neos
{
    namespace bytecode
    {
        typedef std::uint32_t opcode_base_t;

        /// @brief Opcode types
        enum class opcode_type : opcode_base_t
        {
            // Masks
            D8_MASK         = 0b00000000000000000000000011111111,
            REG2_MASK       = 0b00000000000000000000000000111111,
            REG1_MASK       = 0b00000000000000000011111100000000,
            IMMEDIATE_MASK  = 0b00000000000000000100000000000000,
            OPCODE_MASK     = 0b00000011100011111000000000000000,
            DATA_MASK       = 0b00000000111100000000000000000000,
            SIGNED_MASK     = 0b00000000010000000000000000000000,
            CLASS_MASK      = 0b00000011000000000000000000000000,
            LINK_MASK       = 0b00000100000000000000000000000000,
            COND_MASK       = 0b00001000000000000000000000000000,
            COND_OP_MASK    = 0b11110000000000000000000000000000,
            // Data                             
            Immediate       = 0b00000000000000000100000000000000,
            D8              = 0b00000000000000000000000000000000,
            D16             = 0b00000000000100000000000000000000,
            D32             = 0b00000000001000000000000000000000,
            D64             = 0b00000000001100000000000000000000,
            Unsigned        = 0b00000000000000000000000000000000,
            Signed          = 0b00000000010000000000000000000000,
            Integer         = 0b00000000000000000000000000000000,
            Float           = 0b00000000100000000000000000000000,
            // Class                            
            Branch          = 0b00000000000000000000000000000000,
            Data            = 0b00000001000000000000000000000000,
            Memory          = 0b00000010000000000000000000000000,
            Privileged      = 0b00000011000000000000000000000000,
            // Link register                    
            Link            = 0b00000100000000000000000000000000,
            // Conditional execute              
            Cond            = 0b00001000000000000000000000000000,
            // Conditions                       
            CondEQ          = 0b00000000000000000000000000000000,
            CondNE          = 0b00010000000000000000000000000000,
            CondLT          = 0b00100000000000000000000000000000,
            CondLTU         = 0b00110000000000000000000000000000,
            CondLTE         = 0b01000000000000000000000000000000,
            CondLTEU        = 0b01010000000000000000000000000000,
            CondGT          = 0b01100000000000000000000000000000,
            CondGTU         = 0b01110000000000000000000000000000,
            CondGTE         = 0b10000000000000000000000000000000,
            CondGTEU        = 0b10010000000000000000000000000000,
            CondNG          = 0b10100000000000000000000000000000,
            CondPS          = 0b10110000000000000000000000000000,
            CondVS          = 0b11000000000000000000000000000000,
            CondVC          = 0b11010000000000000000000000000000,
        };

        constexpr opcode_base_t REG1_SHIFT = 8;

        inline constexpr opcode_type operator|(opcode_type lhs, opcode_type rhs)
        {
            return static_cast<opcode_type>(static_cast<opcode_base_t>(lhs) | static_cast<opcode_base_t>(rhs));
        }

        inline constexpr opcode_base_t operator|(opcode_base_t lhs, opcode_type rhs)
        {
            return lhs | static_cast<opcode_base_t>(rhs);
        }

        inline constexpr opcode_type operator&(opcode_type lhs, opcode_type rhs)
        {
            return static_cast<opcode_type>(static_cast<opcode_base_t>(lhs) & static_cast<opcode_base_t>(rhs));
        }

        /// @brief Opcodes
        enum class opcode : opcode_base_t
        {
            B       = 0b00000000000000000000000000000000 | opcode_type::Branch,
            BL      = 0b00000000000000000000000000000000 | opcode_type::Branch | opcode_type::Link,
            BEQ     = 0b00000000000000000000000000000000 | opcode_type::Branch | opcode_type::CondEQ,
            BNE     = 0b00000000000000000000000000000000 | opcode_type::Branch | opcode_type::CondNE,
            BLT     = 0b00000000000000000000000000000000 | opcode_type::Branch | opcode_type::CondLT,
            BLTU    = 0b00000000000000000000000000000000 | opcode_type::Branch | opcode_type::CondLTU,
            BGE     = 0b00000000000000000000000000000000 | opcode_type::Branch | opcode_type::CondGTE,
            BGEU    = 0b00000000000000000000000000000000 | opcode_type::Branch | opcode_type::CondGTEU,
            MOV     = 0b00000000000000001000000000000000 | opcode_type::Data,
            LDR     = 0b00000000000000010000000000000000 | opcode_type::Memory,
            STR     = 0b00000000000000011000000000000000 | opcode_type::Memory,
            CMP     = 0b00000000000000100000000000000000 | opcode_type::Data,
            CMPF    = 0b00000000000000100000000000000000 | opcode_type::Data | opcode_type::Float,
            ADD     = 0b00000000000000101000000000000000 | opcode_type::Data,
            ADDF    = 0b00000000000000101000000000000000 | opcode_type::Data | opcode_type::Float,
            ADC     = 0b00000000000000110000000000000000 | opcode_type::Data,
            SUB     = 0b00000000000000111000000000000000 | opcode_type::Data,
            SUBF    = 0b00000000000000111000000000000000 | opcode_type::Data | opcode_type::Float,
            SBC     = 0b00000000000001000000000000000000 | opcode_type::Data,
            MUL     = 0b00000000000001001000000000000000 | opcode_type::Data,
            MULF    = 0b00000000000001001000000000000000 | opcode_type::Data | opcode_type::Float,
            DIV     = 0b00000000000001010000000000000000 | opcode_type::Data,
            DIVF    = 0b00000000000001010000000000000000 | opcode_type::Data | opcode_type::Float,
            AND     = 0b00000000000001011000000000000000 | opcode_type::Data,
            OR      = 0b00000000000001100000000000000000 | opcode_type::Data,
            XOR     = 0b00000000000001101000000000000000 | opcode_type::Data,
            TEQ     = 0b00000000000001110000000000000000 | opcode_type::Data,
            TST     = 0b00000000000001111000000000000000 | opcode_type::Data,
            EPRIV   = 0b00000000000010000000000000000000 | opcode_type::Privileged,
            LPRIV   = 0b00000000000011000000000000000000 | opcode_type::Privileged
        };

        inline constexpr opcode operator|(opcode lhs, std::uint8_t rhs)
        {
            return static_cast<opcode>(static_cast<opcode_base_t>(lhs) | static_cast<opcode_base_t>(rhs));
        }

        inline constexpr opcode operator|(opcode lhs, registers rhs)
        {
            return static_cast<opcode>(static_cast<opcode_base_t>(lhs) | (static_cast<opcode_base_t>(rhs) << REG1_SHIFT));
        }

        inline constexpr opcode operator|(opcode lhs, const std::pair<registers, registers>& rhs)
        {
            return static_cast<opcode>(static_cast<opcode_base_t>(lhs) | (static_cast<opcode_base_t>(rhs.first) << REG1_SHIFT) | static_cast<opcode_base_t>(rhs.second));
        }

        inline constexpr opcode operator|(opcode lhs, opcode_type rhs)
        {
            return static_cast<opcode>(static_cast<opcode_base_t>(lhs) | static_cast<opcode_base_t>(rhs));
        }

        inline constexpr opcode operator&(opcode lhs, opcode_type rhs)
        {
            return static_cast<opcode>(static_cast<opcode_base_t>(lhs) & static_cast<opcode_base_t>(rhs));
        }

        inline constexpr opcode operator|(opcode lhs, opcode rhs)
        {
            return static_cast<opcode>(static_cast<opcode_base_t>(lhs) | static_cast<opcode_base_t>(rhs));
        }

        inline registers r1(opcode aOpcode)
        {
            return static_cast<registers>(static_cast<opcode_base_t>(aOpcode & opcode_type::REG1_MASK) >> REG1_SHIFT);
        }

        inline registers r2(opcode aOpcode)
        {
            return static_cast<registers>(static_cast<opcode_base_t>(aOpcode & opcode_type::REG2_MASK));
        }
    }
}