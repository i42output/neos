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
// <chrono> (std::chrono::get_tzdb, time_zone::get_info); only "UTC" if the library has no time zone database. A zone's offsets
// are those of its compiled (TZif, RFC 8536) file if the system has one (in TZDIR, else /usr/share/zoneinfo: the database as
// zic compiled it, which the C library uses too (some <chrono> libraries' readings of the source data err: e.g. in libstdc++
// 13, a rule's saving in effect at a zone line's change, and a line's UNTIL in local time)), else the library's.
//
// Identifiers are matched ASCII-case-insensitively; a Link's primary identifier is its target (and the zones that CLDR
// treats as UTC (Etc/UTC, Etc/GMT and their links) have the primary identifier "UTC"). Times beyond the range the
// library can answer for (it fails after the year 32767) are moved back by whole 400-year Gregorian cycles (146097 days:
// whole weeks, so the database's rules for the future repeat exactly).

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
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

        // (the zones the IANA database has made links (2024b): their targets are their primary identifiers, though a system's
        // database (one built with its backward-compatible data (backzone)) has them as zones)
        std::optional<std::string> legacy_zone_target(std::string const& aName)
        {
            static std::pair<char const*, char const*> const sLinks[] = { { "CET", "Europe/Brussels" }, { "CST6CDT", "America/Chicago" },
                { "EET", "Europe/Athens" }, { "EST", "America/Panama" }, { "EST5EDT", "America/New_York" }, { "HST", "Pacific/Honolulu" },
                { "MET", "Europe/Brussels" }, { "MST", "America/Phoenix" }, { "MST7MDT", "America/Denver" }, { "PST8PDT", "America/Los_Angeles" },
                { "WET", "Europe/Lisbon" } };
            for (auto const& [name, target] : sLinks)
                if (aName == name)
                    return target;
            return std::nullopt;
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
                        auto primary = is_utc_alias(std::string{ z.name() }) ? std::u16string{ u"UTC" } : name;
                        auto const* zone = &z;
                        if (auto const target = legacy_zone_target(std::string{ z.name() }))
                        {
                            primary = u16(*target);
                            zone = std::chrono::locate_zone(*target);
                        }
                        table.byLowercase[ascii_lowercase(name)] = { name, primary, zone };
                    }
                    for (auto const& l : db.links)
                    {
                        auto const name = u16(std::string{ l.name() });
                        auto const target = std::string{ l.target() };
                        auto const* zone = std::chrono::locate_zone(target);
                        auto primary = is_utc_alias(target) || is_utc_alias(std::string{ l.name() }) ? std::u16string{ u"UTC" } : u16(target);
                        if (auto const legacy = legacy_zone_target(target))
                        {
                            primary = u16(*legacy);
                            zone = std::chrono::locate_zone(*legacy);
                        }
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

        // --- compiled zone files (TZif)

        // (a POSIX TZ string's rule (its footer's): standard and daylight saving time, and the changes between them)
        struct posix_rule
        {
            struct change
            {
                char kind = 'M';            ///< ('J': Julian day 1-365 (no 29 February), 'D': day 0-365, 'M': month, week, weekday)
                int month = 0;
                int week = 0;
                int day = 0;
                std::int64_t time = 7200;   ///< (seconds of local time)
            };
            std::int64_t stdOffset = 0;     ///< (seconds east of UTC)
            std::int64_t dstOffset = 0;
            bool dst = false;
            change start;
            change end;
        };

        bool parse_posix_rule(std::string const& aText, posix_rule& aRule)
        {
            std::size_t i = 0u;
            auto const name = [&]() -> bool
            {
                if (i < aText.size() && aText[i] == '<')
                {
                    auto const close = aText.find('>', i);
                    if (close == std::string::npos)
                        return false;
                    i = close + 1u;
                    return true;
                }
                auto const begin = i;
                while (i < aText.size() && ((aText[i] >= 'A' && aText[i] <= 'Z') || (aText[i] >= 'a' && aText[i] <= 'z')))
                    ++i;
                return i - begin >= 3u;
            };
            auto const number = [&](std::int64_t& aValue) -> bool
            {
                auto const begin = i;
                aValue = 0;
                while (i < aText.size() && aText[i] >= '0' && aText[i] <= '9')
                    aValue = aValue * 10 + (aText[i++] - '0');
                return i > begin;
            };
            // ([+-]hh[:mm[:ss]]: seconds)
            auto const time = [&](std::int64_t& aSeconds) -> bool
            {
                std::int64_t sign = 1;
                if (i < aText.size() && (aText[i] == '+' || aText[i] == '-'))
                    sign = aText[i++] == '-' ? -1 : 1;
                std::int64_t h = 0, m = 0, sec = 0;
                if (!number(h))
                    return false;
                if (i < aText.size() && aText[i] == ':')
                {
                    ++i;
                    if (!number(m))
                        return false;
                    if (i < aText.size() && aText[i] == ':')
                    {
                        ++i;
                        if (!number(sec))
                            return false;
                    }
                }
                aSeconds = sign * (h * 3600 + m * 60 + sec);
                return true;
            };
            auto const change = [&](posix_rule::change& aChange) -> bool
            {
                std::int64_t a = 0, b = 0, c = 0;
                if (i < aText.size() && aText[i] == 'J')
                {
                    ++i;
                    aChange.kind = 'J';
                    if (!number(a))
                        return false;
                    aChange.day = static_cast<int>(a);
                }
                else if (i < aText.size() && aText[i] == 'M')
                {
                    ++i;
                    aChange.kind = 'M';
                    if (!number(a) || i >= aText.size() || aText[i++] != '.' || !number(b) || i >= aText.size() || aText[i++] != '.' || !number(c))
                        return false;
                    aChange.month = static_cast<int>(a);
                    aChange.week = static_cast<int>(b);
                    aChange.day = static_cast<int>(c);
                }
                else
                {
                    aChange.kind = 'D';
                    if (!number(a))
                        return false;
                    aChange.day = static_cast<int>(a);
                }
                aChange.time = 7200;
                if (i < aText.size() && aText[i] == '/')
                {
                    ++i;
                    if (!time(aChange.time))
                        return false;
                }
                return true;
            };
            std::int64_t offset = 0;
            if (!name() || !time(offset))
                return false;
            aRule.stdOffset = -offset;
            if (i == aText.size())
                return true;
            if (!name())
                return false;
            aRule.dst = true;
            aRule.dstOffset = aRule.stdOffset + 3600;
            if (i < aText.size() && aText[i] != ',')
            {
                if (!time(offset))
                    return false;
                aRule.dstOffset = -offset;
            }
            if (i >= aText.size() || aText[i++] != ',' || !change(aRule.start) || i >= aText.size() || aText[i++] != ',' || !change(aRule.end))
                return false;
            return i == aText.size();
        }

        // (the epoch seconds of a POSIX rule's change in a year (its time in local time of the offset in effect before it))
        std::int64_t posix_change_time(posix_rule::change const& aChange, std::int64_t aYear, std::int64_t aOffsetBefore)
        {
            std::int64_t day = 0;   // (epoch days)
            if (aChange.kind == 'J')
            {
                day = iso_date_to_epoch_days(aYear, 0, 1) + aChange.day - 1;
                if (is_leap_year(aYear) && aChange.day >= 60)
                    ++day;
            }
            else if (aChange.kind == 'D')
                day = iso_date_to_epoch_days(aYear, 0, 1) + aChange.day;
            else
            {
                auto const first = iso_date_to_epoch_days(aYear, aChange.month - 1, 1);
                auto const firstWeekday = ((first + 4) % 7 + 7) % 7;    // (0 Sunday: 1970-01-01 was a Thursday)
                auto monthDay = (aChange.day - firstWeekday + 7) % 7 + 1 + (aChange.week - 1) * 7;
                auto const length = iso_days_in_month(aYear, aChange.month);
                while (monthDay > length)
                    monthDay -= 7;
                day = first + monthDay - 1;
            }
            return day * 86400 + aChange.time - aOffsetBefore;
        }

        struct tzif_zone
        {
            std::vector<std::int64_t> times;        ///< (the transitions)
            std::vector<std::int32_t> offsets;      ///< (the offset from each transition)
            std::int32_t initialOffset = 0;         ///< (before the first)
            std::optional<posix_rule> rule;         ///< (after the last)
        public:
            // (the rule's changes in the years about a time: (time, offset after it)), sorted
            std::vector<std::pair<std::int64_t, std::int64_t>> rule_changes(std::int64_t aSeconds) const
            {
                std::vector<std::pair<std::int64_t, std::int64_t>> changes;
                auto const year = epoch_days_to_iso_date(std::clamp<std::int64_t>(aSeconds / 86400, -100000000, 100000000)).year;
                for (auto y = year - 1; y <= year + 1; ++y)
                {
                    changes.emplace_back(posix_change_time(rule->start, y, rule->stdOffset), rule->dstOffset);
                    changes.emplace_back(posix_change_time(rule->end, y, rule->dstOffset), rule->stdOffset);
                }
                std::sort(changes.begin(), changes.end());
                return changes;
            }
            period rule_period(std::int64_t aSeconds, std::int64_t aFloor) const
            {
                if (!rule->dst)
                    return { aFloor, sNone, rule->stdOffset };
                auto const changes = rule_changes(aSeconds);
                period p{ aFloor, sNone, 0 };
                // (the offset before the first change: the last change's of the previous years)
                p.offset = changes.front().second == rule->dstOffset ? rule->stdOffset : rule->dstOffset;
                for (auto const& [t, offset] : changes)
                {
                    if (t <= aSeconds)
                    {
                        if (t > p.begin)
                            p.begin = t;
                        p.offset = offset;
                    }
                    else
                    {
                        p.end = t;
                        break;
                    }
                }
                return p;
            }
            period at(std::int64_t aSeconds) const
            {
                if (times.empty() || aSeconds < times.front())
                {
                    if (times.empty() && rule)
                        return rule_period(aSeconds, sNoneBefore);
                    return { sNoneBefore, times.empty() ? sNone : times.front(), initialOffset };
                }
                auto const next = std::upper_bound(times.begin(), times.end(), aSeconds);
                auto const index = static_cast<std::size_t>(next - times.begin()) - 1u;
                if (next != times.end())
                    return { times[index], *next, offsets[index] };
                if (!rule)
                    return { times[index], sNone, offsets[index] };
                auto p = rule_period(aSeconds, times[index]);
                // (before the rule's first change after the last transition: the last transition's offset)
                if (p.begin == times[index])
                    p.offset = offsets[index];
                return p;
            }
        };

        std::unique_ptr<tzif_zone> read_tzif(std::string const& aName)
        {
            std::string directory = "/usr/share/zoneinfo";
            if (auto const* dir = std::getenv("TZDIR"); dir != nullptr && *dir != '\0')
                directory = dir;
            if (aName.empty() || aName.find("..") != std::string::npos)
                return nullptr;
            std::ifstream file{ directory + "/" + aName, std::ios::binary };
            if (!file)
                return nullptr;
            std::string data{ std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} };
            auto const be32 = [&](std::size_t aAt) -> std::int64_t
            {
                if (aAt + 4u > data.size())
                    throw std::out_of_range{ "tzif" };
                std::uint32_t v = 0u;
                for (std::size_t k = 0u; k < 4u; ++k)
                    v = (v << 8) | static_cast<std::uint8_t>(data[aAt + k]);
                return static_cast<std::int32_t>(v);
            };
            auto const be64 = [&](std::size_t aAt) -> std::int64_t
            {
                if (aAt + 8u > data.size())
                    throw std::out_of_range{ "tzif" };
                std::uint64_t v = 0u;
                for (std::size_t k = 0u; k < 8u; ++k)
                    v = (v << 8) | static_cast<std::uint8_t>(data[aAt + k]);
                return static_cast<std::int64_t>(v);
            };
            try
            {
                if (data.size() < 44u || data.compare(0u, 4u, "TZif") != 0 || data[4] < '2')
                    return nullptr;
                // (the version 1 block, skipped: then the version 2 header and 64-bit data)
                auto const skip = [&](std::size_t aAt, std::size_t aTimeSize) -> std::size_t
                {
                    auto const isutcnt = be32(aAt + 20u), isstdcnt = be32(aAt + 24u), leapcnt = be32(aAt + 28u), timecnt = be32(aAt + 32u),
                        typecnt = be32(aAt + 36u), charcnt = be32(aAt + 40u);
                    return aAt + 44u + static_cast<std::size_t>(timecnt) * (aTimeSize + 1u) + static_cast<std::size_t>(typecnt) * 6u +
                        static_cast<std::size_t>(charcnt) + static_cast<std::size_t>(leapcnt) * (aTimeSize + 4u) +
                        static_cast<std::size_t>(isstdcnt) + static_cast<std::size_t>(isutcnt);
                };
                auto const header = skip(0u, 4u);
                if (data.compare(header, 4u, "TZif") != 0)
                    return nullptr;
                auto const timecnt = static_cast<std::size_t>(be32(header + 32u));
                auto const typecnt = static_cast<std::size_t>(be32(header + 36u));
                auto zone = std::make_unique<tzif_zone>();
                auto at = header + 44u;
                for (std::size_t k = 0u; k < timecnt; ++k)
                    zone->times.push_back(be64(at + k * 8u));
                at += timecnt * 8u;
                std::vector<std::size_t> indices;
                for (std::size_t k = 0u; k < timecnt; ++k)
                    indices.push_back(static_cast<std::uint8_t>(data.at(at + k)));
                at += timecnt;
                std::vector<std::int32_t> typeOffsets;
                for (std::size_t k = 0u; k < typecnt; ++k)
                    typeOffsets.push_back(static_cast<std::int32_t>(be32(at + k * 6u)));
                if (typeOffsets.empty())
                    return nullptr;
                for (auto const index : indices)
                    zone->offsets.push_back(typeOffsets.at(index));
                zone->initialOffset = typeOffsets[0];
                // (the footer: the POSIX TZ string between newlines)
                auto const end = skip(header, 8u);
                if (end < data.size() && data[end] == '\n')
                {
                    auto const close = data.find('\n', end + 1u);
                    if (close != std::string::npos && close > end + 1u)
                    {
                        posix_rule rule;
                        if (parse_posix_rule(data.substr(end + 1u, close - end - 1u), rule))
                            zone->rule = rule;
                    }
                }
                return zone;
            }
            catch (...)
            {
                return nullptr;
            }
        }

        // (a library zone's compiled file (none: the library's offsets))
        tzif_zone const* compiled_zone(std::chrono::time_zone const* aZone)
        {
            static std::mutex sMutex;
            static std::map<std::chrono::time_zone const*, std::unique_ptr<tzif_zone>> sZones;
            std::lock_guard lock{ sMutex };
            auto existing = sZones.find(aZone);
            if (existing == sZones.end())
                existing = sZones.emplace(aZone, read_tzif(std::string{ aZone->name() })).first;
            return existing->second.get();
        }

        period period_at(std::chrono::time_zone const* aZone, std::int64_t aSeconds)
        {
            if (aZone == nullptr)
                return { sNoneBefore, sNone, 0 };
            if (auto const* compiled = compiled_zone(aZone))
                return compiled->at(aSeconds);
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

    std::optional<named_zone_record> get_available_named_time_zone(std::u16string_view aName)
    {
        auto const& table = zones().byLowercase;
        auto const it = table.find(ascii_lowercase(std::u16string{ aName }));
        if (it == table.end())
            return {};
        return it->second;
    }

    std::vector<std::u16string> available_primary_time_zones()
    {
        std::vector<std::u16string> result;
        for (auto const& [name, record] : zones().byLowercase)
            if (record.identifier == record.primary)
                result.push_back(record.identifier);
        std::sort(result.begin(), result.end());
        return result;
    }

    time_zone time_zone_from_identifier(std::u16string_view aIdentifier)
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
            // (no CheckISODaysRange (only an offset zone's date is checked): a local date may be a day beyond the instants' days)
            auto const days = iso_date_to_epoch_days(aDateTime.date.year, aDateTime.date.month - 1, aDateTime.date.day);
            if (days > 100000001 || days < -100000001)
                throw_range_error("date is outside the representable range");
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
