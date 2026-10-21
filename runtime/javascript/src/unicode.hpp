/*
  unicode.hpp

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

// Unicode character data used by the runtime (case mapping, normalization, identifiers, RegExp property escapes)

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace neos::javascript_runtime::unicode
{
    struct range
    {
        char32_t first;
        char32_t last;  ///< (inclusive)
    };

    // RegExp \p{...} and \P{...}: aName is the property name (or a General_Category value or binary property name when aValue is
    // empty, as in \p{Lu} or \p{Alphabetic}); aValue is the value (as in \p{Script=Greek}); the ranges are sorted and disjoint; returns
    // false if the property (or value) is not one ECMAScript supports
    bool property(std::u16string_view aName, std::u16string_view aValue, std::span<range const>& aRanges);
    // properties of strings (the v flag: \p{RGI_Emoji} etc.): the strings (code point sequences of more than one code point) and the
    // single code points; returns false if aName is not one
    bool property_of_strings(std::u16string_view aName, std::span<std::u32string_view const>& aStrings, std::span<range const>& aRanges);

    bool id_start(char32_t aCodePoint);         ///< (ID_Start, plus $ and _ for ECMAScript's IdentifierStartChar)
    bool id_continue(char32_t aCodePoint);      ///< (ID_Continue, plus $, ZWNJ and ZWJ for ECMAScript's IdentifierPartChar)
    bool white_space(char32_t aCodePoint);      ///< (ECMAScript WhiteSpace: TAB, VT, FF, ZWNBSP and Zs)

    char32_t simple_case_fold(char32_t aCodePoint);     ///< (CaseFolding.txt C+S: RegExp Canonicalize with the u or v flag)
    char32_t simple_uppercase(char32_t aCodePoint);     ///< (UnicodeData.txt simple uppercase mapping: RegExp Canonicalize without u/v)

    // (additions)

    bool contains(std::span<range const> aRanges, char32_t aCodePoint);     ///< (binary search of sorted, disjoint ranges)
    bool line_terminator(char32_t aCodePoint);  ///< (ECMAScript LineTerminator: LF, CR, LS, PS)
    char32_t simple_lowercase(char32_t aCodePoint);     ///< (UnicodeData.txt simple lowercase mapping)
    // RegExp Canonicalize(rer, ch) when rer.[[IgnoreCase]] is true: with the u or v flag (aUnicodeMode), simple case folding; without
    // them, the spec's rule: toUppercase(ch) (the FULL mapping) if that is a single code unit and doesn't map a non-ASCII ch to ASCII,
    // else ch (this differs from simple_uppercase for e.g. U+1F80, whose full uppercase is two code points, and U+00DF)
    char32_t canonicalize(char32_t aCharacter, bool aUnicodeMode);

    enum class normalization_form
    {
        NFC,
        NFD,
        NFKC,
        NFKD
    };
    std::u16string normalize(std::u16string_view aText, normalization_form aForm);     ///< (lone surrogates pass through)
}
