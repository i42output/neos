/*
  validate.cpp

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

// C: a check of a generated function's code (its operand stack's types, its blocks): a diagnostic of the compiler (when the
// environment variable NEOS_C_VALIDATE is set) that reports the first instruction that is not valid, with the instructions
// before it. (Adapted from the Ada compiler's: compilers/ada/src/validate.cpp)

#include <neolib/neolib.hpp>
#include <neolib/core/pair.hpp>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>
#include <neos/language/emit.hpp>
#include "codegen.hpp"

namespace neos::compilers::c
{
    namespace
    {
        char type_char(type aType)
        {
            switch (aType)
            {
            case type::I32: return 'i';
            case type::I64: return 'l';
            case type::F32: return 'f';
            case type::F64: return 'd';
            case type::String: return 'i';
            case type::Void: return 'v';
            default: return '?';
            }
        }

        template <typename T>
        char sig_char()
        {
            if constexpr (std::is_same_v<T, bytecode::type::I32>)
                return 'i';
            else if constexpr (std::is_same_v<T, bytecode::type::I64>)
                return 'l';
            else if constexpr (std::is_same_v<T, bytecode::type::F32>)
                return 'f';
            else if constexpr (std::is_same_v<T, bytecode::type::F64>)
                return 'd';
            else if constexpr (std::is_same_v<T, bytecode::type::Void>)
                return 'v';
            else
                return '?';
        }

        // (an opcode's signature: its result then its operands, e.g. "lll")
        std::string signature_of(bytecode::sig::set const& aSig)
        {
            return std::visit([](auto const& t)
                {
                    std::string result;
                    std::apply([&](auto const&... e) { ((result += sig_char<std::decay_t<decltype(e)>>()), ...); }, t);
                    return result;
                }, aSig);
        }

        char block_type(std::uint8_t aByte)
        {
            switch (aByte)
            {
            case 0x7Fu: return 'i';
            case 0x7Eu: return 'l';
            case 0x7Du: return 'f';
            case 0x7Cu: return 'd';
            default: return 'v';
            }
        }
    }

    void program::note_function_type(std::uint32_t aFunction, std::vector<type> const& aParameters, type aResult)
    {
        std::string s;
        s += type_char(aResult);
        for (auto p : aParameters)
            s += type_char(p);
        iFunctionTypes[aFunction] = s;
    }

    void program::note_indirect_type(std::uint32_t aSignature, std::vector<type> const& aParameters, type aResult)
    {
        std::string s;
        s += type_char(aResult);
        for (auto p : aParameters)
            s += type_char(p);
        iIndirectTypes[aSignature] = s;
    }

    void program::note_global_type(std::uint32_t aGlobal, type aType)
    {
        iGlobalTypes[aGlobal] = type_char(aType);
    }

    bool function_generator::validate(std::string const& aName)
    {
        static bool const sEnabled = std::getenv("NEOS_C_VALIDATE") != nullptr || std::getenv("NEOS_C_DUMP") != nullptr;
        if (!sEnabled)
            return true;
        static char const* const sDump = std::getenv("NEOS_C_DUMP");
        bool const dump = sDump != nullptr && aName == sDump;
        std::string locals;
        for (auto t : iParameters)
            locals += type_char(t);
        for (auto t : iLocals)
            locals += type_char(t);
        char const result = type_char(iProgram.wasm_result(iType));
        std::set<std::size_t> relocated;
        for (auto const& r : iRelocations)
            relocated.insert(r.first);
        struct frame
        {
            char kind;              // b(lock), l(oop), i(f), e(lse), f(unction)
            char result;
            std::size_t height;
            bool unreachable = false;
        };
        std::vector<frame> frames{ frame{ 'f', result, 0u } };
        std::string stack;
        std::vector<std::string> trace;
        std::size_t pc = 0u;
        auto const& code = iCode;
        auto const fail = [&](std::string const& aMessage)
        {
            std::fprintf(stderr, "c: invalid code in %s at %zu: %s\n", aName.c_str(), pc, aMessage.c_str());
            auto const from = trace.size() > 40u ? trace.size() - 40u : 0u;
            for (auto i = from; i < trace.size(); ++i)
                std::fprintf(stderr, "  %s\n", trace[i].c_str());
            return false;
        };
        auto const leb = [&]() -> std::uint64_t
        {
            std::uint64_t value = 0u;
            unsigned shift = 0u;
            while (pc < code.size())
            {
                auto const b = code[pc++];
                value |= static_cast<std::uint64_t>(b & 0x7Fu) << shift;
                shift += 7u;
                if ((b & 0x80u) == 0u)
                    break;
            }
            return value;
        };
        auto const sleb = [&]() -> std::int64_t
        {
            std::int64_t value = 0;
            unsigned shift = 0u;
            std::uint8_t b = 0u;
            do
            {
                b = code[pc++];
                value |= static_cast<std::int64_t>(b & 0x7Fu) << shift;
                shift += 7u;
            } while ((b & 0x80u) != 0u && pc < code.size());
            if (shift < 64u && (b & 0x40u) != 0u)
                value |= -(std::int64_t{ 1 } << shift);
            return value;
        };
        auto const pop = [&](char aType) -> bool
        {
            auto& f = frames.back();
            if (stack.size() <= f.height)
                return f.unreachable;
            auto const top = stack.back();
            stack.pop_back();
            return aType == '*' || top == aType;
        };
        auto const push = [&](char aType) { if (aType != 'v') stack += aType; };
        auto const apply = [&](std::string const& aSig) -> bool
        {
            // (result first, then operands in order: popped in reverse)
            for (std::size_t i = aSig.size(); i-- > 1u;)
                if (!pop(aSig[i]))
                    return false;
            push(aSig[0]);
            return true;
        };
        auto const label_type = [&](std::size_t aDepth) -> std::optional<char>
        {
            if (aDepth >= frames.size())
                return {};
            auto const& f = frames[frames.size() - 1u - aDepth];
            return f.kind == 'l' ? 'v' : f.result;
        };
        while (pc < code.size())
        {
            auto const at = pc;
            std::uint32_t value = code[pc++];
            if (value == 0xFBu || value == 0xFCu || value == 0xFDu || value == 0xFEu)
                value = (value << 8u) | static_cast<std::uint32_t>(leb());
            auto const entry = bytecode::opcode_dictionary().find(static_cast<opcode>(value));
            if (entry == bytecode::opcode_dictionary().end())
                return fail("unknown opcode " + std::to_string(value));
            auto const o = static_cast<opcode>(value);
            std::ostringstream line;
            line << at << ": " << entry->second.op;
            bool ok = true;
            switch (o)
            {
            case opcode::Unreachable:
                stack.resize(frames.back().height);
                frames.back().unreachable = true;
                break;
            case opcode::Nop:
                break;
            case opcode::Block:
            case opcode::Loop:
            case opcode::If:
                {
                    auto const bt = block_type(code[pc++]);
                    if (o == opcode::If && !pop('i'))
                    {
                        ok = false;
                        break;
                    }
                    frames.push_back(frame{ o == opcode::Block ? 'b' : o == opcode::Loop ? 'l' : 'i', bt, stack.size() });
                    line << " " << bt;
                    break;
                }
            case opcode::Else:
                {
                    auto& f = frames.back();
                    if (f.kind != 'i')
                        return fail("else without if");
                    if (f.result != 'v' && !pop(f.result))
                        return fail("if's result missing");
                    if (stack.size() != f.height)
                        return fail("values left at else: " + stack.substr(f.height));
                    f.kind = 'e';
                    f.unreachable = false;
                    break;
                }
            case opcode::End:
                {
                    auto f = frames.back();
                    if (f.result != 'v' && !pop(f.result))
                        return fail("block's result missing (stack: " + stack + ")");
                    if (stack.size() != f.height)
                        return fail("values left at end: " + stack.substr(f.height));
                    if (f.kind == 'i' && f.result != 'v')
                        return fail("if with a result has no else");
                    frames.pop_back();
                    if (frames.empty() && dump)
                        for (auto const& l : trace)
                            std::fprintf(stderr, "  %s\n", l.c_str());
                    if (frames.empty())
                    {
                        if (pc != code.size())
                            return fail("code after the function's end");
                        return true;
                    }
                    push(f.result);
                    break;
                }
            case opcode::Br:
            case opcode::BrIf:
                {
                    auto const depth = leb();
                    line << " " << depth;
                    if (o == opcode::BrIf && !pop('i'))
                    {
                        ok = false;
                        break;
                    }
                    auto const lt = label_type(depth);
                    if (!lt)
                        return fail("branch depth out of range");
                    if (*lt != 'v')
                    {
                        if (!pop(*lt))
                        {
                            ok = false;
                            break;
                        }
                        if (o == opcode::BrIf)
                            push(*lt);
                    }
                    if (o == opcode::Br)
                    {
                        stack.resize(frames.back().height);
                        frames.back().unreachable = true;
                    }
                    break;
                }
            case opcode::BrTable:
                {
                    auto const n = leb();
                    for (std::uint64_t i = 0u; i <= n; ++i)
                        leb();
                    ok = pop('i');
                    stack.resize(frames.back().height);
                    frames.back().unreachable = true;
                    break;
                }
            case opcode::Return:
                if (result != 'v' && !pop(result))
                    return fail("return value missing");
                stack.resize(frames.back().height);
                frames.back().unreachable = true;
                break;
            case opcode::CallFunction:
                {
                    if (relocated.find(pc) == relocated.end())
                        return fail("call without a relocation");
                    std::uint32_t function = 0u;
                    for (auto const& r : iRelocations)
                        if (r.first == pc)
                            function = r.second;
                    pc += 5u;
                    auto const t = iProgram.iFunctionTypes.find(function);
                    if (t == iProgram.iFunctionTypes.end())
                        return fail("call of an unknown function " + std::to_string(function));
                    line << " " << function << " (" << t->second << ")";
                    ok = apply(t->second);
                    break;
                }
            case opcode::CallIndirect:
                {
                    std::uint32_t signature = 0u;
                    for (auto const& r : iRelocations)
                        if (r.first == pc)
                            signature = r.second & ~IndirectSignature;
                    pc += 5u;
                    ++pc;   // (the table)
                    auto const t = iProgram.iIndirectTypes.find(signature);
                    if (t == iProgram.iIndirectTypes.end())
                        return fail("call_indirect of an unknown signature");
                    line << " (" << t->second << ")";
                    ok = pop('i') && apply(t->second);
                    break;
                }
            case opcode::Drop:
                ok = pop('*');
                break;
            case opcode::Select:
                {
                    ok = pop('i');
                    auto& f = frames.back();
                    if (ok && stack.size() >= f.height + 2u)
                    {
                        auto const b = stack.back();
                        stack.pop_back();
                        auto const a = stack.back();
                        ok = a == b;
                        if (!ok)
                            line << " (" << a << " " << b << ")";
                    }
                    else if (ok && !f.unreachable)
                        ok = false;
                    break;
                }
            case opcode::LocalGet:
            case opcode::LocalSet:
            case opcode::LocalTee:
                {
                    auto const index = leb();
                    line << " " << index;
                    if (index >= locals.size())
                        return fail("no local " + std::to_string(index));
                    auto const t = locals[index];
                    line << " (" << t << ")";
                    if (o == opcode::LocalGet)
                        push(t);
                    else
                    {
                        ok = pop(t);
                        if (o == opcode::LocalTee)
                            push(t);
                    }
                    break;
                }
            case opcode::GlobalGet:
            case opcode::GlobalSet:
                {
                    auto const index = static_cast<std::uint32_t>(leb());
                    auto const g = iProgram.iGlobalTypes.find(index);
                    char const t = g != iProgram.iGlobalTypes.end() ? g->second : 'l';
                    line << " " << index;
                    if (o == opcode::GlobalGet)
                        push(t);
                    else
                        ok = pop(t);
                    break;
                }
            case opcode::I32Const:
                if (relocated.find(pc) != relocated.end())
                    pc += 5u;
                else
                    line << " " << sleb();
                push('i');
                break;
            case opcode::I64Const:
                line << " " << sleb();
                push('l');
                break;
            case opcode::F32Const:
                pc += 4u;
                push('f');
                break;
            case opcode::F64Const:
                pc += 8u;
                push('d');
                break;
            default:
                {
                    auto const sig = signature_of(entry->second.sig);
                    bool const memory = entry->second.op.find(".load") != std::string::npos || entry->second.op.find(".store") != std::string::npos ||
                        entry->second.op.find(".atomic.") != std::string::npos;
                    if (o == opcode::AtomicFence)
                        ++pc;   // (its reserved byte)
                    if (memory)
                    {
                        auto const align = leb();
                        std::uint64_t index = 0u;
                        if (align & 0x40u)
                            index = leb();
                        auto const offset = leb();
                        line << " mem " << index << " +" << offset;
                        // (the C memory: 64-bit addresses)
                        auto s = sig;
                        if (index == language::c::layout::Memory && s.size() >= 2u)
                            s[1] = 'l';
                        ok = apply(s);
                        break;
                    }
                    if (sig.empty())
                        return fail("opcode not checked: " + entry->second.op);
                    ok = apply(sig);
                    break;
                }
            }
            line << "    [" << stack << "]";
            trace.push_back(line.str());
            if (!ok)
                return fail("operand types (" + entry->second.op + ")");
        }
        if (frames.size() == 1u && dump)
            for (auto const& l : trace)
                std::fprintf(stderr, "  %s\n", l.c_str());
        if (frames.size() == 1u)
        {
            if (result != 'v' && !pop(result))
                return fail("the function's result missing (stack: " + stack + ")");
            if (stack.size() != 0u && !frames.back().unreachable)
                return fail("values left at the function's end: " + stack);
            return true;
        }
        return fail("blocks not ended");
    }
}
