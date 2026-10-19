/*
  re.cpp

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
*/

// The module re (CPython 3.13's sre: its syntax, semantics (backtracking, empty matches), objects and messages) for str patterns

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            enum : int
            {
                FlagTemplate = 1, FlagIgnoreCase = 2, FlagLocale = 4, FlagMultiline = 8, FlagDotAll = 16, FlagUnicode = 32, FlagVerbose = 64,
                FlagDebug = 128, FlagAscii = 256
            };

            // --- the pattern's nodes

            struct char_set
            {
                std::vector<std::pair<char32_t, char32_t>> ranges;
                enum : unsigned { Digit = 1u, NotDigit = 2u, Word = 4u, NotWord = 8u, Space = 16u, NotSpace = 32u };
                unsigned categories = 0u;
                bool negated = false;
            };

            struct node
            {
                enum kind_t { Literal, Any, Set, Bol, Eol, StringStart, StringEnd, Boundary, NonBoundary, Group, Alternation, Repeat, Backref, Look, Atomic,
                    Conditional, Sequence } kind;
                char32_t character = 0;
                char_set set;
                int group = -1;                     ///< (Group: its capture's (-1: none); Backref and Conditional: the group's)
                std::vector<std::unique_ptr<node>> children;
                std::size_t minimum = 0u;
                std::size_t maximum = 0u;
                int mode = 0;                       ///< (Repeat: 0 greedy, 1 lazy, 2 possessive)
                bool behind = false;
                bool negative = false;
                std::size_t width = 0u;             ///< (Look behind: its width)
                int flags = 0;                      ///< (the flags in effect)
            };

            struct program
            {
                std::unique_ptr<node> root;
                std::size_t groups = 0u;
                std::vector<std::pair<std::string, int>> names;     ///< (in order of definition)
                int flags = 0;
            };

            // --- errors

            struct pattern_error
            {
                std::string message;
                std::size_t position;
            };

            // --- character tests

            bool is_word(char32_t c, int aFlags)
            {
                if ((aFlags & FlagAscii) != 0)
                    return c < 0x80u && (std::isalnum(static_cast<int>(c)) != 0 || c == U'_');
                return c == U'_' || char_is_alnum(c);
            }
            bool is_digit(char32_t c, int aFlags) { return (aFlags & FlagAscii) != 0 ? (c >= U'0' && c <= U'9') : char_is_decimal(c); }
            bool is_space(char32_t c, int aFlags)
            {
                if ((aFlags & FlagAscii) != 0)
                    return c == U' ' || (c >= 0x09u && c <= 0x0Du);
                return char_is_space(c);
            }
            char32_t fold(char32_t c, int aFlags)
            {
                if ((aFlags & FlagAscii) != 0)
                    return c < 0x80u ? static_cast<char32_t>(std::tolower(static_cast<int>(c))) : c;
                return char_lower(c);
            }

            bool set_contains(char_set const& aSet, char32_t c, int aFlags)
            {
                auto const test = [&](char32_t x)
                {
                    for (auto const& r : aSet.ranges)
                        if (x >= r.first && x <= r.second)
                            return true;
                    return false;
                };
                bool found = test(c);
                if (!found && (aFlags & FlagIgnoreCase) != 0)
                {
                    auto const lower = fold(c, aFlags);
                    auto const upper = (aFlags & FlagAscii) != 0 ? (c < 0x80u ? static_cast<char32_t>(std::toupper(static_cast<int>(c))) : c) : char_upper(c);
                    found = test(lower) || test(upper);
                    // (a range's characters' other cases)
                    if (!found)
                        for (auto const& r : aSet.ranges)
                            if (r.second - r.first < 0x1000u)
                                for (auto x = r.first; x <= r.second && !found; ++x)
                                    found = fold(x, aFlags) == lower;
                }
                auto const cats = aSet.categories;
                if (!found && cats != 0u)
                    found = ((cats & char_set::Digit) != 0u && is_digit(c, aFlags)) || ((cats & char_set::NotDigit) != 0u && !is_digit(c, aFlags)) ||
                        ((cats & char_set::Word) != 0u && is_word(c, aFlags)) || ((cats & char_set::NotWord) != 0u && !is_word(c, aFlags)) ||
                        ((cats & char_set::Space) != 0u && is_space(c, aFlags)) || ((cats & char_set::NotSpace) != 0u && !is_space(c, aFlags));
                return found != aSet.negated;
            }

            // --- the parser (sre_parse)

            class parser
            {
            public:
                parser(std::u32string const& aPattern, int aFlags) : iPattern{ aPattern }, iFlags{ aFlags } {}
            public:
                std::unique_ptr<program> parse()
                {
                    auto result = std::make_unique<program>();
                    iProgram = result.get();
                    // (global flags at the start)
                    result->root = alternation(iFlags, 0u);
                    if (iPosition < iPattern.size())
                    {
                        if (iPattern[iPosition] == U')')
                            throw pattern_error{ "unbalanced parenthesis", iPosition };
                        throw pattern_error{ "internal error", iPosition };
                    }
                    result->flags = iFlags;
                    result->groups = iGroups;
                    return result;
                }
                int flags() const { return iFlags; }
            private:
                bool at_end() const { return iPosition >= iPattern.size(); }
                char32_t peek() const { return iPattern[iPosition]; }
                bool verbose(int aFlags) const { return (aFlags & FlagVerbose) != 0; }
                void skip_verbose(int aFlags)
                {
                    if (!verbose(aFlags))
                        return;
                    for (;;)
                    {
                        while (!at_end() && (peek() == U' ' || peek() == U'\t' || peek() == U'\n' || peek() == U'\r' || peek() == U'\f' || peek() == U'\v'))
                            ++iPosition;
                        if (!at_end() && peek() == U'#')
                        {
                            while (!at_end() && peek() != U'\n')
                                ++iPosition;
                            continue;
                        }
                        break;
                    }
                }
                std::unique_ptr<node> make(node::kind_t aKind, int aFlags)
                {
                    auto n = std::make_unique<node>();
                    n->kind = aKind;
                    n->flags = aFlags;
                    return n;
                }
                std::unique_ptr<node> alternation(int& aFlags, std::size_t aDepth)
                {
                    auto alt = make(node::Alternation, aFlags);
                    alt->children.push_back(sequence(aFlags, aDepth));
                    while (!at_end() && peek() == U'|')
                    {
                        ++iPosition;
                        alt->children.push_back(sequence(aFlags, aDepth));
                    }
                    if (alt->children.size() == 1u)
                        return std::move(alt->children[0]);
                    return alt;
                }
                std::unique_ptr<node> sequence(int& aFlags, std::size_t aDepth)
                {
                    auto seq = make(node::Sequence, aFlags);
                    for (;;)
                    {
                        skip_verbose(aFlags);
                        if (at_end() || peek() == U'|' || peek() == U')')
                            break;
                        auto const start = iPosition;
                        auto item = atom(aFlags, aDepth);
                        if (!item)
                            continue;
                        skip_verbose(aFlags);
                        item = quantified(std::move(item), aFlags, start);
                        seq->children.push_back(std::move(item));
                    }
                    return seq;
                }
                bool quantifier_at(std::size_t& aMin, std::size_t& aMax, std::size_t& aLength)
                {
                    if (at_end())
                        return false;
                    auto const c = peek();
                    if (c == U'*') { aMin = 0u; aMax = SIZE_MAX; aLength = 1u; return true; }
                    if (c == U'+') { aMin = 1u; aMax = SIZE_MAX; aLength = 1u; return true; }
                    if (c == U'?') { aMin = 0u; aMax = 1u; aLength = 1u; return true; }
                    if (c != U'{')
                        return false;
                    // ({m}, {m,}, {,n}, {m,n}: else a literal)
                    auto p = iPosition + 1u;
                    std::u32string low, high;
                    while (p < iPattern.size() && iPattern[p] >= U'0' && iPattern[p] <= U'9')
                        low += iPattern[p++];
                    bool comma = false;
                    if (p < iPattern.size() && iPattern[p] == U',')
                    {
                        comma = true;
                        ++p;
                        while (p < iPattern.size() && iPattern[p] >= U'0' && iPattern[p] <= U'9')
                            high += iPattern[p++];
                    }
                    if (p >= iPattern.size() || iPattern[p] != U'}' || (low.empty() && !comma))
                        return false;
                    auto const number = [](std::u32string const& t) { std::size_t v = 0u; for (auto ch : t) v = v * 10u + (ch - U'0'); return v; };
                    aMin = low.empty() ? 0u : number(low);
                    aMax = comma ? (high.empty() ? SIZE_MAX : number(high)) : aMin;
                    if (aMax < aMin)
                        throw pattern_error{ "min repeat greater than max repeat", iPosition + 1u };
                    aLength = p + 1u - iPosition;
                    return true;
                }
                std::unique_ptr<node> quantified(std::unique_ptr<node> aItem, int aFlags, std::size_t aStart)
                {
                    std::size_t min = 0u, max = 0u, length = 0u;
                    if (!quantifier_at(min, max, length))
                        return aItem;
                    if (aItem->kind == node::Bol || aItem->kind == node::Eol || aItem->kind == node::StringStart || aItem->kind == node::StringEnd ||
                        aItem->kind == node::Boundary || aItem->kind == node::NonBoundary || (aItem->kind == node::Look))
                        (void)aStart;
                    iPosition += length;
                    auto rep = make(node::Repeat, aFlags);
                    rep->minimum = min;
                    rep->maximum = max;
                    if (!at_end() && peek() == U'?')
                    {
                        rep->mode = 1;
                        ++iPosition;
                    }
                    else if (!at_end() && peek() == U'+')
                    {
                        rep->mode = 2;
                        ++iPosition;
                    }
                    rep->children.push_back(std::move(aItem));
                    skip_verbose(aFlags);
                    std::size_t m2 = 0u, x2 = 0u, l2 = 0u;
                    if (quantifier_at(m2, x2, l2))
                        throw pattern_error{ "multiple repeat", iPosition };
                    return rep;
                }
                char32_t hex_escape(std::size_t aDigits, std::size_t aStart)
                {
                    char32_t value = 0u;
                    for (std::size_t i = 0u; i < aDigits; ++i)
                    {
                        if (at_end() || !std::isxdigit(static_cast<int>(peek() < 0x80u ? peek() : 0u)))
                        {
                            std::string text = "\\" + std::string(1u, static_cast<char>(iPattern[aStart + 1u]));
                            for (auto p = aStart + 2u; p < iPosition; ++p)
                                text += static_cast<char>(iPattern[p]);
                            throw pattern_error{ "incomplete escape " + text, aStart };
                        }
                        auto const c = static_cast<char>(peek());
                        value = value * 16u + static_cast<char32_t>(std::isdigit(c) ? c - '0' : std::tolower(c) - 'a' + 10);
                        ++iPosition;
                    }
                    if (value > 0x10FFFFu)
                        throw pattern_error{ "bad escape", aStart };
                    return value;
                }
                std::string escape_text(std::size_t aStart) const
                {
                    std::string text;
                    for (auto p = aStart; p < iPosition && p < iPattern.size(); ++p)
                        append_code_point(text, iPattern[p]);
                    return text;
                }
                // (an escape in a class (aInClass) or not: a character (aCategory 0) or a category)
                char32_t escape(bool aInClass, unsigned& aCategory, std::unique_ptr<node>& aSpecial, int aFlags)
                {
                    auto const start = iPosition;
                    ++iPosition;
                    if (at_end())
                        throw pattern_error{ "bad escape (end of pattern)", start };
                    auto const c = peek();
                    ++iPosition;
                    aCategory = 0u;
                    switch (c)
                    {
                    case U'a': return 7u;
                    case U'f': return 12u;
                    case U'n': return 10u;
                    case U'r': return 13u;
                    case U't': return 9u;
                    case U'v': return 11u;
                    case U'\\': return U'\\';
                    case U'x': return hex_escape(2u, start);
                    case U'u': return hex_escape(4u, start);
                    case U'U': return hex_escape(8u, start);
                    case U'd': aCategory = char_set::Digit; return 0u;
                    case U'D': aCategory = char_set::NotDigit; return 0u;
                    case U'w': aCategory = char_set::Word; return 0u;
                    case U'W': aCategory = char_set::NotWord; return 0u;
                    case U's': aCategory = char_set::Space; return 0u;
                    case U'S': aCategory = char_set::NotSpace; return 0u;
                    case U'b':
                        if (aInClass)
                            return 8u;
                        aSpecial = make(node::Boundary, aFlags);
                        return 0u;
                    default:
                        break;
                    }
                    if (!aInClass)
                    {
                        switch (c)
                        {
                        case U'A': aSpecial = make(node::StringStart, aFlags); return 0u;
                        case U'Z': aSpecial = make(node::StringEnd, aFlags); return 0u;
                        case U'B': aSpecial = make(node::NonBoundary, aFlags); return 0u;
                        default: break;
                        }
                    }
                    if (c == U'0' || (aInClass && c >= U'1' && c <= U'7'))
                    {
                        // (octal)
                        char32_t value = c - U'0';
                        for (int i = 0; i < 2 && !at_end() && peek() >= U'0' && peek() <= U'7'; ++i)
                            value = value * 8u + (peek() - U'0'), ++iPosition;
                        if (value > 0377u)
                            throw pattern_error{ "octal escape value " + escape_text(start) + " outside of range 0-0o377", start };
                        return value;
                    }
                    if (!aInClass && c >= U'1' && c <= U'9')
                    {
                        // (a backreference, or an octal escape of 3 digits)
                        if (!at_end() && peek() >= U'0' && peek() <= U'7' && c <= U'7' && iPosition + 1u < iPattern.size() && iPattern[iPosition + 1u] >= U'0' &&
                            iPattern[iPosition + 1u] <= U'7')
                        {
                            char32_t value = (c - U'0') * 64u + (peek() - U'0') * 8u + (iPattern[iPosition + 1u] - U'0');
                            iPosition += 2u;
                            if (value > 0377u)
                                throw pattern_error{ "octal escape value " + escape_text(start) + " outside of range 0-0o377", start };
                            return value;
                        }
                        std::size_t group = c - U'0';
                        if (!at_end() && peek() >= U'0' && peek() <= U'9')
                            group = group * 10u + (peek() - U'0'), ++iPosition;
                        if (group > iGroups || std::find(iOpen.begin(), iOpen.end(), static_cast<int>(group)) != iOpen.end())
                        {
                            if (group > iGroups)
                                throw pattern_error{ "invalid group reference " + std::to_string(group), start + 1u };
                            throw pattern_error{ "cannot refer to an open group", start };
                        }
                        aSpecial = make(node::Backref, aFlags);
                        aSpecial->group = static_cast<int>(group);
                        return 0u;
                    }
                    if ((c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || (c >= U'0' && c <= U'9'))
                        throw pattern_error{ "bad escape " + escape_text(start), start };
                    return c;
                }
                std::unique_ptr<node> character_class(int aFlags)
                {
                    auto const start = iPosition;
                    ++iPosition;
                    auto n = make(node::Set, aFlags);
                    if (!at_end() && peek() == U'^')
                    {
                        n->set.negated = true;
                        ++iPosition;
                    }
                    bool first = true;
                    for (;;)
                    {
                        if (at_end())
                            throw pattern_error{ "unterminated character set", start };
                        if (peek() == U']' && !first)
                        {
                            ++iPosition;
                            break;
                        }
                        first = false;
                        auto const itemStart = iPosition;
                        char32_t low = 0u;
                        unsigned category = 0u;
                        std::unique_ptr<node> special;
                        if (peek() == U'\\')
                            low = escape(true, category, special, aFlags);
                        else
                            low = iPattern[iPosition++];
                        if (category != 0u)
                        {
                            n->set.categories |= category;
                            if (!at_end() && peek() == U'-' && iPosition + 1u < iPattern.size() && iPattern[iPosition + 1u] != U']')
                                throw pattern_error{ "bad character range " + escape_text(itemStart) + "-" + std::string(1u, static_cast<char>(iPattern[iPosition + 1u])),
                                    itemStart };
                            continue;
                        }
                        if (!at_end() && peek() == U'-' && iPosition + 1u < iPattern.size() && iPattern[iPosition + 1u] != U']')
                        {
                            ++iPosition;
                            char32_t high = 0u;
                            if (peek() == U'\\')
                            {
                                high = escape(true, category, special, aFlags);
                                if (category != 0u)
                                    throw pattern_error{ "bad character range " + escape_text(itemStart), itemStart };
                            }
                            else
                                high = iPattern[iPosition++];
                            if (high < low)
                                throw pattern_error{ "bad character range " + escape_text(itemStart), itemStart };
                            n->set.ranges.emplace_back(low, high);
                        }
                        else
                            n->set.ranges.emplace_back(low, low);
                    }
                    return n;
                }
                int flag_of(char32_t c) const
                {
                    switch (c)
                    {
                    case U'i': return FlagIgnoreCase;
                    case U'm': return FlagMultiline;
                    case U's': return FlagDotAll;
                    case U'x': return FlagVerbose;
                    case U'a': return FlagAscii;
                    case U'u': return FlagUnicode;
                    case U'L': return FlagLocale;
                    default: return 0;
                    }
                }
                std::string group_name(char32_t aTerminator, std::size_t aStart)
                {
                    std::string name;
                    while (!at_end() && peek() != aTerminator)
                        append_code_point(name, iPattern[iPosition++]);
                    if (at_end())
                        throw pattern_error{ "missing " + std::string(1u, static_cast<char>(aTerminator)) + ", unterminated name", aStart };
                    ++iPosition;
                    if (name.empty())
                        throw pattern_error{ "missing group name", aStart };
                    ref const text{ make_str(name) };
                    ref const ok{ call_method_named(text.get(), "isidentifier", {}) };
                    if (!ok || truth(ok.get()) != 1)
                        throw pattern_error{ "bad character in group name '" + name + "'", aStart };
                    return name;
                }
                int group_of(std::string const& aName, std::size_t aStart) const
                {
                    for (auto const& n : iProgram->names)
                        if (n.first == aName)
                            return n.second;
                    throw pattern_error{ "unknown group name '" + aName + "'", aStart };
                }
                std::unique_ptr<node> group(int aFlags, std::size_t aDepth)
                {
                    auto const start = iPosition;
                    ++iPosition;
                    int capture = -1;
                    int innerFlags = aFlags;
                    std::unique_ptr<node> special;
                    if (!at_end() && peek() == U'?')
                    {
                        ++iPosition;
                        if (at_end())
                            throw pattern_error{ "unexpected end of pattern", iPosition };
                        auto const c = peek();
                        if (c == U':')
                            ++iPosition;
                        else if (c == U'P' && iPosition + 1u < iPattern.size() && iPattern[iPosition + 1u] == U'<')
                        {
                            iPosition += 2u;
                            auto const name = group_name(U'>', iPosition);
                            for (auto const& n : iProgram->names)
                                if (n.first == name)
                                    throw pattern_error{ "redefinition of group name '" + name + "' as group " + std::to_string(iGroups + 1u) +
                                        "; was group " + std::to_string(n.second), start + 4u };
                            capture = static_cast<int>(++iGroups);
                            iProgram->names.emplace_back(name, capture);
                        }
                        else if (c == U'P' && iPosition + 1u < iPattern.size() && iPattern[iPosition + 1u] == U'=')
                        {
                            iPosition += 2u;
                            auto const nameStart = iPosition;
                            auto const name = group_name(U')', nameStart);
                            auto n = make(node::Backref, aFlags);
                            n->group = group_of(name, nameStart);
                            return n;
                        }
                        else if (c == U'#')
                        {
                            while (!at_end() && peek() != U')')
                                ++iPosition;
                            if (at_end())
                                throw pattern_error{ "missing ), unterminated comment", start };
                            ++iPosition;
                            return nullptr;
                        }
                        else if (c == U'=' || c == U'!')
                        {
                            ++iPosition;
                            special = make(node::Look, aFlags);
                            special->negative = c == U'!';
                        }
                        else if (c == U'<' && iPosition + 1u < iPattern.size() && (iPattern[iPosition + 1u] == U'=' || iPattern[iPosition + 1u] == U'!'))
                        {
                            special = make(node::Look, aFlags);
                            special->behind = true;
                            special->negative = iPattern[iPosition + 1u] == U'!';
                            iPosition += 2u;
                        }
                        else if (c == U'>')
                        {
                            ++iPosition;
                            special = make(node::Atomic, aFlags);
                        }
                        else if (c == U'(')
                        {
                            // ((?(id/name)yes|no))
                            ++iPosition;
                            auto const refStart = iPosition;
                            std::string name;
                            while (!at_end() && peek() != U')')
                                append_code_point(name, iPattern[iPosition++]);
                            if (at_end())
                                throw pattern_error{ "missing ), unterminated name", refStart };
                            ++iPosition;
                            auto n = make(node::Conditional, aFlags);
                            if (!name.empty() && std::all_of(name.begin(), name.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; }))
                            {
                                n->group = std::stoi(name);
                                if (n->group == 0 || static_cast<std::size_t>(n->group) > iGroups)
                                    throw pattern_error{ "invalid group reference " + name, refStart };
                            }
                            else
                                n->group = group_of(name, refStart);
                            int flags = aFlags;
                            auto yes = sequence(flags, aDepth + 1u);
                            std::unique_ptr<node> no;
                            if (!at_end() && peek() == U'|')
                            {
                                ++iPosition;
                                no = sequence(flags, aDepth + 1u);
                                if (!at_end() && peek() == U'|')
                                    throw pattern_error{ "conditional backref with more than two branches", iPosition };
                            }
                            if (at_end() || peek() != U')')
                                throw pattern_error{ "missing ), unterminated subpattern", start };
                            ++iPosition;
                            n->children.push_back(std::move(yes));
                            n->children.push_back(no ? std::move(no) : make(node::Sequence, aFlags));
                            return n;
                        }
                        else if (flag_of(c) != 0 || c == U'-')
                        {
                            // ((?flags) or (?flags-flags:...))
                            int add = 0, remove = 0;
                            bool removing = false;
                            while (!at_end() && (flag_of(peek()) != 0 || peek() == U'-'))
                            {
                                if (peek() == U'-')
                                    removing = true;
                                else
                                    (removing ? remove : add) |= flag_of(peek());
                                ++iPosition;
                            }
                            if (at_end())
                                throw pattern_error{ "missing -, : or )", iPosition };
                            if (peek() == U')')
                            {
                                if (removing)
                                    throw pattern_error{ "missing :", iPosition };
                                // (global flags: only at the start)
                                if (start != 0u)
                                    throw pattern_error{ "global flags not at the start of the expression", start };
                                ++iPosition;
                                iFlags |= add;
                                return nullptr;
                            }
                            if (peek() != U':')
                                throw pattern_error{ std::string{ "unknown flag" }, iPosition };
                            ++iPosition;
                            innerFlags = (aFlags | add) & ~remove;
                        }
                        else
                        {
                            std::string text = "?";
                            append_code_point(text, c);
                            if (c == U'<' && iPosition + 1u < iPattern.size())
                                append_code_point(text, iPattern[iPosition + 1u]);
                            throw pattern_error{ "unknown extension " + text, start + 1u };
                        }
                    }
                    else
                        capture = static_cast<int>(++iGroups);
                    if (capture > 0)
                        iOpen.push_back(capture);
                    auto inner = alternation(innerFlags, aDepth + 1u);
                    if (capture > 0)
                        iOpen.pop_back();
                    if (at_end() || peek() != U')')
                        throw pattern_error{ "missing ), unterminated subpattern", start };
                    ++iPosition;
                    if (special)
                    {
                        if (special->kind == node::Look && special->behind)
                        {
                            auto const w = width_of(*inner);
                            if (!w)
                                throw pattern_error{ "look-behind requires fixed-width pattern", SIZE_MAX };
                            special->width = *w;
                        }
                        special->children.push_back(std::move(inner));
                        return special;
                    }
                    auto g = make(node::Group, aFlags);
                    g->group = capture;
                    g->children.push_back(std::move(inner));
                    return g;
                }
                // (a fixed width, if any)
                std::optional<std::size_t> width_of(node const& n) const
                {
                    switch (n.kind)
                    {
                    case node::Literal: case node::Any: case node::Set: return 1u;
                    case node::Bol: case node::Eol: case node::StringStart: case node::StringEnd: case node::Boundary: case node::NonBoundary: case node::Look:
                        return 0u;
                    case node::Group: case node::Atomic: return width_of(*n.children[0]);
                    case node::Sequence:
                        {
                            std::size_t total = 0u;
                            for (auto const& c : n.children)
                            {
                                auto const w = width_of(*c);
                                if (!w)
                                    return {};
                                total += *w;
                            }
                            return total;
                        }
                    case node::Alternation: case node::Conditional:
                        {
                            std::optional<std::size_t> common;
                            for (auto const& c : n.children)
                            {
                                auto const w = width_of(*c);
                                if (!w || (common && *common != *w))
                                    return {};
                                common = w;
                            }
                            return common;
                        }
                    case node::Repeat:
                        {
                            if (n.minimum != n.maximum)
                                return {};
                            auto const w = width_of(*n.children[0]);
                            if (!w)
                                return {};
                            return *w * n.minimum;
                        }
                    default:
                        return {};
                    }
                }
                std::unique_ptr<node> atom(int& aFlags, std::size_t aDepth)
                {
                    auto const c = peek();
                    switch (c)
                    {
                    case U'(':
                        return group(aFlags, aDepth);
                    case U'[':
                        return character_class(aFlags);
                    case U'.':
                        ++iPosition;
                        return make(node::Any, aFlags);
                    case U'^':
                        ++iPosition;
                        return make(node::Bol, aFlags);
                    case U'$':
                        ++iPosition;
                        return make(node::Eol, aFlags);
                    case U'*': case U'+': case U'?':
                        throw pattern_error{ "nothing to repeat", iPosition };
                    case U'{':
                        {
                            std::size_t min = 0u, max = 0u, length = 0u;
                            if (quantifier_at(min, max, length))
                                throw pattern_error{ "nothing to repeat", iPosition };
                            ++iPosition;
                            auto n = make(node::Literal, aFlags);
                            n->character = c;
                            return n;
                        }
                    case U'\\':
                        {
                            unsigned category = 0u;
                            std::unique_ptr<node> special;
                            auto const value = escape(false, category, special, aFlags);
                            if (special)
                                return special;
                            if (category != 0u)
                            {
                                auto n = make(node::Set, aFlags);
                                n->set.categories = category;
                                return n;
                            }
                            auto n = make(node::Literal, aFlags);
                            n->character = value;
                            return n;
                        }
                    default:
                        {
                            ++iPosition;
                            auto n = make(node::Literal, aFlags);
                            n->character = c;
                            return n;
                        }
                    }
                }
            private:
                std::u32string const& iPattern;
                std::size_t iPosition = 0u;
                int iFlags;
                std::size_t iGroups = 0u;
                std::vector<int> iOpen;
                program* iProgram = nullptr;
            };

            // --- the matcher (backtracking; continuations)

            struct continuation
            {
                bool (*function)(void const*, std::size_t);
                void const* context;
                bool operator()(std::size_t aPosition) const { return function(context, aPosition); }
            };

            template <typename F>
            continuation make_continuation(F const& aFunction)
            {
                return continuation{ [](void const* aContext, std::size_t aPosition) { return (*static_cast<F const*>(aContext))(aPosition); }, &aFunction };
            }

            struct matcher
            {
                std::u32string const& text;
                std::size_t end;                    ///< (endpos)
                std::vector<long long> marks;       ///< (each group's start and end: -1 if not matched)
                int lastindex = -1;
                std::size_t steps = 0u;

                bool single(node const& n, std::size_t aPosition) const
                {
                    if (aPosition >= end)
                        return false;
                    auto const c = text[aPosition];
                    switch (n.kind)
                    {
                    case node::Literal:
                        return (n.flags & FlagIgnoreCase) != 0 ? fold(c, n.flags) == fold(n.character, n.flags) : c == n.character;
                    case node::Any:
                        return (n.flags & FlagDotAll) != 0 || c != U'\n';
                    case node::Set:
                        return set_contains(n.set, c, n.flags);
                    default:
                        return false;
                    }
                }

                bool word_at(std::size_t aPosition, int aFlags) const { return aPosition < end && is_word(text[aPosition], aFlags); }

                bool sequence(node const& n, std::size_t aIndex, std::size_t aPosition, continuation const& k)
                {
                    if (aIndex == n.children.size())
                        return k(aPosition);
                    auto const next = [&](std::size_t p) { return sequence(n, aIndex + 1u, p, k); };
                    return match(*n.children[aIndex], aPosition, make_continuation(next));
                }

                bool repeat(node const& n, std::size_t aCount, std::size_t aPosition, continuation const& k)
                {
                    auto const& child = *n.children[0];
                    auto const more = [&](std::size_t p)
                    {
                        // (an empty iteration once the minimum is met ends the repetition)
                        if (p == aPosition && aCount >= n.minimum)
                            return false;
                        return repeat(n, aCount + 1u, p, k);
                    };
                    if (n.mode == 1)
                    {
                        if (aCount >= n.minimum && k(aPosition))
                            return true;
                        return aCount < n.maximum && match(child, aPosition, make_continuation(more));
                    }
                    if (aCount < n.maximum && match(child, aPosition, make_continuation(more)))
                        return true;
                    return aCount >= n.minimum && k(aPosition);
                }

                bool match(node const& n, std::size_t aPosition, continuation const& k)
                {
                    ++steps;
                    switch (n.kind)
                    {
                    case node::Literal: case node::Any: case node::Set:
                        return single(n, aPosition) && k(aPosition + 1u);
                    case node::Bol:
                        return (aPosition == 0u || ((n.flags & FlagMultiline) != 0 && text[aPosition - 1u] == U'\n')) && k(aPosition);
                    case node::Eol:
                        return (aPosition == end || (aPosition + 1u == end && text[aPosition] == U'\n') ||
                            ((n.flags & FlagMultiline) != 0 && aPosition < end && text[aPosition] == U'\n')) && k(aPosition);
                    case node::StringStart:
                        return aPosition == 0u && k(aPosition);
                    case node::StringEnd:
                        return aPosition == end && k(aPosition);
                    case node::Boundary: case node::NonBoundary:
                        {
                            bool const before = aPosition > 0u && aPosition - 1u < end && is_word(text[aPosition - 1u], n.flags);
                            bool const after = word_at(aPosition, n.flags);
                            bool const boundary = before != after;
                            return (n.kind == node::Boundary ? boundary : !boundary) && k(aPosition);
                        }
                    case node::Sequence:
                        return sequence(n, 0u, aPosition, k);
                    case node::Alternation:
                        for (auto const& c : n.children)
                            if (match(*c, aPosition, k))
                                return true;
                        return false;
                    case node::Group:
                        {
                            if (n.group < 0)
                                return match(*n.children[0], aPosition, k);
                            auto const g = static_cast<std::size_t>(n.group);
                            auto const close = [&](std::size_t p)
                            {
                                auto const oldStart = marks[2u * g];
                                auto const oldEnd = marks[2u * g + 1u];
                                auto const oldLast = lastindex;
                                marks[2u * g] = static_cast<long long>(aPosition);
                                marks[2u * g + 1u] = static_cast<long long>(p);
                                lastindex = n.group;
                                if (k(p))
                                    return true;
                                marks[2u * g] = oldStart;
                                marks[2u * g + 1u] = oldEnd;
                                lastindex = oldLast;
                                return false;
                            };
                            return match(*n.children[0], aPosition, make_continuation(close));
                        }
                    case node::Repeat:
                        {
                            auto const& child = *n.children[0];
                            if (child.kind == node::Literal || child.kind == node::Any || child.kind == node::Set)
                            {
                                // (a single character: the run's length, then backtracking without recursion)
                                std::size_t count = 0u;
                                while (count < n.maximum && single(child, aPosition + count))
                                    ++count;
                                if (count < n.minimum)
                                    return false;
                                if (n.mode == 2)
                                    return k(aPosition + count);
                                if (n.mode == 1)
                                {
                                    for (auto c = n.minimum; c <= count; ++c)
                                        if (k(aPosition + c))
                                            return true;
                                    return false;
                                }
                                for (auto c = count + 1u; c-- > n.minimum;)
                                    if (k(aPosition + c))
                                        return true;
                                return false;
                            }
                            if (n.mode == 2)
                            {
                                // (possessive: the greedy match, without backtracking into it)
                                std::size_t found = 0u;
                                bool matched = false;
                                node greedy;
                                auto const take = [&](std::size_t p) { found = p; matched = true; return true; };
                                auto const saved = marks;
                                auto const savedLast = lastindex;
                                struct greedy_view { node const* n; };
                                // (a greedy repeat of the same child)
                                auto const greedyRepeat = [&](auto&& self, std::size_t aCount, std::size_t aPos) -> bool
                                {
                                    auto const more = [&](std::size_t p) { return !(p == aPos && aCount >= n.minimum) && self(self, aCount + 1u, p); };
                                    if (aCount < n.maximum && match(child, aPos, make_continuation(more)))
                                        return true;
                                    return aCount >= n.minimum && take(aPos);
                                };
                                greedyRepeat(greedyRepeat, 0u, aPosition);
                                if (!matched)
                                    return false;
                                if (k(found))
                                    return true;
                                marks = saved;
                                lastindex = savedLast;
                                return false;
                            }
                            return repeat(n, 0u, aPosition, k);
                        }
                    case node::Backref:
                        {
                            auto const g = static_cast<std::size_t>(n.group);
                            if (marks[2u * g] < 0 || marks[2u * g + 1u] < 0)
                                return false;
                            auto const start = static_cast<std::size_t>(marks[2u * g]);
                            auto const length = static_cast<std::size_t>(marks[2u * g + 1u]) - start;
                            if (aPosition + length > end)
                                return false;
                            for (std::size_t i = 0u; i < length; ++i)
                            {
                                auto const a = text[start + i];
                                auto const b = text[aPosition + i];
                                if ((n.flags & FlagIgnoreCase) != 0 ? fold(a, n.flags) != fold(b, n.flags) : a != b)
                                    return false;
                            }
                            return k(aPosition + length);
                        }
                    case node::Look:
                        {
                            auto const saved = marks;
                            auto const savedLast = lastindex;
                            bool found = false;
                            if (n.behind)
                            {
                                if (aPosition >= n.width)
                                {
                                    auto const target = aPosition;
                                    auto const check = [&](std::size_t p) { return p == target; };
                                    found = match(*n.children[0], aPosition - n.width, make_continuation(check));
                                }
                            }
                            else
                            {
                                auto const accept = [](std::size_t) { return true; };
                                found = match(*n.children[0], aPosition, make_continuation(accept));
                            }
                            if (found == n.negative)
                            {
                                marks = saved;
                                lastindex = savedLast;
                                return false;
                            }
                            if (n.negative)
                            {
                                marks = saved;
                                lastindex = savedLast;
                            }
                            if (k(aPosition))
                                return true;
                            marks = saved;
                            lastindex = savedLast;
                            return false;
                        }
                    case node::Atomic:
                        {
                            auto const saved = marks;
                            auto const savedLast = lastindex;
                            std::size_t found = 0u;
                            auto const take = [&](std::size_t p) { found = p; return true; };
                            if (!match(*n.children[0], aPosition, make_continuation(take)))
                                return false;
                            if (k(found))
                                return true;
                            marks = saved;
                            lastindex = savedLast;
                            return false;
                        }
                    case node::Conditional:
                        {
                            auto const g = static_cast<std::size_t>(n.group);
                            bool const matched = marks[2u * g] >= 0 && marks[2u * g + 1u] >= 0;
                            return match(*n.children[matched ? 0u : 1u], aPosition, k);
                        }
                    }
                    return false;
                }
            };

            // (a match at aStart (aMustAdvance: not an empty match at aStart): the end, and the marks)
            bool match_at(program const& aProgram, std::u32string const& aText, std::size_t aStart, std::size_t aEnd, bool aFull, bool aMustAdvance,
                std::vector<long long>& aMarks, int& aLastIndex, std::size_t& aMatchEnd)
            {
                matcher m{ aText, aEnd, std::vector<long long>(2u * (aProgram.groups + 1u), -1) };
                auto const accept = [&](std::size_t p)
                {
                    if (aFull && p != aEnd)
                        return false;
                    if (aMustAdvance && p == aStart)
                        return false;
                    aMatchEnd = p;
                    return true;
                };
                if (!m.match(*aProgram.root, aStart, make_continuation(accept)))
                    return false;
                m.marks[0] = static_cast<long long>(aStart);
                m.marks[1] = static_cast<long long>(aMatchEnd);
                aMarks = std::move(m.marks);
                aLastIndex = m.lastindex;
                return true;
            }

            // --- Pattern and Match objects

            struct pattern_object
            {
                header h;
                object* source;                     ///< (the pattern (str))
                std::shared_ptr<program> compiled;
            };

            struct match_object
            {
                header h;
                object* pattern;
                object* string;
                std::shared_ptr<std::u32string> text;
                std::vector<long long> marks;
                int lastindex;
                long long pos;
                long long endpos;
                ~match_object() { release(pattern); release(string); }
            };

            neos_py_type make_pattern_type();
            neos_py_type make_match_type();
            neos_py_type sPatternType = make_pattern_type();
            neos_py_type sMatchType = make_match_type();
            pattern_object& as_pattern(object* aObject) { return *reinterpret_cast<pattern_object*>(aObject); }
            match_object& as_match(object* aObject) { return *reinterpret_cast<match_object*>(aObject); }
            bool is_pattern(object* aObject) { return aObject != nullptr && aObject->type == &sPatternType.base; }
            bool is_match(object* aObject) { return aObject != nullptr && aObject->type == &sMatchType.base; }

            object* borrowed(object* aObject) { retain(aObject); return aObject; }

            object* raise_pattern_error(pattern_error const& e, object* aPattern)
            {
                // (re.error(msg, pattern, pos): its str is "msg at position pos")
                std::string message = e.position == SIZE_MAX ? e.message : e.message + " at position " + std::to_string(e.position);
                ref const exception{ make_exception("re.PatternError", { make_str(message) }) };
                ref const msg{ make_str(e.message) };
                setattr(exception.get(), "msg", msg.get());
                setattr(exception.get(), "pattern", aPattern);
                ref const pos{ e.position == SIZE_MAX ? borrowed(none()) : make_int(static_cast<long long>(e.position)) };
                setattr(exception.get(), "pos", pos.get());
                fn_raise(exception.get());
                return nullptr;
            }

            object* compile(object* aPattern, int aFlags)
            {
                if (is_pattern(aPattern))
                {
                    if (aFlags != 0)
                        return error("ValueError", "cannot process flags argument with a compiled pattern");
                    return borrowed(aPattern);
                }
                std::string_view bytes;
                if (bytes_like(aPattern, bytes))
                    return error("TypeError", "bytes patterns are not supported");
                if (kind_of(aPattern) != NEOS_PY_STR)
                    return error("TypeError", "first argument must be string or compiled pattern");
                // (a cache of the compiled patterns)
                static std::map<std::pair<std::string, int>, object*> sCache;
                auto const key = std::make_pair(as<str_object>(aPattern)->value, aFlags);
                if (auto const cached = sCache.find(key); cached != sCache.end())
                    return borrowed(cached->second);
                if ((aFlags & FlagAscii) != 0 && (aFlags & FlagUnicode) != 0)
                    return error("ValueError", "ASCII and UNICODE flags are incompatible");
                auto const text = to_code_points(as<str_object>(aPattern)->value);
                std::shared_ptr<program> compiled;
                try
                {
                    parser p{ text, aFlags };
                    compiled = p.parse();
                }
                catch (pattern_error const& e)
                {
                    return raise_pattern_error(e, aPattern);
                }
                if ((compiled->flags & FlagAscii) == 0)
                    compiled->flags |= FlagUnicode;
                auto const result = new pattern_object{ { { &sPatternType.base } }, borrowed(aPattern), compiled };
                if (sCache.size() >= 512u)
                {
                    for (auto& c : sCache)
                        release(c.second);
                    sCache.clear();
                }
                sCache[key] = borrowed(&result->h.base);
                return &result->h.base;
            }

            object* make_match(object* aPattern, object* aString, std::shared_ptr<std::u32string> aText, std::vector<long long> aMarks, int aLastIndex,
                long long aPos, long long aEndpos)
            {
                return &(new match_object{ { { &sMatchType.base } }, borrowed(aPattern), borrowed(aString), std::move(aText), std::move(aMarks), aLastIndex,
                    aPos, aEndpos })->h.base;
            }

            // (a search or match from aPos (aMode 0: match, 1: fullmatch, 2: search); null without an exception: no match)
            object* run(object* aPattern, object* aString, std::shared_ptr<std::u32string> const& aText, long long aPos, long long aEndpos, int aMode,
                bool aMustAdvance = false)
            {
                auto const& p = *as_pattern(aPattern).compiled;
                auto const n = static_cast<long long>(aText->size());
                auto const end = static_cast<std::size_t>(std::clamp(aEndpos, 0ll, n));
                auto const start = static_cast<std::size_t>(std::clamp(aPos, 0ll, n));
                if (start > end)
                    return nullptr;
                std::vector<long long> marks;
                int lastindex = -1;
                std::size_t matchEnd = 0u;
                for (auto s = start; s <= end; ++s)
                {
                    if (match_at(p, *aText, s, end, aMode == 1, aMustAdvance && s == start, marks, lastindex, matchEnd))
                        return make_match(aPattern, aString, aText, std::move(marks), lastindex, aPos < 0 ? 0 : aPos, static_cast<long long>(end));
                    if (aMode != 2)
                        break;
                }
                return nullptr;
            }

            // (a match's group by its index or name: -1 if there is none (an exception))
            long long group_index(match_object const& m, object* aGroup)
            {
                auto const& p = *as_pattern(m.pattern).compiled;
                if (kind_of(aGroup) == NEOS_PY_STR)
                {
                    for (auto const& n : p.names)
                        if (n.first == as<str_object>(aGroup)->value)
                            return n.second;
                }
                else if (is_int_like(aGroup))
                {
                    long long i = 0;
                    if (to_index(aGroup, i) && i >= 0 && static_cast<std::size_t>(i) <= p.groups)
                        return i;
                    clear_error();
                }
                error("IndexError", "no such group");
                return -1;
            }

            object* group_value(match_object const& m, long long aIndex, object* aDefault)
            {
                auto const start = m.marks[2u * static_cast<std::size_t>(aIndex)];
                auto const end = m.marks[2u * static_cast<std::size_t>(aIndex) + 1u];
                if (start < 0 || end < 0)
                    return borrowed(aDefault != nullptr ? aDefault : none());
                return make_str(from_code_points(std::u32string_view{ *m.text }.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start))));
            }

            // (a replacement template's expansion: \1, \g<name>, \g<1>, and escapes)
            bool expand_template(match_object const& m, std::u32string const& aTemplate, std::string& aResult)
            {
                auto const& p = *as_pattern(m.pattern).compiled;
                auto const append_group = [&](long long g, std::size_t aAt) -> bool
                {
                    if (g < 0 || static_cast<std::size_t>(g) > p.groups)
                    {
                        error("re.PatternError", "invalid group reference " + std::to_string(g) + " at position " + std::to_string(aAt));
                        return false;
                    }
                    auto const start = m.marks[2u * static_cast<std::size_t>(g)];
                    auto const end = m.marks[2u * static_cast<std::size_t>(g) + 1u];
                    if (start >= 0 && end >= 0)
                        aResult += from_code_points(std::u32string_view{ *m.text }.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start)));
                    return true;
                };
                for (std::size_t i = 0u; i < aTemplate.size(); ++i)
                {
                    auto const c = aTemplate[i];
                    if (c != U'\\' || i + 1u >= aTemplate.size())
                    {
                        if (c == U'\\')
                        {
                            error("re.PatternError", "bad escape (end of pattern) at position " + std::to_string(i));
                            return false;
                        }
                        append_code_point(aResult, c);
                        continue;
                    }
                    auto const e = aTemplate[++i];
                    if (e == U'g')
                    {
                        if (i + 1u >= aTemplate.size() || aTemplate[i + 1u] != U'<')
                        {
                            error("re.PatternError", "missing < at position " + std::to_string(i + 1u));
                            return false;
                        }
                        auto const close = aTemplate.find(U'>', i + 2u);
                        if (close == std::u32string::npos)
                        {
                            error("re.PatternError", "missing >, unterminated name at position " + std::to_string(i + 2u));
                            return false;
                        }
                        auto const name = from_code_points(std::u32string_view{ aTemplate }.substr(i + 2u, close - i - 2u));
                        long long g = -1;
                        if (!name.empty() && std::all_of(name.begin(), name.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; }))
                            g = std::stoll(name);
                        else
                        {
                            for (auto const& n : p.names)
                                if (n.first == name)
                                    g = n.second;
                            if (g < 0)
                            {
                                error("IndexError", "unknown group name '" + name + "'");
                                return false;
                            }
                        }
                        if (!append_group(g, i + 2u))
                            return false;
                        i = close;
                        continue;
                    }
                    if (e >= U'0' && e <= U'9')
                    {
                        // (\0, octal escapes, or a group (one or two digits))
                        if (e == U'0' || (i + 2u < aTemplate.size() && aTemplate[i + 1u] >= U'0' && aTemplate[i + 1u] <= U'7' && aTemplate[i + 2u] >= U'0' &&
                            aTemplate[i + 2u] <= U'7' && e <= U'7'))
                        {
                            char32_t value = e - U'0';
                            int digits = 1;
                            while (digits < 3 && i + 1u < aTemplate.size() && aTemplate[i + 1u] >= U'0' && aTemplate[i + 1u] <= U'7')
                                value = value * 8u + (aTemplate[++i] - U'0'), ++digits;
                            append_code_point(aResult, value);
                            continue;
                        }
                        long long g = e - U'0';
                        auto const at = i;
                        if (i + 1u < aTemplate.size() && aTemplate[i + 1u] >= U'0' && aTemplate[i + 1u] <= U'9')
                            g = g * 10 + (aTemplate[++i] - U'0');
                        if (!append_group(g, at))
                            return false;
                        continue;
                    }
                    switch (e)
                    {
                    case U'n': aResult += '\n'; break;
                    case U't': aResult += '\t'; break;
                    case U'r': aResult += '\r'; break;
                    case U'f': aResult += '\f'; break;
                    case U'v': aResult += '\v'; break;
                    case U'a': aResult += '\a'; break;
                    case U'b': aResult += '\b'; break;
                    case U'\\': aResult += '\\'; break;
                    default:
                        if ((e >= U'a' && e <= U'z') || (e >= U'A' && e <= U'Z'))
                        {
                            std::string text = "\\";
                            append_code_point(text, e);
                            error("re.PatternError", "bad escape " + text + " at position " + std::to_string(i - 1u));
                            return false;
                        }
                        aResult += '\\';
                        append_code_point(aResult, e);
                    }
                }
                return true;
            }

            std::string flags_text(int aFlags)
            {
                static std::pair<int, char const*> const sNames[] = { { FlagTemplate, "re.TEMPLATE" }, { FlagIgnoreCase, "re.IGNORECASE" },
                    { FlagLocale, "re.LOCALE" }, { FlagMultiline, "re.MULTILINE" }, { FlagDotAll, "re.DOTALL" }, { FlagUnicode, "re.UNICODE" },
                    { FlagVerbose, "re.VERBOSE" }, { FlagDebug, "re.DEBUG" }, { FlagAscii, "re.ASCII" } };
                std::string text;
                for (auto const& n : sNames)
                    if ((aFlags & n.first) != 0)
                        text += (text.empty() ? "" : "|") + std::string{ n.second };
                return text;
            }

            neos_py_type make_match_type()
            {
                auto type = make_type("re.Match", "Match", retain_object, release_object<match_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& m = as_match(aSelf);
                    ref const whole{ group_value(m, 0, nullptr) };
                    std::string text;
                    if (!repr_of(whole.get(), text))
                        return nullptr;
                    return make_str("<re.Match object; span=(" + std::to_string(m.marks[0]) + ", " + std::to_string(m.marks[1]) + "), match=" + text + ">");
                };
                type.getitem = [](neos_py_context*, object* aSelf, object* aKey) -> object*
                {
                    auto const& m = as_match(aSelf);
                    auto const g = group_index(m, aKey);
                    return g < 0 ? nullptr : group_value(m, g, nullptr);
                };
                type.truth = [](neos_py_context*, object*) -> int { return 1; };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& m = as_match(aSelf);
                    auto const& p = *as_pattern(m.pattern).compiled;
                    std::string_view const name{ aName };
                    if (name == "string")
                        return borrowed(m.string);
                    if (name == "re")
                        return borrowed(m.pattern);
                    if (name == "pos")
                        return make_int(m.pos);
                    if (name == "endpos")
                        return make_int(m.endpos);
                    if (name == "lastindex")
                        return m.lastindex < 0 ? borrowed(none()) : make_int(m.lastindex);
                    if (name == "lastgroup")
                    {
                        for (auto const& n : p.names)
                            if (n.second == m.lastindex)
                                return make_str(n.first);
                        return borrowed(none());
                    }
                    static char const* const sMethods[] = { "group", "groups", "groupdict", "start", "end", "span", "expand", "__getitem__" };
                    for (std::size_t i = 0u; i < std::size(sMethods); ++i)
                        if (name == sMethods[i])
                            return make_function(sMethods[i], [](neos_py_context*, void* aData, object* aArgs, object* aKwargs) -> object*
                            {
                                auto const& args = as<tuple_object>(aArgs)->items;
                                auto const& m = as_match(args[0]);
                                auto const& p = *as_pattern(m.pattern).compiled;
                                std::vector<object*> const rest{ args.begin() + 1, args.end() };
                                switch (reinterpret_cast<std::uintptr_t>(aData))
                                {
                                case 0:     // (group(*groups))
                                    {
                                        if (rest.empty())
                                            return group_value(m, 0, nullptr);
                                        std::vector<object*> values;
                                        for (auto g : rest)
                                        {
                                            auto const index = group_index(m, g);
                                            if (index < 0)
                                            {
                                                for (auto v : values)
                                                    release(v);
                                                return nullptr;
                                            }
                                            values.push_back(group_value(m, index, nullptr));
                                        }
                                        if (values.size() == 1u)
                                            return values[0];
                                        return make_tuple(std::move(values));
                                    }
                                case 1:     // (groups(default=None))
                                case 2:     // (groupdict(default=None))
                                    {
                                        std::vector<object*> a;
                                        if (!parse_arguments(reinterpret_cast<std::uintptr_t>(aData) == 1u ? "groups" : "groupdict", rest, aKwargs, { "default" }, 0u, 0u, a))
                                            return nullptr;
                                        if (reinterpret_cast<std::uintptr_t>(aData) == 1u)
                                        {
                                            std::vector<object*> values;
                                            for (std::size_t g = 1u; g <= p.groups; ++g)
                                                values.push_back(group_value(m, static_cast<long long>(g), a[0]));
                                            return make_tuple(std::move(values));
                                        }
                                        ref result{ make_dict() };
                                        for (auto const& n : p.names)
                                        {
                                            ref const key{ make_str(n.first) };
                                            ref const value{ group_value(m, n.second, a[0]) };
                                            dict_set(result.get(), key.get(), value.get());
                                        }
                                        return result.release_ownership();
                                    }
                                case 3: case 4: case 5:     // (start, end and span)
                                    {
                                        long long g = 0;
                                        if (rest.size() > 1u)
                                            return error("TypeError", "expected at most 1 argument");
                                        if (!rest.empty())
                                        {
                                            g = group_index(m, rest[0]);
                                            if (g < 0)
                                                return nullptr;
                                        }
                                        auto const s = m.marks[2u * static_cast<std::size_t>(g)];
                                        auto const e = m.marks[2u * static_cast<std::size_t>(g) + 1u];
                                        auto const which = reinterpret_cast<std::uintptr_t>(aData);
                                        if (which == 3u)
                                            return make_int(s < 0 || e < 0 ? -1 : s);
                                        if (which == 4u)
                                            return make_int(s < 0 || e < 0 ? -1 : e);
                                        return make_tuple({ make_int(s < 0 || e < 0 ? -1 : s), make_int(s < 0 || e < 0 ? -1 : e) });
                                    }
                                case 6:     // (expand(template))
                                    {
                                        if (rest.size() != 1u || kind_of(rest[0]) != NEOS_PY_STR)
                                            return error("TypeError", "expand() argument must be str");
                                        std::string result;
                                        if (!expand_template(m, to_code_points(as<str_object>(rest[0])->value), result))
                                            return nullptr;
                                        return make_str(result);
                                    }
                                default:
                                    {
                                        if (rest.size() != 1u)
                                            return error("TypeError", "expected 1 argument");
                                        auto const g = group_index(m, rest[0]);
                                        return g < 0 ? nullptr : group_value(m, g, nullptr);
                                    }
                                }
                            }, reinterpret_cast<void*>(i), aSelf);
                    return error("AttributeError", std::string{ "'re.Match' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            // (the string argument: str (a pattern's searches))
            bool text_argument(object* aString, std::shared_ptr<std::u32string>& aText)
            {
                if (kind_of(aString) != NEOS_PY_STR)
                {
                    std::string_view bytes;
                    if (bytes_like(aString, bytes))
                        error("TypeError", "cannot use a string pattern on a bytes-like object");
                    else
                        error("TypeError", std::string{ "expected string or bytes-like object, got '" } + type_name(aString) + "'");
                    return false;
                }
                aText = std::make_shared<std::u32string>(to_code_points(as<str_object>(aString)->value));
                return true;
            }

            // (a pattern's matches (findall, finditer, sub and split): CPython's rules for empty matches)
            template <typename F>
            bool each_match(object* aPattern, object* aString, std::shared_ptr<std::u32string> const& aText, long long aPos, long long aEndpos,
                long long aCount, F&& aFunction)
            {
                auto pos = aPos;
                bool mustAdvance = false;
                long long n = 0;
                auto const size = static_cast<long long>(aText->size());
                auto const end = std::clamp(aEndpos, 0ll, size);
                while (pos <= end && (aCount <= 0 || n < aCount))
                {
                    ref const m{ run(aPattern, aString, aText, pos, aEndpos, 2, mustAdvance) };
                    if (!m)
                        return !failed();
                    ++n;
                    if (!aFunction(m.get()))
                        return false;
                    auto const& mm = as_match(m.get());
                    mustAdvance = mm.marks[1] == mm.marks[0];
                    pos = mm.marks[1];
                }
                return true;
            }

            struct pattern_arguments
            {
                object* pattern;
                object* string;
                std::shared_ptr<std::u32string> text;
                long long pos = 0;
                long long endpos = 0;
            };

            bool search_arguments(char const* aName, object* aSelf, std::vector<object*> const& aArgs, object* aKwargs, pattern_arguments& aResult)
            {
                std::vector<object*> a;
                if (!parse_arguments(aName, aArgs, aKwargs, { "string", "pos", "endpos" }, 1u, 0u, a))
                    return false;
                aResult.pattern = aSelf;
                aResult.string = a[0];
                if (!text_argument(a[0], aResult.text))
                    return false;
                auto const n = static_cast<long long>(aResult.text->size());
                aResult.endpos = n;
                if ((a[1] != nullptr && !to_index(a[1], aResult.pos)) || (a[2] != nullptr && !to_index(a[2], aResult.endpos)))
                    return false;
                if (aResult.pos < 0)
                    aResult.pos = 0;
                aResult.pos = std::min(aResult.pos, n);
                aResult.endpos = std::clamp(aResult.endpos, 0ll, n);
                return true;
            }

            object* findall(object* aPattern, object* aString, std::shared_ptr<std::u32string> const& aText, long long aPos, long long aEndpos)
            {
                auto const groups = as_pattern(aPattern).compiled->groups;
                ref result{ make_list() };
                bool const ok = each_match(aPattern, aString, aText, aPos, aEndpos, 0, [&](object* m) -> bool
                {
                    auto const& mm = as_match(m);
                    object* item = nullptr;
                    if (groups == 0u)
                        item = group_value(mm, 0, nullptr);
                    else if (groups == 1u)
                    {
                        ref const empty{ make_str("") };
                        item = group_value(mm, 1, empty.get());
                    }
                    else
                    {
                        ref const empty{ make_str("") };
                        std::vector<object*> values;
                        for (std::size_t g = 1u; g <= groups; ++g)
                            values.push_back(group_value(mm, static_cast<long long>(g), empty.get()));
                        item = make_tuple(std::move(values));
                    }
                    as<list_object>(result.get())->items.push_back(item);
                    return true;
                });
                return ok ? result.release_ownership() : nullptr;
            }

            object* finditer(object* aPattern, object* aString, std::shared_ptr<std::u32string> const& aText, long long aPos, long long aEndpos)
            {
                ref result{ make_list() };
                bool const ok = each_match(aPattern, aString, aText, aPos, aEndpos, 0, [&](object* m) -> bool
                {
                    as<list_object>(result.get())->items.push_back(borrowed(m));
                    return true;
                });
                return ok ? iter(result.get()) : nullptr;
            }

            object* subn(object* aPattern, object* aRepl, object* aString, long long aCount, bool aWithCount)
            {
                std::shared_ptr<std::u32string> text;
                if (!text_argument(aString, text))
                    return nullptr;
                std::u32string templateText;
                bool literal = false;
                bool const callable = kind_of(aRepl) != NEOS_PY_STR;
                if (!callable)
                {
                    templateText = to_code_points(as<str_object>(aRepl)->value);
                    literal = templateText.find(U'\\') == std::u32string::npos;
                }
                else
                {
                    std::string_view bytes;
                    if (bytes_like(aRepl, bytes))
                        return error("TypeError", "expected str instance, bytes found");
                }
                std::string result;
                std::size_t last = 0u;
                long long n = 0;
                bool const ok = each_match(aPattern, aString, text, 0, static_cast<long long>(text->size()), aCount, [&](object* m) -> bool
                {
                    auto const& mm = as_match(m);
                    result += from_code_points(std::u32string_view{ *text }.substr(last, static_cast<std::size_t>(mm.marks[0]) - last));
                    if (callable)
                    {
                        ref const args{ make_tuple({ borrowed(m) }) };
                        ref const replacement{ call(aRepl, args.get(), nullptr) };
                        if (!replacement)
                            return false;
                        if (kind_of(replacement.get()) != NEOS_PY_STR)
                        {
                            error("TypeError", std::string{ "expected str instance, " } + type_name(replacement.get()) + " found");
                            return false;
                        }
                        result += as<str_object>(replacement.get())->value;
                    }
                    else if (literal)
                        result += as<str_object>(aRepl)->value;
                    else if (!expand_template(mm, templateText, result))
                        return false;
                    last = static_cast<std::size_t>(mm.marks[1]);
                    ++n;
                    return true;
                });
                if (!ok)
                    return nullptr;
                result += from_code_points(std::u32string_view{ *text }.substr(last));
                if (aWithCount)
                    return make_tuple({ make_str(result), make_int(n) });
                return make_str(result);
            }

            object* split(object* aPattern, object* aString, long long aMaxsplit)
            {
                std::shared_ptr<std::u32string> text;
                if (!text_argument(aString, text))
                    return nullptr;
                auto const groups = as_pattern(aPattern).compiled->groups;
                ref result{ make_list() };
                auto& items = as<list_object>(result.get())->items;
                std::size_t last = 0u;
                bool const ok = each_match(aPattern, aString, text, 0, static_cast<long long>(text->size()), aMaxsplit, [&](object* m) -> bool
                {
                    auto const& mm = as_match(m);
                    items.push_back(make_str(from_code_points(std::u32string_view{ *text }.substr(last, static_cast<std::size_t>(mm.marks[0]) - last))));
                    for (std::size_t g = 1u; g <= groups; ++g)
                        items.push_back(group_value(mm, static_cast<long long>(g), nullptr));
                    last = static_cast<std::size_t>(mm.marks[1]);
                    return true;
                });
                if (!ok)
                    return nullptr;
                items.push_back(make_str(from_code_points(std::u32string_view{ *text }.substr(last))));
                return result.release_ownership();
            }

            neos_py_type make_pattern_type()
            {
                auto type = make_type("re.Pattern", "Pattern", retain_object, release_object<pattern_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& p = as_pattern(aSelf);
                    std::string text;
                    if (!repr_of(p.source, text))
                        return nullptr;
                    auto const flags = p.compiled->flags & ~FlagUnicode;
                    if (flags == 0)
                        return make_str("re.compile(" + text + ")");
                    return make_str("re.compile(" + text + ", " + flags_text(flags) + ")");
                };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    if ((aOperation != NEOS_PY_EQ && aOperation != NEOS_PY_NE) || !is_pattern(aLhs) || !is_pattern(aRhs))
                        return borrowed(not_implemented());
                    auto const e = detail::equal(as_pattern(aLhs).source, as_pattern(aRhs).source);
                    if (e < 0)
                        return nullptr;
                    bool const same = e == 1 && as_pattern(aLhs).compiled->flags == as_pattern(aRhs).compiled->flags;
                    return make_bool(aOperation == NEOS_PY_EQ ? same : !same);
                };
                type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int
                {
                    return hash_of(as_pattern(aSelf).source, *aResult) ? 0 : -1;
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& p = as_pattern(aSelf);
                    std::string_view const name{ aName };
                    if (name == "pattern")
                        return borrowed(p.source);
                    if (name == "flags")
                        return make_int(p.compiled->flags);
                    if (name == "groups")
                        return make_int(static_cast<long long>(p.compiled->groups));
                    if (name == "groupindex")
                    {
                        ref result{ make_dict() };
                        for (auto const& n : p.compiled->names)
                        {
                            ref const key{ make_str(n.first) };
                            ref const value{ make_int(n.second) };
                            dict_set(result.get(), key.get(), value.get());
                        }
                        return result.release_ownership();
                    }
                    static char const* const sMethods[] = { "match", "fullmatch", "search", "findall", "finditer", "sub", "subn", "split" };
                    for (std::size_t i = 0u; i < std::size(sMethods); ++i)
                        if (name == sMethods[i])
                            return make_function(sMethods[i], [](neos_py_context*, void* aData, object* aArgs, object* aKwargs) -> object*
                            {
                                auto const& args = as<tuple_object>(aArgs)->items;
                                auto const self = args[0];
                                std::vector<object*> const rest{ args.begin() + 1, args.end() };
                                auto const which = reinterpret_cast<std::uintptr_t>(aData);
                                static char const* const sNames[] = { "match", "fullmatch", "search", "findall", "finditer", "sub", "subn", "split" };
                                if (which <= 4u)
                                {
                                    pattern_arguments a;
                                    if (!search_arguments(sNames[which], self, rest, aKwargs, a))
                                        return nullptr;
                                    if (which == 3u)
                                        return findall(self, a.string, a.text, a.pos, a.endpos);
                                    if (which == 4u)
                                        return finditer(self, a.string, a.text, a.pos, a.endpos);
                                    auto const r = run(self, a.string, a.text, a.pos, a.endpos, which == 0u ? 0 : which == 1u ? 1 : 2);
                                    return r != nullptr || failed() ? r : borrowed(none());
                                }
                                if (which <= 6u)
                                {
                                    std::vector<object*> a;
                                    if (!parse_arguments(sNames[which], rest, aKwargs, { "repl", "string", "count" }, 2u, 0u, a))
                                        return nullptr;
                                    long long count = 0;
                                    if (a[2] != nullptr && !to_index(a[2], count))
                                        return nullptr;
                                    return subn(self, a[0], a[1], count, which == 6u);
                                }
                                std::vector<object*> a;
                                if (!parse_arguments("split", rest, aKwargs, { "string", "maxsplit" }, 1u, 0u, a))
                                    return nullptr;
                                long long maxsplit = 0;
                                if (a[1] != nullptr && !to_index(a[1], maxsplit))
                                    return nullptr;
                                return split(self, a[0], maxsplit);
                            }, reinterpret_cast<void*>(i), aSelf);
                    return error("AttributeError", std::string{ "'re.Pattern' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            // --- the module's functions

            bool flags_argument(object* aFlags, int& aResult)
            {
                aResult = 0;
                if (aFlags == nullptr)
                    return true;
                long long f = 0;
                if (!to_index(aFlags, f))
                    return false;
                aResult = static_cast<int>(f);
                return true;
            }

            object* module_search(std::vector<object*> const& aArgs, object* aKwargs, char const* aName, int aMode)
            {
                std::vector<object*> a;
                if (!parse_arguments(aName, aArgs, aKwargs, { "pattern", "string", "flags" }, 2u, 0u, a))
                    return nullptr;
                int flags = 0;
                if (!flags_argument(a[2], flags))
                    return nullptr;
                ref const pattern{ compile(a[0], flags) };
                if (!pattern)
                    return nullptr;
                std::shared_ptr<std::u32string> text;
                if (!text_argument(a[1], text))
                    return nullptr;
                auto const n = static_cast<long long>(text->size());
                if (aMode == 3)
                    return findall(pattern.get(), a[1], text, 0, n);
                if (aMode == 4)
                    return finditer(pattern.get(), a[1], text, 0, n);
                auto const r = run(pattern.get(), a[1], text, 0, n, aMode);
                return r != nullptr || failed() ? r : borrowed(none());
            }

            builtin_entry const sReFunctions[] =
            {
                { "compile", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("compile", aArgs, aKwargs, { "pattern", "flags" }, 1u, 0u, a))
                            return nullptr;
                        int flags = 0;
                        return flags_argument(a[1], flags) ? compile(a[0], flags) : nullptr;
                    } },
                { "match", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return module_search(aArgs, aKwargs, "match", 0); } },
                { "fullmatch", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return module_search(aArgs, aKwargs, "fullmatch", 1); } },
                { "search", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return module_search(aArgs, aKwargs, "search", 2); } },
                { "findall", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return module_search(aArgs, aKwargs, "findall", 3); } },
                { "finditer", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return module_search(aArgs, aKwargs, "finditer", 4); } },
                { "sub", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("sub", aArgs, aKwargs, { "pattern", "repl", "string", "count", "flags" }, 3u, 0u, a))
                            return nullptr;
                        int flags = 0;
                        long long count = 0;
                        if (!flags_argument(a[4], flags) || (a[3] != nullptr && !to_index(a[3], count)))
                            return nullptr;
                        ref const pattern{ compile(a[0], flags) };
                        return pattern ? subn(pattern.get(), a[1], a[2], count, false) : nullptr;
                    } },
                { "subn", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("subn", aArgs, aKwargs, { "pattern", "repl", "string", "count", "flags" }, 3u, 0u, a))
                            return nullptr;
                        int flags = 0;
                        long long count = 0;
                        if (!flags_argument(a[4], flags) || (a[3] != nullptr && !to_index(a[3], count)))
                            return nullptr;
                        ref const pattern{ compile(a[0], flags) };
                        return pattern ? subn(pattern.get(), a[1], a[2], count, true) : nullptr;
                    } },
                { "split", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("split", aArgs, aKwargs, { "pattern", "string", "maxsplit", "flags" }, 2u, 0u, a))
                            return nullptr;
                        int flags = 0;
                        long long maxsplit = 0;
                        if (!flags_argument(a[3], flags) || (a[2] != nullptr && !to_index(a[2], maxsplit)))
                            return nullptr;
                        ref const pattern{ compile(a[0], flags) };
                        return pattern ? split(pattern.get(), a[1], maxsplit) : nullptr;
                    } },
                { "escape", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("escape", aArgs, aKwargs, { "pattern" }, 1u, 0u, a))
                            return nullptr;
                        if (kind_of(a[0]) != NEOS_PY_STR)
                            return error("TypeError", "escape() argument must be str");
                        static std::string const sSpecial = "()[]{}?*+-|^$\\.&~# \t\n\r\v\f";
                        std::string result;
                        for (char c : as<str_object>(a[0])->value)
                        {
                            if (sSpecial.find(c) != std::string::npos)
                                result += '\\';
                            result += c;
                        }
                        return make_str(result);
                    } },
                { "purge", [](std::vector<object*> const&, object*) -> object* { return borrowed(none()); } }
            };

            int pattern_instancecheck(neos_py_context*, void* aData, object* aObject) { return (aData == nullptr ? is_pattern(aObject) : is_match(aObject)) ? 1 : 0; }

            bool re_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sReFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                static std::pair<char const*, int> const sFlags[] = { { "TEMPLATE", FlagTemplate }, { "T", FlagTemplate }, { "IGNORECASE", FlagIgnoreCase },
                    { "I", FlagIgnoreCase }, { "LOCALE", FlagLocale }, { "L", FlagLocale }, { "MULTILINE", FlagMultiline }, { "M", FlagMultiline },
                    { "DOTALL", FlagDotAll }, { "S", FlagDotAll }, { "UNICODE", FlagUnicode }, { "U", FlagUnicode }, { "VERBOSE", FlagVerbose },
                    { "X", FlagVerbose }, { "DEBUG", FlagDebug }, { "ASCII", FlagAscii }, { "A", FlagAscii }, { "NOFLAG", 0 } };
                for (auto const& f : sFlags)
                    aAttributes.emplace_back(f.first, make_int(f.second));
                if (!is_exception_class("re.PatternError"))
                    user_exception_bases()["re.PatternError"] = { "Exception" };
                aAttributes.emplace_back("error", exception_class("re.PatternError"));
                aAttributes.emplace_back("PatternError", exception_class("re.PatternError"));
                aAttributes.emplace_back("Pattern", context()->type_new(context(), "Pattern", "re.Pattern",
                    [](neos_py_context*, void*, object*, object*) -> object* { return error("TypeError", "cannot create 're.Pattern' instances"); }, nullptr, pattern_instancecheck));
                aAttributes.emplace_back("Match", context()->type_new(context(), "Match", "re.Match",
                    [](neos_py_context*, void*, object*, object*) -> object* { return error("TypeError", "cannot create 're.Match' instances"); },
                    reinterpret_cast<void*>(1), pattern_instancecheck));
                return true;
            }

            inline bool const sRe = register_module("re", re_module);
        }
    }
}
