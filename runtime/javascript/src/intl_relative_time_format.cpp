/*
  intl_relative_time_format.cpp

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

// Intl.RelativeTimeFormat (ECMA-402 §17) with ICU's relative date-time formatter (ureldatefmt).

#include "intl.hpp"

#if defined(NEOS_JS_ICU)

#include <cmath>
#include "intl_icu.hpp"

namespace neos::javascript_runtime::intl
{
    namespace
    {
        struct relative_time_format_data : intl_data
        {
            std::string style;
            std::string numeric;
            std::string numberingSystem;
            relative_date_format_ptr format;
        };

        struct relative_time_locale_data : locale_data
        {
            std::vector<std::string> values(std::string const& aDataLocale, std::string_view aKey) const override
            {
                if (aKey != "nu")
                    return {};
                return { default_numbering_system(aDataLocale) };
            }
            bool supports(std::string const& aDataLocale, std::string_view aKey, std::string const& aValue) const override
            {
                return is_supported_numbering_system(aDataLocale, aValue);
            }
        };


        value relative_time_format_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (is_undefined(aNewTarget))
                throw_type_error("Constructor Intl.RelativeTimeFormat requires 'new'");
            auto const result = make_intl_object(aNewTarget, &realm::intlRelativeTimeFormatPrototype, object_class::IntlRelativeTimeFormat);
            auto& rtf = static_cast<intl_object&>(obj(result));
            auto data = std::make_unique<relative_time_format_data>();
            auto& d = *data;
            auto const requested = canonicalize_locale_list(argument(aArguments, 0u));
            auto const options = coerce_options_to_object(argument(aArguments, 1u));
            auto const matcher = *get_string_option(options, "localeMatcher", { "lookup", "best fit" }, "best fit");
            auto const numberingSystem = get_string_option(options, "numberingSystem", {});
            if (numberingSystem && !is_type_sequence(*numberingSystem))
                throw_range_error("Invalid numberingSystem : " + to_ascii(*numberingSystem));
            std::vector<relevant_key> keys{ { "nu", numberingSystem ? std::optional<std::string>{ to_ascii(*numberingSystem) } : std::nullopt } };
            auto const r = resolve_locale(service::RelativeTimeFormat, requested, matcher, keys, relative_time_locale_data{});
            rtf.locale = r.locale;
            d.numberingSystem = r.keywords.at("nu");
            d.style = to_ascii(*get_string_option(options, "style", { "long", "short", "narrow" }, "long"));
            d.numeric = to_ascii(*get_string_option(options, "numeric", { "always", "auto" }, "always"));
            auto const icu = icu_locale(r.dataLocale) + "@numbers=" + d.numberingSystem;
            UErrorCode status = U_ZERO_ERROR;
            auto* number = unum_open(UNUM_DECIMAL, nullptr, 0, icu.c_str(), nullptr, &status);
            if (U_FAILURE(status))
                throw_range_error("Intl.RelativeTimeFormat: the number format cannot be made");
            // (the locale's minimum grouping (e.g. Polish groups from five digits))
#if defined(NEOS_JS_WINDOWS_ICU) || U_ICU_VERSION_MAJOR_NUM >= 68
            unum_setAttribute(number, UNUM_MINIMUM_GROUPING_DIGITS, UNUM_MINIMUM_GROUPING_DIGITS_AUTO);
#endif
            d.format.reset(ureldatefmt_open(icu.c_str(), number, d.style == "long" ? UDAT_STYLE_LONG : d.style == "short" ? UDAT_STYLE_SHORT :
                UDAT_STYLE_NARROW, UDISPCTX_CAPITALIZATION_NONE, &status));
            if (U_FAILURE(status))
                throw_range_error("Intl.RelativeTimeFormat: the format cannot be made");
            rtf.data = std::move(data);
            return result;
        }

        // (SingularRelativeTimeUnit)
        std::pair<URelativeDateTimeUnit, char const*> singular_unit(std::u16string const& aUnit)
        {
            static std::pair<char const*, URelativeDateTimeUnit> const sUnits[] = { { "second", UDAT_REL_UNIT_SECOND },
                { "minute", UDAT_REL_UNIT_MINUTE }, { "hour", UDAT_REL_UNIT_HOUR }, { "day", UDAT_REL_UNIT_DAY }, { "week", UDAT_REL_UNIT_WEEK },
                { "month", UDAT_REL_UNIT_MONTH }, { "quarter", UDAT_REL_UNIT_QUARTER }, { "year", UDAT_REL_UNIT_YEAR } };
            auto const unit = to_ascii(aUnit);
            for (auto const& [name, u] : sUnits)
                if (unit == name || unit == std::string{ name } + "s")
                    return { u, name };
            throw_range_error("Invalid unit argument for format() '" + unit + "'");
        }

        std::optional<std::u16string> relative_part_type(field_span const& aSpan, std::u16string_view aText, void*)
        {
            if (aSpan.category != UFIELD_CATEGORY_NUMBER)
                return std::nullopt;
            switch (aSpan.field)
            {
            case UNUM_INTEGER_FIELD: return u"integer";
            case UNUM_FRACTION_FIELD: return u"fraction";
            case UNUM_DECIMAL_SEPARATOR_FIELD: return u"decimal";
            case UNUM_GROUPING_SEPARATOR_FIELD: return u"group";
            case UNUM_SIGN_FIELD:
                return aText.substr(static_cast<std::size_t>(aSpan.begin), static_cast<std::size_t>(aSpan.end - aSpan.begin)).find(u'+') !=
                    std::u16string_view::npos ? std::u16string{ u"plusSign" } : std::u16string{ u"minusSign" };
            case UNUM_EXPONENT_SYMBOL_FIELD: return u"exponentSeparator";
            case UNUM_EXPONENT_SIGN_FIELD: return u"exponentMinusSign";
            case UNUM_EXPONENT_FIELD: return u"exponentInteger";
            case UNUM_COMPACT_FIELD: return u"compact";
            default: return u"unknown";
            }
        }

        // (PartitionRelativeTimePattern)
        std::vector<part> relative_time_parts(intl_object& aRtf, value aValue, value aUnit)
        {
            auto& d = data_of<relative_time_format_data>(aRtf);
            auto const x = to_number(aValue);
            auto const unitString = str(to_string(aUnit));
            if (!std::isfinite(x))
                throw_range_error("Invalid value: not finite");
            auto const [unit, singular] = singular_unit(unitString);
            UErrorCode status = U_ZERO_ERROR;
            formatted_relative_date_ptr result{ ureldatefmt_openResult(&status) };
            if (d.numeric == "auto")
                ureldatefmt_formatToResult(d.format.get(), x, unit, result.get(), &status);
            else
                ureldatefmt_formatNumericToResult(d.format.get(), x, unit, result.get(), &status);
            if (U_FAILURE(status))
                throw_range_error("Intl.RelativeTimeFormat: the value cannot be formatted");
            auto const* v = ureldatefmt_resultAsValue(result.get(), &status);
            auto parts = make_parts(formatted_value_string(v), formatted_value_spans(v), relative_part_type, nullptr);
            for (auto& p : parts)
                if (p.type != u"literal")
                    p.unit = from_ascii(singular);
            return parts;
        }

        value relative_time_format_format(value aThis, arguments aArguments, value, function_object&)
        {
            auto& rtf = this_intl(aThis, object_class::IntlRelativeTimeFormat, "Intl.RelativeTimeFormat.prototype.format");
            std::u16string text;
            for (auto const& p : relative_time_parts(rtf, argument(aArguments, 0u), argument(aArguments, 1u)))
                text += p.text;
            return u16_value(text);
        }

        value relative_time_format_format_to_parts(value aThis, arguments aArguments, value, function_object&)
        {
            auto& rtf = this_intl(aThis, object_class::IntlRelativeTimeFormat, "Intl.RelativeTimeFormat.prototype.formatToParts");
            return parts_array(relative_time_parts(rtf, argument(aArguments, 0u), argument(aArguments, 1u)));
        }

        value relative_time_format_resolved_options(value aThis, arguments, value, function_object&)
        {
            auto& rtf = this_intl(aThis, object_class::IntlRelativeTimeFormat, "Intl.RelativeTimeFormat.prototype.resolvedOptions");
            auto const& d = data_of<relative_time_format_data>(rtf);
            auto const o = make_object();
            create_data_property_or_throw(o, ascii("locale"), u16_value(from_ascii(rtf.locale)));
            create_data_property_or_throw(o, ascii("style"), u16_value(from_ascii(d.style)));
            create_data_property_or_throw(o, ascii("numeric"), u16_value(from_ascii(d.numeric)));
            create_data_property_or_throw(o, ascii("numberingSystem"), u16_value(from_ascii(d.numberingSystem)));
            return o;
        }
    }

    void init_intl_relative_time_format(realm& aRealm, value)
    {
        make_intl_constructor(aRealm, "RelativeTimeFormat", 0u, relative_time_format_constructor, &realm::intlRelativeTimeFormatPrototype,
            "Intl.RelativeTimeFormat");
        auto const p = aRealm.intlRelativeTimeFormatPrototype;
        method(p, "format", 2u, relative_time_format_format);
        method(p, "formatToParts", 2u, relative_time_format_format_to_parts);
        method(p, "resolvedOptions", 0u, relative_time_format_resolved_options);
    }
}

#endif
