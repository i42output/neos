/*
  kernels.hpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.

     * Neither the name of the copyright holder nor the names of its
       contributors may be used to endorse or promote products derived from
       this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// The element loops whose operands and result are of one C++ type (no conversions), the reductions and the matrix
// product kernel: compiled for the baseline instruction set (NEOS_NUMPY_ISA baseline) and, in their own translation
// units, for the instruction sets the CPU is dispatched to (simd.hpp); each in its own namespace, so that no inline
// function compiled for one instruction set is shared with another. Only operators and std::memcpy are used here:
// no function of a header (that would be shared) is called.

#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

#ifndef NEOS_NUMPY_ISA
#define NEOS_NUMPY_ISA baseline
#endif

namespace neos::numpy::NEOS_NUMPY_ISA
{
    // --- the operations on real (integer and floating point) values that have kernels

    namespace kernel_ops
    {
        struct add { template <typename T> static T apply(T a, T b) { return static_cast<T>(a + b); } };
        struct subtract { template <typename T> static T apply(T a, T b) { return static_cast<T>(a - b); } };
        struct multiply { template <typename T> static T apply(T a, T b) { return static_cast<T>(a * b); } };
        struct true_divide { template <typename T> static T apply(T a, T b) { return a / b; } };
        // (as NumPy (x86 maxss/minss): a NaN: a; b NaN or equal: b)
        struct maximum
        {
            static constexpr int extremum = 1;
            template <typename T> static T apply(T a, T b) { if constexpr (std::is_floating_point_v<T>) return a > b || a != a ? a : b; else return a >= b ? a : b; }
        };
        struct minimum
        {
            static constexpr int extremum = -1;
            template <typename T> static T apply(T a, T b) { if constexpr (std::is_floating_point_v<T>) return a < b || a != a ? a : b; else return a <= b ? a : b; }
        };
        // (b NaN: a; a NaN: b)
        struct fmax
        {
            static constexpr int extremum = 1;
            template <typename T> static T apply(T a, T b) { if constexpr (std::is_floating_point_v<T>) return a >= b || b != b ? a : b; else return a >= b ? a : b; }
        };
        struct fmin
        {
            static constexpr int extremum = -1;
            template <typename T> static T apply(T a, T b) { if constexpr (std::is_floating_point_v<T>) return a <= b || b != b ? a : b; else return a <= b ? a : b; }
        };
    }

    // (whether a reduction by an operation is by a maximum (1) or minimum (-1))
    template <typename Op>
    constexpr int extremum_of()
    {
        if constexpr (requires { Op::extremum; })
            return Op::extremum;
        else
            return 0;
    }

    // (contiguous: unit strides and aligned elements)
    template <typename T>
    inline bool unit(unsigned char const* aPointer, std::int64_t aStride)
    {
        return aStride == static_cast<std::int64_t>(sizeof(T)) && reinterpret_cast<std::uintptr_t>(aPointer) % alignof(T) == 0u;
    }

    // a reduction (the accumulator is the first operand and the result): in order, the accumulator in a register
    template <typename Op, typename T>
    void typed_reduction(T& aAccumulator, unsigned char const* aPointer, std::int64_t aStride, std::int64_t aCount)
    {
        if constexpr (std::is_floating_point_v<T> && extremum_of<Op>() != 0)
        {
            // (in any order (lanes) when there is no NaN and the extremum is not zero: the same value as in order)
            if (unit<T>(aPointer, aStride) && aCount >= 16)
            {
                constexpr bool maximum = extremum_of<Op>() > 0;
                auto const* const b = reinterpret_cast<T const*>(aPointer);
                T lanes[8];
                bool nan[8];
                for (int j = 0; j < 8; ++j)
                {
                    lanes[j] = aAccumulator;
                    nan[j] = aAccumulator != aAccumulator;
                }
                std::int64_t i = 0;
                for (; i + 8 <= aCount; i += 8)
                    for (int j = 0; j < 8; ++j)
                    {
                        T const x = b[i + j];
                        nan[j] |= x != x;
                        if constexpr (maximum)
                            lanes[j] = x > lanes[j] ? x : lanes[j];
                        else
                            lanes[j] = x < lanes[j] ? x : lanes[j];
                    }
                for (; i < aCount; ++i)
                {
                    T const x = b[i];
                    nan[0] |= x != x;
                    if constexpr (maximum)
                        lanes[0] = x > lanes[0] ? x : lanes[0];
                    else
                        lanes[0] = x < lanes[0] ? x : lanes[0];
                }
                bool anyNan = false;
                T result = lanes[0];
                for (int j = 0; j < 8; ++j)
                {
                    anyNan = anyNan || nan[j];
                    if constexpr (maximum)
                        result = lanes[j] > result ? lanes[j] : result;
                    else
                        result = lanes[j] < result ? lanes[j] : result;
                }
                if (!anyNan && result != T(0))
                {
                    aAccumulator = result;
                    return;
                }
            }
        }
        T accumulator = aAccumulator;
        if (unit<T>(aPointer, aStride))
        {
            auto const* const b = reinterpret_cast<T const*>(aPointer);
            for (std::int64_t i = 0; i < aCount; ++i)
                accumulator = Op::apply(accumulator, b[i]);
        }
        else
            for (std::int64_t i = 0; i < aCount; ++i, aPointer += aStride)
            {
                T b;
                std::memcpy(&b, aPointer, sizeof(T));
                accumulator = Op::apply(accumulator, b);
            }
        aAccumulator = accumulator;
    }

    // the loop of an operation whose operands and result are the loop's type (no conversions): contiguous arrays (and
    // scalars) are indexed (vectorised)
    template <typename Op, typename T, typename R>
    void typed_binary_loop(std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
    {
        auto p0 = aPointers[0];
        auto p1 = aPointers[1];
        auto p2 = aPointers[2];
        if constexpr (std::is_same_v<T, R>)
            if (aStrides[0] == 0 && aStrides[2] == 0 && p0 == p2)
            {
                T accumulator;
                std::memcpy(&accumulator, p0, sizeof(T));
                typed_reduction<Op>(accumulator, p1, aStrides[1], aCount);
                std::memcpy(p2, &accumulator, sizeof(T));
                return;
            }
        if constexpr (std::is_same_v<T, R>)
            if (p0 == p2 && unit<T>(p0, aStrides[0]) && aStrides[2] == aStrides[0] && unit<T>(p1, aStrides[1]))
            {
                // (in place: the result is the first operand)
                auto* const r = reinterpret_cast<T*>(p2);
                auto const* const b = reinterpret_cast<T const*>(p1);
                for (std::int64_t i = 0; i < aCount; ++i)
                    r[i] = Op::apply(r[i], b[i]);
                return;
            }
        if (unit<T>(p0, aStrides[0]) && unit<T>(p1, aStrides[1]) && unit<R>(p2, aStrides[2]))
        {
            auto const* const a = reinterpret_cast<T const*>(p0);
            auto const* const b = reinterpret_cast<T const*>(p1);
            auto* const r = reinterpret_cast<R*>(p2);
            for (std::int64_t i = 0; i < aCount; ++i)
                r[i] = Op::apply(a[i], b[i]);
            return;
        }
        // (an array and a scalar)
        if (unit<T>(p0, aStrides[0]) && aStrides[1] == 0 && unit<R>(p2, aStrides[2]))
        {
            auto const* const a = reinterpret_cast<T const*>(p0);
            T b;
            std::memcpy(&b, p1, sizeof(T));
            auto* const r = reinterpret_cast<R*>(p2);
            for (std::int64_t i = 0; i < aCount; ++i)
                r[i] = Op::apply(a[i], b);
            return;
        }
        if (aStrides[0] == 0 && unit<T>(p1, aStrides[1]) && unit<R>(p2, aStrides[2]))
        {
            T a;
            std::memcpy(&a, p0, sizeof(T));
            auto const* const b = reinterpret_cast<T const*>(p1);
            auto* const r = reinterpret_cast<R*>(p2);
            for (std::int64_t i = 0; i < aCount; ++i)
                r[i] = Op::apply(a, b[i]);
            return;
        }
        for (std::int64_t i = 0; i < aCount; ++i, p0 += aStrides[0], p1 += aStrides[1], p2 += aStrides[2])
        {
            T a, b;
            std::memcpy(&a, p0, sizeof(T));
            std::memcpy(&b, p1, sizeof(T));
            R const r = Op::apply(a, b);
            std::memcpy(p2, &r, sizeof(R));
        }
    }

    // the matrix product of an m by k matrix and a k by n matrix (strides in bytes) into the result, of operands of the
    // result's type (integers: their sums wrap; their order does not change them): blocked
    template <typename T>
    void matmul_kernel(std::int64_t m, std::int64_t n, std::int64_t k, unsigned char const* pa, std::int64_t as0, std::int64_t as1,
        unsigned char const* pb, std::int64_t bs0, std::int64_t bs1, unsigned char* pr, std::int64_t rs0, std::int64_t rs1)
    {
        std::int64_t constexpr kBlock = 128;
        std::int64_t constexpr nBlock = 512;
        for (std::int64_t i = 0; i < m; ++i)
            for (std::int64_t j = 0; j < n; ++j)
            {
                T const zero{};
                std::memcpy(pr + i * rs0 + j * rs1, &zero, sizeof(T));
            }
        bool const unitB = bs1 == static_cast<std::int64_t>(sizeof(T)) && reinterpret_cast<std::uintptr_t>(pb) % alignof(T) == 0u &&
            bs0 % static_cast<std::int64_t>(alignof(T)) == 0;
        bool const unitR = rs1 == static_cast<std::int64_t>(sizeof(T)) && reinterpret_cast<std::uintptr_t>(pr) % alignof(T) == 0u &&
            rs0 % static_cast<std::int64_t>(alignof(T)) == 0;
        for (std::int64_t l0 = 0; l0 < k; l0 += kBlock)
            for (std::int64_t j0 = 0; j0 < n; j0 += nBlock)
            {
                auto const l1 = k < l0 + kBlock ? k : l0 + kBlock;
                auto const j1 = n < j0 + nBlock ? n : j0 + nBlock;
                // (the products of row i for the columns from jFirst)
                auto const row = [&](std::int64_t i, std::int64_t jFirst)
                {
                    for (std::int64_t l = l0; l < l1; ++l)
                    {
                        T x;
                        std::memcpy(&x, pa + i * as0 + l * as1, sizeof(T));
                        auto const* const rowB = pb + l * bs0;
                        auto* const rowR = pr + i * rs0;
                        if (unitB && unitR)
                        {
                            auto const* const y = reinterpret_cast<T const*>(rowB);
                            auto* const sum = reinterpret_cast<T*>(rowR);
                            for (std::int64_t j = jFirst; j < j1; ++j)
                                sum[j] = static_cast<T>(sum[j] + x * y[j]);
                        }
                        else
                            for (std::int64_t j = jFirst; j < j1; ++j)
                            {
                                T y, sum;
                                std::memcpy(&y, rowB + j * bs1, sizeof(T));
                                std::memcpy(&sum, rowR + j * rs1, sizeof(T));
                                sum = static_cast<T>(sum + x * y);
                                std::memcpy(rowR + j * rs1, &sum, sizeof(T));
                            }
                    }
                };
                std::int64_t i = 0;
                if (unitB && unitR)
                {
                    // (4 rows by 8 columns: their sums in registers)
                    for (; i + 4 <= m; i += 4)
                    {
                        std::int64_t j = j0;
                        for (; j + 8 <= j1; j += 8)
                        {
                            T sum[4][8];
                            for (int u = 0; u < 4; ++u)
                                for (int v = 0; v < 8; ++v)
                                    sum[u][v] = reinterpret_cast<T const*>(pr + (i + u) * rs0)[j + v];
                            for (std::int64_t l = l0; l < l1; ++l)
                            {
                                auto const* const y = reinterpret_cast<T const*>(pb + l * bs0) + j;
                                for (int u = 0; u < 4; ++u)
                                {
                                    T x;
                                    std::memcpy(&x, pa + (i + u) * as0 + l * as1, sizeof(T));
                                    for (int v = 0; v < 8; ++v)
                                        sum[u][v] = static_cast<T>(sum[u][v] + x * y[v]);
                                }
                            }
                            for (int u = 0; u < 4; ++u)
                                for (int v = 0; v < 8; ++v)
                                    reinterpret_cast<T*>(pr + (i + u) * rs0)[j + v] = sum[u][v];
                        }
                        if (j < j1)
                            for (int u = 0; u < 4; ++u)
                                row(i + u, j);
                    }
                }
                for (; i < m; ++i)
                    row(i, j0);
            }
    }
}
