/*
  builtins_basic.cpp

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

// Symbol, Boolean, Number, Math, the global functions (parseInt, isNaN, URI functions, ...) and the iterator prototypes
// (%IteratorPrototype%, %AsyncIteratorPrototype% and %ArrayIteratorPrototype%)

#include <algorithm>
#include <chrono>
#include <random>
#include <neos/language/ecmascript.hpp>
#include "js.hpp"
#include "intl.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace es = neos::ecmascript;

    // --- Symbol

    namespace
    {
        value symbol_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (aNewTarget != Undefined)
                throw_type_error("Symbol is not a constructor");
            auto const description = argument(aArguments, 0u);
            return make_symbol(description == Undefined ? Undefined : to_string(description));
        }

        value symbol_for(value, arguments aArguments, value, function_object&)
        {
            auto const key = to_string(argument(aArguments, 0u));
            auto& registry = the_realm().registry;
            auto const& text = str(key);
            if (auto const existing = registry.find(std::u16string{ text }); existing != registry.end())
                return existing->second;
            auto const s = make_symbol(key);
            cell_of<symbol>(s).registered = true;
            registry.emplace(text, s);
            return s;
        }

        value symbol_key_for(value, arguments aArguments, value, function_object&)
        {
            auto const s = argument(aArguments, 0u);
            if (!is_symbol(s))
                throw_type_error(describe(s) + " is not a symbol");
            if (cell_of<symbol>(s).registered)
                return cell_of<symbol>(s).description;
            return Undefined;
        }

        value this_symbol(value aThis)
        {
            if (is_symbol(aThis))
                return aThis;
            if (is_object(aThis) && obj(aThis).cls == object_class::Symbol)
                return obj(aThis).internal;
            throw_type_error("Symbol.prototype method called on incompatible receiver " + describe(aThis));
        }

        value symbol_prototype_to_string(value aThis, arguments, value, function_object&)
        {
            return make_string_utf8(describe(this_symbol(aThis)));
        }

        value symbol_prototype_value_of(value aThis, arguments, value, function_object&)
        {
            return this_symbol(aThis);
        }

        value symbol_prototype_description(value aThis, arguments, value, function_object&)
        {
            return cell_of<symbol>(this_symbol(aThis)).description;
        }
    }

    void init_symbol(realm& aRealm)
    {
        auto const c = make_native("Symbol", 0u, symbol_constructor, true);
        aRealm.symbol = c;
        aRealm.symbolPrototype = make_object();
        auto const p = aRealm.symbolPrototype;
        obj(c).put(the_names().prototype, p, 0u);
        obj(p).put(the_names().constructor, c);
        for (auto const& [name, s] : { std::pair{ "asyncIterator", aRealm.symAsyncIterator }, std::pair{ "hasInstance", aRealm.symHasInstance },
            std::pair{ "isConcatSpreadable", aRealm.symIsConcatSpreadable }, std::pair{ "iterator", aRealm.symIterator },
            std::pair{ "match", aRealm.symMatch }, std::pair{ "matchAll", aRealm.symMatchAll }, std::pair{ "replace", aRealm.symReplace },
            std::pair{ "search", aRealm.symSearch }, std::pair{ "species", aRealm.symSpecies }, std::pair{ "split", aRealm.symSplit },
            std::pair{ "toPrimitive", aRealm.symToPrimitive }, std::pair{ "toStringTag", aRealm.symToStringTag },
            std::pair{ "unscopables", aRealm.symUnscopables }, std::pair{ "dispose", aRealm.symDispose }, std::pair{ "asyncDispose", aRealm.symAsyncDispose } })
            constant(c, name, s);
        method(c, "for", 1u, symbol_for);
        method(c, "keyFor", 1u, symbol_key_for);
        method(p, "toString", 0u, symbol_prototype_to_string);
        method(p, "valueOf", 0u, symbol_prototype_value_of);
        getter(p, ascii("description"), symbol_prototype_description);
        auto const toPrimitive = make_native(aRealm.symToPrimitive, 1u, symbol_prototype_value_of);
        set_function_name(toPrimitive, aRealm.symToPrimitive);
        obj(p).put(aRealm.symToPrimitive, toPrimitive, AttributeConfigurable);
        obj(p).put(aRealm.symToStringTag, ascii("Symbol"), AttributeConfigurable);
        obj(aRealm.global).put(ascii("Symbol"), c);
    }

    // --- Boolean

    namespace
    {
        value boolean_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            auto const b = boolean(to_boolean(argument(aArguments, 0u)));
            if (aNewTarget == Undefined)
                return b;
            auto const o = ordinary_create_from_constructor(aNewTarget, &realm::booleanPrototype, object_class::Boolean);
            obj(o).internal = b;
            return o;
        }

        value this_boolean(value aThis)
        {
            if (is_boolean(aThis))
                return aThis;
            if (is_object(aThis) && obj(aThis).cls == object_class::Boolean)
                return obj(aThis).internal;
            throw_type_error("Boolean.prototype method called on incompatible receiver " + describe(aThis));
        }

        value boolean_prototype_to_string(value aThis, arguments, value, function_object&)
        {
            return this_boolean(aThis) == True ? the_names().true_ : the_names().false_;
        }

        value boolean_prototype_value_of(value aThis, arguments, value, function_object&)
        {
            return this_boolean(aThis);
        }
    }

    void init_boolean(realm& aRealm)
    {
        auto const c = make_native("Boolean", 1u, boolean_constructor, true);
        aRealm.boolean = c;
        auto& p = the_heap().make<object>(aRealm.objectPrototype, object_class::Boolean);
        p.internal = False;
        aRealm.booleanPrototype = value_of(p);
        obj(c).put(the_names().prototype, aRealm.booleanPrototype, 0u);
        p.put(the_names().constructor, c);
        method(aRealm.booleanPrototype, "toString", 0u, boolean_prototype_to_string);
        method(aRealm.booleanPrototype, "valueOf", 0u, boolean_prototype_value_of);
        obj(aRealm.global).put(ascii("Boolean"), c);
    }

    // --- Number

    namespace
    {
        value number_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            double n = 0.0;
            if (!aArguments.empty())
            {
                auto const prim = to_numeric(aArguments[0]);
                n = is_bigint(prim) ? bigint_to_number(prim) : as_number(prim);
            }
            if (aNewTarget == Undefined)
                return number(n);
            auto const o = ordinary_create_from_constructor(aNewTarget, &realm::numberPrototype, object_class::Number);
            obj(o).internal = number(n);
            return o;
        }

        double this_number(value aThis)
        {
            if (is_number(aThis))
                return as_number(aThis);
            if (is_object(aThis) && obj(aThis).cls == object_class::Number)
                return as_number(obj(aThis).internal);
            throw_type_error("Number.prototype method called on incompatible receiver " + describe(aThis));
        }

        value number_prototype_to_string(value aThis, arguments aArguments, value, function_object&)
        {
            auto const x = this_number(aThis);
            auto const radixArgument = argument(aArguments, 0u);
            int radix = 10;
            if (radixArgument != Undefined)
            {
                auto const r = to_integer_or_infinity(radixArgument);
                if (r < 2.0 || r > 36.0)
                    throw_range_error("toString() radix must be between 2 and 36");
                radix = static_cast<int>(r);
            }
            if (radix == 10)
                return number_to_string(x);
            return make_string(number_to_u16string(x, radix));
        }

        value number_prototype_to_locale_string(value aThis, arguments aArguments, value, function_object&)
        {
            auto const x = this_number(aThis);
#if defined(NEOS_JS_ICU)
            if (intl::icu_available())
                return intl::number_to_locale_string(number(x), argument(aArguments, 0u), argument(aArguments, 1u));
#endif
            (void)aArguments;
            return number_to_string(x);
        }

        value number_prototype_value_of(value aThis, arguments, value, function_object&)
        {
            return number(this_number(aThis));
        }

        value number_prototype_to_fixed(value aThis, arguments aArguments, value, function_object&)
        {
            auto const x = this_number(aThis);
            auto const f = to_integer_or_infinity(argument(aArguments, 0u));
            if (!std::isfinite(f) || f < 0.0 || f > 100.0)
                throw_range_error("toFixed() digits argument must be between 0 and 100");
            if (!std::isfinite(x))
                return number_to_string(x);
            return make_string_utf8(number_to_fixed(x, static_cast<int>(f)));
        }

        value number_prototype_to_exponential(value aThis, arguments aArguments, value, function_object&)
        {
            auto const x = this_number(aThis);
            auto const fractionDigits = argument(aArguments, 0u);
            auto const f = to_integer_or_infinity(fractionDigits);
            if (!std::isfinite(x))
                return number_to_string(x);
            if (!std::isfinite(f) || f < 0.0 || f > 100.0)
                throw_range_error("toExponential() argument must be between 0 and 100");
            return make_string_utf8(number_to_exponential(x, fractionDigits == Undefined ? std::nullopt : std::optional<int>{ static_cast<int>(f) }));
        }

        value number_prototype_to_precision(value aThis, arguments aArguments, value, function_object&)
        {
            auto const x = this_number(aThis);
            auto const precision = argument(aArguments, 0u);
            if (precision == Undefined)
                return number_to_string(x);
            auto const p = to_integer_or_infinity(precision);
            if (!std::isfinite(x))
                return number_to_string(x);
            if (!std::isfinite(p) || p < 1.0 || p > 100.0)
                throw_range_error("toPrecision() argument must be between 1 and 100");
            return make_string_utf8(number_to_precision(x, static_cast<int>(p)));
        }

        value number_is_finite(value, arguments aArguments, value, function_object&)
        {
            auto const v = argument(aArguments, 0u);
            return boolean(is_number(v) && std::isfinite(as_number(v)));
        }

        value number_is_integer(value, arguments aArguments, value, function_object&)
        {
            return boolean(is_integral_number(argument(aArguments, 0u)));
        }

        value number_is_nan(value, arguments aArguments, value, function_object&)
        {
            auto const v = argument(aArguments, 0u);
            return boolean(is_number(v) && std::isnan(as_number(v)));
        }

        value number_is_safe_integer(value, arguments aArguments, value, function_object&)
        {
            auto const v = argument(aArguments, 0u);
            return boolean(is_integral_number(v) && std::fabs(as_number(v)) <= 9007199254740991.0);
        }

        // --- global functions

        double trimmed_parse_float(std::u16string_view aText)
        {
            auto const text = es::trim(aText, true, false);
            std::string ascii;
            for (auto c : text)
            {
                if (c >= 0x80u)
                    break;
                ascii.push_back(static_cast<char>(c));
            }
            auto const length = es::detail::decimal_literal(ascii);
            if (length == 0u)
                return std::numeric_limits<double>::quiet_NaN();
            return es::detail::decimal_value(std::string_view{ ascii }.substr(0u, length));
        }

        value parse_float(value, arguments aArguments, value, function_object&)
        {
            return number(trimmed_parse_float(str(to_string(argument(aArguments, 0u)))));
        }

        value parse_int(value, arguments aArguments, value, function_object&)
        {
            auto const input = to_string(argument(aArguments, 0u));
            auto text = es::trim(std::u16string_view{ str(input) }, true, false);
            bool negative = false;
            if (!text.empty() && (text[0] == u'+' || text[0] == u'-'))
            {
                negative = text[0] == u'-';
                text.remove_prefix(1u);
            }
            auto r = to_int32(argument(aArguments, 1u));
            bool stripPrefix = true;
            if (r != 0)
            {
                if (r < 2 || r > 36)
                    return CanonicalNaN;
                if (r != 16)
                    stripPrefix = false;
            }
            else
                r = 10;
            if (stripPrefix && text.size() >= 2u && text[0] == u'0' && (text[1] == u'x' || text[1] == u'X'))
            {
                text.remove_prefix(2u);
                r = 16;
            }
            std::size_t end = 0u;
            while (end < text.size() && es::detail::digit_value(text[end]) < r)
                ++end;
            if (end == 0u)
                return CanonicalNaN;
            double v = 0.0;
            if (r == 10)
            {
                // (decimal: correctly rounded (20 significant digits are enough to round correctly, the rest are zeros))
                std::string digits;
                for (std::size_t i = 0u; i < end; ++i)
                    digits.push_back(static_cast<char>(text[i]));
                v = std::strtod(digits.c_str(), nullptr);
            }
            else if ((r & (r - 1)) == 0)
            {
                // (a power of two radix: exactly rounded from the bits)
                std::string hex;
                int const bits = r == 2 ? 1 : r == 4 ? 2 : r == 8 ? 3 : r == 16 ? 4 : 5;
                thread_local std::vector<int> tB;
                tB.clear();
                for (std::size_t i = 0u; i < end; ++i)
                {
                    auto const d = es::detail::digit_value(text[i]);
                    for (int k = bits - 1; k >= 0; --k)
                        tB.push_back((d >> k) & 1);
                }
                while ((tB.size() % 4u) != 0u)
                    tB.insert(tB.begin(), 0);
                for (std::size_t i = 0u; i < tB.size(); i += 4u)
                    hex.push_back("0123456789abcdef"[tB[i] * 8 + tB[i + 1u] * 4 + tB[i + 2u] * 2 + tB[i + 3u]]);
                v = std::strtod(("0x" + hex).c_str(), nullptr);
            }
            else
                for (std::size_t i = 0u; i < end; ++i)
                    v = v * r + es::detail::digit_value(text[i]);
            return number(negative ? -v : v);
        }

        value is_nan(value, arguments aArguments, value, function_object&)
        {
            return boolean(std::isnan(to_number(argument(aArguments, 0u))));
        }

        value is_finite(value, arguments aArguments, value, function_object&)
        {
            return boolean(std::isfinite(to_number(argument(aArguments, 0u))));
        }

        // (URI encoding: the characters not in aUnescaped are escaped as %XX of their UTF-8)
        value encode(value aString, std::u16string_view aUnescaped)
        {
            auto const& text = str(aString);
            std::u16string result;
            for (std::size_t k = 0u; k < text.size(); ++k)
            {
                auto const c = text[k];
                if (c < 0x80u && (std::isalnum(static_cast<unsigned char>(c)) || aUnescaped.find(c) != std::u16string_view::npos))
                {
                    result.push_back(c);
                    continue;
                }
                char32_t cp = c;
                if (c >= 0xDC00u && c < 0xE000u)
                    throw_error(the_realm().uriErrorPrototype, "URI malformed");
                if (c >= 0xD800u && c < 0xDC00u)
                {
                    if (k + 1u >= text.size() || text[k + 1u] < 0xDC00u || text[k + 1u] >= 0xE000u)
                        throw_error(the_realm().uriErrorPrototype, "URI malformed");
                    cp = 0x10000u + ((c - 0xD800u) << 10) + (text[k + 1u] - 0xDC00u);
                    ++k;
                }
                std::string utf8bytes = wtf8(std::u16string_view{ text }.substr(cp >= 0x10000u ? k - 1u : k, cp >= 0x10000u ? 2u : 1u));
                for (auto b : utf8bytes)
                {
                    char buffer[4];
                    std::snprintf(buffer, sizeof(buffer), "%%%02X", static_cast<unsigned>(static_cast<unsigned char>(b)));
                    result += utf16_ascii(buffer);
                }
            }
            return make_string(std::move(result));
        }

        value decode(value aString, std::u16string_view aReserved)
        {
            auto const& text = str(aString);
            std::u16string result;
            auto const hex = [&](std::size_t aAt) -> int
            {
                if (aAt + 2u >= text.size() + 1u || aAt + 1u >= text.size())
                    throw_error(the_realm().uriErrorPrototype, "URI malformed");
                auto const h = es::detail::digit_value(text[aAt]);
                auto const l = es::detail::digit_value(text[aAt + 1u]);
                if (h >= 16 || l >= 16)
                    throw_error(the_realm().uriErrorPrototype, "URI malformed");
                return h * 16 + l;
            };
            for (std::size_t k = 0u; k < text.size(); ++k)
            {
                auto const c = text[k];
                if (c != u'%')
                {
                    result.push_back(c);
                    continue;
                }
                auto const start = k;
                if (k + 2u >= text.size())
                    throw_error(the_realm().uriErrorPrototype, "URI malformed");
                auto const b = hex(k + 1u);
                k += 2u;
                if (b < 0x80)
                {
                    if (aReserved.find(static_cast<char16_t>(b)) != std::u16string_view::npos)
                        result.append(text, start, k - start + 1u);
                    else
                        result.push_back(static_cast<char16_t>(b));
                    continue;
                }
                int n = (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 0;
                if (n == 0)
                    throw_error(the_realm().uriErrorPrototype, "URI malformed");
                char32_t cp = static_cast<char32_t>(b & (0xFF >> (n + 1)));
                for (int j = 1; j < n; ++j)
                {
                    if (k + 3u > text.size() || text[k + 1u] != u'%')
                        throw_error(the_realm().uriErrorPrototype, "URI malformed");
                    auto const cb = hex(k + 2u);
                    if ((cb & 0xC0) != 0x80)
                        throw_error(the_realm().uriErrorPrototype, "URI malformed");
                    cp = (cp << 6) | static_cast<char32_t>(cb & 0x3F);
                    k += 3u;
                }
                if ((n == 2 && cp < 0x80u) || (n == 3 && cp < 0x800u) || (n == 4 && cp < 0x10000u) || cp > 0x10FFFFu || (cp >= 0xD800u && cp < 0xE000u))
                    throw_error(the_realm().uriErrorPrototype, "URI malformed");
                if (cp >= 0x10000u)
                {
                    cp -= 0x10000u;
                    result.push_back(static_cast<char16_t>(0xD800u + (cp >> 10)));
                    result.push_back(static_cast<char16_t>(0xDC00u + (cp & 0x3FFu)));
                }
                else
                    result.push_back(static_cast<char16_t>(cp));
            }
            return make_string(std::move(result));
        }

        value encode_uri(value, arguments aArguments, value, function_object&)
        {
            return encode(to_string(argument(aArguments, 0u)), u";/?:@&=+$,-_.!~*'()#");
        }

        value encode_uri_component(value, arguments aArguments, value, function_object&)
        {
            return encode(to_string(argument(aArguments, 0u)), u"-_.!~*'()");
        }

        value decode_uri(value, arguments aArguments, value, function_object&)
        {
            return decode(to_string(argument(aArguments, 0u)), u";/?:@&=+$,#");
        }

        value decode_uri_component(value, arguments aArguments, value, function_object&)
        {
            return decode(to_string(argument(aArguments, 0u)), u"");
        }

        // (Annex B: escape and unescape)
        value escape_function(value, arguments aArguments, value, function_object&)
        {
            auto const& text = str(to_string(argument(aArguments, 0u)));
            std::u16string result;
            static std::u16string_view const sUnescaped = u"@*_+-./";
            for (auto c : text)
            {
                if (c < 0x80u && (std::isalnum(static_cast<unsigned char>(c)) || sUnescaped.find(c) != std::u16string_view::npos))
                    result.push_back(c);
                else
                {
                    char buffer[8];
                    if (c < 256u)
                        std::snprintf(buffer, sizeof(buffer), "%%%02X", static_cast<unsigned>(c));
                    else
                        std::snprintf(buffer, sizeof(buffer), "%%u%04X", static_cast<unsigned>(c));
                    result += utf16_ascii(buffer);
                }
            }
            return make_string(std::move(result));
        }

        value unescape_function(value, arguments aArguments, value, function_object&)
        {
            auto const& text = str(to_string(argument(aArguments, 0u)));
            std::u16string result;
            auto const hex = [&](std::size_t aAt, std::size_t aCount) -> int
            {
                int v = 0;
                for (std::size_t i = 0u; i < aCount; ++i)
                {
                    auto const d = es::detail::digit_value(text[aAt + i]);
                    if (d >= 16)
                        return -1;
                    v = v * 16 + d;
                }
                return v;
            };
            for (std::size_t k = 0u; k < text.size(); ++k)
            {
                auto c = text[k];
                if (c == u'%')
                {
                    if (k + 6u <= text.size() && text[k + 1u] == u'u')
                        if (auto const v = hex(k + 2u, 4u); v >= 0)
                        {
                            c = static_cast<char16_t>(v);
                            k += 5u;
                            result.push_back(c);
                            continue;
                        }
                    if (k + 3u <= text.size())
                        if (auto const v = hex(k + 1u, 2u); v >= 0)
                        {
                            c = static_cast<char16_t>(v);
                            k += 2u;
                        }
                }
                result.push_back(c);
            }
            return make_string(std::move(result));
        }
    }

    void init_number(realm& aRealm)
    {
        auto const c = make_native("Number", 1u, number_constructor, true);
        aRealm.number = c;
        auto& p = the_heap().make<object>(aRealm.objectPrototype, object_class::Number);
        p.internal = number(0.0);
        aRealm.numberPrototype = value_of(p);
        obj(c).put(the_names().prototype, aRealm.numberPrototype, 0u);
        p.put(the_names().constructor, c);
        constant(c, "EPSILON", number(2.220446049250313e-16));
        constant(c, "MAX_SAFE_INTEGER", number(9007199254740991.0));
        constant(c, "MAX_VALUE", number(std::numeric_limits<double>::max()));
        constant(c, "MIN_SAFE_INTEGER", number(-9007199254740991.0));
        constant(c, "MIN_VALUE", number(std::numeric_limits<double>::denorm_min()));
        constant(c, "NaN", CanonicalNaN);
        constant(c, "NEGATIVE_INFINITY", number(-std::numeric_limits<double>::infinity()));
        constant(c, "POSITIVE_INFINITY", number(std::numeric_limits<double>::infinity()));
        method(c, "isFinite", 1u, number_is_finite);
        method(c, "isInteger", 1u, number_is_integer);
        method(c, "isNaN", 1u, number_is_nan);
        method(c, "isSafeInteger", 1u, number_is_safe_integer);
        auto const pr = aRealm.numberPrototype;
        method(pr, "toExponential", 1u, number_prototype_to_exponential);
        method(pr, "toFixed", 1u, number_prototype_to_fixed);
        method(pr, "toLocaleString", 0u, number_prototype_to_locale_string);
        method(pr, "toPrecision", 1u, number_prototype_to_precision);
        method(pr, "toString", 1u, number_prototype_to_string);
        method(pr, "valueOf", 0u, number_prototype_value_of);
        obj(aRealm.global).put(ascii("Number"), c);
    }

    void init_global(realm& aRealm)
    {
        auto const g = aRealm.global;
        auto const pf = method(g, "parseFloat", 1u, parse_float);
        auto const pi = method(g, "parseInt", 2u, parse_int);
        obj(aRealm.number).put(ascii("parseFloat"), pf);
        obj(aRealm.number).put(ascii("parseInt"), pi);
        method(g, "isNaN", 1u, is_nan);
        method(g, "isFinite", 1u, is_finite);
        method(g, "encodeURI", 1u, encode_uri);
        method(g, "encodeURIComponent", 1u, encode_uri_component);
        method(g, "decodeURI", 1u, decode_uri);
        method(g, "decodeURIComponent", 1u, decode_uri_component);
        method(g, "escape", 1u, escape_function);
        method(g, "unescape", 1u, unescape_function);
    }

    // --- Math

    namespace
    {
        template <double (*F)(double)>
        value math1(value, arguments aArguments, value, function_object&)
        {
            return number(F(to_number(argument(aArguments, 0u))));
        }

        double math_round(double x)
        {
            if (!std::isfinite(x) || x == 0.0)
                return x;
            if (x > 0.0 && x < 0.5)
                return 0.0;
            if (x < 0.0 && x >= -0.5)
                return -0.0;
            if (std::fabs(x) >= 4503599627370496.0)
                return x;
            return std::floor(x + 0.5);
        }

        double math_sign(double x)
        {
            if (std::isnan(x) || x == 0.0)
                return x;
            return x > 0.0 ? 1.0 : -1.0;
        }

        double math_fround(double x)
        {
            return static_cast<double>(static_cast<float>(x));
        }

        double math_cbrt(double x)
        {
            return es::cbrt(x);
        }

        double math_clz32(double x)
        {
            auto const n = to_uint32(x);
            return n == 0u ? 32.0 : static_cast<double>(std::countl_zero(n));
        }

        double math_f16round(double x)
        {
            // (round to the nearest binary16 (ties to even), as a double)
            if (std::isnan(x) || std::isinf(x) || x == 0.0)
                return x;
            double const a = std::fabs(x);
            if (a >= 65520.0)
                return std::copysign(std::numeric_limits<double>::infinity(), x);
            int exponent;
            std::frexp(a, &exponent);
            // (binary16: 10 fraction bits; the smallest normal is 2^-14, subnormals have the quantum 2^-24)
            int const e = std::max(exponent - 1, -14);
            double const quantum = std::ldexp(1.0, e - 10);
            double const q = a / quantum;
            double r = std::nearbyint(q);
            if (std::fabs(q - std::trunc(q) - 0.5) == 0.0)
                r = (static_cast<long long>(std::trunc(q)) % 2 == 0) ? std::trunc(q) : std::trunc(q) + 1.0;
            return std::copysign(r * quantum, x);
        }

        value math_abs(value, arguments aArguments, value, function_object&) { return number(std::fabs(to_number(argument(aArguments, 0u)))); }

        value math_atan2(value, arguments aArguments, value, function_object&)
        {
            auto const y = to_number(argument(aArguments, 0u));
            auto const x = to_number(argument(aArguments, 1u));
            return number(std::atan2(y, x));
        }

        value math_pow(value, arguments aArguments, value, function_object&)
        {
            auto const b = to_number(argument(aArguments, 0u));
            auto const e = to_number(argument(aArguments, 1u));
            return number(es::pow(b, e));
        }

        value math_imul(value, arguments aArguments, value, function_object&)
        {
            auto const a = to_uint32(argument(aArguments, 0u));
            auto const b = to_uint32(argument(aArguments, 1u));
            return number(static_cast<double>(static_cast<std::int32_t>(a * b)));
        }

        value math_max(value, arguments aArguments, value, function_object&)
        {
            std::vector<double> values;
            for (auto a : aArguments)
                values.push_back(to_number(a));
            double result = -std::numeric_limits<double>::infinity();
            for (auto v : values)
            {
                if (std::isnan(v))
                    return CanonicalNaN;
                if (v > result || (v == 0.0 && result == 0.0 && !std::signbit(v)))
                    result = v;
            }
            return number(result);
        }

        value math_min(value, arguments aArguments, value, function_object&)
        {
            std::vector<double> values;
            for (auto a : aArguments)
                values.push_back(to_number(a));
            double result = std::numeric_limits<double>::infinity();
            for (auto v : values)
            {
                if (std::isnan(v))
                    return CanonicalNaN;
                if (v < result || (v == 0.0 && result == 0.0 && std::signbit(v)))
                    result = v;
            }
            return number(result);
        }

        value math_hypot(value, arguments aArguments, value, function_object&)
        {
            std::vector<double> values;
            for (auto a : aArguments)
                values.push_back(to_number(a));
            bool nan = false;
            for (auto v : values)
            {
                if (std::isinf(v))
                    return number(std::numeric_limits<double>::infinity());
                if (std::isnan(v))
                    nan = true;
            }
            if (nan)
                return CanonicalNaN;
            double largest = 0.0;
            for (auto v : values)
                largest = std::max(largest, std::fabs(v));
            if (largest == 0.0)
                return number(0.0);
            double sum = 0.0;
            double compensation = 0.0;
            for (auto v : values)
            {
                double const r = v / largest;
                double const y = r * r - compensation;
                double const t = sum + y;
                compensation = (t - sum) - y;
                sum = t;
            }
            return number(std::sqrt(sum) * largest);
        }

        value math_random(value, arguments, value, function_object&)
        {
            thread_local std::mt19937_64 sGenerator{ std::random_device{}() };
            return number(static_cast<double>(sGenerator() >> 11) * (1.0 / 9007199254740992.0));
        }

        value math_sum_precise(value, arguments aArguments, value, function_object&)
        {
            // (Math.sumPrecise: the exactly rounded sum of an iterable's numbers (Shewchuk's algorithm, with the overflow and final rounding
            // handling of the algorithm of the TC39 proposal-math-sum reference implementation (polyfill/polyfill.mjs, Kevin Gibbons)))
            auto record = get_iterator(argument(aArguments, 0u));
            std::vector<double> partials;
            bool any = false;
            bool positiveInfinity = false, negativeInfinity = false, nan = false;
            bool allNegativeZero = true;
            double overflow = 0.0;
            double constexpr twoTo1023 = 8.98846567431158e+307;
            while (auto const next = iterator_step_value(record))
            {
                if (!is_number(*next))
                {
                    iterator_close_on_throw(record);
                    throw_type_error("Math.sumPrecise requires numbers");
                }
                any = true;
                double x = as_number(*next);
                if (std::isnan(x))
                    nan = true;
                else if (std::isinf(x))
                    (x > 0.0 ? positiveInfinity : negativeInfinity) = true;
                if (!(x == 0.0 && std::signbit(x)))
                    allNegativeZero = false;
                if (!std::isfinite(x) || nan || positiveInfinity || negativeInfinity)
                    continue;
                // (a partial that overflows is kept as a multiple of 2^1024 (overflow) (see the proposal's reference implementation))
                std::size_t used = 0u;
                for (std::size_t j = 0u; j < partials.size(); ++j)
                {
                    double y = partials[j];
                    if (std::fabs(x) < std::fabs(y))
                        std::swap(x, y);
                    double hi = x + y;
                    double lo = y - (hi - x);
                    if (std::isinf(hi))
                    {
                        double const sign = hi > 0.0 ? 1.0 : -1.0;
                        overflow += sign;
                        x = (x - sign * twoTo1023) - sign * twoTo1023;
                        if (std::fabs(x) < std::fabs(y))
                            std::swap(x, y);
                        hi = x + y;
                        lo = y - (hi - x);
                    }
                    if (lo != 0.0)
                        partials[used++] = lo;
                    x = hi;
                }
                partials.resize(used);
                if (x != 0.0)
                    partials.push_back(x);
            }
            if (nan || (positiveInfinity && negativeInfinity))
                return CanonicalNaN;
            if (positiveInfinity)
                return number(std::numeric_limits<double>::infinity());
            if (negativeInfinity)
                return number(-std::numeric_limits<double>::infinity());
            if (!any || allNegativeZero)
                return number(-0.0);
            double constexpr maxDouble = std::numeric_limits<double>::max();
            double constexpr maxUlp = 1.99584030953471981166e+292;    // (2^(1023 - 52))
            auto n = static_cast<std::ptrdiff_t>(partials.size()) - 1;
            double hi = 0.0;
            double lo = 0.0;
            if (overflow != 0.0)
            {
                double const next = n >= 0 ? partials[static_cast<std::size_t>(n)] : 0.0;
                --n;
                if (std::fabs(overflow) > 1.0 || (overflow > 0.0 && next > 0.0) || (overflow < 0.0 && next < 0.0))
                    return number(overflow > 0.0 ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity());
                // (halved so that the arithmetic doesn't overflow)
                double const a = overflow * twoTo1023;
                double const b = next / 2.0;
                hi = a + b;
                lo = b - (hi - a);
                lo *= 2.0;
                if (std::isinf(2.0 * hi))
                {
                    // (rounding to the maximum value: only a further partial of the opposite sign makes it finite)
                    if (hi > 0.0)
                        return number(hi == twoTo1023 && lo == -(maxUlp / 2.0) && n >= 0 && partials[static_cast<std::size_t>(n)] < 0.0 ?
                            maxDouble : std::numeric_limits<double>::infinity());
                    return number(hi == -twoTo1023 && lo == (maxUlp / 2.0) && n >= 0 && partials[static_cast<std::size_t>(n)] > 0.0 ?
                        -maxDouble : -std::numeric_limits<double>::infinity());
                }
                if (lo != 0.0)
                {
                    partials.resize(static_cast<std::size_t>(n + 2));
                    partials[static_cast<std::size_t>(n + 1)] = lo;
                    ++n;
                    lo = 0.0;
                }
                hi *= 2.0;
            }
            // (the exact sum of the partials, until precision is lost)
            while (n >= 0)
            {
                double const x = hi;
                double const y = partials[static_cast<std::size_t>(n)];
                --n;
                hi = x + y;
                lo = y - (hi - x);
                if (lo != 0.0)
                    break;
            }
            // (a roundoff of exactly half an ulp: the next partial decides the rounding)
            if (n >= 0 && ((lo < 0.0 && partials[static_cast<std::size_t>(n)] < 0.0) || (lo > 0.0 && partials[static_cast<std::size_t>(n)] > 0.0)))
            {
                double const y = lo * 2.0;
                double const x = hi + y;
                double const yr = x - hi;
                if (y == yr)
                    hi = x;
            }
            return number(hi);
        }
    }

    void init_math(realm& aRealm)
    {
        auto const m = make_object();
        constant(m, "E", number(2.718281828459045));
        constant(m, "LN10", number(2.302585092994046));
        constant(m, "LN2", number(0.6931471805599453));
        constant(m, "LOG10E", number(0.4342944819032518));
        constant(m, "LOG2E", number(1.4426950408889634));
        constant(m, "PI", number(3.141592653589793));
        constant(m, "SQRT1_2", number(0.7071067811865476));
        constant(m, "SQRT2", number(1.4142135623730951));
        obj(m).put(aRealm.symToStringTag, ascii("Math"), AttributeConfigurable);
        method(m, "abs", 1u, math_abs);
        method(m, "acos", 1u, math1<std::acos>);
        method(m, "acosh", 1u, math1<std::acosh>);
        method(m, "asin", 1u, math1<std::asin>);
        method(m, "asinh", 1u, math1<std::asinh>);
        method(m, "atan", 1u, math1<std::atan>);
        method(m, "atanh", 1u, math1<std::atanh>);
        method(m, "atan2", 2u, math_atan2);
        method(m, "cbrt", 1u, math1<math_cbrt>);
        method(m, "ceil", 1u, math1<std::ceil>);
        method(m, "clz32", 1u, math1<math_clz32>);
        method(m, "cos", 1u, math1<std::cos>);
        method(m, "cosh", 1u, math1<std::cosh>);
        method(m, "exp", 1u, math1<std::exp>);
        method(m, "expm1", 1u, math1<std::expm1>);
        method(m, "f16round", 1u, math1<math_f16round>);
        method(m, "floor", 1u, math1<std::floor>);
        method(m, "fround", 1u, math1<math_fround>);
        method(m, "hypot", 2u, math_hypot);
        method(m, "imul", 2u, math_imul);
        method(m, "log", 1u, math1<std::log>);
        method(m, "log1p", 1u, math1<std::log1p>);
        method(m, "log10", 1u, math1<std::log10>);
        method(m, "log2", 1u, math1<std::log2>);
        method(m, "max", 2u, math_max);
        method(m, "min", 2u, math_min);
        method(m, "pow", 2u, math_pow);
        method(m, "random", 0u, math_random);
        method(m, "round", 1u, math1<math_round>);
        method(m, "sign", 1u, math1<math_sign>);
        method(m, "sin", 1u, math1<std::sin>);
        method(m, "sinh", 1u, math1<std::sinh>);
        method(m, "sqrt", 1u, math1<std::sqrt>);
        method(m, "sumPrecise", 1u, math_sum_precise);
        method(m, "tan", 1u, math1<std::tan>);
        method(m, "tanh", 1u, math1<std::tanh>);
        method(m, "trunc", 1u, math1<std::trunc>);
        obj(aRealm.global).put(ascii("Math"), m);
    }

    bool math_intrinsic(value aFunction, std::uint32_t aIntrinsic)
    {
        // (whether a function is the built-in Math function that the compiler's intrinsic aIntrinsic (1 imul, 2 floor, 3 ceil, 4 trunc, 5 sqrt,
        // 6 abs) computes inline)
        if (!is_object(aFunction) || obj(aFunction).cls != object_class::Function)
            return false;
        auto const& f = static_cast<function_object const&>(obj(aFunction));
        if (f.kind != function_kind::Native)
            return false;
        switch (aIntrinsic)
        {
        case 1u: return f.native == &math_imul;
        case 2u: return f.native == &math1<std::floor>;
        case 3u: return f.native == &math1<std::ceil>;
        case 4u: return f.native == &math1<std::trunc>;
        case 5u: return f.native == &math1<std::sqrt>;
        case 6u: return f.native == &math_abs;
        default: return false;
        }
    }

    // --- iterators

    namespace
    {
        value iterator_prototype_iterator(value aThis, arguments, value, function_object&)
        {
            return aThis;
        }

        // (an array iterator: its object, next index and kind (0 keys, 1 values, 2 entries); a typed array's length is checked)
        class array_iterator : public object
        {
        public:
            array_iterator(value aPrototype) : object{ aPrototype, object_class::ArrayIterator } {}
        public:
            void trace(tracer& aTracer) override
            {
                object::trace(aTracer);
                aTracer.mark(iterated);
            }
        public:
            value iterated = Undefined;
            double next = 0.0;
            int kind = 1;
        };

        value array_iterator_next(value aThis, arguments, value, function_object&)
        {
            if (!is_object(aThis) || obj(aThis).cls != object_class::ArrayIterator)
                throw_type_error("next method called on incompatible receiver " + describe(aThis));
            auto& it = static_cast<array_iterator&>(obj(aThis));
            if (it.iterated == Undefined)
                return create_iter_result_object(Undefined, true);
            double length;
            if (obj(it.iterated).cls == object_class::TypedArray)
                length = typed_array_length_checked(it.iterated);
            else
                length = length_of_array_like(it.iterated);
            if (it.next >= length)
            {
                it.iterated = Undefined;
                return create_iter_result_object(Undefined, true);
            }
            auto const index = it.next;
            it.next += 1.0;
            if (it.kind == 0)
                return create_iter_result_object(number(index), false);
            auto const element = get(it.iterated, index_key(static_cast<std::uint64_t>(index)));
            if (it.kind == 1)
                return create_iter_result_object(element, false);
            value const entry[] = { number(index), element };
            return create_iter_result_object(create_array_from_list(entry), false);
        }
    }

    value make_array_iterator(value aObject, int aKind)
    {
        auto& it = the_heap().make<array_iterator>(the_realm().arrayIteratorPrototype);
        it.iterated = aObject;
        it.kind = aKind;
        return value_of(it);
    }

    void init_iterator(realm& aRealm)
    {
        aRealm.iteratorPrototype = make_object();
        auto const ip = aRealm.iteratorPrototype;
        auto const iter = make_native(aRealm.symIterator, 0u, iterator_prototype_iterator);
        obj(ip).put(aRealm.symIterator, iter);
        aRealm.asyncIteratorPrototype = make_object();
        auto const aiter = make_native(aRealm.symAsyncIterator, 0u, iterator_prototype_iterator);
        obj(aRealm.asyncIteratorPrototype).put(aRealm.symAsyncIterator, aiter);
        aRealm.arrayIteratorPrototype = make_object(ip);
        method(aRealm.arrayIteratorPrototype, "next", 0u, array_iterator_next);
        obj(aRealm.arrayIteratorPrototype).put(aRealm.symToStringTag, ascii("Array Iterator"), AttributeConfigurable);
        init_iterator_helpers(aRealm);
    }
}
