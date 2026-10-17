/*
  simd.hpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

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

// The kernels (kernels.hpp) compiled for the instruction sets NumPy dispatches its loops to on x86 CPUs (X86_V4 and
// X86_V3): the CPU's (and NPY_DISABLE_CPU_FEATURES's) choice. Their results are the baseline kernels' (no fused
// multiply-adds: -ffp-contract=off, or MSVC's /fp:precise).

#pragma once

#include <cstdint>
#include <optional>
#include <type_traits>

namespace neos::numpy::simd
{
    enum class op : std::uint8_t
    {
        None, Add, Subtract, Multiply, TrueDivide, Maximum, Minimum, Fmax, Fmin
    };

    // (the kernels' types)
    enum class type : std::uint8_t
    {
        Int8, UInt8, Int16, UInt16, Int32, UInt32, Int64, UInt64, Float32, Float64
    };

    // (the kernel type of a C++ type)
    template <typename T>
    constexpr std::optional<type> type_of()
    {
        if constexpr (std::is_same_v<T, std::int8_t>) return type::Int8;
        else if constexpr (std::is_same_v<T, std::uint8_t>) return type::UInt8;
        else if constexpr (std::is_same_v<T, std::int16_t>) return type::Int16;
        else if constexpr (std::is_same_v<T, std::uint16_t>) return type::UInt16;
        else if constexpr (std::is_same_v<T, std::int32_t>) return type::Int32;
        else if constexpr (std::is_same_v<T, std::uint32_t>) return type::UInt32;
        else if constexpr (std::is_same_v<T, std::int64_t>) return type::Int64;
        else if constexpr (std::is_same_v<T, std::uint64_t>) return type::UInt64;
        else if constexpr (std::is_same_v<T, float>) return type::Float32;
        else if constexpr (std::is_same_v<T, double>) return type::Float64;
        else return {};
    }

    // the loop of an operation whose operands and result are of one type (typed_binary_loop) in the dispatched
    // instruction set; false: none (the baseline loop is used)
    bool binary(op aOp, type aType, std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides);
    // the matrix product kernel (matmul_kernel) in the dispatched instruction set; false: none
    bool matmul(type aType, std::int64_t m, std::int64_t n, std::int64_t k, unsigned char const* pa, std::int64_t as0, std::int64_t as1,
        unsigned char const* pb, std::int64_t bs0, std::int64_t bs1, unsigned char* pr, std::int64_t rs0, std::int64_t rs1);

    namespace avx2 // (X86_V3: simd.avx2.cpp)
    {
        bool binary(op aOp, type aType, std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides);
        bool matmul(type aType, std::int64_t m, std::int64_t n, std::int64_t k, unsigned char const* pa, std::int64_t as0, std::int64_t as1,
            unsigned char const* pb, std::int64_t bs0, std::int64_t bs1, unsigned char* pr, std::int64_t rs0, std::int64_t rs1);
    }
    namespace avx512 // (X86_V4: simd.avx512.cpp)
    {
        bool binary(op aOp, type aType, std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides);
        bool matmul(type aType, std::int64_t m, std::int64_t n, std::int64_t k, unsigned char const* pa, std::int64_t as0, std::int64_t as1,
            unsigned char const* pb, std::int64_t bs0, std::int64_t bs1, unsigned char* pr, std::int64_t rs0, std::int64_t rs1);
    }
}
