/*
  ecmascript.hpp

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

  Portions of this file are derived from V8 (src/numbers/conversions.cc: DoubleToRadixCString, now
  DoubleToRadixStringView), Copyright 2011 the V8 project authors. All rights reserved. They are used under V8's
  BSD-style licence (see include/neos/bytecode/LICENSE.v8). Changes: translated to C++ for the neos JavaScript compiler
  (see compilers/javascript/NOTICE).

  cbrt is derived from FreeBSD's msun (lib/msun/src/s_cbrt.c), which carries this notice:

    ====================================================
    Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.

    Developed at SunPro, a Sun Microsystems, Inc. business.
    Permission to use, copy, modify, and distribute this
    software is freely granted, provided that this notice
    is preserved.
    ====================================================

    Optimized by Bruce D. Evans.
*/

#pragma once

// ECMAScript (JavaScript) conversions shared by the compiler (constant folding) and the JavaScript runtime plugin:
// Number::toString, Number.prototype.toFixed, StringToNumber, parseInt, parseFloat, ToInt32, Math.pow and
// string indexing in UTF-16 code units (strings are stored as UTF-8)

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <charconv>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace neos::ecmascript
{
    // Number::toString(x) (shortest digits that round trip; exponential notation outside 1e-7 <= |x| < 1e21)
    inline std::string number_to_string(double aValue)
    {
        if (std::isnan(aValue))
            return "NaN";
        if (aValue == 0.0)
            return "0";
        if (std::isinf(aValue))
            return aValue < 0.0 ? "-Infinity" : "Infinity";
        std::string const sign = aValue < 0.0 ? "-" : "";
        char buffer[64];
        auto const result = std::to_chars(buffer, buffer + sizeof(buffer), std::fabs(aValue), std::chars_format::scientific);
        std::string_view const scientific{ buffer, static_cast<std::size_t>(result.ptr - buffer) };
        auto const e = scientific.find('e');
        std::string digits;
        for (auto c : scientific.substr(0u, e))
            if (c != '.')
                digits += c;
        int exponent = 0;
        std::from_chars(scientific.data() + e + (scientific[e + 1u] == '+' ? 2u : 1u), scientific.data() + scientific.size(), exponent);
        int const k = static_cast<int>(digits.size());
        int const n = exponent + 1;
        if (k <= n && n <= 21)
            return sign + digits + std::string(static_cast<std::size_t>(n - k), '0');
        if (0 < n && n <= 21)
            return sign + digits.substr(0u, static_cast<std::size_t>(n)) + "." + digits.substr(static_cast<std::size_t>(n));
        if (-6 < n && n <= 0)
            return sign + "0." + std::string(static_cast<std::size_t>(-n), '0') + digits;
        std::string text = sign + digits.substr(0u, 1u);
        if (k > 1)
            text += "." + digits.substr(1u);
        return text + "e" + (n - 1 < 0 ? "-" : "+") + std::to_string(std::abs(n - 1));
    }

    // Number.prototype.toString(radix) (as V8: DoubleToRadixCString)
    inline std::string number_to_string(double aValue, int aRadix)
    {
        if (aRadix == 10 || std::isnan(aValue) || std::isinf(aValue) || aValue == 0.0)
            return number_to_string(aValue);
        static char const chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
        constexpr int bufferSize = 2200;
        char buffer[bufferSize];
        int integerCursor = bufferSize / 2;
        int fractionCursor = integerCursor;
        bool const negative = aValue < 0.0;
        if (negative)
            aValue = -aValue;
        double integer = std::floor(aValue);
        double fraction = aValue - integer;
        double delta = 0.5 * (std::nextafter(aValue, std::numeric_limits<double>::infinity()) - aValue);
        delta = std::fmax(std::nextafter(0.0, 1.0), delta);
        if (fraction >= delta)
        {
            buffer[fractionCursor++] = '.';
            do
            {
                fraction *= aRadix;
                delta *= aRadix;
                int const digit = static_cast<int>(fraction);
                buffer[fractionCursor++] = chars[digit];
                fraction -= digit;
                if (fraction > 0.5 || (fraction == 0.5 && (digit & 1)))
                {
                    if (fraction + delta > 1.0)
                    {
                        // round up
                        for (;;)
                        {
                            --fractionCursor;
                            if (fractionCursor == bufferSize / 2)
                            {
                                integer += 1.0;
                                break;
                            }
                            char const c = buffer[fractionCursor];
                            int const d = c > '9' ? (c - 'a' + 10) : (c - '0');
                            if (d + 1 < aRadix)
                            {
                                buffer[fractionCursor++] = chars[d + 1];
                                break;
                            }
                        }
                        break;
                    }
                }
            } while (fraction >= delta);
        }
        // integer digits (those that are not represented are zero)
        auto const exponent = [](double aDouble) { return aDouble == 0.0 ? 0 : std::ilogb(aDouble) - 52; };
        while (exponent(integer / aRadix) > 0)
        {
            integer /= aRadix;
            buffer[--integerCursor] = '0';
        }
        do
        {
            double const remainder = std::fmod(integer, static_cast<double>(aRadix));
            buffer[--integerCursor] = chars[static_cast<int>(remainder)];
            integer = (integer - remainder) / aRadix;
        } while (integer > 0.0);
        if (negative)
            buffer[--integerCursor] = '-';
        return std::string{ buffer + integerCursor, buffer + fractionCursor };
    }

    // Number.prototype.toFixed(digits): the exact decimal value rounded half away from zero
    inline std::string number_to_fixed(double aValue, int aDigits)
    {
        if (std::isnan(aValue))
            return "NaN";
        if (std::fabs(aValue) >= 1e21)
            return number_to_string(aValue);
        double const magnitude = std::fabs(aValue);
        // the exact decimal expansion of the double (at most 1074 fractional digits)
        static thread_local char buffer[1500];
        auto const result = std::to_chars(buffer, buffer + sizeof(buffer), magnitude, std::chars_format::fixed, 1100);
        std::string exact{ buffer, result.ptr };
        auto const point = exact.find('.');
        std::string integral = exact.substr(0u, point);
        std::string fraction = exact.substr(point + 1u);
        std::string kept = integral + fraction.substr(0u, static_cast<std::size_t>(aDigits));
        bool roundUp = fraction.size() > static_cast<std::size_t>(aDigits) && fraction[static_cast<std::size_t>(aDigits)] >= '5';
        if (roundUp)
        {
            int i = static_cast<int>(kept.size()) - 1;
            for (; i >= 0; --i)
            {
                if (kept[static_cast<std::size_t>(i)] == '9')
                    kept[static_cast<std::size_t>(i)] = '0';
                else
                {
                    ++kept[static_cast<std::size_t>(i)];
                    break;
                }
            }
            if (i < 0)
                kept.insert(kept.begin(), '1');
        }
        auto const integralSize = kept.size() - static_cast<std::size_t>(aDigits);
        std::string text = kept.substr(0u, integralSize);
        if (aDigits > 0)
            text += "." + kept.substr(integralSize);
        // (-0.0001).toFixed(2) is "-0.00" but (-0).toFixed(2) is "0.00"
        return (aValue < 0.0 ? "-" : "") + text;
    }

    // UTF-16 (JavaScript strings are sequences of UTF-16 code units; neos strings are UTF-8)
    inline std::u16string to_utf16(std::string_view aText)
    {
        std::u16string result;
        result.reserve(aText.size());
        for (std::size_t i = 0u; i < aText.size();)
        {
            auto const c = static_cast<unsigned char>(aText[i]);
            char32_t cp = 0xFFFDu;
            std::size_t length = 1u;
            if (c < 0x80u)
                cp = c;
            else if ((c & 0xE0u) == 0xC0u && i + 1u < aText.size())
                cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(aText[i + 1u]) & 0x3Fu), length = 2u;
            else if ((c & 0xF0u) == 0xE0u && i + 2u < aText.size())
                cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(aText[i + 1u]) & 0x3Fu) << 6) |
                    (static_cast<unsigned char>(aText[i + 2u]) & 0x3Fu), length = 3u;
            else if ((c & 0xF8u) == 0xF0u && i + 3u < aText.size())
                cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(aText[i + 1u]) & 0x3Fu) << 12) |
                    ((static_cast<unsigned char>(aText[i + 2u]) & 0x3Fu) << 6) | (static_cast<unsigned char>(aText[i + 3u]) & 0x3Fu), length = 4u;
            if (cp >= 0x10000u)
            {
                result += static_cast<char16_t>(0xD800u + ((cp - 0x10000u) >> 10));
                result += static_cast<char16_t>(0xDC00u + ((cp - 0x10000u) & 0x3FFu));
            }
            else
                result += static_cast<char16_t>(cp);
            i += length;
        }
        return result;
    }

    inline std::string to_utf8(std::u16string_view aText)
    {
        std::string result;
        result.reserve(aText.size());
        for (std::size_t i = 0u; i < aText.size(); ++i)
        {
            char32_t cp = aText[i];
            if (cp >= 0xD800u && cp < 0xDC00u && i + 1u < aText.size() && aText[i + 1u] >= 0xDC00u && aText[i + 1u] < 0xE000u)
                cp = 0x10000u + ((cp - 0xD800u) << 10) + (aText[++i] - 0xDC00u);
            if (cp < 0x80u)
                result += static_cast<char>(cp);
            else if (cp < 0x800u)
            {
                result += static_cast<char>(0xC0u | (cp >> 6));
                result += static_cast<char>(0x80u | (cp & 0x3Fu));
            }
            else if (cp < 0x10000u)
            {
                result += static_cast<char>(0xE0u | (cp >> 12));
                result += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
                result += static_cast<char>(0x80u | (cp & 0x3Fu));
            }
            else
            {
                result += static_cast<char>(0xF0u | (cp >> 18));
                result += static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
                result += static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
                result += static_cast<char>(0x80u | (cp & 0x3Fu));
            }
        }
        return result;
    }

    // the length of a string in UTF-16 code units
    inline std::size_t length(std::string_view aText)
    {
        std::size_t result = 0u;
        for (auto c : aText)
        {
            auto const b = static_cast<unsigned char>(c);
            if ((b & 0xC0u) != 0x80u)
                result += (b >= 0xF0u) ? 2u : 1u;
        }
        return result;
    }

    // WhiteSpace and LineTerminator
    inline bool is_space(char16_t aCharacter)
    {
        switch (aCharacter)
        {
        case 0x0009u: case 0x000Au: case 0x000Bu: case 0x000Cu: case 0x000Du: case 0x0020u: case 0x00A0u: case 0x1680u:
        case 0x2028u: case 0x2029u: case 0x202Fu: case 0x205Fu: case 0x3000u: case 0xFEFFu:
            return true;
        default:
            return aCharacter >= 0x2000u && aCharacter <= 0x200Au;
        }
    }

    inline std::u16string_view trim(std::u16string_view aText, bool aStart = true, bool aEnd = true)
    {
        while (aStart && !aText.empty() && is_space(aText.front()))
            aText.remove_prefix(1u);
        while (aEnd && !aText.empty() && is_space(aText.back()))
            aText.remove_suffix(1u);
        return aText;
    }

    namespace detail
    {
        inline std::string ascii(std::u16string_view aText)
        {
            std::string result;
            for (auto c : aText)
                result += (c < 0x80u) ? static_cast<char>(c) : '\x7F';
            return result;
        }

        // the length of the longest prefix of aText that is a StrDecimalLiteral (0 if none)
        inline std::size_t decimal_literal(std::string_view aText)
        {
            std::size_t i = 0u;
            if (i < aText.size() && (aText[i] == '+' || aText[i] == '-'))
                ++i;
            if (aText.substr(i, 8u) == "Infinity")
                return i + 8u;
            std::size_t digits = 0u;
            while (i < aText.size() && aText[i] >= '0' && aText[i] <= '9')
                ++i, ++digits;
            if (i < aText.size() && aText[i] == '.')
            {
                std::size_t const point = i++;
                std::size_t fractionDigits = 0u;
                while (i < aText.size() && aText[i] >= '0' && aText[i] <= '9')
                    ++i, ++fractionDigits;
                if (fractionDigits == 0u)
                    i = point + 1u;
                digits += fractionDigits;
            }
            if (digits == 0u)
                return 0u;
            if (i < aText.size() && (aText[i] == 'e' || aText[i] == 'E'))
            {
                std::size_t j = i + 1u;
                if (j < aText.size() && (aText[j] == '+' || aText[j] == '-'))
                    ++j;
                std::size_t exponentDigits = 0u;
                while (j < aText.size() && aText[j] >= '0' && aText[j] <= '9')
                    ++j, ++exponentDigits;
                if (exponentDigits != 0u)
                    i = j;
            }
            return i;
        }

        inline double decimal_value(std::string_view aLiteral)
        {
            bool const negative = !aLiteral.empty() && aLiteral[0] == '-';
            if (!aLiteral.empty() && (aLiteral[0] == '+' || aLiteral[0] == '-'))
                aLiteral.remove_prefix(1u);
            if (aLiteral == "Infinity")
                return negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
            std::string text{ aLiteral };
            if (!text.empty() && text[0] == '.')
                text.insert(text.begin(), '0');
            if (!text.empty() && text.back() == '.')
                text.pop_back();
            double value = 0.0;
            auto const result = std::from_chars(text.data(), text.data() + text.size(), value);
            if (result.ec == std::errc::result_out_of_range)
                value = std::strtod(text.c_str(), nullptr);
            return negative ? -value : value;
        }

        inline int digit_value(char16_t aCharacter)
        {
            if (aCharacter >= u'0' && aCharacter <= u'9')
                return aCharacter - u'0';
            if (aCharacter >= u'a' && aCharacter <= u'z')
                return aCharacter - u'a' + 10;
            if (aCharacter >= u'A' && aCharacter <= u'Z')
                return aCharacter - u'A' + 10;
            return 99;
        }
    }

    // the characters of a string literal (without its quotes) or of template text (escape sequences as in JavaScript)
    inline std::string unescape(std::string_view aText)
    {
        std::u16string result;
        auto const hex = [&](std::size_t aBegin, std::size_t aLength) -> std::optional<char32_t>
        {
            if (aBegin + aLength > aText.size() || aLength == 0u)
                return {};
            char32_t value = 0u;
            for (std::size_t i = aBegin; i < aBegin + aLength; ++i)
            {
                int const digit = detail::digit_value(static_cast<char16_t>(static_cast<unsigned char>(aText[i])));
                if (digit >= 16)
                    return {};
                value = value * 16u + static_cast<char32_t>(digit);
            }
            return value;
        };
        auto const append = [&](char32_t aCodePoint)
        {
            if (aCodePoint >= 0x10000u)
            {
                result += static_cast<char16_t>(0xD800u + ((aCodePoint - 0x10000u) >> 10));
                result += static_cast<char16_t>(0xDC00u + ((aCodePoint - 0x10000u) & 0x3FFu));
            }
            else
                result += static_cast<char16_t>(aCodePoint);
        };
        std::size_t plain = 0u;
        auto const flush = [&](std::size_t aEnd)
        {
            result += to_utf16(aText.substr(plain, aEnd - plain));
        };
        for (std::size_t i = 0u; i < aText.size(); ++i)
        {
            if (aText[i] == '\r' && i + 1u < aText.size() && aText[i + 1u] == '\n')
            {
                // (line terminators in template text are normalized to LF)
                flush(i);
                plain = i + 1u;
                continue;
            }
            if (aText[i] != '\\' || i + 1u == aText.size())
                continue;
            flush(i);
            char const c = aText[++i];
            plain = i + 1u;
            switch (c)
            {
            case 'n': result += u'\n'; break;
            case 'r': result += u'\r'; break;
            case 't': result += u'\t'; break;
            case 'b': result += u'\b'; break;
            case 'f': result += u'\f'; break;
            case 'v': result += u'\v'; break;
            case '0': result += u'\0'; break;
            case '\r':
                if (i + 1u < aText.size() && aText[i + 1u] == '\n')
                    plain = ++i + 1u;
                break; // (a line continuation)
            case '\n':
                break;
            case 'x':
                if (auto const value = hex(i + 1u, 2u))
                {
                    append(*value);
                    plain = (i += 2u) + 1u;
                }
                else
                    result += u'x';
                break;
            case 'u':
                if (i + 1u < aText.size() && aText[i + 1u] == '{')
                {
                    auto const close = aText.find('}', i + 2u);
                    if (close != std::string_view::npos)
                        if (auto const value = hex(i + 2u, close - (i + 2u)))
                        {
                            append(*value);
                            plain = (i = close) + 1u;
                            break;
                        }
                    result += u'u';
                }
                else if (auto const value = hex(i + 1u, 4u))
                {
                    append(*value);
                    plain = (i += 4u) + 1u;
                }
                else
                    result += u'u';
                break;
            default:
                // (any other character, including a multibyte one, is itself)
                plain = i;
                break;
            }
        }
        flush(aText.size());
        return to_utf8(result);
    }

    // ToNumber(string)
    inline double string_to_number(std::string_view aText)
    {
        auto const text = detail::ascii(trim(to_utf16(aText)));
        if (text.empty())
            return 0.0;
        if (text.size() > 2u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X' || text[1] == 'o' || text[1] == 'O' || text[1] == 'b' || text[1] == 'B'))
        {
            int const radix = (text[1] == 'x' || text[1] == 'X') ? 16 : (text[1] == 'o' || text[1] == 'O') ? 8 : 2;
            double value = 0.0;
            for (std::size_t i = 2u; i < text.size(); ++i)
            {
                int const digit = detail::digit_value(static_cast<char16_t>(text[i]));
                if (digit >= radix)
                    return std::numeric_limits<double>::quiet_NaN();
                value = value * radix + digit;
            }
            return value;
        }
        auto const length = detail::decimal_literal(text);
        if (length != text.size())
            return std::numeric_limits<double>::quiet_NaN();
        return detail::decimal_value(text);
    }

    // parseFloat(string)
    inline double parse_float(std::string_view aText)
    {
        auto const text = detail::ascii(trim(to_utf16(aText), true, false));
        auto const length = detail::decimal_literal(text);
        if (length == 0u)
            return std::numeric_limits<double>::quiet_NaN();
        return detail::decimal_value(std::string_view{ text }.substr(0u, length));
    }

    // parseInt(string, radix) (a radix of 0 is undefined)
    inline double parse_int(std::string_view aText, double aRadix)
    {
        auto const utf16 = to_utf16(aText);
        auto text = trim(utf16, true, false);
        bool negative = false;
        if (!text.empty() && (text[0] == u'+' || text[0] == u'-'))
        {
            negative = text[0] == u'-';
            text.remove_prefix(1u);
        }
        int radix = std::isfinite(aRadix) ? static_cast<int>(static_cast<std::int32_t>(static_cast<std::int64_t>(std::trunc(aRadix)))) : 0;
        bool stripPrefix = true;
        if (radix != 0)
        {
            if (radix < 2 || radix > 36)
                return std::numeric_limits<double>::quiet_NaN();
            if (radix != 16)
                stripPrefix = false;
        }
        else
            radix = 10;
        if (stripPrefix && text.size() >= 2u && text[0] == u'0' && (text[1] == u'x' || text[1] == u'X'))
        {
            text.remove_prefix(2u);
            radix = 16;
        }
        std::size_t end = 0u;
        while (end < text.size() && detail::digit_value(text[end]) < radix)
            ++end;
        if (end == 0u)
            return std::numeric_limits<double>::quiet_NaN();
        double value = 0.0;
        if (radix == 10)
            value = detail::decimal_value(detail::ascii(text.substr(0u, end)));
        else
            for (std::size_t i = 0u; i < end; ++i)
                value = value * radix + detail::digit_value(text[i]);
        return negative ? -value : value;
    }

    // ToInt32
    inline std::int32_t to_int32(double aValue)
    {
        if (!std::isfinite(aValue))
            return 0;
        double const truncated = std::trunc(aValue);
        double modulo = std::fmod(truncated, 4294967296.0);
        if (modulo < 0.0)
            modulo += 4294967296.0;
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(modulo));
    }

    // Math.pow / ** (which differ from C's pow for a base of +/-1 and an infinite or NaN exponent)
    inline double pow(double aBase, double aExponent)
    {
        if (std::isnan(aExponent))
            return std::numeric_limits<double>::quiet_NaN();
        if (std::fabs(aBase) == 1.0 && std::isinf(aExponent))
            return std::numeric_limits<double>::quiet_NaN();
        return std::pow(aBase, aExponent);
    }

    // Math.cbrt (as V8, which uses fdlibm's cbrt: more accurate than some C libraries', e.g. Math.cbrt(27) is 3)
    inline double cbrt(double aValue)
    {
        constexpr std::uint32_t B1 = 715094163u;
        constexpr std::uint32_t B2 = 696219795u;
        constexpr double P0 = 1.87595182427177009643;
        constexpr double P1 = -1.88497979543377169875;
        constexpr double P2 = 1.621429720105354466140;
        constexpr double P3 = -0.758397934778766047437;
        constexpr double P4 = 0.145996192886612446982;
        auto const bits_of = [](double aDouble) { std::uint64_t bits; std::memcpy(&bits, &aDouble, sizeof(bits)); return bits; };
        auto const double_of = [](std::uint64_t aBits) { double d; std::memcpy(&d, &aBits, sizeof(d)); return d; };
        std::uint32_t hx = static_cast<std::uint32_t>(bits_of(aValue) >> 32u);
        std::uint32_t const low = static_cast<std::uint32_t>(bits_of(aValue));
        std::uint32_t const sign = hx & 0x80000000u;
        hx &= 0x7FFFFFFFu;
        if (hx >= 0x7FF00000u)
            return aValue + aValue;
        double t = 0.0;
        if (hx < 0x00100000u)
        {
            if ((hx | low) == 0u)
                return aValue;
            t = double_of(std::uint64_t{ 0x43500000u } << 32u) * aValue;
            std::uint32_t const high = static_cast<std::uint32_t>(bits_of(t) >> 32u);
            t = double_of(static_cast<std::uint64_t>(sign | ((high & 0x7FFFFFFFu) / 3u + B2)) << 32u);
        }
        else
            t = double_of(static_cast<std::uint64_t>(sign | (hx / 3u + B1)) << 32u);
        double r = (t * t) * (t / aValue);
        t = t * ((P0 + r * (P1 + r * P2)) + ((r * r) * r) * (P3 + r * P4));
        t = double_of((bits_of(t) + 0x80000000u) & 0xFFFFFFFFC0000000ull);
        double const s = t * t;
        r = aValue / s;
        double const w = t + t;
        r = (r - t) / (w + r);
        return t + t * r;
    }

    // a substring in UTF-16 code units [aBegin, aEnd)
    inline std::string substring(std::string_view aText, std::size_t aBegin, std::size_t aEnd)
    {
        auto const text = to_utf16(aText);
        aEnd = std::min(aEnd, text.size());
        if (aBegin >= aEnd)
            return {};
        return to_utf8(std::u16string_view{ text }.substr(aBegin, aEnd - aBegin));
    }
}
