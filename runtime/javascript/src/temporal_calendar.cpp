/*
  temporal_calendar.cpp

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

// Temporal's calendars: the ISO 8601 calendar and, with ICU (ECMA-402's Intl era and monthCode), the CLDR calendars (buddhist,
// chinese, coptic, dangi, ethioaa, ethiopic, gregory, hebrew, indian, islamic-civil, islamic-tbla, islamic-umalqura, japanese,
// persian and roc): calendar identifiers, Calendar Date Records (CalendarISOToDate), the resolution of calendar fields
// (CalendarResolveFields), calendar dates to ISO dates (CalendarDateToISO, CalendarMonthDayToISOReferenceDate) and calendar
// arithmetic (CalendarDateAdd, CalendarDateUntil).
//
// A non-ISO calendar's years are tables of their months (each month's code and length, and the year's first day) made once
// (the ISO-like calendars' (gregory, buddhist, roc, japanese) from the ISO calendar; the others' from ICU's calendars) and
// kept. The arithmetic of the specification (BalanceNonISODate, NonISODateSurpasses, NonISODateUntil) is done with them; its
// loops over years, months and days are computed directly (a candidate, then the specification's test of it).

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <unordered_map>
#include "temporal.hpp"

#if defined(NEOS_JS_ICU)
#include "intl_icu.hpp"
#endif

namespace neos::javascript_runtime::temporal
{
    namespace
    {
        struct calendar_name
        {
            char const* name;
            calendar_id id;
        };

        // (AvailableCalendars (aliases included), sorted)
        constexpr calendar_name sCalendars[] = {
            { "buddhist", calendar_id::Buddhist }, { "chinese", calendar_id::Chinese }, { "coptic", calendar_id::Coptic },
            { "dangi", calendar_id::Dangi }, { "ethioaa", calendar_id::Ethioaa }, { "ethiopic", calendar_id::Ethiopic },
            { "ethiopic-amete-alem", calendar_id::Ethioaa }, { "gregory", calendar_id::Gregory }, { "hebrew", calendar_id::Hebrew },
            { "indian", calendar_id::Indian }, { "islamic-civil", calendar_id::IslamicCivil }, { "islamic-tbla", calendar_id::IslamicTbla },
            { "islamic-umalqura", calendar_id::IslamicUmalqura }, { "islamicc", calendar_id::IslamicCivil }, { "iso8601", calendar_id::Iso8601 },
            { "japanese", calendar_id::Japanese }, { "persian", calendar_id::Persian }, { "roc", calendar_id::Roc } };

        constexpr char const* sCanonicalNames[] = { "iso8601", "buddhist", "chinese", "coptic", "dangi", "ethioaa", "ethiopic", "gregory",
            "hebrew", "indian", "islamic-civil", "islamic-tbla", "islamic-umalqura", "japanese", "persian", "roc" };
    }

    char const* calendar_identifier(calendar_id aCalendar)
    {
        return sCanonicalNames[static_cast<std::size_t>(aCalendar)];
    }

    std::optional<calendar_id> available_calendar(std::u16string_view aId)
    {
        auto const id = ascii_lowercase(std::u16string{ aId });
#if defined(NEOS_JS_ICU)
        if (intl::icu_available())
        {
            for (auto const& c : sCalendars)
                if (id == u16(c.name))
                    return c.id;
            return std::nullopt;
        }
#endif
        if (id == u"iso8601")
            return calendar_id::Iso8601;
        return std::nullopt;
    }

    value calendar_value(calendar_id aCalendar)
    {
        return ascii(calendar_identifier(aCalendar));
    }

    // --- ISO 8601

    namespace
    {
        calendar_date iso_calendar_date(iso_date const& aDate)
        {
            calendar_date d;
            d.year = aDate.year;
            d.month = aDate.month;
            d.monthNumber = aDate.month;
            d.day = aDate.day;
            d.dayOfWeek = iso_day_of_week(aDate);
            d.dayOfYear = iso_day_of_year(aDate);
            auto const week = iso_week_of_year(aDate);
            d.weekOfYear = week.first;
            d.yearOfWeek = week.second;
            d.daysInMonth = iso_days_in_month(aDate.year, aDate.month);
            d.daysInYear = is_leap_year(aDate.year) ? 366 : 365;
            d.monthsInYear = 12;
            d.inLeapYear = is_leap_year(aDate.year);
            return d;
        }
    }

    std::string calendar_date::month_code() const
    {
        return "M" + zero_pad(monthNumber, 2) + (leapMonth ? "L" : "");
    }

#if defined(NEOS_JS_ICU)

    // --- the non-ISO calendars' years

    namespace
    {
        using intl::calendar_ptr;

        struct month_info
        {
            int number;         ///< (its month code's number)
            bool leap;          ///< (its month code's L)
            int length;
        };

        struct year_info
        {
            std::int64_t start = 0;     ///< (its first day (epoch days))
            std::vector<month_info> months;
            int length = 0;
        };

        bool is_lunisolar(calendar_id aCalendar)
        {
            return aCalendar == calendar_id::Chinese || aCalendar == calendar_id::Dangi || aCalendar == calendar_id::Hebrew;
        }

        bool is_iso_like(calendar_id aCalendar)
        {
            return aCalendar == calendar_id::Gregory || aCalendar == calendar_id::Buddhist || aCalendar == calendar_id::Roc ||
                aCalendar == calendar_id::Japanese;
        }

        // (an ISO-like calendar's year: the ISO year's (offset))
        std::int64_t iso_year_offset(calendar_id aCalendar)
        {
            switch (aCalendar)
            {
            case calendar_id::Buddhist: return -543;
            case calendar_id::Roc: return 1911;
            default: return 0;
            }
        }

        // (ICU's extended year of an arithmetic year: the year's offset)
        std::int64_t icu_year_offset(calendar_id aCalendar)
        {
            switch (aCalendar)
            {
            case calendar_id::Chinese: return 2637;
            case calendar_id::Dangi: return 2333;
            case calendar_id::Ethioaa: return -5500;
            default: return 0;
            }
        }

        char const* icu_calendar_type(calendar_id aCalendar)
        {
            switch (aCalendar)
            {
            case calendar_id::Chinese: return "chinese";
            case calendar_id::Dangi: return "dangi";
            case calendar_id::Coptic: return "coptic";
            case calendar_id::Ethioaa: return "ethiopic-amete-alem";
            case calendar_id::Ethiopic: return "ethiopic";
            case calendar_id::Hebrew: return "hebrew";
            case calendar_id::Indian: return "indian";
            case calendar_id::IslamicCivil: return "islamic-civil";
            case calendar_id::IslamicTbla: return "islamic-tbla";
            case calendar_id::IslamicUmalqura: return "islamic-umalqura";
            case calendar_id::Persian: return "persian";
            default: return "gregorian";
            }
        }

        // (the approximate ISO year of a calendar year: years beyond Temporal's limits are not computed)
        double approximate_iso_year(calendar_id aCalendar, double aYear)
        {
            switch (aCalendar)
            {
            case calendar_id::Buddhist: return aYear - 543.0;
            case calendar_id::Roc: return aYear + 1911.0;
            case calendar_id::Coptic: return aYear + 284.0;
            case calendar_id::Ethiopic: return aYear + 8.0;
            case calendar_id::Ethioaa: return aYear - 5492.0;
            case calendar_id::Hebrew: return aYear - 3760.0;
            case calendar_id::Indian: return aYear + 78.0;
            case calendar_id::Persian: return aYear + 621.0;
            case calendar_id::IslamicCivil: case calendar_id::IslamicTbla: case calendar_id::IslamicUmalqura: return aYear * 0.970224 + 621.6;
            default: return aYear;
            }
        }

        void check_year_range(calendar_id aCalendar, double aYear)
        {
            auto const iso = approximate_iso_year(aCalendar, aYear);
            if (!(iso >= -271823.0 && iso <= 275762.0))
                throw_range_error("date is outside the representable range");
        }

        // (the Chinese and Korean calendars' years from 1900 to 2102 (as published by the Purple Mountain Observatory, the Hong Kong
        // Observatory and the Korea Astronomy and Space Science Institute; from ICU4X's data (Unicode License V3)): each year's months'
        // lengths (bit n: the ordinal month n + 1 has 30 days, else 29), its leap month's ordinal (bits 13 to 16: 0 none) and its new
        // year's day (bits 17 to 22: days after 1 January); other years are ICU's)
        constexpr std::int64_t sEastAsianFirstYear = 1900;
        constexpr std::uint32_t sChineseYears[] = {
            0x03d36d2, 0x0620752, 0x04c0ea5, 0x038d64a, 0x05c064b, 0x0440a9b, 0x030b556, 0x056056a, 0x0400b59, 0x02a7752,
            0x0500752, 0x03afb25, 0x0600b25, 0x0480a4b, 0x032d4ab, 0x05802ad, 0x042056b, 0x02c6b69, 0x0520da9, 0x03f1d92,
            0x0640e92, 0x04c0d25, 0x036da4d, 0x05c0a56, 0x04602b6, 0x02eb5b5, 0x05606d4, 0x0400ea9, 0x02c7e92, 0x0500e92,
            0x03aed26, 0x05e052b, 0x0480a57, 0x032d2b6, 0x0580b5a, 0x04406d4, 0x02e8ec9, 0x0520749, 0x03d1693, 0x0620a93,
            0x04c052b, 0x034ea5b, 0x05a0aad, 0x046056a, 0x030bb55, 0x0560ba4, 0x0400b49, 0x02a7a93, 0x0500a95, 0x039152d,
            0x05e0536, 0x0480aad, 0x034d5aa, 0x05805b2, 0x0420da5, 0x02e9d4a, 0x0540d4a, 0x03d2a95, 0x0600a97, 0x04c0556,
            0x036eab5, 0x05a0ad5, 0x04606d2, 0x030aea5, 0x0560ea5, 0x040064a, 0x0288c97, 0x04e0a9b, 0x03b155a, 0x05e056a,
            0x0480b69, 0x034d752, 0x05a0b52, 0x0420b25, 0x02cb64b, 0x0520a4b, 0x03d34ab, 0x06002ad, 0x04a056d, 0x036eb69,
            0x05c0da9, 0x0460d92, 0x030bd25, 0x0560d25, 0x0417a4d, 0x0640a56, 0x04e02b6, 0x038e5b5, 0x05e06d5, 0x0480ea9,
            0x034de92, 0x05a0e92, 0x0440d26, 0x02c8a56, 0x0500a57, 0x03d34d6, 0x062035a, 0x04a06d5, 0x036d6c9, 0x05c0749,
            0x0460693, 0x02eb52b, 0x054052b, 0x03e0a5b, 0x02a755a, 0x04e056a, 0x0391b55, 0x0600ba4, 0x04a0b49, 0x032da93,
            0x0580a95, 0x042052d, 0x02caaad, 0x0500ab5, 0x03d55aa, 0x06205d2, 0x04c0da5, 0x036fd4a, 0x05c0d4a, 0x0460c95,
            0x030b52e, 0x0540556, 0x03e0ab5, 0x02a75b2, 0x05006d2, 0x038eea5, 0x05e0725, 0x048064b, 0x032cc97, 0x0560cab,
            0x042055a, 0x02c8ad6, 0x0520b69, 0x03d9752, 0x0620b52, 0x04c0b25, 0x036fa4b, 0x05a0a4b, 0x04404ab, 0x02ec55b,
            0x05405ad, 0x03e0b6a, 0x02a7b52, 0x0500d92, 0x03b1d25, 0x05e0d25, 0x0480a55, 0x032d4ad, 0x05804b6, 0x04005b5,
            0x02c8daa, 0x0520ec9, 0x03f3e92, 0x0620e92, 0x04c0d26, 0x036ea56, 0x05a0a57, 0x0440556, 0x02ea6d5, 0x0540755,
            0x0400749, 0x0288e93, 0x04e0693, 0x039152b, 0x05e052b, 0x0460a5b, 0x032d55a, 0x058056a, 0x0420b65, 0x02cb74a,
            0x0520b4a, 0x03d3a95, 0x0620a95, 0x04a052d, 0x034eaad, 0x05a0ab5, 0x04605aa, 0x02eaba5, 0x0540da5, 0x0400d4a,
            0x02a9c95, 0x04e0c96, 0x039194e, 0x05e0556, 0x0480ab5, 0x032d5b2, 0x05806d2, 0x0420ea5, 0x02eae4a, 0x050068b,
            0x03b2c97, 0x06004ab, 0x04a055b, 0x034ead6, 0x05a0b6a, 0x0460752, 0x030b725, 0x0540b45, 0x03e0a8b, 0x028749b,
            0x04e04ab, 0x039095b, 0x05e05ad,
        };
        constexpr std::uint32_t sDangiYears[] = {
            0x03d36d2, 0x0620752, 0x04c0ea5, 0x038d64a, 0x05c064b, 0x0440a9b, 0x030b556, 0x056056a, 0x0400b59, 0x02a7752,
            0x0500752, 0x03afb25, 0x0600b25, 0x0480a4b, 0x032d29b, 0x0580aad, 0x044056a, 0x02c6b69, 0x0520ba9, 0x03f1b52,
            0x0640d92, 0x04c0d25, 0x036da4d, 0x05c0956, 0x04602b5, 0x02eb5ad, 0x05606d4, 0x0400da9, 0x02c7d92, 0x0500e92,
            0x03aed26, 0x05e0527, 0x0480a57, 0x032d2b6, 0x0580ada, 0x04406d4, 0x02e8ea9, 0x0520749, 0x03d1693, 0x0620a93,
            0x04c052b, 0x034ea5b, 0x05a096d, 0x0460b6a, 0x032bb54, 0x0560ba4, 0x0400b49, 0x02a7a93, 0x0500a95, 0x039152b,
            0x05e052d, 0x0480aad, 0x034d56a, 0x0580db2, 0x0440da4, 0x02e9d49, 0x0540d4a, 0x03d3a95, 0x0620a96, 0x04c0556,
            0x036eab5, 0x05a0ad5, 0x04606d2, 0x030aea5, 0x0560ea5, 0x0400e4a, 0x02a8c96, 0x04e0a9b, 0x03b1556, 0x05e056a,
            0x0480b59, 0x034d752, 0x05a0752, 0x0420725, 0x02cb64b, 0x0520a4b, 0x03d32ab, 0x06002ad, 0x04a056b, 0x036eb69,
            0x05c0da9, 0x0460d92, 0x030bb25, 0x0560d25, 0x0417a4d, 0x0640a56, 0x04e02b6, 0x038f5ad, 0x06006d4, 0x0480da9,
            0x034dd92, 0x05a0e92, 0x0440d26, 0x02c8a56, 0x0500a57, 0x03d32b6, 0x0620b5a, 0x04c06d4, 0x036cec9, 0x05c0749,
            0x0460693, 0x02eb527, 0x054052b, 0x03e0a5b, 0x02a755a, 0x04e036a, 0x0391b55, 0x0600ba4, 0x04a0b49, 0x032da93,
            0x0580a95, 0x042052d, 0x02c8a5d, 0x0500aad, 0x03d55aa, 0x06205d2, 0x04c0da5, 0x036dd4a, 0x05c0d4a, 0x0460a95,
            0x030b52d, 0x0540556, 0x03e0ab5, 0x02a75aa, 0x05006d2, 0x038eea5, 0x05e0ea5, 0x04a0e4a, 0x034cc96, 0x0560c9b,
            0x042055a, 0x02c8ad5, 0x0520b69, 0x03d9752, 0x0620752, 0x04c0b25, 0x036f64b, 0x05a0a4b, 0x04404ab, 0x02ec55b,
            0x054056d, 0x03e0b69, 0x02a7b52, 0x0500d92, 0x03b1d25, 0x05e0d25, 0x0480a4d, 0x032d4ad, 0x05802b6, 0x04005b5,
            0x02c8da9, 0x0520ea9, 0x03f3d92, 0x0620e92, 0x04c0d26, 0x036ea56, 0x05a0a57, 0x04404d6, 0x02ea6b5, 0x05406d5,
            0x0400ec9, 0x02a8e92, 0x04e0693, 0x039152b, 0x05e052b, 0x0460a5b, 0x032d55a, 0x058056a, 0x0420b55, 0x02cb749,
            0x0520b49, 0x03d3a93, 0x0620a95, 0x04a052d, 0x034eaad, 0x05a0ab5, 0x04605aa, 0x02eaba5, 0x0540da5, 0x0400d4a,
            0x02a9a95, 0x04e0c95, 0x039152e, 0x05e0556, 0x0480ab5, 0x032d5b2, 0x05806d2, 0x0420ea5, 0x02ebe4a, 0x052064a,
            0x03b2c97, 0x0600cab, 0x04c055a, 0x034ead5, 0x05a0b69, 0x0460752, 0x030b6a5, 0x0540b25, 0x03e064b, 0x0289497,
            0x04e04ab, 0x039055b, 0x05e05ad,
        };

        std::optional<std::uint32_t> east_asian_year(calendar_id aCalendar, std::int64_t aYear)
        {
            auto const index = aYear - sEastAsianFirstYear;
            if (index < 0 || index >= static_cast<std::int64_t>(std::size(sChineseYears)))
                return std::nullopt;
            return (aCalendar == calendar_id::Chinese ? sChineseYears : sDangiYears)[index];
        }

        // (the Hebrew calendar (arithmetic: the molad and the postponements; ICU's calculation errs in some years)): its year's
        // first day (epoch days)
        std::int64_t hebrew_elapsed_days(std::int64_t aYear)
        {
            auto const floor_div = [](std::int64_t a, std::int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
            auto const months = floor_div(235 * aYear - 234, 19);
            auto const parts = 12084 + 13753 * months;
            auto day = months * 29 + floor_div(parts, 25920);
            if (((3 * (day + 1)) % 7 + 7) % 7 < 3)
                ++day;
            return day;
        }

        std::int64_t hebrew_new_year(std::int64_t aYear)
        {
            constexpr std::int64_t epoch = -1373427 - 719163;   // (1 Tishri AM 1 (R.D. -1373427), as epoch days)
            auto const ny0 = hebrew_elapsed_days(aYear - 1);
            auto const ny1 = hebrew_elapsed_days(aYear);
            auto const ny2 = hebrew_elapsed_days(aYear + 1);
            int correction = 0;
            if (ny2 - ny1 == 356)
                correction = 2;
            else if (ny1 - ny0 == 382)
                correction = 1;
            return epoch + ny1 + correction;
        }

        bool hebrew_leap_year(std::int64_t aYear)
        {
            return (((7 * aYear + 1) % 19) + 19) % 19 < 7;
        }

        // (the Indian national calendar: Chaitra 1 is 22 March (21 March in a leap year) of the year 78 years later)
        std::int64_t indian_new_year(std::int64_t aYear)
        {
            auto const isoYear = aYear + 78;
            return iso_date_to_epoch_days(isoYear, 2, is_leap_year(isoYear) ? 21 : 22);
        }

        class calendar_data
        {
        public:
            year_info const& year(calendar_id aCalendar, std::int64_t aYear)
            {
                std::lock_guard lock{ iMutex };
                auto const key = (static_cast<std::uint64_t>(aCalendar) << 48) ^ static_cast<std::uint64_t>(aYear + 0x800000000000LL);
                auto existing = iYears.find(key);
                if (existing != iYears.end())
                    return existing->second;
                return iYears.emplace(key, make_year(aCalendar, aYear)).first->second;
            }
            // (the calendar year of an epoch day)
            std::int64_t year_of(calendar_id aCalendar, std::int64_t aEpochDay)
            {
                if (is_iso_like(aCalendar))
                    return epoch_days_to_iso_date(aEpochDay).year - iso_year_offset(aCalendar);
                if (aCalendar == calendar_id::Indian)
                {
                    auto const year = epoch_days_to_iso_date(aEpochDay).year - 78;
                    return aEpochDay < indian_new_year(year) ? year - 1 : year;
                }
                if (aCalendar == calendar_id::Hebrew)
                {
                    auto year = static_cast<std::int64_t>(std::floor(static_cast<double>(aEpochDay + 1373427 + 719163) / 365.2468)) + 1;
                    while (aEpochDay < hebrew_new_year(year))
                        --year;
                    while (aEpochDay >= hebrew_new_year(year + 1))
                        ++year;
                    return year;
                }
                std::lock_guard lock{ iMutex };
                auto* c = icu_calendar(aCalendar);
                UErrorCode status = U_ZERO_ERROR;
                ucal_setMillis(c, static_cast<double>(aEpochDay) * 86400000.0, &status);
                return static_cast<std::int64_t>(ucal_get(c, UCAL_EXTENDED_YEAR, &status)) - icu_year_offset(aCalendar);
            }
        private:
            UCalendar* icu_calendar(calendar_id aCalendar)
            {
                auto& c = iCalendars[static_cast<std::size_t>(aCalendar)];
                if (!c)
                {
                    UErrorCode status = U_ZERO_ERROR;
                    auto const locale = std::string{ "en@calendar=" } + icu_calendar_type(aCalendar);
                    c.reset(ucal_open(intl::uchars(u"UTC"), 3, locale.c_str(), UCAL_DEFAULT, &status));
                    if (U_FAILURE(status))
                        throw_range_error("the calendar is not available");
                }
                return c.get();
            }
            year_info make_year(calendar_id aCalendar, std::int64_t aYear)
            {
                year_info y;
                if (is_iso_like(aCalendar))
                {
                    auto const isoYear = aYear + iso_year_offset(aCalendar);
                    y.start = iso_date_to_epoch_days(isoYear, 0, 1);
                    for (int m = 1; m <= 12; ++m)
                        y.months.push_back({ m, false, iso_days_in_month(isoYear, m) });
                    y.length = is_leap_year(isoYear) ? 366 : 365;
                    return y;
                }
                if (aCalendar == calendar_id::Indian)
                {
                    bool const leap = is_leap_year(aYear + 78);
                    y.start = indian_new_year(aYear);
                    for (int m = 1; m <= 12; ++m)
                        y.months.push_back({ m, false, m == 1 ? (leap ? 31 : 30) : m <= 6 ? 31 : 30 });
                    y.length = leap ? 366 : 365;
                    return y;
                }
                if (auto const data = (aCalendar == calendar_id::Chinese || aCalendar == calendar_id::Dangi) ? east_asian_year(aCalendar, aYear) :
                    std::nullopt)
                {
                    int const leap = static_cast<int>((*data >> 13) & 0xFu);
                    y.start = iso_date_to_epoch_days(aYear, 0, 1) + static_cast<std::int64_t>((*data >> 17) & 0x3Fu);
                    int const count = leap != 0 ? 13 : 12;
                    for (int i = 0; i < count; ++i)
                    {
                        int const length = ((*data >> i) & 1u) != 0u ? 30 : 29;
                        int const ordinal = i + 1;
                        if (leap == 0 || ordinal < leap)
                            y.months.push_back({ ordinal, false, length });
                        else if (ordinal == leap)
                            y.months.push_back({ leap - 1, true, length });
                        else
                            y.months.push_back({ ordinal - 1, false, length });
                        y.length += length;
                    }
                    return y;
                }
                if (aCalendar == calendar_id::Hebrew)
                {
                    y.start = hebrew_new_year(aYear);
                    y.length = static_cast<int>(hebrew_new_year(aYear + 1) - y.start);
                    bool const longHeshvan = y.length % 10 == 5;
                    bool const shortKislev = y.length % 10 == 3;
                    y.months = { { 1, false, 30 }, { 2, false, longHeshvan ? 30 : 29 }, { 3, false, shortKislev ? 29 : 30 }, { 4, false, 29 },
                        { 5, false, 30 } };
                    if (hebrew_leap_year(aYear))
                        y.months.push_back({ 5, true, 30 });
                    for (auto const& m : { month_info{ 6, false, 29 }, month_info{ 7, false, 30 }, month_info{ 8, false, 29 }, month_info{ 9, false, 30 },
                        month_info{ 10, false, 29 }, month_info{ 11, false, 30 }, month_info{ 12, false, 29 } })
                        y.months.push_back(m);
                    return y;
                }
                auto* c = icu_calendar(aCalendar);
                auto const extended = aYear + icu_year_offset(aCalendar);
                UErrorCode status = U_ZERO_ERROR;
                ucal_clear(c);
                ucal_set(c, UCAL_EXTENDED_YEAR, static_cast<std::int32_t>(extended));
                ucal_set(c, UCAL_MONTH, 0);
                if (aCalendar == calendar_id::Chinese || aCalendar == calendar_id::Dangi)
                    ucal_set(c, UCAL_IS_LEAP_MONTH, 0);
                ucal_set(c, UCAL_DATE, 1);
                auto const startMs = ucal_getMillis(c, &status);
                y.start = static_cast<std::int64_t>(std::floor(startMs / 86400000.0));
                auto day = y.start;
                for (int guard = 0; guard < 14; ++guard)
                {
                    ucal_setMillis(c, static_cast<double>(day) * 86400000.0, &status);
                    if (ucal_get(c, UCAL_EXTENDED_YEAR, &status) != extended)
                        break;
                    auto const m = ucal_get(c, UCAL_MONTH, &status);
                    auto const leap = (aCalendar == calendar_id::Chinese || aCalendar == calendar_id::Dangi) && ucal_get(c, UCAL_IS_LEAP_MONTH, &status) != 0;
                    auto const length = ucal_getLimit(c, UCAL_DATE, UCAL_ACTUAL_MAXIMUM, &status);
                    month_info info{ m + 1, leap, length };
                    if (aCalendar == calendar_id::Hebrew)
                    {
                        // (ICU's Hebrew months: Tishri 0 ... Shevat 4, Adar I 5, Adar (II) 6 ... Elul 12)
                        if (m == 5)
                            info = { 5, true, length };
                        else if (m > 5)
                            info = { m, false, length };
                    }
                    y.months.push_back(info);
                    day += length;
                }
                y.length = static_cast<int>(day - y.start);
                if (U_FAILURE(status) || y.months.empty())
                    throw_range_error("the calendar cannot compute the date");
                return y;
            }
        private:
            std::mutex iMutex;
            std::unordered_map<std::uint64_t, year_info> iYears;     ///< (never cleared: its elements are referred to (and stay put))
            calendar_ptr iCalendars[static_cast<std::size_t>(calendar_id::Count)];
        };

        calendar_data& the_calendar_data()
        {
            static calendar_data sData;
            return sData;
        }

        year_info const& year_data(calendar_id aCalendar, std::int64_t aYear)
        {
            check_year_range(aCalendar, static_cast<double>(aYear));
            return the_calendar_data().year(aCalendar, aYear);
        }

        int months_in_year(calendar_id aCalendar, std::int64_t aYear)
        {
            return static_cast<int>(year_data(aCalendar, aYear).months.size());
        }

        int days_in_month(calendar_id aCalendar, std::int64_t aYear, int aMonth)
        {
            return year_data(aCalendar, aYear).months[static_cast<std::size_t>(aMonth - 1)].length;
        }

        // (the first day of a year's month (ordinal: in range))
        std::int64_t month_start(calendar_id aCalendar, std::int64_t aYear, int aMonth)
        {
            auto const& y = year_data(aCalendar, aYear);
            auto day = y.start;
            for (int m = 1; m < aMonth; ++m)
                day += y.months[static_cast<std::size_t>(m - 1)].length;
            return day;
        }

        struct ymd
        {
            std::int64_t year;
            int month;
            int day;
        };

        ymd date_of_epoch_day(calendar_id aCalendar, std::int64_t aDay)
        {
            auto year = the_calendar_data().year_of(aCalendar, aDay);
            auto const* y = &year_data(aCalendar, year);
            // (ICU's year and the table's agree, but be safe at the years' bounds)
            while (aDay < y->start)
                y = &year_data(aCalendar, --year);
            while (aDay >= y->start + y->length)
                y = &year_data(aCalendar, ++year);
            auto offset = aDay - y->start;
            int month = 1;
            for (auto const& m : y->months)
            {
                if (offset < m.length)
                    break;
                offset -= m.length;
                ++month;
            }
            return { year, month, static_cast<int>(offset) + 1 };
        }

        // --- eras

        enum class era_kind { Epoch, Negative, Offset };

        struct era_row
        {
            calendar_id calendar;
            char const* era;
            char const* alias;
            era_kind kind;
            std::int64_t offset;
        };

        constexpr era_row sEras[] = {
            { calendar_id::Buddhist, "be", nullptr, era_kind::Epoch, 0 },
            { calendar_id::Coptic, "am", nullptr, era_kind::Epoch, 0 },
            { calendar_id::Ethioaa, "aa", nullptr, era_kind::Epoch, 0 },
            { calendar_id::Ethiopic, "am", nullptr, era_kind::Epoch, 0 },
            { calendar_id::Ethiopic, "aa", nullptr, era_kind::Offset, -5499 },
            { calendar_id::Gregory, "ce", "ad", era_kind::Epoch, 0 },
            { calendar_id::Gregory, "bce", "bc", era_kind::Negative, 0 },
            { calendar_id::Hebrew, "am", nullptr, era_kind::Epoch, 0 },
            { calendar_id::Indian, "shaka", nullptr, era_kind::Epoch, 0 },
            { calendar_id::IslamicCivil, "ah", nullptr, era_kind::Epoch, 0 },
            { calendar_id::IslamicCivil, "bh", nullptr, era_kind::Negative, 0 },
            { calendar_id::IslamicTbla, "ah", nullptr, era_kind::Epoch, 0 },
            { calendar_id::IslamicTbla, "bh", nullptr, era_kind::Negative, 0 },
            { calendar_id::IslamicUmalqura, "ah", nullptr, era_kind::Epoch, 0 },
            { calendar_id::IslamicUmalqura, "bh", nullptr, era_kind::Negative, 0 },
            { calendar_id::Japanese, "reiwa", nullptr, era_kind::Offset, 2019 },
            { calendar_id::Japanese, "heisei", nullptr, era_kind::Offset, 1989 },
            { calendar_id::Japanese, "showa", nullptr, era_kind::Offset, 1926 },
            { calendar_id::Japanese, "taisho", nullptr, era_kind::Offset, 1912 },
            { calendar_id::Japanese, "meiji", nullptr, era_kind::Offset, 1868 },
            { calendar_id::Japanese, "ce", "ad", era_kind::Epoch, 0 },
            { calendar_id::Japanese, "bce", "bc", era_kind::Negative, 0 },
            { calendar_id::Persian, "ap", nullptr, era_kind::Epoch, 0 },
            { calendar_id::Roc, "roc", nullptr, era_kind::Epoch, 0 },
            { calendar_id::Roc, "broc", nullptr, era_kind::Negative, 0 } };

        // (CanonicalizeEraInCalendar: none if the calendar has no such era)
        era_row const* canonical_era(calendar_id aCalendar, std::u16string const& aEra)
        {
            for (auto const& row : sEras)
                if (row.calendar == aCalendar && (aEra == u16(row.era) || (row.alias != nullptr && aEra == u16(row.alias))))
                    return &row;
            return nullptr;
        }

        // (CalendarDateArithmeticYearForEraYear)
        double arithmetic_year_for_era_year(era_row const& aEra, double aEraYear)
        {
            switch (aEra.kind)
            {
            case era_kind::Epoch: return aEraYear;
            case era_kind::Negative: return 1.0 - aEraYear;
            default: return static_cast<double>(aEra.offset) + aEraYear - 1.0;
            }
        }

        // (CalendarDateEra and CalendarDateEraYear)
        void date_era(calendar_id aCalendar, iso_date const& aIsoDate, std::int64_t aYear, calendar_date& aDate)
        {
            auto const set = [&](char const* aEra, std::int64_t aEraYear)
            {
                aDate.era = aEra;
                aDate.eraYear = aEraYear;
            };
            switch (aCalendar)
            {
            case calendar_id::Buddhist: set("be", aYear); break;
            case calendar_id::Coptic: set("am", aYear); break;
            case calendar_id::Ethioaa: set("aa", aYear); break;
            case calendar_id::Ethiopic:
                if (aYear >= 1)
                    set("am", aYear);
                else
                    set("aa", aYear + 5500);
                break;
            case calendar_id::Hebrew: set("am", aYear); break;
            case calendar_id::Indian: set("shaka", aYear); break;
            case calendar_id::Persian: set("ap", aYear); break;
            case calendar_id::IslamicCivil: case calendar_id::IslamicTbla: case calendar_id::IslamicUmalqura:
                if (aYear >= 1)
                    set("ah", aYear);
                else
                    set("bh", 1 - aYear);
                break;
            case calendar_id::Roc:
                if (aYear >= 1)
                    set("roc", aYear);
                else
                    set("broc", 1 - aYear);
                break;
            case calendar_id::Japanese:
                {
                    // (the eras of the Japanese government from 1873; before, those of the Gregorian calendar)
                    struct japanese_era { std::int64_t year; int month; int day; char const* era; };
                    static constexpr japanese_era sJapanese[] = { { 2019, 5, 1, "reiwa" }, { 1989, 1, 8, "heisei" }, { 1926, 12, 25, "showa" },
                        { 1912, 7, 30, "taisho" }, { 1873, 1, 1, "meiji" } };
                    for (auto const& e : sJapanese)
                        if (compare_iso_date(aIsoDate, { e.year, e.month, e.day }) >= 0)
                        {
                            auto const start = std::string_view{ e.era } == "meiji" ? 1868 : e.year;
                            set(e.era, aYear - start + 1);
                            return;
                        }
                }
                [[fallthrough]];
            case calendar_id::Gregory:
                if (aYear >= 1)
                    set("ce", aYear);
                else
                    set("bce", 1 - aYear);
                break;
            default:
                break;
            }
        }
    }

    bool calendar_supports_era(calendar_id aCalendar)
    {
        return aCalendar != calendar_id::Iso8601 && aCalendar != calendar_id::Chinese && aCalendar != calendar_id::Dangi;
    }

    namespace
    {
        bool calendar_has_mid_year_eras(calendar_id aCalendar)
        {
            return aCalendar == calendar_id::Japanese;
        }

        // --- month codes

        // (IsValidMonthCodeForCalendar)
        bool is_valid_month_code(calendar_id aCalendar, int aNumber, bool aLeap)
        {
            if (!aLeap && aNumber >= 1 && aNumber <= 12)
                return true;
            switch (aCalendar)
            {
            case calendar_id::Chinese: case calendar_id::Dangi: return aLeap && aNumber >= 1 && aNumber <= 12;
            case calendar_id::Coptic: case calendar_id::Ethioaa: case calendar_id::Ethiopic: return !aLeap && aNumber == 13;
            case calendar_id::Hebrew: return aLeap && aNumber == 5;
            default: return false;
            }
        }

        // (the ordinal month of a month code in a year (none: the year has no such month))
        std::optional<int> month_code_ordinal(calendar_id aCalendar, std::int64_t aYear, int aNumber, bool aLeap)
        {
            auto const& y = year_data(aCalendar, aYear);
            for (std::size_t i = 0u; i < y.months.size(); ++i)
                if (y.months[i].number == aNumber && y.months[i].leap == aLeap)
                    return static_cast<int>(i) + 1;
            return std::nullopt;
        }

        // (ConstrainMonthCode: a month code of the year (its number and leap flag))
        std::pair<int, bool> constrain_month_code(calendar_id aCalendar, std::int64_t aYear, int aNumber, bool aLeap, overflow aOverflow)
        {
            if (month_code_ordinal(aCalendar, aYear, aNumber, aLeap))
                return { aNumber, aLeap };
            if (aOverflow == overflow::Reject)
                throw_range_error("the month code is not in the year");
            if (aCalendar == calendar_id::Hebrew)
                return { 6, false };    // (Adar I in a common year: Adar)
            return { aNumber, false };  // (a leap month (Chinese, Korean): the month it follows)
        }

        bool parse_code(std::u16string const& aCode, int& aNumber, bool& aLeap)
        {
            return parse_month_code(aCode, aNumber, aLeap);
        }

        // (BalanceNonISODate's months: an ordinal month beyond its year's in the years before or after)
        std::pair<std::int64_t, int> balance_months(calendar_id aCalendar, std::int64_t aYear, std::int64_t aMonth)
        {
            check_year_range(aCalendar, static_cast<double>(aYear) + static_cast<double>(aMonth) / 13.0);
            if (aMonth >= 1 && aMonth <= months_in_year(aCalendar, aYear))
                return { aYear, static_cast<int>(aMonth) };
            if (!is_lunisolar(aCalendar))
            {
                // (a fixed number of months in every year)
                std::int64_t const n = months_in_year(aCalendar, aYear);
                auto const m0 = aMonth - 1;
                auto const years = m0 >= 0 ? m0 / n : -((-m0 + n - 1) / n);
                return { aYear + years, static_cast<int>(m0 - years * n) + 1 };
            }
            if (std::abs(aMonth) < 120)
            {
                auto year = aYear;
                auto month = aMonth;
                while (month <= 0)
                    month += months_in_year(aCalendar, --year);
                while (month > months_in_year(aCalendar, year))
                    month -= months_in_year(aCalendar, year++);
                return { year, static_cast<int>(month) };
            }
            // (many months: the month of the day that many mean lunations after the year's first month's middle)
            constexpr double synodicMonth = 29.530588853;
            auto const& y = year_data(aCalendar, aYear);
            auto const day = static_cast<std::int64_t>(std::floor(static_cast<double>(y.start) + static_cast<double>(aMonth - 1) * synodicMonth + 14.0));
            check_iso_days_range(epoch_days_to_iso_date(std::clamp<std::int64_t>(day, -200000000, 200000000)));
            auto const date = date_of_epoch_day(aCalendar, day);
            return { date.year, date.month };
        }

        // (CalendarIntegersToISO)
        iso_date integers_to_iso(calendar_id aCalendar, std::int64_t aYear, int aMonth, int aDay)
        {
            if (aMonth < 1 || aMonth > months_in_year(aCalendar, aYear) || aDay < 1 || aDay > days_in_month(aCalendar, aYear, aMonth))
                throw_range_error("the date does not exist in the calendar");
            return epoch_days_to_iso_date(month_start(aCalendar, aYear, aMonth) + aDay - 1);
        }

        // (the maximum days of a month code in any year)
        int maximum_days_in_month_code(calendar_id aCalendar, int aNumber, bool aLeap)
        {
            switch (aCalendar)
            {
            case calendar_id::Chinese: case calendar_id::Dangi: case calendar_id::IslamicUmalqura: return 30;
            case calendar_id::Coptic: case calendar_id::Ethioaa: case calendar_id::Ethiopic: return aNumber == 13 ? 6 : 30;
            case calendar_id::Hebrew:
                {
                    static constexpr int sDays[] = { 30, 30, 30, 29, 30, 29, 30, 29, 30, 29, 30, 29 };
                    return aLeap ? 30 : sDays[aNumber - 1];
                }
            case calendar_id::Indian: return aNumber <= 6 ? 31 : 30;
            case calendar_id::Persian: return aNumber <= 6 ? 31 : 30;
            case calendar_id::IslamicCivil: case calendar_id::IslamicTbla: return aNumber % 2 == 1 || aNumber == 12 ? 30 : 29;
            default: return aNumber == 2 ? 29 : iso_days_in_month(1972, aNumber);
            }
        }

        bool compare_surpasses(int aSign, std::int64_t aYear, int aMonth, std::optional<std::string> const& aMonthCode, int aDay,
            calendar_date const& aTarget)
        {
            if (aYear != aTarget.year)
                return aSign * (aYear > aTarget.year ? 1 : -1) > 0;
            if (aMonthCode)
            {
                auto const target = aTarget.month_code();
                if (*aMonthCode != target)
                    return aSign > 0 ? *aMonthCode > target : target > *aMonthCode;
            }
            else if (aMonth != aTarget.month)
                return aSign * (aMonth > aTarget.month ? 1 : -1) > 0;
            if (aDay != aTarget.day)
                return aSign * (aDay > aTarget.day ? 1 : -1) > 0;
            return false;
        }
    }

    // --- Calendar Date Records

    calendar_date calendar_iso_to_date(calendar_id aCalendar, iso_date const& aDate)
    {
        if (aCalendar == calendar_id::Iso8601)
            return iso_calendar_date(aDate);
        auto const epochDay = iso_date_to_epoch_days(aDate.year, aDate.month - 1, aDate.day);
        auto const date = date_of_epoch_day(aCalendar, epochDay);
        auto const& y = year_data(aCalendar, date.year);
        auto const& m = y.months[static_cast<std::size_t>(date.month - 1)];
        calendar_date d;
        date_era(aCalendar, aDate, date.year, d);
        d.year = date.year;
        d.month = date.month;
        d.monthNumber = m.number;
        d.leapMonth = m.leap;
        d.day = date.day;
        d.dayOfWeek = iso_day_of_week(aDate);
        d.dayOfYear = static_cast<int>(epochDay - y.start) + 1;
        d.daysInMonth = m.length;
        d.daysInYear = y.length;
        d.monthsInYear = static_cast<int>(y.months.size());
        if (is_lunisolar(aCalendar))
            d.inLeapYear = d.monthsInYear > 12;
        else if (aCalendar == calendar_id::IslamicCivil || aCalendar == calendar_id::IslamicTbla || aCalendar == calendar_id::IslamicUmalqura)
            d.inLeapYear = d.daysInYear > 354;
        else
            d.inLeapYear = d.daysInYear > 365;
        return d;
    }

    // --- fields

    unsigned calendar_extra_fields(calendar_id aCalendar, unsigned aFields)
    {
        if ((aFields & FieldYear) != 0u && calendar_supports_era(aCalendar))
            return FieldEra | FieldEraYear;
        return 0u;
    }

    unsigned calendar_field_keys_to_ignore(calendar_id aCalendar, unsigned aKeys)
    {
        auto ignored = aKeys;
        if ((aKeys & FieldMonth) != 0u)
            ignored |= FieldMonthCode;
        if ((aKeys & FieldMonthCode) != 0u)
            ignored |= FieldMonth;
        if (aCalendar == calendar_id::Iso8601)
            return ignored;
        if ((aKeys & (FieldEra | FieldEraYear | FieldYear)) != 0u && calendar_supports_era(aCalendar))
            ignored |= FieldEra | FieldEraYear | FieldYear;
        if ((aKeys & (FieldDay | FieldMonth | FieldMonthCode)) != 0u && calendar_has_mid_year_eras(aCalendar))
            ignored |= FieldEra | FieldEraYear;
        return ignored;
    }

    // (NonISOResolveFields)
    void non_iso_resolve_fields(calendar_id aCalendar, calendar_fields& aFields, int aType)
    {
        bool needsYear = aType == 0 || aType == 1 || !aFields.monthCode || aFields.month;
        bool const needsDay = aType == 0 || aType == 2;
        bool const eras = calendar_supports_era(aCalendar);
        if (needsYear && !aFields.year)
        {
            if (!eras)
                throw_type_error("year is required");
            if (!aFields.era || !aFields.eraYear)
                throw_type_error("year, or era and eraYear, are required");
        }
        if (eras)
        {
            if (aFields.era && !aFields.eraYear)
                throw_type_error("eraYear is required with era");
            if (aFields.eraYear && !aFields.era)
                throw_type_error("era is required with eraYear");
        }
        if (needsDay && !aFields.day)
            throw_type_error("day is required");
        if (!aFields.month && !aFields.monthCode)
            throw_type_error("month or monthCode is required");
        if (eras && aFields.eraYear)
        {
            auto const* era = canonical_era(aCalendar, *aFields.era);
            if (era == nullptr)
                throw_range_error("invalid era: " + utf8(make_string(*aFields.era)));
            auto const year = arithmetic_year_for_era_year(*era, *aFields.eraYear);
            if (aFields.year && *aFields.year != year)
                throw_range_error("year and eraYear do not agree");
            aFields.year = year;
        }
        aFields.era.reset();
        aFields.eraYear.reset();
        if (aFields.monthCode)
        {
            int number = 0;
            bool leap = false;
            parse_code(*aFields.monthCode, number, leap);
            if (!is_valid_month_code(aCalendar, number, leap))
                throw_range_error("invalid monthCode for the calendar: " + utf8(make_string(*aFields.monthCode)));
            if (aFields.year)
            {
                check_year_range(aCalendar, *aFields.year);
                auto const year = static_cast<std::int64_t>(*aFields.year);
                auto const constrained = constrain_month_code(aCalendar, year, number, leap, overflow::Constrain);
                auto const month = *month_code_ordinal(aCalendar, year, constrained.first, constrained.second);
                if (aFields.month && *aFields.month != month)
                    throw_range_error("month and monthCode do not agree");
                aFields.month = month;
            }
        }
    }

    // (NonISOCalendarDateToISO)
    iso_date non_iso_calendar_date_to_iso(calendar_id aCalendar, calendar_fields const& aFields, overflow aOverflow)
    {
        check_year_range(aCalendar, *aFields.year);
        auto const year = static_cast<std::int64_t>(*aFields.year);
        if (aFields.monthCode)
        {
            int number = 0;
            bool leap = false;
            parse_code(*aFields.monthCode, number, leap);
            constrain_month_code(aCalendar, year, number, leap, aOverflow);
        }
        auto const monthsInYear = months_in_year(aCalendar, year);
        auto month = *aFields.month;
        if (month > monthsInYear)
        {
            if (aOverflow == overflow::Reject)
                throw_range_error("the month does not exist in the year");
            month = monthsInYear;
        }
        auto const daysInMonth = days_in_month(aCalendar, year, static_cast<int>(month));
        auto day = *aFields.day;
        if (day > daysInMonth)
        {
            if (aOverflow == overflow::Reject)
                throw_range_error("the day does not exist in the month");
            day = daysInMonth;
        }
        return integers_to_iso(aCalendar, year, static_cast<int>(month), static_cast<int>(day));
    }

    // (NonISOMonthDayToISOReferenceDate)
    iso_date non_iso_month_day_to_iso_reference_date(calendar_id aCalendar, calendar_fields const& aFields, overflow aOverflow)
    {
        int number = 0;
        bool leap = false;
        int daysInMonth = 0;
        if (aFields.year)
        {
            auto const iso = approximate_iso_year(aCalendar, *aFields.year);
            if (!(iso >= -271821.0 && iso <= 275760.0))
                throw_range_error("the year is outside the representable range");
            auto const year = static_cast<std::int64_t>(*aFields.year);
            auto const monthsInYear = months_in_year(aCalendar, year);
            auto month = *aFields.month;
            if (month > monthsInYear)
            {
                if (aOverflow == overflow::Reject)
                    throw_range_error("the month does not exist in the year");
                month = monthsInYear;
            }
            if (!aFields.monthCode)
            {
                auto const& m = year_data(aCalendar, year).months[static_cast<std::size_t>(month - 1)];
                number = m.number;
                leap = m.leap;
            }
            else
            {
                parse_code(*aFields.monthCode, number, leap);
                std::tie(number, leap) = constrain_month_code(aCalendar, year, number, leap, aOverflow);
            }
            daysInMonth = days_in_month(aCalendar, year, static_cast<int>(month));
        }
        else
        {
            parse_code(*aFields.monthCode, number, leap);
            daysInMonth = maximum_days_in_month_code(aCalendar, number, leap);
        }
        auto day = static_cast<int>(*aFields.day);
        if (day > daysInMonth)
        {
            if (aOverflow == overflow::Reject)
                throw_range_error("the day does not exist in the month");
            day = daysInMonth;
        }
        // (the month code and day in a year: its ISO date, if the year has it)
        auto const date_in = [&](std::int64_t aYear, int aNumber, bool aLeap, int aDay) -> std::optional<iso_date>
        {
            auto const ordinal = month_code_ordinal(aCalendar, aYear, aNumber, aLeap);
            if (!ordinal || aDay > days_in_month(aCalendar, aYear, *ordinal))
                return std::nullopt;
            return epoch_days_to_iso_date(month_start(aCalendar, aYear, *ordinal) + aDay - 1);
        };
        // (the latest ISO date with the month code and day in the ISO year (none: there is none))
        auto const latest_in_iso_year = [&](std::int64_t aIsoYear, int aNumber, bool aLeap, int aDay) -> std::optional<iso_date>
        {
            auto const first = the_calendar_data().year_of(aCalendar, iso_date_to_epoch_days(aIsoYear, 0, 1));
            auto const last = the_calendar_data().year_of(aCalendar, iso_date_to_epoch_days(aIsoYear, 11, 31));
            std::optional<iso_date> result;
            for (auto y = first; y <= last; ++y)
                if (auto const d = date_in(y, aNumber, aLeap, aDay); d && d->year == aIsoYear)
                    if (!result || compare_iso_date(*d, *result) > 0)
                        result = d;
            return result;
        };
        if (aCalendar == calendar_id::Chinese || aCalendar == calendar_id::Dangi)
        {
            // (the reference years of the specification's table: 0 none)
            struct row { int days1to29; int day30; };
            static constexpr row sCommon[] = { { 1972, 1970 }, { 1972, 1972 }, { 1972, 0 }, { 1972, 1970 }, { 1972, 1972 }, { 1972, 1971 },
                { 1972, 1972 }, { 1972, 1971 }, { 1972, 1972 }, { 1972, 1972 }, { 1972, 1970 }, { 1972, 1972 } };
            static constexpr row sLeap[] = { { 0, 0 }, { 1947, 0 }, { 1966, 1955 }, { 1963, 1944 }, { 1971, 1952 }, { 1960, 1941 },
                { 1968, 1938 }, { 1957, 0 }, { 2014, 0 }, { 1984, 0 }, { 2034, 0 }, { 0, 0 } };
            auto referenceYear = [&](int aNumber, bool aLeap, int aDay) -> int
            {
                auto const& r = (aLeap ? sLeap : sCommon)[aNumber - 1];
                if (!aLeap && aNumber == 3 && aDay == 30)
                    return aCalendar == calendar_id::Chinese ? 1966 : 1968;
                if (aLeap && aNumber == 11 && aDay <= 10)
                    return 2033;
                return aDay == 30 ? r.day30 : r.days1to29;
            };
            if (referenceYear(number, leap, day) == 0 || (leap && (number == 1 || number == 12)))
            {
                if (aOverflow == overflow::Reject)
                    throw_range_error("the month and day are not known to occur");
                leap = false;
            }
            if (auto const d = latest_in_iso_year(referenceYear(number, leap, day), number, leap, day))
                return *d;
            // (the table's day 30: a 29-day month in the reference year)
            if (auto const d = latest_in_iso_year(referenceYear(number, leap, 29), number, leap, 29))
                return *d;
            throw_range_error("the month and day are not known to occur");
        }
        // (the latest date from 1900 to 1972, else the earliest from 1973 to 2035)
        if (aCalendar == calendar_id::Gregory || aCalendar == calendar_id::Buddhist || aCalendar == calendar_id::Roc || aCalendar == calendar_id::Japanese)
            return { 1972, number, day };
        std::optional<iso_date> result;
        auto const startDay = iso_date_to_epoch_days(1900, 0, 1);
        auto const endDay = iso_date_to_epoch_days(1972, 11, 31);
        for (auto y = the_calendar_data().year_of(aCalendar, endDay); y >= the_calendar_data().year_of(aCalendar, startDay) && !result; --y)
            if (auto const d = date_in(y, number, leap, day))
            {
                auto const epoch = iso_date_to_epoch_days(d->year, d->month - 1, d->day);
                if (epoch >= startDay && epoch <= endDay)
                    result = d;
            }
        if (!result)
        {
            auto const laterStart = iso_date_to_epoch_days(1973, 0, 1);
            auto const laterEnd = iso_date_to_epoch_days(2035, 11, 31);
            for (auto y = the_calendar_data().year_of(aCalendar, laterStart); y <= the_calendar_data().year_of(aCalendar, laterEnd) && !result; ++y)
                if (auto const d = date_in(y, number, leap, day))
                {
                    auto const epoch = iso_date_to_epoch_days(d->year, d->month - 1, d->day);
                    if (epoch >= laterStart && epoch <= laterEnd)
                        result = d;
                }
        }
        if (!result)
            throw_range_error("the month and day do not occur");
        return *result;
    }

    // --- arithmetic

    namespace
    {
        struct balanced_month
        {
            std::int64_t year;
            int month;
            int daysInMonth;
        };

        // (the month that is aMonths after a year's (constrained) month code)
        balanced_month add_months(calendar_id aCalendar, std::int64_t aYear, int aOrdinal, double aMonths)
        {
            auto const [year, month] = balance_months(aCalendar, aYear, static_cast<std::int64_t>(aOrdinal) + static_cast<std::int64_t>(aMonths));
            return { year, month, days_in_month(aCalendar, year, month) };
        }

        // (NonISODateSurpasses)
        bool non_iso_date_surpasses(calendar_id aCalendar, int aSign, calendar_date const& aFrom, calendar_date const& aTo, std::int64_t aToEpochDay,
            double aYears, double aMonths, double aWeeks, double aDays)
        {
            auto const y0 = aFrom.year + static_cast<std::int64_t>(aYears);
            if (compare_surpasses(aSign, y0, 0, aFrom.month_code(), aFrom.day, aTo))
                return true;
            auto const constrained = constrain_month_code(aCalendar, y0, aFrom.monthNumber, aFrom.leapMonth, overflow::Constrain);
            auto const m0 = *month_code_ordinal(aCalendar, y0, constrained.first, constrained.second);
            auto const added = add_months(aCalendar, y0, m0, aMonths);
            if (compare_surpasses(aSign, added.year, added.month, std::nullopt, aFrom.day, aTo))
                return true;
            if (aWeeks == 0.0 && aDays == 0.0)
                return false;
            auto const regulatedDay = std::min(aFrom.day, added.daysInMonth);
            auto const epochDay = month_start(aCalendar, added.year, added.month) + regulatedDay - 1 + static_cast<std::int64_t>(7.0 * aWeeks + aDays);
            return aSign * (epochDay > aToEpochDay ? 1 : epochDay < aToEpochDay ? -1 : 0) > 0;
        }
    }

    // (NonISODateAdd)
    iso_date non_iso_date_add(calendar_id aCalendar, iso_date const& aDate, date_duration const& aDuration, overflow aOverflow)
    {
        auto const parts = calendar_iso_to_date(aCalendar, aDate);
        auto const y0 = static_cast<double>(parts.year) + aDuration.years;
        check_year_range(aCalendar, y0);
        auto const constrained = constrain_month_code(aCalendar, static_cast<std::int64_t>(y0), parts.monthNumber, parts.leapMonth, aOverflow);
        auto const m0 = *month_code_ordinal(aCalendar, static_cast<std::int64_t>(y0), constrained.first, constrained.second);
        auto const end = add_months(aCalendar, static_cast<std::int64_t>(y0), m0, aDuration.months);
        auto regulatedDay = parts.day;
        if (regulatedDay > end.daysInMonth)
        {
            if (aOverflow == overflow::Reject)
                throw_range_error("the day does not exist in the month");
            regulatedDay = end.daysInMonth;
        }
        auto const days = 7.0 * aDuration.weeks + aDuration.days;
        auto const epochDay = static_cast<double>(month_start(aCalendar, end.year, end.month) + regulatedDay - 1) + days;
        if (!(epochDay >= -100000001.0 && epochDay <= 100000001.0))
            throw_range_error("date is outside the representable range");
        auto const result = epoch_days_to_iso_date(static_cast<std::int64_t>(epochDay));
        if (!iso_date_within_limits(result))
            throw_range_error("date is outside the representable range");
        return result;
    }

    // (NonISODateUntil: each unit's candidate (the specification's loops' result) from the dates' difference, then corrected with
    // NonISODateSurpasses)
    date_duration non_iso_date_until(calendar_id aCalendar, iso_date const& aOne, iso_date const& aTwo, unit aLargestUnit)
    {
        auto const sign = -compare_iso_date(aOne, aTwo);
        if (sign == 0)
            return {};
        auto const from = calendar_iso_to_date(aCalendar, aOne);
        auto const to = calendar_iso_to_date(aCalendar, aTwo);
        auto const toDay = iso_date_to_epoch_days(aTwo.year, aTwo.month - 1, aTwo.day);
        auto const surpasses = [&](double y, double m, double w, double d) { return non_iso_date_surpasses(aCalendar, sign, from, to, toDay, y, m, w, d); };
        // (the largest count (in the sign's direction) that does not surpass, from a candidate)
        auto const largest = [&](double aCandidate, auto&& aSurpasses) -> double
        {
            auto n = aCandidate;
            if (sign * n < 0)
                n = 0.0;
            while (sign * n > 0 && aSurpasses(n))
                n -= sign;
            while (!aSurpasses(n + sign))
                n += sign;
            return n;
        };
        double years = 0.0;
        if (aLargestUnit == unit::Year)
            years = largest(static_cast<double>(to.year - from.year), [&](double n) { return surpasses(n, 0.0, 0.0, 0.0); });
        double months = 0.0;
        if (aLargestUnit == unit::Year || aLargestUnit == unit::Month)
        {
            // (the candidate: the months between the dates' months (mean lunations for the lunisolar calendars))
            double candidate = 0.0;
            if (aLargestUnit == unit::Month)
            {
                auto const fromStart = month_start(aCalendar, from.year, from.month);
                auto const toStart = month_start(aCalendar, to.year, to.month);
                if (is_lunisolar(aCalendar))
                    candidate = std::round(static_cast<double>(toStart - fromStart) / 29.530588853);
                else
                    candidate = static_cast<double>((to.year - from.year) * from.monthsInYear + (to.month - from.month));
            }
            months = largest(candidate, [&](double n) { return surpasses(years, n, 0.0, 0.0); });
        }
        // (the days from the regulated date after the years and months)
        auto const y0 = from.year + static_cast<std::int64_t>(years);
        auto const constrained = constrain_month_code(aCalendar, y0, from.monthNumber, from.leapMonth, overflow::Constrain);
        auto const m0 = *month_code_ordinal(aCalendar, y0, constrained.first, constrained.second);
        auto const added = add_months(aCalendar, y0, m0, months);
        auto const regulatedDay = std::min(from.day, added.daysInMonth);
        auto const regulated = month_start(aCalendar, added.year, added.month) + regulatedDay - 1;
        auto difference = toDay - regulated;
        if (sign * difference < 0)
            difference = 0;
        double weeks = 0.0;
        if (aLargestUnit == unit::Week)
            weeks = static_cast<double>(difference / 7);
        auto const days = static_cast<double>(difference) - weeks * 7.0;
        return { years + 0.0, months + 0.0, weeks + 0.0, days + 0.0 };
    }

#else

    bool calendar_supports_era(calendar_id) { return false; }
    calendar_date calendar_iso_to_date(calendar_id, iso_date const& aDate) { return iso_calendar_date(aDate); }
    unsigned calendar_extra_fields(calendar_id, unsigned) { return 0u; }
    unsigned calendar_field_keys_to_ignore(calendar_id, unsigned aKeys)
    {
        auto ignored = aKeys;
        if ((aKeys & FieldMonth) != 0u)
            ignored |= FieldMonthCode;
        if ((aKeys & FieldMonthCode) != 0u)
            ignored |= FieldMonth;
        return ignored;
    }
    void non_iso_resolve_fields(calendar_id, calendar_fields&, int) { throw_range_error("unsupported calendar"); }
    iso_date non_iso_calendar_date_to_iso(calendar_id, calendar_fields const&, overflow) { throw_range_error("unsupported calendar"); }
    iso_date non_iso_month_day_to_iso_reference_date(calendar_id, calendar_fields const&, overflow) { throw_range_error("unsupported calendar"); }
    iso_date non_iso_date_add(calendar_id, iso_date const&, date_duration const&, overflow) { throw_range_error("unsupported calendar"); }
    date_duration non_iso_date_until(calendar_id, iso_date const&, iso_date const&, unit) { throw_range_error("unsupported calendar"); }

#endif
}
