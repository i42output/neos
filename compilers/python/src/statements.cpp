/*
  statements.cpp

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

// Compound statements that the compiler compiles as other statements: a rewrite of a module's source (after its lines have
// been joined within brackets, before its classes are rewritten); each statement keeps its line (lines are not added)
//
// - A loop's else clause: a flag (_eN_) that the loop's header sets and its breaks clear; "else:" is "if _eN_:".
//   "for x in it:" is "for x in ((_eN_ := True) and (it)):"; "while c:" is "while (_eN_ := True) and (c):"; a break
//   of the loop is "_eN_ = False; break".
// - A with statement of several items: "with A as a, B as b, C:" is "with __with_items('110', lambda: (A),
//   lambda a: (B), lambda a, b: (C)) as (a, b):" (each item's manager is evaluated once the items before it have been
//   entered (their targets are its lambda's parameters); __with_items enters and exits them as nested with statements
//   would); "with (A as a, B):" is "with A as a, B:".
// - await E is __await(E) (E: a primary expression); "async for x in it:" is "for x in __async_iter(it):" (also in
//   comprehensions) and "async with A as a:" is "with __async_with(A) as a:" (the runtime's: their __next__, __enter__
//   and __exit__ await the asynchronous iterator's and context manager's methods: a coroutine's body runs on a coroutine 
//   of its own (generators.cpp), on which awaits suspend it, so they can be awaited in functions that it calls).
// - A match statement: "match S:" is "if ((_mN_ := (S)),):" (its subject, evaluated once), and its cases are an if 
//   statement's clauses: "case P if G:" is "if <P matches _mN_> and (G):" (then elif). A pattern of literals and values (and 
//   their alternatives) is comparisons ("_mN_ == (1) or _mN_ is None"), a capture is "[(x := _mN_)]" and the wildcard is
//   True; other patterns are __match(_mN_, '<descriptor>', <values>) (the runtime's support.cpp: the descriptor's syntax)
//   whose captures are bound from its result: "(_cN_ := __match(...)) and [(x := _cN_[0]), (y := _cN_[1])]".
//
// Class statements (rewrite_classes): "class C(B, metaclass=M):" is "def _bN_C(__bases__=(B,), __kw__=dict(metaclass=M),
// __class__=None):": a function whose body is the class's body and which returns its namespace (the compiler's implicit 
// return of a class body); closures.cpp defines it at the end of the module (_aN_C) and its closure is made into the class
// where the class statement is ("C = __build_class(__closure(_aN_C, ...))").
//
// Comprehensions (rewrite_comprehensions): a comprehension's variables are its own: "[x for x in xs]" is "[_q1_x for _q1_x 
// in xs]" (its first iterable is not renamed: it is evaluated where the comprehension is); in a class's body a comprehension 
// is a lambda's ("(lambda _qit: [... for ... in _qit])(xs)"), whose scope is not the class's. super() in a method is super(__class__, <its first 
// parameter>) (__class__: a cell of the class body that the class is put in once it has been made), and the class's 
// private names (__x) are mangled (_C__x).

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include "tokens.hpp"

namespace neos::compilers::python
{
    namespace
    {
        using namespace tokens;

        struct edit
        {
            std::size_t begin;
            std::size_t end;
            std::string text;
        };

        class statement_rewriter
        {
        public:
            statement_rewriter(std::string const& aText, int aPass) : iText{ aText }, iLines{ tokenize(aText) }, iPass{ aPass }
            {
            }
        public:
            std::string rewrite(bool& aChanged)
            {
                if (iPass == 0)
                    awaits();
                else if (iPass == 1)
                    async_statements();
                else
                {
                    loop_else();
                    with_items();
                    match_statements();
                    variable_annotations();
                    type_parameters();
                    except_star();
                }
                if (iEdits.empty())
                    return iText;
                aChanged = true;
                std::sort(iEdits.begin(), iEdits.end(), [](edit const& a, edit const& b) { return a.begin > b.begin; });
                std::string result = iText;
                for (auto const& e : iEdits)
                    result.replace(e.begin, e.end - e.begin, e.text);
                return result;
            }
        private:
            token const& tok(std::size_t aLine, std::size_t aToken) const { return iLines[aLine].tokens[aToken]; }
            std::string_view text(token const& aToken) const { return std::string_view{ iText }.substr(aToken.begin, aToken.end - aToken.begin); }
            std::string_view text(std::size_t aLine, std::size_t aToken) const { return text(tok(aLine, aToken)); }
            std::size_t count(std::size_t aLine) const { return iLines[aLine].tokens.size(); }
            bool is_op(std::size_t aLine, std::size_t aToken, std::string_view aOp) const
            {
                return aToken < count(aLine) && tok(aLine, aToken).kind == token::Op && text(aLine, aToken) == aOp;
            }
            bool is_name(std::size_t aLine, std::size_t aToken, std::string_view aName) const
            {
                return aToken < count(aLine) && tok(aLine, aToken).kind == token::Name && text(aLine, aToken) == aName;
            }
            static bool opening(std::string_view aOp) { return aOp == "(" || aOp == "[" || aOp == "{"; }
            static bool closing(std::string_view aOp) { return aOp == ")" || aOp == "]" || aOp == "}"; }
            // (the first token of a line's statement: after "async")
            std::size_t first(std::size_t aLine) const { return is_name(aLine, 0u, "async") ? 1u : 0u; }
            // (the header's colon (at the top level, not a lambda's): the line's token count if none)
            std::size_t header_colon(std::size_t aLine, std::size_t aFrom) const
            {
                int depth = 0;
                int lambdas = 0;
                for (auto t = aFrom; t < count(aLine); ++t)
                {
                    auto const& k = tok(aLine, t);
                    if (k.kind == token::Name && text(k) == "lambda")
                        ++lambdas;
                    if (k.kind != token::Op)
                        continue;
                    auto const op = text(k);
                    if (opening(op))
                        ++depth;
                    else if (closing(op))
                        --depth;
                    else if (op == ":" && depth == 0)
                    {
                        if (lambdas > 0)
                            --lambdas;
                        else
                            return t;
                    }
                }
                return count(aLine);
            }
            // (the tokens [aBegin, aEnd) split at top-level commas)
            std::vector<std::pair<std::size_t, std::size_t>> split(std::size_t aLine, std::size_t aBegin, std::size_t aEnd, std::string_view aSeparator = ",") const
            {
                std::vector<std::pair<std::size_t, std::size_t>> result;
                int depth = 0;
                auto start = aBegin;
                for (auto t = aBegin; t < aEnd; ++t)
                {
                    auto const& k = tok(aLine, t);
                    if (k.kind != token::Op)
                        continue;
                    auto const op = text(k);
                    if (opening(op))
                        ++depth;
                    else if (closing(op))
                        --depth;
                    else if (op == aSeparator && depth == 0)
                    {
                        result.emplace_back(start, t);
                        start = t + 1u;
                    }
                }
                if (start < aEnd)
                    result.emplace_back(start, aEnd);
                return result;
            }
            // (the source of tokens [aBegin, aEnd))
            std::string source(std::size_t aLine, std::size_t aBegin, std::size_t aEnd) const
            {
                if (aBegin >= aEnd)
                    return {};
                return iText.substr(tok(aLine, aBegin).begin, tok(aLine, aEnd - 1u).end - tok(aLine, aBegin).begin);
            }
            void replace(std::size_t aLine, std::size_t aBegin, std::size_t aEnd, std::string aText)
            {
                iEdits.push_back({ tok(aLine, aBegin).begin, tok(aLine, aEnd - 1u).end, std::move(aText) });
            }
            // (the line after the body of the compound statement whose header is aLine)
            std::size_t body_end(std::size_t aLine) const
            {
                auto const indent = iLines[aLine].indent;
                auto l = aLine + 1u;
                while (l < iLines.size() && iLines[l].indent > indent)
                    ++l;
                return l;
            }
            bool header_of(std::size_t aLine, std::initializer_list<std::string_view> aKeywords) const
            {
                auto const f = first(aLine);
                for (auto k : aKeywords)
                    if (is_name(aLine, f, k))
                        return true;
                return false;
            }
            // --- a loop's else clause
            void loop_else()
            {
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                {
                    if (!is_name(l, 0u, "else") || !is_op(l, 1u, ":"))
                        continue;
                    // (the statement that it continues: the previous line of the same indentation)
                    auto const indent = iLines[l].indent;
                    std::optional<std::size_t> header;
                    for (auto p = l; p > 0u;)
                    {
                        --p;
                        if (iLines[p].indent < indent)
                            break;
                        if (iLines[p].indent == indent)
                        {
                            header = p;
                            break;
                        }
                    }
                    if (!header || !header_of(*header, { "for", "while" }))
                        continue;
                    auto const h = *header;
                    auto const flag = "_e" + std::to_string(++iFlags) + "_";
                    auto const f = first(h);
                    auto const colon = header_colon(h, f + 1u);
                    if (is_name(h, f, "while"))
                        replace(h, f + 1u, colon, "(" + flag + " := True) and (" + source(h, f + 1u, colon) + ")");
                    else
                    {
                        // (for targets in iterable)
                        std::size_t in = f + 1u;
                        int depth = 0;
                        for (; in < colon; ++in)
                        {
                            if (tok(h, in).kind == token::Op)
                            {
                                if (opening(text(h, in))) ++depth;
                                else if (closing(text(h, in))) --depth;
                            }
                            else if (depth == 0 && is_name(h, in, "in"))
                                break;
                        }
                        if (in + 1u >= colon)
                            continue;
                        replace(h, in + 1u, colon, "((" + flag + " := True) and (" + source(h, in + 1u, colon) + "))");
                    }
                    // (the loop's breaks: not those of loops, functions or classes within it)
                    break_statements(h, colon + 1u, flag);
                    auto const end = body_end(h);
                    std::vector<std::size_t> barriers;  // (the indentation of the nested headers)
                    for (auto b = h + 1u; b < end && b < l; ++b)
                    {
                        while (!barriers.empty() && iLines[b].indent <= barriers.back())
                            barriers.pop_back();
                        if (!barriers.empty())
                            continue;
                        if (header_of(b, { "for", "while", "def", "class" }))
                        {
                            barriers.push_back(iLines[b].indent);
                            continue;
                        }
                        // (a compound statement's inline body, or simple statements)
                        std::size_t from = 0u;
                        if (header_of(b, { "if", "elif", "else", "try", "except", "finally", "with", "match", "case" }))
                        {
                            bool const bare = is_name(b, first(b), "else") || is_name(b, first(b), "try") || is_name(b, first(b), "finally");
                            from = header_colon(b, first(b) + (bare ? 0u : 1u)) + 1u;
                        }
                        break_statements(b, from, flag);
                    }
                    replace(l, 0u, 2u, "if " + flag + ":");
                }
            }
            // (the break statements among the simple statements of tokens [aFrom, ...) of a line)
            void break_statements(std::size_t aLine, std::size_t aFrom, std::string const& aFlag)
            {
                for (auto const& s : split(aLine, aFrom, count(aLine), ";"))
                    if (s.second == s.first + 1u && is_name(aLine, s.first, "break"))
                        replace(aLine, s.first, s.first + 1u, aFlag + " = False; break");
            }
            // --- with statements of several items
            void with_items()
            {
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                {
                    auto const f = first(l);
                    if (!is_name(l, f, "with"))
                        continue;
                    auto const colon = header_colon(l, f + 1u);
                    if (colon >= count(l))
                        continue;
                    auto begin = f + 1u;
                    auto end = colon;
                    // (with (items):)
                    bool parenthesized = false;
                    if (is_op(l, begin, "(") && end > begin + 1u && is_op(l, end - 1u, ")"))
                    {
                        // (the brackets enclose all of the items)
                        int depth = 0;
                        std::size_t close = begin;
                        for (auto t = begin; t < end; ++t)
                            if (tok(l, t).kind == token::Op)
                            {
                                if (opening(text(l, t))) ++depth;
                                else if (closing(text(l, t)) && --depth == 0)
                                {
                                    close = t;
                                    break;
                                }
                            }
                        if (close == end - 1u)
                        {
                            auto const inner = split(l, begin + 1u, end - 1u);
                            bool hasAs = false;
                            for (auto t = begin + 1u; t < end - 1u; ++t)
                                if (is_name(l, t, "as"))
                                    hasAs = true;
                            // ((A as a, B), (A, B) or (A,): items; (A): a parenthesized expression)
                            if (hasAs || inner.size() > 1u || is_op(l, end - 2u, ","))
                            {
                                parenthesized = true;
                                ++begin;
                                --end;
                            }
                        }
                    }
                    auto items = split(l, begin, end);
                    if (items.size() < 2u)
                    {
                        if (parenthesized)
                        {
                            // (with (A as a): with A as a:)
                            replace(l, f + 1u, f + 2u, " ");
                            replace(l, colon - 1u, colon, " ");
                        }
                        continue;
                    }
                    std::string mask;
                    std::string managers;
                    std::vector<std::string> names;
                    bool simple = true;
                    for (auto const& item : items)
                    {
                        std::optional<std::size_t> as;
                        for (auto t = item.first; t < item.second; ++t)
                            if (is_name(l, t, "as"))
                                as = t;
                        auto const expressionEnd = as ? *as : item.second;
                        std::string parameters;
                        for (std::size_t n = 0u; n < names.size(); ++n)
                            parameters += (n == 0u ? " " : ", ") + names[n];
                        managers += ", lambda" + parameters + ": (" + source(l, item.first, expressionEnd) + ")";
                        if (as)
                        {
                            if (*as + 2u != item.second || tok(l, *as + 1u).kind != token::Name)
                                simple = false;
                            mask += '1';
                            names.push_back(source(l, *as + 1u, item.second));
                        }
                        else
                            mask += '0';
                    }
                    if (!simple)
                        continue;
                    std::string replacement = "__with_items('" + mask + "'" + managers + ")";
                    if (!names.empty())
                    {
                        replacement += " as (";
                        for (std::size_t n = 0u; n < names.size(); ++n)
                            replacement += (n == 0u ? "" : ", ") + names[n];
                        replacement += names.size() == 1u ? ",)" : ")";
                    }
                    replace(l, f + 1u, colon, replacement);
                }
            }
            // --- await, async for and async with
            // (the end of the primary expression (an atom and its trailers) at aBegin)
            std::size_t primary_end(std::size_t aLine, std::size_t aBegin) const
            {
                if (aBegin >= count(aLine))
                    return aBegin;
                if (is_name(aLine, aBegin, "await"))
                    return primary_end(aLine, aBegin + 1u);
                auto t = aBegin;
                auto const& k = tok(aLine, t);
                if (k.kind == token::Op)
                {
                    if (!opening(text(k)))
                        return aBegin;
                    t = matching(aLine, t) + 1u;
                }
                else if (k.kind == token::String)
                {
                    while (t < count(aLine) && tok(aLine, t).kind == token::String)
                        ++t;
                }
                else
                    ++t;
                // (trailers)
                for (;;)
                {
                    if (is_op(aLine, t, ".") && t + 1u < count(aLine) && tok(aLine, t + 1u).kind == token::Name)
                        t += 2u;
                    else if (is_op(aLine, t, "(") || is_op(aLine, t, "["))
                        t = matching(aLine, t) + 1u;
                    else
                        break;
                }
                return std::min(t, count(aLine));
            }
            void awaits()
            {
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                    for (std::size_t t = 0u; t < count(l); ++t)
                        if (is_name(l, t, "await"))
                        {
                            auto const end = primary_end(l, t + 1u);
                            if (end <= t + 1u)
                                continue;
                            iEdits.push_back({ tok(l, t).begin, tok(l, t).end, "__await(" });
                            iEdits.push_back({ tok(l, end - 1u).end, tok(l, end - 1u).end, ")" });
                        }
            }
            // (the iterable of the for (at aFor) of a comprehension: its tokens' end)
            std::size_t comprehension_iterable_end(std::size_t aLine, std::size_t aIn) const
            {
                int depth = 0;
                for (auto t = aIn + 1u; t < count(aLine); ++t)
                {
                    auto const& k = tok(aLine, t);
                    if (k.kind == token::Op)
                    {
                        if (opening(text(k)))
                            ++depth;
                        else if (closing(text(k)) && depth-- == 0)
                            return t;
                    }
                    else if (depth == 0 && (is_name(aLine, t, "for") || is_name(aLine, t, "if") || is_name(aLine, t, "async")))
                        return t;
                }
                return count(aLine);
            }
            void async_statements()
            {
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                {
                    for (std::size_t t = 0u; t + 1u < count(l); ++t)
                    {
                        if (!is_name(l, t, "async") || !(is_name(l, t + 1u, "for") || is_name(l, t + 1u, "with")))
                            continue;
                        bool const statement = t == 0u;
                        if (is_name(l, t + 1u, "for"))
                        {
                            // (async for x in it: for x in __async_iter(it))
                            std::size_t in = t + 2u;
                            int depth = 0;
                            for (; in < count(l); ++in)
                            {
                                if (tok(l, in).kind == token::Op)
                                {
                                    if (opening(text(l, in))) ++depth;
                                    else if (closing(text(l, in))) --depth;
                                }
                                else if (depth == 0 && is_name(l, in, "in"))
                                    break;
                            }
                            if (in + 1u >= count(l))
                                continue;
                            auto const end = statement ? header_colon(l, in + 1u) : comprehension_iterable_end(l, in);
                            if (end <= in + 1u)
                                continue;
                            iEdits.push_back({ tok(l, t).begin, tok(l, t + 1u).begin, "" });
                            replace(l, in + 1u, end, "__async_iter(" + source(l, in + 1u, end) + ")");
                            continue;
                        }
                        if (!statement)
                            continue;
                        // (async with A as a, B: with __async_with(A) as a, __async_with(B):)
                        auto const colon = header_colon(l, 2u);
                        auto begin = 2u;
                        auto end = colon;
                        if (is_op(l, begin, "(") && matching(l, begin) == end - 1u)
                        {
                            bool items = is_op(l, end - 2u, ",") || split(l, begin + 1u, end - 1u).size() > 1u;
                            for (auto x = begin + 1u; x < end - 1u; ++x)
                                if (is_name(l, x, "as"))
                                    items = true;
                            if (items)
                            {
                                ++begin;
                                --end;
                            }
                        }
                        iEdits.push_back({ tok(l, t).begin, tok(l, t + 1u).begin, "" });
                        for (auto const& item : split(l, begin, end))
                        {
                            auto expressionEnd = item.second;
                            for (auto x = item.first; x < item.second; ++x)
                                if (is_name(l, x, "as"))
                                {
                                    expressionEnd = x;
                                    break;
                                }
                            if (expressionEnd > item.first)
                                replace(l, item.first, expressionEnd, "__async_with(" + source(l, item.first, expressionEnd) + ")");
                        }
                    }
                }
            }
            // --- annotated assignments: "x: T = v" at a module's or a class's top level is "__annotations__['x'] = (T); x = v"
            // (the first of its scope creates __annotations__); in a function, "x = v" ("x: T": pass)
            void variable_annotations()
            {
                bool future = false;
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                    if (is_name(l, 0u, "from") && is_name(l, 1u, "__future__"))
                        for (std::size_t t = 2u; t < count(l); ++t)
                            if (is_name(l, t, "annotations"))
                                future = true;
                struct block { std::size_t indent; bool function; std::string className; };
                std::vector<block> blocks;
                std::set<std::size_t> started;      // (the scopes (by their header's line + 1; 0: the module) whose __annotations__ exists)
                std::vector<std::size_t> headers;   // (the lines of the blocks)
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                {
                    auto const indent = iLines[l].indent;
                    while (!blocks.empty() && blocks.back().indent >= indent)
                    {
                        blocks.pop_back();
                        headers.pop_back();
                    }
                    auto const f = first(l);
                    // (the statements of the line: those after a def's or a class's colon (its body, if it is on the line))
                    std::size_t start = 0u;
                    if (is_name(l, f, "def") || is_name(l, f, "class"))
                    {
                        blocks.push_back(block{ indent, is_name(l, f, "def"), is_name(l, f, "class") && f + 1u < count(l) ? std::string{ text(l, f + 1u) } : std::string{} });
                        headers.push_back(l);
                        int depth = 0;
                        std::optional<std::size_t> colon;
                        for (auto t = f; t < count(l) && !colon; ++t)
                            if (tok(l, t).kind == token::Op)
                            {
                                auto const op = text(l, t);
                                if (opening(op))
                                    ++depth;
                                else if (closing(op))
                                    --depth;
                                else if (depth == 0 && op == ":")
                                    colon = t;
                            }
                        if (!colon || *colon + 1u >= count(l))
                            continue;
                        start = *colon + 1u;
                    }
                    else if (count(l) < 3u || tok(l, 0u).kind != token::Name || keywords().contains(text(l, 0u)))
                        continue;
                    for (auto const& statement : split(l, start, count(l), ";"))
                    {
                        auto const b = statement.first;
                        auto const e = statement.second;
                        if (e < b + 3u || tok(l, b).kind != token::Name || keywords().contains(text(l, b)))
                            continue;
                        // (the colon after its target, and the = after its annotation)
                        std::optional<std::size_t> colon;
                        std::optional<std::size_t> equals;
                        int depth = 0;
                        for (auto t = b; t < e; ++t)
                        {
                            if (tok(l, t).kind == token::Name && text(l, t) == "lambda")
                                break;
                            if (tok(l, t).kind != token::Op)
                                continue;
                            auto const op = text(l, t);
                            if (opening(op))
                                ++depth;
                            else if (closing(op))
                                --depth;
                            else if (depth == 0 && op == ":" && !colon)
                                colon = t;
                            else if (depth == 0 && op == "=" && colon && !equals)
                                equals = t;
                            else if (depth == 0 && !colon && (op == "=" || (op.size() >= 2u && op.back() == '=' && op != "==" && op != "!=" && op != "<=" && op != ">=")))
                                break;
                        }
                        if (!colon || *colon == b || *colon + 1u >= (equals ? *equals : e))
                            continue;
                        // (its target: a name, or a name's attributes and subscripts)
                        {
                            auto t = b + 1u;
                            while (t < *colon)
                            {
                                if (is_op(l, t, ".") && t + 1u < *colon && tok(l, t + 1u).kind == token::Name)
                                    t += 2u;
                                else if (is_op(l, t, "[") || is_op(l, t, "("))
                                    t = matching(l, t) + 1u;
                                else
                                    break;
                            }
                            if (t != *colon || is_op(l, *colon - 1u, ")"))
                                continue;
                        }
                        bool const name = *colon == b + 1u;
                        auto const annotationEnd = equals ? *equals : e;
                        bool const function = !blocks.empty() && blocks.back().function;
                        std::string replacement;
                        if (!function && name)
                        {
                            auto const scope = headers.empty() ? 0u : headers.back() + 1u;
                            std::string key{ text(l, b) };
                            // (a class's private name: mangled)
                            if (!blocks.empty() && !blocks.back().className.empty() && key.starts_with("__") && !key.ends_with("__"))
                            {
                                auto const stripped = blocks.back().className.substr(blocks.back().className.find_first_not_of('_') == std::string::npos ? 0u : blocks.back().className.find_first_not_of('_'));
                                key = "_" + stripped + key;
                            }
                            auto annotation = source(l, *colon + 1u, annotationEnd);
                            if (future)
                            {
                                std::string quoted = "'";
                                for (auto c : annotation)
                                {
                                    if (c == '\'' || c == '\\')
                                        quoted += '\\';
                                    quoted += c;
                                }
                                annotation = quoted + "'";
                            }
                            if (started.insert(scope).second)
                                replacement = "__annotations__ = {}; ";
                            replacement += "__annotations__['" + key + "'] = (" + annotation + ")";
                            if (equals)
                                replacement += "; " + source(l, b, *colon) + " = " + source(l, *equals + 1u, e);
                        }
                        else if (equals)
                            replacement = source(l, b, *colon) + " = " + source(l, *equals + 1u, e);
                        else
                            replacement = "pass";
                        replace(l, b, e, replacement);
                    }
                }
            }
            // --- type statements and type parameters: "type X[T] = v" is "X = __type_alias('X', lambda T: (v), (__type_param('T', 0),))";
            // a def's or a class's type parameters are removed
            std::string type_parameter_list(std::size_t aLine, std::size_t aOpen, std::size_t aClose, std::string& aNames) const
            {
                std::string result;
                for (auto const& p : split(aLine, aOpen + 1u, aClose))
                {
                    auto t = p.first;
                    int kind = 0;
                    if (is_op(aLine, t, "*"))
                    {
                        kind = 1;
                        ++t;
                    }
                    else if (is_op(aLine, t, "**"))
                    {
                        kind = 2;
                        ++t;
                    }
                    if (t >= p.second || tok(aLine, t).kind != token::Name)
                        continue;
                    std::string const name{ text(aLine, t) };
                    aNames += (aNames.empty() ? "" : ", ") + (kind == 1 ? "*" + name : kind == 2 ? "**" + name : name);
                    result += "__type_param('" + name + "', " + std::to_string(kind) + "), ";
                }
                return "(" + result + ")";
            }
            void type_parameters()
            {
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                {
                    // (type X[...] = value)
                    if (is_name(l, 0u, "type") && count(l) >= 4u && tok(l, 1u).kind == token::Name && (is_op(l, 2u, "=") || is_op(l, 2u, "[")))
                    {
                        std::string names;
                        std::string parameters = "()";
                        auto equals = 2u;
                        if (is_op(l, 2u, "["))
                        {
                            auto const close = matching(l, 2u);
                            if (close >= count(l))
                                continue;
                            parameters = type_parameter_list(l, 2u, close, names);
                            equals = close + 1u;
                        }
                        if (!is_op(l, equals, "=") || equals + 1u >= count(l))
                            continue;
                        // (the parameters' kinds are the lambda's: a TypeVarTuple is an item (*Ts: Ts))
                        std::string lambdaParameters;
                        {
                            std::string plain;
                            std::size_t start = 0u;
                            while (start < names.size())
                            {
                                auto end = names.find(", ", start);
                                auto n = names.substr(start, end == std::string::npos ? std::string::npos : end - start);
                                n.erase(0u, n.find_first_not_of('*'));
                                plain += (plain.empty() ? "" : ", ") + n;
                                if (end == std::string::npos)
                                    break;
                                start = end + 2u;
                            }
                            lambdaParameters = plain;
                        }
                        std::string const name{ text(l, 1u) };
                        replace(l, 0u, count(l), name + " = __type_alias('" + name + "', lambda" + (lambdaParameters.empty() ? "" : " " + lambdaParameters) + ": (" +
                            source(l, equals + 1u, count(l)) + "), " + parameters + ")");
                        continue;
                    }
                    // (class C[...](bases): its type parameters are bound (where it is defined) and its last positional base is Generic 
                    // of them ("class C(bases, __generic_base(((T := __type_param('T', 0)), ...)))"); a def's are its closure's (see closures.cpp))
                    auto const f = first(l);
                    if (is_name(l, f, "class") && f + 2u < count(l) && tok(l, f + 1u).kind == token::Name && is_op(l, f + 2u, "["))
                    {
                        auto const close = matching(l, f + 2u);
                        if (close >= count(l))
                            continue;
                        std::string bindings;
                        for (auto const& p : split(l, f + 3u, close))
                        {
                            auto t = p.first;
                            int kind = 0;
                            if (is_op(l, t, "*"))
                                kind = 1, ++t;
                            else if (is_op(l, t, "**"))
                                kind = 2, ++t;
                            if (t >= p.second || tok(l, t).kind != token::Name)
                                continue;
                            std::string const name{ text(l, t) };
                            bindings += "(" + name + " := __type_param('" + name + "', " + std::to_string(kind) + ")), ";
                        }
                        std::string const base = "__generic_base((" + bindings + "))";
                        if (!is_op(l, close + 1u, "("))
                        {
                            replace(l, f + 2u, close + 1u, "(" + base + ")");
                            continue;
                        }
                        // (before its keyword arguments)
                        auto const closeParen = matching(l, close + 1u);
                        if (closeParen >= count(l))
                            continue;
                        auto insertAt = closeParen;
                        bool any = false;
                        for (auto const& a : split(l, close + 2u, closeParen))
                        {
                            if (a.first >= a.second)
                                continue;
                            if (is_op(l, a.first, "**") || (a.first + 1u < a.second && tok(l, a.first).kind == token::Name && is_op(l, a.first + 1u, "=")))
                            {
                                insertAt = a.first;
                                break;
                            }
                            any = true;
                        }
                        replace(l, f + 2u, close + 1u, std::string{});
                        if (insertAt == closeParen)
                            iEdits.push_back({ tok(l, closeParen).begin, tok(l, closeParen).begin, (any ? ", " : "") + base });
                        else
                            iEdits.push_back({ tok(l, insertAt).begin, tok(l, insertAt).begin, base + ", " });
                    }
                }
            }
            // --- try statements with except* clauses: "try:" is "with __exception_group() as _xN_:" (it holds the exception that its
            // body raises (as an exception group)), "except* E as e:" is "for e in _xN_.match((E), last):" (the part of the group 
            // that E matches, if any; the last raises what remains), "else:" is "if _xN_.none():"
            void except_star()
            {
                std::size_t groups = 0u;
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                {
                    if (!is_name(l, 0u, "try"))
                        continue;
                    auto const indent = iLines[l].indent;
                    std::vector<std::size_t> clauses;
                    bool star = false;
                    for (auto c = l + 1u; c < iLines.size() && iLines[c].indent >= indent; ++c)
                    {
                        if (iLines[c].indent != indent)
                            continue;
                        if (is_name(c, 0u, "except") || is_name(c, 0u, "else") || is_name(c, 0u, "finally"))
                        {
                            clauses.push_back(c);
                            if (is_name(c, 0u, "except") && is_op(c, 1u, "*"))
                                star = true;
                            continue;
                        }
                        break;
                    }
                    if (!star)
                        continue;
                    auto const group = "_x" + std::to_string(++groups) + "_";
                    auto const tryColon = header_colon(l, 1u);
                    if (tryColon >= count(l))
                        continue;
                    replace(l, 0u, tryColon, "with __exception_group() as " + group);
                    std::size_t lastStar = 0u;
                    for (auto c : clauses)
                        if (is_name(c, 0u, "except"))
                            lastStar = c;
                    for (auto c : clauses)
                    {
                        if (is_name(c, 0u, "except"))
                        {
                            auto const colon = header_colon(c, 2u);
                            if (colon >= count(c))
                                continue;
                            std::optional<std::size_t> as;
                            for (auto t = 2u; t < colon; ++t)
                                if (is_name(c, t, "as"))
                                    as = t;
                            auto const expressionEnd = as ? *as : colon;
                            std::string const target = as ? source(c, *as + 1u, colon) : group + "e";
                            replace(c, 0u, colon, "for " + target + " in " + group + ".match((" + source(c, 2u, expressionEnd) + "), " + (c == lastStar ? "True" : "False") + ")");
                        }
                        else if (is_name(c, 0u, "else"))
                            replace(c, 0u, 1u, "if " + group + ".none()");
                        else
                            replace(c, 0u, 1u, "if True");
                    }
                }
            }
            // --- match statements
            struct match_state
            {
                std::string descriptor;
                std::vector<std::string> values;    ///< (the expressions of its literals, values and classes)
                std::vector<std::string> names;     ///< (its captures, by slot)
                bool failed = false;
            };
            std::size_t value_of(match_state& aState, std::string aExpression) const
            {
                aState.values.push_back(std::move(aExpression));
                return aState.values.size() - 1u;
            }
            std::size_t slot_of(match_state& aState, std::string const& aName) const
            {
                auto const existing = std::find(aState.names.begin(), aState.names.end(), aName);
                if (existing != aState.names.end())
                    return static_cast<std::size_t>(existing - aState.names.begin());
                aState.names.push_back(aName);
                return aState.names.size() - 1u;
            }
            // (the bracket that closes the one at aOpen: the line's token count if none)
            std::size_t matching(std::size_t aLine, std::size_t aOpen) const
            {
                int depth = 0;
                for (auto t = aOpen; t < count(aLine); ++t)
                    if (tok(aLine, t).kind == token::Op)
                    {
                        if (opening(text(aLine, t))) ++depth;
                        else if (closing(text(aLine, t)) && --depth == 0)
                            return t;
                    }
                return count(aLine);
            }
            // (a pattern, or patterns separated by commas (a sequence))
            void open_pattern(match_state& aState, std::size_t aLine, std::size_t aBegin, std::size_t aEnd) const
            {
                auto const items = split(aLine, aBegin, aEnd);
                if (items.size() > 1u || (aEnd > aBegin && is_op(aLine, aEnd - 1u, ",")))
                    sequence_pattern(aState, aLine, items);
                else
                    as_pattern(aState, aLine, aBegin, aEnd);
            }
            void sequence_pattern(match_state& aState, std::size_t aLine, std::vector<std::pair<std::size_t, std::size_t>> const& aItems) const
            {
                aState.descriptor += "s" + std::to_string(aItems.size()) + ";";
                for (auto const& item : aItems)
                {
                    if (is_op(aLine, item.first, "*"))
                    {
                        if (item.second != item.first + 2u || tok(aLine, item.first + 1u).kind != token::Name)
                        {
                            aState.failed = true;
                            return;
                        }
                        auto const name = std::string{ text(aLine, item.first + 1u) };
                        aState.descriptor += name == "_" ? std::string{ "*_" } : "*" + std::to_string(slot_of(aState, name)) + ";";
                    }
                    else
                        as_pattern(aState, aLine, item.first, item.second);
                }
            }
            void as_pattern(match_state& aState, std::size_t aLine, std::size_t aBegin, std::size_t aEnd) const
            {
                if (aEnd >= aBegin + 3u && is_name(aLine, aEnd - 2u, "as") && tok(aLine, aEnd - 1u).kind == token::Name)
                {
                    aState.descriptor += "a" + std::to_string(slot_of(aState, std::string{ text(aLine, aEnd - 1u) })) + ";";
                    or_pattern(aState, aLine, aBegin, aEnd - 2u);
                }
                else
                    or_pattern(aState, aLine, aBegin, aEnd);
            }
            void or_pattern(match_state& aState, std::size_t aLine, std::size_t aBegin, std::size_t aEnd) const
            {
                auto const alternatives = split(aLine, aBegin, aEnd, "|");
                if (alternatives.size() > 1u)
                {
                    aState.descriptor += "|" + std::to_string(alternatives.size()) + ";";
                    for (auto const& a : alternatives)
                        closed_pattern(aState, aLine, a.first, a.second);
                }
                else
                    closed_pattern(aState, aLine, aBegin, aEnd);
            }
            void closed_pattern(match_state& aState, std::size_t aLine, std::size_t aBegin, std::size_t aEnd) const
            {
                if (aBegin >= aEnd)
                {
                    aState.failed = true;
                    return;
                }
                auto const& k = tok(aLine, aBegin);
                if (k.kind == token::Op && (text(k) == "(" || text(k) == "[") && matching(aLine, aBegin) == aEnd - 1u)
                {
                    auto const inner = split(aLine, aBegin + 1u, aEnd - 1u);
                    if (text(k) == "[" || inner.empty() || inner.size() > 1u || is_op(aLine, aEnd - 2u, ","))
                        sequence_pattern(aState, aLine, inner);
                    else
                        as_pattern(aState, aLine, aBegin + 1u, aEnd - 1u);
                    return;
                }
                if (k.kind == token::Op && text(k) == "{" && matching(aLine, aBegin) == aEnd - 1u)
                {
                    auto const items = split(aLine, aBegin + 1u, aEnd - 1u);
                    std::string rest = "-1";
                    std::string entries;
                    std::size_t keys = 0u;
                    for (auto const& item : items)
                    {
                        if (is_op(aLine, item.first, "**"))
                        {
                            if (item.second != item.first + 2u || tok(aLine, item.first + 1u).kind != token::Name)
                            {
                                aState.failed = true;
                                return;
                            }
                            rest = std::to_string(slot_of(aState, std::string{ text(aLine, item.first + 1u) }));
                            continue;
                        }
                        auto const parts = split(aLine, item.first, item.second, ":");
                        if (parts.size() != 2u)
                        {
                            aState.failed = true;
                            return;
                        }
                        ++keys;
                        auto const saved = std::move(aState.descriptor);
                        aState.descriptor = std::to_string(value_of(aState, source(aLine, parts[0].first, parts[0].second))) + ";";
                        as_pattern(aState, aLine, parts[1].first, parts[1].second);
                        entries += aState.descriptor;
                        aState.descriptor = saved;
                    }
                    aState.descriptor += "m" + std::to_string(keys) + "," + rest + ";" + entries;
                    return;
                }
                if (k.kind == token::Name && !keywords().contains(text(k)) && text(k) != "_")
                {
                    // (a capture, a value (a.b) or a class (a.b(...)))
                    auto d = aBegin;
                    while (d + 2u < aEnd && is_op(aLine, d + 1u, ".") && tok(aLine, d + 2u).kind == token::Name)
                        d += 2u;
                    if (d + 1u == aEnd)
                    {
                        if (d == aBegin)
                            aState.descriptor += "c" + std::to_string(slot_of(aState, std::string{ text(k) })) + ";";
                        else
                            aState.descriptor += "v" + std::to_string(value_of(aState, source(aLine, aBegin, aEnd))) + ";";
                        return;
                    }
                    if (is_op(aLine, d + 1u, "(") && matching(aLine, d + 1u) == aEnd - 1u)
                    {
                        auto const cls = value_of(aState, source(aLine, aBegin, d + 1u));
                        auto const arguments = split(aLine, d + 2u, aEnd - 1u);
                        std::string positional;
                        std::string keyword;
                        std::size_t positionals = 0u;
                        std::size_t keywordCount = 0u;
                        auto const saved = std::move(aState.descriptor);
                        for (auto const& a : arguments)
                        {
                            aState.descriptor.clear();
                            if (a.second > a.first + 2u && tok(aLine, a.first).kind == token::Name && is_op(aLine, a.first + 1u, "="))
                            {
                                as_pattern(aState, aLine, a.first + 2u, a.second);
                                keyword += std::string{ text(aLine, a.first) } + "=" + aState.descriptor;
                                ++keywordCount;
                            }
                            else
                            {
                                as_pattern(aState, aLine, a.first, a.second);
                                positional += aState.descriptor;
                                ++positionals;
                            }
                        }
                        aState.descriptor = saved + "k" + std::to_string(cls) + "," + std::to_string(positionals) + "," + 
                            std::to_string(keywordCount) + ";" + positional + keyword;
                        return;
                    }
                    aState.failed = true;
                    return;
                }
                if (k.kind == token::Name && text(k) == "_" && aEnd == aBegin + 1u)
                {
                    aState.descriptor += "_";
                    return;
                }
                if (k.kind == token::Name && (text(k) == "None" || text(k) == "True" || text(k) == "False") && aEnd == aBegin + 1u)
                {
                    aState.descriptor += "i" + std::to_string(value_of(aState, std::string{ text(k) })) + ";";
                    return;
                }
                // (a literal: numbers (e.g. -1, 1+2j) and strings)
                for (auto t = aBegin; t < aEnd; ++t)
                    if (tok(aLine, t).kind == token::Name || (tok(aLine, t).kind == token::Op && text(aLine, t) != "-" && text(aLine, t) != "+"))
                    {
                        aState.failed = true;
                        return;
                    }
                aState.descriptor += "v" + std::to_string(value_of(aState, source(aLine, aBegin, aEnd))) + ";";
            }
            // (a pattern of literals and values (and their alternatives) as comparisons of the subject; none if it is not one)
            std::optional<std::string> comparisons(std::size_t aLine, std::size_t aBegin, std::size_t aEnd, std::string const& aSubject) const
            {
                std::string result;
                auto const alternatives = split(aLine, aBegin, aEnd, "|");
                for (auto const& a : alternatives)
                {
                    if (a.first >= a.second)
                        return {};
                    std::string test;
                    if (a.second == a.first + 1u && tok(aLine, a.first).kind == token::Name)
                    {
                        auto const name = text(aLine, a.first);
                        if (name != "None" && name != "True" && name != "False")
                            return {};
                        test = aSubject + " is " + std::string{ name };
                    }
                    else
                    {
                        bool dotted = tok(aLine, a.first).kind == token::Name;
                        for (auto t = a.first; t < a.second; ++t)
                        {
                            auto const& k = tok(aLine, t);
                            if (dotted)
                            {
                                if ((t - a.first) % 2u == 0u ? k.kind != token::Name || keywords().contains(text(k)) : !is_op(aLine, t, "."))
                                    return {};
                            }
                            else if (k.kind == token::Name || (k.kind == token::Op && text(k) != "-" && text(k) != "+"))
                                return {};
                        }
                        if (dotted && (a.second - a.first) % 2u == 0u)
                            return {};
                        if (dotted && a.second == a.first + 1u)
                            return {};  // (a capture)
                        test = aSubject + " == (" + source(aLine, a.first, a.second) + ")";
                    }
                    result += (result.empty() ? "" : " or ") + test;
                }
                return alternatives.size() > 1u ? "(" + result + ")" : result;
            }
            void match_statements()
            {
                std::size_t matches = 0u;
                for (std::size_t l = 0u; l + 1u < iLines.size(); ++l)
                {
                    if (!is_name(l, 0u, "match") || count(l) < 3u || !is_op(l, count(l) - 1u, ":") ||
                        iLines[l + 1u].indent <= iLines[l].indent || !is_name(l + 1u, 0u, "case"))
                        continue;
                    if (header_colon(l, 1u) != count(l) - 1u)
                        continue;
                    auto const n = std::to_string(++matches);
                    auto const subject = "_m" + n + "_";
                    auto const captures = "_c" + n + "_";
                    replace(l, 0u, count(l) - 1u, "if ((" + subject + " := (" + source(l, 1u, count(l) - 1u) + ")),)");
                    auto const indent = iLines[l + 1u].indent;
                    bool firstCase = true;
                    for (auto c = l + 1u; c < iLines.size() && iLines[c].indent >= indent; ++c)
                    {
                        if (iLines[c].indent != indent || !is_name(c, 0u, "case"))
                            continue;
                        auto const colon = header_colon(c, 1u);
                        if (colon >= count(c))
                            continue;
                        // (its guard)
                        auto patternEnd = colon;
                        int depth = 0;
                        for (auto t = 1u; t < colon; ++t)
                        {
                            if (tok(c, t).kind == token::Op)
                            {
                                if (opening(text(c, t))) ++depth;
                                else if (closing(text(c, t))) --depth;
                            }
                            else if (depth == 0 && is_name(c, t, "if"))
                            {
                                patternEnd = t;
                                break;
                            }
                        }
                        std::string condition;
                        if (patternEnd == 2u && is_name(c, 1u, "_"))
                            condition = "True";
                        else if (patternEnd == 2u && tok(c, 1u).kind == token::Name && !keywords().contains(text(c, 1u)))
                            condition = "[(" + std::string{ text(c, 1u) } + " := " + subject + ")]";
                        else if (auto const direct = comparisons(c, 1u, patternEnd, subject))
                            condition = *direct;
                        else
                        {
                            match_state state;
                            open_pattern(state, c, 1u, patternEnd);
                            if (state.failed)
                                continue;
                            std::string call = "__match(" + subject + ", '" + state.descriptor + "'";
                            for (auto const& v : state.values)
                                call += ", " + v;
                            call += ")";
                            if (state.names.empty())
                                condition = call;
                            else
                            {
                                condition = "(" + captures + " := " + call + ") and [";
                                for (std::size_t i = 0u; i < state.names.size(); ++i)
                                    condition += (i == 0u ? "(" : ", (") + state.names[i] + " := " + captures + "[" + std::to_string(i) + "])";
                                condition += "]";
                            }
                        }
                        if (patternEnd < colon)
                            condition = "(" + condition + ") and (" + source(c, patternEnd + 1u, colon) + ")";
                        replace(c, 0u, colon, (firstCase ? "if " : "elif ") + condition);
                        firstCase = false;
                    }
                }
            }
        private:
            std::string const& iText;
            std::vector<logical_line> iLines;
            std::vector<edit> iEdits;
            std::size_t iFlags = 0u;
            int iPass = 2;  ///< (0: awaits; 1: async for and with; 2: the others)
        };
    }

    std::string rewrite_statements(std::string const& aText, bool& aChanged)
    {
        // (await, async for and async with; then the others (of the statements that they are))
        std::string const awaited = aText.find("await") != std::string::npos ? statement_rewriter{ aText, 0 }.rewrite(aChanged) : aText;
        std::string const synchronous = awaited.find("async") != std::string::npos ? statement_rewriter{ awaited, 1 }.rewrite(aChanged) : awaited;
        return statement_rewriter{ synchronous, 2 }.rewrite(aChanged);
    }

    namespace
    {
        class class_rewriter
        {
        public:
            class_rewriter(std::string const& aText) : iText{ aText }, iLines{ tokenize(aText) }
            {
            }
        public:
            std::string rewrite(bool& aChanged)
            {
                classes();
                if (iEdits.empty())
                    return iText;
                aChanged = true;
                // (from the end; at a position, a replacement before an insertion)
                std::sort(iEdits.begin(), iEdits.end(), [](edit const& a, edit const& b) { return a.begin > b.begin || (a.begin == b.begin && a.end > b.end); });
                std::string result = iText;
                for (auto const& e : iEdits)
                    result.replace(e.begin, e.end - e.begin, e.text);
                return result;
            }
        private:
            token const& tok(std::size_t aLine, std::size_t aToken) const { return iLines[aLine].tokens[aToken]; }
            std::string_view text(std::size_t aLine, std::size_t aToken) const
            {
                auto const& k = tok(aLine, aToken);
                return std::string_view{ iText }.substr(k.begin, k.end - k.begin);
            }
            std::size_t count(std::size_t aLine) const { return iLines[aLine].tokens.size(); }
            bool is_op(std::size_t aLine, std::size_t aToken, std::string_view aOp) const
            {
                return aToken < count(aLine) && tok(aLine, aToken).kind == token::Op && text(aLine, aToken) == aOp;
            }
            bool is_name(std::size_t aLine, std::size_t aToken, std::string_view aName) const
            {
                return aToken < count(aLine) && tok(aLine, aToken).kind == token::Name && text(aLine, aToken) == aName;
            }
            static bool opening(std::string_view aOp) { return aOp == "(" || aOp == "[" || aOp == "{"; }
            static bool closing(std::string_view aOp) { return aOp == ")" || aOp == "]" || aOp == "}"; }
            std::string source(std::size_t aLine, std::size_t aBegin, std::size_t aEnd) const
            {
                if (aBegin >= aEnd)
                    return {};
                return iText.substr(tok(aLine, aBegin).begin, tok(aLine, aEnd - 1u).end - tok(aLine, aBegin).begin);
            }
            // (a def's header: the index of "def" (0, or 1 after "async"), if it is one)
            std::optional<std::size_t> def_at(std::size_t aLine) const
            {
                auto const f = is_name(aLine, 0u, "async") ? 1u : 0u;
                if (is_name(aLine, f, "def") && f + 1u < count(aLine) && tok(aLine, f + 1u).kind == token::Name)
                    return f;
                return std::nullopt;
            }
            bool class_at(std::size_t aLine) const
            {
                return is_name(aLine, 0u, "class") && count(aLine) > 1u && tok(aLine, 1u).kind == token::Name;
            }
            // (the header (a line with a smaller indentation) that the line is in the body of)
            std::optional<std::size_t> enclosing(std::size_t aLine) const
            {
                auto const indent = iLines[aLine].indent;
                for (auto l = aLine; l > 0u;)
                {
                    --l;
                    if (iLines[l].indent < indent)
                        return l;
                }
                return std::nullopt;
            }
            // (the top-level colon of a header from aFrom)
            std::size_t header_colon(std::size_t aLine, std::size_t aFrom) const
            {
                int depth = 0;
                for (auto t = aFrom; t < count(aLine); ++t)
                {
                    if (tok(aLine, t).kind != token::Op)
                        continue;
                    auto const op = text(aLine, t);
                    if (opening(op))
                        ++depth;
                    else if (closing(op))
                        --depth;
                    else if (op == ":" && depth == 0)
                        return t;
                }
                return count(aLine);
            }
            void replace(std::size_t aLine, std::size_t aBegin, std::size_t aEnd, std::string aText)
            {
                iEdits.push_back({ tok(aLine, aBegin).begin, tok(aLine, aEnd - 1u).end, std::move(aText) });
            }
            // (names that are not mangled: the runtime's (e.g. __closure) and the compiler's)
            static bool unmangled(std::string_view aName)
            {
                static std::set<std::string_view> const sNames = { "__class", "__closure", "__cell", "__generator", "__yield", "__yield_from", 
                    "__bytes", "__with_items", "__build_class", "__fstring", "__match", "__await", "__async_iter", "__async_with",
                    "__coroutine", "__async_generator", "__type_alias", "__type_param", "__generic_base", "__generic_function", "__annotate", "__exception_group", "__ns_bind" };
                return sNames.contains(aName) || aName.starts_with("__neos");
            }
            void classes()
            {
                // (each line's innermost class (by its header's line))
                std::vector<std::optional<std::size_t>> classOf(iLines.size());
                std::vector<std::size_t> open;
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                {
                    while (!open.empty() && iLines[l].indent <= iLines[open.back()].indent)
                        open.pop_back();
                    if (!open.empty())
                        classOf[l] = open.back();
                    if (class_at(l))
                    {
                        auto const colon = header_colon(l, 2u);
                        // (an inline body is the class's own)
                        if (colon + 1u < count(l))
                            classOf[l] = l;
                        else
                            open.push_back(l);
                    }
                }
                std::size_t number = 0u;
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                {
                    if (!class_at(l))
                        continue;
                    auto const name = std::string{ text(l, 1u) };
                    auto const colon = header_colon(l, 2u);
                    if (colon >= count(l))
                        continue;
                    // (its bases and keywords)
                    std::string bases;
                    std::string keywords;
                    if (is_op(l, 2u, "("))
                    {
                        int depth = 0;
                        std::size_t start = 3u;
                        std::size_t close = colon;
                        for (auto t = 2u; t < colon; ++t)
                        {
                            if (tok(l, t).kind != token::Op)
                                continue;
                            auto const op = text(l, t);
                            if (opening(op))
                                ++depth;
                            else if (closing(op) && --depth == 0)
                            {
                                close = t;
                                break;
                            }
                            else if (op == "," && depth == 1)
                            {
                                auto const item = source(l, start, t);
                                bool const keyword = (t > start + 1u && tok(l, start).kind == token::Name && is_op(l, start + 1u, "=")) || is_op(l, start, "**");
                                if (!item.empty())
                                    (keyword ? keywords : bases) += item + ", ";
                                start = t + 1u;
                            }
                        }
                        if (start < close)
                        {
                            auto const item = source(l, start, close);
                            bool const keyword = (close > start + 1u && tok(l, start).kind == token::Name && is_op(l, start + 1u, "=")) || is_op(l, start, "**");
                            (keyword ? keywords : bases) += item + ", ";
                        }
                    }
                    auto const function = "_b" + std::to_string(++number) + "_" + name;
                    std::string header = "def " + function + "(__bases__=(" + bases + "), __kw__=" + (keywords.empty() ? std::string{ "None" } : "dict(" + keywords + ")") +
                        ", __class__=None):";
                    replace(l, 0u, colon + 1u, header);
                    iClassLines.insert(l);
                }
                // (super() in methods; private names)
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                {
                    for (std::size_t t = 0u; t + 2u < count(l); ++t)
                        if (is_name(l, t, "super") && is_op(l, t + 1u, "(") && is_op(l, t + 2u, ")") && (t == 0u || !is_op(l, t - 1u, ".")))
                        {
                            // (the innermost def: a method of a class)
                            std::optional<std::size_t> method;
                            if (def_at(l) && t > *def_at(l))
                                method = l;
                            else
                                for (auto e = enclosing(l); e; e = enclosing(*e))
                                    if (def_at(*e))
                                    {
                                        method = *e;
                                        break;
                                    }
                                    else if (class_at(*e))
                                        break;
                            if (!method)
                                continue;
                            auto const outer = enclosing(*method);
                            if (!outer || !class_at(*outer))
                                continue;
                            // (the method's first parameter)
                            auto const d = *def_at(*method);
                            // (after its type parameters, if any (def m[T](self, ...)))
                            auto open = d + 2u;
                            if (is_op(*method, open, "["))
                                while (open < count(*method) && !is_op(*method, open, "]"))
                                    ++open;
                            if (is_op(*method, open, "]"))
                                ++open;
                            auto p = open + 1u;
                            if (!is_op(*method, open, "(") || p >= count(*method) || tok(*method, p).kind != token::Name)
                                continue;
                            replace(l, t, t + 3u, "super(__class__, " + std::string{ text(*method, p) } + ")");
                        }
                    if (!classOf[l])
                        continue;
                    // (the class's private names: __x is _C__x)
                    std::string owner{ text(*classOf[l], 1u) };
                    auto const stripped = owner.find_first_not_of('_');
                    if (stripped == std::string::npos)
                        continue;
                    owner = owner.substr(stripped);
                    for (std::size_t t = iClassLines.contains(l) ? header_colon(l, 2u) + 1u : 0u; t < count(l); ++t)
                    {
                        if (tok(l, t).kind != token::Name)
                            continue;
                        auto const n = text(l, t);
                        if (n.size() > 2u && n.starts_with("__") && !n.ends_with("__") && !unmangled(n))
                            replace(l, t, t + 1u, "_" + owner + std::string{ n });
                    }
                }
            }
        private:
            std::string const& iText;
            std::vector<logical_line> iLines;
            std::vector<edit> iEdits;
            std::set<std::size_t> iClassLines;
        };
    }

    std::string rewrite_classes(std::string const& aText, bool& aChanged)
    {
        class_rewriter r{ aText };
        return r.rewrite(aChanged);
    }

    // (the assignments of names ("x = value", including those that defs have become) of the classes' bodies (the functions _aN_C, see 
    // closures.cpp): through their namespaces ("x = __ns_bind('x', value)"), whose __setitem__ (e.g. an enumeration's EnumDict's) 
    // can make the value that the name is then bound to)
    std::string rewrite_class_namespaces(std::string const& aText, bool& aChanged)
    {
        auto const lines = tokenize(aText);
        std::vector<edit> edits;
        auto const text = [&](std::size_t aLine, std::size_t aToken)
        {
            auto const& k = lines[aLine].tokens[aToken];
            return std::string_view{ aText }.substr(k.begin, k.end - k.begin);
        };
        for (std::size_t l = 0u; l < lines.size(); ++l)
        {
            auto const& header = lines[l];
            if (header.tokens.size() < 3u || text(l, 0u) != "def" || !text(l, 1u).starts_with("_a") || text(l, 2u) != "(" || 
                header.tokens.size() < 4u || text(l, 3u) != "__bases__")
                continue;
            auto const name = text(l, 1u);
            if (name.size() < 4u || !std::isdigit(static_cast<unsigned char>(name[2])))
                continue;
            std::optional<std::size_t> bodyIndent;
            for (auto b = l + 1u; b < lines.size() && lines[b].indent > header.indent; ++b)
            {
                auto const& line = lines[b];
                if (!bodyIndent)
                    bodyIndent = line.indent;
                if (line.indent != *bodyIndent || line.tokens.size() < 3u || line.tokens[0].kind != token::Name || text(b, 1u) != "=")
                    continue;
                auto const target = text(b, 0u);
                // (the compiler's names (cells, loop-else flags, ...) and private names: not the namespace's)
                if (target.starts_with("_k_") || (target.starts_with("__") && !target.ends_with("__")) || target == "__class__")
                    continue;
                if (target.size() > 3u && target[0] == '_' && std::islower(static_cast<unsigned char>(target[1])) && std::isdigit(static_cast<unsigned char>(target[2])))
                    continue;
                // (one target, and one statement on the line)
                bool simple = true;
                int depth = 0;
                for (auto t = 2u; t < line.tokens.size() && simple; ++t)
                    if (line.tokens[t].kind == token::Op)
                    {
                        auto const op = text(b, t);
                        if (op == "(" || op == "[" || op == "{")
                            ++depth;
                        else if (op == ")" || op == "]" || op == "}")
                            --depth;
                        else if (depth == 0 && (op == "=" || op == ";" || op == ":"))
                            simple = false;
                    }
                if (!simple)
                    continue;
                auto const begin = line.tokens[2u].begin;
                auto const end = line.tokens.back().end;
                edits.push_back({ end, end, ")" });
                edits.push_back({ begin, begin, "__ns_bind('" + std::string{ target } + "', " });
            }
        }
        if (edits.empty())
            return aText;
        aChanged = true;
        std::sort(edits.begin(), edits.end(), [](edit const& a, edit const& b) { return a.begin > b.begin; });
        std::string result = aText;
        for (auto const& e : edits)
            result.replace(e.begin, e.end - e.begin, e.text);
        return result;
    }

    namespace
    {
        class comprehension_rewriter
        {
        public:
            comprehension_rewriter(std::string const& aText, std::size_t& aNumber, bool aBuiltins) : 
                iText{ aText }, iLines{ tokenize(aText) }, iNumber{ aNumber }, iBuiltins{ aBuiltins }
            {
            }
        public:
            // (one comprehension (without comprehensions within it) rewritten: false if there are none to rewrite)
            bool rewrite(std::string& aResult)
            {
                // (the classes' bodies (_bN_ functions, see rewrite_classes): their lines)
                std::vector<bool> classBody(iLines.size(), false);
                {
                    std::vector<std::pair<std::size_t, bool>> open;     // (indent, class body)
                    for (std::size_t l = 0u; l < iLines.size(); ++l)
                    {
                        while (!open.empty() && iLines[l].indent <= open.back().first)
                            open.pop_back();
                        if (!open.empty())
                            classBody[l] = open.back().second;
                        auto const f = is_name(l, 0u, "async") ? 1u : 0u;
                        if (is_name(l, f, "def") || is_name(l, f, "class"))
                        {
                            bool const body = is_name(l, f, "def") && f + 1u < count(l) && text(l, f + 1u).starts_with("_b") && 
                                text(l, f + 1u).size() > 2u && std::isdigit(static_cast<unsigned char>(text(l, f + 1u)[2]));
                            // (a header's inline body is its own)
                            open.emplace_back(iLines[l].indent, body);
                        }
                    }
                }
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                {
                    for (std::size_t t = 0u; t < count(l); ++t)
                    {
                        if (tok(l, t).kind != token::Op || !opening(text(l, t)))
                            continue;
                        auto const close = matching(l, t);
                        if (close >= count(l))
                            continue;
                        // (a comprehension: a for at the bracket's level, and no comprehension to rewrite within it)
                        auto const clauses = top_level_for(l, t + 1u, close);
                        if (!clauses)
                            continue;
                        bool inner = false;
                        for (auto u = t + 1u; u < close && !inner; ++u)
                            if (tok(l, u).kind == token::Op && opening(text(l, u)))
                            {
                                auto const c = matching(l, u);
                                if (c < close)
                                    if (auto const innerClauses = top_level_for(l, u + 1u, c); innerClauses && pending(l, u, c, *innerClauses, classBody[l]))
                                        inner = true;
                            }
                        if (inner)
                            continue;
                        if (pending(l, t, close, *clauses, classBody[l]) && rewrite_one(l, t, close, *clauses, classBody[l], aResult))
                            return true;
                    }
                }
                return false;
            }
        private:
            token const& tok(std::size_t aLine, std::size_t aToken) const { return iLines[aLine].tokens[aToken]; }
            std::string_view text(std::size_t aLine, std::size_t aToken) const
            {
                auto const& k = tok(aLine, aToken);
                return std::string_view{ iText }.substr(k.begin, k.end - k.begin);
            }
            std::size_t count(std::size_t aLine) const { return iLines[aLine].tokens.size(); }
            bool is_op(std::size_t aLine, std::size_t aToken, std::string_view aOp) const
            {
                return aToken < count(aLine) && tok(aLine, aToken).kind == token::Op && text(aLine, aToken) == aOp;
            }
            bool is_name(std::size_t aLine, std::size_t aToken, std::string_view aName) const
            {
                return aToken < count(aLine) && tok(aLine, aToken).kind == token::Name && text(aLine, aToken) == aName;
            }
            static bool opening(std::string_view aOp) { return aOp == "(" || aOp == "[" || aOp == "{"; }
            static bool closing(std::string_view aOp) { return aOp == ")" || aOp == "]" || aOp == "}"; }
            bool is_class_body_header(std::size_t) const { return false; }
            // (a generator expression (its parenthesis at aOpen) that is the argument of a builtin that collects all its items 
            // before it uses them (sum(x for x in xs); a str literal's join): the builtin's name; none if it is not one, or the 
            // module can bind the name (anything but calls of it: e.g. "sum = f", "def f(sum):", "import *"), or builtins are not 
            // known (exec: its namespaces can bind them))
            std::optional<std::string_view> consuming_builtin(std::size_t aLine, std::size_t aOpen) const
            {
                static std::set<std::string_view> const sConsumers = { "sum", "min", "max", "list", "tuple", "set", "frozenset", "sorted", "dict" };
                auto const l = aLine;
                if (!iBuiltins || aOpen == 0u || tok(l, aOpen - 1u).kind != token::Name)
                    return std::nullopt;
                auto const name = text(l, aOpen - 1u);
                if (name == "join")
                {
                    if (aOpen >= 3u && is_op(l, aOpen - 2u, ".") && tok(l, aOpen - 3u).kind == token::String && 
                        (aOpen == 3u || !is_op(l, aOpen - 4u, ".")))
                    {
                        // (a str literal (not a bytes literal, nor an f-string): its quote after any prefix of r, u)
                        auto const literal = text(l, aOpen - 3u);
                        auto const quote = literal.find_first_of("'\"");
                        if (quote != std::string_view::npos && literal.substr(0u, quote).find_first_not_of("rRuU") == std::string_view::npos)
                            return name;
                    }
                    return std::nullopt;
                }
                if (!sConsumers.contains(name) || (aOpen >= 2u && is_op(l, aOpen - 2u, ".")))
                    return std::nullopt;
                if (!iUnbound.contains(std::string{ name }))
                {
                    bool unbound = true;
                    for (std::size_t m = 0u; m < iLines.size() && unbound; ++m)
                        for (std::size_t t = 0u; t < count(m) && unbound; ++t)
                        {
                            if (is_op(m, t, "*") && t > 0u && is_name(m, t - 1u, "import"))
                                unbound = false;
                            else if (is_name(m, t, name) && !(t > 0u && is_op(m, t - 1u, ".")) && !is_op(m, t + 1u, "("))
                                unbound = false;
                            else if (is_name(m, t, name) && t > 0u && (is_name(m, t - 1u, "def") || is_name(m, t - 1u, "class")))
                                unbound = false;
                        }
                    iUnbound[std::string{ name }] = unbound;
                }
                if (!iUnbound.at(std::string{ name }))
                    return std::nullopt;
                return name;
            }
            std::size_t matching(std::size_t aLine, std::size_t aToken) const
            {
                int depth = 0;
                for (auto t = aToken; t < count(aLine); ++t)
                    if (tok(aLine, t).kind == token::Op)
                    {
                        auto const op = text(aLine, t);
                        if (opening(op))
                            ++depth;
                        else if (closing(op) && --depth == 0)
                            return t;
                    }
                return count(aLine);
            }
            // (the positions of a comprehension's for and if clauses within [aBegin, aEnd) at its top level: none if it is not one)
            std::optional<std::vector<std::size_t>> top_level_for(std::size_t aLine, std::size_t aBegin, std::size_t aEnd) const
            {
                std::vector<std::size_t> result;
                int depth = 0;
                bool any = false;
                for (auto t = aBegin; t < aEnd; ++t)
                {
                    auto const& k = tok(aLine, t);
                    if (k.kind == token::Op)
                    {
                        auto const op = text(aLine, t);
                        if (opening(op))
                            ++depth;
                        else if (closing(op))
                            --depth;
                        continue;
                    }
                    if (depth != 0 || k.kind != token::Name)
                        continue;
                    auto const n = text(aLine, t);
                    if (n == "for")
                    {
                        // (async for: the clause starts at async)
                        result.push_back(t > aBegin && is_name(aLine, t - 1u, "async") ? t - 1u : t);
                        any = true;
                    }
                    else if (n == "if" && any)
                        result.push_back(t);
                }
                if (!any)
                    return std::nullopt;
                return result;
            }
            // (a comprehension that has not been rewritten)
            bool pending(std::size_t aLine, std::size_t aOpen, std::size_t aClose, std::vector<std::size_t> const& aClauses, bool aClassBody) const
            {
                auto const l = aLine;
                for (auto c : aClauses)
                {
                    auto t = c;
                    if (is_name(l, t, "async"))
                        ++t;
                    if (!is_name(l, t, "for"))
                        continue;
                    for (auto u = t + 1u; u < aClose && !is_name(l, u, "in"); ++u)
                        if (tok(l, u).kind == token::Name)
                        {
                            auto const n = text(l, u);
                            if (n.starts_with("_q") && n.size() > 2u && std::isdigit(static_cast<unsigned char>(n[2])))
                            {
                                // (renamed; in a class's body: a lambda's)
                                if (!aClassBody)
                                    return false;
                                return !(aOpen >= 4u && is_name(l, aOpen - 3u, "lambda") && is_name(l, aOpen - 2u, "_qit"));
                            }
                        }
                    break;
                }
                return true;
            }
            bool rewrite_one(std::size_t aLine, std::size_t aOpen, std::size_t aClose, std::vector<std::size_t> const& aClauses, bool aClassBody, std::string& aResult)
            {
                auto const l = aLine;
                // (the targets of its for clauses, and the extent of its first iterable)
                std::set<std::string> targets;
                std::optional<std::pair<std::size_t, std::size_t>> firstIterable;
                for (std::size_t c = 0u; c < aClauses.size(); ++c)
                {
                    auto t = aClauses[c];
                    if (is_name(l, t, "async"))
                        ++t;
                    if (!is_name(l, t, "for"))
                        continue;
                    auto const clauseEnd = c + 1u < aClauses.size() ? aClauses[c + 1u] : aClose;
                    std::size_t in = t + 1u;
                    int depth = 0;
                    for (; in < clauseEnd; ++in)
                    {
                        if (tok(l, in).kind == token::Op)
                        {
                            if (opening(text(l, in))) ++depth;
                            else if (closing(text(l, in))) --depth;
                        }
                        else if (depth == 0 && is_name(l, in, "in"))
                            break;
                    }
                    for (auto u = t + 1u; u < in; ++u)
                        if (tok(l, u).kind == token::Name && !keywords().contains(text(l, u)))
                            targets.insert(std::string{ text(l, u) });
                    if (!firstIterable)
                        firstIterable = std::make_pair(in + 1u, clauseEnd);
                }
                if (!firstIterable)
                    return false;
                // (names already renamed (a comprehension of a class's body that is not yet a lambda's): not again)
                bool renamed = false;
                for (auto const& target : targets)
                    if (target.starts_with("_q") && target.size() > 2u && std::isdigit(static_cast<unsigned char>(target[2])))
                        renamed = true;
                auto const prefix = "_q" + std::to_string(++iNumber) + "_";
                // (a generator expression whose items a builtin collects first anyway (see consuming_builtin): a list comprehension 
                // (list(...): just that; set(...): a set comprehension), not a generator)
                auto const consumer = is_op(l, aOpen, "(") ? consuming_builtin(l, aOpen) : std::nullopt;
                bool const generator = is_op(l, aOpen, "(") && !consumer;
                bool const wrapped = aClassBody || generator;   // (its first iterable is an argument: _qit)
                bool const bare = consumer && (*consumer == "list" || *consumer == "set");
                std::string result = iText.substr(0u, tok(l, bare ? aOpen - 1u : aOpen).begin);
                std::string comprehension;
                std::size_t clausesAt = 0u;
                auto position = tok(l, aOpen).begin;
                for (auto t = aOpen; t <= aClose; ++t)
                {
                    auto const& k = tok(l, t);
                    comprehension += iText.substr(position, k.begin - position);
                    position = k.end;
                    if (t == aClauses[0])
                        clausesAt = comprehension.size();
                    bool const inFirst = t >= firstIterable->first && t < firstIterable->second;
                    if (wrapped && t == firstIterable->first)
                    {
                        comprehension += "_qit";
                        continue;
                    }
                    if (wrapped && inFirst)
                        continue;
                    auto const n = std::string{ text(l, t) };
                    bool const rename = !renamed && k.kind == token::Name && !inFirst && targets.contains(n) && !(t > 0u && is_op(l, t - 1u, ".")) &&
                        !(is_op(l, t + 1u, "=") && t > 0u && (is_op(l, t - 1u, "(") || is_op(l, t - 1u, ",")));
                    comprehension += rename ? prefix + n : n;
                }
                auto const iterable = iText.substr(tok(l, firstIterable->first).begin, tok(l, firstIterable->second - 1u).end - tok(l, firstIterable->first).begin);
                if (consumer)
                {
                    comprehension.front() = *consumer == "set" ? '{' : '[';
                    comprehension.back() = *consumer == "set" ? '}' : ']';
                    if (!bare)
                        result += "(";
                }
                if (generator)
                {
                    // (a generator expression: a generator whose body is a lambda's comprehension (a dict of one item) that yields its 
                    // elements; its argument is an iterator of its first iterable (evaluated where the expression is))
                    std::string element = comprehension.substr(1u, clausesAt - 1u);
                    std::string clauses = comprehension.substr(clausesAt, comprehension.size() - clausesAt - 1u);
                    while (!element.empty() && (element.back() == ' ' || element.back() == '\t'))
                        element.pop_back();
                    result += "(__generator(lambda _qit: {0: __yield(" + element + ") " + clauses + "}, '<genexpr>', '<genexpr>', iter(" + iterable + ")))";
                }
                else if (aClassBody)
                    result += "(lambda _qit: " + comprehension + ")(" + iterable + ")";
                else
                    result += comprehension;
                if (consumer && !bare)
                    result += ")";
                result += iText.substr(tok(l, aClose).end);
                aResult = std::move(result);
                return true;
            }
        private:
            std::string const& iText;
            std::vector<logical_line> iLines;
            std::size_t& iNumber;
            bool const iBuiltins;
            mutable std::map<std::string, bool> iUnbound;  ///< (consuming_builtin's: the names that the module only calls)
        };
    }

    std::string rewrite_comprehensions(std::string const& aText, bool& aChanged, bool aBuiltins)
    {
        // (one comprehension at a time, innermost first)
        std::string text = aText;
        std::size_t number = 0u;
        for (;;)
        {
            std::string result;
            comprehension_rewriter r{ text, number, aBuiltins };
            if (!r.rewrite(result))
                break;
            text = std::move(result);
            aChanged = true;
        }
        return text;
    }
}
