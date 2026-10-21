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

    void function_generator::runtime(std::string const& aName, std::vector<type> const& aParameters, type aResult)
    {
        call(iCompiler.runtime_function("javascript::" + aName, aParameters, aResult));
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
                b->slot = local(rep == numeric_rep::F64 ? F64 : I64);
            if (b->kind == binding_kind::Internal || b->kind == binding_kind::Parameter || rep != numeric_rep::None)
                continue;
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
            i32(aTypeof ? 1 : 0);
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
        infer_numeric();
        begin(control_kind::Exit);
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
                enter_scope(iFunction.body);
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
            return;
        }
        if (iFunction.script)
        {
            iScopes.push_back(iFunction.top);
            for (auto& b : iFunction.top->bindings)
                if (!b->captured && b->lexical())
                {
                    auto const rep = rep_of(b.get());
                    b->slot = local(rep == numeric_rep::F64 ? F64 : I64);
                    if (rep != numeric_rep::None)
                        continue;
                    constant(values::Empty);
                    set(b->slot);
                }
            // (its declarations: var and function declarations are properties of the global object; lexical declarations bindings of the
            // global lexical environment)
            for (auto& b : iFunction.top->bindings)
            {
                if (b->lexical() && !b->captured && !iCompiler.iModule)
                    continue;
                key(b->name);
                i32(b->kind == binding_kind::Var ? 0 : b->kind == binding_kind::Function ? 1 : b->kind == binding_kind::Let ? 2 : 3);
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
        if (iFunction.argumentsBinding != nullptr || iFunction.top->bindings.size() > 0u)
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
            enter_scope(iFunction.body);
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
                        auto* outer = iFunction.top;
                        if (iFunction.script)
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
        if (numeric_condition(aNode))
            return;
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
        }
        else
            undefined();
        if (iFunction.async && iFunction.generator)
            await_value();  // (an async generator's return value is awaited)
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
            check();
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
            statement(finallyNode->child(0u));
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
        if (outer->script && iCompiler.eval_code())
        {
            // (eval code: the home object of the function that called eval)
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
        this_value(aNode);
        auto const thisValue = temporary();
        set(thisValue);
        if (aNode->is(construct::SuperDot))
            key(name_of(aNode->child(0u)));
        else
        {
            expression(aNode->child(0u));
            runtime("to_property_key", { I64 }, I64);
            check();
        }
        auto const keyValue = temporary();
        set(keyValue);
        home_object_value();
        get(keyValue);
        get(thisValue);
        runtime("super_get", { I64, I64, I64 }, I64);
        check();
        release(keyValue);
        release(thisValue);
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
        while (calleeNode->is(construct::Parenthesized) && (calleeNode->child(0u)->is(construct::Dot) || calleeNode->child(0u)->is(construct::Index) ||
            calleeNode->child(0u)->is(construct::Identifier)))
            calleeNode = calleeNode->child(0u);
        if (calleeNode->is(construct::Dot) || calleeNode->is(construct::Index) || calleeNode->is(construct::DotPrivate))
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
            for (auto* a : arguments)
            {
                if (a->is(construct::Spread))
                    iCompiler.error(a->source, "unsupported: spread arguments of eval");
                expression(a);
                auto const v = temporary();
                set(v);
                values.push_back(v);
            }
            get(callee);
            if (!values.empty())
                get(values[0]);
            else
                undefined();
            i32(static_cast<std::int32_t>(values.size()));
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
            release(thisValue);
            release(callee);
            return;
        }
        else
        {
            // (a with statement's object is the this of a function that is its property)
            expression(calleeNode);
            set(callee);
            undefined();
            set(thisValue);
            if (calleeNode->is(construct::Identifier))
            {
                auto const r = resolve(name_of(calleeNode), calleeNode);
                if (r.kind == reference::Dynamic)
                {
                    environment_value();
                    key(r.name);
                    runtime("dyn_this", { I64, I64 }, I64);
                    set(thisValue);
                }
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

    void function_generator::optional_chain(node* aNode)
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
            case construct::Call:
            case construct::OptionalCall:
                {
                    std::optional<std::uint32_t> thisLocal;
                    auto* calleeNode = n->child(0u);
                    bool const member = calleeNode->is(construct::Dot) || calleeNode->is(construct::Index) || calleeNode->is(construct::OptionalDot) ||
                        calleeNode->is(construct::OptionalIndex) || calleeNode->is(construct::DotPrivate) || calleeNode->is(construct::OptionalPrivate);
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
        link(aNode->child(0u), nullptr);
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
        std::string oper;
        for (auto c : between)
            if (!std::isspace(static_cast<unsigned char>(c)))
                oper.push_back(c);
        // (comments between them: the operator is what is not a comment)
        if (auto const p = oper.find("/*"); p != std::string::npos)
        {
            std::string cleaned;
            for (std::size_t i = 0u; i < oper.size(); ++i)
            {
                if (oper.compare(i, 2u, "/*") == 0)
                {
                    auto const e = oper.find("*/", i + 2u);
                    i = e == std::string::npos ? oper.size() : e + 1u;
                    continue;
                }
                cleaned.push_back(oper[i]);
            }
            oper = cleaned;
        }
        while (target->is(construct::Parenthesized) && (oper != "=" || (!target->child(0u)->is(construct::Object) && !target->child(0u)->is(construct::Array))))
            target = target->child(0u);
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
                if (r.kind == reference::Global && iFunction.strict)
                {
                    // (a strict assignment to an undeclared name: a ReferenceError, after the value is evaluated (but the binding is
                    // checked first))
                }
                if (numeric_assignment(r, valueNode, "="))
                    return;
                named_evaluation(valueNode, r.name);
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
                this_value(target);
                auto const thisValue = temporary();
                set(thisValue);
                if (target->is(construct::SuperDot))
                    key(name_of(target->child(0u)));
                else
                {
                    expression(target->child(0u));
                    runtime("to_property_key", { I64 }, I64);
                    check();
                }
                auto const keyValue = temporary();
                set(keyValue);
                expression(valueNode);
                auto const value = temporary();
                set(value);
                home_object_value();
                get(keyValue);
                get(value);
                get(thisValue);
                runtime("super_set", { I64, I64, I64, I64 }, I64);
                check_void();
                get(value);
                release(value);
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
            readTarget = [&]() { load(*r); };
            writeTarget = [&](std::uint32_t aValue) { get(aValue); store(*r, false, target); drop(); };
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
            this_value(target);
            thisValue = temporary();
            set(thisValue);
            if (target->is(construct::SuperDot))
                key(name_of(target->child(0u)));
            else
            {
                expression(target->child(0u));
                runtime("to_property_key", { I64 }, I64);
                check();
            }
            keyValue = temporary();
            set(keyValue);
            readTarget = [&]()
            {
                home_object_value();
                get(keyValue);
                get(thisValue);
                runtime("super_get", { I64, I64, I64 }, I64);
                check();
            };
            writeTarget = [&](std::uint32_t aValue)
            {
                home_object_value();
                get(keyValue);
                get(aValue);
                get(thisValue);
                runtime("super_set", { I64, I64, I64, I64 }, I64);
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
                named_evaluation(valueNode, r->name);
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
            load(r);
            compute();
            get(updated);
            store(r, false, target);
            drop();
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
            this_value(target);
            auto const thisValue = temporary();
            set(thisValue);
            if (target->is(construct::SuperDot))
                key(name_of(target->child(0u)));
            else
            {
                expression(target->child(0u));
                runtime("to_property_key", { I64 }, I64);
                check();
            }
            auto const keyValue = temporary();
            set(keyValue);
            home_object_value();
            get(keyValue);
            get(thisValue);
            runtime("super_get", { I64, I64, I64 }, I64);
            check();
            compute();
            home_object_value();
            get(keyValue);
            get(updated);
            get(thisValue);
            runtime("super_set", { I64, I64, I64, I64 }, I64);
            check_void();
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
                this_value(aTarget);
                auto const thisValue = temporary();
                set(thisValue);
                if (aTarget->is(construct::SuperDot))
                    key(name_of(aTarget->child(0u)));
                else
                {
                    expression(aTarget->child(0u));
                    runtime("to_property_key", { I64 }, I64);
                    check();
                }
                auto const keyValue = temporary();
                set(keyValue);
                home_object_value();
                get(keyValue);
                get(value);
                get(thisValue);
                runtime("super_set", { I64, I64, I64, I64 }, I64);
                check_void();
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
                get(source);
                get(excluded);
                runtime("object_rest", { I64, I64 }, I64);
                check();
                release(excluded);
                bind_element(p->child(0u), aBinding, aInitialize);
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
                    assign_target(p->child(0u));
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
            get(source);
            get(k);
            runtime("get", { I64, I64 }, I64);
            check();
            bind_element(target, aBinding, aInitialize);
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
                get(iterator);
                runtime("iterator_rest", { I64 }, I64);
                check();
                bind_element(e->child(0u), aBinding, aInitialize);
                continue;
            }
            get(iterator);
            runtime("iterator_value", { I64 }, I64);
            check();
            if (e->is(construct::Hole))
            {
                drop();
                continue;
            }
            bind_element(e, aBinding, aInitialize);
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
        // (an assignment's operator)
        std::string assignment_operator(node const* aNode)
        {
            auto const* target = aNode->child(0u);
            auto const* valueNode = aNode->child(1u);
            auto const between = std::string_view{ target->source.data() + target->source.size(),
                static_cast<std::size_t>(valueNode->source.data() - (target->source.data() + target->source.size())) };
            std::string result;
            for (std::size_t i = 0u; i < between.size(); ++i)
            {
                if (between.compare(i, 2u, "/*") == 0)
                {
                    auto const e = between.find("*/", i + 2u);
                    i = e == std::string_view::npos ? between.size() : e + 1u;
                    continue;
                }
                if (between.compare(i, 2u, "//") == 0)
                {
                    while (i < between.size() && between[i] != '\n')
                        ++i;
                    continue;
                }
                if (!std::isspace(static_cast<unsigned char>(between[i])))
                    result.push_back(between[i]);
            }
            return result;
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

    numeric_rep function_generator::num_type(node* aNode)
    {
        // (I64: an integer (an i64 local or an integer literal); F64: another number; None: any value)
        aNode = const_cast<node*>(unparenthesized(aNode));
        switch (aNode->kind)
        {
        case construct::Number:
            {
                auto const n = literal_number(aNode);
                if (!n)
                    return numeric_rep::None;
                return small_integer(n) ? numeric_rep::I64 : numeric_rep::F64;
            }
        case construct::Identifier:
            {
                auto const existing = iNumericNames.find(aNode);
                return existing != iNumericNames.end() ? rep_of(existing->second) : numeric_rep::None;
            }
        case construct::Negate:
        case construct::Plus:
            return num_type(aNode->child(0u)) != numeric_rep::None ? numeric_rep::F64 : numeric_rep::None;
        case construct::Add:
        case construct::Subtract:
        case construct::Multiply:
        case construct::Divide:
        case construct::Remainder:
            return num_type(aNode->child(0u)) != numeric_rep::None && num_type(aNode->child(1u)) != numeric_rep::None ? numeric_rep::F64 : numeric_rep::None;
        default:
            return numeric_rep::None;
        }
    }

    void function_generator::infer_numeric()
    {
        iNumeric.clear();
        iNumericNames.clear();
        static bool const sDisabled = std::getenv("NEOS_JS_NO_NUMERIC") != nullptr;
        if (sDisabled || iBody || iFunction.generator || iFunction.async)
            return;
        struct use
        {
            node* declarator = nullptr;
            int declarators = 0;
            bool eligible = true;
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
        bool inTarget = false;      // (a destructuring assignment's target or a for-in/of head: its identifiers are assigned any value)
        std::function<void(node*)> walk = [&](node* aNode)
        {
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
        // (eligibility: declared once, with an initializer, where it is reached before its uses)
        for (auto& [b, u] : uses)
        {
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
        }
        // (the types: integers (counters) and numbers, until none changes)
        for (auto& [b, u] : uses)
            if (u.eligible)
                iNumeric[b] = small_integer(literal_number(u.declarator->child(1u))) ? numeric_rep::I64 : numeric_rep::F64;
        for (bool changed = true; changed;)
        {
            changed = false;
            for (auto& [b, u] : uses)
            {
                auto const rep = rep_of(b);
                if (rep == numeric_rep::None)
                    continue;
                auto result = rep;
                auto const demote = [&](numeric_rep aTo)
                {
                    if (aTo == numeric_rep::None || (aTo == numeric_rep::F64 && result == numeric_rep::I64))
                        result = aTo;
                };
                if (num_type(u.declarator->child(1u)) == numeric_rep::None)
                    demote(numeric_rep::None);
                for (auto* a : u.assignments)
                {
                    auto const oper = assignment_operator(a);
                    auto* value = a->child(1u);
                    if (oper == "=" || oper == "+=" || oper == "&&=" || oper == "||=" || oper == "??=")
                    {
                        if (num_type(value) == numeric_rep::None)
                            demote(numeric_rep::None);
                        else if (!((oper == "+=" || oper == "=") && small_integer(literal_number(value))))
                            demote(numeric_rep::F64);
                        if (oper != "=" && oper != "+=")
                            demote(numeric_rep::F64);
                    }
                    else if (oper == "-=")
                    {
                        if (!small_integer(literal_number(value)))
                            demote(numeric_rep::F64);
                    }
                    else
                        demote(numeric_rep::F64);
                }
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
    }

    void function_generator::box(numeric_rep aRep)
    {
        if (aRep == numeric_rep::I64)
            op(opcode::F64SConvertI64);
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
    }

    void function_generator::numeric(node* aNode)
    {
        aNode = const_cast<node*>(unparenthesized(aNode));
        switch (aNode->kind)
        {
        case construct::Number:
            f64(*literal_number(aNode));
            return;
        case construct::Identifier:
            {
                auto* b = iNumericNames.at(aNode);
                get(b->slot);
                if (rep_of(b) == numeric_rep::I64)
                    op(opcode::F64SConvertI64);
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
            f64_remainder();
            return;
        default:
            break;
        }
        numeric(aNode->child(0u));
        numeric(aNode->child(1u));
        switch (aNode->kind)
        {
        case construct::Add: op(opcode::F64Add); break;
        case construct::Subtract: op(opcode::F64Sub); break;
        case construct::Multiply: op(opcode::F64Mul); break;
        case construct::Divide: op(opcode::F64Div); break;
        default: break;
        }
    }

    void function_generator::integer(node* aNode)
    {
        // (an integer (num_type I64): as an i64)
        aNode = const_cast<node*>(unparenthesized(aNode));
        if (aNode->is(construct::Number))
            i64(static_cast<std::int64_t>(*literal_number(aNode)));
        else
            get(iNumericNames.at(aNode)->slot);
    }

    void function_generator::counter_guard(std::uint32_t aSlot, std::uint32_t aOld, std::int64_t aStep)
    {
        // (an i64 counter beyond 2^53: as a double would be (its old value plus the step, rounded))
        get(aSlot);
        i64(9007199254740992ll);
        op(opcode::I64Add);
        i64(2 * 9007199254740992ll);
        op(opcode::I64GtU);
        op(opcode::If);
        iCode.push_back(0x40u);
        iControls.push_back(control{ control_kind::Block });
        get(aOld);
        op(opcode::F64SConvertI64);
        f64(static_cast<double>(aStep));
        op(opcode::F64Add);
        op(opcode::I64SConvertF64);
        set(aSlot);
        end_control();
    }

    void function_generator::f64_remainder()
    {
        // (two f64s: the remainder (as % computes it): integers (below 2^53, a divisor that is not 0): i64.rem_s (0 with the dividend's
        // sign); else the runtime's)
        auto const b = temporary(F64);
        set(b);
        auto const a = temporary(F64);
        set(a);
        get(a);
        op(opcode::F64Abs);
        f64(SafeInteger);
        op(opcode::F64Lt);
        get(a);
        get(a);
        op(opcode::F64Trunc);
        op(opcode::F64Eq);
        op(opcode::I32And);
        get(b);
        op(opcode::F64Abs);
        f64(SafeInteger);
        op(opcode::F64Lt);
        op(opcode::I32And);
        get(b);
        get(b);
        op(opcode::F64Trunc);
        op(opcode::F64Eq);
        op(opcode::I32And);
        get(b);
        f64(0.0);
        op(opcode::F64Ne);
        op(opcode::I32And);
        op(opcode::If);
        iCode.push_back(0x7Cu);     // (f64)
        iControls.push_back(control{ control_kind::Block });
        get(a);
        op(opcode::I64SConvertF64);
        get(b);
        op(opcode::I64SConvertF64);
        op(opcode::I64RemS);
        op(opcode::F64SConvertI64);
        get(a);
        op(opcode::F64CopySign);    // (a zero remainder has the dividend's sign)
        op(opcode::Else);
        get(a);
        op(opcode::I64ReinterpretF64);
        get(b);
        op(opcode::I64ReinterpretF64);
        runtime("remainder", { I64, I64 }, I64);
        check();
        op(opcode::F64ReinterpretI64);
        end_control();
        release(a);
        release(b);
    }

    bool function_generator::numeric_binary(node* aNode)
    {
        auto const lhs = num_type(aNode->child(0u));
        auto const rhs = num_type(aNode->child(1u));
        if (lhs == numeric_rep::None || rhs == numeric_rep::None)
            return false;
        std::optional<opcode> compare;
        std::optional<opcode> integerCompare;
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
        case construct::LessThan: compare = opcode::F64Lt; integerCompare = opcode::I64LtS; break;
        case construct::GreaterThan: compare = opcode::F64Gt; integerCompare = opcode::I64GtS; break;
        case construct::LessEqual: compare = opcode::F64Le; integerCompare = opcode::I64LeS; break;
        case construct::GreaterEqual: compare = opcode::F64Ge; integerCompare = opcode::I64GeS; break;
        case construct::StrictEqual:
        case construct::Equal: compare = opcode::F64Eq; integerCompare = opcode::I64Eq; break;
        case construct::StrictNotEqual:
        case construct::NotEqual: compare = opcode::F64Ne; integerCompare = opcode::I64Ne; break;
        default:
            return false;
        }
        if (lhs == numeric_rep::I64 && rhs == numeric_rep::I64)
        {
            integer(aNode->child(0u));
            integer(aNode->child(1u));
            op(*integerCompare);
        }
        else
        {
            numeric(aNode->child(0u));
            numeric(aNode->child(1u));
            op(*compare);
        }
        op(opcode::I64UConvertI32);
        constant(values::False);
        op(opcode::I64Add);
        return true;
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
        if (lhs == numeric_rep::I64 && rhs == numeric_rep::I64)
        {
            static std::map<construct, opcode> const sIntegerCompares = {
                { construct::LessThan, opcode::I64LtS }, { construct::GreaterThan, opcode::I64GtS }, { construct::LessEqual, opcode::I64LeS },
                { construct::GreaterEqual, opcode::I64GeS }, { construct::StrictEqual, opcode::I64Eq }, { construct::StrictNotEqual, opcode::I64Ne } };
            integer(n->child(0u));
            integer(n->child(1u));
            op(sIntegerCompares.at(n->kind));
            return true;
        }
        if (lhs != numeric_rep::None && rhs != numeric_rep::None)
        {
            numeric(n->child(0u));
            numeric(n->child(1u));
            op(*compare);
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
        if (rep == numeric_rep::None)
            return false;
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
        auto const old = temporary(I64);
        get(slot);
        tee(old);
        i64(aIncrement ? 1 : -1);
        op(opcode::I64Add);
        set(slot);
        counter_guard(slot, old, aIncrement ? 1 : -1);
        if (iDiscardedNow)
            constant(values::Undefined);
        else
        {
            get(aPostfix ? old : slot);
            box(numeric_rep::I64);
        }
        release(old);
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
        if (rep == numeric_rep::I64 && (aOperator == "+=" || aOperator == "-=") && small_integer(literal_number(aValue)))
        {
            auto const k = static_cast<std::int64_t>(*literal_number(aValue)) * (aOperator == "-=" ? -1 : 1);
            auto const old = temporary(I64);
            get(slot);
            tee(old);
            i64(k);
            op(opcode::I64Add);
            set(slot);
            counter_guard(slot, old, k);
            if (iDiscardedNow)
                constant(values::Undefined);
            else
            {
                get(slot);
                box(numeric_rep::I64);
            }
            release(old);
            return true;
        }
        if (aOperator == "=" && num_type(aValue) != numeric_rep::None)
        {
            if (rep == numeric_rep::I64)
            {
                i64(static_cast<std::int64_t>(*literal_number(aValue)));
                tee(slot);
                box(numeric_rep::I64);
                return true;
            }
            numeric(aValue);
            tee(slot);
            op(opcode::I64ReinterpretF64);
            return true;
        }
        if (rep == numeric_rep::F64 && num_type(aValue) != numeric_rep::None &&
            (aOperator == "+=" || aOperator == "-=" || aOperator == "*=" || aOperator == "/=" || aOperator == "%="))
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
                f64_remainder();
            tee(slot);
            op(opcode::I64ReinterpretF64);
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
            if (aNode->is(construct::Await) || aNode->is(construct::ForAwaitOf) || aNode->is(construct::AwaitUsing))
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
