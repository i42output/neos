/*
  lib/math.c

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

/*
  The C library: C23's <math.h> functions (a part of the library in C: compiled with a program that uses it (see the 
  "provides" line below))
*/

/* provides: sinpi cospi tanpi acospi asinpi atanpi atan2pi exp10m1 exp2m1 logp1 log2p1 log10p1 rsqrt pown compoundn powr
    rootn fmaximum fminimum fmaximum_mag fminimum_mag fmaximum_num fminimum_num fmaximum_mag_num fminimum_mag_num nextup
    nextdown llogb canonicalize fromfp ufromfp fromfpx ufromfpx totalorder totalordermag totalorderf totalordermagf
    getpayload getpayloadf setpayload setpayloadsig setpayloadf setpayloadsigf __neos_issignaling __neos_issignalingf sinpif
    sinpil cospif cospil tanpif tanpil acospif acospil asinpif asinpil atanpif atanpil atan2pif atan2pil exp10m1f exp10m1l
    exp2m1f exp2m1l logp1f logp1l log2p1f log2p1l log10p1f log10p1l rsqrtf rsqrtl pownf pownl compoundnf compoundnl powrf
    powrl rootnf rootnl fmaximumf fmaximuml fminimumf fminimuml fmaximum_magf fmaximum_magl fminimum_magf fminimum_magl
    fmaximum_numf fmaximum_numl fminimum_numf fminimum_numl fmaximum_mag_numf fmaximum_mag_numl fminimum_mag_numf
    fminimum_mag_numl nextupf nextdownf nextupl nextdownl llogbf llogbl canonicalizef canonicalizel fromfpf ufromfpf
    fromfpxf ufromfpxf fromfpl ufromfpl fromfpxl ufromfpxl totalorderl totalordermagl getpayloadl setpayloadl setpayloadsigl
    fadd faddl daddl fsub fsubl dsubl fmul fmull dmull fdiv fdivl ddivl ffma ffmal dfmal fsqrt fsqrtl dsqrtl */

#include <fenv.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

/* --- <math.h>: C23's functions (computed in double precision; float's rounded; long double is double) */

static const double __neos_pi = 3.14159265358979323846;

double sinpi(double x)
{
    if (isinf(x))
        return x - x;
    if (isnan(x) || x == trunc(x))
        return isnan(x) ? x : copysign(0.0, x);
    double r = fmod(x, 2.0);
    if (r < 0.0)
        r += 2.0;
    if (r < 0.25)
        return sin(__neos_pi * r);
    if (r < 0.75)
        return cos(__neos_pi * (r - 0.5));
    if (r < 1.25)
        return sin(__neos_pi * (1.0 - r));
    if (r < 1.75)
        return -cos(__neos_pi * (r - 1.5));
    return sin(__neos_pi * (r - 2.0));
}

double cospi(double x)
{
    if (isinf(x))
        return x - x;
    if (isnan(x))
        return x;
    double const a = fabs(x);
    if (a >= 0x1p52)
        return fmod(a, 2.0) == 0.0 ? 1.0 : -1.0;
    return sinpi(a + 0.5);
}

double tanpi(double x)
{
    return sinpi(x) / cospi(x);
}

double acospi(double x) { return acos(x) / __neos_pi; }
double asinpi(double x) { return asin(x) / __neos_pi; }
double atanpi(double x) { return atan(x) / __neos_pi; }
double atan2pi(double y, double x) { return atan2(y, x) / __neos_pi; }
double exp10m1(double x) { return expm1(x * 2.30258509299404568402); }
double exp2m1(double x) { return expm1(x * 0.69314718055994530942); }
double logp1(double x) { return log1p(x); }
double log2p1(double x) { return log1p(x) / 0.69314718055994530942; }
double log10p1(double x) { return log1p(x) / 2.30258509299404568402; }
double rsqrt(double x) { return 1.0 / sqrt(x); }
double pown(double x, long long int n) { return pow(x, (double)n); }

double compoundn(double x, long long int n)
{
    if (n == 0)
        return 1.0;
    if (x < -1.0)
        return (x - x) / 0.0;
    return exp((double)n * log1p(x));
}

double powr(double x, double y)
{
    if (x < 0.0 || (x == 0.0 && y == 0.0) || (isinf(x) && y == 0.0) || (x == 1.0 && isinf(y)))
        return NAN;
    return pow(x, y);
}

double rootn(double x, long long int n)
{
    if (n == 0 || (x < 0.0 && n % 2 == 0))
        return NAN;
    double const r = pow(fabs(x), 1.0 / (double)n);
    return x < 0.0 || (x == 0.0 && signbit(x) && n % 2 != 0) ? -r : r;
}

double fmaximum(double x, double y)
{
    if (isnan(x) || isnan(y))
        return x + y;
    if (x == y)
        return signbit(x) ? y : x;
    return x > y ? x : y;
}

double fminimum(double x, double y)
{
    if (isnan(x) || isnan(y))
        return x + y;
    if (x == y)
        return signbit(x) ? x : y;
    return x < y ? x : y;
}

double fmaximum_mag(double x, double y)
{
    if (isnan(x) || isnan(y))
        return x + y;
    return fabs(x) > fabs(y) ? x : fabs(y) > fabs(x) ? y : fmaximum(x, y);
}

double fminimum_mag(double x, double y)
{
    if (isnan(x) || isnan(y))
        return x + y;
    return fabs(x) < fabs(y) ? x : fabs(y) < fabs(x) ? y : fminimum(x, y);
}

double fmaximum_num(double x, double y) { return isnan(x) ? (isnan(y) ? x + y : y) : isnan(y) ? x : fmaximum(x, y); }
double fminimum_num(double x, double y) { return isnan(x) ? (isnan(y) ? x + y : y) : isnan(y) ? x : fminimum(x, y); }
double fmaximum_mag_num(double x, double y) { return isnan(x) ? (isnan(y) ? x + y : y) : isnan(y) ? x : fmaximum_mag(x, y); }
double fminimum_mag_num(double x, double y) { return isnan(x) ? (isnan(y) ? x + y : y) : isnan(y) ? x : fminimum_mag(x, y); }
double nextup(double x) { return nextafter(x, INFINITY); }
double nextdown(double x) { return nextafter(x, -INFINITY); }

long int llogb(double x)
{
    if (isnan(x))
        return FP_LLOGBNAN;
    if (x == 0.0)
        return FP_LLOGB0;
    if (isinf(x))
        return LONG_MAX;
    return ilogb(x);
}

int canonicalize(double* cx, const double* x)
{
    *cx = *x;
    return 0;
}

static double __neos_fromfp(double x, int rnd, unsigned int width, bool isUnsigned, bool inexact)
{
    if (isnan(x) || isinf(x) || width == 0u)
        return NAN;
    double r;
    switch (rnd)
    {
    case FP_INT_UPWARD: r = ceil(x); break;
    case FP_INT_DOWNWARD: r = floor(x); break;
    case FP_INT_TOWARDZERO: r = trunc(x); break;
    case FP_INT_TONEARESTFROMZERO: r = round(x); break;
    case FP_INT_TONEAREST: r = roundeven(x); break;
    default: return NAN;
    }
    if (width > 64u)
        width = 64u;
    double const high = isUnsigned ? ldexp(1.0, (int)width) - 1.0 : ldexp(1.0, (int)width - 1) - 1.0;
    double const low = isUnsigned ? 0.0 : -ldexp(1.0, (int)width - 1);
    if (r < low || r > high)
        return NAN;
    if (inexact && r != x)
        feraiseexcept(FE_INEXACT);
    return r;
}

double fromfp(double x, int rnd, unsigned int width) { return __neos_fromfp(x, rnd, width, false, false); }
double ufromfp(double x, int rnd, unsigned int width) { return __neos_fromfp(x, rnd, width, true, false); }
double fromfpx(double x, int rnd, unsigned int width) { return __neos_fromfp(x, rnd, width, false, true); }
double ufromfpx(double x, int rnd, unsigned int width) { return __neos_fromfp(x, rnd, width, true, true); }

static uint64_t __neos_bits(double x)
{
    uint64_t b;
    memcpy(&b, &x, sizeof b);
    return b;
}

static uint32_t __neos_bitsf(float x)
{
    uint32_t b;
    memcpy(&b, &x, sizeof b);
    return b;
}

int totalorder(const double* x, const double* y)
{
    int64_t a = (int64_t)__neos_bits(*x), b = (int64_t)__neos_bits(*y);
    a ^= (int64_t)((uint64_t)(a >> 63) >> 1);
    b ^= (int64_t)((uint64_t)(b >> 63) >> 1);
    return a <= b;
}

int totalordermag(const double* x, const double* y)
{
    return (__neos_bits(*x) & ~(1ull << 63)) <= (__neos_bits(*y) & ~(1ull << 63));
}

int totalorderf(const float* x, const float* y)
{
    int32_t a = (int32_t)__neos_bitsf(*x), b = (int32_t)__neos_bitsf(*y);
    a ^= (int32_t)((uint32_t)(a >> 31) >> 1);
    b ^= (int32_t)((uint32_t)(b >> 31) >> 1);
    return a <= b;
}

int totalordermagf(const float* x, const float* y)
{
    return (__neos_bitsf(*x) & 0x7FFFFFFFu) <= (__neos_bitsf(*y) & 0x7FFFFFFFu);
}

double getpayload(const double* x)
{
    return isnan(*x) ? (double)(__neos_bits(*x) & ((1ull << 51) - 1u)) : -1.0;
}

float getpayloadf(const float* x)
{
    return isnan(*x) ? (float)(__neos_bitsf(*x) & ((1u << 22) - 1u)) : -1.0f;
}

static int __neos_setpayload(double* res, double pl, bool signaling)
{
    if (!(pl == trunc(pl) && pl >= (signaling ? 1.0 : 0.0) && pl < 0x1p51))
    {
        *res = 0.0;
        return 1;
    }
    uint64_t const b = 0x7FF0000000000000ull | (signaling ? 0u : 1ull << 51) | (uint64_t)pl;
    memcpy(res, &b, sizeof b);
    return 0;
}

int setpayload(double* res, double pl) { return __neos_setpayload(res, pl, false); }
int setpayloadsig(double* res, double pl) { return __neos_setpayload(res, pl, true); }

static int __neos_setpayloadf(float* res, float pl, bool signaling)
{
    if (!(pl == truncf(pl) && pl >= (signaling ? 1.0f : 0.0f) && pl < 0x1p22f))
    {
        *res = 0.0f;
        return 1;
    }
    uint32_t const b = 0x7F800000u | (signaling ? 0u : 1u << 22) | (uint32_t)pl;
    memcpy(res, &b, sizeof b);
    return 0;
}

int setpayloadf(float* res, float pl) { return __neos_setpayloadf(res, pl, false); }
int setpayloadsigf(float* res, float pl) { return __neos_setpayloadf(res, pl, true); }

int __neos_issignaling(double x) { return isnan(x) && (__neos_bits(x) & (1ull << 51)) == 0; }
int __neos_issignalingf(float x) { return isnan(x) && (__neos_bitsf(x) & (1u << 22)) == 0; }

/* (the float and long double functions: the double's) */

#define __NEOS_FL1(name) \
    float name##f(float x) { return (float)name((double)x); } \
    long double name##l(long double x) { return name((double)x); }
#define __NEOS_FL2(name) \
    float name##f(float x, float y) { return (float)name((double)x, (double)y); } \
    long double name##l(long double x, long double y) { return name((double)x, (double)y); }
#define __NEOS_FLN(name) \
    float name##f(float x, long long int n) { return (float)name((double)x, n); } \
    long double name##l(long double x, long long int n) { return name((double)x, n); }

__NEOS_FL1(sinpi) __NEOS_FL1(cospi) __NEOS_FL1(tanpi) __NEOS_FL1(acospi) __NEOS_FL1(asinpi) __NEOS_FL1(atanpi)
__NEOS_FL2(atan2pi) __NEOS_FL1(exp10m1) __NEOS_FL1(exp2m1) __NEOS_FL1(logp1) __NEOS_FL1(log2p1) __NEOS_FL1(log10p1)
__NEOS_FL1(rsqrt) __NEOS_FLN(pown) __NEOS_FLN(compoundn) __NEOS_FL2(powr) __NEOS_FLN(rootn)
__NEOS_FL2(fmaximum) __NEOS_FL2(fminimum) __NEOS_FL2(fmaximum_mag) __NEOS_FL2(fminimum_mag) __NEOS_FL2(fmaximum_num)
__NEOS_FL2(fminimum_num) __NEOS_FL2(fmaximum_mag_num) __NEOS_FL2(fminimum_mag_num)

float nextupf(float x) { return nextafterf(x, INFINITY); }
float nextdownf(float x) { return nextafterf(x, -INFINITY); }
long double nextupl(long double x) { return nextup((double)x); }
long double nextdownl(long double x) { return nextdown((double)x); }
long int llogbf(float x) { return llogb((double)x); }
long int llogbl(long double x) { return llogb((double)x); }
int canonicalizef(float* cx, const float* x) { *cx = *x; return 0; }
int canonicalizel(long double* cx, const long double* x) { *cx = *x; return 0; }
float fromfpf(float x, int rnd, unsigned int width) { return (float)fromfp(x, rnd, width); }
float ufromfpf(float x, int rnd, unsigned int width) { return (float)ufromfp(x, rnd, width); }
float fromfpxf(float x, int rnd, unsigned int width) { return (float)fromfpx(x, rnd, width); }
float ufromfpxf(float x, int rnd, unsigned int width) { return (float)ufromfpx(x, rnd, width); }
long double fromfpl(long double x, int rnd, unsigned int width) { return fromfp((double)x, rnd, width); }
long double ufromfpl(long double x, int rnd, unsigned int width) { return ufromfp((double)x, rnd, width); }
long double fromfpxl(long double x, int rnd, unsigned int width) { return fromfpx((double)x, rnd, width); }
long double ufromfpxl(long double x, int rnd, unsigned int width) { return ufromfpx((double)x, rnd, width); }
int totalorderl(const long double* x, const long double* y) { double a = (double)*x, b = (double)*y; return totalorder(&a, &b); }
int totalordermagl(const long double* x, const long double* y) { double a = (double)*x, b = (double)*y; return totalordermag(&a, &b); }
long double getpayloadl(const long double* x) { double a = (double)*x; return getpayload(&a); }
int setpayloadl(long double* res, long double pl) { double r; int const e = setpayload(&r, (double)pl); *res = r; return e; }
int setpayloadsigl(long double* res, long double pl) { double r; int const e = setpayloadsig(&r, (double)pl); *res = r; return e; }

/* (the operations that round to a narrower type) */

float fadd(double x, double y) { return (float)(x + y); }
float faddl(long double x, long double y) { return (float)(x + y); }
double daddl(long double x, long double y) { return (double)(x + y); }
float fsub(double x, double y) { return (float)(x - y); }
float fsubl(long double x, long double y) { return (float)(x - y); }
double dsubl(long double x, long double y) { return (double)(x - y); }
float fmul(double x, double y) { return (float)(x * y); }
float fmull(long double x, long double y) { return (float)(x * y); }
double dmull(long double x, long double y) { return (double)(x * y); }
float fdiv(double x, double y) { return (float)(x / y); }
float fdivl(long double x, long double y) { return (float)(x / y); }
double ddivl(long double x, long double y) { return (double)(x / y); }
float ffma(double x, double y, double z) { return (float)fma(x, y, z); }
float ffmal(long double x, long double y, long double z) { return (float)fma((double)x, (double)y, (double)z); }
double dfmal(long double x, long double y, long double z) { return fma((double)x, (double)y, (double)z); }
float fsqrt(double x) { return (float)sqrt(x); }
float fsqrtl(long double x) { return (float)sqrt((double)x); }
double dsqrtl(long double x) { return sqrt((double)x); }
