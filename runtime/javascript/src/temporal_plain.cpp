/*
  temporal_plain.cpp

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

// Temporal: Temporal.PlainDate, Temporal.PlainTime, Temporal.PlainDateTime, Temporal.PlainYearMonth and
// Temporal.PlainMonthDay (ISO 8601 calendar).

#include <cmath>
#include "temporal.hpp"
#include "intl.hpp"

namespace neos::javascript_runtime::temporal
{
    value value_of_throws(value, arguments, value, function_object&)
    {
        throw_type_error("Temporal objects cannot be converted to primitives with valueOf; use compare() or equals()");
    }

    char const* calendar_field_name(std::uint32_t aField)
    {
        static char const* const sNames[] = { "calendarId", "era", "eraYear", "year", "month", "monthCode", "day", "dayOfWeek", "dayOfYear",
            "weekOfYear", "yearOfWeek", "daysInWeek", "daysInMonth", "daysInYear", "monthsInYear", "inLeapYear" };
        return sNames[aField];
    }

    value calendar_date_field(calendar_id aCalendar, iso_date const& aDate, std::uint32_t aField)
    {
        if (aField == CalCalendarId)
            return calendar_value(aCalendar);
        if (aCalendar == calendar_id::Iso8601)
        {
            switch (aField)
            {
            case CalEra: return Undefined;
            case CalEraYear: return Undefined;
            case CalYear: return number(static_cast<double>(aDate.year));
            case CalMonth: return number(aDate.month);
            case CalMonthCode: return string_value("M" + zero_pad(aDate.month, 2));
            case CalDay: return number(aDate.day);
            case CalDayOfWeek: return number(iso_day_of_week(aDate));
            case CalDayOfYear: return number(iso_day_of_year(aDate));
            case CalWeekOfYear: return number(iso_week_of_year(aDate).first);
            case CalYearOfWeek: return number(static_cast<double>(iso_week_of_year(aDate).second));
            case CalDaysInWeek: return number(7);
            case CalDaysInMonth: return number(iso_days_in_month(aDate.year, aDate.month));
            case CalDaysInYear: return number(is_leap_year(aDate.year) ? 366 : 365);
            case CalMonthsInYear: return number(12);
            case CalInLeapYear: return boolean(is_leap_year(aDate.year));
            default: return Undefined;
            }
        }
        auto const d = calendar_iso_to_date(aCalendar, aDate);
        switch (aField)
        {
        case CalEra: return d.era ? string_value(*d.era) : Undefined;
        case CalEraYear: return d.eraYear ? number(static_cast<double>(*d.eraYear)) : Undefined;
        case CalYear: return number(static_cast<double>(d.year));
        case CalMonth: return number(d.month);
        case CalMonthCode: return string_value(d.month_code());
        case CalDay: return number(d.day);
        case CalDayOfWeek: return number(d.dayOfWeek);
        case CalDayOfYear: return number(d.dayOfYear);
        case CalWeekOfYear: return d.weekOfYear ? number(*d.weekOfYear) : Undefined;
        case CalYearOfWeek: return d.yearOfWeek ? number(static_cast<double>(*d.yearOfWeek)) : Undefined;
        case CalDaysInWeek: return number(d.daysInWeek);
        case CalDaysInMonth: return number(d.daysInMonth);
        case CalDaysInYear: return number(d.daysInYear);
        case CalMonthsInYear: return number(d.monthsInYear);
        case CalInLeapYear: return boolean(d.inLeapYear);
        default: return Undefined;
        }
    }

    namespace
    {
        calendar_id calendar_argument(value aCalendar)
        {
            if (is_undefined(aCalendar))
                return calendar_id::Iso8601;
            if (!is_string(aCalendar))
                throw_type_error("calendar must be a string");
            return canonicalize_calendar(aCalendar);
        }

        void check_same_calendar(calendar_id a, calendar_id b)
        {
            if (a != b)
                throw_range_error("the calendars are not the same");
        }

        double integer_argument(arguments aArguments, std::size_t aIndex, double aDefault)
        {
            auto const v = argument(aArguments, aIndex);
            return is_undefined(v) && !std::isnan(aDefault) ? aDefault : to_integer_with_truncation(v);
        }

        void define_calendar_getters(value aPrototype, native_function aGetter, std::initializer_list<std::uint32_t> aFields)
        {
            for (auto const f : aFields)
                as_function(getter(aPrototype, ascii(calendar_field_name(f)), aGetter))->nativeId = f;
        }

        // --- Temporal.PlainDate

        temporal_object const& this_date(value aThis, char const* aName)
        {
            return this_temporal(aThis, object_class::TemporalPlainDate, aName);
        }

        value plain_date_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (is_undefined(aNewTarget))
                throw_type_error("Temporal.PlainDate must be called with new");
            auto const y = to_integer_with_truncation(argument(aArguments, 0u));
            auto const m = to_integer_with_truncation(argument(aArguments, 1u));
            auto const d = to_integer_with_truncation(argument(aArguments, 2u));
            auto const calendar = calendar_argument(argument(aArguments, 3u));
            if (!is_valid_iso_date(clamp_int(y), clamp_int(m), clamp_int(d)))
                throw_range_error("invalid ISO date");
            return create_plain_date({ clamp_int(y), static_cast<int>(m), static_cast<int>(d) }, calendar, aNewTarget);
        }

        value plain_date_from(value, arguments aArguments, value, function_object&)
        {
            auto const r = to_temporal_date(argument(aArguments, 0u), argument(aArguments, 1u));
            return create_plain_date(r.date, r.calendar);
        }

        value plain_date_compare(value, arguments aArguments, value, function_object&)
        {
            auto const one = to_temporal_date(argument(aArguments, 0u));
            auto const two = to_temporal_date(argument(aArguments, 1u));
            return number(compare_iso_date(one.date, two.date));
        }

        value plain_date_field(value aThis, arguments, value, function_object& aSelf)
        {
            auto const& o = this_date(aThis, "Temporal.PlainDate getter");
            return calendar_date_field(o.calendar, o.date, aSelf.nativeId);
        }

        value plain_date_to_plain_year_month(value aThis, arguments, value, function_object&)
        {
            auto const& o = this_date(aThis, "Temporal.PlainDate.prototype.toPlainYearMonth");
            return create_plain_year_month(calendar_year_month_from_fields(o.calendar, iso_date_to_fields(o.calendar, o.date, 0), overflow::Constrain),
                o.calendar);
        }

        value plain_date_to_plain_month_day(value aThis, arguments, value, function_object&)
        {
            auto const& o = this_date(aThis, "Temporal.PlainDate.prototype.toPlainMonthDay");
            return create_plain_month_day(calendar_month_day_from_fields(o.calendar, iso_date_to_fields(o.calendar, o.date, 0), overflow::Constrain),
                o.calendar);
        }

        value add_duration_to_date(bool aSubtract, temporal_object const& aDate, value aLike, value aOptions)
        {
            auto const date = aDate.date;
            auto const calendar = aDate.calendar;
            auto duration = to_temporal_duration(aLike);
            if (aSubtract)
                duration = negate_duration(duration);
            auto const dateDuration = to_date_duration_without_time(duration);
            auto const overflow = get_overflow_option(get_options_object(aOptions));
            return create_plain_date(calendar_date_add(calendar, date, dateDuration, overflow), calendar);
        }

        value plain_date_add(value aThis, arguments aArguments, value, function_object&)
        {
            return add_duration_to_date(false, this_date(aThis, "Temporal.PlainDate.prototype.add"), argument(aArguments, 0u), argument(aArguments, 1u));
        }

        value plain_date_subtract(value aThis, arguments aArguments, value, function_object&)
        {
            return add_duration_to_date(true, this_date(aThis, "Temporal.PlainDate.prototype.subtract"), argument(aArguments, 0u),
                argument(aArguments, 1u));
        }

        value plain_date_with(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& o = this_date(aThis, "Temporal.PlainDate.prototype.with");
            auto const date = o.date;
            auto const calendar = o.calendar;
            auto const like = argument(aArguments, 0u);
            if (!is_partial_temporal_object(like))
                throw_type_error("with() requires a partial date object");
            auto fields = iso_date_to_fields(calendar, date, 0);
            auto const partial = prepare_calendar_fields(calendar, like, FieldsDate, 0u, true);
            fields = merge_calendar_fields(calendar, fields, partial);
            auto const overflow = get_overflow_option(get_options_object(argument(aArguments, 1u)));
            return create_plain_date(calendar_date_from_fields(calendar, fields, overflow), calendar);
        }

        value plain_date_with_calendar(value aThis, arguments aArguments, value, function_object&)
        {
            auto const date = this_date(aThis, "Temporal.PlainDate.prototype.withCalendar").date;
            auto const calendar = to_temporal_calendar_identifier(argument(aArguments, 0u));
            return create_plain_date(date, calendar);
        }

        value difference_temporal_plain_date(bool aSince, temporal_object const& aDate, value aOther, value aOptions)
        {
            auto const date = aDate.date;
            auto const calendar = aDate.calendar;
            auto const other = to_temporal_date(aOther);
            check_same_calendar(calendar, other.calendar);
            auto const options = get_options_object(aOptions);
            auto const settings = get_difference_settings(aSince, options, unit_group::Date, {}, unit::Day, unit::Day);
            if (compare_iso_date(date, other.date) == 0)
                return create_duration({});
            internal_duration duration{ calendar_date_until(calendar, date, other.date, settings.largestUnit), i128{} };
            if (settings.smallestUnit != unit::Day || settings.roundingIncrement != 1)
            {
                iso_date_time const dateTime{ date, {} };
                iso_date_time const otherDateTime{ other.date, {} };
                duration = round_relative_duration(duration, get_utc_epoch_ns(dateTime), get_utc_epoch_ns(otherDateTime), dateTime, calendar, nullptr,
                    settings.largestUnit, settings.roundingIncrement, settings.smallestUnit, settings.roundingMode);
            }
            auto result = duration_from_internal(duration, unit::Day);
            if (aSince)
                result = negate_duration(result);
            return create_duration(result);
        }

        value plain_date_until(value aThis, arguments aArguments, value, function_object&)
        {
            return difference_temporal_plain_date(false, this_date(aThis, "Temporal.PlainDate.prototype.until"), argument(aArguments, 0u),
                argument(aArguments, 1u));
        }

        value plain_date_since(value aThis, arguments aArguments, value, function_object&)
        {
            return difference_temporal_plain_date(true, this_date(aThis, "Temporal.PlainDate.prototype.since"), argument(aArguments, 0u),
                argument(aArguments, 1u));
        }

        value plain_date_equals(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& o = this_date(aThis, "Temporal.PlainDate.prototype.equals");
            auto const date = o.date;
            auto const calendar = o.calendar;
            auto const other = to_temporal_date(argument(aArguments, 0u));
            return boolean(compare_iso_date(date, other.date) == 0 && calendar == other.calendar);
        }

        value plain_date_to_plain_date_time(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& o = this_date(aThis, "Temporal.PlainDate.prototype.toPlainDateTime");
            auto const date = o.date;
            auto const calendar = o.calendar;
            auto const time = to_time_record_or_midnight(argument(aArguments, 0u));
            return create_plain_date_time({ date, time }, calendar);
        }

        value plain_date_to_zoned_date_time(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& o = this_date(aThis, "Temporal.PlainDate.prototype.toZonedDateTime");
            auto const date = o.date;
            auto const calendar = o.calendar;
            auto const item = argument(aArguments, 0u);
            time_zone timeZone;
            value temporalTime = Undefined;
            if (is_object(item))
            {
                auto const timeZoneLike = get(item, ascii("timeZone"));
                if (is_undefined(timeZoneLike))
                    timeZone = to_temporal_time_zone_identifier(item);
                else
                {
                    timeZone = to_temporal_time_zone_identifier(timeZoneLike);
                    temporalTime = get(item, ascii("plainTime"));
                }
            }
            else
                timeZone = to_temporal_time_zone_identifier(item);
            i128 epochNs;
            if (is_undefined(temporalTime))
                epochNs = get_start_of_day(timeZone, date);
            else
            {
                auto const time = to_temporal_time(temporalTime);
                iso_date_time const dateTime{ date, time };
                if (!iso_date_time_within_limits(dateTime))
                    throw_range_error("date-time is outside the representable range");
                epochNs = get_epoch_ns_for(timeZone, dateTime, disambiguation::Compatible);
            }
            return create_zoned_date_time(epochNs, timeZone, calendar);
        }

        std::string plain_date_string(temporal_object const& aDate, show_calendar aShowCalendar)
        {
            return iso_date_to_string(aDate.date) + format_calendar_annotation(aDate.calendar, aShowCalendar);
        }

        value plain_date_to_string(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& o = this_date(aThis, "Temporal.PlainDate.prototype.toString");
            auto const showCalendar = get_show_calendar_option(get_options_object(argument(aArguments, 0u)));
            return string_value(plain_date_string(o, showCalendar));
        }

        value plain_date_to_json(value aThis, arguments, value, function_object&)
        {
            return string_value(plain_date_string(this_date(aThis, "Temporal.PlainDate.prototype.toJSON"), show_calendar::Auto));
        }

        value plain_date_to_locale_string(value aThis, arguments aArguments, value, function_object&)
        {
#if defined(NEOS_JS_ICU)
            if (intl::icu_available())
            {
                this_date(aThis, "Temporal.PlainDate.prototype.toLocaleString");
                return intl::temporal_to_locale_string(aThis, argument(aArguments, 0u), argument(aArguments, 1u));
            }
#endif
            (void)aArguments;
            return string_value(plain_date_string(this_date(aThis, "Temporal.PlainDate.prototype.toLocaleString"), show_calendar::Auto));
        }

        // --- Temporal.PlainTime

        time_record const& this_time(value aThis, char const* aName)
        {
            return this_temporal(aThis, object_class::TemporalPlainTime, aName).time;
        }

        value plain_time_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (is_undefined(aNewTarget))
                throw_type_error("Temporal.PlainTime must be called with new");
            double f[6];
            for (std::size_t i = 0u; i < 6u; ++i)
                f[i] = integer_argument(aArguments, i, 0.0);
            if (!is_valid_time(f[0], f[1], f[2], f[3], f[4], f[5]))
                throw_range_error("invalid time");
            return create_plain_time(regulate_time(f[0], f[1], f[2], f[3], f[4], f[5], overflow::Reject), aNewTarget);
        }

        value plain_time_from(value, arguments aArguments, value, function_object&)
        {
            return create_plain_time(to_temporal_time(argument(aArguments, 0u), argument(aArguments, 1u)));
        }

        value plain_time_compare(value, arguments aArguments, value, function_object&)
        {
            auto const one = to_temporal_time(argument(aArguments, 0u));
            auto const two = to_temporal_time(argument(aArguments, 1u));
            return number(compare_time(one, two));
        }

        value time_field(time_record const& aTime, std::uint32_t aField)
        {
            switch (aField)
            {
            case 0: return number(aTime.hour);
            case 1: return number(aTime.minute);
            case 2: return number(aTime.second);
            case 3: return number(aTime.millisecond);
            case 4: return number(aTime.microsecond);
            default: return number(aTime.nanosecond);
            }
        }

        char const* const sTimeFields[] = { "hour", "minute", "second", "millisecond", "microsecond", "nanosecond" };

        value plain_time_field(value aThis, arguments, value, function_object& aSelf)
        {
            return time_field(this_time(aThis, "Temporal.PlainTime getter"), aSelf.nativeId);
        }

        value add_duration_to_time(bool aSubtract, time_record const& aTime, value aLike)
        {
            auto duration = to_temporal_duration(aLike);
            if (aSubtract)
                duration = negate_duration(duration);
            auto const internal = to_internal_duration(duration);
            return create_plain_time(add_time(aTime, internal.time));
        }

        value plain_time_add(value aThis, arguments aArguments, value, function_object&)
        {
            return add_duration_to_time(false, this_time(aThis, "Temporal.PlainTime.prototype.add"), argument(aArguments, 0u));
        }

        value plain_time_subtract(value aThis, arguments aArguments, value, function_object&)
        {
            return add_duration_to_time(true, this_time(aThis, "Temporal.PlainTime.prototype.subtract"), argument(aArguments, 0u));
        }

        value plain_time_with(value aThis, arguments aArguments, value, function_object&)
        {
            auto const time = this_time(aThis, "Temporal.PlainTime.prototype.with");
            auto const like = argument(aArguments, 0u);
            if (!is_partial_temporal_object(like))
                throw_type_error("with() requires a partial time object");
            double fields[6] = { static_cast<double>(time.hour), static_cast<double>(time.minute), static_cast<double>(time.second),
                static_cast<double>(time.millisecond), static_cast<double>(time.microsecond), static_cast<double>(time.nanosecond) };
            bool any = false;
            static struct { char const* name; int index; } const sOrder[] = {
                { "hour", 0 }, { "microsecond", 4 }, { "millisecond", 3 }, { "minute", 1 }, { "nanosecond", 5 }, { "second", 2 } };
            for (auto const& [name, index] : sOrder)
            {
                auto const v = get(like, ascii(name));
                if (!is_undefined(v))
                {
                    fields[index] = to_integer_with_truncation(v);
                    any = true;
                }
            }
            if (!any)
                throw_type_error("time-like object must have at least one time property");
            auto const overflow = get_overflow_option(get_options_object(argument(aArguments, 1u)));
            return create_plain_time(regulate_time(fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], overflow));
        }

        value difference_temporal_plain_time(bool aSince, time_record const& aTime, value aOther, value aOptions)
        {
            auto const other = to_temporal_time(aOther);
            auto const options = get_options_object(aOptions);
            auto const settings = get_difference_settings(aSince, options, unit_group::Time, {}, unit::Nanosecond, unit::Hour);
            auto const timeDuration = round_time_duration(difference_time(aTime, other), settings.roundingIncrement, settings.smallestUnit,
                settings.roundingMode);
            auto result = duration_from_internal({ {}, timeDuration }, settings.largestUnit);
            if (aSince)
                result = negate_duration(result);
            return create_duration(result);
        }

        value plain_time_until(value aThis, arguments aArguments, value, function_object&)
        {
            return difference_temporal_plain_time(false, this_time(aThis, "Temporal.PlainTime.prototype.until"), argument(aArguments, 0u),
                argument(aArguments, 1u));
        }

        value plain_time_since(value aThis, arguments aArguments, value, function_object&)
        {
            return difference_temporal_plain_time(true, this_time(aThis, "Temporal.PlainTime.prototype.since"), argument(aArguments, 0u),
                argument(aArguments, 1u));
        }

        value plain_time_round(value aThis, arguments aArguments, value, function_object&)
        {
            auto const time = this_time(aThis, "Temporal.PlainTime.prototype.round");
            auto const roundTo = round_to_options(argument(aArguments, 0u), "smallestUnit");
            auto const roundingIncrement = get_rounding_increment_option(roundTo);
            auto const roundingMode = get_rounding_mode_option(roundTo, rounding_mode::HalfExpand);
            auto const smallestUnit = get_unit_option(roundTo, "smallestUnit", true);
            validate_unit(smallestUnit, unit_group::Time);
            validate_rounding_increment(roundingIncrement, maximum_rounding_increment(smallestUnit), false);
            return create_plain_time(round_time(time, roundingIncrement, smallestUnit, roundingMode));
        }

        value plain_time_equals(value aThis, arguments aArguments, value, function_object&)
        {
            auto const time = this_time(aThis, "Temporal.PlainTime.prototype.equals");
            auto const other = to_temporal_time(argument(aArguments, 0u));
            return boolean(compare_time(time, other) == 0);
        }

        value plain_time_to_string(value aThis, arguments aArguments, value, function_object&)
        {
            auto const time = this_time(aThis, "Temporal.PlainTime.prototype.toString");
            auto const options = get_options_object(argument(aArguments, 0u));
            auto const digits = get_fractional_second_digits_option(options);
            auto const roundingMode = get_rounding_mode_option(options, rounding_mode::Trunc);
            auto const smallestUnit = get_unit_option(options, "smallestUnit", false);
            validate_unit(smallestUnit, unit_group::Time);
            if (smallestUnit == unit::Hour)
                throw_range_error("smallestUnit must not be hour");
            auto const precision = to_seconds_string_precision(smallestUnit, digits);
            auto const rounded = round_time(time, precision.increment, precision.u, roundingMode);
            return string_value(time_record_to_string(rounded, precision.precision));
        }

        value plain_time_to_json(value aThis, arguments, value, function_object&)
        {
            return string_value(time_record_to_string(this_time(aThis, "Temporal.PlainTime.prototype.toJSON"), -1));
        }

        value plain_time_to_locale_string(value aThis, arguments aArguments, value, function_object&)
        {
#if defined(NEOS_JS_ICU)
            if (intl::icu_available())
            {
                this_time(aThis, "Temporal.PlainTime.prototype.toLocaleString");
                return intl::temporal_to_locale_string(aThis, argument(aArguments, 0u), argument(aArguments, 1u));
            }
#endif
            (void)aArguments;
            return string_value(time_record_to_string(this_time(aThis, "Temporal.PlainTime.prototype.toLocaleString"), -1));
        }

        // --- Temporal.PlainDateTime

        temporal_object const& this_date_time_object(value aThis, char const* aName)
        {
            return this_temporal(aThis, object_class::TemporalPlainDateTime, aName);
        }

        iso_date_time this_date_time(value aThis, char const* aName)
        {
            auto const& o = this_date_time_object(aThis, aName);
            return { o.date, o.time };
        }

        value plain_date_time_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (is_undefined(aNewTarget))
                throw_type_error("Temporal.PlainDateTime must be called with new");
            auto const y = to_integer_with_truncation(argument(aArguments, 0u));
            auto const m = to_integer_with_truncation(argument(aArguments, 1u));
            auto const d = to_integer_with_truncation(argument(aArguments, 2u));
            double f[6];
            for (std::size_t i = 0u; i < 6u; ++i)
                f[i] = integer_argument(aArguments, 3u + i, 0.0);
            auto const calendar = calendar_argument(argument(aArguments, 9u));
            if (!is_valid_iso_date(clamp_int(y), clamp_int(m), clamp_int(d)))
                throw_range_error("invalid ISO date");
            if (!is_valid_time(f[0], f[1], f[2], f[3], f[4], f[5]))
                throw_range_error("invalid time");
            iso_date_time const dateTime{ { clamp_int(y), static_cast<int>(m), static_cast<int>(d) },
                regulate_time(f[0], f[1], f[2], f[3], f[4], f[5], overflow::Reject) };
            return create_plain_date_time(dateTime, calendar, aNewTarget);
        }

        value plain_date_time_from(value, arguments aArguments, value, function_object&)
        {
            auto const r = to_temporal_date_time(argument(aArguments, 0u), argument(aArguments, 1u));
            return create_plain_date_time(r.dateTime, r.calendar);
        }

        value plain_date_time_compare(value, arguments aArguments, value, function_object&)
        {
            auto const one = to_temporal_date_time(argument(aArguments, 0u));
            auto const two = to_temporal_date_time(argument(aArguments, 1u));
            return number(compare_iso_date_time(one.dateTime, two.dateTime));
        }

        value plain_date_time_field(value aThis, arguments, value, function_object& aSelf)
        {
            auto const& o = this_date_time_object(aThis, "Temporal.PlainDateTime getter");
            if (aSelf.nativeId >= 100u)
                return time_field(o.time, aSelf.nativeId - 100u);
            return calendar_date_field(o.calendar, o.date, aSelf.nativeId);
        }

        calendar_fields date_time_fields(calendar_id aCalendar, iso_date_time const& aDateTime)
        {
            auto fields = iso_date_to_fields(aCalendar, aDateTime.date, 0);
            fields.hour = aDateTime.time.hour;
            fields.minute = aDateTime.time.minute;
            fields.second = aDateTime.time.second;
            fields.millisecond = aDateTime.time.millisecond;
            fields.microsecond = aDateTime.time.microsecond;
            fields.nanosecond = aDateTime.time.nanosecond;
            return fields;
        }

        value plain_date_time_with(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& o = this_date_time_object(aThis, "Temporal.PlainDateTime.prototype.with");
            iso_date_time const dateTime{ o.date, o.time };
            auto const calendar = o.calendar;
            auto const like = argument(aArguments, 0u);
            if (!is_partial_temporal_object(like))
                throw_type_error("with() requires a partial date-time object");
            auto fields = date_time_fields(calendar, dateTime);
            auto const partial = prepare_calendar_fields(calendar, like, FieldsDate | FieldsTime, 0u, true);
            fields = merge_calendar_fields(calendar, fields, partial);
            auto const overflow = get_overflow_option(get_options_object(argument(aArguments, 1u)));
            return create_plain_date_time(interpret_temporal_date_time_fields(calendar, fields, overflow), calendar);
        }

        value plain_date_time_with_plain_time(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& o = this_date_time_object(aThis, "Temporal.PlainDateTime.prototype.withPlainTime");
            auto const date = o.date;
            auto const calendar = o.calendar;
            auto const time = to_time_record_or_midnight(argument(aArguments, 0u));
            return create_plain_date_time({ date, time }, calendar);
        }

        value plain_date_time_with_calendar(value aThis, arguments aArguments, value, function_object&)
        {
            auto const dateTime = this_date_time(aThis, "Temporal.PlainDateTime.prototype.withCalendar");
            auto const calendar = to_temporal_calendar_identifier(argument(aArguments, 0u));
            return create_plain_date_time(dateTime, calendar);
        }

        value add_duration_to_date_time(bool aSubtract, temporal_object const& aDateTime, value aLike, value aOptions)
        {
            iso_date_time const dateTime{ aDateTime.date, aDateTime.time };
            auto const calendar = aDateTime.calendar;
            auto duration = to_temporal_duration(aLike);
            if (aSubtract)
                duration = negate_duration(duration);
            auto const overflow = get_overflow_option(get_options_object(aOptions));
            auto const internal = to_internal_duration_24(duration);
            auto const timeResult = add_time(dateTime.time, internal.time);
            auto const dateDuration = create_date_duration(internal.date.years, internal.date.months, internal.date.weeks,
                static_cast<double>(timeResult.days));
            auto const addedDate = calendar_date_add(calendar, dateTime.date, dateDuration, overflow);
            return create_plain_date_time({ addedDate, timeResult }, calendar);
        }

        value plain_date_time_add(value aThis, arguments aArguments, value, function_object&)
        {
            return add_duration_to_date_time(false, this_date_time_object(aThis, "Temporal.PlainDateTime.prototype.add"), argument(aArguments, 0u),
                argument(aArguments, 1u));
        }

        value plain_date_time_subtract(value aThis, arguments aArguments, value, function_object&)
        {
            return add_duration_to_date_time(true, this_date_time_object(aThis, "Temporal.PlainDateTime.prototype.subtract"), argument(aArguments, 0u),
                argument(aArguments, 1u));
        }

        value difference_temporal_plain_date_time(bool aSince, temporal_object const& aDateTime, value aOther, value aOptions)
        {
            iso_date_time const dateTime{ aDateTime.date, aDateTime.time };
            auto const calendar = aDateTime.calendar;
            auto const other = to_temporal_date_time(aOther);
            check_same_calendar(calendar, other.calendar);
            auto const options = get_options_object(aOptions);
            auto const settings = get_difference_settings(aSince, options, unit_group::DateTime, {}, unit::Nanosecond, unit::Day);
            if (compare_iso_date_time(dateTime, other.dateTime) == 0)
                return create_duration({});
            auto const internal = difference_plain_date_time_with_rounding(calendar, dateTime, other.dateTime, settings.largestUnit,
                settings.roundingIncrement, settings.smallestUnit, settings.roundingMode);
            auto result = duration_from_internal(internal, settings.largestUnit);
            if (aSince)
                result = negate_duration(result);
            return create_duration(result);
        }

        value plain_date_time_until(value aThis, arguments aArguments, value, function_object&)
        {
            return difference_temporal_plain_date_time(false, this_date_time_object(aThis, "Temporal.PlainDateTime.prototype.until"),
                argument(aArguments, 0u), argument(aArguments, 1u));
        }

        value plain_date_time_since(value aThis, arguments aArguments, value, function_object&)
        {
            return difference_temporal_plain_date_time(true, this_date_time_object(aThis, "Temporal.PlainDateTime.prototype.since"),
                argument(aArguments, 0u), argument(aArguments, 1u));
        }

        value plain_date_time_round(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& o = this_date_time_object(aThis, "Temporal.PlainDateTime.prototype.round");
            iso_date_time const dateTime{ o.date, o.time };
            auto const calendar = o.calendar;
            auto const roundTo = round_to_options(argument(aArguments, 0u), "smallestUnit");
            auto const roundingIncrement = get_rounding_increment_option(roundTo);
            auto const roundingMode = get_rounding_mode_option(roundTo, rounding_mode::HalfExpand);
            auto const smallestUnit = get_unit_option(roundTo, "smallestUnit", true);
            validate_unit(smallestUnit, unit_group::Time, { unit::Day });
            if (smallestUnit == unit::Day)
                validate_rounding_increment(roundingIncrement, 1, true);
            else
                validate_rounding_increment(roundingIncrement, maximum_rounding_increment(smallestUnit), false);
            if (smallestUnit == unit::Nanosecond && roundingIncrement == 1)
                return create_plain_date_time(dateTime, calendar);
            return create_plain_date_time(round_iso_date_time(dateTime, roundingIncrement, smallestUnit, roundingMode), calendar);
        }

        value plain_date_time_equals(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& o = this_date_time_object(aThis, "Temporal.PlainDateTime.prototype.equals");
            iso_date_time const dateTime{ o.date, o.time };
            auto const calendar = o.calendar;
            auto const other = to_temporal_date_time(argument(aArguments, 0u));
            return boolean(compare_iso_date_time(dateTime, other.dateTime) == 0 && calendar == other.calendar);
        }

        value plain_date_time_to_string(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& o = this_date_time_object(aThis, "Temporal.PlainDateTime.prototype.toString");
            iso_date_time const dateTime{ o.date, o.time };
            auto const calendar = o.calendar;
            auto const options = get_options_object(argument(aArguments, 0u));
            auto const showCalendar = get_show_calendar_option(options);
            auto const digits = get_fractional_second_digits_option(options);
            auto const roundingMode = get_rounding_mode_option(options, rounding_mode::Trunc);
            auto const smallestUnit = get_unit_option(options, "smallestUnit", false);
            validate_unit(smallestUnit, unit_group::Time);
            if (smallestUnit == unit::Hour)
                throw_range_error("smallestUnit must not be hour");
            auto const precision = to_seconds_string_precision(smallestUnit, digits);
            auto const result = round_iso_date_time(dateTime, precision.increment, precision.u, roundingMode);
            if (!iso_date_time_within_limits(result))
                throw_range_error("date-time is outside the representable range");
            return string_value(iso_date_time_to_string(result, precision.precision, calendar, showCalendar));
        }

        value plain_date_time_to_json(value aThis, arguments, value, function_object&)
        {
            auto const& o = this_date_time_object(aThis, "Temporal.PlainDateTime.prototype.toJSON");
            return string_value(iso_date_time_to_string({ o.date, o.time }, -1, o.calendar, show_calendar::Auto));
        }

        value plain_date_time_to_locale_string(value aThis, arguments aArguments, value, function_object&)
        {
#if defined(NEOS_JS_ICU)
            if (intl::icu_available())
            {
                this_date_time(aThis, "Temporal.PlainDateTime.prototype.toLocaleString");
                return intl::temporal_to_locale_string(aThis, argument(aArguments, 0u), argument(aArguments, 1u));
            }
#endif
            (void)aArguments;
            auto const& o = this_date_time_object(aThis, "Temporal.PlainDateTime.prototype.toLocaleString");
            return string_value(iso_date_time_to_string({ o.date, o.time }, -1, o.calendar, show_calendar::Auto));
        }

        value plain_date_time_to_zoned_date_time(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& o = this_date_time_object(aThis, "Temporal.PlainDateTime.prototype.toZonedDateTime");
            iso_date_time const dateTime{ o.date, o.time };
            auto const calendar = o.calendar;
            auto const timeZone = to_temporal_time_zone_identifier(argument(aArguments, 0u));
            auto const disambiguationOption = get_disambiguation_option(get_options_object(argument(aArguments, 1u)));
            auto const epochNs = get_epoch_ns_for(timeZone, dateTime, disambiguationOption);
            return create_zoned_date_time(epochNs, timeZone, calendar);
        }

        value plain_date_time_to_plain_date(value aThis, arguments, value, function_object&)
        {
            auto const& o = this_date_time_object(aThis, "Temporal.PlainDateTime.prototype.toPlainDate");
            return create_plain_date(o.date, o.calendar);
        }

        value plain_date_time_to_plain_time(value aThis, arguments, value, function_object&)
        {
            return create_plain_time(this_date_time(aThis, "Temporal.PlainDateTime.prototype.toPlainTime").time);
        }

        // --- Temporal.PlainYearMonth

        temporal_object const& this_year_month(value aThis, char const* aName)
        {
            return this_temporal(aThis, object_class::TemporalPlainYearMonth, aName);
        }

        value plain_year_month_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (is_undefined(aNewTarget))
                throw_type_error("Temporal.PlainYearMonth must be called with new");
            auto const y = to_integer_with_truncation(argument(aArguments, 0u));
            auto const m = to_integer_with_truncation(argument(aArguments, 1u));
            auto const calendar = calendar_argument(argument(aArguments, 2u));
            auto const ref = integer_argument(aArguments, 3u, 1.0);
            if (!is_valid_iso_date(clamp_int(y), clamp_int(m), clamp_int(ref)))
                throw_range_error("invalid ISO date");
            return create_plain_year_month({ clamp_int(y), static_cast<int>(m), static_cast<int>(ref) }, calendar, aNewTarget);
        }

        value plain_year_month_from(value, arguments aArguments, value, function_object&)
        {
            auto const r = to_temporal_year_month(argument(aArguments, 0u), argument(aArguments, 1u));
            return create_plain_year_month(r.date, r.calendar);
        }

        value plain_year_month_compare(value, arguments aArguments, value, function_object&)
        {
            auto const one = to_temporal_year_month(argument(aArguments, 0u));
            auto const two = to_temporal_year_month(argument(aArguments, 1u));
            return number(compare_iso_date(one.date, two.date));
        }

        value plain_year_month_field(value aThis, arguments, value, function_object& aSelf)
        {
            auto const& o = this_year_month(aThis, "Temporal.PlainYearMonth getter");
            return calendar_date_field(o.calendar, o.date, aSelf.nativeId);
        }

        value plain_year_month_with(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& o = this_year_month(aThis, "Temporal.PlainYearMonth.prototype.with");
            auto const date = o.date;
            auto const calendar = o.calendar;
            auto const like = argument(aArguments, 0u);
            if (!is_partial_temporal_object(like))
                throw_type_error("with() requires a partial year-month object");
            auto fields = iso_date_to_fields(calendar, date, 1);
            auto const partial = prepare_calendar_fields(calendar, like, FieldYear | FieldMonth | FieldMonthCode, 0u, true);
            fields = merge_calendar_fields(calendar, fields, partial);
            auto const overflow = get_overflow_option(get_options_object(argument(aArguments, 1u)));
            return create_plain_year_month(calendar_year_month_from_fields(calendar, fields, overflow), calendar);
        }

        value add_duration_to_year_month(bool aSubtract, temporal_object const& aYearMonth, value aLike, value aOptions)
        {
            auto const yearMonth = aYearMonth.date;
            auto const calendar = aYearMonth.calendar;
            auto duration = to_temporal_duration(aLike);
            if (aSubtract)
                duration = negate_duration(duration);
            auto const internal = to_internal_duration(duration);
            auto const overflow = get_overflow_option(get_options_object(aOptions));
            auto const& durationToAdd = internal.date;
            if (durationToAdd.weeks != 0.0 || durationToAdd.days != 0.0 || !internal.time.zero())
                throw_range_error("only years and months can be added to a year-month");
            auto fields = iso_date_to_fields(calendar, yearMonth, 1);
            fields.day = 1.0;
            auto const date = calendar_date_from_fields(calendar, fields, overflow::Constrain);
            auto const addedDate = calendar_date_add(calendar, date, durationToAdd, overflow);
            auto const addedFields = iso_date_to_fields(calendar, addedDate, 1);
            return create_plain_year_month(calendar_year_month_from_fields(calendar, addedFields, overflow), calendar);
        }

        value plain_year_month_add(value aThis, arguments aArguments, value, function_object&)
        {
            return add_duration_to_year_month(false, this_year_month(aThis, "Temporal.PlainYearMonth.prototype.add"), argument(aArguments, 0u),
                argument(aArguments, 1u));
        }

        value plain_year_month_subtract(value aThis, arguments aArguments, value, function_object&)
        {
            return add_duration_to_year_month(true, this_year_month(aThis, "Temporal.PlainYearMonth.prototype.subtract"), argument(aArguments, 0u),
                argument(aArguments, 1u));
        }

        value difference_temporal_plain_year_month(bool aSince, temporal_object const& aYearMonth, value aOther, value aOptions)
        {
            auto const yearMonth = aYearMonth.date;
            auto const calendar = aYearMonth.calendar;
            auto const other = to_temporal_year_month(aOther);
            check_same_calendar(calendar, other.calendar);
            auto const options = get_options_object(aOptions);
            auto const settings = get_difference_settings(aSince, options, unit_group::Date, { unit::Week, unit::Day }, unit::Month, unit::Year);
            if (compare_iso_date(yearMonth, other.date) == 0)
                return create_duration({});
            auto thisFields = iso_date_to_fields(calendar, yearMonth, 1);
            thisFields.day = 1.0;
            auto const thisDate = calendar_date_from_fields(calendar, thisFields, overflow::Constrain);
            auto otherFields = iso_date_to_fields(calendar, other.date, 1);
            otherFields.day = 1.0;
            auto const otherDate = calendar_date_from_fields(calendar, otherFields, overflow::Constrain);
            auto const dateDifference = calendar_date_until(calendar, thisDate, otherDate, settings.largestUnit);
            internal_duration duration{ create_date_duration(dateDifference.years, dateDifference.months, 0.0, 0.0), i128{} };
            if (settings.smallestUnit != unit::Month || settings.roundingIncrement != 1)
            {
                iso_date_time const dateTime{ thisDate, {} };
                iso_date_time const otherDateTime{ otherDate, {} };
                duration = round_relative_duration(duration, get_utc_epoch_ns(dateTime), get_utc_epoch_ns(otherDateTime), dateTime, calendar, nullptr,
                    settings.largestUnit, settings.roundingIncrement, settings.smallestUnit, settings.roundingMode);
            }
            auto result = duration_from_internal(duration, unit::Day);
            if (aSince)
                result = negate_duration(result);
            return create_duration(result);
        }

        value plain_year_month_until(value aThis, arguments aArguments, value, function_object&)
        {
            return difference_temporal_plain_year_month(false, this_year_month(aThis, "Temporal.PlainYearMonth.prototype.until"),
                argument(aArguments, 0u), argument(aArguments, 1u));
        }

        value plain_year_month_since(value aThis, arguments aArguments, value, function_object&)
        {
            return difference_temporal_plain_year_month(true, this_year_month(aThis, "Temporal.PlainYearMonth.prototype.since"),
                argument(aArguments, 0u), argument(aArguments, 1u));
        }

        value plain_year_month_equals(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& o = this_year_month(aThis, "Temporal.PlainYearMonth.prototype.equals");
            auto const date = o.date;
            auto const calendar = o.calendar;
            auto const other = to_temporal_year_month(argument(aArguments, 0u));
            return boolean(compare_iso_date(date, other.date) == 0 && calendar == other.calendar);
        }

        std::string year_month_string(temporal_object const& aYearMonth, show_calendar aShowCalendar)
        {
            auto const& date = aYearMonth.date;
            auto result = pad_iso_year(date.year) + "-" + zero_pad(date.month, 2);
            if (aShowCalendar == show_calendar::Always || aShowCalendar == show_calendar::Critical || aYearMonth.calendar != calendar_id::Iso8601)
                result += "-" + zero_pad(date.day, 2);
            return result + format_calendar_annotation(aYearMonth.calendar, aShowCalendar);
        }

        value plain_year_month_to_string(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& o = this_year_month(aThis, "Temporal.PlainYearMonth.prototype.toString");
            auto const showCalendar = get_show_calendar_option(get_options_object(argument(aArguments, 0u)));
            return string_value(year_month_string(o, showCalendar));
        }

        value plain_year_month_to_json(value aThis, arguments, value, function_object&)
        {
            return string_value(year_month_string(this_year_month(aThis, "Temporal.PlainYearMonth.prototype.toJSON"), show_calendar::Auto));
        }

        value plain_year_month_to_locale_string(value aThis, arguments aArguments, value, function_object&)
        {
#if defined(NEOS_JS_ICU)
            if (intl::icu_available())
            {
                this_year_month(aThis, "Temporal.PlainYearMonth.prototype.toLocaleString");
                return intl::temporal_to_locale_string(aThis, argument(aArguments, 0u), argument(aArguments, 1u));
            }
#endif
            (void)aArguments;
            return string_value(year_month_string(this_year_month(aThis, "Temporal.PlainYearMonth.prototype.toLocaleString"), show_calendar::Auto));
        }

        value plain_year_month_to_plain_date(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& o = this_year_month(aThis, "Temporal.PlainYearMonth.prototype.toPlainDate");
            auto const date = o.date;
            auto const calendar = o.calendar;
            auto const item = argument(aArguments, 0u);
            if (!is_object(item))
                throw_type_error("toPlainDate requires an object");
            auto const fields = iso_date_to_fields(calendar, date, 1);
            auto const input = prepare_calendar_fields(calendar, item, FieldDay, 0u, false);
            auto const merged = merge_calendar_fields(calendar, fields, input);
            return create_plain_date(calendar_date_from_fields(calendar, merged, overflow::Constrain), calendar);
        }

        // --- Temporal.PlainMonthDay

        temporal_object const& this_month_day(value aThis, char const* aName)
        {
            return this_temporal(aThis, object_class::TemporalPlainMonthDay, aName);
        }

        value plain_month_day_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (is_undefined(aNewTarget))
                throw_type_error("Temporal.PlainMonthDay must be called with new");
            auto const m = to_integer_with_truncation(argument(aArguments, 0u));
            auto const d = to_integer_with_truncation(argument(aArguments, 1u));
            auto const calendar = calendar_argument(argument(aArguments, 2u));
            auto const y = integer_argument(aArguments, 3u, 1972.0);
            if (!is_valid_iso_date(clamp_int(y), clamp_int(m), clamp_int(d)))
                throw_range_error("invalid ISO date");
            return create_plain_month_day({ clamp_int(y), static_cast<int>(m), static_cast<int>(d) }, calendar, aNewTarget);
        }

        value plain_month_day_from(value, arguments aArguments, value, function_object&)
        {
            auto const r = to_temporal_month_day(argument(aArguments, 0u), argument(aArguments, 1u));
            return create_plain_month_day(r.date, r.calendar);
        }

        value plain_month_day_field(value aThis, arguments, value, function_object& aSelf)
        {
            auto const& o = this_month_day(aThis, "Temporal.PlainMonthDay getter");
            return calendar_date_field(o.calendar, o.date, aSelf.nativeId);
        }

        value plain_month_day_with(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& o = this_month_day(aThis, "Temporal.PlainMonthDay.prototype.with");
            auto const date = o.date;
            auto const calendar = o.calendar;
            auto const like = argument(aArguments, 0u);
            if (!is_partial_temporal_object(like))
                throw_type_error("with() requires a partial month-day object");
            auto fields = iso_date_to_fields(calendar, date, 2);
            auto const partial = prepare_calendar_fields(calendar, like, FieldsDate, 0u, true);
            fields = merge_calendar_fields(calendar, fields, partial);
            auto const overflow = get_overflow_option(get_options_object(argument(aArguments, 1u)));
            return create_plain_month_day(calendar_month_day_from_fields(calendar, fields, overflow), calendar);
        }

        value plain_month_day_equals(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& o = this_month_day(aThis, "Temporal.PlainMonthDay.prototype.equals");
            auto const date = o.date;
            auto const calendar = o.calendar;
            auto const other = to_temporal_month_day(argument(aArguments, 0u));
            return boolean(compare_iso_date(date, other.date) == 0 && calendar == other.calendar);
        }

        std::string month_day_string(temporal_object const& aMonthDay, show_calendar aShowCalendar)
        {
            auto const& date = aMonthDay.date;
            auto result = zero_pad(date.month, 2) + "-" + zero_pad(date.day, 2);
            if (aShowCalendar == show_calendar::Always || aShowCalendar == show_calendar::Critical || aMonthDay.calendar != calendar_id::Iso8601)
                result = pad_iso_year(date.year) + "-" + result;
            return result + format_calendar_annotation(aMonthDay.calendar, aShowCalendar);
        }

        value plain_month_day_to_string(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& o = this_month_day(aThis, "Temporal.PlainMonthDay.prototype.toString");
            auto const showCalendar = get_show_calendar_option(get_options_object(argument(aArguments, 0u)));
            return string_value(month_day_string(o, showCalendar));
        }

        value plain_month_day_to_json(value aThis, arguments, value, function_object&)
        {
            return string_value(month_day_string(this_month_day(aThis, "Temporal.PlainMonthDay.prototype.toJSON"), show_calendar::Auto));
        }

        value plain_month_day_to_locale_string(value aThis, arguments aArguments, value, function_object&)
        {
#if defined(NEOS_JS_ICU)
            if (intl::icu_available())
            {
                this_month_day(aThis, "Temporal.PlainMonthDay.prototype.toLocaleString");
                return intl::temporal_to_locale_string(aThis, argument(aArguments, 0u), argument(aArguments, 1u));
            }
#endif
            (void)aArguments;
            return string_value(month_day_string(this_month_day(aThis, "Temporal.PlainMonthDay.prototype.toLocaleString"), show_calendar::Auto));
        }

        value plain_month_day_to_plain_date(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& o = this_month_day(aThis, "Temporal.PlainMonthDay.prototype.toPlainDate");
            auto const date = o.date;
            auto const calendar = o.calendar;
            auto const item = argument(aArguments, 0u);
            if (!is_object(item))
                throw_type_error("toPlainDate requires an object");
            auto const fields = iso_date_to_fields(calendar, date, 2);
            auto const input = prepare_calendar_fields(calendar, item, FieldYear, 0u, false);
            auto const merged = merge_calendar_fields(calendar, fields, input);
            return create_plain_date(calendar_date_from_fields(calendar, merged, overflow::Constrain), calendar);
        }
    }

    void init_temporal_plain(realm& aRealm, value aNamespace)
    {
        // Temporal.PlainDate
        auto const dc = make_temporal_constructor(aRealm, aNamespace, "PlainDate", 3u, plain_date_constructor, &realm::temporalPlainDatePrototype,
            &realm::temporalPlainDate, "Temporal.PlainDate");
        auto const dp = aRealm.temporalPlainDatePrototype;
        method(dc, "from", 1u, plain_date_from);
        method(dc, "compare", 2u, plain_date_compare);
        define_calendar_getters(dp, plain_date_field, { CalCalendarId, CalEra, CalEraYear, CalYear, CalMonth, CalMonthCode, CalDay, CalDayOfWeek,
            CalDayOfYear, CalWeekOfYear, CalYearOfWeek, CalDaysInWeek, CalDaysInMonth, CalDaysInYear, CalMonthsInYear, CalInLeapYear });
        method(dp, "toPlainYearMonth", 0u, plain_date_to_plain_year_month);
        method(dp, "toPlainMonthDay", 0u, plain_date_to_plain_month_day);
        method(dp, "add", 1u, plain_date_add);
        method(dp, "subtract", 1u, plain_date_subtract);
        method(dp, "with", 1u, plain_date_with);
        method(dp, "withCalendar", 1u, plain_date_with_calendar);
        method(dp, "until", 1u, plain_date_until);
        method(dp, "since", 1u, plain_date_since);
        method(dp, "equals", 1u, plain_date_equals);
        method(dp, "toPlainDateTime", 0u, plain_date_to_plain_date_time);
        method(dp, "toZonedDateTime", 1u, plain_date_to_zoned_date_time);
        method(dp, "toString", 0u, plain_date_to_string);
        method(dp, "toLocaleString", 0u, plain_date_to_locale_string);
        method(dp, "toJSON", 0u, plain_date_to_json);
        method(dp, "valueOf", 0u, value_of_throws);

        // Temporal.PlainTime
        auto const tc = make_temporal_constructor(aRealm, aNamespace, "PlainTime", 0u, plain_time_constructor, &realm::temporalPlainTimePrototype,
            &realm::temporalPlainTime, "Temporal.PlainTime");
        auto const tp = aRealm.temporalPlainTimePrototype;
        method(tc, "from", 1u, plain_time_from);
        method(tc, "compare", 2u, plain_time_compare);
        for (std::uint32_t i = 0u; i < 6u; ++i)
            as_function(getter(tp, ascii(sTimeFields[i]), plain_time_field))->nativeId = i;
        method(tp, "add", 1u, plain_time_add);
        method(tp, "subtract", 1u, plain_time_subtract);
        method(tp, "with", 1u, plain_time_with);
        method(tp, "until", 1u, plain_time_until);
        method(tp, "since", 1u, plain_time_since);
        method(tp, "round", 1u, plain_time_round);
        method(tp, "equals", 1u, plain_time_equals);
        method(tp, "toString", 0u, plain_time_to_string);
        method(tp, "toLocaleString", 0u, plain_time_to_locale_string);
        method(tp, "toJSON", 0u, plain_time_to_json);
        method(tp, "valueOf", 0u, value_of_throws);

        // Temporal.PlainDateTime
        auto const dtc = make_temporal_constructor(aRealm, aNamespace, "PlainDateTime", 3u, plain_date_time_constructor,
            &realm::temporalPlainDateTimePrototype, &realm::temporalPlainDateTime, "Temporal.PlainDateTime");
        auto const dtp = aRealm.temporalPlainDateTimePrototype;
        method(dtc, "from", 1u, plain_date_time_from);
        method(dtc, "compare", 2u, plain_date_time_compare);
        define_calendar_getters(dtp, plain_date_time_field, { CalCalendarId, CalEra, CalEraYear, CalYear, CalMonth, CalMonthCode, CalDay });
        for (std::uint32_t i = 0u; i < 6u; ++i)
            as_function(getter(dtp, ascii(sTimeFields[i]), plain_date_time_field))->nativeId = 100u + i;
        define_calendar_getters(dtp, plain_date_time_field, { CalDayOfWeek, CalDayOfYear, CalWeekOfYear, CalYearOfWeek, CalDaysInWeek,
            CalDaysInMonth, CalDaysInYear, CalMonthsInYear, CalInLeapYear });
        method(dtp, "with", 1u, plain_date_time_with);
        method(dtp, "withPlainTime", 0u, plain_date_time_with_plain_time);
        method(dtp, "withCalendar", 1u, plain_date_time_with_calendar);
        method(dtp, "add", 1u, plain_date_time_add);
        method(dtp, "subtract", 1u, plain_date_time_subtract);
        method(dtp, "until", 1u, plain_date_time_until);
        method(dtp, "since", 1u, plain_date_time_since);
        method(dtp, "round", 1u, plain_date_time_round);
        method(dtp, "equals", 1u, plain_date_time_equals);
        method(dtp, "toString", 0u, plain_date_time_to_string);
        method(dtp, "toLocaleString", 0u, plain_date_time_to_locale_string);
        method(dtp, "toJSON", 0u, plain_date_time_to_json);
        method(dtp, "valueOf", 0u, value_of_throws);
        method(dtp, "toZonedDateTime", 1u, plain_date_time_to_zoned_date_time);
        method(dtp, "toPlainDate", 0u, plain_date_time_to_plain_date);
        method(dtp, "toPlainTime", 0u, plain_date_time_to_plain_time);

        // Temporal.PlainYearMonth
        auto const ymc = make_temporal_constructor(aRealm, aNamespace, "PlainYearMonth", 2u, plain_year_month_constructor,
            &realm::temporalPlainYearMonthPrototype, &realm::temporalPlainYearMonth, "Temporal.PlainYearMonth");
        auto const ymp = aRealm.temporalPlainYearMonthPrototype;
        method(ymc, "from", 1u, plain_year_month_from);
        method(ymc, "compare", 2u, plain_year_month_compare);
        define_calendar_getters(ymp, plain_year_month_field, { CalCalendarId, CalEra, CalEraYear, CalYear, CalMonth, CalMonthCode, CalDaysInYear,
            CalDaysInMonth, CalMonthsInYear, CalInLeapYear });
        method(ymp, "with", 1u, plain_year_month_with);
        method(ymp, "add", 1u, plain_year_month_add);
        method(ymp, "subtract", 1u, plain_year_month_subtract);
        method(ymp, "until", 1u, plain_year_month_until);
        method(ymp, "since", 1u, plain_year_month_since);
        method(ymp, "equals", 1u, plain_year_month_equals);
        method(ymp, "toString", 0u, plain_year_month_to_string);
        method(ymp, "toLocaleString", 0u, plain_year_month_to_locale_string);
        method(ymp, "toJSON", 0u, plain_year_month_to_json);
        method(ymp, "valueOf", 0u, value_of_throws);
        method(ymp, "toPlainDate", 1u, plain_year_month_to_plain_date);

        // Temporal.PlainMonthDay
        make_temporal_constructor(aRealm, aNamespace, "PlainMonthDay", 2u, plain_month_day_constructor, &realm::temporalPlainMonthDayPrototype,
            &realm::temporalPlainMonthDay, "Temporal.PlainMonthDay");
        auto const mdc = aRealm.temporalPlainMonthDay;
        auto const mdp = aRealm.temporalPlainMonthDayPrototype;
        method(mdc, "from", 1u, plain_month_day_from);
        define_calendar_getters(mdp, plain_month_day_field, { CalCalendarId, CalMonthCode, CalDay });
        method(mdp, "with", 1u, plain_month_day_with);
        method(mdp, "equals", 1u, plain_month_day_equals);
        method(mdp, "toString", 0u, plain_month_day_to_string);
        method(mdp, "toLocaleString", 0u, plain_month_day_to_locale_string);
        method(mdp, "toJSON", 0u, plain_month_day_to_json);
        method(mdp, "valueOf", 0u, value_of_throws);
        method(mdp, "toPlainDate", 1u, plain_month_day_to_plain_date);
    }
}
