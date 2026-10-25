/*
  lib_string.cpp

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

// The string library: Lua's patterns (a backtracking matcher of the patterns that the reference manual (§6.4.1) describes),
// string.format, string.pack and string.unpack, and the others
//
// The pattern matcher follows the design of the matcher in the Lua reference implementation's lstrlib.c (Copyright (c) 1994-2023
// Lua.org, PUC-Rio, MIT licence; see runtime/lua/NOTICE).

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstring>
#include <limits>
#include "lua.hpp"
#include "api.hpp"

namespace neos::lua_runtime
{
    namespace
    {
        // --- positions

        // (a relative initial position (negative: from the end) as a position from 1 (0 if it is before the start))
        std::int64_t start_position(std::int64_t aPosition, std::size_t aLength)
        {
            auto const length = static_cast<std::int64_t>(aLength);
            if (aPosition > 0)
                return aPosition;
            if (aPosition == 0)
                return 1;
            if (aPosition < -length)
                return 1;
            return length + aPosition + 1;
        }

        // (a relative end position (negative: from the end), clipped to the length)
        std::int64_t end_position(std::int64_t aPosition, std::size_t aLength)
        {
            auto const length = static_cast<std::int64_t>(aLength);
            if (aPosition > length)
                return length;
            if (aPosition >= 0)
                return aPosition;
            if (aPosition < -length)
                return 0;
            return length + aPosition + 1;
        }

        // --- patterns

        constexpr int MaxCaptures = 32;
        constexpr int CapturePosition = -2;
        constexpr int CaptureUnfinished = -1;
        constexpr int MaxMatchDepth = 200;

        class matcher
        {
        public:
            matcher(native_call& aCall, std::string_view aSource, std::string_view aPattern) :
                iCall{ aCall }, iSource{ aSource }, iPattern{ aPattern }
            {
            }
        public:
            void reset()
            {
                iLevel = 0;
                iDepth = 0;
            }
            // (a match at the position: its end (npos: none))
            std::size_t match(std::size_t aSource, std::size_t aPattern)
            {
                if (++iDepth > MaxMatchDepth)
                    error("pattern too complex");
                struct depth_guard { int& depth; ~depth_guard() { --depth; } } const guard{ iDepth };
                for (;;)
                {
                    if (aPattern == iPattern.size())
                        return aSource;
                    char const p = iPattern[aPattern];
                    if (p == '(')
                    {
                        if (aPattern + 1u < iPattern.size() && iPattern[aPattern + 1u] == ')')
                            return start_capture(aSource, aPattern + 2u, CapturePosition);
                        return start_capture(aSource, aPattern + 1u, CaptureUnfinished);
                    }
                    if (p == ')')
                        return end_capture(aSource, aPattern + 1u);
                    if (p == '$' && aPattern + 1u == iPattern.size())
                        return aSource == iSource.size() ? aSource : npos;
                    if (p == '%' && aPattern + 1u < iPattern.size())
                    {
                        char const n = iPattern[aPattern + 1u];
                        if (n == 'b')
                        {
                            aSource = match_balance(aSource, aPattern + 2u);
                            if (aSource == npos)
                                return npos;
                            aPattern += 4u;
                            continue;
                        }
                        if (n == 'f')
                        {
                            aPattern += 2u;
                            if (aPattern >= iPattern.size() || iPattern[aPattern] != '[')
                                error("missing '[' after '%f' in pattern");
                            auto const end = class_end(aPattern);
                            unsigned char const previous = aSource == 0u ? '\0' : static_cast<unsigned char>(iSource[aSource - 1u]);
                            unsigned char const current = aSource < iSource.size() ? static_cast<unsigned char>(iSource[aSource]) : '\0';
                            if (!match_set(previous, aPattern, end - 1u) && match_set(current, aPattern, end - 1u))
                            {
                                aPattern = end;
                                continue;
                            }
                            return npos;
                        }
                        if (std::isdigit(static_cast<unsigned char>(n)))
                        {
                            aSource = match_capture(aSource, n);
                            if (aSource == npos)
                                return npos;
                            aPattern += 2u;
                            continue;
                        }
                    }
                    // (a single character class, and its repetition)
                    auto const end = class_end(aPattern);
                    bool const matches = aSource < iSource.size() && single_match(static_cast<unsigned char>(iSource[aSource]), aPattern, end);
                    char const q = end < iPattern.size() ? iPattern[end] : '\0';
                    if (q == '?')
                    {
                        if (matches)
                        {
                            auto const r = match(aSource + 1u, end + 1u);
                            if (r != npos)
                                return r;
                        }
                        aPattern = end + 1u;
                        continue;
                    }
                    if (q == '+')
                        return matches ? max_expand(aSource + 1u, aPattern, end) : npos;
                    if (q == '*')
                        return max_expand(aSource, aPattern, end);
                    if (q == '-')
                        return min_expand(aSource, aPattern, end);
                    if (!matches)
                        return npos;
                    ++aSource;
                    aPattern = end;
                }
            }
            int level() const { return iLevel; }
            // (a capture's value (an index (0-based) of iLevel's, or the whole match if there are none))
            value capture(int aIndex, std::size_t aStart, std::size_t aEnd)
            {
                if (aIndex >= iLevel)
                {
                    if (aIndex != 0)
                        error("invalid capture index %" + std::to_string(aIndex + 1));
                    return make_string(iSource.substr(aStart, aEnd - aStart));
                }
                auto const& c = iCaptures[aIndex];
                if (c.length == CaptureUnfinished)
                    error("unfinished capture");
                if (c.length == CapturePosition)
                    return make_integer(static_cast<std::int64_t>(c.start) + 1);
                return make_string(iSource.substr(c.start, static_cast<std::size_t>(c.length)));
            }
            // (the captures (or the whole match): pushed as results; their number)
            std::size_t push_captures(std::size_t aStart, std::size_t aEnd, bool aWholeIfNone)
            {
                auto const count = (iLevel == 0 && aWholeIfNone) ? 1 : iLevel;
                for (int i = 0; i < count; ++i)
                    iCall.ret(capture(i, aStart, aEnd));
                return static_cast<std::size_t>(count);
            }
            [[noreturn]] void error(std::string const& aMessage)
            {
                if (iCall.direct)
                    runtime_error(aMessage);
                throw_value(make_string(aMessage));
            }
            static constexpr std::size_t npos = std::string_view::npos;
        private:
            std::size_t class_end(std::size_t aPattern)
            {
                auto p = aPattern;
                char const c = iPattern[p++];
                if (c == '%')
                {
                    if (p >= iPattern.size())
                        error("malformed pattern (ends with '%')");
                    return p + 1u;
                }
                if (c == '[')
                {
                    if (p < iPattern.size() && iPattern[p] == '^')
                        ++p;
                    do
                    {
                        // (look for a ']')
                        if (p >= iPattern.size())
                            error("malformed pattern (missing ']')");
                        char const k = iPattern[p++];
                        if (k == '%')
                        {
                            if (p >= iPattern.size())
                                error("malformed pattern (missing ']')");
                            ++p;
                        }
                    } while (p >= iPattern.size() || iPattern[p] != ']');
                    return p + 1u;
                }
                return p;
            }
            static bool match_class(unsigned char c, unsigned char aClass)
            {
                bool result;
                switch (std::tolower(aClass))
                {
                case 'a': result = std::isalpha(c) != 0; break;
                case 'c': result = std::iscntrl(c) != 0; break;
                case 'd': result = std::isdigit(c) != 0; break;
                case 'g': result = std::isgraph(c) != 0; break;
                case 'l': result = std::islower(c) != 0; break;
                case 'p': result = std::ispunct(c) != 0; break;
                case 's': result = std::isspace(c) != 0; break;
                case 'u': result = std::isupper(c) != 0; break;
                case 'w': result = std::isalnum(c) != 0; break;
                case 'x': result = std::isxdigit(c) != 0; break;
                default: return aClass == c;
                }
                if (std::isupper(aClass))
                    result = !result;
                return result;
            }
            // (a set: [ ... ] from aPattern (its '[') to aLast (its ']'))
            bool match_set(unsigned char c, std::size_t aPattern, std::size_t aLast) const
            {
                bool positive = true;
                auto p = aPattern + 1u;
                if (iPattern[p] == '^')
                {
                    positive = false;
                    ++p;
                }
                while (p < aLast)
                {
                    auto const k = static_cast<unsigned char>(iPattern[p]);
                    if (k == '%')
                    {
                        ++p;
                        if (match_class(c, static_cast<unsigned char>(iPattern[p])))
                            return positive;
                        ++p;
                    }
                    else if (p + 2u < aLast && iPattern[p + 1u] == '-')
                    {
                        if (static_cast<unsigned char>(iPattern[p]) <= c && c <= static_cast<unsigned char>(iPattern[p + 2u]))
                            return positive;
                        p += 3u;
                    }
                    else
                    {
                        if (k == c)
                            return positive;
                        ++p;
                    }
                }
                return !positive;
            }
            bool single_match(unsigned char c, std::size_t aPattern, std::size_t aEnd) const
            {
                switch (iPattern[aPattern])
                {
                case '.': return true;
                case '%': return match_class(c, static_cast<unsigned char>(iPattern[aPattern + 1u]));
                case '[': return match_set(c, aPattern, aEnd - 1u);
                default: return static_cast<unsigned char>(iPattern[aPattern]) == c;
                }
            }
            std::size_t max_expand(std::size_t aSource, std::size_t aPattern, std::size_t aEnd)
            {
                std::size_t count = 0u;
                while (aSource + count < iSource.size() && single_match(static_cast<unsigned char>(iSource[aSource + count]), aPattern, aEnd))
                    ++count;
                for (;;)
                {
                    auto const r = match(aSource + count, aEnd + 1u);
                    if (r != npos)
                        return r;
                    if (count == 0u)
                        return npos;
                    --count;
                }
            }
            std::size_t min_expand(std::size_t aSource, std::size_t aPattern, std::size_t aEnd)
            {
                for (;;)
                {
                    auto const r = match(aSource, aEnd + 1u);
                    if (r != npos)
                        return r;
                    if (aSource < iSource.size() && single_match(static_cast<unsigned char>(iSource[aSource]), aPattern, aEnd))
                        ++aSource;
                    else
                        return npos;
                }
            }
            std::size_t start_capture(std::size_t aSource, std::size_t aPattern, int aWhat)
            {
                if (iLevel >= MaxCaptures)
                    error("too many captures");
                iCaptures[iLevel].start = aSource;
                iCaptures[iLevel].length = aWhat;
                ++iLevel;
                auto const r = match(aSource, aPattern);
                if (r == npos)
                    --iLevel;
                return r;
            }
            std::size_t end_capture(std::size_t aSource, std::size_t aPattern)
            {
                int open = -1;
                for (int i = iLevel - 1; i >= 0; --i)
                    if (iCaptures[i].length == CaptureUnfinished)
                    {
                        open = i;
                        break;
                    }
                if (open < 0)
                    error("invalid pattern capture");
                iCaptures[open].length = static_cast<std::ptrdiff_t>(aSource - iCaptures[open].start);
                auto const r = match(aSource, aPattern);
                if (r == npos)
                    iCaptures[open].length = CaptureUnfinished;
                return r;
            }
            std::size_t match_balance(std::size_t aSource, std::size_t aPattern)
            {
                if (aPattern + 1u >= iPattern.size())
                    error("malformed pattern (missing arguments to '%b')");
                if (aSource >= iSource.size() || iSource[aSource] != iPattern[aPattern])
                    return npos;
                char const open = iPattern[aPattern];
                char const close = iPattern[aPattern + 1u];
                int depth = 1;
                for (auto s = aSource + 1u; s < iSource.size(); ++s)
                {
                    if (iSource[s] == close)
                    {
                        if (--depth == 0)
                            return s + 1u;
                    }
                    else if (iSource[s] == open)
                        ++depth;
                }
                return npos;
            }
            std::size_t match_capture(std::size_t aSource, char aIndex)
            {
                int const index = aIndex - '1';
                if (index < 0 || index >= iLevel || iCaptures[index].length == CaptureUnfinished)
                    error("invalid capture index %" + std::to_string(index + 1));
                auto const length = static_cast<std::size_t>(iCaptures[index].length);
                if (iSource.size() - aSource >= length && iSource.compare(aSource, length, iSource.substr(iCaptures[index].start, length)) == 0)
                    return aSource + length;
                return npos;
            }
        private:
            native_call& iCall;
            std::string_view iSource;
            std::string_view iPattern;
            int iLevel = 0;
            int iDepth = 0;
            struct capture_t
            {
                std::size_t start = 0u;
                std::ptrdiff_t length = 0;
            } iCaptures[MaxCaptures];
        };

        bool has_specials(std::string_view aPattern)
        {
            return aPattern.find_first_of("^$*+?.([%-") != std::string_view::npos;
        }

        void find_aux(native_call& c, bool aFind)
        {
            auto const source = c.arg(0u);
            auto const s = check_string(c, 0u);
            auto const pattern = check_string(c, 1u);
            value_list keep{ source, c.arg(1u) };
            auto const init = start_position(optional_integer(c, 2u, 1), s.size());
            if (init > static_cast<std::int64_t>(s.size()) + 1)
            {
                c.ret(Nil);
                return;
            }
            if (aFind && (truthy(c.arg(3u)) || !has_specials(pattern)))
            {
                // (a plain search)
                auto const position = s.find(pattern, static_cast<std::size_t>(init - 1));
                if (position == std::string_view::npos)
                {
                    c.ret(Nil);
                    return;
                }
                c.ret(make_integer(static_cast<std::int64_t>(position) + 1));
                c.ret(make_integer(static_cast<std::int64_t>(position + pattern.size())));
                return;
            }
            matcher m{ c, s, pattern };
            bool const anchor = !pattern.empty() && pattern[0] == '^';
            std::size_t p = anchor ? 1u : 0u;
            auto start = static_cast<std::size_t>(init - 1);
            do
            {
                m.reset();
                auto const end = m.match(start, p);
                if (end != matcher::npos)
                {
                    if (aFind)
                    {
                        c.ret(make_integer(static_cast<std::int64_t>(start) + 1));
                        c.ret(make_integer(static_cast<std::int64_t>(end)));
                        m.push_captures(start, end, false);
                    }
                    else
                        m.push_captures(start, end, true);
                    return;
                }
            } while (start++ < s.size() && !anchor);
            c.ret(Nil);
        }

        void str_find(native_call& c)
        {
            find_aux(c, true);
        }

        void str_match(native_call& c)
        {
            find_aux(c, false);
        }

        void gmatch_iterator(native_call& c)
        {
            // (upvalues: the string, the pattern and a box of the position and the end of the last match)
            auto const source = c.self.upvalues[0];
            auto const pattern = c.self.upvalues[1];
            auto& state = cell_of<box_cell>(c.self.upvalues[2]);
            auto& position = cell_of<box_cell>(c.self.upvalues[3]);
            auto const s = str_view(source);
            auto const p = str_view(pattern);
            matcher m{ c, s, p };
            auto start = static_cast<std::size_t>(integer_of(position.v));
            auto const lastMatch = is_nil(state.v) ? matcher::npos : static_cast<std::size_t>(integer_of(state.v));
            for (; start <= s.size(); ++start)
            {
                m.reset();
                auto const end = m.match(start, 0u);
                if (end != matcher::npos && end != lastMatch)
                {
                    position.v = make_integer(static_cast<std::int64_t>(end));
                    state.v = make_integer(static_cast<std::int64_t>(end));
                    m.push_captures(start, end, true);
                    return;
                }
            }
            position.v = make_integer(static_cast<std::int64_t>(s.size()) + 1);
            c.ret(Nil);
        }

        void str_gmatch(native_call& c)
        {
            auto const s = check_string_value(c, 0u);
            auto const p = check_string_value(c, 1u);
            value_list keep{ s, p };
            auto const init = start_position(optional_integer(c, 2u, 1), str(s).size());
            auto const start = std::min<std::int64_t>(init - 1, static_cast<std::int64_t>(str(s).size()) + 1);
            auto const state = make_box(Nil);
            keep.push_back(state);
            auto const position = make_box(make_integer(start));
            keep.push_back(position);
            c.ret(make_native(&gmatch_iterator, "gmatch_iterator", { s, p, state, position }));
        }

        void add_value(native_call& c, matcher& m, std::string& aResult, std::size_t aStart, std::size_t aEnd, value aReplacement,
            std::string_view aSource)
        {
            value v;
            if (is_string(aReplacement) || is_number(aReplacement))
            {
                // (a string: %0-%9 are captures, %% a %)
                auto const text = is_string(aReplacement) ? str(aReplacement) : number_text(aReplacement);
                for (std::size_t i = 0u; i < text.size(); ++i)
                {
                    char const ch = text[i];
                    if (ch != '%')
                    {
                        aResult.push_back(ch);
                        continue;
                    }
                    if (++i >= text.size())
                        m.error("invalid use of '%' in replacement string");
                    char const n = text[i];
                    if (n == '%')
                        aResult.push_back('%');
                    else if (n == '0')
                        aResult.append(aSource.substr(aStart, aEnd - aStart));
                    else if (std::isdigit(static_cast<unsigned char>(n)))
                    {
                        auto const capture = m.capture(n - '1', aStart, aEnd);
                        if (is_string(capture))
                            aResult += str(capture);
                        else
                            aResult += number_text(capture);
                    }
                    else
                        m.error("invalid use of '%' in replacement string");
                }
                return;
            }
            auto const first = m.capture(0, aStart, aEnd);
            value_list keep{ first };
            if (is_table(aReplacement))
                v = index(aReplacement, first);
            else
            {
                value_list arguments;
                auto const count = m.level() == 0 ? 1 : m.level();
                for (int i = 0; i < count; ++i)
                    arguments.push_back(m.capture(i, aStart, aEnd));
                v = call1(aReplacement, arguments.span());
            }
            if (!truthy(v))
            {
                // (false or nil: the original match)
                aResult.append(aSource.substr(aStart, aEnd - aStart));
                return;
            }
            if (is_string(v))
                aResult += str(v);
            else if (is_number(v))
                aResult += number_text(v);
            else
                m.error("invalid replacement value (a " + type_name(v) + ")");
        }

        void str_gsub(native_call& c)
        {
            auto const source = check_string_value(c, 0u);
            auto const pattern = check_string_value(c, 1u);
            value_list keep{ source, pattern };
            auto const s = str_view(source);
            auto const p = str_view(pattern);
            auto const replacement = c.arg(2u);
            if (!is_string(replacement) && !is_number(replacement) && !is_table(replacement) && !is_function(replacement))
                type_error(c, 2u, "string/function/table");
            auto const maximum = optional_integer(c, 3u, static_cast<std::int64_t>(s.size()) + 1);
            bool const anchor = !p.empty() && p[0] == '^';
            std::size_t const patternStart = anchor ? 1u : 0u;
            matcher m{ c, s, p };
            std::string result;
            std::size_t position = 0u;
            std::int64_t count = 0;
            while (count < maximum)
            {
                m.reset();
                auto const end = m.match(position, patternStart);
                if (end != matcher::npos)
                {
                    ++count;
                    add_value(c, m, result, position, end, replacement, s);
                }
                if (end != matcher::npos && end > position)
                    position = end;
                else if (position < s.size())
                    result.push_back(s[position++]);
                else
                    break;
                if (anchor)
                    break;
            }
            result.append(s.substr(std::min(position, s.size())));
            c.ret(make_string(std::move(result)));
            c.ret(make_integer(count));
        }

        // --- the others

        void str_len(native_call& c)
        {
            c.ret(make_integer(static_cast<std::int64_t>(check_string(c, 0u).size())));
        }

        void str_sub(native_call& c)
        {
            auto const s = check_string(c, 0u);
            auto const i = start_position(optional_integer(c, 1u, 1), s.size());
            auto const j = end_position(optional_integer(c, 2u, -1), s.size());
            if (i > j)
                c.ret(make_string(std::string_view{}));
            else
                c.ret(make_string(s.substr(static_cast<std::size_t>(i - 1), static_cast<std::size_t>(j - i + 1))));
        }

        void str_upper(native_call& c)
        {
            std::string s{ check_string(c, 0u) };
            for (auto& ch : s)
                ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            c.ret(make_string(std::move(s)));
        }

        void str_lower(native_call& c)
        {
            std::string s{ check_string(c, 0u) };
            for (auto& ch : s)
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            c.ret(make_string(std::move(s)));
        }

        void str_rep(native_call& c)
        {
            auto const s = check_string(c, 0u);
            auto const n = check_integer(c, 1u);
            auto const separator = optional_string(c, 2u, "");
            if (n <= 0)
            {
                c.ret(make_string(std::string_view{}));
                return;
            }
            auto const total = static_cast<double>(s.size() + separator.size()) * static_cast<double>(n);
            if (total >= static_cast<double>(std::numeric_limits<std::int32_t>::max()))
                runtime_error("resulting string too large");
            std::string result;
            result.reserve(static_cast<std::size_t>(total));
            for (std::int64_t i = 0; i < n; ++i)
            {
                if (i > 0)
                    result.append(separator);
                result.append(s);
            }
            c.ret(make_string(std::move(result)));
        }

        void str_reverse(native_call& c)
        {
            std::string s{ check_string(c, 0u) };
            std::reverse(s.begin(), s.end());
            c.ret(make_string(std::move(s)));
        }

        void str_byte(native_call& c)
        {
            auto const s = check_string(c, 0u);
            auto const i = start_position(optional_integer(c, 1u, 1), s.size());
            auto const j = end_position(optional_integer(c, 2u, i), s.size());
            for (auto k = i; k <= j; ++k)
                c.ret(make_integer(static_cast<unsigned char>(s[static_cast<std::size_t>(k - 1)])));
        }

        void str_char(native_call& c)
        {
            std::string result;
            for (std::size_t i = 0u; i < c.count(); ++i)
            {
                auto const v = check_integer(c, i);
                if (v < 0 || v > 255)
                    argument_error(c, i, "value out of range");
                result.push_back(static_cast<char>(v));
            }
            c.ret(make_string(std::move(result)));
        }

        void str_dump(native_call& c)
        {
            if (!is_function(c.arg(0u)))
                type_error(c, 0u, "function");
            runtime_error("unable to dump given function");
        }

        // --- format

        void add_quoted(std::string& aResult, std::string_view aText)
        {
            aResult.push_back('"');
            for (std::size_t i = 0u; i < aText.size(); ++i)
            {
                auto const ch = static_cast<unsigned char>(aText[i]);
                if (ch == '"' || ch == '\\' || ch == '\n')
                {
                    aResult.push_back('\\');
                    aResult.push_back(static_cast<char>(ch));
                }
                else if (ch == '\r')
                    aResult += "\\r";
                else if (ch == '\0' || std::iscntrl(ch))
                {
                    char buffer[8];
                    if (i + 1u < aText.size() && std::isdigit(static_cast<unsigned char>(aText[i + 1u])))
                        std::snprintf(buffer, sizeof(buffer), "\\%03d", static_cast<int>(ch));
                    else
                        std::snprintf(buffer, sizeof(buffer), "\\%d", static_cast<int>(ch));
                    aResult += buffer;
                }
                else
                    aResult.push_back(static_cast<char>(ch));
            }
            aResult.push_back('"');
        }

        void str_format(native_call& c)
        {
            auto const format = check_string(c, 0u);
            std::string result;
            std::size_t argument = 1u;
            for (std::size_t i = 0u; i < format.size(); ++i)
            {
                char const ch = format[i];
                if (ch != '%')
                {
                    result.push_back(ch);
                    continue;
                }
                if (++i >= format.size())
                    runtime_error("invalid conversion '%' to 'format'");
                if (format[i] == '%')
                {
                    result.push_back('%');
                    continue;
                }
                // (the specification: flags, width (2 digits at most), precision (2 digits at most), conversion)
                auto const specStart = i;
                while (i < format.size() && std::strchr("-+ #0", format[i]) != nullptr)
                    ++i;
                for (int k = 0; k < 2 && i < format.size() && std::isdigit(static_cast<unsigned char>(format[i])); ++k)
                    ++i;
                if (i < format.size() && format[i] == '.')
                {
                    ++i;
                    for (int k = 0; k < 2 && i < format.size() && std::isdigit(static_cast<unsigned char>(format[i])); ++k)
                        ++i;
                }
                if (i >= format.size())
                    runtime_error("invalid conversion '%" + std::string{ format.substr(specStart) } + "' to 'format'");
                auto const spec = std::string{ format.substr(specStart, i - specStart) };
                char const conversion = format[i];
                if (std::isdigit(static_cast<unsigned char>(conversion)))
                    runtime_error("invalid conversion '%" + spec + conversion + "' to 'format'");
                if (argument >= c.count() && conversion != '%')
                    argument_error(c, argument, "no value");
                char buffer[512];
                switch (conversion)
                {
                case 'c':
                    {
                        auto const v = check_integer(c, argument++);
                        std::snprintf(buffer, sizeof(buffer), ("%" + spec + "c").c_str(), static_cast<int>(v));
                        result.append(buffer, std::strlen(buffer) == 0u && v == 0 ? 1u : std::strlen(buffer));
                    }
                    break;
                case 'd':
                case 'i':
                    {
                        auto const index = argument++;
                        auto const n = c.arg(index);
                        std::int64_t v;
                        if (!to_integer(n, v, true))
                        {
                            value number;
                            if (to_number(n, number))
                                argument_error(c, index, "number has no integer representation");
                            type_error(c, index, "number");
                        }
                        std::snprintf(buffer, sizeof(buffer), ("%" + spec + "lld").c_str(), static_cast<long long>(v));
                        result += buffer;
                    }
                    break;
                case 'u':
                case 'o':
                case 'x':
                case 'X':
                    {
                        auto const index = argument++;
                        std::int64_t v;
                        if (!to_integer(c.arg(index), v, true))
                        {
                            value number;
                            if (to_number(c.arg(index), number))
                                argument_error(c, index, "number has no integer representation");
                            type_error(c, index, "number");
                        }
                        std::snprintf(buffer, sizeof(buffer), ("%" + spec + "ll" + conversion).c_str(), static_cast<unsigned long long>(v));
                        result += buffer;
                    }
                    break;
                case 'a':
                case 'A':
                case 'e':
                case 'E':
                case 'f':
                case 'F':
                case 'g':
                case 'G':
                    {
                        auto const v = check_number(c, argument++);
                        if (!std::isfinite(v) && conversion != 'a' && conversion != 'A')
                        {
                            // (as C99's printf: inf, -inf, nan (or uppercase))
                            std::string text = std::isnan(v) ? (std::signbit(v) ? "-nan" : "nan") : (v < 0.0 ? "-inf" : "inf");
                            if (std::isupper(static_cast<unsigned char>(conversion)))
                                for (auto& t : text)
                                    t = static_cast<char>(std::toupper(static_cast<unsigned char>(t)));
                            if (!std::signbit(v) && spec.find('+') != std::string::npos)
                                text = "+" + text;
                            else if (!std::signbit(v) && spec.find(' ') != std::string::npos)
                                text = " " + text;
                            // (its width (and '-'), as a string's)
                            std::size_t flags = 0u;
                            while (flags < spec.size() && std::strchr("-+ #0", spec[flags]) != nullptr)
                                ++flags;
                            auto const width = spec.substr(flags, spec.find('.') == std::string::npos ? std::string::npos : spec.find('.') - flags);
                            auto const stringSpec = std::string{ spec.substr(0u, flags).find('-') != std::string::npos ? "-" : "" } + width;
                            std::snprintf(buffer, sizeof(buffer), ("%" + stringSpec + "s").c_str(), text.c_str());
                            result += buffer;
                        }
                        else
                        {
                            std::snprintf(buffer, sizeof(buffer), ("%" + spec + conversion).c_str(), v);
                            result += buffer;
                        }
                    }
                    break;
                case 'p':
                    {
                        auto const v = c.arg(argument++);
                        auto* cell = cell_at(v);
                        if (cell == nullptr || is_string(v))
                            std::snprintf(buffer, sizeof(buffer), ("%" + spec + "s").c_str(), "(null)");
                        else
                            std::snprintf(buffer, sizeof(buffer), ("%" + spec + "p").c_str(), static_cast<void*>(cell));
                        result += buffer;
                    }
                    break;
                case 'q':
                    {
                        if (!spec.empty())
                            runtime_error("specifier '%q' cannot have modifiers");
                        auto const v = c.arg(argument++);
                        if (is_string(v))
                            add_quoted(result, str_view(v));
                        else if (is_integer(v))
                        {
                            auto const i = integer_of(v);
                            if (i == std::numeric_limits<std::int64_t>::min())
                                result += "0x8000000000000000";
                            else
                                result += integer_text(i);
                        }
                        else if (is_float(v))
                        {
                            auto const d = float_of(v);
                            if (std::isinf(d))
                                result += d > 0.0 ? "1e9999" : "-1e9999";
                            else if (std::isnan(d))
                                result += "(0/0)";
                            else
                            {
                                std::snprintf(buffer, sizeof(buffer), "%a", d);
                                result += buffer;
                            }
                        }
                        else if (is_nil(v) || is_boolean(v))
                            result += raw_tostring(v);
                        else
                            argument_error(c, argument - 1u, "value has no literal form");
                    }
                    break;
                case 's':
                    {
                        auto const text = tostring_text(c.arg(argument++));
                        if (spec.empty())
                            result += text;
                        else
                        {
                            if (text.find('\0') != std::string::npos)
                                argument_error(c, argument - 1u, "string contains zeros");
                            auto const needed = std::snprintf(nullptr, 0, ("%" + spec + "s").c_str(), text.c_str());
                            std::string formatted(static_cast<std::size_t>(needed) + 1u, '\0');
                            std::snprintf(formatted.data(), formatted.size(), ("%" + spec + "s").c_str(), text.c_str());
                            formatted.resize(static_cast<std::size_t>(needed));
                            result += formatted;
                        }
                    }
                    break;
                default:
                    runtime_error("invalid conversion '%" + spec + conversion + "' to 'format'");
                }
            }
            c.ret(make_string(std::move(result)));
        }

        // --- pack and unpack

        enum class pack_kind
        {
            Int, Uint, Float, Double, Number, Char, String, Zstr, Padding, PaddingAlign, Nop
        };

        struct pack_header
        {
            bool little = true;
            std::size_t maxAlign = 1u;
        };

        bool native_little()
        {
            std::uint16_t const one = 1u;
            return *reinterpret_cast<std::uint8_t const*>(&one) == 1u;
        }

        class pack_reader
        {
        public:
            pack_reader(native_call& aCall, std::string_view aFormat) : iCall{ aCall }, iFormat{ aFormat }
            {
                iHeader.little = native_little();
            }
            bool done() const { return iPosition >= iFormat.size(); }
            // (the next option: its kind, size and alignment)
            pack_kind next(std::size_t& aSize, std::size_t& aAlign)
            {
                char const ch = iFormat[iPosition++];
                aSize = 0u;
                pack_kind kind = pack_kind::Nop;
                switch (ch)
                {
                case 'b': aSize = 1u; kind = pack_kind::Int; break;
                case 'B': aSize = 1u; kind = pack_kind::Uint; break;
                case 'h': aSize = 2u; kind = pack_kind::Int; break;
                case 'H': aSize = 2u; kind = pack_kind::Uint; break;
                case 'l': aSize = 8u; kind = pack_kind::Int; break;
                case 'L': aSize = 8u; kind = pack_kind::Uint; break;
                case 'j': aSize = 8u; kind = pack_kind::Int; break;
                case 'J': aSize = 8u; kind = pack_kind::Uint; break;
                case 'T': aSize = sizeof(std::size_t); kind = pack_kind::Uint; break;
                case 'f': aSize = 4u; kind = pack_kind::Float; break;
                case 'd': aSize = 8u; kind = pack_kind::Double; break;
                case 'n': aSize = 8u; kind = pack_kind::Number; break;
                case 'i': aSize = optional_size(4u); kind = pack_kind::Int; break;
                case 'I': aSize = optional_size(4u); kind = pack_kind::Uint; break;
                case 's': aSize = optional_size(sizeof(std::size_t)); kind = pack_kind::String; break;
                case 'c':
                    aSize = optional_size(std::numeric_limits<std::size_t>::max());
                    if (aSize == std::numeric_limits<std::size_t>::max())
                        error("missing size for format option 'c'");
                    kind = pack_kind::Char;
                    break;
                case 'z': kind = pack_kind::Zstr; break;
                case 'x': aSize = 1u; kind = pack_kind::Padding; break;
                case 'X': kind = pack_kind::PaddingAlign; break;
                case ' ': break;
                case '<': iHeader.little = true; break;
                case '>': iHeader.little = false; break;
                case '=': iHeader.little = native_little(); break;
                case '!': iHeader.maxAlign = optional_size(8u); break;
                default: error(std::string{ "invalid format option '" } + ch + "'");
                }
                if ((kind == pack_kind::Int || kind == pack_kind::Uint) && (aSize < 1u || aSize > 16u))
                    error("integral size (" + std::to_string(aSize) + ") out of limits [1,16]");
                if (kind == pack_kind::String && (aSize < 1u || aSize > 16u))
                    error("integral size (" + std::to_string(aSize) + ") out of limits [1,16]");
                aAlign = aSize;
                if (kind == pack_kind::PaddingAlign)
                {
                    // (Xop: aligned as op is)
                    if (done())
                        error("invalid next option for option 'X'");
                    std::size_t size;
                    std::size_t align;
                    auto const saved = iPosition;
                    auto const nextKind = next(size, align);
                    iPosition = saved + 1u;
                    while (iPosition < iFormat.size() && std::isdigit(static_cast<unsigned char>(iFormat[iPosition])))
                        ++iPosition;
                    if (nextKind == pack_kind::Char || size == 0u)
                        error("invalid next option for option 'X'");
                    aAlign = size;
                    aSize = 0u;
                }
                if (aAlign <= 1u || kind == pack_kind::Char || kind == pack_kind::Zstr || kind == pack_kind::Padding || kind == pack_kind::Nop)
                    aAlign = 1u;
                else
                {
                    if (aAlign > iHeader.maxAlign)
                        aAlign = iHeader.maxAlign;
                    if ((aAlign & (aAlign - 1u)) != 0u)
                        error("format asks for alignment not power of 2");
                }
                return kind;
            }
            pack_header const& header() const { return iHeader; }
            [[noreturn]] void error(std::string const& aMessage)
            {
                argument_error(iCall, 0u, aMessage);
            }
        private:
            std::size_t optional_size(std::size_t aDefault)
            {
                if (iPosition >= iFormat.size() || !std::isdigit(static_cast<unsigned char>(iFormat[iPosition])))
                    return aDefault;
                std::size_t n = 0u;
                while (iPosition < iFormat.size() && std::isdigit(static_cast<unsigned char>(iFormat[iPosition])))
                {
                    n = n * 10u + static_cast<std::size_t>(iFormat[iPosition++] - '0');
                    if (n > 0x7FFFFFFFu)
                        error("size out of limits");
                }
                return n;
            }
        private:
            native_call& iCall;
            std::string_view iFormat;
            std::size_t iPosition = 0u;
            pack_header iHeader;
        };

        void pack_integer(std::string& aResult, std::uint64_t aValue, std::size_t aSize, bool aLittle, bool aNegative)
        {
            std::string bytes(aSize, '\0');
            for (std::size_t i = 0u; i < aSize; ++i)
            {
                std::uint8_t b = i < 8u ? static_cast<std::uint8_t>(aValue >> (i * 8u)) : (aNegative ? 0xFFu : 0u);
                bytes[aLittle ? i : aSize - 1u - i] = static_cast<char>(b);
            }
            aResult += bytes;
        }

        void str_pack(native_call& c)
        {
            auto const format = check_string(c, 0u);
            pack_reader reader{ c, format };
            std::string result;
            std::size_t argument = 1u;
            while (!reader.done())
            {
                std::size_t size;
                std::size_t align;
                auto const kind = reader.next(size, align);
                // (padding for alignment)
                if (align > 1u)
                    while (result.size() % align != 0u)
                        result.push_back('\0');
                auto const little = reader.header().little;
                switch (kind)
                {
                case pack_kind::Int:
                case pack_kind::Uint:
                    {
                        auto const v = check_integer(c, argument++);
                        if (size < 8u)
                        {
                            if (kind == pack_kind::Int)
                            {
                                auto const limit = std::int64_t{ 1 } << (size * 8u - 1u);
                                if (v < -limit || v >= limit)
                                    argument_error(c, argument - 1u, "integer overflow");
                            }
                            else if (static_cast<std::uint64_t>(v) >= (std::uint64_t{ 1 } << (size * 8u)))
                                argument_error(c, argument - 1u, "unsigned overflow");
                        }
                        pack_integer(result, static_cast<std::uint64_t>(v), size, little, kind == pack_kind::Int && v < 0);
                    }
                    break;
                case pack_kind::Float:
                    {
                        auto const f = static_cast<float>(check_number(c, argument++));
                        std::uint32_t bits;
                        std::memcpy(&bits, &f, sizeof(bits));
                        pack_integer(result, bits, 4u, little, false);
                    }
                    break;
                case pack_kind::Double:
                case pack_kind::Number:
                    {
                        auto const d = check_number(c, argument++);
                        std::uint64_t bits;
                        std::memcpy(&bits, &d, sizeof(bits));
                        pack_integer(result, bits, 8u, little, false);
                    }
                    break;
                case pack_kind::Char:
                    {
                        auto const s = check_string(c, argument++);
                        if (s.size() > size)
                            argument_error(c, argument - 1u, "string longer than given size");
                        result.append(s);
                        result.append(size - s.size(), '\0');
                    }
                    break;
                case pack_kind::String:
                    {
                        auto const s = check_string(c, argument++);
                        if (size < 8u && s.size() >= (std::size_t{ 1 } << (size * 8u)))
                            argument_error(c, argument - 1u, "string length does not fit in given size");
                        pack_integer(result, s.size(), size, little, false);
                        result.append(s);
                    }
                    break;
                case pack_kind::Zstr:
                    {
                        auto const s = check_string(c, argument++);
                        if (s.find('\0') != std::string_view::npos)
                            argument_error(c, argument - 1u, "string contains zeros");
                        result.append(s);
                        result.push_back('\0');
                    }
                    break;
                case pack_kind::Padding:
                    result.push_back('\0');
                    break;
                default:
                    break;
                }
            }
            c.ret(make_string(std::move(result)));
        }

        void str_packsize(native_call& c)
        {
            auto const format = check_string(c, 0u);
            pack_reader reader{ c, format };
            std::size_t total = 0u;
            while (!reader.done())
            {
                std::size_t size;
                std::size_t align;
                auto const kind = reader.next(size, align);
                if (kind == pack_kind::String || kind == pack_kind::Zstr)
                    argument_error(c, 0u, "variable-length format");
                if (align > 1u)
                    total = (total + align - 1u) / align * align;
                total += size;
            }
            c.ret(make_integer(static_cast<std::int64_t>(total)));
        }

        std::uint64_t unpack_integer(std::string_view aData, std::size_t aSize, bool aLittle, bool aSigned, native_call& c)
        {
            std::uint64_t v = 0u;
            for (std::size_t i = 0u; i < std::min<std::size_t>(aSize, 8u); ++i)
            {
                auto const b = static_cast<std::uint8_t>(aData[aLittle ? i : aSize - 1u - i]);
                v |= static_cast<std::uint64_t>(b) << (i * 8u);
            }
            if (aSize < 8u && aSigned)
            {
                auto const mask = std::uint64_t{ 1 } << (aSize * 8u - 1u);
                v = (v ^ mask) - mask;
            }
            else if (aSize > 8u)
            {
                // (the bytes beyond 8 must be the sign extension)
                for (std::size_t i = 8u; i < aSize; ++i)
                {
                    auto const b = static_cast<std::uint8_t>(aData[aLittle ? i : aSize - 1u - i]);
                    std::uint8_t const expected = (aSigned && static_cast<std::int64_t>(v) < 0) ? 0xFFu : 0u;
                    if (b != expected)
                        runtime_error(std::to_string(aSize) + "-byte integer does not fit into Lua Integer");
                }
            }
            (void)c;
            return v;
        }

        void str_unpack(native_call& c)
        {
            auto const format = check_string(c, 0u);
            auto const dataValue = check_string_value(c, 1u);
            value_list keep{ dataValue };
            auto const data = str_view(dataValue);
            auto position = static_cast<std::size_t>(start_position(optional_integer(c, 2u, 1), data.size()) - 1);
            if (position > data.size())
                argument_error(c, 2u, "initial position out of string");
            pack_reader reader{ c, format };
            while (!reader.done())
            {
                std::size_t size;
                std::size_t align;
                auto const kind = reader.next(size, align);
                if (align > 1u)
                    position = (position + align - 1u) / align * align;
                if ((kind != pack_kind::Zstr && kind != pack_kind::Nop) && size > data.size() - std::min(position, data.size()))
                    argument_error(c, 1u, "data string too short");
                auto const little = reader.header().little;
                switch (kind)
                {
                case pack_kind::Int:
                case pack_kind::Uint:
                    {
                        auto const v = unpack_integer(data.substr(position), size, little, kind == pack_kind::Int, c);
                        c.ret(make_integer(static_cast<std::int64_t>(v)));
                    }
                    break;
                case pack_kind::Float:
                    {
                        auto const bits = static_cast<std::uint32_t>(unpack_integer(data.substr(position), 4u, little, false, c));
                        float f;
                        std::memcpy(&f, &bits, sizeof(f));
                        c.ret(make_float(f));
                    }
                    break;
                case pack_kind::Double:
                case pack_kind::Number:
                    {
                        auto const bits = unpack_integer(data.substr(position), 8u, little, false, c);
                        double d;
                        std::memcpy(&d, &bits, sizeof(d));
                        c.ret(make_float(d));
                    }
                    break;
                case pack_kind::Char:
                    c.ret(make_string(data.substr(position, size)));
                    break;
                case pack_kind::String:
                    {
                        auto const length = unpack_integer(data.substr(position), size, little, false, c);
                        if (length > data.size() - position - size)
                            argument_error(c, 1u, "data string too short");
                        c.ret(make_string(data.substr(position + size, static_cast<std::size_t>(length))));
                        position += static_cast<std::size_t>(length);
                    }
                    break;
                case pack_kind::Zstr:
                    {
                        auto const end = data.find('\0', position);
                        if (end == std::string_view::npos)
                            argument_error(c, 1u, "unfinished string for format 'z'");
                        c.ret(make_string(data.substr(position, end - position)));
                        position = end + 1u;
                    }
                    break;
                default:
                    break;
                }
                position += size;
            }
            c.ret(make_integer(static_cast<std::int64_t>(position) + 1));
        }
    }

    void open_string()
    {
        auto& s = the_state();
        auto const t = library_table("string");
        register_library(t, "string", {
            { "byte", &str_byte },
            { "char", &str_char },
            { "dump", &str_dump },
            { "find", &str_find },
            { "format", &str_format },
            { "gmatch", &str_gmatch },
            { "gsub", &str_gsub },
            { "len", &str_len },
            { "lower", &str_lower },
            { "match", &str_match },
            { "rep", &str_rep },
            { "reverse", &str_reverse },
            { "sub", &str_sub },
            { "upper", &str_upper },
            { "pack", &str_pack },
            { "packsize", &str_packsize },
            { "unpack", &str_unpack } });
        // (strings' metatable: their methods)
        s.stringMetatable = make_table();
        raw_set(s.stringMetatable, name(NameIndex), t);
    }
}
