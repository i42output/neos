/*
  temporal_zoned.cpp

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

// Temporal: Temporal.ZonedDateTime (ISO 8601 calendar).

#include <cmath>
#include "temporal.hpp"
#include "intl.hpp"

namespace neos::javascript_runtime::temporal
{
    namespace
    {
        temporal_object const& this_zoned(value aThis, char const* aName)
        {
            return this_temporal(aThis, object_class::TemporalZonedDateTime, aName);
        }

        iso_date_time zoned_date_time(temporal_object const& aZoned)
        {
            return get_iso_date_time_for(aZoned.timeZone, aZoned.epochNs);
        }

        value zoned_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (is_undefined(aNewTarget))
                throw_type_error("Temporal.ZonedDateTime must be called with new");
            auto const big = to_bigint(argument(aArguments, 0u));
            auto const ns = bigint_to_i128(big);
            if (!ns || !is_valid_epoch_ns(*ns))
                throw_range_error("epoch nanoseconds are outside the representable range");
            auto const timeZoneArgument = argument(aArguments, 1u);
            if (!is_string(timeZoneArgument))
                throw_type_error("time zone must be a string");
            auto const timeZone = parse_time_zone_identifier_checked(timeZoneArgument);
            auto const calendarArgument = argument(aArguments, 2u);
            auto calendar = calendar_id::Iso8601;
            if (!is_undefined(calendarArgument))
            {
                if (!is_string(calendarArgument))
                    throw_type_error("calendar must be a string");
                calendar = canonicalize_calendar(calendarArgument);
            }
            return create_zoned_date_time(*ns, timeZone, calendar, aNewTarget);
        }

        value zoned_from(value, arguments aArguments, value, function_object&)
        {
            return value_of(to_temporal_zoned_date_time(argument(aArguments, 0u), argument(aArguments, 1u)));
        }

        value zoned_compare(value, arguments aArguments, value, function_object&)
        {
            auto const one = to_temporal_zoned_date_time(argument(aArguments, 0u)).epochNs;
            auto const two = to_temporal_zoned_date_time(argument(aArguments, 1u)).epochNs;
            return number(compare(one, two));
        }

        value zoned_calendar_field(value aThis, arguments, value, function_object& aSelf)
        {
            auto const& z = this_zoned(aThis, "Temporal.ZonedDateTime getter");
            if (aSelf.nativeId == CalCalendarId)
                return calendar_value(z.calendar);
            return calendar_date_field(z.calendar, zoned_date_time(z).date, aSelf.nativeId);
        }

        char const* const sTimeFields[] = { "hour", "minute", "second", "millisecond", "microsecond", "nanosecond" };

        value zoned_time_field(value aThis, arguments, value, function_object& aSelf)
        {
            auto const t = zoned_date_time(this_zoned(aThis, "Temporal.ZonedDateTime getter")).time;
            int const fields[] = { t.hour, t.minute, t.second, t.millisecond, t.microsecond, t.nanosecond };
            return number(fields[aSelf.nativeId]);
        }

        value zoned_time_zone_id(value aThis, arguments, value, function_object&)
        {
            return this_zoned(aThis, "Temporal.ZonedDateTime.prototype.timeZoneId").timeZone.id;
        }

        value zoned_epoch_milliseconds(value aThis, arguments, value, function_object&)
        {
            auto const& z = this_zoned(aThis, "Temporal.ZonedDateTime.prototype.epochMilliseconds");
            return number(floor_div(z.epochNs, i128{ 1000000 }).to_double());
        }

        value zoned_epoch_nanoseconds(value aThis, arguments, value, function_object&)
        {
            return make_bigint_from(this_zoned(aThis, "Temporal.ZonedDateTime.prototype.epochNanoseconds").epochNs);
        }

        value zoned_hours_in_day(value aThis, arguments, value, function_object&)
        {
            auto const& z = this_zoned(aThis, "Temporal.ZonedDateTime.prototype.hoursInDay");
            auto const today = zoned_date_time(z).date;
            auto const tomorrow = add_days_to_iso_date(today, 1);
            auto const todayNs = get_start_of_day(z.timeZone, today);
            auto const tomorrowNs = get_start_of_day(z.timeZone, tomorrow);
            return number(total_time_duration(tomorrowNs - todayNs, unit::Hour));
        }

        value zoned_offset_nanoseconds(value aThis, arguments, value, function_object&)
        {
            auto const& z = this_zoned(aThis, "Temporal.ZonedDateTime.prototype.offsetNanoseconds");
            return number(static_cast<double>(get_offset_ns_for(z.timeZone, z.epochNs)));
        }

        value zoned_offset(value aThis, arguments, value, function_object&)
        {
            auto const& z = this_zoned(aThis, "Temporal.ZonedDateTime.prototype.offset");
            return string_value(format_utc_offset_ns(get_offset_ns_for(z.timeZone, z.epochNs)));
        }

        value zoned_with(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& z = this_zoned(aThis, "Temporal.ZonedDateTime.prototype.with");
            auto const like = argument(aArguments, 0u);
            if (!is_partial_temporal_object(like))
                throw_type_error("with() requires a partial zoned date-time object");
            auto const epochNs = z.epochNs;
            auto const timeZone = z.timeZone;
            auto const calendar = z.calendar;
            auto const offsetNs = get_offset_ns_for(timeZone, epochNs);
            auto const dateTime = get_iso_date_time_for(timeZone, epochNs);
            auto fields = iso_date_to_fields(calendar, dateTime.date, 0);
            fields.hour = dateTime.time.hour;
            fields.minute = dateTime.time.minute;
            fields.second = dateTime.time.second;
            fields.millisecond = dateTime.time.millisecond;
            fields.microsecond = dateTime.time.microsecond;
            fields.nanosecond = dateTime.time.nanosecond;
            fields.offset = u16(format_utc_offset_ns(offsetNs));
            auto const partial = prepare_calendar_fields(calendar, like, FieldsDate | FieldsTime | FieldOffset, 0u, true);
            fields = merge_calendar_fields(calendar, fields, partial);
            auto const options = get_options_object(argument(aArguments, 1u));
            auto const disambiguationOption = get_disambiguation_option(options);
            auto const offsetOption = get_offset_option(options, offset_option::Prefer);
            auto const overflow = get_overflow_option(options);
            auto const result = interpret_temporal_date_time_fields(calendar, fields, overflow);
            auto const newOffsetNs = *parse_date_time_utc_offset(*fields.offset);
            auto const ns = interpret_iso_date_time_offset(result.date, result.time, 0, newOffsetNs, timeZone, disambiguationOption, offsetOption,
                false);
            return create_zoned_date_time(ns, timeZone, calendar);
        }

        value zoned_with_plain_time(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& z = this_zoned(aThis, "Temporal.ZonedDateTime.prototype.withPlainTime");
            auto const timeZone = z.timeZone;
            auto const dateTime = zoned_date_time(z);
            auto const like = argument(aArguments, 0u);
            i128 epochNs;
            if (is_undefined(like))
                epochNs = get_start_of_day(timeZone, dateTime.date);
            else
            {
                auto const time = to_temporal_time(like);
                epochNs = get_epoch_ns_for(timeZone, { dateTime.date, time }, disambiguation::Compatible);
            }
            return create_zoned_date_time(epochNs, timeZone, z.calendar);
        }

        value zoned_with_time_zone(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& z = this_zoned(aThis, "Temporal.ZonedDateTime.prototype.withTimeZone");
            auto const epochNs = z.epochNs;
            auto const timeZone = to_temporal_time_zone_identifier(argument(aArguments, 0u));
            return create_zoned_date_time(epochNs, timeZone, z.calendar);
        }

        value zoned_with_calendar(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& z = this_zoned(aThis, "Temporal.ZonedDateTime.prototype.withCalendar");
            auto const epochNs = z.epochNs;
            auto const timeZone = z.timeZone;
            auto const calendar = to_temporal_calendar_identifier(argument(aArguments, 0u));
            return create_zoned_date_time(epochNs, timeZone, calendar);
        }

        value add_duration_to_zoned(bool aSubtract, temporal_object const& aZoned, value aLike, value aOptions)
        {
            auto const epochNs = aZoned.epochNs;
            auto const timeZone = aZoned.timeZone;
            auto duration = to_temporal_duration(aLike);
            if (aSubtract)
                duration = negate_duration(duration);
            auto const overflow = get_overflow_option(get_options_object(aOptions));
            auto const internal = to_internal_duration(duration);
            return create_zoned_date_time(add_zoned_date_time(epochNs, timeZone, aZoned.calendar, internal, overflow), timeZone, aZoned.calendar);
        }

        value zoned_add(value aThis, arguments aArguments, value, function_object&)
        {
            return add_duration_to_zoned(false, this_zoned(aThis, "Temporal.ZonedDateTime.prototype.add"), argument(aArguments, 0u),
                argument(aArguments, 1u));
        }

        value zoned_subtract(value aThis, arguments aArguments, value, function_object&)
        {
            return add_duration_to_zoned(true, this_zoned(aThis, "Temporal.ZonedDateTime.prototype.subtract"), argument(aArguments, 0u),
                argument(aArguments, 1u));
        }

        value difference_temporal_zoned(bool aSince, temporal_object const& aZoned, value aOther, value aOptions)
        {
            auto const epochNs = aZoned.epochNs;
            auto const timeZone = aZoned.timeZone;
            auto const calendar = aZoned.calendar;
            auto const& other = to_temporal_zoned_date_time(aOther);
            if (calendar != other.calendar)
                throw_range_error("the calendars are not the same");
            auto const options = get_options_object(aOptions);
            auto const settings = get_difference_settings(aSince, options, unit_group::DateTime, {}, unit::Nanosecond, unit::Hour);
            if (!is_date_unit(settings.largestUnit))
            {
                auto const internal = difference_instant(epochNs, other.epochNs, settings.roundingIncrement, settings.smallestUnit,
                    settings.roundingMode);
                auto result = duration_from_internal(internal, settings.largestUnit);
                if (aSince)
                    result = negate_duration(result);
                return create_duration(result);
            }
            if (!time_zone_equals(timeZone, other.timeZone))
                throw_range_error("date units cannot be computed between different time zones");
            if (epochNs == other.epochNs)
                return create_duration({});
            auto const internal = difference_zoned_date_time_with_rounding(epochNs, other.epochNs, timeZone, calendar, settings.largestUnit,
                settings.roundingIncrement, settings.smallestUnit, settings.roundingMode);
            auto result = duration_from_internal(internal, unit::Hour);
            if (aSince)
                result = negate_duration(result);
            return create_duration(result);
        }

        value zoned_until(value aThis, arguments aArguments, value, function_object&)
        {
            return difference_temporal_zoned(false, this_zoned(aThis, "Temporal.ZonedDateTime.prototype.until"), argument(aArguments, 0u),
                argument(aArguments, 1u));
        }

        value zoned_since(value aThis, arguments aArguments, value, function_object&)
        {
            return difference_temporal_zoned(true, this_zoned(aThis, "Temporal.ZonedDateTime.prototype.since"), argument(aArguments, 0u),
                argument(aArguments, 1u));
        }

        value zoned_round(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& z = this_zoned(aThis, "Temporal.ZonedDateTime.prototype.round");
            auto const thisNs = z.epochNs;
            auto const timeZone = z.timeZone;
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
                return create_zoned_date_time(thisNs, timeZone, z.calendar);
            auto const dateTime = get_iso_date_time_for(timeZone, thisNs);
            i128 epochNs;
            if (smallestUnit == unit::Day)
            {
                auto const dateStart = dateTime.date;
                auto const dateEnd = add_days_to_iso_date(dateStart, 1);
                auto const startNs = get_start_of_day(timeZone, dateStart);
                auto const endNs = get_start_of_day(timeZone, dateEnd);
                auto const dayLength = endNs - startNs;
                // (a transition after midnight back to before it interleaves two days: an instant in the first day's second piece
                // is beyond the next day's start (proposal-temporal issue 3312))
                auto const progress = (thisNs >= endNs ? endNs - i128{ 1 } : thisNs) - startNs;
                auto const rounded = round_to_increment(progress, dayLength, roundingMode);
                epochNs = startNs + rounded;
            }
            else
            {
                auto const roundResult = round_iso_date_time(dateTime, roundingIncrement, smallestUnit, roundingMode);
                auto const offsetNs = get_offset_ns_for(timeZone, thisNs);
                epochNs = interpret_iso_date_time_offset(roundResult.date, roundResult.time, 0, offsetNs, timeZone, disambiguation::Compatible,
                    offset_option::Prefer, false);
            }
            return create_zoned_date_time(epochNs, timeZone, z.calendar);
        }

        value zoned_equals(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& z = this_zoned(aThis, "Temporal.ZonedDateTime.prototype.equals");
            auto const epochNs = z.epochNs;
            auto const timeZone = z.timeZone;
            auto const& other = to_temporal_zoned_date_time(argument(aArguments, 0u));
            if (epochNs != other.epochNs)
                return False;
            return boolean(time_zone_equals(timeZone, other.timeZone) && z.calendar == other.calendar);
        }

        std::string zoned_to_string(i128 aEpochNs, time_zone const& aTimeZone, calendar_id aCalendar, int aPrecision, show_calendar aShowCalendar,
            show_time_zone aShowTimeZone, bool aShowOffset, std::int64_t aIncrement = 1, unit aUnit = unit::Nanosecond,
            rounding_mode aMode = rounding_mode::Trunc)
        {
            aEpochNs = round_to_increment_as_if_positive(aEpochNs, unit_length(aUnit) * i128{ aIncrement }, aMode);
            auto const offsetNs = get_offset_ns_for(aTimeZone, aEpochNs);
            auto const dateTime = get_iso_date_time_for(aTimeZone, aEpochNs);
            auto result = iso_date_time_to_string(dateTime, aPrecision, aCalendar, show_calendar::Never);
            if (aShowOffset)
                result += format_utc_offset_rounded(offsetNs);
            if (aShowTimeZone != show_time_zone::Never)
                result += std::string{ "[" } + (aShowTimeZone == show_time_zone::Critical ? "!" : "") + utf8(aTimeZone.id) + "]";
            return result + format_calendar_annotation(aCalendar, aShowCalendar);
        }

        value zoned_to_string_method(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& z = this_zoned(aThis, "Temporal.ZonedDateTime.prototype.toString");
            auto const epochNs = z.epochNs;
            auto const timeZone = z.timeZone;
            auto const options = get_options_object(argument(aArguments, 0u));
            auto const showCalendar = get_show_calendar_option(options);
            auto const digits = get_fractional_second_digits_option(options);
            auto const showOffset = get_show_offset_option(options);
            auto const roundingMode = get_rounding_mode_option(options, rounding_mode::Trunc);
            auto const smallestUnit = get_unit_option(options, "smallestUnit", false);
            auto const showTimeZone = get_show_time_zone_option(options);
            validate_unit(smallestUnit, unit_group::Time);
            if (smallestUnit == unit::Hour)
                throw_range_error("smallestUnit must not be hour");
            auto const precision = to_seconds_string_precision(smallestUnit, digits);
            return string_value(zoned_to_string(epochNs, timeZone, z.calendar, precision.precision, showCalendar, showTimeZone, showOffset,
                precision.increment, precision.u, roundingMode));
        }

        value zoned_to_json(value aThis, arguments, value, function_object&)
        {
            auto const& z = this_zoned(aThis, "Temporal.ZonedDateTime.prototype.toJSON");
            return string_value(zoned_to_string(z.epochNs, z.timeZone, z.calendar, -1, show_calendar::Auto, show_time_zone::Auto, true));
        }

        value zoned_to_locale_string(value aThis, arguments aArguments, value, function_object&)
        {
#if defined(NEOS_JS_ICU)
            if (intl::icu_available())
            {
                this_zoned(aThis, "Temporal.ZonedDateTime.prototype.toLocaleString");
                return intl::temporal_to_locale_string(aThis, argument(aArguments, 0u), argument(aArguments, 1u));
            }
#endif
            (void)aArguments;
            auto const& z = this_zoned(aThis, "Temporal.ZonedDateTime.prototype.toLocaleString");
            return string_value(zoned_to_string(z.epochNs, z.timeZone, z.calendar, -1, show_calendar::Auto, show_time_zone::Auto, true));
        }

        value zoned_start_of_day(value aThis, arguments, value, function_object&)
        {
            auto const& z = this_zoned(aThis, "Temporal.ZonedDateTime.prototype.startOfDay");
            auto const timeZone = z.timeZone;
            auto const dateTime = zoned_date_time(z);
            return create_zoned_date_time(get_start_of_day(timeZone, dateTime.date), timeZone, z.calendar);
        }

        value zoned_get_time_zone_transition(value aThis, arguments aArguments, value, function_object&)
        {
            auto const& z = this_zoned(aThis, "Temporal.ZonedDateTime.prototype.getTimeZoneTransition");
            auto const epochNs = z.epochNs;
            auto const timeZone = z.timeZone;
            auto const directionParam = argument(aArguments, 0u);
            if (is_undefined(directionParam))
                throw_type_error("direction is required");
            auto const options = round_to_options(directionParam, "direction");
            auto const v = get(options, ascii("direction"));
            if (is_undefined(v))
                throw_range_error("direction is required");
            auto const& direction = str(to_string(v));
            bool next = false;
            if (direction == u"next")
                next = true;
            else if (direction != u"previous")
                throw_range_error("direction must be next or previous");
            if (!timeZone.named)
                return Null;
            auto const transition = get_time_zone_transition(timeZone, epochNs, next);
            if (!transition)
                return Null;
            return create_zoned_date_time(*transition, timeZone, z.calendar);
        }

        value zoned_to_instant(value aThis, arguments, value, function_object&)
        {
            return create_instant(this_zoned(aThis, "Temporal.ZonedDateTime.prototype.toInstant").epochNs);
        }

        value zoned_to_plain_date(value aThis, arguments, value, function_object&)
        {
            auto const& z = this_zoned(aThis, "Temporal.ZonedDateTime.prototype.toPlainDate");
            return create_plain_date(zoned_date_time(z).date, z.calendar);
        }

        value zoned_to_plain_time(value aThis, arguments, value, function_object&)
        {
            return create_plain_time(zoned_date_time(this_zoned(aThis, "Temporal.ZonedDateTime.prototype.toPlainTime")).time);
        }

        value zoned_to_plain_date_time(value aThis, arguments, value, function_object&)
        {
            auto const& z = this_zoned(aThis, "Temporal.ZonedDateTime.prototype.toPlainDateTime");
            return create_plain_date_time(zoned_date_time(z), z.calendar);
        }
    }

    void init_temporal_zoned(realm& aRealm, value aNamespace)
    {
        auto const c = make_temporal_constructor(aRealm, aNamespace, "ZonedDateTime", 2u, zoned_constructor, &realm::temporalZonedDateTimePrototype,
            &realm::temporalZonedDateTime, "Temporal.ZonedDateTime");
        auto const p = aRealm.temporalZonedDateTimePrototype;
        method(c, "from", 1u, zoned_from);
        method(c, "compare", 2u, zoned_compare);
        as_function(getter(p, ascii("calendarId"), zoned_calendar_field))->nativeId = CalCalendarId;
        getter(p, ascii("timeZoneId"), zoned_time_zone_id);
        for (auto const f : { CalEra, CalEraYear, CalYear, CalMonth, CalMonthCode, CalDay })
            as_function(getter(p, ascii(calendar_field_name(f)), zoned_calendar_field))->nativeId = f;
        for (std::uint32_t i = 0u; i < 6u; ++i)
            as_function(getter(p, ascii(sTimeFields[i]), zoned_time_field))->nativeId = i;
        getter(p, ascii("epochMilliseconds"), zoned_epoch_milliseconds);
        getter(p, ascii("epochNanoseconds"), zoned_epoch_nanoseconds);
        for (auto const f : { CalDayOfWeek, CalDayOfYear, CalWeekOfYear, CalYearOfWeek })
            as_function(getter(p, ascii(calendar_field_name(f)), zoned_calendar_field))->nativeId = f;
        getter(p, ascii("hoursInDay"), zoned_hours_in_day);
        for (auto const f : { CalDaysInWeek, CalDaysInMonth, CalDaysInYear, CalMonthsInYear, CalInLeapYear })
            as_function(getter(p, ascii(calendar_field_name(f)), zoned_calendar_field))->nativeId = f;
        getter(p, ascii("offsetNanoseconds"), zoned_offset_nanoseconds);
        getter(p, ascii("offset"), zoned_offset);
        method(p, "with", 1u, zoned_with);
        method(p, "withPlainTime", 0u, zoned_with_plain_time);
        method(p, "withTimeZone", 1u, zoned_with_time_zone);
        method(p, "withCalendar", 1u, zoned_with_calendar);
        method(p, "add", 1u, zoned_add);
        method(p, "subtract", 1u, zoned_subtract);
        method(p, "until", 1u, zoned_until);
        method(p, "since", 1u, zoned_since);
        method(p, "round", 1u, zoned_round);
        method(p, "equals", 1u, zoned_equals);
        method(p, "toString", 0u, zoned_to_string_method);
        method(p, "toLocaleString", 0u, zoned_to_locale_string);
        method(p, "toJSON", 0u, zoned_to_json);
        method(p, "valueOf", 0u, value_of_throws);
        method(p, "startOfDay", 0u, zoned_start_of_day);
        method(p, "getTimeZoneTransition", 1u, zoned_get_time_zone_transition);
        method(p, "toInstant", 0u, zoned_to_instant);
        method(p, "toPlainDate", 0u, zoned_to_plain_date);
        method(p, "toPlainTime", 0u, zoned_to_plain_time);
        method(p, "toPlainDateTime", 0u, zoned_to_plain_date_time);
    }
}
