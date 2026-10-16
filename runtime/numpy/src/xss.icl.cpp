/*
  xss.icl.cpp

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
