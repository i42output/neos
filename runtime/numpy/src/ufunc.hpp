/*
  ufunc.hpp

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

#include "numpy.hpp"

namespace neos::numpy
{
    // (how a ufunc's loop and output types follow from its inputs' types)
    enum class category
    {
        Arithmetic,     // common type
        TrueDivide,     // integers: float64
        IntegerDivide,  // bool: int8; no complex
        Power,          // bool: int8
        FloatPower,     // float64 or complex128
        Comparison,     // -> bool
        Logical,        // -> bool
        Bitwise,        // bool and integers
        Shift,          // integers (bool: int8)
        IntegerOnly,    // integers (bool: int8)
        FloatBinary,    // integers: the smallest float that holds them; no complex
        FloatUnary,     // (unary) as FloatBinary
        InexactUnary,   // integers: the smallest float that holds them
        Negative,       // no bool
        SameNoBool,
        SameBoolInt8,
        Absolute,       // complex: real
        Predicate,      // -> bool
        RealPredicate,  // -> bool; no complex
        Rounding,       // no complex
        Invert,         // bool and integers
        Same
    };

    using binary_kernel = void (*)(dtype aLoop, dtype aOut, dtype aIn0, dtype aIn1, std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides);
    using unary_kernel = void (*)(dtype aLoop, dtype aOut, dtype aIn0, std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides);
    using supports_function = bool (*)(dtype aLoop);

    struct ufunc
    {
        char const* name;
        int nin;
        category kind;
        std::optional<double> identity;
        binary_kernel binary;
        supports_function binarySupports;
        unary_kernel unary;
        supports_function unarySupports;
    };

    std::vector<ufunc const*> all_ufuncs();
    bool resolve(ufunc const& aUfunc, std::vector<dtype> const& aInputs, dtype& aLoop, dtype& aOut);
    object* reduce_array(ufunc const& aUfunc, ndarray* aArray, std::vector<std::size_t> const& aAxes, std::optional<dtype> aType, bool aKeepdims,
        object* aInitial, ndarray* aOut);
    object* accumulate_array(ufunc const& aUfunc, ndarray* aArray, std::size_t aAxis, std::optional<dtype> aType);
}
