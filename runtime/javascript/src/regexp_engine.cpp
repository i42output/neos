/*
  regexp_engine.cpp

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

// The RegExp engine: the pattern parser and the backtracking matcher (ECMA-262 22.2 and Annex B.1.2)
//
// A pattern is parsed (with its early errors) to a tree of nodes, which is compiled to a program of instructions (character
// tests, forks, jumps, capture saves, loops, lookarounds); the matcher runs the program with an explicit backtrack stack that holds
// choice points and the records that undo changes to captures and loop registers, so it does not recurse on the C++ stack.
//
// Characters are UTF-16 code units without the u or v flag and code points with them (the input stays UTF-16: a surrogate pair is
// read as one character). A lookbehind's contents are compiled to match backwards (their sequences reversed, their characters read
// before the position) as the specification's direction parameter does.

#include <algorithm>
#include <array>
#include <span>
#include "unicode.hpp"
#include "regexp_engine.hpp"

namespace neos::javascript_runtime::regexp
{
    namespace
    {
        constexpr char32_t NoChar = 0xFFFFFFFFu;
        constexpr char32_t MaxCodePoint = 0x10FFFFu;
        constexpr std::uint32_t MaxDepth = 500u;                    ///< (the nesting of groups and classes)
        constexpr std::size_t MaxBacktrackStack = 1u << 23u;       ///< (entries)

        using range_list = std::vector<range>;

        inline bool is_high_surrogate(char32_t aChar) { return aChar >= 0xD800u && aChar <= 0xDBFFu; }
        inline bool is_low_surrogate(char32_t aChar) { return aChar >= 0xDC00u && aChar <= 0xDFFFu; }
        inline char32_t combine_surrogates(char32_t aHigh, char32_t aLow) { return 0x10000u + ((aHigh - 0xD800u) << 10u) + (aLow - 0xDC00u); }
        inline bool is_line_terminator(char32_t aChar) { return aChar == 0x0Au || aChar == 0x0Du || aChar == 0x2028u || aChar == 0x2029u; }
        inline bool is_digit(char32_t aChar) { return aChar >= U'0' && aChar <= U'9'; }
        inline bool is_octal_digit(char32_t aChar) { return aChar >= U'0' && aChar <= U'7'; }
        inline bool is_ascii_letter(char32_t aChar) { return (aChar >= U'a' && aChar <= U'z') || (aChar >= U'A' && aChar <= U'Z'); }
        inline bool is_ascii_word(char32_t aChar) { return is_ascii_letter(aChar) || is_digit(aChar) || aChar == U'_'; }
        inline int hex_value(char32_t aChar)
        {
            if (aChar >= U'0' && aChar <= U'9')
                return static_cast<int>(aChar - U'0');
            if (aChar >= U'a' && aChar <= U'f')
                return static_cast<int>(aChar - U'a') + 10;
            if (aChar >= U'A' && aChar <= U'F')
                return static_cast<int>(aChar - U'A') + 10;
            return -1;
        }
        inline bool is_syntax_character(char32_t aChar)
        {
            switch (aChar)
            {
            case U'^': case U'$': case U'\\': case U'.': case U'*': case U'+': case U'?': case U'(': case U')': case U'[': case U']':
            case U'{': case U'}': case U'|':
                return true;
            default:
                return false;
            }
        }
        inline bool is_class_set_syntax_character(char32_t aChar)
        {
            switch (aChar)
            {
            case U'(': case U')': case U'[': case U']': case U'{': case U'}': case U'/': case U'-': case U'\\': case U'|':
                return true;
            default:
                return false;
            }
        }
        inline bool is_class_set_reserved_double_punctuator(char32_t aChar)
        {
            switch (aChar)
            {
            case U'&': case U'!': case U'#': case U'$': case U'%': case U'*': case U'+': case U',': case U'.': case U':': case U';':
            case U'<': case U'=': case U'>': case U'?': case U'@': case U'^': case U'`': case U'~':
                return true;
            default:
                return false;
            }
        }
        inline bool is_class_set_reserved_punctuator(char32_t aChar)
        {
            switch (aChar)
            {
            case U'&': case U'-': case U'!': case U'#': case U'%': case U',': case U':': case U';': case U'<': case U'=': case U'>':
            case U'@': case U'`': case U'~':
                return true;
            default:
                return false;
            }
        }
        inline char16_t lead_surrogate_of(char32_t aCodePoint)
        {
            return static_cast<char16_t>(0xD800u + ((aCodePoint - 0x10000u) >> 10u));
        }
        void append_utf16(std::u16string& aText, char32_t aCodePoint)
        {
            if (aCodePoint >= 0x10000u)
            {
                aText.push_back(lead_surrogate_of(aCodePoint));
                aText.push_back(static_cast<char16_t>(0xDC00u + ((aCodePoint - 0x10000u) & 0x3FFu)));
            }
            else
                aText.push_back(static_cast<char16_t>(aCodePoint));
        }

        // --- sets of characters (sorted, disjoint, non-adjacent ranges)

        void normalize(range_list& aRanges)
        {
            if (aRanges.size() < 2u)
                return;
            std::sort(aRanges.begin(), aRanges.end(), [](range const& a, range const& b) { return a.first < b.first; });
            std::size_t out = 0u;
            for (std::size_t i = 1u; i < aRanges.size(); ++i)
            {
                if (aRanges[i].first <= aRanges[out].last + 1u)
                    aRanges[out].last = std::max(aRanges[out].last, aRanges[i].last);
                else
                    aRanges[++out] = aRanges[i];
            }
            aRanges.resize(out + 1u);
        }

        bool contains(range_list const& aRanges, char32_t aChar)
        {
            auto const it = std::upper_bound(aRanges.begin(), aRanges.end(), aChar, [](char32_t c, range const& r) { return c < r.first; });
            return it != aRanges.begin() && aChar <= std::prev(it)->last;
        }

        range_list complement_of(range_list const& aRanges, char32_t aMax)
        {
            range_list result;
            char32_t next = 0u;
            for (auto const& r : aRanges)
            {
                if (r.first > aMax)
                    break;
                if (r.first > next)
                    result.push_back({ next, r.first - 1u });
                next = r.last + 1u;
            }
            if (next <= aMax)
                result.push_back({ next, aMax });
            return result;
        }

        range_list intersection_of(range_list const& aLhs, range_list const& aRhs)
        {
            range_list result;
            std::size_t i = 0u;
            std::size_t j = 0u;
            while (i < aLhs.size() && j < aRhs.size())
            {
                auto const first = std::max(aLhs[i].first, aRhs[j].first);
                auto const last = std::min(aLhs[i].last, aRhs[j].last);
                if (first <= last)
                    result.push_back({ first, last });
                if (aLhs[i].last < aRhs[j].last)
                    ++i;
                else
                    ++j;
            }
            return result;
        }

        range_list union_of(range_list const& aLhs, range_list const& aRhs)
        {
            range_list result = aLhs;
            result.insert(result.end(), aRhs.begin(), aRhs.end());
            normalize(result);
            return result;
        }

        range_list difference_of(range_list const& aLhs, range_list const& aRhs)
        {
            return intersection_of(aLhs, complement_of(aRhs, MaxCodePoint));
        }

        // (a CharSet: characters (as ranges) and, with the v flag, strings (of other than one character))
        struct char_set
        {
            range_list ranges;
            std::vector<std::u32string> strings;    ///< (sorted, unique)
        };

        void add_string(char_set& aSet, std::u32string aString)
        {
            if (aString.size() == 1u)
            {
                aSet.ranges.push_back({ aString[0], aString[0] });
                normalize(aSet.ranges);
                return;
            }
            auto const it = std::lower_bound(aSet.strings.begin(), aSet.strings.end(), aString);
            if (it == aSet.strings.end() || *it != aString)
                aSet.strings.insert(it, std::move(aString));
        }

        char_set set_union(char_set const& aLhs, char_set const& aRhs)
        {
            char_set result;
            result.ranges = union_of(aLhs.ranges, aRhs.ranges);
            std::set_union(aLhs.strings.begin(), aLhs.strings.end(), aRhs.strings.begin(), aRhs.strings.end(), std::back_inserter(result.strings));
            return result;
        }

        char_set set_intersection(char_set const& aLhs, char_set const& aRhs)
        {
            char_set result;
            result.ranges = intersection_of(aLhs.ranges, aRhs.ranges);
            std::set_intersection(aLhs.strings.begin(), aLhs.strings.end(), aRhs.strings.begin(), aRhs.strings.end(), std::back_inserter(result.strings));
            return result;
        }

        char_set set_difference(char_set const& aLhs, char_set const& aRhs)
        {
            char_set result;
            result.ranges = difference_of(aLhs.ranges, aRhs.ranges);
            std::set_difference(aLhs.strings.begin(), aLhs.strings.end(), aRhs.strings.begin(), aRhs.strings.end(), std::back_inserter(result.strings));
            return result;
        }

        // --- case-insensitive matching: Canonicalize

        // (without the u or v flag: toUppercase (if it is one code unit and does not map a non-ASCII character to an ASCII one):
        // unicode::canonicalize, tabulated by code unit)
        struct legacy_case_data
        {
            std::vector<char16_t> canonical;    ///< (by code unit)
            std::vector<char32_t> changing;     ///< (the code units that it maps to others)
            std::vector<char32_t> targets;      ///< (what they map to (sorted, unique))
        };

        legacy_case_data const& legacy_case()
        {
            static legacy_case_data const sData = []()
            {
                legacy_case_data data;
                data.canonical.resize(0x10000u);
                for (char32_t c = 0u; c < 0x10000u; ++c)
                {
                    data.canonical[c] = static_cast<char16_t>(c);
                    if (c >= 0xD800u && c <= 0xDFFFu)
                        continue;
                    auto const upper = unicode::canonicalize(c, false);
                    if (upper == c || upper > 0xFFFFu)
                        continue;
                    data.canonical[c] = static_cast<char16_t>(upper);
                    data.changing.push_back(c);
                    data.targets.push_back(upper);
                }
                std::sort(data.targets.begin(), data.targets.end());
                data.targets.erase(std::unique(data.targets.begin(), data.targets.end()), data.targets.end());
                return data;
            }();
            return sData;
        }

        // (with the u or v flag: simple case folding)
        struct unicode_case_data
        {
            std::vector<char32_t> changing;     ///< (the code points that simple case folding maps to others)
            std::vector<char32_t> targets;
            range_list extraWordCharacters;     ///< (WordCharacters' extra characters with u or v and i)
        };

        inline char32_t simple_fold(char32_t aChar)
        {
            if (aChar < 128u)
                return aChar >= U'A' && aChar <= U'Z' ? aChar + 32u : aChar;
            return unicode::canonicalize(aChar, true);
        }

        unicode_case_data const& unicode_case()
        {
            static unicode_case_data const sData = []()
            {
                unicode_case_data data;
                for (char32_t c = 0u; c <= MaxCodePoint; ++c)
                {
                    auto const folded = simple_fold(c);
                    if (folded != c)
                    {
                        data.changing.push_back(c);
                        data.targets.push_back(folded);
                        if (is_ascii_word(folded) && !is_ascii_word(c))
                            data.extraWordCharacters.push_back({ c, c });
                    }
                }
                std::sort(data.targets.begin(), data.targets.end());
                data.targets.erase(std::unique(data.targets.begin(), data.targets.end()), data.targets.end());
                normalize(data.extraWordCharacters);
                return data;
            }();
            return sData;
        }

        inline char32_t canonicalize(char32_t aChar, bool aUnicode)
        {
            if (aUnicode)
                return simple_fold(aChar);
            return aChar < 0x10000u ? legacy_case().canonical[aChar] : aChar;
        }

        std::vector<char32_t> const& changing_characters(bool aUnicode)
        {
            return aUnicode ? unicode_case().changing : legacy_case().changing;
        }

        // ({ Canonicalize(c) : c in the set } and the set itself (the characters that a case-insensitive CharacterSetMatcher tests
        // against the canonicalized input: the set's non-canonical characters are never a canonicalized input))
        range_list canonical_closure(range_list const& aRanges, bool aUnicode)
        {
            auto const& changing = changing_characters(aUnicode);
            range_list result = aRanges;
            for (auto const& r : aRanges)
                for (auto it = std::lower_bound(changing.begin(), changing.end(), r.first); it != changing.end() && *it <= r.last; ++it)
                {
                    auto const c = canonicalize(*it, aUnicode);
                    result.push_back({ c, c });
                }
            normalize(result);
            return result;
        }

        // (MaybeSimpleCaseFolding (the v and i flags))
        char_set simple_case_folding(char_set const& aSet)
        {
            auto const& changing = unicode_case().changing;
            char_set result;
            range_list changed;
            for (auto const& r : aSet.ranges)
                for (auto it = std::lower_bound(changing.begin(), changing.end(), r.first); it != changing.end() && *it <= r.last; ++it)
                {
                    changed.push_back({ *it, *it });
                    auto const c = simple_fold(*it);
                    result.ranges.push_back({ c, c });
                }
            normalize(changed);
            auto kept = difference_of(aSet.ranges, changed);
            result.ranges.insert(result.ranges.end(), kept.begin(), kept.end());
            normalize(result.ranges);
            for (auto const& s : aSet.strings)
            {
                std::u32string folded;
                for (auto const c : s)
                    folded.push_back(simple_fold(c));
                result.strings.push_back(std::move(folded));
            }
            std::sort(result.strings.begin(), result.strings.end());
            result.strings.erase(std::unique(result.strings.begin(), result.strings.end()), result.strings.end());
            return result;
        }

        range_list const& white_space_characters()
        {
            static range_list const sSet = []()
            {
                range_list result;
                for (char32_t c = 0u; c < 0x10000u; ++c)
                    if (is_line_terminator(c) || unicode::white_space(c))
                        result.push_back({ c, c });
                normalize(result);
                return result;
            }();
            return sSet;
        }

        // --- the parse tree

        enum class node_type : std::uint8_t
        {
            Empty,
            Char,
            Class,
            Dot,
            Sequence,
            Alternation,
            Group,
            Look,
            LineStart,
            LineEnd,
            WordBoundary,
            NotWordBoundary,
            Backreference,
            Repeat
        };

        struct node
        {
            node_type type = node_type::Empty;
            bool ignoreCase = false;
            bool multiline = false;
            bool dotAll = false;
            bool behind = false;                ///< (Look)
            bool negative = false;              ///< (Look)
            bool greedy = true;                 ///< (Repeat)
            char32_t ch = 0u;                   ///< (Char)
            std::uint32_t index = 0u;           ///< (Class: the class; Group: its number (0: not capturing); Backreference: the backreference)
            std::uint32_t min = 0u;             ///< (Repeat)
            std::uint32_t max = 0u;
            std::uint32_t firstGroup = 0u;      ///< (Repeat: the groups within it: firstGroup + 1 ... firstGroup + groupCount)
            std::uint32_t groupCount = 0u;
            std::vector<node*> children;
        };

        struct class_atom
        {
            bool isSet = false;
            char32_t ch = 0u;
            range_list set;
        };

        // --- the parser (Pattern[UnicodeMode, UnicodeSetsMode, NamedCaptureGroups] with Annex B's extensions without u or v)

        class parser
        {
        public:
            parser(std::u16string_view aPattern, std::uint32_t aFlags, program& aProgram) :
                iProgram{ aProgram },
                iUnicode{ (aFlags & (FlagUnicode | FlagUnicodeSets)) != 0u },
                iSets{ (aFlags & FlagUnicodeSets) != 0u },
                iIgnoreCase{ (aFlags & FlagIgnoreCase) != 0u },
                iMultiline{ (aFlags & FlagMultiline) != 0u },
                iDotAll{ (aFlags & FlagDotAll) != 0u }
            {
                iText.reserve(aPattern.size());
                for (std::size_t i = 0u; i < aPattern.size(); ++i)
                {
                    char32_t c = aPattern[i];
                    if (iUnicode && is_high_surrogate(c) && i + 1u < aPattern.size() && is_low_surrogate(aPattern[i + 1u]))
                        c = combine_surrogates(c, aPattern[++i]);
                    iText.push_back(c);
                }
            }
        public:
            node* parse()
            {
                prescan();
                auto* const result = parse_disjunction();
                if (!at_end())
                    error("Unmatched ')'");
                // (named backreferences: the groups of their names)
                for (auto const& r : iPendingReferences)
                {
                    auto& groups = iProgram.backreferences[r.second];
                    for (auto const& g : iNamedGroups)
                        if (g.name == r.first)
                            groups.push_back(g.number);
                    if (groups.empty())
                        error("Invalid named capture referenced");
                    std::sort(groups.begin(), groups.end());
                }
                iProgram.captureCount = iGroups;
                iProgram.groupNames.assign(iGroups + 1u, std::u16string{});
                for (auto const& g : iNamedGroups)
                    iProgram.groupNames[g.number] = g.name;
                iProgram.hasNamedGroups = !iNamedGroups.empty();
                return result;
            }
            bool unicode_mode() const { return iUnicode; }
        private:
            [[noreturn]] void error(char const* aMessage)
            {
                throw syntax_error{ aMessage };
            }
            bool at_end() const { return iPos >= iText.size(); }
            char32_t peek(std::size_t aOffset = 0u) const { return iPos + aOffset < iText.size() ? iText[iPos + aOffset] : NoChar; }
            bool eat(char32_t aChar)
            {
                if (peek() != aChar)
                    return false;
                ++iPos;
                return true;
            }
            node* make(node_type aType)
            {
                iNodes.push_back(std::make_unique<node>());
                auto* const result = iNodes.back().get();
                result->type = aType;
                result->ignoreCase = iIgnoreCase;
                result->multiline = iMultiline;
                result->dotAll = iDotAll;
                return result;
            }
            node* make_char(char32_t aChar)
            {
                auto* const result = make(node_type::Char);
                result->ch = aChar;
                return result;
            }
            // (counts the capturing groups (for decimal escapes) and finds whether there are named groups (Annex B: without u or v the
            // pattern is parsed with [+NamedCaptureGroups] only if it has a GroupName))
            void prescan()
            {
                bool named = false;
                auto const n = iText.size();
                for (std::size_t i = 0u; i < n; ++i)
                {
                    auto const c = iText[i];
                    if (c == U'\\')
                        ++i;
                    else if (c == U'[')
                    {
                        std::size_t depth = 1u;
                        for (++i; i < n; ++i)
                        {
                            if (iText[i] == U'\\')
                                ++i;
                            else if (iText[i] == U'[' && iSets)
                                ++depth;
                            else if (iText[i] == U']' && --depth == 0u)
                                break;
                        }
                    }
                    else if (c == U'(')
                    {
                        if (i + 1u < n && iText[i + 1u] == U'?')
                        {
                            if (i + 3u < n && iText[i + 2u] == U'<' && iText[i + 3u] != U'=' && iText[i + 3u] != U'!')
                            {
                                ++iTotalGroups;
                                named = true;
                            }
                        }
                        else
                            ++iTotalGroups;
                    }
                }
                iNamed = iUnicode || named;
            }
            node* parse_disjunction()
            {
                if (++iDepth > MaxDepth)
                    error("Regular expression too deeply nested");
                iPath.emplace_back(iDisjunctions++, 0u);
                std::vector<node*> alternatives{ parse_alternative() };
                while (eat(U'|'))
                {
                    ++iPath.back().second;
                    alternatives.push_back(parse_alternative());
                }
                iPath.pop_back();
                --iDepth;
                if (alternatives.size() == 1u)
                    return alternatives[0];
                auto* const result = make(node_type::Alternation);
                result->children = std::move(alternatives);
                return result;
            }
            node* parse_alternative()
            {
                std::vector<node*> terms;
                while (!at_end() && peek() != U'|' && peek() != U')')
                    parse_term(terms);
                if (terms.empty())
                    return make(node_type::Empty);
                if (terms.size() == 1u)
                    return terms[0];
                auto* const result = make(node_type::Sequence);
                result->children = std::move(terms);
                return result;
            }
            // (an assertion that cannot be quantified)
            void no_quantifier()
            {
                auto const c = peek();
                if (c == U'*' || c == U'+' || c == U'?' || (c == U'{' && is_braced_quantifier()))
                    error("Nothing to repeat");
            }
            void parse_term(std::vector<node*>& aTerms)
            {
                auto const c = peek();
                auto const groupsBefore = iGroups;
                node* atom = nullptr;
                switch (c)
                {
                case U'^':
                    ++iPos;
                    aTerms.push_back(make(node_type::LineStart));
                    no_quantifier();
                    return;
                case U'$':
                    ++iPos;
                    aTerms.push_back(make(node_type::LineEnd));
                    no_quantifier();
                    return;
                case U'\\':
                    if (peek(1u) == U'b' || peek(1u) == U'B')
                    {
                        iPos += 2u;
                        aTerms.push_back(make(iText[iPos - 1u] == U'b' ? node_type::WordBoundary : node_type::NotWordBoundary));
                        no_quantifier();
                        return;
                    }
                    ++iPos;
                    atom = parse_atom_escape();
                    break;
                case U'(':
                    {
                        bool lookbehind = false;
                        bool lookahead = false;
                        atom = parse_group(lookahead, lookbehind);
                        if (lookbehind || (lookahead && iUnicode))
                        {
                            aTerms.push_back(atom);
                            no_quantifier();
                            return;
                        }
                    }
                    break;
                case U'.':
                    ++iPos;
                    atom = make(node_type::Dot);
                    break;
                case U'[':
                    atom = parse_class();
                    break;
                case U'*':
                case U'+':
                case U'?':
                    error("Nothing to repeat");
                case U'{':
                    if (is_braced_quantifier())
                        error("Nothing to repeat");
                    if (iUnicode)
                        error("Lone quantifier brackets");
                    ++iPos;
                    atom = make_char(c);
                    break;
                case U'}':
                case U']':
                    if (iUnicode)
                        error("Lone quantifier brackets");
                    ++iPos;
                    atom = make_char(c);
                    break;
                default:
                    ++iPos;
                    atom = make_char(c);
                    break;
                }
                aTerms.push_back(parse_quantifier(atom, groupsBefore));
            }
            // (at {: whether it is { DecimalDigits } or { DecimalDigits ,} or { DecimalDigits , DecimalDigits })
            bool is_braced_quantifier() const
            {
                std::size_t i = iPos + 1u;
                auto const digits = [&]()
                {
                    auto const start = i;
                    while (i < iText.size() && is_digit(iText[i]))
                        ++i;
                    return i > start;
                };
                if (!digits())
                    return false;
                if (i < iText.size() && iText[i] == U',')
                {
                    ++i;
                    digits();
                }
                return i < iText.size() && iText[i] == U'}';
            }
            static std::uint32_t decimal_value(std::u32string_view aDigits)
            {
                std::uint64_t result = 0u;
                for (auto const d : aDigits)
                {
                    result = result * 10u + (d - U'0');
                    if (result >= Infinite)
                        return Infinite - 1u;
                }
                return static_cast<std::uint32_t>(result);
            }
            static int compare_decimal(std::u32string_view aLhs, std::u32string_view aRhs)
            {
                while (aLhs.size() > 1u && aLhs[0] == U'0')
                    aLhs.remove_prefix(1u);
                while (aRhs.size() > 1u && aRhs[0] == U'0')
                    aRhs.remove_prefix(1u);
                if (aLhs.size() != aRhs.size())
                    return aLhs.size() < aRhs.size() ? -1 : 1;
                return aLhs.compare(aRhs);
            }
            node* parse_quantifier(node* aAtom, std::uint32_t aGroupsBefore)
            {
                std::uint32_t min = 0u;
                std::uint32_t max = Infinite;
                switch (peek())
                {
                case U'*':
                    ++iPos;
                    break;
                case U'+':
                    ++iPos;
                    min = 1u;
                    break;
                case U'?':
                    ++iPos;
                    max = 1u;
                    break;
                case U'{':
                    {
                        if (!is_braced_quantifier())
                        {
                            if (iUnicode)
                                error("Incomplete quantifier");
                            return aAtom;
                        }
                        ++iPos;
                        auto const start = iPos;
                        while (is_digit(peek()))
                            ++iPos;
                        std::u32string_view const first{ iText.data() + start, iPos - start };
                        min = decimal_value(first);
                        max = min;
                        if (eat(U','))
                        {
                            auto const start2 = iPos;
                            while (is_digit(peek()))
                                ++iPos;
                            if (iPos > start2)
                            {
                                std::u32string_view const second{ iText.data() + start2, iPos - start2 };
                                if (compare_decimal(first, second) > 0)
                                    error("numbers out of order in {} quantifier");
                                std::uint64_t value = 0u;
                                for (auto const d : second)
                                {
                                    value = value * 10u + (d - U'0');
                                    if (value >= Infinite)
                                        break;
                                }
                                max = value >= Infinite ? Infinite : static_cast<std::uint32_t>(value);
                            }
                            else
                                max = Infinite;
                        }
                        ++iPos; // (})
                    }
                    break;
                default:
                    return aAtom;
                }
                auto* const result = make(node_type::Repeat);
                result->min = min;
                result->max = max;
                result->greedy = !eat(U'?');
                result->firstGroup = aGroupsBefore;
                result->groupCount = iGroups - aGroupsBefore;
                result->children.push_back(aAtom);
                return result;
            }
            static bool might_both_participate(std::vector<std::pair<std::uint32_t, std::uint32_t>> const& aLhs,
                std::vector<std::pair<std::uint32_t, std::uint32_t>> const& aRhs)
            {
                for (std::size_t i = 0u; i < aLhs.size() && i < aRhs.size(); ++i)
                {
                    if (aLhs[i].first != aRhs[i].first)
                        return true;
                    if (aLhs[i].second != aRhs[i].second)
                        return false;
                }
                return true;
            }
            node* parse_group(bool& aLookahead, bool& aLookbehind)
            {
                ++iPos; // (()
                std::u16string name;
                bool named = false;
                if (eat(U'?'))
                {
                    auto const c = peek();
                    if (c == U'=' || c == U'!' || (c == U'<' && (peek(1u) == U'=' || peek(1u) == U'!')))
                    {
                        auto* const result = make(node_type::Look);
                        if (c == U'<')
                        {
                            ++iPos;
                            result->behind = true;
                            aLookbehind = true;
                        }
                        else
                            aLookahead = true;
                        result->negative = peek() == U'!';
                        ++iPos;
                        result->children.push_back(parse_disjunction());
                        if (!eat(U')'))
                            error("Unterminated group");
                        return result;
                    }
                    if (c == U'<')
                    {
                        ++iPos;
                        name = parse_group_name();
                        named = true;
                    }
                    else if (c == U':')
                    {
                        ++iPos;
                        auto* const result = make(node_type::Group);
                        result->children.push_back(parse_disjunction());
                        if (!eat(U')'))
                            error("Unterminated group");
                        return result;
                    }
                    else if (c == U'i' || c == U'm' || c == U's' || c == U'-')
                        return parse_modifiers();
                    else
                        error("Invalid group");
                }
                auto* const result = make(node_type::Group);
                result->index = ++iGroups;
                if (named)
                {
                    for (auto const& g : iNamedGroups)
                        if (g.name == name && might_both_participate(g.path, iPath))
                            error("Duplicate capture group name");
                    iNamedGroups.push_back({ name, result->index, iPath });
                }
                result->children.push_back(parse_disjunction());
                if (!eat(U')'))
                    error("Unterminated group");
                return result;
            }
            node* parse_modifiers()
            {
                auto const modifiers = [&](std::uint32_t& aSet)
                {
                    for (;;)
                    {
                        std::uint32_t flag = 0u;
                        switch (peek())
                        {
                        case U'i': flag = FlagIgnoreCase; break;
                        case U'm': flag = FlagMultiline; break;
                        case U's': flag = FlagDotAll; break;
                        default: return;
                        }
                        if ((aSet & flag) != 0u)
                            error("Repeated flag in modifiers");
                        aSet |= flag;
                        ++iPos;
                    }
                };
                std::uint32_t add = 0u;
                std::uint32_t remove = 0u;
                modifiers(add);
                if (eat(U'-'))
                {
                    modifiers(remove);
                    if (add == 0u && remove == 0u)
                        error("Invalid group modifiers");
                    if ((add & remove) != 0u)
                        error("Repeated flag in modifiers");
                }
                if (!eat(U':'))
                    error("Invalid group");
                auto const savedIgnoreCase = iIgnoreCase;
                auto const savedMultiline = iMultiline;
                auto const savedDotAll = iDotAll;
                if ((add & FlagIgnoreCase) != 0u) iIgnoreCase = true;
                if ((add & FlagMultiline) != 0u) iMultiline = true;
                if ((add & FlagDotAll) != 0u) iDotAll = true;
                if ((remove & FlagIgnoreCase) != 0u) iIgnoreCase = false;
                if ((remove & FlagMultiline) != 0u) iMultiline = false;
                if ((remove & FlagDotAll) != 0u) iDotAll = false;
                auto* const result = make(node_type::Group);
                result->children.push_back(parse_disjunction());
                iIgnoreCase = savedIgnoreCase;
                iMultiline = savedMultiline;
                iDotAll = savedDotAll;
                if (!eat(U')'))
                    error("Unterminated group");
                return result;
            }
            // (after <: RegExpIdentifierName >)
            std::u16string parse_group_name()
            {
                std::u16string name;
                bool first = true;
                for (;;)
                {
                    if (at_end())
                        error("Invalid capture group name");
                    char32_t c = peek();
                    if (c == U'>')
                    {
                        if (first)
                            error("Invalid capture group name");
                        ++iPos;
                        return name;
                    }
                    ++iPos;
                    if (c == U'\\')
                    {
                        if (!eat(U'u'))
                            error("Invalid capture group name");
                        auto const value = parse_unicode_escape(true);
                        if (!value)
                            error("Invalid Unicode escape");
                        c = *value;
                    }
                    else if (!iUnicode && is_high_surrogate(c) && is_low_surrogate(peek()))
                        c = combine_surrogates(c, iText[iPos++]);
                    if (first ? !unicode::id_start(c) : !unicode::id_continue(c))
                        error("Invalid capture group name");
                    append_utf16(name, c);
                    first = false;
                }
            }
            // (after u: Hex4Digits, or (with aUnicode) a surrogate pair of them or { CodePoint }; none (and nothing consumed) if not one)
            std::optional<char32_t> parse_unicode_escape(bool aUnicode)
            {
                auto const start = iPos;
                if (aUnicode && peek() == U'{')
                {
                    ++iPos;
                    char32_t value = 0u;
                    std::size_t digits = 0u;
                    while (hex_value(peek()) >= 0)
                    {
                        value = value * 16u + static_cast<char32_t>(hex_value(peek()));
                        if (value > MaxCodePoint)
                            error("Invalid Unicode escape");
                        ++digits;
                        ++iPos;
                    }
                    if (digits == 0u || !eat(U'}'))
                    {
                        iPos = start;
                        return {};
                    }
                    return value;
                }
                auto const hex4 = [&](std::size_t aAt) -> std::optional<char32_t>
                {
                    char32_t value = 0u;
                    for (std::size_t i = 0u; i < 4u; ++i)
                    {
                        auto const d = hex_value(aAt + i < iText.size() ? iText[aAt + i] : NoChar);
                        if (d < 0)
                            return {};
                        value = value * 16u + static_cast<char32_t>(d);
                    }
                    return value;
                };
                auto const value = hex4(iPos);
                if (!value)
                    return {};
                iPos += 4u;
                if (aUnicode && is_high_surrogate(*value) && peek() == U'\\' && peek(1u) == U'u')
                {
                    auto const low = hex4(iPos + 2u);
                    if (low && is_low_surrogate(*low))
                    {
                        iPos += 6u;
                        return combine_surrogates(*value, *low);
                    }
                }
                return value;
            }
            // (Annex B: LegacyOctalEscapeSequence)
            char32_t parse_legacy_octal()
            {
                char32_t value = peek() - U'0';
                ++iPos;
                auto const maxDigits = value <= 3u ? 3u : 2u;
                for (std::uint32_t i = 1u; i < maxDigits && is_octal_digit(peek()); ++i)
                {
                    value = value * 8u + (peek() - U'0');
                    ++iPos;
                }
                return value;
            }
            // (at the character after \: CharacterEscape (ControlEscape, x, u and IdentityEscape))
            char32_t parse_character_escape(bool aInClass)
            {
                auto const c = peek();
                ++iPos;
                switch (c)
                {
                case U'f': return 0x0Cu;
                case U'n': return 0x0Au;
                case U'r': return 0x0Du;
                case U't': return 0x09u;
                case U'v': return 0x0Bu;
                case U'x':
                    {
                        auto const h1 = hex_value(peek());
                        auto const h2 = hex_value(peek(1u));
                        if (h1 >= 0 && h2 >= 0)
                        {
                            iPos += 2u;
                            return static_cast<char32_t>(h1 * 16 + h2);
                        }
                        if (iUnicode)
                            error("Invalid escape");
                        return c;
                    }
                case U'u':
                    if (auto const value = parse_unicode_escape(iUnicode))
                        return *value;
                    if (iUnicode)
                        error("Invalid Unicode escape");
                    return c;
                default:
                    if (iUnicode)
                    {
                        if (is_syntax_character(c) || c == U'/' || (aInClass && c == U'-'))
                            return c;
                        error("Invalid escape");
                    }
                    return c;
                }
            }
            static bool is_class_escape_letter(char32_t aChar)
            {
                switch (aChar)
                {
                case U'd': case U'D': case U's': case U'S': case U'w': case U'W':
                    return true;
                default:
                    return false;
                }
            }
            range_list all_characters() const
            {
                if (iSets && iIgnoreCase)
                {
                    range_list changing;
                    for (auto const c : unicode_case().changing)
                        changing.push_back({ c, c });
                    normalize(changing);
                    return complement_of(changing, MaxCodePoint);
                }
                return { { 0u, iUnicode ? MaxCodePoint : 0xFFFFu } };
            }
            // (CharacterComplement)
            char_set complement(char_set const& aSet) const
            {
                char_set result;
                result.ranges = difference_of(all_characters(), aSet.ranges);
                return result;
            }
            char_set maybe_simple_case_folding(char_set aSet) const
            {
                if (!iSets || !iIgnoreCase)
                    return aSet;
                return simple_case_folding(aSet);
            }
            range_list word_characters() const
            {
                range_list result{ { U'0', U'9' }, { U'A', U'Z' }, { U'_', U'_' }, { U'a', U'z' } };
                if (iUnicode && iIgnoreCase)
                    result = union_of(result, unicode_case().extraWordCharacters);
                return result;
            }
            // (at the letter of CharacterClassEscape (d D s S w W p P))
            char_set parse_class_escape(bool& aMayContainStrings)
            {
                aMayContainStrings = false;
                auto const c = peek();
                ++iPos;
                char_set result;
                switch (c)
                {
                case U'd':
                case U'D':
                    result.ranges = { { U'0', U'9' } };
                    return c == U'd' ? result : complement(result);
                case U's':
                case U'S':
                    result.ranges = white_space_characters();
                    return c == U's' ? result : complement(result);
                case U'w':
                case U'W':
                    result.ranges = word_characters();
                    result = maybe_simple_case_folding(std::move(result));
                    return c == U'w' ? result : complement(result);
                default:
                    break;
                }
                // (\p{...} and \P{...})
                bool const negated = c == U'P';
                if (!eat(U'{'))
                    error("Invalid property name");
                std::u16string name;
                std::u16string value;
                bool hasValue = false;
                auto const property_character = [](char32_t aChar) { return is_ascii_letter(aChar) || is_digit(aChar) || aChar == U'_'; };
                while (property_character(peek()))
                    name.push_back(static_cast<char16_t>(iText[iPos++]));
                if (eat(U'='))
                {
                    hasValue = true;
                    while (property_character(peek()))
                        value.push_back(static_cast<char16_t>(iText[iPos++]));
                }
                if (!eat(U'}') || name.empty() || (hasValue && value.empty()))
                    error("Invalid property name");
                std::span<unicode::range const> ranges;
                if (hasValue)
                {
                    if (!unicode::property(name, value, ranges))
                        error("Invalid property name");
                }
                else
                {
                    std::span<std::u32string_view const> strings;
                    if (unicode::property_of_strings(name, strings, ranges))
                    {
                        if (!iSets || negated)
                            error("Invalid property name");
                        for (auto const& s : strings)
                        {
                            if (s.size() == 1u)
                                result.ranges.push_back({ s[0], s[0] });
                            else
                                result.strings.emplace_back(s);
                        }
                        std::sort(result.strings.begin(), result.strings.end());
                        result.strings.erase(std::unique(result.strings.begin(), result.strings.end()), result.strings.end());
                        aMayContainStrings = true;
                    }
                    else if (!unicode::property(name, std::u16string_view{}, ranges))
                        error("Invalid property name");
                }
                for (auto const& r : ranges)
                    result.ranges.push_back({ r.first, r.last });
                normalize(result.ranges);
                result = maybe_simple_case_folding(std::move(result));
                return negated ? complement(result) : result;
            }
            node* make_class_node(char_set const& aSet, bool aInvert)
            {
                auto const make_class = [&](range_list const& aRanges, bool aInvertClass)
                {
                    character_class cls;
                    cls.ranges = iIgnoreCase ? canonical_closure(aRanges, iUnicode) : aRanges;
                    cls.invert = aInvertClass;
                    cls.ignoreCase = iIgnoreCase;
                    for (auto const& r : cls.ranges)
                        for (char32_t c = r.first; c <= r.last && c < 128u; ++c)
                            cls.ascii[c >> 6u] |= 1ull << (c & 63u);
                    iProgram.classes.push_back(std::move(cls));
                    auto* const result = make(node_type::Class);
                    result->index = static_cast<std::uint32_t>(iProgram.classes.size() - 1u);
                    return result;
                };
                if (aSet.strings.empty())
                    return make_class(aSet.ranges, aInvert);
                // (the v flag: the strings (longest first), then the single characters, then the empty string)
                std::vector<std::u32string const*> strings;
                bool empty = false;
                for (auto const& s : aSet.strings)
                {
                    if (s.empty())
                        empty = true;
                    else
                        strings.push_back(&s);
                }
                std::stable_sort(strings.begin(), strings.end(), [](auto const* a, auto const* b) { return a->size() > b->size(); });
                auto* const result = make(node_type::Alternation);
                for (auto const* s : strings)
                {
                    auto* const sequence = make(node_type::Sequence);
                    for (auto const c : *s)
                        sequence->children.push_back(make_char(c));
                    result->children.push_back(sequence);
                }
                if (!aSet.ranges.empty())
                    result->children.push_back(make_class(aSet.ranges, false));
                if (empty)
                    result->children.push_back(make(node_type::Empty));
                if (result->children.empty())
                    return make_class({}, false);
                if (result->children.size() == 1u)
                    return result->children[0];
                return result;
            }
            node* make_backreference(std::vector<std::uint32_t> aGroups)
            {
                iProgram.backreferences.push_back(std::move(aGroups));
                auto* const result = make(node_type::Backreference);
                result->index = static_cast<std::uint32_t>(iProgram.backreferences.size() - 1u);
                return result;
            }
            // (after \)
            node* parse_atom_escape()
            {
                if (at_end())
                    error("\\ at end of pattern");
                auto const c = peek();
                if (c >= U'1' && c <= U'9')
                {
                    auto const start = iPos;
                    std::uint64_t n = 0u;
                    while (is_digit(peek()))
                    {
                        n = std::min<std::uint64_t>(n * 10u + (peek() - U'0'), 0xFFFFFFFFull);
                        ++iPos;
                    }
                    if (iUnicode || n <= iTotalGroups)
                    {
                        if (n > iTotalGroups)
                            error("Invalid escape");
                        return make_backreference({ static_cast<std::uint32_t>(n) });
                    }
                    // (Annex B: a legacy octal escape or an identity escape)
                    iPos = start;
                    if (c <= U'7')
                        return make_char(parse_legacy_octal());
                    ++iPos;
                    return make_char(c);
                }
                if (c == U'0')
                {
                    if (is_digit(peek(1u)))
                    {
                        if (iUnicode)
                            error("Invalid decimal escape");
                        return make_char(parse_legacy_octal());
                    }
                    ++iPos;
                    return make_char(0u);
                }
                if (c == U'k')
                {
                    ++iPos;
                    if (!iNamed)
                        return make_char(c);
                    if (!eat(U'<'))
                        error("Invalid named reference");
                    auto name = parse_group_name();
                    auto* const result = make_backreference({});
                    iPendingReferences.emplace_back(std::move(name), result->index);
                    return result;
                }
                if (is_class_escape_letter(c) || (iUnicode && (c == U'p' || c == U'P')))
                {
                    bool mayContainStrings = false;
                    auto const set = parse_class_escape(mayContainStrings);
                    return make_class_node(set, false);
                }
                if (c == U'c')
                {
                    if (is_ascii_letter(peek(1u)))
                    {
                        iPos += 2u;
                        return make_char(iText[iPos - 1u] % 32u);
                    }
                    if (iUnicode)
                        error("Invalid unicode escape");
                    return make_char(U'\\');  // (Annex B: the \ is itself; the c follows)
                }
                return make_char(parse_character_escape(false));
            }
            // (at [)
            node* parse_class()
            {
                ++iPos;
                if (iSets)
                {
                    bool mayContainStrings = false;
                    auto const set = parse_class_contents_v(mayContainStrings);
                    return make_class_node(set, false);
                }
                bool const invert = eat(U'^');
                range_list ranges;
                auto const add = [&](class_atom const& aAtom)
                {
                    if (aAtom.isSet)
                        ranges.insert(ranges.end(), aAtom.set.begin(), aAtom.set.end());
                    else
                        ranges.push_back({ aAtom.ch, aAtom.ch });
                };
                for (;;)
                {
                    if (at_end())
                        error("Unterminated character class");
                    if (eat(U']'))
                        break;
                    auto const first = parse_class_atom();
                    if (peek() == U'-' && iPos + 1u < iText.size() && iText[iPos + 1u] != U']')
                    {
                        ++iPos;
                        auto const second = parse_class_atom();
                        if (first.isSet || second.isSet)
                        {
                            if (iUnicode)
                                error("Invalid character class");
                            add(first);
                            ranges.push_back({ U'-', U'-' });
                            add(second);
                            continue;
                        }
                        if (first.ch > second.ch)
                            error("Range out of order in character class");
                        ranges.push_back({ first.ch, second.ch });
                        continue;
                    }
                    add(first);
                }
                normalize(ranges);
                char_set set;
                set.ranges = std::move(ranges);
                return make_class_node(set, invert);
            }
            // (ClassAtom (without the v flag))
            class_atom parse_class_atom()
            {
                class_atom result;
                auto c = peek();
                ++iPos;
                if (c != U'\\')
                {
                    result.ch = c;
                    return result;
                }
                if (at_end())
                    error("\\ at end of pattern");
                c = peek();
                if (c == U'b')
                {
                    ++iPos;
                    result.ch = 0x08u;
                    return result;
                }
                if (c == U'c')
                {
                    auto const n = peek(1u);
                    if (is_ascii_letter(n) || (!iUnicode && (is_digit(n) || n == U'_')))
                    {
                        iPos += 2u;
                        result.ch = n % 32u;
                        return result;
                    }
                    if (iUnicode)
                        error("Invalid class escape");
                    result.ch = U'\\';  // (Annex B: the \ is itself; the c follows)
                    return result;
                }
                if (is_class_escape_letter(c) || (iUnicode && (c == U'p' || c == U'P')))
                {
                    bool mayContainStrings = false;
                    result.isSet = true;
                    result.set = parse_class_escape(mayContainStrings).ranges;
                    return result;
                }
                if (is_digit(c))
                {
                    if (iUnicode)
                    {
                        if (c == U'0' && !is_digit(peek(1u)))
                        {
                            ++iPos;
                            result.ch = 0u;
                            return result;
                        }
                        error("Invalid class escape");
                    }
                    if (c <= U'7')
                        result.ch = parse_legacy_octal();
                    else
                    {
                        ++iPos;
                        result.ch = c;
                    }
                    return result;
                }
                if (c == U'k' && iNamed)
                    error("Invalid escape");
                result.ch = parse_character_escape(true);
                return result;
            }
            // --- the v flag: ClassSetExpression (after [)
            char_set parse_class_contents_v(bool& aMayContainStrings)
            {
                if (++iDepth > MaxDepth)
                    error("Regular expression too deeply nested");
                bool const negate = eat(U'^');
                char_set result;
                bool may = false;
                if (!eat(U']'))
                {
                    bool isRange = false;
                    result = parse_class_set_operand(true, isRange, may);
                    auto const is_operator = [&](char32_t aChar) { return peek() == aChar && peek(1u) == aChar; };
                    if (!isRange && (is_operator(U'&') || is_operator(U'-')))
                    {
                        auto const op = peek();
                        while (is_operator(op))
                        {
                            iPos += 2u;
                            if (op == U'&' && peek() == U'&')
                                error("Invalid set operation in character class");
                            bool operandMay = false;
                            bool operandRange = false;
                            auto const operand = parse_class_set_operand(false, operandRange, operandMay);
                            if (op == U'&')
                            {
                                result = set_intersection(result, operand);
                                may = may && operandMay;
                            }
                            else
                                result = set_difference(result, operand);
                        }
                        if (!eat(U']'))
                        {
                            if (at_end())
                                error("Unterminated character class");
                            error("Invalid set operation in character class");
                        }
                    }
                    else
                    {
                        for (;;)
                        {
                            if (at_end())
                                error("Unterminated character class");
                            if (eat(U']'))
                                break;
                            if (is_operator(U'&') || is_operator(U'-'))
                                error("Invalid set operation in character class");
                            bool operandMay = false;
                            auto const operand = parse_class_set_operand(true, isRange, operandMay);
                            result = set_union(result, operand);
                            may = may || operandMay;
                        }
                    }
                }
                if (negate)
                {
                    if (may)
                        error("Negated character class may contain strings");
                    result = complement(result);
                    may = false;
                }
                --iDepth;
                aMayContainStrings = may;
                return result;
            }
            // (ClassSetOperand, or (if aAllowRange) ClassSetRange)
            char_set parse_class_set_operand(bool aAllowRange, bool& aIsRange, bool& aMayContainStrings)
            {
                aIsRange = false;
                aMayContainStrings = false;
                if (at_end())
                    error("Unterminated character class");
                if (eat(U'['))
                    return parse_class_contents_v(aMayContainStrings);
                if (peek() == U'\\')
                {
                    auto const n = peek(1u);
                    if (n == U'q')
                    {
                        if (peek(2u) != U'{')
                            error("Invalid escape");
                        iPos += 3u;
                        return parse_class_string_disjunction(aMayContainStrings);
                    }
                    if (is_class_escape_letter(n) || n == U'p' || n == U'P')
                    {
                        ++iPos;
                        return parse_class_escape(aMayContainStrings);
                    }
                }
                auto const first = parse_class_set_character();
                char_set result;
                if (aAllowRange && peek() == U'-' && peek(1u) != U'-')
                {
                    ++iPos;
                    auto const last = parse_class_set_character();
                    if (first > last)
                        error("Range out of order in character class");
                    aIsRange = true;
                    result.ranges = { { first, last } };
                }
                else
                    result.ranges = { { first, first } };
                return maybe_simple_case_folding(std::move(result));
            }
            // (ClassSetCharacter)
            char32_t parse_class_set_character()
            {
                if (at_end())
                    error("Unterminated character class");
                auto const c = peek();
                if (c == U'\\')
                {
                    ++iPos;
                    if (at_end())
                        error("\\ at end of pattern");
                    auto const e = peek();
                    if (e == U'b')
                    {
                        ++iPos;
                        return 0x08u;
                    }
                    if (is_class_set_reserved_punctuator(e))
                    {
                        ++iPos;
                        return e;
                    }
                    if (e == U'c')
                    {
                        if (!is_ascii_letter(peek(1u)))
                            error("Invalid class escape");
                        iPos += 2u;
                        return iText[iPos - 1u] % 32u;
                    }
                    if (e == U'0')
                    {
                        if (is_digit(peek(1u)))
                            error("Invalid decimal escape");
                        ++iPos;
                        return 0u;
                    }
                    return parse_character_escape(false);
                }
                if (is_class_set_syntax_character(c))
                    error("Invalid character in character class");
                if (is_class_set_reserved_double_punctuator(c) && peek(1u) == c)
                    error("Invalid set operation in character class");
                ++iPos;
                return c;
            }
            // (after \q{)
            char_set parse_class_string_disjunction(bool& aMayContainStrings)
            {
                char_set result;
                std::u32string current;
                for (;;)
                {
                    if (at_end())
                        error("Unterminated character class");
                    auto const c = peek();
                    if (c == U'}' || c == U'|')
                    {
                        ++iPos;
                        if (current.size() != 1u)
                            aMayContainStrings = true;
                        add_string(result, std::move(current));
                        current.clear();
                        if (c == U'}')
                            break;
                        continue;
                    }
                    current.push_back(parse_class_set_character());
                }
                return maybe_simple_case_folding(std::move(result));
            }
        private:
            program& iProgram;
            std::u32string iText;
            std::size_t iPos = 0u;
            bool iUnicode;
            bool iSets;
            bool iNamed = false;
            bool iIgnoreCase;
            bool iMultiline;
            bool iDotAll;
            std::uint32_t iTotalGroups = 0u;
            std::uint32_t iGroups = 0u;
            std::uint32_t iDepth = 0u;
            std::uint32_t iDisjunctions = 0u;
            std::vector<std::unique_ptr<node>> iNodes;
            std::vector<std::pair<std::uint32_t, std::uint32_t>> iPath;     ///< (the disjunctions (and their alternatives) we are in)
            struct named_group
            {
                std::u16string name;
                std::uint32_t number;
                std::vector<std::pair<std::uint32_t, std::uint32_t>> path;
            };
            std::vector<named_group> iNamedGroups;
            std::vector<std::pair<std::u16string, std::uint32_t>> iPendingReferences;
        };

        // --- the code generator

        std::uint64_t min_length(node const* aNode)
        {
            switch (aNode->type)
            {
            case node_type::Char:
            case node_type::Class:
            case node_type::Dot:
                return 1u;
            case node_type::Sequence:
                {
                    std::uint64_t result = 0u;
                    for (auto const* c : aNode->children)
                        result = std::min<std::uint64_t>(result + min_length(c), 0xFFFFFFFFull);
                    return result;
                }
            case node_type::Alternation:
                {
                    std::uint64_t result = 0xFFFFFFFFull;
                    for (auto const* c : aNode->children)
                        result = std::min(result, min_length(c));
                    return result;
                }
            case node_type::Group:
                return min_length(aNode->children[0]);
            case node_type::Repeat:
                return aNode->max == 0u ? 0u : std::min<std::uint64_t>(aNode->min * min_length(aNode->children[0]), 0xFFFFFFFFull);
            default:
                return 0u;
            }
        }

        node const* single_character(node const* aNode)
        {
            while (aNode->type == node_type::Group && aNode->index == 0u)
                aNode = aNode->children[0];
            return aNode->type == node_type::Char || aNode->type == node_type::Class || aNode->type == node_type::Dot ? aNode : nullptr;
        }

        class code_generator
        {
        public:
            code_generator(program& aProgram, bool aUnicode) : iProgram{ aProgram }, iUnicode{ aUnicode } {}
        public:
            void generate(node const* aRoot)
            {
                emit(aRoot, false);
                push({ opcode::Match });
            }
        private:
            std::uint32_t here() const { return static_cast<std::uint32_t>(iProgram.code.size()); }
            std::uint32_t push(instruction const& aInstruction)
            {
                iProgram.code.push_back(aInstruction);
                return here() - 1u;
            }
            // (whether a character that ignores case has no other characters of its case (it is compared exactly))
            bool caseless(char32_t aChar) const
            {
                if (canonicalize(aChar, iUnicode) != aChar)
                    return false;
                auto const& targets = iUnicode ? unicode_case().targets : legacy_case().targets;
                return !std::binary_search(targets.begin(), targets.end(), aChar);
            }
            void emit(node const* aNode, bool aBackward)
            {
                std::uint8_t const direction = aBackward ? InstructionBackward : 0u;
                switch (aNode->type)
                {
                case node_type::Empty:
                    break;
                case node_type::Char:
                    if (aNode->ignoreCase && !caseless(aNode->ch))
                        push({ opcode::CharI, direction, canonicalize(aNode->ch, iUnicode) });
                    else
                        push({ opcode::Char, direction, aNode->ch });
                    break;
                case node_type::Class:
                    push({ opcode::Class, direction, aNode->index });
                    break;
                case node_type::Dot:
                    push({ aNode->dotAll ? opcode::Any : opcode::Dot, direction });
                    break;
                case node_type::Sequence:
                    if (!aBackward)
                        for (auto const* c : aNode->children)
                            emit(c, false);
                    else
                        for (auto it = aNode->children.rbegin(); it != aNode->children.rend(); ++it)
                            emit(*it, true);
                    break;
                case node_type::Alternation:
                    {
                        std::vector<std::uint32_t> jumps;
                        for (std::size_t i = 0u; i < aNode->children.size(); ++i)
                        {
                            if (i + 1u < aNode->children.size())
                            {
                                auto const fork = push({ opcode::Fork });
                                emit(aNode->children[i], aBackward);
                                jumps.push_back(push({ opcode::Jump }));
                                iProgram.code[fork].a = here();
                            }
                            else
                                emit(aNode->children[i], aBackward);
                        }
                        for (auto const j : jumps)
                            iProgram.code[j].a = here();
                    }
                    break;
                case node_type::Group:
                    if (aNode->index == 0u)
                        emit(aNode->children[0], aBackward);
                    else
                    {
                        auto const start = aNode->index * 2u;
                        auto const end = start + 1u;
                        push({ opcode::Save, 0u, aBackward ? end : start });
                        emit(aNode->children[0], aBackward);
                        push({ opcode::Save, 0u, aBackward ? start : end });
                    }
                    break;
                case node_type::Look:
                    {
                        auto const start = push({ opcode::LookStart, static_cast<std::uint8_t>(aNode->negative ? InstructionNegative : 0u) });
                        emit(aNode->children[0], aNode->behind);
                        push({ opcode::LookEnd });
                        iProgram.code[start].a = here();
                    }
                    break;
                case node_type::LineStart:
                    push({ opcode::LineStart, static_cast<std::uint8_t>(aNode->multiline ? InstructionMultiline : 0u) });
                    break;
                case node_type::LineEnd:
                    push({ opcode::LineEnd, static_cast<std::uint8_t>(aNode->multiline ? InstructionMultiline : 0u) });
                    break;
                case node_type::WordBoundary:
                case node_type::NotWordBoundary:
                    push({ aNode->type == node_type::WordBoundary ? opcode::WordBoundary : opcode::NotWordBoundary,
                        static_cast<std::uint8_t>(iUnicode && aNode->ignoreCase ? InstructionExtraWord : 0u) });
                    break;
                case node_type::Backreference:
                    push({ opcode::Backreference, static_cast<std::uint8_t>(direction | (aNode->ignoreCase ? InstructionIgnoreCase : 0u)), aNode->index });
                    break;
                case node_type::Repeat:
                    emit_repeat(aNode, aBackward);
                    break;
                }
            }
            void emit_repeat(node const* aNode, bool aBackward)
            {
                auto const* body = aNode->children[0];
                auto const min = aNode->min;
                auto const max = aNode->max;
                bool const greedy = aNode->greedy;
                if (max == 0u)
                    return;
                if (min == 1u && max == 1u)
                {
                    emit(body, aBackward);
                    return;
                }
                if (single_character(body) != nullptr)
                {
                    push({ opcode::Star, static_cast<std::uint8_t>(greedy ? InstructionGreedy : 0u), min, max });
                    emit(body, aBackward);
                    return;
                }
                bool const canBeEmpty = min_length(body) == 0u;
                auto const firstSlot = (aNode->firstGroup + 1u) * 2u;
                auto const slotCount = aNode->groupCount * 2u;
                auto const clear = [&]()
                {
                    if (slotCount != 0u)
                        push({ opcode::ClearCaptures, 0u, firstSlot, slotCount });
                };
                if (!canBeEmpty && max == Infinite && min <= 1u)
                {
                    if (min == 0u)
                    {
                        auto const loop = here();
                        auto const fork = push({ opcode::Fork });
                        if (greedy)
                        {
                            clear();
                            emit(body, aBackward);
                            push({ opcode::Jump, 0u, loop });
                            iProgram.code[fork].a = here();
                        }
                        else
                        {
                            auto const jump = push({ opcode::Jump });
                            iProgram.code[fork].a = here();
                            clear();
                            emit(body, aBackward);
                            push({ opcode::Jump, 0u, loop });
                            iProgram.code[jump].a = here();
                        }
                    }
                    else
                    {
                        auto const loop = here();
                        clear();
                        emit(body, aBackward);
                        if (greedy)
                        {
                            auto const fork = push({ opcode::Fork });
                            push({ opcode::Jump, 0u, loop });
                            iProgram.code[fork].a = here();
                        }
                        else
                            push({ opcode::Fork, 0u, loop });
                    }
                    return;
                }
                if (!canBeEmpty && min == 0u && max == 1u)
                {
                    auto const fork = push({ opcode::Fork });
                    if (greedy)
                    {
                        clear();
                        emit(body, aBackward);
                        iProgram.code[fork].a = here();
                    }
                    else
                    {
                        auto const jump = push({ opcode::Jump });
                        iProgram.code[fork].a = here();
                        clear();
                        emit(body, aBackward);
                        iProgram.code[jump].a = here();
                    }
                    return;
                }
                // (RepeatMatcher: a count and (if the body can match the empty string) the position each iteration starts at)
                auto const r = iProgram.registers++;
                push({ opcode::RepeatInit, 0u, r });
                auto const loop = push({ opcode::RepeatLoop, static_cast<std::uint8_t>(greedy ? InstructionGreedy : 0u), r, min, max });
                clear();
                if (canBeEmpty)
                    push({ opcode::RepeatStart, 0u, r });
                emit(body, aBackward);
                push({ opcode::RepeatEnd, static_cast<std::uint8_t>(canBeEmpty ? InstructionEmptyCheck : 0u), r, min, loop });
                iProgram.code[loop].d = here();
            }
        private:
            program& iProgram;
            bool iUnicode;
        };

        // --- search optimizations: the code units a match can start with, and whether it is anchored at the start

        void add_first_unit(range_list& aOut, char32_t aFirst, char32_t aLast)
        {
            if (aFirst <= 0xFFFFu)
                aOut.push_back({ aFirst, std::min<char32_t>(aLast, 0xFFFFu) });
            if (aLast >= 0x10000u)
                aOut.push_back({ lead_surrogate_of(std::max<char32_t>(aFirst, 0x10000u)), lead_surrogate_of(aLast) });
        }

        void first_characters(program const& aProgram, node const* aNode, bool aUnicode, range_list& aOut, bool& aEmpty, bool& aKnown)
        {
            switch (aNode->type)
            {
            case node_type::Empty:
            case node_type::Look:
            case node_type::LineStart:
            case node_type::LineEnd:
            case node_type::WordBoundary:
            case node_type::NotWordBoundary:
                aEmpty = true;
                return;
            case node_type::Backreference:
            case node_type::Dot:
                aKnown = false;
                return;
            case node_type::Char:
                aEmpty = false;
                if (!aNode->ignoreCase)
                    add_first_unit(aOut, aNode->ch, aNode->ch);
                else
                {
                    auto const target = canonicalize(aNode->ch, aUnicode);
                    add_first_unit(aOut, target, target);
                    for (auto const c : changing_characters(aUnicode))
                        if (canonicalize(c, aUnicode) == target)
                            add_first_unit(aOut, c, c);
                }
                return;
            case node_type::Class:
                {
                    aEmpty = false;
                    auto const& cls = aProgram.classes[aNode->index];
                    if (cls.invert || cls.ignoreCase)
                    {
                        aKnown = false;
                        return;
                    }
                    for (auto const& r : cls.ranges)
                        add_first_unit(aOut, r.first, r.last);
                }
                return;
            case node_type::Group:
                first_characters(aProgram, aNode->children[0], aUnicode, aOut, aEmpty, aKnown);
                return;
            case node_type::Repeat:
                if (aNode->max == 0u)
                {
                    aEmpty = true;
                    return;
                }
                first_characters(aProgram, aNode->children[0], aUnicode, aOut, aEmpty, aKnown);
                aEmpty = aEmpty || aNode->min == 0u;
                return;
            case node_type::Sequence:
                aEmpty = true;
                for (auto const* c : aNode->children)
                {
                    bool empty = false;
                    first_characters(aProgram, c, aUnicode, aOut, empty, aKnown);
                    if (!aKnown)
                        return;
                    if (!empty)
                    {
                        aEmpty = false;
                        return;
                    }
                }
                return;
            case node_type::Alternation:
                aEmpty = false;
                for (auto const* c : aNode->children)
                {
                    bool empty = false;
                    first_characters(aProgram, c, aUnicode, aOut, empty, aKnown);
                    if (!aKnown)
                        return;
                    aEmpty = aEmpty || empty;
                }
                return;
            }
        }

        // (the literal text that a match starts with)
        void literal_prefix(node const* aNode, std::u16string& aPrefix)
        {
            if (aNode->type == node_type::Char && !aNode->ignoreCase)
                append_utf16(aPrefix, aNode->ch);
            else if (aNode->type == node_type::Sequence)
            {
                for (auto const* c : aNode->children)
                {
                    if (c->type != node_type::Char || c->ignoreCase)
                        break;
                    append_utf16(aPrefix, c->ch);
                }
            }
        }

        std::size_t find_prefix(program const& aProgram, std::u16string_view aInput, std::size_t aFrom)
        {
            auto const& prefix = aProgram.prefix;
            auto const m = prefix.size();
            auto const n = aInput.size();
            auto const last = prefix[m - 1u];
            for (auto i = aFrom; i + m <= n;)
            {
                auto const c = aInput[i + m - 1u];
                if (c == last && std::equal(prefix.begin(), prefix.end() - 1, aInput.begin() + static_cast<std::ptrdiff_t>(i)))
                    return i;
                i += aProgram.prefixSkip[c & 0xFFu];
            }
            return std::u16string_view::npos;
        }

        bool anchored_at_start(node const* aNode)
        {
            switch (aNode->type)
            {
            case node_type::LineStart:
                return !aNode->multiline;
            case node_type::Sequence:
                return !aNode->children.empty() && anchored_at_start(aNode->children[0]);
            case node_type::Alternation:
                for (auto const* c : aNode->children)
                    if (!anchored_at_start(c))
                        return false;
                return true;
            case node_type::Group:
                return anchored_at_start(aNode->children[0]);
            default:
                return false;
            }
        }

        // --- the matcher

        enum : std::uint32_t
        {
            EntryChoice,
            EntrySlot,
            EntryRegister,
            EntryBarrier,
            EntryStarGreedy,
            EntryStarLazy
        };

        struct backtrack_entry
        {
            std::uint32_t kind;
            std::uint32_t pc;               ///< (a slot or register: its index)
            std::int32_t pos;               ///< (a slot or register: its previous value)
            std::int32_t aux;
        };

        struct look_frame
        {
            std::size_t height;             ///< (the backtrack stack's height (where its barrier is))
            std::int32_t pos;
            std::uint32_t pc;               ///< (its LookStart)
        };

        struct matcher_state
        {
            std::vector<backtrack_entry> stack;
            std::vector<look_frame> frames;
            std::vector<std::int32_t> slots;
            std::vector<std::int32_t> registers;
        };

        thread_local matcher_state tState;

        template <bool Unicode>
        class matcher
        {
        public:
            matcher(program const& aProgram, std::u16string_view aInput, matcher_state& aState) :
                iProgram{ aProgram },
                iInput{ aInput.data() },
                iLength{ static_cast<std::int32_t>(aInput.size()) },
                iState{ aState },
                iLegacy{ Unicode ? nullptr : legacy_case().canonical.data() }
            {
            }
        public:
            match_result run(std::int32_t aStart)
            {
                auto& stack = iState.stack;
                auto& frames = iState.frames;
                auto& slots = iState.slots;
                auto& registers = iState.registers;
                stack.clear();
                frames.clear();
                auto const* const code = iProgram.code.data();
                std::uint32_t pc = 0u;
                std::int32_t pos = aStart;
                auto const push = [&](std::uint32_t aKind, std::uint32_t aPc, std::int32_t aPos, std::int32_t aAux = 0)
                {
                    stack.push_back({ aKind, aPc, aPos, aAux });
                    return stack.size() < MaxBacktrackStack;
                };
                for (;;)
                {
                    auto const& i = code[pc];
                    switch (i.op)
                    {
                    case opcode::Match:
                        slots[0] = aStart;
                        slots[1] = pos;
                        return match_result::Match;
                    case opcode::Char:
                    case opcode::CharI:
                    case opcode::Class:
                    case opcode::Dot:
                    case opcode::Any:
                        if (step(i, pos))
                        {
                            ++pc;
                            continue;
                        }
                        goto fail;
                    case opcode::LineStart:
                        if (pos == 0 || ((i.flags & InstructionMultiline) != 0u && is_line_terminator(iInput[pos - 1])))
                        {
                            ++pc;
                            continue;
                        }
                        goto fail;
                    case opcode::LineEnd:
                        if (pos == iLength || ((i.flags & InstructionMultiline) != 0u && is_line_terminator(iInput[pos])))
                        {
                            ++pc;
                            continue;
                        }
                        goto fail;
                    case opcode::WordBoundary:
                    case opcode::NotWordBoundary:
                        {
                            bool const extra = (i.flags & InstructionExtraWord) != 0u;
                            bool const boundary = is_word(pos - 1, extra) != is_word(pos, extra);
                            if (boundary == (i.op == opcode::WordBoundary))
                            {
                                ++pc;
                                continue;
                            }
                        }
                        goto fail;
                    case opcode::Fork:
                        if (!push(EntryChoice, i.a, pos))
                            return match_result::Overflow;
                        ++pc;
                        continue;
                    case opcode::Jump:
                        pc = i.a;
                        continue;
                    case opcode::Save:
                        if (!push(EntrySlot, i.a, slots[i.a]))
                            return match_result::Overflow;
                        slots[i.a] = pos;
                        ++pc;
                        continue;
                    case opcode::ClearCaptures:
                        for (std::uint32_t s = i.a; s < i.a + i.b; ++s)
                            if (slots[s] != -1)
                            {
                                if (!push(EntrySlot, s, slots[s]))
                                    return match_result::Overflow;
                                slots[s] = -1;
                            }
                        ++pc;
                        continue;
                    case opcode::Backreference:
                        if (backreference(i, pos))
                        {
                            ++pc;
                            continue;
                        }
                        goto fail;
                    case opcode::LookStart:
                        frames.push_back({ stack.size(), pos, pc });
                        if (!push(EntryBarrier, pc, pos))
                            return match_result::Overflow;
                        ++pc;
                        continue;
                    case opcode::LookEnd:
                        {
                            auto const frame = frames.back();
                            frames.pop_back();
                            auto const& start = code[frame.pc];
                            if ((start.flags & InstructionNegative) == 0u)
                            {
                                // (a lookaround is atomic: its choice points are discarded; the records that undo its captures are kept)
                                auto out = frame.height;
                                for (auto k = frame.height + 1u; k < stack.size(); ++k)
                                    if (stack[k].kind == EntrySlot || stack[k].kind == EntryRegister)
                                        stack[out++] = stack[k];
                                stack.resize(out);
                                pos = frame.pos;
                                pc = start.a;
                                continue;
                            }
                            // (a negative lookaround whose contents matched: undo them and fail)
                            while (stack.size() > frame.height + 1u)
                            {
                                auto const& e = stack.back();
                                if (e.kind == EntrySlot)
                                    slots[e.pc] = e.pos;
                                else if (e.kind == EntryRegister)
                                    registers[e.pc] = e.pos;
                                stack.pop_back();
                            }
                            stack.pop_back();
                        }
                        goto fail;
                    case opcode::RepeatInit:
                        if (!push(EntryRegister, i.a * 2u, registers[i.a * 2u]))
                            return match_result::Overflow;
                        registers[i.a * 2u] = 0;
                        ++pc;
                        continue;
                    case opcode::RepeatLoop:
                        {
                            auto const count = static_cast<std::uint32_t>(registers[i.a * 2u]);
                            if (count < i.b)
                            {
                                ++pc;
                                continue;
                            }
                            if (i.c != Infinite && count >= i.c)
                            {
                                pc = i.d;
                                continue;
                            }
                            if ((i.flags & InstructionGreedy) != 0u)
                            {
                                if (!push(EntryChoice, i.d, pos))
                                    return match_result::Overflow;
                                ++pc;
                            }
                            else
                            {
                                if (!push(EntryChoice, pc + 1u, pos))
                                    return match_result::Overflow;
                                pc = i.d;
                            }
                        }
                        continue;
                    case opcode::RepeatStart:
                        if (!push(EntryRegister, i.a * 2u + 1u, registers[i.a * 2u + 1u]))
                            return match_result::Overflow;
                        registers[i.a * 2u + 1u] = pos;
                        ++pc;
                        continue;
                    case opcode::RepeatEnd:
                        {
                            auto const count = static_cast<std::uint32_t>(registers[i.a * 2u]);
                            if ((i.flags & InstructionEmptyCheck) != 0u && count >= i.b && pos == registers[i.a * 2u + 1u])
                                goto fail;
                            if (!push(EntryRegister, i.a * 2u, registers[i.a * 2u]))
                                return match_result::Overflow;
                            registers[i.a * 2u] = static_cast<std::int32_t>(count + 1u);
                            pc = i.c;
                        }
                        continue;
                    case opcode::Star:
                        {
                            auto const& m = code[pc + 1u];
                            std::uint32_t count = 0u;
                            while (count < i.a)
                            {
                                if (!step(m, pos))
                                    goto fail;
                                ++count;
                            }
                            if ((i.flags & InstructionGreedy) != 0u)
                            {
                                auto const minPos = pos;
                                while (count < i.b && step(m, pos))
                                    ++count;
                                if (pos != minPos && !push(EntryStarGreedy, pc, pos, minPos))
                                    return match_result::Overflow;
                            }
                            else if (count < i.b && !push(EntryStarLazy, pc, pos, static_cast<std::int32_t>(count)))
                                return match_result::Overflow;
                            pc += 2u;
                        }
                        continue;
                    }
                fail:
                    for (;;)
                    {
                        if (stack.empty())
                            return match_result::NoMatch;
                        auto const e = stack.back();
                        stack.pop_back();
                        switch (e.kind)
                        {
                        case EntryChoice:
                            pc = e.pc;
                            pos = e.pos;
                            goto resume;
                        case EntrySlot:
                            slots[e.pc] = e.pos;
                            continue;
                        case EntryRegister:
                            registers[e.pc] = e.pos;
                            continue;
                        case EntryBarrier:
                            {
                                auto const frame = frames.back();
                                frames.pop_back();
                                auto const& start = code[frame.pc];
                                if ((start.flags & InstructionNegative) != 0u)
                                {
                                    // (a negative lookaround whose contents did not match)
                                    pos = frame.pos;
                                    pc = start.a;
                                    goto resume;
                                }
                            }
                            continue;
                        case EntryStarGreedy:
                            {
                                // (give back one character)
                                auto p = e.pos;
                                if ((code[e.pc + 1u].flags & InstructionBackward) == 0u)
                                {
                                    if (Unicode && p - 2 >= e.aux && is_low_surrogate(iInput[p - 1]) && is_high_surrogate(iInput[p - 2]))
                                        p -= 2;
                                    else
                                        p -= 1;
                                }
                                else
                                {
                                    if (Unicode && p + 2 <= e.aux && is_high_surrogate(iInput[p]) && is_low_surrogate(iInput[p + 1]))
                                        p += 2;
                                    else
                                        p += 1;
                                }
                                if (p != e.aux && !push(EntryStarGreedy, e.pc, p, e.aux))
                                    return match_result::Overflow;
                                pos = p;
                                pc = e.pc + 2u;
                            }
                            goto resume;
                        case EntryStarLazy:
                            {
                                // (take one more character)
                                auto p = e.pos;
                                if (!step(code[e.pc + 1u], p))
                                    continue;
                                auto const count = static_cast<std::uint32_t>(e.aux) + 1u;
                                if (count < code[e.pc].b && !push(EntryStarLazy, e.pc, p, static_cast<std::int32_t>(count)))
                                    return match_result::Overflow;
                                pos = p;
                                pc = e.pc + 2u;
                            }
                            goto resume;
                        }
                    }
                resume:
                    ;
                }
            }
        private:
            bool read_forward(std::int32_t& aPos, char32_t& aChar, std::int32_t aLimit) const
            {
                if (aPos >= aLimit)
                    return false;
                aChar = iInput[aPos];
                if constexpr (Unicode)
                {
                    if (is_high_surrogate(aChar) && aPos + 1 < aLimit && is_low_surrogate(iInput[aPos + 1]))
                    {
                        aChar = combine_surrogates(aChar, iInput[aPos + 1]);
                        aPos += 2;
                        return true;
                    }
                }
                ++aPos;
                return true;
            }
            bool read_backward(std::int32_t& aPos, char32_t& aChar, std::int32_t aLimit) const
            {
                if (aPos <= aLimit)
                    return false;
                aChar = iInput[aPos - 1];
                if constexpr (Unicode)
                {
                    if (is_low_surrogate(aChar) && aPos - 2 >= aLimit && is_high_surrogate(iInput[aPos - 2]))
                    {
                        aChar = combine_surrogates(iInput[aPos - 2], aChar);
                        aPos -= 2;
                        return true;
                    }
                }
                --aPos;
                return true;
            }
            char32_t canonical(char32_t aChar) const
            {
                if constexpr (Unicode)
                    return simple_fold(aChar);
                else
                    return iLegacy[aChar];
            }
            bool test(instruction const& aInstruction, char32_t aChar) const
            {
                switch (aInstruction.op)
                {
                case opcode::Char:
                    return aChar == aInstruction.a;
                case opcode::CharI:
                    return canonical(aChar) == aInstruction.a;
                case opcode::Class:
                    {
                        auto const& cls = iProgram.classes[aInstruction.a];
                        auto const c = cls.ignoreCase ? canonical(aChar) : aChar;
                        bool const in = c < 128u ? ((cls.ascii[c >> 6u] >> (c & 63u)) & 1u) != 0u : contains(cls.ranges, c);
                        return in != cls.invert;
                    }
                case opcode::Dot:
                    return !is_line_terminator(aChar);
                default:
                    return true;
                }
            }
            bool step(instruction const& aInstruction, std::int32_t& aPos) const
            {
                auto p = aPos;
                char32_t c;
                if ((aInstruction.flags & InstructionBackward) == 0u)
                {
                    if (!read_forward(p, c, iLength))
                        return false;
                }
                else if (!read_backward(p, c, 0))
                    return false;
                if (!test(aInstruction, c))
                    return false;
                aPos = p;
                return true;
            }
            bool is_word(std::int32_t aIndex, bool aExtra) const
            {
                if (aIndex < 0 || aIndex >= iLength)
                    return false;
                char32_t const c = iInput[aIndex];
                if (is_ascii_word(c))
                    return true;
                return aExtra && c >= 128u && contains(unicode_case().extraWordCharacters, c);
            }
            bool backreference(instruction const& aInstruction, std::int32_t& aPos) const
            {
                auto const& slots = iState.slots;
                std::int32_t start = -1;
                std::int32_t end = -1;
                for (auto const g : iProgram.backreferences[aInstruction.a])
                    if (slots[g * 2u] != -1 && slots[g * 2u + 1u] != -1)
                    {
                        start = slots[g * 2u];
                        end = slots[g * 2u + 1u];
                        break;
                    }
                if (start == -1)
                    return true;
                auto const length = end - start;
                bool const backward = (aInstruction.flags & InstructionBackward) != 0u;
                auto const at = backward ? aPos - length : aPos;
                if (at < 0 || at + length > iLength)
                    return false;
                if ((aInstruction.flags & InstructionIgnoreCase) == 0u)
                {
                    if (!std::equal(iInput + start, iInput + end, iInput + at))
                        return false;
                }
                else
                {
                    auto p = start;
                    auto q = at;
                    while (p < end)
                    {
                        char32_t a;
                        char32_t b;
                        auto const p0 = p;
                        auto const q0 = q;
                        if (!read_forward(p, a, end) || !read_forward(q, b, at + length))
                            return false;
                        if (p - p0 != q - q0 || canonical(a) != canonical(b))
                            return false;
                    }
                }
                if constexpr (Unicode)
                {
                    // (the input's characters are code points: the match must not end or start inside a surrogate pair)
                    if (!backward && at + length < iLength && length > 0 && is_low_surrogate(iInput[at + length]) && is_high_surrogate(iInput[at + length - 1]))
                        return false;
                    if (backward && at > 0 && length > 0 && is_low_surrogate(iInput[at]) && is_high_surrogate(iInput[at - 1]))
                        return false;
                }
                aPos = backward ? at : at + length;
                return true;
            }
        private:
            program const& iProgram;
            char16_t const* iInput;
            std::int32_t iLength;
            matcher_state& iState;
            char16_t const* iLegacy;
        };

        template <bool Unicode>
        match_result search(program const& aProgram, std::u16string_view aInput, std::int32_t aStart, bool aSticky, matcher_state& aState)
        {
            matcher<Unicode> m{ aProgram, aInput, aState };
            auto const length = static_cast<std::int32_t>(aInput.size());
            auto pos = aStart;
            if (aProgram.anchored && pos != 0)
                return match_result::NoMatch;
            auto const& firstSet = aProgram.firstSet;
            for (;;)
            {
                if (!aSticky)
                {
                    if (!aProgram.prefix.empty())
                    {
                        auto const found = find_prefix(aProgram, aInput, static_cast<std::size_t>(pos));
                        if (found == std::u16string_view::npos)
                            return match_result::NoMatch;
                        pos = static_cast<std::int32_t>(found);
                    }
                    else if (aProgram.hasFirstChar)
                    {
                        auto const found = aInput.find(aProgram.firstChar, static_cast<std::size_t>(pos));
                        if (found == std::u16string_view::npos)
                            return match_result::NoMatch;
                        pos = static_cast<std::int32_t>(found);
                    }
                    else if (aProgram.hasFirstSet)
                    {
                        while (pos < length)
                        {
                            char32_t const c = aInput[pos];
                            if (c < 128u ? ((firstSet.ascii[c >> 6u] >> (c & 63u)) & 1u) != 0u : contains(firstSet.ranges, c))
                                break;
                            ++pos;
                        }
                        if (pos >= length)
                            return match_result::NoMatch;
                    }
                    if (Unicode && pos > aStart && is_low_surrogate(aInput[pos]) && is_high_surrogate(aInput[pos - 1]))
                    {
                        ++pos;
                        continue;
                    }
                }
                auto const result = m.run(pos);
                if (result != match_result::NoMatch)
                    return result;
                if (aSticky || pos >= length || aProgram.anchored)
                    return match_result::NoMatch;
                if (Unicode && is_high_surrogate(aInput[pos]) && pos + 1 < length && is_low_surrogate(aInput[pos + 1]))
                    pos += 2;
                else
                    ++pos;
            }
        }
    }

    std::optional<std::uint32_t> parse_flags(std::u16string_view aFlags)
    {
        std::uint32_t result = 0u;
        for (auto const c : aFlags)
        {
            std::uint32_t flag = 0u;
            switch (c)
            {
            case u'd': flag = FlagHasIndices; break;
            case u'g': flag = FlagGlobal; break;
            case u'i': flag = FlagIgnoreCase; break;
            case u'm': flag = FlagMultiline; break;
            case u's': flag = FlagDotAll; break;
            case u'u': flag = FlagUnicode; break;
            case u'v': flag = FlagUnicodeSets; break;
            case u'y': flag = FlagSticky; break;
            default: return {};
            }
            if ((result & flag) != 0u)
                return {};
            result |= flag;
        }
        return result;
    }

    std::shared_ptr<program const> compile(std::u16string_view aPattern, std::uint32_t aFlags)
    {
        if ((aFlags & FlagUnicode) != 0u && (aFlags & FlagUnicodeSets) != 0u)
            throw syntax_error{ "Invalid regular expression flags" };
        auto result = std::make_shared<program>();
        result->flags = aFlags;
        parser p{ aPattern, aFlags, *result };
        auto const* root = p.parse();
        bool const unicode = p.unicode_mode();
        code_generator{ *result, unicode }.generate(root);
        result->anchored = anchored_at_start(root);
        literal_prefix(root, result->prefix);
        if (result->prefix.size() > 255u)
            result->prefix.resize(255u);
        if (result->prefix.size() < 2u)
            result->prefix.clear();
        else
        {
            auto const m = result->prefix.size();
            result->prefixSkip.fill(static_cast<std::uint16_t>(m));
            for (std::size_t j = 0u; j + 1u < m; ++j)
                result->prefixSkip[result->prefix[j] & 0xFFu] = static_cast<std::uint16_t>(m - 1u - j);
        }
        range_list first;
        bool empty = false;
        bool known = true;
        first_characters(*result, root, unicode, first, empty, known);
        if (known && !empty)
        {
            normalize(first);
            if (first.size() == 1u && first[0].first == first[0].last)
            {
                result->hasFirstChar = true;
                result->firstChar = static_cast<char16_t>(first[0].first);
            }
            else
            {
                result->hasFirstSet = true;
                for (auto const& r : first)
                    for (char32_t c = r.first; c <= r.last && c < 128u; ++c)
                        result->firstSet.ascii[c >> 6u] |= 1ull << (c & 63u);
                result->firstSet.ranges = std::move(first);
            }
        }
        return result;
    }

    match_result exec(program const& aProgram, std::u16string_view aInput, std::size_t aStart, bool aSticky, std::vector<std::int32_t>& aCaptures)
    {
        if (aStart > aInput.size())
            return match_result::NoMatch;
        auto& state = tState;
        state.slots.assign((aProgram.captureCount + 1u) * 2u, -1);
        state.registers.assign(aProgram.registers * 2u, 0);
        bool const unicode = (aProgram.flags & (FlagUnicode | FlagUnicodeSets)) != 0u;
        auto const result = unicode ?
            search<true>(aProgram, aInput, static_cast<std::int32_t>(aStart), aSticky, state) :
            search<false>(aProgram, aInput, static_cast<std::int32_t>(aStart), aSticky, state);
        if (result == match_result::Match)
            aCaptures = state.slots;
        state.stack.clear();
        if (state.stack.capacity() > (1u << 16u))
            state.stack.shrink_to_fit();
        return result;
    }
}
