/*
  registers.hpp

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
        /// @brief Registers
        enum class reg : uint8_t
        {
            // R0 to R15 are 64-bit registers
            R0  = 0x00,
            R1  = 0x01,
            R2  = 0x02,
            R3  = 0x03,
            R4  = 0x04,
            R5  = 0x05,
            R6  = 0x06,
            R7  = 0x07,
            R8  = 0x08,
            R9  = 0x09,
            R10 = 0x0A,
            R11 = 0x0B,
            R12 = 0x0C,
            R13 = 0x0D,
            R14 = 0x0E,
            R15 = 0x0F,
            // X0 to X15 are 128-bit registers
            X0  = 0x10,
            X1  = 0x11,
            X2  = 0x12,
            X3  = 0x13,
            X4  = 0x14,
            X5  = 0x15,
            X6  = 0x16,
            X7  = 0x17,
            X8  = 0x18,
            X9  = 0x19,
            X10 = 0x1A,
            X11 = 0x1B,
            X12 = 0x1C,
            X13 = 0x1D,
            X14 = 0x1E,
            X15 = 0x1F,            
            // Y0 to Y15 are 256-bit registers
            Y0  = 0x20,
            Y1  = 0x21,
            Y2  = 0x22,
            Y3  = 0x23,
            Y4  = 0x24,
            Y5  = 0x25,
            Y6  = 0x26,
            Y7  = 0x27,
            Y8  = 0x28,
            Y9  = 0x29,
            Y10 = 0x2A,
            Y11 = 0x2B,
            Y12 = 0x2C,
            Y13 = 0x2D,
            Y14 = 0x2E,
            Y15 = 0x2F,
            // Z0 to Z15 are 512-bit registers
            Z0  = 0x30,
            Z1  = 0x31,
            Z2  = 0x32,
            Z3  = 0x33,
            Z4  = 0x34,
            Z5  = 0x35,
            Z6  = 0x36,
            Z7  = 0x37,
            Z8  = 0x38,
            Z9  = 0x39,
            Z10 = 0x3A,
            Z11 = 0x3B,
            Z12 = 0x3C,
            Z13 = 0x3D,
            Z14 = 0x3E,
            Z15 = 0x3F,
            // Aliases
            ZERO  = R0,  // Hardwired zero
            PC    = R12, // Program counter (instruction pointer)
            SP    = R13, // Stack pointer
            LR    = R14, // Link register
            FLAGS = R15
        };

        /// @brief Register data size
        enum class data : uint8_t
        {
            D8  = 0x00,
            D16 = 0x40,
            D32 = 0x80,
            D64 = 0xC0,
        };

        /// @brief Flag register
        enum class flag : uint64_t
        {
            CF = 0x0000000000000001ull, // Carry flag
            PF = 0x0000000000000002ull, // Parity flag
            ZF = 0x0000000000000004ull, // Zero flag
            SF = 0x0000000000000008ull, // Sign flag
            OF = 0x0000000000000010ull  // Overflow flag
        };

        inline uint8_t operator-(reg lhs, reg rhs)
        {
            return static_cast<uint8_t>(lhs) - static_cast<uint8_t>(rhs);
        }
    }
}