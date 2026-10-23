/*
  math.cpp

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

  Portions of this file are derived from NumPy 2.5.3 (numpy/_core/_methods.py, numpy/lib/_function_base_impl.py,
  numpy/_core/src/common/cblasfuncs.c, numpy/_core/src/common/numpy_tag.h and
  numpy/_core/src/multiarray/compiled_base.c), Copyright (c) 2005-2025, NumPy Developers; All rights reserved. They are
  used under NumPy's BSD 3-Clause licence (see runtime/numpy/LICENSE.NumPy). Changes: translated from C and Python to
  C++ for the neos NumPy runtime plugin and adapted to its object model (see runtime/numpy/NOTICE).
*/

// reductions, statistics, sorting and searching, products, and other functions of the numpy module

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include "numpy.hpp"
#include "cast.hpp"
#include "functions.hpp"
#include "sort.hpp"
#include "kernels.hpp"
#include "simd.hpp"
#include "blas.hpp"

namespace neos::numpy
{
    namespace
    {
        ufunc const& u(char const* aName)
        {
            return *find_ufunc(aName);
        }

        object* binary(char const* aName, object* a, object* b, ndarray* aOut = nullptr, std::optional<dtype> aType = {})
        {
            return apply_binary(u(aName), a, b, aOut, aType);
        }

        object* unary(char const* aName, object* a, ndarray* aOut = nullptr)
        {
            return apply_unary(u(aName), a, aOut);
        }

        // (the axes of a reduction: None is all of them)
        bool reduction_axes(object* aAxis, std::size_t aNdim, std::vector<std::size_t>& aAxes)
        {
            return to_axes(aAxis, aNdim, aAxes);
        }

        object* reduction(char const* aFunction, char const* aUfunc, object* aArgs, object* aKwargs, bool aHasDtype)
        {
            arguments args = aHasDtype ?
                arguments{ aFunction, aArgs, aKwargs, { "a", "axis", "dtype", "out", "keepdims", "initial", "where" } } :
                arguments{ aFunction, aArgs, aKwargs, { "a", "axis", "out", "keepdims", "initial", "where" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto* const a = array_of(v);
            std::vector<std::size_t> axes;
            if (!reduction_axes(args[1], a->ndim(), axes))
                return nullptr;
            std::size_t const shift = aHasDtype ? 1u : 0u;
            std::optional<dtype> t;
            if (aHasDtype && !optional_dtype(args, 2, t))
                return nullptr;
            bool keepdims = false;
            if (!optional_bool(args, 3 + shift, false, keepdims))
                return nullptr;
            ndarray* out = args.has(2 + shift) && is_ndarray(args[2 + shift]) ? as_array(args[2 + shift]) : nullptr;
            return reduce_array(u(aUfunc), a, axes, t, keepdims, args[4 + shift], out);
        }

        object* f_sum(object* aArgs, object* aKwargs) { return reduction("sum", "add", aArgs, aKwargs, true); }
        object* f_prod(object* aArgs, object* aKwargs) { return reduction("prod", "multiply", aArgs, aKwargs, true); }
        object* f_max(object* aArgs, object* aKwargs) { return reduction("max", "maximum", aArgs, aKwargs, false); }
        object* f_min(object* aArgs, object* aKwargs) { return reduction("min", "minimum", aArgs, aKwargs, false); }

        object* logical_reduction(char const* aFunction, char const* aUfunc, object* aArgs, object* aKwargs)
        {
            arguments args{ aFunction, aArgs, aKwargs, { "a", "axis", "out", "keepdims", "*where" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto* const a = array_of(v);
            std::vector<std::size_t> axes;
            if (!reduction_axes(args[1], a->ndim(), axes))
                return nullptr;
            bool keepdims = false;
            if (!optional_bool(args, 3, false, keepdims))
                return nullptr;
            ndarray* out = args.has(2) && is_ndarray(args[2]) ? as_array(args[2]) : nullptr;
            return reduce_array(u(aUfunc), a, axes, dtype::Bool, keepdims, nullptr, out);
        }

        object* f_all(object* aArgs, object* aKwargs) { return logical_reduction("all", "logical_and", aArgs, aKwargs); }
        object* f_any(object* aArgs, object* aKwargs) { return logical_reduction("any", "logical_or", aArgs, aKwargs); }

        // the number of elements a reduction reduces
        std::int64_t reduce_count(ndarray const& aArray, std::vector<std::size_t> const& aAxes)
        {
            std::int64_t n = 1;
            for (auto a : aAxes)
                n *= aArray.shape[a];
            return n;
        }

        // (np.mean: _methods._mean)
        object* mean_of(ndarray* aArray, std::vector<std::size_t> const& aAxes, std::optional<dtype> aType, bool aKeepdims, ndarray* aOut)
        {
            auto const count = reduce_count(*aArray, aAxes);
            bool float16Result = false;
            auto t = aType;
            if (!t)
            {
                if (is_integer(aArray->type) || is_bool(aArray->type))
                    t = dtype::Float64;
                else if (aArray->type == dtype::Float16)
                {
                    t = dtype::Float32;
                    float16Result = true;
                }
            }
            ref sum{ reduce_array(u("add"), aArray, aAxes, t, aKeepdims, nullptr, aOut) };
            if (!sum)
                return nullptr;
            ref const n{ as_object(new_scalar(dtype::Int64, [&]() { scalar_value v; v.type = dtype::Int64; v.i = count; return v; }())) };
            if (is_ndarray(sum.get()))
            {
                auto* const s = as_array(sum.get());
                ref r{ binary("true_divide", sum.get(), n.get(), s) };
                if (!r)
                    return nullptr;
                if (float16Result && aOut == nullptr)
                    return as_object(copy_array(*s, dtype::Float16));
                return r.release();
            }
            // (a scalar: its type's value of sum / n)
            auto const sumType = as_array(sum.get())->type;
            ref const quotient{ binary("true_divide", sum.get(), n.get()) };
            if (!quotient)
                return nullptr;
            auto const resultType = float16Result ? dtype::Float16 : sumType;
            return as_object(new_scalar(resultType, load(as_array(quotient.get())->pointer(), as_array(quotient.get())->type)));
        }

        object* f_mean(object* aArgs, object* aKwargs)
        {
            arguments args{ "mean", aArgs, aKwargs, { "a", "axis", "dtype", "out", "keepdims", "*where" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto* const a = array_of(v);
            std::vector<std::size_t> axes;
            if (!reduction_axes(args[1], a->ndim(), axes))
                return nullptr;
            std::optional<dtype> t;
            if (!optional_dtype(args, 2, t))
                return nullptr;
            bool keepdims = false;
            if (!optional_bool(args, 4, false, keepdims))
                return nullptr;
            ndarray* out = args.has(3) && is_ndarray(args[3]) ? as_array(args[3]) : nullptr;
            return mean_of(a, axes, t, keepdims, out);
        }

        // (np.var: _methods._var)
        object* variance(ndarray* aArray, std::vector<std::size_t> const& aAxes, std::optional<dtype> aType, double aDdof, bool aKeepdims, bool aStd)
        {
            auto const count = reduce_count(*aArray, aAxes);
            auto t = aType;
            if (!t && (is_integer(aArray->type) || is_bool(aArray->type)))
                t = dtype::Float64;
            ref const n{ as_object(new_scalar(dtype::Int64, [&]() { scalar_value v; v.type = dtype::Int64; v.i = count; return v; }())) };
            ref arrmean{ reduce_array(u("add"), aArray, aAxes, t, true, nullptr, nullptr) };
            if (!arrmean)
                return nullptr;
            if (is_ndarray(arrmean.get()))
                arrmean = ref{ binary("true_divide", arrmean.get(), n.get(), as_array(arrmean.get())) };
            else
            {
                auto const meanType = as_array(arrmean.get())->type;
                ref const q{ binary("true_divide", arrmean.get(), n.get()) };
                arrmean = ref{ as_object(new_scalar(meanType, load(as_array(q.get())->pointer(), as_array(q.get())->type))) };
            }
            if (!arrmean)
                return nullptr;
            ref x{ binary("subtract", as_object(aArray), arrmean.get()) };
            if (!x)
                return nullptr;
            if (is_complex(as_array(x.get())->type))
            {
                ref const re{ array_getattr(x.get(), "real") };
                ref const im{ array_getattr(x.get(), "imag") };
                ref const re2{ binary("multiply", re.get(), re.get()) };
                ref const im2{ binary("multiply", im.get(), im.get()) };
                x = ref{ binary("add", re2.get(), im2.get()) };
            }
            else
                x = ref{ unary("square", x.get(), is_ndarray(x.get()) ? as_array(x.get()) : nullptr) };
            if (!x)
                return nullptr;
            auto const xa = array_ref(x.get(), {});
            ref r{ reduce_array(u("add"), array_of(xa), aAxes, t, aKeepdims, nullptr, nullptr) };
            if (!r)
                return nullptr;
            double const divisor = std::max(static_cast<double>(count) - aDdof, 0.0);
            ref const d{ divisor == std::floor(divisor) ? as_object(new_scalar(dtype::Int64, [&]() { scalar_value v; v.type = dtype::Int64; v.i = static_cast<std::int64_t>(divisor); return v; }())) :
                as_object(new_scalar(dtype::Float64, [&]() { scalar_value v; v.type = dtype::Float64; v.f = divisor; return v; }())) };
            if (is_ndarray(r.get()))
            {
                r = ref{ binary("true_divide", r.get(), d.get(), as_array(r.get())) };
                if (aStd && r)
                    r = ref{ unary("sqrt", r.get(), as_array(r.get())) };
                return r.release();
            }
            auto const resultType = as_array(r.get())->type;
            ref q{ binary("true_divide", r.get(), d.get()) };
            if (!q)
                return nullptr;
            q = ref{ as_object(new_scalar(resultType, load(as_array(q.get())->pointer(), as_array(q.get())->type))) };
            if (aStd)
            {
                ref const s{ unary("sqrt", q.get()) };
                if (!s)
                    return nullptr;
                return as_object(new_scalar(resultType, load(as_array(s.get())->pointer(), as_array(s.get())->type)));
            }
            return q.release();
        }

        object* var_std(char const* aName, object* aArgs, object* aKwargs, bool aStd)
        {
            arguments args{ aName, aArgs, aKwargs, { "a", "axis", "dtype", "out", "ddof", "keepdims", "*where", "*mean", "*correction" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto* const a = array_of(v);
            std::vector<std::size_t> axes;
            if (!reduction_axes(args[1], a->ndim(), axes))
                return nullptr;
            std::optional<dtype> t;
            if (!optional_dtype(args, 2, t))
                return nullptr;
            double ddof = 0.0;
            if (args.has(4) && !to_double(args[4], ddof))
                return nullptr;
            if (args.has(8) && !to_double(args[8], ddof))
                return nullptr;
            bool keepdims = false;
            if (!optional_bool(args, 5, false, keepdims))
                return nullptr;
            return variance(a, axes, t, ddof, keepdims, aStd);
        }

        object* f_var(object* aArgs, object* aKwargs) { return var_std("var", aArgs, aKwargs, false); }
        object* f_std(object* aArgs, object* aKwargs) { return var_std("std", aArgs, aKwargs, true); }

        object* f_ptp(object* aArgs, object* aKwargs)
        {
            ref const maximum{ f_max(aArgs, aKwargs) };
            if (!maximum)
                return nullptr;
            ref const minimum{ f_min(aArgs, aKwargs) };
            if (!minimum)
                return nullptr;
            return binary("subtract", maximum.get(), minimum.get());
        }

        // argmax/argmin: the index of the first maximum (minimum); NaN is the maximum (and minimum)
        object* arg_extreme(char const* aName, object* aArgs, object* aKwargs, bool aMax)
        {
            arguments args{ aName, aArgs, aKwargs, { "a", "axis", "out", "*keepdims" } };
            if (!args.ok())
                return nullptr;
            auto v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            bool keepdims = false;
            if (!optional_bool(args, 3, false, keepdims))
                return nullptr;
            std::optional<std::size_t> axis;
            auto* a = array_of(v);
            if (args.has(1))
            {
                if (!optional_axis(args, 1, a->ndim(), axis))
                    return nullptr;
            }
            else
            {
                v = new_ref(ravel(a));
                a = array_of(v);
                axis = 0u;
            }
            if (a->shape[*axis] == 0)
                return error("ValueError", std::string{ "attempt to get " } + aName + " of an empty sequence");
            shape_t outShape;
            shape_t outerShape, outerStrides;
            for (std::size_t i = 0u; i < a->ndim(); ++i)
                if (i != *axis)
                {
                    outShape.push_back(a->shape[i]);
                    outerShape.push_back(a->shape[i]);
                    outerStrides.push_back(a->strides[i]);
                }
            auto* const r = new_array(dtype::Int64, outShape);
            auto const n = a->shape[*axis];
            auto const stride = a->strides[*axis];
            iterate(outerShape, { a->pointer(), r->pointer() }, { outerStrides, r->strides }, [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
            {
                auto p = aPointers[0];
                auto o = aPointers[1];
                for (std::int64_t k = 0; k < aCount; ++k, p += aStrides[0], o += aStrides[1])
                {
                    std::int64_t best = 0;
                    dispatch(a->type, [&](auto tag)
                    {
                        using T = decltype(tag);
                        using C = std::conditional_t<is_half_v<T>, float, T>;
                        auto const at = [&](std::int64_t i) { return cast<C>(read_as<T>(p + i * stride, a->type)); };
                        C bestValue = at(0);
                        auto const isnan = [](C x) { if constexpr (is_complex_v<C>) return x != x; else if constexpr (std::is_floating_point_v<C>) return std::isnan(x); else return false; };
                        if (isnan(bestValue))
                            return;
                        for (std::int64_t i = 1; i < n; ++i)
                        {
                            auto const x = at(i);
                            if (isnan(x))
                            {
                                best = i;
                                return;
                            }
                            bool better;
                            if constexpr (is_complex_v<C>)
                                better = aMax ? (x.real() > bestValue.real() || (x.real() == bestValue.real() && x.imag() > bestValue.imag())) :
                                    (x.real() < bestValue.real() || (x.real() == bestValue.real() && x.imag() < bestValue.imag()));
                            else
                                better = aMax ? x > bestValue : x < bestValue;
                            if (better)
                            {
                                best = i;
                                bestValue = x;
                            }
                        }
                    });
                    write_as(o, dtype::Int64, best);
                }
            });
            if (keepdims && args.has(1))
            {
                shape_t kept = a->shape;
                kept[*axis] = 1;
                auto* const k = reshape(r, kept);
                ctx->release(as_object(r));
                return as_object(k);
            }
            return result_object(r);
        }

        object* f_argmax(object* aArgs, object* aKwargs) { return arg_extreme("argmax", aArgs, aKwargs, true); }
        object* f_argmin(object* aArgs, object* aKwargs) { return arg_extreme("argmin", aArgs, aKwargs, false); }

        object* cumulative(char const* aName, char const* aUfunc, object* aArgs, object* aKwargs)
        {
            arguments args{ aName, aArgs, aKwargs, { "a", "axis", "dtype", "out" } };
            if (!args.ok())
                return nullptr;
            auto v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            std::optional<std::size_t> axis;
            if (args.has(1))
            {
                if (!optional_axis(args, 1, array_of(v)->ndim(), axis))
                    return nullptr;
            }
            else
            {
                v = new_ref(ravel(array_of(v)));
                axis = 0u;
            }
            std::optional<dtype> t;
            if (!optional_dtype(args, 2, t))
                return nullptr;
            return accumulate_array(u(aUfunc), array_of(v), *axis, t);
        }

        object* f_cumsum(object* aArgs, object* aKwargs) { return cumulative("cumsum", "add", aArgs, aKwargs); }
        object* f_cumprod(object* aArgs, object* aKwargs) { return cumulative("cumprod", "multiply", aArgs, aKwargs); }

        object* f_count_nonzero(object* aArgs, object* aKwargs)
        {
            arguments args{ "count_nonzero", aArgs, aKwargs, { "a", "axis", "*keepdims" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto* const a = array_of(v);
            ref const b{ as_object(copy_array(*a, dtype::Bool)) };
            std::vector<std::size_t> axes;
            if (!reduction_axes(args[1], a->ndim(), axes))
                return nullptr;
            bool keepdims = false;
            if (!optional_bool(args, 2, false, keepdims))
                return nullptr;
            ref r{ reduce_array(u("add"), as_array(b.get()), axes, dtype::Int64, keepdims, nullptr, nullptr) };
            if (!r)
                return nullptr;
            if (!args.has(1) && is_array(r.get()))
                return to_python(*as_array(r.get()));
            return r.release();
        }

        std::vector<std::vector<std::int64_t>> nonzero_positions(ndarray const& aArray)
        {
            std::vector<std::vector<std::int64_t>> positions(aArray.ndim());
            std::vector<std::int64_t> index(aArray.ndim(), 0);
            auto const n = aArray.size();
            for (std::int64_t flat = 0; flat < n; ++flat)
            {
                std::int64_t offset = 0;
                for (std::size_t d = 0u; d < aArray.ndim(); ++d)
                    offset += index[d] * aArray.strides[d];
                if (read_as<bool>(aArray.pointer() + offset, aArray.type))
                    for (std::size_t d = 0u; d < aArray.ndim(); ++d)
                        positions[d].push_back(index[d]);
                for (std::size_t d = aArray.ndim(); d-- > 0u;)
                {
                    if (++index[d] < aArray.shape[d])
                        break;
                    index[d] = 0;
                }
            }
            return positions;
        }

        ndarray* int64_array(std::vector<std::int64_t> const& aValues)
        {
            auto* const r = new_array(dtype::Int64, { static_cast<std::int64_t>(aValues.size()) });
            if (!aValues.empty())
                std::memcpy(r->pointer(), aValues.data(), aValues.size() * sizeof(std::int64_t));
            return r;
        }

        object* f_nonzero(object* aArgs, object* aKwargs)
        {
            arguments args{ "nonzero", aArgs, aKwargs, { "a" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto* const a = array_of(v);
            if (a->ndim() == 0)
                return error("ValueError", "Calling nonzero on 0d arrays is not allowed. Use np.atleast_1d(scalar).nonzero() instead. If the context of this error is of the form `arr[nonzero(cond)]`, just use `arr[cond]`.");
            std::vector<object*> results;
            for (auto const& p : nonzero_positions(*a))
                results.push_back(as_object(int64_array(p)));
            return make_tuple(std::move(results));
        }

        object* f_flatnonzero(object* aArgs, object* aKwargs)
        {
            arguments args{ "flatnonzero", aArgs, aKwargs, { "a" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            ref const flat{ as_object(ravel(array_of(v))) };
            return as_object(int64_array(nonzero_positions(*as_array(flat.get()))[0]));
        }

        object* f_argwhere(object* aArgs, object* aKwargs)
        {
            arguments args{ "argwhere", aArgs, aKwargs, { "a" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto* const a = array_of(v);
            auto const positions = nonzero_positions(*a);
            std::int64_t const n = positions.empty() ? (read_as<bool>(a->pointer(), a->type) ? 1 : 0) : static_cast<std::int64_t>(positions[0].size());
            auto* const r = new_array(dtype::Int64, { n, static_cast<std::int64_t>(a->ndim()) });
            for (std::int64_t i = 0; i < n; ++i)
                for (std::size_t d = 0u; d < a->ndim(); ++d)
                    write_as(r->pointer() + i * r->strides[0] + static_cast<std::int64_t>(d) * r->strides[1], dtype::Int64, positions[d][static_cast<std::size_t>(i)]);
            return as_object(r);
        }

        object* f_where(object* aArgs, object* aKwargs)
        {
            arguments args{ "where", aArgs, aKwargs, { "condition", "x", "y" } };
            if (!args.ok())
                return nullptr;
            if (args[1] == nullptr && args[2] == nullptr)
            {
                ref const t{ make_tuple({ (ctx->retain(args[0]), args[0]) }) };
                return f_nonzero(t.get(), nullptr);
            }
            if (args[1] == nullptr || args[2] == nullptr)
                return error("ValueError", "either both or neither of x and y should be given");
            auto const t = result_type({ args[1], args[2] });
            if (!t)
                return nullptr;
            auto const c = array_ref(args[0], {});
            auto const x = array_ref(args[1], {});
            auto const y = array_ref(args[2], {});
            if (!c || !x || !y)
                return nullptr;
            auto* const ca = array_of(c);
            auto* const xa = array_of(x);
            auto* const ya = array_of(y);
            shape_t shape;
            if (!broadcast_shapes({ &ca->shape, &xa->shape, &ya->shape }, shape))
                return nullptr;
            auto* const r = new_array(*t, shape);
            iterate(shape, { ca->pointer(), xa->pointer(), ya->pointer(), r->pointer() },
                { broadcast_strides(*ca, shape), broadcast_strides(*xa, shape), broadcast_strides(*ya, shape), r->strides },
                [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
                {
                    auto pc = aPointers[0], px = aPointers[1], py = aPointers[2], pr = aPointers[3];
                    for (std::int64_t i = 0; i < aCount; ++i, pc += aStrides[0], px += aStrides[1], py += aStrides[2], pr += aStrides[3])
                    {
                        if (read_as<bool>(pc, ca->type))
                            cast_element(px, xa->type, pr, *t);
                        else
                            cast_element(py, ya->type, pr, *t);
                    }
                });
            return result_object(r);
        }

        // --- sorting

        // (NumPy's order: NaN last)
        template <typename C>
        bool sort_less(C a, C b)
        {
            if constexpr (is_complex_v<C>)
            {
                // (complex: lexicographic, NaNs last: npy::cmp)
                if (a.real() < b.real())
                    return a.imag() == a.imag() || b.imag() != b.imag();
                if (a.real() > b.real())
                    return b.imag() != b.imag() && a.imag() == a.imag();
                if (a.real() == b.real() || (a.real() != a.real() && b.real() != b.real()))
                    return a.imag() < b.imag() || (b.imag() != b.imag() && a.imag() == a.imag());
                return b.real() != b.real();
            }
            else if constexpr (std::is_floating_point_v<C>)
                return a < b || (b != b && a == a);
            else
                return a < b;
        }

        // (the lanes of an array along an axis: each copied to a contiguous buffer, processed, and copied back; as NumPy's _new_sortlike)
        void for_each_lane(ndarray* aArray, ndarray* aResult, std::size_t aAxis, std::function<void(unsigned char* aBuffer, std::int64_t* aIndices)> const& aFunction,
            bool aIndices)
        {
            shape_t outerShape, outerStrides, resultStrides;
            for (std::size_t i = 0u; i < aArray->ndim(); ++i)
                if (i != aAxis)
                {
                    outerShape.push_back(aArray->shape[i]);
                    outerStrides.push_back(aArray->strides[i]);
                    resultStrides.push_back(aResult->strides[i]);
                }
            auto const n = aArray->shape[aAxis];
            auto const stride = aArray->strides[aAxis];
            auto const rStride = aResult->strides[aAxis];
            auto const size = static_cast<std::int64_t>(aArray->itemsize());
            std::vector<unsigned char> buffer(static_cast<std::size_t>(n * size));
            std::vector<std::int64_t> indices(aIndices ? static_cast<std::size_t>(n) : 0u);
            iterate(outerShape, { aArray->pointer(), aResult->pointer() }, { outerStrides, resultStrides }, [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
            {
                auto p = aPointers[0];
                auto o = aPointers[1];
                for (std::int64_t k = 0; k < aCount; ++k, p += aStrides[0], o += aStrides[1])
                {
                    // (a contiguous result lane: the lane itself (the values copied to it, or the indices))
                    if (!aIndices && rStride == size)
                    {
                        if (p == o)
                            ;
                        else if (stride == size)
                            std::memmove(o, p, static_cast<std::size_t>(n * size));
                        else
                            for (std::int64_t i = 0; i < n; ++i)
                                std::memcpy(o + i * size, p + i * stride, static_cast<std::size_t>(size));
                        aFunction(o, nullptr);
                        continue;
                    }
                    if (stride == size)
                        std::memcpy(buffer.data(), p, static_cast<std::size_t>(n * size));
                    else
                        for (std::int64_t i = 0; i < n; ++i)
                            std::memcpy(buffer.data() + i * size, p + i * stride, static_cast<std::size_t>(size));
                    if (aIndices && rStride == static_cast<std::int64_t>(sizeof(std::int64_t)) && reinterpret_cast<std::uintptr_t>(o) % alignof(std::int64_t) == 0u)
                    {
                        aFunction(buffer.data(), reinterpret_cast<std::int64_t*>(o));
                        continue;
                    }
                    aFunction(buffer.data(), aIndices ? indices.data() : nullptr);
                    for (std::int64_t i = 0; i < n; ++i)
                    {
                        if (aIndices)
                            write_as(o + i * rStride, dtype::Int64, indices[static_cast<std::size_t>(i)]);
                        else
                            std::memcpy(o + i * rStride, buffer.data() + i * size, static_cast<std::size_t>(size));
                    }
                }
            });
        }

        // sort (or argsort) along an axis
        ndarray* sort_along(ndarray* aArray, std::size_t aAxis, bool aIndices, sort_kind aKind = sort_kind::Quick)
        {
            auto* const r = aIndices ? new_unfilled_array(dtype::Int64, aArray->shape) : copy_array(*aArray);
            auto const n = aArray->shape[aAxis];
            if (aArray->size() == 0)
                return r;
            for_each_lane(aArray, r, aAxis, [&](unsigned char* aBuffer, std::int64_t* aIndexBuffer)
            {
                if (aIndexBuffer != nullptr)
                    argsort_contiguous(aArray->type, aBuffer, aIndexBuffer, n, aKind);
                else
                    sort_contiguous(aArray->type, aBuffer, n, aKind);
            }, aIndices);
            return r;
        }

        bool parse_sort_kind(arguments const& aArgs, std::size_t aKindIndex, std::size_t aStableIndex, sort_kind& aKind)
        {
            aKind = sort_kind::Quick;
            if (aStableIndex != ~std::size_t{} && aArgs.has(aStableIndex))
            {
                bool stable = false;
                if (!to_bool(aArgs[aStableIndex], stable))
                    return false;
                if (aArgs.has(aKindIndex))
                {
                    error("ValueError", "`kind` and `stable` parameters can't be provided at the same time. Use only one of them.");
                    return false;
                }
                aKind = stable ? sort_kind::Stable : sort_kind::Quick;
                return true;
            }
            if (!aArgs.has(aKindIndex))
                return true;
            auto const k = as_str(aArgs[aKindIndex]);
            if (!k || k->empty())
            {
                error("ValueError", "sort kind must be one of 'quick', 'heap', or 'stable' (got " + (k ? "'" + *k + "'" : std::string{ "None" }) + ")");
                return false;
            }
            switch (std::tolower(static_cast<unsigned char>((*k)[0])))
            {
            case 'q': aKind = sort_kind::Quick; return true;
            case 'h': aKind = sort_kind::Quick; return true;     // (as NumPy: heapsort is quicksort, whose worst case is heapsort)
            case 's': case 'm': aKind = sort_kind::Stable; return true;
            default:
                error("ValueError", "sort kind must be one of 'quick', 'heap', or 'stable' (got '" + *k + "')");
                return false;
            }
        }

        object* sorting(char const* aName, object* aArgs, object* aKwargs, bool aIndices)
        {
            arguments args{ aName, aArgs, aKwargs, { "a", "axis", "kind", "order", "*stable" } };
            if (!args.ok())
                return nullptr;
            auto v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            sort_kind sortKind;
            if (!parse_sort_kind(args, 2, 4, sortKind))
                return nullptr;
            std::size_t axis = 0u;
            if (args[1] != nullptr && kind(args[1]) == NEOS_PY_NONE)
            {
                v = new_ref(ravel(array_of(v)));
                axis = 0u;
            }
            else
            {
                std::int64_t x = -1;
                if (args.has(1) && !to_int(args[1], x))
                    return nullptr;
                if (array_of(v)->ndim() == 0)
                {
                    if (aIndices)
                        return as_object(new_array(dtype::Int64, {}));
                    return error("AxisError", "axis -1 is out of bounds for array of dimension 0");
                }
                if (!normalize_axis(x, array_of(v)->ndim(), axis))
                    return nullptr;
            }
            return as_object(sort_along(array_of(v), axis, aIndices, sortKind));
        }

        object* partitioning(char const* aName, object* aArgs, object* aKwargs, bool aIndices)
        {
            arguments args{ aName, aArgs, aKwargs, { "a", "kth", "axis", "kind", "order" } };
            if (!args.ok())
                return nullptr;
            auto v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            if (args.has(3) && as_str(args[3]) != std::optional<std::string>{ "introselect" })
                return error("ValueError", "select kind must be 'introselect' (got '" + as_str(args[3]).value_or("") + "')");
            std::size_t axis = 0u;
            if (args.get(2) != nullptr && kind(args[2]) == NEOS_PY_NONE)
                v = new_ref(ravel(array_of(v)));
            else
            {
                std::int64_t x = -1;
                if (args.has(2) && !to_int(args[2], x))
                    return nullptr;
                if (array_of(v)->ndim() == 0)
                    return error("ValueError", "Cannot partition a 0-d array");
                if (!normalize_axis(x, array_of(v)->ndim(), axis))
                    return nullptr;
            }
            auto* const a = array_of(v);
            auto const n = a->shape[axis];
            // (partition_prep_kth_array)
            auto const kv = array_ref(args[1], {});
            if (!kv)
                return nullptr;
            if (is_bool(array_of(kv)->type))
                return error("TypeError", "Booleans unacceptable as partition index");
            if (!is_integer(array_of(kv)->type))
                return error("TypeError", "Partition index must be integer");
            if (array_of(kv)->ndim() > 1)
                return error("ValueError", "kth array must have dimension <= 1");
            std::vector<std::int64_t> kth;
            for_each(*array_of(kv), [&](unsigned char* p) { kth.push_back(read_as<std::int64_t>(p, array_of(kv)->type)); });
            for (auto& k : kth)
            {
                if (k < 0)
                    k += n;
                if (k < 0 || k >= n)
                    return error("ValueError", "kth(=" + std::to_string(k) + ") out of bounds (" + std::to_string(n) + ")");
            }
            std::sort(kth.begin(), kth.end());
            auto* const r = aIndices ? new_unfilled_array(dtype::Int64, a->shape) : copy_array(*a);
            if (a->size() == 0)
                return as_object(r);
            for_each_lane(a, r, axis, [&](unsigned char* aBuffer, std::int64_t* aIndexBuffer)
            {
                if (aIndexBuffer != nullptr)
                    argpartition_contiguous(a->type, aBuffer, aIndexBuffer, n, kth);
                else
                    partition_contiguous(a->type, aBuffer, n, kth);
            }, aIndices);
            return as_object(r);
        }

        object* f_partition(object* aArgs, object* aKwargs) { return partitioning("partition", aArgs, aKwargs, false); }
        object* f_argpartition(object* aArgs, object* aKwargs) { return partitioning("argpartition", aArgs, aKwargs, true); }

        object* f_sort(object* aArgs, object* aKwargs) { return sorting("sort", aArgs, aKwargs, false); }
        object* f_argsort(object* aArgs, object* aKwargs) { return sorting("argsort", aArgs, aKwargs, true); }

        object* f_unique(object* aArgs, object* aKwargs)
        {
            arguments args{ "unique", aArgs, aKwargs, { "ar", "return_index", "return_inverse", "return_counts", "axis", "*equal_nan", "*sorted" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            bool returnIndex = false, returnInverse = false, returnCounts = false;
            if (!optional_bool(args, 1, false, returnIndex) || !optional_bool(args, 2, false, returnInverse) || !optional_bool(args, 3, false, returnCounts))
                return nullptr;
            bool sorted = true;
            if (!optional_bool(args, 6, true, sorted))
                return nullptr;
            if (!sorted && !returnIndex && !returnInverse && !returnCounts && !args.has(4))
            {
                // (NumPy's hash based unique: its order)
                ref const t{ make_tuple({ (ctx->retain(v.get()), v.get()) }) };
                return call_function("unique_values", t.get(), nullptr);
            }
            ref const flat{ as_object(ravel(array_of(v))) };
            auto* const a = as_array(flat.get());
            if (!returnIndex && !returnInverse && !returnCounts && (is_integer(a->type) || is_bool(a->type)))
            {
                // (integers: the values sorted (equal values are indistinguishable) and their repeats removed)
                ref const copy{ as_object(copy_array(*a)) };
                auto* const c = as_array(copy.get());
                auto const n = c->size();
                auto const size = c->itemsize();
                auto* const data = c->pointer();
                sort_contiguous(c->type, data, n, sort_kind::Quick);
                std::int64_t count = 0;
                for (std::int64_t k = 0; k < n; ++k)
                    if (k == 0 || std::memcmp(data + k * size, data + (count - 1) * size, size) != 0)
                    {
                        if (count != k)
                            std::memcpy(data + count * size, data + k * size, size);
                        ++count;
                    }
                auto* const values = new_unfilled_array(c->type, { count });
                if (count > 0)
                    std::memcpy(values->pointer(), data, static_cast<std::size_t>(count) * size);
                return as_object(values);
            }
            ref const order{ as_object(sort_along(a, 0u, true, sort_kind::Stable)) };
            auto* const o = as_array(order.get());
            auto const n = a->size();
            std::vector<std::int64_t> sortedIndex(static_cast<std::size_t>(n));
            if (n > 0)
                std::memcpy(sortedIndex.data(), o->pointer(), static_cast<std::size_t>(n) * sizeof(std::int64_t));
            std::vector<std::int64_t> firstIndices, counts, inverse(static_cast<std::size_t>(n));
            std::vector<std::int64_t> uniquePositions;
            auto const size = a->itemsize();
            auto const element = [&](std::int64_t i) { return a->pointer() + i * a->strides[0]; };
            auto const equal = [&](std::int64_t i, std::int64_t j)
            {
                if (std::memcmp(element(i), element(j), size) == 0)
                    return true;
                return dispatch(a->type, [&](auto tag) -> bool
                {
                    using T = decltype(tag);
                    using C = std::conditional_t<is_half_v<T>, float, T>;
                    auto const x = cast<C>(read_as<T>(element(i), a->type));
                    auto const y = cast<C>(read_as<T>(element(j), a->type));
                    if constexpr (is_complex_v<C>)
                        return x == y || (x != x && y != y);
                    else if constexpr (std::is_floating_point_v<C>)
                        return x == y || (std::isnan(x) && std::isnan(y));
                    else
                        return x == y;
                });
            };
            for (std::int64_t k = 0; k < n; ++k)
            {
                auto const i = sortedIndex[static_cast<std::size_t>(k)];
                if (k == 0 || !equal(sortedIndex[static_cast<std::size_t>(k - 1)], i))
                {
                    uniquePositions.push_back(i);
                    firstIndices.push_back(i);
                    counts.push_back(1);
                }
                else
                {
                    ++counts.back();
                    firstIndices.back() = std::min(firstIndices.back(), i);
                }
                inverse[static_cast<std::size_t>(i)] = static_cast<std::int64_t>(uniquePositions.size()) - 1;
            }
            auto* const values = new_array(a->type, { static_cast<std::int64_t>(uniquePositions.size()) });
            for (std::size_t i = 0u; i < uniquePositions.size(); ++i)
                std::memcpy(values->pointer() + i * size, element(uniquePositions[i]), size);
            if (!returnIndex && !returnInverse && !returnCounts)
                return as_object(values);
            std::vector<object*> results{ as_object(values) };
            if (returnIndex)
                results.push_back(as_object(int64_array(firstIndices)));
            if (returnInverse)
            {
                auto* const inv = int64_array(inverse);
                // (NumPy 2: the inverse has the input's shape)
                auto* const shaped = reshape(inv, array_of(v)->shape);
                ctx->release(as_object(inv));
                results.push_back(as_object(shaped));
            }
            if (returnCounts)
                results.push_back(as_object(int64_array(counts)));
            return make_tuple(std::move(results));
        }

        object* f_searchsorted(object* aArgs, object* aKwargs)
        {
            arguments args{ "searchsorted", aArgs, aKwargs, { "a", "v", "side", "sorter" } };
            if (!args.ok())
                return nullptr;
            auto const av = array_ref(args[0], {});
            auto const vv = array_ref(args[1], {});
            if (!av || !vv)
                return nullptr;
            bool const right = args.has(2) && as_str(args[2]) == std::optional<std::string>{ "right" };
            auto* const a = array_of(av);
            auto* const values = array_of(vv);
            auto const t = promote(a->type, values->type);
            auto* const r = new_array(dtype::Int64, values->shape);
            auto const n = a->shape.at(0);
            dispatch(t, [&](auto tag)
            {
                using T = decltype(tag);
                using C = std::conditional_t<is_half_v<T>, float, T>;
                iterate(values->shape, { values->pointer(), r->pointer() }, { values->strides, r->strides }, [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
                {
                    auto p = aPointers[0];
                    auto o = aPointers[1];
                    for (std::int64_t k = 0; k < aCount; ++k, p += aStrides[0], o += aStrides[1])
                    {
                        auto const x = cast<C>(read_as<T>(p, values->type));
                        std::int64_t low = 0, high = n;
                        while (low < high)
                        {
                            auto const middle = (low + high) / 2;
                            auto const m = cast<C>(read_as<T>(a->pointer() + middle * a->strides[0], a->type));
                            bool const goRight = right ? !sort_less(x, m) : sort_less(m, x);
                            if (goRight)
                                low = middle + 1;
                            else
                                high = middle;
                        }
                        write_as(o, dtype::Int64, low);
                    }
                });
            });
            return result_object(r);
        }

        // --- products (NumPy's, with BLAS (blas.hpp) for float32, float64, complex64 and complex128)

        // an operand in the product's type: the array, or a copy cast to the type (aKeepOrder: a 2-D copy in the array's
        // order (NumPy's keep order), otherwise C order)
        ref product_operand(ndarray* aArray, dtype aType, bool aKeepOrder)
        {
            if (aArray->type == aType)
                return array_ref(as_object(aArray), {});
            bool const fortran = aKeepOrder && aArray->ndim() == 2u && std::abs(aArray->strides[0]) < std::abs(aArray->strides[1]);
            return new_ref(copy_array(*aArray, aType, fortran));
        }

        // a gufunc of two operands (aCoreA, aCoreB: their core dimensions' counts): the result (its core shape aResultCore,
        // after the broadcast outer dimensions) and the loop (aLoop: the number of outer iterations, the pointers and their
        // outer strides) over the outer dimensions
        ndarray* gufunc2(ndarray* aA, std::size_t aCoreA, ndarray* aB, std::size_t aCoreB, dtype aType, shape_t const& aResultCore,
            std::function<void(std::int64_t, unsigned char**, std::int64_t const*, ndarray const&)> const& aLoop)
        {
            shape_t const aOuterShape{ aA->shape.begin(), aA->shape.end() - static_cast<std::ptrdiff_t>(aCoreA) };
            shape_t const bOuterShape{ aB->shape.begin(), aB->shape.end() - static_cast<std::ptrdiff_t>(aCoreB) };
            shape_t batch;
            if (!broadcast_shapes({ &aOuterShape, &bOuterShape }, batch))
                return nullptr;
            shape_t shape = batch;
            shape.insert(shape.end(), aResultCore.begin(), aResultCore.end());
            auto* const r = new_unfilled_array(aType, shape);
            auto const outer = [&](ndarray const& aArray, std::size_t aCore)
            {
                shape_t full = batch;
                full.insert(full.end(), aArray.shape.end() - static_cast<std::ptrdiff_t>(aCore), aArray.shape.end());
                auto strides = broadcast_strides(aArray, full);
                strides.resize(batch.size());
                return strides;
            };
            shape_t const rOuter{ r->strides.begin(), r->strides.begin() + static_cast<std::ptrdiff_t>(batch.size()) };
            iterate(batch, { aA->pointer(), aB->pointer(), r->pointer() }, { outer(*aA, aCoreA), outer(*aB, aCoreB), rOuter },
                [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
                {
                    aLoop(aCount, aPointers, aStrides, *r);
                });
            return r;
        }

        // the integer matrix product kernel (kernels.hpp, in the CPU's instruction set): NumPy's loop's results (the sums wrap:
        // their order does not change them)
        bool integer_matmul(dtype aType, std::int64_t aOuter, unsigned char** aArgs, std::int64_t const* aOuterSteps, std::int64_t aDm,
            std::int64_t aDn, std::int64_t aDp, std::int64_t const* aSteps)
        {
            if (!is_integer(aType))
                return false;
            dispatch(aType, [&](auto tag)
            {
                using T = decltype(tag);
                if constexpr (std::is_integral_v<T> && !is_bool_v<T>)
                {
                    unsigned char* pa = aArgs[0];
                    unsigned char* pb = aArgs[1];
                    unsigned char* pr = aArgs[2];
                    for (std::int64_t q = 0; q < aOuter; ++q, pa += aOuterSteps[0], pb += aOuterSteps[1], pr += aOuterSteps[2])
                    {
                        if constexpr (simd::type_of<T>().has_value())
                            if (simd::matmul(*simd::type_of<T>(), aDm, aDp, aDn, pa, aSteps[0], aSteps[1], pb, aSteps[2], aSteps[3], pr, aSteps[4], aSteps[5]))
                                continue;
                        baseline::matmul_kernel<T>(aDm, aDp, aDn, pa, aSteps[0], aSteps[1], pb, aSteps[2], aSteps[3], pr, aSteps[4], aSteps[5]);
                    }
                }
            });
            return true;
        }

        object* f_matmul(object* aArgs, object* aKwargs)
        {
            arguments args{ "matmul", aArgs, aKwargs, { "x1", "x2", "out", "*dtype", "*casting", "*order", "*subok", "*axes", "*axis" } };
            if (!args.ok())
                return nullptr;
            auto const av = array_ref(args[0], {});
            auto const bv = array_ref(args[1], {});
            if (!av || !bv)
                return nullptr;
            auto* a = array_of(av);
            auto* b = array_of(bv);
            if (a->ndim() == 0 || b->ndim() == 0)
                return error("ValueError", "matmul: Input operand " + std::string{ a->ndim() == 0 ? "0" : "1" } + " does not have enough dimensions (has 0, gufunc core with signature (n?,k),(k,m?)->(n?,m?) requires 1)");
            bool const vectorA = a->ndim() == 1u;
            bool const vectorB = b->ndim() == 1u;
            auto const k1 = a->shape[a->ndim() - 1u];
            auto const k2 = vectorB ? b->shape[0] : b->shape[b->ndim() - 2u];
            if (k1 != k2)
                return error("ValueError", "matmul: Input operand 1 has a mismatch in its core dimension 0, with gufunc signature (n?,k),(k,m?)->(n?,m?) (size " +
                    std::to_string(k2) + " is different from " + std::to_string(k1) + ")");
            auto t = promote(a->type, b->type);
            std::optional<dtype> requested;
            if (!optional_dtype(args, 3, requested))
                return nullptr;
            if (requested)
                t = *requested;
            // (the operands in the loop's type; a vector's missing core dimension has size 1 and stride 0)
            ref const ac = product_operand(a, t, false);
            ref const bc = product_operand(b, t, false);
            a = array_of(ac);
            b = array_of(bc);
            auto const dm = vectorA ? 1 : a->shape[a->ndim() - 2u];
            auto const dn = k1;
            auto const dp = vectorB ? 1 : b->shape[b->ndim() - 1u];
            shape_t core;
            if (!vectorA)
                core.push_back(dm);
            if (!vectorB)
                core.push_back(dp);
            std::int64_t steps[6] = {
                vectorA ? 0 : a->strides[a->ndim() - 2u], a->strides[a->ndim() - 1u],
                vectorB ? b->strides[0] : b->strides[b->ndim() - 2u], vectorB ? 0 : b->strides[b->ndim() - 1u], 0, 0 };
            auto* const r = gufunc2(a, vectorA ? 1u : 2u, b, vectorB ? 1u : 2u, t, core,
                [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides, ndarray const& aResult)
                {
                    steps[4] = vectorA ? 0 : aResult.strides[aResult.ndim() - (vectorB ? 1u : 2u)];
                    steps[5] = vectorB ? 0 : aResult.strides[aResult.ndim() - 1u];
                    if (!integer_matmul(t, aCount, aPointers, aStrides, dm, dn, dp, steps))
                        blas::matmul(t, aCount, aPointers, aStrides, dm, dn, dp, steps);
                });
            if (r == nullptr)
                return nullptr;
            return result_object(r);
        }

        // (NumPy's _bad_strides: data not aligned, or a negative, non-element or zero (in a dimension longer than one) stride)
        bool bad_strides(ndarray const& aArray)
        {
            auto const itemsize = static_cast<std::int64_t>(aArray.itemsize());
            if (reinterpret_cast<std::uintptr_t>(aArray.pointer()) % static_cast<std::uintptr_t>(itemsize) != 0u)
                return true;
            for (std::size_t i = 0u; i < aArray.ndim(); ++i)
                if (aArray.strides[i] < 0 || aArray.strides[i] % itemsize != 0 || (aArray.strides[i] == 0 && aArray.shape[i] > 1))
                    return true;
            return false;
        }

        object* dot_alignment_error(ndarray const& aA, std::size_t aAxisA, ndarray const& aB, std::size_t aAxisB)
        {
            return error("ValueError", "shapes " + shape_repr(aA.shape) + " and " + shape_repr(aB.shape) + " not aligned: " + std::to_string(aA.shape[aAxisA]) +
                " (dim " + std::to_string(aAxisA) + ") != " + std::to_string(aB.shape[aAxisB]) + " (dim " + std::to_string(aAxisB) + ")");
        }

        // np.dot of vectors and matrices of a BLAS type (NumPy's cblas_matrixproduct)
        object* cblas_matrixproduct(ref aAp1, ref aAp2, dtype aType)
        {
            enum matrix_shape { Scalar, Column, Row, Matrix };
            auto const select_matrix_shape = [](ndarray const& aArray)
            {
                switch (aArray.ndim())
                {
                case 0:
                    return Scalar;
                case 1:
                    return aArray.shape[0] > 1 ? Column : Scalar;
                case 2:
                    if (aArray.shape[0] > 1)
                        return aArray.shape[1] == 1 ? Column : Matrix;
                    return aArray.shape[1] == 1 ? Scalar : Row;
                default:
                    return Matrix;
                }
            };
            // (a copy in C order, or in Fortran order if the array is Fortran contiguous (any order))
            auto const copy_of = [](ref const& aArray, bool aAnyOrder)
            {
                auto const& a = *array_of(aArray);
                return new_ref(copy_array(a, {}, aAnyOrder && a.f_contiguous() && !a.c_contiguous()));
            };
            if (bad_strides(*array_of(aAp1)))
                aAp1 = copy_of(aAp1, true);
            if (bad_strides(*array_of(aAp2)))
                aAp2 = copy_of(aAp2, true);
            auto ap1shape = select_matrix_shape(*array_of(aAp1));
            auto ap2shape = select_matrix_shape(*array_of(aAp2));
            std::int64_t l = 0;
            std::int64_t ap1stride = 0;
            shape_t dimensions;
            if (ap1shape == Scalar || ap2shape == Scalar)
            {
                auto const& oap1 = *array_of(aAp1);
                auto const& oap2 = *array_of(aAp2);
                ref const oap1ref = aAp1, oap2ref = aAp2;
                if (ap1shape == Scalar)
                {
                    // (ap2 the scalar)
                    std::swap(aAp1, aAp2);
                    ap1shape = ap2shape;
                    ap2shape = Scalar;
                }
                auto const& ap1 = *array_of(aAp1);
                auto const& ap2 = *array_of(aAp2);
                if (ap1shape == Row)
                    ap1stride = ap1.strides[1];
                else if (ap1.ndim() > 0u)
                    ap1stride = ap1.strides[0];
                if (ap1.ndim() == 0u || ap2.ndim() == 0u)
                {
                    dimensions = ap1.ndim() == 0u ? ap2.shape : ap1.shape;
                    l = element_count(dimensions);
                }
                else
                {
                    l = oap1.shape[oap1.ndim() - 1u];
                    if (oap2.shape[0] != l)
                        return dot_alignment_error(oap1, oap1.ndim() - 1u, oap2, 0u);
                    auto const nd = oap1.ndim() + oap2.ndim() - 2u;
                    if (nd == 1u)
                    {
                        dimensions = { oap1.ndim() == 2u ? oap1.shape[0] : oap2.shape[1] };
                        l = dimensions[0];
                    }
                    else if (nd == 2u)
                    {
                        dimensions = { oap1.shape[0], oap2.shape[1] };
                        l = ap1shape == Row ? dimensions[1] : dimensions[0];
                    }
                    if (oap1.shape[oap1.ndim() - 1u] == 0)
                        l = 0;
                }
            }
            else
            {
                auto const& ap1 = *array_of(aAp1);
                auto const& ap2 = *array_of(aAp2);
                l = ap1.shape[ap1.ndim() - 1u];
                if (ap2.shape[0] != l)
                    return dot_alignment_error(ap1, ap1.ndim() - 1u, ap2, 0u);
                auto const nd = ap1.ndim() + ap2.ndim() - 2u;
                if (nd == 1u)
                    dimensions = { ap1.ndim() == 2u ? ap1.shape[0] : ap2.shape[1] };
                else if (nd == 2u)
                    dimensions = { ap1.shape[0], ap2.shape[1] };
            }
            auto* const out = new_array(aType, dimensions);
            ref const result = new_ref(out);
            if (out->size() == 0 || l == 0)
                return result_object((ctx->retain(result.get()), out));
            auto const itemsize = static_cast<std::int64_t>(info(aType).itemsize);
            if (ap2shape == Scalar)
            {
                // (multiplication by a scalar: axpy)
                auto& ap1 = *array_of(aAp1);
                auto& ap2 = *array_of(aAp2);
                if (l == 1)
                {
                    dispatch(aType, [&](auto tag)
                    {
                        using T = decltype(tag);
                        if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>)
                            *reinterpret_cast<T*>(out->pointer()) = *reinterpret_cast<T const*>(ap2.pointer()) * *reinterpret_cast<T const*>(ap1.pointer());
                        else if constexpr (is_complex_v<T>)
                        {
                            using R = typename T::value_type;
                            auto const* const ptr1 = reinterpret_cast<R const*>(ap2.pointer());
                            auto const* const ptr2 = reinterpret_cast<R const*>(ap1.pointer());
                            auto* const res = reinterpret_cast<R*>(out->pointer());
                            res[0] = ptr1[0] * ptr2[0] - ptr1[1] * ptr2[1];
                            res[1] = ptr1[0] * ptr2[1] + ptr1[1] * ptr2[0];
                        }
                    });
                }
                else if (ap1shape != Matrix)
                    blas::axpy(aType, l, ap2.pointer(), ap1.pointer(), ap1stride / itemsize, out->pointer(), 1);
                else
                {
                    std::size_t const maxind = ap1.shape[0] >= ap1.shape[1] ? 0u : 1u;
                    std::size_t const oind = 1u - maxind;
                    auto* ptr = ap1.pointer();
                    auto* optr = out->pointer();
                    l = ap1.shape[maxind];
                    auto const a1s = ap1.strides[maxind] / itemsize;
                    auto const outs = out->strides[maxind] / itemsize;
                    for (std::int64_t i = 0; i < ap1.shape[oind]; ++i)
                    {
                        blas::axpy(aType, l, ap2.pointer(), ptr, a1s, optr, outs);
                        ptr += ap1.strides[oind];
                        optr += out->strides[oind];
                    }
                }
            }
            else if (ap2shape == Column && ap1shape != Matrix)
            {
                // (the dot product of vectors)
                auto& ap1 = *array_of(aAp1);
                auto& ap2 = *array_of(aAp2);
                blas::dot(aType, ap1.pointer(), ap1.strides[ap1shape == Row ? 1u : 0u], ap2.pointer(), ap2.strides[0], out->pointer(), l);
            }
            else if (ap1shape == Matrix && ap2shape != Matrix)
            {
                // (matrix vector: gemv)
                if (!array_of(aAp1)->c_contiguous() && !array_of(aAp1)->f_contiguous())
                    aAp1 = copy_of(aAp1, false);
                auto& ap1 = *array_of(aAp1);
                auto& ap2 = *array_of(aAp2);
                blas::order order;
                std::int64_t lda;
                if (ap1.c_contiguous())
                {
                    order = blas::order::RowMajor;
                    lda = std::max<std::int64_t>(ap1.shape[1], 1);
                }
                else
                {
                    order = blas::order::ColMajor;
                    lda = std::max<std::int64_t>(ap1.shape[0], 1);
                }
                blas::gemv(aType, order, blas::transpose::NoTrans, ap1.shape[0], ap1.shape[1], ap1.pointer(), lda, ap2.pointer(), ap2.strides[0] / itemsize, out->pointer());
            }
            else if (ap1shape != Matrix && ap2shape == Matrix)
            {
                // (vector matrix: gemv)
                if (!array_of(aAp2)->c_contiguous() && !array_of(aAp2)->f_contiguous())
                    aAp2 = copy_of(aAp2, false);
                auto& ap1 = *array_of(aAp1);
                auto& ap2 = *array_of(aAp2);
                blas::order order;
                std::int64_t lda;
                if (ap2.c_contiguous())
                {
                    order = blas::order::RowMajor;
                    lda = std::max<std::int64_t>(ap2.shape[1], 1);
                }
                else
                {
                    order = blas::order::ColMajor;
                    lda = std::max<std::int64_t>(ap2.shape[0], 1);
                }
                auto const ap1s = (ap1shape == Row ? ap1.strides[1] : ap1.strides[0]) / itemsize;
                blas::gemv(aType, order, blas::transpose::Trans, ap2.shape[0], ap2.shape[1], ap2.pointer(), lda, ap1.pointer(), ap1s, out->pointer());
            }
            else
            {
                // (matrix matrix: gemm, or syrk for a matrix times its transpose)
                if (!array_of(aAp2)->c_contiguous() && !array_of(aAp2)->f_contiguous())
                    aAp2 = copy_of(aAp2, false);
                if (!array_of(aAp1)->c_contiguous() && !array_of(aAp1)->f_contiguous())
                    aAp1 = copy_of(aAp1, false);
                auto& ap1 = *array_of(aAp1);
                auto& ap2 = *array_of(aAp2);
                auto trans1 = blas::transpose::NoTrans, trans2 = blas::transpose::NoTrans;
                auto const L = ap1.shape[0], N = ap2.shape[1], M = ap2.shape[0];
                auto lda = std::max<std::int64_t>(ap1.shape[1], 1);
                auto ldb = std::max<std::int64_t>(ap2.shape[1], 1);
                auto const ldc = std::max<std::int64_t>(out->shape[1], 1);
                if (ap1.f_contiguous())
                {
                    trans1 = blas::transpose::Trans;
                    lda = std::max<std::int64_t>(ap1.shape[0], 1);
                }
                if (ap2.f_contiguous())
                {
                    trans2 = blas::transpose::Trans;
                    ldb = std::max<std::int64_t>(ap2.shape[0], 1);
                }
                if (ap1.pointer() == ap2.pointer() && ap1.shape[0] == ap2.shape[1] && ap1.shape[1] == ap2.shape[0] &&
                    ap1.strides[0] == ap2.strides[1] && ap1.strides[1] == ap2.strides[0] && (trans1 == blas::transpose::Trans) != (trans2 == blas::transpose::Trans))
                {
                    if (trans1 == blas::transpose::NoTrans)
                        blas::syrk(aType, blas::order::RowMajor, trans1, N, M, ap1.pointer(), lda, out->pointer(), ldc);
                    else
                        blas::syrk(aType, blas::order::RowMajor, trans1, N, M, ap2.pointer(), ldb, out->pointer(), ldc);
                    // (the lower triangle copied from the upper)
                    auto* const r = out->pointer();
                    for (std::int64_t i = 0; i < N; ++i)
                        for (std::int64_t j = i + 1; j < N; ++j)
                            std::memcpy(r + j * out->strides[0] + i * out->strides[1], r + i * out->strides[0] + j * out->strides[1], static_cast<std::size_t>(itemsize));
                }
                else
                    blas::gemm(aType, blas::order::RowMajor, trans1, trans2, L, N, M, ap1.pointer(), lda, ap2.pointer(), ldb, out->pointer(), ldc);
            }
            return result_object((ctx->retain(result.get()), out));
        }

        object* f_dot(object* aArgs, object* aKwargs)
        {
            arguments args{ "dot", aArgs, aKwargs, { "a", "b", "out" } };
            if (!args.ok())
                return nullptr;
            auto const av = array_ref(args[0], {});
            auto const bv = array_ref(args[1], {});
            if (!av || !bv)
                return nullptr;
            auto const t = promote(array_of(av)->type, array_of(bv)->type);
            // (NumPy's PyArray_MatrixProduct2: the operands in the common type)
            ref const ac = product_operand(array_of(av), t, true);
            ref const bc = product_operand(array_of(bv), t, true);
            auto* const a = array_of(ac);
            auto* const b = array_of(bc);
            if (a->ndim() <= 2u && b->ndim() <= 2u && blas::is_blas_type(t))
                return cblas_matrixproduct(ac, bc, t);
            if (a->ndim() == 0 || b->ndim() == 0)
                return binary("multiply", ac.get(), bc.get());
            // dot(a, b)[i,j,k,m] = sum(a[i,j,:] * b[k,:,m]): the dot function of every pair of a's and b's vectors (C order)
            auto const l = a->shape[a->ndim() - 1u];
            auto const matchDim = b->ndim() > 1u ? b->ndim() - 2u : 0u;
            if (b->shape[matchDim] != l)
                return dot_alignment_error(*a, a->ndim() - 1u, *b, matchDim);
            shape_t shape{ a->shape.begin(), a->shape.end() - 1 };
            shape_t bOther;
            for (std::size_t i = 0u; i < b->ndim(); ++i)
                if (i != matchDim)
                {
                    shape.push_back(b->shape[i]);
                    bOther.push_back(b->shape[i]);
                }
            auto* const r = new_array(t, shape);
            auto const aOuter = element_count({ a->shape.begin(), a->shape.end() - 1 });
            auto const bOuter = element_count(bOther);
            auto const is1 = a->strides[a->ndim() - 1u];
            auto const is2 = b->strides[matchDim];
            std::vector<std::int64_t> ai(a->ndim() - 1u, 0);
            std::vector<std::int64_t> bi;
            auto* out = r->pointer();
            for (std::int64_t i = 0; i < aOuter; ++i)
            {
                std::int64_t aOffset = 0;
                for (std::size_t d = 0u; d + 1u < a->ndim(); ++d)
                    aOffset += ai[d] * a->strides[d];
                bi.assign(bOther.size(), 0);
                for (std::int64_t j = 0; j < bOuter; ++j)
                {
                    std::int64_t bOffset = 0;
                    for (std::size_t d = 0u, e = 0u; d < b->ndim(); ++d)
                        if (d != matchDim)
                            bOffset += bi[e++] * b->strides[d];
                    blas::dot(t, a->pointer() + aOffset, is1, b->pointer() + bOffset, is2, out, l);
                    out += r->itemsize();
                    for (std::size_t d = bi.size(); d-- > 0u;) { if (++bi[d] < bOther[d]) break; bi[d] = 0; }
                }
                for (std::size_t d = ai.size(); d-- > 0u;) { if (++ai[d] < a->shape[d]) break; ai[d] = 0; }
            }
            return result_object(r);
        }

        object* f_vdot(object* aArgs, object* aKwargs)
        {
            arguments args{ "vdot", aArgs, aKwargs, { "a", "b" } };
            if (!args.ok())
                return nullptr;
            auto const av = array_ref(args[0], {});
            auto const bv = array_ref(args[1], {});
            if (!av || !bv)
                return nullptr;
            auto const t = promote(array_of(av)->type, array_of(bv)->type);
            // (the operands in the common type, flattened (C order))
            ref const ac = product_operand(array_of(av), t, false);
            ref const bc = product_operand(array_of(bv), t, false);
            ref const a = new_ref(ravel(array_of(ac)));
            ref const b = new_ref(ravel(array_of(bc)));
            auto* const ap1 = array_of(a);
            auto* const ap2 = array_of(b);
            if (ap1->shape[0] != ap2->shape[0])
                return error("ValueError", "vectors have different lengths");
            auto* const r = new_array(t, {});
            blas::vdot(t, ap1->pointer(), ap1->strides[0], ap2->pointer(), ap2->strides[0], r->pointer(), ap1->shape[0]);
            return result_object(r);
        }

        // vecdot (n),(n)->(): the first operand conjugated
        object* f_vecdot(object* aArgs, object* aKwargs)
        {
            arguments args{ "vecdot", aArgs, aKwargs, { "x1", "x2", "*out", "*casting", "*order", "*dtype", "*subok", "*signature", "*axes", "*axis" } };
            if (!args.ok())
                return nullptr;
            auto av = array_ref(args[0], {});
            auto bv = array_ref(args[1], {});
            if (!av || !bv)
                return nullptr;
            if (array_of(av)->ndim() == 0u || array_of(bv)->ndim() == 0u)
                return error("ValueError", "vecdot: Input operand " + std::string{ array_of(av)->ndim() == 0u ? "0" : "1" } +
                    " does not have enough dimensions (has 0, gufunc core with signature (n),(n)->() requires 1)");
            if (args.has(9))
            {
                // (axis: the core dimension of both operands)
                auto const moved = [&](ref const& aArray) -> ref
                {
                    std::optional<std::size_t> axis;
                    if (!optional_axis(args, 9, array_of(aArray)->ndim(), axis))
                        return ref{};
                    std::vector<std::size_t> order;
                    for (std::size_t i = 0u; i < array_of(aArray)->ndim(); ++i)
                        if (i != *axis)
                            order.push_back(i);
                    order.push_back(*axis);
                    return new_ref(transpose(array_of(aArray), order));
                };
                av = moved(av);
                if (!av)
                    return nullptr;
                bv = moved(bv);
                if (!bv)
                    return nullptr;
            }
            auto const n1 = array_of(av)->shape.back(), n2 = array_of(bv)->shape.back();
            if (n1 != n2)
                return error("ValueError", "vecdot: Input operand 1 has a mismatch in its core dimension 0, with gufunc signature (n),(n)->() (size " +
                    std::to_string(n2) + " is different from " + std::to_string(n1) + ")");
            auto t = promote(array_of(av)->type, array_of(bv)->type);
            std::optional<dtype> requested;
            if (!optional_dtype(args, 5, requested))
                return nullptr;
            if (requested)
                t = *requested;
            ref const ac = product_operand(array_of(av), t, false);
            ref const bc = product_operand(array_of(bv), t, false);
            auto* const a = array_of(ac);
            auto* const b = array_of(bc);
            auto* const r = gufunc2(a, 1u, b, 1u, t, {}, [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides, ndarray const&)
            {
                blas::vecdot(t, aCount, aPointers, aStrides, n1, a->strides.back(), b->strides.back());
            });
            if (r == nullptr)
                return nullptr;
            return result_object(r);
        }

        // matvec (m,n),(n)->(m) and vecmat (n),(n,m)->(m)
        object* matrix_vector(char const* aName, object* aArgs, object* aKwargs, bool aVectorFirst)
        {
            arguments args{ aName, aArgs, aKwargs, { "x1", "x2", "*out", "*casting", "*order", "*dtype", "*subok", "*signature", "*axes", "*axis" } };
            if (!args.ok())
                return nullptr;
            auto const av = array_ref(args[0], {});
            auto const bv = array_ref(args[1], {});
            if (!av || !bv)
                return nullptr;
            std::string const name = aName;
            std::string const signature = aVectorFirst ? "(n),(n,m)->(m)" : "(m,n),(n)->(m)";
            std::size_t const coreA = aVectorFirst ? 1u : 2u, coreB = aVectorFirst ? 2u : 1u;
            if (array_of(av)->ndim() < coreA)
                return error("ValueError", name + ": Input operand 0 does not have enough dimensions (has " + std::to_string(array_of(av)->ndim()) +
                    ", gufunc core with signature " + signature + " requires " + std::to_string(coreA) + ")");
            if (array_of(bv)->ndim() < coreB)
                return error("ValueError", name + ": Input operand 1 does not have enough dimensions (has " + std::to_string(array_of(bv)->ndim()) +
                    ", gufunc core with signature " + signature + " requires " + std::to_string(coreB) + ")");
            auto const& x1 = *array_of(av);
            auto const& x2 = *array_of(bv);
            auto const dn = aVectorFirst ? x1.shape.back() : x1.shape.back();
            auto const dn2 = aVectorFirst ? x2.shape[x2.ndim() - 2u] : x2.shape.back();
            if (dn != dn2)
                return error("ValueError", name + ": Input operand 1 has a mismatch in its core dimension 0, with gufunc signature " + signature + " (size " +
                    std::to_string(dn2) + " is different from " + std::to_string(dn) + ")");
            auto t = promote(x1.type, x2.type);
            std::optional<dtype> requested;
            if (!optional_dtype(args, 5, requested))
                return nullptr;
            if (requested)
                t = *requested;
            ref const ac = product_operand(array_of(av), t, false);
            ref const bc = product_operand(array_of(bv), t, false);
            auto* const a = array_of(ac);
            auto* const b = array_of(bc);
            auto const dm = aVectorFirst ? b->shape.back() : a->shape[a->ndim() - 2u];
            auto* const r = gufunc2(a, coreA, b, coreB, t, { dm }, [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides, ndarray const& aResult)
            {
                if (aVectorFirst)
                {
                    std::int64_t const steps[4] = { a->strides.back(), b->strides[b->ndim() - 2u], b->strides.back(), aResult.strides.back() };
                    blas::vecmat(t, aCount, aPointers, aStrides, dn, dm, steps);
                }
                else
                {
                    std::int64_t const steps[4] = { a->strides[a->ndim() - 2u], a->strides.back(), b->strides.back(), aResult.strides.back() };
                    blas::matvec(t, aCount, aPointers, aStrides, dm, dn, steps);
                }
            });
            if (r == nullptr)
                return nullptr;
            return result_object(r);
        }

        object* f_matvec(object* aArgs, object* aKwargs) { return matrix_vector("matvec", aArgs, aKwargs, false); }
        object* f_vecmat(object* aArgs, object* aKwargs) { return matrix_vector("vecmat", aArgs, aKwargs, true); }

        object* f_inner(object* aArgs, object* aKwargs)
        {
            arguments args{ "inner", aArgs, aKwargs, { "a", "b" } };
            if (!args.ok())
                return nullptr;
            auto const av = array_ref(args[0], {});
            auto const bv = array_ref(args[1], {});
            if (!av || !bv)
                return nullptr;
            auto* const b = array_of(bv);
            if (array_of(av)->ndim() == 0 || b->ndim() == 0)
                return binary("multiply", args[0], args[1]);
            // inner(a, b) = dot(a, moveaxis(b, -1, 0)) for 1-D/2-D; general: tensordot over the last axes
            if (b->ndim() == 1u)
                return f_dot(aArgs, nullptr);
            std::vector<std::size_t> axes;
            for (std::size_t i = 0u; i + 1u < b->ndim(); ++i)
                axes.push_back(i);
            axes.insert(axes.end() - static_cast<std::ptrdiff_t>(b->ndim() - 1u > 0u ? 1u : 0u), b->ndim() - 1u);
            // (b with its last axis second to last)
            std::vector<std::size_t> order;
            for (std::size_t i = 0u; i + 1u < b->ndim(); ++i)
                order.push_back(i);
            order.insert(order.end() - 1, b->ndim() - 1u);
            ref const bt = new_ref(transpose(b, order));
            ref const t{ make_tuple({ av.get() ? (ctx->retain(av.get()), av.get()) : nullptr, (ctx->retain(bt.get()), bt.get()) }) };
            return f_dot(t.get(), nullptr);
        }

        object* f_outer(object* aArgs, object* aKwargs)
        {
            arguments args{ "outer", aArgs, aKwargs, { "a", "b", "out" } };
            if (!args.ok())
                return nullptr;
            auto const av = array_ref(args[0], {});
            auto const bv = array_ref(args[1], {});
            if (!av || !bv)
                return nullptr;
            auto* const a = ravel(array_of(av));
            auto* const b = ravel(array_of(bv));
            ref const ac{ as_object(view_of(a, { a->shape[0], 1 }, { a->strides[0], 0 }, a->offset)) };
            ctx->release(as_object(a));
            ref const bc{ as_object(b) };
            return binary("multiply", ac.get(), bc.get());
        }

        object* f_cross(object* aArgs, object* aKwargs)
        {
            arguments args{ "cross", aArgs, aKwargs, { "a", "b", "axisa", "axisb", "axisc", "axis" } };
            if (!args.ok())
                return nullptr;
            auto const av = array_ref(args[0], {});
            auto const bv = array_ref(args[1], {});
            if (!av || !bv)
                return nullptr;
            auto* const a = array_of(av);
            auto* const b = array_of(bv);
            auto const n = a->shape.back();
            auto const m = b->shape.back();
            if ((n != 2 && n != 3) || (m != 2 && m != 3))
                return error("ValueError", "incompatible dimensions for cross product\n(dimension must be 2 or 3)");
            auto const t = promote(a->type, b->type);
            shape_t aOuter{ a->shape.begin(), a->shape.end() - 1 }, bOuter{ b->shape.begin(), b->shape.end() - 1 }, outer;
            if (!broadcast_shapes({ &aOuter, &bOuter }, outer))
                return nullptr;
            shape_t shape = outer;
            bool const scalarResult = n == 2 && m == 2;
            if (!scalarResult)
                shape.push_back(3);
            auto* const r = new_array(t, shape);
            auto const as = a->strides.back(), bs = b->strides.back();
            shape_t aStrides = broadcast_strides(*a, [&]() { auto s = outer; s.push_back(n); return s; }());
            shape_t bStrides = broadcast_strides(*b, [&]() { auto s = outer; s.push_back(m); return s; }());
            aStrides.pop_back();
            bStrides.pop_back();
            shape_t rStrides{ r->strides.begin(), r->strides.end() - (scalarResult ? 0 : 1) };
            auto const rs = scalarResult ? 0 : r->strides.back();
            dispatch(t, [&](auto tag)
            {
                using T = decltype(tag);
                using C = std::conditional_t<is_half_v<T>, float, T>;
                if constexpr (!is_bool_v<C>)
                iterate(outer, { a->pointer(), b->pointer(), r->pointer() }, { aStrides, bStrides, rStrides }, [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aS)
                {
                    auto pa = aPointers[0], pb = aPointers[1], pr = aPointers[2];
                    for (std::int64_t q = 0; q < aCount; ++q, pa += aS[0], pb += aS[1], pr += aS[2])
                    {
                        auto const x = [&](std::int64_t i) { return i < n ? cast<C>(read_as<T>(pa + i * as, a->type)) : C{}; };
                        auto const y = [&](std::int64_t i) { return i < m ? cast<C>(read_as<T>(pb + i * bs, b->type)) : C{}; };
                        auto const put = [&](std::int64_t i, C v) { if constexpr (is_half_v<T>) write_as(pr + i * rs, t, half{ v }); else write_as(pr + i * rs, t, v); };
                        if (scalarResult)
                            put(0, static_cast<C>(x(0) * y(1) - x(1) * y(0)));
                        else
                        {
                            put(0, static_cast<C>(x(1) * y(2) - x(2) * y(1)));
                            put(1, static_cast<C>(x(2) * y(0) - x(0) * y(2)));
                            put(2, static_cast<C>(x(0) * y(1) - x(1) * y(0)));
                        }
                    }
                });
            });
            return result_object(r);
        }

        object* f_trace(object* aArgs, object* aKwargs)
        {
            arguments args{ "trace", aArgs, aKwargs, { "a", "offset", "axis1", "axis2", "dtype", "out" } };
            if (!args.ok())
                return nullptr;
            std::vector<object*> diagonalArgs;
            for (std::size_t i = 0u; i < 4u; ++i)
            {
                object* x = args[i] != nullptr ? args[i] : (i == 1u ? nullptr : nullptr);
                if (x == nullptr)
                    x = i == 0u ? nullptr : i == 1u ? make_int(0) : i == 2u ? make_int(0) : make_int(1);
                else
                    ctx->retain(x);
                diagonalArgs.push_back(x);
            }
            ref const t{ make_tuple(diagonalArgs) };
            ref const d{ call_function("diagonal", t.get(), nullptr) };
            if (!d)
                return nullptr;
            auto* const da = as_array(d.get());
            std::vector<std::size_t> axes{ da->ndim() - 1u };
            std::optional<dtype> type;
            if (!optional_dtype(args, 4, type))
                return nullptr;
            return reduce_array(u("add"), da, axes, type, false, nullptr, nullptr);
        }

        // --- elementwise functions that are not ufuncs

        object* f_clip(object* aArgs, object* aKwargs)
        {
            arguments args{ "clip", aArgs, aKwargs, { "a", "a_min", "a_max", "out", "*min", "*max", "*dtype", "*casting" } };
            if (!args.ok())
                return nullptr;
            object* low = args.has(1) ? args[1] : args.has(4) ? args[4] : nullptr;
            object* high = args.has(2) ? args[2] : args.has(5) ? args[5] : nullptr;
            ndarray* out = args.has(3) && is_ndarray(args[3]) ? as_array(args[3]) : nullptr;
            if (low == nullptr && high == nullptr)
                return unary("positive", args[0], out);
            // (the result type of all three)
            std::vector<object*> parts{ args[0] };
            if (low != nullptr) parts.push_back(low);
            if (high != nullptr) parts.push_back(high);
            auto const t = result_type(parts);
            if (!t)
                return nullptr;
            ref r;
            if (low != nullptr)
                r = ref{ binary("maximum", args[0], low, nullptr, *t) };
            else
                r = ref::borrowed(args[0]);
            if (!r)
                return nullptr;
            if (high != nullptr)
                r = ref{ binary("minimum", r.get(), high, out, *t) };
            else if (out != nullptr)
                r = ref{ unary("positive", r.get(), out) };
            return r.release();
        }

        object* f_round(object* aArgs, object* aKwargs)
        {
            arguments args{ "round", aArgs, aKwargs, { "a", "decimals", "out" } };
            if (!args.ok())
                return nullptr;
            std::int64_t decimals = 0;
            if (args.has(1) && !to_int(args[1], decimals))
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto* const a = array_of(v);
            bool const scalar = a->scalar;
            if (is_bool(a->type) || is_integer(a->type))
            {
                if (decimals >= 0)
                    return scalar ? (ctx->retain(args[0]), args[0]) : as_object(copy_array(*a));
                // (integers rounded to tens, hundreds, ...: half to even)
                std::int64_t f = 1;
                for (std::int64_t i = 0; i < -decimals; ++i)
                    f *= 10;
                auto* const r = copy_array(*a);
                for_each(*r, [&](unsigned char* p)
                {
                    auto const x = read_as<std::int64_t>(p, r->type);
                    auto q = x / f;
                    auto rem = x % f;
                    if (rem < 0) { rem += f; --q; }
                    if (2 * rem > f || (2 * rem == f && (q % 2) != 0))
                        ++q;
                    write_as(p, r->type, q * f);
                });
                if (scalar)
                    r->base.type = a->base.type, r->scalar = true;
                return as_object(r);
            }
            // (as NumPy: rint(a * 10**decimals) / 10**decimals)
            double const f = std::pow(10.0, static_cast<double>(std::llabs(decimals)));
            ref const factor{ as_object(new_scalar(is_complex(a->type) ? real_for(a->type) : a->type, [&]() { scalar_value s; s.type = dtype::Float64; s.f = f; return s; }())) };
            ref y{ decimals >= 0 ? binary("multiply", as_object(a), factor.get()) : binary("true_divide", as_object(a), factor.get()) };
            if (!y)
                return nullptr;
            y = ref{ unary("rint", y.get()) };
            if (!y)
                return nullptr;
            y = ref{ decimals >= 0 ? binary("true_divide", y.get(), factor.get()) : binary("multiply", y.get(), factor.get()) };
            return y.release();
        }

        object* f_fix(object* aArgs, object* aKwargs)
        {
            arguments args{ "fix", aArgs, aKwargs, { "x", "out" } };
            if (!args.ok())
                return nullptr;
            return unary("trunc", args[0]);
        }

        object* f_diff(object* aArgs, object* aKwargs)
        {
            arguments args{ "diff", aArgs, aKwargs, { "a", "n", "axis", "prepend", "append" } };
            if (!args.ok())
                return nullptr;
            auto v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            std::int64_t n = 1, axis = -1;
            if (args.has(1) && !to_int(args[1], n)) return nullptr;
            if (args.has(2) && !to_int(args[2], axis)) return nullptr;
            if (n < 0)
                return error("ValueError", "order must be non-negative but got " + std::to_string(n));
            if (array_of(v)->ndim() == 0)
                return error("ValueError", "diff requires input that is at least one dimensional");
            std::size_t ax = 0u;
            if (!normalize_axis(axis, array_of(v)->ndim(), ax))
                return nullptr;
            std::vector<ref> parts;
            auto add_part = [&](object* aPart) -> bool
            {
                auto p = array_ref(aPart, {});
                if (!p)
                    return false;
                if (array_of(p)->ndim() == 0)
                {
                    shape_t shape = array_of(v)->shape;
                    shape[ax] = 1;
                    p = new_ref(view_of(array_of(p), shape, shape_t(shape.size(), 0), array_of(p)->offset));
                }
                parts.push_back(p);
                return true;
            };
            if (args.has(3) || args.has(4))
            {
                if (args.has(3) && !add_part(args[3])) return nullptr;
                parts.push_back(v);
                if (args.has(4) && !add_part(args[4])) return nullptr;
                v = new_ref(concatenate(pointers(parts), ax, {}));
                if (!v)
                    return nullptr;
            }
            for (std::int64_t k = 0; k < n; ++k)
            {
                auto* const a = array_of(v);
                auto const length = a->shape[ax];
                shape_t shape = a->shape;
                shape[ax] = std::max<std::int64_t>(length - 1, 0);
                ref const upper{ as_object(view_of(a, shape, a->strides, static_cast<std::size_t>(static_cast<std::int64_t>(a->offset) + (length > 1 ? a->strides[ax] : 0)))) };
                ref const lower{ as_object(view_of(a, shape, a->strides, a->offset)) };
                v = ref{ binary(is_bool(a->type) ? "not_equal" : "subtract", upper.get(), lower.get()) };
                if (!v)
                    return nullptr;
                if (!is_ndarray(v.get()))
                    v = array_ref(v.get(), {});
            }
            return v.release();
        }

        object* f_isclose(object* aArgs, object* aKwargs)
        {
            arguments args{ "isclose", aArgs, aKwargs, { "a", "b", "rtol", "atol", "equal_nan" } };
            if (!args.ok())
                return nullptr;
            double rtol = 1e-05, atol = 1e-08;
            if (args.has(2) && !to_double(args[2], rtol)) return nullptr;
            if (args.has(3) && !to_double(args[3], atol)) return nullptr;
            bool equalNan = false;
            if (!optional_bool(args, 4, false, equalNan))
                return nullptr;
            auto const av = array_ref(args[0], {});
            auto const bv = array_ref(args[1], {});
            if (!av || !bv)
                return nullptr;
            auto* const a = array_of(av);
            auto* const b = array_of(bv);
            shape_t shape;
            if (!broadcast_shapes({ &a->shape, &b->shape }, shape))
                return nullptr;
            auto* const r = new_array(dtype::Bool, shape);
            iterate(shape, { a->pointer(), b->pointer(), r->pointer() }, { broadcast_strides(*a, shape), broadcast_strides(*b, shape), r->strides },
                [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
                {
                    auto pa = aPointers[0], pb = aPointers[1], pr = aPointers[2];
                    for (std::int64_t i = 0; i < aCount; ++i, pa += aStrides[0], pb += aStrides[1], pr += aStrides[2])
                    {
                        auto const x = read_as<std::complex<double>>(pa, a->type);
                        auto const y = read_as<std::complex<double>>(pb, b->type);
                        bool close;
                        bool const xnan = std::isnan(x.real()) || std::isnan(x.imag());
                        bool const ynan = std::isnan(y.real()) || std::isnan(y.imag());
                        if (xnan || ynan)
                            close = equalNan && xnan && ynan;
                        else if (std::isinf(x.real()) || std::isinf(y.real()) || std::isinf(x.imag()) || std::isinf(y.imag()))
                            close = x == y;
                        else
                            close = std::abs(x - y) <= atol + rtol * std::abs(y);
                        write_as(pr, dtype::Bool, close);
                    }
                });
            return result_object(r);
        }

        object* f_allclose(object* aArgs, object* aKwargs)
        {
            ref const close{ f_isclose(aArgs, aKwargs) };
            if (!close)
                return nullptr;
            auto const a = array_ref(close.get(), {});
            bool all = true;
            for_each(*array_of(a), [&](unsigned char* p) { all = all && read_as<bool>(p, dtype::Bool); });
            return make_bool(all);
        }

        object* f_array_equal(object* aArgs, object* aKwargs)
        {
            arguments args{ "array_equal", aArgs, aKwargs, { "a1", "a2", "equal_nan" } };
            if (!args.ok())
                return nullptr;
            auto const av = array_ref(args[0], {});
            auto const bv = array_ref(args[1], {});
            if (!av || !bv)
                return nullptr;
            if (array_of(av)->shape != array_of(bv)->shape)
                return make_bool(false);
            bool equalNan = false;
            if (!optional_bool(args, 2, false, equalNan))
                return nullptr;
            ref const equal{ binary("equal", av.get(), bv.get()) };
            if (!equal)
                return nullptr;
            auto const e = array_ref(equal.get(), {});
            bool all = true;
            for_each(*array_of(e), [&](unsigned char* p) { all = all && read_as<bool>(p, dtype::Bool); });
            if (!all && equalNan)
            {
                ref const na{ unary("isnan", av.get()) };
                ref const nb{ unary("isnan", bv.get()) };
                ref const both{ binary("logical_and", na.get(), nb.get()) };
                ref const either{ binary("logical_or", equal.get(), both.get()) };
                all = true;
                auto const x = array_ref(either.get(), {});
                for_each(*array_of(x), [&](unsigned char* p) { all = all && read_as<bool>(p, dtype::Bool); });
            }
            return make_bool(all);
        }

        object* f_array_equiv(object* aArgs, object* aKwargs)
        {
            arguments args{ "array_equiv", aArgs, aKwargs, { "a1", "a2" } };
            if (!args.ok())
                return nullptr;
            auto const av = array_ref(args[0], {});
            auto const bv = array_ref(args[1], {});
            if (!av || !bv)
                return nullptr;
            shape_t shape;
            if (!broadcast_shapes({ &array_of(av)->shape, &array_of(bv)->shape }, shape))
            {
                ctx->clear(ctx);
                return make_bool(false);
            }
            ref const equal{ binary("equal", av.get(), bv.get()) };
            auto const e = array_ref(equal.get(), {});
            bool all = true;
            for_each(*array_of(e), [&](unsigned char* p) { all = all && read_as<bool>(p, dtype::Bool); });
            return make_bool(all);
        }

        object* f_take(object* aArgs, object* aKwargs)
        {
            arguments args{ "take", aArgs, aKwargs, { "a", "indices", "axis", "out", "mode" } };
            if (!args.ok())
                return nullptr;
            auto v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            std::size_t axis = 0u;
            if (!args.has(2))
                v = new_ref(ravel(array_of(v)));
            else
            {
                std::int64_t x = 0;
                if (!to_int(args[2], x) || !normalize_axis(x, array_of(v)->ndim(), axis))
                    return nullptr;
            }
            // a[(slice(None),) * axis + (indices,)]
            std::vector<object*> key;
            for (std::size_t i = 0u; i < axis; ++i)
                key.push_back(ctx->slice_new(ctx, nullptr, nullptr, nullptr));
            ctx->retain(args[1]);
            key.push_back(args[1]);
            ref const k{ make_tuple(std::move(key)) };
            return array_getitem(v.get(), k.get());
        }

        object* f_median(object* aArgs, object* aKwargs);
        object* f_percentile(object* aArgs, object* aKwargs);
        object* f_quantile(object* aArgs, object* aKwargs);

        // (np.quantile, method 'linear': _quantile/_lerp)
        object* quantile_of(object* aArray, object* aQ, object* aAxis, bool aKeepdims, bool aPercent)
        {
            auto v = array_ref(aArray, {});
            if (!v)
                return nullptr;
            auto qv = array_ref(aQ, dtype::Float64);
            if (!qv)
                return nullptr;
            if (aPercent)
            {
                ref const hundred{ make_float(100.0) };
                qv = array_ref(ref{ binary("true_divide", qv.get(), hundred.get()) }.get(), {});
            }
            for (bool bad = false; !bad;)
            {
                for_each(*array_of(qv), [&](unsigned char* p) { auto const q = read_as<double>(p, dtype::Float64); if (!(q >= 0.0 && q <= 1.0)) bad = true; });
                if (bad)
                    return error("ValueError", aPercent ? "Percentiles must be in the range [0, 100]" : "Quantiles must be in the range [0, 1]");
                break;
            }
            auto* a = array_of(v);
            std::size_t axis = 0u;
            if (aAxis == nullptr || kind(aAxis) == NEOS_PY_NONE)
            {
                v = new_ref(ravel(a));
                a = array_of(v);
            }
            else
            {
                std::int64_t x = 0;
                if (!to_int(aAxis, x) || !normalize_axis(x, a->ndim(), axis))
                    return nullptr;
            }
            ref const sorted{ as_object(sort_along(a, axis, false)) };
            auto* const s = as_array(sorted.get());
            auto const n = s->shape[axis];
            auto* const q = array_of(qv);
            // result type: float64 for integers, else the input's (inexact) type
            auto const t = is_inexact(a->type) ? a->type : dtype::Float64;
            shape_t outer;
            shape_t outerStrides;
            for (std::size_t i = 0u; i < s->ndim(); ++i)
                if (i != axis)
                {
                    outer.push_back(s->shape[i]);
                    outerStrides.push_back(s->strides[i]);
                }
            shape_t shape = q->shape;
            shape.insert(shape.end(), outer.begin(), outer.end());
            auto* const r = new_array(t, shape);
            auto const outerCount = element_count(outer);
            auto* out = r->pointer();
            std::vector<std::int64_t> index;
            for (std::int64_t qi = 0; qi < q->size(); ++qi)
            {
                auto const quantile = read_as<double>(q->pointer() + qi * (q->ndim() ? q->strides.back() : 0), dtype::Float64);
                double const virtualIndex = quantile * static_cast<double>(n - 1);
                auto previous = static_cast<std::int64_t>(std::floor(virtualIndex));
                auto next = previous + 1;
                double const gamma = virtualIndex - static_cast<double>(previous);
                previous = std::clamp<std::int64_t>(previous, 0, n - 1);
                next = std::clamp<std::int64_t>(next, 0, n - 1);
                index.assign(outer.size(), 0);
                for (std::int64_t o = 0; o < outerCount; ++o, out += r->itemsize())
                {
                    std::int64_t offset = static_cast<std::int64_t>(s->offset);
                    for (std::size_t d = 0u; d < outer.size(); ++d)
                        offset += index[d] * outerStrides[d];
                    dispatch(t, [&](auto tag)
                    {
                        using T = decltype(tag);
                        using C = std::conditional_t<is_half_v<T>, float, T>;
                        if constexpr (std::is_floating_point_v<C>)
                        {
                            auto const below = cast<C>(read_as<T>(s->data->data() + offset + previous * s->strides[axis], s->type));
                            auto const above = cast<C>(read_as<T>(s->data->data() + offset + next * s->strides[axis], s->type));
                            // (a NaN in the data: NaN)
                            C result;
                            bool anyNan = std::isnan(cast<C>(read_as<T>(s->data->data() + offset + (n - 1) * s->strides[axis], s->type)));
                            if (anyNan)
                                result = std::numeric_limits<C>::quiet_NaN();
                            else
                            {
                                C const g = static_cast<C>(gamma);
                                C const diff = above - below;
                                result = g >= C(0.5) ? above - diff * (C(1) - g) : below + diff * g;
                            }
                            if constexpr (is_half_v<T>) write_as(out, t, half{ result }); else write_as(out, t, result);
                        }
                    });
                    for (std::size_t d = outer.size(); d-- > 0u;) { if (++index[d] < outer[d]) break; index[d] = 0; }
                }
            }
            (void)aKeepdims;
            return result_object(r);
        }

        object* f_quantile(object* aArgs, object* aKwargs)
        {
            arguments args{ "quantile", aArgs, aKwargs, { "a", "q", "axis", "out", "overwrite_input", "method", "keepdims", "*weights", "*interpolation" } };
            if (!args.ok())
                return nullptr;
            bool keepdims = false;
            if (!optional_bool(args, 6, false, keepdims))
                return nullptr;
            return quantile_of(args[0], args[1], args[2], keepdims, false);
        }

        object* f_percentile(object* aArgs, object* aKwargs)
        {
            arguments args{ "percentile", aArgs, aKwargs, { "a", "q", "axis", "out", "overwrite_input", "method", "keepdims", "*weights", "*interpolation" } };
            if (!args.ok())
                return nullptr;
            bool keepdims = false;
            if (!optional_bool(args, 6, false, keepdims))
                return nullptr;
            return quantile_of(args[0], args[1], args[2], keepdims, true);
        }

        object* f_median(object* aArgs, object* aKwargs)
        {
            arguments args{ "median", aArgs, aKwargs, { "a", "axis", "out", "overwrite_input", "keepdims" } };
            if (!args.ok())
                return nullptr;
            auto v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto* a = array_of(v);
            std::size_t axis = 0u;
            if (!args.has(1))
            {
                v = new_ref(ravel(a));
                a = array_of(v);
            }
            else
            {
                std::int64_t x = 0;
                if (!to_int(args[1], x) || !normalize_axis(x, a->ndim(), axis))
                    return nullptr;
            }
            auto const n = a->shape[axis];
            if (n == 0)
                return as_object(new_scalar(dtype::Float64, [&]() { scalar_value s; s.type = dtype::Float64; s.f = std::numeric_limits<double>::quiet_NaN(); return s; }()));
            ref const sorted{ as_object(sort_along(a, axis, false)) };
            auto* const s = as_array(sorted.get());
            // (the middle one or two along the axis: their mean)
            auto const k = n / 2;
            shape_t shape = s->shape;
            shape[axis] = (n % 2 == 0) ? 2 : 1;
            auto const start = (n % 2 == 0) ? k - 1 : k;
            ref const middle{ as_object(view_of(s, shape, s->strides, static_cast<std::size_t>(static_cast<std::int64_t>(s->offset) + start * s->strides[axis]))) };
            ref const meanArgs{ make_tuple({ (ctx->retain(middle.get()), middle.get()), make_int(static_cast<long long>(axis)) }) };
            ref r{ f_mean(meanArgs.get(), nullptr) };
            if (!r)
                return nullptr;
            // (a NaN anywhere along the axis: NaN; it is sorted last)
            if (is_float(s->type) || is_complex(s->type))
            {
                shape_t lastShape = s->shape;
                lastShape[axis] = 1;
                ref const last{ as_object(view_of(s, lastShape, s->strides, static_cast<std::size_t>(static_cast<std::int64_t>(s->offset) + (n - 1) * s->strides[axis]))) };
                ref const lastSqueezed{ as_object(reshape(as_array(last.get()), [&]() { shape_t x; for (std::size_t i = 0u; i < lastShape.size(); ++i) if (i != axis) x.push_back(lastShape[i]); return x; }())) };
                ref const nan{ unary("isnan", lastSqueezed.get()) };
                ref const nanValue{ make_float(std::numeric_limits<double>::quiet_NaN()) };
                ref const w{ make_tuple({ (ctx->retain(nan.get()), nan.get()), (ctx->retain(nanValue.get()), nanValue.get()), (ctx->retain(r.get()), r.get()) }) };
                ref const fixed{ f_where(w.get(), nullptr) };
                if (!fixed)
                    return nullptr;
                // (the type of the mean)
                auto const meanType = as_array(r.get())->type;
                auto* const fa = as_array(fixed.get());
                if (fa->type != meanType)
                {
                    auto* const c = copy_array(*fa, meanType);
                    return result_object(c);
                }
                return ref{ fixed }.release();
            }
            return r.release();
        }

        // (nan-ignoring reductions: NaNs replaced by the identity, or ignored)
        object* nan_reduction(char const* aName, object* aArgs, object* aKwargs)
        {
            std::string_view const name{ aName };
            arguments args{ aName, aArgs, aKwargs, { "a", "axis", "dtype", "out", "keepdims", "*initial", "*where", "*ddof" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto* const a = array_of(v);
            if (!is_inexact(a->type))
            {
                std::string const plain{ name.substr(3u) };
                return call_function(plain, aArgs, aKwargs);
            }
            ref const nanMask{ unary("isnan", v.get()) };
            if (name == "nansum" || name == "nanprod" || name == "nancumsum" || name == "nancumprod")
            {
                ref const fill{ make_float(name == "nanprod" || name == "nancumprod" ? 1.0 : 0.0) };
                ref const w{ make_tuple({ (ctx->retain(nanMask.get()), nanMask.get()), (ctx->retain(fill.get()), fill.get()), (ctx->retain(v.get()), v.get()) }) };
                ref const replaced{ f_where(w.get(), nullptr) };
                auto items = sequence_items(aArgs);
                std::vector<object*> rest{ replaced.get() };
                ctx->retain(replaced.get());
                for (std::size_t i = 1u; i < items.size(); ++i)
                {
                    ctx->retain(items[i]);
                    rest.push_back(items[i]);
                }
                ref const t{ make_tuple(rest) };
                return call_function(std::string{ name.substr(3u) }, t.get(), aKwargs);
            }
            // nanmin/nanmax/nanmean/nanvar/nanstd/nanargmin/nanargmax/nanmedian: computed on the non-NaN elements (along the axis)
            std::vector<std::size_t> axes;
            if (!to_axes(args[1], a->ndim(), axes))
                return nullptr;
            if (name == "nanmin" || name == "nanmax")
            {
                ref const fill{ make_float(name == "nanmin" ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity()) };
                ref const w{ make_tuple({ (ctx->retain(nanMask.get()), nanMask.get()), (ctx->retain(fill.get()), fill.get()), (ctx->retain(v.get()), v.get()) }) };
                ref const replaced{ f_where(w.get(), nullptr) };
                ref r{ reduce_array(u(name == "nanmin" ? "fmin" : "fmax"), as_array(replaced.get()), axes, {}, false, nullptr, nullptr) };
                // (all NaN: NaN)
                ref const allNan{ reduce_array(u("logical_and"), as_array(nanMask.get()), axes, dtype::Bool, false, nullptr, nullptr) };
                ref const nanValue{ make_float(std::numeric_limits<double>::quiet_NaN()) };
                ref const w2{ make_tuple({ (ctx->retain(allNan.get()), allNan.get()), (ctx->retain(nanValue.get()), nanValue.get()), (ctx->retain(r.get()), r.get()) }) };
                ref fixed{ f_where(w2.get(), nullptr) };
                if (fixed && as_array(fixed.get())->type != a->type)
                    return result_object(copy_array(*as_array(fixed.get()), a->type));
                return fixed.release();
            }
            if (name == "nanmean" || name == "nanvar" || name == "nanstd")
            {
                ref const zero{ make_float(0.0) };
                ref const w{ make_tuple({ (ctx->retain(nanMask.get()), nanMask.get()), (ctx->retain(zero.get()), zero.get()), (ctx->retain(v.get()), v.get()) }) };
                ref const replaced{ f_where(w.get(), nullptr) };
                ref const valid{ unary("logical_not", nanMask.get()) };
                ref const count{ reduce_array(u("add"), as_array(valid.get()), axes, dtype::Int64, name != "nanmean", nullptr, nullptr) };
                ref const total{ reduce_array(u("add"), as_array(replaced.get()), axes, {}, name != "nanmean", nullptr, nullptr) };
                ref mean{ binary("true_divide", total.get(), count.get()) };
                if (name == "nanmean")
                    return mean.release();
                double ddof = 0.0;
                if (args.has(7) && !to_double(args[7], ddof))
                    return nullptr;
                ref const deviation{ binary("subtract", v.get(), mean.get()) };
                ref const squared{ binary("multiply", deviation.get(), deviation.get()) };
                ref const w2{ make_tuple({ (ctx->retain(nanMask.get()), nanMask.get()), (ctx->retain(zero.get()), zero.get()), (ctx->retain(squared.get()), squared.get()) }) };
                ref const squaredValid{ f_where(w2.get(), nullptr) };
                ref const sum{ reduce_array(u("add"), as_array(squaredValid.get()), axes, {}, false, nullptr, nullptr) };
                ref const countFlat{ reduce_array(u("add"), as_array(valid.get()), axes, dtype::Int64, false, nullptr, nullptr) };
                ref const ddofValue{ make_float(ddof) };
                ref const divisor{ binary("subtract", countFlat.get(), ddofValue.get()) };
                ref r{ binary("true_divide", sum.get(), divisor.get()) };
                if (name == "nanstd")
                    r = ref{ unary("sqrt", r.get()) };
                return r.release();
            }
            return error("NotImplementedError", std::string{ aName } + " is not supported");
        }

        object* f_nansum(object* aArgs, object* aKwargs) { return nan_reduction("nansum", aArgs, aKwargs); }
        object* f_nanprod(object* aArgs, object* aKwargs) { return nan_reduction("nanprod", aArgs, aKwargs); }
        object* f_nanmin(object* aArgs, object* aKwargs) { return nan_reduction("nanmin", aArgs, aKwargs); }
        object* f_nanmax(object* aArgs, object* aKwargs) { return nan_reduction("nanmax", aArgs, aKwargs); }
        object* f_nanmean(object* aArgs, object* aKwargs) { return nan_reduction("nanmean", aArgs, aKwargs); }
        object* f_nanvar(object* aArgs, object* aKwargs) { return nan_reduction("nanvar", aArgs, aKwargs); }
        object* f_nanstd(object* aArgs, object* aKwargs) { return nan_reduction("nanstd", aArgs, aKwargs); }
        object* f_nancumsum(object* aArgs, object* aKwargs) { return nan_reduction("nancumsum", aArgs, aKwargs); }
        object* f_nancumprod(object* aArgs, object* aKwargs) { return nan_reduction("nancumprod", aArgs, aKwargs); }

        object* f_interp(object* aArgs, object* aKwargs)
        {
            arguments args{ "interp", aArgs, aKwargs, { "x", "xp", "fp", "left", "right", "period" } };
            if (!args.ok())
                return nullptr;
            auto const xv = array_ref(args[0], dtype::Float64);
            auto const xpv = array_ref(args[1], dtype::Float64);
            auto const fpv = array_ref(args[2], dtype::Float64);
            if (!xv || !xpv || !fpv)
                return nullptr;
            auto* const xp = array_of(xpv);
            auto* const fp = array_of(fpv);
            auto const n = xp->shape.at(0);
            if (n == 0)
                return error("ValueError", "array of sample points is empty");
            if (fp->shape.at(0) != n)
                return error("ValueError", "fp and xp are not of the same length.");
            std::vector<double> xs, fs;
            for (std::int64_t i = 0; i < n; ++i)
            {
                xs.push_back(read_as<double>(xp->pointer() + i * xp->strides[0], xp->type));
                fs.push_back(read_as<double>(fp->pointer() + i * fp->strides[0], fp->type));
            }
            double left = fs.front(), right = fs.back();
            if (args.has(3) && !to_double(args[3], left)) return nullptr;
            if (args.has(4) && !to_double(args[4], right)) return nullptr;
            auto* const x = array_of(xv);
            auto* const r = new_array(dtype::Float64, x->shape);
            iterate(x->shape, { x->pointer(), r->pointer() }, { x->strides, r->strides }, [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
            {
                auto p = aPointers[0];
                auto o = aPointers[1];
                for (std::int64_t i = 0; i < aCount; ++i, p += aStrides[0], o += aStrides[1])
                {
                    double const value = read_as<double>(p, dtype::Float64);
                    double result;
                    if (std::isnan(value))
                        result = value;
                    else if (value < xs.front())
                        result = left;
                    else if (value > xs.back())
                        result = right;
                    else if (value == xs.back())
                        result = fs.back();
                    else
                    {
                        auto const j = static_cast<std::size_t>(std::upper_bound(xs.begin(), xs.end(), value) - xs.begin()) - 1u;
                        double const slope = (fs[j + 1u] - fs[j]) / (xs[j + 1u] - xs[j]);
                        result = slope * (value - xs[j]) + fs[j];
                        if (std::isnan(result))
                        {
                            result = slope * (value - xs[j + 1u]) + fs[j + 1u];
                            if (std::isnan(result) && fs[j] == fs[j + 1u])
                                result = fs[j];
                        }
                    }
                    write_as(o, dtype::Float64, result);
                }
            });
            return result_object(r);
        }

        object* f_bincount(object* aArgs, object* aKwargs)
        {
            arguments args{ "bincount", aArgs, aKwargs, { "x", "weights", "minlength" } };
            if (!args.ok())
                return nullptr;
            auto const xv = array_ref(args[0], {});
            if (!xv)
                return nullptr;
            auto* const x = array_of(xv);
            std::int64_t minlength = 0;
            if (args.has(2) && !to_int(args[2], minlength))
                return nullptr;
            std::int64_t length = minlength;
            for_each(*x, [&](unsigned char* p) { length = std::max(length, read_as<std::int64_t>(p, x->type) + 1); });
            bool negative = false;
            for_each(*x, [&](unsigned char* p) { if (read_as<std::int64_t>(p, x->type) < 0) negative = true; });
            if (negative)
                return error("ValueError", "'list' argument must have no negative elements");
            if (args.has(1))
            {
                auto const wv = array_ref(args[1], dtype::Float64);
                if (!wv)
                    return nullptr;
                auto* const r = new_array(dtype::Float64, { length });
                auto* const w = array_of(wv);
                iterate(x->shape, { x->pointer(), w->pointer() }, { x->strides, w->strides }, [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
                {
                    auto p = aPointers[0], q = aPointers[1];
                    for (std::int64_t i = 0; i < aCount; ++i, p += aStrides[0], q += aStrides[1])
                    {
                        auto* const bin = r->pointer() + read_as<std::int64_t>(p, x->type) * 8;
                        write_as(bin, dtype::Float64, read_as<double>(bin, dtype::Float64) + read_as<double>(q, dtype::Float64));
                    }
                });
                return as_object(r);
            }
            auto* const r = new_array(dtype::Int64, { length });
            for_each(*x, [&](unsigned char* p)
            {
                auto* const bin = r->pointer() + read_as<std::int64_t>(p, x->type) * 8;
                write_as(bin, dtype::Int64, read_as<std::int64_t>(bin, dtype::Int64) + 1);
            });
            return as_object(r);
        }

        object* f_nan_to_num(object* aArgs, object* aKwargs)
        {
            arguments args{ "nan_to_num", aArgs, aKwargs, { "x", "copy", "nan", "posinf", "neginf" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            auto* const r = copy_array(*array_of(v));
            r->scalar = array_of(v)->scalar;
            r->base.type = array_of(v)->base.type;
            if (!is_inexact(r->type))
                return as_object(r);
            double nan = 0.0;
            double large = r->type == dtype::Float16 || r->type == dtype::Float32 || r->type == dtype::Complex64 ?
                (r->type == dtype::Float16 ? 65504.0 : static_cast<double>(std::numeric_limits<float>::max())) : std::numeric_limits<double>::max();
            double posinf = large, neginf = -large;
            if (args.has(2) && !to_double(args[2], nan)) return nullptr;
            if (args.has(3) && !to_double(args[3], posinf)) return nullptr;
            if (args.has(4) && !to_double(args[4], neginf)) return nullptr;
            auto const fix = [&](double x) { return std::isnan(x) ? nan : std::isinf(x) ? (x > 0 ? posinf : neginf) : x; };
            for_each(*r, [&](unsigned char* p)
            {
                if (is_complex(r->type))
                {
                    auto const c = read_as<std::complex<double>>(p, r->type);
                    write_as(p, r->type, std::complex<double>{ fix(c.real()), fix(c.imag()) });
                }
                else
                    write_as(p, r->type, fix(read_as<double>(p, r->type)));
            });
            return as_object(r);
        }

        object* part(object* aArgs, object* aKwargs, char const* aName, bool aImag)
        {
            arguments args{ aName, aArgs, aKwargs, { "val" } };
            if (!args.ok())
                return nullptr;
            auto const v = array_ref(args[0], {});
            if (!v)
                return nullptr;
            return array_getattr(as_object(array_of(v)), aImag ? "imag" : "real");
        }

        object* f_real(object* aArgs, object* aKwargs) { return part(aArgs, aKwargs, "real", false); }
        object* f_imag(object* aArgs, object* aKwargs) { return part(aArgs, aKwargs, "imag", true); }

        object* f_angle(object* aArgs, object* aKwargs)
        {
            arguments args{ "angle", aArgs, aKwargs, { "z", "deg" } };
            if (!args.ok())
                return nullptr;
            bool degrees = false;
            if (!optional_bool(args, 1, false, degrees))
                return nullptr;
            ref const im{ part(ref{ make_tuple({ (ctx->retain(args[0]), args[0]) }) }.get(), nullptr, "imag", true) };
            ref const re{ part(ref{ make_tuple({ (ctx->retain(args[0]), args[0]) }) }.get(), nullptr, "real", false) };
            if (!im || !re)
                return nullptr;
            ref r{ binary("arctan2", im.get(), re.get()) };
            if (degrees && r)
                r = ref{ unary("rad2deg", r.get()) };
            return r.release();
        }
    }

    void register_math()
    {
        register_functions({
            { "sum", f_sum }, { "prod", f_prod }, { "max", f_max }, { "amax", f_max }, { "min", f_min }, { "amin", f_min }, { "all", f_all },
            { "any", f_any }, { "mean", f_mean }, { "var", f_var }, { "std", f_std }, { "ptp", f_ptp }, { "argmax", f_argmax }, { "argmin", f_argmin },
            { "cumsum", f_cumsum }, { "cumprod", f_cumprod }, { "cumulative_sum", f_cumsum }, { "cumulative_prod", f_cumprod },
            { "count_nonzero", f_count_nonzero }, { "nonzero", f_nonzero }, { "flatnonzero", f_flatnonzero }, { "argwhere", f_argwhere },
            { "where", f_where }, { "sort", f_sort }, { "argsort", f_argsort }, { "partition", f_partition }, { "argpartition", f_argpartition }, { "unique", f_unique }, { "searchsorted", f_searchsorted },
            { "matmul", f_matmul }, { "dot", f_dot }, { "vdot", f_vdot }, { "vecdot", f_vecdot }, { "matvec", f_matvec }, { "vecmat", f_vecmat }, { "inner", f_inner }, { "outer", f_outer }, { "cross", f_cross },
            { "trace", f_trace }, { "clip", f_clip }, { "round", f_round }, { "around", f_round }, { "fix", f_fix }, { "diff", f_diff },
            { "isclose", f_isclose }, { "allclose", f_allclose }, { "array_equal", f_array_equal }, { "array_equiv", f_array_equiv },
            { "take", f_take }, { "median", f_median }, { "percentile", f_percentile }, { "quantile", f_quantile }, { "nansum", f_nansum }, { "nanprod", f_nanprod }, { "nanmin", f_nanmin }, { "nanmax", f_nanmax }, { "nanmean", f_nanmean },
            { "nanvar", f_nanvar }, { "nanstd", f_nanstd }, { "nancumsum", f_nancumsum }, { "nancumprod", f_nancumprod }, { "interp", f_interp },
            { "bincount", f_bincount }, { "nan_to_num", f_nan_to_num }, { "real", f_real }, { "imag", f_imag }, { "angle", f_angle }
        });
    }
}
