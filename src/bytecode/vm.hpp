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
                thread_local uint64_t r[16];
                thread_local std::array<double, 2> x[16];
                thread_local std::array<double, 4> y[16];
                thread_local std::array<double, 8> z[16];
            }

            template <typename DataType>
            inline DataType& r(reg aRegister)
            {
                return *reinterpret_cast<DataType*>(&registers::r[aRegister - reg::R0]);
            }

            template <typename DataType, reg Register>
            inline DataType& r()
            {
                return *reinterpret_cast<DataType*>(&registers::r[Register - reg::R0]);
            }

            template <typename DataType>
            inline DataType& x(reg aRegister)
            {
                return *reinterpret_cast<DataType*>(&registers::x[aRegister - reg::X0][0]);
            }

            template <typename DataType, reg Register>
            inline DataType& x()
            {
                return *reinterpret_cast<DataType*>(&registers::x[Register - reg::X0][0]);
            }

            template <typename DataType>
            inline DataType& y(reg aRegister)
            {
                return *reinterpret_cast<DataType*>(&registers::y[aRegister - reg::Y0][0]);
            }

            template <typename DataType, reg Register>
            inline DataType& y()
            {
                return *reinterpret_cast<DataType*>(&registers::y[Register - reg::Y0][0]);
            }

            template <typename DataType>
            inline DataType& z(reg aRegister)
            {
                return *reinterpret_cast<DataType*>(&registers::y[aRegister - reg::Z0][0]);
            }

            template <typename DataType, reg Register>
            inline DataType& z()
            {
                return *reinterpret_cast<DataType*>(&registers::y[Register - reg::Z0][0]);
            }

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