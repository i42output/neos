/*
  xss.hpp

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

// x86-simd-sort (3rdparty/x86-simd-sort): the SIMD sorts and selections that NumPy uses on x86 CPUs, compiled for each
// of the instruction sets NumPy compiles them for (xss.avx512.cpp: X86_V4, xss.avx2.cpp: X86_V3, xss.icl.cpp: AVX512_ICL)

#pragma once

#include <cstddef>
#include <cstdint>

#if !defined(NEOS_NUMPY_NO_XSS)

#define NEOS_XSS_DECLARE(ISA, T) \
    void qsort(T* aArray, std::size_t aSize); \
    void qselect(T* aArray, std::size_t aKth, std::size_t aSize); \
    void argsort(T const* aArray, std::size_t* aIndices, std::size_t aSize); \
    void argselect(T const* aArray, std::size_t* aIndices, std::size_t aKth, std::size_t aSize);

#define NEOS_XSS_DECLARE_16(T) \
    void qsort(T* aArray, std::size_t aSize); \
    void qselect(T* aArray, std::size_t aKth, std::size_t aSize); \
    void qsort_half(std::uint16_t* aArray, std::size_t aSize); \
    void qselect_half(std::uint16_t* aArray, std::size_t aKth, std::size_t aSize);

#else

// (built without x86-simd-sort (not x86, or not GCC or Clang): the CPU's features are not detected so these are never called)
#define NEOS_XSS_DECLARE(ISA, T) \
    inline void qsort(T*, std::size_t) {} \
    inline void qselect(T*, std::size_t, std::size_t) {} \
    inline void argsort(T const*, std::size_t*, std::size_t) {} \
    inline void argselect(T const*, std::size_t*, std::size_t, std::size_t) {}

#define NEOS_XSS_DECLARE_16(T) \
    inline void qsort(T*, std::size_t) {} \
    inline void qselect(T*, std::size_t, std::size_t) {} \
    inline void qsort_half(std::uint16_t*, std::size_t) {} \
    inline void qselect_half(std::uint16_t*, std::size_t, std::size_t) {}

#endif

namespace neos::numpy::xss
{
    namespace avx512
    {
        NEOS_XSS_DECLARE(avx512, std::int32_t)
        NEOS_XSS_DECLARE(avx512, std::uint32_t)
        NEOS_XSS_DECLARE(avx512, float)
        NEOS_XSS_DECLARE(avx512, std::int64_t)
        NEOS_XSS_DECLARE(avx512, std::uint64_t)
        NEOS_XSS_DECLARE(avx512, double)
    }
    namespace avx2
    {
        NEOS_XSS_DECLARE(avx2, std::int32_t)
        NEOS_XSS_DECLARE(avx2, std::uint32_t)
        NEOS_XSS_DECLARE(avx2, float)
        NEOS_XSS_DECLARE(avx2, std::int64_t)
        NEOS_XSS_DECLARE(avx2, std::uint64_t)
        NEOS_XSS_DECLARE(avx2, double)
    }
    namespace icl
    {
        NEOS_XSS_DECLARE_16(std::int16_t)
        NEOS_XSS_DECLARE_16(std::uint16_t)
    }
}
