/*
  xss.icl.cpp

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

  Portions of this file are derived from NumPy 2.5.3 (numpy/_core/src/npysort/x86_simd_qsort_16bit.dispatch.cpp),
  Copyright (c) 2005-2025, NumPy Developers; All rights reserved. They are used under NumPy's BSD 3-Clause licence (see
  runtime/numpy/LICENSE.NumPy). Changes: translated from C to C++ for the neos NumPy runtime plugin and adapted to its
  object model (see runtime/numpy/NOTICE).
*/

// (compiled for AVX512_ICL: as NumPy's x86_simd_qsort_16bit.dispatch.cpp)

#include <x86simdsort-static-incl.h>
#ifdef _MSC_VER
#include <avx512-16bit-qsort.hpp>
#endif
#include "xss.hpp"

namespace neos::numpy::xss::icl
{
    void qsort(std::int16_t* aArray, std::size_t aSize) { x86simdsortStatic::qsort(aArray, aSize); }
    void qselect(std::int16_t* aArray, std::size_t aKth, std::size_t aSize) { x86simdsortStatic::qselect(aArray, aKth, aSize); }
    void qsort(std::uint16_t* aArray, std::size_t aSize) { x86simdsortStatic::qsort(aArray, aSize); }
    void qselect(std::uint16_t* aArray, std::size_t aKth, std::size_t aSize) { x86simdsortStatic::qselect(aArray, aKth, aSize); }
    void qsort_half(std::uint16_t* aArray, std::size_t aSize) { avx512_qsort_fp16(aArray, aSize, true, false); }
    void qselect_half(std::uint16_t* aArray, std::size_t aKth, std::size_t aSize) { avx512_qselect_fp16(aArray, aKth, aSize, true, false); }
}
