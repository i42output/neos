/*
  emit.hpp

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

  Portions of this file are derived from CPython 3.13 (Objects/floatobject.c), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: its float floor division and modulo (_float_div_mod) translated from C to C++
  for neos (see runtime/python/NOTICE).
*/

#pragma once

// WASM code generation helpers (used by the compiler and the language compilers (i_language_compiler))

#include <neolib/neolib.hpp>
#include <iomanip>
#include <sstream>
#include <limits>
#include <array>
#include <cmath>
#include <neos/bytecode/opcodes.hpp>
#include <neos/bytecode/text.hpp>
#include <neos/language/i_compiler.hpp>

namespace neos::language
{
    namespace emitter
    {
        using bytes = std::vector<std::uint8_t>;

        constexpr std::uint32_t DataBase = 16u;

        inline void put_op(bytes& aCode, bytecode::opcode aOpcode)
        {
            // (an opcode of one byte (most): its byte (from a table of them, made once); else its encoding)
            static auto const sSingle = []()
            {
                std::array<std::int16_t, 256u> result;
                result.fill(-1);
                for (auto const& [op, entry] : bytecode::opcode_dictionary())
                    if (auto const* e = std::get_if<bytecode::op1>(&entry.encoding); e != nullptr && static_cast<std::uint32_t>(op) < 256u)
                        result[static_cast<std::uint32_t>(op)] = static_cast<std::int16_t>(e->b);
                return result;
            }();
            auto const value = static_cast<std::uint32_t>(aOpcode);
            if (value < 256u && sSingle[value] >= 0)
            {
                aCode.push_back(static_cast<std::uint8_t>(sSingle[value]));
                return;
            }
            auto const& encoding = bytecode::opcode_dictionary().at(aOpcode).encoding;
            if (auto const* e1 = std::get_if<bytecode::op1>(&encoding))
                aCode.push_back(static_cast<std::uint8_t>(e1->b));
            else if (auto const* e2 = std::get_if<bytecode::op2>(&encoding))
            {
                aCode.push_back(static_cast<std::uint8_t>(e2->p));
                std::visit([&](auto const& e)
                    {
                        for (auto const& b : e)
                            aCode.push_back(static_cast<std::uint8_t>(b));
                    }, e2->e);
            }
        }

        inline void put_u32(bytes& aCode, std::uint64_t aValue)
        {
            do
            {
                std::uint8_t b = aValue & 0x7Fu;
                aValue >>= 7u;
                if (aValue != 0u)
                    b |= 0x80u;
                aCode.push_back(b);
            } while (aValue != 0u);
        }

        inline void put_s64(bytes& aCode, std::int64_t aValue)
        {
            bool more = true;
            while (more)
            {
                std::uint8_t b = aValue & 0x7F;
                aValue >>= 7;
                if ((aValue == 0 && (b & 0x40u) == 0u) || (aValue == -1 && (b & 0x40u) != 0u))
                    more = false;
                else
                    b |= 0x80u;
                aCode.push_back(b);
            }
        }

        inline void put_u32_padded(bytes& aCode, std::size_t aOffset, std::uint32_t aValue)
        {
            for (std::size_t i = 0u; i < 5u; ++i)
            {
                std::uint8_t b = aValue & 0x7Fu;
                aValue >>= 7u;
                if (i < 4u)
                    b |= 0x80u;
                aCode[aOffset + i] = b;
            }
        }

        inline void put_name(bytes& aCode, std::string const& aName)
        {
            put_u32(aCode, aName.size());
            aCode.insert(aCode.end(), aName.begin(), aName.end());
        }

        template <typename T>
        inline void put_raw(bytes& aCode, T aValue)
        {
            auto const* p = reinterpret_cast<std::uint8_t const*>(&aValue);
            aCode.insert(aCode.end(), p, p + sizeof(T));
        }

        inline void put_section(bytes& aModule, std::uint8_t aId, bytes const& aContents)
        {
            aModule.push_back(aId);
            put_u32(aModule, aContents.size());
            aModule.insert(aModule.end(), aContents.begin(), aContents.end());
        }

        inline bool is_float(type aType)
        {
            return aType == type::F32 || aType == type::F64 || aType == type::Fbig;
        }

        inline bool is_64(type aType)
        {
            return aType == type::I64 || aType == type::U64 || aType == type::Ibig || aType == type::Integer;
        }

        inline bool is_unsigned(type aType)
        {
            return aType == type::U8 || aType == type::U16 || aType == type::U32 || aType == type::U64 || aType == type::Boolean;
        }

        inline bool is_numeric(type aType)
        {
            return (aType >= type::Boolean && aType <= type::Fbig);
        }

        inline bool is_flexible(type aType)
        {
            return aType == type::Ibig || aType == type::Fbig;
        }

        inline std::optional<std::uint8_t> wasm_type(type aType)
        {
            switch (aType)
            {
            case type::Void:
                return std::nullopt;
            case type::I64:
            case type::U64:
            case type::Ibig:
            case type::Integer:
                return 0x7Eu;
            case type::F32:
                return 0x7Du;
            case type::F64:
            case type::Fbig:
                return 0x7Cu;
            case type::UNKNOWN:
                throw std::runtime_error("type unknown (type inference not supported)");
            default:
                return 0x7Fu;
            }
        }

        inline std::uint32_t rank(type aType)
        {
            switch (aType)
            {
            case type::Boolean: return 0u;
            case type::I8: case type::U8: return 1u;
            case type::I16: case type::U16: return 2u;
            case type::I32: case type::U32: return 3u;
            case type::I64: case type::U64: return 4u;
            case type::F32: return 5u;
            case type::F64: return 6u;
            default: return 3u;
            }
        }

        inline void convert(bytes& aCode, type aFrom, type aTo)
        {
            using bytecode::opcode;
            if (aFrom == aTo)
                return;
            if (aFrom == type::String || aTo == type::String)
                throw std::runtime_error("cannot convert " + type_name(aFrom) + " to " + type_name(aTo));
            if (!is_numeric(aFrom) || !is_numeric(aTo))
                throw std::runtime_error("cannot convert " + type_name(aFrom) + " to " + type_name(aTo));
            if (aTo == type::Boolean)
            {
                // compare against zero in the source type
                if (aFrom == type::F32)
                {
                    put_op(aCode, opcode::F32Const);
                    put_raw(aCode, 0.0f);
                    put_op(aCode, opcode::F32Ne);
                }
                else if (is_float(aFrom))
                {
                    put_op(aCode, opcode::F64Const);
                    put_raw(aCode, 0.0);
                    put_op(aCode, opcode::F64Ne);
                }
                else if (is_64(aFrom))
                {
                    put_op(aCode, opcode::I64Const);
                    put_s64(aCode, 0);
                    put_op(aCode, opcode::I64Ne);
                }
                else
                {
                    put_op(aCode, opcode::I32Const);
                    put_s64(aCode, 0);
                    put_op(aCode, opcode::I32Ne);
                }
                return;
            }
            bool const fromFloat = is_float(aFrom);
            bool const toFloat = is_float(aTo);
            bool const fromUnsigned = is_unsigned(aFrom);
            bool const toUnsigned = is_unsigned(aTo);
            if (!fromFloat && !toFloat)
            {
                if (is_64(aFrom) && !is_64(aTo))
                    put_op(aCode, opcode::I32ConvertI64);
                else if (!is_64(aFrom) && is_64(aTo))
                    put_op(aCode, fromUnsigned ? opcode::I64UConvertI32 : opcode::I64SConvertI32);
            }
            else if (!fromFloat && toFloat)
            {
                if (aTo == type::F32)
                    put_op(aCode, is_64(aFrom) ? (fromUnsigned ? opcode::F32UConvertI64 : opcode::F32SConvertI64) : (fromUnsigned ? opcode::F32UConvertI32 : opcode::F32SConvertI32));
                else
                    put_op(aCode, is_64(aFrom) ? (fromUnsigned ? opcode::F64UConvertI64 : opcode::F64SConvertI64) : (fromUnsigned ? opcode::F64UConvertI32 : opcode::F64SConvertI32));
                return;
            }
            else if (fromFloat && toFloat)
            {
                if (aFrom == type::F32 && aTo != type::F32)
                    put_op(aCode, opcode::F64ConvertF32);
                else if (aFrom != type::F32 && aTo == type::F32)
                    put_op(aCode, opcode::F32ConvertF64);
                return;
            }
            else
            {
                if (aFrom == type::F32)
                    put_op(aCode, is_64(aTo) ? (toUnsigned ? opcode::I64UConvertF32 : opcode::I64SConvertF32) : (toUnsigned ? opcode::I32UConvertF32 : opcode::I32SConvertF32));
                else
                    put_op(aCode, is_64(aTo) ? (toUnsigned ? opcode::I64UConvertF64 : opcode::I64SConvertF64) : (toUnsigned ? opcode::I32UConvertF64 : opcode::I32SConvertF64));
            }
            switch (aTo)
            {
            case type::I8:
                put_op(aCode, opcode::I32SExtendI8);
                break;
            case type::I16:
                put_op(aCode, opcode::I32SExtendI16);
                break;
            case type::U8:
                put_op(aCode, opcode::I32Const);
                put_s64(aCode, 0xFF);
                put_op(aCode, opcode::I32And);
                break;
            case type::U16:
                put_op(aCode, opcode::I32Const);
                put_s64(aCode, 0xFFFF);
                put_op(aCode, opcode::I32And);
                break;
            default:
                break;
            }
        }

        // whether code can set a local: if it has the bytes of a local.set or local.tee of it (they can also be other code's)
        inline bool sets_local(i_fragment const& aCode, std::uint32_t aLocal)
        {
            bytes pattern;
            put_u32(pattern, aLocal);
            auto const& code = aCode.code();
            for (std::size_t i = 0u; i + pattern.size() < code.size(); ++i)
                if (code[i] == 0x21u || code[i] == 0x22u)
                {
                    bool match = true;
                    for (std::size_t j = 0u; j < pattern.size() && match; ++j)
                        match = code[i + 1u + j] == pattern[j];
                    if (match)
                        return true;
                }
            return false;
        }

        inline void append(fragment& aTarget, i_fragment const& aSource)
        {
            auto& code = aTarget.fragmentCode.as_std_vector();
            auto const offset = static_cast<std::uint32_t>(code.size());
            for (auto const& r : aSource.relocations())
                aTarget.fragmentRelocations.push_back(neolib::pair<std::uint32_t, std::uint32_t>{ r.first() + offset, r.second() });
            for (auto b : aSource.code())
                code.push_back(b);
        }

        inline bool within(neolib::i_string_view const& aInner, neolib::i_string_view const& aOuter)
        {
            if (aInner.empty())
                return false;
            auto const inner = aInner.to_std_string_view();
            auto const outer = aOuter.to_std_string_view();
            return inner.data() >= outer.data() && inner.data() + inner.size() <= outer.data() + outer.size();
        }

        // a statement nested in aLevels WASM blocks: breaks out of a loop that encloses it must pass more labels
        inline void append_nested(fragment& aTarget, i_fragment const& aSource, std::uint32_t aLevels = 1u)
        {
            auto const before = aTarget.fragmentRelocations.size();
            append(aTarget, aSource);
            for (auto i = before; i < aTarget.fragmentRelocations.size(); ++i)
            {
                auto& r = aTarget.fragmentRelocations[i];
                if ((r.second() & BranchTarget) != 0u && (r.second() & DeferredCall) == 0u)
                    r.second() = r.second() + aLevels;
            }
        }

        // breaks and continues within a loop's body (which is within aBreakDepth labels of the loop's exit and
        // aContinueDepth labels of the end of the current iteration)
        inline void resolve_branches(fragment& aBody, std::uint32_t aBreakDepth, std::uint32_t aContinueDepth)
        {
            auto& code = aBody.fragmentCode.as_std_vector();
            neolib::vector<neolib::pair<std::uint32_t, std::uint32_t>> remaining;
            for (auto const& r : aBody.fragmentRelocations)
            {
                if ((r.second() & BranchTarget) != 0u && (r.second() & DeferredCall) == 0u)
                {
                    bool const isContinue = (r.second() & ContinueTarget) != 0u;
                    auto const depth = r.second() & ~(BranchTarget | ContinueTarget);
                    put_u32_padded(code, r.first(), depth + (isContinue ? aContinueDepth : aBreakDepth));
                }
                else
                    remaining.push_back(r);
            }
            aBody.fragmentRelocations = remaining;
        }

        // the fragment has a continue (that the loop enclosing it has not resolved)
        inline bool has_continue(i_fragment const& aFragment)
        {
            for (auto const& r : aFragment.relocations())
                if ((r.second() & BranchTarget) != 0u && (r.second() & ContinueTarget) != 0u && (r.second() & DeferredCall) == 0u)
                    return true;
            return false;
        }

        inline fragment make_fragment(fragment_kind aKind, type aType, neolib::i_string_view const& aSource)
        {
            fragment result;
            result.fragmentKind = aKind;
            result.valueType = aType;
            result.fragmentSource = aSource;
            return result;
        }

        inline bool is_immediate(operand_type const& aOperand)
        {
            if (!aOperand.holds_alternative<i_data_type>())
                return false;
            bool result = false;
            std::visit([&](auto const& d)
                {
                    using vt = std::decay_t<decltype(d)>;
                    if constexpr (!std::is_same_v<vt, std::monostate>)
                        result = !d.symbol().has_value() && d.value().has_value();
                }, std::get<data_type>(aOperand));
            return result;
        }

        // immediate value as a scalar: integer (as int64), real (as double), boolean or string
        struct immediate
        {
            type t = type::UNKNOWN;
            std::int64_t i = 0;
            double f = 0.0;
            std::string s;
        };

        // float floor division and modulo (the sign of the divisor; as CPython)
        inline std::pair<double, double> floored_divmod(double aLhs, double aRhs)
        {
            double mod = std::fmod(aLhs, aRhs);
            double div = (aLhs - mod) / aRhs;
            if (mod != 0.0)
            {
                if ((aRhs < 0.0) != (mod < 0.0))
                {
                    mod += aRhs;
                    div -= 1.0;
                }
            }
            else
                mod = std::copysign(0.0, aRhs);
            double floorDiv;
            if (div != 0.0)
            {
                floorDiv = std::floor(div);
                if (div - floorDiv > 0.5)
                    floorDiv += 1.0;
            }
            else
                floorDiv = std::copysign(0.0, aLhs / aRhs);
            return { floorDiv, mod };
        }

        inline immediate get_immediate(operand_type const& aOperand)
        {
            immediate result;
            std::visit([&](auto const& d)
                {
                    using vt = std::decay_t<decltype(d)>;
                    if constexpr (!std::is_same_v<vt, std::monostate>)
                    {
                        using value_type = typename vt::type;
                        if constexpr (std::is_same_v<value_type, ibig>)
                        {
                            result.t = type::Ibig;
                            auto const text = d.value().value().to_string();
                            try
                            {
                                result.i = std::stoll(text);
                            }
                            catch (std::out_of_range const&)
                            {
                                // (literals up to the u64 maximum: the bits of an unsigned 64 bit value)
                                try
                                {
                                    if (text.empty() || text[0] == '-')
                                        throw std::out_of_range{ text };
                                    result.i = static_cast<std::int64_t>(std::stoull(text));
                                }
                                catch (std::out_of_range const&)
                                {
                                    throw std::runtime_error("integer literal out of range: " + text);
                                }
                            }
                        }
                        else if constexpr (std::is_same_v<value_type, fbig>)
                        {
                            result.t = type::Fbig;
                            result.f = static_cast<double>(d.value().value());
                        }
                        else if constexpr (std::is_same_v<value_type, string>)
                        {
                            result.t = type::String;
                            result.s = d.value().value().to_std_string();
                        }
                        else if constexpr (std::is_same_v<value_type, boolean>)
                        {
                            result.t = type::Boolean;
                            result.i = d.value().value() ? 1 : 0;
                        }
                        else if constexpr (std::is_floating_point_v<value_type>)
                        {
                            result.t = type_to_enum_v<value_type>;
                            result.f = static_cast<double>(d.value().value());
                        }
                        else if constexpr (std::is_integral_v<value_type>)
                        {
                            result.t = type_to_enum_v<value_type>;
                            result.i = static_cast<std::int64_t>(d.value().value());
                        }
                        else
                            throw std::runtime_error("unsupported immediate value");
                    }
                }, std::get<data_type>(aOperand));
            return result;
        }

        inline data_type make_immediate(immediate const& aValue)
        {
            switch (aValue.t)
            {
            case type::Boolean:
                return data<boolean>{ aValue.i != 0 };
            case type::String:
                return data<string>{ string{ aValue.s } };
            case type::F32:
            case type::F64:
            case type::Fbig:
                {
                    std::ostringstream oss;
                    oss << std::setprecision(17) << aValue.f;
                    return data<fbig>{ fbig{ oss.str() } };
                }
            default:
                return data<ibig>{ ibig{ std::to_string(aValue.i) } };
            }
        }
    }
}
