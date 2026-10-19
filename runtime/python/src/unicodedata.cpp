/*
  unicodedata.cpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  This program is free software: you can redistribute it and / or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  Portions of this file are derived from CPython 3.13 (Modules/unicodedata.c), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE).

  The Unicode Character Database: Copyright © 1991-2026 Unicode, Inc., used under the Unicode License V3 (see
  runtime/python/NOTICE).
*/

// The Unicode database (UCD 15.1.0, CPython 3.13's): the properties of characters that str's methods use, and the module
// unicodedata (its functions, normalization (CPython's algorithms) and ucd_3_2_0)

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <neos/language/unicode_names.hpp>
#include "objects.hpp"
#include "unicode_tables.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            namespace ut = unicode_tables;
            namespace un = neos::language::python::unicode_names;

            static_assert(ut::ALPHA == unicode_flag::ALPHA && ut::DECIMAL == unicode_flag::DECIMAL && ut::DIGIT == unicode_flag::DIGIT &&
                ut::NUMERIC == unicode_flag::NUMERIC && ut::LOWER == unicode_flag::LOWER && ut::UPPER == unicode_flag::UPPER && ut::TITLE == unicode_flag::TITLE &&
                ut::SPACE == unicode_flag::SPACE && ut::LINEBREAK == unicode_flag::LINEBREAK && ut::PRINTABLE == unicode_flag::PRINTABLE &&
                ut::CASED == unicode_flag::CASED && ut::CASE_IGNORABLE == unicode_flag::CASE_IGNORABLE && ut::XID_START == unicode_flag::XID_START &&
                ut::XID_CONTINUE == unicode_flag::XID_CONTINUE && ut::MIRRORED == unicode_flag::MIRRORED);

            ut::record const& record_of(char32_t aCharacter)
            {
                auto const cp = static_cast<std::uint32_t>(aCharacter);
                if (cp >= 0x110000u)
                    return ut::sRecords[0];
                auto const block = ut::sIndex1[cp >> ut::SHIFT];
                return ut::sRecords[ut::sIndex2[(static_cast<std::uint32_t>(block) << ut::SHIFT) + (cp & ((1u << ut::SHIFT) - 1u))]];
            }

            // (UCD 3.2.0's record of a character: null if it is the same as UCD 15.1.0's)
            ut::old_record const* old_record_of(char32_t aCharacter)
            {
                auto const cp = static_cast<std::uint32_t>(aCharacter);
                auto const i = std::upper_bound(std::begin(ut::sOldRecords), std::end(ut::sOldRecords), cp,
                    [](std::uint32_t aCodePoint, ut::old_record const& aRecord) { return aCodePoint < aRecord.first; });
                if (i == std::begin(ut::sOldRecords))
                    return nullptr;
                auto const& r = *(i - 1);
                return cp >= r.first && cp <= r.last ? &r : nullptr;
            }

            bool unassigned_in_old(char32_t aCharacter)
            {
                auto const r = old_record_of(aCharacter);
                return r != nullptr && r->category == 0u;
            }

            // (a character's decomposition: its tag (0: canonical) and characters (false if it has none))
            bool decomposition_of(char32_t aCharacter, bool aOld, std::uint32_t& aTag, std::u32string& aResult)
            {
                auto const cp = static_cast<std::uint32_t>(aCharacter);
                auto const find = [&](std::uint32_t const* aCodePoints, std::size_t aCount, std::uint32_t const* aOffsets, std::uint32_t const* aData) -> int
                {
                    auto const i = std::lower_bound(aCodePoints, aCodePoints + aCount, cp);
                    if (i == aCodePoints + aCount || *i != cp)
                        return 0;
                    auto const index = static_cast<std::size_t>(i - aCodePoints);
                    auto const start = aOffsets[index];
                    auto const end = aOffsets[index + 1u];
                    if (aData[start] == 0xFFu && end == start + 1u)
                        return -1;  // (none)
                    aTag = aData[start];
                    aResult.clear();
                    for (auto k = start + 1u; k < end; ++k)
                        aResult += static_cast<char32_t>(aData[k]);
                    return 1;
                };
                if (aOld)
                {
                    auto const r = find(ut::sOldDecompositionCodePoints, std::size(ut::sOldDecompositionCodePoints), ut::sOldDecompositionOffsets,
                        ut::sOldDecompositionData);
                    if (r != 0)
                        return r > 0;
                }
                return find(ut::sDecompositionCodePoints, std::size(ut::sDecompositionCodePoints), ut::sDecompositionOffsets, ut::sDecompositionData) > 0;
            }

            // --- normalization (CPython's nfd_nfkd and nfc_nfkc)

            constexpr char32_t SBase = 0xAC00u, LBase = 0x1100u, VBase = 0x1161u, TBase = 0x11A7u;
            constexpr char32_t LCount = 19u, VCount = 21u, TCount = 28u, NCount = VCount * TCount, SCount = LCount * NCount;

            // (UCD 3.2.0's corrections of normalization (CPython's normalization_3_2_0))
            char32_t old_normalization(char32_t aCharacter)
            {
                switch (aCharacter)
                {
                case 0x2F868u: return 0x2136Au;
                case 0x2F874u: return 0x5F33u;
                case 0x2F91Fu: return 0x43ABu;
                case 0x2F95Fu: return 0x7AAEu;
                case 0x2F9BFu: return 0x4D57u;
                default: return 0u;
                }
            }

            std::u32string decomposed(std::u32string const& aInput, bool aCompatibility, bool aOld)
            {
                std::u32string output;
                output.reserve(aInput.size());
                std::vector<char32_t> stack;
                std::u32string decomposition;
                for (auto c : aInput)
                {
                    stack.push_back(c);
                    while (!stack.empty())
                    {
                        auto const code = stack.back();
                        stack.pop_back();
                        if (code >= SBase && code < SBase + SCount)
                        {
                            auto const s = code - SBase;
                            output += LBase + s / NCount;
                            output += VBase + (s % NCount) / TCount;
                            if (auto const t = TBase + s % TCount; t != TBase)
                                output += t;
                            continue;
                        }
                        if (aOld)
                            if (auto const value = old_normalization(code); value != 0u)
                            {
                                stack.push_back(value);
                                continue;
                            }
                        std::uint32_t tag = 0u;
                        if ((aOld && unassigned_in_old(code)) || !decomposition_of(code, false, tag, decomposition) || (tag != 0u && !aCompatibility))
                        {
                            output += code;
                            continue;
                        }
                        for (auto i = decomposition.size(); i-- > 0u;)
                            stack.push_back(decomposition[i]);
                    }
                }
                // (canonical ordering)
                if (output.empty())
                    return output;
                auto prev = record_of(output[0]).combining;
                for (std::size_t i = 1u; i < output.size(); ++i)
                {
                    auto const cur = record_of(output[i]).combining;
                    if (prev == 0u || cur == 0u || prev <= cur)
                    {
                        prev = cur;
                        continue;
                    }
                    for (auto o = static_cast<std::ptrdiff_t>(i) - 1;;)
                    {
                        std::swap(output[static_cast<std::size_t>(o) + 1u], output[static_cast<std::size_t>(o)]);
                        --o;
                        if (o < 0)
                            break;
                        prev = record_of(output[static_cast<std::size_t>(o)]).combining;
                        if (prev == 0u || prev <= cur)
                            break;
                    }
                    prev = record_of(output[i]).combining;
                }
                return output;
            }

            bool composes_first(char32_t aCharacter)
            {
                auto const key = static_cast<std::uint64_t>(aCharacter) << 21;
                auto const i = std::lower_bound(std::begin(ut::sCompositionKeys), std::end(ut::sCompositionKeys), key);
                return i != std::end(ut::sCompositionKeys) && (*i >> 21) == aCharacter;
            }

            bool composes_last(char32_t aCharacter)
            {
                static std::vector<std::uint32_t> const sLasts = []()
                {
                    std::vector<std::uint32_t> result;
                    for (auto k : ut::sCompositionKeys)
                        result.push_back(static_cast<std::uint32_t>(k & 0x1FFFFFu));
                    std::sort(result.begin(), result.end());
                    result.erase(std::unique(result.begin(), result.end()), result.end());
                    return result;
                }();
                return std::binary_search(sLasts.begin(), sLasts.end(), static_cast<std::uint32_t>(aCharacter));
            }

            char32_t composition_of(char32_t aFirst, char32_t aLast)
            {
                auto const key = (static_cast<std::uint64_t>(aFirst) << 21) | aLast;
                auto const i = std::lower_bound(std::begin(ut::sCompositionKeys), std::end(ut::sCompositionKeys), key);
                if (i == std::end(ut::sCompositionKeys) || *i != key)
                    return 0u;
                return static_cast<char32_t>(ut::sCompositionValues[i - std::begin(ut::sCompositionKeys)]);
            }

            std::u32string composed(std::u32string const& aInput, bool aCompatibility, bool aOld)
            {
                auto const data = decomposed(aInput, aCompatibility, aOld);
                auto const len = data.size();
                std::u32string output(len, U'\0');
                std::vector<std::size_t> skipped;
                std::size_t i = 0u, o = 0u;
                while (i < len)
                {
                    if (auto const s = std::find(skipped.begin(), skipped.end(), i); s != skipped.end())
                    {
                        *s = skipped.back();
                        skipped.pop_back();
                        ++i;
                        continue;
                    }
                    auto code = data[i];
                    if (code >= LBase && code < LBase + LCount && i + 1u < len && data[i + 1u] >= VBase && data[i + 1u] < VBase + VCount)
                    {
                        code = SBase + ((code - LBase) * VCount + (data[i + 1u] - VBase)) * TCount;
                        i += 2u;
                        if (i < len && data[i] > TBase && data[i] < TBase + TCount)
                            code += data[i++] - TBase;
                        output[o++] = code;
                        continue;
                    }
                    if (!composes_first(code))
                    {
                        output[o++] = code;
                        ++i;
                        continue;
                    }
                    auto i1 = i + 1u;
                    unsigned comb = 0u;
                    output[o] = data[i];
                    while (i1 < len)
                    {
                        auto const code1 = data[i1];
                        unsigned const comb1 = record_of(code1).combining;
                        if (comb != 0u)
                        {
                            if (comb1 == 0u)
                                break;
                            if (comb >= comb1)
                            {
                                ++i1;
                                continue;
                            }
                        }
                        char32_t composite = 0u;
                        if (composes_last(code1))
                            composite = composition_of(output[o], code1);
                        if (composite == 0u)
                        {
                            if (comb1 == 0u)
                                break;
                            comb = comb1;
                            ++i1;
                            continue;
                        }
                        output[o] = composite;
                        skipped.push_back(i1);
                        ++i1;
                        if (!composes_first(output[o]))
                            break;
                    }
                    ++o;
                    ++i;
                }
                output.resize(o);
                return output;
            }

            // --- the module's functions (aOld: ucd_3_2_0's)

            object* sUcd32 = nullptr;     // (ucd_3_2_0)

            bool is_old(std::vector<object*> const& aArgs, bool aMethod) { return aMethod && !aArgs.empty() && aArgs[0] == sUcd32; }

            // (the character argument: aSecond: the function has a second parameter (CPython's message then numbers it))
            bool character_argument(char const* aName, object* aArgument, bool aSecond, char32_t& aResult)
            {
                if (kind_of(aArgument) == NEOS_PY_STR)
                {
                    auto const& text = as<str_object>(aArgument)->value;
                    auto const points = to_code_points(text);
                    if (points.size() == 1u)
                    {
                        aResult = points[0];
                        return true;
                    }
                }
                error("TypeError", std::string{ aName } + "() argument " + (aSecond ? "1 " : "") + "must be a unicode character, not " + type_name(aArgument));
                return false;
            }

            // (the arguments of a function of a character (and, aDefault, a default value): method: self first)
            bool arguments(char const* aName, std::vector<object*> const& aArgs, object* aKwargs, bool aMethod, bool aDefault, char32_t& aCharacter, object*& aDefaultValue)
            {
                std::vector<object*> a;
                std::vector<object*> const args{ aArgs.begin() + (aMethod ? 1 : 0), aArgs.end() };
                if (!parse_arguments(aName, args, aKwargs, aDefault ? std::vector<char const*>{ "chr", "default" } : std::vector<char const*>{ "chr" }, 1u,
                    aDefault ? 2u : 1u, a))
                    return false;
                aDefaultValue = aDefault ? a[1] : nullptr;
                return character_argument(aName, a[0], aDefault, aCharacter);
            }

            object* category(std::vector<object*> const& aArgs, object* aKwargs, bool aMethod)
            {
                char32_t c = 0u;
                object* unused = nullptr;
                if (!arguments("category", aArgs, aKwargs, aMethod, false, c, unused))
                    return nullptr;
                auto index = record_of(c).category;
                if (is_old(aArgs, aMethod))
                    if (auto const r = old_record_of(c))
                        index = r->category;
                return make_str(ut::sCategories[index]);
            }

            object* bidirectional(std::vector<object*> const& aArgs, object* aKwargs, bool aMethod)
            {
                char32_t c = 0u;
                object* unused = nullptr;
                if (!arguments("bidirectional", aArgs, aKwargs, aMethod, false, c, unused))
                    return nullptr;
                auto index = record_of(c).bidirectional;
                if (is_old(aArgs, aMethod))
                    if (auto const r = old_record_of(c))
                        index = r->bidirectional;
                return make_str(ut::sBidirectional[index]);
            }

            object* east_asian_width(std::vector<object*> const& aArgs, object* aKwargs, bool aMethod)
            {
                char32_t c = 0u;
                object* unused = nullptr;
                if (!arguments("east_asian_width", aArgs, aKwargs, aMethod, false, c, unused))
                    return nullptr;
                auto index = record_of(c).eastAsianWidth;
                if (is_old(aArgs, aMethod))
                    if (auto const r = old_record_of(c))
                        index = r->eastAsianWidth;
                return make_str(ut::sEastAsianWidth[index]);
            }

            object* combining(std::vector<object*> const& aArgs, object* aKwargs, bool aMethod)
            {
                char32_t c = 0u;
                object* unused = nullptr;
                if (!arguments("combining", aArgs, aKwargs, aMethod, false, c, unused))
                    return nullptr;
                unsigned value = record_of(c).combining;
                if (is_old(aArgs, aMethod))
                    if (auto const r = old_record_of(c))
                        value = r->combining;
                return make_int(value);
            }

            object* mirrored(std::vector<object*> const& aArgs, object* aKwargs, bool aMethod)
            {
                char32_t c = 0u;
                object* unused = nullptr;
                if (!arguments("mirrored", aArgs, aKwargs, aMethod, false, c, unused))
                    return nullptr;
                int value = (record_of(c).flags & ut::MIRRORED) != 0u ? 1 : 0;
                if (is_old(aArgs, aMethod))
                    if (auto const r = old_record_of(c))
                        value = r->mirrored;
                return make_int(value);
            }

            object* value_function(char const* aName, char const* aError, std::vector<object*> const& aArgs, object* aKwargs, bool aMethod, int aWhich)
            {
                char32_t c = 0u;
                object* defaultValue = nullptr;
                if (!arguments(aName, aArgs, aKwargs, aMethod, true, c, defaultValue))
                    return nullptr;
                auto const& r = record_of(c);
                int decimal = r.decimal, digit = r.digit, numeric = r.numeric;
                if (is_old(aArgs, aMethod))
                    if (auto const o = old_record_of(c))
                    {
                        decimal = o->decimal;
                        digit = o->digit;
                        numeric = o->numeric;
                    }
                auto const value = aWhich == 0 ? decimal : aWhich == 1 ? digit : numeric;
                if (value < 0)
                {
                    if (defaultValue != nullptr)
                        return new_reference(defaultValue);
                    return error("ValueError", aError);
                }
                if (aWhich == 2)
                    return make_float(ut::sNumerics[value]);
                return make_int(value);
            }

            object* decomposition(std::vector<object*> const& aArgs, object* aKwargs, bool aMethod)
            {
                char32_t c = 0u;
                object* unused = nullptr;
                if (!arguments("decomposition", aArgs, aKwargs, aMethod, false, c, unused))
                    return nullptr;
                bool const old = is_old(aArgs, aMethod);
                std::uint32_t tag = 0u;
                std::u32string d;
                if ((old && unassigned_in_old(c)) || !decomposition_of(c, old, tag, d))
                    return make_str("");
                std::string result = ut::sDecompositionTags[tag];
                for (auto x : d)
                {
                    char hex[16];
                    std::snprintf(hex, sizeof(hex), "%04X", static_cast<unsigned>(x));
                    if (!result.empty())
                        result += ' ';
                    result += hex;
                }
                return make_str(result);
            }

            bool normalization_form(object* aForm, bool& aComposed, bool& aCompatibility)
            {
                if (kind_of(aForm) != NEOS_PY_STR)
                    return (error("TypeError", std::string{ "normalize() argument 1 must be str, not " } + type_name(aForm)), false);
                auto const& f = as<str_object>(aForm)->value;
                if (f == "NFC") { aComposed = true; aCompatibility = false; }
                else if (f == "NFKC") { aComposed = true; aCompatibility = true; }
                else if (f == "NFD") { aComposed = false; aCompatibility = false; }
                else if (f == "NFKD") { aComposed = false; aCompatibility = true; }
                else
                    return (error("ValueError", "invalid normalization form"), false);
                return true;
            }

            object* normalize(std::vector<object*> const& aArgs, object* aKwargs, bool aMethod, bool aCheck)
            {
                char const* const name = aCheck ? "is_normalized" : "normalize";
                std::vector<object*> a;
                if (!parse_arguments(name, std::vector<object*>{ aArgs.begin() + (aMethod ? 1 : 0), aArgs.end() }, aKwargs, { "form", "unistr" }, 2u, 2u, a))
                    return nullptr;
                bool composedForm = false, compatibility = false;
                if (kind_of(a[0]) != NEOS_PY_STR)
                    return error("TypeError", std::string{ name } + "() argument 1 must be str, not " + type_name(a[0]));
                if (kind_of(a[1]) != NEOS_PY_STR)
                    return error("TypeError", std::string{ name } + "() argument 2 must be str, not " + type_name(a[1]));
                if (!normalization_form(a[0], composedForm, compatibility))
                    return nullptr;
                auto const input = to_code_points(as<str_object>(a[1])->value);
                bool const old = is_old(aArgs, aMethod);
                auto const result = composedForm ? composed(input, compatibility, old) : decomposed(input, compatibility, old);
                if (aCheck)
                    return make_bool(result == input);
                if (result == input)
                    return new_reference(a[1]);
                return make_str(from_code_points(result));
            }

            object* name(std::vector<object*> const& aArgs, object* aKwargs, bool aMethod)
            {
                char32_t c = 0u;
                object* defaultValue = nullptr;
                if (!arguments("name", aArgs, aKwargs, aMethod, true, c, defaultValue))
                    return nullptr;
                std::string result;
                if ((is_old(aArgs, aMethod) && unassigned_in_old(c)) || !un::name_of(c, result))
                {
                    if (defaultValue != nullptr)
                        return new_reference(defaultValue);
                    return error("ValueError", "no such name");
                }
                return make_str(result);
            }

            object* lookup(std::vector<object*> const& aArgs, object* aKwargs, bool aMethod)
            {
                std::vector<object*> a;
                if (!parse_arguments("lookup", std::vector<object*>{ aArgs.begin() + (aMethod ? 1 : 0), aArgs.end() }, aKwargs, { "name" }, 1u, 1u, a))
                    return nullptr;
                std::string text;
                std::string_view bytes;
                if (kind_of(a[0]) == NEOS_PY_STR)
                    text = as<str_object>(a[0])->value;
                else if (bytes_like(a[0], bytes))
                    text = std::string{ bytes };
                else
                    return error("TypeError", std::string{ "a bytes-like object is required, not '" } + type_name(a[0]) + "'");
                if (text.size() > 256u)
                {
                    ref const message{ make_str("name too long") };
                    raise_exception(ref{ make_exception("KeyError", { new_reference(message.get()) }) }.get());
                    return nullptr;
                }
                bool const old = is_old(aArgs, aMethod);
                auto const result = un::lookup(text, !old, !old);
                if (!result || (old && unassigned_in_old((*result)[0])))
                {
                    ref const message{ make_str("undefined character name '" + text + "'") };
                    raise_exception(ref{ make_exception("KeyError", { new_reference(message.get()) }) }.get());
                    return nullptr;
                }
                return make_str(from_code_points(*result));
            }

            builtin_entry const sUnicodedataFunctions[] =
            {
                { "lookup", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return lookup(aArgs, aKwargs, false); } },
                { "name", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return name(aArgs, aKwargs, false); } },
                { "decimal", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return value_function("decimal", "not a decimal", aArgs, aKwargs, false, 0); } },
                { "digit", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return value_function("digit", "not a digit", aArgs, aKwargs, false, 1); } },
                { "numeric", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return value_function("numeric", "not a numeric character", aArgs, aKwargs, false, 2); } },
                { "category", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return category(aArgs, aKwargs, false); } },
                { "bidirectional", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return bidirectional(aArgs, aKwargs, false); } },
                { "combining", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return combining(aArgs, aKwargs, false); } },
                { "east_asian_width", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return east_asian_width(aArgs, aKwargs, false); } },
                { "mirrored", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return mirrored(aArgs, aKwargs, false); } },
                { "decomposition", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return decomposition(aArgs, aKwargs, false); } },
                { "normalize", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return normalize(aArgs, aKwargs, false, false); } },
                { "is_normalized", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return normalize(aArgs, aKwargs, false, true); } }
            };

            // (UCD's methods (ucd_3_2_0's))
            builtin_entry const sUcdMethods[] =
            {
                { "__new__", [](std::vector<object*> const&, object*) -> object* { return error("TypeError", "cannot create 'unicodedata.UCD' instances"); } },
                { "lookup", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return lookup(aArgs, aKwargs, true); } },
                { "name", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return name(aArgs, aKwargs, true); } },
                { "decimal", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return value_function("decimal", "not a decimal", aArgs, aKwargs, true, 0); } },
                { "digit", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return value_function("digit", "not a digit", aArgs, aKwargs, true, 1); } },
                { "numeric", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return value_function("numeric", "not a numeric character", aArgs, aKwargs, true, 2); } },
                { "category", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return category(aArgs, aKwargs, true); } },
                { "bidirectional", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return bidirectional(aArgs, aKwargs, true); } },
                { "combining", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return combining(aArgs, aKwargs, true); } },
                { "east_asian_width", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return east_asian_width(aArgs, aKwargs, true); } },
                { "mirrored", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return mirrored(aArgs, aKwargs, true); } },
                { "decomposition", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return decomposition(aArgs, aKwargs, true); } },
                { "normalize", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return normalize(aArgs, aKwargs, true, false); } },
                { "is_normalized", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return normalize(aArgs, aKwargs, true, true); } }
            };

            bool unicodedata_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                ref const ucd{ make_class_of("unicodedata", "UCD", {}, sUcdMethods, { { "unidata_version", make_str("3.2.0") } }) };
                if (!ucd)
                    return false;
                if (sUcd32 == nullptr)
                {
                    ref const args{ make_tuple() };
                    sUcd32 = new_instance(ucd.get(), args.get(), nullptr);
                    if (sUcd32 == nullptr)
                        return false;
                }
                aAttributes.emplace_back("UCD", new_reference(ucd.get()));
                aAttributes.emplace_back("ucd_3_2_0", new_reference(sUcd32));
                aAttributes.emplace_back("unidata_version", make_str("15.1.0"));
                for (auto const& f : sUnicodedataFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                return true;
            }

            inline bool const sUnicodedata = register_module("unicodedata", unicodedata_module);
        }

        // --- the properties of characters (str's methods)

        std::uint16_t unicode_flags(char32_t aCharacter)
        {
            return record_of(aCharacter).flags;
        }

        void append_unicode_case(std::u32string& aResult, char32_t aCharacter, unicode_case aCase)
        {
            auto const& r = record_of(aCharacter);
            auto const mapping = aCase == unicode_case::Upper ? r.upper : aCase == unicode_case::Lower ? r.lower : aCase == unicode_case::Title ? r.title : r.fold;
            if ((r.flags & ut::EXTENDED_CASE) != 0u)
            {
                auto const offset = static_cast<std::uint32_t>(mapping) & 0xFFFFFFu;
                auto const length = static_cast<std::uint32_t>(mapping) >> 24;
                for (std::uint32_t i = 0u; i < length; ++i)
                    aResult += static_cast<char32_t>(ut::sExtendedCase[offset + i]);
            }
            else
                aResult += static_cast<char32_t>(static_cast<std::int64_t>(aCharacter) + mapping);
        }

        char32_t unicode_simple_case(char32_t aCharacter, unicode_case aCase)
        {
            std::u32string mapped;
            append_unicode_case(mapped, aCharacter, aCase);
            return mapped.empty() ? aCharacter : mapped[0];
        }

        int unicode_decimal(char32_t aCharacter) { return record_of(aCharacter).decimal; }
        int unicode_digit(char32_t aCharacter) { return record_of(aCharacter).digit; }

        bool unicode_name(char32_t aCharacter, std::string& aResult)
        {
            return un::name_of(aCharacter, aResult);
        }

        bool unicode_lookup(std::string_view aName, char32_t& aResult)
        {
            auto const result = un::lookup(aName, true, false);
            if (!result || result->size() != 1u)
                return false;
            aResult = (*result)[0];
            return true;
        }
    }
}
