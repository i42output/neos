/*
  temporal_objects.cpp

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

// Temporal: the Temporal namespace object; creating and converting the Temporal objects; Temporal.Duration,
// Temporal.Instant and Temporal.Now; Date.prototype.toTemporalInstant.

#include <cmath>
#include "temporal.hpp"
#include "intl.hpp"
#include "gc.hpp"

namespace neos::javascript_runtime
{
    using namespace temporal;

    namespace temporal
    {
        value string_value(std::string const& aText)
        {
            return make_string(u16(aText));
        }

        temporal_object& tobj(value aObject)
        {
            return static_cast<temporal_object&>(obj(aObject));
        }

        temporal_object& this_temporal(value aThis, object_class aClass, char const* aName)
        {
            auto* t = as_temporal(aThis, aClass);
            if (t == nullptr)
                throw_type_error(std::string{ aName } + " called on an incompatible receiver");
            return *t;
        }

        namespace
        {
            temporal_object& make_temporal(object_class aClass, value aNewTarget, value realm::*aPrototype)
            {
                auto const prototype = is_undefined(aNewTarget) ? the_realm().*aPrototype : get_prototype_from_constructor(aNewTarget, aPrototype);
                return the_heap().make<temporal_object>(prototype, aClass);
            }
        }

        value create_duration(duration_fields const& aFields, value aNewTarget)
        {
            if (!is_valid_duration(aFields))
                throw_range_error("invalid duration");
            auto& o = make_temporal(object_class::TemporalDuration, aNewTarget, &realm::temporalDurationPrototype);
            for (int i = 0; i < 10; ++i)
                o.duration.v[i] = aFields.v[i] + 0.0;
            return value_of(o);
        }

        value create_instant(i128 const& aEpochNs, value aNewTarget)
        {
            auto& o = make_temporal(object_class::TemporalInstant, aNewTarget, &realm::temporalInstantPrototype);
            o.epochNs = aEpochNs;
            return value_of(o);
        }

        value create_plain_date(iso_date const& aDate, calendar_id aCalendar, value aNewTarget)
        {
            if (!iso_date_within_limits(aDate))
                throw_range_error("date is outside the representable range");
            auto& o = make_temporal(object_class::TemporalPlainDate, aNewTarget, &realm::temporalPlainDatePrototype);
            o.date = aDate;
            o.calendar = aCalendar;
            return value_of(o);
        }

        value create_plain_time(time_record const& aTime, value aNewTarget)
        {
            auto& o = make_temporal(object_class::TemporalPlainTime, aNewTarget, &realm::temporalPlainTimePrototype);
            o.time = aTime;
            o.time.days = 0;
            return value_of(o);
        }

        value create_plain_date_time(iso_date_time const& aDateTime, calendar_id aCalendar, value aNewTarget)
        {
            if (!iso_date_time_within_limits(aDateTime))
                throw_range_error("date-time is outside the representable range");
            auto& o = make_temporal(object_class::TemporalPlainDateTime, aNewTarget, &realm::temporalPlainDateTimePrototype);
            o.date = aDateTime.date;
            o.calendar = aCalendar;
            o.time = aDateTime.time;
            o.time.days = 0;
            return value_of(o);
        }

        value create_plain_year_month(iso_date const& aDate, calendar_id aCalendar, value aNewTarget)
        {
            if (!iso_year_month_within_limits(aDate))
                throw_range_error("year-month is outside the representable range");
            auto& o = make_temporal(object_class::TemporalPlainYearMonth, aNewTarget, &realm::temporalPlainYearMonthPrototype);
            o.date = aDate;
            o.calendar = aCalendar;
            return value_of(o);
        }

        value create_plain_month_day(iso_date const& aDate, calendar_id aCalendar, value aNewTarget)
        {
            if (!iso_date_within_limits(aDate))
                throw_range_error("month-day is outside the representable range");
            auto& o = make_temporal(object_class::TemporalPlainMonthDay, aNewTarget, &realm::temporalPlainMonthDayPrototype);
            o.date = aDate;
            o.calendar = aCalendar;
            return value_of(o);
        }

        value create_zoned_date_time(i128 const& aEpochNs, time_zone const& aTimeZone, calendar_id aCalendar, value aNewTarget)
        {
            auto& o = make_temporal(object_class::TemporalZonedDateTime, aNewTarget, &realm::temporalZonedDateTimePrototype);
            o.epochNs = aEpochNs;
            o.timeZone = aTimeZone;
            o.calendar = aCalendar;
            return value_of(o);
        }

        duration_fields to_temporal_duration(value aItem)
        {
            if (auto const* d = as_temporal(aItem, object_class::TemporalDuration))
                return d->duration;
            if (!is_object(aItem))
            {
                if (!is_string(aItem))
                    throw_type_error("duration must be a string or an object");
                return parse_duration_string(str(aItem));
            }
            bool any = false;
            auto partial = to_partial_duration(aItem, any);
            for (auto& v : partial.v)
                if (std::isnan(v))
                    v = 0.0;
            if (!is_valid_duration(partial))
                throw_range_error("invalid duration");
            return partial;
        }

        i128 to_temporal_instant(value aItem)
        {
            if (is_object(aItem))
            {
                auto const cls = obj(aItem).cls;
                if (cls == object_class::TemporalInstant || cls == object_class::TemporalZonedDateTime)
                    return tobj(aItem).epochNs;
                aItem = to_primitive(aItem, hint::String);
            }
            if (!is_string(aItem))
                throw_type_error("instant must be a string or an object");
            auto const parsed = parse_iso_date_time(str(aItem), { GoalInstant });
            std::int64_t offsetNs = 0;
            if (!parsed.z)
                offsetNs = *parse_date_time_utc_offset(*parsed.offset);
            auto const& t = parsed.time;
            auto const balanced = balance_iso_date_time({ parsed.year, parsed.month, parsed.day }, t.hour, t.minute, t.second, t.millisecond,
                t.microsecond, i128{ t.nanosecond } - i128{ offsetNs });
            check_iso_days_range(balanced.date);
            auto const epochNs = get_utc_epoch_ns(balanced);
            if (!is_valid_epoch_ns(epochNs))
                throw_range_error("instant is outside the representable range");
            return epochNs;
        }

        temporal_date to_temporal_date(value aItem, value aOptions)
        {
            if (is_object(aItem))
            {
                auto const cls = obj(aItem).cls;
                if (cls == object_class::TemporalPlainDate || cls == object_class::TemporalPlainDateTime)
                {
                    get_overflow_option(get_options_object(aOptions));
                    return { tobj(aItem).date, tobj(aItem).calendar };
                }
                if (cls == object_class::TemporalZonedDateTime)
                {
                    auto const& z = tobj(aItem);
                    auto const dateTime = get_iso_date_time_for(z.timeZone, z.epochNs);
                    get_overflow_option(get_options_object(aOptions));
                    return { dateTime.date, z.calendar };
                }
                auto const calendar = get_temporal_calendar_with_iso_default(aItem);
                auto const fields = prepare_calendar_fields(calendar, aItem, FieldsDate, 0u, false);
                auto const overflow = get_overflow_option(get_options_object(aOptions));
                return { calendar_date_from_fields(calendar, fields, overflow), calendar };
            }
            if (!is_string(aItem))
                throw_type_error("date must be a string or an object");
            auto const result = parse_iso_date_time(str(aItem), { GoalDateTime });
            auto const calendar = result.calendar ? canonicalize_calendar(make_string(*result.calendar)) : calendar_id::Iso8601;
            get_overflow_option(get_options_object(aOptions));
            iso_date const date{ result.year, result.month, result.day };
            if (!iso_date_within_limits(date))
                throw_range_error("date is outside the representable range");
            return { date, calendar };
        }

        time_record to_temporal_time(value aItem, value aOptions)
        {
            if (is_object(aItem))
            {
                auto const cls = obj(aItem).cls;
                if (cls == object_class::TemporalPlainTime || cls == object_class::TemporalPlainDateTime)
                {
                    get_overflow_option(get_options_object(aOptions));
                    return tobj(aItem).time;
                }
                if (cls == object_class::TemporalZonedDateTime)
                {
                    auto const& z = tobj(aItem);
                    auto const dateTime = get_iso_date_time_for(z.timeZone, z.epochNs);
                    get_overflow_option(get_options_object(aOptions));
                    return dateTime.time;
                }
                bool any = false;
                // (ToTemporalTimeRecord: alphabetical order)
                double fields[6] = {};
                static struct { char const* name; int index; } const sOrder[] = {
                    { "hour", 0 }, { "microsecond", 4 }, { "millisecond", 3 }, { "minute", 1 }, { "nanosecond", 5 }, { "second", 2 } };
                for (auto const& [name, index] : sOrder)
                {
                    auto const v = get(aItem, ascii(name));
                    if (!is_undefined(v))
                    {
                        fields[index] = to_integer_with_truncation(v);
                        any = true;
                    }
                }
                if (!any)
                    throw_type_error("time-like object must have at least one time property");
                auto const overflow = get_overflow_option(get_options_object(aOptions));
                return regulate_time(fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], overflow);
            }
            if (!is_string(aItem))
                throw_type_error("time must be a string or an object");
            auto const result = parse_iso_date_time(str(aItem), { GoalTime });
            get_overflow_option(get_options_object(aOptions));
            return result.time;
        }

        time_record to_time_record_or_midnight(value aItem)
        {
            if (is_undefined(aItem))
                return {};
            return to_temporal_time(aItem);
        }

        temporal_date_time to_temporal_date_time(value aItem, value aOptions)
        {
            if (is_object(aItem))
            {
                auto const cls = obj(aItem).cls;
                if (cls == object_class::TemporalPlainDateTime)
                {
                    get_overflow_option(get_options_object(aOptions));
                    return { { tobj(aItem).date, tobj(aItem).time }, tobj(aItem).calendar };
                }
                if (cls == object_class::TemporalZonedDateTime)
                {
                    auto const& z = tobj(aItem);
                    auto const dateTime = get_iso_date_time_for(z.timeZone, z.epochNs);
                    get_overflow_option(get_options_object(aOptions));
                    return { dateTime, z.calendar };
                }
                if (cls == object_class::TemporalPlainDate)
                {
                    get_overflow_option(get_options_object(aOptions));
                    iso_date_time const dateTime{ tobj(aItem).date, {} };
                    if (!iso_date_time_within_limits(dateTime))
                        throw_range_error("date-time is outside the representable range");
                    return { dateTime, tobj(aItem).calendar };
                }
                auto const calendar = get_temporal_calendar_with_iso_default(aItem);
                auto const fields = prepare_calendar_fields(calendar, aItem, FieldsDate | FieldsTime, 0u, false);
                auto const overflow = get_overflow_option(get_options_object(aOptions));
                auto const result = interpret_temporal_date_time_fields(calendar, fields, overflow);
                if (!iso_date_time_within_limits(result))
                    throw_range_error("date-time is outside the representable range");
                return { result, calendar };
            }
            if (!is_string(aItem))
                throw_type_error("date-time must be a string or an object");
            auto const result = parse_iso_date_time(str(aItem), { GoalDateTime });
            auto const calendar = result.calendar ? canonicalize_calendar(make_string(*result.calendar)) : calendar_id::Iso8601;
            get_overflow_option(get_options_object(aOptions));
            iso_date_time const dateTime{ { result.year, result.month, result.day }, result.hasTime ? result.time : time_record{} };
            if (!iso_date_time_within_limits(dateTime))
                throw_range_error("date-time is outside the representable range");
            return { dateTime, calendar };
        }

        temporal_date to_temporal_year_month(value aItem, value aOptions)
        {
            if (is_object(aItem))
            {
                if (auto const* ym = as_temporal(aItem, object_class::TemporalPlainYearMonth))
                {
                    get_overflow_option(get_options_object(aOptions));
                    return { ym->date, ym->calendar };
                }
                auto const calendar = get_temporal_calendar_with_iso_default(aItem);
                auto const fields = prepare_calendar_fields(calendar, aItem, FieldYear | FieldMonth | FieldMonthCode, 0u, false);
                auto const overflow = get_overflow_option(get_options_object(aOptions));
                return { calendar_year_month_from_fields(calendar, fields, overflow), calendar };
            }
            if (!is_string(aItem))
                throw_type_error("year-month must be a string or an object");
            auto const result = parse_iso_date_time(str(aItem), { GoalYearMonth });
            auto const calendar = result.calendar ? canonicalize_calendar(make_string(*result.calendar)) : calendar_id::Iso8601;
            get_overflow_option(get_options_object(aOptions));
            iso_date const date{ result.year, result.month, result.day };
            if (!iso_year_month_within_limits(date))
                throw_range_error("year-month is outside the representable range");
            return { calendar_year_month_from_fields(calendar, iso_date_to_fields(calendar, date, 1), overflow::Constrain), calendar };
        }

        temporal_date to_temporal_month_day(value aItem, value aOptions)
        {
            if (is_object(aItem))
            {
                if (auto const* md = as_temporal(aItem, object_class::TemporalPlainMonthDay))
                {
                    get_overflow_option(get_options_object(aOptions));
                    return { md->date, md->calendar };
                }
                auto const calendar = get_temporal_calendar_with_iso_default(aItem);
                auto const fields = prepare_calendar_fields(calendar, aItem, FieldsDate, 0u, false);
                auto const overflow = get_overflow_option(get_options_object(aOptions));
                return { calendar_month_day_from_fields(calendar, fields, overflow), calendar };
            }
            if (!is_string(aItem))
                throw_type_error("month-day must be a string or an object");
            auto const result = parse_iso_date_time(str(aItem), { GoalMonthDay });
            auto const calendar = result.calendar ? canonicalize_calendar(make_string(*result.calendar)) : calendar_id::Iso8601;
            get_overflow_option(get_options_object(aOptions));
            if (calendar == calendar_id::Iso8601)
                return { { 1972, result.month, result.day }, calendar };
            iso_date const date{ result.year, result.month, result.day };
            if (!iso_date_within_limits(date))
                throw_range_error("month-day is outside the representable range");
            return { calendar_month_day_from_fields(calendar, iso_date_to_fields(calendar, date, 2), overflow::Constrain), calendar };
        }

        temporal_object& to_temporal_zoned_date_time(value aItem, value aOptions)
        {
            bool hasUTCDesignator = false;
            bool matchMinutes = false;
            time_zone timeZone;
            std::optional<std::u16string> offsetString;
            iso_date date;
            std::optional<time_record> time;
            disambiguation disambiguationOption = disambiguation::Compatible;
            offset_option offsetOption = offset_option::Reject;
            calendar_id calendar = calendar_id::Iso8601;
            if (is_object(aItem))
            {
                if (auto* z = as_temporal(aItem, object_class::TemporalZonedDateTime))
                {
                    auto const options = get_options_object(aOptions);
                    get_disambiguation_option(options);
                    get_offset_option(options, offset_option::Reject);
                    get_overflow_option(options);
                    return tobj(create_zoned_date_time(z->epochNs, z->timeZone, z->calendar));
                }
                calendar = get_temporal_calendar_with_iso_default(aItem);
                auto const fields = prepare_calendar_fields(calendar, aItem, FieldsDate | FieldsTime | FieldOffset | FieldTimeZone, FieldTimeZone, false);
                timeZone = *fields.timeZone;
                offsetString = fields.offset;
                auto const options = get_options_object(aOptions);
                disambiguationOption = get_disambiguation_option(options);
                offsetOption = get_offset_option(options, offset_option::Reject);
                auto const overflow = get_overflow_option(options);
                auto const result = interpret_temporal_date_time_fields(calendar, fields, overflow);
                date = result.date;
                time = result.time;
            }
            else
            {
                if (!is_string(aItem))
                    throw_type_error("zoned date-time must be a string or an object");
                auto const result = parse_iso_date_time(str(aItem), { GoalDateTimeZoned });
                timeZone = to_temporal_time_zone_identifier(make_string(*result.timeZoneAnnotation));
                offsetString = result.offset;
                hasUTCDesignator = result.z;
                if (result.calendar)
                    calendar = canonicalize_calendar(make_string(*result.calendar));
                matchMinutes = true;
                if (offsetString)
                {
                    bool subMinute = false;
                    parse_date_time_utc_offset(*offsetString, &subMinute);
                    if (subMinute)
                        matchMinutes = false;
                }
                auto const options = get_options_object(aOptions);
                disambiguationOption = get_disambiguation_option(options);
                offsetOption = get_offset_option(options, offset_option::Reject);
                get_overflow_option(options);
                date = { result.year, result.month, result.day };
                if (result.hasTime)
                    time = result.time;
            }
            int offsetBehaviour = 0;
            if (hasUTCDesignator)
                offsetBehaviour = 1;
            else if (!offsetString)
                offsetBehaviour = 2;
            std::int64_t offsetNs = 0;
            if (offsetBehaviour == 0)
                offsetNs = *parse_date_time_utc_offset(*offsetString);
            auto const epochNs = interpret_iso_date_time_offset(date, time, offsetBehaviour, offsetNs, timeZone, disambiguationOption,
                offsetOption, matchMinutes);
            return tobj(create_zoned_date_time(epochNs, timeZone, calendar));
        }

        bool is_partial_temporal_object(value aValue)
        {
            if (!is_object(aValue))
                return false;
            auto const cls = obj(aValue).cls;
            if (cls == object_class::TemporalPlainDate || cls == object_class::TemporalPlainDateTime || cls == object_class::TemporalPlainMonthDay ||
                cls == object_class::TemporalPlainTime || cls == object_class::TemporalPlainYearMonth || cls == object_class::TemporalZonedDateTime)
                return false;
            if (!is_undefined(get(aValue, ascii("calendar"))))
                return false;
            if (!is_undefined(get(aValue, ascii("timeZone"))))
                return false;
            return true;
        }

        value round_to_options(value aRoundTo, char const* aKey)
        {
            if (is_undefined(aRoundTo))
                throw_type_error("options are required");
            if (is_string(aRoundTo))
            {
                auto const o = make_object(Null);
                create_data_property_or_throw(o, ascii(aKey), aRoundTo);
                return o;
            }
            return get_options_object(aRoundTo);
        }

        value make_temporal_constructor(realm& aRealm, value aNamespace, char const* aName, std::uint32_t aLength, native_function aFunction,
            value realm::*aPrototype, value realm::*aConstructor, char const* aTag)
        {
            auto const p = make_object();
            auto const c = make_native(aName, aLength, aFunction, true);
            aRealm.*aPrototype = p;
            aRealm.*aConstructor = c;
            obj(c).put(the_names().prototype, p, 0u);
            obj(p).put(the_names().constructor, c);
            obj(p).put(aRealm.symToStringTag, ascii(aTag), AttributeConfigurable);
            obj(aNamespace).put(ascii(aName), c);
            return c;
        }
    }

    namespace
    {
        // --- relativeTo

        struct relative_to
        {
            bool plain = false;
            bool zoned = false;
            iso_date date;
            i128 epochNs;
            time_zone timeZone;
            calendar_id calendar = calendar_id::Iso8601;
        };

        relative_to get_relative_to_option(value aOptions)
        {
            relative_to r;
            auto const v = get(aOptions, ascii("relativeTo"));
            if (is_undefined(v))
                return r;
            int offsetBehaviour = 0;
            bool matchMinutes = false;
            std::optional<time_zone> timeZone;
            std::optional<std::u16string> offsetString;
            iso_date date;
            std::optional<time_record> time;
            if (is_object(v))
            {
                auto const cls = obj(v).cls;
                if (cls == object_class::TemporalZonedDateTime)
                {
                    r.zoned = true;
                    r.epochNs = tobj(v).epochNs;
                    r.timeZone = tobj(v).timeZone;
                    r.calendar = tobj(v).calendar;
                    return r;
                }
                if (cls == object_class::TemporalPlainDate || cls == object_class::TemporalPlainDateTime)
                {
                    r.plain = true;
                    r.date = tobj(v).date;
                    r.calendar = tobj(v).calendar;
                    return r;
                }
                r.calendar = get_temporal_calendar_with_iso_default(v);
                auto const fields = prepare_calendar_fields(r.calendar, v, FieldsDate | FieldsTime | FieldOffset | FieldTimeZone, 0u, false);
                auto const result = interpret_temporal_date_time_fields(r.calendar, fields, overflow::Constrain);
                timeZone = fields.timeZone;
                offsetString = fields.offset;
                if (!offsetString)
                    offsetBehaviour = 2;
                date = result.date;
                time = result.time;
            }
            else
            {
                if (!is_string(v))
                    throw_type_error("relativeTo must be a string or an object");
                auto const result = parse_iso_date_time(str(v), { GoalDateTimeZoned, GoalDateTime });
                offsetString = result.offset;
                if (result.timeZoneAnnotation)
                {
                    timeZone = to_temporal_time_zone_identifier(make_string(*result.timeZoneAnnotation));
                    if (result.z)
                        offsetBehaviour = 1;
                    else if (!offsetString)
                        offsetBehaviour = 2;
                    matchMinutes = true;
                    if (offsetString)
                    {
                        bool subMinute = false;
                        parse_date_time_utc_offset(*offsetString, &subMinute);
                        if (subMinute)
                            matchMinutes = false;
                    }
                }
                if (result.calendar)
                    r.calendar = canonicalize_calendar(make_string(*result.calendar));
                date = { result.year, result.month, result.day };
                if (result.hasTime)
                    time = result.time;
            }
            if (!timeZone)
            {
                if (!iso_date_within_limits(date))
                    throw_range_error("relativeTo is outside the representable range");
                r.plain = true;
                r.date = date;
                return r;
            }
            std::int64_t offsetNs = 0;
            if (offsetBehaviour == 0)
                offsetNs = *parse_date_time_utc_offset(*offsetString);
            r.zoned = true;
            r.timeZone = *timeZone;
            r.epochNs = interpret_iso_date_time_offset(date, time, offsetBehaviour, offsetNs, *timeZone, disambiguation::Compatible,
                offset_option::Reject, matchMinutes);
            return r;
        }

        double date_duration_days(date_duration const& aDuration, iso_date const& aRelativeTo, calendar_id aCalendar)
        {
            auto const yearsMonthsWeeks = create_date_duration(aDuration.years, aDuration.months, aDuration.weeks, 0.0);
            if (date_duration_sign(yearsMonthsWeeks) == 0)
                return aDuration.days;
            auto const later = calendar_date_add(aCalendar, aRelativeTo, yearsMonthsWeeks, overflow::Constrain);
            auto const epochDays1 = iso_date_to_epoch_days(aRelativeTo.year, aRelativeTo.month - 1, aRelativeTo.day);
            auto const epochDays2 = iso_date_to_epoch_days(later.year, later.month - 1, later.day);
            return aDuration.days + static_cast<double>(epochDays2 - epochDays1);
        }

        // --- Temporal.Duration

        duration_fields& this_duration(value aThis, char const* aName)
        {
            return this_temporal(aThis, object_class::TemporalDuration, aName).duration;
        }

        value duration_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (is_undefined(aNewTarget))
                throw_type_error("Temporal.Duration must be called with new");
            duration_fields f;
            for (std::size_t i = 0u; i < 10u; ++i)
            {
                auto const v = argument(aArguments, i);
                f.v[i] = is_undefined(v) ? 0.0 : to_integer_if_integral(v);
            }
            return create_duration(f, aNewTarget);
        }

        value duration_from(value, arguments aArguments, value, function_object&)
        {
            return create_duration(to_temporal_duration(argument(aArguments, 0u)));
        }

        value duration_compare(value, arguments aArguments, value, function_object&)
        {
            auto const one = to_temporal_duration(argument(aArguments, 0u));
            auto const two = to_temporal_duration(argument(aArguments, 1u));
            auto const options = get_options_object(argument(aArguments, 2u));
            auto const relativeTo = get_relative_to_option(options);
            bool same = true;
            for (int i = 0; i < 10; ++i)
                if (one.v[i] != two.v[i])
                    same = false;
            if (same)
                return number(0.0);
            auto const largestUnit1 = default_largest_unit(one);
            auto const largestUnit2 = default_largest_unit(two);
            auto const duration1 = to_internal_duration(one);
            auto const duration2 = to_internal_duration(two);
            if (relativeTo.zoned && (is_date_unit(largestUnit1) || is_date_unit(largestUnit2)))
            {
                auto const after1 = add_zoned_date_time(relativeTo.epochNs, relativeTo.timeZone, relativeTo.calendar, duration1, overflow::Constrain);
                auto const after2 = add_zoned_date_time(relativeTo.epochNs, relativeTo.timeZone, relativeTo.calendar, duration2, overflow::Constrain);
                return number(static_cast<double>(compare(after1, after2)));
            }
            double days1 = one.v[3];
            double days2 = two.v[3];
            if (is_calendar_unit(largestUnit1) || is_calendar_unit(largestUnit2))
            {
                if (!relativeTo.plain)
                    throw_range_error("relativeTo is required to compare durations with calendar units");
                days1 = date_duration_days(duration1.date, relativeTo.date, relativeTo.calendar);
                days2 = date_duration_days(duration2.date, relativeTo.date, relativeTo.calendar);
            }
            auto const time1 = add_24_hour_days(duration1.time, days1);
            auto const time2 = add_24_hour_days(duration2.time, days2);
            return number(static_cast<double>(compare(time1, time2)));
        }

        value duration_field(value aThis, arguments, value, function_object& aSelf)
        {
            auto const& d = this_duration(aThis, "Temporal.Duration getter");
            return number(d.v[aSelf.nativeId] + 0.0);
        }

        value duration_sign_getter(value aThis, arguments, value, function_object&)
        {
            return number(static_cast<double>(duration_sign(this_duration(aThis, "Temporal.Duration.prototype.sign"))));
        }

        value duration_blank(value aThis, arguments, value, function_object&)
        {
            return boolean(duration_sign(this_duration(aThis, "Temporal.Duration.prototype.blank")) == 0);
        }

        value duration_with(value aThis, arguments aArguments, value, function_object&)
        {
            auto const d = this_duration(aThis, "Temporal.Duration.prototype.with");
            bool any = false;
            auto const partial = to_partial_duration(argument(aArguments, 0u), any);
            duration_fields f;
            for (int i = 0; i < 10; ++i)
                f.v[i] = std::isnan(partial.v[i]) ? d.v[i] : partial.v[i];
            return create_duration(f);
        }

        value duration_negated(value aThis, arguments, value, function_object&)
        {
            return create_duration(negate_duration(this_duration(aThis, "Temporal.Duration.prototype.negated")));
        }

        value duration_abs(value aThis, arguments, value, function_object&)
        {
            auto f = this_duration(aThis, "Temporal.Duration.prototype.abs");
            for (auto& v : f.v)
                v = std::fabs(v);
            return create_duration(f);
        }

        value add_durations(bool aSubtract, duration_fields const& aDuration, value aOther)
        {
            auto other = to_temporal_duration(aOther);
            if (aSubtract)
                other = negate_duration(other);
            auto const largestUnit = larger_unit(default_largest_unit(aDuration), default_largest_unit(other));
            if (is_calendar_unit(largestUnit))
                throw_range_error("durations with calendar units cannot be added without a relative date");
            auto const d1 = to_internal_duration_24(aDuration);
            auto const d2 = to_internal_duration_24(other);
            auto const time = add_time_duration(d1.time, d2.time);
            return create_duration(duration_from_internal({ {}, time }, largestUnit));
        }

        value duration_add(value aThis, arguments aArguments, value, function_object&)
        {
            return add_durations(false, this_duration(aThis, "Temporal.Duration.prototype.add"), argument(aArguments, 0u));
        }

        value duration_subtract(value aThis, arguments aArguments, value, function_object&)
        {
            return add_durations(true, this_duration(aThis, "Temporal.Duration.prototype.subtract"), argument(aArguments, 0u));
        }

        value duration_round(value aThis, arguments aArguments, value, function_object&)
        {
            auto const duration = this_duration(aThis, "Temporal.Duration.prototype.round");
            auto const roundTo = round_to_options(argument(aArguments, 0u), "smallestUnit");
            bool smallestUnitPresent = true;
            bool largestUnitPresent = true;
            auto largestUnit = get_unit_option(roundTo, "largestUnit", false);
            auto const relativeTo = get_relative_to_option(roundTo);
            auto const roundingIncrement = get_rounding_increment_option(roundTo);
            auto const roundingMode = get_rounding_mode_option(roundTo, rounding_mode::HalfExpand);
            auto smallestUnit = get_unit_option(roundTo, "smallestUnit", false);
            validate_unit(smallestUnit, unit_group::DateTime);
            if (smallestUnit == unit::Unset)
            {
                smallestUnitPresent = false;
                smallestUnit = unit::Nanosecond;
            }
            auto const existingLargestUnit = default_largest_unit(duration);
            auto const defaultLargestUnit = larger_unit(existingLargestUnit, smallestUnit);
            if (largestUnit == unit::Unset)
            {
                largestUnitPresent = false;
                largestUnit = defaultLargestUnit;
            }
            else if (largestUnit == unit::Auto)
                largestUnit = defaultLargestUnit;
            if (!smallestUnitPresent && !largestUnitPresent)
                throw_range_error("smallestUnit or largestUnit is required");
            if (larger_unit(largestUnit, smallestUnit) != largestUnit)
                throw_range_error("largestUnit must not be smaller than smallestUnit");
            auto const maximum = maximum_rounding_increment(smallestUnit);
            if (maximum != 0)
                validate_rounding_increment(roundingIncrement, maximum, false);
            if (roundingIncrement > 1 && largestUnit != smallestUnit && is_date_unit(smallestUnit))
                throw_range_error("roundingIncrement must be 1 when rounding to a date unit smaller than largestUnit");
            if (relativeTo.zoned)
            {
                auto internal = to_internal_duration(duration);
                auto const target = add_zoned_date_time(relativeTo.epochNs, relativeTo.timeZone, relativeTo.calendar, internal, overflow::Constrain);
                internal = difference_zoned_date_time_with_rounding(relativeTo.epochNs, target, relativeTo.timeZone, relativeTo.calendar, largestUnit,
                    roundingIncrement, smallestUnit, roundingMode);
                if (is_date_unit(largestUnit))
                    largestUnit = unit::Hour;
                return create_duration(duration_from_internal(internal, largestUnit));
            }
            if (relativeTo.plain)
            {
                auto internal = to_internal_duration_24(duration);
                auto const targetTime = add_time({}, internal.time);
                auto const dateDuration = create_date_duration(internal.date.years, internal.date.months, internal.date.weeks,
                    static_cast<double>(targetTime.days));
                auto const targetDate = calendar_date_add(relativeTo.calendar, relativeTo.date, dateDuration, overflow::Constrain);
                iso_date_time const dateTime{ relativeTo.date, {} };
                iso_date_time const targetDateTime{ targetDate, targetTime };
                internal = difference_plain_date_time_with_rounding(relativeTo.calendar, dateTime, targetDateTime, largestUnit, roundingIncrement,
                    smallestUnit, roundingMode);
                return create_duration(duration_from_internal(internal, largestUnit));
            }
            if (is_calendar_unit(existingLargestUnit) || is_calendar_unit(largestUnit))
                throw_range_error("relativeTo is required to round durations with calendar units");
            auto internal = to_internal_duration_24(duration);
            if (smallestUnit == unit::Day)
            {
                auto const days = round_to_increment(internal.time, i128{ nsPerDay } * i128{ roundingIncrement }, roundingMode) / i128{ nsPerDay };
                auto const dateDuration = create_date_duration(0.0, 0.0, 0.0, days.to_double());
                internal = { dateDuration, i128{} };
            }
            else
                internal = { {}, round_time_duration(internal.time, roundingIncrement, smallestUnit, roundingMode) };
            return create_duration(duration_from_internal(internal, largestUnit));
        }

        value duration_total(value aThis, arguments aArguments, value, function_object&)
        {
            auto const duration = this_duration(aThis, "Temporal.Duration.prototype.total");
            auto const totalOf = round_to_options(argument(aArguments, 0u), "unit");
            auto const relativeTo = get_relative_to_option(totalOf);
            auto const u = get_unit_option(totalOf, "unit", true);
            validate_unit(u, unit_group::DateTime);
            double total = 0.0;
            if (relativeTo.zoned)
            {
                auto const internal = to_internal_duration(duration);
                auto const target = add_zoned_date_time(relativeTo.epochNs, relativeTo.timeZone, relativeTo.calendar, internal, overflow::Constrain);
                total = difference_zoned_date_time_with_total(relativeTo.epochNs, target, relativeTo.timeZone, relativeTo.calendar, u);
            }
            else if (relativeTo.plain)
            {
                auto const internal = to_internal_duration_24(duration);
                auto const targetTime = add_time({}, internal.time);
                auto const dateDuration = create_date_duration(internal.date.years, internal.date.months, internal.date.weeks,
                    static_cast<double>(targetTime.days));
                auto const targetDate = calendar_date_add(relativeTo.calendar, relativeTo.date, dateDuration, overflow::Constrain);
                total = difference_plain_date_time_with_total(relativeTo.calendar, { relativeTo.date, {} }, { targetDate, targetTime }, u);
            }
            else
            {
                if (is_calendar_unit(default_largest_unit(duration)) || is_calendar_unit(u))
                    throw_range_error("relativeTo is required to total durations with calendar units");
                total = total_time_duration(to_internal_duration_24(duration).time, u);
            }
            return number(total);
        }

        value duration_to_string_method(value aThis, arguments aArguments, value, function_object&)
        {
            auto const duration = this_duration(aThis, "Temporal.Duration.prototype.toString");
            auto const options = get_options_object(argument(aArguments, 0u));
            auto const digits = get_fractional_second_digits_option(options);
            auto const roundingMode = get_rounding_mode_option(options, rounding_mode::Trunc);
            auto const smallestUnit = get_unit_option(options, "smallestUnit", false);
            validate_unit(smallestUnit, unit_group::Time);
            if (smallestUnit == unit::Hour || smallestUnit == unit::Minute)
                throw_range_error("smallestUnit must be seconds or smaller");
            auto const precision = to_seconds_string_precision(smallestUnit, digits);
            if (precision.u == unit::Nanosecond && precision.increment == 1)
                return string_value(duration_to_string(duration, precision.precision));
            auto const largestUnit = default_largest_unit(duration);
            auto internal = to_internal_duration(duration);
            internal.time = round_time_duration(internal.time, precision.increment, precision.u, roundingMode);
            auto const rounded = duration_from_internal(internal, larger_unit(largestUnit, unit::Second));
            return string_value(duration_to_string(rounded, precision.precision));
        }

        value duration_to_json(value aThis, arguments, value, function_object&)
        {
            return string_value(duration_to_string(this_duration(aThis, "Temporal.Duration.prototype.toJSON"), -1));
        }

        value duration_to_locale_string(value aThis, arguments aArguments, value, function_object&)
        {
#if defined(NEOS_JS_ICU)
            this_duration(aThis, "Temporal.Duration.prototype.toLocaleString");
            return intl::duration_to_locale_string(aThis, argument(aArguments, 0u), argument(aArguments, 1u));
#else
            (void)aArguments;
            return string_value(duration_to_string(this_duration(aThis, "Temporal.Duration.prototype.toLocaleString"), -1));
#endif
        }

        // --- Temporal.Instant

        i128 const& this_instant(value aThis, char const* aName)
        {
            return this_temporal(aThis, object_class::TemporalInstant, aName).epochNs;
        }

        i128 epoch_ns_argument(value aValue)
        {
            auto const big = to_bigint(aValue);
            auto const ns = bigint_to_i128(big);
            if (!ns || !is_valid_epoch_ns(*ns))
                throw_range_error("epoch nanoseconds are outside the representable range");
            return *ns;
        }

        value instant_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (is_undefined(aNewTarget))
                throw_type_error("Temporal.Instant must be called with new");
            auto const ns = epoch_ns_argument(argument(aArguments, 0u));
            return create_instant(ns, aNewTarget);
        }

        value instant_from(value, arguments aArguments, value, function_object&)
        {
            return create_instant(to_temporal_instant(argument(aArguments, 0u)));
        }

        value instant_from_epoch_milliseconds(value, arguments aArguments, value, function_object&)
        {
            auto const ms = to_number(argument(aArguments, 0u));
            auto const big = number_to_bigint(ms);
            auto const value = bigint_to_i128(big);
            if (!value || value->abs() > i128{ 8640000000000000LL })
                throw_range_error("epoch milliseconds are outside the representable range");
            auto const ns = *value * i128{ 1000000 };
            if (!is_valid_epoch_ns(ns))
                throw_range_error("epoch milliseconds are outside the representable range");
            return create_instant(ns);
        }

        value instant_from_epoch_nanoseconds(value, arguments aArguments, value, function_object&)
        {
            return create_instant(epoch_ns_argument(argument(aArguments, 0u)));
        }

        value instant_compare(value, arguments aArguments, value, function_object&)
        {
            auto const one = to_temporal_instant(argument(aArguments, 0u));
            auto const two = to_temporal_instant(argument(aArguments, 1u));
            return number(static_cast<double>(compare(one, two)));
        }

        value instant_epoch_milliseconds(value aThis, arguments, value, function_object&)
        {
            auto const& ns = this_instant(aThis, "Temporal.Instant.prototype.epochMilliseconds");
            return number(floor_div(ns, i128{ 1000000 }).to_double());
        }

        value instant_epoch_nanoseconds(value aThis, arguments, value, function_object&)
        {
            return make_bigint_from(this_instant(aThis, "Temporal.Instant.prototype.epochNanoseconds"));
        }

        value add_duration_to_instant(bool aSubtract, i128 const& aEpochNs, value aLike)
        {
            auto duration = to_temporal_duration(aLike);
            if (aSubtract)
                duration = negate_duration(duration);
            if (is_date_unit(default_largest_unit(duration)))
                throw_range_error("durations with date units cannot be added to an instant");
            auto const internal = to_internal_duration_24(duration);
            return create_instant(add_instant(aEpochNs, internal.time));
        }

        value instant_add(value aThis, arguments aArguments, value, function_object&)
        {
            return add_duration_to_instant(false, this_instant(aThis, "Temporal.Instant.prototype.add"), argument(aArguments, 0u));
        }

        value instant_subtract(value aThis, arguments aArguments, value, function_object&)
        {
            return add_duration_to_instant(true, this_instant(aThis, "Temporal.Instant.prototype.subtract"), argument(aArguments, 0u));
        }

        value difference_temporal_instant(bool aSince, i128 const& aEpochNs, value aOther, value aOptions)
        {
            auto const other = to_temporal_instant(aOther);
            auto const options = get_options_object(aOptions);
            auto const settings = get_difference_settings(aSince, options, unit_group::Time, {}, unit::Nanosecond, unit::Second);
            auto const internal = difference_instant(aEpochNs, other, settings.roundingIncrement, settings.smallestUnit, settings.roundingMode);
            auto result = duration_from_internal(internal, settings.largestUnit);
            if (aSince)
                result = negate_duration(result);
            return create_duration(result);
        }

        value instant_until(value aThis, arguments aArguments, value, function_object&)
        {
            return difference_temporal_instant(false, this_instant(aThis, "Temporal.Instant.prototype.until"), argument(aArguments, 0u),
                argument(aArguments, 1u));
        }

        value instant_since(value aThis, arguments aArguments, value, function_object&)
        {
            return difference_temporal_instant(true, this_instant(aThis, "Temporal.Instant.prototype.since"), argument(aArguments, 0u),
                argument(aArguments, 1u));
        }

        value instant_round(value aThis, arguments aArguments, value, function_object&)
        {
            auto const ns = this_instant(aThis, "Temporal.Instant.prototype.round");
            auto const roundTo = round_to_options(argument(aArguments, 0u), "smallestUnit");
            auto const roundingIncrement = get_rounding_increment_option(roundTo);
            auto const roundingMode = get_rounding_mode_option(roundTo, rounding_mode::HalfExpand);
            auto const smallestUnit = get_unit_option(roundTo, "smallestUnit", true);
            validate_unit(smallestUnit, unit_group::Time);
            std::int64_t maximum = 0;
            switch (smallestUnit)
            {
            case unit::Hour: maximum = 24; break;
            case unit::Minute: maximum = 1440; break;
            case unit::Second: maximum = 86400; break;
            case unit::Millisecond: maximum = 86400000LL; break;
            case unit::Microsecond: maximum = 86400000000LL; break;
            default: maximum = nsPerDay; break;
            }
            validate_rounding_increment(roundingIncrement, maximum, true);
            auto const rounded = round_to_increment_as_if_positive(ns, unit_length(smallestUnit) * i128{ roundingIncrement }, roundingMode);
            return create_instant(rounded);
        }

        value instant_equals(value aThis, arguments aArguments, value, function_object&)
        {
            auto const ns = this_instant(aThis, "Temporal.Instant.prototype.equals");
            auto const other = to_temporal_instant(argument(aArguments, 0u));
            return boolean(ns == other);
        }

        std::string temporal_instant_to_string(i128 const& aEpochNs, std::optional<time_zone> const& aTimeZone, int aPrecision)
        {
            auto const outputTimeZone = aTimeZone ? *aTimeZone : time_zone_from_identifier(u"UTC");
            auto const dateTime = get_iso_date_time_for(outputTimeZone, aEpochNs);
            auto const dateTimeString = iso_date_time_to_string(dateTime, aPrecision, calendar_id::Iso8601, show_calendar::Never);
            if (!aTimeZone)
                return dateTimeString + "Z";
            return dateTimeString + format_utc_offset_rounded(get_offset_ns_for(outputTimeZone, aEpochNs));
        }

        value instant_to_string(value aThis, arguments aArguments, value, function_object&)
        {
            auto const ns = this_instant(aThis, "Temporal.Instant.prototype.toString");
            auto const options = get_options_object(argument(aArguments, 0u));
            auto const digits = get_fractional_second_digits_option(options);
            auto const roundingMode = get_rounding_mode_option(options, rounding_mode::Trunc);
            auto const smallestUnit = get_unit_option(options, "smallestUnit", false);
            auto const timeZoneValue = get(options, ascii("timeZone"));
            validate_unit(smallestUnit, unit_group::Time);
            if (smallestUnit == unit::Hour)
                throw_range_error("smallestUnit must not be hour");
            std::optional<time_zone> timeZone;
            if (!is_undefined(timeZoneValue))
                timeZone = to_temporal_time_zone_identifier(timeZoneValue);
            auto const precision = to_seconds_string_precision(smallestUnit, digits);
            auto const rounded = round_to_increment_as_if_positive(ns, unit_length(precision.u) * i128{ precision.increment }, roundingMode);
            return string_value(temporal_instant_to_string(rounded, timeZone, precision.precision));
        }

        value instant_to_json(value aThis, arguments, value, function_object&)
        {
            return string_value(temporal_instant_to_string(this_instant(aThis, "Temporal.Instant.prototype.toJSON"), {}, -1));
        }

        value instant_to_locale_string(value aThis, arguments aArguments, value, function_object&)
        {
#if defined(NEOS_JS_ICU)
            this_instant(aThis, "Temporal.Instant.prototype.toLocaleString");
            return intl::temporal_to_locale_string(aThis, argument(aArguments, 0u), argument(aArguments, 1u));
#else
            (void)aArguments;
            return string_value(temporal_instant_to_string(this_instant(aThis, "Temporal.Instant.prototype.toLocaleString"), {}, -1));
#endif
        }

        value instant_to_zoned_date_time_iso(value aThis, arguments aArguments, value, function_object&)
        {
            auto const ns = this_instant(aThis, "Temporal.Instant.prototype.toZonedDateTimeISO");
            auto const timeZone = to_temporal_time_zone_identifier(argument(aArguments, 0u));
            return create_zoned_date_time(ns, timeZone, calendar_id::Iso8601);
        }

        // --- Temporal.Now

        iso_date_time system_date_time(value aTimeZoneLike)
        {
            auto const timeZone = is_undefined(aTimeZoneLike) ? system_time_zone() : to_temporal_time_zone_identifier(aTimeZoneLike);
            return get_iso_date_time_for(timeZone, system_utc_epoch_ns());
        }

        value now_time_zone_id(value, arguments, value, function_object&)
        {
            return system_time_zone().id;
        }

        value now_instant(value, arguments, value, function_object&)
        {
            return create_instant(system_utc_epoch_ns());
        }

        value now_zoned_date_time_iso(value, arguments aArguments, value, function_object&)
        {
            auto const like = argument(aArguments, 0u);
            auto const timeZone = is_undefined(like) ? system_time_zone() : to_temporal_time_zone_identifier(like);
            return create_zoned_date_time(system_utc_epoch_ns(), timeZone, calendar_id::Iso8601);
        }

        value now_plain_date_time_iso(value, arguments aArguments, value, function_object&)
        {
            return create_plain_date_time(system_date_time(argument(aArguments, 0u)), calendar_id::Iso8601);
        }

        value now_plain_date_iso(value, arguments aArguments, value, function_object&)
        {
            return create_plain_date(system_date_time(argument(aArguments, 0u)).date, calendar_id::Iso8601);
        }

        value now_plain_time_iso(value, arguments aArguments, value, function_object&)
        {
            return create_plain_time(system_date_time(argument(aArguments, 0u)).time);
        }

        // --- Date.prototype.toTemporalInstant

        value date_to_temporal_instant(value aThis, arguments, value, function_object&)
        {
            if (!is_object(aThis) || obj(aThis).cls != object_class::Date)
                throw_type_error("Date.prototype.toTemporalInstant called on an incompatible receiver");
            auto const t = as_number(obj(aThis).internal);
            if (std::isnan(t))
                throw_range_error("Invalid Date");
            auto const ns = i128{ static_cast<std::int64_t>(t) } * i128{ 1000000 };
            return create_instant(ns);
        }
    }

    void init_temporal(realm& aRealm)
    {
        auto const ns = make_object();
        obj(ns).put(aRealm.symToStringTag, ascii("Temporal"), AttributeConfigurable);
        obj(aRealm.global).put(ascii("Temporal"), ns);

        // Temporal.Duration
        auto const dc = make_temporal_constructor(aRealm, ns, "Duration", 0u, duration_constructor, &realm::temporalDurationPrototype,
            &realm::temporalDuration, "Temporal.Duration");
        auto const dp = aRealm.temporalDurationPrototype;
        method(dc, "from", 1u, duration_from);
        method(dc, "compare", 2u, duration_compare);
        static char const* const sDurationFields[] = { "years", "months", "weeks", "days", "hours", "minutes", "seconds", "milliseconds",
            "microseconds", "nanoseconds" };
        for (std::uint32_t i = 0u; i < 10u; ++i)
            as_function(getter(dp, ascii(sDurationFields[i]), duration_field))->nativeId = i;
        getter(dp, ascii("sign"), duration_sign_getter);
        getter(dp, ascii("blank"), duration_blank);
        method(dp, "with", 1u, duration_with);
        method(dp, "negated", 0u, duration_negated);
        method(dp, "abs", 0u, duration_abs);
        method(dp, "add", 1u, duration_add);
        method(dp, "subtract", 1u, duration_subtract);
        method(dp, "round", 1u, duration_round);
        method(dp, "total", 1u, duration_total);
        method(dp, "toString", 0u, duration_to_string_method);
        method(dp, "toJSON", 0u, duration_to_json);
        method(dp, "toLocaleString", 0u, duration_to_locale_string);
        method(dp, "valueOf", 0u, value_of_throws);

        // Temporal.Instant
        auto const ic = make_temporal_constructor(aRealm, ns, "Instant", 1u, instant_constructor, &realm::temporalInstantPrototype,
            &realm::temporalInstant, "Temporal.Instant");
        auto const ip = aRealm.temporalInstantPrototype;
        method(ic, "from", 1u, instant_from);
        method(ic, "fromEpochMilliseconds", 1u, instant_from_epoch_milliseconds);
        method(ic, "fromEpochNanoseconds", 1u, instant_from_epoch_nanoseconds);
        method(ic, "compare", 2u, instant_compare);
        getter(ip, ascii("epochMilliseconds"), instant_epoch_milliseconds);
        getter(ip, ascii("epochNanoseconds"), instant_epoch_nanoseconds);
        method(ip, "add", 1u, instant_add);
        method(ip, "subtract", 1u, instant_subtract);
        method(ip, "until", 1u, instant_until);
        method(ip, "since", 1u, instant_since);
        method(ip, "round", 1u, instant_round);
        method(ip, "equals", 1u, instant_equals);
        method(ip, "toString", 0u, instant_to_string);
        method(ip, "toLocaleString", 0u, instant_to_locale_string);
        method(ip, "toJSON", 0u, instant_to_json);
        method(ip, "valueOf", 0u, value_of_throws);
        method(ip, "toZonedDateTimeISO", 1u, instant_to_zoned_date_time_iso);

        init_temporal_plain(aRealm, ns);
        init_temporal_zoned(aRealm, ns);

        // Temporal.Now
        auto const now = make_object();
        obj(now).put(aRealm.symToStringTag, ascii("Temporal.Now"), AttributeConfigurable);
        method(now, "timeZoneId", 0u, now_time_zone_id);
        method(now, "instant", 0u, now_instant);
        method(now, "plainDateTimeISO", 0u, now_plain_date_time_iso);
        method(now, "zonedDateTimeISO", 0u, now_zoned_date_time_iso);
        method(now, "plainDateISO", 0u, now_plain_date_iso);
        method(now, "plainTimeISO", 0u, now_plain_time_iso);
        obj(ns).put(ascii("Now"), now);

        // Date.prototype.toTemporalInstant
        method(aRealm.datePrototype, "toTemporalInstant", 0u, date_to_temporal_instant);
    }

    void temporal::temporal_object::trace(tracer& aTracer)
    {
        object::trace(aTracer);
        aTracer.mark(timeZone.id);
    }
}
