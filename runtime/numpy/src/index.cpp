/*
  index.cpp

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

  Portions of this file are derived from CPython 3.13 (Objects/sliceobject.c: PySlice_AdjustIndices), Copyright (c)
  2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License
  Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos NumPy runtime plugin (see
  runtime/numpy/NOTICE).
*/

// indexing: basic (integers, slices, np.newaxis, Ellipsis: views) and advanced (integer and boolean arrays: copies)

#include <algorithm>
#include "numpy.hpp"
#include "cast.hpp"

namespace neos::numpy
{
    namespace
    {
        struct index_item
        {
            enum class kind_t { Integer, Slice, NewAxis, Ellipsis, Array } kind;
            std::int64_t value = 0;
            object* slice = nullptr;
            ref array;      // (Array: an integer array; a boolean array is converted to integer arrays)
            bool boolean = false;
        };

        bool slice_indices(object* aSlice, std::int64_t aLength, std::int64_t& aStart, std::int64_t& aStop, std::int64_t& aStep, std::int64_t& aCount)
        {
            object* start = nullptr;
            object* stop = nullptr;
            object* step = nullptr;
            ctx->slice_parts(ctx, aSlice, &start, &stop, &step);
            aStep = 1;
            if (kind(step) != NEOS_PY_NONE && !to_int(step, aStep))
                return false;
            if (aStep == 0)
            {
                error("ValueError", "slice step cannot be zero");
                return false;
            }
            auto const clamp = [&](object* aPart, std::int64_t aDefault, std::int64_t& aResult) -> bool
            {
                if (kind(aPart) == NEOS_PY_NONE)
                {
                    aResult = aDefault;
                    return true;
                }
                if (!to_int(aPart, aResult))
                    return false;
                if (aResult < 0)
                {
                    aResult += aLength;
                    if (aResult < 0)
                        aResult = aStep < 0 ? -1 : 0;
                }
                else if (aResult >= aLength)
                    aResult = aStep < 0 ? aLength - 1 : aLength;
                return true;
            };
            if (!clamp(start, aStep < 0 ? aLength - 1 : 0, aStart) || !clamp(stop, aStep < 0 ? -1 : aLength, aStop))
                return false;
            if (aStep < 0)
                aCount = aStop < aStart ? (aStart - aStop - 1) / (-aStep) + 1 : 0;
            else
                aCount = aStart < aStop ? (aStop - aStart - 1) / aStep + 1 : 0;
            return true;
        }

        bool is_integer_object(object* aObject)
        {
            auto const k = kind(aObject);
            if (k == NEOS_PY_INT)
                return true;
            if (is_array(aObject))
            {
                auto const* a = as_array(aObject);
                return a->ndim() == 0 && is_integer(a->type);
            }
            return false;
        }

        bool parse_index(object* aKey, std::vector<index_item>& aItems)
        {
            std::vector<object*> keys;
            if (kind(aKey) == NEOS_PY_TUPLE)
                keys = sequence_items(aKey);
            else
                keys.push_back(aKey);
            for (auto key : keys)
            {
                index_item item;
                auto const k = kind(key);
                if (k == NEOS_PY_NONE)
                    item.kind = index_item::kind_t::NewAxis;
                else if (k == NEOS_PY_ELLIPSIS)
                    item.kind = index_item::kind_t::Ellipsis;
                else if (k == NEOS_PY_SLICE)
                {
                    item.kind = index_item::kind_t::Slice;
                    item.slice = key;
                }
                else if (is_integer_object(key))
                {
                    item.kind = index_item::kind_t::Integer;
                    if (!to_int(key, item.value))
                        return false;
                }
                else if (k == NEOS_PY_LIST || is_array(key) || k == NEOS_PY_BOOL)
                {
                    auto* const array = to_array(key, { {}, false });
                    if (array == nullptr)
                        return false;
                    item.array = ref{ as_object(array) };
                    if (array->size() == 0 && k == NEOS_PY_LIST)
                    {
                        // (an empty list indexes nothing: an integer index)
                        auto* const empty = copy_array(*array, dtype::Int64);
                        item.array = ref{ as_object(empty) };
                    }
                    else if (!is_integer(array->type) && !is_bool(array->type))
                    {
                        error("IndexError", "arrays used as indices must be of integer (or boolean) type");
                        return false;
                    }
                    item.kind = index_item::kind_t::Array;
                    item.boolean = is_bool(as_array(item.array.get())->type);
                }
                else
                {
                    error("IndexError", "only integers, slices (`:`), ellipsis (`...`), numpy.newaxis (`None`) and integer or boolean arrays are valid indices");
                    return false;
                }
                aItems.push_back(std::move(item));
            }
            return true;
        }

        // the integer arrays (one per dimension) of the positions of the true elements of a boolean array
        std::vector<ref> nonzero_arrays(ndarray const& aMask)
        {
            std::vector<std::vector<std::int64_t>> positions(std::max<std::size_t>(aMask.ndim(), 1u));
            std::vector<std::int64_t> index(aMask.ndim(), 0);
            auto const total = aMask.size();
            for (std::int64_t flat = 0; flat < total; ++flat)
            {
                std::int64_t offset = 0;
                for (std::size_t d = 0u; d < aMask.ndim(); ++d)
                    offset += index[d] * aMask.strides[d];
                if (read_as<bool>(aMask.pointer() + offset, aMask.type))
                    for (std::size_t d = 0u; d < aMask.ndim(); ++d)
                        positions[d].push_back(index[d]);
                for (std::size_t d = aMask.ndim(); d-- > 0u;)
                {
                    if (++index[d] < aMask.shape[d])
                        break;
                    index[d] = 0;
                }
            }
            std::vector<ref> result;
            for (std::size_t d = 0u; d < aMask.ndim(); ++d)
            {
                auto* const a = new_array(dtype::Int64, { static_cast<std::int64_t>(positions[d].size()) });
                std::memcpy(a->pointer(), positions[d].data(), positions[d].size() * sizeof(std::int64_t));
                result.emplace_back(as_object(a));
            }
            return result;
        }

        // the result of an index: a view (basic), or the positions of the elements (advanced)
        struct index_result
        {
            bool advanced = false;
            bool scalar = false;            ///< (all integers: an element)
            std::size_t offset = 0u;
            shape_t shape;
            shape_t strides;
            // (advanced)
            std::vector<std::int64_t> offsets; ///< (the offsets of the elements, in C order of the result)
        };

        bool apply_index(ndarray const& aArray, object* aKey, index_result& aResult)
        {
            std::vector<index_item> items;
            if (!parse_index(aKey, items))
                return false;
            // (boolean arrays index the dimensions they have)
            std::size_t consumed = 0u;
            std::size_t ellipses = 0u;
            for (auto const& item : items)
            {
                if (item.kind == index_item::kind_t::Integer || item.kind == index_item::kind_t::Slice)
                    ++consumed;
                else if (item.kind == index_item::kind_t::Array)
                    consumed += item.boolean ? std::max<std::size_t>(as_array(item.array.get())->ndim(), 1u) : 1u;
                else if (item.kind == index_item::kind_t::Ellipsis)
                    ++ellipses;
            }
            if (ellipses > 1u)
            {
                error("IndexError", "an index can only have a single ellipsis ('...')");
                return false;
            }
            bool const zeroDimensionalBool = items.size() == 1u && items[0].kind == index_item::kind_t::Array && items[0].boolean && as_array(items[0].array.get())->ndim() == 0;
            if (consumed > aArray.ndim() && !zeroDimensionalBool)
            {
                error("IndexError", "too many indices for array: array is " + std::to_string(aArray.ndim()) + "-dimensional, but " + std::to_string(consumed) + " were indexed");
                return false;
            }
            // (expand the ellipsis; trailing dimensions are indexed by ":")
            std::vector<index_item> expanded;
            for (auto& item : items)
            {
                if (item.kind == index_item::kind_t::Ellipsis)
                {
                    for (std::size_t i = 0u; i < aArray.ndim() - consumed; ++i)
                        expanded.push_back(index_item{ index_item::kind_t::Slice, 0, nullptr, {}, false });
                    consumed = aArray.ndim();
                }
                else if (item.kind == index_item::kind_t::Array && item.boolean)
                {
                    auto const& mask = *as_array(item.array.get());
                    if (mask.ndim() == 0)
                    {
                        // (a scalar boolean: a new dimension of length 1 or 0)
                        auto* const indices = new_array(dtype::Int64, { read_as<bool>(mask.pointer(), mask.type) ? 1 : 0 });
                        index_item axis{ index_item::kind_t::NewAxis };
                        expanded.push_back(axis);
                        expanded.back().array = ref{ as_object(indices) };
                        expanded.back().boolean = true;
                        continue;
                    }
                    std::size_t const first = [&]() { std::size_t d = 0u; for (auto const& e : expanded) if (e.kind != index_item::kind_t::NewAxis) ++d; return d; }();
                    for (std::size_t d = 0u; d < mask.ndim(); ++d)
                        if (first + d >= aArray.ndim() || mask.shape[d] != aArray.shape[first + d])
                        {
                            error("IndexError", "boolean index did not match indexed array along axis " + std::to_string(first + d) + "; size of axis is " +
                                std::to_string(first + d < aArray.ndim() ? aArray.shape[first + d] : 0) + " but size of corresponding boolean axis is " + std::to_string(mask.shape[d]));
                            return false;
                        }
                    for (auto& a : nonzero_arrays(mask))
                    {
                        index_item e{ index_item::kind_t::Array };
                        e.array = a;
                        expanded.push_back(std::move(e));
                    }
                }
                else
                    expanded.push_back(std::move(item));
            }
            {
                std::size_t d = 0u;
                for (auto const& e : expanded)
                    if (e.kind != index_item::kind_t::NewAxis)
                        ++d;
                for (; d < aArray.ndim(); ++d)
                    expanded.push_back(index_item{ index_item::kind_t::Slice, 0, nullptr, {}, false });
            }
            bool const advanced = std::any_of(expanded.begin(), expanded.end(), [](auto const& e) { return e.kind == index_item::kind_t::Array || (e.kind == index_item::kind_t::NewAxis && e.array); });
            // the view (slices and new axes; integers too if the index is basic)
            std::size_t offset = aArray.offset;
            std::size_t source = 0u;
            struct view_dimension { std::int64_t size; std::int64_t stride; };
            std::vector<view_dimension> view;
            struct advanced_index { std::vector<std::int64_t> values; shape_t shape; std::int64_t stride; std::int64_t size; std::size_t axis; };
            std::vector<advanced_index> arrays;
            std::vector<std::size_t> viewBefore; // (the number of view dimensions before each advanced index)
            for (auto const& e : expanded)
            {
                switch (e.kind)
                {
                case index_item::kind_t::Integer:
                    if (!advanced)
                    {
                        auto i = e.value;
                        auto const n = aArray.shape[source];
                        if (i < -n || i >= n)
                        {
                            error("IndexError", "index " + std::to_string(e.value) + " is out of bounds for axis " + std::to_string(source) + " with size " + std::to_string(n));
                            return false;
                        }
                        if (i < 0)
                            i += n;
                        offset = static_cast<std::size_t>(static_cast<std::int64_t>(offset) + i * aArray.strides[source]);
                        ++source;
                    }
                    else
                    {
                        advanced_index a;
                        a.values.push_back(e.value);
                        a.stride = aArray.strides[source];
                        a.size = aArray.shape[source];
                        a.axis = source++;
                        viewBefore.push_back(view.size());
                        arrays.push_back(std::move(a));
                    }
                    break;
                case index_item::kind_t::Slice:
                    {
                        std::int64_t start = 0, stop = 0, step = 1, count = 0;
                        auto const n = aArray.shape[source];
                        if (e.slice == nullptr)
                            count = n;
                        else if (!slice_indices(e.slice, n, start, stop, step, count))
                            return false;
                        offset = static_cast<std::size_t>(static_cast<std::int64_t>(offset) + (count > 0 ? start * aArray.strides[source] : 0));
                        view.push_back({ count, step * aArray.strides[source] });
                        ++source;
                    }
                    break;
                case index_item::kind_t::NewAxis:
                    if (e.array)
                    {
                        // (a scalar boolean index: advanced, of no axis)
                        advanced_index a;
                        auto const& v = *as_array(e.array.get());
                        for (std::int64_t i = 0; i < v.size(); ++i)
                            a.values.push_back(0);
                        a.shape = v.shape;
                        a.stride = 0;
                        a.size = 1;
                        a.axis = source;
                        viewBefore.push_back(view.size());
                        arrays.push_back(std::move(a));
                    }
                    else
                        view.push_back({ 1, 0 });
                    break;
                case index_item::kind_t::Array:
                    {
                        auto const& v = *as_array(e.array.get());
                        advanced_index a;
                        a.shape = v.shape;
                        std::vector<std::int64_t> index(v.ndim(), 0);
                        for (std::int64_t flat = 0; flat < v.size(); ++flat)
                        {
                            std::int64_t o = 0;
                            for (std::size_t d = 0u; d < v.ndim(); ++d)
                                o += index[d] * v.strides[d];
                            a.values.push_back(read_as<std::int64_t>(v.pointer() + o, v.type));
                            for (std::size_t d = v.ndim(); d-- > 0u;)
                            {
                                if (++index[d] < v.shape[d])
                                    break;
                                index[d] = 0;
                            }
                        }
                        a.stride = aArray.strides[source];
                        a.size = aArray.shape[source];
                        a.axis = source++;
                        viewBefore.push_back(view.size());
                        arrays.push_back(std::move(a));
                    }
                    break;
                default:
                    break;
                }
            }
            aResult.advanced = advanced;
            if (!advanced)
            {
                aResult.offset = offset;
                for (auto const& v : view)
                {
                    aResult.shape.push_back(v.size);
                    aResult.strides.push_back(v.stride);
                }
                aResult.scalar = view.empty() && std::all_of(expanded.begin(), expanded.end(), [](auto const& e) { return e.kind == index_item::kind_t::Integer; }) && !expanded.empty();
                return true;
            }
            // (the advanced indices broadcast together)
            std::vector<shape_t const*> shapes;
            for (auto const& a : arrays)
                shapes.push_back(&a.shape);
            shape_t broadcast;
            if (!broadcast_shapes(shapes, broadcast))
            {
                std::string message = "shape mismatch: indexing arrays could not be broadcast together with shapes";
                (void)ctx->failed(ctx);
                return false;
            }
            // (their values checked and normalized)
            for (auto& a : arrays)
                for (auto& v : a.values)
                {
                    if (v < -a.size || v >= a.size)
                    {
                        error("IndexError", "index " + std::to_string(v) + " is out of bounds for axis " + std::to_string(a.axis) + " with size " + std::to_string(a.size));
                        return false;
                    }
                    if (v < 0)
                        v += a.size;
                }
            bool adjacent = true;
            for (std::size_t i = 1u; i < viewBefore.size(); ++i)
                if (viewBefore[i] != viewBefore[0])
                    adjacent = false;
            // (consecutive in the index, not separated by slices)
            {
                std::size_t first = expanded.size(), last = 0u;
                for (std::size_t i = 0u; i < expanded.size(); ++i)
                    if (expanded[i].kind == index_item::kind_t::Array || expanded[i].kind == index_item::kind_t::Integer || (expanded[i].kind == index_item::kind_t::NewAxis && expanded[i].array))
                    {
                        first = std::min(first, i);
                        last = i;
                    }
                for (std::size_t i = first; i <= last && first < expanded.size(); ++i)
                    if (!(expanded[i].kind == index_item::kind_t::Array || expanded[i].kind == index_item::kind_t::Integer || (expanded[i].kind == index_item::kind_t::NewAxis && expanded[i].array)))
                        adjacent = false;
            }
            std::size_t const insertAt = adjacent ? viewBefore[0] : 0u;
            shape_t resultShape;
            for (std::size_t i = 0u; i < insertAt; ++i)
                resultShape.push_back(view[i].size);
            resultShape.insert(resultShape.end(), broadcast.begin(), broadcast.end());
            for (std::size_t i = insertAt; i < view.size(); ++i)
                resultShape.push_back(view[i].size);
            aResult.shape = resultShape;
            // (the offsets of the elements in C order)
            auto const total = element_count(resultShape);
            aResult.offsets.reserve(static_cast<std::size_t>(total));
            std::vector<std::int64_t> index(resultShape.size(), 0);
            auto const nb = broadcast.size();
            for (std::int64_t flat = 0; flat < total; ++flat)
            {
                std::int64_t o = static_cast<std::int64_t>(offset);
                // (view dimensions)
                for (std::size_t i = 0u; i < view.size(); ++i)
                {
                    auto const position = i < insertAt ? i : i + nb;
                    o += index[position] * view[i].stride;
                }
                // (advanced: the broadcast position of each index array)
                for (auto const& a : arrays)
                {
                    std::int64_t element = 0;
                    std::int64_t multiplier = 1;
                    for (std::size_t d = a.shape.size(); d-- > 0u;)
                    {
                        auto const bd = nb - a.shape.size() + d;
                        auto const i = a.shape[d] == 1 ? 0 : index[insertAt + bd];
                        element += i * multiplier;
                        multiplier *= a.shape[d];
                    }
                    o += a.values[static_cast<std::size_t>(element)] * a.stride;
                }
                aResult.offsets.push_back(o);
                for (std::size_t d = resultShape.size(); d-- > 0u;)
                {
                    if (++index[d] < resultShape[d])
                        break;
                    index[d] = 0;
                }
            }
            return true;
        }
    }

    object* array_getitem(object* aSelf, object* aKey)
    {
        auto* const array = as_array(aSelf);
        // (an int indexing a 1-D array: the element)
        if (kind(aKey) == NEOS_PY_INT && array->ndim() == 1u && !array->scalar)
        {
            std::int64_t i = 0;
            if (!to_int(aKey, i))
                return nullptr;
            auto const n = array->shape[0];
            if (i >= -n && i < n)
            {
                if (i < 0)
                    i += n;
                return as_object(new_scalar(array->type, load(array->pointer() + i * array->strides[0], array->type)));
            }
        }
        index_result r;
        if (!apply_index(*array, aKey, r))
            return nullptr;
        if (!r.advanced)
        {
            if (r.scalar)
                return as_object(new_scalar(array->type, load(array->data->data() + r.offset, array->type)));
            if (array->scalar && kind(aKey) == NEOS_PY_TUPLE && r.shape.empty())
                return as_object(new_scalar(array->type, load(array->data->data() + r.offset, array->type)));
            auto* const view = new_view(array, array->type, r.offset, r.shape, r.strides);
            return as_object(view);
        }
        auto* const result = new_array(array->type, r.shape);
        auto const size = array->itemsize();
        auto* to = result->pointer();
        for (auto o : r.offsets)
        {
            std::memcpy(to, array->data->data() + o, size);
            to += size;
        }
        return as_object(result);
    }

    int array_setitem(object* aSelf, object* aKey, object* aValue)
    {
        auto* const array = as_array(aSelf);
        if (aValue == nullptr)
        {
            error("ValueError", "cannot delete array elements");
            return -1;
        }
        if (array->scalar)
        {
            error("TypeError", std::string{ "'numpy." } + info(array->type).scalarName + "' object does not support item assignment");
            return -1;
        }
        if (!array->writeable)
        {
            error("ValueError", "assignment destination is read-only");
            return -1;
        }
        index_result r;
        if (!apply_index(*array, aKey, r))
            return -1;
        ref const valueHolder{ as_object(to_array(aValue, { {}, false })) };
        if (!valueHolder)
            return -1;
        auto const* value = as_array(valueHolder.get());
        // (a Python integer assigned to a smaller integer type must fit)
        if (is_python_scalar(aValue) && kind(aValue) == NEOS_PY_INT && is_integer(array->type) && array->type != dtype::Int64)
        {
            ref const check{ as_object(to_array(aValue, { array->type })) };
            if (!check)
                return -1;
        }
        shape_t shape = r.shape;
        shape_t check;
        if (!broadcast_shapes({ &value->shape, &shape }, check) || check != shape)
        {
            if (!failed())
                error("ValueError", "could not broadcast input array from shape " + shape_repr(value->shape) + " into shape " + shape_repr(shape));
            else
            {
                // (NumPy's message)
            }
            return -1;
        }
        // (the value may overlap the array: copied first)
        ref const source{ as_object(copy_array(*value)) };
        auto const* v = as_array(source.get());
        auto const strides = broadcast_strides(*v, shape);
        if (!r.advanced)
        {
            iterate(shape, { v->pointer(), array->data->data() + r.offset }, { strides, r.strides },
                [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
                {
                    auto from = aPointers[0];
                    auto to = aPointers[1];
                    cast_elements(from, aStrides[0], v->type, to, aStrides[1], array->type, aCount);
                });
            return 0;
        }
        std::size_t k = 0u;
        auto const contiguous = contiguous_strides(shape, 1u);
        iterate(shape, { v->pointer() }, { strides },
            [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
            {
                auto from = aPointers[0];
                for (std::int64_t i = 0; i < aCount; ++i, from += aStrides[0])
                    cast_element(from, v->type, array->data->data() + r.offsets[k++], array->type);
            });
        return 0;
    }
}
