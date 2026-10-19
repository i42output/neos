/*
  lib2.cpp

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

  Portions of this file are derived from NumPy 2.5.3 (numpy/lib/_function_base_impl.py, numpy/lib/_histograms_impl.py,
  numpy/lib/_twodim_base_impl.py, numpy/lib/_type_check_impl.py, numpy/lib/_ufunclike_impl.py,
  numpy/lib/_nanfunctions_impl.py, numpy/_core/numeric.py, numpy/_core/src/multiarray/multiarraymodule.c and
  numpy/_core/src/multiarray/compiled_base.c), Copyright (c) 2005-2025, NumPy Developers; All rights reserved. They are
  used under NumPy's BSD 3-Clause licence (see runtime/numpy/LICENSE.NumPy). Changes: translated from C and Python to
  C++ for the neos NumPy runtime plugin and adapted to its object model (see runtime/numpy/NOTICE).

  The coefficients and Chebyshev series of i0 (the modified Bessel function of order zero) are, as NumPy's, from the
  Cephes Math Library by Stephen L. Moshier.
*/

// numerical functions that NumPy implements in Python (numpy/lib/_function_base_impl.py, _histograms_impl.py, _nanfunctions_impl.py, ...)

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include "numpy.hpp"
#include "cast.hpp"
#include "functions.hpp"
#include "py.hpp"
#include "blas.hpp"

namespace neos::numpy
{
    namespace
    {
        using namespace py;

        value arg(arguments const& aArgs, std::size_t aIndex)
        {
            return value::borrow(aArgs.get(aIndex));
        }

        value arg(arguments const& aArgs, std::size_t aIndex, value const& aDefault)
        {
            return aArgs.has(aIndex) ? value::borrow(aArgs.get(aIndex)) : aDefault;
        }

        value asarray(value const& aValue)
        {
            return np("asarray")(aValue);
        }

        value int_tuple(std::vector<long long> const& aValues)
        {
            std::vector<value> items;
            for (auto v : aValues)
                items.emplace_back(v);
            return tuple(items);
        }

        long long normalize_axis_index(long long aAxis, long long aNdim)
        {
            if (aAxis < -aNdim || aAxis >= aNdim)
                raise("AxisError", "axis " + std::to_string(aAxis) + " is out of bounds for array of dimension " + std::to_string(aNdim));
            return aAxis < 0 ? aAxis + aNdim : aAxis;
        }

        std::vector<long long> normalize_axis_tuple(value const& aAxis, long long aNdim)
        {
            std::vector<long long> result;
            if (is_int(aAxis) || is_array(aAxis.get()))
                result.push_back(normalize_axis_index(aAxis.to_int(), aNdim));
            else
                for (auto const& x : aAxis.items())
                    result.push_back(normalize_axis_index(x.to_int(), aNdim));
            for (std::size_t i = 0u; i < result.size(); ++i)
                for (std::size_t j = i + 1u; j < result.size(); ++j)
                    if (result[i] == result[j])
                        raise("ValueError", "repeated axis");
            return result;
        }

        dtype type_of(value const& aArray)
        {
            return aArray.array()->type;
        }

        value dtype_value(dtype aType)
        {
            return value::borrow(dtype_object(aType));
        }

        value slices(long long aNdim, long long aAxis, value const& aSlice)
        {
            std::vector<value> s(static_cast<std::size_t>(aNdim), all());
            s[static_cast<std::size_t>(aAxis)] = aSlice;
            return tuple(s);
        }

        // --- statistics

        value weights_are_valid(value const& aWeights, value const& a, std::vector<long long> const* aAxis)
        {
            auto wgt = asarray(aWeights);
            if (a.array()->shape != wgt.array()->shape)
            {
                if (aAxis == nullptr)
                    raise("TypeError", "Axis must be specified when shapes of a and weights differ.");
                if (static_cast<long long>(aAxis->size()) != wgt.ndim() || wgt.array()->shape != [&]() { shape_t s; for (auto x : *aAxis) s.push_back(a.array()->shape[static_cast<std::size_t>(x)]); return s; }())
                    raise("ValueError", "Shape of weights must be consistent with shape of a along specified axis.");
                // (the weights broadcast along the axes)
                std::vector<long long> order(aAxis->begin(), aAxis->end());
                std::vector<long long> sorted = order;
                std::sort(sorted.begin(), sorted.end());
                auto const n = a.ndim();
                std::vector<long long> shape(static_cast<std::size_t>(n), 1);
                for (std::size_t i = 0u; i < sorted.size(); ++i)
                    shape[static_cast<std::size_t>(sorted[i])] = a.array()->shape[static_cast<std::size_t>(sorted[i])];
                // (wgt's axes are in the order of aAxis: put them in sorted order)
                std::vector<long long> perm;
                for (auto x : sorted)
                    perm.push_back(std::find(order.begin(), order.end(), x) - order.begin());
                wgt = wgt.method("transpose", int_tuple(perm));
                wgt = np("broadcast_to")(wgt.method("reshape", int_tuple(shape)), a.shape());
            }
            return wgt;
        }

        object* f_average(object* aArgs, object* aKwargs)
        {
            arguments args{ "average", aArgs, aKwargs, { "a", "axis", "weights", "returned", "*keepdims" } };
            if (!args.ok())
                return nullptr;
            auto const a = np("asanyarray")(arg(args, 0));
            std::vector<long long> axisTuple;
            value axis = py::none();
            if (args.has(1))
            {
                axisTuple = normalize_axis_tuple(arg(args, 1), a.ndim());
                axis = int_tuple(axisTuple);
            }
            value const keepdims = args.get(4) != nullptr ? arg(args, 4) : value{};
            value avg, avgAsArray, scl;
            if (!args.has(2))
            {
                avg = a.method("mean", axis, kw{ "keepdims", keepdims });
                avgAsArray = np("asanyarray")(avg);
                auto const t = type_of(avgAsArray);
                scl = np(info(t).scalarName)(value{ static_cast<double>(a.size()) / static_cast<double>(avgAsArray.size()) });
            }
            else
            {
                auto const wgt = weights_are_valid(arg(args, 2), a, args.has(1) ? &axisTuple : nullptr);
                value resultDtype;
                if (is_integer(type_of(a)) || is_bool(type_of(a)))
                    resultDtype = np("result_type")(dtype_of(a), dtype_of(wgt), "f8");
                else
                    resultDtype = np("result_type")(dtype_of(a), dtype_of(wgt));
                scl = wgt.method("sum", kw{ "axis", axis }, kw{ "dtype", resultDtype }, kw{ "keepdims", keepdims });
                if (np("any")(scl == 0.0).truth())
                    raise("ZeroDivisionError", "Weights sum to zero, can't be normalized");
                avg = avgAsArray = np("multiply")(a, wgt, kw{ "dtype", resultDtype }).method("sum", axis, kw{ "keepdims", keepdims }) / scl;
            }
            if (arg(args, 3, false).truth())
            {
                if (!(np("shape")(scl) == np("shape")(avgAsArray)).truth())
                    scl = np("broadcast_to")(scl, avgAsArray.shape()).method("copy");
                return tuple({ avg, scl }).release();
            }
            return avg.release();
        }

        object* f_cov(object* aArgs, object* aKwargs)
        {
            arguments args{ "cov", aArgs, aKwargs, { "m", "y", "rowvar", "bias", "ddof", "fweights", "aweights", "*dtype" } };
            if (!args.ok())
                return nullptr;
            if (args.has(4) && arg(args, 4).to_double() != static_cast<double>(arg(args, 4).to_int()))
                raise("ValueError", "ddof must be integer");
            auto const m = asarray(arg(args, 0));
            if (m.ndim() > 2)
                raise("ValueError", "m has more than 2 dimensions");
            value y;
            if (args.has(1))
            {
                y = asarray(arg(args, 1));
                if (y.ndim() > 2)
                    raise("ValueError", "y has more than 2 dimensions");
            }
            value dt = args.has(7) ? arg(args, 7) : (y.empty() ? np("result_type")(m, np("float64")) : np("result_type")(m, y, np("float64")));
            auto X = np("array")(m, kw{ "ndmin", value{ 2 } }, kw{ "dtype", dt });
            bool const rowvar = arg(args, 2, true).truth();
            if (!rowvar && m.ndim() != 1)
                X = X.attr("T");
            if (X.array()->shape[0] == 0)
                return np("array")(list({})).method("reshape", 0, 0).release();
            if (!y.empty())
            {
                y = np("array")(y, kw{ "ndmin", value{ 2 } }, kw{ "dtype", dt });
                if (!rowvar && y.array()->shape[0] != 1)
                    y = y.attr("T");
                X = np("concatenate")(tuple({ X, y }), kw{ "axis", value{ 0 } });
            }
            long long ddof;
            if (!args.has(4))
                ddof = arg(args, 3, false).truth() ? 0 : 1;
            else
                ddof = arg(args, 4).to_int();
            value w;
            value aweights;
            if (args.has(5))
            {
                auto fweights = np("asarray")(arg(args, 5), kw{ "dtype", np("float64") });
                if (!np("all")(fweights == np("around")(fweights)).truth())
                    raise("TypeError", "fweights must be integer");
                if (fweights.ndim() > 1)
                    raise("RuntimeError", "cannot handle multidimensional fweights");
                if (fweights.array()->shape[0] != X.array()->shape[1])
                    raise("RuntimeError", "incompatible numbers of samples and fweights");
                if (np("any")(fweights < 0).truth())
                    raise("ValueError", "fweights cannot be negative");
                w = fweights;
            }
            if (args.has(6))
            {
                aweights = np("asarray")(arg(args, 6), kw{ "dtype", np("float64") });
                if (aweights.ndim() > 1)
                    raise("RuntimeError", "cannot handle multidimensional aweights");
                if (aweights.array()->shape[0] != X.array()->shape[1])
                    raise("RuntimeError", "incompatible numbers of samples and aweights");
                if (np("any")(aweights < 0).truth())
                    raise("ValueError", "aweights cannot be negative");
                w = w.empty() ? aweights : w * aweights;
            }
            auto const result = np("average")(X, kw{ "axis", value{ 1 } }, kw{ "weights", w.empty() ? py::none() : w }, kw{ "returned", value{ true } }).items();
            auto const avg = result[0];
            auto const wSum = result[1][0];
            value fact;
            if (w.empty())
                fact = value{ X.array()->shape[1] - ddof };
            else if (ddof == 0)
                fact = wSum;
            else if (aweights.empty())
                fact = wSum - ddof;
            else
                fact = wSum - value{ ddof } * np("sum")(w * aweights) / wSum;
            if ((fact <= 0).truth())
                fact = value{ 0.0 };
            X = X - avg[tuple({ all(), py::none() })];
            auto const XT = w.empty() ? X.attr("T") : (X * w).attr("T");
            auto c = np("dot")(X, XT.method("conj"));
            c = c * np("true_divide")(1, fact);
            return c.method("squeeze").release();
        }

        object* f_corrcoef(object* aArgs, object* aKwargs)
        {
            arguments args{ "corrcoef", aArgs, aKwargs, { "x", "y", "rowvar", "bias", "ddof", "*dtype" } };
            if (!args.ok())
                return nullptr;
            auto c = np("cov")(arg(args, 0), arg(args, 1, py::none()), arg(args, 2, true), kw{ "dtype", arg(args, 5, py::none()) });
            if (c.ndim() != 2)
                return (c / c).release();
            auto const d = np("diag")(c);
            auto const stddev = np("sqrt")(d.attr("real"));
            c = c / stddev[tuple({ all(), py::none() })];
            c = c / stddev[tuple({ py::none(), all() })];
            if (is_complex(type_of(c)))
            {
                np("clip")(c.attr("real"), -1, 1, kw{ "out", c.attr("real") });
                np("clip")(c.attr("imag"), -1, 1, kw{ "out", c.attr("imag") });
            }
            else
                np("clip")(c, -1, 1, kw{ "out", c });
            return c.release();
        }

        object* f_trapezoid(object* aArgs, object* aKwargs)
        {
            arguments args{ "trapezoid", aArgs, aKwargs, { "y", "x", "dx", "axis" } };
            if (!args.ok())
                return nullptr;
            auto const y = np("asanyarray")(arg(args, 0));
            auto const axis = arg(args, 3, -1).to_int();
            value d;
            if (!args.has(1))
                d = arg(args, 2, 1.0);
            else
            {
                auto const x = np("asanyarray")(arg(args, 1));
                if (x.ndim() == 1)
                {
                    d = np("diff")(x);
                    std::vector<long long> shape(static_cast<std::size_t>(y.ndim()), 1);
                    shape[static_cast<std::size_t>(normalize_axis_index(axis, y.ndim()))] = d.array()->shape[0];
                    d = d.method("reshape", int_tuple(shape));
                }
                else
                    d = np("diff")(x, kw{ "axis", value{ axis } });
            }
            auto const nd = y.ndim();
            auto const ax = normalize_axis_index(axis, nd);
            auto const s1 = slices(nd, ax, slice(1, {}));
            auto const s2 = slices(nd, ax, slice({}, -1));
            return (d * (y[s1] + y[s2]) / 2.0).method("sum", axis).release();
        }

        object* f_gradient(object* aArgs, object* aKwargs)
        {
            arguments args{ "gradient", aArgs, aKwargs, { "f", "...", "*axis", "*edge_order" } };
            if (!args.ok())
                return nullptr;
            auto f = np("asanyarray")(arg(args, 0));
            auto const N = f.ndim();
            std::vector<long long> axes;
            if (!args.has(2))
                for (long long i = 0; i < N; ++i)
                    axes.push_back(i);
            else
                axes = normalize_axis_tuple(arg(args, 2), N);
            auto const lenAxes = axes.size();
            auto const& varargs = args.extra();
            std::vector<value> dx;
            if (varargs.empty())
                dx.assign(lenAxes, value{ 1.0 });
            else if (varargs.size() == 1u && np("ndim")(value::borrow(varargs[0])).to_int() == 0)
                dx.assign(lenAxes, value::borrow(varargs[0]));
            else if (varargs.size() == lenAxes)
            {
                for (std::size_t i = 0u; i < lenAxes; ++i)
                {
                    auto distances = np("asanyarray")(value::borrow(varargs[i]));
                    if (distances.ndim() == 0)
                    {
                        dx.push_back(value::borrow(varargs[i]));
                        continue;
                    }
                    if (distances.ndim() != 1)
                        raise("ValueError", "distances must be either scalars or 1d");
                    if (distances.len() != f.array()->shape[static_cast<std::size_t>(axes[i])])
                        raise("ValueError", "when 1d, distances must match the length of the corresponding dimension");
                    if (is_integer(type_of(distances)))
                        distances = distances.method("astype", np("float64"));
                    auto diffx = np("diff")(distances);
                    if ((diffx == diffx[0]).method("all").truth())
                        diffx = diffx[0];
                    dx.push_back(diffx);
                }
            }
            else
                raise("TypeError", "invalid number of arguments");
            auto const edgeOrder = arg(args, 3, 1).to_int();
            if (edgeOrder > 2)
                raise("ValueError", "'edge_order' greater than 2 not supported");
            std::vector<value> outvals;
            value otype = dtype_of(f);
            if (!is_inexact(type_of(f)))
            {
                if (is_integer(type_of(f)))
                    f = f.method("astype", np("float64"));
                otype = np("float64");
            }
            for (std::size_t k = 0u; k < lenAxes; ++k)
            {
                auto const axis = axes[k];
                auto const& axDx = dx[k];
                if (f.array()->shape[static_cast<std::size_t>(axis)] < edgeOrder + 1)
                    raise("ValueError", "Shape of array too small to calculate a numerical gradient, at least (edge_order + 1) elements are required.");
                auto const out = np("empty_like")(f, kw{ "dtype", otype });
                bool const uniform = np("ndim")(axDx).to_int() == 0;
                auto const sl = [&](value const& aSlice) { return slices(N, axis, aSlice); };
                if (uniform)
                    out.set(sl(slice(1, -1)), (f[sl(slice(2, {}))] - f[sl(slice({}, -2))]) / (value{ 2.0 } * axDx));
                else
                {
                    auto const dx1 = axDx[slice(0, -1)];
                    auto const dx2 = axDx[slice(1, {})];
                    auto a = -dx2 / (dx1 * (dx1 + dx2));
                    auto b = (dx2 - dx1) / (dx1 * dx2);
                    auto c = dx1 / (dx2 * (dx1 + dx2));
                    std::vector<long long> shape(static_cast<std::size_t>(N), 1);
                    shape[static_cast<std::size_t>(axis)] = -1;
                    a = a.method("reshape", int_tuple(shape));
                    b = b.method("reshape", int_tuple(shape));
                    c = c.method("reshape", int_tuple(shape));
                    out.set(sl(slice(1, -1)), a * f[sl(slice({}, -2))] + b * f[sl(slice(1, -1))] + c * f[sl(slice(2, {}))]);
                }
                if (edgeOrder == 1)
                {
                    auto const dx0 = uniform ? axDx : axDx[0];
                    out.set(sl(0), (f[sl(1)] - f[sl(0)]) / dx0);
                    auto const dxn = uniform ? axDx : axDx[-1];
                    out.set(sl(-1), (f[sl(-1)] - f[sl(-2)]) / dxn);
                }
                else
                {
                    value a, b, c;
                    if (uniform)
                    {
                        a = value{ -1.5 } / axDx;
                        b = value{ 2.0 } / axDx;
                        c = value{ -0.5 } / axDx;
                    }
                    else
                    {
                        auto const dx1 = axDx[0];
                        auto const dx2 = axDx[1];
                        a = -(value{ 2.0 } * dx1 + dx2) / (dx1 * (dx1 + dx2));
                        b = (dx1 + dx2) / (dx1 * dx2);
                        c = -dx1 / (dx2 * (dx1 + dx2));
                    }
                    out.set(sl(0), a * f[sl(0)] + b * f[sl(1)] + c * f[sl(2)]);
                    if (uniform)
                    {
                        a = value{ 0.5 } / axDx;
                        b = value{ -2.0 } / axDx;
                        c = value{ 1.5 } / axDx;
                    }
                    else
                    {
                        auto const dx1 = axDx[-2];
                        auto const dx2 = axDx[-1];
                        a = dx2 / (dx1 * (dx1 + dx2));
                        b = -(dx2 + dx1) / (dx1 * dx2);
                        c = (value{ 2.0 } * dx2 + dx1) / (dx2 * (dx1 + dx2));
                    }
                    out.set(sl(-1), a * f[sl(-3)] + b * f[sl(-2)] + c * f[sl(-1)]);
                }
                outvals.push_back(out);
            }
            if (lenAxes == 1u)
                return outvals[0].release();
            return tuple(outvals).release();
        }

        object* f_sinc(object* aArgs, object* aKwargs)
        {
            arguments args{ "sinc", aArgs, aKwargs, { "x" } };
            if (!args.ok())
                return nullptr;
            auto x = np("asanyarray")(arg(args, 0));
            x = value{ std::numbers::pi } * x;
            value eps{ 1e-20 };
            if (is_float(type_of(x)))
                eps = np("finfo")(dtype_of(x)).attr("eps");
            auto const y = np("where")(x, x, eps);
            return (np("sin")(y) / y).release();
        }

        object* f_unwrap(object* aArgs, object* aKwargs)
        {
            arguments args{ "unwrap", aArgs, aKwargs, { "p", "discont", "axis", "*period" } };
            if (!args.ok())
                return nullptr;
            auto const p = asarray(arg(args, 0));
            auto const nd = p.ndim();
            auto const axis = arg(args, 2, -1).to_int();
            auto const dd = np("diff")(p, kw{ "axis", value{ axis } });
            auto const period = arg(args, 3, 2 * std::numbers::pi);
            auto const discont = arg(args, 1, period / 2);
            auto const slice1 = slices(nd, normalize_axis_index(axis, nd), slice(1, {}));
            auto const dt = np("result_type")(dd, period);
            value intervalHigh;
            bool boundaryAmbiguous;
            if (is_integer(*dtype_of_object(dt.get())))
            {
                auto const qr = np("divmod")(period, 2).items();
                intervalHigh = qr[0];
                boundaryAmbiguous = (qr[1] == 0).truth();
            }
            else
            {
                intervalHigh = period / 2;
                boundaryAmbiguous = true;
            }
            auto const intervalLow = -intervalHigh;
            auto const ddmod = np("mod")(dd - intervalLow, period) + intervalLow;
            if (boundaryAmbiguous)
                np("copyto")(ddmod, intervalHigh, kw{ "where", (ddmod == intervalLow) & (dd > 0) });
            auto const phCorrect = ddmod - dd;
            np("copyto")(phCorrect, 0, kw{ "where", np("abs")(dd) < discont });
            auto const up = np("array")(p, kw{ "copy", value{ true } }, kw{ "dtype", dt });
            up.set(slice1, p[slice1] + phCorrect.method("cumsum", axis));
            return up.release();
        }

        object* f_vander(object* aArgs, object* aKwargs)
        {
            arguments args{ "vander", aArgs, aKwargs, { "x", "N", "increasing" } };
            if (!args.ok())
                return nullptr;
            auto const x = asarray(arg(args, 0));
            if (x.ndim() != 1)
                raise("ValueError", "x must be a one-dimensional array or sequence.");
            auto const N = args.has(1) ? arg(args, 1).to_int() : x.len();
            auto const t = promote(type_of(x), dtype::Int64);
            auto* const v = new_array(t, { x.len(), N });
            auto const result = array_value(v);
            bool const increasing = arg(args, 2, false).truth();
            auto* const xa = x.array();
            for (std::int64_t i = 0; i < x.len(); ++i)
            {
                auto const* xp = xa->pointer() + i * xa->strides[0];
                // (multiply.accumulate along the row: powers by repeated multiplication)
                std::vector<unsigned char> current(v->itemsize());
                scalar_value one;
                one.type = dtype::Int64;
                one.i = 1;
                for (std::int64_t j = 0; j < N; ++j)
                {
                    auto* const out = v->pointer() + i * v->strides[0] + (increasing ? j : N - 1 - j) * v->strides[1];
                    if (j == 0)
                        store(out, t, one);
                    else if (j == 1)
                        cast_element(xp, xa->type, out, t);
                    else
                    {
                        auto* const previous = v->pointer() + i * v->strides[0] + (increasing ? j - 1 : N - j) * v->strides[1];
                        std::vector<unsigned char> xv(v->itemsize());
                        cast_element(xp, xa->type, xv.data(), t);
                        dispatch(t, [&](auto tag)
                        {
                            using T = decltype(tag);
                            if constexpr (is_half_v<T>)
                                write_as(out, t, half{ static_cast<float>(read_as<float>(previous, t) * read_as<float>(xv.data(), t)) });
                            else if constexpr (!is_bool_v<T>)
                                write_as(out, t, static_cast<T>(read_as<T>(previous, t) * read_as<T>(xv.data(), t)));
                        });
                    }
                }
            }
            return result.release();
        }

        double const sI0A[] = { -4.4153416464793395e-18, 3.3307945188222384e-17, -2.431279846547955e-16, 1.715391285555133e-15, -1.1685332877993451e-14, 7.676185498604936e-14, -4.856446783111929e-13, 2.95505266312964e-12, -1.726826291441556e-11, 9.675809035373237e-11, -5.189795601635263e-10, 2.6598237246823866e-09, -1.300025009986248e-08, 6.046995022541919e-08, -2.670793853940612e-07, 1.1173875391201037e-06, -4.4167383584587505e-06, 1.6448448070728896e-05, -5.754195010082104e-05, 0.00018850288509584165, -0.0005763755745385824, 0.0016394756169413357, -0.004324309995050576, 0.010546460394594998, -0.02373741480589947, 0.04930528423967071, -0.09490109704804764, 0.17162090152220877, -0.3046826723431984, 0.6767952744094761 };
        double const sI0B[] = { -7.233180487874754e-18, -4.830504485944182e-18, 4.46562142029676e-17, 3.461222867697461e-17, -2.8276239805165836e-16, -3.425485619677219e-16, 1.7725601330565263e-15, 3.8116806693526224e-15, -9.554846698828307e-15, -4.150569347287222e-14, 1.54008621752141e-14, 3.8527783827421426e-13, 7.180124451383666e-13, -1.7941785315068062e-12, -1.3215811840447713e-11, -3.1499165279632416e-11, 1.1889147107846439e-11, 4.94060238822497e-10, 3.3962320257083865e-09, 2.266668990498178e-08, 2.0489185894690638e-07, 2.8913705208347567e-06, 6.889758346916825e-05, 0.0033691164782556943, 0.8044904110141088 };

        value chbevl(value const& x, double const* aValues, std::size_t aCount)
        {
            value b0{ aValues[0] };
            value b1{ 0.0 };
            value b2;
            for (std::size_t i = 1u; i < aCount; ++i)
            {
                b2 = b1;
                b1 = b0;
                b0 = x * b1 - b2 + aValues[i];
            }
            return value{ 0.5 } * (b0 - b2);
        }

        object* f_i0(object* aArgs, object* aKwargs)
        {
            arguments args{ "i0", aArgs, aKwargs, { "x" } };
            if (!args.ok())
                return nullptr;
            auto x = np("asanyarray")(arg(args, 0));
            if (is_complex(type_of(x)))
                raise("TypeError", "i0 not supported for complex values");
            if (!is_float(type_of(x)))
                x = x.method("astype", np("float64"));
            x = np("abs")(x);
            auto const y = np("zeros_like")(x);
            auto const low = x <= 8.0;
            auto const high = ~low;
            auto const xl = x[low];
            if (xl.size() > 0)
                y.set(low, np("exp")(xl) * chbevl(xl / 2.0 - 2, sI0A, std::size(sI0A)));
            auto const xh = x[high];
            if (xh.size() > 0)
                y.set(high, np("exp")(xh) * chbevl(value{ 32.0 } / xh - 2.0, sI0B, std::size(sI0B)) / np("sqrt")(xh));
            return y.release();
        }

        object* window(char const* aName, object* aArgs, object* aKwargs)
        {
            arguments args{ aName, aArgs, aKwargs, { "M" } };
            if (!args.ok())
                return nullptr;
            auto const values = np("array")(list({ value{ 0.0 }, arg(args, 0) }));
            auto const M = values[1];
            if ((M < 1).truth())
                return np("array")(list({}), kw{ "dtype", dtype_of(values) }).release();
            if ((M == 1).truth())
                return np("ones")(1, kw{ "dtype", dtype_of(values) }).release();
            auto const n = np("arange")(value{ 1 } - M, M, 2);
            std::string_view const name{ aName };
            auto const pi = value{ std::numbers::pi };
            if (name == "bartlett")
                return np("where")(np("less_equal")(n, 0), value{ 1 } + n / (M - 1), value{ 1 } - n / (M - 1)).release();
            if (name == "blackman")
                return (value{ 0.42 } + value{ 0.5 } * np("cos")(pi * n / (M - 1)) + value{ 0.08 } * np("cos")(value{ 2.0 } * pi * n / (M - 1))).release();
            if (name == "hamming")
                return (value{ 0.54 } + value{ 0.46 } * np("cos")(pi * n / (M - 1))).release();
            return (value{ 0.5 } + value{ 0.5 } * np("cos")(pi * n / (M - 1))).release();
        }

        object* f_bartlett(object* aArgs, object* aKwargs) { return window("bartlett", aArgs, aKwargs); }
        object* f_blackman(object* aArgs, object* aKwargs) { return window("blackman", aArgs, aKwargs); }
        object* f_hamming(object* aArgs, object* aKwargs) { return window("hamming", aArgs, aKwargs); }
        object* f_hanning(object* aArgs, object* aKwargs) { return window("hanning", aArgs, aKwargs); }

        object* f_kaiser(object* aArgs, object* aKwargs)
        {
            arguments args{ "kaiser", aArgs, aKwargs, { "M", "beta" } };
            if (!args.ok())
                return nullptr;
            auto const values = np("array")(list({ value{ 0.0 }, arg(args, 0), arg(args, 1) }));
            auto const M = values[1];
            auto const beta = values[2];
            if ((M == 1).truth())
                return np("ones")(1, kw{ "dtype", dtype_of(values) }).release();
            auto const n = np("arange")(0, M);
            auto const alpha = (M - 1) / 2.0;
            return (np("i0")(beta * np("sqrt")(value{ 1 } - pow((n - alpha) / alpha, 2.0))) / np("i0")(beta)).release();
        }

        // (NumPy's _pyarray_correlate: the dot products in its order)
        value pyarray_correlate(value const& aAp1, value const& aAp2, dtype aType, int aMode, bool& aInverted)
        {
            auto ap1 = aAp1, ap2 = aAp2;
            auto n1 = ap1.len(), n2 = ap2.len();
            aInverted = false;
            if (n1 < n2)
            {
                std::swap(ap1, ap2);
                std::swap(n1, n2);
                aInverted = true;
            }
            auto length = n1;
            auto n = n2;
            long long nLeft = 0, nRight = 0;
            switch (aMode)
            {
            case 0:
                length = length - n + 1;
                break;
            case 1:
                nLeft = n / 2;
                nRight = n - nLeft - 1;
                break;
            default:
                nRight = n - 1;
                nLeft = n - 1;
                length = length + n - 1;
                break;
            }
            auto const a1 = np("ascontiguousarray")(ap1, kw{ "dtype", dtype_value(aType) });
            auto const a2 = np("ascontiguousarray")(ap2, kw{ "dtype", dtype_value(aType) });
            auto* const r = new_array(aType, { length });
            auto const result = array_value(r);
            dispatch(aType, [&](auto tag)
            {
                using T = decltype(tag);
                using C = std::conditional_t<is_half_v<T>, float, T>;
                if constexpr (!is_bool_v<C>)
                {
                    auto const* p1 = reinterpret_cast<T const*>(a1.array()->pointer());
                    auto const* p2 = reinterpret_cast<T const*>(a2.array()->pointer());
                    auto* op = reinterpret_cast<T*>(r->pointer());
                    // (the type's dot function (blas.hpp); small_correlate's sums (in order) for a float kernel of at most 11 elements)
                    auto const dot = [&](T const* x, T const* y, long long aCount)
                    {
                        auto* const px = reinterpret_cast<unsigned char*>(const_cast<T*>(x));
                        auto* const py = reinterpret_cast<unsigned char*>(const_cast<T*>(y));
                        blas::dot(aType, px, sizeof(T), py, sizeof(T), reinterpret_cast<unsigned char*>(op++), aCount);
                    };
                    auto const small_dot = [&](T const* x, T const* y, long long aCount)
                    {
                        C sum{};
                        for (long long i = 0; i < aCount; ++i)
                            sum = static_cast<C>(sum + cast<C>(x[i]) * cast<C>(y[i]));
                        if constexpr (is_half_v<T>)
                            *op++ = half{ sum };
                        else
                            *op++ = sum;
                    };
                    bool const small = (std::is_same_v<T, float> || std::is_same_v<T, double>) && n <= 11;
                    auto const* ip1 = p1;
                    auto const* ip2 = p2 + nLeft;
                    auto count = n - nLeft;
                    for (long long i = 0; i < nLeft; ++i)
                    {
                        dot(ip1, ip2, count);
                        ++count;
                        --ip2;
                    }
                    for (long long i = 0; i < n1 - n2 + 1; ++i)
                    {
                        if (small)
                            small_dot(ip1, ip2, count);
                        else
                            dot(ip1, ip2, count);
                        ++ip1;
                    }
                    for (long long i = 0; i < nRight; ++i)
                    {
                        --count;
                        dot(ip1, ip2, count);
                        ++ip1;
                    }
                }
            });
            return result;
        }

        int correlate_mode(arguments const& aArgs, std::size_t aIndex, int aDefault)
        {
            if (!aArgs.has(aIndex))
                return aDefault;
            auto const m = arg(aArgs, aIndex);
            if (is_int(m))
                return static_cast<int>(m.to_int());
            auto const s = m.to_str();
            if (s == "valid" || s == "v")
                return 0;
            if (s == "same" || s == "s")
                return 1;
            if (s == "full" || s == "f")
                return 2;
            raise("ValueError", "mode must be one of 'valid', 'same', or 'full' (got '" + s + "')");
        }

        object* f_correlate(object* aArgs, object* aKwargs)
        {
            arguments args{ "correlate", aArgs, aKwargs, { "a", "v", "mode" } };
            if (!args.ok())
                return nullptr;
            auto const mode = correlate_mode(args, 2, 0);
            auto a = np("array")(arg(args, 0), kw{ "ndmin", value{ 1 } });
            auto v = np("array")(arg(args, 1), kw{ "ndmin", value{ 1 } });
            auto const t = promote(type_of(a), type_of(v));
            if (a.len() == 0 || v.len() == 0)
                raise("ValueError", a.len() == 0 ? "first array argument cannot be empty" : "second array argument cannot be empty");
            if (is_complex(t))
                v = np("conjugate")(v);
            bool inverted;
            auto r = pyarray_correlate(a, v, t, mode, inverted);
            if (inverted)
                r = np("ascontiguousarray")(r[slice({}, {}, -1)]);
            return r.release();
        }

        object* f_convolve(object* aArgs, object* aKwargs)
        {
            arguments args{ "convolve", aArgs, aKwargs, { "a", "v", "mode" } };
            if (!args.ok())
                return nullptr;
            auto a = np("array")(arg(args, 0), kw{ "ndmin", value{ 1 } });
            auto v = np("array")(arg(args, 1), kw{ "ndmin", value{ 1 } });
            if (v.len() > a.len())
                std::swap(a, v);
            if (a.len() == 0)
                raise("ValueError", "a cannot be empty");
            if (v.len() == 0)
                raise("ValueError", "v cannot be empty");
            auto const mode = correlate_mode(args, 2, 2);
            bool inverted;
            return pyarray_correlate(a, v[slice({}, {}, -1)], promote(type_of(a), type_of(v)), mode, inverted).release();
        }

        // --- histograms

        value unsigned_subtract(value const& a, value const& b)
        {
            auto const dt = np("result_type")(a, b);
            auto const t = *dtype_of_object(dt.get());
            std::optional<dtype> unsignedType;
            switch (t)
            {
            case dtype::Int8: unsignedType = dtype::UInt8; break;
            case dtype::Int16: unsignedType = dtype::UInt16; break;
            case dtype::Int32: unsignedType = dtype::UInt32; break;
            case dtype::Int64: unsignedType = dtype::UInt64; break;
            default: break;
            }
            if (!unsignedType)
                return np("subtract")(a, b, kw{ "dtype", dt });
            return np("subtract")(np("asarray")(a, kw{ "dtype", dt }), np("asarray")(b, kw{ "dtype", dt }), kw{ "casting", value{ "unsafe" } },
                kw{ "dtype", dtype_value(*unsignedType) });
        }

        std::pair<value, value> get_outer_edges(value const& a, value const& aRange)
        {
            value first, last;
            if (!aRange.empty() && !aRange.is_none())
            {
                auto const r = aRange.items();
                first = r.at(0);
                last = r.at(1);
                if ((first > last).truth())
                    raise("ValueError", "max must be larger than min in range parameter.");
                if (!(np("isfinite")(first).truth() && np("isfinite")(last).truth()))
                    raise("ValueError", "supplied range of [" + first.str() + ", " + last.str() + "] is not finite");
            }
            else if (a.size() == 0)
            {
                first = value{ 0 };
                last = value{ 1 };
            }
            else
            {
                first = a.method("min");
                last = a.method("max");
                if (!(np("isfinite")(first).truth() && np("isfinite")(last).truth()))
                    raise("ValueError", "autodetected range of [" + first.str() + ", " + last.str() + "] is not finite");
            }
            if ((first == last).truth())
            {
                first = first - 0.5;
                last = last + 0.5;
            }
            return { first, last };
        }

        value ptp(value const& x)
        {
            return unsigned_subtract(x.method("max"), x.method("min"));
        }

        value hist_bin_width(std::string const& aName, value const& x, value const& aRange);

        value hist_bin_sturges(value const& x) { return ptp(x) / (np("log2")(x.size()) + 1.0); }
        value hist_bin_sqrt(value const& x) { return ptp(x) / np("sqrt")(x.size()); }
        value hist_bin_fd(value const& x)
        {
            auto const p = np("percentile")(x, list({ value{ 75 }, value{ 25 } }));
            auto const iqr = np("subtract")(p[0], p[1]);
            return value{ 2.0 } * iqr * std::pow(static_cast<double>(x.size()), -1.0 / 3.0);
        }

        value hist_bin_width(std::string const& aName, value const& x, value const& aRange)
        {
            auto const n = static_cast<double>(x.size());
            if (aName == "sqrt")
                return hist_bin_sqrt(x);
            if (aName == "sturges")
                return hist_bin_sturges(x);
            if (aName == "rice")
                return ptp(x) / (2.0 * std::pow(n, 1.0 / 3));
            if (aName == "scott")
                return value{ std::pow(24.0 * std::pow(std::numbers::pi, 0.5) / n, 1.0 / 3.0) } * np("std")(x);
            if (aName == "fd")
                return hist_bin_fd(x);
            if (aName == "doane")
            {
                if (x.size() > 2)
                {
                    auto const sg1 = std::sqrt(6.0 * (n - 2) / ((n + 1.0) * (n + 3)));
                    auto const sigma = np("std")(x);
                    if ((sigma > 0.0).truth())
                    {
                        auto temp = x - np("mean")(x);
                        temp = np("true_divide")(temp, sigma);
                        temp = np("power")(temp, 3);
                        auto const g1 = np("mean")(temp);
                        return ptp(x) / (value{ 1.0 } + np("log2")(x.size()) + np("log2")(value{ 1.0 } + np("absolute")(g1) / sg1));
                    }
                }
                return value{ 0.0 };
            }
            if (aName == "stone")
            {
                auto const ptpX = ptp(x);
                if (x.size() <= 1 || (ptpX == 0).truth())
                    return value{ 0 };
                auto const upper = std::max<long long>(100, static_cast<long long>(std::sqrt(n)));
                long long best = 1;
                double bestValue = std::numeric_limits<double>::infinity();
                for (long long nbins = 1; nbins <= upper; ++nbins)
                {
                    auto const hh = ptpX / nbins;
                    auto const pk = np("histogram")(x, kw{ "bins", value{ nbins } }, kw{ "range", aRange }).items()[0] / n;
                    auto const j = ((value{ 2 } - (n + 1) * pk.method("dot", pk)) / hh).to_double();
                    if (j < bestValue)
                    {
                        bestValue = j;
                        best = nbins;
                    }
                }
                return ptpX / best;
            }
            // (auto)
            auto const fd = hist_bin_fd(x);
            auto const sturges = hist_bin_sturges(x);
            auto const sqrtBw = hist_bin_sqrt(x);
            auto const corrected = (fd > sqrtBw / 2).truth() ? fd : sqrtBw / 2;
            return (corrected < sturges).truth() ? corrected : sturges;
        }

        struct bin_edges
        {
            value edges;
            bool uniform = false;
            value first;
            value last;
            long long count = 0;
        };

        bin_edges get_bin_edges(value a, value const& aBins, value const& aRange, value const& aWeights)
        {
            std::optional<long long> nEqualBins;
            bin_edges result;
            value first, last;
            if (is_str(aBins))
            {
                auto const name = aBins.to_str();
                if (name != "auto" && name != "fd" && name != "doane" && name != "rice" && name != "scott" && name != "sqrt" && name != "stone" && name != "sturges")
                    raise("ValueError", "'" + name + "' is not a valid estimator for `bins`");
                if (!aWeights.empty() && !aWeights.is_none())
                    raise("TypeError", "Automated estimation of the number of bins is not supported for weighted data");
                std::tie(first, last) = get_outer_edges(a, aRange);
                if (!aRange.empty() && !aRange.is_none())
                {
                    auto const keep = (a >= first) & (a <= last);
                    if (!np("all")(keep).truth())
                        a = a[keep];
                }
                if (a.size() == 0)
                    nEqualBins = 1;
                else
                {
                    auto width = hist_bin_width(name, a, aRange);
                    if (width.truth())
                    {
                        if (is_integer(type_of(a)) && (width < 1).truth())
                            width = value{ 1 };
                        auto const delta = unsigned_subtract(last, first);
                        nEqualBins = np("ceil")(delta / width).to_int();
                    }
                    else
                        nEqualBins = 1;
                }
            }
            else if (np("ndim")(aBins).to_int() == 0)
            {
                if (!is_int(aBins) && !(is_array(aBins.get()) && is_integer(type_of(aBins))))
                    raise("TypeError", "`bins` must be an integer, a string, or an array");
                nEqualBins = aBins.to_int();
                if (*nEqualBins < 1)
                    raise("ValueError", "`bins` must be positive, when an integer");
                std::tie(first, last) = get_outer_edges(a, aRange);
            }
            else if (np("ndim")(aBins).to_int() == 1)
            {
                result.edges = asarray(aBins);
                if (np("any")(result.edges[slice({}, -1)] > result.edges[slice(1, {})]).truth())
                    raise("ValueError", "`bins` must increase monotonically, when an array");
            }
            else
                raise("ValueError", "`bins` must be 1d, when an array");
            if (nEqualBins)
            {
                auto binType = np("result_type")(first, last, a);
                if (is_integer(*dtype_of_object(binType.get())))
                    binType = np("result_type")(binType, np("float64"));
                result.edges = np("linspace")(first, last, *nEqualBins + 1, kw{ "endpoint", value{ true } }, kw{ "dtype", binType });
                if (np("any")(result.edges[slice({}, -1)] >= result.edges[slice(1, {})]).truth())
                    raise("ValueError", "Too many bins for data range. Cannot create " + std::to_string(*nEqualBins) + " finite-sized bins.");
                result.uniform = true;
                result.first = first;
                result.last = last;
                result.count = *nEqualBins;
            }
            return result;
        }

        std::pair<value, value> ravel_and_check_weights(value const& aA, value const& aWeights)
        {
            auto a = asarray(aA);
            if (is_bool(type_of(a)))
                a = a.method("astype", np("uint8"));
            value weights;
            if (!aWeights.empty() && !aWeights.is_none())
            {
                weights = asarray(aWeights);
                if (weights.array()->shape != a.array()->shape)
                    raise("ValueError", "weights should have the same shape as a.");
                weights = weights.method("ravel");
            }
            return { a.method("ravel"), weights };
        }

        value search_sorted_inclusive(value const& a, value const& v)
        {
            return np("concatenate")(tuple({ a.method("searchsorted", v[slice({}, -1)], "left"), a.method("searchsorted", v[slice(-1, {})], "right") }));
        }

        object* f_histogram(object* aArgs, object* aKwargs)
        {
            arguments args{ "histogram", aArgs, aKwargs, { "a", "bins", "range", "density", "weights" } };
            if (!args.ok())
                return nullptr;
            auto [a, weights] = ravel_and_check_weights(arg(args, 0), arg(args, 4));
            auto const edges = get_bin_edges(a, arg(args, 1, 10), arg(args, 2), weights);
            value const ntype = weights.empty() ? np("intp") : dtype_of(weights);
            value n;
            bool const simpleWeights = weights.empty() || can_cast(type_of(weights), dtype::Float64, "safe") || can_cast(type_of(weights), dtype::Complex128, "safe");
            long long const block = 65536;
            auto const length = a.len();
            if (edges.uniform && simpleWeights)
            {
                n = np("zeros")(edges.count, ntype);
                auto const normDenom = unsigned_subtract(edges.last, edges.first);
                for (long long i = 0; i < length; i += block)
                {
                    auto tmpA = a[slice(i, i + block)];
                    value tmpW;
                    if (!weights.empty())
                        tmpW = weights[slice(i, i + block)];
                    auto const keep = (tmpA >= edges.first) & (tmpA <= edges.last);
                    if (!np("logical_and").method("reduce", keep).truth())
                    {
                        tmpA = tmpA[keep];
                        if (!tmpW.empty())
                            tmpW = tmpW[keep];
                    }
                    tmpA = tmpA.method("astype", dtype_of(edges.edges), kw{ "copy", value{ false } });
                    auto const fIndices = unsigned_subtract(tmpA, edges.first) / normDenom * edges.count;
                    auto const indices = fIndices.method("astype", np("intp"));
                    indices.set(indices == edges.count, indices[indices == edges.count] - 1);
                    auto const decrement = tmpA < edges.edges[indices];
                    indices.set(decrement, indices[decrement] - 1);
                    auto const increment = (tmpA >= edges.edges[indices + 1]) & (indices != edges.count - 1);
                    indices.set(increment, indices[increment] + 1);
                    if (!tmpW.empty() && is_complex(type_of(tmpW)))
                    {
                        auto const re = np("bincount")(indices, kw{ "weights", tmpW.attr("real") }, kw{ "minlength", value{ edges.count } });
                        auto const im = np("bincount")(indices, kw{ "weights", tmpW.attr("imag") }, kw{ "minlength", value{ edges.count } });
                        n = n + re + im * value::steal(make_complex({ 0.0, 1.0 }));
                    }
                    else
                        n = n + np("bincount")(indices, kw{ "weights", tmpW.empty() ? py::none() : tmpW }, kw{ "minlength", value{ edges.count } }).method("astype", ntype);
                }
            }
            else
            {
                auto cumN = np("zeros")(edges.edges.shape(), ntype);
                if (weights.empty())
                {
                    for (long long i = 0; i < length; i += block)
                    {
                        auto const sa = np("sort")(a[slice(i, i + block)]);
                        cumN = cumN + search_sorted_inclusive(sa, edges.edges);
                    }
                }
                else
                {
                    auto const zero = np("zeros")(1, kw{ "dtype", ntype });
                    for (long long i = 0; i < length; i += block)
                    {
                        auto const tmpA = a[slice(i, i + block)];
                        auto const tmpW = weights[slice(i, i + block)];
                        auto const sortingIndex = np("argsort")(tmpA);
                        auto const sa = tmpA[sortingIndex];
                        auto const sw = tmpW[sortingIndex];
                        auto const cw = np("concatenate")(tuple({ zero, sw.method("cumsum") }));
                        auto const binIndex = search_sorted_inclusive(sa, edges.edges);
                        cumN = cumN + cw[binIndex];
                    }
                }
                n = np("diff")(cumN);
            }
            if (arg(args, 3, false).truth())
            {
                auto const db = np("array")(np("diff")(edges.edges), np("float64"));
                return tuple({ n / db / n.method("sum"), edges.edges }).release();
            }
            return tuple({ n, edges.edges }).release();
        }

        object* f_histogram_bin_edges(object* aArgs, object* aKwargs)
        {
            arguments args{ "histogram_bin_edges", aArgs, aKwargs, { "a", "bins", "range", "weights" } };
            if (!args.ok())
                return nullptr;
            auto [a, weights] = ravel_and_check_weights(arg(args, 0), arg(args, 3));
            return get_bin_edges(a, arg(args, 1, 10), arg(args, 2), weights).edges.release();
        }

        object* f_histogramdd(object* aArgs, object* aKwargs)
        {
            arguments args{ "histogramdd", aArgs, aKwargs, { "sample", "bins", "range", "density", "weights" } };
            if (!args.ok())
                return nullptr;
            auto sample = arg(args, 0);
            if (!isinstance_array(sample) || sample.ndim() != 2)
                sample = np("atleast_2d")(sample).attr("T");
            auto const D = sample.array()->shape[1];
            std::vector<long long> nbin(static_cast<std::size_t>(D));
            std::vector<value> edges(static_cast<std::size_t>(D)), dedges(static_cast<std::size_t>(D));
            value weights;
            if (args.has(4))
                weights = asarray(arg(args, 4));
            std::vector<value> bins;
            auto const binsArg = arg(args, 1, 10);
            if (np("ndim")(binsArg).to_int() == 0 && !is_sequence(binsArg))
                bins.assign(static_cast<std::size_t>(D), binsArg);
            else
            {
                bins = binsArg.items();
                if (static_cast<long long>(bins.size()) != D)
                    raise("ValueError", "The dimension of bins must be equal to the dimension of the sample x.");
            }
            std::vector<value> ranges;
            if (!args.has(2))
                ranges.assign(static_cast<std::size_t>(D), py::none());
            else
            {
                ranges = arg(args, 2).items();
                if (static_cast<long long>(ranges.size()) != D)
                    raise("ValueError", "range argument must have one entry per dimension");
            }
            for (long long i = 0; i < D; ++i)
            {
                auto const& b = bins[static_cast<std::size_t>(i)];
                if (np("ndim")(b).to_int() == 0)
                {
                    if ((b < 1).truth())
                        raise("ValueError", "`bins[" + std::to_string(i) + "]` must be positive, when an integer");
                    auto const [smin, smax] = get_outer_edges(sample[tuple({ all(), value{ i } })], ranges[static_cast<std::size_t>(i)]);
                    edges[static_cast<std::size_t>(i)] = np("linspace")(smin, smax, b.to_int() + 1);
                }
                else if (np("ndim")(b).to_int() == 1)
                {
                    edges[static_cast<std::size_t>(i)] = asarray(b);
                    auto const& e = edges[static_cast<std::size_t>(i)];
                    if (np("any")(e[slice({}, -1)] > e[slice(1, {})]).truth())
                        raise("ValueError", "`bins[" + std::to_string(i) + "]` must be monotonically increasing, when an array");
                }
                else
                    raise("ValueError", "`bins[" + std::to_string(i) + "]` must be a scalar or 1d array");
                nbin[static_cast<std::size_t>(i)] = edges[static_cast<std::size_t>(i)].len() + 1;
                dedges[static_cast<std::size_t>(i)] = np("diff")(edges[static_cast<std::size_t>(i)]);
            }
            std::vector<value> ncount;
            for (long long i = 0; i < D; ++i)
                ncount.push_back(np("searchsorted")(edges[static_cast<std::size_t>(i)], sample[tuple({ all(), value{ i } })], kw{ "side", value{ "right" } }));
            for (long long i = 0; i < D; ++i)
            {
                auto const onEdge = sample[tuple({ all(), value{ i } })] == edges[static_cast<std::size_t>(i)][-1];
                ncount[static_cast<std::size_t>(i)].set(onEdge, ncount[static_cast<std::size_t>(i)][onEdge] - 1);
            }
            auto const xy = np("ravel_multi_index")(tuple(ncount), int_tuple(nbin));
            long long product = 1;
            for (auto x : nbin)
                product *= x;
            auto hist = np("bincount")(xy, weights.empty() ? py::none() : weights, kw{ "minlength", value{ product } });
            hist = hist.method("reshape", int_tuple(nbin));
            hist = hist.method("astype", np("float64"));
            hist = hist[tuple(std::vector<value>(static_cast<std::size_t>(D), slice(1, -1)))];
            if (arg(args, 3, false).truth())
            {
                auto const s = hist.method("sum");
                for (long long i = 0; i < D; ++i)
                {
                    std::vector<long long> shape(static_cast<std::size_t>(D), 1);
                    shape[static_cast<std::size_t>(i)] = nbin[static_cast<std::size_t>(i)] - 2;
                    hist = hist / dedges[static_cast<std::size_t>(i)].method("reshape", int_tuple(shape));
                }
                hist = hist / s;
            }
            return tuple({ hist, list(edges) }).release();
        }

        object* f_histogram2d(object* aArgs, object* aKwargs)
        {
            arguments args{ "histogram2d", aArgs, aKwargs, { "x", "y", "bins", "range", "density", "weights" } };
            if (!args.ok())
                return nullptr;
            auto const x = arg(args, 0), y = arg(args, 1);
            if (x.len() != y.len())
                raise("ValueError", "x and y must have the same length.");
            auto bins = arg(args, 2, 10);
            long long N = 1;
            if (is_sequence(bins) || isinstance_array(bins))
                N = bins.len();
            if (N != 1 && N != 2)
            {
                auto const e = asarray(bins);
                bins = list({ e, e });
            }
            auto const r = np("histogramdd")(list({ x, y }), bins, arg(args, 3, py::none()), arg(args, 4, py::none()), arg(args, 5, py::none())).items();
            auto const edges = r[1].items();
            return tuple({ r[0], edges[0], edges[1] }).release();
        }

        object* f_digitize(object* aArgs, object* aKwargs)
        {
            arguments args{ "digitize", aArgs, aKwargs, { "x", "bins", "right" } };
            if (!args.ok())
                return nullptr;
            auto const x = asarray(arg(args, 0));
            auto const bins = asarray(arg(args, 1));
            if (is_complex(type_of(x)))
                raise("TypeError", "x may not be complex");
            if (bins.ndim() != 1)
                raise("ValueError", "object too deep for desired array");
            // (_monotonicity)
            int mono = 1;
            auto const n = bins.len();
            if (n > 1)
            {
                auto const* b = bins.array();
                bool increasing = true, decreasing = true;
                for (long long i = 1; i < n; ++i)
                {
                    auto const p = read_as<double>(b->pointer() + (i - 1) * b->strides[0], b->type);
                    auto const q = read_as<double>(b->pointer() + i * b->strides[0], b->type);
                    if (q < p) increasing = false;
                    if (q > p) decreasing = false;
                }
                auto const first = read_as<double>(b->pointer(), b->type);
                auto const last = read_as<double>(b->pointer() + (n - 1) * b->strides[0], b->type);
                mono = increasing && (!decreasing || last >= first) ? 1 : decreasing ? -1 : 0;
            }
            if (mono == 0)
                raise("ValueError", "bins must be monotonically increasing or decreasing");
            value const side{ arg(args, 2, false).truth() ? "left" : "right" };
            if (mono == -1)
                return (value{ n } - np("searchsorted")(bins[slice({}, {}, -1)], x, kw{ "side", side })).release();
            return np("searchsorted")(bins, x, kw{ "side", side }).release();
        }

        // --- elementwise functions with two results, and others

        object* f_frexp(object* aArgs, object* aKwargs)
        {
            arguments args{ "frexp", aArgs, aKwargs, { "x" } };
            if (!args.ok())
                return nullptr;
            auto const x = asarray(arg(args, 0));
            auto t = type_of(x);
            if (is_complex(t))
                raise("TypeError", "ufunc 'frexp' not supported for the input types, and the inputs could not be safely coerced to any supported types according to the casting rule ''safe''");
            if (!is_float(t))
                t = smallest_float_for(t);
            auto* const m = new_array(t, x.array()->shape);
            auto* const e = new_array(dtype::Int32, x.array()->shape);
            auto const mv = array_value(m), ev = array_value(e);
            auto const* xa = x.array();
            std::int64_t i = 0;
            for_each(*xa, [&](unsigned char* p)
            {
                int exponent = 0;
                auto const v = read_as<double>(p, xa->type);
                double const mantissa = std::isfinite(v) ? std::frexp(v, &exponent) : v;
                write_as(m->pointer() + i * static_cast<std::int64_t>(m->itemsize()), t, mantissa);
                reinterpret_cast<std::int32_t*>(e->pointer())[i] = exponent;
                ++i;
            });
            return tuple({ value::steal(result_object((ctx->retain(mv.get()), m))), value::steal(result_object((ctx->retain(ev.get()), e))) }).release();
        }

        object* f_ldexp(object* aArgs, object* aKwargs)
        {
            arguments args{ "ldexp", aArgs, aKwargs, { "x1", "x2" } };
            if (!args.ok())
                return nullptr;
            auto const x1 = asarray(arg(args, 0));
            auto const x2 = asarray(arg(args, 1));
            if (!is_integer(type_of(x2)) && !is_bool(type_of(x2)))
                raise("TypeError", std::string{ "ufunc 'ldexp' not supported for the input types, and the inputs could not be safely coerced to any supported types according to the casting rule ''safe''" });
            auto t = type_of(x1);
            if (!is_float(t))
                t = is_complex(t) ? t : smallest_float_for(t);
            if (is_complex(t))
                raise("TypeError", "ufunc 'ldexp' not supported for the input types, and the inputs could not be safely coerced to any supported types according to the casting rule ''safe''");
            if (is_python_scalar(args[0]) && kind(args[0]) == NEOS_PY_INT)
                t = dtype::Float64;
            auto const b = np("broadcast_arrays")(x1, x2).items();
            auto* const r = new_array(t, b[0].array()->shape);
            auto const rv = array_value(r);
            auto const* a = b[0].array();
            auto const* e = b[1].array();
            iterate(r->shape, { a->pointer(), e->pointer(), r->pointer() }, { a->strides, e->strides, r->strides },
                [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
                {
                    auto pa = aPointers[0], pe = aPointers[1], pr = aPointers[2];
                    for (std::int64_t i = 0; i < aCount; ++i, pa += aStrides[0], pe += aStrides[1], pr += aStrides[2])
                    {
                        auto const exponent = std::clamp<std::int64_t>(read_as<std::int64_t>(pe, e->type), std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
                        if (t == dtype::Float32)
                            write_as(pr, t, std::ldexp(read_as<float>(pa, a->type), static_cast<int>(exponent)));
                        else if (t == dtype::Float16)
                            write_as(pr, t, half{ std::ldexp(read_as<float>(pa, a->type), static_cast<int>(exponent)) });
                        else
                            write_as(pr, t, std::ldexp(read_as<double>(pa, a->type), static_cast<int>(exponent)));
                    }
                });
            return result_object((ctx->retain(rv.get()), r));
        }

        object* f_modf(object* aArgs, object* aKwargs)
        {
            arguments args{ "modf", aArgs, aKwargs, { "x" } };
            if (!args.ok())
                return nullptr;
            auto const x = asarray(arg(args, 0));
            auto t = type_of(x);
            if (is_complex(t))
                raise("TypeError", "ufunc 'modf' not supported for the input types, and the inputs could not be safely coerced to any supported types according to the casting rule ''safe''");
            if (!is_float(t))
                t = smallest_float_for(t);
            auto* const f = new_array(t, x.array()->shape);
            auto* const n = new_array(t, x.array()->shape);
            auto const fv = array_value(f), nv = array_value(n);
            auto const* xa = x.array();
            std::int64_t i = 0;
            for_each(*xa, [&](unsigned char* p)
            {
                auto const v = read_as<double>(p, xa->type);
                double integral = 0.0;
                double const fraction = std::modf(v, &integral);
                write_as(f->pointer() + i * static_cast<std::int64_t>(f->itemsize()), t, fraction);
                write_as(n->pointer() + i * static_cast<std::int64_t>(n->itemsize()), t, integral);
                ++i;
            });
            return tuple({ value::steal(result_object((ctx->retain(fv.get()), f))), value::steal(result_object((ctx->retain(nv.get()), n))) }).release();
        }

        object* f_divmod(object* aArgs, object* aKwargs)
        {
            arguments args{ "divmod", aArgs, aKwargs, { "x1", "x2" } };
            if (!args.ok())
                return nullptr;
            auto const q = np("floor_divide")(arg(args, 0), arg(args, 1));
            auto const r = np("remainder")(arg(args, 0), arg(args, 1));
            return tuple({ q, r }).release();
        }

        object* f_iscomplex(object* aArgs, object* aKwargs)
        {
            arguments args{ "iscomplex", aArgs, aKwargs, { "x" } };
            if (!args.ok())
                return nullptr;
            auto const ax = np("asanyarray")(arg(args, 0));
            if (is_complex(type_of(ax)))
                return (ax.attr("imag") != 0).release();
            auto const res = np("zeros")(ax.shape(), np("bool"));
            return res[tuple({})].release();
        }

        object* f_isreal(object* aArgs, object* aKwargs)
        {
            arguments args{ "isreal", aArgs, aKwargs, { "x" } };
            if (!args.ok())
                return nullptr;
            return (np("imag")(arg(args, 0)) == 0).release();
        }

        object* f_real_if_close(object* aArgs, object* aKwargs)
        {
            arguments args{ "real_if_close", aArgs, aKwargs, { "a", "tol" } };
            if (!args.ok())
                return nullptr;
            auto a = np("asanyarray")(arg(args, 0));
            auto const t = type_of(a);
            if (!is_complex(t))
                return a.release();
            auto tol = arg(args, 1, 100).to_double();
            if (tol > 1)
                tol = tol * (t == dtype::Complex64 ? static_cast<double>(std::numeric_limits<float>::epsilon()) : std::numeric_limits<double>::epsilon());
            if (np("all")(np("absolute")(a.attr("imag")) < tol).truth())
                a = a.attr("real");
            return a.release();
        }

        object* f_isposinf(object* aArgs, object* aKwargs)
        {
            arguments args{ "isposinf", aArgs, aKwargs, { "x", "out" } };
            if (!args.ok())
                return nullptr;
            auto const x = np("asanyarray")(arg(args, 0));
            if (is_complex(type_of(x)))
                raise("TypeError", "This operation is not supported for complex values because it would be ambiguous.");
            return (np("isinf")(x) & ~np("signbit")(x)).release();
        }

        object* f_isneginf(object* aArgs, object* aKwargs)
        {
            arguments args{ "isneginf", aArgs, aKwargs, { "x", "out" } };
            if (!args.ok())
                return nullptr;
            auto const x = np("asanyarray")(arg(args, 0));
            if (is_complex(type_of(x)))
                raise("TypeError", "This operation is not supported for complex values because it would be ambiguous.");
            return (np("isinf")(x) & np("signbit")(x)).release();
        }

        // --- nan functions

        object* nan_arg(char const* aName, object* aArgs, object* aKwargs, bool aMax)
        {
            arguments args{ aName, aArgs, aKwargs, { "a", "axis", "out", "*keepdims" } };
            if (!args.ok())
                return nullptr;
            auto a = np("asanyarray")(arg(args, 0));
            if (is_inexact(type_of(a)))
            {
                auto const mask = np("isnan")(a);
                a = np("where")(mask, aMax ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity(), a).method("astype", dtype_of(a));
                if (mask.size() > 0 && np("any")(np("all")(mask, kw{ "axis", arg(args, 1, py::none()) })).truth())
                    raise("ValueError", "All-NaN slice encountered");
            }
            return np(aMax ? "argmax" : "argmin")(a, kw{ "axis", arg(args, 1, py::none()) }, kw{ "keepdims", arg(args, 3, false) }).release();
        }

        object* f_nanargmax(object* aArgs, object* aKwargs) { return nan_arg("nanargmax", aArgs, aKwargs, true); }
        object* f_nanargmin(object* aArgs, object* aKwargs) { return nan_arg("nanargmin", aArgs, aKwargs, false); }

        // (a function of the non-NaN elements along an axis)
        value nan_lanes(value const& a, value const& aAxis, std::function<value(value const&)> const& aFunction, bool aKeepdims)
        {
            if (!is_inexact(type_of(a)) || !np("any")(np("isnan")(a)).truth())
                return {};
            value x = a;
            long long axis;
            if (aAxis.empty() || aAxis.is_none())
            {
                auto const flat = x.method("ravel");
                auto const r = aFunction(flat[~np("isnan")(flat)]);
                if (aKeepdims)
                    return np("asarray")(r).method("reshape", int_tuple(std::vector<long long>(static_cast<std::size_t>(a.ndim()), 1)));
                return r;
            }
            axis = normalize_axis_index(aAxis.to_int(), a.ndim());
            x = np("moveaxis")(x, axis, -1);
            auto const outer = np("asarray")(x[tuple({ ellipsis(), 0 })]).shape();
            auto const lanes = x.method("reshape", -1, x.array()->shape.back());
            std::vector<value> results;
            for (auto const& lane : lanes.items())
                results.push_back(aFunction(lane[~np("isnan")(lane)]));
            auto r = np("array")(list(results));
            if (r.ndim() > 1)
            {
                // (each result an array (e.g. several quantiles): the quantiles first)
                r = np("moveaxis")(r, 0, -1);
                std::vector<long long> shape;
                for (std::size_t i = 0u; i + 1u < r.array()->shape.size(); ++i)
                    shape.push_back(r.array()->shape[i]);
                for (auto const& d : outer.items())
                    shape.push_back(d.to_int());
                r = r.method("reshape", int_tuple(shape));
            }
            else
                r = r.method("reshape", outer);
            if (aKeepdims)
                r = np("expand_dims")(r, axis);
            return r;
        }

        object* f_nanmedian(object* aArgs, object* aKwargs)
        {
            arguments args{ "nanmedian", aArgs, aKwargs, { "a", "axis", "out", "overwrite_input", "keepdims" } };
            if (!args.ok())
                return nullptr;
            auto const a = np("asanyarray")(arg(args, 0));
            bool const keepdims = arg(args, 4, false).truth();
            auto const r = nan_lanes(a, arg(args, 1), [&](value const& aLane)
            {
                if (aLane.size() == 0)
                    return np(info(type_of(a)).scalarName)(std::numeric_limits<double>::quiet_NaN());
                return np("median")(aLane);
            }, keepdims);
            if (!r.empty())
                return r.release();
            return np("median")(a, kw{ "axis", arg(args, 1, py::none()) }, kw{ "keepdims", value{ keepdims } }).release();
        }

        object* nan_quantile(char const* aName, object* aArgs, object* aKwargs, bool aPercent)
        {
            arguments args{ aName, aArgs, aKwargs, { "a", "q", "axis", "out", "overwrite_input", "method", "keepdims", "*weights", "*interpolation" } };
            if (!args.ok())
                return nullptr;
            auto const a = np("asanyarray")(arg(args, 0));
            auto const q = arg(args, 1);
            bool const keepdims = arg(args, 6, false).truth();
            char const* const function = aPercent ? "percentile" : "quantile";
            auto const r = nan_lanes(a, arg(args, 2), [&](value const& aLane)
            {
                if (aLane.size() == 0)
                    return np("full")(np("shape")(q), std::numeric_limits<double>::quiet_NaN())[tuple({})];
                return np(function)(aLane, q);
            }, keepdims);
            if (!r.empty())
                return r.release();
            return np(function)(a, q, kw{ "axis", arg(args, 2, py::none()) }, kw{ "keepdims", value{ keepdims } }).release();
        }

        object* f_nanpercentile(object* aArgs, object* aKwargs) { return nan_quantile("nanpercentile", aArgs, aKwargs, true); }
        object* f_nanquantile(object* aArgs, object* aKwargs) { return nan_quantile("nanquantile", aArgs, aKwargs, false); }
    }

    void register_lib2()
    {
        register_functions({
            { "average", f_average }, { "cov", f_cov }, { "corrcoef", f_corrcoef }, { "trapezoid", f_trapezoid }, { "gradient", f_gradient },
            { "sinc", f_sinc }, { "unwrap", f_unwrap }, { "vander", f_vander }, { "i0", f_i0 }, { "bartlett", f_bartlett }, { "blackman", f_blackman },
            { "hamming", f_hamming }, { "hanning", f_hanning }, { "kaiser", f_kaiser }, { "correlate", f_correlate }, { "convolve", f_convolve },
            { "histogram", f_histogram }, { "histogram_bin_edges", f_histogram_bin_edges }, { "histogramdd", f_histogramdd },
            { "histogram2d", f_histogram2d }, { "digitize", f_digitize }, { "frexp", f_frexp }, { "ldexp", f_ldexp }, { "modf", f_modf },
            { "divmod", f_divmod }, { "iscomplex", f_iscomplex }, { "isreal", f_isreal }, { "real_if_close", f_real_if_close },
            { "isposinf", f_isposinf }, { "isneginf", f_isneginf }, { "nanargmax", f_nanargmax }, { "nanargmin", f_nanargmin },
            { "nanmedian", f_nanmedian }, { "nanpercentile", f_nanpercentile }, { "nanquantile", f_nanquantile }
        });
    }
}
