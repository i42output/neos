/*
  temporal.hpp

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

// Temporal (ECMA-262 Temporal; with Intl (ICU), ECMA-402's calendars too): the internal records and abstract operations shared by the
// Temporal built-ins (temporal_*.cpp).
//
// Exact arithmetic: epoch nanoseconds and time durations are integers of up to about 90 bits; they are i128s (a portable
// 128-bit two's complement integer: no __int128, so that MSVC builds it too). A duration's fields are float64-representable
// integers (doubles), as the specification has them.

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "js.hpp"

namespace neos::javascript_runtime::temporal
{
    // --- 128-bit signed integers

    class i128
    {
    public:
        constexpr i128() = default;
        constexpr i128(std::int64_t aValue) : iLo{ static_cast<std::uint64_t>(aValue) }, iHi{ aValue < 0 ? ~0ull : 0ull } {}
        static constexpr i128 from_parts(std::uint64_t aHi, std::uint64_t aLo) { i128 r; r.iHi = aHi; r.iLo = aLo; return r; }
    public:
        std::uint64_t lo() const { return iLo; }
        std::uint64_t hi() const { return iHi; }
        bool negative() const { return (iHi >> 63) != 0u; }
        bool zero() const { return iLo == 0u && iHi == 0u; }
        int sign() const { return negative() ? -1 : (zero() ? 0 : 1); }
        bool fits_int64() const { return iHi == (negative() ? ~0ull : 0ull) && ((iLo >> 63) != 0u) == negative(); }
        std::int64_t to_int64() const { return static_cast<std::int64_t>(iLo); }   ///< (truncating)
        double to_double() const;                                                   ///< (correctly rounded)
        static i128 from_double(double aValue);                                     ///< (an integral double whose magnitude is below 2^127)
        std::string to_string() const;
        i128 abs() const { return negative() ? -*this : *this; }
    public:
        friend i128 operator+(i128 const& a, i128 const& b)
        {
            i128 r;
            r.iLo = a.iLo + b.iLo;
            r.iHi = a.iHi + b.iHi + (r.iLo < a.iLo ? 1u : 0u);
            return r;
        }
        friend i128 operator-(i128 const& a)
        {
            i128 r;
            r.iLo = ~a.iLo + 1u;
            r.iHi = ~a.iHi + (r.iLo == 0u ? 1u : 0u);
            return r;
        }
        friend i128 operator-(i128 const& a, i128 const& b) { return a + (-b); }
        friend i128 operator*(i128 const& a, i128 const& b);
        friend i128 operator/(i128 const& a, i128 const& b);                        ///< (truncating)
        friend i128 operator%(i128 const& a, i128 const& b);                        ///< (the remainder of truncating division)
        i128& operator+=(i128 const& b) { return *this = *this + b; }
        i128& operator-=(i128 const& b) { return *this = *this - b; }
        i128& operator*=(i128 const& b) { return *this = *this * b; }
        friend bool operator==(i128 const& a, i128 const& b) { return a.iLo == b.iLo && a.iHi == b.iHi; }
        friend bool operator!=(i128 const& a, i128 const& b) { return !(a == b); }
        friend bool operator<(i128 const& a, i128 const& b)
        {
            auto const ah = static_cast<std::int64_t>(a.iHi);
            auto const bh = static_cast<std::int64_t>(b.iHi);
            return ah != bh ? ah < bh : a.iLo < b.iLo;
        }
        friend bool operator>(i128 const& a, i128 const& b) { return b < a; }
        friend bool operator<=(i128 const& a, i128 const& b) { return !(b < a); }
        friend bool operator>=(i128 const& a, i128 const& b) { return !(a < b); }
    private:
        std::uint64_t iLo = 0u;
        std::uint64_t iHi = 0u;
    };

    void divmod(i128 const& aNumerator, i128 const& aDenominator, i128& aQuotient, i128& aRemainder);   ///< (truncating)
    i128 floor_div(i128 const& aNumerator, i128 const& aDenominator);
    i128 floor_mod(i128 const& aNumerator, i128 const& aDenominator);            ///< (the result has the divisor's sign)
    double ratio_to_double(i128 aNumerator, i128 aDenominator);                  ///< (the double nearest the exact quotient)
    int compare(i128 const& a, i128 const& b);

    inline constexpr std::int64_t nsPerDay = 86400000000000LL;
    i128 const& ns_max_instant();               ///< (10^8 days)
    i128 const& max_time_duration();            ///< (2^53 × 10^9 - 1)

    // (BigInts)
    value make_bigint_from(i128 const& aValue);
    std::optional<i128> bigint_to_i128(value aBigInt);    ///< (none if it is beyond 2^127)

    // --- units, options and rounding

    enum class unit : int
    {
        Year,
        Month,
        Week,
        Day,
        Hour,
        Minute,
        Second,
        Millisecond,
        Microsecond,
        Nanosecond,
        Unset,
        Auto
    };

    enum class rounding_mode
    {
        Ceil,
        Floor,
        Expand,
        Trunc,
        HalfCeil,
        HalfFloor,
        HalfExpand,
        HalfTrunc,
        HalfEven
    };

    enum class overflow { Constrain, Reject };
    enum class disambiguation { Compatible, Earlier, Later, Reject };
    enum class offset_option { Prefer, Use, Ignore, Reject };
    enum class show_calendar { Auto, Always, Never, Critical };
    enum class show_time_zone { Auto, Never, Critical };
    enum class unit_group { Date, Time, DateTime };

    inline bool is_calendar_unit(unit aUnit) { return aUnit == unit::Year || aUnit == unit::Month || aUnit == unit::Week; }
    inline bool is_date_unit(unit aUnit) { return static_cast<int>(aUnit) <= static_cast<int>(unit::Day); }
    inline unit larger_unit(unit a, unit b) { return static_cast<int>(a) <= static_cast<int>(b) ? a : b; }
    i128 unit_length(unit aUnit);              ///< (time units and days: their length in nanoseconds)
    std::int64_t maximum_rounding_increment(unit aUnit);  ///< (0: unset)

    value get_options_object(value aOptions);
    value get_option_string(value aOptions, char const* aProperty);     ///< (undefined, or the property converted to a string)
    overflow get_overflow_option(value aOptions);
    disambiguation get_disambiguation_option(value aOptions);
    offset_option get_offset_option(value aOptions, offset_option aFallback);
    show_calendar get_show_calendar_option(value aOptions);
    show_time_zone get_show_time_zone_option(value aOptions);
    bool get_show_offset_option(value aOptions);                        ///< (false: never)
    int get_fractional_second_digits_option(value aOptions);            ///< (-1: auto)
    rounding_mode get_rounding_mode_option(value aOptions, rounding_mode aFallback);
    std::int64_t get_rounding_increment_option(value aOptions);
    unit get_unit_option(value aOptions, char const* aKey, bool aRequired);
    void validate_unit(unit aUnit, unit_group aGroup, std::initializer_list<unit> aExtra = {});
    void validate_rounding_increment(std::int64_t aIncrement, std::int64_t aDividend, bool aInclusive);
    rounding_mode negate_rounding_mode(rounding_mode aMode);
    double to_integer_with_truncation(value aValue);
    double to_positive_integer_with_truncation(value aValue);
    double to_integer_if_integral(value aValue);

    struct precision_record
    {
        int precision;          ///< (-1: auto, -2: minute, else 0 to 9 digits)
        unit u;
        std::int64_t increment;
    };
    precision_record to_seconds_string_precision(unit aSmallestUnit, int aDigits);

    struct difference_settings
    {
        unit smallestUnit;
        unit largestUnit;
        rounding_mode roundingMode;
        std::int64_t roundingIncrement;
    };
    difference_settings get_difference_settings(bool aSince, value aOptions, unit_group aGroup, std::initializer_list<unit> aDisallowed,
        unit aFallbackSmallest, unit aSmallestLargestDefault);

    i128 round_to_increment(i128 const& aValue, i128 const& aIncrement, rounding_mode aMode);
    i128 round_to_increment_as_if_positive(i128 const& aValue, i128 const& aIncrement, rounding_mode aMode);
    int unsigned_round(int aCompareHalf, bool aR1Even, rounding_mode aMode, bool aNegative);   ///< (0: r1, 1: r2)

    // --- dates and times

    struct iso_date
    {
        std::int64_t year = 1970;
        int month = 1;
        int day = 1;
    };

    struct time_record
    {
        std::int64_t days = 0;
        int hour = 0;
        int minute = 0;
        int second = 0;
        int millisecond = 0;
        int microsecond = 0;
        int nanosecond = 0;
    };

    struct iso_date_time
    {
        iso_date date;
        time_record time;
    };

    std::int64_t clamp_int(double aValue);     ///< (an integer saturated to ±10^12 (beyond any valid date's fields))
    bool is_leap_year(std::int64_t aYear);
    int iso_days_in_month(std::int64_t aYear, int aMonth);
    std::int64_t iso_date_to_epoch_days(std::int64_t aYear, std::int64_t aMonth0, std::int64_t aDay);   ///< (month 0-based, may overflow)
    iso_date epoch_days_to_iso_date(std::int64_t aDays);
    bool is_valid_iso_date(std::int64_t aYear, std::int64_t aMonth, std::int64_t aDay);
    bool is_valid_time(double h, double mi, double s, double ms, double us, double ns);
    iso_date add_days_to_iso_date(iso_date const& aDate, std::int64_t aDays);
    std::pair<std::int64_t, int> balance_iso_year_month(std::int64_t aYear, std::int64_t aMonth);
    iso_date regulate_iso_date(std::int64_t aYear, std::int64_t aMonth, std::int64_t aDay, overflow aOverflow);
    time_record regulate_time(double h, double mi, double s, double ms, double us, double ns, overflow aOverflow);
    time_record balance_time(i128 aHour, i128 aMinute, i128 aSecond, i128 aMillisecond, i128 aMicrosecond, i128 aNanosecond);
    time_record add_time(time_record const& aTime, i128 const& aTimeDuration);
    time_record round_time(time_record const& aTime, std::int64_t aIncrement, unit aUnit, rounding_mode aMode);
    i128 difference_time(time_record const& a, time_record const& b);
    i128 time_to_ns(time_record const& aTime);           ///< (within its day)
    int compare_iso_date(iso_date const& a, iso_date const& b);
    int compare_time(time_record const& a, time_record const& b);
    int compare_iso_date_time(iso_date_time const& a, iso_date_time const& b);
    iso_date_time balance_iso_date_time(iso_date const& aDate, i128 aHour, i128 aMinute, i128 aSecond, i128 aMs, i128 aUs, i128 aNs);
    i128 get_utc_epoch_ns(iso_date_time const& aDateTime);
    iso_date_time iso_date_time_from_epoch_ns(i128 const& aEpochNs);    ///< (GetISOPartsFromEpoch)
    bool iso_date_time_within_limits(iso_date_time const& aDateTime);
    bool iso_date_within_limits(iso_date const& aDate);
    bool iso_year_month_within_limits(iso_date const& aDate);
    void check_iso_days_range(iso_date const& aDate);
    bool is_valid_epoch_ns(i128 const& aEpochNs);
    iso_date_time round_iso_date_time(iso_date_time const& aDateTime, std::int64_t aIncrement, unit aUnit, rounding_mode aMode);
    int iso_day_of_week(iso_date const& aDate);
    int iso_day_of_year(iso_date const& aDate);
    std::pair<int, std::int64_t> iso_week_of_year(iso_date const& aDate);    ///< (week, year)

    // formatting
    std::string pad_iso_year(std::int64_t aYear);
    std::string zero_pad(std::int64_t aValue, int aWidth);
    std::string format_time_string(int aHour, int aMinute, int aSecond, std::int64_t aSubSecondNs, int aPrecision, bool aSeparated = true);
    std::string format_fractional_seconds(std::int64_t aSubSecondNs, int aPrecision);
    std::string time_record_to_string(time_record const& aTime, int aPrecision);
    std::string iso_date_to_string(iso_date const& aDate);
    enum class calendar_id : std::uint8_t;
    std::string iso_date_time_to_string(iso_date_time const& aDateTime, int aPrecision, calendar_id aCalendar, show_calendar aShowCalendar);
    std::string format_calendar_annotation(calendar_id aCalendar, show_calendar aShowCalendar);
    std::string format_offset_minutes(int aOffsetMinutes, bool aSeparated = true);
    std::string format_utc_offset_ns(std::int64_t aOffsetNs);
    std::string format_utc_offset_rounded(std::int64_t aOffsetNs);

    // --- durations

    struct date_duration
    {
        double years = 0.0;
        double months = 0.0;
        double weeks = 0.0;
        double days = 0.0;
    };

    struct internal_duration
    {
        date_duration date;
        i128 time;
    };

    struct duration_fields
    {
        double v[10] = {};      ///< (years, months, weeks, days, hours, minutes, seconds, milliseconds, microseconds, nanoseconds)
    };

    bool is_valid_duration(duration_fields const& aFields);
    int duration_sign(duration_fields const& aFields);
    int date_duration_sign(date_duration const& aDuration);
    int internal_duration_sign(internal_duration const& aDuration);
    date_duration create_date_duration(double aYears, double aMonths, double aWeeks, double aDays);   ///< (a RangeError if it is invalid)
    unit default_largest_unit(duration_fields const& aFields);
    i128 time_duration_from_components(double h, double mi, double s, double ms, double us, double ns);
    i128 add_time_duration(i128 const& a, i128 const& b);
    i128 add_24_hour_days(i128 const& aTime, double aDays);
    internal_duration to_internal_duration(duration_fields const& aFields);
    internal_duration to_internal_duration_24(duration_fields const& aFields);
    date_duration to_date_duration_without_time(duration_fields const& aFields);
    duration_fields duration_from_internal(internal_duration const& aDuration, unit aLargestUnit);    ///< (a RangeError if invalid)
    duration_fields negate_duration(duration_fields const& aFields);
    i128 round_time_duration(i128 const& aTime, std::int64_t aIncrement, unit aUnit, rounding_mode aMode);
    std::string duration_to_string(duration_fields const& aFields, int aPrecision);
    duration_fields to_partial_duration(value aObject, bool& aAny);     ///< (the fields read; aAny: whether any was present) (NaN: absent)

    // --- time zones (temporal_timezone.cpp)

    struct time_zone
    {
        value id = Undefined;                       ///< (its identifier: a string)
        bool named = false;
        int offsetMinutes = 0;
        std::chrono::time_zone const* zone = nullptr;   ///< (a named zone (null: UTC without a time zone database))
    };

    struct named_zone_record
    {
        std::u16string identifier;
        std::u16string primary;
        std::chrono::time_zone const* zone = nullptr;
    };

    std::optional<named_zone_record> get_available_named_time_zone(std::u16string_view aName);
    std::vector<std::u16string> available_primary_time_zones();                 ///< (AvailableNamedTimeZoneIdentifiers' primary identifiers)
    time_zone time_zone_from_identifier(std::u16string_view aIdentifier);     ///< (an available identifier (as it is stored))
    time_zone system_time_zone();
    time_zone to_temporal_time_zone_identifier(value aValue);
    time_zone parse_time_zone_identifier_checked(value aString);   ///< (the ZonedDateTime constructor's: identifier syntax only)
    bool time_zone_equals(time_zone const& a, time_zone const& b);
    std::int64_t get_offset_ns_for(time_zone const& aTimeZone, i128 const& aEpochNs);
    iso_date_time get_iso_date_time_for(time_zone const& aTimeZone, i128 const& aEpochNs);
    std::vector<i128> get_possible_epoch_ns(time_zone const& aTimeZone, iso_date_time const& aDateTime);
    i128 disambiguate_possible_epoch_ns(std::vector<i128> const& aPossible, time_zone const& aTimeZone, iso_date_time const& aDateTime,
        disambiguation aDisambiguation);
    i128 get_epoch_ns_for(time_zone const& aTimeZone, iso_date_time const& aDateTime, disambiguation aDisambiguation);
    i128 get_start_of_day(time_zone const& aTimeZone, iso_date const& aDate);
    std::optional<i128> get_time_zone_transition(time_zone const& aTimeZone, i128 const& aEpochNs, bool aNext);
    i128 system_utc_epoch_ns();

    // --- parsing (temporal_parse.cpp)

    enum parse_goal : unsigned
    {
        GoalDateTimeZoned = 1u,
        GoalDateTime = 2u,
        GoalInstant = 4u,
        GoalTime = 8u,
        GoalMonthDay = 16u,
        GoalYearMonth = 32u
    };

    struct iso_parse_result
    {
        bool hasYear = true;
        std::int64_t year = 0;
        int month = 1;
        int day = 1;
        bool hasTime = false;   ///< (false: start-of-day)
        time_record time;
        bool z = false;
        std::optional<std::u16string> offset;
        std::optional<std::u16string> timeZoneAnnotation;
        std::optional<std::u16string> calendar;
    };

    iso_parse_result parse_iso_date_time(std::u16string_view aText, std::initializer_list<parse_goal> aGoals);
    bool parse_time_zone_identifier(std::u16string_view aText, std::u16string& aName, int& aOffsetMinutes, bool& aIsOffset);
    std::optional<std::int64_t> parse_date_time_utc_offset(std::u16string_view aText, bool* aSubMinute = nullptr);   ///< (ns)
    duration_fields parse_duration_string(std::u16string_view aText);
    bool parse_month_code(std::u16string_view aText, int& aMonth, bool& aLeap);
    std::u16string parse_temporal_calendar_string(std::u16string_view aText);
    bool is_annotation_value(std::u16string const& aText);
    std::u16string ascii_lowercase(std::u16string aText);

    // --- calendars (temporal_calendar.cpp: ISO 8601; with ICU, ECMA-402's (CLDR's) calendars too)

    enum class calendar_id : std::uint8_t
    {
        Iso8601,
        Buddhist,
        Chinese,
        Coptic,
        Dangi,
        Ethioaa,
        Ethiopic,
        Gregory,
        Hebrew,
        Indian,
        IslamicCivil,
        IslamicTbla,
        IslamicUmalqura,
        Japanese,
        Persian,
        Roc,
        Count
    };

    char const* calendar_identifier(calendar_id aCalendar);                 ///< (its canonical identifier, e.g. "islamic-civil")
    std::optional<calendar_id> available_calendar(std::u16string_view aId);   ///< (AvailableCalendars (ASCII case-insensitive; aliases too))
    value calendar_value(calendar_id aCalendar);
    calendar_id canonicalize_calendar(value aId);   ///< (CanonicalizeCalendar: a RangeError if it is not available)
    calendar_id to_temporal_calendar_identifier(value aValue);
    calendar_id get_temporal_calendar_with_iso_default(value aItem);
    bool calendar_supports_era(calendar_id aCalendar);

    // (a Calendar Date Record)
    struct calendar_date
    {
        std::optional<std::string> era;
        std::optional<std::int64_t> eraYear;
        std::int64_t year = 0;              ///< (the arithmetic year)
        int month = 1;                      ///< (ordinal)
        int monthNumber = 1;                ///< (its month code's number)
        bool leapMonth = false;             ///< (its month code's L)
        int day = 1;
        int dayOfWeek = 1;
        int dayOfYear = 1;
        std::optional<int> weekOfYear;
        std::optional<std::int64_t> yearOfWeek;
        int daysInWeek = 7;
        int daysInMonth = 30;
        int daysInYear = 365;
        int monthsInYear = 12;
        bool inLeapYear = false;
    public:
        std::string month_code() const;
    };
    calendar_date calendar_iso_to_date(calendar_id aCalendar, iso_date const& aDate);     ///< (CalendarISOToDate)

    struct calendar_fields
    {
        std::optional<double> year, month, day, hour, minute, second, millisecond, microsecond, nanosecond;
        std::optional<std::u16string> monthCode;
        std::optional<std::u16string> era;
        std::optional<double> eraYear;
        std::optional<std::u16string> offset;
        std::optional<time_zone> timeZone;
    };

    enum field_key : unsigned
    {
        FieldYear = 1u,
        FieldMonth = 2u,
        FieldMonthCode = 4u,
        FieldDay = 8u,
        FieldHour = 16u,
        FieldMinute = 32u,
        FieldSecond = 64u,
        FieldMillisecond = 128u,
        FieldMicrosecond = 256u,
        FieldNanosecond = 512u,
        FieldOffset = 1024u,
        FieldTimeZone = 2048u,
        FieldEra = 4096u,
        FieldEraYear = 8192u,
        FieldsDate = FieldYear | FieldMonth | FieldMonthCode | FieldDay,
        FieldsTime = FieldHour | FieldMinute | FieldSecond | FieldMillisecond | FieldMicrosecond | FieldNanosecond
    };

    unsigned calendar_extra_fields(calendar_id aCalendar, unsigned aFields);            ///< (CalendarExtraFields)
    unsigned calendar_field_keys_to_ignore(calendar_id aCalendar, unsigned aKeys);      ///< (CalendarFieldKeysToIgnore)
    calendar_fields prepare_calendar_fields(calendar_id aCalendar, value aObject, unsigned aFieldNames, unsigned aRequired, bool aPartial);
    calendar_fields iso_date_to_fields(calendar_id aCalendar, iso_date const& aDate, int aType);   ///< (0 date, 1 year-month, 2 month-day)
    calendar_fields merge_calendar_fields(calendar_id aCalendar, calendar_fields const& aFields, calendar_fields const& aAdditional);
    iso_date calendar_date_from_fields(calendar_id aCalendar, calendar_fields aFields, overflow aOverflow);
    iso_date calendar_year_month_from_fields(calendar_id aCalendar, calendar_fields aFields, overflow aOverflow);
    iso_date calendar_month_day_from_fields(calendar_id aCalendar, calendar_fields aFields, overflow aOverflow);
    iso_date_time interpret_temporal_date_time_fields(calendar_id aCalendar, calendar_fields const& aFields, overflow aOverflow);
    iso_date calendar_date_add(calendar_id aCalendar, iso_date const& aDate, date_duration const& aDuration, overflow aOverflow);
    date_duration calendar_date_until(calendar_id aCalendar, iso_date const& aOne, iso_date const& aTwo, unit aLargestUnit);
    // (the non-ISO calendars' (temporal_calendar.cpp))
    void non_iso_resolve_fields(calendar_id aCalendar, calendar_fields& aFields, int aType);
    iso_date non_iso_calendar_date_to_iso(calendar_id aCalendar, calendar_fields const& aFields, overflow aOverflow);
    iso_date non_iso_month_day_to_iso_reference_date(calendar_id aCalendar, calendar_fields const& aFields, overflow aOverflow);
    iso_date non_iso_date_add(calendar_id aCalendar, iso_date const& aDate, date_duration const& aDuration, overflow aOverflow);
    date_duration non_iso_date_until(calendar_id aCalendar, iso_date const& aOne, iso_date const& aTwo, unit aLargestUnit);

    // --- relative durations

    internal_duration round_relative_duration(internal_duration aDuration, i128 const& aOriginNs, i128 const& aDestNs,
        iso_date_time const& aDateTime, calendar_id aCalendar, time_zone const* aTimeZone, unit aLargestUnit, std::int64_t aIncrement,
        unit aSmallestUnit, rounding_mode aMode);
    double total_relative_duration(internal_duration const& aDuration, i128 const& aOriginNs, i128 const& aDestNs,
        iso_date_time const& aDateTime, calendar_id aCalendar, time_zone const* aTimeZone, unit aUnit);
    internal_duration difference_iso_date_time(calendar_id aCalendar, iso_date_time const& a, iso_date_time const& b, unit aLargestUnit);
    internal_duration difference_plain_date_time_with_rounding(calendar_id aCalendar, iso_date_time const& a, iso_date_time const& b,
        unit aLargestUnit, std::int64_t aIncrement, unit aSmallestUnit, rounding_mode aMode);
    double difference_plain_date_time_with_total(calendar_id aCalendar, iso_date_time const& a, iso_date_time const& b, unit aUnit);
    internal_duration difference_instant(i128 const& a, i128 const& b, std::int64_t aIncrement, unit aSmallestUnit, rounding_mode aMode);
    internal_duration difference_zoned_date_time(i128 const& a, i128 const& b, time_zone const& aTimeZone, calendar_id aCalendar,
        unit aLargestUnit);
    internal_duration difference_zoned_date_time_with_rounding(i128 const& a, i128 const& b, time_zone const& aTimeZone, calendar_id aCalendar,
        unit aLargestUnit, std::int64_t aIncrement, unit aSmallestUnit, rounding_mode aMode);
    double difference_zoned_date_time_with_total(i128 const& a, i128 const& b, time_zone const& aTimeZone, calendar_id aCalendar, unit aUnit);
    i128 add_instant(i128 const& aEpochNs, i128 const& aTime);
    i128 add_zoned_date_time(i128 const& aEpochNs, time_zone const& aTimeZone, calendar_id aCalendar, internal_duration const& aDuration,
        overflow aOverflow);
    i128 interpret_iso_date_time_offset(iso_date const& aDate, std::optional<time_record> const& aTime, int aOffsetBehaviour,
        std::int64_t aOffsetNs, time_zone const& aTimeZone, disambiguation aDisambiguation, offset_option aOffsetOption, bool aMatchMinutes);
    double total_time_duration(i128 const& aTime, unit aUnit);

    // --- the Temporal objects

    class temporal_object : public object
    {
    public:
        temporal_object(value aPrototype, object_class aClass) : object{ aPrototype, aClass } {}
    public:
        i128 epochNs;                   ///< (Instant, ZonedDateTime)
        iso_date date;                  ///< (PlainDate, PlainDateTime, PlainYearMonth, PlainMonthDay)
        time_record time;               ///< (PlainTime, PlainDateTime)
        time_zone timeZone;             ///< (ZonedDateTime)
        duration_fields duration;       ///< (Duration)
        calendar_id calendar = calendar_id::Iso8601;    ///< (PlainDate, PlainDateTime, PlainYearMonth, PlainMonthDay, ZonedDateTime)
    public:
        void trace(tracer& aTracer) override;   ///< (temporal_objects.cpp: its time zone's identifier too)
    };

    inline temporal_object* as_temporal(value aValue, object_class aClass)
    {
        if (!is_object(aValue))
            return nullptr;
        auto& o = obj(aValue);
        return o.cls == aClass ? static_cast<temporal_object*>(&o) : nullptr;
    }

    std::u16string u16(std::string const& aText);
    [[noreturn]] void range_error(char const* aMessage);
    [[noreturn]] void type_error(char const* aMessage);

    // --- creating and converting the Temporal objects (temporal_objects.cpp)

    temporal_object& this_temporal(value aThis, object_class aClass, char const* aName);
    value create_duration(duration_fields const& aFields, value aNewTarget = Undefined);
    value create_instant(i128 const& aEpochNs, value aNewTarget = Undefined);
    value create_plain_date(iso_date const& aDate, calendar_id aCalendar, value aNewTarget = Undefined);
    value create_plain_time(time_record const& aTime, value aNewTarget = Undefined);
    value create_plain_date_time(iso_date_time const& aDateTime, calendar_id aCalendar, value aNewTarget = Undefined);
    value create_plain_year_month(iso_date const& aDate, calendar_id aCalendar, value aNewTarget = Undefined);
    value create_plain_month_day(iso_date const& aDate, calendar_id aCalendar, value aNewTarget = Undefined);
    value create_zoned_date_time(i128 const& aEpochNs, time_zone const& aTimeZone, calendar_id aCalendar, value aNewTarget = Undefined);
    temporal_object& tobj(value aObject);
    duration_fields to_temporal_duration(value aItem);
    i128 to_temporal_instant(value aItem);
    // (a date (and a date-time) with its calendar)
    struct temporal_date
    {
        iso_date date;
        calendar_id calendar = calendar_id::Iso8601;
    };
    struct temporal_date_time
    {
        iso_date_time dateTime;
        calendar_id calendar = calendar_id::Iso8601;
    };
    temporal_date to_temporal_date(value aItem, value aOptions = Undefined);
    time_record to_temporal_time(value aItem, value aOptions = Undefined);
    time_record to_time_record_or_midnight(value aItem);
    temporal_date_time to_temporal_date_time(value aItem, value aOptions = Undefined);
    temporal_date to_temporal_year_month(value aItem, value aOptions = Undefined);
    temporal_date to_temporal_month_day(value aItem, value aOptions = Undefined);
    temporal_object& to_temporal_zoned_date_time(value aItem, value aOptions = Undefined);
    bool is_partial_temporal_object(value aValue);
    value round_to_options(value aRoundTo, char const* aKey);   ///< (round's and total's argument: a string becomes { [aKey]: string })
    value string_value(std::string const& aText);

    enum calendar_field_id : std::uint32_t
    {
        CalCalendarId, CalEra, CalEraYear, CalYear, CalMonth, CalMonthCode, CalDay, CalDayOfWeek, CalDayOfYear, CalWeekOfYear, CalYearOfWeek,
        CalDaysInWeek, CalDaysInMonth, CalDaysInYear, CalMonthsInYear, CalInLeapYear
    };
    value calendar_date_field(calendar_id aCalendar, iso_date const& aDate, std::uint32_t aField);     ///< (temporal_plain.cpp: the calendar getters)
    char const* calendar_field_name(std::uint32_t aField);
    value value_of_throws(value aThis, arguments aArguments, value aNewTarget, function_object& aSelf);

    void init_temporal_plain(realm& aRealm, value aNamespace);    ///< (temporal_plain.cpp)
    void init_temporal_zoned(realm& aRealm, value aNamespace);    ///< (temporal_zoned.cpp)
    value make_temporal_constructor(realm& aRealm, value aNamespace, char const* aName, std::uint32_t aLength, native_function aFunction,
        value realm::*aPrototype, value realm::*aConstructor, char const* aTag);
}
