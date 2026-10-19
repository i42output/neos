/*
  syntax.cpp

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

// A module's syntax errors as CPython 3.13 reports them: those of its tokenizer (strings, brackets, indentation), of its parser (that the
// grammar does not reject, or whose messages are CPython's own), of its symbol table and of its compiler; the errors' locations (lines and
// character offsets) and the report of one (traceback.py's _format_syntax_error)

#include <neolib/neolib.hpp>
#include <algorithm>
#include <optional>
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
                    for (; j < n; ++j)
                    {
                        if (aSource[j] == '\\')
                        {
                            if (j + 1u < n)
                                ++j;
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
            struct block { std::size_t indent; kind_t kind; std::size_t lastLine; };
            std::vector<block> blocks;
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
            // (a try block that ended: its except or finally clause)
            auto const close_blocks = [&](std::size_t aIndent, std::optional<std::size_t> aNext)
            {
                while (!blocks.empty() && blocks.back().indent >= aIndent)
                {
                    auto const b = blocks.back();
                    blocks.pop_back();
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
                    if (is_op(aSource, t, "*") || is_op(aSource, t, "**") || is_op(aSource, t, "/"))
                        continue;
                    if (expectName && t.kind == token::Name)
                    {
                        auto const name = text_of(aSource, t);
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
            };
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
                                    parameters(t, i, matching(aSource, t, i));
                                    break;
                                }
                        start = colon + 1u;
                        if (start < t.size())
                        {
                            // (a body on the header's line: within the block)
                            blocks.push_back({ line.indent + 1u, *opened, l });
                        }
                        break;
                    }
                std::vector<statement> statements;
                for (std::size_t i = start; i < t.size(); ++i)
                    if (d[i] == 0 && is_op(aSource, t[i], ";"))
                    {
                        statements.push_back({ start, i });
                        start = i + 1u;
                    }
                if (start < t.size())
                    statements.push_back({ start, t.size() });
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
                    if (!lambda && function && enclosing->kind == Function)
                        for (std::size_t i = s.begin; i < s.end; ++i)
                            if (is_name(aSource, t[i], "await"))
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
                            auto j = i + 1u;
                            while (j < s.end && !(d[j] == d[i] && is_op(aSource, t[j], ":")))
                                ++j;
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
                            }
                        }
                    // (the parser's: x := v as a statement; an assignment to (or del of) what is not a target)
                    if (s.end - s.begin >= 2u && first.kind == token::Name && is_op(aSource, t[s.begin + 1u], ":="))
                        add(syntax_issue::Parser, "invalid syntax", t[s.begin + 1u].begin, t[s.begin + 1u].end);
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
                    blocks.pop_back();  // (a body on its header's line: the block ends with it)
                else if (opened)
                    blocks.push_back({ line.indent, *opened, l });
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
                        ++j;
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
