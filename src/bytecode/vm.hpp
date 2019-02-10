/*
  vm.hpp

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
#include <array>
#include <vector>
#include <optional>
#include <thread>
#include <neos/context.hpp>
#include "bytecode.hpp"
#include "registers.hpp"
#include "opcodes.hpp"

namespace neos
{
    namespace bytecode
    {
        namespace vm
        {
            namespace exceptions
            {
                struct no_text : std::runtime_error { no_text() : std::runtime_error("neos::bytecode::vm: no text") {} };
                struct invalid_instruction : std::runtime_error { invalid_instruction() : std::runtime_error("neos::bytecode::vm: invalid instruction") {} };
                struct vm_logic_error : std::logic_error { vm_logic_error() : std::logic_error("neos::bytecode::vm: vm logic error") {} };
            }

            namespace registers
            {
                union data
                {
                    u8 u8;
                    u16 u16;
                    u32 u32;
                    u64 u64;
                    i8 i8;
                    i16 i16;
                    i32 i32;
                    i64 i64;
                    f32 f32;
                    f64 f64;
                };
                union simd_128
                {
                    std::array<data, 2> d;
                    // todo: add SIMD data types
                };
                union simd_256
                {
                    std::array<data, 4> d;
                    // todo: add SIMD data types
                };
                union simd_512
                {
                    std::array<data, 8> d;
                    // todo: add SIMD data types
                };
                extern thread_local data r[16];
                extern thread_local simd_128 x[16];
                extern thread_local simd_256 y[16];
                extern thread_local simd_512 z[16];
            }

            template <typename DataType> inline DataType& crack_data(registers::data& aData);
            template <> inline u8& crack_data<u8>(registers::data& aData) { return aData.u8; }
            template <> inline u16& crack_data<u16>(registers::data& aData) { return aData.u16; }
            template <> inline u32& crack_data<u32>(registers::data& aData) { return aData.u32; }
            template <> inline u64& crack_data<u64>(registers::data& aData) { return aData.u64; }
            template <> inline i8& crack_data<i8>(registers::data& aData) { return aData.i8; }
            template <> inline i16& crack_data<i16>(registers::data& aData) { return aData.i16; }
            template <> inline i32& crack_data<i32>(registers::data& aData) { return aData.i32; }
            template <> inline i64& crack_data<i64>(registers::data& aData) { return aData.i64; }
            template <> inline f32& crack_data<f32>(registers::data& aData) { return aData.f32; }
            template <> inline f64& crack_data<f64>(registers::data& aData) { return aData.f64; }

            template <typename DataType> inline DataType& r(reg aRegister) { return crack_data<DataType>(registers::r[aRegister - reg::R0]); }
            template <typename DataType, reg Register> inline DataType& r() { return crack_data<DataType>(registers::r[Register - reg::R0]); }
            template <typename DataType> inline DataType& x(reg aRegister) { return crack_data<DataType>(registers::x[aRegister - reg::X0].d[0]); }
            template <typename DataType, reg Register> inline DataType& x() { return crack_data<DataType>(registers::x[Register - reg::X0].d[0]); }
            template <typename DataType> inline DataType& y(reg aRegister) { return crack_data<DataType>(registers::y[aRegister - reg::Y0].d[0]); }
            template <typename DataType, reg Register> inline DataType& y() { return crack_data<DataType>(registers::y[Register - reg::Y0].d[0]); }
            template <typename DataType> inline DataType& z(reg aRegister) { return crack_data<DataType>(registers::z[aRegister - reg::Z0].d[0]); }
            template <typename DataType, reg Register> inline DataType& z() { return crack_data<DataType>(registers::z[Register - reg::Z0].d[0]); }

            template <typename DataType> 
            inline DataType read(reg aRegister)
            {
                switch (aRegister)
                {
                case reg::R0:
                    return 0;
                case reg::R1:
                case reg::R2:
                case reg::R3:
                case reg::R4:
                case reg::R5:
                case reg::R6:
                case reg::R7:
                case reg::R8:
                case reg::R9:
                case reg::R10:
                case reg::R11:
                case reg::R12:
                case reg::R13:
                case reg::R14:
                case reg::R15:
                    return r<DataType>(aRegister);
                case reg::X0:
                case reg::X1:
                case reg::X2:
                case reg::X3:
                case reg::X4:
                case reg::X5:
                case reg::X6:
                case reg::X7:
                case reg::X8:
                case reg::X9:
                case reg::X10:
                case reg::X11:
                case reg::X12:
                case reg::X13:
                case reg::X14:
                case reg::X15:
                    return x<DataType>(aRegister);
                case reg::Y0:
                case reg::Y1:
                case reg::Y2:
                case reg::Y3:
                case reg::Y4:
                case reg::Y5:
                case reg::Y6:
                case reg::Y7:
                case reg::Y8:
                case reg::Y9:
                case reg::Y10:
                case reg::Y11:
                case reg::Y12:
                case reg::Y13:
                case reg::Y14:
                case reg::Y15:
                    return y<DataType>(aRegister);
                case reg::Z0:
                case reg::Z1:
                case reg::Z2:
                case reg::Z3:
                case reg::Z4:
                case reg::Z5:
                case reg::Z6:
                case reg::Z7:
                case reg::Z8:
                case reg::Z9:
                case reg::Z10:
                case reg::Z11:
                case reg::Z12:
                case reg::Z13:
                case reg::Z14:
                case reg::Z15:
                    return z<DataType>(aRegister);
                }
            }

            template <typename DataType>
            inline DataType& write(reg aRegister)
            {
                switch (aRegister)
                {
                case reg::R0:
                    throw exceptions::invalid_instruction();
                case reg::R1:
                case reg::R2:
                case reg::R3:
                case reg::R4:
                case reg::R5:
                case reg::R6:
                case reg::R7:
                case reg::R8:
                case reg::R9:
                case reg::R10:
                case reg::R11:
                case reg::R12:
                case reg::R13:
                case reg::R14:
                case reg::R15:
                    return r<DataType>(aRegister);
                case reg::X0:
                case reg::X1:
                case reg::X2:
                case reg::X3:
                case reg::X4:
                case reg::X5:
                case reg::X6:
                case reg::X7:
                case reg::X8:
                case reg::X9:
                case reg::X10:
                case reg::X11:
                case reg::X12:
                case reg::X13:
                case reg::X14:
                case reg::X15:
                    return x<DataType>(aRegister);
                case reg::Y0:
                case reg::Y1:
                case reg::Y2:
                case reg::Y3:
                case reg::Y4:
                case reg::Y5:
                case reg::Y6:
                case reg::Y7:
                case reg::Y8:
                case reg::Y9:
                case reg::Y10:
                case reg::Y11:
                case reg::Y12:
                case reg::Y13:
                case reg::Y14:
                case reg::Y15:
                    return y<DataType>(aRegister);
                case reg::Z0:
                case reg::Z1:
                case reg::Z2:
                case reg::Z3:
                case reg::Z4:
                case reg::Z5:
                case reg::Z6:
                case reg::Z7:
                case reg::Z8:
                case reg::Z9:
                case reg::Z10:
                case reg::Z11:
                case reg::Z12:
                case reg::Z13:
                case reg::Z14:
                case reg::Z15:
                    return z<DataType>(aRegister);
                default:
                    throw exceptions::vm_logic_error();
                }
            }

            class thread
            {
            public:
                thread(const text_t& aText);
            public:
                bool joinable() const;
                void join();
                void terminate();
                const std::chrono::steady_clock::time_point& start_time() const;
                uint64_t count() const;
                std::string metrics() const;
                i64 result() const;
            private:
                i64 execute();
            private:
                const text_t& iText;
                std::optional<std::thread> iNativeThread;
                uint64_t iCount;
                std::atomic<bool> iTerminate;
                std::chrono::steady_clock::time_point iStartTime;
                std::atomic<uint64_t> iCountSample;
                i64 iResult;
            };
        }
   }
}