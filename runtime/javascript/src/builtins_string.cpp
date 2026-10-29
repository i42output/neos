/*
  builtins_string.cpp

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

// String: the String constructor, String.prototype, String exotic objects and the string iterator

#include <algorithm>
#include <neos/language/ecmascript.hpp>
#include "js.hpp"
#include "api.hpp"
#include "intl.hpp"

namespace neos::javascript_runtime
{
    namespace es = neos::ecmascript;

    // --- String exotic objects (their indices are the string's characters; their length its length)

    namespace
    {
        class string_object : public object
        {
        public:
            explicit string_object(value aPrototype) : object{ aPrototype, object_class::String } {}
        public:
            std::optional<property_descriptor> string_property(value aKey)
            {
                auto const index = array_index(aKey);
                if (!index || *index >= string_length(internal()))
                    return {};
                return property_descriptor::data(make_string(str(internal())[*index]), AttributeEnumerable);
            }
            std::optional<property_descriptor> get_own_property(value aKey) override
            {
                if (auto const desc = ordinary_get_own_property(aKey))
                    return desc;
                return string_property(aKey);
            }
            bool define_own_property(value aKey, property_descriptor const& aDescriptor) override
            {
                if (auto const current = string_property(aKey))
                    return is_compatible_property_descriptor(is_extensible(), aDescriptor, current);
                return ordinary_define_own_property(aKey, aDescriptor);
            }
            bool has_property(value aKey) override
            {
                if (string_property(aKey))
                    return true;
                return object::has_property(aKey);
            }
            value get(value aKey, value aReceiver) override
            {
                if (auto const desc = string_property(aKey))
                    return *desc->valueField;
                return ordinary_get(aKey, aReceiver);
            }
            bool set(value aKey, value aValue, value aReceiver) override
            {
                return ordinary_set(aKey, aValue, aReceiver);
            }
            bool delete_property(value aKey) override
            {
                if (string_property(aKey))
                    return false;
                return ordinary_delete(aKey);
            }
            value_list own_property_keys() override
            {
                value_list result;
                auto const length = string_length(internal());
                for (std::uint32_t i = 0u; i < length; ++i)
                    result.push_back(index_key(i));
                for (auto const k : ordinary_own_property_keys())
                    if (auto const index = array_index(k); !index || *index >= length)
                        result.push_back(k);
                // (integer indices first, in order)
                std::stable_sort(result.begin(), result.end(), [](value a, value b)
                {
                    auto const ia = array_index(a);
                    auto const ib = array_index(b);
                    if (ia && ib)
                        return *ia < *ib;
                    return ia.has_value() && !ib.has_value();
                });
                return result;
            }
        };
    }

    value make_string_object(value aString, value aPrototype)
    {
        auto& o = the_heap().make<string_object>(aPrototype);
        o.internal() = aString;
        o.properties.add(the_names().length, number(string_length(aString)), Undefined, 0u);
        return value_of(o);
    }

    namespace
    {
        value this_string_value(value aThis, char const* aMethod)
        {
            if (is_string(aThis))
                return aThis;
            if (is_object(aThis) && obj(aThis).cls == object_class::String)
                return obj(aThis).internal();
            throw_type_error(std::string{ "String.prototype." } + aMethod + " requires that 'this' be a String");
        }

        // (RequireObjectCoercible(this) then ToString)
        value this_string(value aThis, char const* aMethod)
        {
            if (is_string(aThis))
                return aThis;
            if (is_nullish(aThis))
                throw_type_error(std::string{ "String.prototype." } + aMethod + " called on null or undefined");
            return to_string(aThis);
        }

        std::u16string_view text_of(value aString)
        {
            return str(aString);
        }

        double clamp_index(double aValue, double aLength)
        {
            return std::clamp(aValue, 0.0, aLength);
        }

        char32_t code_point_at(std::u16string_view aText, std::size_t aIndex, std::size_t& aUnits)
        {
            auto const first = aText[aIndex];
            aUnits = 1u;
            if (first < 0xD800u || first > 0xDBFFu || aIndex + 1u == aText.size())
                return first;
            auto const second = aText[aIndex + 1u];
            if (second < 0xDC00u || second > 0xDFFFu)
                return first;
            aUnits = 2u;
            return 0x10000u + ((first - 0xD800u) << 10) + (second - 0xDC00u);
        }

        // --- the constructor

        value string_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            value s;
            if (aArguments.empty())
                s = the_names().empty;
            else
            {
                if (aNewTarget == Undefined && is_symbol(aArguments[0]))
                    return make_string_utf8(describe(aArguments[0]));
                s = to_string(aArguments[0]);
            }
            if (aNewTarget == Undefined)
                return s;
            auto const prototype = get_prototype_from_constructor(aNewTarget, &realm::stringPrototype);
            return make_string_object(s, prototype);
        }

        value string_from_char_code(value, arguments aArguments, value, function_object&)
        {
            std::u16string result;
            for (auto const a : aArguments)
                result.push_back(static_cast<char16_t>(to_uint32(a) & 0xFFFFu));
            return make_string(std::move(result));
        }

        value string_from_code_point(value, arguments aArguments, value, function_object&)
        {
            std::u16string result;
            for (auto const a : aArguments)
            {
                auto const n = to_number(a);
                if (std::trunc(n) != n || n < 0.0 || n > 1114111.0 || std::isnan(n))
                    throw_range_error("Invalid code point " + describe(a));
                auto cp = static_cast<char32_t>(n);
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

        value string_raw(value, arguments aArguments, value, function_object&)
        {
            auto const cooked = to_object(argument(aArguments, 0u));
            auto const literals = to_object(get(cooked, the_names().raw));
            auto const literalCount = length_of_array_like(literals);
            if (literalCount <= 0.0)
                return the_names().empty;
            std::u16string result;
            for (double i = 0.0;; ++i)
            {
                result += str(to_string(get(literals, index_key(static_cast<std::uint64_t>(i)))));
                if (i + 1.0 == literalCount)
                    return make_string(std::move(result));
                auto const subIndex = static_cast<std::size_t>(i) + 1u;
                if (subIndex < aArguments.size())
                    result += str(to_string(aArguments[subIndex]));
            }
        }

        // --- String.prototype

        value string_at(value aThis, arguments aArguments, value, function_object&)
        {
            auto const s = this_string(aThis, "at");
            auto const& text = text_of(s);
            auto const len = static_cast<double>(text.size());
            auto const relative = to_integer_or_infinity(argument(aArguments, 0u));
            auto const k = relative >= 0.0 ? relative : len + relative;
            if (k < 0.0 || k >= len)
                return Undefined;
            return make_string(text[static_cast<std::size_t>(k)]);
        }

        value string_char_at(value aThis, arguments aArguments, value, function_object&)
        {
            auto const s = this_string(aThis, "charAt");
            auto const position = to_integer_or_infinity(argument(aArguments, 0u));
            auto const& text = text_of(s);
            if (position < 0.0 || position >= static_cast<double>(text.size()))
                return the_names().empty;
            return make_string(text[static_cast<std::size_t>(position)]);
        }

        value string_char_code_at(value aThis, arguments aArguments, value, function_object&)
        {
            auto const s = this_string(aThis, "charCodeAt");
            auto const position = to_integer_or_infinity(argument(aArguments, 0u));
            auto const& text = text_of(s);
            if (position < 0.0 || position >= static_cast<double>(text.size()))
                return CanonicalNaN;
            return number(text[static_cast<std::size_t>(position)]);
        }

        value string_code_point_at(value aThis, arguments aArguments, value, function_object&)
        {
            auto const s = this_string(aThis, "codePointAt");
            auto const position = to_integer_or_infinity(argument(aArguments, 0u));
            auto const& text = text_of(s);
            if (position < 0.0 || position >= static_cast<double>(text.size()))
                return Undefined;
            std::size_t units;
            return number(code_point_at(text, static_cast<std::size_t>(position), units));
        }

        value string_concat(value aThis, arguments aArguments, value, function_object&)
        {
            auto result = this_string(aThis, "concat");
            for (auto const a : aArguments)
                result = concat(result, to_string(a));
            return result;
        }

        value string_ends_with(value aThis, arguments aArguments, value, function_object&)
        {
            auto const s = this_string(aThis, "endsWith");
            auto const search = argument(aArguments, 0u);
            if (is_regexp(search))
                throw_type_error("First argument to String.prototype.endsWith must not be a regular expression");
            auto const searchString = to_string(search);
            auto const& text = text_of(s);
            auto const len = static_cast<double>(text.size());
            auto const end = argument(aArguments, 1u) == Undefined ? len : clamp_index(to_integer_or_infinity(aArguments[1]), len);
            auto const& searchText = text_of(searchString);
            auto const start = end - static_cast<double>(searchText.size());
            if (start < 0.0)
                return False;
            return boolean(std::u16string_view{ text }.substr(static_cast<std::size_t>(start), searchText.size()) == searchText);
        }

        value string_includes(value aThis, arguments aArguments, value, function_object&)
        {
            auto const s = this_string(aThis, "includes");
            auto const search = argument(aArguments, 0u);
            if (is_regexp(search))
                throw_type_error("First argument to String.prototype.includes must not be a regular expression");
            auto const searchString = to_string(search);
            auto const& text = text_of(s);
            auto const start = clamp_index(to_integer_or_infinity(argument(aArguments, 1u)), static_cast<double>(text.size()));
            return boolean(text.find(text_of(searchString), static_cast<std::size_t>(start)) != std::u16string::npos);
        }

        value string_index_of(value aThis, arguments aArguments, value, function_object&)
        {
            auto const s = this_string(aThis, "indexOf");
            auto const searchString = to_string(argument(aArguments, 0u));
            auto const& text = text_of(s);
            auto const start = clamp_index(to_integer_or_infinity(argument(aArguments, 1u)), static_cast<double>(text.size()));
            auto const found = text.find(text_of(searchString), static_cast<std::size_t>(start));
            return number(found == std::u16string::npos ? -1.0 : static_cast<double>(found));
        }

        value string_is_well_formed(value aThis, arguments, value, function_object&)
        {
            auto const& text = text_of(this_string(aThis, "isWellFormed"));
            for (std::size_t i = 0u; i < text.size(); ++i)
            {
                std::size_t units;
                auto const cp = code_point_at(text, i, units);
                if (cp >= 0xD800u && cp <= 0xDFFFu)
                    return False;
                i += units - 1u;
            }
            return True;
        }

        value string_to_well_formed(value aThis, arguments, value, function_object&)
        {
            std::u16string text{ text_of(this_string(aThis, "toWellFormed")) };
            for (std::size_t i = 0u; i < text.size(); ++i)
            {
                std::size_t units;
                auto const cp = code_point_at(text, i, units);
                if (cp >= 0xD800u && cp <= 0xDFFFu)
                    text[i] = u'\xFFFD';
                i += units - 1u;
            }
            return make_string(std::move(text));
        }

        value string_last_index_of(value aThis, arguments aArguments, value, function_object&)
        {
            auto const s = this_string(aThis, "lastIndexOf");
            auto const searchString = to_string(argument(aArguments, 0u));
            auto const numPos = to_number(argument(aArguments, 1u));
            auto const& text = text_of(s);
            auto const& searchText = text_of(searchString);
            double pos = std::isnan(numPos) ? std::numeric_limits<double>::infinity() : std::trunc(numPos);
            auto const len = static_cast<double>(text.size());
            auto const start = clamp_index(pos, len);
            if (searchText.size() > text.size())
                return number(-1.0);
            auto const found = text.rfind(searchText, static_cast<std::size_t>(start));
            return number(found == std::u16string::npos ? -1.0 : static_cast<double>(found));
        }

        value string_locale_compare(value aThis, arguments aArguments, value, function_object&)
        {
            auto const s = this_string(aThis, "localeCompare");
            auto const that = to_string(argument(aArguments, 0u));
#if defined(NEOS_JS_ICU)
            return intl::locale_compare(s, that, argument(aArguments, 1u), argument(aArguments, 2u));
#endif
            auto const a = utf8(normalize_string(s, u"NFC"));
            auto const b = utf8(normalize_string(that, u"NFC"));
            return number(a < b ? -1.0 : a > b ? 1.0 : 0.0);
        }

        value string_normalize(value aThis, arguments aArguments, value, function_object&)
        {
            auto const s = this_string(aThis, "normalize");
            auto const form = argument(aArguments, 0u);
            std::u16string const f = form == Undefined ? std::u16string{ u"NFC" } : std::u16string{ str(to_string(form)) };
            if (f != u"NFC" && f != u"NFD" && f != u"NFKC" && f != u"NFKD")
                throw_range_error("The normalization form should be one of NFC, NFD, NFKC, NFKD.");
            return normalize_string(s, f);
        }

        value pad(value aThis, arguments aArguments, bool aStart)
        {
            auto const s = this_string(aThis, aStart ? "padStart" : "padEnd");
            auto const intMaxLength = to_length(argument(aArguments, 0u));
            auto const& text = text_of(s);
            auto const stringLength = static_cast<double>(text.size());
            if (intMaxLength <= stringLength)
                return s;
            std::u16string filler = u" ";
            if (argument(aArguments, 1u) != Undefined)
                filler = str(to_string(aArguments[1]));
            if (filler.empty())
                return s;
            if (intMaxLength > MaxStringLength)
                throw_range_error("Invalid string length");
            auto const fillLen = static_cast<std::size_t>(intMaxLength - stringLength);
            std::u16string fill;
            fill.reserve(fillLen);
            while (fill.size() < fillLen)
                fill += filler;
            fill.resize(fillLen);
            return make_string(aStart ? fill + std::u16string{ text } : std::u16string{ text } + fill);
        }

        value string_pad_end(value aThis, arguments aArguments, value, function_object&) { return pad(aThis, aArguments, false); }
        value string_pad_start(value aThis, arguments aArguments, value, function_object&) { return pad(aThis, aArguments, true); }

        value string_repeat(value aThis, arguments aArguments, value, function_object&)
        {
            auto const s = this_string(aThis, "repeat");
            auto const n = to_integer_or_infinity(argument(aArguments, 0u));
            if (n < 0.0 || n == std::numeric_limits<double>::infinity())
                throw_range_error("Invalid count value: " + describe(argument(aArguments, 0u)));
            auto const& text = text_of(s);
            if (n == 0.0 || text.empty())
                return the_names().empty;
            if (static_cast<double>(text.size()) * n > MaxStringLength)
                throw_range_error("Invalid string length");
            std::u16string result;
            result.reserve(static_cast<std::size_t>(text.size() * n));
            for (double i = 0.0; i < n; ++i)
                result += text;
            return make_string(std::move(result));
        }

        value string_slice(value aThis, arguments aArguments, value, function_object&)
        {
            auto const s = this_string(aThis, "slice");
            auto const& text = text_of(s);
            auto const len = static_cast<double>(text.size());
            auto const intStart = to_integer_or_infinity(argument(aArguments, 0u));
            double from = intStart == -std::numeric_limits<double>::infinity() ? 0.0 : intStart < 0.0 ? std::max(len + intStart, 0.0) : std::min(intStart, len);
            double to = len;
            if (argument(aArguments, 1u) != Undefined)
            {
                auto const intEnd = to_integer_or_infinity(aArguments[1]);
                to = intEnd == -std::numeric_limits<double>::infinity() ? 0.0 : intEnd < 0.0 ? std::max(len + intEnd, 0.0) : std::min(intEnd, len);
            }
            if (from >= to)
                return the_names().empty;
            return substring_of(s, static_cast<std::size_t>(from), static_cast<std::size_t>(to - from));
        }

        value string_starts_with(value aThis, arguments aArguments, value, function_object&)
        {
            auto const s = this_string(aThis, "startsWith");
            auto const search = argument(aArguments, 0u);
            if (is_regexp(search))
                throw_type_error("First argument to String.prototype.startsWith must not be a regular expression");
            auto const searchString = to_string(search);
            auto const& text = text_of(s);
            auto const start = clamp_index(to_integer_or_infinity(argument(aArguments, 1u)), static_cast<double>(text.size()));
            auto const& searchText = text_of(searchString);
            if (start + static_cast<double>(searchText.size()) > static_cast<double>(text.size()))
                return False;
            return boolean(std::u16string_view{ text }.substr(static_cast<std::size_t>(start), searchText.size()) == searchText);
        }

        value string_substring(value aThis, arguments aArguments, value, function_object&)
        {
            auto const s = this_string(aThis, "substring");
            auto const& text = text_of(s);
            auto const len = static_cast<double>(text.size());
            auto const intStart = to_integer_or_infinity(argument(aArguments, 0u));
            auto const intEnd = argument(aArguments, 1u) == Undefined ? len : to_integer_or_infinity(aArguments[1]);
            auto const finalStart = clamp_index(intStart, len);
            auto const finalEnd = clamp_index(intEnd, len);
            auto const from = std::min(finalStart, finalEnd);
            auto const to = std::max(finalStart, finalEnd);
            return substring_of(s, static_cast<std::size_t>(from), static_cast<std::size_t>(to - from));
        }

        value string_substr(value aThis, arguments aArguments, value, function_object&)
        {
            auto const s = this_string(aThis, "substr");
            auto const& text = text_of(s);
            auto const size = static_cast<double>(text.size());
            auto intStart = to_integer_or_infinity(argument(aArguments, 0u));
            if (intStart == -std::numeric_limits<double>::infinity())
                intStart = 0.0;
            else if (intStart < 0.0)
                intStart = std::max(size + intStart, 0.0);
            else
                intStart = std::min(intStart, size);
            auto const intLength = argument(aArguments, 1u) == Undefined ? size : to_integer_or_infinity(aArguments[1]);
            auto const intEnd = std::clamp(intStart + intLength, intStart, size);
            if (intEnd <= intStart)
                return the_names().empty;
            return substring_of(s, static_cast<std::size_t>(intStart), static_cast<std::size_t>(intEnd - intStart));
        }

        value string_to_lower_case(value aThis, arguments, value, function_object&)
        {
            return make_string(case_map(text_of(this_string(aThis, "toLowerCase")), false));
        }

        value string_to_upper_case(value aThis, arguments, value, function_object&)
        {
            return make_string(case_map(text_of(this_string(aThis, "toUpperCase")), true));
        }

        value string_to_locale_lower_case(value aThis, arguments aArguments, value, function_object&)
        {
            auto const s = this_string(aThis, "toLocaleLowerCase");
#if defined(NEOS_JS_ICU)
            return intl::to_locale_case(s, argument(aArguments, 0u), false);
#else
            (void)aArguments;
            return make_string(case_map(text_of(s), false));
#endif
        }

        value string_to_locale_upper_case(value aThis, arguments aArguments, value, function_object&)
        {
            auto const s = this_string(aThis, "toLocaleUpperCase");
#if defined(NEOS_JS_ICU)
            return intl::to_locale_case(s, argument(aArguments, 0u), true);
#else
            (void)aArguments;
            return make_string(case_map(text_of(s), true));
#endif
        }

        value string_to_string(value aThis, arguments, value, function_object&)
        {
            return this_string_value(aThis, "toString");
        }

        value string_value_of(value aThis, arguments, value, function_object&)
        {
            return this_string_value(aThis, "valueOf");
        }

        value trim(value aThis, bool aStart, bool aEnd, char const* aMethod)
        {
            auto const s = this_string(aThis, aMethod);
            auto const& text = text_of(s);
            return make_string(es::trim(std::u16string_view{ text }, aStart, aEnd));
        }

        value string_trim(value aThis, arguments, value, function_object&) { return trim(aThis, true, true, "trim"); }
        value string_trim_start(value aThis, arguments, value, function_object&) { return trim(aThis, true, false, "trimStart"); }
        value string_trim_end(value aThis, arguments, value, function_object&) { return trim(aThis, false, true, "trimEnd"); }

        // (GetSubstitution: $$ $& $` $' $n $nn $<name>)
        std::u16string get_substitution(std::u16string_view aMatched, std::u16string_view aString, std::size_t aPosition,
            std::span<value const> aCaptures, value aNamedCaptures, std::u16string_view aReplacement)
        {
            std::u16string result;
            auto const m = aCaptures.size();
            for (std::size_t i = 0u; i < aReplacement.size(); ++i)
            {
                auto const c = aReplacement[i];
                if (c != u'$' || i + 1u == aReplacement.size())
                {
                    result.push_back(c);
                    continue;
                }
                auto const n = aReplacement[i + 1u];
                if (n == u'$')
                {
                    result.push_back(u'$');
                    ++i;
                }
                else if (n == u'&')
                {
                    result += aMatched;
                    ++i;
                }
                else if (n == u'`')
                {
                    result += aString.substr(0u, aPosition);
                    ++i;
                }
                else if (n == u'\'')
                {
                    auto const tailPos = std::min(aPosition + aMatched.size(), aString.size());
                    result += aString.substr(tailPos);
                    ++i;
                }
                else if (n >= u'0' && n <= u'9')
                {
                    // ($n or $nn: a capture (the longest that is one))
                    std::size_t digits = 1u;
                    std::size_t index = static_cast<std::size_t>(n - u'0');
                    if (i + 2u < aReplacement.size() && aReplacement[i + 2u] >= u'0' && aReplacement[i + 2u] <= u'9')
                    {
                        auto const two = index * 10u + static_cast<std::size_t>(aReplacement[i + 2u] - u'0');
                        if (two >= 1u && two <= m)
                        {
                            index = two;
                            digits = 2u;
                        }
                    }
                    if (index >= 1u && index <= m)
                    {
                        auto const capture = aCaptures[index - 1u];
                        if (capture != Undefined)
                            result += str(capture);
                        i += digits;
                    }
                    else
                        result.push_back(c);
                }
                else if (n == u'<')
                {
                    if (aNamedCaptures == Undefined)
                    {
                        result.push_back(c);
                        continue;
                    }
                    auto const close = aReplacement.find(u'>', i + 2u);
                    if (close == std::u16string::npos)
                    {
                        result.push_back(c);
                        continue;
                    }
                    auto const groupName = make_string(std::u16string_view{ aReplacement }.substr(i + 2u, close - (i + 2u)));
                    auto const capture = get(aNamedCaptures, intern(groupName));
                    if (capture != Undefined)
                        result += str(to_string(capture));
                    i = close;
                }
                else
                    result.push_back(c);
            }
            return result;
        }

        value string_replace_common(value aThis, arguments aArguments, bool aAll)
        {
            if (is_nullish(aThis))
                throw_type_error(std::string{ "String.prototype." } + (aAll ? "replaceAll" : "replace") + " called on null or undefined");
            auto const searchValue = argument(aArguments, 0u);
            auto replaceValue = argument(aArguments, 1u);
            if (is_object(searchValue))
            {
                if (aAll && is_regexp(searchValue))
                {
                    auto const flags = get(searchValue, ascii("flags"));
                    if (is_nullish(flags))
                        throw_type_error("String.prototype.replaceAll called with a non-global RegExp argument");
                    if (str(to_string(flags)).find(u'g') == std::u16string::npos)
                        throw_type_error("replaceAll must be called with a global RegExp");
                }
                auto const replacer = get_method(searchValue, the_realm().symReplace);
                if (replacer != Undefined)
                {
                    value const args[] = { aThis, replaceValue };
                    return call(replacer, searchValue, args);
                }
            }
            auto const s = to_string(aThis);
            auto const searchString = to_string(searchValue);
            bool const functional = is_callable(replaceValue);
            if (!functional)
                replaceValue = to_string(replaceValue);
            auto const& text = text_of(s);
            auto const& searchText = text_of(searchString);
            auto const advanceBy = std::max<std::size_t>(1u, searchText.size());
            std::vector<std::size_t> positions;
            auto position = text.find(searchText);
            while (position != std::u16string::npos)
            {
                positions.push_back(position);
                if (!aAll)
                    break;
                position = position + advanceBy > text.size() ? std::u16string::npos : text.find(searchText, position + advanceBy);
            }
            std::u16string result;
            std::size_t endOfLastMatch = 0u;
            for (auto p : positions)
            {
                result += text.substr(endOfLastMatch, p - endOfLastMatch);
                std::u16string replacement;
                if (functional)
                {
                    value const args[] = { searchString, number(static_cast<double>(p)), s };
                    replacement = str(to_string(call(replaceValue, Undefined, args)));
                }
                else
                    replacement = get_substitution(searchText, text, p, {}, Undefined, str(replaceValue));
                result += replacement;
                endOfLastMatch = p + searchText.size();
            }
            if (positions.empty())
                return s;
            result += text.substr(std::min(endOfLastMatch, text.size()));
            return make_string(std::move(result));
        }

        value string_replace(value aThis, arguments aArguments, value, function_object&) { return string_replace_common(aThis, aArguments, false); }
        value string_replace_all(value aThis, arguments aArguments, value, function_object&) { return string_replace_common(aThis, aArguments, true); }

        // (match, matchAll and search: the argument's method, or a new RegExp's)
        value regexp_method(value aThis, arguments aArguments, value realm::*aSymbol, char const* aMethod, bool aMatchAll)
        {
            if (is_nullish(aThis))
                throw_type_error(std::string{ "String.prototype." } + aMethod + " called on null or undefined");
            auto const regexp = argument(aArguments, 0u);
            if (is_object(regexp))
            {
                if (aMatchAll && is_regexp(regexp))
                {
                    auto const flags = get(regexp, ascii("flags"));
                    if (is_nullish(flags))
                        throw_type_error("String.prototype.matchAll called with a non-global RegExp argument");
                    if (str(to_string(flags)).find(u'g') == std::u16string::npos)
                        throw_type_error("String.prototype.matchAll called with a non-global RegExp argument");
                }
                auto const method = get_method(regexp, the_realm().*aSymbol);
                if (method != Undefined)
                {
                    value const args[] = { aThis };
                    return call(method, regexp, args);
                }
            }
            auto const s = to_string(aThis);
            auto const rx = make_regexp(regexp == Undefined ? the_names().empty : to_string(regexp), aMatchAll ? ascii("g") : Undefined);
            value const args[] = { s };
            return invoke(rx, the_realm().*aSymbol, args);
        }

        value string_match(value aThis, arguments aArguments, value, function_object&) { return regexp_method(aThis, aArguments, &realm::symMatch, "match", false); }
        value string_match_all(value aThis, arguments aArguments, value, function_object&) { return regexp_method(aThis, aArguments, &realm::symMatchAll, "matchAll", true); }
        value string_search(value aThis, arguments aArguments, value, function_object&) { return regexp_method(aThis, aArguments, &realm::symSearch, "search", false); }

        value string_split(value aThis, arguments aArguments, value, function_object&)
        {
            if (is_nullish(aThis))
                throw_type_error("String.prototype.split called on null or undefined");
            auto const separator = argument(aArguments, 0u);
            auto const limit = argument(aArguments, 1u);
            if (is_object(separator))
            {
                auto const splitter = get_method(separator, the_realm().symSplit);
                if (splitter != Undefined)
                {
                    value const args[] = { aThis, limit };
                    return call(splitter, separator, args);
                }
            }
            auto const s = to_string(aThis);
            auto const lim = limit == Undefined ? 4294967295.0 : static_cast<double>(to_uint32(limit));
            auto const r = to_string(separator);
            if (lim == 0.0)
                return make_array();
            if (separator == Undefined)
            {
                value const element[] = { s };
                return create_array_from_list(element);
            }
            auto const& text = text_of(s);
            auto const& separatorText = text_of(r);
            value_list parts;
            if (separatorText.empty())
            {
                for (std::size_t i = 0u; i < text.size() && static_cast<double>(parts.size()) < lim; ++i)
                    parts.push_back(make_string(text[i]));
                return make_array(std::move(parts));
            }
            if (text.empty())
            {
                value const element[] = { s };
                return create_array_from_list(element);
            }
            std::size_t i = 0u;
            for (auto j = text.find(separatorText); j != std::u16string::npos; j = text.find(separatorText, i))
            {
                parts.push_back(make_string(std::u16string_view{ text }.substr(i, j - i)));
                if (static_cast<double>(parts.size()) >= lim)
                    return make_array(std::move(parts));
                i = j + separatorText.size();
            }
            parts.push_back(make_string(std::u16string_view{ text }.substr(i)));
            return make_array(std::move(parts));
        }

        value string_iterator_function(value aThis, arguments, value, function_object&)
        {
            auto const s = this_string(aThis, "[Symbol.iterator]");
            return make_string_iterator(s);
        }

        // (Annex B: CreateHTML)
        value create_html(value aThis, char const* aMethod, char const* aTag, char const* aAttribute, value aValue)
        {
            auto const s = this_string(aThis, aMethod);
            std::u16string p1 = u"<" + utf16_ascii(aTag);
            if (aAttribute != nullptr)
            {
                auto v = str(to_string(aValue));
                std::u16string escaped;
                for (auto c : v)
                    if (c == u'"')
                        escaped += u"&quot;";
                    else
                        escaped.push_back(c);
                p1 += u" " + utf16_ascii(aAttribute) + u"=\"" + escaped + u"\"";
            }
            p1 += u">" + std::u16string{ str(s) } + u"</" + utf16_ascii(aTag) + u">";
            return make_string(std::move(p1));
        }

#define NEOS_JS_HTML(aFunction, aMethod, aTag, aAttribute) \
        value aFunction(value aThis, arguments aArguments, value, function_object&) { return create_html(aThis, aMethod, aTag, aAttribute, argument(aArguments, 0u)); }
        NEOS_JS_HTML(string_anchor, "anchor", "a", "name")
        NEOS_JS_HTML(string_big, "big", "big", nullptr)
        NEOS_JS_HTML(string_blink, "blink", "blink", nullptr)
        NEOS_JS_HTML(string_bold, "bold", "b", nullptr)
        NEOS_JS_HTML(string_fixed, "fixed", "tt", nullptr)
        NEOS_JS_HTML(string_fontcolor, "fontcolor", "font", "color")
        NEOS_JS_HTML(string_fontsize, "fontsize", "font", "size")
        NEOS_JS_HTML(string_italics, "italics", "i", nullptr)
        NEOS_JS_HTML(string_link, "link", "a", "href")
        NEOS_JS_HTML(string_small, "small", "small", nullptr)
        NEOS_JS_HTML(string_strike, "strike", "strike", nullptr)
        NEOS_JS_HTML(string_sub, "sub", "sub", nullptr)
        NEOS_JS_HTML(string_sup, "sup", "sup", nullptr)
#undef NEOS_JS_HTML

        // --- the string iterator

        class string_iterator : public object
        {
        public:
            explicit string_iterator(value aPrototype) : object{ aPrototype, object_class::StringIterator } {}
        public:
            void trace(tracer& aTracer) override
            {
                object::trace(aTracer);
                aTracer.mark(iterated);
            }
        public:
            value iterated = Undefined;
            std::size_t position = 0u;
        };

        value string_iterator_next(value aThis, arguments, value, function_object&)
        {
            if (!is_object(aThis) || obj(aThis).cls != object_class::StringIterator)
                throw_type_error("next method called on incompatible receiver " + describe(aThis));
            auto& it = static_cast<string_iterator&>(obj(aThis));
            if (it.iterated == Undefined)
                return create_iter_result_object(Undefined, true);
            auto const& text = str(it.iterated);
            if (it.position >= text.size())
            {
                it.iterated = Undefined;
                return create_iter_result_object(Undefined, true);
            }
            std::size_t units;
            code_point_at(text, it.position, units);
            auto const result = make_string(std::u16string_view{ text }.substr(it.position, units));
            it.position += units;
            return create_iter_result_object(result, false);
        }
    }

    value make_string_iterator(value aString)
    {
        auto& it = the_heap().make<string_iterator>(the_realm().stringIteratorPrototype);
        it.iterated = aString;
        return value_of(it);
    }

    bool char_code_at(value aCallee, value aThis, value aPosition, value& aResult)
    {
        auto const* f = as_function(aCallee);
        if (f == nullptr || f->native != &string_char_code_at || !is_string(aThis) || !is_number(aPosition))
            return false;
        auto const text = cell_of<string>(aThis).chars();
        auto const n = as_number(aPosition);
        auto const position = std::isnan(n) ? 0.0 : std::trunc(n);  // (ToIntegerOrInfinity)
        if (position < 0.0 || position >= static_cast<double>(text.size()))
            aResult = CanonicalNaN;
        else
            aResult = number(text[static_cast<std::size_t>(position)]);
        return true;
    }

    value string_slice_direct(value aCallee, value aThis, value aStart, value aEnd)
    {
        // (a string's, with numbers: its ToIntegerOrInfinity's (which call nothing) relative to its length (as the built-in's are))
        if (is_string(aThis) && is_number(aStart) && (aEnd == Undefined || is_number(aEnd)))
        {
            auto const length = static_cast<double>(cell_of<string>(aThis).length);
            auto const relative = [length](double aPosition)
            {
                if (std::isnan(aPosition))
                    return 0.0;
                aPosition = std::trunc(aPosition);
                return aPosition < 0.0 ? std::max(length + aPosition, 0.0) : std::min(aPosition, length);
            };
            auto const from = relative(as_number(aStart));
            auto const to = aEnd == Undefined ? length : relative(as_number(aEnd));
            if (from >= to)
                return the_names().empty;
            return substring_of(aThis, static_cast<std::size_t>(from), static_cast<std::size_t>(to - from));
        }
        value const parameters[] = { aStart, aEnd };
        return string_slice(aThis, parameters, Undefined, *as_function(aCallee));
    }

    std::u16string get_substitution_text(std::u16string_view aMatched, std::u16string_view aString, std::size_t aPosition,
        std::span<value const> aCaptures, value aNamedCaptures, std::u16string_view aReplacement)
    {
        return get_substitution(aMatched, aString, aPosition, aCaptures, aNamedCaptures, aReplacement);
    }

    void init_string(realm& aRealm)
    {
        aRealm.stringPrototype = value_of(the_heap().make<string_object>(aRealm.objectPrototype));
        auto& p = obj(aRealm.stringPrototype);
        p.internal() = the_names().empty;
        p.properties.add(the_names().length, number(0.0), Undefined, 0u);
        auto const c = make_native("String", 1u, string_constructor, true);
        aRealm.string = c;
        obj(c).put(the_names().prototype, aRealm.stringPrototype, 0u);
        p.put(the_names().constructor, c);
        method(c, "fromCharCode", 1u, string_from_char_code);
        method(c, "fromCodePoint", 1u, string_from_code_point);
        method(c, "raw", 1u, string_raw);
        auto const pr = aRealm.stringPrototype;
        method(pr, "at", 1u, string_at);
        method(pr, "charAt", 1u, string_char_at);
        aRealm.charCodeAt = method(pr, "charCodeAt", 1u, string_char_code_at);
        method(pr, "codePointAt", 1u, string_code_point_at);
        method(pr, "concat", 1u, string_concat);
        method(pr, "endsWith", 1u, string_ends_with);
        method(pr, "includes", 1u, string_includes);
        aRealm.stringIndexOf = method(pr, "indexOf", 1u, string_index_of);
        method(pr, "isWellFormed", 0u, string_is_well_formed);
        method(pr, "lastIndexOf", 1u, string_last_index_of);
        method(pr, "localeCompare", 1u, string_locale_compare);
        method(pr, "match", 1u, string_match);
        method(pr, "matchAll", 1u, string_match_all);
        method(pr, "normalize", 0u, string_normalize);
        method(pr, "padEnd", 1u, string_pad_end);
        method(pr, "padStart", 1u, string_pad_start);
        method(pr, "repeat", 1u, string_repeat);
        method(pr, "replace", 2u, string_replace);
        method(pr, "replaceAll", 2u, string_replace_all);
        method(pr, "search", 1u, string_search);
        aRealm.stringSlice = method(pr, "slice", 2u, string_slice);
        method(pr, "split", 2u, string_split);
        method(pr, "startsWith", 1u, string_starts_with);
        method(pr, "substr", 2u, string_substr);
        method(pr, "substring", 2u, string_substring);
        method(pr, "toLocaleLowerCase", 0u, string_to_locale_lower_case);
        method(pr, "toLocaleUpperCase", 0u, string_to_locale_upper_case);
        method(pr, "toLowerCase", 0u, string_to_lower_case);
        method(pr, "toString", 0u, string_to_string);
        method(pr, "toUpperCase", 0u, string_to_upper_case);
        method(pr, "toWellFormed", 0u, string_to_well_formed);
        method(pr, "trim", 0u, string_trim);
        auto const trimStart = method(pr, "trimStart", 0u, string_trim_start);
        auto const trimEnd = method(pr, "trimEnd", 0u, string_trim_end);
        p.put(ascii("trimLeft"), trimStart);
        p.put(ascii("trimRight"), trimEnd);
        method(pr, "valueOf", 0u, string_value_of);
        method(pr, "anchor", 1u, string_anchor);
        method(pr, "big", 0u, string_big);
        method(pr, "blink", 0u, string_blink);
        method(pr, "bold", 0u, string_bold);
        method(pr, "fixed", 0u, string_fixed);
        method(pr, "fontcolor", 1u, string_fontcolor);
        method(pr, "fontsize", 1u, string_fontsize);
        method(pr, "italics", 0u, string_italics);
        method(pr, "link", 1u, string_link);
        method(pr, "small", 0u, string_small);
        method(pr, "strike", 0u, string_strike);
        method(pr, "sub", 0u, string_sub);
        method(pr, "sup", 0u, string_sup);
        auto const iterator = make_native(aRealm.symIterator, 0u, string_iterator_function);
        p.put(aRealm.symIterator, iterator);
        aRealm.stringIteratorPrototype = make_object(aRealm.iteratorPrototype);
        method(aRealm.stringIteratorPrototype, "next", 0u, string_iterator_next);
        obj(aRealm.stringIteratorPrototype).put(aRealm.symToStringTag, ascii("String Iterator"), AttributeConfigurable);
        obj(aRealm.global).put(ascii("String"), c);
    }
}
