/*
  intl_date_time_format.cpp

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

// Intl.DateTimeFormat (ECMA-402 §11) with ICU's date formatting (a pattern generator's best pattern for the requested components (or a
// style's pattern), with the hour cycle applied; udat, udtitvfmt) and the Date's locale-sensitive methods (toLocaleString,
// toLocaleDateString, toLocaleTimeString). The proleptic Gregorian calendar is used throughout (as ECMAScript's dates are).

#include "intl.hpp"

#if defined(NEOS_JS_ICU)

#include <algorithm>
#include <cmath>
#include <cstring>
#include "gc.hpp"
#include "intl_icu.hpp"
#include "temporal.hpp"

namespace neos::javascript_runtime::intl
{
    namespace
    {
        // --- the components (Table 16)

        enum component : std::size_t { Weekday, Era, Year, Month, Day, DayPeriod, Hour, Minute, Second, FractionalSecondDigits, TimeZoneName,
            ComponentCount };

        constexpr char const* sComponentNames[ComponentCount] = { "weekday", "era", "year", "month", "day", "dayPeriod", "hour", "minute",
            "second", "fractionalSecondDigits", "timeZoneName" };

        struct format_options
        {
            std::string hourCycle;                              ///< ("" none)
            std::optional<std::string> components[ComponentCount];
            std::optional<std::string> dateStyle;
            std::optional<std::string> timeStyle;
        };

        // (what a format needs (CreateDateTimeFormat's required) and gets by default (its defaults))
        enum class required { Any, Date, Time, YearMonth, MonthDay };
        enum class defaults { All, Date, Time, YearMonth, MonthDay };

        // (the kinds of value formatted: the Date's ([[DateTimeFormat]]) and the Temporal objects' ([[TemporalPlainDateFormat]] ...))
        enum kind : std::size_t { KindDate, KindPlainDate, KindPlainYearMonth, KindPlainMonthDay, KindPlainTime, KindPlainDateTime, KindInstant,
            KindCount };

        struct kind_format
        {
            bool made = false;
            std::u16string pattern;         ///< (empty: none (the kind cannot be formatted: TypeError))
            date_format_ptr format;
            date_interval_format_ptr intervalFormat;
        };

        struct date_time_format_data : intl_data
        {
            std::string dataLocale;
            std::string calendar;
            std::string numberingSystem;
            std::string timeZone;                   ///< (as resolvedOptions gives it)
            std::u16string icuTimeZone;             ///< (ICU's identifier of it)
            std::string hourCycle;                  ///< (if the format has an hour)
            std::string hc;                         ///< (the hour cycle, whatever the format)
            format_options requested;               ///< (the options as given (the Temporal kinds' formats are of them))
            format_options resolved;                ///< (the Date format's components (from its pattern))
            std::string icuLocale;
            kind_format kinds[KindCount];
        };

        // (the ICU locale ID of the data locale with the calendar and numbering system)
        std::string icu_locale_with(std::string const& aDataLocale, std::string const& aCalendar, std::string const& aNumberingSystem)
        {
            auto icu = icu_locale(aDataLocale);
            auto const* calendar = uloc_toLegacyType("ca", aCalendar.c_str());
            icu += "@calendar=" + std::string{ calendar != nullptr ? calendar : aCalendar.c_str() };
            if (!aNumberingSystem.empty())
                icu += ";numbers=" + aNumberingSystem;
            return icu;
        }

        struct date_time_locale_data : locale_data
        {
            std::vector<std::string> values(std::string const& aDataLocale, std::string_view aKey) const override
            {
                if (aKey == "ca")
                {
                    std::vector<std::string> result;
                    auto const icu = icu_locale(aDataLocale);
                    for (bool commonly : { true, false })
                    {
                        UErrorCode status = U_ZERO_ERROR;
                        enumeration_ptr e{ ucal_getKeywordValuesForLocale("calendar", icu.c_str(), commonly, &status) };
                        std::int32_t length = 0;
                        while (U_SUCCESS(status))
                        {
                            auto const* v = uenum_next(e.get(), &length, &status);
                            if (v == nullptr)
                                break;
                            auto const* bcp = uloc_toUnicodeLocaleType("ca", v);
                            std::string c = bcp != nullptr ? bcp : v;
                            // (islamic and islamic-rgsa are not supported: they fall back)
                            if (c == "islamic" || c == "islamic-rgsa")
                                continue;
                            if (std::find(result.begin(), result.end(), c) == result.end())
                                result.push_back(c);
                        }
                    }
                    if (result.empty())
                        result.push_back("gregory");
                    return result;
                }
                if (aKey == "nu")
                    return { default_numbering_system(aDataLocale) };
                if (aKey == "hc")
                    return { "", "h11", "h12", "h23", "h24" };
                return {};
            }
            bool supports(std::string const& aDataLocale, std::string_view aKey, std::string const& aValue) const override
            {
                if (aKey == "nu")
                    return is_supported_numbering_system(aDataLocale, aValue);
                return locale_data::supports(aDataLocale, aKey, aValue);
            }
        };


        // --- patterns

        // (the hour cycle of an hour pattern character)
        char16_t hour_character(std::string const& aHourCycle)
        {
            if (aHourCycle == "h11")
                return u'K';
            if (aHourCycle == "h12")
                return u'h';
            if (aHourCycle == "h23")
                return u'H';
            if (aHourCycle == "h24")
                return u'k';
            return u'j';
        }

        std::string hour_cycle_of(char16_t aCharacter)
        {
            switch (aCharacter)
            {
            case u'K': return "h11";
            case u'h': return "h12";
            case u'H': return "h23";
            case u'k': return "h24";
            default: return {};
            }
        }

        // (a pattern with its hour fields of the hour cycle (and a 12-hour cycle's day period kept, a 24-hour cycle's removed))
        std::u16string apply_hour_cycle(std::u16string const& aPattern, std::string const& aHourCycle)
        {
            if (aHourCycle.empty())
                return aPattern;
            auto const h = hour_character(aHourCycle);
            bool const twelve = h == u'h' || h == u'K';
            std::u16string result;
            bool quoted = false;
            for (std::size_t i = 0u; i < aPattern.size(); ++i)
            {
                auto const c = aPattern[i];
                if (c == u'\'')
                {
                    quoted = !quoted;
                    result.push_back(c);
                    continue;
                }
                if (!quoted && (c == u'h' || c == u'H' || c == u'k' || c == u'K'))
                {
                    result.push_back(h);
                    continue;
                }
                if (!quoted && !twelve && (c == u'a' || c == u'b' || c == u'B'))
                {
                    // (a 24-hour cycle has no day period: it and the space before it go)
                    while (i + 1u < aPattern.size() && aPattern[i + 1u] == c)
                        ++i;
                    while (!result.empty() && (result.back() == u' ' || result.back() == 0x202Fu || result.back() == 0xA0u))
                        result.pop_back();
                    continue;
                }
                result.push_back(c);
            }
            // (a 12-hour cycle needs a day period)
            if (twelve && result.find_first_of(u"abB") == std::u16string::npos)
            {
                std::size_t hour = result.find(h);
                if (hour != std::u16string::npos)
                {
                    auto end = result.find_last_of(u"hKmsS");
                    result.insert(end + 1u, u" a");
                }
            }
            return result;
        }

        // (the components of a pattern (resolvedOptions'))
        void pattern_components(std::u16string const& aPattern, format_options& aOptions, std::string& aHourCycle)
        {
            bool quoted = false;
            for (std::size_t i = 0u; i < aPattern.size();)
            {
                auto const c = aPattern[i];
                if (c == u'\'')
                {
                    quoted = !quoted;
                    ++i;
                    continue;
                }
                std::size_t count = 1u;
                while (i + count < aPattern.size() && aPattern[i + count] == c)
                    ++count;
                i += count;
                if (quoted)
                    continue;
                auto const text = [&](char const* aShort, char const* aLong, char const* aNarrow) -> std::string
                {
                    return count <= 3u ? aShort : count == 4u ? aLong : aNarrow;
                };
                switch (c)
                {
                case u'G': aOptions.components[Era] = text("short", "long", "narrow"); break;
                case u'y': case u'Y': case u'u': case u'U': case u'r':
                    aOptions.components[Year] = count == 2u ? "2-digit" : "numeric";
                    break;
                case u'M': case u'L':
                    aOptions.components[Month] = count == 1u ? "numeric" : count == 2u ? "2-digit" : count == 3u ? "short" : count == 4u ? "long" : "narrow";
                    break;
                case u'E': case u'c': case u'e': aOptions.components[Weekday] = text("short", "long", "narrow"); break;
                case u'd': aOptions.components[Day] = count == 2u ? "2-digit" : "numeric"; break;
                case u'B': case u'b': aOptions.components[DayPeriod] = text("short", "long", "narrow"); break;
                case u'h': case u'H': case u'k': case u'K':
                    aOptions.components[Hour] = count == 2u ? "2-digit" : "numeric";
                    aHourCycle = hour_cycle_of(c);
                    break;
                case u'm': aOptions.components[Minute] = count == 2u ? "2-digit" : "numeric"; break;
                case u's': aOptions.components[Second] = count == 2u ? "2-digit" : "numeric"; break;
                case u'S': aOptions.components[FractionalSecondDigits] = std::to_string(std::min<std::size_t>(count, 3u)); break;
                case u'z': aOptions.components[TimeZoneName] = count < 4u ? "short" : "long"; break;
                case u'O': aOptions.components[TimeZoneName] = count < 4u ? "shortOffset" : "longOffset"; break;
                case u'v': aOptions.components[TimeZoneName] = count < 4u ? "shortGeneric" : "longGeneric"; break;
                case u'V': aOptions.components[TimeZoneName] = count < 4u ? "shortGeneric" : "longGeneric"; break;
                case u'Z': case u'X': case u'x': aOptions.components[TimeZoneName] = count < 4u ? "shortOffset" : "longOffset"; break;
                default: break;
                }
            }
        }

        // (a skeleton of requested components)
        std::u16string components_skeleton(format_options const& aOptions, std::string const& aHourCycle)
        {
            std::u16string s;
            auto const repeat = [&](char16_t c, std::size_t n) { s.append(n, c); };
            auto const& o = aOptions.components;
            if (o[Weekday])
                repeat(u'E', *o[Weekday] == "narrow" ? 5u : *o[Weekday] == "long" ? 4u : 3u);
            if (o[Era])
                repeat(u'G', *o[Era] == "narrow" ? 5u : *o[Era] == "long" ? 4u : 1u);
            if (o[Year])
                repeat(u'y', *o[Year] == "2-digit" ? 2u : 1u);
            if (o[Month])
            {
                auto const& m = *o[Month];
                repeat(u'M', m == "numeric" ? 1u : m == "2-digit" ? 2u : m == "short" ? 3u : m == "long" ? 4u : 5u);
            }
            if (o[Day])
                repeat(u'd', *o[Day] == "2-digit" ? 2u : 1u);
            if (o[DayPeriod])
                repeat(u'B', *o[DayPeriod] == "narrow" ? 5u : *o[DayPeriod] == "long" ? 4u : 1u);
            if (o[Hour])
                repeat(hour_character(aHourCycle), *o[Hour] == "2-digit" ? 2u : 1u);
            if (o[Minute])
                repeat(u'm', *o[Minute] == "2-digit" ? 2u : 1u);
            if (o[Second])
                repeat(u's', *o[Second] == "2-digit" ? 2u : 1u);
            if (o[FractionalSecondDigits])
                repeat(u'S', static_cast<std::size_t>(std::stoi(*o[FractionalSecondDigits])));
            if (o[TimeZoneName])
            {
                auto const& z = *o[TimeZoneName];
                if (z == "short")
                    repeat(u'z', 1u);
                else if (z == "long")
                    repeat(u'z', 4u);
                else if (z == "shortOffset")
                    repeat(u'O', 1u);
                else if (z == "longOffset")
                    repeat(u'O', 4u);
                else if (z == "shortGeneric")
                    repeat(u'v', 1u);
                else
                    repeat(u'v', 4u);
            }
            return s;
        }

        UDateFormatStyle style_of(std::optional<std::string> const& aStyle)
        {
            if (!aStyle)
                return UDAT_NONE;
            if (*aStyle == "full")
                return UDAT_FULL;
            if (*aStyle == "long")
                return UDAT_LONG;
            if (*aStyle == "medium")
                return UDAT_MEDIUM;
            return UDAT_SHORT;
        }

        // (the calendar for formatting: ECMAScript's proleptic Gregorian calendar)
        void set_proleptic(UDateFormat* aFormat)
        {
            auto const* calendar = udat_getCalendar(aFormat);
            UErrorCode status = U_ZERO_ERROR;
            calendar_ptr c{ ucal_clone(calendar, &status) };
            if (U_FAILURE(status))
                return;
            ucal_setGregorianChange(c.get(), -8.64e15, &status);
            udat_setCalendar(aFormat, c.get());
        }

        // (a pattern's ICU formatter (in a time zone))
        date_format_ptr make_format(date_time_format_data const& d, std::u16string const& aPattern, std::u16string const& aTimeZone)
        {
            UErrorCode status = U_ZERO_ERROR;
            date_format_ptr f{ udat_open(UDAT_PATTERN, UDAT_PATTERN, d.icuLocale.c_str(), uchars(aTimeZone), ulength(aTimeZone),
                uchars(aPattern), ulength(aPattern), &status) };
            if (U_FAILURE(status))
                throw_range_error("Intl.DateTimeFormat: the format cannot be made");
            set_proleptic(f.get());
            return f;
        }

        // (the pattern of a format's options (styles, or components (the best pattern for their skeleton)))
        std::u16string pattern_of(date_time_format_data const& d, format_options const& aOptions, std::string const& aHourCycle)
        {
            UErrorCode status = U_ZERO_ERROR;
            if (aOptions.dateStyle || aOptions.timeStyle)
            {
                date_format_ptr f{ udat_open(style_of(aOptions.timeStyle), style_of(aOptions.dateStyle), d.icuLocale.c_str(), uchars(d.icuTimeZone),
                    ulength(d.icuTimeZone), nullptr, 0, &status) };
                if (U_FAILURE(status))
                    return {};
                auto pattern = icu_string([&](UChar* b, std::int32_t c, UErrorCode* s) { return udat_toPattern(f.get(), false, b, c, s); });
                return aOptions.timeStyle ? apply_hour_cycle(pattern, aHourCycle) : pattern;
            }
            auto const skeleton = components_skeleton(aOptions, aHourCycle);
            if (skeleton.empty())
                return {};
            date_pattern_generator_ptr generator{ udatpg_open(d.icuLocale.c_str(), &status) };
            if (U_FAILURE(status))
                return {};
            auto pattern = icu_string([&](UChar* b, std::int32_t c, UErrorCode* s)
                { return udatpg_getBestPatternWithOptions(generator.get(), uchars(skeleton), ulength(skeleton), UDATPG_MATCH_HOUR_FIELD_LENGTH, b, c, s); });
            return aOptions.components[Hour] ? apply_hour_cycle(pattern, aHourCycle) : pattern;
        }

        // (GetDateTimeFormat: the components of a kind's format (none: the options are of other components only))
        std::optional<format_options> get_date_time_format(format_options const& aOptions, required aRequired, defaults aDefaults, bool aInheritAll)
        {
            static std::vector<component> const sRequired[] = {
                { Weekday, Year, Month, Day, DayPeriod, Hour, Minute, Second, FractionalSecondDigits },
                { Weekday, Year, Month, Day },
                { DayPeriod, Hour, Minute, Second, FractionalSecondDigits },
                { Year, Month },
                { Month, Day } };
            static std::vector<component> const sDefaults[] = {
                { Year, Month, Day, Hour, Minute, Second },
                { Year, Month, Day },
                { Hour, Minute, Second },
                { Year, Month },
                { Month, Day } };
            format_options result;
            if (aInheritAll)
                result = aOptions;
            else
            {
                result.hourCycle = aOptions.hourCycle;
                if (aRequired == required::Date || aRequired == required::YearMonth || aRequired == required::Any)
                    result.components[Era] = aOptions.components[Era];
            }
            bool anyPresent = false;
            for (auto c : sRequired[static_cast<std::size_t>(aRequired)])
                if (aOptions.components[c])
                {
                    result.components[c] = aOptions.components[c];
                    anyPresent = true;
                }
            if (!anyPresent)
            {
                if (!aInheritAll)
                    for (auto c : sRequired[0])
                        if (aOptions.components[c])
                            return std::nullopt;
                for (auto c : sDefaults[static_cast<std::size_t>(aDefaults)])
                    result.components[c] = "numeric";
            }
            return result;
        }

        // (AdjustDateTimeStyleFormat: a style's components restricted to some (empty: none of them))
        std::u16string adjust_style_pattern(date_time_format_data const& d, std::u16string const& aPattern, std::initializer_list<component> aAllowed)
        {
            format_options components;
            std::string hourCycle;
            pattern_components(aPattern, components, hourCycle);
            format_options adjusted;
            bool any = false;
            for (auto c : aAllowed)
                if (components.components[c])
                {
                    adjusted.components[c] = components.components[c];
                    any = true;
                }
            if (!any)
                return {};
            // (none to remove: the style's own pattern)
            bool all = true;
            for (std::size_t c = 0u; c < ComponentCount; ++c)
                if (components.components[c] && std::find(aAllowed.begin(), aAllowed.end(), static_cast<component>(c)) == aAllowed.end())
                    all = false;
            if (all)
                return aPattern;
            return pattern_of(d, adjusted, d.hc);
        }

        bool is_plain(kind aKind)
        {
            return aKind != KindDate && aKind != KindInstant;
        }

        // (a kind's format, made when first needed)
        kind_format& kind_format_of(date_time_format_data& d, kind aKind)
        {
            auto& k = d.kinds[aKind];
            if (k.made)
                return k;
            k.made = true;
            auto const& main = d.kinds[KindDate].pattern;
            auto const& o = d.requested;
            if (o.dateStyle || o.timeStyle)
            {
                switch (aKind)
                {
                case KindPlainDate:
                    if (o.dateStyle)
                        k.pattern = adjust_style_pattern(d, main, { Weekday, Era, Year, Month, Day });
                    break;
                case KindPlainYearMonth:
                    if (o.dateStyle)
                        k.pattern = adjust_style_pattern(d, main, { Era, Year, Month });
                    break;
                case KindPlainMonthDay:
                    if (o.dateStyle)
                        k.pattern = adjust_style_pattern(d, main, { Month, Day });
                    break;
                case KindPlainTime:
                    if (o.timeStyle)
                        k.pattern = adjust_style_pattern(d, main, { DayPeriod, Hour, Minute, Second, FractionalSecondDigits });
                    break;
                case KindPlainDateTime:
                    k.pattern = adjust_style_pattern(d, main, { Weekday, Era, Year, Month, Day, DayPeriod, Hour, Minute, Second, FractionalSecondDigits });
                    break;
                default:
                    k.pattern = main;
                    break;
                }
            }
            else
            {
                std::optional<format_options> f;
                switch (aKind)
                {
                case KindPlainDate: f = get_date_time_format(o, required::Date, defaults::Date, false); break;
                case KindPlainYearMonth: f = get_date_time_format(o, required::YearMonth, defaults::YearMonth, false); break;
                case KindPlainMonthDay: f = get_date_time_format(o, required::MonthDay, defaults::MonthDay, false); break;
                case KindPlainTime: f = get_date_time_format(o, required::Time, defaults::Time, false); break;
                case KindPlainDateTime: f = get_date_time_format(o, required::Any, defaults::All, false); break;
                default: f = get_date_time_format(o, required::Any, defaults::All, true); break;
                }
                if (f)
                    k.pattern = pattern_of(d, *f, d.hc);
            }
            if (!k.pattern.empty())
                k.format = make_format(d, k.pattern, is_plain(aKind) ? std::u16string{ u"UTC" } : d.icuTimeZone);
            return k;
        }

        // --- time zones

        std::string offset_identifier(int aOffsetMinutes)
        {
            auto const sign = aOffsetMinutes < 0 ? '-' : '+';
            auto const m = std::abs(aOffsetMinutes);
            char buffer[16];
            std::snprintf(buffer, sizeof buffer, "%c%02d:%02d", sign, m / 60, m % 60);
            return buffer;
        }

        // (a time zone: its identifier (case-normalized; an offset as +HH:MM) and ICU's)
        void set_time_zone(std::u16string_view aName, date_time_format_data& d)
        {
            std::u16string zone;
            int offsetMinutes = 0;
            bool isOffset = false;
            if (temporal::parse_time_zone_identifier(aName, zone, offsetMinutes, isOffset) && isOffset)
            {
                d.timeZone = offset_identifier(offsetMinutes);
                d.icuTimeZone = from_ascii("GMT" + d.timeZone);
                return;
            }
            auto const record = temporal::get_available_named_time_zone(aName);
            if (!record)
                throw_range_error("Invalid time zone specified: " + to_ascii(aName));
            d.timeZone = to_ascii(record->identifier);
            // (ICU's identifier: the identifier, or its primary's if ICU does not know it)
            auto const known = [](std::u16string const& aId)
            {
                UBool system = false;
                auto const canonical = icu_string([&](UChar* b, std::int32_t c, UErrorCode* s)
                    { return ucal_getCanonicalTimeZoneID(uchars(aId), ulength(aId), b, c, &system, s); });
                return !canonical.empty() && canonical != u"Etc/Unknown";
            };
            d.icuTimeZone = known(record->identifier) ? record->identifier : record->primary;
            if (d.icuTimeZone == u"UTC" || d.icuTimeZone == u"Etc/UTC" || d.icuTimeZone == u"Etc/GMT" || d.icuTimeZone == u"GMT")
                d.icuTimeZone = u"UTC";
        }

        // --- CreateDateTimeFormat

        value create_date_time_format(value aNewTarget, value aLocales, value aOptions, required aRequired, defaults aDefaults,
            std::optional<std::u16string> const& aToLocaleStringTimeZone = std::nullopt)
        {
            auto const result = make_intl_object(aNewTarget, &realm::intlDateTimeFormatPrototype, object_class::IntlDateTimeFormat);
            auto& dtf = static_cast<intl_object&>(obj(result));
            auto data = std::make_unique<date_time_format_data>();
            auto& d = *data;
            auto const requested = canonicalize_locale_list(aLocales);
            auto const options = coerce_options_to_object(aOptions);
            auto const matcher = *get_string_option(options, "localeMatcher", { "lookup", "best fit" }, "best fit");
            auto const calendar = get_string_option(options, "calendar", {});
            if (calendar && !is_type_sequence(*calendar))
                throw_range_error("Invalid calendar : " + to_ascii(*calendar));
            auto const numberingSystem = get_string_option(options, "numberingSystem", {});
            if (numberingSystem && !is_type_sequence(*numberingSystem))
                throw_range_error("Invalid numberingSystem : " + to_ascii(*numberingSystem));
            auto const hour12 = get_boolean_option(options, "hour12");
            auto hourCycle = get_string_option(options, "hourCycle", { "h11", "h12", "h23", "h24" });
            std::optional<std::string> hcOption;
            if (hourCycle && !hour12)
                hcOption = to_ascii(*hourCycle);
            std::vector<relevant_key> keys{ { "ca", calendar ? std::optional<std::string>{ to_ascii(*calendar) } : std::nullopt },
                { "hc", hcOption }, { "nu", numberingSystem ? std::optional<std::string>{ to_ascii(*numberingSystem) } : std::nullopt } };
            auto r = resolve_locale(service::DateTimeFormat, requested, matcher, keys, date_time_locale_data{});
            // (an hour12 option: the extension's hc is not the locale's)
            if (hour12)
            {
                if (auto parsed = parse_language_tag(r.locale))
                {
                    std::erase_if(parsed->keywords, [](auto const& k) { return k.first == "hc"; });
                    parsed->unicodeExtension = !parsed->keywords.empty() || !parsed->attributes.empty();
                    r.locale = parsed->to_string();
                }
            }
            dtf.locale = r.locale;
            d.dataLocale = r.dataLocale;
            d.calendar = r.keywords.at("ca");
            d.numberingSystem = r.keywords.at("nu");
            d.icuLocale = icu_locale_with(d.dataLocale, d.calendar, d.numberingSystem);
            // (the hour cycle: hour12's (the locale's 12- or 24-hour cycle), else the extension's or option's, else the locale's)
            std::string hc;
            {
                UErrorCode status = U_ZERO_ERROR;
                date_pattern_generator_ptr generator{ udatpg_open(d.icuLocale.c_str(), &status) };
                auto const twelve = [&]()
                {
                    auto const p = icu_string([&](UChar* b, std::int32_t c, UErrorCode* s)
                        { return udatpg_getBestPattern(generator.get(), uchars(u"h"), 1, b, c, s); });
                    return p.find(u'K') != std::u16string::npos ? std::string{ "h11" } : std::string{ "h12" };
                };
                if (hour12)
                    hc = *hour12 ? twelve() : "h23";
                else
                {
                    hc = r.keywords.at("hc");
                    if (hc.empty())
                    {
                        switch (udatpg_getDefaultHourCycle(generator.get(), &status))
                        {
                        case UDAT_HOUR_CYCLE_11: hc = "h11"; break;
                        case UDAT_HOUR_CYCLE_12: hc = "h12"; break;
                        case UDAT_HOUR_CYCLE_24: hc = "h24"; break;
                        default: hc = "h23"; break;
                        }
                    }
                }
            }
            d.hc = hc;
            {
                auto const v = get(options, ascii("timeZone"));
                if (aToLocaleStringTimeZone)
                {
                    if (!is_undefined(v))
                        throw_type_error("ZonedDateTime.prototype.toLocaleString: the timeZone option is not allowed");
                    set_time_zone(*aToLocaleStringTimeZone, d);
                }
                else if (is_undefined(v))
                    set_time_zone(str(temporal::system_time_zone().id), d);
                else
                    set_time_zone(str(to_string(v)), d);
            }
            // (the components)
            format_options& f = d.requested;
            f.hourCycle = hc;
            bool hasExplicitFormatComponents = false;
            for (std::size_t i = 0u; i < ComponentCount; ++i)
            {
                std::optional<std::string> v;
                switch (i)
                {
                case Weekday: case Era: case DayPeriod:
                    if (auto const s = get_string_option(options, sComponentNames[i], { "narrow", "short", "long" }))
                        v = to_ascii(*s);
                    break;
                case Year: case Day: case Hour: case Minute: case Second:
                    if (auto const s = get_string_option(options, sComponentNames[i], { "2-digit", "numeric" }))
                        v = to_ascii(*s);
                    break;
                case Month:
                    if (auto const s = get_string_option(options, sComponentNames[i], { "2-digit", "numeric", "narrow", "short", "long" }))
                        v = to_ascii(*s);
                    break;
                case FractionalSecondDigits:
                    if (auto const n = get_number_option(options, "fractionalSecondDigits", 1, 3, std::nullopt))
                        v = std::to_string(*n);
                    break;
                default:
                    if (auto const s = get_string_option(options, sComponentNames[i], { "short", "long", "shortOffset", "longOffset", "shortGeneric",
                        "longGeneric" }))
                        v = to_ascii(*s);
                    break;
                }
                f.components[i] = v;
                if (v)
                    hasExplicitFormatComponents = true;
            }
            get_string_option(options, "formatMatcher", { "basic", "best fit" }, "best fit");
            if (auto const s = get_string_option(options, "dateStyle", { "full", "long", "medium", "short" }))
                f.dateStyle = to_ascii(*s);
            if (auto const s = get_string_option(options, "timeStyle", { "full", "long", "medium", "short" }))
                f.timeStyle = to_ascii(*s);
            format_options formatOptions;
            if (f.dateStyle || f.timeStyle)
            {
                if (hasExplicitFormatComponents)
                    throw_type_error("Can't set option " + std::string{ f.dateStyle ? "dateStyle" : "timeStyle" } + " when other date or time options are set");
                if (aRequired == required::Date && f.timeStyle)
                    throw_type_error("Invalid option : timeStyle");
                if (aRequired == required::Time && f.dateStyle)
                    throw_type_error("Invalid option : dateStyle");
                formatOptions = f;
            }
            else
            {
                formatOptions = *get_date_time_format(f, aRequired, aDefaults, true);
                // (a ZonedDateTime's toLocaleString: its time zone's name too by default)
                if (aToLocaleStringTimeZone && !f.components[TimeZoneName] &&
                    std::none_of(std::begin(f.components), std::end(f.components), [](auto const& c) { return c.has_value(); }))
                    formatOptions.components[TimeZoneName] = "short";
            }
            auto& main = d.kinds[KindDate];
            main.made = true;
            main.pattern = pattern_of(d, formatOptions, hc);
            if (main.pattern.empty())
                throw_range_error("Intl.DateTimeFormat: the format cannot be made");
            main.format = make_format(d, main.pattern, d.icuTimeZone);
            // (the resolved components: the pattern's)
            d.resolved.dateStyle = formatOptions.dateStyle;
            d.resolved.timeStyle = formatOptions.timeStyle;
            std::string patternHourCycle;
            if (!formatOptions.dateStyle && !formatOptions.timeStyle)
                pattern_components(main.pattern, d.resolved, patternHourCycle);
            else
            {
                format_options styleComponents;
                pattern_components(main.pattern, styleComponents, patternHourCycle);
                if (styleComponents.components[Hour])
                    d.resolved.components[Hour] = styleComponents.components[Hour];
            }
            if (d.resolved.components[Hour])
                d.hourCycle = hc;
            dtf.data = std::move(data);
            return result;
        }

        value date_time_format_constructor(value aThis, arguments aArguments, value aNewTarget, function_object& aCallee)
        {
            auto const newTarget = is_undefined(aNewTarget) ? value_of(aCallee) : aNewTarget;
            auto const result = create_date_time_format(newTarget, argument(aArguments, 0u), argument(aArguments, 1u), required::Any, defaults::Date);
            auto const& r = the_realm();
            if (is_undefined(aNewTarget) && is_object(aThis) && ordinary_has_instance(r.intlDateTimeFormat, aThis))
            {
                define_property_or_throw(aThis, r.intlFallbackSymbol, property_descriptor::data(result, 0u));
                return aThis;
            }
            return result;
        }

        intl_object& unwrap_date_time_format(value aThis, char const* aMethod)
        {
            auto const& r = the_realm();
            if (!is_object(aThis))
                throw_type_error(std::string{ "Method " } + aMethod + " called on incompatible receiver");
            auto value = aThis;
            if (as_intl(aThis, object_class::IntlDateTimeFormat) == nullptr && ordinary_has_instance(r.intlDateTimeFormat, aThis))
                value = get(aThis, r.intlFallbackSymbol);
            return this_intl(value, object_class::IntlDateTimeFormat, aMethod);
        }

        // --- the values formatted (ToDateTimeFormattable, HandleDateTimeValue)

        bool is_temporal_object(value aValue)
        {
            if (!is_object(aValue))
                return false;
            auto const c = obj(aValue).cls;
            return c >= object_class::TemporalDuration && c <= object_class::TemporalZonedDateTime;
        }

        // (ToDateTimeFormattable: a Temporal object, or a number)
        value to_date_time_formattable(value aValue)
        {
            return is_temporal_object(aValue) ? aValue : number(to_number(aValue));
        }

        // (a Temporal object's calendar)
        std::string temporal_calendar(temporal::temporal_object const& aObject)
        {
            return temporal::calendar_identifier(aObject.calendar);
        }

        double wall_time_ms(temporal::iso_date const& aDate, temporal::time_record const& aTime)
        {
            auto const days = temporal::iso_date_to_epoch_days(aDate.year, aDate.month - 1, aDate.day);
            return static_cast<double>(days) * 86400000.0 + aTime.hour * 3600000.0 + aTime.minute * 60000.0 + aTime.second * 1000.0 +
                aTime.millisecond;
        }

        struct formattable
        {
            kind k;
            double ms;
        };

        // (HandleDateTimeValue: the kind and time of a formattable value (a number: TimeClip))
        formattable handle_date_time_value(date_time_format_data& d, value aValue)
        {
            if (!is_temporal_object(aValue))
            {
                auto const x = to_number(aValue);
                if (!std::isfinite(x) || std::abs(x) > 8.64e15)
                    throw_range_error("Invalid time value");
                return { KindDate, std::trunc(x) + 0.0 };
            }
            auto const& t = static_cast<temporal::temporal_object const&>(obj(aValue));
            auto const checkCalendar = [&](bool aIsoAllowed)
            {
                auto const c = temporal_calendar(t);
                if (c != d.calendar && !(aIsoAllowed && c == "iso8601"))
                    throw_range_error("Intl.DateTimeFormat: the calendar " + c + " is not the format's (" + d.calendar + ")");
            };
            switch (obj(aValue).cls)
            {
            case object_class::TemporalPlainDate:
                checkCalendar(true);
                return { KindPlainDate, wall_time_ms(t.date, { 0, 12 }) };
            case object_class::TemporalPlainYearMonth:
                checkCalendar(false);
                return { KindPlainYearMonth, wall_time_ms(t.date, { 0, 12 }) };
            case object_class::TemporalPlainMonthDay:
                checkCalendar(false);
                return { KindPlainMonthDay, wall_time_ms(t.date, { 0, 12 }) };
            case object_class::TemporalPlainTime:
                return { KindPlainTime, wall_time_ms({}, t.time) };
            case object_class::TemporalPlainDateTime:
                checkCalendar(true);
                return { KindPlainDateTime, wall_time_ms(t.date, t.time) };
            case object_class::TemporalInstant:
                return { KindInstant, temporal::floor_div(t.epochNs, temporal::i128{ 1000000 }).to_double() };
            case object_class::TemporalZonedDateTime:
                throw_type_error("Intl.DateTimeFormat: a ZonedDateTime cannot be formatted (use its toLocaleString)");
            default:
                throw_type_error("Intl.DateTimeFormat: a Temporal.Duration cannot be formatted (use Intl.DurationFormat)");
            }
        }

        UDateFormat* format_for(date_time_format_data& d, kind aKind)
        {
            auto& k = kind_format_of(d, aKind);
            if (!k.format)
                throw_type_error("Intl.DateTimeFormat: the format has none of the value's fields");
            return k.format.get();
        }

        double now_ms()
        {
            return std::floor(static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()));
        }

        // --- formatting

        // (CLDR's narrow no-break spaces (e.g. before a day period) are spaces (as other implementations have them, for compatibility))
        std::u16string web_compatible(std::u16string aText)
        {
            std::replace(aText.begin(), aText.end(), char16_t{ 0x202F }, u' ');
            return aText;
        }

        std::u16string format_date_time(date_time_format_data& d, formattable const& x)
        {
            auto* f = format_for(d, x.k);
            return web_compatible(icu_string([&](UChar* b, std::int32_t c, UErrorCode* s) { return udat_format(f, x.ms, b, c, nullptr, s); }));
        }

        std::u16string date_field_type(std::int32_t aField)
        {
            // (UDAT_RELATED_YEAR_FIELD: ICU's internal API, which the Windows SDK's icu.h does not have)
            constexpr std::int32_t relatedYearField = 34;
            switch (aField)
            {
            case UDAT_ERA_FIELD: return u"era";
            case UDAT_YEAR_FIELD: case UDAT_EXTENDED_YEAR_FIELD: case UDAT_YEAR_WOY_FIELD: return u"year";
            case relatedYearField: return u"relatedYear";
            case UDAT_YEAR_NAME_FIELD: return u"yearName";
            case UDAT_MONTH_FIELD: case UDAT_STANDALONE_MONTH_FIELD: return u"month";
            case UDAT_DATE_FIELD: return u"day";
            case UDAT_HOUR_OF_DAY1_FIELD: case UDAT_HOUR_OF_DAY0_FIELD: case UDAT_HOUR1_FIELD: case UDAT_HOUR0_FIELD: return u"hour";
            case UDAT_MINUTE_FIELD: return u"minute";
            case UDAT_SECOND_FIELD: return u"second";
            case UDAT_FRACTIONAL_SECOND_FIELD: return u"fractionalSecond";
            case UDAT_DAY_OF_WEEK_FIELD: case UDAT_DOW_LOCAL_FIELD: case UDAT_STANDALONE_DAY_FIELD: return u"weekday";
            case UDAT_AM_PM_FIELD: case UDAT_AM_PM_MIDNIGHT_NOON_FIELD: case UDAT_FLEXIBLE_DAY_PERIOD_FIELD: return u"dayPeriod";
            case UDAT_TIMEZONE_FIELD: case UDAT_TIMEZONE_RFC_FIELD: case UDAT_TIMEZONE_GENERIC_FIELD: case UDAT_TIMEZONE_SPECIAL_FIELD:
            case UDAT_TIMEZONE_LOCALIZED_GMT_OFFSET_FIELD: case UDAT_TIMEZONE_ISO_FIELD: case UDAT_TIMEZONE_ISO_LOCAL_FIELD:
                return u"timeZoneName";
            default: return u"unknown";
            }
        }

        std::optional<std::u16string> date_part_type(field_span const& aSpan, std::u16string_view, void*)
        {
            if (aSpan.category != UFIELD_CATEGORY_DATE)
                return std::nullopt;
            return date_field_type(aSpan.field);
        }

        std::vector<part> format_date_time_to_parts(date_time_format_data& d, formattable const& x)
        {
            auto* f = format_for(d, x.k);
            UErrorCode status = U_ZERO_ERROR;
            field_position_iterator_ptr positions{ ufieldpositer_open(&status) };
            std::u16string text(64u, u'\0');
            auto length = udat_formatForFields(f, x.ms, reinterpret_cast<UChar*>(text.data()), static_cast<std::int32_t>(text.size()), positions.get(), &status);
            if (status == U_BUFFER_OVERFLOW_ERROR)
            {
                status = U_ZERO_ERROR;
                positions.reset(ufieldpositer_open(&status));
                text.assign(static_cast<std::size_t>(length) + 1u, u'\0');
                length = udat_formatForFields(f, x.ms, reinterpret_cast<UChar*>(text.data()), static_cast<std::int32_t>(text.size()), positions.get(), &status);
            }
            text.resize(static_cast<std::size_t>(length));
            text = web_compatible(std::move(text));
            std::vector<field_span> spans;
            for (;;)
            {
                std::int32_t begin = 0;
                std::int32_t end = 0;
                auto const field = ufieldpositer_next(positions.get(), &begin, &end);
                if (field < 0)
                    break;
                spans.push_back({ UFIELD_CATEGORY_DATE, field, begin, end });
            }
            return make_parts(text, spans, date_part_type, nullptr);
        }

        // --- ranges

        UDateIntervalFormat* interval_format(date_time_format_data& d, kind aKind)
        {
            auto& k = kind_format_of(d, aKind);
            if (!k.format)
                throw_type_error("Intl.DateTimeFormat: the format has none of the value's fields");
            if (!k.intervalFormat)
            {
                UErrorCode status = U_ZERO_ERROR;
                date_pattern_generator_ptr generator{ udatpg_open(d.icuLocale.c_str(), &status) };
                auto const skeleton = icu_string([&](UChar* b, std::int32_t c, UErrorCode* s)
                    { return udatpg_getSkeleton(generator.get(), uchars(k.pattern), ulength(k.pattern), b, c, s); });
                std::u16string const zone = is_plain(aKind) ? std::u16string{ u"UTC" } : d.icuTimeZone;
                status = U_ZERO_ERROR;
                k.intervalFormat.reset(udtitvfmt_open(d.icuLocale.c_str(), uchars(skeleton), ulength(skeleton), uchars(zone), ulength(zone), &status));
                if (U_FAILURE(status))
                    throw_range_error("Intl.DateTimeFormat: the range format cannot be made");
            }
            return k.intervalFormat.get();
        }

#if !defined(NEOS_JS_WINDOWS_ICU)
        calendar_ptr proleptic_calendar(date_time_format_data const& d, kind aKind, double x)
        {
            std::u16string const zone = is_plain(aKind) ? std::u16string{ u"UTC" } : d.icuTimeZone;
            UErrorCode status = U_ZERO_ERROR;
            calendar_ptr c{ ucal_open(uchars(zone), ulength(zone), d.icuLocale.c_str(), UCAL_DEFAULT, &status) };
            ucal_setGregorianChange(c.get(), -8.64e15, &status);
            ucal_setMillis(c.get(), x, &status);
            return c;
        }
#endif

        // (a range's parts (its ends practically equal: the format's parts, shared))
        std::vector<part> format_range_parts(date_time_format_data& d, formattable const& x, formattable const& y)
        {
            auto* f = interval_format(d, x.k);
            UErrorCode status = U_ZERO_ERROR;
            formatted_date_interval_ptr result{ udtitvfmt_openResult(&status) };
#if defined(NEOS_JS_WINDOWS_ICU)
            // (Windows' ICU does not export udtitvfmt_formatCalendarToResult: the format's own calendar (Julian before 1582))
            udtitvfmt_formatToResult(f, x.ms, y.ms, result.get(), &status);
#else
            auto const from = proleptic_calendar(d, x.k, x.ms);
            auto const to = proleptic_calendar(d, y.k, y.ms);
            udtitvfmt_formatCalendarToResult(f, from.get(), to.get(), result.get(), &status);
#endif
            if (U_FAILURE(status))
                throw_range_error("Intl.DateTimeFormat: the range cannot be formatted");
            auto const* v = udtitvfmt_resultAsValue(result.get(), &status);
            auto const text = web_compatible(formatted_value_string(v));
            auto const spans = formatted_value_spans(v);
            bool const ranged = std::any_of(spans.begin(), spans.end(), [](field_span const& s) { return s.category == UFIELD_CATEGORY_DATE_INTERVAL_SPAN; });
            if (!ranged)
            {
                auto parts = format_date_time_to_parts(d, x);
                // (practically equal (unless ICU has not seen the ends' difference (e.g. of related years): then the fallback "x – y"))
                if (format_date_time(d, x) == format_date_time(d, y))
                {
                    for (auto& p : parts)
                        p.source = u"shared";
                    return parts;
                }
                for (auto& p : parts)
                    p.source = u"startRange";
                parts.push_back({ u"literal", u" \u2013 ", u"shared", std::nullopt });
                for (auto& p : format_date_time_to_parts(d, y))
                {
                    p.source = u"endRange";
                    parts.push_back(std::move(p));
                }
                return parts;
            }
            auto parts = make_parts(text, spans, date_part_type, nullptr);
            std::int32_t offset = 0;
            for (auto& p : parts)
            {
                std::u16string source = u"shared";
                for (auto const& s : spans)
                    if (s.category == UFIELD_CATEGORY_DATE_INTERVAL_SPAN && offset >= s.begin && offset < s.end)
                        source = s.field == 0 ? u"startRange" : u"endRange";
                p.source = source;
                offset += static_cast<std::int32_t>(p.text.size());
            }
            return parts;
        }

        // --- the prototype's functions

        formattable formattable_or_now(date_time_format_data& d, value aDate)
        {
            if (is_undefined(aDate))
                return { KindDate, now_ms() };
            return handle_date_time_value(d, to_date_time_formattable(aDate));
        }

        value date_time_format_function(value, arguments aArguments, value, function_object& aCallee)
        {
            auto& d = data_of<date_time_format_data>(static_cast<intl_object&>(obj(aCallee.slots[0])));
            return u16_value(format_date_time(d, formattable_or_now(d, argument(aArguments, 0u))));
        }

        value date_time_format_format_getter(value aThis, arguments, value, function_object&)
        {
            auto& dtf = unwrap_date_time_format(aThis, "get Intl.DateTimeFormat.prototype.format");
            if (is_undefined(dtf.bound))
            {
                auto const f = make_native(ascii(""), 1u, date_time_format_function);
                as_function(f)->slots[0] = value_of(dtf);
                dtf.bound = f;
            }
            return dtf.bound;
        }

        value date_time_format_format_to_parts(value aThis, arguments aArguments, value, function_object&)
        {
            auto& dtf = this_intl(aThis, object_class::IntlDateTimeFormat, "Intl.DateTimeFormat.prototype.formatToParts");
            auto& d = data_of<date_time_format_data>(dtf);
            return parts_array(format_date_time_to_parts(d, formattable_or_now(d, argument(aArguments, 0u))));
        }

        value date_time_format_format_range(value aThis, arguments aArguments, value, function_object& aCallee)
        {
            auto& dtf = this_intl(aThis, object_class::IntlDateTimeFormat, aCallee.nativeId == 0u ? "Intl.DateTimeFormat.prototype.formatRange" :
                "Intl.DateTimeFormat.prototype.formatRangeToParts");
            auto& d = data_of<date_time_format_data>(dtf);
            auto const start = argument(aArguments, 0u);
            auto const end = argument(aArguments, 1u);
            if (is_undefined(start) || is_undefined(end))
                throw_type_error("startDate and endDate must be defined");
            auto const x = to_date_time_formattable(start);
            auto const y = to_date_time_formattable(end);
            if (is_temporal_object(x) != is_temporal_object(y) || (is_temporal_object(x) && obj(x).cls != obj(y).cls))
                throw_type_error("Intl.DateTimeFormat: a range's ends must be of the same kind");
            auto const fx = handle_date_time_value(d, x);
            auto const fy = handle_date_time_value(d, y);
            auto parts = format_range_parts(d, fx, fy);
            if (aCallee.nativeId == 1u)
                return parts_array(parts);
            std::u16string text;
            for (auto const& p : parts)
                text += p.text;
            return u16_value(text);
        }

        value date_time_format_resolved_options(value aThis, arguments, value, function_object&)
        {
            auto& dtf = unwrap_date_time_format(aThis, "Intl.DateTimeFormat.prototype.resolvedOptions");
            auto const& d = data_of<date_time_format_data>(dtf);
            auto const o = make_object();
            auto const put = [&](char const* aName, std::string const& aValue)
            {
                create_data_property_or_throw(o, ascii(aName), u16_value(from_ascii(aValue)));
            };
            put("locale", dtf.locale);
            put("calendar", d.calendar);
            put("numberingSystem", d.numberingSystem);
            put("timeZone", d.timeZone);
            if (!d.hourCycle.empty())
            {
                put("hourCycle", d.hourCycle);
                create_data_property_or_throw(o, ascii("hour12"), boolean(d.hourCycle == "h11" || d.hourCycle == "h12"));
            }
            if (!d.resolved.dateStyle && !d.resolved.timeStyle)
                for (std::size_t i = 0u; i < ComponentCount; ++i)
                    if (d.resolved.components[i])
                    {
                        if (i == FractionalSecondDigits)
                            create_data_property_or_throw(o, ascii(sComponentNames[i]), number(std::stoi(*d.resolved.components[i])));
                        else
                            put(sComponentNames[i], *d.resolved.components[i]);
                    }
            if (d.resolved.dateStyle)
                put("dateStyle", *d.resolved.dateStyle);
            if (d.resolved.timeStyle)
                put("timeStyle", *d.resolved.timeStyle);
            return o;
        }

        // (Date.prototype.toLocale*String's formats of the default locale and options (made once per realm and kind): roots)
        struct default_formats
        {
            value formats[3] = { Undefined, Undefined, Undefined };
            realm const* formatsRealm = nullptr;
        };
        isolate_local<default_formats> sDefaults;
        root_tracer const sDefaultFormatsRoot{ [](tracer& aTracer) { for (auto v : sDefaults->formats) aTracer.mark(v); } };
    }

    value date_to_locale_string(double aTime, value aLocales, value aOptions, int aRequired, int aDefaults)
    {
        // (Date.prototype.toLocaleString (any, all), toLocaleDateString (date, date) and toLocaleTimeString (time, time))
        if (std::isnan(aTime))
            return ascii("Invalid Date");
        auto const& r = the_realm();
        value dtf;
        if (is_undefined(aLocales) && is_undefined(aOptions))
        {
            auto& d = *sDefaults;
            if (d.formatsRealm != &r)
            {
                for (auto& f : d.formats)
                    f = Undefined;
                d.formatsRealm = &r;
            }
            auto& cached = d.formats[aRequired];
            if (is_undefined(cached))
                cached = create_date_time_format(r.intlDateTimeFormat, Undefined, Undefined, static_cast<required>(aRequired), static_cast<defaults>(aDefaults));
            dtf = cached;
        }
        else
            dtf = create_date_time_format(r.intlDateTimeFormat, aLocales, aOptions, static_cast<required>(aRequired), static_cast<defaults>(aDefaults));
        return u16_value(format_date_time(data_of<date_time_format_data>(static_cast<intl_object&>(obj(dtf))), { KindDate, aTime }));
    }

    value temporal_to_locale_string(value aTemporal, value aLocales, value aOptions)
    {
        // (the Temporal objects' toLocaleString: a format of the object's kind (a ZonedDateTime's in its time zone))
        auto const& r = the_realm();
        auto const& t = static_cast<temporal::temporal_object const&>(obj(aTemporal));
        required req = required::Any;
        defaults def = defaults::All;
        switch (obj(aTemporal).cls)
        {
        case object_class::TemporalPlainDate: case object_class::TemporalPlainYearMonth: case object_class::TemporalPlainMonthDay:
            req = required::Date;
            def = defaults::Date;
            break;
        case object_class::TemporalPlainTime:
            req = required::Time;
            def = defaults::Time;
            break;
        default:
            break;
        }
        if (obj(aTemporal).cls == object_class::TemporalZonedDateTime)
        {
            auto const dtf = create_date_time_format(r.intlDateTimeFormat, aLocales, aOptions, req, def, std::u16string{ str(t.timeZone.id) });
            auto& d = data_of<date_time_format_data>(static_cast<intl_object&>(obj(dtf)));
            auto const c = temporal_calendar(t);
            if (c != d.calendar && c != "iso8601")
                throw_range_error("Intl.DateTimeFormat: the calendar " + c + " is not the format's (" + d.calendar + ")");
            return u16_value(format_date_time(d, { KindDate, temporal::floor_div(t.epochNs, temporal::i128{ 1000000 }).to_double() }));
        }
        auto const dtf = create_date_time_format(r.intlDateTimeFormat, aLocales, aOptions, req, def);
        auto& d = data_of<date_time_format_data>(static_cast<intl_object&>(obj(dtf)));
        return u16_value(format_date_time(d, handle_date_time_value(d, aTemporal)));
    }

    void init_intl_date_time_format(realm& aRealm, value)
    {
        if (is_undefined(aRealm.intlFallbackSymbol))
            aRealm.intlFallbackSymbol = make_symbol(ascii("IntlLegacyConstructedSymbol"));
        auto const c = make_intl_constructor(aRealm, "DateTimeFormat", 0u, date_time_format_constructor, &realm::intlDateTimeFormatPrototype,
            "Intl.DateTimeFormat");
        aRealm.intlDateTimeFormat = c;
        auto const p = aRealm.intlDateTimeFormatPrototype;
        getter(p, ascii("format"), date_time_format_format_getter);
        method(p, "formatToParts", 1u, date_time_format_format_to_parts);
        as_function(method(p, "formatRange", 2u, date_time_format_format_range))->nativeId = 0u;
        as_function(method(p, "formatRangeToParts", 2u, date_time_format_format_range))->nativeId = 1u;
        method(p, "resolvedOptions", 0u, date_time_format_resolved_options);
    }
}

#endif
