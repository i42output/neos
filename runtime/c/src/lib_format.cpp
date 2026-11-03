/*
  lib_format.cpp

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

// The C language's runtime: the printf and scanf families' formatting (7.23.6.1) and scanning (7.23.6.2), of their variable 
// arguments (8-byte slots)

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include "format.hpp"

namespace neos::c_runtime
{
    void append_utf8(std::string& aOut, std::uint32_t aCode)
    {
        if (aCode < 0x80u)
            aOut += static_cast<char>(aCode);
        else if (aCode < 0x800u)
        {
            aOut += static_cast<char>(0xC0u | (aCode >> 6u));
            aOut += static_cast<char>(0x80u | (aCode & 0x3Fu));
        }
        else if (aCode < 0x10000u)
        {
            aOut += static_cast<char>(0xE0u | (aCode >> 12u));
            aOut += static_cast<char>(0x80u | ((aCode >> 6u) & 0x3Fu));
            aOut += static_cast<char>(0x80u | (aCode & 0x3Fu));
        }
        else
        {
            aOut += static_cast<char>(0xF0u | (aCode >> 18u));
            aOut += static_cast<char>(0x80u | ((aCode >> 12u) & 0x3Fu));
            aOut += static_cast<char>(0x80u | ((aCode >> 6u) & 0x3Fu));
            aOut += static_cast<char>(0x80u | (aCode & 0x3Fu));
        }
    }

    int decode_utf8(char const* aText, std::size_t aLength, std::uint32_t& aCode)
    {
        if (aLength == 0u)
            return -2;
        auto const c = static_cast<unsigned char>(aText[0]);
        if (c < 0x80u)
        {
            aCode = c;
            return c == 0u ? 0 : 1;
        }
        int extra = c >= 0xF0u && c < 0xF8u ? 3 : c >= 0xE0u ? 2 : c >= 0xC2u ? 1 : -1;
        if (extra < 0)
            return -1;
        std::uint32_t code = c & (0x3Fu >> extra);
        for (int i = 1; i <= extra; ++i)
        {
            if (static_cast<std::size_t>(i) >= aLength)
                return -2;
            auto const d = static_cast<unsigned char>(aText[i]);
            if ((d & 0xC0u) != 0x80u)
                return -1;
            code = (code << 6u) | (d & 0x3Fu);
        }
        if ((extra == 2 && code < 0x800u) || (extra == 3 && (code < 0x10000u || code > 0x10FFFFu)) || (code >= 0xD800u && code <= 0xDFFFu))
            return -1;
        aCode = code;
        return extra + 1;
    }

    std::string format(char const* aFormat, arguments& aArguments)
    {
        std::string out;
        for (char const* p = aFormat; *p != '\0'; ++p)
        {
            if (*p != '%')
            {
                out += *p;
                continue;
            }
            char const* const start = p;
            ++p;
            if (*p == '%')
            {
                out += '%';
                continue;
            }
            // (flags, width, precision, length)
            bool left = false, plus = false, space = false, alternate = false, zero = false;
            for (;; ++p)
            {
                if (*p == '-') left = true;
                else if (*p == '+') plus = true;
                else if (*p == ' ') space = true;
                else if (*p == '#') alternate = true;
                else if (*p == '0') zero = true;
                else break;
            }
            int width = -1;
            if (*p == '*')
            {
                width = static_cast<int>(aArguments.integer());
                if (width < 0)
                {
                    left = true;
                    width = -width;
                }
                ++p;
            }
            else if (std::isdigit(static_cast<unsigned char>(*p)))
            {
                width = 0;
                while (std::isdigit(static_cast<unsigned char>(*p)))
                    width = width * 10 + (*p++ - '0');
            }
            int precision = -1;
            if (*p == '.')
            {
                ++p;
                if (*p == '*')
                {
                    precision = static_cast<int>(aArguments.integer());
                    if (precision < 0)
                        precision = -1;
                    ++p;
                }
                else
                {
                    precision = 0;
                    while (std::isdigit(static_cast<unsigned char>(*p)))
                        precision = precision * 10 + (*p++ - '0');
                }
            }
            // (length: hh h l ll j z t L, and C23's wN and wfN)
            int bits = 32;
            bool longDouble = false;
            bool wide = false;
            if (*p == 'h')
            {
                ++p;
                bits = 16;
                if (*p == 'h')
                {
                    ++p;
                    bits = 8;
                }
            }
            else if (*p == 'l')
            {
                ++p;
                bits = 64;
                wide = true;
                if (*p == 'l')
                {
                    ++p;
                    wide = false;
                }
            }
            else if (*p == 'j' || *p == 'z' || *p == 't')
            {
                ++p;
                bits = 64;
            }
            else if (*p == 'L')
            {
                ++p;
                longDouble = true;
            }
            else if (*p == 'w')
            {
                ++p;
                if (*p == 'f')
                    ++p;
                bits = 0;
                while (std::isdigit(static_cast<unsigned char>(*p)))
                    bits = bits * 10 + (*p++ - '0');
                if (bits != 8 && bits != 16 && bits != 32 && bits != 64)
                    bits = 64;
            }
            (void)longDouble;
            char const conversion = *p;
            if (conversion == '\0')
            {
                out.append(start, p);
                break;
            }
            auto const pad = [&](std::string aBody, bool aNumeric, std::size_t aPrefix)
            {
                // (the field: padded to the width (zeros after the sign and prefix, if 0 and numeric))
                if (width >= 0 && aBody.size() < static_cast<std::size_t>(width))
                {
                    auto const n = static_cast<std::size_t>(width) - aBody.size();
                    if (left)
                        aBody.append(n, ' ');
                    else if (zero && aNumeric)
                        aBody.insert(aPrefix, n, '0');
                    else
                        aBody.insert(0u, n, ' ');
                }
                out += aBody;
            };
            switch (conversion)
            {
            case 'd':
            case 'i':
            case 'u':
            case 'o':
            case 'x':
            case 'X':
            case 'b':
            case 'B':
            {
                auto raw = static_cast<std::uint64_t>(aArguments.integer());
                bool const isSigned = conversion == 'd' || conversion == 'i';
                bool negative = false;
                std::uint64_t magnitude;
                if (bits < 64)
                {
                    auto const mask = (std::uint64_t{ 1 } << bits) - 1u;
                    raw &= mask;
                    if (isSigned && (raw >> (bits - 1)) != 0u)
                        raw |= ~mask;
                }
                if (isSigned && static_cast<std::int64_t>(raw) < 0)
                {
                    negative = true;
                    magnitude = 0u - raw;
                }
                else
                    magnitude = raw;
                unsigned const base = conversion == 'o' ? 8u : (conversion == 'x' || conversion == 'X') ? 16u : (conversion == 'b' || conversion == 'B') ? 2u : 10u;
                char const* const digits = conversion == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
                std::string number;
                for (auto v = magnitude; v != 0u; v /= base)
                    number.insert(number.begin(), digits[v % base]);
                if (precision >= 0)
                {
                    if (number.size() < static_cast<std::size_t>(precision))
                        number.insert(0u, static_cast<std::size_t>(precision) - number.size(), '0');
                }
                else if (number.empty())
                    number = "0";
                if (alternate && conversion == 'o' && (number.empty() || number[0] != '0'))
                    number.insert(number.begin(), '0');
                std::string prefix;
                if (negative)
                    prefix = "-";
                else if (isSigned && plus)
                    prefix = "+";
                else if (isSigned && space)
                    prefix = " ";
                if (alternate && magnitude != 0u)
                {
                    if (conversion == 'x') prefix += "0x";
                    else if (conversion == 'X') prefix += "0X";
                    else if (conversion == 'b') prefix += "0b";
                    else if (conversion == 'B') prefix += "0B";
                }
                if (precision >= 0)
                    zero = false;
                pad(prefix + number, true, prefix.size());
                break;
            }
            case 'c':
            {
                auto const c = aArguments.integer();
                std::string body;
                if (wide)
                    append_utf8(body, static_cast<std::uint32_t>(c));
                else
                    body += static_cast<char>(c);
                pad(body, false, 0u);
                break;
            }
            case 's':
            {
                auto const a = aArguments.pointer();
                std::string body;
                if (a == 0u)
                    body = "(null)";
                else if (wide)
                {
                    auto const* w = at<std::int32_t>(a);
                    for (std::size_t i = 0u; w[i] != 0; ++i)
                    {
                        std::string c;
                        append_utf8(c, static_cast<std::uint32_t>(w[i]));
                        if (precision >= 0 && body.size() + c.size() > static_cast<std::size_t>(precision))
                            break;
                        body += c;
                    }
                }
                else
                {
                    auto const* s = str(a);
                    std::size_t n = 0u;
                    while ((precision < 0 || n < static_cast<std::size_t>(precision)) && s[n] != '\0')
                        ++n;
                    body.assign(s, n);
                }
                pad(body, false, 0u);
                break;
            }
            case 'p':
            {
                auto const a = aArguments.pointer();
                char buffer[32];
                if (a == 0u)
                    std::snprintf(buffer, sizeof(buffer), "(nil)");
                else
                    std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(a));
                pad(buffer, false, 0u);
                break;
            }
            case 'n':
            {
                auto const a = aArguments.pointer();
                auto const count = static_cast<std::int64_t>(out.size());
                if (a != 0u)
                {
                    switch (bits)
                    {
                    case 8: *at<std::int8_t>(a) = static_cast<std::int8_t>(count); break;
                    case 16: *at<std::int16_t>(a) = static_cast<std::int16_t>(count); break;
                    case 64: *at<std::int64_t>(a) = count; break;
                    default: *at<std::int32_t>(a) = static_cast<std::int32_t>(count); break;
                    }
                }
                break;
            }
            case 'f':
            case 'F':
            case 'e':
            case 'E':
            case 'g':
            case 'G':
            case 'a':
            case 'A':
            {
                // (the host's formatting of the conversion (a long double is a double))
                auto const v = aArguments.floating();
                std::string spec = "%";
                if (left) spec += '-';
                if (plus) spec += '+';
                if (space) spec += ' ';
                if (alternate) spec += '#';
                if (zero) spec += '0';
                if (width >= 0) spec += std::to_string(width);
                if (precision >= 0) spec += "." + std::to_string(precision);
                spec += conversion;
                auto const n = std::snprintf(nullptr, 0, spec.c_str(), v);
                std::string body(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
                std::snprintf(body.data(), body.size() + 1u, spec.c_str(), v);
                out += body;
                break;
            }
            default:
                // (an invalid conversion: as written)
                out.append(start, p + 1);
                break;
            }
        }
        return out;
    }

    int scan(character_source& aSource, char const* aFormat, arguments& aArguments)
    {
        int assigned = 0;
        bool anyConversion = false;
        auto const get = [&]() { int const c = aSource.get(); if (c != EOF) ++aSource.count; return c; };
        auto const unget = [&](int c) { if (c != EOF) { aSource.unget(c); --aSource.count; } };
        auto const skip_space = [&]()
        {
            int c;
            while ((c = get()) != EOF && std::isspace(c))
                ;
            unget(c);
        };
        for (char const* p = aFormat; *p != '\0'; ++p)
        {
            if (std::isspace(static_cast<unsigned char>(*p)))
            {
                skip_space();
                continue;
            }
            if (*p != '%')
            {
                int const c = get();
                if (c != static_cast<unsigned char>(*p))
                {
                    unget(c);
                    if (c == EOF && !anyConversion)
                        return EOF;
                    return assigned;
                }
                continue;
            }
            ++p;
            if (*p == '%')
            {
                skip_space();
                int const c = get();
                if (c != '%')
                {
                    unget(c);
                    return assigned;
                }
                continue;
            }
            bool suppress = false;
            if (*p == '*')
            {
                suppress = true;
                ++p;
            }
            int width = -1;
            if (std::isdigit(static_cast<unsigned char>(*p)))
            {
                width = 0;
                while (std::isdigit(static_cast<unsigned char>(*p)))
                    width = width * 10 + (*p++ - '0');
            }
            int bits = 32;
            bool isDouble = false;
            bool wide = false;
            if (*p == 'h')
            {
                ++p;
                bits = 16;
                if (*p == 'h')
                {
                    ++p;
                    bits = 8;
                }
            }
            else if (*p == 'l')
            {
                ++p;
                bits = 64;
                isDouble = true;
                wide = true;
                if (*p == 'l')
                {
                    ++p;
                    wide = false;
                }
            }
            else if (*p == 'j' || *p == 'z' || *p == 't')
            {
                ++p;
                bits = 64;
            }
            else if (*p == 'L')
            {
                ++p;
                bits = 64;
                isDouble = true;
            }
            else if (*p == 'w')
            {
                ++p;
                if (*p == 'f')
                    ++p;
                bits = 0;
                while (std::isdigit(static_cast<unsigned char>(*p)))
                    bits = bits * 10 + (*p++ - '0');
            }
            char const conversion = *p;
            if (conversion == '\0')
                break;
            auto const store_integer = [&](std::uint64_t aValue)
            {
                auto const a = aArguments.pointer();
                switch (bits)
                {
                case 8: *at<std::uint8_t>(a) = static_cast<std::uint8_t>(aValue); break;
                case 16: *at<std::uint16_t>(a) = static_cast<std::uint16_t>(aValue); break;
                case 64: *at<std::uint64_t>(a) = aValue; break;
                default: *at<std::uint32_t>(a) = static_cast<std::uint32_t>(aValue); break;
                }
            };
            if (conversion == 'n')
            {
                if (!suppress)
                    store_integer(aSource.count);
                continue;
            }
            if (conversion != 'c' && conversion != '[')
                skip_space();
            int const limit = width < 0 ? (conversion == 'c' ? 1 : 0x7FFFFFFF) : width;
            switch (conversion)
            {
            case 'd':
            case 'i':
            case 'u':
            case 'o':
            case 'x':
            case 'X':
            case 'b':
            case 'p':
            {
                // (the longest prefix of the input that is a number of the base (i: of its prefix's base))
                std::string text;
                int c = get();
                auto const take = [&]() { text += static_cast<char>(c); c = get(); };
                if ((c == '+' || c == '-') && static_cast<int>(text.size()) < limit)
                    take();
                int base = conversion == 'o' ? 8 : (conversion == 'x' || conversion == 'X' || conversion == 'p') ? 16 : conversion == 'b' ? 2 : conversion == 'i' ? 0 : 10;
                if ((base == 0 || base == 16 || base == 2) && c == '0' && static_cast<int>(text.size()) < limit)
                {
                    take();
                    if ((base == 0 || base == 16) && (c == 'x' || c == 'X') && static_cast<int>(text.size()) < limit)
                    {
                        take();
                        base = 16;
                    }
                    else if ((base == 0 || base == 2) && (c == 'b' || c == 'B') && static_cast<int>(text.size()) < limit)
                    {
                        take();
                        base = 2;
                    }
                    else if (base == 0)
                        base = 8;
                }
                if (base == 0)
                    base = 10;
                bool digits = !text.empty() && text.back() == '0';
                while (c != EOF && static_cast<int>(text.size()) < limit)
                {
                    int d = std::isdigit(c) ? c - '0' : std::isalpha(c) ? std::tolower(c) - 'a' + 10 : 99;
                    if (d >= base)
                        break;
                    digits = true;
                    take();
                }
                unget(c);
                if (!digits)
                    return c == EOF && !anyConversion ? EOF : assigned;
                anyConversion = true;
                std::string clean = text;
                if (base == 2)
                {
                    // (strtoull has no binary prefix before C23 hosts: the digits alone)
                    auto const at = clean.find_first_of("bB");
                    if (at != std::string::npos)
                        clean.erase(0u, at + 1u);
                    if (!text.empty() && text[0] == '-')
                        clean.insert(clean.begin(), '-');
                }
                auto const value = (conversion == 'd' || (conversion == 'i')) ? static_cast<std::uint64_t>(std::strtoll(clean.c_str(), nullptr, base)) :
                    static_cast<std::uint64_t>(std::strtoull(clean.c_str(), nullptr, base));
                if (!suppress)
                {
                    if (conversion == 'p')
                        bits = 64;
                    store_integer(value);
                    ++assigned;
                }
                break;
            }
            case 'f':
            case 'F':
            case 'e':
            case 'E':
            case 'g':
            case 'G':
            case 'a':
            case 'A':
            {
                std::string text;
                int c = get();
                auto const take = [&]() { text += static_cast<char>(c); c = get(); };
                if ((c == '+' || c == '-') && static_cast<int>(text.size()) < limit)
                    take();
                // (infinity and NaN)
                if (c == 'i' || c == 'I' || c == 'n' || c == 'N')
                {
                    while (c != EOF && std::isalpha(c) && static_cast<int>(text.size()) < limit)
                        take();
                }
                else
                {
                    bool hex = false;
                    if (c == '0' && static_cast<int>(text.size()) < limit)
                    {
                        take();
                        if ((c == 'x' || c == 'X') && static_cast<int>(text.size()) < limit)
                        {
                            take();
                            hex = true;
                        }
                    }
                    auto const digit = [&](int d) { return hex ? std::isxdigit(d) != 0 : std::isdigit(d) != 0; };
                    while (c != EOF && digit(c) && static_cast<int>(text.size()) < limit)
                        take();
                    if (c == '.' && static_cast<int>(text.size()) < limit)
                    {
                        take();
                        while (c != EOF && digit(c) && static_cast<int>(text.size()) < limit)
                            take();
                    }
                    if ((hex ? (c == 'p' || c == 'P') : (c == 'e' || c == 'E')) && static_cast<int>(text.size()) < limit)
                    {
                        take();
                        if ((c == '+' || c == '-') && static_cast<int>(text.size()) < limit)
                            take();
                        while (c != EOF && std::isdigit(c) && static_cast<int>(text.size()) < limit)
                            take();
                    }
                }
                unget(c);
                char* end = nullptr;
                double const value = std::strtod(text.c_str(), &end);
                if (text.empty() || end == text.c_str())
                    return c == EOF && !anyConversion ? EOF : assigned;
                anyConversion = true;
                if (!suppress)
                {
                    auto const a = aArguments.pointer();
                    if (isDouble)
                        std::memcpy(at<std::uint8_t>(a), &value, 8u);
                    else
                    {
                        float const f = static_cast<float>(value);
                        std::memcpy(at<std::uint8_t>(a), &f, 4u);
                    }
                    ++assigned;
                }
                break;
            }
            case 's':
            case 'c':
            case '[':
            {
                // (a set: [...] or [^...] (a ']' first is a member))
                bool inSet[256] = {};
                if (conversion == '[')
                {
                    ++p;
                    bool negate = false;
                    if (*p == '^')
                    {
                        negate = true;
                        ++p;
                    }
                    bool first = true;
                    for (; *p != '\0' && (*p != ']' || first); ++p)
                    {
                        first = false;
                        if (p[1] == '-' && p[2] != ']' && p[2] != '\0')
                        {
                            for (int x = static_cast<unsigned char>(p[0]); x <= static_cast<unsigned char>(p[2]); ++x)
                                inSet[x] = true;
                            p += 2;
                        }
                        else
                            inSet[static_cast<unsigned char>(*p)] = true;
                    }
                    if (negate)
                        for (auto& b : inSet)
                            b = !b;
                    if (*p == '\0')
                        --p;
                }
                auto const a = suppress ? 0u : aArguments.pointer();
                int n = 0;
                int c = get();
                if (c == EOF)
                    return anyConversion ? assigned : EOF;
                while (c != EOF && n < limit)
                {
                    if (conversion == 's' && std::isspace(c))
                        break;
                    if (conversion == '[' && !inSet[c])
                        break;
                    if (a != 0u)
                    {
                        if (wide)
                            at<std::int32_t>(a)[n] = c;
                        else
                            at<char>(a)[n] = static_cast<char>(c);
                    }
                    ++n;
                    c = get();
                }
                unget(c);
                if (n == 0)
                    return assigned;
                if (conversion == 'c' && n < limit)
                    return assigned;
                if (a != 0u && conversion != 'c')
                {
                    if (wide)
                        at<std::int32_t>(a)[n] = 0;
                    else
                        at<char>(a)[n] = '\0';
                }
                anyConversion = true;
                if (!suppress)
                    ++assigned;
                break;
            }
            default:
                return assigned;
            }
        }
        return assigned;
    }
}
