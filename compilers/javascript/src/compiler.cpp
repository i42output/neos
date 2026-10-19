/*
  compiler.cpp

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

// JavaScript (ECMAScript) code generation
//
// Values are numbers (f64), strings and booleans; a variable, parameter or function result has one of these types,
// which is inferred: a variable has the type of the value it is declared with (or first assigned), a parameter the
// type of the arguments passed to it and a function the type of the values it returns. Operators convert their
// operands as JavaScript does (e.g. "a" + 1 is "a1", true + 1 is 2, "5" * 2 is 10).
//
// Top-level statements form the program's entry point; top-level declarations declare global variables (WASM
// globals) that functions can use; functions can be called before they are declared (as they are hoisted).

#include <neolib/neolib.hpp>
#include <algorithm>
#include <cstddef>
#include <limits>
#include <regex>
#include <neos/language/ecmascript.hpp>
#include <neos/bytecode/bytecode.hpp>
#include <neos/language/emit.hpp>
#include "compiler.hpp"

namespace neos::compilers::javascript
{
    using namespace emitter;
    using bytecode::opcode;

    namespace
    {
        constexpr double TwoTo53 = 9007199254740992.0;
        constexpr double TwoTo51 = 2251799813685248.0;
        // (an integer number's update step is at most this (in magnitude): its value is then at most 2^62 (an f64 add of a
        // step of at most 256 to a value of at least 2^62 (whose ulp is at least 1024) does not change it); see inference::integers)
        constexpr std::int64_t MaxIntegerStep = 256;
        constexpr std::int64_t ExactIntegerLimit = std::int64_t{ 1 } << 53;

        std::string_view trimmed(std::string_view aText)
        {
            while (!aText.empty() && std::isspace(static_cast<unsigned char>(aText.front())))
                aText.remove_prefix(1u);
            while (!aText.empty() && (std::isspace(static_cast<unsigned char>(aText.back())) || aText.back() == ';'))
                aText.remove_suffix(1u);
            return aText;
        }

        bool is_name_character(char aCharacter)
        {
            return std::isalnum(static_cast<unsigned char>(aCharacter)) || aCharacter == '_' || aCharacter == '$' ||
                static_cast<unsigned char>(aCharacter) >= 0x80u;
        }

        // the name at the start of aText (after any whitespace) and the text that follows it
        std::pair<std::string, std::string_view> leading_name(std::string_view aText)
        {
            while (!aText.empty() && !is_name_character(aText.front()))
                aText.remove_prefix(1u);
            std::size_t length = 0u;
            while (length < aText.size() && is_name_character(aText[length]))
                ++length;
            auto rest = aText.substr(length);
            while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front())))
                rest.remove_prefix(1u);
            return { std::string{ aText.substr(0u, length) }, rest };
        }

        std::string js_type_name(type aType)
        {
            switch (aType)
            {
            case type::String: return "string";
            case type::Boolean: return "boolean";
            case type::Void: return "undefined";
            default: return "number";
            }
        }

        // (names are parsed as names even if they are reserved words, e.g. a statement that is just "default")
        void check_name(std::string const& aName)
        {
            static std::set<std::string> const sReserved = { "break", "case", "catch", "class", "const", "continue", "debugger", "default", 
                "delete", "do", "else", "export", "extends", "false", "finally", "for", "function", "if", "import", "in", "instanceof", "let", 
                "new", "return", "super", "switch", "this", "throw", "true", "try", "typeof", "var", "void", "while", "with", "yield" };
            if (sReserved.contains(aName))
                throw std::runtime_error("unexpected '" + aName + "' (a reserved word" + 
                    (aName == "class" || aName == "new" || aName == "this" || aName == "try" || aName == "throw" ? ", not supported" : "") + ")");
        }

        bool is_static_namespace(std::string const& aName)
        {
            return aName == "console" || aName == "Math" || aName == "Number" || aName == "String" || aName == "Date" || aName == "performance";
        }

        void put_f64(bytes& aCode, double aValue)
        {
            put_op(aCode, opcode::F64Const);
            put_raw(aCode, aValue);
        }

        void put_i32(bytes& aCode, std::int32_t aValue)
        {
            put_op(aCode, opcode::I32Const);
            put_s64(aCode, aValue);
        }

        void put_i64(bytes& aCode, std::int64_t aValue)
        {
            put_op(aCode, opcode::I64Const);
            put_s64(aCode, aValue);
        }

        void put_local(bytes& aCode, opcode aOpcode, std::uint32_t aLocal)
        {
            put_op(aCode, aOpcode);
            put_u32(aCode, aLocal);
        }

        void put_block(bytes& aCode, opcode aOpcode, std::optional<type> aResult = {})
        {
            put_op(aCode, aOpcode);
            aCode.push_back(aResult ? *wasm_type(*aResult) : 0x40u);
        }

        // the regions of a for statement: [ init, test, update, body ] (positions within the statement)
        std::array<std::pair<std::size_t, std::size_t>, 4u> for_regions(std::string_view aText)
        {
            std::array<std::pair<std::size_t, std::size_t>, 4u> regions = {};
            std::size_t i = aText.find('(');
            std::size_t start = i + 1u;
            std::size_t part = 0u;
            int depth = 0;
            for (; i < aText.size(); ++i)
            {
                char const c = aText[i];
                if (c == '\'' || c == '\"' || c == '`')
                {
                    for (++i; i < aText.size() && aText[i] != c; ++i)
                        if (aText[i] == '\\')
                            ++i;
                    continue;
                }
                if (c == '/' && i + 1u < aText.size() && aText[i + 1u] == '/')
                {
                    i = aText.find('\n', i);
                    if (i == std::string_view::npos)
                        break;
                    continue;
                }
                if (c == '/' && i + 1u < aText.size() && aText[i + 1u] == '*')
                {
                    i = aText.find("*/", i + 2u);
                    if (i == std::string_view::npos)
                        break;
                    ++i;
                    continue;
                }
                if (c == '(' || c == '[' || c == '{')
                    ++depth;
                else if (c == ')' || c == ']' || c == '}')
                {
                    if (--depth == 0)
                    {
                        regions[part] = { start, i };
                        regions[3u] = { i + 1u, aText.size() };
                        break;
                    }
                }
                else if (c == ';' && depth == 1 && part < 2u)
                {
                    regions[part++] = { start, i };
                    start = i + 1u;
                }
            }
            return regions;
        }
    }

    // --- operands

    namespace
    {
        // code that is compiled with types that have since changed (it is replaced by the next compilation pass)
        fragment placeholder(fragment_kind aKind, type aType, neolib::i_string_view const& aSource)
        {
            auto result = make_fragment(aKind, aType, aSource);
            put_op(result.fragmentCode.as_std_vector(), opcode::Unreachable);
            return result;
        }
    }

    type compiler::js_type(operand_type const& aOperand) const
    {
        type t = type::UNKNOWN;
        if (aOperand.holds_alternative<i_fragment>())
            t = aOperand.get<i_fragment>().value_type();
        else if (aOperand.holds_alternative<i_symbol_reference>())
        {
            auto const& reference = aOperand.get<i_symbol_reference>();
            auto const* symbol = find_symbol(reference.scope(), neolib::string_view{ reference.name() });
            if (symbol == nullptr)
                throw std::runtime_error("'" + reference.name().to_std_string() + "' is not defined");
            if (symbol->type() == type::Function)
                throw std::runtime_error("function '" + reference.name().to_std_string() + "' used as a value");
            t = symbol->type();
        }
        else if (is_immediate(aOperand))
            t = get_immediate(aOperand).t;
        else
            t = static_cast<type>(aOperand.get<i_data_type>().index());
        if (t == type::String || t == type::Boolean || t == type::Void)
            return t;
        if (is_numeric(t))
            return type::F64;
        throw std::runtime_error("unsupported value of type " + type_name(t));
    }

    std::optional<type> compiler::integer_type(operand_type const& aOperand) const
    {
        // a number represented as a 32-bit integer (i32 or u32) rather than an f64
        type t = type::UNKNOWN;
        if (aOperand.holds_alternative<i_fragment>())
            t = aOperand.get<i_fragment>().value_type();
        else if (aOperand.holds_alternative<i_symbol_reference>())
        {
            auto const& reference = aOperand.get<i_symbol_reference>();
            if (auto const* symbol = find_symbol(reference.scope(), neolib::string_view{ reference.name() }))
                t = symbol->type();
        }
        if (t == type::I32 || t == type::U32)
            return t;
        return {};
    }

    std::uint32_t compiler::js_runtime(std::string const& aName, std::vector<type> const& aParameters, type aReturnType)
    {
        return runtime_function(aName, aParameters, aReturnType);
    }


    operand_type compiler::js_to_number(operand_type const& aOperand)
    {
        auto const t = js_type(aOperand);
        if (is_immediate(aOperand))
        {
            auto const value = get_immediate(aOperand);
            switch (t)
            {
            case type::Boolean:
                return operand_type{ data<f64>{ value.i != 0 ? 1.0 : 0.0 } };
            case type::String:
                return operand_type{ data<f64>{ ecmascript::string_to_number(value.s) } };
            default:
                return aOperand;
            }
        }
        switch (t)
        {
        case type::F64:
            // (a 32-bit integer is converted)
            if (integer_type(aOperand))
                return operand_type{ materialize(aOperand, type::F64) };
            return aOperand;
        case type::Boolean:
            return operand_type{ materialize(aOperand, type::F64) };
        case type::String:
            {
                fragment result = materialize(aOperand, type::String);
                emit_call_to(result, js_runtime("javascript::to_number", { type::String }, type::F64));
                result.valueType = type::F64;
                return operand_type{ result };
            }
        default:
            throw std::runtime_error("cannot convert " + js_type_name(t) + " to number");
        }
    }

    operand_type compiler::js_to_string(operand_type const& aOperand, bool aInspect)
    {
        auto const t = js_type(aOperand);
        if (t == type::String)
            return aOperand;
        if (is_immediate(aOperand))
        {
            auto const value = get_immediate(aOperand);
            if (t == type::Boolean)
                return operand_type{ data<string>{ string{ value.i != 0 ? "true" : "false" } } };
            double const number = is_float(value.t) ? value.f : static_cast<double>(value.i);
            return operand_type{ data<string>{ string{ aInspect && number == 0.0 && std::signbit(number) ?
                std::string{ "-0" } : ecmascript::number_to_string(number) } } };
        }
        if (t == type::Boolean)
        {
            fragment result;
            auto& code = result.fragmentCode.as_std_vector();
            put_i32(code, static_cast<std::int32_t>(string_literal("true")));
            put_i32(code, static_cast<std::int32_t>(string_literal("false")));
            append(result, materialize(aOperand, type::Boolean));
            put_op(code, opcode::Select);
            result.valueType = type::String;
            return operand_type{ result };
        }
        if (t == type::F64 && integer_range(aOperand))
        {
            // (a 32-bit integer: never -0; decimal)
            fragment result = materialize(aOperand, type::I32);
            convert(result.fragmentCode.as_std_vector(), type::I32, type::I64);
            emit_call_to(result, js_runtime("neos::string_from_i64", { type::I64 }, type::String));
            result.valueType = type::String;
            return operand_type{ result };
        }
        if (t == type::F64)
        {
            fragment result = materialize(aOperand, type::F64);
            emit_call_to(result, js_runtime(aInspect ? "javascript::inspect" : "javascript::to_string", { type::F64 }, type::String));
            result.valueType = type::String;
            return operand_type{ result };
        }
        throw std::runtime_error("cannot convert " + js_type_name(t) + " to string");
    }

    fragment compiler::js_to_boolean(operand_type const& aOperand)
    {
        auto const t = js_type(aOperand);
        fragment result;
        auto& code = result.fragmentCode.as_std_vector();
        result.valueType = type::Boolean;
        if (is_immediate(aOperand))
        {
            auto const value = get_immediate(aOperand);
            bool truthy = false;
            if (t == type::String)
                truthy = !value.s.empty();
            else if (is_float(value.t))
                truthy = value.f != 0.0 && !std::isnan(value.f);
            else
                truthy = value.i != 0;
            put_i32(code, truthy ? 1 : 0);
            return result;
        }
        switch (t)
        {
        case type::Boolean:
            return materialize(aOperand, type::Boolean);
        case type::F64:
            // (0, -0 and NaN are false)
            append(result, materialize(aOperand, type::F64));
            put_op(code, opcode::F64Abs);
            put_f64(code, 0.0);
            put_op(code, opcode::F64Gt);
            return result;
        case type::String:
            // (the empty string is false; a string's length precedes its characters)
            append(result, materialize(aOperand, type::String));
            put_op(code, opcode::I32LoadMem);
            put_u32(code, 2u);
            put_u32(code, 0u);
            put_i32(code, 0);
            put_op(code, opcode::I32Ne);
            return result;
        default:
            throw std::runtime_error("cannot convert " + js_type_name(t) + " to boolean");
        }
    }

    fragment compiler::js_to_int32(operand_type const& aOperand)
    {
        // (the result of a bitwise operator is an i32, or a u32, which is already a 32-bit integer, as is an i32 variable)
        if (integer_type(aOperand))
        {
            fragment result = materialize(aOperand);
            result.fragmentKind = fragment_kind::Value;
            result.valueType = type::I32;
            return result;
        }
        auto const number = js_to_number(aOperand);
        fragment result;
        auto& code = result.fragmentCode.as_std_vector();
        result.valueType = type::I32;
        if (is_immediate(number))
        {
            auto const value = get_immediate(number);
            put_i32(code, ecmascript::to_int32(is_float(value.t) ? value.f : static_cast<double>(value.i)));
            return result;
        }
        // trunc(x) + 1.5 * 2^52 has trunc(x) modulo 2^32 as the low 32 bits of its significand if |x| < 2^51 (values of 
        // greater magnitude, infinities and NaN are converted by the runtime)
        auto const x = temporary(type::F64);
        append(result, materialize(number, type::F64));
        put_local(code, opcode::LocalTee, x);
        put_op(code, opcode::F64Abs);
        put_f64(code, TwoTo51);
        put_op(code, opcode::F64Lt);
        put_block(code, opcode::If, type::I32);
        put_local(code, opcode::LocalGet, x);
        put_op(code, opcode::F64Trunc);
        put_f64(code, 6755399441055744.0);
        put_op(code, opcode::F64Add);
        put_op(code, opcode::I64ReinterpretF64);
        put_op(code, opcode::I32ConvertI64);
        put_op(code, opcode::Else);
        put_local(code, opcode::LocalGet, x);
        emit_call_to(result, js_runtime("javascript::to_int32", { type::F64 }, type::I32));
        put_op(code, opcode::End);
        return result;
    }

    namespace
    {
        std::int64_t constexpr Int32Min = std::numeric_limits<std::int32_t>::min();
        std::int64_t constexpr Int32Max = std::numeric_limits<std::int32_t>::max();
        std::int64_t constexpr Int64Min = std::numeric_limits<std::int64_t>::min();
        std::int64_t constexpr Int64Max = std::numeric_limits<std::int64_t>::max();

        std::int64_t isqrt(std::int64_t aValue)
        {
            // (-1 if there is no integer whose square is aValue or less)
            if (aValue < 0)
                return -1;
            auto root = static_cast<std::int64_t>(std::sqrt(static_cast<double>(aValue)));
            while (root * root > aValue)
                --root;
            while ((root + 1) * (root + 1) <= aValue)
                ++root;
            return root;
        }
    }

    // --- operators

    void compiler::emit_js_operation(construct aOperation)
    {
        bool const unary = (aOperation == construct::LogicalNot || aOperation == construct::BitwiseNot ||
            aOperation == construct::Negate || aOperation == construct::Plus || aOperation == construct::TypeOf);
        auto const rhs = pop_operand_value();
        std::optional<operand_type> lhs;
        if (!unary)
            lhs = pop_operand_value();
        if (declaration_pass())
        {
            fragment dummy;
            dummy.valueType = type::F64;
            push_operand(operand_type{ dummy });
            return;
        }
        auto const binary = [&](operation aGeneric, operand_type const& aLhs, operand_type const& aRhs)
        {
            push_operand(aLhs);
            push_operand(aRhs);
            emit_operation(aGeneric);
        };
        auto const constant_number = [&](double aValue)
        {
            // (a result that a literal cannot represent is computed at run time)
            if (!std::isfinite(aValue) || (aValue == 0.0 && std::signbit(aValue)))
                return false;
            push_operand(operand_type{ data<f64>{ aValue } });
            return true;
        };
        auto const number_of = [&](operand_type const& aOperand) -> std::optional<double>
        {
            if (!is_immediate(aOperand))
                return {};
            auto const value = get_immediate(aOperand);
            return is_float(value.t) ? value.f : static_cast<double>(value.i);
        };
        auto const discard = [&](operand_type const& aOperand, fragment& aTarget)
        {
            // (an operand whose value is not needed is evaluated for its side effects)
            if (aOperand.holds_alternative<i_fragment>())
            {
                append(aTarget, aOperand.get<i_fragment>());
                put_op(aTarget.fragmentCode.as_std_vector(), opcode::Drop);
            }
        };
        fragment result;
        auto& code = result.fragmentCode.as_std_vector();
        // numbers known to be 32-bit integers (not -0) whose result is also one (by range) are computed as i32
        auto const lr = lhs ? integer_range(*lhs) : std::optional<interval>{};
        auto const rr = integer_range(rhs);
        bool const integers = rr && (unary || lr) && !(is_immediate(rhs) && (unary || is_immediate(*lhs)));
        auto const int32_result = [&](opcode aOpcode, std::optional<interval> const& aRange)
        {
            if (aRange && (aRange->low < Int32Min || aRange->high > Int32Max))
                return false;
            if (lhs)
                append(result, materialize(*lhs, type::I32));
            append(result, materialize(rhs, type::I32));
            put_op(code, aOpcode);
            result.valueType = aRange ? type::I32 : type::Boolean;
            result.hasRange = aRange.has_value();
            result.rangeLow = aRange ? aRange->low : 0;
            result.rangeHigh = aRange ? aRange->high : 0;
            push_operand(operand_type{ result });
            return true;
        };
        if (integers)
        {
            switch (aOperation)
            {
            case construct::Add:
                if (int32_result(opcode::I32Add, interval{ lr->low + rr->low, lr->high + rr->high }))
                    return;
                break;
            case construct::Subtract:
                if (int32_result(opcode::I32Sub, interval{ lr->low - rr->high, lr->high - rr->low }))
                    return;
                break;
            case construct::Multiply:
                {
                    // (0 * a negative number is -0)
                    bool const negativeZero = (lr->low <= 0 && lr->high >= 0 && rr->low < 0) || (rr->low <= 0 && rr->high >= 0 && lr->low < 0);
                    auto const p1 = lr->low * rr->low, p2 = lr->low * rr->high, p3 = lr->high * rr->low, p4 = lr->high * rr->high;
                    if (!negativeZero && int32_result(opcode::I32Mul, interval{ std::min({ p1, p2, p3, p4 }), std::max({ p1, p2, p3, p4 }) }))
                        return;
                }
                break;
            case construct::Remainder:
                // (a non-negative dividend and a divisor that is not 0: the result is neither -0 nor NaN; unsigned if the 
                // divisor is positive too)
                if (lr->low >= 0 && (rr->low > 0 || rr->high < 0) &&
                    int32_result(rr->low > 0 ? opcode::I32RemU : opcode::I32RemS, 
                        interval{ 0, std::min(lr->high, std::max(std::abs(rr->low), std::abs(rr->high)) - 1) }))
                    return;
                break;
            case construct::LessThan:
                int32_result(opcode::I32LtS, std::nullopt);
                return;
            case construct::GreaterThan:
                int32_result(opcode::I32GtS, std::nullopt);
                return;
            case construct::LessThanOrEqual:
                int32_result(opcode::I32LeS, std::nullopt);
                return;
            case construct::GreaterThanOrEqual:
                int32_result(opcode::I32GeS, std::nullopt);
                return;
            case construct::Equal:
            case construct::StrictEqual:
                int32_result(opcode::I32Eq, std::nullopt);
                return;
            case construct::NotEqual:
            case construct::StrictNotEqual:
                int32_result(opcode::I32Ne, std::nullopt);
                return;
            case construct::Negate:
                // (-0 is not a 32-bit integer)
                if ((rr->low > 0 || rr->high < 0) && rr->low > Int32Min)
                {
                    put_op(code, opcode::I32Const);
                    put_s64(code, 0);
                    append(result, materialize(rhs, type::I32));
                    put_op(code, opcode::I32Sub);
                    result.valueType = type::I32;
                    result.hasRange = true;
                    result.rangeLow = -rr->high;
                    result.rangeHigh = -rr->low;
                    push_operand(operand_type{ result });
                    return;
                }
                break;
            default:
                break;
            }
        }
        switch (aOperation)
        {
        case construct::Add:
            if (js_type(*lhs) == type::String || js_type(rhs) == type::String)
                binary(operation::Add, js_to_string(*lhs), js_to_string(rhs));
            else
                binary(operation::Add, js_to_number(*lhs), js_to_number(rhs));
            return;
        case construct::Subtract:
            binary(operation::Subtract, js_to_number(*lhs), js_to_number(rhs));
            return;
        case construct::Multiply:
            binary(operation::Multiply, js_to_number(*lhs), js_to_number(rhs));
            return;
        case construct::Divide:
            binary(operation::Divide, js_to_number(*lhs), js_to_number(rhs));
            return;
        case construct::Remainder:
            {
                auto const a = js_to_number(*lhs);
                auto const b = js_to_number(rhs);
                auto const na = number_of(a);
                auto const nb = number_of(b);
                if (na && nb && constant_number(std::fmod(*na, *nb)))
                    return;
                // integers (of magnitude less than 2^53) a % b = a - b * trunc(a / b) (with the sign of a); otherwise fmod
                auto const ta = temporary(type::F64);
                auto const get = [&](std::uint32_t aLocal) { put_local(code, opcode::LocalGet, aLocal); };
                append(result, materialize(a, type::F64));
                put_local(code, opcode::LocalTee, ta);
                std::optional<std::uint32_t> tb;
                bool const integerDivisor = nb && std::trunc(*nb) == *nb && *nb != 0.0 && std::fabs(*nb) < TwoTo53;
                auto const divisor = [&]() { if (tb) get(*tb); else put_f64(code, *nb); };
                put_op(code, opcode::F64Abs);
                put_f64(code, TwoTo53);
                put_op(code, opcode::F64Lt);
                get(ta); put_op(code, opcode::F64Trunc); get(ta); put_op(code, opcode::F64Eq);
                put_op(code, opcode::I32And);
                if (!integerDivisor)
                {
                    tb = temporary(type::F64);
                    append(result, materialize(b, type::F64));
                    put_local(code, opcode::LocalTee, *tb);
                    put_op(code, opcode::F64Abs);
                    put_f64(code, TwoTo53);
                    put_op(code, opcode::F64Lt);
                    put_op(code, opcode::I32And);
                    get(*tb); put_op(code, opcode::F64Trunc); get(*tb); put_op(code, opcode::F64Eq);
                    put_op(code, opcode::I32And);
                    get(*tb); put_f64(code, 0.0); put_op(code, opcode::F64Ne);
                    put_op(code, opcode::I32And);
                }
                put_block(code, opcode::If, type::F64);
                get(ta); get(ta); divisor(); put_op(code, opcode::F64Div); put_op(code, opcode::F64Trunc);
                divisor(); put_op(code, opcode::F64Mul); put_op(code, opcode::F64Sub);
                get(ta); put_op(code, opcode::F64CopySign);
                put_op(code, opcode::Else);
                get(ta); divisor();
                emit_call_to(result, js_runtime("neos::fmod", { type::F64, type::F64 }, type::F64));
                put_op(code, opcode::End);
                result.valueType = type::F64;
                push_operand(operand_type{ result });
            }
            return;
        case construct::Power:
            {
                auto const a = js_to_number(*lhs);
                auto const b = js_to_number(rhs);
                auto const na = number_of(a);
                auto const nb = number_of(b);
                if (na && nb && constant_number(ecmascript::pow(*na, *nb)))
                    return;
                if (nb && *nb == 2.0)
                {
                    auto const t = temporary(type::F64);
                    append(result, materialize(a, type::F64));
                    put_local(code, opcode::LocalTee, t);
                    put_local(code, opcode::LocalGet, t);
                    put_op(code, opcode::F64Mul);
                }
                else
                {
                    append(result, materialize(a, type::F64));
                    append(result, materialize(b, type::F64));
                    emit_call_to(result, js_runtime("javascript::pow", { type::F64, type::F64 }, type::F64));
                }
                result.valueType = type::F64;
                push_operand(operand_type{ result });
            }
            return;
        case construct::LessThan:
        case construct::GreaterThan:
        case construct::LessThanOrEqual:
        case construct::GreaterThanOrEqual:
            {
                static std::map<construct, operation> const sGeneric = {
                    { construct::LessThan, operation::LessThan }, { construct::GreaterThan, operation::GreaterThan },
                    { construct::LessThanOrEqual, operation::LessThanOrEqual }, { construct::GreaterThanOrEqual, operation::GreaterThanOrEqual } };
                if (js_type(*lhs) == type::String && js_type(rhs) == type::String)
                    binary(sGeneric.at(aOperation), *lhs, rhs);
                else
                    binary(sGeneric.at(aOperation), js_to_number(*lhs), js_to_number(rhs));
            }
            return;
        case construct::Equal:
        case construct::NotEqual:
        case construct::StrictEqual:
        case construct::StrictNotEqual:
            {
                bool const equal = (aOperation == construct::Equal || aOperation == construct::StrictEqual);
                bool const strict = (aOperation == construct::StrictEqual || aOperation == construct::StrictNotEqual);
                auto l = *lhs;
                auto r = rhs;
                auto lt = js_type(l);
                auto rt = js_type(r);
                if (lt != rt && !strict)
                {
                    // (a boolean is compared as a number, a string and a number as numbers)
                    l = js_to_number(l);
                    r = js_to_number(r);
                    lt = rt = type::F64;
                }
                auto const integer = [&](operand_type const& aOperand, type aType)
                {
                    return integer_type(aOperand) == aType;
                };
                if (lt == type::F64 && rt == type::F64 && !(integer(l, type::I32) && integer(r, type::I32)) && 
                    !(integer(l, type::U32) && integer(r, type::U32)))
                {
                    // (numbers are compared as f64 unless both are 32-bit integers of the same signedness)
                    l = js_to_number(l);
                    r = js_to_number(r);
                }
                if (lt != rt)
                {
                    discard(l, result);
                    discard(r, result);
                    put_i32(code, equal ? 0 : 1);
                    result.valueType = type::Boolean;
                    push_operand(operand_type{ result });
                    return;
                }
                binary(equal ? operation::Equal : operation::NotEqual, l, r);
            }
            return;
        case construct::LogicalAnd:
        case construct::LogicalOr:
            {
                bool const isAnd = (aOperation == construct::LogicalAnd);
                auto const lt = js_type(*lhs);
                auto const rt = js_type(rhs);
                if (lt != rt)
                    throw std::runtime_error(std::string{ "the operands of " } + (isAnd ? "&&" : "||") + " have different types (" +
                        js_type_name(lt) + " and " + js_type_name(rt) + ")");
                if (is_immediate(*lhs))
                {
                    // (the value of the left operand decides which operand is the result)
                    auto const truth = js_to_boolean(*lhs).fragmentCode.as_std_vector().back() == 1u;
                    push_operand(isAnd == truth ? rhs : *lhs);
                    return;
                }
                if (lt == type::Boolean)
                {
                    binary(isAnd ? operation::LogicalAnd : operation::LogicalOr, *lhs, rhs);
                    return;
                }
                // the value of the operand that decides the result
                auto const t = temporary(lt);
                append(result, materialize(*lhs, lt));
                put_local(code, opcode::LocalSet, t);
                fragment test;
                put_local(test.fragmentCode.as_std_vector(), opcode::LocalGet, t);
                test.valueType = lt;
                append(result, js_to_boolean(operand_type{ test }));
                put_block(code, opcode::If, lt);
                if (isAnd)
                    append(result, materialize(rhs, lt));
                else
                    put_local(code, opcode::LocalGet, t);
                put_op(code, opcode::Else);
                if (isAnd)
                    put_local(code, opcode::LocalGet, t);
                else
                    append(result, materialize(rhs, lt));
                put_op(code, opcode::End);
                result.valueType = lt;
                push_operand(operand_type{ result });
            }
            return;
        case construct::LogicalNot:
            {
                auto const value = js_to_boolean(rhs);
                if (value.fragmentRelocations.empty() && value.fragmentCode.size() == 2u && value.fragmentCode[0] == 0x41u)
                {
                    push_operand(operand_type{ data<boolean>{ value.fragmentCode[1] == 0u } });
                    return;
                }
                append(result, value);
                put_op(code, opcode::I32Eqz);
                result.valueType = type::Boolean;
                push_operand(operand_type{ result });
            }
            return;
        case construct::BitwiseAnd:
        case construct::BitwiseOr:
        case construct::BitwiseXor:
        case construct::ShiftLeft:
        case construct::ShiftRight:
        case construct::ShiftRightUnsigned:
        case construct::BitwiseNot:
            {
                std::optional<std::int32_t> la;
                std::optional<std::int32_t> ra;
                auto const constant_int32 = [&](operand_type const& aOperand) -> std::optional<std::int32_t>
                {
                    auto const n = number_of(js_to_number(aOperand));
                    if (n)
                        return ecmascript::to_int32(*n);
                    return {};
                };
                ra = constant_int32(rhs);
                if (!unary)
                    la = constant_int32(*lhs);
                if (ra && (unary || la))
                {
                    std::int32_t const a = la.value_or(0);
                    std::int32_t const b = *ra;
                    auto const ua = static_cast<std::uint32_t>(a);
                    switch (aOperation)
                    {
                    case construct::BitwiseAnd: push_operand(operand_type{ data<f64>{ static_cast<double>(a & b) } }); break;
                    case construct::BitwiseOr: push_operand(operand_type{ data<f64>{ static_cast<double>(a | b) } }); break;
                    case construct::BitwiseXor: push_operand(operand_type{ data<f64>{ static_cast<double>(a ^ b) } }); break;
                    case construct::ShiftLeft: push_operand(operand_type{ data<f64>{ static_cast<double>(static_cast<std::int32_t>(ua << (b & 31))) } }); break;
                    case construct::ShiftRight: push_operand(operand_type{ data<f64>{ static_cast<double>(a >> (b & 31)) } }); break;
                    case construct::ShiftRightUnsigned: push_operand(operand_type{ data<f64>{ static_cast<double>(ua >> (b & 31)) } }); break;
                    case construct::BitwiseNot: push_operand(operand_type{ data<f64>{ static_cast<double>(~b) } }); break;
                    default: break;
                    }
                    return;
                }
                if (unary)
                {
                    append(result, js_to_int32(rhs));
                    put_i32(code, -1);
                    put_op(code, opcode::I32Xor);
                }
                else
                {
                    append(result, js_to_int32(*lhs));
                    append(result, js_to_int32(rhs));
                    switch (aOperation)
                    {
                    case construct::BitwiseAnd: put_op(code, opcode::I32And); break;
                    case construct::BitwiseOr: put_op(code, opcode::I32Ior); break;
                    case construct::BitwiseXor: put_op(code, opcode::I32Xor); break;
                    case construct::ShiftLeft: put_op(code, opcode::I32Shl); break;
                    case construct::ShiftRight: put_op(code, opcode::I32ShrS); break;
                    case construct::ShiftRightUnsigned: put_op(code, opcode::I32ShrU); break;
                    default: break;
                    }
                }
                // (a number: converted to f64 when it is used as one)
                result.valueType = aOperation == construct::ShiftRightUnsigned ? type::U32 : type::I32;
                push_operand(operand_type{ result });
            }
            return;
        case construct::Negate:
            push_operand(js_to_number(rhs));
            emit_operation(operation::Negate);
            return;
        case construct::Plus:
            push_operand(js_to_number(rhs));
            return;
        case construct::TypeOf:
            {
                auto const t = js_type(rhs);
                if (rhs.holds_alternative<i_fragment>())
                {
                    discard(rhs, result);
                    put_i32(code, static_cast<std::int32_t>(string_literal(js_type_name(t))));
                    result.valueType = type::String;
                    push_operand(operand_type{ result });
                }
                else
                    push_operand(operand_type{ data<string>{ string{ js_type_name(t) } } });
            }
            return;
        default:
            throw std::logic_error("neos::language::compiler::emit_js_operation");
        }
    }

    // --- constructs

    // --- the language compiler

    compiler::compiler() :
        language_compiler{ language::javascript::Language }
    {
    }

    void compiler::begin_compilation()
    {
        iInference = {};
    }

    void compiler::begin_pass()
    {
        iInference.arguments.clear();
        iInference.observed.clear();
        iInference.assignments.clear();
        iInference.counters.clear();
        iInference.steps.clear();
        iInference.declaredGlobals.clear();
        iRangeBound = std::nullopt;
        iSymbolInfo.clear();
    }

    void compiler::end_pass()
    {
        // number variables and parameters only assigned 32-bit integers can be i32
        update_ranges();
        // (an integer number's updates are rounded (beyond 2^53) in the directions of its steps)
        for (auto const& [key, steps] : iInference.steps)
            if (auto const fact = iInference.integers.find(key); fact != iInference.integers.end() && !fact->second.contains(steps))
            {
                fact->second = fact->second.join(steps);
                set_changed();
            }
    }

    void compiler::begin_fragment(i_source_fragment&)
    {
        iFragments.emplace_back();
    }

    void compiler::end_fragment()
    {
        iFragments.pop_back();
    }

    compiler::fragment_state& compiler::js()
    {
        return iFragments.back();
    }

    void compiler::emit_construct(std::uint32_t aConstruct, neolib::i_string_view const& aSource)
    {
        emit_js(static_cast<construct>(aConstruct), aSource);
    }

    type compiler::inferred_type(i_operand_type const& aOperand)
    {
        return js_type(operand_type{ aOperand });
    }

    type compiler::parameter_type(neolib::i_string_view const& aFunction, std::uint32_t aIndex, type aType)
    {
        // (a number parameter only passed 32-bit integers is an i32)
        if (aType == type::F64 && iInference.ranges.contains(range_key{ nullptr, aFunction.to_std_string() + "#" + std::to_string(aIndex) }))
            return type::I32;
        return aType;
    }

    bool compiler::value_range(i_operand_type const& aOperand, std::int64_t& aLow, std::int64_t& aHigh)
    {
        auto const range = integer_range(operand_type{ aOperand });
        if (!range)
            return false;
        aLow = range->low;
        aHigh = range->high;
        return true;
    }

    void compiler::emit_js(construct aConstruct, neolib::i_string_view const& aSource)
    {
        if (aConstruct >= construct::Add)
        {
            emit_js_operation(aConstruct);
            return;
        }
        switch (aConstruct)
        {
        case construct::Module:
            generator().end_indented();
            break;
        case construct::Function:
            generator().set_pending_inferred();
            break;
        case construct::Let:
        case construct::Const:
        case construct::Var:
        case construct::GlobalLet:
        case construct::GlobalConst:
        case construct::GlobalVar:
            js().jsDeclaration = aConstruct;
            break;
        case construct::Declarator:
            js_declarator(aSource);
            break;
        case construct::Statement:
            js_statement(aSource);
            break;
        case construct::EmptyStatement:
            push_operand(operand_type{ make_fragment(fragment_kind::Statement, type::Void, aSource) });
            break;
        case construct::GlobalStatement:
            js_global_statement(aSource);
            break;
        case construct::If:
            js_if(aSource);
            break;
        case construct::While:
        case construct::DoWhile:
        case construct::For:
            js_loop(aConstruct, aSource);
            break;
        case construct::ForTest:
            {
                auto const condition = pop_operand_value();
                if (!declaration_pass())
                    js_loop_test(aSource);
                fragment test = declaration_pass() ? fragment{} : js_to_boolean(condition);
                test.valueType = type::Boolean;
                test.fragmentSource = aSource;
                push_operand(operand_type{ test });
            }
            break;
        case construct::Return:
            generator().emit_return(aSource);
            break;
        case construct::Break:
        case construct::Continue:
            js_branch(aConstruct, aSource);
            break;
        case construct::Assignment:
        case construct::PrefixIncrement:
        case construct::PrefixDecrement:
        case construct::PostfixIncrement:
        case construct::PostfixDecrement:
            js_assignment(aConstruct, aSource);
            break;
        case construct::Conditional:
            js_conditional(aSource);
            break;
        case construct::Call:
            js_call(aSource);
            break;
        case construct::Member:
        case construct::Index:
            js_member(aConstruct, aSource);
            break;
        case construct::Variable:
            js_variable(aSource);
            break;
        case construct::Switch:
            js_switch(aSource);
            break;
        case construct::Case:
        case construct::DefaultCase:
            js_case(aConstruct, aSource);
            break;
        case construct::TemplateBegin:
            push_operand(operand_type{ data<string>{ string{} } });
            break;
        case construct::TemplateSubstitution:
            if (declaration_pass())
                emit_js_operation(construct::Add);
            else
            {
                push_operand(js_to_string(pop_operand_value()));
                emit_js_operation(construct::Add);
            }
            break;
        default:
            break;
        }
    }


    // --- integer ranges

    std::optional<compiler::interval> compiler::integer_range(operand_type const& aOperand) const
    {
        // the range of a number known to be a 32-bit integer (and not -0)
        interval const int32{ Int32Min, Int32Max };
        if (aOperand.holds_alternative<i_fragment>())
        {
            auto const& value = aOperand.get<i_fragment>();
            if (value.has_range() && (value.value_type() == type::I32 || value.kind() == fragment_kind::Argument))
                return interval{ value.range_low(), value.range_high() };
            if (value.value_type() == type::I32)
                return int32;
            return {};
        }
        if (aOperand.holds_alternative<i_symbol_reference>())
        {
            auto const& reference = aOperand.get<i_symbol_reference>();
            auto const* symbol = find_symbol(reference.scope(), neolib::string_view{ reference.name() });
            if (symbol == nullptr || symbol->type() != type::I32)
                return {};
            auto result = int32;
            if (auto const key = range_key_of(*symbol, reference.name().to_std_string()))
            {
                if (auto const fact = iInference.ranges.find(*key); fact != iInference.ranges.end())
                    result = fact->second;
                if (iRangeBound && iRangeBound->first == *key)
                {
                    // (a loop counter when it is updated)
                    auto const low = std::max(result.low, iRangeBound->second.low);
                    auto const high = std::min(result.high, iRangeBound->second.high);
                    if (low <= high)
                        result = interval{ low, high };
                    else
                        result = interval{ std::clamp(low, result.low, result.high), std::clamp(low, result.low, result.high) };
                }
            }
            return result;
        }
        if (!is_immediate(aOperand))
            return {};
        auto const value = get_immediate(aOperand);
        if (value.t == type::String || value.t == type::Boolean)
            return {};
        double const d = is_float(value.t) ? value.f : static_cast<double>(value.i);
        if (std::trunc(d) != d || d < static_cast<double>(Int32Min) || d > static_cast<double>(Int32Max) || (d == 0.0 && std::signbit(d)))
            return {};
        return interval{ static_cast<std::int64_t>(d), static_cast<std::int64_t>(d) };
    }

    std::optional<compiler::range_key> compiler::range_key_of(i_symbol_table_entry const& aSymbol, std::string const& aName) const
    {
        // a variable is identified by its declaration; a parameter by its function and index
        auto const info = iSymbolInfo.find(&aSymbol);
        if (info != iSymbolInfo.end() && info->second.site != nullptr)
            return range_key{ info->second.site, {} };
        if (!in_function() || implicit_entry())
            return {};
        auto const& signature = generator().signature_of(current_function());
        if (signature.parameters().has_value())
            for (std::size_t i = 0u; i < signature.parameters().value().size(); ++i)
                if (signature.parameters().value()[i].parameter_name() == aName)
                    return range_key{ nullptr, generator().function_name(current_function()).to_std_string() + "#" + std::to_string(i) };
        return {};
    }

    void compiler::observe_range(range_key const& aKey, std::optional<interval> const& aRange)
    {
        if (!aRange)
        {
            // (not a 32-bit integer: an f64 from now on)
            iInference.notInt32.insert(aKey);
            if (iInference.ranges.erase(aKey) != 0u)
                set_changed();
            return;
        }
        auto const observed = iInference.observed.find(aKey);
        if (observed == iInference.observed.end())
            iInference.observed.emplace(aKey, *aRange);
        else
            observed->second = observed->second.join(*aRange);
        auto const fact = iInference.ranges.find(aKey);
        if (fact != iInference.ranges.end() && !fact->second.contains(*aRange))
            set_changed();
    }

    void compiler::update_ranges()
    {
        // (after many passes a range that changes is no longer an integer range; so the passes end)
        bool const converge = generator().inference_pass() >= 24u;
        // a loop counter's update must be its only assignment
        for (auto const& counter : iInference.counters)
            if (iInference.assignments[counter] != 1u && iInference.notCounters.insert(counter).second)
                set_changed();
        for (auto const& [key, observed] : iInference.observed)
        {
            if (iInference.notInt32.contains(key))
                continue;
            auto const fact = iInference.ranges.find(key);
            if (fact == iInference.ranges.end())
            {
                // (a variable's range is created when it is declared; a parameter's here)
                if (key.first == nullptr)
                {
                    iInference.ranges.emplace(key, observed);
                    set_changed();
                }
                continue;
            }
            if (!fact->second.contains(observed))
            {
                set_changed();
                if (converge)
                {
                    iInference.notInt32.insert(key);
                    iInference.ranges.erase(fact);
                    continue;
                }
                // grow (and widen if it grows again)
                auto range = fact->second.join(observed);
                if (++iInference.grown[key] > 1u)
                {
                    if (observed.low < fact->second.low)
                        range.low = Int32Min;
                    if (observed.high > fact->second.high)
                        range.high = Int32Max;
                }
                fact->second = range;
            }
            else if (observed != fact->second && !converge && iInference.narrowed.insert(key).second)
            {
                // narrow (once)
                fact->second = observed;
                set_changed();
            }
        }
    }

    void compiler::js_loop_test(neolib::i_string_view const& aSource)
    {
        // a loop whose test is "v < E", "v <= E", "v > E", "v >= E", "v * v <= E" or "v * v < E" (or one of these && ...) 
        // bounds the value of the counter v when its (only) assignment, an update by a positive integer constant at most once 
        // an iteration, happens
        js().counterLoops.push_back(fragment_state::counter_loop{ aSource.to_std_string_view().data() });
        auto& loop = js().counterLoops.back();
        auto const text = aSource.to_std_string_view();
        if (text.find("||") != std::string_view::npos || text.find('?') != std::string_view::npos)
            return;
        std::string const first{ text.substr(0u, text.find("&&")) };
        static std::regex const sTest{ R"(^\s*([A-Za-z_$][\w$]*)\s*(\*\s*([A-Za-z_$][\w$]*)\s*)?(<=|<|>=|>)\s*([A-Za-z_$][\w$]*|[0-9]{1,10})\s*$)" };
        std::smatch match;
        if (!std::regex_match(first, match, sTest))
            return;
        std::string const counter = match[1].str();
        bool const square = match[2].matched;
        if ((square && match[3].str() != counter) || match[5].str() == counter)
            return;
        auto const* symbol = find_symbol(current_scope(), neolib::string_view{ counter });
        if (symbol == nullptr || symbol->type() == type::Function)
            return;
        auto const info = iSymbolInfo.find(symbol);
        if (info == iSymbolInfo.end() || info->second.site == nullptr || iInference.notCounters.contains(info->second.site))
            return;
        std::optional<interval> limit;
        auto const& e = match[5].str();
        if (e[0] >= '0' && e[0] <= '9')
            limit = interval{ std::stoll(e), std::stoll(e) };
        else
            limit = integer_range(operand_type{ symbol_reference{ current_scope(), neolib::string{ e } } });
        if (!limit)
            return;
        auto const op = match[4].str();
        loop.counter = info->second.site;
        if (square)
        {
            if (op == "<=" || op == "<")
            {
                auto const root = isqrt(op == "<=" ? limit->high : limit->high - 1);
                loop.maximum = root;
                loop.minimum = -root;
            }
        }
        else if (op == "<")
            loop.maximum = limit->high - 1;
        else if (op == "<=")
            loop.maximum = limit->high;
        else if (op == ">")
            loop.minimum = limit->low + 1;
        else
            loop.minimum = limit->low;
    }


    void compiler::js_declarator(neolib::i_string_view const& aSource)
    {
        auto const [name, rest] = leading_name(aSource.to_std_string_view());
        check_name(name);
        bool const hasValue = !rest.empty() && rest.front() == '=';
        auto const kind = js().jsDeclaration.value_or(construct::Let);
        bool const declaredGlobal = (kind == construct::GlobalLet || kind == construct::GlobalConst || kind == construct::GlobalVar);
        // (a top-level variable that no function uses is a local of the entry point)
        bool const global = declaredGlobal && (declaration_pass() || !generator().entry_global(neolib::string_view{ name }));
        bool const constant = (kind == construct::Const || kind == construct::GlobalConst);
        bool const hoisted = (kind == construct::Var || kind == construct::GlobalVar) || (declaredGlobal && !global);
        if (declaredGlobal)
        {
            // (a top-level let or const cannot be redeclared)
            bool const isVar = (kind == construct::GlobalVar);
            auto const previous = iInference.declaredGlobals.find(name);
            if (previous != iInference.declaredGlobals.end() && (!isVar || !previous->second))
                throw std::runtime_error("identifier '" + name + "' has already been declared");
            iInference.declaredGlobals[name] = isVar;
        }
        std::optional<operand_type> value;
        if (hasValue)
            value = pop_operand_value();
        else if (constant)
            throw std::runtime_error("missing initializer in const declaration of '" + name + "'");
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        if (declaration_pass())
        {
            if (global)
            {
                bool known = false;
                for (std::size_t g = 0u; g < generator().global_count(); ++g)
                    known = known || generator().global_name(g).to_std_string_view() == name;
                if (!known)
                    generator().add_global(neolib::string{ name }, type::F64);
            }
            else
                ensure_function();
            push_operand(operand_type{ statement });
            return;
        }
        // the variable's type is that of its value (or of the value first assigned to it)
        auto const position = aSource.to_std_string_view().data();
        type t = type::F64;
        if (value)
            t = js_type(*value);
        else if (auto const fact = iInference.declarations.find(position); fact != iInference.declarations.end())
            t = fact->second;
        if (t == type::Void)
            throw std::runtime_error("'" + name + "' cannot be declared with a value of undefined");
        // a number variable that is only assigned 32-bit integers (in a range) is an i32
        if (t == type::F64 && kind != construct::Var && kind != construct::GlobalVar)
        {
            range_key const key{ position, {} };
            auto const range = value ? integer_range(*value) : std::optional<interval>{ interval{ 0, 0 } };
            // (or, if it is an integer number, an i64)
            if (range && !iInference.notInt32.contains(key) && iInference.integers.contains(key) && !global)
                t = type::I64;
            else if (range && !iInference.notInt32.contains(key))
            {
                iInference.ranges.emplace(key, *range);
                observe_range(key, range);
                t = type::I32;
            }
        }
        i_symbol_table_entry* symbol = nullptr;
        if (global)
        {
            symbol = find_symbol(generator().global_scope(), neolib::string_view{ name }, true);
            if (symbol == nullptr)
            {
                infer("'" + name + "'", {}, t);
                symbol = &define_global(name, t);
            }
            else if (symbol->type() != t)
            {
                for (std::size_t g = 0u; g < generator().global_count(); ++g)
                    if (generator().global_name(g).to_std_string_view() == name)
                        generator().set_global_type(g, t);
                infer("'" + name + "'", symbol->type(), t);
                push_operand(operand_type{ placeholder(fragment_kind::Statement, type::Void, aSource) });
                return;
            }
        }
        else
        {
            ensure_function();
            if (hoisted)
            {
                // (a var redeclared in the same function is the same variable)
                auto* existing = find_symbol(current_scope(), neolib::string_view{ name }, true);
                if (existing != nullptr && !generator().is_block_scoped(*existing) && existing->type() == t)
                    symbol = existing;
            }
            if (symbol == nullptr)
            {
                generator().define_symbol(neolib::string{ name }, t, aSource);
                symbol = find_symbol(current_scope(), neolib::string_view{ name }, true);
                if (hoisted)
                    generator().set_block_scoped(*symbol, false);
            }
        }
        auto& info = iSymbolInfo[symbol];
        generator().set_constant(*symbol, false);
        info.declaration = value ? nullptr : position;
        info.site = position;
        // (a variable declared without a value is initialized to the zero of its type; JavaScript's undefined)
        if (!value)
            value = t == type::String ? operand_type{ data<string>{ string{} } } : t == type::Boolean ?
                operand_type{ data<boolean>{ false } } : operand_type{ data<f64>{ 0.0 } };
        push_operand(*value);
        generator().emit_assignment(neolib::string_view{ name }, aSource);
        generator().set_constant(*symbol, constant);
    }

    void compiler::js_statement(neolib::i_string_view const& aSource)
    {
        // an assignment or update whose value is unused needs only its effect
        auto stack = operands();
        if (!declaration_pass() && !stack.empty() && stack.back().holds_alternative<i_fragment>() &&
            stack.back().get<i_fragment>().kind() == fragment_kind::Value && !stack.back().get<i_fragment>().source().empty())
        {
            auto const form = js().jsStatementForms.find(stack.back().get<i_fragment>().source().to_std_string_view().data());
            if (form != js().jsStatementForms.end() && form->second.fragmentSource == stack.back().get<i_fragment>().source())
            {
                auto statement = form->second;
                statement.fragmentSource = aSource;
                stack.set_back(operand_type{ statement });
                return;
            }
        }
        generator().emit_statement(aSource);
    }

    void compiler::js_global_statement(neolib::i_string_view const& aSource)
    {
        // a top-level statement is part of the entry point
        auto statements = pop_fragments(aSource, fragment_kind::Statement);
        ensure_function();
        for (auto const& statement : statements)
            push_operand(operand_type{ statement });
    }

    void compiler::js_assignment(construct aConstruct, neolib::i_string_view const& aSource)
    {
        auto const text = aSource.to_std_string_view();
        auto const [name, rest] = leading_name(text);
        check_name(name);
        std::optional<construct> compound;
        if (aConstruct == construct::Assignment)
        {
            static std::vector<std::pair<std::string_view, std::optional<construct>>> const sOperators = {
                { ">>>=", construct::ShiftRightUnsigned }, { "**=", construct::Power }, { "<<=", construct::ShiftLeft },
                { ">>=", construct::ShiftRight }, { "+=", construct::Add }, { "-=", construct::Subtract },
                { "*=", construct::Multiply }, { "/=", construct::Divide }, { "%=", construct::Remainder },
                { "&=", construct::BitwiseAnd }, { "|=", construct::BitwiseOr }, { "^=", construct::BitwiseXor },
                { "=", std::nullopt } };
            for (auto const& o : sOperators)
                if (rest.starts_with(o.first))
                {
                    compound = o.second;
                    break;
                }
        }
        else
            compound = (aConstruct == construct::PrefixIncrement || aConstruct == construct::PostfixIncrement) ?
                construct::Add : construct::Subtract;
        bool const update = (aConstruct != construct::Assignment);
        bool const postfix = (aConstruct == construct::PostfixIncrement || aConstruct == construct::PostfixDecrement);
        std::optional<operand_type> value;
        if (!update)
            value = pop_operand_value();
        if (declaration_pass())
        {
            fragment dummy;
            dummy.valueType = type::F64;
            push_operand(operand_type{ dummy });
            return;
        }
        auto* const symbol = find_symbol(current_scope(), neolib::string_view{ name });
        if (symbol == nullptr)
            throw std::runtime_error("'" + name + "' is not defined");
        if (symbol->type() == type::Function)
            throw std::runtime_error("cannot assign to function '" + name + "'");
        if (is_global(*symbol) && in_function() && !implicit_entry())
            generator().function_global(neolib::string{ name });
        auto& info = iSymbolInfo[symbol];
        if (generator().is_constant(*symbol))
            throw std::runtime_error("assignment to constant variable '" + name + "'");
        if (info.site != nullptr)
            ++iInference.assignments[info.site];
        bool const global = is_global(*symbol);
        // (an i32 or i64 variable is a number)
        type const variableType = symbol->type() == type::I32 || symbol->type() == type::I64 ? type::F64 : symbol->type();
        if (update && variableType != type::F64)
            throw std::runtime_error("'" + name + "' is a " + js_type_name(symbol->type()) + " (not a number)");
        // (an update by a small integer step: see inference::integers)
        std::optional<interval> step;
        if (compound == construct::Add || compound == construct::Subtract)
            if (auto const s = update ? std::optional<interval>{ interval{ 1, 1 } } : integer_range(*value))
            {
                interval const signedStep = *compound == construct::Add ? *s : interval{ -s->high, -s->low };
                if (signedStep.low >= -MaxIntegerStep && signedStep.high <= MaxIntegerStep)
                    step = signedStep;
            }
        if (symbol->type() == type::I64)
        {
            // an integer number: an update is an i64 add (rounded, in cold code, as an f64 add would be if beyond 2^53); any
            // other value assigned to it must be a 32-bit integer (else it is an f64)
            auto const key = *range_key_of(*symbol, name);
            if (compound ? !step : !integer_range(*value))
            {
                iInference.integers.erase(key);
                iInference.notInt32.insert(key);
                set_changed();
                infer("'" + name + "'", type::I64, type::F64);
                push_operand(operand_type{ placeholder(fragment_kind::Value, type::F64, aSource) });
                return;
            }
            auto const index = symbol->index();
            auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
            auto& code = statement.fragmentCode.as_std_vector();
            if (compound)
            {
                put_local(code, opcode::LocalGet, index);
                if (update)
                    put_i64(code, 1);
                else
                    append(statement, materialize(*value, type::I64));
                put_op(code, *compound == construct::Add ? opcode::I64Add : opcode::I64Sub);
                put_local(code, opcode::LocalSet, index);
                if (auto const steps = iInference.steps.find(key); steps != iInference.steps.end())
                    steps->second = steps->second.join(*step);
                else
                    iInference.steps.emplace(key, *step);
                auto const fact = iInference.integers.find(key);
                auto const directions = fact != iInference.integers.end() ? fact->second.join(*step) : *step;
                if (directions.high > 0 || directions.low < 0)
                {
                    // (the rounding is cold code (see the JIT's cold tails): it follows a block that ends with a branch out of
                    // the block that contains both)
                    put_block(code, opcode::Block);
                    put_block(code, opcode::Block);
                    if (directions.high > 0)
                    {
                        put_local(code, opcode::LocalGet, index);
                        put_i64(code, ExactIntegerLimit);
                        put_op(code, opcode::I64GtS);
                        put_op(code, opcode::BrIf);
                        put_u32(code, 0u);
                    }
                    if (directions.low < 0)
                    {
                        put_local(code, opcode::LocalGet, index);
                        put_i64(code, -ExactIntegerLimit);
                        put_op(code, opcode::I64LtS);
                        put_op(code, opcode::BrIf);
                        put_u32(code, 0u);
                    }
                    put_op(code, opcode::Br);
                    put_u32(code, 1u);
                    put_op(code, opcode::End);
                    put_local(code, opcode::LocalGet, index);
                    put_op(code, opcode::F64SConvertI64);
                    put_op(code, opcode::I64SConvertF64);
                    put_local(code, opcode::LocalSet, index);
                    put_op(code, opcode::End);
                }
            }
            else
            {
                append(statement, materialize(*value, type::I64));
                put_local(code, opcode::LocalSet, index);
            }
            // the value of the expression (an f64)
            auto expression = make_fragment(fragment_kind::Value, type::F64, aSource);
            auto const get = [&]()
            {
                put_local(expression.fragmentCode.as_std_vector(), opcode::LocalGet, index);
                put_op(expression.fragmentCode.as_std_vector(), opcode::F64SConvertI64);
            };
            if (postfix)
                get();
            append(expression, statement);
            if (!postfix)
                get();
            js().jsStatementForms[text.data()] = statement;
            push_operand(operand_type{ expression });
            return;
        }
        if (compound)
        {
            // a loop counter's value when it is updated by a positive integer constant is bounded by its loop's test
            auto const step = update ? std::optional<interval>{ interval{ 1, 1 } } : integer_range(*value);
            auto const& loops = js().counterLoops;
            if (info.site != nullptr && step && step->low > 0 && !loops.empty() && loops.back().counter == info.site &&
                !iInference.notCounters.contains(info.site))
            {
                auto const& loop = loops.back();
                if (*compound == construct::Add && loop.maximum)
                    iRangeBound.emplace(range_key{ info.site, {} }, interval{ Int64Min, *loop.maximum });
                else if (*compound == construct::Subtract && loop.minimum)
                    iRangeBound.emplace(range_key{ info.site, {} }, interval{ *loop.minimum, Int64Max });
                if (iRangeBound)
                {
                    iInference.counters.insert(info.site);
                    js().counterUpdates.push_back(fragment_state::counter_update{ info.site, loop.test, text.data() });
                }
            }
            push_operand(operand_type{ symbol_reference{ current_scope(), neolib::string{ name } } });
            push_operand(update ? operand_type{ data<f64>{ 1.0 } } : *value);
            emit_js_operation(*compound);
            iRangeBound = std::nullopt;
            value = pop_operand_value();
        }
        auto const t = js_type(*value);
        if (t == type::F64 && symbol->type() == type::I32)
        {
            auto const range = integer_range(*value);
            auto const key = range_key_of(*symbol, name);
            if (!range && step && key && key->first != nullptr && !global)
            {
                // (an update by a small integer step whose value is not a 32-bit integer: an integer number (an i64))
                iInference.ranges.erase(*key);
                iInference.integers.emplace(*key, *step);
                set_changed();
                infer("'" + name + "'", type::I32, type::I64);
                push_operand(operand_type{ placeholder(fragment_kind::Value, t, aSource) });
                return;
            }
            if (key)
                observe_range(*key, range);
            if (!range)
            {
                infer("'" + name + "'", type::I32, type::F64);
                push_operand(operand_type{ placeholder(fragment_kind::Value, t, aSource) });
                return;
            }
        }
        if (t != variableType)
        {
            // a variable declared without a value has the type of the value first assigned to it
            if (info.declaration != nullptr && !iInference.declarations.contains(info.declaration))
            {
                iInference.declarations[info.declaration] = t;
                infer("'" + name + "'", symbol->type(), t);
                push_operand(operand_type{ placeholder(fragment_kind::Value, t, aSource) });
                return;
            }
            throw std::runtime_error("cannot assign a " + js_type_name(t) + " to '" + name + "' (a " + js_type_name(variableType) + ")");
        }
        if (info.declaration != nullptr && !iInference.declarations.contains(info.declaration))
            iInference.declarations[info.declaration] = t;
        auto const index = symbol->index();
        auto const get = [&](fragment& aTarget) { put_local(aTarget.fragmentCode.as_std_vector(), global ? opcode::GlobalGet : opcode::LocalGet, index); };
        auto const set = [&](fragment& aTarget) { put_local(aTarget.fragmentCode.as_std_vector(), global ? opcode::GlobalSet : opcode::LocalSet, index); };
        auto const valueCode = materialize(*value, symbol->type());
        // the value of the expression
        auto expression = make_fragment(fragment_kind::Value, symbol->type(), aSource);
        if (postfix)
        {
            get(expression);
            append(expression, valueCode);
            set(expression);
        }
        else
        {
            append(expression, valueCode);
            if (global)
            {
                set(expression);
                get(expression);
            }
            else
                put_local(expression.fragmentCode.as_std_vector(), opcode::LocalTee, index);
        }
        // its effect (if its value is unused)
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        append(statement, valueCode);
        set(statement);
        js().jsStatementForms[text.data()] = statement;
        push_operand(operand_type{ expression });
    }

    void compiler::js_variable(neolib::i_string_view const& aSource)
    {
        auto const name = std::string{ trimmed(aSource.to_std_string_view()) };
        check_name(name);
        auto const* const symbol = declaration_pass() ? nullptr : find_symbol(current_scope(), neolib::string_view{ name });
        if (symbol != nullptr && is_global(*symbol) && in_function() && !implicit_entry())
            generator().function_global(neolib::string{ name });
        if (!declaration_pass() && symbol == nullptr)
        {
            if (name == "NaN")
            {
                push_operand(operand_type{ data<f64>{ std::numeric_limits<double>::quiet_NaN() } });
                return;
            }
            if (name == "Infinity")
            {
                push_operand(operand_type{ data<f64>{ std::numeric_limits<double>::infinity() } });
                return;
            }
            if (name == "undefined" || name == "null")
                throw std::runtime_error("'" + name + "' is not supported (values are numbers, strings and booleans)");
            if (!is_static_namespace(name))
                throw std::runtime_error("'" + name + "' is not defined");
        }
        if (symbol != nullptr && symbol->type() == type::I64)
        {
            // (an integer number's value (exactly an f64's) is an f64)
            auto value = materialize(operand_type{ symbol_reference{ current_scope(), neolib::string{ name } } }, type::F64);
            value.fragmentSource = aSource;
            push_operand(operand_type{ value });
            return;
        }
        push_operand(operand_type{ symbol_reference{ current_scope(), neolib::string{ name } } });
    }

    void compiler::js_conditional(neolib::i_string_view const& aSource)
    {
        auto const whenFalse = pop_operand_value();
        auto const whenTrue = pop_operand_value();
        auto const condition = pop_operand_value();
        if (declaration_pass())
        {
            fragment dummy;
            dummy.valueType = type::F64;
            push_operand(operand_type{ dummy });
            return;
        }
        auto const t = js_type(whenTrue);
        auto const f = js_type(whenFalse);
        if (t != f)
            throw std::runtime_error("the values of a conditional expression have different types (" + js_type_name(t) + " and " + js_type_name(f) + ")");
        if (is_immediate(condition))
        {
            push_operand(js_to_boolean(condition).fragmentCode.as_std_vector().back() == 1u ? whenTrue : whenFalse);
            return;
        }
        fragment result;
        auto& code = result.fragmentCode.as_std_vector();
        append(result, js_to_boolean(condition));
        put_block(code, opcode::If, t);
        append(result, materialize(whenTrue, t));
        put_op(code, opcode::Else);
        append(result, materialize(whenFalse, t));
        put_op(code, opcode::End);
        result.valueType = t;
        push_operand(operand_type{ result });
    }

    void compiler::js_if(neolib::i_string_view const& aSource)
    {
        auto arms = pop_fragments(aSource, fragment_kind::Statement);
        if (arms.empty() || arms.size() > 2u)
            throw std::runtime_error("if statement body expected");
        auto const condition = pop_operand_value();
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        if (!declaration_pass())
        {
            auto& code = statement.fragmentCode.as_std_vector();
            append(statement, js_to_boolean(condition));
            put_block(code, opcode::If);
            append_nested(statement, arms[0]);
            if (arms.size() == 2u)
            {
                put_op(code, opcode::Else);
                append_nested(statement, arms[1]);
            }
            put_op(code, opcode::End);
        }
        push_operand(operand_type{ statement });
    }

    void compiler::js_loop(construct aConstruct, neolib::i_string_view const& aSource)
    {
        auto stack = operands();
        auto const source = aSource.to_std_string_view();
        auto const in = [&](i_fragment const& aFragment, std::pair<std::size_t, std::size_t> const& aRegion)
        {
            auto const fragmentSource = aFragment.source().to_std_string_view();
            return !fragmentSource.empty() && fragmentSource.data() >= source.data() + aRegion.first &&
                fragmentSource.data() + fragmentSource.size() <= source.data() + aRegion.second;
        };
        auto const top_in = [&](fragment_kind aKind, std::pair<std::size_t, std::size_t> const& aRegion)
        {
            return stack.size() > operand_base() && stack.back().holds_alternative<i_fragment>() &&
                stack.back().get<i_fragment>().kind() == aKind && in(stack.back().get<i_fragment>(), aRegion);
        };
        auto const pop = [&]()
        {
            fragment result{ stack.back().get<i_fragment>() };
            stack.pop_back();
            return result;
        };
        std::optional<fragment> body;
        std::optional<fragment> update;
        std::optional<fragment> test;
        std::optional<operand_type> condition;
        std::vector<fragment> init;
        std::pair<std::size_t, std::size_t> const everything{ 0u, source.size() };
        switch (aConstruct)
        {
        case construct::While:
            if (top_in(fragment_kind::Statement, everything))
                body = pop();
            condition = pop_operand_value();
            break;
        case construct::DoWhile:
            condition = pop_operand_value();
            if (top_in(fragment_kind::Statement, everything))
                body = pop();
            break;
        default:
            {
                auto const regions = for_regions(source);
                if (top_in(fragment_kind::Statement, regions[3]))
                    body = pop();
                if (top_in(fragment_kind::Statement, regions[2]))
                    update = pop();
                if (top_in(fragment_kind::Value, regions[1]))
                    test = pop();
                while (top_in(fragment_kind::Statement, regions[0]))
                    init.insert(init.begin(), pop());
            }
            break;
        }
        if (!body)
            throw std::runtime_error("loop body expected");
        if (!declaration_pass())
        {
            // a loop counter's update must be in its loop (and not in a loop within it)
            char const* own = nullptr;
            auto& loops = js().counterLoops;
            auto const within = [&](char const* aPosition) { return aPosition >= source.data() && aPosition < source.data() + source.size(); };
            if (aConstruct != construct::DoWhile && !loops.empty() && within(loops.back().test))
            {
                own = loops.back().test;
                loops.pop_back();
            }
            auto& updates = js().counterUpdates;
            for (auto u = updates.begin(); u != updates.end();)
                if (within(u->position))
                {
                    if (u->test != own && iInference.notCounters.insert(u->counter).second)
                        set_changed();
                    u = updates.erase(u);
                }
                else
                    ++u;
        }
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        if (!declaration_pass())
        {
            auto& code = statement.fragmentCode.as_std_vector();
            // block (exit) loop [test] block (iteration) body end [update] br loop end end
            resolve_branches(*body, 2u, 0u);
            for (auto const& i : init)
                append(statement, i);
            put_block(code, opcode::Block);
            put_block(code, opcode::Loop);
            if (condition && aConstruct == construct::While)
                test = js_to_boolean(*condition);
            if (test)
            {
                append(statement, *test);
                put_op(code, opcode::I32Eqz);
                put_op(code, opcode::BrIf);
                put_u32(code, 1u);
            }
            put_block(code, opcode::Block);
            append(statement, *body);
            put_op(code, opcode::End);
            if (update)
                append(statement, *update);
            if (aConstruct == construct::DoWhile)
            {
                append(statement, js_to_boolean(*condition));
                put_op(code, opcode::BrIf);
                put_u32(code, 0u);
            }
            else
            {
                put_op(code, opcode::Br);
                put_u32(code, 0u);
            }
            put_op(code, opcode::End);
            put_op(code, opcode::End);
        }
        push_operand(operand_type{ statement });
        retire_symbols(aSource);
    }

    void compiler::js_branch(construct aConstruct, neolib::i_string_view const& aSource)
    {
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        auto& code = statement.fragmentCode.as_std_vector();
        put_op(code, opcode::Br);
        statement.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ static_cast<std::uint32_t>(code.size()),
            BranchTarget | (aConstruct == construct::Continue ? ContinueTarget : 0u) });
        code.resize(code.size() + 5u);
        push_operand(operand_type{ statement });
    }

    void compiler::js_case(construct aConstruct, neolib::i_string_view const& aSource)
    {
        // a case label (its value) or the default label (the statements that follow them are part of their clause)
        if (aConstruct == construct::Case)
        {
            auto const value = pop_operand_value();
            fragment test = declaration_pass() ? fragment{} : materialize(value);
            test.fragmentKind = fragment_kind::Value;
            test.fragmentSource = aSource;
            push_operand(operand_type{ test });
        }
        else
            push_operand(operand_type{ make_fragment(fragment_kind::Statement, type::Void, aSource) });
    }

    void compiler::js_switch(neolib::i_string_view const& aSource)
    {
        auto stack = operands();
        auto const label = [&](i_fragment const& aFragment)
        {
            auto const text = aFragment.source().to_std_string_view();
            return (aFragment.kind() == fragment_kind::Value && text.starts_with("case")) || 
                (aFragment.kind() == fragment_kind::Statement && text.starts_with("default"));
        };
        // the labels and statements of the switch's body (in order)
        std::vector<fragment> items;
        while (stack.size() > operand_base() && stack.back().holds_alternative<i_fragment>() && 
            within(stack.back().get<i_fragment>().source(), aSource) &&
            (stack.back().get<i_fragment>().kind() == fragment_kind::Statement || label(stack.back().get<i_fragment>())))
        {
            items.insert(items.begin(), fragment{ stack.back().get<i_fragment>() });
            stack.pop_back();
        }
        struct clause
        {
            std::optional<fragment> test;
            fragment body;
        };
        std::vector<clause> clauses;
        for (auto const& item : items)
        {
            if (label(item))
                clauses.push_back(clause{ item.kind() == fragment_kind::Value ? std::optional<fragment>{ item } : std::nullopt, fragment{} });
            else if (clauses.empty())
                throw std::runtime_error("case or default expected");
            else
                append(clauses.back().body, item);
        }
        auto const discriminant = pop_operand_value();
        auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
        if (!declaration_pass())
        {
            auto& code = statement.fragmentCode.as_std_vector();
            auto value = materialize(discriminant);
            auto const t = temporary(value.valueType);
            append(statement, value);
            put_local(code, opcode::LocalSet, t);
            // block (exit) block (clause k-1) ... block (clause 0) [dispatch] end [clause 0] end [clause 1] ... end
            std::uint32_t const k = static_cast<std::uint32_t>(clauses.size());
            put_block(code, opcode::Block);
            for (std::uint32_t i = 0u; i < k; ++i)
                put_block(code, opcode::Block);
            std::optional<std::uint32_t> defaultClause;
            for (std::uint32_t i = 0u; i < k; ++i)
            {
                if (!clauses[i].test)
                {
                    if (defaultClause)
                        throw std::runtime_error("more than one default clause in switch statement");
                    defaultClause = i;
                    continue;
                }
                // (strict equality)
                fragment current;
                put_local(current.fragmentCode.as_std_vector(), opcode::LocalGet, t);
                current.valueType = value.valueType;
                push_operand(operand_type{ current });
                push_operand(operand_type{ *clauses[i].test });
                emit_js_operation(construct::StrictEqual);
                append(statement, materialize(pop_operand_value(), type::Boolean));
                put_op(code, opcode::BrIf);
                put_u32(code, i);
            }
            put_op(code, opcode::Br);
            put_u32(code, defaultClause.value_or(k));
            for (std::uint32_t i = 0u; i < k; ++i)
            {
                put_op(code, opcode::End);
                // break leaves the switch (k - 1 - i labels out); continue passes k - i labels on its way to a loop
                auto& body = clauses[i].body;
                auto& bodyCode = body.fragmentCode.as_std_vector();
                neolib::vector<neolib::pair<std::uint32_t, std::uint32_t>> remaining;
                for (auto const& r : body.fragmentRelocations)
                {
                    if ((r.second() & BranchTarget) != 0u && (r.second() & DeferredCall) == 0u)
                    {
                        auto const depth = r.second() & ~(BranchTarget | ContinueTarget);
                        if ((r.second() & ContinueTarget) != 0u)
                            remaining.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ r.first(), r.second() + (k - i) });
                        else
                            put_u32_padded(bodyCode, r.first(), depth + (k - 1u - i));
                    }
                    else
                        remaining.push_back(r);
                }
                body.fragmentRelocations = remaining;
                append(statement, body);
            }
            put_op(code, opcode::End);
        }
        push_operand(operand_type{ statement });
        retire_symbols(aSource);
    }

    // --- calls

    void compiler::js_call(neolib::i_string_view const& aSource)
    {
        auto const [name, rest] = leading_name(aSource.to_std_string_view());
        check_name(name);
        auto arguments = pop_fragments(aSource, fragment_kind::Argument);
        if (declaration_pass())
        {
            fragment dummy;
            dummy.valueType = type::F64;
            push_operand(operand_type{ dummy });
            return;
        }
        // a function declared in the program (before or after the call)
        std::optional<function_signature> signature;
        bool inferred = false;
        if (auto const* symbol = find_symbol(current_scope(), neolib::string_view{ name }))
        {
            if (symbol->type() != type::Function)
                throw std::runtime_error("'" + name + "' is not a function");
            signature = function_signature{ generator().signature_of(symbol->index()) };
            inferred = generator().function_inferred(symbol->index());
        }
        else
            for (std::size_t d = 0u; d < generator().forward_declaration_count(); ++d)
                if (generator().forward_declaration_name(d).to_std_string_view() == name)
                {
                    signature = function_signature{ generator().forward_declaration_signature(d) };
                    inferred = generator().forward_declaration_inferred(d);
                }
        if (!signature)
        {
            if (js_builtin(name + "()", std::nullopt, arguments, aSource))
                return;
            throw std::runtime_error("'" + name + "' is not defined");
        }
        auto const parameterCount = signature->parameters().has_value() ? signature->parameters().value().size() : 0u;
        if (arguments.size() != parameterCount)
            throw std::runtime_error("'" + name + "' takes " + std::to_string(parameterCount) + " argument(s)");
        if (inferred)
        {
            // the type of a parameter is that of the arguments passed to it
            auto& seen = iInference.arguments[name];
            seen.resize(parameterCount);
            bool changed = false;
            for (std::size_t i = 0u; i < parameterCount; ++i)
            {
                auto const t = js_type(operand_type{ arguments[i] });
                if (seen[i] && *seen[i] != t)
                    throw std::runtime_error("argument " + std::to_string(i + 1u) + " of '" + name + "' is a " + js_type_name(*seen[i]) +
                        " in one call and a " + js_type_name(t) + " in another");
                seen[i] = t;
                auto fact = generator().inferred_parameter(neolib::string_view{ name }, static_cast<std::uint32_t>(i));
                if (fact == type::UNKNOWN)
                    fact = type::F64;
                if (fact != t)
                {
                    infer("parameter " + std::to_string(i + 1u) + " of '" + name + "'", fact, t);
                    generator().set_inferred_parameter(neolib::string_view{ name }, static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(parameterCount), t);
                }
                auto const parameterType = signature->parameters().value()[i].parameter_type();
                if ((parameterType == type::I32 ? type::F64 : parameterType) != t)
                    changed = true;
                if (t == type::F64)
                {
                    // (a number parameter only passed 32-bit integers is an i32)
                    auto const range = integer_range(operand_type{ arguments[i] });
                    if (!range && parameterType == type::I32)
                        changed = true;
                    observe_range(range_key{ nullptr, name + "#" + std::to_string(i) }, range);
                }
            }
            if (changed)
            {
                auto const result = signature->return_type();
                push_operand(operand_type{ placeholder(result == type::Void ? fragment_kind::Statement : fragment_kind::Value, result, 
                    result == type::Void ? neolib::string_view{ aSource } : neolib::string_view{}) });
                return;
            }
        }
        for (std::size_t i = 0u; i < arguments.size(); ++i)
        {
            auto& argument = arguments[i];
            auto const target = inferred && signature->parameters().value()[i].parameter_type() == type::I32 ? type::I32 : type::F64;
            if (argument.valueType != target && is_numeric(argument.valueType) && argument.valueType != type::Boolean)
            {
                convert(argument.fragmentCode.as_std_vector(), argument.valueType, target);
                argument.valueType = target;
            }
            push_operand(operand_type{ argument });
        }
        generator().emit_call(neolib::string_view{ name }, aSource);
    }

    void compiler::js_member(construct aConstruct, neolib::i_string_view const& aSource)
    {
        auto const text = aSource.to_std_string_view();
        std::vector<fragment> arguments;
        std::string member;
        bool call = false;
        std::optional<operand_type> index;
        if (aConstruct == construct::Member)
        {
            auto const [name, rest] = leading_name(text.substr(text.find('.') + 1u));
            member = name;
            call = !rest.empty() && rest.front() == '(';
            if (call)
                arguments = pop_fragments(aSource, fragment_kind::Argument);
        }
        else
            index = pop_operand_value();
        auto const receiver = pop_operand_value();
        if (declaration_pass())
        {
            fragment dummy;
            dummy.valueType = type::F64;
            push_operand(operand_type{ dummy });
            return;
        }
        if (index)
        {
            // string[index]
            if (js_type(receiver) != type::String)
                throw std::runtime_error("only strings can be indexed");
            arguments.push_back(materialize(js_to_number(*index), type::F64));
            if (!js_builtin("string.at", receiver, arguments, aSource))
                throw std::logic_error("neos::language::compiler::js_member");
            return;
        }
        if (receiver.holds_alternative<i_symbol_reference>())
        {
            auto const name = receiver.get<i_symbol_reference>().name().to_std_string();
            if (find_symbol(current_scope(), neolib::string_view{ name }) == nullptr && is_static_namespace(name))
            {
                if (js_builtin(name + "." + member + (call ? "()" : ""), std::nullopt, arguments, aSource))
                    return;
                throw std::runtime_error("'" + name + "." + member + "' is not supported");
            }
        }
        auto const t = js_type(receiver);
        if (js_builtin(js_type_name(t) + "." + member + (call ? "()" : ""), receiver, arguments, aSource))
            return;
        throw std::runtime_error("'" + member + "' of a " + js_type_name(t) + " is not supported");
    }

    bool compiler::js_builtin(std::string const& aName, std::optional<operand_type> const& aReceiver, std::vector<fragment>& aArguments, neolib::i_string_view const& aSource)
    {
        for (auto& a : aArguments)
            a.fragmentKind = fragment_kind::Value;
        fragment result;
        auto& code = result.fragmentCode.as_std_vector();
        auto const argument = [&](std::size_t aIndex) -> operand_type
        {
            if (aIndex >= aArguments.size())
                throw std::runtime_error("'" + aName + "' expects at least " + std::to_string(aIndex + 1u) + " argument(s)");
            return operand_type{ aArguments[aIndex] };
        };
        auto const optional_number = [&](std::size_t aIndex, double aDefault) -> operand_type
        {
            return aIndex < aArguments.size() ? js_to_number(argument(aIndex)) : operand_type{ data<f64>{ aDefault } };
        };
        auto const number = [&](operand_type const& aOperand) { append(result, materialize(js_to_number(aOperand), type::F64)); };
        auto const text = [&](operand_type const& aOperand) { append(result, materialize(js_to_string(aOperand), type::String)); };
        auto const runtime = [&](std::string const& aFunction, std::vector<type> const& aParameters, type aResult)
        {
            emit_call_to(result, js_runtime(aFunction, aParameters, aResult));
            result.valueType = aResult;
        };
        auto const value = [&](type aType)
        {
            result.valueType = aType;
            push_operand(operand_type{ result });
            return true;
        };
        auto const constant = [&](double aValue)
        {
            push_operand(operand_type{ data<f64>{ aValue } });
            return true;
        };
        auto const unary_op = [&](opcode aOpcode)
        {
            number(argument(0u));
            put_op(code, aOpcode);
            return value(type::F64);
        };
        auto const temporary_number = [&](operand_type const& aOperand)
        {
            auto const t = temporary(type::F64);
            number(aOperand);
            put_local(code, opcode::LocalSet, t);
            return t;
        };
        auto const get = [&](std::uint32_t aLocal) { put_local(code, opcode::LocalGet, aLocal); };
        // console
        if (aName == "console.log()" || aName == "console.error()" || aName == "console.info()" || aName == "console.warn()" || aName == "console.debug()")
        {
            // (console.log writes a line to the script's output; console.error and console.warn to the standard error stream)
            bool const error = (aName == "console.error()" || aName == "console.warn()");
            // the arguments (as console.log formats them) separated by spaces
            operand_type line = data<string>{ string{} };
            for (std::size_t i = 0u; i < aArguments.size(); ++i)
            {
                push_operand(line);
                if (i != 0u)
                {
                    push_operand(operand_type{ data<string>{ string{ " " } } });
                    emit_operation(operation::Add);
                }
                auto text = js_to_string(argument(i), true);
                if (js_type(argument(i)) != type::String)
                {
                    // (a value that is not a string is styled (coloured) if its stream is (see javascript::stylize))
                    fragment styled = materialize(text, type::String);
                    put_i32(styled.fragmentCode.as_std_vector(), error ? 2 : 1);
                    emit_call_to(styled, js_runtime("javascript::stylize", { type::String, type::I32 }, type::String));
                    styled.valueType = type::String;
                    text = operand_type{ styled };
                }
                push_operand(text);
                emit_operation(operation::Add);
                line = pop_operand_value();
            }
            if (!error)
            {
                push_operand(line);
                push_operand(operand_type{ data<string>{ string{ "\n" } } });
                emit_operation(operation::Add);
                line = pop_operand_value();
            }
            auto statement = make_fragment(fragment_kind::Statement, type::Void, aSource);
            append(statement, materialize(line, type::String));
            emit_call_to(statement, js_runtime(error ? "javascript::console_error" : "neo::print", { type::String }, type::Void));
            push_operand(operand_type{ statement });
            return true;
        }
        // Math
        static std::map<std::string, double> const sConstants = {
            { "Math.PI", 3.141592653589793 }, { "Math.E", 2.718281828459045 }, { "Math.LN2", 0.6931471805599453 },
            { "Math.LN10", 2.302585092994046 }, { "Math.LOG2E", 1.4426950408889634 }, { "Math.LOG10E", 0.4342944819032518 },
            { "Math.SQRT2", 1.4142135623730951 }, { "Math.SQRT1_2", 0.7071067811865476 },
            { "Number.MAX_SAFE_INTEGER", 9007199254740991.0 }, { "Number.MIN_SAFE_INTEGER", -9007199254740991.0 },
            { "Number.EPSILON", 2.220446049250313e-16 }, { "Number.MAX_VALUE", std::numeric_limits<double>::max() },
            { "Number.MIN_VALUE", std::numeric_limits<double>::denorm_min() } };
        if (auto const c = sConstants.find(aName); c != sConstants.end())
            return constant(c->second);
        if (aName == "Number.POSITIVE_INFINITY" || aName == "Number.NEGATIVE_INFINITY" || aName == "Number.NaN")
        {
            put_f64(code, aName == "Number.NaN" ? std::numeric_limits<double>::quiet_NaN() :
                aName == "Number.POSITIVE_INFINITY" ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity());
            return value(type::F64);
        }
        if (aName == "Math.floor()")
            return unary_op(opcode::F64Floor);
        if (aName == "Math.ceil()")
            return unary_op(opcode::F64Ceil);
        if (aName == "Math.trunc()")
            return unary_op(opcode::F64Trunc);
        if (aName == "Math.sqrt()")
            return unary_op(opcode::F64Sqrt);
        if (aName == "Math.abs()")
            return unary_op(opcode::F64Abs);
        if (aName == "Math.fround()")
        {
            number(argument(0u));
            put_op(code, opcode::F32ConvertF64);
            put_op(code, opcode::F64ConvertF32);
            return value(type::F64);
        }
        if (aName == "Math.round()")
        {
            // floor(x) + (x - floor(x) >= 0.5) with the sign of x (Math.round(-0.4) is -0)
            auto const x = temporary_number(argument(0u));
            auto const f = temporary(type::F64);
            get(x); put_op(code, opcode::F64Floor); put_local(code, opcode::LocalTee, f);
            get(x); get(f); put_op(code, opcode::F64Sub); put_f64(code, 0.5); put_op(code, opcode::F64Ge);
            put_op(code, opcode::F64UConvertI32);
            put_op(code, opcode::F64Add);
            get(x); put_op(code, opcode::F64CopySign);
            return value(type::F64);
        }
        if (aName == "Math.sign()")
        {
            auto const x = temporary_number(argument(0u));
            get(x); put_f64(code, 0.0); put_op(code, opcode::F64Gt);
            put_block(code, opcode::If, type::F64);
            put_f64(code, 1.0);
            put_op(code, opcode::Else);
            get(x); put_f64(code, 0.0); put_op(code, opcode::F64Lt);
            put_block(code, opcode::If, type::F64);
            put_f64(code, -1.0);
            put_op(code, opcode::Else);
            get(x);
            put_op(code, opcode::End);
            put_op(code, opcode::End);
            return value(type::F64);
        }
        if (aName == "Math.min()" || aName == "Math.max()")
        {
            bool const isMin = (aName == "Math.min()");
            if (aArguments.empty())
                return constant(isMin ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity());
            number(argument(0u));
            for (std::size_t i = 1u; i < aArguments.size(); ++i)
            {
                number(argument(i));
                put_op(code, isMin ? opcode::F64Min : opcode::F64Max);
            }
            return value(type::F64);
        }
        if (aName == "Math.imul()" || aName == "Math.clz32()")
        {
            append(result, js_to_int32(argument(0u)));
            if (aName == "Math.imul()")
            {
                append(result, js_to_int32(argument(1u)));
                put_op(code, opcode::I32Mul);
                return value(type::I32);
            }
            put_op(code, opcode::I32Clz);
            return value(type::U32);
        }
        if (aName == "Math.pow()")
        {
            push_operand(js_to_number(argument(0u)));
            push_operand(js_to_number(argument(1u)));
            emit_js_operation(construct::Power);
            return true;
        }
        static std::set<std::string> const sMath1 = { "exp", "expm1", "log", "log1p", "log2", "log10", "sin", "cos", "tan",
            "asin", "acos", "atan", "sinh", "cosh", "tanh", "asinh", "acosh", "atanh", "cbrt" };
        if (aName.starts_with("Math.") && aName.ends_with("()") && sMath1.contains(aName.substr(5u, aName.size() - 7u)))
        {
            number(argument(0u));
            runtime("javascript::math_" + aName.substr(5u, aName.size() - 7u), { type::F64 }, type::F64);
            return value(type::F64);
        }
        if (aName == "Math.atan2()" || aName == "Math.hypot()")
        {
            number(argument(0u));
            number(argument(1u));
            runtime("javascript::math_" + aName.substr(5u, aName.size() - 7u), { type::F64, type::F64 }, type::F64);
            return value(type::F64);
        }
        if (aName == "Date.now()" || aName == "performance.now()")
        {
            runtime(aName == "Date.now()" ? "javascript::date_now" : "javascript::performance_now", {}, type::F64);
            return value(type::F64);
        }
        if (aName == "Math.random()")
        {
            runtime("javascript::math_random", {}, type::F64);
            return value(type::F64);
        }
        // Number and global functions
        if (aName == "Number.isInteger()" || aName == "Number.isSafeInteger()" || aName == "Number.isFinite()" || aName == "Number.isNaN()" ||
            aName == "isFinite()" || aName == "isNaN()")
        {
            bool const global = !aName.starts_with("Number.");
            auto const operand = argument(0u);
            if (!global && js_type(operand) != type::F64)
            {
                // (only numbers are integers, finite or NaN)
                append(result, aArguments[0]);
                put_op(code, opcode::Drop);
                put_i32(code, 0);
                return value(type::Boolean);
            }
            auto const x = temporary_number(operand);
            if (aName.ends_with("isNaN()"))
            {
                get(x); get(x); put_op(code, opcode::F64Ne);
            }
            else
            {
                get(x); put_op(code, opcode::F64Abs);
                put_f64(code, aName == "Number.isSafeInteger()" ? 9007199254740991.0 : std::numeric_limits<double>::infinity());
                put_op(code, aName == "Number.isSafeInteger()" ? opcode::F64Le : opcode::F64Lt);
                if (aName == "Number.isInteger()" || aName == "Number.isSafeInteger()")
                {
                    get(x); put_op(code, opcode::F64Trunc); get(x); put_op(code, opcode::F64Eq);
                    put_op(code, opcode::I32And);
                }
            }
            return value(type::Boolean);
        }
        if (aName == "parseInt()" || aName == "Number.parseInt()")
        {
            text(argument(0u));
            append(result, materialize(optional_number(1u, 0.0), type::F64));
            runtime("javascript::parse_int", { type::String, type::F64 }, type::F64);
            return value(type::F64);
        }
        if (aName == "parseFloat()" || aName == "Number.parseFloat()")
        {
            text(argument(0u));
            runtime("javascript::parse_float", { type::String }, type::F64);
            return value(type::F64);
        }
        if (aName == "String()")
        {
            if (aArguments.empty())
                push_operand(operand_type{ data<string>{ string{} } });
            else
                push_operand(js_to_string(argument(0u)));
            return true;
        }
        if (aName == "Number()")
        {
            push_operand(aArguments.empty() ? operand_type{ data<f64>{ 0.0 } } : js_to_number(argument(0u)));
            return true;
        }
        if (aName == "Boolean()")
        {
            if (aArguments.empty())
                push_operand(operand_type{ data<boolean>{ false } });
            else
                push_operand(operand_type{ js_to_boolean(argument(0u)) });
            return true;
        }
        if (aName == "String.fromCharCode()" || aName == "String.fromCodePoint()")
        {
            operand_type joined = data<string>{ string{} };
            for (std::size_t i = 0u; i < aArguments.size(); ++i)
            {
                fragment character;
                append(character, materialize(js_to_number(argument(i)), type::F64));
                emit_call_to(character, js_runtime(aName == "String.fromCharCode()" ? "javascript::from_char_code" : "javascript::from_code_point",
                    { type::F64 }, type::String));
                character.valueType = type::String;
                push_operand(joined);
                push_operand(operand_type{ character });
                emit_operation(operation::Add);
                joined = pop_operand_value();
            }
            push_operand(joined);
            return true;
        }
        // methods
        if (!aReceiver)
            return false;
        auto const receiver = *aReceiver;
        if (aName == "number.toString()" || aName == "boolean.toString()" || aName == "string.toString()" || aName == "string.valueOf()")
        {
            if (aName == "number.toString()" && !aArguments.empty())
            {
                number(receiver);
                number(argument(0u));
                runtime("javascript::to_string_radix", { type::F64, type::F64 }, type::String);
                return value(type::String);
            }
            push_operand(js_to_string(receiver));
            return true;
        }
        if (aName == "number.toFixed()")
        {
            number(receiver);
            append(result, materialize(optional_number(0u, 0.0), type::F64));
            runtime("javascript::to_fixed", { type::F64, type::F64 }, type::String);
            return value(type::String);
        }
        if (aName == "string.length")
        {
            if (is_immediate(receiver))
                return constant(static_cast<double>(ecmascript::length(get_immediate(receiver).s)));
            // (a string's length in UTF-16 code units follows its length in bytes; see bytecode::string_header)
            text(receiver);
            put_op(code, opcode::I32LoadMem);
            put_u32(code, 2u);
            put_u32(code, static_cast<std::uint32_t>(offsetof(bytecode::string_header, utf16Units)));
            result.hasRange = true;
            result.rangeLow = 0;
            result.rangeHigh = Int32Max;
            return value(type::I32);
        }
        struct string_method
        {
            std::string runtime;
            std::vector<double> defaults;   ///< (the number arguments; NaN: a string argument)
            type result;
        };
        double const nan = std::numeric_limits<double>::quiet_NaN();
        double const infinity = std::numeric_limits<double>::infinity();
        static std::map<std::string, string_method> const sStringMethods = {
            { "string.charAt()", { "javascript::char_at", { 0.0 }, type::String } },
            { "string.at", { "javascript::at", { 0.0 }, type::String } },
            { "string.at()", { "javascript::at", { 0.0 }, type::String } },
            { "string.charCodeAt()", { "javascript::char_code_at", { 0.0 }, type::F64 } },
            { "string.codePointAt()", { "javascript::code_point_at", { 0.0 }, type::F64 } },
            { "string.indexOf()", { "javascript::index_of", { nan, 0.0 }, type::F64 } },
            { "string.lastIndexOf()", { "javascript::last_index_of", { nan, infinity }, type::F64 } },
            { "string.includes()", { "javascript::includes", { nan, 0.0 }, type::Boolean } },
            { "string.startsWith()", { "javascript::starts_with", { nan, 0.0 }, type::Boolean } },
            { "string.endsWith()", { "javascript::ends_with", { nan, infinity }, type::Boolean } },
            { "string.slice()", { "javascript::slice", { 0.0, infinity }, type::String } },
            { "string.substring()", { "javascript::substring", { 0.0, infinity }, type::String } },
            { "string.substr()", { "javascript::substr", { 0.0, infinity }, type::String } },
            { "string.toUpperCase()", { "javascript::to_upper_case", {}, type::String } },
            { "string.toLowerCase()", { "javascript::to_lower_case", {}, type::String } },
            { "string.trim()", { "javascript::trim", {}, type::String } },
            { "string.trimStart()", { "javascript::trim_start", {}, type::String } },
            { "string.trimEnd()", { "javascript::trim_end", {}, type::String } },
            { "string.repeat()", { "javascript::repeat", { 0.0 }, type::String } },
            { "string.padStart()", { "javascript::pad_start", { 0.0, nan }, type::String } },
            { "string.padEnd()", { "javascript::pad_end", { 0.0, nan }, type::String } } };
        if (auto const m = sStringMethods.find(aName); m != sStringMethods.end())
        {
            auto const& method = m->second;
            std::vector<type> parameters = { type::String };
            text(receiver);
            for (std::size_t i = 0u; i < method.defaults.size(); ++i)
            {
                bool const isText = std::isnan(method.defaults[i]);
                parameters.push_back(isText ? type::String : type::F64);
                if (isText)
                {
                    if (i < aArguments.size())
                        text(argument(i));
                    else if (aName == "string.padStart()" || aName == "string.padEnd()")
                        put_i32(code, static_cast<std::int32_t>(string_literal(" ")));
                    else
                        text(operand_type{ data<string>{ string{ "undefined" } } });
                }
                else
                    append(result, materialize(optional_number(i, method.defaults[i]), type::F64));
            }
            runtime(method.runtime, parameters, method.result);
            return value(method.result);
        }
        if (aName == "string.concat()")
        {
            push_operand(receiver);
            for (std::size_t i = 0u; i < aArguments.size(); ++i)
            {
                push_operand(js_to_string(argument(i)));
                emit_operation(operation::Add);
            }
            return true;
        }
        return false;
    }
}
