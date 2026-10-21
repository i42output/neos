/*
  early_errors.cpp

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

// JavaScript (ECMAScript): the static semantics' early errors of a script (ECMA-262's "Static Semantics: Early Errors"
// clauses, with Annex B's (web compatibility) modifications for non-strict code), checked on the syntax tree before its
// analysis: what the grammar (languages/javascript.neos) accepts but the language does not (declarations that conflict,
// labels, strict mode code's restrictions, the contexts of yield, await, super, new.target and private names, classes'
// elements, assignment targets, literals, automatic semicolon insertion, ...)

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

namespace neos::compilers::javascript
{
    std::string name_of(node const* aNode);     ///< (compiler.cpp: an identifier's name (its escapes decoded))

    namespace
    {
        // --- the source's text

        bool is_line_terminator(char32_t aCodePoint)
        {
            return aCodePoint == U'\n' || aCodePoint == U'\r' || aCodePoint == 0x2028u || aCodePoint == 0x2029u;
        }

        bool is_white_space(char32_t aCodePoint)
        {
            switch (aCodePoint)
            {
            case 0x09u: case 0x0Bu: case 0x0Cu: case 0x20u: case 0xA0u: case 0xFEFFu: case 0x1680u: case 0x202Fu: case 0x205Fu: case 0x3000u:
                return true;
            default:
                return aCodePoint >= 0x2000u && aCodePoint <= 0x200Au;
            }
        }

        // (the code point at aPosition (UTF-8) and its length)
        char32_t code_point(std::string_view aText, std::size_t aPosition, std::size_t& aLength)
        {
            auto const c = static_cast<unsigned char>(aText[aPosition]);
            std::size_t const n = c < 0x80u ? 1u : c < 0xE0u ? 2u : c < 0xF0u ? 3u : 4u;
            char32_t result = n == 1u ? c : n == 2u ? (c & 0x1Fu) : n == 3u ? (c & 0x0Fu) : (c & 0x07u);
            for (std::size_t i = 1u; i < n && aPosition + i < aText.size(); ++i)
                result = (result << 6) | (static_cast<unsigned char>(aText[aPosition + i]) & 0x3Fu);
            aLength = std::min(n, aText.size() - aPosition);
            return result;
        }

        bool is_identifier_character(char32_t aCodePoint)
        {
            return (aCodePoint >= U'a' && aCodePoint <= U'z') || (aCodePoint >= U'A' && aCodePoint <= U'Z') || (aCodePoint >= U'0' && aCodePoint <= U'9') ||
                aCodePoint == U'_' || aCodePoint == U'$' || aCodePoint == U'\\' || (aCodePoint >= 0x80u && !is_white_space(aCodePoint) && !is_line_terminator(aCodePoint));
        }

        bool is_hex_digit(char aCharacter)
        {
            return (aCharacter >= '0' && aCharacter <= '9') || (aCharacter >= 'a' && aCharacter <= 'f') || (aCharacter >= 'A' && aCharacter <= 'F');
        }

        unsigned hex_value(char aCharacter)
        {
            return aCharacter <= '9' ? static_cast<unsigned>(aCharacter - '0') : static_cast<unsigned>((aCharacter | 0x20) - 'a' + 10);
        }

        // (the position of the next token after aPosition (white space and comments skipped); aLineTerminator: a line terminator was skipped)
        std::size_t next_token(std::string_view aText, std::size_t aPosition, bool& aLineTerminator)
        {
            aLineTerminator = false;
            while (aPosition < aText.size())
            {
                std::size_t length = 1u;
                auto const c = code_point(aText, aPosition, length);
                if (is_line_terminator(c))
                {
                    aLineTerminator = true;
                    aPosition += length;
                }
                else if (is_white_space(c))
                    aPosition += length;
                else if (c == U'/' && aPosition + 1u < aText.size() && aText[aPosition + 1u] == '/')
                {
                    aLineTerminator = true;     // (a line comment ends at a line terminator (or the end))
                    while (aPosition < aText.size())
                    {
                        auto const d = code_point(aText, aPosition, length);
                        if (is_line_terminator(d))
                            break;
                        aPosition += length;
                    }
                }
                else if (c == U'/' && aPosition + 1u < aText.size() && aText[aPosition + 1u] == '*')
                {
                    auto const end = aText.find("*/", aPosition + 2u);
                    if (end == std::string_view::npos)
                        break;      // (an unterminated comment: a token (an error))
                    auto const stop = end + 2u;
                    for (std::size_t i = aPosition; i < stop; i += length)
                        if (is_line_terminator(code_point(aText, i, length)))
                            aLineTerminator = true;
                    aPosition = stop;
                }
                else
                    break;
            }
            return aPosition;
        }

        // (a string's value: its escapes decoded (UTF-8; invalid escapes: their characters))
        std::string string_value(std::string_view aBody)
        {
            std::string result;
            auto const append = [&](char32_t aCodePoint)
            {
                if (aCodePoint < 0x80u)
                    result.push_back(static_cast<char>(aCodePoint));
                else if (aCodePoint < 0x800u)
                {
                    result.push_back(static_cast<char>(0xC0u | (aCodePoint >> 6)));
                    result.push_back(static_cast<char>(0x80u | (aCodePoint & 0x3Fu)));
                }
                else if (aCodePoint < 0x10000u)
                {
                    result.push_back(static_cast<char>(0xE0u | (aCodePoint >> 12)));
                    result.push_back(static_cast<char>(0x80u | ((aCodePoint >> 6) & 0x3Fu)));
                    result.push_back(static_cast<char>(0x80u | (aCodePoint & 0x3Fu)));
                }
                else
                {
                    result.push_back(static_cast<char>(0xF0u | (aCodePoint >> 18)));
                    result.push_back(static_cast<char>(0x80u | ((aCodePoint >> 12) & 0x3Fu)));
                    result.push_back(static_cast<char>(0x80u | ((aCodePoint >> 6) & 0x3Fu)));
                    result.push_back(static_cast<char>(0x80u | (aCodePoint & 0x3Fu)));
                }
            };
            for (std::size_t i = 0u; i < aBody.size();)
            {
                if (aBody[i] != '\\' || i + 1u >= aBody.size())
                {
                    result.push_back(aBody[i++]);
                    continue;
                }
                char const e = aBody[i + 1u];
                i += 2u;
                switch (e)
                {
                case 'n': result.push_back('\n'); break;
                case 't': result.push_back('\t'); break;
                case 'r': result.push_back('\r'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'v': result.push_back('\v'); break;
                case '0': result.push_back('\0'); break;
                case 'x':
                    if (i + 2u <= aBody.size() && is_hex_digit(aBody[i]) && is_hex_digit(aBody[i + 1u]))
                    {
                        append(hex_value(aBody[i]) * 16u + hex_value(aBody[i + 1u]));
                        i += 2u;
                    }
                    break;
                case 'u':
                    if (i < aBody.size() && aBody[i] == '{')
                    {
                        char32_t cp = 0u;
                        std::size_t j = i + 1u;
                        while (j < aBody.size() && is_hex_digit(aBody[j]))
                            cp = cp * 16u + hex_value(aBody[j++]);
                        append(cp);
                        i = j < aBody.size() ? j + 1u : j;
                    }
                    else if (i + 4u <= aBody.size())
                    {
                        char32_t cp = 0u;
                        for (std::size_t k = 0u; k < 4u; ++k)
                            cp = cp * 16u + hex_value(aBody[i + k]);
                        append(cp);
                        i += 4u;
                    }
                    break;
                case '\r':
                    if (i < aBody.size() && aBody[i] == '\n')
                        ++i;
                    break;
                case '\n':
                    break;
                default:
                    result.push_back(e);
                    break;
                }
            }
            return result;
        }

        enum class escape_error
        {
            None,
            Invalid,            ///< (\x or \u without its hex digits, a code point beyond U+10FFFF)
            LegacyOctal,        ///< (\1 .. \7, \0 followed by a digit)
            NonOctalDecimal     ///< (\8, \9)
        };

        // (a string literal's or template's characters: their first escape that is not a plain escape)
        escape_error check_escapes(std::string_view aBody, bool aTemplate)
        {
            auto result = escape_error::None;
            for (std::size_t i = 0u; i + 1u < aBody.size(); ++i)
            {
                if (aBody[i] != '\\')
                    continue;
                char const e = aBody[++i];
                if (e == 'x')
                {
                    if (i + 2u >= aBody.size() || !is_hex_digit(aBody[i + 1u]) || !is_hex_digit(aBody[i + 2u]))
                        return escape_error::Invalid;
                    i += 2u;
                }
                else if (e == 'u')
                {
                    if (i + 1u < aBody.size() && aBody[i + 1u] == '{')
                    {
                        std::size_t j = i + 2u;
                        std::uint64_t value = 0u;
                        while (j < aBody.size() && is_hex_digit(aBody[j]))
                        {
                            value = value * 16u + hex_value(aBody[j++]);
                            if (value > 0x10FFFFu)
                                return escape_error::Invalid;
                        }
                        if (j == i + 2u || j >= aBody.size() || aBody[j] != '}')
                            return escape_error::Invalid;
                        i = j;
                    }
                    else
                    {
                        for (std::size_t k = 1u; k <= 4u; ++k)
                            if (i + k >= aBody.size() || !is_hex_digit(aBody[i + k]))
                                return escape_error::Invalid;
                        i += 4u;
                    }
                }
                else if (e == '0' && !(i + 1u < aBody.size() && aBody[i + 1u] >= '0' && aBody[i + 1u] <= '9'))
                    continue;
                else if (e >= '0' && e <= '7')
                {
                    if (aTemplate)
                        return escape_error::LegacyOctal;
                    if (result == escape_error::None)
                        result = escape_error::LegacyOctal;
                }
                else if (e == '8' || e == '9')
                {
                    if (aTemplate)
                        return escape_error::NonOctalDecimal;
                    if (result == escape_error::None)
                        result = escape_error::NonOctalDecimal;
                }
            }
            return result;
        }

        // --- names

        // (a name's characters are identifier characters: with unicode escapes, ASCII letters, digits (not first), '$' and '_' (and
        // any non-ASCII character but white space); ZWNJ and ZWJ are not the first)
        bool valid_name(std::string_view aName, bool aEscaped)
        {
            for (std::size_t i = 0u; i < aName.size();)
            {
                std::size_t length = 1u;
                auto const c = code_point(aName, i, length);
                if (i == 0u && (c == 0x200Cu || c == 0x200Du))
                    return false;
                if (c == 0x180Eu || c == 0x2E2Fu)
                    return false;   // (not identifier characters (U+180E is a format character, U+2E2F is Pattern_Syntax))
                bool const valid = !aEscaped || (c >= 0x80u ? !is_white_space(c) && !is_line_terminator(c) :
                    (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || c == U'$' || c == U'_' || (i > 0u && c >= U'0' && c <= U'9'));
                if (!valid)
                    return false;
                i += length;
            }
            return !aName.empty();
        }

        bool is_reserved_word(std::string_view aName)
        {
            static std::set<std::string_view> const sReserved = {
                "break", "case", "catch", "class", "const", "continue", "debugger", "default", "delete", "do", "else", "enum", "export",
                "extends", "false", "finally", "for", "function", "if", "import", "in", "instanceof", "new", "null", "return", "super",
                "switch", "this", "throw", "true", "try", "typeof", "var", "void", "while", "with" };
            return sReserved.contains(aName);
        }

        bool is_strict_reserved_word(std::string_view aName)
        {
            static std::set<std::string_view> const sReserved = {
                "implements", "interface", "let", "package", "private", "protected", "public", "static", "yield" };
            return sReserved.contains(aName);
        }

        // --- the syntax tree

        bool is_function_node(node const* aNode)
        {
            switch (aNode->kind)
            {
            case construct::Function:
            case construct::AsyncFunction:
            case construct::FunctionExpression:
            case construct::AsyncFunctionExpression:
            case construct::Arrow:
            case construct::AsyncArrow:
            case construct::Method:
            case construct::Getter:
            case construct::Setter:
            case construct::GeneratorMethod:
            case construct::AsyncMethod:
            case construct::AsyncGeneratorMethod:
                return true;
            default:
                return false;
            }
        }

        bool is_method_node(node const* aNode)
        {
            switch (aNode->kind)
            {
            case construct::Method:
            case construct::Getter:
            case construct::Setter:
            case construct::GeneratorMethod:
            case construct::AsyncMethod:
            case construct::AsyncGeneratorMethod:
                return true;
            default:
                return false;
            }
        }

        bool is_class_node(node const* aNode)
        {
            return aNode->is(construct::Class) || aNode->is(construct::ClassExpression);
        }

        bool is_iteration(node const* aNode)
        {
            switch (aNode->kind)
            {
            case construct::For:
            case construct::ForIn:
            case construct::ForOf:
            case construct::ForAwaitOf:
            case construct::While:
            case construct::Do:
                return true;
            default:
                return false;
            }
        }

        bool is_for(node const* aNode)
        {
            return aNode->is(construct::For) || aNode->is(construct::ForIn) || aNode->is(construct::ForOf) || aNode->is(construct::ForAwaitOf);
        }

        // (a container of a statement list: its statements are statement list items (declarations are allowed))
        bool is_statement_list(node const* aNode)
        {
            switch (aNode->kind)
            {
            case construct::Script:
            case construct::Body:
            case construct::Block:
            case construct::Case:
            case construct::Default:
            case construct::StaticBlock:
                return true;
            default:
                return false;
            }
        }

        node* find_child(node const* aNode, construct aKind)
        {
            for (auto* c : aNode->children)
                if (c->is(aKind))
                    return c;
            return nullptr;
        }

        node* unparenthesized(node* aNode)
        {
            while (aNode != nullptr && aNode->is(construct::Parenthesized) && aNode->size() == 1u)
                aNode = aNode->child(0u);
            return aNode;
        }

        // (a function's header (before its parameters) has a '*': a generator)
        bool is_generator(node const* aNode)
        {
            if (aNode->is(construct::GeneratorMethod) || aNode->is(construct::AsyncGeneratorMethod))
                return true;
            if (!aNode->is(construct::Function) && !aNode->is(construct::AsyncFunction) && !aNode->is(construct::FunctionExpression) &&
                !aNode->is(construct::AsyncFunctionExpression))
                return false;
            auto const* parameters = find_child(aNode, construct::Parameters);
            if (parameters == nullptr)
                return false;
            auto const header = aNode->source.substr(0u, static_cast<std::size_t>(parameters->source.data() - aNode->source.data()));
            auto const keyword = header.find("function");
            return keyword != std::string_view::npos && header.find('*', keyword + 8u) != std::string_view::npos;
        }

        bool is_async(node const* aNode)
        {
            return aNode->is(construct::AsyncFunction) || aNode->is(construct::AsyncFunctionExpression) || aNode->is(construct::AsyncArrow) ||
                aNode->is(construct::AsyncMethod) || aNode->is(construct::AsyncGeneratorMethod);
        }

        // (a binding pattern's bound names: its Binding nodes)
        void bound_names(node* aTarget, std::vector<node*>& aNames)
        {
            if (aTarget == nullptr)
                return;
            switch (aTarget->kind)
            {
            case construct::Binding:
                aNames.push_back(aTarget);
                break;
            case construct::Initializer:
            case construct::Rest:
                bound_names(aTarget->child(0u), aNames);
                break;
            case construct::ObjectPattern:
            case construct::ArrayPattern:
                for (auto* c : aTarget->children)
                    bound_names(c, aNames);
                break;
            case construct::PatternProperty:
                bound_names(aTarget->child(1u), aNames);
                break;
            default:
                break;
            }
        }

        // (VarDeclaredNames: the names var declarations within aNode declare (not within functions and classes))
        void var_names(node* aNode, std::vector<node*>& aNames)
        {
            if (aNode == nullptr || is_function_node(aNode) || is_class_node(aNode))
                return;
            if (aNode->is(construct::Var) || aNode->is(construct::ForVar))
            {
                for (auto* d : aNode->children)
                    bound_names(d->is(construct::Declarator) ? d->child(0u) : d, aNames);
                return;
            }
            for (auto* c : aNode->children)
                var_names(c, aNames);
        }

        // a lexically declared name
        struct lexical_name
        {
            node* binding;
            bool function;      ///< (a plain function declaration (Annex B: block-level functions can be redeclared in non-strict code))
        };

        // (a statement list item's lexically declared names and (a top-level function declaration) var declared names)
        void declared_names(node* aItem, bool aTopLevel, std::vector<lexical_name>& aLexical, std::vector<node*>& aVar)
        {
            while (aItem->is(construct::Labelled) && aItem->size() == 2u)
                aItem = aItem->child(1u);
            if (aItem->is(construct::Export) && aItem->size() == 1u)
                aItem = aItem->child(0u);   // (a module's exported declaration)
            switch (aItem->kind)
            {
            case construct::Import:
                {
                    // (a module's imported bindings)
                    std::vector<node*> pending{ aItem };
                    while (!pending.empty())
                    {
                        auto* n = pending.back();
                        pending.pop_back();
                        if (n->is(construct::Binding))
                            aLexical.push_back({ n, false });
                        else
                            pending.insert(pending.end(), n->children.rbegin(), n->children.rend());
                    }
                }
                break;
            case construct::Let:
            case construct::Const:
            case construct::Using:
            case construct::AwaitUsing:
                for (auto* d : aItem->children)
                {
                    std::vector<node*> names;
                    bound_names(d->is(construct::Declarator) ? d->child(0u) : d, names);
                    for (auto* n : names)
                        aLexical.push_back({ n, false });
                }
                break;
            case construct::Class:
                if (aItem->size() > 0u && aItem->child(0u)->is(construct::Binding))
                    aLexical.push_back({ aItem->child(0u), false });
                break;
            case construct::Function:
            case construct::AsyncFunction:
                if (aItem->size() > 0u && aItem->child(0u)->is(construct::Binding))
                {
                    if (aTopLevel)
                        aVar.push_back(aItem->child(0u));
                    else
                        aLexical.push_back({ aItem->child(0u), aItem->is(construct::Function) && !is_generator(aItem) });
                }
                break;
            default:
                break;
            }
        }

        // --- the checker

        struct label_set
        {
            std::string name;
            bool iteration = false;
            label_set const* next = nullptr;
        };

        struct class_scope
        {
            std::set<std::string> names;
            class_scope const* outer = nullptr;
        };

        struct context
        {
            bool strict = false;
            bool module = false;            ///< (a module's code: await is reserved)
            bool function = false;          ///< (a function's code: return is allowed)
            bool yield = false;             ///< (yield is a keyword: a generator's code)
            bool await = false;             ///< (await is a keyword: an async function's code)
            bool staticBlock = false;       ///< (a class static block's code (or its arrow functions' parameters): await is reserved)
            bool noArguments = false;       ///< (a field initializer's or static block's code: arguments is not allowed)
            bool parameters = false;        ///< (formal parameters: yield and await expressions are not allowed)
            bool newTarget = false;
            bool superProperty = false;
            bool superCall = false;
            bool breakable = false;
            bool continuable = false;
            label_set const* labels = nullptr;
            class_scope const* classes = nullptr;
        };

        class checker
        {
        public:
            checker(compiler const& aCompiler) :
                iCompiler{ aCompiler }, iSource{ aCompiler.fragment_source() }
            {
            }
        public:
            void script(node* aScript)
            {
                // (the parser stops at the first item it cannot match: the script must be the whole source)
                {
                    bool lineTerminator = false;
                    std::size_t p = 0u;
                    if (aScript->source.empty() || !within_source(aScript))
                    {
                        if (iSource.starts_with("#!"))
                            while (p < iSource.size() && iSource[p] != '\n' && iSource[p] != '\r')
                                ++p;
                        p = next_token(iSource, p, lineTerminator);
                    }
                    else
                        p = next_token(iSource, end_of(aScript), lineTerminator);
                    if (p < iSource.size() && iSource.find_first_not_of('\0', p) != std::string_view::npos)   // (a host's terminating NULs: not tokens)
                    {
                        std::size_t length = 1u;
                        (void)code_point(iSource, p, length);
                        iCompiler.error(iSource.substr(p, length), "Unexpected token '" + std::string{ iSource.substr(p, length) } + "'");
                    }
                }
                context c;
                c.module = iCompiler.iKind == compiler::code_kind::Module;
                c.strict = c.module || directive_strict(aScript);
                c.await = c.module;     // (a module's top level: await expressions)
                if (iCompiler.iKind == compiler::code_kind::DirectEval)
                {
                    // (a direct eval's code: the context of the code that calls it (its private names are not known here))
                    auto const flags = iCompiler.iEvalFlags;
                    c.strict = c.strict || (flags & compiler::EvalStrict) != 0u;
                    c.newTarget = (flags & compiler::EvalFunction) != 0u;
                    c.superProperty = (flags & compiler::EvalMethod) != 0u;
                    c.superCall = (flags & compiler::EvalDerivedConstructor) != 0u;
                    c.noArguments = (flags & compiler::EvalFieldInitializer) != 0u;
                    iPrivateNamesUnknown = true;
                }
                // (a module's top-level function declarations are lexical declarations)
                declarations(aScript->children, !c.module, c.strict);
                for (auto* s : aScript->children)
                    walk(s, c);
                if (c.module)
                    module_items(aScript);
            }
        private:
            [[noreturn]] void error(node const* aNode, std::string const& aMessage) const
            {
                iCompiler.error(aNode != nullptr ? aNode->source : iSource, aMessage);
            }
            // (the offset of a node's end in the source)
            std::size_t end_of(node const* aNode) const
            {
                return static_cast<std::size_t>(aNode->source.data() + aNode->source.size() - iSource.data());
            }
            std::size_t begin_of(node const* aNode) const
            {
                return static_cast<std::size_t>(aNode->source.data() - iSource.data());
            }
            bool within_source(node const* aNode) const
            {
                return aNode->source.data() >= iSource.data() && aNode->source.data() + aNode->source.size() <= iSource.data() + iSource.size();
            }
            // (the next token after a node: its first character (0: the end))
            char after(node const* aNode, bool* aLineTerminator = nullptr) const
            {
                bool lineTerminator = false;
                auto const p = next_token(iSource, end_of(aNode), lineTerminator);
                if (aLineTerminator != nullptr)
                    *aLineTerminator = lineTerminator;
                return p < iSource.size() ? iSource[p] : '\0';
            }
            static bool directive_strict(node const* aStatements)
            {
                for (auto* s : aStatements->children)
                {
                    if (!s->is(construct::ExpressionStatement) || s->size() != 1u || !s->child(0u)->is(construct::String))
                        return false;
                    auto const text = s->child(0u)->source;
                    if (text == "\"use strict\"" || text == "'use strict'")
                        return true;
                }
                return false;
            }
            std::string operator_of(node const* aAssignment) const
            {
                // (an assignment's operator: the text between its target and its value)
                if (aAssignment->size() < 2u || !within_source(aAssignment))
                    return "=";
                bool lineTerminator = false;
                auto p = next_token(iSource, end_of(aAssignment->child(0u)), lineTerminator);
                std::string result;
                while (p < iSource.size() && std::string_view{ "=*/%+-<>&^|?" }.find(iSource[p]) != std::string_view::npos)
                    result.push_back(iSource[p++]);
                return result;
            }
        private:
            // --- modules

            // (a module export name or import attribute key: its value)
            static std::string export_name(node const* aName)
            {
                if (aName->is(construct::String) && aName->source.size() >= 2u)
                    return string_value(aName->source.substr(1u, aName->source.size() - 2u));
                return name_of(aName);
            }

            void import_attributes(node const* aAttributes)
            {
                std::set<std::string> keys;
                for (std::size_t i = 0u; i < aAttributes->size(); i += 2u)
                    if (!keys.insert(export_name(aAttributes->child(i))).second)
                        error(aAttributes->child(i), "Import attribute has duplicate key '" + export_name(aAttributes->child(i)) + "'");
            }

            void module_items(node* aModule)
            {
                // (its top-level declarations)
                std::set<std::string> declared;
                {
                    std::vector<lexical_name> lexical;
                    std::vector<node*> vars;
                    for (auto* item : aModule->children)
                    {
                        declared_names(item, false, lexical, vars);
                        var_names(item, vars);
                    }
                    for (auto const& l : lexical)
                        declared.insert(name_of(l.binding));
                    for (auto* v : vars)
                        declared.insert(name_of(v));
                }
                // (its exported names: not duplicated; its exported bindings: declared)
                std::set<std::string> exported;
                auto const add = [&](node* aSite, std::string const& aName)
                {
                    if (!exported.insert(aName).second)
                        error(aSite, "Duplicate export of '" + aName + "'");
                };
                for (auto* item : aModule->children)
                {
                    if (item->is(construct::Import))
                    {
                        semicolon(item);
                        for (auto* c : item->children)
                        {
                            if (c->is(construct::ImportAttributes))
                                import_attributes(c);
                            if (c->is(construct::ImportNamed))
                                for (auto* s : c->children)
                                    if (s->size() > 0u && s->child(0u)->is(construct::String) && !well_formed(s->child(0u)))
                                        error(s->child(0u), "Invalid module export name: contains unpaired surrogate");
                        }
                        continue;
                    }
                    if (!item->is(construct::Export))
                        continue;
                    auto const text = item->source.substr(6u);
                    bool const isDefault = text.find_first_not_of(" \t\r\n") != std::string_view::npos &&
                        text.substr(text.find_first_not_of(" \t\r\n")).starts_with("default");
                    node* specifier = nullptr;
                    node* named = nullptr;
                    std::vector<node*> strings;
                    for (auto* c : item->children)
                    {
                        if (c->is(construct::String))
                            strings.push_back(c);
                        else if (c->is(construct::ExportNamed))
                            named = c;
                        else if (c->is(construct::ImportAttributes))
                            import_attributes(c);
                    }
                    if (!strings.empty())
                        specifier = strings.back();
                    if (isDefault)
                    {
                        add(item, "default");
                        if (item->size() > 0u && item->child(0u)->is(construct::ExportDefault))
                            semicolon(item->child(0u));
                        continue;
                    }
                    if (named != nullptr)
                    {
                        for (auto* s : named->children)
                        {
                            if (s->size() == 0u)
                                continue;
                            auto* local = s->child(0u);
                            auto* name = s->children.back();
                            for (auto* n : s->children)
                                if (n->is(construct::String) && !well_formed(n))
                                    error(n, "Invalid module export name: contains unpaired surrogate");
                            add(name, export_name(name));
                            if (specifier == nullptr)
                            {
                                if (local->is(construct::String))
                                    error(local, "A string literal cannot be used as an exported binding without 'from'");
                                if (!declared.contains(name_of(local)))
                                    error(local, "Export '" + name_of(local) + "' is not defined in module");
                            }
                        }
                        semicolon(item);
                        continue;
                    }
                    if (specifier != nullptr)
                    {
                        // (export * [as name] from ...)
                        node* name = nullptr;
                        for (auto* c : item->children)
                            if ((c->is(construct::Name) || c->is(construct::String)) && c != specifier)
                            {
                                name = c;
                                break;
                            }
                        if (name != nullptr)
                        {
                            if (name->is(construct::String) && !well_formed(name))
                                error(name, "Invalid module export name: contains unpaired surrogate");
                            add(name, export_name(name));
                        }
                        semicolon(item);
                        continue;
                    }
                    // (an exported declaration)
                    std::vector<lexical_name> lexical;
                    std::vector<node*> vars;
                    declared_names(item, false, lexical, vars);
                    var_names(item, vars);
                    for (auto const& l : lexical)
                        add(l.binding, name_of(l.binding));
                    for (auto* v : vars)
                        add(v, name_of(v));
                }
            }

            // (a string literal's value is well-formed Unicode (no unpaired surrogates))
            static bool well_formed(node const* aString)
            {
                auto const body = aString->source.substr(1u, aString->source.size() - 2u);
                bool high = false;
                for (std::size_t i = 0u; i < body.size(); ++i)
                {
                    std::uint32_t unit = 0x110000u;    // (not a surrogate)
                    if (body[i] == '\\' && i + 1u < body.size() && body[i + 1u] == 'u')
                    {
                        std::size_t j = i + 2u;
                        std::uint32_t value = 0u;
                        if (j < body.size() && body[j] == '{')
                        {
                            ++j;
                            while (j < body.size() && is_hex_digit(body[j]))
                                value = value * 16u + hex_value(body[j++]);
                        }
                        else
                            for (std::size_t k = 0u; k < 4u && j < body.size() && is_hex_digit(body[j]); ++k)
                                value = value * 16u + hex_value(body[j++]);
                        unit = value;
                        i = j - 1u;
                    }
                    else if (body[i] == '\\')
                        ++i;
                    bool const isHigh = unit >= 0xD800u && unit <= 0xDBFFu;
                    bool const isLow = unit >= 0xDC00u && unit <= 0xDFFFu;
                    if (isLow && !high)
                        return false;
                    if (high && !isLow)
                        return false;
                    high = isHigh;
                }
                return !high;
            }

            // --- declarations

            void declarations(std::vector<node*> const& aItems, bool aTopLevel, bool aStrict, std::vector<node*> const* aParameters = nullptr)
            {
                std::vector<lexical_name> lexical;
                std::vector<node*> vars;
                for (auto* item : aItems)
                {
                    declared_names(item, aTopLevel, lexical, vars);
                    var_names(item, vars);
                }
                std::map<std::string, lexical_name const*> seen;
                for (auto const& l : lexical)
                {
                    auto const name = name_of(l.binding);
                    auto const existing = seen.find(name);
                    if (existing != seen.end())
                    {
                        if (!(!aStrict && !aTopLevel && l.function && existing->second->function))
                            error(l.binding, "Identifier '" + name + "' has already been declared");
                        continue;
                    }
                    seen.emplace(name, &l);
                }
                if (seen.empty())
                    return;
                for (auto* v : vars)
                    if (seen.contains(name_of(v)))
                        error(v, "Identifier '" + name_of(v) + "' has already been declared");
                if (aParameters != nullptr)
                    for (auto* p : *aParameters)
                        if (seen.contains(name_of(p)))
                        {
                            auto const* l = seen.find(name_of(p))->second;
                            error(l->binding, "Identifier '" + name_of(p) + "' has already been declared");
                        }
            }
            void lexical_bindings(node* aDeclaration, std::vector<node*> const& aNames, node* aBody)
            {
                // (a for statement's lexical declaration: its names are not let, not duplicated and not var declared in its body)
                std::set<std::string> names;
                for (auto* n : aNames)
                {
                    auto const name = name_of(n);
                    if (name == "let")
                        error(n, "let is disallowed as a lexically bound name");
                    if (!names.insert(name).second)
                        error(n, "Identifier '" + name + "' has already been declared");
                }
                (void)aDeclaration;
                if (aBody == nullptr)
                    return;
                std::vector<node*> vars;
                var_names(aBody, vars);
                for (auto* v : vars)
                    if (names.contains(name_of(v)))
                        error(v, "Identifier '" + name_of(v) + "' has already been declared");
            }

            // --- names

            void identifier(node* aNode, context const& c, bool aBinding)
            {
                auto const name = name_of(aNode);
                bool const escaped = aNode->source.find('\\') != std::string_view::npos;
                if (!valid_name(name, escaped))
                    error(aNode, "Invalid Unicode escape sequence");
                if (escaped && is_reserved_word(name))
                    error(aNode, "Keyword must not contain escaped characters");
                if (c.strict && is_strict_reserved_word(name))
                    error(aNode, "Unexpected strict mode reserved word");
                if (name == "yield" && (c.yield || c.strict))
                    error(aNode, "Unexpected identifier 'yield'");
                if (name == "await" && (c.await || c.staticBlock || c.module))
                    error(aNode, "Unexpected identifier 'await'");
                if (aBinding && c.strict && (name == "eval" || name == "arguments"))
                    error(aNode, "Unexpected eval or arguments in strict mode");
                if (!aBinding && name == "arguments" && c.noArguments && aNode->is(construct::Identifier))
                    error(aNode, "'arguments' is not allowed in class field initializer or static initialization block");
            }

            // --- statements and expressions

            void walk(node* aNode, context const& c)
            {
                if (aNode == nullptr)
                    return;
                switch (aNode->kind)
                {
                case construct::Identifier:
                    identifier(aNode, c, false);
                    return;
                case construct::Binding:
                    identifier(aNode, c, true);
                    return;
                case construct::Label:
                    identifier(aNode, c, false);
                    return;
                case construct::Name:
                    if (!valid_name(name_of(aNode), aNode->source.find('\\') != std::string_view::npos))
                        error(aNode, "Invalid Unicode escape sequence");
                    return;
                case construct::Private:
                    {
                        auto const name = name_of(aNode);
                        if (!valid_name(name.substr(1u), aNode->source.find('\\') != std::string_view::npos))
                            error(aNode, "Invalid Unicode escape sequence");
                        bool declared = false;
                        for (auto const* s = c.classes; s != nullptr && !declared; s = s->outer)
                            declared = s->names.contains(name);
                        if (!declared && !iPrivateNamesUnknown)
                            error(aNode, "Private field '" + name + "' must be declared in an enclosing class");
                    }
                    return;
                case construct::Function:
                case construct::AsyncFunction:
                case construct::FunctionExpression:
                case construct::AsyncFunctionExpression:
                case construct::Arrow:
                case construct::AsyncArrow:
                case construct::Method:
                case construct::Getter:
                case construct::Setter:
                case construct::GeneratorMethod:
                case construct::AsyncMethod:
                case construct::AsyncGeneratorMethod:
                    function(aNode, c);
                    return;
                case construct::Class:
                case construct::ClassExpression:
                    class_(aNode, c);
                    return;
                case construct::Number:
                    number(aNode, c);
                    return;
                case construct::String:
                    string(aNode, c);
                    return;
                case construct::Template:
                    template_(aNode, c, aNode->parent != nullptr && aNode->parent->is(construct::Tagged));
                    return;
                case construct::RegExp:
                    regexp(aNode);
                    return;
                case construct::Block:
                    declarations(aNode->children, false, c.strict);
                    break;
                case construct::Switch:
                    {
                        std::vector<node*> items;
                        for (std::size_t i = 1u; i < aNode->size(); ++i)
                            for (auto* s : aNode->child(i)->children)
                                if (!(aNode->child(i)->is(construct::Case) && s == aNode->child(i)->child(0u)))
                                    items.push_back(s);
                        declarations(items, false, c.strict);
                        std::size_t defaults = 0u;
                        for (std::size_t i = 1u; i < aNode->size(); ++i)
                            if (aNode->child(i)->is(construct::Default) && ++defaults > 1u)
                                error(aNode->child(i), "More than one default clause in switch statement");
                        walk(aNode->child(0u), c);
                        context inner = c;
                        inner.breakable = true;
                        for (std::size_t i = 1u; i < aNode->size(); ++i)
                            for (auto* s : aNode->child(i)->children)
                                walk(s, inner);
                    }
                    return;
                case construct::Labelled:
                    labelled(aNode, c);
                    return;
                case construct::Break:
                case construct::Continue:
                    jump(aNode, c);
                    return;
                case construct::Return:
                    if (!c.function)
                        error(aNode, "Illegal return statement");
                    semicolon(aNode);
                    break;
                case construct::Throw:
                case construct::Debugger:
                    semicolon(aNode);
                    break;
                case construct::With:
                    if (c.strict)
                        error(aNode, "Strict mode code may not include a with statement");
                    walk(aNode->child(0u), c);
                    statement_body(aNode->child(1u), c);
                    return;
                case construct::If:
                    walk(aNode->child(0u), c);
                    for (std::size_t i = 1u; i < aNode->size(); ++i)
                        statement_body(aNode->child(i), c, true);
                    return;
                case construct::Do:
                case construct::While:
                case construct::For:
                case construct::ForIn:
                case construct::ForOf:
                case construct::ForAwaitOf:
                    loop(aNode, c);
                    return;
                case construct::Catch:
                    catch_(aNode, c);
                    return;
                case construct::ExpressionStatement:
                    expression_statement(aNode, c);
                    break;
                case construct::Var:
                case construct::Let:
                case construct::Const:
                case construct::Using:
                case construct::AwaitUsing:
                    declaration(aNode, c);
                    break;
                case construct::Yield:
                    yield(aNode, c);
                    break;
                case construct::Await:
                    await(aNode, c);
                    break;
                case construct::NewTarget:
                    if (!c.newTarget)
                        error(aNode, "new.target expression is not allowed here");
                    return;
                case construct::SuperDot:
                case construct::SuperIndex:
                    if (!c.superProperty)
                        error(aNode, "'super' keyword unexpected here");
                    break;
                case construct::SuperCall:
                    if (!c.superCall)
                        error(aNode, "'super' keyword unexpected here");
                    break;
                case construct::Delete:
                    {
                        auto* operand = unparenthesized(aNode->child(0u));
                        if (operand != nullptr && operand->is(construct::Identifier) && c.strict)
                            error(aNode, "Delete of an unqualified identifier in strict mode.");
                        if (operand != nullptr && (operand->is(construct::DotPrivate) ||
                            (operand->is(construct::Optional) && operand->size() > 0u && (operand->child(0u)->is(construct::OptionalPrivate) || operand->child(0u)->is(construct::DotPrivate)))))
                            error(aNode, "Private fields can not be deleted");
                    }
                    break;
                case construct::Assign:
                    assignment(aNode, c);
                    break;
                case construct::PreIncrement:
                case construct::PreDecrement:
                case construct::PostIncrement:
                case construct::PostDecrement:
                    if (!simple_target(aNode->child(0u), c, false))
                        error(aNode->child(0u), std::string{ "Invalid left-hand side expression in " } +
                            (aNode->is(construct::PostIncrement) || aNode->is(construct::PostDecrement) ? "postfix" : "prefix") + " operation");
                    break;
                case construct::Object:
                    if (!iPatterns.contains(aNode))
                        object_literal(aNode);
                    break;
                case construct::Array:
                    break;
                case construct::ArrayPattern:
                case construct::ObjectPattern:
                    // (a rest element is the last, without a trailing comma)
                    for (std::size_t i = 0u; i < aNode->size(); ++i)
                        if (aNode->child(i)->is(construct::Rest) && (i + 1u != aNode->size() || after(aNode->child(i)) == ','))
                            error(aNode->child(i), "Rest element must be last element");
                    break;
                case construct::Tagged:
                    {
                        // (a tagged template in an optional chain)
                        for (auto* t = aNode->child(0u); t != nullptr && t->size() > 0u; t = t->child(0u))
                        {
                            if (t->is(construct::OptionalDot) || t->is(construct::OptionalIndex) || t->is(construct::OptionalCall) || t->is(construct::OptionalPrivate))
                                error(aNode, "Invalid tagged template on optional chain");
                            if (!(t->is(construct::Dot) || t->is(construct::Index) || t->is(construct::Call) || t->is(construct::Tagged) || t->is(construct::DotPrivate)))
                                break;
                        }
                    }
                    break;
                case construct::Field:
                    semicolon(aNode);
                    break;
                default:
                    break;
                }
                for (auto* child : aNode->children)
                    walk(child, c);
            }

            // (a statement in a position where a statement (not a declaration) is expected)
            void statement_body(node* aNode, context const& c, bool aIf = false)
            {
                if (aNode == nullptr)
                    return;
                if (aNode->is(construct::ExpressionStatement) && aNode->size() > 0u)
                {
                    auto const text = aNode->source;
                    auto const keyword = [&](std::string_view aKeyword)
                    {
                        return text.starts_with(aKeyword) && (text.size() == aKeyword.size() || !is_identifier_character(static_cast<unsigned char>(text[aKeyword.size()])));
                    };
                    if (keyword("function"))
                    {
                        // (Annex B: a function declaration as an if statement's clause in non-strict code)
                        auto* f = aNode->child(0u);
                        if (!(aIf && !c.strict && f->is(construct::FunctionExpression) && !is_generator(f)))
                            error(aNode, "In strict mode code, functions can only be declared at top level or inside a block.");
                    }
                }
                if (aNode->is(construct::Function) || aNode->is(construct::AsyncFunction) || aNode->is(construct::Class) || aNode->is(construct::Let) ||
                    aNode->is(construct::Const) || aNode->is(construct::Using) || aNode->is(construct::AwaitUsing))
                    error(aNode, "Lexical declaration cannot appear in a single-statement context");
                walk(aNode, c);
            }

            void expression_statement(node* aNode, context const& c)
            {
                (void)c;
                semicolon(aNode);
                // (an expression statement cannot begin with '{', function, async function, class or let [)
                auto const text = aNode->source;
                auto const keyword = [&](std::string_view aText, std::string_view aKeyword, std::size_t& aEnd)
                {
                    aEnd = aKeyword.size();
                    return aText.starts_with(aKeyword) && (aText.size() == aKeyword.size() || !is_identifier_character(static_cast<unsigned char>(aText[aKeyword.size()])));
                };
                std::size_t end = 0u;
                if (text.starts_with("{"))
                    error(aNode, "Unexpected token '{'");
                if (keyword(text, "class", end))
                    error(aNode, "Unexpected token 'class'");
                if (keyword(text, "function", end))
                {
                    if (aNode->parent == nullptr || is_statement_list(aNode->parent) || aNode->parent->is(construct::Labelled))
                        error(aNode, "Unexpected token 'function'");
                }
                if (keyword(text, "let", end))
                {
                    bool lineTerminator = false;
                    auto const p = next_token(iSource, begin_of(aNode) + end, lineTerminator);
                    if (p < iSource.size() && iSource[p] == '[')
                        error(aNode, "Unexpected token 'let ['");
                }
                if (keyword(text, "async", end))
                {
                    bool lineTerminator = false;
                    auto const p = next_token(iSource, begin_of(aNode) + end, lineTerminator);
                    if (!lineTerminator && iSource.substr(p).starts_with("function") &&
                        (p + 8u >= iSource.size() || !is_identifier_character(static_cast<unsigned char>(iSource[p + 8u]))))
                        error(aNode, "Async functions can only be declared at the top level or inside a block.");
                }
            }

            // (automatic semicolon insertion: a statement that does not end with a semicolon must be followed by a line terminator, '}'
            // or the end of the source)
            void semicolon(node* aNode)
            {
                if (!within_source(aNode) || aNode->source.ends_with(";"))
                    return;
                if (aNode->is(construct::ExpressionStatement) && aNode->size() == 1u && aNode->child(0u)->is(construct::FunctionExpression) &&
                    aNode->source.starts_with("function"))
                    return;     // (Annex B: a function declaration as an if statement's clause (not an expression))
                if (aNode->parent != nullptr && is_for(aNode->parent) && aNode != aNode->parent->children.back())
                    return;     // (a for statement's head)
                if (aNode->parent != nullptr && aNode->parent->is(construct::Do) && aNode == aNode->parent->child(0u))
                {
                    // (do statement's body: followed by while)
                }
                bool lineTerminator = false;
                auto const next = after(aNode, &lineTerminator);
                if (lineTerminator || next == '}' || next == '\0')
                    return;
                if (next == ')' && aNode->parent != nullptr && aNode->parent->is(construct::Do))
                    return;
                error(aNode, "Unexpected token: missing ';' before '" + std::string(1u, next) + "'");
            }

            void declaration(node* aNode, context const& c)
            {
                bool const head = aNode->parent != nullptr && is_for(aNode->parent) && aNode != aNode->parent->children.back();
                if (!head)
                    semicolon(aNode);
                bool const lexical = !aNode->is(construct::Var);
                bool const usingDeclaration = aNode->is(construct::Using) || aNode->is(construct::AwaitUsing);
                if (usingDeclaration && !head && (aNode->parent == nullptr || (aNode->parent->is(construct::Script) && !c.module) ||
                    aNode->parent->is(construct::Case) || aNode->parent->is(construct::Default)))
                    error(aNode, "Using declaration cannot appear in the top level of a script or a switch case");
                if (aNode->is(construct::AwaitUsing) && (!c.await || c.parameters))
                    error(aNode, "await using is only valid in async functions");
                for (auto* d : aNode->children)
                {
                    if (!d->is(construct::Declarator))
                        continue;
                    auto* target = d->child(0u);
                    std::vector<node*> names;
                    bound_names(target, names);
                    if (lexical)
                        for (auto* n : names)
                            if (name_of(n) == "let")
                                error(n, "let is disallowed as a lexically bound name");
                    if (usingDeclaration && !target->is(construct::Binding))
                        error(target, "Using declarations may not have binding patterns");
                    if (d->size() == 1u)
                    {
                        if (target->is(construct::ObjectPattern) || target->is(construct::ArrayPattern))
                            error(d, "Missing initializer in destructuring declaration");
                        if (aNode->is(construct::Const) || usingDeclaration)
                            error(d, "Missing initializer in const declaration");
                    }
                }
                if (lexical && !head)
                {
                    // (a lexical declaration's names are not duplicated (a block's declarations are checked by its block)
                    std::vector<node*> names;
                    for (auto* d : aNode->children)
                        bound_names(d->is(construct::Declarator) ? d->child(0u) : d, names);
                    (void)names;
                }
            }

            void labelled(node* aNode, context const& c)
            {
                auto* label = aNode->child(0u);
                identifier(label, c, false);
                auto const name = name_of(label);
                for (auto const* l = c.labels; l != nullptr; l = l->next)
                    if (l->name == name)
                        error(label, "Label '" + name + "' has already been declared");
                auto* body = aNode->child(1u);
                auto* item = body;
                while (item != nullptr && item->is(construct::Labelled))
                    item = item->child(1u);
                label_set entry{ name, item != nullptr && is_iteration(item), c.labels };
                context inner = c;
                inner.labels = &entry;
                if (body != nullptr && (body->is(construct::Function) || body->is(construct::AsyncFunction)))
                {
                    // (a labelled function declaration: only in non-strict code, where a statement list item is allowed)
                    auto* outer = aNode;
                    while (outer->parent != nullptr && outer->parent->is(construct::Labelled))
                        outer = outer->parent;
                    if (c.strict)
                        error(body, "In strict mode code, functions can only be declared at top level or inside a block.");
                    if (outer->parent == nullptr || !is_statement_list(outer->parent))
                        error(body, "Labelled function declaration not allowed as the body of a control flow structure");
                    if (body->is(construct::AsyncFunction) || is_generator(body))
                        error(body, "Generators and async functions cannot be labelled");
                    walk(body, inner);
                }
                else
                    statement_body(body, inner);
            }

            void jump(node* aNode, context const& c)
            {
                semicolon(aNode);
                bool const isContinue = aNode->is(construct::Continue);
                auto* label = find_child(aNode, construct::Label);
                if (label != nullptr)
                {
                    identifier(label, c, false);
                    auto const name = name_of(label);
                    for (auto const* l = c.labels; l != nullptr; l = l->next)
                        if (l->name == name)
                        {
                            if (isContinue && !l->iteration)
                                error(aNode, "Illegal continue statement: '" + name + "' does not denote an iteration statement");
                            return;
                        }
                    error(aNode, "Undefined label '" + name + "'");
                }
                if (isContinue ? !c.continuable : !c.breakable)
                    error(aNode, isContinue ? "Illegal continue statement: no surrounding iteration statement" : "Illegal break statement");
            }

            void loop(node* aNode, context const& c)
            {
                context inner = c;
                inner.breakable = true;
                inner.continuable = true;
                // (the labels of the labelled statements whose item is this loop are its iteration labels (already so marked))
                auto* body = aNode->is(construct::Do) ? aNode->child(0u) : aNode->children.back();
                if (aNode->is(construct::Do))
                {
                    statement_body(body, inner);
                    for (std::size_t i = 1u; i < aNode->size(); ++i)
                        walk(aNode->child(i), c);
                    return;
                }
                if (aNode->is(construct::While))
                {
                    walk(aNode->child(0u), c);
                    statement_body(body, inner);
                    return;
                }
                auto* head = aNode->child(0u);
                if (aNode->is(construct::For))
                {
                    if (head != nullptr && head != body && (head->is(construct::Let) || head->is(construct::Const) || head->is(construct::Using) ||
                        head->is(construct::AwaitUsing)))
                    {
                        std::vector<node*> names;
                        for (auto* d : head->children)
                            bound_names(d->is(construct::Declarator) ? d->child(0u) : d, names);
                        lexical_bindings(head, names, body);
                    }
                    if (head != nullptr && head != body && (head->is(construct::ForInit) || head->is(construct::Var) || head->is(construct::Let) ||
                        head->is(construct::Const) || head->is(construct::Using) || head->is(construct::AwaitUsing)) && contains_in(head))
                        error(head, "Unexpected 'in' in the initialization of a for statement");
                    if (head != nullptr && head->is(construct::ForInit) && head->size() > 0u)
                    {
                        auto const text = head->source;
                        if (text.starts_with("let") && !(text.size() > 3u && is_identifier_character(static_cast<unsigned char>(text[3u]))))
                        {
                            bool lineTerminator = false;
                            auto const p = next_token(iSource, begin_of(head) + 3u, lineTerminator);
                            if (p < iSource.size() && iSource[p] == '[')
                                error(head, "Unexpected token 'let ['");
                        }
                    }
                }
                else
                {
                    // (for-in/of: the head's declaration or target)
                    if (head->is(construct::ForLet) || head->is(construct::ForConst) || head->is(construct::ForUsing) || head->is(construct::ForAwaitUsing))
                    {
                        std::vector<node*> names;
                        bound_names(head->child(0u), names);
                        lexical_bindings(head, names, body);
                        if ((head->is(construct::ForUsing) || head->is(construct::ForAwaitUsing)) && !head->child(0u)->is(construct::Binding))
                            error(head, "Using declarations may not have binding patterns");
                        if ((head->is(construct::ForUsing) || head->is(construct::ForAwaitUsing)) && aNode->is(construct::ForIn))
                            error(head, "Using declarations are not allowed in for-in statements");
                        if (head->is(construct::ForAwaitUsing) && !c.await)
                            error(head, "await using is only valid in async functions");
                    }
                    else if (head->is(construct::ForVar))
                    {
                        if (head->size() > 0u && head->child(0u)->is(construct::Initializer) &&
                            (!aNode->is(construct::ForIn) || c.strict || !head->child(0u)->child(0u)->is(construct::Binding)))
                            error(head, "for-in/of loop variable declaration may not have an initializer.");
                    }
                    else
                    {
                        // (a left-hand side expression: an assignment target)
                        auto const text = head->source;
                        bool const startsLet = text.starts_with("let") && !(text.size() > 3u && is_identifier_character(static_cast<unsigned char>(text[3u])));
                        if (startsLet && !aNode->is(construct::ForIn))
                            error(head, "The left-hand side of a for-of loop may not be 'let'.");
                        if (startsLet)
                        {
                            bool lineTerminator = false;
                            auto const p = next_token(iSource, begin_of(head) + 3u, lineTerminator);
                            if (p < iSource.size() && iSource[p] == '[')
                                error(head, "Unexpected token 'let ['");
                        }
                        if (aNode->is(construct::ForOf) && head->is(construct::Identifier) && head->source == "async")
                            error(head, "The left-hand side of a for-of loop may not be 'async'.");
                        destructuring_target(head, c, "Invalid left-hand side in for-loop");
                    }
                }
                for (auto* child : aNode->children)
                {
                    if (child == body)
                        statement_body(child, inner);
                    else
                        walk(child, c);
                }
            }

            // (a for statement's initialization: an in expression that is not within brackets, parentheses or a function (the grammar's
            // yield expressions do not exclude it))
            static bool contains_in(node const* aNode)
            {
                if (aNode->is(construct::In) || aNode->is(construct::PrivateIn))
                    return true;
                if (is_function_node(aNode) || is_class_node(aNode))
                    return false;
                switch (aNode->kind)
                {
                case construct::Parenthesized:
                case construct::Array:
                case construct::Object:
                case construct::Template:
                case construct::Computed:
                case construct::Arguments:
                    return false;
                case construct::Index:
                case construct::Call:
                case construct::OptionalCall:
                case construct::OptionalIndex:
                case construct::Tagged:
                case construct::New:
                    return aNode->size() > 0u && contains_in(aNode->child(0u));
                case construct::Conditional:
                    // (its consequent allows in)
                    return (aNode->size() > 0u && contains_in(aNode->child(0u))) || (aNode->size() > 2u && contains_in(aNode->child(2u)));
                default:
                    break;
                }
                for (auto const* c : aNode->children)
                    if (contains_in(c))
                        return true;
                return false;
            }

            void catch_(node* aNode, context const& c)
            {
                auto* block = aNode->children.back();
                if (aNode->size() == 2u)
                {
                    auto* parameter = aNode->child(0u);
                    std::vector<node*> names;
                    bound_names(parameter, names);
                    std::set<std::string> set;
                    for (auto* n : names)
                        if (!set.insert(name_of(n)).second)
                            error(n, "Identifier '" + name_of(n) + "' has already been declared");
                    std::vector<lexical_name> lexical;
                    std::vector<node*> vars;
                    for (auto* s : block->children)
                    {
                        declared_names(s, false, lexical, vars);
                        var_names(s, vars);
                    }
                    for (auto const& l : lexical)
                        if (set.contains(name_of(l.binding)))
                            error(l.binding, "Identifier '" + name_of(l.binding) + "' has already been declared");
                    if (!parameter->is(construct::Binding))
                        for (auto* v : vars)
                            if (set.contains(name_of(v)))
                                error(v, "Identifier '" + name_of(v) + "' has already been declared");
                    walk(parameter, c);
                }
                walk(block, c);
            }

            void yield(node* aNode, context const& c)
            {
                if (c.yield)
                {
                    if (c.parameters)
                        error(aNode, "Yield expression not allowed in formal parameter");
                    return;
                }
                // (yield: an identifier)
                if (c.strict)
                    error(aNode, "Unexpected strict mode reserved word 'yield'");
                if (aNode->size() > 0u)
                {
                    bool lineTerminator = false;
                    auto const p = next_token(iSource, begin_of(aNode) + 5u, lineTerminator);
                    if (p < iSource.size() && iSource[p] != '*' && iSource[p] != '(' && iSource[p] != '[' && iSource[p] != '`' && iSource[p] != '/' &&
                        iSource[p] != '+' && iSource[p] != '-')
                        error(aNode, "Unexpected identifier after 'yield'");
                    // ('yield * x = 1': a multiplication is not an assignment target)
                    if (p < iSource.size() && iSource[p] == '*' && aNode->child(0u)->is(construct::Assign))
                        error(aNode, "Invalid left-hand side in assignment");
                }
            }

            void await(node* aNode, context const& c)
            {
                if (c.await)
                {
                    if (c.parameters)
                        error(aNode, "Illegal await-expression in formal parameters of async function");
                    return;
                }
                if (c.staticBlock)
                    error(aNode, "Unexpected 'await' in class static block");
                if (c.module)
                    error(aNode, "await is only valid in async functions and the top level bodies of modules");
                // (await: an identifier)
                bool lineTerminator = false;
                auto const p = next_token(iSource, begin_of(aNode) + 5u, lineTerminator);
                if (!lineTerminator && p < iSource.size() && iSource[p] != '(' && iSource[p] != '[' && iSource[p] != '`' && iSource[p] != '/' &&
                    iSource[p] != '+' && iSource[p] != '-' && iSource[p] != '*')
                    error(aNode, "await is only valid in async functions and the top level bodies of modules");
            }

            // --- assignment targets

            bool simple_target(node* aTarget, context const& c, bool aLogical)
            {
                auto* t = unparenthesized(aTarget);
                if (t == nullptr)
                    return false;
                switch (t->kind)
                {
                case construct::Identifier:
                    {
                        auto const name = name_of(t);
                        if (c.strict && (name == "eval" || name == "arguments"))
                            error(t, "Unexpected eval or arguments in strict mode");
                    }
                    return true;
                case construct::Dot:
                case construct::Index:
                case construct::DotPrivate:
                case construct::SuperDot:
                case construct::SuperIndex:
                    return true;
                case construct::Call:
                    return !c.strict && !aLogical;  // (Annex B: a runtime ReferenceError in non-strict code)
                default:
                    return false;
                }
            }

            void assignment(node* aNode, context const& c)
            {
                auto const oper = operator_of(aNode);
                auto* target = aNode->child(0u);
                if (oper == "=")
                    destructuring_target(target, c, "Invalid left-hand side in assignment");
                else if (!simple_target(target, c, oper == "&&=" || oper == "||=" || oper == "?\?="))
                    error(target, "Invalid left-hand side in assignment");
            }

            void destructuring_target(node* aTarget, context const& c, char const* aMessage)
            {
                if (aTarget->is(construct::Object) || aTarget->is(construct::Array))
                    assignment_pattern(aTarget, c);
                else if (!simple_target(aTarget, c, false))
                    error(aTarget, aMessage);
            }

            void assignment_pattern(node* aPattern, context const& c)
            {
                iPatterns.insert(aPattern);
                for (std::size_t i = 0u; i < aPattern->size(); ++i)
                {
                    auto* e = aPattern->child(i);
                    bool const last = i + 1u == aPattern->size();
                    switch (e->kind)
                    {
                    case construct::Hole:
                        break;
                    case construct::Spread:
                        {
                            if (!last || after(e) == ',')
                                error(e, "Rest element must be last element");
                            auto* rest = e->child(0u);
                            if (rest->is(construct::Assign))
                                error(rest, "Invalid rest element");
                            if (aPattern->is(construct::Object))
                            {
                                if (!simple_target(rest, c, false))
                                    error(rest, "`...` must be followed by an assignable reference in assignment contexts");
                            }
                            else
                                destructuring_target(rest, c, "Invalid destructuring assignment target");
                        }
                        break;
                    case construct::Property:
                        assignment_element(e->child(1u), c);
                        break;
                    case construct::Shorthand:
                    case construct::ShorthandDefault:
                        if (!simple_target(e->child(0u), c, false))
                            error(e, "Invalid destructuring assignment target");
                        break;
                    default:
                        if (aPattern->is(construct::Object))
                            error(e, "Invalid destructuring assignment target");
                        assignment_element(e, c);
                        break;
                    }
                }
            }

            void assignment_element(node* aElement, context const& c)
            {
                if (aElement->is(construct::Assign) && operator_of(aElement) == "=")
                    destructuring_target(aElement->child(0u), c, "Invalid destructuring assignment target");
                else
                    destructuring_target(aElement, c, "Invalid destructuring assignment target");
            }

            void object_literal(node* aNode)
            {
                bool proto = false;
                for (auto* p : aNode->children)
                {
                    if (p->is(construct::ShorthandDefault))
                        error(p, "Invalid shorthand property initializer");
                    if (p->size() > 0u && p->child(0u)->is(construct::Private))
                        error(p, "Private names are only allowed in classes");
                    if (p->is(construct::Property) && p->size() == 2u)
                    {
                        auto* key = p->child(0u);
                        bool isProto = false;
                        if (key->is(construct::Name))
                            isProto = name_of(key) == "__proto__";
                        else if (key->is(construct::String) && key->source.size() >= 2u)
                            isProto = string_value(key->source.substr(1u, key->source.size() - 2u)) == "__proto__";
                        if (isProto)
                        {
                            if (proto)
                                error(p, "Duplicate __proto__ fields are not allowed in object literals");
                            proto = true;
                        }
                    }
                }
            }

            // --- literals

            void number(node* aNode, context const& c)
            {
                auto const text = aNode->source;
                if (text.size() >= 2u && text[0] == '0' && ((text[1] >= '0' && text[1] <= '9') || text[1] == '_'))
                {
                    if (text.find('_') != std::string_view::npos)
                        error(aNode, "Numeric separators are not allowed in legacy octal-like literals");
                    if (c.strict)
                        error(aNode, "Octal literals are not allowed in strict mode.");
                }
                if (!within_source(aNode))
                    return;
                auto const end = end_of(aNode);
                if (end < iSource.size())
                {
                    std::size_t length = 1u;
                    auto const next = code_point(iSource, end, length);
                    if (is_identifier_character(next))
                        error(aNode, "Invalid or unexpected token");
                }
            }

            void string(node* aNode, context const& c)
            {
                auto const text = aNode->source;
                if (text.size() < 2u)
                    return;
                switch (check_escapes(text.substr(1u, text.size() - 2u), false))
                {
                case escape_error::Invalid:
                    error(aNode, "Invalid escape sequence");
                case escape_error::LegacyOctal:
                    if (c.strict)
                        error(aNode, "Octal escape sequences are not allowed in strict mode.");
                    break;
                case escape_error::NonOctalDecimal:
                    if (c.strict)
                        error(aNode, "\\8 and \\9 are not allowed in strict mode.");
                    break;
                default:
                    break;
                }
            }

            void template_(node* aNode, context const& c, bool aTagged)
            {
                for (auto* part : aNode->children)
                {
                    if (part->is(construct::TemplateString))
                    {
                        if (!aTagged && check_escapes(part->source, true) != escape_error::None)
                            error(part, "Invalid escape sequence in template");
                    }
                    else
                        walk(part, c);
                }
            }

            void regexp(node* aNode)
            {
                auto const text = aNode->source;
                auto const end = text.rfind('/');
                if (end == std::string_view::npos || end == 0u)
                    return;
                if (text.find("\xE2\x80\xA8") != std::string_view::npos || text.find("\xE2\x80\xA9") != std::string_view::npos)
                    error(aNode, "Invalid regular expression: missing /");
                auto const flags = text.substr(end + 1u);
                std::string seen;
                for (auto f : flags)
                {
                    if (std::string_view{ "dgimsuvy" }.find(f) == std::string_view::npos)
                        error(aNode, "Invalid regular expression flags");
                    if (seen.find(f) != std::string::npos)
                        error(aNode, "Invalid regular expression flags");
                    seen.push_back(f);
                }
                if (seen.find('u') != std::string::npos && seen.find('v') != std::string::npos)
                    error(aNode, "Invalid regular expression flags");
            }

            // --- functions

            void function(node* aNode, context const& c)
            {
                bool const arrow = aNode->is(construct::Arrow) || aNode->is(construct::AsyncArrow);
                bool const method = is_method_node(aNode);
                bool const generator = is_generator(aNode);
                bool const async = is_async(aNode);
                bool const declaration = aNode->is(construct::Function) || aNode->is(construct::AsyncFunction);
                node* name = nullptr;
                node* parameters = nullptr;
                node* body = nullptr;
                for (auto* child : aNode->children)
                {
                    if (child->is(construct::Parameters))
                        parameters = child;
                    else if (child->is(construct::Body))
                        body = child;
                    else if (child->is(construct::Binding) && parameters == nullptr && name == nullptr && !arrow)
                        name = child;
                }
                if (arrow && body == nullptr && aNode->size() >= 2u)
                    body = aNode->children.back();      // (a concise body)
                bool const useStrict = body != nullptr && body->is(construct::Body) && directive_strict(body);
                bool const strict = c.strict || useStrict;
                // (its parameters)
                std::vector<node*> names;
                bool simple = true;
                if (parameters != nullptr)
                    for (auto* p : parameters->children)
                    {
                        if (!p->is(construct::Binding))
                            simple = false;
                        bound_names(p, names);
                    }
                if (useStrict && !simple)
                    error(aNode, "Illegal 'use strict' directive in function with non-simple parameter list");
                if (strict || arrow || method || !simple)
                {
                    std::set<std::string> set;
                    for (auto* n : names)
                        if (!set.insert(name_of(n)).second)
                            error(n, "Duplicate parameter name not allowed in this context");
                }
                if (parameters != nullptr)
                {
                    if (aNode->is(construct::Getter) && parameters->size() != 0u)
                        error(parameters, "Getter must not have any formal parameters.");
                    if (aNode->is(construct::Setter) && (parameters->size() != 1u || parameters->child(0u)->is(construct::Rest)))
                        error(parameters, "Setter must have exactly one formal parameter.");
                    if (aNode->is(construct::Setter) && parameters->size() == 1u && after(parameters->child(0u)) == ',')
                        error(parameters, "Setter must have exactly one formal parameter.");
                    // (a rest parameter is the last, without a trailing comma)
                    for (std::size_t i = 0u; i < parameters->size(); ++i)
                        if (parameters->child(i)->is(construct::Rest) && (i + 1u != parameters->size() || after(parameters->child(i)) == ','))
                            error(parameters->child(i), "Rest parameter must be last formal parameter");
                }
                // (its code's context)
                context body_context;
                body_context.strict = strict;
                body_context.module = c.module;
                body_context.function = true;
                body_context.yield = generator;
                body_context.await = async;
                body_context.classes = c.classes;
                body_context.noArguments = arrow ? c.noArguments : false;
                body_context.newTarget = arrow ? c.newTarget : true;
                body_context.superProperty = arrow ? c.superProperty : (method || iMethodContext.contains(aNode));
                body_context.superCall = arrow ? c.superCall : iDerivedConstructors.contains(aNode);
                context parameter_context = body_context;
                parameter_context.parameters = true;
                if (arrow)
                {
                    parameter_context.yield = c.yield;
                    parameter_context.await = c.await || async;
                    parameter_context.staticBlock = c.staticBlock;
                }
                // (its name)
                if (name != nullptr)
                {
                    context name_context = body_context;
                    if (declaration)
                    {
                        name_context.yield = c.yield;
                        name_context.await = c.await;
                        name_context.staticBlock = c.staticBlock;
                    }
                    identifier(name, name_context, true);
                }
                // (a method's key)
                if (method && aNode->size() > 0u)
                {
                    auto* key = aNode->child(0u);
                    if (!key->is(construct::Parameters))
                        walk(key, c);
                }
                if (parameters != nullptr)
                    for (auto* p : parameters->children)
                        walk(p, parameter_context);
                if (body != nullptr && body->is(construct::Body))
                {
                    declarations(body->children, true, strict, &names);
                    for (auto* s : body->children)
                        walk(s, body_context);
                }
                else if (body != nullptr)
                    walk(body, body_context);
            }

            // --- classes

            static std::optional<std::string> property_name(node const* aKey)
            {
                if (aKey == nullptr)
                    return {};
                if (aKey->is(construct::Name))
                    return name_of(aKey);
                if (aKey->is(construct::String) && aKey->source.size() >= 2u)
                    return string_value(aKey->source.substr(1u, aKey->source.size() - 2u));
                return {};
            }

            void class_(node* aNode, context const& c)
            {
                context inner = c;
                inner.strict = true;
                node* name = (aNode->size() > 0u && aNode->child(0u)->is(construct::Binding)) ? aNode->child(0u) : nullptr;
                if (name != nullptr)
                {
                    identifier(name, inner, true);
                    if (name_of(name) == "let")
                        error(name, "let is disallowed as a lexically bound name");
                }
                bool const derived = find_child(aNode, construct::Extends) != nullptr;
                // (its private names)
                class_scope scope;
                scope.outer = c.classes;
                struct private_entry
                {
                    int getter = 0;
                    int setter = 0;
                    int other = 0;
                    bool isStatic = false;
                    bool staticMismatch = false;
                };
                std::map<std::string, private_entry> privates;
                int constructors = 0;
                for (auto* element : aNode->children)
                {
                    if (element == name || element->is(construct::Extends))
                        continue;
                    bool const isStatic = element->is(construct::Static);
                    auto* e = isStatic ? element->child(0u) : element;
                    if (e == nullptr || e->is(construct::StaticBlock) || e->size() == 0u)
                        continue;
                    auto* key = e->child(0u);
                    if (key->is(construct::Private))
                    {
                        auto const privateName = name_of(key);
                        if (privateName == "#constructor")
                            error(key, "Classes may not have a private field named '#constructor'");
                        auto& entry = privates[privateName];
                        bool const first = entry.getter + entry.setter + entry.other == 0;
                        if (!first && entry.isStatic != isStatic)
                            entry.staticMismatch = true;
                        entry.isStatic = isStatic;
                        if (e->is(construct::Getter))
                            ++entry.getter;
                        else if (e->is(construct::Setter))
                            ++entry.setter;
                        else
                            ++entry.other;
                        if (entry.other > 0 ? entry.getter + entry.setter + entry.other > 1 : (entry.getter > 1 || entry.setter > 1 || entry.staticMismatch))
                            error(key, "Identifier '" + privateName + "' has already been declared");
                        scope.names.insert(privateName);
                    }
                    auto const propertyName = property_name(key);
                    if (is_method_node(e))
                    {
                        iMethodContext.insert(e);
                        if (!isStatic && propertyName == "constructor")
                        {
                            if (!e->is(construct::Method))
                                error(key, "Class constructor may not be a" + std::string{ e->is(construct::Getter) ? " getter" : e->is(construct::Setter) ? " setter" :
                                    e->is(construct::GeneratorMethod) ? " generator" : "n async method" });
                            if (++constructors > 1)
                                error(e, "A class may only have one constructor");
                            if (derived)
                                iDerivedConstructors.insert(e);
                        }
                        if (isStatic && propertyName == "prototype")
                            error(key, "Classes may not have a static property named 'prototype'");
                    }
                    else if (e->is(construct::Field))
                    {
                        if (propertyName == "constructor")
                            error(key, "Classes may not have a field named 'constructor'");
                        if (isStatic && propertyName == "prototype")
                            error(key, "Classes may not have a static property named 'prototype'");
                    }
                }
                // (its heritage: in the outer private environment)
                if (auto* heritage = find_child(aNode, construct::Extends))
                    walk(heritage, inner);
                context body = inner;
                body.classes = &scope;
                for (auto* element : aNode->children)
                {
                    if (element == name || element->is(construct::Extends))
                        continue;
                    auto* e = element->is(construct::Static) ? element->child(0u) : element;
                    if (e == nullptr)
                        continue;
                    if (e->is(construct::Field))
                    {
                        semicolon(e);
                        auto* key = e->child(0u);
                        walk(key, body);
                        if (e->size() > 1u)
                        {
                            context initializer = body;
                            initializer.function = false;
                            initializer.yield = false;
                            initializer.await = false;
                            initializer.staticBlock = false;
                            initializer.parameters = false;
                            initializer.noArguments = true;
                            initializer.newTarget = true;
                            initializer.superProperty = true;
                            initializer.superCall = false;
                            initializer.breakable = false;
                            initializer.continuable = false;
                            initializer.labels = nullptr;
                            for (std::size_t i = 1u; i < e->size(); ++i)
                                walk(e->child(i), initializer);
                        }
                    }
                    else if (e->is(construct::StaticBlock))
                    {
                        context block = body;
                        block.function = false;
                        block.yield = false;
                        block.await = false;
                        block.staticBlock = true;
                        block.parameters = false;
                        block.noArguments = true;
                        block.newTarget = true;
                        block.superProperty = true;
                        block.superCall = false;
                        block.breakable = false;
                        block.continuable = false;
                        block.labels = nullptr;
                        declarations(e->children, true, true);
                        for (auto* s : e->children)
                            walk(s, block);
                    }
                    else
                        walk(e, body);
                }
            }
        private:
            compiler const& iCompiler;
            std::string_view iSource;
            std::set<node const*> iPatterns;            ///< (object and array literals that are assignment patterns)
            std::set<node const*> iMethodContext;       ///< (class methods (super properties are allowed))
            std::set<node const*> iDerivedConstructors; ///< (derived classes' constructors (super calls are allowed))
            bool iPrivateNamesUnknown = false;          ///< (a direct eval's code: the private names of the classes that enclose it are not known)
        };
    }

    void compiler::check_early_errors(node* aScript)
    {
        checker{ *this }.script(aScript);
    }
}
