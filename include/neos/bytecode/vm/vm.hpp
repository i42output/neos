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
#include <neos/bytecode/bytecode.hpp>
#include <neos/bytecode/registers.hpp>
#include <neos/bytecode/opcodes.hpp>

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

            namespace cpu
            {
                namespace registers
                {
                    extern thread_local reg_64 r[16];
                    extern thread_local reg_simd_128 x[16];
                    extern thread_local reg_simd_256 y[16];
                    extern thread_local reg_simd_512 z[16];
                }
            }

            template <typename DataType> inline DataType& crack_data(reg_data_64& aData);
            template <> inline u8& crack_data<u8>(reg_data_64& aData) { return aData.u8; }
            template <> inline u16& crack_data<u16>(reg_data_64& aData) { return aData.u16; }
            template <> inline u32& crack_data<u32>(reg_data_64& aData) { return aData.u32; }
            template <> inline u64& crack_data<u64>(reg_data_64& aData) { return aData.u64; }
            template <> inline i8& crack_data<i8>(reg_data_64& aData) { return aData.i8; }
            template <> inline i16& crack_data<i16>(reg_data_64& aData) { return aData.i16; }
            template <> inline i32& crack_data<i32>(reg_data_64& aData) { return aData.i32; }
            template <> inline i64& crack_data<i64>(reg_data_64& aData) { return aData.i64; }
            template <> inline f32& crack_data<f32>(reg_data_64& aData) { return aData.f32; }
            template <> inline f64& crack_data<f64>(reg_data_64& aData) { return aData.f64; }

            template <typename DataType> inline DataType& r(registers aRegister) { return crack_data<DataType>(cpu::registers::r[aRegister - registers::R0]); }
            template <typename DataType, registers Register> inline DataType& r() { return crack_data<DataType>(cpu::registers::r[Register - registers::R0]); }
            template <typename DataType> inline DataType& x(registers aRegister) { return crack_data<DataType>(cpu::registers::x[aRegister - registers::X0].d[0]); }
            template <typename DataType, registers Register> inline DataType& x() { return crack_data<DataType>(cpu::registers::x[Register - registers::X0].d[0]); }
            template <typename DataType> inline DataType& y(registers aRegister) { return crack_data<DataType>(cpu::registers::y[aRegister - registers::Y0].d[0]); }
            template <typename DataType, registers Register> inline DataType& y() { return crack_data<DataType>(cpu::registers::y[Register - registers::Y0].d[0]); }
            template <typename DataType> inline DataType& z(registers aRegister) { return crack_data<DataType>(cpu::registers::z[aRegister - registers::Z0].d[0]); }
            template <typename DataType, registers Register> inline DataType& z() { return crack_data<DataType>(cpu::registers::z[Register - registers::Z0].d[0]); }

            template <typename DataType> 
            inline DataType read(registers aRegister)
            {
                switch (aRegister)
                {
                case registers::R0:
                    return 0;
                case registers::R1:
                case registers::R2:
                case registers::R3:
                case registers::R4:
                case registers::R5:
                case registers::R6:
                case registers::R7:
                case registers::R8:
                case registers::R9:
                case registers::R10:
                case registers::R11:
                case registers::R12:
                case registers::R13:
                case registers::R14:
                case registers::R15:
                    return r<DataType>(aRegister);
                case registers::X0:
                case registers::X1:
                case registers::X2:
                case registers::X3:
                case registers::X4:
                case registers::X5:
                case registers::X6:
                case registers::X7:
                case registers::X8:
                case registers::X9:
                case registers::X10:
                case registers::X11:
                case registers::X12:
                case registers::X13:
                case registers::X14:
                case registers::X15:
                    return x<DataType>(aRegister);
                case registers::Y0:
                case registers::Y1:
                case registers::Y2:
                case registers::Y3:
                case registers::Y4:
                case registers::Y5:
                case registers::Y6:
                case registers::Y7:
                case registers::Y8:
                case registers::Y9:
                case registers::Y10:
                case registers::Y11:
                case registers::Y12:
                case registers::Y13:
                case registers::Y14:
                case registers::Y15:
                    return y<DataType>(aRegister);
                case registers::Z0:
                case registers::Z1:
                case registers::Z2:
                case registers::Z3:
                case registers::Z4:
                case registers::Z5:
                case registers::Z6:
                case registers::Z7:
                case registers::Z8:
                case registers::Z9:
                case registers::Z10:
                case registers::Z11:
                case registers::Z12:
                case registers::Z13:
                case registers::Z14:
                case registers::Z15:
                    return z<DataType>(aRegister);
                }
            }

            template <typename DataType>
            inline DataType& write(registers aRegister)
            {
                switch (aRegister)
                {
                case registers::R0:
                    throw exceptions::invalid_instruction();
                case registers::R1:
                case registers::R2:
                case registers::R3:
                case registers::R4:
                case registers::R5:
                case registers::R6:
                case registers::R7:
                case registers::R8:
                case registers::R9:
                case registers::R10:
                case registers::R11:
                case registers::R12:
                case registers::R13:
                case registers::R14:
                case registers::R15:
                    return r<DataType>(aRegister);
                case registers::X0:
                case registers::X1:
                case registers::X2:
                case registers::X3:
                case registers::X4:
                case registers::X5:
                case registers::X6:
                case registers::X7:
                case registers::X8:
                case registers::X9:
                case registers::X10:
                case registers::X11:
                case registers::X12:
                case registers::X13:
                case registers::X14:
                case registers::X15:
                    return x<DataType>(aRegister);
                case registers::Y0:
                case registers::Y1:
                case registers::Y2:
                case registers::Y3:
                case registers::Y4:
                case registers::Y5:
                case registers::Y6:
                case registers::Y7:
                case registers::Y8:
                case registers::Y9:
                case registers::Y10:
                case registers::Y11:
                case registers::Y12:
                case registers::Y13:
                case registers::Y14:
                case registers::Y15:
                    return y<DataType>(aRegister);
                case registers::Z0:
                case registers::Z1:
                case registers::Z2:
                case registers::Z3:
                case registers::Z4:
                case registers::Z5:
                case registers::Z6:
                case registers::Z7:
                case registers::Z8:
                case registers::Z9:
                case registers::Z10:
                case registers::Z11:
                case registers::Z12:
                case registers::Z13:
                case registers::Z14:
                case registers::Z15:
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
                reg_64 result() const;
            private:
                reg_64 execute();
            private:
                const text_t& iText;
                std::optional<std::thread> iNativeThread;
                uint64_t iCount;
                std::atomic<bool> iTerminate;
                std::chrono::steady_clock::time_point iStartTime;
                std::atomic<uint64_t> iCountSample;
                reg_64 iResult;
            };
        }
    }
}