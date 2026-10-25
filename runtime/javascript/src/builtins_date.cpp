/*
  builtins_date.cpp

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

// Date (ECMA-262 §21.4 and Annex B.2.3): time values and their day, year and month arithmetic (exact over the whole
// range of time values, with 64-bit integers: no reliance on time_t), the host's time zone (C++20 <chrono> time zones:
// the zone that the TZ environment variable names, if the library knows it, otherwise the current zone; UTC if the
// library has no time zone database), the Date constructor, Date.now/parse/UTC and Date.prototype's methods.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include "js.hpp"
#include "intl.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        constexpr double msPerSecond = 1000.0;
        constexpr double msPerMinute = 60000.0;
        constexpr double msPerHour = 3600000.0;
        constexpr double msPerDay = 86400000.0;
        constexpr double maxTimeValue = 8.64e15;

        double const nan = std::numeric_limits<double>::quiet_NaN();

        // --- time value arithmetic (§21.4.1)

        constexpr std::int64_t floor_div(std::int64_t aNumerator, std::int64_t aDenominator)
        {
            auto const q = aNumerator / aDenominator;
            return (aNumerator % aDenominator != 0 && ((aNumerator < 0) != (aDenominator < 0))) ? q - 1 : q;
        }

        constexpr std::int64_t floor_mod(std::int64_t aNumerator, std::int64_t aDenominator)
        {
            return aNumerator - floor_div(aNumerator, aDenominator) * aDenominator;
        }

        double modulo(double aNumber, double aDivisor)     ///< (the mathematical modulo: the result has the divisor's sign)
        {
            auto const r = std::fmod(aNumber, aDivisor);
            return r < 0.0 ? r + aDivisor : r + 0.0;
        }

        // (DayFromYear)
        constexpr std::int64_t day_from_year(std::int64_t aYear)
        {
            return 365 * (aYear - 1970) + floor_div(aYear - 1969, 4) - floor_div(aYear - 1901, 100) + floor_div(aYear - 1601, 400);
        }

        constexpr bool in_leap_year(std::int64_t aYear)
        {
            return floor_mod(aYear, 4) == 0 && (floor_mod(aYear, 100) != 0 || floor_mod(aYear, 400) == 0);
        }

        constexpr std::int64_t sMonthStart[2][13] =
        {
            { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
            { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
        };

        // (the largest year whose first day is not after the day)
        std::int64_t year_from_day(std::int64_t aDay)
        {
            auto year = 1970 + static_cast<std::int64_t>(std::floor(static_cast<double>(aDay) / 365.2425));
            while (day_from_year(year) > aDay)
                --year;
            while (day_from_year(year + 1) <= aDay)
                ++year;
            return year;
        }

        struct civil
        {
            std::int64_t year;
            int month;          ///< (0 to 11)
            int date;           ///< (1 to 31)
            int weekDay;        ///< (0 (Sunday) to 6)
            int hour;
            int minute;
            int second;
            int millisecond;
        };

        civil civil_from_time(double aTime)
        {
            civil result{};
            auto const t = static_cast<std::int64_t>(aTime);
            auto const day = floor_div(t, 86400000);
            auto const withinDay = floor_mod(t, 86400000);
            result.year = year_from_day(day);
            auto const dayWithinYear = day - day_from_year(result.year);
            auto const& starts = sMonthStart[in_leap_year(result.year) ? 1 : 0];
            int month = 0;
            while (month < 11 && dayWithinYear >= starts[month + 1])
                ++month;
            result.month = month;
            result.date = static_cast<int>(dayWithinYear - starts[month]) + 1;
            result.weekDay = static_cast<int>(floor_mod(day + 4, 7));
            result.hour = static_cast<int>(withinDay / 3600000);
            result.minute = static_cast<int>(withinDay / 60000 % 60);
            result.second = static_cast<int>(withinDay / 1000 % 60);
            result.millisecond = static_cast<int>(withinDay % 1000);
            return result;
        }

        double day(double aTime)
        {
            return std::floor(aTime / msPerDay);
        }

        double time_within_day(double aTime)
        {
            return modulo(aTime, msPerDay);
        }

        double to_integer(double aNumber)       ///< (ToIntegerOrInfinity of a number)
        {
            if (std::isnan(aNumber))
                return 0.0;
            return std::trunc(aNumber) + 0.0;
        }

        // (MakeTime)
        double make_time(double aHour, double aMinute, double aSecond, double aMillisecond)
        {
            if (!std::isfinite(aHour) || !std::isfinite(aMinute) || !std::isfinite(aSecond) || !std::isfinite(aMillisecond))
                return nan;
            auto const h = to_integer(aHour);
            auto const m = to_integer(aMinute);
            auto const s = to_integer(aSecond);
            auto const milli = to_integer(aMillisecond);
            return ((h * msPerHour + m * msPerMinute) + s * msPerSecond) + milli;
        }

        // (MakeDay)
        double make_day(double aYear, double aMonth, double aDate)
        {
            if (!std::isfinite(aYear) || !std::isfinite(aMonth) || !std::isfinite(aDate))
                return nan;
            auto const y = to_integer(aYear);
            auto const m = to_integer(aMonth);
            auto const dt = to_integer(aDate);
            auto const ym = y + std::floor(m / 12.0);
            // (a year beyond this range has no time value however the date offsets it: the time value of its first day is
            // beyond any date's reach that a double can hold exactly)
            if (!std::isfinite(ym) || std::fabs(ym) > 4.0e8)
                return nan;
            auto const year = static_cast<std::int64_t>(ym);
            auto const mn = static_cast<int>(modulo(m, 12.0));
            auto const firstDay = day_from_year(year) + sMonthStart[in_leap_year(year) ? 1 : 0][mn];
            return static_cast<double>(firstDay) + dt - 1.0;
        }

        // (MakeDate)
        double make_date(double aDay, double aTime)
        {
            if (!std::isfinite(aDay) || !std::isfinite(aTime))
                return nan;
            auto const tv = aDay * msPerDay + aTime;
            if (!std::isfinite(tv))
                return nan;
            return tv;
        }

        // (MakeFullYear)
        double make_full_year(double aYear)
        {
            if (std::isnan(aYear))
                return nan;
            auto const truncated = to_integer(aYear);
            if (truncated >= 0.0 && truncated <= 99.0)
                return 1900.0 + truncated;
            return truncated;
        }

        // (TimeClip)
        double time_clip(double aTime)
        {
            if (!std::isfinite(aTime) || std::fabs(aTime) > maxTimeValue)
                return nan;
            return to_integer(aTime);
        }

        // --- the host's time zone

        std::chrono::time_zone const* host_zone()
        {
            static std::chrono::time_zone const* const sZone = []() -> std::chrono::time_zone const*
            {
                try
                {
                    if (auto const* tz = std::getenv("TZ"); tz != nullptr && *tz != '\0')
                    {
                        std::string_view name{ tz };
                        if (name.front() == ':')
                            name.remove_prefix(1u);
                        try
                        {
                            return std::chrono::locate_zone(name);
                        }
                        catch (...)
                        {
                        }
                    }
                    return std::chrono::current_zone();
                }
                catch (...)
                {
                    return nullptr;
                }
            }();
            return sZone;
        }

        // (seconds the time zone database is asked about: a time before 1600 has the offset of 1600 (every zone's local
        // mean time); a time after 2600 is moved back by whole 400-year cycles (146097 days: whole weeks, so the
        // database's rules for the future repeat exactly))
        constexpr std::int64_t sCycleSeconds = 146097LL * 86400LL;
        constexpr std::int64_t sLowSeconds = day_from_year(1600) * 86400LL;
        constexpr std::int64_t sHighSeconds = day_from_year(2600) * 86400LL;

        std::int64_t database_seconds(double aTime)
        {
            auto s = static_cast<std::int64_t>(std::floor(aTime / msPerSecond));
            if (s < sLowSeconds)
                s = sLowSeconds;
            else if (s >= sHighSeconds)
                s -= ((s - sHighSeconds) / sCycleSeconds + 1) * sCycleSeconds;
            return s;
        }

        struct zone_period
        {
            std::int64_t begin = 0;
            std::int64_t end = 0;
            double offset = 0.0;    ///< (ms)
            std::string abbreviation;
        };

        zone_period const& period_at(std::int64_t aSeconds)
        {
            thread_local zone_period sCache;
            thread_local bool sCached = false;
            if (sCached && aSeconds >= sCache.begin && aSeconds < sCache.end)
                return sCache;
            sCached = true;
            auto const* zone = host_zone();
            if (zone == nullptr)
            {
                sCache = { std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max(), 0.0, "UTC" };
                return sCache;
            }
            try
            {
                auto const info = zone->get_info(std::chrono::sys_seconds{ std::chrono::seconds{ aSeconds } });
                sCache.begin = info.begin.time_since_epoch().count();
                sCache.end = info.end.time_since_epoch().count();
                sCache.offset = static_cast<double>(info.offset.count()) * msPerSecond;
                sCache.abbreviation = info.abbrev;
            }
            catch (...)
            {
                sCache = { aSeconds, aSeconds + 1, 0.0, "UTC" };
            }
            return sCache;
        }

        // (the offset (ms) of local time from UTC at the time value)
        double offset_at(double aTime)
        {
            return period_at(database_seconds(aTime)).offset;
        }

        // (LocalTime)
        double local_time(double aTime)
        {
            return aTime + offset_at(aTime);
        }

        // (UTC: a local time that is repeated (a backward transition) is the earlier of its instants; a local time that
        // is skipped (a forward transition) is interpreted with the offset before the transition)
        double utc(double aLocal)
        {
            if (!std::isfinite(aLocal))
                return nan;
            auto const* zone = host_zone();
            if (zone == nullptr)
                return aLocal;
            double offset = 0.0;
            try
            {
                auto const info = zone->get_info(std::chrono::local_seconds{ std::chrono::seconds{ database_seconds(aLocal) } });
                offset = static_cast<double>(info.first.offset.count()) * msPerSecond;
            }
            catch (...)
            {
            }
            return aLocal - offset;
        }

        double now()
        {
            using namespace std::chrono;
            return static_cast<double>(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
        }

        // --- formatting

        constexpr char const* sDayNames[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
        constexpr char const* sMonthNames[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

        std::string padded(std::int64_t aNumber, int aWidth)
        {
            auto text = std::to_string(aNumber);
            if (static_cast<int>(text.size()) < aWidth)
                text.insert(0u, static_cast<std::size_t>(aWidth) - text.size(), '0');
            return text;
        }

        std::string year_text(std::int64_t aYear)
        {
            return (aYear < 0 ? "-" : "") + padded(aYear < 0 ? -aYear : aYear, 4);
        }

        // (DateString)
        std::string date_string(double aLocal)
        {
            auto const c = civil_from_time(aLocal);
            return std::string{ sDayNames[c.weekDay] } + " " + sMonthNames[c.month] + " " + padded(c.date, 2) + " " + year_text(c.year);
        }

        // (TimeString)
        std::string time_string(double aTime)
        {
            auto const c = civil_from_time(aTime);
            return padded(c.hour, 2) + ":" + padded(c.minute, 2) + ":" + padded(c.second, 2) + " GMT";
        }

        // (TimeZoneString)
        std::string time_zone_string(double aTime)
        {
            auto const& period = period_at(database_seconds(aTime));
            auto const offset = static_cast<std::int64_t>(period.offset);
            auto const absolute = offset < 0 ? -offset : offset;
            auto const text = std::string{ offset >= 0 ? "+" : "-" } + padded(absolute / 3600000, 2) + padded(absolute / 60000 % 60, 2);
            return text + " (" + period.abbreviation + ")";
        }

        // (ToDateString)
        std::string to_date_string(double aTime)
        {
            if (std::isnan(aTime))
                return "Invalid Date";
            auto const t = local_time(aTime);
            return date_string(t) + " " + time_string(t) + time_zone_string(aTime);
        }

        std::string utc_string(double aTime)
        {
            auto const c = civil_from_time(aTime);
            return std::string{ sDayNames[c.weekDay] } + ", " + padded(c.date, 2) + " " + sMonthNames[c.month] + " " + year_text(c.year) + " " +
                time_string(aTime);
        }

        std::string iso_string(double aTime)
        {
            auto const c = civil_from_time(aTime);
            std::string year;
            if (c.year >= 0 && c.year <= 9999)
                year = padded(c.year, 4);
            else
                year = (c.year < 0 ? "-" : "+") + padded(c.year < 0 ? -c.year : c.year, 6);
            return year + "-" + padded(c.month + 1, 2) + "-" + padded(c.date, 2) + "T" + padded(c.hour, 2) + ":" + padded(c.minute, 2) + ":" +
                padded(c.second, 2) + "." + padded(c.millisecond, 3) + "Z";
        }

        // --- parsing (Date.parse)

        bool is_digit(char16_t aCharacter)
        {
            return aCharacter >= u'0' && aCharacter <= u'9';
        }

        bool is_letter(char16_t aCharacter)
        {
            return (aCharacter >= u'a' && aCharacter <= u'z') || (aCharacter >= u'A' && aCharacter <= u'Z');
        }

        int days_in_month(std::int64_t aYear, int aMonth)   ///< (aMonth 0 to 11)
        {
            auto const& starts = sMonthStart[in_leap_year(aYear) ? 1 : 0];
            return static_cast<int>(starts[aMonth + 1] - starts[aMonth]);
        }

        // (the Date Time String Format (§21.4.1.32): none if the text is not in it (the other formats are tried); NaN if
        // it is but its values are not valid)
        std::optional<double> parse_iso(std::u16string_view aText)
        {
            std::size_t pos = 0u;
            auto const digits = [&](std::size_t aCount, std::int64_t& aResult) -> bool
            {
                if (pos + aCount > aText.size())
                    return false;
                aResult = 0;
                for (std::size_t i = 0u; i < aCount; ++i)
                {
                    if (!is_digit(aText[pos + i]))
                        return false;
                    aResult = aResult * 10 + (aText[pos + i] - u'0');
                }
                pos += aCount;
                return true;
            };
            auto const next = [&](char16_t aCharacter) -> bool
            {
                if (pos < aText.size() && aText[pos] == aCharacter)
                {
                    ++pos;
                    return true;
                }
                return false;
            };
            std::int64_t year = 0;
            bool invalid = false;
            if (pos < aText.size() && (aText[pos] == u'+' || aText[pos] == u'-'))
            {
                bool const negative = aText[pos++] == u'-';
                if (!digits(6u, year))
                    return {};
                if (negative)
                {
                    if (year == 0)
                        invalid = true;     // (-000000)
                    year = -year;
                }
            }
            else if (!digits(4u, year))
                return {};
            std::int64_t month = 1;
            std::int64_t date = 1;
            if (next(u'-'))
            {
                if (!digits(2u, month))
                    return {};
                if (next(u'-') && !digits(2u, date))
                    return {};
            }
            std::int64_t hour = 0;
            std::int64_t minute = 0;
            std::int64_t second = 0;
            std::int64_t millisecond = 0;
            bool const hasTime = next(u'T');
            if (hasTime)
            {
                if (!digits(2u, hour) || !next(u':') || !digits(2u, minute))
                    return {};
                if (next(u':'))
                {
                    if (!digits(2u, second))
                        return {};
                    if (next(u'.'))
                    {
                        // (one or more digits: three are significant)
                        if (pos >= aText.size() || !is_digit(aText[pos]))
                            return {};
                        int count = 0;
                        while (pos < aText.size() && is_digit(aText[pos]))
                        {
                            if (count < 3)
                                millisecond = millisecond * 10 + (aText[pos] - u'0');
                            ++count;
                            ++pos;
                        }
                        for (; count < 3; ++count)
                            millisecond *= 10;
                    }
                }
            }
            std::optional<double> offset;
            if (hasTime && pos < aText.size())
            {
                if (next(u'Z'))
                    offset = 0.0;
                else if (aText[pos] == u'+' || aText[pos] == u'-')
                {
                    bool const negative = aText[pos++] == u'-';
                    std::int64_t offsetHour = 0;
                    std::int64_t offsetMinute = 0;
                    if (!digits(2u, offsetHour) || !next(u':') || !digits(2u, offsetMinute))
                        return {};
                    if (offsetHour > 23 || offsetMinute > 59)
                        invalid = true;
                    offset = (negative ? -1.0 : 1.0) * static_cast<double>(offsetHour * 60 + offsetMinute) * msPerMinute;
                }
            }
            if (pos != aText.size())
                return {};
            if (invalid || month < 1 || month > 12 || date < 1 || date > days_in_month(year, static_cast<int>(month - 1)) ||
                hour > 24 || minute > 59 || second > 59 || (hour == 24 && (minute != 0 || second != 0 || millisecond != 0)))
                return nan;
            auto const d = make_date(make_day(static_cast<double>(year), static_cast<double>(month - 1), static_cast<double>(date)),
                make_time(static_cast<double>(hour), static_cast<double>(minute), static_cast<double>(second), static_cast<double>(millisecond)));
            double tv;
            if (offset)
                tv = d - *offset;
            else if (hasTime)
                tv = utc(d);
            else
                tv = d;     // (a date-only form is UTC)
            return time_clip(tv);
        }

        // (other formats: those of toString, toUTCString and toDateString, and common ones, e.g. "Jan 2 2006 15:04:05",
        // "2 January 2006", "1/2/2006 3:04 PM", "2006/01/02 15:04 GMT+0700", "2006-1-2 15:04:05"; a text without a time
        // zone is local time)
        double parse_legacy(std::u16string_view aText)
        {
            struct number_token
            {
                std::int64_t value;
                int digits;
                bool signed_;
            };
            std::optional<std::int64_t> year;
            std::optional<int> month;
            std::optional<std::int64_t> date;
            bool yearFull = false;      ///< (the year has three or more digits or a sign: not a two-digit year)
            std::optional<std::int64_t> hour;
            std::int64_t minute = 0;
            std::int64_t second = 0;
            std::int64_t millisecond = 0;
            int meridiem = 0;           ///< (1 AM, 2 PM)
            bool utcWord = false;
            std::optional<double> offset;
            thread_local std::vector<number_token> tNumbers;
            tNumbers.clear();
            std::size_t pos = 0u;
            auto const n = aText.size();
            auto const read_number = [&](int& aDigits) -> std::int64_t
            {
                std::int64_t result = 0;
                aDigits = 0;
                while (pos < n && is_digit(aText[pos]))
                {
                    if (aDigits < 15)
                        result = result * 10 + (aText[pos] - u'0');
                    ++aDigits;
                    ++pos;
                }
                return result;
            };
            while (pos < n)
            {
                auto const c = aText[pos];
                if (c == u' ' || c == u',' || c == u'\t' || c == u'\n' || c == u'\r')
                {
                    ++pos;
                    continue;
                }
                if (c == u'(')
                {
                    int depth = 0;
                    while (pos < n)
                    {
                        if (aText[pos] == u'(')
                            ++depth;
                        else if (aText[pos] == u')' && --depth == 0)
                        {
                            ++pos;
                            break;
                        }
                        ++pos;
                    }
                    continue;
                }
                if (is_letter(c))
                {
                    std::string word;
                    while (pos < n && (is_letter(aText[pos]) || aText[pos] == u'.'))
                    {
                        if (aText[pos] != u'.')
                            word.push_back(static_cast<char>(aText[pos] | 0x20));
                        ++pos;
                    }
                    if (word == "am" || word == "pm")
                    {
                        if (meridiem != 0)
                            return nan;
                        meridiem = word == "am" ? 1 : 2;
                        continue;
                    }
                    if (word == "utc" || word == "gmt" || word == "ut" || word == "z")
                    {
                        if (offset || utcWord)
                            return nan;
                        utcWord = true;
                        offset = 0.0;
                        continue;
                    }
                    // (a T before a time: only in the Date Time String Format (e.g. "1997-3-8T11:19" is not a date))
                    static constexpr std::pair<char const*, int> sZones[] =
                    {
                        { "est", -5 }, { "edt", -4 }, { "cst", -6 }, { "cdt", -5 }, { "mst", -7 }, { "mdt", -6 }, { "pst", -8 }, { "pdt", -7 }
                    };
                    bool known = false;
                    for (auto const& [name, hours] : sZones)
                        if (word == name)
                        {
                            if (offset)
                                return nan;
                            offset = hours * msPerHour;
                            known = true;
                        }
                    if (known)
                        continue;
                    if (word.size() >= 3u)
                    {
                        static constexpr char const* sMonths[] =
                        {
                            "january", "february", "march", "april", "may", "june", "july", "august", "september", "october", "november", "december"
                        };
                        static constexpr char const* sDays[] = { "sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday" };
                        auto const prefix_of = [&](char const* aName) { return std::string_view{ aName }.starts_with(word); };
                        for (int m = 0; m < 12 && !known; ++m)
                            if (prefix_of(sMonths[m]))
                            {
                                if (month)
                                    return nan;
                                month = m;
                                known = true;
                            }
                        for (int d = 0; d < 7 && !known; ++d)
                            if (prefix_of(sDays[d]))
                                known = true;
                    }
                    if (!known)
                        return nan;
                    continue;
                }
                if ((c == u'+' || c == u'-') && pos + 1u < n && is_digit(aText[pos + 1u]))
                {
                    bool const negative = c == u'-';
                    ++pos;
                    if (hour || utcWord)
                    {
                        // (a time zone offset: hh, hhmm or hh:mm)
                        if (offset && !utcWord)
                            return nan;
                        int count = 0;
                        auto v = read_number(count);
                        std::int64_t hours = 0;
                        std::int64_t minutes = 0;
                        if (count <= 2)
                        {
                            hours = v;
                            if (pos + 1u < n && aText[pos] == u':' && is_digit(aText[pos + 1u]))
                            {
                                ++pos;
                                minutes = read_number(count);
                                if (count != 2)
                                    return nan;
                            }
                        }
                        else if (count == 4)
                        {
                            hours = v / 100;
                            minutes = v % 100;
                        }
                        else
                            return nan;
                        if (hours > 23 || minutes > 59)
                            return nan;
                        offset = (negative ? -1.0 : 1.0) * static_cast<double>(hours * 60 + minutes) * msPerMinute;
                        utcWord = false;
                        continue;
                    }
                    int count = 0;
                    auto const v = read_number(count);
                    if (pos + 1u < n && aText[pos] == u'-' && is_digit(aText[pos + 1u]))
                    {
                        // (a signed year's date: y-m[-d] (e.g. "+001997-3-8"))
                        if (date || year || month)
                            return nan;
                        ++pos;
                        int count2 = 0;
                        auto const v2 = read_number(count2);
                        std::int64_t v3 = 1;
                        if (pos + 1u < n && aText[pos] == u'-' && is_digit(aText[pos + 1u]))
                        {
                            ++pos;
                            int count3 = 0;
                            v3 = read_number(count3);
                        }
                        year = negative ? -v : v;
                        yearFull = true;
                        month = static_cast<int>(v2) - 1;
                        date = v3;
                        continue;
                    }
                    tNumbers.push_back({ negative ? -v : v, count, true });
                    continue;
                }
                if (is_digit(c))
                {
                    int count = 0;
                    auto const v = read_number(count);
                    if (pos < n && aText[pos] == u':')
                    {
                        // (a time: h:mm[:ss[.sss]])
                        if (hour)
                            return nan;
                        hour = v;
                        ++pos;
                        minute = read_number(count);
                        if (count == 0 || count > 2)
                            return nan;
                        if (pos < n && aText[pos] == u':')
                        {
                            ++pos;
                            second = read_number(count);
                            if (count == 0 || count > 2)
                                return nan;
                            if (pos + 1u < n && aText[pos] == u'.' && is_digit(aText[pos + 1u]))
                            {
                                ++pos;
                                std::int64_t fraction = 0;
                                int fractionDigits = 0;
                                while (pos < n && is_digit(aText[pos]))
                                {
                                    if (fractionDigits < 3)
                                    {
                                        fraction = fraction * 10 + (aText[pos] - u'0');
                                        ++fractionDigits;
                                    }
                                    ++pos;
                                }
                                for (; fractionDigits < 3; ++fractionDigits)
                                    fraction *= 10;
                                millisecond = fraction;
                            }
                        }
                        continue;
                    }
                    if (pos + 1u < n && (aText[pos] == u'/' || aText[pos] == u'-') && is_digit(aText[pos + 1u]))
                    {
                        // (a numeric date: y/m/d or m/d/y (or with hyphens))
                        auto const separator = aText[pos];
                        if (date || year)
                            return nan;
                        ++pos;
                        int count2 = 0;
                        auto const v2 = read_number(count2);
                        std::optional<std::int64_t> v3;
                        int count3 = 0;
                        if (pos + 1u < n && aText[pos] == separator && is_digit(aText[pos + 1u]))
                        {
                            ++pos;
                            v3 = read_number(count3);
                        }
                        if (count >= 3 || v > 31)
                        {
                            year = v;
                            yearFull = count >= 3;
                            if (month)
                                return nan;
                            month = static_cast<int>(v2) - 1;
                            date = v3.value_or(1);
                        }
                        else
                        {
                            if (month)
                                return nan;
                            month = static_cast<int>(v) - 1;
                            date = v2;
                            if (v3)
                            {
                                year = *v3;
                                yearFull = count3 >= 3;
                            }
                        }
                        continue;
                    }
                    tNumbers.push_back({ v, count, false });
                    continue;
                }
                if (c == u'-' || c == u'/' || c == u'.')
                {
                    ++pos;
                    continue;
                }
                return nan;
            }
            // (the other numbers: a day of the month and a year)
            for (auto const& number : tNumbers)
            {
                // (a two-digit number greater than 31 is a year)
                if (!number.signed_ && number.digits <= 2 && number.value <= 31 && !date && month)
                    date = number.value;
                else if (!year)
                {
                    year = number.value;
                    yearFull = number.signed_ || number.digits >= 3;
                }
                else if (!date && !number.signed_ && number.digits <= 2)
                    date = number.value;
                else
                    return nan;
            }
            if (!year || !month)
                return nan;
            if (!date)
                date = 1;
            if (!yearFull)
                *year += *year < 50 ? 2000 : 1900;
            std::int64_t h = hour.value_or(0);
            if (meridiem != 0)
            {
                if (!hour || h < 1 || h > 12)
                    return nan;
                h = (h % 12) + (meridiem == 2 ? 12 : 0);
            }
            if (*month < 0 || *month > 11 || *date < 1 || *date > 31 || h > 24 || minute > 59 || second > 59 ||
                (h == 24 && (minute != 0 || second != 0 || millisecond != 0)))
                return nan;
            auto const d = make_date(make_day(static_cast<double>(*year), static_cast<double>(*month), static_cast<double>(*date)),
                make_time(static_cast<double>(h), static_cast<double>(minute), static_cast<double>(second), static_cast<double>(millisecond)));
            return time_clip(offset ? d - *offset : utc(d));
        }

        double parse_date(value aString)
        {
            std::u16string_view const text = str(aString);
            if (auto const iso = parse_iso(text))
                return *iso;
            return parse_legacy(text);
        }

        // --- the Date constructor and its functions

        value make_text(std::string const& aText)
        {
            return make_string_utf8(aText);
        }

        value date_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (aNewTarget == Undefined)
                return make_text(to_date_string(now()));
            double tv;
            if (aArguments.empty())
                tv = now();
            else if (aArguments.size() == 1u)
            {
                auto const v = aArguments[0];
                if (is_object(v) && obj(v).cls == object_class::Date)
                    tv = as_number(obj(v).internal);
                else
                {
                    auto const primitive = to_primitive(v);
                    if (is_string(primitive))
                        tv = parse_date(primitive);
                    else
                        tv = to_number(primitive);
                }
            }
            else
            {
                double fields[7] = { 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0 };
                for (std::size_t i = 0u; i < 7u && i < aArguments.size(); ++i)
                    fields[i] = to_number(aArguments[i]);
                auto const finalDate = make_date(make_day(make_full_year(fields[0]), fields[1], fields[2]),
                    make_time(fields[3], fields[4], fields[5], fields[6]));
                tv = utc(finalDate);
            }
            auto const o = ordinary_create_from_constructor(aNewTarget, &realm::datePrototype, object_class::Date);
            obj(o).internal = number(time_clip(tv));
            return o;
        }

        value date_now(value, arguments, value, function_object&)
        {
            return number(now());
        }

        value date_parse(value, arguments aArguments, value, function_object&)
        {
            return number(parse_date(to_string(argument(aArguments, 0u))));
        }

        value date_utc(value, arguments aArguments, value, function_object&)
        {
            double fields[7] = { 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0 };
            fields[0] = to_number(argument(aArguments, 0u));
            for (std::size_t i = 1u; i < 7u && i < aArguments.size(); ++i)
                fields[i] = to_number(aArguments[i]);
            return number(time_clip(make_date(make_day(make_full_year(fields[0]), fields[1], fields[2]),
                make_time(fields[3], fields[4], fields[5], fields[6]))));
        }

        // --- Date.prototype

        object& this_date(value aThis)
        {
            if (!is_object(aThis) || obj(aThis).cls != object_class::Date)
                throw_type_error("this is not a Date object.");
            return obj(aThis);
        }

        double this_time_value(value aThis)
        {
            return as_number(this_date(aThis).internal);
        }

        value set_time_value(object& aDate, double aTime)
        {
            aDate.internal = number(aTime);
            return aDate.internal;
        }

        enum date_field : std::uint32_t
        {
            FieldFullYear,
            FieldMonth,
            FieldDate,
            FieldDay,
            FieldHours,
            FieldMinutes,
            FieldSeconds,
            FieldMilliseconds,
            FieldUtc = 0x100u
        };

        // (getDate, getDay, getFullYear, ... and their UTC versions)
        value date_get_field(value aThis, arguments, value, function_object& aCallee)
        {
            auto t = this_time_value(aThis);
            if (std::isnan(t))
                return CanonicalNaN;
            if ((aCallee.nativeId & FieldUtc) == 0u)
                t = local_time(t);
            auto const c = civil_from_time(t);
            switch (aCallee.nativeId & ~FieldUtc)
            {
            case FieldFullYear: return number(static_cast<double>(c.year));
            case FieldMonth: return number(c.month);
            case FieldDate: return number(c.date);
            case FieldDay: return number(c.weekDay);
            case FieldHours: return number(c.hour);
            case FieldMinutes: return number(c.minute);
            case FieldSeconds: return number(c.second);
            default: return number(c.millisecond);
            }
        }

        value date_get_time(value aThis, arguments, value, function_object&)
        {
            return number(this_time_value(aThis));
        }

        value date_get_timezone_offset(value aThis, arguments, value, function_object&)
        {
            auto const t = this_time_value(aThis);
            if (std::isnan(t))
                return CanonicalNaN;
            return number((t - local_time(t)) / msPerMinute);
        }

        // (setHours, setMinutes, setSeconds, setMilliseconds and their UTC versions: the arguments are converted (those
        // present) before the time value is tested for NaN)
        value date_set_time_fields(value aThis, arguments aArguments, value, function_object& aCallee)
        {
            auto& date = this_date(aThis);
            auto t = as_number(date.internal);
            bool const isUtc = (aCallee.nativeId & FieldUtc) != 0u;
            auto const first = (aCallee.nativeId & ~FieldUtc) - FieldHours;     // (0 hours, 1 minutes, 2 seconds, 3 milliseconds)
            double fields[4] = {};
            bool present[4] = {};
            fields[first] = to_number(argument(aArguments, 0u));
            present[first] = true;
            for (std::size_t i = 1u; first + i < 4u && i < aArguments.size(); ++i)
            {
                fields[first + i] = to_number(aArguments[i]);
                present[first + i] = true;
            }
            if (std::isnan(t))
                return CanonicalNaN;
            if (!isUtc)
                t = local_time(t);
            auto const c = civil_from_time(t);
            double const current[4] = { static_cast<double>(c.hour), static_cast<double>(c.minute), static_cast<double>(c.second),
                static_cast<double>(c.millisecond) };
            for (int i = 0; i < 4; ++i)
                if (!present[i])
                    fields[i] = current[i];
            auto const newDate = make_date(day(t), make_time(fields[0], fields[1], fields[2], fields[3]));
            return set_time_value(date, time_clip(isUtc ? newDate : utc(newDate)));
        }

        // (setFullYear, setMonth, setDate and their UTC versions)
        value date_set_date_fields(value aThis, arguments aArguments, value, function_object& aCallee)
        {
            auto& date = this_date(aThis);
            auto t = as_number(date.internal);
            bool const isUtc = (aCallee.nativeId & FieldUtc) != 0u;
            auto const first = aCallee.nativeId & ~FieldUtc;      // (0 year, 1 month, 2 date)
            double fields[3] = {};
            bool present[3] = {};
            fields[first] = to_number(argument(aArguments, 0u));
            present[first] = true;
            if (first == FieldFullYear)
            {
                // (setFullYear: a NaN time value is +0; the month and date are converted after that)
                t = std::isnan(t) ? 0.0 : isUtc ? t : local_time(t);
                for (std::size_t i = 1u; i < 3u && i < aArguments.size(); ++i)
                {
                    fields[i] = to_number(aArguments[i]);
                    present[i] = true;
                }
            }
            else
            {
                for (std::size_t i = 1u; first + i < 3u && i < aArguments.size(); ++i)
                {
                    fields[first + i] = to_number(aArguments[i]);
                    present[first + i] = true;
                }
                if (std::isnan(t))
                    return CanonicalNaN;
                if (!isUtc)
                    t = local_time(t);
            }
            auto const c = civil_from_time(t);
            double const current[3] = { static_cast<double>(c.year), static_cast<double>(c.month), static_cast<double>(c.date) };
            for (int i = 0; i < 3; ++i)
                if (!present[i])
                    fields[i] = current[i];
            auto const newDate = make_date(make_day(fields[0], fields[1], fields[2]), time_within_day(t));
            return set_time_value(date, time_clip(isUtc ? newDate : utc(newDate)));
        }

        value date_set_time(value aThis, arguments aArguments, value, function_object&)
        {
            auto& date = this_date(aThis);
            auto const t = to_number(argument(aArguments, 0u));
            return set_time_value(date, time_clip(t));
        }

        value date_to_string(value aThis, arguments, value, function_object&)
        {
            return make_text(to_date_string(this_time_value(aThis)));
        }

        value date_to_date_string(value aThis, arguments, value, function_object&)
        {
            auto const tv = this_time_value(aThis);
            if (std::isnan(tv))
                return make_text("Invalid Date");
            return make_text(date_string(local_time(tv)));
        }

        value date_to_time_string(value aThis, arguments, value, function_object&)
        {
            auto const tv = this_time_value(aThis);
            if (std::isnan(tv))
                return make_text("Invalid Date");
            return make_text(time_string(local_time(tv)) + time_zone_string(tv));
        }

        // (toLocaleString (nativeId 0), toLocaleDateString (1) and toLocaleTimeString (2): ECMA-402's, with Intl; else toString's)
        value date_to_locale_string(value aThis, arguments aArguments, value aNewTarget, function_object& aCallee)
        {
#if defined(NEOS_JS_ICU)
            if (intl::icu_available())
            {
                auto const kind = static_cast<int>(aCallee.nativeId);
                return intl::date_to_locale_string(this_time_value(aThis), argument(aArguments, 0u), argument(aArguments, 1u), kind, kind);
            }
#endif
            switch (aCallee.nativeId)
            {
            case 1u: return date_to_date_string(aThis, aArguments, aNewTarget, aCallee);
            case 2u: return date_to_time_string(aThis, aArguments, aNewTarget, aCallee);
            default: return date_to_string(aThis, aArguments, aNewTarget, aCallee);
            }
        }

        value date_to_utc_string(value aThis, arguments, value, function_object&)
        {
            auto const tv = this_time_value(aThis);
            if (std::isnan(tv))
                return make_text("Invalid Date");
            return make_text(utc_string(tv));
        }

        value date_to_iso_string(value aThis, arguments, value, function_object&)
        {
            auto const tv = this_time_value(aThis);
            if (!std::isfinite(tv))
                throw_range_error("Invalid time value");
            return make_text(iso_string(tv));
        }

        value date_to_json(value aThis, arguments, value, function_object&)
        {
            auto const o = to_object(aThis);
            auto const tv = to_primitive(o, hint::Number);
            if (is_number(tv) && !std::isfinite(as_number(tv)))
                return Null;
            return invoke(o, ascii("toISOString"));
        }

        value date_to_primitive(value aThis, arguments aArguments, value, function_object&)
        {
            if (!is_object(aThis))
                throw_type_error("Date.prototype[Symbol.toPrimitive] called on non-object");
            auto const hintArgument = argument(aArguments, 0u);
            bool stringFirst = false;
            if (is_string(hintArgument))
            {
                auto const& h = str(hintArgument);
                if (h == u"string" || h == u"default")
                    stringFirst = true;
                else if (h != u"number")
                    throw_type_error("Invalid hint: " + utf8(hintArgument));
            }
            else
                throw_type_error("Invalid hint: " + describe(hintArgument));
            // (OrdinaryToPrimitive)
            auto const& names = the_names();
            for (auto const name : { stringFirst ? names.toString : names.valueOf, stringFirst ? names.valueOf : names.toString })
            {
                auto const method = get(aThis, name);
                if (is_callable(method))
                {
                    auto const result = call(method, aThis);
                    if (!is_object(result))
                        return result;
                }
            }
            throw_type_error("Cannot convert object to primitive value");
        }

        // --- Annex B

        value date_get_year(value aThis, arguments, value, function_object&)
        {
            auto const t = this_time_value(aThis);
            if (std::isnan(t))
                return CanonicalNaN;
            return number(static_cast<double>(civil_from_time(local_time(t)).year) - 1900.0);
        }

        value date_set_year(value aThis, arguments aArguments, value, function_object&)
        {
            auto& date = this_date(aThis);
            auto t = as_number(date.internal);
            auto const y = to_number(argument(aArguments, 0u));
            t = std::isnan(t) ? 0.0 : local_time(t);
            auto const yyyy = make_full_year(y);
            auto const c = civil_from_time(t);
            auto const d = make_day(yyyy, static_cast<double>(c.month), static_cast<double>(c.date));
            return set_time_value(date, time_clip(utc(make_date(d, time_within_day(t)))));
        }
    }

    void init_date(realm& aRealm)
    {
        auto const c = make_native("Date", 7u, date_constructor, true);
        aRealm.date = c;
        aRealm.datePrototype = make_object();
        auto const p = aRealm.datePrototype;
        obj(c).put(the_names().prototype, p, 0u);
        obj(p).put(the_names().constructor, c);
        method(c, "now", 0u, date_now);
        method(c, "parse", 1u, date_parse);
        method(c, "UTC", 7u, date_utc);
        struct field_method
        {
            char const* name;
            std::uint32_t id;
        };
        for (auto const& [name, id] : {
            field_method{ "getFullYear", FieldFullYear }, field_method{ "getMonth", FieldMonth }, field_method{ "getDate", FieldDate },
            field_method{ "getDay", FieldDay }, field_method{ "getHours", FieldHours }, field_method{ "getMinutes", FieldMinutes },
            field_method{ "getSeconds", FieldSeconds }, field_method{ "getMilliseconds", FieldMilliseconds },
            field_method{ "getUTCFullYear", FieldFullYear | FieldUtc }, field_method{ "getUTCMonth", FieldMonth | FieldUtc },
            field_method{ "getUTCDate", FieldDate | FieldUtc }, field_method{ "getUTCDay", FieldDay | FieldUtc },
            field_method{ "getUTCHours", FieldHours | FieldUtc }, field_method{ "getUTCMinutes", FieldMinutes | FieldUtc },
            field_method{ "getUTCSeconds", FieldSeconds | FieldUtc }, field_method{ "getUTCMilliseconds", FieldMilliseconds | FieldUtc } })
            as_function(method(p, name, 0u, date_get_field))->nativeId = id;
        struct setter_method
        {
            char const* name;
            std::uint32_t length;
            std::uint32_t id;
            native_function function;
        };
        for (auto const& [name, length, id, function] : {
            setter_method{ "setFullYear", 3u, FieldFullYear, date_set_date_fields }, setter_method{ "setMonth", 2u, FieldMonth, date_set_date_fields },
            setter_method{ "setDate", 1u, FieldDate, date_set_date_fields }, setter_method{ "setHours", 4u, FieldHours, date_set_time_fields },
            setter_method{ "setMinutes", 3u, FieldMinutes, date_set_time_fields }, setter_method{ "setSeconds", 2u, FieldSeconds, date_set_time_fields },
            setter_method{ "setMilliseconds", 1u, FieldMilliseconds, date_set_time_fields },
            setter_method{ "setUTCFullYear", 3u, FieldFullYear | FieldUtc, date_set_date_fields },
            setter_method{ "setUTCMonth", 2u, FieldMonth | FieldUtc, date_set_date_fields },
            setter_method{ "setUTCDate", 1u, FieldDate | FieldUtc, date_set_date_fields },
            setter_method{ "setUTCHours", 4u, FieldHours | FieldUtc, date_set_time_fields },
            setter_method{ "setUTCMinutes", 3u, FieldMinutes | FieldUtc, date_set_time_fields },
            setter_method{ "setUTCSeconds", 2u, FieldSeconds | FieldUtc, date_set_time_fields },
            setter_method{ "setUTCMilliseconds", 1u, FieldMilliseconds | FieldUtc, date_set_time_fields } })
            as_function(method(p, name, length, function))->nativeId = id;
        method(p, "getTime", 0u, date_get_time);
        method(p, "getTimezoneOffset", 0u, date_get_timezone_offset);
        method(p, "setTime", 1u, date_set_time);
        method(p, "valueOf", 0u, date_get_time);
        method(p, "toString", 0u, date_to_string);
        method(p, "toDateString", 0u, date_to_date_string);
        method(p, "toTimeString", 0u, date_to_time_string);
        method(p, "toISOString", 0u, date_to_iso_string);
        auto const toUTCString = method(p, "toUTCString", 0u, date_to_utc_string);
        method(p, "toJSON", 1u, date_to_json);
        as_function(method(p, "toLocaleString", 0u, date_to_locale_string))->nativeId = 0u;
        as_function(method(p, "toLocaleDateString", 0u, date_to_locale_string))->nativeId = 1u;
        as_function(method(p, "toLocaleTimeString", 0u, date_to_locale_string))->nativeId = 2u;
        auto const toPrimitive = make_native(aRealm.symToPrimitive, 1u, date_to_primitive);
        obj(p).put(aRealm.symToPrimitive, toPrimitive, AttributeConfigurable);
        // (Annex B.2.3)
        method(p, "getYear", 0u, date_get_year);
        method(p, "setYear", 1u, date_set_year);
        obj(p).put(ascii("toGMTString"), toUTCString);
        obj(aRealm.global).put(ascii("Date"), c);
    }
}
