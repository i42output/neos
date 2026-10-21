/*
  temporal_timezone.cpp

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

// Temporal: time zones. Offset time zones (±HH:MM) and the named time zones of the IANA Time Zone Database through C++20
// <chrono> (std::chrono::get_tzdb, time_zone::get_info); only "UTC" if the library has no time zone database.
//
// Identifiers are matched ASCII-case-insensitively; a Link's primary identifier is its target (and the zones that CLDR
// treats as UTC (Etc/UTC, Etc/GMT and their links) have the primary identifier "UTC"). Times beyond the range the
// library can answer for (it fails after the year 32767) are moved back by whole 400-year Gregorian cycles (146097 days:
// whole weeks, so the database's rules for the future repeat exactly).

#include <algorithm>
#include <cstdlib>
#include <map>
#include "temporal.hpp"

namespace neos::javascript_runtime::temporal
{
    namespace
    {
        struct zone_table
        {
            std::map<std::u16string, named_zone_record> byLowercase;
        };

        bool is_utc_alias(std::string const& aTarget)
        {
            return aTarget == "Etc/UTC" || aTarget == "Etc/GMT" || aTarget == "UTC" || aTarget == "GMT";
        }

        zone_table const& zones()
        {
            static zone_table const sTable = []()
            {
                zone_table table;
                try
                {
                    auto const& db = std::chrono::get_tzdb();
                    for (auto const& z : db.zones)
                    {
                        auto const name = u16(std::string{ z.name() });
                        auto const primary = is_utc_alias(std::string{ z.name() }) ? std::u16string{ u"UTC" } : name;
                        table.byLowercase[ascii_lowercase(name)] = { name, primary, &z };
                    }
                    for (auto const& l : db.links)
                    {
                        auto const name = u16(std::string{ l.name() });
                        auto const target = std::string{ l.target() };
                        auto const* zone = std::chrono::locate_zone(target);
                        auto const primary = is_utc_alias(target) || is_utc_alias(std::string{ l.name() }) ? std::u16string{ u"UTC" } : u16(target);
                        table.byLowercase[ascii_lowercase(name)] = { name, primary, zone };
                    }
                }
                catch (...)
                {
                }
                auto& utc = table.byLowercase[u"utc"];
                utc.identifier = u"UTC";
                utc.primary = u"UTC";
                return table;
            }();
            return sTable;
        }

        constexpr std::int64_t sCycleSeconds = 146097LL * 86400LL;
        // (the start of the year 30000: the library answers below it; and of the years 32000 and -32000 (beyond them: no
        // transition))
        constexpr std::int64_t sHighSeconds = 884541340800LL;
        constexpr std::int64_t sNoTransitionHigh = 947656800000LL;
        constexpr std::int64_t sNoTransitionLow = -1072014220800LL;
        constexpr std::int64_t sNone = std::numeric_limits<std::int64_t>::max();   ///< (a period's end: no later transition)
        constexpr std::int64_t sNoneBefore = std::numeric_limits<std::int64_t>::min();  ///< (a period's begin: no earlier one)

        struct period
        {
            std::int64_t begin = 0;     ///< (seconds; sNoneBefore: none)
            std::int64_t end = 0;       ///< (seconds; sNone: none)
            std::int64_t offset = 0;    ///< (seconds)
        };

        period period_at(std::chrono::time_zone const* aZone, std::int64_t aSeconds)
        {
            if (aZone == nullptr)
                return { sNoneBefore, sNone, 0 };
            std::int64_t shift = 0;
            if (aSeconds >= sHighSeconds)
            {
                auto const cycles = (aSeconds - sHighSeconds) / sCycleSeconds + 1;
                shift = cycles * sCycleSeconds;
                aSeconds -= shift;
            }
            try
            {
                auto const info = aZone->get_info(std::chrono::sys_seconds{ std::chrono::seconds{ aSeconds } });
                period p;
                p.begin = info.begin.time_since_epoch().count();
                p.end = info.end.time_since_epoch().count();
                p.offset = info.offset.count();
                if (p.begin <= sNoTransitionLow)
                    p.begin = sNoneBefore;
                else
                    p.begin += shift;
                if (p.end >= sNoTransitionHigh)
                    p.end = sNone;
                else
                    p.end += shift;
                return p;
            }
            catch (...)
            {
                return { sNoneBefore, sNone, 0 };
            }
        }

        std::int64_t floor_seconds(i128 const& aEpochNs)
        {
            return floor_div(aEpochNs, i128{ 1000000000 }).to_int64();
        }

        std::int64_t named_offset_ns(std::chrono::time_zone const* aZone, i128 const& aEpochNs)
        {
            return period_at(aZone, floor_seconds(aEpochNs)).offset * 1000000000LL;
        }

        time_zone make_offset_zone(int aOffsetMinutes)
        {
            time_zone tz;
            tz.id = make_string(u16(format_offset_minutes(aOffsetMinutes)));
            tz.named = false;
            tz.offsetMinutes = aOffsetMinutes;
            return tz;
        }

        time_zone make_named_zone(named_zone_record const& aRecord)
        {
            time_zone tz;
            tz.id = make_string(aRecord.identifier);
            tz.named = true;
            tz.zone = aRecord.zone;
            return tz;
        }
    }

    std::optional<named_zone_record> get_available_named_time_zone(std::u16string const& aName)
    {
        auto const& table = zones().byLowercase;
        auto const it = table.find(ascii_lowercase(aName));
        if (it == table.end())
            return {};
        return it->second;
    }

    time_zone time_zone_from_identifier(std::u16string const& aIdentifier)
    {
        std::u16string name;
        int offsetMinutes = 0;
        bool isOffset = false;
        if (!parse_time_zone_identifier(aIdentifier, name, offsetMinutes, isOffset))
            throw_range_error("invalid time zone identifier");
        if (isOffset)
            return make_offset_zone(offsetMinutes);
        auto const record = get_available_named_time_zone(name);
        if (!record)
            throw_range_error("unknown time zone: " + utf8(make_string(name)));
        return make_named_zone(*record);
    }

    time_zone parse_time_zone_identifier_checked(value aString)
    {
        return time_zone_from_identifier(str(aString));
    }

    time_zone system_time_zone()
    {
        static std::u16string const sIdentifier = []() -> std::u16string
        {
            try
            {
                if (auto const* tz = std::getenv("TZ"); tz != nullptr && *tz != '\0')
                {
                    std::string_view name{ tz };
                    if (name.front() == ':')
                        name.remove_prefix(1u);
                    if (auto const record = get_available_named_time_zone(u16(std::string{ name })))
                        return record->primary;
                }
                auto const* current = std::chrono::current_zone();
                if (current != nullptr)
                    if (auto const record = get_available_named_time_zone(u16(std::string{ current->name() })))
                        return record->primary;
            }
            catch (...)
            {
            }
            return u"UTC";
        }();
        return time_zone_from_identifier(sIdentifier);
    }

    time_zone to_temporal_time_zone_identifier(value aValue)
    {
        if (auto const* zdt = as_temporal(aValue, object_class::TemporalZonedDateTime))
            return zdt->timeZone;
        if (!is_string(aValue))
            throw_type_error("time zone must be a string");
        auto const& text = str(aValue);
        std::u16string name;
        int offsetMinutes = 0;
        bool isOffset = false;
        if (!parse_time_zone_identifier(text, name, offsetMinutes, isOffset))
        {
            auto const result = parse_iso_date_time(text, { GoalDateTimeZoned, GoalDateTime, GoalInstant, GoalTime, GoalMonthDay, GoalYearMonth });
            if (result.timeZoneAnnotation)
                parse_time_zone_identifier(*result.timeZoneAnnotation, name, offsetMinutes, isOffset);
            else if (result.z)
                name = u"UTC";
            else if (result.offset)
            {
                if (!parse_time_zone_identifier(*result.offset, name, offsetMinutes, isOffset))
                    throw_range_error("a time zone offset must not have seconds");
            }
            else
                throw_range_error("the string has no time zone");
        }
        if (isOffset)
            return make_offset_zone(offsetMinutes);
        auto const record = get_available_named_time_zone(name);
        if (!record)
            throw_range_error("unknown time zone: " + utf8(make_string(name)));
        return make_named_zone(*record);
    }

    bool time_zone_equals(time_zone const& a, time_zone const& b)
    {
        if (string_equal(a.id, b.id))
            return true;
        if (a.named && b.named)
        {
            auto const ra = get_available_named_time_zone(str(a.id));
            auto const rb = get_available_named_time_zone(str(b.id));
            return ra && rb && ra->primary == rb->primary;
        }
        return false;
    }

    std::int64_t get_offset_ns_for(time_zone const& aTimeZone, i128 const& aEpochNs)
    {
        if (!aTimeZone.named)
            return static_cast<std::int64_t>(aTimeZone.offsetMinutes) * 60000000000LL;
        return named_offset_ns(aTimeZone.zone, aEpochNs);
    }

    iso_date_time get_iso_date_time_for(time_zone const& aTimeZone, i128 const& aEpochNs)
    {
        auto const offset = get_offset_ns_for(aTimeZone, aEpochNs);
        return iso_date_time_from_epoch_ns(aEpochNs + i128{ offset });
    }

    std::vector<i128> get_possible_epoch_ns(time_zone const& aTimeZone, iso_date_time const& aDateTime)
    {
        std::vector<i128> result;
        if (!aTimeZone.named)
        {
            auto const& t = aDateTime.time;
            auto const balanced = balance_iso_date_time(aDateTime.date, t.hour, i128{ t.minute } - i128{ aTimeZone.offsetMinutes }, t.second,
                t.millisecond, t.microsecond, t.nanosecond);
            check_iso_days_range(balanced.date);
            result.push_back(get_utc_epoch_ns(balanced));
        }
        else
        {
            check_iso_days_range(aDateTime.date);
            auto const local = get_utc_epoch_ns(aDateTime);
            auto const day = i128{ nsPerDay };
            for (auto const probe : { local - day, local + day })
            {
                auto const offset = named_offset_ns(aTimeZone.zone, probe);
                auto const candidate = local - i128{ offset };
                if (named_offset_ns(aTimeZone.zone, candidate) == offset &&
                    std::find(result.begin(), result.end(), candidate) == result.end())
                    result.push_back(candidate);
            }
            std::sort(result.begin(), result.end());
        }
        for (auto const& ns : result)
            if (!is_valid_epoch_ns(ns))
                throw_range_error("date-time is outside the representable range");
        return result;
    }

    i128 disambiguate_possible_epoch_ns(std::vector<i128> const& aPossible, time_zone const& aTimeZone, iso_date_time const& aDateTime,
        disambiguation aDisambiguation)
    {
        auto const n = aPossible.size();
        if (n == 1u)
            return aPossible[0];
        if (n != 0u)
        {
            if (aDisambiguation == disambiguation::Earlier || aDisambiguation == disambiguation::Compatible)
                return aPossible[0];
            if (aDisambiguation == disambiguation::Later)
                return aPossible[n - 1u];
            throw_range_error("the date-time is ambiguous in the time zone");
        }
        if (aDisambiguation == disambiguation::Reject)
            throw_range_error("the date-time does not exist in the time zone");
        auto const local = get_utc_epoch_ns(aDateTime);
        auto const day = i128{ nsPerDay };
        auto const offsetBefore = get_offset_ns_for(aTimeZone, local - day);
        auto const offsetAfter = get_offset_ns_for(aTimeZone, local + day);
        auto const nanoseconds = offsetAfter - offsetBefore;
        if (aDisambiguation == disambiguation::Earlier)
        {
            auto const earlierTime = add_time(aDateTime.time, i128{ -nanoseconds });
            iso_date_time const earlier{ add_days_to_iso_date(aDateTime.date, earlierTime.days), earlierTime };
            auto const possible = get_possible_epoch_ns(aTimeZone, earlier);
            if (possible.empty())
                throw_range_error("the date-time does not exist in the time zone");
            return possible[0];
        }
        auto const laterTime = add_time(aDateTime.time, i128{ nanoseconds });
        iso_date_time const later{ add_days_to_iso_date(aDateTime.date, laterTime.days), laterTime };
        auto const possible = get_possible_epoch_ns(aTimeZone, later);
        if (possible.empty())
            throw_range_error("the date-time does not exist in the time zone");
        return possible.back();
    }

    i128 get_epoch_ns_for(time_zone const& aTimeZone, iso_date_time const& aDateTime, disambiguation aDisambiguation)
    {
        auto const possible = get_possible_epoch_ns(aTimeZone, aDateTime);
        return disambiguate_possible_epoch_ns(possible, aTimeZone, aDateTime, aDisambiguation);
    }

    i128 get_start_of_day(time_zone const& aTimeZone, iso_date const& aDate)
    {
        iso_date_time const midnight{ aDate, {} };
        auto const possible = get_possible_epoch_ns(aTimeZone, midnight);
        if (!possible.empty())
            return possible[0];
        // (midnight is skipped: the day starts at the transition)
        auto const local = get_utc_epoch_ns(midnight);
        auto const offsetBefore = get_offset_ns_for(aTimeZone, local - i128{ nsPerDay });
        auto const candidate = local - i128{ offsetBefore };
        auto const transition = get_time_zone_transition(aTimeZone, candidate + i128{ 1 }, false);
        if (!transition)
            throw_range_error("no start of day");
        return *transition;
    }

    std::optional<i128> get_time_zone_transition(time_zone const& aTimeZone, i128 const& aEpochNs, bool aNext)
    {
        if (!aTimeZone.named || aTimeZone.zone == nullptr)
            return {};
        auto const* zone = aTimeZone.zone;
        i128 const billion{ 1000000000 };
        if (aNext)
        {
            auto s = floor_seconds(aEpochNs);
            for (int i = 0; i < 10000; ++i)
            {
                auto const p = period_at(zone, s);
                if (p.end == sNone)
                    return {};
                auto const t = i128{ p.end } * billion;
                if (t > ns_max_instant())
                    return {};
                if (period_at(zone, p.end).offset != p.offset)
                    return t;
                s = p.end;
            }
            return {};
        }
        // (the last whole second strictly before aEpochNs)
        auto s = floor_div(aEpochNs - i128{ 1 }, billion).to_int64();
        for (int i = 0; i < 10000; ++i)
        {
            auto const p = period_at(zone, s);
            if (p.begin == sNoneBefore)
                return {};
            auto const t = i128{ p.begin } * billion;
            if (t < -ns_max_instant())
                return {};
            if (period_at(zone, p.begin - 1).offset != p.offset)
            {
                if (t > ns_max_instant())
                {
                    s = p.begin - 1;
                    continue;
                }
                return t;
            }
            s = p.begin - 1;
        }
        return {};
    }

    i128 system_utc_epoch_ns()
    {
        using namespace std::chrono;
        auto const ns = duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
        return i128{ static_cast<std::int64_t>(ns) };
    }
}
