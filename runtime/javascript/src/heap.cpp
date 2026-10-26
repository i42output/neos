/*
  heap.cpp

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

// The heap's values (strings, symbols, BigInts, objects and environments: see gc.cpp for its memory and collection) and strings

#include <algorithm>
#include <charconv>
#include "js.hpp"
#include "gc.hpp"

namespace neos::javascript_runtime
{
    // --- the heap

    bool valid(value aValue)
    {
        auto const tag = tag_of(aValue);
        if (tag < TagSpecial)
            return true;
        if (tag == TagSpecial)
            return aValue <= Exception;
        auto const* c = the_heap().find(handle_of(aValue));
        if (c == nullptr)
            return false;
        switch (tag)
        {
        case TagString: return c->type == cell_type::String;
        case TagSymbol: return c->type == cell_type::Symbol;
        case TagBigInt: return c->type == cell_type::BigInt;
        case TagObject: return c->type == cell_type::Object;
        case TagInternal: return c->type == cell_type::Internal;
        default: return false;
        }
    }

    // --- strings

    region_u16string const& string::flat()
    {
        if (smallText)
        {
            text.assign(small, length);
            smallText = false;
            charsOffset = region_offset(text.data());
        }
        if (base != 0u && text.empty())
        {
            text.assign(reinterpret_cast<char16_t const*>(gRegionBase + charsOffset), length);
            charsOffset = region_offset(text.data());
        }
        if (left == 0u)
            return text;
        // (a rope: its leaves in order (iteratively: a rope made by repeated concatenation is deep))
        region_u16string result;
        result.reserve(length);
        thread_local std::vector<value> tPending;
        tPending.clear();
        tPending.push_back(right);
        tPending.push_back(left);
        while (!tPending.empty())
        {
            auto& s = cell_of<string>(tPending.back());
            tPending.pop_back();
            if (s.left == 0u)
                result.append(s.chars());
            else
            {
                tPending.push_back(s.right);
                tPending.push_back(s.left);
            }
        }
        text = std::move(result);
        charsOffset = region_offset(text.data());
        left = 0u;
        right = 0u;
        return text;
    }

    std::size_t string::hash()
    {
        if (!hashed)
        {
            hashValue = std::hash<std::u16string_view>{}(chars());
            hashed = true;
        }
        return hashValue;
    }

    std::int64_t string::array_index()
    {
        if (arrayIndex != -2)
            return arrayIndex;
        auto const t = chars();
        arrayIndex = -1;
        if (t.empty() || t.size() > 10u || (t.size() > 1u && t[0] == u'0'))
            return arrayIndex;
        std::uint64_t n = 0u;
        for (auto c : t)
        {
            if (c < u'0' || c > u'9')
                return arrayIndex;
            n = n * 10u + static_cast<std::uint64_t>(c - u'0');
        }
        if (n < 0xFFFFFFFFull)
            arrayIndex = static_cast<std::int64_t>(n);
        return arrayIndex;
    }

    namespace
    {
        struct intern_table
        {
            // (pinned strings (that live as long as the program): marked by the collector (pinnedValues, in the order they were 
            // pinned); the others (weak) live while they are referred to (those that die are removed))
            std::unordered_map<std::u16string_view, value> pinned;
            std::unordered_map<std::u16string_view, value> weak;
            std::vector<value> pinnedValues;
            value find(std::u16string_view aText) const
            {
                if (auto const p = pinned.find(aText); p != pinned.end())
                    return p->second;
                if (auto const w = weak.find(aText); w != weak.end())
                    return w->second;
                return 0u;
            }
            void pin(std::u16string_view aText, value aString)
            {
                pinned.emplace(aText, aString);
                pinnedValues.push_back(aString);
            }
        };

        intern_table& interned()
        {
            static isolate_local<intern_table> sTable;
            return *sTable;
        }

        // (pinned interned strings live as long as the program (compiled code's literals are interned strings that it keeps in its 
        // globals; the runtime keeps others in statics); the others live while they are referred to: those that die are removed)
        root_tracer const sInternedRoots{ [](tracer& aTracer)
        {
            for (auto const v : interned().pinnedValues)
                aTracer.mark(v);
        }, [](tracer& aTracer)
        {
            std::erase_if(interned().weak, [&](auto const& aEntry) { return !aTracer.alive(aEntry.second); });
        } };
    }

    value make_string(std::u16string aText)
    {
        if (aText.size() > MaxStringLength)
            throw_range_error("invalid string length");
        the_heap().account(aText.size() * sizeof(char16_t));
        auto& s = the_heap().make<string>(std::move(aText));
        return TagString | s.handle;
    }

    value make_string(std::u16string_view aText)
    {
        // (its code units copied once: to its own storage (in the region))
        if (aText.size() <= string::SmallCapacity)
            return make_small_string(aText);
        if (aText.size() > MaxStringLength)
            throw_range_error("invalid string length");
        the_heap().account(aText.size() * sizeof(char16_t));
        auto& s = the_heap().make<string>(aText);
        return TagString | s.handle;
    }

    value make_small_string(std::u16string_view aText)
    {
        auto& s = the_heap().make<string>(string::small_tag{}, aText);
        return TagString | s.handle;
    }

    value make_string(char16_t aCharacter)
    {
        // (single code unit strings (ASCII) are shared)
        if (aCharacter < 0x80u)
        {
            struct ascii_strings
            {
                value strings[0x80] = {};
            };
            static isolate_local<ascii_strings> sStrings;
            auto& s = sStrings->strings[aCharacter];
            if (s == 0u)
                s = intern(std::u16string_view{ &aCharacter, 1u });
            return s;
        }
        return make_string(std::u16string(1u, aCharacter));
    }

    std::u16string utf16(std::string_view aText)
    {
        std::u16string result;
        result.reserve(aText.size());
        for (std::size_t i = 0u; i < aText.size();)
        {
            auto const c = static_cast<unsigned char>(aText[i]);
            char32_t cp = 0xFFFDu;
            std::size_t n = 1u;
            if (c < 0x80u)
                cp = c;
            else if ((c & 0xE0u) == 0xC0u && i + 1u < aText.size())
            {
                cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(aText[i + 1u]) & 0x3Fu);
                n = 2u;
            }
            else if ((c & 0xF0u) == 0xE0u && i + 2u < aText.size())
            {
                cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(aText[i + 1u]) & 0x3Fu) << 6) | (static_cast<unsigned char>(aText[i + 2u]) & 0x3Fu);
                n = 3u;
            }
            else if ((c & 0xF8u) == 0xF0u && i + 3u < aText.size())
            {
                cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(aText[i + 1u]) & 0x3Fu) << 12) |
                    ((static_cast<unsigned char>(aText[i + 2u]) & 0x3Fu) << 6) | (static_cast<unsigned char>(aText[i + 3u]) & 0x3Fu);
                n = 4u;
            }
            i += n;
            if (cp >= 0x10000u)
            {
                cp -= 0x10000u;
                result.push_back(static_cast<char16_t>(0xD800u + (cp >> 10)));
                result.push_back(static_cast<char16_t>(0xDC00u + (cp & 0x3FFu)));
            }
            else
                result.push_back(static_cast<char16_t>(cp));
        }
        return result;
    }

    std::u16string utf16_ascii(std::string_view aText)
    {
        return std::u16string(aText.begin(), aText.end());
    }

    value make_string_utf8(std::string_view aText)
    {
        return make_string(utf16(aText));
    }

    namespace
    {
        void append_utf8(std::string& aResult, char32_t aCodePoint)
        {
            if (aCodePoint < 0x80u)
                aResult.push_back(static_cast<char>(aCodePoint));
            else if (aCodePoint < 0x800u)
            {
                aResult.push_back(static_cast<char>(0xC0u | (aCodePoint >> 6)));
                aResult.push_back(static_cast<char>(0x80u | (aCodePoint & 0x3Fu)));
            }
            else if (aCodePoint < 0x10000u)
            {
                aResult.push_back(static_cast<char>(0xE0u | (aCodePoint >> 12)));
                aResult.push_back(static_cast<char>(0x80u | ((aCodePoint >> 6) & 0x3Fu)));
                aResult.push_back(static_cast<char>(0x80u | (aCodePoint & 0x3Fu)));
            }
            else
            {
                aResult.push_back(static_cast<char>(0xF0u | (aCodePoint >> 18)));
                aResult.push_back(static_cast<char>(0x80u | ((aCodePoint >> 12) & 0x3Fu)));
                aResult.push_back(static_cast<char>(0x80u | ((aCodePoint >> 6) & 0x3Fu)));
                aResult.push_back(static_cast<char>(0x80u | (aCodePoint & 0x3Fu)));
            }
        }

        std::string encode(std::u16string_view aText, bool aWtf)
        {
            std::string result;
            result.reserve(aText.size());
            for (std::size_t i = 0u; i < aText.size(); ++i)
            {
                char32_t cp = aText[i];
                if (cp >= 0xD800u && cp < 0xDC00u && i + 1u < aText.size() && aText[i + 1u] >= 0xDC00u && aText[i + 1u] < 0xE000u)
                {
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (aText[i + 1u] - 0xDC00u);
                    ++i;
                }
                else if (cp >= 0xD800u && cp < 0xE000u && !aWtf)
                    cp = 0xFFFDu;
                append_utf8(result, cp);
            }
            return result;
        }
    }

    std::string utf8(value aString)
    {
        return encode(str(aString), false);
    }

    std::string wtf8(std::u16string_view aText)
    {
        return encode(aText, true);
    }

    value intern(std::u16string_view aText)
    {
        auto& table = interned();
        if (auto existing = table.pinned.find(aText); existing != table.pinned.end())
            return existing->second;
        if (auto existing = table.weak.find(aText); existing != table.weak.end())
        {
            // (pinned from now on)
            auto const result = existing->second;
            auto const key = existing->first;
            table.weak.erase(existing);
            cell_of<string>(result).pinned = true;
            table.pin(key, result);
            return result;
        }
        auto& s = the_heap().make<string>(std::u16string{ aText });
        s.interned = true;
        s.pinned = true;
        auto const result = TagString | s.handle;
        table.pin(std::u16string_view{ s.text }, result);
        return result;
    }

    value intern(value aString)
    {
        auto& s = cell_of<string>(aString);
        if (s.interned)
            return aString;
        auto& table = interned();
        auto const& text = s.flat();
        if (auto const existing = table.find(std::u16string_view{ text }); existing != 0u)
            return existing;
        s.interned = true;
        table.weak.emplace(std::u16string_view{ s.text }, aString);
        return aString;
    }

    value ascii(char const* aText)
    {
        std::u16string text;
        for (auto p = aText; *p != '\0'; ++p)
            text.push_back(static_cast<unsigned char>(*p));
        return intern(text);
    }

    value concat(value aLeft, value aRight)
    {
        auto& l = cell_of<string>(aLeft);
        auto& r = cell_of<string>(aRight);
        if (l.length == 0u)
            return aRight;
        if (r.length == 0u)
            return aLeft;
        auto const length = static_cast<std::uint64_t>(l.length) + r.length;
        if (length > MaxStringLength)
            throw_range_error("invalid string length");
        if (length <= string::SmallCapacity)
        {
            char16_t text[string::SmallCapacity];
            auto const lc = l.chars();
            auto const rc = r.chars();
            std::copy(rc.begin(), rc.end(), std::copy(lc.begin(), lc.end(), text));
            return make_small_string(std::u16string_view{ text, static_cast<std::size_t>(length) });
        }
        if (length <= 32u)
        {
            std::u16string text;
            text.reserve(static_cast<std::size_t>(length));
            text.append(l.chars());
            text.append(r.chars());
            return make_string(std::move(text));
        }
        auto& s = the_heap().make<string>(aLeft, aRight, static_cast<std::uint32_t>(length));
        return TagString | s.handle;
    }

    value concat_number(value aString, double aNumber, bool aNumberFirst)
    {
        // (string + number, or number + string: one string made (not the number's string and then the two concatenated))
        char16_t digits[32];
        auto const count = number_characters(aNumber, digits);
        auto& s = cell_of<string>(aString);
        auto const length = static_cast<std::uint64_t>(s.length) + count;
        if (length <= string::SmallCapacity)
        {
            char16_t text[string::SmallCapacity];
            auto const sc = s.chars();
            auto* p = text;
            if (aNumberFirst)
                p = std::copy(digits, digits + count, p);
            p = std::copy(sc.begin(), sc.end(), p);
            if (!aNumberFirst)
                p = std::copy(digits, digits + count, p);
            return make_small_string(std::u16string_view{ text, static_cast<std::size_t>(length) });
        }
        if (length > 32u)
        {
            auto const n = make_string(std::u16string{ digits, count });
            return aNumberFirst ? concat(n, aString) : concat(aString, n);
        }
        std::u16string text;
        text.reserve(static_cast<std::size_t>(length));
        if (aNumberFirst)
            text.append(digits, count);
        text.append(s.chars());
        if (!aNumberFirst)
            text.append(digits, count);
        return make_string(std::move(text));
    }

    std::u16string_view str(value aString)
    {
        return cell_of<string>(aString).chars();
    }

    value substring_of(value aString, std::size_t aStart, std::size_t aLength)
    {
        // (a long one is a slice (it shares its code units: none are copied; the string whose they are (the base) lives while it does))
        constexpr std::size_t MinimumSlice = 24u;
        auto& s = cell_of<string>(aString);
        if (aStart == 0u && aLength == s.length)
            return aString;
        if (aLength < MinimumSlice || s.smallText)
            return make_string(s.chars().substr(aStart, aLength));
        auto const base = s.base != 0u && s.text.empty() ? s.base : (s.flat(), aString);
        auto const offset = cell_of<string>(aString).charsOffset + aStart * sizeof(char16_t);
        auto& slice = the_heap().make<string>(string::slice_tag{}, base, offset, static_cast<std::uint32_t>(aLength));
        return TagString | slice.handle;
    }

    bool string_equal(value aLhs, value aRhs)
    {
        if (aLhs == aRhs)
            return true;
        auto& l = cell_of<string>(aLhs);
        auto& r = cell_of<string>(aRhs);
        if (l.length != r.length || (l.interned && r.interned))
            return false;
        return l.flat() == r.flat();
    }

    std::optional<std::uint32_t> array_index(value aKey)
    {
        if (!is_string(aKey))
            return {};
        auto const index = cell_of<string>(aKey).array_index();
        if (index < 0)
            return {};
        return static_cast<std::uint32_t>(index);
    }

    value index_key(std::uint64_t aIndex)
    {
        if (aIndex < 1024u)
        {
            struct small_keys
            {
                value keys[1024u] = {};
            };
            static isolate_local<small_keys> sSmall;
            auto& k = sSmall->keys[static_cast<std::size_t>(aIndex)];
            if (k == 0u)
            {
                char digits[24];
                auto const end = std::to_chars(std::begin(digits), std::end(digits), aIndex).ptr;
                k = intern(utf16_ascii(std::string_view{ digits, static_cast<std::size_t>(end - digits) }));
            }
            return k;
        }
        char digits[24];
        auto const end = std::to_chars(std::begin(digits), std::end(digits), aIndex).ptr;
        auto text = utf16_ascii(std::string_view{ digits, static_cast<std::size_t>(end - digits) });
        if (auto const existing = interned().find(std::u16string_view{ text }); existing != 0u)
            return existing;
        return intern(make_string(std::move(text)));    // (not pinned: it lives while it is referred to)
    }

    // --- symbols

    value make_symbol(value aDescription)
    {
        auto& s = the_heap().make<symbol>(aDescription);
        return TagSymbol | s.handle;
    }
}
