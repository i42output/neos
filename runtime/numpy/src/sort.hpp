/*
  sort.hpp

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

#pragma once

#include <vector>
#include "numpy.hpp"

namespace neos::numpy
{
    enum class sort_kind
    {
        Quick,  // (the default (and heapsort): introsort, or the CPU's SIMD sort)
        Stable  // (stable and mergesort)
    };

    // (the elements of a contiguous lane)
    void sort_contiguous(dtype aType, unsigned char* aData, std::int64_t aCount, sort_kind aKind);
    void argsort_contiguous(dtype aType, unsigned char const* aData, std::int64_t* aIndices, std::int64_t aCount, sort_kind aKind);
    void partition_contiguous(dtype aType, unsigned char* aData, std::int64_t aCount, std::vector<std::int64_t> const& aKth);    // (aKth: sorted)
    void argpartition_contiguous(dtype aType, unsigned char* aData, std::int64_t* aIndices, std::int64_t aCount, std::vector<std::int64_t> const& aKth);
}
