/*
  lib_wchar.cpp

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

// The C language's runtime: <wchar.h> (7.31), <uchar.h> (7.30) and <wctype.h> (7.32): wchar_t is a 32-bit int (UTF-32); multibyte
// characters are UTF-8 (in every locale); wide streams' characters are written and read as UTF-8

#include <cerrno>
#include <cstdlib>
#include <cwctype>
#include <clocale>
#include <locale.h>
#include <wctype.h>
#include <vector>
#include <string>
#include "format.hpp"

namespace neos::c_runtime
{
    std::size_t stream_write(std::int64_t aFile, char const* aData, std::size_t aCount);
    int stream_get(std::int64_t aFile);
    void stream_unget(std::int64_t aFile, int aCharacter);

    namespace
    {
        inline std::int32_t* w(std::int64_t aAddress) { return at<std::int32_t>(static_cast<address>(aAddress)); }

        std::int64_t c_wcslen(std::int64_t aString)
        {
            auto const* s = w(aString);
            std::int64_t n = 0;
            while (s[n] != 0)
                ++n;
            return n;
        }

        std::int64_t c_wcscpy(std::int64_t aTo, std::int64_t aFrom)
        {
            auto const n = c_wcslen(aFrom);
            std::memmove(w(aTo), w(aFrom), static_cast<std::size_t>(n + 1) * 4u);
            return aTo;
        }

        std::int64_t c_wcsncpy(std::int64_t aTo, std::int64_t aFrom, std::int64_t aCount)
        {
            std::int64_t i = 0;
            for (; i < aCount && w(aFrom)[i] != 0; ++i)
                w(aTo)[i] = w(aFrom)[i];
            for (; i < aCount; ++i)
                w(aTo)[i] = 0;
            return aTo;
        }

        std::int64_t c_wcscat(std::int64_t aTo, std::int64_t aFrom)
        {
            auto const n = c_wcslen(aTo);
            c_wcscpy(aTo + n * 4, aFrom);
            return aTo;
        }

        std::int32_t c_wcsncmp(std::int64_t aA, std::int64_t aB, std::int64_t aCount)
        {
            for (std::int64_t i = 0; i < aCount; ++i)
            {
                auto const x = w(aA)[i], y = w(aB)[i];
                if (x != y)
                    return x < y ? -1 : 1;
                if (x == 0)
                    return 0;
            }
            return 0;
        }

        std::int32_t c_wcscmp(std::int64_t aA, std::int64_t aB) { return c_wcsncmp(aA, aB, INT64_MAX); }

        std::int64_t c_wcschr(std::int64_t aString, std::int32_t aCharacter)
        {
            for (std::int64_t i = 0;; ++i)
            {
                if (w(aString)[i] == aCharacter)
                    return aString + i * 4;
                if (w(aString)[i] == 0)
                    return 0;
            }
        }

        std::int64_t c_wcsrchr(std::int64_t aString, std::int32_t aCharacter)
        {
            std::int64_t result = 0;
            for (std::int64_t i = 0;; ++i)
            {
                if (w(aString)[i] == aCharacter)
                    result = aString + i * 4;
                if (w(aString)[i] == 0)
                    return result;
            }
        }

        std::int64_t c_wcsstr(std::int64_t aString, std::int64_t aFind)
        {
            auto const n = c_wcslen(aFind);
            auto const m = c_wcslen(aString);
            for (std::int64_t i = 0; i + n <= m; ++i)
                if (std::memcmp(w(aString) + i, w(aFind), static_cast<std::size_t>(n) * 4u) == 0)
                    return aString + i * 4;
            return 0;
        }

        std::int64_t c_wmemcpy(std::int64_t aTo, std::int64_t aFrom, std::int64_t aCount)
        {
            if (aCount != 0)
                std::memmove(w(aTo), w(aFrom), static_cast<std::size_t>(aCount) * 4u);
            return aTo;
        }

        std::int64_t c_wmemset(std::int64_t aTo, std::int32_t aCharacter, std::int64_t aCount)
        {
            for (std::int64_t i = 0; i < aCount; ++i)
                w(aTo)[i] = aCharacter;
            return aTo;
        }

        std::int32_t c_wmemcmp(std::int64_t aA, std::int64_t aB, std::int64_t aCount)
        {
            for (std::int64_t i = 0; i < aCount; ++i)
                if (w(aA)[i] != w(aB)[i])
                    return w(aA)[i] < w(aB)[i] ? -1 : 1;
            return 0;
        }

        std::int64_t c_wmemchr(std::int64_t aString, std::int32_t aCharacter, std::int64_t aCount)
        {
            for (std::int64_t i = 0; i < aCount; ++i)
                if (w(aString)[i] == aCharacter)
                    return aString + i * 4;
            return 0;
        }

        // --- conversions (mbstate_t: its first 4 bytes hold an incomplete sequence's bytes count, then the bytes)

        std::int64_t c_mbrtowc(std::int64_t aWide, std::int64_t aString, std::int64_t aCount, std::int64_t)
        {
            if (aString == 0)
                return 0;
            std::uint32_t code = 0u;
            auto const n = decode_utf8(str(static_cast<address>(aString)), static_cast<std::size_t>(aCount), code);
            if (n == -1)
            {
                errno_value() = EILSEQ;
                return -1;
            }
            if (n == -2)
                return -2;
            if (aWide != 0)
                *w(aWide) = static_cast<std::int32_t>(code);
            return n;
        }

        std::int64_t c_wcrtomb(std::int64_t aString, std::int32_t aWide, std::int64_t)
        {
            if (aString == 0)
                return 1;
            if (aWide < 0 || aWide > 0x10FFFF || (aWide >= 0xD800 && aWide <= 0xDFFF))
            {
                errno_value() = EILSEQ;
                return -1;
            }
            std::string utf8;
            append_utf8(utf8, static_cast<std::uint32_t>(aWide));
            std::memcpy(str(static_cast<address>(aString)), utf8.data(), utf8.size());
            return static_cast<std::int64_t>(utf8.size());
        }

        std::int64_t c_mbrlen(std::int64_t aString, std::int64_t aCount, std::int64_t aState) { return c_mbrtowc(0, aString, aCount, aState); }
        std::int32_t c_mbsinit(std::int64_t) { return 1; }
        std::int32_t c_btowc(std::int32_t aCharacter) { return aCharacter >= 0 && aCharacter < 128 ? aCharacter : -1; }
        std::int32_t c_wctob(std::int32_t aWide) { return aWide >= 0 && aWide < 128 ? aWide : -1; }

        std::int64_t c_mbsrtowcs(std::int64_t aWide, std::int64_t aSource, std::int64_t aCount, std::int64_t)
        {
            auto& source = *at<std::uint64_t>(static_cast<address>(aSource));
            auto const* s = str(source);
            auto const length = std::strlen(s);
            std::int64_t n = 0;
            std::size_t i = 0u;
            while (aWide == 0 || n < aCount)
            {
                std::uint32_t code = 0u;
                auto const k = decode_utf8(s + i, length + 1u - i, code);
                if (k < 0)
                {
                    errno_value() = EILSEQ;
                    return -1;
                }
                if (aWide != 0)
                    w(aWide)[n] = static_cast<std::int32_t>(code);
                if (k == 0)
                {
                    if (aWide != 0)
                        source = 0u;
                    return n;
                }
                i += static_cast<std::size_t>(k);
                ++n;
            }
            source += i;
            return n;
        }

        std::int64_t c_wcsrtombs(std::int64_t aString, std::int64_t aSource, std::int64_t aCount, std::int64_t)
        {
            auto& source = *at<std::uint64_t>(static_cast<address>(aSource));
            std::string out;
            std::size_t i = 0u;
            for (; w(static_cast<std::int64_t>(source))[i] != 0; ++i)
            {
                std::string c;
                append_utf8(c, static_cast<std::uint32_t>(w(static_cast<std::int64_t>(source))[i]));
                if (aString != 0 && out.size() + c.size() > static_cast<std::size_t>(aCount))
                    break;
                out += c;
            }
            bool const complete = w(static_cast<std::int64_t>(source))[i] == 0;
            if (aString != 0)
            {
                std::memcpy(str(static_cast<address>(aString)), out.data(), out.size());
                if (complete && out.size() < static_cast<std::size_t>(aCount))
                {
                    str(static_cast<address>(aString))[out.size()] = '\0';
                    source = 0u;
                }
                else
                    source += i * 4u;
            }
            return static_cast<std::int64_t>(out.size());
        }

        // (char16_t: UTF-16; a surrogate pair's second unit is returned by the next call (its state: a pending unit))
        std::int64_t c_mbrtoc16(std::int64_t aWide, std::int64_t aString, std::int64_t aCount, std::int64_t aState)
        {
            auto* pending = aState != 0 ? at<std::uint32_t>(static_cast<address>(aState)) : nullptr;
            static std::uint32_t sPending = 0u;
            if (pending == nullptr)
                pending = &sPending;
            if (*pending != 0u)
            {
                if (aWide != 0)
                    *at<std::uint16_t>(static_cast<address>(aWide)) = static_cast<std::uint16_t>(*pending);
                *pending = 0u;
                return -3;
            }
            std::uint32_t c = 0u;
            auto const k = aString == 0 ? 0 : decode_utf8(str(static_cast<address>(aString)), static_cast<std::size_t>(aCount), c);
            if (k < 0)
                return k == -1 ? (errno_value() = EILSEQ, -1) : -2;
            if (c >= 0x10000u)
            {
                c -= 0x10000u;
                if (aWide != 0)
                    *at<std::uint16_t>(static_cast<address>(aWide)) = static_cast<std::uint16_t>(0xD800u + (c >> 10u));
                *pending = 0xDC00u + (c & 0x3FFu);
            }
            else if (aWide != 0)
                *at<std::uint16_t>(static_cast<address>(aWide)) = static_cast<std::uint16_t>(c);
            return k;
        }

        std::int64_t c_c16rtomb(std::int64_t aString, std::int32_t aUnit, std::int64_t aState)
        {
            static std::uint32_t sHigh = 0u;
            auto* high = aState != 0 ? at<std::uint32_t>(static_cast<address>(aState)) : &sHigh;
            auto const unit = static_cast<std::uint32_t>(aUnit) & 0xFFFFu;
            if (unit >= 0xD800u && unit < 0xDC00u)
            {
                *high = unit;
                return 0;
            }
            std::uint32_t code = unit;
            if (unit >= 0xDC00u && unit < 0xE000u)
            {
                if (*high == 0u)
                {
                    errno_value() = EILSEQ;
                    return -1;
                }
                code = 0x10000u + ((*high - 0xD800u) << 10u) + (unit - 0xDC00u);
                *high = 0u;
            }
            return c_wcrtomb(aString, static_cast<std::int32_t>(code), 0);
        }

        std::int64_t c_mbrtoc32(std::int64_t aWide, std::int64_t aString, std::int64_t aCount, std::int64_t aState) { return c_mbrtowc(aWide, aString, aCount, aState); }
        std::int64_t c_c32rtomb(std::int64_t aString, std::int32_t aWide, std::int64_t aState) { return c_wcrtomb(aString, aWide, aState); }

        std::int64_t c_mbrtoc8(std::int64_t aUnit, std::int64_t aString, std::int64_t aCount, std::int64_t)
        {
            // (char8_t: UTF-8 code units, one at a time)
            if (aString == 0 || aCount == 0)
                return aCount == 0 ? -2 : 0;
            auto const c = static_cast<unsigned char>(*str(static_cast<address>(aString)));
            if (aUnit != 0)
                *at<std::uint8_t>(static_cast<address>(aUnit)) = c;
            return c == 0u ? 0 : 1;
        }

        std::int64_t c_c8rtomb(std::int64_t aString, std::int32_t aUnit, std::int64_t)
        {
            if (aString != 0)
                *str(static_cast<address>(aString)) = static_cast<char>(aUnit);
            return 1;
        }

        // --- <wctype.h>: the code point's classification (the basic characters' by C's rules; others' by the host's, in a UTF-8 
        // locale (wide characters are Unicode's code points, whatever the program's locale: see setlocale))

#if defined(_WIN32)
        _locale_t unicode_locale()
        {
            static _locale_t const sLocale = _create_locale(LC_CTYPE, ".UTF-8");
            return sLocale;
        }
        // (the host's wide characters are UTF-16's units: a code point beyond the basic multilingual plane is not classified)
#define NEOS_C_WCTYPE(name, host) \
        std::int32_t c_##name(std::int32_t c) \
        { \
            if (c >= 0 && c < 0x80) \
                return std::name(static_cast<std::wint_t>(c)) != 0; \
            if (c < 0 || c > 0xFFFF || unicode_locale() == nullptr) \
                return 0; \
            return host(static_cast<wint_t>(c), unicode_locale()) != 0; \
        }
#define NEOS_C_WCTRANS(name, host) \
        std::int32_t c_##name(std::int32_t c) \
        { \
            if (c >= 0 && c < 0x80) \
                return static_cast<std::int32_t>(std::name(static_cast<std::wint_t>(c))); \
            if (c < 0 || c > 0xFFFF || unicode_locale() == nullptr) \
                return c; \
            return static_cast<std::int32_t>(host(static_cast<wint_t>(c), unicode_locale())); \
        }
        NEOS_C_WCTYPE(iswalnum, _iswalnum_l)
        NEOS_C_WCTYPE(iswalpha, _iswalpha_l)
        NEOS_C_WCTYPE(iswblank, _iswblank_l)
        NEOS_C_WCTYPE(iswcntrl, _iswcntrl_l)
        NEOS_C_WCTYPE(iswgraph, _iswgraph_l)
        NEOS_C_WCTYPE(iswlower, _iswlower_l)
        NEOS_C_WCTYPE(iswprint, _iswprint_l)
        NEOS_C_WCTYPE(iswpunct, _iswpunct_l)
        NEOS_C_WCTYPE(iswspace, _iswspace_l)
        NEOS_C_WCTYPE(iswupper, _iswupper_l)
        NEOS_C_WCTRANS(towlower, _towlower_l)
        NEOS_C_WCTRANS(towupper, _towupper_l)
#else
        locale_t unicode_locale()
        {
            static locale_t const sLocale = []()
            {
                for (char const* name : { "C.UTF-8", "C.utf8", "en_US.UTF-8", "en_US.utf8" })
                    if (auto const l = ::newlocale(LC_CTYPE_MASK, name, static_cast<locale_t>(0)); l != static_cast<locale_t>(0))
                        return l;
                return static_cast<locale_t>(0);
            }();
            return sLocale;
        }
#define NEOS_C_WCTYPE(name, host) \
        std::int32_t c_##name(std::int32_t c) \
        { \
            if (c >= 0 && c < 0x80) \
                return std::name(static_cast<std::wint_t>(c)) != 0; \
            if (c < 0 || unicode_locale() == static_cast<locale_t>(0)) \
                return 0; \
            return ::host(static_cast<wint_t>(c), unicode_locale()) != 0; \
        }
#define NEOS_C_WCTRANS(name, host) \
        std::int32_t c_##name(std::int32_t c) \
        { \
            if (c >= 0 && c < 0x80) \
                return static_cast<std::int32_t>(std::name(static_cast<std::wint_t>(c))); \
            if (c < 0 || unicode_locale() == static_cast<locale_t>(0)) \
                return c; \
            return static_cast<std::int32_t>(::host(static_cast<wint_t>(c), unicode_locale())); \
        }
        NEOS_C_WCTYPE(iswalnum, iswalnum_l)
        NEOS_C_WCTYPE(iswalpha, iswalpha_l)
        NEOS_C_WCTYPE(iswblank, iswblank_l)
        NEOS_C_WCTYPE(iswcntrl, iswcntrl_l)
        NEOS_C_WCTYPE(iswgraph, iswgraph_l)
        NEOS_C_WCTYPE(iswlower, iswlower_l)
        NEOS_C_WCTYPE(iswprint, iswprint_l)
        NEOS_C_WCTYPE(iswpunct, iswpunct_l)
        NEOS_C_WCTYPE(iswspace, iswspace_l)
        NEOS_C_WCTYPE(iswupper, iswupper_l)
        NEOS_C_WCTRANS(towlower, towlower_l)
        NEOS_C_WCTRANS(towupper, towupper_l)
#endif
#undef NEOS_C_WCTYPE
#undef NEOS_C_WCTRANS
        std::int32_t c_iswdigit(std::int32_t c) { return c >= '0' && c <= '9'; }
        std::int32_t c_iswxdigit(std::int32_t c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

        // --- wide character input and output (as UTF-8)

        std::int32_t c_fputwc(std::int32_t aWide, std::int64_t aFile)
        {
            std::string utf8;
            append_utf8(utf8, static_cast<std::uint32_t>(aWide));
            return stream_write(aFile, utf8.data(), utf8.size()) == utf8.size() ? aWide : -1;
        }

        std::int32_t c_putwchar(std::int32_t aWide) { return c_fputwc(aWide, static_cast<std::int64_t>(language::c::layout::Streams + language::c::layout::StreamSize)); }

        std::int32_t c_fgetwc(std::int64_t aFile)
        {
            char bytes[4];
            int n = 0;
            for (;;)
            {
                int const c = stream_get(aFile);
                if (c == EOF)
                    return -1;
                bytes[n++] = static_cast<char>(c);
                std::uint32_t code = 0u;
                auto const k = decode_utf8(bytes, static_cast<std::size_t>(n), code);
                if (k > 0 || (k == 0 && n == 1))
                    return static_cast<std::int32_t>(code);
                if (k == -1 || n == 4)
                {
                    errno_value() = EILSEQ;
                    return -1;
                }
            }
        }

        std::int32_t c_getwchar() { return c_fgetwc(static_cast<std::int64_t>(language::c::layout::Streams)); }

        std::int32_t c_fputws(std::int64_t aString, std::int64_t aFile)
        {
            std::string out;
            for (auto const* s = w(aString); *s != 0; ++s)
                append_utf8(out, static_cast<std::uint32_t>(*s));
            return stream_write(aFile, out.data(), out.size()) == out.size() ? 0 : -1;
        }

        std::string narrow(std::int64_t aWide)
        {
            std::string out;
            for (auto const* s = w(aWide); *s != 0; ++s)
                append_utf8(out, static_cast<std::uint32_t>(*s));
            return out;
        }

        std::int64_t c_wcstol(std::int64_t aString, std::int64_t aEnd, std::int32_t aBase)
        {
            auto const text = narrow(aString);
            char* end = nullptr;
            errno = 0;
            auto const v = std::strtoll(text.c_str(), &end, aBase);
            if (errno != 0)
                errno_value() = errno;
            if (aEnd != 0)
                *at<std::uint64_t>(static_cast<address>(aEnd)) = static_cast<address>(aString) + static_cast<address>(end - text.c_str()) * 4u;
            return static_cast<std::int64_t>(v);
        }

        std::int64_t c_wcstoul(std::int64_t aString, std::int64_t aEnd, std::int32_t aBase)
        {
            auto const text = narrow(aString);
            char* end = nullptr;
            errno = 0;
            auto const v = std::strtoull(text.c_str(), &end, aBase);
            if (errno != 0)
                errno_value() = errno;
            if (aEnd != 0)
                *at<std::uint64_t>(static_cast<address>(aEnd)) = static_cast<address>(aString) + static_cast<address>(end - text.c_str()) * 4u;
            return static_cast<std::int64_t>(v);
        }

        double c_wcstod(std::int64_t aString, std::int64_t aEnd)
        {
            auto const text = narrow(aString);
            char* end = nullptr;
            auto const v = std::strtod(text.c_str(), &end);
            if (aEnd != 0)
                *at<std::uint64_t>(static_cast<address>(aEnd)) = static_cast<address>(aString) + static_cast<address>(end - text.c_str()) * 4u;
            return v;
        }

        // (the wide printf functions: the format (wide) as UTF-8, formatted as printf's (%ls and %lc are wide in both))
        std::string wide_format(std::int64_t aFormat, std::int64_t aArguments)
        {
            auto const format_text = narrow(aFormat);
            arguments args{ static_cast<address>(aArguments) };
            return format(format_text.c_str(), args);
        }

        std::int32_t c_vfwprintf(std::int64_t aFile, std::int64_t aFormat, std::int64_t aArguments)
        {
            auto const text = wide_format(aFormat, aArguments);
            if (stream_write(aFile, text.data(), text.size()) != text.size())
                return -1;
            std::int32_t count = 0;
            for (std::size_t i = 0u; i < text.size(); ++i)
                if ((static_cast<unsigned char>(text[i]) & 0xC0u) != 0x80u)
                    ++count;
            return count;
        }

        std::int32_t c_vwprintf(std::int64_t aFormat, std::int64_t aArguments)
        {
            return c_vfwprintf(static_cast<std::int64_t>(language::c::layout::Streams + language::c::layout::StreamSize), aFormat, aArguments);
        }

        std::int32_t c_vswprintf(std::int64_t aBuffer, std::int64_t aSize, std::int64_t aFormat, std::int64_t aArguments)
        {
            auto const text = wide_format(aFormat, aArguments);
            std::vector<std::int32_t> units;
            for (std::size_t i = 0u; i < text.size();)
            {
                std::uint32_t code = 0u;
                auto const k = decode_utf8(text.data() + i, text.size() - i, code);
                if (k <= 0)
                {
                    code = static_cast<unsigned char>(text[i]);
                    ++i;
                }
                else
                    i += static_cast<std::size_t>(k);
                units.push_back(static_cast<std::int32_t>(code));
            }
            if (aSize <= 0)
                return -1;
            auto const n = std::min<std::size_t>(units.size(), static_cast<std::size_t>(aSize) - 1u);
            for (std::size_t i = 0u; i < n; ++i)
                w(aBuffer)[i] = units[i];
            w(aBuffer)[n] = 0;
            return units.size() < static_cast<std::size_t>(aSize) ? static_cast<std::int32_t>(units.size()) : -1;
        }

        function_table const sWideFunctions
        {
            library("wcslen", NEOS_RT_FUNCTION(c_wcslen)), library("wcscpy", NEOS_RT_FUNCTION(c_wcscpy)),
            library("wcsncpy", NEOS_RT_FUNCTION(c_wcsncpy)), library("wcscat", NEOS_RT_FUNCTION(c_wcscat)),
            library("wcscmp", NEOS_RT_FUNCTION(c_wcscmp)), library("wcsncmp", NEOS_RT_FUNCTION(c_wcsncmp)),
            library("wcschr", NEOS_RT_FUNCTION(c_wcschr)), library("wcsrchr", NEOS_RT_FUNCTION(c_wcsrchr)),
            library("wcsstr", NEOS_RT_FUNCTION(c_wcsstr)), library("wmemcpy", NEOS_RT_FUNCTION(c_wmemcpy)),
            library("wmemmove", NEOS_RT_FUNCTION(c_wmemcpy)), library("wmemset", NEOS_RT_FUNCTION(c_wmemset)),
            library("wmemcmp", NEOS_RT_FUNCTION(c_wmemcmp)), library("wmemchr", NEOS_RT_FUNCTION(c_wmemchr)),
            library("mbrtowc", NEOS_RT_FUNCTION(c_mbrtowc)), library("wcrtomb", NEOS_RT_FUNCTION(c_wcrtomb)),
            library("mbrlen", NEOS_RT_FUNCTION(c_mbrlen)), library("mbsinit", NEOS_RT_FUNCTION(c_mbsinit)),
            library("btowc", NEOS_RT_FUNCTION(c_btowc)), library("wctob", NEOS_RT_FUNCTION(c_wctob)),
            library("mbsrtowcs", NEOS_RT_FUNCTION(c_mbsrtowcs)), library("wcsrtombs", NEOS_RT_FUNCTION(c_wcsrtombs)),
            library("mbrtoc16", NEOS_RT_FUNCTION(c_mbrtoc16)), library("c16rtomb", NEOS_RT_FUNCTION(c_c16rtomb)),
            library("mbrtoc32", NEOS_RT_FUNCTION(c_mbrtoc32)), library("c32rtomb", NEOS_RT_FUNCTION(c_c32rtomb)),
            library("mbrtoc8", NEOS_RT_FUNCTION(c_mbrtoc8)), library("c8rtomb", NEOS_RT_FUNCTION(c_c8rtomb)),
            library("iswalnum", NEOS_RT_FUNCTION(c_iswalnum)), library("iswalpha", NEOS_RT_FUNCTION(c_iswalpha)),
            library("iswblank", NEOS_RT_FUNCTION(c_iswblank)), library("iswcntrl", NEOS_RT_FUNCTION(c_iswcntrl)),
            library("iswdigit", NEOS_RT_FUNCTION(c_iswdigit)), library("iswgraph", NEOS_RT_FUNCTION(c_iswgraph)),
            library("iswlower", NEOS_RT_FUNCTION(c_iswlower)), library("iswprint", NEOS_RT_FUNCTION(c_iswprint)),
            library("iswpunct", NEOS_RT_FUNCTION(c_iswpunct)), library("iswspace", NEOS_RT_FUNCTION(c_iswspace)),
            library("iswupper", NEOS_RT_FUNCTION(c_iswupper)), library("iswxdigit", NEOS_RT_FUNCTION(c_iswxdigit)),
            library("towlower", NEOS_RT_FUNCTION(c_towlower)), library("towupper", NEOS_RT_FUNCTION(c_towupper)),
            library("fputwc", NEOS_RT_FUNCTION(c_fputwc)), library("putwchar", NEOS_RT_FUNCTION(c_putwchar)),
            library("fgetwc", NEOS_RT_FUNCTION(c_fgetwc)), library("getwchar", NEOS_RT_FUNCTION(c_getwchar)),
            library("fputws", NEOS_RT_FUNCTION(c_fputws)), library("wcstol", NEOS_RT_FUNCTION(c_wcstol)),
            library("wcstoul", NEOS_RT_FUNCTION(c_wcstoul)), library("wcstod", NEOS_RT_FUNCTION(c_wcstod)),
            library("swprintf", NEOS_RT_FUNCTION(c_vswprintf)), library("vswprintf", NEOS_RT_FUNCTION(c_vswprintf)),
            library("wprintf", NEOS_RT_FUNCTION(c_vwprintf)), library("vwprintf", NEOS_RT_FUNCTION(c_vwprintf)),
            library("fwprintf", NEOS_RT_FUNCTION(c_vfwprintf)), library("vfwprintf", NEOS_RT_FUNCTION(c_vfwprintf))
        };
    }
}
