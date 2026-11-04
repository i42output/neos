/*
  exprs.cpp

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

// C: expressions (6.5): their types and value categories, the constraints on their operands, literals (constants and string 
// literals, 6.4.4, 6.4.5), and constant expressions (6.6)

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include "program.hpp"

namespace neos::compilers::c
{
    namespace
    {
        // (a code point as UTF-8)
        void append_utf8(std::string& aOut, std::uint32_t aCode)
        {
            if (aCode < 0x80u)
                aOut += static_cast<char>(aCode);
            else if (aCode < 0x800u)
            {
                aOut += static_cast<char>(0xC0u | (aCode >> 6u));
                aOut += static_cast<char>(0x80u | (aCode & 0x3Fu));
            }
            else if (aCode < 0x10000u)
            {
                aOut += static_cast<char>(0xE0u | (aCode >> 12u));
                aOut += static_cast<char>(0x80u | ((aCode >> 6u) & 0x3Fu));
                aOut += static_cast<char>(0x80u | (aCode & 0x3Fu));
            }
            else
            {
                aOut += static_cast<char>(0xF0u | (aCode >> 18u));
                aOut += static_cast<char>(0x80u | ((aCode >> 12u) & 0x3Fu));
                aOut += static_cast<char>(0x80u | ((aCode >> 6u) & 0x3Fu));
                aOut += static_cast<char>(0x80u | (aCode & 0x3Fu));
            }
        }

        enum class encoding { Plain, Utf8, Utf16, Utf32, Wide };

        encoding encoding_of(std::string_view aLiteral, std::string_view& aBody)
        {
            encoding e = encoding::Plain;
            if (aLiteral.starts_with("u8"))
                e = encoding::Utf8, aLiteral.remove_prefix(2u);
            else if (aLiteral.starts_with("u"))
                e = encoding::Utf16, aLiteral.remove_prefix(1u);
            else if (aLiteral.starts_with("U"))
                e = encoding::Utf32, aLiteral.remove_prefix(1u);
            else if (aLiteral.starts_with("L"))
                e = encoding::Wide, aLiteral.remove_prefix(1u);
            aBody = aLiteral.substr(1u, aLiteral.size() - 2u);
            return e;
        }
    }

    // --- literals

    void program::literal(node* aLiteral)
    {
        switch (aLiteral->kind)
        {
        case construct::Number:
        {
            std::string text;
            for (char c : aLiteral->source)
                if (c != '\'')
                    text += c;
            bool const hex = text.size() > 1u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
            bool const isFloat = text.find('.') != std::string::npos || (!hex && text.find_first_of("eE") != std::string::npos) ||
                (hex && text.find_first_of("pP") != std::string::npos);
            if (isFloat)
            {
                // (a floating constant: its suffix decides its type (f: float; l: long double (as double)))
                std::string digits = text;
                char suffix = 0;
                while (!digits.empty() && std::isalpha(static_cast<unsigned char>(digits.back())) && !(hex ? false : false))
                {
                    char const c = static_cast<char>(std::tolower(static_cast<unsigned char>(digits.back())));
                    if (c != 'f' && c != 'l' && c != 'd')
                        break;
                    if (hex && c == 'f' && digits.find_first_of("pP") == std::string::npos)
                        break;
                    suffix = suffix == 0 ? c : suffix;
                    digits.pop_back();
                    if (c == 'd')
                        error(aLiteral, "decimal floating constants are not supported");
                }
                char* end = nullptr;
                constant v;
                v.k = constant::kind::Floating;
                if (suffix == 'f')
                {
                    v.floating = static_cast<double>(std::strtof(digits.c_str(), &end));
                    aLiteral->type = qtype{ basic(type_kind::Float) };
                }
                else
                {
                    v.floating = std::strtod(digits.c_str(), &end);
                    aLiteral->type = qtype{ basic(suffix == 'l' ? type_kind::LongDouble : type_kind::Double) };
                }
                if (end == nullptr || *end != '\0')
                    error(aLiteral, "invalid floating constant '" + std::string{ aLiteral->source } + "'");
                aLiteral->value = v;
                return;
            }
            int base = 10;
            std::size_t start = 0u;
            if (hex)
                base = 16, start = 2u;
            else if (text.size() > 1u && text[0] == '0' && (text[1] == 'b' || text[1] == 'B'))
                base = 2, start = 2u;
            else if (text.size() > 1u && text[0] == '0')
                base = 8, start = 1u;
            std::uint64_t value = 0u;
            bool overflow = false;
            std::size_t i = start;
            for (; i < text.size(); ++i)
            {
                char const c = text[i];
                int digit = -1;
                if (c >= '0' && c <= '9') digit = c - '0';
                else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                if (digit < 0 || digit >= base)
                {
                    if (digit >= 0 && digit < 10 && base == 8)
                        error(aLiteral, "invalid digit '" + std::string(1u, c) + "' in octal constant");
                    break;
                }
                if (value > (std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(digit)) / static_cast<std::uint64_t>(base))
                    overflow = true;
                value = value * static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(digit);
            }
            if (i == start && base != 8)
                error(aLiteral, "invalid integer constant '" + std::string{ aLiteral->source } + "'");
            std::string suffix = text.substr(i);
            std::string lower = suffix;
            for (auto& c : lower)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            bool isUnsigned = false;
            int longs = 0;
            bool bitPrecise = false;
            for (std::size_t j = 0u; j < lower.size();)
            {
                if (lower[j] == 'u' && !isUnsigned)
                    isUnsigned = true, ++j;
                else if (lower.compare(j, 2u, "ll") == 0 && longs == 0 && (suffix[j] == suffix[j + 1u]))
                    longs = 2, j += 2u;
                else if (lower[j] == 'l' && longs == 0)
                    longs = 1, ++j;
                else if (lower.compare(j, 2u, "wb") == 0 && !bitPrecise && longs == 0 && (suffix[j] == 'w') == (suffix[j + 1u] == 'b'))
                    bitPrecise = true, j += 2u;
                else
                    error(aLiteral, "invalid suffix \"" + suffix + "\" on integer constant");
            }
            if (overflow)
                error(aLiteral, "integer constant is too large for its type");
            constant v;
            v.k = constant::kind::Integer;
            v.integer = value;
            if (bitPrecise)
            {
                // (wb: _BitInt(N), N the least width that represents the value (a sign bit included); uwb: unsigned _BitInt(N))
                std::uint32_t width = 1u;
                while (width < 64u && (value >> width) != 0u)
                    ++width;
                if (!isUnsigned)
                    ++width;
                width = std::max(width, isUnsigned ? 1u : 2u);
                if (width > 64u)
                    error(aLiteral, "integer constant is too large for _BitInt(BITINT_MAXWIDTH)");
                aLiteral->type = qtype{ bit_int(width, !isUnsigned) };
                aLiteral->value = v;
                return;
            }
            // (the first type of the list (6.4.4.1) that represents the value)
            bool const decimal = base == 10;
            type_kind k;
            auto const fitsInt = value <= 0x7FFFFFFFu;
            auto const fitsUInt = value <= 0xFFFFFFFFu;
            auto const fitsLong = value <= 0x7FFFFFFFFFFFFFFFull;
            if (isUnsigned)
                k = longs == 0 && fitsUInt ? type_kind::UInt : longs == 2 ? type_kind::ULongLong : type_kind::ULong;
            else if (longs == 0 && fitsInt)
                k = type_kind::Int;
            else if (longs == 0 && !decimal && fitsUInt)
                k = type_kind::UInt;
            else if (fitsLong)
                k = longs == 2 ? type_kind::LongLong : type_kind::Long;
            else if (!decimal)
                k = longs == 2 ? type_kind::ULongLong : type_kind::ULong;
            else
            {
                warning(aLiteral, "integer constant is so large that it is unsigned");
                k = type_kind::ULongLong;
            }
            aLiteral->type = qtype{ basic(k) };
            aLiteral->value = v;
            return;
        }
        case construct::Character:
        {
            std::string_view body;
            auto const e = encoding_of(aLiteral->source, body);
            // (the code points (a plain or u8 character constant: bytes))
            std::vector<std::uint32_t> values;
            for (std::size_t i = 0u; i < body.size();)
            {
                std::uint32_t c = static_cast<unsigned char>(body[i++]);
                bool escaped = false;
                if (c == '\\' && i < body.size())
                {
                    escaped = true;
                    char const x = body[i++];
                    switch (x)
                    {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case 'a': c = '\a'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'v': c = '\v'; break;
                    case 'e': c = 0x1Bu; break;
                    case '\\': c = '\\'; break;
                    case '\'': c = '\''; break;
                    case '"': c = '"'; break;
                    case '?': c = '?'; break;
                    case 'x':
                    {
                        c = 0u;
                        std::size_t n = 0u;
                        while (i < body.size() && std::isxdigit(static_cast<unsigned char>(body[i])))
                        {
                            auto const d = body[i++];
                            c = c * 16u + static_cast<std::uint32_t>(std::isdigit(static_cast<unsigned char>(d)) ? d - '0' : std::tolower(d) - 'a' + 10);
                            ++n;
                        }
                        if (n == 0u)
                            error(aLiteral, "\\x used with no following hex digits");
                        break;
                    }
                    case 'u':
                    case 'U':
                    {
                        std::size_t const n = x == 'u' ? 4u : 8u;
                        c = 0u;
                        for (std::size_t k = 0u; k < n; ++k)
                        {
                            if (i >= body.size() || !std::isxdigit(static_cast<unsigned char>(body[i])))
                                error(aLiteral, "incomplete universal character name");
                            auto const d = body[i++];
                            c = c * 16u + static_cast<std::uint32_t>(std::isdigit(static_cast<unsigned char>(d)) ? d - '0' : std::tolower(d) - 'a' + 10);
                        }
                        escaped = false;    // (a code point, encoded as the literal's encoding)
                        break;
                    }
                    default:
                        if (x >= '0' && x <= '7')
                        {
                            c = static_cast<std::uint32_t>(x - '0');
                            for (int d = 0; d < 2 && i < body.size() && body[i] >= '0' && body[i] <= '7'; ++d)
                                c = c * 8u + static_cast<std::uint32_t>(body[i++] - '0');
                        }
                        else
                        {
                            warning(aLiteral, std::string{ "unknown escape sequence '\\" } + x + "'");
                            c = static_cast<unsigned char>(x);
                        }
                        break;
                    }
                }
                else if (c >= 0x80u)
                {
                    // (a UTF-8 sequence: its code point)
                    int extra = c >= 0xF0u ? 3 : c >= 0xE0u ? 2 : 1;
                    c &= (0x3Fu >> extra);
                    while (extra-- > 0 && i < body.size())
                        c = (c << 6u) | (static_cast<unsigned char>(body[i++]) & 0x3Fu);
                    escaped = false;
                }
                if ((e == encoding::Plain || e == encoding::Utf8) && !escaped && c >= 0x80u)
                {
                    std::string utf8;
                    append_utf8(utf8, c);
                    for (char b : utf8)
                        values.push_back(static_cast<unsigned char>(b));
                }
                else
                    values.push_back(c);
            }
            if (values.empty())
                error(aLiteral, "empty character constant");
            constant v;
            v.k = constant::kind::Integer;
            switch (e)
            {
            case encoding::Plain:
            {
                // (an int: a single character's value as a char's (signed); more than one: implementation-defined (as GCC's))
                if (values.size() == 1u)
                    v.integer = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int8_t>(values[0] & 0xFFu)));
                else
                {
                    warning(aLiteral, "multi-character character constant");
                    std::uint32_t r = 0u;
                    for (auto x : values)
                        r = (r << 8u) | (x & 0xFFu);
                    v.integer = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(r)));
                }
                aLiteral->type = int_type();
                break;
            }
            case encoding::Utf8:
                if (values.size() != 1u || values[0] > 0xFFu)
                    error(aLiteral, "character not encodable in a single code unit");
                v.integer = values[0];
                aLiteral->type = qtype{ basic(type_kind::UChar) };
                break;
            case encoding::Utf16:
                if (values.size() != 1u || values[0] > 0xFFFFu)
                    error(aLiteral, "character not encodable in a single code unit");
                v.integer = values[0];
                aLiteral->type = qtype{ basic(type_kind::UShort) };
                break;
            case encoding::Utf32:
                v.integer = values.back();
                aLiteral->type = qtype{ basic(type_kind::UInt) };
                break;
            case encoding::Wide:
                v.integer = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(values.back())));
                aLiteral->type = int_type();
                break;
            }
            aLiteral->value = v;
            return;
        }
        default:
            return;
        }
    }

    void program::string_literal(node* aStrings)
    {
        // (adjacent string literals concatenated: their encoding is the prefixed ones' (all the same))
        encoding e = encoding::Plain;
        for (auto* s : aStrings->children)
        {
            std::string_view body;
            auto const se = encoding_of(s->source, body);
            if (se != encoding::Plain)
            {
                if (e != encoding::Plain && e != se)
                    error(s, "concatenation of string literals with different encoding prefixes");
                e = se;
            }
        }
        // (the code points)
        std::vector<std::uint32_t> units;
        std::vector<bool> raw;      ///< (a unit that an octal or hexadecimal escape gave: as it is, not encoded)
        for (auto* s : aStrings->children)
        {
            std::string_view body;
            encoding_of(s->source, body);
            for (std::size_t i = 0u; i < body.size();)
            {
                std::uint32_t c = static_cast<unsigned char>(body[i++]);
                bool isRaw = false;
                if (c == '\\' && i < body.size())
                {
                    char const x = body[i++];
                    switch (x)
                    {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case 'a': c = '\a'; break;
                    case 'b': c = '\b'; break;
                    case 'f': c = '\f'; break;
                    case 'v': c = '\v'; break;
                    case 'e': c = 0x1Bu; break;
                    case '\\': c = '\\'; break;
                    case '\'': c = '\''; break;
                    case '"': c = '"'; break;
                    case '?': c = '?'; break;
                    case 'x':
                    {
                        c = 0u;
                        while (i < body.size() && std::isxdigit(static_cast<unsigned char>(body[i])))
                        {
                            auto const d = body[i++];
                            c = c * 16u + static_cast<std::uint32_t>(std::isdigit(static_cast<unsigned char>(d)) ? d - '0' : std::tolower(d) - 'a' + 10);
                        }
                        isRaw = true;
                        break;
                    }
                    case 'u':
                    case 'U':
                    {
                        std::size_t const n = x == 'u' ? 4u : 8u;
                        c = 0u;
                        for (std::size_t k = 0u; k < n && i < body.size(); ++k)
                        {
                            auto const d = body[i++];
                            c = c * 16u + static_cast<std::uint32_t>(std::isdigit(static_cast<unsigned char>(d)) ? d - '0' : std::tolower(d) - 'a' + 10);
                        }
                        break;
                    }
                    default:
                        if (x >= '0' && x <= '7')
                        {
                            c = static_cast<std::uint32_t>(x - '0');
                            for (int d = 0; d < 2 && i < body.size() && body[i] >= '0' && body[i] <= '7'; ++d)
                                c = c * 8u + static_cast<std::uint32_t>(body[i++] - '0');
                            isRaw = true;
                        }
                        else
                            c = static_cast<unsigned char>(x);
                        break;
                    }
                }
                else if (c >= 0x80u)
                {
                    int extra = c >= 0xF0u ? 3 : c >= 0xE0u ? 2 : 1;
                    c &= (0x3Fu >> extra);
                    while (extra-- > 0 && i < body.size())
                        c = (c << 6u) | (static_cast<unsigned char>(body[i++]) & 0x3Fu);
                }
                units.push_back(c);
                raw.push_back(isRaw);
            }
        }
        // (the array's bytes, in its element type's representation (little endian))
        std::string bytes;
        qtype element;
        std::uint64_t count = 0u;
        switch (e)
        {
        case encoding::Plain:
        case encoding::Utf8:
            for (std::size_t i = 0u; i < units.size(); ++i)
            {
                if (raw[i] || units[i] < 0x80u)
                    bytes += static_cast<char>(units[i] & 0xFFu);
                else
                    append_utf8(bytes, units[i]);
            }
            bytes += '\0';
            count = bytes.size();
            element = qtype{ basic(e == encoding::Utf8 ? type_kind::UChar : type_kind::Char) };
            break;
        case encoding::Utf16:
            for (std::size_t i = 0u; i < units.size(); ++i)
            {
                auto const put = [&](std::uint32_t aUnit) { bytes += static_cast<char>(aUnit & 0xFFu); bytes += static_cast<char>((aUnit >> 8u) & 0xFFu); };
                if (!raw[i] && units[i] >= 0x10000u)
                {
                    auto const v = units[i] - 0x10000u;
                    put(0xD800u + (v >> 10u));
                    put(0xDC00u + (v & 0x3FFu));
                }
                else
                    put(units[i]);
            }
            bytes += std::string(2u, '\0');
            count = bytes.size() / 2u;
            element = qtype{ basic(type_kind::UShort) };
            break;
        case encoding::Utf32:
        case encoding::Wide:
            for (auto u : units)
                for (int b = 0; b < 4; ++b)
                    bytes += static_cast<char>((u >> (8 * b)) & 0xFFu);
            bytes += std::string(4u, '\0');
            count = bytes.size() / 4u;
            element = qtype{ basic(e == encoding::Utf32 ? type_kind::UInt : type_kind::Int) };
            break;
        }
        aStrings->bytes = bytes;
        aStrings->type = qtype{ array_of(element, count) };
        aStrings->lvalue = true;
    }

    // --- expressions

    qtype program::value_type(node const* aExpression) const
    {
        auto const t = aExpression->type;
        if (!t)
            return t;
        if (t->is_array())
            return qtype{ const_cast<program*>(this)->pointer_to(t->base) };
        if (t->is_function())
            return qtype{ const_cast<program*>(this)->pointer_to(t) };
        return t.unqualified();
    }

    bool program::null_pointer_constant(node const* aExpression) const
    {
        // (an integer constant expression with the value 0, such an expression cast to void*, or nullptr)
        auto const* e = aExpression;
        while (e->is(construct::Paren))
            e = e->child(0);
        if (e->type && e->type->kind == type_kind::NullPtr)
            return true;
        if (e->is(construct::Cast) && e->type && e->type->is_pointer() && e->type->base->is_void() && e->type->base.qualifiers == 0u)
            return null_pointer_constant(e->child(1));
        if (!e->type || !e->type->is_integer())
            return false;
        auto const v = const_cast<program*>(this)->evaluate(const_cast<node*>(e), true);
        return v && v->is_integer() && v->integer == 0u;
    }

    void program::mark_address_taken(node* aExpression)
    {
        auto* e = aExpression;
        while (e != nullptr && (e->is(construct::Paren) || e->is(construct::Member) || e->is(construct::Subscript)))
        {
            if (e->is(construct::Subscript))
            {
                // (an array object's element: the array's address)
                auto* base = e->child(0);
                if (!(base->type && base->type->is_array()))
                    return;
                e = base;
                continue;
            }
            e = e->child(0);
        }
        if (e != nullptr && (e->is(construct::Identifier) || e->is(construct::CompoundLiteral)) && e->ent != nullptr && e->ent->kind == entity_kind::Object)
            e->ent->addressTaken = true;
        if (e != nullptr && e->is(construct::Generic) && e->selected != nullptr)
            mark_address_taken(e->selected);
    }

    builtin_kind program::builtin_kind_of(std::string const& aName)
    {
        static std::map<std::string, builtin_kind> const sBuiltins =
        {
            { "__builtin_va_start", builtin_kind::VaStart }, { "__builtin_va_next", builtin_kind::VaNext }, { "__builtin_setjmp", builtin_kind::Setjmp },
            { "__builtin_longjmp", builtin_kind::Longjmp }, { "__builtin_unreachable", builtin_kind::Unreachable }, { "__builtin_trap", builtin_kind::Trap },
            { "__builtin_abort", builtin_kind::Trap },
            { "__builtin_neos_exit", builtin_kind::Exit }, { "__builtin_clz", builtin_kind::Clz }, { "__builtin_ctz", builtin_kind::Ctz },
            { "__builtin_popcount", builtin_kind::Popcount }, { "__builtin_clzll", builtin_kind::Clzll }, { "__builtin_ctzll", builtin_kind::Ctzll },
            { "__builtin_popcountll", builtin_kind::Popcountll }, { "__builtin_clzl", builtin_kind::Clzll }, { "__builtin_ctzl", builtin_kind::Ctzll },
            { "__builtin_popcountl", builtin_kind::Popcountll }, { "__builtin_bswap16", builtin_kind::Bswap16 }, { "__builtin_bswap32", builtin_kind::Bswap32 },
            { "__builtin_bswap64", builtin_kind::Bswap64 }, { "__builtin_expect", builtin_kind::Expect }, { "__builtin_alloca", builtin_kind::Alloca },
            { "__builtin_ckd_add", builtin_kind::CkdAdd }, { "__builtin_ckd_sub", builtin_kind::CkdSub }, { "__builtin_ckd_mul", builtin_kind::CkdMul },
            { "__builtin_constant_p", builtin_kind::ConstantP }, { "__builtin_sqrt", builtin_kind::Sqrt }, { "__builtin_sqrtf", builtin_kind::Sqrtf },
            { "__builtin_fabs", builtin_kind::Fabs }, { "__builtin_fabsf", builtin_kind::Fabsf }, { "__builtin_floor", builtin_kind::Floor },
            { "__builtin_ceil", builtin_kind::Ceil }, { "__builtin_trunc", builtin_kind::Trunc }, { "__builtin_nearbyint", builtin_kind::Nearbyint },
            { "__builtin_copysign", builtin_kind::Copysign }, { "__builtin_fmin", builtin_kind::Fmin }, { "__builtin_fmax", builtin_kind::Fmax },
            { "__builtin_atomic_exchange", builtin_kind::AtomicExchange }, { "__builtin_atomic_compare_exchange", builtin_kind::AtomicCompareExchange },
            { "__builtin_atomic_fetch_add", builtin_kind::AtomicFetchAdd }, { "__builtin_atomic_fetch_sub", builtin_kind::AtomicFetchSub },
            { "__builtin_atomic_fetch_or", builtin_kind::AtomicFetchOr }, { "__builtin_atomic_fetch_xor", builtin_kind::AtomicFetchXor },
            { "__builtin_atomic_fetch_and", builtin_kind::AtomicFetchAnd }, { "__builtin_atomic_load", builtin_kind::AtomicLoad },
            { "__builtin_atomic_store", builtin_kind::AtomicStore }, { "__builtin_atomic_fence", builtin_kind::AtomicFence }
        };
        auto const found = sBuiltins.find(aName);
        return found != sBuiltins.end() ? found->second : builtin_kind::None;
    }

    entity* program::builtin(std::string const& aName)
    {
        (void)aName;
        return nullptr;
    }

    void program::check_assignable(qtype aTarget, node* aValue, node const* aWhere, char const* aContext)
    {
        // (simple assignment's constraints (6.5.17.2), for assignment, initialization, arguments and return)
        auto const target = aTarget.unqualified();
        auto const source = value_type(aValue);
        auto const* t = target.type;
        auto const* s = source.type;
        if (t == nullptr || s == nullptr)
            return;
        std::string const context = aContext;
        if (t->is_arithmetic() && s->is_arithmetic())
            return;
        if (t->kind == type_kind::Bool && (s->is_pointer() || s->kind == type_kind::NullPtr))
            return;
        if (t->is_record() && s->is_record())
        {
            if (!compatible(target, source, true))
                error(aWhere, "incompatible types in " + context + " ('" + type_name(aTarget) + "' from '" + type_name(source) + "')");
            return;
        }
        if (t->kind == type_kind::NullPtr)
        {
            if (s->kind != type_kind::NullPtr && !null_pointer_constant(aValue))
                error(aWhere, "incompatible types in " + context + " ('nullptr_t' from '" + type_name(source) + "')");
            return;
        }
        if (t->is_pointer())
        {
            if (s->kind == type_kind::NullPtr || null_pointer_constant(aValue))
                return;
            if (s->is_pointer())
            {
                auto const tb = t->base;
                auto const sb = s->base;
                if ((sb.qualifiers & ~tb.qualifiers & (QConst | QVolatile | QRestrict | QAtomic)) != 0u)
                    warning(aWhere, context + " discards '" + type_name(qtype{ basic(type_kind::Void), static_cast<std::uint8_t>(sb.qualifiers & ~tb.qualifiers) }).substr(0u,
                        type_name(qtype{ basic(type_kind::Void), static_cast<std::uint8_t>(sb.qualifiers & ~tb.qualifiers) }).size() - 5u) + "' qualifier from pointer target type");
                if (tb->is_void() || sb->is_void())
                    return;
                if (!compatible(tb.unqualified(), sb.unqualified(), true))
                {
                    // (char pointers of different signedness, and the like: as GCC, a warning)
                    warning(aWhere, "incompatible pointer types in " + context + " ('" + type_name(aTarget) + "' from '" + type_name(source) + "')");
                }
                return;
            }
            if (s->is_integer())
            {
                warning(aWhere, context + " makes pointer from integer without a cast");
                return;
            }
            error(aWhere, "incompatible types in " + context + " ('" + type_name(aTarget) + "' from '" + type_name(source) + "')");
        }
        if (t->is_integer() && s->is_pointer())
        {
            warning(aWhere, context + " makes integer from pointer without a cast");
            return;
        }
        if (t->is_void())
            error(aWhere, "void value not ignored as it ought to be");
        error(aWhere, "incompatible types in " + context + " ('" + type_name(aTarget) + "' from '" + type_name(source) + "')");
    }

    void program::condition(node* aExpression)
    {
        expression(aExpression);
        auto const t = value_type(aExpression);
        if (!t->is_scalar())
            error(aExpression, "used '" + type_name(t) + "' where a scalar is required");
    }

    void program::expression(node* aExpression)
    {
        auto* e = aExpression;
        auto const operand = [&](std::size_t i) -> node* { auto* c = e->child(i); if (c == nullptr) error(e, "missing operand"); return c; };
        auto const integer_operand = [&](node* aOperand, char const* aWhat)
        {
            if (!value_type(aOperand)->is_integer())
                error(aOperand, std::string{ "invalid operand to " } + aWhat + " (have '" + type_name(value_type(aOperand)) + "')");
        };
        auto const arithmetic_operand = [&](node* aOperand, char const* aWhat)
        {
            if (!value_type(aOperand)->is_arithmetic())
                error(aOperand, std::string{ "invalid operand to " } + aWhat + " (have '" + type_name(value_type(aOperand)) + "')");
        };
        auto const modifiable = [&](node* aTarget, char const* aWhat)
        {
            if (!aTarget->lvalue)
                error(aTarget, std::string{ "lvalue required as " } + aWhat);
            auto const t = aTarget->type;
            if (t.is_const())
                error(aTarget, std::string{ aWhat } + " of read-only location");
            if (t->is_array())
                error(aTarget, std::string{ aWhat } + " to an expression with array type");
            if (t->is_record() && t->record != nullptr)
            {
                std::function<bool(record_info const*)> constMember = [&](record_info const* r) -> bool
                {
                    for (auto const& m : r->members)
                        if (m.type.is_const() || (m.type->is_record() && m.type->record != nullptr && constMember(m.type->record)))
                            return true;
                    return false;
                };
                if (constMember(t->record))
                    error(aTarget, std::string{ aWhat } + " of read-only members");
            }
            if (!t->is_complete())
                error(aTarget, std::string{ aWhat } + " to an object of incomplete type");
        };
        switch (e->kind)
        {
        case construct::Identifier:
        {
            auto const name = name_of(e);
            auto* ent = lookup(name);
            if (ent == nullptr)
            {
                if (name.starts_with("__builtin_"))
                {
                    // (__builtin_ and the name of a function of the C library: that function (GNU); else a builtin function, whose 
                    // call is analysed by the call)
                    if (auto const library = iExternals.find(name.substr(10u)); library != iExternals.end() && library->second->kind == entity_kind::Function)
                        ent = library->second;
                    else
                    {
                        e->type = qtype{ function_of(int_type(), {}, true, false) };
                        return;
                    }
                }
                else
                    error(e, "'" + name + "' undeclared");
            }
            if (ent->kind == entity_kind::Typedef)
                error(e, "unexpected type name '" + name + "'");
            e->ent = ent;
            ent->used = true;
            if (ent->kind == entity_kind::Function && ent->name == "longjmp" && iUnit != nullptr && !iUnit->prelude)
                iUsesLongjmp = true;
            if (ent->kind == entity_kind::EnumConstant)
            {
                constant v;
                v.k = constant::kind::Integer;
                v.integer = static_cast<std::uint64_t>(ent->value);
                e->value = v;
                e->type = ent->type;
                return;
            }
            e->type = ent->type;
            e->lvalue = ent->kind == entity_kind::Object;
            return;
        }
        case construct::Number:
        case construct::Character:
            literal(e);
            return;
        case construct::True:
        case construct::False:
        {
            constant v;
            v.k = constant::kind::Integer;
            v.integer = e->is(construct::True) ? 1u : 0u;
            e->value = v;
            e->type = qtype{ basic(type_kind::Bool) };
            return;
        }
        case construct::Nullptr:
        {
            constant v;
            v.k = constant::kind::Integer;
            v.integer = 0u;
            e->value = v;
            e->type = qtype{ basic(type_kind::NullPtr) };
            return;
        }
        case construct::Strings:
            string_literal(e);
            return;
        case construct::Paren:
        {
            auto* inner = operand(0);
            expression(inner);
            e->type = inner->type;
            e->lvalue = inner->lvalue;
            e->bitField = inner->bitField;
            e->value = inner->value;
            e->ent = inner->ent;
            e->members = inner->members;
            return;
        }
        case construct::Generic:
        {
            auto* controlling = operand(0);
            expression(controlling);
            auto const t = value_type(controlling);
            node* selected = nullptr;
            node* fallback = nullptr;
            for (std::size_t i = 1u; i < e->size(); ++i)
            {
                auto* a = e->child(i);
                if (a->is(construct::GenericDefault))
                {
                    if (fallback != nullptr)
                        error(a, "duplicate 'default' in generic selection");
                    fallback = a->child(0);
                    continue;
                }
                auto const at = type_name_type(a->child(0));
                if (compatible(at.unqualified(), t, true) && at.qualifiers == 0u)
                {
                    if (selected != nullptr)
                        error(a, "more than one type in a generic selection is compatible with the controlling expression's");
                    selected = a->child(1);
                }
            }
            if (selected == nullptr)
                selected = fallback;
            if (selected == nullptr)
                error(e, "generic selection has no association compatible with '" + type_name(t) + "'");
            expression(selected);
            e->selected = selected;
            e->type = selected->type;
            e->lvalue = selected->lvalue;
            e->value = selected->value;
            e->ent = selected->ent;
            e->bitField = selected->bitField;
            e->members = selected->members;
            return;
        }
        case construct::CompoundLiteral:
        {
            // (an unnamed object: static at file scope, else automatic (one of the function's locals))
            node* typeName = nullptr;
            node* init = nullptr;
            bool isStatic = file_scope() || iFunction == nullptr;
            bool isConstexpr = false;
            for (auto* c : e->children)
            {
                if (c->is(construct::TypeName))
                    typeName = c;
                else if (c->is(construct::Braced))
                    init = c;
                else if (c->is(construct::Storage))
                {
                    auto const k = c->source;
                    if (k == "static" || k.starts_with("thread_local") || k.starts_with("_Thread_local"))
                        isStatic = true;
                    else if (k == "constexpr")
                        isConstexpr = true;
                }
            }
            auto type = type_name_type(typeName);
            type = initializer_type(type, init);
            if (!type->is_complete())
                error(e, "compound literal has incomplete type");
            if (type->is_variably_modified())
                error(e, "compound literal has variable size");
            auto& ent = iEntities.emplace_back();
            ent.kind = entity_kind::Object;
            ent.name = "(compound literal)";
            ent.type = isConstexpr ? type.with(QConst) : type;
            ent.decl = e;
            ent.unit = iUnit;
            ent.initializer = init;
            ent.defined = true;
            ent.isConstexpr = isConstexpr;
            if (isStatic)
            {
                ent.where = object_storage::Static;
                ent.storage = storage_class::Static;
                iObjects.push_back(&ent);
            }
            else if (iFunction != nullptr)
                iFunction->function->locals.push_back(&ent);
            check_initializer(type, init, isStatic);
            e->ent = &ent;
            e->type = ent.type;
            e->lvalue = true;
            return;
        }
        case construct::Subscript:
        {
            auto* a = operand(0);
            auto* b = operand(1);
            expression(a);
            expression(b);
            auto const ta = value_type(a);
            auto const tb = value_type(b);
            qtype pointer;
            if (ta->is_pointer() && tb->is_integer())
                pointer = ta;
            else if (tb->is_pointer() && ta->is_integer())
                pointer = tb;
            else
                error(e, "subscripted value is neither array nor pointer");
            if (!pointer->base->is_complete() && !pointer->base->is_void())
                error(e, "subscript of pointer to incomplete type '" + type_name(pointer->base) + "'");
            e->type = pointer->base;
            e->lvalue = true;
            return;
        }
        case construct::Call:
        {
            auto* callee = operand(0);
            if (callee->is(construct::Identifier) && lookup(name_of(callee)) == nullptr && name_of(callee).starts_with("__builtin_") &&
                (builtin_kind_of(name_of(callee)) != builtin_kind::None || iExternals.find(name_of(callee).substr(10u)) == iExternals.end()))
            {
                // (a builtin function)
                auto const name = name_of(callee);
                auto const kind = builtin_kind_of(name);
                if (kind == builtin_kind::None)
                    error(callee, "unknown builtin function '" + name + "'");
                e->builtin = static_cast<std::uint32_t>(kind);
                for (std::size_t i = 1u; i < e->size(); ++i)
                    expression(e->child(i));
                auto const args = e->size() - 1u;
                auto const want = [&](std::size_t n)
                {
                    if (args != n)
                        error(e, "wrong number of arguments to '" + name + "'");
                };
                switch (kind)
                {
                case builtin_kind::VaStart:
                    want(0u);
                    if (iFunction == nullptr || !iFunction->type->variadic)
                        error(e, "'va_start' used in function with fixed arguments");
                    e->type = qtype{ pointer_to(qtype{ basic(type_kind::Char) }) };
                    break;
                case builtin_kind::VaNext:
                    want(2u);
                    e->type = qtype{ pointer_to(qtype{ basic(type_kind::Void) }) };
                    break;
                case builtin_kind::Setjmp:
                    want(1u);
                    if (iFunction != nullptr)
                        iFunction->function->usesSetjmp = true;
                    e->type = int_type();
                    break;
                case builtin_kind::Longjmp:
                    want(2u);
                    if (iUnit != nullptr && !iUnit->prelude)
                        iUsesLongjmp = true;
                    e->type = qtype{ basic(type_kind::Void) };
                    break;
                case builtin_kind::Unreachable:
                case builtin_kind::Trap:
                    want(0u);
                    e->type = qtype{ basic(type_kind::Void) };
                    break;
                case builtin_kind::Exit:
                    want(1u);
                    e->type = qtype{ basic(type_kind::Void) };
                    break;
                case builtin_kind::Clz:
                case builtin_kind::Ctz:
                case builtin_kind::Popcount:
                case builtin_kind::Clzll:
                case builtin_kind::Ctzll:
                case builtin_kind::Popcountll:
                    want(1u);
                    integer_operand(e->child(1), name.c_str());
                    e->type = int_type();
                    break;
                case builtin_kind::Bswap16:
                    want(1u);
                    e->type = qtype{ basic(type_kind::UShort) };
                    break;
                case builtin_kind::Bswap32:
                    want(1u);
                    e->type = qtype{ basic(type_kind::UInt) };
                    break;
                case builtin_kind::Bswap64:
                    want(1u);
                    e->type = qtype{ basic(type_kind::ULongLong) };
                    break;
                case builtin_kind::Expect:
                    want(2u);
                    e->type = qtype{ basic(type_kind::Long) };
                    break;
                case builtin_kind::Alloca:
                    want(1u);
                    if (iFunction != nullptr)
                        iFunction->function->usesAlloca = true;
                    e->type = qtype{ pointer_to(qtype{ basic(type_kind::Void) }) };
                    break;
                case builtin_kind::CkdAdd:
                case builtin_kind::CkdSub:
                case builtin_kind::CkdMul:
                {
                    want(3u);
                    auto const r = value_type(e->child(1));
                    if (!r->is_pointer() || !r->base->is_integer() || r->base->kind == type_kind::Bool || r->base->kind == type_kind::Enum || r->base.is_const())
                        error(e->child(1), "the result of a checked integer operation must be a pointer to a modifiable integer object");
                    for (std::size_t i = 2u; i <= 3u; ++i)
                        if (!value_type(e->child(i))->is_integer() || value_type(e->child(i))->kind == type_kind::Bool)
                            error(e->child(i), "the operands of a checked integer operation must be integers");
                    e->type = qtype{ basic(type_kind::Bool) };
                    break;
                }
                case builtin_kind::ConstantP:
                    want(1u);
                    e->type = int_type();
                    break;
                case builtin_kind::Sqrt:
                case builtin_kind::Fabs:
                case builtin_kind::Floor:
                case builtin_kind::Ceil:
                case builtin_kind::Trunc:
                case builtin_kind::Nearbyint:
                    want(1u);
                    e->type = qtype{ basic(type_kind::Double) };
                    break;
                case builtin_kind::Sqrtf:
                case builtin_kind::Fabsf:
                    want(1u);
                    e->type = qtype{ basic(type_kind::Float) };
                    break;
                case builtin_kind::Copysign:
                case builtin_kind::Fmin:
                case builtin_kind::Fmax:
                    want(2u);
                    e->type = qtype{ basic(type_kind::Double) };
                    break;
                case builtin_kind::AtomicFence:
                    want(0u);
                    e->type = qtype{ basic(type_kind::Void) };
                    break;
                case builtin_kind::AtomicLoad:
                case builtin_kind::AtomicStore:
                {
                    want(kind == builtin_kind::AtomicLoad ? 1u : 2u);
                    auto const p = value_type(e->child(1));
                    if (!p->is_pointer() || !p->base->is_scalar())
                        error(e->child(1), "the object of an atomic operation must be a pointer to an atomic scalar object");
                    e->type = kind == builtin_kind::AtomicLoad ? p->base.unqualified() : qtype{ basic(type_kind::Void) };
                    if (kind == builtin_kind::AtomicStore)
                        check_assignable(p->base.unqualified(), e->child(2), e, "atomic store");
                    break;
                }
                case builtin_kind::AtomicExchange:
                case builtin_kind::AtomicCompareExchange:
                case builtin_kind::AtomicFetchAdd:
                case builtin_kind::AtomicFetchSub:
                case builtin_kind::AtomicFetchOr:
                case builtin_kind::AtomicFetchXor:
                case builtin_kind::AtomicFetchAnd:
                {
                    want(kind == builtin_kind::AtomicCompareExchange ? 3u : 2u);
                    auto const p = value_type(e->child(1));
                    if (!p->is_pointer() || !p->base->is_scalar())
                        error(e->child(1), "the object of an atomic operation must be a pointer to an atomic scalar object");
                    e->type = kind == builtin_kind::AtomicCompareExchange ? qtype{ basic(type_kind::Bool) } : p->base.unqualified();
                    break;
                }
                default:
                    break;
                }
                return;
            }
            expression(callee);
            auto const ct = value_type(callee);
            if (!ct->is_pointer() || !ct->base->is_function())
                error(e, "called object is not a function or function pointer");
            auto const* ft = ct->base.type;
            auto const args = e->size() - 1u;
            for (std::size_t i = 1u; i < e->size(); ++i)
                expression(e->child(i));
            if (ft->prototype)
            {
                if (args < ft->parameters.size() || (args > ft->parameters.size() && !ft->variadic))
                {
                    std::string name = callee->ent != nullptr ? "'" + callee->ent->name + "'" : "function";
                    error(e, std::string{ args < ft->parameters.size() ? "too few" : "too many" } + " arguments to " + name);
                }
                for (std::size_t i = 0u; i < ft->parameters.size(); ++i)
                    check_assignable(ft->parameters[i].type, e->child(i + 1u), e->child(i + 1u), "argument passing");
            }
            for (std::size_t i = ft->prototype ? ft->parameters.size() : 0u; i < args; ++i)
            {
                auto const t = value_type(e->child(i + 1u));
                if (t->is_void())
                    error(e->child(i + 1u), "invalid use of void expression");
            }
            if (!ft->base->is_void() && !ft->base->is_complete())
                error(e, "calling a function with incomplete return type '" + type_name(ft->base) + "'");
            e->type = ft->base.unqualified();
            return;
        }
        case construct::Member:
        case construct::Arrow:
        {
            auto* base = operand(0);
            auto* name = operand(1);
            expression(base);
            qtype recordType;
            if (e->is(construct::Arrow))
            {
                auto const t = value_type(base);
                if (!t->is_pointer() || !t->base->is_record())
                    error(e, "invalid type argument of '->' (have '" + type_name(t) + "')");
                recordType = t->base;
                e->lvalue = true;
            }
            else
            {
                if (!base->type->is_record())
                    error(e, "request for member '" + name_of(name) + "' in something not a structure or union");
                recordType = base->type;
                e->lvalue = base->lvalue;
            }
            if (!recordType->is_complete())
                error(e, "invalid use of incomplete type '" + type_name(recordType) + "'");
            std::vector<member_info const*> path;
            if (!recordType->record->find(name_of(name), path))
                error(name, "'" + type_name(recordType) + "' has no member named '" + name_of(name) + "'");
            e->members = path;
            auto const& m = *path.back();
            std::uint8_t q = recordType.qualifiers;
            for (auto const* p : path)
                q |= p->type.qualifiers;
            e->type = m.type.with(q);
            e->bitField = m.bitWidth.has_value();
            // (a bit-field narrower than int, of an integer type (a wider one's too, as GCC's): int (its values are int's; its promotion 
            // is int's))
            if (e->bitField && *m.bitWidth < width_of(basic(type_kind::Int)) && m.type->is_integer() && m.type->kind != type_kind::Bool)
                e->type = qtype{ basic(type_kind::Int) }.with(q);
            return;
        }
        case construct::PostIncrement:
        case construct::PostDecrement:
        case construct::PreIncrement:
        case construct::PreDecrement:
        {
            auto* target = operand(0);
            expression(target);
            char const* what = e->is(construct::PostIncrement) || e->is(construct::PreIncrement) ? "increment operand" : "decrement operand";
            modifiable(target, what);
            auto const t = value_type(target);
            if (!t->is_arithmetic() && !t->is_pointer())
                error(e, std::string{ "wrong type argument to " } + (e->is(construct::PostIncrement) || e->is(construct::PreIncrement) ? "increment" : "decrement"));
            if (t->is_pointer() && !t->base->is_complete() && !t->base->is_void())
                error(e, "arithmetic on pointer to an incomplete type");
            e->type = t;
            return;
        }
        case construct::Address:
        {
            auto* target = operand(0);
            expression(target);
            if (target->bitField)
                error(e, "cannot take address of bit-field");
            if (target->type->is_function())
            {
                e->type = qtype{ pointer_to(target->type) };
                return;
            }
            if (!target->lvalue)
                error(e, "lvalue required as unary '&' operand");
            if (target->ent != nullptr && target->ent->storage == storage_class::Register && target->is(construct::Identifier))
                error(e, "address of register variable '" + target->ent->name + "' requested");
            mark_address_taken(target);
            e->type = qtype{ pointer_to(target->type) };
            return;
        }
        case construct::Indirection:
        {
            auto* p = operand(0);
            expression(p);
            auto const t = value_type(p);
            if (!t->is_pointer())
                error(e, "invalid type argument of unary '*' (have '" + type_name(t) + "')");
            e->type = t->base;
            e->lvalue = !t->base->is_function();
            return;
        }
        case construct::Plus:
        case construct::Minus:
        {
            auto* a = operand(0);
            expression(a);
            arithmetic_operand(a, e->is(construct::Plus) ? "unary +" : "unary -");
            e->type = promote(value_type(a));
            return;
        }
        case construct::Complement:
        {
            auto* a = operand(0);
            expression(a);
            integer_operand(a, "unary ~");
            e->type = promote(value_type(a));
            return;
        }
        case construct::Not:
        {
            auto* a = operand(0);
            expression(a);
            if (!value_type(a)->is_scalar())
                error(e, "invalid operand to unary !");
            e->type = int_type();
            return;
        }
        case construct::LabelAddress:
        {
            // (GNU: a label's address: a void* that a computed goto in its function goes to (its value: see function_definition))
            if (iFunction == nullptr)
                error(e, "a label's address outside a function");
            e->type = qtype{ pointer_to(qtype{ basic(type_kind::Void) }) };
            iLabelAddresses.push_back(e);
            return;
        }
        case construct::Sizeof:
        case construct::SizeofType:
        case construct::Alignof:
        {
            qtype t;
            auto* a = operand(0);
            if (a->is(construct::TypeName))
                t = type_name_type(a);
            else
            {
                expression(a);
                if (a->bitField)
                    error(e, "'sizeof' applied to a bit-field");
                t = a->type;
            }
            if (t->is_function())
                error(e, std::string{ "invalid application of '" } + (e->is(construct::Alignof) ? "alignof" : "sizeof") + "' to a function type");
            if (!t->is_complete() && !t->is_void())
                error(e, std::string{ "invalid application of '" } + (e->is(construct::Alignof) ? "alignof" : "sizeof") + "' to incomplete type '" + type_name(t) + "'");
            e->type = size_type();
            if (!t->is_variably_modified() || e->is(construct::Alignof))
            {
                constant v;
                v.k = constant::kind::Integer;
                v.integer = e->is(construct::Alignof) ? align_of(t) : size_of(t);
                // (GNU: alignof of an object: its alignment (an alignment specifier's))
                auto* x = a;
                while (x->is(construct::Paren))
                    x = x->child(0);
                if (e->is(construct::Alignof) && x->is(construct::Identifier) && x->ent != nullptr && 
                    x->ent->kind == entity_kind::Object && x->ent->self()->value > 0)
                    v.integer = std::max<std::uint64_t>(v.integer, static_cast<std::uint64_t>(x->ent->self()->value));
                e->value = v;
            }
            return;
        }
        case construct::Cast:
        {
            auto* typeName = operand(0);
            auto* a = operand(1);
            auto const t = type_name_type(typeName);
            expression(a);
            auto const s = value_type(a);
            if (t->is_void())
            {
                e->type = t.unqualified();
                return;
            }
            if (!t->is_scalar())
            {
                if (t->is_record() && compatible(t.unqualified(), s, true))
                {
                    // (GCC: a cast to the operand's own structure type)
                    e->type = t.unqualified();
                    return;
                }
                error(e, "conversion to non-scalar type requested");
            }
            if (!s->is_scalar())
                error(e, "aggregate value used where a scalar was expected");
            if ((t->is_pointer() && s->is_floating()) || (t->is_floating() && s->is_pointer()))
                error(e, "invalid cast between a pointer and a floating type");
            if (t->kind == type_kind::NullPtr && s->kind != type_kind::NullPtr && !null_pointer_constant(a))
                error(e, "invalid cast to nullptr_t");
            if (s->kind == type_kind::NullPtr && !t->is_pointer() && t->kind != type_kind::NullPtr && t->kind != type_kind::Bool)
                error(e, "invalid cast from nullptr_t");
            e->type = t.unqualified();
            return;
        }
        case construct::Multiply:
        case construct::Divide:
        case construct::Modulo:
        {
            auto* a = operand(0);
            auto* b = operand(1);
            expression(a);
            expression(b);
            char const* what = e->is(construct::Multiply) ? "binary *" : e->is(construct::Divide) ? "binary /" : "binary %";
            if (e->is(construct::Modulo))
            {
                integer_operand(a, what);
                integer_operand(b, what);
            }
            else
            {
                arithmetic_operand(a, what);
                arithmetic_operand(b, what);
            }
            e->type = usual_arithmetic(value_type(a), value_type(b));
            return;
        }
        case construct::Add:
        case construct::Subtract:
        {
            auto* a = operand(0);
            auto* b = operand(1);
            expression(a);
            expression(b);
            auto const ta = value_type(a);
            auto const tb = value_type(b);
            auto const complete_pointer = [&](qtype p)
            {
                if (!p->base->is_complete() && !p->base->is_void())
                    error(e, "arithmetic on pointer to an incomplete type");
                if (p->base->is_function())
                    error(e, "arithmetic on pointer to a function");
            };
            if (ta->is_arithmetic() && tb->is_arithmetic())
                e->type = usual_arithmetic(ta, tb);
            else if (ta->is_pointer() && tb->is_integer())
            {
                complete_pointer(ta);
                e->type = ta;
            }
            else if (e->is(construct::Add) && ta->is_integer() && tb->is_pointer())
            {
                complete_pointer(tb);
                e->type = tb;
            }
            else if (e->is(construct::Subtract) && ta->is_pointer() && tb->is_pointer())
            {
                complete_pointer(ta);
                if (!compatible(ta->base.unqualified(), tb->base.unqualified(), true))
                    error(e, "invalid operands to binary - (have '" + type_name(ta) + "' and '" + type_name(tb) + "')");
                e->type = ptrdiff_type();
            }
            else
                error(e, std::string{ "invalid operands to binary " } + (e->is(construct::Add) ? "+" : "-") + " (have '" + type_name(ta) + "' and '" + type_name(tb) + "')");
            return;
        }
        case construct::ShiftLeft:
        case construct::ShiftRight:
        {
            auto* a = operand(0);
            auto* b = operand(1);
            expression(a);
            expression(b);
            integer_operand(a, e->is(construct::ShiftLeft) ? "<<" : ">>");
            integer_operand(b, e->is(construct::ShiftLeft) ? "<<" : ">>");
            e->type = promote(value_type(a));
            return;
        }
        case construct::Less:
        case construct::Greater:
        case construct::LessEqual:
        case construct::GreaterEqual:
        case construct::Equal:
        case construct::NotEqual:
        {
            auto* a = operand(0);
            auto* b = operand(1);
            expression(a);
            expression(b);
            auto const ta = value_type(a);
            auto const tb = value_type(b);
            bool const equality = e->is(construct::Equal) || e->is(construct::NotEqual);
            bool ok = (ta->is_arithmetic() && tb->is_arithmetic());
            if (!ok && ta->is_pointer() && tb->is_pointer())
            {
                ok = compatible(ta->base.unqualified(), tb->base.unqualified(), true) || (equality && (ta->base->is_void() || tb->base->is_void()));
                if (!ok)
                {
                    warning(e, "comparison of distinct pointer types lacks a cast");
                    ok = true;
                }
            }
            if (!ok && equality)
                ok = (ta->is_pointer() && (null_pointer_constant(b) || tb->kind == type_kind::NullPtr)) ||
                    (tb->is_pointer() && (null_pointer_constant(a) || ta->kind == type_kind::NullPtr)) ||
                    (ta->kind == type_kind::NullPtr && (tb->kind == type_kind::NullPtr || null_pointer_constant(b))) ||
                    (tb->kind == type_kind::NullPtr && null_pointer_constant(a));
            if (!ok && ((ta->is_pointer() && tb->is_integer()) || (ta->is_integer() && tb->is_pointer())))
            {
                warning(e, "comparison between pointer and integer");
                ok = true;
            }
            if (!ok)
                error(e, "invalid operands to comparison (have '" + type_name(ta) + "' and '" + type_name(tb) + "')");
            e->type = int_type();
            return;
        }
        case construct::BitAnd:
        case construct::BitXor:
        case construct::BitOr:
        {
            auto* a = operand(0);
            auto* b = operand(1);
            expression(a);
            expression(b);
            integer_operand(a, e->is(construct::BitAnd) ? "binary &" : e->is(construct::BitXor) ? "binary ^" : "binary |");
            integer_operand(b, e->is(construct::BitAnd) ? "binary &" : e->is(construct::BitXor) ? "binary ^" : "binary |");
            e->type = usual_arithmetic(value_type(a), value_type(b));
            return;
        }
        case construct::And:
        case construct::Or:
        {
            auto* a = operand(0);
            auto* b = operand(1);
            expression(a);
            expression(b);
            if (!value_type(a)->is_scalar() || !value_type(b)->is_scalar())
                error(e, std::string{ "invalid operands to " } + (e->is(construct::And) ? "&&" : "||"));
            e->type = int_type();
            return;
        }
        case construct::Conditional:
        {
            auto* c = operand(0);
            auto* a = operand(1);
            auto* b = operand(2);
            condition(c);
            expression(a);
            expression(b);
            auto const ta = value_type(a);
            auto const tb = value_type(b);
            if (ta->is_arithmetic() && tb->is_arithmetic())
                e->type = usual_arithmetic(ta, tb);
            else if (ta->is_record() && tb->is_record() && compatible(ta, tb, true))
                e->type = ta;
            else if (ta->is_void() || tb->is_void())
                e->type = qtype{ basic(type_kind::Void) };
            else if (ta->kind == type_kind::NullPtr && tb->kind == type_kind::NullPtr)
                e->type = ta;
            else if (ta->is_pointer() && (null_pointer_constant(b) || tb->kind == type_kind::NullPtr))
                e->type = ta;
            else if (tb->is_pointer() && (null_pointer_constant(a) || ta->kind == type_kind::NullPtr))
                e->type = tb;
            else if (ta->is_pointer() && tb->is_pointer())
            {
                auto const q = static_cast<std::uint8_t>(ta->base.qualifiers | tb->base.qualifiers);
                if (ta->base->is_void() || tb->base->is_void())
                    e->type = qtype{ pointer_to(qtype{ basic(type_kind::Void), q }) };
                else if (compatible(ta->base.unqualified(), tb->base.unqualified(), true))
                    e->type = qtype{ pointer_to(composite(ta->base.unqualified(), tb->base.unqualified()).with(q)) };
                else
                {
                    warning(e, "pointer type mismatch in conditional expression");
                    e->type = qtype{ pointer_to(qtype{ basic(type_kind::Void), q }) };
                }
            }
            else if ((ta->is_pointer() && tb->is_integer()) || (ta->is_integer() && tb->is_pointer()))
            {
                warning(e, "pointer/integer type mismatch in conditional expression");
                e->type = ta->is_pointer() ? ta : tb;
            }
            else
                error(e, "type mismatch in conditional expression");
            return;
        }
        case construct::Assign:
        case construct::AssignMultiply:
        case construct::AssignDivide:
        case construct::AssignModulo:
        case construct::AssignAdd:
        case construct::AssignSubtract:
        case construct::AssignShiftLeft:
        case construct::AssignShiftRight:
        case construct::AssignBitAnd:
        case construct::AssignBitXor:
        case construct::AssignBitOr:
        {
            auto* target = operand(0);
            auto* value = operand(1);
            expression(target);
            expression(value);
            modifiable(target, "left operand of assignment");
            auto const tt = target->type.unqualified();
            auto const tv = value_type(value);
            switch (e->kind)
            {
            case construct::Assign:
                check_assignable(target->type, value, e, "assignment");
                break;
            case construct::AssignAdd:
            case construct::AssignSubtract:
                if (tt->is_pointer())
                {
                    if (!tv->is_integer())
                        error(e, "invalid operands to compound assignment");
                }
                else if (!tt->is_arithmetic() || !tv->is_arithmetic())
                    error(e, "invalid operands to compound assignment");
                break;
            case construct::AssignModulo:
            case construct::AssignShiftLeft:
            case construct::AssignShiftRight:
            case construct::AssignBitAnd:
            case construct::AssignBitXor:
            case construct::AssignBitOr:
                if (!tt->is_integer() || !tv->is_integer())
                    error(e, "invalid operands to compound assignment");
                break;
            default:
                if (!tt->is_arithmetic() || !tv->is_arithmetic())
                    error(e, "invalid operands to compound assignment");
                break;
            }
            e->type = tt;
            return;
        }
        case construct::Comma:
        {
            auto* a = operand(0);
            auto* b = operand(1);
            expression(a);
            expression(b);
            e->type = value_type(b);
            return;
        }
        case construct::StatementExpression:
        {
            // (GNU: a compound statement whose value is its last statement's (an expression statement's), or void)
            if (iFunction == nullptr)
                error(e, "statement expressions are allowed only inside functions");
            statement(e->child(0));
            e->type = qtype{ basic(type_kind::Void) };
            if (auto* value = statement_expression_value(e); value != nullptr)
                e->type = value_type(value);
            return;
        }
        case construct::Braced:
            error(e, "expected expression before '{' token");
        default:
            error(e, "expected expression");
        }
    }

    // --- constant expressions

    std::int64_t program::integer_constant(node* aExpression, char const* aWhat)
    {
        if (!value_type(aExpression)->is_integer())
            error(aExpression, std::string{ aWhat } + " is not an integer constant expression");
        auto const v = evaluate(aExpression, true);
        if (!v || !v->is_integer())
            error(aExpression, std::string{ aWhat } + " is not an integer constant expression");
        return v->s();
    }

    std::optional<constant> program::evaluate(node* aExpression, bool aIntegerOnly)
    {
        auto* e = aExpression;
        auto const integer = [&](std::uint64_t aValue, qtype aType) -> constant
        {
            // (the value as its type's: truncated, then sign or zero extended)
            constant v;
            v.k = constant::kind::Integer;
            auto const* t = integer_type(aType.type);
            if (t->kind == type_kind::Bool)
                aValue = aValue != 0u ? 1u : 0u;
            else if (t->is_integer())
            {
                auto const w = width_of(t);
                if (w < 64u)
                {
                    auto const mask = (std::uint64_t{ 1 } << w) - 1u;
                    aValue &= mask;
                    if (t->is_signed() && (aValue >> (w - 1u)) != 0u)
                        aValue |= ~mask;
                }
            }
            v.integer = aValue;
            return v;
        };
        auto const floating = [&](double aValue, qtype aType) -> constant
        {
            constant v;
            v.k = constant::kind::Floating;
            v.floating = aType->kind == type_kind::Float ? static_cast<double>(static_cast<float>(aValue)) : aValue;
            return v;
        };
        auto const convert = [&](constant const& aValue, qtype aFrom, qtype aTo) -> std::optional<constant>
        {
            auto const* to = aTo.type;
            if (to->is_void())
                return aValue;
            if (to->kind == type_kind::Bool)
            {
                if (aValue.k == constant::kind::Floating)
                    return integer(aValue.floating != 0.0 ? 1u : 0u, aTo);
                if (aValue.k == constant::kind::Address)
                {
                    if (aIntegerOnly)
                        return std::nullopt;
                    return integer(1u, aTo);
                }
                return integer(aValue.integer != 0u ? 1u : 0u, aTo);
            }
            if (to->is_integer())
            {
                if (aValue.k == constant::kind::Floating)
                {
                    auto const f = aValue.floating;
                    if (std::isnan(f))
                        return integer(0u, aTo);
                    if (to->is_signed() || f < 0.0)
                        return integer(static_cast<std::uint64_t>(static_cast<std::int64_t>(f)), aTo);
                    return integer(f >= 18446744073709551616.0 ? ~std::uint64_t{} : static_cast<std::uint64_t>(f), aTo);
                }
                if (aValue.k == constant::kind::Address)
                {
                    // (an address constant as an integer: only an offset from null (offsetof's idiom) is an integer constant)
                    if (aValue.object == nullptr && aValue.literal == nullptr)
                        return integer(aValue.integer, aTo);
                    if (aIntegerOnly)
                        return std::nullopt;
                    if (size_of(aTo) < 8u)
                        return std::nullopt;
                    return aValue;
                }
                return integer(aValue.integer, aTo);
            }
            if (to->is_floating())
            {
                if (aValue.k == constant::kind::Floating)
                    return floating(aValue.floating, aTo);
                if (aValue.k == constant::kind::Address)
                    return std::nullopt;
                if (aFrom->is_signed() || aFrom->kind == type_kind::Bool)
                    return floating(static_cast<double>(static_cast<std::int64_t>(aValue.integer)), aTo);
                return floating(static_cast<double>(aValue.integer), aTo);
            }
            if (to->is_pointer() || to->kind == type_kind::NullPtr)
            {
                if (aValue.k == constant::kind::Floating)
                    return std::nullopt;
                if (aValue.k == constant::kind::Integer)
                {
                    constant v;
                    v.k = constant::kind::Address;
                    v.integer = aValue.integer;
                    if (aIntegerOnly)
                        return v;
                    return v;
                }
                return aValue;
            }
            return std::nullopt;
        };
        // (an address constant: the address of an object of static storage duration (or a function, or a string or compound literal))
        std::function<std::optional<constant>(node*)> address = [&](node* aLvalue) -> std::optional<constant>
        {
            auto* l = aLvalue;
            while (l->is(construct::Paren))
                l = l->child(0);
            if (l->is(construct::Identifier))
            {
                if (l->ent == nullptr)
                    return std::nullopt;
                auto* ent = l->ent->self();
                if (ent->kind == entity_kind::Function || (ent->kind == entity_kind::Object && ent->where == object_storage::Static))
                {
                    constant v;
                    v.k = constant::kind::Address;
                    v.object = ent;
                    return v;
                }
                return std::nullopt;
            }
            if (l->is(construct::Strings))
            {
                constant v;
                v.k = constant::kind::Address;
                v.literal = l;
                return v;
            }
            if (l->is(construct::CompoundLiteral))
            {
                if (l->ent == nullptr || l->ent->where != object_storage::Static)
                    return std::nullopt;
                constant v;
                v.k = constant::kind::Address;
                v.object = l->ent;
                return v;
            }
            if (l->is(construct::Generic) && l->selected != nullptr)
                return address(l->selected);
            if (l->is(construct::Indirection))
                return evaluate(l->child(0), false);
            if (l->is(construct::Member) || l->is(construct::Arrow))
            {
                std::optional<constant> base;
                if (l->is(construct::Arrow))
                    base = evaluate(l->child(0), false);
                else
                    base = address(l->child(0));
                if (!base || base->k != constant::kind::Address || l->members.empty())
                    return std::nullopt;
                for (auto const* m : l->members)
                    base->integer += m->offset;
                return base;
            }
            if (l->is(construct::Subscript))
            {
                auto* a = l->child(0);
                auto* b = l->child(1);
                if (!value_type(a)->is_pointer())
                    std::swap(a, b);
                auto base = evaluate(a, false);
                auto index = evaluate(b, true);
                if (!base || !index || base->k != constant::kind::Address || !index->is_integer())
                    return std::nullopt;
                base->integer += static_cast<std::uint64_t>(index->s() * static_cast<std::int64_t>(size_of(value_type(a)->base)));
                return base;
            }
            return std::nullopt;
        };
        if (e->value && (e->is(construct::Number) || e->is(construct::Character) || e->is(construct::True) || e->is(construct::False) ||
            e->is(construct::Nullptr) || e->is(construct::Sizeof) || e->is(construct::SizeofType) || e->is(construct::Alignof)))
            return e->value;
        switch (e->kind)
        {
        case construct::LabelAddress:
            return e->value;        // (GNU: a label's address (see function_definition))
        case construct::Identifier:
        {
            if (e->value)
                return e->value;    // (an enumeration constant)
            if (e->ent == nullptr)
                return std::nullopt;
            auto* ent = e->ent->self();
            if (ent->isConstexpr && ent->constantValue)
            {
                if (aIntegerOnly && ent->constantValue->k != constant::kind::Integer)
                    return std::nullopt;
                return ent->constantValue;
            }
            if (aIntegerOnly)
                return std::nullopt;
            if (e->type->is_array() || e->type->is_function())
                return address(e);
            return std::nullopt;
        }
        case construct::Strings:
            if (aIntegerOnly)
                return std::nullopt;
            return address(e);
        case construct::CompoundLiteral:
            if (aIntegerOnly)
                return std::nullopt;
            if (e->type->is_array())
                return address(e);
            return std::nullopt;
        case construct::Paren:
            return evaluate(e->child(0), aIntegerOnly);
        case construct::Generic:
            return e->selected != nullptr ? evaluate(e->selected, aIntegerOnly) : std::nullopt;
        case construct::Cast:
        {
            auto* a = e->child(1);
            // (an integer constant expression's casts are to integer types, of arithmetic operands; offsetof's idiom (an address from 
            // null cast to an integer) too)
            auto v = evaluate(a, aIntegerOnly && !e->type->is_integer());
            if (!v)
            {
                if (aIntegerOnly && e->type->is_integer())
                    v = evaluate(a, false);
                if (!v)
                    return std::nullopt;
            }
            return convert(*v, value_type(a), e->type);
        }
        case construct::Address:
        {
            if (aIntegerOnly)
            {
                // (offsetof's idiom: &((T*)0)->m)
                auto const v = address(e->child(0));
                if (v && v->k == constant::kind::Address && v->object == nullptr && v->literal == nullptr)
                    return v;
                return std::nullopt;
            }
            if (e->child(0)->type->is_function())
                return address(e->child(0));
            return address(e->child(0));
        }
        case construct::Indirection:
            return std::nullopt;
        case construct::Member:
        case construct::Arrow:
        case construct::Subscript:
            if (!aIntegerOnly && e->type->is_array())
                return address(e);
            return std::nullopt;
        case construct::Plus:
        case construct::Minus:
        case construct::Complement:
        case construct::Not:
        {
            auto const v = evaluate(e->child(0), aIntegerOnly);
            if (!v || v->k == constant::kind::Address)
            {
                if (v && e->is(construct::Not) && !aIntegerOnly)
                    return integer(0u, int_type());
                return std::nullopt;
            }
            auto const operandType = value_type(e->child(0));
            if (e->is(construct::Not))
                return integer((v->k == constant::kind::Floating ? v->floating == 0.0 : v->integer == 0u) ? 1u : 0u, int_type());
            auto const pv = convert(*v, operandType, e->type);
            if (!pv)
                return std::nullopt;
            if (e->is(construct::Plus))
                return pv;
            if (e->is(construct::Minus))
                return pv->k == constant::kind::Floating ? floating(-pv->floating, e->type) : integer(0u - pv->integer, e->type);
            return integer(~pv->integer, e->type);
        }
        case construct::Multiply:
        case construct::Divide:
        case construct::Modulo:
        case construct::Add:
        case construct::Subtract:
        case construct::ShiftLeft:
        case construct::ShiftRight:
        case construct::BitAnd:
        case construct::BitXor:
        case construct::BitOr:
        {
            auto* a = e->child(0);
            auto* b = e->child(1);
            auto const ta = value_type(a);
            auto const tb = value_type(b);
            auto va = evaluate(a, aIntegerOnly);
            auto vb = evaluate(b, aIntegerOnly);
            if (!va || !vb)
                return std::nullopt;
            // (pointer arithmetic on address constants)
            if (e->is(construct::Add) || e->is(construct::Subtract))
            {
                if (ta->is_pointer() && tb->is_integer())
                {
                    if (va->k != constant::kind::Address || !vb->is_integer())
                        return std::nullopt;
                    auto const scale = static_cast<std::int64_t>(size_of(ta->base));
                    auto delta = vb->s() * scale;
                    if (tb->is_unsigned() && size_of(tb) < 8u)
                        delta = static_cast<std::int64_t>(vb->integer) * scale;
                    va->integer += static_cast<std::uint64_t>(e->is(construct::Add) ? delta : -delta);
                    return va;
                }
                if (ta->is_integer() && tb->is_pointer() && e->is(construct::Add))
                {
                    if (vb->k != constant::kind::Address || !va->is_integer())
                        return std::nullopt;
                    vb->integer += static_cast<std::uint64_t>(va->s() * static_cast<std::int64_t>(size_of(tb->base)));
                    return vb;
                }
                if (ta->is_pointer() && tb->is_pointer())
                {
                    if (va->k != constant::kind::Address || vb->k != constant::kind::Address || va->object != vb->object || va->literal != vb->literal)
                        return std::nullopt;
                    auto const size = std::max<std::int64_t>(1, static_cast<std::int64_t>(size_of(ta->base)));
                    return integer(static_cast<std::uint64_t>((static_cast<std::int64_t>(va->integer) - static_cast<std::int64_t>(vb->integer)) / size), e->type);
                }
            }
            if (va->k == constant::kind::Address || vb->k == constant::kind::Address)
                return std::nullopt;
            bool const shift = e->is(construct::ShiftLeft) || e->is(construct::ShiftRight);
            auto const type = e->type;
            auto const ca = convert(*va, ta, shift ? type : type);
            auto const cb = convert(*vb, tb, shift ? promote(tb) : type);
            if (!ca || !cb)
                return std::nullopt;
            if (type->is_floating())
            {
                double const x = ca->floating;
                double const y = cb->floating;
                switch (e->kind)
                {
                case construct::Multiply: return floating(x * y, type);
                case construct::Divide:
                {
                    // (a NaN made by the operation (0.0 / 0.0, as NAN is): positive, whatever the compiler's machine makes)
                    auto const quotient = x / y;
                    return floating(std::isnan(quotient) && !std::isnan(x) && !std::isnan(y) ? std::numeric_limits<double>::quiet_NaN() : quotient, type);
                }
                case construct::Add: return floating(x + y, type);
                case construct::Subtract: return floating(x - y, type);
                default: return std::nullopt;
                }
            }
            auto const x = ca->integer;
            auto const y = cb->integer;
            bool const isSigned = integer_type(type.type)->is_signed();
            auto const w = width_of(type.type);
            switch (e->kind)
            {
            case construct::Multiply: return integer(x * y, type);
            case construct::Divide:
            case construct::Modulo:
                if (y == 0u)
                {
                    warning(e, "division by zero");
                    return std::nullopt;
                }
                if (isSigned)
                {
                    auto const sx = static_cast<std::int64_t>(x);
                    auto const sy = static_cast<std::int64_t>(y);
                    if (sx == INT64_MIN && sy == -1)
                        return integer(e->is(construct::Divide) ? x : 0u, type);
                    return integer(static_cast<std::uint64_t>(e->is(construct::Divide) ? sx / sy : sx % sy), type);
                }
                return integer(e->is(construct::Divide) ? x / y : x % y, type);
            case construct::Add: return integer(x + y, type);
            case construct::Subtract: return integer(x - y, type);
            case construct::ShiftLeft: return integer(y >= 64u ? 0u : x << y, type);
            case construct::ShiftRight:
                if (isSigned)
                    return integer(static_cast<std::uint64_t>(static_cast<std::int64_t>(x) >> std::min<std::uint64_t>(y, 63u)), type);
                return integer(y >= 64u ? 0u : (w < 64u ? (x & ((std::uint64_t{ 1 } << w) - 1u)) : x) >> y, type);
            case construct::BitAnd: return integer(x & y, type);
            case construct::BitXor: return integer(x ^ y, type);
            case construct::BitOr: return integer(x | y, type);
            default: return std::nullopt;
            }
        }
        case construct::Less:
        case construct::Greater:
        case construct::LessEqual:
        case construct::GreaterEqual:
        case construct::Equal:
        case construct::NotEqual:
        {
            auto* a = e->child(0);
            auto* b = e->child(1);
            auto const ta = value_type(a);
            auto const tb = value_type(b);
            auto const va = evaluate(a, aIntegerOnly);
            auto const vb = evaluate(b, aIntegerOnly);
            if (!va || !vb)
                return std::nullopt;
            if (va->k == constant::kind::Address || vb->k == constant::kind::Address)
            {
                // (an address compared with null: known; two addresses of the same object: their offsets)
                bool const equality = e->is(construct::Equal) || e->is(construct::NotEqual);
                if (va->k == constant::kind::Address && vb->k == constant::kind::Address && va->object == vb->object && va->literal == vb->literal)
                {
                    auto const x = va->integer, y = vb->integer;
                    bool r = false;
                    switch (e->kind)
                    {
                    case construct::Less: r = x < y; break;
                    case construct::Greater: r = x > y; break;
                    case construct::LessEqual: r = x <= y; break;
                    case construct::GreaterEqual: r = x >= y; break;
                    case construct::Equal: r = x == y; break;
                    default: r = x != y; break;
                    }
                    return integer(r ? 1u : 0u, int_type());
                }
                if (equality && (va->object != nullptr || va->literal != nullptr || vb->object != nullptr || vb->literal != nullptr))
                {
                    bool const nullA = va->k != constant::kind::Address ? va->integer == 0u : (va->object == nullptr && va->literal == nullptr && va->integer == 0u);
                    bool const nullB = vb->k != constant::kind::Address ? vb->integer == 0u : (vb->object == nullptr && vb->literal == nullptr && vb->integer == 0u);
                    if (nullA || nullB)
                        return integer(e->is(construct::Equal) ? 0u : 1u, int_type());
                }
                return std::nullopt;
            }
            qtype common = (ta->is_arithmetic() && tb->is_arithmetic()) ? usual_arithmetic(ta, tb) : qtype{ basic(type_kind::ULong) };
            auto const ca = convert(*va, ta, common);
            auto const cb = convert(*vb, tb, common);
            if (!ca || !cb)
                return std::nullopt;
            bool r = false;
            if (common->is_floating())
            {
                auto const x = ca->floating, y = cb->floating;
                switch (e->kind)
                {
                case construct::Less: r = x < y; break;
                case construct::Greater: r = x > y; break;
                case construct::LessEqual: r = x <= y; break;
                case construct::GreaterEqual: r = x >= y; break;
                case construct::Equal: r = x == y; break;
                default: r = x != y; break;
                }
            }
            else if (integer_type(common.type)->is_signed())
            {
                auto const x = ca->s(), y = cb->s();
                switch (e->kind)
                {
                case construct::Less: r = x < y; break;
                case construct::Greater: r = x > y; break;
                case construct::LessEqual: r = x <= y; break;
                case construct::GreaterEqual: r = x >= y; break;
                case construct::Equal: r = x == y; break;
                default: r = x != y; break;
                }
            }
            else
            {
                auto const x = ca->integer, y = cb->integer;
                switch (e->kind)
                {
                case construct::Less: r = x < y; break;
                case construct::Greater: r = x > y; break;
                case construct::LessEqual: r = x <= y; break;
                case construct::GreaterEqual: r = x >= y; break;
                case construct::Equal: r = x == y; break;
                default: r = x != y; break;
                }
            }
            return integer(r ? 1u : 0u, int_type());
        }
        case construct::And:
        case construct::Or:
        {
            auto const truth = [&](std::optional<constant> const& v) -> std::optional<bool>
            {
                if (!v)
                    return std::nullopt;
                if (v->k == constant::kind::Floating)
                    return v->floating != 0.0;
                if (v->k == constant::kind::Address)
                {
                    if (v->object != nullptr || v->literal != nullptr)
                        return true;
                    return v->integer != 0u;
                }
                return v->integer != 0u;
            };
            auto const a = truth(evaluate(e->child(0), aIntegerOnly));
            if (!a)
                return std::nullopt;
            if (e->is(construct::And) && !*a)
                return integer(0u, int_type());
            if (e->is(construct::Or) && *a)
                return integer(1u, int_type());
            auto const b = truth(evaluate(e->child(1), aIntegerOnly));
            if (!b)
                return std::nullopt;
            return integer(*b ? 1u : 0u, int_type());
        }
        case construct::Conditional:
        {
            auto const c = evaluate(e->child(0), aIntegerOnly);
            if (!c)
                return std::nullopt;
            bool truth = false;
            if (c->k == constant::kind::Floating)
                truth = c->floating != 0.0;
            else if (c->k == constant::kind::Address)
                truth = c->object != nullptr || c->literal != nullptr || c->integer != 0u;
            else
                truth = c->integer != 0u;
            auto* chosen = truth ? e->child(1) : e->child(2);
            auto const v = evaluate(chosen, aIntegerOnly);
            if (!v)
                return std::nullopt;
            if (e->type->is_void())
                return v;
            return convert(*v, value_type(chosen), e->type);
        }
        case construct::Call:
            if (e->builtin == static_cast<std::uint32_t>(builtin_kind::ConstantP))
            {
                auto const v = evaluate(e->child(1), false);
                return integer(v ? 1u : 0u, int_type());
            }
            if (e->builtin == static_cast<std::uint32_t>(builtin_kind::Expect))
                return evaluate(e->child(1), aIntegerOnly);
            return std::nullopt;
        default:
            return std::nullopt;
        }
    }

    node* program::statement_expression_value(node* aExpression) const
    {
        auto* compound = aExpression->child(0);
        if (compound == nullptr || compound->children.empty())
            return nullptr;
        auto* last = compound->children.back();
        if (!last->is(construct::ExpressionStatement))
            return nullptr;
        for (auto* c : last->children)
            if (!c->is(construct::Attributes))
                return c;
        return nullptr;
    }
}
