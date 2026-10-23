/*
  python.cpp

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
  The Python runtime plugin (neos.runtime.python): the Python builtins declared (with stub bodies) in
  packages/python/builtins.py; a stub "f(types)" is imported as "python::f(types)".

  print writes to the standard output stream and input reads from the standard input stream.
*/

#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>
#include <neos/runtime/plugin.h>
#include "python.hpp"
#include "objects.hpp"

namespace neos::python_runtime::detail
{
    void resolve_raised_text(std::string& aText);
}

namespace neos::python_runtime
{
    namespace
    {
        // a string result (copied by the caller before the next call on this thread)
        thread_local std::string tResult;
        // the exception raised by the last call on this thread
        thread_local std::string tError;
        thread_local std::string tReportedError;
        thread_local bool tFailed = false;
        // (a trap requested: the plugin's error when the function returns (tTrapped: if one is; a flag that is cheap to test))
        thread_local std::optional<std::string> tTrap;
        thread_local bool tTrapped = false;
        // (the threads that have an error to report (tFailed or tTrapped): when there are none, NEOS_RUNTIME_PLUGIN_ERROR's test needs 
        // no thread-local (finding one is not free in a plugin: its thread-locals are dynamic))
        std::atomic<long> sPendingThreads{ 0 };
        void set_flags(bool aFailed, bool aTrapped)
        {
            bool const before = tFailed || tTrapped;
            tFailed = aFailed;
            tTrapped = aTrapped;
            if (bool const after = aFailed || aTrapped; after != before)
                sPendingThreads.fetch_add(after ? 1 : -1, std::memory_order_relaxed);
        }
    }

    void trap(std::string const& aMessage)
    {
        if (auto const* h = host(); h != nullptr && h->version >= 4u && h->trap != nullptr)
            h->trap(aMessage.c_str());
        if (!tTrap)
            tTrap = aMessage;
        set_flags(tFailed, true);
    }

    char const* result(std::string aValue)
    {
        // (a thread_local's address: found once)
        auto& r = tResult;
        r = std::move(aValue);
        return r.c_str();
    }

    char const* result_view(std::string_view aValue)
    {
        auto& r = tResult;
        r.assign(aValue);
        return r.c_str();
    }

    void raise(std::string const& aType, std::string const& aMessage)
    {
        // (the first exception raised is the one reported)
        if (tFailed)
            return;
        tError = aMessage.empty() ? aType : aType + ": " + aMessage;
        set_flags(true, tTrapped);
    }

    bool failed()
    {
        return tFailed;
    }

    char const* error_text()
    {
        // (an exception that the program raised, whose message is a token: its message, now that it is read)
        if (tError.find('\x01') != std::string::npos)
            detail::resolve_raised_text(tError);
        return tError.c_str();
    }

    void clear_error()
    {
        set_flags(false, tTrapped);
        tError.clear();
    }

    std::string python_float(double aValue)
    {
        if (std::isnan(aValue))
            return "nan";
        if (std::isinf(aValue))
            return aValue < 0.0 ? "-inf" : "inf";
        char buffer[64];
        auto const converted = std::to_chars(buffer, buffer + sizeof(buffer), aValue, std::chars_format::scientific);
        std::string_view const scientific{ buffer, static_cast<std::size_t>(converted.ptr - buffer) };
        std::string text = std::signbit(aValue) ? "-" : "";
        auto const mantissa = scientific.substr(text.size(), scientific.find('e') - text.size());
        int const exponent = std::stoi(std::string{ scientific.substr(scientific.find('e') + 1u) });
        std::string digits;
        for (auto c : mantissa)
            if (c != '.')
                digits += c;
        if (exponent < -4 || exponent >= 16)
        {
            text += digits.substr(0u, 1u);
            if (digits.size() > 1u)
                text += "." + digits.substr(1u);
            auto const e = std::to_string(std::abs(exponent));
            text += std::string{ "e" } + (exponent < 0 ? "-" : "+") + (e.size() < 2u ? "0" : "") + e;
        }
        else if (exponent < 0)
            text += "0." + std::string(static_cast<std::size_t>(-exponent - 1), '0') + digits;
        else
        {
            auto const integral = static_cast<std::size_t>(exponent) + 1u;
            if (digits.size() < integral)
                digits.append(integral - digits.size(), '0');
            text += digits.substr(0u, integral) + "." + (digits.size() > integral ? digits.substr(integral) : "0");
        }
        return text;
    }

}

namespace
{
    using namespace neos::python_runtime;

    std::string trimmed(std::string const& aText)
    {
        auto const begin = aText.find_first_not_of(" \t\r\n\f\v");
        auto const end = aText.find_last_not_of(" \t\r\n\f\v");
        return begin == std::string::npos ? std::string{} : aText.substr(begin, end - begin + 1u);
    }

    void write_line(std::string const& aText)
    {
        // (to sys.stdout, if the program has replaced it)
        detail::write_out(aText + "\n");
    }

    void print_string(char const* aValue) { write_line(aValue); }
    void print_float(double aValue) { write_line(python_float(aValue)); }
    void print_bool(bool aValue) { write_line(aValue ? "True" : "False"); }

    char const* input()
    {
        std::string line;
        if (!detail::read_standard_line(line))
        {
            raise("EOFError", "EOF when reading a line");
            return "";
        }
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        return result(std::move(line));
    }

    char const* str_float(double aValue) { return result(python_float(aValue)); }
    char const* str_bool(bool aValue) { return result(aValue ? "True" : "False"); }

    // the whole of the (trimmed) string must be a number (single underscores may separate digits)
    double float_string(char const* aValue)
    {
        auto text = trimmed(aValue);
        auto const digit = [&](std::size_t aIndex) { return aIndex < text.size() && text[aIndex] >= '0' && text[aIndex] <= '9'; };
        for (std::size_t i = 0u; i < text.size(); ++i)
            if (text[i] == '_' && i > 0u && digit(i - 1u) && digit(i + 1u))
                text.erase(i--, 1u);
        auto const sign = (!text.empty() && (text[0] == '+' || text[0] == '-')) ? 1u : 0u;
        double value = 0.0;
        auto const converted = std::from_chars(text.data() + sign, text.data() + text.size(), value);
        if (text.find_first_of("_(") != std::string::npos || sign == text.size() || text[sign] == '+' || text[sign] == '-' ||
            converted.ec == std::errc::invalid_argument || converted.ptr != text.data() + text.size())
        {
            raise("ValueError", "could not convert string to float: '" + text + "'");
            return 0.0;
        }
        if (converted.ec == std::errc::result_out_of_range)
            value = std::strtod(text.c_str() + sign, nullptr);
        return text[0] == '-' ? -value : value;
    }

    // (code points)
    std::int64_t len_string(char const* aValue)
    {
        std::int64_t length = 0;
        for (auto p = aValue; *p != '\0'; ++p)
            if ((static_cast<unsigned char>(*p) & 0xC0u) != 0x80u)
                ++length;
        return length;
    }

    double abs_float(double aValue) { return std::fabs(aValue); }

    neos_rt_function const sBuiltins[] =
    {
        { "print(string)", NEOS_RT_FUNCTION(print_string), NEOS_RT_VOID, 1u, { NEOS_RT_STRING }, 0u },
        { "print(f64)", NEOS_RT_FUNCTION(print_float), NEOS_RT_VOID, 1u, { NEOS_RT_F64 }, 0u },
        { "print(bool)", NEOS_RT_FUNCTION(print_bool), NEOS_RT_VOID, 1u, { NEOS_RT_BOOL }, 0u },
        { "input()", NEOS_RT_FUNCTION(input), NEOS_RT_STRING, 0u, {}, 0u },
        { "str(f64)", NEOS_RT_FUNCTION(str_float), NEOS_RT_STRING, 1u, { NEOS_RT_F64 }, 0u },
        { "str(bool)", NEOS_RT_FUNCTION(str_bool), NEOS_RT_STRING, 1u, { NEOS_RT_BOOL }, 0u },
        { "float(string)", NEOS_RT_FUNCTION(float_string), NEOS_RT_F64, 1u, { NEOS_RT_STRING }, 0u },
        { "len(string)", NEOS_RT_FUNCTION(len_string), NEOS_RT_I64, 1u, { NEOS_RT_STRING }, 0u },
        { "abs(f64)", NEOS_RT_FUNCTION(abs_float), NEOS_RT_F64, 1u, { NEOS_RT_F64 }, 0u }
    };

    std::vector<neos_rt_function> const& functions()
    {
        static std::vector<neos_rt_function> const sFunctions = []()
        {
            std::vector<neos_rt_function> result{ std::begin(sBuiltins), std::end(sBuiltins) };
            result.insert(result.end(), object_functions().begin(), object_functions().end());
            return result;
        }();
        return sFunctions;
    }

    neos_rt_plugin const sPlugin =
    {
        NEOS_RUNTIME_PLUGIN_VERSION,
        "python",
        "Python builtins and objects",
        static_cast<unsigned>(functions().size()),
        functions().data()
    };
}

neos_rt_host const*& neos::python_runtime::host()
{
    static neos_rt_host const* sHost = nullptr;
    return sHost;
}

NEOS_RUNTIME_PLUGIN_SET_HOST
{
    neos::python_runtime::host() = aHost;
    neos::python_runtime::detail::signals_start();
}

NEOS_RUNTIME_PLUGIN_END
{
    neos::python_runtime::detail::threads_end();
    neos::python_runtime::detail::generators_end();
}

NEOS_RUNTIME_PLUGIN_ENTRY
{
    return &sPlugin;
}

namespace neos::python_runtime
{
    namespace
    {
        // (the error to report: see NEOS_RUNTIME_PLUGIN_ERROR (apart from it, so that the test it makes first is cheap))
#if defined(_MSC_VER)
        __declspec(noinline)
#else
        __attribute__((noinline))
#endif
        char const* reported_error()
        {
            if (tTrapped)
            {
                set_flags(false, false);
                tReportedError = std::move(*tTrap);
                tTrap.reset();
                return tReportedError.c_str();
            }
            if (!tFailed)
                return nullptr;
            set_flags(false, false);
            // (the frames that it is raised in)
            neos::python_runtime::detail::frames_reported(tError);
            tReportedError = std::move(tError);
            return tReportedError.c_str();
        }
    }
}

NEOS_RUNTIME_PLUGIN_ERROR_PENDING
{
    // (zero when no thread has an error to report: see NEOS_RUNTIME_PLUGIN_ERROR)
    static_assert(sizeof(std::atomic<long>) == sizeof(long) && std::atomic<long>::is_always_lock_free);
    return reinterpret_cast<long const*>(&neos::python_runtime::sPendingThreads);
}

NEOS_RUNTIME_PLUGIN_ERROR
{
    using namespace neos::python_runtime;
    // (tested after every call of the plugin's functions: no error is the common case)
    if (sPendingThreads.load(std::memory_order_relaxed) == 0 || (!tFailed && !tTrapped))
        return nullptr;
    return reported_error();
}
