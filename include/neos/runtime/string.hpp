/*
  string.hpp

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

#pragma once

// the strings of languages' runtimes (see plugin.h): a std::variant of a string's text as UTF-8, or as one of three fixed widths
// (a code point per unit of one, two or four bytes: UCS-1 (Latin-1), UCS-2 and UCS-4, as CPython's strings are (PEP 393)), each
// immutable (e.g. Python's str) or mutable (for languages whose strings are mutable): eight alternatives.
//
// - len() is the number of code points; at() (operator[]) is the code point at an index.
// - ascii_len() and ascii_at() are the units of the text, ignoring its encoding (UTF-8: its bytes): what code that looks only for
//   ASCII (e.g. line endings) uses.
// - An immutable UTF-8 string that is not ASCII makes its text's narrowest fixed width form the first time at() needs it (an
//   index of its code points, kept beside its UTF-8, which does not move: utf8()'s reference stays valid while the string lives).
//   A fixed width string makes its UTF-8 the first time utf8() needs it.
// - Mutable strings can be changed (set(), insert(), append(), erase(), clear()): a fixed width one widens itself for a code point
//   that it cannot hold. freeze() makes a mutable string immutable (a fixed width one at its narrowest width); mutable_copy() is
//   a mutable copy of a string.
// - Strings that are equal (their code points are) have the same hash() whatever their alternatives.
// - A string's const functions fill its caches: a string that threads share needs their lock (e.g. a language's global lock).

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <optional>
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <iterator>
#include <type_traits>
#include <compare>
#include <cstring>

namespace neos::runtime
{
    enum class string_encoding : std::uint8_t
    {
        Utf8,
        Ucs1,
        Ucs2,
        Ucs4
    };

    namespace string_detail
    {
        inline constexpr std::size_t npos = static_cast<std::size_t>(-1);

        // (whether UTF-8 is ASCII: eight bytes at a time)
        inline bool is_ascii(std::string_view aText)
        {
            auto const* p = aText.data();
            auto n = aText.size();
            std::uint64_t bits = 0u;
            for (; n >= 8u; p += 8, n -= 8u)
            {
                std::uint64_t word;
                std::memcpy(&word, p, sizeof(word));
                bits |= word;
            }
            for (; n > 0u; ++p, --n)
                bits |= static_cast<unsigned char>(*p);
            return (bits & 0x8080808080808080ull) == 0u;
        }

        // (UTF-8: the code point that starts at aOffset (which moves past it); U+FFFD for bytes that are not UTF-8 (each a code
        // point); surrogates' three byte forms are code points (as Python's surrogateescape and surrogatepass make them))
        inline char32_t decode(std::string_view aText, std::size_t& aOffset)
        {
            auto const lead = static_cast<unsigned char>(aText[aOffset++]);
            if (lead < 0x80u)
                return lead;
            std::size_t extra = lead >= 0xC0u && lead < 0xE0u ? 1u : lead >= 0xE0u && lead < 0xF0u ? 2u : lead >= 0xF0u && lead < 0xF8u ? 3u : 0u;
            if (extra == 0u)
                return 0xFFFDu;
            char32_t codePoint = lead & (0x3Fu >> extra);
            for (; extra > 0u; --extra)
            {
                if (aOffset >= aText.size() || (static_cast<unsigned char>(aText[aOffset]) & 0xC0u) != 0x80u)
                    return 0xFFFDu;
                codePoint = (codePoint << 6u) | (static_cast<unsigned char>(aText[aOffset++]) & 0x3Fu);
            }
            return codePoint;
        }

        // (a code point's UTF-8 (surrogates' too), appended; U+FFFD for one that is not a code point)
        inline void encode(char32_t aCodePoint, std::string& aText)
        {
            if (aCodePoint < 0x80u)
                aText.push_back(static_cast<char>(aCodePoint));
            else if (aCodePoint < 0x800u)
            {
                aText.push_back(static_cast<char>(0xC0u | (aCodePoint >> 6u)));
                aText.push_back(static_cast<char>(0x80u | (aCodePoint & 0x3Fu)));
            }
            else if (aCodePoint < 0x10000u)
            {
                aText.push_back(static_cast<char>(0xE0u | (aCodePoint >> 12u)));
                aText.push_back(static_cast<char>(0x80u | ((aCodePoint >> 6u) & 0x3Fu)));
                aText.push_back(static_cast<char>(0x80u | (aCodePoint & 0x3Fu)));
            }
            else if (aCodePoint <= 0x10FFFFu)
            {
                aText.push_back(static_cast<char>(0xF0u | (aCodePoint >> 18u)));
                aText.push_back(static_cast<char>(0x80u | ((aCodePoint >> 12u) & 0x3Fu)));
                aText.push_back(static_cast<char>(0x80u | ((aCodePoint >> 6u) & 0x3Fu)));
                aText.push_back(static_cast<char>(0x80u | (aCodePoint & 0x3Fu)));
            }
            else
                encode(0xFFFDu, aText);
        }

        // (FNV-1a, of code points)
        inline constexpr std::uint64_t HashBasis = 0xCBF29CE484222325ull;
        inline std::uint64_t hash_step(std::uint64_t aHash, char32_t aCodePoint)
        {
            return (aHash ^ static_cast<std::uint64_t>(aCodePoint)) * 0x100000001B3ull;
        }

        // (code points as units of a narrower fixed width: each of them fits in one (the caller has checked))
        template <typename Unit, typename Iterator>
        inline std::vector<Unit> narrowed(Iterator aFirst, Iterator aLast)
        {
            std::vector<Unit> result;
            result.reserve(static_cast<std::size_t>(std::distance(aFirst, aLast)));
            for (; aFirst != aLast; ++aFirst)
                result.push_back(static_cast<Unit>(*aFirst));
            return result;
        }

        // (code points in the narrowest fixed width that holds them)
        using fixed_units = std::variant<std::vector<std::uint8_t>, std::vector<char16_t>, std::vector<char32_t>>;
        inline fixed_units narrowest(std::vector<char32_t>&& aCodePoints)
        {
            char32_t const greatest = aCodePoints.empty() ? 0u : *std::max_element(aCodePoints.begin(), aCodePoints.end());
            if (greatest <= 0xFFu)
                return narrowed<std::uint8_t>(aCodePoints.begin(), aCodePoints.end());
            if (greatest <= 0xFFFFu)
                return narrowed<char16_t>(aCodePoints.begin(), aCodePoints.end());
            return std::move(aCodePoints);
        }
    }

    // a code point's UTF-8
    inline std::string utf8_of(char32_t aCodePoint)
    {
        std::string result;
        string_detail::encode(aCodePoint, result);
        return result;
    }

    // --- the alternatives

    // UTF-8
    template <bool Mutable>
    class basic_utf8_string
    {
    public:
        static constexpr bool is_mutable = Mutable;
        static constexpr string_encoding encoding = string_encoding::Utf8;
    public:
        basic_utf8_string() = default;
        // (aLength: its code points, if known (its size: it is ASCII))
        explicit basic_utf8_string(std::string aText, std::size_t aLength = string_detail::npos) :
            iText{ std::move(aText) }, iLength{ aLength }
        {
            if (aLength != string_detail::npos)
                iAscii = aLength == iText.size() ? 1 : 0;
        }
        basic_utf8_string(basic_utf8_string const& aOther) :
            iText{ aOther.iText }, iLength{ aOther.iLength }, iAscii{ aOther.iAscii }, iHashed{ aOther.iHashed }, iHash{ aOther.iHash }
        {
        }
        basic_utf8_string(basic_utf8_string&&) noexcept = default;
        basic_utf8_string& operator=(basic_utf8_string const& aOther)
        {
            if (&aOther != this)
                *this = basic_utf8_string{ aOther };
            return *this;
        }
        basic_utf8_string& operator=(basic_utf8_string&&) noexcept = default;
    public:
        std::size_t len() const
        {
            if (iLength == string_detail::npos)
            {
                std::size_t n = 0u;
                std::string_view const text{ iText };
                if (is_ascii())
                    n = text.size();
                else
                    for (std::size_t offset = 0u; offset < text.size(); ++n)
                        string_detail::decode(text, offset);
                iLength = n;
            }
            return iLength;
        }
        bool is_ascii() const
        {
            if (iAscii < 0)
                iAscii = string_detail::is_ascii(iText) ? 1 : 0;
            return iAscii > 0;
        }
        char32_t at(std::size_t aIndex) const
        {
            if (is_ascii())
                return static_cast<unsigned char>(iText[aIndex]);
            if constexpr (!Mutable)
            {
                // (the index of its code points: made once)
                auto& index = cache().index;
                if (!index)
                {
                    std::vector<char32_t> codePoints;
                    codePoints.reserve(len());
                    std::string_view const text{ iText };
                    for (std::size_t offset = 0u; offset < text.size();)
                        codePoints.push_back(string_detail::decode(text, offset));
                    index = string_detail::narrowest(std::move(codePoints));
                }
                return std::visit([&](auto const& aUnits) -> char32_t { return aUnits[aIndex]; }, *index);
            }
            else
            {
                auto offset = offset_of(aIndex);
                return string_detail::decode(iText, offset);
            }
        }
        // (the offset of a code point's UTF-8 (aIndex <= len()): from the one found last, if it is not after it)
        std::size_t offset_of(std::size_t aIndex) const
        {
            if (is_ascii())
                return aIndex;
            auto& c = cache();
            std::string_view const text{ iText };
            std::size_t index = 0u;
            std::size_t offset = 0u;
            if (c.lastIndex <= aIndex)
            {
                index = c.lastIndex;
                offset = c.lastOffset;
            }
            for (; index < aIndex && offset < text.size(); ++index)
                string_detail::decode(text, offset);
            c.lastIndex = index;
            c.lastOffset = offset;
            return offset;
        }
        std::size_t ascii_len() const
        {
            return iText.size();
        }
        char32_t ascii_at(std::size_t aIndex) const
        {
            return static_cast<unsigned char>(iText[aIndex]);
        }
        std::string const& utf8() const
        {
            return iText;
        }
        std::optional<std::size_t> cached_hash() const
        {
            return iHashed ? std::optional<std::size_t>{ iHash } : std::nullopt;
        }
        void cache_hash(std::size_t aHash) const
        {
            iHash = aHash;
            iHashed = true;
        }
    public:
        // (its text, to change (its caches are discarded))
        std::string& edit() requires Mutable
        {
            reset();
            return iText;
        }
        // (its text, moved out (it is then empty))
        std::string take()
        {
            reset();
            return std::move(iText);
        }
    private:
        // (what is found of a string that is not ASCII: made when it is first needed)
        struct position_cache
        {
            std::optional<string_detail::fixed_units> index;    ///< (an immutable string's code points)
            std::size_t lastIndex = 0u;                         ///< (the code point that offset_of() found last, and its offset)
            std::size_t lastOffset = 0u;
        };
        position_cache& cache() const
        {
            if (!iCache)
                iCache = std::make_unique<position_cache>();
            return *iCache;
        }
        void reset()
        {
            iLength = string_detail::npos;
            iAscii = -1;
            iHashed = false;
            iCache.reset();
        }
    private:
        std::string iText;
        mutable std::size_t iLength = string_detail::npos;
        mutable std::int8_t iAscii = -1;    ///< (-1: not found yet)
        mutable bool iHashed = false;
        mutable std::size_t iHash = 0u;
        mutable std::unique_ptr<position_cache> iCache;
    };

    // a fixed width: a code point per unit (std::uint8_t: UCS-1 (Latin-1), char16_t: UCS-2, char32_t: UCS-4)
    template <typename Unit, bool Mutable>
    class basic_fixed_string
    {
        static_assert(std::is_same_v<Unit, std::uint8_t> || std::is_same_v<Unit, char16_t> || std::is_same_v<Unit, char32_t>);
    public:
        using unit_type = Unit;
        static constexpr bool is_mutable = Mutable;
        static constexpr string_encoding encoding = sizeof(Unit) == 1u ? string_encoding::Ucs1 : sizeof(Unit) == 2u ? string_encoding::Ucs2 : string_encoding::Ucs4;
        static constexpr char32_t max_code_point = sizeof(Unit) == 1u ? 0xFFu : sizeof(Unit) == 2u ? 0xFFFFu : 0x10FFFFu;
    public:
        basic_fixed_string() = default;
        explicit basic_fixed_string(std::vector<Unit> aUnits) :
            iUnits{ std::move(aUnits) }
        {
        }
        basic_fixed_string(basic_fixed_string const& aOther) :
            iUnits{ aOther.iUnits }, iAscii{ aOther.iAscii }, iHashed{ aOther.iHashed }, iHash{ aOther.iHash }
        {
        }
        basic_fixed_string(basic_fixed_string&&) noexcept = default;
        basic_fixed_string& operator=(basic_fixed_string const& aOther)
        {
            if (&aOther != this)
                *this = basic_fixed_string{ aOther };
            return *this;
        }
        basic_fixed_string& operator=(basic_fixed_string&&) noexcept = default;
    public:
        std::size_t len() const
        {
            return iUnits.size();
        }
        bool is_ascii() const
        {
            if (iAscii < 0)
                iAscii = std::all_of(iUnits.begin(), iUnits.end(), [](Unit u) { return static_cast<char32_t>(u) < 0x80u; }) ? 1 : 0;
            return iAscii > 0;
        }
        char32_t at(std::size_t aIndex) const
        {
            return iUnits[aIndex];
        }
        std::size_t ascii_len() const
        {
            return iUnits.size();
        }
        char32_t ascii_at(std::size_t aIndex) const
        {
            return iUnits[aIndex];
        }
        std::vector<Unit> const& units() const
        {
            return iUnits;
        }
        std::string const& utf8() const
        {
            if (!iUtf8)
            {
                auto text = std::make_unique<std::string>();
                text->reserve(iUnits.size());
                for (auto const u : iUnits)
                    string_detail::encode(u, *text);
                iUtf8 = std::move(text);
            }
            return *iUtf8;
        }
        std::optional<std::size_t> cached_hash() const
        {
            return iHashed ? std::optional<std::size_t>{ iHash } : std::nullopt;
        }
        void cache_hash(std::size_t aHash) const
        {
            iHash = aHash;
            iHashed = true;
        }
    public:
        // (its units, to change (its caches are discarded))
        std::vector<Unit>& edit() requires Mutable
        {
            reset();
            return iUnits;
        }
        // (its units, moved out (it is then empty))
        std::vector<Unit> take()
        {
            reset();
            return std::move(iUnits);
        }
    private:
        void reset()
        {
            iUtf8.reset();
            iAscii = -1;
            iHashed = false;
        }
    private:
        std::vector<Unit> iUnits;
        mutable std::unique_ptr<std::string> iUtf8; ///< (its UTF-8: made when it is first needed)
        mutable std::int8_t iAscii = -1;            ///< (-1: not found yet)
        mutable bool iHashed = false;
        mutable std::size_t iHash = 0u;
    };

    // --- a string

    class string
    {
    public:
        using immutable_utf8 = basic_utf8_string<false>;
        using immutable_ucs1 = basic_fixed_string<std::uint8_t, false>;
        using immutable_ucs2 = basic_fixed_string<char16_t, false>;
        using immutable_ucs4 = basic_fixed_string<char32_t, false>;
        using mutable_utf8 = basic_utf8_string<true>;
        using mutable_ucs1 = basic_fixed_string<std::uint8_t, true>;
        using mutable_ucs2 = basic_fixed_string<char16_t, true>;
        using mutable_ucs4 = basic_fixed_string<char32_t, true>;
        using variant_type = std::variant<immutable_utf8, immutable_ucs1, immutable_ucs2, immutable_ucs4, mutable_utf8, mutable_ucs1, mutable_ucs2, mutable_ucs4>;
        static constexpr std::size_t npos = string_detail::npos;
    public:
        // (immutable and UTF-8; aLength: its code points, if known)
        string() = default;
        explicit string(std::string aUtf8, std::size_t aLength = npos) :
            iRep{ std::in_place_type<immutable_utf8>, std::move(aUtf8), aLength }
        {
        }
        explicit string(std::string_view aUtf8) :
            string{ std::string{ aUtf8 } }
        {
        }
        explicit string(char const* aUtf8) :
            string{ std::string{ aUtf8 } }
        {
        }
        explicit string(variant_type aRep) :
            iRep{ std::move(aRep) }
        {
        }
        // (mutable and UTF-8)
        static string mutable_utf8_string(std::string aUtf8)
        {
            return string{ variant_type{ std::in_place_type<mutable_utf8>, std::move(aUtf8) } };
        }
        // (code points, in the narrowest fixed width that holds them)
        static string from_code_points(std::u32string_view aCodePoints, bool aMutable = false)
        {
            return fixed(std::vector<char32_t>(aCodePoints.begin(), aCodePoints.end()), aMutable);
        }
        // (UTF-8's code points, in the narrowest fixed width that holds them)
        static string fixed_width(std::string_view aUtf8, bool aMutable = false)
        {
            std::vector<char32_t> codePoints;
            for (std::size_t offset = 0u; offset < aUtf8.size();)
                codePoints.push_back(string_detail::decode(aUtf8, offset));
            return fixed(std::move(codePoints), aMutable);
        }
    public:
        variant_type const& rep() const
        {
            return iRep;
        }
        string_encoding encoding() const
        {
            return std::visit([](auto const& aRep) { return std::decay_t<decltype(aRep)>::encoding; }, iRep);
        }
        bool is_mutable() const
        {
            return std::visit([](auto const& aRep) { return std::decay_t<decltype(aRep)>::is_mutable; }, iRep);
        }
        // (immutable UTF-8, the commonest alternative, first: without std::visit)
        std::size_t len() const
        {
            if (auto const* utf8 = std::get_if<immutable_utf8>(&iRep))
                return utf8->len();
            return std::visit([](auto const& aRep) { return aRep.len(); }, iRep);
        }
        bool empty() const
        {
            return ascii_len() == 0u;
        }
        bool is_ascii() const
        {
            return std::visit([](auto const& aRep) { return aRep.is_ascii(); }, iRep);
        }
        // (0 <= aIndex < len())
        char32_t at(std::size_t aIndex) const
        {
            if (auto const* utf8 = std::get_if<immutable_utf8>(&iRep))
                return utf8->at(aIndex);
            return std::visit([&](auto const& aRep) { return aRep.at(aIndex); }, iRep);
        }
        char32_t operator[](std::size_t aIndex) const
        {
            return at(aIndex);
        }
        std::size_t ascii_len() const
        {
            if (auto const* utf8 = std::get_if<immutable_utf8>(&iRep))
                return utf8->ascii_len();
            return std::visit([](auto const& aRep) { return aRep.ascii_len(); }, iRep);
        }
        // (0 <= aIndex < ascii_len())
        char32_t ascii_at(std::size_t aIndex) const
        {
            if (auto const* utf8 = std::get_if<immutable_utf8>(&iRep))
                return utf8->ascii_at(aIndex);
            return std::visit([&](auto const& aRep) { return aRep.ascii_at(aIndex); }, iRep);
        }
        // (valid while the string lives, if it is immutable; until it is changed, if it is mutable)
        std::string const& utf8() const
        {
            if (auto const* utf8 = std::get_if<immutable_utf8>(&iRep))
                return utf8->utf8();
            return std::visit([](auto const& aRep) -> std::string const& { return aRep.utf8(); }, iRep);
        }
        std::u32string code_points() const
        {
            std::u32string result;
            for_each_code_point([&](char32_t c) { result.push_back(c); });
            return result;
        }
        std::size_t hash() const
        {
            if (auto const cached = std::visit([](auto const& aRep) { return aRep.cached_hash(); }, iRep))
                return *cached;
            auto h = string_detail::HashBasis;
            for_each_code_point([&](char32_t c) { h = string_detail::hash_step(h, c); });
            std::visit([&](auto const& aRep) { aRep.cache_hash(static_cast<std::size_t>(h)); }, iRep);
            return static_cast<std::size_t>(h);
        }
        // (by code points: < 0, 0 or > 0)
        int compare(string const& aOther) const
        {
            if (&aOther == this)
                return 0;
            // (UTF-8's byte order is its code points')
            bool const lhsUtf8 = encoding() == string_encoding::Utf8;
            bool const rhsUtf8 = aOther.encoding() == string_encoding::Utf8;
            if (lhsUtf8 && rhsUtf8)
            {
                auto const order = utf8().compare(aOther.utf8());
                return order < 0 ? -1 : order > 0 ? 1 : 0;
            }
            auto const lhs = code_points();
            auto const rhs = aOther.code_points();
            auto const order = lhs.compare(rhs);
            return order < 0 ? -1 : order > 0 ? 1 : 0;
        }
        friend bool operator==(string const& aLhs, string const& aRhs)
        {
            return aLhs.compare(aRhs) == 0;
        }
        friend std::strong_ordering operator<=>(string const& aLhs, string const& aRhs)
        {
            return aLhs.compare(aRhs) <=> 0;
        }
    public:
        // --- a mutable string (std::logic_error if it is immutable; std::out_of_range for an index that is not in it)
        void set(std::size_t aIndex, char32_t aCodePoint)
        {
            check_index(aIndex, false);
            widen(aCodePoint);
            std::visit([&](auto& aRep)
            {
                using rep_type = std::decay_t<decltype(aRep)>;
                if constexpr (rep_type::is_mutable)
                {
                    if constexpr (rep_type::encoding == string_encoding::Utf8)
                    {
                        auto const offset = aRep.offset_of(aIndex);
                        auto end = offset;
                        string_detail::decode(aRep.utf8(), end);
                        aRep.edit().replace(offset, end - offset, utf8_of(aCodePoint));
                    }
                    else
                        aRep.edit()[aIndex] = static_cast<typename rep_type::unit_type>(aCodePoint);
                }
            }, iRep);
        }
        void insert(std::size_t aIndex, std::u32string_view aCodePoints)
        {
            check_index(aIndex, true);
            if (aCodePoints.empty())
                return;
            widen(*std::max_element(aCodePoints.begin(), aCodePoints.end()));
            std::visit([&](auto& aRep)
            {
                using rep_type = std::decay_t<decltype(aRep)>;
                if constexpr (rep_type::is_mutable)
                {
                    if constexpr (rep_type::encoding == string_encoding::Utf8)
                    {
                        std::string text;
                        for (auto const c : aCodePoints)
                            string_detail::encode(c, text);
                        auto const offset = aRep.offset_of(aIndex);
                        aRep.edit().insert(offset, text);
                    }
                    else
                    {
                        // (widened above: each code point fits in a unit)
                        auto& units = aRep.edit();
                        auto const narrowedCodePoints = string_detail::narrowed<typename rep_type::unit_type>(aCodePoints.begin(), aCodePoints.end());
                        units.insert(units.begin() + static_cast<std::ptrdiff_t>(aIndex), narrowedCodePoints.begin(), narrowedCodePoints.end());
                    }
                }
            }, iRep);
        }
        void insert(std::size_t aIndex, string const& aOther)
        {
            check_index(aIndex, true);
            if (encoding() == string_encoding::Utf8 && aOther.encoding() == string_encoding::Utf8)
            {
                // (UTF-8 into UTF-8: its bytes)
                std::string const text{ aOther.utf8() };
                auto& rep = std::get<mutable_utf8>(iRep);
                auto const offset = rep.offset_of(aIndex);
                rep.edit().insert(offset, text);
                return;
            }
            insert(aIndex, aOther.code_points());
        }
        void append(char32_t aCodePoint)
        {
            insert(len(), std::u32string_view{ &aCodePoint, 1u });
        }
        void append(std::u32string_view aCodePoints)
        {
            insert(len(), aCodePoints);
        }
        void append(string const& aOther)
        {
            insert(len(), aOther);
        }
        // (aCount: as many as there are, at most)
        void erase(std::size_t aIndex, std::size_t aCount = 1u)
        {
            check_index(aIndex, true);
            auto const end = aIndex + std::min(aCount, len() - aIndex);
            std::visit([&](auto& aRep)
            {
                using rep_type = std::decay_t<decltype(aRep)>;
                if constexpr (rep_type::is_mutable)
                {
                    if constexpr (rep_type::encoding == string_encoding::Utf8)
                    {
                        auto const first = aRep.offset_of(aIndex);
                        auto const last = aRep.offset_of(end);
                        aRep.edit().erase(first, last - first);
                    }
                    else
                    {
                        auto& units = aRep.edit();
                        units.erase(units.begin() + static_cast<std::ptrdiff_t>(aIndex), units.begin() + static_cast<std::ptrdiff_t>(end));
                    }
                }
            }, iRep);
        }
        void clear()
        {
            check_mutable();
            std::visit([](auto& aRep)
            {
                if constexpr (std::decay_t<decltype(aRep)>::is_mutable)
                    aRep.edit().clear();
            }, iRep);
        }
        // (it becomes immutable: a fixed width one at the narrowest width that holds it)
        void freeze()
        {
            switch (iRep.index())
            {
            case 4u:
                {
                    auto text = std::get<mutable_utf8>(iRep).take();
                    iRep.emplace<immutable_utf8>(std::move(text));
                }
                break;
            case 5u:
            case 6u:
            case 7u:
                {
                    auto codePoints = code_points();
                    *this = fixed(std::vector<char32_t>(codePoints.begin(), codePoints.end()), false);
                }
                break;
            default:
                break;
            }
        }
        // (a mutable copy (of the same encoding))
        string mutable_copy() const
        {
            return std::visit([](auto const& aRep) -> string
            {
                using rep_type = std::decay_t<decltype(aRep)>;
                if constexpr (rep_type::encoding == string_encoding::Utf8)
                    return string{ variant_type{ std::in_place_type<mutable_utf8>, aRep.utf8() } };
                else if constexpr (rep_type::encoding == string_encoding::Ucs1)
                    return string{ variant_type{ std::in_place_type<mutable_ucs1>, aRep.units() } };
                else if constexpr (rep_type::encoding == string_encoding::Ucs2)
                    return string{ variant_type{ std::in_place_type<mutable_ucs2>, aRep.units() } };
                else
                    return string{ variant_type{ std::in_place_type<mutable_ucs4>, aRep.units() } };
            }, iRep);
        }
    private:
        static string fixed(std::vector<char32_t>&& aCodePoints, bool aMutable)
        {
            return std::visit([&](auto&& aUnits) -> string
            {
                using units_type = std::decay_t<decltype(aUnits)>;
                using unit_type = typename units_type::value_type;
                if (aMutable)
                    return string{ variant_type{ std::in_place_type<basic_fixed_string<unit_type, true>>, std::move(aUnits) } };
                return string{ variant_type{ std::in_place_type<basic_fixed_string<unit_type, false>>, std::move(aUnits) } };
            }, string_detail::narrowest(std::move(aCodePoints)));
        }
        template <typename Visitor>
        void for_each_code_point(Visitor&& aVisitor) const
        {
            std::visit([&](auto const& aRep)
            {
                using rep_type = std::decay_t<decltype(aRep)>;
                if constexpr (rep_type::encoding == string_encoding::Utf8)
                {
                    std::string_view const text{ aRep.utf8() };
                    for (std::size_t offset = 0u; offset < text.size();)
                        aVisitor(string_detail::decode(text, offset));
                }
                else
                    for (auto const u : aRep.units())
                        aVisitor(static_cast<char32_t>(u));
            }, iRep);
        }
        void check_mutable() const
        {
            if (!is_mutable())
                throw std::logic_error("neos::runtime::string: immutable");
        }
        // (aIndex is in it (aEnd: or is its end))
        void check_index(std::size_t aIndex, bool aEnd) const
        {
            check_mutable();
            if (aEnd ? aIndex > len() : aIndex >= len())
                throw std::out_of_range("neos::runtime::string: index out of range");
        }
        // (a mutable fixed width string that cannot hold aCodePoint becomes the narrowest that can)
        void widen(char32_t aCodePoint)
        {
            auto const widened = [&](auto const& aUnits)
            {
                if (aCodePoint <= 0xFFFFu)
                    iRep = variant_type{ std::in_place_type<mutable_ucs2>, std::vector<char16_t>(aUnits.begin(), aUnits.end()) };
                else
                    iRep = variant_type{ std::in_place_type<mutable_ucs4>, std::vector<char32_t>(aUnits.begin(), aUnits.end()) };
            };
            if (auto const* ucs1 = std::get_if<mutable_ucs1>(&iRep); ucs1 != nullptr && aCodePoint > mutable_ucs1::max_code_point)
                widened(ucs1->units());
            else if (auto const* ucs2 = std::get_if<mutable_ucs2>(&iRep); ucs2 != nullptr && aCodePoint > mutable_ucs2::max_code_point)
                widened(ucs2->units());
        }
    private:
        variant_type iRep;
    };
}

template <>
struct std::hash<neos::runtime::string>
{
    std::size_t operator()(neos::runtime::string const& aString) const
    {
        return aString.hash();
    }
};
