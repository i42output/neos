/*
  c_library.hpp

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

#pragma once

// The C standard library's functions that the C runtime plugin (neos.runtime.c) implements natively, with their WebAssembly 
// signatures as the C compiler calls them (the C compiler's calling convention: an integer type of up to 32 bits is an i32 (I), 
// a 64-bit integer type or a pointer an i64 (L: an address in the C memory, see c.hpp), float an f32 (F), double and long double 
// an f64 (D); a variadic function's variable arguments are an i64 (the address of their area: each in an 8-byte slot); a 
// va_list (char*) is an i64), as "result:parameters" (V: void). A function of the library declared by the standard headers 
// and not defined by the program's code (or the library's own C code, the prelude) is called in the runtime as "c::libc.<name>".

#define NEOS_C_LIBRARY(X) \
    /* <stdio.h> */ \
    X(remove, "I:L", true) X(rename, "I:LL", true) X(tmpfile, "L:", true) X(tmpnam, "L:L", false) \
    X(fclose, "I:L", false) X(fflush, "I:L", false) X(fopen, "L:LL", true) X(freopen, "L:LLL", true) X(setbuf, "V:LL", false) \
    X(setvbuf, "I:LLIL", false) X(fprintf, "I:LLL", false) X(fscanf, "I:LLL", false) X(printf, "I:LL", false) X(scanf, "I:LL", false) \
    X(snprintf, "I:LLLL", false) X(sprintf, "I:LLL", false) X(sscanf, "I:LLL", false) X(vfprintf, "I:LLL", false) X(vfscanf, "I:LLL", false) \
    X(vprintf, "I:LL", false) X(vscanf, "I:LL", false) X(vsnprintf, "I:LLLL", false) X(vsprintf, "I:LLL", false) X(vsscanf, "I:LLL", false) \
    X(fgetc, "I:L", false) X(fgets, "L:LIL", false) X(fputc, "I:IL", false) X(fputs, "I:LL", false) X(getc, "I:L", false) \
    X(getchar, "I:", false) X(putc, "I:IL", false) X(putchar, "I:I", false) X(puts, "I:L", false) X(ungetc, "I:IL", false) \
    X(fread, "L:LLLL", false) X(fwrite, "L:LLLL", false) X(fgetpos, "I:LL", false) X(fseek, "I:LLI", false) X(fsetpos, "I:LL", false) \
    X(ftell, "L:L", false) X(rewind, "V:L", false) X(clearerr, "V:L", false) X(feof, "I:L", false) X(ferror, "I:L", false) \
    X(perror, "V:L", false) X(getline, "L:LLL", false) X(getdelim, "L:LLIL", false) X(fileno, "I:L", false) \
    /* <stdlib.h> */ \
    X(atof, "D:L", false) X(atoi, "I:L", false) X(atol, "L:L", false) X(atoll, "L:L", false) X(strtod, "D:LL", false) \
    X(strtof, "F:LL", false) X(strtold, "D:LL", false) X(strtol, "L:LLI", false) X(strtoll, "L:LLI", false) X(strtoul, "L:LLI", false) \
    X(strtoull, "L:LLI", false) X(strfromd, "I:LLLD", false) X(strfromf, "I:LLLF", false) X(strfroml, "I:LLLD", false) \
    X(rand, "I:", false) X(srand, "V:I", false) X(aligned_alloc, "L:LL", false) X(calloc, "L:LL", false) X(free, "V:L", false) \
    X(free_sized, "V:LL", false) X(free_aligned_sized, "V:LLL", false) X(malloc, "L:L", false) X(realloc, "L:LL", false) \
    X(atexit, "I:L", false) X(at_quick_exit, "I:L", false) X(getenv, "L:L", false) X(system, "I:L", true) X(abs, "I:I", false) \
    X(labs, "L:L", false) X(llabs, "L:L", false) X(mblen, "I:LL", false) X(mbtowc, "I:LLL", false) X(wctomb, "I:LI", false) \
    X(mbstowcs, "L:LLL", false) X(wcstombs, "L:LLL", false) X(memalignment, "L:L", false) X(setenv, "I:LLI", true) X(unsetenv, "I:L", true) \
    /* <string.h> */ \
    X(memcpy, "L:LLL", false) X(memccpy, "L:LLIL", false) X(memmove, "L:LLL", false) X(strcpy, "L:LL", false) X(strncpy, "L:LLL", false) \
    X(strdup, "L:L", false) X(strndup, "L:LL", false) X(strcat, "L:LL", false) X(strncat, "L:LLL", false) X(memcmp, "I:LLL", false) \
    X(strcmp, "I:LL", false) X(strcoll, "I:LL", false) X(strncmp, "I:LLL", false) X(strxfrm, "L:LLL", false) X(memchr, "L:LIL", false) \
    X(strchr, "L:LI", false) X(strcspn, "L:LL", false) X(strpbrk, "L:LL", false) X(strrchr, "L:LI", false) X(strspn, "L:LL", false) \
    X(strstr, "L:LL", false) X(strtok, "L:LL", false) X(memset, "L:LIL", false) X(memset_explicit, "L:LIL", false) X(strerror, "L:I", false) \
    X(strlen, "L:L", false) X(strnlen, "L:LL", false) X(strtok_r, "L:LLL", false) X(strcasecmp, "I:LL", false) X(strncasecmp, "I:LLL", false) \
    /* <ctype.h> */ \
    X(isalnum, "I:I", false) X(isalpha, "I:I", false) X(isblank, "I:I", false) X(iscntrl, "I:I", false) X(isdigit, "I:I", false) \
    X(isgraph, "I:I", false) X(islower, "I:I", false) X(isprint, "I:I", false) X(ispunct, "I:I", false) X(isspace, "I:I", false) \
    X(isupper, "I:I", false) X(isxdigit, "I:I", false) X(tolower, "I:I", false) X(toupper, "I:I", false) \
    /* <math.h> */ \
    X(acos, "D:D", false) X(asin, "D:D", false) X(atan, "D:D", false) X(atan2, "D:DD", false) X(cos, "D:D", false) X(sin, "D:D", false) \
    X(tan, "D:D", false) X(acosh, "D:D", false) X(asinh, "D:D", false) X(atanh, "D:D", false) X(cosh, "D:D", false) X(sinh, "D:D", false) \
    X(tanh, "D:D", false) X(exp, "D:D", false) X(exp2, "D:D", false) X(expm1, "D:D", false) X(frexp, "D:DL", false) X(ilogb, "I:D", false) \
    X(ldexp, "D:DI", false) X(log, "D:D", false) X(log10, "D:D", false) X(log1p, "D:D", false) X(log2, "D:D", false) X(logb, "D:D", false) \
    X(modf, "D:DL", false) X(scalbn, "D:DI", false) X(scalbln, "D:DL", false) X(cbrt, "D:D", false) X(fabs, "D:D", false) \
    X(hypot, "D:DD", false) X(pow, "D:DD", false) X(sqrt, "D:D", false) X(erf, "D:D", false) X(erfc, "D:D", false) X(lgamma, "D:D", false) \
    X(tgamma, "D:D", false) X(ceil, "D:D", false) X(floor, "D:D", false) X(nearbyint, "D:D", false) X(rint, "D:D", false) \
    X(lrint, "L:D", false) X(llrint, "L:D", false) X(round, "D:D", false) X(lround, "L:D", false) X(llround, "L:D", false) \
    X(trunc, "D:D", false) X(roundeven, "D:D", false) X(fmod, "D:DD", false) X(remainder, "D:DD", false) X(remquo, "D:DDL", false) \
    X(copysign, "D:DD", false) X(nan, "D:L", false) X(nextafter, "D:DD", false) X(nexttoward, "D:DD", false) X(fdim, "D:DD", false) \
    X(fmax, "D:DD", false) X(fmin, "D:DD", false) X(fma, "D:DDD", false) X(exp10, "D:D", false) \
    X(acosf, "F:F", false) X(asinf, "F:F", false) X(atanf, "F:F", false) X(atan2f, "F:FF", false) X(cosf, "F:F", false) X(sinf, "F:F", false) \
    X(tanf, "F:F", false) X(acoshf, "F:F", false) X(asinhf, "F:F", false) X(atanhf, "F:F", false) X(coshf, "F:F", false) X(sinhf, "F:F", false) \
    X(tanhf, "F:F", false) X(expf, "F:F", false) X(exp2f, "F:F", false) X(expm1f, "F:F", false) X(frexpf, "F:FL", false) X(ilogbf, "I:F", false) \
    X(ldexpf, "F:FI", false) X(logf, "F:F", false) X(log10f, "F:F", false) X(log1pf, "F:F", false) X(log2f, "F:F", false) X(logbf, "F:F", false) \
    X(modff, "F:FL", false) X(scalbnf, "F:FI", false) X(scalblnf, "F:FL", false) X(cbrtf, "F:F", false) X(fabsf, "F:F", false) \
    X(hypotf, "F:FF", false) X(powf, "F:FF", false) X(sqrtf, "F:F", false) X(erff, "F:F", false) X(erfcf, "F:F", false) X(lgammaf, "F:F", false) \
    X(tgammaf, "F:F", false) X(ceilf, "F:F", false) X(floorf, "F:F", false) X(nearbyintf, "F:F", false) X(rintf, "F:F", false) \
    X(lrintf, "L:F", false) X(llrintf, "L:F", false) X(roundf, "F:F", false) X(lroundf, "L:F", false) X(llroundf, "L:F", false) \
    X(truncf, "F:F", false) X(roundevenf, "F:F", false) X(fmodf, "F:FF", false) X(remainderf, "F:FF", false) X(remquof, "F:FFL", false) \
    X(copysignf, "F:FF", false) X(nanf, "F:L", false) X(nextafterf, "F:FF", false) X(fdimf, "F:FF", false) X(fmaxf, "F:FF", false) \
    X(fminf, "F:FF", false) X(fmaf, "F:FFF", false) \
    /* (long double: as double) */ \
    X(acosl, "D:D", false) X(asinl, "D:D", false) X(atanl, "D:D", false) X(cosl, "D:D", false) X(sinl, "D:D", false) \
    X(tanl, "D:D", false) X(acoshl, "D:D", false) X(asinhl, "D:D", false) X(atanhl, "D:D", false) X(coshl, "D:D", false) \
    X(sinhl, "D:D", false) X(tanhl, "D:D", false) X(expl, "D:D", false) X(exp2l, "D:D", false) X(expm1l, "D:D", false) \
    X(logl, "D:D", false) X(log10l, "D:D", false) X(log1pl, "D:D", false) X(log2l, "D:D", false) X(logbl, "D:D", false) \
    X(cbrtl, "D:D", false) X(fabsl, "D:D", false) X(sqrtl, "D:D", false) X(erfl, "D:D", false) X(erfcl, "D:D", false) \
    X(lgammal, "D:D", false) X(tgammal, "D:D", false) X(ceill, "D:D", false) X(floorl, "D:D", false) X(nearbyintl, "D:D", false) \
    X(rintl, "D:D", false) X(roundl, "D:D", false) X(truncl, "D:D", false) X(roundevenl, "D:D", false) X(atan2l, "D:DD", false) \
    X(hypotl, "D:DD", false) X(powl, "D:DD", false) X(fmodl, "D:DD", false) X(remainderl, "D:DD", false) X(copysignl, "D:DD", false) \
    X(nextafterl, "D:DD", false) X(fdiml, "D:DD", false) X(fmaxl, "D:DD", false) X(fminl, "D:DD", false) X(frexpl, "D:DL", false) \
    X(ilogbl, "I:D", false) X(ldexpl, "D:DI", false) X(modfl, "D:DL", false) X(scalbnl, "D:DI", false) X(scalblnl, "D:DL", false) \
    X(lrintl, "L:D", false) X(llrintl, "L:D", false) X(lroundl, "L:D", false) X(llroundl, "L:D", false) X(remquol, "D:DDL", false) \
    X(nanl, "D:L", false) X(nexttowardl, "D:DD", false) X(nexttowardf, "F:FD", false) X(fmal, "D:DDD", false) \
    X(__neos_fpclassify, "I:D", false) X(__neos_signbit, "I:D", false) \
    /* <time.h> */ \
    X(clock, "L:", false) X(difftime, "D:LL", false) X(mktime, "L:L", false) X(time, "L:L", false) X(timespec_get, "I:LI", false) \
    X(timespec_getres, "I:LI", false) X(asctime, "L:L", false) X(ctime, "L:L", false) X(gmtime, "L:L", false) X(gmtime_r, "L:LL", false) \
    X(localtime, "L:L", false) X(localtime_r, "L:LL", false) X(strftime, "L:LLLL", false) X(timegm, "L:L", false) \
    /* <signal.h> */ \
    X(signal, "L:IL", false) X(raise, "I:I", false) \
    /* <locale.h> */ \
    X(setlocale, "L:IL", false) X(localeconv, "L:", false) \
    /* <inttypes.h> */ \
    X(strtoimax, "L:LLI", false) X(strtoumax, "L:LLI", false) X(imaxabs, "L:L", false) \
    /* <wchar.h>, <uchar.h>, <wctype.h> */ \
    X(wcslen, "L:L", false) X(wcscpy, "L:LL", false) X(wcsncpy, "L:LLL", false) X(wcscat, "L:LL", false) X(wcscmp, "I:LL", false) \
    X(wcsncmp, "I:LLL", false) X(wcschr, "L:LI", false) X(wcsrchr, "L:LI", false) X(wcsstr, "L:LL", false) X(wmemcpy, "L:LLL", false) \
    X(wmemmove, "L:LLL", false) X(wmemset, "L:LIL", false) X(wmemcmp, "I:LLL", false) X(wmemchr, "L:LIL", false) \
    X(mbrtowc, "L:LLLL", false) X(wcrtomb, "L:LIL", false) X(mbrlen, "L:LLL", false) X(mbsinit, "I:L", false) X(btowc, "I:I", false) \
    X(wctob, "I:I", false) X(mbsrtowcs, "L:LLLL", false) X(wcsrtombs, "L:LLLL", false) X(mbrtoc16, "L:LLLL", false) X(c16rtomb, "L:LIL", false) \
    X(mbrtoc32, "L:LLLL", false) X(c32rtomb, "L:LIL", false) X(mbrtoc8, "L:LLLL", false) X(c8rtomb, "L:LIL", false) \
    X(iswalnum, "I:I", false) X(iswalpha, "I:I", false) X(iswblank, "I:I", false) X(iswcntrl, "I:I", false) X(iswdigit, "I:I", false) \
    X(iswgraph, "I:I", false) X(iswlower, "I:I", false) X(iswprint, "I:I", false) X(iswpunct, "I:I", false) X(iswspace, "I:I", false) \
    X(iswupper, "I:I", false) X(iswxdigit, "I:I", false) X(towlower, "I:I", false) X(towupper, "I:I", false) \
    X(fputwc, "I:IL", false) X(putwchar, "I:I", false) X(fgetwc, "I:L", false) X(getwchar, "I:", false) X(fputws, "I:LL", false) \
    X(wcstol, "L:LLI", false) X(wcstoul, "L:LLI", false) X(wcstod, "D:LL", false) \
    X(swprintf, "I:LLLL", false) X(vswprintf, "I:LLLL", false) X(wprintf, "I:LL", false) X(vwprintf, "I:LL", false) X(fwprintf, "I:LLL", false) \
    X(vfwprintf, "I:LLL", false) \
    /* <fenv.h> */ \
    X(feclearexcept, "I:I", false) X(fetestexcept, "I:I", false) X(feraiseexcept, "I:I", false) X(fegetround, "I:", false) \
    X(fesetround, "I:I", false) \
    /* <threads.h>, <pthread.h>, <semaphore.h>, <errno.h> (the runtime's: see languages/packages/c/lib/threads.c) */ \
    X(__errno_location, "L:", false) X(__neos_thread_create, "I:LLLLLI", false) X(__neos_thread_join, "I:LL", false) \
    X(__neos_thread_detach, "I:L", false) X(__neos_thread_self, "L:", false) X(__neos_thread_main, "I:", false) \
    X(__neos_thread_sleep, "I:LL", false) X(__neos_thread_yield, "V:", false) X(__neos_thread_cancel, "I:L", false) \
    X(__neos_thread_cancelled, "I:", false) X(__neos_thread_wait_others, "V:", false) X(__neos_thread_exit, "V:L", false) X(__neos_mutex_init, "I:LI", false) X(__neos_mutex_lock, "I:L", false) \
    X(__neos_mutex_trylock, "I:L", false) X(__neos_mutex_timedlock, "I:LLL", false) X(__neos_mutex_unlock, "I:L", false) \
    X(__neos_object_destroy, "I:L", false) X(__neos_cond_init, "I:L", false) X(__neos_cond_wait, "I:LL", false) \
    X(__neos_cond_timedwait, "I:LLLL", false) X(__neos_cond_signal, "I:L", false) X(__neos_cond_broadcast, "I:L", false) \
    X(__neos_rwlock_init, "I:L", false) X(__neos_rwlock_lock, "I:LII", false) X(__neos_rwlock_unlock, "I:L", false) \
    X(__neos_barrier_init, "I:LI", false) X(__neos_barrier_wait, "I:L", false) X(__neos_sem_init, "I:LI", false) \
    X(__neos_sem_wait, "I:LI", false) X(__neos_sem_timedwait, "I:LLL", false) X(__neos_sem_post, "I:L", false) \
    X(__neos_sem_getvalue, "I:LL", false) X(__neos_key_create, "I:LL", false) X(__neos_key_delete, "I:I", false) \
    X(__neos_key_get, "L:I", false) X(__neos_key_set, "I:IL", false) \
    /* (the library's C code's (the prelude's) own) */ \
    X(__neos_exit_prepare, "I:I", false) X(__neos_quick_exit_prepare, "I:I", false) X(__neos_exit_flush, "I:I", false) \
    X(__neos_abort, "V:", false)

namespace neos::language::c
{
    struct library_function
    {
        char const* name;
        char const* signature;      ///< ("result:parameters": V void, I i32, L i64, F f32, D f64)
        bool restricted;            ///< (it can change the host environment: see NEOS_RT_RESTRICTED)
    };

    inline constexpr library_function LibraryFunctions[] =
    {
#define NEOS_C_LIBRARY_ENTRY(aName, aSignature, aRestricted) { #aName, aSignature, aRestricted },
        NEOS_C_LIBRARY(NEOS_C_LIBRARY_ENTRY)
#undef NEOS_C_LIBRARY_ENTRY
    };
}
