/*
  javascript.cpp

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

  Portions of this file (decide_styled) are derived from Node.js (lib/internal/tty.js: getColorDepth and its table of
  terminals), which is used under the MIT licence below; Node.js's table of terminals is in part from GNU dircolors,
  whose notice is also below. Changes: translated from JavaScript to C++ for the neos JavaScript runtime plugin (see
  runtime/javascript/NOTICE).

    MIT License

    Copyright (c) Sindre Sorhus <sindresorhus@gmail.com> (sindresorhus.com)

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

    dircolors: Copyright (C) 1996-2016 Free Software Foundation, Inc. Copying and
    distribution of this file, with or without modification, are permitted
    provided the copyright notice and this notice are preserved.
*/

/*
  The JavaScript runtime plugin (neos.runtime.javascript): the functions that code compiled from JavaScript calls
  (as imports named "javascript::f") for the parts of the language that are not compiled to WebAssembly directly:
  converting numbers to and from strings, the methods of String and the Math functions that are not WebAssembly
  instructions.

  Strings are UTF-8; JavaScript indexes strings in UTF-16 code units, as these functions do.
*/

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <neos/runtime/plugin.h>
#include <neos/language/ecmascript.hpp>

namespace
{
    namespace es = neos::ecmascript;

    // a string result (copied by the caller before the next call on this thread)
    thread_local std::string tResult;
    // the error (a JavaScript RangeError) raised by the last call on this thread
    thread_local std::string tError;
    thread_local std::string tReportedError;
    thread_local bool tFailed = false;

    char const* result(std::string aValue)
    {
        tResult = std::move(aValue);
        return tResult.c_str();
    }

    char const* result(std::u16string_view aValue)
    {
        return result(es::to_utf8(aValue));
    }

    void raise(std::string const& aError)
    {
        tError = aError;
        tFailed = true;
    }

    // ToIntegerOrInfinity
    double integer(double aValue)
    {
        return std::isnan(aValue) ? 0.0 : std::trunc(aValue);
    }

    double clamp(double aValue, double aMinimum, double aMaximum)
    {
        return std::fmin(std::fmax(aValue, aMinimum), aMaximum);
    }

    bool ascii(std::string_view aText)
    {
        for (auto c : aText)
            if (static_cast<unsigned char>(c) >= 0x80u)
                return false;
        return true;
    }

    double const NaN = std::numeric_limits<double>::quiet_NaN();

    // --- numbers

    char const* to_string(double aValue) { return result(es::number_to_string(aValue)); }
    // (as console.log shows a number: -0 is "-0")
    char const* inspect(double aValue) { return result(aValue == 0.0 && std::signbit(aValue) ? std::string{ "-0" } : es::number_to_string(aValue)); }
    double to_number(char const* aText) { return es::string_to_number(aText); }
    double parse_int(char const* aText, double aRadix) { return es::parse_int(aText, aRadix); }
    double parse_float(char const* aText) { return es::parse_float(aText); }
    std::int32_t to_int32(double aValue) { return es::to_int32(aValue); }
    double pow(double aBase, double aExponent) { return es::pow(aBase, aExponent); }

    char const* to_fixed(double aValue, double aDigits)
    {
        double const digits = integer(aDigits);
        if (digits < 0.0 || digits > 100.0)
        {
            raise("RangeError: toFixed() digits argument must be between 0 and 100");
            return nullptr;
        }
        return result(es::number_to_fixed(aValue, static_cast<int>(digits)));
    }

    char const* to_string_radix(double aValue, double aRadix)
    {
        double const radix = integer(aRadix);
        if (radix < 2.0 || radix > 36.0)
        {
            raise("RangeError: toString() radix must be between 2 and 36");
            return nullptr;
        }
        return result(es::number_to_string(aValue, static_cast<int>(radix)));
    }

    // --- strings

    double length(char const* aText) { return static_cast<double>(es::length(aText)); }

    char const* char_at(char const* aText, double aIndex)
    {
        double const i = integer(aIndex);
        std::string_view const text{ aText };
        if (ascii(text))
            return result(i < 0.0 || i >= static_cast<double>(text.size()) ? std::string{} : std::string(1u, text[static_cast<std::size_t>(i)]));
        auto const s = es::to_utf16(text);
        if (i < 0.0 || i >= static_cast<double>(s.size()))
            return result(std::string{});
        return result(std::u16string_view{ s }.substr(static_cast<std::size_t>(i), 1u));
    }

    char const* at(char const* aText, double aIndex)
    {
        auto const s = es::to_utf16(aText);
        double i = integer(aIndex);
        if (i < 0.0)
            i += static_cast<double>(s.size());
        if (i < 0.0 || i >= static_cast<double>(s.size()))
            return result(std::string{});
        return result(std::u16string_view{ s }.substr(static_cast<std::size_t>(i), 1u));
    }

    double char_code_at(char const* aText, double aIndex)
    {
        double const i = integer(aIndex);
        std::string_view const text{ aText };
        if (ascii(text))
            return i < 0.0 || i >= static_cast<double>(text.size()) ? NaN : static_cast<double>(static_cast<unsigned char>(text[static_cast<std::size_t>(i)]));
        auto const s = es::to_utf16(text);
        if (i < 0.0 || i >= static_cast<double>(s.size()))
            return NaN;
        return static_cast<double>(s[static_cast<std::size_t>(i)]);
    }

    double code_point_at(char const* aText, double aIndex)
    {
        auto const s = es::to_utf16(aText);
        double const d = integer(aIndex);
        if (d < 0.0 || d >= static_cast<double>(s.size()))
            return NaN;
        auto const i = static_cast<std::size_t>(d);
        char32_t cp = s[i];
        if (cp >= 0xD800u && cp < 0xDC00u && i + 1u < s.size() && s[i + 1u] >= 0xDC00u && s[i + 1u] < 0xE000u)
            cp = 0x10000u + ((cp - 0xD800u) << 10) + (s[i + 1u] - 0xDC00u);
        return static_cast<double>(cp);
    }

    double index_of(char const* aText, char const* aSearch, double aFrom)
    {
        auto const s = es::to_utf16(aText);
        auto const from = static_cast<std::size_t>(clamp(integer(aFrom), 0.0, static_cast<double>(s.size())));
        auto const found = s.find(es::to_utf16(aSearch), from);
        return found == std::u16string::npos ? -1.0 : static_cast<double>(found);
    }

    double last_index_of(char const* aText, char const* aSearch, double aFrom)
    {
        auto const s = es::to_utf16(aText);
        auto const from = static_cast<std::size_t>(clamp(std::isnan(aFrom) ? static_cast<double>(s.size()) : std::trunc(aFrom), 0.0, static_cast<double>(s.size())));
        auto const found = s.rfind(es::to_utf16(aSearch), from);
        return found == std::u16string::npos ? -1.0 : static_cast<double>(found);
    }

    bool includes(char const* aText, char const* aSearch, double aFrom)
    {
        auto const s = es::to_utf16(aText);
        auto const from = static_cast<std::size_t>(clamp(integer(aFrom), 0.0, static_cast<double>(s.size())));
        return s.find(es::to_utf16(aSearch), from) != std::u16string::npos;
    }

    bool starts_with(char const* aText, char const* aSearch, double aFrom)
    {
        auto const s = es::to_utf16(aText);
        auto const from = static_cast<std::size_t>(clamp(integer(aFrom), 0.0, static_cast<double>(s.size())));
        return std::u16string_view{ s }.substr(from).starts_with(es::to_utf16(aSearch));
    }

    bool ends_with(char const* aText, char const* aSearch, double aEnd)
    {
        auto const s = es::to_utf16(aText);
        auto const end = static_cast<std::size_t>(clamp(integer(aEnd), 0.0, static_cast<double>(s.size())));
        return std::u16string_view{ s }.substr(0u, end).ends_with(es::to_utf16(aSearch));
    }

    char const* slice(char const* aText, double aStart, double aEnd)
    {
        auto const s = es::to_utf16(aText);
        double const length = static_cast<double>(s.size());
        auto const relative = [&](double aIndex) { return aIndex < 0.0 ? std::fmax(length + aIndex, 0.0) : std::fmin(aIndex, length); };
        double const from = relative(integer(aStart));
        double const to = relative(integer(aEnd));
        if (from >= to)
            return result(std::string{});
        return result(std::u16string_view{ s }.substr(static_cast<std::size_t>(from), static_cast<std::size_t>(to - from)));
    }

    char const* substring(char const* aText, double aStart, double aEnd)
    {
        auto const s = es::to_utf16(aText);
        double const length = static_cast<double>(s.size());
        double from = clamp(integer(aStart), 0.0, length);
        double to = clamp(integer(aEnd), 0.0, length);
        if (from > to)
            std::swap(from, to);
        return result(std::u16string_view{ s }.substr(static_cast<std::size_t>(from), static_cast<std::size_t>(to - from)));
    }

    char const* substr(char const* aText, double aStart, double aLength)
    {
        auto const s = es::to_utf16(aText);
        double const length = static_cast<double>(s.size());
        double from = integer(aStart);
        if (from < 0.0)
            from = std::fmax(length + from, 0.0);
        from = std::fmin(from, length);
        double const count = clamp(integer(aLength), 0.0, length - from);
        return result(std::u16string_view{ s }.substr(static_cast<std::size_t>(from), static_cast<std::size_t>(count)));
    }

    // (ASCII, Latin-1, Greek and Cyrillic letters)
    char const* to_upper_case(char const* aText)
    {
        auto s = es::to_utf16(aText);
        for (auto& c : s)
            if ((c >= u'a' && c <= u'z') || (c >= 0xE0u && c <= 0xFEu && c != 0xF7u) || (c >= 0x3B1u && c <= 0x3C9u && c != 0x3C2u) || (c >= 0x430u && c <= 0x44Fu))
                c = static_cast<char16_t>(c - 0x20u);
            else if (c >= 0x450u && c <= 0x45Fu)
                c = static_cast<char16_t>(c - 0x50u);
            else if (c == 0xFFu)
                c = 0x178u;
        return result(std::u16string_view{ s });
    }

    char const* to_lower_case(char const* aText)
    {
        auto s = es::to_utf16(aText);
        for (auto& c : s)
            if ((c >= u'A' && c <= u'Z') || (c >= 0xC0u && c <= 0xDEu && c != 0xD7u) || (c >= 0x391u && c <= 0x3A9u && c != 0x3A2u) || (c >= 0x410u && c <= 0x42Fu))
                c = static_cast<char16_t>(c + 0x20u);
            else if (c >= 0x400u && c <= 0x40Fu)
                c = static_cast<char16_t>(c + 0x50u);
            else if (c == 0x178u)
                c = 0xFFu;
        return result(std::u16string_view{ s });
    }

    char const* trim(char const* aText) { return result(es::trim(es::to_utf16(aText))); }
    char const* trim_start(char const* aText) { return result(es::trim(es::to_utf16(aText), true, false)); }
    char const* trim_end(char const* aText) { return result(es::trim(es::to_utf16(aText), false, true)); }

    char const* repeat(char const* aText, double aCount)
    {
        double const count = integer(aCount);
        if (count < 0.0 || std::isinf(count))
        {
            raise("RangeError: Invalid count value: " + es::number_to_string(aCount));
            return nullptr;
        }
        std::string_view const text{ aText };
        if (static_cast<double>(text.size()) * count > 512.0 * 1024.0 * 1024.0)
        {
            raise("RangeError: Invalid string length");
            return nullptr;
        }
        std::string repeated;
        for (double i = 0.0; i < count; i += 1.0)
            repeated += text;
        return result(std::move(repeated));
    }

    char const* pad(char const* aText, double aLength, char const* aFiller, bool aStart)
    {
        auto const s = es::to_utf16(aText);
        double const target = integer(aLength);
        auto const filler = es::to_utf16(aFiller);
        if (target <= static_cast<double>(s.size()) || filler.empty())
            return result(std::u16string_view{ s });
        if (target > 512.0 * 1024.0 * 1024.0)
        {
            raise("RangeError: Invalid string length");
            return nullptr;
        }
        std::u16string padding;
        while (padding.size() < static_cast<std::size_t>(target) - s.size())
            padding += filler;
        padding.resize(static_cast<std::size_t>(target) - s.size());
        return result(std::u16string_view{ aStart ? padding + s : s + padding });
    }

    char const* pad_start(char const* aText, double aLength, char const* aFiller) { return pad(aText, aLength, aFiller, true); }
    char const* pad_end(char const* aText, double aLength, char const* aFiller) { return pad(aText, aLength, aFiller, false); }

    char const* from_char_code(double aCode)
    {
        char16_t const c = static_cast<char16_t>(static_cast<std::uint32_t>(es::to_int32(aCode)) & 0xFFFFu);
        return result(std::u16string_view{ &c, 1u });
    }

    char const* from_code_point(double aCodePoint)
    {
        if (std::trunc(aCodePoint) != aCodePoint || aCodePoint < 0.0 || aCodePoint > 0x10FFFF)
        {
            raise("RangeError: Invalid code point " + es::number_to_string(aCodePoint));
            return nullptr;
        }
        auto const cp = static_cast<char32_t>(aCodePoint);
        std::u16string s;
        if (cp >= 0x10000u)
        {
            s += static_cast<char16_t>(0xD800u + ((cp - 0x10000u) >> 10));
            s += static_cast<char16_t>(0xDC00u + ((cp - 0x10000u) & 0x3FFu));
        }
        else
            s += static_cast<char16_t>(cp);
        return result(std::u16string_view{ s });
    }

    // --- Math (those that are not WebAssembly instructions)

    double math_exp(double x) { return std::exp(x); }
    double math_expm1(double x) { return std::expm1(x); }
    double math_log(double x) { return std::log(x); }
    double math_log1p(double x) { return std::log1p(x); }
    double math_log2(double x) { return std::log2(x); }
    double math_log10(double x) { return std::log10(x); }
    double math_sin(double x) { return std::sin(x); }
    double math_cos(double x) { return std::cos(x); }
    double math_tan(double x) { return std::tan(x); }
    double math_asin(double x) { return std::asin(x); }
    double math_acos(double x) { return std::acos(x); }
    double math_atan(double x) { return std::atan(x); }
    double math_sinh(double x) { return std::sinh(x); }
    double math_cosh(double x) { return std::cosh(x); }
    double math_tanh(double x) { return std::tanh(x); }
    double math_asinh(double x) { return std::asinh(x); }
    double math_acosh(double x) { return std::acosh(x); }
    double math_atanh(double x) { return std::atanh(x); }
    double math_cbrt(double x) { return es::cbrt(x); }
    double math_atan2(double y, double x) { return std::atan2(y, x); }
    double math_hypot(double x, double y) { return std::hypot(x, y); }

    double math_random()
    {
        thread_local std::mt19937_64 generator{ std::random_device{}() };
        return std::uniform_real_distribution<double>{ 0.0, 1.0 }(generator);
    }

    // --- Date, performance, console

    double date_now()
    {
        return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    }

    double performance_now()
    {
        static auto const sStart = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sStart).count();
    }

    // (console.log writes to the script's output; console.error writes to the standard error stream)
    void console_error(char const* aText)
    {
        std::fputs(aText, stderr);
        std::fputc('\n', stderr);
        std::fflush(stderr);
    }

    // whether console output to a stream (1: the standard output, 2: the standard error stream) is styled (coloured), as
    // node decides it: if FORCE_COLOR is set, as it says (0 or false: not styled); else only if the stream is a terminal
    // (on Windows, a console that processes ANSI escape sequences) whose colours are not disabled (NO_COLOR,
    // NODE_DISABLE_COLORS, TERM=dumb) and that has them (Windows, tmux, COLORTERM or a TERM that node knows has them)
    bool decide_styled(int aStream)
    {
        auto const env = [](char const* aName) -> std::optional<std::string_view>
        {
            if (auto const value = std::getenv(aName))
                return std::string_view{ value };
            return {};
        };
#if defined(_WIN32)
        HANDLE const handle = ::GetStdHandle(aStream == 2 ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        bool const terminal = handle != nullptr && handle != INVALID_HANDLE_VALUE && ::GetConsoleMode(handle, &mode) != 0;
        auto const ansi = [&]()
        {
            return !terminal || (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0 ||
                ::SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
        };
#else
        bool const terminal = ::isatty(aStream) != 0;
        auto const ansi = []() { return true; };
#endif
        if (auto const force = env("FORCE_COLOR"))
            return (force->empty() || *force == "1" || *force == "2" || *force == "3" || *force == "true") && ansi();
        if (!terminal || env("NODE_DISABLE_COLORS") || env("NO_COLOR") || env("TERM") == "dumb")
            return false;
#if defined(_WIN32)
        return ansi();
#else
        if (env("TMUX") || !env("COLORTERM").value_or("").empty())
            return true;
        auto const term = env("TERM").value_or("");
        static std::string_view const sNames[] = { "eterm", "cons25", "console", "cygwin", "dtterm", "gnome", "hurd", "jfbterm",
            "konsole", "kterm", "mlterm", "mosh", "putty", "st", "rxvt-unicode-24bit", "terminator", "xterm-kitty" };
        static std::string_view const sContaining[] = { "ansi", "color", "linux", "direct" };
        static std::string_view const sPrefixes[] = { "rxvt", "screen", "xterm", "vt100", "vt220" };
        std::string lower{ term };
        for (auto& c : lower)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (auto const name : sNames)
            if (lower == name)
                return true;
        for (auto const part : sContaining)
            if (lower.find(part) != std::string::npos)
                return true;
        for (auto const prefix : sPrefixes)
            if (lower.starts_with(prefix))
                return true;
        // (con<digits>x<digit>)
        if (lower.starts_with("con"))
        {
            auto p = lower.find_first_not_of("0123456789", 3u);
            if (p != std::string::npos && lower[p] == 'x' && p + 1u < lower.size() && std::isdigit(static_cast<unsigned char>(lower[p + 1u])))
                return true;
        }
        return false;
#endif
    }

    // (as console.log shows a value that is not a string: a number or boolean is yellow if the stream is styled)
    char const* stylize(char const* aText, std::int32_t aStream)
    {
        static bool const sStyled[] = { decide_styled(1), decide_styled(2) };
        if (!sStyled[aStream == 2 ? 1 : 0])
            return result(std::string{ aText });
        return result("\x1b[33m" + std::string{ aText } + "\x1b[39m");
    }

    constexpr auto F64 = NEOS_RT_F64;
    constexpr auto STR = NEOS_RT_STRING;

    neos_rt_function const sFunctions[] =
    {
        { "to_string", NEOS_RT_FUNCTION(to_string), STR, 1u, { F64 }, 0u },
        { "inspect", NEOS_RT_FUNCTION(inspect), STR, 1u, { F64 }, 0u },
        { "to_number", NEOS_RT_FUNCTION(to_number), F64, 1u, { STR }, 0u },
        { "parse_int", NEOS_RT_FUNCTION(parse_int), F64, 2u, { STR, F64 }, 0u },
        { "parse_float", NEOS_RT_FUNCTION(parse_float), F64, 1u, { STR }, 0u },
        { "to_int32", NEOS_RT_FUNCTION(to_int32), NEOS_RT_I32, 1u, { F64 }, 0u },
        { "pow", NEOS_RT_FUNCTION(pow), F64, 2u, { F64, F64 }, 0u },
        { "to_fixed", NEOS_RT_FUNCTION(to_fixed), STR, 2u, { F64, F64 }, 0u },
        { "to_string_radix", NEOS_RT_FUNCTION(to_string_radix), STR, 2u, { F64, F64 }, 0u },
        { "length", NEOS_RT_FUNCTION(length), F64, 1u, { STR }, 0u },
        { "char_at", NEOS_RT_FUNCTION(char_at), STR, 2u, { STR, F64 }, 0u },
        { "at", NEOS_RT_FUNCTION(at), STR, 2u, { STR, F64 }, 0u },
        { "char_code_at", NEOS_RT_FUNCTION(char_code_at), F64, 2u, { STR, F64 }, 0u },
        { "code_point_at", NEOS_RT_FUNCTION(code_point_at), F64, 2u, { STR, F64 }, 0u },
        { "index_of", NEOS_RT_FUNCTION(index_of), F64, 3u, { STR, STR, F64 }, 0u },
        { "last_index_of", NEOS_RT_FUNCTION(last_index_of), F64, 3u, { STR, STR, F64 }, 0u },
        { "includes", NEOS_RT_FUNCTION(includes), NEOS_RT_BOOL, 3u, { STR, STR, F64 }, 0u },
        { "starts_with", NEOS_RT_FUNCTION(starts_with), NEOS_RT_BOOL, 3u, { STR, STR, F64 }, 0u },
        { "ends_with", NEOS_RT_FUNCTION(ends_with), NEOS_RT_BOOL, 3u, { STR, STR, F64 }, 0u },
        { "slice", NEOS_RT_FUNCTION(slice), STR, 3u, { STR, F64, F64 }, 0u },
        { "substring", NEOS_RT_FUNCTION(substring), STR, 3u, { STR, F64, F64 }, 0u },
        { "substr", NEOS_RT_FUNCTION(substr), STR, 3u, { STR, F64, F64 }, 0u },
        { "to_upper_case", NEOS_RT_FUNCTION(to_upper_case), STR, 1u, { STR }, 0u },
        { "to_lower_case", NEOS_RT_FUNCTION(to_lower_case), STR, 1u, { STR }, 0u },
        { "trim", NEOS_RT_FUNCTION(trim), STR, 1u, { STR }, 0u },
        { "trim_start", NEOS_RT_FUNCTION(trim_start), STR, 1u, { STR }, 0u },
        { "trim_end", NEOS_RT_FUNCTION(trim_end), STR, 1u, { STR }, 0u },
        { "repeat", NEOS_RT_FUNCTION(repeat), STR, 2u, { STR, F64 }, 0u },
        { "pad_start", NEOS_RT_FUNCTION(pad_start), STR, 3u, { STR, F64, STR }, 0u },
        { "pad_end", NEOS_RT_FUNCTION(pad_end), STR, 3u, { STR, F64, STR }, 0u },
        { "from_char_code", NEOS_RT_FUNCTION(from_char_code), STR, 1u, { F64 }, 0u },
        { "from_code_point", NEOS_RT_FUNCTION(from_code_point), STR, 1u, { F64 }, 0u },
        { "math_exp", NEOS_RT_FUNCTION(math_exp), F64, 1u, { F64 }, 0u },
        { "math_expm1", NEOS_RT_FUNCTION(math_expm1), F64, 1u, { F64 }, 0u },
        { "math_log", NEOS_RT_FUNCTION(math_log), F64, 1u, { F64 }, 0u },
        { "math_log1p", NEOS_RT_FUNCTION(math_log1p), F64, 1u, { F64 }, 0u },
        { "math_log2", NEOS_RT_FUNCTION(math_log2), F64, 1u, { F64 }, 0u },
        { "math_log10", NEOS_RT_FUNCTION(math_log10), F64, 1u, { F64 }, 0u },
        { "math_sin", NEOS_RT_FUNCTION(math_sin), F64, 1u, { F64 }, 0u },
        { "math_cos", NEOS_RT_FUNCTION(math_cos), F64, 1u, { F64 }, 0u },
        { "math_tan", NEOS_RT_FUNCTION(math_tan), F64, 1u, { F64 }, 0u },
        { "math_asin", NEOS_RT_FUNCTION(math_asin), F64, 1u, { F64 }, 0u },
        { "math_acos", NEOS_RT_FUNCTION(math_acos), F64, 1u, { F64 }, 0u },
        { "math_atan", NEOS_RT_FUNCTION(math_atan), F64, 1u, { F64 }, 0u },
        { "math_sinh", NEOS_RT_FUNCTION(math_sinh), F64, 1u, { F64 }, 0u },
        { "math_cosh", NEOS_RT_FUNCTION(math_cosh), F64, 1u, { F64 }, 0u },
        { "math_tanh", NEOS_RT_FUNCTION(math_tanh), F64, 1u, { F64 }, 0u },
        { "math_asinh", NEOS_RT_FUNCTION(math_asinh), F64, 1u, { F64 }, 0u },
        { "math_acosh", NEOS_RT_FUNCTION(math_acosh), F64, 1u, { F64 }, 0u },
        { "math_atanh", NEOS_RT_FUNCTION(math_atanh), F64, 1u, { F64 }, 0u },
        { "math_cbrt", NEOS_RT_FUNCTION(math_cbrt), F64, 1u, { F64 }, 0u },
        { "math_atan2", NEOS_RT_FUNCTION(math_atan2), F64, 2u, { F64, F64 }, 0u },
        { "math_hypot", NEOS_RT_FUNCTION(math_hypot), F64, 2u, { F64, F64 }, 0u },
        { "math_random", NEOS_RT_FUNCTION(math_random), F64, 0u, {}, 0u },
        { "date_now", NEOS_RT_FUNCTION(date_now), F64, 0u, {}, 0u },
        { "performance_now", NEOS_RT_FUNCTION(performance_now), F64, 0u, {}, 0u },
        { "console_error", NEOS_RT_FUNCTION(console_error), NEOS_RT_VOID, 1u, { STR }, 0u },
        { "stylize", NEOS_RT_FUNCTION(stylize), STR, 2u, { STR, NEOS_RT_I32 }, 0u }
    };

    neos_rt_plugin const sPlugin =
    {
        NEOS_RUNTIME_PLUGIN_VERSION,
        "javascript",
        "JavaScript runtime",
        static_cast<unsigned>(sizeof(sFunctions) / sizeof(sFunctions[0])),
        sFunctions
    };
}

NEOS_RUNTIME_PLUGIN_ENTRY
{
    return &sPlugin;
}

NEOS_RUNTIME_PLUGIN_ERROR
{
    if (!tFailed)
        return nullptr;
    tFailed = false;
    tReportedError = std::move(tError);
    return tReportedError.c_str();
}
