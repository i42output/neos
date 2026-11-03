/*
  libc.c

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
  The C library's own C code (the prelude: compiled with every C program; only the functions that a program uses are generated):
  the parts of the standard library that call the program's functions or end it, that return structures, or that are simple 
  enough in C. The rest of the library is native (the C runtime plugin's).
*/

#include <assert.h>
#include <fenv.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

int __neos_exit_prepare(int status);
int __neos_quick_exit_prepare(int status);
int __neos_exit_flush(int status);
void __neos_abort(void);

/* --- <stdlib.h>: program termination */

[[noreturn]] void exit(int status)
{
    __builtin_neos_exit(__neos_exit_prepare(status));
}

[[noreturn]] void quick_exit(int status)
{
    __builtin_neos_exit(__neos_quick_exit_prepare(status));
}

[[noreturn]] void _Exit(int status)
{
    __builtin_neos_exit(__neos_exit_flush(status));
}

[[noreturn]] void abort(void)
{
    __neos_abort();
    __builtin_neos_exit(__neos_exit_flush(134));
}

[[noreturn]] void __neos_assert_fail(const char* expression, const char* file, int line, const char* function)
{
    fprintf(stderr, "%s:%d: %s: Assertion `%s' failed.\n", file, line, function, expression);
    abort();
}

void call_once(once_flag* flag, void (*func)(void))
{
    if (*flag == 0)
    {
        *flag = 1;
        func();
    }
}

/* --- <setjmp.h> */

[[noreturn]] void longjmp(jmp_buf env, int val)
{
    __builtin_longjmp(env, val);
}

/* --- <stdlib.h>: searching and sorting */

void* bsearch(const void* key, const void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*))
{
    const char* low = base;
    size_t n = nmemb;
    while (n > 0)
    {
        const char* middle = low + (n / 2) * size;
        int c = compar(key, middle);
        if (c == 0)
            return (void*)middle;
        if (c > 0)
        {
            low = middle + size;
            n -= n / 2 + 1;
        }
        else
            n /= 2;
    }
    return nullptr;
}

static void __neos_swap(char* a, char* b, size_t size)
{
    if (size == sizeof(long long) && ((uintptr_t)a | (uintptr_t)b) % sizeof(long long) == 0)
    {
        long long t = *(long long*)a;
        *(long long*)a = *(long long*)b;
        *(long long*)b = t;
        return;
    }
    if (size == sizeof(int) && ((uintptr_t)a | (uintptr_t)b) % sizeof(int) == 0)
    {
        int t = *(int*)a;
        *(int*)a = *(int*)b;
        *(int*)b = t;
        return;
    }
    while (size-- > 0)
    {
        char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

/* (an introsort: quicksort (median of three), heapsort when it recurses too deeply, insertion sort for short ranges) */
static void __neos_sift(char* base, size_t root, size_t n, size_t size, int (*compar)(const void*, const void*))
{
    for (;;)
    {
        size_t child = 2 * root + 1;
        if (child >= n)
            return;
        if (child + 1 < n && compar(base + child * size, base + (child + 1) * size) < 0)
            ++child;
        if (compar(base + root * size, base + child * size) >= 0)
            return;
        __neos_swap(base + root * size, base + child * size, size);
        root = child;
    }
}

static void __neos_sort(char* base, size_t n, size_t size, int (*compar)(const void*, const void*), int depth)
{
    while (n > 12)
    {
        if (depth-- == 0)
        {
            for (size_t i = n / 2; i-- > 0;)
                __neos_sift(base, i, n, size, compar);
            for (size_t i = n; i-- > 1;)
            {
                __neos_swap(base, base + i * size, size);
                __neos_sift(base, 0, i, size, compar);
            }
            return;
        }
        char* first = base;
        char* middle = base + (n / 2) * size;
        char* last = base + (n - 1) * size;
        if (compar(middle, first) < 0)
            __neos_swap(middle, first, size);
        if (compar(last, middle) < 0)
        {
            __neos_swap(last, middle, size);
            if (compar(middle, first) < 0)
                __neos_swap(middle, first, size);
        }
        /* (the pivot at the start) */
        __neos_swap(first, middle, size);
        size_t i = 1, j = n - 1;
        for (;;)
        {
            while (i <= j && compar(base + i * size, first) < 0)
                ++i;
            while (j >= i && compar(base + j * size, first) > 0)
                --j;
            if (i >= j)
                break;
            __neos_swap(base + i * size, base + j * size, size);
            ++i;
            --j;
        }
        __neos_swap(first, base + j * size, size);
        /* (the smaller side recursively, the larger iteratively) */
        size_t left = j;
        size_t right = n - j - 1;
        if (left < right)
        {
            __neos_sort(base, left, size, compar, depth);
            base += (j + 1) * size;
            n = right;
        }
        else
        {
            __neos_sort(base + (j + 1) * size, right, size, compar, depth);
            n = left;
        }
    }
    for (size_t i = 1; i < n; ++i)
        for (size_t k = i; k > 0 && compar(base + (k - 1) * size, base + k * size) > 0; --k)
            __neos_swap(base + (k - 1) * size, base + k * size, size);
}

void qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*))
{
    int depth = 0;
    for (size_t n = nmemb; n > 1; n >>= 1)
        depth += 2;
    if (nmemb > 1 && size > 0)
        __neos_sort(base, nmemb, size, compar, depth);
}

/* --- <stdlib.h>, <inttypes.h>: integer division */

div_t div(int numer, int denom)
{
    div_t r = { numer / denom, numer % denom };
    return r;
}

ldiv_t ldiv(long int numer, long int denom)
{
    ldiv_t r = { numer / denom, numer % denom };
    return r;
}

lldiv_t lldiv(long long int numer, long long int denom)
{
    lldiv_t r = { numer / denom, numer % denom };
    return r;
}

imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom)
{
    imaxdiv_t r = { numer / denom, numer % denom };
    return r;
}

/* --- <fenv.h>: the environment (the rounding direction and the exceptions) */

int fegetexceptflag(fexcept_t* flagp, int excepts)
{
    *flagp = fetestexcept(excepts);
    return 0;
}

int fesetexcept(int excepts)
{
    return feraiseexcept(excepts);
}

int fesetexceptflag(const fexcept_t* flagp, int excepts)
{
    feclearexcept(excepts);
    return feraiseexcept(*flagp & excepts);
}

int fetestexceptflag(const fexcept_t* flagp, int excepts)
{
    return *flagp & excepts;
}

int fegetenv(fenv_t* envp)
{
    envp->__round = fegetround();
    envp->__except = fetestexcept(FE_ALL_EXCEPT);
    return 0;
}

int feholdexcept(fenv_t* envp)
{
    fegetenv(envp);
    feclearexcept(FE_ALL_EXCEPT);
    return 0;
}

int fesetenv(const fenv_t* envp)
{
    feclearexcept(FE_ALL_EXCEPT);
    if (envp == FE_DFL_ENV)
        return fesetround(FE_TONEAREST);
    fesetround(envp->__round);
    return feraiseexcept(envp->__except);
}

int feupdateenv(const fenv_t* envp)
{
    int raised = fetestexcept(FE_ALL_EXCEPT);
    fesetenv(envp);
    return feraiseexcept(raised);
}

/* --- <wctype.h>: properties and mappings by name */

wctype_t wctype(const char* property)
{
    static const char* const names[] = { "alnum", "alpha", "blank", "cntrl", "digit", "graph", "lower", "print", "punct", "space", "upper", "xdigit" };
    for (int i = 0; i < 12; ++i)
        if (strcmp(property, names[i]) == 0)
            return i + 1;
    return 0;
}

int iswctype(wint_t wc, wctype_t desc)
{
    switch (desc)
    {
    case 1: return iswalnum(wc);
    case 2: return iswalpha(wc);
    case 3: return iswblank(wc);
    case 4: return iswcntrl(wc);
    case 5: return iswdigit(wc);
    case 6: return iswgraph(wc);
    case 7: return iswlower(wc);
    case 8: return iswprint(wc);
    case 9: return iswpunct(wc);
    case 10: return iswspace(wc);
    case 11: return iswupper(wc);
    case 12: return iswxdigit(wc);
    default: return 0;
    }
}

wctrans_t wctrans(const char* property)
{
    if (strcmp(property, "tolower") == 0)
        return 1;
    if (strcmp(property, "toupper") == 0)
        return 2;
    return 0;
}

wint_t towctrans(wint_t wc, wctrans_t desc)
{
    return desc == 1 ? towlower(wc) : desc == 2 ? towupper(wc) : wc;
}

/* --- <wchar.h>: strings */

wchar_t* wcsncat(wchar_t* restrict s1, const wchar_t* restrict s2, size_t n)
{
    wchar_t* d = s1 + wcslen(s1);
    while (n-- > 0 && *s2 != 0)
        *d++ = *s2++;
    *d = 0;
    return s1;
}

int wcscoll(const wchar_t* s1, const wchar_t* s2)
{
    return wcscmp(s1, s2);  /* (the "C" locale's collation) */
}

size_t wcsxfrm(wchar_t* restrict s1, const wchar_t* restrict s2, size_t n)
{
    size_t const length = wcslen(s2);
    if (length < n)
        wcscpy(s1, s2);
    return length;
}

size_t wcsspn(const wchar_t* s1, const wchar_t* s2)
{
    size_t n = 0;
    while (s1[n] != 0 && wcschr(s2, s1[n]) != nullptr)
        ++n;
    return n;
}

size_t wcscspn(const wchar_t* s1, const wchar_t* s2)
{
    size_t n = 0;
    while (s1[n] != 0 && wcschr(s2, s1[n]) == nullptr)
        ++n;
    return n;
}

wchar_t* wcspbrk(const wchar_t* s1, const wchar_t* s2)
{
    s1 += wcscspn(s1, s2);
    return *s1 != 0 ? (wchar_t*)s1 : nullptr;
}

wchar_t* wcstok(wchar_t* restrict s1, const wchar_t* restrict s2, wchar_t** restrict ptr)
{
    if (s1 == nullptr)
        s1 = *ptr;
    s1 += wcsspn(s1, s2);
    if (*s1 == 0)
    {
        *ptr = s1;
        return nullptr;
    }
    wchar_t* const end = s1 + wcscspn(s1, s2);
    if (*end != 0)
    {
        *end = 0;
        *ptr = end + 1;
    }
    else
        *ptr = end;
    return s1;
}

/* --- <wchar.h>, <inttypes.h>: numeric conversions (the number's characters are those of the basic character set: converted by 
   the narrow functions) */

static const wchar_t* __neos_narrow_number(const wchar_t* s, char* buffer, size_t size)
{
    while (iswspace((wint_t)*s))
        ++s;
    size_t i = 0;
    while (i + 1u < size && s[i] > 0 && s[i] < 0x80)
    {
        buffer[i] = (char)s[i];
        ++i;
    }
    buffer[i] = '\0';
    return s;
}

#define __NEOS_WCSTO(name, type, narrow, ...) \
    type name(const wchar_t* restrict nptr, wchar_t** restrict endptr __VA_OPT__(, int base)) \
    { \
        char buffer[512]; \
        const wchar_t* const start = __neos_narrow_number(nptr, buffer, sizeof buffer); \
        char* end; \
        type const result = narrow(buffer, &end __VA_OPT__(, base)); \
        if (endptr != nullptr) \
            *endptr = (wchar_t*)(end == buffer ? nptr : start + (end - buffer)); \
        return result; \
    }

__NEOS_WCSTO(wcstoll, long long int, strtoll, base)
__NEOS_WCSTO(wcstoull, unsigned long long int, strtoull, base)
__NEOS_WCSTO(wcstoimax, intmax_t, strtoimax, base)
__NEOS_WCSTO(wcstoumax, uintmax_t, strtoumax, base)
__NEOS_WCSTO(wcstof, float, strtof)
__NEOS_WCSTO(wcstold, long double, strtold)

/* --- <wchar.h>: input and output (a stream's wide characters: as UTF-8) */

int fwide(FILE* stream, int mode)
{
    (void)stream;
    return mode;    /* (streams have no orientation of their own: each as asked) */
}

wint_t getwc(FILE* stream)
{
    return fgetwc(stream);
}

wint_t putwc(wchar_t c, FILE* stream)
{
    return fputwc(c, stream);
}

wint_t ungetwc(wint_t c, FILE* stream)
{
    if (c == WEOF)
        return WEOF;
    char bytes[8];
    mbstate_t state = { 0 };
    size_t const n = wcrtomb(bytes, (wchar_t)c, &state);
    if (n == (size_t)-1)
        return WEOF;
    for (size_t i = n; i-- > 0;)
        if (ungetc((unsigned char)bytes[i], stream) == EOF)
            return WEOF;
    return c;
}

wchar_t* fgetws(wchar_t* restrict s, int n, FILE* restrict stream)
{
    if (n <= 0)
        return nullptr;
    int i = 0;
    while (i + 1 < n)
    {
        wint_t const c = fgetwc(stream);
        if (c == WEOF)
            break;
        s[i++] = (wchar_t)c;
        if (c == L'\n')
            break;
    }
    if (i == 0)
        return nullptr;
    s[i] = 0;
    return s;
}

static char* __neos_narrow(const wchar_t* s)
{
    mbstate_t state = { 0 };
    const wchar_t* p = s;
    size_t const length = wcsrtombs(nullptr, &p, 0, &state);
    if (length == (size_t)-1)
        return nullptr;
    char* const result = malloc(length + 1u);
    if (result != nullptr)
    {
        p = s;
        state = (mbstate_t){ 0 };
        wcsrtombs(result, &p, length + 1u, &state);
    }
    return result;
}

int vfwscanf(FILE* restrict stream, const wchar_t* restrict format, va_list arg)
{
    char* const f = __neos_narrow(format);
    if (f == nullptr)
        return EOF;
    int const result = vfscanf(stream, f, arg);
    free(f);
    return result;
}

int vswscanf(const wchar_t* restrict s, const wchar_t* restrict format, va_list arg)
{
    char* const f = __neos_narrow(format);
    char* const input = __neos_narrow(s);
    int result = EOF;
    if (f != nullptr && input != nullptr)
        result = vsscanf(input, f, arg);
    free(f);
    free(input);
    return result;
}

int vwscanf(const wchar_t* restrict format, va_list arg)
{
    return vfwscanf(stdin, format, arg);
}

int fwscanf(FILE* restrict stream, const wchar_t* restrict format, ...)
{
    va_list arg;
    va_start(arg, format);
    int const result = vfwscanf(stream, format, arg);
    va_end(arg);
    return result;
}

int swscanf(const wchar_t* restrict s, const wchar_t* restrict format, ...)
{
    va_list arg;
    va_start(arg, format);
    int const result = vswscanf(s, format, arg);
    va_end(arg);
    return result;
}

int wscanf(const wchar_t* restrict format, ...)
{
    va_list arg;
    va_start(arg, format);
    int const result = vfwscanf(stdin, format, arg);
    va_end(arg);
    return result;
}

size_t wcsftime(wchar_t* restrict s, size_t maxsize, const wchar_t* restrict format, const struct tm* restrict timeptr)
{
    char* const f = __neos_narrow(format);
    if (f == nullptr || maxsize == 0u)
    {
        free(f);
        return 0;
    }
    char buffer[1024];
    size_t const n = strftime(buffer, sizeof buffer, f, timeptr);
    free(f);
    if (n == 0u && buffer[0] != '\0')
        return 0;
    size_t const length = mbstowcs(s, buffer, maxsize);
    if (length == (size_t)-1 || length >= maxsize)
        return 0;
    return length;
}

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

/* --- <stdatomic.h>: the functions (as well as their macros: a program has one thread) */

void (atomic_thread_fence)(memory_order order) { (void)order; }
void (atomic_signal_fence)(memory_order order) { (void)order; }
bool (atomic_flag_test_and_set)(volatile atomic_flag* object) { return atomic_flag_test_and_set(object); }
bool (atomic_flag_test_and_set_explicit)(volatile atomic_flag* object, memory_order order) { (void)order; return atomic_flag_test_and_set(object); }
void (atomic_flag_clear)(volatile atomic_flag* object) { atomic_flag_clear(object); }
void (atomic_flag_clear_explicit)(volatile atomic_flag* object, memory_order order) { (void)order; atomic_flag_clear(object); }
