/*
  intl.cpp

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

// Intl (ECMA-402): the Intl object (getCanonicalLocales, supportedValuesOf), locale lists and negotiation (CanonicalizeLocaleList,
// LookupMatchingLocaleByPrefix, ResolveLocale, FilterLocales), the option getters and the Intl objects' common parts.

#include "intl.hpp"

#if defined(NEOS_JS_ICU)

#include <algorithm>
#include <cmath>
#include <set>
#include <cstring>
#include "gc.hpp"
#include "intl_icu.hpp"
#include "temporal.hpp"

namespace neos::javascript_runtime::intl
{
    // --- strings

    std::string to_ascii(std::u16string_view aText)
    {
        std::string result;
        result.reserve(aText.size());
        for (auto c : aText)
            result.push_back(c < 0x80u ? static_cast<char>(c) : '?');
        return result;
    }

    std::u16string from_ascii(std::string_view aText)
    {
        return std::u16string{ aText.begin(), aText.end() };
    }

    value u16_value(std::u16string_view aText)
    {
        return make_string(aText);
    }

    // --- the Intl objects

    void intl_object::trace(tracer& aTracer)
    {
        object::trace(aTracer);
        aTracer.mark(bound);
        aTracer.mark(extra);
        if (data)
            data->trace(aTracer);
    }

    intl_object* as_intl(value aValue, object_class aClass)
    {
        if (!is_object(aValue))
            return nullptr;
        auto& o = obj(aValue);
        return o.cls == aClass ? static_cast<intl_object*>(&o) : nullptr;
    }

    intl_object& this_intl(value aThis, object_class aClass, char const* aMethod)
    {
        auto* o = as_intl(aThis, aClass);
        if (o == nullptr || !o->data)
            throw_type_error(std::string{ "Method " } + aMethod + " called on incompatible receiver");
        return *o;
    }

    value make_intl_object(value aNewTarget, value realm::*aDefaultPrototype, object_class aClass)
    {
        auto const prototype = get_prototype_from_constructor(aNewTarget, aDefaultPrototype);
        auto& o = the_heap().make<intl_object>(prototype, aClass);
        return value_of(o);
    }

    namespace
    {
        value supported_locales_of(value, arguments aArguments, value, function_object& aCallee)
        {
            return supported_locales(static_cast<service>(aCallee.nativeId), argument(aArguments, 0u), argument(aArguments, 1u));
        }

        service service_of(char const* aName)
        {
            static char const* const sNames[] = { "Collator", "DateTimeFormat", "DisplayNames", "DurationFormat", "ListFormat", "NumberFormat",
                "PluralRules", "RelativeTimeFormat", "Segmenter" };
            for (std::size_t i = 0u; i < std::size(sNames); ++i)
                if (std::strcmp(sNames[i], aName) == 0)
                    return static_cast<service>(i);
            return service::Count;
        }
    }

    value make_intl_constructor(realm& aRealm, char const* aName, std::uint32_t aLength, native_function aFunction, value realm::*aPrototype,
        char const* aTag)
    {
        auto const p = make_object();
        auto const c = make_native(aName, aLength, aFunction, true);
        aRealm.*aPrototype = p;
        obj(c).put(the_names().prototype, p, 0u);
        obj(p).put(the_names().constructor, c);
        obj(p).put(aRealm.symToStringTag, ascii(aTag), AttributeConfigurable);
        obj(aRealm.intl).put(ascii(aName), c);
        as_function(method(c, "supportedLocalesOf", 1u, supported_locales_of))->nativeId = static_cast<std::uint32_t>(service_of(aName));
        return c;
    }

    value parts_array(std::vector<part> const& aParts)
    {
        auto const result = make_array();
        auto const typeKey = ascii("type");
        auto const valueKey = ascii("value");
        auto const sourceKey = ascii("source");
        auto const unitKey = ascii("unit");
        std::uint32_t index = 0u;
        for (auto const& p : aParts)
        {
            auto const o = make_object();
            create_data_property_or_throw(o, typeKey, u16_value(p.type));
            create_data_property_or_throw(o, valueKey, u16_value(p.text));
            if (p.source)
                create_data_property_or_throw(o, sourceKey, u16_value(*p.source));
            if (p.unit)
                create_data_property_or_throw(o, unitKey, u16_value(*p.unit));
            create_data_property_or_throw(result, index_key(index++), o);
        }
        return result;
    }

    std::vector<part> make_parts(std::u16string const& aText, std::vector<field_span> const& aSpans, field_type aType, void* aContext)
    {
        // (each code unit's innermost span (the shortest that covers it; a later one of the same length (e.g. a nested field ICU
        // reports after its container) wins))
        std::vector<std::int32_t> owner(aText.size(), -1);
        for (std::int32_t s = 0; s < static_cast<std::int32_t>(aSpans.size()); ++s)
        {
            auto const& span = aSpans[static_cast<std::size_t>(s)];
            if (!aType(span, aText, aContext))
                continue;
            for (auto i = std::max(span.begin, 0); i < std::min(span.end, static_cast<std::int32_t>(aText.size())); ++i)
            {
                auto const current = owner[static_cast<std::size_t>(i)];
                if (current < 0)
                    owner[static_cast<std::size_t>(i)] = s;
                else
                {
                    auto const& c = aSpans[static_cast<std::size_t>(current)];
                    if (span.end - span.begin <= c.end - c.begin)
                        owner[static_cast<std::size_t>(i)] = s;
                }
            }
        }
        std::vector<part> result;
        std::size_t i = 0u;
        while (i < aText.size())
        {
            auto const o = owner[i];
            auto j = i + 1u;
            while (j < aText.size() && owner[j] == o)
                ++j;
            part p;
            if (o >= 0)
                p.type = *aType(aSpans[static_cast<std::size_t>(o)], aText, aContext);
            else
                p.type = u"literal";
            p.text = aText.substr(i, j - i);
            result.push_back(std::move(p));
            i = j;
        }
        return result;
    }

    bool is_type_sequence(std::u16string_view aValue)
    {
        std::size_t length = 0u;
        for (auto c : aValue)
        {
            if (c == u'-')
            {
                if (length < 3u || length > 8u)
                    return false;
                length = 0u;
                continue;
            }
            if (!((c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9')))
                return false;
            ++length;
        }
        return length >= 3u && length <= 8u;
    }

    std::string default_numbering_system(std::string const& aDataLocale)
    {
        UErrorCode status = U_ZERO_ERROR;
        auto const icu = icu_locale(aDataLocale);
        numbering_system_ptr ns{ unumsys_open(icu.c_str(), &status) };
        return U_SUCCESS(status) && unumsys_getName(ns.get()) != nullptr ? unumsys_getName(ns.get()) : "latn";
    }

    bool is_supported_numbering_system(std::string const& aDataLocale, std::string const& aValue)
    {
        if (default_numbering_system(aDataLocale) == aValue)
            return true;
        UErrorCode status = U_ZERO_ERROR;
        numbering_system_ptr ns{ unumsys_openByName(aValue.c_str(), &status) };
        return U_SUCCESS(status) && ns && !unumsys_isAlgorithmic(ns.get());
    }

    std::vector<field_span> formatted_value_spans(UFormattedValue const* aValue)
    {
        std::vector<field_span> result;
        UErrorCode status = U_ZERO_ERROR;
        constrained_position_ptr position{ ucfpos_open(&status) };
        if (U_FAILURE(status))
            return result;
        while (ufmtval_nextPosition(aValue, position.get(), &status) && U_SUCCESS(status))
        {
            field_span span{};
            span.category = ucfpos_getCategory(position.get(), &status);
            span.field = ucfpos_getField(position.get(), &status);
            ucfpos_getIndexes(position.get(), &span.begin, &span.end, &status);
            if (U_FAILURE(status))
                break;
            result.push_back(span);
        }
        return result;
    }

    std::u16string formatted_value_string(UFormattedValue const* aValue)
    {
        UErrorCode status = U_ZERO_ERROR;
        std::int32_t length = 0;
        auto const* text = ufmtval_getString(aValue, &length, &status);
        if (U_FAILURE(status) || text == nullptr)
            return {};
        return std::u16string{ reinterpret_cast<char16_t const*>(text), static_cast<std::size_t>(length) };
    }

    // --- digit options

    void set_number_format_digit_options(digit_options& aOptions, value aOptionsObject, int aMnfdDefault, int aMxfdDefault,
        std::string_view aNotation)
    {
        // SetNumberFormatDigitOptions
        auto const options = aOptionsObject;
        aOptions.minimumIntegerDigits = *get_number_option(options, "minimumIntegerDigits", 1, 21, 1);
        auto const mnfd = get(options, ascii("minimumFractionDigits"));
        auto const mxfd = get(options, ascii("maximumFractionDigits"));
        auto const mnsd = get(options, ascii("minimumSignificantDigits"));
        auto const mxsd = get(options, ascii("maximumSignificantDigits"));
        auto const roundingIncrement = *get_number_option(options, "roundingIncrement", 1, 5000, 1);
        static constexpr int sIncrements[] = { 1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 2500, 5000 };
        if (std::find(std::begin(sIncrements), std::end(sIncrements), roundingIncrement) == std::end(sIncrements))
            throw_range_error("roundingIncrement value is out of range.");
        aOptions.roundingMode = to_ascii(*get_string_option(options, "roundingMode",
            { "ceil", "floor", "expand", "trunc", "halfCeil", "halfFloor", "halfExpand", "halfTrunc", "halfEven" }, "halfExpand"));
        auto const roundingPriority = to_ascii(*get_string_option(options, "roundingPriority", { "auto", "morePrecision", "lessPrecision" }, "auto"));
        aOptions.trailingZeroDisplay = to_ascii(*get_string_option(options, "trailingZeroDisplay", { "auto", "stripIfInteger" }, "auto"));
        if (roundingIncrement != 1)
            aMxfdDefault = aMnfdDefault;
        aOptions.roundingIncrement = roundingIncrement;
        bool const hasSd = !is_undefined(mnsd) || !is_undefined(mxsd);
        bool const hasFd = !is_undefined(mnfd) || !is_undefined(mxfd);
        bool needSd = true;
        bool needFd = true;
        if (roundingPriority == "auto")
        {
            needSd = hasSd;
            if (needSd || (!hasFd && aNotation == "compact"))
                needFd = false;
        }
        if (needSd)
        {
            if (hasSd)
            {
                auto const mn = *default_number_option(mnsd, 1, 21, 1, "minimumSignificantDigits");
                auto const mx = *default_number_option(mxsd, mn, 21, 21, "maximumSignificantDigits");
                aOptions.minimumSignificantDigits = mn;
                aOptions.maximumSignificantDigits = mx;
            }
            else
            {
                aOptions.minimumSignificantDigits = 1;
                aOptions.maximumSignificantDigits = 21;
            }
        }
        if (needFd)
        {
            if (hasFd)
            {
                auto mn = default_number_option(mnfd, 0, 100, std::nullopt, "minimumFractionDigits");
                auto mx = default_number_option(mxfd, 0, 100, std::nullopt, "maximumFractionDigits");
                if (!mn)
                    mn = std::min(aMnfdDefault, *mx);
                else if (!mx)
                    mx = std::max(aMxfdDefault, *mn);
                else if (*mn > *mx)
                    throw_range_error("maximumFractionDigits value is out of range.");
                aOptions.minimumFractionDigits = mn;
                aOptions.maximumFractionDigits = mx;
            }
            else
            {
                aOptions.minimumFractionDigits = aMnfdDefault;
                aOptions.maximumFractionDigits = aMxfdDefault;
            }
        }
        if (!needSd && !needFd)
        {
            aOptions.minimumFractionDigits = 0;
            aOptions.maximumFractionDigits = 0;
            aOptions.minimumSignificantDigits = 1;
            aOptions.maximumSignificantDigits = 2;
            aOptions.roundingType = "morePrecision";
            aOptions.computedRoundingPriority = "morePrecision";
        }
        else if (roundingPriority == "auto")
        {
            aOptions.roundingType = hasSd ? "significantDigits" : "fractionDigits";
            aOptions.computedRoundingPriority = "auto";
        }
        else
        {
            aOptions.roundingType = roundingPriority;
            aOptions.computedRoundingPriority = roundingPriority;
        }
        if (roundingIncrement != 1)
        {
            if (aOptions.roundingType != "fractionDigits")
                throw_type_error("roundingIncrement is only supported with fraction digits rounding");
            if (aOptions.maximumFractionDigits != aOptions.minimumFractionDigits)
                throw_range_error("maximumFractionDigits must be equal to minimumFractionDigits with a roundingIncrement");
        }
    }

    std::string digit_options_skeleton(digit_options const& aOptions)
    {
        std::string result;
        auto const fraction = [&]()
        {
            auto const mn = aOptions.minimumFractionDigits.value_or(0);
            auto const mx = aOptions.maximumFractionDigits.value_or(mn);
            if (mx == 0)
                return std::string{ "." };
            return "." + std::string(static_cast<std::size_t>(mn), '0') + std::string(static_cast<std::size_t>(mx - mn), '#');
        };
        auto const significant = [&]()
        {
            auto const mn = aOptions.minimumSignificantDigits.value_or(1);
            auto const mx = aOptions.maximumSignificantDigits.value_or(mn);
            return std::string(static_cast<std::size_t>(mn), '@') + std::string(static_cast<std::size_t>(mx - mn), '#');
        };
        std::string precision;
        if (aOptions.roundingIncrement != 1)
        {
            // (the increment in units of the last fraction digit)
            auto const digits = aOptions.maximumFractionDigits.value_or(0);
            auto increment = std::to_string(aOptions.roundingIncrement);
            if (digits > 0)
            {
                if (static_cast<int>(increment.size()) <= digits)
                    increment = std::string(static_cast<std::size_t>(digits - static_cast<int>(increment.size()) + 1), '0') + increment;
                increment.insert(increment.size() - static_cast<std::size_t>(digits), ".");
            }
            precision = "precision-increment/" + increment;
        }
        else if (aOptions.roundingType == "significantDigits")
            precision = significant();
        else if (aOptions.roundingType == "fractionDigits")
            precision = fraction();
#if defined(NEOS_JS_WINDOWS_ICU) || U_ICU_VERSION_MAJOR_NUM >= 69
        else
            precision = fraction() + "/" + significant() + (aOptions.roundingType == "morePrecision" ? "r" : "s");
        if (aOptions.trailingZeroDisplay == "stripIfInteger")
            precision += "/w";
#else
        // (before ICU 69 there is no rounding priority and no trailing zero display: compact notation's default (more precision of no
        // fraction digits and two significant digits) is ICU's default for compact notation; otherwise the significant digits with more
        // precision, the fraction digits with less)
        else if (aOptions.roundingType == "morePrecision" && aOptions.minimumFractionDigits.value_or(0) == 0 &&
            aOptions.maximumFractionDigits.value_or(0) == 0 && aOptions.minimumSignificantDigits.value_or(1) == 1 &&
            aOptions.maximumSignificantDigits.value_or(1) == 2)
            precision = {};
        else
            precision = aOptions.roundingType == "morePrecision" ? significant() : fraction();
#endif
        result += precision;
        static constexpr std::pair<char const*, char const*> sModes[] =
        {
            { "ceil", "rounding-mode-ceiling" }, { "floor", "rounding-mode-floor" }, { "expand", "rounding-mode-up" }, { "trunc", "rounding-mode-down" },
#if defined(NEOS_JS_WINDOWS_ICU) || U_ICU_VERSION_MAJOR_NUM >= 69
            { "halfCeil", "rounding-mode-half-ceiling" }, { "halfFloor", "rounding-mode-half-floor" }, { "halfExpand", "rounding-mode-half-up" },
#else
            // (before ICU 69: half-ceiling and half-floor are half-up)
            { "halfCeil", "rounding-mode-half-up" }, { "halfFloor", "rounding-mode-half-up" }, { "halfExpand", "rounding-mode-half-up" },
#endif
            { "halfTrunc", "rounding-mode-half-down" }, { "halfEven", "rounding-mode-half-even" }
        };
        for (auto const& [mode, stem] : sModes)
            if (aOptions.roundingMode == mode)
                result += std::string{ " " } + stem;
        if (aOptions.minimumIntegerDigits > 1)
            result += " integer-width/*" + std::string(static_cast<std::size_t>(aOptions.minimumIntegerDigits), '0');
        return result;
    }

    void digit_options_resolved(digit_options const& aOptions, value aObject, bool aRoundingFields)
    {
        create_data_property_or_throw(aObject, ascii("minimumIntegerDigits"), number(aOptions.minimumIntegerDigits));
        if (aOptions.minimumFractionDigits)
            create_data_property_or_throw(aObject, ascii("minimumFractionDigits"), number(*aOptions.minimumFractionDigits));
        if (aOptions.maximumFractionDigits)
            create_data_property_or_throw(aObject, ascii("maximumFractionDigits"), number(*aOptions.maximumFractionDigits));
        if (aOptions.minimumSignificantDigits)
            create_data_property_or_throw(aObject, ascii("minimumSignificantDigits"), number(*aOptions.minimumSignificantDigits));
        if (aOptions.maximumSignificantDigits)
            create_data_property_or_throw(aObject, ascii("maximumSignificantDigits"), number(*aOptions.maximumSignificantDigits));
        if (aRoundingFields)
            digit_options_rounding_resolved(aOptions, aObject);
    }

    void digit_options_rounding_resolved(digit_options const& aOptions, value aObject)
    {
        create_data_property_or_throw(aObject, ascii("roundingIncrement"), number(aOptions.roundingIncrement));
        create_data_property_or_throw(aObject, ascii("roundingMode"), u16_value(from_ascii(aOptions.roundingMode)));
        create_data_property_or_throw(aObject, ascii("roundingPriority"), u16_value(from_ascii(aOptions.computedRoundingPriority)));
        create_data_property_or_throw(aObject, ascii("trailingZeroDisplay"), u16_value(from_ascii(aOptions.trailingZeroDisplay)));
    }

    // --- options

    value get_options_object(value aOptions)
    {
        if (is_undefined(aOptions))
            return make_object(Null);
        if (is_object(aOptions))
            return aOptions;
        throw_type_error("Options must be an object");
    }

    value coerce_options_to_object(value aOptions)
    {
        if (is_undefined(aOptions))
            return make_object(Null);
        return to_object(aOptions);
    }

    std::optional<std::u16string> get_string_option(value aOptions, char const* aProperty, std::initializer_list<char const*> aValues,
        std::optional<char const*> aDefault)
    {
        auto const v = get(aOptions, ascii(aProperty));
        if (is_undefined(v))
        {
            if (aDefault)
                return from_ascii(*aDefault);
            return {};
        }
        std::u16string text{ str(to_string(v)) };
        if (aValues.size() != 0u)
        {
            bool found = false;
            for (auto const* allowed : aValues)
                if (text == from_ascii(allowed))
                    found = true;
            if (!found)
                throw_range_error("Value " + utf8(make_string(text)) + " out of range for options property " + aProperty);
        }
        return text;
    }

    std::optional<bool> get_boolean_option(value aOptions, char const* aProperty, std::optional<bool> aDefault)
    {
        auto const v = get(aOptions, ascii(aProperty));
        if (is_undefined(v))
            return aDefault;
        return to_boolean(v);
    }

    std::optional<int> default_number_option(value aValue, int aMinimum, int aMaximum, std::optional<int> aFallback, char const* aProperty)
    {
        if (is_undefined(aValue))
            return aFallback;
        auto const n = to_number(aValue);
        if (std::isnan(n) || n < aMinimum || n > aMaximum)
            throw_range_error(std::string{ aProperty } + " value is out of range.");
        return static_cast<int>(std::floor(n));
    }

    std::optional<int> get_number_option(value aOptions, char const* aProperty, int aMinimum, int aMaximum, std::optional<int> aFallback)
    {
        return default_number_option(get(aOptions, ascii(aProperty)), aMinimum, aMaximum, aFallback, aProperty);
    }

    // --- locale lists

    std::vector<std::string> canonicalize_locale_list(value aLocales)
    {
        std::vector<std::string> seen;
        if (is_undefined(aLocales))
            return seen;
        value o;
        if (is_string(aLocales) || as_intl(aLocales, object_class::IntlLocale) != nullptr)
        {
            value const element = aLocales;
            o = create_array_from_list(std::span<value const>{ &element, 1u });
        }
        else
            o = to_object(aLocales);
        auto const length = to_length(get(o, the_names().length));
        for (double k = 0; k < length; ++k)
        {
            auto const key = index_key(static_cast<std::uint64_t>(k));
            if (!has_property(o, key))
                continue;
            auto const element = get(o, key);
            if (!is_string(element) && !is_object(element))
                throw_type_error("Language ID should be string or object.");
            std::string tag;
            if (auto* l = as_intl(element, object_class::IntlLocale))
                tag = l->locale;
            else
            {
                auto const& text = str(to_string(element));
                for (auto c : text)
                    if (c >= 0x80u)
                        throw_range_error("Incorrect locale information provided");
                tag = to_ascii(text);
            }
            auto parsed = parse_language_tag(tag);
            if (!parsed)
                throw_range_error("Incorrect locale information provided");
            canonicalize(*parsed);
            auto canonical = parsed->to_string();
            if (std::find(seen.begin(), seen.end(), canonical) == seen.end())
                seen.push_back(std::move(canonical));
        }
        return seen;
    }

    namespace
    {
        // (the available locales: ICU's (as canonical tags), with their minimal forms (e.g. zh-Hant-TW's zh-TW))
        std::set<std::string> const& available_locales(service aService)
        {
            static std::set<std::string> const sLocales = []()
            {
                std::set<std::string> result;
                auto const add = [&](char const* aIcu)
                {
                    auto const tag = language_tag(aIcu);
                    if (tag.empty() || tag.find("-x-") != std::string::npos || tag.find("-u-") != std::string::npos)
                        return;
                    result.insert(tag);
                    auto const minimized = icu_chars([&](char* b, std::int32_t c, UErrorCode* s) { return uloc_minimizeSubtags(aIcu, b, c, s); });
                    if (!minimized.empty())
                        result.insert(language_tag(minimized.c_str()));
                };
                for (std::int32_t i = 0; i < uloc_countAvailable(); ++i)
                    add(uloc_getAvailable(i));
                return result;
            }();
            static std::set<std::string> const sCollatorLocales = []()
            {
                std::set<std::string> result = sLocales;
                for (std::int32_t i = 0; i < ucol_countAvailable(); ++i)
                    result.insert(language_tag(ucol_getAvailable(i)));
                return result;
            }();
            return aService == service::Collator ? sCollatorLocales : sLocales;
        }

        // (a tag without its -u- extension)
        std::string remove_unicode_extension(std::string const& aTag, std::string* aExtension = nullptr)
        {
            auto parsed = parse_language_tag(aTag);
            if (!parsed || !parsed->unicodeExtension)
                return aTag;
            if (aExtension != nullptr)
            {
                locale_id u;
                u.unicodeExtension = true;
                u.attributes = parsed->attributes;
                u.keywords = parsed->keywords;
                auto const s = u.to_string();
                *aExtension = s.substr(s.find("-u"));
            }
            parsed->unicodeExtension = false;
            parsed->attributes.clear();
            parsed->keywords.clear();
            return parsed->to_string();
        }
    }

    bool is_available_locale(service aService, std::string const& aTag)
    {
        return available_locales(aService).contains(aTag);
    }

    std::optional<std::string> lookup_matching_locale(service aService, std::string const& aNoExtensionsLocale)
    {
        // (LookupMatchingLocaleByPrefix's search: the locale, then its prefixes (a singleton's subtag goes with it))
        auto prefix = aNoExtensionsLocale;
        auto const& available = available_locales(aService);
        while (!prefix.empty())
        {
            if (available.contains(prefix))
                return prefix;
            auto pos = prefix.rfind('-');
            if (pos == std::string::npos)
                break;
            if (pos >= 2u && prefix[pos - 2u] == '-')
                pos -= 2u;
            prefix.resize(pos);
        }
        return {};
    }

    std::string const& default_locale()
    {
        static std::string const sLocale = []()
        {
            // (the host's (ICU's default), its base name, if it is available; else en-US)
            auto tag = language_tag(uloc_getDefault());
            if (auto parsed = parse_language_tag(tag))
            {
                parsed->variants.clear();
                tag = parsed->base_name();
            }
            if (auto const match = lookup_matching_locale(service::NumberFormat, tag); match && *match != "und")
                return *match;
            return std::string{ "en-US" };
        }();
        return sLocale;
    }

    bool locale_data::supports(std::string const& aDataLocale, std::string_view aKey, std::string const& aValue) const
    {
        auto const v = values(aDataLocale, aKey);
        return std::find(v.begin(), v.end(), aValue) != v.end();
    }

    std::string insert_unicode_extension(std::string const& aLocale, std::vector<std::pair<std::string, std::string>> const& aKeywords)
    {
        // (InsertUnicodeExtensionAndCanonicalize (the locale has no -u- extension))
        auto parsed = parse_language_tag(aLocale);
        if (!parsed)
            return aLocale;
        for (auto const& [key, val] : aKeywords)
            parsed->keywords.emplace_back(key, val);
        parsed->unicodeExtension = parsed->unicodeExtension || !aKeywords.empty();
        canonicalize(*parsed);
        return parsed->to_string();
    }

    resolved_locale resolve_locale(service aService, std::vector<std::string> const& aRequested, std::u16string_view,
        std::vector<relevant_key> const& aKeys, locale_data const& aData)
    {
        // ResolveLocale (the best fit matcher is the lookup matcher)
        std::optional<std::string> found;
        std::string extension;
        for (auto const& locale : aRequested)
        {
            std::string e;
            auto const noExtensions = remove_unicode_extension(locale, &e);
            if (auto const match = lookup_matching_locale(aService, noExtensions))
            {
                found = *match;
                extension = e;
                break;
            }
        }
        resolved_locale result;
        if (!found)
            found = default_locale();
        result.dataLocale = *found;
        std::vector<std::pair<std::string, std::string>> keywords;
        if (!extension.empty())
            if (auto const parsed = parse_language_tag("und" + extension))
                keywords = parsed->keywords;
        std::vector<std::pair<std::string, std::string>> supported;
        for (auto const& k : aKeys)
        {
            auto const keyValues = aData.values(result.dataLocale, k.key);
            std::string v = keyValues.empty() ? std::string{} : keyValues.front();
            std::optional<std::pair<std::string, std::string>> supportedKeyword;
            auto const entry = std::find_if(keywords.begin(), keywords.end(), [&](auto const& kw) { return kw.first == k.key; });
            if (entry != keywords.end())
            {
                auto const& requested = entry->second;
                if (!requested.empty())
                {
                    if (aData.supports(result.dataLocale, k.key, requested))
                    {
                        v = requested;
                        supportedKeyword = std::make_pair(std::string{ k.key }, v);
                    }
                }
                else if (aData.supports(result.dataLocale, k.key, "true"))
                {
                    v = "true";
                    supportedKeyword = std::make_pair(std::string{ k.key }, std::string{});
                }
            }
            if (k.option)
            {
                auto optionValue = canonicalize_u_value(k.key, *k.option);
                if (optionValue.empty())
                    optionValue = "true";
                if (optionValue != v && aData.supports(result.dataLocale, k.key, optionValue))
                {
                    v = optionValue;
                    supportedKeyword.reset();
                }
            }
            if (supportedKeyword)
                supported.push_back(*supportedKeyword);
            result.keywords[k.key] = v;
        }
        result.locale = supported.empty() ? result.dataLocale : insert_unicode_extension(result.dataLocale, supported);
        return result;
    }

    value supported_locales(service aService, value aLocales, value aOptions)
    {
        // FilterLocales
        auto const requested = canonicalize_locale_list(aLocales);
        auto const options = coerce_options_to_object(aOptions);
        get_string_option(options, "localeMatcher", { "lookup", "best fit" }, "best fit");
        std::vector<value> subset;
        for (auto const& locale : requested)
            if (lookup_matching_locale(aService, remove_unicode_extension(locale)))
                subset.push_back(u16_value(from_ascii(locale)));
        return create_array_from_list(subset);
    }

    // --- the Intl object's functions

    namespace
    {
        value get_canonical_locales(value, arguments aArguments, value, function_object&)
        {
            std::vector<value> result;
            for (auto const& l : canonicalize_locale_list(argument(aArguments, 0u)))
                result.push_back(u16_value(from_ascii(l)));
            return create_array_from_list(result);
        }

        std::vector<std::string> enumeration_strings(UEnumeration* aEnumeration)
        {
            std::vector<std::string> result;
            if (aEnumeration == nullptr)
                return result;
            UErrorCode status = U_ZERO_ERROR;
            std::int32_t length = 0;
            while (char const* s = uenum_next(aEnumeration, &length, &status))
            {
                if (U_FAILURE(status))
                    break;
                result.emplace_back(s, static_cast<std::size_t>(length));
            }
            return result;
        }

        value supported_values_of(value, arguments aArguments, value, function_object&)
        {
            auto const key = to_ascii(str(to_string(argument(aArguments, 0u))));
            std::set<std::string> values;
            UErrorCode status = U_ZERO_ERROR;
            if (key == "calendar")
            {
                enumeration_ptr e{ ucal_getKeywordValuesForLocale("calendar", "und", false, &status) };
                for (auto const& v : enumeration_strings(e.get()))
                    if (auto const* bcp = uloc_toUnicodeLocaleType("ca", v.c_str()))
                        // (islamic and islamic-rgsa are not supported (DateTimeFormat has other calendars for them))
                        if (std::strcmp(bcp, "islamic") != 0 && std::strcmp(bcp, "islamic-rgsa") != 0)
                            values.insert(bcp);
            }
            else if (key == "collation")
            {
                enumeration_ptr e{ ucol_getKeywordValues("collation", &status) };
                for (auto const& v : enumeration_strings(e.get()))
                    if (auto const* bcp = uloc_toUnicodeLocaleType("co", v.c_str()))
                        if (std::strcmp(bcp, "standard") != 0 && std::strcmp(bcp, "search") != 0)
                            values.insert(bcp);
            }
            else if (key == "currency")
            {
                // (the currencies with names (as DisplayNames has them))
                enumeration_ptr e{ ucurr_openISOCurrencies(UCURR_ALL, &status) };
                for (auto const& v : enumeration_strings(e.get()))
                {
                    std::string code = v;
                    for (auto& c : code)
                        c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
                    auto const currency = from_ascii(code);
                    UErrorCode s = U_ZERO_ERROR;
                    UBool isChoiceFormat = false;
                    std::int32_t length = 0;
                    ucurr_getName(reinterpret_cast<UChar const*>(currency.c_str()), "en", UCURR_LONG_NAME, &isChoiceFormat, &length, &s);
                    if (U_SUCCESS(s) && s != U_USING_DEFAULT_WARNING)
                        values.insert(code);
                }
            }
            else if (key == "numberingSystem")
            {
                enumeration_ptr e{ unumsys_openAvailableNames(&status) };
                for (auto const& v : enumeration_strings(e.get()))
                {
                    UErrorCode s = U_ZERO_ERROR;
                    numbering_system_ptr ns{ unumsys_openByName(v.c_str(), &s) };
                    if (U_SUCCESS(s) && !unumsys_isAlgorithmic(ns.get()))
                        values.insert(v);
                }
            }
            else if (key == "timeZone")
            {
                for (auto const& z : temporal::available_primary_time_zones())
                    values.insert(to_ascii(z));
            }
            else if (key == "unit")
            {
                static constexpr char const* sUnits[] =
                {
                    "acre", "bit", "byte", "celsius", "centimeter", "day", "degree", "fahrenheit", "fluid-ounce", "foot", "gallon", "gigabit",
                    "gigabyte", "gram", "hectare", "hour", "inch", "kilobit", "kilobyte", "kilogram", "kilometer", "liter", "megabit", "megabyte",
                    "meter", "microsecond", "mile", "mile-scandinavian", "milliliter", "millimeter", "millisecond", "minute", "month", "nanosecond",
                    "ounce", "percent", "petabyte", "pound", "second", "stone", "terabit", "terabyte", "week", "yard", "year"
                };
                for (auto const* u : sUnits)
                    values.insert(u);
            }
            else
                throw_range_error("Invalid key : " + key);
            std::vector<value> result;
            for (auto const& v : values)
                result.push_back(u16_value(from_ascii(v)));
            return create_array_from_list(result);
        }
    }
}

#if defined(NEOS_JS_WINDOWS_ICU)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <delayimp.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace neos::javascript_runtime::intl
{
    namespace
    {
        // (whether Windows' icu.dll (delay-loaded) has every function this module imports from it: Windows' ICU depends on the Windows
        // version)
        bool windows_icu_has_imports()
        {
            auto const* base = reinterpret_cast<std::uint8_t const*>(&__ImageBase);
            auto const* nt = reinterpret_cast<IMAGE_NT_HEADERS const*>(base + __ImageBase.e_lfanew);
            auto const& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
            if (directory.VirtualAddress == 0)
                return true;
            for (auto const* d = reinterpret_cast<ImgDelayDescr const*>(base + directory.VirtualAddress); d->rvaDLLName != 0; ++d)
            {
                if (_stricmp(reinterpret_cast<char const*>(base + d->rvaDLLName), "icu.dll") != 0)
                    continue;
                auto const icu = ::LoadLibraryExW(L"icu.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
                if (icu == nullptr)
                    return false;
                for (auto const* thunk = reinterpret_cast<IMAGE_THUNK_DATA const*>(base + d->rvaINT); thunk->u1.AddressOfData != 0; ++thunk)
                {
                    if (IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal))
                        continue;
                    auto const* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME const*>(base + thunk->u1.AddressOfData);
                    if (::GetProcAddress(icu, reinterpret_cast<char const*>(name->Name)) == nullptr)
                        return false;
                }
            }
            return true;
        }
    }

    bool icu_available()
    {
        static bool const sAvailable = windows_icu_has_imports();
        return sAvailable;
    }
}

#else

namespace neos::javascript_runtime::intl
{
    bool icu_available()
    {
        return true;
    }
}

#endif

namespace neos::javascript_runtime
{
    void init_intl(realm& aRealm)
    {
        using namespace intl;
        if (!icu_available())
            return;
        aRealm.intl = make_object();
        obj(aRealm.global).put(ascii("Intl"), aRealm.intl);
        obj(aRealm.intl).put(aRealm.symToStringTag, ascii("Intl"), AttributeConfigurable);
        method(aRealm.intl, "getCanonicalLocales", 1u, get_canonical_locales);
        method(aRealm.intl, "supportedValuesOf", 1u, supported_values_of);
        init_intl_locale(aRealm, aRealm.intl);
        init_intl_collator(aRealm, aRealm.intl);
        init_intl_number_format(aRealm, aRealm.intl);
        init_intl_plural_rules(aRealm, aRealm.intl);
        init_intl_date_time_format(aRealm, aRealm.intl);
        init_intl_relative_time_format(aRealm, aRealm.intl);
        init_intl_list_format(aRealm, aRealm.intl);
        init_intl_display_names(aRealm, aRealm.intl);
        init_intl_segmenter(aRealm, aRealm.intl);
        init_intl_duration_format(aRealm, aRealm.intl);
    }
}

#else

namespace neos::javascript_runtime
{
    void init_intl(realm&)
    {
    }
}

#endif
