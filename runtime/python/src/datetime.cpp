/*
  datetime.cpp

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

  Portions of this file are derived from CPython 3.13 (Modules/_datetimemodule.c and Lib/_strptime.py), Copyright (c)
  2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License
  Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from C and Python to C++ for the neos Python runtime
  plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// The module datetime as CPython 3.13 implements it (Modules/_datetimemodule.c, and Lib/_strptime.py for strptime): timedelta, date,
// time, datetime, tzinfo and timezone

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <optional>
#include <string>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            using B = big_integer;

            object* borrowed(object* aObject) { retain(aObject); return aObject; }

            long long ll_of(B const& aValue)
            {
                auto const value = std::stoll(big_digits(big_abs(aValue), 10));
                return big_negative(aValue) ? -value : value;
            }

            long long floor_div(long long a, long long b) { auto q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) --q; return q; }
            long long floor_mod(long long a, long long b) { return a - floor_div(a, b) * b; }

            std::string pad(long long aValue, int aWidth, char aFill = '0')
            {
                auto text = std::to_string(aValue < 0 ? -aValue : aValue);
                if (static_cast<int>(text.size()) < aWidth)
                    text.insert(0u, static_cast<std::size_t>(aWidth) - text.size(), aFill);
                return (aValue < 0 ? "-" : "") + text;
            }

            // --- the proleptic Gregorian calendar

            constexpr int sMinYear = 1;
            constexpr int sMaxYear = 9999;
            constexpr long long sMaxOrdinal = 3652059;
            constexpr long long sMaxDeltaDays = 999999999;
            constexpr long long sEpoch = 719163LL * 24 * 60 * 60;   ///< (1970-01-01 in seconds since 0001-01-01 (as ordinal 0))
            constexpr long long sMaxFoldSeconds = 24 * 3600;

            char const* const sDayNames[] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };
            char const* const sMonthNames[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };

            bool is_leap(long long y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
            int days_in_month(long long y, int m)
            {
                static int const sDays[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
                return m == 2 && is_leap(y) ? 29 : sDays[m];
            }
            long long days_before_year(long long y) { auto const y1 = y - 1; return y1 * 365 + y1 / 4 - y1 / 100 + y1 / 400; }
            long long days_before_month(long long y, int m)
            {
                static int const sBefore[] = { 0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
                return sBefore[m] + (m > 2 && is_leap(y) ? 1 : 0);
            }
            long long ymd_to_ord(long long y, int m, int d) { return days_before_year(y) + days_before_month(y, m) + d; }
            void ord_to_ymd(long long n, int& aYear, int& aMonth, int& aDay)
            {
                n -= 1;
                auto const n400 = n / 146097;
                n %= 146097;
                long long year = n400 * 400 + 1;
                auto const n100 = n / 36524;
                n %= 36524;
                auto const n4 = n / 1461;
                n %= 1461;
                auto const n1 = n / 365;
                n %= 365;
                year += n100 * 100 + n4 * 4 + n1;
                if (n1 == 4 || n100 == 4)
                {
                    aYear = static_cast<int>(year - 1);
                    aMonth = 12;
                    aDay = 31;
                    return;
                }
                bool const leap = n1 == 3 && (n4 != 24 || n100 == 3);
                int month = static_cast<int>((n + 50) >> 5);
                static int const sBefore[] = { 0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
                static int const sDays[] = { 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
                long long preceding = sBefore[month] + (month > 2 && leap ? 1 : 0);
                if (preceding > n)
                {
                    --month;
                    preceding -= sDays[month] + (month == 2 && leap ? 1 : 0);
                }
                aYear = static_cast<int>(year);
                aMonth = month;
                aDay = static_cast<int>(n - preceding + 1);
            }
            int weekday_of(long long y, int m, int d) { return static_cast<int>((ymd_to_ord(y, m, d) + 6) % 7); }
            long long iso_week1_monday(long long year)
            {
                auto const firstDay = ymd_to_ord(year, 1, 1);
                auto const firstWeekday = (firstDay + 6) % 7;
                auto week1Monday = firstDay - firstWeekday;
                if (firstWeekday > 3)
                    week1Monday += 7;
                return week1Monday;
            }
            void iso_calendar(long long y, int m, int d, long long& aYear, long long& aWeek, long long& aDay)
            {
                auto year = y;
                auto week1Monday = iso_week1_monday(year);
                auto const today = ymd_to_ord(y, m, d);
                auto week = floor_div(today - week1Monday, 7);
                auto day = floor_mod(today - week1Monday, 7);
                if (week < 0)
                {
                    --year;
                    week1Monday = iso_week1_monday(year);
                    week = floor_div(today - week1Monday, 7);
                    day = floor_mod(today - week1Monday, 7);
                }
                else if (week >= 52 && today >= iso_week1_monday(year + 1))
                {
                    ++year;
                    week = 0;
                }
                aYear = year;
                aWeek = week + 1;
                aDay = day + 1;
            }

            // --- values

            struct td
            {
                long long days = 0;
                long long seconds = 0;
                long long us = 0;
                bool zero() const { return days == 0 && seconds == 0 && us == 0; }
            };

            B us_of(td const& aDelta) { return B{ aDelta.days } * B{ 86400000000ll } + B{ aDelta.seconds } * B{ 1000000 } + B{ aDelta.us }; }

            // (microseconds as a timedelta (OverflowError if out of range))
            bool delta_of(B const& aMicroseconds, td& aResult)
            {
                B s, us, d, sec;
                big_divmod(aMicroseconds, B{ 1000000 }, s, us);
                big_divmod(s, B{ 86400 }, d, sec);
                if (B{ 2147483647 } < big_abs(d))
                {
                    error("OverflowError", "Python int too large to convert to C int");
                    return false;
                }
                auto const days = ll_of(d);
                if (days < -sMaxDeltaDays || days > sMaxDeltaDays)
                {
                    error("OverflowError", "days=" + std::to_string(days) + "; must have magnitude <= " + std::to_string(sMaxDeltaDays));
                    return false;
                }
                aResult = td{ days, ll_of(sec), ll_of(us) };
                return true;
            }

            td delta_of(long long aMicroseconds)
            {
                auto const s = floor_div(aMicroseconds, 1000000);
                return td{ floor_div(s, 86400), floor_mod(s, 86400), floor_mod(aMicroseconds, 1000000) };
            }

            long long small_us(td const& aDelta) { return (aDelta.days * 86400 + aDelta.seconds) * 1000000 + aDelta.us; }   // (|days| < 2**30: as a date's range)

            int compare_delta(td const& a, td const& b)
            {
                if (a.days != b.days) return a.days < b.days ? -1 : 1;
                if (a.seconds != b.seconds) return a.seconds < b.seconds ? -1 : 1;
                if (a.us != b.us) return a.us < b.us ? -1 : 1;
                return 0;
            }

            struct fields
            {
                int year = 1;
                int month = 1;
                int day = 1;
                int hour = 0;
                int minute = 0;
                int second = 0;
                int us = 0;
                int fold = 0;
                long long ordinal() const { return ymd_to_ord(year, month, day); }
                long long seconds() const { return hour * 3600LL + minute * 60LL + second; }
            };

            // --- objects

            struct timedelta_object { header h; td value; };
            struct temporal_object { header h; fields f; object* tz = nullptr; ~temporal_object() { release(tz); } };  // (date, time and datetime (tz: null for None))
            struct timezone_object { header h; td offset; std::optional<std::string> name; };

            neos_py_type make_timedelta_type();
            neos_py_type make_date_type();
            neos_py_type make_time_type();
            neos_py_type make_datetime_type();
            neos_py_type make_timezone_type();
            neos_py_type sTimedeltaType = make_timedelta_type();
            neos_py_type sDateType = make_date_type();
            neos_py_type sTimeType = make_time_type();
            neos_py_type sDatetimeType = make_datetime_type();
            neos_py_type sTimezoneType = make_timezone_type();

            object* sTzinfoClass = nullptr;

            bool is_timedelta(object* aObject) { return aObject != nullptr && aObject->type == &sTimedeltaType.base; }
            bool is_date(object* aObject) { return aObject != nullptr && aObject->type == &sDateType.base; }
            bool is_time(object* aObject) { return aObject != nullptr && aObject->type == &sTimeType.base; }
            bool is_datetime(object* aObject) { return aObject != nullptr && aObject->type == &sDatetimeType.base; }
            bool is_date_like(object* aObject) { return is_date(aObject) || is_datetime(aObject); }
            bool is_timezone(object* aObject) { return aObject != nullptr && aObject->type == &sTimezoneType.base; }
            bool is_ours(object* aObject) { return is_timedelta(aObject) || is_date(aObject) || is_time(aObject) || is_datetime(aObject) || is_timezone(aObject); }

            td const& delta(object* aObject) { return as<timedelta_object>(aObject)->value; }
            fields const& fields_of(object* aObject) { return as<temporal_object>(aObject)->f; }
            object* tz_of(object* aObject) { return as<temporal_object>(aObject)->tz; }
            timezone_object const& zone(object* aObject) { return *as<timezone_object>(aObject); }

            object* make_timedelta(td aValue) { return &(new timedelta_object{ { { &sTimedeltaType.base } }, aValue })->h.base; }
            object* make_temporal(neos_py_type& aType, fields const& aFields, object* aTz)
            {
                if (aTz != nullptr && kind_of(aTz) == NEOS_PY_NONE)
                    aTz = nullptr;
                retain(aTz);
                return &(new temporal_object{ { { &aType.base } }, aFields, aTz })->h.base;
            }
            object* make_date(int y, int m, int d) { fields f; f.year = y; f.month = m; f.day = d; return make_temporal(sDateType, f, nullptr); }
            object* make_time(fields const& aFields, object* aTz) { return make_temporal(sTimeType, aFields, aTz); }
            object* make_datetime(fields const& aFields, object* aTz) { return make_temporal(sDatetimeType, aFields, aTz); }

            object* utc()
            {
                static object* sUtc = &(new timezone_object{ { { &sTimezoneType.base } }, td{}, std::nullopt })->h.base;
                return borrowed(sUtc);
            }
            object* make_timezone(td const& aOffset, std::optional<std::string> aName)
            {
                if (!aName && aOffset.zero())
                    return utc();
                return &(new timezone_object{ { { &sTimezoneType.base } }, aOffset, std::move(aName) })->h.base;
            }

            char const* qualified_name(object* aObject)
            {
                if (is_timedelta(aObject)) return "datetime.timedelta";
                if (is_date(aObject)) return "datetime.date";
                if (is_time(aObject)) return "datetime.time";
                if (is_datetime(aObject)) return "datetime.datetime";
                if (is_timezone(aObject)) return "datetime.timezone";
                return type_name(aObject);
            }

            std::string repr_text(object* aObject)
            {
                std::string text;
                repr_of(aObject, text);
                return text;
            }

            // --- operator errors (with CPython's type names); null: not ours to raise (NotImplemented)

            char const* symbol_of(int aOperation)
            {
                switch (aOperation)
                {
                case NEOS_PY_ADD: return "+";
                case NEOS_PY_SUBTRACT: return "-";
                case NEOS_PY_MULTIPLY: return "*";
                case NEOS_PY_TRUE_DIVIDE: return "/";
                case NEOS_PY_FLOOR_DIVIDE: return "//";
                case NEOS_PY_REMAINDER: return "%";
                case NEOS_PY_POWER: return "**";
                case NEOS_PY_MATMUL: return "@";
                case NEOS_PY_LSHIFT: return "<<";
                case NEOS_PY_RSHIFT: return ">>";
                case NEOS_PY_AND: return "&";
                case NEOS_PY_OR: return "|";
                default: return "^";
                }
            }

            bool raises_for(object* aOther) { return kind_of(aOther) != NEOS_PY_OTHER || is_ours(aOther); }

            object* unsupported(int aOperation, object* aLhs, object* aRhs, int aInplace)
            {
                if (!raises_for(aLhs) || !raises_for(aRhs))
                    return borrowed(not_implemented());
                return error("TypeError", std::string{ "unsupported operand type(s) for " } + symbol_of(aOperation) + (aInplace != 0 ? "=" : "") + ": '" +
                    qualified_name(aLhs) + "' and '" + qualified_name(aRhs) + "'");
            }

            object* compare_result(int aOperation, int aDifference)
            {
                switch (aOperation)
                {
                case NEOS_PY_LT: return make_bool(aDifference < 0);
                case NEOS_PY_LE: return make_bool(aDifference <= 0);
                case NEOS_PY_EQ: return make_bool(aDifference == 0);
                case NEOS_PY_NE: return make_bool(aDifference != 0);
                case NEOS_PY_GT: return make_bool(aDifference > 0);
                default: return make_bool(aDifference >= 0);
                }
            }

            object* not_comparable(int aOperation, object* aLhs, object* aRhs)
            {
                if (aOperation == NEOS_PY_EQ || aOperation == NEOS_PY_NE || !raises_for(aLhs) || !raises_for(aRhs))
                    return borrowed(not_implemented());
                static char const* const sSymbols[] = { "<", "<=", "==", "!=", ">", ">=" };
                return error("TypeError", std::string{ "'" } + sSymbols[aOperation - NEOS_PY_LT] + "' not supported between instances of '" + qualified_name(aLhs) +
                    "' and '" + qualified_name(aRhs) + "'");
            }

            // --- arguments

            bool int_arg(object* aValue, long long& aResult)
            {
                if (kind_of(aValue) == NEOS_PY_FLOAT)
                {
                    error("TypeError", "'float' object cannot be interpreted as an integer");
                    return false;
                }
                return to_index(aValue, aResult);
            }

            bool int_arg(object* aValue, int& aResult, int aDefault)
            {
                if (aValue == nullptr)
                {
                    aResult = aDefault;
                    return true;
                }
                long long value = 0;
                if (!int_arg(aValue, value))
                    return false;
                if (value < -2147483648ll || value > 2147483647ll)
                {
                    error("OverflowError", value < 0 ? "signed integer is less than minimum" : "signed integer is greater than maximum");
                    return false;
                }
                aResult = static_cast<int>(value);
                return true;
            }

            bool check_date(long long y, long long m, long long d)
            {
                if (y < sMinYear || y > sMaxYear)
                {
                    error("ValueError", "year " + std::to_string(y) + " is out of range");
                    return false;
                }
                if (m < 1 || m > 12)
                {
                    error("ValueError", "month must be in 1..12");
                    return false;
                }
                if (d < 1 || d > days_in_month(y, static_cast<int>(m)))
                {
                    error("ValueError", "day is out of range for month");
                    return false;
                }
                return true;
            }

            bool check_time(long long h, long long m, long long s, long long us, long long fold)
            {
                if (h < 0 || h > 23) { error("ValueError", "hour must be in 0..23"); return false; }
                if (m < 0 || m > 59) { error("ValueError", "minute must be in 0..59"); return false; }
                if (s < 0 || s > 59) { error("ValueError", "second must be in 0..59"); return false; }
                if (us < 0 || us > 999999) { error("ValueError", "microsecond must be in 0..999999"); return false; }
                if (fold != 0 && fold != 1) { error("ValueError", "fold must be either 0 or 1"); return false; }
                return true;
            }

            bool is_tzinfo(object* aObject)
            {
                if (is_timezone(aObject))
                    return true;
                if (sTzinfoClass == nullptr)
                    return false;
                ref const result{ b_isinstance({ aObject, sTzinfoClass }, nullptr) };
                if (!result)
                {
                    clear_error();
                    return false;
                }
                return result.get() == make_bool(true);
            }

            bool check_tzinfo(object* aObject)
            {
                if (aObject == nullptr || kind_of(aObject) == NEOS_PY_NONE || is_tzinfo(aObject))
                    return true;
                error("TypeError", std::string{ "tzinfo argument must be None or of a tzinfo subclass, not type '" } + type_name(aObject) + "'");
                return false;
            }

            // --- tzinfo's methods (utcoffset and dst: 1 a timedelta, 0 None, -1 an exception)

            bool check_offset(td const& aOffset)
            {
                if (aOffset.days < -1 || aOffset.days >= 1 || (aOffset.days == -1 && aOffset.seconds == 0 && aOffset.us == 0))
                {
                    ref const value{ make_timedelta(aOffset) };
                    error("ValueError", "offset must be a timedelta strictly between -timedelta(hours=24) and timedelta(hours=24), not " + repr_text(value.get()) + ".");
                    return false;
                }
                return true;
            }

            int tz_offset(object* aTz, char const* aMethod, object* aArgument, td& aResult)
            {
                if (aTz == nullptr || kind_of(aTz) == NEOS_PY_NONE)
                    return 0;
                if (is_timezone(aTz))
                {
                    if (std::strcmp(aMethod, "dst") == 0)
                        return 0;
                    aResult = zone(aTz).offset;
                    return 1;
                }
                ref const result{ call_method_named(aTz, aMethod, { borrowed(aArgument != nullptr ? aArgument : none()) }) };
                if (!result)
                    return -1;
                if (kind_of(result.get()) == NEOS_PY_NONE)
                    return 0;
                if (!is_timedelta(result.get()))
                {
                    error("TypeError", std::string{ "tzinfo." } + aMethod + "() must return None or timedelta, not '" + type_name(result.get()) + "'");
                    return -1;
                }
                aResult = delta(result.get());
                return check_offset(aResult) ? 1 : -1;
            }

            // (tzname: false: an exception)
            bool tz_name(object* aTz, object* aArgument, std::optional<std::string>& aResult)
            {
                aResult.reset();
                if (aTz == nullptr || kind_of(aTz) == NEOS_PY_NONE)
                    return true;
                ref const result{ call_method_named(aTz, "tzname", { borrowed(aArgument != nullptr ? aArgument : none()) }) };
                if (!result)
                    return false;
                if (kind_of(result.get()) == NEOS_PY_NONE)
                    return true;
                if (kind_of(result.get()) != NEOS_PY_STR)
                {
                    error("TypeError", std::string{ "tzinfo.tzname() must return None or a string, not '" } + type_name(result.get()) + "'");
                    return false;
                }
                aResult = as<str_object>(result.get())->value;
                return true;
            }

            std::string format_offset(td aOffset, char const* aSeparator)
            {
                char sign = '+';
                if (aOffset.days < 0)
                {
                    sign = '-';
                    aOffset = delta_of(-small_us(aOffset));
                }
                auto const seconds = aOffset.seconds % 60;
                auto const minutes = aOffset.seconds / 60 % 60;
                auto const hours = aOffset.seconds / 3600;
                std::string text = std::string(1u, sign) + pad(hours, 2) + aSeparator + pad(minutes, 2);
                if (aOffset.us != 0)
                    text += aSeparator + pad(seconds, 2) + "." + pad(aOffset.us, 6);
                else if (seconds != 0)
                    text += aSeparator + pad(seconds, 2);
                return text;
            }

            std::string timezone_name(timezone_object const& aZone)
            {
                if (aZone.name)
                    return *aZone.name;
                if (aZone.offset.zero())
                    return "UTC";
                return "UTC" + format_offset(aZone.offset, ":");
            }

            // --- local time

            bool local_tm(long long aTime, std::tm& aResult)
            {
                auto const t = static_cast<std::time_t>(aTime);
#if defined(_WIN32)
                return ::localtime_s(&aResult, &t) == 0;
#else
                return ::localtime_r(&t, &aResult) != nullptr;
#endif
            }

            long long gmtoff_of(std::tm const& aTm, long long aTime)
            {
#if defined(_WIN32)
                (void)aTm;
                std::tm utcTm{};
                std::time_t const t = static_cast<std::time_t>(aTime);
                ::gmtime_s(&utcTm, &t);
                utcTm.tm_isdst = -1;
                return static_cast<long long>(t - std::mktime(&utcTm));
#else
                (void)aTime;
                return aTm.tm_gmtoff;
#endif
            }

            std::string zone_of(std::tm const& aTm)
            {
                char buffer[128];
                auto const n = std::strftime(buffer, sizeof(buffer), "%Z", &aTm);
                return std::string{ buffer, n };
            }

            long long utc_to_seconds(long long y, int m, int d, long long hh, long long mm, long long ss) { return ((ymd_to_ord(y, m, d) * 24 + hh) * 60 + mm) * 60 + ss; }

            bool local_seconds(long long u, long long& aResult)
            {
                std::tm tm{};
                if (!local_tm(u - sEpoch, tm))
                {
                    error("OverflowError", "timestamp out of range for platform localtime()");
                    return false;
                }
                aResult = utc_to_seconds(tm.tm_year + 1900LL, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
                return true;
            }

            // (local time as seconds since 0001-01-01 (the inverse of local_seconds))
            bool local_to_seconds(fields const& f, long long& aResult)
            {
                auto const t = utc_to_seconds(f.year, f.month, f.day, f.hour, f.minute, f.second);
                long long lt = 0, t1 = 0, t2 = 0, b = 0;
                if (!local_seconds(t, lt))
                    return false;
                auto const a = lt - t;
                auto const u1 = t - a;
                if (!local_seconds(u1, t1))
                    return false;
                if (t1 == t)
                {
                    auto const u2 = f.fold != 0 ? u1 + sMaxFoldSeconds : u1 - sMaxFoldSeconds;
                    if (!local_seconds(u2, lt))
                        return false;
                    b = lt - u2;
                    if (a == b)
                    {
                        aResult = u1;
                        return true;
                    }
                }
                else
                    b = t1 - u1;
                auto const u2 = t - b;
                if (!local_seconds(u2, t2))
                    return false;
                if (t2 == t)
                    aResult = u2;
                else if (t1 == t)
                    aResult = u1;
                else
                    aResult = f.fold != 0 ? std::min(u1, u2) : std::max(u1, u2);
                return true;
            }

            // (the local timezone at a time (seconds since the epoch))
            object* local_timezone(long long aTimestamp)
            {
                std::tm tm{};
                if (!local_tm(aTimestamp, tm))
                    return error("OverflowError", "timestamp out of range for platform localtime()");
                auto const offset = delta_of(gmtoff_of(tm, aTimestamp) * 1000000);
                return make_timezone(offset, zone_of(tm));
            }

            // --- arithmetic of fields

            // (fields plus a timedelta (aSign -1: minus); false: OverflowError)
            bool add_delta(fields const& aFields, td const& aDelta, int aSign, fields& aResult)
            {
                auto us = static_cast<long long>(aFields.us) + aSign * aDelta.us;
                auto seconds = aFields.seconds() + aSign * aDelta.seconds;
                auto days = aFields.ordinal() + aSign * aDelta.days;
                seconds += floor_div(us, 1000000);
                us = floor_mod(us, 1000000);
                days += floor_div(seconds, 86400);
                seconds = floor_mod(seconds, 86400);
                if (days < 1 || days > sMaxOrdinal)
                {
                    error("OverflowError", "date value out of range");
                    return false;
                }
                aResult = aFields;
                ord_to_ymd(days, aResult.year, aResult.month, aResult.day);
                aResult.hour = static_cast<int>(seconds / 3600);
                aResult.minute = static_cast<int>(seconds / 60 % 60);
                aResult.second = static_cast<int>(seconds % 60);
                aResult.us = static_cast<int>(us);
                aResult.fold = 0;
                return true;
            }

            long long total_us(fields const& f) { return (f.ordinal() * 86400 + f.seconds()) * 1000000 + f.us; }

            // --- formatting

            std::string iso_date(fields const& f) { return pad(f.year, 4) + "-" + pad(f.month, 2) + "-" + pad(f.day, 2); }

            bool iso_time(fields const& f, std::string const& aTimespec, std::string& aResult)
            {
                auto const hh = pad(f.hour, 2), mm = pad(f.minute, 2), ss = pad(f.second, 2);
                if (aTimespec == "auto")
                    aResult = hh + ":" + mm + ":" + ss + (f.us != 0 ? "." + pad(f.us, 6) : "");
                else if (aTimespec == "hours")
                    aResult = hh;
                else if (aTimespec == "minutes")
                    aResult = hh + ":" + mm;
                else if (aTimespec == "seconds")
                    aResult = hh + ":" + mm + ":" + ss;
                else if (aTimespec == "milliseconds")
                    aResult = hh + ":" + mm + ":" + ss + "." + pad(f.us / 1000, 3);
                else if (aTimespec == "microseconds")
                    aResult = hh + ":" + mm + ":" + ss + "." + pad(f.us, 6);
                else
                {
                    error("ValueError", "Unknown timespec value");
                    return false;
                }
                return true;
            }

            std::string ctime_of(fields const& f)
            {
                return std::string{ sDayNames[weekday_of(f.year, f.month, f.day)] }.substr(0u, 3u) + " " + std::string{ sMonthNames[f.month - 1] }.substr(0u, 3u) + " " +
                    pad(f.day, 2, ' ') + " " + pad(f.hour, 2) + ":" + pad(f.minute, 2) + ":" + pad(f.second, 2) + " " + pad(f.year, 4);
            }

            // (strftime: glibc's directives in the C locale, and Python's %f, %z, %:z and %Z; aSelf: the object (its utcoffset and tzname (null: none)))
            bool strftime_of(fields const& f, object* aTz, object* aTzArgument, std::string const& aFormat, std::string& aResult)
            {
                std::optional<std::string> z, colonZ, zoneName;
                bool zDone = false, zoneDone = false;
                auto const offsets = [&]() -> bool
                {
                    if (zDone)
                        return true;
                    zDone = true;
                    td offset;
                    auto const r = tz_offset(aTz, "utcoffset", aTzArgument, offset);
                    if (r < 0)
                        return false;
                    if (r == 1)
                    {
                        z = format_offset(offset, "");
                        colonZ = format_offset(offset, ":");
                    }
                    return true;
                };
                auto const names = [&]() -> bool
                {
                    if (zoneDone)
                        return true;
                    zoneDone = true;
                    return tz_name(aTz, aTzArgument, zoneName);
                };
                auto const weekday = weekday_of(f.year, f.month, f.day);   // (Monday 0)
                auto const yday = static_cast<int>(days_before_month(f.year, f.month) + f.day);  // (1-based)
                long long isoYear = 0, isoWeek = 0, isoDay = 0;
                iso_calendar(f.year, f.month, f.day, isoYear, isoWeek, isoDay);
                auto const hour12 = f.hour % 12 == 0 ? 12 : f.hour % 12;
                std::string result;
                for (std::size_t i = 0u; i < aFormat.size(); ++i)
                {
                    char const c = aFormat[i];
                    if (c != '%' || i + 1u >= aFormat.size())
                    {
                        result += c;
                        continue;
                    }
                    auto const start = i;
                    char flag = 0;
                    if (std::strchr("-_0^#", aFormat[i + 1u]) != nullptr && i + 2u < aFormat.size())
                        flag = aFormat[++i];
                    char d = aFormat[++i];
                    bool colon = false;
                    if (d == ':' && i + 1u < aFormat.size() && aFormat[i + 1u] == 'z')
                    {
                        colon = true;
                        d = aFormat[++i];
                    }
                    auto const number = [&](long long aValue, int aWidth, char aFill = '0')
                    {
                        if (flag == '-')
                            result += std::to_string(aValue);
                        else
                            result += pad(aValue, aWidth, flag == '_' ? ' ' : flag == '0' ? '0' : aFill);
                    };
                    auto const text = [&](std::string aText)
                    {
                        if (flag == '^')
                            for (auto& ch : aText)
                                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                        else if (flag == '#')
                        {
                            bool const allUpper = std::all_of(aText.begin(), aText.end(), [](char ch) { return !std::islower(static_cast<unsigned char>(ch)); });
                            for (auto& ch : aText)
                                ch = static_cast<char>(allUpper ? std::tolower(static_cast<unsigned char>(ch)) : std::toupper(static_cast<unsigned char>(ch)));
                        }
                        result += aText;
                    };
                    auto const year = [&](long long aYear) { if (flag == 0 || flag == '-') result += std::to_string(aYear); else number(aYear, 4); };
                    switch (d)
                    {
                    case 'a': text(std::string{ sDayNames[weekday] }.substr(0u, 3u)); break;
                    case 'A': text(sDayNames[weekday]); break;
                    case 'b': case 'h': text(std::string{ sMonthNames[f.month - 1] }.substr(0u, 3u)); break;
                    case 'B': text(sMonthNames[f.month - 1]); break;
                    case 'c':
                        result += std::string{ sDayNames[weekday] }.substr(0u, 3u) + " " + std::string{ sMonthNames[f.month - 1] }.substr(0u, 3u) + " " + pad(f.day, 2, ' ') + " " +
                            pad(f.hour, 2) + ":" + pad(f.minute, 2) + ":" + pad(f.second, 2) + " " + std::to_string(f.year);
                        break;
                    case 'C': if (flag == 0) result += f.year >= 1000 ? pad(f.year / 100, 2) : std::to_string(f.year / 100); else number(f.year / 100, 2); break;
                    case 'd': number(f.day, 2); break;
                    case 'D': result += pad(f.month, 2) + "/" + pad(f.day, 2) + "/" + pad(f.year % 100, 2); break;
                    case 'e': number(f.day, 2, ' '); break;
                    case 'f': result += pad(f.us, 6); break;
                    case 'F': result += std::to_string(f.year) + "-" + pad(f.month, 2) + "-" + pad(f.day, 2); break;
                    case 'g': number(isoYear % 100, 2); break;
                    case 'G': year(isoYear); break;
                    case 'H': number(f.hour, 2); break;
                    case 'I': number(hour12, 2); break;
                    case 'j': number(yday, 3); break;
                    case 'k': number(f.hour, 2, ' '); break;
                    case 'l': number(hour12, 2, ' '); break;
                    case 'm': number(f.month, 2); break;
                    case 'M': number(f.minute, 2); break;
                    case 'n': result += '\n'; break;
                    case 'p': text(f.hour < 12 ? "AM" : "PM"); break;
                    case 'P': result += f.hour < 12 ? "am" : "pm"; break;
                    case 'r': result += pad(hour12, 2) + ":" + pad(f.minute, 2) + ":" + pad(f.second, 2) + " " + (f.hour < 12 ? "AM" : "PM"); break;
                    case 'R': result += pad(f.hour, 2) + ":" + pad(f.minute, 2); break;
                    case 's':
                        {
                            long long seconds = 0;
                            if (!local_to_seconds(f, seconds))
                                return false;
                            result += std::to_string(seconds - sEpoch);
                        }
                        break;
                    case 'S': number(f.second, 2); break;
                    case 't': result += '\t'; break;
                    case 'T': result += pad(f.hour, 2) + ":" + pad(f.minute, 2) + ":" + pad(f.second, 2); break;
                    case 'u': number(weekday + 1, 1); break;
                    case 'U': number((yday - 1 + 7 - (weekday + 1) % 7) / 7, 2); break;
                    case 'V': number(isoWeek, 2); break;
                    case 'w': number((weekday + 1) % 7, 1); break;
                    case 'W': number((yday - 1 + 7 - weekday) / 7, 2); break;
                    case 'x': result += pad(f.month, 2) + "/" + pad(f.day, 2) + "/" + pad(f.year % 100, 2); break;
                    case 'X': result += pad(f.hour, 2) + ":" + pad(f.minute, 2) + ":" + pad(f.second, 2); break;
                    case 'y': number(f.year % 100, 2); break;
                    case 'Y': year(f.year); break;
                    case 'z':
                        if (!offsets())
                            return false;
                        if (colon ? colonZ.has_value() : z.has_value())
                            result += colon ? *colonZ : *z;
                        break;
                    case 'Z':
                        if (!names())
                            return false;
                        if (zoneName)
                            result += *zoneName;
                        break;
                    case '%': result += '%'; break;
                    default:
                        result += aFormat.substr(start, i + 1u - start);
                        break;
                    }
                }
                aResult = std::move(result);
                return true;
            }

            // --- parsing (fromisoformat)

            bool digits_at(std::string const& s, std::size_t aPosition, std::size_t aCount, int& aResult)
            {
                if (aPosition + aCount > s.size())
                    return false;
                int value = 0;
                for (std::size_t i = 0u; i < aCount; ++i)
                {
                    auto const ch = s[aPosition + i];
                    if (ch < '0' || ch > '9')
                        return false;
                    value = value * 10 + (ch - '0');
                }
                aResult = value;
                return true;
            }

            // (a date of an ISO format string: its length (0: invalid); false: an exception (out of range))
            bool parse_iso_date(std::string const& s, std::size_t& aLength, int& aYear, int& aMonth, int& aDay)
            {
                aLength = 0u;
                int year = 0;
                if (!digits_at(s, 0u, 4u, year))
                    return true;
                bool const extended = s.size() > 4u && s[4] == '-';
                std::size_t p = extended ? 5u : 4u;
                if (p < s.size() && s[p] == 'W')
                {
                    // (an ISO week date: YYYY-Www[-D] or YYYYWww[D])
                    int week = 0, day = 1;
                    if (!digits_at(s, p + 1u, 2u, week))
                        return true;
                    p += 3u;
                    if (p < s.size() && (extended ? s[p] == '-' : std::isdigit(static_cast<unsigned char>(s[p])) != 0))
                    {
                        if (extended)
                            ++p;
                        if (!digits_at(s, p, 1u, day))
                            return true;
                        ++p;
                    }
                    if (year < sMinYear || year > sMaxYear)
                        return true;
                    bool weekValid = week > 0 && week < 53;
                    if (week == 53)
                    {
                        auto const firstWeekday = ymd_to_ord(year, 1, 1) % 7;
                        weekValid = firstWeekday == 4 || (firstWeekday == 3 && is_leap(year));
                    }
                    if (!weekValid)
                    {
                        error("ValueError", "Invalid week: " + std::to_string(week));
                        return false;
                    }
                    if (day < 1 || day > 7)
                    {
                        error("ValueError", "Invalid weekday: " + std::to_string(day) + " (range is [1, 7])");
                        return false;
                    }
                    auto const ordinal = iso_week1_monday(year) + (week - 1) * 7 + day - 1;
                    ord_to_ymd(ordinal, aYear, aMonth, aDay);
                    aLength = p;
                    return true;
                }
                int month = 0, day = 0;
                if (!digits_at(s, p, 2u, month))
                    return true;
                p += 2u;
                if (extended)
                {
                    if (p >= s.size() || s[p] != '-')
                        return true;
                    ++p;
                }
                if (!digits_at(s, p, 2u, day))
                    return true;
                p += 2u;
                aYear = year;
                aMonth = month;
                aDay = day;
                aLength = p;
                return true;
            }

            // (HH[:MM[:SS[.fff[fff]]]] (or without separators): -1 invalid; else 0)
            int parse_hh_mm_ss_ff(std::string const& s, std::size_t aStart, std::size_t aEnd, int* aValues, int& aMicrosecond)
            {
                std::size_t p = aStart;
                aValues[0] = aValues[1] = aValues[2] = 0;
                aMicrosecond = 0;
                bool hasSeparator = false;
                for (std::size_t i = 0u; i < 3u; ++i)
                {
                    if (!digits_at(s, p, 2u, aValues[i]) || p + 2u > aEnd)
                        return -1;
                    p += 2u;
                    if (p >= aEnd)
                        return 0;
                    char const c = s[p++];
                    if (i == 0u && c == ':')
                        hasSeparator = true;
                    if (hasSeparator && c == ':')
                    {
                        if (i == 2u)
                            return -1;
                        continue;
                    }
                    if (c == '.' || c == ',')
                    {
                        if (i < 2u)
                            return -1;
                        if (p >= aEnd)
                            return -1;
                        break;
                    }
                    if (!hasSeparator)
                    {
                        --p;
                        if (i == 2u)
                            return -1;
                        continue;
                    }
                    return -1;
                }
                auto const remaining = aEnd - p;
                auto const count = std::min<std::size_t>(remaining, 6u);
                int us = 0;
                if (!digits_at(s, p, count, us))
                    return -1;
                static int const sCorrection[] = { 100000, 10000, 1000, 100, 10, 1 };
                aMicrosecond = us * sCorrection[count - 1u];
                p += count;
                while (p < aEnd && std::isdigit(static_cast<unsigned char>(s[p])))
                    ++p;
                return p == aEnd ? 0 : -1;
            }

            // (a time of an ISO format string (with its timezone): false: invalid or an exception (failed()))
            bool parse_iso_time(std::string const& s, std::size_t aStart, fields& aFields, ref& aTz)
            {
                auto tzPosition = s.find_first_of("+-Z", aStart);
                auto const timeEnd = tzPosition == std::string::npos ? s.size() : tzPosition;
                if (timeEnd == aStart)
                    return false;
                int values[3] = {};
                int us = 0;
                if (parse_hh_mm_ss_ff(s, aStart, timeEnd, values, us) < 0)
                    return false;
                aFields.hour = values[0];
                aFields.minute = values[1];
                aFields.second = values[2];
                aFields.us = us;
                if (tzPosition == std::string::npos)
                    return true;
                if (s[tzPosition] == 'Z')
                {
                    if (tzPosition + 1u != s.size())
                        return false;
                    aTz = ref{ utc() };
                    return true;
                }
                int offsetValues[3] = {};
                int offsetUs = 0;
                if (tzPosition + 1u >= s.size() || parse_hh_mm_ss_ff(s, tzPosition + 1u, s.size(), offsetValues, offsetUs) < 0)
                    return false;
                auto total = ((offsetValues[0] * 3600LL + offsetValues[1] * 60LL + offsetValues[2]) * 1000000) + offsetUs;
                if (s[tzPosition] == '-')
                    total = -total;
                auto const offset = delta_of(total);
                if (!check_offset(offset))
                    return false;
                aTz = ref{ make_timezone(offset, std::nullopt) };
                return true;
            }

            object* invalid_iso(object* aText)
            {
                return error("ValueError", "Invalid isoformat string: " + repr_text(aText));
            }

            // --- strptime (Lib/_strptime.py in the C locale)

            struct strptime_result
            {
                fields f;
                std::optional<long long> gmtoff;    // (seconds)
                long long gmtoffFraction = 0;       // (microseconds)
                std::optional<std::string> tzname;
            };

            std::string lower(std::string aText)
            {
                for (auto& c : aText)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return aText;
            }

            bool strptime_of(std::string const& aText, std::string const& aFormat, strptime_result& aResult)
            {
                std::optional<long long> year, isoYear, isoWeek, weekday, julian;
                long long month = 1, day = 1, hour = 0, minute = 0, second = 0, fraction = 0, weekOfYear = -1, weekOfYearStart = -1;
                int ampm = -1;      // (0 am, 1 pm)
                bool twelveHour = false;
                std::optional<std::string> tzname;
                std::optional<long long> gmtoff;
                long long gmtoffFraction = 0;
                // (the format's directives: %c, %x, %X expanded)
                std::string format;
                for (std::size_t i = 0u; i < aFormat.size(); ++i)
                {
                    if (aFormat[i] == '%' && i + 1u < aFormat.size())
                    {
                        auto const d = aFormat[i + 1u];
                        if (d == 'c') { format += "%a %b %d %H:%M:%S %Y"; ++i; continue; }
                        if (d == 'x') { format += "%m/%d/%y"; ++i; continue; }
                        if (d == 'X' || d == 'T') { format += "%H:%M:%S"; ++i; continue; }
                        if (d == 'R') { format += "%H:%M"; ++i; continue; }
                        format += aFormat[i];
                        format += aFormat[++i];
                        continue;
                    }
                    format += aFormat[i];
                }
                for (std::size_t i = 0u; i < format.size(); ++i)
                    if (format[i] == '%')
                    {
                        if (i + 1u >= format.size())
                        {
                            error("ValueError", "stray % in format '" + aFormat + "'");
                            return false;
                        }
                        auto const d = format[++i];
                        if (std::strchr("aAbBdefGHIjklmMpSUuVwWyYzZh%", d) == nullptr)
                        {
                            error("ValueError", std::string{ "'" } + d + "' is a bad directive in format '" + aFormat + "'");
                            return false;
                        }
                    }
                std::size_t p = 0u;
                auto const mismatch = [&]() -> bool
                {
                    ref const text{ make_str(aText) };
                    ref const formatText{ make_str(aFormat) };
                    error("ValueError", "time data " + repr_text(text.get()) + " does not match format " + repr_text(formatText.get()));
                    return false;
                };
                // (a number: alternatives (width, minimum, maximum) tried in order (as the regular expressions' alternatives))
                struct alternative { std::size_t width; long long minimum; long long maximum; };
                auto const number = [&](std::initializer_list<alternative> aAlternatives, long long& aValue, bool aSpacePadded = false) -> bool
                {
                    for (auto const& a : aAlternatives)
                    {
                        int value = 0;
                        if (digits_at(aText, p, a.width, value) && value >= a.minimum && value <= a.maximum)
                        {
                            p += a.width;
                            aValue = value;
                            return true;
                        }
                    }
                    if (aSpacePadded && p + 1u < aText.size() && aText[p] == ' ' && aText[p + 1u] >= '1' && aText[p + 1u] <= '9')
                    {
                        aValue = aText[p + 1u] - '0';
                        p += 2u;
                        return true;
                    }
                    return false;
                };
                auto const name = [&](std::vector<std::string> const& aNames, long long& aIndex) -> bool
                {
                    // (the longest name that matches)
                    std::size_t best = 0u;
                    for (std::size_t i = 0u; i < aNames.size(); ++i)
                        if (aNames[i].size() > best && lower(aText.substr(p, aNames[i].size())) == aNames[i])
                        {
                            best = aNames[i].size();
                            aIndex = static_cast<long long>(i);
                        }
                    p += best;
                    return best != 0u;
                };
                std::vector<std::string> fullDays, shortDays, fullMonths, shortMonths;
                for (auto n : sDayNames)
                {
                    fullDays.push_back(lower(n));
                    shortDays.push_back(lower(n).substr(0u, 3u));
                }
                for (auto n : sMonthNames)
                {
                    fullMonths.push_back(lower(n));
                    shortMonths.push_back(lower(n).substr(0u, 3u));
                }
                for (std::size_t i = 0u; i < format.size(); ++i)
                {
                    char const c = format[i];
                    if (c != '%')
                    {
                        if (std::isspace(static_cast<unsigned char>(c)))
                        {
                            while (i + 1u < format.size() && std::isspace(static_cast<unsigned char>(format[i + 1u])))
                                ++i;
                            if (p >= aText.size() || !std::isspace(static_cast<unsigned char>(aText[p])))
                                return mismatch();
                            while (p < aText.size() && std::isspace(static_cast<unsigned char>(aText[p])))
                                ++p;
                            continue;
                        }
                        if (p >= aText.size() || std::tolower(static_cast<unsigned char>(aText[p])) != std::tolower(static_cast<unsigned char>(c)))
                            return mismatch();
                        ++p;
                        continue;
                    }
                    char const d = format[++i];
                    long long v = 0;
                    bool ok = true;
                    switch (d)
                    {
                    case 'Y': ok = number({ { 4u, 0, 9999 } }, v); year = v; break;
                    case 'y': ok = number({ { 2u, 0, 99 } }, v); year = v <= 68 ? v + 2000 : v + 1900; break;
                    case 'G': ok = number({ { 4u, 0, 9999 } }, v); isoYear = v; break;
                    case 'm': ok = number({ { 2u, 1, 12 }, { 1u, 1, 9 } }, month); break;
                    case 'd': case 'e': ok = number({ { 2u, 1, 31 }, { 1u, 1, 9 } }, day, true); break;
                    case 'H': case 'k': ok = number({ { 2u, 0, 23 }, { 1u, 0, 9 } }, hour, d == 'k'); break;
                    case 'I': case 'l': ok = number({ { 2u, 1, 12 }, { 1u, 1, 9 } }, hour, d == 'l'); twelveHour = true; break;
                    case 'M': ok = number({ { 2u, 0, 59 }, { 1u, 0, 9 } }, minute); break;
                    case 'S': ok = number({ { 2u, 0, 61 }, { 1u, 0, 9 } }, second); break;
                    case 'f':
                        {
                            std::size_t n = 0u;
                            std::string digits;
                            while (n < 6u && p < aText.size() && std::isdigit(static_cast<unsigned char>(aText[p])))
                            {
                                digits += aText[p++];
                                ++n;
                            }
                            ok = n > 0u;
                            if (ok)
                                fraction = std::stoll(digits + std::string(6u - n, '0'));
                        }
                        break;
                    case 'j': ok = number({ { 3u, 1, 366 }, { 2u, 1, 99 }, { 1u, 1, 9 } }, v); julian = v; break;
                    case 'U': case 'W': ok = number({ { 2u, 0, 53 }, { 1u, 0, 9 } }, weekOfYear); weekOfYearStart = d == 'U' ? 6 : 0; break;
                    case 'V': ok = number({ { 2u, 1, 53 }, { 1u, 0, 9 } }, v); isoWeek = v; break;
                    case 'w': ok = number({ { 1u, 0, 6 } }, v); weekday = v == 0 ? 6 : v - 1; break;
                    case 'u': ok = number({ { 1u, 1, 7 } }, v); weekday = v - 1; break;
                    case 'a': ok = name(shortDays, v); weekday = v; break;
                    case 'A': ok = name(fullDays, v); weekday = v; break;
                    case 'b': case 'h': ok = name(shortMonths, v); month = v + 1; break;
                    case 'B': ok = name(fullMonths, v); month = v + 1; break;
                    case 'p': ok = name({ "am", "pm" }, v); ampm = static_cast<int>(v); break;
                    case '%': ok = p < aText.size() && aText[p] == '%'; ++p; break;
                    case 'z':
                        {
                            if (p < aText.size() && aText[p] == 'Z')
                            {
                                ++p;
                                gmtoff = 0;
                                break;
                            }
                            // ([+-]HH[:]MM[[:]SS[.ffffff]])
                            auto const start = p;
                            if (p >= aText.size() || (aText[p] != '+' && aText[p] != '-'))
                            {
                                ok = false;
                                break;
                            }
                            ++p;
                            int hh = 0, mm = 0, ss = 0;
                            if (!digits_at(aText, p, 2u, hh)) { ok = false; break; }
                            p += 2u;
                            bool const colonUsed = p < aText.size() && aText[p] == ':';
                            if (colonUsed)
                                ++p;
                            if (!digits_at(aText, p, 2u, mm) || mm > 59) { ok = false; break; }
                            p += 2u;
                            std::string fractionDigits;
                            auto const beforeSeconds = p;
                            bool secondColon = p < aText.size() && aText[p] == ':';
                            if (secondColon)
                                ++p;
                            if (digits_at(aText, p, 2u, ss) && ss <= 59)
                            {
                                p += 2u;
                                if (p + 1u < aText.size() && aText[p] == '.' && std::isdigit(static_cast<unsigned char>(aText[p + 1u])))
                                {
                                    ++p;
                                    while (fractionDigits.size() < 6u && p < aText.size() && std::isdigit(static_cast<unsigned char>(aText[p])))
                                        fractionDigits += aText[p++];
                                }
                                if (colonUsed != secondColon)
                                {
                                    error("ValueError", "Inconsistent use of : in " + aText.substr(start, p - start));
                                    return false;
                                }
                            }
                            else
                            {
                                p = beforeSeconds;
                                ss = 0;
                            }
                            long long offset = hh * 3600LL + mm * 60LL + ss;
                            long long offsetFraction = fractionDigits.empty() ? 0 : std::stoll(fractionDigits + std::string(6u - fractionDigits.size(), '0'));
                            if (aText[start] == '-')
                            {
                                offset = -offset;
                                offsetFraction = -offsetFraction;
                            }
                            gmtoff = offset;
                            gmtoffFraction = offsetFraction;
                        }
                        break;
                    case 'Z':
                        {
                            // (UTC, GMT and the local timezone's names)
                            std::vector<std::string> names{ "utc", "gmt" };
                            std::tm january{}, july{};
                            if (local_tm(1704067200ll, january))
                                names.push_back(lower(zone_of(january)));
                            if (local_tm(1719792000ll, july))
                                names.push_back(lower(zone_of(july)));
                            auto const start = p;
                            ok = name(names, v);
                            if (ok)
                                tzname = aText.substr(start, p - start);
                        }
                        break;
                    }
                    if (!ok)
                        return mismatch();
                }
                if (p != aText.size())
                {
                    error("ValueError", "unconverted data remains: " + aText.substr(p));
                    return false;
                }
                if (twelveHour && ampm >= 0)
                {
                    if (ampm == 0 && hour == 12)
                        hour = 0;
                    else if (ampm == 1 && hour != 12)
                        hour += 12;
                }
                if (isoYear)
                {
                    if (julian)
                    {
                        error("ValueError", "Day of the year directive '%j' is not compatible with ISO year directive '%G'. Use '%Y' instead.");
                        return false;
                    }
                    if (!isoWeek || !weekday)
                    {
                        error("ValueError", "ISO year directive '%G' must be used with the ISO week directive '%V' and a weekday directive ('%A', '%a', '%w', or '%u').");
                        return false;
                    }
                }
                else if (isoWeek)
                {
                    if (!year || !weekday)
                        error("ValueError", "ISO week directive '%V' must be used with the ISO year directive '%G' and a weekday directive ('%A', '%a', '%w', or '%u').");
                    else
                        error("ValueError", "ISO week directive '%V' is incompatible with the year directive '%Y'. Use the ISO year '%G' instead.");
                    return false;
                }
                bool leapYearFix = false;
                if (!year && month == 2 && day == 29)
                {
                    year = 1904;
                    leapYearFix = true;
                }
                else if (!year)
                    year = 1900;
                if (!julian && weekday)
                {
                    if (weekOfYear != -1)
                    {
                        auto firstWeekday = static_cast<long long>(weekday_of(*year, 1, 1));
                        auto dayOfWeek = *weekday;
                        if (weekOfYearStart != 0)
                        {
                            firstWeekday = (firstWeekday + 1) % 7;
                            dayOfWeek = (dayOfWeek + 1) % 7;
                        }
                        auto const week0Length = (7 - firstWeekday) % 7;
                        if (weekOfYear == 0)
                            julian = 1 + dayOfWeek - firstWeekday;
                        else
                            julian = 1 + week0Length + 7 * (weekOfYear - 1) + dayOfWeek;
                    }
                    else if (isoYear && isoWeek)
                    {
                        auto const ordinal = iso_week1_monday(*isoYear) + (*isoWeek - 1) * 7 + *weekday;
                        if (ordinal < 1 || ordinal > sMaxOrdinal)
                        {
                            error("ValueError", "year is out of range");
                            return false;
                        }
                        int y = 0, m = 0, dd = 0;
                        ord_to_ymd(ordinal, y, m, dd);
                        year = y;
                        month = m;
                        day = dd;
                    }
                    if (julian && *julian <= 0)
                    {
                        year = *year - 1;
                        julian = *julian + (is_leap(*year) ? 366 : 365);
                    }
                }
                if (julian)
                {
                    if (!check_date(*year, 1, 1))
                        return false;
                    auto const ordinal = *julian - 1 + ymd_to_ord(*year, 1, 1);
                    if (ordinal < 1 || ordinal > sMaxOrdinal)
                    {
                        error("OverflowError", "date value out of range");
                        return false;
                    }
                    int y = 0, m = 0, dd = 0;
                    ord_to_ymd(ordinal, y, m, dd);
                    year = y;
                    month = m;
                    day = dd;
                }
                else if (!check_date(*year, month, day))
                    return false;
                if (leapYearFix)
                    year = 1900;
                aResult.f.year = static_cast<int>(*year);
                aResult.f.month = static_cast<int>(month);
                aResult.f.day = static_cast<int>(day);
                aResult.f.hour = static_cast<int>(hour);
                aResult.f.minute = static_cast<int>(minute);
                aResult.f.second = static_cast<int>(second);
                aResult.f.us = static_cast<int>(fraction);
                aResult.gmtoff = gmtoff;
                aResult.gmtoffFraction = gmtoffFraction;
                aResult.tzname = tzname;
                return true;
            }

            // --- methods

            using method_function = object* (*)(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs);
            struct method_entry { char const* name; method_function function; };

            object* call_method_entry(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& args = as<tuple_object>(aArgs)->items;
                std::vector<object*> const rest{ args.begin() + 1, args.end() };
                return static_cast<method_entry const*>(aData)->function(args[0], rest, aKwargs);
            }

            template <std::size_t N>
            object* find_method(method_entry const (&aMethods)[N], object* aSelf, char const* aName)
            {
                for (auto const& m : aMethods)
                    if (std::strcmp(m.name, aName) == 0)
                        return make_function(m.name, call_method_entry, const_cast<method_entry*>(&m), aSelf);
                return nullptr;
            }

            bool no_arguments(char const* aName, std::vector<object*> const& aArgs, object* aKwargs)
            {
                if (!no_keywords(aKwargs, aName))
                    return false;
                if (!aArgs.empty())
                {
                    error("TypeError", std::string{ aName } + "() takes no arguments (" + std::to_string(aArgs.size()) + " given)");
                    return false;
                }
                return true;
            }

            object* struct_time_of(fields const& f, int aDst)
            {
                auto const& type = structseq_type("time.struct_time", { "tm_year", "tm_mon", "tm_mday", "tm_hour", "tm_min", "tm_sec", "tm_wday", "tm_yday", "tm_isdst",
                    "tm_zone", "tm_gmtoff" }, 9u);
                return make_structseq(type, { make_int(f.year), make_int(f.month), make_int(f.day), make_int(f.hour), make_int(f.minute), make_int(f.second),
                    make_int(weekday_of(f.year, f.month, f.day)), make_int(days_before_month(f.year, f.month) + f.day), make_int(aDst), borrowed(none()), borrowed(none()) });
            }

            object* iso_calendar_of(fields const& f)
            {
                long long y = 0, w = 0, d = 0;
                iso_calendar(f.year, f.month, f.day, y, w, d);
                auto const& type = structseq_type("datetime.IsoCalendarDate", { "year", "week", "weekday" }, 3u);
                return make_structseq(type, { make_int(y), make_int(w), make_int(d) });
            }

            object* strftime_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, char const* aName)
            {
                std::vector<object*> a;
                if (!parse_arguments(aName, aArgs, aKwargs, { "format" }, 1u, 0u, a))
                    return nullptr;
                if (kind_of(a[0]) != NEOS_PY_STR)
                    return error("TypeError", std::string{ aName } + "() argument 1 must be str, not " + type_name(a[0]));
                fields f = fields_of(aSelf);
                object* tz = nullptr;
                object* tzArgument = nullptr;
                if (is_date(aSelf))
                    f.hour = f.minute = f.second = f.us = 0;
                else if (is_time(aSelf))
                {
                    f.year = 1900;
                    f.month = 1;
                    f.day = 1;
                    tz = tz_of(aSelf);
                }
                else
                {
                    tz = tz_of(aSelf);
                    tzArgument = aSelf;
                }
                std::string text;
                if (!strftime_of(f, tz, tzArgument, as<str_object>(a[0])->value, text))
                    return nullptr;
                return make_str(std::move(text));
            }

            object* format_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                if (!no_keywords(aKwargs, "__format__"))
                    return nullptr;
                if (aArgs.size() != 1u)
                    return error("TypeError", "__format__() takes exactly one argument (" + std::to_string(aArgs.size()) + " given)");
                if (kind_of(aArgs[0]) != NEOS_PY_STR)
                    return error("TypeError", std::string{ "__format__() argument 1 must be str, not " } + type_name(aArgs[0]));
                if (as<str_object>(aArgs[0])->value.empty())
                {
                    std::string text;
                    return str_of(aSelf, text) ? make_str(std::move(text)) : nullptr;
                }
                return strftime_method(aSelf, aArgs, nullptr, "strftime");
            }

            object* tz_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, char const* aName)
            {
                if (!no_arguments(aName, aArgs, aKwargs))
                    return nullptr;
                auto const tz = tz_of(aSelf);
                object* const argument = is_datetime(aSelf) ? aSelf : nullptr;
                if (std::strcmp(aName, "tzname") == 0)
                {
                    std::optional<std::string> name;
                    if (!tz_name(tz, argument, name))
                        return nullptr;
                    return name ? make_str(*name) : borrowed(none());
                }
                td offset;
                auto const r = tz_offset(tz, aName, argument, offset);
                if (r < 0)
                    return nullptr;
                return r == 1 ? make_timedelta(offset) : borrowed(none());
            }

            // (replace's arguments: the fields (and tzinfo (null: unchanged)))
            bool replace_fields(char const* aName, std::vector<object*> const& aArgs, object* aKwargs, std::vector<char const*> const& aNames, std::size_t aFirst,
                fields& aFields, object*& aTz)
            {
                std::vector<object*> a;
                auto const positional = aNames.size() - (aNames.back() == std::string_view{ "fold" } ? 1u : 0u);
                if (aArgs.size() > positional)
                {
                    error("TypeError", std::string{ aName } + "() takes at most " + std::to_string(positional) + " arguments (" + std::to_string(aArgs.size()) + " given)");
                    return false;
                }
                if (!parse_arguments(aName, aArgs, aKwargs, aNames, 0u, 0u, a))
                    return false;
                int* const targets[] = { &aFields.year, &aFields.month, &aFields.day, &aFields.hour, &aFields.minute, &aFields.second, &aFields.us };
                for (std::size_t i = 0u; i < aNames.size(); ++i)
                {
                    std::string_view const n{ aNames[i] };
                    if (n == "tzinfo")
                    {
                        if (a[i] != nullptr)
                        {
                            if (!check_tzinfo(a[i]))
                                return false;
                            aTz = a[i];
                        }
                    }
                    else if (n == "fold")
                    {
                        if (!int_arg(a[i], aFields.fold, aFields.fold))
                            return false;
                    }
                    else if (!int_arg(a[i], *targets[aFirst + i], *targets[aFirst + i]))
                        return false;
                }
                return true;
            }

            // --- timedelta

            std::string timedelta_repr(td const& d)
            {
                std::string args;
                if (d.days != 0)
                    args = "days=" + std::to_string(d.days);
                if (d.seconds != 0)
                    args += (args.empty() ? "" : ", ") + std::string{ "seconds=" } + std::to_string(d.seconds);
                if (d.us != 0)
                    args += (args.empty() ? "" : ", ") + std::string{ "microseconds=" } + std::to_string(d.us);
                return "datetime.timedelta(" + (args.empty() ? std::string{ "0" } : args) + ")";
            }

            std::string timedelta_str(td const& d)
            {
                std::string text = std::to_string(d.seconds / 3600) + ":" + pad(d.seconds / 60 % 60, 2) + ":" + pad(d.seconds % 60, 2);
                if (d.days != 0)
                    text = std::to_string(d.days) + " day" + (d.days == 1 || d.days == -1 ? "" : "s") + ", " + text;
                if (d.us != 0)
                    text += "." + pad(d.us, 6);
                return text;
            }

            // (a / b rounded half to even)
            B divide_nearest(B const& a, B const& b)
            {
                B q, r;
                big_divmod(a, b, q, r);
                auto const r2 = r + r;
                bool const greater = big_negative(b) ? r2 < b : b < r2;
                B q2, rem2;
                big_divmod(q, B{ 2 }, q2, rem2);
                if (greater || (r2 == b && !big_zero(rem2)))
                    q = q + B{ 1 };
                return q;
            }

            object* timedelta_from_us(B const& aMicroseconds)
            {
                td result;
                if (!delta_of(aMicroseconds, result))
                    return nullptr;
                return make_timedelta(result);
            }

            bool float_ratio(object* aFloat, B& aNumerator, B& aDenominator)
            {
                ref const ratio{ call_method_named(aFloat, "as_integer_ratio", {}) };
                if (!ratio)
                    return false;
                auto const& items = as<tuple_object>(ratio.get())->items;
                aNumerator = big_value(items[0]);
                aDenominator = big_value(items[1]);
                return true;
            }

            object* int_true_divide(B const& a, B const& b)
            {
                ref const lhs{ make_int(a) };
                ref const rhs{ make_int(b) };
                return binary(NEOS_PY_TRUE_DIVIDE, lhs.get(), rhs.get(), false);
            }

            object* timedelta_binary(int aOperation, object* aLhs, object* aRhs, int aInplace)
            {
                bool const l = is_timedelta(aLhs);
                bool const r = is_timedelta(aRhs);
                auto const isInt = [](object* o) { return kind_of(o) == NEOS_PY_INT || kind_of(o) == NEOS_PY_BOOL; };
                auto const zeroDivision = []() { return error("ZeroDivisionError", "integer division or modulo by zero"); };
                switch (aOperation)
                {
                case NEOS_PY_ADD:
                case NEOS_PY_SUBTRACT:
                    if (l && r)
                        return timedelta_from_us(aOperation == NEOS_PY_ADD ? us_of(delta(aLhs)) + us_of(delta(aRhs)) : us_of(delta(aLhs)) - us_of(delta(aRhs)));
                    if (is_date_like(aLhs) || is_date_like(aRhs))
                        return borrowed(not_implemented());
                    break;
                case NEOS_PY_MULTIPLY:
                    {
                        auto const other = l ? aRhs : aLhs;
                        auto const self = l ? aLhs : aRhs;
                        if (isInt(other))
                            return timedelta_from_us(us_of(delta(self)) * big_value(other));
                        if (kind_of(other) == NEOS_PY_FLOAT)
                        {
                            B n, d;
                            if (!float_ratio(other, n, d))
                                return nullptr;
                            return timedelta_from_us(divide_nearest(us_of(delta(self)) * n, d));
                        }
                    }
                    break;
                case NEOS_PY_TRUE_DIVIDE:
                    if (!l)
                        break;
                    if (r)
                    {
                        if (delta(aRhs).zero())
                            return error("ZeroDivisionError", "division by zero");
                        return int_true_divide(us_of(delta(aLhs)), us_of(delta(aRhs)));
                    }
                    if (isInt(aRhs))
                    {
                        auto const divisor = big_value(aRhs);
                        if (big_zero(divisor))
                            return zeroDivision();
                        return timedelta_from_us(divide_nearest(us_of(delta(aLhs)), divisor));
                    }
                    if (kind_of(aRhs) == NEOS_PY_FLOAT)
                    {
                        B n, d;
                        if (!float_ratio(aRhs, n, d))
                            return nullptr;
                        if (big_zero(n))
                            return zeroDivision();
                        return timedelta_from_us(divide_nearest(us_of(delta(aLhs)) * d, n));
                    }
                    break;
                case NEOS_PY_FLOOR_DIVIDE:
                case NEOS_PY_REMAINDER:
                    if (!l)
                        break;
                    if (r)
                    {
                        if (delta(aRhs).zero())
                            return zeroDivision();
                        B q, rem;
                        big_divmod(us_of(delta(aLhs)), us_of(delta(aRhs)), q, rem);
                        return aOperation == NEOS_PY_FLOOR_DIVIDE ? make_int(q) : timedelta_from_us(rem);
                    }
                    if (isInt(aRhs) && aOperation == NEOS_PY_FLOOR_DIVIDE)
                    {
                        auto const divisor = big_value(aRhs);
                        if (big_zero(divisor))
                            return zeroDivision();
                        B q, rem;
                        big_divmod(us_of(delta(aLhs)), divisor, q, rem);
                        return timedelta_from_us(q);
                    }
                    break;
                default:
                    break;
                }
                // (str * timedelta: a sequence's repetition)
                if (aOperation == NEOS_PY_MULTIPLY && (kind_of(l ? aRhs : aLhs) == NEOS_PY_STR || kind_of(l ? aRhs : aLhs) == NEOS_PY_LIST ||
                    kind_of(l ? aRhs : aLhs) == NEOS_PY_TUPLE))
                    return error("TypeError", "can't multiply sequence by non-int of type 'datetime.timedelta'");
                return unsupported(aOperation, aLhs, aRhs, aInplace);
            }

            object* timedelta_hash_tuple(td const& d) { return make_tuple({ make_int(d.days), make_int(d.seconds), make_int(d.us) }); }

            // (pickling (CPython's): a date's, time's or datetime's state (its fields' bytes; a fold (protocols beyond 3): its hour's or month's
            // high bit), and (type, (state[, tzinfo])))
            object* temporal_reduce(object* aSelf, long long aProto)
            {
                auto const& f = fields_of(aSelf);
                std::string state;
                auto const push = [&](int aByte) { state.push_back(static_cast<char>(aByte & 0xff)); };
                bool const fold = aProto > 3 && f.fold != 0;
                if (!is_time(aSelf))
                {
                    push(f.year >> 8);
                    push(f.year);
                    push(f.month | (fold && is_datetime(aSelf) ? 128 : 0));
                    push(f.day);
                }
                if (!is_date(aSelf))
                {
                    push(f.hour | (fold && is_time(aSelf) ? 128 : 0));
                    push(f.minute);
                    push(f.second);
                    push(f.us >> 16);
                    push(f.us >> 8);
                    push(f.us);
                }
                ref const cls{ class_of(aSelf) };
                auto const tz = tz_of(aSelf);
                return make_tuple({ borrowed(cls.get()), tz != nullptr && !is_date(aSelf) ? make_tuple({ make_bytes(std::move(state)), borrowed(tz) }) :
                    make_tuple({ make_bytes(std::move(state)) }) });
            }
            object* temporal_reduce_ex(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                if ((aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty()) || aArgs.size() != 1u)
                    return error("TypeError", "__reduce_ex__() takes exactly one argument (" + std::to_string(aArgs.size()) + " given)");
                long long proto = 0;
                if (!to_index(aArgs[0], proto))
                    return nullptr;
                return temporal_reduce(aSelf, proto);
            }
            // (__reduce_ex__: __reduce__'s)
            object* reduce_ex_of_reduce(object* aSelf, std::vector<object*> const&, object*)
            {
                ref const reduce{ getattr(aSelf, "__reduce__") };
                return reduce ? invoke(reduce.get(), {}) : nullptr;
            }

            method_entry const sTimedeltaMethods[] =
            {
                { "total_seconds", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_arguments("total_seconds", aArgs, aKwargs))
                            return nullptr;
                        return int_true_divide(us_of(delta(aSelf)), B{ 1000000 });
                    } },
                { "__reduce__", [](object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        auto const& d = delta(aSelf);
                        return make_tuple({ type_object_for("timedelta", "datetime.timedelta"), make_tuple({ make_int(d.days), make_int(d.seconds), make_int(d.us) }) });
                    } },
                { "__reduce_ex__", reduce_ex_of_reduce },
                { "__copy__", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return borrowed(aSelf); } },
                { "__deepcopy__", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return borrowed(aSelf); } }
            };

            object* class_attribute(std::string const& aClass, char const* aName);

            neos_py_type make_timedelta_type()
            {
                auto type = make_type("datetime.timedelta", "timedelta", retain_object, release_object<timedelta_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object* { return make_str(timedelta_repr(delta(aSelf))); };
                type.str = [](neos_py_context*, object* aSelf) -> object* { return make_str(timedelta_str(delta(aSelf))); };
                type.binary = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs, int aInplace) -> object* { return timedelta_binary(aOperation, aLhs, aRhs, aInplace); };
                type.unary = [](neos_py_context*, int aOperation, object* aSelf) -> object*
                {
                    auto const& d = delta(aSelf);
                    switch (aOperation)
                    {
                    case NEOS_PY_NEGATIVE: return timedelta_from_us(-us_of(d));
                    case NEOS_PY_POSITIVE: return borrowed(aSelf);
                    case NEOS_PY_ABSOLUTE: return d.days < 0 ? timedelta_from_us(-us_of(d)) : borrowed(aSelf);
                    default: return error("TypeError", "bad operand type for unary ~: 'datetime.timedelta'");
                    }
                };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    if (!is_timedelta(aLhs) || !is_timedelta(aRhs))
                        return not_comparable(aOperation, aLhs, aRhs);
                    return compare_result(aOperation, compare_delta(delta(aLhs), delta(aRhs)));
                };
                type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int
                {
                    ref const t{ timedelta_hash_tuple(delta(aSelf)) };
                    return hash_of(t.get(), *aResult) ? 0 : -1;
                };
                type.truth = [](neos_py_context*, object* aSelf) -> int { return delta(aSelf).zero() ? 0 : 1; };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& d = delta(aSelf);
                    if (std::strcmp(aName, "days") == 0) return make_int(d.days);
                    if (std::strcmp(aName, "seconds") == 0) return make_int(d.seconds);
                    if (std::strcmp(aName, "microseconds") == 0) return make_int(d.us);
                    if (auto const m = find_method(sTimedeltaMethods, aSelf, aName))
                        return m;
                    if (auto const a = class_attribute("datetime.timedelta", aName))
                        return a;
                    return error("AttributeError", std::string{ "'datetime.timedelta' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            // --- date

            object* date_add(object* aDate, td const& aDelta, int aSign)
            {
                auto const& f = fields_of(aDate);
                auto const days = f.ordinal() + aSign * aDelta.days;
                if (days < 1 || days > sMaxOrdinal)
                    return error("OverflowError", "date value out of range");
                int y = 0, m = 0, d = 0;
                ord_to_ymd(days, y, m, d);
                return make_date(y, m, d);
            }

            int compare_ymd(fields const& a, fields const& b)
            {
                if (a.year != b.year) return a.year < b.year ? -1 : 1;
                if (a.month != b.month) return a.month < b.month ? -1 : 1;
                if (a.day != b.day) return a.day < b.day ? -1 : 1;
                return 0;
            }

            int compare_hms(fields const& a, fields const& b)
            {
                long long const x[] = { a.hour, a.minute, a.second, a.us };
                long long const y[] = { b.hour, b.minute, b.second, b.us };
                for (int i = 0; i < 4; ++i)
                    if (x[i] != y[i])
                        return x[i] < y[i] ? -1 : 1;
                return 0;
            }

            object* date_replace(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                auto f = fields_of(aSelf);
                object* tz = nullptr;
                if (!replace_fields("replace", aArgs, aKwargs, { "year", "month", "day" }, 0u, f, tz) || !check_date(f.year, f.month, f.day))
                    return nullptr;
                return make_date(f.year, f.month, f.day);
            }

            method_entry const sDateMethods[] =
            {
                { "isoformat", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_keywords(aKwargs, "date.isoformat"))
                            return nullptr;
                        if (!aArgs.empty())
                            return error("TypeError", "date.isoformat() takes no arguments (" + std::to_string(aArgs.size()) + " given)");
                        return make_str(iso_date(fields_of(aSelf)));
                    } },
                { "ctime", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return no_arguments("ctime", aArgs, aKwargs) ? make_str(ctime_of(fields_of(aSelf))) : nullptr; } },
                { "strftime", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object* { return strftime_method(aSelf, aArgs, aKwargs, "strftime"); } },
                { "__format__", format_method },
                { "timetuple", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_arguments("timetuple", aArgs, aKwargs))
                            return nullptr;
                        auto f = fields_of(aSelf);
                        f.hour = f.minute = f.second = 0;
                        return struct_time_of(f, -1);
                    } },
                { "toordinal", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return no_arguments("toordinal", aArgs, aKwargs) ? make_int(fields_of(aSelf).ordinal()) : nullptr; } },
                { "weekday", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { auto const& f = fields_of(aSelf); return no_arguments("weekday", aArgs, aKwargs) ? make_int(weekday_of(f.year, f.month, f.day)) : nullptr; } },
                { "isoweekday", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { auto const& f = fields_of(aSelf); return no_arguments("isoweekday", aArgs, aKwargs) ? make_int(weekday_of(f.year, f.month, f.day) + 1) : nullptr; } },
                { "isocalendar", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return no_arguments("isocalendar", aArgs, aKwargs) ? iso_calendar_of(fields_of(aSelf)) : nullptr; } },
                { "replace", date_replace },
                { "__replace__", date_replace },
                { "__reduce__", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return temporal_reduce(aSelf, 2); } },
                { "__reduce_ex__", reduce_ex_of_reduce },
                { "__copy__", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return borrowed(aSelf); } },
                { "__deepcopy__", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return borrowed(aSelf); } }
            };

            object* date_getattr(object* aSelf, char const* aName)
            {
                auto const& f = fields_of(aSelf);
                if (std::strcmp(aName, "year") == 0) return make_int(f.year);
                if (std::strcmp(aName, "month") == 0) return make_int(f.month);
                if (std::strcmp(aName, "day") == 0) return make_int(f.day);
                return find_method(sDateMethods, aSelf, aName);
            }

            neos_py_type make_date_type()
            {
                auto type = make_type("datetime.date", "date", retain_object, release_object<temporal_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& f = fields_of(aSelf);
                    return make_str("datetime.date(" + std::to_string(f.year) + ", " + std::to_string(f.month) + ", " + std::to_string(f.day) + ")");
                };
                type.str = [](neos_py_context*, object* aSelf) -> object* { return make_str(iso_date(fields_of(aSelf))); };
                type.binary = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs, int aInplace) -> object*
                {
                    if (aOperation == NEOS_PY_ADD)
                    {
                        if (is_date(aLhs) && is_timedelta(aRhs))
                            return date_add(aLhs, delta(aRhs), 1);
                        if (is_timedelta(aLhs) && is_date(aRhs))
                            return date_add(aRhs, delta(aLhs), 1);
                    }
                    else if (aOperation == NEOS_PY_SUBTRACT && is_date(aLhs))
                    {
                        if (is_timedelta(aRhs))
                            return date_add(aLhs, delta(aRhs), -1);
                        if (is_date(aRhs))
                            return make_timedelta(td{ fields_of(aLhs).ordinal() - fields_of(aRhs).ordinal(), 0, 0 });
                    }
                    if (is_datetime(aLhs) || is_datetime(aRhs))
                    {
                        // (datetime's)
                        if ((aOperation == NEOS_PY_ADD && (is_timedelta(aLhs) || is_timedelta(aRhs))) || (aOperation == NEOS_PY_SUBTRACT && is_datetime(aLhs)))
                            return borrowed(not_implemented());
                    }
                    return unsupported(aOperation, aLhs, aRhs, aInplace);
                };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    if (!is_date(aLhs) || !is_date(aRhs))
                        return not_comparable(aOperation, aLhs, aRhs);
                    return compare_result(aOperation, compare_ymd(fields_of(aLhs), fields_of(aRhs)));
                };
                type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int
                {
                    auto const& f = fields_of(aSelf);
                    ref const t{ make_tuple({ make_int(f.year), make_int(f.month), make_int(f.day) }) };
                    return hash_of(t.get(), *aResult) ? 0 : -1;
                };
                type.truth = [](neos_py_context*, object*) -> int { return 1; };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    if (auto const a = date_getattr(aSelf, aName))
                        return a;
                    if (auto const a = class_attribute("datetime.date", aName))
                        return a;
                    return error("AttributeError", std::string{ "'datetime.date' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            // --- time

            std::string time_repr_args(fields const& f)
            {
                std::string text = std::to_string(f.hour) + ", " + std::to_string(f.minute);
                if (f.us != 0)
                    text += ", " + std::to_string(f.second) + ", " + std::to_string(f.us);
                else if (f.second != 0)
                    text += ", " + std::to_string(f.second);
                return text;
            }

            bool repr_tail(object* aTz, int aFold, std::string& aText)
            {
                if (aTz != nullptr)
                {
                    std::string tz;
                    if (!repr_of(aTz, tz))
                        return false;
                    aText += ", tzinfo=" + tz;
                }
                if (aFold != 0)
                    aText += ", fold=1";
                return true;
            }

            object* time_isoformat(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("isoformat", aArgs, aKwargs, { "timespec" }, 0u, 0u, a))
                    return nullptr;
                std::string timespec = "auto";
                if (a[0] != nullptr)
                {
                    if (kind_of(a[0]) != NEOS_PY_STR)
                        return error("TypeError", std::string{ "isoformat() argument 1 must be str, not " } + type_name(a[0]));
                    timespec = as<str_object>(a[0])->value;
                }
                std::string text;
                if (!iso_time(fields_of(aSelf), timespec, text))
                    return nullptr;
                td offset;
                auto const r = tz_offset(tz_of(aSelf), "utcoffset", nullptr, offset);
                if (r < 0)
                    return nullptr;
                if (r == 1)
                    text += format_offset(offset, ":");
                return make_str(std::move(text));
            }

            object* time_replace(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                auto f = fields_of(aSelf);
                object* tz = tz_of(aSelf);
                if (!replace_fields("replace", aArgs, aKwargs, { "hour", "minute", "second", "microsecond", "tzinfo", "fold" }, 3u, f, tz) ||
                    !check_time(f.hour, f.minute, f.second, f.us, f.fold))
                    return nullptr;
                return make_time(f, tz);
            }

            method_entry const sTimeMethods[] =
            {
                { "isoformat", time_isoformat },
                { "strftime", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object* { return strftime_method(aSelf, aArgs, aKwargs, "strftime"); } },
                { "__format__", format_method },
                { "utcoffset", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object* { return tz_method(aSelf, aArgs, aKwargs, "utcoffset"); } },
                { "dst", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object* { return tz_method(aSelf, aArgs, aKwargs, "dst"); } },
                { "tzname", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object* { return tz_method(aSelf, aArgs, aKwargs, "tzname"); } },
                { "replace", time_replace },
                { "__replace__", time_replace },
                { "__reduce__", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return temporal_reduce(aSelf, 2); } },
                { "__reduce_ex__", temporal_reduce_ex },
                { "__copy__", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return borrowed(aSelf); } },
                { "__deepcopy__", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return borrowed(aSelf); } }
            };

            neos_py_type make_time_type()
            {
                auto type = make_type("datetime.time", "time", retain_object, release_object<temporal_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& f = fields_of(aSelf);
                    std::string text = "datetime.time(" + time_repr_args(f);
                    if (!repr_tail(tz_of(aSelf), f.fold, text))
                        return nullptr;
                    return make_str(text + ")");
                };
                type.str = [](neos_py_context*, object* aSelf) -> object* { return time_isoformat(aSelf, {}, nullptr); };
                type.binary = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs, int aInplace) -> object* { return unsupported(aOperation, aLhs, aRhs, aInplace); };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    if (!is_time(aLhs) || !is_time(aRhs))
                        return not_comparable(aOperation, aLhs, aRhs);
                    auto const& a = fields_of(aLhs);
                    auto const& b = fields_of(aRhs);
                    if (tz_of(aLhs) == tz_of(aRhs))
                        return compare_result(aOperation, compare_hms(a, b));
                    td o1, o2;
                    auto const r1 = tz_offset(tz_of(aLhs), "utcoffset", nullptr, o1);
                    if (r1 < 0)
                        return nullptr;
                    auto const r2 = tz_offset(tz_of(aRhs), "utcoffset", nullptr, o2);
                    if (r2 < 0)
                        return nullptr;
                    if (r1 == r2 && (r1 == 0 || compare_delta(o1, o2) == 0))
                        return compare_result(aOperation, compare_hms(a, b));
                    if (r1 == 1 && r2 == 1)
                    {
                        auto const s1 = a.seconds() - o1.days * 86400 - o1.seconds;
                        auto const s2 = b.seconds() - o2.days * 86400 - o2.seconds;
                        auto const difference = s1 != s2 ? (s1 < s2 ? -1 : 1) : (a.us != b.us ? (a.us < b.us ? -1 : 1) : 0);
                        return compare_result(aOperation, difference);
                    }
                    if (aOperation == NEOS_PY_EQ)
                        return make_bool(false);
                    if (aOperation == NEOS_PY_NE)
                        return make_bool(true);
                    return error("TypeError", "can't compare offset-naive and offset-aware times");
                };
                type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int
                {
                    auto const& f = fields_of(aSelf);
                    td offset;
                    auto const r = tz_offset(tz_of(aSelf), "utcoffset", nullptr, offset);
                    if (r < 0)
                        return -1;
                    ref t;
                    if (r == 0)
                        t = ref{ make_tuple({ make_int(f.hour), make_int(f.minute), make_int(f.second), make_int(f.us) }) };
                    else
                        t = ref{ timedelta_hash_tuple(delta_of((f.seconds() * 1000000 + f.us) - small_us(offset))) };
                    return hash_of(t.get(), *aResult) ? 0 : -1;
                };
                type.truth = [](neos_py_context*, object*) -> int { return 1; };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& f = fields_of(aSelf);
                    std::string_view const name{ aName };
                    if (name == "hour") return make_int(f.hour);
                    if (name == "minute") return make_int(f.minute);
                    if (name == "second") return make_int(f.second);
                    if (name == "microsecond") return make_int(f.us);
                    if (name == "fold") return make_int(f.fold);
                    if (name == "tzinfo") return borrowed(tz_of(aSelf) != nullptr ? tz_of(aSelf) : none());
                    if (auto const m = find_method(sTimeMethods, aSelf, aName))
                        return m;
                    if (auto const a = class_attribute("datetime.time", aName))
                        return a;
                    return error("AttributeError", std::string{ "'datetime.time' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            // --- datetime

            object* datetime_add(object* aSelf, td const& aDelta, int aSign)
            {
                fields result;
                if (!add_delta(fields_of(aSelf), aDelta, aSign, result))
                    return nullptr;
                return make_datetime(result, tz_of(aSelf));
            }

            // (dt - other as microseconds (with their offsets); false: an exception)
            bool datetime_difference(object* aLhs, object* aRhs, long long& aResult)
            {
                auto difference = total_us(fields_of(aLhs)) - total_us(fields_of(aRhs));
                if (tz_of(aLhs) != tz_of(aRhs))
                {
                    td o1, o2;
                    auto const r1 = tz_offset(tz_of(aLhs), "utcoffset", aLhs, o1);
                    if (r1 < 0)
                        return false;
                    auto const r2 = tz_offset(tz_of(aRhs), "utcoffset", aRhs, o2);
                    if (r2 < 0)
                        return false;
                    if (r1 != r2)
                    {
                        error("TypeError", "can't subtract offset-naive and offset-aware datetimes");
                        return false;
                    }
                    if (r1 == 1)
                        difference = difference - small_us(o1) + small_us(o2);
                }
                aResult = difference;
                return true;
            }

            object* datetime_isoformat(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, std::string aSeparator)
            {
                std::vector<object*> a;
                if (!parse_arguments("isoformat", aArgs, aKwargs, { "sep", "timespec" }, 0u, 0u, a))
                    return nullptr;
                if (a[0] != nullptr)
                {
                    if (kind_of(a[0]) != NEOS_PY_STR || to_code_points(as<str_object>(a[0])->value).size() != 1u)
                        return error("TypeError", std::string{ "isoformat() argument 1 must be a unicode character, not " } + type_name(a[0]));
                    aSeparator = as<str_object>(a[0])->value;
                }
                std::string timespec = "auto";
                if (a[1] != nullptr)
                {
                    if (kind_of(a[1]) != NEOS_PY_STR)
                        return error("TypeError", std::string{ "isoformat() argument 2 must be str, not " } + type_name(a[1]));
                    timespec = as<str_object>(a[1])->value;
                }
                auto const& f = fields_of(aSelf);
                std::string time;
                if (!iso_time(f, timespec, time))
                    return nullptr;
                td offset;
                auto const r = tz_offset(tz_of(aSelf), "utcoffset", aSelf, offset);
                if (r < 0)
                    return nullptr;
                return make_str(iso_date(f) + aSeparator + time + (r == 1 ? format_offset(offset, ":") : std::string{}));
            }

            object* datetime_replace(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                auto f = fields_of(aSelf);
                object* tz = tz_of(aSelf);
                if (!replace_fields("replace", aArgs, aKwargs, { "year", "month", "day", "hour", "minute", "second", "microsecond", "tzinfo", "fold" }, 0u, f, tz) ||
                    !check_date(f.year, f.month, f.day) || !check_time(f.hour, f.minute, f.second, f.us, f.fold))
                    return nullptr;
                return make_datetime(f, tz);
            }

            // (timezone.fromutc and tzinfo.fromutc's results: dt (UTC, with tzinfo tz) as local time)
            object* from_utc(object* aTz, object* aDatetime)
            {
                if (is_timezone(aTz))
                    return datetime_add(aDatetime, zone(aTz).offset, 1);
                return call_method_named(aTz, "fromutc", { borrowed(aDatetime) });
            }

            object* timestamp_of(object* aSelf)
            {
                auto const& f = fields_of(aSelf);
                td offset;
                auto const r = tz_offset(tz_of(aSelf), "utcoffset", aSelf, offset);
                if (r < 0)
                    return nullptr;
                if (r == 1)
                {
                    auto const us = total_us(f) - small_us(offset) - sEpoch * 1000000;
                    return int_true_divide(B{ us }, B{ 1000000 });
                }
                long long seconds = 0;
                if (!local_to_seconds(f, seconds))
                    return nullptr;
                return make_float(static_cast<double>(seconds - sEpoch) + f.us / 1e6);
            }

            object* astimezone(object* aSelf, object* aTz)
            {
                if (aTz != nullptr && kind_of(aTz) == NEOS_PY_NONE)
                    aTz = nullptr;
                if (!check_tzinfo(aTz))
                    return nullptr;
                auto const selfTz = tz_of(aSelf);
                if (aTz != nullptr && selfTz == aTz)
                    return borrowed(aSelf);
                td offset;
                auto r = tz_offset(selfTz, "utcoffset", aSelf, offset);
                if (r < 0)
                    return nullptr;
                if (r == 0)
                {
                    // (naive: local time)
                    long long seconds = 0;
                    if (!local_to_seconds(fields_of(aSelf), seconds))
                        return nullptr;
                    ref const local{ local_timezone(seconds - sEpoch) };
                    if (!local)
                        return nullptr;
                    offset = zone(local.get()).offset;
                }
                fields utcFields;
                if (!add_delta(fields_of(aSelf), offset, -1, utcFields))
                    return nullptr;
                ref tz = ref::borrowed(aTz);
                if (aTz == nullptr)
                {
                    auto const seconds = utcFields.ordinal() * 86400 + utcFields.seconds() - sEpoch;
                    tz = ref{ local_timezone(seconds) };
                    if (!tz)
                        return nullptr;
                }
                ref const utcDatetime{ make_datetime(utcFields, tz.get()) };
                return from_utc(tz.get(), utcDatetime.get());
            }

            method_entry const sDatetimeMethods[] =
            {
                { "isoformat", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object* { return datetime_isoformat(aSelf, aArgs, aKwargs, "T"); } },
                { "ctime", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return no_arguments("ctime", aArgs, aKwargs) ? make_str(ctime_of(fields_of(aSelf))) : nullptr; } },
                { "strftime", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object* { return strftime_method(aSelf, aArgs, aKwargs, "strftime"); } },
                { "__format__", format_method },
                { "date", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { auto const& f = fields_of(aSelf); return no_arguments("date", aArgs, aKwargs) ? make_date(f.year, f.month, f.day) : nullptr; } },
                { "time", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return no_arguments("time", aArgs, aKwargs) ? make_time(fields_of(aSelf), nullptr) : nullptr; } },
                { "timetz", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return no_arguments("timetz", aArgs, aKwargs) ? make_time(fields_of(aSelf), tz_of(aSelf)) : nullptr; } },
                { "utcoffset", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object* { return tz_method(aSelf, aArgs, aKwargs, "utcoffset"); } },
                { "dst", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object* { return tz_method(aSelf, aArgs, aKwargs, "dst"); } },
                { "tzname", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object* { return tz_method(aSelf, aArgs, aKwargs, "tzname"); } },
                { "timetuple", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_arguments("timetuple", aArgs, aKwargs))
                            return nullptr;
                        td dst;
                        auto const r = tz_offset(tz_of(aSelf), "dst", aSelf, dst);
                        if (r < 0)
                            return nullptr;
                        return struct_time_of(fields_of(aSelf), r == 0 ? -1 : dst.zero() ? 0 : 1);
                    } },
                { "utctimetuple", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_arguments("utctimetuple", aArgs, aKwargs))
                            return nullptr;
                        td offset;
                        auto const r = tz_offset(tz_of(aSelf), "utcoffset", aSelf, offset);
                        if (r < 0)
                            return nullptr;
                        fields f = fields_of(aSelf);
                        if (r == 1 && !add_delta(fields_of(aSelf), offset, -1, f))
                            return nullptr;
                        return struct_time_of(f, 0);
                    } },
                { "timestamp", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return no_arguments("timestamp", aArgs, aKwargs) ? timestamp_of(aSelf) : nullptr; } },
                { "astimezone", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("astimezone", aArgs, aKwargs, { "tz" }, 0u, 0u, a))
                            return nullptr;
                        return astimezone(aSelf, a[0]);
                    } },
                { "toordinal", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return no_arguments("toordinal", aArgs, aKwargs) ? make_int(fields_of(aSelf).ordinal()) : nullptr; } },
                { "weekday", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { auto const& f = fields_of(aSelf); return no_arguments("weekday", aArgs, aKwargs) ? make_int(weekday_of(f.year, f.month, f.day)) : nullptr; } },
                { "isoweekday", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { auto const& f = fields_of(aSelf); return no_arguments("isoweekday", aArgs, aKwargs) ? make_int(weekday_of(f.year, f.month, f.day) + 1) : nullptr; } },
                { "isocalendar", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return no_arguments("isocalendar", aArgs, aKwargs) ? iso_calendar_of(fields_of(aSelf)) : nullptr; } },
                { "replace", datetime_replace },
                { "__replace__", datetime_replace },
                { "__reduce__", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return temporal_reduce(aSelf, 2); } },
                { "__reduce_ex__", temporal_reduce_ex },
                { "__copy__", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return borrowed(aSelf); } },
                { "__deepcopy__", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return borrowed(aSelf); } }
            };

            neos_py_type make_datetime_type()
            {
                auto type = make_type("datetime.datetime", "datetime", retain_object, release_object<temporal_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& f = fields_of(aSelf);
                    std::string text = "datetime.datetime(" + std::to_string(f.year) + ", " + std::to_string(f.month) + ", " + std::to_string(f.day) + ", " + time_repr_args(f);
                    if (!repr_tail(tz_of(aSelf), f.fold, text))
                        return nullptr;
                    return make_str(text + ")");
                };
                type.str = [](neos_py_context*, object* aSelf) -> object* { return datetime_isoformat(aSelf, {}, nullptr, " "); };
                type.binary = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs, int aInplace) -> object*
                {
                    if (aOperation == NEOS_PY_ADD)
                    {
                        if (is_datetime(aLhs) && is_timedelta(aRhs))
                            return datetime_add(aLhs, delta(aRhs), 1);
                        if (is_timedelta(aLhs) && is_datetime(aRhs))
                            return datetime_add(aRhs, delta(aLhs), 1);
                    }
                    else if (aOperation == NEOS_PY_SUBTRACT && is_datetime(aLhs))
                    {
                        if (is_timedelta(aRhs))
                            return datetime_add(aLhs, delta(aRhs), -1);
                        if (is_datetime(aRhs))
                        {
                            long long difference = 0;
                            if (!datetime_difference(aLhs, aRhs, difference))
                                return nullptr;
                            return make_timedelta(delta_of(difference));
                        }
                    }
                    return unsupported(aOperation, aLhs, aRhs, aInplace);
                };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    if (!is_datetime(aLhs) || !is_datetime(aRhs))
                        return not_comparable(aOperation, aLhs, aRhs);
                    auto const& a = fields_of(aLhs);
                    auto const& b = fields_of(aRhs);
                    auto const fieldsDifference = [&]() { auto const d = compare_ymd(a, b); return d != 0 ? d : compare_hms(a, b); };
                    if (tz_of(aLhs) == tz_of(aRhs))
                        return compare_result(aOperation, fieldsDifference());
                    td o1, o2;
                    auto const r1 = tz_offset(tz_of(aLhs), "utcoffset", aLhs, o1);
                    if (r1 < 0)
                        return nullptr;
                    auto const r2 = tz_offset(tz_of(aRhs), "utcoffset", aRhs, o2);
                    if (r2 < 0)
                        return nullptr;
                    if (r1 == r2 && (r1 == 0 || compare_delta(o1, o2) == 0))
                        return compare_result(aOperation, fieldsDifference());
                    if (r1 == 1 && r2 == 1)
                    {
                        auto const difference = total_us(a) - small_us(o1) - (total_us(b) - small_us(o2));
                        return compare_result(aOperation, difference < 0 ? -1 : difference > 0 ? 1 : 0);
                    }
                    if (aOperation == NEOS_PY_EQ)
                        return make_bool(false);
                    if (aOperation == NEOS_PY_NE)
                        return make_bool(true);
                    return error("TypeError", "can't compare offset-naive and offset-aware datetimes");
                };
                type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int
                {
                    auto f = fields_of(aSelf);
                    f.fold = 0;
                    td offset;
                    auto const r = tz_offset(tz_of(aSelf), "utcoffset", aSelf, offset);
                    if (r < 0)
                        return -1;
                    auto us = total_us(f);
                    if (r == 1)
                        us -= small_us(offset);
                    ref const t{ timedelta_hash_tuple(delta_of(us)) };
                    return hash_of(t.get(), *aResult) ? 0 : -1;
                };
                type.truth = [](neos_py_context*, object*) -> int { return 1; };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& f = fields_of(aSelf);
                    std::string_view const name{ aName };
                    if (name == "year") return make_int(f.year);
                    if (name == "month") return make_int(f.month);
                    if (name == "day") return make_int(f.day);
                    if (name == "hour") return make_int(f.hour);
                    if (name == "minute") return make_int(f.minute);
                    if (name == "second") return make_int(f.second);
                    if (name == "microsecond") return make_int(f.us);
                    if (name == "fold") return make_int(f.fold);
                    if (name == "tzinfo") return borrowed(tz_of(aSelf) != nullptr ? tz_of(aSelf) : none());
                    if (auto const m = find_method(sDatetimeMethods, aSelf, aName))
                        return m;
                    if (auto const a = class_attribute("datetime.datetime", aName))
                        return a;
                    return error("AttributeError", std::string{ "'datetime.datetime' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            // --- timezone

            bool datetime_or_none(object* aValue, char const* aMethod)
            {
                if (kind_of(aValue) == NEOS_PY_NONE || is_datetime(aValue))
                    return true;
                error("TypeError", std::string{ aMethod } + "(dt) argument must be a datetime instance or None, not " + type_name(aValue));
                return false;
            }

            object* one_argument(char const* aName, std::vector<object*> const& aArgs, object* aKwargs)
            {
                if (!no_keywords(aKwargs, aName))
                    return nullptr;
                if (aArgs.size() != 1u)
                    return error("TypeError", std::string{ aName } + "() takes exactly one argument (" + std::to_string(aArgs.size()) + " given)");
                return aArgs[0];
            }

            method_entry const sTimezoneMethods[] =
            {
                { "utcoffset", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        auto const dt = one_argument("utcoffset", aArgs, aKwargs);
                        if (dt == nullptr || !datetime_or_none(dt, "utcoffset"))
                            return nullptr;
                        return make_timedelta(zone(aSelf).offset);
                    } },
                { "dst", [](object*, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        auto const dt = one_argument("dst", aArgs, aKwargs);
                        if (dt == nullptr || !datetime_or_none(dt, "dst"))
                            return nullptr;
                        return borrowed(none());
                    } },
                { "tzname", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        auto const dt = one_argument("tzname", aArgs, aKwargs);
                        if (dt == nullptr || !datetime_or_none(dt, "tzname"))
                            return nullptr;
                        return make_str(timezone_name(zone(aSelf)));
                    } },
                { "fromutc", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        auto const dt = one_argument("fromutc", aArgs, aKwargs);
                        if (dt == nullptr)
                            return nullptr;
                        if (!is_datetime(dt))
                            return error("TypeError", "fromutc: argument must be a datetime");
                        if (tz_of(dt) != aSelf)
                            return error("ValueError", "fromutc: dt.tzinfo is not self");
                        return datetime_add(dt, zone(aSelf).offset, 1);
                    } },
                { "__getinitargs__", [](object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        auto const& z = zone(aSelf);
                        if (z.name)
                            return make_tuple({ make_timedelta(z.offset), make_str(*z.name) });
                        return make_tuple({ make_timedelta(z.offset) });
                    } },
                { "__reduce__", [](object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        // (tzinfo's: (type, __getinitargs__(), its state))
                        ref const cls{ class_of(aSelf) };
                        auto const& z = zone(aSelf);
                        return make_tuple({ borrowed(cls.get()), z.name ? make_tuple({ make_timedelta(z.offset), make_str(*z.name) }) : make_tuple({ make_timedelta(z.offset) }),
                            borrowed(none()) });
                    } },
                { "__reduce_ex__", reduce_ex_of_reduce },
                { "__copy__", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return borrowed(aSelf); } },
                { "__deepcopy__", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return borrowed(aSelf); } }
            };

            neos_py_type make_timezone_type()
            {
                auto type = make_type("datetime.timezone", "timezone", retain_object, release_object<timezone_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& z = zone(aSelf);
                    ref const u{ utc() };
                    if (aSelf == u.get())
                        return make_str("datetime.timezone.utc");
                    std::string text = "datetime.timezone(" + timedelta_repr(z.offset);
                    if (z.name)
                    {
                        ref const name{ make_str(*z.name) };
                        text += ", " + repr_text(name.get());
                    }
                    return make_str(text + ")");
                };
                type.str = [](neos_py_context*, object* aSelf) -> object* { return make_str(timezone_name(zone(aSelf))); };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    if (!is_timezone(aLhs) || !is_timezone(aRhs) || (aOperation != NEOS_PY_EQ && aOperation != NEOS_PY_NE))
                        return borrowed(not_implemented());
                    return compare_result(aOperation, compare_delta(zone(aLhs).offset, zone(aRhs).offset));
                };
                type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int
                {
                    ref const t{ timedelta_hash_tuple(zone(aSelf).offset) };
                    return hash_of(t.get(), *aResult) ? 0 : -1;
                };
                type.truth = [](neos_py_context*, object*) -> int { return 1; };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    if (auto const m = find_method(sTimezoneMethods, aSelf, aName))
                        return m;
                    if (auto const a = class_attribute("datetime.timezone", aName))
                        return a;
                    return error("AttributeError", std::string{ "'datetime.timezone' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            // --- tzinfo (a class: its subclasses implement utcoffset, dst and tzname)

            builtin_entry const sTzinfoMethods[] =
            {
                { "utcoffset", [](std::vector<object*> const&, object*) -> object* { return error("NotImplementedError", "a tzinfo subclass must implement utcoffset()"); } },
                { "dst", [](std::vector<object*> const&, object*) -> object* { return error("NotImplementedError", "a tzinfo subclass must implement dst()"); } },
                { "tzname", [](std::vector<object*> const&, object*) -> object* { return error("NotImplementedError", "a tzinfo subclass must implement tzname()"); } },
                { "fromutc", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return error("TypeError", "fromutc() takes exactly one argument (" + std::to_string(aArgs.size() - 1u) + " given)");
                        auto const self = aArgs[0];
                        auto const dt = aArgs[1];
                        if (!is_datetime(dt))
                            return error("TypeError", "fromutc: argument must be a datetime");
                        if (tz_of(dt) != self)
                            return error("ValueError", "fromutc: dt.tzinfo is not self");
                        td offset, dst;
                        auto r = tz_offset(self, "utcoffset", dt, offset);
                        if (r < 0)
                            return nullptr;
                        if (r == 0)
                            return error("ValueError", "fromutc: non-None utcoffset() result required");
                        r = tz_offset(self, "dst", dt, dst);
                        if (r < 0)
                            return nullptr;
                        if (r == 0)
                            return error("ValueError", "fromutc: non-None dst() result required");
                        auto const difference = delta_of(small_us(offset) - small_us(dst));
                        ref result = ref::borrowed(dt);
                        if (!difference.zero())
                        {
                            result = ref{ datetime_add(dt, difference, 1) };
                            if (!result)
                                return nullptr;
                            r = tz_offset(self, "dst", result.get(), dst);
                            if (r < 0)
                                return nullptr;
                            if (r == 0)
                                return error("ValueError", "fromutc: tz.dst() gave inconsistent results; cannot convert");
                        }
                        return datetime_add(result.get(), dst, 1);
                    } }
            };

            int tzinfo_instancecheck(object* aObject) { return is_timezone(aObject) ? 1 : 0; }

            // --- constructors

            object* fields_constructor(char const* aName, std::vector<object*> const& aArgs, object* aKwargs, std::vector<char const*> const& aNames,
                std::size_t aRequired, bool aDate, bool aTime)
            {
                auto const positional = aNames.size() - (aTime ? 1u : 0u);
                if (aArgs.size() > positional)
                    return error("TypeError", std::string{ "function takes at most " } + std::to_string(positional) + " arguments (" + std::to_string(aArgs.size()) + " given)");
                std::vector<object*> a;
                if (!parse_arguments(aName, aArgs, aKwargs, aNames, aRequired, 0u, a))
                    return nullptr;
                fields f;
                std::size_t i = 0u;
                if (aDate)
                {
                    if (!int_arg(a[0], f.year, 1) || !int_arg(a[1], f.month, 1) || !int_arg(a[2], f.day, 1) || !check_date(f.year, f.month, f.day))
                        return nullptr;
                    i = 3u;
                }
                if (!aTime)
                    return make_date(f.year, f.month, f.day);
                object* tz = a[i + 4u];
                if (!int_arg(a[i], f.hour, 0) || !int_arg(a[i + 1u], f.minute, 0) || !int_arg(a[i + 2u], f.second, 0) || !int_arg(a[i + 3u], f.us, 0) ||
                    !int_arg(a[i + 5u], f.fold, 0) || !check_time(f.hour, f.minute, f.second, f.us, f.fold) || !check_tzinfo(tz))
                    return nullptr;
                return aDate ? make_datetime(f, tz) : make_time(f, tz);
            }

            // (unpickling: a date's, time's or datetime's state (bytes, or Python 2's str of Latin-1 characters) of its size (aSize), if aArgs
            // are a state (and, for a time or datetime, a tzinfo: at most aMaxArgs))
            bool pickle_state(std::vector<object*> const& aArgs, object* aKwargs, std::size_t aSize, std::size_t aMaxArgs, std::string& aState)
            {
                if ((aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty()) || aArgs.empty() || aArgs.size() > aMaxArgs)
                    return false;
                std::string_view data;
                std::string latin1;
                if (kind_of(aArgs[0]) == NEOS_PY_STR)
                {
                    for (auto c : to_code_points(as<str_object>(aArgs[0])->value))
                    {
                        if (c > 0xffu)
                            return false;
                        latin1.push_back(static_cast<char>(c));
                    }
                    data = latin1;
                }
                else if (kind_of(aArgs[0]) != NEOS_PY_OTHER || std::strcmp(type_name(aArgs[0]), "bytes") != 0 || !bytes_like(aArgs[0], data))
                    return false;
                if (data.size() != aSize)
                    return false;
                aState.assign(data);
                return true;
            }
            fields fields_of_state(std::string const& aState, bool aDate, bool aTime)
            {
                auto const b = [&](std::size_t i) { return static_cast<int>(static_cast<unsigned char>(aState[i])); };
                fields f;
                std::size_t i = 0u;
                if (aDate)
                {
                    f.year = b(0) * 256 + b(1);
                    f.month = b(2) & 0x7f;
                    f.fold = (b(2) & 0x80) != 0 ? 1 : 0;
                    f.day = b(3);
                    i = 4u;
                }
                if (aTime)
                {
                    f.hour = b(i) & 0x7f;
                    if ((b(i) & 0x80) != 0)
                        f.fold = 1;
                    f.minute = b(i + 1u);
                    f.second = b(i + 2u);
                    f.us = (b(i + 3u) << 16) | (b(i + 4u) << 8) | b(i + 5u);
                }
                return f;
            }

            object* new_date(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                if (std::string state; pickle_state(as<tuple_object>(aArgs)->items, aKwargs, 4u, 1u, state) && (state[2] & 0x7f) >= 1 && (state[2] & 0x7f) <= 12)
                {
                    auto const f = fields_of_state(state, true, false);
                    return make_date(f.year, f.month, f.day);
                }
                return fields_constructor("date", as<tuple_object>(aArgs)->items, aKwargs, { "year", "month", "day" }, 3u, true, false);
            }

            object* new_time(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                auto const& args = as<tuple_object>(aArgs)->items;
                if (std::string state; pickle_state(args, aKwargs, 6u, 2u, state) && (state[0] & 0x7f) < 24)
                {
                    object* tz = args.size() > 1u ? args[1] : nullptr;
                    if (tz != nullptr && kind_of(tz) != NEOS_PY_NONE && !is_tzinfo(tz))
                        return error("TypeError", "bad tzinfo state arg");
                    return make_time(fields_of_state(state, false, true), tz);
                }
                return fields_constructor("time", as<tuple_object>(aArgs)->items, aKwargs, { "hour", "minute", "second", "microsecond", "tzinfo", "fold" }, 0u, false, true);
            }

            object* new_datetime(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                auto const& args = as<tuple_object>(aArgs)->items;
                if (std::string state; pickle_state(args, aKwargs, 10u, 2u, state) && (state[2] & 0x7f) >= 1 && (state[2] & 0x7f) <= 12)
                {
                    object* tz = args.size() > 1u ? args[1] : nullptr;
                    if (tz != nullptr && kind_of(tz) != NEOS_PY_NONE && !is_tzinfo(tz))
                        return error("TypeError", "bad tzinfo state arg");
                    return make_datetime(fields_of_state(state, true, true), tz);
                }
                return fields_constructor("datetime", as<tuple_object>(aArgs)->items, aKwargs,
                    { "year", "month", "day", "hour", "minute", "second", "microsecond", "tzinfo", "fold" }, 3u, true, true);
            }

            // (a timedelta argument: its microseconds (sofar), and its fraction (aLeftover))
            bool accumulate(char const* aTag, object* aValue, long long aFactor, B& aSofar, double& aLeftover)
            {
                if (kind_of(aValue) == NEOS_PY_INT || kind_of(aValue) == NEOS_PY_BOOL)
                {
                    aSofar = aSofar + big_value(aValue) * B{ aFactor };
                    return true;
                }
                if (kind_of(aValue) == NEOS_PY_FLOAT)
                {
                    auto const value = as<float_object>(aValue)->value;
                    if (std::isnan(value) || std::isinf(value))
                    {
                        error(std::isnan(value) ? "ValueError" : "OverflowError", std::isnan(value) ? "cannot convert float NaN to integer" : "cannot convert float infinity to integer");
                        return false;
                    }
                    double intpart = 0.0;
                    auto fracpart = std::modf(value, &intpart);
                    if (std::fabs(intpart) > 9.2e18 / static_cast<double>(aFactor))
                    {
                        error("OverflowError", "Python int too large to convert to C int");
                        return false;
                    }
                    aSofar = aSofar + B{ static_cast<long long>(intpart) } * B{ aFactor };
                    if (fracpart == 0.0)
                        return true;
                    auto const scaled = static_cast<double>(aFactor) * fracpart;
                    fracpart = std::modf(scaled, &intpart);
                    aSofar = aSofar + B{ static_cast<long long>(intpart) };
                    aLeftover += fracpart;
                    return true;
                }
                error("TypeError", std::string{ "unsupported type for timedelta " } + aTag + " component: " + type_name(aValue));
                return false;
            }

            object* new_timedelta(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("__new__", as<tuple_object>(aArgs)->items, aKwargs, { "days", "seconds", "microseconds", "milliseconds", "minutes", "hours", "weeks" },
                    0u, 0u, a))
                    return nullptr;
                B x{ 0 };
                double leftover = 0.0;
                struct component { std::size_t index; char const* tag; long long factor; };
                static component const sComponents[] = { { 2u, "microseconds", 1 }, { 3u, "milliseconds", 1000 }, { 1u, "seconds", 1000000 }, { 4u, "minutes", 60000000 },
                    { 5u, "hours", 3600000000ll }, { 0u, "days", 86400000000ll }, { 6u, "weeks", 604800000000ll } };
                for (auto const& c : sComponents)
                    if (a[c.index] != nullptr && !accumulate(c.tag, a[c.index], c.factor, x, leftover))
                        return nullptr;
                if (leftover != 0.0)
                {
                    auto wholeUs = std::round(leftover);
                    if (std::fabs(wholeUs - leftover) == 0.5)
                    {
                        B q, r;
                        big_divmod(x, B{ 2 }, q, r);
                        auto const xIsOdd = big_zero(r) ? 0.0 : 1.0;
                        wholeUs = 2.0 * std::round((leftover + xIsOdd) * 0.5) - xIsOdd;
                    }
                    x = x + B{ static_cast<long long>(wholeUs) };
                }
                return timedelta_from_us(x);
            }

            object* new_timezone(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("timezone", as<tuple_object>(aArgs)->items, aKwargs, { "offset", "name" }, 1u, 0u, a))
                    return nullptr;
                if (!is_timedelta(a[0]))
                    return error("TypeError", std::string{ "timezone() argument 1 must be datetime.timedelta, not " } + type_name(a[0]));
                std::optional<std::string> name;
                if (a[1] != nullptr)
                {
                    if (kind_of(a[1]) != NEOS_PY_STR)
                        return error("TypeError", std::string{ "timezone() argument 2 must be str, not " } + type_name(a[1]));
                    name = as<str_object>(a[1])->value;
                }
                if (!check_offset(delta(a[0])))
                    return nullptr;
                return make_timezone(delta(a[0]), std::move(name));
            }

            int date_instancecheck(neos_py_context*, void*, object* aObject) { return is_date_like(aObject) ? 1 : 0; }
            int time_instancecheck(neos_py_context*, void*, object* aObject) { return is_time(aObject) ? 1 : 0; }
            int datetime_instancecheck(neos_py_context*, void*, object* aObject) { return is_datetime(aObject) ? 1 : 0; }
            int timedelta_instancecheck(neos_py_context*, void*, object* aObject) { return is_timedelta(aObject) ? 1 : 0; }
            int timezone_instancecheck(neos_py_context*, void*, object* aObject) { return is_timezone(aObject) ? 1 : 0; }

            // --- class methods

            // (a timestamp (seconds since the epoch): seconds and microseconds (rounded half to even))
            bool timestamp_parts(object* aValue, long long& aSeconds, int& aMicroseconds)
            {
                aMicroseconds = 0;
                if (kind_of(aValue) == NEOS_PY_INT || kind_of(aValue) == NEOS_PY_BOOL)
                {
                    auto const value = big_value(aValue);
                    if (B{ 9223372036854775807ll } < big_abs(value))
                    {
                        error("OverflowError", "timestamp out of range for platform time_t");
                        return false;
                    }
                    aSeconds = ll_of(value);
                    return true;
                }
                double value = 0.0;
                if (kind_of(aValue) != NEOS_PY_FLOAT)
                {
                    if (!to_double(aValue, value))
                        return false;
                }
                else
                    value = as<float_object>(aValue)->value;
                if (std::isnan(value))
                {
                    error("ValueError", "Invalid value NaN (not a number)");
                    return false;
                }
                double intpart = 0.0;
                auto const fracpart = std::modf(value, &intpart);
                auto us = std::nearbyint(fracpart * 1e6);
                if (us >= 1e6)
                {
                    intpart += 1.0;
                    us -= 1e6;
                }
                else if (us < 0.0)
                {
                    intpart -= 1.0;
                    us += 1e6;
                }
                if (!(intpart >= -9.2e18 && intpart <= 9.2e18))
                {
                    error("OverflowError", "timestamp out of range for platform time_t");
                    return false;
                }
                aSeconds = static_cast<long long>(intpart);
                aMicroseconds = static_cast<int>(us);
                return true;
            }

            // (fields of a timestamp: local time, or UTC)
            bool timestamp_fields(long long aSeconds, int aMicroseconds, bool aLocal, fields& aResult)
            {
                fields f;
                if (aLocal)
                {
                    std::tm tm{};
                    if (!local_tm(aSeconds, tm))
                    {
                        error("OverflowError", "timestamp out of range for platform time_t");
                        return false;
                    }
                    if (tm.tm_year + 1900LL < sMinYear || tm.tm_year + 1900LL > sMaxYear)
                    {
                        error("ValueError", "year " + std::to_string(tm.tm_year + 1900LL) + " is out of range");
                        return false;
                    }
                    f.year = tm.tm_year + 1900;
                    f.month = tm.tm_mon + 1;
                    f.day = tm.tm_mday;
                    f.hour = tm.tm_hour;
                    f.minute = tm.tm_min;
                    f.second = std::min(tm.tm_sec, 59);
                    // (a fold: the time is repeated (e.g. the end of summer time))
                    auto const resultSeconds = utc_to_seconds(f.year, f.month, f.day, f.hour, f.minute, f.second);
                    long long probe = 0;
                    if (!local_seconds(sEpoch + aSeconds - sMaxFoldSeconds, probe))
                        return false;
                    auto const transition = resultSeconds - probe - sMaxFoldSeconds;
                    if (transition < 0)
                    {
                        if (!local_seconds(sEpoch + aSeconds + transition, probe))
                            return false;
                        if (probe == resultSeconds)
                            f.fold = 1;
                    }
                }
                else
                {
                    auto const days = floor_div(aSeconds, 86400);
                    auto const seconds = floor_mod(aSeconds, 86400);
                    auto const ordinal = days + 719163;
                    if (ordinal < 1 || ordinal > sMaxOrdinal)
                    {
                        // (the year (as gmtime's))
                        auto const z = days + 719468;
                        auto const era = (z >= 0 ? z : z - 146096) / 146097;
                        auto const doe = z - era * 146097;
                        auto const yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
                        auto const doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
                        auto const mp = (5 * doy + 2) / 153;
                        auto const year = yoe + era * 400 + (mp >= 10 ? 1 : 0);
                        error("ValueError", "year " + std::to_string(year) + " is out of range");
                        return false;
                    }
                    ord_to_ymd(ordinal, f.year, f.month, f.day);
                    f.hour = static_cast<int>(seconds / 3600);
                    f.minute = static_cast<int>(seconds / 60 % 60);
                    f.second = static_cast<int>(seconds % 60);
                }
                f.us = aMicroseconds;
                aResult = f;
                return true;
            }

            object* from_timestamp(object* aTimestamp, object* aTz, bool aDatetime)
            {
                if (aTz != nullptr && kind_of(aTz) == NEOS_PY_NONE)
                    aTz = nullptr;
                if (!check_tzinfo(aTz))
                    return nullptr;
                long long seconds = 0;
                int us = 0;
                if (!timestamp_parts(aTimestamp, seconds, us))
                    return nullptr;
                fields f;
                if (!timestamp_fields(seconds, us, aTz == nullptr, f))
                    return nullptr;
                if (!aDatetime)
                    return make_date(f.year, f.month, f.day);
                if (aTz == nullptr)
                    return make_datetime(f, nullptr);
                ref const utcDatetime{ make_datetime(f, aTz) };
                return from_utc(aTz, utcDatetime.get());
            }

            object* now(object* aTz, bool aDatetime)
            {
                auto const us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                ref const timestamp{ make_float(static_cast<double>(floor_div(us, 1000000)) + static_cast<double>(floor_mod(us, 1000000)) / 1e6) };
                return from_timestamp(timestamp.get(), aTz, aDatetime);
            }

            object* from_iso_format(object* aText, int aKind)    // (0: date, 1: time, 2: datetime)
            {
                if (kind_of(aText) != NEOS_PY_STR)
                    return error("TypeError", "fromisoformat: argument must be str");
                auto const& s = as<str_object>(aText)->value;
                fields f;
                ref tz;
                if (aKind == 1)
                {
                    std::size_t start = !s.empty() && s[0] == 'T' ? 1u : 0u;
                    if (!parse_iso_time(s, start, f, tz))
                        return failed() ? nullptr : invalid_iso(aText);
                    if (!check_time(f.hour, f.minute, f.second, f.us, 0))
                        return nullptr;
                    return make_time(f, tz.get());
                }
                std::size_t length = 0u;
                int y = 0, m = 0, d = 0;
                if (!parse_iso_date(s, length, y, m, d))
                    return nullptr;
                if (length == 0u || (aKind == 0 && length != s.size()))
                    return invalid_iso(aText);
                if (aKind == 2 && length < s.size())
                {
                    // (a separator (any character), then a time)
                    auto const separator = to_code_points(s.substr(length));
                    std::string first;
                    append_code_point(first, separator[0]);
                    if (length + first.size() >= s.size() || !parse_iso_time(s, length + first.size(), f, tz))
                        return failed() ? nullptr : invalid_iso(aText);
                }
                if (!check_date(y, m, d))
                    return nullptr;
                f.year = y;
                f.month = m;
                f.day = d;
                if (aKind == 0)
                    return make_date(y, m, d);
                if (!check_time(f.hour, f.minute, f.second, f.us, 0))
                    return nullptr;
                return make_datetime(f, tz.get());
            }

            object* from_ordinal(object* aOrdinal, bool aDatetime)
            {
                long long ordinal = 0;
                if (!int_arg(aOrdinal, ordinal))
                    return nullptr;
                if (ordinal < 1)
                    return error("ValueError", "ordinal must be >= 1");
                if (ordinal > sMaxOrdinal)
                    return error("ValueError", "year 10000 is out of range");
                fields f;
                ord_to_ymd(ordinal, f.year, f.month, f.day);
                return aDatetime ? make_datetime(f, nullptr) : make_date(f.year, f.month, f.day);
            }

            object* from_iso_calendar(std::vector<object*> const& aArgs, object* aKwargs, bool aDatetime)
            {
                std::vector<object*> a;
                if (!parse_arguments("fromisocalendar", aArgs, aKwargs, { "year", "week", "day" }, 3u, 0u, a))
                    return nullptr;
                long long year = 0, week = 0, day = 0;
                if (!int_arg(a[0], year) || !int_arg(a[1], week) || !int_arg(a[2], day))
                    return nullptr;
                if (year < sMinYear || year > sMaxYear)
                    return error("ValueError", "Year is out of range: " + std::to_string(year));
                bool weekValid = week > 0 && week < 53;
                if (week == 53)
                {
                    auto const firstWeekday = ymd_to_ord(year, 1, 1) % 7;
                    weekValid = firstWeekday == 4 || (firstWeekday == 3 && is_leap(year));
                }
                if (!weekValid)
                    return error("ValueError", "Invalid week: " + std::to_string(week));
                if (day < 1 || day > 7)
                    return error("ValueError", "Invalid day: " + std::to_string(day) + " (range is [1, 7])");
                fields f;
                ord_to_ymd(iso_week1_monday(year) + (week - 1) * 7 + day - 1, f.year, f.month, f.day);
                return aDatetime ? make_datetime(f, nullptr) : make_date(f.year, f.month, f.day);
            }

            builtin_entry const sDateClassMethods[] =
            {
                { "today", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return no_arguments("today", aArgs, aKwargs) ? now(nullptr, false) : nullptr; } },
                { "fromtimestamp", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("fromtimestamp", aArgs, aKwargs, { "timestamp" }, 1u, 1u, a))
                            return nullptr;
                        return from_timestamp(a[0], nullptr, false);
                    } },
                { "fromordinal", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        auto const o = one_argument("fromordinal", aArgs, aKwargs);
                        return o != nullptr ? from_ordinal(o, false) : nullptr;
                    } },
                { "fromisoformat", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        auto const s = one_argument("fromisoformat", aArgs, aKwargs);
                        return s != nullptr ? from_iso_format(s, 0) : nullptr;
                    } },
                { "fromisocalendar", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return from_iso_calendar(aArgs, aKwargs, false); } }
            };

            builtin_entry const sTimeClassMethods[] =
            {
                { "fromisoformat", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        auto const s = one_argument("fromisoformat", aArgs, aKwargs);
                        return s != nullptr ? from_iso_format(s, 1) : nullptr;
                    } }
            };

            builtin_entry const sDatetimeClassMethods[] =
            {
                { "today", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return no_arguments("today", aArgs, aKwargs) ? now(nullptr, true) : nullptr; } },
                { "now", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("now", aArgs, aKwargs, { "tz" }, 0u, 0u, a))
                            return nullptr;
                        return now(a[0], true);
                    } },
                { "utcnow", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_arguments("utcnow", aArgs, aKwargs))
                            return nullptr;
                        ref const aware{ now(none(), true) };
                        ref const u{ utc() };
                        ref const result{ now(u.get(), true) };
                        if (!result)
                            return nullptr;
                        return make_datetime(fields_of(result.get()), nullptr);
                    } },
                { "fromtimestamp", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("fromtimestamp", aArgs, aKwargs, { "timestamp", "tz" }, 1u, 0u, a))
                            return nullptr;
                        return from_timestamp(a[0], a[1], true);
                    } },
                { "utcfromtimestamp", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        auto const t = one_argument("utcfromtimestamp", aArgs, aKwargs);
                        if (t == nullptr)
                            return nullptr;
                        long long seconds = 0;
                        int us = 0;
                        fields f;
                        if (!timestamp_parts(t, seconds, us) || !timestamp_fields(seconds, us, false, f))
                            return nullptr;
                        return make_datetime(f, nullptr);
                    } },
                { "fromordinal", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        auto const o = one_argument("fromordinal", aArgs, aKwargs);
                        return o != nullptr ? from_ordinal(o, true) : nullptr;
                    } },
                { "fromisoformat", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        auto const s = one_argument("fromisoformat", aArgs, aKwargs);
                        return s != nullptr ? from_iso_format(s, 2) : nullptr;
                    } },
                { "fromisocalendar", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return from_iso_calendar(aArgs, aKwargs, true); } },
                { "combine", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("combine", aArgs, aKwargs, { "date", "time", "tzinfo" }, 2u, 0u, a))
                            return nullptr;
                        if (!is_date_like(a[0]))
                            return error("TypeError", std::string{ "combine() argument 1 must be datetime.date, not " } + type_name(a[0]));
                        if (!is_time(a[1]))
                            return error("TypeError", std::string{ "combine() argument 2 must be datetime.time, not " } + type_name(a[1]));
                        auto f = fields_of(a[1]);
                        auto const& d = fields_of(a[0]);
                        f.year = d.year;
                        f.month = d.month;
                        f.day = d.day;
                        object* tz = a[2] != nullptr ? a[2] : tz_of(a[1]);
                        if (!check_tzinfo(tz))
                            return nullptr;
                        return make_datetime(f, tz);
                    } },
                { "strptime", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("strptime", aArgs, aKwargs, { "string", "format" }, 2u, 2u, a))
                            return nullptr;
                        for (std::size_t i = 0u; i < 2u; ++i)
                            if (kind_of(a[i]) != NEOS_PY_STR)
                                return error("TypeError", "strptime() argument " + std::to_string(i + 1u) + " must be str, not " + type_name(a[i]));
                        strptime_result result;
                        if (!strptime_of(as<str_object>(a[0])->value, as<str_object>(a[1])->value, result))
                            return nullptr;
                        if (!check_time(result.f.hour, result.f.minute, result.f.second, result.f.us, 0))
                            return nullptr;
                        ref tz;
                        if (result.gmtoff)
                        {
                            auto const offset = delta_of(*result.gmtoff * 1000000 + result.gmtoffFraction);
                            if (!check_offset(offset))
                                return nullptr;
                            tz = ref{ make_timezone(offset, result.tzname && !result.tzname->empty() ? result.tzname : std::nullopt) };
                        }
                        return make_datetime(result.f, tz.get());
                    } }
            };

            object* constant_date(int y, int m, int d) { return make_date(y, m, d); }

            // (the classes' attributes (min, max, resolution, utc) and class methods; null: none (no exception))
            object* class_attribute(std::string const& aClass, char const* aName)
            {
                std::string_view const name{ aName };
                if (aClass == "datetime.timedelta")
                {
                    if (name == "min") return make_timedelta(td{ -sMaxDeltaDays, 0, 0 });
                    if (name == "max") return make_timedelta(td{ sMaxDeltaDays, 86399, 999999 });
                    if (name == "resolution") return make_timedelta(td{ 0, 0, 1 });
                    return nullptr;
                }
                if (aClass == "datetime.timezone")
                {
                    if (name == "utc") return utc();
                    if (name == "min") return make_timezone(td{ -1, 60, 0 }, std::nullopt);
                    if (name == "max") return make_timezone(td{ 0, 86340, 0 }, std::nullopt);
                    return nullptr;
                }
                auto const find = [&](auto const& aMethods) -> object*
                {
                    for (auto const& m : aMethods)
                        if (name == m.name)
                            return make_builtin(m);
                    return nullptr;
                };
                if (aClass == "datetime.date")
                {
                    if (name == "min") return constant_date(1, 1, 1);
                    if (name == "max") return constant_date(9999, 12, 31);
                    if (name == "resolution") return make_timedelta(td{ 1, 0, 0 });
                    return find(sDateClassMethods);
                }
                if (aClass == "datetime.time")
                {
                    fields f;
                    if (name == "min") return make_time(f, nullptr);
                    f.hour = 23; f.minute = 59; f.second = 59; f.us = 999999;
                    if (name == "max") return make_time(f, nullptr);
                    if (name == "resolution") return make_timedelta(td{ 0, 0, 1 });
                    return find(sTimeClassMethods);
                }
                if (aClass == "datetime.datetime")
                {
                    fields f;
                    if (name == "min") return make_datetime(f, nullptr);
                    f.year = 9999; f.month = 12; f.day = 31; f.hour = 23; f.minute = 59; f.second = 59; f.us = 999999;
                    if (name == "max") return make_datetime(f, nullptr);
                    if (name == "resolution") return make_timedelta(td{ 0, 0, 1 });
                    return find(sDatetimeClassMethods);
                }
                return nullptr;
            }

            // --- the module

            bool datetime_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                if (sTzinfoClass == nullptr)
                {
                    std::vector<std::pair<std::string, object*>> ns;
                    for (auto const& m : sTzinfoMethods)
                        ns.emplace_back(m.name, make_builtin(m));
                    sTzinfoClass = make_class("datetime", "tzinfo", {}, ns);
                    for (auto& n : ns)
                        release(n.second);
                    if (sTzinfoClass == nullptr)
                        return false;
                    set_class_instancecheck(sTzinfoClass, tzinfo_instancecheck);
                }
                aAttributes.emplace_back("tzinfo", borrowed(sTzinfoClass));
                aAttributes.emplace_back("timedelta", context()->type_new(context(), "timedelta", "datetime.timedelta", new_timedelta, nullptr, timedelta_instancecheck));
                aAttributes.emplace_back("date", context()->type_new(context(), "date", "datetime.date", new_date, nullptr, date_instancecheck));
                aAttributes.emplace_back("time", context()->type_new(context(), "time", "datetime.time", new_time, nullptr, time_instancecheck));
                aAttributes.emplace_back("datetime", context()->type_new(context(), "datetime", "datetime.datetime", new_datetime, nullptr, datetime_instancecheck));
                aAttributes.emplace_back("timezone", context()->type_new(context(), "timezone", "datetime.timezone", new_timezone, nullptr, timezone_instancecheck));
                aAttributes.emplace_back("MINYEAR", make_int(sMinYear));
                aAttributes.emplace_back("MAXYEAR", make_int(sMaxYear));
                aAttributes.emplace_back("UTC", utc());
                return true;
            }

            inline bool const sDatetime = register_module("datetime", datetime_module);
        }

        // (the classes' attributes (e.g. datetime.max, date.today))
        object* datetime_class_function(std::string const& aClass, char const* aName)
        {
            return class_attribute(aClass, aName);
        }
    }
}
