/*
  unicode_data.hpp

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

// Unicode character tables (generated: unicode_data.cpp, by runtime/javascript/tools/generate_unicode.py): private to unicode.cpp

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include "unicode.hpp"

namespace neos::javascript_runtime::unicode::data
{
    struct slice
    {
        std::uint32_t offset;
        std::uint32_t count;
    };

    struct named_slice
    {
        std::u16string_view name;
        std::uint32_t offset;   ///< (into sRanges)
        std::uint32_t count;
    };

    struct string_property
    {
        std::u16string_view name;
        slice strings;          ///< (into sStrings)
        slice ranges;           ///< (into sRanges)
    };

    struct mapping
    {
        char32_t from;
        char32_t to;
    };

    struct special_casing
    {
        char32_t from;
        char32_t to[3];
        std::uint8_t length;
    };

    struct combining_class
    {
        char32_t first;
        char32_t last;
        std::uint8_t ccc;
    };

    struct decomposition
    {
        char32_t from;
        std::uint16_t offset;   ///< (into sDecompositionData)
        std::uint8_t length;
    };

    struct composition
    {
        char32_t first;
        char32_t second;
        char32_t composite;
    };

    extern range const sRanges[];
    extern std::size_t const sRangeCount;
    extern named_slice const sBinaryProperties[];
    extern std::size_t const sBinaryPropertiesCount;
    extern named_slice const sGeneralCategoryValues[];
    extern std::size_t const sGeneralCategoryValuesCount;
    extern named_slice const sScriptValues[];
    extern std::size_t const sScriptValuesCount;
    extern named_slice const sScriptExtensionsValues[];
    extern std::size_t const sScriptExtensionsValuesCount;
    extern slice const sWhiteSpaceZs;
    extern slice const sCased;
    extern slice const sCaseIgnorable;
    extern slice const sIdStart;
    extern slice const sIdContinue;

    extern std::u32string_view const sStrings[];
    extern string_property const sStringProperties[];
    extern std::size_t const sStringPropertiesCount;

    extern mapping const sSimpleUppercase[];
    extern std::size_t const sSimpleUppercaseCount;
    extern mapping const sSimpleLowercase[];
    extern std::size_t const sSimpleLowercaseCount;
    extern mapping const sSimpleCaseFolding[];
    extern std::size_t const sSimpleCaseFoldingCount;
    extern special_casing const sSpecialUppercase[];
    extern std::size_t const sSpecialUppercaseCount;
    extern special_casing const sSpecialLowercase[];
    extern std::size_t const sSpecialLowercaseCount;

    extern combining_class const sCombiningClasses[];
    extern std::size_t const sCombiningClassesCount;
    extern decomposition const sCanonicalDecompositions[];
    extern std::size_t const sCanonicalDecompositionsCount;
    extern decomposition const sCompatibilityDecompositions[];
    extern std::size_t const sCompatibilityDecompositionsCount;
    extern char32_t const sDecompositionData[];
    extern composition const sCompositions[];
    extern std::size_t const sCompositionsCount;
}
