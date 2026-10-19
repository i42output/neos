/*
  tokens.hpp

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

// The tokens and logical lines of a module's source, as the rewrites of its source (closures.cpp, statements.cpp) see them

#include <cctype>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace neos::compilers::python::tokens
{
    struct token
    {
        enum kind_t { Name, Number, String, Op } kind;
        std::size_t begin;
        std::size_t end;
    };

    struct logical_line
    {
        std::size_t begin = 0u;         ///< (the start of its first physical line)
        std::size_t end = 0u;           ///< (its end: its newline (or the end of the text))
        std::size_t indent = 0u;        ///< (columns)
        std::vector<token> tokens;
    };

    inline bool name_start(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80u; }
    inline bool name_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80u; }

    inline std::set<std::string_view> const& keywords()
    {
        static std::set<std::string_view> const sKeywords = { "False", "None", "True", "and", "as", "assert", "async", "await", "break", "class",
            "continue", "def", "del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal",
            "not", "or", "pass", "raise", "return", "try", "while", "with", "yield" };
        return sKeywords;
    }

    // (the end of a string literal whose quote is at aStart)
    inline std::size_t string_end(std::string const& aText, std::size_t aStart)
    {
        char const q = aText[aStart];
        bool const triple = aStart + 2u < aText.size() && aText[aStart + 1u] == q && aText[aStart + 2u] == q;
        std::size_t j = aStart + (triple ? 3u : 1u);
        for (; j < aText.size(); ++j)
        {
            if (aText[j] == '\\')
                ++j;
            else if (aText[j] == q && (!triple || (j + 2u < aText.size() && aText[j + 1u] == q && aText[j + 2u] == q)))
                return triple ? j + 3u : j + 1u;
            else if (!triple && aText[j] == '\n')
                return j;
        }
        return aText.size();
    }

    inline std::vector<logical_line> tokenize(std::string const& aText)
    {
        std::vector<logical_line> lines;
        std::size_t i = 0u;
        auto const n = aText.size();
        while (i < n)
        {
            // (a physical line that starts a logical line: its indentation)
            auto const lineBegin = i;
            std::size_t indent = 0u;
            while (i < n && (aText[i] == ' ' || aText[i] == '\t' || aText[i] == '\f'))
            {
                indent = aText[i] == '\t' ? (indent / 8u + 1u) * 8u : indent + 1u;
                ++i;
            }
            if (i >= n)
                break;
            if (aText[i] == '\n' || aText[i] == '\r' || aText[i] == '#')
            {
                while (i < n && aText[i] != '\n')
                    ++i;
                ++i;
                continue;
            }
            logical_line line;
            line.begin = lineBegin;
            line.indent = indent;
            int depth = 0;
            while (i < n)
            {
                char const c = aText[i];
                if (c == ' ' || c == '\t' || c == '\f' || c == '\r')
                {
                    ++i;
                    continue;
                }
                if (c == '\\' && i + 1u < n && (aText[i + 1u] == '\n' || aText[i + 1u] == '\r'))
                {
                    i += aText[i + 1u] == '\r' && i + 2u < n && aText[i + 2u] == '\n' ? 3u : 2u;
                    continue;
                }
                if (c == '#')
                {
                    while (i < n && aText[i] != '\n')
                        ++i;
                    continue;
                }
                if (c == '\n')
                {
                    if (depth > 0)
                    {
                        ++i;
                        continue;
                    }
                    break;
                }
                if (c == '\'' || c == '\"')
                {
                    auto const end = string_end(aText, i);
                    line.tokens.push_back({ token::String, i, end });
                    i = end;
                    continue;
                }
                if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1u < n && std::isdigit(static_cast<unsigned char>(aText[i + 1u]))))
                {
                    auto const start = i;
                    bool const hex = c == '0' && i + 1u < n && (aText[i + 1u] == 'x' || aText[i + 1u] == 'X');
                    ++i;
                    while (i < n && (name_char(aText[i]) || aText[i] == '.' ||
                        (!hex && (aText[i] == '+' || aText[i] == '-') && (aText[i - 1u] == 'e' || aText[i - 1u] == 'E'))))
                        ++i;
                    line.tokens.push_back({ token::Number, start, i });
                    continue;
                }
                if (name_start(c))
                {
                    auto const start = i;
                    while (i < n && name_char(aText[i]))
                        ++i;
                    // (a string's prefix)
                    if (i < n && (aText[i] == '\'' || aText[i] == '\"') && i - start <= 2u &&
                        std::string_view{ aText }.substr(start, i - start).find_first_not_of("rRbBuUfF") == std::string_view::npos)
                    {
                        auto const end = string_end(aText, i);
                        line.tokens.push_back({ token::String, start, end });
                        i = end;
                        continue;
                    }
                    line.tokens.push_back({ token::Name, start, i });
                    continue;
                }
                static char const* const sOperators[] = { "**=", "//=", ">>=", "<<=", "...", "->", ":=", "==", "!=", "<=", ">=", "**", "//", "<<", ">>",
                    "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "@=" };
                std::size_t length = 1u;
                for (auto op : sOperators)
                    if (aText.compare(i, std::char_traits<char>::length(op), op) == 0)
                    {
                        length = std::char_traits<char>::length(op);
                        break;
                    }
                if (c == '(' || c == '[' || c == '{')
                    ++depth;
                else if ((c == ')' || c == ']' || c == '}') && depth > 0)
                    --depth;
                line.tokens.push_back({ token::Op, i, i + length });
                i += length;
            }
            line.end = i;
            if (!line.tokens.empty())
                lines.push_back(std::move(line));
            if (i < n)
                ++i;
        }
        return lines;
    }
}
