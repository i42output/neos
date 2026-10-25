/*
  intl_display_names.cpp

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

// Intl.DisplayNames (ECMA-402 §12) with ICU's locale display names (uldn), currency names (ucurr) and date-time field names (udatpg).

#include "intl.hpp"

#if defined(NEOS_JS_ICU)

#include <algorithm>
#include "intl_icu.hpp"

namespace neos::javascript_runtime::intl
{
    namespace
    {
        struct display_names_data : intl_data
        {
            std::string dataLocale;
            std::string style;
            std::string type;
            std::string fallback;
            std::string languageDisplay;
            display_names_ptr names;
        };

        struct no_keys_locale_data : locale_data
        {
            std::vector<std::string> values(std::string const&, std::string_view) const override { return {}; }
        };

        value display_names_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (is_undefined(aNewTarget))
                throw_type_error("Constructor Intl.DisplayNames requires 'new'");
            auto const result = make_intl_object(aNewTarget, &realm::intlDisplayNamesPrototype, object_class::IntlDisplayNames);
            auto& dn = static_cast<intl_object&>(obj(result));
            auto data = std::make_unique<display_names_data>();
            auto& d = *data;
            auto const requested = canonicalize_locale_list(argument(aArguments, 0u));
            auto const options = get_options_object(argument(aArguments, 1u));
            auto const matcher = *get_string_option(options, "localeMatcher", { "lookup", "best fit" }, "best fit");
            auto const r = resolve_locale(service::DisplayNames, requested, matcher, {}, no_keys_locale_data{});
            d.style = to_ascii(*get_string_option(options, "style", { "narrow", "short", "long" }, "long"));
            auto const type = get_string_option(options, "type", { "language", "region", "script", "currency", "calendar", "dateTimeField" });
            if (!type)
                throw_type_error("Intl.DisplayNames: the type option is required");
            d.type = to_ascii(*type);
            d.fallback = to_ascii(*get_string_option(options, "fallback", { "code", "none" }, "code"));
            dn.locale = r.locale;
            d.dataLocale = r.dataLocale;
            auto const languageDisplay = to_ascii(*get_string_option(options, "languageDisplay", { "dialect", "standard" }, "dialect"));
            if (d.type == "language")
                d.languageDisplay = languageDisplay;
            UDisplayContext contexts[] = { d.languageDisplay == "standard" ? UDISPCTX_STANDARD_NAMES : UDISPCTX_DIALECT_NAMES,
                d.style == "long" ? UDISPCTX_LENGTH_FULL : UDISPCTX_LENGTH_SHORT, UDISPCTX_NO_SUBSTITUTE };
            UErrorCode status = U_ZERO_ERROR;
            auto const icu = icu_locale(d.dataLocale);
            d.names.reset(uldn_openForContext(icu.c_str(), contexts, 3, &status));
            if (U_FAILURE(status))
                throw_range_error("Intl.DisplayNames: the display names cannot be made");
            dn.data = std::move(data);
            return result;
        }

        bool is_alpha(std::string_view aText)
        {
            return std::all_of(aText.begin(), aText.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); });
        }

        bool is_digit(std::string_view aText)
        {
            return std::all_of(aText.begin(), aText.end(), [](char c) { return c >= '0' && c <= '9'; });
        }

        std::string lower(std::string aText)
        {
            for (auto& c : aText)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c - 'A' + 'a');
            return aText;
        }

        std::string upper(std::string aText)
        {
            for (auto& c : aText)
                if (c >= 'a' && c <= 'z')
                    c = static_cast<char>(c - 'a' + 'A');
            return aText;
        }

        // (CanonicalCodeForDisplayNames: RangeError if the code is not of the type's syntax)
        std::string canonical_code(std::string const& aType, std::u16string_view aCode)
        {
            auto const code = to_ascii(aCode);
            bool const ascii = std::all_of(aCode.begin(), aCode.end(), [](char16_t c) { return c < 0x80u; });
            auto const invalid = [&]() -> std::string { throw_range_error("invalid_argument: " + code + " is not a valid " + aType + " code"); };
            if (!ascii)
                invalid();
            if (aType == "language")
            {
                auto const parsed = parse_language_tag(code);
                if (!parsed || parsed->unicodeExtension || parsed->transformedExtension || !parsed->otherExtensions.empty() ||
                    !parsed->privateUse.empty() || lower(code) == "root" || code.find('_') != std::string::npos)
                    invalid();
                // (a bare script, or a script first, is CLDR's syntax, not BCP 47's)
                auto const first = code.substr(0u, code.find('-'));
                if (first.size() == 4u && is_alpha(first))
                    invalid();
                return canonicalize_language_tag(code);
            }
            if (aType == "region")
            {
                if (!((code.size() == 2u && is_alpha(code)) || (code.size() == 3u && is_digit(code))))
                    invalid();
                return upper(code);
            }
            if (aType == "script")
            {
                if (code.size() != 4u || !is_alpha(code))
                    invalid();
                auto result = lower(code);
                result[0] = static_cast<char>(result[0] - 'a' + 'A');
                return result;
            }
            if (aType == "calendar")
            {
                // (a type: (3*8alphanum) *("-" (3*8alphanum)))
                std::size_t length = 0u;
                for (auto c : code)
                {
                    if (c == '-')
                    {
                        if (length < 3u || length > 8u)
                            invalid();
                        length = 0u;
                        continue;
                    }
                    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
                        invalid();
                    ++length;
                }
                if (length < 3u || length > 8u)
                    invalid();
                return lower(code);
            }
            if (aType == "dateTimeField")
            {
                static char const* const sFields[] = { "era", "year", "quarter", "month", "weekOfYear", "weekday", "day", "dayPeriod", "hour",
                    "minute", "second", "timeZoneName" };
                if (std::none_of(std::begin(sFields), std::end(sFields), [&](char const* f) { return code == f; }))
                    invalid();
                return code;
            }
            // (currency)
            if (!is_well_formed_currency_code(aCode))
                invalid();
            return upper(code);
        }

        // (a code's display name (none if there is none))
        std::optional<std::u16string> display_name(display_names_data const& d, std::string const& aCode)
        {
            UErrorCode status = U_ZERO_ERROR;
            std::u16string name;
            auto const result = [&]() -> std::optional<std::u16string>
            {
                if (name.empty())
                    return std::nullopt;
                return name;
            };
            if (d.type == "language")
            {
                auto const icu = icu_locale(aCode);
                name = icu_string([&](UChar* b, std::int32_t c, UErrorCode* s) { return uldn_localeDisplayName(d.names.get(), icu.c_str(), b, c, s); });
                return result();
            }
            if (d.type == "region")
            {
                name = icu_string([&](UChar* b, std::int32_t c, UErrorCode* s) { return uldn_regionDisplayName(d.names.get(), aCode.c_str(), b, c, s); });
                return result();
            }
            if (d.type == "script")
            {
                name = icu_string([&](UChar* b, std::int32_t c, UErrorCode* s) { return uldn_scriptDisplayName(d.names.get(), aCode.c_str(), b, c, s); });
                return result();
            }
            if (d.type == "calendar")
            {
                auto const* legacy = uloc_toLegacyType("calendar", aCode.c_str());
                std::string const type = legacy != nullptr ? legacy : aCode;
                name = icu_string([&](UChar* b, std::int32_t c, UErrorCode* s)
                    { return uldn_keyValueDisplayName(d.names.get(), "calendar", type.c_str(), b, c, s); });
                return result();
            }
            auto const icu = icu_locale(d.dataLocale);
            if (d.type == "dateTimeField")
            {
                static std::pair<char const*, UDateTimePatternField> const sFields[] = { { "era", UDATPG_ERA_FIELD }, { "year", UDATPG_YEAR_FIELD },
                    { "quarter", UDATPG_QUARTER_FIELD }, { "month", UDATPG_MONTH_FIELD }, { "weekOfYear", UDATPG_WEEK_OF_YEAR_FIELD },
                    { "weekday", UDATPG_WEEKDAY_FIELD }, { "day", UDATPG_DAY_FIELD }, { "dayPeriod", UDATPG_DAYPERIOD_FIELD },
                    { "hour", UDATPG_HOUR_FIELD }, { "minute", UDATPG_MINUTE_FIELD }, { "second", UDATPG_SECOND_FIELD },
                    { "timeZoneName", UDATPG_ZONE_FIELD } };
                auto const field = std::find_if(std::begin(sFields), std::end(sFields), [&](auto const& f) { return aCode == f.first; })->second;
                date_pattern_generator_ptr generator{ udatpg_open(icu.c_str(), &status) };
                if (U_FAILURE(status))
                    return std::nullopt;
                auto const width = d.style == "long" ? UDATPG_WIDE : d.style == "short" ? UDATPG_ABBREVIATED : UDATPG_NARROW;
                name = icu_string([&](UChar* b, std::int32_t c, UErrorCode* s) { return udatpg_getFieldDisplayName(generator.get(), field, width, b, c, s); });
                return result();
            }
            // (currency: its long name, symbol or narrow symbol (ICU's default (the code itself) is none))
            auto const currency = from_ascii(aCode);
            UBool isChoiceFormat = false;
            std::int32_t length = 0;
            auto const style = d.style == "long" ? UCURR_LONG_NAME : d.style == "short" ? UCURR_SYMBOL_NAME : UCURR_NARROW_SYMBOL_NAME;
            auto const* text = ucurr_getName(uchars(currency), icu.c_str(), style, &isChoiceFormat, &length, &status);
            if (U_FAILURE(status) || status == U_USING_DEFAULT_WARNING || text == nullptr)
                return std::nullopt;
            name.assign(reinterpret_cast<char16_t const*>(text), static_cast<std::size_t>(length));
            return result();
        }

        value display_names_of(value aThis, arguments aArguments, value, function_object&)
        {
            auto& dn = this_intl(aThis, object_class::IntlDisplayNames, "Intl.DisplayNames.prototype.of");
            auto const& d = data_of<display_names_data>(dn);
            auto const code = canonical_code(d.type, str(to_string(argument(aArguments, 0u))));
            if (auto const name = display_name(d, code))
                return u16_value(*name);
            if (d.fallback == "code")
                return u16_value(from_ascii(code));
            return Undefined;
        }

        value display_names_resolved_options(value aThis, arguments, value, function_object&)
        {
            auto& dn = this_intl(aThis, object_class::IntlDisplayNames, "Intl.DisplayNames.prototype.resolvedOptions");
            auto const& d = data_of<display_names_data>(dn);
            auto const o = make_object();
            create_data_property_or_throw(o, ascii("locale"), u16_value(from_ascii(dn.locale)));
            create_data_property_or_throw(o, ascii("style"), u16_value(from_ascii(d.style)));
            create_data_property_or_throw(o, ascii("type"), u16_value(from_ascii(d.type)));
            create_data_property_or_throw(o, ascii("fallback"), u16_value(from_ascii(d.fallback)));
            if (!d.languageDisplay.empty())
                create_data_property_or_throw(o, ascii("languageDisplay"), u16_value(from_ascii(d.languageDisplay)));
            return o;
        }
    }

    void init_intl_display_names(realm& aRealm, value)
    {
        make_intl_constructor(aRealm, "DisplayNames", 2u, display_names_constructor, &realm::intlDisplayNamesPrototype, "Intl.DisplayNames");
        auto const p = aRealm.intlDisplayNamesPrototype;
        method(p, "of", 1u, display_names_of);
        method(p, "resolvedOptions", 0u, display_names_resolved_options);
    }
}

#endif
