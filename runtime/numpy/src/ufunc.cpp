/*
  ufunc.cpp

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

  Portions of this file are derived from NumPy 2.5.3 (numpy/_core/src/npymath/npy_math_internal.h.src,
  numpy/_core/src/umath/loops_utils.h.src and numpy/_core/src/umath/ufunc_type_resolution.c), Copyright (c) 2005-2025,
  NumPy Developers; All rights reserved. They are used under NumPy's BSD 3-Clause licence (see
  runtime/numpy/LICENSE.NumPy). Changes: translated from C to C++ for the neos NumPy runtime plugin and adapted to its
  object model (see runtime/numpy/NOTICE).
*/

// universal functions: type resolution (NumPy's promotion rules, NEP 50 for Python scalars), broadcasting element loops,
// reductions and accumulations

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include "numpy.hpp"
#include "cast.hpp"
#include "ufunc.hpp"
#include "kernels.hpp"
#include "simd.hpp"

namespace neos::numpy
{
    namespace
    {
        template <typename T> using compute_t = std::conditional_t<is_half_v<T>, float, T>;

        template <typename R, typename T>
        inline auto narrow_result(R aResult)
        {
            if constexpr (is_half_v<T> && std::is_same_v<R, float>)
                return half{ aResult };
            else
                return aResult;
        }

        // (Python's floor division / modulo of floats, as npy_divmod)
        template <typename T>
        std::pair<T, T> float_divmod(T a, T b)
        {
            if (b == 0)
            {
                T const mod = std::fmod(a, b);
                return { a / b, mod };
            }
            T mod = std::fmod(a, b);
            T div = (a - mod) / b;
            if (mod != 0)
            {
                if ((b < 0) != (mod < 0))
                {
                    mod += b;
                    div -= T(1);
                }
            }
            else
                mod = std::copysign(T(0), b);
            T floordiv;
            if (div != 0)
            {
                floordiv = std::floor(div);
                if (div - floordiv > T(0.5))
                    floordiv += T(1);
            }
            else
                floordiv = std::copysign(T(0), a / b);
            return { floordiv, mod };
        }

        template <typename T>
        T integer_floor_divide(T a, T b)
        {
            if (b == 0)
                return 0;
            if constexpr (std::is_signed_v<T>)
            {
                if (a == std::numeric_limits<T>::min() && b == -1)
                    return std::numeric_limits<T>::min();
                T q = static_cast<T>(a / b);
                if ((a % b != 0) && ((a < 0) != (b < 0)))
                    --q;
                return q;
            }
            else
                return static_cast<T>(a / b);
        }

        template <typename T>
        T integer_remainder(T a, T b)
        {
            if (b == 0)
                return 0;
            if constexpr (std::is_signed_v<T>)
            {
                if (b == -1)
                    return 0;
                T r = static_cast<T>(a % b);
                if (r != 0 && ((r < 0) != (b < 0)))
                    r = static_cast<T>(r + b);
                return r;
            }
            else
                return static_cast<T>(a % b);
        }

        template <typename T>
        T integer_power(T a, T b)
        {
            using U = std::make_unsigned_t<T>;
            if constexpr (std::is_signed_v<T>)
                if (b < 0)
                    return 0; // (raised before the loop)
            U base = static_cast<U>(a);
            U result = 1u;
            auto e = static_cast<std::uint64_t>(b);
            while (e != 0u)
            {
                if ((e & 1u) != 0u)
                    result = static_cast<U>(result * base);
                base = static_cast<U>(base * base);
                e >>= 1u;
            }
            return static_cast<T>(result);
        }

        template <typename T>
        bool complex_less(T a, T b)
        {
            // (NumPy orders complex numbers lexicographically: real then imaginary parts)
            if (a.real() < b.real())
                return a.imag() == a.imag() && b.imag() == b.imag();
            if (a.real() > b.real())
                return false;
            if (a.real() == b.real())
                return a.imag() < b.imag();
            return false;
        }
    }

    // --- operations (Op::apply on the compute type; supports<T>: whether there is a loop for T)

#define NEOS_NUMPY_ALL template <typename T> static constexpr bool supports = true;
#define NEOS_NUMPY_INTEGRAL template <typename T> static constexpr bool supports = std::is_integral_v<T>;
#define NEOS_NUMPY_REAL template <typename T> static constexpr bool supports = !is_complex_v<T>;
#define NEOS_NUMPY_INEXACT template <typename T> static constexpr bool supports = is_floating_v<T> || is_complex_v<T>;
#define NEOS_NUMPY_FLOATING template <typename T> static constexpr bool supports = is_floating_v<T>;

    namespace ops
    {
        // (simd: the operation's kernels (kernels.hpp), whose operations on real values are these operations')
        struct add { NEOS_NUMPY_ALL static constexpr simd::op simd = simd::op::Add; template <typename T> static T apply(T a, T b) { if constexpr (is_bool_v<T>) return a || b; else return static_cast<T>(a + b); } };
        struct subtract { NEOS_NUMPY_ALL static constexpr simd::op simd = simd::op::Subtract; template <typename T> static T apply(T a, T b) { if constexpr (is_bool_v<T>) return a != b; else return static_cast<T>(a - b); } };
        struct multiply { NEOS_NUMPY_ALL static constexpr simd::op simd = simd::op::Multiply; template <typename T> static T apply(T a, T b) { if constexpr (is_bool_v<T>) return a && b; else return static_cast<T>(a * b); } };
        struct true_divide { NEOS_NUMPY_INEXACT static constexpr simd::op simd = simd::op::TrueDivide; template <typename T> static T apply(T a, T b) { return a / b; } };
        struct floor_divide
        {
            NEOS_NUMPY_REAL
            template <typename T> static T apply(T a, T b)
            {
                if constexpr (std::is_floating_point_v<T>) return float_divmod(a, b).first;
                else if constexpr (is_bool_v<T>) return a && b;
                else return integer_floor_divide(a, b);
            }
        };
        struct remainder
        {
            NEOS_NUMPY_REAL
            template <typename T> static T apply(T a, T b)
            {
                if constexpr (std::is_floating_point_v<T>) return b == 0 ? std::fmod(a, b) : float_divmod(a, b).second;
                else if constexpr (is_bool_v<T>) return false;
                else return integer_remainder(a, b);
            }
        };
        struct fmod
        {
            NEOS_NUMPY_REAL
            template <typename T> static T apply(T a, T b)
            {
                if constexpr (std::is_floating_point_v<T>) return std::fmod(a, b);
                else if constexpr (is_bool_v<T>) return false;
                else
                {
                    if (b == 0) return 0;
                    if constexpr (std::is_signed_v<T>) if (b == -1) return 0;
                    return static_cast<T>(a % b);
                }
            }
        };
        struct power
        {
            NEOS_NUMPY_ALL
            template <typename T> static T apply(T a, T b)
            {
                if constexpr (is_complex_v<T>)
                {
                    if (b == T(0)) return T(1);
                    if (a == T(0)) return (b.real() > 0 && b.imag() == 0) ? T(0) : T(std::numeric_limits<typename T::value_type>::quiet_NaN(), std::numeric_limits<typename T::value_type>::quiet_NaN());
                    return std::pow(a, b);
                }
                else if constexpr (std::is_floating_point_v<T>) return std::pow(a, b);
                else if constexpr (is_bool_v<T>) return a || !b;
                else return integer_power(a, b);
            }
        };
        struct float_power { NEOS_NUMPY_INEXACT template <typename T> static T apply(T a, T b) { return power::apply(a, b); } };
        struct maximum
        {
            NEOS_NUMPY_ALL
            static constexpr simd::op simd = simd::op::Maximum;
            static constexpr int extremum = 1;
            template <typename T> static T apply(T a, T b)
            {
                if constexpr (is_complex_v<T>) { if (a != a) return a; if (b != b) return b; return complex_less(a, b) ? b : a; }
                else return baseline::kernel_ops::maximum::apply(a, b);
            }
        };
        struct minimum
        {
            NEOS_NUMPY_ALL
            static constexpr simd::op simd = simd::op::Minimum;
            static constexpr int extremum = -1;
            template <typename T> static T apply(T a, T b)
            {
                if constexpr (is_complex_v<T>) { if (a != a) return a; if (b != b) return b; return complex_less(b, a) ? b : a; }
                else return baseline::kernel_ops::minimum::apply(a, b);
            }
        };
        struct fmax
        {
            NEOS_NUMPY_ALL
            static constexpr simd::op simd = simd::op::Fmax;
            static constexpr int extremum = 1;
            template <typename T> static T apply(T a, T b)
            {
                if constexpr (is_complex_v<T>) { if (b != b) return a; if (a != a) return b; return complex_less(a, b) ? b : a; }
                else return baseline::kernel_ops::fmax::apply(a, b);
            }
        };
        struct fmin
        {
            NEOS_NUMPY_ALL
            static constexpr simd::op simd = simd::op::Fmin;
            static constexpr int extremum = -1;
            template <typename T> static T apply(T a, T b)
            {
                if constexpr (is_complex_v<T>) { if (b != b) return a; if (a != a) return b; return complex_less(b, a) ? b : a; }
                else return baseline::kernel_ops::fmin::apply(a, b);
            }
        };
        struct equal { NEOS_NUMPY_ALL template <typename T> static bool apply(T a, T b) { return a == b; } };
        struct not_equal { NEOS_NUMPY_ALL template <typename T> static bool apply(T a, T b) { return a != b; } };
        struct less { NEOS_NUMPY_ALL template <typename T> static bool apply(T a, T b) { if constexpr (is_complex_v<T>) return complex_less(a, b); else return a < b; } };
        struct less_equal { NEOS_NUMPY_ALL template <typename T> static bool apply(T a, T b) { if constexpr (is_complex_v<T>) return complex_less(a, b) || a == b; else return a <= b; } };
        struct greater { NEOS_NUMPY_ALL template <typename T> static bool apply(T a, T b) { if constexpr (is_complex_v<T>) return complex_less(b, a); else return a > b; } };
        struct greater_equal { NEOS_NUMPY_ALL template <typename T> static bool apply(T a, T b) { if constexpr (is_complex_v<T>) return complex_less(b, a) || a == b; else return a >= b; } };
        struct logical_and { NEOS_NUMPY_ALL template <typename T> static bool apply(T a, T b) { return cast<bool>(a) && cast<bool>(b); } };
        struct logical_or { NEOS_NUMPY_ALL template <typename T> static bool apply(T a, T b) { return cast<bool>(a) || cast<bool>(b); } };
        struct logical_xor { NEOS_NUMPY_ALL template <typename T> static bool apply(T a, T b) { return cast<bool>(a) != cast<bool>(b); } };
        struct bitwise_and { template <typename T> static constexpr bool supports = std::is_integral_v<T>; template <typename T> static T apply(T a, T b) { if constexpr (is_bool_v<T>) return a && b; else return static_cast<T>(a & b); } };
        struct bitwise_or { template <typename T> static constexpr bool supports = std::is_integral_v<T>; template <typename T> static T apply(T a, T b) { if constexpr (is_bool_v<T>) return a || b; else return static_cast<T>(a | b); } };
        struct bitwise_xor { template <typename T> static constexpr bool supports = std::is_integral_v<T>; template <typename T> static T apply(T a, T b) { if constexpr (is_bool_v<T>) return a != b; else return static_cast<T>(a ^ b); } };
        struct left_shift
        {
            template <typename T> static constexpr bool supports = is_integral_v<T>;
            template <typename T> static T apply(T a, T b)
            {
                using U = std::make_unsigned_t<T>;
                if (static_cast<std::uint64_t>(b) >= sizeof(T) * 8u)
                    return 0;
                return static_cast<T>(static_cast<U>(a) << static_cast<U>(b));
            }
        };
        struct right_shift
        {
            template <typename T> static constexpr bool supports = is_integral_v<T>;
            template <typename T> static T apply(T a, T b)
            {
                if (static_cast<std::uint64_t>(b) >= sizeof(T) * 8u)
                {
                    if constexpr (std::is_signed_v<T>) return a < 0 ? T(-1) : T(0);
                    else return 0;
                }
                return static_cast<T>(a >> b);
            }
        };
        struct arctan2 { NEOS_NUMPY_FLOATING template <typename T> static T apply(T a, T b) { return std::atan2(a, b); } };
        struct hypot { NEOS_NUMPY_FLOATING template <typename T> static T apply(T a, T b) { return std::hypot(a, b); } };
        struct copysign { NEOS_NUMPY_FLOATING template <typename T> static T apply(T a, T b) { return std::copysign(a, b); } };
        struct nextafter { NEOS_NUMPY_FLOATING template <typename T> static T apply(T a, T b) { return std::nextafter(a, b); } };
        struct logaddexp
        {
            NEOS_NUMPY_FLOATING
            template <typename T> static T apply(T x, T y)
            {
                if (x == y)
                    return x + std::log(T(2)); // (handles infinities of the same sign)
                T const tmp = x - y;
                if (tmp > 0) return x + std::log1p(std::exp(-tmp));
                if (tmp <= 0) return y + std::log1p(std::exp(tmp));
                return tmp; // (NaN)
            }
        };
        struct logaddexp2
        {
            NEOS_NUMPY_FLOATING
            template <typename T> static T apply(T x, T y)
            {
                if (x == y)
                    return x + T(1);
                T const tmp = x - y;
                T const ln2 = T(0.693147180559945309417232121458176568L);
                if (tmp > 0) return x + std::log1p(std::exp2(-tmp)) / ln2;
                if (tmp <= 0) return y + std::log1p(std::exp2(tmp)) / ln2;
                return tmp;
            }
        };
        struct heaviside
        {
            NEOS_NUMPY_FLOATING
            template <typename T> static T apply(T x, T h0)
            {
                if (std::isnan(x)) return x;
                if (x == 0) return h0;
                return x < 0 ? T(0) : T(1);
            }
        };
        struct gcd
        {
            template <typename T> static constexpr bool supports = is_integral_v<T>;
            template <typename T> static T apply(T a, T b)
            {
                using U = std::make_unsigned_t<T>;
                U x = a < 0 ? static_cast<U>(0u - static_cast<U>(a)) : static_cast<U>(a);
                U y = b < 0 ? static_cast<U>(0u - static_cast<U>(b)) : static_cast<U>(b);
                while (y != 0u) { U const t = static_cast<U>(x % y); x = y; y = t; }
                return static_cast<T>(x);
            }
        };
        struct lcm
        {
            template <typename T> static constexpr bool supports = is_integral_v<T>;
            template <typename T> static T apply(T a, T b)
            {
                T const g = gcd::apply(a, b);
                if (g == 0) return 0;
                T r = static_cast<T>((a / g) * b);
                if constexpr (std::is_signed_v<T>) if (r < 0) r = static_cast<T>(-r);
                return r;
            }
        };

        // (unary)
        struct negative { NEOS_NUMPY_ALL template <typename T> static T apply(T a) { if constexpr (is_bool_v<T>) return !a; else if constexpr (std::is_unsigned_v<T>) return static_cast<T>(0u - a); else return static_cast<T>(-a); } };
        struct positive { NEOS_NUMPY_ALL template <typename T> static T apply(T a) { return a; } };
        struct absolute
        {
            NEOS_NUMPY_ALL
            template <typename T> static auto apply(T a)
            {
                if constexpr (is_complex_v<T>) return std::hypot(a.real(), a.imag());
                else if constexpr (is_bool_v<T> || std::is_unsigned_v<T>) return a;
                else if constexpr (std::is_floating_point_v<T>) return std::fabs(a);
                else return static_cast<T>(a < 0 ? -a : a);
            }
        };
        struct sign
        {
            NEOS_NUMPY_ALL
            template <typename T> static T apply(T a)
            {
                if constexpr (is_complex_v<T>)
                {
                    auto const m = std::hypot(a.real(), a.imag());
                    if (a.real() != a.real() || a.imag() != a.imag()) return T(std::numeric_limits<typename T::value_type>::quiet_NaN(), std::numeric_limits<typename T::value_type>::quiet_NaN());
                    if (m == 0) return T(0);
                    if (std::isinf(m))
                    {
                        // (infinities: the direction of the infinite parts)
                        typename T::value_type re = std::isinf(a.real()) ? std::copysign(typename T::value_type(1), a.real()) : 0;
                        typename T::value_type im = std::isinf(a.imag()) ? std::copysign(typename T::value_type(1), a.imag()) : 0;
                        auto const n = std::hypot(re, im);
                        return T(re / n, im / n);
                    }
                    return T(a.real() / m, a.imag() / m);
                }
                else if constexpr (std::is_floating_point_v<T>) return std::isnan(a) ? a : a > 0 ? T(1) : a < 0 ? T(-1) : T(0);
                else if constexpr (is_bool_v<T>) return a;
                else if constexpr (std::is_unsigned_v<T>) return a > 0 ? 1 : 0;
                else return static_cast<T>(a > 0 ? 1 : a < 0 ? -1 : 0);
            }
        };
        struct square { NEOS_NUMPY_ALL template <typename T> static T apply(T a) { if constexpr (is_bool_v<T>) return a; else return static_cast<T>(a * a); } };
        struct reciprocal
        {
            NEOS_NUMPY_ALL
            template <typename T> static T apply(T a)
            {
                if constexpr (is_bool_v<T>) return a;
                else if constexpr (std::is_integral_v<T>)
                {
                    if (a == 0) return std::numeric_limits<T>::min();
                    return static_cast<T>(1 / a);
                }
                else return T(1) / a;
            }
        };
        struct conjugate { NEOS_NUMPY_ALL template <typename T> static T apply(T a) { if constexpr (is_complex_v<T>) return std::conj(a); else return a; } };
        struct logical_not { NEOS_NUMPY_ALL template <typename T> static bool apply(T a) { return !cast<bool>(a); } };
        struct invert { template <typename T> static constexpr bool supports = std::is_integral_v<T>; template <typename T> static T apply(T a) { if constexpr (is_bool_v<T>) return !a; else return static_cast<T>(~a); } };
        struct isnan { NEOS_NUMPY_ALL template <typename T> static bool apply(T a) { if constexpr (is_complex_v<T>) return std::isnan(a.real()) || std::isnan(a.imag()); else if constexpr (std::is_floating_point_v<T>) return std::isnan(a); else return false; } };
        struct isinf { NEOS_NUMPY_ALL template <typename T> static bool apply(T a) { if constexpr (is_complex_v<T>) return std::isinf(a.real()) || std::isinf(a.imag()); else if constexpr (std::is_floating_point_v<T>) return std::isinf(a); else return false; } };
        struct isfinite { NEOS_NUMPY_ALL template <typename T> static bool apply(T a) { if constexpr (is_complex_v<T>) return std::isfinite(a.real()) && std::isfinite(a.imag()); else if constexpr (std::is_floating_point_v<T>) return std::isfinite(a); else return true; } };
        struct signbit { NEOS_NUMPY_REAL template <typename T> static bool apply(T a) { if constexpr (std::is_floating_point_v<T>) return std::signbit(a); else if constexpr (std::is_signed_v<T>) return a < 0; else return false; } };
        struct floor { NEOS_NUMPY_REAL template <typename T> static T apply(T a) { if constexpr (std::is_floating_point_v<T>) return std::floor(a); else return a; } };
        struct ceil { NEOS_NUMPY_REAL template <typename T> static T apply(T a) { if constexpr (std::is_floating_point_v<T>) return std::ceil(a); else return a; } };
        struct trunc { NEOS_NUMPY_REAL template <typename T> static T apply(T a) { if constexpr (std::is_floating_point_v<T>) return std::trunc(a); else return a; } };
        struct rint
        {
            NEOS_NUMPY_INEXACT
            template <typename T> static T apply(T a)
            {
                if constexpr (is_complex_v<T>) return T(std::nearbyint(a.real()), std::nearbyint(a.imag()));
                else return std::nearbyint(a);
            }
        };
        struct spacing
        {
            NEOS_NUMPY_FLOATING
            template <typename T> static T apply(T a)
            {
                if (std::isnan(a)) return a;
                if (std::isinf(a)) return std::numeric_limits<T>::quiet_NaN();
                if (a < 0) return std::nextafter(a, -std::numeric_limits<T>::infinity()) - a;
                return std::nextafter(a, std::numeric_limits<T>::infinity()) - a;
            }
        };

#define NEOS_NUMPY_INEXACT_FUNCTION(NAME, REAL, COMPLEX) \
        struct NAME { NEOS_NUMPY_INEXACT template <typename T> static T apply(T a) { if constexpr (is_complex_v<T>) return COMPLEX; else return REAL; } };
#define NEOS_NUMPY_REAL_FUNCTION(NAME, REAL) \
        struct NAME { NEOS_NUMPY_FLOATING template <typename T> static T apply(T a) { return REAL; } };

        NEOS_NUMPY_INEXACT_FUNCTION(sqrt, std::sqrt(a), std::sqrt(a))
        NEOS_NUMPY_INEXACT_FUNCTION(exp, std::exp(a), std::exp(a))
        NEOS_NUMPY_INEXACT_FUNCTION(log, std::log(a), std::log(a))
        NEOS_NUMPY_INEXACT_FUNCTION(log10, std::log10(a), std::log10(a))
        NEOS_NUMPY_INEXACT_FUNCTION(sin, std::sin(a), std::sin(a))
        NEOS_NUMPY_INEXACT_FUNCTION(cos, std::cos(a), std::cos(a))
        NEOS_NUMPY_INEXACT_FUNCTION(tan, std::tan(a), std::tan(a))
        NEOS_NUMPY_INEXACT_FUNCTION(arcsin, std::asin(a), std::asin(a))
        NEOS_NUMPY_INEXACT_FUNCTION(arccos, std::acos(a), std::acos(a))
        NEOS_NUMPY_INEXACT_FUNCTION(arctan, std::atan(a), std::atan(a))
        NEOS_NUMPY_INEXACT_FUNCTION(sinh, std::sinh(a), std::sinh(a))
        NEOS_NUMPY_INEXACT_FUNCTION(cosh, std::cosh(a), std::cosh(a))
        NEOS_NUMPY_INEXACT_FUNCTION(tanh, std::tanh(a), std::tanh(a))
        NEOS_NUMPY_INEXACT_FUNCTION(arcsinh, std::asinh(a), std::asinh(a))
        NEOS_NUMPY_INEXACT_FUNCTION(arccosh, std::acosh(a), std::acosh(a))
        NEOS_NUMPY_INEXACT_FUNCTION(arctanh, std::atanh(a), std::atanh(a))
        NEOS_NUMPY_INEXACT_FUNCTION(expm1, std::expm1(a), std::exp(a) - T(1))
        NEOS_NUMPY_INEXACT_FUNCTION(log1p, std::log1p(a), std::log(T(1) + a))
        NEOS_NUMPY_INEXACT_FUNCTION(exp2, std::exp2(a), std::pow(T(2), a))
        NEOS_NUMPY_INEXACT_FUNCTION(log2, std::log2(a), std::log(a) / std::log(typename T::value_type(2)))
        NEOS_NUMPY_REAL_FUNCTION(cbrt, std::cbrt(a))
        NEOS_NUMPY_REAL_FUNCTION(fabs, std::fabs(a))
        NEOS_NUMPY_REAL_FUNCTION(deg2rad, a * (T(3.14159265358979323846264338327950288L) / T(180)))
        NEOS_NUMPY_REAL_FUNCTION(rad2deg, a * (T(180) / T(3.14159265358979323846264338327950288L)))
    }

    // --- loops

    namespace
    {
        // (a C++ type that is the type of a dtype)
        template <typename T> constexpr bool is_ctype_v = std::is_same_v<T, bool> || std::is_same_v<T, std::int8_t> || std::is_same_v<T, std::uint8_t> ||
            std::is_same_v<T, std::int16_t> || std::is_same_v<T, std::uint16_t> || std::is_same_v<T, std::int32_t> || std::is_same_v<T, std::uint32_t> ||
            std::is_same_v<T, std::int64_t> || std::is_same_v<T, std::uint64_t> || std::is_same_v<T, float> || std::is_same_v<T, double> ||
            std::is_same_v<T, std::complex<float>> || std::is_same_v<T, std::complex<double>>;

        // (the baseline kernels: kernels.hpp)
        using baseline::unit;
        using baseline::typed_binary_loop;

        // (the kernel operation of an operation)
        template <typename Op>
        constexpr simd::op simd_op()
        {
            if constexpr (requires { Op::simd; })
                return Op::simd;
            else
                return simd::op::None;
        }

        template <typename Op, typename T, typename R>
        void typed_unary_loop(std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
        {
            auto p0 = aPointers[0];
            auto p1 = aPointers[1];
            if (unit<T>(p0, aStrides[0]) && unit<R>(p1, aStrides[1]))
            {
                auto const* const a = reinterpret_cast<T const*>(p0);
                auto* const r = reinterpret_cast<R*>(p1);
                for (std::int64_t i = 0; i < aCount; ++i)
                    r[i] = Op::apply(a[i]);
                return;
            }
            for (std::int64_t i = 0; i < aCount; ++i, p0 += aStrides[0], p1 += aStrides[1])
            {
                T a;
                std::memcpy(&a, p0, sizeof(T));
                R const r = Op::apply(a);
                std::memcpy(p1, &r, sizeof(R));
            }
        }

        template <typename Op>
        void binary_loop(dtype aLoop, dtype aOut, dtype aIn0, dtype aIn1, std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
        {
            dispatch(aLoop, [&](auto tag)
            {
                using T = decltype(tag);
                using C = compute_t<T>;
                if constexpr (Op::template supports<C>)
                {
                    if constexpr (std::is_same_v<T, C>)
                    {
                        using R = std::decay_t<decltype(Op::apply(std::declval<C>(), std::declval<C>()))>;
                        if constexpr (is_ctype_v<R>)
                            if (aOut == dtype_of_ctype<R>() && (aIn0 == aLoop || aStrides[0] == 0) && (aIn1 == aLoop || aStrides[1] == 0))
                            {
                                // (a scalar operand of another type: converted once)
                                T scalars[2];
                                unsigned char* pointers[3] = { aPointers[0], aPointers[1], aPointers[2] };
                                if (aIn0 != aLoop)
                                {
                                    scalars[0] = read_as<T>(aPointers[0], aIn0);
                                    pointers[0] = reinterpret_cast<unsigned char*>(&scalars[0]);
                                }
                                if (aIn1 != aLoop)
                                {
                                    scalars[1] = read_as<T>(aPointers[1], aIn1);
                                    pointers[1] = reinterpret_cast<unsigned char*>(&scalars[1]);
                                }
                                // (the kernel of the CPU's instruction set, or the baseline's)
                                if constexpr (simd_op<Op>() != simd::op::None && simd::type_of<T>().has_value() && std::is_same_v<T, R>)
                                    if (simd::binary(simd_op<Op>(), *simd::type_of<T>(), aCount, pointers, aStrides))
                                        return;
                                typed_binary_loop<Op, T, R>(aCount, pointers, aStrides);
                                return;
                            }
                    }
                    auto p0 = aPointers[0];
                    auto p1 = aPointers[1];
                    auto p2 = aPointers[2];
                    for (std::int64_t i = 0; i < aCount; ++i, p0 += aStrides[0], p1 += aStrides[1], p2 += aStrides[2])
                    {
                        C const a = cast<C>(read_as<T>(p0, aIn0));
                        C const b = cast<C>(read_as<T>(p1, aIn1));
                        auto const r = Op::apply(a, b);
                        if constexpr (is_half_v<T> && std::is_same_v<std::decay_t<decltype(r)>, float>)
                            write_as(p2, aOut, half{ r });
                        else
                            write_as(p2, aOut, r);
                    }
                }
            });
        }

        template <typename Op>
        void unary_loop(dtype aLoop, dtype aOut, dtype aIn0, std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
        {
            dispatch(aLoop, [&](auto tag)
            {
                using T = decltype(tag);
                using C = compute_t<T>;
                if constexpr (Op::template supports<C>)
                {
                    if constexpr (std::is_same_v<T, C>)
                    {
                        using R = std::decay_t<decltype(Op::apply(std::declval<C>()))>;
                        if constexpr (is_ctype_v<R>)
                            if (aIn0 == aLoop && aOut == dtype_of_ctype<R>())
                            {
                                typed_unary_loop<Op, T, R>(aCount, aPointers, aStrides);
                                return;
                            }
                    }
                    auto p0 = aPointers[0];
                    auto p1 = aPointers[1];
                    for (std::int64_t i = 0; i < aCount; ++i, p0 += aStrides[0], p1 += aStrides[1])
                    {
                        C const a = cast<C>(read_as<T>(p0, aIn0));
                        auto const r = Op::apply(a);
                        if constexpr (is_half_v<T> && std::is_same_v<std::decay_t<decltype(r)>, float>)
                            write_as(p1, aOut, half{ r });
                        else
                            write_as(p1, aOut, r);
                    }
                }
            });
        }

        // (supported loop types of an operation)
        template <typename Op>
        bool supported(dtype aLoop)
        {
            return dispatch(aLoop, [&](auto tag) -> bool
            {
                using C = compute_t<decltype(tag)>;
                return Op::template supports<C>;
            });
        }

        std::string dtype_class(dtype aType)
        {
            static char const* const sNames[] = { "BoolDType", "Int8DType", "UInt8DType", "Int16DType", "UInt16DType", "Int32DType", "UInt32DType",
                "Int64DType", "UInt64DType", "Float16DType", "Float32DType", "Float64DType", "Complex64DType", "Complex128DType" };
            return std::string{ "<class 'numpy.dtypes." } + sNames[static_cast<int>(aType)] + "'>";
        }

        std::nullptr_t not_supported(ufunc const& aUfunc)
        {
            return error("UFuncTypeError", std::string{ "ufunc '" } + aUfunc.name + "' not supported for the input types, and the inputs could not be safely coerced to any supported types according to the casting rule ''safe''");
        }

        std::nullptr_t no_loop(ufunc const& aUfunc, std::vector<dtype> const& aTypes)
        {
            std::string types;
            for (std::size_t i = 0u; i < aTypes.size(); ++i)
                types += (i != 0u ? " " : "") + dtype_class(aTypes[i]);
            return error("UFuncTypeError", std::string{ "ufunc '" } + aUfunc.name + "' did not contain a loop with signature matching types " + types + " -> None");
        }
    }

#define NEOS_NUMPY_BINARY(OP) \
    [](dtype l, dtype o, dtype i0, dtype i1, std::int64_t n, unsigned char** p, std::int64_t const* s) { binary_loop<ops::OP>(l, o, i0, i1, n, p, s); }, \
    [](dtype l) { return supported<ops::OP>(l); }
#define NEOS_NUMPY_UNARY(OP) \
    [](dtype l, dtype o, dtype i0, std::int64_t n, unsigned char** p, std::int64_t const* s) { unary_loop<ops::OP>(l, o, i0, n, p, s); }, \
    [](dtype l) { return supported<ops::OP>(l); }

    namespace
    {
        ufunc const sUfuncs[] =
        {
            { "add", 2, category::Arithmetic, 0.0, NEOS_NUMPY_BINARY(add), nullptr, nullptr },
            { "subtract", 2, category::Arithmetic, std::nullopt, NEOS_NUMPY_BINARY(subtract), nullptr, nullptr },
            { "multiply", 2, category::Arithmetic, 1.0, NEOS_NUMPY_BINARY(multiply), nullptr, nullptr },
            { "divide", 2, category::TrueDivide, std::nullopt, NEOS_NUMPY_BINARY(true_divide), nullptr, nullptr },
            { "true_divide", 2, category::TrueDivide, std::nullopt, NEOS_NUMPY_BINARY(true_divide), nullptr, nullptr },
            { "floor_divide", 2, category::IntegerDivide, std::nullopt, NEOS_NUMPY_BINARY(floor_divide), nullptr, nullptr },
            { "remainder", 2, category::IntegerDivide, std::nullopt, NEOS_NUMPY_BINARY(remainder), nullptr, nullptr },
            { "mod", 2, category::IntegerDivide, std::nullopt, NEOS_NUMPY_BINARY(remainder), nullptr, nullptr },
            { "fmod", 2, category::IntegerDivide, std::nullopt, NEOS_NUMPY_BINARY(fmod), nullptr, nullptr },
            { "power", 2, category::Power, std::nullopt, NEOS_NUMPY_BINARY(power), nullptr, nullptr },
            { "float_power", 2, category::FloatPower, std::nullopt, NEOS_NUMPY_BINARY(float_power), nullptr, nullptr },
            { "maximum", 2, category::Arithmetic, std::nullopt, NEOS_NUMPY_BINARY(maximum), nullptr, nullptr },
            { "minimum", 2, category::Arithmetic, std::nullopt, NEOS_NUMPY_BINARY(minimum), nullptr, nullptr },
            { "fmax", 2, category::Arithmetic, std::nullopt, NEOS_NUMPY_BINARY(fmax), nullptr, nullptr },
            { "fmin", 2, category::Arithmetic, std::nullopt, NEOS_NUMPY_BINARY(fmin), nullptr, nullptr },
            { "equal", 2, category::Comparison, std::nullopt, NEOS_NUMPY_BINARY(equal), nullptr, nullptr },
            { "not_equal", 2, category::Comparison, std::nullopt, NEOS_NUMPY_BINARY(not_equal), nullptr, nullptr },
            { "less", 2, category::Comparison, std::nullopt, NEOS_NUMPY_BINARY(less), nullptr, nullptr },
            { "less_equal", 2, category::Comparison, std::nullopt, NEOS_NUMPY_BINARY(less_equal), nullptr, nullptr },
            { "greater", 2, category::Comparison, std::nullopt, NEOS_NUMPY_BINARY(greater), nullptr, nullptr },
            { "greater_equal", 2, category::Comparison, std::nullopt, NEOS_NUMPY_BINARY(greater_equal), nullptr, nullptr },
            { "logical_and", 2, category::Logical, 1.0, NEOS_NUMPY_BINARY(logical_and), nullptr, nullptr },
            { "logical_or", 2, category::Logical, 0.0, NEOS_NUMPY_BINARY(logical_or), nullptr, nullptr },
            { "logical_xor", 2, category::Logical, 0.0, NEOS_NUMPY_BINARY(logical_xor), nullptr, nullptr },
            { "bitwise_and", 2, category::Bitwise, -1.0, NEOS_NUMPY_BINARY(bitwise_and), nullptr, nullptr },
            { "bitwise_or", 2, category::Bitwise, 0.0, NEOS_NUMPY_BINARY(bitwise_or), nullptr, nullptr },
            { "bitwise_xor", 2, category::Bitwise, 0.0, NEOS_NUMPY_BINARY(bitwise_xor), nullptr, nullptr },
            { "left_shift", 2, category::Shift, std::nullopt, NEOS_NUMPY_BINARY(left_shift), nullptr, nullptr },
            { "right_shift", 2, category::Shift, std::nullopt, NEOS_NUMPY_BINARY(right_shift), nullptr, nullptr },
            { "arctan2", 2, category::FloatBinary, std::nullopt, NEOS_NUMPY_BINARY(arctan2), nullptr, nullptr },
            { "hypot", 2, category::FloatBinary, std::nullopt, NEOS_NUMPY_BINARY(hypot), nullptr, nullptr },
            { "copysign", 2, category::FloatBinary, std::nullopt, NEOS_NUMPY_BINARY(copysign), nullptr, nullptr },
            { "nextafter", 2, category::FloatBinary, std::nullopt, NEOS_NUMPY_BINARY(nextafter), nullptr, nullptr },
            { "logaddexp", 2, category::FloatBinary, std::nullopt, NEOS_NUMPY_BINARY(logaddexp), nullptr, nullptr },
            { "logaddexp2", 2, category::FloatBinary, std::nullopt, NEOS_NUMPY_BINARY(logaddexp2), nullptr, nullptr },
            { "heaviside", 2, category::FloatBinary, std::nullopt, NEOS_NUMPY_BINARY(heaviside), nullptr, nullptr },
            { "gcd", 2, category::IntegerOnly, 0.0, NEOS_NUMPY_BINARY(gcd), nullptr, nullptr },
            { "lcm", 2, category::IntegerOnly, std::nullopt, NEOS_NUMPY_BINARY(lcm), nullptr, nullptr },
            { "negative", 1, category::Negative, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(negative) },
            { "positive", 1, category::SameNoBool, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(positive) },
            { "absolute", 1, category::Absolute, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(absolute) },
            { "fabs", 1, category::FloatUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(fabs) },
            { "sign", 1, category::SameNoBool, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(sign) },
            { "square", 1, category::SameBoolInt8, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(square) },
            { "reciprocal", 1, category::SameBoolInt8, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(reciprocal) },
            { "conjugate", 1, category::Same, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(conjugate) },
            { "conj", 1, category::Same, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(conjugate) },
            { "logical_not", 1, category::Predicate, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(logical_not) },
            { "invert", 1, category::Invert, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(invert) },
            { "bitwise_not", 1, category::Invert, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(invert) },
            { "bitwise_invert", 1, category::Invert, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(invert) },
            { "isnan", 1, category::Predicate, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(isnan) },
            { "isinf", 1, category::Predicate, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(isinf) },
            { "isfinite", 1, category::Predicate, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(isfinite) },
            { "signbit", 1, category::RealPredicate, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(signbit) },
            { "floor", 1, category::Rounding, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(floor) },
            { "ceil", 1, category::Rounding, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(ceil) },
            { "trunc", 1, category::Rounding, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(trunc) },
            { "rint", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(rint) },
            { "spacing", 1, category::FloatUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(spacing) },
            { "sqrt", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(sqrt) },
            { "exp", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(exp) },
            { "exp2", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(exp2) },
            { "expm1", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(expm1) },
            { "log", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(log) },
            { "log2", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(log2) },
            { "log10", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(log10) },
            { "log1p", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(log1p) },
            { "sin", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(sin) },
            { "cos", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(cos) },
            { "tan", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(tan) },
            { "arcsin", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(arcsin) },
            { "arccos", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(arccos) },
            { "arctan", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(arctan) },
            { "sinh", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(sinh) },
            { "cosh", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(cosh) },
            { "tanh", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(tanh) },
            { "arcsinh", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(arcsinh) },
            { "arccosh", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(arccosh) },
            { "arctanh", 1, category::InexactUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(arctanh) },
            { "cbrt", 1, category::FloatUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(cbrt) },
            { "deg2rad", 1, category::FloatUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(deg2rad) },
            { "radians", 1, category::FloatUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(deg2rad) },
            { "rad2deg", 1, category::FloatUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(rad2deg) },
            { "degrees", 1, category::FloatUnary, std::nullopt, nullptr, nullptr, NEOS_NUMPY_UNARY(rad2deg) }
        };
    }

    ufunc const* find_ufunc(std::string_view aName)
    {
        for (auto const& u : sUfuncs)
            if (aName == u.name)
                return &u;
        return nullptr;
    }

    std::vector<ufunc const*> all_ufuncs()
    {
        std::vector<ufunc const*> result;
        for (auto const& u : sUfuncs)
            result.push_back(&u);
        return result;
    }

    // --- type resolution

    namespace
    {
        // the type of a Python scalar operand next to an operand of type aOther (NEP 50)
        dtype weak_type(int aKind, dtype aOther)
        {
            switch (aKind)
            {
            case NEOS_PY_BOOL:
                return aOther;
            case NEOS_PY_INT:
                return is_bool(aOther) ? dtype::Int64 : aOther;
            case NEOS_PY_FLOAT:
                return (is_bool(aOther) || is_integer(aOther)) ? dtype::Float64 : aOther;
            default:
                if (is_bool(aOther) || is_integer(aOther)) return dtype::Complex128;
                if (aOther == dtype::Float16 || aOther == dtype::Float32) return dtype::Complex64;
                if (aOther == dtype::Float64) return dtype::Complex128;
                return aOther;
            }
        }

        // (a Python integer must fit the integer type it is converted to)
        bool check_weak_integer(operand const& aOperand, dtype aType)
        {
            if (!aOperand.weak || aOperand.weakKind != NEOS_PY_INT || !is_integer(aType))
                return true;
            auto const value = load(aOperand.array->pointer(), aOperand.array->type).i;
            auto const& n = info(aType);
            bool fits;
            if (aType == dtype::Int64)
                fits = true;
            else if (aType == dtype::UInt64)
                fits = value >= 0;
            else
            {
                long long const bits = static_cast<long long>(n.itemsize) * 8;
                long long const low = is_signed(aType) ? -(1ll << (bits - 1)) : 0;
                long long const high = is_signed(aType) ? (1ll << (bits - 1)) - 1 : (1ll << bits) - 1;
                fits = value >= low && value <= high;
            }
            if (!fits)
            {
                error("OverflowError", "Python integer " + std::to_string(value) + " out of bounds for " + n.name);
                return false;
            }
            return true;
        }
    }

    bool resolve(ufunc const& aUfunc, std::vector<dtype> const& aInputs, dtype& aLoop, dtype& aOut)
    {
        auto const supported = [&](dtype t) { return aUfunc.nin == 2 ? aUfunc.binarySupports(t) : aUfunc.unarySupports(t); };
        dtype const common = aInputs.size() == 2u ? promote(aInputs[0], aInputs[1]) : aInputs[0];
        aLoop = common;
        aOut = common;
        switch (aUfunc.kind)
        {
        case category::Arithmetic:
            if (std::string_view{ aUfunc.name } == "subtract" && is_bool(common))
            {
                error("TypeError", "numpy boolean subtract, the `-` operator, is not supported, use the bitwise_xor, the `^` operator, or the logical_xor function instead.");
                return false;
            }
            break;
        case category::TrueDivide:
            if (is_bool(common) || is_integer(common))
                aLoop = aOut = dtype::Float64;
            break;
        case category::IntegerDivide:
        case category::Power:
            if (is_bool(common))
                aLoop = aOut = dtype::Int8;
            if (is_complex(common) && aUfunc.kind == category::IntegerDivide)
            {
                not_supported(aUfunc);
                return false;
            }
            break;
        case category::FloatPower:
            aLoop = aOut = is_complex(common) ? dtype::Complex128 : dtype::Float64;
            break;
        case category::Comparison:
        case category::Logical:
            aOut = dtype::Bool;
            break;
        case category::Bitwise:
            if (!is_bool(common) && !is_integer(common))
            {
                not_supported(aUfunc);
                return false;
            }
            break;
        case category::Shift:
            if (is_bool(common))
                aLoop = aOut = dtype::Int8;
            if (!is_integer(aLoop))
            {
                not_supported(aUfunc);
                return false;
            }
            break;
        case category::IntegerOnly:
            if (is_bool(common))
                aLoop = aOut = dtype::Int8;
            if (!is_integer(aLoop))
            {
                not_supported(aUfunc);
                return false;
            }
            break;
        case category::FloatBinary:
        case category::FloatUnary:
            if (!is_inexact(common))
                aLoop = aOut = smallest_float_for(common);
            if (is_complex(common))
            {
                not_supported(aUfunc);
                return false;
            }
            break;
        case category::InexactUnary:
            if (!is_inexact(common))
                aLoop = aOut = smallest_float_for(common);
            break;
        case category::Negative:
            if (is_bool(common))
            {
                error("TypeError", "The numpy boolean negative, the `-` operator, is not supported, use the `~` operator or the logical_not function instead.");
                return false;
            }
            break;
        case category::SameNoBool:
            if (is_bool(common))
            {
                no_loop(aUfunc, aInputs);
                return false;
            }
            break;
        case category::SameBoolInt8:
            if (is_bool(common))
                aLoop = aOut = dtype::Int8;
            break;
        case category::Absolute:
            if (is_complex(common))
                aOut = real_for(common);
            break;
        case category::Predicate:
            aOut = dtype::Bool;
            break;
        case category::RealPredicate:
            aOut = dtype::Bool;
            if (is_complex(common))
            {
                not_supported(aUfunc);
                return false;
            }
            break;
        case category::Rounding:
            if (is_complex(common))
            {
                not_supported(aUfunc);
                return false;
            }
            break;
        case category::Invert:
            if (!is_bool(common) && !is_integer(common))
            {
                not_supported(aUfunc);
                return false;
            }
            break;
        case category::Same:
            break;
        }
        if (!supported(aLoop))
        {
            not_supported(aUfunc);
            return false;
        }
        return true;
    }

    namespace
    {
        bool check_output_cast(ufunc const& aUfunc, dtype aFrom, dtype aTo, std::string_view aCasting = "same_kind")
        {
            if (can_cast(aFrom, aTo, aCasting))
                return true;
            error("UFuncTypeError", std::string{ "Cannot cast ufunc '" } + aUfunc.name + "' output from dtype('" + info(aFrom).name + "') to dtype('" +
                info(aTo).name + "') with casting rule '" + std::string{ aCasting } + "'");
            return false;
        }
    }

    object* apply_binary(ufunc const& aUfunc, object* aLhs, object* aRhs, ndarray* aOut, std::optional<dtype> aType)
    {
        operand a, b;
        if (!to_operand(aLhs, a) || !to_operand(aRhs, b))
            return nullptr;
        dtype ta = a.array->type;
        dtype tb = b.array->type;
        if (a.weak && !b.weak)
            ta = weak_type(a.weakKind, tb);
        else if (b.weak && !a.weak)
            tb = weak_type(b.weakKind, ta);
        if (!check_weak_integer(a, ta) || !check_weak_integer(b, tb))
            return nullptr;
        dtype loop, out;
        if (aType)
        {
            loop = *aType;
            out = aUfunc.kind == category::Comparison || aUfunc.kind == category::Logical ? dtype::Bool : *aType;
            if (!(aUfunc.binarySupports(loop)))
                return not_supported(aUfunc);
        }
        else if (!resolve(aUfunc, { ta, tb }, loop, out))
            return nullptr;
        // integers to negative integer powers
        if (aUfunc.kind == category::Power && is_integer(loop))
        {
            bool negative = false;
            iterate(b.array->shape, { b.array->pointer() }, { b.array->strides }, [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
            {
                auto p = aPointers[0];
                for (std::int64_t i = 0; i < aCount; ++i, p += aStrides[0])
                    if (read_as<std::int64_t>(p, b.array->type) < 0 && is_signed(b.array->type))
                        negative = true;
            });
            if (negative)
                return error("ValueError", "Integers to negative integer powers are not allowed.");
        }
        shape_t shape;
        if (!broadcast_shapes({ &a.array->shape, &b.array->shape }, shape))
            return nullptr;
        ndarray* result = aOut;
        if (result != nullptr)
        {
            if (!check_output_cast(aUfunc, out, result->type))
                return nullptr;
            shape_t check;
            if (!broadcast_shapes({ &shape, &result->shape }, check) || check != result->shape)
                return error("ValueError", "non-broadcastable output operand with shape " + shape_repr(result->shape) + " doesn't match the broadcast shape " + shape_repr(shape));
            shape = result->shape;
            ctx->retain(as_object(result));
        }
        else
            result = new_unfilled_array(out, shape);
        // (an output that overlaps an input: computed into a temporary first)
        bool const overlap = aOut != nullptr && (aOut->data == a.array->data || aOut->data == b.array->data);
        ndarray* target = overlap ? new_array(result->type, shape) : result;
        auto const sa = broadcast_strides(*a.array, shape);
        auto const sb = broadcast_strides(*b.array, shape);
        auto const ta0 = a.array->type;
        auto const tb0 = b.array->type;
        auto const resultType = target->type;
        iterate(shape, { a.array->pointer(), b.array->pointer(), target->pointer() }, { sa, sb, target->strides },
            [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
            {
                if (resultType == out)
                    aUfunc.binary(loop, out, ta0, tb0, aCount, aPointers, aStrides);
                else
                {
                    // (computed as the loop's output type then cast)
                    std::vector<std::uint64_t> buffer(static_cast<std::size_t>(aCount) * 2u + 2u);
                    auto* temporary = reinterpret_cast<unsigned char*>(buffer.data());
                    unsigned char* pointers[3] = { aPointers[0], aPointers[1], temporary };
                    std::int64_t const strides[3] = { aStrides[0], aStrides[1], static_cast<std::int64_t>(info(out).itemsize) };
                    aUfunc.binary(loop, out, ta0, tb0, aCount, pointers, strides);
                    cast_elements(temporary, strides[2], out, aPointers[2], aStrides[2], resultType, aCount);
                }
            });
        if (overlap)
        {
            iterate(shape, { target->pointer(), result->pointer() }, { target->strides, result->strides },
                [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
                {
                    auto from = aPointers[0];
                    auto to = aPointers[1];
                    for (std::int64_t i = 0; i < aCount; ++i, from += aStrides[0], to += aStrides[1])
                        std::memcpy(to, from, info(result->type).itemsize);
                });
            ctx->release(as_object(target));
        }
        if (aOut != nullptr)
            return as_object(result);
        return result_object(result);
    }

    object* apply_unary(ufunc const& aUfunc, object* aOperand, ndarray* aOut, std::optional<dtype> aType)
    {
        operand a;
        if (!to_operand(aOperand, a))
            return nullptr;
        dtype loop, out;
        if (aType)
        {
            loop = *aType;
            out = aUfunc.kind == category::Predicate || aUfunc.kind == category::RealPredicate ? dtype::Bool :
                aUfunc.kind == category::Absolute && is_complex(loop) ? real_for(loop) : loop;
        }
        else if (!resolve(aUfunc, { a.array->type }, loop, out))
            return nullptr;
        auto shape = a.array->shape;
        ndarray* result = aOut;
        if (result != nullptr)
        {
            if (!check_output_cast(aUfunc, out, result->type))
                return nullptr;
            shape_t check;
            if (!broadcast_shapes({ &shape, &result->shape }, check) || check != result->shape)
                return error("ValueError", "non-broadcastable output operand with shape " + shape_repr(result->shape) + " doesn't match the broadcast shape " + shape_repr(shape));
            shape = result->shape;
            ctx->retain(as_object(result));
        }
        else
            result = new_unfilled_array(out, shape);
        auto const sa = broadcast_strides(*a.array, shape);
        auto const t0 = a.array->type;
        auto const resultType = result->type;
        iterate(shape, { a.array->pointer(), result->pointer() }, { sa, result->strides },
            [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
            {
                if (resultType == out)
                    aUfunc.unary(loop, out, t0, aCount, aPointers, aStrides);
                else
                {
                    std::vector<std::uint64_t> buffer(static_cast<std::size_t>(aCount) * 2u + 2u);
                    auto* temporary = reinterpret_cast<unsigned char*>(buffer.data());
                    unsigned char* pointers[2] = { aPointers[0], temporary };
                    std::int64_t const strides[2] = { aStrides[0], static_cast<std::int64_t>(info(out).itemsize) };
                    aUfunc.unary(loop, out, t0, aCount, pointers, strides);
                    cast_elements(temporary, strides[1], out, aPointers[1], aStrides[1], resultType, aCount);
                }
            });
        if (aOut != nullptr)
            return as_object(result);
        return result_object(result);
    }

    // --- operators

    object* binary_operator(int aOperation, object* aLhs, object* aRhs, bool aInplace)
    {
        static char const* const sNames[] = { "add", "subtract", "multiply", "true_divide", "floor_divide", "remainder", "power", "matmul",
            "left_shift", "right_shift", "bitwise_and", "bitwise_or", "bitwise_xor" };
        if (aOperation == NEOS_PY_MATMUL)
        {
            ref const args{ make_tuple({ (ctx->retain(aLhs), aLhs), (ctx->retain(aRhs), aRhs) }) };
            return call_function("matmul", args.get(), nullptr);
        }
        // (an operand that is not an array or a Python scalar or sequence: not ours)
        for (auto o : { aLhs, aRhs })
            if (!is_array(o) && !is_python_scalar(o) && !is_sequence(o))
                return ctx->not_implemented(ctx);
        auto const* u = find_ufunc(sNames[aOperation]);
        if (aInplace && is_ndarray(aLhs))
            return apply_binary(*u, aLhs, aRhs, as_array(aLhs));
        return apply_binary(*u, aLhs, aRhs);
    }

    object* unary_operator(int aOperation, object* aOperand)
    {
        static char const* const sNames[] = { "negative", "positive", "invert", "absolute" };
        return apply_unary(*find_ufunc(sNames[aOperation]), aOperand);
    }

    object* compare_operator(int aOperation, object* aLhs, object* aRhs)
    {
        static char const* const sNames[] = { "less", "less_equal", "equal", "not_equal", "greater", "greater_equal" };
        for (auto o : { aLhs, aRhs })
            if (!is_array(o) && !is_python_scalar(o) && !is_sequence(o))
                return ctx->not_implemented(ctx);
        return apply_binary(*find_ufunc(sNames[aOperation]), aLhs, aRhs);
    }

    // --- reductions

    namespace
    {
        // (NumPy's pairwise summation of complex numbers: n counts the real and imaginary parts)
        template <typename R>
        std::complex<R> pairwise_sum_complex(unsigned char const* aPointer, std::int64_t n, std::int64_t aStride, dtype aType)
        {
            auto const part = [&](std::int64_t i) -> R
            {
                auto const v = read_as<std::complex<R>>(aPointer + (i / 2) * aStride, aType);
                return (i % 2) == 0 ? v.real() : v.imag();
            };
            if (n < 8)
            {
                R rr = R(-0.0), ri = R(-0.0);
                for (std::int64_t i = 0; i < n; i += 2)
                {
                    rr += part(i);
                    ri += part(i + 1);
                }
                return { rr, ri };
            }
            if (n <= 128)
            {
                R r[8];
                for (int j = 0; j < 8; ++j)
                    r[j] = part(j);
                std::int64_t i = 8;
                for (; i < n - (n % 8); i += 8)
                    for (int j = 0; j < 8; ++j)
                        r[j] += part(i + j);
                R rr = (r[0] + r[2]) + (r[4] + r[6]);
                R ri = (r[1] + r[3]) + (r[5] + r[7]);
                for (; i < n; i += 2)
                {
                    rr += part(i);
                    ri += part(i + 1);
                }
                return { rr, ri };
            }
            std::int64_t n2 = n / 2;
            n2 -= n2 % 8;
            return pairwise_sum_complex<R>(aPointer, n2, aStride, aType) + pairwise_sum_complex<R>(aPointer + (n2 / 2) * aStride, n - n2, aStride, aType);
        }

        // (NumPy's pairwise summation)
        template <typename A, typename T, bool Direct = false>
        A pairwise_sum(unsigned char const* aPointer, std::int64_t n, std::int64_t aStride, dtype aType)
        {
            // (elements of the type T are loaded directly)
            if constexpr (!Direct)
                if (aType == dtype_of_ctype<T>())
                    return pairwise_sum<A, T, true>(aPointer, n, aStride, aType);
            auto const at = [&](std::int64_t i)
            {
                if constexpr (Direct)
                {
                    T value;
                    std::memcpy(&value, aPointer + i * aStride, sizeof(T));
                    return static_cast<A>(cast<compute_t<T>>(value));
                }
                else
                    return static_cast<A>(cast<compute_t<T>>(read_as<T>(aPointer + i * aStride, aType)));
            };
            if (n < 8)
            {
                A result = A(0);
                for (std::int64_t i = 0; i < n; ++i)
                    result += at(i);
                return result;
            }
            if (n <= 128)
            {
                A r[8];
                for (int j = 0; j < 8; ++j)
                    r[j] = at(j);
                std::int64_t i = 8;
                for (; i < n - (n % 8); i += 8)
                    for (int j = 0; j < 8; ++j)
                        r[j] += at(i + j);
                A result = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
                for (; i < n; ++i)
                    result += at(i);
                return result;
            }
            std::int64_t n2 = n / 2;
            n2 -= n2 % 8;
            return pairwise_sum<A, T, Direct>(aPointer, n2, aStride, aType) + pairwise_sum<A, T, Direct>(aPointer + n2 * aStride, n - n2, aStride, aType);
        }
    }

    object* reduce_array(ufunc const& aUfunc, ndarray* aArray, std::vector<std::size_t> const& aAxes, std::optional<dtype> aType, bool aKeepdims,
        object* aInitial, ndarray* aOut)
    {
        if (aUfunc.nin != 2)
            return error("ValueError", "reduce only supported for binary functions");
        auto const name = std::string_view{ aUfunc.name };
        // the type of the reduction
        dtype loop, out;
        if (aType)
        {
            loop = out = *aType;
            if (aUfunc.kind == category::Comparison || aUfunc.kind == category::Logical)
                out = dtype::Bool;
        }
        else
        {
            dtype t = aArray->type;
            if ((name == "add" || name == "multiply") && (is_bool(t) || is_integer(t)))
                t = is_unsigned(t) ? dtype::UInt64 : dtype::Int64;
            if (!resolve(aUfunc, { t, t }, loop, out))
                return nullptr;
        }
        if (aUfunc.kind == category::Logical)
            loop = dtype::Bool;
        auto const ndim = aArray->ndim();
        std::vector<bool> reduced(ndim, false);
        for (auto a : aAxes)
            reduced[a] = true;
        shape_t outShape, keptShape;
        for (std::size_t i = 0u; i < ndim; ++i)
        {
            if (!reduced[i])
                outShape.push_back(aArray->shape[i]);
            keptShape.push_back(reduced[i] ? 1 : aArray->shape[i]);
        }
        std::int64_t count = 1;
        for (auto a : aAxes)
            count *= aArray->shape[a];
        bool const hasInitial = aInitial != nullptr && kind(aInitial) != NEOS_PY_NONE;
        if (count == 0 && !aUfunc.identity && !hasInitial)
            return error("ValueError", std::string{ "zero-size array to reduction operation " } + aUfunc.name + " which has no identity");
        ndarray* result = nullptr;
        if (aOut != nullptr)
        {
            result = aOut;
            ctx->retain(as_object(result));
        }
        else
            result = new_array(out, aKeepdims ? keptShape : outShape);
        // (the outer positions: the result's elements; the inner: the reduced elements of each, in NumPy's order)
        shape_t outerShape, outerStrides, innerShape, innerStrides;
        auto const resultStrides = contiguous_strides(keptShape, info(result->type).itemsize);
        // (the strides of the result as the kept shape)
        shape_t keptResultStrides(ndim, 0);
        {
            std::size_t j = 0u;
            for (std::size_t i = 0u; i < ndim; ++i)
            {
                if (aKeepdims)
                    keptResultStrides[i] = reduced[i] ? 0 : result->strides[i];
                else if (!reduced[i])
                    keptResultStrides[i] = result->strides[j++];
            }
        }
        std::vector<std::size_t> outerAxes, innerAxes;
        for (std::size_t i = 0u; i < ndim; ++i)
            (reduced[i] ? innerAxes : outerAxes).push_back(i);
        // (the reduced axes in memory order: largest stride first)
        std::stable_sort(innerAxes.begin(), innerAxes.end(), [&](std::size_t a, std::size_t b) { return std::llabs(aArray->strides[a]) > std::llabs(aArray->strides[b]); });
        for (auto a : outerAxes)
        {
            outerShape.push_back(aArray->shape[a]);
            outerStrides.push_back(aArray->strides[a]);
        }
        shape_t outerResultStrides;
        for (auto a : outerAxes)
            outerResultStrides.push_back(keptResultStrides[a]);
        // is the innermost reduced axis the array's innermost (in memory)? (its runs are summed pairwise)
        bool contiguousRuns = false;
        std::int64_t runLength = 1;
        std::int64_t runStride = 0;
        bool const allAxes = innerAxes.size() == ndim;
        if (!innerAxes.empty())
        {
            std::size_t innermost = 0u;
            for (std::size_t i = 0u; i < ndim; ++i)
                if (aArray->shape[i] > 1 && (aArray->shape[innermost] <= 1 || std::llabs(aArray->strides[i]) < std::llabs(aArray->strides[innermost])))
                    innermost = i;
            contiguousRuns = reduced[innermost] || aArray->shape[innermost] <= 1;
            if (allAxes && (aArray->c_contiguous() || aArray->f_contiguous()))
            {
                // (coalesced: one run of all the elements in memory order)
                runLength = aArray->size();
                runStride = static_cast<std::int64_t>(aArray->itemsize());
                innerShape.clear();
                innerStrides.clear();
                // (memory order: the lowest address first)
                bool const fortran = aArray->f_contiguous() && !aArray->c_contiguous();
                (void)fortran;
            }
            else
            {
                auto const last = innerAxes.back();
                runLength = aArray->shape[last];
                runStride = aArray->strides[last];
                for (std::size_t k = 0u; k + 1u < innerAxes.size(); ++k)
                {
                    innerShape.push_back(aArray->shape[innerAxes[k]]);
                    innerStrides.push_back(aArray->strides[innerAxes[k]]);
                }
            }
        }
        bool const coalesced = allAxes && (aArray->c_contiguous() || aArray->f_contiguous()) && !innerAxes.empty();
        unsigned char const* base = aArray->pointer();
        if (coalesced)
        {
            // (the lowest address)
            std::int64_t low = 0;
            for (std::size_t i = 0u; i < ndim; ++i)
                if (aArray->strides[i] < 0)
                    low += aArray->strides[i] * (aArray->shape[i] - 1);
            base = aArray->pointer() + low;
        }
        bool const casting = loop != aArray->type;
        bool const pairwise = name == "add" && (is_inexact(loop)) && contiguousRuns;
        auto const outType = result->type;
        // the initial value
        scalar_value initial;
        initial.type = dtype::Float64;
        if (hasInitial)
        {
            ref const a{ as_object(to_array(aInitial, { loop })) };
            if (!a)
                return nullptr;
            initial = load(as_array(a.get())->pointer(), loop);
        }
        else if (aUfunc.identity)
        {
            initial.f = *aUfunc.identity;
            initial.i = static_cast<std::int64_t>(*aUfunc.identity);
            initial.u = static_cast<std::uint64_t>(initial.i);
            initial.b = *aUfunc.identity != 0.0;
            initial.c = *aUfunc.identity;
            if (is_bool(loop)) initial.type = dtype::Bool;
            else if (is_unsigned(loop)) initial.type = dtype::UInt64;
            else if (is_integer(loop)) initial.type = dtype::Int64;
            else if (is_complex(loop)) initial.type = dtype::Complex128;
        }
        bool const useFirst = !hasInitial && !aUfunc.identity;
        if (!innerAxes.empty() && !contiguousRuns && !casting && outType == loop && !coalesced)
        {
            // (the reduced axes are not the innermost (in memory): the result's elements are combined with each reduced 
            // slice in turn (as rows), so each element's values are combined in the same order)
            shape_t reducedShape, reducedStrides;
            for (auto a : innerAxes)
            {
                reducedShape.push_back(aArray->shape[a]);
                reducedStrides.push_back(aArray->strides[a]);
            }
            auto const itemsize = static_cast<std::int64_t>(info(loop).itemsize);
            bool first = useFirst;
            if (!useFirst)
            {
                unsigned char value[16];
                store(value, loop, initial);
                iterate(outerShape, { result->pointer() }, { outerResultStrides }, [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
                {
                    cast_elements(value, 0, loop, aPointers[0], aStrides[0], loop, aCount);
                });
            }
            iterate(reducedShape, { const_cast<unsigned char*>(base) }, { reducedStrides }, [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
            {
                auto slice = aPointers[0];
                for (std::int64_t r = 0; r < aCount; ++r, slice += aStrides[0])
                {
                    if (first)
                    {
                        iterate(outerShape, { slice, result->pointer() }, { outerStrides, outerResultStrides }, [&](std::int64_t aN, unsigned char** aP, std::int64_t const* aS)
                        {
                            cast_elements(aP[0], aS[0], loop, aP[1], aS[1], loop, aN);
                        });
                        first = false;
                        continue;
                    }
                    if (outerShape.size() == 1u)
                    {
                        // (one outer axis: one loop)
                        unsigned char* pointers[3] = { result->pointer(), slice, result->pointer() };
                        std::int64_t const strides[3] = { outerResultStrides[0], outerStrides[0], outerResultStrides[0] };
                        aUfunc.binary(loop, loop, loop, loop, outerShape[0], pointers, strides);
                    }
                    else
                        iterate(outerShape, { result->pointer(), slice, result->pointer() }, { outerResultStrides, outerStrides, outerResultStrides },
                            [&](std::int64_t aN, unsigned char** aP, std::int64_t const* aS)
                            {
                                aUfunc.binary(loop, loop, loop, loop, aN, aP, aS);
                            });
                }
            });
            (void)itemsize;
            if (failed())
            {
                ctx->release(as_object(result));
                return nullptr;
            }
            if (aOut != nullptr)
                return as_object(result);
            return result_object(result);
        }
        std::vector<unsigned char*> resultPointer{ result->pointer() };
        dispatch(loop, [&](auto tag)
        {
            using T = decltype(tag);
            using C = compute_t<T>;
            auto const loopItem = sizeof(T);
            (void)loopItem;
            iterate(outerShape, { const_cast<unsigned char*>(base), result->pointer() }, { outerStrides, outerResultStrides },
                [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
                {
                    auto inPointer = aPointers[0];
                    auto outPointer = aPointers[1];
                    for (std::int64_t o = 0; o < aCount; ++o, inPointer += aStrides[0], outPointer += aStrides[1])
                    {
                        T accumulator{};
                        bool first = useFirst;
                        if (!useFirst)
                        {
                            unsigned char buffer[16];
                            store(buffer, loop, initial);
                            std::memcpy(&accumulator, buffer, sizeof(T));
                        }
                        auto const combine = [&](T aValue)
                        {
                            if (first)
                            {
                                accumulator = aValue;
                                first = false;
                                return;
                            }
                            unsigned char a[16], b[16], r[16];
                            std::memcpy(a, &accumulator, sizeof(T));
                            std::memcpy(b, &aValue, sizeof(T));
                            unsigned char* pointers[3] = { a, b, r };
                            std::int64_t const strides[3] = { 0, 0, 0 };
                            aUfunc.binary(loop, loop, loop, loop, 1, pointers, strides);
                            std::memcpy(&accumulator, r, sizeof(T));
                        };
                        // (each run: the elements of the innermost reduced axis)
                        iterate(innerShape, { inPointer }, { innerStrides }, [&](std::int64_t aRuns, unsigned char** aRunPointers, std::int64_t const* aRunStrides)
                        {
                            auto runPointer = aRunPointers[0];
                            for (std::int64_t r = 0; r < aRuns; ++r, runPointer += aRunStrides[0])
                            {
                                if constexpr (std::is_same_v<C, float> || std::is_same_v<C, double> || is_complex_v<C>)
                                {
                                    if (pairwise)
                                    {
                                        // (the run is summed in chunks of the iterator's buffer size when it is cast)
                                        std::int64_t const chunk = casting ? 8192 : runLength;
                                        for (std::int64_t start = 0; start < runLength; start += chunk)
                                        {
                                            auto const n = std::min(chunk, runLength - start);
                                            C sum{};
                                            if constexpr (is_complex_v<C>)
                                            {
                                                using R = typename C::value_type;
                                                if (casting)
                                                {
                                                    std::vector<T> converted;
                                                    for (std::int64_t i = 0; i < n; ++i)
                                                        converted.push_back(read_as<T>(runPointer + (start + i) * runStride, aArray->type));
                                                    sum = pairwise_sum_complex<R>(reinterpret_cast<unsigned char const*>(converted.data()), 2 * n, sizeof(T), loop);
                                                }
                                                else
                                                    sum = pairwise_sum_complex<R>(runPointer + start * runStride, 2 * n, runStride, aArray->type);
                                            }
                                            else if (casting)
                                            {
                                                std::vector<T> converted;
                                                for (std::int64_t i = 0; i < n; ++i)
                                                    converted.push_back(read_as<T>(runPointer + (start + i) * runStride, aArray->type));
                                                sum = pairwise_sum<C, T>(reinterpret_cast<unsigned char const*>(converted.data()), n, sizeof(T), loop);
                                            }
                                            else
                                                sum = pairwise_sum<C, T>(runPointer + start * runStride, n, runStride, aArray->type);
                                            if constexpr (is_half_v<T>)
                                                combine(half{ sum });
                                            else
                                                combine(static_cast<T>(sum));
                                        }
                                        continue;
                                    }
                                }
                                if (!casting && runLength > 1)
                                {
                                    // (the whole run: the accumulator is the first operand and the result)
                                    std::int64_t i = 0;
                                    if (first)
                                        combine(read_as<T>(runPointer + i++ * runStride, aArray->type));
                                    unsigned char a[16];
                                    std::memcpy(a, &accumulator, sizeof(T));
                                    unsigned char* pointers[3] = { a, const_cast<unsigned char*>(runPointer) + i * runStride, a };
                                    std::int64_t const strides[3] = { 0, runStride, 0 };
                                    aUfunc.binary(loop, loop, loop, loop, runLength - i, pointers, strides);
                                    std::memcpy(&accumulator, a, sizeof(T));
                                    continue;
                                }
                                for (std::int64_t i = 0; i < runLength; ++i)
                                    combine(read_as<T>(runPointer + i * runStride, aArray->type));
                            }
                        });
                        if (first)
                            continue;
                        write_as(outPointer, outType, accumulator);
                    }
                });
        });
        if (failed())
        {
            ctx->release(as_object(result));
            return nullptr;
        }
        if (aOut != nullptr)
            return as_object(result);
        return result_object(result);
    }

    object* accumulate_array(ufunc const& aUfunc, ndarray* aArray, std::size_t aAxis, std::optional<dtype> aType)
    {
        auto const name = std::string_view{ aUfunc.name };
        dtype loop, out;
        if (aType)
            loop = out = *aType;
        else
        {
            dtype t = aArray->type;
            if ((name == "add" || name == "multiply") && (is_bool(t) || is_integer(t)))
                t = is_unsigned(t) ? dtype::UInt64 : dtype::Int64;
            if (!resolve(aUfunc, { t, t }, loop, out))
                return nullptr;
        }
        if (aUfunc.kind == category::Logical)
            loop = dtype::Bool;
        auto* const result = new_array(out, aArray->shape);
        shape_t outerShape, outerStrides, outerResult;
        for (std::size_t i = 0u; i < aArray->ndim(); ++i)
            if (i != aAxis)
            {
                outerShape.push_back(aArray->shape[i]);
                outerStrides.push_back(aArray->strides[i]);
                outerResult.push_back(result->strides[i]);
            }
        auto const n = aArray->shape[aAxis];
        auto const stride = aArray->strides[aAxis];
        auto const resultStride = result->strides[aAxis];
        iterate(outerShape, { aArray->pointer(), result->pointer() }, { outerStrides, outerResult },
            [&](std::int64_t aCount, unsigned char** aPointers, std::int64_t const* aStrides)
            {
                auto in = aPointers[0];
                auto o = aPointers[1];
                for (std::int64_t k = 0; k < aCount; ++k, in += aStrides[0], o += aStrides[1])
                {
                    if (n == 0)
                        continue;
                    unsigned char accumulator[16];
                    cast_element(in, aArray->type, accumulator, loop);
                    cast_element(accumulator, loop, o, out);
                    for (std::int64_t i = 1; i < n; ++i)
                    {
                        unsigned char value[16], r[16];
                        cast_element(in + i * stride, aArray->type, value, loop);
                        unsigned char* pointers[3] = { accumulator, value, r };
                        std::int64_t const strides[3] = { 0, 0, 0 };
                        aUfunc.binary(loop, loop, loop, loop, 1, pointers, strides);
                        std::memcpy(accumulator, r, info(loop).itemsize);
                        cast_element(accumulator, loop, o + i * resultStride, out);
                    }
                }
            });
        return as_object(result);
    }
}
