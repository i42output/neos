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

#include <neolib/neolib.hpp>
#include <array>
#include <vector>
#include <optional>
#include <thread>
#include "registers.hpp"

namespace neos
{
    namespace bytecode
    {
        namespace vm
        {
            typedef uint8_t u8;
            typedef uint16_t u16;
            typedef uint32_t u32;
            typedef uint64_t u64;
            typedef int8_t i8;
            typedef int16_t i16;
            typedef int32_t i32;
            typedef int64_t i64;
            typedef float f32;
            typedef double f64;
            static_assert(sizeof(f32) == 4);
            static_assert(sizeof(f64) == 8);

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
                    // todo: add SIMD data types
                };
                thread_local data r[16];
                thread_local std::array<data, 2> x[16]; // todo: use SIMD data types
                thread_local std::array<data, 4> y[16]; // todo: use SIMD data types
                thread_local std::array<data, 8> z[16]; // todo: use SIMD data types
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
            template <typename DataType> inline DataType& x(reg aRegister) { return crack_data<DataType>(registers::x[aRegister - reg::X0][0]); }
            template <typename DataType, reg Register> inline DataType& x() { return crack_data<DataType>(registers::x[Register - reg::X0][0]); }
            template <typename DataType> inline DataType& y(reg aRegister) { return crack_data<DataType>(registers::y[aRegister - reg::Y0][0]); }
            template <typename DataType, reg Register> inline DataType& y() { return crack_data<DataType>(registers::y[Register - reg::Y0][0]); }
            template <typename DataType> inline DataType& z(reg aRegister) { return crack_data<DataType>(registers::z[aRegister - reg::Z0][0]); }
            template <typename DataType, reg Register> inline DataType& z() { return crack_data<DataType>(registers::z[Register - reg::Z0][0]); }

            typedef std::vector<std::byte> text;

            class thread
            {
            public:
                thread(const text& aText);
            public:
                bool joinable() const;
                void join();
                void terminate();
                i64 result() const;
            private:
                i64 execute();
            private:
                const text& iText;
                std::optional<std::thread> iNativeThread;
                i64 iResult;
            };
        }
   }
}