/*
  methods.cpp

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

// the attributes and methods of ndarray (and of NumPy scalars)

#include <map>
#include "numpy.hpp"
#include "cast.hpp"
#include "functions.hpp"

namespace neos::numpy
{
    object* array_flags(ndarray* aArray);
    object* array_flat(object* aArray);

    namespace
    {
        object* shape_tuple(shape_t const& aShape)
        {
            std::vector<object*> items;
            for (auto d : aShape)
                items.push_back(make_int(d));
            return make_tuple(std::move(items));
        }

        // (the args tuple with aSelf, as an array, first)
        ref with_self(object* aSelf, object* aArgs)
        {
            std::vector<object*> items{ (ctx->retain(aSelf), aSelf) };
            for (auto x : sequence_items(aArgs))
                items.push_back((ctx->retain(x), x));
            return ref{ make_tuple(std::move(items)) };
        }

        // (varargs as a single tuple argument: a.reshape(2, 3), a.transpose(1, 0))
        ref packed(object* aSelf, object* aArgs)
        {
            auto const items = sequence_items(aArgs);
            if (items.size() <= 1u)
                return with_self(aSelf, aArgs);
            std::vector<object*> rest;
            for (auto x : items)
                rest.push_back((ctx->retain(x), x));
            return ref{ make_tuple({ (ctx->retain(aSelf), aSelf), make_tuple(std::move(rest)) }) };
        }

        object* tolist(ndarray const& aArray, unsigned char* aPointer, std::size_t aDimension)
        {
            if (aDimension == aArray.ndim())
            {
                ref const s{ scalar_from(aArray, aPointer) };
                return to_python(*as_array(s.get()));
            }
            std::vector<object*> items;
            for (std::int64_t i = 0; i < aArray.shape[aDimension]; ++i)
                items.push_back(tolist(aArray, aPointer + i * aArray.strides[aDimension], aDimension + 1u));
            return make_list(std::move(items));
        }

        object* method_entry(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
        {
            auto const items = sequence_items(aArgs);
            auto const self = items.at(0);
            std::vector<object*> rest;
            for (std::size_t i = 1u; i < items.size(); ++i)
                rest.push_back((ctx->retain(items[i]), items[i]));
            ref const args{ make_tuple(std::move(rest)) };
            return method_call(static_cast<char const*>(aData), self, args.get(), aKwargs);
        }

        char const* const sMethods[] =
        {
            "all", "any", "argmax", "argmin", "argsort", "astype", "clip", "conj", "conjugate", "copy", "cumprod", "cumsum", "diagonal", "dot",
            "fill", "flatten", "item", "max", "mean", "min", "nonzero", "prod", "ravel", "repeat", "reshape", "round", "searchsorted", "sort",
            "squeeze", "std", "sum", "swapaxes", "take", "tolist", "trace", "transpose", "var", "view", "tobytes", "is_integer", "__array__",
            "__len__", "__getitem__", "__setitem__", "__contains__", "__iter__", "__repr__", "__str__", "choose", "compress", "put", "resize", "setflags",
            "byteswap", "to_device", "partition", "argpartition", "searchsorted", "__copy__", "__deepcopy__"
        };

        char const* method_name(std::string_view aName)
        {
            for (auto m : sMethods)
                if (aName == m)
                    return m;
            return nullptr;
        }
    }

    object* array_getattr(object* aSelf, char const* aName)
    {
        auto* const a = as_array(aSelf);
        std::string_view const name{ aName };
        if (name == "shape")
            return shape_tuple(a->shape);
        if (name == "strides")
            return shape_tuple(a->strides);
        if (name == "dtype")
        {
            auto const d = dtype_object(a->type);
            ctx->retain(d);
            return d;
        }
        if (name == "ndim")
            return make_int(static_cast<long long>(a->ndim()));
        if (name == "size")
            return make_int(a->size());
        if (name == "itemsize")
            return make_int(static_cast<long long>(a->itemsize()));
        if (name == "nbytes")
            return make_int(a->size() * static_cast<std::int64_t>(a->itemsize()));
        if (name == "T" || name == "mT")
        {
            if (a->scalar)
                return (ctx->retain(aSelf), aSelf);
            std::vector<std::size_t> axes;
            for (std::size_t i = 0u; i < a->ndim(); ++i)
                axes.push_back(i);
            if (name == "T")
                std::reverse(axes.begin(), axes.end());
            else
            {
                if (a->ndim() < 2u)
                    return error("ValueError", "matrix transpose with ndim < 2 is undefined");
                std::swap(axes[a->ndim() - 1u], axes[a->ndim() - 2u]);
            }
            return as_object(transpose(a, axes));
        }
        if (name == "real" || name == "imag")
        {
            if (is_complex(a->type))
            {
                auto const t = real_for(a->type);
                auto* const v = new_view(a, t, a->offset + (name == "imag" ? info(t).itemsize : 0u), a->shape, a->strides);
                if (a->scalar)
                {
                    auto* const s = new_scalar(t, load(v->pointer(), t));
                    ctx->release(as_object(v));
                    return as_object(s);
                }
                return as_object(v);
            }
            if (name == "real")
                return a->scalar ? (ctx->retain(aSelf), aSelf) : as_object(new_view(a, a->type, a->offset, a->shape, a->strides));
            auto* const z = new_array(a->type, a->shape);
            if (a->scalar)
            {
                auto* const s = new_scalar(a->type, load(z->pointer(), a->type));
                ctx->release(as_object(z));
                return as_object(s);
            }
            z->writeable = false;
            return as_object(z);
        }
        if (name == "flat")
            return array_flat(aSelf);
        if (name == "device")
            return make_str("cpu");
        if (name == "flags")
            return array_flags(a);
        if (name == "base")
            return new_none();
        if (name == "__array_priority__")
            return make_float(0.0);
        if (auto const m = method_name(name))
            return ctx->method_new(ctx, m, method_entry, const_cast<char*>(m), aSelf);
        if (name == "ptp" || name == "newbyteorder" || name == "itemset")
            return error("AttributeError", std::string{ "`" } + aName + "` was removed from the ndarray class in NumPy 2.0. Use np." + aName + "(arr, ...) instead.");
        return error("AttributeError", std::string{ "'numpy." } + (a->scalar ? info(a->type).scalarName : "ndarray") + "' object has no attribute '" + aName + "'");
    }

    object* reduce(std::string_view aName, ndarray* aArray, object* aArgs, object* aKwargs)
    {
        ref const args{ with_self(as_object(aArray), aArgs) };
        return call_function(aName, args.get(), aKwargs);
    }

    object* method_call(std::string_view aName, object* aSelf, object* aArgs, object* aKwargs)
    {
        auto* const a = as_array(aSelf);
        auto const items = sequence_items(aArgs);
        if (aName == "reshape" || aName == "transpose")
        {
            if (aName == "transpose" && items.empty())
                return array_getattr(aSelf, "T");
            ref const args{ packed(aSelf, aArgs) };
            return call_function(aName, args.get(), aKwargs);
        }
        if (aName == "astype")
        {
            arguments args{ "astype", aArgs, aKwargs, { "dtype", "order", "casting", "subok", "copy" } };
            if (!args.ok())
                return nullptr;
            auto const t = parse_dtype(args[0]);
            if (!t)
                return nullptr;
            if (args.has(2))
            {
                auto const casting = as_str(args[2]).value_or("unsafe");
                if (!can_cast(a->type, *t, casting))
                    return error("TypeError", std::string{ "Cannot cast array data from dtype('" } + info(a->type).name + "') to dtype('" + info(*t).name + "') according to the rule '" + casting + "'");
            }
            auto* const r = copy_array(*a, *t);
            if (a->scalar)
                return result_object(r);
            return as_object(r);
        }
        if (aName == "copy" || aName == "flatten")
        {
            arguments args{ aName == "copy" ? "copy" : "flatten", aArgs, aKwargs, { "order" } };
            if (!args.ok())
                return nullptr;
            bool const fortran = args.has(0) && as_str(args[0]) == std::optional<std::string>{ "F" };
            auto* const r = copy_array(*a, {}, fortran);
            if (aName == "flatten")
            {
                auto* const flat = ravel(r, fortran);
                ctx->release(as_object(r));
                return as_object(flat);
            }
            if (a->scalar)
                return result_object(r);
            return as_object(r);
        }
        if (aName == "tolist")
        {
            if (!items.empty())
                return error("TypeError", "tolist() takes no arguments");
            return tolist(*a, a->pointer(), 0u);
        }
        if (aName == "item")
        {
            if (items.empty())
            {
                if (a->size() != 1)
                    return error("ValueError", "can only convert an array of size 1 to a Python scalar");
                ref const s{ scalar_from(*a, a->pointer()) };
                return to_python(*as_array(s.get()));
            }
            std::int64_t offset = 0;
            if (items.size() == 1u && kind(items[0]) != NEOS_PY_TUPLE)
            {
                std::int64_t flat = 0;
                if (!to_int(items[0], flat))
                    return nullptr;
                auto const n = a->size();
                if (flat < -n || flat >= n)
                    return error("IndexError", "index " + std::to_string(flat) + " is out of bounds for size " + std::to_string(n));
                if (flat < 0)
                    flat += n;
                for (std::size_t d = a->ndim(); d-- > 0u;)
                {
                    offset += (flat % a->shape[d]) * a->strides[d];
                    flat /= a->shape[d];
                }
            }
            else
            {
                auto const indices = items.size() == 1u ? sequence_items(items[0]) : items;
                if (indices.size() != a->ndim())
                    return error("ValueError", "incorrect number of indices for array");
                for (std::size_t d = 0u; d < a->ndim(); ++d)
                {
                    std::int64_t i = 0;
                    if (!to_int(indices[d], i))
                        return nullptr;
                    if (i < -a->shape[d] || i >= a->shape[d])
                        return error("IndexError", "index " + std::to_string(i) + " is out of bounds for axis " + std::to_string(d) + " with size " + std::to_string(a->shape[d]));
                    if (i < 0)
                        i += a->shape[d];
                    offset += i * a->strides[d];
                }
            }
            ref const s{ scalar_from(*a, a->pointer() + offset) };
            return to_python(*as_array(s.get()));
        }
        if (aName == "fill")
        {
            if (items.size() != 1u)
                return error("TypeError", "fill() takes exactly one argument");
            ref const key{ ctx->ellipsis(ctx) };
            if (array_setitem(aSelf, key.get(), items[0]) != 0)
                return nullptr;
            return new_none();
        }
        if (aName == "sort")
        {
            if (a->scalar)
                return error("AttributeError", std::string{ "'numpy." } + info(a->type).scalarName + "' object has no attribute 'sort'");
            ref const args{ with_self(aSelf, aArgs) };
            ref const sorted{ call_function("sort", args.get(), aKwargs) };
            if (!sorted)
                return nullptr;
            ref const key{ ctx->ellipsis(ctx) };
            if (array_setitem(aSelf, key.get(), sorted.get()) != 0)
                return nullptr;
            return new_none();
        }
        if (aName == "partition")
        {
            ref const args{ with_self(aSelf, aArgs) };
            ref const partitioned{ call_function("partition", args.get(), aKwargs) };
            if (!partitioned)
                return nullptr;
            ref const key{ ctx->ellipsis(ctx) };
            if (array_setitem(aSelf, key.get(), partitioned.get()) != 0)
                return nullptr;
            return new_none();
        }
        if (aName == "conj" || aName == "conjugate")
            return apply_unary(*find_ufunc("conjugate"), aSelf);
        if (aName == "view")
        {
            if (items.empty() && aKwargs == nullptr)
            {
                if (a->scalar)
                    return (ctx->retain(aSelf), aSelf);
                return as_object(new_view(a, a->type, a->offset, a->shape, a->strides));
            }
            arguments args{ "view", aArgs, aKwargs, { "dtype", "type" } };
            if (!args.ok())
                return nullptr;
            auto const t = parse_dtype(args[0]);
            if (!t)
                return nullptr;
            if (info(*t).itemsize == a->itemsize())
                return as_object(new_view(a, *t, a->offset, a->shape, a->strides));
            if (a->ndim() == 0 || !a->c_contiguous() || (a->shape.back() * static_cast<std::int64_t>(a->itemsize())) % static_cast<std::int64_t>(info(*t).itemsize) != 0)
                return error("ValueError", "When changing to a smaller dtype, its size must be a divisor of the size of original dtype");
            shape_t shape = a->shape;
            shape.back() = shape.back() * static_cast<std::int64_t>(a->itemsize()) / static_cast<std::int64_t>(info(*t).itemsize);
            return as_object(new_view(a, *t, a->offset, shape, contiguous_strides(shape, info(*t).itemsize)));
        }
        if (aName == "tobytes")
        {
            ref const c{ as_object(copy_array(*a)) };
            auto const* ca = as_array(c.get());
            std::string bytes(reinterpret_cast<char const*>(ca->pointer()), static_cast<std::size_t>(ca->size()) * ca->itemsize());
            return make_str(bytes);
        }
        if (aName == "is_integer")
        {
            if (!a->scalar || !is_float(a->type))
                return error("AttributeError", std::string{ "'numpy." } + (a->scalar ? info(a->type).scalarName : "ndarray") + "' object has no attribute 'is_integer'");
            auto const x = read_as<double>(a->pointer(), a->type);
            return make_bool(std::isfinite(x) && x == std::floor(x));
        }
        if (aName == "__array__")
            return as_object(new_view(a, a->type, a->offset, a->shape, a->strides));
        if (aName == "__len__")
        {
            auto const n = ctx->length(ctx, aSelf);
            return n < 0 ? nullptr : make_int(n);
        }
        if (aName == "__getitem__")
            return items.size() == 1u ? array_getitem(aSelf, items[0]) : error("TypeError", "__getitem__ expected 1 argument");
        if (aName == "__setitem__")
            return items.size() == 2u ? (array_setitem(aSelf, items[0], items[1]) == 0 ? new_none() : nullptr) : error("TypeError", "__setitem__ expected 2 arguments");
        if (aName == "__repr__")
            return ctx->repr(ctx, aSelf);
        if (aName == "__str__")
            return ctx->str(ctx, aSelf);
        if (aName == "__iter__")
            return ctx->iter(ctx, aSelf);
        if (aName == "__contains__")
        {
            ref equal{ compare_operator(NEOS_PY_EQ, aSelf, items.at(0)) };
            if (!equal)
                return nullptr;
            ref const args{ make_tuple({ equal.release() }) };
            return call_function("any", args.get(), nullptr);
        }
        if (aName == "compress")
        {
            arguments args{ "compress", aArgs, aKwargs, { "condition", "axis", "out" } };
            if (!args.ok())
                return nullptr;
            ref const t{ make_tuple({ (ctx->retain(args[0]), args[0]), (ctx->retain(aSelf), aSelf), args.get(1) != nullptr ? (ctx->retain(args[1]), args[1]) : new_none() }) };
            return call_function("compress", t.get(), nullptr);
        }
        if (aName == "resize")
        {
            // (in place: the elements in C order, then zeros)
            arguments args{ "resize", aArgs, aKwargs, { "...", "*refcheck" } };
            if (!args.ok())
                return nullptr;
            shape_t shape;
            auto const& e = args.extra();
            if (e.size() == 1u)
            {
                if (!to_shape(e[0], shape))
                    return nullptr;
            }
            else
                for (auto x : e)
                {
                    std::int64_t d = 0;
                    if (!to_int(x, d))
                        return nullptr;
                    shape.push_back(d);
                }
            if (a->scalar || !a->c_contiguous() || a->view)
                return error("ValueError", "cannot resize this array: it does not own its data");
            auto* const r = new_array(a->type, shape);
            auto const n = std::min(r->size(), a->size()) * static_cast<std::int64_t>(a->itemsize());
            std::memcpy(r->pointer(), a->pointer(), static_cast<std::size_t>(n));
            a->data = r->data;
            a->offset = 0u;
            a->shape = r->shape;
            a->strides = r->strides;
            ctx->release(as_object(r));
            return new_none();
        }
        if (aName == "setflags")
        {
            arguments args{ "setflags", aArgs, aKwargs, { "write", "align", "uic" } };
            if (!args.ok())
                return nullptr;
            if (args.has(0))
            {
                bool write = true;
                if (!to_bool(args[0], write))
                    return nullptr;
                a->writeable = write;
            }
            return new_none();
        }
        if (aName == "byteswap")
        {
            arguments args{ "byteswap", aArgs, aKwargs, { "inplace" } };
            if (!args.ok())
                return nullptr;
            bool inplace = false;
            if (args.has(0) && !to_bool(args[0], inplace))
                return nullptr;
            auto* const r = inplace ? a : copy_array(*a);
            auto const size = r->itemsize();
            auto const part = is_complex(r->type) ? size / 2u : size;
            for_each(*r, [&](unsigned char* p)
            {
                for (std::size_t o = 0u; o < size; o += part)
                    std::reverse(p + o, p + o + part);
            });
            if (inplace)
                return (ctx->retain(aSelf), aSelf);
            if (a->scalar)
                return result_object(r);
            return as_object(r);
        }
        if (aName == "to_device")
            return (ctx->retain(aSelf), aSelf);
        if (aName == "__copy__" || aName == "__deepcopy__")
            return as_object(copy_array(*a));
        if (aName == "dot")
        {
            ref const args{ with_self(aSelf, aArgs) };
            return call_function("dot", args.get(), aKwargs);
        }
        // (the others: the numpy function of the same name with the array as its first argument)
        ref const args{ with_self(aSelf, aArgs) };
        return call_function(aName, args.get(), aKwargs);
    }
}
