/*
  tgmath.h

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

/* <tgmath.h> (7.27): type-generic math */

#ifndef __NEOS_TGMATH_H
#define __NEOS_TGMATH_H
#include <math.h>

/* (type-generic macros: real types only (__STDC_NO_COMPLEX__)) */
#define acos(x) _Generic((x), float: acosf, long double: acosl, default: acos)(x)
#define asin(x) _Generic((x), float: asinf, long double: asinl, default: asin)(x)
#define atan(x) _Generic((x), float: atanf, long double: atanl, default: atan)(x)
#define cos(x) _Generic((x), float: cosf, long double: cosl, default: cos)(x)
#define sin(x) _Generic((x), float: sinf, long double: sinl, default: sin)(x)
#define tan(x) _Generic((x), float: tanf, long double: tanl, default: tan)(x)
#define acosh(x) _Generic((x), float: acoshf, long double: acoshl, default: acosh)(x)
#define asinh(x) _Generic((x), float: asinhf, long double: asinhl, default: asinh)(x)
#define atanh(x) _Generic((x), float: atanhf, long double: atanhl, default: atanh)(x)
#define cosh(x) _Generic((x), float: coshf, long double: coshl, default: cosh)(x)
#define sinh(x) _Generic((x), float: sinhf, long double: sinhl, default: sinh)(x)
#define tanh(x) _Generic((x), float: tanhf, long double: tanhl, default: tanh)(x)
#define exp(x) _Generic((x), float: expf, long double: expl, default: exp)(x)
#define exp2(x) _Generic((x), float: exp2f, long double: exp2l, default: exp2)(x)
#define expm1(x) _Generic((x), float: expm1f, long double: expm1l, default: expm1)(x)
#define log(x) _Generic((x), float: logf, long double: logl, default: log)(x)
#define log10(x) _Generic((x), float: log10f, long double: log10l, default: log10)(x)
#define log1p(x) _Generic((x), float: log1pf, long double: log1pl, default: log1p)(x)
#define log2(x) _Generic((x), float: log2f, long double: log2l, default: log2)(x)
#define logb(x) _Generic((x), float: logbf, long double: logbl, default: logb)(x)
#define cbrt(x) _Generic((x), float: cbrtf, long double: cbrtl, default: cbrt)(x)
#define fabs(x) _Generic((x), float: fabsf, long double: fabsl, default: fabs)(x)
#define sqrt(x) _Generic((x), float: sqrtf, long double: sqrtl, default: sqrt)(x)
#define erf(x) _Generic((x), float: erff, long double: erfl, default: erf)(x)
#define erfc(x) _Generic((x), float: erfcf, long double: erfcl, default: erfc)(x)
#define lgamma(x) _Generic((x), float: lgammaf, long double: lgammal, default: lgamma)(x)
#define tgamma(x) _Generic((x), float: tgammaf, long double: tgammal, default: tgamma)(x)
#define ceil(x) _Generic((x), float: ceilf, long double: ceill, default: ceil)(x)
#define floor(x) _Generic((x), float: floorf, long double: floorl, default: floor)(x)
#define nearbyint(x) _Generic((x), float: nearbyintf, long double: nearbyintl, default: nearbyint)(x)
#define rint(x) _Generic((x), float: rintf, long double: rintl, default: rint)(x)
#define round(x) _Generic((x), float: roundf, long double: roundl, default: round)(x)
#define trunc(x) _Generic((x), float: truncf, long double: truncl, default: trunc)(x)
#define roundeven(x) _Generic((x), float: roundevenf, long double: roundevenl, default: roundeven)(x)
#define atan2(x, y) _Generic((x) + (y), float: atan2f, long double: atan2l, default: atan2)((x), (y))
#define hypot(x, y) _Generic((x) + (y), float: hypotf, long double: hypotl, default: hypot)((x), (y))
#define pow(x, y) _Generic((x) + (y), float: powf, long double: powl, default: pow)((x), (y))
#define fmod(x, y) _Generic((x) + (y), float: fmodf, long double: fmodl, default: fmod)((x), (y))
#define remainder(x, y) _Generic((x) + (y), float: remainderf, long double: remainderl, default: remainder)((x), (y))
#define copysign(x, y) _Generic((x) + (y), float: copysignf, long double: copysignl, default: copysign)((x), (y))
#define nextafter(x, y) _Generic((x) + (y), float: nextafterf, long double: nextafterl, default: nextafter)((x), (y))
#define fdim(x, y) _Generic((x) + (y), float: fdimf, long double: fdiml, default: fdim)((x), (y))
#define fmax(x, y) _Generic((x) + (y), float: fmaxf, long double: fmaxl, default: fmax)((x), (y))
#define fmin(x, y) _Generic((x) + (y), float: fminf, long double: fminl, default: fmin)((x), (y))
#define frexp(x, p) _Generic((x), float: frexpf, long double: frexpl, default: frexp)(x, p)
#define ldexp(x, p) _Generic((x), float: ldexpf, long double: ldexpl, default: ldexp)(x, p)
#define scalbn(x, p) _Generic((x), float: scalbnf, long double: scalbnl, default: scalbn)(x, p)
#define scalbln(x, p) _Generic((x), float: scalblnf, long double: scalblnl, default: scalbln)(x, p)
#define ilogb(x) _Generic((x), float: ilogbf, long double: ilogbl, default: ilogb)(x)
#define lrint(x) _Generic((x), float: lrintf, long double: lrintl, default: lrint)(x)
#define llrint(x) _Generic((x), float: llrintf, long double: llrintl, default: llrint)(x)
#define lround(x) _Generic((x), float: lroundf, long double: lroundl, default: lround)(x)
#define llround(x) _Generic((x), float: llroundf, long double: llroundl, default: llround)(x)
#define fma(x, y, z) _Generic((x) + (y) + (z), float: fmaf, long double: fmal, default: fma)((x), (y), (z))
#define remquo(x, y, q) _Generic((x) + (y), float: remquof, long double: remquol, default: remquo)((x), (y), (q))

#endif
