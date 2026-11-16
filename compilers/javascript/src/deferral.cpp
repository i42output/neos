/*
  deferral.cpp

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

// The functions of a script that can be compiled when they are first called (see compiler::preparse): found by a pass over the
// script's source of its own (a recognizer: it makes no tree), as other engines find theirs (e.g. V8's preparser); a function is one
// if:
// - it is a function expression (not a generator or async function, an arrow function or a method) whose body is large;
// - it is closed: the names that it (and the functions within it) uses are bound within it, or are not bound by the code that
//   encloses it (they are properties of the global object, perhaps the script's own var bindings): its code does not depend on where
//   it is, so its source can be compiled on its own (and not a name that the script declares with let, const or class (the script's
//   own, perhaps its local), or as a function (which the script's code calls directly)); it is not within a with statement, a class
//   (whose private names it may use), or a function that calls eval directly (whose code may declare the names that it uses); one
//   that calls eval directly itself (or has a function that does) may use any name: it is closed if no scope that encloses it
//   declares a name at all (and the script none of those above);
// - the recognizer is certain that the function has no early error (the script's are reported when it is compiled: a deferred
//   function's would be only when it is called): it accepts the syntax whose early errors it finds (classes, patterns, regular
//   expression literals without the u and v flags among it), and what it is not certain of (e.g. a generator or an async function,
//   what is an error only in strict code) makes the functions that contain it not deferrable.
// The recognizer parses all of the script (to know where its functions are): source that it cannot parse (an error, or syntax that
// it does not know) has no deferrable functions. A program's functions are deferred only if those that can be amount to a large part
// of a large program (see LargeProgram): a deferred function that is called is compiled all the same (later, and with what compiling
// it on its own costs): deferral gains where much of a program is never called.

#include <neolib/neolib.hpp>
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <neos/bytecode/bytecode.hpp>
#include <neos/language/emit.hpp>
#include "compiler.hpp"

namespace neos::compilers::javascript
{
    namespace
    {
        constexpr std::size_t SmallestBody = 256u;                  ///< (a deferrable function's body is at least this large (bytes))
        constexpr std::size_t LargeProgram = 8u * 1024u * 1024u;    ///< (functions are deferred if their bodies amount to this (bytes))
        constexpr std::uint32_t DeepestNesting = 2000u;             ///< (the recognizer is recursive: source nested deeper is not parsed)

        struct unrecognized {};     ///< (source that the recognizer does not parse: nothing is deferred)

        // (the names that the recognizer knows: their identifiers are these (see recognizer::intern))
        enum name_id : std::uint32_t
        {
            // (reserved words)
            KBreak, KCase, KCatch, KClass, KConst, KContinue, KDebugger, KDefault, KDelete, KDo, KElse, KEnum, KExport, KExtends, KFalse, KFinally,
            KFor, KFunction, KIf, KImport, KIn, KInstanceof, KNew, KNull, KReturn, KSuper, KSwitch, KThis, KThrow, KTrue, KTry, KTypeof, KVar,
            KVoid, KWhile, KWith,
            ReservedEnd,
            // (reserved in strict code, or in some code: identifiers that make a function not deferrable)
            KImplements = ReservedEnd, KInterface, KLet, KPackage, KPrivate, KProtected, KPublic, KStatic, KYield, KAwait,
            RestrictedEnd,
            // (names with a meaning where they are)
            KAsync = RestrictedEnd, KOf, KGet, KSet, KTarget, KMeta, KEval, KArguments, KProto, KConstructor, KUsing, KPrototype, KPrivateConstructor,
            KnownEnd
        };
        constexpr std::string_view KnownNames[] =
        {
            "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do", "else", "enum", "export", "extends", "false",
            "finally", "for", "function", "if", "import", "in", "instanceof", "new", "null", "return", "super", "switch", "this", "throw", "true", "try",
            "typeof", "var", "void", "while", "with",
            "implements", "interface", "let", "package", "private", "protected", "public", "static", "yield", "await",
            "async", "of", "get", "set", "target", "meta", "eval", "arguments", "__proto__", "constructor", "using", "prototype", "#constructor"
        };

        enum class token : std::uint8_t { End, Name, PrivateName, Number, String, Template, Punctuator };

        enum punctuator : std::uint8_t
        {
            LBrace, RBrace, LParen, RParen, LBracket, RBracket, Semicolon, Comma, Dot, Ellipsis, Question, OptionalChain, Colon, Arrow,
            Assign, AssignOp, LogicalAssign,    // (=, the arithmetic ones, and &&= ||= ??=)
            Increment,                          // (++ and --)
            Not,                                // (! and ~)
            Additive,                           // (+ and -: unary too)
            Divide, DivideAssign,               // (/ and /=: or a regular expression literal's start)
            Exponent, Binary, LogicalAnd, LogicalOr, Coalesce
        };

        class recognizer
        {
        public:
            recognizer(std::string_view aSource) : s{ aSource }, n{ aSource.size() }
            {
                for (auto const name : KnownNames)
                    intern(name);
            }
        public:
            std::vector<deferrable_function> run()
            {
                // (a hashbang comment)
                if (n >= 2u && s[0] == '#' && s[1] == '!')
                    while (pos < n && !line_terminator_at(pos))
                        ++pos;
                iLineStart = true;
                open_function(false, false, false);
                functions.back().script = true;
                next();
                directives();
                while (tok != token::End)
                    statement(false);
                // (the script's: not those that use a name that the script declares with let, const or class, or as a function)
                if (pendingCovers != 0u)
                    throw unrecognized{};
                auto const scriptScope = static_cast<std::int32_t>(scopes.size() - 1u);
                // (one with a direct eval may use any of them)
                bool const declares = std::any_of(symbols.begin(), symbols.end(), [&](symbol const& aSymbol)
                    { return aSymbol.scope == scriptScope && (aSymbol.kind == declaration::Lexical || aSymbol.kind == declaration::Function); });
                std::vector<std::uint32_t> kept;
                std::vector<std::uint32_t> pending{ live.begin(), live.end() };
                std::reverse(pending.begin(), pending.end());
                while (!pending.empty())
                {
                    auto const c = pending.back();
                    pending.pop_back();
                    bool uses = candidates[c].anyName && declares;
                    for (auto f = candidates[c].freeBegin; f != candidates[c].freeEnd && !uses; ++f)
                    {
                        auto const& symbol = symbols[freeNames[f]];
                        uses = symbol.scope == scriptScope && (symbol.kind == declaration::Lexical || symbol.kind == declaration::Function);
                    }
                    if (!uses)
                        kept.push_back(c);
                    else
                        for (auto child = candidates[c].children.rbegin(); child != candidates[c].children.rend(); ++child)
                            pending.push_back(*child);
                }
                std::vector<deferrable_function> result;
                std::size_t total = 0u;
                for (auto const c : kept)
                {
                    result.push_back(deferrable_function{ candidates[c].body, candidates[c].end, candidates[c].strict });
                    total += candidates[c].end - candidates[c].body;
                }
                std::sort(result.begin(), result.end(), [](auto const& a, auto const& b) { return a.body < b.body; });
                if (total < LargeProgram)
                    result.clear();
                return result;
            }
        private:
            // --- the source's tokens

            std::string_view const s;
            std::size_t const n;
            std::size_t pos = 0u;
            token tok = token::End;
            punctuator punct = LBrace;
            std::uint32_t name = 0u;            ///< (a name's)
            std::size_t tokBegin = 0u;
            std::size_t tokEnd = 0u;
            std::size_t lastEnd = 0u;           ///< (the end of the token before)
            bool newline = false;               ///< (a line terminator precedes the token)
            bool iLineStart = true;             ///< (nothing but white space and comments precedes the token on its line)
            std::unordered_map<std::string_view, std::uint32_t> names;

            std::uint32_t intern(std::string_view aName)
            {
                auto const existing = names.find(aName);
                if (existing != names.end())
                    return existing->second;
                auto const id = static_cast<std::uint32_t>(names.size());
                names.emplace(aName, id);
                symbols.emplace_back();
                iNameLengths.push_back(aName.size());
                return id;
            }
            unsigned char at(std::size_t aAt) const { return aAt < n ? static_cast<unsigned char>(s[aAt]) : 0u; }
            bool line_terminator_at(std::size_t aAt) const
            {
                auto const c = at(aAt);
                return c == '\n' || c == '\r' || (c == 0xE2u && at(aAt + 1u) == 0x80u && (at(aAt + 2u) == 0xA8u || at(aAt + 2u) == 0xA9u));
            }
            // (white space that is not ASCII, at aAt: its length (0: not white space))
            std::size_t wide_space_at(std::size_t aAt) const
            {
                auto const c = at(aAt), d = at(aAt + 1u), e = at(aAt + 2u);
                if (c == 0xC2u && d == 0xA0u)
                    return 2u;
                if ((c == 0xEFu && d == 0xBBu && e == 0xBFu) || (c == 0xE1u && d == 0x9Au && e == 0x80u) || (c == 0xE2u && d == 0x80u && e >= 0x80u && e <= 0x8Au) ||
                    (c == 0xE2u && d == 0x80u && e == 0xAFu) || (c == 0xE2u && d == 0x81u && e == 0x9Fu) || (c == 0xE3u && d == 0x80u && e == 0x80u))
                    return 3u;
                return 0u;
            }
            static bool name_start(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$'; }
            static bool name_part(unsigned char c) { return name_start(c) || (c >= '0' && c <= '9'); }
            static bool digit(unsigned char c) { return c >= '0' && c <= '9'; }
            static bool hex(unsigned char c) { return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
            static std::int32_t hex_value(unsigned char c) { return digit(c) ? c - '0' : (c | 0x20) - 'a' + 10; }

            void skip_space()
            {
                for (;;)
                {
                    auto const c = at(pos);
                    if (c == ' ' || c == '\t' || c == '\v' || c == '\f')
                        ++pos;
                    else if (c == '\n' || c == '\r')
                    {
                        ++pos;
                        newline = iLineStart = true;
                    }
                    else if (c == '/' && at(pos + 1u) == '/')
                    {
                        pos += 2u;
                        while (pos < n && !line_terminator_at(pos))
                            ++pos;
                    }
                    else if (c == '/' && at(pos + 1u) == '*')
                    {
                        auto const end = s.find("*/", pos + 2u);
                        if (end == std::string_view::npos)
                            throw unrecognized{};
                        for (auto i = pos + 2u; i < end; ++i)
                            if (line_terminator_at(i))
                                newline = iLineStart = true;
                        pos = end + 2u;
                    }
                    else if (c >= 0x80u)
                    {
                        if (line_terminator_at(pos))
                        {
                            pos += 3u;
                            newline = iLineStart = true;
                        }
                        else if (auto const wide = wide_space_at(pos))
                            pos += wide;
                        else
                            return;
                    }
                    else
                        return;
                }
            }

            void next()
            {
                lastEnd = tokEnd;
                newline = false;
                skip_space();
                bool const lineStart = iLineStart;
                iLineStart = false;
                tokBegin = pos;
                if (pos >= n)
                {
                    tok = token::End;
                    tokEnd = pos;
                    return;
                }
                auto const c = at(pos);
                if (name_start(c))
                {
                    auto end = pos + 1u;
                    while (name_part(at(end)))
                        ++end;
                    if (at(end) == '\\' || at(end) >= 0x80u)
                        end = wide_name(end);
                    tok = token::Name;
                    name = intern(s.substr(pos, end - pos));
                    pos = end;
                }
                else if (c >= 0x80u)
                {
                    tok = token::Name;
                    auto const end = wide_name(pos);
                    name = intern(s.substr(pos, end - pos));
                    pos = end;
                }
                else if (digit(c) || (c == '.' && digit(at(pos + 1u))))
                    number();
                else if (c == '"' || c == '\'')
                    string(c);
                else if (c == '`')
                {
                    tok = token::Template;
                    ++pos;
                }
                else if (c == '#')
                {
                    if (!name_start(at(pos + 1u)))
                        throw unrecognized{};
                    auto end = pos + 2u;
                    while (name_part(at(end)))
                        ++end;
                    if (at(end) == '\\' || at(end) >= 0x80u)
                        throw unrecognized{};
                    tok = token::PrivateName;
                    name = intern(s.substr(pos, end - pos));
                    pos = end;
                }
                else
                    punctuator_token(c, lineStart);
                tokEnd = pos;
            }

            // (a name with characters that are not ASCII: they are taken to be a name's (it may not be one: a function that has one is
            // not deferrable); a name with an escape is not recognized (it is another spelling of a name))
            std::size_t wide_name(std::size_t aFrom)
            {
                auto end = aFrom;
                for (;;)
                {
                    auto const c = at(end);
                    if (name_part(c))
                        ++end;
                    else if (c >= 0x80u && !line_terminator_at(end) && wide_space_at(end) == 0u)
                    {
                        ++end;
                        while ((at(end) & 0xC0u) == 0x80u)
                            ++end;
                    }
                    else if (c == '\\')
                        throw unrecognized{};
                    else
                        break;
                }
                if (end == aFrom)
                    throw unrecognized{};
                uncertain();
                return end;
            }

            void number()
            {
                tok = token::Number;
                auto const c = at(pos);
                auto const d = at(pos + 1u);
                auto const digits = [&](int aRadix)
                {
                    // (and numeric separators: where they may be is not checked (a function that has one is not deferrable))
                    auto const from = pos;
                    for (;; ++pos)
                    {
                        auto const x = at(pos);
                        if (x == '_')
                            uncertain();
                        else if (!(aRadix == 16 ? hex(x) : aRadix == 10 ? digit(x) : aRadix == 8 ? (x >= '0' && x <= '7') : (x == '0' || x == '1')))
                            break;
                    }
                    return pos != from;
                };
                if (c == '0' && (d == 'x' || d == 'X' || d == 'o' || d == 'O' || d == 'b' || d == 'B'))
                {
                    pos += 2u;
                    if (!digits(d == 'x' || d == 'X' ? 16 : d == 'o' || d == 'O' ? 8 : 2))
                        throw unrecognized{};
                    if (at(pos) == 'n')
                        ++pos;
                }
                else
                {
                    // (a legacy octal literal, or a decimal one that begins with a zero: an error in strict code)
                    if (c == '0' && (digit(d) || d == '_'))
                        uncertain();
                    bool const integer = digits(10);
                    if (integer && at(pos) == 'n')
                        ++pos;
                    else
                    {
                        if (at(pos) == '.')
                        {
                            ++pos;
                            digits(10);
                        }
                        if (at(pos) == 'e' || at(pos) == 'E')
                        {
                            ++pos;
                            if (at(pos) == '+' || at(pos) == '-')
                                ++pos;
                            if (!digits(10))
                                throw unrecognized{};
                        }
                    }
                }
                if (name_part(at(pos)) || at(pos) == '\\' || at(pos) >= 0x80u)
                    throw unrecognized{};
            }

            // (an escape sequence's characters after its backslash (at pos): a string's; aTemplate: a template's)
            void escape(bool aTemplate)
            {
                auto const c = at(pos);
                if (pos >= n)
                    throw unrecognized{};
                if (c == 'x')
                {
                    if (!hex(at(pos + 1u)) || !hex(at(pos + 2u)))
                        throw unrecognized{};
                    pos += 3u;
                }
                else if (c == 'u')
                {
                    ++pos;
                    if (at(pos) == '{')
                    {
                        ++pos;
                        std::uint32_t value = 0u;
                        auto const from = pos;
                        while (hex(at(pos)))
                        {
                            auto const h = at(pos++);
                            value = value * 16u + (digit(h) ? h - '0' : (h | 0x20u) - 'a' + 10u);
                            if (value > 0x10FFFFu)
                                throw unrecognized{};
                        }
                        if (pos == from || at(pos) != '}')
                            throw unrecognized{};
                        ++pos;
                    }
                    else
                    {
                        for (int i = 0; i < 4; ++i)
                            if (!hex(at(pos + i)))
                                throw unrecognized{};
                        pos += 4u;
                    }
                }
                else if (digit(c))
                {
                    // (\0 alone is the NUL; else a legacy octal escape (or \8, \9): an error in strict code, and in a template)
                    if (c != '0' || digit(at(pos + 1u)) || aTemplate)
                        uncertain();
                    ++pos;
                }
                else if (c == '\r' && at(pos + 1u) == '\n')
                    pos += 2u;
                else if (c >= 0x80u)
                {
                    ++pos;
                    while ((at(pos) & 0xC0u) == 0x80u)
                        ++pos;
                }
                else
                    ++pos;
            }

            void string(unsigned char aQuote)
            {
                tok = token::String;
                ++pos;
                for (;;)
                {
                    if (pos >= n)
                        throw unrecognized{};
                    auto const c = at(pos);
                    if (c == aQuote)
                    {
                        ++pos;
                        return;
                    }
                    if (c == '\n' || c == '\r')
                        throw unrecognized{};
                    ++pos;
                    if (c == '\\')
                        escape(false);
                }
            }

            // (a template's characters from pos to its end (true) or to a substitution (false: its "${" is passed))
            bool template_characters()
            {
                for (;;)
                {
                    if (pos >= n)
                        throw unrecognized{};
                    auto const c = at(pos++);
                    if (c == '`')
                        return true;
                    if (c == '$' && at(pos) == '{')
                    {
                        ++pos;
                        return false;
                    }
                    if (c == '\\')
                    {
                        // (an escape that is not valid is an error unless the template is tagged (which is not known here: a function 
                        // that has one is not deferrable))
                        auto const e = at(pos);
                        if (pos >= n)
                            throw unrecognized{};
                        if (e == 'x' && hex(at(pos + 1u)) && hex(at(pos + 2u)))
                            pos += 3u;
                        else if (e == 'u' && hex(at(pos + 1u)) && hex(at(pos + 2u)) && hex(at(pos + 3u)) && hex(at(pos + 4u)))
                            pos += 5u;
                        else if (e == 'x' || e == 'u' || (digit(e) && (e != '0' || digit(at(pos + 1u)))))
                        {
                            uncertain();
                            ++pos;
                        }
                        else
                            ++pos;
                    }
                }
            }

            // (the token is a '/' or a "/=" where an expression begins: a regular expression literal)
            // (a regular expression literal's pattern (one without the u and v flags) is plainly a valid one: what may be an early
            // error, and what is not examined (named groups, modifiers), is not; the grammar is the one of the standard's Annex B, in
            // which escapes, and braces and a closing bracket that are not what they would be, are characters)
            bool plain_pattern(std::size_t aBegin, std::size_t aEnd) const
            {
                enum class group : std::uint8_t { Plain, Assertion };
                std::vector<group> groups;
                bool quantifiable = false;      // (what precedes may have a quantifier)
                // (decimal digits at aAt: 0: none; 1: their value is aValue; 2: too many to examine)
                auto const digits = [&](std::size_t& aAt, std::uint64_t& aValue)
                {
                    auto const from = aAt;
                    aValue = 0u;
                    for (; aAt < aEnd && digit(at(aAt)); ++aAt)
                    {
                        if (aAt - from >= 15u)
                            return 2;
                        aValue = aValue * 10u + (at(aAt) - '0');
                    }
                    return aAt != from ? 1 : 0;
                };
                auto i = aBegin;
                while (i < aEnd)
                {
                    auto const c = at(i);
                    switch (c)
                    {
                    case '\\':
                        if (at(i + 1u) >= 0x80u)
                            return false;
                        // (\b and \B are assertions; \k is a character unless the pattern has named groups (not examined))
                        quantifiable = at(i + 1u) != 'b' && at(i + 1u) != 'B';
                        i += 2u;
                        break;
                    case '(':
                        if (at(i + 1u) != '?')
                        {
                            groups.push_back(group::Plain);
                            ++i;
                        }
                        else if (at(i + 2u) == ':')
                        {
                            groups.push_back(group::Plain);
                            i += 3u;
                        }
                        else if (at(i + 2u) == '=' || at(i + 2u) == '!')
                        {
                            groups.push_back(group::Assertion);
                            i += 3u;
                        }
                        else if (at(i + 2u) == '<' && (at(i + 3u) == '=' || at(i + 3u) == '!'))
                        {
                            groups.push_back(group::Assertion);
                            i += 4u;
                        }
                        else
                            return false;
                        quantifiable = false;
                        break;
                    case ')':
                        if (groups.empty())
                            return false;
                        quantifiable = groups.back() == group::Plain;
                        groups.pop_back();
                        ++i;
                        break;
                    case '[':
                        {
                            // (a class: its ranges are in order (a range's end that is a class is a character: no order))
                            constexpr std::int32_t Unknown = -1, Class = -2;
                            ++i;
                            if (at(i) == '^')
                                ++i;
                            bool previous = false;
                            bool dash = false;
                            std::int32_t first = Unknown;
                            for (;;)
                            {
                                if (i >= aEnd)
                                    return false;
                                auto const a = at(i);
                                if (a == ']')
                                    break;
                                if (a == '-' && previous && !dash)
                                {
                                    dash = true;
                                    ++i;
                                    continue;
                                }
                                std::int32_t value = Unknown;
                                if (a == '\\')
                                {
                                    auto const e = at(i + 1u);
                                    if (e >= 0x80u || i + 1u >= aEnd)
                                        return false;
                                    i += 2u;
                                    switch (e)
                                    {
                                    case 'd': case 'D': case 's': case 'S': case 'w': case 'W':
                                        value = Class;
                                        break;
                                    case 't': value = 9; break;
                                    case 'n': value = 10; break;
                                    case 'v': value = 11; break;
                                    case 'f': value = 12; break;
                                    case 'r': value = 13; break;
                                    case 'b': value = 8; break;
                                    case 'c':
                                        if (name_part(at(i)) && at(i) != '$')
                                            value = at(i++) % 32;
                                        else
                                        {
                                            // (the backslash is a character; the c is the next)
                                            value = '\\';
                                            --i;
                                        }
                                        break;
                                    case 'x':
                                        if (hex(at(i)) && hex(at(i + 1u)))
                                        {
                                            value = hex_value(at(i)) * 16 + hex_value(at(i + 1u));
                                            i += 2u;
                                        }
                                        else
                                            value = 'x';
                                        break;
                                    case 'u':
                                        if (hex(at(i)) && hex(at(i + 1u)) && hex(at(i + 2u)) && hex(at(i + 3u)))
                                        {
                                            value = ((hex_value(at(i)) * 16 + hex_value(at(i + 1u))) * 16 + hex_value(at(i + 2u))) * 16 + hex_value(at(i + 3u));
                                            i += 4u;
                                        }
                                        else
                                            value = 'u';
                                        break;
                                    default:
                                        if (digit(e))
                                        {
                                            // (a legacy octal escape (of up to three digits, to 0377), or the digit 8 or 9)
                                            if (e >= '8')
                                                value = e;
                                            else
                                            {
                                                value = e - '0';
                                                if (at(i) >= '0' && at(i) <= '7')
                                                {
                                                    value = value * 8 + (at(i++) - '0');
                                                    if (e <= '3' && at(i) >= '0' && at(i) <= '7')
                                                        value = value * 8 + (at(i++) - '0');
                                                }
                                            }
                                        }
                                        else
                                            value = e;
                                        break;
                                    }
                                }
                                else if (a >= 0x80u)
                                {
                                    // (not examined as a range's end)
                                    ++i;
                                    while ((at(i) & 0xC0u) == 0x80u)
                                        ++i;
                                }
                                else
                                {
                                    value = a;
                                    ++i;
                                }
                                if (dash)
                                {
                                    if (first == Unknown || value == Unknown || (first != Class && value != Class && first > value))
                                        return false;
                                    dash = false;
                                    previous = false;
                                }
                                else
                                {
                                    first = value;
                                    previous = true;
                                }
                            }
                            ++i;
                            quantifiable = true;
                        }
                        break;
                    case '*':
                    case '+':
                    case '?':
                        if (!quantifiable)
                            return false;
                        ++i;
                        if (at(i) == '?')
                            ++i;
                        quantifiable = false;
                        break;
                    case '{':
                        {
                            // (a quantifier if it has one's form; else a character)
                            auto j = i + 1u;
                            std::uint64_t low = 0u, high = 0u;
                            bool unbounded = false;
                            bool quantifier = false;
                            auto const lowDigits = digits(j, low);
                            if (lowDigits == 2)
                                return false;
                            if (lowDigits == 1)
                            {
                                high = low;
                                bool formed = true;
                                if (at(j) == ',')
                                {
                                    ++j;
                                    if (at(j) == '}')
                                        unbounded = true;
                                    else
                                    {
                                        auto const highDigits = digits(j, high);
                                        if (highDigits == 2)
                                            return false;
                                        formed = highDigits == 1;
                                    }
                                }
                                quantifier = formed && at(j) == '}' && j < aEnd;
                            }
                            if (quantifier)
                            {
                                if (!quantifiable || (!unbounded && high < low))
                                    return false;
                                i = j + 1u;
                                if (at(i) == '?')
                                    ++i;
                                quantifiable = false;
                            }
                            else
                            {
                                ++i;
                                quantifiable = true;
                            }
                        }
                        break;
                    case '^':
                    case '$':
                    case '|':
                        ++i;
                        quantifiable = false;
                        break;
                    default:
                        ++i;
                        while ((at(i) & 0xC0u) == 0x80u)
                            ++i;
                        quantifiable = true;
                        break;
                    }
                }
                return groups.empty() && i == aEnd;
            }

            void regular_expression()
            {
                pos = tokBegin + 1u;
                bool inClass = false;
                for (;;)
                {
                    if (pos >= n || line_terminator_at(pos))
                        throw unrecognized{};
                    auto const c = at(pos++);
                    if (c == '\\')
                    {
                        if (pos >= n || line_terminator_at(pos))
                            throw unrecognized{};
                        ++pos;
                    }
                    else if (c == '[')
                        inClass = true;
                    else if (c == ']')
                        inClass = false;
                    else if (c == '/' && !inClass)
                        break;
                }
                auto const patternEnd = pos - 1u;
                // (its flags: as the compiler's early errors check them)
                std::string_view const valid{ "dgimsuvy" };
                std::uint32_t seen = 0u;
                while (name_part(at(pos)))
                {
                    auto const flag = valid.find(static_cast<char>(at(pos)));
                    if (flag == std::string_view::npos || (seen & (1u << flag)) != 0u)
                        uncertain();
                    else
                        seen |= 1u << flag;
                    ++pos;
                }
                // (its pattern: a pattern with the u or v flag is not examined)
                if ((seen & ((1u << valid.find('u')) | (1u << valid.find('v')))) != 0u || !plain_pattern(tokBegin + 1u, patternEnd))
                    uncertain();
                if (at(pos) == '\\' || at(pos) >= 0x80u)
                    throw unrecognized{};
                tok = token::Number;    // (an operand)
                tokEnd = pos;
            }

            void punctuator_token(unsigned char c, bool aLineStart)
            {
                tok = token::Punctuator;
                auto const d = at(pos + 1u), e = at(pos + 2u);
                auto const is = [&](punctuator aPunctuator, std::size_t aLength) { punct = aPunctuator; pos += aLength; };
                switch (c)
                {
                case '{': is(LBrace, 1u); break;
                case '}': is(RBrace, 1u); break;
                case '(': is(LParen, 1u); break;
                case ')': is(RParen, 1u); break;
                case '[': is(LBracket, 1u); break;
                case ']': is(RBracket, 1u); break;
                case ';': is(Semicolon, 1u); break;
                case ',': is(Comma, 1u); break;
                case ':': is(Colon, 1u); break;
                case '~': is(Not, 1u); break;
                case '.':
                    if (d == '.' && e == '.')
                        is(Ellipsis, 3u);
                    else
                        is(Dot, 1u);
                    break;
                case '?':
                    if (d == '?')
                        is(e == '=' ? LogicalAssign : Coalesce, e == '=' ? 3u : 2u);
                    else if (d == '.' && !digit(e))
                        is(OptionalChain, 2u);
                    else
                        is(Question, 1u);
                    break;
                case '=':
                    if (d == '>')
                        is(Arrow, 2u);
                    else if (d == '=')
                        is(Binary, e == '=' ? 3u : 2u);
                    else
                        is(Assign, 1u);
                    break;
                case '!':
                    if (d == '=')
                        is(Binary, e == '=' ? 3u : 2u);
                    else
                        is(Not, 1u);
                    break;
                case '+':
                case '-':
                    if (d == c)
                    {
                        // (an HTML-like comment's end (Annex B): not recognized)
                        if (c == '-' && e == '>' && aLineStart)
                            throw unrecognized{};
                        is(Increment, 2u);
                    }
                    else if (d == '=')
                        is(AssignOp, 2u);
                    else
                        is(Additive, 1u);
                    break;
                case '*':
                    if (d == '*')
                        is(e == '=' ? AssignOp : Exponent, e == '=' ? 3u : 2u);
                    else if (d == '=')
                        is(AssignOp, 2u);
                    else
                    {
                        is(Binary, 1u);
                        iStar = true;
                        tokEnd = pos;
                        return;
                    }
                    break;
                case '/':
                    if (d == '=')
                        is(DivideAssign, 2u);
                    else
                        is(Divide, 1u);
                    break;
                case '%':
                case '^':
                    if (d == '=')
                        is(AssignOp, 2u);
                    else
                        is(Binary, 1u);
                    break;
                case '&':
                case '|':
                    if (d == c)
                        is(e == '=' ? LogicalAssign : c == '&' ? LogicalAnd : LogicalOr, e == '=' ? 3u : 2u);
                    else if (d == '=')
                        is(AssignOp, 2u);
                    else
                        is(Binary, 1u);
                    break;
                case '<':
                    // (an HTML-like comment (Annex B): not recognized)
                    if (d == '!' && e == '-' && at(pos + 3u) == '-')
                        throw unrecognized{};
                    if (d == '<')
                        is(e == '=' ? AssignOp : Binary, e == '=' ? 3u : 2u);
                    else
                        is(Binary, d == '=' ? 2u : 1u);
                    break;
                case '>':
                    if (d == '>' && e == '>')
                        is(at(pos + 3u) == '=' ? AssignOp : Binary, at(pos + 3u) == '=' ? 4u : 3u);
                    else if (d == '>')
                        is(e == '=' ? AssignOp : Binary, e == '=' ? 3u : 2u);
                    else
                        is(Binary, d == '=' ? 2u : 1u);
                    break;
                default:
                    throw unrecognized{};
                }
                iStar = false;
            }
            bool iStar = false;     ///< (the token is a '*')

            bool is(punctuator aPunctuator) const { return tok == token::Punctuator && punct == aPunctuator; }
            bool is_name(std::uint32_t aName) const { return tok == token::Name && name == aName; }
            bool star() const { return tok == token::Punctuator && punct == Binary && iStar; }
            bool eat(punctuator aPunctuator)
            {
                if (!is(aPunctuator))
                    return false;
                next();
                return true;
            }
            void expect(punctuator aPunctuator)
            {
                if (!eat(aPunctuator))
                    throw unrecognized{};
            }
            bool eat_name(std::uint32_t aName)
            {
                if (!is_name(aName))
                    return false;
                next();
                return true;
            }
            // (the token after the token (which stays the token))
            // (the current token, to return to (see parenthesized))
            struct token_state
            {
                std::size_t pos, tokBegin, tokEnd, lastEnd;
                token tok;
                punctuator punct;
                std::uint32_t name;
                bool newline, lineStart, star;
            };
            token_state current() const { return { pos, tokBegin, tokEnd, lastEnd, tok, punct, name, newline, iLineStart, iStar }; }
            void restore(token_state const& aState)
            {
                pos = aState.pos; tokBegin = aState.tokBegin; tokEnd = aState.tokEnd; lastEnd = aState.lastEnd; tok = aState.tok; punct = aState.punct;
                name = aState.name; newline = aState.newline; iLineStart = aState.lineStart; iStar = aState.star;
            }

            struct peeked { token tok; punctuator punct; std::uint32_t name; bool newline; bool star; };
            peeked peek()
            {
                auto const savedPos = pos, savedBegin = tokBegin, savedEnd = tokEnd, savedLast = lastEnd;
                auto const savedTok = tok;
                auto const savedPunct = punct;
                auto const savedName = name;
                bool const savedNewline = newline, savedLineStart = iLineStart, savedStar = iStar;
                auto const savedUncertain = functions.back().uncertain;
                next();
                peeked const result{ tok, punct, name, newline, star() };
                pos = savedPos; tokBegin = savedBegin; tokEnd = savedEnd; lastEnd = savedLast; tok = savedTok; punct = savedPunct; name = savedName;
                newline = savedNewline; iLineStart = savedLineStart; iStar = savedStar;
                functions.back().uncertain = savedUncertain;
                return result;
            }
            // (a statement's end: a semicolon, or one inserted (before a '}', at the end, or after a line terminator))
            void semicolon()
            {
                if (eat(Semicolon) || is(RBrace) || tok == token::End || newline)
                    return;
                throw unrecognized{};
            }

            // --- scopes: the names that they declare, the names used that they do not, and the functions that can be deferred

            enum class declaration : std::uint8_t { None, Var, Function, Lexical };
            struct symbol
            {
                std::int32_t scope = -1;    ///< (the innermost scope (of those open) that declares the name: its index; -1: none)
                declaration kind = declaration::None;
                std::uint64_t time = 0u;    ///< (a var: when it was last declared (see declare_lexical))
            };
            struct undo { std::uint32_t name; symbol previous; };
            struct scope
            {
                bool function = false;
                bool opaque = false;        ///< (what it declares may not be known (or is found when the code runs): no function within it is deferrable)
                std::size_t undoBegin = 0u;
                std::size_t usesBegin = 0u;
                std::size_t liveBegin = 0u;
                std::uint64_t opened = 0u;
                bool declares = false;
            };
            struct label { std::uint32_t name; bool loop; };
            struct function
            {
                bool script = false;
                bool arrow = false;
                bool generator = false;
                bool async = false;
                bool uncertain = false;     ///< (it (or a function within it) may have an early error, or is not deferrable for what it has)
                bool anyName = false;       ///< (a direct eval is in it (or in a function within it): its code may use any name)
                bool superProperty = false; ///< (super.x may be in it (a method, a field's initializer, a static block))
                bool superCall = false;     ///< (super() may be in it (a derived class's constructor))
                bool noArguments = false;   ///< (arguments may not be used in it (a field's initializer, a static block))
                bool staticBlock = false;   ///< (a class's static block: no return statement)
                bool nonSimple = false;     ///< (its parameters are not identifiers alone)
                std::size_t parameterCount = 0u;
                bool restParameter = false;
                std::size_t parameterUses = 0u;     ///< (the end, in uses, of what its parameters' code uses)
                std::size_t parameterUndo = 0u;     ///< (the end, in functionUndo, of what its parameters declare)
                bool strict = false;        ///< (its body has a directive that makes it strict)
                std::uint32_t ownName = ~std::uint32_t{};  ///< (a function expression's own name)
                std::int32_t scope = 0;
                std::size_t undoBegin = 0u;
                std::vector<label> labels;
                std::uint32_t loops = 0u;
                std::uint32_t breakables = 0u;
            };
            struct candidate
            {
                std::size_t body;
                std::size_t end;
                bool strict;
                bool anyName;           ///< (it may use any name (a direct eval is in it))
                std::size_t freeBegin;
                std::size_t freeEnd;
                std::vector<std::uint32_t> children;    ///< (those within it that would be deferrable if it is not)
            };
            struct private_name { std::uint32_t name; int accessor; bool isStatic; };   ///< (accessor: 1: a getter; 2: a setter)
            struct class_body
            {
                std::vector<private_name> declared;     ///< (its private names)
                std::vector<std::uint32_t> used;        ///< (the private names used in it (found to be its own, or not, when it closes))
            };
            std::vector<class_body> classes;    ///< (those open (their bodies))
            std::size_t pendingCovers = 0u;     ///< (shorthand properties with initializers in object literals not yet known to be patterns)
            std::uint32_t reparsing = 0u;       ///< (arrow functions' parameters being parsed again, nested)
            std::vector<symbol> symbols;        ///< (by name)
            std::vector<scope> scopes;          ///< (those open)
            std::vector<function> functions;    ///< (those open)
            std::vector<undo> blockUndo;        ///< (what the open scopes declared (but functions' vars): undone when they close)
            std::vector<undo> functionUndo;     ///< (the open functions' vars)
            std::vector<std::uint32_t> uses;    ///< (the names that the open scopes use and have not been found to declare)
            std::vector<std::uint32_t> live;    ///< (the open scopes' candidates)
            std::vector<candidate> candidates;
            std::vector<std::uint32_t> freeNames;
            std::uint64_t now = 0u;
            std::uint32_t depth = 0u;

            void uncertain() { if (!functions.empty()) functions.back().uncertain = true; }

            struct nesting
            {
                recognizer& r;
                nesting(recognizer& aRecognizer) : r{ aRecognizer } { if (++r.depth > DeepestNesting) throw unrecognized{}; }
                ~nesting() { --r.depth; }
            };

            void open_scope(bool aFunction = false)
            {
                scope sc;
                sc.function = aFunction;
                sc.undoBegin = blockUndo.size();
                sc.usesBegin = uses.size();
                sc.liveBegin = live.size();
                sc.opened = ++now;
                scopes.push_back(sc);
            }

            // (the scope closes: the names used in it that it declares are its own; a candidate in it that uses one is not one (those
            // within that one may be))
            void close_scope()
            {
                auto const index = static_cast<std::int32_t>(scopes.size() - 1u);
                auto const sc = scopes.back();
                if (sc.opaque)
                    live.resize(sc.liveBegin);
                if (sc.declares)
                {
                    auto const mine = [&](std::uint32_t aName) { return symbols[aName].scope == index; };
                    uses.erase(std::remove_if(uses.begin() + sc.usesBegin, uses.end(), mine), uses.end());
                    if (live.size() > sc.liveBegin)
                    {
                        std::vector<std::uint32_t> pending{ live.begin() + sc.liveBegin, live.end() };
                        std::reverse(pending.begin(), pending.end());
                        live.resize(sc.liveBegin);
                        while (!pending.empty())
                        {
                            auto const c = pending.back();
                            pending.pop_back();
                            if (!candidates[c].anyName && std::none_of(freeNames.begin() + candidates[c].freeBegin, freeNames.begin() + candidates[c].freeEnd, mine))
                                live.push_back(c);
                            else
                                for (auto child = candidates[c].children.rbegin(); child != candidates[c].children.rend(); ++child)
                                    pending.push_back(*child);
                        }
                    }
                }
                while (blockUndo.size() > sc.undoBegin)
                {
                    symbols[blockUndo.back().name] = blockUndo.back().previous;
                    blockUndo.pop_back();
                }
                scopes.pop_back();
            }

            void open_function(bool aArrow, bool aGenerator, bool aAsync)
            {
                open_scope(true);
                function f;
                f.arrow = aArrow;
                f.generator = aGenerator;
                f.async = aAsync;
                f.scope = static_cast<std::int32_t>(scopes.size() - 1u);
                f.undoBegin = functionUndo.size();
                functions.push_back(std::move(f));
            }

            // (the function closes; aBody, aEnd: its body's braces, if it is a function expression that may be deferred)
            void close_function(std::size_t aBody = 0u, std::size_t aEnd = 0u, bool aDeferrable = false)
            {
                auto const f = std::move(functions.back());
                auto const sc = scopes.back();
                // (its arguments object)
                if (!f.arrow && !f.script)
                    declare(KArguments, declaration::Var);
                scopes.back().declares = true;
                // (the names that it uses and does not declare (once each), and its candidates)
                auto const index = static_cast<std::int32_t>(scopes.size() - 1u);
                auto const mine = [&](std::uint32_t aName) { return symbols[aName].scope == index; };
                // (a function expression that uses its own name is not deferrable: the name is bound to the function object that its
                // source's own evaluation makes (not to the program's function object for it))
                // (so is one with a direct eval: its code may use the name)
                bool const usesOwnName = f.ownName != ~std::uint32_t{} && (f.anyName || std::find(uses.begin() + sc.usesBegin, uses.end(), f.ownName) != uses.end());
                // (arguments in a field's initializer or a static block (or in an arrow function in one) is an error)
                bool const badArguments = f.noArguments && std::find(uses.begin() + sc.usesBegin, uses.end(), static_cast<std::uint32_t>(KArguments)) != uses.end();
                // (a function with parameters that are not simple: what its parameters' code uses is not what its body declares (the
                // names that its parameters declare, its own name and arguments are its own there): those are names of the code that
                // encloses it)
                std::vector<std::uint32_t> outer;
                if (f.nonSimple)
                    for (auto u = sc.usesBegin; u < f.parameterUses && u < uses.size(); ++u)
                    {
                        auto const used = uses[u];
                        if (!mine(used) || (used == KArguments && !f.arrow))
                            continue;
                        bool parameter = false;
                        for (auto d = f.undoBegin; d < f.parameterUndo && !parameter; ++d)
                            parameter = functionUndo[d].name == used;
                        if (!parameter)
                            outer.push_back(used);
                    }
                uses.erase(std::remove_if(uses.begin() + sc.usesBegin, uses.end(), mine), uses.end());
                uses.insert(uses.end(), outer.begin(), outer.end());
                std::sort(uses.begin() + sc.usesBegin, uses.end());
                uses.erase(std::unique(uses.begin() + sc.usesBegin, uses.end()), uses.end());
                std::vector<std::uint32_t> const free{ uses.begin() + sc.usesBegin, uses.end() };
                // (a function's own scope is opaque if a direct eval is in it: what is within it is not deferrable; it may be)
                bool const deferrable = aDeferrable && !usesOwnName && !f.uncertain && !f.arrow && !f.generator && !f.async && aEnd - aBody >= SmallestBody;
                close_scope();      // (its candidates that use a name of its own are not ones; its names used that are its own were removed)
                while (functionUndo.size() > f.undoBegin)
                {
                    symbols[functionUndo.back().name] = functionUndo.back().previous;
                    functionUndo.pop_back();
                }
                functions.pop_back();
                // (the names that it uses are used where it is)
                uses.resize(sc.usesBegin);
                uses.insert(uses.end(), free.begin(), free.end());
                if (f.uncertain || badArguments)
                    uncertain();
                if (f.anyName && !functions.empty())
                    functions.back().anyName = true;
                if (deferrable)
                {
                    candidate c;
                    c.body = aBody;
                    c.end = aEnd;
                    c.strict = f.strict;
                    c.anyName = f.anyName;
                    c.freeBegin = freeNames.size();
                    freeNames.insert(freeNames.end(), free.begin(), free.end());
                    c.freeEnd = freeNames.size();
                    c.children.assign(live.begin() + sc.liveBegin, live.end());
                    live.resize(sc.liveBegin);
                    candidates.push_back(std::move(c));
                    live.push_back(static_cast<std::uint32_t>(candidates.size() - 1u));
                }
            }

            void use(std::uint32_t aName)
            {
                if (aName < RestrictedEnd && aName >= ReservedEnd)
                    uncertain();    // (e.g. let, yield as identifiers: not in strict code)
                uses.push_back(aName);
            }

            void declare(std::uint32_t aName, declaration aKind)
            {
                auto& f = functions.back();
                auto& symbol = symbols[aName];
                if (aKind == declaration::Lexical)
                {
                    // (a lexical declaration: not of a name that its scope declares, or a var declared within its scope)
                    auto const index = static_cast<std::int32_t>(scopes.size() - 1u);
                    if (symbol.scope == index || (symbol.scope == f.scope && symbol.time > scopes.back().opened))
                        uncertain();
                    if (symbol.scope != index)
                    {
                        blockUndo.push_back(undo{ aName, symbol });
                        symbol = { index, declaration::Lexical, now };
                    }
                    scopes.back().declares = true;
                }
                else
                {
                    // (a var (or a function's top-level function declaration, or its parameter): not of a name that a scope between
                    // it and its function declares lexically)
                    if (symbol.scope > f.scope)
                    {
                        // (an error, unless the scope's name is a catch clause's parameter (Annex B): the var is declared (beneath
                        // the scopes' declarations of the name))
                        uncertain();
                        for (auto u = scopes[static_cast<std::size_t>(f.scope)].undoBegin; u < blockUndo.size(); ++u)
                            if (blockUndo[u].name == aName)
                            {
                                if (blockUndo[u].previous.scope != f.scope)
                                {
                                    functionUndo.push_back(undo{ aName, blockUndo[u].previous });
                                    blockUndo[u].previous = { f.scope, aKind, ++now };
                                    scopes[static_cast<std::size_t>(f.scope)].declares = true;
                                }
                                break;
                            }
                        return;
                    }
                    if (symbol.scope == f.scope)
                    {
                        if (symbol.kind == declaration::Lexical)
                            uncertain();
                        else if (aKind == declaration::Function)
                            symbol.kind = declaration::Function;
                        symbol.time = ++now;
                        return;
                    }
                    functionUndo.push_back(undo{ aName, symbol });
                    symbol = { f.scope, aKind, ++now };
                    scopes[static_cast<std::size_t>(f.scope)].declares = true;
                }
            }

            // (a binding identifier (the token): declared; aParameter: a function's parameter's)
            void binding_identifier(declaration aKind, bool aParameter = false)
            {
                if (tok != token::Name || name < ReservedEnd)
                    throw unrecognized{};
                if (name < RestrictedEnd || name == KEval || name == KArguments)
                    uncertain();    // (not in strict code (or where yield, await are operators))
                if (aParameter && symbols[name].scope == functions.back().scope)
                    uncertain();    // (a duplicate: an error in strict code, and where the parameters are not simple)
                declare(name, aKind);
                next();
            }

            // (a binding target: an identifier or a pattern)
            void binding_target(declaration aKind, bool aParameter = false)
            {
                nesting const nested{ *this };
                if (eat(LBracket))
                {
                    while (!eat(RBracket))
                    {
                        if (eat(Comma))
                            continue;
                        bool const rest = eat(Ellipsis);
                        binding_target(aKind, aParameter);
                        if (!rest && eat(Assign))
                            assignment(false);
                        if (rest ? !is(RBracket) : (!is(RBracket) && !is(Comma)))
                            throw unrecognized{};
                        if (!rest)
                            eat(Comma);
                    }
                }
                else if (eat(LBrace))
                {
                    while (!eat(RBrace))
                    {
                        if (eat(Ellipsis))
                        {
                            binding_identifier(aKind, aParameter);
                            if (!is(RBrace))
                                throw unrecognized{};
                            continue;
                        }
                        bool shorthand = false;
                        if (tok == token::Name)
                        {
                            // (a property name (then ':' and its target), or a binding identifier)
                            auto const following = peek();
                            if (following.tok == token::Punctuator && following.punct == Colon)
                                next();
                            else
                            {
                                binding_identifier(aKind, aParameter);
                                shorthand = true;
                            }
                        }
                        else if (tok == token::String || tok == token::Number)
                            next();
                        else if (eat(LBracket))
                        {
                            assignment(false);
                            expect(RBracket);
                        }
                        else
                            throw unrecognized{};
                        if (!shorthand)
                        {
                            expect(Colon);
                            binding_target(aKind, aParameter);
                        }
                        if (eat(Assign))
                            assignment(false);
                        if (!is(RBrace))
                            expect(Comma);
                    }
                }
                else
                    binding_identifier(aKind, aParameter);
            }

            // --- expressions

            enum class operand : std::uint8_t { Other, Identifier, Member, Call, Literal, ParenthesizedTarget, ArrowFunction, Super };
            struct expression_result
            {
                operand kind = operand::Other;
                std::uint32_t identifier = 0u;      ///< (an identifier's (or a call's callee that is one))
                bool unary = false;                 ///< (it begins with a unary operator)
                bool privateMember = false;         ///< (a property whose name is a private name (perhaps parenthesized))
                bool pattern = false;               ///< (an array or object literal that may be an assignment's pattern)
                std::size_t begin = 0u;             ///< (such a literal's source)
                std::size_t end = 0u;
                bool defaulted = false;             ///< (an assignment (=) to what may be a pattern's target: a pattern's element)
            };

            // (what may be assigned to (but a pattern): an identifier or a property)
            static bool simple_target(expression_result const& aTarget)
            {
                if (aTarget.kind == operand::Identifier || aTarget.kind == operand::ParenthesizedTarget)
                    return aTarget.identifier != KEval && aTarget.identifier != KArguments;
                return aTarget.kind == operand::Member;
            }

            // (aResult is no more than an array or object literal that may be a pattern, whose source begins at aBegin (the token
            // that follows it is the current one))
            bool whole_pattern(expression_result const& aResult, std::size_t aBegin) const
            {
                return aResult.pattern && aResult.begin == aBegin && aResult.end == lastEnd;
            }

            // (an element of an array or object literal (parsed by assignment, as an element; its source begins at aBegin) may be a
            // pattern's: a target, a pattern, or one of them with an initializer)
            bool pattern_element(expression_result const& aElement, std::size_t aBegin) const
            {
                return aElement.defaulted || simple_target(aElement) || whole_pattern(aElement, aBegin);
            }

            // (shorthand properties with initializers found since there were aBefore that are not a pattern's: an error)
            void settle_covers(std::size_t aBefore)
            {
                if (pendingCovers != aBefore)
                {
                    uncertain();
                    pendingCovers = aBefore;
                }
            }

            // (a private name is used: one that a class that encloses the code declares (known when the classes close))
            void use_private(std::uint32_t aName)
            {
                if (classes.empty())
                    uncertain();
                else
                    classes.back().used.push_back(aName);
            }

            // (what is assigned to (or incremented, etc.): an identifier or a property; a function with another is not deferrable (a
            // pattern (a destructuring assignment), or an error))
            void assignment_target(expression_result const& aTarget)
            {
                // (a parenthesized target's identifier is its identifier's (a property's: none of these))
                if (aTarget.kind == operand::Identifier || aTarget.kind == operand::ParenthesizedTarget)
                {
                    if (aTarget.identifier == KEval || aTarget.identifier == KArguments)
                        uncertain();
                }
                else if (aTarget.kind != operand::Member)
                    uncertain();
            }

            void expression(bool aNoIn)
            {
                assignment(aNoIn);
                while (eat(Comma))
                    assignment(aNoIn);
            }

            // (aElement: an element of an array or object literal (that may be a pattern: see pattern_element))
            expression_result assignment(bool aNoIn, bool aElement = false)
            {
                nesting const nested{ *this };
                auto& f = functions.back();
                if (f.generator && is_name(KYield))
                {
                    next();
                    if (!newline && !is(RParen) && !is(RBracket) && !is(RBrace) && !is(Comma) && !is(Semicolon) && !is(Colon) && tok != token::End &&
                        !(tok == token::Name && name == KIn) && !(tok == token::Name && name == KOf))
                    {
                        if (star())
                            next();
                        assignment(aNoIn);
                    }
                    return {};
                }
                auto const liveBegin = live.size();
                auto const coversBefore = pendingCovers;
                auto const begin = tokBegin;
                auto const left = conditional(aNoIn);
                if (is(Arrow))
                {
                    // (an async arrow function whose parameters parsed as a call's arguments)
                    if (left.kind != operand::Call || left.identifier != KAsync || newline)
                        throw unrecognized{};
                    live.resize(liveBegin);
                    arrow_function({}, false, true);
                    return { operand::ArrowFunction };
                }
                bool const pattern = whole_pattern(left, begin);
                if (tok == token::Punctuator && (punct == Assign || punct == AssignOp || punct == LogicalAssign || punct == DivideAssign))
                {
                    if (left.kind == operand::ArrowFunction)
                        throw unrecognized{};
                    // (a destructuring assignment: its pattern's shorthand properties may have initializers)
                    bool const destructuring = punct == Assign && pattern;
                    if (destructuring)
                        pendingCovers = coversBefore;
                    else
                    {
                        settle_covers(coversBefore);
                        assignment_target(left);
                    }
                    expression_result result;
                    result.defaulted = destructuring || (punct == Assign && simple_target(left));
                    next();
                    assignment(aNoIn);
                    return result;
                }
                // (an element that is a pattern is its literal's to settle)
                if (!aElement || !pattern)
                    settle_covers(coversBefore);
                return left;
            }

            expression_result conditional(bool aNoIn)
            {
                auto const condition = binary(aNoIn);
                if (!is(Question))
                    return condition;
                if (condition.kind == operand::ArrowFunction)
                    throw unrecognized{};
                next();
                assignment(false);
                expect(Colon);
                assignment(aNoIn);
                return {};
            }

            // (#x in o (the token is the private name), where the private name is the left operand of in: at an expression's start,
            // or after an operator that binds less than in does (those that are certainly so: && || ??))
            bool private_in(bool aNoIn)
            {
                if (tok != token::PrivateName || aNoIn)
                    return false;
                auto const following = peek();
                if (following.tok != token::Name || following.name != KIn)
                    return false;
                use_private(name);
                next();
                return true;
            }

            expression_result binary(bool aNoIn)
            {
                auto left = private_in(aNoIn) ? expression_result{} : unary();
                bool logical = false;
                bool coalesce = false;
                for (;;)
                {
                    bool exponent = false;
                    bool loose = false;
                    if (tok == token::Punctuator)
                    {
                        if (punct == LogicalAnd || punct == LogicalOr)
                            logical = loose = true;
                        else if (punct == Coalesce)
                            coalesce = loose = true;
                        else if (punct == Exponent)
                            exponent = true;
                        else if (punct != Binary && punct != Additive && punct != Divide)
                            break;
                    }
                    else if (tok == token::Name && (name == KInstanceof || (name == KIn && !aNoIn)))
                        ;
                    else
                        break;
                    if (left.kind == operand::ArrowFunction)
                        throw unrecognized{};
                    // (-a ** b is an error; ?? with || or && (without parentheses) is)
                    if ((exponent && left.unary) || (logical && coalesce))
                        uncertain();
                    next();
                    left = loose && private_in(aNoIn) ? expression_result{} : unary();
                    if (left.kind == operand::ArrowFunction)
                        throw unrecognized{};
                    left = { operand::Other, 0u, left.unary };
                }
                return left;
            }

            expression_result unary()
            {
                nesting const nested{ *this };
                if (tok == token::Punctuator && (punct == Not || punct == Additive))
                {
                    next();
                    if (unary().kind == operand::ArrowFunction)
                        throw unrecognized{};
                    return { operand::Other, 0u, true };
                }
                if (tok == token::Punctuator && punct == Increment)
                {
                    next();
                    auto const target = unary();
                    if (target.kind == operand::ArrowFunction)
                        throw unrecognized{};
                    assignment_target(target);
                    return { operand::Other, 0u, true };
                }
                if (tok == token::Name && (name == KTypeof || name == KVoid || name == KDelete))
                {
                    bool const isDelete = name == KDelete;
                    next();
                    auto const target = unary();
                    if (target.kind == operand::ArrowFunction)
                        throw unrecognized{};
                    // (delete of an identifier: an error in strict code; of a property with a private name: an error)
                    if (isDelete && (target.kind == operand::Identifier || target.kind == operand::ParenthesizedTarget || target.privateMember))
                        uncertain();
                    return { operand::Other, 0u, true };
                }
                if (functions.back().async && is_name(KAwait))
                {
                    next();
                    if (unary().kind == operand::ArrowFunction)
                        throw unrecognized{};
                    return { operand::Other, 0u, true };
                }
                auto result = subscripts(primary(), false);
                if (tok == token::Punctuator && punct == Increment && !newline)
                {
                    if (result.kind == operand::ArrowFunction)
                        throw unrecognized{};
                    assignment_target(result);
                    next();
                    result = {};
                }
                return result;
            }

            void arguments()
            {
                while (!eat(RParen))
                {
                    eat(Ellipsis);
                    assignment(false);
                    if (!is(RParen))
                        expect(Comma);
                }
            }

            // (a property's name after a '.' (any identifier name))
            void property_name()
            {
                if (tok == token::PrivateName)
                    use_private(name);
                else if (tok != token::Name)
                    throw unrecognized{};
                next();
            }

            // (what follows an operand: property accesses, calls (unless aNew: a new expression's callee) and tagged templates)
            expression_result subscripts(expression_result aOperand, bool aNew)
            {
                auto result = aOperand;
                if (result.kind == operand::ArrowFunction)
                    return result;
                bool optional = false;
                for (;;)
                {
                    if (eat(Dot))
                    {
                        bool const privateName = tok == token::PrivateName;
                        // (super.#x is an error)
                        if (privateName && result.kind == operand::Super)
                            uncertain();
                        property_name();
                        result = { optional ? operand::Other : operand::Member };
                        result.privateMember = privateName;
                    }
                    else if (is(OptionalChain))
                    {
                        if (aNew)
                            throw unrecognized{};
                        if (result.kind == operand::Super)
                            uncertain();
                        next();
                        optional = true;
                        bool privateName = false;
                        if (eat(LParen))
                            arguments();
                        else if (eat(LBracket))
                        {
                            expression(false);
                            expect(RBracket);
                        }
                        else if (tok == token::Template)
                            throw unrecognized{};
                        else
                        {
                            privateName = tok == token::PrivateName;
                            property_name();
                        }
                        result = {};
                        result.privateMember = privateName;
                    }
                    else if (eat(LBracket))
                    {
                        expression(false);
                        expect(RBracket);
                        result = { optional ? operand::Other : operand::Member };
                    }
                    else if (!aNew && is(LParen))
                    {
                        // (a direct eval: its code may use (and declare) any name of the scopes that enclose it)
                        if ((result.kind == operand::Identifier || result.kind == operand::ParenthesizedTarget) && result.identifier == KEval)
                            direct_eval();
                        next();
                        arguments();
                        result = { operand::Call, result.kind == operand::Identifier ? result.identifier : 0u };
                    }
                    else if (tok == token::Template)
                    {
                        if (optional)
                            uncertain();
                        template_literal();
                        result = {};
                    }
                    else
                        return result;
                }
            }

            void direct_eval()
            {
                // (the function, and those that enclose it, may use any name (see close_scope and run: one is deferrable if no scope
                // that encloses it declares a name that it could use); a function within the function whose scope the code's var
                // declarations are of (an arrow function's: its enclosing function's) is not deferrable)
                functions.back().anyName = true;
                for (auto f = functions.size(); f-- > 0u;)
                {
                    scopes[static_cast<std::size_t>(functions[f].scope)].opaque = true;
                    if (!functions[f].arrow)
                        break;
                }
            }

            void template_literal()
            {
                // (the token is the template's '`')
                for (;;)
                {
                    if (template_characters())
                        break;
                    next();
                    expression(false);
                    if (!is(RBrace))
                        throw unrecognized{};
                    pos = tokEnd;
                }
                tokEnd = pos;
                next();
            }

            expression_result primary()
            {
                nesting const nested{ *this };
                switch (tok)
                {
                case token::Number:
                case token::String:
                    next();
                    return { operand::Literal };
                case token::Template:
                    template_literal();
                    return { operand::Literal };
                case token::PrivateName:
                    // (#x in o, where it is not known to be the left operand of in (see private_in))
                    uncertain();
                    use_private(name);
                    next();
                    if (!is_name(KIn))
                        throw unrecognized{};
                    return {};
                case token::Punctuator:
                    switch (punct)
                    {
                    case Divide:
                    case DivideAssign:
                        regular_expression();
                        next();
                        return { operand::Literal };
                    case LParen:
                        return parenthesized();
                    case LBracket:
                        {
                            // (an array literal; it may be a pattern if its elements may be a pattern's (a rest element is the
                            // last, with no initializer and no comma after it))
                            expression_result result{ operand::Literal };
                            result.begin = tokBegin;
                            result.pattern = true;
                            next();
                            while (!eat(RBracket))
                            {
                                if (eat(Comma))
                                    continue;
                                bool const rest = eat(Ellipsis);
                                auto const elementBegin = tokBegin;
                                auto const element = assignment(false, true);
                                if (!pattern_element(element, elementBegin) || (rest && element.defaulted))
                                    result.pattern = false;
                                if (!is(RBracket))
                                {
                                    expect(Comma);
                                    if (rest)
                                        result.pattern = false;
                                }
                            }
                            result.end = lastEnd;
                            return result;
                        }
                    case LBrace:
                        return object_literal();
                    default:
                        throw unrecognized{};
                    }
                case token::Name:
                    break;
                default:
                    throw unrecognized{};
                }
                auto const id = name;
                switch (id)
                {
                case KThis:
                case KNull:
                case KTrue:
                case KFalse:
                    next();
                    return {};
                case KFunction:
                    function_expression(false);
                    return {};
                case KClass:
                    class_definition(false);
                    return {};
                case KNew:
                    {
                        next();
                        if (eat(Dot))
                        {
                            // (new.target: in a function (not the script's code, or an arrow function's that is in it))
                            if (!is_name(KTarget))
                                throw unrecognized{};
                            next();
                            for (auto f = functions.size(); f-- > 0u;)
                                if (!functions[f].arrow)
                                {
                                    if (functions[f].script)
                                        uncertain();
                                    break;
                                }
                            return {};
                        }
                        auto const callee = subscripts(primary(), true);
                        if (callee.kind == operand::ArrowFunction)
                            throw unrecognized{};
                        // (new super() is an error)
                        if (callee.kind == operand::Super)
                            uncertain();
                        if (eat(LParen))
                            arguments();
                        return {};
                    }
                case KSuper:
                    {
                        // (super.x and super[x]: in a method, a field's initializer or a static block (or an arrow function in
                        // one); super(): in a derived class's constructor (or an arrow function in it))
                        next();
                        if (!is(Dot) && !is(LBracket) && !is(LParen))
                            throw unrecognized{};
                        bool allowed = false;
                        for (auto f = functions.size(); f-- > 0u;)
                            if (!functions[f].arrow)
                            {
                                allowed = is(LParen) ? functions[f].superCall : functions[f].superProperty;
                                break;
                            }
                        if (!allowed)
                            uncertain();
                        return { operand::Super };
                    }
                case KImport:
                    // (import(x), import.meta)
                    uncertain();
                    next();
                    if (eat(Dot))
                    {
                        if (!is_name(KMeta))
                            throw unrecognized{};
                        next();
                    }
                    else
                    {
                        expect(LParen);
                        arguments();
                    }
                    return {};
                default:
                    break;
                }
                if (id < ReservedEnd)
                    throw unrecognized{};
                if (id == KAsync)
                {
                    // (async function, async x => ..., or an identifier (perhaps the callee of what is an async arrow function's
                    // parameters: see assignment))
                    auto const following = peek();
                    if (!following.newline && following.tok == token::Name && following.name == KFunction)
                    {
                        next();
                        function_expression(true);
                        return {};
                    }
                    if (!following.newline && following.tok == token::Name && following.name >= ReservedEnd)
                    {
                        next();
                        auto const parameter = name;
                        next();
                        if (!is(Arrow) || newline)
                            throw unrecognized{};
                        arrow_function({ parameter }, true, true);
                        return { operand::ArrowFunction };
                    }
                }
                next();
                if (is(Arrow) && !newline)
                {
                    arrow_function({ id }, true, false);
                    return { operand::ArrowFunction };
                }
                use(id);
                return { operand::Identifier, id };
            }

            // (a parenthesized expression, or an arrow function's parameters)
            expression_result parenthesized()
            {
                auto const start = current();
                bool const wasUncertain = functions.back().uncertain;
                auto const coversBefore = pendingCovers;
                next();
                auto const usesBegin = uses.size();
                auto const liveBegin = live.size();
                std::vector<std::uint32_t> parameters;
                bool simple = true;     // (identifiers alone: an arrow function's parameters that are declared as they are)
                bool pattern = false;   // (they can only be an arrow function's parameters)
                expression_result only;
                std::size_t count = 0u;
                while (!is(RParen))
                {
                    if (eat(Ellipsis))
                    {
                        simple = false;
                        pattern = true;
                        binding_target_as_expression();
                        if (!is(RParen))
                            throw unrecognized{};
                        break;
                    }
                    auto const elementUses = uses.size();
                    auto const begin = tokBegin;
                    auto const e = assignment(false);
                    if (e.kind == operand::Identifier && lastEnd - begin == identifier_length(e.identifier) && uses.size() == elementUses + 1u)
                        parameters.push_back(e.identifier);
                    else
                        simple = false;
                    only = e;
                    ++count;
                    if (!is(RParen))
                    {
                        expect(Comma);
                        if (is(RParen))
                            pattern = true;     // (a trailing comma)
                    }
                }
                next();
                if (is(Arrow) && !newline)
                {
                    if (simple)
                    {
                        // (its parameters were not uses of the names)
                        uses.resize(usesBegin);
                        arrow_function(parameters, true, false);
                    }
                    else if (reparsing < 2u)
                    {
                        // (its parameters are parsed again, as what they are (what was found in them as an expression is forgotten))
                        uses.resize(usesBegin);
                        live.resize(liveBegin);
                        functions.back().uncertain = wasUncertain;
                        pendingCovers = coversBefore;
                        restore(start);
                        open_function(true, false, false);
                        ++reparsing;
                        this->parameters();
                        --reparsing;
                        if (!is(Arrow) || newline)
                            throw unrecognized{};
                        arrow_body();
                    }
                    else
                    {
                        // (the functions within its parameters are within a scope that is not known)
                        live.resize(liveBegin);
                        arrow_function({}, false, false);
                    }
                    return { operand::ArrowFunction };
                }
                if (pattern || count == 0u)
                    throw unrecognized{};
                if (count == 1u && (only.kind == operand::Identifier || only.kind == operand::Member || only.kind == operand::ParenthesizedTarget))
                {
                    expression_result result{ operand::ParenthesizedTarget, only.kind == operand::Member ? 0u : only.identifier };
                    result.privateMember = only.privateMember;
                    return result;
                }
                return {};
            }

            std::size_t identifier_length(std::uint32_t aName) const
            {
                return iNameLengths.size() > aName ? iNameLengths[aName] : 0u;
            }
            std::vector<std::size_t> iNameLengths;

            // (a rest parameter's target, where it is parsed as an expression (an arrow function's parameters, not known to be yet): its
            // names are uses (the arrow function is then one whose parameters are not known: see arrow_function))
            void binding_target_as_expression()
            {
                assignment(false);
            }

            // (an arrow function (the token is its "=>"); aKnown: its parameters are aParameters (identifiers); else they are not known
            // (they were parsed as expressions: their names are uses of the code that encloses it): no function within it is deferrable)
            void arrow_function(std::vector<std::uint32_t> const& aParameters, bool aKnown, bool aAsync)
            {
                next();
                open_function(true, false, aAsync);
                if (!aKnown || aAsync)
                    uncertain();
                if (!aKnown)
                    scopes.back().opaque = true;
                for (auto const p : aParameters)
                {
                    // (duplicates are an error)
                    if (symbols[p].scope == functions.back().scope || p < RestrictedEnd || p == KEval || p == KArguments)
                        uncertain();
                    declare(p, declaration::Var);
                }
                auto& f = functions.back();
                f.parameterUses = uses.size();
                f.parameterUndo = functionUndo.size();
                arrow_body(true);
            }

            // (an arrow function's body (its function is open, its parameters declared); the token is its "=>" (or, if aAfter, the
            // one after it))
            void arrow_body(bool aAfter = false)
            {
                if (!aAfter)
                    next();
                if (is(LBrace))
                {
                    next();
                    body_directives();
                    while (!is(RBrace))
                        statement(false);
                    // (the token after its body's '}' is found when the function has closed)
                    close_function();
                    next();
                }
                else
                {
                    assignment(false);
                    close_function();
                }
            }

            // (a function's parameters (the token is their '('): declared; the token is then the one after their ')')
            void parameters()
            {
                expect(LParen);
                bool simple = true;
                bool rest = false;
                std::size_t count = 0u;
                while (!eat(RParen))
                {
                    rest = eat(Ellipsis);
                    if (rest || tok != token::Name)
                        simple = false;
                    binding_target(declaration::Var, true);
                    if (is(Assign))
                    {
                        // (a rest parameter has no initializer)
                        if (rest)
                            throw unrecognized{};
                        simple = false;
                        next();
                        assignment(false);
                    }
                    if (rest ? !is(RParen) : !is(RParen) && !is(Comma))
                        throw unrecognized{};
                    eat(Comma);
                    ++count;
                }
                auto& f = functions.back();
                f.nonSimple = !simple;
                f.parameterCount = count;
                f.restParameter = rest;
                f.parameterUses = uses.size();
                f.parameterUndo = functionUndo.size();
            }

            // (a function's body's directives (the token is the first of its statements): one that makes it strict is an error if
            // its parameters are not simple)
            void body_directives()
            {
                directives();
                if (functions.back().strict && functions.back().nonSimple)
                    uncertain();
            }

            // (a function's parameters (the token is their '(') and body; returns its body's braces)
            std::pair<std::size_t, std::size_t> parameters_and_body()
            {
                parameters();
                if (!is(LBrace))
                    throw unrecognized{};
                auto const body = tokBegin;
                next();
                body_directives();
                while (!is(RBrace))
                    statement(false);
                return { body, tokBegin };
            }

            // (a directive prologue (the token is the first of a body's statements))
            void directives()
            {
                while (tok == token::String)
                {
                    auto const text = s.substr(tokBegin, tokEnd - tokBegin);
                    auto const following = peek();
                    // (a directive: a string literal that is a statement)
                    bool const statementEnd = (following.tok == token::Punctuator && (following.punct == Semicolon || following.punct == RBrace)) ||
                        following.tok == token::End || (following.newline && !(following.tok == token::Punctuator && (following.punct == LParen ||
                            following.punct == LBracket || following.punct == Dot || following.punct == OptionalChain || following.punct == Binary ||
                            following.punct == Additive || following.punct == Divide || following.punct == DivideAssign || following.punct == Comma ||
                            following.punct == Question || following.punct == Assign || following.punct == AssignOp || following.punct == LogicalAssign ||
                            following.punct == Exponent || following.punct == LogicalAnd || following.punct == LogicalOr || following.punct == Coalesce)) &&
                            following.tok != token::Template && !(following.tok == token::Name && (following.name == KIn || following.name == KInstanceof)));
                    if (!statementEnd)
                        return;
                    if (text == "\"use strict\"" || text == "'use strict'")
                        functions.back().strict = true;
                    next();
                    semicolon();
                }
            }

            // (a function expression (the token is function))
            void function_expression(bool aAsync)
            {
                next();
                bool generator = false;
                if (star())
                {
                    generator = true;
                    next();
                }
                open_function(false, generator, aAsync);
                if (generator || aAsync)
                    uncertain();
                if (tok == token::Name)
                {
                    // (its own name: a binding of its own)
                    if (name < ReservedEnd)
                        throw unrecognized{};
                    if (name < RestrictedEnd || name == KEval || name == KArguments)
                        uncertain();
                    declare(name, declaration::Var);
                    functions.back().ownName = name;
                    next();
                }
                auto const body = parameters_and_body();
                // (not one that is called where it is (e.g. (function () { ... })()): it would be compiled at once)
                bool called = false;
                {
                    auto const following = peek();
                    if (following.tok == token::Punctuator && following.punct == LParen)
                        called = true;
                    else if (following.tok == token::Punctuator && following.punct == RParen)
                    {
                        // (the token after the ')')
                        auto p = pos;
                        std::swap(p, pos);
                        auto const savedBegin = tokBegin, savedEnd = tokEnd, savedLast = lastEnd;
                        auto const savedTok = tok;
                        auto const savedPunct = punct;
                        auto const savedName = name;
                        bool const savedNewline = newline, savedLineStart = iLineStart, savedStar = iStar;
                        auto const savedUncertain = functions.back().uncertain;
                        next();
                        next();
                        called = is(LParen);
                        pos = p; tokBegin = savedBegin; tokEnd = savedEnd; lastEnd = savedLast; tok = savedTok; punct = savedPunct; name = savedName;
                        newline = savedNewline; iLineStart = savedLineStart; iStar = savedStar;
                        functions.back().uncertain = savedUncertain;
                    }
                }
                close_function(body.first, body.second, !called);
                next();
            }

            // (a function declaration (the token is function); aLexical: in a block (a lexical declaration))
            void function_declaration(bool aAsync, bool aLexical)
            {
                next();
                bool generator = false;
                if (star())
                {
                    generator = true;
                    next();
                }
                // (its name: declared where the declaration is)
                if (tok != token::Name || name < ReservedEnd)
                    throw unrecognized{};
                if (name < RestrictedEnd || name == KEval || name == KArguments)
                    uncertain();
                declare(name, aLexical ? declaration::Lexical : declaration::Function);
                // (a block's function declaration may also be a var of its function (Annex B): taken to be (the functions within its
                // function that use the name are then not deferrable))
                if (aLexical)
                    declare(name, declaration::Var);
                open_function(false, generator, aAsync);
                if (generator || aAsync)
                    uncertain();
                next();
                parameters_and_body();
                close_function();
                next();
            }

            // (a method's function: its parameters and body (the token is their '('); aSuperCall: a derived class's constructor)
            void method(bool aGenerator, bool aAsync, int aAccessor, bool aSuperCall = false)
            {
                // (a method is not deferrable (its home object is where it is); what contains it may be)
                open_function(false, aGenerator, aAsync);
                functions.back().superProperty = true;
                functions.back().superCall = aSuperCall;
                if (aGenerator || aAsync)
                    uncertain();
                parameters_and_body();
                // (a getter has no parameters; a setter has one, that is not a rest parameter)
                if ((aAccessor == 1 && functions.back().parameterCount != 0u) ||
                    (aAccessor == 2 && (functions.back().parameterCount != 1u || functions.back().restParameter)))
                    uncertain();
                close_function();
                next();
            }

            // (a property's or a class element's name; returns the name's identifier if it is one (else ~0))
            std::uint32_t element_name()
            {
                if (tok == token::Name || tok == token::PrivateName)
                {
                    auto const id = tok == token::Name ? name : ~std::uint32_t{};
                    next();
                    return id;
                }
                if (tok == token::String || tok == token::Number)
                    next();
                else if (eat(LBracket))
                {
                    assignment(false);
                    expect(RBracket);
                }
                else
                    throw unrecognized{};
                return ~std::uint32_t{};
            }

            // (the token (async, get, set or static) is a modifier of the element that follows it: not the element's name)
            bool modifier()
            {
                auto const following = peek();
                if (following.tok == token::Name || following.tok == token::PrivateName || following.tok == token::String || following.tok == token::Number)
                    return true;
                return following.tok == token::Punctuator && (following.punct == LBracket || following.star);
            }

            // (an object literal; it may be a pattern if its properties may be a pattern's (no methods; a rest property is the last,
            // its target is an identifier or a property, and no comma follows it))
            expression_result object_literal()
            {
                expression_result result{ operand::Literal };
                result.begin = tokBegin;
                result.pattern = true;
                next();
                bool proto = false;
                while (!eat(RBrace))
                {
                    if (eat(Ellipsis))
                    {
                        if (!simple_target(assignment(false)) || !is(RBrace))
                            result.pattern = false;
                    }
                    else
                    {
                        bool async = false;
                        bool generator = false;
                        int accessor = 0;
                        if (is_name(KAsync) && modifier() && !peek().newline)
                        {
                            async = true;
                            next();
                        }
                        if (star())
                        {
                            generator = true;
                            next();
                        }
                        else if (!async && (is_name(KGet) || is_name(KSet)) && modifier())
                        {
                            accessor = is_name(KGet) ? 1 : 2;
                            next();
                        }
                        bool const plain = tok == token::Name && !async && !generator && accessor == 0;
                        bool const stringProto = tok == token::String && (s.substr(tokBegin, tokEnd - tokBegin) == "\"__proto__\"" || s.substr(tokBegin, tokEnd - tokBegin) == "'__proto__'");
                        if (tok == token::PrivateName)
                            throw unrecognized{};
                        auto const id = element_name();
                        if (is(LParen))
                        {
                            method(generator, async, accessor);
                            result.pattern = false;
                        }
                        else if (async || generator || accessor != 0)
                            throw unrecognized{};
                        else if (eat(Colon))
                        {
                            // (two __proto__ properties are an error (not in a pattern: taken to be one there too))
                            if (id == KProto || stringProto)
                            {
                                if (proto)
                                    uncertain();
                                proto = true;
                            }
                            auto const valueBegin = tokBegin;
                            if (!pattern_element(assignment(false, true), valueBegin))
                                result.pattern = false;
                        }
                        else
                        {
                            // (a shorthand property: an identifier; with an initializer: only in a pattern (see settle_covers))
                            if (!plain || id < ReservedEnd)
                                throw unrecognized{};
                            if (id == KEval || id == KArguments)
                                result.pattern = false;
                            use(id);
                            if (eat(Assign))
                            {
                                ++pendingCovers;
                                assignment(false);
                            }
                        }
                    }
                    if (!is(RBrace))
                        expect(Comma);
                }
                result.end = lastEnd;
                return result;
            }

            // (a class's private name is declared: once (or by a getter and a setter that are both static, or neither is))
            void declare_private(std::uint32_t aName, int aAccessor, bool aStatic)
            {
                auto& declared = classes.back().declared;
                for (auto const& d : declared)
                    if (d.name == aName && (aAccessor == 0 || d.accessor == 0 || d.accessor == aAccessor || d.isStatic != aStatic))
                        uncertain();
                declared.push_back(private_name{ aName, aAccessor, aStatic });
            }

            // (a class (the token is class); aDeclaration: its name is declared where it is. What is within it is not deferrable
            // (a function there may use the class's private names); what contains it may be: its own early errors are found here
            // (its code is strict: what is an error in strict code alone makes a function uncertain wherever it is))
            void class_definition(bool aDeclaration)
            {
                next();
                if (aDeclaration)
                    binding_identifier(declaration::Lexical);
                open_scope();
                scopes.back().opaque = true;
                if (!aDeclaration && tok == token::Name && name != KExtends)
                    binding_identifier(declaration::Lexical);
                bool derived = false;
                if (eat_name(KExtends))
                {
                    // (its heritage: the private names used in it are not the class's own)
                    derived = true;
                    auto const coversBefore = pendingCovers;
                    auto const heritage = subscripts(primary(), false);
                    if (heritage.kind == operand::ArrowFunction)
                        throw unrecognized{};
                    settle_covers(coversBefore);
                }
                expect(LBrace);
                classes.emplace_back();
                bool constructor = false;
                while (!eat(RBrace))
                {
                    if (eat(Semicolon))
                        continue;
                    bool isStatic = false;
                    if (is_name(KStatic))
                    {
                        auto const following = peek();
                        if (following.tok == token::Punctuator && following.punct == LBrace)
                        {
                            // (a static block: a function's body, with no return statement, arguments or super())
                            next();
                            open_function(false, false, false);
                            functions.back().superProperty = true;
                            functions.back().noArguments = true;
                            functions.back().staticBlock = true;
                            next();
                            while (!is(RBrace))
                                statement(false);
                            close_function();
                            next();
                            continue;
                        }
                        // (static is a modifier of the element that follows it, or the element's name)
                        if (following.tok == token::Name || following.tok == token::PrivateName || following.tok == token::String || following.tok == token::Number ||
                            (following.tok == token::Punctuator && (following.punct == LBracket || following.star)))
                        {
                            next();
                            isStatic = true;
                        }
                    }
                    bool async = false;
                    bool generator = false;
                    int accessor = 0;
                    if (is_name(KAsync) && modifier() && !peek().newline)
                    {
                        async = true;
                        next();
                    }
                    if (star())
                    {
                        generator = true;
                        next();
                    }
                    else if (!async && (is_name(KGet) || is_name(KSet)) && modifier())
                    {
                        accessor = is_name(KGet) ? 1 : 2;
                        next();
                    }
                    // (the element's name: constructor and prototype are names with rules (as identifiers or strings: a string
                    // with an escape may be either))
                    bool const privateName = tok == token::PrivateName;
                    auto const privateId = name;
                    auto const text = tok == token::String ? s.substr(tokBegin + 1u, tokEnd - tokBegin - 2u) : std::string_view{};
                    if (text.find('\\') != std::string_view::npos)
                        uncertain();
                    bool const namedConstructor = is_name(KConstructor) || (tok == token::String && text == "constructor");
                    bool const namedPrototype = is_name(KPrototype) || (tok == token::String && text == "prototype");
                    element_name();
                    if (privateName && privateId == KPrivateConstructor)
                        uncertain();
                    if (is(LParen))
                    {
                        // (a method: the class's constructor is one method (not static), not an accessor, a generator or async; a
                        // static method is not named prototype)
                        bool const isConstructor = !isStatic && namedConstructor;
                        if (isConstructor)
                        {
                            if (constructor || generator || async || accessor != 0)
                                uncertain();
                            constructor = true;
                        }
                        if (isStatic && namedPrototype)
                            uncertain();
                        if (privateName)
                            declare_private(privateId, accessor, isStatic);
                        method(generator, async, accessor, isConstructor && derived);
                    }
                    else if (async || generator || accessor != 0)
                        throw unrecognized{};
                    else
                    {
                        // (a field (not named constructor; a static one not prototype): its initializer is a function's code, in
                        // which arguments and super() are errors)
                        if (namedConstructor || (isStatic && namedPrototype))
                            uncertain();
                        if (privateName)
                            declare_private(privateId, 0, isStatic);
                        if (eat(Assign))
                        {
                            open_function(false, false, false);
                            functions.back().superProperty = true;
                            functions.back().noArguments = true;
                            assignment(false);
                            close_function();
                        }
                        semicolon();
                    }
                }
                // (the private names used in it that it does not declare are an enclosing class's (an error if none declares them))
                auto const body = std::move(classes.back());
                classes.pop_back();
                for (auto const used : body.used)
                    if (std::none_of(body.declared.begin(), body.declared.end(), [&](private_name const& d) { return d.name == used; }))
                        use_private(used);
                close_scope();
            }

            // --- statements

            // (var, let or const declarations (the token is the first's target): how many, whether the last has no initializer, and
            // whether one that must have an initializer has none (an error unless it is a for-in or for-of statement's))
            struct declared { std::size_t count = 0u; bool bare = false; bool missing = false; };
            declared declarations(declaration aKind, bool aConst, bool aNoIn)
            {
                declared result;
                do
                {
                    bool const pattern = tok != token::Name;
                    if (aKind == declaration::Lexical && is_name(KLet))
                        uncertain();
                    binding_target(aKind);
                    result.bare = !eat(Assign);
                    if (!result.bare)
                        assignment(aNoIn);
                    else if (pattern || aConst)
                        result.missing = true;
                    ++result.count;
                } while (eat(Comma));
                return result;
            }

            // (let is a declaration's (the token is let): an identifier or a pattern follows it)
            bool let_declaration()
            {
                auto const following = peek();
                if (following.tok == token::Name)
                    return following.name >= ReservedEnd && following.name != KIn && following.name != KInstanceof && following.name != KOf;
                return following.tok == token::Punctuator && (following.punct == LBracket || following.punct == LBrace);
            }

            void block()
            {
                // (the token is its '{')
                next();
                open_scope();
                while (!is(RBrace))
                    statement(false);
                close_scope();
                next();
            }

            // (a statement that is another's body (not a statement list's): a declaration there is an error or (a function's) one in
            // strict code)
            void body_statement()
            {
                statement(true);
            }

            void loop_body()
            {
                auto& f = functions.back();
                ++f.loops;
                ++f.breakables;
                body_statement();
                auto& g = functions.back();
                --g.loops;
                --g.breakables;
            }

            void statement(bool aBody, bool aLabelled = false)
            {
                nesting const nested{ *this };
                if (tok == token::Punctuator)
                {
                    if (punct == LBrace)
                    {
                        block();
                        return;
                    }
                    if (punct == Semicolon)
                    {
                        next();
                        return;
                    }
                    expression(false);
                    semicolon();
                    return;
                }
                if (tok != token::Name)
                {
                    if (tok == token::End)
                        throw unrecognized{};
                    expression(false);
                    semicolon();
                    return;
                }
                switch (name)
                {
                case KVar:
                    next();
                    if (declarations(declaration::Var, false, false).missing)
                        uncertain();
                    semicolon();
                    return;
                case KConst:
                    if (aBody || aLabelled)
                        uncertain();
                    next();
                    if (declarations(declaration::Lexical, true, false).missing)
                        uncertain();
                    semicolon();
                    return;
                case KLet:
                    if (let_declaration())
                    {
                        if (aBody || aLabelled)
                            uncertain();
                        next();
                        if (declarations(declaration::Lexical, false, false).missing)
                            uncertain();
                        semicolon();
                        return;
                    }
                    break;
                case KFunction:
                    // (in a block (or as a statement's body, or labelled): a lexical declaration (and perhaps a var: Annex B); a
                    // function that has one is not deferrable (what it may be declared with is not checked))
                    if (aBody || aLabelled || !scopes.back().function)
                        uncertain();
                    function_declaration(false, !scopes.back().function);
                    return;
                case KAsync:
                    {
                        auto const following = peek();
                        if (!following.newline && following.tok == token::Name && following.name == KFunction)
                        {
                            if (aBody || aLabelled || !scopes.back().function)
                                uncertain();
                            next();
                            function_declaration(true, !scopes.back().function);
                            return;
                        }
                    }
                    break;
                case KClass:
                    if (aBody || aLabelled)
                        uncertain();
                    class_definition(true);
                    return;
                case KIf:
                    next();
                    expect(LParen);
                    expression(false);
                    expect(RParen);
                    body_statement();
                    if (eat_name(KElse))
                        body_statement();
                    return;
                case KFor:
                    for_statement();
                    return;
                case KWhile:
                    next();
                    expect(LParen);
                    expression(false);
                    expect(RParen);
                    loop_body();
                    return;
                case KDo:
                    next();
                    loop_body();
                    if (!eat_name(KWhile))
                        throw unrecognized{};
                    expect(LParen);
                    expression(false);
                    expect(RParen);
                    eat(Semicolon);
                    return;
                case KContinue:
                case KBreak:
                    {
                        bool const isContinue = name == KContinue;
                        next();
                        auto& f = functions.back();
                        if (tok == token::Name && !newline && name >= ReservedEnd)
                        {
                            // (its label: one of the statements that enclose it (a loop's, if it is a continue's))
                            auto const target = std::find_if(f.labels.rbegin(), f.labels.rend(), [&](label const& l) { return l.name == name; });
                            if (target == f.labels.rend() || (isContinue && !target->loop))
                                uncertain();
                            next();
                        }
                        else if (isContinue ? f.loops == 0u : f.breakables == 0u)
                            uncertain();
                        semicolon();
                    }
                    return;
                case KReturn:
                    if (functions.back().script)
                        throw unrecognized{};
                    if (functions.back().staticBlock)
                        uncertain();
                    next();
                    if (!newline && !is(Semicolon) && !is(RBrace) && tok != token::End)
                        expression(false);
                    semicolon();
                    return;
                case KWith:
                    // (its names are found when the code runs: a function within it is not deferrable (nor one that it is in: not
                    // in strict code))
                    uncertain();
                    next();
                    expect(LParen);
                    expression(false);
                    expect(RParen);
                    open_scope();
                    scopes.back().opaque = true;
                    body_statement();
                    close_scope();
                    return;
                case KSwitch:
                    {
                        next();
                        expect(LParen);
                        expression(false);
                        expect(RParen);
                        expect(LBrace);
                        open_scope();
                        ++functions.back().breakables;
                        bool defaulted = false;
                        while (!is(RBrace))
                        {
                            if (eat_name(KCase))
                                expression(false);
                            else if (eat_name(KDefault))
                            {
                                if (defaulted)
                                    uncertain();
                                defaulted = true;
                            }
                            else
                                throw unrecognized{};
                            expect(Colon);
                            while (!is(RBrace) && !is_name(KCase) && !is_name(KDefault))
                                statement(false);
                        }
                        --functions.back().breakables;
                        close_scope();
                        next();
                    }
                    return;
                case KThrow:
                    next();
                    if (newline)
                        throw unrecognized{};
                    expression(false);
                    semicolon();
                    return;
                case KTry:
                    {
                        next();
                        if (!is(LBrace))
                            throw unrecognized{};
                        block();
                        bool handled = false;
                        if (eat_name(KCatch))
                        {
                            handled = true;
                            // (its parameter and its block's declarations: one scope (a declaration of the parameter's name there is
                            // an error, but a var's of a parameter that is an identifier: not checked))
                            open_scope();
                            if (eat(LParen))
                            {
                                binding_target(declaration::Lexical);
                                expect(RParen);
                            }
                            if (!is(LBrace))
                                throw unrecognized{};
                            next();
                            while (!is(RBrace))
                                statement(false);
                            close_scope();
                            next();
                        }
                        if (eat_name(KFinally))
                        {
                            handled = true;
                            if (!is(LBrace))
                                throw unrecognized{};
                            block();
                        }
                        if (!handled)
                            throw unrecognized{};
                    }
                    return;
                case KDebugger:
                    next();
                    semicolon();
                    return;
                case KImport:
                    {
                        // (import(x) and import.meta are expressions; an import declaration is a module's)
                        auto const following = peek();
                        if (following.tok != token::Punctuator || (following.punct != LParen && following.punct != Dot))
                            throw unrecognized{};
                    }
                    break;
                case KExport:
                case KEnum:
                    throw unrecognized{};
                default:
                    break;
                }
                // (a labelled statement)
                if (name >= ReservedEnd)
                {
                    auto const following = peek();
                    if (following.tok == token::Punctuator && following.punct == Colon)
                    {
                        auto const labelName = name;
                        if (labelName < RestrictedEnd)
                            uncertain();
                        next();
                        next();
                        auto& labels = functions.back().labels;
                        if (std::any_of(labels.begin(), labels.end(), [&](label const& l) { return l.name == labelName; }))
                            uncertain();
                        bool const loop = is_name(KFor) || is_name(KWhile) || is_name(KDo);
                        labels.push_back(label{ labelName, loop });
                        ++functions.back().breakables;
                        if (aBody && is_name(KFunction))
                            uncertain();
                        statement(aBody, true);
                        --functions.back().breakables;
                        functions.back().labels.pop_back();
                        return;
                    }
                }
                expression(false);
                semicolon();
            }

            void for_statement()
            {
                next();
                if (is_name(KAwait))
                {
                    uncertain();
                    next();
                }
                expect(LParen);
                open_scope();
                bool each = false;
                if (is(Semicolon))
                    ;
                else if (is_name(KVar) || is_name(KConst) || (is_name(KLet) && let_declaration()))
                {
                    auto const kind = is_name(KVar) ? declaration::Var : declaration::Lexical;
                    bool const isConst = is_name(KConst);
                    next();
                    auto const head = declarations(kind, isConst, true);
                    if (is_name(KIn) || is_name(KOf))
                    {
                        // (one declaration, with no initializer)
                        if (head.count != 1u || !head.bare)
                            uncertain();
                        each = true;
                    }
                    else if (head.missing)
                        uncertain();
                }
                else
                {
                    bool const startsLet = is_name(KLet);
                    bool const startsAsync = is_name(KAsync);
                    auto const begin = tokBegin;
                    auto const usesBegin = uses.size();
                    auto const coversBefore = pendingCovers;
                    auto const target = assignment(true, true);
                    if (is_name(KIn) || is_name(KOf))
                    {
                        // (its target: a pattern, or what may be assigned to)
                        if (target.kind == operand::ArrowFunction)
                            throw unrecognized{};
                        if (whole_pattern(target, begin))
                            pendingCovers = coversBefore;
                        else
                        {
                            settle_covers(coversBefore);
                            assignment_target(target);
                        }
                        if (startsLet || (startsAsync && is_name(KOf) && lastEnd - begin == 5u))
                            uncertain();
                        (void)usesBegin;
                        each = true;
                    }
                    else
                    {
                        settle_covers(coversBefore);
                        while (eat(Comma))
                            assignment(true);
                    }
                }
                if (each)
                {
                    if (eat_name(KOf))
                        assignment(false);
                    else
                    {
                        next();
                        expression(false);
                    }
                }
                else
                {
                    expect(Semicolon);
                    if (!is(Semicolon))
                        expression(false);
                    expect(Semicolon);
                    if (!is(RParen))
                        expression(false);
                }
                expect(RParen);
                loop_body();
                close_scope();
            }
        };
    }

    std::vector<deferrable_function> deferrable_functions(std::string_view aSource)
    {
        // (a script that is not large has none (see LargeProgram))
        if (aSource.size() < LargeProgram)
            return {};
        try
        {
            return recognizer{ aSource }.run();
        }
        catch (unrecognized const&)
        {
            return {};
        }
    }
}
