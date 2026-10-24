/*
  intl_duration_format.cpp

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

// Intl.DurationFormat (ECMA-402 §14): durations as lists (Intl.ListFormat's, of type unit) of numbers (Intl.NumberFormat's, of unit
// style; or numeric (h:mm:ss) with the locale's digital separators (ICU's unit data's durationUnits patterns)).

#include "intl.hpp"

#if defined(NEOS_JS_ICU)

#include <algorithm>
#include <cmath>
#include "intl_icu.hpp"
#include "temporal.hpp"

namespace neos::javascript_runtime::intl
{
    namespace
    {
        enum unit_index : std::size_t { Years, Months, Weeks, Days, Hours, Minutes, Seconds, Milliseconds, Microseconds, Nanoseconds, UnitCount };

        struct unit_row
        {
            char const* unit;               ///< (the option: plural)
            char const* numberFormatUnit;   ///< (singular)
            bool numeric;                   ///< (numeric and 2-digit styles allowed (hours, minutes, seconds))
            bool fractional;                ///< (a fractional second unit (numeric style allowed: it becomes fractional))
        };

        constexpr unit_row sUnits[UnitCount] = {
            { "years", "year", false, false }, { "months", "month", false, false }, { "weeks", "week", false, false }, { "days", "day", false, false },
            { "hours", "hour", true, false }, { "minutes", "minute", true, false }, { "seconds", "second", true, false },
            { "milliseconds", "millisecond", false, true }, { "microseconds", "microsecond", false, true }, { "nanoseconds", "nanosecond", false, true } };

        struct unit_options
        {
            std::string style;
            std::string display;
        };

        struct duration_format_data : intl_data
        {
            std::string dataLocale;
            std::string numberingSystem;
            std::string style;
            unit_options units[UnitCount];
            std::u16string hourMinuteSeparator = u":";
            std::u16string minuteSecondSeparator = u":";
            std::optional<int> fractionalDigits;
        };

        struct duration_locale_data : locale_data
        {
            std::vector<std::string> values(std::string const& aDataLocale, std::string_view aKey) const override
            {
                if (aKey != "nu")
                    return {};
                return { default_numbering_system(aDataLocale) };
            }
            bool supports(std::string const& aDataLocale, std::string_view aKey, std::string const& aValue) const override
            {
                return aKey == "nu" && is_supported_numbering_system(aDataLocale, aValue);
            }
        };

        // (the locale's digital format: its separators and whether its hours are two digits (ICU's durationUnits: e.g. "h:mm", "m:ss"))
        void digital_format(std::string const& aDataLocale, duration_format_data& d, bool& aTwoDigitHours)
        {
            aTwoDigitHours = false;
            auto const icu = icu_locale(aDataLocale);
            UErrorCode status = U_ZERO_ERROR;
            resource_ptr units{ ures_open("ICUDATA-unit", icu.c_str(), &status) };
            if (U_FAILURE(status))
                return;
            resource_ptr durationUnits{ ures_getByKey(units.get(), "durationUnits", nullptr, &status) };
            if (U_FAILURE(status))
                return;
            auto const pattern = [&](char const* aKey) -> std::u16string
            {
                UErrorCode s = U_ZERO_ERROR;
                std::int32_t length = 0;
                auto const* text = ures_getStringByKey(durationUnits.get(), aKey, &length, &s);
                if (U_FAILURE(s) || text == nullptr)
                    return {};
                return std::u16string{ reinterpret_cast<char16_t const*>(text), static_cast<std::size_t>(length) };
            };
            // (the separator: the text between the fields (unquoted))
            auto const separator = [](std::u16string const& aPattern, char16_t aFirst, char16_t aSecond) -> std::optional<std::u16string>
            {
                auto const first = aPattern.find_last_of(aFirst);
                auto const second = aPattern.find(aSecond, first == std::u16string::npos ? 0u : first);
                if (first == std::u16string::npos || second == std::u16string::npos || second <= first)
                    return std::nullopt;
                std::u16string result;
                for (auto c : aPattern.substr(first + 1u, second - first - 1u))
                    if (c != u'\'')
                        result.push_back(c);
                return result;
            };
            auto const hm = pattern("hm");
            if (auto const s = separator(hm, u'h', u'm'))
                d.hourMinuteSeparator = *s;
            if (auto const s = separator(pattern("ms"), u'm', u's'))
                d.minuteSecondSeparator = *s;
            aTwoDigitHours = hm.find(u"hh") != std::u16string::npos || hm.find(u"HH") != std::u16string::npos;
        }

        // (GetDurationUnitOptions (and ValidateDurationUnitStyle))
        unit_options get_duration_unit_options(std::size_t aUnit, value aOptions, std::string const& aBaseStyle, std::string const& aPrevStyle,
            bool aTwoDigitHours)
        {
            auto const& row = sUnits[aUnit];
            std::optional<std::u16string> styleOption;
            if (row.numeric)
                styleOption = get_string_option(aOptions, row.unit, { "long", "short", "narrow", "numeric", "2-digit" });
            else if (row.fractional)
                styleOption = get_string_option(aOptions, row.unit, { "long", "short", "narrow", "numeric" });
            else
                styleOption = get_string_option(aOptions, row.unit, { "long", "short", "narrow" });
            std::string displayDefault = "always";
            std::string style;
            if (!styleOption)
            {
                if (aBaseStyle == "digital")
                {
                    style = row.numeric || row.fractional ? "numeric" : "short";
                    if (aUnit != Hours && aUnit != Minutes && aUnit != Seconds)
                        displayDefault = "auto";
                }
                else if (aPrevStyle == "fractional" || aPrevStyle == "numeric" || aPrevStyle == "2-digit")
                {
                    style = "numeric";
                    if (aUnit != Minutes && aUnit != Seconds)
                        displayDefault = "auto";
                }
                else
                {
                    style = aBaseStyle;
                    displayDefault = "auto";
                }
            }
            else
                style = to_ascii(*styleOption);
            if (style == "numeric" && row.fractional)
            {
                style = "fractional";
                displayDefault = "auto";
            }
            auto const displayField = std::string{ row.unit } + "Display";
            auto const display = to_ascii(*get_string_option(aOptions, displayField.c_str(), { "auto", "always" }, displayDefault.c_str()));
            if (display == "always" && style == "fractional")
                throw_range_error(std::string{ "Intl.DurationFormat: " } + row.unit + " cannot be fractional and always displayed");
            if (aPrevStyle == "fractional" && style != "fractional")
                throw_range_error(std::string{ "Intl.DurationFormat: " } + row.unit + " must be fractional (as the unit before it is)");
            if ((aPrevStyle == "numeric" || aPrevStyle == "2-digit") && style != "fractional" && style != "numeric" && style != "2-digit")
                throw_range_error(std::string{ "Intl.DurationFormat: " } + row.unit + " must be numeric (as the unit before it is)");
            if (aUnit == Hours && aTwoDigitHours)
                style = "2-digit";
            if ((aUnit == Minutes || aUnit == Seconds) && (aPrevStyle == "numeric" || aPrevStyle == "2-digit"))
                style = "2-digit";
            return { style, display };
        }

        value duration_format_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (is_undefined(aNewTarget))
                throw_type_error("Constructor Intl.DurationFormat requires 'new'");
            auto const result = make_intl_object(aNewTarget, &realm::intlDurationFormatPrototype, object_class::IntlDurationFormat);
            auto& df = static_cast<intl_object&>(obj(result));
            auto data = std::make_unique<duration_format_data>();
            auto& d = *data;
            auto const requested = canonicalize_locale_list(argument(aArguments, 0u));
            auto const options = get_options_object(argument(aArguments, 1u));
            auto const matcher = *get_string_option(options, "localeMatcher", { "lookup", "best fit" }, "best fit");
            auto const numberingSystem = get_string_option(options, "numberingSystem", {});
            if (numberingSystem && !is_type_sequence(*numberingSystem))
                throw_range_error("Invalid numberingSystem : " + to_ascii(*numberingSystem));
            std::vector<relevant_key> keys{ { "nu", numberingSystem ? std::optional<std::string>{ to_ascii(*numberingSystem) } : std::nullopt } };
            auto const r = resolve_locale(service::DurationFormat, requested, matcher, keys, duration_locale_data{});
            df.locale = r.locale;
            d.dataLocale = r.dataLocale;
            bool twoDigitHours = false;
            digital_format(d.dataLocale, d, twoDigitHours);
            d.numberingSystem = r.keywords.at("nu");
            d.style = to_ascii(*get_string_option(options, "style", { "long", "short", "narrow", "digital" }, "short"));
            std::string prevStyle;
            for (std::size_t u = 0u; u < UnitCount; ++u)
            {
                d.units[u] = get_duration_unit_options(u, options, d.style, prevStyle, twoDigitHours);
                if (u >= Hours && u <= Microseconds)
                    prevStyle = d.units[u].style;
            }
            d.fractionalDigits = get_number_option(options, "fractionalDigits", 0, 9, std::nullopt);
            df.data = std::move(data);
            return result;
        }

        // --- duration records

        // (ToIntegerIfIntegral)
        double to_integer_if_integral(value aValue)
        {
            auto const n = to_number(aValue);
            if (!std::isfinite(n) || std::trunc(n) != n)
                throw_range_error("Intl.DurationFormat: a duration's fields must be integers");
            return n + 0.0;
        }

        // (ToDurationRecord)
        temporal::duration_fields to_duration_record(value aInput)
        {
            // (a Temporal.Duration's fields (its getters are not called); a string's (a Temporal duration string))
            if (auto const* t = temporal::as_temporal(aInput, object_class::TemporalDuration))
                return t->duration;
            if (is_string(aInput))
                return temporal::to_temporal_duration(aInput);
            if (!is_object(aInput))
                throw_type_error("Intl.DurationFormat: a duration must be an object");
            temporal::duration_fields result;
            // (the fields are read in alphabetical order)
            static constexpr std::size_t sOrder[] = { Days, Hours, Microseconds, Milliseconds, Minutes, Months, Nanoseconds, Seconds, Weeks, Years };
            bool any = false;
            for (auto u : sOrder)
            {
                auto const v = get(aInput, ascii(sUnits[u].unit));
                if (!is_undefined(v))
                {
                    result.v[u] = to_integer_if_integral(v);
                    any = true;
                }
            }
            if (!any)
                throw_type_error("Intl.DurationFormat: a duration must have at least one of its fields");
            if (!temporal::is_valid_duration(result))
                throw_range_error("Intl.DurationFormat: the duration is not valid");
            return result;
        }

        bool next_unit_fractional(duration_format_data const& d, std::size_t aUnit)
        {
            return (aUnit == Seconds || aUnit == Milliseconds || aUnit == Microseconds) && d.units[aUnit + 1u].style == "fractional";
        }

        // (a unit's value as an exact decimal, with the fractional units after it (ComputeFractionalDigits) if they are ("-0" if it is
        // zero and the duration negative and aNegativeZero))
        std::string unit_value(duration_format_data const& d, temporal::duration_fields const& aDuration, std::size_t aUnit, bool aWithFraction,
            bool aNegativeZero)
        {
            using temporal::i128;
            i128 total = i128::from_double(aDuration.v[aUnit]);
            std::size_t digits = 0u;
            if (aWithFraction)
                for (auto u = aUnit + 1u; u < UnitCount && d.units[u].style == "fractional"; ++u)
                {
                    total = total * i128{ 1000 } + i128::from_double(aDuration.v[u]);
                    digits += 3u;
                }
            if (total == i128{ 0 })
                return aNegativeZero && temporal::duration_sign(aDuration) == -1 ? "-0" : "0";
            bool const negative = total < i128{ 0 };
            auto text = total.abs().to_string();
            if (digits > 0u)
            {
                if (text.size() <= digits)
                    text.insert(0u, digits - text.size() + 1u, '0');
                text.insert(text.size() - digits, ".");
            }
            return negative ? "-" + text : text;
        }

        using parts_list = std::vector<part>;

        parts_list with_unit(parts_list aParts, char const* aUnit)
        {
            for (auto& p : aParts)
                p.unit = from_ascii(aUnit);
            return aParts;
        }

        // (FormatNumericHours, FormatNumericMinutes, FormatNumericSeconds)
        parts_list format_numeric_unit(intl_object const& aDf, std::size_t aUnit, std::string const& aValue, bool aSeparator, bool aSignDisplayed)
        {
            auto const& d = data_of<duration_format_data>(const_cast<intl_object&>(aDf));
            parts_list result;
            if (aSeparator)
                result.push_back({ u"literal", aUnit == Minutes ? d.hourMinuteSeparator : d.minuteSecondSeparator });
            auto const options = make_object(Null);
            auto const put = [&](char const* aName, value aValue) { create_data_property_or_throw(options, ascii(aName), aValue); };
            put("numberingSystem", u16_value(from_ascii(d.numberingSystem)));
            if (d.units[aUnit].style == "2-digit")
                put("minimumIntegerDigits", number(2));
            if (!aSignDisplayed)
                put("signDisplay", ascii("never"));
            put("useGrouping", False);
            if (aUnit == Seconds)
            {
                put("maximumFractionDigits", number(d.fractionalDigits ? *d.fractionalDigits : 9));
                put("minimumFractionDigits", number(d.fractionalDigits ? *d.fractionalDigits : 0));
                put("roundingMode", ascii("trunc"));
            }
            value const arguments[] = { u16_value(from_ascii(aDf.locale)), options };
            auto const nf = construct(the_realm().intlNumberFormat, arguments);
            for (auto& p : with_unit(format_decimal_to_parts(nf, aValue), sUnits[aUnit].numberFormatUnit))
                result.push_back(std::move(p));
            return result;
        }

        // (FormatNumericUnits)
        parts_list format_numeric_units(intl_object const& aDf, temporal::duration_fields const& aDuration, std::size_t aFirstNumericUnit,
            bool aSignDisplayed)
        {
            auto const& d = data_of<duration_format_data>(const_cast<intl_object&>(aDf));
            parts_list result;
            auto const hoursValue = aDuration.v[Hours];
            auto const minutesValue = aDuration.v[Minutes];
            bool const fraction = aDuration.v[Milliseconds] != 0 || aDuration.v[Microseconds] != 0 || aDuration.v[Nanoseconds] != 0;
            auto const secondsValue = unit_value(d, aDuration, Seconds, fraction, false);
            bool hoursFormatted = false;
            if (aFirstNumericUnit == Hours && (hoursValue != 0 || d.units[Hours].display == "always"))
                hoursFormatted = true;
            bool const secondsFormatted = secondsValue != "0" || d.units[Seconds].display == "always";
            bool minutesFormatted = false;
            if (aFirstNumericUnit == Hours || aFirstNumericUnit == Minutes)
            {
                if (hoursFormatted && secondsFormatted)
                    minutesFormatted = true;
                else if (minutesValue != 0 || d.units[Minutes].display == "always")
                    minutesFormatted = true;
            }
            if (hoursFormatted)
            {
                auto const v = unit_value(d, aDuration, Hours, false, aSignDisplayed);
                auto parts = format_numeric_unit(aDf, Hours, v, false, aSignDisplayed);
                result.insert(result.end(), parts.begin(), parts.end());
                aSignDisplayed = false;
            }
            if (minutesFormatted)
            {
                auto const v = unit_value(d, aDuration, Minutes, false, aSignDisplayed);
                auto parts = format_numeric_unit(aDf, Minutes, v, hoursFormatted, aSignDisplayed);
                result.insert(result.end(), parts.begin(), parts.end());
                aSignDisplayed = false;
            }
            if (secondsFormatted)
            {
                auto parts = format_numeric_unit(aDf, Seconds, secondsValue, minutesFormatted, aSignDisplayed);
                result.insert(result.end(), parts.begin(), parts.end());
            }
            return result;
        }

        // (ListFormatParts)
        parts_list list_format_parts(intl_object const& aDf, std::vector<parts_list> const& aPartitioned)
        {
            auto const& d = data_of<duration_format_data>(const_cast<intl_object&>(aDf));
            std::vector<std::u16string> strings;
            for (auto const& parts : aPartitioned)
            {
                std::u16string s;
                for (auto const& p : parts)
                    s += p.text;
                strings.push_back(std::move(s));
            }
            auto const listStyle = d.style == "digital" ? std::string{ "short" } : d.style;
            parts_list result;
            std::size_t index = 0u;
            for (auto const& listPart : format_list_to_parts(d.dataLocale, "unit", listStyle, strings))
            {
                if (listPart.type == u"element" && index < aPartitioned.size())
                {
                    auto const& parts = aPartitioned[index++];
                    result.insert(result.end(), parts.begin(), parts.end());
                }
                else
                    result.push_back({ u"literal", listPart.text });
            }
            return result;
        }

        // (PartitionDurationFormatPattern)
        parts_list partition_duration_format_pattern(intl_object const& aDf, temporal::duration_fields const& aDuration)
        {
            auto const& d = data_of<duration_format_data>(const_cast<intl_object&>(aDf));
            std::vector<parts_list> result;
            bool signDisplayed = true;
            for (std::size_t u = 0u; u < UnitCount; ++u)
            {
                auto const& options = d.units[u];
                if (options.style == "numeric" || options.style == "2-digit")
                {
                    auto numeric = format_numeric_units(aDf, aDuration, u, signDisplayed);
                    if (!numeric.empty())
                        result.push_back(std::move(numeric));
                    break;
                }
                bool const fractional = next_unit_fractional(d, u);
                auto v = unit_value(d, aDuration, u, fractional, false);
                if (options.display == "always" || v != "0")
                {
                    auto const nfOptions = make_object(Null);
                    auto const put = [&](char const* aName, value aValue) { create_data_property_or_throw(nfOptions, ascii(aName), aValue); };
                    if (fractional)
                    {
                        put("maximumFractionDigits", number(d.fractionalDigits ? *d.fractionalDigits : 9));
                        put("minimumFractionDigits", number(d.fractionalDigits ? *d.fractionalDigits : 0));
                        put("roundingMode", ascii("trunc"));
                    }
                    put("numberingSystem", u16_value(from_ascii(d.numberingSystem)));
                    if (signDisplayed)
                    {
                        signDisplayed = false;
                        if (v == "0" && temporal::duration_sign(aDuration) == -1)
                            v = "-0";
                    }
                    else
                        put("signDisplay", ascii("never"));
                    put("style", ascii("unit"));
                    put("unit", ascii(sUnits[u].numberFormatUnit));
                    put("unitDisplay", u16_value(from_ascii(options.style)));
                    value const arguments[] = { u16_value(from_ascii(aDf.locale)), nfOptions };
                    auto const nf = construct(the_realm().intlNumberFormat, arguments);
                    result.push_back(with_unit(format_decimal_to_parts(nf, v), sUnits[u].numberFormatUnit));
                }
                if (fractional)
                    break;
            }
            return list_format_parts(aDf, result);
        }

        value duration_format_format(value aThis, arguments aArguments, value, function_object& aCallee)
        {
            auto& df = this_intl(aThis, object_class::IntlDurationFormat, aCallee.nativeId == 0u ? "Intl.DurationFormat.prototype.format" :
                "Intl.DurationFormat.prototype.formatToParts");
            auto const record = to_duration_record(argument(aArguments, 0u));
            auto const parts = partition_duration_format_pattern(df, record);
            if (aCallee.nativeId == 1u)
                return parts_array(parts);
            std::u16string text;
            for (auto const& p : parts)
                text += p.text;
            return u16_value(text);
        }

        value duration_format_resolved_options(value aThis, arguments, value, function_object&)
        {
            auto& df = this_intl(aThis, object_class::IntlDurationFormat, "Intl.DurationFormat.prototype.resolvedOptions");
            auto const& d = data_of<duration_format_data>(df);
            auto const o = make_object();
            auto const put = [&](std::string const& aName, std::string const& aValue)
            {
                create_data_property_or_throw(o, ascii(aName.c_str()), u16_value(from_ascii(aValue)));
            };
            put("locale", df.locale);
            put("numberingSystem", d.numberingSystem);
            put("style", d.style);
            for (std::size_t u = 0u; u < UnitCount; ++u)
            {
                put(sUnits[u].unit, d.units[u].style == "fractional" ? std::string{ "numeric" } : d.units[u].style);
                put(std::string{ sUnits[u].unit } + "Display", d.units[u].display);
            }
            if (d.fractionalDigits)
                create_data_property_or_throw(o, ascii("fractionalDigits"), number(*d.fractionalDigits));
            return o;
        }
    }

    value duration_to_locale_string(value aDuration, value aLocales, value aOptions)
    {
        // (Temporal.Duration.prototype.toLocaleString: an Intl.DurationFormat's format of the duration's fields)
        value const arguments[] = { aLocales, aOptions };
        auto const df = construct(the_realm().intlDurationFormat, arguments);
        auto const& t = static_cast<temporal::temporal_object const&>(obj(aDuration));
        std::u16string text;
        for (auto const& p : partition_duration_format_pattern(static_cast<intl_object&>(obj(df)), t.duration))
            text += p.text;
        return u16_value(text);
    }

    void init_intl_duration_format(realm& aRealm, value)
    {
        aRealm.intlDurationFormat = make_intl_constructor(aRealm, "DurationFormat", 0u, duration_format_constructor, &realm::intlDurationFormatPrototype,
            "Intl.DurationFormat");
        auto const p = aRealm.intlDurationFormatPrototype;
        as_function(method(p, "format", 1u, duration_format_format))->nativeId = 0u;
        as_function(method(p, "formatToParts", 1u, duration_format_format))->nativeId = 1u;
        method(p, "resolvedOptions", 0u, duration_format_resolved_options);
    }
}

#endif
