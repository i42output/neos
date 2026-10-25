/*
  intl_number_format.cpp

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

// Intl.NumberFormat (ECMA-402 §16) and Intl.PluralRules (§17) with ICU's number formatter (number skeletons: unumf, unumrf) and
// plural rules (uplrules); Number.prototype.toLocaleString and BigInt.prototype.toLocaleString.

#include "intl.hpp"

#if defined(NEOS_JS_ICU)

#include <algorithm>
#include <cmath>
#include <cstring>
#include "intl_icu.hpp"

namespace neos::javascript_runtime::intl
{
    namespace
    {
        // --- Intl mathematical values (ToIntlMathematicalValue): a number, or an exact decimal (a BigInt's, a string's)

        struct intl_value
        {
            bool decimal = false;
            double number = 0.0;
            std::string text;           ///< (decimal: its digits ("-1.5e300"))
        public:
            bool is_nan() const { return !decimal && std::isnan(number); }
        };

        bool js_whitespace(char16_t c)
        {
            return c == u' ' || c == u'\t' || c == u'\n' || c == u'\r' || c == 0x0Bu || c == 0x0Cu || c == 0xA0u || c == 0xFEFFu || c == 0x1680u ||
                (c >= 0x2000u && c <= 0x200Au) || c == 0x2028u || c == 0x2029u || c == 0x202Fu || c == 0x205Fu || c == 0x3000u;
        }

        // (a StrDecimalLiteral (without its Infinity): its text, if it is one)
        bool decimal_literal(std::u16string_view aText)
        {
            std::size_t i = 0u;
            if (i < aText.size() && (aText[i] == u'+' || aText[i] == u'-'))
                ++i;
            bool digits = false;
            while (i < aText.size() && aText[i] >= u'0' && aText[i] <= u'9')
            {
                ++i;
                digits = true;
            }
            if (i < aText.size() && aText[i] == u'.')
            {
                ++i;
                while (i < aText.size() && aText[i] >= u'0' && aText[i] <= u'9')
                {
                    ++i;
                    digits = true;
                }
            }
            if (!digits)
                return false;
            if (i < aText.size() && (aText[i] == u'e' || aText[i] == u'E'))
            {
                ++i;
                if (i < aText.size() && (aText[i] == u'+' || aText[i] == u'-'))
                    ++i;
                bool exponent = false;
                while (i < aText.size() && aText[i] >= u'0' && aText[i] <= u'9')
                {
                    ++i;
                    exponent = true;
                }
                if (!exponent)
                    return false;
            }
            return i == aText.size();
        }

        intl_value to_intl_mathematical_value(value aValue)
        {
            intl_value result;
            auto const primitive = to_primitive(aValue, hint::Number);
            if (is_bigint(primitive))
            {
                result.decimal = true;
                result.text = to_ascii(bigint_to_string(primitive));
                return result;
            }
            if (!is_string(primitive))
            {
                result.number = to_number(primitive);
                return result;
            }
            std::u16string_view text = str(primitive);
            while (!text.empty() && js_whitespace(text.front()))
                text.remove_prefix(1u);
            while (!text.empty() && js_whitespace(text.back()))
                text.remove_suffix(1u);
            if (text.empty())
            {
                result.number = 0.0;
                return result;
            }
            if (text == u"Infinity" || text == u"+Infinity" || text == u"-Infinity")
            {
                result.number = text.front() == u'-' ? -INFINITY : INFINITY;
                return result;
            }
            if (decimal_literal(text))
            {
                // (exactly: the decimal (a leading + dropped; ICU keeps -0))
                result.decimal = true;
                result.text = to_ascii(text.front() == u'+' ? text.substr(1u) : text);
                if (result.text.front() == '.' || (result.text.size() > 1u && result.text[0] == '-' && result.text[1] == '.'))
                    result.text.insert(result.text.front() == '-' ? 1u : 0u, "0");
                if (result.text.back() == '.')
                    result.text.pop_back();
                return result;
            }
            if (text.size() > 2u && text[0] == u'0' && (text[1] == u'x' || text[1] == u'X' || text[1] == u'o' || text[1] == u'O' || text[1] == u'b' ||
                text[1] == u'B'))
            {
                // (a non-decimal integer: exactly, as a BigInt would be)
                auto const n = string_to_number(text);
                if (std::isnan(n))
                {
                    result.number = n;
                    return result;
                }
                result.decimal = true;
                result.text = to_ascii(bigint_to_string(to_bigint(make_string(text))));
                return result;
            }
            result.number = NAN;
            return result;
        }

        // --- the formatter

        struct number_format_data : intl_data
        {
            std::string dataLocale;
            std::string numberingSystem;
            std::string style = "decimal";
            std::string currency;
            std::string currencyDisplay;
            std::string currencySign;
            std::string unit;
            std::string unitDisplay;
            std::string notation = "standard";
            std::string compactDisplay;
            std::string useGrouping;            ///< ("false": false)
            std::string signDisplay = "auto";
            digit_options digits;
            number_formatter_ptr formatter;
#if defined(NEOS_JS_ICU_NUMBER_RANGES)
            number_range_formatter_ptr rangeFormatter;
#endif
            std::u16string skeleton;
        };

        std::string sign_skeleton(number_format_data const& d)
        {
            bool const accounting = d.style == "currency" && d.currencySign == "accounting";
#if !defined(NEOS_JS_WINDOWS_ICU) && U_ICU_VERSION_MAJOR_NUM < 69
            // (before ICU 69 there is no sign-negative: auto (only negative zero differs))
            if (d.signDisplay == "negative")
                return accounting ? "sign-accounting" : std::string{};
#endif
            if (d.signDisplay == "never")
                return "sign-never";
            if (accounting)
            {
                if (d.signDisplay == "always")
                    return "sign-accounting-always";
                if (d.signDisplay == "exceptZero")
                    return "sign-accounting-except-zero";
                if (d.signDisplay == "negative")
                    return "sign-accounting-negative";
                return "sign-accounting";
            }
            if (d.signDisplay == "always")
                return "sign-always";
            if (d.signDisplay == "exceptZero")
                return "sign-except-zero";
            if (d.signDisplay == "negative")
                return "sign-negative";
            return {};
        }

        std::string number_skeleton(number_format_data const& d)
        {
            std::string s;
            auto const add = [&](std::string const& aStem)
            {
                if (aStem.empty())
                    return;
                if (!s.empty())
                    s += " ";
                s += aStem;
            };
            if (d.style == "percent")
                add("percent scale/100");
            else if (d.style == "currency")
            {
                add("currency/" + d.currency);
                if (d.currencyDisplay == "code")
                    add("unit-width-iso-code");
                else if (d.currencyDisplay == "narrowSymbol")
                    add("unit-width-narrow");
                else if (d.currencyDisplay == "name")
                    add("unit-width-full-name");
            }
            else if (d.style == "unit")
            {
                add("unit/" + d.unit);
                if (d.unitDisplay == "narrow")
                    add("unit-width-narrow");
                else if (d.unitDisplay == "long")
                    add("unit-width-full-name");
                else
                    add("unit-width-short");
            }
            if (d.notation == "scientific")
                add("scientific");
            else if (d.notation == "engineering")
                add("engineering");
            else if (d.notation == "compact")
                add(d.compactDisplay == "long" ? "compact-long" : "compact-short");
            add(digit_options_skeleton(d.digits));
            if (d.useGrouping == "false")
                add("group-off");
            else if (d.useGrouping == "min2")
                add("group-min2");
            else if (d.useGrouping == "always")
                add("group-on-aligned");
            else
                add("group-auto");
            add(sign_skeleton(d));
            add("numbering-system/" + d.numberingSystem);
            return s;
        }

        struct number_locale_data : locale_data
        {
            std::vector<std::string> values(std::string const& aDataLocale, std::string_view aKey) const override
            {
                if (aKey != "nu")
                    return {};
                return { default_numbering_system(aDataLocale) };
            }
            bool supports(std::string const& aDataLocale, std::string_view aKey, std::string const& aValue) const override
            {
                if (aKey != "nu")
                    return false;
                return is_supported_numbering_system(aDataLocale, aValue);
            }
        };


        int currency_digits(std::string const& aCurrency)
        {
            auto const code = from_ascii(aCurrency);
            UErrorCode status = U_ZERO_ERROR;
            auto const digits = ucurr_getDefaultFractionDigits(uchars(code), &status);
            return U_SUCCESS(status) ? digits : 2;
        }

        // (GetBooleanOrStringNumberFormatOption of useGrouping)
        std::string use_grouping_option(value aOptions, std::string const& aFallback)
        {
            auto const v = get(aOptions, ascii("useGrouping"));
            if (is_undefined(v))
                return aFallback;
            if (v == True)
                return "always";
            if (!to_boolean(v))
                return "false";
            auto const text = to_ascii(str(to_string(v)));
            if (text == "true" || text == "false")
                return aFallback;
            if (text != "min2" && text != "auto" && text != "always")
                throw_range_error("Value " + text + " out of range for Intl.NumberFormat options property useGrouping");
            return text;
        }

        void make_formatter(number_format_data& d)
        {
            auto const skeleton = number_skeleton(d);
            d.skeleton = from_ascii(skeleton);
            auto const icu = icu_locale(d.dataLocale);
            UErrorCode status = U_ZERO_ERROR;
            UParseError error;
            d.formatter.reset(unumf_openForSkeletonAndLocaleWithError(uchars(d.skeleton), ulength(d.skeleton), icu.c_str(), &error, &status));
            if (U_FAILURE(status))
                throw_range_error("Intl.NumberFormat: unsupported options (" + skeleton + ")");
        }

        // InitializeNumberFormat
        void initialize_number_format(intl_object& aObject, value aLocales, value aOptions)
        {
            auto data = std::make_unique<number_format_data>();
            auto& d = *data;
            auto const requested = canonicalize_locale_list(aLocales);
            auto const options = coerce_options_to_object(aOptions);
            auto const matcher = *get_string_option(options, "localeMatcher", { "lookup", "best fit" }, "best fit");
            auto const numberingSystem = get_string_option(options, "numberingSystem", {});
            if (numberingSystem && !is_type_sequence(*numberingSystem))
                throw_range_error("Invalid numberingSystem : " + to_ascii(*numberingSystem));
            std::vector<relevant_key> keys{ { "nu", numberingSystem ? std::optional<std::string>{ to_ascii(*numberingSystem) } : std::nullopt } };
            auto const r = resolve_locale(service::NumberFormat, requested, matcher, keys, number_locale_data{});
            aObject.locale = r.locale;
            d.dataLocale = r.dataLocale;
            d.numberingSystem = r.keywords.at("nu");
            // SetNumberFormatUnitOptions
            d.style = to_ascii(*get_string_option(options, "style", { "decimal", "percent", "currency", "unit" }, "decimal"));
            auto const currency = get_string_option(options, "currency", {});
            if (!currency)
            {
                if (d.style == "currency")
                    throw_type_error("Currency code is required with currency style.");
            }
            else if (!is_well_formed_currency_code(*currency))
                throw_range_error("Invalid currency code : " + to_ascii(*currency));
            auto const currencyDisplay = to_ascii(*get_string_option(options, "currencyDisplay", { "code", "symbol", "narrowSymbol", "name" }, "symbol"));
            auto const currencySign = to_ascii(*get_string_option(options, "currencySign", { "standard", "accounting" }, "standard"));
            auto const unit = get_string_option(options, "unit", {});
            if (!unit)
            {
                if (d.style == "unit")
                    throw_type_error("Unit is required with unit style.");
            }
            else if (!is_well_formed_unit_identifier(*unit))
                throw_range_error("Invalid unit argument for Intl.NumberFormat() '" + to_ascii(*unit) + "'");
            auto const unitDisplay = to_ascii(*get_string_option(options, "unitDisplay", { "short", "narrow", "long" }, "short"));
            if (d.style == "currency")
            {
                d.currency = to_ascii(*currency);
                for (auto& c : d.currency)
                    c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
                d.currencyDisplay = currencyDisplay;
                d.currencySign = currencySign;
            }
            if (d.style == "unit")
            {
                d.unit = to_ascii(*unit);
                d.unitDisplay = unitDisplay;
            }
            d.notation = to_ascii(*get_string_option(options, "notation", { "standard", "scientific", "engineering", "compact" }, "standard"));
            int mnfdDefault = 0;
            int mxfdDefault = 3;
            if (d.style == "currency" && d.notation == "standard")
                mnfdDefault = mxfdDefault = currency_digits(d.currency);
            else if (d.style == "percent")
                mxfdDefault = 0;
            set_number_format_digit_options(d.digits, options, mnfdDefault, mxfdDefault, d.notation);
            auto const compactDisplay = to_ascii(*get_string_option(options, "compactDisplay", { "short", "long" }, "short"));
            auto const defaultUseGrouping = d.notation == "compact" ? std::string{ "min2" } : std::string{ "auto" };
            d.useGrouping = use_grouping_option(options, defaultUseGrouping);
            if (d.notation == "compact")
                d.compactDisplay = compactDisplay;
            d.signDisplay = to_ascii(*get_string_option(options, "signDisplay", { "auto", "never", "always", "exceptZero", "negative" }, "auto"));
            make_formatter(d);
            aObject.data = std::move(data);
        }

        // (%Intl%.[[FallbackSymbol]]: the legacy constructor's (ChainNumberFormat, UnwrapNumberFormat))
        value number_format_constructor(value aThis, arguments aArguments, value aNewTarget, function_object& aCallee)
        {
            auto const newTarget = is_undefined(aNewTarget) ? value_of(aCallee) : aNewTarget;
            auto const result = make_intl_object(newTarget, &realm::intlNumberFormatPrototype, object_class::IntlNumberFormat);
            initialize_number_format(static_cast<intl_object&>(obj(result)), argument(aArguments, 0u), argument(aArguments, 1u));
            auto const& r = the_realm();
            if (is_undefined(aNewTarget) && is_object(aThis) && ordinary_has_instance(r.intlNumberFormat, aThis))
            {
                define_property_or_throw(aThis, r.intlFallbackSymbol, property_descriptor::data(result, 0u));
                return aThis;
            }
            return result;
        }

        intl_object& unwrap_number_format(value aThis, char const* aMethod)
        {
            auto const& r = the_realm();
            if (!is_object(aThis))
                throw_type_error(std::string{ "Method " } + aMethod + " called on incompatible receiver");
            auto value = aThis;
            if (as_intl(aThis, object_class::IntlNumberFormat) == nullptr && ordinary_has_instance(r.intlNumberFormat, aThis))
                value = get(aThis, r.intlFallbackSymbol);
            return this_intl(value, object_class::IntlNumberFormat, aMethod);
        }

        formatted_number_ptr format_value(number_format_data const& d, intl_value const& x)
        {
            UErrorCode status = U_ZERO_ERROR;
            formatted_number_ptr result{ unumf_openResult(&status) };
            if (x.decimal)
                unumf_formatDecimal(d.formatter.get(), x.text.c_str(), static_cast<std::int32_t>(x.text.size()), result.get(), &status);
            else
                unumf_formatDouble(d.formatter.get(), x.number, result.get(), &status);
            if (U_FAILURE(status))
                throw_range_error("Intl.NumberFormat: the number cannot be formatted");
            return result;
        }

        std::u16string format_numeric(number_format_data const& d, intl_value const& x)
        {
            auto const result = format_value(d, x);
            UErrorCode status = U_ZERO_ERROR;
            return formatted_value_string(unumf_resultAsValue(result.get(), &status));
        }

        struct number_part_context
        {
            intl_value const* x;            ///< (the value formatted (nan, infinity))
            intl_value const* y = nullptr;  ///< (a range's end)
            bool unitStyle = false;         ///< (a percent sign is the unit's)
        };

        std::optional<std::u16string> number_field_type(field_span const& aSpan, std::u16string_view aText, void* aContext)
        {
            auto const& c = *static_cast<number_part_context*>(aContext);
            if (aSpan.category != UFIELD_CATEGORY_NUMBER)
                return {};
            bool nan = c.x->is_nan();
            bool infinite = !c.x->decimal && std::isinf(c.x->number);
            if (c.y != nullptr)
            {
                nan = nan || c.y->is_nan();
                infinite = infinite || (!c.y->decimal && std::isinf(c.y->number));
            }
            switch (aSpan.field)
            {
            case UNUM_INTEGER_FIELD:
                {
                    auto const text = aText.substr(static_cast<std::size_t>(aSpan.begin), static_cast<std::size_t>(aSpan.end - aSpan.begin));
                    if (nan && text.find(u'∞') == std::u16string_view::npos && !text.empty() && !(text[0] >= u'0' && text[0] <= u'9'))
                        return u"nan";
                    if (infinite && text.find(u'∞') != std::u16string_view::npos)
                        return u"infinity";
                    return u"integer";
                }
            case UNUM_FRACTION_FIELD: return u"fraction";
            case UNUM_DECIMAL_SEPARATOR_FIELD: return u"decimal";
            case UNUM_EXPONENT_SYMBOL_FIELD: return u"exponentSeparator";
            case UNUM_EXPONENT_SIGN_FIELD: return u"exponentMinusSign";
            case UNUM_EXPONENT_FIELD: return u"exponentInteger";
            case UNUM_GROUPING_SEPARATOR_FIELD: return u"group";
            case UNUM_CURRENCY_FIELD: return u"currency";
            case UNUM_PERCENT_FIELD: return c.unitStyle ? u"unit" : u"percentSign";
            case UNUM_PERMILL_FIELD: return u"unknown";
            case UNUM_SIGN_FIELD:
                {
                    auto const text = aText.substr(static_cast<std::size_t>(aSpan.begin), static_cast<std::size_t>(aSpan.end - aSpan.begin));
                    return text.find(u'+') != std::u16string_view::npos ? std::u16string{ u"plusSign" } : std::u16string{ u"minusSign" };
                }
            case UNUM_MEASURE_UNIT_FIELD: return u"unit";
            case UNUM_COMPACT_FIELD: return u"compact";
#if !defined(NEOS_JS_WINDOWS_ICU) && U_ICU_VERSION_MAJOR_NUM >= 71
            case UNUM_APPROXIMATELY_SIGN_FIELD: return u"approximatelySign";
#endif
            default: return u"unknown";
            }
        }

        std::vector<part> format_numeric_to_parts(number_format_data const& d, intl_value const& x)
        {
            auto const result = format_value(d, x);
            UErrorCode status = U_ZERO_ERROR;
            auto const* v = unumf_resultAsValue(result.get(), &status);
            auto const text = formatted_value_string(v);
            auto spans = formatted_value_spans(v);
            // (NaN's text has no field of its own: it is the integer's)
            if (x.is_nan() && spans.empty() && !text.empty())
                spans.push_back({ UFIELD_CATEGORY_NUMBER, UNUM_INTEGER_FIELD, 0, static_cast<std::int32_t>(text.size()) });
            number_part_context context{ &x, nullptr, d.style == "unit" };
            return make_parts(text, spans, number_field_type, &context);
        }

        // --- ranges

#if defined(NEOS_JS_ICU_NUMBER_RANGES)
        number_range_formatter_ptr& range_formatter(number_format_data& d)
        {
            if (!d.rangeFormatter)
            {
                auto const icu = icu_locale(d.dataLocale);
                UErrorCode status = U_ZERO_ERROR;
                UParseError error;
                d.rangeFormatter.reset(unumrf_openForSkeletonWithCollapseAndIdentityFallback(uchars(d.skeleton), ulength(d.skeleton),
                    UNUM_RANGE_COLLAPSE_AUTO, UNUM_IDENTITY_FALLBACK_APPROXIMATELY, icu.c_str(), &error, &status));
                if (U_FAILURE(status))
                    throw_range_error("Intl.NumberFormat: unsupported options for a range");
            }
            return d.rangeFormatter;
        }

        formatted_number_range_ptr format_range_value(number_format_data& d, intl_value const& x, intl_value const& y)
        {
            if (x.is_nan() || y.is_nan())
                throw_range_error("Intl.NumberFormat.prototype.formatRange: a NaN cannot be formatted as a range");
            auto& f = range_formatter(d);
            UErrorCode status = U_ZERO_ERROR;
            formatted_number_range_ptr result{ unumrf_openResult(&status) };
            auto const decimal = [](intl_value const& v)
            {
                if (v.decimal)
                    return v.text;
                if (std::isinf(v.number))
                    return std::string{ v.number < 0 ? "-Infinity" : "Infinity" };
                char buffer[64];
                std::snprintf(buffer, sizeof buffer, "%.17g", v.number);
                std::string s{ buffer };
                if (v.number == 0.0 && std::signbit(v.number))
                    s = "-0";
                return s;
            };
            auto const a = decimal(x);
            auto const b = decimal(y);
            if ((!x.decimal && std::isinf(x.number)) || (!y.decimal && std::isinf(y.number)))
                unumrf_formatDoubleRange(f.get(), x.decimal ? std::strtod(x.text.c_str(), nullptr) : x.number,
                    y.decimal ? std::strtod(y.text.c_str(), nullptr) : y.number, result.get(), &status);
            else
                unumrf_formatDecimalRange(f.get(), a.c_str(), static_cast<std::int32_t>(a.size()), b.c_str(), static_cast<std::int32_t>(b.size()),
                    result.get(), &status);
            if (U_FAILURE(status))
                throw_range_error("Intl.NumberFormat: the range cannot be formatted");
            return result;
        }

        std::vector<part> format_range_to_parts(number_format_data& d, intl_value const& x, intl_value const& y)
        {
            auto const result = format_range_value(d, x, y);
            UErrorCode status = U_ZERO_ERROR;
            auto const* v = unumrf_resultAsValue(result.get(), &status);
            auto const text = formatted_value_string(v);
            auto const spans = formatted_value_spans(v);
            number_part_context context{ &x, &y, d.style == "unit" };
            auto parts = make_parts(text, spans, number_field_type, &context);
            // (each part's source: the range span (0 start, 1 end) that contains it, else shared)
            std::int32_t offset = 0;
            for (auto& p : parts)
            {
                std::u16string source = u"shared";
                for (auto const& s : spans)
                    if (s.category == UFIELD_CATEGORY_NUMBER_RANGE_SPAN && offset >= s.begin && offset < s.end)
                        source = s.field == 0 ? u"startRange" : u"endRange";
                p.source = source;
                offset += static_cast<std::int32_t>(p.text.size());
            }
            return parts;
        }
#else
        // (without ICU's number range formatter (before ICU 69): PartitionNumberRangePattern with the start's and the end's parts, the
        // en dash as the range separator and "~" as the approximately sign (FormatApproximately))
        std::vector<part> format_range_to_parts(number_format_data& d, intl_value const& x, intl_value const& y)
        {
            if (x.is_nan() || y.is_nan())
                throw_range_error("Intl.NumberFormat.prototype.formatRange: a NaN cannot be formatted as a range");
            auto parts = format_numeric_to_parts(d, x);
            auto const end = format_numeric_to_parts(d, y);
            auto const text = [](std::vector<part> const& aParts)
            {
                std::u16string result;
                for (auto const& p : aParts)
                    result += p.text;
                return result;
            };
            if (text(parts) == text(end))
            {
                for (auto& p : parts)
                    p.source = u"shared";
                parts.insert(parts.begin(), part{ u"approximatelySign", u"~", u"shared" });
                return parts;
            }
            // (spaced when either end has an affix (e.g. "$3 – $5"), as ICU's range formatter does)
            auto const affixed = [](std::vector<part> const& aParts)
            {
                return std::any_of(aParts.begin(), aParts.end(), [](part const& p)
                    { return p.type != u"integer" && p.type != u"group" && p.type != u"decimal" && p.type != u"fraction"; });
            };
            bool const spaced = affixed(parts) || affixed(end);
            for (auto& p : parts)
                p.source = u"startRange";
            parts.push_back(part{ u"literal", spaced ? u" \u2013 " : u"\u2013", u"shared" });
            for (auto p : end)
            {
                p.source = u"endRange";
                parts.push_back(std::move(p));
            }
            return parts;
        }
#endif

        // --- the prototype's functions

        value number_format_function(value, arguments aArguments, value, function_object& aCallee)
        {
            auto& nf = static_cast<intl_object&>(obj(aCallee.slots[0]));
            auto const x = to_intl_mathematical_value(argument(aArguments, 0u));
            return u16_value(format_numeric(data_of<number_format_data>(nf), x));
        }

        value number_format_format_getter(value aThis, arguments, value, function_object&)
        {
            auto& nf = unwrap_number_format(aThis, "get Intl.NumberFormat.prototype.format");
            if (is_undefined(nf.bound))
            {
                auto const f = make_native(ascii(""), 1u, number_format_function);
                as_function(f)->slots[0] = value_of(nf);
                nf.bound = f;
            }
            return nf.bound;
        }

        value number_format_format_to_parts(value aThis, arguments aArguments, value, function_object&)
        {
            auto& nf = this_intl(aThis, object_class::IntlNumberFormat, "Intl.NumberFormat.prototype.formatToParts");
            auto const x = to_intl_mathematical_value(argument(aArguments, 0u));
            return parts_array(format_numeric_to_parts(data_of<number_format_data>(nf), x));
        }

        value number_format_format_range(value aThis, arguments aArguments, value, function_object& aCallee)
        {
            auto& nf = this_intl(aThis, object_class::IntlNumberFormat, aCallee.nativeId == 0u ? "Intl.NumberFormat.prototype.formatRange" :
                "Intl.NumberFormat.prototype.formatRangeToParts");
            auto const start = argument(aArguments, 0u);
            auto const end = argument(aArguments, 1u);
            if (is_undefined(start) || is_undefined(end))
                throw_type_error("start and end must be defined");
            auto const x = to_intl_mathematical_value(start);
            auto const y = to_intl_mathematical_value(end);
            auto& d = data_of<number_format_data>(nf);
            if (aCallee.nativeId == 0u)
            {
#if defined(NEOS_JS_ICU_NUMBER_RANGES)
                auto const result = format_range_value(d, x, y);
                UErrorCode status = U_ZERO_ERROR;
                return u16_value(formatted_value_string(unumrf_resultAsValue(result.get(), &status)));
#else
                std::u16string text;
                for (auto const& p : format_range_to_parts(d, x, y))
                    text += p.text;
                return u16_value(text);
#endif
            }
            return parts_array(format_range_to_parts(d, x, y));
        }

        value number_format_resolved_options(value aThis, arguments, value, function_object&)
        {
            auto& nf = unwrap_number_format(aThis, "Intl.NumberFormat.prototype.resolvedOptions");
            auto const& d = data_of<number_format_data>(nf);
            auto const o = make_object();
            auto const put = [&](char const* aName, std::string const& aValue)
            {
                create_data_property_or_throw(o, ascii(aName), u16_value(from_ascii(aValue)));
            };
            put("locale", nf.locale);
            put("numberingSystem", d.numberingSystem);
            put("style", d.style);
            if (d.style == "currency")
            {
                put("currency", d.currency);
                put("currencyDisplay", d.currencyDisplay);
                put("currencySign", d.currencySign);
            }
            if (d.style == "unit")
            {
                put("unit", d.unit);
                put("unitDisplay", d.unitDisplay);
            }
            digit_options_resolved(d.digits, o, false);
            if (d.useGrouping == "false")
                create_data_property_or_throw(o, ascii("useGrouping"), False);
            else
                put("useGrouping", d.useGrouping);
            put("notation", d.notation);
            if (d.notation == "compact")
                put("compactDisplay", d.compactDisplay);
            put("signDisplay", d.signDisplay);
            digit_options_rounding_resolved(d.digits, o);
            return o;
        }

        // --- Intl.PluralRules

        struct plural_rules_data : intl_data
        {
            std::string dataLocale;
            std::string type;
            std::string notation = "standard";
            std::string compactDisplay;
            digit_options digits;
            plural_rules_ptr rules;
            number_formatter_ptr formatter;
#if defined(NEOS_JS_ICU_NUMBER_RANGES)
            number_range_formatter_ptr rangeFormatter;
#endif
            std::u16string skeleton;
        };

        struct no_locale_data : locale_data
        {
            std::vector<std::string> values(std::string const&, std::string_view) const override { return {}; }
        };

        value plural_rules_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (is_undefined(aNewTarget))
                throw_type_error("Constructor Intl.PluralRules requires 'new'");
            auto const result = make_intl_object(aNewTarget, &realm::intlPluralRulesPrototype, object_class::IntlPluralRules);
            auto& pr = static_cast<intl_object&>(obj(result));
            auto data = std::make_unique<plural_rules_data>();
            auto& d = *data;
            auto const requested = canonicalize_locale_list(argument(aArguments, 0u));
            auto const options = coerce_options_to_object(argument(aArguments, 1u));
            auto const matcher = *get_string_option(options, "localeMatcher", { "lookup", "best fit" }, "best fit");
            d.type = to_ascii(*get_string_option(options, "type", { "cardinal", "ordinal" }, "cardinal"));
            d.notation = to_ascii(*get_string_option(options, "notation", { "standard", "scientific", "engineering", "compact" }, "standard"));
            auto const compactDisplay = to_ascii(*get_string_option(options, "compactDisplay", { "short", "long" }, "short"));
            if (d.notation == "compact")
                d.compactDisplay = compactDisplay;
            set_number_format_digit_options(d.digits, options, 0, 3, d.notation);
            auto const r = resolve_locale(service::PluralRules, requested, matcher, {}, no_locale_data{});
            pr.locale = r.locale;
            d.dataLocale = r.dataLocale;
            auto const icu = icu_locale(d.dataLocale);
            UErrorCode status = U_ZERO_ERROR;
            d.rules.reset(uplrules_openForType(icu.c_str(), d.type == "ordinal" ? UPLURAL_TYPE_ORDINAL : UPLURAL_TYPE_CARDINAL, &status));
            std::string skeleton = digit_options_skeleton(d.digits);
            if (d.notation == "scientific")
                skeleton += " scientific";
            else if (d.notation == "engineering")
                skeleton += " engineering";
            else if (d.notation == "compact")
                skeleton += d.compactDisplay == "long" ? " compact-long" : " compact-short";
            d.skeleton = from_ascii(skeleton);
            UParseError error;
            status = U_ZERO_ERROR;
            d.formatter.reset(unumf_openForSkeletonAndLocaleWithError(uchars(d.skeleton), ulength(d.skeleton), icu.c_str(), &error, &status));
            if (U_FAILURE(status))
                throw_range_error("Intl.PluralRules: unsupported options");
            pr.data = std::move(data);
            return result;
        }

        std::u16string resolve_plural(plural_rules_data const& d, double n)
        {
            if (!std::isfinite(n))
                return u"other";
            UErrorCode status = U_ZERO_ERROR;
            formatted_number_ptr formatted{ unumf_openResult(&status) };
            unumf_formatDouble(d.formatter.get(), n, formatted.get(), &status);
            return icu_string([&](UChar* aBuffer, std::int32_t aCapacity, UErrorCode* aStatus)
                { return uplrules_selectFormatted(d.rules.get(), formatted.get(), aBuffer, aCapacity, aStatus); });
        }

        value plural_rules_select(value aThis, arguments aArguments, value, function_object&)
        {
            auto& pr = this_intl(aThis, object_class::IntlPluralRules, "Intl.PluralRules.prototype.select");
            auto const n = to_number(argument(aArguments, 0u));
            return u16_value(resolve_plural(data_of<plural_rules_data>(pr), n));
        }

        value plural_rules_select_range(value aThis, arguments aArguments, value, function_object&)
        {
            auto& pr = this_intl(aThis, object_class::IntlPluralRules, "Intl.PluralRules.prototype.selectRange");
            auto const start = argument(aArguments, 0u);
            auto const end = argument(aArguments, 1u);
            if (is_undefined(start) || is_undefined(end))
                throw_type_error("start and end must be defined");
            auto const x = to_number(start);
            auto const y = to_number(end);
            if (std::isnan(x) || std::isnan(y))
                throw_range_error("Intl.PluralRules.prototype.selectRange: a NaN is not a range's end");
            auto& d = data_of<plural_rules_data>(pr);
#if defined(NEOS_JS_ICU_NUMBER_RANGES) && !defined(NEOS_JS_WINDOWS_ICU)
            if (!d.rangeFormatter)
            {
                auto const icu = icu_locale(d.dataLocale);
                UErrorCode status = U_ZERO_ERROR;
                UParseError error;
                d.rangeFormatter.reset(unumrf_openForSkeletonWithCollapseAndIdentityFallback(uchars(d.skeleton), ulength(d.skeleton),
                    UNUM_RANGE_COLLAPSE_AUTO, UNUM_IDENTITY_FALLBACK_APPROXIMATELY, icu.c_str(), &error, &status));
            }
            if (d.rangeFormatter && std::isfinite(x) && std::isfinite(y))
            {
                UErrorCode status = U_ZERO_ERROR;
                formatted_number_range_ptr formatted{ unumrf_openResult(&status) };
                unumrf_formatDoubleRange(d.rangeFormatter.get(), x, y, formatted.get(), &status);
                auto const category = icu_string([&](UChar* aBuffer, std::int32_t aCapacity, UErrorCode* aStatus)
                    { return uplrules_selectForRange(d.rules.get(), formatted.get(), aBuffer, aCapacity, aStatus); });
                if (U_SUCCESS(status) && !category.empty())
                    return u16_value(category);
            }
#endif
            // (without ICU's plural ranges: the end's category)
            return u16_value(resolve_plural(d, y));
        }

        value plural_rules_resolved_options(value aThis, arguments, value, function_object&)
        {
            auto& pr = this_intl(aThis, object_class::IntlPluralRules, "Intl.PluralRules.prototype.resolvedOptions");
            auto const& d = data_of<plural_rules_data>(pr);
            auto const o = make_object();
            create_data_property_or_throw(o, ascii("locale"), u16_value(from_ascii(pr.locale)));
            create_data_property_or_throw(o, ascii("type"), u16_value(from_ascii(d.type)));
            create_data_property_or_throw(o, ascii("notation"), u16_value(from_ascii(d.notation)));
            if (d.notation == "compact")
                create_data_property_or_throw(o, ascii("compactDisplay"), u16_value(from_ascii(d.compactDisplay)));
            digit_options_resolved(d.digits, o, false);
            // (the plural categories, in the order zero, one, two, few, many, other)
            std::vector<std::string> keywords;
            UErrorCode status = U_ZERO_ERROR;
            enumeration_ptr e{ uplrules_getKeywords(d.rules.get(), &status) };
            std::int32_t length = 0;
            while (char const* k = uenum_next(e.get(), &length, &status))
            {
                if (U_FAILURE(status))
                    break;
                keywords.emplace_back(k, static_cast<std::size_t>(length));
            }
            std::vector<value> categories;
            for (auto const* c : { "zero", "one", "two", "few", "many", "other" })
                if (std::find(keywords.begin(), keywords.end(), c) != keywords.end())
                    categories.push_back(u16_value(from_ascii(c)));
            create_data_property_or_throw(o, ascii("pluralCategories"), create_array_from_list(categories));
            digit_options_rounding_resolved(d.digits, o);
            return o;
        }
    }

    std::vector<part> format_decimal_to_parts(value aNumberFormat, std::string const& aDecimal)
    {
        // (PartitionNumberPattern of an exact decimal ("-0": negative zero))
        intl_value x;
        if (aDecimal == "0" || aDecimal == "-0")
            x.number = aDecimal[0] == '-' ? -0.0 : 0.0;
        else
        {
            x.decimal = true;
            x.text = aDecimal;
        }
        return format_numeric_to_parts(data_of<number_format_data>(static_cast<intl_object&>(obj(aNumberFormat))), x);
    }

    value number_to_locale_string(value aNumber, value aLocales, value aOptions)
    {
        auto const& r = the_realm();
        value const arguments[] = { aLocales, aOptions };
        auto const nf = construct(r.intlNumberFormat, arguments);
        intl_value x;
        if (is_bigint(aNumber))
        {
            x.decimal = true;
            x.text = to_ascii(bigint_to_string(aNumber));
        }
        else
            x.number = to_number(aNumber);
        return u16_value(format_numeric(data_of<number_format_data>(static_cast<intl_object&>(obj(nf))), x));
    }

    void init_intl_number_format(realm& aRealm, value)
    {
        if (is_undefined(aRealm.intlFallbackSymbol))
            aRealm.intlFallbackSymbol = make_symbol(ascii("IntlLegacyConstructedSymbol"));
        auto const c = make_intl_constructor(aRealm, "NumberFormat", 0u, number_format_constructor, &realm::intlNumberFormatPrototype,
            "Intl.NumberFormat");
        aRealm.intlNumberFormat = c;
        auto const p = aRealm.intlNumberFormatPrototype;
        getter(p, ascii("format"), number_format_format_getter);
        method(p, "formatToParts", 1u, number_format_format_to_parts);
        as_function(method(p, "formatRange", 2u, number_format_format_range))->nativeId = 0u;
        as_function(method(p, "formatRangeToParts", 2u, number_format_format_range))->nativeId = 1u;
        method(p, "resolvedOptions", 0u, number_format_resolved_options);
    }

    void init_intl_plural_rules(realm& aRealm, value)
    {
        make_intl_constructor(aRealm, "PluralRules", 0u, plural_rules_constructor, &realm::intlPluralRulesPrototype, "Intl.PluralRules");
        auto const p = aRealm.intlPluralRulesPrototype;
        method(p, "select", 1u, plural_rules_select);
        method(p, "selectRange", 2u, plural_rules_select_range);
        method(p, "resolvedOptions", 0u, plural_rules_resolved_options);
    }
}

#endif
