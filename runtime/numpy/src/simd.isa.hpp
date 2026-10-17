/*
  simd.isa.hpp

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

// The kernels of an instruction set (NEOS_NUMPY_ISA): included by its translation unit (simd.avx2.cpp, simd.avx512.cpp)

#include "kernels.hpp"
#include "simd.hpp"

namespace neos::numpy::simd::NEOS_NUMPY_ISA
{
    namespace
    {
        namespace kernels = neos::numpy::NEOS_NUMPY_ISA;

        template <typename Op, bool Integers = true>
        bool binary_of(type aType, std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
        {
            switch (aType)
            {
            case type::Float32: kernels::typed_binary_loop<Op, float, float>(aCount, aPointers, aStrides); return true;
            case type::Float64: kernels::typed_binary_loop<Op, double, double>(aCount, aPointers, aStrides); return true;
            default:
                break;
            }
            if constexpr (Integers)
                switch (aType)
                {
                case type::Int8: kernels::typed_binary_loop<Op, std::int8_t, std::int8_t>(aCount, aPointers, aStrides); return true;
                case type::UInt8: kernels::typed_binary_loop<Op, std::uint8_t, std::uint8_t>(aCount, aPointers, aStrides); return true;
                case type::Int16: kernels::typed_binary_loop<Op, std::int16_t, std::int16_t>(aCount, aPointers, aStrides); return true;
                case type::UInt16: kernels::typed_binary_loop<Op, std::uint16_t, std::uint16_t>(aCount, aPointers, aStrides); return true;
                case type::Int32: kernels::typed_binary_loop<Op, std::int32_t, std::int32_t>(aCount, aPointers, aStrides); return true;
                case type::UInt32: kernels::typed_binary_loop<Op, std::uint32_t, std::uint32_t>(aCount, aPointers, aStrides); return true;
                case type::Int64: kernels::typed_binary_loop<Op, std::int64_t, std::int64_t>(aCount, aPointers, aStrides); return true;
                case type::UInt64: kernels::typed_binary_loop<Op, std::uint64_t, std::uint64_t>(aCount, aPointers, aStrides); return true;
                default:
                    break;
                }
            return false;
        }
    }

    bool binary(op aOp, type aType, std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
    {
        switch (aOp)
        {
        case op::Add: return binary_of<kernels::kernel_ops::add>(aType, aCount, aPointers, aStrides);
        case op::Subtract: return binary_of<kernels::kernel_ops::subtract>(aType, aCount, aPointers, aStrides);
        case op::Multiply: return binary_of<kernels::kernel_ops::multiply>(aType, aCount, aPointers, aStrides);
        case op::TrueDivide: return binary_of<kernels::kernel_ops::true_divide, false>(aType, aCount, aPointers, aStrides);
        case op::Maximum: return binary_of<kernels::kernel_ops::maximum>(aType, aCount, aPointers, aStrides);
        case op::Minimum: return binary_of<kernels::kernel_ops::minimum>(aType, aCount, aPointers, aStrides);
        case op::Fmax: return binary_of<kernels::kernel_ops::fmax>(aType, aCount, aPointers, aStrides);
        case op::Fmin: return binary_of<kernels::kernel_ops::fmin>(aType, aCount, aPointers, aStrides);
        default:
            return false;
        }
    }

    bool matmul(type aType, std::int64_t m, std::int64_t n, std::int64_t k, unsigned char const* pa, std::int64_t as0, std::int64_t as1,
        unsigned char const* pb, std::int64_t bs0, std::int64_t bs1, unsigned char* pr, std::int64_t rs0, std::int64_t rs1)
    {
        switch (aType)
        {
        case type::Float32: kernels::matmul_kernel<float>(m, n, k, pa, as0, as1, pb, bs0, bs1, pr, rs0, rs1); return true;
        case type::Float64: kernels::matmul_kernel<double>(m, n, k, pa, as0, as1, pb, bs0, bs1, pr, rs0, rs1); return true;
        case type::Int32: kernels::matmul_kernel<std::int32_t>(m, n, k, pa, as0, as1, pb, bs0, bs1, pr, rs0, rs1); return true;
        case type::Int64: kernels::matmul_kernel<std::int64_t>(m, n, k, pa, as0, as1, pb, bs0, bs1, pr, rs0, rs1); return true;
        default:
            return false;
        }
    }
}
