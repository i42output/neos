/*
  unicode.cpp

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

// Unicode: properties, identifiers, case mapping and normalization (the tables are in unicode_data.cpp, generated from the Unicode
// Character Database by runtime/javascript/tools/generate_unicode.py)

#include <algorithm>
#include <utility>
#include <vector>
#if !defined(NEOS_JAVASCRIPT_COMPILER)
#include "js.hpp"
#endif
#include "unicode.hpp"
#include "unicode_data.hpp"

namespace neos::javascript_runtime
{
    namespace unicode
    {
        namespace
        {
            constexpr char32_t sHangulBase = 0xAC00u;
            constexpr char32_t sHangulL = 0x1100u;
            constexpr char32_t sHangulV = 0x1161u;
            constexpr char32_t sHangulT = 0x11A7u;
            constexpr char32_t sHangulLCount = 19u;
            constexpr char32_t sHangulVCount = 21u;
            constexpr char32_t sHangulTCount = 28u;
            constexpr char32_t sHangulNCount = sHangulVCount * sHangulTCount;
            constexpr char32_t sHangulCount = sHangulLCount * sHangulNCount;

            std::span<range const> ranges_of(data::slice const& aSlice)
            {
                return { data::sRanges + aSlice.offset, aSlice.count };
            }

            std::span<range const> ranges_of(data::named_slice const& aSlice)
            {
                return { data::sRanges + aSlice.offset, aSlice.count };
            }

            bool find_named(data::named_slice const* aTable, std::size_t aCount, std::u16string_view aName, std::span<range const>& aRanges)
            {
                for (std::size_t i = 0u; i < aCount; ++i)
                    if (aTable[i].name == aName)
                    {
                        aRanges = ranges_of(aTable[i]);
                        return true;
                    }
                return false;
            }

            template <typename T>
            T const* find_mapping(T const* aTable, std::size_t aCount, char32_t aCodePoint)
            {
                auto const end = aTable + aCount;
                auto const found = std::lower_bound(aTable, end, aCodePoint, [](T const& aEntry, char32_t aValue) { return aEntry.from < aValue; });
                return found != end && found->from == aCodePoint ? found : nullptr;
            }

            char32_t simple_map(data::mapping const* aTable, std::size_t aCount, char32_t aCodePoint)
            {
                auto const found = find_mapping(aTable, aCount, aCodePoint);
                return found ? found->to : aCodePoint;
            }

            bool cased(char32_t aCodePoint)
            {
                return contains(ranges_of(data::sCased), aCodePoint);
            }

            bool case_ignorable(char32_t aCodePoint)
            {
                return contains(ranges_of(data::sCaseIgnorable), aCodePoint);
            }

            // UTF-16 decoding (a lone surrogate is a code point of its own)
            char32_t next_code_point(std::u16string_view aText, std::size_t& aIndex)
            {
                char32_t const c = aText[aIndex++];
                if (c >= 0xD800u && c <= 0xDBFFu && aIndex < aText.size() && aText[aIndex] >= 0xDC00u && aText[aIndex] <= 0xDFFFu)
                    return 0x10000u + ((c - 0xD800u) << 10u) + (aText[aIndex++] - 0xDC00u);
                return c;
            }

            char32_t previous_code_point(std::u16string_view aText, std::size_t& aIndex)
            {
                char32_t const c = aText[--aIndex];
                if (c >= 0xDC00u && c <= 0xDFFFu && aIndex > 0u && aText[aIndex - 1u] >= 0xD800u && aText[aIndex - 1u] <= 0xDBFFu)
                    return 0x10000u + ((aText[--aIndex] - 0xD800u) << 10u) + (c - 0xDC00u);
                return c;
            }

            void append(std::u16string& aText, char32_t aCodePoint)
            {
                if (aCodePoint < 0x10000u)
                    aText.push_back(static_cast<char16_t>(aCodePoint));
                else
                {
                    aText.push_back(static_cast<char16_t>(0xD800u + ((aCodePoint - 0x10000u) >> 10u)));
                    aText.push_back(static_cast<char16_t>(0xDC00u + ((aCodePoint - 0x10000u) & 0x3FFu)));
                }
            }

            // Final_Sigma (The Unicode Standard, table 3-17): C is preceded by a cased letter then zero or more case-ignorable
            // characters, and is not followed by zero or more case-ignorable characters then a cased letter (a character that is
            // both case-ignorable and cased, such as U+0345, is skipped as case-ignorable, as test262 expects)
            bool final_sigma(std::u16string_view aText, std::size_t aStart, std::size_t aEnd)
            {
                bool before = false;
                for (std::size_t i = aStart; i > 0u;)
                {
                    auto const c = previous_code_point(aText, i);
                    if (case_ignorable(c))
                        continue;
                    before = cased(c);
                    break;
                }
                if (!before)
                    return false;
                for (std::size_t i = aEnd; i < aText.size();)
                {
                    auto const c = next_code_point(aText, i);
                    if (case_ignorable(c))
                        continue;
                    return !cased(c);
                }
                return true;
            }

            int combining_class(char32_t aCodePoint)
            {
                if (aCodePoint < 0x300u)
                    return 0;
                auto const end = data::sCombiningClasses + data::sCombiningClassesCount;
                auto const found = std::lower_bound(data::sCombiningClasses, end, aCodePoint,
                    [](data::combining_class const& aEntry, char32_t aValue) { return aEntry.last < aValue; });
                return found != end && found->first <= aCodePoint ? found->ccc : 0;
            }

            // full (canonical or compatibility) decomposition, appended in canonical order
            void decompose(char32_t aCodePoint, bool aCompatibility, std::vector<char32_t>& aOutput)
            {
                auto const put = [&](char32_t aPart)
                {
                    auto const ccc = combining_class(aPart);
                    auto pos = aOutput.size();
                    aOutput.push_back(aPart);
                    if (ccc != 0)
                        for (; pos > 0u && combining_class(aOutput[pos - 1u]) > ccc; --pos)
                            std::swap(aOutput[pos - 1u], aOutput[pos]);
                };
                if (aCodePoint >= sHangulBase && aCodePoint < sHangulBase + sHangulCount)
                {
                    auto const index = aCodePoint - sHangulBase;
                    put(sHangulL + index / sHangulNCount);
                    put(sHangulV + (index % sHangulNCount) / sHangulTCount);
                    if (index % sHangulTCount != 0u)
                        put(sHangulT + index % sHangulTCount);
                    return;
                }
                data::decomposition const* entry = nullptr;
                if (aCompatibility)
                    entry = find_mapping(data::sCompatibilityDecompositions, data::sCompatibilityDecompositionsCount, aCodePoint);
                if (!entry)
                    entry = find_mapping(data::sCanonicalDecompositions, data::sCanonicalDecompositionsCount, aCodePoint);
                if (!entry)
                {
                    put(aCodePoint);
                    return;
                }
                for (std::size_t i = 0u; i < entry->length; ++i)
                    put(data::sDecompositionData[entry->offset + i]);
            }

            char32_t primary_composite(char32_t aFirst, char32_t aSecond)  ///< (0 if none)
            {
                if (aFirst >= sHangulL && aFirst < sHangulL + sHangulLCount && aSecond >= sHangulV && aSecond < sHangulV + sHangulVCount)
                    return sHangulBase + ((aFirst - sHangulL) * sHangulVCount + (aSecond - sHangulV)) * sHangulTCount;
                if (aFirst >= sHangulBase && aFirst < sHangulBase + sHangulCount && (aFirst - sHangulBase) % sHangulTCount == 0u &&
                    aSecond > sHangulT && aSecond < sHangulT + sHangulTCount)
                    return aFirst + (aSecond - sHangulT);
                auto const end = data::sCompositions + data::sCompositionsCount;
                auto const found = std::lower_bound(data::sCompositions, end, std::make_pair(aFirst, aSecond),
                    [](data::composition const& aEntry, std::pair<char32_t, char32_t> const& aValue)
                    { return aEntry.first < aValue.first || (aEntry.first == aValue.first && aEntry.second < aValue.second); });
                return found != end && found->first == aFirst && found->second == aSecond ? found->composite : 0u;
            }

            // canonical composition (of a canonically ordered full decomposition)
            void compose(std::vector<char32_t>& aText)
            {
                if (aText.empty())
                    return;
                std::size_t starter = 0u;
                // the combining class of the last character kept since the starter (0: none, so a character is adjacent to the
                // starter; 256: no starter yet)
                int lastClass = combining_class(aText[0]) == 0 ? 0 : 256;
                std::size_t out = 1u;
                for (std::size_t i = 1u; i < aText.size(); ++i)
                {
                    auto const c = aText[i];
                    auto const ccc = combining_class(c);
                    if (lastClass < ccc || lastClass == 0)
                    {
                        auto const composite = primary_composite(aText[starter], c);
                        if (composite != 0u)
                        {
                            aText[starter] = composite;
                            continue;
                        }
                    }
                    if (ccc == 0)
                        starter = out;
                    lastClass = ccc;
                    aText[out++] = c;
                }
                aText.resize(out);
            }
        }

        bool contains(std::span<range const> aRanges, char32_t aCodePoint)
        {
            auto const found = std::lower_bound(aRanges.begin(), aRanges.end(), aCodePoint,
                [](range const& aRange, char32_t aValue) { return aRange.last < aValue; });
            return found != aRanges.end() && found->first <= aCodePoint;
        }

        bool property(std::u16string_view aName, std::u16string_view aValue, std::span<range const>& aRanges)
        {
            if (aValue.empty())
                return find_named(data::sGeneralCategoryValues, data::sGeneralCategoryValuesCount, aName, aRanges) ||
                    find_named(data::sBinaryProperties, data::sBinaryPropertiesCount, aName, aRanges);
            if (aName == u"General_Category" || aName == u"gc")
                return find_named(data::sGeneralCategoryValues, data::sGeneralCategoryValuesCount, aValue, aRanges);
            if (aName == u"Script" || aName == u"sc")
                return find_named(data::sScriptValues, data::sScriptValuesCount, aValue, aRanges);
            if (aName == u"Script_Extensions" || aName == u"scx")
                return find_named(data::sScriptExtensionsValues, data::sScriptExtensionsValuesCount, aValue, aRanges);
            return false;
        }

        bool property_of_strings(std::u16string_view aName, std::span<std::u32string_view const>& aStrings, std::span<range const>& aRanges)
        {
            for (std::size_t i = 0u; i < data::sStringPropertiesCount; ++i)
            {
                auto const& p = data::sStringProperties[i];
                if (p.name == aName)
                {
                    aStrings = { data::sStrings + p.strings.offset, p.strings.count };
                    aRanges = ranges_of(p.ranges);
                    return true;
                }
            }
            return false;
        }

        bool id_start(char32_t aCodePoint)
        {
            if (aCodePoint < 0x80u)
                return (aCodePoint >= U'a' && aCodePoint <= U'z') || (aCodePoint >= U'A' && aCodePoint <= U'Z') || aCodePoint == U'$' ||
                    aCodePoint == U'_';
            return contains(ranges_of(data::sIdStart), aCodePoint);
        }

        bool id_continue(char32_t aCodePoint)
        {
            if (aCodePoint < 0x80u)
                return (aCodePoint >= U'a' && aCodePoint <= U'z') || (aCodePoint >= U'A' && aCodePoint <= U'Z') ||
                    (aCodePoint >= U'0' && aCodePoint <= U'9') || aCodePoint == U'$' || aCodePoint == U'_';
            return aCodePoint == 0x200Cu || aCodePoint == 0x200Du || contains(ranges_of(data::sIdContinue), aCodePoint);
        }

        bool white_space(char32_t aCodePoint)
        {
            if (aCodePoint < 0x80u)
                return aCodePoint == 0x09u || aCodePoint == 0x0Bu || aCodePoint == 0x0Cu || aCodePoint == 0x20u;
            return aCodePoint == 0xFEFFu || contains(ranges_of(data::sWhiteSpaceZs), aCodePoint);
        }

        bool line_terminator(char32_t aCodePoint)
        {
            return aCodePoint == 0x0Au || aCodePoint == 0x0Du || aCodePoint == 0x2028u || aCodePoint == 0x2029u;
        }

        char32_t simple_case_fold(char32_t aCodePoint)
        {
            if (aCodePoint < 0x80u)
                return aCodePoint >= U'A' && aCodePoint <= U'Z' ? aCodePoint + 32u : aCodePoint;
            return simple_map(data::sSimpleCaseFolding, data::sSimpleCaseFoldingCount, aCodePoint);
        }

        char32_t simple_uppercase(char32_t aCodePoint)
        {
            if (aCodePoint < 0x80u)
                return aCodePoint >= U'a' && aCodePoint <= U'z' ? aCodePoint - 32u : aCodePoint;
            return simple_map(data::sSimpleUppercase, data::sSimpleUppercaseCount, aCodePoint);
        }

        char32_t simple_lowercase(char32_t aCodePoint)
        {
            if (aCodePoint < 0x80u)
                return aCodePoint >= U'A' && aCodePoint <= U'Z' ? aCodePoint + 32u : aCodePoint;
            return simple_map(data::sSimpleLowercase, data::sSimpleLowercaseCount, aCodePoint);
        }

        char32_t canonicalize(char32_t aCharacter, bool aUnicodeMode)
        {
            if (aUnicodeMode)
                return simple_case_fold(aCharacter);
            if (aCharacter < 0x80u)
                return simple_uppercase(aCharacter);
            if (find_mapping(data::sSpecialUppercase, data::sSpecialUppercaseCount, aCharacter))
                return aCharacter;  // (the full uppercase mappings that differ from the simple ones are all of more than one code point)
            auto const upper = simple_uppercase(aCharacter);
            if (upper >= 0x10000u || upper < 0x80u)
                return aCharacter;
            return upper;
        }

        std::u16string normalize(std::u16string_view aText, normalization_form aForm)
        {
            bool const compatibility = aForm == normalization_form::NFKC || aForm == normalization_form::NFKD;
            bool const composition = aForm == normalization_form::NFC || aForm == normalization_form::NFKC;
            char16_t const limit = compatibility ? 0xA0u : 0xC0u;   // (below which nothing decomposes, reorders or composes)
            if (std::all_of(aText.begin(), aText.end(), [limit](char16_t c) { return c < limit; }))
                return std::u16string{ aText };
            std::vector<char32_t> codePoints;
            codePoints.reserve(aText.size());
            for (std::size_t i = 0u; i < aText.size();)
                decompose(next_code_point(aText, i), compatibility, codePoints);
            if (composition)
                compose(codePoints);
            std::u16string result;
            result.reserve(codePoints.size());
            for (auto c : codePoints)
                append(result, c);
            return result;
        }
    }

#if !defined(NEOS_JAVASCRIPT_COMPILER)
    // (the runtime's: the compiler (which uses the Unicode data for regular expression literals' patterns) has none)
    std::u16string case_map(std::u16string const& aText, bool aUpper)
    {
        std::u16string result;
        result.reserve(aText.size());
        std::u16string_view const text{ aText };
        for (std::size_t i = 0u; i < text.size();)
        {
            auto const c = text[i];
            if (c < 0x80u)
            {
                if (aUpper && c >= u'a' && c <= u'z')
                    result.push_back(static_cast<char16_t>(c - 32u));
                else if (!aUpper && c >= u'A' && c <= u'Z')
                    result.push_back(static_cast<char16_t>(c + 32u));
                else
                    result.push_back(c);
                ++i;
                continue;
            }
            auto const start = i;
            auto const cp = unicode::next_code_point(text, i);
            if (!aUpper && cp == 0x3A3u)
            {
                result.push_back(unicode::final_sigma(text, start, i) ? u'\x3C2' : u'\x3C3');
                continue;
            }
            auto const special = aUpper ?
                unicode::find_mapping(unicode::data::sSpecialUppercase, unicode::data::sSpecialUppercaseCount, cp) :
                unicode::find_mapping(unicode::data::sSpecialLowercase, unicode::data::sSpecialLowercaseCount, cp);
            if (special)
            {
                for (std::size_t j = 0u; j < special->length; ++j)
                    unicode::append(result, special->to[j]);
                continue;
            }
            unicode::append(result, aUpper ? unicode::simple_uppercase(cp) : unicode::simple_lowercase(cp));
        }
        return result;
    }

    value normalize_string(value aString, std::u16string const& aForm)
    {
        auto const form =
            aForm == u"NFD" ? unicode::normalization_form::NFD :
            aForm == u"NFKC" ? unicode::normalization_form::NFKC :
            aForm == u"NFKD" ? unicode::normalization_form::NFKD : unicode::normalization_form::NFC;
        auto const& text = str(aString);
        auto normalized = unicode::normalize(text, form);
        if (normalized == text)
            return aString;
        return make_string(std::move(normalized));
    }
#endif
}
