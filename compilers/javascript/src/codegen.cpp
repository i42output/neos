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

// JavaScript (ECMAScript): code generation (one WebAssembly function for each JavaScript function, and one for the
// script); see compiler.hpp
//
// Every JavaScript function has the signature (callee, this, new.target, argument count, the first four arguments) ->
// result; a function's arguments that follow the first four are on the runtime's extra arguments stack. Calls of the
// program's own functions go through the dispatcher (a function that calls the program's function whose number it is
// given); others through the runtime. A call that throws returns the value Exception (the exception is pending in the
// runtime): the code that called it branches to the innermost exception handler.

#include <neolib/neolib.hpp>
#include <algorithm>
#include <set>
#include <cmath>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <neos/bytecode/bytecode.hpp>
#include <neos/language/emit.hpp>
#include "compiler.hpp"
#include "regexp_engine.hpp"

namespace neos::compilers::javascript
{
    using namespace emitter;

    std::string name_of(node const* aNode);

    namespace
    {
        std::string operator_text(std::string_view aBetween);
    }

    namespace
    {
        constexpr type I64 = type::I64;
        constexpr type I32 = type::I32;
        constexpr type F64 = type::F64;
        constexpr type STR = type::String;

        // completion kinds (finally blocks)
        constexpr std::int32_t CompletionNormal = 0;
        constexpr std::int32_t CompletionThrow = 1;
        constexpr std::int32_t CompletionReturn = 2;
        constexpr std::int32_t CompletionJumps = 3;

        void append_utf16(std::u16string& aResult, char32_t aCodePoint)
        {
            if (aCodePoint >= 0x10000u)
            {
                aCodePoint -= 0x10000u;
                aResult.push_back(static_cast<char16_t>(0xD800u + (aCodePoint >> 10)));
                aResult.push_back(static_cast<char16_t>(0xDC00u + (aCodePoint & 0x3FFu)));
            }
            else
                aResult.push_back(static_cast<char16_t>(aCodePoint));
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

        // the UTF-16 code units of a string literal's or template's characters (without quotes); nullopt: an invalid escape
        // (a template's cooked value is then undefined); aLegacyOctal: an octal escape (strict mode forbids them)
        std::optional<std::u16string> unescape(std::string_view aText, bool aTemplate, bool* aLegacyOctal = nullptr)
        {
            std::u16string result;
            for (std::size_t i = 0u; i < aText.size();)
            {
                auto const c = static_cast<unsigned char>(aText[i]);
                if (c == '\r' && aTemplate)
                {
                    // (CR and CRLF are LF in templates)
                    result.push_back(u'\n');
                    i += (i + 1u < aText.size() && aText[i + 1u] == '\n') ? 2u : 1u;
                    continue;
                }
                if (c != '\\')
                {
                    std::size_t n = c < 0x80u ? 1u : c < 0xE0u ? 2u : c < 0xF0u ? 3u : 4u;
                    char32_t cp = n == 1u ? c : n == 2u ? (c & 0x1Fu) : n == 3u ? (c & 0x0Fu) : (c & 0x07u);
                    for (std::size_t k = 1u; k < n && i + k < aText.size(); ++k)
                        cp = (cp << 6) | (static_cast<unsigned char>(aText[i + k]) & 0x3Fu);
                    append_utf16(result, cp);
                    i += n;
                    continue;
                }
                if (++i >= aText.size())
                    return std::nullopt;
                char const e = aText[i++];
                switch (e)
                {
                case 'n': result.push_back(u'\n'); break;
                case 'r': result.push_back(u'\r'); break;
                case 't': result.push_back(u'\t'); break;
                case 'b': result.push_back(u'\b'); break;
                case 'f': result.push_back(u'\f'); break;
                case 'v': result.push_back(u'\v'); break;
                case '\r':
                    if (i < aText.size() && aText[i] == '\n')
                        ++i;
                    break;
                case '\n':
                    break;
                case 'x':
                    {
                        if (i + 2u > aText.size() || hex_value(aText[i]) < 0 || hex_value(aText[i + 1u]) < 0)
                            return std::nullopt;
                        result.push_back(static_cast<char16_t>(hex_value(aText[i]) * 16 + hex_value(aText[i + 1u])));
                        i += 2u;
                    }
                    break;
                case 'u':
                    {
                        char32_t cp = 0u;
                        if (i < aText.size() && aText[i] == '{')
                        {
                            ++i;
                            std::size_t digits = 0u;
                            while (i < aText.size() && aText[i] != '}')
                            {
                                auto const h = hex_value(aText[i++]);
                                if (h < 0)
                                    return std::nullopt;
                                cp = cp * 16u + static_cast<char32_t>(h);
                                if (cp > 0x10FFFFu)
                                    return std::nullopt;
                                ++digits;
                            }
                            if (i >= aText.size() || digits == 0u)
                                return std::nullopt;
                            ++i;
                        }
                        else
                        {
                            if (i + 4u > aText.size())
                                return std::nullopt;
                            for (std::size_t k = 0u; k < 4u; ++k)
                            {
                                auto const h = hex_value(aText[i++]);
                                if (h < 0)
                                    return std::nullopt;
                                cp = cp * 16u + static_cast<char32_t>(h);
                            }
                        }
                        append_utf16(result, cp);
                    }
                    break;
                default:
                    if (e >= '0' && e <= '7')
                    {
                        if (e == '0' && (i >= aText.size() || aText[i] < '0' || aText[i] > '9'))
                        {
                            result.push_back(u'\0');
                            break;
                        }
                        if (aTemplate)
                            return std::nullopt;
                        if (aLegacyOctal != nullptr)
                            *aLegacyOctal = true;
                        // (legacy octal: up to three digits, at most 0377)
                        unsigned value = static_cast<unsigned>(e - '0');
                        std::size_t const maximum = e <= '3' ? 2u : 1u;
                        for (std::size_t k = 0u; k < maximum && i < aText.size() && aText[i] >= '0' && aText[i] <= '7'; ++k)
                            value = value * 8u + static_cast<unsigned>(aText[i++] - '0');
                        result.push_back(static_cast<char16_t>(value));
                        break;
                    }
                    if (e == '8' || e == '9')
                    {
                        if (aTemplate)
                            return std::nullopt;
                        if (aLegacyOctal != nullptr)
                            *aLegacyOctal = true;
                    }
                    {
                        // (any other character is itself (UTF-8: its code point))
                        --i;
                        auto const c2 = static_cast<unsigned char>(aText[i]);
                        std::size_t n = c2 < 0x80u ? 1u : c2 < 0xE0u ? 2u : c2 < 0xF0u ? 3u : 4u;
                        char32_t cp = n == 1u ? c2 : n == 2u ? (c2 & 0x1Fu) : n == 3u ? (c2 & 0x0Fu) : (c2 & 0x07u);
                        for (std::size_t k = 1u; k < n && i + k < aText.size(); ++k)
                            cp = (cp << 6) | (static_cast<unsigned char>(aText[i + k]) & 0x3Fu);
                        // (LS and PS are line continuations after a backslash)
                        if (cp != 0x2028u && cp != 0x2029u)
                            append_utf16(result, cp);
                        i += n;
                    }
                    break;
                }
            }
            return result;
        }

        // UTF-16 as the runtime's literals are encoded: WTF-8 (lone surrogates are encoded as code points) with NUL as C0 80
        std::string literal_encoding(std::u16string_view aText)
        {
            std::string result;
            for (std::size_t i = 0u; i < aText.size(); ++i)
            {
                char32_t cp = aText[i];
                if (cp >= 0xD800u && cp < 0xDC00u && i + 1u < aText.size() && aText[i + 1u] >= 0xDC00u && aText[i + 1u] < 0xE000u)
                {
                    cp = 0x10000u + ((cp - 0xD800u) << 10) + (aText[i + 1u] - 0xDC00u);
                    ++i;
                }
                if (cp == 0u)
                {
                    result.push_back('\xC0');
                    result.push_back('\x80');
                }
                else if (cp < 0x80u)
                    result.push_back(static_cast<char>(cp));
                else if (cp < 0x800u)
                {
                    result.push_back(static_cast<char>(0xC0u | (cp >> 6)));
                    result.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                }
                else if (cp < 0x10000u)
                {
                    result.push_back(static_cast<char>(0xE0u | (cp >> 12)));
                    result.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                    result.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                }
                else
                {
                    result.push_back(static_cast<char>(0xF0u | (cp >> 18)));
                    result.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
                    result.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                    result.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                }
            }
            return result;
        }

        std::string utf8_encoding(std::string_view aText)
        {
            // (UTF-8 source text as a literal: NUL as C0 80)
            std::string result;
            for (auto c : aText)
            {
                if (c == '\0')
                {
                    result.push_back('\xC0');
                    result.push_back('\x80');
                }
                else
                    result.push_back(c);
            }
            return result;
        }

        // a numeric literal's value (none: a BigInt)
        std::optional<double> number_value(std::string_view aText, bool& aLegacyOctal)
        {
            std::string text;
            for (auto c : aText)
                if (c != '_')
                    text.push_back(c);
            if (!text.empty() && text.back() == 'n')
                return std::nullopt;
            if (text.size() > 1u && text[0] == '0' && std::isalpha(static_cast<unsigned char>(text[1])) && text[1] != 'e' && text[1] != 'E')
            {
                int const radix = (text[1] == 'x' || text[1] == 'X') ? 16 : (text[1] == 'o' || text[1] == 'O') ? 8 : 2;
                if (radix == 16)
                    return std::strtod(text.c_str(), nullptr);
                double value = 0.0;
                for (std::size_t i = 2u; i < text.size(); ++i)
                    value = value * radix + (text[i] - '0');
                return value;
            }
            if (text.size() > 1u && text[0] == '0' && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
            {
                aLegacyOctal = true;
                if (std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '7'; }))
                {
                    double value = 0.0;
                    for (auto c : text)
                        value = value * 8 + (c - '0');
                    return value;
                }
            }
            return std::strtod(text.c_str(), nullptr);
        }

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

        // (the closure flags (see the runtime's closure function))
        enum : std::int32_t
        {
            ClosureStrict = 0x1,
            ClosureConstructor = 0x2,
            ClosureArrow = 0x4,
            ClosureMethod = 0x8,
            ClosureGenerator = 0x10,
            ClosureAsync = 0x20,
            ClosureClassConstructor = 0x40,
            ClosureDerived = 0x80,
            ClosureGetter = 0x100,
            ClosureSetter = 0x200
        };
    }

    // --- code generation (the module)

    void compiler::generate(node* aScript)
    {
        try
        {
            analyse(aScript);
            iLiterals.clear();
            iLiteralOrder.clear();
            iFrameNames.clear();
            iTemplateSites = 0u;
            // (the functions: their WebAssembly functions)
            function_signature signature;
            signature.set_return_type(I64);
            {
                function_parameters parameters;
                for (auto t : { I64, I64, I64, I32, I64, I64, I64, I64 })
                {
                    function_parameter p;
                    p.set_parameter_type(t);
                    parameters.push_back(p);
                }
                signature.set_parameters(parameters);
            }
            std::vector<function_info*> all;
            for (auto& f : iFunctions)
                all.push_back(&f);
            std::uint32_t number = 0u;
            for (auto* f : all)
            {
                auto const frame = f->script ? std::string{ "<script>" } : f->name.empty() ? std::string{ "<anonymous>" } : f->name;
                auto const line = f->declaration != nullptr ? source_line_of(f->declaration->source) : 1u;
                signature.set_name(neolib::string{ "%js." + std::to_string(number) });
                f->index = generator().define_function(neolib::string{ "%js." + std::to_string(number) }, signature, neolib::vector<type>{},
                    neolib::string{ frame }, line);
                iFrameNames[f->index] = frame;
                ++number;
                if ((f->generator || f->async) && !f->script)
                {
                    signature.set_name(neolib::string{ "%js." + std::to_string(number) + ".body" });
                    f->bodyIndex = generator().define_function(neolib::string{ "%js." + std::to_string(number) + ".body" }, signature, neolib::vector<type>{},
                        neolib::string{ frame }, line);
                    iFrameNames[f->bodyIndex] = frame;
                    ++number;
                }
            }
            // (the dispatcher: (function number, callee, this, new.target, count, arguments) -> result)
            {
                function_signature dispatcher;
                dispatcher.set_return_type(I64);
                function_parameters parameters;
                for (auto t : { I32, I64, I64, I64, I32, I64, I64, I64, I64 })
                {
                    function_parameter p;
                    p.set_parameter_type(t);
                    parameters.push_back(p);
                }
                dispatcher.set_parameters(parameters);
                dispatcher.set_name(neolib::string{ "%js.dispatch" });
                iDispatcher = generator().define_function(neolib::string{ "%js.dispatch" }, dispatcher, neolib::vector<type>{}, neolib::string{}, 1u);
            }
            // (each function's code)
            for (auto* f : all)
            {
                function_generator{ *this, *f }.generate();
                if ((f->generator || f->async) && !f->script)
                    function_generator{ *this, *f, true }.generate();
            }
            // the dispatcher's code: a binary search of the function numbers (each leaf calls its function and returns)
            {
                std::vector<std::uint32_t> targets;
                for (auto* f : all)
                {
                    targets.push_back(f->index);
                    if ((f->generator || f->async) && !f->script)
                        targets.push_back(f->bodyIndex);
                }
                fragment body;
                auto& code = body.fragmentCode.as_std_vector();
                std::function<void(std::uint32_t, std::uint32_t)> search = [&](std::uint32_t aLow, std::uint32_t aHigh)
                {
                    // (the numbers [aLow, aHigh))
                    if (aHigh - aLow == 1u)
                    {
                        for (std::uint32_t p = 1u; p <= 8u; ++p)
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
                put_s64(code, static_cast<std::int64_t>(values::Undefined));
                generator().set_function_body(iDispatcher, body);
            }
            // the entry point: the runtime is initialized, the literals are created, the script's function is called (an exception
            // that it does not handle is reported) and the jobs it queued run
            ensure_function();
            auto statement = fragment{};
            statement.fragmentKind = fragment_kind::Statement;
            auto& code = statement.fragmentCode.as_std_vector();
            auto const call = [&](std::string const& aName, std::vector<type> const& aParameters, type aResult)
            {
                emit_call_to(statement, runtime_function(aName, aParameters, aResult));
            };
            put_op(code, opcode::I32Const);
            statement.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), iDispatcher });
            code.resize(code.size() + 5u);
            call("javascript::init", { I32 }, I64);
            put_op(code, opcode::Drop);
            std::optional<std::uint32_t> moduleDescriptor;
            if (iModule)
                moduleDescriptor = literal_string(utf8_encoding(module_descriptor(aScript) + "N\x01" +
                    function_generator{ *this, iFunctions.front() }.names_descriptor(iFunctions.front().top) + "\x02"));
            for (auto const& [text, global] : iLiteralOrder)
            {
                put_op(code, opcode::I32Const);
                put_s64(code, static_cast<std::int32_t>(string_literal(text)));
                call("javascript::string", { STR }, I64);
                put_op(code, opcode::GlobalSet);
                put_u32(code, global);
            }
            if (iModule)
            {
                // (a module: defined (its function and its imports and exports); the program's module is then linked and evaluated)
                put_op(code, opcode::I32Const);
                statement.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), iFunctions.front().index });
                code.resize(code.size() + 5u);
                put_op(code, opcode::GlobalGet);
                put_u32(code, *moduleDescriptor);
                call("javascript::module_define", { I32, I64 }, I64);
                put_op(code, opcode::Drop);
                if (iModuleMain)
                {
                    call("javascript::module_main", {}, I32);
                    put_op(code, opcode::If);
                    code.push_back(0x40u);
                    put_op(code, opcode::I32Const);
                    put_s64(code, 1);
                    call("neos::exit", { I32 }, type::Void);
                    put_op(code, opcode::End);
                }
                push_operand(operand_type{ statement });
                generator().end_indented();
                return;
            }
            // (eval code (and a script evaluated for its completion value): its this, new.target and environment are the eval's; its
            // result (or exception) is the eval's)
            bool const evaluation = eval_code() || iKind == code_kind::EvaluateScript;
            put_op(code, opcode::I64Const);
            put_s64(code, static_cast<std::int64_t>(values::Undefined));
            if (evaluation)
                call("javascript::eval_this", {}, I64);
            else
                call("javascript::global_this", {}, I64);
            if (evaluation)
                call("javascript::eval_new_target", {}, I64);
            else
            {
                put_op(code, opcode::I64Const);
                put_s64(code, static_cast<std::int64_t>(values::Undefined));
            }
            put_op(code, opcode::I32Const);
            put_s64(code, 0);
            if (evaluation)
                call("javascript::eval_environment", {}, I64);
            else
            {
                put_op(code, opcode::I64Const);
                put_s64(code, static_cast<std::int64_t>(values::Undefined));
            }
            for (int i = 1; i < 4; ++i)
            {
                put_op(code, opcode::I64Const);
                put_s64(code, static_cast<std::int64_t>(values::Undefined));
            }
            put_op(code, opcode::CallFunction);
            statement.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()), iFunctions.front().index });
            code.resize(code.size() + 5u);
            if (evaluation)
            {
                call("javascript::eval_complete", { I64 }, I64);
                put_op(code, opcode::Drop);
            }
            else
            {
                // (an exception that the script did not handle: the program's exit status is 1)
                call("javascript::finish", { I64 }, I32);
                put_op(code, opcode::If);
                code.push_back(0x40u);
                put_op(code, opcode::I32Const);
                put_s64(code, 1);
                call("neos::exit", { I32 }, type::Void);
                put_op(code, opcode::End);
            }
            push_operand(operand_type{ statement });
            generator().end_indented();
        }
        catch (syntax_error const& e)
        {
            auto const position = neolib::string_view{ e.source.empty() ? iSource : e.source };
            generator().throw_error(position.begin(), neolib::string{ e.what() }, neolib::string{ "SyntaxError" });
        }
    }

    // --- code generation (a function)

    function_generator::function_generator(compiler& aCompiler, function_info& aFunction, bool aBody) :
        iCompiler{ aCompiler }, iFunction{ aFunction }, iBody{ aBody }
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

    namespace
    {
        // (NEOS_JS_WASM=<file>: each function's code is appended to the file (for debugging code generation): its name, locals, code
        // (hex) and calls)
        char const* wasm_dump_path()
        {
            static char const* const sPath = std::getenv("NEOS_JS_WASM");
            return sPath;
        }

        std::map<std::uint32_t, std::string>& wasm_dump_names()
        {
            static std::map<std::uint32_t, std::string> sNames;
            return sNames;
        }
    }

    void function_generator::runtime(std::string const& aName, std::vector<type> const& aParameters, type aResult)
    {
        auto const index = iCompiler.runtime_function("javascript::" + aName, aParameters, aResult);
        if (wasm_dump_path() != nullptr)
            wasm_dump_names()[index] = aName;
        call(index);
    }

    void function_generator::function_index(function_info& aFunction, bool aBody)
    {
        op(opcode::I32Const);
        iRelocations.emplace_back(static_cast<std::uint32_t>(iCode.size()), aBody ? aFunction.bodyIndex : aFunction.index);
        iCode.resize(iCode.size() + 5u);
    }

    void function_generator::begin(control_kind aKind, std::optional<type> aResult, std::vector<std::string> aLabels)
    {
        op(aKind == control_kind::Loop ? opcode::Loop : opcode::Block);
        iCode.push_back(aResult ? *wasm_type(*aResult) : 0x40u);
        control c{ aKind };
        c.labels = std::move(aLabels);
        c.environment = current_environment();
        iControls.push_back(std::move(c));
    }

    void function_generator::end_control()
    {
        op(opcode::End);
        iControls.pop_back();
    }

    std::size_t function_generator::handler() const
    {
        for (std::size_t i = iControls.size(); i-- > 0u;)
            if (iControls[i].kind == control_kind::Handler || iControls[i].kind == control_kind::Exit)
                return i;
        throw std::logic_error("javascript: no exception handler");
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
        branch(handler(), true);
        get(iScratch);
    }

    void function_generator::check_void()
    {
        check();
        drop();
    }

    void function_generator::string_value(std::string_view aUtf16AsLiteral)
    {
        op(opcode::GlobalGet);
        u32(iCompiler.literal_string(std::string{ aUtf16AsLiteral }));
    }

    void function_generator::key(std::string_view aName)
    {
        // (an identifier name (UTF-8, unicode escapes resolved))
        string_value(utf8_encoding(aName));
    }

    // --- environments

    std::optional<std::uint32_t> function_generator::current_environment() const
    {
        for (auto s = iScopes.rbegin(); s != iScopes.rend(); ++s)
            if ((*s)->hasEnvironment && (*s)->environmentLocal)
                return (*s)->environmentLocal;
        return iFunctionEnvironment;
    }

    void function_generator::environment_value()
    {
        if (auto const e = current_environment())
            get(*e);
        else
            undefined();
    }

    std::string function_generator::names_descriptor(scope const* aScope) const
    {
        // (its bindings can be found by name: their names (and kinds), in slot order; V: a var scope (a sloppy direct eval's var
        // declarations are added to it))
        std::vector<std::pair<std::uint32_t, std::string>> entries;
        for (auto& b : aScope->bindings)
            if (b->captured)
            {
                char kind = 'v';
                switch (b->kind)
                {
                case binding_kind::Let: kind = 'l'; break;
                case binding_kind::Const: kind = b->name.starts_with("#") ? 'i' : 'c'; break;
                case binding_kind::Class: kind = 'l'; break;
                case binding_kind::Function: kind = 'f'; break;
                case binding_kind::Parameter: kind = 'p'; break;
                case binding_kind::Catch: kind = 'k'; break;
                case binding_kind::Callee: kind = 'n'; break;
                case binding_kind::Import: kind = 'm'; break;
                case binding_kind::Internal: kind = 'i'; break;
                default: break;
                }
                entries.emplace_back(b->slot, std::string{ kind } + b->name);
            }
        std::sort(entries.begin(), entries.end());
        std::string descriptor = (aScope->kind == scope_kind::Function && aScope == aScope->function->top) ? "V" : "-";
        for (auto const& e : entries)
            descriptor += "," + e.second;
        return descriptor;
    }

    void function_generator::enter_scope(scope* aScope)
    {
        if (aScope->hasEnvironment && aScope->kind != scope_kind::With)
        {
            environment_value();
            i32(static_cast<std::int32_t>(aScope->environmentSize));
            runtime("env_new", { I64, I32 }, I64);
            aScope->environmentLocal = local();
            set(*aScope->environmentLocal);
            if (aScope->dynamic || iCompiler.sloppy_eval_var_scope(aScope))
            {
                auto const descriptor = names_descriptor(aScope);
                get(*aScope->environmentLocal);
                key(descriptor);
                runtime("env_names", { I64, I64 }, I64);
                drop();
            }
        }
        iScopes.push_back(aScope);
        // (its bindings that are locals: uninitialized (lexical declarations) or undefined)
        for (auto& b : aScope->bindings)
        {
            auto const rep = rep_of(b.get());
            if (!b->captured)
                b->slot = local(rep == numeric_rep::F64 ? F64 : rep == numeric_rep::I32 ? I32 : I64);
            if (b->kind == binding_kind::Internal || (b->kind == binding_kind::Parameter && !b->tdz) || rep != numeric_rep::None)
                continue;
            if (b->kind == binding_kind::Parameter)
            {
                constant(values::Empty);    // (uninitialized until it is bound)
                initialize_binding(b.get());
                continue;
            }
            if (b->kind == binding_kind::Var || b->kind == binding_kind::Function || b->kind == binding_kind::Catch)
            {
                if (aScope->kind == scope_kind::Script && (!iCompiler.eval_code() || !iFunction.strict))
                    continue;
                undefined();
                initialize_binding(b.get());
            }
            else if (b->tdz || b->lexical())
            {
                if (aScope->kind == scope_kind::Script && !iCompiler.eval_code())
                    continue;
                constant(values::Empty);
                initialize_binding(b.get());
            }
        }
    }

    void function_generator::leave_scope(scope* aScope)
    {
        if (!iScopes.empty() && iScopes.back() == aScope)
            iScopes.pop_back();
    }

    // --- bindings

    function_generator::reference function_generator::resolve(std::string const& aName, node* aSite)
    {
        (void)aSite;
        reference result;
        result.name = aName;
        // (the scopes of the function whose code is being generated, then those of the functions that enclose it; a name that may be a
        // with statement's object's property or a binding that a sloppy direct eval declared (or, in eval code, a binding of the code that
        // called eval) is found by name at run time)
        std::uint32_t hops = 0u;
        bool dynamic = false;
        scope* start = iScopes.empty() ? iFunction.top : iScopes.back();
        for (auto* s = start; s != nullptr; s = s->parent)
        {
            if (s->kind == scope_kind::With)
            {
                dynamic = true;
                if (s->hasEnvironment)
                    ++hops;
                continue;
            }
            if (s->kind == scope_kind::Script && !iCompiler.eval_code() && !iCompiler.iModule)
            {
                if (auto* b = s->find(aName); b != nullptr && !b->captured && s->function == &iFunction && b->lexical() && !iCompiler.iModule && !dynamic)
                {
                    // (a top-level lexical declaration that no function uses: a local of the script's function)
                    result.kind = reference::Local;
                    result.target = b;
                    result.local = b->slot;
                    return result;
                }
                else if (b != nullptr && b->lexical())
                    result.globalLexical = true;
                break;
            }
            auto* b = s->find(aName);
            if (b != nullptr && s->kind == scope_kind::Script && !s->function->strict && (b->kind == binding_kind::Var || b->kind == binding_kind::Function))
                b = nullptr;    // (sloppy eval code's var declarations are the var bindings of the code that called eval)
            if (b != nullptr && b->kind != binding_kind::Internal)
            {
                result.target = b;
                if (dynamic)
                {
                    result.kind = reference::Dynamic;
                    return result;
                }
                if (!b->captured)
                {
                    result.kind = reference::Local;
                    result.local = b->slot;
                    return result;
                }
                result.kind = reference::Environment;
                result.hops = hops;
                return result;
            }
            if (s->hasEnvironment)
                ++hops;
            if (iCompiler.sloppy_eval_var_scope(s) || (s->kind == scope_kind::Script && iCompiler.eval_code()))
                dynamic = true;
        }
        result.target = nullptr;
        result.kind = dynamic ? reference::Dynamic : reference::Global;
        return result;
    }

    void function_generator::load(reference const& aReference, bool aTypeof, node* aSite)
    {
        (void)aSite;
        std::size_t opened = 0u;
        switch (aReference.kind)
        {
        case reference::Local:
            get(aReference.local);
            if (auto const rep = rep_of(aReference.target); rep != numeric_rep::None)
            {
                counter_value(aReference.target);
                box(rep);
                break;
            }
            if (aReference.target != nullptr && (aReference.target->tdz || aReference.target->lexical()))
            {
                // (an uninitialized binding: a ReferenceError)
                tee(iScratch);
                constant(values::Empty);
                op(opcode::I64Eq);
                op(opcode::If);
                iCode.push_back(0x40u);
                iControls.push_back(control{ control_kind::Block });
                key(aReference.name);
                runtime("throw_tdz", { I64 }, I64);
                check_void();
                end_control();
                get(iScratch);
            }
            break;
        case reference::Environment:
            environment_value();
            i32(static_cast<std::int32_t>(aReference.hops));
            i32(static_cast<std::int32_t>(aReference.target->slot));
            if (aReference.target->kind == binding_kind::Import)
            {
                // (an imported binding: the binding it refers to (in the module that exports it))
                key(aReference.name);
                runtime("import_get", { I64, I32, I32, I64 }, I64);
                check();
            }
            else if (aReference.target->tdz || aReference.target->lexical())
            {
                key(aReference.name);
                runtime("env_get_checked", { I64, I32, I32, I64 }, I64);
                check();
            }
            else
                runtime("env_get", { I64, I32, I32 }, I64);
            break;
        case reference::Global:
            key(aReference.name);
            runtime(aTypeof ? "global_get_typeof" : "global_get", { I64 }, I64);
            check();
            break;
        case reference::Dynamic:
            environment_value();
            key(aReference.name);
            if (aReference.name.starts_with("#"))
            {
                // (eval code's private name: a class's (an internal binding of an environment that encloses the call))
                runtime("eval_internal", { I64, I64 }, I64);
                check();
                break;
            }
            i32((aTypeof ? 1 : 0) | (iFunction.strict ? 2 : 0));
            runtime("dyn_get", { I64, I64, I32 }, I64);
            check();
            break;
        default:
            break;
        }
        for (; opened > 0u; --opened)
            end_control();
    }

    void function_generator::store(reference const& aReference, bool aInitialize, node* aSite)
    {
        (void)aSite;
        // (the value is on the stack; it is left there)
        auto const value = temporary();
        set(value);
        std::size_t opened = 0u;
        auto const constant_check = [&]()
        {
            // (an assignment to a const binding (or a named function expression's own name in strict code) throws)
            if (aInitialize || aReference.target == nullptr)
                return false;
            if (aReference.target->kind == binding_kind::Const || aReference.target->kind == binding_kind::Import)
            {
                load(aReference);   // (TDZ first)
                drop();
                key(aReference.name);
                runtime("throw_const", { I64 }, I64);
                check_void();
                return true;
            }
            if (aReference.target->kind == binding_kind::Callee)
            {
                if (iFunction.strict)
                {
                    key(aReference.name);
                    runtime("throw_const", { I64 }, I64);
                    check_void();
                }
                return true;
            }
            return false;
        };
        switch (aReference.kind)
        {
        case reference::Local:
            if (!constant_check())
            {
                auto const rep = rep_of(aReference.target);
                if (!aInitialize && rep == numeric_rep::None && aReference.target != nullptr && (aReference.target->tdz || aReference.target->lexical()))
                {
                    load(aReference);
                    drop();
                }
                get(value);
                unbox(rep);
                set(aReference.local);
            }
            break;
        case reference::Environment:
            if (!constant_check())
            {
                environment_value();
                i32(static_cast<std::int32_t>(aReference.hops));
                i32(static_cast<std::int32_t>(aReference.target->slot));
                get(value);
                if (!aInitialize && (aReference.target->tdz || aReference.target->lexical()))
                {
                    key(aReference.name);
                    runtime("env_set_checked", { I64, I32, I32, I64, I64 }, I64);
                    check_void();
                }
                else
                {
                    runtime("env_set", { I64, I32, I32, I64 }, I64);
                    drop();
                }
            }
            break;
        case reference::Dynamic:
            environment_value();
            key(aReference.name);
            get(value);
            i32((iFunction.strict ? 1 : 0) | (aInitialize ? 2 : 0));
            runtime("dyn_set", { I64, I64, I64, I32 }, I64);
            check_void();
            break;
        case reference::Global:
            key(aReference.name);
            get(value);
            if (aInitialize && aReference.globalLexical)
            {
                runtime("global_init", { I64, I64 }, I64);
                drop();
            }
            else
            {
                i32(iFunction.strict ? 1 : 0);
                runtime("global_set", { I64, I64, I32 }, I64);
                check_void();
            }
            break;
        default:
            break;
        }
        for (; opened > 0u; --opened)
            end_control();
        get(value);
        release(value);
    }

    void function_generator::load_binding(binding* aBinding)
    {
        if (!aBinding->captured)
        {
            get(aBinding->slot);
            return;
        }
        // (a binding of a scope of this function: its hops from the innermost environment)
        std::uint32_t hops = 0u;
        for (auto s = iScopes.rbegin(); s != iScopes.rend(); ++s)
        {
            if (*s == aBinding->owner)
                break;
            if ((*s)->hasEnvironment)
                ++hops;
        }
        environment_value();
        i32(static_cast<std::int32_t>(hops));
        i32(static_cast<std::int32_t>(aBinding->slot));
        runtime("env_get", { I64, I32, I32 }, I64);
    }

    void function_generator::initialize_binding(binding* aBinding)
    {
        if (!aBinding->captured)
        {
            unbox(rep_of(aBinding));
            set(aBinding->slot);
            return;
        }
        auto const value = temporary();
        set(value);
        std::uint32_t hops = 0u;
        for (auto s = iScopes.rbegin(); s != iScopes.rend(); ++s)
        {
            if (*s == aBinding->owner)
                break;
            if ((*s)->hasEnvironment)
                ++hops;
        }
        environment_value();
        i32(static_cast<std::int32_t>(hops));
        i32(static_cast<std::int32_t>(aBinding->slot));
        get(value);
        runtime("env_set", { I64, I32, I32, I64 }, I64);
        drop();
        release(value);
    }

    // --- the function

    bool function_generator::this_lexical() const
    {
        return iFunction.arrow;
    }

    void function_generator::generate()
    {
        iScratch = local();
        // (a numeric version: when its parameters' arguments are numbers (see version_check))
        bool versioned = false;
        if (version_eligible())
        {
            iNumericParameters = true;
            infer_numeric();
            versioned = !iNumericArguments.empty() || iIntrinsicMask != 0u;
            iNumericParameters = versioned;
        }
        if (!versioned)
            infer_numeric();
        begin(control_kind::Exit);
        auto const generate_code = [&]()
        {
        prologue();
        auto* declaration = iFunction.declaration;
        if (iFunction.script)
        {
            // (the script: its function declarations, then its statements)
            auto* script = iScopes.empty() ? nullptr : iScopes.front()->owner;
            if (iCompiler.eval_code() || iCompiler.iKind == compiler::code_kind::EvaluateScript)
            {
                iCompletion = local();
                undefined();
                set(*iCompletion);
            }
            if (iCompiler.iModule)
            {
                // (a module's function: instantiation (its second argument true): its bindings are initialized and its functions created;
                // else evaluation)
                get(FirstArgumentLocal + 1u);
                constant(values::True);
                op(opcode::I64Eq);
                op(opcode::If);
                iCode.push_back(0x40u);
                iControls.push_back(control{ control_kind::Block });
                for (auto& b : iFunction.top->bindings)
                {
                    if (b->kind == binding_kind::Var)
                    {
                        undefined();
                        initialize_binding(b.get());
                    }
                    else if (b->lexical())
                    {
                        constant(values::Empty);
                        initialize_binding(b.get());
                    }
                }
                hoist_functions(script, iFunction.top);
                for (auto* item : script->children)
                    if (item->is(construct::Export) && item->size() > 0u &&
                        (item->child(0u)->is(construct::FunctionExpression) || item->child(0u)->is(construct::AsyncFunctionExpression)))
                    {
                        // (export default function () {}: hoisted, named default)
                        closure(*item->child(0u)->function);
                        key("default");
                        runtime("name_function", { I64, I64 }, I64);
                        initialize_binding(iFunction.top->find("*default*"));
                    }
                undefined();
                op(opcode::Return);
                end_control();
            }
            else
                hoist_functions(script, iFunction.top);
            if (iCompiler.iModule && has_using(script))
                disposal_scope([&]() { statements(script); });     // (a module's using declarations: disposed when it has been evaluated)
            else
                statements(script);
            if (iCompletion)
                get(*iCompletion);
            else
                undefined();
            complete_return();
        }
        else if (iFunction.fieldInitializer)
        {
            // (a field's initializer: its argument is the field's name (for an anonymous function's name))
            expression(declaration->child(1u));
            if (is_anonymous_function(declaration->child(1u)))
            {
                get(FirstArgumentLocal);
                runtime("name_function", { I64, I64 }, I64);
            }
            complete_return();
        }
        else if (iFunction.staticBlock)
        {
            hoist_functions(declaration, iFunction.top);
            if (has_using(declaration))
                disposal_scope([&]() { statements(declaration); });
            else
                statements(declaration);
            undefined();
            complete_return();
        }
        else if ((iFunction.generator || iFunction.async) && !iBody)
        {
            // (the generator or async function: its body runs later (as a coroutine))
            callee_value();
            get(iThisLocal);
            environment_value();
            function_index(iFunction, true);
            i32((iFunction.generator ? 1 : 0) | (iFunction.async ? 2 : 0) | (iFunction.arrow ? 4 : 0));
            runtime("start", { I64, I64, I64, I32, I32 }, I64);
            check();
            op(opcode::Return);
        }
        else
        {
            auto* body = declaration->children.back();
            if (body->is(construct::Body))
            {
                hoist_functions(body, iFunction.body);
                if (has_using(body))
                    disposal_scope([&]() { statements(body); });
                else
                    statements(body);
                undefined();
                complete_return();
            }
            else
            {
                expression(body);
                complete_return();
            }
        }
        };
        if (versioned)
        {
            // (the numeric version is a block that the function's block starts with, which it leaves (br 1): what follows it (the general
            // version) is cold code to the JIT)
            op(opcode::Block);
            iCode.push_back(0x40u);
            iControls.push_back(control{ control_kind::Block });
            version_check();
            op(opcode::I32Eqz);
            op(opcode::BrIf);
            u32(0u);
            auto const scopes = iScopes;
            generate_code();
            op(opcode::Br);
            u32(1u);
            end_control();
            // (the general version)
            iScopes = scopes;
            iThisLocal = ThisLocal;
            iFunctionEnvironment = std::nullopt;
            iNumericParameters = false;
            infer_numeric();
            generate_code();
        }
        else
            generate_code();
        end_control();
        if (iFunction.async && !iFunction.generator && !iFunction.script && !iBody)
            runtime("rejected_promise", {}, I64);  // (an async function whose parameters threw returns a rejected promise)
        else
            constant(values::Exception);
        fragment code;
        code.fragmentCode.as_std_vector() = iCode;
        for (auto const& r : iRelocations)
            code.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ r.first, r.second });
        auto const index = iBody ? iFunction.bodyIndex : iFunction.index;
        if (auto const path = wasm_dump_path())
            if (auto* file = std::fopen(path, "ab"))
            {
                std::fprintf(file, "function %u %s\nlocals", index, iFunction.script ? "<script>" : iFunction.name.c_str());
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
        iCompiler.generator().set_function_body(index, code);
        // (its locals)
        iCompiler.generator().set_function_locals(index, neolib::vector<type>{ iLocals.begin(), iLocals.end() });
    }

    void function_generator::callee_value()
    {
        if (iFunction.arrow && iBody)
        {
            get(CalleeLocal);
            return;
        }
        get(CalleeLocal);
    }

    void function_generator::prologue()
    {
        // the function's closure (its environment)
        bool needsEnvironment = iFunction.needsOuterEnvironment;
        for (auto* f : iFunction.functions)
            needsEnvironment = needsEnvironment || f->needsOuterEnvironment;
        // (a named generator or async function expression: its body finds its name (captured) in the closure's environment)
        if ((iFunction.generator || iFunction.async) && iFunction.calleeBinding != nullptr && iFunction.calleeBinding->captured)
            needsEnvironment = true;
        if (iBody)
        {
            // (a generator's or async function's body: its environment is its top scope's (the function made it))
            iFunctionEnvironment = local();
            get(FirstArgumentLocal);
            set(*iFunctionEnvironment);
            iFunction.top->environmentLocal = *iFunctionEnvironment;
            iScopes.push_back(iFunction.top);
            if (iFunction.top->hasEnvironment)
                iFunctionEnvironment = std::nullopt;    // (else the environment is the function's closure's)
            if (iFunction.body != iFunction.top)
            {
                enter_scope(iFunction.body);
                body_vars_from_parameters();
            }
            return;
        }
        if (!iFunction.script && needsEnvironment)
        {
            iFunctionEnvironment = local();
            get(CalleeLocal);
            runtime("function_env", { I64 }, I64);
            set(*iFunctionEnvironment);
        }
        if (iFunction.script && iCompiler.iModule)
        {
            // (a module: its environment (the runtime made it; its bindings are initialized when it is instantiated))
            iFunctionEnvironment = local();
            get(FirstArgumentLocal);
            set(*iFunctionEnvironment);
            iFunction.top->environmentLocal = *iFunctionEnvironment;
            iScopes.push_back(iFunction.top);
            return;
        }
        if (iFunction.script && iCompiler.eval_code())
        {
            // (eval code: its environment is the code that called eval's (or the global environment (undefined)); a sloppy eval's var and
            // function declarations are the var bindings of that code (EvalDeclarationInstantiation: they are checked, then declared); its
            // lexical declarations (and a strict eval's var declarations) are its own)
            iFunctionEnvironment = local();
            get(FirstArgumentLocal);
            set(*iFunctionEnvironment);
            if (!iFunction.strict)
                for (int pass = 0; pass < 2; ++pass)
                    for (auto& b : iFunction.top->bindings)
                        if (b->kind == binding_kind::Var || b->kind == binding_kind::Function)
                        {
                            environment_value();
                            key(b->name);
                            i32((b->kind == binding_kind::Function ? 1 : 0) | (pass == 0 ? 2 : 0) |
                                ((iCompiler.iEvalFlags & compiler::EvalParameters) != 0u ? 4 : 0));
                            runtime("eval_declare", { I64, I64, I32 }, I64);
                            check_void();
                        }
            enter_scope(iFunction.top);
            if (iFunction.thisBinding != nullptr)
            {
                get(ThisLocal);
                initialize_binding(iFunction.thisBinding);
            }
            if (iFunction.homeBinding != nullptr)
            {
                runtime("eval_home", {}, I64);
                initialize_binding(iFunction.homeBinding);
            }
            return;
        }
        if (iFunction.script)
        {
            iScopes.push_back(iFunction.top);
            for (auto& b : iFunction.top->bindings)
                if (!b->captured && b->lexical())
                {
                    auto const rep = rep_of(b.get());
                    b->slot = local(rep == numeric_rep::F64 ? F64 : rep == numeric_rep::I32 ? I32 : I64);
                    if (rep != numeric_rep::None)
                        continue;
                    constant(values::Empty);
                    set(b->slot);
                }
            // (its declarations: var and function declarations are properties of the global object; lexical declarations bindings of the
            // global lexical environment)
            for (int pass = 0; pass < 2; ++pass)
                for (auto& b : iFunction.top->bindings)
                {
                    if (b->lexical() && !b->captured && !iCompiler.iModule)
                        continue;
                    key(b->name);
                    i32((b->kind == binding_kind::Var ? 0 : b->kind == binding_kind::Function ? 1 : (b->kind == binding_kind::Let || b->kind == binding_kind::Class) ? 2 : 3) |
                        (pass == 0 ? 8 : 0));
                    runtime("global_declare", { I64, I32 }, I64);
                    check_void();
                }
            return;
        }
        // (this: in sloppy mode undefined and null are the global object and a primitive is its object)
        if (!iFunction.arrow && !iFunction.strict && iFunction.usesThis)
        {
            iThisLocal = local();
            get(ThisLocal);
            runtime("sloppy_this", { I64 }, I64);
            set(iThisLocal);
        }
        bool extraArguments = iFunction.argumentsBinding != nullptr;
        for (auto* c : iFunction.declaration->children)
            if (c->is(construct::Parameters))
                extraArguments = extraArguments || c->size() > RegisterArguments || (c->size() > 0u && c->children.back()->is(construct::Rest));
        if (extraArguments)
        {
            // (more than four arguments: the extra arguments)
            iArgumentsBase = local(I32);
            get(ArgcLocal);
            runtime("args_base", { I32 }, I32);
            set(iArgumentsBase);
        }
        enter_scope(iFunction.top);
        // (internal bindings that arrow functions use)
        if (iFunction.thisBinding != nullptr)
        {
            get(iThisLocal);
            initialize_binding(iFunction.thisBinding);
        }
        if (iFunction.newTargetBinding != nullptr)
        {
            get(NewTargetLocal);
            initialize_binding(iFunction.newTargetBinding);
        }
        if (iFunction.homeBinding != nullptr)
        {
            get(CalleeLocal);
            runtime("function_home", { I64 }, I64);
            initialize_binding(iFunction.homeBinding);
        }
        if (auto* callee = iFunction.top->find("%callee"))
        {
            get(CalleeLocal);
            initialize_binding(callee);
        }
        // (a named function expression's own name)
        if (iFunction.calleeBinding != nullptr && iFunction.calleeBinding->kind == binding_kind::Callee && iFunction.calleeBinding->captured)
        {
            // (its scope is between the function's and its parent's: an environment of its own)
            auto* s = iFunction.calleeBinding->owner;
            s->environmentLocal = std::nullopt;
        }
        instantiate_parameters();
        // (a generator's or async function's body scope is its body function's)
        if (iFunction.body != iFunction.top && !iFunction.generator && !iFunction.async)
        {
            enter_scope(iFunction.body);
            body_vars_from_parameters();
        }
    }

    void function_generator::body_vars_from_parameters()
    {
        // (parameters with expressions: a var of the body named as a parameter (or arguments) starts with its value)
        for (auto& b : iFunction.body->bindings)
            if (b->kind == binding_kind::Var)
                if (auto* p = iFunction.top->find(b->name); p != nullptr && p != b.get() &&
                    (p->kind == binding_kind::Parameter || p == iFunction.argumentsBinding))
                {
                    load_binding(p);
                    initialize_binding(b.get());
                }
    }

    void function_generator::instantiate_parameters()
    {
        auto* declaration = iFunction.declaration;
        node* parameters = nullptr;
        for (auto* c : declaration->children)
            if (c->is(construct::Parameters))
            {
                parameters = c;
                break;
            }
        // (the arguments object)
        if (iFunction.argumentsBinding != nullptr)
        {
            callee_value();
            get(ArgcLocal);
            for (std::uint32_t i = 0u; i < RegisterArguments; ++i)
                get(FirstArgumentLocal + i);
            get(iArgumentsBase);
            i32(!iFunction.strict && iFunction.simpleParameters ? 1 : 0);
            runtime("arguments_object", { I64, I32, I64, I64, I64, I64, I32, I32 }, I64);
            check();
            if (!iFunction.strict && iFunction.simpleParameters && parameters != nullptr && iFunction.top->environmentLocal)
            {
                // (a mapped arguments object: its elements are the parameters' bindings (the last of parameters with the same name))
                auto const object = temporary();
                set(object);
                std::set<std::string> mapped;
                for (std::size_t i = parameters->size(); i-- > 0u;)
                {
                    auto* p = parameters->child(i);
                    if (!p->is(construct::Binding))
                        continue;
                    auto const name = name_of(p);
                    if (!mapped.insert(name).second)
                        continue;
                    auto* b = iFunction.top->find(name);
                    if (b == nullptr || !b->captured)
                        continue;
                    get(object);
                    get(*iFunction.top->environmentLocal);
                    i32(static_cast<std::int32_t>(i));
                    i32(static_cast<std::int32_t>(b->slot));
                    runtime("arguments_map", { I64, I64, I32, I32 }, I64);
                    drop();
                }
                get(object);
                release(object);
            }
            initialize_binding(iFunction.argumentsBinding);
        }
        if (parameters == nullptr)
            return;
        std::uint32_t index = 0u;
        for (auto* p : parameters->children)
        {
            auto const argument = [&](std::uint32_t aIndex)
            {
                if (aIndex < RegisterArguments)
                    get(FirstArgumentLocal + aIndex);
                else
                {
                    get(ArgcLocal);
                    get(iArgumentsBase);
                    i32(static_cast<std::int32_t>(aIndex));
                    runtime("argument", { I32, I32, I32 }, I64);
                }
            };
            if (p->is(construct::Rest))
            {
                get(ArgcLocal);
                i32(static_cast<std::int32_t>(index));
                for (std::uint32_t i = 0u; i < RegisterArguments; ++i)
                    get(FirstArgumentLocal + i);
                get(iArgumentsBase);
                runtime("rest", { I32, I32, I64, I64, I64, I64, I32 }, I64);
                check();
                bind_pattern(p->child(0u), true, binding_kind::Parameter);
            }
            else
            {
                argument(index);
                bind_pattern(p, true, binding_kind::Parameter);
            }
            ++index;
        }
    }

    void function_generator::hoist_functions(node* aStatements, scope* aScope)
    {
        // (the function declarations of a block or body: their functions are created when it is entered)
        if (aStatements == nullptr)
            return;
        for (auto* s : aStatements->children)
        {
            node* f = s;
            if (f->is(construct::Labelled))
                while (f->is(construct::Labelled) && f->size() == 2u)
                    f = f->child(1u);
            if (f->is(construct::Export) && f->size() > 0u)
                f = f->child(0u);
            if (!f->is(construct::Function) && !f->is(construct::AsyncFunction))
                continue;
            closure(*f->function);
            auto* b = aScope->find(f->function->name);
            if (b == nullptr)
                continue;
            if (aScope->kind == scope_kind::Script && iCompiler.eval_code() && !iFunction.strict)
            {
                // (a sloppy eval's function declaration: a var binding of the code that called eval)
                auto const value = temporary();
                set(value);
                environment_value();
                key(f->function->name);
                get(value);
                runtime("eval_function", { I64, I64, I64 }, I64);
                check_void();
                release(value);
            }
            else if (aScope->kind == scope_kind::Script && !iCompiler.eval_code() && !iCompiler.iModule)
            {
                auto const value = temporary();
                set(value);
                key(f->function->name);
                get(value);
                runtime("global_define_function", { I64, I64 }, I64);
                check_void();
                release(value);
            }
            else
                initialize_binding(b);
        }
    }

    void function_generator::closure(function_info& aFunction)
    {
        // (a function object for one of the program's functions, closed over the innermost environment)
        std::int32_t flags = 0;
        if (aFunction.strict)
            flags |= ClosureStrict;
        if (aFunction.arrow)
            flags |= ClosureArrow;
        if (aFunction.method)
            flags |= ClosureMethod;
        if (aFunction.generator)
            flags |= ClosureGenerator;
        if (aFunction.async)
            flags |= ClosureAsync;
        if (aFunction.classConstructor)
            flags |= ClosureClassConstructor;
        if (aFunction.derived)
            flags |= ClosureDerived;
        if (aFunction.getter)
            flags |= ClosureGetter;
        if (aFunction.setter)
            flags |= ClosureSetter;
        if (!aFunction.arrow && !aFunction.method && !aFunction.generator && !aFunction.async)
            flags |= ClosureConstructor;
        if (aFunction.classConstructor)
            flags |= ClosureConstructor;
        // (its number for the dispatcher: its position in the program's functions)
        std::uint32_t number = 0u;
        for (auto& f : iCompiler.iFunctions)
        {
            if (&f == &aFunction)
                break;
            ++number;
            if ((f.generator || f.async) && !f.script)
                ++number;
        }
        i32(static_cast<std::int32_t>(number));
        function_index(aFunction);
        environment_value();
        i32(flags);
        key(aFunction.name);
        i32(static_cast<std::int32_t>(aFunction.length));
        if (aFunction.declaration != nullptr)
            string_value(utf8_encoding(aFunction.declaration->source));
        else
            undefined();
        runtime("closure", { I32, I32, I64, I32, I64, I32, I64 }, I64);
    }

    // --- statements

    void function_generator::statements(node* aNode, std::size_t aFirst)
    {
        if (aNode == nullptr)
            return;
        for (std::size_t i = aFirst; i < aNode->size(); ++i)
            statement(aNode->child(i));
    }

    void function_generator::statement(node* aNode)
    {
        // (eval code's completion value: an expression statement's value; statements whose value is undefined if their own statements
        // have none (if, loops, switch, try, with) reset it)
        if (iCompletion)
            switch (aNode->kind)
            {
            case construct::If:
            case construct::Do:
            case construct::While:
            case construct::For:
            case construct::ForIn:
            case construct::ForOf:
            case construct::ForAwaitOf:
            case construct::Try:
            case construct::Switch:
            case construct::With:
                undefined();
                set(*iCompletion);
                break;
            default:
                break;
            }
        switch (aNode->kind)
        {
        case construct::Empty:
        case construct::Debugger:
            break;
        case construct::ExpressionStatement:
            iDiscarded = !iCompletion;
            expression(aNode->child(0u));
            iDiscarded = false;
            if (iCompletion)
                set(*iCompletion);
            else
                drop();
            break;
        case construct::Var:
        case construct::Let:
        case construct::Const:
        case construct::Using:
        case construct::AwaitUsing:
            variable_declaration(aNode);
            break;
        case construct::Function:
        case construct::AsyncFunction:
            {
                // (hoisted; a function declaration in a block in sloppy code also assigns the function's var binding (Annex B))
                if (aNode->annexB && !iScopes.empty() && iScopes.back()->kind != scope_kind::Function && iScopes.back()->kind != scope_kind::Script &&
                    iScopes.back() != iFunction.body)
                {
                    auto* f = aNode->function;
                    auto* local = iScopes.back()->find(f->name);
                    if (local != nullptr)
                    {
                        auto* outer = iFunction.body != nullptr ? iFunction.body : iFunction.top;
                        if (iFunction.script && iCompiler.eval_code() && iFunctionEnvironment)
                        {
                            // (eval code: the var binding (EvalDeclarationInstantiation declared it) of the code that called eval (or the
                            // global object's property), found from the eval's environment (not the block's, where the name is the function's))
                            load_binding(local);
                            auto const value = temporary();
                            set(value);
                            get(*iFunctionEnvironment);
                            key(f->name);
                            get(value);
                            i32(0);
                            runtime("dyn_set", { I64, I64, I64, I32 }, I64);
                            check_void();
                            release(value);
                        }
                        else if (iFunction.script)
                        {
                            load_binding(local);
                            auto const value = temporary();
                            set(value);
                            key(f->name);
                            get(value);
                            i32(0);
                            runtime("global_set", { I64, I64, I32 }, I64);
                            check_void();
                            release(value);
                        }
                        else if (auto* var = outer->find(f->name); var != nullptr && var != local && !var->lexical())
                        {
                            load_binding(local);
                            initialize_binding(var);
                        }
                    }
                }
            }
            break;
        case construct::Class:
            {
                class_expression(aNode, name_of(aNode->child(0u)));
                auto const r = resolve(name_of(aNode->child(0u)), aNode);
                store(r, true);
                drop();
            }
            break;
        case construct::Block:
            {
                auto* s = aNode->ownScope;
                enter_scope(s);
                hoist_functions(aNode, s);
                if (has_using(aNode))
                    disposal_scope([&]() { statements(aNode); });
                else
                    statements(aNode);
                leave_scope(s);
            }
            break;
        case construct::If:
            if_statement(aNode);
            break;
        case construct::Do:
        case construct::While:
            loop_statement(aNode, {});
            break;
        case construct::For:
            for_statement(aNode, {});
            break;
        case construct::ForIn:
        case construct::ForOf:
        case construct::ForAwaitOf:
            for_in_of_statement(aNode, {});
            break;
        case construct::Continue:
            jump(aNode, true);
            break;
        case construct::Break:
            jump(aNode, false);
            break;
        case construct::Return:
            return_statement(aNode);
            break;
        case construct::Throw:
            expression(aNode->child(0u));
            runtime("throw", { I64 }, I64);
            check_void();
            break;
        case construct::Try:
            try_statement(aNode);
            break;
        case construct::Switch:
            switch_statement(aNode, {});
            break;
        case construct::Labelled:
            {
                // (labels: of the statement that follows them)
                std::vector<std::string> labels;
                node* s = aNode;
                while (s->is(construct::Labelled))
                {
                    labels.push_back(name_of(s->child(0u)));
                    s = s->child(1u);
                }
                switch (s->kind)
                {
                case construct::Do:
                case construct::While:
                    loop_statement(s, labels);
                    break;
                case construct::For:
                    for_statement(s, labels);
                    break;
                case construct::ForIn:
                case construct::ForOf:
                case construct::ForAwaitOf:
                    for_in_of_statement(s, labels);
                    break;
                case construct::Switch:
                    switch_statement(s, labels);
                    break;
                default:
                    begin(control_kind::Breakable, {}, labels);
                    statement(s);
                    end_control();
                    break;
                }
            }
            break;
        case construct::With:
            with_statement(aNode);
            break;
        case construct::Import:
            if (!iCompiler.iModule || !iFunction.script)
                iCompiler.error(aNode->source, "Cannot use import statement outside a module");
            break;
        case construct::Export:
            if (!iCompiler.iModule || !iFunction.script)
                iCompiler.error(aNode->source, "Unexpected token 'export'");
            export_statement(aNode);
            break;
        default:
            iCompiler.error(aNode->source, std::string{ "unsupported statement (" } + std::string{ language::javascript::info(aNode->kind).name } + ")");
        }
    }

    void function_generator::variable_declaration(node* aNode)
    {
        bool const lexical = !aNode->is(construct::Var);
        for (auto* d : aNode->children)
        {
            auto* target = d->child(0u);
            if (d->size() < 2u)
            {
                // (let x; is x = undefined; var x; does nothing; const x; is an error)
                if (aNode->is(construct::Const))
                    iCompiler.error(d->source, "Missing initializer in const declaration");
                if (!lexical)
                    continue;
                undefined();
                bind_pattern(target, true, binding_kind::Let);
                continue;
            }
            if (target->is(construct::Binding) && !aNode->is(construct::Using) && !aNode->is(construct::AwaitUsing))
            {
                // (a numeric local: its initial value as its rep)
                auto const r = resolve(name_of(target), target);
                if (auto const rep = r.kind == reference::Local ? rep_of(r.target) : numeric_rep::None; rep != numeric_rep::None)
                {
                    typed(d->child(1u), rep);
                    set(r.local);
                    continue;
                }
                if (!lexical && r.kind == reference::Dynamic && !r.name.starts_with("#"))
                {
                    // (a var found by name (e.g. in a with statement): it is resolved before its initializer is evaluated)
                    auto const base = dynamic_reference(r);
                    named_evaluation(d->child(1u), name_of(target));
                    auto const value = temporary();
                    set(value);
                    dynamic_reference_set(base, r, value);
                    release(value);
                    release(base);
                    continue;
                }
            }
            if (target->is(construct::Binding))
                named_evaluation(d->child(1u), name_of(target));
            else
                expression(d->child(1u));
            if ((aNode->is(construct::Using) || aNode->is(construct::AwaitUsing)) && !iDisposals.empty())
            {
                // (a resource of the block: disposed when it completes)
                auto const v = temporary();
                tee(v);
                drop();
                get(iDisposals.back());
                get(v);
                i32(aNode->is(construct::AwaitUsing) ? 1 : 0);
                runtime("dispose_add", { I64, I64, I32 }, I64);
                check_void();
                get(v);
                release(v);
            }
            bind_pattern(target, lexical, lexical ? binding_kind::Let : binding_kind::Var);
        }
    }

    bool function_generator::has_using(node* aStatements) const
    {
        for (auto* c : aStatements->children)
            if (c->is(construct::Using) || c->is(construct::AwaitUsing))
                return true;
        return false;
    }

    void function_generator::disposal_scope(std::function<void()> const& aBody)
    {
        // (a block with using declarations: its resources are disposed when it completes (normally, by an exception or by a jump), as a
        // finally block would run)
        auto const capability = local();
        runtime("dispose_new", {}, I64);
        set(capability);
        iDisposals.push_back(capability);
        begin(control_kind::Finally);
        auto const fin = iControls.size() - 1u;
        iControls.back().completionLocal = local(I32);
        iControls.back().valueLocal = local();
        i32(CompletionNormal);
        set(iControls.back().completionLocal);
        begin(control_kind::Handler);
        aBody();
        i32(CompletionNormal);
        set(iControls[fin].completionLocal);
        branch(fin);
        end_control();
        runtime("catch", {}, I64);
        set(iControls[fin].valueLocal);
        i32(CompletionThrow);
        set(iControls[fin].completionLocal);
        auto jumps = std::move(iControls.back().jumps);
        auto const completion = iControls.back().completionLocal;
        auto const value = iControls.back().valueLocal;
        end_control();
        iDisposals.pop_back();
        // (DisposeResources: a throw completion's exception is the error that the disposal's errors suppress)
        get(capability);
        get(completion);
        i32(CompletionThrow);
        op(opcode::I32Eq);
        op(opcode::If);
        iCode.push_back(0x7Eu);
        iControls.push_back(control{ control_kind::Block });
        get(value);
        op(opcode::Else);
        undefined();
        end_control();
        get(completion);
        i32(CompletionThrow);
        op(opcode::I32Eq);
        runtime("dispose_all", { I64, I64, I32 }, I64);
        check_void();
        get(completion);
        i32(CompletionReturn);
        op(opcode::I32Eq);
        op(opcode::If);
        iCode.push_back(0x40u);
        iControls.push_back(control{ control_kind::Block });
        get(value);
        complete_return();
        end_control();
        for (std::size_t j = 0u; j < jumps.size(); ++j)
        {
            get(completion);
            i32(static_cast<std::int32_t>(CompletionJumps + j));
            op(opcode::I32Eq);
            op(opcode::If);
            iCode.push_back(0x40u);
            iControls.push_back(control{ control_kind::Block });
            jumps[j]();
            end_control();
        }
    }

    void function_generator::condition(node* aNode)
    {
        auto* n = aNode;
        while (n->is(construct::Parenthesized) && n->size() == 1u)
            n = n->child(0u);
        if (n->is(construct::Not))
        {
            condition(n->child(0u));
            op(opcode::I32Eqz);
            return;
        }
        if (n->is(construct::And) || n->is(construct::Or))
        {
            // (ToBoolean(a && b) is ToBoolean(a) && ToBoolean(b); ToBoolean(a || b) is ToBoolean(a) || ToBoolean(b))
            condition(n->child(0u));
            op(opcode::If);
            iCode.push_back(0x7Fu);
            iControls.push_back(control{ control_kind::Block });
            if (n->is(construct::And))
                condition(n->child(1u));
            else
                i32(1);
            op(opcode::Else);
            if (n->is(construct::And))
                i32(0);
            else
                condition(n->child(1u));
            end_control();
            return;
        }
        if (numeric_condition(aNode))
            return;
        if (auto const t = num_type(n); t != numeric_rep::None)
        {
            // (a number: true unless it is 0, -0 or NaN)
            typed(n, t);
            if (t == numeric_rep::I32)
            {
                i32(0);
                op(opcode::I32Ne);
            }
            else if (t == numeric_rep::I64)
            {
                i64(0);
                op(opcode::I64Ne);
            }
            else
            {
                op(opcode::F64Abs);
                f64(0.0);
                op(opcode::F64Gt);
            }
            return;
        }
        expression(aNode);
        // (booleans: inline)
        tee(iScratch);
        constant(values::True);
        op(opcode::I64Eq);
        get(iScratch);
        constant(values::False);
        op(opcode::I64Eq);
        op(opcode::I32Ior);
        op(opcode::If);
        iCode.push_back(0x7Fu);
        get(iScratch);
        constant(values::True);
        op(opcode::I64Eq);
        op(opcode::Else);
        get(iScratch);
        runtime("to_boolean", { I64 }, I32);
        op(opcode::End);
    }

    void function_generator::if_statement(node* aNode)
    {
        condition(aNode->child(0u));
        op(opcode::If);
        iCode.push_back(0x40u);
        iControls.push_back(control{ control_kind::Block });
        statement(aNode->child(1u));
        if (aNode->size() > 2u)
        {
            op(opcode::Else);
            statement(aNode->child(2u));
        }
        end_control();
    }

    void function_generator::loop_statement(node* aNode, std::vector<std::string> const& aLabels)
    {
        bool const doWhile = aNode->is(construct::Do);
        auto* test = doWhile ? aNode->child(1u) : aNode->child(0u);
        auto* body = doWhile ? aNode->child(0u) : aNode->child(1u);
        begin(control_kind::Breakable, {}, aLabels);
        iControls.back().loop = true;
        auto const exit = iControls.size() - 1u;
        begin(control_kind::Loop);
        if (!doWhile)
        {
            condition(test);
            op(opcode::I32Eqz);
            branch(exit, true);
        }
        begin(control_kind::Continuable, {}, aLabels);
        iControls.back().loop = true;
        statement(body);
        end_control();
        if (doWhile)
        {
            condition(test);
            op(opcode::BrIf);
            u32(0u);
        }
        else
        {
            op(opcode::Br);
            u32(0u);
        }
        end_control();
        end_control();
    }

    void function_generator::for_statement(node* aNode, std::vector<std::string> const& aLabels)
    {
        node* init = nullptr;
        node* test = nullptr;
        node* update = nullptr;
        node* body = aNode->children.back();
        for (std::size_t i = 0u; i + 1u < aNode->size(); ++i)
        {
            auto* c = aNode->child(i);
            if (c->is(construct::ForTest))
                test = c;
            else if (c->is(construct::ForUpdate))
                update = c;
            else
                init = c;
        }
        auto* s = aNode->ownScope;
        enter_scope(s);
        bool const perIteration = s->hasEnvironment && init != nullptr && init->is(construct::Let);
        auto* const initDeclaration = init != nullptr && init->is(construct::ForInit) ? init->child(0u) : init;
        if (initDeclaration != nullptr && (initDeclaration->is(construct::Using) || initDeclaration->is(construct::AwaitUsing)))
        {
            // (for (using x = ...; ...; ...): its resources are disposed when the loop completes)
            disposal_scope([&]() { for_loop(init, test, update, body, s, perIteration, aLabels); });
            leave_scope(s);
            return;
        }
        for_loop(init, test, update, body, s, perIteration, aLabels);
        leave_scope(s);
    }

    void function_generator::for_loop(node* init, node* test, node* update, node* body, scope* s, bool perIteration, std::vector<std::string> const& aLabels)
    {
        if (init != nullptr)
        {
            auto* i = init->is(construct::ForInit) ? init->child(0u) : init;
            if (i->is(construct::Var) || i->is(construct::Let) || i->is(construct::Const) || i->is(construct::Using) || i->is(construct::AwaitUsing))
                variable_declaration(i);
            else
            {
                expression(i);
                drop();
            }
        }
        auto const copy_environment = [&]()
        {
            if (perIteration)
            {
                get(*s->environmentLocal);
                runtime("env_copy", { I64 }, I64);
                set(*s->environmentLocal);
            }
        };
        copy_environment();
        std::vector<std::pair<binding const*, int>> shadows;
        if (test != nullptr && !iInvariantNumerics.empty())
            bound_shadows(test->child(0u), shadows);
        begin(control_kind::Breakable, {}, aLabels);
        iControls.back().loop = true;
        auto const exit = iControls.size() - 1u;
        begin(control_kind::Loop);
        if (test != nullptr)
        {
            condition(test->child(0u));
            op(opcode::I32Eqz);
            branch(exit, true);
        }
        begin(control_kind::Continuable, {}, aLabels);
        iControls.back().loop = true;
        statement(body);
        end_control();
        copy_environment();
        if (update != nullptr)
        {
            iDiscarded = true;
            expression(update->child(0u));
            drop();
        }
        op(opcode::Br);
        u32(0u);
        end_control();
        end_control();
        for (auto const& key : shadows)
            iBoundShadows.erase(key);
    }

    void function_generator::for_in_of_statement(node* aNode, std::vector<std::string> const& aLabels)
    {
        // for (binding in/of expression) body: the binding's scope is entered (fresh) for each iteration
        bool const forIn = aNode->is(construct::ForIn);
        bool const async = aNode->is(construct::ForAwaitOf);
        auto* head = aNode->child(0u);
        auto* object = aNode->child(1u);
        auto* body = aNode->child(2u);
        auto* s = aNode->ownScope;
        bool const lexicalHead = head->is(construct::ForLet) || head->is(construct::ForConst) || head->is(construct::ForUsing) || head->is(construct::ForAwaitUsing);
        // (the expression is evaluated with the head's bindings uninitialized (TDZ))
        enter_scope(s);
        if (head->is(construct::ForVar) && head->child(0u)->is(construct::Initializer))
        {
            // (Annex B: for (var x = init in ...))
            expression(head->child(0u)->child(1u));
            bind_pattern(head->child(0u)->child(0u), false, binding_kind::Var);
        }
        expression(object);
        auto const iterator = local();
        if (forIn)
            runtime("for_in", { I64 }, I64);
        else
        {
            i32(async ? 1 : 0);
            runtime("iterator", { I64, I32 }, I64);
        }
        check();
        set(iterator);
        begin(control_kind::Breakable, {}, aLabels);
        iControls.back().loop = true;
        auto const exit = iControls.size() - 1u;
        // (for-of: abrupt completions of the body close the iterator)
        std::optional<std::size_t> cleanup;
        if (!forIn)
        {
            begin(control_kind::Finally);
            cleanup = iControls.size() - 1u;
            iControls.back().completionLocal = local(I32);
            iControls.back().valueLocal = local();
            iControls.back().iteratorLocal = iterator;
            iControls.back().asyncIterator = async;
            i32(CompletionNormal);
            set(iControls.back().completionLocal);
        }
        begin(control_kind::Loop);
        get(iterator);
        if (forIn)
            runtime("for_in_next", { I64 }, I64);
        else
            runtime("iterator_step", { I64 }, I64);
        check();
        tee(iScratch);
        constant(values::Empty);
        op(opcode::I64Eq);
        branch(cleanup ? *cleanup : exit, true);
        auto const value = temporary();
        get(iScratch);
        set(value);
        // (the body: exceptions in it (and in the binding) close the iterator)
        std::optional<std::size_t> handlerControl;
        if (!forIn)
        {
            begin(control_kind::Handler);
            handlerControl = iControls.size() - 1u;
        }
        if (lexicalHead && s->hasEnvironment)
        {
            environment_value();
            runtime("env_copy", { I64 }, I64);
            set(*s->environmentLocal);
            for (auto& b : s->bindings)
                if (b->captured && (b->tdz || b->lexical()))
                {
                    constant(values::Empty);
                    initialize_binding(b.get());
                }
        }
        auto const iteration = [&]()
        {
            get(value);
            if (head->is(construct::ForUsing) || head->is(construct::ForAwaitUsing))
            {
                // (for (using x of ...): each value is a resource of its iteration)
                get(iDisposals.back());
                get(value);
                i32(head->is(construct::ForAwaitUsing) ? 1 : 0);
                runtime("dispose_add", { I64, I64, I32 }, I64);
                check_void();
            }
            if (head->is(construct::ForVar))
                bind_pattern(head->child(0u)->is(construct::Initializer) ? head->child(0u)->child(0u) : head->child(0u), false, binding_kind::Var);
            else if (lexicalHead)
                bind_pattern(head->child(0u), true, binding_kind::Let);
            else
                assign_target(head);
            begin(control_kind::Continuable, {}, aLabels);
            iControls.back().loop = true;
            statement(body);
            end_control();
        };
        if (head->is(construct::ForUsing) || head->is(construct::ForAwaitUsing))
            disposal_scope(iteration);
        else
            iteration();
        release(value);
        op(opcode::Br);
        u32(static_cast<std::uint32_t>(handlerControl ? 1u : 0u));
        if (handlerControl)
        {
            end_control();
            // (an exception: the iterator is closed (its errors are ignored) and the exception propagates)
            get(iterator);
            runtime("iterator_close_throw", { I64 }, I64);
            drop();
            constant(values::Exception);
            check_void();
        }
        end_control();  // (loop)
        if (cleanup)
        {
            auto jumps = std::move(iControls.back().jumps);
            auto const completion = iControls.back().completionLocal;
            auto const valueLocal = iControls.back().valueLocal;
            end_control();
            // (the loop ended (normally: the iterator is done) or a jump left it: the iterator is closed if it left early)
            get(completion);
            i32(CompletionNormal);
            op(opcode::I32Ne);
            op(opcode::If);
            iCode.push_back(0x40u);
            iControls.push_back(control{ control_kind::Block });
            get(iterator);
            runtime("iterator_close", { I64 }, I64);
            check_void();
            end_control();
            for (std::size_t j = 0u; j < jumps.size(); ++j)
            {
                get(completion);
                i32(static_cast<std::int32_t>(CompletionJumps + j));
                op(opcode::I32Eq);
                op(opcode::If);
                iCode.push_back(0x40u);
                iControls.push_back(control{ control_kind::Block });
                jumps[j]();
                end_control();
            }
            get(completion);
            i32(CompletionReturn);
            op(opcode::I32Eq);
            op(opcode::If);
            iCode.push_back(0x40u);
            iControls.push_back(control{ control_kind::Block });
            get(valueLocal);
            complete_return();
            end_control();
        }
        end_control();  // (breakable)
        leave_scope(s);
    }

    void function_generator::switch_statement(node* aNode, std::vector<std::string> const& aLabels)
    {
        expression(aNode->child(0u));
        auto const discriminant = local();
        set(discriminant);
        auto* s = aNode->ownScope;
        enter_scope(s);
        begin(control_kind::Breakable, {}, aLabels);
        iControls.back().switch_ = true;
        auto const exit = iControls.size() - 1u;
        std::vector<node*> clauses{ aNode->children.begin() + 1, aNode->children.end() };
        auto const n = clauses.size();
        for (std::size_t i = 0u; i < n; ++i)
            begin(control_kind::Block);
        for (auto* clause : clauses)
            hoist_functions(clause, s);
        // (the dispatch: case tests in order, then the default)
        std::optional<std::size_t> defaultClause;
        for (std::size_t i = 0u; i < n; ++i)
        {
            if (clauses[i]->is(construct::Default))
            {
                defaultClause = i;
                continue;
            }
            get(discriminant);
            expression(clauses[i]->child(0u));
            runtime("strict_equal", { I64, I64 }, I32);
            op(opcode::BrIf);
            u32(static_cast<std::uint32_t>(i));
        }
        if (defaultClause)
        {
            op(opcode::Br);
            u32(static_cast<std::uint32_t>(*defaultClause));
        }
        else
            branch(exit);
        for (std::size_t i = 0u; i < n; ++i)
        {
            end_control();
            statements(clauses[i], clauses[i]->is(construct::Case) ? 1u : 0u);
        }
        end_control();
        leave_scope(s);
    }

    void function_generator::jump(node* aNode, bool aContinue)
    {
        std::optional<std::string> label;
        if (aNode->size() > 0u)
            label = name_of(aNode->child(0u));
        // (the target: the innermost loop (or switch, for break) or the statement with the label)
        std::optional<std::size_t> target;
        for (std::size_t i = iControls.size(); i-- > 0u;)
        {
            auto const& c = iControls[i];
            if (aContinue && c.kind != control_kind::Continuable)
                continue;
            if (!aContinue && c.kind != control_kind::Breakable)
                continue;
            if (label)
            {
                if (std::find(c.labels.begin(), c.labels.end(), *label) != c.labels.end())
                {
                    target = i;
                    break;
                }
            }
            else if (c.loop || c.switch_)
            {
                target = i;
                break;
            }
        }
        if (!target)
            iCompiler.error(aNode->source, label ? "Undefined label '" + *label + "'" : aContinue ? "Illegal continue statement" : "Illegal break statement");
        // (through finally blocks (and iterators to close) between here and the target)
        std::function<void()> go;
        auto const targetControl = *target;
        go = [this, targetControl, &go]()
        {
            for (std::size_t i = iControls.size(); i-- > targetControl + 1u;)
                if (iControls[i].kind == control_kind::Finally)
                {
                    auto& f = iControls[i];
                    auto const id = static_cast<std::int32_t>(CompletionJumps + f.jumps.size());
                    // (after the finally block, the jump continues from there (with the controls as they are there))
                    std::size_t const target2 = targetControl;
                    f.jumps.push_back([this, target2]()
                    {
                        std::function<void()> again;
                        again = [this, target2]()
                        {
                            for (std::size_t k = iControls.size(); k-- > target2 + 1u;)
                                if (iControls[k].kind == control_kind::Finally)
                                {
                                    auto& g = iControls[k];
                                    auto const id2 = static_cast<std::int32_t>(CompletionJumps + g.jumps.size());
                                    g.jumps.push_back([this, target2]() { for (std::size_t m = iControls.size(); m-- > target2 + 1u;) if (iControls[m].kind == control_kind::Finally) { i32(0); drop(); break; } branch(target2); });
                                    i32(id2);
                                    set(g.completionLocal);
                                    branch(k);
                                    return;
                                }
                            branch(target2);
                        };
                        again();
                    });
                    i32(id);
                    set(f.completionLocal);
                    branch(i);
                    return;
                }
            branch(targetControl);
        };
        go();
    }

    void function_generator::return_statement(node* aNode)
    {
        if (iFunction.script)
            iCompiler.error(aNode->source, "Illegal return statement");
        if (aNode->size() > 0u)
        {
            tail_positions(aNode->child(0u));
            expression(aNode->child(0u));
            if (iFunction.async && iFunction.generator)
                await_value();  // (an async generator's return value is awaited (return; is not))
        }
        else
            undefined();
        complete_return();
    }

    void function_generator::complete_return()
    {
        // (the value on the stack is returned: through finally blocks; a derived class's constructor returns its this)
        for (std::size_t i = iControls.size(); i-- > 0u;)
            if (iControls[i].kind == control_kind::Finally)
            {
                auto& f = iControls[i];
                set(f.valueLocal);
                i32(CompletionReturn);
                set(f.completionLocal);
                branch(i);
                return;
            }
        if (iFunction.derived && iFunction.classConstructor)
        {
            // (undefined: its this (which must have been initialized by super()); another object: that object; else a TypeError (before
            // this is checked))
            if (iFunction.thisBinding != nullptr && iFunction.thisBinding->captured)
                load_binding(iFunction.thisBinding);
            else
                get(iThisLocal);
            runtime("derived_result", { I64, I64 }, I64);
            // (its exception is the constructor's (not its handlers', which this return has left): returned as it is)
        }
        op(opcode::Return);
    }

    void function_generator::try_statement(node* aNode)
    {
        auto* block = aNode->child(0u);
        node* handlerNode = nullptr;
        node* finallyNode = nullptr;
        for (std::size_t i = 1u; i < aNode->size(); ++i)
        {
            if (aNode->child(i)->is(construct::Catch))
                handlerNode = aNode->child(i);
            else if (aNode->child(i)->is(construct::Finally))
                finallyNode = aNode->child(i);
        }
        std::optional<std::size_t> fin;
        if (finallyNode != nullptr)
        {
            begin(control_kind::Finally);
            fin = iControls.size() - 1u;
            iControls.back().completionLocal = local(I32);
            iControls.back().valueLocal = local();
            i32(CompletionNormal);
            set(iControls.back().completionLocal);
        }
        auto const leave = [&]()
        {
            // (a normal completion: on to the finally block (or past the statement))
            if (fin)
            {
                i32(CompletionNormal);
                set(iControls[*fin].completionLocal);
                branch(*fin);
            }
        };
        std::optional<std::size_t> after;
        if (!fin)
        {
            begin(control_kind::Block);
            after = iControls.size() - 1u;
        }
        if (fin)
            begin(control_kind::Handler);   // (exceptions in the catch block (or the try block, if there is no catch block))
        if (handlerNode != nullptr)
        {
            begin(control_kind::Handler);
            statement(block);
            if (fin)
                leave();
            else
                branch(*after);
            end_control();
            // (the catch block: its parameter is the exception)
            auto* s = handlerNode->ownScope;
            enter_scope(s);
            runtime("catch", {}, I64);
            if (handlerNode->size() == 2u)
                bind_pattern(handlerNode->child(0u), true, binding_kind::Catch);
            else
                drop();
            if (iCompletion)
            {
                // (the completion value is the catch block's (undefined if it has none), not the try block's)
                undefined();
                set(*iCompletion);
            }
            statement(handlerNode->children.back());
            leave_scope(s);
            if (fin)
                leave();
        }
        else
        {
            statement(block);
            leave();
        }
        if (fin)
        {
            end_control();
            // (an exception: kept while the finally block runs)
            runtime("catch", {}, I64);
            set(iControls[*fin].valueLocal);
            i32(CompletionThrow);
            set(iControls[*fin].completionLocal);
        }
        if (after)
            end_control();
        if (fin)
        {
            auto jumps = std::move(iControls.back().jumps);
            auto const completion = iControls.back().completionLocal;
            auto const value = iControls.back().valueLocal;
            end_control();
            // (the completion value: the try (or catch) block's, unless the finally block completes abruptly (then its own, or undefined))
            std::optional<std::uint32_t> savedCompletion;
            if (iCompletion)
            {
                savedCompletion = temporary();
                get(*iCompletion);
                set(*savedCompletion);
                undefined();
                set(*iCompletion);
            }
            statement(finallyNode->child(0u));
            if (savedCompletion)
            {
                get(*savedCompletion);
                set(*iCompletion);
                release(*savedCompletion);
            }
            // (the completion that the finally block interrupted)
            get(completion);
            i32(CompletionThrow);
            op(opcode::I32Eq);
            op(opcode::If);
            iCode.push_back(0x40u);
            iControls.push_back(control{ control_kind::Block });
            get(value);
            runtime("throw", { I64 }, I64);
            check_void();
            end_control();
            get(completion);
            i32(CompletionReturn);
            op(opcode::I32Eq);
            op(opcode::If);
            iCode.push_back(0x40u);
            iControls.push_back(control{ control_kind::Block });
            get(value);
            complete_return();
            end_control();
            for (std::size_t j = 0u; j < jumps.size(); ++j)
            {
                get(completion);
                i32(static_cast<std::int32_t>(CompletionJumps + j));
                op(opcode::I32Eq);
                op(opcode::If);
                iCode.push_back(0x40u);
                iControls.push_back(control{ control_kind::Block });
                jumps[j]();
                end_control();
            }
        }
    }

    void function_generator::with_statement(node* aNode)
    {
        expression(aNode->child(0u));
        runtime("to_object", { I64 }, I64);
        check();
        auto const object = temporary();
        set(object);
        auto* s = aNode->ownScope;
        environment_value();
        get(object);
        release(object);
        runtime("env_with", { I64, I64 }, I64);
        s->environmentLocal = local();
        set(*s->environmentLocal);
        iScopes.push_back(s);
        statement(aNode->child(1u));
        leave_scope(s);
        s->environmentLocal = std::nullopt;
    }

    // --- expressions

    void function_generator::literal(node* aNode)
    {
        switch (aNode->kind)
        {
        case construct::Null:
            constant(values::Null);
            break;
        case construct::True:
            constant(values::True);
            break;
        case construct::False:
            constant(values::False);
            break;
        case construct::Number:
            {
                bool legacy = false;
                auto const n = number_value(aNode->source, legacy);
                if (legacy && iFunction.strict)
                    iCompiler.error(aNode->source, "Octal literals are not allowed in strict mode.");
                if (n)
                {
                    double const v = *n;
                    std::uint64_t bits;
                    std::memcpy(&bits, &v, sizeof(bits));
                    constant(bits);
                }
                else
                {
                    std::string digits;
                    for (auto c : aNode->source)
                        if (c != '_' && c != 'n')
                            digits.push_back(c);
                    op(opcode::I32Const);
                    put_s64(iCode, static_cast<std::int32_t>(iCompiler.string_literal(digits)));
                    runtime("bigint", { STR }, I64);
                }
            }
            break;
        case construct::String:
            {
                bool legacy = false;
                auto const text = unescape(aNode->source.substr(1u, aNode->source.size() - 2u), false, &legacy);
                if (!text)
                    iCompiler.error(aNode->source, "Invalid escape sequence");
                if (legacy && iFunction.strict)
                    iCompiler.error(aNode->source, "Octal escape sequences are not allowed in strict mode.");
                string_value(literal_encoding(*text));
            }
            break;
        case construct::RegExp:
            {
                auto const end = aNode->source.rfind('/');
                auto const pattern = aNode->source.substr(1u, end - 1u);
                auto const flags = aNode->source.substr(end + 1u);
                {
                    // (early errors: its flags and pattern (the runtime's regular expression parser))
                    auto const utf16 = [](std::string_view aText)
                    {
                        std::u16string result;
                        for (std::size_t i = 0u; i < aText.size();)
                        {
                            auto const c = static_cast<unsigned char>(aText[i]);
                            std::size_t n = c < 0x80u ? 1u : c < 0xE0u ? 2u : c < 0xF0u ? 3u : 4u;
                            char32_t cp = n == 1u ? c : n == 2u ? (c & 0x1Fu) : n == 3u ? (c & 0x0Fu) : (c & 0x07u);
                            for (std::size_t k = 1u; k < n && i + k < aText.size(); ++k)
                                cp = (cp << 6) | (static_cast<unsigned char>(aText[i + k]) & 0x3Fu);
                            i += n;
                            if (cp >= 0x10000u)
                            {
                                result.push_back(static_cast<char16_t>(0xD800u + ((cp - 0x10000u) >> 10)));
                                result.push_back(static_cast<char16_t>(0xDC00u + ((cp - 0x10000u) & 0x3FFu)));
                            }
                            else
                                result.push_back(static_cast<char16_t>(cp));
                        }
                        return result;
                    };
                    auto const flagBits = javascript_runtime::regexp::parse_flags(utf16(flags));
                    if (!flagBits || ((*flagBits & javascript_runtime::regexp::FlagUnicode) && (*flagBits & javascript_runtime::regexp::FlagUnicodeSets)))
                        iCompiler.error(aNode->source, "Invalid regular expression flags");
                    try
                    {
                        javascript_runtime::regexp::compile(utf16(pattern), *flagBits);
                    }
                    catch (javascript_runtime::regexp::syntax_error const& e)
                    {
                        iCompiler.error(aNode->source, "Invalid regular expression: /" + std::string{ pattern } + "/" + std::string{ flags } + ": " + e.message);
                    }
                }
                string_value(utf8_encoding(pattern));
                string_value(utf8_encoding(flags));
                runtime("regexp", { I64, I64 }, I64);
                check();
            }
            break;
        default:
            break;
        }
    }

    void function_generator::identifier(node* aNode, bool aTypeof)
    {
        auto const name = name_of(aNode);
        if (name == "undefined")
        {
            auto const r = resolve(name, aNode);
            if (r.kind == reference::Global)
            {
                undefined();
                return;
            }
        }
        if (name == "arguments" && !iFunction.script)
        {
            // (the arguments object of the function, or of the function that encloses an arrow function)
            auto const r = resolve(name, aNode);
            if (r.kind != reference::Global || iFunction.argumentsBinding != nullptr)
            {
                load(r, aTypeof, aNode);
                return;
            }
        }
        load(resolve(name, aNode), aTypeof, aNode);
    }

    bool function_generator::derived_eval_code() const
    {
        return iCompiler.eval_code() && (iCompiler.iEvalFlags & compiler::EvalDerivedConstructor) != 0u;
    }

    void function_generator::this_value(node* aSite)
    {
        (void)aSite;
        if (derived_eval_code())
        {
            // (eval code in a derived class's constructor: the constructor's this (which super() may bind))
            auto* outer = &iFunction;
            while (outer->arrow && outer->parent != nullptr)
                outer = outer->parent;
            if (outer->script)
            {
                environment_value();
                runtime("eval_this_derived", { I64 }, I64);
                check();
                return;
            }
        }
        if (iFunction.script)
        {
            get(ThisLocal);
            return;
        }
        // (an arrow function's this: the enclosing function's)
        auto* f = &iFunction;
        if (f->arrow || (iBody && f->arrow))
        {
            auto const r = resolve("%this", aSite);
            (void)r;
            // (the binding of the function that encloses it)
            auto* outer = f;
            while (outer->arrow && outer->parent != nullptr)
                outer = outer->parent;
            if (outer->script && iCompiler.iModule)
            {
                undefined();    // (a module's this)
                return;
            }
            if (outer->script && !iCompiler.eval_code())
            {
                runtime("global_this", {}, I64);
                return;
            }
            reference ref;
            ref.kind = reference::Environment;
            ref.target = outer->thisBinding;
            ref.name = "this";
            // (hops: environments between here and the enclosing function's top scope)
            std::uint32_t hops = 0u;
            scope* start = iScopes.empty() ? iFunction.top : iScopes.back();
            for (auto* s = start; s != nullptr && s != outer->top; s = s->parent)
                if (s->hasEnvironment)
                    ++hops;
            ref.hops = hops;
            if (ref.target == nullptr)
            {
                runtime("global_this", {}, I64);
                return;
            }
            environment_value();
            i32(static_cast<std::int32_t>(hops));
            i32(static_cast<std::int32_t>(ref.target->slot));
            if (outer->derived)
            {
                key("this");
                runtime("env_get_this", { I64, I32, I32, I64 }, I64);
                check();
            }
            else
                runtime("env_get", { I64, I32, I32 }, I64);
            return;
        }
        if (iFunction.thisBinding != nullptr && iFunction.thisBinding->captured)
        {
            load_binding(iFunction.thisBinding);
            if (iFunction.derived)
            {
                tee(iScratch);
                constant(values::Empty);
                op(opcode::I64Eq);
                op(opcode::If);
                iCode.push_back(0x40u);
                iControls.push_back(control{ control_kind::Block });
                key("this");
                runtime("throw_this", { I64 }, I64);
                check_void();
                end_control();
                get(iScratch);
            }
            return;
        }
        get(iThisLocal);
        if (iFunction.derived)
        {
            tee(iScratch);
            constant(values::Empty);
            op(opcode::I64Eq);
            op(opcode::If);
            iCode.push_back(0x40u);
            iControls.push_back(control{ control_kind::Block });
            key("this");
            runtime("throw_this", { I64 }, I64);
            check_void();
            end_control();
            get(iScratch);
        }
    }

    void function_generator::new_target_value()
    {
        auto* outer = &iFunction;
        while (outer->arrow && outer->parent != nullptr)
            outer = outer->parent;
        if (outer == &iFunction)
        {
            get(NewTargetLocal);
            return;
        }
        if (outer->script && iCompiler.eval_code())
        {
            runtime("eval_new_target", {}, I64);
            return;
        }
        if (outer->script || outer->newTargetBinding == nullptr)
        {
            undefined();
            return;
        }
        std::uint32_t hops = 0u;
        scope* start = iScopes.empty() ? iFunction.top : iScopes.back();
        for (auto* s = start; s != nullptr && s != outer->top; s = s->parent)
            if (s->hasEnvironment)
                ++hops;
        environment_value();
        i32(static_cast<std::int32_t>(hops));
        i32(static_cast<std::int32_t>(outer->newTargetBinding->slot));
        runtime("env_get", { I64, I32, I32 }, I64);
    }

    void function_generator::home_object_value()
    {
        auto* outer = &iFunction;
        while (outer->arrow && outer->parent != nullptr)
            outer = outer->parent;
        if (outer->script && iCompiler.eval_code() && (outer == &iFunction || outer->homeBinding == nullptr))
        {
            // (eval code: the home object of the function that called eval (its arrow functions': the eval code's %home binding's))
            runtime("eval_home", {}, I64);
            return;
        }
        if (outer == &iFunction)
        {
            get(CalleeLocal);
            runtime("function_home", { I64 }, I64);
            return;
        }
        if (outer->homeBinding == nullptr)
        {
            undefined();
            return;
        }
        std::uint32_t hops = 0u;
        scope* start = iScopes.empty() ? iFunction.top : iScopes.back();
        for (auto* s = start; s != nullptr && s != outer->top; s = s->parent)
            if (s->hasEnvironment)
                ++hops;
        environment_value();
        i32(static_cast<std::int32_t>(hops));
        i32(static_cast<std::int32_t>(outer->homeBinding->slot));
        runtime("env_get", { I64, I32, I32 }, I64);
    }

    void function_generator::expression(node* aNode)
    {
        // (whether its value is used: only the expression of an expression statement (or a for statement's update) is discarded)
        iDiscardedNow = std::exchange(iDiscarded, false);
        if (numeric_value(aNode))
            return;
        switch (aNode->kind)
        {
        case construct::Null:
        case construct::True:
        case construct::False:
        case construct::Number:
        case construct::String:
        case construct::RegExp:
            literal(aNode);
            break;
        case construct::This:
            this_value(aNode);
            break;
        case construct::Identifier:
            identifier(aNode);
            break;
        case construct::Parenthesized:
            expression(aNode->child(0u));
            break;
        case construct::Template:
            template_literal(aNode);
            break;
        case construct::Array:
            array_literal(aNode);
            break;
        case construct::Object:
            object_literal(aNode);
            break;
        case construct::FunctionExpression:
        case construct::AsyncFunctionExpression:
        case construct::Arrow:
        case construct::AsyncArrow:
            function_expression(aNode);
            break;
        case construct::ClassExpression:
            class_expression(aNode);
            break;
        case construct::Dot:
        case construct::Index:
        case construct::DotPrivate:
            member(aNode);
            break;
        case construct::SuperDot:
        case construct::SuperIndex:
            super_property(aNode);
            break;
        case construct::Call:
            call_expression(aNode);
            break;
        case construct::SuperCall:
            super_call(aNode);
            break;
        case construct::New:
            new_expression(aNode);
            break;
        case construct::NewTarget:
            new_target_value();
            break;
        case construct::Tagged:
            tagged_template(aNode);
            break;
        case construct::Optional:
            optional_chain(aNode);
            break;
        case construct::Assign:
            assignment(aNode);
            break;
        case construct::PreIncrement:
        case construct::PreDecrement:
        case construct::PostIncrement:
        case construct::PostDecrement:
            update(aNode);
            break;
        case construct::Delete:
            delete_expression(aNode);
            break;
        case construct::Void:
            expression(aNode->child(0u));
            drop();
            undefined();
            break;
        case construct::TypeOf:
            if (auto* operand = aNode->child(0u); [&]()
                {
                    // (typeof (x): a parenthesized identifier is still a reference)
                    while (operand->is(construct::Parenthesized) && operand->size() == 1u)
                        operand = operand->child(0u);
                    return operand->is(construct::Identifier);
                }())
                identifier(operand, true);
            else
                expression(aNode->child(0u));
            runtime("typeof", { I64 }, I64);
            break;
        case construct::Plus:
        case construct::Negate:
        case construct::BitNot:
        case construct::Not:
            unary(aNode);
            break;
        case construct::Await:
            expression(aNode->child(0u));
            await_value();
            break;
        case construct::Yield:
            yield_expression(aNode);
            break;
        case construct::And:
        case construct::Or:
        case construct::Coalesce:
            logical(aNode);
            break;
        case construct::Conditional:
            conditional(aNode);
            break;
        case construct::Comma:
            expression(aNode->child(0u));
            drop();
            expression(aNode->child(1u));
            break;
        case construct::Exponent:
        case construct::Multiply:
        case construct::Divide:
        case construct::Remainder:
        case construct::Add:
        case construct::Subtract:
        case construct::ShiftLeft:
        case construct::ShiftRight:
        case construct::ShiftRightUnsigned:
        case construct::LessThan:
        case construct::GreaterThan:
        case construct::LessEqual:
        case construct::GreaterEqual:
        case construct::InstanceOf:
        case construct::In:
        case construct::StrictEqual:
        case construct::StrictNotEqual:
        case construct::Equal:
        case construct::NotEqual:
        case construct::BitAnd:
        case construct::BitXor:
        case construct::BitOr:
            binary(aNode);
            break;
        case construct::PrivateIn:
            {
                auto const name = name_of(aNode->child(0u));
                load(resolve(name, aNode));
                expression(aNode->child(1u));
                runtime("private_in", { I64, I64 }, I64);
                check();
            }
            break;
        case construct::ImportCall:
            expression(aNode->child(0u));
            if (aNode->size() > 1u)
                expression(aNode->child(1u));
            else
                undefined();
            runtime("import_dynamic", { I64, I64 }, I64);
            check();
            break;
        case construct::ImportMeta:
            if (!iCompiler.iModule)
                iCompiler.error(aNode->source, "Cannot use 'import.meta' outside a module");
            environment_value();
            runtime("import_meta", { I64 }, I64);
            check();
            break;
        default:
            iCompiler.error(aNode->source, std::string{ "unsupported expression (" } + std::string{ language::javascript::info(aNode->kind).name } + ")");
        }
    }

    void function_generator::template_literal(node* aNode)
    {
        // (its strings and the values of its substitutions (as strings) concatenated)
        std::string pending;
        bool havePending = false;
        bool haveValue = false;
        auto const flush = [&]()
        {
            if (!havePending)
                return;
            string_value(literal_encoding(*unescape(pending, true)));
            if (haveValue)
            {
                runtime("concat", { I64, I64 }, I64);
                check();
            }
            haveValue = true;
            havePending = false;
            pending.clear();
        };
        for (auto* c : aNode->children)
        {
            if (c->is(construct::TemplateString))
            {
                if (!unescape(c->source, true))
                    iCompiler.error(c->source, "Invalid escape sequence in template");
                pending += c->source;
                havePending = true;
                continue;
            }
            flush();
            expression(c);
            runtime("to_string", { I64 }, I64);
            check();
            if (haveValue)
            {
                runtime("concat", { I64, I64 }, I64);
                check();
            }
            haveValue = true;
        }
        if (!haveValue && !havePending)
            havePending = true;
        flush();
    }

    void function_generator::tagged_template(node* aNode)
    {
        // tag`...`: tag(template object, ...substitutions) (a member tag's this is its object)
        auto* tag = aNode->child(0u);
        auto* literalNode = aNode->child(1u);
        auto const callee = local();
        auto const thisValue = local();
        if (tag->is(construct::Dot) || tag->is(construct::Index))
        {
            expression(tag->child(0u));
            set(thisValue);
            get(thisValue);
            if (tag->is(construct::Dot))
            {
                key(name_of(tag->child(1u)));
                runtime("get_named", { I64, I64 }, I64);
            }
            else
            {
                expression(tag->child(1u));
                runtime("get", { I64, I64 }, I64);
            }
            check();
            set(callee);
        }
        else
        {
            expression(tag);
            set(callee);
            undefined();
            set(thisValue);
        }
        // (the template object: cooked strings (undefined for an invalid escape) and raw strings; one for each site)
        auto const cooked = local();
        auto const raw = local();
        i32(0);
        runtime("array", { I32 }, I64);
        set(cooked);
        i32(0);
        runtime("array", { I32 }, I64);
        set(raw);
        std::vector<node*> substitutions;
        std::string pending;
        bool havePending = false;
        auto const flush = [&]()
        {
            get(cooked);
            if (auto const value = unescape(pending, true))
                string_value(literal_encoding(*value));
            else
                undefined();
            runtime("array_push", { I64, I64 }, I64);
            drop();
            get(raw);
            // (the raw string: CR and CRLF are LF)
            std::string rawText;
            for (std::size_t i = 0u; i < pending.size(); ++i)
            {
                if (pending[i] == '\r')
                {
                    rawText.push_back('\n');
                    if (i + 1u < pending.size() && pending[i + 1u] == '\n')
                        ++i;
                }
                else
                    rawText.push_back(pending[i]);
            }
            string_value(utf8_encoding(rawText));
            runtime("array_push", { I64, I64 }, I64);
            drop();
            pending.clear();
            havePending = false;
        };
        for (auto* c : literalNode->children)
        {
            if (c->is(construct::TemplateString))
            {
                pending += c->source;
                havePending = true;
                continue;
            }
            flush();
            substitutions.push_back(c);
        }
        flush();
        (void)havePending;
        i32(static_cast<std::int32_t>(iCompiler.iTemplateSites++));
        get(cooked);
        get(raw);
        runtime("template_object", { I32, I64, I64 }, I64);
        auto const site = local();
        set(site);
        // (the call)
        std::vector<node*> arguments;
        arguments.push_back(nullptr);   // (the template object)
        for (auto* s : substitutions)
            arguments.push_back(s);
        // (arguments: the template object then the substitutions)
        std::vector<std::uint32_t> values;
        values.push_back(site);
        for (auto* s : substitutions)
        {
            expression(s);
            auto const v = local();
            set(v);
            values.push_back(v);
        }
        auto const argc = static_cast<std::uint32_t>(values.size());
        for (std::size_t i = RegisterArguments; i < values.size(); ++i)
        {
            get(values[i]);
            runtime("push_arg", { I64 }, I64);
            drop();
        }
        get(callee);
        get(thisValue);
        i32(static_cast<std::int32_t>(argc));
        for (std::uint32_t i = 0u; i < RegisterArguments; ++i)
        {
            if (i < values.size())
                get(values[i]);
            else
                undefined();
        }
        bool const tail = tail_call(aNode);
        runtime(tail ? "tail_call" : "call", { I64, I64, I32, I64, I64, I64, I64 }, I64);
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
        if (!tail)
            check();
    }

    void function_generator::member(node* aNode)
    {
        expression(aNode->child(0u));
        switch (aNode->kind)
        {
        case construct::Dot:
            key(name_of(aNode->child(1u)));
            runtime("get_named", { I64, I64 }, I64);
            break;
        case construct::DotPrivate:
            load(resolve(name_of(aNode->child(1u)), aNode));
            runtime("private_get", { I64, I64 }, I64);
            break;
        default:
            expression(aNode->child(1u));
            runtime("get", { I64, I64 }, I64);
            break;
        }
        check();
    }

    void function_generator::super_property(node* aNode)
    {
        // super.x / super[x]: the home object's prototype's property, with this as its receiver
        std::uint32_t thisValue = 0u, keyValue = 0u;
        auto const base = super_reference(aNode, thisValue, keyValue);
        get(base);
        get(keyValue);
        get(thisValue);
        runtime("super_get", { I64, I64, I64 }, I64);
        check();
        release(base);
        release(keyValue);
        release(thisValue);
    }

    std::uint32_t function_generator::super_reference(node* aTarget, std::uint32_t& aThis, std::uint32_t& aKey)
    {
        // (super.x / super[x]'s reference: this, the key (super[x]'s: not yet a property key (GetValue or PutValue makes it one)) and
        // the super base (the home object's prototype when the reference is made (GetSuperBase)), as temporaries; returns the base)
        this_value(aTarget);
        aThis = temporary();
        set(aThis);
        if (aTarget->is(construct::SuperDot))
            key(name_of(aTarget->child(0u)));
        else
            expression(aTarget->child(0u));
        aKey = temporary();
        set(aKey);
        home_object_value();
        runtime("super_base", { I64 }, I64);
        check();
        auto const base = temporary();
        set(base);
        return base;
    }

    void function_generator::arguments_values(std::vector<node*> const& aArguments, std::optional<std::uint32_t>& aSpread)
    {
        (void)aArguments;
        (void)aSpread;
    }

    bool function_generator::tail_call(node* aCall) const
    {
        // (a call in tail position of a strict function (not a generator's, an async function's or a class constructor's) whose return
        // leaves the function at once (no finally blocks, exception handlers, iterators to close or resources to dispose of): a proper
        // tail call)
        if (!iTailCalls.contains(aCall))
            return false;
        auto const& f = iFunction;
        if (!f.strict || f.script || f.generator || f.async || f.classConstructor || iBody || !iDisposals.empty())
            return false;
        for (auto const& c : iControls)
            if (c.kind == control_kind::Handler || c.kind == control_kind::Finally || c.kind == control_kind::Cleanup)
                return false;
        return true;
    }

    void function_generator::tail_positions(node* aExpression)
    {
        // (the calls in tail position of a return statement's expression)
        if (aExpression == nullptr)
            return;
        switch (aExpression->kind)
        {
        case construct::Parenthesized:
            tail_positions(aExpression->child(0u));
            break;
        case construct::Comma:
            tail_positions(aExpression->child(1u));
            break;
        case construct::Conditional:
            tail_positions(aExpression->child(1u));
            tail_positions(aExpression->child(2u));
            break;
        case construct::And:
        case construct::Or:
        case construct::Coalesce:
            tail_positions(aExpression->child(1u));
            break;
        case construct::Call:
        case construct::Tagged:
            iTailCalls.insert(aExpression);
            break;
        default:
            break;
        }
    }

    void function_generator::check_call()
    {
        // (a call's result: Exception is a pending tail call of the callee's (made now), or an exception)
        tee(iScratch);
        constant(values::Exception);
        op(opcode::I64Eq);
        op(opcode::If);
        iCode.push_back(0x40u);
        iControls.push_back(control{ control_kind::Block });
        runtime("tail_resolve", {}, I64);
        tee(iScratch);
        constant(values::Exception);
        op(opcode::I64Eq);
        branch(handler(), true);
        end_control();
        get(iScratch);
    }

    void function_generator::call_value(std::uint32_t aCallee, std::uint32_t aThis, std::vector<node*> const& aArguments, bool aTail)
    {
        // (the arguments are evaluated (into locals), then the callee is called: the program's own functions through the
        // dispatcher, others through the runtime)
        bool const spread = std::any_of(aArguments.begin(), aArguments.end(), [](node* a) { return a->is(construct::Spread); });
        if (spread)
        {
            i32(static_cast<std::int32_t>(aArguments.size()));
            runtime("array", { I32 }, I64);
            auto const list = temporary();
            set(list);
            for (auto* a : aArguments)
            {
                get(list);
                if (a->is(construct::Spread))
                {
                    expression(a->child(0u));
                    runtime("array_spread", { I64, I64 }, I64);
                }
                else
                {
                    expression(a);
                    runtime("array_push", { I64, I64 }, I64);
                }
                check_void();
            }
            get(aCallee);
            get(aThis);
            get(list);
            runtime(aTail ? "tail_call_spread" : "call_spread", { I64, I64, I64 }, I64);
            if (!aTail)
                check();
            release(list);
            return;
        }
        std::vector<std::uint32_t> values;
        for (auto* a : aArguments)
        {
            expression(a);
            auto const v = temporary();
            set(v);
            values.push_back(v);
        }
        auto const argc = static_cast<std::uint32_t>(values.size());
        for (std::size_t i = RegisterArguments; i < values.size(); ++i)
        {
            get(values[i]);
            runtime("push_arg", { I64 }, I64);
            drop();
        }
        if (aTail)
        {
            // (a tail call: made by the caller once this function has returned)
            get(aCallee);
            get(aThis);
            i32(static_cast<std::int32_t>(argc));
            for (std::uint32_t i = 0u; i < RegisterArguments; ++i)
            {
                if (i < values.size())
                    get(values[i]);
                else
                    undefined();
            }
            runtime("tail_call", { I64, I64, I32, I64, I64, I64, I64 }, I64);
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
            for (auto v : values)
                release(v);
            return;
        }
        // (the program's function: its number)
        auto const number = temporary(I32);
        get(aCallee);
        runtime("callee_number", { I64 }, I32);
        tee(number);
        i32(0);
        op(opcode::I32GeS);
        op(opcode::If);
        iCode.push_back(0x7Eu);
        iControls.push_back(control{ control_kind::Block });
        get(number);
        get(aCallee);
        get(aThis);
        undefined();
        i32(static_cast<std::int32_t>(argc));
        for (std::uint32_t i = 0u; i < RegisterArguments; ++i)
        {
            if (i < values.size())
                get(values[i]);
            else
                undefined();
        }
        call(iCompiler.iDispatcher);
        op(opcode::Else);
        get(aCallee);
        get(aThis);
        i32(static_cast<std::int32_t>(argc));
        for (std::uint32_t i = 0u; i < RegisterArguments; ++i)
        {
            if (i < values.size())
                get(values[i]);
            else
                undefined();
        }
        runtime("call", { I64, I64, I32, I64, I64, I64, I64 }, I64);
        end_control();
        release(number);
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
        check_call();
        for (auto v : values)
            release(v);
    }

    void function_generator::call_expression(node* aNode)
    {
        auto* calleeNode = aNode->child(0u);
        std::vector<node*> arguments{ aNode->children.begin() + 1, aNode->children.end() };
        auto const callee = temporary();
        auto const thisValue = temporary();
        // ((a.b)(): a parenthesized reference is the reference (its base is the this))
        while (calleeNode->is(construct::Parenthesized) && calleeNode->size() == 1u && is_call_reference(calleeNode->child(0u)))
            calleeNode = calleeNode->child(0u);
        if (calleeNode->is(construct::Optional))
        {
            // ((a?.b)(): the chain's reference's base is the this)
            std::optional<std::uint32_t> chainThis;
            optional_chain(calleeNode, &chainThis);
            set(callee);
            if (chainThis)
            {
                get(*chainThis);
                release(*chainThis);
            }
            else
                undefined();
            set(thisValue);
        }
        else if (calleeNode->is(construct::Dot) || calleeNode->is(construct::Index) || calleeNode->is(construct::DotPrivate))
        {
            // (a method call: this is the object)
            expression(calleeNode->child(0u));
            tee(thisValue);
            if (calleeNode->is(construct::Dot))
            {
                key(name_of(calleeNode->child(1u)));
                runtime("get_named", { I64, I64 }, I64);
            }
            else if (calleeNode->is(construct::DotPrivate))
            {
                load(resolve(name_of(calleeNode->child(1u)), calleeNode));
                runtime("private_get", { I64, I64 }, I64);
            }
            else
            {
                expression(calleeNode->child(1u));
                runtime("get", { I64, I64 }, I64);
            }
            check();
            set(callee);
        }
        else if (calleeNode->is(construct::SuperDot) || calleeNode->is(construct::SuperIndex))
        {
            super_property(calleeNode);
            set(callee);
            this_value(calleeNode);
            set(thisValue);
        }
        else if (calleeNode->is(construct::Identifier) && name_of(calleeNode) == "eval")
        {
            // (a direct eval (if eval is %eval%): the code is compiled in the scope of the call)
            identifier(calleeNode);
            set(callee);
            std::vector<std::uint32_t> values;
            bool const spread = std::any_of(arguments.begin(), arguments.end(), [](node* a) { return a->is(construct::Spread); });
            std::optional<std::uint32_t> list;
            if (spread)
            {
                // (spread arguments: the argument list (its first element is the source; its length decides whether there is one))
                i32(static_cast<std::int32_t>(arguments.size()));
                runtime("array", { I32 }, I64);
                list = temporary();
                set(*list);
                for (auto* a : arguments)
                {
                    get(*list);
                    if (a->is(construct::Spread))
                    {
                        expression(a->child(0u));
                        runtime("array_spread", { I64, I64 }, I64);
                    }
                    else
                    {
                        expression(a);
                        runtime("array_push", { I64, I64 }, I64);
                    }
                    check_void();
                }
            }
            else
                for (auto* a : arguments)
                {
                    expression(a);
                    auto const v = temporary();
                    set(v);
                    values.push_back(v);
                }
            get(callee);
            if (list)
            {
                get(*list);
                key("0");
                runtime("get_named", { I64, I64 }, I64);
                check();
                get(*list);
                key("length");
                runtime("get_named", { I64, I64 }, I64);
                check();
                op(opcode::F64ReinterpretI64);
                op(opcode::I32SConvertF64);
            }
            else
            {
                if (!values.empty())
                    get(values[0]);
                else
                    undefined();
                i32(static_cast<std::int32_t>(values.size()));
            }
            {
                // (the eval code's context: strict, and what it may use (new.target, super.x, super(), arguments))
                auto* outer = &iFunction;
                while (outer->arrow && outer->parent != nullptr)
                    outer = outer->parent;
                std::uint32_t flags = iFunction.strict ? compiler::EvalStrict : 0u;
                if (outer->script)
                    flags |= iCompiler.iEvalFlags & ~compiler::EvalStrict;
                else
                {
                    flags |= compiler::EvalFunction;
                    if (outer->method || outer->classConstructor || outer->fieldInitializer || outer->staticBlock)
                        flags |= compiler::EvalMethod;
                    if (outer->classConstructor && outer->derived)
                        flags |= compiler::EvalDerivedConstructor;
                    if (outer->fieldInitializer)
                        flags |= compiler::EvalFieldInitializer;
                }
                if (tail_call(aNode))
                    flags |= compiler::EvalTailCall;    // (not eval itself: a tail call)
                if (!iFunction.script)
                {
                    // (in the function's parameters (not in a function within them))
                    for (node* n = aNode->parent; n != nullptr && n->function == nullptr; n = n->parent)
                        if (n->is(construct::Parameters))
                        {
                            flags |= compiler::EvalParameters;
                            if (!iFunction.arrow)
                                flags |= compiler::EvalArgumentsObject;
                            break;
                        }
                }
                i32(static_cast<std::int32_t>(flags));
            }
            environment_value();
            {
                auto* outer = &iFunction;
                while (outer->arrow && outer->parent != nullptr)
                    outer = outer->parent;
                if ((!outer->script && outer->classConstructor && outer->derived) || (outer->script && derived_eval_code()))
                    constant(values::Empty);    // (the eval code finds the constructor's this itself: it may not be bound yet)
                else
                    this_value(calleeNode);
            }
            new_target_value();
            home_object_value();
            runtime("eval_direct", { I64, I64, I32, I32, I64, I64, I64, I64 }, I64);
            if (!tail_call(aNode))
                check();
            for (auto v : values)
                release(v);
            if (list)
                release(*list);
            release(thisValue);
            release(callee);
            return;
        }
        else
        {
            // (a with statement's object is the this of a function that is its property: the reference is resolved once (its base is
            // the this))
            std::optional<reference> dynamicCallee;
            if (calleeNode->is(construct::Identifier))
                if (auto const r = resolve(name_of(calleeNode), calleeNode); r.kind == reference::Dynamic && !r.name.starts_with("#"))
                    dynamicCallee = r;
            if (dynamicCallee)
            {
                auto const base = dynamic_reference(*dynamicCallee);
                dynamic_reference_get(base, *dynamicCallee);
                set(callee);
                get(base);
                runtime("dyn_reference_this", { I64 }, I64);
                set(thisValue);
                release(base);
            }
            else
            {
                expression(calleeNode);
                set(callee);
                undefined();
                set(thisValue);
            }
        }
        call_value(callee, thisValue, arguments, tail_call(aNode));
        release(thisValue);
        release(callee);
    }

    void function_generator::new_expression(node* aNode)
    {
        expression(aNode->child(0u));
        auto const callee = temporary();
        set(callee);
        std::vector<node*> arguments;
        if (aNode->size() > 1u)
            arguments.assign(aNode->child(1u)->children.begin(), aNode->child(1u)->children.end());
        bool const spread = std::any_of(arguments.begin(), arguments.end(), [](node* a) { return a->is(construct::Spread); });
        i32(static_cast<std::int32_t>(arguments.size()));
        runtime("array", { I32 }, I64);
        auto const list = temporary();
        set(list);
        for (auto* a : arguments)
        {
            get(list);
            if (a->is(construct::Spread))
            {
                expression(a->child(0u));
                runtime("array_spread", { I64, I64 }, I64);
            }
            else
            {
                expression(a);
                runtime("array_push", { I64, I64 }, I64);
            }
            check_void();
        }
        (void)spread;
        get(callee);
        get(list);
        runtime("construct", { I64, I64 }, I64);
        check();
        release(list);
        release(callee);
    }

    void function_generator::super_call(node* aNode)
    {
        // super(...): the parent constructor constructs this (with new.target); this is then bound and the class's fields initialized
        std::vector<node*> arguments{ aNode->children.begin(), aNode->children.end() };
        i32(static_cast<std::int32_t>(arguments.size()));
        runtime("array", { I32 }, I64);
        auto const list = temporary();
        set(list);
        for (auto* a : arguments)
        {
            get(list);
            if (a->is(construct::Spread))
            {
                expression(a->child(0u));
                runtime("array_spread", { I64, I64 }, I64);
            }
            else
            {
                expression(a);
                runtime("array_push", { I64, I64 }, I64);
            }
            check_void();
        }
        // (the constructor: the function itself, or the function that encloses the arrow function)
        auto* outer = &iFunction;
        while (outer->arrow && outer->parent != nullptr)
            outer = outer->parent;
        if (outer->script && derived_eval_code())
        {
            // (eval code: the constructor's bindings, found by name)
            environment_value();
            key("%callee");
            runtime("eval_internal", { I64, I64 }, I64);
            check();
            new_target_value();
            get(list);
            runtime("super_construct", { I64, I64, I64 }, I64);
            check();
            release(list);
            auto const result = temporary();
            set(result);
            environment_value();
            get(result);
            runtime("eval_bind_this", { I64, I64 }, I64);
            check_void();
            get(result);
            environment_value();
            key("%callee");
            runtime("eval_internal", { I64, I64 }, I64);
            check();
            runtime("initialize_fields", { I64, I64 }, I64);
            check_void();
            get(result);
            release(result);
            return;
        }
        if (outer == &iFunction)
            get(CalleeLocal);
        else
        {
            auto* b = outer->top->find("%callee");
            if (b == nullptr)
                iCompiler.error(aNode->source, "'super' keyword unexpected here");
            std::uint32_t hops = 0u;
            scope* start = iScopes.empty() ? iFunction.top : iScopes.back();
            for (auto* s = start; s != nullptr && s != outer->top; s = s->parent)
                if (s->hasEnvironment)
                    ++hops;
            environment_value();
            i32(static_cast<std::int32_t>(hops));
            i32(static_cast<std::int32_t>(b->slot));
            runtime("env_get", { I64, I32, I32 }, I64);
        }
        new_target_value();
        get(list);
        runtime("super_construct", { I64, I64, I64 }, I64);
        check();
        release(list);
        // (this is bound: once)
        auto const result = temporary();
        set(result);
        if (outer == &iFunction && (iFunction.thisBinding == nullptr || !iFunction.thisBinding->captured))
        {
            get(iThisLocal);
            constant(values::Empty);
            op(opcode::I64Ne);
            op(opcode::If);
            iCode.push_back(0x40u);
            iControls.push_back(control{ control_kind::Block });
            runtime("throw_super_twice", {}, I64);
            check_void();
            end_control();
            get(result);
            set(iThisLocal);
        }
        else
        {
            auto* b = outer->thisBinding;
            std::uint32_t hops = 0u;
            scope* start = iScopes.empty() ? iFunction.top : iScopes.back();
            for (auto* s = start; s != nullptr && s != outer->top; s = s->parent)
                if (s->hasEnvironment)
                    ++hops;
            environment_value();
            i32(static_cast<std::int32_t>(hops));
            i32(static_cast<std::int32_t>(b->slot));
            get(result);
            runtime("env_bind_this", { I64, I32, I32, I64 }, I64);
            check_void();
            if (outer == &iFunction)
            {
                get(result);
                set(iThisLocal);
            }
        }
        // (the class's fields)
        get(result);
        if (outer == &iFunction)
            get(CalleeLocal);
        else
        {
            auto* b = outer->top->find("%callee");
            std::uint32_t hops = 0u;
            scope* start = iScopes.empty() ? iFunction.top : iScopes.back();
            for (auto* s = start; s != nullptr && s != outer->top; s = s->parent)
                if (s->hasEnvironment)
                    ++hops;
            environment_value();
            i32(static_cast<std::int32_t>(hops));
            i32(static_cast<std::int32_t>(b->slot));
            runtime("env_get", { I64, I32, I32 }, I64);
        }
        runtime("initialize_fields", { I64, I64 }, I64);
        check_void();
        get(result);
        release(result);
    }

    bool function_generator::is_call_reference(node* aNode) const
    {
        // (an expression whose value is a reference that a call takes its this from (or a direct eval's eval))
        switch (aNode->kind)
        {
        case construct::Dot:
        case construct::Index:
        case construct::DotPrivate:
        case construct::SuperDot:
        case construct::SuperIndex:
        case construct::Identifier:
        case construct::Optional:
            return true;
        case construct::Parenthesized:
            return aNode->size() == 1u && is_call_reference(aNode->child(0u));
        default:
            return false;
        }
    }

    void function_generator::optional_chain(node* aNode, std::optional<std::uint32_t>* aThis)
    {
        // a?.b.c: if a is null or undefined the whole chain is undefined (the chain's optional links branch out of its block)
        begin(control_kind::Block, I64);
        iControls.back().kind = control_kind::Block;
        auto const chain = iControls.size() - 1u;
        std::function<void(node*, std::optional<std::uint32_t>*)> link;
        // (evaluates a link of the chain; aThis: where a call's this is stored (the object of a member link))
        link = [&](node* n, std::optional<std::uint32_t>* aThis)
        {
            auto const short_circuit = [&]()
            {
                // (the value on the stack: undefined (the chain's value) if it is null or undefined)
                tee(iScratch);
                constant(values::Undefined);
                op(opcode::I64Eq);
                get(iScratch);
                constant(values::Null);
                op(opcode::I64Eq);
                op(opcode::I32Ior);
                op(opcode::If);
                iCode.push_back(0x40u);
                iControls.push_back(control{ control_kind::Block });
                undefined();
                branch(chain);
                end_control();
                get(iScratch);
            };
            switch (n->kind)
            {
            case construct::OptionalDot:
            case construct::OptionalIndex:
            case construct::OptionalPrivate:
            case construct::Dot:
            case construct::Index:
            case construct::DotPrivate:
                {
                    link(n->child(0u), nullptr);
                    if (n->is(construct::OptionalDot) || n->is(construct::OptionalIndex) || n->is(construct::OptionalPrivate))
                        short_circuit();
                    if (aThis != nullptr)
                    {
                        *aThis = temporary();
                        tee(**aThis);
                    }
                    if (n->is(construct::Dot) || n->is(construct::OptionalDot))
                    {
                        key(name_of(n->child(1u)));
                        runtime("get_named", { I64, I64 }, I64);
                    }
                    else if (n->is(construct::DotPrivate) || n->is(construct::OptionalPrivate))
                    {
                        load(resolve(name_of(n->child(1u)), n));
                        runtime("private_get", { I64, I64 }, I64);
                    }
                    else
                    {
                        expression(n->child(1u));
                        runtime("get", { I64, I64 }, I64);
                    }
                    check();
                }
                break;
            case construct::SuperDot:
            case construct::SuperIndex:
                if (aThis != nullptr)
                {
                    *aThis = temporary();
                    this_value(n);
                    set(**aThis);
                }
                super_property(n);
                break;
            case construct::Optional:
                // (a parenthesized chain (a call's callee): a chain of its own)
                optional_chain(n, aThis);
                break;
            case construct::Call:
            case construct::OptionalCall:
                {
                    std::optional<std::uint32_t> thisLocal;
                    auto* calleeNode = n->child(0u);
                    // ((a.b)?.(): a parenthesized reference is the reference (its base is the this))
                    while (calleeNode->is(construct::Parenthesized) && calleeNode->size() == 1u && is_call_reference(calleeNode->child(0u)))
                        calleeNode = calleeNode->child(0u);
                    bool const member = calleeNode->is(construct::Dot) || calleeNode->is(construct::Index) || calleeNode->is(construct::OptionalDot) ||
                        calleeNode->is(construct::OptionalIndex) || calleeNode->is(construct::DotPrivate) || calleeNode->is(construct::OptionalPrivate) ||
                        calleeNode->is(construct::SuperDot) || calleeNode->is(construct::SuperIndex) || calleeNode->is(construct::Optional);
                    link(calleeNode, member ? &thisLocal : nullptr);
                    if (n->is(construct::OptionalCall))
                        short_circuit();
                    auto const callee = temporary();
                    set(callee);
                    if (!thisLocal)
                    {
                        thisLocal = temporary();
                        undefined();
                        set(*thisLocal);
                    }
                    call_value(callee, *thisLocal, std::vector<node*>{ n->children.begin() + 1, n->children.end() });
                    release(callee);
                    release(*thisLocal);
                }
                break;
            default:
                expression(n);
                break;
            }
        };
        link(aNode->child(0u), aThis);
        end_control();
    }

    void function_generator::delete_expression(node* aNode)
    {
        auto* target = aNode->child(0u);
        while (target->is(construct::Parenthesized))
            target = target->child(0u);
        switch (target->kind)
        {
        case construct::Dot:
        case construct::Index:
            expression(target->child(0u));
            if (target->is(construct::Dot))
                key(name_of(target->child(1u)));
            else
                expression(target->child(1u));
            i32(iFunction.strict ? 1 : 0);
            runtime("delete", { I64, I64, I32 }, I64);
            check();
            break;
        case construct::Identifier:
            {
                if (iFunction.strict)
                    iCompiler.error(target->source, "Delete of an unqualified identifier in strict mode.");
                auto const r = resolve(name_of(target), target);
                if (r.kind == reference::Global)
                {
                    key(r.name);
                    runtime("global_delete", { I64 }, I64);
                    check();
                }
                else if (r.kind == reference::Dynamic)
                {
                    environment_value();
                    key(r.name);
                    runtime("dyn_delete", { I64, I64 }, I64);
                    check();
                }
                else
                    constant(values::False);
            }
            break;
        case construct::SuperDot:
        case construct::SuperIndex:
            runtime("throw_super_delete", {}, I64);
            check();
            break;
        default:
            expression(target);
            drop();
            constant(values::True);
            break;
        }
    }

    void function_generator::unary(node* aNode)
    {
        if (aNode->is(construct::Not))
        {
            condition(aNode->child(0u));
            op(opcode::If);
            iCode.push_back(0x7Eu);
            constant(values::False);
            op(opcode::Else);
            constant(values::True);
            op(opcode::End);
            return;
        }
        expression(aNode->child(0u));
        switch (aNode->kind)
        {
        case construct::Plus:
            runtime("to_number", { I64 }, I64);
            break;
        case construct::Negate:
            runtime("negate", { I64 }, I64);
            break;
        default:
            runtime("bitwise_not", { I64 }, I64);
            break;
        }
        check();
    }

    void function_generator::binary(node* aNode)
    {
        if (numeric_binary(aNode))
            return;
        static std::map<construct, std::string> const sOperations = {
            { construct::Exponent, "exponent" }, { construct::Multiply, "multiply" }, { construct::Divide, "divide" }, { construct::Remainder, "remainder" },
            { construct::Add, "add" }, { construct::Subtract, "subtract" }, { construct::ShiftLeft, "shift_left" }, { construct::ShiftRight, "shift_right" },
            { construct::ShiftRightUnsigned, "shift_right_unsigned" }, { construct::LessThan, "less_than" }, { construct::GreaterThan, "greater_than" },
            { construct::LessEqual, "less_equal" }, { construct::GreaterEqual, "greater_equal" }, { construct::InstanceOf, "instance_of" },
            { construct::In, "in" }, { construct::StrictEqual, "strict_equal_value" }, { construct::StrictNotEqual, "strict_not_equal" },
            { construct::Equal, "equal" }, { construct::NotEqual, "not_equal" }, { construct::BitAnd, "bitwise_and" }, { construct::BitXor, "bitwise_xor" },
            { construct::BitOr, "bitwise_or" } };
        expression(aNode->child(0u));
        auto const lhs = temporary();
        set(lhs);
        expression(aNode->child(1u));
        auto const rhs = temporary();
        set(rhs);
        // (numbers: inline for the arithmetic and relational operators)
        std::optional<opcode> inlineOp;
        bool relational = false;
        switch (aNode->kind)
        {
        case construct::Add: inlineOp = opcode::F64Add; break;
        case construct::Subtract: inlineOp = opcode::F64Sub; break;
        case construct::Multiply: inlineOp = opcode::F64Mul; break;
        case construct::Divide: inlineOp = opcode::F64Div; break;
        case construct::LessThan: inlineOp = opcode::F64Lt; relational = true; break;
        case construct::GreaterThan: inlineOp = opcode::F64Gt; relational = true; break;
        case construct::LessEqual: inlineOp = opcode::F64Le; relational = true; break;
        case construct::GreaterEqual: inlineOp = opcode::F64Ge; relational = true; break;
        case construct::StrictEqual: inlineOp = opcode::F64Eq; relational = true; break;
        case construct::StrictNotEqual: inlineOp = opcode::F64Ne; relational = true; break;
        case construct::Equal: inlineOp = opcode::F64Eq; relational = true; break;
        case construct::NotEqual: inlineOp = opcode::F64Ne; relational = true; break;
        default: break;
        }
        if (inlineOp)
        {
            get(lhs);
            constant(values::TagSpecial);
            op(opcode::I64LtU);
            get(rhs);
            constant(values::TagSpecial);
            op(opcode::I64LtU);
            op(opcode::I32And);
            op(opcode::If);
            iCode.push_back(0x7Eu);
            iControls.push_back(control{ control_kind::Block });
            get(lhs);
            op(opcode::F64ReinterpretI64);
            get(rhs);
            op(opcode::F64ReinterpretI64);
            op(*inlineOp);
            if (relational)
            {
                // (an i32: a boolean value)
                op(opcode::I64UConvertI32);
                constant(values::False);
                op(opcode::I64Add);
            }
            else
                op(opcode::I64ReinterpretF64);
            op(opcode::Else);
            get(lhs);
            get(rhs);
            runtime(sOperations.at(aNode->kind), { I64, I64 }, I64);
            check();
            end_control();
        }
        else
        {
            get(lhs);
            get(rhs);
            runtime(sOperations.at(aNode->kind), { I64, I64 }, I64);
            check();
        }
        release(rhs);
        release(lhs);
    }

    void function_generator::logical(node* aNode)
    {
        // a && b, a || b, a ?? b: the value of the operand that decides
        expression(aNode->child(0u));
        auto const value = temporary();
        tee(value);
        if (aNode->is(construct::Coalesce))
        {
            constant(values::Undefined);
            op(opcode::I64Eq);
            get(value);
            constant(values::Null);
            op(opcode::I64Eq);
            op(opcode::I32Ior);
        }
        else
        {
            runtime("to_boolean", { I64 }, I32);
            if (aNode->is(construct::Or))
                op(opcode::I32Eqz);
        }
        op(opcode::If);
        iCode.push_back(0x7Eu);
        iControls.push_back(control{ control_kind::Block });
        release(value);
        expression(aNode->child(1u));
        op(opcode::Else);
        get(value);
        end_control();
    }

    void function_generator::conditional(node* aNode)
    {
        condition(aNode->child(0u));
        op(opcode::If);
        iCode.push_back(0x7Eu);
        iControls.push_back(control{ control_kind::Block });
        expression(aNode->child(1u));
        op(opcode::Else);
        expression(aNode->child(2u));
        end_control();
    }

    bool function_generator::is_anonymous_function(node* aNode) const
    {
        while (aNode->is(construct::Parenthesized) && aNode->size() == 1u)
            aNode = aNode->child(0u);     // (a parenthesized function is one (IsFunctionDefinition of the cover grammar))
        switch (aNode->kind)
        {
        case construct::Arrow:
        case construct::AsyncArrow:
            return true;
        case construct::FunctionExpression:
        case construct::AsyncFunctionExpression:
        case construct::ClassExpression:
            return aNode->size() == 0u || !aNode->child(0u)->is(construct::Binding);
        default:
            return false;
        }
    }

    void function_generator::named_evaluation(node* aNode, std::string_view aName)
    {
        // (an anonymous function or class gets the name of what it is assigned to)
        if (is_anonymous_function(aNode))
        {
            while (aNode->is(construct::Parenthesized) && aNode->size() == 1u)
                aNode = aNode->child(0u);
            if (aNode->is(construct::ClassExpression))
            {
                class_expression(aNode, std::string{ aName });
                return;
            }
            expression(aNode);
            key(aName);
            runtime("name_function", { I64, I64 }, I64);
            return;
        }
        expression(aNode);
    }

    void function_generator::assignment(node* aNode)
    {
        auto* target = aNode->child(0u);
        auto* valueNode = aNode->child(1u);
        // (the operator: the text between the target and the value)
        auto const between = std::string_view{ target->source.data() + target->source.size(),
            static_cast<std::size_t>(valueNode->source.data() - (target->source.data() + target->source.size())) };
        std::string oper = operator_text(between);
        while (target->is(construct::Parenthesized) && (oper != "=" || (!target->child(0u)->is(construct::Object) && !target->child(0u)->is(construct::Array))))
            target = target->child(0u);
        // ((x) = function () {}: a parenthesized target is not an IdentifierRef: the function is not named)
        bool const covered = target != aNode->child(0u);
        auto const nameable = [&](node* aValue, std::string_view aName)
        {
            if (covered)
                expression(aValue);
            else
                named_evaluation(aValue, aName);
        };
        if (oper == "=")
        {
            if (target->is(construct::Object) || target->is(construct::Array))
            {
                expression(valueNode);
                auto const value = temporary();
                tee(value);
                assign_target(target);
                get(value);
                release(value);
                return;
            }
            if (target->is(construct::Identifier))
            {
                auto const r = resolve(name_of(target), target);
                if (iFunction.strict && (r.name == "eval" || r.name == "arguments"))
                    iCompiler.error(target->source, "Unexpected eval or arguments in strict mode");
                if (numeric_assignment(r, valueNode, "="))
                    return;
                if (r.kind == reference::Global && iFunction.strict)
                {
                    // (a strict assignment to an undeclared name: a ReferenceError, after the value is evaluated (but the binding is
                    // resolved first: global_set's flags (see global_resolved))
                    key(r.name);
                    runtime("global_resolved", { I64 }, I32);
                    auto const flags = local(I32);
                    set(flags);
                    nameable(valueNode, r.name);
                    auto const value = temporary();
                    set(value);
                    key(r.name);
                    get(value);
                    get(flags);
                    runtime("global_set", { I64, I64, I32 }, I64);
                    check_void();
                    get(value);
                    release(value);
                    return;
                }
                if (r.kind == reference::Dynamic)
                {
                    // (a name found by name: the reference is resolved before the value is evaluated (PutValue uses it))
                    auto const base = dynamic_reference(r);
                    nameable(valueNode, r.name);
                    auto const value = temporary();
                    set(value);
                    dynamic_reference_set(base, r, value);
                    get(value);
                    release(value);
                    release(base);
                    return;
                }
                nameable(valueNode, r.name);
                store(r, false, target);
                return;
            }
            if (target->is(construct::Dot) || target->is(construct::Index) || target->is(construct::DotPrivate))
            {
                expression(target->child(0u));
                auto const object = temporary();
                set(object);
                auto const keyValue = temporary();
                if (target->is(construct::Dot))
                    key(name_of(target->child(1u)));
                else if (target->is(construct::DotPrivate))
                    load(resolve(name_of(target->child(1u)), target));
                else
                    expression(target->child(1u));
                set(keyValue);
                expression(valueNode);
                auto const value = temporary();
                set(value);
                get(object);
                get(keyValue);
                get(value);
                if (target->is(construct::DotPrivate))
                    runtime("private_set", { I64, I64, I64 }, I64);
                else
                {
                    i32(iFunction.strict ? 1 : 0);
                    runtime(target->is(construct::Dot) ? "set_named" : "set", { I64, I64, I64, I32 }, I64);
                }
                check_void();
                get(value);
                release(value);
                release(keyValue);
                release(object);
                return;
            }
            if (target->is(construct::SuperDot) || target->is(construct::SuperIndex))
            {
                std::uint32_t thisValue = 0u, keyValue = 0u;
                auto const base = super_reference(target, thisValue, keyValue);
                expression(valueNode);
                auto const value = temporary();
                set(value);
                get(base);
                get(keyValue);
                get(value);
                get(thisValue);
                runtime(iFunction.strict ? "super_set" : "super_set_sloppy", { I64, I64, I64, I64 }, I64);
                check_void();
                get(value);
                release(value);
                release(base);
                release(keyValue);
                release(thisValue);
                return;
            }
            if (target->is(construct::Call) && !iFunction.strict)
            {
                // (an assignment to a call: the call then a ReferenceError (sloppy code))
                expression(target);
                drop();
                runtime("throw_invalid_assignment", {}, I64);
                check();
                return;
            }
            iCompiler.error(target->source, "Invalid left-hand side in assignment");
        }
        // compound assignment: op= (&&=, ||= and ??= only assign if the operand decides)
        bool const logicalAssignment = (oper == "&&=" || oper == "||=" || oper == "??=");
        static std::map<std::string, std::string> const sOperations = {
            { "**=", "exponent" }, { "*=", "multiply" }, { "/=", "divide" }, { "%=", "remainder" }, { "+=", "add" }, { "-=", "subtract" },
            { "<<=", "shift_left" }, { ">>=", "shift_right" }, { ">>>=", "shift_right_unsigned" }, { "&=", "bitwise_and" }, { "^=", "bitwise_xor" },
            { "|=", "bitwise_or" } };
        if (!logicalAssignment && !sOperations.contains(oper))
            iCompiler.error(aNode->source, "Invalid assignment operator '" + oper + "'");
        std::function<void()> readTarget;
        std::function<void(std::uint32_t)> writeTarget;
        std::optional<reference> r;
        std::uint32_t object = 0u, keyValue = 0u, thisValue = 0u;
        if (target->is(construct::Identifier))
        {
            r = resolve(name_of(target), target);
            if (iFunction.strict && (r->name == "eval" || r->name == "arguments"))
                iCompiler.error(target->source, "Unexpected eval or arguments in strict mode");
            if (numeric_assignment(*r, valueNode, oper))
                return;
            if (r->kind == reference::Dynamic)
            {
                object = dynamic_reference(*r);
                readTarget = [&]() { dynamic_reference_get(object, *r); };
                writeTarget = [&](std::uint32_t aValue) { dynamic_reference_set(object, *r, aValue); };
            }
            else
            {
                readTarget = [&]() { load(*r); };
                writeTarget = [&](std::uint32_t aValue) { get(aValue); store(*r, false, target); drop(); };
            }
        }
        else if (target->is(construct::Dot) || target->is(construct::Index) || target->is(construct::DotPrivate))
        {
            expression(target->child(0u));
            object = temporary();
            set(object);
            if (target->is(construct::Dot))
                key(name_of(target->child(1u)));
            else if (target->is(construct::DotPrivate))
                load(resolve(name_of(target->child(1u)), target));
            else
            {
                expression(target->child(1u));
                get(object);
                runtime("require_object_coercible", { I64 }, I64);
                check_void();
                runtime("to_property_key", { I64 }, I64);
                check();
            }
            keyValue = temporary();
            set(keyValue);
            readTarget = [&]()
            {
                get(object);
                get(keyValue);
                if (target->is(construct::DotPrivate))
                    runtime("private_get", { I64, I64 }, I64);
                else
                    runtime("get", { I64, I64 }, I64);
                check();
            };
            writeTarget = [&](std::uint32_t aValue)
            {
                get(object);
                get(keyValue);
                get(aValue);
                if (target->is(construct::DotPrivate))
                    runtime("private_set", { I64, I64, I64 }, I64);
                else
                {
                    i32(iFunction.strict ? 1 : 0);
                    runtime("set", { I64, I64, I64, I32 }, I64);
                }
                check_void();
            };
        }
        else if (target->is(construct::SuperDot) || target->is(construct::SuperIndex))
        {
            // (the key is made a property key once (by GetValue))
            object = super_reference(target, thisValue, keyValue);
            if (target->is(construct::SuperIndex))
            {
                get(keyValue);
                runtime("to_property_key", { I64 }, I64);
                check();
                set(keyValue);
            }
            readTarget = [&]()
            {
                get(object);
                get(keyValue);
                get(thisValue);
                runtime("super_get", { I64, I64, I64 }, I64);
                check();
            };
            writeTarget = [&](std::uint32_t aValue)
            {
                get(object);
                get(keyValue);
                get(aValue);
                get(thisValue);
                runtime(iFunction.strict ? "super_set" : "super_set_sloppy", { I64, I64, I64, I64 }, I64);
                check_void();
            };
        }
        else if (target->is(construct::Call) && !iFunction.strict && !logicalAssignment)
        {
            expression(target);
            drop();
            runtime("throw_invalid_assignment", {}, I64);
            check();
            return;
        }
        else
            iCompiler.error(target->source, "Invalid left-hand side in assignment");
        readTarget();
        auto const current = temporary();
        set(current);
        if (logicalAssignment)
        {
            // (the condition: whether to assign)
            get(current);
            if (oper == "??=")
            {
                constant(values::Undefined);
                op(opcode::I64Eq);
                get(current);
                constant(values::Null);
                op(opcode::I64Eq);
                op(opcode::I32Ior);
            }
            else
            {
                runtime("to_boolean", { I64 }, I32);
                if (oper == "||=")
                    op(opcode::I32Eqz);
            }
            op(opcode::If);
            iCode.push_back(0x7Eu);
            iControls.push_back(control{ control_kind::Block });
            if (target->is(construct::Identifier))
                nameable(valueNode, r->name);
            else
                expression(valueNode);
            auto const value = temporary();
            set(value);
            writeTarget(value);
            get(value);
            release(value);
            op(opcode::Else);
            get(current);
            end_control();
        }
        else
        {
            expression(valueNode);
            auto const rhs = temporary();
            set(rhs);
            get(current);
            get(rhs);
            runtime(sOperations.at(oper), { I64, I64 }, I64);
            check();
            auto const value = temporary();
            set(value);
            writeTarget(value);
            get(value);
            release(value);
            release(rhs);
        }
        release(current);
    }

    std::uint32_t function_generator::dynamic_reference(reference const& aReference)
    {
        // (a name found by name: its reference's base (see the runtime's dyn_reference))
        environment_value();
        key(aReference.name);
        runtime("dyn_reference", { I64, I64 }, I64);
        check();
        auto const base = temporary();
        set(base);
        return base;
    }

    void function_generator::dynamic_reference_get(std::uint32_t aBase, reference const& aReference)
    {
        get(aBase);
        key(aReference.name);
        i32(iFunction.strict ? 1 : 0);
        runtime("dyn_reference_get", { I64, I64, I32 }, I64);
        check();
    }

    void function_generator::dynamic_reference_set(std::uint32_t aBase, reference const& aReference, std::uint32_t aValue)
    {
        get(aBase);
        key(aReference.name);
        get(aValue);
        i32(iFunction.strict ? 1 : 0);
        runtime("dyn_reference_set", { I64, I64, I64, I32 }, I64);
        check_void();
    }

    void function_generator::update(node* aNode)
    {
        // ++x, --x, x++, x--: ToNumeric, then the value plus or minus one is assigned
        bool const increment = aNode->is(construct::PreIncrement) || aNode->is(construct::PostIncrement);
        bool const postfix = aNode->is(construct::PostIncrement) || aNode->is(construct::PostDecrement);
        auto* target = aNode->child(0u);
        while (target->is(construct::Parenthesized))
            target = target->child(0u);
        auto const old = temporary();
        auto const updated = temporary();
        auto const compute = [&]()
        {
            runtime("to_numeric", { I64 }, I64);
            check();
            tee(old);
            runtime(increment ? "increment" : "decrement", { I64 }, I64);
            check();
            set(updated);
        };
        if (target->is(construct::Identifier))
        {
            auto const r = resolve(name_of(target), target);
            if (iFunction.strict && (r.name == "eval" || r.name == "arguments"))
                iCompiler.error(target->source, "Unexpected eval or arguments in strict mode");
            if (numeric_update(r, increment, postfix))
            {
                release(updated);
                release(old);
                return;
            }
            if (r.kind == reference::Dynamic)
            {
                auto const base = dynamic_reference(r);
                dynamic_reference_get(base, r);
                compute();
                dynamic_reference_set(base, r, updated);
                release(base);
            }
            else
            {
                load(r);
                compute();
                get(updated);
                store(r, false, target);
                drop();
            }
        }
        else if (target->is(construct::Dot) || target->is(construct::Index) || target->is(construct::DotPrivate))
        {
            expression(target->child(0u));
            auto const object = temporary();
            set(object);
            if (target->is(construct::Dot))
                key(name_of(target->child(1u)));
            else if (target->is(construct::DotPrivate))
                load(resolve(name_of(target->child(1u)), target));
            else
            {
                expression(target->child(1u));
                get(object);
                runtime("require_object_coercible", { I64 }, I64);
                check_void();
                runtime("to_property_key", { I64 }, I64);
                check();
            }
            auto const keyValue = temporary();
            set(keyValue);
            get(object);
            get(keyValue);
            runtime(target->is(construct::DotPrivate) ? "private_get" : "get", { I64, I64 }, I64);
            check();
            compute();
            get(object);
            get(keyValue);
            get(updated);
            if (target->is(construct::DotPrivate))
                runtime("private_set", { I64, I64, I64 }, I64);
            else
            {
                i32(iFunction.strict ? 1 : 0);
                runtime("set", { I64, I64, I64, I32 }, I64);
            }
            check_void();
            release(keyValue);
            release(object);
        }
        else if (target->is(construct::SuperDot) || target->is(construct::SuperIndex))
        {
            std::uint32_t thisValue = 0u, keyValue = 0u;
            auto const base = super_reference(target, thisValue, keyValue);
            if (target->is(construct::SuperIndex))
            {
                // (the key is made a property key once (by GetValue))
                get(keyValue);
                runtime("to_property_key", { I64 }, I64);
                check();
                set(keyValue);
            }
            get(base);
            get(keyValue);
            get(thisValue);
            runtime("super_get", { I64, I64, I64 }, I64);
            check();
            compute();
            get(base);
            get(keyValue);
            get(updated);
            get(thisValue);
            runtime(iFunction.strict ? "super_set" : "super_set_sloppy", { I64, I64, I64, I64 }, I64);
            check_void();
            release(base);
            release(keyValue);
            release(thisValue);
        }
        else if (target->is(construct::Call) && !iFunction.strict)
        {
            expression(target);
            drop();
            runtime("throw_invalid_assignment", {}, I64);
            check();
            release(updated);
            release(old);
            return;
        }
        else
            iCompiler.error(target->source, "Invalid left-hand side expression in " + std::string{ postfix ? "postfix" : "prefix" } + " operation");
        get(postfix ? old : updated);
        release(updated);
        release(old);
    }

    void function_generator::array_literal(node* aNode)
    {
        i32(static_cast<std::int32_t>(aNode->size()));
        runtime("array", { I32 }, I64);
        auto const array = temporary();
        set(array);
        for (auto* e : aNode->children)
        {
            get(array);
            if (e->is(construct::Hole))
            {
                runtime("array_hole", { I64 }, I64);
                drop();
                continue;
            }
            if (e->is(construct::Spread))
            {
                expression(e->child(0u));
                runtime("array_spread", { I64, I64 }, I64);
                check_void();
                continue;
            }
            expression(e);
            runtime("array_push", { I64, I64 }, I64);
            drop();
        }
        // (a trailing hole is not an element: "[a, ,]" has two elements (the grammar makes the last comma a hole))
        get(array);
        release(array);
    }

    void function_generator::property_key(node* aNode)
    {
        switch (aNode->kind)
        {
        case construct::Name:
            key(name_of(aNode));
            break;
        case construct::String:
            literal(aNode);
            break;
        case construct::Number:
            literal(aNode);
            runtime("to_property_key", { I64 }, I64);
            check();
            break;
        case construct::Computed:
            expression(aNode->child(0u));
            runtime("to_property_key", { I64 }, I64);
            check();
            break;
        case construct::Private:
            load(resolve(name_of(aNode), aNode));
            break;
        default:
            iCompiler.error(aNode->source, "property name expected");
        }
    }

    void function_generator::object_literal(node* aNode)
    {
        runtime("object", {}, I64);
        auto const object = temporary();
        set(object);
        for (auto* p : aNode->children)
        {
            switch (p->kind)
            {
            case construct::Property:
                {
                    auto* keyNode = p->child(0u);
                    // (__proto__: value sets the prototype)
                    if ((keyNode->is(construct::Name) && keyNode->source == "__proto__") ||
                        (keyNode->is(construct::String) && (keyNode->source == "\"__proto__\"" || keyNode->source == "'__proto__'")))
                    {
                        get(object);
                        expression(p->child(1u));
                        runtime("object_proto", { I64, I64 }, I64);
                        drop();
                        break;
                    }
                    get(object);
                    property_key(keyNode);
                    auto const k = temporary();
                    tee(k);
                    drop();
                    get(k);
                    if (is_anonymous_function(p->child(1u)))
                    {
                        expression(p->child(1u));
                        get(k);
                        runtime("name_function", { I64, I64 }, I64);
                    }
                    else
                        expression(p->child(1u));
                    runtime("object_define", { I64, I64, I64 }, I64);
                    check_void();
                    release(k);
                }
                break;
            case construct::Shorthand:
                get(object);
                key(name_of(p->child(0u)));
                identifier(p->child(0u));
                runtime("object_define", { I64, I64, I64 }, I64);
                check_void();
                break;
            case construct::ShorthandDefault:
                iCompiler.error(p->source, "Invalid shorthand property initializer");
            case construct::Spread:
                get(object);
                expression(p->child(0u));
                runtime("object_spread", { I64, I64 }, I64);
                check_void();
                break;
            case construct::Method:
            case construct::Getter:
            case construct::Setter:
            case construct::GeneratorMethod:
            case construct::AsyncMethod:
            case construct::AsyncGeneratorMethod:
                {
                    get(object);
                    property_key(p->child(0u));
                    auto const k = temporary();
                    set(k);
                    get(k);
                    closure(*p->function);
                    i32(p->is(construct::Getter) ? 1 : p->is(construct::Setter) ? 2 : 0);
                    i32(1);
                    runtime("define_method", { I64, I64, I64, I32, I32 }, I64);
                    check_void();
                    release(k);
                }
                break;
            default:
                iCompiler.error(p->source, "unsupported property definition");
            }
        }
        get(object);
        release(object);
    }

    void function_generator::function_expression(node* aNode)
    {
        auto& f = *aNode->function;
        if (f.calleeBinding != nullptr && f.calleeBinding->captured)
        {
            // (a named function expression whose name an inner function uses: the name's environment)
            auto* s = f.calleeBinding->owner;
            enter_scope(s);
            closure(f);
            auto const value = temporary();
            tee(value);
            initialize_binding(f.calleeBinding);
            leave_scope(s);
            get(value);
            release(value);
            // (the function's environment is the name's)
            return;
        }
        closure(f);
    }

    // --- patterns (destructuring)

    void function_generator::bind_pattern(node* aTarget, bool aInitialize, binding_kind aKind)
    {
        (void)aKind;
        switch (aTarget->kind)
        {
        case construct::Binding:
            {
                auto const r = resolve(name_of(aTarget), aTarget);
                store(r, aInitialize, aTarget);
                drop();
            }
            break;
        case construct::Initializer:
            {
                // (a default: used if the value is undefined)
                auto const value = temporary();
                tee(value);
                constant(values::Undefined);
                op(opcode::I64Eq);
                op(opcode::If);
                iCode.push_back(0x40u);
                iControls.push_back(control{ control_kind::Block });
                if (aTarget->child(0u)->is(construct::Binding))
                    named_evaluation(aTarget->child(1u), name_of(aTarget->child(0u)));
                else
                    expression(aTarget->child(1u));
                set(value);
                end_control();
                get(value);
                release(value);
                bind_pattern(aTarget->child(0u), aInitialize, aKind);
            }
            break;
        case construct::ObjectPattern:
            destructure_object(aTarget, true, aInitialize);
            break;
        case construct::ArrayPattern:
            destructure_array(aTarget, true, aInitialize);
            break;
        default:
            iCompiler.error(aTarget->source, "invalid binding target");
        }
    }

    void function_generator::bind_element(node* aTarget, bool aBinding, bool aInitialize)
    {
        if (aBinding)
            bind_pattern(aTarget, aInitialize, binding_kind::Let);
        else
            assign_target(aTarget);
    }

    function_generator::prepared_reference function_generator::prepare_reference(node* aElement, bool aBinding)
    {
        // (a destructuring assignment's element whose target is a property reference: the reference is evaluated (its object and key)
        // before the element's value is taken; likewise a name found by name (e.g. in a with statement): it is resolved first (also a
        // var binding's (KeyedBindingInitialization's ResolveBinding)))
        prepared_reference result;
        auto* t = aElement;
        if (t->is(aBinding ? construct::Initializer : construct::Assign))
            t = t->child(0u);
        while (t->is(construct::Parenthesized))
            t = t->child(0u);
        result.target = t;
        if (t->is(aBinding ? construct::Binding : construct::Identifier))
        {
            if (auto const r = resolve(name_of(t), t); r.kind == reference::Dynamic && !r.name.starts_with("#"))
            {
                result.object = dynamic_reference(r);
                result.dynamic = r;
                result.prepared = true;
            }
            return result;
        }
        if (aBinding)
            return result;
        if (t->is(construct::Dot) || t->is(construct::Index) || t->is(construct::DotPrivate))
        {
            expression(t->child(0u));
            result.object = temporary();
            set(result.object);
            if (t->is(construct::Dot))
                key(name_of(t->child(1u)));
            else if (t->is(construct::DotPrivate))
                load(resolve(name_of(t->child(1u)), t));
            else
                expression(t->child(1u));
            result.keyValue = temporary();
            set(result.keyValue);
            result.prepared = true;
        }
        else if (t->is(construct::SuperDot) || t->is(construct::SuperIndex))
        {
            result.superBase = super_reference(t, result.object, result.keyValue);
            result.prepared = true;
        }
        return result;
    }

    void function_generator::assign_prepared(prepared_reference const& aReference, node* aElement)
    {
        // (the value on the stack: assigned to the element (its default if it is undefined) through its prepared reference)
        if (!aReference.prepared)
        {
            assign_target(aElement);
            return;
        }
        auto const value = temporary();
        set(value);
        if (aElement->is(construct::Assign) || aElement->is(construct::Initializer))
        {
            get(value);
            constant(values::Undefined);
            op(opcode::I64Eq);
            op(opcode::If);
            iCode.push_back(0x40u);
            iControls.push_back(control{ control_kind::Block });
            if (aReference.dynamic)
                named_evaluation(aElement->child(1u), aReference.dynamic->name);
            else
                expression(aElement->child(1u));
            set(value);
            end_control();
        }
        auto* t = aReference.target;
        if (aReference.dynamic)
        {
            dynamic_reference_set(aReference.object, *aReference.dynamic, value);
            release(value);
            release(aReference.object);
            return;
        }
        if (t->is(construct::SuperDot) || t->is(construct::SuperIndex))
        {
            get(aReference.superBase);
            get(aReference.keyValue);
            get(value);
            get(aReference.object);
            runtime(iFunction.strict ? "super_set" : "super_set_sloppy", { I64, I64, I64, I64 }, I64);
            check_void();
            release(aReference.superBase);
        }
        else
        {
            get(aReference.object);
            get(aReference.keyValue);
            get(value);
            if (t->is(construct::DotPrivate))
                runtime("private_set", { I64, I64, I64 }, I64);
            else
            {
                i32(iFunction.strict ? 1 : 0);
                runtime(t->is(construct::Dot) ? "set_named" : "set", { I64, I64, I64, I32 }, I64);
            }
            check_void();
        }
        release(value);
        release(aReference.keyValue);
        release(aReference.object);
    }

    void function_generator::assign_target(node* aTarget)
    {
        // (an assignment pattern (an object or array literal), or an assignment target expression; the value on the stack)
        while (aTarget->is(construct::Parenthesized))
            aTarget = aTarget->child(0u);
        switch (aTarget->kind)
        {
        case construct::Identifier:
            {
                auto const r = resolve(name_of(aTarget), aTarget);
                if (iFunction.strict && (r.name == "eval" || r.name == "arguments"))
                    iCompiler.error(aTarget->source, "Unexpected eval or arguments in strict mode");
                store(r, false, aTarget);
                drop();
            }
            break;
        case construct::Dot:
        case construct::Index:
        case construct::DotPrivate:
            {
                auto const value = temporary();
                set(value);
                expression(aTarget->child(0u));
                auto const object = temporary();
                set(object);
                if (aTarget->is(construct::Dot))
                    key(name_of(aTarget->child(1u)));
                else if (aTarget->is(construct::DotPrivate))
                    load(resolve(name_of(aTarget->child(1u)), aTarget));
                else
                    expression(aTarget->child(1u));
                auto const keyValue = temporary();
                set(keyValue);
                get(object);
                get(keyValue);
                get(value);
                if (aTarget->is(construct::DotPrivate))
                    runtime("private_set", { I64, I64, I64 }, I64);
                else
                {
                    i32(iFunction.strict ? 1 : 0);
                    runtime(aTarget->is(construct::Dot) ? "set_named" : "set", { I64, I64, I64, I32 }, I64);
                }
                check_void();
                release(keyValue);
                release(object);
                release(value);
            }
            break;
        case construct::SuperDot:
        case construct::SuperIndex:
            {
                auto const value = temporary();
                set(value);
                std::uint32_t thisValue = 0u, keyValue = 0u;
                auto const base = super_reference(aTarget, thisValue, keyValue);
                get(base);
                get(keyValue);
                get(value);
                get(thisValue);
                runtime(iFunction.strict ? "super_set" : "super_set_sloppy", { I64, I64, I64, I64 }, I64);
                check_void();
                release(base);
                release(keyValue);
                release(thisValue);
                release(value);
            }
            break;
        case construct::Object:
            destructure_object(aTarget, false, false);
            break;
        case construct::Array:
            destructure_array(aTarget, false, false);
            break;
        case construct::Assign:
            {
                // (a target with a default: target = default)
                auto const value = temporary();
                tee(value);
                constant(values::Undefined);
                op(opcode::I64Eq);
                op(opcode::If);
                iCode.push_back(0x40u);
                iControls.push_back(control{ control_kind::Block });
                auto* inner = aTarget->child(0u);
                if (inner->is(construct::Identifier))
                    named_evaluation(aTarget->child(1u), name_of(inner));
                else
                    expression(aTarget->child(1u));
                set(value);
                end_control();
                get(value);
                release(value);
                assign_target(inner);
            }
            break;
        case construct::Call:
            if (!iFunction.strict)
            {
                // (a call as a for-in/of head's target (sloppy code, web compatibility): the call, then a ReferenceError)
                drop();
                expression(aTarget);
                drop();
                runtime("throw_invalid_assignment", {}, I64);
                check_void();
                break;
            }
            iCompiler.error(aTarget->source, "Invalid destructuring assignment target");
            break;
        default:
            iCompiler.error(aTarget->source, "Invalid destructuring assignment target");
        }
    }

    void function_generator::destructure_object(node* aPattern, bool aBinding, bool aInitialize)
    {
        // ({ a, b: c, [k]: d = 1, ...rest } = value): the value must not be null or undefined
        auto const source = temporary();
        set(source);
        get(source);
        runtime("require_object_coercible", { I64 }, I64);
        check_void();
        std::vector<std::uint32_t> keys;
        bool const hasRest = !aPattern->children.empty() && (aPattern->children.back()->is(construct::Rest) || aPattern->children.back()->is(construct::Spread));
        for (auto* p : aPattern->children)
        {
            if (p->is(construct::Rest) || p->is(construct::Spread))
            {
                // (the rest: a copy of the source without the keys taken)
                i32(static_cast<std::int32_t>(keys.size()));
                runtime("array", { I32 }, I64);
                auto const excluded = temporary();
                set(excluded);
                for (auto k : keys)
                {
                    get(excluded);
                    get(k);
                    runtime("array_push", { I64, I64 }, I64);
                    drop();
                }
                auto const reference = aBinding && aInitialize ? prepared_reference{} : prepare_reference(p->child(0u), aBinding);
                get(source);
                get(excluded);
                runtime("object_rest", { I64, I64 }, I64);
                check();
                release(excluded);
                if (aBinding && !reference.prepared)
                    bind_element(p->child(0u), aBinding, aInitialize);
                else
                    assign_prepared(reference, p->child(0u));
                continue;
            }
            node* keyNode = nullptr;
            node* target = nullptr;
            switch (p->kind)
            {
            case construct::PatternProperty:
            case construct::Property:
                keyNode = p->child(0u);
                target = p->child(1u);
                break;
            case construct::Binding:
            case construct::Initializer:
                keyNode = p->is(construct::Binding) ? p : p->child(0u);
                target = p;
                break;
            case construct::Shorthand:
                keyNode = p->child(0u);
                target = p->child(0u);
                break;
            case construct::ShorthandDefault:
                {
                    // ({ a = 1 } = value)
                    auto const reference = prepare_reference(p->child(0u));
                    key(name_of(p->child(0u)));
                    auto const k = temporary();
                    tee(k);
                    if (hasRest)
                        keys.push_back(k);
                    drop();
                    get(source);
                    get(k);
                    runtime("get", { I64, I64 }, I64);
                    check();
                    auto const value = temporary();
                    tee(value);
                    constant(values::Undefined);
                    op(opcode::I64Eq);
                    op(opcode::If);
                    iCode.push_back(0x40u);
                    iControls.push_back(control{ control_kind::Block });
                    named_evaluation(p->child(1u), name_of(p->child(0u)));
                    set(value);
                    end_control();
                    get(value);
                    release(value);
                    assign_prepared(reference, p->child(0u));
                    if (!hasRest)
                        release(k);
                }
                continue;
            default:
                iCompiler.error(p->source, "Invalid destructuring target");
            }
            if (keyNode->is(construct::Binding) || keyNode->is(construct::Identifier))
                key(name_of(keyNode));
            else
                property_key(keyNode);
            auto const k = temporary();
            tee(k);
            drop();
            if (hasRest)
                keys.push_back(k);
            auto const reference = aBinding && aInitialize ? prepared_reference{} : prepare_reference(target, aBinding);
            get(source);
            get(k);
            runtime("get", { I64, I64 }, I64);
            check();
            if (aBinding && !reference.prepared)
                bind_element(target, aBinding, aInitialize);
            else
                assign_prepared(reference, target);
            if (!hasRest)
                release(k);
        }
        for (auto k : keys)
            release(k);
        release(source);
    }

    void function_generator::destructure_array(node* aPattern, bool aBinding, bool aInitialize)
    {
        // ([a, , b = 1, ...rest] = value): the value's iterator (closed unless it is done)
        i32(0);
        runtime("iterator", { I64, I32 }, I64);
        check();
        auto const iterator = local();
        set(iterator);
        begin(control_kind::Finally);
        auto const cleanup = iControls.size() - 1u;
        iControls.back().completionLocal = local(I32);
        iControls.back().valueLocal = local();
        iControls.back().iteratorLocal = iterator;
        i32(CompletionNormal);
        set(iControls.back().completionLocal);
        begin(control_kind::Handler);
        for (auto* e : aPattern->children)
        {
            if (e->is(construct::Rest) || e->is(construct::Spread))
            {
                auto const reference = aBinding && aInitialize ? prepared_reference{} : prepare_reference(e->child(0u), aBinding);
                get(iterator);
                runtime("iterator_rest", { I64 }, I64);
                check();
                if (aBinding && !reference.prepared)
                    bind_element(e->child(0u), aBinding, aInitialize);
                else
                    assign_prepared(reference, e->child(0u));
                continue;
            }
            auto const reference = ((aBinding && aInitialize) || e->is(construct::Hole)) ? prepared_reference{} : prepare_reference(e, aBinding);
            get(iterator);
            runtime("iterator_value", { I64 }, I64);
            check();
            if (e->is(construct::Hole))
            {
                drop();
                continue;
            }
            if (aBinding && !reference.prepared)
                bind_element(e, aBinding, aInitialize);
            else
                assign_prepared(reference, e);
        }
        branch(cleanup);
        end_control();
        // (an exception (not the iterator's own): the iterator is closed and the exception propagates)
        get(iterator);
        runtime("iterator_close_throw", { I64 }, I64);
        drop();
        constant(values::Exception);
        check_void();
        auto jumps = std::move(iControls.back().jumps);
        auto const completion = iControls.back().completionLocal;
        auto const valueLocal = iControls.back().valueLocal;
        end_control();
        // (done with it: closed if it is not done)
        get(iterator);
        runtime("iterator_close_if_open", { I64 }, I64);
        check_void();
        for (std::size_t j = 0u; j < jumps.size(); ++j)
        {
            get(completion);
            i32(static_cast<std::int32_t>(CompletionJumps + j));
            op(opcode::I32Eq);
            op(opcode::If);
            iCode.push_back(0x40u);
            iControls.push_back(control{ control_kind::Block });
            jumps[j]();
            end_control();
        }
        get(completion);
        i32(CompletionReturn);
        op(opcode::I32Eq);
        op(opcode::If);
        iCode.push_back(0x40u);
        iControls.push_back(control{ control_kind::Block });
        get(valueLocal);
        complete_return();
        end_control();
    }

    // --- not yet supported

    namespace
    {
        bool is_class_method(node const* aNode)
        {
            return aNode->is(construct::Method) || aNode->is(construct::Getter) || aNode->is(construct::Setter) || aNode->is(construct::GeneratorMethod) ||
                aNode->is(construct::AsyncMethod) || aNode->is(construct::AsyncGeneratorMethod);
        }
    }

    bool is_class_constructor_method(node const* aNode)
    {
        // (a class's constructor: a (non-static) plain method named constructor (not computed))
        if (!aNode->is(construct::Method))
            return false;
        auto const* key = aNode->child(0u);
        if (key->is(construct::Name))
            return key->source == "constructor";
        if (key->is(construct::String))
            return key->source == "\"constructor\"" || key->source == "'constructor'";
        return false;
    }

    void function_generator::class_expression(node* aNode, std::optional<std::string> aName)
    {
        // ClassDefinitionEvaluation: the class's scope (its name and private names); its heritage; its prototype and constructor; its
        // elements (methods are defined; fields and static blocks are recorded); its static elements run when it is complete
        auto* classScope = aNode->ownScope;
        node* nameNode = (aNode->size() > 0u && aNode->child(0u)->is(construct::Binding)) ? aNode->child(0u) : nullptr;
        node* heritage = nullptr;
        node* constructor = nullptr;
        for (auto* c : aNode->children)
        {
            if (c->is(construct::Extends))
                heritage = c;
            else if (is_class_constructor_method(c))
                constructor = c;
        }
        enter_scope(classScope);
        for (auto& b : classScope->bindings)
            if (!b->name.empty() && b->name[0] == '#')
            {
                key(b->name);
                runtime("private_name", { I64 }, I64);
                check();
                initialize_binding(b.get());
            }
        if (heritage != nullptr)
            expression(heritage->child(0u));
        else
            constant(values::Empty);
        auto const parent = temporary();
        set(parent);
        get(parent);
        runtime("class_prototype", { I64 }, I64);
        check();
        auto const prototype = temporary();
        set(prototype);
        if (constructor != nullptr)
            closure(*constructor->function);
        else
            undefined();
        get(prototype);
        get(parent);
        key(nameNode != nullptr ? name_of(nameNode) : aName ? *aName : std::string{});
        string_value(utf8_encoding(aNode->source));
        runtime("class_constructor", { I64, I64, I64, I64, I64 }, I64);
        check();
        auto const f = temporary();
        set(f);
        for (auto* c : aNode->children)
        {
            if (c == nameNode || c == heritage || c == constructor)
                continue;
            bool const isStatic = c->is(construct::Static) || c->is(construct::StaticBlock);
            auto* e = c->is(construct::Static) ? c->child(0u) : c;
            auto const home = isStatic ? f : prototype;
            if (is_class_method(e))
            {
                std::int32_t const kind = e->is(construct::Getter) ? 1 : e->is(construct::Setter) ? 2 : 0;
                if (e->child(0u)->is(construct::Private))
                {
                    get(f);
                    get(home);
                    property_key(e->child(0u));
                    closure(*e->function);
                    i32(kind);
                    i32(isStatic ? 1 : 0);
                    runtime("class_method", { I64, I64, I64, I64, I32, I32 }, I64);
                }
                else
                {
                    get(home);
                    property_key(e->child(0u));
                    closure(*e->function);
                    i32(kind);
                    i32(0);
                    runtime("define_method", { I64, I64, I64, I32, I32 }, I64);
                }
                check_void();
            }
            else if (e->is(construct::Field))
            {
                get(f);
                get(home);
                property_key(e->child(0u));
                if (e->function != nullptr)
                    closure(*e->function);
                else
                    undefined();
                i32(isStatic ? 1 : 0);
                runtime("class_field", { I64, I64, I64, I64, I32 }, I64);
                check_void();
            }
            else if (e->is(construct::StaticBlock))
            {
                get(f);
                closure(*e->function);
                runtime("class_static_block", { I64, I64 }, I64);
                check_void();
            }
        }
        if (nameNode != nullptr)
        {
            if (auto* b = classScope->find(name_of(nameNode)))
            {
                get(f);
                initialize_binding(b);
            }
        }
        get(f);
        runtime("class_finish", { I64 }, I64);
        check();
        leave_scope(classScope);
        release(f);
        release(prototype);
        release(parent);
    }

    void function_generator::yield_expression(node* aNode)
    {
        // yield, yield value and yield* iterable: the runtime suspends the generator's coroutine; it returns the value sent in (or
        // Exception if one was thrown in, or Return if the generator was resumed to return (its return value is then the runtime's
        // resume_value): the return runs the finally blocks between it and the function's body)
        bool delegate = false;
        {
            auto text = aNode->source.substr(5u);
            std::size_t i = 0u;
            while (i < text.size())
            {
                if (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r' || text[i] == '\v' || text[i] == '\f' ||
                    static_cast<unsigned char>(text[i]) >= 0x80u)
                    ++i;
                else if (text.substr(i, 2u) == "//")
                {
                    while (i < text.size() && text[i] != '\n')
                        ++i;
                }
                else if (text.substr(i, 2u) == "/*")
                {
                    auto const end = text.find("*/", i + 2u);
                    i = end == std::string_view::npos ? text.size() : end + 2u;
                }
                else
                    break;
            }
            delegate = i < text.size() && text[i] == '*';
        }
        if (aNode->size() > 0u)
            expression(aNode->child(0u));
        else
            undefined();
        if (delegate)
            runtime("yield_star", { I64 }, I64);
        else
            runtime("yield", { I64 }, I64);
        resumption();
    }

    void function_generator::resumption()
    {
        // (the result of a suspension: Exception (thrown in) is handled; Return (resumed to return) returns)
        check();
        constant(values::Return);
        op(opcode::I64Eq);
        op(opcode::If);
        iCode.push_back(0x40u);
        iControls.push_back(control{ control_kind::Block });
        runtime("resume_value", {}, I64);
        complete_return();
        end_control();
        get(iScratch);
    }

    void function_generator::await_value()
    {
        runtime("await", { I64 }, I64);
        check();
    }

    void function_generator::set_home_object(std::uint32_t aFunction, std::uint32_t aHome)
    {
        get(aFunction);
        get(aHome);
        runtime("set_home", { I64, I64 }, I64);
        drop();
    }

    void function_generator::expression_list(node* aNode)
    {
        expression(aNode);
    }

    void function_generator::declarations(node* aNode, bool aInitialize)
    {
        (void)aNode;
        (void)aInitialize;
    }

    void function_generator::initialize_scope(scope* aScope, node* aStatements)
    {
        (void)aScope;
        (void)aStatements;
    }

    // --- numeric locals
    //
    // A local binding (not captured, not in a scope whose bindings are found by name) that is declared once with a numeric initializer, that
    // is only assigned numbers and that is not used before its declaration (so it has no TDZ) is a WebAssembly f64 local; one that is
    // initialized with a small integer and only counts (++, --, += and -= of small integers) is an i64 local (exact until 2^53, then
    // as a double's rounding would be). Expressions of numbers (literals, such locals and the arithmetic of numbers) are computed as f64s.

    namespace
    {
        // (the operator in a source between two operands: its punctuators (not white space, Unicode's included, or comments))
        std::string operator_text(std::string_view aBetween)
        {
            std::string result;
            for (std::size_t i = 0u; i < aBetween.size(); ++i)
            {
                if (aBetween.compare(i, 2u, "/*") == 0)
                {
                    auto const e = aBetween.find("*/", i + 2u);
                    i = e == std::string_view::npos ? aBetween.size() : e + 1u;
                    continue;
                }
                if (aBetween.compare(i, 2u, "//") == 0)
                {
                    while (i < aBetween.size() && aBetween[i] != '\n' && aBetween[i] != '\r' &&
                        !(aBetween.compare(i, 3u, "\xE2\x80\xA8") == 0 || aBetween.compare(i, 3u, "\xE2\x80\xA9") == 0))
                        ++i;
                    --i;
                    continue;
                }
                if (std::string_view{ "*/%+-<>&^|=?!" }.find(aBetween[i]) != std::string_view::npos)
                    result.push_back(aBetween[i]);
            }
            return result;
        }

        // (an assignment's operator)
        std::string assignment_operator(node const* aNode)
        {
            auto const* target = aNode->child(0u);
            auto const* valueNode = aNode->child(1u);
            return operator_text(std::string_view{ target->source.data() + target->source.size(),
                static_cast<std::size_t>(valueNode->source.data() - (target->source.data() + target->source.size())) });
        }

        // (a numeric literal's value, if it is one (not legacy octal))
        std::optional<double> literal_number(node const* aNode)
        {
            while (aNode->is(construct::Parenthesized) && aNode->size() == 1u)
                aNode = aNode->child(0u);
            if (!aNode->is(construct::Number))
                return {};
            bool legacy = false;
            auto const n = number_value(aNode->source, legacy);
            if (legacy)
                return {};
            return n;
        }

        bool small_integer(std::optional<double> const& aValue)
        {
            return aValue && *aValue == std::trunc(*aValue) && std::fabs(*aValue) <= 2147483648.0 && !(*aValue == 0.0 && std::signbit(*aValue));
        }

        node const* unparenthesized(node const* aNode)
        {
            while (aNode->is(construct::Parenthesized) && aNode->size() == 1u)
                aNode = aNode->child(0u);
            return aNode;
        }

        constexpr double SafeInteger = 9007199254740992.0;  // (2^53)
    }

    numeric_rep function_generator::rep_of(binding const* aBinding) const
    {
        if (aBinding == nullptr)
            return numeric_rep::None;
        auto const existing = iNumeric.find(aBinding);
        return existing != iNumeric.end() ? existing->second : numeric_rep::None;
    }

    namespace
    {
        bool integral(numeric_rep aRep)
        {
            return aRep == numeric_rep::I32 || aRep == numeric_rep::I64;
        }

        // (the representation of the values of both (None if either is None))
        numeric_rep join(numeric_rep aLeft, numeric_rep aRight)
        {
            if (aLeft == numeric_rep::None || aRight == numeric_rep::None)
                return numeric_rep::None;
            if (aLeft == numeric_rep::F64 || aRight == numeric_rep::F64)
                return numeric_rep::F64;
            if (aLeft == numeric_rep::I64 || aRight == numeric_rep::I64)
                return numeric_rep::I64;
            return numeric_rep::I32;
        }

        // (a numeric literal's value, or a negated numeric literal's)
        std::optional<double> constant_number(node const* aNode)
        {
            aNode = unparenthesized(aNode);
            if (aNode->is(construct::Negate) && aNode->size() == 1u)
            {
                if (auto const n = literal_number(aNode->child(0u)))
                    return -*n;
                return {};
            }
            return literal_number(aNode);
        }

        bool int32_constant(std::optional<double> const& aValue)
        {
            return small_integer(aValue) && *aValue < 2147483648.0;
        }

        // (a counter's step: an i64 counter's updates are at most 256 (in magnitude), so that its value stays below 2^62 (an f64 add of
        // such a step to a value of at least 2^62 (whose ulp is at least 1024) does not change it): see counter_guard)
        bool counter_step(std::optional<double> const& aValue)
        {
            return small_integer(aValue) && std::fabs(*aValue) <= 256.0;
        }

        // (ToInt32 of a number)
        std::int32_t to_int32_value(double aValue)
        {
            if (!std::isfinite(aValue))
                return 0;
            auto const m = std::fmod(std::trunc(aValue), 4294967296.0);
            return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int64_t>(m < 0.0 ? m + 4294967296.0 : m)));
        }

        bool power_of_two(double aValue)
        {
            int exponent = 0;
            return aValue != 0.0 && std::isfinite(aValue) && std::fabs(std::frexp(aValue, &exponent)) == 0.5;
        }

        intrinsic intrinsic_named(std::string_view aName, std::size_t aArguments)
        {
            if (aName == "imul")
                return aArguments == 2u ? intrinsic::Imul : intrinsic::None;
            if (aArguments != 1u)
                return intrinsic::None;
            if (aName == "floor")
                return intrinsic::Floor;
            if (aName == "ceil")
                return intrinsic::Ceil;
            if (aName == "trunc")
                return intrinsic::Trunc;
            if (aName == "sqrt")
                return intrinsic::Sqrt;
            if (aName == "abs")
                return intrinsic::Abs;
            return intrinsic::None;
        }

        // (the representation of a numeric local after an assignment with an operator (None: not a number))
        numeric_rep assignment_rep(std::string const& aOperator, numeric_rep aValue, std::optional<double> const& aConstant)
        {
            if (aOperator == "=")
                return aValue;
            if (aOperator == "+=")
                return counter_step(aConstant) ? numeric_rep::I64 : aValue != numeric_rep::None ? numeric_rep::F64 : numeric_rep::None;
            if (aOperator == "-=")
                return counter_step(aConstant) ? numeric_rep::I64 : numeric_rep::F64;   // (a number less any value is a number (or throws))
            if (aOperator == "&=" || aOperator == "|=" || aOperator == "^=" || aOperator == "<<=" || aOperator == ">>=")
                return numeric_rep::I32;
            if (aOperator == ">>>=")
                return numeric_rep::I64;
            if (aOperator == "&&=" || aOperator == "||=" || aOperator == "??=")
                return aValue != numeric_rep::None ? numeric_rep::F64 : numeric_rep::None;
            return numeric_rep::F64;
        }
    }

    numeric_rep function_generator::num_type(node* aNode)
    {
        // (I32: a 32-bit integer; I64: an integer (an i64 local, an integer literal); F64: another number; None: any value)
        aNode = const_cast<node*>(unparenthesized(aNode));
        if (aNode->is(construct::Number) || (aNode->is(construct::Negate) && aNode->size() == 1u && unparenthesized(aNode->child(0u))->is(construct::Number)))
        {
            auto const n = constant_number(aNode);
            if (!n)
                return numeric_rep::None;
            return int32_constant(n) ? numeric_rep::I32 : small_integer(n) ? numeric_rep::I64 : numeric_rep::F64;
        }
        auto const operands = [&]()
        {
            return num_type(aNode->child(0u)) != numeric_rep::None && num_type(aNode->child(1u)) != numeric_rep::None;
        };
        switch (aNode->kind)
        {
        case construct::Identifier:
            {
                auto const existing = iNumericNames.find(aNode);
                return existing != iNumericNames.end() ? rep_of(existing->second) : numeric_rep::None;
            }
        case construct::Negate:
        case construct::Plus:
            return num_type(aNode->child(0u)) != numeric_rep::None ? numeric_rep::F64 : numeric_rep::None;
        case construct::BitNot:
            return num_type(aNode->child(0u)) != numeric_rep::None ? numeric_rep::I32 : numeric_rep::None;
        case construct::Add:
        case construct::Subtract:
        case construct::Multiply:
        case construct::Divide:
        case construct::Remainder:
            return operands() ? numeric_rep::F64 : numeric_rep::None;
        case construct::BitAnd:
        case construct::BitOr:
        case construct::BitXor:
        case construct::ShiftLeft:
        case construct::ShiftRight:
            return operands() ? numeric_rep::I32 : numeric_rep::None;
        case construct::ShiftRightUnsigned:
            return operands() ? numeric_rep::I64 : numeric_rep::None;
        case construct::Conditional:
            return join(num_type(aNode->child(1u)), num_type(aNode->child(2u)));
        case construct::Call:
            switch (intrinsic_of(aNode))
            {
            case intrinsic::None:
                return numeric_rep::None;
            case intrinsic::Imul:
                return numeric_rep::I32;
            default:
                return numeric_rep::F64;
            }
        default:
            return numeric_rep::None;
        }
    }

    intrinsic function_generator::intrinsic_of(node* aNode) const
    {
        auto const existing = iIntrinsics.find(aNode);
        return existing != iIntrinsics.end() ? existing->second : intrinsic::None;
    }

    void function_generator::infer_numeric()
    {
        iNumeric.clear();
        iNumericNames.clear();
        iNumericArguments.clear();
        iInvariantNumerics.clear();
        iUnitCounters.clear();
        iMonotoneCounters.clear();
        iIntrinsics.clear();
        iIntrinsicMask = 0u;
        static bool const sDisabled = std::getenv("NEOS_JS_NO_NUMERIC") != nullptr;
        if (sDisabled || iBody || iFunction.generator || iFunction.async)
            return;
        struct use
        {
            node* declarator = nullptr;
            int declarators = 0;
            bool eligible = true;
            std::optional<std::uint32_t> parameter;     ///< (the numeric version's parameter: its index)
            std::vector<node*> sites;       ///< (its references: reads, assignments and updates)
            std::vector<node*> assignments;
            std::vector<node*> updates;
        };
        std::unordered_map<binding*, use> uses;
        for (auto& s : iCompiler.iScopes)
        {
            if (s.function != &iFunction || s.dynamic || s.kind == scope_kind::With)
                continue;
            for (auto& b : s.bindings)
                if (!b->captured && (b->kind == binding_kind::Let || b->kind == binding_kind::Const || (b->kind == binding_kind::Var && s.kind != scope_kind::Script)))
                    uses[b.get()];
        }
        if (iNumericParameters && iFunction.declaration != nullptr)
        {
            // (the numeric version: its first four parameters (each named once), whose arguments are numbers)
            for (auto* c : iFunction.declaration->children)
                if (c->is(construct::Parameters))
                {
                    std::map<std::string, int> names;
                    for (auto* p : c->children)
                        if (p->is(construct::Binding))
                            ++names[name_of(p)];
                    for (std::uint32_t i = 0u; i < c->size() && i < RegisterArguments; ++i)
                    {
                        auto* p = c->child(i);
                        if (!p->is(construct::Binding) || names[name_of(p)] != 1)
                            continue;
                        auto* b = iFunction.top->find(name_of(p));
                        if (b != nullptr && b->kind == binding_kind::Parameter && !b->captured)
                            uses[b].parameter = i;
                    }
                    break;
                }
        }
        if (uses.empty())
            return;
        // (the references: by the scopes of the function (a node's scope is its children's))
        std::vector<scope*> scopes;
        scopes.push_back(iFunction.top);
        auto const lookup = [&](std::string const& aName) -> binding*
        {
            for (auto s = scopes.rbegin(); s != scopes.rend(); ++s)
                if (auto* b = (*s)->find(aName))
                    return b;
            return nullptr;
        };
        // (the numeric version's calls of Math functions that can be computed inline: Math is not a binding of the program (other than
        // the global object's property, which its entry checks))
        std::unordered_map<node const*, intrinsic> intrinsics;
        auto const intrinsic_call = [&](node* aCall) -> intrinsic
        {
            if (!iNumericParameters || iCompiler.eval_code() || iCompiler.iModule || aCall->size() < 1u)
                return intrinsic::None;
            auto* callee = const_cast<node*>(unparenthesized(aCall->child(0u)));
            if (!callee->is(construct::Dot) || callee->size() < 2u || !callee->child(0u)->is(construct::Identifier) || name_of(callee->child(0u)) != "Math")
                return intrinsic::None;
            for (std::size_t i = 1u; i < aCall->size(); ++i)
                if (aCall->child(i)->is(construct::Spread))
                    return intrinsic::None;
            auto const result = intrinsic_named(name_of(callee->child(1u)), aCall->size() - 1u);
            if (result == intrinsic::None || lookup("Math") != nullptr)
                return intrinsic::None;
            for (auto* s = iFunction.top->parent; s != nullptr; s = s->parent)
                if (s->kind == scope_kind::With || s->dynamic || s->find("Math") != nullptr || iCompiler.sloppy_eval_var_scope(s))
                    return intrinsic::None;
            return result;
        };
        bool inTarget = false;      // (a destructuring assignment's target or a for-in/of head: its identifiers are assigned any value)
        std::function<void(node*)> walk = [&](node* aNode)
        {
            if (aNode->annexB && aNode->function != nullptr)
            {
                // (a block's function declaration that assigns the function's var binding (Annex B.3.3): that binding holds a function)
                if (auto* b = iFunction.body->find(aNode->function->name); b != nullptr && uses.contains(b))
                    uses[b].eligible = false;
            }
            if (aNode->function != nullptr && aNode->function != &iFunction)
            {
                // (another function (its uses of this function's bindings captured them); a field's key is this function's)
                if (aNode->is(construct::Field) && aNode->size() > 0u)
                    walk(aNode->child(0u));
                return;
            }
            bool pushed = false;
            if (aNode->ownScope != nullptr && aNode->ownScope->function == &iFunction && scopes.back() != aNode->ownScope)
            {
                if ((aNode->is(construct::With) || aNode->is(construct::Switch)) && aNode->size() > 0u)
                {
                    walk(aNode->child(0u));
                    scopes.push_back(aNode->ownScope);
                    for (std::size_t i = 1u; i < aNode->size(); ++i)
                        walk(aNode->child(i));
                    scopes.pop_back();
                    return;
                }
                scopes.push_back(aNode->ownScope);
                pushed = true;
            }
            switch (aNode->kind)
            {
            case construct::Identifier:
                if (auto* b = lookup(name_of(aNode)); b != nullptr && uses.contains(b))
                {
                    uses[b].sites.push_back(aNode);
                    iNumericNames[aNode] = b;
                    if (inTarget)
                        uses[b].eligible = false;
                }
                break;
            case construct::ForIn:
            case construct::ForOf:
            case construct::ForAwaitOf:
                if (aNode->size() > 0u)
                {
                    auto const was = inTarget;
                    inTarget = true;
                    walk(aNode->child(0u));
                    inTarget = was;
                    for (std::size_t i = 1u; i < aNode->size(); ++i)
                        walk(aNode->child(i));
                    if (pushed)
                        scopes.pop_back();
                    return;
                }
                break;
            case construct::Binding:
                if (auto* b = lookup(name_of(aNode)); b != nullptr && uses.contains(b))
                {
                    auto& u = uses[b];
                    auto* d = aNode->parent;
                    if (u.parameter && d != nullptr && d->is(construct::Parameters))
                        break;      // (the parameter itself)
                    if (d != nullptr && d->is(construct::Declarator) && d->child(0u) == aNode && d->parent != nullptr &&
                        (d->parent->is(construct::Let) || d->parent->is(construct::Const) || d->parent->is(construct::Var)))
                    {
                        ++u.declarators;
                        u.declarator = d;
                    }
                    else
                        u.eligible = false;
                }
                break;
            case construct::Assign:
                if (auto* t = const_cast<node*>(unparenthesized(aNode->child(0u))); t->is(construct::Identifier))
                {
                    if (auto* b = lookup(name_of(t)); b != nullptr && uses.contains(b))
                    {
                        uses[b].assignments.push_back(aNode);
                        uses[b].sites.push_back(t);
                        iNumericNames[t] = b;
                        if (inTarget)
                            uses[b].eligible = false;
                        walk(aNode->child(1u));
                        if (pushed)
                            scopes.pop_back();
                        return;
                    }
                }
                else
                {
                    auto const was = inTarget;
                    inTarget = true;
                    walk(aNode->child(0u));
                    inTarget = was;
                    walk(aNode->child(1u));
                    if (pushed)
                        scopes.pop_back();
                    return;
                }
                break;
            case construct::PreIncrement:
            case construct::PreDecrement:
            case construct::PostIncrement:
            case construct::PostDecrement:
                if (auto* t = const_cast<node*>(unparenthesized(aNode->child(0u))); t->is(construct::Identifier))
                {
                    if (auto* b = lookup(name_of(t)); b != nullptr && uses.contains(b))
                    {
                        uses[b].updates.push_back(aNode);
                        uses[b].sites.push_back(t);
                        iNumericNames[t] = b;
                        if (pushed)
                            scopes.pop_back();
                        return;
                    }
                }
                break;
            case construct::Call:
                if (auto const k = intrinsic_call(aNode); k != intrinsic::None)
                    intrinsics[aNode] = k;
                break;
            default:
                break;
            }
            for (auto* c : aNode->children)
                walk(c);
            if (pushed)
                scopes.pop_back();
        };
        if (iFunction.script)
        {
            for (auto* c : iFunction.top->owner->children)
                walk(c);
        }
        else if (iFunction.declaration != nullptr)
        {
            for (auto* c : iFunction.declaration->children)
                walk(c);
        }
        // (eligibility: declared once, with an initializer, where it is reached before its uses (a parameter: not declared again))
        for (auto& [b, u] : uses)
        {
            if (b == iFunction.argumentsBinding)
            {
                // (var arguments: the binding holds the arguments object first)
                u.eligible = false;
                continue;
            }
            if (u.parameter)
            {
                u.eligible = u.eligible && u.declarators == 0;
                continue;
            }
            if (!u.eligible || u.declarators != 1 || u.declarator == nullptr || u.declarator->size() < 2u)
            {
                u.eligible = false;
                continue;
            }
            auto* statement = u.declarator->parent;
            auto* list = statement->parent;
            if (list != nullptr && list->is(construct::ForInit))
            {
                statement = list;
                list = list->parent;
            }
            bool const forInit = list != nullptr && list->is(construct::For) && list->child(0u) == statement;
            bool placed = list != nullptr && (list->is(construct::Block) || list->is(construct::Body) || list->is(construct::Script) ||
                (forInit && (b->kind != binding_kind::Var || (list->parent != nullptr &&
                (list->parent->is(construct::Block) || list->parent->is(construct::Body) || list->parent->is(construct::Script))))));
            if (b->kind == binding_kind::Var)
            {
                // (a var: declared in the function's body (or a for statement's head in it))
                auto* body = list != nullptr && list->is(construct::For) ? list->parent : list;
                placed = placed && body != nullptr && (body->is(construct::Body) || body->is(construct::Script)) && body->ownScope == nullptr;
                placed = placed && (body == iFunction.declaration || (body->parent == iFunction.declaration) || iFunction.script);
            }
            auto const end = u.declarator->source.data() + u.declarator->source.size();
            for (auto* site : u.sites)
                if (site->source.data() < end)
                    placed = false;
            u.eligible = placed;
            // (a const that is assigned: the assignment throws (its general code does))
            if (b->kind == binding_kind::Const && (!u.assignments.empty() || !u.updates.empty()))
                u.eligible = false;
        }
        // (the representations: the least that holds every value assigned (a parameter's argument is any number), from i32 up)
        auto const solve = [&]()
        {
            iNumeric.clear();
            for (auto& [b, u] : uses)
                if (u.eligible)
                    iNumeric[b] = u.parameter ? numeric_rep::F64 : numeric_rep::I32;
            for (bool changed = true; changed;)
            {
                changed = false;
                for (auto& [b, u] : uses)
                {
                    auto const rep = rep_of(b);
                    if (rep == numeric_rep::None)
                        continue;
                    auto result = rep;
                    if (!u.parameter)
                        result = join(result, num_type(u.declarator->child(1u)));
                    for (auto* a : u.assignments)
                    {
                        auto* value = a->child(1u);
                        result = join(result, assignment_rep(assignment_operator(a), num_type(value), constant_number(value)));
                    }
                    if (!u.updates.empty())
                        result = join(result, numeric_rep::I64);
                    if (result != rep)
                    {
                        if (result == numeric_rep::None)
                            iNumeric.erase(b);
                        else
                            iNumeric[b] = result;
                        changed = true;
                    }
                }
            }
        };
        iIntrinsics = intrinsics;
        solve();
        if (!iIntrinsics.empty() && (iFunction.declaration == nullptr || !pure(iFunction.declaration->children.back())))
        {
            // (intrinsics are computed inline only in a version that runs no other code (that could change their bindings once its entry has
            // checked them))
            iIntrinsics.clear();
            solve();
        }
        for (auto const& i : iIntrinsics)
            iIntrinsicMask |= 1u << static_cast<std::uint32_t>(i.second);
        for (auto& [b, u] : uses)
            if (u.parameter && rep_of(b) != numeric_rep::None)
                iNumericArguments.emplace_back(*u.parameter, b);
        std::sort(iNumericArguments.begin(), iNumericArguments.end());
        for (auto& [b, u] : uses)
            if (rep_of(b) == numeric_rep::F64 && u.assignments.empty() && u.updates.empty() && (u.parameter || b->kind == binding_kind::Const))
                iInvariantNumerics.insert(b);
        for (auto& [b, u] : uses)
        {
            // (a counter: initialized to a safe integer, changed only by steps of one)
            if (rep_of(b) != numeric_rep::I64 || u.parameter || u.declarator == nullptr || u.declarator->size() < 2u)
                continue;
            auto const initial = constant_number(u.declarator->child(1u));
            if (!(num_type(u.declarator->child(1u)) == numeric_rep::I32 || (initial && *initial == std::trunc(*initial) && std::fabs(*initial) <= SafeInteger)))
                continue;
            bool unit = true;
            bool up = false;
            bool down = false;
            for (auto* a : u.assignments)
            {
                auto const oper = assignment_operator(a);
                auto const step = constant_number(a->child(1u));
                if (!((oper == "+=" || oper == "-=") && step && std::fabs(*step) == 1.0))
                    unit = false;
                else
                    ((*step > 0.0) == (oper == "+=") ? up : down) = true;
            }
            for (auto* a : u.updates)
                (a->is(construct::PreIncrement) || a->is(construct::PostIncrement) ? up : down) = true;
            if (unit)
            {
                iUnitCounters.insert(b);
                if (up != down)
                    iMonotoneCounters[b] = up ? 1 : -1;
            }
        }
    }

    namespace
    {
        // (a comparison of an integer and an invariant f64 (i OP x): the integer bound of x that the integer is compared with (as i64s
        // compare): its rounding (ceil or floor) and the bound that a NaN gives (the comparison is false))
        int bound_variant(construct aKind)
        {
            switch (aKind)
            {
            case construct::LessThan: return 1;         // (i < x: i < ceil(x); NaN: the least)
            case construct::LessEqual: return 0;        // (i <= x: i <= floor(x); NaN: the least)
            case construct::GreaterThan: return 2;      // (i > x: i > floor(x); NaN: the greatest)
            case construct::GreaterEqual: return 3;     // (i >= x: i >= ceil(x); NaN: the greatest)
            default: return -1;
            }
        }

        construct reversed_comparison(construct aKind)
        {
            switch (aKind)
            {
            case construct::LessThan: return construct::GreaterThan;
            case construct::GreaterThan: return construct::LessThan;
            case construct::LessEqual: return construct::GreaterEqual;
            case construct::GreaterEqual: return construct::LessEqual;
            default: return aKind;
            }
        }
    }

    void function_generator::bound_shadows(node* aTest, std::vector<std::pair<binding const*, int>>& aAdded)
    {
        // (before a loop: the integer bounds of the invariant f64s that its test compares integers with (computed once, not each iteration))
        if (aTest == nullptr || aTest->function != nullptr)
            return;
        auto* n = const_cast<node*>(unparenthesized(aTest));
        if (bound_variant(n->kind) >= 0 && n->size() == 2u)
        {
            for (int side = 0; side < 2; ++side)
            {
                auto* x = const_cast<node*>(unparenthesized(n->child(side == 0 ? 1u : 0u)));
                auto* i = n->child(side == 0 ? 0u : 1u);
                if (!x->is(construct::Identifier) || !integral(num_type(i)) || num_type(x) != numeric_rep::F64)
                    continue;
                auto const name = iNumericNames.find(x);
                if (name == iNumericNames.end() || !iInvariantNumerics.contains(name->second))
                    continue;
                auto const kind = side == 0 ? n->kind : reversed_comparison(n->kind);
                auto const variant = bound_variant(kind);
                // (a monotone counter compared: its own bound (see the end))
                auto const counter = unparenthesized(i)->is(construct::Identifier) && iNumericNames.contains(unparenthesized(i)) ?
                    monotone(iNumericNames.at(unparenthesized(i))) : 0;
                auto const key = std::make_pair(static_cast<binding const*>(name->second), variant + (counter > 0 ? 16 : counter < 0 ? 32 : 0));
                if (iBoundShadows.contains(key))
                    continue;
                bool const ceiling = (variant & 1) != 0;
                bool const nanHigh = (variant & 2) != 0;
                auto const value = temporary(F64);
                numeric(x);
                set(value);
                get(value);
                get(value);
                op(opcode::F64Ne);
                op(opcode::If);
                iCode.push_back(0x7Eu);
                iControls.push_back(control{ control_kind::Block });
                i64(nanHigh ? std::numeric_limits<std::int64_t>::max() : std::numeric_limits<std::int64_t>::min());
                op(opcode::Else);
                get(value);
                f64(4611686018427387904.0);     // (2^62: beyond the integers (counters) compared)
                op(opcode::F64Ge);
                op(opcode::If);
                iCode.push_back(0x7Eu);
                iControls.push_back(control{ control_kind::Block });
                i64(std::numeric_limits<std::int64_t>::max());
                op(opcode::Else);
                get(value);
                f64(-4611686018427387904.0);
                op(opcode::F64Le);
                op(opcode::If);
                iCode.push_back(0x7Eu);
                iControls.push_back(control{ control_kind::Block });
                i64(std::numeric_limits<std::int64_t>::min());
                op(opcode::Else);
                get(value);
                op(ceiling ? opcode::F64Ceil : opcode::F64Floor);
                op(opcode::I64SConvertF64);
                end_control();
                end_control();
                end_control();
                release(value);
                auto const shadow = local(I64);
                set(shadow);
                if (counter != 0)
                {
                    // (a monotone counter c (its local) is compared, not its value v (min(c, 2^53) up, max(c, -2^53) down): v OP bound is
                    // c OP bound, unless the bound is beyond where v stops (then v OP bound is always true or always false, as c OP the
                    // extreme i64 is (c never gets there)))
                    bool const strictBelowOrAtLeast = kind == construct::LessThan || kind == construct::GreaterEqual;
                    bool const strictAboveOrAtMost = kind == construct::GreaterThan || kind == construct::LessEqual;
                    get(shadow);
                    if (counter > 0)
                    {
                        i64(strictBelowOrAtLeast ? 9007199254740993ll : 9007199254740992ll);
                        op(opcode::I64GeS);
                    }
                    else
                    {
                        i64(strictAboveOrAtMost ? -9007199254740993ll : -9007199254740992ll);
                        op(opcode::I64LeS);
                    }
                    op(opcode::If);
                    iCode.push_back(0x40u);
                    iControls.push_back(control{ control_kind::Block });
                    i64(counter > 0 ? std::numeric_limits<std::int64_t>::max() : std::numeric_limits<std::int64_t>::min());
                    set(shadow);
                    end_control();
                }
                iBoundShadows[key] = shadow;
                aAdded.push_back(key);
            }
            return;
        }
        if (n->is(construct::And) || n->is(construct::Or) || n->is(construct::Parenthesized))
            for (auto* c : n->children)
                bound_shadows(c, aAdded);
    }

    bool function_generator::pure(node* aNode)
    {
        // (code that runs no user code (and so changes no binding but its locals): literals, locals, operations of numbers, intrinsic calls of
        // numbers and control flow)
        if (aNode == nullptr)
            return true;
        auto const all = [&](std::size_t aFirst = 0u)
        {
            for (std::size_t i = aFirst; i < aNode->size(); ++i)
                if (!pure(aNode->child(i)))
                    return false;
            return true;
        };
        auto const numbers = [&]()
        {
            for (auto* c : aNode->children)
                if (num_type(c) == numeric_rep::None)
                    return false;
            return all();
        };
        switch (aNode->kind)
        {
        case construct::Number:
        case construct::True:
        case construct::False:
        case construct::Null:
        case construct::String:
        case construct::Empty:
        case construct::Break:
        case construct::Continue:
            return true;
        case construct::Identifier:
            return iNumericNames.contains(aNode);
        case construct::Parenthesized:
        case construct::Comma:
        case construct::Body:
        case construct::Block:
        case construct::If:
        case construct::While:
        case construct::Do:
        case construct::For:
        case construct::ForInit:
        case construct::ForTest:
        case construct::ForUpdate:
        case construct::ExpressionStatement:
        case construct::Return:
        case construct::Not:
        case construct::And:
        case construct::Or:
        case construct::Coalesce:
        case construct::Conditional:
        case construct::StrictEqual:
        case construct::StrictNotEqual:
            return all();
        case construct::Labelled:
            return aNode->size() == 2u && pure(aNode->child(1u));
        case construct::Let:
        case construct::Const:
        case construct::Var:
            for (auto* d : aNode->children)
                if (!d->is(construct::Declarator) || d->size() < 1u || !d->child(0u)->is(construct::Binding) || (d->size() > 1u && !pure(d->child(1u))))
                    return false;
            return true;
        case construct::Negate:
        case construct::Plus:
        case construct::BitNot:
        case construct::Add:
        case construct::Subtract:
        case construct::Multiply:
        case construct::Divide:
        case construct::Remainder:
        case construct::BitAnd:
        case construct::BitOr:
        case construct::BitXor:
        case construct::ShiftLeft:
        case construct::ShiftRight:
        case construct::ShiftRightUnsigned:
        case construct::LessThan:
        case construct::GreaterThan:
        case construct::LessEqual:
        case construct::GreaterEqual:
        case construct::Equal:
        case construct::NotEqual:
            return numbers();
        case construct::Assign:
            {
                auto* target = unparenthesized(aNode->child(0u));
                if (!target->is(construct::Identifier) || !iNumericNames.contains(target) || !pure(aNode->child(1u)))
                    return false;
                auto const oper = assignment_operator(aNode);
                return oper == "=" || oper == "&&=" || oper == "||=" || oper == "??=" ||
                    (num_type(const_cast<node*>(target)) != numeric_rep::None && num_type(aNode->child(1u)) != numeric_rep::None);
            }
        case construct::PreIncrement:
        case construct::PreDecrement:
        case construct::PostIncrement:
        case construct::PostDecrement:
            {
                auto* target = const_cast<node*>(unparenthesized(aNode->child(0u)));
                return target->is(construct::Identifier) && iNumericNames.contains(target) && num_type(target) != numeric_rep::None;
            }
        case construct::Call:
            return intrinsic_of(aNode) != intrinsic::None && all(1u);
        default:
            return false;
        }
    }

    bool function_generator::version_eligible() const
    {
        // (a function (not a generator, async function, class constructor, ...) with simple parameters and without the arguments object, direct
        // eval, with statements, functions or tagged templates (whose code would be generated twice))
        static bool const sDisabled = std::getenv("NEOS_JS_NO_NUMERIC") != nullptr || std::getenv("NEOS_JS_NO_VERSIONS") != nullptr;
        if (sDisabled || iBody || iFunction.script || iFunction.generator || iFunction.async || iFunction.classConstructor || iFunction.fieldInitializer ||
            iFunction.staticBlock || iFunction.directEval || iFunction.usesArguments || iFunction.argumentsBinding != nullptr || !iFunction.simpleParameters ||
            !iFunction.functions.empty() || iFunction.declaration == nullptr || iCompiler.eval_code() || iFunction.declaration->source.size() > 20000u)
            return false;
        for (auto const& s : iCompiler.iScopes)
            if (s.function == &iFunction && (s.dynamic || s.kind == scope_kind::With || s.hasEnvironment))
                return false;
        std::function<bool(node const*)> tagged = [&](node const* aNode)
        {
            if (aNode->is(construct::Tagged) || aNode->is(construct::ClassExpression) || aNode->is(construct::Class))
                return true;
            for (auto const* c : aNode->children)
                if (tagged(c))
                    return true;
            return false;
        };
        return !tagged(iFunction.declaration);
    }

    void function_generator::version_check()
    {
        // (the numeric version's parameters' arguments are numbers and its intrinsics' bindings the built-in functions)
        bool first = true;
        for (auto const& a : iNumericArguments)
        {
            get(FirstArgumentLocal + a.first);
            constant(values::TagSpecial);
            op(opcode::I64LtU);
            if (!first)
                op(opcode::I32And);
            first = false;
        }
        if (iIntrinsicMask != 0u)
        {
            i32(static_cast<std::int32_t>(iIntrinsicMask));
            runtime("intrinsics_original", { I32 }, I32);
            if (!first)
                op(opcode::I32And);
            first = false;
        }
        if (first)
            i32(1);
    }

    void function_generator::box(numeric_rep aRep)
    {
        if (aRep == numeric_rep::I64)
            op(opcode::F64SConvertI64);
        else if (aRep == numeric_rep::I32)
            op(opcode::F64SConvertI32);
        if (aRep != numeric_rep::None)
            op(opcode::I64ReinterpretF64);
    }

    void function_generator::unbox(numeric_rep aRep)
    {
        if (aRep == numeric_rep::None)
            return;
        op(opcode::F64ReinterpretI64);
        if (aRep == numeric_rep::I64)
            op(opcode::I64SConvertF64);
        else if (aRep == numeric_rep::I32)
            op(opcode::I32SConvertF64);
    }

    void function_generator::numeric(node* aNode)
    {
        aNode = const_cast<node*>(unparenthesized(aNode));
        if (auto const c = constant_number(aNode); c && (aNode->is(construct::Number) || aNode->is(construct::Negate)))
        {
            f64(*c);
            return;
        }
        switch (aNode->kind)
        {
        case construct::Identifier:
            {
                auto* b = iNumericNames.at(aNode);
                get(b->slot);
                counter_value(b);
                if (rep_of(b) == numeric_rep::I64)
                    op(opcode::F64SConvertI64);
                else if (rep_of(b) == numeric_rep::I32)
                    op(opcode::F64SConvertI32);
            }
            return;
        case construct::Negate:
            numeric(aNode->child(0u));
            op(opcode::F64Neg);
            return;
        case construct::Plus:
            numeric(aNode->child(0u));
            return;
        case construct::Remainder:
            numeric(aNode->child(0u));
            numeric(aNode->child(1u));
            f64_remainder(aNode->child(0u), aNode->child(1u));
            return;
        case construct::Add:
        case construct::Subtract:
        case construct::Multiply:
        case construct::Divide:
            numeric(aNode->child(0u));
            numeric(aNode->child(1u));
            op(aNode->is(construct::Add) ? opcode::F64Add : aNode->is(construct::Subtract) ? opcode::F64Sub : aNode->is(construct::Multiply) ? opcode::F64Mul : opcode::F64Div);
            return;
        case construct::ShiftRightUnsigned:
            int32(aNode->child(0u));
            int32(aNode->child(1u));
            op(opcode::I32ShrU);
            op(opcode::F64UConvertI32);
            return;
        case construct::Conditional:
            condition(aNode->child(0u));
            op(opcode::If);
            iCode.push_back(0x7Cu);     // (f64)
            iControls.push_back(control{ control_kind::Block });
            numeric(aNode->child(1u));
            op(opcode::Else);
            numeric(aNode->child(2u));
            end_control();
            return;
        case construct::Call:
            {
                static std::map<intrinsic, opcode> const sOperations = {
                    { intrinsic::Floor, opcode::F64Floor }, { intrinsic::Ceil, opcode::F64Ceil }, { intrinsic::Trunc, opcode::F64Trunc },
                    { intrinsic::Sqrt, opcode::F64Sqrt }, { intrinsic::Abs, opcode::F64Abs } };
                if (auto const o = sOperations.find(intrinsic_of(aNode)); o != sOperations.end())
                {
                    numeric(aNode->child(1u));
                    op(o->second);
                    return;
                }
            }
            break;
        default:
            break;
        }
        // (a 32-bit integer)
        int32(aNode);
        op(opcode::F64SConvertI32);
    }

    void function_generator::int32(node* aNode)
    {
        // (ToInt32 of a number: an i32)
        aNode = const_cast<node*>(unparenthesized(aNode));
        if (auto const c = constant_number(aNode); c && (aNode->is(construct::Number) || aNode->is(construct::Negate)))
        {
            i32(to_int32_value(*c));
            return;
        }
        static std::map<construct, opcode> const sOperations = {
            { construct::BitAnd, opcode::I32And }, { construct::BitOr, opcode::I32Ior }, { construct::BitXor, opcode::I32Xor },
            { construct::ShiftLeft, opcode::I32Shl }, { construct::ShiftRight, opcode::I32ShrS }, { construct::ShiftRightUnsigned, opcode::I32ShrU } };
        switch (aNode->kind)
        {
        case construct::Identifier:
            {
                auto* b = iNumericNames.at(aNode);
                get(b->slot);
                counter_value(b);
                if (rep_of(b) == numeric_rep::I64)
                    op(opcode::I32ConvertI64);  // (an integer below 2^63: its low 32 bits)
                else if (rep_of(b) == numeric_rep::F64)
                    to_int32();
            }
            return;
        case construct::BitAnd:
        case construct::BitOr:
        case construct::BitXor:
        case construct::ShiftLeft:
        case construct::ShiftRight:
        case construct::ShiftRightUnsigned:
            int32(aNode->child(0u));
            int32(aNode->child(1u));
            op(sOperations.at(aNode->kind));    // (shift counts are masked (& 31) as JavaScript's are)
            return;
        case construct::BitNot:
            int32(aNode->child(0u));
            i32(-1);
            op(opcode::I32Xor);
            return;
        case construct::Add:
        case construct::Subtract:
            if (num_type(aNode->child(0u)) == numeric_rep::I32 && num_type(aNode->child(1u)) == numeric_rep::I32)
            {
                // (the sum of 32-bit integers is exact: its ToInt32 is the i32 sum)
                int32(aNode->child(0u));
                int32(aNode->child(1u));
                op(aNode->is(construct::Add) ? opcode::I32Add : opcode::I32Sub);
                return;
            }
            break;
        case construct::Conditional:
            condition(aNode->child(0u));
            op(opcode::If);
            iCode.push_back(0x7Fu);     // (i32)
            iControls.push_back(control{ control_kind::Block });
            int32(aNode->child(1u));
            op(opcode::Else);
            int32(aNode->child(2u));
            end_control();
            return;
        case construct::Call:
            if (intrinsic_of(aNode) == intrinsic::Imul)
            {
                int32(aNode->child(1u));
                int32(aNode->child(2u));
                op(opcode::I32Mul);
                return;
            }
            break;
        default:
            break;
        }
        if (num_type(aNode) == numeric_rep::I64)
        {
            integer(aNode);
            op(opcode::I32ConvertI64);
            return;
        }
        numeric(aNode);
        to_int32();
    }

    void function_generator::integer(node* aNode)
    {
        // (an integer (num_type I64 or I32): as an i64)
        aNode = const_cast<node*>(unparenthesized(aNode));
        if (auto const c = constant_number(aNode); c && (aNode->is(construct::Number) || aNode->is(construct::Negate)))
        {
            i64(static_cast<std::int64_t>(*c));
            return;
        }
        switch (aNode->kind)
        {
        case construct::Identifier:
            {
                auto* b = iNumericNames.at(aNode);
                get(b->slot);
                counter_value(b);
                if (rep_of(b) == numeric_rep::I32)
                    op(opcode::I64SConvertI32);
            }
            return;
        case construct::ShiftRightUnsigned:
            int32(aNode->child(0u));
            int32(aNode->child(1u));
            op(opcode::I32ShrU);
            op(opcode::I64UConvertI32);
            return;
        case construct::Conditional:
            condition(aNode->child(0u));
            op(opcode::If);
            iCode.push_back(0x7Eu);     // (i64)
            iControls.push_back(control{ control_kind::Block });
            integer(aNode->child(1u));
            op(opcode::Else);
            integer(aNode->child(2u));
            end_control();
            return;
        default:
            int32(aNode);
            op(opcode::I64SConvertI32);
            return;
        }
    }

    void function_generator::typed(node* aNode, numeric_rep aRep)
    {
        switch (aRep)
        {
        case numeric_rep::I32:
            int32(aNode);
            break;
        case numeric_rep::I64:
            integer(aNode);
            break;
        default:
            numeric(aNode);
            break;
        }
    }

    void function_generator::to_int32()
    {
        // (an f64: its ToInt32: below 2^63 (in magnitude) the low 32 bits of its truncation; NaN and the infinities 0; beyond, its truncation
        // less a multiple of 2^32 (exact: Sterbenz), then its low 32 bits)
        auto const t = temporary(F64);
        tee(t);
        op(opcode::F64Abs);
        f64(9223372036854775808.0);
        op(opcode::F64Lt);
        op(opcode::If);
        iCode.push_back(0x7Fu);
        iControls.push_back(control{ control_kind::Block });
        get(t);
        op(opcode::I64SConvertF64);
        op(opcode::I32ConvertI64);
        op(opcode::Else);
        get(t);
        op(opcode::F64Abs);
        f64(std::numeric_limits<double>::infinity());
        op(opcode::F64Lt);
        op(opcode::If);
        iCode.push_back(0x7Fu);
        iControls.push_back(control{ control_kind::Block });
        get(t);
        get(t);
        f64(1.0 / 4294967296.0);
        op(opcode::F64Mul);
        op(opcode::F64Trunc);
        f64(4294967296.0);
        op(opcode::F64Mul);
        op(opcode::F64Sub);
        op(opcode::I64SConvertF64);
        op(opcode::I32ConvertI64);
        op(opcode::Else);
        i32(0);
        end_control();
        end_control();
        release(t);
    }

    int function_generator::monotone(binding const* aBinding) const
    {
        auto const m = iMonotoneCounters.find(aBinding);
        return m != iMonotoneCounters.end() ? m->second : 0;
    }

    void function_generator::counter_value(binding const* aBinding)
    {
        // (a monotone counter's value (its local on the stack): as a double would be: a counter that only steps up by one from a safe integer
        // stays at 2^53 once it gets there (2^53 plus one rounds to 2^53), so its value is min(local, 2^53) (the local counts on: it would
        // reach 2^63 only after centuries); stepping down, max(local, -2^53). Without a branch: limit + ((local - limit) & mask), the mask
        // all ones if the difference is negative (up) or positive (down))
        auto const direction = monotone(aBinding);
        if (direction == 0)
            return;
        auto const limit = direction > 0 ? 9007199254740992ll : -9007199254740992ll;
        auto const difference = temporary(I64);
        i64(limit);
        op(opcode::I64Sub);
        set(difference);
        get(difference);
        if (direction > 0)
        {
            get(difference);
            i64(63);
            op(opcode::I64ShrS);
        }
        else
        {
            i64(0);
            get(difference);
            op(opcode::I64Sub);
            i64(63);
            op(opcode::I64ShrS);
        }
        op(opcode::I64And);
        i64(limit);
        op(opcode::I64Add);
        release(difference);
    }

    bool function_generator::raw_counter(node* aNode, node* aOther)
    {
        // (a monotone counter compared with an i32 or a constant within (-2^53, 2^53): its local compares as its value does (beyond 2^53
        // both are beyond the other operand))
        auto* n = const_cast<node*>(unparenthesized(aNode));
        if (!n->is(construct::Identifier))
            return false;
        auto const name = iNumericNames.find(n);
        if (name == iNumericNames.end() || monotone(name->second) == 0)
            return false;
        if (num_type(aOther) == numeric_rep::I32)
            return true;
        auto const k = constant_number(aOther);
        return k && std::fabs(*k) < 9007199254740992.0;
    }

    void function_generator::counter_guard(std::uint32_t aSlot, std::int64_t aStep, binding const* aBinding)
    {
        // (an i64 counter beyond 2^53: as a double would be (its old value (its value less the step) plus the step, rounded); a counter whose
        // only changes are steps of one from a safe integer is never beyond 2^53 (2^53 plus one rounds to 2^53): only 2^53 + 1 is)
        if (aBinding != nullptr && (aStep == 1 || aStep == -1) && monotone(aBinding) == aStep)
            return;     // (a monotone counter: its value is clamped when it is read (see counter_value))
        get(aSlot);
        if (aBinding != nullptr && (aStep == 1 || aStep == -1) && iUnitCounters.contains(aBinding))
        {
            i64(aStep == 1 ? 9007199254740993ll : -9007199254740993ll);
            op(opcode::I64Eq);
        }
        else
        {
        i64(9007199254740992ll);
        op(opcode::I64Add);
        i64(2 * 9007199254740992ll);
        op(opcode::I64GtU);
        }
        op(opcode::If);
        iCode.push_back(0x40u);
        iControls.push_back(control{ control_kind::Block });
        get(aSlot);
        i64(aStep);
        op(opcode::I64Sub);
        op(opcode::F64SConvertI64);
        f64(static_cast<double>(aStep));
        op(opcode::F64Add);
        op(opcode::I64SConvertF64);
        set(aSlot);
        end_control();
    }

    void function_generator::f64_remainder(node* aDividend, node* aDivisor)
    {
        // (two f64s: the remainder (as % computes it): of integers below 2^53 (in magnitude) and a divisor that is not 0, a - b * trunc(a / b),
        // which is exact (a / b is within half an ulp, which is less than 1 / b, of the exact quotient, so its truncation is the exact
        // quotient's), with the dividend's sign (a zero remainder too); else the runtime's)
        auto const divisor = aDivisor != nullptr ? constant_number(aDivisor) : std::nullopt;
        bool const constantDivisor = divisor && *divisor != 0.0 && *divisor == std::trunc(*divisor) && std::fabs(*divisor) < SafeInteger;
        auto const lhs = aDividend != nullptr ? num_type(aDividend) : numeric_rep::F64;
        auto const rhs = aDivisor != nullptr ? num_type(aDivisor) : numeric_rep::F64;
        auto const b = temporary(F64);
        set(b);
        auto const a = temporary(F64);
        set(a);
        int conditions = 0;
        auto const both = [&]()
        {
            if (++conditions > 1)
                op(opcode::I32And);
        };
        auto const safe_integer = [&](std::uint32_t aLocal, numeric_rep aRep)
        {
            if (aRep == numeric_rep::I32)
                return;
            get(aLocal);
            op(opcode::F64Abs);
            f64(SafeInteger);
            op(opcode::F64Lt);
            both();
            if (aRep == numeric_rep::I64)
                return;
            get(aLocal);
            get(aLocal);
            op(opcode::F64Trunc);
            op(opcode::F64Eq);
            both();
        };
        safe_integer(a, lhs);
        if (!constantDivisor)
        {
            safe_integer(b, rhs);
            get(b);
            f64(0.0);
            op(opcode::F64Ne);
            both();
        }
        auto const fast = [&]()
        {
            get(a);
            get(a);
            if (constantDivisor && power_of_two(*divisor))
            {
                f64(1.0 / *divisor);
                op(opcode::F64Mul);
            }
            else
            {
                get(b);
                op(opcode::F64Div);
            }
            op(opcode::F64Trunc);
            get(b);
            op(opcode::F64Mul);
            op(opcode::F64Sub);
            get(a);
            op(opcode::F64CopySign);    // (a zero remainder has the dividend's sign)
        };
        if (conditions == 0)
            fast();
        else
        {
            op(opcode::If);
            iCode.push_back(0x7Cu);     // (f64)
            iControls.push_back(control{ control_kind::Block });
            fast();
            op(opcode::Else);
            get(a);
            op(opcode::I64ReinterpretF64);
            get(b);
            op(opcode::I64ReinterpretF64);
            runtime("remainder", { I64, I64 }, I64);
            check();
            op(opcode::F64ReinterpretI64);
            end_control();
        }
        release(a);
        release(b);
    }

    bool function_generator::numeric_value(node* aNode)
    {
        // (a unary operation, conditional expression or intrinsic call of numbers: its value)
        auto* n = const_cast<node*>(unparenthesized(aNode));
        switch (n->kind)
        {
        case construct::Negate:
        case construct::Plus:
        case construct::BitNot:
        case construct::Conditional:
        case construct::Call:
            if (auto const t = num_type(n); t != numeric_rep::None)
            {
                typed(n, t);
                box(t);
                return true;
            }
            return false;
        default:
            return false;
        }
    }

    namespace
    {
        struct compare_opcodes
        {
            opcode f64;
            opcode i64;
            opcode i32;
        };

        std::optional<compare_opcodes> compare_of(construct aKind)
        {
            switch (aKind)
            {
            case construct::LessThan: return compare_opcodes{ opcode::F64Lt, opcode::I64LtS, opcode::I32LtS };
            case construct::GreaterThan: return compare_opcodes{ opcode::F64Gt, opcode::I64GtS, opcode::I32GtS };
            case construct::LessEqual: return compare_opcodes{ opcode::F64Le, opcode::I64LeS, opcode::I32LeS };
            case construct::GreaterEqual: return compare_opcodes{ opcode::F64Ge, opcode::I64GeS, opcode::I32GeS };
            case construct::StrictEqual:
            case construct::Equal: return compare_opcodes{ opcode::F64Eq, opcode::I64Eq, opcode::I32Eq };
            case construct::StrictNotEqual:
            case construct::NotEqual: return compare_opcodes{ opcode::F64Ne, opcode::I64Ne, opcode::I32Ne };
            default: return {};
            }
        }
    }

    bool function_generator::numeric_binary(node* aNode)
    {
        auto const lhs = num_type(aNode->child(0u));
        auto const rhs = num_type(aNode->child(1u));
        if (lhs == numeric_rep::None || rhs == numeric_rep::None)
            return false;
        switch (aNode->kind)
        {
        case construct::Add:
        case construct::Subtract:
        case construct::Multiply:
        case construct::Divide:
        case construct::Remainder:
            numeric(aNode);
            op(opcode::I64ReinterpretF64);
            return true;
        case construct::BitAnd:
        case construct::BitOr:
        case construct::BitXor:
        case construct::ShiftLeft:
        case construct::ShiftRight:
            int32(aNode);
            box(numeric_rep::I32);
            return true;
        case construct::ShiftRightUnsigned:
            integer(aNode);
            box(numeric_rep::I64);
            return true;
        default:
            break;
        }
        auto const compare = compare_of(aNode->kind);
        if (!compare)
            return false;
        if (lhs == numeric_rep::I32 && rhs == numeric_rep::I32)
        {
            int32(aNode->child(0u));
            int32(aNode->child(1u));
            op(compare->i32);
        }
        else if (integral(lhs) && integral(rhs))
        {
            integer(aNode->child(0u));
            integer(aNode->child(1u));
            op(compare->i64);
        }
        else
        {
            numeric(aNode->child(0u));
            numeric(aNode->child(1u));
            op(compare->f64);
        }
        op(opcode::I64UConvertI32);
        constant(values::False);
        op(opcode::I64Add);
        return true;
    }

    bool function_generator::bound_comparison(node* aComparison)
    {
        // (i OP x, x an invariant f64 whose integer bound the loop computed: an i64 comparison)
        auto* n = aComparison;
        for (int side = 0; side < 2; ++side)
        {
            auto* x = const_cast<node*>(unparenthesized(n->child(side == 0 ? 1u : 0u)));
            auto* i = n->child(side == 0 ? 0u : 1u);
            if (!x->is(construct::Identifier) || !integral(num_type(i)))
                continue;
            auto const name = iNumericNames.find(x);
            if (name == iNumericNames.end())
                continue;
            auto const kind = side == 0 ? n->kind : reversed_comparison(n->kind);
            auto const variant = bound_variant(kind);
            if (variant < 0)
                continue;
            auto const counter = unparenthesized(i)->is(construct::Identifier) && iNumericNames.contains(unparenthesized(i)) ?
                monotone(iNumericNames.at(unparenthesized(i))) : 0;
            auto const shadow = iBoundShadows.find(std::make_pair(static_cast<binding const*>(name->second),
                variant + (counter > 0 ? 16 : counter < 0 ? 32 : 0)));
            if (shadow == iBoundShadows.end())
                continue;
            if (counter != 0)
                get(iNumericNames.at(unparenthesized(i))->slot);   // (its local (see bound_shadows))
            else
                integer(i);
            get(shadow->second);
            op(compare_of(kind)->i64);
            return true;
        }
        return false;
    }

    bool function_generator::numeric_condition(node* aNode)
    {
        // (a comparison with a number: an i32 (the other operand: if it is a number, compared inline; else as the runtime compares)
        auto* n = const_cast<node*>(unparenthesized(aNode));
        std::optional<opcode> compare;
        switch (n->kind)
        {
        case construct::LessThan: compare = opcode::F64Lt; break;
        case construct::GreaterThan: compare = opcode::F64Gt; break;
        case construct::LessEqual: compare = opcode::F64Le; break;
        case construct::GreaterEqual: compare = opcode::F64Ge; break;
        case construct::StrictEqual: compare = opcode::F64Eq; break;
        case construct::StrictNotEqual: compare = opcode::F64Ne; break;
        default:
            return false;
        }
        auto const lhs = num_type(n->child(0u));
        auto const rhs = num_type(n->child(1u));
        if (lhs == numeric_rep::None && rhs == numeric_rep::None)
            return false;
        if (lhs != numeric_rep::None && rhs != numeric_rep::None)
        {
            auto const opcodes = compare_of(n->kind);
            if (!iBoundShadows.empty() && (integral(lhs) != integral(rhs)) && bound_comparison(n))
                return true;
            if (lhs == numeric_rep::I32 && rhs == numeric_rep::I32)
            {
                int32(n->child(0u));
                int32(n->child(1u));
                op(opcodes->i32);
            }
            else if (integral(lhs) && integral(rhs))
            {
                for (int side = 0; side < 2; ++side)
                {
                    auto* operand = n->child(side == 0 ? 0u : 1u);
                    if (raw_counter(operand, n->child(side == 0 ? 1u : 0u)))
                        get(iNumericNames.at(const_cast<node*>(unparenthesized(operand)))->slot);
                    else
                        integer(operand);
                }
                op(opcodes->i64);
            }
            else
            {
                numeric(n->child(0u));
                numeric(n->child(1u));
                op(opcodes->f64);
            }
            return true;
        }
        // (one is a number: the other is evaluated in order)
        auto const a = temporary(F64);
        auto const other = temporary();
        if (lhs != numeric_rep::None)
        {
            numeric(n->child(0u));
            set(a);
            expression(n->child(1u));
            set(other);
        }
        else
        {
            expression(n->child(0u));
            set(other);
            numeric(n->child(1u));
            set(a);
        }
        get(other);
        constant(values::TagSpecial);
        op(opcode::I64LtU);
        op(opcode::If);
        iCode.push_back(0x7Fu);     // (i32)
        iControls.push_back(control{ control_kind::Block });
        if (lhs != numeric_rep::None)
        {
            get(a);
            get(other);
            op(opcode::F64ReinterpretI64);
        }
        else
        {
            get(other);
            op(opcode::F64ReinterpretI64);
            get(a);
        }
        op(*compare);
        op(opcode::Else);
        static std::map<construct, std::string> const sOperations = {
            { construct::LessThan, "less_than" }, { construct::GreaterThan, "greater_than" }, { construct::LessEqual, "less_equal" },
            { construct::GreaterEqual, "greater_equal" }, { construct::StrictEqual, "strict_equal_value" }, { construct::StrictNotEqual, "strict_not_equal" } };
        if (lhs != numeric_rep::None)
        {
            get(a);
            op(opcode::I64ReinterpretF64);
            get(other);
        }
        else
        {
            get(other);
            get(a);
            op(opcode::I64ReinterpretF64);
        }
        runtime(sOperations.at(n->kind), { I64, I64 }, I64);
        check();
        constant(values::True);
        op(opcode::I64Eq);
        end_control();
        release(other);
        release(a);
        return true;
    }

    bool function_generator::numeric_update(reference const& aReference, bool aIncrement, bool aPostfix)
    {
        // (++ and -- of a numeric local: its value (the old one if postfix))
        if (aReference.kind != reference::Local || aReference.target == nullptr)
            return false;
        auto const rep = rep_of(aReference.target);
        if (rep == numeric_rep::None || rep == numeric_rep::I32)
            return false;   // (not I32: a counter is an I64)
        auto const slot = aReference.local;
        if (rep == numeric_rep::F64)
        {
            if (aPostfix)
            {
                get(slot);
                op(opcode::I64ReinterpretF64);
            }
            get(slot);
            f64(1.0);
            op(aIncrement ? opcode::F64Add : opcode::F64Sub);
            if (aPostfix)
                set(slot);
            else
            {
                tee(slot);
                op(opcode::I64ReinterpretF64);
            }
            return true;
        }
        // (an i64: exact below 2^53; beyond, as a double rounds)
        std::int64_t const step = aIncrement ? 1 : -1;
        std::optional<std::uint32_t> old;
        if (aPostfix && !iDiscardedNow)
        {
            old = temporary(I64);
            get(slot);
            set(*old);
        }
        get(slot);
        i64(step);
        op(opcode::I64Add);
        set(slot);
        counter_guard(slot, step, aReference.target);
        if (iDiscardedNow)
            constant(values::Undefined);
        else
        {
            get(old ? *old : slot);
            counter_value(aReference.target);
            box(numeric_rep::I64);
        }
        if (old)
            release(*old);
        return true;
    }

    bool function_generator::numeric_assignment(reference const& aReference, node* aValue, std::string const& aOperator)
    {
        // (an assignment to a numeric local: its value)
        if (aReference.kind != reference::Local || aReference.target == nullptr)
            return false;
        auto const rep = rep_of(aReference.target);
        if (rep == numeric_rep::None)
            return false;
        auto const slot = aReference.local;
        auto const value = num_type(aValue);
        auto const result = [&]()
        {
            if (iDiscardedNow)
                constant(values::Undefined);
            else
            {
                get(slot);
                counter_value(aReference.target);
                box(rep);
            }
        };
        auto const step = constant_number(aValue);
        if (rep == numeric_rep::I64 && (aOperator == "+=" || aOperator == "-=") && counter_step(step))
        {
            auto const k = static_cast<std::int64_t>(*step) * (aOperator == "-=" ? -1 : 1);
            get(slot);
            i64(k);
            op(opcode::I64Add);
            set(slot);
            counter_guard(slot, k, aReference.target);
            result();
            return true;
        }
        if (value == numeric_rep::None || (aOperator == ">>>=" && rep == numeric_rep::I32))
            return false;   // (>>>= of an i32 local makes it an i64 (see infer_numeric))
        if (aOperator == "=")
        {
            typed(aValue, rep);     // (its rep holds the value's (see infer_numeric))
            set(slot);
            result();
            return true;
        }
        static std::map<std::string, opcode> const sInt32 = {
            { "&=", opcode::I32And }, { "|=", opcode::I32Ior }, { "^=", opcode::I32Xor }, { "<<=", opcode::I32Shl }, { ">>=", opcode::I32ShrS },
            { ">>>=", opcode::I32ShrU } };
        if (auto const o = sInt32.find(aOperator); o != sInt32.end())
        {
            get(slot);
            if (rep == numeric_rep::I64)
                op(opcode::I32ConvertI64);
            else if (rep == numeric_rep::F64)
                to_int32();
            int32(aValue);
            op(o->second);
            bool const unsignedResult = aOperator == ">>>=";
            if (rep == numeric_rep::I64)
                op(unsignedResult ? opcode::I64UConvertI32 : opcode::I64SConvertI32);
            else if (rep == numeric_rep::F64)
                op(unsignedResult ? opcode::F64UConvertI32 : opcode::F64SConvertI32);
            set(slot);
            result();
            return true;
        }
        if (rep == numeric_rep::F64 && (aOperator == "+=" || aOperator == "-=" || aOperator == "*=" || aOperator == "/=" || aOperator == "%="))
        {
            get(slot);
            numeric(aValue);
            if (aOperator == "+=")
                op(opcode::F64Add);
            else if (aOperator == "-=")
                op(opcode::F64Sub);
            else if (aOperator == "*=")
                op(opcode::F64Mul);
            else if (aOperator == "/=")
                op(opcode::F64Div);
            else
                f64_remainder(nullptr, aValue);
            set(slot);
            result();
            return true;
        }
        return false;
    }

    // --- modules

    namespace
    {
        // (a module export name (an identifier name or a string literal) as UTF-8)
        std::string export_name(node const* aNode)
        {
            if (aNode->is(construct::String))
            {
                auto const text = unescape(aNode->source.substr(1u, aNode->source.size() - 2u), false);
                std::string result;
                if (!text)
                    return result;
                for (std::size_t i = 0u; i < text->size(); ++i)
                {
                    char32_t cp = (*text)[i];
                    if (cp >= 0xD800u && cp <= 0xDBFFu && i + 1u < text->size() && (*text)[i + 1u] >= 0xDC00u && (*text)[i + 1u] <= 0xDFFFu)
                        cp = 0x10000u + ((cp - 0xD800u) << 10) + ((*text)[++i] - 0xDC00u);
                    if (cp < 0x80u)
                        result.push_back(static_cast<char>(cp));
                    else if (cp < 0x800u)
                    {
                        result.push_back(static_cast<char>(0xC0u | (cp >> 6)));
                        result.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                    }
                    else if (cp < 0x10000u)
                    {
                        result.push_back(static_cast<char>(0xE0u | (cp >> 12)));
                        result.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                        result.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                    }
                    else
                    {
                        result.push_back(static_cast<char>(0xF0u | (cp >> 18)));
                        result.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
                        result.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                        result.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                    }
                }
                return result;
            }
            return name_of(aNode);
        }

        bool default_export(node const* aExport)
        {
            // (export default ...)
            auto text = aExport->source.substr(6u);
            while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
                text.remove_prefix(1u);
            return text.starts_with("default");
        }

        void bound_names(node const* aPattern, std::vector<std::string>& aNames)
        {
            if (aPattern->is(construct::Binding))
            {
                aNames.push_back(name_of(aPattern));
                return;
            }
            if (aPattern->is(construct::Initializer) || aPattern->is(construct::Assign))
            {
                bound_names(aPattern->child(0u), aNames);
                return;
            }
            for (auto* c : aPattern->children)
            {
                if (aPattern->is(construct::PatternProperty) && c == aPattern->child(0u) && aPattern->size() > 1u)
                    continue;   // (a property's key)
                bound_names(c, aNames);
            }
        }

        bool contains_await(node const* aNode)
        {
            // (top-level await: outside functions)
            if (aNode->is(construct::Await) || aNode->is(construct::ForAwaitOf) || aNode->is(construct::AwaitUsing) || aNode->is(construct::ForAwaitUsing))
                return true;
            if (aNode->function != nullptr)
            {
                if (aNode->is(construct::Field) && aNode->size() > 0u)
                    return contains_await(aNode->child(0u));
                return false;
            }
            for (auto* c : aNode->children)
                if (contains_await(c))
                    return true;
            return false;
        }
    }

    std::string compiler::module_descriptor(node* aModule)
    {
        // (records separated by \x02, fields by \x01: R specifier (its requests, in order); I local request import (an import); N local request
        // (a namespace import); E export local (a local export); X export request import (an indirect export); Y export request (export * as
        // name); S request (export *); A (it awaits))
        std::string result;
        std::vector<std::string> requests;
        auto const record = [&](std::initializer_list<std::string> aFields)
        {
            bool first = true;
            for (auto const& f : aFields)
            {
                if (!first)
                    result += '\x01';
                result += f;
                first = false;
            }
            result += '\x02';
        };
        auto const request = [&](node const* aSpecifier) -> std::string
        {
            // (a module request: its specifier and its import attributes (with { key: "value" }))
            std::vector<std::string> fields{ "R", export_name(aSpecifier) };
            for (auto* c : aSpecifier->parent->children)
                if (c->is(construct::ImportAttributes))
                    for (std::size_t i = 0u; i + 1u < c->size(); i += 2u)
                        fields.push_back(export_name(c->child(i)) + "=" + export_name(c->child(i + 1u)));
            std::string key;
            for (auto const& f : fields)
                key += f + '\x01';
            for (std::size_t i = 0u; i < requests.size(); ++i)
                if (requests[i] == key)
                    return std::to_string(i);
            requests.push_back(key);
            for (std::size_t i = 0u; i < fields.size(); ++i)
            {
                if (i != 0u)
                    result += '\x01';
                result += fields[i];
            }
            result += '\x02';
            return std::to_string(requests.size() - 1u);
        };
        auto const specifier_of = [](node const* aItem) -> node const*
        {
            node const* result = nullptr;
            for (auto* c : aItem->children)
                if (c->is(construct::String))
                    result = c;
            return result;
        };
        // (early errors: an exported name exported twice; a local export of a name the module does not declare (or of a string); a
        // string name that is not well-formed Unicode; an import attribute's key given twice)
        std::set<std::string> exportedNames;
        auto const well_formed = [&](node const* aName)
        {
            if (!aName->is(construct::String))
                return;
            auto const text = unescape(aName->source.substr(1u, aName->source.size() - 2u), false);
            if (!text)
                return;
            for (std::size_t i = 0u; i < text->size(); ++i)
            {
                char32_t const cp = (*text)[i];
                if (cp >= 0xD800u && cp <= 0xDBFFu && i + 1u < text->size() && (*text)[i + 1u] >= 0xDC00u && (*text)[i + 1u] <= 0xDFFFu)
                    ++i;
                else if (cp >= 0xD800u && cp <= 0xDFFFu)
                    error(aName->source, "A module export or import name must be well-formed Unicode");
            }
        };
        auto const exported = [&](std::string const& aName, node const* aSite)
        {
            if (!exportedNames.insert(aName).second)
                error(aSite->source, "Duplicate export of '" + aName + "'");
        };
        for (auto* item : aModule->children)
        {
            if (!item->is(construct::Import) && !item->is(construct::Export))
                continue;
            for (auto* c : item->children)
            {
                if (c->is(construct::ImportAttributes))
                {
                    std::set<std::string> keys;
                    for (std::size_t i = 0u; i + 1u < c->size(); i += 2u)
                        if (!keys.insert(export_name(c->child(i))).second)
                            error(c->child(i)->source, "Duplicate import attribute '" + export_name(c->child(i)) + "'");
                }
                else if (c->is(construct::ImportNamed) || c->is(construct::ExportNamed))
                    for (auto* spec : c->children)
                        for (auto* n : spec->children)
                            well_formed(n);
                else if (c->is(construct::String) && c != item->children.back())
                    well_formed(c);
            }
        }
        // (imports first: the requests' order is the source's)
        struct import_entry { std::string request; std::string name; bool ns; };
        std::map<std::string, import_entry> imports;
        for (auto* item : aModule->children)
        {
            if (item->is(construct::Import))
            {
                auto const r = request(specifier_of(item));
                for (auto* c : item->children)
                {
                    if (c->is(construct::ImportDefault))
                    {
                        record({ "I", name_of(c->child(0u)), r, "default" });
                        imports[name_of(c->child(0u))] = { r, "default", false };
                    }
                    else if (c->is(construct::ImportNamespace))
                    {
                        record({ "N", name_of(c->child(0u)), r });
                        imports[name_of(c->child(0u))] = { r, "", true };
                    }
                    else if (c->is(construct::ImportNamed))
                        for (auto* spec : c->children)
                        {
                            auto const local = name_of(spec->children.back());
                            auto const imported = spec->size() > 1u ? export_name(spec->child(0u)) : local;
                            record({ "I", local, r, imported });
                            imports[local] = { r, imported, false };
                        }
                }
            }
            else if (item->is(construct::Export) && item->size() > 0u)
            {
                auto* c = item->child(0u);
                auto const from = specifier_of(item);
                if (c->is(construct::String) && (item->size() == 1u || !item->child(1u)->is(construct::String)))
                    record({ "S", request(c) });                                            // (export * from)
                else if ((c->is(construct::Name) || c->is(construct::String)) && from != nullptr && from != c)
                {
                    exported(export_name(c), c);
                    record({ "Y", export_name(c), request(from) });                         // (export * as name from)
                }
                else if (c->is(construct::ExportNamed))
                {
                    if (from != nullptr)
                    {
                        auto const r = request(from);
                        for (auto* spec : c->children)
                        {
                            exported(export_name(spec->children.back()), spec);
                            record({ "X", export_name(spec->children.back()), r, export_name(spec->child(0u)) });
                        }
                    }
                    else
                        for (auto* spec : c->children)
                        {
                            if (spec->child(0u)->is(construct::String))
                                error(spec->child(0u)->source, "A string export name requires 'from'");
                            auto const local = export_name(spec->child(0u));
                            auto const exportedName = export_name(spec->children.back());
                            exported(exportedName, spec);
                            if (iFunctions.front().top->find(local) == nullptr)
                                error(spec->child(0u)->source, "Export '" + local + "' is not defined in module");
                            if (auto const i = imports.find(local); i != imports.end() && i->second.ns)
                                record({ "Y", exportedName, i->second.request });   // (an imported namespace: as if re-exported from its module)
                            else if (i != imports.end())
                                record({ "X", exportedName, i->second.request, i->second.name });
                            else
                                record({ "E", exportedName, local });
                        }
                }
                else if (c->is(construct::ExportDefault) || c->is(construct::ClassExpression) || c->is(construct::FunctionExpression) ||
                    c->is(construct::AsyncFunctionExpression))
                {
                    exported("default", item);
                    record({ "E", "default", "*default*" });
                }
                else if (c->is(construct::Function) || c->is(construct::AsyncFunction) || c->is(construct::Class))
                {
                    auto const name = name_of(c->child(0u));
                    exported(default_export(item) ? "default" : name, item);
                    record({ "E", default_export(item) ? "default" : name, name });
                }
                else if (c->is(construct::Var) || c->is(construct::Let) || c->is(construct::Const) || c->is(construct::Using) || c->is(construct::AwaitUsing))
                {
                    std::vector<std::string> names;
                    for (auto* d : c->children)
                        bound_names(d->child(0u), names);
                    for (auto const& n : names)
                    {
                        exported(n, item);
                        record({ "E", n, n });
                    }
                }
            }
        }
        if (contains_await(aModule))
            record({ "A" });
        return result;
    }

    void function_generator::export_statement(node* aNode)
    {
        auto* c = aNode->child(0u);
        if (c == nullptr)
            return;
        if (c->is(construct::ExportDefault) || c->is(construct::ClassExpression))
        {
            // (export default expression (or class): the binding *default*)
            auto* value = c->is(construct::ExportDefault) ? c->child(0u) : c;
            named_evaluation(value, "default");
            initialize_binding(iFunction.top->find("*default*"));
            return;
        }
        if (c->is(construct::Var) || c->is(construct::Let) || c->is(construct::Const) || c->is(construct::Using) || c->is(construct::AwaitUsing) ||
            c->is(construct::Class) || c->is(construct::Function) || c->is(construct::AsyncFunction))
            statement(c);
    }
}
