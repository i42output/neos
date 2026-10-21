/*
  temporal_parse.cpp

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

// Temporal: the ISO 8601 / RFC 9557 string grammar (date-times with UTC offsets, time zone and calendar annotations; time
// zone identifiers; UTC offsets; durations; month codes), parsed by recursive descent with backtracking between the
// alternatives of each goal symbol.

#include <cmath>
#include "temporal.hpp"

namespace neos::javascript_runtime::temporal
{
    namespace
    {
        struct annotation
        {
            std::u16string key;
            std::u16string value;
            bool critical = false;
        };

        struct captures
        {
            std::optional<std::int64_t> year;
            std::optional<int> month;
            std::optional<int> day;
            std::optional<int> hour;
            std::optional<int> minute;
            std::optional<int> second;
            std::u16string fraction;            ///< (the digits)
            bool z = false;
            std::optional<std::u16string> offset;
            std::optional<std::u16string> timeZone;
            std::vector<annotation> annotations;
        };

        class parser
        {
        public:
            explicit parser(std::u16string_view aText) : s{ aText } {}
        public:
            std::u16string_view s;
        public:
            bool at_end(std::size_t aPos) const { return aPos == s.size(); }
            bool is(std::size_t aPos, char16_t aCharacter) const { return aPos < s.size() && s[aPos] == aCharacter; }
            bool digit(std::size_t aPos) const { return aPos < s.size() && s[aPos] >= u'0' && s[aPos] <= u'9'; }
            int dv(std::size_t aPos) const { return s[aPos] - u'0'; }
            bool alpha(std::size_t aPos) const
            {
                return aPos < s.size() && ((s[aPos] >= u'a' && s[aPos] <= u'z') || (s[aPos] >= u'A' && s[aPos] <= u'Z'));
            }
            bool sign(std::size_t aPos) const { return is(aPos, u'+') || is(aPos, u'-'); }

            // DateYear
            bool date_year(std::size_t& aPos, captures& c) const
            {
                auto p = aPos;
                if (sign(p))
                {
                    bool const negative = s[p] == u'-';
                    ++p;
                    std::int64_t y = 0;
                    for (int i = 0; i < 6; ++i, ++p)
                    {
                        if (!digit(p))
                            return false;
                        y = y * 10 + dv(p);
                    }
                    if (negative && y == 0)
                        return false;
                    c.year = negative ? -y : y;
                }
                else
                {
                    std::int64_t y = 0;
                    for (int i = 0; i < 4; ++i, ++p)
                    {
                        if (!digit(p))
                            return false;
                        y = y * 10 + dv(p);
                    }
                    c.year = y;
                }
                aPos = p;
                return true;
            }

            bool two_digits(std::size_t aPos, int& aValue) const
            {
                if (!digit(aPos) || !digit(aPos + 1))
                    return false;
                aValue = dv(aPos) * 10 + dv(aPos + 1);
                return true;
            }

            bool date_month(std::size_t& aPos, captures& c) const
            {
                int m = 0;
                if (!two_digits(aPos, m) || m < 1 || m > 12)
                    return false;
                c.month = m;
                aPos += 2;
                return true;
            }

            bool date_day(std::size_t& aPos, captures& c) const
            {
                int d = 0;
                if (!two_digits(aPos, d) || d < 1 || d > 31)
                    return false;
                c.day = d;
                aPos += 2;
                return true;
            }

            static bool valid_month_day(int aMonth, int aDay)
            {
                if (aDay == 31 && (aMonth == 2 || aMonth == 4 || aMonth == 6 || aMonth == 9 || aMonth == 11))
                    return false;
                if (aMonth == 2 && aDay == 30)
                    return false;
                return true;
            }

            // Date (DateSpec[+Extended] or DateSpec[~Extended], with IsValidDate)
            bool date(std::size_t& aPos, captures& c) const
            {
                auto p = aPos;
                if (!date_year(p, c))
                    return false;
                bool const extended = is(p, u'-');
                if (extended)
                    ++p;
                if (!date_month(p, c))
                    return false;
                if (extended)
                {
                    if (!is(p, u'-'))
                        return false;
                    ++p;
                }
                if (!date_day(p, c))
                    return false;
                if (!valid_month_day(*c.month, *c.day))
                    return false;
                if (*c.month == 2 && *c.day == 29 && !is_leap_year(*c.year))
                    return false;
                aPos = p;
                return true;
            }

            bool hour(std::size_t aPos, int& aValue) const
            {
                return two_digits(aPos, aValue) && aValue <= 23;
            }

            bool minute_second(std::size_t aPos, int& aValue) const
            {
                return two_digits(aPos, aValue) && aValue <= 59;
            }

            bool fraction(std::size_t& aPos, std::u16string& aDigits) const
            {
                if (!is(aPos, u'.') && !is(aPos, u','))
                    return false;
                auto p = aPos + 1;
                std::u16string digits;
                while (digit(p) && digits.size() < 9u)
                    digits.push_back(s[p++]);
                if (digits.empty())
                    return false;
                aDigits = digits;
                aPos = p;
                return true;
            }

            // Time (TimeSpec[+Extended] or TimeSpec[~Extended])
            bool time(std::size_t& aPos, captures& c) const
            {
                auto p = aPos;
                int h = 0;
                if (!hour(p, h))
                    return false;
                p += 2;
                c.hour = h;
                c.minute.reset();
                c.second.reset();
                c.fraction.clear();
                bool extended = false;
                int m = 0;
                if (is(p, u':') && minute_second(p + 1, m))
                {
                    extended = true;
                    p += 3;
                }
                else if (minute_second(p, m))
                    p += 2;
                else
                {
                    aPos = p;
                    return true;
                }
                c.minute = m;
                int sec = 0;
                auto const secondAt = extended ? p + 1 : p;
                if ((!extended || is(p, u':')) && two_digits(secondAt, sec) && sec <= 60)
                {
                    p = secondAt + 2;
                    c.second = sec;
                    std::u16string digits;
                    if (fraction(p, digits))
                        c.fraction = digits;
                }
                aPos = p;
                return true;
            }

            // UTCOffset[SubMinutePrecision]: its source text (and whether it has seconds)
            bool utc_offset(std::size_t& aPos, bool aSubMinute, bool* aHasSeconds = nullptr) const
            {
                auto p = aPos;
                if (!sign(p))
                    return false;
                ++p;
                int h = 0;
                if (!hour(p, h))
                    return false;
                p += 2;
                int m = 0;
                bool extended = false;
                if (is(p, u':') && minute_second(p + 1, m))
                {
                    extended = true;
                    p += 3;
                }
                else if (minute_second(p, m))
                    p += 2;
                else
                {
                    aPos = p;
                    return true;
                }
                if (aSubMinute)
                {
                    auto const secondAt = extended ? p + 1 : p;
                    int sec = 0;
                    if ((!extended || is(p, u':')) && minute_second(secondAt, sec))
                    {
                        p = secondAt + 2;
                        std::u16string digits;
                        fraction(p, digits);
                        if (aHasSeconds != nullptr)
                            *aHasSeconds = true;
                    }
                }
                aPos = p;
                return true;
            }

            bool date_time_utc_offset(std::size_t& aPos, captures& c, bool aZ) const
            {
                if (aZ && (is(aPos, u'Z') || is(aPos, u'z')))
                {
                    c.z = true;
                    ++aPos;
                    return true;
                }
                auto p = aPos;
                if (!utc_offset(p, true))
                    return false;
                c.offset = std::u16string{ s.substr(aPos, p - aPos) };
                aPos = p;
                return true;
            }

            bool tz_leading(std::size_t aPos) const
            {
                return alpha(aPos) || is(aPos, u'.') || is(aPos, u'_');
            }

            bool tz_char(std::size_t aPos) const
            {
                return tz_leading(aPos) || digit(aPos) || is(aPos, u'-') || is(aPos, u'+');
            }

            bool iana_name(std::size_t& aPos) const
            {
                auto p = aPos;
                for (;;)
                {
                    if (!tz_leading(p))
                        return false;
                    ++p;
                    while (tz_char(p))
                        ++p;
                    if (is(p, u'/'))
                    {
                        ++p;
                        continue;
                    }
                    break;
                }
                aPos = p;
                return true;
            }

            // TimeZoneIdentifier (followed by aEnd)
            bool time_zone_identifier(std::size_t& aPos, char16_t aEnd) const
            {
                auto p = aPos;
                if (utc_offset(p, false) && (aEnd == 0 ? at_end(p) : is(p, aEnd)))
                {
                    aPos = p;
                    return true;
                }
                p = aPos;
                if (iana_name(p) && (aEnd == 0 ? at_end(p) : is(p, aEnd)))
                {
                    aPos = p;
                    return true;
                }
                return false;
            }

            bool time_zone_annotation(std::size_t& aPos, captures& c) const
            {
                auto p = aPos;
                if (!is(p, u'['))
                    return false;
                ++p;
                if (is(p, u'!'))
                    ++p;
                auto const start = p;
                if (!time_zone_identifier(p, u']'))
                    return false;
                c.timeZone = std::u16string{ s.substr(start, p - start) };
                aPos = p + 1;
                return true;
            }

            bool key_leading(std::size_t aPos) const
            {
                return (aPos < s.size() && s[aPos] >= u'a' && s[aPos] <= u'z') || is(aPos, u'_');
            }

            bool annotation_value(std::size_t& aPos) const
            {
                auto p = aPos;
                for (;;)
                {
                    if (!alpha(p) && !digit(p))
                        return false;
                    while (alpha(p) || digit(p))
                        ++p;
                    if (is(p, u'-') && (alpha(p + 1) || digit(p + 1)))
                    {
                        ++p;
                        continue;
                    }
                    break;
                }
                aPos = p;
                return true;
            }

            bool one_annotation(std::size_t& aPos, captures& c) const
            {
                auto p = aPos;
                if (!is(p, u'['))
                    return false;
                ++p;
                annotation a;
                if (is(p, u'!'))
                {
                    a.critical = true;
                    ++p;
                }
                if (!key_leading(p))
                    return false;
                auto const keyStart = p;
                ++p;
                while (key_leading(p) || digit(p) || is(p, u'-'))
                    ++p;
                a.key = std::u16string{ s.substr(keyStart, p - keyStart) };
                if (!is(p, u'='))
                    return false;
                ++p;
                auto const valueStart = p;
                if (!annotation_value(p))
                    return false;
                a.value = std::u16string{ s.substr(valueStart, p - valueStart) };
                if (!is(p, u']'))
                    return false;
                c.annotations.push_back(a);
                aPos = p + 1;
                return true;
            }

            // TimeZoneAnnotation? Annotations? (the time zone annotation required if aZoned), then the end
            bool annotations_to_end(std::size_t aPos, captures& c, bool aTimeZoneRequired) const
            {
                auto p = aPos;
                if (!time_zone_annotation(p, c) && aTimeZoneRequired)
                    return false;
                while (one_annotation(p, c))
                    ;
                return at_end(p);
            }

            // AnnotatedDateTime[Zoned, TimeRequired]
            bool annotated_date_time(captures& c, bool aZoned, bool aTimeRequired) const
            {
                std::size_t p = 0u;
                if (!date(p, c))
                    return false;
                if (is(p, u' ') || is(p, u'T') || is(p, u't'))
                {
                    ++p;
                    if (!time(p, c))
                        return false;
                    date_time_utc_offset(p, c, aZoned);
                }
                else if (aTimeRequired)
                    return false;
                return annotations_to_end(p, c, aZoned);
            }

            bool instant(captures& c) const
            {
                std::size_t p = 0u;
                if (!date(p, c))
                    return false;
                if (!(is(p, u' ') || is(p, u'T') || is(p, u't')))
                    return false;
                ++p;
                if (!time(p, c))
                    return false;
                if (!date_time_utc_offset(p, c, true))
                    return false;
                return annotations_to_end(p, c, false);
            }

            bool month_day_spec(std::size_t& aPos, captures& c) const
            {
                auto p = aPos;
                if (is(p, u'-') && is(p + 1, u'-'))
                    p += 2;
                if (!date_month(p, c))
                    return false;
                if (is(p, u'-'))
                    ++p;
                if (!date_day(p, c))
                    return false;
                if (!valid_month_day(*c.month, *c.day))
                    return false;
                aPos = p;
                return true;
            }

            bool year_month_spec(std::size_t& aPos, captures& c) const
            {
                auto p = aPos;
                if (!date_year(p, c))
                    return false;
                if (is(p, u'-'))
                    ++p;
                if (!date_month(p, c))
                    return false;
                aPos = p;
                return true;
            }

            bool annotated_time(captures& c) const
            {
                std::size_t p = 0u;
                bool const designator = is(p, u'T') || is(p, u't');
                if (designator)
                    ++p;
                auto const start = p;
                if (!time(p, c))
                    return false;
                date_time_utc_offset(p, c, false);
                if (!designator)
                {
                    // (early errors: the time and offset must not be a month-day or a year-month)
                    parser const sub{ s.substr(start, p - start) };
                    captures scratch;
                    std::size_t q = 0u;
                    if (sub.month_day_spec(q, scratch) && sub.at_end(q))
                        return false;
                    scratch = {};
                    q = 0u;
                    if (sub.year_month_spec(q, scratch) && sub.at_end(q))
                        return false;
                }
                return annotations_to_end(p, c, false);
            }
        };

        std::optional<iso_parse_result> try_parse(std::u16string const& aText, std::initializer_list<parse_goal> aGoals)
        {
            parser const ps{ aText };
            for (auto const goal : aGoals)
            {
                captures c;
                bool ok = false;
                bool dayPresent = true;
                bool yearPresent = true;
                switch (goal)
                {
                case GoalDateTimeZoned:
                    ok = ps.annotated_date_time(c, true, false);
                    break;
                case GoalDateTime:
                    ok = ps.annotated_date_time(c, false, false);
                    break;
                case GoalInstant:
                    ok = ps.instant(c);
                    break;
                case GoalTime:
                    ok = ps.annotated_time(c);
                    if (!ok)
                    {
                        c = {};
                        ok = ps.annotated_date_time(c, false, true);
                    }
                    break;
                case GoalMonthDay:
                {
                    std::size_t p = 0u;
                    ok = ps.month_day_spec(p, c) && ps.annotations_to_end(p, c, false);
                    if (ok)
                        yearPresent = false;
                    else
                    {
                        c = {};
                        ok = ps.annotated_date_time(c, false, false);
                    }
                    break;
                }
                case GoalYearMonth:
                {
                    std::size_t p = 0u;
                    ok = ps.year_month_spec(p, c) && ps.annotations_to_end(p, c, false);
                    if (ok)
                        dayPresent = false;
                    else
                    {
                        c = {};
                        ok = ps.annotated_date_time(c, false, false);
                    }
                    break;
                }
                }
                if (!ok)
                    continue;
                // (annotations)
                std::optional<std::u16string> calendar;
                bool calendarWasCritical = false;
                for (auto const& a : c.annotations)
                {
                    if (a.key == u"u-ca")
                    {
                        if (!calendar)
                        {
                            calendar = a.value;
                            calendarWasCritical = a.critical;
                        }
                        else if (a.critical || calendarWasCritical)
                            return {};
                    }
                    else if (a.critical)
                        return {};
                }
                if ((goal == GoalYearMonth && !dayPresent) || (goal == GoalMonthDay && !yearPresent))
                    if (calendar && ascii_lowercase(*calendar) != u"iso8601")
                        return {};
                iso_parse_result r;
                r.hasYear = yearPresent;
                r.year = c.year.value_or(0);
                r.month = c.month.value_or(1);
                r.day = c.day.value_or(1);
                if (c.hour)
                {
                    r.hasTime = true;
                    r.time.hour = *c.hour;
                    r.time.minute = c.minute.value_or(0);
                    r.time.second = c.second.value_or(0);
                    if (r.time.second == 60)
                        r.time.second = 59;
                    auto digits = c.fraction + u"000000000";
                    auto const part = [&](std::size_t aStart) { return (digits[aStart] - u'0') * 100 + (digits[aStart + 1] - u'0') * 10 + (digits[aStart + 2] - u'0'); };
                    r.time.millisecond = part(0u);
                    r.time.microsecond = part(3u);
                    r.time.nanosecond = part(6u);
                }
                r.z = c.z;
                r.offset = c.offset;
                r.timeZoneAnnotation = c.timeZone;
                r.calendar = calendar;
                return r;
            }
            return {};
        }
    }

    iso_parse_result parse_iso_date_time(std::u16string const& aText, std::initializer_list<parse_goal> aGoals)
    {
        auto const result = try_parse(aText, aGoals);
        if (!result)
            throw_range_error("invalid ISO 8601 string: " + utf8(make_string(aText)));
        return *result;
    }

    bool parse_time_zone_identifier(std::u16string const& aText, std::u16string& aName, int& aOffsetMinutes, bool& aIsOffset)
    {
        parser const ps{ aText };
        std::size_t p = 0u;
        if (ps.utc_offset(p, false) && ps.at_end(p))
        {
            auto const ns = parse_date_time_utc_offset(aText);
            aIsOffset = true;
            aOffsetMinutes = static_cast<int>(*ns / 60000000000LL);
            return true;
        }
        p = 0u;
        if (ps.iana_name(p) && ps.at_end(p))
        {
            aIsOffset = false;
            aName = aText;
            return true;
        }
        return false;
    }

    std::optional<std::int64_t> parse_date_time_utc_offset(std::u16string const& aText, bool* aSubMinute)
    {
        parser const ps{ aText };
        std::size_t p = 0u;
        bool hasSeconds = false;
        if (!ps.utc_offset(p, true, &hasSeconds) || !ps.at_end(p))
            return {};
        if (aSubMinute != nullptr)
            *aSubMinute = hasSeconds;
        std::int64_t const sign = aText[0] == u'-' ? -1 : 1;
        std::size_t i = 1u;
        auto const two = [&]() { auto const v = (aText[i] - u'0') * 10 + (aText[i + 1] - u'0'); i += 2; return static_cast<std::int64_t>(v); };
        auto const hours = two();
        std::int64_t minutes = 0;
        std::int64_t seconds = 0;
        std::int64_t nanoseconds = 0;
        if (i < aText.size())
        {
            if (aText[i] == u':')
                ++i;
            minutes = two();
            if (i < aText.size())
            {
                if (aText[i] == u':')
                    ++i;
                seconds = two();
                if (i < aText.size())
                {
                    ++i;
                    std::u16string digits = aText.substr(i) + u"000000000";
                    for (std::size_t k = 0u; k < 9u; ++k)
                        nanoseconds = nanoseconds * 10 + (digits[k] - u'0');
                }
            }
        }
        return sign * (((hours * 60 + minutes) * 60 + seconds) * 1000000000LL + nanoseconds);
    }

    duration_fields parse_duration_string(std::u16string const& aText)
    {
        auto const fail = [&]() { throw_range_error("invalid duration string: " + utf8(make_string(aText))); };
        parser const ps{ aText };
        std::size_t p = 0u;
        bool negative = false;
        if (ps.sign(p))
        {
            negative = aText[p] == u'-';
            ++p;
        }
        if (!ps.is(p, u'P') && !ps.is(p, u'p'))
            fail();
        ++p;
        duration_fields f;
        bool any = false;
        auto const digits = [&](std::u16string& aDigits) -> bool
        {
            aDigits.clear();
            while (ps.digit(p))
                aDigits.push_back(aText[p++]);
            return !aDigits.empty();
        };
        auto const number = [](std::u16string const& aDigits) { return string_to_number(aDigits); };
        // (date part: Y M W D in order)
        static char16_t const sDateDesignators[] = { u'Y', u'M', u'W', u'D' };
        int next = 0;
        while (ps.digit(p))
        {
            std::u16string d;
            digits(d);
            if (p >= aText.size())
                fail();
            auto const designator = static_cast<char16_t>(aText[p] >= u'a' ? aText[p] - (u'a' - u'A') : aText[p]);
            int index = -1;
            for (int k = next; k < 4; ++k)
                if (sDateDesignators[k] == designator)
                {
                    index = k;
                    break;
                }
            if (index < 0)
                fail();
            f.v[index] = number(d);
            next = index + 1;
            ++p;
            any = true;
        }
        if (ps.is(p, u'T') || ps.is(p, u't'))
        {
            ++p;
            static char16_t const sTimeDesignators[] = { u'H', u'M', u'S' };
            int nextTime = 0;
            bool anyTime = false;
            bool fractionUsed = false;
            std::int64_t fractionNs = 0;      // (the fraction of the last unit, in nanoseconds)
            int fractionUnit = -1;
            while (ps.digit(p))
            {
                if (fractionUsed)
                    fail();
                std::u16string d;
                digits(d);
                std::u16string fractionDigits;
                bool const hasFraction = ps.fraction(p, fractionDigits);
                if (p >= aText.size())
                    fail();
                auto const designator = static_cast<char16_t>(aText[p] >= u'a' ? aText[p] - (u'a' - u'A') : aText[p]);
                int index = -1;
                for (int k = nextTime; k < 3; ++k)
                    if (sTimeDesignators[k] == designator)
                    {
                        index = k;
                        break;
                    }
                if (index < 0)
                    fail();
                f.v[4 + index] = number(d);
                if (hasFraction)
                {
                    fractionUsed = true;
                    std::int64_t value = 0;
                    std::int64_t scale = 1;
                    for (auto const c : fractionDigits)
                    {
                        value = value * 10 + (c - u'0');
                        scale *= 10;
                    }
                    static std::int64_t const sUnitNs[] = { 3600000000000LL, 60000000000LL, 1000000000LL };
                    fractionNs = (i128{ value } * i128{ sUnitNs[index] } / i128{ scale }).to_int64();
                    fractionUnit = index;
                }
                nextTime = index + 1;
                ++p;
                anyTime = true;
            }
            if (!anyTime)
                fail();
            any = true;
            if (fractionUnit >= 0)
            {
                // (the fraction's nanoseconds go to the units below it)
                auto rem = fractionNs;
                if (fractionUnit == 0)
                {
                    f.v[5] = static_cast<double>(rem / 60000000000LL);
                    rem %= 60000000000LL;
                }
                if (fractionUnit <= 1)
                {
                    f.v[6] = static_cast<double>(rem / 1000000000LL);
                    rem %= 1000000000LL;
                }
                f.v[7] = static_cast<double>(rem / 1000000LL);
                rem %= 1000000LL;
                f.v[8] = static_cast<double>(rem / 1000LL);
                f.v[9] = static_cast<double>(rem % 1000LL);
            }
        }
        if (!any || p != aText.size())
            fail();
        if (negative)
            f = negate_duration(f);
        if (!is_valid_duration(f))
            throw_range_error("duration is out of range");
        return f;
    }

    bool parse_month_code(std::u16string const& aText, int& aMonth, bool& aLeap)
    {
        if (aText.size() < 3u || aText.size() > 4u || aText[0] != u'M')
            return false;
        if (aText[1] < u'0' || aText[1] > u'9' || aText[2] < u'0' || aText[2] > u'9')
            return false;
        aLeap = aText.size() == 4u;
        if (aLeap && aText[3] != u'L')
            return false;
        aMonth = (aText[1] - u'0') * 10 + (aText[2] - u'0');
        if (aMonth == 0 && !aLeap)
            return false;
        return true;
    }

    bool is_annotation_value(std::u16string const& aText)
    {
        parser const ps{ aText };
        std::size_t p = 0u;
        return ps.annotation_value(p) && ps.at_end(p);
    }

    std::u16string parse_temporal_calendar_string(std::u16string const& aText)
    {
        if (auto const result = try_parse(aText, { GoalDateTimeZoned, GoalDateTime, GoalInstant, GoalTime, GoalMonthDay, GoalYearMonth }))
            return result->calendar.value_or(u"iso8601");
        if (!is_annotation_value(aText))
            throw_range_error("invalid calendar: " + utf8(make_string(aText)));
        return aText;
    }
}
