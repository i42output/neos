/*
  lib_stdlib.cpp

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

// The C language's runtime: <stdlib.h> (7.24) (qsort, bsearch, div and the exit functions are the library's C code: the prelude),
// <inttypes.h>'s strtoimax and strtoumax, <signal.h> (7.14) and <locale.h> (7.11)

#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include "format.hpp"

namespace neos::c_runtime
{
    std::size_t stream_write(std::int64_t aFile, char const* aData, std::size_t aCount);

    namespace
    {
        // --- numeric conversions: the host's (the strings are NUL terminated in the C memory)

        void end_pointer(std::int64_t aEnd, char const* aStart, char const* aStop)
        {
            if (aEnd != 0)
                *at<std::uint64_t>(static_cast<address>(aEnd)) = address_of(aStop != nullptr ? aStop : aStart);
        }

        double c_atof(std::int64_t aString) { return std::atof(str(static_cast<address>(aString))); }
        std::int32_t c_atoi(std::int64_t aString) { return std::atoi(str(static_cast<address>(aString))); }
        std::int64_t c_atol(std::int64_t aString) { return std::atoll(str(static_cast<address>(aString))); }

        double c_strtod(std::int64_t aString, std::int64_t aEnd)
        {
            char* end = nullptr;
            errno = 0;
            auto const* s = str(static_cast<address>(aString));
            auto const v = std::strtod(s, &end);
            if (errno != 0)
                errno_value() = errno;
            end_pointer(aEnd, s, end);
            return v;
        }

        float c_strtof(std::int64_t aString, std::int64_t aEnd)
        {
            char* end = nullptr;
            errno = 0;
            auto const* s = str(static_cast<address>(aString));
            auto const v = std::strtof(s, &end);
            if (errno != 0)
                errno_value() = errno;
            end_pointer(aEnd, s, end);
            return v;
        }

        // (strtol and the like: C23's binary prefix (0b) too)
        template <typename Convert>
        auto integer_conversion(std::int64_t aString, std::int64_t aEnd, std::int32_t aBase, Convert aConvert)
        {
            auto const* s = str(static_cast<address>(aString));
            char* end = nullptr;
            errno = 0;
            auto const* p = s;
            while (std::isspace(static_cast<unsigned char>(*p)))
                ++p;
            char const* q = p;
            if (*q == '+' || *q == '-')
                ++q;
            if ((aBase == 0 || aBase == 2) && q[0] == '0' && (q[1] == 'b' || q[1] == 'B') && (q[2] == '0' || q[2] == '1'))
            {
                std::string copy{ p, static_cast<std::size_t>(q - p) };
                copy += (q + 2);
                auto const v = aConvert(copy.c_str(), &end, 2);
                if (errno != 0)
                    errno_value() = errno;
                end_pointer(aEnd, s, q + 2 + (end - copy.c_str() - static_cast<std::ptrdiff_t>(q - p)));
                return v;
            }
            auto const v = aConvert(s, &end, aBase);
            if (errno != 0)
                errno_value() = errno;
            end_pointer(aEnd, s, end);
            return v;
        }

        std::int64_t c_strtol(std::int64_t aString, std::int64_t aEnd, std::int32_t aBase)
        {
            return static_cast<std::int64_t>(integer_conversion(aString, aEnd, aBase, [](char const* a, char** b, int c) { return std::strtoll(a, b, c); }));
        }

        std::int64_t c_strtoul(std::int64_t aString, std::int64_t aEnd, std::int32_t aBase)
        {
            return static_cast<std::int64_t>(integer_conversion(aString, aEnd, aBase, [](char const* a, char** b, int c) { return std::strtoull(a, b, c); }));
        }

        std::int32_t c_strfromd(std::int64_t aBuffer, std::int64_t aSize, std::int64_t aFormat, double aValue)
        {
            // (the format: %[.precision]{a,A,e,E,f,F,g,G})
            std::vector<std::int64_t> slot{ 0 };
            std::memcpy(slot.data(), &aValue, 8u);
            auto const area = allocate(8u);
            std::memcpy(at<std::uint8_t>(area), &aValue, 8u);
            arguments args{ area };
            auto const text = format(str(static_cast<address>(aFormat)), args);
            deallocate(area);
            if (aSize > 0)
            {
                auto const n = std::min<std::size_t>(text.size(), static_cast<std::size_t>(aSize) - 1u);
                std::memcpy(at<char>(static_cast<address>(aBuffer)), text.data(), n);
                at<char>(static_cast<address>(aBuffer))[n] = '\0';
            }
            return static_cast<std::int32_t>(text.size());
        }

        std::int32_t c_strfromf(std::int64_t aBuffer, std::int64_t aSize, std::int64_t aFormat, float aValue)
        {
            return c_strfromd(aBuffer, aSize, aFormat, static_cast<double>(aValue));
        }

        // --- pseudo-random sequence generation: the additive feedback generator of glibc's rand (TYPE_3: r[i] = r[i-3] + r[i-31]),
        // so that programs' sequences are those of the same programs compiled for Linux; RAND_MAX is 2147483647. (The algorithm is 
        // BSD random(3)'s (TYPE_3), as glibc's random_r.c describes it; this implementation is neos's own: no code is taken from
        // glibc)

        struct random_state
        {
            std::int32_t ring[31];
            int f = 3;
            int b = 0;
            void seed(std::uint32_t aSeed)
            {
                if (aSeed == 0u)
                    aSeed = 1u;
                std::int32_t word = static_cast<std::int32_t>(aSeed);
                ring[0] = word;
                for (int i = 1; i < 31; ++i)
                {
                    // (16807 * word % 2147483647, without overflow (Schrage's method))
                    std::int32_t const hi = word / 127773;
                    std::int32_t const lo = word % 127773;
                    word = 16807 * lo - 2836 * hi;
                    if (word < 0)
                        word += 2147483647;
                    ring[i] = word;
                }
                f = 3;
                b = 0;
                for (int i = 0; i < 310; ++i)
                    next();
            }
            std::int32_t next()
            {
                auto& a = ring[f];
                a = static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(ring[b]));
                std::int32_t const result = static_cast<std::int32_t>(static_cast<std::uint32_t>(a) >> 1u);
                if (++f >= 31)
                    f = 0;
                if (++b >= 31)
                    b = 0;
                return result;
            }
        };

        random_state& random()
        {
            static random_state sState = []() { random_state s; s.seed(1u); return s; }();
            return sState;
        }

        std::int32_t c_rand() { return random().next(); }
        void c_srand(std::int32_t aSeed) { random().seed(static_cast<std::uint32_t>(aSeed)); }

        // --- memory management

        std::int64_t c_malloc(std::int64_t aSize)
        {
            auto const r = allocate(static_cast<std::uint64_t>(aSize));
            if (r == 0u)
                errno_value() = ENOMEM;
            return static_cast<std::int64_t>(r);
        }

        std::int64_t c_calloc(std::int64_t aCount, std::int64_t aSize)
        {
            auto const count = static_cast<std::uint64_t>(aCount);
            auto const size = static_cast<std::uint64_t>(aSize);
            if (size != 0u && count > ~std::uint64_t{} / size)
            {
                errno_value() = ENOMEM;
                return 0;
            }
            return c_malloc(static_cast<std::int64_t>(count * size));
        }

        void c_free(std::int64_t aBlock) { deallocate(static_cast<address>(aBlock)); }
        void c_free_sized(std::int64_t aBlock, std::int64_t) { deallocate(static_cast<address>(aBlock)); }
        void c_free_aligned_sized(std::int64_t aBlock, std::int64_t, std::int64_t) { deallocate(static_cast<address>(aBlock)); }

        std::int64_t c_realloc(std::int64_t aBlock, std::int64_t aSize)
        {
            auto const r = reallocate(static_cast<address>(aBlock), static_cast<std::uint64_t>(aSize));
            if (r == 0u)
                errno_value() = ENOMEM;
            return static_cast<std::int64_t>(r);
        }

        std::int64_t c_aligned_alloc(std::int64_t aAlignment, std::int64_t aSize)
        {
            auto const alignment = static_cast<std::uint64_t>(aAlignment);
            if (alignment == 0u || (alignment & (alignment - 1u)) != 0u)
                return 0;
            return static_cast<std::int64_t>(allocate_aligned(alignment, static_cast<std::uint64_t>(aSize)));
        }

        std::int64_t c_memalignment(std::int64_t aPointer)
        {
            auto const p = static_cast<std::uint64_t>(aPointer);
            return p == 0u ? 0 : static_cast<std::int64_t>(p & (~p + 1u));
        }

        // --- communication with the environment

        std::vector<address>& exit_functions(bool aQuick)
        {
            static std::vector<address> sAtExit;
            static std::vector<address> sAtQuickExit;
            return aQuick ? sAtQuickExit : sAtExit;
        }

        std::int32_t c_atexit(std::int64_t aFunction)
        {
            exit_functions(false).push_back(static_cast<address>(aFunction));
            return 0;
        }

        std::int32_t c_at_quick_exit(std::int64_t aFunction)
        {
            exit_functions(true).push_back(static_cast<address>(aFunction));
            return 0;
        }

        std::int64_t c_getenv(std::int64_t aName)
        {
            // (a copy in the C memory: the same name's value has the same copy while it is unchanged)
            static std::map<std::string, std::pair<std::string, address>> sValues;
            std::string const name = str(static_cast<address>(aName));
            auto const* value = std::getenv(name.c_str());
            if (value == nullptr)
                return 0;
            auto& entry = sValues[name];
            if (entry.second == 0u || entry.first != value)
            {
                entry.first = value;
                entry.second = copy_string(value);
            }
            return static_cast<std::int64_t>(entry.second);
        }

        std::int32_t c_setenv(std::int64_t aName, std::int64_t aValue, std::int32_t aOverwrite)
        {
            auto const* name = str(static_cast<address>(aName));
            if (!aOverwrite && std::getenv(name) != nullptr)
                return 0;
#if defined(_WIN32)
            return _putenv_s(name, str(static_cast<address>(aValue))) == 0 ? 0 : -1;
#else
            return ::setenv(name, str(static_cast<address>(aValue)), 1);
#endif
        }

        std::int32_t c_unsetenv(std::int64_t aName)
        {
#if defined(_WIN32)
            return _putenv_s(str(static_cast<address>(aName)), "") == 0 ? 0 : -1;
#else
            return ::unsetenv(str(static_cast<address>(aName)));
#endif
        }

        std::int32_t c_system(std::int64_t aCommand)
        {
            std_flush(1);
            if (aCommand == 0)
                return 1;
            return std::system(str(static_cast<address>(aCommand)));
        }

        // --- integer arithmetic

        std::int32_t c_abs(std::int32_t aValue) { return aValue < 0 ? static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(aValue)) : aValue; }
        std::int64_t c_labs(std::int64_t aValue) { return aValue < 0 ? static_cast<std::int64_t>(0u - static_cast<std::uint64_t>(aValue)) : aValue; }

        // --- multibyte characters (UTF-8, in every locale)

        std::int32_t c_mblen(std::int64_t aString, std::int64_t aSize)
        {
            if (aString == 0)
                return 0;
            std::uint32_t code = 0u;
            auto const n = decode_utf8(str(static_cast<address>(aString)), static_cast<std::size_t>(aSize), code);
            return n < 0 ? -1 : n;
        }

        std::int32_t c_mbtowc(std::int64_t aWide, std::int64_t aString, std::int64_t aSize)
        {
            if (aString == 0)
                return 0;
            std::uint32_t code = 0u;
            auto const n = decode_utf8(str(static_cast<address>(aString)), static_cast<std::size_t>(aSize), code);
            if (n < 0)
            {
                errno_value() = EILSEQ;
                return -1;
            }
            if (aWide != 0)
                *at<std::int32_t>(static_cast<address>(aWide)) = static_cast<std::int32_t>(code);
            return n;
        }

        std::int32_t c_wctomb(std::int64_t aString, std::int32_t aWide)
        {
            if (aString == 0)
                return 0;
            std::string utf8;
            append_utf8(utf8, static_cast<std::uint32_t>(aWide));
            std::memcpy(at<char>(static_cast<address>(aString)), utf8.data(), utf8.size());
            return static_cast<std::int32_t>(utf8.size());
        }

        std::int64_t c_mbstowcs(std::int64_t aWide, std::int64_t aString, std::int64_t aCount)
        {
            auto const* s = str(static_cast<address>(aString));
            std::size_t n = 0u;
            auto const length = std::strlen(s);
            std::size_t i = 0u;
            while (aWide == 0 || n < static_cast<std::size_t>(aCount))
            {
                std::uint32_t code = 0u;
                auto const k = decode_utf8(s + i, length + 1u - i, code);
                if (k < 0)
                    return -1;
                if (aWide != 0)
                    at<std::int32_t>(static_cast<address>(aWide))[n] = static_cast<std::int32_t>(code);
                if (k == 0)
                    break;
                i += static_cast<std::size_t>(k);
                ++n;
            }
            return static_cast<std::int64_t>(n);
        }

        std::int64_t c_wcstombs(std::int64_t aString, std::int64_t aWide, std::int64_t aCount)
        {
            auto const* w = at<std::int32_t>(static_cast<address>(aWide));
            std::string out;
            for (std::size_t i = 0u; w[i] != 0; ++i)
                append_utf8(out, static_cast<std::uint32_t>(w[i]));
            if (aString == 0)
                return static_cast<std::int64_t>(out.size());
            auto const n = std::min<std::size_t>(out.size(), static_cast<std::size_t>(aCount));
            std::memcpy(at<char>(static_cast<address>(aString)), out.data(), n);
            if (n < static_cast<std::size_t>(aCount))
                at<char>(static_cast<address>(aString))[n] = '\0';
            return static_cast<std::int64_t>(n);
        }

        // --- <signal.h>: handlers (their descriptors; SIG_DFL 0, SIG_IGN 1, SIG_ERR -1) called by raise

        std::map<int, address>& handlers()
        {
            static std::map<int, address> sHandlers;
            return sHandlers;
        }

        std::int64_t c_signal(std::int32_t aSignal, std::int64_t aHandler)
        {
            if (aSignal <= 0 || aSignal >= 65)
            {
                errno_value() = EINVAL;
                return -1;
            }
            auto& h = handlers()[aSignal];
            auto const previous = h;
            h = static_cast<address>(aHandler);
            return static_cast<std::int64_t>(previous);
        }

        std::int32_t c_raise(std::int32_t aSignal)
        {
            auto const found = handlers().find(aSignal);
            auto const handler = found != handlers().end() ? found->second : 0u;
            if (handler == 1u)
                return 0;
            if (handler == 0u)
            {
                // (the default action: the program terminates (abnormally))
                std::string const message = "C runtime: signal " + std::to_string(aSignal) + " raised\n";
                streams_end();
                std_write(2, message.data(), message.size());
                trap("C program terminated by a signal");
                return -1;
            }
            // (the handler: reset to the default, then called)
            handlers()[aSignal] = 0u;
            call_function(handler, { aSignal }, NEOS_RT_VOID, { NEOS_RT_I32 });
            return 0;
        }

        void c_abort()
        {
            // (abort: a handler for SIGABRT (that the program set) is called first; the library's C code then ends the program)
            auto const found = handlers().find(6);
            if (found != handlers().end() && found->second > 1u)
            {
                auto const handler = found->second;
                found->second = 0u;
                call_function(handler, { 6 }, NEOS_RT_VOID, { NEOS_RT_I32 });
            }
        }

        // --- <locale.h>: the "C" locale (and "", the same); characters are UTF-8

        std::int64_t c_setlocale(std::int32_t, std::int64_t aLocale)
        {
            static address sName = 0u;
            if (sName == 0u)
                sName = copy_string("C");
            if (aLocale == 0)
                return static_cast<std::int64_t>(sName);
            std::string const name = str(static_cast<address>(aLocale));
            if (name.empty() || name == "C" || name == "POSIX" || name.find("UTF-8") != std::string::npos || name.find("utf8") != std::string::npos)
                return static_cast<std::int64_t>(sName);
            return 0;
        }

        std::int64_t c_localeconv()
        {
            // (struct lconv: its char* members, then its char members (see <locale.h>))
            static address sConv = 0u;
            if (sConv == 0u)
            {
                sConv = allocate(160u);
                auto const empty = copy_string("");
                auto const point = copy_string(".");
                auto* p = at<std::uint64_t>(sConv);
                p[0] = point;   // decimal_point
                for (int i = 1; i < 10; ++i)
                    p[i] = empty;   // thousands_sep, grouping, mon_decimal_point, mon_thousands_sep, mon_grouping, positive_sign, negative_sign, currency_symbol, int_curr_symbol
                auto* c = at<char>(sConv + 80u);
                for (int i = 0; i < 14; ++i)
                    c[i] = 127; // (CHAR_MAX: not available)
            }
            return static_cast<std::int64_t>(sConv);
        }

        // --- <inttypes.h>

        std::int64_t c_imaxabs(std::int64_t aValue) { return c_labs(aValue); }

        function_table const sStdlibFunctions
        {
            library("atof", NEOS_RT_FUNCTION(c_atof)), library("atoi", NEOS_RT_FUNCTION(c_atoi)), library("atol", NEOS_RT_FUNCTION(c_atol)),
            library("atoll", NEOS_RT_FUNCTION(c_atol)), library("strtod", NEOS_RT_FUNCTION(c_strtod)), library("strtof", NEOS_RT_FUNCTION(c_strtof)),
            library("strtold", NEOS_RT_FUNCTION(c_strtod)), library("strtol", NEOS_RT_FUNCTION(c_strtol)), library("strtoll", NEOS_RT_FUNCTION(c_strtol)),
            library("strtoul", NEOS_RT_FUNCTION(c_strtoul)), library("strtoull", NEOS_RT_FUNCTION(c_strtoul)),
            library("strfromd", NEOS_RT_FUNCTION(c_strfromd)), library("strfromf", NEOS_RT_FUNCTION(c_strfromf)),
            library("strfroml", NEOS_RT_FUNCTION(c_strfromd)), library("rand", NEOS_RT_FUNCTION(c_rand)), library("srand", NEOS_RT_FUNCTION(c_srand)),
            library("aligned_alloc", NEOS_RT_FUNCTION(c_aligned_alloc)), library("calloc", NEOS_RT_FUNCTION(c_calloc)),
            library("free", NEOS_RT_FUNCTION(c_free)), library("free_sized", NEOS_RT_FUNCTION(c_free_sized)),
            library("free_aligned_sized", NEOS_RT_FUNCTION(c_free_aligned_sized)), library("malloc", NEOS_RT_FUNCTION(c_malloc)),
            library("realloc", NEOS_RT_FUNCTION(c_realloc)), library("atexit", NEOS_RT_FUNCTION(c_atexit)),
            library("at_quick_exit", NEOS_RT_FUNCTION(c_at_quick_exit)), library("getenv", NEOS_RT_FUNCTION(c_getenv)),
            library("system", NEOS_RT_FUNCTION(c_system)), library("abs", NEOS_RT_FUNCTION(c_abs)), library("labs", NEOS_RT_FUNCTION(c_labs)),
            library("llabs", NEOS_RT_FUNCTION(c_labs)), library("mblen", NEOS_RT_FUNCTION(c_mblen)), library("mbtowc", NEOS_RT_FUNCTION(c_mbtowc)),
            library("wctomb", NEOS_RT_FUNCTION(c_wctomb)), library("mbstowcs", NEOS_RT_FUNCTION(c_mbstowcs)),
            library("wcstombs", NEOS_RT_FUNCTION(c_wcstombs)), library("memalignment", NEOS_RT_FUNCTION(c_memalignment)),
            library("setenv", NEOS_RT_FUNCTION(c_setenv)), library("unsetenv", NEOS_RT_FUNCTION(c_unsetenv)),
            library("signal", NEOS_RT_FUNCTION(c_signal)), library("raise", NEOS_RT_FUNCTION(c_raise)),
            library("setlocale", NEOS_RT_FUNCTION(c_setlocale)), library("localeconv", NEOS_RT_FUNCTION(c_localeconv)),
            library("strtoimax", NEOS_RT_FUNCTION(c_strtol)), library("strtoumax", NEOS_RT_FUNCTION(c_strtoul)),
            library("imaxabs", NEOS_RT_FUNCTION(c_imaxabs)), library("__neos_abort", NEOS_RT_FUNCTION(c_abort))
        };
    }

    void exit_handlers(bool aQuick)
    {
        // (in the reverse order of their registration; a handler may register more)
        auto& list = exit_functions(aQuick);
        while (!list.empty())
        {
            auto const f = list.back();
            list.pop_back();
            call_function(f, {}, NEOS_RT_VOID);
        }
    }
}
