/*
  closures.cpp

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

// Nested functions, closures and decorators: a rewrite of a module's source (after its classes have been rewritten)
//
// - A function's variables that functions nested in it use (its cells, as CPython's) are cell objects: "x" is "_k_x.v"
//   in it and in the nested functions (which receive the cells as leading parameters).
// - A nested function is defined at the end of the module ("def _nN_name(_k_a, ..., params)") and where it was defined
//   is a closure ("name = __closure(_nN_name, 'name', 'qualname', defaults, _k_a, ...)") that passes the cells to it
//   (and that has its default values: those of the function at the end of the module are None).
// - A lambda that uses cells (or that has default values) is a closure of a lambda that receives them.
// - A decorated function is a closure that its decorators are applied to ("name = d(__closure(_dN_name, ...))").
// - nonlocal statements are pass statements (the cells are the variables that they declare).
// - A generator function's body is a function of its own (_gN_name, at the end of the module) and the function returns
//   "__generator(_gN_name, 'name', 'qualname', cells..., arguments...)" (its yields are __yield(value) and 
//   __yield_from(iterable)); an async def is "def" whose function returns __coroutine(...) (or __async_generator(...) if 
//   it yields) instead (its awaits are __await(awaitable): see statements.cpp).

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include "tokens.hpp"

namespace neos::compilers::python
{
    namespace
    {
        using namespace tokens;

        struct scope
        {
            enum kind_t { Module, Function, Lambda } kind = Module;
            scope* parent = nullptr;
            std::vector<scope*> children;
            std::string name;
            std::string qualname;
            // (a def)
            std::size_t header = 0u;                    ///< (its line)
            std::vector<std::size_t> decorators;        ///< (lines)
            std::size_t nameToken = 0u;
            std::size_t openParen = 0u;
            std::size_t closeParen = 0u;
            std::size_t colon = 0u;                     ///< (tokens of its header (or a lambda's line))
            bool inlineBody = false;
            std::size_t bodyEnd = 0u;                   ///< (the line after its body)
            std::string hoisted;                        ///< (the name of the function at the end of the module)
            std::string typeParameters;                 ///< (a def's (def f[T, *Ts](...)): their bindings ("(T := __type_param('T', 0)), ..."))
            // (a lambda: its tokens in its line)
            std::size_t line = 0u;
            std::size_t first = 0u;                     ///< (lambda)
            std::size_t last = 0u;                      ///< (the token after its body)
            // (parameters: name, the tokens of its default value (if any))
            struct parameter
            {
                enum kind_t { Positional, VarArgs, VarKeywords, KeywordOnly } kind = Positional;
                std::string name;
                std::size_t token = 0u;
                std::optional<std::pair<std::size_t, std::size_t>> defaultValue;
                std::optional<std::pair<std::size_t, std::size_t>> annotation;     ///< (its tokens: after its colon)
            };
            std::optional<std::pair<std::size_t, std::size_t>> returnAnnotation;   ///< (a def's: after its arrow)
            bool generator = false;                     ///< (a def whose body yields, or an async def: its body is a function of its own)
            bool yields = false;                        ///< (a def whose body yields)
            bool isAsync = false;                       ///< (an async def: a coroutine function (an asynchronous generator function if it yields))
            bool classBody = false;                     ///< (the function of a class's body (_bN_name, see statements.cpp))
            std::string className;
            std::string body;                           ///< (a generator's: the function of its body)
            std::vector<parameter> parameters;
            std::set<std::string> bound;
            std::set<std::string> globals;
            std::set<std::string> nonlocals;
            std::set<std::string> referenced;
            std::set<std::string> cells;
            std::set<std::string> frees;
            bool local(std::string const& aName) const
            {
                if (globals.contains(aName) || nonlocals.contains(aName))
                    return false;
                if (bound.contains(aName))
                    return true;
                for (auto const& p : parameters)
                    if (p.name == aName)
                        return true;
                return false;
            }
        };

        class rewriter
        {
        public:
            using annotations_t = std::map<std::string, std::vector<std::pair<std::string, std::string>>>;
            rewriter(std::string const& aText, annotations_t* aAnnotations = nullptr) : iText{ aText }, iLines{ tokenize(aText) }, iAnnotations{ aAnnotations }
            {
                // (from __future__ import annotations: annotations are their sources)
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                    if (is_name(l, 0u, "from") && is_name(l, 1u, "__future__"))
                        for (std::size_t t = 2u; t < iLines[l].tokens.size(); ++t)
                            if (is_name(l, t, "annotations"))
                                iStringAnnotations = true;
                iModule.kind = scope::Module;
                iOwners.resize(iLines.size());
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                    iOwners[l].assign(iLines[l].tokens.size(), &iModule);
                iBinding.resize(iLines.size());
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                    iBinding[l].assign(iLines[l].tokens.size(), false);
                iSkip.resize(iLines.size());
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                    iSkip[l].assign(iLines[l].tokens.size(), false);
                method_names();
            }
        public:
            // (a module's names (those bound at its top level, or declared global): renamed aPrefix + name, as the variables of
            // the program that they are (aNames: the module's names (not those of the compiler's rewrites); aAll: its __all__ 
            // (if it is a list or tuple of strings)))
            std::string rename_module(std::string const& aPrefix, std::vector<std::string>& aNames, std::optional<std::vector<std::string>>& aAll,
                std::set<std::string>& aDefined)
            {
                find_functions();
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                    find_lambdas(l, 0u, iLines[l].tokens.size());
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                    analyze_line(l);
                redefined();
                static std::set<std::string_view> const sSpecial = { "__name__", "__file__", "__doc__", "__builtins__", "__spec__", "__loader__",
                    "__package__", "__path__", "__debug__", "__cached__" };
                std::set<std::string> names = iModule.bound;
                for (auto const& s : iScopes)
                    names.insert(s->globals.begin(), s->globals.end());
                for (auto const& special : sSpecial)
                    names.erase(std::string{ special });
                // (the names of the compiler's rewrites' functions and variables (e.g. _n1_f, _e2_))
                auto const synthetic = [](std::string const& aName)
                {
                    if (aName.size() < 4u || aName[0] != '_' || std::string_view{ "ndgablemcq" }.find(aName[1]) == std::string_view::npos ||
                        !std::isdigit(static_cast<unsigned char>(aName[2])))
                        return false;
                    auto const underscore = aName.find('_', 2u);
                    return underscore != std::string::npos && std::all_of(aName.begin() + 2, aName.begin() + static_cast<std::ptrdiff_t>(underscore),
                        [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
                };
                for (auto const& n : names)
                    if (!synthetic(n))
                        aNames.push_back(n);
                // (the module's functions that its defs define (that no other statements bind): they cannot be rebound)
                for (auto const& s : iScopes)
                    if (s->kind == scope::Function && s->parent == &iModule && !s->classBody && !conditional(*s))
                        aDefined.insert(s->name);
                std::vector<std::pair<std::size_t, std::size_t>> renamed;    // (text positions)
                std::map<std::string, std::size_t> firstModule, firstAny;    // (the names' first positions: in the module's statements, and anywhere)
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                {
                    auto const& line = iLines[l];
                    bool const global = is_name(l, 0u, "global");
                    for (std::size_t t = 0u; t < line.tokens.size(); ++t)
                    {
                        auto const& k = line.tokens[t];
                        if (k.kind != token::Name)
                            continue;
                        std::string const name{ text(k) };
                        if (!names.contains(name) || keywords().contains(name))
                            continue;
                        if (!(global && t > 0u))
                        {
                            if (iSkip[l][t] && !iBinding[l][t])
                                continue;
                            if (t > 0u && is_op(l, t - 1u, "."))
                                continue;
                            // (the scope whose name it is)
                            bool module = true;
                            for (auto* s = iOwners[l][t]; s != nullptr && s->kind != scope::Module; s = s->parent)
                            {
                                if (s->globals.contains(name))
                                    break;
                                if (s->local(name))
                                {
                                    module = false;
                                    break;
                                }
                            }
                            if (!module)
                                continue;
                        }
                        renamed.emplace_back(k.begin, k.end);
                        firstAny.emplace(name, k.begin);
                        if (iOwners[l][t] == &iModule)
                            firstModule.emplace(name, k.begin);
                    }
                    // (__all__ = [...] or (...))
                    if (line.indent == 0u && line.tokens.size() >= 3u && is_name(l, 0u, "__all__") && is_op(l, 1u, "="))
                    {
                        std::vector<std::string> all;
                        bool literal = true;
                        for (std::size_t t = 2u; t < line.tokens.size(); ++t)
                        {
                            auto const& k = line.tokens[t];
                            if (k.kind == token::String)
                            {
                                auto const value = text(k);
                                auto const quote = value.find_first_of("'\"");
                                if (quote == std::string_view::npos || value.size() < quote + 2u || value.find_first_of("fFbB") < quote)
                                {
                                    literal = false;
                                    break;
                                }
                                all.emplace_back(value.substr(quote + 1u, value.size() - quote - 2u));
                            }
                            else if (k.kind != token::Op || (text(k) != "[" && text(k) != "]" && text(k) != "(" && text(k) != ")" && text(k) != ","))
                            {
                                literal = false;
                                break;
                            }
                        }
                        if (literal)
                            aAll = all;
                    }
                }
                // (the names in the order the module binds them (as its namespace's, a dict's, are): their first positions)
                auto const first = [&](std::string const& aName)
                {
                    auto const m = firstModule.find(aName);
                    if (m != firstModule.end())
                        return m->second;
                    auto const a = firstAny.find(aName);
                    return a != firstAny.end() ? a->second : iText.size();
                };
                std::stable_sort(aNames.begin(), aNames.end(), [&](std::string const& aLhs, std::string const& aRhs) { return first(aLhs) < first(aRhs); });
                std::string result;
                std::size_t position = 0u;
                for (auto const& r : renamed)
                {
                    result += iText.substr(position, r.first - position);
                    result += aPrefix;
                    result += iText.substr(r.first, r.second - r.first);
                    position = r.second;
                }
                result += iText.substr(position);
                return result;
            }
            std::string rewrite(bool& aChanged, std::vector<std::size_t>* aLines = nullptr)
            {
                find_functions();
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                    find_lambdas(l, 0u, iLines[l].tokens.size());
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                    analyze_line(l);
                redefined();
                resolve(iModule);
                // (the annotations of the defs that are not the grammar's types: removed (the module's functions': their sources are
                // its functions' __annotations__ (see aAnnotations); the others' are __annotate's))
                for (auto const& s : iScopes)
                {
                    if (s->kind != scope::Function || s->classBody)
                        continue;
                    auto const ranges = stripped_annotations(*s);
                    if (!ranges.empty())
                        iStripped[s->header].insert(iStripped[s->header].end(), ranges.begin(), ranges.end());
                    if (iAnnotations != nullptr && s->parent == &iModule && s->decorators.empty() && !conditional(*s))
                    {
                        std::vector<std::pair<std::string, std::string>> sources;
                        for (auto const& p : s->parameters)
                            if (p.annotation && p.annotation->first < p.annotation->second)
                            {
                                auto const source = source_of(s->header, p.annotation->first, p.annotation->second);
                                sources.emplace_back(p.name, iStringAnnotations ? quoted(source) : source);
                            }
                        if (s->returnAnnotation)
                        {
                            auto const source = source_of(s->header, s->returnAnnotation->first, s->returnAnnotation->second);
                            sources.emplace_back("return", iStringAnnotations ? quoted(source) : source);
                        }
                        if (!sources.empty())
                            (*iAnnotations)[s->name] = std::move(sources);
                    }
                }
                // (nothing to rewrite: no nested functions, decorators or lambdas that need closures)
                bool needed = !iStripped.empty();
                for (auto const& s : iScopes)
                    if (s->kind == scope::Function ? (s->parent != &iModule || !s->decorators.empty() || s->generator || s->classBody || conditional(*s)) : (!s->frees.empty() || has_defaults(*s) || s->generator))
                        needed = true;
                if (!needed)
                    return iText;
                aChanged = true;
                number_functions(iModule);
                iAddedLines.clear();
                std::string result = render(0u, iText.size(), &iModule, 0u);
                // (the lines of the result: the module's are its own (its functions' regions are blank lines), and a line that it 
                // added (the cells' initializers of a function of the module whose body starts with a compound statement) is the 
                // line after it; the functions at the end of the module are their definitions')
                if (aLines != nullptr)
                {
                    aLines->clear();
                    auto const lines = static_cast<std::size_t>(std::count(result.begin(), result.end(), '\n')) + 1u;
                    auto added = iAddedLines.begin();
                    for (std::size_t l = 0u; aLines->size() < lines;)
                    {
                        if (added != iAddedLines.end() && *added == l)
                        {
                            ++added;
                            aLines->push_back(l);
                        }
                        else
                            aLines->push_back(l++);
                    }
                }
                for (std::size_t i = 0u; i < iHoisted.size(); ++i)
                {
                    auto const& h = iHoisted[i];
                    if (!result.empty() && result.back() != '\n')
                        result += '\n';
                    else if (aLines != nullptr && !aLines->empty())
                        aLines->pop_back(); // (the empty line after the last newline: the function's first)
                    result += h;
                    if (aLines != nullptr)
                    {
                        aLines->insert(aLines->end(), iHoistedLines[i].begin(), iHoistedLines[i].end());
                        aLines->push_back(aLines->empty() ? 0u : aLines->back());
                    }
                }
                return result;
            }
        private:
            token const& tok(std::size_t aLine, std::size_t aToken) const { return iLines[aLine].tokens[aToken]; }
            std::string_view text(token const& aToken) const { return std::string_view{ iText }.substr(aToken.begin, aToken.end - aToken.begin); }
            std::string_view text(std::size_t aLine, std::size_t aToken) const { return text(tok(aLine, aToken)); }
            bool is_op(std::size_t aLine, std::size_t aToken, std::string_view aOp) const
            {
                return aToken < iLines[aLine].tokens.size() && tok(aLine, aToken).kind == token::Op && text(aLine, aToken) == aOp;
            }
            bool is_name(std::size_t aLine, std::size_t aToken, std::string_view aName) const
            {
                return aToken < iLines[aLine].tokens.size() && tok(aLine, aToken).kind == token::Name && text(aLine, aToken) == aName;
            }
            static bool opening(std::string_view aOp) { return aOp == "(" || aOp == "[" || aOp == "{"; }
            static bool closing(std::string_view aOp) { return aOp == ")" || aOp == "]" || aOp == "}"; }

            // (the matching closing bracket of the one at aToken)
            std::size_t matching(std::size_t aLine, std::size_t aToken) const
            {
                int depth = 0;
                auto const& tokens = iLines[aLine].tokens;
                for (auto t = aToken; t < tokens.size(); ++t)
                    if (tokens[t].kind == token::Op)
                    {
                        auto const op = text(tokens[t]);
                        if (opening(op))
                            ++depth;
                        else if (closing(op) && --depth == 0)
                            return t;
                    }
                return tokens.size();
            }

            // (a def of the module that is within a compound statement (e.g. if, try): its function is defined at the end of the 
            // module and its closure where it is (as a nested function's))
            // (a def whose body is the one statement aStatement (e.g. "pass"))
            bool body_is(scope const& aFunction, std::string_view aStatement) const
            {
                if (aFunction.inlineBody)
                    return aFunction.colon + 2u == iLines[aFunction.header].tokens.size() && text(aFunction.header, aFunction.colon + 1u) == aStatement;
                for (auto l = aFunction.header + 1u; l < aFunction.bodyEnd; ++l)
                    if (!iLines[l].tokens.empty())
                        return iLines[l].tokens.size() == 1u && text(l, 0u) == aStatement && aFunction.bodyEnd == l + 1u;
                return false;
            }

            // (a name that more than one of the module's defs define is rebound (by each of them); not a declaration's (a def whose 
            // body is "...": the builtins' overloads, imported from the runtime))
            void redefined()
            {
                std::map<std::string, std::size_t> defs;
                for (auto const& s : iScopes)
                    if (s->kind == scope::Function && s->parent == &iModule && !s->classBody && !body_is(*s, "...") && ++defs[s->name] == 2u)
                        iRebound.insert(s->name);
            }

            bool conditional(scope const& aFunction) const
            {
                if (aFunction.kind != scope::Function || aFunction.parent != &iModule || aFunction.classBody)
                    return false;
                auto const first = aFunction.decorators.empty() ? aFunction.header : aFunction.decorators.front();
                // (or whose name is bound (or deleted) by other statements of the module)
                return iLines[first].indent > 0u || iRebound.contains(aFunction.name);
            }

            // (an annotation that the grammar's types are (int, float, str, bool, None or a (dotted) name): its type)
            bool grammar_annotation(std::size_t aLine, std::pair<std::size_t, std::size_t> const& aRange) const
            {
                if (aRange.first >= aRange.second)
                    return false;
                for (auto t = aRange.first; t < aRange.second; ++t)
                {
                    bool const name = (t - aRange.first) % 2u == 0u;
                    if (name ? tok(aLine, t).kind != token::Name || (keywords().contains(text(aLine, t)) && text(aLine, t) != "None") : !is_op(aLine, t, "."))
                        return false;
                }
                return (aRange.second - aRange.first) % 2u == 1u;
            }
            // (the annotations of a def that are not the grammar's types: the tokens that they are (with their colons and arrow))
            std::vector<std::pair<std::size_t, std::size_t>> stripped_annotations(scope const& aFunction) const
            {
                std::vector<std::pair<std::size_t, std::size_t>> result;
                // (*args' and **kwargs' annotations are their items')
                for (auto const& p : aFunction.parameters)
                    if (p.annotation && (!grammar_annotation(aFunction.header, *p.annotation) || p.kind == scope::parameter::VarArgs || p.kind == scope::parameter::VarKeywords))
                        result.emplace_back(p.annotation->first - 1u, p.annotation->second);
                // (a def whose body is "pass" (or "...") returns None: its return annotation is not its type (a stub's, e.g. an overload's or 
                // a protocol's, "def f(x: int) -> int: ..."))
                if (aFunction.returnAnnotation && (!grammar_annotation(aFunction.header, *aFunction.returnAnnotation) || body_is(aFunction, "pass")))
                    result.emplace_back(aFunction.returnAnnotation->first - 1u, aFunction.returnAnnotation->second);
                return result;
            }
            // (an annotation's source as a string literal)
            static std::string quoted(std::string const& aSource)
            {
                std::string result = "'";
                for (auto c : aSource)
                {
                    if (c == '\'' || c == '\\')
                        result += '\\';
                    result += c;
                }
                return result + "'";
            }
            std::string annotation_value(std::size_t aLine, std::pair<std::size_t, std::size_t> const& aRange, scope const* aScope)
            {
                if (iStringAnnotations)
                    return quoted(source_of(aLine, aRange.first, aRange.second));
                return "(" + render_tokens(aLine, aRange.first, aRange.second, aScope) + ")";
            }
            // (the default value of a parameter of a function at the end of the module (its closure's are its values): a value of 
            // its type (if it is one of the grammar's types), or None)
            std::string placeholder(std::size_t aLine, scope::parameter const& aParameter) const
            {
                if (aParameter.annotation && aParameter.annotation->second == aParameter.annotation->first + 1u && 
                    aParameter.kind != scope::parameter::VarArgs && aParameter.kind != scope::parameter::VarKeywords)
                {
                    auto const type = text(aLine, aParameter.annotation->first);
                    return type == "int" ? "0" : type == "float" ? "0.0" : type == "str" ? "''" : type == "bool" ? "False" : "None";
                }
                return "None";
            }
            // (a def's annotations: the source of a dict of them (empty: none))
            std::string annotations_dict(scope const& aFunction)
            {
                iAnnotating = true;
                struct done { bool& flag; ~done() { flag = false; } } const finished{ iAnnotating };
                // (a function that makes them (evaluated when __annotations__ is first read), and their sources (if it raises))
                std::string entries;
                std::string sources;
                auto const l = aFunction.header;
                for (auto const& p : aFunction.parameters)
                    if (p.annotation && p.annotation->first < p.annotation->second)
                    {
                        entries += (entries.empty() ? "" : ", ") + ("'" + p.name + "': " + annotation_value(l, *p.annotation, aFunction.parent));
                        sources += "'" + p.name + "', " + quoted(source_of(l, p.annotation->first, p.annotation->second)) + ", ";
                    }
                if (aFunction.returnAnnotation)
                {
                    entries += (entries.empty() ? "" : ", ") + ("'return': " + annotation_value(l, *aFunction.returnAnnotation, aFunction.parent));
                    sources += "'return', " + quoted(source_of(l, aFunction.returnAnnotation->first, aFunction.returnAnnotation->second)) + ", ";
                }
                // (a nested function's: evaluated where it is defined (as CPython's), in its scope (whose variables they may be))
                if (entries.empty())
                    return {};
                return (aFunction.parent != &iModule ? "{" + entries + "}" : "lambda: {" + entries + "}") + ", (" + sources + ")";
            }

            std::string source_of(std::size_t aLine, std::size_t aBegin, std::size_t aEnd) const
            {
                return iText.substr(tok(aLine, aBegin).begin, tok(aLine, aEnd - 1u).end - tok(aLine, aBegin).begin);
            }

            // (the classes' methods (_cN_class_method): their qualified names (class.method))
            void method_names()
            {
                std::size_t at = 0u;
                for (;;)
                {
                    auto const c = iText.find("__class('", at);
                    if (c == std::string::npos)
                        break;
                    auto const nameEnd = iText.find('\'', c + 9u);
                    auto const lineEnd = iText.find('\n', c);
                    if (nameEnd == std::string::npos)
                        break;
                    auto const className = iText.substr(c + 9u, nameEnd - c - 9u);
                    std::string_view const line = std::string_view{ iText }.substr(nameEnd + 1u, (lineEnd == std::string::npos ? iText.size() : lineEnd) - nameEnd - 1u);
                    // ('method': ... _cN_class_method ...)
                    std::size_t p = 0u;
                    std::string key;
                    while (p < line.size())
                    {
                        if (line[p] == '\'' && p + 1u < line.size())
                        {
                            auto const close = line.find('\'', p + 1u);
                            if (close == std::string_view::npos)
                                break;
                            auto const k = line.substr(p + 1u, close - p - 1u);
                            if (close + 1u < line.size() && line[close + 1u] == ':')
                                key = std::string{ k };
                            p = close + 1u;
                            continue;
                        }
                        if (line.compare(p, 2u, "_c") == 0 && (p == 0u || !name_char(line[p - 1u])))
                        {
                            auto e = p;
                            while (e < line.size() && name_char(line[e]))
                                ++e;
                            if (!key.empty())
                                iMethods[std::string{ line.substr(p, e - p) }] = className + "." + key;
                            p = e;
                            continue;
                        }
                        ++p;
                    }
                    at = nameEnd;
                }
            }

            // --- the scopes

            scope& new_scope(scope::kind_t aKind, scope& aParent)
            {
                iScopes.push_back(std::make_unique<scope>());
                auto& s = *iScopes.back();
                s.kind = aKind;
                s.parent = &aParent;
                aParent.children.push_back(&s);
                return s;
            }

            // (the parameters of a def's or lambda's tokens [aBegin, aEnd))
            void parameters(scope& aScope, std::size_t aLine, std::size_t aBegin, std::size_t aEnd)
            {
                std::size_t t = aBegin;
                bool keywordOnly = false;
                while (t < aEnd)
                {
                    // (one parameter: up to a comma at its level)
                    auto e = t;
                    int depth = 0;
                    while (e < aEnd)
                    {
                        auto const& k = tok(aLine, e);
                        if (k.kind == token::Op)
                        {
                            auto const op = text(k);
                            if (opening(op))
                                ++depth;
                            else if (closing(op))
                                --depth;
                            else if (op == "," && depth == 0)
                                break;
                        }
                        ++e;
                    }
                    auto p = t;
                    while (p < e && (is_op(aLine, p, "*") || is_op(aLine, p, "**")))
                        ++p;
                    if (p == t + 1u && p == e && is_op(aLine, t, "*"))
                        keywordOnly = true;
                    if (p < e && tok(aLine, p).kind == token::Name)
                    {
                        scope::parameter parameter;
                        parameter.kind = p > t ? (is_op(aLine, t, "**") ? scope::parameter::VarKeywords : scope::parameter::VarArgs) :
                            keywordOnly ? scope::parameter::KeywordOnly : scope::parameter::Positional;
                        if (parameter.kind == scope::parameter::VarArgs)
                            keywordOnly = true;
                        parameter.name = std::string{ text(aLine, p) };
                        parameter.token = p;
                        iOwners[aLine][p] = &aScope;
                        iSkip[aLine][p] = true;
                        int d = 0;
                        for (auto q = p + 1u; q < e; ++q)
                        {
                            if (tok(aLine, q).kind == token::Op)
                            {
                                if (opening(text(aLine, q))) ++d;
                                else if (closing(text(aLine, q))) --d;
                            }
                            if (d == 0 && is_op(aLine, q, "="))
                            {
                                parameter.defaultValue = std::make_pair(q + 1u, e);
                                break;
                            }
                        }
                        if (is_op(aLine, p + 1u, ":"))
                            parameter.annotation = std::make_pair(p + 2u, parameter.defaultValue ? parameter.defaultValue->first - 1u : e);
                        aScope.parameters.push_back(std::move(parameter));
                    }
                    t = e + 1u;
                }
            }

            void find_functions()
            {
                // (the open functions, innermost last)
                std::vector<scope*> open;
                std::vector<std::size_t> decorators;
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                {
                    auto const& line = iLines[l];
                    while (!open.empty() && line.indent <= iLines[open.back()->header].indent)
                    {
                        open.back()->bodyEnd = l;
                        open.pop_back();
                    }
                    auto& parent = open.empty() ? iModule : *open.back();
                    for (std::size_t t = 0u; t < line.tokens.size(); ++t)
                        iOwners[l][t] = &parent;
                    if (is_op(l, 0u, "@"))
                    {
                        decorators.push_back(l);
                        continue;
                    }
                    std::size_t d = 0u;
                    if (is_name(l, 0u, "async"))
                        d = 1u;
                    if (!is_name(l, d, "def") || d + 1u >= line.tokens.size())
                    {
                        decorators.clear();
                        continue;
                    }
                    auto& f = new_scope(scope::Function, parent);
                    f.header = l;
                    f.isAsync = d == 1u;
                    f.generator = f.isAsync;
                    f.nameToken = d + 1u;
                    f.name = std::string{ text(l, d + 1u) };
                    // (a class's body: _bN_name)
                    if (f.name.size() > 3u && f.name.starts_with("_b") && std::isdigit(static_cast<unsigned char>(f.name[2])))
                    {
                        auto const underscore = f.name.find('_', 2u);
                        if (underscore != std::string::npos && std::all_of(f.name.begin() + 2, f.name.begin() + static_cast<std::ptrdiff_t>(underscore),
                            [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
                        {
                            f.classBody = true;
                            f.className = f.name.substr(underscore + 1u);
                        }
                    }
                    for (auto x : decorators)
                        if (iLines[x].indent == line.indent)
                            f.decorators.push_back(x);
                    decorators.clear();
                    parent.bound.insert(f.classBody ? f.className : f.name);
                    iBinding[l][d + 1u] = true;
                    f.openParen = d + 2u;
                    // (its type parameters: bound where it is defined (its closure is of them: see render_function))
                    if (is_op(l, f.openParen, "["))
                    {
                        auto const close = matching(l, f.openParen);
                        int kind = 0;
                        for (auto t = f.openParen + 1u; t < close; ++t)
                        {
                            if (is_op(l, t, "*") || is_op(l, t, "**"))
                                kind = is_op(l, t, "*") ? 1 : 2;
                            else if (tok(l, t).kind == token::Name && (is_op(l, t - 1u, "[") || is_op(l, t - 1u, ",") || is_op(l, t - 1u, "*") || is_op(l, t - 1u, "**")))
                            {
                                std::string const name{ text(l, t) };
                                f.typeParameters += "(" + name + " := __type_param('" + name + "', " + std::to_string(kind) + ")), ";
                                parent.bound.insert(name);
                                if (&parent == &iModule)
                                    iRebound.insert(name);
                                iBinding[l][t] = true;
                                kind = 0;
                            }
                            else if (is_op(l, t, ","))
                                kind = 0;
                        }
                        if (&parent == &iModule)
                            iRebound.insert(f.name);
                        f.openParen = close + 1u;
                    }
                    f.closeParen = matching(l, f.openParen);
                    parameters(f, l, f.openParen + 1u, f.closeParen);
                    f.colon = f.closeParen + 1u;
                    while (f.colon < line.tokens.size() && !is_op(l, f.colon, ":"))
                        ++f.colon;
                    if (is_op(l, f.closeParen + 1u, "->") && f.closeParen + 2u < f.colon)
                        f.returnAnnotation = std::make_pair(f.closeParen + 2u, f.colon);
                    f.inlineBody = f.colon + 1u < line.tokens.size();
                    for (auto t = f.colon + 1u; t < line.tokens.size(); ++t)
                        iOwners[l][t] = &f;
                    if (f.inlineBody)
                        f.bodyEnd = l + 1u;
                    else
                        open.push_back(&f);
                }
                for (auto f : open)
                    f->bodyEnd = iLines.size();
                for (auto const& s : iScopes)
                {
                    auto& f = *s;
                    if (f.kind != scope::Function)
                        continue;
                    auto const& shown = f.classBody ? f.className : f.name;
                    if (f.parent == &iModule)
                    {
                        auto const method = iMethods.find(f.name);
                        f.qualname = method != iMethods.end() ? method->second : shown;
                    }
                    else if (f.parent->classBody)
                        f.qualname = f.parent->qualname + "." + shown;
                    else
                        f.qualname = f.parent->qualname + ".<locals>." + shown;
                }
            }

            // (the lambdas of a line's tokens [aBegin, aEnd))
            void find_lambdas(std::size_t aLine, std::size_t aBegin, std::size_t aEnd)
            {
                for (auto t = aBegin; t < aEnd; ++t)
                {
                    if (!is_name(aLine, t, "lambda"))
                        continue;
                    auto& owner = *iOwners[aLine][t];
                    auto& l = new_scope(scope::Lambda, owner);
                    l.line = aLine;
                    l.first = t;
                    l.name = "<lambda>";
                    l.qualname = (owner.kind == scope::Module ? std::string{} : owner.qualname + ".<locals>.") + "<lambda>";
                    // (its parameters: up to its colon)
                    auto colon = t + 1u;
                    int depth = 0;
                    while (colon < aEnd)
                    {
                        auto const& k = tok(aLine, colon);
                        if (k.kind == token::Op)
                        {
                            auto const op = text(k);
                            if (opening(op))
                                ++depth;
                            else if (closing(op))
                                --depth;
                            else if (op == ":" && depth == 0)
                                break;
                        }
                        ++colon;
                    }
                    l.colon = colon;
                    parameters(l, aLine, t + 1u, colon);
                    // (its body: up to a comma or a closing bracket at its level, a comprehension's for, or the end of the statement)
                    auto end = colon + 1u;
                    depth = 0;
                    while (end < aEnd)
                    {
                        auto const& k = tok(aLine, end);
                        if (k.kind == token::Op)
                        {
                            auto const op = text(k);
                            if (opening(op))
                                ++depth;
                            else if (closing(op))
                            {
                                if (depth == 0)
                                    break;
                                --depth;
                            }
                            else if ((op == "," || op == ";" || op == "=" || op == ":") && depth == 0)
                                break;
                        }
                        else if (depth == 0 && k.kind == token::Name && (text(k) == "for" || text(k) == "async"))
                            break;
                        else if (k.kind == token::Name && text(k) == "lambda")
                        {
                            // (a lambda in its body: its parameters' colon is not the end)
                            int d = 0;
                            auto c = end + 1u;
                            while (c < aEnd && !(d == 0 && is_op(aLine, c, ":")))
                            {
                                if (tok(aLine, c).kind == token::Op)
                                {
                                    auto const op = text(aLine, c);
                                    if (opening(op))
                                        ++d;
                                    else if (closing(op))
                                        --d;
                                }
                                ++c;
                            }
                            end = c;
                        }
                        ++end;
                    }
                    l.last = end;
                    for (auto x = colon + 1u; x < end; ++x)
                        iOwners[aLine][x] = &l;
                    iSkip[aLine][t] = true;
                    find_lambdas(aLine, colon + 1u, end);
                    t = end - 1u;
                }
            }

            // --- names: bindings and references

            // (the names that a target list (tokens [aBegin, aEnd)) binds)
            void targets(std::size_t aLine, std::size_t aBegin, std::size_t aEnd)
            {
                // (a name in a subscript or call is a reference)
                std::vector<bool> plain;
                for (auto t = aBegin; t < aEnd; ++t)
                {
                    auto const& k = tok(aLine, t);
                    if (k.kind == token::Op)
                    {
                        auto const op = text(k);
                        if (opening(op))
                        {
                            bool const applied = t > aBegin && (tok(aLine, t - 1u).kind == token::Name || tok(aLine, t - 1u).kind == token::String ||
                                closing(text(aLine, t - 1u)));
                            plain.push_back(!applied && (plain.empty() || plain.back()));
                        }
                        else if (closing(op) && !plain.empty())
                            plain.pop_back();
                        continue;
                    }
                    if (k.kind != token::Name || keywords().contains(text(k)) || (!plain.empty() && !plain.back()))
                        continue;
                    if (t > aBegin && is_op(aLine, t - 1u, "."))
                        continue;
                    if (t + 1u < aEnd && (is_op(aLine, t + 1u, ".") || is_op(aLine, t + 1u, "(") || is_op(aLine, t + 1u, "[")))
                        continue;
                    iBinding[aLine][t] = true;
                    iOwners[aLine][t]->bound.insert(std::string{ text(k) });
                    if (iOwners[aLine][t] == &iModule)
                        iRebound.insert(std::string{ text(k) });
                }
            }

            // (a simple statement: tokens [aBegin, aEnd))
            void simple_statement(std::size_t aLine, std::size_t aBegin, std::size_t aEnd)
            {
                if (aBegin >= aEnd)
                    return;
                auto& owner = *iOwners[aLine][aBegin];
                auto const first = text(aLine, aBegin);
                if (first == "global" || first == "nonlocal")
                {
                    for (auto t = aBegin + 1u; t < aEnd; ++t)
                        if (tok(aLine, t).kind == token::Name)
                        {
                            (first == "global" ? owner.globals : owner.nonlocals).insert(std::string{ text(aLine, t) });
                            iSkip[aLine][t] = true;
                        }
                    if (first == "nonlocal")
                        iNonlocals.emplace_back(aLine, std::make_pair(aBegin, aEnd));
                    return;
                }
                if (first == "import" || first == "from")
                {
                    // (the names bound: a module's first name (import a.b) or its alias; or the imported names (from m import a as b))
                    auto t = aBegin + 1u;
                    if (first == "from")
                    {
                        while (t < aEnd && !is_name(aLine, t, "import"))
                            iSkip[aLine][t++] = true;
                        ++t;
                    }
                    bool firstName = true;
                    for (; t < aEnd; ++t)
                    {
                        if (is_op(aLine, t, ","))
                        {
                            firstName = true;
                            continue;
                        }
                        if (tok(aLine, t).kind != token::Name)
                            continue;
                        iSkip[aLine][t] = true;
                        if (is_name(aLine, t, "as"))
                            continue;
                        bool const alias = t + 1u < aEnd && is_name(aLine, t + 1u, "as");
                        if (t > aBegin && is_name(aLine, t - 1u, "as"))
                        {
                            owner.bound.insert(std::string{ text(aLine, t) });
                            if (&owner == &iModule)
                                iRebound.insert(std::string{ text(aLine, t) });
                        }
                        else if (firstName && !alias && (first == "from" || true))
                        {
                            // (import a.b.c [as d]: a (unless aliased))
                            auto u = t;
                            bool aliased = false;
                            while (u < aEnd && !is_op(aLine, u, ","))
                            {
                                if (is_name(aLine, u, "as"))
                                    aliased = true;
                                ++u;
                            }
                            if (!aliased)
                            {
                                owner.bound.insert(std::string{ text(aLine, t) });
                                if (&owner == &iModule)
                                    iRebound.insert(std::string{ text(aLine, t) });
                            }
                        }
                        firstName = false;
                    }
                    return;
                }
                if (first == "del")
                {
                    targets(aLine, aBegin + 1u, aEnd);
                    return;
                }
                // (assignments: targets = ... = value; target op= value; target: annotation [= value])
                std::vector<std::size_t> equals;
                std::optional<std::size_t> augmented;
                std::optional<std::size_t> annotation;
                int depth = 0;
                for (auto t = aBegin; t < aEnd; ++t)
                {
                    auto const& k = tok(aLine, t);
                    if (k.kind != token::Op || iOwners[aLine][t] != &owner)
                        continue;
                    auto const op = text(k);
                    if (opening(op))
                        ++depth;
                    else if (closing(op))
                        --depth;
                    else if (depth == 0)
                    {
                        if (op == "=")
                            equals.push_back(t);
                        else if (op.size() >= 2u && op.back() == '=' && op != "==" && op != "!=" && op != "<=" && op != ">=" && !augmented && equals.empty())
                            augmented = t;
                        else if (op == ":" && !annotation && equals.empty())
                            annotation = t;
                    }
                }
                if (augmented)
                    targets(aLine, aBegin, *augmented);
                else if (annotation)
                    targets(aLine, aBegin, *annotation);
                else
                {
                    auto start = aBegin;
                    for (auto e : equals)
                    {
                        targets(aLine, start, e);
                        start = e + 1u;
                    }
                }
            }

            // (the statements of a line)
            void analyze_line(std::size_t aLine)
            {
                auto const& tokens = iLines[aLine].tokens;
                auto const n = tokens.size();
                // (walrus targets and comprehensions' targets)
                for (std::size_t t = 0u; t < n; ++t)
                {
                    if (tokens[t].kind == token::Name && t + 1u < n && is_op(aLine, t + 1u, ":="))
                    {
                        // (in a comprehension, the function's)
                        iBinding[aLine][t] = true;
                        iSkip[aLine][t] = true;
                        iOwners[aLine][t]->bound.insert(std::string{ text(aLine, t) });
                    }
                }
                {
                    int depth = 0;
                    for (std::size_t t = 0u; t < n; ++t)
                    {
                        if (tokens[t].kind == token::Op)
                        {
                            auto const op = text(tokens[t]);
                            if (opening(op))
                                ++depth;
                            else if (closing(op))
                                --depth;
                            continue;
                        }
                        if (depth > 0 && is_name(aLine, t, "for"))
                        {
                            auto e = t + 1u;
                            int d = 0;
                            while (e < n && !(d == 0 && is_name(aLine, e, "in")))
                            {
                                if (tokens[e].kind == token::Op)
                                {
                                    auto const op = text(tokens[e]);
                                    if (opening(op))
                                        ++d;
                                    else if (closing(op))
                                        --d;
                                }
                                ++e;
                            }
                            targets(aLine, t + 1u, e);
                        }
                    }
                }
                // (a compound statement's header)
                std::size_t start = 0u;
                if (is_name(aLine, 0u, "async"))
                    start = 1u;
                auto const keyword = n > start ? text(aLine, start) : std::string_view{};
                auto const header_colon = [&](std::size_t aFrom)
                {
                    int depth = 0;
                    for (auto t = aFrom; t < n; ++t)
                    {
                        if (tokens[t].kind != token::Op || iOwners[aLine][t]->kind == scope::Lambda)
                            continue;
                        auto const op = text(tokens[t]);
                        if (opening(op))
                            ++depth;
                        else if (closing(op))
                            --depth;
                        else if (op == ":" && depth == 0)
                            return t;
                    }
                    return n;
                };
                std::size_t body = n;
                if (is_op(aLine, 0u, "@"))
                    return;
                if (keyword == "def")
                {
                    // (its inline body)
                    for (auto const& s : iScopes)
                        if (s->kind == scope::Function && s->header == aLine)
                            body = s->colon + 1u;
                }
                else if (keyword == "class")
                    body = header_colon(start) + 1u;
                else if (keyword == "if" || keyword == "elif" || keyword == "while" || keyword == "else" || keyword == "try" || keyword == "finally")
                    body = header_colon(start) + 1u;
                else if (keyword == "for")
                {
                    auto e = start + 1u;
                    int d = 0;
                    while (e < n && !(d == 0 && is_name(aLine, e, "in")))
                    {
                        if (tokens[e].kind == token::Op)
                        {
                            auto const op = text(tokens[e]);
                            if (opening(op))
                                ++d;
                            else if (closing(op))
                                --d;
                        }
                        ++e;
                    }
                    targets(aLine, start + 1u, e);
                    body = header_colon(e) + 1u;
                }
                else if (keyword == "except" || keyword == "with")
                {
                    auto const colon = header_colon(start);
                    // (as targets)
                    for (auto t = start + 1u; t < colon; ++t)
                        if (is_name(aLine, t, "as"))
                        {
                            auto e = t + 1u;
                            int d = 0;
                            while (e < colon && !(d == 0 && is_op(aLine, e, ",")))
                            {
                                if (tokens[e].kind == token::Op)
                                {
                                    auto const op = text(tokens[e]);
                                    if (opening(op))
                                        ++d;
                                    else if (closing(op))
                                        --d;
                                }
                                ++e;
                            }
                            targets(aLine, t + 1u, e);
                            for (auto x = t + 1u; x < e; ++x)
                                iSkip[aLine][x] = iSkip[aLine][x] || iBinding[aLine][x];
                        }
                    body = colon + 1u;
                }
                else
                    body = 0u;
                // (the simple statements)
                int depth = 0;
                auto s = body;
                for (auto t = body; t <= n; ++t)
                {
                    if (t < n && tokens[t].kind == token::Op)
                    {
                        auto const op = text(tokens[t]);
                        if (opening(op))
                            ++depth;
                        else if (closing(op))
                            --depth;
                        else if (op == ";" && depth == 0)
                        {
                            simple_statement(aLine, s, t);
                            s = t + 1u;
                        }
                    }
                    if (t == n)
                        simple_statement(aLine, s, n);
                }
                // (references)
                std::vector<std::string_view> brackets;
                for (std::size_t t = 0u; t < n; ++t)
                {
                    auto const& k = tokens[t];
                    if (k.kind == token::Op)
                    {
                        auto const op = text(k);
                        if (opening(op))
                            brackets.push_back(op);
                        else if (closing(op) && !brackets.empty())
                            brackets.pop_back();
                        continue;
                    }
                    if (k.kind == token::Name && text(k) == "yield" && (iOwners[aLine][t]->kind == scope::Function || iOwners[aLine][t]->kind == scope::Lambda))
                        iOwners[aLine][t]->generator = iOwners[aLine][t]->yields = true;
                    if (k.kind != token::Name || iSkip[aLine][t] || keywords().contains(text(k)))
                        continue;
                    if (t > 0u && is_op(aLine, t - 1u, "."))
                    {
                        iSkip[aLine][t] = true;
                        continue;
                    }
                    // (a keyword argument)
                    if (t + 1u < n && is_op(aLine, t + 1u, "=") && !brackets.empty() && brackets.back() == "(" && t > 0u &&
                        (is_op(aLine, t - 1u, "(") || is_op(aLine, t - 1u, ",")))
                    {
                        iSkip[aLine][t] = true;
                        continue;
                    }
                    iOwners[aLine][t]->referenced.insert(std::string{ text(k) });
                }
            }

            // (the scope whose local a name (used in aScope, which does not bind it) is: null if a global)
            scope* defining(scope& aScope, std::string const& aName)
            {
                // (a class's body is not a scope of the functions within it (except for its __class__))
                for (auto s = aScope.parent; s != nullptr && s->kind != scope::Module; s = s->parent)
                    if (s->local(aName) && (!s->classBody || aName == "__class__"))
                        return s;
                return nullptr;
            }

            void resolve(scope& aScope)
            {
                for (auto child : aScope.children)
                    resolve(*child);
                if (aScope.kind == scope::Module)
                    return;
                std::set<std::string> names = aScope.referenced;
                names.insert(aScope.nonlocals.begin(), aScope.nonlocals.end());
                for (auto const& name : names)
                {
                    if (aScope.local(name) || aScope.globals.contains(name))
                        continue;
                    auto const d = defining(aScope, name);
                    if (d == nullptr)
                    {
                        if (aScope.nonlocals.contains(name))
                            throw std::runtime_error("no binding for nonlocal '" + name + "' found");
                        continue;
                    }
                    d->cells.insert(name);
                    for (auto s = &aScope; s != d; s = s->parent)
                        s->frees.insert(name);
                }
            }

            static bool has_defaults(scope const& aScope)
            {
                for (auto const& p : aScope.parameters)
                    if (p.defaultValue)
                        return true;
                return false;
            }

            void number_functions(scope& aScope)
            {
                for (auto child : aScope.children)
                {
                    if (child->kind == scope::Function && child->classBody)
                        child->hoisted = "_a" + child->name.substr(2u);   // (_bN_name: _aN_name, the compiler's class body (see its class_body))
                    else if (child->kind == scope::Function && (child->parent != &iModule || !child->decorators.empty() || conditional(*child)))
                        child->hoisted = (child->parent != &iModule ? "_n" : "_d") + std::to_string(++iFunctions) + "_" + child->name;
                    if (child->kind == scope::Function && child->generator)
                        child->body = "_g" + std::to_string(++iFunctions) + "_" + child->name;
                    number_functions(*child);
                }
            }

            // --- the rewritten text

            static std::string cell(std::string const& aName) { return "_k_" + aName; }

            std::string cell_parameters(scope const& aScope) const
            {
                std::string result;
                for (auto const& f : aScope.frees)
                    result += (result.empty() ? "" : ", ") + cell(f);
                return result;
            }

            // (the cells of a function's variables: created at the start of its body)
            std::string cell_initializers(scope const& aScope) const
            {
                std::string result;
                for (auto const& c : aScope.cells)
                {
                    bool parameter = false;
                    for (auto const& p : aScope.parameters)
                        if (p.name == c)
                            parameter = true;
                    result += (result.empty() ? "" : "; ") + cell(c) + " = __cell('" + c + "'" + (parameter ? ", " + c : std::string{}) + ")";
                }
                return result;
            }

            // (the text of a line's tokens [aBegin, aEnd) rendered in aScope)
            std::string render_tokens(std::size_t aLine, std::size_t aBegin, std::size_t aEnd, scope const* aScope)
            {
                if (aBegin >= aEnd)
                    return {};
                auto const begin = tok(aLine, aBegin).begin;
                auto const end = tok(aLine, aEnd - 1u).end;
                return render(begin, end, aScope, iLines[aLine].indent);
            }

            // (a lambda: a closure of a lambda that receives its cells and default values, if it needs them)
            std::string render_lambda(scope& aLambda)
            {
                auto const l = aLambda.line;
                std::string parameters;
                std::string defaults;
                for (std::size_t t = aLambda.first + 1u; t < aLambda.colon; )
                {
                    // (each parameter, with None for its default value)
                    auto e = t;
                    int depth = 0;
                    while (e < aLambda.colon)
                    {
                        if (tok(l, e).kind == token::Op)
                        {
                            auto const op = text(l, e);
                            if (opening(op)) ++depth;
                            else if (closing(op)) --depth;
                            else if (op == "," && depth == 0) break;
                        }
                        ++e;
                    }
                    std::optional<std::size_t> equals;
                    for (auto q = t; q < e; ++q)
                        if (is_op(l, q, "="))
                        {
                            equals = q;
                            break;
                        }
                    if (equals)
                    {
                        parameters += (parameters.empty() ? "" : ", ") + std::string{ std::string_view{ iText }.substr(tok(l, t).begin, tok(l, *equals).begin - tok(l, t).begin) } + "=None";
                        defaults += render_tokens(l, *equals + 1u, e, aLambda.parent) + ", ";
                    }
                    else if (e > t)
                        parameters += (parameters.empty() ? "" : ", ") + std::string{ std::string_view{ iText }.substr(tok(l, t).begin, tok(l, e - 1u).end - tok(l, t).begin) };
                    t = e + 1u;
                }
                auto body = render_tokens(l, aLambda.colon + 1u, aLambda.last, &aLambda);
                if (aLambda.generator)
                {
                    // (a lambda whose body yields: a generator function, whose body is a function at the end of the module (as a
                    // generator def's): "lambda x: __generator(_gN_lambda, '<lambda>', qualname, cells..., x)")
                    auto const function = "_g" + std::to_string(++iFunctions) + "_lambda";
                    std::string generator = "__generator(" + function + ", '<lambda>', '" + aLambda.qualname + "'";
                    std::string functionParameters = cell_parameters(aLambda);
                    for (auto const& f : aLambda.frees)
                        generator += ", " + cell(f);
                    bool star = false;
                    for (auto const& p : aLambda.parameters)
                    {
                        std::string item;
                        switch (p.kind)
                        {
                        case scope::parameter::Positional: item = p.name; generator += ", " + p.name; break;
                        case scope::parameter::VarArgs: item = "*" + p.name; star = true; generator += ", *" + p.name; break;
                        case scope::parameter::VarKeywords: item = "**" + p.name; generator += ", **" + p.name; break;
                        case scope::parameter::KeywordOnly: item = (star ? "" : "*, ") + p.name; star = true; generator += ", " + p.name + "=" + p.name; break;
                        }
                        functionParameters += (functionParameters.empty() ? "" : ", ") + item;
                    }
                    iHoisted.push_back("def " + function + "(" + functionParameters + "):\n    return " + body + "\n");
                    auto const line = physical_line(tok(l, aLambda.first).begin);
                    iHoistedLines.push_back({ line, line });
                    body = generator + ")";
                }
                if (aLambda.frees.empty() && defaults.empty() && aLambda.cells.empty())
                    return "lambda " + parameters + ": " + body;
                if (!aLambda.cells.empty())
                {
                    // (its cells: a lambda called with them)
                    std::string inner;
                    std::string arguments;
                    for (auto const& c : aLambda.cells)
                    {
                        bool parameter = false;
                        for (auto const& p : aLambda.parameters)
                            if (p.name == c)
                                parameter = true;
                        inner += (inner.empty() ? "" : ", ") + cell(c);
                        arguments += (arguments.empty() ? "" : ", ") + std::string{ "__cell('" } + c + "'" + (parameter ? ", " + c : std::string{}) + ")";
                    }
                    for (auto const& f : aLambda.frees)
                    {
                        inner += ", " + cell(f);
                        arguments += ", " + cell(f);
                    }
                    body = "(lambda " + inner + ": " + body + ")(" + arguments + ")";
                }
                auto const cells = cell_parameters(aLambda);
                std::string const all = cells + (!cells.empty() && !parameters.empty() ? ", " : "") + parameters;
                return "__closure(lambda " + all + ": " + body + ", '<lambda>', '" + aLambda.qualname + "', " +
                    (defaults.empty() ? std::string{ "None" } : "(" + defaults + ")") + (cells.empty() ? "" : ", " + cells) + ")";
            }

            // (a generator function's body: return __generator(body function, name, qualname, cells..., arguments...))
            std::string generator_return(scope const& aFunction) const
            {
                std::string result = std::string{ "return " } + (!aFunction.isAsync ? "__generator(" : aFunction.yields ? "__async_generator(" : "__coroutine(") + aFunction.body + ", '" + aFunction.name + "', '" + aFunction.qualname + "'";
                for (auto const& f : aFunction.frees)
                    result += ", " + cell(f);
                for (auto const& p : aFunction.parameters)
                    switch (p.kind)
                    {
                    case scope::parameter::Positional: result += ", " + p.name; break;
                    case scope::parameter::VarArgs: result += ", *" + p.name; break;
                    case scope::parameter::VarKeywords: result += ", **" + p.name; break;
                    case scope::parameter::KeywordOnly: result += ", " + p.name + "=" + p.name; break;
                    }
                return result + ")";
            }

            // (the function of a generator's body (at the end of the module): its body's text [aBegin, aEnd), or an inline body)
            void hoist_generator_body(scope& aFunction)
            {
                auto const l = aFunction.header;
                std::string parameters = cell_parameters(aFunction);
                bool star = false;
                for (auto const& p : aFunction.parameters)
                {
                    std::string item;
                    switch (p.kind)
                    {
                    case scope::parameter::Positional: item = p.name; break;
                    case scope::parameter::VarArgs: item = "*" + p.name; star = true; break;
                    case scope::parameter::VarKeywords: item = "**" + p.name; break;
                    case scope::parameter::KeywordOnly: item = (star ? "" : "*, ") + p.name; star = true; break;
                    }
                    parameters += (parameters.empty() ? "" : ", ") + item;
                }
                std::string function = "def " + aFunction.body + "(" + parameters + "):";
                iBodies.insert(&aFunction);
                if (aFunction.inlineBody)
                    function += " " + render_tokens(l, aFunction.colon + 1u, iLines[l].tokens.size(), &aFunction) + "\n";
                else
                {
                    function += "\n";
                    if (aFunction.bodyEnd > l + 1u)
                        function += dedent(render(iLines[l + 1u].begin, iLines[aFunction.bodyEnd - 1u].end, &aFunction, iLines[l].indent), iLines[l].indent) + "\n";
                }
                iBodies.erase(&aFunction);
                hoist(std::move(function), aFunction, 1u);
            }

            // (a function at the end of the module: the lines it is (its first aHeader lines: its def's; then its body's, after the 
            // lines that its rendering added at the start of its body (the cells' initializers))
            void hoist(std::string aFunction, scope const& aDefinition, std::size_t aHeader)
            {
                std::vector<std::size_t> lines;
                auto const l = aDefinition.header;
                auto const header = physical_line(iLines[l].begin);
                auto const count = static_cast<std::size_t>(std::count(aFunction.begin(), aFunction.end(), '\n'));
                for (std::size_t i = 0u; i < std::min(aHeader, count); ++i)
                    lines.push_back(header);
                if (count > aHeader && !aDefinition.inlineBody && aDefinition.bodyEnd > l + 1u)
                {
                    auto const first = physical_line(iLines[l + 1u].begin);
                    auto const body = physical_line(iLines[aDefinition.bodyEnd - 1u].end) - first + 1u;
                    auto const rendered = count - aHeader;
                    auto const added = rendered > body ? rendered - body : 0u;
                    for (std::size_t i = 0u; i < rendered; ++i)
                        lines.push_back(i < added ? first : first + i - added);
                }
                while (lines.size() < count)
                    lines.push_back(header);
                iHoisted.push_back(std::move(aFunction));
                iHoistedLines.push_back(std::move(lines));
            }

            // (the line (0-based) of a position of the text)
            std::size_t physical_line(std::size_t aPosition)
            {
                if (iLineStarts.empty())
                {
                    iLineStarts.push_back(0u);
                    for (std::size_t i = 0u; i < iText.size(); ++i)
                        if (iText[i] == '\n')
                            iLineStarts.push_back(i + 1u);
                }
                return static_cast<std::size_t>(std::upper_bound(iLineStarts.begin(), iLineStarts.end(), aPosition) - iLineStarts.begin()) - 1u;
            }

            // (a nested or decorated function: where it is defined, the closure (decorated); and its function at the end of the module)
            std::string render_function(scope& aFunction)
            {
                auto const l = aFunction.header;
                auto const& header = iLines[l];
                // (the closure)
                std::string parameters;
                std::string defaults;
                for (auto const& p : aFunction.parameters)
                    if (p.defaultValue)
                        defaults += render_tokens(l, p.defaultValue->first, p.defaultValue->second, aFunction.parent) + ", ";
                {
                    // (the parameters with None for their default values (and without the annotations that are not the grammar's types))
                    std::size_t from = aFunction.openParen + 1u;
                    std::size_t position = tok(l, from < aFunction.closeParen ? from : aFunction.closeParen).begin;
                    std::vector<std::tuple<std::size_t, std::size_t, std::string>> cuts;
                    for (auto const& p : aFunction.parameters)
                    {
                        if (p.annotation && (!grammar_annotation(l, *p.annotation) || p.kind == scope::parameter::VarArgs || p.kind == scope::parameter::VarKeywords) &&
                            p.annotation->first < p.annotation->second)
                            cuts.emplace_back(tok(l, p.annotation->first - 1u).begin, tok(l, p.annotation->second - 1u).end, std::string{});
                        if (p.defaultValue)
                            cuts.emplace_back(tok(l, p.defaultValue->first).begin, tok(l, p.defaultValue->second - 1u).end, placeholder(l, p));
                    }
                    for (auto const& c : cuts)
                    {
                        parameters += iText.substr(position, std::get<0>(c) - position) + std::get<2>(c);
                        position = std::get<1>(c);
                    }
                    parameters += iText.substr(position, tok(l, aFunction.closeParen).begin - position);
                }
                auto const cells = cell_parameters(aFunction);
                auto const& shown = aFunction.classBody ? aFunction.className : aFunction.name;
                std::string value = "__closure(" + aFunction.hoisted + ", '" + shown + "', '" + aFunction.qualname + "', " +
                    (defaults.empty() ? std::string{ "None" } : "(" + defaults + ")") + (cells.empty() ? "" : ", " + cells) + ")";
                // (a class's body: the class)
                if (aFunction.classBody)
                    value = "__build_class(" + value + ")";
                else if (auto const annotations = annotations_dict(aFunction); !annotations.empty())
                    value = "__annotate(" + value + ", " + annotations + ")";
                if (!aFunction.typeParameters.empty())
                    value = "__generic_function((" + aFunction.typeParameters + "), " + value + ")";
                for (auto d = aFunction.decorators.rbegin(); d != aFunction.decorators.rend(); ++d)
                    value = render_tokens(*d, 1u, iLines[*d].tokens.size(), aFunction.parent) + "(" + value + ")";
                auto const& parent = *aFunction.parent;
                std::string const target = parent.cells.contains(shown) ? cell(shown) + ".v" : shown;
                // (the function)
                bool const any = parameters.find_first_not_of(" \t") != std::string::npos;
                std::string function = "def " + aFunction.hoisted + "(" + cells + (!cells.empty() && any ? ", " : "") + parameters + ")";
                // (its return annotation (if it is the grammar's type) and colon)
                if (aFunction.returnAnnotation && !grammar_annotation(l, *aFunction.returnAnnotation))
                    function += ":";
                else
                    function += render_tokens(l, aFunction.closeParen + 1u, aFunction.colon + 1u, aFunction.parent);
                std::size_t headerLines = 1u;
                if (aFunction.generator)
                {
                    function += "\n    " + generator_return(aFunction) + "\n";
                    headerLines = 2u;
                    hoist_generator_body(aFunction);
                }
                else if (aFunction.inlineBody)
                    function += " " + render_tokens(l, aFunction.colon + 1u, header.tokens.size(), &aFunction) + "\n";
                else
                {
                    function += "\n";
                    if (aFunction.bodyEnd > l + 1u)
                    {
                        auto const begin = iLines[l + 1u].begin;
                        auto const end = iLines[aFunction.bodyEnd - 1u].end;
                        function += dedent(render(begin, end, &aFunction, header.indent), header.indent) + "\n";
                    }
                }
                hoist(std::move(function), aFunction, headerLines);
                return target + " = " + value;
            }

            // (aText without aColumns of the indentation of each of its lines (not those within strings))
            std::string dedent(std::string const& aText, std::size_t aColumns) const
            {
                std::string result;
                bool lineStart = true;
                char quote = 0;
                bool triple = false;
                for (std::size_t i = 0u; i < aText.size(); ++i)
                {
                    char const c = aText[i];
                    if (lineStart && quote == 0)
                    {
                        std::size_t k = 0u;
                        while (k < aColumns && i < aText.size() && (aText[i] == ' ' || aText[i] == '\t'))
                        {
                            ++k;
                            ++i;
                        }
                        lineStart = false;
                        if (i >= aText.size())
                            break;
                        --i;
                        continue;
                    }
                    result += c;
                    if (quote != 0)
                    {
                        if (c == '\\' && i + 1u < aText.size())
                            result += aText[++i];
                        else if (c == quote && (!triple || (i >= 2u && aText[i - 1u] == quote && aText[i - 2u] == quote && result.size() >= 3u)))
                            quote = 0;
                        else if (c == '\n' && !triple)
                            quote = 0;
                    }
                    else if (c == '\'' || c == '\"')
                    {
                        triple = i + 2u < aText.size() && aText[i + 1u] == c && aText[i + 2u] == c;
                        quote = c;
                        if (triple)
                        {
                            result += aText[++i];
                            result += aText[++i];
                        }
                    }
                    else if (c == '#')
                    {
                        while (i + 1u < aText.size() && aText[i + 1u] != '\n')
                            result += aText[++i];
                    }
                    if (c == '\n' && quote == 0)
                        lineStart = true;
                }
                return result;
            }

            // (the text [aBegin, aEnd) rendered in aScope: its nested functions and lambdas, its cells and nonlocal statements)
            // (the cells of the function whose body starts at a line's token (if any))
            std::string body_start(std::size_t aLine, std::size_t aToken) const
            {
                for (auto const& s : iScopes)
                    if (s->kind == scope::Function && !s->cells.empty())
                    {
                        bool const start = s->inlineBody ? (s->header == aLine && aToken == s->colon + 1u) : (aLine == s->header + 1u && aToken == 0u && s->bodyEnd > aLine);
                        if (start)
                            return cell_initializers(*s);
                    }
                return {};
            }

            std::string render(std::size_t aBegin, std::size_t aEnd, scope const* aScope, std::size_t aIndent)
            {
                (void)aIndent;
                (void)aScope;
                // (the module's rendering is the outermost (see iAddedLines); the functions hoisted to its end are rendered within it)
                ++iRenderDepth;
                struct leave
                {
                    std::size_t& depth;
                    ~leave() { --depth; }
                } const leaving{ iRenderDepth };
                std::string result;
                auto position = aBegin;
                auto const emit = [&](std::size_t aTo)
                {
                    if (aTo > position)
                        result += iText.substr(position, aTo - position);
                    position = std::max(position, aTo);
                };
                for (std::size_t l = 0u; l < iLines.size(); ++l)
                {
                    auto const& line = iLines[l];
                    if (line.tokens.back().end <= aBegin)
                        continue;
                    if (line.tokens.front().begin >= aEnd)
                        break;
                    scope* function = nullptr;
                    // (the body of a generator function of the module: it returns a generator (its body is a function at the end of the module))
                    for (auto const& s : iScopes)
                        if (s->kind == scope::Function && s->parent == &iModule && s->generator && !s->inlineBody && s->header + 1u == l && s->bodyEnd > l && !conditional(*s) &&
                            !iBodies.contains(s.get()))
                        {
                            function = s.get();
                            break;
                        }
                    if (function != nullptr && line.tokens.front().begin >= aBegin)
                    {
                        emit(line.tokens.front().begin);
                        result += generator_return(*function);
                        auto const regionEnd = iLines[function->bodyEnd - 1u].end;
                        auto const lines = std::count(iText.begin() + static_cast<std::ptrdiff_t>(line.tokens.front().begin), iText.begin() + static_cast<std::ptrdiff_t>(regionEnd), '\n');
                        result += std::string(static_cast<std::size_t>(lines), '\n');
                        hoist_generator_body(*function);
                        position = regionEnd;
                        l = function->bodyEnd - 1u;
                        continue;
                    }
                    // (a nested function defined here: its decorators and body are replaced by its closure (and blank lines))
                    for (auto const& s : iScopes)
                        if (s->kind == scope::Function && !s->hoisted.empty() && (s->decorators.empty() ? s->header : s->decorators.front()) == l &&
                            (s->parent != &iModule || s->classBody || conditional(*s)))
                            function = s.get();
                    if (function != nullptr && line.tokens.front().begin >= aBegin)
                    {
                        auto const regionBegin = line.tokens.front().begin;
                        auto const lastLine = function->inlineBody ? function->header : function->bodyEnd - 1u;
                        auto const regionEnd = iLines[lastLine].end;
                        emit(regionBegin);
                        if (auto const initializers = body_start(l, 0u); !initializers.empty())
                            result += initializers + "; ";
                        auto const closure = render_function(*function);
                        result += closure;
                        // (the lines of the region (blank))
                        auto const lines = std::count(iText.begin() + static_cast<std::ptrdiff_t>(regionBegin), iText.begin() + static_cast<std::ptrdiff_t>(regionEnd), '\n');
                        result += std::string(static_cast<std::size_t>(lines), '\n');
                        position = regionEnd;
                        l = lastLine;
                        continue;
                    }
                    // (a decorated function of the module: its closure is defined where its decorators are; its function is renamed)
                    for (auto const& s : iScopes)
                        if (s->kind == scope::Function && s->parent == &iModule && !s->decorators.empty() && s->decorators.front() == l && !s->classBody && !conditional(*s))
                        {
                            function = s.get();
                            break;
                        }
                    if (function != nullptr && line.tokens.front().begin >= aBegin)
                    {
                        emit(line.tokens.front().begin);
                        std::string defaults;
                        for (auto const& p : function->parameters)
                            if (p.defaultValue)
                                defaults += render_tokens(function->header, p.defaultValue->first, p.defaultValue->second, &iModule) + ", ";
                        std::string value = "__closure(" + function->hoisted + ", '" + function->name + "', '" + function->qualname + "', " +
                            (defaults.empty() ? std::string{ "None" } : "(" + defaults + ")") + ")";
                        if (auto const annotations = annotations_dict(*function); !annotations.empty())
                            value = "__annotate(" + value + ", " + annotations + ")";
                        for (auto d = function->decorators.rbegin(); d != function->decorators.rend(); ++d)
                            value = render_tokens(*d, 1u, iLines[*d].tokens.size(), &iModule) + "(" + value + ")";
                        result += function->name + " = " + value;
                        position = iLines[function->decorators.back()].end;
                        // (the other decorators' lines: blank)
                        for (auto d = function->decorators.begin() + 1; d != function->decorators.end(); ++d)
                        {
                            emit(iLines[*d].begin);
                            position = iLines[*d].end;
                        }
                        // (the def: renamed; its default values are the closure's)
                        auto const h = function->header;
                        if (function->isAsync)
                        {
                            // (async def: def (its function returns a coroutine))
                            emit(tok(h, 0u).begin);
                            position = tok(h, 1u).begin;
                        }
                        emit(tok(h, function->nameToken).begin);
                        result += function->hoisted;
                        position = tok(h, function->nameToken).end;
                        for (auto const& p : function->parameters)
                        {
                            if (p.annotation && p.annotation->first < p.annotation->second && (!grammar_annotation(h, *p.annotation) ||
                                p.kind == scope::parameter::VarArgs || p.kind == scope::parameter::VarKeywords))
                            {
                                emit(tok(h, p.annotation->first - 1u).begin);
                                position = tok(h, p.annotation->second - 1u).end;
                            }
                            if (p.defaultValue)
                            {
                                emit(tok(h, p.defaultValue->first).begin);
                                result += placeholder(h, p);
                                position = tok(h, p.defaultValue->second - 1u).end;
                            }
                        }
                        l = h - 1u;
                        continue;
                    }
                    // (the line's tokens)
                    for (std::size_t t = 0u; t < line.tokens.size(); ++t)
                    {
                        auto const& k = line.tokens[t];
                        if (k.begin < position || k.begin < aBegin)
                            continue;
                        if (k.end > aEnd)
                            break;
                        auto* const owner = iOwners[l][t];
                        // (an annotation that is not the grammar's type: removed)
                        if (auto const stripped = iStripped.find(l); !iAnnotating && stripped != iStripped.end() &&
                            std::any_of(stripped->second.begin(), stripped->second.end(), [&](auto const& r) { return t >= r.first && t < r.second; }))
                        {
                            emit(k.begin);
                            position = k.end;
                            continue;
                        }
                        // (async def: def (its function returns a coroutine))
                        if (t == 0u && is_name(l, 0u, "async") && is_name(l, 1u, "def"))
                        {
                            emit(k.begin);
                            position = tok(l, 1u).begin;
                            continue;
                        }
                        // (the closing brackets of yield expressions)
                        for (auto c = iClosers.begin(); c != iClosers.end();)
                            if (c->first.first == l && c->first.second == t)
                            {
                                emit(k.begin);
                                result += c->second;
                                c = iClosers.erase(c);
                            }
                            else
                                ++c;
                        // (the inline body of a generator function of the module)
                        bool inlineGenerator = false;
                        for (auto const& s : iScopes)
                            if (s->kind == scope::Function && s->parent == &iModule && s->generator && s->inlineBody && s->header == l && t == s->colon + 1u && !conditional(*s) &&
                                !iBodies.contains(s.get()))
                            {
                                emit(k.begin);
                                result += generator_return(*s);
                                hoist_generator_body(*s);
                                position = line.tokens.back().end;
                                inlineGenerator = true;
                            }
                        if (inlineGenerator)
                            break;
                        // (the start of a function's body: its cells (before its first statement, e.g. a yield expression))
                        if (auto const initializers = body_start(l, t); !initializers.empty())
                        {
                            emit(k.begin);
                            auto const first = text(l, t);
                            bool const compound = first == "if" || first == "for" || first == "while" || first == "try" || first == "with" ||
                                first == "def" || first == "class" || first == "async" || first == "@";
                            if (compound)
                            {
                                result += initializers + "\n" + std::string(line.indent, ' ');
                                if (iRenderDepth == 1u)
                                    iAddedLines.push_back(physical_line(iLines[l].begin));
                            }
                            else
                                result += initializers + "; ";
                        }
                        // (a yield expression: __yield(value) or __yield_from(iterable))
                        if (is_name(l, t, "yield") && (owner->kind == scope::Function || owner->kind == scope::Lambda) && owner->generator)
                        {
                            bool const from = is_name(l, t + 1u, "from");
                            auto const first = t + (from ? 2u : 1u);
                            auto end = first;
                            int depth = 0;
                            bool tuple = false;
                            for (; end < line.tokens.size(); ++end)
                            {
                                if (tok(l, end).kind != token::Op)
                                    continue;
                                auto const op = text(l, end);
                                if (opening(op))
                                    ++depth;
                                else if (closing(op))
                                {
                                    if (depth == 0)
                                        break;
                                    --depth;
                                }
                                else if (depth == 0 && (op == ";" || op == "=" || (op == ":" && iOwners[l][end]->kind != scope::Lambda)))
                                    break;
                                else if (depth == 0 && op == ",")
                                    tuple = true;
                            }
                            emit(k.begin);
                            result += from ? "__yield_from(" : "__yield(";
                            position = tok(l, first - 1u).end;
                            std::string closer = ")";
                            if (end == first)
                                result += "None";
                            else if (tuple && !from)
                            {
                                result += "(";
                                closer = "))";
                            }
                            if (end == first)
                                result += closer;
                            else
                                iClosers.emplace_back(std::make_pair(l, end), closer);
                            t = first - 1u;
                            continue;
                        }
                        // (a lambda)
                        if (is_name(l, t, "lambda"))
                        {
                            scope* lambda = nullptr;
                            for (auto const& s : iScopes)
                                if (s->kind == scope::Lambda && s->line == l && s->first == t)
                                    lambda = s.get();
                            if (lambda != nullptr && lambda->last > 0u)
                            {
                                emit(k.begin);
                                result += render_lambda(*lambda);
                                position = tok(l, lambda->last - 1u).end;
                                t = lambda->last - 1u;
                                continue;
                            }
                        }
                        // (a nonlocal statement)
                        bool nonlocal = false;
                        for (auto const& n : iNonlocals)
                            if (n.first == l && n.second.first == t)
                            {
                                emit(k.begin);
                                result += "pass";
                                position = tok(l, n.second.second - 1u).end;
                                t = n.second.second - 1u;
                                nonlocal = true;
                            }
                        if (nonlocal)
                            continue;
                        // (a cell's name)
                        if (k.kind == token::Name && !iSkip[l][t] && !keywords().contains(text(k)) && owner->kind != scope::Module)
                        {
                            std::string const name{ text(k) };
                            if (owner->cells.contains(name) || owner->frees.contains(name))
                            {
                                emit(k.begin);
                                result += cell(name) + ".v";
                                position = k.end;
                            }
                        }
                    }
                    // (yield expressions that end with the line, if this renders its end (not, e.g., a lambda's body within it))
                    for (auto c = iClosers.begin(); c != iClosers.end();)
                        if (c->first.first == l && c->first.second >= line.tokens.size() && line.tokens.back().end <= aEnd)
                        {
                            emit(std::min(line.tokens.back().end, aEnd));
                            result += c->second;
                            c = iClosers.erase(c);
                        }
                        else
                            ++c;
                }
                emit(aEnd);
                return result;
            }
        private:
            std::string const& iText;
            std::vector<logical_line> iLines;
            scope iModule;
            std::vector<std::unique_ptr<scope>> iScopes;
            std::vector<std::vector<scope*>> iOwners;   ///< (the scope of each token)
            std::vector<std::vector<bool>> iBinding;
            std::vector<std::vector<bool>> iSkip;       ///< (names that are not variables' (attributes, keywords, parameters, ...))
            std::vector<std::pair<std::size_t, std::pair<std::size_t, std::size_t>>> iNonlocals;
            std::map<std::string, std::string> iMethods;
            std::size_t iFunctions = 0u;
            std::vector<std::string> iHoisted;
            std::vector<std::vector<std::size_t>> iHoistedLines;    ///< (the line of the text that each of a hoisted function's lines is)
            std::vector<std::size_t> iAddedLines;                   ///< (the lines of the text before which the module's rendering added a line)
            std::size_t iRenderDepth = 0u;
            std::vector<std::size_t> iLineStarts;
            std::set<scope const*> iBodies;     ///< (generators whose bodies are being rendered)
            annotations_t* iAnnotations = nullptr;
            bool iAnnotating = false;
            bool iStringAnnotations = false;    ///< (from __future__ import annotations)
            std::set<std::string> iRebound;     ///< (the names that the module's statements (not its defs) bind or delete)           ///< (an annotations dict is being rendered: its tokens are not removed)
            std::map<std::size_t, std::vector<std::pair<std::size_t, std::size_t>>> iStripped;  ///< (by line: tokens of annotations removed)
            std::vector<std::pair<std::pair<std::size_t, std::size_t>, std::string>> iClosers; ///< (yield expressions' closing brackets: before a line's token)
        };
    }

    std::string rewrite_nested_functions(std::string const& aText, bool& aChanged, std::map<std::string, std::vector<std::pair<std::string, std::string>>>* aAnnotations,
        std::vector<std::size_t>* aLines)
    {
        rewriter r{ aText, aAnnotations };
        return r.rewrite(aChanged, aLines);
    }

    // (an imported module's source (once its other rewrites have been made: its functions are not nested): its names are
    // the program's variables aPrefix + name, and its getter (the function aGetter(index) of the module object: the value of 
    // the module's name aNames[index]; aGetter(-1): the names; aGetter(index, value, True): the name is bound to the value (e.g. 
    // module.name = value, or exec's binding of it): True, or False if it cannot be (a function that a def defines, or (the main 
    // module's) a builtin's name)) is defined at its end)
    std::string rewrite_module_names(std::string const& aText, std::string const& aPrefix, std::string const& aGetter, std::vector<std::string>& aNames,
        std::optional<std::vector<std::string>>& aAll, std::set<std::string> const& aBuiltins)
    {
        rewriter r{ aText };
        std::set<std::string> defined;
        std::string result = r.rename_module(aPrefix, aNames, aAll, defined);
        // (the main module's names that are builtins' (its prefix is empty): a function's global is not one of those)
        if (aPrefix.empty())
            for (auto const& n : aNames)
                if (aBuiltins.contains(n))
                    defined.insert(n);
        if (!result.empty() && result.back() != '\n')
            result += '\n';
        result += "def " + aGetter + "(_i, _v=None, _s=False):\n";
        {
            std::string bound;
            for (auto const& n : aNames)
                if (!defined.contains(n))
                    bound += (bound.empty() ? "" : ", ") + aPrefix + n;
            if (!bound.empty())
                result += "    global " + bound + "\n";
        }
        result += "    if _i < 0:\n        return (";
        for (auto const& n : aNames)
            result += "'" + n + "', ";
        result += ")\n";
        // (a binary search of the indexes: the name's value, or (_s) its binding)
        auto tree = [&](auto& self, std::size_t aLow, std::size_t aHigh, std::string const& aIndent, bool aBind) -> void
        {
            if (aHigh - aLow == 1u)
            {
                if (aBind && defined.contains(aNames[aLow]))
                    result += aIndent + "return False\n";
                else if (aBind)
                    result += aIndent + aPrefix + aNames[aLow] + " = _v\n" + aIndent + "return True\n";
                else
                    result += aIndent + "return " + aPrefix + aNames[aLow] + "\n";
                return;
            }
            auto const middle = aLow + (aHigh - aLow) / 2u;
            result += aIndent + "if _i < " + std::to_string(middle) + ":\n";
            self(self, aLow, middle, aIndent + "    ", aBind);
            self(self, middle, aHigh, aIndent, aBind);
        };
        if (aNames.empty())
            result += "    return None\n";
        else
        {
            result += "    if _s:\n";
            tree(tree, 0u, aNames.size(), "        ", true);
            tree(tree, 0u, aNames.size(), "    ", false);
        }
        return result;
    }
}
