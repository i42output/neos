/*
  xss.avx2.cpp

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

// (compiled for X86_V3 (AVX2, FMA, F16C, BMI): as NumPy's x86_simd_qsort.dispatch.cpp and x86_simd_argsort.dispatch.cpp)

#include <x86simdsort-static-incl.h>
#include "xss.hpp"

#define NEOS_XSS_DEFINE(T) \
    void qsort(T* aArray, std::size_t aSize) { x86simdsortStatic::qsort(aArray, aSize, true); } \
    void qselect(T* aArray, std::size_t aKth, std::size_t aSize) { x86simdsortStatic::qselect(aArray, aKth, aSize, true); } \
    void argsort(T const* aArray, std::size_t* aIndices, std::size_t aSize) { x86simdsortStatic::argsort(aArray, aIndices, aSize, true); } \
    void argselect(T const* aArray, std::size_t* aIndices, std::size_t aKth, std::size_t aSize) { x86simdsortStatic::argselect(aArray, aIndices, aKth, aSize, true); }

namespace neos::numpy::xss::avx2
{
    NEOS_XSS_DEFINE(std::int32_t)
    NEOS_XSS_DEFINE(std::uint32_t)
    NEOS_XSS_DEFINE(float)
    NEOS_XSS_DEFINE(std::int64_t)
    NEOS_XSS_DEFINE(std::uint64_t)
    NEOS_XSS_DEFINE(double)
}
