/*
  statistics.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/statistics.py and Modules/_statisticsmodule.c), Copyright (c)
  2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License
  Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from Python and C to C++ for the neos Python runtime
  plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// The module statistics (CPython 3.13's statistics.py, and _statistics's _normal_dist_inv_cdf): its averages, measures of spread and relations
// (computed exactly with fractions, as CPython's are), kde, kde_random and NormalDist

#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>
#include "values.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            double const sPi = 3.141592653589793;
            double const sTau = 6.283185307179586;
            double const sSqrt2 = std::sqrt(2.0);

            py statistics_error_class() { return py{ module_exception("statistics.StatisticsError", { "ValueError" }) }; }

            [[noreturn]] void statistics_error(std::string const& aMessage) { py_raise(statistics_error_class()(py{ aMessage })); }

            py fraction_class() { return import_attribute("fractions", "Fraction"); }
            py decimal_class() { return import_attribute("decimal", "Decimal"); }
            py math(char const* aName) { return import_attribute("math", aName); }

            std::string name_of(py const& aType) { return aType.attr("__name__").str(); }

            // (a source of values (e.g. a generator's): false when it is exhausted)
            using source = std::function<bool(py&)>;

            source iterate(py const& aIterable)
            {
                py const iterator{ iter(aIterable.get()) };
                return [iterator](py& aResult) -> bool
                {
                    ref value{ next(iterator.get()) };
                    if (!value)
                    {
                        if (failed())
                            throw raised{};
                        return false;
                    }
                    aResult = py{ std::move(value) };
                    return true;
                };
            }

            // --- private utilities

            // (_exact_ratio: (numerator, denominator); (x, None) for a NaN or an infinity)
            std::pair<py, py> exact_ratio(py const& x)
            {
                try
                {
                    auto const ratio = x.method("as_integer_ratio");
                    return { ratio[py{ 0 }], ratio[py{ 1 }] };
                }
                catch (raised const&)
                {
                    py const e{ take_exception() };
                    if (exception_is(e.get(), "OverflowError") || exception_is(e.get(), "ValueError"))
                        return { x, py::none() };
                    if (!exception_is(e.get(), "AttributeError"))
                        py_raise(e);
                }
                try
                {
                    return { x.attr("numerator"), x.attr("denominator") };
                }
                catch (raised const&)
                {
                    if (!caught({ "AttributeError" }))
                        throw;
                    py_raise("TypeError", "can't convert type '" + name_of(type_of(x)) + "' to numerator/denominator");
                }
            }

            // (_coerce)
            py coerce(py const& T, py const& S)
            {
                if (T.is(S))
                    return T;
                auto const intType = builtin("int");
                if (S.is(intType) || S.is(builtin("bool")))
                    return T;
                if (T.is(intType))
                    return S;
                if (issubclass(S, T))
                    return S;
                if (issubclass(T, S))
                    return T;
                if (issubclass(T, intType))
                    return S;
                if (issubclass(S, intType))
                    return T;
                auto const fraction = fraction_class();
                auto const floatType = builtin("float");
                if (issubclass(T, fraction) && issubclass(S, floatType))
                    return S;
                if (issubclass(T, floatType) && issubclass(S, fraction))
                    return T;
                py_raise("TypeError", "don't know how to coerce " + name_of(T) + " and " + name_of(S));
            }

            py coerced_type(py const& aTypes)
            {
                auto T = builtin("int");
                for (auto const& t : aTypes.items())
                    T = coerce(T, t);
                return T;
            }

            struct sum_result { py T; py total; long long count = 0; };

            // (_sum)
            sum_result sum_of(source const& aData)
            {
                long long count = 0;
                auto const types = builtin("set")();
                auto const partials = py_dict();
                py x;
                while (aData(x))
                {
                    types.method("add", { type_of(x) });
                    auto const [n, d] = exact_ratio(x);
                    ++count;
                    partials.set_item(d, partials.method("get", { d, py{ 0 } }) + n);
                }
                py total;
                if (partials.contains(py::none()))
                    total = partials[py::none()];
                else
                {
                    total = py{ 0 };
                    auto const fraction = fraction_class();
                    for (auto const& item : partials.method("items").items())
                        total = total + fraction(item[py{ 1 }], item[py{ 0 }]);
                }
                return { coerced_type(types), total, count };
            }

            struct ss_result { py T; py ssd; py c; long long count = 0; };

            // (_ss)
            ss_result ss_of(py const& aData, py const& c)
            {
                if (!c.empty() && !c.is_none())
                {
                    auto const values = iterate(aData);
                    auto const r = sum_of([&](py& aResult) -> bool
                    {
                        py x;
                        if (!values(x))
                            return false;
                        auto const d = x - c;
                        aResult = d * d;
                        return true;
                    });
                    return { r.T, r.total, c, r.count };
                }
                long long count = 0;
                auto const types = builtin("set")();
                auto const sxPartials = py_dict();
                auto const sxxPartials = py_dict();
                auto const values = iterate(aData);
                py x;
                while (values(x))
                {
                    types.method("add", { type_of(x) });
                    auto const [n, d] = exact_ratio(x);
                    ++count;
                    sxPartials.set_item(d, sxPartials.method("get", { d, py{ 0 } }) + n);
                    sxxPartials.set_item(d, sxxPartials.method("get", { d, py{ 0 } }) + n * n);
                }
                py ssd, mean;
                if (count == 0)
                    ssd = mean = fraction_class()(py{ 0 });
                else if (sxPartials.contains(py::none()))
                    ssd = mean = sxPartials[py::none()];
                else
                {
                    auto const fraction = fraction_class();
                    py sx{ 0 }, sxx{ 0 };
                    for (auto const& item : sxPartials.method("items").items())
                        sx = sx + fraction(item[py{ 1 }], item[py{ 0 }]);
                    for (auto const& item : sxxPartials.method("items").items())
                    {
                        auto const d = item[py{ 0 }];
                        sxx = sxx + fraction(item[py{ 1 }], d * d);
                    }
                    ssd = (py{ count } * sxx - sx * sx) / py{ count };
                    mean = sx / py{ count };
                }
                return { coerced_type(types), ssd, mean, count };
            }

            // (_isfinite)
            bool is_finite(py const& x)
            {
                try
                {
                    return x.method("is_finite").truthy();
                }
                catch (raised const&)
                {
                    if (!caught({ "AttributeError" }))
                        throw;
                }
                return math("isfinite")(x).truthy();
            }

            // (_convert)
            py convert(py const& aValue, py T)
            {
                if (type_of(aValue).is(T))
                    return aValue;
                if (issubclass(T, builtin("int")) && aValue.attr("denominator") != py{ 1 })
                    T = builtin("float");
                try
                {
                    return T(aValue);
                }
                catch (raised const&)
                {
                    py const e{ take_exception() };
                    if (exception_is(e.get(), "TypeError") && issubclass(T, decimal_class()))
                        return T(aValue.attr("numerator")) / T(aValue.attr("denominator"));
                    py_raise(e);
                }
            }

            // (_fail_neg)
            source fail_negative(source aValues, std::string aMessage)
            {
                return [aValues, aMessage](py& aResult) -> bool
                {
                    if (!aValues(aResult))
                        return false;
                    if (aResult < py{ 0 })
                        statistics_error(aMessage);
                    return true;
                };
            }

            // (_rank)
            py rank(py const& aData, py const& aStart, bool aReverse = false)
            {
                auto const values = aData.items();
                std::vector<py> pairs;
                for (std::size_t i = 0u; i < values.size(); ++i)
                    pairs.push_back(py_tuple({ values[i], py{ i } }));
                auto const valPos = builtin("sorted").call({ py_list(pairs) }, { { "reverse", py::boolean(aReverse) } }).items();
                auto i = aStart - py{ 1 };
                std::vector<py> result(valPos.size(), py{ 0 });
                for (std::size_t g = 0u; g < valPos.size();)
                {
                    auto const key = valPos[g][py{ 0 }];
                    auto e = g + 1u;
                    while (e < valPos.size() && valPos[e][py{ 0 }] == key)
                        ++e;
                    auto const size = static_cast<long long>(e - g);
                    auto const r = i + py{ static_cast<double>(size + 1) / 2.0 };
                    for (auto k = g; k < e; ++k)
                        result[static_cast<std::size_t>(valPos[k][py{ 1 }].as_index())] = r;
                    i = i + py{ size };
                    g = e;
                }
                return py_list(result);
            }

            // (_integer_sqrt_of_frac_rto: the square root of n/m, rounded to the nearest integer using round-to-odd)
            py integer_sqrt_of_frac_rto(py const& n, py const& m)
            {
                auto const a = math("isqrt")(floordiv(n, m));
                return a | py{ a * a * m != n ? 1 : 0 };
            }

            // (_float_sqrt_of_frac)
            py float_sqrt_of_frac(py const& n, py const& m)
            {
                long long const sqrtBitWidth = 2 * 53 + 3;
                auto const q = (n.method("bit_length").as_index() - m.method("bit_length").as_index() - sqrtBitWidth);
                auto const half = q >= 0 ? q / 2 : -((-q + 1) / 2);   // (floor)
                py numerator, denominator;
                if (half >= 0)
                {
                    numerator = integer_sqrt_of_frac_rto(n, m << py{ 2 * half }) << py{ half };
                    denominator = py{ 1 };
                }
                else
                {
                    numerator = integer_sqrt_of_frac_rto(n << py{ -2 * half }, m);
                    denominator = py{ 1 } << py{ -half };
                }
                return numerator / denominator;
            }

            // (_decimal_sqrt_of_frac)
            py decimal_sqrt_of_frac(py n, py m)
            {
                auto const decimal = decimal_class();
                if (n <= py{ 0 })
                {
                    if (!n.truthy())
                        return decimal(py{ "0.0" });
                    n = -n;
                    m = -m;
                }
                auto const root = (decimal(n) / decimal(m)).method("sqrt");
                auto const rootRatio = root.method("as_integer_ratio");
                auto const nr = rootRatio[py{ 0 }], dr = rootRatio[py{ 1 }];
                auto const plus = root.method("next_plus");
                auto const plusRatio = plus.method("as_integer_ratio");
                auto const np = plusRatio[py{ 0 }], dp = plusRatio[py{ 1 }];
                if (py{ 4 } * n * power(dr * dp, py{ 2 }) > m * power(dr * np + dp * nr, py{ 2 }))
                    return plus;
                auto const minus = root.method("next_minus");
                auto const minusRatio = minus.method("as_integer_ratio");
                auto const nm = minusRatio[py{ 0 }], dm = minusRatio[py{ 1 }];
                if (py{ 4 } * n * power(dr * dm, py{ 2 }) < m * power(dr * nm + dm * nr, py{ 2 }))
                    return minus;
                return root;
            }

            // (the square root of a mean square (stdev and pstdev))
            py root_of(py const& T, py const& mss)
            {
                py numerator, denominator;
                try
                {
                    numerator = mss.attr("numerator");
                    denominator = mss.attr("denominator");
                }
                catch (raised const&)
                {
                    if (!caught({ "AttributeError" }))
                        throw;
                    py_raise("ValueError", "inf or nan encountered in data");
                }
                if (issubclass(T, decimal_class()))
                    return decimal_sqrt_of_frac(numerator, denominator);
                return float_sqrt_of_frac(numerator, denominator);
            }

            // (_mean_stdev)
            std::pair<py, py> mean_stdev(py const& aData)
            {
                auto const r = ss_of(aData, py{});
                if (r.count < 2)
                    statistics_error("stdev requires at least two data points");
                auto const mss = r.ssd / py{ r.count - 1 };
                auto const floatType = builtin("float");
                try
                {
                    auto const mean = floatType(r.c);
                    return { mean, float_sqrt_of_frac(mss.attr("numerator"), mss.attr("denominator")) };
                }
                catch (raised const&)
                {
                    if (!caught({ "AttributeError" }))
                        throw;
                    return { floatType(r.c), floatType(r.c) / floatType(r.ssd) };
                }
            }

            // (_sqrtprod: sqrt(x * y) computed with improved accuracy and without overflow/underflow)
            py sqrtprod(py const& x, py const& y)
            {
                auto const h = math("sqrt")(x * y);
                auto const isinf = math("isinf");
                if (!math("isfinite")(h).truthy())
                {
                    if (isinf(h).truthy() && !isinf(x).truthy() && !isinf(y).truthy())
                    {
                        py const scale{ std::ldexp(1.0, -512) };
                        return sqrtprod(scale * x, scale * y) / scale;
                    }
                    return h;
                }
                if (!h.truthy())
                {
                    if (x.truthy() && y.truthy())
                    {
                        py const scale{ std::ldexp(1.0, 537) };
                        return sqrtprod(scale * x, scale * y) / scale;
                    }
                    return h;
                }
                auto const d = math("sumprod")(py_tuple({ x, h }), py_tuple({ y, -h }));
                return h + d / (py{ 2.0 } * h);
            }

            // (_normal_dist_inv_cdf (Wichura's AS241))
            double normal_dist_inv_cdf(double p, double mu, double sigma)
            {
                double const q = p - 0.5;
                double num = 0.0, den = 0.0, r = 0.0, x = 0.0;
                if (std::fabs(q) <= 0.425)
                {
                    r = 0.180625 - q * q;
                    num = (((((((2.5090809287301226727e+3 * r + 3.3430575583588128105e+4) * r + 6.7265770927008700853e+4) * r + 4.5921953931549871457e+4) * r +
                        1.3731693765509461125e+4) * r + 1.9715909503065514427e+3) * r + 1.3314166789178437745e+2) * r + 3.3871328727963666080e+0) * q;
                    den = (((((((5.2264952788528545610e+3 * r + 2.8729085735721942674e+4) * r + 3.9307895800092710610e+4) * r + 2.1213794301586595867e+4) * r +
                        5.3941960214247511077e+3) * r + 6.8718700749205790830e+2) * r + 4.2313330701600911252e+1) * r + 1.0);
                    x = num / den;
                    return mu + (x * sigma);
                }
                r = q <= 0.0 ? p : 1.0 - p;
                r = std::sqrt(-std::log(r));
                if (r <= 5.0)
                {
                    r = r - 1.6;
                    num = (((((((7.74545014278341407640e-4 * r + 2.27238449892691845833e-2) * r + 2.41780725177450611770e-1) * r + 1.27045825245236838258e+0) * r +
                        3.64784832476320460504e+0) * r + 5.76949722146069140550e+0) * r + 4.63033784615654529590e+0) * r + 1.42343711074968357734e+0);
                    den = (((((((1.05075007164441684324e-9 * r + 5.47593808499534494600e-4) * r + 1.51986665636164571966e-2) * r + 1.48103976427480074590e-1) * r +
                        6.89767334985100004550e-1) * r + 1.67638483018380384940e+0) * r + 2.05319162663775882187e+0) * r + 1.0);
                }
                else
                {
                    r = r - 5.0;
                    num = (((((((2.01033439929228813265e-7 * r + 2.71155556874348757815e-5) * r + 1.24266094738807843860e-3) * r + 2.65321895265761230930e-2) * r +
                        2.96560571828504891230e-1) * r + 1.78482653991729133580e+0) * r + 5.46378491116411436990e+0) * r + 6.65790464350110377720e+0);
                    den = (((((((2.04426310338993978564e-15 * r + 1.42151175831644588870e-7) * r + 1.84631831751005468180e-5) * r + 7.86869131145613259100e-4) * r +
                        1.48753612908506148525e-2) * r + 1.36929880922735805310e-1) * r + 5.99832206555887937690e-1) * r + 1.0);
                }
                x = num / den;
                if (q < 0.0)
                    x = -x;
                return mu + (x * sigma);
            }

            // --- functions that are closures (kde's, kde_random's)

            py make_closure(std::string aName, std::string aQualname, std::string aDoc, native_body aBody)
            {
                return make_native_function(std::move(aName), std::move(aQualname), std::move(aDoc), "statistics", std::move(aBody));
            }

            void positional(char const* aFunction, std::vector<py> const& aArgs, object* aKwargs, std::size_t aCount)
            {
                py_checked(no_keywords(aKwargs, aFunction));
                if (aArgs.size() != aCount)
                    py_raise("TypeError", std::string{ aFunction } + "() takes " + std::to_string(aCount) + " positional argument" + (aCount == 1u ? "" : "s") +
                        " but " + std::to_string(aArgs.size()) + (aArgs.size() == 1u ? " was" : " were") + " given");
            }

            // (the kernels: K, W and the inverse of W)
            enum class kernel_kind { Normal, Logistic, Sigmoid, Rectangular, Triangular, Parabolic, Quartic, Triweight, Cosine };

            struct kernel_name { char const* name; kernel_kind kind; };
            kernel_name const sKernels[] =
            {
                { "normal", kernel_kind::Normal }, { "gauss", kernel_kind::Normal }, { "logistic", kernel_kind::Logistic }, { "sigmoid", kernel_kind::Sigmoid },
                { "rectangular", kernel_kind::Rectangular }, { "uniform", kernel_kind::Rectangular }, { "triangular", kernel_kind::Triangular },
                { "parabolic", kernel_kind::Parabolic }, { "epanechnikov", kernel_kind::Parabolic }, { "quartic", kernel_kind::Quartic },
                { "biweight", kernel_kind::Quartic }, { "triweight", kernel_kind::Triweight }, { "cosine", kernel_kind::Cosine }
            };

            double kernel_K(kernel_kind aKind, double t)
            {
                switch (aKind)
                {
                case kernel_kind::Normal: return std::exp(-1.0 / 2.0 * t * t) / std::sqrt(2.0 * sPi);
                case kernel_kind::Logistic: return 1.0 / 2.0 / (1.0 + std::cosh(t));
                case kernel_kind::Sigmoid: return (1.0 / sPi) / std::cosh(t);
                case kernel_kind::Rectangular: return 1.0 / 2.0;
                case kernel_kind::Triangular: return 1.0 - std::fabs(t);
                case kernel_kind::Parabolic: return 3.0 / 4.0 * (1.0 - t * t);
                case kernel_kind::Quartic: return 15.0 / 16.0 * std::pow(1.0 - t * t, 2.0);
                case kernel_kind::Triweight: return 35.0 / 32.0 * std::pow(1.0 - t * t, 3.0);
                default: return (sPi / 4.0) * std::cos((sPi / 2.0) * t);
                }
            }

            double kernel_W(kernel_kind aKind, double t)
            {
                switch (aKind)
                {
                case kernel_kind::Normal: return 1.0 / 2.0 * (1.0 + std::erf(t / std::sqrt(2.0)));
                case kernel_kind::Logistic: return 1.0 - 1.0 / (std::exp(t) + 1.0);
                case kernel_kind::Sigmoid: return (2.0 / sPi) * std::atan(std::exp(t));
                case kernel_kind::Rectangular: return 1.0 / 2.0 * t + 1.0 / 2.0;
                case kernel_kind::Triangular: return t * t * (t < 0.0 ? 1.0 / 2.0 : -1.0 / 2.0) + t + 1.0 / 2.0;
                case kernel_kind::Parabolic: return -1.0 / 4.0 * std::pow(t, 3.0) + 3.0 / 4.0 * t + 1.0 / 2.0;
                case kernel_kind::Quartic: return 3.0 / 16.0 * std::pow(t, 5.0) - 5.0 / 8.0 * std::pow(t, 3.0) + 15.0 / 16.0 * t + 1.0 / 2.0;
                case kernel_kind::Triweight: return 35.0 / 32.0 * (-1.0 / 7.0 * std::pow(t, 7.0) + 3.0 / 5.0 * std::pow(t, 5.0) - std::pow(t, 3.0) + t) + 1.0 / 2.0;
                default: return 1.0 / 2.0 * std::sin((sPi / 2.0) * t) + 1.0 / 2.0;
                }
            }

            // (_newton_raphson)
            double newton_raphson(double aEstimate, double y, double (*f)(double), double (*fPrime)(double))
            {
                double x = aEstimate;
                for (;;)
                {
                    double const diff = f(x) - y;
                    if (!(std::fabs(diff) > 1e-12))
                        return x;
                    x -= diff / fPrime(x);
                }
            }

            double quartic_W(double t) { return kernel_W(kernel_kind::Quartic, t); }
            double quartic_K(double t) { return kernel_K(kernel_kind::Quartic, t); }
            double triweight_W(double t) { return kernel_W(kernel_kind::Triweight, t); }
            double triweight_K(double t) { return kernel_K(kernel_kind::Triweight, t); }

            double kernel_invcdf(kernel_kind aKind, double p)
            {
                switch (aKind)
                {
                case kernel_kind::Normal:
                    if (p <= 0.0 || p >= 1.0)
                        statistics_error("p must be in the range 0.0 < p < 1.0");
                    return normal_dist_inv_cdf(p, 0.0, 1.0);
                case kernel_kind::Logistic:
                    if (1.0 - p == 0.0)
                        py_raise("ZeroDivisionError", "float division by zero");
                    return std::log(p / (1.0 - p));
                case kernel_kind::Sigmoid: return std::log(std::tan(p * sPi / 2.0));
                case kernel_kind::Rectangular: return 2.0 * p - 1.0;
                case kernel_kind::Parabolic: return 2.0 * std::cos((std::acos(2.0 * p - 1.0) + sPi) / 3.0);
                case kernel_kind::Quartic:
                    {
                        auto [sign, q] = p <= 1.0 / 2.0 ? std::pair{ 1.0, p } : std::pair{ -1.0, 1.0 - p };
                        double x = std::pow(2.0 * q, 0.4258865685331) - 1.0;
                        if (q >= 0.004)
                            x += 0.026818732 * std::sin(7.101753784 * q + 2.73230839482953);
                        return newton_raphson(x * sign, p, quartic_W, quartic_K);
                    }
                case kernel_kind::Triweight:
                    {
                        auto [sign, q] = p <= 1.0 / 2.0 ? std::pair{ 1.0, p } : std::pair{ -1.0, 1.0 - p };
                        double const x = std::pow(2.0 * q, 0.3400218741872791) - 1.0;
                        return newton_raphson(x * sign, p, triweight_W, triweight_K);
                    }
                case kernel_kind::Triangular: return p < 1.0 / 2.0 ? std::sqrt(2.0 * p) - 1.0 : 1.0 - std::sqrt(2.0 - 2.0 * p);
                default: return 2.0 * std::asin(2.0 * p - 1.0) / sPi;
                }
            }

            // (the kernel of a name (a kernel argument): null if it is not one)
            std::optional<kernel_kind> kernel_of(py const& aKernel)
            {
                auto const text = str_of_value(builtin_value(aKernel.get()));
                if (text != nullptr)
                    for (auto const& k : sKernels)
                        if (*text == k.name)
                            return k.kind;
                return std::nullopt;
            }

            void check_kde(py const& aData, py const& h)
            {
                auto const n = aData.size();
                if (n == 0)
                    statistics_error("Empty data sequence");
                if (!isinstance(aData[py{ 0 }], py_tuple({ builtin("int"), builtin("float") })))
                    py_raise("TypeError", "Data sequence must contain ints or floats");
                if (h <= py{ 0.0 })
                    statistics_error("Bandwidth h must be positive, not h=" + h.repr());
            }

            py kde(py const& aData, py const& h, py const& aKernel, bool aCumulative)
            {
                check_kde(aData, h);
                auto const kind = kernel_of(aKernel);
                if (!kind)
                    statistics_error("Unknown kernel name: " + aKernel.repr());
                bool const supported = *kind != kernel_kind::Normal && *kind != kernel_kind::Logistic && *kind != kernel_kind::Sigmoid;
                auto const doc = std::string{ aCumulative ? "CDF" : "PDF" } + " estimate with h=" + h.repr() + " and kernel=" + aKernel.repr();
                struct state
                {
                    py data;
                    py h;
                    kernel_kind kind;
                    long long n;
                    py sample;
                    py bandwidth;
                };
                auto s = std::make_shared<state>(state{ aData, h, *kind, aData.size(), {}, {} });
                if (supported)
                {
                    s->sample = builtin("sorted")(aData);
                    s->bandwidth = h * py{ 1.0 };
                }
                auto const t_of = [](state const& aState, py const& x, py const& xi) { return ((x - xi) / aState.h).as_double(); };
                auto body = [s, supported, aCumulative, t_of](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    positional(aCumulative ? "cdf" : "pdf", aArgs, aKwargs, 1u);
                    auto const& x = aArgs[0];
                    auto& st = *s;
                    // (the terms are summed as sum() sums them)
                    std::vector<py> terms;
                    if (!supported)
                    {
                        auto const n = st.data.size();
                        for (auto const& xi : st.data.items())
                            terms.push_back(py{ aCumulative ? kernel_W(st.kind, t_of(st, x, xi)) : kernel_K(st.kind, t_of(st, x, xi)) });
                        auto const total = builtin("sum")(py_list(terms));
                        if (aCumulative)
                            return total / py{ n };
                        return total / (py{ n } * st.h);
                    }
                    if (st.data.size() != st.n)
                    {
                        st.sample = builtin("sorted")(st.data);
                        st.n = st.data.size();
                    }
                    auto const i = import_attribute("bisect", "bisect_left")(st.sample, x - st.bandwidth).as_index();
                    auto const j = import_attribute("bisect", "bisect_right")(st.sample, x + st.bandwidth).as_index();
                    for (long long k = i; k < j; ++k)
                    {
                        auto const xi = st.sample[py{ k }];
                        terms.push_back(py{ aCumulative ? kernel_W(st.kind, t_of(st, x, xi)) : kernel_K(st.kind, t_of(st, x, xi)) });
                    }
                    auto const sum = builtin("sum")(py_list(terms), aCumulative ? py{ i } : py{ 0 });
                    if (aCumulative)
                        return sum / py{ st.n };
                    return sum / (py{ st.n } * st.h);
                };
                return make_closure(aCumulative ? "cdf" : "pdf", aCumulative ? "kde.<locals>.cdf" : "kde.<locals>.pdf", doc, body);
            }

            py kde_random(py const& aData, py const& h, py const& aKernel, py const& aSeed)
            {
                check_kde(aData, h);
                long long hash = 0;
                py_checked(hash_of(aKernel.get(), hash));
                auto const kind = kernel_of(aKernel);
                if (!kind)
                    statistics_error("Unknown kernel name: " + aKernel.repr());
                auto const prng = import_attribute("random", "Random")(aSeed);
                auto const random = prng.attr("random");
                auto const choice = prng.attr("choice");
                auto const doc = "Random KDE selection with h=" + h.repr() + " and kernel=" + aKernel.repr();
                auto const k = *kind;
                py const data = aData;
                return make_closure("rand", "kde_random.<locals>.rand", doc, [data, h, random, choice, k](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    positional("rand", aArgs, aKwargs, 0u);
                    auto const chosen = choice(data);
                    return chosen + h * py{ kernel_invcdf(k, random().as_double()) };
                });
            }

            // --- the functions

            std::vector<py> arguments(char const* aFunction, std::vector<object*> const& aArgs, object* aKwargs, std::vector<char const*> const& aNames,
                std::size_t aRequired, std::size_t aPositionalOnly = 0u, std::vector<char const*> const& aKeywordOnly = {})
            {
                // (keyword-only parameters: aKeywordOnly)
                std::size_t const positional = aNames.size();
                if (aArgs.size() > positional && !aKeywordOnly.empty())
                    py_raise("TypeError", std::string{ aFunction } + "() takes " + std::to_string(positional) + " positional argument" + (positional == 1u ? "" : "s") +
                        " but " + std::to_string(aArgs.size()) + (aArgs.size() == 1u ? " was" : " were") + " given");
                std::vector<char const*> names = aNames;
                names.insert(names.end(), aKeywordOnly.begin(), aKeywordOnly.end());
                std::vector<object*> a;
                py_checked(parse_arguments(aFunction, aArgs, aKwargs, names, aRequired, aPositionalOnly, a));
                std::vector<py> result;
                for (auto v : a)
                    result.push_back(v != nullptr ? py::borrowed(v) : py{});
                return result;
            }

            py mean(py const& aData)
            {
                auto const r = sum_of(iterate(aData));
                if (r.count < 1)
                    statistics_error("mean requires at least one data point");
                return convert(r.total / py{ r.count }, r.T);
            }

            py fmean(py aData, py const& aWeights)
            {
                auto const fsum = math("fsum");
                if (aWeights.empty() || aWeights.is_none())
                {
                    long long n = length(aData.get());
                    if (n < 0)
                    {
                        if (!caught({ "TypeError" }))
                            throw raised{};
                        aData = py_list(aData.items());
                        n = aData.size();
                    }
                    auto const total = fsum(aData);
                    if (n == 0)
                        statistics_error("fmean requires at least one data point");
                    return total / py{ n };
                }
                auto weights = aWeights;
                if (!isinstance(weights, py_tuple({ builtin("list"), builtin("tuple") })))
                    weights = builtin("list")(weights);
                py num;
                try
                {
                    num = math("sumprod")(aData, weights);
                }
                catch (raised const&)
                {
                    if (!caught({ "ValueError" }))
                        throw;
                    statistics_error("data and weights must be the same length");
                }
                auto const den = fsum(weights);
                if (!den.truthy())
                    statistics_error("sum of weights must be non-zero");
                return num / den;
            }

            py geometric_mean(py const& aData)
            {
                long long n = 0;
                bool foundZero = false;
                auto const isnan = math("isnan");
                auto const log = math("log");
                std::vector<py> logs;
                auto const values = iterate(aData);
                py x;
                while (values(x))
                {
                    ++n;
                    if (x > py{ 0.0 } || isnan(x).truthy())
                        logs.push_back(log(x));
                    else if (x == py{ 0.0 })
                        foundZero = true;
                    else
                        py_raise(statistics_error_class()(py{ "No negative inputs allowed" }, x));
                }
                auto const total = math("fsum")(py_list(logs));
                if (n == 0)
                    statistics_error("Must have a non-empty dataset");
                auto const t = total.as_double();
                if (std::isnan(t))
                    return py{ std::nan("") };
                if (foundZero)
                    return py{ t == HUGE_VAL ? std::nan("") : 0.0 };
                return math("exp")(total / py{ n });
            }

            py harmonic_mean(py aData, py aWeights)
            {
                if (py{ iter(aData.get()) }.is(aData))
                    aData = builtin("list")(aData);
                std::string const errmsg = "harmonic mean does not support negative values";
                auto const n = aData.size();
                if (n < 1)
                    statistics_error("harmonic_mean requires at least one data point");
                bool const noWeights = aWeights.empty() || aWeights.is_none();
                if (n == 1 && noWeights)
                {
                    auto const x = aData[py{ 0 }];
                    if (isinstance(x, py_tuple({ import_attribute("numbers", "Real"), decimal_class() })))
                    {
                        if (x < py{ 0 })
                            statistics_error(errmsg);
                        return x;
                    }
                    py_raise("TypeError", "unsupported type");
                }
                py sumWeights;
                source weights;
                if (noWeights)
                {
                    auto remaining = std::make_shared<long long>(n);
                    weights = [remaining](py& aResult) -> bool
                    {
                        if (*remaining == 0)
                            return false;
                        --*remaining;
                        aResult = py{ 1 };
                        return true;
                    };
                    sumWeights = py{ n };
                }
                else
                {
                    if (py{ iter(aWeights.get()) }.is(aWeights))
                        aWeights = builtin("list")(aWeights);
                    if (aWeights.size() != n)
                        statistics_error("Number of weights does not match data size");
                    sumWeights = sum_of(fail_negative(iterate(aWeights), errmsg)).total;
                    weights = iterate(aWeights);
                }
                sum_result r;
                try
                {
                    auto const data = fail_negative(iterate(aData), errmsg);
                    r = sum_of([&](py& aResult) -> bool
                    {
                        py w, x;
                        if (!weights(w) || !data(x))
                            return false;
                        aResult = w.truthy() ? w / x : py{ 0 };
                        return true;
                    });
                }
                catch (raised const&)
                {
                    if (!caught({ "ZeroDivisionError" }))
                        throw;
                    return py{ 0 };
                }
                if (r.total <= py{ 0 })
                    statistics_error("Weighted sum must be positive");
                return convert(sumWeights / r.total, r.T);
            }

            py sorted_data(py const& aData) { return builtin("sorted")(aData); }

            py median(py const& aData)
            {
                auto const data = sorted_data(aData);
                auto const n = data.size();
                if (n == 0)
                    statistics_error("no median for empty data");
                if (n % 2 == 1)
                    return data[py{ n / 2 }];
                auto const i = n / 2;
                return (data[py{ i - 1 }] + data[py{ i }]) / py{ 2 };
            }

            py median_low(py const& aData)
            {
                auto const data = sorted_data(aData);
                auto const n = data.size();
                if (n == 0)
                    statistics_error("no median for empty data");
                return n % 2 == 1 ? data[py{ n / 2 }] : data[py{ n / 2 - 1 }];
            }

            py median_high(py const& aData)
            {
                auto const data = sorted_data(aData);
                auto const n = data.size();
                if (n == 0)
                    statistics_error("no median for empty data");
                return data[py{ n / 2 }];
            }

            py median_grouped(py const& aData, py aInterval)
            {
                auto const data = sorted_data(aData);
                auto const n = data.size();
                if (n == 0)
                    statistics_error("no median for empty data");
                auto x = data[py{ n / 2 }];
                auto const i = import_attribute("bisect", "bisect_left")(data, x).as_index();
                auto const j = import_attribute("bisect", "bisect_right").call({ data, x }, { { "lo", py{ i } } }).as_index();
                auto const floatType = builtin("float");
                try
                {
                    aInterval = floatType(aInterval);
                    x = floatType(x);
                }
                catch (raised const&)
                {
                    if (!caught({ "ValueError" }))
                        throw;
                    py_raise("TypeError", "Value cannot be converted to a float");
                }
                double const interval = aInterval.as_double();
                double const L = x.as_double() - interval / 2.0;
                double const cf = static_cast<double>(i);
                double const f = static_cast<double>(j - i);
                return py{ L + interval * (static_cast<double>(n) / 2.0 - cf) / f };
            }

            py mode(py const& aData)
            {
                auto const pairs = import_attribute("collections", "Counter")(py{ iter(aData.get()) }).method("most_common", { py{ 1 } });
                if (pairs.size() == 0)
                    statistics_error("no mode for empty data");
                return pairs[py{ 0 }][py{ 0 }];
            }

            py multimode(py const& aData)
            {
                auto const counts = import_attribute("collections", "Counter")(py{ iter(aData.get()) });
                if (!counts.truthy())
                    return py_list();
                auto const maxcount = builtin("max")(counts.method("values"));
                std::vector<py> result;
                for (auto const& item : counts.method("items").items())
                    if (item[py{ 1 }] == maxcount)
                        result.push_back(item[py{ 0 }]);
                return py_list(result);
            }

            py quantiles(py const& aData, py const& aN, py const& aMethod)
            {
                if (aN < py{ 1 })
                    statistics_error("n must be at least 1");
                auto const data = sorted_data(aData);
                auto const ld = data.size();
                if (ld < 2)
                {
                    if (ld == 1)
                        return data * (aN - py{ 1 });
                    statistics_error("must have at least one data point");
                }
                auto const n = aN.as_index();
                std::vector<py> result;
                if (aMethod == py{ "inclusive" })
                {
                    auto const m = ld - 1;
                    for (long long i = 1; i < n; ++i)
                    {
                        auto const j = i * m / n;
                        auto const delta = i * m % n;
                        result.push_back((data[py{ j }] * py{ n - delta } + data[py{ j + 1 }] * py{ delta }) / py{ n });
                    }
                    return py_list(result);
                }
                if (aMethod == py{ "exclusive" })
                {
                    auto const m = ld + 1;
                    for (long long i = 1; i < n; ++i)
                    {
                        auto j = i * m / n;
                        j = j < 1 ? 1 : j > ld - 1 ? ld - 1 : j;
                        auto const delta = i * m - j * n;
                        result.push_back((data[py{ j - 1 }] * py{ n - delta } + data[py{ j }] * py{ delta }) / py{ n });
                    }
                    return py_list(result);
                }
                py_raise("ValueError", "Unknown method: " + aMethod.repr());
            }

            py variance(py const& aData, py const& aXbar, bool aPopulation)
            {
                auto const r = ss_of(aData, aXbar);
                if (r.count < (aPopulation ? 1 : 2))
                    statistics_error(aPopulation ? "pvariance requires at least one data point" : "variance requires at least two data points");
                return convert(r.ssd / py{ aPopulation ? r.count : r.count - 1 }, r.T);
            }

            py stdev(py const& aData, py const& aXbar, bool aPopulation)
            {
                auto const r = ss_of(aData, aXbar);
                if (r.count < (aPopulation ? 1 : 2))
                    statistics_error(aPopulation ? "pstdev requires at least one data point" : "stdev requires at least two data points");
                return root_of(r.T, r.ssd / py{ aPopulation ? r.count : r.count - 1 });
            }

            long long same_length(py const& x, py const& y, char const* aWhat)
            {
                auto const n = x.size();
                if (y.size() != n)
                    statistics_error(std::string{ aWhat } + " requires that both inputs have same number of data points");
                if (n < 2)
                    statistics_error(std::string{ aWhat } + " requires at least two data points");
                return n;
            }

            py deviations(py const& aData, py const& aMean)
            {
                std::vector<py> result;
                for (auto const& v : aData.items())
                    result.push_back(v - aMean);
                return py_list(result);
            }

            py covariance(py const& x, py const& y)
            {
                auto const n = same_length(x, y, "covariance");
                auto const fsum = math("fsum");
                auto const xbar = fsum(x) / py{ n };
                auto const ybar = fsum(y) / py{ n };
                auto const sxy = math("sumprod")(deviations(x, xbar), deviations(y, ybar));
                return sxy / py{ n - 1 };
            }

            py correlation(py x, py y, py const& aMethod)
            {
                auto const n = same_length(x, y, "correlation");
                if (!(aMethod == py{ "linear" }) && !(aMethod == py{ "ranked" }))
                {
                    long long hash = 0;
                    py_checked(hash_of(aMethod.get(), hash));
                    py_raise("ValueError", "Unknown method: " + aMethod.repr());
                }
                if (aMethod == py{ "ranked" })
                {
                    py const start{ static_cast<double>(n - 1) / -2.0 };
                    x = rank(x, start);
                    y = rank(y, start);
                }
                else
                {
                    auto const fsum = math("fsum");
                    auto const xbar = fsum(x) / py{ n };
                    auto const ybar = fsum(y) / py{ n };
                    x = deviations(x, xbar);
                    y = deviations(y, ybar);
                }
                auto const sumprod = math("sumprod");
                auto const sxy = sumprod(x, y);
                auto const sxx = sumprod(x, x);
                auto const syy = sumprod(y, y);
                try
                {
                    return sxy / sqrtprod(sxx, syy);
                }
                catch (raised const&)
                {
                    if (!caught({ "ZeroDivisionError" }))
                        throw;
                    statistics_error("at least one of the inputs is constant");
                }
            }

            object* sLinearRegression = nullptr;

            py linear_regression(py x, py y, bool aProportional)
            {
                auto const n = same_length(x, y, "linear regression");
                py xbar, ybar;
                if (!aProportional)
                {
                    auto const fsum = math("fsum");
                    xbar = fsum(x) / py{ n };
                    ybar = fsum(y) / py{ n };
                    x = deviations(x, xbar);
                    y = deviations(y, ybar);
                }
                auto const sumprod = math("sumprod");
                auto const sxy = sumprod(x, y) + py{ 0.0 };
                auto const sxx = sumprod(x, x);
                py slope;
                try
                {
                    slope = sxy / sxx;
                }
                catch (raised const&)
                {
                    if (!caught({ "ZeroDivisionError" }))
                        throw;
                    statistics_error("x is constant");
                }
                auto const intercept = aProportional ? py{ 0.0 } : ybar - slope * xbar;
                return py::borrowed(sLinearRegression).call({}, { { "slope", slope }, { "intercept", intercept } });
            }

            bool proportional_of(py const& aValue) { return !aValue.empty() && aValue.truthy(); }

            builtin_entry const sStatisticsFunctions[] =
            {
                { "mean", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return py_guarded([&]() { auto const a = arguments("mean", aArgs, aKwargs, { "data" }, 1u); return mean(a[0]); }); } },
                { "fmean", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return py_guarded([&]() { auto const a = arguments("fmean", aArgs, aKwargs, { "data", "weights" }, 1u); return fmean(a[0], a[1]); }); } },
                { "geometric_mean", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return py_guarded([&]() { auto const a = arguments("geometric_mean", aArgs, aKwargs, { "data" }, 1u); return geometric_mean(a[0]); }); } },
                { "harmonic_mean", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return py_guarded([&]() { auto const a = arguments("harmonic_mean", aArgs, aKwargs, { "data", "weights" }, 1u); return harmonic_mean(a[0], a[1]); }); } },
                { "median", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return py_guarded([&]() { auto const a = arguments("median", aArgs, aKwargs, { "data" }, 1u); return median(a[0]); }); } },
                { "median_low", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return py_guarded([&]() { auto const a = arguments("median_low", aArgs, aKwargs, { "data" }, 1u); return median_low(a[0]); }); } },
                { "median_high", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return py_guarded([&]() { auto const a = arguments("median_high", aArgs, aKwargs, { "data" }, 1u); return median_high(a[0]); }); } },
                { "median_grouped", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = arguments("median_grouped", aArgs, aKwargs, { "data", "interval" }, 1u);
                            return median_grouped(a[0], a[1].empty() ? py{ 1.0 } : a[1]);
                        });
                    } },
                { "mode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return py_guarded([&]() { auto const a = arguments("mode", aArgs, aKwargs, { "data" }, 1u); return mode(a[0]); }); } },
                { "multimode", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return py_guarded([&]() { auto const a = arguments("multimode", aArgs, aKwargs, { "data" }, 1u); return multimode(a[0]); }); } },
                { "kde", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = arguments("kde", aArgs, aKwargs, { "data", "h", "kernel" }, 2u, 0u, { "cumulative" });
                            return kde(a[0], a[1], a[2].empty() ? py{ "normal" } : a[2], proportional_of(a[3]));
                        });
                    } },
                { "kde_random", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = arguments("kde_random", aArgs, aKwargs, { "data", "h", "kernel" }, 2u, 0u, { "seed" });
                            return kde_random(a[0], a[1], a[2].empty() ? py{ "normal" } : a[2], a[3].empty() ? py::none() : a[3]);
                        });
                    } },
                { "quantiles", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = arguments("quantiles", aArgs, aKwargs, { "data" }, 1u, 0u, { "n", "method" });
                            return quantiles(a[0], a[1].empty() ? py{ 4 } : a[1], a[2].empty() ? py{ "exclusive" } : a[2]);
                        });
                    } },
                { "variance", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return py_guarded([&]() { auto const a = arguments("variance", aArgs, aKwargs, { "data", "xbar" }, 1u); return variance(a[0], a[1], false); }); } },
                { "pvariance", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return py_guarded([&]() { auto const a = arguments("pvariance", aArgs, aKwargs, { "data", "mu" }, 1u); return variance(a[0], a[1], true); }); } },
                { "stdev", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return py_guarded([&]() { auto const a = arguments("stdev", aArgs, aKwargs, { "data", "xbar" }, 1u); return stdev(a[0], a[1], false); }); } },
                { "pstdev", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return py_guarded([&]() { auto const a = arguments("pstdev", aArgs, aKwargs, { "data", "mu" }, 1u); return stdev(a[0], a[1], true); }); } },
                { "covariance", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return py_guarded([&]() { auto const a = arguments("covariance", aArgs, aKwargs, { "x", "y" }, 2u, 2u); return covariance(a[0], a[1]); }); } },
                { "correlation", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = arguments("correlation", aArgs, aKwargs, { "x", "y" }, 2u, 2u, { "method" });
                            return correlation(a[0], a[1], a[2].empty() ? py{ "linear" } : a[2]);
                        });
                    } },
                { "linear_regression", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = arguments("linear_regression", aArgs, aKwargs, { "x", "y" }, 2u, 2u, { "proportional" });
                            return linear_regression(a[0], a[1], proportional_of(a[2]));
                        });
                    } },
                { "_normal_dist_inv_cdf", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = arguments("_normal_dist_inv_cdf", aArgs, aKwargs, { "p", "mu", "sigma" }, 3u, 3u);
                            return py{ normal_dist_inv_cdf(a[0].as_double(), a[1].as_double(), a[2].as_double()) };
                        });
                    } },
                { "_sum", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = arguments("_sum", aArgs, aKwargs, { "data" }, 1u);
                            auto const r = sum_of(iterate(a[0]));
                            return py_tuple({ r.T, r.total, py{ r.count } });
                        });
                    } },
                { "_ss", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = arguments("_ss", aArgs, aKwargs, { "data", "c" }, 1u);
                            auto const r = ss_of(a[0], a[1]);
                            return py_tuple({ r.T, r.ssd, r.c, py{ r.count } });
                        });
                    } },
                { "_exact_ratio", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = arguments("_exact_ratio", aArgs, aKwargs, { "x" }, 1u);
                            auto const [n, d] = exact_ratio(a[0]);
                            return py_tuple({ n, d });
                        });
                    } },
                { "_convert", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return py_guarded([&]() { auto const a = arguments("_convert", aArgs, aKwargs, { "value", "T" }, 2u); return convert(a[0], a[1]); }); } },
                { "_coerce", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return py_guarded([&]() { auto const a = arguments("_coerce", aArgs, aKwargs, { "T", "S" }, 2u); return coerce(a[0], a[1]); }); } },
                { "_isfinite", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return py_guarded([&]() { auto const a = arguments("_isfinite", aArgs, aKwargs, { "x" }, 1u); return py::boolean(is_finite(a[0])); }); } },
                { "_rank", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = arguments("_rank", aArgs, aKwargs, { "data" }, 1u, 1u, { "key", "reverse", "ties", "start" });
                            if (!a[3].empty() && !(a[3] == py{ "average" }))
                                py_raise("ValueError", "Unknown tie resolution method: " + a[3].repr());
                            auto data = a[0];
                            if (!a[1].empty() && !a[1].is_none())
                                data = builtin("map")(a[1], data);
                            return rank(data, a[4].empty() ? py{ 1 } : a[4], !a[2].empty() && a[2].truthy());
                        });
                    } },
                { "_mean_stdev", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = arguments("_mean_stdev", aArgs, aKwargs, { "data" }, 1u);
                            auto const [m, s] = mean_stdev(a[0]);
                            return py_tuple({ m, s });
                        });
                    } },
                { "_sqrtprod", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return py_guarded([&]() { auto const a = arguments("_sqrtprod", aArgs, aKwargs, { "x", "y" }, 2u); return sqrtprod(a[0], a[1]); }); } }
            };

            // --- NormalDist

            object* sNormalDist = nullptr;

            double mu_of(py const& aSelf) { return aSelf.attr("_mu").as_double(); }
            double sigma_of(py const& aSelf) { return aSelf.attr("_sigma").as_double(); }
            py normal_dist(py const& aMu, py const& aSigma) { return py::borrowed(sNormalDist)(aMu, aSigma); }
            bool is_normal_dist(py const& aObject) { return isinstance(aObject, py::borrowed(sNormalDist)); }

            std::vector<py> self_args(char const* aFunction, std::vector<object*> const& aArgs, object* aKwargs, std::vector<char const*> const& aNames,
                std::size_t aRequired, std::vector<char const*> const& aKeywordOnly = {})
            {
                if (aArgs.empty())
                    py_raise("TypeError", std::string{ aFunction } + "() missing 1 required positional argument: 'self'");
                std::vector<object*> const rest{ aArgs.begin() + 1, aArgs.end() };
                auto result = arguments(aFunction, rest, aKwargs, aNames, aRequired, 0u, aKeywordOnly);
                result.insert(result.begin(), py::borrowed(aArgs[0]));
                return result;
            }

            builtin_entry const sNormalDistMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.__init__", aArgs, aKwargs, { "mu", "sigma" }, 0u);
                            py const mu = a[1].empty() ? py{ 0.0 } : a[1];
                            py const sigma = a[2].empty() ? py{ 1.0 } : a[2];
                            if (sigma < py{ 0.0 })
                                statistics_error("sigma must be non-negative");
                            auto const floatType = builtin("float");
                            a[0].set_attr("_mu", floatType(mu));
                            a[0].set_attr("_sigma", floatType(sigma));
                            return py::none();
                        });
                    } },
                { "samples", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.samples", aArgs, aKwargs, { "n" }, 1u, { "seed" });
                            auto const rnd = a[2].empty() || a[2].is_none() ? import_attribute("random", "random") :
                                import_attribute("random", "Random")(a[2]).attr("random");
                            auto const mu = mu_of(a[0]);
                            auto const sigma = sigma_of(a[0]);
                            auto const n = a[1].as_index();
                            std::vector<py> result;
                            for (long long i = 0; i < n; ++i)
                                result.push_back(py{ normal_dist_inv_cdf(rnd().as_double(), mu, sigma) });
                            return py_list(result);
                        });
                    } },
                { "pdf", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.pdf", aArgs, aKwargs, { "x" }, 1u);
                            auto const sigma = sigma_of(a[0]);
                            auto const variance = sigma * sigma;
                            if (variance == 0.0)
                                statistics_error("pdf() not defined when sigma is zero");
                            auto const diff = a[1] - a[0].attr("_mu");
                            return py{ std::exp((diff * diff / py{ -2.0 * variance }).as_double()) / std::sqrt(sTau * variance) };
                        });
                    } },
                { "cdf", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.cdf", aArgs, aKwargs, { "x" }, 1u);
                            auto const sigma = sigma_of(a[0]);
                            if (sigma == 0.0)
                                statistics_error("cdf() not defined when sigma is zero");
                            return py{ 0.5 * (1.0 + std::erf(((a[1] - a[0].attr("_mu")) / py{ sigma * sSqrt2 }).as_double())) };
                        });
                    } },
                { "inv_cdf", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.inv_cdf", aArgs, aKwargs, { "p" }, 1u);
                            if (a[1] <= py{ 0.0 } || a[1] >= py{ 1.0 })
                                statistics_error("p must be in the range 0.0 < p < 1.0");
                            return py{ normal_dist_inv_cdf(a[1].as_double(), mu_of(a[0]), sigma_of(a[0])) };
                        });
                    } },
                { "quantiles", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.quantiles", aArgs, aKwargs, { "n" }, 0u);
                            auto const n = a[1].empty() ? py{ 4 } : a[1];
                            std::vector<py> result;
                            auto const count = n.as_index();
                            for (long long i = 1; i < count; ++i)
                                result.push_back(a[0].method("inv_cdf", { py{ i } / n }));
                            return py_list(result);
                        });
                    } },
                { "overlap", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.overlap", aArgs, aKwargs, { "other" }, 1u);
                            if (!is_normal_dist(a[1]))
                                py_raise("TypeError", "Expected another NormalDist instance");
                            auto X = a[0], Y = a[1];
                            auto const ys = sigma_of(Y), xs = sigma_of(X), ym = mu_of(Y), xm = mu_of(X);
                            if (!(ys == xs) ? ys < xs : ym < xm)
                                std::swap(X, Y);
                            auto const xVar = X.attr("variance").as_double(), yVar = Y.attr("variance").as_double();
                            if (xVar == 0.0 || yVar == 0.0)
                                statistics_error("overlap() not defined when sigma is zero");
                            auto const dv = yVar - xVar;
                            auto const dm = std::fabs(mu_of(Y) - mu_of(X));
                            if (dv == 0.0)
                                return py{ 1.0 - std::erf(dm / (2.0 * sigma_of(X) * sSqrt2)) };
                            auto const am = mu_of(X) * yVar - mu_of(Y) * xVar;
                            auto const b = sigma_of(X) * sigma_of(Y) * std::sqrt(dm * dm + dv * std::log(yVar / xVar));
                            py const x1{ (am + b) / dv };
                            py const x2{ (am - b) / dv };
                            auto const cdf = [](py const& aDist, py const& x) { return aDist.method("cdf", { x }).as_double(); };
                            return py{ 1.0 - (std::fabs(cdf(Y, x1) - cdf(X, x1)) + std::fabs(cdf(Y, x2) - cdf(X, x2))) };
                        });
                    } },
                { "zscore", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.zscore", aArgs, aKwargs, { "x" }, 1u);
                            if (sigma_of(a[0]) == 0.0)
                                statistics_error("zscore() not defined when sigma is zero");
                            return (a[1] - a[0].attr("_mu")) / a[0].attr("_sigma");
                        });
                    } },
                { "__add__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.__add__", aArgs, aKwargs, { "x2" }, 1u);
                            if (is_normal_dist(a[1]))
                                return normal_dist(a[0].attr("_mu") + a[1].attr("_mu"), py{ std::hypot(sigma_of(a[0]), sigma_of(a[1])) });
                            return normal_dist(a[0].attr("_mu") + a[1], a[0].attr("_sigma"));
                        });
                    } },
                { "__sub__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.__sub__", aArgs, aKwargs, { "x2" }, 1u);
                            if (is_normal_dist(a[1]))
                                return normal_dist(a[0].attr("_mu") - a[1].attr("_mu"), py{ std::hypot(sigma_of(a[0]), sigma_of(a[1])) });
                            return normal_dist(a[0].attr("_mu") - a[1], a[0].attr("_sigma"));
                        });
                    } },
                { "__mul__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.__mul__", aArgs, aKwargs, { "x2" }, 1u);
                            return normal_dist(a[0].attr("_mu") * a[1], a[0].attr("_sigma") * math("fabs")(a[1]));
                        });
                    } },
                { "__truediv__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.__truediv__", aArgs, aKwargs, { "x2" }, 1u);
                            return normal_dist(a[0].attr("_mu") / a[1], a[0].attr("_sigma") / math("fabs")(a[1]));
                        });
                    } },
                { "__pos__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.__pos__", aArgs, aKwargs, {}, 0u);
                            return normal_dist(a[0].attr("_mu"), a[0].attr("_sigma"));
                        });
                    } },
                { "__neg__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.__neg__", aArgs, aKwargs, {}, 0u);
                            return normal_dist(-a[0].attr("_mu"), a[0].attr("_sigma"));
                        });
                    } },
                { "__rsub__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.__rsub__", aArgs, aKwargs, { "x2" }, 1u);
                            return -(a[0] - a[1]);
                        });
                    } },
                { "__eq__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.__eq__", aArgs, aKwargs, { "x2" }, 1u);
                            if (!is_normal_dist(a[1]))
                                return py::borrowed(not_implemented());
                            return py::boolean(a[0].attr("_mu") == a[1].attr("_mu") && a[0].attr("_sigma") == a[1].attr("_sigma"));
                        });
                    } },
                { "__hash__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.__hash__", aArgs, aKwargs, {}, 0u);
                            return builtin("hash")(py_tuple({ a[0].attr("_mu"), a[0].attr("_sigma") }));
                        });
                    } },
                { "__repr__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.__repr__", aArgs, aKwargs, {}, 0u);
                            return py{ name_of(type_of(a[0])) + "(mu=" + a[0].attr("_mu").repr() + ", sigma=" + a[0].attr("_sigma").repr() + ")" };
                        });
                    } },
                { "__getstate__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.__getstate__", aArgs, aKwargs, {}, 0u);
                            return py_tuple({ a[0].attr("_mu"), a[0].attr("_sigma") });
                        });
                    } },
                { "__setstate__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const a = self_args("NormalDist.__setstate__", aArgs, aKwargs, { "state" }, 1u);
                            auto const items = a[1].items();
                            if (items.size() != 2u)
                                py_raise("ValueError", items.size() < 2u ? "not enough values to unpack (expected 2, got " + std::to_string(items.size()) + ")" :
                                    "too many values to unpack (expected 2)");
                            a[0].set_attr("_mu", items[0]);
                            a[0].set_attr("_sigma", items[1]);
                            return py::none();
                        });
                    } },
                { "_mean", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() { return py::borrowed(aArgs.at(0u)).attr("_mu"); }); } },
                { "_stdev", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() { return py::borrowed(aArgs.at(0u)).attr("_sigma"); }); } },
                { "_variance", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        return py_guarded([&]()
                        {
                            auto const sigma = py::borrowed(aArgs.at(0u)).attr("_sigma");
                            return sigma * sigma;
                        });
                    } }
            };

            builtin_entry const sFromSamples{ "from_samples", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        if (aArgs.empty())
                            py_raise("TypeError", "from_samples() missing 1 required positional argument: 'cls'");
                        std::vector<object*> const rest{ aArgs.begin() + 1, aArgs.end() };
                        auto const a = arguments("from_samples", rest, aKwargs, { "data" }, 1u);
                        auto const [m, s] = mean_stdev(a[0]);
                        return py::borrowed(aArgs[0])(m, s);
                    });
                } };

            bool make_normal_dist()
            {
                if (sNormalDist != nullptr)
                    return true;
                auto const made = make_class_of("statistics", "NormalDist", {}, sNormalDistMethods, { { "__doc__", make_str("Normal distribution of a random variable") } });
                if (made == nullptr)
                    return false;
                py const cls{ made };
                auto const result = py_guarded([&]()
                {
                    for (auto const& [property, getter] : { std::pair{ "mean", "_mean" }, std::pair{ "median", "_mean" }, std::pair{ "mode", "_mean" },
                        std::pair{ "stdev", "_stdev" }, std::pair{ "variance", "_variance" } })
                    {
                        py const p{ b_property({ cls.attr(getter).get() }, nullptr) };
                        py_checked(class_setattr_raw(cls.get(), property, p.get()) == 0);
                    }
                    for (auto name : { "_mean", "_stdev", "_variance" })
                        py_checked(class_setattr_raw(cls.get(), name, nullptr) == 0);
                    py_checked(class_setattr_raw(cls.get(), "__radd__", cls.attr("__add__").get()) == 0);
                    py_checked(class_setattr_raw(cls.get(), "__rmul__", cls.attr("__mul__").get()) == 0);
                    py const fromSamples{ make_builtin(sFromSamples) };
                    py const method{ b_classmethod({ fromSamples.get() }, nullptr) };
                    py_checked(class_setattr_raw(cls.get(), "from_samples", method.get()) == 0);
                    sNormalDist = cls.reference();
                    return py::none();
                });
                release(result);
                return result != nullptr;
            }

            bool statistics_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sStatisticsFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("StatisticsError", module_exception("statistics.StatisticsError", { "ValueError" }));
                if (!make_normal_dist())
                    return false;
                aAttributes.emplace_back("NormalDist", new_reference(sNormalDist));
                if (sLinearRegression == nullptr)
                {
                    auto const made = py_guarded([&]()
                    {
                        return import_attribute("collections", "namedtuple").call({ py{ "LinearRegression" }, py_tuple({ py{ "slope" }, py{ "intercept" } }) },
                            { { "module", py{ "statistics" } } });
                    });
                    if (made == nullptr)
                        return false;
                    sLinearRegression = made;
                }
                aAttributes.emplace_back("LinearRegression", new_reference(sLinearRegression));
                std::vector<object*> all;
                for (auto name : { "NormalDist", "StatisticsError", "correlation", "covariance", "fmean", "geometric_mean", "harmonic_mean", "kde", "kde_random",
                    "linear_regression", "mean", "median", "median_grouped", "median_high", "median_low", "mode", "multimode", "pstdev", "pvariance", "quantiles",
                    "stdev", "variance" })
                    all.push_back(make_str(name));
                aAttributes.emplace_back("__all__", make_list(std::move(all)));
                for (auto const& [name, module, attribute] : { std::tuple{ "Fraction", "fractions", "Fraction" }, std::tuple{ "Decimal", "decimal", "Decimal" } })
                {
                    auto const value = module_attribute(module, attribute);
                    if (value == nullptr)
                        return false;
                    aAttributes.emplace_back(name, value);
                }
                return true;
            }

            inline bool const sStatistics = register_module("statistics", statistics_module);
        }
    }
}
