/*
  xss.hpp

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
    void qselect(T* aArray, std::size_t aKth, std::size_t aSize);

#define NEOS_XSS_DECLARE_HALF() \
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
    inline void qselect(T*, std::size_t, std::size_t) {}

#define NEOS_XSS_DECLARE_HALF() \
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
        NEOS_XSS_DECLARE_HALF()
    }
}
