/*
  sort.hpp

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
