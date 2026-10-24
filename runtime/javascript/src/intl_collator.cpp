/*
  intl_collator.cpp

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

// Intl.Collator (ECMA-402 §10) with ICU's collation (ucol), and String.prototype.localeCompare.

#include "intl.hpp"

#if defined(NEOS_JS_ICU)

#include <algorithm>
#include <cstring>
#include "gc.hpp"
#include "intl_icu.hpp"

namespace neos::javascript_runtime::intl
{
    namespace
    {
        // (String.prototype.localeCompare's collator of the default locale and options (of the realm that made it last): a root)
        value sDefaultCollator = Undefined;
        realm const* sDefaultCollatorRealm = nullptr;
        root_tracer const sDefaultCollatorRoot{ [](tracer& aTracer) { aTracer.mark(sDefaultCollator); } };

        struct collator_data : intl_data
        {
            collator_ptr collator;
            std::string usage;
            std::string sensitivity;
            bool ignorePunctuation = false;
            std::string collation;
            bool numeric = false;
            std::string caseFirst;
        };

        struct collator_locale_data : locale_data
        {
            std::vector<std::string> values(std::string const& aDataLocale, std::string_view aKey) const override
            {
                if (aKey == "co")
                {
                    // (null (the default) first; not "standard" or "search" (they are not collations a locale can ask for))
                    std::vector<std::string> result{ std::string{} };
                    UErrorCode status = U_ZERO_ERROR;
                    auto const icu = icu_locale(aDataLocale);
                    enumeration_ptr e{ ucol_getKeywordValuesForLocale("collation", icu.c_str(), false, &status) };
                    if (U_SUCCESS(status))
                    {
                        std::int32_t length = 0;
                        while (char const* v = uenum_next(e.get(), &length, &status))
                        {
                            if (U_FAILURE(status))
                                break;
                            auto const* bcp = uloc_toUnicodeLocaleType("co", v);
                            if (bcp != nullptr && std::strcmp(bcp, "standard") != 0 && std::strcmp(bcp, "search") != 0)
                                result.push_back(bcp);
                        }
                    }
                    return result;
                }
                if (aKey == "kn")
                    return { "false", "true" };
                if (aKey == "kf")
                    return { "false", "lower", "upper" };
                return {};
            }
        };


        value collator_constructor(value, arguments aArguments, value aNewTarget, function_object& aCallee)
        {
            auto const newTarget = is_undefined(aNewTarget) ? value_of(aCallee) : aNewTarget;
            auto const result = make_intl_object(newTarget, &realm::intlCollatorPrototype, object_class::IntlCollator);
            auto& c = static_cast<intl_object&>(obj(result));
            auto data = std::make_unique<collator_data>();
            // InitializeCollator
            auto const requested = canonicalize_locale_list(argument(aArguments, 0u));
            auto const options = coerce_options_to_object(argument(aArguments, 1u));
            data->usage = to_ascii(*get_string_option(options, "usage", { "sort", "search" }, "sort"));
            auto const matcher = *get_string_option(options, "localeMatcher", { "lookup", "best fit" }, "best fit");
            auto const collation = get_string_option(options, "collation", {});
            if (collation && !is_type_sequence(*collation))
                throw_range_error("Invalid collation : " + to_ascii(*collation));
            std::optional<std::string> numeric;
            if (auto const n = get_boolean_option(options, "numeric"))
                numeric = *n ? "true" : "false";
            std::optional<std::string> caseFirst;
            if (auto const cf = get_string_option(options, "caseFirst", { "upper", "lower", "false" }))
                caseFirst = to_ascii(*cf);
            std::vector<relevant_key> keys{ { "co", collation ? std::optional<std::string>{ to_ascii(*collation) } : std::nullopt },
                { "kf", caseFirst }, { "kn", numeric } };
            auto const r = resolve_locale(service::Collator, requested, matcher, keys, collator_locale_data{});
            c.locale = r.locale;
            data->collation = r.keywords.at("co").empty() ? std::string{ "default" } : r.keywords.at("co");
            data->numeric = r.keywords.at("kn") == "true";
            data->caseFirst = r.keywords.at("kf");
            // (ICU's collator: the data locale with the collation, or the search collation)
            std::string icu = icu_locale(r.dataLocale);
            if (data->usage == "search")
                icu += "@collation=search";
            else if (data->collation != "default")
                icu += "@collation=" + std::string{ uloc_toLegacyType("co", data->collation.c_str()) != nullptr ? uloc_toLegacyType("co", data->collation.c_str()) : data->collation.c_str() };
            UErrorCode status = U_ZERO_ERROR;
            data->collator.reset(ucol_open(icu.c_str(), &status));
            if (U_FAILURE(status))
            {
                status = U_ZERO_ERROR;
                data->collator.reset(ucol_open("", &status));
            }
            auto* ucol = data->collator.get();
            auto const sensitivity = get_string_option(options, "sensitivity", { "base", "accent", "case", "variant" });
            data->sensitivity = sensitivity ? to_ascii(*sensitivity) : std::string{ "variant" };
            status = U_ZERO_ERROR;
            bool const defaultIgnorePunctuation = ucol_getAttribute(ucol, UCOL_ALTERNATE_HANDLING, &status) == UCOL_SHIFTED;
            data->ignorePunctuation = *get_boolean_option(options, "ignorePunctuation", defaultIgnorePunctuation);
            // (the collator's attributes)
            status = U_ZERO_ERROR;
            ucol_setAttribute(ucol, UCOL_NUMERIC_COLLATION, data->numeric ? UCOL_ON : UCOL_OFF, &status);
            ucol_setAttribute(ucol, UCOL_NORMALIZATION_MODE, UCOL_ON, &status);     // (canonically equivalent strings are equal)
            if (data->caseFirst == "upper")
                ucol_setAttribute(ucol, UCOL_CASE_FIRST, UCOL_UPPER_FIRST, &status);
            else if (data->caseFirst == "lower")
                ucol_setAttribute(ucol, UCOL_CASE_FIRST, UCOL_LOWER_FIRST, &status);
            else
                ucol_setAttribute(ucol, UCOL_CASE_FIRST, UCOL_OFF, &status);
            if (data->sensitivity == "base")
                ucol_setStrength(ucol, UCOL_PRIMARY);
            else if (data->sensitivity == "accent")
                ucol_setStrength(ucol, UCOL_SECONDARY);
            else if (data->sensitivity == "case")
            {
                ucol_setStrength(ucol, UCOL_PRIMARY);
                ucol_setAttribute(ucol, UCOL_CASE_LEVEL, UCOL_ON, &status);
            }
            else
                ucol_setStrength(ucol, UCOL_TERTIARY);
            ucol_setAttribute(ucol, UCOL_ALTERNATE_HANDLING, data->ignorePunctuation ? UCOL_SHIFTED : UCOL_NON_IGNORABLE, &status);
            c.data = std::move(data);
            return result;
        }

        std::int32_t compare_strings(collator_data const& aCollator, value x, value y)
        {
            auto const& a = str(x);
            auto const& b = str(y);
            switch (ucol_strcoll(aCollator.collator.get(), uchars(a), ulength(a), uchars(b), ulength(b)))
            {
            case UCOL_LESS: return -1;
            case UCOL_GREATER: return 1;
            default: return 0;
            }
        }

        value collator_compare_function(value, arguments aArguments, value, function_object& aCallee)
        {
            auto& c = static_cast<intl_object&>(obj(aCallee.slots[0]));
            auto const x = to_string(argument(aArguments, 0u));
            auto const y = to_string(argument(aArguments, 1u));
            return number(compare_strings(data_of<collator_data>(c), x, y));
        }

        value collator_compare_getter(value aThis, arguments, value, function_object&)
        {
            auto& c = this_intl(aThis, object_class::IntlCollator, "get Intl.Collator.prototype.compare");
            if (is_undefined(c.bound))
            {
                auto const f = make_native(ascii(""), 2u, collator_compare_function);
                as_function(f)->slots[0] = value_of(c);
                c.bound = f;
            }
            return c.bound;
        }

        value collator_resolved_options(value aThis, arguments, value, function_object&)
        {
            auto& c = this_intl(aThis, object_class::IntlCollator, "Intl.Collator.prototype.resolvedOptions");
            auto const& d = data_of<collator_data>(c);
            auto const o = make_object();
            create_data_property_or_throw(o, ascii("locale"), u16_value(from_ascii(c.locale)));
            create_data_property_or_throw(o, ascii("usage"), u16_value(from_ascii(d.usage)));
            create_data_property_or_throw(o, ascii("sensitivity"), u16_value(from_ascii(d.sensitivity)));
            create_data_property_or_throw(o, ascii("ignorePunctuation"), boolean(d.ignorePunctuation));
            create_data_property_or_throw(o, ascii("collation"), u16_value(from_ascii(d.collation)));
            create_data_property_or_throw(o, ascii("numeric"), boolean(d.numeric));
            create_data_property_or_throw(o, ascii("caseFirst"), u16_value(from_ascii(d.caseFirst)));
            return o;
        }
    }

    value locale_compare(value aThis, value aThat, value aLocales, value aOptions)
    {
        // (String.prototype.localeCompare: a collator of the locales and options (the default one is made once))
        auto const& r = the_realm();
        value collator;
        if (is_undefined(aLocales) && is_undefined(aOptions))
        {
            if (sDefaultCollatorRealm != &r || is_undefined(sDefaultCollator))
            {
                sDefaultCollator = construct(r.intlCollator, {});
                sDefaultCollatorRealm = &r;
            }
            collator = sDefaultCollator;
        }
        else
        {
            value const arguments[] = { aLocales, aOptions };
            collator = construct(r.intlCollator, arguments);
        }
        return number(compare_strings(data_of<collator_data>(static_cast<intl_object&>(obj(collator))), aThis, aThat));
    }

    value to_locale_case(value aString, value aLocales, bool aUpper)
    {
        // TransformCase: the first requested locale's (or the default locale's) language, if its case mapping is language-sensitive
        auto const requested = canonicalize_locale_list(aLocales);
        auto locale = requested.empty() ? default_locale() : requested.front();
        if (auto parsed = parse_language_tag(locale))
            locale = parsed->language;
        if (locale != "az" && locale != "lt" && locale != "tr")
            locale = "und";
        auto const& text = str(aString);
        auto const icu = locale == "und" ? std::string{} : locale;
        auto const result = icu_string([&](UChar* aBuffer, std::int32_t aCapacity, UErrorCode* aStatus)
        {
            return aUpper ? u_strToUpper(aBuffer, aCapacity, uchars(text), ulength(text), icu.c_str(), aStatus) :
                u_strToLower(aBuffer, aCapacity, uchars(text), ulength(text), icu.c_str(), aStatus);
        });
        return u16_value(result);
    }

    void init_intl_collator(realm& aRealm, value)
    {
        auto const c = make_intl_constructor(aRealm, "Collator", 0u, collator_constructor, &realm::intlCollatorPrototype, "Intl.Collator");
        aRealm.intlCollator = c;
        auto const p = aRealm.intlCollatorPrototype;
        getter(p, ascii("compare"), collator_compare_getter);
        method(p, "resolvedOptions", 0u, collator_resolved_options);
    }
}

#endif
