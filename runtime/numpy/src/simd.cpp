/*
  simd.cpp

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

// The dispatch of the kernels to the CPU's instruction set (as NumPy's: X86_V4, X86_V3; NPY_DISABLE_CPU_FEATURES)

#include <cstdlib>
#include <string>
#include "simd.hpp"

#if !defined(NEOS_NUMPY_NO_SIMD) && (defined(__x86_64__) || defined(_M_X64))
#define NEOS_NUMPY_SIMD_X86
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

namespace neos::numpy::simd
{
    namespace
    {
#ifdef NEOS_NUMPY_SIMD_X86
        void cpuid(unsigned aLeaf, unsigned aSubleaf, unsigned (&aRegisters)[4])
        {
#ifdef _MSC_VER
            int r[4];
            __cpuidex(r, static_cast<int>(aLeaf), static_cast<int>(aSubleaf));
            for (int i = 0; i < 4; ++i)
                aRegisters[i] = static_cast<unsigned>(r[i]);
#else
            __cpuid_count(aLeaf, aSubleaf, aRegisters[0], aRegisters[1], aRegisters[2], aRegisters[3]);
#endif
        }

        unsigned long long xgetbv0()
        {
#ifdef _MSC_VER
            return _xgetbv(0);
#else
            unsigned eax, edx;
            __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
            return (static_cast<unsigned long long>(edx) << 32) | eax;
#endif
        }
#endif

        // (4: X86_V4, 3: X86_V3, 0: neither (the baseline))
        int level()
        {
            static int const sLevel = []()
            {
                int result = 0;
#ifdef NEOS_NUMPY_SIMD_X86
                unsigned r0[4], r1[4], r7[4] = {}, e1[4] = {};
                cpuid(0u, 0u, r0);
                cpuid(1u, 0u, r1);
                if (r0[0] >= 7u)
                    cpuid(7u, 0u, r7);
                cpuid(0x80000000u, 0u, e1);
                if (e1[0] >= 0x80000001u)
                    cpuid(0x80000001u, 0u, e1);
                else
                    e1[2] = 0u;
                auto const bit = [](unsigned aRegister, int aBit) { return (aRegister & (1u << aBit)) != 0u; };
                bool const osxsave = bit(r1[2], 27);
                auto const xcr0 = osxsave ? xgetbv0() : 0ull;
                bool const avxState = (xcr0 & 0x6ull) == 0x6ull;
                bool const avx512State = (xcr0 & 0xE6ull) == 0xE6ull;
                bool const v3 = avxState && bit(r1[2], 28) /* AVX */ && bit(r1[2], 12) /* FMA */ && bit(r1[2], 29) /* F16C */ &&
                    bit(r1[2], 22) /* MOVBE */ && bit(r7[1], 5) /* AVX2 */ && bit(r7[1], 3) /* BMI1 */ && bit(r7[1], 8) /* BMI2 */ &&
                    bit(e1[2], 5) /* LZCNT */;
                bool const v4 = v3 && avx512State && bit(r7[1], 16) /* F */ && bit(r7[1], 17) /* DQ */ && bit(r7[1], 28) /* CD */ &&
                    bit(r7[1], 30) /* BW */ && bit(r7[1], 31) /* VL */;
                result = v4 ? 4 : v3 ? 3 : 0;
                // (NPY_DISABLE_CPU_FEATURES, as NumPy)
                if (auto const disabled = std::getenv("NPY_DISABLE_CPU_FEATURES"))
                {
                    std::string const d = disabled;
                    if (result == 4 && (d.find("X86_V4") != std::string::npos || d.find("AVX512") != std::string::npos))
                        result = 3;
                    if (d.find("X86_V3") != std::string::npos || d.find("AVX2") != std::string::npos)
                        result = 0;
                }
#endif
                return result;
            }();
            return sLevel;
        }
    }

    bool binary(op aOp, type aType, std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
    {
#ifdef NEOS_NUMPY_SIMD_X86
        switch (level())
        {
        case 4: return avx512::binary(aOp, aType, aCount, aPointers, aStrides);
        case 3: return avx2::binary(aOp, aType, aCount, aPointers, aStrides);
        default: break;
        }
#endif
        (void)aOp; (void)aType; (void)aCount; (void)aPointers; (void)aStrides;
        return false;
    }

    bool matmul(type aType, std::int64_t m, std::int64_t n, std::int64_t k, unsigned char const* pa, std::int64_t as0, std::int64_t as1,
        unsigned char const* pb, std::int64_t bs0, std::int64_t bs1, unsigned char* pr, std::int64_t rs0, std::int64_t rs1)
    {
#ifdef NEOS_NUMPY_SIMD_X86
        switch (level())
        {
        case 4: return avx512::matmul(aType, m, n, k, pa, as0, as1, pb, bs0, bs1, pr, rs0, rs1);
        case 3: return avx2::matmul(aType, m, n, k, pa, as0, as1, pb, bs0, bs1, pr, rs0, rs1);
        default: break;
        }
#endif
        (void)aType; (void)m; (void)n; (void)k; (void)pa; (void)as0; (void)as1; (void)pb; (void)bs0; (void)bs1; (void)pr; (void)rs0; (void)rs1;
        return false;
    }
}
