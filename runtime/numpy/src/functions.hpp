/*
  functions.hpp

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
#include "ufunc.hpp"

namespace neos::numpy
{
    // (helpers of the module's functions)
    ref array_ref(object* aObject, std::optional<dtype> aType);    // (an array: a new reference, or null)
    ndarray* array_of(ref const& aRef);
    ref new_ref(ndarray* aArray);
    bool optional_dtype(arguments const& aArgs, std::size_t aIndex, std::optional<dtype>& aResult);
    bool optional_bool(arguments const& aArgs, std::size_t aIndex, bool aDefault, bool& aResult);
    bool optional_axis(arguments const& aArgs, std::size_t aIndex, std::size_t aNdim, std::optional<std::size_t>& aResult);
    void for_each(ndarray const& aArray, std::function<void(unsigned char*)> const& aFunction);
    ndarray* view_of(ndarray* aArray, shape_t aShape, shape_t aStrides, std::size_t aOffset);
    ndarray* contiguous(ndarray* aArray);
    std::optional<dtype> result_type(std::vector<object*> const& aObjects);
    bool resolve_shape(shape_t& aShape, std::int64_t aSize);
    ndarray* reshape(ndarray* aArray, shape_t aShape, bool aFortran = false);
    ndarray* transpose(ndarray* aArray, std::vector<std::size_t> const& aAxes);
    ndarray* ravel(ndarray* aArray, bool aFortran = false);
    ndarray* concatenate(std::vector<ndarray*> const& aArrays, std::optional<std::size_t> aAxis, std::optional<dtype> aType);
    ndarray* expand_dims(ndarray* aArray, std::size_t aAxis);
    ndarray* atleast(ndarray* aArray, std::size_t aNdim);
    bool array_sequence(object* aObject, std::vector<ref>& aResult);
    std::vector<ndarray*> pointers(std::vector<ref> const& aRefs);
    object* call_ufunc(ufunc const& aUfunc, object* aArgs, object* aKwargs);
    void register_functions(std::vector<std::pair<char const*, object* (*)(object*, object*)>> const& aFunctions);
    bool has_function(std::string_view aName);
    object* find_class(std::string_view aName);
}
