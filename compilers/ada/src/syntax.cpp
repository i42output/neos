/*
  syntax.cpp

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

// Ada: the source's own pass (see syntax.hpp)

#include <algorithm>
#include <cctype>
#include <set>
#include "syntax.hpp"

namespace neos::compilers::ada
{
    namespace
    {
        bool is_word_start(unsigned char c)
        {
            return std::isalpha(c) || c >= 0x80u;
        }

        bool is_word_char(unsigned char c)
        {
            return std::isalnum(c) || c == '_' || c >= 0x80u;
        }

        // a token of the source (for the pass: words, literals and delimiters)
        enum class token_kind { Word, Number, String, Character, Delimiter, End };

        struct token
        {
            token_kind kind;
            std::size_t begin;
            std::size_t end;
        };

        // the tokens of a (folded) source (comments skipped)
        std::vector<token> tokenize(std::string_view aSource)
        {
            std::vector<token> result;
            std::size_t i = 0u;
            auto const n = aSource.size();
            auto previous_allows_tick = [&]()
            {
                // (a tick after an identifier, ')', ']' or "all" is an attribute's or a qualified expression's)
                if (result.empty())
                    return false;
                auto const& t = result.back();
                if (t.kind == token_kind::Word)
                {
                    // (reserved words in any case (the source is not folded yet))
                    std::string word{ aSource.substr(t.begin, t.end - t.begin) };
                    for (auto& ch : word)
                        if (ch >= 'A' && ch <= 'Z')
                            ch = static_cast<char>(ch - 'A' + 'a');
                    static std::set<std::string_view> const sReserved = { "abort", "abs", "abstract", "accept", "access", "aliased", "and", "array", 
                        "at", "begin", "body", "case", "constant", "declare", "delay", "delta", "digits", "do", "else", "elsif", "end", "entry", 
                        "exception", "exit", "for", "function", "generic", "goto", "if", "in", "interface", "is", "limited", "loop", "mod", 
                        "new", "not", "null", "of", "or", "others", "out", "overriding", "package", "parallel", "pragma", "private", "procedure",
                        "protected", "raise", "range", "record", "rem", "renames", "requeue", "return", "reverse", "select", "separate", "some",
                        "subtype", "synchronized", "tagged", "task", "terminate", "then", "type", "until", "use", "when", "while", "with", "xor" };
                    return !sReserved.contains(std::string_view{ word });
                }
                if (t.kind == token_kind::Delimiter)
                {
                    auto const c = aSource[t.begin];
                    return c == ')' || c == ']';
                }
                return t.kind == token_kind::String;
            };
            while (i < n)
            {
                unsigned char const c = static_cast<unsigned char>(aSource[i]);
                if (c == '-' && i + 1u < n && aSource[i + 1u] == '-')
                {
                    while (i < n && aSource[i] != '\n' && aSource[i] != '\r')
                        ++i;
                    continue;
                }
                if (std::isspace(c))
                {
                    ++i;
                    continue;
                }
                auto const start = i;
                if (is_word_start(c))
                {
                    while (i < n && is_word_char(static_cast<unsigned char>(aSource[i])))
                        ++i;
                    result.push_back(token{ token_kind::Word, start, i });
                }
                else if (std::isdigit(c))
                {
                    while (i < n && (std::isalnum(static_cast<unsigned char>(aSource[i])) || aSource[i] == '_' || aSource[i] == '#' || 
                        (aSource[i] == '.' && i + 1u < n && aSource[i + 1u] != '.') || 
                        ((aSource[i] == '+' || aSource[i] == '-') && (aSource[i - 1u] == 'e' || aSource[i - 1u] == 'E'))))
                        ++i;
                    result.push_back(token{ token_kind::Number, start, i });
                }
                else if (c == '\"')
                {
                    ++i;
                    while (i < n)
                    {
                        if (aSource[i] == '\"')
                        {
                            if (i + 1u < n && aSource[i + 1u] == '\"')
                            {
                                i += 2u;
                                continue;
                            }
                            ++i;
                            break;
                        }
                        if (aSource[i] == '\n')
                            break;
                        ++i;
                    }
                    result.push_back(token{ token_kind::String, start, i });
                }
                else if (c == '\'' && !previous_allows_tick() && i + 2u < n)
                {
                    // (a character literal: one character (UTF-8: its bytes))
                    std::size_t j = i + 1u;
                    if (static_cast<unsigned char>(aSource[j]) >= 0xC0u)
                    {
                        ++j;
                        while (j < n && (static_cast<unsigned char>(aSource[j]) & 0xC0u) == 0x80u)
                            ++j;
                    }
                    else
                        ++j;
                    if (j < n && aSource[j] == '\'')
                    {
                        i = j + 1u;
                        result.push_back(token{ token_kind::Character, start, i });
                    }
                    else
                    {
                        ++i;
                        result.push_back(token{ token_kind::Delimiter, start, i });
                    }
                }
                else
                {
                    ++i;
                    result.push_back(token{ token_kind::Delimiter, start, i });
                }
            }
            return result;
        }
    }

    void fold_case(std::string& aSource)
    {
        // (a byte order mark: not part of the text (spaces, keeping the positions))
        if (aSource.size() >= 3u && aSource.compare(0u, 3u, "\xEF\xBB\xBF") == 0)
            aSource.replace(0u, 3u, "   ");
        for (auto const& t : tokenize(aSource))
            if (t.kind == token_kind::Word || t.kind == token_kind::Number)
                for (auto i = t.begin; i < t.end; ++i)
                {
                    auto& c = aSource[i];
                    if (c >= 'A' && c <= 'Z')
                        c = static_cast<char>(c - 'A' + 'a');
                    else if (static_cast<unsigned char>(c) == 0xC3u && i + 1u < t.end)
                    {
                        // (Latin-1's capital letters (UTF-8: C3 80 .. C3 9E, not the multiplication sign C3 97): their small 
                        // letters (C3 A0 .. C3 BE))
                        auto& d = aSource[i + 1u];
                        auto const u = static_cast<unsigned char>(d);
                        if (u >= 0x80u && u <= 0x9Eu && u != 0x97u)
                            d = static_cast<char>(u + 0x20u);
                        ++i;
                    }
                }
    }

    std::vector<std::string> units_named(std::string_view aSource)
    {
        std::vector<std::string> result;
        auto const tokens = tokenize(aSource);
        auto const text = [&](std::size_t aIndex) -> std::string_view
        {
            if (aIndex >= tokens.size())
                return {};
            return aSource.substr(tokens[aIndex].begin, tokens[aIndex].end - tokens[aIndex].begin);
        };
        auto const add = [&](std::string const& aName)
        {
            // (a unit and its parent units)
            for (auto dot = aName.find('.'); dot != std::string::npos; dot = aName.find('.', dot + 1u))
                if (std::find(result.begin(), result.end(), aName.substr(0u, dot)) == result.end())
                    result.push_back(aName.substr(0u, dot));
            if (std::find(result.begin(), result.end(), aName) == result.end())
                result.push_back(aName);
        };
        // (a dotted name at a token: its text and the token after it)
        auto const dotted = [&](std::size_t& aIndex) -> std::string
        {
            std::string name;
            while (aIndex < tokens.size() && tokens[aIndex].kind == token_kind::Word)
            {
                name += text(aIndex);
                ++aIndex;
                if (text(aIndex) == "." && aIndex + 1u < tokens.size() && tokens[aIndex + 1u].kind == token_kind::Word)
                {
                    name += '.';
                    ++aIndex;
                }
                else
                    break;
            }
            return name;
        };
        int depth = 0;  // (parentheses)
        for (std::size_t i = 0u; i < tokens.size(); ++i)
        {
            auto const t = text(i);
            if (t == "(")
                ++depth;
            else if (t == ")")
                --depth;
            // (a contract (an assertion (delimiters are single characters), a precondition, postcondition, type invariant or predicate): Ada.Assertions (its
            // Assertion_Error))
            if (tokens[i].kind == token_kind::Word)
            {
                static std::set<std::string_view> const sAspects = { "pre", "post", "type_invariant", "static_predicate", "dynamic_predicate", "predicate" };
                static std::set<std::string_view> const sPragmas = { "assert", "assume", "check", "precondition", "postcondition", "loop_invariant",
                    "assert_and_cut", "type_invariant", "predicate" };
                if ((sAspects.contains(t) && ((text(i + 1u) == "=" && text(i + 2u) == ">") || (text(i + 1u) == "'" && text(i + 2u) == "class"))) ||
                    (i > 0u && text(i - 1u) == "pragma" && sPragmas.contains(t)))
                    add("ada.assertions");
            }
            if (depth != 0)
                continue;
            auto const previous = i > 0u ? text(i - 1u) : std::string_view{ ";" };
            bool const unitStart = previous == ";" || previous == "limited" || previous == "private";
            if (t == "with" && unitStart)
            {
                auto const next = text(i + 1u);
                if (next == "procedure" || next == "function" || next == "package" || next == "record" || next == "private" || next == "null" || next == "type")
                    continue;
                // (a with clause: names separated by commas, then ';')
                std::size_t j = i + 1u;
                std::vector<std::string> names;
                for (;;)
                {
                    auto const name = dotted(j);
                    if (name.empty())
                        break;
                    names.push_back(name);
                    if (text(j) == ",")
                    {
                        ++j;
                        continue;
                    }
                    break;
                }
                if (text(j) == ";")
                {
                    for (auto const& name : names)
                        add(name);
                    i = j;
                }
            }
            else if (t == "separate" && unitStart && text(i + 1u) == "(")
            {
                std::size_t j = i + 2u;
                auto const name = dotted(j);
                if (!name.empty())
                    add(name);
            }
            else if ((t == "package" || t == "procedure" || t == "function") && unitStart)
            {
                // (a child unit: its parent units)
                std::size_t j = i + 1u;
                if (text(j) == "body")
                    ++j;
                auto const name = dotted(j);
                auto const dot = name.rfind('.');
                if (dot != std::string::npos)
                    add(name.substr(0u, dot));
            }
        }
        return result;
    }

    std::vector<std::string> stub_names(std::string_view aSource)
    {
        std::vector<std::string> result;
        auto const tokens = tokenize(aSource);
        auto const text = [&](std::size_t aIndex) -> std::string_view
        {
            if (aIndex >= tokens.size())
                return {};
            return aSource.substr(tokens[aIndex].begin, tokens[aIndex].end - tokens[aIndex].begin);
        };
        for (std::size_t i = 2u; i + 1u < tokens.size(); ++i)
        {
            if (text(i) != "separate" || text(i - 1u) != "is" || text(i + 1u) != ";")
                continue;
            // (the stub's name: after the nearest procedure, function or body before it)
            int depth = 0;  // (a parameter list's parentheses)
            for (std::size_t j = i - 1u; j-- > 0u;)
            {
                auto const t = text(j);
                if (t == ")")
                    ++depth;
                else if (t == "(")
                    --depth;
                if (depth > 0)
                    continue;
                if (t == ";")
                    break;
                if (t == "procedure" || t == "function" || t == "body")
                {
                    if (j + 1u < tokens.size() && tokens[j + 1u].kind == token_kind::Word)
                        result.emplace_back(text(j + 1u));
                    break;
                }
            }
        }
        return result;
    }

    std::string name_text(std::string_view aSource)
    {
        // (a character literal: as written ('Q' and 'q', ' ' and ':' are distinct literals))
        if (aSource.size() >= 3u && aSource.front() == '\'' && aSource.back() == '\'')
            return std::string{ aSource };
        std::string result;
        for (auto c : aSource)
            if (!std::isspace(static_cast<unsigned char>(c)) && c != ':')
                result += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        return result;
    }

    bool node::has_flag(std::string_view aFlag) const
    {
        for (auto* c : children)
            if (c->kind == construct::Flag)
            {
                std::string normalized;
                bool space = false;
                for (auto ch : c->source)
                    if (std::isspace(static_cast<unsigned char>(ch)))
                        space = true;
                    else
                    {
                        if (space && !normalized.empty())
                            normalized += ' ';
                        space = false;
                        normalized += ch;
                    }
                if (normalized == aFlag)
                    return true;
            }
        return false;
    }

    std::uint32_t source_file::line_of(std::string_view aSource) const
    {
        if (aSource.data() < text.data() || aSource.data() > text.data() + text.size())
            return 0u;
        return static_cast<std::uint32_t>(std::count(text.data(), aSource.data(), '\n') + 1);
    }

    std::uint32_t source_file::column_of(std::string_view aSource) const
    {
        if (aSource.data() < text.data() || aSource.data() > text.data() + text.size())
            return 0u;
        auto const* p = aSource.data();
        std::uint32_t column = 1u;
        while (p > text.data() && p[-1] != '\n')
        {
            --p;
            ++column;
        }
        return column;
    }

    std::string_view source_file::original_of(std::string_view aSource) const
    {
        if (aSource.data() < text.data() || aSource.data() + aSource.size() > text.data() + text.size() || original.size() != text.size())
            return aSource;
        return std::string_view{ original.data() + (aSource.data() - text.data()), aSource.size() };
    }
}
