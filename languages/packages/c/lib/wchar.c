/*
  lib/wchar.c

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
  The C library: the parts of <wchar.h> (and <inttypes.h>'s wcstoimax and wcstoumax) in C (a part of the library in C: compiled with a program that uses it (see the 
  "provides" line below))
*/

/* provides: wcsncat wcscoll wcsxfrm wcsspn wcscspn wcspbrk wcstok wcstoll wcstoull wcstoimax wcstoumax wcstof wcstold fwide
    getwc putwc ungetwc fgetws vfwscanf vswscanf vwscanf fwscanf swscanf wscanf wcsftime */

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

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
