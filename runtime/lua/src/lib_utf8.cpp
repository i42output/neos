/*
  lib_utf8.cpp

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

// The utf8 library

#include "lua.hpp"
#include "api.hpp"

namespace neos::lua_runtime
{
    namespace
    {
        constexpr std::uint32_t MaxUnicode = 0x10FFFFu;
        constexpr std::uint32_t MaxUtf = 0x7FFFFFFFu;

        void append_utf8(std::string& aResult, std::uint32_t aCodePoint)
        {
            // (code points up to 2^31: up to six bytes)
            if (aCodePoint < 0x80u)
            {
                aResult.push_back(static_cast<char>(aCodePoint));
                return;
            }
            std::size_t length = aCodePoint < 0x800u ? 2u : aCodePoint < 0x10000u ? 3u : aCodePoint < 0x200000u ? 4u : aCodePoint < 0x4000000u ? 5u : 6u;
            static unsigned char const leads[] = { 0u, 0u, 0xC0u, 0xE0u, 0xF0u, 0xF8u, 0xFCu };
            char bytes[6];
            for (std::size_t k = length; k-- > 1u;)
            {
                bytes[k] = static_cast<char>(0x80u | (aCodePoint & 0x3Fu));
                aCodePoint >>= 6;
            }
            bytes[0] = static_cast<char>(leads[length] | aCodePoint);
            aResult.append(bytes, length);
        }

        // (the character at a position: its code point and length (0: invalid))
        std::size_t decode(std::string_view aText, std::size_t aPosition, std::uint32_t& aCodePoint, bool aStrict)
        {
            auto const lead = static_cast<unsigned char>(aText[aPosition]);
            std::size_t length;
            std::uint32_t code;
            if (lead < 0x80u) { length = 1u; code = lead; }
            else if ((lead & 0xE0u) == 0xC0u) { length = 2u; code = lead & 0x1Fu; }
            else if ((lead & 0xF0u) == 0xE0u) { length = 3u; code = lead & 0x0Fu; }
            else if ((lead & 0xF8u) == 0xF0u) { length = 4u; code = lead & 0x07u; }
            else if ((lead & 0xFCu) == 0xF8u) { length = 5u; code = lead & 0x03u; }
            else if ((lead & 0xFEu) == 0xFCu) { length = 6u; code = lead & 0x01u; }
            else
                return 0u;
            if (aPosition + length > aText.size())
                return 0u;
            for (std::size_t k = 1u; k < length; ++k)
            {
                auto const b = static_cast<unsigned char>(aText[aPosition + k]);
                if ((b & 0xC0u) != 0x80u)
                    return 0u;
                code = (code << 6) | (b & 0x3Fu);
            }
            // (the shortest encoding only; strict: Unicode's code points, not surrogates)
            static std::uint32_t const minimum[] = { 0u, 0u, 0x80u, 0x800u, 0x10000u, 0x200000u, 0x4000000u };
            if (code < minimum[length] || code > MaxUtf)
                return 0u;
            if (aStrict && (code > MaxUnicode || (code >= 0xD800u && code <= 0xDFFFu)))
                return 0u;
            aCodePoint = code;
            return length;
        }

        std::int64_t relative_position(std::int64_t aPosition, std::size_t aLength)
        {
            if (aPosition >= 0)
                return aPosition;
            if (static_cast<std::size_t>(-aPosition) > aLength)
                return 0;
            return static_cast<std::int64_t>(aLength) + aPosition + 1;
        }

        bool is_continuation(std::string_view aText, std::int64_t aPosition)
        {
            return aPosition >= 0 && static_cast<std::size_t>(aPosition) < aText.size() && (static_cast<unsigned char>(aText[static_cast<std::size_t>(aPosition)]) & 0xC0u) == 0x80u;
        }

        void utf8_char(native_call& c)
        {
            std::string result;
            for (std::size_t i = 0u; i < c.count(); ++i)
            {
                auto const code = check_integer(c, i);
                if (code < 0 || static_cast<std::uint64_t>(code) > MaxUtf)
                    argument_error(c, i, "value out of range");
                append_utf8(result, static_cast<std::uint32_t>(code));
            }
            c.ret(make_string(std::move(result)));
        }

        void utf8_codepoint(native_call& c)
        {
            auto const s = check_string(c, 0u);
            auto const i = relative_position(optional_integer(c, 1u, 1), s.size());
            auto const j = relative_position(optional_integer(c, 2u, i), s.size());
            bool const lax = truthy(c.arg(3u));
            if (i < 1)
                argument_error(c, 1u, "out of bounds");
            if (j > static_cast<std::int64_t>(s.size()))
                argument_error(c, 2u, "out of bounds");
            if (i > j)
                return;
            auto position = static_cast<std::size_t>(i - 1);
            while (position < static_cast<std::size_t>(j))
            {
                std::uint32_t code = 0u;
                auto const n = decode(s, position, code, !lax);
                if (n == 0u)
                    runtime_error("invalid UTF-8 code");
                c.ret(make_integer(code));
                position += n;
            }
        }

        void utf8_len(native_call& c)
        {
            auto const s = check_string(c, 0u);
            auto i = relative_position(optional_integer(c, 1u, 1), s.size());
            auto j = relative_position(optional_integer(c, 2u, -1), s.size());
            bool const lax = truthy(c.arg(3u));
            if (i < 1 || i > static_cast<std::int64_t>(s.size()) + 1)
                argument_error(c, 1u, "initial position out of bounds");
            if (j > static_cast<std::int64_t>(s.size()))
                argument_error(c, 2u, "final position out of bounds");
            std::int64_t count = 0;
            auto position = static_cast<std::size_t>(i - 1);
            while (static_cast<std::int64_t>(position) < j)
            {
                std::uint32_t code = 0u;
                auto const n = decode(s, position, code, !lax);
                if (n == 0u)
                {
                    c.ret(Nil);
                    c.ret(make_integer(static_cast<std::int64_t>(position) + 1));
                    return;
                }
                position += n;
                ++count;
            }
            c.ret(make_integer(count));
        }

        void utf8_offset(native_call& c)
        {
            auto const s = check_string(c, 0u);
            auto n = check_integer(c, 1u);
            auto const length = s.size();
            auto i = relative_position(optional_integer(c, 2u, n >= 0 ? 1 : static_cast<std::int64_t>(length) + 1), length);
            if (i < 1 || i > static_cast<std::int64_t>(length) + 1)
                argument_error(c, 2u, "position out of bounds");
            auto position = i - 1;
            if (n == 0)
            {
                // (the start of the character that contains byte i)
                while (position > 0 && is_continuation(s, position))
                    --position;
                c.ret(make_integer(position + 1));
                return;
            }
            if (is_continuation(s, position))
                runtime_error("initial position is a continuation byte");
            if (n < 0)
            {
                while (n < 0 && position > 0)
                {
                    do
                        --position;
                    while (position > 0 && is_continuation(s, position));
                    ++n;
                }
            }
            else
            {
                --n;
                while (n > 0 && position < static_cast<std::int64_t>(length))
                {
                    do
                        ++position;
                    while (is_continuation(s, position));
                    --n;
                }
            }
            if (n == 0)
                c.ret(make_integer(position + 1));
            else
                c.ret(Nil);
        }

        void utf8_codes_iterator(native_call& c, bool aLax)
        {
            auto const s = check_string(c, 0u);
            auto position = static_cast<std::size_t>(check_integer(c, 1u));
            // (the character after the one at the position)
            if (position > 0u)
            {
                ++position;
                while (position <= s.size() && is_continuation(s, static_cast<std::int64_t>(position) - 1))
                    ++position;
            }
            else
                position = 1u;
            if (position > s.size())
                return;
            std::uint32_t code = 0u;
            auto const n = decode(s, position - 1u, code, !aLax);
            if (n == 0u || is_continuation(s, static_cast<std::int64_t>(position - 1u + n)))
                runtime_error("invalid UTF-8 code");
            c.ret(make_integer(static_cast<std::int64_t>(position)));
            c.ret(make_integer(code));
        }

        void utf8_codes_strict(native_call& c) { utf8_codes_iterator(c, false); }
        void utf8_codes_lax(native_call& c) { utf8_codes_iterator(c, true); }

        void utf8_codes(native_call& c)
        {
            auto const s = check_string_value(c, 0u);
            if (!str(s).empty() && is_continuation(str(s), 0))
                argument_error(c, 0u, "invalid UTF-8 code");
            static static_values tIterators;
            if (tIterators.empty())
            {
                tIterators.push_back(make_native(&utf8_codes_strict, "codes_iterator"));
                tIterators.push_back(make_native(&utf8_codes_lax, "codes_iterator"));
            }
            c.ret(tIterators[truthy(c.arg(1u)) ? 1u : 0u]);
            c.ret(s);
            c.ret(make_integer(0));
        }
    }

    void open_utf8()
    {
        auto const t = library_table("utf8");
        value_list keep{ t };
        register_library(t, "utf8", {
            { "char", &utf8_char },
            { "codepoint", &utf8_codepoint },
            { "len", &utf8_len },
            { "offset", &utf8_offset },
            { "codes", &utf8_codes } });
        raw_set(t, intern("charpattern"), make_string(std::string_view{ "[\0-\x7F\xC2-\xFD][\x80-\xBF]*", 14u }));
    }
}
