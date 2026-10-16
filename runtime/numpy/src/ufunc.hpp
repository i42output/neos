/*
  ufunc.hpp

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
