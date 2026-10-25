/*
  codegen.cpp

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

// Lua: code generation (one WebAssembly function for each Lua function, and one for the chunk); see compiler.hpp
//
// Calls of the program's own functions go through the dispatcher (a function that calls the program's function whose number it
// is given); others through the runtime. Arithmetic and comparisons of integers (that fit inline) and floats are compiled inline;
// the runtime does the rest (strings' coercions, metamethods, errors).

#include <neolib/neolib.hpp>
#include <algorithm>
#include <set>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <neos/bytecode/bytecode.hpp>
#include <neos/language/emit.hpp>
#include "compiler.hpp"

namespace neos::compilers::lua
{
    using namespace emitter;

    namespace
    {
        constexpr type I64 = type::I64;
        constexpr type I32 = type::I32;
        constexpr type F64 = type::F64;
        constexpr type STR = type::String;

        // (the runtime's arithmetic operations (lua::arith), in the order of their metamethods)
        enum : std::int32_t
        {
            OpAdd, OpSub, OpMul, OpMod, OpPow, OpDiv, OpIDiv, OpBAnd, OpBOr, OpBXor, OpShl, OpShr
        };

        std::int32_t arithmetic_operation(construct aOperator)
        {
            switch (aOperator)
            {
            case construct::Add: return OpAdd;
            case construct::Subtract: return OpSub;
            case construct::Multiply: return OpMul;
            case construct::Modulo: return OpMod;
            case construct::Power: return OpPow;
            case construct::Divide: return OpDiv;
            case construct::FloorDivide: return OpIDiv;
            case construct::BitAnd: return OpBAnd;
            case construct::BitOr: return OpBOr;
            case construct::BitXor: return OpBXor;
            case construct::ShiftLeft: return OpShl;
            case construct::ShiftRight: return OpShr;
            default: return -1;
            }
        }

        bool is_arithmetic(construct aKind)
        {
            return arithmetic_operation(aKind) >= 0;
        }

        bool is_comparison(construct aKind)
        {
            switch (aKind)
            {
            case construct::Less:
            case construct::Greater:
            case construct::LessEqual:
            case construct::GreaterEqual:
            case construct::Equal:
            case construct::NotEqual:
                return true;
            default:
                return false;
            }
        }

        int hex_value(char aCharacter)
        {
            if (aCharacter >= '0' && aCharacter <= '9')
                return aCharacter - '0';
            if (aCharacter >= 'a' && aCharacter <= 'f')
                return aCharacter - 'a' + 10;
            if (aCharacter >= 'A' && aCharacter <= 'F')
                return aCharacter - 'A' + 10;
            return -1;
        }

        void append_utf8(std::string& aResult, std::uint32_t aCodePoint)
        {
            // (Lua's UTF-8: code points up to 2^31 (up to six bytes))
            if (aCodePoint < 0x80u)
                aResult.push_back(static_cast<char>(aCodePoint));
            else if (aCodePoint < 0x800u)
            {
                aResult.push_back(static_cast<char>(0xC0u | (aCodePoint >> 6)));
                aResult.push_back(static_cast<char>(0x80u | (aCodePoint & 0x3Fu)));
            }
            else if (aCodePoint < 0x10000u)
            {
                aResult.push_back(static_cast<char>(0xE0u | (aCodePoint >> 12)));
                aResult.push_back(static_cast<char>(0x80u | ((aCodePoint >> 6) & 0x3Fu)));
                aResult.push_back(static_cast<char>(0x80u | (aCodePoint & 0x3Fu)));
            }
            else if (aCodePoint < 0x200000u)
            {
                aResult.push_back(static_cast<char>(0xF0u | (aCodePoint >> 18)));
                aResult.push_back(static_cast<char>(0x80u | ((aCodePoint >> 12) & 0x3Fu)));
                aResult.push_back(static_cast<char>(0x80u | ((aCodePoint >> 6) & 0x3Fu)));
                aResult.push_back(static_cast<char>(0x80u | (aCodePoint & 0x3Fu)));
            }
            else if (aCodePoint < 0x4000000u)
            {
                aResult.push_back(static_cast<char>(0xF8u | (aCodePoint >> 24)));
                aResult.push_back(static_cast<char>(0x80u | ((aCodePoint >> 18) & 0x3Fu)));
                aResult.push_back(static_cast<char>(0x80u | ((aCodePoint >> 12) & 0x3Fu)));
                aResult.push_back(static_cast<char>(0x80u | ((aCodePoint >> 6) & 0x3Fu)));
                aResult.push_back(static_cast<char>(0x80u | (aCodePoint & 0x3Fu)));
            }
            else
            {
                aResult.push_back(static_cast<char>(0xFCu | (aCodePoint >> 30)));
                aResult.push_back(static_cast<char>(0x80u | ((aCodePoint >> 24) & 0x3Fu)));
                aResult.push_back(static_cast<char>(0x80u | ((aCodePoint >> 18) & 0x3Fu)));
                aResult.push_back(static_cast<char>(0x80u | ((aCodePoint >> 12) & 0x3Fu)));
                aResult.push_back(static_cast<char>(0x80u | ((aCodePoint >> 6) & 0x3Fu)));
                aResult.push_back(static_cast<char>(0x80u | (aCodePoint & 0x3Fu)));
            }
        }

        bool is_space(char c)
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
        }

        // a string literal's value (its bytes): a short string's escape sequences resolved, a long string's first line break skipped
        // (and its line breaks LF)
        std::string string_value(compiler const& aCompiler, std::string_view aText)
        {
            std::string result;
            if (aText.empty())
                return result;
            if (aText[0] == '[')
            {
                // (a long string: [=*[ ... ]=*])
                std::size_t level = 0u;
                while (1u + level < aText.size() && aText[1u + level] == '=')
                    ++level;
                auto body = aText.substr(level + 2u, aText.size() - 2u * level - 4u);
                if (!body.empty() && (body[0] == '\r' || body[0] == '\n'))
                {
                    auto const skip = body.size() > 1u && (body[1] == '\r' || body[1] == '\n') && body[1] != body[0] ? 2u : 1u;
                    body.remove_prefix(skip);
                }
                for (std::size_t i = 0u; i < body.size(); ++i)
                {
                    if (body[i] == '\r' || body[i] == '\n')
                    {
                        if (i + 1u < body.size() && (body[i + 1u] == '\r' || body[i + 1u] == '\n') && body[i + 1u] != body[i])
                            ++i;
                        result.push_back('\n');
                    }
                    else
                        result.push_back(body[i]);
                }
                return result;
            }
            auto const text = aText.substr(1u, aText.size() - 2u);
            for (std::size_t i = 0u; i < text.size();)
            {
                char const c = text[i];
                if (c != '\\')
                {
                    result.push_back(c);
                    ++i;
                    continue;
                }
                if (++i >= text.size())
                    aCompiler.error(aText, "unfinished string");
                char const e = text[i++];
                switch (e)
                {
                case 'a': result.push_back('\a'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'v': result.push_back('\v'); break;
                case '\\': result.push_back('\\'); break;
                case '\"': result.push_back('\"'); break;
                case '\'': result.push_back('\''); break;
                case '\n':
                case '\r':
                    // (a line break (\r\n, \n\r: one))
                    if (i < text.size() && (text[i] == '\n' || text[i] == '\r') && text[i] != e)
                        ++i;
                    result.push_back('\n');
                    break;
                case 'z':
                    while (i < text.size() && is_space(text[i]))
                        ++i;
                    break;
                case 'x':
                    {
                        if (i + 2u > text.size() || hex_value(text[i]) < 0 || hex_value(text[i + 1u]) < 0)
                            aCompiler.error(aText, "hexadecimal digit expected near '\\x" + std::string{ text.substr(i, 2u) } + "'");
                        result.push_back(static_cast<char>(hex_value(text[i]) * 16 + hex_value(text[i + 1u])));
                        i += 2u;
                    }
                    break;
                case 'u':
                    {
                        if (i >= text.size() || text[i] != '{')
                            aCompiler.error(aText, "missing '{' in \\u{xxxx}");
                        ++i;
                        std::uint64_t codePoint = 0u;
                        std::size_t digits = 0u;
                        while (i < text.size() && hex_value(text[i]) >= 0)
                        {
                            codePoint = codePoint * 16u + static_cast<std::uint64_t>(hex_value(text[i++]));
                            ++digits;
                            if (codePoint > 0x7FFFFFFFull)
                                aCompiler.error(aText, "UTF-8 value too large");
                        }
                        if (digits == 0u)
                            aCompiler.error(aText, "hexadecimal digit expected");
                        if (i >= text.size() || text[i] != '}')
                            aCompiler.error(aText, "missing '}' in \\u{xxxx}");
                        ++i;
                        append_utf8(result, static_cast<std::uint32_t>(codePoint));
                    }
                    break;
                default:
                    if (e >= '0' && e <= '9')
                    {
                        unsigned value = static_cast<unsigned>(e - '0');
                        for (int k = 0; k < 2 && i < text.size() && text[i] >= '0' && text[i] <= '9'; ++k)
                            value = value * 10u + static_cast<unsigned>(text[i++] - '0');
                        if (value > 255u)
                            aCompiler.error(aText, "decimal escape too large");
                        result.push_back(static_cast<char>(value));
                    }
                    else
                        aCompiler.error(aText, "invalid escape sequence near '" + std::string{ aText.substr(0u, static_cast<std::size_t>(text.data() - aText.data()) + i) } + "'");
                    break;
                }
            }
            return result;
        }

        // a numeral's value: an integer (a decimal one that overflows is a float; a hexadecimal one wraps around) or a float
        std::variant<std::int64_t, double> number_value(std::string_view aText)
        {
            std::string const text{ aText };
            bool const hex = text.size() > 1u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
            bool const isFloat = hex ?
                (text.find_first_of(".pP") != std::string::npos) :
                (text.find_first_of(".eE") != std::string::npos);
            if (!isFloat)
            {
                if (hex)
                {
                    std::uint64_t value = 0u;
                    for (std::size_t i = 2u; i < text.size(); ++i)
                        value = value * 16u + static_cast<std::uint64_t>(hex_value(text[i]));
                    return static_cast<std::int64_t>(value);
                }
                std::uint64_t value = 0u;
                bool overflow = false;
                for (auto c : text)
                {
                    auto const digit = static_cast<std::uint64_t>(c - '0');
                    if (value > (0x7FFFFFFFFFFFFFFFull - digit) / 10u)
                        overflow = true;
                    value = value * 10u + digit;
                }
                if (!overflow)
                    return static_cast<std::int64_t>(value);
            }
            return std::strtod(text.c_str(), nullptr);
        }

        // (a string literal as the runtime's lua::string receives it: NUL as 01 02, 01 as 01 01)
        std::string literal_encoding(std::string_view aText)
        {
            std::string result;
            result.reserve(aText.size());
            for (auto c : aText)
            {
                if (c == '\0')
                {
                    result.push_back('\x01');
                    result.push_back('\x02');
                }
                else if (c == '\x01')
                {
                    result.push_back('\x01');
                    result.push_back('\x01');
                }
                else
                    result.push_back(c);
            }
            return result;
        }

        std::uint64_t float_value(double aValue)
        {
            std::uint64_t bits;
            std::memcpy(&bits, &aValue, sizeof(bits));
            return bits;
        }

        std::uint64_t integer_value(std::int64_t aValue)
        {
            return values::TagInteger | (static_cast<std::uint64_t>(aValue) & values::PayloadMask);
        }

        bool inline_integer(std::int64_t aValue)
        {
            return aValue >= values::MinInline && aValue <= values::MaxInline;
        }

        // (NEOS_LUA_WASM=<file>: each function's code is appended to the file (for debugging code generation))
        char const* wasm_dump_path()
        {
            static char const* const sPath = std::getenv("NEOS_LUA_WASM");
            return sPath;
        }

        std::map<std::uint32_t, std::string>& wasm_dump_names()
        {
            static std::map<std::uint32_t, std::string> sNames;
            return sNames;
        }

        // (an expression whose value is an integer: an integer numeral, an integer variable or arithmetic of them (the result of + - * of
        // integers, and of // and % (else an error), is an integer))
        bool integer_expression(node const* aNode)
        {
            while (aNode->is(construct::Parenthesized))
                aNode = aNode->child(0u);
            switch (aNode->kind)
            {
            case construct::Number:
                return std::holds_alternative<std::int64_t>(number_value(aNode->source));
            case construct::Identifier:
                return aNode->resolved != nullptr && aNode->resolved->integer;
            case construct::Add:
            case construct::Subtract:
            case construct::Multiply:
            case construct::FloorDivide:
            case construct::Modulo:
                return integer_expression(aNode->child(0u)) && integer_expression(aNode->child(1u));
            case construct::Negate:
                return integer_expression(aNode->child(0u));
            default:
                return false;
            }
        }

        // (an expression whose value is a float: a float numeral, a float variable, / of integer or float expressions, and + - * // of 
        // them, at least one a float's; unary - of one)
        bool float_expression(node const* aNode)
        {
            while (aNode->is(construct::Parenthesized))
                aNode = aNode->child(0u);
            auto const numeric = [](node const* n) { return integer_expression(n) || float_expression(n); };
            switch (aNode->kind)
            {
            case construct::Number:
                return std::holds_alternative<double>(number_value(aNode->source));
            case construct::Identifier:
                return aNode->resolved != nullptr && aNode->resolved->floating;
            case construct::Divide:
                return numeric(aNode->child(0u)) && numeric(aNode->child(1u));
            case construct::Add:
            case construct::Subtract:
            case construct::Multiply:
            case construct::FloorDivide:
                return numeric(aNode->child(0u)) && numeric(aNode->child(1u)) && (float_expression(aNode->child(0u)) || float_expression(aNode->child(1u)));
            case construct::Negate:
                return float_expression(aNode->child(0u));
            default:
                return false;
            }
        }
    }

    void compiler::infer_integers(node* aChunk)
    {
        // (each candidate's assignments' values; a variable that may have another value (nil, a call's) is not a candidate)
        // (candidates: variables declared by local statements and numeric fors (not parameters, generic fors' variables or local 
        // functions' names))
        std::map<variable*, std::vector<node*>> assignments;
        std::set<variable*> declared;
        std::set<variable*> excluded;
        // (local functions (local function f, local f = function): constant if they are not assigned again)
        std::set<variable*> assigned;
        std::vector<std::pair<variable*, function_info*>> functions;
        auto const assign = [&](variable* aVariable, std::vector<node*> const& aExpressions, std::size_t aIndex)
        {
            if (aVariable == nullptr)
                return;
            if (aIndex < aExpressions.size() && !(aIndex + 1u == aExpressions.size() && is_multiple(aExpressions[aIndex])))
                assignments[aVariable].push_back(aExpressions[aIndex]);
            else
                excluded.insert(aVariable);
        };
        std::vector<node*> pending{ aChunk };
        while (!pending.empty())
        {
            auto* n = pending.back();
            pending.pop_back();
            for (auto* c : n->children)
                pending.push_back(c);
            if (n->is(construct::Local))
            {
                std::vector<variable*> variables;
                std::vector<node*> expressions;
                for (auto* c : n->children)
                {
                    if (c->is(construct::AttributeName))
                        variables.push_back(c->child(0u)->declared);
                    else
                        expressions.push_back(c);
                }
                if (variables.size() == 1u && expressions.size() == 1u && expressions[0]->is(construct::Function) && variables[0] != nullptr &&
                    variables[0]->attrib != attribute::Close)
                    functions.emplace_back(variables[0], expressions[0]->function);
                for (std::size_t i = 0u; i < variables.size(); ++i)
                {
                    declared.insert(variables[i]);
                    assign(variables[i], expressions, i);
                    if (variables[i] != nullptr && variables[i]->attrib == attribute::Close)
                        excluded.insert(variables[i]);
                }
            }
            else if (n->is(construct::Assign))
            {
                std::vector<node*> const expressions{ n->children.begin() + 1, n->children.end() };
                auto const& targets = n->child(0u)->children;
                for (std::size_t i = 0u; i < targets.size(); ++i)
                    if (targets[i]->is(construct::Identifier))
                    {
                        assign(targets[i]->resolved, expressions, i);
                        assigned.insert(targets[i]->resolved);
                    }
            }
            else if (n->is(construct::LocalFunction))
                functions.emplace_back(n->child(0u)->declared, n->function);
            else if (n->is(construct::FunctionStatement) && n->child(0u)->size() == 1u)
                assigned.insert(n->child(0u)->child(0u)->resolved);
            else if (n->is(construct::NumericFor))
            {
                auto* v = n->child(0u)->declared;
                if (v != nullptr)
                {
                    declared.insert(v);
                    assignments[v].push_back(n->child(1u));
                    if (first_statement(n) > 3u)
                        assignments[v].push_back(n->child(3u));
                }
            }
        }
        for (auto const& [v, f] : functions)
            if (v != nullptr && f != nullptr && !assigned.contains(v))
                v->constant = f;
        for (auto& [v, expressions] : assignments)
            v->integer = declared.contains(v) && !v->captured && !excluded.contains(v);
        auto const solve = [&](std::vector<variable*> const& aVariables)
        {
            for (bool changed = true; changed;)
            {
                changed = false;
                for (auto* v : aVariables)
                {
                    if (!v->integer)
                        continue;
                    auto const a = assignments.find(v);
                    if (a == assignments.end())
                        continue;
                    if (!std::all_of(a->second.begin(), a->second.end(), [](node const* e) { return integer_expression(e); }))
                    {
                        v->integer = false;
                        changed = true;
                    }
                }
            }
        };
        std::vector<variable*> all;
        for (auto& [v, expressions] : assignments)
            all.push_back(v);
        solve(all);
        // (the candidates that are not integers: floats if all their values are floats (not numeric fors' variables))
        std::set<variable*> loops;
        for (auto& [v, expressions] : assignments)
            if (v->declaration != nullptr && v->declaration->parent != nullptr && v->declaration->parent->is(construct::NumericFor))
                loops.insert(v);
        for (auto& [v, expressions] : assignments)
            v->floating = !v->integer && declared.contains(v) && !v->captured && !excluded.contains(v) && !loops.contains(v);
        for (bool changed = true; changed;)
        {
            changed = false;
            for (auto& [v, expressions] : assignments)
                if (v->floating && !std::all_of(expressions.begin(), expressions.end(), [](node const* e) { return float_expression(e); }))
                {
                    v->floating = false;
                    changed = true;
                }
        }
        // (each function's variables if its parameters are integers (they are, if they are not captured and are assigned only integers))
        std::map<function_info*, std::vector<variable*>> owned;
        for (auto& [v, expressions] : assignments)
            if (v->function != nullptr && declared.contains(v) && !v->captured && !excluded.contains(v))
                owned[v->function].push_back(v);
        for (auto& f : iFunctions)
        {
            std::vector<variable*> parameters;
            for (auto* p : f.parameters)
                if (p->declaration != nullptr && !p->captured)
                    parameters.push_back(p);
            if (parameters.empty())
                continue;
            auto variables = owned[&f];
            std::vector<variable*> base;
            for (auto* v : variables)
                if (v->integer)
                    base.push_back(v);
            for (auto* v : variables)
                v->integer = true;
            for (auto* p : parameters)
            {
                p->integer = true;
                variables.push_back(p);
            }
            solve(variables);
            for (auto* p : parameters)
                if (p->integer)
                    f.speculativeParameters.push_back(p);
            if (!f.speculativeParameters.empty())
                for (auto* v : variables)
                    if (v->integer)
                        f.speculative.push_back(v);
            for (auto* v : variables)
                v->integer = false;
            for (auto* v : base)
                v->integer = true;
        }
    }

    // --- code generation (the module)

    void compiler::generate(node* aChunk)
    {
        try
        {
            analyse(aChunk);
            infer_integers(aChunk);
            iLiterals.clear();
            iLiteralOrder.clear();
            iGlobalIndex.clear();
            iGlobalIndexed = 0u;
            iFrameNames.clear();
            iResultsGlobal = global_index("%lua.nret", I64);
            iProgramGlobal = global_index("%lua.pid", I64);
            // (the functions: their WebAssembly functions)
            function_signature signature;
            signature.set_return_type(I64);
            {
                function_parameters parameters;
                for (auto t : { I64, I32, I64, I64, I64, I64 })
                {
                    function_parameter p;
                    p.set_parameter_type(t);
                    parameters.push_back(p);
                }
                signature.set_parameters(parameters);
            }
            std::uint32_t number = 0u;
            for (auto& f : iFunctions)
            {
                signature.set_name(neolib::string{ "%lua." + std::to_string(number) });
                f.number = number;
                f.index = generator().define_function(neolib::string{ "%lua." + std::to_string(number) }, signature, neolib::vector<type>{},
                    neolib::string{ f.name }, f.chunk ? 1u : f.line);
                iFrameNames[f.index] = f.name;
                ++number;
            }
            // (the dispatcher: (function number, callee, count, arguments) -> result)
            {
                function_signature dispatcher;
                dispatcher.set_return_type(I64);
                function_parameters parameters;
                for (auto t : { I32, I64, I32, I64, I64, I64, I64 })
                {
                    function_parameter p;
                    p.set_parameter_type(t);
                    parameters.push_back(p);
                }
                dispatcher.set_parameters(parameters);
                dispatcher.set_name(neolib::string{ "%lua.dispatch" });
                iDispatcher = generator().define_function(neolib::string{ "%lua.dispatch" }, dispatcher, neolib::vector<type>{}, neolib::string{}, 1u);
            }
            // (each function's code)
            for (auto& f : iFunctions)
                function_generator{ *this, f }.generate();
            // the dispatcher's code: a binary search of the function numbers (each leaf calls its function and returns)
            {
                std::vector<std::uint32_t> targets;
                for (auto& f : iFunctions)
                    targets.push_back(f.index);
                fragment body;
                auto& code = body.fragmentCode.as_std_vector();
                std::function<void(std::uint32_t, std::uint32_t)> search = [&](std::uint32_t aLow, std::uint32_t aHigh)
                {
                    if (aHigh - aLow == 1u)
                    {
                        for (std::uint32_t p = 1u; p <= 6u; ++p)
                        {
                            put_op(code, opcode::LocalGet);
                            put_u32(code, p);
                        }
                        put_op(code, opcode::CallFunction);
                        body.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), targets[aLow] });
                        code.resize(code.size() + 5u);
                        put_op(code, opcode::Return);
                        return;
                    }
                    auto const middle = aLow + (aHigh - aLow) / 2u;
                    put_op(code, opcode::LocalGet);
                    put_u32(code, 0u);
                    put_op(code, opcode::I32Const);
                    put_s64(code, static_cast<std::int32_t>(middle));
                    put_op(code, opcode::I32LtU);
                    put_op(code, opcode::If);
                    code.push_back(0x40u);
                    search(aLow, middle);
                    put_op(code, opcode::Else);
                    search(middle, aHigh);
                    put_op(code, opcode::End);
                };
                if (!targets.empty())
                    search(0u, static_cast<std::uint32_t>(targets.size()));
                put_op(code, opcode::I64Const);
                put_s64(code, static_cast<std::int64_t>(values::Nil));
                generator().set_function_body(iDispatcher, body);
            }
            // the entry point: the runtime is initialized, the literals are created and the chunk's function is called (an error
            // that it does not handle is reported); a chunk that load compiles: its function is the program's result
            ensure_function();
            auto statement = fragment{};
            statement.fragmentKind = fragment_kind::Statement;
            auto& code = statement.fragmentCode.as_std_vector();
            auto const call = [&](std::string const& aName, std::vector<type> const& aParameters, type aResult)
            {
                emit_call_to(statement, runtime_function("lua::" + aName, aParameters, aResult));
            };
            auto const function_index = [&](std::uint32_t aIndex)
            {
                put_op(code, opcode::I32Const);
                statement.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), aIndex });
                code.resize(code.size() + 5u);
            };
            function_index(iDispatcher);
            put_op(code, opcode::I32Const);
            put_s64(code, static_cast<std::int32_t>(iResultsGlobal));
            call("init", { I32, I32 }, I64);
            put_op(code, opcode::GlobalSet);
            put_u32(code, iProgramGlobal);
            for (auto const& [text, global] : iLiteralOrder)
            {
                put_op(code, opcode::I32Const);
                put_s64(code, static_cast<std::int32_t>(string_literal(literal_encoding(text))));
                call("string", { STR }, I64);
                put_op(code, opcode::GlobalSet);
                put_u32(code, global);
            }
            auto& main = iFunctions.front();
            put_op(code, opcode::I32Const);
            put_s64(code, static_cast<std::int32_t>(main.number));
            function_index(main.index);
            call("main_closure", { I32, I32 }, I64);
            if (iLoad)
            {
                call("load_complete", { I64 }, I64);
                put_op(code, opcode::Drop);
            }
            else
            {
                // (an error that the chunk did not handle (the program's exit status is 1), or os.exit (its status))
                call("run_main", { I64 }, I32);
                put_op(code, opcode::If);
                code.push_back(0x40u);
                call("exit_status", {}, I32);
                emit_call_to(statement, runtime_function("neos::exit", { I32 }, type::Void));
                put_op(code, opcode::End);
            }
            push_operand(operand_type{ statement });
            generator().end_indented();
        }
        catch (syntax_error const& e)
        {
            auto const position = neolib::string_view{ e.source.empty() ? iSource : e.source };
            generator().throw_error(position.begin(), neolib::string{ e.what() }, neolib::string{ "syntax error" });
        }
    }

    // --- code generation (a function)

    function_generator::function_generator(compiler& aCompiler, function_info& aFunction) :
        iCompiler{ aCompiler }, iFunction{ aFunction }
    {
    }

    void function_generator::op(opcode aOpcode)
    {
        put_op(iCode, aOpcode);
    }

    void function_generator::u32(std::uint32_t aValue)
    {
        put_u32(iCode, aValue);
    }

    void function_generator::i32(std::int32_t aValue)
    {
        op(opcode::I32Const);
        put_s64(iCode, aValue);
    }

    void function_generator::i64(std::int64_t aValue)
    {
        op(opcode::I64Const);
        put_s64(iCode, aValue);
    }

    void function_generator::f64(double aValue)
    {
        op(opcode::F64Const);
        put_raw(iCode, aValue);
    }

    void function_generator::get(std::uint32_t aLocal)
    {
        op(opcode::LocalGet);
        u32(aLocal);
    }

    void function_generator::set(std::uint32_t aLocal)
    {
        op(opcode::LocalSet);
        u32(aLocal);
    }

    void function_generator::tee(std::uint32_t aLocal)
    {
        op(opcode::LocalTee);
        u32(aLocal);
    }

    void function_generator::drop()
    {
        op(opcode::Drop);
    }

    std::uint32_t function_generator::local(type aType)
    {
        iLocals.push_back(aType);
        return ParameterCount + static_cast<std::uint32_t>(iLocals.size() - 1u);
    }

    std::uint32_t function_generator::temporary(type aType)
    {
        for (auto i = iFreeTemporaries.begin(); i != iFreeTemporaries.end(); ++i)
            if (iLocals[*i - ParameterCount] == aType)
            {
                auto const result = *i;
                iFreeTemporaries.erase(i);
                return result;
            }
        return local(aType);
    }

    void function_generator::release(std::uint32_t aLocal)
    {
        iFreeTemporaries.push_back(aLocal);
    }

    void function_generator::call(std::uint32_t aFunction)
    {
        op(opcode::CallFunction);
        iRelocations.emplace_back(static_cast<std::uint32_t>(iCode.size()), aFunction);
        iCode.resize(iCode.size() + 5u);
    }

    void function_generator::runtime(std::string const& aName, std::vector<type> const& aParameters, type aResult)
    {
        auto const index = iCompiler.runtime_function("lua::" + aName, aParameters, aResult);
        if (wasm_dump_path() != nullptr)
            wasm_dump_names()[index] = aName;
        call(index);
    }

    void function_generator::begin(control_kind aKind, std::optional<type> aResult)
    {
        op(aKind == control_kind::Loop ? opcode::Loop : opcode::Block);
        iCode.push_back(aResult ? *wasm_type(*aResult) : 0x40u);
        iControls.push_back(control{ aKind, iTbcCount });
    }

    void function_generator::begin_if(std::optional<type> aResult)
    {
        op(opcode::If);
        iCode.push_back(aResult ? *wasm_type(*aResult) : 0x40u);
        iControls.push_back(control{ control_kind::Block, iTbcCount });
    }

    void function_generator::else_()
    {
        op(opcode::Else);
    }

    void function_generator::end_control()
    {
        op(opcode::End);
        iControls.pop_back();
    }

    std::size_t function_generator::break_control() const
    {
        for (std::size_t i = iControls.size(); i-- > 0u;)
            if (iControls[i].kind == control_kind::Break)
                return i;
        return iControls.size();
    }

    void function_generator::branch(std::size_t aControl, bool aConditional)
    {
        op(aConditional ? opcode::BrIf : opcode::Br);
        u32(static_cast<std::uint32_t>(depth_of(aControl)));
    }

    void function_generator::check()
    {
        tee(iScratch);
        constant(values::Exception);
        op(opcode::I64Eq);
        branch(exit_control(), true);
        get(iScratch);
    }

    void function_generator::check_drop()
    {
        check();
        drop();
    }

    void function_generator::literal_string(std::string const& aText)
    {
        op(opcode::GlobalGet);
        u32(iCompiler.literal_string(aText));
    }

    void function_generator::description(std::string const& aText)
    {
        if (aText.empty())
            nil();
        else
            literal_string(aText);
    }

    void function_generator::set_results(std::uint32_t aCount)
    {
        i64(aCount);
        op(opcode::GlobalSet);
        u32(iCompiler.iResultsGlobal);
    }

    void function_generator::line_of(node* aNode)
    {
        auto const line = iCompiler.source_line_of(aNode->source);
        i64(static_cast<std::int64_t>(bytecode::LineMarker | line));
        drop();
        iLastLine = line;
    }

    // --- values (inline)

    void function_generator::is_integer()
    {
        i64(48);
        op(opcode::I64ShrU);
        i64(static_cast<std::int64_t>(values::TagInteger >> 48u));
        op(opcode::I64Eq);
    }

    void function_generator::is_float()
    {
        constant(values::TagSpecial);
        op(opcode::I64LtU);
    }

    void function_generator::unbox_integer()
    {
        i64(16);
        op(opcode::I64Shl);
        i64(16);
        op(opcode::I64ShrS);
    }

    void function_generator::box_integer()
    {
        // (an i64: inline if it fits in 48 bits, else a runtime cell)
        auto const v = temporary();
        tee(v);
        i64(16);
        op(opcode::I64Shl);
        i64(16);
        op(opcode::I64ShrS);
        get(v);
        op(opcode::I64Eq);
        begin_if(I64);
        get(v);
        constant(values::PayloadMask);
        op(opcode::I64And);
        constant(values::TagInteger);
        op(opcode::I64Ior);
        else_();
        get(v);
        runtime("int_box", { I64 }, I64);
        check();
        end_control();
        release(v);
    }

    void function_generator::truth()
    {
        // (nil and false (TagSpecial | 0, TagSpecial | 1) are false)
        i64(1);
        op(opcode::I64Ior);
        constant(values::False | 1u);
        op(opcode::I64Ne);
    }

    // --- the function

    void function_generator::generate()
    {
        iScratch = local();
        iArgumentsBase = local(I32);
        if (iFunction.hasClose)
            iTbcMark = local(I32);
        begin(control_kind::Exit);
        // (the prologue: the parameters and the varargs)
        line_of(iFunction.declaration);
        bool const extra = iFunction.vararg || iFunction.parameters.size() > RegisterArguments;
        if (extra)
        {
            get(ArgcLocal);
            runtime("args_base", { I32 }, I32);
            set(iArgumentsBase);
        }
        if (iFunction.hasClose)
        {
            runtime("tbc_mark", {}, I32);
            set(iTbcMark);
        }
        for (std::size_t i = 0u; i < iFunction.parameters.size(); ++i)
        {
            auto* p = iFunction.parameters[i];
            if (i < RegisterArguments)
            {
                if (!p->captured)
                {
                    // (its local is the argument's)
                    p->local = FirstArgumentLocal + static_cast<std::uint32_t>(i);
                    continue;
                }
                get(FirstArgumentLocal + static_cast<std::uint32_t>(i));
            }
            else
            {
                get(ArgcLocal);
                get(iArgumentsBase);
                i32(static_cast<std::int32_t>(i));
                runtime("argument", { I32, I32, I32 }, I64);
            }
            declare(p);
        }
        if (iFunction.vararg)
        {
            iVarargs = local();
            get(ArgcLocal);
            i32(static_cast<std::int32_t>(iFunction.parameters.size()));
            for (std::uint32_t i = 0u; i < RegisterArguments; ++i)
                get(FirstArgumentLocal + i);
            get(iArgumentsBase);
            runtime("varargs", { I32, I32, I64, I64, I64, I64, I32 }, I64);
            check();
            set(*iVarargs);
        }
        // (the body; a function that ends without a return statement returns nothing)
        auto* declaration = iFunction.declaration;
        auto const first = compiler::first_statement(declaration);
        if (!iFunction.speculativeParameters.empty())
        {
            // (its integer parameters' arguments are integers (inline): its version for them, whose integer variables are i64s)
            bool firstCheck = true;
            for (auto* p : iFunction.speculativeParameters)
            {
                get(p->local);
                is_integer();
                if (!firstCheck)
                    op(opcode::I32And);
                firstCheck = false;
            }
            begin_if();
            std::vector<std::pair<variable*, std::uint32_t>> boxed;
            for (auto* p : iFunction.speculativeParameters)
            {
                auto const raw = local();
                get(p->local);
                unbox_integer();
                set(raw);
                boxed.emplace_back(p, p->local);
                p->local = raw;
            }
            std::vector<variable*> base;
            for (auto* v : iFunction.speculative)
            {
                if (v->integer)
                    base.push_back(v);
                v->integer = true;
            }
            block(declaration, compiler::statements_of(declaration, first), first);
            close_to(0u);
            set_results(0u);
            nil();
            op(opcode::Return);
            for (auto* v : iFunction.speculative)
                v->integer = false;
            for (auto* v : base)
                v->integer = true;
            for (auto const& [p, l] : boxed)
                p->local = l;
            end_control();
        }
        block(declaration, compiler::statements_of(declaration, first), first);
        close_to(0u);
        set_results(0u);
        nil();
        op(opcode::Return);
        end_control();
        // (an error: its to-be-closed variables are closed, and it returns Exception)
        if (iFunction.hasClose)
        {
            get(iTbcMark);
            runtime("tbc_close_error", { I32 }, I64);
            drop();
        }
        constant(values::Exception);
        fragment code;
        code.fragmentCode.as_std_vector() = iCode;
        for (auto const& r : iRelocations)
            code.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ r.first, r.second });
        if (auto const path = wasm_dump_path())
            if (auto* file = std::fopen(path, "ab"))
            {
                std::fprintf(file, "function %u %s\nlocals", iFunction.index, iFunction.name.c_str());
                for (auto t : iLocals)
                    std::fprintf(file, " %d", static_cast<int>(t));
                std::fprintf(file, "\ncalls");
                for (auto const& r : iRelocations)
                {
                    auto const name = wasm_dump_names().find(r.second);
                    std::fprintf(file, " %u:%s", r.first, name != wasm_dump_names().end() ? name->second.c_str() : std::to_string(r.second).c_str());
                }
                std::fprintf(file, "\ncode ");
                for (auto b : iCode)
                    std::fprintf(file, "%02x", b);
                std::fprintf(file, "\n");
                std::fclose(file);
            }
        iCompiler.generator().set_function_body(iFunction.index, code);
        iCompiler.generator().set_function_locals(iFunction.index, neolib::vector<type>{ iLocals.begin(), iLocals.end() });
    }

    // --- variables

    void function_generator::to_raw_integer()
    {
        auto const v = temporary();
        tee(v);
        is_integer();
        begin_if(I64);
        get(v);
        unbox_integer();
        else_();
        get(v);
        runtime("int_value", { I64 }, I64);
        end_control();
        release(v);
    }

    void function_generator::declare(variable* aVariable)
    {
        aVariable->local = local();
        if (aVariable->integer)
        {
            to_raw_integer();
            set(aVariable->local);
            return;
        }
        if (aVariable->floating)
        {
            set(aVariable->local);
            return;
        }
        if (aVariable->captured)
        {
            runtime("box_new", { I64 }, I64);
            check();
        }
        set(aVariable->local);
        if (aVariable->attrib == attribute::Close)
        {
            load(aVariable);
            literal_string(aVariable->name);
            runtime("tbc_push", { I64, I64 }, I64);
            check_drop();
            aVariable->tbcDepth = static_cast<std::uint32_t>(iTbcCount++);
        }
    }

    void function_generator::load(variable* aVariable)
    {
        if (aVariable->integer)
        {
            get(aVariable->local);
            box_integer();
            return;
        }
        if (aVariable->floating)
        {
            get(aVariable->local);
            return;
        }
        if (aVariable->function != &iFunction)
        {
            get(CalleeLocal);
            i32(static_cast<std::int32_t>(compiler::upvalue_index(iFunction, aVariable)));
            runtime("upval_get", { I64, I32 }, I64);
            return;
        }
        get(aVariable->local);
        if (aVariable->captured)
            runtime("box_get", { I64 }, I64);
    }

    void function_generator::store(variable* aVariable)
    {
        if (aVariable->integer)
        {
            to_raw_integer();
            set(aVariable->local);
            return;
        }
        if (aVariable->floating)
        {
            set(aVariable->local);
            return;
        }
        auto const v = temporary();
        set(v);
        if (aVariable->function != &iFunction)
        {
            get(CalleeLocal);
            i32(static_cast<std::int32_t>(compiler::upvalue_index(iFunction, aVariable)));
            get(v);
            runtime("upval_set", { I64, I32, I64 }, I64);
            drop();
        }
        else if (aVariable->captured)
        {
            get(aVariable->local);
            get(v);
            runtime("box_set", { I64, I64 }, I64);
            drop();
        }
        else
        {
            get(v);
            set(aVariable->local);
        }
        release(v);
    }

    void function_generator::box_of(variable* aVariable)
    {
        if (aVariable->function != &iFunction)
        {
            get(CalleeLocal);
            i32(static_cast<std::int32_t>(compiler::upvalue_index(iFunction, aVariable)));
            runtime("upval_box", { I64, I32 }, I64);
            return;
        }
        get(aVariable->local);
    }

    // --- statements

    void function_generator::close_to(std::size_t aCount)
    {
        if (iTbcCount <= aCount)
            return;
        get(iTbcMark);
        i32(static_cast<std::int32_t>(aCount));
        op(opcode::I32Add);
        runtime("tbc_close", { I32 }, I64);
        check_drop();
    }

    void function_generator::block(node* aOwner, std::vector<node*> const& aStatements, std::size_t, bool)
    {
        // a statement list: its labels' gotos are branches: a forward goto's label ends a block (that begins at the earliest of its
        // gotos), a backward goto's label begins a loop (that ends with the list); they must nest
        std::vector<label_info*> labels;
        for (auto* s : aStatements)
            if (s->is(construct::Label))
                labels.push_back(s->label);
        // (the to-be-closed variables in scope at each label)
        {
            std::size_t count = iTbcCount;
            std::size_t next = 0u;
            for (std::size_t i = 0u; i < aStatements.size(); ++i)
            {
                if (next < labels.size() && labels[next]->position == i)
                    labels[next++]->tbcCount = count;
                if (aStatements[i]->is(construct::Local))
                    for (auto* c : aStatements[i]->children)
                        if (c->is(construct::AttributeName) && c->child(0u)->declared != nullptr && c->child(0u)->declared->attrib == attribute::Close)
                            ++count;
            }
        }
        // (forward blocks that would overlap others begin earlier; one that would overlap a loop cannot be compiled)
        std::vector<label_info*> forward;
        for (auto* l : labels)
            if (l->forward)
                forward.push_back(l);
        for (bool changed = true; changed;)
        {
            changed = false;
            for (auto* a : forward)
                for (auto* b : forward)
                    if (a->open < b->open && b->open < a->position && a->position < b->position)
                    {
                        b->open = a->open;
                        changed = true;
                    }
        }
        for (auto* f : forward)
            for (auto* l : labels)
                if (l->backward && f->open < l->position && l->position < f->position)
                    iCompiler.error(l->statement->source, "goto to label '" + f->name + "' jumps over label '" + l->name + "' (which a goto after it jumps back to): not supported");
        auto const tbcCount = iTbcCount;
        std::vector<label_info*> loops;
        for (std::size_t i = 0u; i <= aStatements.size(); ++i)
        {
            // (forward blocks that end here (their labels'), innermost first)
            for (auto f = forward.rbegin(); f != forward.rend(); ++f)
                if ((*f)->position == i && (*f)->forwardControl)
                {
                    // (the innermost control must be its block)
                    if (*(*f)->forwardControl != iControls.size() - 1u)
                        iCompiler.error((*f)->statement->source, "goto to label '" + (*f)->name + "' cannot be compiled");
                    end_control();
                    (*f)->forwardControl = std::nullopt;
                }
            if (i == aStatements.size())
                break;
            // (a loop that begins here (its label's), then the forward blocks that begin here (outermost first))
            if (aStatements[i]->is(construct::Label) && aStatements[i]->label->backward)
            {
                auto* l = aStatements[i]->label;
                begin(control_kind::Loop);
                iControls.back().kind = control_kind::Label;
                l->backwardControl = iControls.size() - 1u;
                loops.push_back(l);
            }
            std::vector<label_info*> opening;
            for (auto* f : forward)
                if (f->open == i)
                    opening.push_back(f);
            std::sort(opening.begin(), opening.end(), [](label_info* a, label_info* b) { return a->position > b->position; });
            for (auto* f : opening)
            {
                begin(control_kind::Label);
                f->forwardControl = iControls.size() - 1u;
            }
            statement(aStatements[i]);
        }
        for (auto l = loops.rbegin(); l != loops.rend(); ++l)
        {
            end_control();
            (*l)->backwardControl = std::nullopt;
        }
        close_to(tbcCount);
        iTbcCount = tbcCount;
        (void)aOwner;
    }

    void function_generator::statement(node* aNode)
    {
        if (!aNode->is(construct::Label) && !aNode->is(construct::Empty))
            line_of(aNode);
        switch (aNode->kind)
        {
        case construct::Empty:
        case construct::Label:
            break;
        case construct::Break:
            break_statement(aNode);
            break;
        case construct::Goto:
            goto_statement(aNode);
            break;
        case construct::Do:
            block(aNode, compiler::statements_of(aNode, 0u));
            break;
        case construct::While:
            while_statement(aNode);
            break;
        case construct::Repeat:
            repeat_statement(aNode);
            break;
        case construct::If:
            if_statement(aNode);
            break;
        case construct::NumericFor:
            numeric_for(aNode);
            break;
        case construct::GenericFor:
            generic_for(aNode);
            break;
        case construct::FunctionStatement:
            function_statement(aNode);
            break;
        case construct::LocalFunction:
            local_function(aNode);
            break;
        case construct::Local:
            local_statement(aNode);
            break;
        case construct::Assign:
            assignment(aNode);
            break;
        case construct::CallStatement:
            expression(aNode->child(0u));
            drop();
            break;
        case construct::Return:
            return_statement(aNode);
            break;
        default:
            iCompiler.error(aNode->source, "unexpected statement");
        }
    }

    void function_generator::break_statement(node* aNode)
    {
        auto const target = break_control();
        if (target >= iControls.size())
            iCompiler.error(aNode->source, "break outside loop at line " + std::to_string(iCompiler.source_line_of(aNode->source)));
        close_to(iControls[target].tbcCount);
        branch(target);
    }

    void function_generator::goto_statement(node* aNode)
    {
        auto* l = aNode->label;
        auto const target = l->forwardControl ? l->forwardControl : l->backwardControl;
        if (!target)
            iCompiler.error(aNode->source, "goto to label '" + l->name + "' cannot be compiled");
        close_to(l->tbcCount);
        branch(*target);
    }

    void function_generator::local_statement(node* aNode)
    {
        std::vector<variable*> variables;
        std::vector<node*> expressions;
        for (auto* c : aNode->children)
        {
            if (c->is(construct::AttributeName))
                variables.push_back(c->child(0u)->declared);
            else
                expressions.push_back(c);
        }
        if (variables.size() == 1u && expressions.size() == 1u && variables[0]->integer)
        {
            // (an integer variable: its value's i64)
            raw_integer(expressions[0], {}, std::nullopt);
            variables[0]->local = local();
            set(variables[0]->local);
            return;
        }
        auto const values = values_of(expressions, variables.size());
        for (std::size_t i = 0u; i < variables.size(); ++i)
        {
            get(values[i]);
            release(values[i]);
            declare(variables[i]);
        }
    }

    void function_generator::local_function(node* aNode)
    {
        auto* v = aNode->child(0u)->declared;
        nil();
        declare(v);
        closure(*aNode->function);
        store(v);
    }

    void function_generator::function_statement(node* aNode)
    {
        auto* name = aNode->child(0u);
        if (name->size() == 1u)
        {
            closure(*aNode->function);
            auto const f = temporary();
            set(f);
            auto* target = name->child(0u);
            if (target->resolved != nullptr)
            {
                get(f);
                store(target->resolved);
            }
            else
            {
                // (a global)
                if (target->environment != nullptr)
                {
                    load(target->environment);
                    literal_string(target->text());
                    get(f);
                    description("upvalue '_ENV'");
                    runtime("setindex", { I64, I64, I64, I64 }, I64);
                }
                else
                {
                    get(CalleeLocal);
                    literal_string(target->text());
                    get(f);
                    runtime("setglobal", { I64, I64, I64 }, I64);
                }
                check_drop();
            }
            release(f);
            return;
        }
        // (a field of a table: function a.b.c() or function a.b:c())
        auto const object = temporary();
        identifier(name->child(0u));
        set(object);
        for (std::size_t i = 1u; i + 1u < name->size(); ++i)
        {
            get(object);
            literal_string(name->child(i)->text());
            description(describe(name->child(i - 1u)));
            runtime("index", { I64, I64, I64 }, I64);
            check();
            set(object);
        }
        closure(*aNode->function);
        auto const f = temporary();
        set(f);
        get(object);
        literal_string(name->children.back()->text());
        get(f);
        description(name->size() > 2u ? "field '" + name->child(name->size() - 2u)->text() + "'" : describe(name->child(0u)));
        runtime("setindex", { I64, I64, I64, I64 }, I64);
        check_drop();
        release(f);
        release(object);
    }

    void function_generator::assignment(node* aNode)
    {
        // (the targets' tables and keys, then the values, then the assignments)
        auto* targets = aNode->child(0u);
        std::vector<node*> expressions{ aNode->children.begin() + 1, aNode->children.end() };
        if (targets->size() == 1u && expressions.size() == 1u && targets->child(0u)->is(construct::Identifier) &&
            targets->child(0u)->resolved != nullptr && targets->child(0u)->resolved->integer)
        {
            // (an integer variable: its value's i64)
            raw_integer(expressions[0], {}, std::nullopt);
            set(targets->child(0u)->resolved->local);
            return;
        }
        struct target_info
        {
            node* target;
            std::uint32_t object = 0u;
            std::uint32_t key = 0u;
        };
        std::vector<target_info> infos;
        for (auto* t : targets->children)
        {
            target_info info{ t };
            if (t->is(construct::Dot) || t->is(construct::Index))
            {
                info.object = temporary();
                expression(t->child(0u));
                set(info.object);
                info.key = temporary();
                if (t->is(construct::Dot))
                    literal_string(t->child(1u)->text());
                else
                    expression(t->child(1u));
                set(info.key);
            }
            infos.push_back(info);
        }
        auto const values = values_of(expressions, infos.size());
        for (std::size_t i = 0u; i < infos.size(); ++i)
        {
            auto& info = infos[i];
            auto* t = info.target;
            if (t->is(construct::Identifier))
            {
                if (t->resolved != nullptr)
                {
                    get(values[i]);
                    store(t->resolved);
                }
                else if (t->environment != nullptr)
                {
                    load(t->environment);
                    literal_string(t->text());
                    get(values[i]);
                    description("upvalue '_ENV'");
                    runtime("setindex", { I64, I64, I64, I64 }, I64);
                    check_drop();
                }
                else if (t->text() == "_ENV")
                {
                    get(CalleeLocal);
                    get(values[i]);
                    runtime("setenv", { I64, I64 }, I64);
                    check_drop();
                }
                else
                {
                    get(CalleeLocal);
                    literal_string(t->text());
                    get(values[i]);
                    runtime("setglobal", { I64, I64, I64 }, I64);
                    check_drop();
                }
            }
            else
            {
                get(info.object);
                get(info.key);
                get(values[i]);
                description(describe(t->child(0u)));
                runtime("setindex", { I64, I64, I64, I64 }, I64);
                check_drop();
                release(info.object);
                release(info.key);
            }
            release(values[i]);
        }
    }

    void function_generator::if_statement(node* aNode)
    {
        // (if c then ... elseif c then ... else ... end: nested ifs)
        std::vector<node*> clauses{ aNode };
        node* elseClause = nullptr;
        for (auto* c : aNode->children)
            if (c->is(construct::ElseIf))
                clauses.push_back(c);
            else if (c->is(construct::Else))
                elseClause = c;
        for (std::size_t i = 0u; i < clauses.size(); ++i)
        {
            auto* clause = clauses[i];
            line_of(clause->child(0u));
            condition(clause->child(0u));
            begin_if();
            block(clause, compiler::statements_of(clause, 1u), 1u);
            if (i + 1u < clauses.size() || elseClause != nullptr)
                else_();
        }
        if (elseClause != nullptr)
            block(elseClause, compiler::statements_of(elseClause, 0u));
        for (std::size_t i = 0u; i < clauses.size(); ++i)
            end_control();
    }

    void function_generator::while_statement(node* aNode)
    {
        begin(control_kind::Break);
        begin(control_kind::Loop);
        line_of(aNode->child(0u));
        condition(aNode->child(0u));
        op(opcode::I32Eqz);
        branch(iControls.size() - 2u, true);
        block(aNode, compiler::statements_of(aNode, 1u), 1u, true);
        branch(iControls.size() - 1u);
        end_control();
        end_control();
    }

    void function_generator::repeat_statement(node* aNode)
    {
        // (the condition is in the scope of the body's locals: evaluated before they are closed)
        begin(control_kind::Break);
        begin(control_kind::Loop);
        auto const loop = iControls.size() - 1u;
        auto const done = temporary(I32);
        auto* condition = aNode->children.back();
        // (the body's to-be-closed variables are closed before the condition is evaluated; its other variables keep their values)
        block(aNode, compiler::statements_of(aNode, 0u), 0u, true);
        line_of(condition);
        this->condition(condition);
        set(done);
        get(done);
        op(opcode::I32Eqz);
        branch(loop, true);
        release(done);
        end_control();
        end_control();
    }

    void function_generator::numeric_for(node* aNode)
    {
        // (the loop's start, limit and step: an integer loop (its variable's i64 and the number of iterations that remain) or a float
        // loop (its f64s); the inline integers' loop is set up inline, others by the runtime (lua::forprep))
        auto const first = compiler::first_statement(aNode);
        auto* v = aNode->child(0u)->declared;
        auto const start = temporary();
        auto const limit = temporary();
        auto const step = temporary();
        expression(aNode->child(1u));
        set(start);
        expression(aNode->child(2u));
        set(limit);
        if (first > 3u)
            expression(aNode->child(3u));
        else
            constant(integer_value(1));
        set(step);
        auto const mode = local(I32);
        auto const ivar = local(I64);
        auto const icount = local(I64);
        auto const istep = local(I64);
        auto const fvar = local(F64);
        auto const flimit = local(F64);
        auto const fstep = local(F64);
        i32(-1);
        set(mode);
        get(start);
        is_integer();
        get(limit);
        is_integer();
        op(opcode::I32And);
        get(step);
        is_integer();
        op(opcode::I32And);
        begin_if();
        {
            get(step);
            unbox_integer();
            tee(istep);
            op(opcode::I64Eqz);
            op(opcode::I32Eqz);
            begin_if();
            {
                get(start);
                unbox_integer();
                set(ivar);
                get(istep);
                i64(0);
                op(opcode::I64GtS);
                begin_if();
                {
                    // (step > 0: start <= limit; the count is (limit - start) / step)
                    get(ivar);
                    get(limit);
                    unbox_integer();
                    op(opcode::I64LeS);
                    set(mode);
                    get(limit);
                    unbox_integer();
                    get(ivar);
                    op(opcode::I64Sub);
                    get(istep);
                    op(opcode::I64DivU);
                    set(icount);
                }
                else_();
                {
                    get(ivar);
                    get(limit);
                    unbox_integer();
                    op(opcode::I64GeS);
                    set(mode);
                    get(ivar);
                    get(limit);
                    unbox_integer();
                    op(opcode::I64Sub);
                    i64(0);
                    get(istep);
                    op(opcode::I64Sub);
                    op(opcode::I64DivU);
                    set(icount);
                }
                end_control();
            }
            end_control();
        }
        end_control();
        get(mode);
        i32(-1);
        op(opcode::I32Eq);
        begin_if();
        {
            get(start);
            get(limit);
            get(step);
            runtime("forprep", { I64, I64, I64 }, I64);
            check();
            op(opcode::I32ConvertI64);
            tee(mode);
            i32(1);
            op(opcode::I32Eq);
            begin_if();
            for (std::int32_t k = 0; k < 3; ++k)
            {
                i32(k);
                runtime("for_integer", { I32 }, I64);
                set(k == 0 ? ivar : k == 1 ? icount : istep);
            }
            else_();
            for (std::int32_t k = 0; k < 3; ++k)
            {
                i32(k);
                runtime("for_float", { I32 }, F64);
                set(k == 0 ? fvar : k == 1 ? flimit : fstep);
            }
            end_control();
        }
        end_control();
        release(start);
        release(limit);
        release(step);
        begin(control_kind::Break);
        auto const exit = iControls.size() - 1u;
        get(mode);
        op(opcode::I32Eqz);
        branch(exit, true);
        begin(control_kind::Loop);
        auto const loop = iControls.size() - 1u;
        // (the variable: a new one each iteration (an integer variable's loop is an integer loop: its start and step are integers))
        if (v->integer)
        {
            v->local = local();
            get(ivar);
            set(v->local);
        }
        else
        {
            get(mode);
            i32(1);
            op(opcode::I32Eq);
            begin_if(I64);
            get(ivar);
            box_integer();
            else_();
            get(fvar);
            op(opcode::I64ReinterpretF64);
            end_control();
            declare(v);
        }
        block(aNode, compiler::statements_of(aNode, first), first, true);
        // (the next iteration)
        if (v->integer)
            i32(1);
        else
        {
            get(mode);
            i32(1);
            op(opcode::I32Eq);
        }
        begin_if();
        get(icount);
        op(opcode::I64Eqz);
        branch(exit, true);
        get(icount);
        i64(1);
        op(opcode::I64Sub);
        set(icount);
        get(ivar);
        get(istep);
        op(opcode::I64Add);
        set(ivar);
        else_();
        get(fvar);
        get(fstep);
        op(opcode::F64Add);
        set(fvar);
        get(fstep);
        f64(0.0);
        op(opcode::F64Gt);
        begin_if(I32);
        get(fvar);
        get(flimit);
        op(opcode::F64Le);
        else_();
        get(fvar);
        get(flimit);
        op(opcode::F64Ge);
        end_control();
        op(opcode::I32Eqz);
        branch(exit, true);
        end_control();
        branch(loop);
        end_control();
        end_control();
    }

    void function_generator::generic_for(node* aNode)
    {
        // (for vars in explist: the iterator function, its state, the control variable and the closing value (to-be-closed))
        auto const first = compiler::first_statement(aNode);
        std::vector<variable*> variables;
        std::vector<node*> expressions;
        for (std::size_t i = 0u; i < first; ++i)
        {
            if (aNode->child(i)->is(construct::Binding))
                variables.push_back(aNode->child(i)->declared);
            else
                expressions.push_back(aNode->child(i));
        }
        auto const values = values_of(expressions, 4u);
        auto const tbcCount = iTbcCount;
        begin(control_kind::Break);
        auto const exit = iControls.size() - 1u;
        // (the closing value: closed when the loop ends (unless it is certainly nil))
        if (aNode->closing)
        {
            get(values[3]);
            literal_string("(for state)");
            runtime("tbc_push", { I64, I64 }, I64);
            check_drop();
            ++iTbcCount;
        }
        begin(control_kind::Loop);
        auto const loop = iControls.size() - 1u;
        call_value(values[0], { values[1], values[2] }, std::nullopt, "for iterator 'for iterator'");
        std::vector<std::uint32_t> results;
        auto const v1 = temporary();
        set(v1);
        results.push_back(v1);
        for (std::size_t i = 1u; i < variables.size(); ++i)
        {
            auto const r = temporary();
            i32(static_cast<std::int32_t>(i));
            runtime("result", { I32 }, I64);
            set(r);
            results.push_back(r);
        }
        get(v1);
        nil();
        op(opcode::I64Eq);
        branch(exit, true);
        get(v1);
        set(values[2]);
        for (std::size_t i = 0u; i < variables.size(); ++i)
        {
            get(results[i]);
            release(results[i]);
            declare(variables[i]);
        }
        block(aNode, compiler::statements_of(aNode, first), first, true);
        branch(loop);
        end_control();
        end_control();
        close_to(tbcCount);
        iTbcCount = tbcCount;
        for (auto v : values)
            release(v);
    }

    void function_generator::return_statement(node* aNode)
    {
        std::vector<node*> expressions{ aNode->children.begin(), aNode->children.end() };
        if (iTbcCount > 0u && !expressions.empty())
        {
            // (its values, then its to-be-closed variables are closed)
            auto const base = push_values(expressions);
            close_to(0u);
            get(base);
            runtime("stack_return", { I32 }, I64);
            check();
            op(opcode::Return);
            release(base);
            return;
        }
        close_to(0u);
        if (expressions.empty())
        {
            set_results(0u);
            nil();
            op(opcode::Return);
            return;
        }
        if (expressions.size() == 1u)
        {
            if (is_multiple(expressions[0]))
            {
                // (its values are the call's (or the varargs'))
                multiple(expressions[0]);
                op(opcode::Return);
                return;
            }
            expression(expressions[0]);
            set_results(1u);
            op(opcode::Return);
            return;
        }
        if (is_multiple(expressions.back()))
        {
            auto const base = push_values(expressions);
            get(base);
            runtime("stack_return", { I32 }, I64);
            check();
            op(opcode::Return);
            release(base);
            return;
        }
        std::vector<std::uint32_t> values;
        for (auto* e : expressions)
        {
            expression(e);
            auto const v = temporary();
            set(v);
            values.push_back(v);
        }
        if (values.size() - 1u <= 4u)
        {
            i32(static_cast<std::int32_t>(values.size() - 1u));
            for (std::size_t i = 1u; i <= 4u; ++i)
            {
                if (i < values.size())
                    get(values[i]);
                else
                    nil();
            }
            runtime("ret_extra", { I32, I64, I64, I64, I64 }, I64);
            check_drop();
        }
        else
        {
            runtime("ret_begin", {}, I64);
            drop();
            for (std::size_t i = 1u; i < values.size(); ++i)
            {
                get(values[i]);
                runtime("ret_push", { I64 }, I64);
                check_drop();
            }
        }
        set_results(static_cast<std::uint32_t>(values.size()));
        get(values[0]);
        op(opcode::Return);
        for (auto v : values)
            release(v);
    }

    std::vector<std::uint32_t> function_generator::values_of(std::vector<node*> const& aExpressions, std::size_t aCount)
    {
        std::vector<std::uint32_t> result;
        for (std::size_t i = 0u; i < aExpressions.size(); ++i)
        {
            auto* e = aExpressions[i];
            bool const last = i + 1u == aExpressions.size();
            if (last && is_multiple(e) && aCount > i + 1u)
            {
                // (the last expression's values fill the rest)
                multiple(e);
                auto const v = temporary();
                set(v);
                result.push_back(v);
                for (std::size_t j = i + 1u; j < aCount; ++j)
                {
                    auto const r = temporary();
                    i32(static_cast<std::int32_t>(j - i));
                    runtime("result", { I32 }, I64);
                    set(r);
                    result.push_back(r);
                }
                continue;
            }
            expression(e);
            if (i < aCount)
            {
                auto const v = temporary();
                set(v);
                result.push_back(v);
            }
            else
                drop();
        }
        while (result.size() < aCount)
        {
            auto const v = temporary();
            nil();
            set(v);
            result.push_back(v);
        }
        return result;
    }

    std::uint32_t function_generator::push_values(std::vector<node*> const& aExpressions)
    {
        auto const base = temporary(I32);
        runtime("stack_begin", {}, I32);
        set(base);
        for (std::size_t i = 0u; i < aExpressions.size(); ++i)
        {
            auto* e = aExpressions[i];
            if (i + 1u == aExpressions.size() && is_multiple(e))
            {
                multiple(e);
                runtime("stack_push_results", { I64 }, I64);
            }
            else
            {
                expression(e);
                runtime("stack_push", { I64 }, I64);
            }
            check_drop();
        }
        return base;
    }

    // --- expressions

    std::string function_generator::describe(node* aNode)
    {
        while (aNode->is(construct::Parenthesized))
            aNode = aNode->child(0u);
        switch (aNode->kind)
        {
        case construct::Identifier:
            if (aNode->resolved != nullptr)
                return (aNode->resolved->function == &iFunction ? "local '" : "upvalue '") + aNode->text() + "'";
            if (aNode->environment != nullptr)
                return "field '" + aNode->text() + "'";
            return "global '" + aNode->text() + "'";
        case construct::Dot:
            return "field '" + aNode->child(1u)->text() + "'";
        case construct::Method:
            return "method '" + aNode->child(1u)->text() + "'";
        case construct::String:
            {
                auto const text = string_value(iCompiler, aNode->source);
                return "constant '" + text + "'";
            }
        default:
            return {};
        }
    }

    void function_generator::expression(node* aNode)
    {
        switch (aNode->kind)
        {
        case construct::Identifier:
            identifier(aNode);
            break;
        case construct::Nil:
        case construct::True:
        case construct::False:
            literal(aNode);
            break;
        case construct::Number:
            number(aNode);
            break;
        case construct::String:
            string(aNode);
            break;
        case construct::Vararg:
            vararg_value();
            break;
        case construct::Function:
            function_expression(aNode);
            break;
        case construct::Table:
            table_constructor(aNode);
            break;
        case construct::Parenthesized:
            expression(aNode->child(0u));
            break;
        case construct::Dot:
        case construct::Index:
            index_expression(aNode);
            break;
        case construct::Call:
            call_expression(aNode);
            break;
        case construct::Method:
            method_call(aNode);
            break;
        case construct::And:
        case construct::Or:
            logical(aNode);
            break;
        case construct::Concat:
            concatenation(aNode);
            break;
        case construct::Not:
        case construct::Negate:
        case construct::Length:
        case construct::BitNot:
            unary(aNode);
            break;
        default:
            if (is_arithmetic(aNode->kind) && integer_expression(aNode))
            {
                raw_integer(aNode, {}, std::nullopt);
                box_integer();
            }
            else if (is_arithmetic(aNode->kind) && float_expression(aNode))
            {
                raw_float(aNode);
                op(opcode::I64ReinterpretF64);
            }
            else if (iFusing && fusable(aNode))
                fused_arithmetic(aNode);
            else if (is_arithmetic(aNode->kind) || is_comparison(aNode->kind))
                binary(aNode);
            else
                iCompiler.error(aNode->source, "unexpected expression");
            break;
        }
    }

    void function_generator::multiple(node* aNode)
    {
        if (aNode->is(construct::Vararg))
        {
            get(*iVarargs);
            runtime("vararg_results", { I64 }, I64);
            check();
            return;
        }
        expression(aNode);
    }

    void function_generator::condition(node* aNode)
    {
        switch (aNode->kind)
        {
        case construct::True:
            i32(1);
            return;
        case construct::False:
        case construct::Nil:
            i32(0);
            return;
        case construct::Not:
            condition(aNode->child(0u));
            op(opcode::I32Eqz);
            return;
        case construct::And:
            condition(aNode->child(0u));
            begin_if(I32);
            condition(aNode->child(1u));
            else_();
            i32(0);
            end_control();
            return;
        case construct::Or:
            condition(aNode->child(0u));
            begin_if(I32);
            i32(1);
            else_();
            condition(aNode->child(1u));
            end_control();
            return;
        case construct::Parenthesized:
            condition(aNode->child(0u));
            return;
        default:
            if (is_comparison(aNode->kind))
            {
                if (integer_comparison(aNode))
                    return;
                auto const a = temporary();
                auto const b = temporary();
                expression(aNode->child(0u));
                set(a);
                expression(aNode->child(1u));
                set(b);
                comparison(aNode->kind, a, b, true);
                release(a);
                release(b);
                return;
            }
            expression(aNode);
            truth();
            return;
        }
    }

    void function_generator::identifier(node* aNode)
    {
        if (aNode->resolved != nullptr && aNode->resolved->constant == &iFunction && aNode->resolved->function != &iFunction)
        {
            // (a function's own name (a local function's that is not assigned again): the closure that is running)
            get(CalleeLocal);
            return;
        }
        if (aNode->resolved != nullptr)
        {
            load(aNode->resolved);
            return;
        }
        if (aNode->environment != nullptr)
        {
            load(aNode->environment);
            literal_string(aNode->text());
            description("upvalue '_ENV'");
            runtime("index", { I64, I64, I64 }, I64);
            check();
            return;
        }
        get(CalleeLocal);
        if (aNode->text() == "_ENV")
        {
            runtime("getenv", { I64 }, I64);
            return;
        }
        literal_string(aNode->text());
        runtime("getglobal", { I64, I64 }, I64);
        check();
    }

    void function_generator::literal(node* aNode)
    {
        constant(aNode->is(construct::True) ? values::True : aNode->is(construct::False) ? values::False : values::Nil);
    }

    void function_generator::number(node* aNode)
    {
        auto const value = number_value(aNode->source);
        if (std::holds_alternative<double>(value))
        {
            auto const d = std::get<double>(value);
            constant(std::isnan(d) ? 0x7FF8000000000000ull : float_value(d));
            return;
        }
        auto const i = std::get<std::int64_t>(value);
        if (inline_integer(i))
            constant(integer_value(i));
        else
        {
            i64(i);
            runtime("int_box", { I64 }, I64);
            check();
        }
    }

    void function_generator::string(node* aNode)
    {
        literal_string(string_value(iCompiler, aNode->source));
    }

    void function_generator::vararg_value()
    {
        get(*iVarargs);
        i32(0);
        runtime("vararg_get", { I64, I32 }, I64);
    }

    void function_generator::function_expression(node* aNode, std::optional<std::string>)
    {
        closure(*aNode->function);
    }

    void function_generator::closure(function_info& aFunction)
    {
        // (a new closure of the function: its upvalues are the boxes of the variables it uses)
        i32(static_cast<std::int32_t>(aFunction.number));
        op(opcode::I32Const);
        iRelocations.emplace_back(static_cast<std::uint32_t>(iCode.size()), aFunction.index);
        iCode.resize(iCode.size() + 5u);
        i32(static_cast<std::int32_t>(aFunction.upvalues.size()));
        get(CalleeLocal);
        runtime("closure", { I32, I32, I32, I64 }, I64);
        check();
        if (aFunction.upvalues.empty())
            return;
        auto const c = temporary();
        set(c);
        for (std::size_t i = 0u; i < aFunction.upvalues.size(); ++i)
        {
            auto const& u = aFunction.upvalues[i];
            get(c);
            i32(static_cast<std::int32_t>(i));
            if (u.parentLocal)
                get(u.target->local);
            else
            {
                get(CalleeLocal);
                i32(static_cast<std::int32_t>(u.parentIndex));
                runtime("upval_box", { I64, I32 }, I64);
            }
            runtime("closure_upval", { I64, I32, I64 }, I64);
            drop();
        }
        get(c);
        release(c);
    }

    void function_generator::table_constructor(node* aNode)
    {
        std::size_t positional = 0u;
        std::size_t named = 0u;
        for (auto* c : aNode->children)
        {
            if (c->is(construct::FieldNamed) || c->is(construct::FieldKeyed))
                ++named;
            else
                ++positional;
        }
        i32(static_cast<std::int32_t>(positional));
        i32(static_cast<std::int32_t>(named));
        runtime("table_new", { I32, I32 }, I64);
        check();
        if (aNode->children.empty())
            return;
        auto const t = temporary();
        set(t);
        // (positional fields are assigned in batches (of 50), after the fields before them (as Lua's constructors do))
        std::int32_t index = 1;
        std::vector<std::pair<std::int32_t, std::uint32_t>> pending;
        auto const flush = [&]()
        {
            for (auto const& [i, v] : pending)
            {
                get(t);
                i32(i);
                get(v);
                runtime("table_seti", { I64, I32, I64 }, I64);
                check_drop();
                release(v);
            }
            pending.clear();
        };
        for (std::size_t i = 0u; i < aNode->size(); ++i)
        {
            auto* c = aNode->child(i);
            if (c->is(construct::FieldNamed))
            {
                auto const v = temporary();
                expression(c->child(1u));
                set(v);
                get(t);
                literal_string(c->child(0u)->text());
                get(v);
                runtime("table_set", { I64, I64, I64 }, I64);
                check_drop();
                release(v);
            }
            else if (c->is(construct::FieldKeyed))
            {
                auto const k = temporary();
                auto const v = temporary();
                expression(c->child(0u));
                set(k);
                expression(c->child(1u));
                set(v);
                get(t);
                get(k);
                get(v);
                runtime("table_set", { I64, I64, I64 }, I64);
                check_drop();
                release(k);
                release(v);
            }
            else if (i + 1u == aNode->size() && is_multiple(c))
            {
                multiple(c);
                auto const v = temporary();
                set(v);
                flush();
                get(t);
                i32(index);
                get(v);
                runtime("table_append_results", { I64, I32, I64 }, I64);
                check_drop();
                release(v);
            }
            else
            {
                auto const v = temporary();
                expression(c);
                set(v);
                pending.emplace_back(index++, v);
                if (pending.size() == 50u)
                    flush();
            }
        }
        flush();
        get(t);
        release(t);
    }

    void function_generator::index_expression(node* aNode)
    {
        auto const object = temporary();
        expression(aNode->child(0u));
        set(object);
        auto const key = temporary();
        if (aNode->is(construct::Dot))
            literal_string(aNode->child(1u)->text());
        else
            expression(aNode->child(1u));
        set(key);
        get(object);
        get(key);
        description(describe(aNode->child(0u)));
        runtime("index", { I64, I64, I64 }, I64);
        check();
        release(object);
        release(key);
    }

    std::vector<std::uint32_t> function_generator::arguments_of(std::vector<node*> const& aArguments, std::optional<std::uint32_t>& aMultiple)
    {
        std::vector<std::uint32_t> result;
        for (std::size_t i = 0u; i < aArguments.size(); ++i)
        {
            auto* a = aArguments[i];
            auto const v = temporary();
            if (i + 1u == aArguments.size() && is_multiple(a))
            {
                multiple(a);
                set(v);
                aMultiple = v;
                break;
            }
            expression(a);
            set(v);
            result.push_back(v);
        }
        return result;
    }

    void function_generator::call_expression(node* aNode)
    {
        auto const callee = temporary();
        expression(aNode->child(0u));
        set(callee);
        std::optional<std::uint32_t> multiple;
        auto const arguments = arguments_of(std::vector<node*>{ aNode->children.begin() + 1, aNode->children.end() }, multiple);
        auto* target = aNode->child(0u);
        while (target->is(construct::Parenthesized))
            target = target->child(0u);
        function_info const* known = target->is(construct::Identifier) && target->resolved != nullptr ? target->resolved->constant : nullptr;
        call_value(callee, arguments, multiple, describe(aNode->child(0u)), known);
        release(callee);
        for (auto a : arguments)
            release(a);
        if (multiple)
            release(*multiple);
    }

    void function_generator::method_call(node* aNode)
    {
        // (o:m(...): o.m(o, ...), o evaluated once)
        auto const object = temporary();
        expression(aNode->child(0u));
        set(object);
        auto const callee = temporary();
        get(object);
        literal_string(aNode->child(1u)->text());
        description(describe(aNode->child(0u)));
        runtime("index", { I64, I64, I64 }, I64);
        check();
        set(callee);
        std::optional<std::uint32_t> multiple;
        auto arguments = arguments_of(std::vector<node*>{ aNode->children.begin() + 2, aNode->children.end() }, multiple);
        arguments.insert(arguments.begin(), object);
        call_value(callee, arguments, multiple, describe(aNode));
        release(callee);
        for (auto a : arguments)
            release(a);
        if (multiple)
            release(*multiple);
    }

    void function_generator::call_value(std::uint32_t aCallee, std::vector<std::uint32_t> const& aArguments, std::optional<std::uint32_t> aMultiple,
        std::string const& aDescription, function_info const* aKnown)
    {
        if (aMultiple)
        {
            // (the last argument has one value (usually): a call with it as an argument; else the arguments, and the last's values, on the
            // runtime's argument stack: the runtime calls the callee)
            op(opcode::GlobalGet);
            u32(iCompiler.iResultsGlobal);
            i64(1);
            op(opcode::I64Eq);
            begin_if(I64);
            auto arguments = aArguments;
            arguments.push_back(*aMultiple);
            call_value(aCallee, arguments, std::nullopt, aDescription, aKnown);
            else_();
            auto const base = temporary(I32);
            runtime("stack_begin", {}, I32);
            set(base);
            for (auto a : aArguments)
            {
                get(a);
                runtime("stack_push", { I64 }, I64);
                drop();
            }
            get(*aMultiple);
            runtime("stack_push_results", { I64 }, I64);
            check_drop();
            get(aCallee);
            get(base);
            description(aDescription);
            runtime("stack_call", { I64, I32, I64 }, I64);
            check();
            release(base);
            end_control();
            return;
        }
        auto const argc = static_cast<std::uint32_t>(aArguments.size());
        for (std::size_t i = RegisterArguments; i < aArguments.size(); ++i)
        {
            get(aArguments[i]);
            runtime("push_arg", { I64 }, I64);
            drop();
        }
        if (aKnown != nullptr)
        {
            // (a known function: called directly)
            get(aCallee);
            i32(static_cast<std::int32_t>(argc));
            for (std::uint32_t i = 0u; i < RegisterArguments; ++i)
            {
                if (i < aArguments.size())
                    get(aArguments[i]);
                else
                    nil();
            }
            call(aKnown->index);
        }
        else
        {
        // (the runtime calls the callee, unless it is the program's own Lua function: then its result is a dispatch (its number) and the
        // dispatcher calls it)
        auto const number = temporary();
        get(aCallee);
        op(opcode::GlobalGet);
        u32(iCompiler.iProgramGlobal);
        i32(static_cast<std::int32_t>(argc));
        for (std::uint32_t i = 0u; i < RegisterArguments; ++i)
        {
            if (i < aArguments.size())
                get(aArguments[i]);
            else
                nil();
        }
        description(aDescription);
        runtime("invoke", { I64, I64, I32, I64, I64, I64, I64, I64 }, I64);
        tee(number);
        i64(32);
        op(opcode::I64ShrU);
        i64(static_cast<std::int64_t>(values::Dispatch >> 32u));
        op(opcode::I64Eq);
        begin_if(I64);
        get(number);
        op(opcode::I32ConvertI64);
        get(aCallee);
        i32(static_cast<std::int32_t>(argc));
        for (std::uint32_t i = 0u; i < RegisterArguments; ++i)
        {
            if (i < aArguments.size())
                get(aArguments[i]);
            else
                nil();
        }
        call(iCompiler.iDispatcher);
        else_();
        get(number);
        end_control();
        release(number);
        }
        if (argc > RegisterArguments)
        {
            auto const result = temporary();
            set(result);
            i32(static_cast<std::int32_t>(argc - RegisterArguments));
            runtime("pop_args", { I32 }, I64);
            drop();
            get(result);
            release(result);
        }
        check();
    }

    bool function_generator::integer_comparison(node* aNode)
    {
        if (float_expression(aNode->child(0u)) && float_expression(aNode->child(1u)))
        {
            // (floats: compared as f64s)
            raw_float(aNode->child(0u));
            raw_float(aNode->child(1u));
            switch (aNode->kind)
            {
            case construct::Less: op(opcode::F64Lt); break;
            case construct::Greater: op(opcode::F64Gt); break;
            case construct::LessEqual: op(opcode::F64Le); break;
            case construct::GreaterEqual: op(opcode::F64Ge); break;
            case construct::Equal: op(opcode::F64Eq); break;
            default: op(opcode::F64Ne); break;
            }
            return true;
        }
        if (!integer_expression(aNode->child(0u)) || !integer_expression(aNode->child(1u)))
            return false;
        raw_integer(aNode->child(0u), {}, std::nullopt);
        raw_integer(aNode->child(1u), {}, std::nullopt);
        switch (aNode->kind)
        {
        case construct::Less: op(opcode::I64LtS); break;
        case construct::Greater: op(opcode::I64GtS); break;
        case construct::LessEqual: op(opcode::I64LeS); break;
        case construct::GreaterEqual: op(opcode::I64GeS); break;
        case construct::Equal: op(opcode::I64Eq); break;
        default: op(opcode::I64Ne); break;
        }
        return true;
    }

    void function_generator::binary(node* aNode)
    {
        if (is_comparison(aNode->kind) && integer_comparison(aNode))
        {
            // (a boolean)
            auto const c = temporary(I32);
            set(c);
            constant(values::True);
            constant(values::False);
            get(c);
            op(opcode::Select);
            release(c);
            return;
        }
        auto const a = temporary();
        auto const b = temporary();
        expression(aNode->child(0u));
        set(a);
        expression(aNode->child(1u));
        set(b);
        if (is_comparison(aNode->kind))
        {
            comparison(aNode->kind, a, b, false);
        }
        else
            arithmetic(aNode->kind, a, b, aNode->child(0u), aNode->child(1u));
        release(a);
        release(b);
    }

    bool function_generator::fusable(node* aNode, bool aOperand) const
    {
        // (an operand: a local variable (its value has no side effects to read), an integer numeral or such arithmetic)
        while (aNode->is(construct::Parenthesized))
            aNode = aNode->child(0u);
        if (aOperand)
        {
            if (aNode->is(construct::Identifier))
                return aNode->resolved != nullptr;
            if (aNode->is(construct::Number))
                return std::holds_alternative<std::int64_t>(number_value(aNode->source));
            if (aNode->is(construct::Negate))
                return fusable(aNode->child(0u), true);
        }
        switch (aNode->kind)
        {
        case construct::Add:
        case construct::Subtract:
        case construct::Multiply:
            return fusable(aNode->child(0u), true) && fusable(aNode->child(1u), true);
        case construct::Modulo:
        case construct::FloorDivide:
            {
                // (a numeral divisor must be positive)
                auto* divisor = aNode->child(1u);
                while (divisor->is(construct::Parenthesized))
                    divisor = divisor->child(0u);
                if (divisor->is(construct::Number))
                {
                    auto const value = number_value(divisor->source);
                    if (!std::holds_alternative<std::int64_t>(value) || std::get<std::int64_t>(value) <= 0)
                        return false;
                }
                return fusable(aNode->child(0u), true) && fusable(aNode->child(1u), true);
            }
        default:
            return false;
        }
    }

    void function_generator::fused_arithmetic(node* aNode)
    {
        // (block (result i64) block: the variables' values (each an integer, else the general case); the integer result, boxed; br out
        // end; the general case end)
        std::map<variable*, std::uint32_t> leaves;
        std::vector<node*> pending{ aNode };
        std::vector<variable*> order;
        while (!pending.empty())
        {
            auto* n = pending.back();
            pending.pop_back();
            if (n->is(construct::Identifier))
            {
                if (!n->resolved->integer && leaves.emplace(n->resolved, 0u).second)
                    order.push_back(n->resolved);
                continue;
            }
            for (auto i = n->children.rbegin(); i != n->children.rend(); ++i)
                pending.push_back(*i);
        }
        begin(control_kind::Block, I64);
        auto const done = iControls.size() - 1u;
        begin(control_kind::Block, {});
        auto const slow = iControls.size() - 1u;
        for (auto* v : order)
        {
            auto const t = temporary();
            load(v);
            tee(t);
            is_integer();
            op(opcode::I32Eqz);
            branch(slow, true);
            leaves[v] = t;
        }
        raw_integer(aNode, leaves, slow);
        box_integer();
        branch(done, false);
        end_control();
        for (auto const& [v, t] : leaves)
            release(t);
        bool const fusing = iFusing;
        iFusing = false;
        binary(aNode);
        iFusing = fusing;
        end_control();
    }

    void function_generator::raw_integer(node* aNode, std::map<variable*, std::uint32_t> const& aLeaves, std::optional<std::size_t> aSlow)
    {
        // (an i64 (wrapping, as Lua's integers))
        while (aNode->is(construct::Parenthesized))
            aNode = aNode->child(0u);
        switch (aNode->kind)
        {
        case construct::Identifier:
            if (aNode->resolved->integer)
                get(aNode->resolved->local);
            else
            {
                get(aLeaves.at(aNode->resolved));
                unbox_integer();
            }
            return;
        case construct::Number:
            i64(std::get<std::int64_t>(number_value(aNode->source)));
            return;
        case construct::Negate:
            i64(0);
            raw_integer(aNode->child(0u), aLeaves, aSlow);
            op(opcode::I64Sub);
            return;
        case construct::Add:
        case construct::Subtract:
        case construct::Multiply:
            raw_integer(aNode->child(0u), aLeaves, aSlow);
            raw_integer(aNode->child(1u), aLeaves, aSlow);
            op(aNode->is(construct::Add) ? opcode::I64Add : aNode->is(construct::Subtract) ? opcode::I64Sub : opcode::I64Mul);
            return;
        default:
            break;
        }
        // (% and //: floored (a nonzero remainder whose sign is not the divisor's is adjusted); a fused expression's divisor that is not a 
        // positive numeral must be positive (else the general case); an integer expression's divisor: 0 is an error (the runtime's), -1 
        // negates (// of the least integer by -1 would trap))
        bool const modulo = aNode->is(construct::Modulo);
        auto* divisor = aNode->child(1u);
        while (divisor->is(construct::Parenthesized))
            divisor = divisor->child(0u);
        std::optional<std::int64_t> literal;
        if (divisor->is(construct::Number))
            literal = std::get<std::int64_t>(number_value(divisor->source));
        auto const left = temporary();
        auto const right = temporary();
        auto const remainder = temporary();
        raw_integer(aNode->child(0u), aLeaves, aSlow);
        set(left);
        // (a numeral divisor is a constant (division by a constant is a multiplication))
        bool const constantDivisor = literal && *literal > 0;
        auto const push_divisor = [&]()
        {
            if (constantDivisor)
                i64(*literal);
            else
                get(right);
        };
        if (!constantDivisor)
        {
            if (literal)
                i64(*literal);
            else
                raw_integer(divisor, aLeaves, aSlow);
            set(right);
        }
        if (!literal || *literal <= 0)
        {
            if (aSlow)
            {
                get(right);
                i64(0);
                op(opcode::I64LeS);
                branch(*aSlow, true);
            }
            else
            {
                // (division by zero: the runtime's error)
                get(right);
                op(opcode::I64Eqz);
                begin_if();
                i32(arithmetic_operation(aNode->kind));
                get(left);
                box_integer();
                constant(integer_value(0));
                description(describe(aNode->child(0u)));
                description(describe(aNode->child(1u)));
                runtime("arith", { I32, I64, I64, I64, I64 }, I64);
                check_drop();
                end_control();
            }
        }
        bool const negative = !aSlow && (!literal || *literal < 0);
        std::optional<std::uint32_t> minusOne;
        if (negative && !modulo)
        {
            // (-1: 1 (the quotient is negated below))
            minusOne = temporary(I32);
            get(right);
            i64(-1);
            op(opcode::I64Eq);
            set(*minusOne);
            i64(1);
            get(right);
            get(*minusOne);
            op(opcode::Select);
            set(right);
        }
        get(left);
        push_divisor();
        op(opcode::I64RemS);
        set(remainder);
        // (the adjustment: the remainder is nonzero and its sign is not the divisor's ((remainder ^ divisor) < 0); a positive divisor: the 
        // remainder is negative)
        auto const adjust = [&]()
        {
            if (negative)
            {
                get(remainder);
                push_divisor();
                op(opcode::I64Xor);
                i64(0);
                op(opcode::I64LtS);
                get(remainder);
                op(opcode::I64Eqz);
                op(opcode::I32Eqz);
                op(opcode::I32And);
            }
            else
            {
                get(remainder);
                i64(0);
                op(opcode::I64LtS);
            }
        };
        if (modulo)
        {
            // (remainder + (adjust ? divisor : 0))
            get(remainder);
            push_divisor();
            i64(0);
            adjust();
            op(opcode::Select);
            op(opcode::I64Add);
        }
        else
        {
            // (quotient - (adjust ? 1 : 0))
            get(left);
            push_divisor();
            op(opcode::I64DivS);
            adjust();
            op(opcode::I64UConvertI32);
            op(opcode::I64Sub);
            if (minusOne)
            {
                // (a divisor of -1: the dividend negated (wrapping))
                auto const q = temporary();
                set(q);
                i64(0);
                get(left);
                op(opcode::I64Sub);
                get(q);
                get(*minusOne);
                op(opcode::Select);
                release(q);
                release(*minusOne);
            }
        }
        release(remainder);
        release(right);
        release(left);
    }

    void function_generator::raw_float(node* aNode)
    {
        while (aNode->is(construct::Parenthesized))
            aNode = aNode->child(0u);
        if (integer_expression(aNode))
        {
            raw_integer(aNode, {}, std::nullopt);
            op(opcode::F64SConvertI64);
            return;
        }
        switch (aNode->kind)
        {
        case construct::Number:
            f64(std::get<double>(number_value(aNode->source)));
            return;
        case construct::Identifier:
            get(aNode->resolved->local);
            op(opcode::F64ReinterpretI64);
            return;
        case construct::Negate:
            raw_float(aNode->child(0u));
            op(opcode::F64Neg);
            return;
        default:
            break;
        }
        raw_float(aNode->child(0u));
        raw_float(aNode->child(1u));
        switch (aNode->kind)
        {
        case construct::Add: op(opcode::F64Add); break;
        case construct::Subtract: op(opcode::F64Sub); break;
        case construct::Multiply: op(opcode::F64Mul); break;
        case construct::Divide: op(opcode::F64Div); break;
        default:
            op(opcode::F64Div);
            op(opcode::F64Floor);
            break;
        }
    }

    void function_generator::to_float(std::uint32_t aValue)
    {
        // (select (f64 of the integer) (f64 of its bits) (is integer))
        get(aValue);
        unbox_integer();
        op(opcode::F64SConvertI64);
        get(aValue);
        op(opcode::F64ReinterpretI64);
        get(aValue);
        is_integer();
        op(opcode::Select);
    }

    void function_generator::arithmetic(construct aOperator, std::uint32_t aLeft, std::uint32_t aRight, node* aLeftNode, node* aRightNode)
    {
        auto const slow = [&]()
        {
            i32(arithmetic_operation(aOperator));
            get(aLeft);
            get(aRight);
            description(describe(aLeftNode));
            description(describe(aRightNode));
            runtime("arith", { I32, I64, I64, I64, I64 }, I64);
            check();
        };
        bool const integerInline = aOperator == construct::Add || aOperator == construct::Subtract || aOperator == construct::Multiply;
        bool const floatInline = integerInline || aOperator == construct::Divide;
        if (aOperator == construct::Modulo || aOperator == construct::FloorDivide)
        {
            // (integers (that fit inline) and a positive divisor: % and // inline (floored: a negative remainder is adjusted); their
            // results fit inline)
            get(aLeft);
            is_integer();
            get(aRight);
            is_integer();
            op(opcode::I32And);
            begin_if(I64);
            get(aRight);
            unbox_integer();
            i64(0);
            op(opcode::I64GtS);
            begin_if(I64);
            auto const left = temporary();
            auto const right = temporary();
            auto const remainder = temporary();
            get(aLeft);
            unbox_integer();
            set(left);
            get(aRight);
            unbox_integer();
            set(right);
            get(left);
            get(right);
            op(opcode::I64RemS);
            set(remainder);
            if (aOperator == construct::Modulo)
            {
                // (remainder + (remainder < 0 ? right : 0))
                get(remainder);
                get(right);
                i64(0);
                get(remainder);
                i64(0);
                op(opcode::I64LtS);
                op(opcode::Select);
                op(opcode::I64Add);
            }
            else
            {
                // (quotient - (remainder < 0 ? 1 : 0))
                get(left);
                get(right);
                op(opcode::I64DivS);
                get(remainder);
                i64(0);
                op(opcode::I64LtS);
                op(opcode::I64UConvertI32);
                op(opcode::I64Sub);
            }
            constant(values::PayloadMask);
            op(opcode::I64And);
            constant(values::TagInteger);
            op(opcode::I64Ior);
            release(remainder);
            release(right);
            release(left);
            else_();
            slow();
            end_control();
            else_();
            slow();
            end_control();
            return;
        }
        if (!floatInline)
        {
            slow();
            return;
        }
        // (integers (that fit inline): + - * inline (wrapping as 64 bit integers); / of integers is a float's)
        get(aLeft);
        is_integer();
        get(aRight);
        is_integer();
        op(opcode::I32And);
        begin_if(I64);
        if (integerInline)
        {
            get(aLeft);
            unbox_integer();
            get(aRight);
            unbox_integer();
            op(aOperator == construct::Add ? opcode::I64Add : aOperator == construct::Subtract ? opcode::I64Sub : opcode::I64Mul);
            box_integer();
        }
        else
        {
            get(aLeft);
            unbox_integer();
            op(opcode::F64SConvertI64);
            get(aRight);
            unbox_integer();
            op(opcode::F64SConvertI64);
            op(opcode::F64Div);
            op(opcode::I64ReinterpretF64);
        }
        else_();
        get(aLeft);
        is_float();
        get(aRight);
        is_float();
        op(opcode::I32And);
        begin_if(I64);
        get(aLeft);
        op(opcode::F64ReinterpretI64);
        get(aRight);
        op(opcode::F64ReinterpretI64);
        op(aOperator == construct::Add ? opcode::F64Add : aOperator == construct::Subtract ? opcode::F64Sub :
            aOperator == construct::Multiply ? opcode::F64Mul : opcode::F64Div);
        op(opcode::I64ReinterpretF64);
        else_();
        // (an integer and a float: the integer is converted)
        get(aLeft);
        is_integer();
        get(aRight);
        is_float();
        op(opcode::I32And);
        get(aLeft);
        is_float();
        get(aRight);
        is_integer();
        op(opcode::I32And);
        op(opcode::I32Ior);
        begin_if(I64);
        to_float(aLeft);
        to_float(aRight);
        op(aOperator == construct::Add ? opcode::F64Add : aOperator == construct::Subtract ? opcode::F64Sub :
            aOperator == construct::Multiply ? opcode::F64Mul : opcode::F64Div);
        op(opcode::I64ReinterpretF64);
        else_();
        slow();
        end_control();
        end_control();
        end_control();
    }

    void function_generator::comparison(construct aOperator, std::uint32_t aLeft, std::uint32_t aRight, bool aCondition)
    {
        // (an i32 (aCondition) or a boolean)
        if (aOperator == construct::Equal || aOperator == construct::NotEqual)
        {
            // (the same value is equal unless it is NaN; two floats are compared as floats, two (inline) integers are unequal;
            // the runtime compares the others)
            get(aLeft);
            get(aRight);
            op(opcode::I64Eq);
            begin_if(I32);
            get(aLeft);
            is_float();
            begin_if(I32);
            get(aLeft);
            op(opcode::F64ReinterpretI64);
            get(aLeft);
            op(opcode::F64ReinterpretI64);
            op(opcode::F64Eq);
            else_();
            i32(1);
            end_control();
            else_();
            get(aLeft);
            is_float();
            get(aRight);
            is_float();
            op(opcode::I32And);
            begin_if(I32);
            get(aLeft);
            op(opcode::F64ReinterpretI64);
            get(aRight);
            op(opcode::F64ReinterpretI64);
            op(opcode::F64Eq);
            else_();
            get(aLeft);
            is_integer();
            get(aRight);
            is_integer();
            op(opcode::I32And);
            begin_if(I32);
            i32(0);
            else_();
            get(aLeft);
            get(aRight);
            runtime("eq", { I64, I64 }, I64);
            check();
            constant(values::True);
            op(opcode::I64Eq);
            end_control();
            end_control();
            end_control();
            if (aOperator == construct::NotEqual)
                op(opcode::I32Eqz);
        }
        else
        {
            // (a > b is b < a, a >= b is b <= a)
            bool const swap = aOperator == construct::Greater || aOperator == construct::GreaterEqual;
            bool const orEqual = aOperator == construct::LessEqual || aOperator == construct::GreaterEqual;
            auto const left = swap ? aRight : aLeft;
            auto const right = swap ? aLeft : aRight;
            get(left);
            is_integer();
            get(right);
            is_integer();
            op(opcode::I32And);
            begin_if(I32);
            get(left);
            unbox_integer();
            get(right);
            unbox_integer();
            op(orEqual ? opcode::I64LeS : opcode::I64LtS);
            else_();
            get(left);
            is_float();
            get(right);
            is_float();
            op(opcode::I32And);
            begin_if(I32);
            get(left);
            op(opcode::F64ReinterpretI64);
            get(right);
            op(opcode::F64ReinterpretI64);
            op(orEqual ? opcode::F64Le : opcode::F64Lt);
            else_();
            get(left);
            get(right);
            runtime(orEqual ? "le" : "lt", { I64, I64 }, I64);
            check();
            constant(values::True);
            op(opcode::I64Eq);
            end_control();
            end_control();
        }
        if (!aCondition)
        {
            begin_if(I64);
            constant(values::True);
            else_();
            constant(values::False);
            end_control();
        }
    }

    void function_generator::concatenation(node* aNode)
    {
        // (a .. b .. c: the operands (left to right), concatenated by the runtime)
        std::vector<node*> operands;
        auto* n = aNode;
        while (n->is(construct::Concat))
        {
            operands.push_back(n->child(0u));
            n = n->child(1u);
        }
        operands.push_back(n);
        if (operands.size() == 2u)
        {
            auto const a = temporary();
            expression(operands[0]);
            set(a);
            expression(operands[1]);
            auto const b = temporary();
            set(b);
            get(a);
            get(b);
            runtime("concat", { I64, I64 }, I64);
            check();
            release(a);
            release(b);
            return;
        }
        auto const base = push_values(operands);
        get(base);
        runtime("stack_concat", { I32 }, I64);
        check();
        release(base);
    }

    void function_generator::logical(node* aNode)
    {
        auto const a = temporary();
        expression(aNode->child(0u));
        tee(a);
        truth();
        begin_if(I64);
        if (aNode->is(construct::And))
            expression(aNode->child(1u));
        else
            get(a);
        else_();
        if (aNode->is(construct::And))
            get(a);
        else
            expression(aNode->child(1u));
        end_control();
        release(a);
    }

    void function_generator::unary(node* aNode)
    {
        if (aNode->is(construct::Not))
        {
            condition(aNode->child(0u));
            begin_if(I64);
            constant(values::False);
            else_();
            constant(values::True);
            end_control();
            return;
        }
        if (aNode->is(construct::Negate) && integer_expression(aNode))
        {
            raw_integer(aNode, {}, std::nullopt);
            box_integer();
            return;
        }
        if (aNode->is(construct::Negate) && float_expression(aNode))
        {
            raw_float(aNode);
            op(opcode::I64ReinterpretF64);
            return;
        }
        auto const a = temporary();
        expression(aNode->child(0u));
        set(a);
        if (aNode->is(construct::Negate))
        {
            get(a);
            is_integer();
            begin_if(I64);
            i64(0);
            get(a);
            unbox_integer();
            op(opcode::I64Sub);
            box_integer();
            else_();
            get(a);
            is_float();
            begin_if(I64);
            get(a);
            op(opcode::F64ReinterpretI64);
            op(opcode::F64Neg);
            op(opcode::I64ReinterpretF64);
            else_();
            get(a);
            description(describe(aNode->child(0u)));
            runtime("unm", { I64, I64 }, I64);
            check();
            end_control();
            end_control();
        }
        else
        {
            get(a);
            description(describe(aNode->child(0u)));
            runtime(aNode->is(construct::Length) ? "len" : "bnot", { I64, I64 }, I64);
            check();
        }
        release(a);
    }
}
