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

  Portions of this file are derived from CPython 3.13 (Lib/traceback.py and Parser/lexer/lexer.c), Copyright (c)
  2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License
  Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from C and Python to C++ for the neos Python compiler
  (see runtime/python/NOTICE).
*/

// A module's syntax errors as CPython 3.13 reports them: those of its tokenizer (strings, brackets, indentation), of its parser (that the
// grammar does not reject, or whose messages are CPython's own), of its symbol table and of its compiler; the errors' locations (lines and
// character offsets) and the report of one (traceback.py's _format_syntax_error)

#include <neolib/neolib.hpp>
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <neos/bytecode/bytecode.hpp>
#include <neos/language/emit.hpp>
#include "compiler.hpp"
#include "tokens.hpp"

namespace neos::compilers::python
{
    namespace
    {
        using tokens::token;

        // (a position's line (1-based) and its character offset (1-based))
        std::pair<std::uint32_t, std::int64_t> location_of(std::string const& aSource, std::size_t aPosition)
        {
            aPosition = std::min(aPosition, aSource.size());
            std::uint32_t line = 1u;
            std::size_t lineStart = 0u;
            for (std::size_t i = 0u; i < aPosition; ++i)
                if (aSource[i] == '\n')
                {
                    ++line;
                    lineStart = i + 1u;
                }
            std::int64_t offset = 1;
            for (std::size_t i = lineStart; i < aPosition; ++i)
                if ((static_cast<unsigned char>(aSource[i]) & 0xC0u) != 0x80u)
                    ++offset;
            return { line, offset };
        }

        syntax_issue make_issue(std::string const& aSource, syntax_issue::stage_t aStage, std::string aMessage, std::size_t aBegin, std::size_t aEnd,
            char const* aType = "SyntaxError")
        {
            syntax_issue result;
            result.stage = aStage;
            result.type = aType;
            result.message = std::move(aMessage);
            result.position = aBegin;
            result.order = aBegin;
            result.endPosition = aEnd;
            auto const [line, offset] = location_of(aSource, aBegin);
            // (an end after a newline: after the newline on its line)
            auto [endLine, endOffset] = aEnd > aBegin && aSource[aEnd - 1u] == '\n' ? location_of(aSource, aEnd - 1u) : location_of(aSource, aEnd);
            if (aEnd > aBegin && aSource[aEnd - 1u] == '\n')
                ++endOffset;
            result.line = line;
            result.offset = offset;
            result.endLine = endLine;
            result.endOffset = endOffset;
            return result;
        }

        std::size_t line_end(std::string const& aSource, std::size_t aPosition)
        {
            auto const end = aSource.find('\n', aPosition);
            return end == std::string::npos ? aSource.size() : end;
        }

        std::size_t line_begin(std::string const& aSource, std::size_t aPosition)
        {
            aPosition = std::min(aPosition, aSource.size());
            while (aPosition > 0u && aSource[aPosition - 1u] != '\n')
                --aPosition;
            return aPosition;
        }

        // (a physical line's content (without its newline (or carriage return)))
        std::size_t content_end(std::string const& aSource, std::size_t aPosition)
        {
            auto end = line_end(aSource, aPosition);
            while (end > aPosition && aSource[end - 1u] == '\r')
                --end;
            return end;
        }

        std::uint32_t last_line(std::string const& aSource)
        {
            auto const lines = static_cast<std::uint32_t>(std::count(aSource.begin(), aSource.end(), '\n'));
            return !aSource.empty() && aSource.back() == '\n' ? lines : lines + 1u;
        }

        bool is_space(char c) { return c == ' ' || c == '\t' || c == '\f'; }

        // --- the tokenizer's errors (and those of indentation, which CPython's parser reports): the first

        std::optional<syntax_issue> tokenizer_issue(std::string const& aSource, bool aExpression)
        {
            auto const n = aSource.size();
            struct bracket { char c; std::size_t position; };
            std::vector<bracket> brackets;
            struct indentation { std::size_t column; std::size_t alternative; };
            std::vector<indentation> indents{ { 0u, 0u } };
            // (a block that is expected (the previous logical line ends with ':'): its statement and line)
            struct expected_block { std::string what; std::uint32_t line; std::size_t colon; bool report; };
            std::optional<expected_block> expected;
            std::size_t i = 0u;
            bool lineStart = true;
            std::size_t firstToken = std::string::npos;    // (of the logical line)
            std::size_t lastToken = std::string::npos;
            std::size_t lastTokenEnd = 0u;
            std::size_t secondToken = std::string::npos;
            auto const token_text = [&](std::size_t aBegin)
            {
                std::size_t e = aBegin;
                while (e < n && tokens::name_char(aSource[e]))
                    ++e;
                return std::string_view{ aSource }.substr(aBegin, e - aBegin);
            };
            // (a logical line that ended: a block is expected if it ends with ':' (outside brackets))
            auto const end_logical_line = [&]()
            {
                if (firstToken != std::string::npos && lastToken != std::string::npos && aSource[lastToken] == ':' && lastTokenEnd == lastToken + 1u)
                {
                    auto keyword = token_text(firstToken);
                    if (keyword == "async" && secondToken != std::string::npos)
                        keyword = token_text(secondToken);
                    std::string what;
                    if (keyword == "def")
                        what = "function definition";
                    else if (keyword == "class")
                        what = "class definition";
                    else if (keyword == "if" || keyword == "elif" || keyword == "else" || keyword == "while" || keyword == "for" || keyword == "with" ||
                        keyword == "try" || keyword == "finally" || keyword == "match" || keyword == "case")
                        what = "'" + std::string{ keyword } + "' statement";
                    else if (keyword == "except")
                        what = secondToken != std::string::npos && aSource[secondToken] == '*' ? "'except*' statement" : "'except' statement";
                    // (a line that is not a compound statement's header: its block is not reported (the parser's error is))
                    expected = expected_block{ what, location_of(aSource, firstToken).first, lastToken, !what.empty() };
                }
                firstToken = lastToken = secondToken = std::string::npos;
            };
            auto const token_seen = [&](std::size_t aBegin, std::size_t aEnd)
            {
                if (firstToken == std::string::npos)
                    firstToken = aBegin;
                else if (secondToken == std::string::npos)
                    secondToken = aBegin;
                lastToken = aBegin;
                lastTokenEnd = aEnd;
            };
            auto const expected_issue = [&](std::size_t aPosition, std::size_t aEnd)
            {
                auto issue = make_issue(aSource, syntax_issue::Tokenizer, expected->what.empty() ? std::string{ "expected an indented block" } :
                    "expected an indented block after " + expected->what + " on line " + std::to_string(expected->line), aPosition, aEnd, "IndentationError");
                return issue;
            };
            while (i < n)
            {
                if (lineStart && brackets.empty())
                {
                    lineStart = false;
                    auto const begin = i;
                    std::size_t column = 0u;
                    std::size_t alternative = 0u;
                    while (i < n && is_space(aSource[i]))
                    {
                        if (aSource[i] == '\t')
                        {
                            column = (column / 8u + 1u) * 8u;
                            ++alternative;
                        }
                        else if (aSource[i] == '\f')
                            column = alternative = 0u;
                        else
                        {
                            ++column;
                            ++alternative;
                        }
                        ++i;
                    }
                    // (a blank line, or one of a comment: no indentation)
                    if (i >= n || aSource[i] == '\n' || aSource[i] == '\r' || aSource[i] == '#')
                    {
                        while (i < n && aSource[i] != '\n')
                            ++i;
                        if (i < n)
                            ++i;
                        lineStart = true;
                        continue;
                    }
                    if (aExpression)
                        continue;
                    // (the first token's end: the extent of the indentation errors')
                    std::size_t tokenEnd = i + 1u;
                    if (tokens::name_char(aSource[i]))
                        while (tokenEnd < n && tokens::name_char(aSource[tokenEnd]))
                            ++tokenEnd;
                    auto const& top = indents.back();
                    if (column > top.column)
                    {
                        if (!expected)
                        {
                            // (CPython: its offset is the indentation's length; its end -1)
                            auto issue = make_issue(aSource, syntax_issue::Tokenizer, "unexpected indent", begin, begin, "IndentationError");
                            issue.offset = static_cast<std::int64_t>(i - begin);
                            issue.endOffset = -1;
                            return issue;
                        }
                        if (alternative <= top.alternative)
                        {
                            auto issue = make_issue(aSource, syntax_issue::Tokenizer, "inconsistent use of tabs and spaces in indentation", begin, begin, "TabError");
                            issue.endOffset = 0;
                            return issue;
                        }
                        indents.push_back({ column, alternative });
                    }
                    else
                    {
                        if (expected && expected->report)
                            return expected_issue(i, tokenEnd);
                        while (column < indents.back().column)
                            indents.pop_back();
                        if (column != indents.back().column)
                        {
                            auto issue = make_issue(aSource, syntax_issue::Tokenizer, "unindent does not match any outer indentation level", begin, begin,
                                "IndentationError");
                            issue.offset = static_cast<std::int64_t>(i - begin) + 2;
                            issue.endOffset = -1;
                            return issue;
                        }
                        if (alternative != indents.back().alternative)
                        {
                            auto issue = make_issue(aSource, syntax_issue::Tokenizer, "inconsistent use of tabs and spaces in indentation", begin, begin, "TabError");
                            issue.endOffset = 0;
                            return issue;
                        }
                    }
                    expected.reset();
                    continue;
                }
                char const c = aSource[i];
                if (is_space(c) || c == '\r')
                {
                    ++i;
                    continue;
                }
                if (c == '\\' && i + 1u < n && (aSource[i + 1u] == '\n' || aSource[i + 1u] == '\r'))
                {
                    i += aSource[i + 1u] == '\r' && i + 2u < n && aSource[i + 2u] == '\n' ? 3u : 2u;
                    continue;
                }
                if (c == '#')
                {
                    while (i < n && aSource[i] != '\n')
                        ++i;
                    continue;
                }
                if (c == '\n')
                {
                    ++i;
                    if (brackets.empty())
                    {
                        end_logical_line();
                        lineStart = true;
                    }
                    continue;
                }
                // (a string (with its prefix))
                std::size_t prefix = i;
                std::size_t quote = std::string::npos;
                if (c == '\'' || c == '"')
                    quote = i;
                else if (tokens::name_start(c))
                {
                    auto j = i;
                    while (j < n && tokens::name_char(aSource[j]))
                        ++j;
                    if (j < n && (aSource[j] == '\'' || aSource[j] == '"') && j - i <= 2u &&
                        std::string_view{ aSource }.substr(i, j - i).find_first_not_of("rRbBuUfF") == std::string_view::npos)
                        quote = j;
                    else
                    {
                        token_seen(i, j);
                        i = j;
                        continue;
                    }
                }
                if (quote != std::string::npos)
                {
                    char const q = aSource[quote];
                    bool const triple = quote + 2u < n && aSource[quote + 1u] == q && aSource[quote + 2u] == q;
                    bool const raw = std::string_view{ aSource }.substr(prefix, quote - prefix).find_first_of("rR") != std::string_view::npos;
                    bool const formatted = std::string_view{ aSource }.substr(prefix, quote - prefix).find_first_of("fF") != std::string_view::npos;
                    std::size_t j = quote + (triple ? 3u : 1u);
                    bool closed = false;
                    if (formatted)
                    {
                        // (an f-string's replacement fields can contain its own quote, line breaks and comments (PEP 701))
                        auto const end = tokens::fstring_end(aSource, quote);
                        auto const quoteSize = triple ? 3u : 1u;
                        closed = end >= quote + 2u * quoteSize && end <= n && aSource.compare(end - quoteSize, quoteSize, std::string(quoteSize, q)) == 0;
                        if (closed)
                        {
                            // (fstring_end stops before a line break or at the end of the text if it's unterminated: its last
                            // character is then an escaped quote, not a closing one)
                            std::size_t backslashes = 0u;
                            for (auto b = end - quoteSize; b > quote + quoteSize && aSource[b - 1u] == '\\'; --b)
                                ++backslashes;
                            closed = backslashes % 2u == 0u;
                        }
                        j = closed ? end : n;
                    }
                    for (; !formatted && j < n; ++j)
                    {
                        if (aSource[j] == '\\')
                        {
                            if (j + 1u < n)
                                j = tokens::escape_last(aSource, j);
                            (void)raw;
                        }
                        else if (aSource[j] == q && (!triple || (j + 2u < n && aSource[j + 1u] == q && aSource[j + 2u] == q)))
                        {
                            j += triple ? 3u : 1u;
                            closed = true;
                            break;
                        }
                        else if (!triple && (aSource[j] == '\n' || (aSource[j] == '\r' && j + 1u < n && aSource[j + 1u] == '\n')))
                            break;
                    }
                    if (!closed)
                    {
                        auto const detected = triple ? last_line(aSource) : location_of(aSource, prefix).first;
                        auto issue = make_issue(aSource, syntax_issue::Tokenizer, std::string{ "unterminated " } + (triple ? "triple-quoted " : "") +
                            (formatted ? "f-string" : "string") + " literal (detected at line " + std::to_string(detected) + ")", prefix, prefix);
                        issue.lineText = false;
                        return issue;
                    }
                    token_seen(prefix, j);
                    i = j;
                    continue;
                }
                if (c == '(' || c == '[' || c == '{')
                    brackets.push_back({ c, i });
                else if (c == ')' || c == ']' || c == '}')
                {
                    if (brackets.empty())
                    {
                        auto issue = make_issue(aSource, syntax_issue::Tokenizer, std::string{ "unmatched '" } + c + "'", i, i);
                        issue.lineText = false;
                        return issue;
                    }
                    auto const opening = brackets.back();
                    if ((opening.c == '(' ? ')' : opening.c == '[' ? ']' : '}') != c)
                    {
                        auto const openingLine = location_of(aSource, opening.position).first;
                        auto issue = make_issue(aSource, syntax_issue::Tokenizer, std::string{ "closing parenthesis '" } + c +
                            "' does not match opening parenthesis '" + opening.c + "'" +
                            (openingLine != location_of(aSource, i).first ? " on line " + std::to_string(openingLine) : std::string{}), i, i);
                        issue.lineText = false;
                        return issue;
                    }
                    brackets.pop_back();
                }
                token_seen(i, i + 1u);
                ++i;
            }
            if (!brackets.empty())
            {
                auto issue = make_issue(aSource, syntax_issue::Tokenizer, std::string{ "'" } + brackets.back().c + "' was never closed", brackets.back().position,
                    brackets.back().position);
                issue.endOffset = 0;
                issue.order = n;    // (at the end of the source)
                issue.lineText = !aExpression;
                return issue;
            }
            if (!aExpression)
            {
                end_logical_line();
                if (expected && expected->report)
                {
                    // (at the end of the source: after the header's colon)
                    auto issue = expected_issue(expected->colon + 1u, expected->colon + 1u);
                    issue.endOffset = -1;
                    issue.order = n;
                    return issue;
                }
            }
            return std::nullopt;
        }

        // --- the statements' errors (of the parser, the symbol table and the compiler)

        bool is_op(std::string const& aSource, token const& aToken, std::string_view aOp)
        {
            return aToken.kind == token::Op && std::string_view{ aSource }.substr(aToken.begin, aToken.end - aToken.begin) == aOp;
        }

        bool is_name(std::string const& aSource, token const& aToken, std::string_view aName)
        {
            return aToken.kind == token::Name && std::string_view{ aSource }.substr(aToken.begin, aToken.end - aToken.begin) == aName;
        }

        std::string_view text_of(std::string const& aSource, token const& aToken)
        {
            return std::string_view{ aSource }.substr(aToken.begin, aToken.end - aToken.begin);
        }

        bool opening(std::string const& aSource, token const& aToken) { return is_op(aSource, aToken, "(") || is_op(aSource, aToken, "[") || is_op(aSource, aToken, "{"); }
        bool closing(std::string const& aSource, token const& aToken) { return is_op(aSource, aToken, ")") || is_op(aSource, aToken, "]") || is_op(aSource, aToken, "}"); }

        // (the depth (of brackets) of each token of a logical line)
        std::vector<int> depths(std::string const& aSource, std::vector<token> const& aTokens)
        {
            std::vector<int> result;
            int depth = 0;
            for (auto const& t : aTokens)
            {
                if (closing(aSource, t) && depth > 0)
                    --depth;
                result.push_back(depth);
                if (opening(aSource, t))
                    ++depth;
            }
            return result;
        }

        // (the bracket that matches the bracket at aIndex (searching forward (opening) or back (closing)))
        std::size_t matching(std::string const& aSource, std::vector<token> const& aTokens, std::size_t aIndex)
        {
            int depth = 0;
            if (opening(aSource, aTokens[aIndex]))
            {
                for (std::size_t i = aIndex; i < aTokens.size(); ++i)
                {
                    if (opening(aSource, aTokens[i]))
                        ++depth;
                    else if (closing(aSource, aTokens[i]) && --depth == 0)
                        return i;
                }
                return aTokens.size();
            }
            for (std::size_t i = aIndex + 1u; i-- > 0u;)
            {
                if (closing(aSource, aTokens[i]))
                    ++depth;
                else if (opening(aSource, aTokens[i]) && --depth == 0)
                    return i;
            }
            return aTokens.size();
        }

        bool keyword(std::string const& aSource, token const& aToken)
        {
            return aToken.kind == token::Name && tokens::keywords().contains(text_of(aSource, aToken));
        }

        // (a token that ends an expression (an atom or a trailer))
        bool ends_expression(std::string const& aSource, token const& aToken)
        {
            if (aToken.kind == token::Number || aToken.kind == token::String)
                return true;
            if (aToken.kind == token::Name)
                return !keyword(aSource, aToken) || is_name(aSource, aToken, "None") || is_name(aSource, aToken, "True") || is_name(aSource, aToken, "False");
            return closing(aSource, aToken);
        }

        bool starts_expression(std::string const& aSource, token const& aToken)
        {
            if (aToken.kind == token::Number || aToken.kind == token::String)
                return true;
            if (aToken.kind == token::Name)
                return !keyword(aSource, aToken) || is_name(aSource, aToken, "None") || is_name(aSource, aToken, "True") || is_name(aSource, aToken, "False");
            return opening(aSource, aToken);
        }

        // (the start of the primary that ends at aIndex (a.b, f(x), a[i]))
        std::size_t primary_start(std::string const& aSource, std::vector<token> const& aTokens, std::size_t aIndex)
        {
            auto i = aIndex;
            for (;;)
            {
                if (closing(aSource, aTokens[i]))
                {
                    auto const open = matching(aSource, aTokens, i);
                    if (open >= aTokens.size())
                        return i;
                    i = open;
                    if (i > 0u && (aTokens[i - 1u].kind == token::Name || closing(aSource, aTokens[i - 1u]) || aTokens[i - 1u].kind == token::String) &&
                        !keyword(aSource, aTokens[i - 1u]))
                    {
                        --i;
                        continue;
                    }
                    return i;
                }
                if (i >= 2u && is_op(aSource, aTokens[i - 1u], ".") && ends_expression(aSource, aTokens[i - 2u]))
                {
                    i -= 2u;
                    continue;
                }
                return i;
            }
        }

        struct statement
        {
            std::size_t begin;  ///< (tokens [begin, end) of its logical line)
            std::size_t end;
        };

        // (an assignment's or del's target that is not one: its kind ("literal", "None", "function call", "expression"), or empty)
        std::string invalid_target(std::string const& aSource, std::vector<token> const& aTokens, std::size_t aBegin, std::size_t aEnd)
        {
            if (aBegin >= aEnd)
                return {};
            auto const d = depths(aSource, aTokens);
            int const base = d[aBegin];
            for (std::size_t i = aBegin; i < aEnd; ++i)
                if (d[i] == base && (is_op(aSource, aTokens[i], ",") || is_op(aSource, aTokens[i], ":")))
                    return {};
            if (aEnd - aBegin == 1u)
            {
                auto const& t = aTokens[aBegin];
                if (t.kind == token::Number || t.kind == token::String)
                    return "literal";
                for (auto n : { "None", "True", "False" })
                    if (is_name(aSource, t, n))
                        return n;
                if (is_op(aSource, t, "..."))
                    return "ellipsis";
                return {};
            }
            if (closing(aSource, aTokens[aEnd - 1u]) && is_op(aSource, aTokens[aEnd - 1u], ")"))
            {
                auto const open = matching(aSource, aTokens, aEnd - 1u);
                if (open > aBegin && open < aTokens.size() && primary_start(aSource, aTokens, aEnd - 1u) == aBegin)
                    return "function call";
            }
            static std::vector<std::string_view> const sOperators = { "+", "-", "*", "/", "%", "**", "//", "<<", ">>", "&", "|", "^", "@", "<", ">", "==",
                "!=", "<=", ">=", "~" };
            for (std::size_t i = aBegin; i < aEnd; ++i)
                if (d[i] == base && ((aTokens[i].kind == token::Op && std::find(sOperators.begin(), sOperators.end(), text_of(aSource, aTokens[i])) != sOperators.end() &&
                    !(i == aBegin && is_op(aSource, aTokens[i], "*"))) || is_name(aSource, aTokens[i], "and") || is_name(aSource, aTokens[i], "or") ||
                    is_name(aSource, aTokens[i], "not") || is_name(aSource, aTokens[i], "in") || is_name(aSource, aTokens[i], "is")))
                    return "expression";
            return {};
        }

        void statement_issues(std::string const& aSource, std::vector<syntax_issue>& aIssues)
        {
            auto const lines = tokens::tokenize(aSource);
            // (the enclosing blocks: their indentation and kind)
            enum kind_t { Function, AsyncFunction, Class, Loop, Other, Try };
            struct block 
            { 
                std::size_t indent; 
                kind_t kind; 
                std::size_t lastLine; 
                bool yields = false;                                        ///< (a function's: it is a generator)
                std::optional<std::pair<std::size_t, std::size_t>> returned;  ///< (an async function's first return with a value: its span)
                // (a function's or class's (or the module's) names (the symbol table's): its parameters, the names that its statements bind
                // and use, and its global and nonlocal declarations (their statements' spans))
                std::vector<std::string> parameters;
                std::set<std::string, std::less<>> assigned;
                std::set<std::string, std::less<>> used;
                std::map<std::string, std::pair<std::size_t, std::size_t>, std::less<>> globals;
                std::map<std::string, std::pair<std::size_t, std::size_t>, std::less<>> nonlocals;
            };
            std::vector<block> blocks;
            block moduleNames{ 0u, Other, 0u };
            bool futureAllowed = true;      // (from __future__ imports: only at the beginning of the module (after its docstring))
            bool docstringAllowed = true;
            std::map<std::size_t, std::optional<std::pair<std::size_t, std::size_t>>> bareExcepts;  // (by indentation: a try's except clause without a type)
            std::vector<std::string> headerParameters;  // (the parameters of the def whose header is being checked)
            auto const add = [&](syntax_issue::stage_t aStage, std::string aMessage, std::size_t aBegin, std::size_t aEnd)
            {
                aIssues.push_back(make_issue(aSource, aStage, std::move(aMessage), aBegin, aEnd));
            };
            // (the innermost function or class (null: the module))
            auto const scope = [&]() -> block const*
            {
                for (auto b = blocks.rbegin(); b != blocks.rend(); ++b)
                    if (b->kind == Function || b->kind == AsyncFunction || b->kind == Class)
                        return &*b;
                return nullptr;
            };
            auto const in_loop = [&]()
            {
                for (auto b = blocks.rbegin(); b != blocks.rend(); ++b)
                {
                    if (b->kind == Loop)
                        return true;
                    if (b->kind == Function || b->kind == AsyncFunction || b->kind == Class)
                        return false;
                }
                return false;
            };
            auto const in_function = [&]()
            {
                for (auto b = blocks.rbegin(); b != blocks.rend(); ++b)
                    if (b->kind == Function || b->kind == AsyncFunction)
                        return true;
                return false;
            };
            // (an async function that ended: a return with a value, if it is an async generator (the symbol table's, as CPython's))
            auto const finish_block = [&](block const& aBlock)
            {
                if (aBlock.kind == AsyncFunction && aBlock.yields && aBlock.returned)
                    add(syntax_issue::Compiler, "'return' with value in async generator", aBlock.returned->first, aBlock.returned->second);
            };
            // (a try block that ended: its except or finally clause)
            auto const close_blocks = [&](std::size_t aIndent, std::optional<std::size_t> aNext)
            {
                while (!blocks.empty() && blocks.back().indent >= aIndent)
                {
                    auto const b = blocks.back();
                    blocks.pop_back();
                    finish_block(b);
                    if (b.kind == Try)
                    {
                        bool handled = false;
                        if (aNext && b.indent == aIndent)
                        {
                            auto const& next = lines[*aNext];
                            handled = is_name(aSource, next.tokens[0], "except") || is_name(aSource, next.tokens[0], "finally");
                        }
                        if (!handled)
                        {
                            auto const& last = lines[b.lastLine];
                            auto const end = content_end(aSource, line_begin(aSource, last.tokens.back().begin));
                            auto issue = make_issue(aSource, syntax_issue::Parser, "expected 'except' or 'finally' block", end, end);
                            issue.endOffset = -1;
                            aIssues.push_back(issue);
                        }
                    }
                }
            };
            auto const parameters = [&](std::vector<token> const& aTokens, std::size_t aOpen, std::size_t aClose)
            {
                // (the parameters' names (from aOpen + 1 to aClose): a duplicate is an error of the symbol table)
                std::vector<std::string_view> names;
                auto const d = depths(aSource, aTokens);
                bool expectName = true;
                std::optional<std::size_t> bareStar;    // (a * without a name: names must follow it (the parser's))
                bool reported = false;
                auto const bare_star = [&]()
                {
                    if (bareStar && !reported)
                        aIssues.push_back(make_issue(aSource, syntax_issue::Parser, "named arguments must follow bare *", aTokens[*bareStar].begin, aTokens[*bareStar].end));
                    reported = reported || bareStar.has_value();
                    bareStar.reset();
                };
                for (std::size_t i = aOpen + 1u; i < aClose; ++i)
                {
                    if (d[i] != d[aOpen + 1u])
                        continue;
                    auto const& t = aTokens[i];
                    if (is_op(aSource, t, ","))
                    {
                        expectName = true;
                        continue;
                    }
                    if (is_op(aSource, t, "*") && (i + 1u == aClose || is_op(aSource, aTokens[i + 1u], ",")))
                    {
                        bareStar = i;
                        continue;
                    }
                    if (is_op(aSource, t, "**"))
                        bare_star();
                    if (is_op(aSource, t, "*") || is_op(aSource, t, "**") || is_op(aSource, t, "/"))
                        continue;
                    if (expectName && t.kind == token::Name)
                        bareStar.reset();
                    if (expectName && t.kind == token::Name)
                    {
                        auto const name = text_of(aSource, t);
                        headerParameters.emplace_back(name);
                        if (std::find(names.begin(), names.end(), name) != names.end())
                        {
                            auto issue = make_issue(aSource, syntax_issue::Symtable, "duplicate argument '" + std::string{ name } + "' in function definition",
                                t.begin, t.end);
                            aIssues.push_back(issue);
                        }
                        names.push_back(name);
                    }
                    expectName = false;
                }
                bare_star();
            };
            std::vector<statement> statements;
            for (std::size_t l = 0u; l < lines.size(); ++l)
            {
                auto const& line = lines[l];
                auto const& t = line.tokens;
                close_blocks(line.indent, l);
                for (auto& b : blocks)
                    b.lastLine = l;
                auto const d = depths(aSource, t);
                // (the statements: a compound statement's header (to its colon), then simple statements separated by ';')
                std::size_t start = 0u;
                std::optional<kind_t> opened;
                headerParameters.clear();
                bool const async = is_name(aSource, t[0], "async") && t.size() > 1u;
                auto const& head = async ? t[1] : t[0];
                for (auto k : { "if", "elif", "else", "while", "for", "with", "try", "except", "finally", "def", "class" })
                    if (is_name(aSource, head, k))
                    {
                        // (its colon: the first outside brackets (not of a lambda))
                        std::size_t colon = t.size();
                        int lambdas = 0;
                        for (std::size_t i = 0u; i < t.size(); ++i)
                            if (d[i] == 0)
                            {
                                if (is_name(aSource, t[i], "lambda"))
                                    ++lambdas;
                                else if (is_op(aSource, t[i], ":"))
                                {
                                    if (lambdas > 0)
                                        --lambdas;
                                    else
                                    {
                                        colon = i;
                                        break;
                                    }
                                }
                            }
                        if (colon == t.size())
                            break;
                        std::string_view const kw{ k };
                        opened = kw == "def" ? (async ? AsyncFunction : Function) : kw == "class" ? Class : (kw == "while" || kw == "for") ? Loop :
                            kw == "try" ? Try : Other;
                        if (kw == "def")
                            for (std::size_t i = 0u; i < colon; ++i)
                                if (is_op(aSource, t[i], "("))
                                {
                                {
                                    headerParameters.clear();
                                    parameters(t, i, matching(aSource, t, i));
                                }
                                    break;
                                }
                        start = colon + 1u;
                        if (start < t.size())
                        {
                            // (a body on the header's line: within the block)
                            blocks.push_back({ line.indent + 1u, *opened, l });
                            if (*opened == Function || *opened == AsyncFunction)
                                blocks.back().parameters = headerParameters;
                        }
                        break;
                    }
                statements.clear();
                for (std::size_t i = start; i < t.size(); ++i)
                    if (d[i] == 0 && is_op(aSource, t[i], ";"))
                    {
                        statements.push_back({ start, i });
                        start = i + 1u;
                    }
                if (start < t.size())
                    statements.push_back({ start, t.size() });
                // (assignment expressions (name := value), comprehensions and calls' arguments: the parser's errors and the symbol table's 
                // (as CPython's invalid_named_expression, invalid_comprehension, invalid_arguments and symtable_handle_namedexpr))
                {
                    auto const n = t.size();
                    // (the colon of the parameters of the lambda at aLambda)
                    auto const lambda_colon = [&](std::size_t aLambda) -> std::size_t
                    {
                        int lambdas = 0;
                        for (auto j = aLambda + 1u; j < n; ++j)
                        {
                            if (d[j] < d[aLambda])
                                break;
                            if (d[j] != d[aLambda])
                                continue;
                            if (is_name(aSource, t[j], "lambda"))
                                ++lambdas;
                            else if (is_op(aSource, t[j], ":"))
                            {
                                if (lambdas == 0)
                                    return j;
                                --lambdas;
                            }
                        }
                        return n;
                    };
                    // (the body of the lambda at aLambda contains the token at aAt)
                    auto const lambda_contains = [&](std::size_t aLambda, std::size_t aAt)
                    {
                        auto const colon = lambda_colon(aLambda);
                        if (colon >= aAt)
                            return false;
                        for (auto j = colon + 1u; j < aAt; ++j)
                            if (d[j] < d[aLambda] || (d[j] == d[aLambda] && (is_op(aSource, t[j], ",") || is_op(aSource, t[j], ";") || is_name(aSource, t[j], "for"))))
                                return false;
                        return true;
                    };
                    // (the items of brackets [aOpen, aClose]: separated by commas at their level (not a lambda's parameters'), up to aEnd)
                    auto const items = [&](std::size_t aOpen, std::size_t aEnd)
                    {
                        std::vector<std::pair<std::size_t, std::size_t>> result;
                        auto const level = d[aOpen] + 1;
                        auto begin = aOpen + 1u;
                        bool targets = false;   // (a comprehension's for clause's targets (for a, b in ...): its commas)
                        for (auto j = aOpen + 1u; j <= aEnd; ++j)
                        {
                            if (j < aEnd && d[j] == level && is_name(aSource, t[j], "lambda"))
                            {
                                j = std::min(lambda_colon(j), aEnd - 1u);
                                continue;
                            }
                            if (j < aEnd && d[j] == level && (is_name(aSource, t[j], "for") || is_name(aSource, t[j], "in")))
                                targets = is_name(aSource, t[j], "for");
                            if (j < aEnd && targets)
                                continue;
                            if (j == aEnd || (d[j] == level && is_op(aSource, t[j], ",")))
                            {
                                if (begin < j)
                                    result.emplace_back(begin, j);
                                begin = j + 1u;
                            }
                        }
                        return result;
                    };
                    // (a comprehension's for and if clauses (within brackets [aOpen, aClose]): none if they are not a comprehension's)
                    struct clause { std::size_t begin; bool isFor; std::size_t in; std::size_t end; };
                    auto const clauses_of = [&](std::size_t aOpen, std::size_t aClose)
                    {
                        std::vector<clause> result;
                        auto const level = d[aOpen] + 1;
                        for (auto j = aOpen + 1u; j < aClose; ++j)
                        {
                            if (d[j] != level || t[j].kind != token::Name)
                                continue;
                            if (is_name(aSource, t[j], "for"))
                                result.push_back({ j, true, aClose, aClose });
                            else if (is_name(aSource, t[j], "if") && !result.empty())
                                result.push_back({ j, false, aClose, aClose });
                            else if (is_name(aSource, t[j], "in") && !result.empty() && result.back().isFor && result.back().in == aClose)
                                result.back().in = j;
                        }
                        for (std::size_t k = 0u; k + 1u < result.size(); ++k)
                            result[k].end = result[k + 1u].begin > 0u && is_name(aSource, t[result[k + 1u].begin - 1u], "async") ? result[k + 1u].begin - 1u : result[k + 1u].begin;
                        return result;
                    };
                    // (the names that a for clause's targets bind (not those in attributes, subscripts or calls))
                    auto const iteration_names = [&](clause const& aClause)
                    {
                        std::vector<std::pair<std::string_view, std::size_t>> names;   // (and their tokens)
                        std::vector<bool> plain;
                        for (auto j = aClause.begin + 1u; j < aClause.in; ++j)
                        {
                            if (opening(aSource, t[j]))
                            {
                                bool const applied = j > aClause.begin + 1u && ends_expression(aSource, t[j - 1u]);
                                plain.push_back(!applied && (plain.empty() || plain.back()));
                                continue;
                            }
                            if (closing(aSource, t[j]))
                            {
                                if (!plain.empty())
                                    plain.pop_back();
                                continue;
                            }
                            if (t[j].kind != token::Name || keyword(aSource, t[j]) || (!plain.empty() && !plain.back()) || is_op(aSource, t[j - 1u], "."))
                                continue;
                            if (j + 1u < aClause.in && (is_op(aSource, t[j + 1u], ".") || is_op(aSource, t[j + 1u], "(") || is_op(aSource, t[j + 1u], "[")))
                                continue;
                            names.emplace_back(text_of(aSource, t[j]), j);
                        }
                        return names;
                    };
                    auto const applied = [&](std::size_t aOpen)
                    {
                        return aOpen > 0u && ends_expression(aSource, t[aOpen - 1u]);
                    };
                    // (a def's parameters: their brackets)
                    std::optional<std::pair<std::size_t, std::size_t>> defParameters;
                    {
                        auto const h = async ? 1u : 0u;
                        if (h + 2u < n && is_name(aSource, t[h], "def") && is_op(aSource, t[h + 2u], "("))
                            defParameters = std::make_pair(h + 2u, matching(aSource, t, h + 2u));
                    }
                    for (std::size_t o = 0u; o < n; ++o)
                    {
                        if (!opening(aSource, t[o]))
                            continue;
                        auto const c = matching(aSource, t, o);
                        if (c >= n)
                            continue;
                        if (is_op(aSource, t[o], "(") && applied(o) && !(defParameters && defParameters->first == o))
                        {
                            // (a call's arguments: positional ones after keyword ones)
                            bool keywordSeen = false, unpackingSeen = false;
                            auto const arguments = items(o, c);
                            if (arguments.size() > 1u)
                            {
                                bool generator = false;
                                for (auto const& [b, e] : arguments)
                                    for (auto j = b; j < e && !generator; ++j)
                                        if (d[j] == d[o] + 1 && is_name(aSource, t[j], "for"))
                                        {
                                            add(syntax_issue::Parser, "Generator expression must be parenthesized", t[b].begin, t[e - 1u].end);
                                            generator = true;
                                        }
                                if (generator)
                                    continue;
                            }
                            for (auto const& [b, e] : arguments)
                            {
                                if (is_op(aSource, t[b], "**"))
                                    unpackingSeen = true;
                                else if (is_op(aSource, t[b], "*"))
                                {
                                    if (unpackingSeen)
                                    {
                                        add(syntax_issue::Parser, "iterable argument unpacking follows keyword argument unpacking", t[b].begin, t[e - 1u].end);
                                        break;
                                    }
                                }
                                else if (t[b].kind == token::Name && b + 1u < e && is_op(aSource, t[b + 1u], "="))
                                    keywordSeen = true;
                                else if (unpackingSeen || keywordSeen)
                                {
                                    add(syntax_issue::Parser, unpackingSeen ? "positional argument follows keyword argument unpacking" : 
                                        "positional argument follows keyword argument", t[b].begin, t[e - 1u].end);
                                    break;
                                }
                            }
                        }
                        else if ((is_op(aSource, t[o], "[") && !applied(o)) || is_op(aSource, t[o], "{"))
                        {
                            // (a comprehension of several elements without parentheses: [x, y for x, y in z])
                            auto const clauses = clauses_of(o, c);
                            if (!clauses.empty())
                            {
                                bool colon = false;
                                for (auto j = o + 1u; j < clauses.front().begin; ++j)
                                    if (d[j] == d[o] + 1 && is_op(aSource, t[j], ":"))
                                        colon = true;
                                auto const elements = items(o, clauses.front().begin);
                                if (!colon && elements.size() > 1u)
                                    add(syntax_issue::Parser, "did you forget parentheses around the comprehension target?", t[elements.front().first].begin,
                                        t[elements.back().second - 1u].end);
                            }
                        }
                    }
                    // (async and await: keywords (the parser's: as names, or an await of what is not a primary); an asynchronous comprehension 
                    // (not a generator expression), an async for or with statement, or an await in a def's parameters' defaults or annotations 
                    // (evaluated in the enclosing scope) outside an async function (the compiler's))
                    {
                        // (the scope that encloses the line's header (its inline body's block is not)
                        block const* outer = nullptr;
                        {
                            bool const inline_ = opened && start < n && !blocks.empty() && blocks.back().lastLine == l && blocks.back().indent == line.indent + 1u;
                            for (auto b = blocks.rbegin(); b != blocks.rend(); ++b)
                                if ((!inline_ || b != blocks.rbegin()) && (b->kind == Function || b->kind == AsyncFunction || b->kind == Class))
                                {
                                    outer = &*b;
                                    break;
                                }
                        }
                        auto const* const inner = scope();
                        bool keywordIssue = false;
                        for (std::size_t i = 0u; i < n && !keywordIssue; ++i)
                        {
                            if (is_name(aSource, t[i], "async"))
                            {
                                if (i + 1u >= n || !(is_name(aSource, t[i + 1u], "def") || is_name(aSource, t[i + 1u], "for") || is_name(aSource, t[i + 1u], "with")))
                                {
                                    add(syntax_issue::Parser, "invalid syntax", t[i].begin, t[i].end);
                                    keywordIssue = true;
                                }
                            }
                            else if (is_name(aSource, t[i], "await"))
                            {
                                bool const primary = i + 1u < n && (starts_expression(aSource, t[i + 1u]) || is_op(aSource, t[i + 1u], "..."));
                                if (!primary || (i > 0u && (is_name(aSource, t[i - 1u], "def") || is_name(aSource, t[i - 1u], "class") || is_op(aSource, t[i - 1u], "."))))
                                {
                                    auto const& at = i + 1u < n && i > 0u && !is_name(aSource, t[i - 1u], "def") && !is_name(aSource, t[i - 1u], "class") ? t[i + 1u] : t[i];
                                    add(syntax_issue::Parser, "invalid syntax", at.begin, at.end);
                                    keywordIssue = true;
                                }
                            }
                        }
                        if (!keywordIssue)
                        {
                            // (an async for or with statement)
                            for (auto const& st : statements)
                                if (st.begin + 1u < st.end && is_name(aSource, t[st.begin], "async") && 
                                    (is_name(aSource, t[st.begin + 1u], "for") || is_name(aSource, t[st.begin + 1u], "with")) && st.begin != 0u)
                                    if (inner == nullptr || inner->kind != AsyncFunction)
                                        add(syntax_issue::Compiler, "'async " + std::string{ text_of(aSource, t[st.begin + 1u]) } + "' outside async function", 
                                            t[st.begin].begin, t[st.end - 1u].end);
                            if (async && n > 1u && (is_name(aSource, t[1], "for") || is_name(aSource, t[1], "with")) && (outer == nullptr || outer->kind != AsyncFunction))
                                add(syntax_issue::Compiler, "'async " + std::string{ text_of(aSource, t[1]) } + "' outside async function", t[0].begin, t[n - 1u].end);
                            // (an await in a def's parameters)
                            if (defParameters)
                                for (auto i = defParameters->first + 1u; i < defParameters->second; ++i)
                                    if (is_name(aSource, t[i], "await") && (outer == nullptr || outer->kind != AsyncFunction))
                                    {
                                        bool inLambda = false;
                                        for (auto j = defParameters->first + 1u; j < i && !inLambda; ++j)
                                            if (is_name(aSource, t[j], "lambda") && lambda_contains(j, i))
                                                inLambda = true;
                                        if (inLambda)
                                            continue;
                                        auto e = i + 1u;
                                        while (e < defParameters->second && !(d[e] == d[i] && (is_op(aSource, t[e], ",") || is_op(aSource, t[e], "="))) && d[e] >= d[i])
                                            ++e;
                                        add(syntax_issue::Compiler, outer == nullptr || outer->kind == Class ? "'await' outside function" : "'await' outside async function", 
                                            t[i].begin, t[e - 1u].end);
                                        break;
                                    }
                            // (an asynchronous comprehension: in an async function's body (not a lambda's), or a generator expression)
                            for (std::size_t i = 1u; i < n; ++i)
                            {
                                if (!is_name(aSource, t[i], "for") || !is_name(aSource, t[i - 1u], "async") || d[i] == 0)
                                    continue;
                                // (its brackets)
                                std::size_t o = i;
                                while (o-- > 0u && !(opening(aSource, t[o]) && d[o] == d[i] - 1))
                                    ;
                                if (o >= n || is_op(aSource, t[o], "("))
                                    continue;
                                bool inLambda = false;
                                for (std::size_t j = 0u; j < o && !inLambda; ++j)
                                    if (is_name(aSource, t[j], "lambda") && lambda_contains(j, o))
                                        inLambda = true;
                                bool inParameters = defParameters && o > defParameters->first && o < defParameters->second;
                                auto const* const s = inParameters ? outer : inner;
                                if (inLambda || s == nullptr || s->kind != AsyncFunction)
                                {
                                    auto const c = matching(aSource, t, o);
                                    add(syntax_issue::Compiler, "asynchronous comprehension outside of an asynchronous function", t[o].begin, t[std::min(c, n - 1u)].end);
                                    break;
                                }
                            }
                        }
                    }
                    for (std::size_t i = 1u; i < n; ++i)
                    {
                        if (!is_op(aSource, t[i], ":="))
                            continue;
                        auto const& target = t[i - 1u];
                        // (the parser's: a target that is not a name)
                        std::string kind;
                        std::size_t targetBegin = i - 1u;
                        if (target.kind == token::Number || target.kind == token::String)
                            kind = "literal";
                        else if (is_name(aSource, target, "None") || is_name(aSource, target, "True") || is_name(aSource, target, "False"))
                            kind = std::string{ text_of(aSource, target) };
                        else if (target.kind == token::Name && i >= 2u && is_op(aSource, t[i - 2u], "."))
                        {
                            kind = "attribute";
                            targetBegin = primary_start(aSource, t, i - 1u);
                        }
                        else if (is_op(aSource, target, ")") || is_op(aSource, target, "]"))
                        {
                            auto const open = matching(aSource, t, i - 1u);
                            if (open < n)
                            {
                                targetBegin = applied(open) ? primary_start(aSource, t, i - 1u) : open;
                                if (applied(open))
                                    kind = is_op(aSource, target, ")") ? "function call" : "subscript";
                                else if (is_op(aSource, target, "]"))
                                    kind = "list";
                                else
                                {
                                    bool comma = open + 1u == i - 1u;
                                    for (auto j = open + 1u; j + 1u < i; ++j)
                                        if (d[j] == d[open] + 1 && is_op(aSource, t[j], ","))
                                            comma = true;
                                    if (comma)
                                        kind = "tuple";
                                }
                            }
                        }
                        if (!kind.empty())
                        {
                            add(syntax_issue::Parser, "cannot use assignment expressions with " + kind, t[targetBegin].begin, target.end);
                            continue;
                        }
                        if (target.kind != token::Name || keyword(aSource, target))
                            continue;
                        // (the parser's: the body of a lambda ("lambda: x := 1": the lambda is the target), a keyword argument's or a default 
                        // value's (f(a=b := 1)), a def's parameter's)
                        {
                            std::optional<std::size_t> lambda;
                            for (auto j = i - 1u; j-- > 0u;)
                                if (d[j] == d[i] && is_name(aSource, t[j], "lambda") && lambda_contains(j, i - 1u) && lambda_colon(j) + 2u == i)
                                {
                                    lambda = j;
                                    break;
                                }
                            if (lambda)
                            {
                                if (*lambda > 0u && is_op(aSource, t[*lambda - 1u], ":="))
                                    add(syntax_issue::Parser, "invalid syntax", t[i].begin, t[i].end);
                                else
                                    add(syntax_issue::Parser, "cannot use assignment expressions with lambda", t[*lambda].begin, target.end);
                                continue;
                            }
                            if ((i >= 2u && d[i - 2u] == d[i] && d[i] > 0 && is_op(aSource, t[i - 2u], "=")) ||
                                (defParameters && i > defParameters->first && i < defParameters->second && d[i] == d[defParameters->first] + 1))
                            {
                                add(syntax_issue::Parser, "invalid syntax", t[i].begin, t[i].end);
                                continue;
                            }
                        }
                        // (the symbol table's: within comprehensions, innermost first)
                        auto const name = text_of(aSource, target);
                        bool function = false;          // (a lambda contains it: the comprehensions beyond it are not its scope's)
                        bool comprehension = false;     // (a comprehension's, its scope's)
                        bool reported = false;
                        // (within any comprehension's iterable, a nested comprehension's or lambda's included: an error first)
                        {
                            auto level = d[i];
                            for (std::size_t j = i - 1u; j-- > 0u && level > 0 && !reported;)
                            {
                                if (!opening(aSource, t[j]) || d[j] != level - 1)
                                    continue;
                                level = d[j];
                                auto const c = matching(aSource, t, j);
                                if (c >= n || c < i)
                                    continue;
                                for (auto const& k : clauses_of(j, c))
                                    if (k.isFor && k.in < i && i < k.end)
                                    {
                                        // (unparenthesized: the parser's (an iterable is a disjunction))
                                        if (d[i] == d[j] + 1)
                                            add(syntax_issue::Parser, "invalid syntax", t[i].begin, t[i].end);
                                        else
                                            add(syntax_issue::Symtable, "assignment expression cannot be used in a comprehension iterable expression", target.begin, t[i].end);
                                        reported = true;
                                        break;
                                    }
                            }
                        }
                        auto level = d[i];
                        for (std::size_t j = i - 1u; j-- > 0u && level > 0 && !reported;)
                        {
                            if (d[j] == level && is_name(aSource, t[j], "lambda") && lambda_contains(j, i))
                                function = true;
                            if (!opening(aSource, t[j]) || d[j] != level - 1)
                                continue;
                            level = d[j];
                            auto const c = matching(aSource, t, j);
                            if (c >= n || c < i)
                                continue;
                            auto const clauses = clauses_of(j, c);
                            if (clauses.empty())
                                continue;
                            // (where it is: the element (before the clauses), or a clause)
                            std::optional<std::size_t> in;
                            for (std::size_t k = 0u; k < clauses.size(); ++k)
                                if (clauses[k].begin < i && i < clauses[k].end)
                                    in = k;
                            if (in && clauses[*in].isFor && i > clauses[*in].in)
                            {
                                add(syntax_issue::Symtable, "assignment expression cannot be used in a comprehension iterable expression", target.begin, t[i].end);
                                reported = true;
                                break;
                            }
                            if (function)
                                continue;
                            comprehension = true;
                            for (std::size_t k = 0u; k < clauses.size() && !reported; ++k)
                            {
                                if (!clauses[k].isFor)
                                    continue;
                                auto const names = iteration_names(clauses[k]);
                                auto const found = std::find_if(names.begin(), names.end(), [&](auto const& aName) { return aName.first == name; });
                                if (found == names.end())
                                    continue;
                                // (an inner loop's target: its location)
                                if (!in || k < *in)
                                    add(syntax_issue::Symtable, "assignment expression cannot rebind comprehension iteration variable '" + std::string{ name } + "'", target.begin, t[i].end);
                                else
                                    add(syntax_issue::Symtable, "comprehension inner loop cannot rebind assignment expression target '" + std::string{ name } + "'", 
                                        t[found->second].begin, t[found->second].end);
                                reported = true;
                            }
                        }
                        if (!reported && comprehension && !function)
                            if (auto const* const enclosing = scope(); enclosing != nullptr && enclosing->kind == Class)
                                add(syntax_issue::Symtable, "assignment expression within a comprehension cannot be used in a class body", target.begin, t[i].end);
                    }
                }
                // (the symbol table's: the names that a scope's statements bind and use, its global and nonlocal declarations (a name that
                // is a parameter, or that is bound or used before its declaration, or both global and nonlocal), import * (the module's only); 
                // the parser's: an except clause without a type before another (or except* without one))
                {
                    auto const n = t.size();
                    // (the tokens of comprehensions and of lambdas: their scopes' names, not the line's)
                    std::vector<bool> excluded(n, false);
                    for (std::size_t o = 0u; o < n; ++o)
                    {
                        if (opening(aSource, t[o]))
                        {
                            auto const c = matching(aSource, t, o);
                            if (c >= n)
                                continue;
                            bool comprehension = false;
                            for (auto j = o + 1u; j < c && !comprehension; ++j)
                                comprehension = d[j] == d[o] + 1 && is_name(aSource, t[j], "for");
                            if (comprehension)
                                for (auto j = o; j <= c; ++j)
                                    excluded[j] = true;
                        }
                        else if (is_name(aSource, t[o], "lambda"))
                        {
                            auto j = o + 1u;
                            bool body = false;
                            int lambdas = 0;
                            for (; j < n; ++j)
                            {
                                if (d[j] < d[o])
                                    break;
                                if (d[j] != d[o])
                                    continue;
                                if (!body)
                                {
                                    if (is_name(aSource, t[j], "lambda"))
                                        ++lambdas;
                                    else if (is_op(aSource, t[j], ":") && lambdas-- == 0)
                                        body = true;
                                    continue;
                                }
                                if (is_op(aSource, t[j], ",") || is_op(aSource, t[j], ";") || is_op(aSource, t[j], "=") || is_name(aSource, t[j], "for") || 
                                    (is_op(aSource, t[j], ":") && j > 0u))
                                    break;
                            }
                            for (auto k = o; k < j; ++k)
                                excluded[k] = true;
                        }
                    }
                    // (a name that is a plain target (not an attribute's, subscript's or call's object))
                    auto const plain_target = [&](std::size_t aAt, std::size_t aBegin)
                    {
                        if (aAt + 1u < n && (is_op(aSource, t[aAt + 1u], ".") || is_op(aSource, t[aAt + 1u], "(") || is_op(aSource, t[aAt + 1u], "[")))
                            return false;
                        // (its brackets: groups, not calls or subscripts)
                        auto level = d[aAt];
                        for (auto j = aAt; j-- > aBegin && level > 0;)
                            if (opening(aSource, t[j]) && d[j] == level - 1)
                            {
                                if (j > 0u && ends_expression(aSource, t[j - 1u]) && j > aBegin)
                                    return false;
                                level = d[j];
                            }
                        return level == d[aBegin];
                    };
                    auto const record_names = [&](block& aScope, std::size_t aBegin, std::size_t aEnd, std::optional<std::size_t> aTargetsEnd, std::size_t aTargetsBegin)
                    {
                        for (auto i = aBegin; i < aEnd; ++i)
                        {
                            if (excluded[i] || t[i].kind != token::Name || keyword(aSource, t[i]) || (i > 0u && is_op(aSource, t[i - 1u], ".")))
                                continue;
                            // (a keyword argument's name)
                            if (d[i] > 0 && i + 1u < n && is_op(aSource, t[i + 1u], "=") && i > 0u && (is_op(aSource, t[i - 1u], "(") || is_op(aSource, t[i - 1u], ",")))
                                continue;
                            std::string const name{ text_of(aSource, t[i]) };
                            bool const target = (aTargetsEnd && i >= aTargetsBegin && i < *aTargetsEnd && plain_target(i, aTargetsBegin)) || 
                                (i + 1u < n && is_op(aSource, t[i + 1u], ":="));
                            (target ? aScope.assigned : aScope.used).insert(name);
                        }
                    };
                    static char const* const sAugmented[] = { "+=", "-=", "*=", "/=", "//=", "%=", "**=", ">>=", "<<=", "&=", "|=", "^=", "@=" };
                    // (the header's scope: not its inline body's block)
                    block* headerScope = &moduleNames;
                    {
                        bool const inline_ = opened && start < n && !blocks.empty() && blocks.back().lastLine == l && blocks.back().indent == line.indent + 1u;
                        for (auto b = blocks.rbegin(); b != blocks.rend(); ++b)
                            if ((!inline_ || b != blocks.rbegin()) && (b->kind == Function || b->kind == AsyncFunction || b->kind == Class))
                            {
                                headerScope = &*b;
                                break;
                            }
                    }
                    if (opened && start > 0u)
                    {
                        auto const h = async ? 1u : 0u;
                        auto const headerEnd = start - 1u;   // (its colon)
                        if (is_name(aSource, t[h], "def") || is_name(aSource, t[h], "class"))
                        {
                            if (h + 1u < headerEnd && t[h + 1u].kind == token::Name)
                                headerScope->assigned.insert(std::string{ text_of(aSource, t[h + 1u]) });
                        }
                        else if (is_name(aSource, t[h], "for"))
                        {
                            std::size_t in = headerEnd;
                            for (auto j = h + 1u; j < headerEnd; ++j)
                                if (d[j] == 0 && is_name(aSource, t[j], "in"))
                                {
                                    in = j;
                                    break;
                                }
                            record_names(*headerScope, h + 1u, headerEnd, in, h + 1u);
                        }
                        else
                        {
                            // (with ... as x, except E as x: x is bound)
                            for (auto j = h + 1u; j < headerEnd; ++j)
                            {
                                if (j > 0u && is_name(aSource, t[j - 1u], "as") && t[j].kind == token::Name && !excluded[j])
                                    headerScope->assigned.insert(std::string{ text_of(aSource, t[j]) });
                                else
                                    record_names(*headerScope, j, j + 1u, std::nullopt, 0u);
                            }
                        }
                        // (except clauses: one without a type is the last)
                        if (is_name(aSource, t[0], "try"))
                            bareExcepts[line.indent].reset();
                        else if (is_name(aSource, t[0], "except"))
                        {
                            auto& bare = bareExcepts[line.indent];
                            if (bare)
                                add(syntax_issue::Parser, "default 'except:' must be last", bare->first, bare->second);
                            else if (headerEnd == 1u)
                                bare = std::make_pair(t[0].begin, t[n - 1u].end);
                            else if (headerEnd == 2u && is_op(aSource, t[1], "*"))
                                add(syntax_issue::Parser, "expected one or more exception types", t[2].begin, t[2].end);
                        }
                    }
                    if (!(opened && start > 0u && is_name(aSource, t[0], "except")))
                        if (auto b = bareExcepts.find(line.indent); b != bareExcepts.end() && !is_name(aSource, t[0], "else") && !is_name(aSource, t[0], "finally"))
                            bareExcepts.erase(b);
                    if (opened)
                        futureAllowed = docstringAllowed = false;
                    for (auto const& st : statements)
                    {
                        if (st.begin >= st.end)
                            continue;
                        // (from __future__ import: its features (the compiler's future.c))
                        if (st.begin + 3u < st.end && is_name(aSource, t[st.begin], "from") && is_name(aSource, t[st.begin + 1u], "__future__") && 
                            is_name(aSource, t[st.begin + 2u], "import"))
                        {
                            if (!futureAllowed || opened || line.indent > 0u)
                                add(syntax_issue::Symtable, "from __future__ imports must occur at the beginning of the file", t[st.begin].begin, t[st.end - 1u].end);
                            else
                            {
                                static std::set<std::string_view> const sFeatures = { "nested_scopes", "generators", "division", "absolute_import", 
                                    "with_statement", "print_function", "unicode_literals", "barry_as_FLUFL", "generator_stop", "annotations" };
                                for (auto j = st.begin + 3u; j < st.end; ++j)
                                {
                                    if (t[j].kind != token::Name || (j > 0u && is_name(aSource, t[j - 1u], "as")) || is_name(aSource, t[j], "as"))
                                        continue;
                                    auto const feature = text_of(aSource, t[j]);
                                    if (feature == "braces")
                                        add(syntax_issue::Symtable, "not a chance", t[j].begin, t[j].end);
                                    else if (!sFeatures.contains(feature))
                                        add(syntax_issue::Symtable, "future feature " + std::string{ feature } + " is not defined", t[j].begin, t[j].end);
                                    else
                                        continue;
                                    break;
                                }
                            }
                            docstringAllowed = false;
                        }
                        else if (docstringAllowed && st.end == st.begin + 1u && t[st.begin].kind == token::String)
                            docstringAllowed = false;
                        else
                            futureAllowed = docstringAllowed = false;
                        auto* const statementScope = const_cast<block*>(scope());
                        auto& names = statementScope != nullptr ? *statementScope : moduleNames;
                        auto const& f = t[st.begin];
                        std::pair<std::size_t, std::size_t> const span{ f.begin, t[st.end - 1u].end };
                        bool const global = is_name(aSource, f, "global");
                        if (global || is_name(aSource, f, "nonlocal"))
                        {
                            std::string const kind = global ? "global" : "nonlocal";
                            for (auto j = st.begin + 1u; j < st.end; ++j)
                            {
                                if (t[j].kind != token::Name)
                                    continue;
                                std::string const name{ text_of(aSource, t[j]) };
                                std::optional<std::string> message;
                                std::pair<std::size_t, std::size_t> at = span;
                                if (std::find(names.parameters.begin(), names.parameters.end(), name) != names.parameters.end())
                                    message = "name '" + name + "' is parameter and " + kind;
                                else if (names.assigned.contains(name))
                                    message = "name '" + name + "' is assigned to before " + kind + " declaration";
                                else if (names.used.contains(name))
                                    message = "name '" + name + "' is used prior to " + kind + " declaration";
                                else if (auto const other = global ? names.nonlocals.find(name) : names.globals.find(name); other != (global ? names.nonlocals.end() : names.globals.end()))
                                {
                                    message = "name '" + name + "' is nonlocal and global";
                                    at = other->second;
                                }
                                if (message && (global || statementScope != nullptr))
                                {
                                    add(syntax_issue::Symtable, *message, at.first, at.second);
                                    break;
                                }
                                (global ? names.globals : names.nonlocals).emplace(name, span);
                            }
                            continue;
                        }
                        if (is_name(aSource, f, "from") || is_name(aSource, f, "import"))
                        {
                            if (is_name(aSource, f, "from") && statementScope != nullptr && is_op(aSource, t[st.end - 1u], "*"))
                                add(syntax_issue::Symtable, "import * only allowed at module level", t[st.end - 1u].begin, t[st.end - 1u].end);
                            continue;
                        }
                        if (is_name(aSource, f, "del"))
                            continue;
                        // (an assignment's targets: before its last '=' (or an augmented assignment's operator); an annotation: not recorded)
                        std::optional<std::size_t> targetsEnd;
                        bool annotated = false;
                        for (auto j = st.begin; j < st.end; ++j)
                        {
                            if (d[j] != 0 || excluded[j])
                                continue;
                            if (is_op(aSource, t[j], "="))
                                targetsEnd = j;
                            else if (std::any_of(std::begin(sAugmented), std::end(sAugmented), [&](char const* aOp) { return is_op(aSource, t[j], aOp); }))
                            {
                                targetsEnd = j;
                                break;
                            }
                            else if (is_op(aSource, t[j], ":"))
                                annotated = true;
                        }
                        if (!annotated)
                            record_names(names, st.begin, st.end, targetsEnd, st.begin);
                    }
                }
                for (auto const& s : statements)
                {
                    if (s.begin >= s.end)
                        continue;
                    auto const& first = t[s.begin];
                    auto const end = t[s.end - 1u].end;
                    // (lambdas are functions)
                    bool lambda = false;
                    for (std::size_t i = s.begin; i < s.end; ++i)
                        if (is_name(aSource, t[i], "lambda"))
                            lambda = true;
                    auto const* const enclosing = scope();
                    bool const function = enclosing != nullptr && enclosing->kind != Class;
                    // (an async function's yields (not a lambda's, or a generator expression's): it is an async generator; yield from: an error;
                    // and its first return with a value)
                    if (function && !lambda && enclosing->kind == AsyncFunction)
                    {
                        auto& f = const_cast<block&>(*enclosing);
                        for (std::size_t i = s.begin; i < s.end; ++i)
                            if (is_name(aSource, t[i], "yield"))
                            {
                                bool inBrackets = false;
                                for (std::size_t j = i; j-- > s.begin && !inBrackets;)
                                    if (d[j] < d[i] && is_op(aSource, t[j], "("))
                                    {
                                        // (a generator expression's: its brackets have a for)
                                        auto const close = std::min(matching(aSource, t, j), s.end);
                                        for (auto k = j + 1u; k < close; ++k)
                                            if (d[k] == d[j] + 1 && is_name(aSource, t[k], "for"))
                                                inBrackets = true;
                                        break;
                                    }
                                if (inBrackets)
                                    continue;
                                f.yields = true;
                                if (i + 1u < s.end && is_name(aSource, t[i + 1u], "from"))
                                {
                                    auto e = s.end;
                                    for (std::size_t j = i + 1u; j < s.end; ++j)
                                        if (d[j] < d[i] || (d[j] == d[i] && closing(aSource, t[j])))
                                        {
                                            e = j;
                                            break;
                                        }
                                    add(syntax_issue::Compiler, "'yield from' inside async function", t[i].begin, t[e - 1u].end);
                                }
                            }
                        if (is_name(aSource, first, "return") && s.end - s.begin > 1u && !f.returned)
                            f.returned = std::make_pair(first.begin, end);
                    }
                    if (is_name(aSource, first, "return") && !function)
                        add(syntax_issue::Compiler, "'return' outside function", first.begin, end);
                    else if (is_name(aSource, first, "break") && !in_loop())
                        add(syntax_issue::Compiler, "'break' outside loop", first.begin, first.end);
                    else if (is_name(aSource, first, "continue") && !in_loop())
                        add(syntax_issue::Compiler, "'continue' not properly in loop", first.begin, first.end);
                    else if (is_name(aSource, first, "nonlocal") && !in_function())
                        add(syntax_issue::Symtable, "nonlocal declaration not allowed at module level", first.begin, end);
                    if (!lambda && !function)
                        for (std::size_t i = s.begin; i < s.end; ++i)
                            if (is_name(aSource, t[i], "yield") || is_name(aSource, t[i], "await"))
                            {
                                // (to the end of its expression: of its brackets, or the statement)
                                auto e = s.end;
                                for (std::size_t j = i + 1u; j < s.end; ++j)
                                    if (d[j] < d[i] || (d[j] == d[i] && closing(aSource, t[j])))
                                    {
                                        e = j;
                                        break;
                                    }
                                add(syntax_issue::Compiler, is_name(aSource, t[i], "yield") ? "'yield' outside function" : "'await' outside function", t[i].begin, t[e - 1u].end);
                                break;
                            }
                    // (an await within a generator expression (in a function that is not async): an asynchronous generator expression)
                    auto const in_generator_expression = [&](std::size_t aAt)
                    {
                        auto level = d[aAt];
                        for (std::size_t j = aAt; j-- > s.begin && level > 0;)
                            if (opening(aSource, t[j]) && d[j] == level - 1)
                            {
                                if (is_op(aSource, t[j], "("))
                                {
                                    auto const close = std::min(matching(aSource, t, j), s.end);
                                    for (auto k = j + 1u; k < close; ++k)
                                        if (d[k] == d[j] + 1 && is_name(aSource, t[k], "for"))
                                            return true;
                                }
                                --level;
                            }
                        return false;
                    };
                    if (!lambda && function && enclosing->kind == Function)
                        for (std::size_t i = s.begin; i < s.end; ++i)
                            if (is_name(aSource, t[i], "await") && !in_generator_expression(i))
                            {
                                auto e = s.end;
                                for (std::size_t j = i + 1u; j < s.end; ++j)
                                    if (d[j] < d[i] || (d[j] == d[i] && closing(aSource, t[j])))
                                    {
                                        e = j;
                                        break;
                                    }
                                add(syntax_issue::Compiler, "'await' outside async function", t[i].begin, t[e - 1u].end);
                                break;
                            }
                    // (lambda's parameters)
                    for (std::size_t i = s.begin; i < s.end; ++i)
                        if (is_name(aSource, t[i], "lambda"))
                        {
                            // (its colon: not that of a lambda in a parameter's default ("lambda x = lambda: 1: x()"))
                            auto j = i + 1u;
                            for (int lambdas = 0; j < s.end; ++j)
                                if (d[j] == d[i] && is_name(aSource, t[j], "lambda"))
                                    ++lambdas;
                                else if (d[j] == d[i] && is_op(aSource, t[j], ":"))
                                {
                                    if (lambdas == 0)
                                        break;
                                    --lambdas;
                                }
                            if (j < s.end)
                            {
                                // (as a def's: aOpen is the lambda)
                                std::vector<token> params(t.begin() + static_cast<std::ptrdiff_t>(i), t.begin() + static_cast<std::ptrdiff_t>(j));
                                std::vector<std::string_view> names;
                                bool expectName = true;
                                auto const pd = depths(aSource, params);
                                for (std::size_t p = 1u; p < params.size(); ++p)
                                {
                                    if (pd[p] != pd[0])
                                        continue;
                                    if (is_op(aSource, params[p], ","))
                                        expectName = true;
                                    else if (is_op(aSource, params[p], "*") || is_op(aSource, params[p], "**") || is_op(aSource, params[p], "/"))
                                        continue;
                                    else
                                    {
                                        if (expectName && params[p].kind == token::Name)
                                        {
                                            auto const name = text_of(aSource, params[p]);
                                            if (std::find(names.begin(), names.end(), name) != names.end())
                                                aIssues.push_back(make_issue(aSource, syntax_issue::Symtable, "duplicate argument '" + std::string{ name } +
                                                    "' in function definition", params[p].begin, params[p].end));
                                            names.push_back(name);
                                        }
                                        expectName = false;
                                    }
                                }
                                // (a * without a name: names must follow it)
                                std::optional<std::size_t> bareStar;
                                for (std::size_t p = 1u; p <= params.size(); ++p)
                                {
                                    if (p < params.size() && pd[p] != pd[0])
                                        continue;
                                    if (p == params.size() || is_op(aSource, params[p], "**"))
                                    {
                                        // (at the token that follows it (and its comma), as CPython's invalid_lambda_star_etc)
                                        if (bareStar)
                                        {
                                            auto const after = i + *bareStar + (is_op(aSource, t[i + *bareStar + 1u], ",") ? 2u : 1u);
                                            add(syntax_issue::Parser, "named arguments must follow bare *", t[after].begin, t[after].end);
                                        }
                                        break;
                                    }
                                    if (is_op(aSource, params[p], "*") && (p + 1u == params.size() || is_op(aSource, params[p + 1u], ",")))
                                        bareStar = p;
                                    else if (params[p].kind == token::Name)
                                        bareStar.reset();
                                }
                            }
                        }
                    // (the parser's: x := v as a statement; an assignment to (or del of) what is not a target)
                    for (std::size_t i = s.begin + 1u; i < s.end && !is_op(aSource, first, "@"); ++i)
                        if (d[i] == 0 && is_op(aSource, t[i], ":=") && t[i - 1u].kind == token::Name && !keyword(aSource, t[i - 1u]))
                        {
                            add(syntax_issue::Parser, "invalid syntax", t[i].begin, t[i].end);
                            break;
                        }
                    // (the parser's: an augmented assignment to several targets (a, b += 1): a tuple)
                    {
                        static char const* const sAugmented[] = { "+=", "-=", "*=", "/=", "//=", "%=", "**=", ">>=", "<<=", "&=", "|=", "^=", "@=" };
                        bool comma = false;
                        for (std::size_t i = s.begin; i < s.end; ++i)
                        {
                            if (d[i] != 0)
                                continue;
                            // (not after an assignment's "=", or a compound statement's header (for a, b in x: c += 1))
                            if (is_op(aSource, t[i], "=") || is_op(aSource, t[i], ":"))
                                break;
                            if (is_op(aSource, t[i], ","))
                            {
                                comma = true;
                                continue;
                            }
                            if (std::any_of(std::begin(sAugmented), std::end(sAugmented), [&](char const* aOp) { return is_op(aSource, t[i], aOp); }))
                            {
                                if (comma && i > s.begin)
                                    add(syntax_issue::Parser, "'tuple' is an illegal expression for augmented assignment", t[s.begin].begin, t[i - 1u].end);
                                break;
                            }
                        }
                    }
                    if (is_name(aSource, first, "del"))
                    {
                        std::size_t targetStart = s.begin + 1u;
                        for (std::size_t i = s.begin + 1u; i <= s.end; ++i)
                            if (i == s.end || (d[i] == 0 && is_op(aSource, t[i], ",")))
                            {
                                auto const kind = invalid_target(aSource, t, targetStart, i);
                                if (!kind.empty() && targetStart < i)
                                {
                                    add(syntax_issue::Parser, "cannot delete " + std::string{ kind == "None" || kind == "True" || kind == "False" ? kind : kind },
                                        t[targetStart].begin, t[i - 1u].end);
                                    break;
                                }
                                targetStart = i + 1u;
                            }
                    }
                    else
                    {
                        std::size_t equals = s.end;
                        for (std::size_t i = s.begin; i < s.end; ++i)
                            if (d[i] == 0 && is_op(aSource, t[i], "="))
                            {
                                equals = i;
                                break;
                            }
                        if (equals < s.end && !is_name(aSource, first, "lambda"))
                        {
                            auto const kind = invalid_target(aSource, t, s.begin, equals);
                            if (kind == "None" || kind == "True" || kind == "False")
                                add(syntax_issue::Parser, "cannot assign to " + kind, t[s.begin].begin, t[equals - 1u].end);
                            else if (!kind.empty())
                                add(syntax_issue::Parser, "cannot assign to " + kind + " here. Maybe you meant '==' instead of '='?", t[s.begin].begin,
                                    t[equals - 1u].end);
                        }
                    }
                }
                if (opened && blocks.empty() == false && blocks.back().indent == line.indent + 1u && blocks.back().lastLine == l && !statements.empty())
                {
                    // (a body on its header's line: the block ends with it)
                    auto const b = blocks.back();
                    blocks.pop_back();
                    finish_block(b);
                }
                else if (opened)
                {
                    blocks.push_back({ line.indent, *opened, l });
                    if (*opened == Function || *opened == AsyncFunction)
                        blocks.back().parameters = headerParameters;
                }
            }
            close_blocks(0u, std::nullopt);
        }
    }

    std::vector<syntax_issue> syntax_issues(std::string const& aSource, bool aExpression)
    {
        std::vector<syntax_issue> result;
        if (auto const issue = tokenizer_issue(aSource, aExpression))
        {
            result.push_back(*issue);
            return result;
        }
        if (!aExpression)
            statement_issues(aSource, result);
        std::stable_sort(result.begin(), result.end(), [](syntax_issue const& a, syntax_issue const& b) { return a.position < b.position; });
        return result;
    }

    syntax_issue parse_failure_issue(std::string const& aSource, std::size_t aPosition, bool aExpression)
    {
        aPosition = std::min(aPosition, aSource.size());
        auto const lines = tokens::tokenize(aSource);
        // (the logical line of the position: the last that begins at or before it)
        tokens::logical_line const* line = nullptr;
        for (auto const& l : lines)
            if (l.begin <= aPosition)
                line = &l;
        auto const generic = [&]()
        {
            // (the token at the position: the extent)
            std::size_t end = aPosition + 1u;
            if (line != nullptr)
                for (auto const& t : line->tokens)
                    if (t.begin == aPosition)
                        end = t.end;
            if (aExpression && aSource.find_first_not_of(" \t\r\n", aPosition) == std::string::npos)
            {
                // (eval: at the end of its source)
                auto issue = make_issue(aSource, syntax_issue::Parser, "invalid syntax", aPosition, aPosition);
                issue.offset = 0;
                issue.endOffset = 0;
                issue.lineText = false;
                return issue;
            }
            return make_issue(aSource, syntax_issue::Parser, "invalid syntax", aPosition, end);
        };
        if (line == nullptr || line->tokens.empty())
            return generic();
        auto const& t = line->tokens;
        auto const d = depths(aSource, t);
        std::size_t at = t.size();      // (the token at the position (or after it))
        for (std::size_t i = 0u; i < t.size(); ++i)
            if (t[i].end > aPosition)
            {
                at = i;
                break;
            }
        // (a compound statement's header with no colon: expected ':')
        bool const async = is_name(aSource, t[0], "async") && t.size() > 1u;
        auto const& head = async ? t[1] : t[0];
        // (class C: the parser's error is at its name)
        if ((at == t.size() || (is_name(aSource, head, "class") && t.size() == (async ? 3u : 2u) && at == t.size() - 1u)) && !aExpression)
        {
            bool compound = false;
            for (auto k : { "if", "elif", "else", "while", "for", "with", "try", "except", "finally", "def", "class" })
                if (is_name(aSource, head, k))
                    compound = true;
            bool colon = false;
            for (std::size_t i = 0u; i < t.size(); ++i)
                if (d[i] == 0 && is_op(aSource, t[i], ":"))
                    colon = true;
            if (compound && !colon)
                return make_issue(aSource, syntax_issue::Parser, "expected ':'", t.back().end, t.back().end + 1u);
        }
        // (print "x": Python 2's print statement)
        if (t.size() >= 2u && (is_name(aSource, t[0], "print") || is_name(aSource, t[0], "exec")) && at == 1u && starts_expression(aSource, t[1]))
            return make_issue(aSource, syntax_issue::Parser, "Missing parentheses in call to '" + std::string{ text_of(aSource, t[0]) } + "'. Did you mean " +
                std::string{ text_of(aSource, t[0]) } + "(...)?", t[0].begin, t.back().end);
        // (two expressions within brackets: a comma forgotten)
        if (at < t.size() && at > 0u && d[at] > 0 && ends_expression(aSource, t[at - 1u]) && starts_expression(aSource, t[at]))
            return make_issue(aSource, syntax_issue::Parser, "invalid syntax. Perhaps you forgot a comma?", t[primary_start(aSource, t, at - 1u)].begin, t[at].end);
        return generic();
    }

    // (a source whose tokens are not valid: its lines joined within its brackets (as the tokenizer joins them), so that the parser's error 
    // is where CPython's is)
    std::string join_bracketed_lines(std::string const& aSource)
    {
        std::string result = aSource;
        int depth = 0;
        auto const n = result.size();
        for (std::size_t i = 0u; i < n; ++i)
        {
            char const c = result[i];
            if (c == '\'' || c == '"')
            {
                bool const triple = i + 2u < n && result[i + 1u] == c && result[i + 2u] == c;
                std::size_t j = i + (triple ? 3u : 1u);
                for (; j < n; ++j)
                {
                    if (result[j] == '\\')
                        j = tokens::escape_last(result, j);
                    else if (result[j] == c && (!triple || (j + 2u < n && result[j + 1u] == c && result[j + 2u] == c)))
                    {
                        j += triple ? 2u : 0u;
                        break;
                    }
                    else if (!triple && result[j] == '\n')
                    {
                        --j;
                        break;
                    }
                }
                i = j;
                continue;
            }
            if (c == '#')
            {
                for (; i < n && result[i] != '\n'; ++i)
                    if (depth > 0)
                        result[i] = ' ';
                --i;
                continue;
            }
            if (c == '(' || c == '[' || c == '{')
                ++depth;
            else if ((c == ')' || c == ']' || c == '}') && depth > 0)
                --depth;
            else if (depth > 0 && (c == '\n' || c == '\r'))
                result[i] = ' ';
        }
        return result;
    }

    // (the line of a source (without its newline))
    std::string source_line(std::string const& aSource, std::uint32_t aLine)
    {
        std::size_t begin = 0u;
        for (std::uint32_t l = 1u; l < aLine && begin < aSource.size(); ++l)
        {
            auto const next = aSource.find('\n', begin);
            if (next == std::string::npos)
                return {};
            begin = next + 1u;
        }
        return aSource.substr(begin, content_end(aSource, begin) - begin);
    }

    // (traceback.py's _format_syntax_error: the report of a syntax error)
    std::string syntax_error_report(std::string const& aFile, std::string const& aSource, syntax_issue const& aIssue)
    {
        std::string result = "  File \"" + aFile + "\", line " + std::to_string(aIssue.line) + "\n";
        auto const rtext = source_line(aSource, aIssue.line);
        auto const first = rtext.find_first_not_of(" \n\f");
        std::string const ltext = first == std::string::npos ? std::string{} : rtext.substr(first);
        auto const spaces = static_cast<std::int64_t>(rtext.size() - ltext.size());
        result += "    " + ltext + "\n";
        // (in characters)
        auto const characters = [](std::string const& aText)
        {
            std::int64_t count = 0;
            for (auto c : aText)
                if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u)
                    ++count;
            return count;
        };
        auto const length = characters(aIssue.lineText ? rtext + "\n" : rtext);
        auto offset = aIssue.offset;
        auto endOffset = aIssue.endOffset != 0 ? aIssue.endOffset : offset;
        if (offset > length)
            offset = length + 1;
        if (endOffset > length)
            endOffset = length + 1;
        if (offset >= endOffset || endOffset < 0)
            endOffset = offset + 1;
        auto const column = offset - 1 - spaces;
        auto const endColumn = endOffset - 1 - spaces;
        if (column >= 0)
        {
            std::string caret = "    ";
            std::int64_t index = 0;
            for (std::size_t i = 0u; i < ltext.size() && index < column; ++i)
            {
                auto const c = ltext[i];
                if ((static_cast<unsigned char>(c) & 0xC0u) == 0x80u)
                    continue;
                caret += c == '\t' || c == '\f' ? c : ' ';
                ++index;
            }
            caret += std::string(static_cast<std::size_t>(std::max<std::int64_t>(endColumn - column, 0)), '^');
            result += caret + "\n";
        }
        return result + aIssue.type + ": " + aIssue.message + "\n";
    }
}
