/*
  time.cpp

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

  Portions of this file are derived from CPython 3.13 (Modules/timemodule.c), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE).
*/

// The time module (CPython 3.13's Modules/timemodule.c)

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            structseq_descriptor const& struct_time_type()
            {
                return structseq_type("time.struct_time", { "tm_year", "tm_mon", "tm_mday", "tm_hour", "tm_min", "tm_sec", "tm_wday", "tm_yday", "tm_isdst",
                    "tm_zone", "tm_gmtoff" }, 9u);
            }

            double now()
            {
                return std::chrono::duration<double>{ std::chrono::system_clock::now().time_since_epoch() }.count();
            }

            long long now_ns()
            {
                return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            }

            long long monotonic_ns()
            {
                return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            }

            long long process_ns()
            {
                return static_cast<long long>(static_cast<double>(std::clock()) * 1e9 / CLOCKS_PER_SEC);
            }

            bool to_tm(std::time_t aTime, bool aLocal, std::tm& aResult)
            {
#if defined(_WIN32)
                return (aLocal ? ::localtime_s(&aResult, &aTime) : ::gmtime_s(&aResult, &aTime)) == 0;
#else
                return (aLocal ? ::localtime_r(&aTime, &aResult) : ::gmtime_r(&aTime, &aResult)) != nullptr;
#endif
            }

            long long gmtoff(std::tm const& aTm, std::time_t aTime)
            {
#if defined(_WIN32)
                (void)aTm;
                std::tm utc{};
                ::gmtime_s(&utc, &aTime);
                utc.tm_isdst = -1;
                return static_cast<long long>(aTime - std::mktime(&utc));
#else
                (void)aTime;
                return aTm.tm_gmtoff;
#endif
            }

            std::string zone(std::tm const& aTm, bool aLocal)
            {
                if (!aLocal)
                    return "UTC";
                char buffer[64];
                auto const n = std::strftime(buffer, sizeof(buffer), "%Z", &aTm);
                return std::string{ buffer, n };
            }

            object* struct_time(std::tm const& aTm, bool aLocal, std::time_t aTime)
            {
                return make_structseq(struct_time_type(), { make_int(aTm.tm_year + 1900), make_int(aTm.tm_mon + 1), make_int(aTm.tm_mday),
                    make_int(aTm.tm_hour), make_int(aTm.tm_min), make_int(aTm.tm_sec), make_int((aTm.tm_wday + 6) % 7), make_int(aTm.tm_yday + 1),
                    make_int(aLocal ? aTm.tm_isdst : 0), make_str(zone(aTm, aLocal)), make_int(aLocal ? gmtoff(aTm, aTime) : 0) });
            }

            // (the seconds argument of gmtime, localtime and ctime: None for now)
            bool seconds_of(object* aValue, std::time_t& aResult)
            {
                if (aValue == nullptr || kind_of(aValue) == NEOS_PY_NONE)
                {
                    aResult = std::time(nullptr);
                    return true;
                }
                double seconds = 0.0;
                if (kind_of(aValue) == NEOS_PY_INT || kind_of(aValue) == NEOS_PY_BOOL)
                {
                    long long i = 0;
                    if (!to_index(aValue, i))
                        return false;
                    aResult = static_cast<std::time_t>(i);
                    return true;
                }
                if (!to_double(aValue, seconds))
                    return false;
                if (std::isnan(seconds))
                {
                    error("ValueError", "Invalid value NaN (not a number)");
                    return false;
                }
                aResult = static_cast<std::time_t>(std::floor(seconds));
                return true;
            }

            // (a struct_time or a 9-tuple as a tm)
            bool tm_of(object* aValue, std::tm& aResult, char const* aFunction)
            {
                std::vector<object*> items;
                if (!structseq_items(aValue, items))
                {
                    if (kind_of(aValue) != NEOS_PY_TUPLE)
                    {
                        error("TypeError", std::string{ "Tuple or struct_time argument required" });
                        return false;
                    }
                    items = as<tuple_object>(aValue)->items;
                }
                if (items.size() != 9u)
                {
                    error("TypeError", std::string{ aFunction } + "(): illegal time tuple argument");
                    return false;
                }
                long long v[9];
                for (std::size_t i = 0u; i < 9u; ++i)
                    if (!to_index(items[i], v[i]))
                        return false;
                aResult = std::tm{};
                aResult.tm_year = static_cast<int>(v[0] - 1900);
                aResult.tm_mon = static_cast<int>(v[1] - 1);
                aResult.tm_mday = static_cast<int>(v[2]);
                aResult.tm_hour = static_cast<int>(v[3]);
                aResult.tm_min = static_cast<int>(v[4]);
                aResult.tm_sec = static_cast<int>(v[5]);
                aResult.tm_wday = static_cast<int>((v[6] + 1) % 7);
                aResult.tm_yday = static_cast<int>(v[7] - 1);
                aResult.tm_isdst = static_cast<int>(v[8]);
                return true;
            }

            bool now_tm(object* aValue, std::tm& aResult, char const* aFunction)
            {
                if (aValue == nullptr)
                {
                    auto const t = std::time(nullptr);
                    return to_tm(t, true, aResult);
                }
                return tm_of(aValue, aResult, aFunction);
            }

            std::string asctime(std::tm const& aTm)
            {
                static char const* const sDays[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
                static char const* const sMonths[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
                char buffer[64];
                std::snprintf(buffer, sizeof(buffer), "%s %s %2d %02d:%02d:%02d %d", sDays[(aTm.tm_wday % 7 + 7) % 7], sMonths[(aTm.tm_mon % 12 + 12) % 12],
                    aTm.tm_mday, aTm.tm_hour, aTm.tm_min, aTm.tm_sec, aTm.tm_year + 1900);
                return buffer;
            }

            std::string strftime(std::string const& aFormat, std::tm const& aTm)
            {
                if (aFormat.empty())
                    return {};
                thread_local std::vector<char> tBuffer;
                tBuffer.assign(256u + aFormat.size() * 8u, '\0');
                for (;;)
                {
                    auto const n = std::strftime(tBuffer.data(), tBuffer.size(), aFormat.c_str(), &aTm);
                    if (n != 0u || tBuffer.size() > 65536u)
                        return std::string{ tBuffer.data(), n };
                    tBuffer.resize(tBuffer.size() * 4u);
                }
            }

            // (days since 1970-01-01 of a date (proleptic Gregorian))
            long long days_from_civil(long long y, unsigned m, unsigned d)
            {
                y -= m <= 2u;
                auto const era = (y >= 0 ? y : y - 399) / 400;
                auto const yoe = static_cast<unsigned>(y - era * 400);
                auto const doy = (153u * (m + (m > 2u ? -3 : 9)) + 2u) / 5u + d - 1u;
                auto const doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
                return era * 146097 + static_cast<long long>(doe) - 719468;
            }

            // strptime (the common directives)
            object* strptime(std::string const& aText, std::string const& aFormat)
            {
                static char const* const sDays[] = { "monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday" };
                static char const* const sMonths[] = { "january", "february", "march", "april", "may", "june", "july", "august", "september", "october", "november", "december" };
                int year = 1900, month = 1, day = 1, hour = 0, minute = 0, second = 0, weekday = -1, yearday = -1;
                bool pm = false, ampm = false;
                std::size_t p = 0u;
                auto const mismatch = [&]() -> object*
                {
                    return error("ValueError", "time data " + std::string{ "'" } + aText + "' does not match format '" + aFormat + "'");
                };
                auto const number = [&](int aMaxDigits, int& aResult) -> bool
                {
                    std::size_t n = 0u;
                    int value = 0;
                    bool negative = false;
                    if (p < aText.size() && (aText[p] == '+' || aText[p] == '-') && aMaxDigits > 4)
                        negative = aText[p++] == '-';
                    while (p < aText.size() && n < static_cast<std::size_t>(aMaxDigits) && std::isdigit(static_cast<unsigned char>(aText[p])))
                    {
                        value = value * 10 + (aText[p++] - '0');
                        ++n;
                    }
                    aResult = negative ? -value : value;
                    return n > 0u;
                };
                auto const name = [&](char const* const* aNames, std::size_t aCount, bool aAbbreviated, int& aResult) -> bool
                {
                    for (std::size_t i = 0u; i < aCount; ++i)
                    {
                        std::string const n = aAbbreviated ? std::string{ aNames[i] }.substr(0u, 3u) : std::string{ aNames[i] };
                        if (p + n.size() <= aText.size())
                        {
                            bool match = true;
                            for (std::size_t k = 0u; k < n.size(); ++k)
                                if (std::tolower(static_cast<unsigned char>(aText[p + k])) != n[k])
                                    match = false;
                            if (match)
                            {
                                p += n.size();
                                aResult = static_cast<int>(i);
                                return true;
                            }
                        }
                    }
                    return false;
                };
                for (std::size_t f = 0u; f < aFormat.size(); ++f)
                {
                    char const c = aFormat[f];
                    if (c != '%' || f + 1u >= aFormat.size())
                    {
                        if (std::isspace(static_cast<unsigned char>(c)))
                        {
                            while (p < aText.size() && std::isspace(static_cast<unsigned char>(aText[p])))
                                ++p;
                            continue;
                        }
                        if (p >= aText.size() || aText[p] != c)
                            return mismatch();
                        ++p;
                        continue;
                    }
                    char const d = aFormat[++f];
                    int v = 0;
                    bool ok = true;
                    switch (d)
                    {
                    case 'Y': ok = number(4, year); break;
                    case 'y': ok = number(2, v); year = v < 69 ? 2000 + v : 1900 + v; break;
                    case 'm': ok = number(2, month); break;
                    case 'd': ok = number(2, day); break;
                    case 'H': ok = number(2, hour); break;
                    case 'I': ok = number(2, hour); ampm = true; break;
                    case 'M': ok = number(2, minute); break;
                    case 'S': ok = number(2, second); break;
                    case 'j': ok = number(3, yearday); break;
                    case 'f': ok = number(6, v); break;
                    case 'p':
                        if (p + 2u <= aText.size() && (std::tolower(static_cast<unsigned char>(aText[p])) == 'a' || std::tolower(static_cast<unsigned char>(aText[p])) == 'p') &&
                            std::tolower(static_cast<unsigned char>(aText[p + 1u])) == 'm')
                        {
                            pm = std::tolower(static_cast<unsigned char>(aText[p])) == 'p';
                            p += 2u;
                        }
                        else
                            ok = false;
                        break;
                    case 'b': case 'h': ok = name(sMonths, 12u, true, month); ++month; break;
                    case 'B': ok = name(sMonths, 12u, false, month); ++month; break;
                    case 'a': ok = name(sDays, 7u, true, weekday); break;
                    case 'A': ok = name(sDays, 7u, false, weekday); break;
                    case '%': ok = p < aText.size() && aText[p++] == '%'; break;
                    case 'Z':
                        while (p < aText.size() && std::isalpha(static_cast<unsigned char>(aText[p])))
                            ++p;
                        break;
                    case 'z':
                        ok = number(5, v);
                        break;
                    default:
                        return error("ValueError", std::string{ "'" } + d + "' is a bad directive in format '%" + d + "'");
                    }
                    if (!ok)
                        return mismatch();
                }
                if (p != aText.size())
                    return error("ValueError", "unconverted data remains: " + aText.substr(p));
                if (ampm)
                    hour = hour % 12 + (pm ? 12 : 0);
                auto const days = days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
                if (weekday < 0)
                    weekday = static_cast<int>(((days % 7) + 7 + 3) % 7);
                if (yearday < 0)
                    yearday = static_cast<int>(days - days_from_civil(year, 1u, 1u) + 1);
                return make_structseq(struct_time_type(), { make_int(year), make_int(month), make_int(day), make_int(hour), make_int(minute), make_int(second),
                    make_int(weekday), make_int(yearday), make_int(-1), (retain(none()), none()), (retain(none()), none()) });
            }

            builtin_entry const sTimeFunctions[] =
            {
                // (a clock's information: the clock that implements it (the runtime's: std::chrono's clocks and std::clock))
                { "get_clock_info", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("get_clock_info", aArgs, aKwargs, { "name" }, 1u, 1u, a))
                            return nullptr;
                        if (kind_of(a[0]) != NEOS_PY_STR)
                            return error("TypeError", std::string{ "get_clock_info() argument must be str, not " } + type_name(a[0]));
                        auto const& name = as<str_object>(a[0])->value;
                        char const* implementation = nullptr;
                        bool monotonic = true;
                        bool adjustable = false;
                        double resolution = 1e-9;
#if defined(_WIN32)
                        if (name == "monotonic" || name == "perf_counter")
                            implementation = "QueryPerformanceCounter()", resolution = 1e-7;
                        else if (name == "time")
                            implementation = "GetSystemTimePreciseAsFileTime()", monotonic = false, adjustable = true, resolution = 1e-7;
                        else if (name == "process_time" || name == "thread_time")
                            implementation = "clock()", resolution = 1.0 / CLOCKS_PER_SEC;
#else
                        if (name == "monotonic" || name == "perf_counter")
                            implementation = "clock_gettime(CLOCK_MONOTONIC)";
                        else if (name == "time")
                            implementation = "clock_gettime(CLOCK_REALTIME)", monotonic = false, adjustable = true;
                        else if (name == "process_time" || name == "thread_time")
                            implementation = "clock()", resolution = 1.0 / CLOCKS_PER_SEC;
#endif
                        if (implementation == nullptr)
                            return error("ValueError", "unknown clock");
                        ref const ns{ module_attribute("types", "SimpleNamespace") };
                        if (!ns)
                            return nullptr;
                        ref const kw{ make_dict() };
                        for (auto const& [key, value] : { std::pair<char const*, object*>{ "implementation", make_str(implementation) },
                            std::pair<char const*, object*>{ "monotonic", make_bool(monotonic) }, std::pair<char const*, object*>{ "adjustable", make_bool(adjustable) },
                            std::pair<char const*, object*>{ "resolution", make_float(resolution) } })
                        {
                            ref const k{ make_str(key) };
                            ref const v{ value };
                            dict_set(kw.get(), k.get(), v.get());
                        }
                        ref const args{ make_tuple() };
                        return call(ns.get(), args.get(), kw.get());
                    } },
                { "time", [](std::vector<object*> const&, object*) -> object* { return make_float(now()); } },
                { "time_ns", [](std::vector<object*> const&, object*) -> object* { return make_int(now_ns()); } },
                { "monotonic", [](std::vector<object*> const&, object*) -> object* { return make_float(static_cast<double>(monotonic_ns()) / 1e9); } },
                { "monotonic_ns", [](std::vector<object*> const&, object*) -> object* { return make_int(monotonic_ns()); } },
                { "perf_counter", [](std::vector<object*> const&, object*) -> object* { return make_float(static_cast<double>(monotonic_ns()) / 1e9); } },
                { "perf_counter_ns", [](std::vector<object*> const&, object*) -> object* { return make_int(monotonic_ns()); } },
                { "process_time", [](std::vector<object*> const&, object*) -> object* { return make_float(static_cast<double>(process_ns()) / 1e9); } },
                { "process_time_ns", [](std::vector<object*> const&, object*) -> object* { return make_int(process_ns()); } },
                { "thread_time", [](std::vector<object*> const&, object*) -> object* { return make_float(static_cast<double>(process_ns()) / 1e9); } },
                { "thread_time_ns", [](std::vector<object*> const&, object*) -> object* { return make_int(process_ns()); } },
                { "sleep", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("sleep", aArgs, aKwargs, { "seconds" }, 1u, 1u, a))
                            return nullptr;
                        double seconds = 0.0;
                        if (kind_of(a[0]) != NEOS_PY_INT && kind_of(a[0]) != NEOS_PY_FLOAT && kind_of(a[0]) != NEOS_PY_BOOL)
                            return error("TypeError", std::string{ "'" } + type_name(a[0]) + "' object cannot be interpreted as an integer");
                        if (!to_double(a[0], seconds))
                            return nullptr;
                        if (std::isnan(seconds))
                            return error("ValueError", "Invalid value NaN (not a number)");
                        if (seconds < 0.0)
                            return error("ValueError", "sleep length must be non-negative");
                        // (in slices: the handlers of signals that arrive are called (an exception that one raises ends the sleep))
                        auto const until = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>{ seconds });
                        for (;;)
                        {
                            auto const now = std::chrono::steady_clock::now();
                            if (now >= until)
                                break;
                            gil_release();
                            std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(until - now, std::chrono::milliseconds{ 20 }));
                            gil_acquire();
                            if (!signals_check())
                                return nullptr;
                        }
                        return none();
                    } },
                { "gmtime", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("gmtime", aArgs, aKwargs, { "seconds" }, 0u, 1u, a))
                            return nullptr;
                        std::time_t t = 0;
                        std::tm tm{};
                        if (!seconds_of(a[0], t))
                            return nullptr;
                        if (!to_tm(t, false, tm))
                            return error("OverflowError", "timestamp out of range for platform time_t");
                        return struct_time(tm, false, t);
                    } },
                { "localtime", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("localtime", aArgs, aKwargs, { "seconds" }, 0u, 1u, a))
                            return nullptr;
                        std::time_t t = 0;
                        std::tm tm{};
                        if (!seconds_of(a[0], t))
                            return nullptr;
                        if (!to_tm(t, true, tm))
                            return error("OverflowError", "timestamp out of range for platform time_t");
                        return struct_time(tm, true, t);
                    } },
                { "mktime", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("mktime", aArgs, aKwargs, { "t" }, 1u, 1u, a))
                            return nullptr;
                        std::tm tm{};
                        if (!tm_of(a[0], tm, "mktime"))
                            return nullptr;
                        auto const t = std::mktime(&tm);
                        if (t == static_cast<std::time_t>(-1) && tm.tm_year != 69)
                            return error("OverflowError", "mktime argument out of range");
                        return make_float(static_cast<double>(t));
                    } },
                { "asctime", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("asctime", aArgs, aKwargs, { "t" }, 0u, 1u, a))
                            return nullptr;
                        std::tm tm{};
                        if (!now_tm(a[0], tm, "asctime"))
                            return nullptr;
                        return make_str(asctime(tm));
                    } },
                { "ctime", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("ctime", aArgs, aKwargs, { "seconds" }, 0u, 1u, a))
                            return nullptr;
                        std::time_t t = 0;
                        std::tm tm{};
                        if (!seconds_of(a[0], t))
                            return nullptr;
                        if (!to_tm(t, true, tm))
                            return error("OverflowError", "timestamp out of range for platform time_t");
                        return make_str(asctime(tm));
                    } },
                { "strftime", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("strftime", aArgs, aKwargs, { "format", "t" }, 1u, 2u, a))
                            return nullptr;
                        if (kind_of(a[0]) != NEOS_PY_STR)
                            return error("TypeError", std::string{ "strftime() argument 1 must be str, not " } + type_name(a[0]));
                        std::tm tm{};
                        if (!now_tm(a[1], tm, "strftime"))
                            return nullptr;
                        if (tm.tm_mon < 0 || tm.tm_mon > 11)
                            return error("ValueError", "month out of range");
                        if (tm.tm_mday < 0 || tm.tm_mday > 31)
                            return error("ValueError", "day of month out of range");
                        return make_str(strftime(as<str_object>(a[0])->value, tm));
                    } },
                { "strptime", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("strptime", aArgs, aKwargs, { "string", "format" }, 1u, 2u, a))
                            return nullptr;
                        if (kind_of(a[0]) != NEOS_PY_STR || (a[1] != nullptr && kind_of(a[1]) != NEOS_PY_STR))
                            return error("TypeError", std::string{ "strptime() argument 1 must be str, not " } + type_name(kind_of(a[0]) != NEOS_PY_STR ? a[0] : a[1]));
                        return strptime(as<str_object>(a[0])->value, a[1] != nullptr ? as<str_object>(a[1])->value : std::string{ "%a %b %d %H:%M:%S %Y" });
                    } }
            };

            bool time_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sTimeFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("struct_time", type_object_for("struct_time", "time.struct_time"));
                // (the local time zone)
                auto const t = std::time(nullptr);
                std::tm local{};
                to_tm(t, true, local);
                std::tm january{};
                january.tm_year = local.tm_year;
                january.tm_mday = 1;
                january.tm_isdst = -1;
                std::time_t const j = std::mktime(&january);
                std::tm july = january;
                july.tm_mon = 6;
                std::time_t const jl = std::mktime(&july);
                std::tm jtm{}, ltm{};
                to_tm(j, true, jtm);
                to_tm(jl, true, ltm);
                auto const janOffset = gmtoff(jtm, j);
                auto const julOffset = gmtoff(ltm, jl);
                auto const standard = std::min(janOffset, julOffset);
                auto const summer = std::max(janOffset, julOffset);
                aAttributes.emplace_back("timezone", make_int(-standard));
                aAttributes.emplace_back("altzone", make_int(-summer));
                aAttributes.emplace_back("daylight", make_int(janOffset != julOffset ? 1 : 0));
                auto const standardZone = zone(janOffset <= julOffset ? jtm : ltm, true);
                auto const summerZone = zone(janOffset <= julOffset ? ltm : jtm, true);
                aAttributes.emplace_back("tzname", make_tuple({ make_str(standardZone), make_str(summerZone) }));
                return true;
            }

            inline bool const sTime = register_module("time", time_module);
        }
    }
}
