/*
  str.cpp

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

  Portions of this file are derived from CPython 3.13 (Objects/unicodeobject.c), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE).
*/

// str's methods (CPython 3.13's unicodeobject.c: their arguments, results and messages); the character properties are those
// of the Latin, Greek, Cyrillic and common CJK ranges

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        // --- UTF-8 and code points

        std::u32string to_code_points(std::string_view aText)
        {
            std::u32string result;
            result.reserve(aText.size());
            for (std::size_t i = 0u; i < aText.size();)
            {
                auto const b = static_cast<unsigned char>(aText[i]);
                char32_t c = b;
                std::size_t n = 1u;
                if (b >= 0xF0u && i + 3u < aText.size())
                {
                    c = ((b & 0x07u) << 18) | ((static_cast<unsigned char>(aText[i + 1u]) & 0x3Fu) << 12) |
                        ((static_cast<unsigned char>(aText[i + 2u]) & 0x3Fu) << 6) | (static_cast<unsigned char>(aText[i + 3u]) & 0x3Fu);
                    n = 4u;
                }
                else if (b >= 0xE0u && i + 2u < aText.size())
                {
                    c = ((b & 0x0Fu) << 12) | ((static_cast<unsigned char>(aText[i + 1u]) & 0x3Fu) << 6) | (static_cast<unsigned char>(aText[i + 2u]) & 0x3Fu);
                    n = 3u;
                }
                else if (b >= 0xC0u && i + 1u < aText.size())
                {
                    c = ((b & 0x1Fu) << 6) | (static_cast<unsigned char>(aText[i + 1u]) & 0x3Fu);
                    n = 2u;
                }
                result += c;
                i += n;
            }
            return result;
        }

        void append_code_point(std::string& aText, char32_t aCodePoint)
        {
            auto const u = static_cast<std::uint32_t>(aCodePoint);
            if (u < 0x80u)
                aText += static_cast<char>(u);
            else if (u < 0x800u)
            {
                aText += static_cast<char>(0xC0u | (u >> 6));
                aText += static_cast<char>(0x80u | (u & 0x3Fu));
            }
            else if (u < 0x10000u)
            {
                aText += static_cast<char>(0xE0u | (u >> 12));
                aText += static_cast<char>(0x80u | ((u >> 6) & 0x3Fu));
                aText += static_cast<char>(0x80u | (u & 0x3Fu));
            }
            else
            {
                aText += static_cast<char>(0xF0u | (u >> 18));
                aText += static_cast<char>(0x80u | ((u >> 12) & 0x3Fu));
                aText += static_cast<char>(0x80u | ((u >> 6) & 0x3Fu));
                aText += static_cast<char>(0x80u | (u & 0x3Fu));
            }
        }

        std::string from_code_points(std::u32string_view aText)
        {
            std::string result;
            result.reserve(aText.size());
            for (auto c : aText)
                append_code_point(result, c);
            return result;
        }

        // --- character properties

        namespace
        {

            bool is_upper(char32_t c) { return unicode_is(c, unicode_flag::UPPER); }
            bool is_lower(char32_t c) { return unicode_is(c, unicode_flag::LOWER); }
            bool is_title(char32_t c) { return unicode_is(c, unicode_flag::TITLE); }
            bool is_cased(char32_t c) { return unicode_is(c, unicode_flag::CASED); }
            bool is_case_ignorable(char32_t c) { return unicode_is(c, unicode_flag::CASE_IGNORABLE); }
            bool is_space(char32_t c) { return unicode_is(c, unicode_flag::SPACE); }
            bool is_linebreak(char32_t c) { return unicode_is(c, unicode_flag::LINEBREAK); }
            bool is_decimal(char32_t c) { return unicode_is(c, unicode_flag::DECIMAL); }
            bool is_digit(char32_t c) { return unicode_is(c, unicode_flag::DIGIT); }
            bool is_numeric(char32_t c) { return unicode_is(c, unicode_flag::NUMERIC); }
            bool is_alpha(char32_t c) { return unicode_is(c, unicode_flag::ALPHA); }
            bool is_alnum(char32_t c) { return (unicode_flags(c) & (unicode_flag::ALPHA | unicode_flag::DECIMAL | unicode_flag::DIGIT | unicode_flag::NUMERIC)) != 0u; }
            bool is_printable(char32_t c) { return unicode_is(c, unicode_flag::PRINTABLE); }

            // (full case mappings)
            void append_upper(std::u32string& aResult, char32_t c) { append_unicode_case(aResult, c, unicode_case::Upper); }
            void append_title(std::u32string& aResult, char32_t c) { append_unicode_case(aResult, c, unicode_case::Title); }
            void append_fold(std::u32string& aResult, char32_t c) { append_unicode_case(aResult, c, unicode_case::Fold); }

            // (the lower case of aText[i]: a capital sigma is final (CPython's handle_capital_sigma) if a cased character precedes it and
            // none follows it (case-ignorable characters between))
            void append_lower(std::u32string& aResult, std::u32string const& aText, std::size_t i)
            {
                auto const c = aText[i];
                if (c == 0x3A3u)
                {
                    std::size_t j = i;
                    while (j > 0u && is_case_ignorable(aText[j - 1u]))
                        --j;
                    bool const final = j > 0u && is_cased(aText[j - 1u]) && [&]()
                    {
                        auto k = i + 1u;
                        while (k < aText.size() && is_case_ignorable(aText[k]))
                            ++k;
                        return k == aText.size() || !is_cased(aText[k]);
                    }();
                    aResult += final ? char32_t{ 0x3C2u } : char32_t{ 0x3C3u };
                    return;
                }
                append_unicode_case(aResult, c, unicode_case::Lower);
            }

            // (the arguments: self (str) and the method's)
            struct str_method_entry
            {
                char const* name;
                object* (*function)(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs);
            };

            std::u32string const& points_of(object* aStr, std::u32string& aStorage)
            {
                aStorage = to_code_points(as<str_object>(aStr)->value);
                return aStorage;
            }

            object* make_text(std::u32string_view aText) { return make_str(from_code_points(aText)); }

            bool no_arguments(char const* aName, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                return parse_arguments(aName, aArgs, aKwargs, {}, 0u, 0u, a);
            }

            // (a slice index argument (None: aDefault))
            bool slice_index(object* aValue, long long aDefault, long long& aResult)
            {
                if (aValue == nullptr || kind_of(aValue) == NEOS_PY_NONE)
                {
                    aResult = aDefault;
                    return true;
                }
                if (!to_index(aValue, aResult))
                {
                    clear_error();
                    error("TypeError", "slice indices must be integers or None or have an __index__ method");
                    return false;
                }
                return true;
            }

            void adjust_indices(long long& aStart, long long& aEnd, long long aLength)
            {
                if (aEnd > aLength)
                    aEnd = aLength;
                else if (aEnd < 0)
                {
                    aEnd += aLength;
                    if (aEnd < 0)
                        aEnd = 0;
                }
                if (aStart < 0)
                {
                    aStart += aLength;
                    if (aStart < 0)
                        aStart = 0;
                }
            }

            bool size_argument(object* aValue, long long& aResult)
            {
                if (kind_of(aValue) == NEOS_PY_FLOAT)
                {
                    error("TypeError", "'float' object cannot be interpreted as an integer");
                    return false;
                }
                return to_index(aValue, aResult);
            }

            object* case_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, char const* aName, int aWhich)
            {
                if (!no_arguments(aName, aArgs, aKwargs))
                    return nullptr;
                std::u32string storage;
                auto const& text = points_of(aSelf, storage);
                std::u32string result;
                bool previousCased = false;
                for (std::size_t i = 0u; i < text.size(); ++i)
                {
                    auto const c = text[i];
                    switch (aWhich)
                    {
                    case 0: append_upper(result, c); break;
                    case 1: append_lower(result, text, i); break;
                    case 2: append_fold(result, c); break;
                    case 3:
                        // (swapcase)
                        if (is_upper(c)) append_lower(result, text, i);
                        else if (is_lower(c)) append_upper(result, c);
                        else result += c;
                        break;
                    case 4:
                        // (title: the first cased character of each run of cased characters is title case)
                        if (previousCased)
                            append_lower(result, text, i);
                        else
                            append_title(result, c);
                        previousCased = is_cased(c);
                        break;
                    case 5:
                        // (capitalize)
                        if (i == 0u)
                            append_title(result, c);
                        else
                            append_lower(result, text, i);
                        break;
                    }
                }
                return make_text(result);
            }

            object* predicate(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, char const* aName, bool (*aTest)(char32_t), bool aEmpty = false)
            {
                if (!no_arguments(aName, aArgs, aKwargs))
                    return nullptr;
                std::u32string storage;
                auto const& text = points_of(aSelf, storage);
                if (text.empty())
                    return make_bool(aEmpty);
                for (auto c : text)
                    if (!aTest(c))
                        return make_bool(false);
                return make_bool(true);
            }

            object* cased_predicate(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, char const* aName, bool aUpper)
            {
                if (!no_arguments(aName, aArgs, aKwargs))
                    return nullptr;
                std::u32string storage;
                bool cased = false;
                for (auto c : points_of(aSelf, storage))
                {
                    if (aUpper ? is_lower(c) : is_upper(c))
                        return make_bool(false);
                    if (aUpper ? is_upper(c) : is_lower(c))
                        cased = true;
                }
                return make_bool(cased);
            }

            // (the padding of center, ljust and rjust)
            object* pad(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, char const* aName, int aWhich)
            {
                std::vector<object*> a;
                if (!parse_arguments(aName, aArgs, aKwargs, { "width", "fillchar" }, 1u, 2u, a))
                    return nullptr;
                long long width = 0;
                if (!size_argument(a[0], width))
                    return nullptr;
                char32_t fill = U' ';
                if (a[1] != nullptr)
                {
                    if (kind_of(a[1]) != NEOS_PY_STR)
                        return error("TypeError", std::string{ "The fill character must be a unicode character, not " } + type_name(a[1]));
                    auto const f = to_code_points(as<str_object>(a[1])->value);
                    if (f.size() != 1u)
                        return error("TypeError", "The fill character must be exactly one character long");
                    fill = f[0];
                }
                std::u32string storage;
                auto const& text = points_of(aSelf, storage);
                auto const n = static_cast<long long>(text.size());
                if (width <= n)
                    return make_str(as<str_object>(aSelf)->value);
                auto const marg = width - n;
                long long left = 0;
                if (aWhich == 0)
                    left = marg / 2 + (marg & width & 1);
                else if (aWhich == 2)
                    left = marg;
                std::u32string result(static_cast<std::size_t>(left), fill);
                result += text;
                result.append(static_cast<std::size_t>(marg - left), fill);
                return make_text(result);
            }

            // (find, rfind, index, rindex and count: the substring and the range (aAscii: if the text and the substring are ASCII, their 
            // bytes (a code point each) rather than their code points))
            struct ascii_search
            {
                std::string_view text;
                std::string_view sub;
            };
            bool search_arguments(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, char const* aName, std::u32string& aText,
                std::u32string& aSub, long long& aStart, long long& aEnd, std::optional<ascii_search>* aAscii = nullptr)
            {
                std::vector<object*> a;
                if (!parse_arguments(aName, aArgs, aKwargs, { "sub", "start", "end" }, 1u, 3u, a))
                    return false;
                if (kind_of(a[0]) != NEOS_PY_STR)
                {
                    error("TypeError", std::string{ aName + 4 } + "() argument 1 must be str, not " + type_name(a[0]));
                    return false;
                }
                if (!slice_index(a[1], 0, aStart) || !slice_index(a[2], std::numeric_limits<long long>::max(), aEnd))
                    return false;
                if (aAscii != nullptr && as<str_object>(aSelf)->is_ascii() && as<str_object>(a[0])->is_ascii())
                {
                    *aAscii = ascii_search{ as<str_object>(aSelf)->value, as<str_object>(a[0])->value };
                    adjust_indices(aStart, aEnd, static_cast<long long>((*aAscii)->text.size()));
                    return true;
                }
                aText = to_code_points(as<str_object>(aSelf)->value);
                aSub = to_code_points(as<str_object>(a[0])->value);
                adjust_indices(aStart, aEnd, static_cast<long long>(aText.size()));
                return true;
            }

            template <typename Char>
            long long find_in(std::basic_string_view<Char> aText, std::basic_string_view<Char> aSub, long long aStart, long long aEnd, bool aReverse)
            {
                if (aStart > static_cast<long long>(aText.size()) || aEnd - aStart < static_cast<long long>(aSub.size()))
                    return -1;
                std::basic_string_view<Char> const range{ aText.data() + aStart, static_cast<std::size_t>(aEnd - aStart) };
                auto const i = aReverse ? range.rfind(aSub) : range.find(aSub);
                return i == std::basic_string_view<Char>::npos ? -1 : static_cast<long long>(i) + aStart;
            }
            long long find_in(std::u32string const& aText, std::u32string const& aSub, long long aStart, long long aEnd, bool aReverse)
            {
                return find_in(std::u32string_view{ aText }, std::u32string_view{ aSub }, aStart, aEnd, aReverse);
            }

            template <typename Char>
            long long count_in(std::basic_string_view<Char> aText, std::basic_string_view<Char> aSub, long long aStart, long long aEnd)
            {
                if (aStart > static_cast<long long>(aText.size()) || aEnd < aStart)
                    return 0;
                if (aSub.empty())
                    return aEnd - aStart + 1;
                long long n = 0;
                std::basic_string_view<Char> const range{ aText.data() + aStart, static_cast<std::size_t>(aEnd - aStart) };
                for (auto i = range.find(aSub); i != std::basic_string_view<Char>::npos; i = range.find(aSub, i + aSub.size()))
                    ++n;
                return n;
            }

            object* find_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, char const* aName, bool aReverse, bool aRaise)
            {
                std::u32string text, sub;
                long long start = 0, end = 0;
                std::optional<ascii_search> ascii;
                if (!search_arguments(aSelf, aArgs, aKwargs, aName, text, sub, start, end, &ascii))
                    return nullptr;
                auto const i = ascii ? find_in(ascii->text, ascii->sub, start, end, aReverse) : find_in(text, sub, start, end, aReverse);
                if (i < 0 && aRaise)
                    return error("ValueError", "substring not found");
                return make_int(i);
            }

            // (the characters of strip's argument)
            bool strip_characters(std::vector<object*> const& aArgs, object* aKwargs, char const* aName, bool& aWhitespace, std::u32string& aChars)
            {
                std::vector<object*> a;
                if (!parse_arguments(aName, aArgs, aKwargs, { "chars" }, 0u, 1u, a))
                    return false;
                aWhitespace = a[0] == nullptr || kind_of(a[0]) == NEOS_PY_NONE;
                if (!aWhitespace)
                {
                    if (kind_of(a[0]) != NEOS_PY_STR)
                    {
                        error("TypeError", std::string{ aName }.substr(4u) + " arg must be None or str");
                        return false;
                    }
                    aChars = to_code_points(as<str_object>(a[0])->value);
                }
                return true;
            }

            object* strip_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, char const* aName, bool aLeft, bool aRight)
            {
                bool whitespace = true;
                std::u32string chars;
                if (!strip_characters(aArgs, aKwargs, aName, whitespace, chars))
                    return nullptr;
                std::u32string storage;
                auto const& text = points_of(aSelf, storage);
                auto const strip = [&](char32_t c) { return whitespace ? is_space(c) : chars.find(c) != std::u32string::npos; };
                std::size_t begin = 0u;
                std::size_t end = text.size();
                if (aLeft)
                    while (begin < end && strip(text[begin]))
                        ++begin;
                if (aRight)
                    while (end > begin && strip(text[end - 1u]))
                        --end;
                return make_text(std::u32string_view{ text }.substr(begin, end - begin));
            }

            object* split_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, char const* aName, bool aReverse)
            {
                std::vector<object*> a;
                if (!parse_arguments(aName, aArgs, aKwargs, { "sep", "maxsplit" }, 0u, 0u, a))
                    return nullptr;
                long long maxsplit = -1;
                if (a[1] != nullptr && !size_argument(a[1], maxsplit))
                    return nullptr;
                if (maxsplit < 0)
                    maxsplit = std::numeric_limits<long long>::max();
                std::u32string storage;
                auto const& text = points_of(aSelf, storage);
                std::vector<std::u32string_view> parts;
                std::u32string_view const view{ text };
                if (a[0] == nullptr || kind_of(a[0]) == NEOS_PY_NONE)
                {
                    if (!aReverse)
                    {
                        std::size_t i = 0u;
                        while (maxsplit-- > 0)
                        {
                            while (i < text.size() && is_space(text[i]))
                                ++i;
                            if (i == text.size())
                                break;
                            auto j = i;
                            while (j < text.size() && !is_space(text[j]))
                                ++j;
                            parts.push_back(view.substr(i, j - i));
                            i = j;
                        }
                        while (i < text.size() && is_space(text[i]))
                            ++i;
                        if (i < text.size())
                            parts.push_back(view.substr(i));
                    }
                    else
                    {
                        auto i = text.size();
                        while (maxsplit-- > 0)
                        {
                            while (i > 0u && is_space(text[i - 1u]))
                                --i;
                            if (i == 0u)
                                break;
                            auto j = i;
                            while (j > 0u && !is_space(text[j - 1u]))
                                --j;
                            parts.push_back(view.substr(j, i - j));
                            i = j;
                        }
                        while (i > 0u && is_space(text[i - 1u]))
                            --i;
                        if (i > 0u)
                            parts.push_back(view.substr(0u, i));
                        std::reverse(parts.begin(), parts.end());
                    }
                }
                else
                {
                    if (kind_of(a[0]) != NEOS_PY_STR)
                        return error("TypeError", std::string{ "must be str or None, not " } + type_name(a[0]));
                    auto const separator = to_code_points(as<str_object>(a[0])->value);
                    if (separator.empty())
                        return error("ValueError", "empty separator");
                    if (!aReverse)
                    {
                        std::size_t i = 0u;
                        for (;;)
                        {
                            auto const j = maxsplit > 0 ? view.find(separator, i) : std::u32string_view::npos;
                            if (j == std::u32string_view::npos)
                            {
                                parts.push_back(view.substr(i));
                                break;
                            }
                            parts.push_back(view.substr(i, j - i));
                            i = j + separator.size();
                            --maxsplit;
                        }
                    }
                    else
                    {
                        auto end = text.size();
                        for (;;)
                        {
                            auto const j = maxsplit > 0 && end >= separator.size() ? view.substr(0u, end).rfind(separator) : std::u32string_view::npos;
                            if (j == std::u32string_view::npos)
                            {
                                parts.push_back(view.substr(0u, end));
                                break;
                            }
                            parts.push_back(view.substr(j + separator.size(), end - j - separator.size()));
                            end = j;
                            --maxsplit;
                        }
                        std::reverse(parts.begin(), parts.end());
                    }
                }
                std::vector<object*> items;
                for (auto const& p : parts)
                    items.push_back(make_text(p));
                return make_list(std::move(items));
            }

            object* partition_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, char const* aName, bool aReverse)
            {
                std::vector<object*> a;
                if (!parse_arguments(aName, aArgs, aKwargs, { "sep" }, 1u, 1u, a))
                    return nullptr;
                if (kind_of(a[0]) != NEOS_PY_STR)
                    return error("TypeError", std::string{ "must be str, not " } + type_name(a[0]));
                auto const& text = as<str_object>(aSelf)->value;
                auto const& separator = as<str_object>(a[0])->value;
                if (separator.empty())
                    return error("ValueError", "empty separator");
                auto const i = aReverse ? text.rfind(separator) : text.find(separator);
                if (i == std::string::npos)
                    return aReverse ? make_tuple({ make_str(""), make_str(""), make_str(text) }) : make_tuple({ make_str(text), make_str(""), make_str("") });
                return make_tuple({ make_str(text.substr(0u, i)), make_str(separator), make_str(text.substr(i + separator.size())) });
            }

            object* affix_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, char const* aName, bool aStart)
            {
                std::vector<object*> a;
                if (!parse_arguments(aName, aArgs, aKwargs, { "prefix", "start", "end" }, 1u, 3u, a))
                    return nullptr;
                long long start = 0, end = 0;
                if (!slice_index(a[1], 0, start) || !slice_index(a[2], std::numeric_limits<long long>::max(), end))
                    return nullptr;
                std::string const bare = aStart ? "startswith" : "endswith";
                std::vector<object*> candidates;
                if (kind_of(a[0]) == NEOS_PY_TUPLE)
                {
                    candidates = as<tuple_object>(a[0])->items;
                }
                else if (kind_of(a[0]) == NEOS_PY_STR)
                    candidates.push_back(a[0]);
                else
                    return error("TypeError", bare + " first arg must be str or a tuple of str, not " + type_name(a[0]));
                // (as UTF-8 (its code points are its bytes if it is ASCII; a prefix or suffix of its code points is one of its bytes): if 
                // it is ASCII or the whole of it is searched)
                auto const& self = *as<str_object>(aSelf);
                if (self.is_ascii() || (start == 0 && end == std::numeric_limits<long long>::max()))
                {
                    std::string_view const bytes{ self.value };
                    for (auto c : candidates)
                    {
                        if (kind_of(c) != NEOS_PY_STR)
                            return error("TypeError", "tuple for " + bare + " must only contain str, not " + type_name(c));
                        std::string_view const affix{ as<str_object>(c)->value };
                        auto s = start;
                        auto e = end;
                        adjust_indices(s, e, static_cast<long long>(bytes.size()));
                        e -= static_cast<long long>(affix.size());
                        if (e < s)
                            continue;
                        auto const at = aStart ? s : e;
                        if (bytes.substr(static_cast<std::size_t>(at), affix.size()) == affix)
                            return make_bool(true);
                    }
                    return make_bool(false);
                }
                std::u32string storage;
                auto const& text = points_of(aSelf, storage);
                for (auto c : candidates)
                {
                    if (kind_of(c) != NEOS_PY_STR)
                        return error("TypeError", "tuple for " + bare + " must only contain str, not " + type_name(c));
                    auto const affix = to_code_points(as<str_object>(c)->value);
                    auto s = start;
                    auto e = end;
                    adjust_indices(s, e, static_cast<long long>(text.size()));
                    e -= static_cast<long long>(affix.size());
                    if (e < s)
                        continue;
                    auto const at = aStart ? s : e;
                    if (std::u32string_view{ text }.substr(static_cast<std::size_t>(at), affix.size()) == affix)
                        return make_bool(true);
                }
                return make_bool(false);
            }

            object* splitlines_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("str.splitlines", aArgs, aKwargs, { "keepends" }, 0u, 0u, a))
                    return nullptr;
                bool keepends = false;
                if (a[0] != nullptr)
                {
                    if (kind_of(a[0]) == NEOS_PY_FLOAT)
                        return error("TypeError", "'float' object cannot be interpreted as an integer");
                    keepends = truth(a[0]) == 1;
                }
                std::u32string storage;
                auto const& text = points_of(aSelf, storage);
                std::vector<object*> lines;
                std::size_t i = 0u;
                while (i < text.size())
                {
                    auto j = i;
                    while (j < text.size() && !is_linebreak(text[j]))
                        ++j;
                    auto eol = j;
                    if (j < text.size())
                    {
                        if (text[j] == U'\r' && j + 1u < text.size() && text[j + 1u] == U'\n')
                            j += 2u;
                        else
                            ++j;
                        if (keepends)
                            eol = j;
                    }
                    lines.push_back(make_text(std::u32string_view{ text }.substr(i, eol - i)));
                    i = j;
                }
                return make_list(std::move(lines));
            }

            object* replace_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("str.replace", aArgs, aKwargs, { "old", "new", "count" }, 2u, 2u, a))
                    return nullptr;
                for (std::size_t i = 0u; i < 2u; ++i)
                    if (kind_of(a[i]) != NEOS_PY_STR)
                        return error("TypeError", "replace() argument " + std::to_string(i + 1u) + " must be str, not " + type_name(a[i]));
                long long count = -1;
                if (a[2] != nullptr && !size_argument(a[2], count))
                    return nullptr;
                if (count < 0)
                    count = std::numeric_limits<long long>::max();
                auto const& text = as<str_object>(aSelf)->value;
                auto const& from = as<str_object>(a[0])->value;
                auto const& to = as<str_object>(a[1])->value;
                std::string result;
                if (from.empty())
                {
                    // (the replacement between the characters (and at the ends))
                    auto const points = to_code_points(text);
                    for (std::size_t i = 0u; i < points.size(); ++i)
                    {
                        if (count-- > 0)
                            result += to;
                        append_code_point(result, points[i]);
                    }
                    if (count > 0)
                        result += to;
                    return make_str(std::move(result));
                }
                std::size_t i = 0u;
                for (auto j = text.find(from); j != std::string::npos && count > 0; j = text.find(from, i), --count)
                {
                    result += text.substr(i, j - i) + to;
                    i = j + from.size();
                }
                result += text.substr(i);
                return make_str(std::move(result));
            }

            object* join_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("str.join", aArgs, aKwargs, { "iterable" }, 1u, 1u, a))
                    return nullptr;
                std::vector<object*> items;
                if (!collect(a[0], items))
                {
                    if (std::string_view{ error_text() }.starts_with("TypeError: '"))
                    {
                        clear_error();
                        return error("TypeError", "can only join an iterable");
                    }
                    return nullptr;
                }
                std::string result;
                std::string message;
                for (std::size_t i = 0u; i < items.size() && message.empty(); ++i)
                {
                    if (kind_of(items[i]) != NEOS_PY_STR)
                    {
                        message = "sequence item " + std::to_string(i) + ": expected str instance, " + type_name(items[i]) + " found";
                        break;
                    }
                    if (i != 0u)
                        result += as<str_object>(aSelf)->value;
                    result += as<str_object>(items[i])->value;
                }
                for (auto item : items)
                    release(item);
                if (!message.empty())
                    return error("TypeError", message);
                return make_str(std::move(result));
            }

            object* expandtabs_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("str.expandtabs", aArgs, aKwargs, { "tabsize" }, 0u, 0u, a))
                    return nullptr;
                long long tabsize = 8;
                if (a[0] != nullptr && !size_argument(a[0], tabsize))
                    return nullptr;
                std::u32string storage;
                std::u32string result;
                long long column = 0;
                for (auto c : points_of(aSelf, storage))
                {
                    if (c == U'\t')
                    {
                        if (tabsize > 0)
                        {
                            auto const spaces = tabsize - column % tabsize;
                            result.append(static_cast<std::size_t>(spaces), U' ');
                            column += spaces;
                        }
                    }
                    else
                    {
                        result += c;
                        column = c == U'\n' || c == U'\r' ? 0 : column + 1;
                    }
                }
                return make_text(result);
            }

            object* zfill_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("str.zfill", aArgs, aKwargs, { "width" }, 1u, 1u, a))
                    return nullptr;
                long long width = 0;
                if (!size_argument(a[0], width))
                    return nullptr;
                std::u32string storage;
                auto text = points_of(aSelf, storage);
                if (width <= static_cast<long long>(text.size()))
                    return make_str(as<str_object>(aSelf)->value);
                auto const fill = static_cast<std::size_t>(width) - text.size();
                std::size_t const at = !text.empty() && (text[0] == U'+' || text[0] == U'-') ? 1u : 0u;
                text.insert(at, fill, U'0');
                return make_text(text);
            }

            object* istitle_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                if (!no_arguments("str.istitle", aArgs, aKwargs))
                    return nullptr;
                std::u32string storage;
                bool cased = false;
                bool previousCased = false;
                for (auto c : points_of(aSelf, storage))
                {
                    if (is_upper(c) || is_title(c))
                    {
                        if (previousCased)
                            return make_bool(false);
                        previousCased = true;
                        cased = true;
                    }
                    else if (is_lower(c))
                    {
                        if (!previousCased)
                            return make_bool(false);
                        previousCased = true;
                        cased = true;
                    }
                    else
                        previousCased = false;
                }
                return make_bool(cased);
            }

            object* isidentifier_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                if (!no_arguments("str.isidentifier", aArgs, aKwargs))
                    return nullptr;
                std::u32string storage;
                auto const& text = points_of(aSelf, storage);
                if (text.empty() || !unicode_is(text[0], unicode_flag::XID_START))
                    return make_bool(false);
                for (auto c : text)
                    if (!unicode_is(c, unicode_flag::XID_CONTINUE))
                        return make_bool(false);
                return make_bool(true);
            }

            object* affix_remove(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, bool aPrefix)
            {
                std::vector<object*> a;
                char const* const name = aPrefix ? "str.removeprefix" : "str.removesuffix";
                if (!parse_arguments(name, aArgs, aKwargs, { aPrefix ? "prefix" : "suffix" }, 1u, 1u, a))
                    return nullptr;
                if (kind_of(a[0]) != NEOS_PY_STR)
                    return error("TypeError", std::string{ name + 4 } + "() argument must be str, not " + type_name(a[0]));
                auto const& text = as<str_object>(aSelf)->value;
                auto const& affix = as<str_object>(a[0])->value;
                if (aPrefix && text.starts_with(affix))
                    return make_str(text.substr(affix.size()));
                if (!aPrefix && !affix.empty() && text.ends_with(affix))
                    return make_str(text.substr(0u, text.size() - affix.size()));
                return make_str(text);
            }

            object* translate_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("str.translate", aArgs, aKwargs, { "table" }, 1u, 1u, a))
                    return nullptr;
                std::u32string storage;
                std::string result;
                for (auto c : points_of(aSelf, storage))
                {
                    ref const key{ make_int(static_cast<long long>(c)) };
                    ref const value{ getitem(a[0], key.get()) };
                    if (!value)
                    {
                        std::string_view const text{ error_text() };
                        if (text.starts_with("LookupError") || text.starts_with("KeyError") || text.starts_with("IndexError"))
                        {
                            clear_error();
                            append_code_point(result, c);
                            continue;
                        }
                        return nullptr;
                    }
                    auto const k = kind_of(value.get());
                    if (k == NEOS_PY_NONE)
                        continue;
                    if (k == NEOS_PY_STR)
                        result += as<str_object>(value.get())->value;
                    else if (is_int_like(value.get()))
                    {
                        long long n = 0;
                        if (!to_index(value.get(), n))
                            return nullptr;
                        if (n < 0 || n > 0x10FFFF)
                            return error("ValueError", "character mapping must be in range(0x110000)");
                        append_code_point(result, static_cast<char32_t>(n));
                    }
                    else
                        return error("TypeError", "character mapping must return integer, None or str");
                }
                return make_str(std::move(result));
            }

            object* format_map_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("str.format_map", aArgs, aKwargs, { "mapping" }, 1u, 1u, a))
                    return nullptr;
                ref const format{ getattr(aSelf, "format") };
                if (!format)
                    return nullptr;
                ref const empty{ make_tuple() };
                if (kind_of(a[0]) == NEOS_PY_DICT)
                    return call(format.get(), empty.get(), a[0]);
                // (another mapping: its items)
                ref const items{ call_method_named(a[0], "items", {}) };
                if (!items)
                    return nullptr;
                ref const d{ make_dict() };
                std::vector<object*> pairs;
                if (!collect(items.get(), pairs))
                    return nullptr;
                for (auto p : pairs)
                {
                    ref const pair{ p };
                    ref const k{ getitem(pair.get(), ref{ make_int(0) }.get()) };
                    ref const v{ getitem(pair.get(), ref{ make_int(1) }.get()) };
                    if (!k || !v)
                        return nullptr;
                    dict_set(d.get(), k.get(), v.get());
                }
                return call(format.get(), empty.get(), d.get());
            }

            str_method_entry const sStrMethods[] =
            {
                { "upper", [](object* s, std::vector<object*> const& a, object* k) { return case_method(s, a, k, "str.upper", 0); } },
                { "lower", [](object* s, std::vector<object*> const& a, object* k) { return case_method(s, a, k, "str.lower", 1); } },
                { "casefold", [](object* s, std::vector<object*> const& a, object* k) { return case_method(s, a, k, "str.casefold", 2); } },
                { "swapcase", [](object* s, std::vector<object*> const& a, object* k) { return case_method(s, a, k, "str.swapcase", 3); } },
                { "title", [](object* s, std::vector<object*> const& a, object* k) { return case_method(s, a, k, "str.title", 4); } },
                { "capitalize", [](object* s, std::vector<object*> const& a, object* k) { return case_method(s, a, k, "str.capitalize", 5); } },
                { "center", [](object* s, std::vector<object*> const& a, object* k) { return pad(s, a, k, "str.center", 0); } },
                { "ljust", [](object* s, std::vector<object*> const& a, object* k) { return pad(s, a, k, "str.ljust", 1); } },
                { "rjust", [](object* s, std::vector<object*> const& a, object* k) { return pad(s, a, k, "str.rjust", 2); } },
                { "zfill", zfill_method },
                { "count", [](object* s, std::vector<object*> const& a, object* k) -> object*
                    {
                        std::u32string text, sub;
                        long long start = 0, end = 0;
                        std::optional<ascii_search> ascii;
                        if (!search_arguments(s, a, k, "str.count", text, sub, start, end, &ascii))
                            return nullptr;
                        return make_int(ascii ? count_in(ascii->text, ascii->sub, start, end) : count_in(std::u32string_view{ text }, std::u32string_view{ sub }, start, end));
                    } },
                { "find", [](object* s, std::vector<object*> const& a, object* k) { return find_method(s, a, k, "str.find", false, false); } },
                { "rfind", [](object* s, std::vector<object*> const& a, object* k) { return find_method(s, a, k, "str.rfind", true, false); } },
                { "index", [](object* s, std::vector<object*> const& a, object* k) { return find_method(s, a, k, "str.index", false, true); } },
                { "rindex", [](object* s, std::vector<object*> const& a, object* k) { return find_method(s, a, k, "str.rindex", true, true); } },
                { "startswith", [](object* s, std::vector<object*> const& a, object* k) { return affix_method(s, a, k, "str.startswith", true); } },
                { "endswith", [](object* s, std::vector<object*> const& a, object* k) { return affix_method(s, a, k, "str.endswith", false); } },
                { "strip", [](object* s, std::vector<object*> const& a, object* k) { return strip_method(s, a, k, "str.strip", true, true); } },
                { "lstrip", [](object* s, std::vector<object*> const& a, object* k) { return strip_method(s, a, k, "str.lstrip", true, false); } },
                { "rstrip", [](object* s, std::vector<object*> const& a, object* k) { return strip_method(s, a, k, "str.rstrip", false, true); } },
                { "split", [](object* s, std::vector<object*> const& a, object* k) { return split_method(s, a, k, "str.split", false); } },
                { "rsplit", [](object* s, std::vector<object*> const& a, object* k) { return split_method(s, a, k, "str.rsplit", true); } },
                { "splitlines", splitlines_method },
                { "partition", [](object* s, std::vector<object*> const& a, object* k) { return partition_method(s, a, k, "str.partition", false); } },
                { "rpartition", [](object* s, std::vector<object*> const& a, object* k) { return partition_method(s, a, k, "str.rpartition", true); } },
                { "join", join_method },
                { "replace", replace_method },
                { "removeprefix", [](object* s, std::vector<object*> const& a, object* k) { return affix_remove(s, a, k, true); } },
                { "removesuffix", [](object* s, std::vector<object*> const& a, object* k) { return affix_remove(s, a, k, false); } },
                { "expandtabs", expandtabs_method },
                { "translate", translate_method },
                { "format_map", format_map_method },
                { "isascii", [](object* s, std::vector<object*> const& a, object* k) { return predicate(s, a, k, "str.isascii", [](char32_t c) { return c < 0x80u; }, true); } },
                { "isalpha", [](object* s, std::vector<object*> const& a, object* k) { return predicate(s, a, k, "str.isalpha", is_alpha); } },
                { "isalnum", [](object* s, std::vector<object*> const& a, object* k) { return predicate(s, a, k, "str.isalnum", is_alnum); } },
                { "isdecimal", [](object* s, std::vector<object*> const& a, object* k) { return predicate(s, a, k, "str.isdecimal", is_decimal); } },
                { "isdigit", [](object* s, std::vector<object*> const& a, object* k) { return predicate(s, a, k, "str.isdigit", is_digit); } },
                { "isnumeric", [](object* s, std::vector<object*> const& a, object* k) { return predicate(s, a, k, "str.isnumeric", is_numeric); } },
                { "isspace", [](object* s, std::vector<object*> const& a, object* k) { return predicate(s, a, k, "str.isspace", is_space); } },
                { "isprintable", [](object* s, std::vector<object*> const& a, object* k) { return predicate(s, a, k, "str.isprintable", is_printable, true); } },
                { "islower", [](object* s, std::vector<object*> const& a, object* k) { return cased_predicate(s, a, k, "str.islower", false); } },
                { "isupper", [](object* s, std::vector<object*> const& a, object* k) { return cased_predicate(s, a, k, "str.isupper", true); } },
                { "istitle", istitle_method },
                { "isidentifier", isidentifier_method },
                { "encode", str_encode },
                { "maketrans", [](object*, std::vector<object*> const& a, object* k) { return str_maketrans(a, k); } }
            };

            object* call_str_method(neos_py_context*, void* aData, neos_py_object* aArgs, neos_py_object* aKwargs)
            {
                auto const& entry = *static_cast<str_method_entry const*>(aData);
                auto const& args = as<tuple_object>(aArgs)->items;
                if (std::strcmp(entry.name, "maketrans") == 0)
                    return entry.function(nullptr, args, aKwargs);    // (a static method)
                if (args.empty())
                    return error("TypeError", std::string{ "unbound method str." } + entry.name + "() needs an argument");
                if (kind_of(args[0]) != NEOS_PY_STR)
                    return error("TypeError", std::string{ "descriptor '" } + entry.name + "' for 'str' objects doesn't apply to a '" + type_name(args[0]) + "' object");
                std::vector<object*> rest{ args.begin() + 1, args.end() };
                auto const kwargs = aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty() ? aKwargs : nullptr;
                return entry.function(args[0], rest, kwargs);
            }
        }

        object* str_maketrans(std::vector<object*> const& aArgs, object* aKwargs)
        {
            std::vector<object*> a;
            if (!parse_arguments("maketrans", aArgs, aKwargs, { "x", "y", "z" }, 1u, 3u, a))
                return nullptr;
            ref const result{ make_dict() };
            if (a[1] == nullptr)
            {
                if (kind_of(a[0]) != NEOS_PY_DICT)
                    return error("TypeError", "if you give only one argument to maketrans it must be a dict");
                for (auto const& item : as<dict_object>(a[0])->items)
                {
                    ref key;
                    if (kind_of(item.first) == NEOS_PY_STR)
                    {
                        auto const points = to_code_points(as<str_object>(item.first)->value);
                        if (points.size() != 1u)
                            return error("ValueError", "string keys in translate table must be of length 1");
                        key = ref{ make_int(static_cast<long long>(points[0])) };
                    }
                    else if (is_int_like(item.first))
                        key = ref::borrowed(item.first);
                    else
                        return error("TypeError", "keys in translate table must be strings or integers");
                    dict_set(result.get(), key.get(), item.second);
                }
                retain(result.get());
                return result.get();
            }
            if (kind_of(a[0]) != NEOS_PY_STR || kind_of(a[1]) != NEOS_PY_STR)
                return error("TypeError", std::string{ "maketrans() argument " } + (kind_of(a[0]) != NEOS_PY_STR ? "1" : "2") + " must be str, not " +
                    type_name(kind_of(a[0]) != NEOS_PY_STR ? a[0] : a[1]));
            auto const x = to_code_points(as<str_object>(a[0])->value);
            auto const y = to_code_points(as<str_object>(a[1])->value);
            if (x.size() != y.size())
                return error("ValueError", "the first two maketrans arguments must have equal length");
            for (std::size_t i = 0u; i < x.size(); ++i)
            {
                ref const key{ make_int(static_cast<long long>(x[i])) };
                ref const value{ make_int(static_cast<long long>(y[i])) };
                dict_set(result.get(), key.get(), value.get());
            }
            if (a[2] != nullptr)
            {
                if (kind_of(a[2]) != NEOS_PY_STR)
                    return error("TypeError", std::string{ "maketrans() argument 3 must be str, not " } + type_name(a[2]));
                for (auto c : to_code_points(as<str_object>(a[2])->value))
                {
                    ref const key{ make_int(static_cast<long long>(c)) };
                    dict_set(result.get(), key.get(), none());
                }
            }
            retain(result.get());
            return result.get();
        }

        // (character properties (re's))
        char32_t char_lower(char32_t aCharacter) { return unicode_simple_case(aCharacter, unicode_case::Lower); }
        char32_t char_upper(char32_t aCharacter) { return unicode_simple_case(aCharacter, unicode_case::Upper); }
        bool char_is_space(char32_t aCharacter) { return is_space(aCharacter); }
        bool char_is_alnum(char32_t aCharacter) { return is_alnum(aCharacter); }
        bool char_is_decimal(char32_t aCharacter) { return is_decimal(aCharacter); }
        bool char_is_linebreak(char32_t aCharacter) { return is_linebreak(aCharacter); }

        object* str_method(object* aSelf, char const* aName)
        {
            for (auto const& entry : sStrMethods)
                if (std::strcmp(entry.name, aName) == 0)
                    return make_function(entry.name, call_str_method, const_cast<str_method_entry*>(&entry), std::strcmp(aName, "maketrans") == 0 ? nullptr : aSelf);
            return nullptr;
        }

        native_method str_method_function(char const* aName)
        {
            // (by name: made once)
            static std::unordered_map<std::string_view, native_method> const sFunctions = []()
            {
                std::unordered_map<std::string_view, native_method> result;
                for (auto const& entry : sStrMethods)
                    if (std::strcmp(entry.name, "maketrans") != 0)
                        result.emplace(entry.name, entry.function);
                return result;
            }();
            auto const f = sFunctions.find(aName);
            return f != sFunctions.end() ? f->second : nullptr;
        }
    }
}
