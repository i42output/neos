/*
  functions.hpp

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
