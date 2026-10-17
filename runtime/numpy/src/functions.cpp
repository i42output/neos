/*
  functions.cpp

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

// the numpy module's functions (array creation and manipulation), the attributes and methods of arrays, and the module

#include <algorithm>
#include <deque>
#include <cmath>
#include <map>
#include <numeric>
#include "numpy.hpp"
#include "cast.hpp"
#include "functions.hpp"
#include "ufunc.hpp"
#include "py.hpp"

namespace neos::numpy
{
    // --- helpers

    ref array_ref(object* aObject, std::optional<dtype> aType)
    {
        return ref{ as_object(to_array(aObject, { aType, false })) };
    }

    ndarray* array_of(ref const& aRef)
    {
        return as_array(aRef.get());
    }

    ref new_ref(ndarray* aArray)
    {
        return ref{ as_object(aArray) };
    }

    object* result(ndarray* aArray)
    {
        return result_object(aArray);
    }

    bool optional_dtype(arguments const& aArgs, std::size_t aIndex, std::optional<dtype>& aResult)
    {
        aResult.reset();
        if (!aArgs.has(aIndex))
            return true;
        aResult = parse_dtype(aArgs[aIndex]);
        return aResult.has_value();
    }

    bool optional_bool(arguments const& aArgs, std::size_t aIndex, bool aDefault, bool& aResult)
    {
        aResult = aDefault;
        if (!aArgs.has(aIndex))
            return true;
        return to_bool(aArgs[aIndex], aResult);
    }

    bool optional_axis(arguments const& aArgs, std::size_t aIndex, std::size_t aNdim, std::optional<std::size_t>& aResult)
    {
        aResult.reset();
        if (!aArgs.has(aIndex))
            return true;
        std::int64_t axis = 0;
        if (!to_int(aArgs[aIndex], axis))
            return false;
        std::size_t normalized = 0u;
        if (!normalize_axis(axis, aNdim, normalized))
            return false;
        aResult = normalized;
        return true;
    }

    // C order traversal of the elements of an array
    void for_each(ndarray const& aArray, std::function<void(unsigned char*)> const& aFunction)
    {
        iterate(aArray.shape, { aArray.pointer() }, { aArray.strides }, [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
        {
            auto p = aPointers[0];
            for (std::int64_t i = 0; i < aCount; ++i, p += aStrides[0])
                aFunction(p);
        });
    }

    ndarray* view_of(ndarray* aArray, shape_t aShape, shape_t aStrides, std::size_t aOffset)
    {
        return new_view(aArray, aArray->type, aOffset, std::move(aShape), std::move(aStrides));
    }

    ndarray* contiguous(ndarray* aArray)
    {
        if (aArray->c_contiguous() && !aArray->scalar)
        {
            ctx->retain(as_object(aArray));
            return aArray;
        }
        auto* const result = copy_array(*aArray);
        return result;
    }

    // (the result type of operands, as np.result_type: Python scalars are weak)
    std::optional<dtype> result_type(std::vector<object*> const& aObjects)
    {
        std::optional<dtype> strong;
        std::optional<int> weakKind;
        for (auto o : aObjects)
        {
            if (is_python_scalar(o))
            {
                auto const k = kind(o);
                auto const order = [](int aKind) { return aKind == NEOS_PY_BOOL ? 0 : aKind == NEOS_PY_INT ? 1 : aKind == NEOS_PY_FLOAT ? 2 : 3; };
                if (!weakKind || order(k) > order(*weakKind))
                    weakKind = k;
                continue;
            }
            std::optional<dtype> t;
            if (is_array(o))
                t = as_array(o)->type;
            else if (auto const d = dtype_of_object(o))
                t = d;
            else if (kind(o) == NEOS_PY_STR)
            {
                t = parse_dtype(o);
                if (!t)
                    return {};
            }
            else
            {
                auto const a = array_ref(o, {});
                if (!a)
                    return {};
                t = array_of(a)->type;
            }
            strong = strong ? promote(*strong, *t) : *t;
        }
        if (!weakKind)
            return strong.value_or(dtype::Float64);
        auto const defaultType = *weakKind == NEOS_PY_BOOL ? dtype::Bool : *weakKind == NEOS_PY_INT ? dtype::Int64 : *weakKind == NEOS_PY_FLOAT ? dtype::Float64 : dtype::Complex128;
        if (!strong)
            return defaultType;
        auto const s = *strong;
        switch (*weakKind)
        {
        case NEOS_PY_BOOL: return s;
        case NEOS_PY_INT: return is_bool(s) ? dtype::Int64 : s;
        case NEOS_PY_FLOAT: return (is_bool(s) || is_integer(s)) ? dtype::Float64 : s;
        default:
            if (is_bool(s) || is_integer(s)) return dtype::Complex128;
            if (s == dtype::Float16 || s == dtype::Float32) return dtype::Complex64;
            if (s == dtype::Float64) return dtype::Complex128;
            return s;
        }
    }

    // --- reshape (NumPy's _attempt_nocopy_reshape)

    namespace
    {
        bool attempt_nocopy_reshape(ndarray const& aArray, shape_t const& aShape, shape_t& aStrides)
        {
            shape_t oldDims, oldStrides;
            for (std::size_t i = 0u; i < aArray.ndim(); ++i)
                if (aArray.shape[i] != 1)
                {
                    oldDims.push_back(aArray.shape[i]);
                    oldStrides.push_back(aArray.strides[i]);
                }
            auto const oldNd = oldDims.size();
            auto const newNd = aShape.size();
            aStrides.assign(newNd, 0);
            std::size_t oi = 0u, oj = 1u, ni = 0u, nj = 1u;
            while (ni < newNd && oi < oldNd)
            {
                std::int64_t np = aShape[ni];
                std::int64_t op = oldDims[oi];
                while (np != op)
                {
                    if (np < op)
                        np *= aShape[nj++];
                    else
                        op *= oldDims[oj++];
                }
                for (std::size_t ok = oi; ok < oj - 1u; ++ok)
                    if (oldStrides[ok] != oldDims[ok + 1u] * oldStrides[ok + 1u])
                        return false;
                aStrides[nj - 1u] = oldStrides[oj - 1u];
                for (std::size_t nk = nj - 1u; nk > ni; --nk)
                    aStrides[nk - 1u] = aStrides[nk] * aShape[nk];
                ni = nj++;
                oi = oj++;
            }
            std::int64_t last = ni >= 1u ? aStrides[ni - 1u] : static_cast<std::int64_t>(aArray.itemsize());
            if (ni >= 1u)
                last *= aShape[ni - 1u];
            for (std::size_t nk = ni; nk < newNd; ++nk)
                aStrides[nk] = last;
            return true;
        }
    }

    // (NumPy's convert_shape_to_string: no spaces; leading negative dimensions are omitted, others are "newaxis")
    std::string shape_string(shape_t const& aShape)
    {
        std::size_t i = 0u;
        while (i < aShape.size() && aShape[i] < 0)
            ++i;
        if (i == aShape.size())
            return "()";
        std::string result = "(" + std::to_string(aShape[i]);
        for (++i; i < aShape.size(); ++i)
            result += aShape[i] < 0 ? std::string{ ",newaxis" } : "," + std::to_string(aShape[i]);
        return result + (aShape.size() == 1u ? ",)" : ")");
    }

    bool resolve_shape(shape_t& aShape, std::int64_t aSize)
    {
        std::optional<std::size_t> unknown;
        std::int64_t known = 1;
        for (std::size_t i = 0u; i < aShape.size(); ++i)
        {
            if (aShape[i] < 0)
            {
                if (aShape[i] != -1 || unknown)
                {
                    error("ValueError", aShape[i] != -1 ? "negative dimensions not allowed" : "can only specify one unknown dimension");
                    return false;
                }
                unknown = i;
            }
            else
                known *= aShape[i];
        }
        if (unknown)
        {
            if (known == 0 || aSize % known != 0)
            {
                error("ValueError", "cannot reshape array of size " + std::to_string(aSize) + " into shape " + shape_string(aShape));
                return false;
            }
            aShape[*unknown] = aSize / known;
        }
        else if (known != aSize)
        {
            error("ValueError", "cannot reshape array of size " + std::to_string(aSize) + " into shape " + shape_string(aShape));
            return false;
        }
        return true;
    }

    ndarray* reshape(ndarray* aArray, shape_t aShape, bool aFortran)
    {
        if (!resolve_shape(aShape, aArray->size()))
            return nullptr;
        shape_t strides;
        if (!aFortran)
        {
            if (aArray->size() == 0 || attempt_nocopy_reshape(*aArray, aShape, strides))
            {
                if (aArray->size() == 0)
                    strides = contiguous_strides(aShape, aArray->itemsize());
                auto* const view = view_of(aArray, aShape, strides, aArray->offset);
                view->scalar = false;
                return view;
            }
            auto* const copy = copy_array(*aArray);
            copy->shape = aShape;
            copy->strides = contiguous_strides(aShape, copy->itemsize());
            return copy;
        }
        // (Fortran order: reshape the reversed transposes)
        shape_t reversedShape{ aShape.rbegin(), aShape.rend() };
        auto* const transposed = view_of(aArray, { aArray->shape.rbegin(), aArray->shape.rend() }, { aArray->strides.rbegin(), aArray->strides.rend() }, aArray->offset);
        auto* const r = reshape(transposed, reversedShape, false);
        ctx->release(as_object(transposed));
        if (r == nullptr)
            return nullptr;
        std::reverse(r->shape.begin(), r->shape.end());
        std::reverse(r->strides.begin(), r->strides.end());
        return r;
    }

    ndarray* transpose(ndarray* aArray, std::vector<std::size_t> const& aAxes)
    {
        shape_t shape, strides;
        for (auto a : aAxes)
        {
            shape.push_back(aArray->shape[a]);
            strides.push_back(aArray->strides[a]);
        }
        auto* const view = view_of(aArray, shape, strides, aArray->offset);
        view->scalar = false;
        return view;
    }

    ndarray* ravel(ndarray* aArray, bool aFortran)
    {
        return reshape(aArray, { aArray->size() }, aFortran);
    }

    // --- concatenation

    ndarray* concatenate(std::vector<ndarray*> const& aArrays, std::optional<std::size_t> aAxis, std::optional<dtype> aType)
    {
        if (aArrays.empty())
            return error("ValueError", "need at least one array to concatenate");
        std::vector<ref> flattened;
        std::vector<ndarray*> arrays = aArrays;
        if (!aAxis)
        {
            arrays.clear();
            for (auto* a : aArrays)
            {
                flattened.push_back(new_ref(ravel(a)));
                arrays.push_back(array_of(flattened.back()));
            }
        }
        auto const axis = aAxis.value_or(0u);
        auto const ndim = arrays[0]->ndim();
        if (ndim == 0)
            return error("ValueError", "zero-dimensional arrays cannot be concatenated");
        dtype t = arrays[0]->type;
        for (std::size_t i = 0u; i < arrays.size(); ++i)
        {
            if (arrays[i]->ndim() != ndim)
                return error("ValueError", "all the input array dimensions except for the concatenation axis must match exactly, but along dimension 0, the array at index 0 has " +
                    std::to_string(ndim) + " dimension(s) and the array at index " + std::to_string(i) + " has " + std::to_string(arrays[i]->ndim()) + " dimension(s)");
            for (std::size_t d = 0u; d < ndim; ++d)
                if (d != axis && arrays[i]->shape[d] != arrays[0]->shape[d])
                    return error("ValueError", "all the input array dimensions except for the concatenation axis must match exactly, but along dimension " +
                        std::to_string(d) + ", the array at index 0 has size " + std::to_string(arrays[0]->shape[d]) + " and the array at index " + std::to_string(i) +
                        " has size " + std::to_string(arrays[i]->shape[d]));
            t = promote(t, arrays[i]->type);
        }
        if (aType)
            t = *aType;
        shape_t shape = arrays[0]->shape;
        shape[axis] = 0;
        for (auto* a : arrays)
            shape[axis] += a->shape[axis];
        auto* const result = new_array(t, shape);
        std::int64_t position = 0;
        for (auto* a : arrays)
        {
            iterate(a->shape, { a->pointer(), result->pointer() + position * result->strides[axis] }, { a->strides, result->strides },
                [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
                {
                    auto from = aPointers[0];
                    auto to = aPointers[1];
                    for (std::int64_t i = 0; i < aCount; ++i, from += aStrides[0], to += aStrides[1])
                        cast_element(from, a->type, to, t);
                });
            position += a->shape[axis];
        }
        return result;
    }

    bool array_sequence(object* aObject, std::vector<ref>& aResult)
    {
        std::vector<object*> items;
        if (is_sequence(aObject))
            items = sequence_items(aObject);
        else if (is_ndarray(aObject))
        {
            auto* const a = as_array(aObject);
            for (std::int64_t i = 0; i < (a->ndim() == 0 ? 0 : a->shape[0]); ++i)
            {
                ref const index{ make_int(i) };
                aResult.emplace_back(array_getitem(aObject, index.get()));
                if (!aResult.back())
                    return false;
                if (!is_array(aResult.back().get()))
                    aResult.back() = array_ref(aResult.back().get(), {});
            }
            return true;
        }
        else
        {
            error("TypeError", "expected a sequence of arrays");
            return false;
        }
        for (auto item : items)
        {
            aResult.push_back(array_ref(item, {}));
            if (!aResult.back())
                return false;
        }
        return true;
    }

    std::vector<ndarray*> pointers(std::vector<ref> const& aRefs)
    {
        std::vector<ndarray*> result;
        for (auto const& r : aRefs)
            result.push_back(array_of(r));
        return result;
    }

    ndarray* expand_dims(ndarray* aArray, std::size_t aAxis)
    {
        shape_t shape = aArray->shape;
        shape_t strides = aArray->strides;
        shape.insert(shape.begin() + static_cast<std::ptrdiff_t>(aAxis), 1);
        strides.insert(strides.begin() + static_cast<std::ptrdiff_t>(aAxis), 0);
        auto* const view = view_of(aArray, shape, strides, aArray->offset);
        view->scalar = false;
        return view;
    }

    ndarray* atleast(ndarray* aArray, std::size_t aNdim)
    {
        auto* a = aArray;
        ctx->retain(as_object(a));
        if (a->ndim() >= aNdim)
        {
            if (a->scalar)
            {
                auto* const view = view_of(a, a->shape, a->strides, a->offset);
                ctx->release(as_object(a));
                return view;
            }
            return a;
        }
        shape_t shape = a->shape, strides = a->strides;
        switch (aNdim)
        {
        case 1:
            shape = { 1 }; strides = { 0 };
            break;
        case 2:
            if (a->ndim() == 0) { shape = { 1, 1 }; strides = { 0, 0 }; }
            else { shape.insert(shape.begin(), 1); strides.insert(strides.begin(), 0); }
            break;
        default:
            if (a->ndim() == 0) { shape = { 1, 1, 1 }; strides = { 0, 0, 0 }; }
            else if (a->ndim() == 1) { shape = { 1, a->shape[0], 1 }; strides = { 0, a->strides[0], 0 }; }
            else { shape.push_back(1); strides.push_back(0); }
            break;
        }
        auto* const view = view_of(a, shape, strides, a->offset);
        view->scalar = false;
        ctx->release(as_object(a));
        return view;
    }

    // a new array of an element type with each element given by a function of its flat (C order) index
    template <typename T, typename F>
    ndarray* generate(dtype aType, shape_t const& aShape, F&& aFunction)
    {
        auto* const result = new_array(aType, aShape);
        auto const n = result->size();
        auto* p = result->pointer();
        for (std::int64_t i = 0; i < n; ++i, p += result->itemsize())
            write_as(p, aType, aFunction(i));
        return result;
    }

    // --- array creation

    namespace
    {
        object* f_array(object* aArgs, object* aKwargs, bool aCopyDefault, char const* aName)
        {
            arguments args{ aName, aArgs, aKwargs, { "object", "dtype", "*copy", "*order", "*subok", "*ndmin", "*like" } };
            if (!args.ok())
                return nullptr;
            array_options o;
            if (args.has(1) && !(o.type = parse_dtype(args[1])))
                return nullptr;
            o.copy = aCopyDefault;
            if (args.has(2))
            {
                bool copy = true;
                if (!to_bool(args[2], copy))
                    return nullptr;
                o.copy = copy;
            }
            if (args.has(5))
            {
                std::int64_t n = 0;
                if (!to_int(args[5], n))
                    return nullptr;
                o.ndmin = static_cast<int>(n);
            }
            auto* const a = to_array(args[0], o);
            if (a == nullptr)
                return nullptr;
            bool const fortran = args.has(3) && as_str(args[3]) == std::optional<std::string>{ "F" };
            if (fortran && !a->f_contiguous())
            {
                auto* const f = copy_array(*a, {}, true);
                ctx->release(as_object(a));
                return as_object(f);
            }
            return as_object(a);
        }

        object* f_np_array(object* aArgs, object* aKwargs) { return f_array(aArgs, aKwargs, true, "array"); }
        object* f_asarray(object* aArgs, object* aKwargs) { return f_array(aArgs, aKwargs, false, "asarray"); }

        object* f_ascontiguousarray(object* aArgs, object* aKwargs)
        {
            arguments args{ "ascontiguousarray", aArgs, aKwargs, { "a", "dtype", "*like" } };
            if (!args.ok())
                return nullptr;
            std::optional<dtype> t;
            if (!optional_dtype(args, 1, t))
                return nullptr;
            auto const a = array_ref(args[0], t);
            if (!a)
                return nullptr;
            auto* const c = contiguous(array_of(a));
            if (c->ndim() == 0)
            {
                auto* const one = reshape(c, { 1 });
                ctx->release(as_object(c));
                return as_object(one);
            }
            return as_object(c);
        }

        object* f_copy(object* aArgs, object* aKwargs)
        {
            arguments args{ "copy", aArgs, aKwargs, { "a", "order", "subok" } };
            if (!args.ok())
                return nullptr;
            auto* const a = to_array(args[0], { {}, true });
            if (a == nullptr)
                return nullptr;
            a->scalar = false;
            a->base.type = &array_type()->base;
            return as_object(a);
        }

        object* filled(char const* aName, object* aArgs, object* aKwargs, int aFill)
        {
            arguments args{ aName, aArgs, aKwargs, { "shape", "dtype", "order", "*like", "*device" } };
            if (!args.ok())
                return nullptr;
            shape_t shape;
            if (!to_shape(args[0], shape))
                return nullptr;
            for (auto d : shape)
                if (d < 0)
                    return error("ValueError", "negative dimensions are not allowed");
            std::optional<dtype> t;
            if (!optional_dtype(args, 1, t))
                return nullptr;
            bool const fortran = args.has(2) && as_str(args[2]) == std::optional<std::string>{ "F" };
            auto* const a = new_array(t.value_or(dtype::Float64), shape, fortran);
            if (aFill == 1)
            {
                scalar_value one;
                one.type = dtype::Int64;
                one.i = 1;
                for_each(*a, [&](unsigned char* p) { store(p, a->type, one); });
            }
            return as_object(a);
        }

        object* f_zeros(object* aArgs, object* aKwargs) { return filled("zeros", aArgs, aKwargs, 0); }
        object* f_ones(object* aArgs, object* aKwargs) { return filled("ones", aArgs, aKwargs, 1); }
        object* f_empty(object* aArgs, object* aKwargs) { return filled("empty", aArgs, aKwargs, 0); }

        object* f_full(object* aArgs, object* aKwargs)
        {
            arguments args{ "full", aArgs, aKwargs, { "shape", "fill_value", "dtype", "order", "*like", "*device" } };
            if (!args.ok())
                return nullptr;
            shape_t shape;
            if (!to_shape(args[0], shape))
                return nullptr;
            std::optional<dtype> t;
            if (!optional_dtype(args, 2, t))
                return nullptr;
            auto const value = array_ref(args[1], t);
            if (!value)
                return nullptr;
            auto* const v = array_of(value);
            auto* const a = new_array(t.value_or(v->type), shape);
            shape_t check;
            if (!broadcast_shapes({ &v->shape, &shape }, check) || check != shape)
            {
                ctx->release(as_object(a));
                return nullptr;
            }
            auto const strides = broadcast_strides(*v, shape);
            iterate(shape, { v->pointer(), a->pointer() }, { strides, a->strides }, [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
            {
                auto from = aPointers[0];
                auto to = aPointers[1];
                for (std::int64_t i = 0; i < aCount; ++i, from += aStrides[0], to += aStrides[1])
                    cast_element(from, v->type, to, a->type);
            });
            return as_object(a);
        }

        object* like(char const* aName, object* aArgs, object* aKwargs, int aMode)
        {
            arguments args{ aName, aArgs, aKwargs, aMode == 2 ? std::initializer_list<char const*>{ "a", "fill_value", "dtype", "order", "subok", "shape", "*device" } :
                std::initializer_list<char const*>{ "a", "dtype", "order", "subok", "shape", "*device" } };
            if (!args.ok())
                return nullptr;
            auto const a = array_ref(args[0], {});
            if (!a)
                return nullptr;
            std::size_t const dtypeIndex = aMode == 2 ? 2u : 1u;
            std::size_t const shapeIndex = aMode == 2 ? 5u : 4u;
            std::optional<dtype> t;
            if (!optional_dtype(args, dtypeIndex, t))
                return nullptr;
            shape_t shape = array_of(a)->shape;
            if (args.has(shapeIndex) && !to_shape(args[shapeIndex], shape))
                return nullptr;
            auto* const r = new_array(t.value_or(array_of(a)->type), shape);
            if (aMode == 1)
            {
                scalar_value one;
                one.type = dtype::Int64;
                one.i = 1;
                for_each(*r, [&](unsigned char* p) { store(p, r->type, one); });
            }
            else if (aMode == 2)
            {
                auto const value = array_ref(args[1], r->type);
                if (!value)
                {
                    ctx->release(as_object(r));
                    return nullptr;
                }
                auto const* v = array_of(value);
                auto const strides = broadcast_strides(*v, shape);
                iterate(shape, { v->pointer(), r->pointer() }, { strides, r->strides }, [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
                {
                    auto from = aPointers[0];
                    auto to = aPointers[1];
                    for (std::int64_t i = 0; i < aCount; ++i, from += aStrides[0], to += aStrides[1])
                        cast_element(from, v->type, to, r->type);
                });
            }
            return as_object(r);
        }

        object* f_zeros_like(object* aArgs, object* aKwargs) { return like("zeros_like", aArgs, aKwargs, 0); }
        object* f_ones_like(object* aArgs, object* aKwargs) { return like("ones_like", aArgs, aKwargs, 1); }
        object* f_empty_like(object* aArgs, object* aKwargs) { return like("empty_like", aArgs, aKwargs, 0); }
        object* f_full_like(object* aArgs, object* aKwargs) { return like("full_like", aArgs, aKwargs, 2); }

        object* f_arange(object* aArgs, object* aKwargs)
        {
            arguments args{ "arange", aArgs, aKwargs, { "start", "stop", "step", "dtype", "*device", "*like" } };
            if (!args.ok())
                return nullptr;
            object* start = args[0];
            object* stop = args[1];
            object* step = args[2];
            if (start == nullptr)
                return error("TypeError", "arange() requires stop to be specified.");
            if (stop == nullptr || kind(stop) == NEOS_PY_NONE)
            {
                stop = start;
                start = nullptr;
            }
            std::optional<dtype> t;
            if (!optional_dtype(args, 3, t))
                return nullptr;
            std::vector<object*> parts{ stop };
            if (start != nullptr) parts.push_back(start);
            if (step != nullptr && kind(step) != NEOS_PY_NONE) parts.push_back(step);
            auto const inferred = result_type(parts);
            if (!inferred)
                return nullptr;
            auto const type = t.value_or(is_bool(*inferred) ? dtype::Int64 : *inferred);
            if (is_complex(type))
                return error("TypeError", "arange() does not support complex dtypes");
            double startValue = 0.0, stopValue = 0.0, stepValue = 1.0;
            if (start != nullptr && !to_double(start, startValue))
                return nullptr;
            if (!to_double(stop, stopValue))
                return nullptr;
            if (step != nullptr && kind(step) != NEOS_PY_NONE && !to_double(step, stepValue))
                return nullptr;
            if (stepValue == 0.0)
                return error("ZeroDivisionError", "division by zero");
            std::int64_t length;
            bool const integral = (start == nullptr || kind(start) == NEOS_PY_INT || (is_array(start) && is_integer(as_array(start)->type))) &&
                (kind(stop) == NEOS_PY_INT || (is_array(stop) && is_integer(as_array(stop)->type))) &&
                (step == nullptr || kind(step) == NEOS_PY_NONE || kind(step) == NEOS_PY_INT || (is_array(step) && is_integer(as_array(step)->type)));
            if (integral)
            {
                std::int64_t a = 0, b = 0, s = 1;
                if (start != nullptr) to_int(start, a);
                to_int(stop, b);
                if (step != nullptr && kind(step) != NEOS_PY_NONE) to_int(step, s);
                auto const delta = b - a;
                length = s > 0 ? (delta > 0 ? (delta + s - 1) / s : 0) : (delta < 0 ? (delta + s + 1) / s : 0);
            }
            else
            {
                double const n = std::ceil((stopValue - startValue) / stepValue);
                if (!std::isfinite(n))
                    return error("ValueError", "arange: cannot compute length");
                length = n > 0.0 ? static_cast<std::int64_t>(n) : 0;
            }
            auto* const r = new_array(type, { length });
            if (length > 0)
            {
                auto const size = r->itemsize();
                // (as NumPy: the first two values, then start + i * (second - first) in the array's type)
                dispatch(type, [&](auto tag)
                {
                    using T = decltype(tag);
                    using C = std::conditional_t<is_half_v<T>, float, T>;
                    T first, second;
                    if constexpr (std::is_integral_v<T> && !is_bool_v<T>)
                    {
                        std::int64_t a = 0, s = 1;
                        if (start != nullptr) to_int(start, a);
                        if (step != nullptr && kind(step) != NEOS_PY_NONE) to_int(step, s);
                        if (!integral)
                        {
                            a = static_cast<std::int64_t>(startValue);
                            s = static_cast<std::int64_t>(stepValue);
                        }
                        first = static_cast<T>(a);
                        second = static_cast<T>(a + s);
                    }
                    else
                    {
                        first = cast<T>(startValue);
                        second = cast<T>(startValue + stepValue);
                    }
                    std::memcpy(r->pointer(), &first, size);
                    if (length > 1)
                        std::memcpy(r->pointer() + size, &second, size);
                    C const f = cast<C>(first);
                    C const delta = static_cast<C>(cast<C>(second) - f);
                    for (std::int64_t i = 2; i < length; ++i)
                    {
                        T value;
                        if constexpr (is_bool_v<T>)
                            value = (f + static_cast<C>(i) * delta) != 0;
                        else if constexpr (is_half_v<T>)
                            value = half{ f + static_cast<float>(i) * delta };
                        else
                            value = static_cast<T>(f + static_cast<C>(i) * delta);
                        std::memcpy(r->pointer() + static_cast<std::size_t>(i) * size, &value, size);
                    }
                });
            }
            return as_object(r);
        }

        object* f_linspace(object* aArgs, object* aKwargs)
        {
            arguments args{ "linspace", aArgs, aKwargs, { "start", "stop", "num", "endpoint", "retstep", "dtype", "axis", "*device" } };
            if (!args.ok())
                return nullptr;
            std::int64_t num = 50;
            if (args.has(2) && !to_int(args[2], num))
                return nullptr;
            if (num < 0)
                return error("ValueError", "Number of samples, " + std::to_string(num) + ", must be non-negative.");
            bool endpoint = true, retstep = false;
            if (!optional_bool(args, 3, true, endpoint) || !optional_bool(args, 4, false, retstep))
                return nullptr;
            std::optional<dtype> t;
            if (!optional_dtype(args, 5, t))
                return nullptr;
            auto const div = endpoint ? num - 1 : num;
            auto dt = result_type({ args[0], args[1] });
            if (!dt)
                return nullptr;
            if (!is_inexact(*dt))
                dt = promote(*dt, dtype::Float64) == dtype::Float64 || is_bool(*dt) ? dtype::Float64 : promote(*dt, dtype::Float64);
            auto const start = array_ref(args[0], *dt);
            auto const stop = array_ref(args[1], *dt);
            if (!start || !stop)
                return nullptr;
            ref const delta{ apply_binary(*find_ufunc("subtract"), stop.get(), start.get(), nullptr, *dt) };
            if (!delta)
                return nullptr;
            auto const deltaArray = array_ref(delta.get(), {});
            auto const deltaNdim = array_of(deltaArray)->ndim();
            // y = arange(0, num).reshape(-1, 1, ...)
            auto* const y0 = generate<double>(*dt, { num }, [](std::int64_t i) { return static_cast<double>(i); });
            shape_t yShape{ num };
            for (std::size_t i = 0u; i < deltaNdim; ++i)
                yShape.push_back(1);
            ref y{ as_object(reshape(y0, yShape)) };
            ctx->release(as_object(y0));
            ref stepObject;
            auto const* const divide = find_ufunc("true_divide");
            auto const* const multiply = find_ufunc("multiply");
            if (div > 0)
            {
                ref const divisor{ make_int(div) };
                stepObject = ref{ apply_binary(*divide, delta.get(), divisor.get()) };
                if (!stepObject)
                    return nullptr;
                auto const stepArray = array_ref(stepObject.get(), {});
                bool anyZero = false;
                for_each(*array_of(stepArray), [&](unsigned char* p) { if (read_as<std::complex<double>>(p, array_of(stepArray)->type) == std::complex<double>{}) anyZero = true; });
                if (anyZero)
                {
                    y = ref{ apply_binary(*divide, y.get(), divisor.get(), as_array(y.get())) };
                    y = ref{ apply_binary(*multiply, y.get(), delta.get()) };
                }
                else
                    y = ref{ apply_binary(*multiply, y.get(), stepObject.get()) };
            }
            else
            {
                stepObject = ref{ make_float(std::numeric_limits<double>::quiet_NaN()) };
                y = ref{ apply_binary(*multiply, y.get(), delta.get()) };
            }
            if (!y)
                return nullptr;
            y = ref{ apply_binary(*find_ufunc("add"), y.get(), start.get()) };
            if (!y)
                return nullptr;
            auto yArray = array_ref(y.get(), {});
            auto* ya = array_of(yArray);
            if (endpoint && num > 1)
            {
                // y[-1, ...] = stop
                auto const* s = array_of(stop);
                shape_t rowShape{ ya->shape.begin() + 1, ya->shape.end() };
                shape_t rowStrides{ ya->strides.begin() + 1, ya->strides.end() };
                auto const strides = broadcast_strides(*s, rowShape);
                iterate(rowShape, { s->pointer(), ya->pointer() + (num - 1) * ya->strides[0] }, { strides, rowStrides },
                    [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
                    {
                        auto from = aPointers[0];
                        auto to = aPointers[1];
                        for (std::int64_t i = 0; i < aCount; ++i, from += aStrides[0], to += aStrides[1])
                            cast_element(from, s->type, to, ya->type);
                    });
            }
            if (args.has(6))
            {
                std::int64_t axis = 0;
                if (!to_int(args[6], axis))
                    return nullptr;
                std::size_t normalized = 0u;
                if (!normalize_axis(axis, ya->ndim(), normalized))
                    return nullptr;
                std::vector<std::size_t> order;
                for (std::size_t i = 1u; i < ya->ndim(); ++i)
                    order.push_back(i);
                order.insert(order.begin() + static_cast<std::ptrdiff_t>(normalized), 0u);
                yArray = new_ref(transpose(ya, order));
                ya = array_of(yArray);
            }
            if (t && is_integer(*t))
            {
                ref const floored{ apply_unary(*find_ufunc("floor"), as_object(ya)) };
                yArray = array_ref(floored.get(), {});
                ya = array_of(yArray);
            }
            ref finalResult;
            if (t && *t != ya->type)
                finalResult = new_ref(copy_array(*ya, *t));
            else
                finalResult = yArray;
            if (retstep)
                return make_tuple({ finalResult.release(), stepObject.release() });
            return finalResult.release();
        }

        object* f_logspace(object* aArgs, object* aKwargs)
        {
            arguments args{ "logspace", aArgs, aKwargs, { "start", "stop", "num", "endpoint", "base", "dtype", "axis" } };
            if (!args.ok())
                return nullptr;
            std::vector<object*> items;
            for (std::size_t i : { 0u, 1u })
            {
                ctx->retain(args[i]);
                items.push_back(args[i]);
            }
            ref const linArgs{ make_tuple(items) };
            ref const linKwargs{ ctx->dict_new(ctx) };
            if (args.has(2)) ctx->dict_set(ctx, linKwargs.get(), ref{ make_str("num") }.get(), args[2]);
            if (args.has(3)) ctx->dict_set(ctx, linKwargs.get(), ref{ make_str("endpoint") }.get(), args[3]);
            ref const y{ f_linspace(linArgs.get(), linKwargs.get()) };
            if (!y)
                return nullptr;
            ref const base{ args.has(4) ? ref::borrowed(args[4]) : ref{ make_float(10.0) } };
            ref const r{ apply_binary(*find_ufunc("power"), base.get(), y.get()) };
            if (!r)
                return nullptr;
            std::optional<dtype> t;
            if (!optional_dtype(args, 5, t))
                return nullptr;
            if (t)
                return as_object(copy_array(*as_array(r.get()), *t));
            return ref{ r }.release();
        }

        object* f_geomspace(object* aArgs, object* aKwargs)
        {
            arguments args{ "geomspace", aArgs, aKwargs, { "start", "stop", "num", "endpoint", "dtype", "axis" } };
            if (!args.ok())
                return nullptr;
            double start = 0.0, stop = 0.0;
            if (!to_double(args[0], start) || !to_double(args[1], stop))
                return nullptr;
            if (start == 0.0 || stop == 0.0)
                return error("ValueError", "Geometric sequence cannot include zero");
            std::int64_t num = 50;
            if (args.has(2) && !to_int(args[2], num))
                return nullptr;
            bool endpoint = true;
            if (!optional_bool(args, 3, true, endpoint))
                return nullptr;
            // (as NumPy: logspace of the logs, with the ends exact)
            double const sign = start < 0.0 && stop < 0.0 ? -1.0 : 1.0;
            double const logStart = std::log10(start * sign);
            double const logStop = std::log10(stop * sign);
            ref const linArgs{ make_tuple({ make_float(logStart), make_float(logStop) }) };
            ref const linKwargs{ ctx->dict_new(ctx) };
            ctx->dict_set(ctx, linKwargs.get(), ref{ make_str("num") }.get(), ref{ make_int(num) }.get());
            ctx->dict_set(ctx, linKwargs.get(), ref{ make_str("endpoint") }.get(), ref{ make_bool(endpoint) }.get());
            ctx->dict_set(ctx, linKwargs.get(), ref{ make_str("base") }.get(), ref{ make_float(10.0) }.get());
            ref const r{ f_logspace(linArgs.get(), linKwargs.get()) };
            if (!r)
                return nullptr;
            auto* const a = copy_array(*as_array(r.get()));
            auto* p = a->pointer();
            if (num > 0)
                write_as(p, a->type, start * sign);
            if (num > 1 && endpoint)
                write_as(p + (num - 1) * a->strides[0], a->type, stop * sign);
            if (sign < 0.0)
                for_each(*a, [&](unsigned char* e) { write_as(e, a->type, -read_as<double>(e, a->type)); });
            std::optional<dtype> t;
            if (!optional_dtype(args, 4, t))
            {
                ctx->release(as_object(a));
                return nullptr;
            }
            if (t)
            {
                auto* const c = copy_array(*a, *t);
                ctx->release(as_object(a));
                return as_object(c);
            }
            return as_object(a);
        }

        object* f_eye(object* aArgs, object* aKwargs)
        {
            arguments args{ "eye", aArgs, aKwargs, { "N", "M", "k", "dtype", "order", "*device", "*like" } };
            if (!args.ok())
                return nullptr;
            std::int64_t n = 0, m = 0, k = 0;
            if (!to_int(args[0], n))
                return nullptr;
            m = n;
            if (args.has(1) && !to_int(args[1], m))
                return nullptr;
            if (args.has(2) && !to_int(args[2], k))
                return nullptr;
            std::optional<dtype> t;
            if (!optional_dtype(args, 3, t))
                return nullptr;
            auto* const a = new_array(t.value_or(dtype::Float64), { n, m });
            scalar_value one;
            one.type = dtype::Int64;
            one.i = 1;
            for (std::int64_t i = 0; i < n; ++i)
                if (i + k >= 0 && i + k < m)
                    store(a->pointer() + i * a->strides[0] + (i + k) * a->strides[1], a->type, one);
            return as_object(a);
        }

        object* f_identity(object* aArgs, object* aKwargs)
        {
            arguments args{ "identity", aArgs, aKwargs, { "n", "dtype", "*like" } };
            if (!args.ok())
                return nullptr;
            ctx->retain(args[0]);
            ref const eyeArgs{ make_tuple({ args[0] }) };
            ref const eyeKwargs{ ctx->dict_new(ctx) };
            if (args.has(1))
                ctx->dict_set(ctx, eyeKwargs.get(), ref{ make_str("dtype") }.get(), args[1]);
            return f_eye(eyeArgs.get(), eyeKwargs.get());
        }

        object* f_diag(object* aArgs, object* aKwargs)
        {
            arguments args{ "diag", aArgs, aKwargs, { "v", "k" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            std::int64_t k = 0;
            if (args.has(1) && !to_int(args[1], k))
                return nullptr;
            auto* const a = array_of(v);
            if (a->ndim() == 1)
            {
                auto const n = a->shape[0] + std::llabs(k);
                auto* const r = new_array(a->type, { n, n });
                for (std::int64_t i = 0; i < a->shape[0]; ++i)
                {
                    auto const row = k >= 0 ? i : i - k;
                    auto const column = k >= 0 ? i + k : i;
                    std::memcpy(r->pointer() + row * r->strides[0] + column * r->strides[1], a->pointer() + i * a->strides[0], a->itemsize());
                }
                return as_object(r);
            }
            if (a->ndim() != 2)
                return error("ValueError", "Input must be 1- or 2-d.");
            ref const diagonalArgs{ make_tuple({ (ctx->retain(v.get()), v.get()), make_int(k) }) };
            return call_function("diagonal", diagonalArgs.get(), nullptr);
        }

        object* f_diagonal(object* aArgs, object* aKwargs)
        {
            arguments args{ "diagonal", aArgs, aKwargs, { "a", "offset", "axis1", "axis2" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto* const a = array_of(v);
            std::int64_t offset = 0, axis1 = 0, axis2 = 1;
            if (args.has(1) && !to_int(args[1], offset)) return nullptr;
            if (args.has(2) && !to_int(args[2], axis1)) return nullptr;
            if (args.has(3) && !to_int(args[3], axis2)) return nullptr;
            if (a->ndim() < 2)
                return error("ValueError", "diag requires an array of at least two dimensions");
            std::size_t a1 = 0u, a2 = 0u;
            if (!normalize_axis(axis1, a->ndim(), a1) || !normalize_axis(axis2, a->ndim(), a2))
                return nullptr;
            if (a1 == a2)
                return error("ValueError", "axis1 and axis2 cannot be the same");
            shape_t shape, strides;
            for (std::size_t i = 0u; i < a->ndim(); ++i)
                if (i != a1 && i != a2)
                {
                    shape.push_back(a->shape[i]);
                    strides.push_back(a->strides[i]);
                }
            std::int64_t n;
            std::size_t start = a->offset;
            if (offset >= 0)
            {
                n = std::max<std::int64_t>(std::min(a->shape[a1], a->shape[a2] - offset), 0);
                if (n > 0) start = static_cast<std::size_t>(static_cast<std::int64_t>(start) + offset * a->strides[a2]);
            }
            else
            {
                n = std::max<std::int64_t>(std::min(a->shape[a1] + offset, a->shape[a2]), 0);
                if (n > 0) start = static_cast<std::size_t>(static_cast<std::int64_t>(start) - offset * a->strides[a1]);
            }
            shape.push_back(n);
            strides.push_back(a->strides[a1] + a->strides[a2]);
            auto* const view = view_of(a, shape, strides, start);
            view->writeable = false;
            return as_object(view);
        }

        object* triangle(char const* aName, object* aArgs, object* aKwargs, bool aLower)
        {
            arguments args{ aName, aArgs, aKwargs, { "m", "k" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            std::int64_t k = 0;
            if (args.has(1) && !to_int(args[1], k))
                return nullptr;
            auto* const a = copy_array(*array_of(v));
            if (a->ndim() < 2)
            {
                ctx->release(as_object(a));
                return error("ValueError", "input array must be at least 2-d");
            }
            auto const rows = a->shape[a->ndim() - 2u];
            auto const columns = a->shape[a->ndim() - 1u];
            auto const size = a->itemsize();
            std::int64_t const matrices = a->size() / std::max<std::int64_t>(rows * columns, 1);
            for (std::int64_t m = 0; m < matrices; ++m)
                for (std::int64_t i = 0; i < rows; ++i)
                    for (std::int64_t j = 0; j < columns; ++j)
                        if (aLower ? j - i > k : j - i < k)
                            std::memset(a->pointer() + static_cast<std::size_t>((m * rows + i) * columns + j) * size, 0, size);
            return as_object(a);
        }

        object* f_tril(object* aArgs, object* aKwargs) { return triangle("tril", aArgs, aKwargs, true); }
        object* f_triu(object* aArgs, object* aKwargs) { return triangle("triu", aArgs, aKwargs, false); }

        object* f_tri(object* aArgs, object* aKwargs)
        {
            arguments args{ "tri", aArgs, aKwargs, { "N", "M", "k", "dtype", "*like" } };
            if (!args.ok())
                return nullptr;
            std::int64_t n = 0, m = 0, k = 0;
            if (!to_int(args[0], n))
                return nullptr;
            m = n;
            if (args.has(1) && !to_int(args[1], m)) return nullptr;
            if (args.has(2) && !to_int(args[2], k)) return nullptr;
            std::optional<dtype> t;
            if (!optional_dtype(args, 3, t))
                return nullptr;
            auto* const a = new_array(t.value_or(dtype::Float64), { n, m });
            for (std::int64_t i = 0; i < n; ++i)
                for (std::int64_t j = 0; j < m; ++j)
                    if (j - i <= k)
                        write_as(a->pointer() + i * a->strides[0] + j * a->strides[1], a->type, std::int64_t{ 1 });
            return as_object(a);
        }

        // --- shape manipulation

        bool shape_arguments(object* aArgs, std::size_t aFirst, shape_t& aShape, char const* aName)
        {
            auto const items = sequence_items(aArgs);
            if (items.size() <= aFirst)
            {
                error("TypeError", std::string{ aName } + "() takes exactly 1 argument (0 given)");
                return false;
            }
            if (items.size() == aFirst + 1u)
                return to_shape(items[aFirst], aShape);
            aShape.clear();
            for (std::size_t i = aFirst; i < items.size(); ++i)
            {
                std::int64_t n = 0;
                if (!to_int(items[i], n))
                    return false;
                aShape.push_back(n);
            }
            return true;
        }

        object* f_reshape(object* aArgs, object* aKwargs)
        {
            arguments args{ "reshape", aArgs, aKwargs, { "a", "shape", "order", "*newshape", "*copy" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            shape_t shape;
            object* s = args.has(1) ? args[1] : args[3];
            if (s == nullptr)
                return error("TypeError", "reshape() missing required argument 'shape' (pos 1)");
            if (!to_shape(s, shape))
                return nullptr;
            bool const fortran = args.has(2) && as_str(args[2]) == std::optional<std::string>{ "F" };
            auto* const r = reshape(array_of(v), shape, fortran);
            return r == nullptr ? nullptr : as_object(r);
        }

        object* f_ravel(object* aArgs, object* aKwargs)
        {
            arguments args{ "ravel", aArgs, aKwargs, { "a", "order" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            bool const fortran = args.has(1) && as_str(args[1]) == std::optional<std::string>{ "F" };
            return as_object(ravel(array_of(v), fortran));
        }

        object* f_transpose(object* aArgs, object* aKwargs)
        {
            arguments args{ "transpose", aArgs, aKwargs, { "a", "axes" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto* const a = array_of(v);
            std::vector<std::size_t> axes;
            if (args.has(1))
            {
                if (!to_axes(args[1], a->ndim(), axes))
                    return nullptr;
                if (axes.size() != a->ndim())
                    return error("ValueError", "axes don't match array");
            }
            else
                for (std::size_t i = a->ndim(); i-- > 0u;)
                    axes.push_back(i);
            return as_object(transpose(a, axes));
        }

        object* f_swapaxes(object* aArgs, object* aKwargs)
        {
            arguments args{ "swapaxes", aArgs, aKwargs, { "a", "axis1", "axis2" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto* const a = array_of(v);
            std::int64_t x = 0, y = 0;
            std::size_t ax = 0u, ay = 0u;
            if (!to_int(args[1], x) || !to_int(args[2], y) || !normalize_axis(x, a->ndim(), ax) || !normalize_axis(y, a->ndim(), ay))
                return nullptr;
            std::vector<std::size_t> axes(a->ndim());
            std::iota(axes.begin(), axes.end(), 0u);
            std::swap(axes[ax], axes[ay]);
            return as_object(transpose(a, axes));
        }

        object* f_moveaxis(object* aArgs, object* aKwargs)
        {
            arguments args{ "moveaxis", aArgs, aKwargs, { "a", "source", "destination" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto* const a = array_of(v);
            std::vector<std::size_t> source, destination;
            if (!to_axes(args[1], a->ndim(), source) || !to_axes(args[2], a->ndim(), destination))
                return nullptr;
            if (source.size() != destination.size())
                return error("ValueError", "`source` and `destination` arguments must have the same number of elements");
            std::vector<std::size_t> order;
            for (std::size_t i = 0u; i < a->ndim(); ++i)
                if (std::find(source.begin(), source.end(), i) == source.end())
                    order.push_back(i);
            std::vector<std::pair<std::size_t, std::size_t>> pairs;
            for (std::size_t i = 0u; i < source.size(); ++i)
                pairs.emplace_back(destination[i], source[i]);
            std::sort(pairs.begin(), pairs.end());
            for (auto const& p : pairs)
                order.insert(order.begin() + static_cast<std::ptrdiff_t>(p.first), p.second);
            return as_object(transpose(a, order));
        }

        object* f_expand_dims(object* aArgs, object* aKwargs)
        {
            arguments args{ "expand_dims", aArgs, aKwargs, { "a", "axis" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            shape_t axes;
            if (!to_shape(args[1], axes))
                return nullptr;
            auto const ndim = array_of(v)->ndim() + axes.size();
            std::vector<std::size_t> normalized;
            for (auto x : axes)
            {
                std::size_t n = 0u;
                if (!normalize_axis(x, ndim, n))
                    return nullptr;
                normalized.push_back(n);
            }
            std::sort(normalized.begin(), normalized.end());
            ref current = v;
            for (auto n : normalized)
                current = new_ref(expand_dims(array_of(current), n));
            return current.release();
        }

        object* f_squeeze(object* aArgs, object* aKwargs)
        {
            arguments args{ "squeeze", aArgs, aKwargs, { "a", "axis" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto* const a = array_of(v);
            std::vector<std::size_t> axes;
            if (args.has(1))
            {
                if (!to_axes(args[1], a->ndim(), axes))
                    return nullptr;
                for (auto x : axes)
                    if (a->shape[x] != 1)
                        return error("ValueError", "cannot select an axis to squeeze out which has size not equal to one");
            }
            else
                for (std::size_t i = 0u; i < a->ndim(); ++i)
                    if (a->shape[i] == 1)
                        axes.push_back(i);
            shape_t shape, strides;
            for (std::size_t i = 0u; i < a->ndim(); ++i)
                if (std::find(axes.begin(), axes.end(), i) == axes.end())
                {
                    shape.push_back(a->shape[i]);
                    strides.push_back(a->strides[i]);
                }
            auto* const view = view_of(a, shape, strides, a->offset);
            view->scalar = false;
            return as_object(view);
        }

        object* f_concatenate(object* aArgs, object* aKwargs)
        {
            arguments args{ "concatenate", aArgs, aKwargs, { "arrays", "axis", "out", "*dtype", "*casting" } };
            if (!args.ok())
                return nullptr;
            std::vector<ref> arrays;
            if (!array_sequence(args[0], arrays))
                return nullptr;
            std::optional<std::size_t> axis = 0u;
            if (args[1] != nullptr && kind(args[1]) == NEOS_PY_NONE)
                axis.reset();
            else if (args.has(1))
            {
                std::int64_t x = 0;
                if (!to_int(args[1], x) || arrays.empty())
                    return arrays.empty() ? error("ValueError", "need at least one array to concatenate") : nullptr;
                std::size_t n = 0u;
                if (!normalize_axis(x, array_of(arrays[0])->ndim(), n))
                    return nullptr;
                axis = n;
            }
            std::optional<dtype> t;
            if (!optional_dtype(args, 3, t))
                return nullptr;
            auto* const r = concatenate(pointers(arrays), axis, t);
            return r == nullptr ? nullptr : as_object(r);
        }

        object* stacking(char const* aName, object* aArgs, object* aKwargs, int aMode)
        {
            arguments args{ aName, aArgs, aKwargs, { "arrays", "axis", "out", "*dtype", "*casting" } };
            if (!args.ok())
                return nullptr;
            std::vector<ref> arrays;
            if (!array_sequence(args[0], arrays))
                return nullptr;
            if (arrays.empty())
                return error("ValueError", "need at least one array to stack");
            std::optional<dtype> t;
            if (!optional_dtype(args, 3, t))
                return nullptr;
            std::vector<ref> prepared;
            std::size_t axis = 0u;
            switch (aMode)
            {
            case 0: // stack
                {
                    auto const& shape = array_of(arrays[0])->shape;
                    for (auto const& a : arrays)
                        if (array_of(a)->shape != shape)
                            return error("ValueError", "all input arrays must have the same shape");
                    std::int64_t x = 0;
                    if (args.has(1) && !to_int(args[1], x))
                        return nullptr;
                    if (!normalize_axis(x, shape.size() + 1u, axis))
                        return nullptr;
                    for (auto const& a : arrays)
                        prepared.push_back(new_ref(expand_dims(array_of(a), axis)));
                }
                break;
            case 1: // vstack
                for (auto const& a : arrays)
                    prepared.push_back(new_ref(atleast(array_of(a), 2u)));
                axis = 0u;
                break;
            case 2: // hstack
                for (auto const& a : arrays)
                    prepared.push_back(new_ref(atleast(array_of(a), 1u)));
                axis = array_of(prepared[0])->ndim() == 1u ? 0u : 1u;
                break;
            case 3: // dstack
                for (auto const& a : arrays)
                    prepared.push_back(new_ref(atleast(array_of(a), 3u)));
                axis = 2u;
                break;
            default: // column_stack
                for (auto const& a : arrays)
                {
                    auto* const x = array_of(a);
                    if (x->ndim() < 2u)
                    {
                        auto* const one = atleast(x, 1u);
                        shape_t shape{ one->shape[0], 1 };
                        shape_t strides{ one->strides[0], 0 };
                        prepared.push_back(new_ref(view_of(one, shape, strides, one->offset)));
                        ctx->release(as_object(one));
                    }
                    else
                        prepared.push_back(a);
                }
                axis = 1u;
                break;
            }
            auto* const r = concatenate(pointers(prepared), axis, t);
            return r == nullptr ? nullptr : as_object(r);
        }

        object* f_stack(object* aArgs, object* aKwargs) { return stacking("stack", aArgs, aKwargs, 0); }
        object* f_vstack(object* aArgs, object* aKwargs) { return stacking("vstack", aArgs, aKwargs, 1); }
        object* f_hstack(object* aArgs, object* aKwargs) { return stacking("hstack", aArgs, aKwargs, 2); }
        object* f_dstack(object* aArgs, object* aKwargs) { return stacking("dstack", aArgs, aKwargs, 3); }
        object* f_column_stack(object* aArgs, object* aKwargs) { return stacking("column_stack", aArgs, aKwargs, 4); }

        object* atleast_n(char const* aName, object* aArgs, object* aKwargs, std::size_t aNdim)
        {
            (void)aKwargs;
            auto const items = sequence_items(aArgs);
            std::vector<object*> results;
            for (auto item : items)
            {
                auto const v = array_ref(item, {});
                if (!v)
                {
                    for (auto r : results)
                        ctx->release(r);
                    return nullptr;
                }
                results.push_back(as_object(atleast(array_of(v), aNdim)));
            }
            (void)aName;
            if (results.size() == 1u)
                return results[0];
            return make_tuple(std::move(results));
        }

        object* f_atleast_1d(object* aArgs, object* aKwargs) { return atleast_n("atleast_1d", aArgs, aKwargs, 1u); }
        object* f_atleast_2d(object* aArgs, object* aKwargs) { return atleast_n("atleast_2d", aArgs, aKwargs, 2u); }
        object* f_atleast_3d(object* aArgs, object* aKwargs) { return atleast_n("atleast_3d", aArgs, aKwargs, 3u); }

        object* splitting(char const* aName, object* aArgs, object* aKwargs, int aMode)
        {
            arguments args{ aName, aArgs, aKwargs, { "ary", "indices_or_sections", "axis" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto* const a = array_of(v);
            std::size_t axis = 0u;
            if (aMode == 2) // hsplit
                axis = a->ndim() > 1u ? 1u : 0u;
            else if (aMode == 3) // vsplit
            {
                if (a->ndim() < 2u)
                    return error("ValueError", "vsplit only works on arrays of 2 or more dimensions");
                axis = 0u;
            }
            else if (aMode == 4) // dsplit
            {
                if (a->ndim() < 3u)
                    return error("ValueError", "dsplit only works on arrays of 3 or more dimensions");
                axis = 2u;
            }
            else if (args.has(2))
            {
                std::int64_t x = 0;
                if (!to_int(args[2], x) || !normalize_axis(x, a->ndim(), axis))
                    return nullptr;
            }
            auto const n = a->shape[axis];
            std::vector<std::int64_t> bounds{ 0 };
            if (is_sequence(args[1]) || is_ndarray(args[1]))
            {
                shape_t indices;
                if (!to_shape(args[1], indices))
                    return nullptr;
                for (auto i : indices)
                    bounds.push_back(i);
                bounds.push_back(n);
            }
            else
            {
                std::int64_t sections = 0;
                if (!to_int(args[1], sections))
                    return nullptr;
                if (sections <= 0)
                    return error("ValueError", "number sections must be larger than 0.");
                if (aMode != 1 && n % sections != 0)
                    return error("ValueError", "array split does not result in an equal division");
                auto const each = n / sections;
                auto const extras = n % sections;
                std::int64_t position = 0;
                for (std::int64_t i = 0; i < sections; ++i)
                {
                    position += each + (i < extras ? 1 : 0);
                    bounds.push_back(position);
                }
            }
            std::vector<object*> parts;
            for (std::size_t i = 0u; i + 1u < bounds.size(); ++i)
            {
                auto start = std::clamp<std::int64_t>(bounds[i] < 0 ? bounds[i] + n : bounds[i], 0, n);
                auto stop = std::clamp<std::int64_t>(bounds[i + 1u] < 0 ? bounds[i + 1u] + n : bounds[i + 1u], 0, n);
                if (stop < start)
                    stop = start;
                shape_t shape = a->shape;
                shape[axis] = stop - start;
                auto const offset = static_cast<std::size_t>(static_cast<std::int64_t>(a->offset) + (stop > start ? start * a->strides[axis] : 0));
                parts.push_back(as_object(view_of(a, shape, a->strides, offset)));
            }
            return make_list(std::move(parts));
        }

        object* f_split(object* aArgs, object* aKwargs) { return splitting("split", aArgs, aKwargs, 0); }
        object* f_array_split(object* aArgs, object* aKwargs) { return splitting("array_split", aArgs, aKwargs, 1); }
        object* f_hsplit(object* aArgs, object* aKwargs) { return splitting("hsplit", aArgs, aKwargs, 2); }
        object* f_vsplit(object* aArgs, object* aKwargs) { return splitting("vsplit", aArgs, aKwargs, 3); }
        object* f_dsplit(object* aArgs, object* aKwargs) { return splitting("dsplit", aArgs, aKwargs, 4); }

        object* f_flip(object* aArgs, object* aKwargs)
        {
            arguments args{ "flip", aArgs, aKwargs, { "m", "axis" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto* const a = array_of(v);
            std::vector<std::size_t> axes;
            if (!to_axes(args[1], a->ndim(), axes))
                return nullptr;
            shape_t strides = a->strides;
            auto offset = static_cast<std::int64_t>(a->offset);
            for (auto x : axes)
            {
                if (a->shape[x] > 0)
                    offset += (a->shape[x] - 1) * a->strides[x];
                strides[x] = -strides[x];
            }
            auto* const view = view_of(a, a->shape, strides, static_cast<std::size_t>(offset));
            view->scalar = false;
            return as_object(view);
        }

        object* flip_axis(object* aArgs, std::int64_t aAxis, char const* aName)
        {
            auto const items = sequence_items(aArgs);
            if (items.size() != 1u)
                return error("TypeError", std::string{ aName } + "() takes 1 positional argument");
            ctx->retain(items[0]);
            ref const args{ make_tuple({ items[0], make_int(aAxis) }) };
            return f_flip(args.get(), nullptr);
        }

        object* f_fliplr(object* aArgs, object*) { return flip_axis(aArgs, 1, "fliplr"); }
        object* f_flipud(object* aArgs, object*) { return flip_axis(aArgs, 0, "flipud"); }

        object* f_rot90(object* aArgs, object* aKwargs)
        {
            arguments args{ "rot90", aArgs, aKwargs, { "m", "k", "axes" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            std::int64_t k = 1;
            if (args.has(1) && !to_int(args[1], k))
                return nullptr;
            auto* const a = array_of(v);
            std::size_t ax0 = 0u, ax1 = 1u;
            if (args.has(2))
            {
                std::vector<std::size_t> axes;
                if (!to_axes(args[2], a->ndim(), axes))
                    return nullptr;
                ax0 = axes.at(0);
                ax1 = axes.at(1);
            }
            k = ((k % 4) + 4) % 4;
            auto flip = [&](ref const& aArray, std::size_t aAxis)
            {
                ref const fa{ make_tuple({ (ctx->retain(aArray.get()), aArray.get()), make_int(static_cast<long long>(aAxis)) }) };
                return ref{ f_flip(fa.get(), nullptr) };
            };
            auto swap = [&](ref const& aArray)
            {
                auto* const x = array_of(aArray);
                std::vector<std::size_t> axes(x->ndim());
                std::iota(axes.begin(), axes.end(), 0u);
                std::swap(axes[ax0], axes[ax1]);
                return new_ref(transpose(x, axes));
            };
            if (k == 0)
                return as_object(view_of(a, a->shape, a->strides, a->offset));
            if (k == 2)
                return flip(flip(v, ax0), ax1).release();
            if (k == 1)
                return swap(flip(v, ax1)).release();
            return flip(swap(v), ax1).release();
        }

        object* f_roll(object* aArgs, object* aKwargs)
        {
            arguments args{ "roll", aArgs, aKwargs, { "a", "shift", "axis" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto* const a = array_of(v);
            shape_t shifts;
            if (!to_shape(args[1], shifts))
                return nullptr;
            if (!args.has(2))
            {
                // (flattened)
                auto* const flat = ravel(a);
                auto const n = flat->size();
                auto* const r = new_array(a->type, { n });
                auto const s = n == 0 ? 0 : ((shifts.at(0) % n) + n) % n;
                for (std::int64_t i = 0; i < n; ++i)
                    std::memcpy(r->pointer() + ((i + s) % n) * r->strides[0], flat->pointer() + i * flat->strides[0], a->itemsize());
                ctx->release(as_object(flat));
                auto* const shaped = reshape(r, a->shape);
                ctx->release(as_object(r));
                return as_object(shaped);
            }
            std::vector<std::size_t> axes;
            if (!to_axes(args[2], a->ndim(), axes, true))
                return nullptr;
            if (shifts.size() == 1u && axes.size() > 1u)
                shifts.assign(axes.size(), shifts[0]);
            std::vector<std::int64_t> total(a->ndim(), 0);
            for (std::size_t i = 0u; i < axes.size(); ++i)
                total[axes[i]] += shifts.at(i);
            auto* const r = new_array(a->type, a->shape);
            std::vector<std::int64_t> index(a->ndim(), 0);
            auto const n = a->size();
            for (std::int64_t flat = 0; flat < n; ++flat)
            {
                std::int64_t from = static_cast<std::int64_t>(a->offset), to = 0;
                for (std::size_t d = 0u; d < a->ndim(); ++d)
                {
                    from += index[d] * a->strides[d];
                    auto const size = a->shape[d];
                    auto const shifted = ((index[d] + total[d]) % size + size) % size;
                    to += shifted * r->strides[d];
                }
                std::memcpy(r->pointer() + to, a->data->data() + from, a->itemsize());
                for (std::size_t d = a->ndim(); d-- > 0u;)
                {
                    if (++index[d] < a->shape[d])
                        break;
                    index[d] = 0;
                }
            }
            return as_object(r);
        }

        object* f_tile(object* aArgs, object* aKwargs)
        {
            arguments args{ "tile", aArgs, aKwargs, { "A", "reps" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            shape_t reps;
            if (!to_shape(args[1], reps))
                return nullptr;
            auto* const a = array_of(v);
            auto const ndim = std::max(a->ndim(), reps.size());
            shape_t shape(ndim - a->ndim(), 1), strides(ndim - a->ndim(), 0);
            shape.insert(shape.end(), a->shape.begin(), a->shape.end());
            strides.insert(strides.end(), a->strides.begin(), a->strides.end());
            reps.insert(reps.begin(), ndim - reps.size(), 1);
            shape_t resultShape(ndim);
            for (std::size_t i = 0u; i < ndim; ++i)
                resultShape[i] = shape[i] * reps[i];
            auto* const r = new_array(a->type, resultShape);
            std::vector<std::int64_t> index(ndim, 0);
            auto const n = r->size();
            auto* to = r->pointer();
            for (std::int64_t flat = 0; flat < n; ++flat, to += r->itemsize())
            {
                std::int64_t from = static_cast<std::int64_t>(a->offset);
                for (std::size_t d = 0u; d < ndim; ++d)
                    from += (index[d] % shape[d]) * strides[d];
                std::memcpy(to, a->data->data() + from, a->itemsize());
                for (std::size_t d = ndim; d-- > 0u;)
                {
                    if (++index[d] < resultShape[d])
                        break;
                    index[d] = 0;
                }
            }
            return as_object(r);
        }

        object* f_repeat(object* aArgs, object* aKwargs)
        {
            arguments args{ "repeat", aArgs, aKwargs, { "a", "repeats", "axis" } };
            if (!args.ok())
                return nullptr;
            auto v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            std::optional<std::size_t> axis;
            if (!args.has(2))
                v = new_ref(ravel(array_of(v)));
            if (!optional_axis(args, 2, array_of(v)->ndim(), axis))
                return nullptr;
            auto* const a = array_of(v);
            auto const ax = axis.value_or(0u);
            shape_t repeats;
            if (!to_shape(args[1], repeats))
                return nullptr;
            auto const n = a->shape[ax];
            if (repeats.size() == 1u)
                repeats.assign(static_cast<std::size_t>(n), repeats[0]);
            if (static_cast<std::int64_t>(repeats.size()) != n)
                return error("ValueError", "operands could not be broadcast together with shape (" + std::to_string(n) + ",) (" + std::to_string(repeats.size()) + ",)");
            shape_t shape = a->shape;
            shape[ax] = std::accumulate(repeats.begin(), repeats.end(), std::int64_t{ 0 });
            auto* const r = new_array(a->type, shape);
            std::int64_t position = 0;
            for (std::int64_t i = 0; i < n; ++i)
                for (std::int64_t k = 0; k < repeats[static_cast<std::size_t>(i)]; ++k, ++position)
                {
                    shape_t sub = a->shape;
                    sub.erase(sub.begin() + static_cast<std::ptrdiff_t>(ax));
                    shape_t fromStrides = a->strides, toStrides = r->strides;
                    fromStrides.erase(fromStrides.begin() + static_cast<std::ptrdiff_t>(ax));
                    toStrides.erase(toStrides.begin() + static_cast<std::ptrdiff_t>(ax));
                    iterate(sub, { a->pointer() + i * a->strides[ax], r->pointer() + position * r->strides[ax] }, { fromStrides, toStrides },
                        [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
                        {
                            auto from = aPointers[0];
                            auto to = aPointers[1];
                            for (std::int64_t j = 0; j < aCount; ++j, from += aStrides[0], to += aStrides[1])
                                std::memcpy(to, from, a->itemsize());
                        });
                }
            return as_object(r);
        }

        object* f_broadcast_to(object* aArgs, object* aKwargs)
        {
            arguments args{ "broadcast_to", aArgs, aKwargs, { "array", "shape", "subok" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            shape_t shape;
            if (!to_shape(args[1], shape))
                return nullptr;
            auto* const a = array_of(v);
            shape_t check;
            if (!broadcast_shapes({ &a->shape, &shape }, check) || check != shape)
            {
                if (!failed())
                    error("ValueError", "operands could not be broadcast together with remapped shapes [original->remapped]: " + shape_repr(a->shape) + " and requested shape " + shape_repr(shape));
                return nullptr;
            }
            auto* const view = view_of(a, shape, broadcast_strides(*a, shape), a->offset);
            view->writeable = false;
            view->scalar = false;
            return as_object(view);
        }

        object* f_broadcast_arrays(object* aArgs, object*)
        {
            std::vector<ref> arrays;
            for (auto item : sequence_items(aArgs))
            {
                arrays.push_back(array_ref(item, {}));
                if (!arrays.back())
                    return nullptr;
            }
            std::vector<shape_t const*> shapes;
            for (auto const& a : arrays)
                shapes.push_back(&array_of(a)->shape);
            shape_t shape;
            if (!broadcast_shapes(shapes, shape))
                return nullptr;
            std::vector<object*> results;
            for (auto const& a : arrays)
            {
                auto* const x = array_of(a);
                auto* const view = view_of(x, shape, broadcast_strides(*x, shape), x->offset);
                view->scalar = false;
                results.push_back(as_object(view));
            }
            return make_tuple(std::move(results));
        }

        object* f_append(object* aArgs, object* aKwargs)
        {
            arguments args{ "append", aArgs, aKwargs, { "arr", "values", "axis" } };
            if (!args.ok())
                return nullptr;
            auto a = array_ref(args[0], {});
            auto b = array_ref(args[1], {});
            if (!a || !b)
                return nullptr;
            std::optional<std::size_t> axis;
            if (args.has(2))
            {
                if (!optional_axis(args, 2, array_of(a)->ndim(), axis))
                    return nullptr;
            }
            else
            {
                a = new_ref(ravel(array_of(a)));
                b = new_ref(ravel(array_of(b)));
                axis = 0u;
            }
            auto* const r = concatenate({ array_of(a), array_of(b) }, axis, {});
            return r == nullptr ? nullptr : as_object(r);
        }

        object* f_delete(object* aArgs, object* aKwargs)
        {
            arguments args{ "delete", aArgs, aKwargs, { "arr", "obj", "axis" } };
            if (!args.ok())
                return nullptr;
            auto v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            if (!args.has(2))
                v = new_ref(ravel(array_of(v)));
            std::optional<std::size_t> axis;
            if (!optional_axis(args, 2, array_of(v)->ndim(), axis))
                return nullptr;
            auto* const a = array_of(v);
            auto const ax = axis.value_or(0u);
            auto const n = a->shape[ax];
            std::vector<bool> keep(static_cast<std::size_t>(n), true);
            if (kind(args[1]) == NEOS_PY_SLICE)
            {
                ref const r{ ctx->call(ctx, ref{ ctx->getattr(ctx, args[1], "indices") }.get(), ref{ make_tuple({ make_int(n) }) }.get(), nullptr) };
                std::int64_t start = 0, stop = 0, step = 1;
                auto const parts = sequence_items(r.get());
                to_int(parts[0], start); to_int(parts[1], stop); to_int(parts[2], step);
                for (auto i = start; step > 0 ? i < stop : i > stop; i += step)
                    keep[static_cast<std::size_t>(i)] = false;
            }
            else
            {
                shape_t indices;
                if (!to_shape(args[1], indices))
                    return nullptr;
                for (auto i : indices)
                {
                    if (i < -n || i >= n)
                        return error("IndexError", "index " + std::to_string(i) + " is out of bounds for axis " + std::to_string(ax) + " with size " + std::to_string(n));
                    keep[static_cast<std::size_t>(i < 0 ? i + n : i)] = false;
                }
            }
            std::vector<object*> kept;
            for (std::int64_t i = 0; i < n; ++i)
                if (keep[static_cast<std::size_t>(i)])
                    kept.push_back(make_int(i));
            ref const indexArray{ as_object(to_array(ref{ make_list(std::move(kept)) }.get(), { dtype::Int64 })) };
            ref const takeArgs{ make_tuple({ (ctx->retain(as_object(a)), as_object(a)), (ctx->retain(indexArray.get()), indexArray.get()), make_int(static_cast<long long>(ax)) }) };
            return call_function("take", takeArgs.get(), nullptr);
        }

        // (copy aFrom into aTo, the same shape, casting)
        void copy_into(ndarray const& aTo, ndarray const& aFrom)
        {
            iterate(aTo.shape, { aFrom.pointer(), aTo.pointer() }, { aFrom.strides, aTo.strides }, [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
            {
                auto p = aPointers[0], q = aPointers[1];
                for (std::int64_t i = 0; i < aCount; ++i, p += aStrides[0], q += aStrides[1])
                    cast_element(p, aFrom.type, q, aTo.type);
            });
        }

        // np.insert with a sequence of positions
        object* insert_sequence(ndarray* a, std::size_t ax, object* aIndices, object* aValues)
        {
            auto const iv = array_ref(aIndices, {});
            if (!iv)
                return nullptr;
            auto* const ia = array_of(iv);
            if (ia->ndim() > 1u)
                return error("ValueError", "index array argument obj to insert must be one dimensional or scalar");
            if (ia->size() > 0 && !is_integer(ia->type))
                return error("IndexError", "arrays used as indices must be of integer (or boolean) type");
            auto const n = a->shape[ax];
            std::vector<std::int64_t> indices;
            for_each(*ia, [&](unsigned char* p) { indices.push_back(read_as<std::int64_t>(p, ia->type)); });
            for (auto& i : indices)
            {
                if (i < -n || i > n)
                    return error("IndexError", "index " + std::to_string(i) + " is out of bounds for axis " + std::to_string(ax) + " with size " + std::to_string(n));
                if (i < 0)
                    i += n;
            }
            auto const numnew = static_cast<std::int64_t>(indices.size());
            std::vector<std::size_t> order(indices.size());
            std::iota(order.begin(), order.end(), 0u);
            std::stable_sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return indices[x] < indices[y]; });
            for (std::size_t k = 0u; k < order.size(); ++k)
                indices[order[k]] += static_cast<std::int64_t>(k);
            shape_t shape = a->shape;
            shape[ax] += numnew;
            auto const values = array_ref(aValues, a->type);
            if (!values)
                return nullptr;
            shape_t valuesShape = a->shape;
            valuesShape[ax] = numnew;
            shape_t check;
            if (!broadcast_shapes({ &array_of(values)->shape, &valuesShape }, check))
                return nullptr;
            if (check != valuesShape)
                return error("ValueError", "shape mismatch: value array of shape " + shape_repr(array_of(values)->shape) + " could not be broadcast to indexing result of shape " + shape_repr(valuesShape));
            ref const vb = new_ref(view_of(array_of(values), valuesShape, broadcast_strides(*array_of(values), valuesShape), array_of(values)->offset));
            auto* const r = new_array(a->type, shape);
            std::vector<std::int64_t> source(static_cast<std::size_t>(shape[ax]), 0);
            std::vector<bool> isNew(source.size(), false);
            for (std::size_t k = 0u; k < indices.size(); ++k)
            {
                source[static_cast<std::size_t>(indices[k])] = static_cast<std::int64_t>(k);
                isNew[static_cast<std::size_t>(indices[k])] = true;
            }
            std::int64_t old = 0;
            for (std::size_t p = 0u; p < source.size(); ++p)
                if (!isNew[p])
                    source[p] = old++;
            shape_t sliceShape = shape;
            sliceShape[ax] = 1;
            for (std::int64_t p = 0; p < shape[ax]; ++p)
            {
                auto const fromIndex = source[static_cast<std::size_t>(p)];
                ref const to = new_ref(view_of(r, sliceShape, r->strides, static_cast<std::size_t>(static_cast<std::int64_t>(r->offset) + p * r->strides[ax])));
                ndarray* const from = isNew[static_cast<std::size_t>(p)] ? array_of(vb) : a;
                ref const fromView = new_ref(view_of(from, sliceShape, from->strides, static_cast<std::size_t>(static_cast<std::int64_t>(from->offset) + fromIndex * from->strides[ax])));
                copy_into(*array_of(to), *array_of(fromView));
            }
            return as_object(r);
        }

        object* f_insert(object* aArgs, object* aKwargs)
        {
            arguments args{ "insert", aArgs, aKwargs, { "arr", "obj", "values", "axis" } };
            if (!args.ok())
                return nullptr;
            auto v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            if (!args.has(3))
                v = new_ref(ravel(array_of(v)));
            std::optional<std::size_t> axis;
            if (!optional_axis(args, 3, array_of(v)->ndim(), axis))
                return nullptr;
            auto* const a = array_of(v);
            auto const ax = axis.value_or(0u);
            auto const n = a->shape[ax];
            std::int64_t position = 0;
            if (is_sequence(args[1]) || is_ndarray(args[1]))
                return insert_sequence(a, ax, args[1], args[2]);
            if (!to_int(args[1], position))
                return nullptr;
            if (position < -n || position > n)
                return error("IndexError", "index " + std::to_string(position) + " is out of bounds for axis " + std::to_string(ax) + " with size " + std::to_string(n));
            if (position < 0)
                position += n;
            auto const values = array_ref(args[2], a->type);
            if (!values)
                return nullptr;
            // (the values broadcast to the shape of one slice along the axis)
            shape_t sliceShape = a->shape;
            sliceShape[ax] = 1;
            auto* const valuesArray = array_of(values);
            shape_t check;
            ref inserted;
            if (valuesArray->ndim() == a->ndim() && broadcast_shapes({ &valuesArray->shape, &sliceShape }, check))
            {
                auto shape = sliceShape;
                shape[ax] = valuesArray->shape[ax];
                inserted = new_ref(view_of(valuesArray, shape, broadcast_strides(*valuesArray, shape), valuesArray->offset));
            }
            else
            {
                if (failed())
                    return nullptr;
                shape_t shape = sliceShape;
                inserted = new_ref(view_of(valuesArray, shape, broadcast_strides(*valuesArray, shape), valuesArray->offset));
            }
            auto before = shape_t(a->shape);
            before[ax] = position;
            auto after = shape_t(a->shape);
            after[ax] = n - position;
            ref const first{ as_object(view_of(a, before, a->strides, a->offset)) };
            ref const second{ as_object(view_of(a, after, a->strides, static_cast<std::size_t>(static_cast<std::int64_t>(a->offset) + position * a->strides[ax]))) };
            auto* const r = concatenate({ array_of(first), array_of(inserted), array_of(second) }, ax, a->type);
            return r == nullptr ? nullptr : as_object(r);
        }

        object* f_shape(object* aArgs, object* aKwargs)
        {
            arguments args{ "shape", aArgs, aKwargs, { "a" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            std::vector<object*> items;
            for (auto d : array_of(v)->shape)
                items.push_back(make_int(d));
            return make_tuple(std::move(items));
        }

        object* f_ndim(object* aArgs, object* aKwargs)
        {
            arguments args{ "ndim", aArgs, aKwargs, { "a" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            return v ? make_int(static_cast<long long>(array_of(v)->ndim())) : nullptr;
        }

        object* f_size(object* aArgs, object* aKwargs)
        {
            arguments args{ "size", aArgs, aKwargs, { "a", "axis" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            if (args.has(1))
            {
                std::optional<std::size_t> axis;
                if (!optional_axis(args, 1, array_of(v)->ndim(), axis))
                    return nullptr;
                return make_int(array_of(v)->shape[*axis]);
            }
            return make_int(array_of(v)->size());
        }

        object* f_result_type(object* aArgs, object*)
        {
            auto const t = result_type(sequence_items(aArgs));
            if (!t)
                return nullptr;
            return dtype_object(*t);
        }

        object* f_promote_types(object* aArgs, object* aKwargs)
        {
            arguments args{ "promote_types", aArgs, aKwargs, { "type1", "type2" } };
            if (!args.ok())
                return nullptr;
            auto const a = parse_dtype(args[0]);
            auto const b = parse_dtype(args[1]);
            if (!a || !b)
                return nullptr;
            return dtype_object(promote(*a, *b));
        }

        object* f_can_cast(object* aArgs, object* aKwargs)
        {
            arguments args{ "can_cast", aArgs, aKwargs, { "from_", "to", "casting" } };
            if (!args.ok())
                return nullptr;
            std::optional<dtype> from;
            if (is_array(args[0]))
                from = as_array(args[0])->type;
            else if (!(from = parse_dtype(args[0])))
                return nullptr;
            auto const to = parse_dtype(args[1]);
            if (!to)
                return nullptr;
            std::string const casting = args.has(2) ? as_str(args[2]).value_or("safe") : "safe";
            return make_bool(can_cast(*from, *to, casting));
        }

        object* f_dtype(object* aArgs, object* aKwargs)
        {
            arguments args{ "dtype", aArgs, aKwargs, { "dtype", "align", "copy" } };
            if (!args.ok())
                return nullptr;
            auto const t = parse_dtype(args[0]);
            if (!t)
                return nullptr;
            return dtype_object(*t);
        }

        object* f_isscalar(object* aArgs, object* aKwargs)
        {
            arguments args{ "isscalar", aArgs, aKwargs, { "element" } };
            if (!args.ok())
                return nullptr;
            auto const k = kind(args[0]);
            return make_bool(is_numpy_scalar(args[0]) || k == NEOS_PY_BOOL || k == NEOS_PY_INT || k == NEOS_PY_FLOAT || k == NEOS_PY_COMPLEX || k == NEOS_PY_STR);
        }

        object* f_iscomplexobj(object* aArgs, object* aKwargs)
        {
            arguments args{ "iscomplexobj", aArgs, aKwargs, { "x" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            return v ? make_bool(is_complex(array_of(v)->type)) : nullptr;
        }

        object* f_isrealobj(object* aArgs, object* aKwargs)
        {
            arguments args{ "isrealobj", aArgs, aKwargs, { "x" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            return v ? make_bool(!is_complex(array_of(v)->type)) : nullptr;
        }

        object* f_meshgrid(object* aArgs, object* aKwargs)
        {
            arguments args{ "meshgrid", aArgs, aKwargs, { "...", "*copy", "*sparse", "*indexing" } };
            if (!args.ok())
                return nullptr;
            bool const ij = args.has(3) && as_str(args[3]) == std::optional<std::string>{ "ij" };
            bool sparse = false;
            if (!optional_bool(args, 2, false, sparse))
                return nullptr;
            std::vector<ref> inputs;
            for (auto x : args.extra())
            {
                inputs.push_back(new_ref(ravel(array_of(array_ref(x, {})))));
                if (!inputs.back())
                    return nullptr;
            }
            auto const n = inputs.size();
            shape_t shape;
            for (auto const& x : inputs)
                shape.push_back(array_of(x)->shape[0]);
            if (!ij && n >= 2u)
                std::swap(shape[0], shape[1]);
            std::vector<object*> results;
            for (std::size_t i = 0u; i < n; ++i)
            {
                auto* const x = array_of(inputs[i]);
                std::size_t axis = i;
                if (!ij && n >= 2u && i < 2u)
                    axis = 1u - i;
                shape_t s(n, 1), strides(n, 0);
                s[axis] = x->shape[0];
                strides[axis] = x->strides[0];
                if (sparse)
                {
                    ref const reshaped{ as_object(reshape(x, s)) };
                    results.push_back(as_object(copy_array(*as_array(reshaped.get()))));
                }
                else
                {
                    ref const view{ as_object(view_of(x, shape, [&]() { shape_t st(n, 0); st[axis] = x->strides[0]; return st; }(), x->offset)) };
                    results.push_back(as_object(copy_array(*as_array(view.get()))));
                }
            }
            return make_tuple(std::move(results));
        }

        object* f_indices(object* aArgs, object* aKwargs)
        {
            arguments args{ "indices", aArgs, aKwargs, { "dimensions", "dtype", "sparse" } };
            if (!args.ok())
                return nullptr;
            shape_t dims;
            if (!to_shape(args[0], dims))
                return nullptr;
            std::optional<dtype> t;
            if (!optional_dtype(args, 1, t))
                return nullptr;
            shape_t shape{ static_cast<std::int64_t>(dims.size()) };
            shape.insert(shape.end(), dims.begin(), dims.end());
            auto* const r = new_array(t.value_or(dtype::Int64), shape);
            auto const n = element_count(dims);
            auto* p = r->pointer();
            for (std::size_t d = 0u; d < dims.size(); ++d)
            {
                std::vector<std::int64_t> index(dims.size(), 0);
                for (std::int64_t i = 0; i < n; ++i, p += r->itemsize())
                {
                    write_as(p, r->type, index[d]);
                    for (std::size_t k = dims.size(); k-- > 0u;)
                    {
                        if (++index[k] < dims[k])
                            break;
                        index[k] = 0;
                    }
                }
            }
            return as_object(r);
        }
    }

    // --- the function table

    namespace
    {
        struct function_entry
        {
            char const* name;
            object* (*function)(object* aArgs, object* aKwargs);
        };

        std::deque<function_entry>& function_table()
        {
            static std::deque<function_entry> sTable =
            {
                { "array", f_np_array }, { "asarray", f_asarray }, { "asanyarray", f_asarray }, { "ascontiguousarray", f_ascontiguousarray },
                { "copy", f_copy }, { "zeros", f_zeros }, { "ones", f_ones }, { "empty", f_empty }, { "full", f_full },
                { "zeros_like", f_zeros_like }, { "ones_like", f_ones_like }, { "empty_like", f_empty_like }, { "full_like", f_full_like },
                { "arange", f_arange }, { "linspace", f_linspace }, { "logspace", f_logspace }, { "geomspace", f_geomspace },
                { "eye", f_eye }, { "identity", f_identity }, { "diag", f_diag }, { "diagonal", f_diagonal }, { "tril", f_tril }, { "triu", f_triu },
                { "tri", f_tri }, { "reshape", f_reshape }, { "ravel", f_ravel }, { "transpose", f_transpose }, { "permute_dims", f_transpose },
                { "swapaxes", f_swapaxes }, { "moveaxis", f_moveaxis }, { "expand_dims", f_expand_dims }, { "squeeze", f_squeeze },
                { "concatenate", f_concatenate }, { "concat", f_concatenate }, { "stack", f_stack }, { "vstack", f_vstack }, { "row_stack", f_vstack },
                { "hstack", f_hstack }, { "dstack", f_dstack }, { "column_stack", f_column_stack }, { "atleast_1d", f_atleast_1d },
                { "atleast_2d", f_atleast_2d }, { "atleast_3d", f_atleast_3d }, { "split", f_split }, { "array_split", f_array_split },
                { "hsplit", f_hsplit }, { "vsplit", f_vsplit }, { "dsplit", f_dsplit }, { "flip", f_flip }, { "fliplr", f_fliplr },
                { "flipud", f_flipud }, { "rot90", f_rot90 }, { "roll", f_roll }, { "tile", f_tile }, { "repeat", f_repeat },
                { "broadcast_to", f_broadcast_to }, { "broadcast_arrays", f_broadcast_arrays }, { "append", f_append }, { "delete", f_delete },
                { "insert", f_insert }, { "shape", f_shape }, { "ndim", f_ndim }, { "size", f_size }, { "result_type", f_result_type },
                { "promote_types", f_promote_types }, { "can_cast", f_can_cast }, { "dtype", f_dtype }, { "isscalar", f_isscalar },
                { "iscomplexobj", f_iscomplexobj }, { "isrealobj", f_isrealobj }, { "meshgrid", f_meshgrid }, { "indices", f_indices }
            };
            return sTable;
        }

        object* call_entry(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
        {
            return py::guard(static_cast<function_entry const*>(aData)->function, aArgs, aKwargs);
        }
    }

    void register_functions(std::vector<std::pair<char const*, object* (*)(object*, object*)>> const& aFunctions)
    {
        for (auto const& f : aFunctions)
            function_table().push_back({ f.first, f.second });
    }

    object* call_function(std::string_view aName, object* aArgs, object* aKwargs)
    {
        for (auto const& f : function_table())
            if (aName == f.name)
                return py::guard(f.function, aArgs, aKwargs);
        if (auto const* u = find_ufunc(aName))
            return call_ufunc(*u, aArgs, aKwargs);
        return error("AttributeError", "module 'numpy' has no attribute '" + std::string{ aName } + "'");
    }

    object* make_function_object(char const* aName)
    {
        static std::map<std::string, object*> sObjects;
        auto& o = sObjects[aName];
        if (o == nullptr)
        {
            for (auto const& f : function_table())
                if (std::string_view{ aName } == f.name)
                    o = ctx->function_new(ctx, f.name, call_entry, const_cast<function_entry*>(&f));
            if (o == nullptr)
                return nullptr;
        }
        ctx->retain(o);
        return o;
    }

    bool has_function(std::string_view aName)
    {
        for (auto const& f : function_table())
            if (aName == f.name)
                return true;
        return false;
    }
}
