/*
  lib_math.cpp

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

// The C language's runtime: <math.h> (7.12), the host's (float functions as float, double and long double as double), <fenv.h> (7.6)

#include <cfenv>
#include <cmath>
#include <cstring>
#include "core.hpp"

namespace neos::c_runtime
{
    namespace
    {
        double c_frexp(double aValue, std::int64_t aExponent) { int e = 0; auto const r = std::frexp(aValue, &e); *at<std::int32_t>(static_cast<address>(aExponent)) = e; return r; }
        float c_frexpf(float aValue, std::int64_t aExponent) { int e = 0; auto const r = std::frexp(aValue, &e); *at<std::int32_t>(static_cast<address>(aExponent)) = e; return r; }
        double c_modf(double aValue, std::int64_t aIntegral) { double i = 0.0; auto const r = std::modf(aValue, &i); std::memcpy(at<std::uint8_t>(static_cast<address>(aIntegral)), &i, 8u); return r; }
        float c_modff(float aValue, std::int64_t aIntegral) { float i = 0.0f; auto const r = std::modf(aValue, &i); std::memcpy(at<std::uint8_t>(static_cast<address>(aIntegral)), &i, 4u); return r; }
        double c_remquo(double aX, double aY, std::int64_t aQuotient) { int q = 0; auto const r = std::remquo(aX, aY, &q); *at<std::int32_t>(static_cast<address>(aQuotient)) = q; return r; }
        float c_remquof(float aX, float aY, std::int64_t aQuotient) { int q = 0; auto const r = std::remquo(aX, aY, &q); *at<std::int32_t>(static_cast<address>(aQuotient)) = q; return r; }
        double c_nan(std::int64_t aTag) { return std::nan(aTag != 0 ? str(static_cast<address>(aTag)) : ""); }
        float c_nanf(std::int64_t aTag) { return std::nanf(aTag != 0 ? str(static_cast<address>(aTag)) : ""); }
        std::int32_t c_ilogb(double aValue) { return std::ilogb(aValue); }
        std::int32_t c_ilogbf(float aValue) { return std::ilogb(aValue); }
        double c_ldexp(double aValue, std::int32_t aExponent) { return std::ldexp(aValue, aExponent); }
        float c_ldexpf(float aValue, std::int32_t aExponent) { return std::ldexp(aValue, aExponent); }
        double c_scalbn(double aValue, std::int32_t aExponent) { return std::scalbn(aValue, aExponent); }
        float c_scalbnf(float aValue, std::int32_t aExponent) { return std::scalbn(aValue, aExponent); }
        double c_scalbln(double aValue, std::int64_t aExponent) { return std::scalbln(aValue, static_cast<long>(aExponent)); }
        float c_scalblnf(float aValue, std::int64_t aExponent) { return std::scalbln(aValue, static_cast<long>(aExponent)); }
        std::int64_t c_lrint(double aValue) { return static_cast<std::int64_t>(std::llrint(aValue)); }
        std::int64_t c_lrintf(float aValue) { return static_cast<std::int64_t>(std::llrint(aValue)); }
        std::int64_t c_lround(double aValue) { return static_cast<std::int64_t>(std::llround(aValue)); }
        std::int64_t c_lroundf(float aValue) { return static_cast<std::int64_t>(std::llround(aValue)); }
        double c_fma(double aX, double aY, double aZ) { return std::fma(aX, aY, aZ); }
        float c_fmaf(float aX, float aY, float aZ) { return std::fma(aX, aY, aZ); }
        double c_nexttoward(double aX, double aY) { return std::nextafter(aX, aY); }
        float c_nexttowardf(float aX, double aY) { return std::nexttoward(aX, static_cast<long double>(aY)); }
        double c_exp10(double aValue) { return std::pow(10.0, aValue); }
        double c_roundeven(double aValue)
        {
            // (to the nearest integer, ties to even)
            auto const r = std::round(aValue);
            if (std::fabs(aValue - std::trunc(aValue)) == 0.5)
                return 2.0 * std::round(aValue / 2.0);
            return r;
        }
        float c_roundevenf(float aValue) { return static_cast<float>(c_roundeven(aValue)); }
        std::int32_t c_fpclassify(double aValue)
        {
            // (FP_NAN 0, FP_INFINITE 1, FP_ZERO 2, FP_SUBNORMAL 3, FP_NORMAL 4 (see <math.h>))
            switch (std::fpclassify(aValue))
            {
            case FP_NAN: return 0;
            case FP_INFINITE: return 1;
            case FP_ZERO: return 2;
            case FP_SUBNORMAL: return 3;
            default: return 4;
            }
        }
        std::int32_t c_signbit(double aValue) { return std::signbit(aValue) ? 1 : 0; }

        // (<fenv.h>: the exceptions as <fenv.h> defines them (FE_INVALID 1, FE_DIVBYZERO 4, FE_OVERFLOW 8, FE_UNDERFLOW 16, FE_INEXACT 32), 
        // the host's; the rounding directions (FE_TONEAREST 0, FE_DOWNWARD 0x400, FE_UPWARD 0x800, FE_TOWARDZERO 0xC00))
        int host_exceptions(int aExceptions)
        {
            int r = 0;
            if (aExceptions & 1) r |= FE_INVALID;
            if (aExceptions & 4) r |= FE_DIVBYZERO;
            if (aExceptions & 8) r |= FE_OVERFLOW;
            if (aExceptions & 16) r |= FE_UNDERFLOW;
            if (aExceptions & 32) r |= FE_INEXACT;
            return r;
        }
        int c_exceptions(int aExceptions)
        {
            int r = 0;
            if (aExceptions & FE_INVALID) r |= 1;
            if (aExceptions & FE_DIVBYZERO) r |= 4;
            if (aExceptions & FE_OVERFLOW) r |= 8;
            if (aExceptions & FE_UNDERFLOW) r |= 16;
            if (aExceptions & FE_INEXACT) r |= 32;
            return r;
        }
        std::int32_t c_feclearexcept(std::int32_t aExceptions) { return std::feclearexcept(host_exceptions(aExceptions)) == 0 ? 0 : -1; }
        std::int32_t c_fetestexcept(std::int32_t aExceptions) { return c_exceptions(std::fetestexcept(host_exceptions(aExceptions))); }
        std::int32_t c_feraiseexcept(std::int32_t aExceptions) { return std::feraiseexcept(host_exceptions(aExceptions)) == 0 ? 0 : -1; }
        std::int32_t c_fegetround()
        {
            switch (std::fegetround())
            {
            case FE_DOWNWARD: return 0x400;
            case FE_UPWARD: return 0x800;
            case FE_TOWARDZERO: return 0xC00;
            default: return 0;
            }
        }
        std::int32_t c_fesetround(std::int32_t aRound)
        {
            int r = FE_TONEAREST;
            switch (aRound)
            {
            case 0: r = FE_TONEAREST; break;
            case 0x400: r = FE_DOWNWARD; break;
            case 0x800: r = FE_UPWARD; break;
            case 0xC00: r = FE_TOWARDZERO; break;
            default: return -1;
            }
            return std::fesetround(r) == 0 ? 0 : -1;
        }

        double c_acos(double x) { return std::acos(x); }
        float c_acosf(float x) { return std::acos(x); }
        double c_asin(double x) { return std::asin(x); }
        float c_asinf(float x) { return std::asin(x); }
        double c_atan(double x) { return std::atan(x); }
        float c_atanf(float x) { return std::atan(x); }
        double c_cos(double x) { return std::cos(x); }
        float c_cosf(float x) { return std::cos(x); }
        double c_sin(double x) { return std::sin(x); }
        float c_sinf(float x) { return std::sin(x); }
        double c_tan(double x) { return std::tan(x); }
        float c_tanf(float x) { return std::tan(x); }
        double c_acosh(double x) { return std::acosh(x); }
        float c_acoshf(float x) { return std::acosh(x); }
        double c_asinh(double x) { return std::asinh(x); }
        float c_asinhf(float x) { return std::asinh(x); }
        double c_atanh(double x) { return std::atanh(x); }
        float c_atanhf(float x) { return std::atanh(x); }
        double c_cosh(double x) { return std::cosh(x); }
        float c_coshf(float x) { return std::cosh(x); }
        double c_sinh(double x) { return std::sinh(x); }
        float c_sinhf(float x) { return std::sinh(x); }
        double c_tanh(double x) { return std::tanh(x); }
        float c_tanhf(float x) { return std::tanh(x); }
        double c_exp(double x) { return std::exp(x); }
        float c_expf(float x) { return std::exp(x); }
        double c_exp2(double x) { return std::exp2(x); }
        float c_exp2f(float x) { return std::exp2(x); }
        double c_expm1(double x) { return std::expm1(x); }
        float c_expm1f(float x) { return std::expm1(x); }
        double c_log(double x) { return std::log(x); }
        float c_logf(float x) { return std::log(x); }
        double c_log10(double x) { return std::log10(x); }
        float c_log10f(float x) { return std::log10(x); }
        double c_log1p(double x) { return std::log1p(x); }
        float c_log1pf(float x) { return std::log1p(x); }
        double c_log2(double x) { return std::log2(x); }
        float c_log2f(float x) { return std::log2(x); }
        double c_logb(double x) { return std::logb(x); }
        float c_logbf(float x) { return std::logb(x); }
        double c_cbrt(double x) { return std::cbrt(x); }
        float c_cbrtf(float x) { return std::cbrt(x); }
        double c_fabs(double x) { return std::fabs(x); }
        float c_fabsf(float x) { return std::fabs(x); }
        double c_sqrt(double x) { return std::sqrt(x); }
        float c_sqrtf(float x) { return std::sqrt(x); }
        double c_erf(double x) { return std::erf(x); }
        float c_erff(float x) { return std::erf(x); }
        double c_erfc(double x) { return std::erfc(x); }
        float c_erfcf(float x) { return std::erfc(x); }
        double c_lgamma(double x) { return std::lgamma(x); }
        float c_lgammaf(float x) { return std::lgamma(x); }
        double c_tgamma(double x) { return std::tgamma(x); }
        float c_tgammaf(float x) { return std::tgamma(x); }
        double c_ceil(double x) { return std::ceil(x); }
        float c_ceilf(float x) { return std::ceil(x); }
        double c_floor(double x) { return std::floor(x); }
        float c_floorf(float x) { return std::floor(x); }
        double c_nearbyint(double x) { return std::nearbyint(x); }
        float c_nearbyintf(float x) { return std::nearbyint(x); }
        double c_rint(double x) { return std::rint(x); }
        float c_rintf(float x) { return std::rint(x); }
        double c_round(double x) { return std::round(x); }
        float c_roundf(float x) { return std::round(x); }
        double c_trunc(double x) { return std::trunc(x); }
        float c_truncf(float x) { return std::trunc(x); }
        double c_atan2(double x, double y) { return std::atan2(x, y); }
        float c_atan2f(float x, float y) { return std::atan2(x, y); }
        double c_hypot(double x, double y) { return std::hypot(x, y); }
        float c_hypotf(float x, float y) { return std::hypot(x, y); }
        double c_pow(double x, double y) { return std::pow(x, y); }
        float c_powf(float x, float y) { return std::pow(x, y); }
        double c_fmod(double x, double y) { return std::fmod(x, y); }
        float c_fmodf(float x, float y) { return std::fmod(x, y); }
        double c_remainder(double x, double y) { return std::remainder(x, y); }
        float c_remainderf(float x, float y) { return std::remainder(x, y); }
        double c_copysign(double x, double y) { return std::copysign(x, y); }
        float c_copysignf(float x, float y) { return std::copysign(x, y); }
        double c_nextafter(double x, double y) { return std::nextafter(x, y); }
        float c_nextafterf(float x, float y) { return std::nextafter(x, y); }
        double c_fdim(double x, double y) { return std::fdim(x, y); }
        float c_fdimf(float x, float y) { return std::fdim(x, y); }
        double c_fmax(double x, double y) { return std::fmax(x, y); }
        float c_fmaxf(float x, float y) { return std::fmax(x, y); }
        double c_fmin(double x, double y) { return std::fmin(x, y); }
        float c_fminf(float x, float y) { return std::fmin(x, y); }

        function_table const sMathFunctions
        {
            library("acos", NEOS_RT_FUNCTION(c_acos)), library("acosf", NEOS_RT_FUNCTION(c_acosf)),
            library("asin", NEOS_RT_FUNCTION(c_asin)), library("asinf", NEOS_RT_FUNCTION(c_asinf)),
            library("atan", NEOS_RT_FUNCTION(c_atan)), library("atanf", NEOS_RT_FUNCTION(c_atanf)),
            library("cos", NEOS_RT_FUNCTION(c_cos)), library("cosf", NEOS_RT_FUNCTION(c_cosf)),
            library("sin", NEOS_RT_FUNCTION(c_sin)), library("sinf", NEOS_RT_FUNCTION(c_sinf)),
            library("tan", NEOS_RT_FUNCTION(c_tan)), library("tanf", NEOS_RT_FUNCTION(c_tanf)),
            library("acosh", NEOS_RT_FUNCTION(c_acosh)), library("acoshf", NEOS_RT_FUNCTION(c_acoshf)),
            library("asinh", NEOS_RT_FUNCTION(c_asinh)), library("asinhf", NEOS_RT_FUNCTION(c_asinhf)),
            library("atanh", NEOS_RT_FUNCTION(c_atanh)), library("atanhf", NEOS_RT_FUNCTION(c_atanhf)),
            library("cosh", NEOS_RT_FUNCTION(c_cosh)), library("coshf", NEOS_RT_FUNCTION(c_coshf)),
            library("sinh", NEOS_RT_FUNCTION(c_sinh)), library("sinhf", NEOS_RT_FUNCTION(c_sinhf)),
            library("tanh", NEOS_RT_FUNCTION(c_tanh)), library("tanhf", NEOS_RT_FUNCTION(c_tanhf)),
            library("exp", NEOS_RT_FUNCTION(c_exp)), library("expf", NEOS_RT_FUNCTION(c_expf)),
            library("exp2", NEOS_RT_FUNCTION(c_exp2)), library("exp2f", NEOS_RT_FUNCTION(c_exp2f)),
            library("expm1", NEOS_RT_FUNCTION(c_expm1)), library("expm1f", NEOS_RT_FUNCTION(c_expm1f)),
            library("log", NEOS_RT_FUNCTION(c_log)), library("logf", NEOS_RT_FUNCTION(c_logf)),
            library("log10", NEOS_RT_FUNCTION(c_log10)), library("log10f", NEOS_RT_FUNCTION(c_log10f)),
            library("log1p", NEOS_RT_FUNCTION(c_log1p)), library("log1pf", NEOS_RT_FUNCTION(c_log1pf)),
            library("log2", NEOS_RT_FUNCTION(c_log2)), library("log2f", NEOS_RT_FUNCTION(c_log2f)),
            library("logb", NEOS_RT_FUNCTION(c_logb)), library("logbf", NEOS_RT_FUNCTION(c_logbf)),
            library("cbrt", NEOS_RT_FUNCTION(c_cbrt)), library("cbrtf", NEOS_RT_FUNCTION(c_cbrtf)),
            library("fabs", NEOS_RT_FUNCTION(c_fabs)), library("fabsf", NEOS_RT_FUNCTION(c_fabsf)),
            library("sqrt", NEOS_RT_FUNCTION(c_sqrt)), library("sqrtf", NEOS_RT_FUNCTION(c_sqrtf)),
            library("erf", NEOS_RT_FUNCTION(c_erf)), library("erff", NEOS_RT_FUNCTION(c_erff)),
            library("erfc", NEOS_RT_FUNCTION(c_erfc)), library("erfcf", NEOS_RT_FUNCTION(c_erfcf)),
            library("lgamma", NEOS_RT_FUNCTION(c_lgamma)), library("lgammaf", NEOS_RT_FUNCTION(c_lgammaf)),
            library("tgamma", NEOS_RT_FUNCTION(c_tgamma)), library("tgammaf", NEOS_RT_FUNCTION(c_tgammaf)),
            library("ceil", NEOS_RT_FUNCTION(c_ceil)), library("ceilf", NEOS_RT_FUNCTION(c_ceilf)),
            library("floor", NEOS_RT_FUNCTION(c_floor)), library("floorf", NEOS_RT_FUNCTION(c_floorf)),
            library("nearbyint", NEOS_RT_FUNCTION(c_nearbyint)), library("nearbyintf", NEOS_RT_FUNCTION(c_nearbyintf)),
            library("rint", NEOS_RT_FUNCTION(c_rint)), library("rintf", NEOS_RT_FUNCTION(c_rintf)),
            library("round", NEOS_RT_FUNCTION(c_round)), library("roundf", NEOS_RT_FUNCTION(c_roundf)),
            library("trunc", NEOS_RT_FUNCTION(c_trunc)), library("truncf", NEOS_RT_FUNCTION(c_truncf)),
            library("atan2", NEOS_RT_FUNCTION(c_atan2)), library("atan2f", NEOS_RT_FUNCTION(c_atan2f)),
            library("hypot", NEOS_RT_FUNCTION(c_hypot)), library("hypotf", NEOS_RT_FUNCTION(c_hypotf)),
            library("pow", NEOS_RT_FUNCTION(c_pow)), library("powf", NEOS_RT_FUNCTION(c_powf)),
            library("fmod", NEOS_RT_FUNCTION(c_fmod)), library("fmodf", NEOS_RT_FUNCTION(c_fmodf)),
            library("remainder", NEOS_RT_FUNCTION(c_remainder)), library("remainderf", NEOS_RT_FUNCTION(c_remainderf)),
            library("copysign", NEOS_RT_FUNCTION(c_copysign)), library("copysignf", NEOS_RT_FUNCTION(c_copysignf)),
            library("nextafter", NEOS_RT_FUNCTION(c_nextafter)), library("nextafterf", NEOS_RT_FUNCTION(c_nextafterf)),
            library("fdim", NEOS_RT_FUNCTION(c_fdim)), library("fdimf", NEOS_RT_FUNCTION(c_fdimf)),
            library("fmax", NEOS_RT_FUNCTION(c_fmax)), library("fmaxf", NEOS_RT_FUNCTION(c_fmaxf)),
            library("fmin", NEOS_RT_FUNCTION(c_fmin)), library("fminf", NEOS_RT_FUNCTION(c_fminf)),
            library("frexp", NEOS_RT_FUNCTION(c_frexp)), library("frexpf", NEOS_RT_FUNCTION(c_frexpf)),
            library("modf", NEOS_RT_FUNCTION(c_modf)), library("modff", NEOS_RT_FUNCTION(c_modff)),
            library("remquo", NEOS_RT_FUNCTION(c_remquo)), library("remquof", NEOS_RT_FUNCTION(c_remquof)),
            library("nan", NEOS_RT_FUNCTION(c_nan)), library("nanf", NEOS_RT_FUNCTION(c_nanf)),
            library("ilogb", NEOS_RT_FUNCTION(c_ilogb)), library("ilogbf", NEOS_RT_FUNCTION(c_ilogbf)),
            library("ldexp", NEOS_RT_FUNCTION(c_ldexp)), library("ldexpf", NEOS_RT_FUNCTION(c_ldexpf)),
            library("scalbn", NEOS_RT_FUNCTION(c_scalbn)), library("scalbnf", NEOS_RT_FUNCTION(c_scalbnf)),
            library("scalbln", NEOS_RT_FUNCTION(c_scalbln)), library("scalblnf", NEOS_RT_FUNCTION(c_scalblnf)),
            library("lrint", NEOS_RT_FUNCTION(c_lrint)), library("llrint", NEOS_RT_FUNCTION(c_lrint)),
            library("lrintf", NEOS_RT_FUNCTION(c_lrintf)), library("llrintf", NEOS_RT_FUNCTION(c_lrintf)),
            library("lround", NEOS_RT_FUNCTION(c_lround)), library("llround", NEOS_RT_FUNCTION(c_lround)),
            library("lroundf", NEOS_RT_FUNCTION(c_lroundf)), library("llroundf", NEOS_RT_FUNCTION(c_lroundf)),
            library("fma", NEOS_RT_FUNCTION(c_fma)), library("fmaf", NEOS_RT_FUNCTION(c_fmaf)),
            library("nexttoward", NEOS_RT_FUNCTION(c_nexttoward)), library("exp10", NEOS_RT_FUNCTION(c_exp10)),
            library("roundeven", NEOS_RT_FUNCTION(c_roundeven)), library("roundevenf", NEOS_RT_FUNCTION(c_roundevenf)),
            library("__neos_fpclassify", NEOS_RT_FUNCTION(c_fpclassify)), library("__neos_signbit", NEOS_RT_FUNCTION(c_signbit)),
            library("feclearexcept", NEOS_RT_FUNCTION(c_feclearexcept)), library("fetestexcept", NEOS_RT_FUNCTION(c_fetestexcept)),
            library("feraiseexcept", NEOS_RT_FUNCTION(c_feraiseexcept)), library("fegetround", NEOS_RT_FUNCTION(c_fegetround)),
            library("fesetround", NEOS_RT_FUNCTION(c_fesetround)),
            library("acosl", NEOS_RT_FUNCTION(c_acos)),
            library("asinl", NEOS_RT_FUNCTION(c_asin)),
            library("atanl", NEOS_RT_FUNCTION(c_atan)),
            library("cosl", NEOS_RT_FUNCTION(c_cos)),
            library("sinl", NEOS_RT_FUNCTION(c_sin)),
            library("tanl", NEOS_RT_FUNCTION(c_tan)),
            library("acoshl", NEOS_RT_FUNCTION(c_acosh)),
            library("asinhl", NEOS_RT_FUNCTION(c_asinh)),
            library("atanhl", NEOS_RT_FUNCTION(c_atanh)),
            library("coshl", NEOS_RT_FUNCTION(c_cosh)),
            library("sinhl", NEOS_RT_FUNCTION(c_sinh)),
            library("tanhl", NEOS_RT_FUNCTION(c_tanh)),
            library("expl", NEOS_RT_FUNCTION(c_exp)),
            library("exp2l", NEOS_RT_FUNCTION(c_exp2)),
            library("expm1l", NEOS_RT_FUNCTION(c_expm1)),
            library("logl", NEOS_RT_FUNCTION(c_log)),
            library("log10l", NEOS_RT_FUNCTION(c_log10)),
            library("log1pl", NEOS_RT_FUNCTION(c_log1p)),
            library("log2l", NEOS_RT_FUNCTION(c_log2)),
            library("logbl", NEOS_RT_FUNCTION(c_logb)),
            library("cbrtl", NEOS_RT_FUNCTION(c_cbrt)),
            library("fabsl", NEOS_RT_FUNCTION(c_fabs)),
            library("sqrtl", NEOS_RT_FUNCTION(c_sqrt)),
            library("erfl", NEOS_RT_FUNCTION(c_erf)),
            library("erfcl", NEOS_RT_FUNCTION(c_erfc)),
            library("lgammal", NEOS_RT_FUNCTION(c_lgamma)),
            library("tgammal", NEOS_RT_FUNCTION(c_tgamma)),
            library("ceill", NEOS_RT_FUNCTION(c_ceil)),
            library("floorl", NEOS_RT_FUNCTION(c_floor)),
            library("nearbyintl", NEOS_RT_FUNCTION(c_nearbyint)),
            library("rintl", NEOS_RT_FUNCTION(c_rint)),
            library("roundl", NEOS_RT_FUNCTION(c_round)),
            library("truncl", NEOS_RT_FUNCTION(c_trunc)),
            library("roundevenl", NEOS_RT_FUNCTION(c_roundeven)),
            library("atan2l", NEOS_RT_FUNCTION(c_atan2)),
            library("hypotl", NEOS_RT_FUNCTION(c_hypot)),
            library("powl", NEOS_RT_FUNCTION(c_pow)),
            library("fmodl", NEOS_RT_FUNCTION(c_fmod)),
            library("remainderl", NEOS_RT_FUNCTION(c_remainder)),
            library("copysignl", NEOS_RT_FUNCTION(c_copysign)),
            library("nextafterl", NEOS_RT_FUNCTION(c_nextafter)),
            library("fdiml", NEOS_RT_FUNCTION(c_fdim)),
            library("fmaxl", NEOS_RT_FUNCTION(c_fmax)),
            library("fminl", NEOS_RT_FUNCTION(c_fmin)),
            library("frexpl", NEOS_RT_FUNCTION(c_frexp)),
            library("ilogbl", NEOS_RT_FUNCTION(c_ilogb)),
            library("ldexpl", NEOS_RT_FUNCTION(c_ldexp)),
            library("modfl", NEOS_RT_FUNCTION(c_modf)),
            library("scalbnl", NEOS_RT_FUNCTION(c_scalbn)),
            library("scalblnl", NEOS_RT_FUNCTION(c_scalbln)),
            library("lrintl", NEOS_RT_FUNCTION(c_lrint)),
            library("llrintl", NEOS_RT_FUNCTION(c_lrint)),
            library("lroundl", NEOS_RT_FUNCTION(c_lround)),
            library("llroundl", NEOS_RT_FUNCTION(c_lround)),
            library("remquol", NEOS_RT_FUNCTION(c_remquo)),
            library("nanl", NEOS_RT_FUNCTION(c_nan)),
            library("nexttowardl", NEOS_RT_FUNCTION(c_nexttoward)),
            library("nexttowardf", NEOS_RT_FUNCTION(c_nexttowardf)),
            library("fmal", NEOS_RT_FUNCTION(c_fma))
        };
    }
}
