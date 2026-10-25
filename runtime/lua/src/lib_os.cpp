/*
  lib_os.cpp

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

// The os library (its functions that change the host environment are refused in restricted mode)

#include <cerrno>
#include <climits>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#if !defined(_WIN32)
#include <sys/wait.h>
#endif
#include "lua.hpp"
#include "api.hpp"

namespace neos::lua_runtime
{
    void check_unrestricted(char const* aFunction)
    {
        auto const* h = host();
        if (h != nullptr && h->version >= 4u && h->restricted != nullptr && h->restricted() != 0)
        {
            std::string const message = "'" + std::string{ aFunction } + "' is restricted: it can access the host environment and the program is running in restricted mode";
            if (h->trap != nullptr)
                h->trap(message.c_str());
            runtime_error(message);
        }
    }

    void file_result(native_call& c, bool aOk, std::string const& aName)
    {
        // (true, or nil and the error's message and number)
        if (aOk)
        {
            c.ret(True);
            return;
        }
        auto const error = errno;
        c.ret(Nil);
        c.ret(make_string(aName.empty() ? std::string{ std::strerror(error) } : aName + ": " + std::strerror(error)));
        c.ret(make_integer(error));
    }

    namespace
    {
        bool local_time(std::time_t aTime, std::tm& aResult, bool aUtc)
        {
#if defined(_WIN32)
            return (aUtc ? ::gmtime_s(&aResult, &aTime) : ::localtime_s(&aResult, &aTime)) == 0;
#else
            return (aUtc ? ::gmtime_r(&aTime, &aResult) : ::localtime_r(&aTime, &aResult)) != nullptr;
#endif
        }

        std::time_t current_time()
        {
            return std::time(nullptr);
        }

        int field(table& t, char const* aKey, int aDefault, int aDelta)
        {
            auto const v = t.get(intern(aKey));
            if (is_nil(v))
            {
                if (aDefault < 0)
                    runtime_error(std::string{ "field '" } + aKey + "' missing in date table");
                return aDefault;
            }
            std::int64_t i;
            if (!to_integer(v, i, true))
            {
                if (is_string(v) || is_number(v))
                    runtime_error(std::string{ "field '" } + aKey + "' is not an integer");
                runtime_error(std::string{ "field '" } + aKey + "' is not an integer");
            }
            if (i < static_cast<std::int64_t>(INT_MIN) + aDelta || i > static_cast<std::int64_t>(INT_MAX) + aDelta)
                runtime_error(std::string{ "field '" } + aKey + "' is out-of-bound");
            return static_cast<int>(i - aDelta);
        }

        void set_fields(table& t, std::tm const& aTime)
        {
            t.set(intern("year"), make_integer(aTime.tm_year + 1900));
            t.set(intern("month"), make_integer(aTime.tm_mon + 1));
            t.set(intern("day"), make_integer(aTime.tm_mday));
            t.set(intern("hour"), make_integer(aTime.tm_hour));
            t.set(intern("min"), make_integer(aTime.tm_min));
            t.set(intern("sec"), make_integer(aTime.tm_sec));
            t.set(intern("yday"), make_integer(aTime.tm_yday + 1));
            t.set(intern("wday"), make_integer(aTime.tm_wday + 1));
            if (aTime.tm_isdst >= 0)
                t.set(intern("isdst"), boolean(aTime.tm_isdst != 0));
        }

        void os_time(native_call& c)
        {
            if (c.none(0u) || is_nil(c.arg(0u)))
            {
                c.ret(make_integer(static_cast<std::int64_t>(current_time())));
                return;
            }
            auto& t = check_table(c, 0u);
            std::tm time{};
            time.tm_year = field(t, "year", -1, 1900);
            time.tm_mon = field(t, "month", -1, 1);
            time.tm_mday = field(t, "day", -1, 0);
            time.tm_hour = field(t, "hour", 12, 0);
            time.tm_min = field(t, "min", 0, 0);
            time.tm_sec = field(t, "sec", 0, 0);
            auto const isdst = t.get(intern("isdst"));
            time.tm_isdst = is_nil(isdst) ? -1 : (truthy(isdst) ? 1 : 0);
            auto const result = std::mktime(&time);
            if (result == static_cast<std::time_t>(-1) && errno != 0)
                runtime_error("time result cannot be represented in this installation");
            set_fields(t, time);
            c.ret(make_integer(static_cast<std::int64_t>(result)));
        }

        void os_clock(native_call& c)
        {
            c.ret(make_float(static_cast<double>(std::clock()) / static_cast<double>(CLOCKS_PER_SEC)));
        }

        void os_date(native_call& c)
        {
            auto format = std::string{ optional_string(c, 0u, "%c") };
            std::time_t const time = c.none(1u) || is_nil(c.arg(1u)) ? current_time() : static_cast<std::time_t>(check_integer(c, 1u));
            bool utc = false;
            if (!format.empty() && format[0] == '!')
            {
                utc = true;
                format.erase(0u, 1u);
            }
            std::tm tm{};
            if (!local_time(time, tm, utc))
                runtime_error("date result cannot be represented in this installation");
            if (format.starts_with("*t"))
            {
                auto const t = make_table(0u, 9u);
                value_list keep{ t };
                set_fields(tab(t), tm);
                c.ret(t);
                return;
            }
            // (each conversion checked (as C99's strftime's))
            static char const* const valid = "aAbBcdHIjmMpSUwWxXyYZ%" "CDeFgGhnrRtTuV" "z";
            std::string result;
            for (std::size_t i = 0u; i < format.size(); ++i)
            {
                if (format[i] != '%')
                {
                    result.push_back(format[i]);
                    continue;
                }
                std::string conversion = "%";
                if (++i >= format.size())
                    argument_error(c, 0u, "invalid conversion specifier '%'");
                if (format[i] == 'E' || format[i] == 'O')
                {
                    // (C99's modifiers: %E of c, C, x, X, y and Y; %O of d, e, H, I, m, M, S, u, U, V, w, W and y)
                    auto const modifier = format[i];
                    conversion.push_back(modifier);
                    if (++i >= format.size())
                        argument_error(c, 0u, "invalid conversion specifier '" + conversion + "'");
                    conversion.push_back(format[i]);
                    if (std::strchr(modifier == 'E' ? "cCxXyY" : "deHImMSuUVwWy", format[i]) == nullptr)
                        argument_error(c, 0u, "invalid conversion specifier '" + conversion + "'");
                }
                else
                {
                    conversion.push_back(format[i]);
                    if (std::strchr(valid, format[i]) == nullptr)
                        argument_error(c, 0u, "invalid conversion specifier '" + conversion + "'");
                }
                char buffer[256];
                auto const n = std::strftime(buffer, sizeof(buffer), conversion.c_str(), &tm);
                result.append(buffer, n);
            }
            c.ret(make_string(std::move(result)));
        }

        void os_getenv(native_call& c)
        {
            auto const name = std::string{ check_string(c, 0u) };
            auto const* text = std::getenv(name.c_str());
            c.ret(text != nullptr ? make_string(text) : Nil);
        }

        void os_difftime(native_call& c)
        {
            auto const t2 = check_integer(c, 0u);
            auto const t1 = optional_integer(c, 1u, 0);
            c.ret(make_float(std::difftime(static_cast<std::time_t>(t2), static_cast<std::time_t>(t1))));
        }

        void os_remove(native_call& c)
        {
            check_unrestricted("os.remove");
            auto const path = std::string{ check_string(c, 0u) };
            std::error_code error;
            bool const ok = std::filesystem::remove(path, error);
            if (!ok)
                errno = error ? error.value() : ENOENT;
            file_result(c, ok, path);
        }

        void os_rename(native_call& c)
        {
            check_unrestricted("os.rename");
            auto const from = std::string{ check_string(c, 0u) };
            auto const to = std::string{ check_string(c, 1u) };
            file_result(c, std::rename(from.c_str(), to.c_str()) == 0, from);
        }

        void os_tmpname(native_call& c)
        {
            auto const directory = std::filesystem::temp_directory_path();
            static std::uint64_t sCounter = 0u;
            auto const now = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
            auto const path = directory / ("lua_" + std::to_string(now % 1000000007ull) + "_" + std::to_string(++sCounter));
            c.ret(make_string(path.string()));
        }

        void os_execute(native_call& c)
        {
            if (c.none(0u) || is_nil(c.arg(0u)))
            {
                c.ret(boolean(std::system(nullptr) != 0));
                return;
            }
            check_unrestricted("os.execute");
            auto const command = std::string{ check_string(c, 0u) };
            flush_standard(1);
            auto const status = std::system(command.c_str());
            if (status == -1)
            {
                file_result(c, false, command);
                return;
            }
#if defined(_WIN32)
            auto const code = status;
#else
            auto const code = WIFEXITED(status) ? WEXITSTATUS(status) : status;
#endif
            c.ret(code == 0 ? True : Nil);
            c.ret(intern("exit"));
            c.ret(make_integer(code));
        }

        void os_exit(native_call& c)
        {
            int code = 0;
            auto const v = c.arg(0u);
            if (is_nil(v) || v == True)
                code = 0;
            else if (v == False)
                code = 1;
            else
                code = static_cast<int>(check_integer(c, 0u));
            request_exit(code);
        }

        void os_setlocale(native_call& c)
        {
            auto const locale = c.arg(0u);
            if (is_nil(locale) || (is_string(locale) && (str(locale).empty() || str(locale) == "C" || str(locale) == "POSIX")))
                c.ret(intern("C"));
            else
                c.ret(Nil);
        }
    }

    void open_os()
    {
        auto const t = library_table("os");
        register_library(t, "os", {
            { "time", &os_time },
            { "clock", &os_clock },
            { "date", &os_date },
            { "getenv", &os_getenv },
            { "difftime", &os_difftime },
            { "remove", &os_remove },
            { "rename", &os_rename },
            { "tmpname", &os_tmpname },
            { "execute", &os_execute },
            { "exit", &os_exit },
            { "setlocale", &os_setlocale } });
    }
}
