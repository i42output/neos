/*
  jit.cpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  This program is free software: you can redistribute it and / or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <neos/neos.hpp>
#include <cstring>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <exception>
#include <algorithm>
#include <optional>
#include <string>
#include <initializer_list>
#include <map>
#include <cstdio>
#include <cstdlib>
#include <neos/bytecode/vm/jit.hpp>

#if defined(_M_X64) || defined(__x86_64__)
#define NEOS_JIT_X64 1
#endif

#if defined(NEOS_JIT_X64)
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <pthread.h>
#endif
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

// A single pass JIT compiler from (pre-decoded) WebAssembly to x86-64:
// - the most used locals of each function are kept in registers (rbx, r12-r14, xmm8-xmm15); the others in the frame
// - the operand stack is virtual: constants and local reads are not materialized until used (constants become
//   immediate operands), values live in scratch registers (rax, rcx, rdx, rsi, rdi, r8-r11, xmm0-xmm7) and are
//   spilled to fixed stack slots only under register pressure or at control flow merges and calls
// - comparisons are fused with the conditional branch/select that consumes them
// - integer division by a constant uses a multiply by its "magic" reciprocal
// - traps unwind to the entry trampoline (no exception passes through generated code)
// Internal calling convention: arguments in the caller's outgoing area ([rsp] at the call), the first result in rax
// (raw bits), further results in the context; rbx, rbp, r12-r15 and xmm8-xmm15 are preserved; r15 holds the context.

namespace neos
{
    namespace bytecode
    {
        namespace vm
        {
            namespace jit
            {
                namespace
                {
                    enum trap_code : std::uint32_t
                    {
                        NoTrap = 0,
                        TrapUnreachable = 1,
                        TrapDivideByZero = 2,
                        TrapIntegerOverflow = 3,
                        TrapInvalidConversion = 4,
                        TrapStackExhausted = 5,
                        TrapHostException = 6,
                        TrapOutOfBounds = 7,
                        TrapCount
                    };

                    char const* trap_message(std::uint32_t aTrap)
                    {
                        switch (aTrap)
                        {
                        case TrapUnreachable: return "unreachable executed";
                        case TrapDivideByZero: return "integer divide by zero";
                        case TrapIntegerOverflow: return "integer overflow";
                        case TrapInvalidConversion: return "invalid conversion to integer";
                        case TrapStackExhausted: return "call stack exhausted";
                        case TrapOutOfBounds: return "out of bounds memory access";
                        default: return "trap";
                        }
                    }

                    constexpr std::size_t MaxResults = 64u;

                    struct context
                    {
                        std::uint64_t stackLimit;
                        std::uint64_t savedStackPointer;
                        std::uint32_t trap;
                        std::uint32_t argumentCount;
                        value const* arguments;
                        value* globals;
                        memory const* linearMemory;
                        compiled_module* module;
                        value results[MaxResults];
                    };

                    constexpr std::int32_t ContextStackLimit = static_cast<std::int32_t>(offsetof(context, stackLimit));
                    constexpr std::int32_t ContextSavedStackPointer = static_cast<std::int32_t>(offsetof(context, savedStackPointer));
                    constexpr std::int32_t ContextTrap = static_cast<std::int32_t>(offsetof(context, trap));
                    constexpr std::int32_t ContextArgumentCount = static_cast<std::int32_t>(offsetof(context, argumentCount));
                    constexpr std::int32_t ContextArguments = static_cast<std::int32_t>(offsetof(context, arguments));
                    constexpr std::int32_t ContextGlobals = static_cast<std::int32_t>(offsetof(context, globals));
                    constexpr std::int32_t ContextMemory = static_cast<std::int32_t>(offsetof(context, linearMemory));
                    constexpr std::int32_t MemoryBase = static_cast<std::int32_t>(offsetof(memory, base));
                    constexpr std::int32_t MemorySize = static_cast<std::int32_t>(offsetof(memory, size));
                    constexpr std::int32_t ContextResults = static_cast<std::int32_t>(offsetof(context, results));

                    // value types
                    constexpr std::uint8_t I32 = 0x7Fu;
                    constexpr std::uint8_t I64 = 0x7Eu;
                    constexpr std::uint8_t F32 = 0x7Du;
                    constexpr std::uint8_t F64 = 0x7Cu;

                    bool is_float(std::uint8_t aType) { return aType == F32 || aType == F64; }
                    bool is_wide(std::uint8_t aType) { return aType == I64 || aType == F64; }
                }

                class compiled_module
                {
                public:
                    ~compiled_module();
                public:
                    module_description description;
                    host_function host;
                    void* code = nullptr;
                    std::size_t codeSize = 0u;
                    std::uint32_t(*trampoline)(context*, void const*) = nullptr;
                    std::vector<std::size_t> functionOffsets;
                    std::exception_ptr exception;
                };

                void compiled_module_deleter::operator()(compiled_module* aModule) const
                {
                    delete aModule;
                }

#if defined(NEOS_JIT_X64)

                namespace
                {
                    using bytes = std::vector<std::uint8_t>;

                    enum gpr : std::uint8_t { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };

                    // condition codes
                    enum cc : std::uint8_t { O, NO, B, AE, E, NE, BE, A, S, NS, P, NP, L, GE, LE, G };

                    cc invert(std::uint8_t aCc) { return static_cast<cc>(aCc ^ 1u); }

                    bool fits_i8(std::int64_t aValue) { return aValue >= -128 && aValue <= 127; }
                    bool fits_i32(std::int64_t aValue) { return aValue >= std::numeric_limits<std::int32_t>::min() && aValue <= std::numeric_limits<std::int32_t>::max(); }

                    struct unsupported : std::runtime_error { using std::runtime_error::runtime_error; };

                    class assembler
                    {
                    public:
                        bytes code;
                    private:
                        static constexpr std::size_t NoFixup = ~std::size_t{};
                        struct label
                        {
                            std::optional<std::size_t> offset;
                            std::vector<std::size_t> fixups; // positions of rel32 fields
                        };
                        std::vector<label> iLabels;
                        std::map<std::uint64_t, std::size_t> iConstants;
                        // (cold code (rarely executed): emitted to its own section, which follows the function's code)
                        bytes iCold;
                        bool iInCold = false;
                        std::vector<std::size_t> iColdLabels;
                        std::vector<std::pair<std::size_t, std::size_t>> iColdFixups; ///< (label, fixup index)
                        std::optional<std::pair<std::size_t, std::size_t>> iLastJump; ///< (the jmp that code ends with: its offset and label)
                        std::vector<std::size_t> iBoundAfterJump;   ///< (labels bound after that jmp)
                        bytes& out() { return iInCold ? iCold : code; }
                    public:
                        std::size_t offset() const { return iInCold ? iCold.size() : code.size(); }
                        std::size_t new_label() { iLabels.emplace_back(); return iLabels.size() - 1u; }
                        void bind(std::size_t aLabel)
                        {
                            if (!iInCold && iLastJump)
                            {
                                if (iLastJump->second == aLabel)
                                {
                                    // (a jmp to the code that follows it: removed (labels bound after it move back to where it was))
                                    auto& fixups = iLabels[aLabel].fixups;
                                    *std::find(fixups.begin(), fixups.end(), iLastJump->first + 1u) = NoFixup;
                                    code.resize(iLastJump->first);
                                    for (auto const l : iBoundAfterJump)
                                        iLabels[l].offset = code.size();
                                    iLastJump.reset();
                                    iBoundAfterJump.clear();
                                }
                                else
                                    iBoundAfterJump.push_back(aLabel);
                            }
                            iLabels[aLabel].offset = offset();
                            if (iInCold)
                                iColdLabels.push_back(aLabel);
                        }
                        bool in_cold() const { return iInCold; }
                        void begin_cold() { iInCold = true; }
                        void end_cold() { iInCold = false; }
                        // (the end of a function: its cold code follows it)
                        void append_cold()
                        {
                            iLastJump.reset();
                            iBoundAfterJump.clear();
                            auto const base = code.size();
                            code.insert(code.end(), iCold.begin(), iCold.end());
                            iCold.clear();
                            for (auto const l : iColdLabels)
                                *iLabels[l].offset += base;
                            iColdLabels.clear();
                            for (auto const& f : iColdFixups)
                                iLabels[f.first].fixups[f.second] += base;
                            iColdFixups.clear();
                        }
                        std::size_t label_offset(std::size_t aLabel) const { return iLabels[aLabel].offset.value(); }
                        void resolve()
                        {
                            for (auto const& l : iLabels)
                                for (auto f : l.fixups)
                                {
                                    if (f == NoFixup)
                                        continue;
                                    if (!l.offset)
                                        throw std::logic_error("neos::jit: unbound label");
                                    auto const rel = static_cast<std::int32_t>(static_cast<std::int64_t>(*l.offset) - static_cast<std::int64_t>(f + 4u));
                                    std::memcpy(&code[f], &rel, sizeof(rel));
                                }
                            // a jump (jmp or jcc) to a jmp: to the jmp's destination
                            auto const destination = [&](std::size_t aField)
                            {
                                std::int32_t rel;
                                std::memcpy(&rel, &code[aField], sizeof(rel));
                                return static_cast<std::size_t>(static_cast<std::int64_t>(aField + 4u) + rel);
                            };
                            for (auto const& l : iLabels)
                                for (auto f : l.fixups)
                                {
                                    if (f == NoFixup)
                                        continue;
                                    bool const jump = (f >= 1u && code[f - 1u] == 0xE9u) || (f >= 2u && code[f - 2u] == 0x0Fu && (code[f - 1u] & 0xF0u) == 0x80u);
                                    if (!jump)
                                        continue;
                                    auto target = destination(f);
                                    for (int hops = 0; hops < 8 && target + 5u <= code.size() && code[target] == 0xE9u; ++hops)
                                    {
                                        auto const next = destination(target + 1u);
                                        if (next == target)
                                            break;
                                        target = next;
                                    }
                                    auto const rel = static_cast<std::int32_t>(static_cast<std::int64_t>(target) - static_cast<std::int64_t>(f + 4u));
                                    std::memcpy(&code[f], &rel, sizeof(rel));
                                }
                        }
                    public:
                        void emit8(std::uint8_t aByte)
                        {
                            if (!iInCold)
                            {
                                iLastJump.reset();
                                iBoundAfterJump.clear();
                            }
                            out().push_back(aByte);
                        }
                        void emit32(std::uint32_t aValue) { for (int i = 0; i < 4; ++i) emit8(static_cast<std::uint8_t>(aValue >> (i * 8))); }
                        void emit64(std::uint64_t aValue) { for (int i = 0; i < 8; ++i) emit8(static_cast<std::uint8_t>(aValue >> (i * 8))); }
                        void patch32(std::size_t aPosition, std::uint32_t aValue) { std::memcpy(&code[aPosition], &aValue, sizeof(aValue)); }
                    private:
                        void rex(bool aW, int aR, int aX, int aB, bool aForce = false)
                        {
                            std::uint8_t const r = static_cast<std::uint8_t>(0x40u | (aW ? 8u : 0u) | ((aR & 8) ? 4u : 0u) | ((aX & 8) ? 2u : 0u) | ((aB & 8) ? 1u : 0u));
                            if (r != 0x40u || aForce)
                                emit8(r);
                        }
                        void modrm_mem(int aReg, int aBase, std::int32_t aDisp)
                        {
                            std::uint8_t const reg = static_cast<std::uint8_t>((aReg & 7) << 3);
                            std::uint8_t const base = static_cast<std::uint8_t>(aBase & 7);
                            std::uint8_t mod;
                            if (aDisp == 0 && base != 5u)
                                mod = 0x00u;
                            else if (fits_i8(aDisp))
                                mod = 0x40u;
                            else
                                mod = 0x80u;
                            emit8(static_cast<std::uint8_t>(mod | reg | base));
                            if (base == 4u)
                                emit8(0x24u); // SIB: base only
                            if (mod == 0x40u)
                                emit8(static_cast<std::uint8_t>(static_cast<std::int8_t>(aDisp)));
                            else if (mod == 0x80u)
                                emit32(static_cast<std::uint32_t>(aDisp));
                        }
                    public:
                        // generic encodings: [prefix] [REX] opcode modrm
                        void op_rr(std::initializer_list<std::uint8_t> aOpcode, bool aW, int aReg, int aRm, int aPrefix = -1, bool aByteRm = false, bool aByteReg = false)
                        {
                            if (aPrefix >= 0)
                                emit8(static_cast<std::uint8_t>(aPrefix));
                            rex(aW, aReg, 0, aRm, (aByteRm && aRm >= 4 && aRm < 8) || (aByteReg && aReg >= 4 && aReg < 8));
                            for (auto b : aOpcode)
                                emit8(b);
                            emit8(static_cast<std::uint8_t>(0xC0u | ((aReg & 7) << 3) | (aRm & 7)));
                        }
                        void op_rm(std::initializer_list<std::uint8_t> aOpcode, bool aW, int aReg, int aBase, std::int32_t aDisp, int aPrefix = -1)
                        {
                            if (aPrefix >= 0)
                                emit8(static_cast<std::uint8_t>(aPrefix));
                            rex(aW, aReg, 0, aBase);
                            for (auto b : aOpcode)
                                emit8(b);
                            modrm_mem(aReg, aBase, aDisp);
                        }
                        // [base + index * 8]
                        void op_sib8(std::initializer_list<std::uint8_t> aOpcode, bool aW, int aReg, int aBase, int aIndex)
                        {
                            rex(aW, aReg, aIndex, aBase);
                            for (auto b : aOpcode)
                                emit8(b);
                            if ((aBase & 7) == 5)
                            {
                                emit8(static_cast<std::uint8_t>(0x44u | ((aReg & 7) << 3)));
                                emit8(static_cast<std::uint8_t>(0xC0u | ((aIndex & 7) << 3) | (aBase & 7)));
                                emit8(0u);
                            }
                            else
                            {
                                emit8(static_cast<std::uint8_t>(0x04u | ((aReg & 7) << 3)));
                                emit8(static_cast<std::uint8_t>(0xC0u | ((aIndex & 7) << 3) | (aBase & 7)));
                            }
                        }
                    public:
                        // moves
                        void mov_rr(bool aW, int aDst, int aSrc) { op_rr({ 0x89 }, aW, aSrc, aDst); }
                        void mov_ri(int aDst, std::uint64_t aImm, bool aW)
                        {
                            if (!aW || aImm <= 0xFFFFFFFFull)
                            {
                                rex(false, 0, 0, aDst);
                                emit8(static_cast<std::uint8_t>(0xB8u + (aDst & 7)));
                                emit32(static_cast<std::uint32_t>(aImm));
                            }
                            else if (fits_i32(static_cast<std::int64_t>(aImm)))
                            {
                                op_rr({ 0xC7 }, true, 0, aDst);
                                emit32(static_cast<std::uint32_t>(aImm));
                            }
                            else
                            {
                                rex(true, 0, 0, aDst);
                                emit8(static_cast<std::uint8_t>(0xB8u + (aDst & 7)));
                                emit64(aImm);
                            }
                        }
                        void load(bool aW, int aDst, int aBase, std::int32_t aDisp) { op_rm({ 0x8B }, aW, aDst, aBase, aDisp); }
                        void store(bool aW, int aBase, std::int32_t aDisp, int aSrc) { op_rm({ 0x89 }, aW, aSrc, aBase, aDisp); }
                        void store_imm(bool aW, int aBase, std::int32_t aDisp, std::int32_t aImm) { op_rm({ 0xC7 }, aW, 0, aBase, aDisp); emit32(static_cast<std::uint32_t>(aImm)); }
                        void lea(int aDst, int aBase, std::int32_t aDisp) { op_rm({ 0x8D }, true, aDst, aBase, aDisp); }
                        void movsxd(int aDst, int aSrc) { op_rr({ 0x63 }, true, aDst, aSrc); }
                        void movzx8(int aDst, int aSrc) { op_rr({ 0x0F, 0xB6 }, false, aDst, aSrc, -1, true); }
                        void movsx8(int aDst, int aSrc) { op_rr({ 0x0F, 0xBE }, false, aDst, aSrc, -1, true); }
                        void movsx16(int aDst, int aSrc) { op_rr({ 0x0F, 0xBF }, false, aDst, aSrc); }
                        // arithmetic (aOp: add 0, or 1, and 4, sub 5, xor 6, cmp 7)
                        void alu_rr(int aOp, bool aW, int aDst, int aSrc) { op_rr({ static_cast<std::uint8_t>(aOp * 8 + 1) }, aW, aSrc, aDst); }
                        void alu_rm(int aOp, bool aW, int aDst, int aBase, std::int32_t aDisp) { op_rm({ static_cast<std::uint8_t>(aOp * 8 + 3) }, aW, aDst, aBase, aDisp); }
                        void alu_mr(int aOp, bool aW, int aBase, std::int32_t aDisp, int aSrc) { op_rm({ static_cast<std::uint8_t>(aOp * 8 + 1) }, aW, aSrc, aBase, aDisp); }
                        void alu_ri(int aOp, bool aW, int aDst, std::int32_t aImm)
                        {
                            if (fits_i8(aImm))
                            {
                                op_rr({ 0x83 }, aW, aOp, aDst);
                                emit8(static_cast<std::uint8_t>(static_cast<std::int8_t>(aImm)));
                            }
                            else
                            {
                                op_rr({ 0x81 }, aW, aOp, aDst);
                                emit32(static_cast<std::uint32_t>(aImm));
                            }
                        }
                        void alu_mi(int aOp, bool aW, int aBase, std::int32_t aDisp, std::int32_t aImm)
                        {
                            if (fits_i8(aImm))
                            {
                                op_rm({ 0x83 }, aW, aOp, aBase, aDisp);
                                emit8(static_cast<std::uint8_t>(static_cast<std::int8_t>(aImm)));
                            }
                            else
                            {
                                op_rm({ 0x81 }, aW, aOp, aBase, aDisp);
                                emit32(static_cast<std::uint32_t>(aImm));
                            }
                        }
                        void sub_rsp_imm32(std::int32_t aImm) { op_rr({ 0x81 }, true, 5, RSP); emit32(static_cast<std::uint32_t>(aImm)); }
                        void test_rr(bool aW, int aA, int aB) { op_rr({ 0x85 }, aW, aB, aA); }
                        void test_ri(int aReg, std::int32_t aImm) { op_rr({ 0xF7 }, false, 0, aReg); emit32(static_cast<std::uint32_t>(aImm)); }
                        void test_mi(int aBase, std::int32_t aDisp, std::int32_t aImm) { op_rm({ 0xF7 }, false, 0, aBase, aDisp); emit32(static_cast<std::uint32_t>(aImm)); }
                        void imul_rr(bool aW, int aDst, int aSrc) { op_rr({ 0x0F, 0xAF }, aW, aDst, aSrc); }
                        void imul_rm(bool aW, int aDst, int aBase, std::int32_t aDisp) { op_rm({ 0x0F, 0xAF }, aW, aDst, aBase, aDisp); }
                        void imul_rri(bool aW, int aDst, int aSrc, std::int32_t aImm)
                        {
                            if (fits_i8(aImm))
                            {
                                op_rr({ 0x6B }, aW, aDst, aSrc);
                                emit8(static_cast<std::uint8_t>(static_cast<std::int8_t>(aImm)));
                            }
                            else
                            {
                                op_rr({ 0x69 }, aW, aDst, aSrc);
                                emit32(static_cast<std::uint32_t>(aImm));
                            }
                        }
                        // F7 group: neg 3, mul 4, imul 5, div 6, idiv 7
                        void f7(int aDigit, bool aW, int aReg) { op_rr({ 0xF7 }, aW, aDigit, aReg); }
                        void cqo(bool aW) { if (aW) emit8(0x48u); emit8(0x99u); }
                        // shifts: rol 0, ror 1, shl 4, shr 5, sar 7
                        void shift_cl(int aDigit, bool aW, int aReg) { op_rr({ 0xD3 }, aW, aDigit, aReg); }
                        void shift_ri(int aDigit, bool aW, int aReg, std::uint8_t aCount) { op_rr({ 0xC1 }, aW, aDigit, aReg); emit8(aCount); }
                        // bit test and complement/reset/set: bts 5, btr 6, btc 7
                        void bt_ri(int aDigit, bool aW, int aReg, std::uint8_t aBit) { op_rr({ 0x0F, 0xBA }, aW, aDigit, aReg); emit8(aBit); }
                        void setcc(std::uint8_t aCc, int aReg) { op_rr({ 0x0F, static_cast<std::uint8_t>(0x90u + aCc) }, false, 0, aReg, -1, true); }
                        void cmov_rr(std::uint8_t aCc, bool aW, int aDst, int aSrc) { op_rr({ 0x0F, static_cast<std::uint8_t>(0x40u + aCc) }, aW, aDst, aSrc); }
                        void cmov_rm(std::uint8_t aCc, bool aW, int aDst, int aBase, std::int32_t aDisp) { op_rm({ 0x0F, static_cast<std::uint8_t>(0x40u + aCc) }, aW, aDst, aBase, aDisp); }
                        void push(int aReg) { rex(false, 0, 0, aReg); emit8(static_cast<std::uint8_t>(0x50u + (aReg & 7))); }
                        void pop(int aReg) { rex(false, 0, 0, aReg); emit8(static_cast<std::uint8_t>(0x58u + (aReg & 7))); }
                        void ret() { emit8(0xC3u); }
                        void call_r(int aReg) { op_rr({ 0xFF }, false, 2, aReg); }
                        void jmp(std::size_t aLabel)
                        {
                            auto const at = offset();
                            emit8(0xE9u);
                            fixup(aLabel);
                            if (!iInCold)
                                iLastJump = std::make_pair(at, aLabel);
                        }
                        void jcc(std::uint8_t aCc, std::size_t aLabel) { emit8(0x0Fu); emit8(static_cast<std::uint8_t>(0x80u + aCc)); fixup(aLabel); }
                        void call(std::size_t aLabel) { emit8(0xE8u); fixup(aLabel); }
                        // SSE
                        void sse_rr(int aPrefix, std::uint8_t aOp, bool aW, int aReg, int aRm) { op_rr({ 0x0F, aOp }, aW, aReg, aRm, aPrefix); }
                        void sse_rm(int aPrefix, std::uint8_t aOp, bool aW, int aReg, int aBase, std::int32_t aDisp) { op_rm({ 0x0F, aOp }, aW, aReg, aBase, aDisp, aPrefix); }
                        void movf_load(bool aDouble, int aX, int aBase, std::int32_t aDisp) { sse_rm(aDouble ? 0xF2 : 0xF3, 0x10, false, aX, aBase, aDisp); }
                        void movf_store(bool aDouble, int aBase, std::int32_t aDisp, int aX) { sse_rm(aDouble ? 0xF2 : 0xF3, 0x11, false, aX, aBase, aDisp); }
                        void movapd(int aDst, int aSrc) { sse_rr(0x66, 0x28, false, aDst, aSrc); }
                        void xorpd(int aDst, int aSrc) { sse_rr(0x66, 0x57, false, aDst, aSrc); }
                        void movq_xg(bool aW, int aX, int aG) { sse_rr(0x66, 0x6E, aW, aX, aG); }
                        void movq_gx(bool aW, int aG, int aX) { sse_rr(0x66, 0x7E, aW, aX, aG); }
                        void ucomis(bool aDouble, int aA, int aB) { sse_rr(aDouble ? 0x66 : -1, 0x2E, false, aA, aB); }
                        void ucomis_m(bool aDouble, int aA, int aBase, std::int32_t aDisp) { sse_rm(aDouble ? 0x66 : -1, 0x2E, false, aA, aBase, aDisp); }
                        void round(bool aDouble, int aDst, int aSrc, std::uint8_t aMode) { op_rr({ 0x0F, 0x3A, static_cast<std::uint8_t>(aDouble ? 0x0B : 0x0A) }, false, aDst, aSrc, 0x66); emit8(aMode); }
                        // [rip + constant]
                        void sse_rip(int aPrefix, std::uint8_t aOp, int aReg, std::size_t aLabel)
                        {
                            if (aPrefix >= 0)
                                emit8(static_cast<std::uint8_t>(aPrefix));
                            rex(false, aReg, 0, 0);
                            emit8(0x0Fu);
                            emit8(aOp);
                            emit8(static_cast<std::uint8_t>(0x05u | ((aReg & 7) << 3)));
                            fixup(aLabel);
                        }
                        std::size_t constant(std::uint64_t aBits)
                        {
                            auto existing = iConstants.find(aBits);
                            if (existing != iConstants.end())
                                return existing->second;
                            auto const label = new_label();
                            iConstants.emplace(aBits, label);
                            return label;
                        }
                        void emit_constants()
                        {
                            while (code.size() % 8u != 0u)
                                emit8(0xCCu);
                            for (auto const& c : iConstants)
                            {
                                bind(c.second);
                                emit64(c.first);
                            }
                        }
                        void movdqu_store(int aBase, std::int32_t aDisp, int aX) { sse_rm(0xF3, 0x7F, false, aX, aBase, aDisp); }
                        void movdqu_load(int aX, int aBase, std::int32_t aDisp) { sse_rm(0xF3, 0x6F, false, aX, aBase, aDisp); }
                    private:
                        void fixup(std::size_t aLabel)
                        {
                            iLabels[aLabel].fixups.push_back(offset());
                            if (iInCold)
                                iColdFixups.emplace_back(aLabel, iLabels[aLabel].fixups.size() - 1u);
                            emit32(0u);
                        }
                    };

#if defined(_WIN32)
                    constexpr bool Windows = true;
                    constexpr int NativeIntegerArguments[] = { RCX, RDX, R8, R9 };
#else
                    constexpr bool Windows = false;
                    constexpr int NativeIntegerArguments[] = { RDI, RSI, RDX, RCX, R8, R9 };
#endif
                    constexpr std::int32_t ShadowSpace = 32; // (also reserved on System V for simplicity)

                    // (pinned registers hold locals: a function saves those it uses that native code preserves (they are callee saved), 
                    // and saves the others around calls (they are caller saved))
#if defined(_WIN32)
                    constexpr int ScratchGprs[] = { RAX, RCX, RDX, R8, R9, R10, R11 };
                    constexpr int PinnedGprs[] = { RBX, R12, R13, R14, RSI, RDI };
#else
                    constexpr int ScratchGprs[] = { RAX, RCX, RDX, RSI, RDI, R8, R9 };
                    constexpr int PinnedGprs[] = { RBX, R12, R13, R14, R10, R11 };
#endif
                    bool native_volatile(int aReg)
                    {
                        return aReg == RAX || aReg == RCX || aReg == RDX || (aReg >= R8 && aReg <= R11) || (!Windows && (aReg == RSI || aReg == RDI));
                    }
                    constexpr int ScratchXmms[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
                    constexpr int PinnedXmms[] = { 8, 9, 10, 11, 12, 13, 14, 15 };

                    constexpr int Free = -1;
                    constexpr int Locked = -2;
                    constexpr int Unavailable = -3;

                    bool has_sse41()
                    {
#if defined(NEOS_JIT_NO_SSE41)
                        return false; // (testing: use the helper functions)
#elif defined(_MSC_VER)
                        int r[4];
                        __cpuid(r, 1);
                        return ((r[2] >> 19) & 1) != 0;
#else
                        unsigned a, b, c, d;
                        if (!__get_cpuid(1u, &a, &b, &c, &d))
                            return false;
                        return ((c >> 19) & 1u) != 0u;
#endif
                    }

                    // native helpers (called from generated code)

                    template <typename F>
                    F wasm_min(F aLhs, F aRhs)
                    {
                        if (std::isnan(aLhs) || std::isnan(aRhs))
                            return std::numeric_limits<F>::quiet_NaN();
                        if (aLhs == aRhs)
                            return std::signbit(aLhs) ? aLhs : aRhs;
                        return aLhs < aRhs ? aLhs : aRhs;
                    }
                    template <typename F>
                    F wasm_max(F aLhs, F aRhs)
                    {
                        if (std::isnan(aLhs) || std::isnan(aRhs))
                            return std::numeric_limits<F>::quiet_NaN();
                        if (aLhs == aRhs)
                            return std::signbit(aLhs) ? aRhs : aLhs;
                        return aLhs > aRhs ? aLhs : aRhs;
                    }
                    std::uint32_t i32_clz(std::uint32_t a) { std::uint32_t n = 0u; while (n < 32u && (a & (0x80000000u >> n)) == 0u) ++n; return n; }
                    std::uint32_t i32_ctz(std::uint32_t a) { std::uint32_t n = 0u; while (n < 32u && (a & (1u << n)) == 0u) ++n; return n; }
                    std::uint32_t i32_popcnt(std::uint32_t a) { std::uint32_t n = 0u; for (; a != 0u; a &= a - 1u) ++n; return n; }
                    std::uint32_t i32_rotl(std::uint32_t a, std::uint32_t b) { b &= 31u; return b == 0u ? a : (a << b) | (a >> (32u - b)); }
                    std::uint32_t i32_rotr(std::uint32_t a, std::uint32_t b) { b &= 31u; return b == 0u ? a : (a >> b) | (a << (32u - b)); }
                    std::uint64_t i64_clz(std::uint64_t a) { std::uint64_t n = 0u; while (n < 64u && (a & (0x8000000000000000ull >> n)) == 0u) ++n; return n; }
                    std::uint64_t i64_ctz(std::uint64_t a) { std::uint64_t n = 0u; while (n < 64u && (a & (1ull << n)) == 0u) ++n; return n; }
                    std::uint64_t i64_popcnt(std::uint64_t a) { std::uint64_t n = 0u; for (; a != 0u; a &= a - 1u) ++n; return n; }
                    std::uint64_t i64_rotl(std::uint64_t a, std::uint64_t b) { b &= 63u; return b == 0u ? a : (a << b) | (a >> (64u - b)); }
                    std::uint64_t i64_rotr(std::uint64_t a, std::uint64_t b) { b &= 63u; return b == 0u ? a : (a >> b) | (a << (64u - b)); }
                    double f64_min(double a, double b) { return wasm_min(a, b); }
                    double f64_max(double a, double b) { return wasm_max(a, b); }
                    float f32_min(float a, float b) { return wasm_min(a, b); }
                    float f32_max(float a, float b) { return wasm_max(a, b); }
                    double f64_ceil(double a) { return std::ceil(a); }
                    double f64_floor(double a) { return std::floor(a); }
                    double f64_trunc(double a) { return std::trunc(a); }
                    double f64_nearest(double a) { return std::nearbyint(a); }
                    float f32_ceil(float a) { return std::ceil(a); }
                    float f32_floor(float a) { return std::floor(a); }
                    float f32_trunc(float a) { return std::trunc(a); }
                    float f32_nearest(float a) { return std::nearbyint(a); }
                    double f64_from_u64(std::uint64_t a) { return static_cast<double>(a); }
                    float f32_from_u64(std::uint64_t a) { return static_cast<float>(a); }

                    template <typename T, typename F>
                    std::uint64_t truncate(F aValue, context* aContext)
                    {
                        if (std::isnan(aValue))
                        {
                            aContext->trap = TrapInvalidConversion;
                            return 0u;
                        }
                        auto const t = static_cast<long double>(std::trunc(aValue));
                        long double const lowest = static_cast<long double>(std::numeric_limits<T>::min());
                        long double const limit = static_cast<long double>(std::numeric_limits<T>::max()) + 1.0L;
                        if (t < lowest || t >= limit)
                        {
                            aContext->trap = TrapIntegerOverflow;
                            return 0u;
                        }
                        if constexpr (sizeof(T) == 4u)
                            return static_cast<std::uint32_t>(static_cast<T>(t));
                        else
                            return static_cast<std::uint64_t>(static_cast<T>(t));
                    }
                    std::uint64_t trunc_f32_i32(float a, context* c) { return truncate<std::int32_t>(a, c); }
                    std::uint64_t trunc_f32_u32(float a, context* c) { return truncate<std::uint32_t>(a, c); }
                    std::uint64_t trunc_f64_i32(double a, context* c) { return truncate<std::int32_t>(a, c); }
                    std::uint64_t trunc_f64_u32(double a, context* c) { return truncate<std::uint32_t>(a, c); }
                    std::uint64_t trunc_f32_i64(float a, context* c) { return truncate<std::int64_t>(a, c); }
                    std::uint64_t trunc_f32_u64(float a, context* c) { return truncate<std::uint64_t>(a, c); }
                    std::uint64_t trunc_f64_i64(double a, context* c) { return truncate<std::int64_t>(a, c); }
                    std::uint64_t trunc_f64_u64(double a, context* c) { return truncate<std::uint64_t>(a, c); }

                    std::uint32_t host_thunk(context* aContext, std::uint32_t aFunction, value* aBuffer) noexcept
                    {
                        try
                        {
                            aContext->module->host(aFunction, aBuffer);
                            return 0u;
                        }
                        catch (...)
                        {
                            aContext->module->exception = std::current_exception();
                            return 1u;
                        }
                    }

                    // signed division by a constant: multiplier and shift (Hacker's Delight, 10-1)
                    template <typename U>
                    std::pair<U, int> signed_magic(U aDivisor)
                    {
                        constexpr int W = static_cast<int>(sizeof(U) * 8u);
                        U const signBit = static_cast<U>(U{ 1 } << (W - 1));
                        bool const negative = (aDivisor & signBit) != 0u;
                        U const ad = negative ? static_cast<U>(U{ 0 } - aDivisor) : aDivisor;
                        U const t = static_cast<U>(signBit + (negative ? 1u : 0u));
                        U const anc = static_cast<U>(t - 1u - t % ad);
                        int p = W - 1;
                        U q1 = static_cast<U>(signBit / anc);
                        U r1 = static_cast<U>(signBit - q1 * anc);
                        U q2 = static_cast<U>(signBit / ad);
                        U r2 = static_cast<U>(signBit - q2 * ad);
                        U delta;
                        do
                        {
                            ++p;
                            q1 = static_cast<U>(q1 * 2u);
                            r1 = static_cast<U>(r1 * 2u);
                            if (r1 >= anc)
                            {
                                ++q1;
                                r1 = static_cast<U>(r1 - anc);
                            }
                            q2 = static_cast<U>(q2 * 2u);
                            r2 = static_cast<U>(r2 * 2u);
                            if (r2 >= ad)
                            {
                                ++q2;
                                r2 = static_cast<U>(r2 - ad);
                            }
                            delta = static_cast<U>(ad - r2);
                        } while (q1 < delta || (q1 == delta && r1 == 0u));
                        U magic = static_cast<U>(q2 + 1u);
                        if (negative)
                            magic = static_cast<U>(U{ 0 } - magic);
                        return { magic, p - W };
                    }

                    std::size_t immediate_count(std::uint8_t aOp, std::vector<std::uint64_t> const& aTokens, std::size_t aPosition)
                    {
                        switch (aOp)
                        {
                        case 0x02: case 0x03: case 0x04:
                        case 0x0C: case 0x0D: case 0x10: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24:
                        case 0x3F: case 0x40:
                        case 0x41: case 0x42: case 0x43: case 0x44:
                            return 1u;
                        case 0x0E:
                            return 2u + static_cast<std::size_t>(aTokens[aPosition + 1u]);
                        case 0x11:
                            return 2u;
                        default:
                            return (aOp >= 0x28u && aOp <= 0x3Eu) ? 2u : 0u;
                        }
                    }

                    class code_generator
                    {
                    private:
                        enum class kind : std::uint8_t { Const, Local, Reg, Slot, Cond };
                        struct entry
                        {
                            kind k = kind::Const;
                            std::uint8_t type = I32;
                            std::uint64_t bits = 0u;
                            int reg = 0;
                            std::uint32_t local = 0u;
                            std::uint8_t cc = 0u;
                            bool parity = false; // (float ==/!=: E means ZF=1 and PF=0, NE means ZF=0 or PF=1)
                            std::int32_t disp = 0;
                        };
                        struct operand
                        {
                            enum { Imm, Reg, Mem, Rip } k = Imm;
                            int reg = 0;
                            std::size_t label = 0u;
                            int base = 0;
                            std::int32_t disp = 0;
                            std::int64_t imm = 0;
                        };
                        struct local_info
                        {
                            std::uint8_t type = I32;
                            bool pinned = false;
                            int reg = 0;
                            std::int32_t disp = 0;
                        };
                        struct control
                        {
                            enum { Block, Loop, If, Function } kind = Block;
                            std::optional<std::uint8_t> result;
                            std::size_t base = 0u;
                            std::size_t label = 0u;
                            std::size_t elseLabel = 0u;
                            bool hasElse = false;
                            bool branched = false;
                            // (a block whose (i32) result is only tested (by the if or br_if that follows it, perhaps after an 
                            // i32.eqz): its result is not materialized; the code that delivers it jumps to where the test would)
                            struct condition_target
                            {
                                std::size_t whenTrue = 0u;      ///< (labels: where a true (non zero) result goes...)
                                std::size_t whenFalse = 0u;     ///< (...and a false one)
                                std::size_t next = 0u;          ///< (whenTrue or whenFalse: the label that follows the test)
                                std::size_t testEnd = 0u;       ///< (the tokens after the test)
                                bool isIf = false;
                                std::optional<std::uint8_t> ifResult;
                                std::size_t brTarget = 0u;      ///< (br_if: the index of its target in the control stack)
                            };
                            std::optional<condition_target> condition;
                            std::size_t bodyBegin = 0u;         ///< (a block: the position of its body)
                            // (a block whose body starts with a block that ends with a branch out of this one (br 1): what follows that 
                            // block is only reached by branches to its end (e.g. a slow path): its code is cold)
                            bool coldTail = false;
                            // (a loop whose code up to its first exit (br_if out of it) has no control flow: that code is 
                            // repeated at the branch back to the loop, which then continues after the exit (the loop is 
                            // rotated: one branch an iteration))
                            std::size_t headBegin = 0u;
                            std::size_t headEnd = 0u;           ///< (the exit; 0 if none)
                            std::optional<std::size_t> bodyLabel;
                        };
                    public:
                        code_generator(module_description const& aModule) :
                            iModule{ aModule }, iTokens{ *aModule.tokens }, iSse41{ has_sse41() }
                        {
                            for (std::size_t i = 0u; i < TrapCount; ++i)
                                iTrapLabels[i] = a.new_label();
                            iTrapFromContext = a.new_label();
                            iExit = a.new_label();
                            for (std::size_t f = 0u; f < aModule.functions.size(); ++f)
                                iFunctionLabels.push_back(a.new_label());
                        }
                    public:
                        void generate(compiled_module& aOutput)
                        {
                            trampoline();
                            for (std::uint32_t f = 0u; f < iModule.functions.size(); ++f)
                                if (!iModule.functions[f].imported)
                                    function(f);
                            trap_stubs();
                            a.emit_constants();
                            a.resolve();
                            for (std::uint32_t f = 0u; f < iModule.functions.size(); ++f)
                                aOutput.functionOffsets.push_back(iModule.functions[f].imported ? 0u : a.label_offset(iFunctionLabels[f]));
                            iCode = std::move(a.code);
                        }
                        bytes& code() { return iCode; }
                    private:
                        // --- module level code
                        void trampoline()
                        {
                            // std::uint32_t trampoline(context* ctx, void const* function): callee saved registers are saved,
                            // arguments copied from the context to the outgoing area, the function called, and its first
                            // result stored in the context; traps return here (via iExit) with the trap code in eax
                            a.push(RBP);
                            a.mov_rr(true, RBP, RSP);
                            a.push(RBX); a.push(R12); a.push(R13); a.push(R14); a.push(R15);
                            if (Windows)
                            {
                                a.push(RSI); a.push(RDI);
                                a.sub_rsp_imm32(168);
                                for (int x = 6; x < 16; ++x)
                                    a.movdqu_store(RSP, (x - 6) * 16, x);
                            }
                            else
                                a.sub_rsp_imm32(8);
                            a.mov_rr(true, R15, NativeIntegerArguments[0]);
                            a.mov_rr(true, RAX, NativeIntegerArguments[1]);
                            a.store(true, R15, ContextSavedStackPointer, RSP);
                            a.load(false, RCX, R15, ContextArgumentCount);
                            a.load(true, RDX, R15, ContextArguments);
                            // r8 = (count * 8 + 15) & ~15
                            a.op_rm({ 0x8D }, true, R8, RCX, 0); // lea r8, [rcx] (then scaled below)
                            a.shift_ri(4, true, R8, 3);
                            a.alu_ri(0, true, R8, 15);
                            a.alu_ri(4, true, R8, -16);
                            a.alu_rr(5, true, RSP, R8);
                            a.alu_rr(6, false, R9, R9);
                            auto const loop = a.new_label();
                            auto const done = a.new_label();
                            a.bind(loop);
                            a.alu_rr(7, true, R9, RCX);
                            a.jcc(AE, done);
                            a.op_sib8({ 0x8B }, true, R10, RDX, R9); // mov r10, [rdx + r9 * 8]
                            a.op_sib8({ 0x89 }, true, R10, RSP, R9); // mov [rsp + r9 * 8], r10
                            a.alu_ri(0, true, R9, 1);
                            a.jmp(loop);
                            a.bind(done);
                            a.call_r(RAX);
                            a.store(true, R15, ContextResults, RAX);
                            a.alu_rr(6, false, RAX, RAX);
                            a.bind(iExit);
                            a.load(true, RSP, R15, ContextSavedStackPointer);
                            if (Windows)
                            {
                                for (int x = 6; x < 16; ++x)
                                    a.movdqu_load(x, RSP, (x - 6) * 16);
                                a.alu_ri(0, true, RSP, 168);
                                a.pop(RDI); a.pop(RSI);
                            }
                            else
                                a.alu_ri(0, true, RSP, 8);
                            a.pop(R15); a.pop(R14); a.pop(R13); a.pop(R12); a.pop(RBX);
                            a.pop(RBP);
                            a.ret();
                        }
                        void trap_stubs()
                        {
                            for (std::uint32_t t = 1u; t < TrapCount; ++t)
                            {
                                a.bind(iTrapLabels[t]);
                                a.store_imm(false, R15, ContextTrap, static_cast<std::int32_t>(t));
                                a.mov_ri(RAX, t, false);
                                a.jmp(iExit);
                            }
                            a.bind(iTrapFromContext);
                            a.load(false, RAX, R15, ContextTrap);
                            a.jmp(iExit);
                        }
                    private:
                        // --- function level state
                        void reset_function()
                        {
                            iStack.clear();
                            iControl.clear();
                            iLocals.clear();
                            iUsedPinnedGprs.clear();
                            iNativeVolatilePinnedGprs.clear();
                            iUsedPinnedXmms.clear();
                            for (int r = 0; r < 16; ++r)
                            {
                                iGprOwner[r] = Unavailable;
                                iXmmOwner[r] = Unavailable;
                            }
                            for (auto r : ScratchGprs)
                                iGprOwner[r] = Free;
                            for (auto r : ScratchXmms)
                                iXmmOwner[r] = Free;
                            iTemps.clear();
                            iMaxDepth = 0u;
                            iOutgoing = 0;
                            iReachable = true;
                        }
                        std::int32_t pushed_bytes() const { return static_cast<std::int32_t>(iUsedPinnedGprs.size()) * 8; }
                        std::int32_t xmm_save_disp(std::size_t aIndex) const { return -pushed_bytes() - 8 * static_cast<std::int32_t>(aIndex + 1u); }
                        std::int32_t xmm_native_save_disp(std::size_t aIndex) const { return -pushed_bytes() - 8 * static_cast<std::int32_t>(iUsedPinnedXmms.size() + aIndex + 1u); }
                        std::int32_t gpr_native_save_disp(std::size_t aIndex) const { return -pushed_bytes() - 16 * static_cast<std::int32_t>(iUsedPinnedXmms.size()) - 8 * static_cast<std::int32_t>(aIndex + 1u); }
                        std::int32_t locals_base() const { return -pushed_bytes() - 16 * static_cast<std::int32_t>(iUsedPinnedXmms.size()) - 8 * static_cast<std::int32_t>(iNativeVolatilePinnedGprs.size()); }
                        std::int32_t slot_disp(std::size_t aDepth) const { return iSlotsBase - 8 * static_cast<std::int32_t>(aDepth + 1u); }
                    private:
                        // --- registers
                        int& owner(bool aXmm, int aReg) { return aXmm ? iXmmOwner[aReg] : iGprOwner[aReg]; }
                        int alloc(bool aXmm, std::uint32_t aAvoid = 0u)
                        {
                            int const* const regs = aXmm ? ScratchXmms : ScratchGprs;
                            std::size_t const count = aXmm ? std::size(ScratchXmms) : std::size(ScratchGprs);
                            for (std::size_t i = 0u; i < count; ++i)
                                if (owner(aXmm, regs[i]) == Free && (aAvoid & (1u << regs[i])) == 0u)
                                {
                                    owner(aXmm, regs[i]) = Locked;
                                    return regs[i];
                                }
                            // spill the deepest register entry
                            for (std::size_t i = 0u; i < iStack.size(); ++i)
                                if (iStack[i].k == kind::Reg && is_float(iStack[i].type) == aXmm && (aAvoid & (1u << iStack[i].reg)) == 0u)
                                {
                                    auto const r = iStack[i].reg;
                                    spill(i);
                                    owner(aXmm, r) = Locked;
                                    return r;
                                }
                            throw unsupported{ "out of registers" };
                        }
                        void release_reg(bool aXmm, int aReg)
                        {
                            if (owner(aXmm, aReg) != Unavailable)
                                owner(aXmm, aReg) = Free;
                        }
                        bool is_scratch_reg(entry const& aEntry) const { return aEntry.k == kind::Reg; }
                        void release(entry const& aEntry)
                        {
                            if (aEntry.k == kind::Reg)
                                release_reg(is_float(aEntry.type), aEntry.reg);
                        }
                        void release_temps()
                        {
                            for (auto const& t : iTemps)
                                release_reg(t.first, t.second);
                            iTemps.clear();
                        }
                        int temp(bool aXmm, std::uint32_t aAvoid = 0u)
                        {
                            auto const r = alloc(aXmm, aAvoid);
                            iTemps.emplace_back(aXmm, r);
                            return r;
                        }
                        void spill(std::size_t aIndex)
                        {
                            auto& e = iStack[aIndex];
                            if (e.k != kind::Reg)
                                return;
                            auto const disp = slot_disp(aIndex);
                            if (is_float(e.type))
                                a.movf_store(e.type == F64, RBP, disp, e.reg);
                            else
                                a.store(true, RBP, disp, e.reg);
                            release_reg(is_float(e.type), e.reg);
                            e.k = kind::Slot;
                            e.disp = disp;
                        }
                        // move a stack entry out of a register that is needed for a specific purpose
                        void evict(bool aXmm, int aReg)
                        {
                            if (owner(aXmm, aReg) >= 0)
                                spill(static_cast<std::size_t>(owner(aXmm, aReg)));
                        }
                    private:
                        // --- operand stack
                        void push(entry aEntry)
                        {
                            iStack.push_back(aEntry);
                            if (aEntry.k == kind::Reg)
                                owner(is_float(aEntry.type), aEntry.reg) = static_cast<int>(iStack.size() - 1u);
                            if (aEntry.k == kind::Slot && aEntry.disp != slot_disp(iStack.size() - 1u))
                                throw std::logic_error("neos::jit: slot mismatch");
                            iMaxDepth = std::max(iMaxDepth, iStack.size());
                        }
                        void push_reg(std::uint8_t aType, int aReg)
                        {
                            entry e;
                            e.k = kind::Reg;
                            e.type = aType;
                            e.reg = aReg;
                            push(e);
                        }
                        void push_cond(std::uint8_t aCc, bool aParity = false)
                        {
                            entry e;
                            e.k = kind::Cond;
                            e.type = I32;
                            e.cc = aCc;
                            e.parity = aParity;
                            push(e);
                        }
                        // a condition (flags) as 0/1 in a register (only moves and setcc: other flags consumers may follow)
                        void cond_to_reg(entry const& aCondition, int aReg)
                        {
                            if (!aCondition.parity)
                            {
                                a.setcc(aCondition.cc, aReg);
                                a.movzx8(aReg, aReg);
                                return;
                            }
                            auto const t = alloc(false, 1u << aReg);
                            a.mov_ri(aReg, 0u, false);
                            a.mov_ri(t, 0u, false);
                            if (aCondition.cc == E)
                            {
                                a.setcc(E, aReg);
                                a.setcc(NP, t);
                                a.alu_rr(4, false, aReg, t);
                            }
                            else
                            {
                                a.setcc(NE, aReg);
                                a.setcc(P, t);
                                a.alu_rr(1, false, aReg, t);
                            }
                            release_reg(false, t);
                        }
                        // jump if a condition (flags) is aWhen
                        void jump_on(entry const& aCondition, bool aWhen, std::size_t aLabel)
                        {
                            std::uint8_t const cc = aWhen ? aCondition.cc : invert(aCondition.cc);
                            if (!aCondition.parity)
                                a.jcc(cc, aLabel);
                            else if (cc == E)
                            {
                                auto const unordered = a.new_label();
                                a.jcc(P, unordered);
                                a.jcc(E, aLabel);
                                a.bind(unordered);
                            }
                            else
                            {
                                a.jcc(NE, aLabel);
                                a.jcc(P, aLabel);
                            }
                        }
                        entry pop()
                        {
                            if (iStack.size() <= iControl.back().base)
                                throw unsupported{ "operand stack underflow" };
                            auto const e = iStack.back();
                            iStack.pop_back();
                            if (e.k == kind::Reg)
                                owner(is_float(e.type), e.reg) = Locked;
                            return e;
                        }
                        void materialize_cond()
                        {
                            if (!iStack.empty() && iStack.back().k == kind::Cond)
                            {
                                auto const condition = iStack.back();
                                iStack.pop_back();
                                auto const r = alloc(false);
                                cond_to_reg(condition, r);
                                push_reg(I32, r);
                            }
                        }
                        // load an entry's value into a general purpose register (aWide: 64 bits)
                        void load_gpr(entry const& aEntry, int aDst)
                        {
                            bool const w = is_wide(aEntry.type);
                            switch (aEntry.k)
                            {
                            case kind::Const:
                                a.mov_ri(aDst, w ? aEntry.bits : (aEntry.bits & 0xFFFFFFFFull), w);
                                break;
                            case kind::Local:
                                {
                                    auto const& l = iLocals[aEntry.local];
                                    if (l.pinned)
                                    {
                                        if (is_float(l.type))
                                            a.movq_gx(w, aDst, l.reg);
                                        else if (l.reg != aDst)
                                            a.mov_rr(true, aDst, l.reg);
                                    }
                                    else
                                        a.load(w, aDst, RBP, l.disp);
                                }
                                break;
                            case kind::Slot:
                                a.load(w, aDst, RBP, aEntry.disp);
                                break;
                            case kind::Reg:
                                if (is_float(aEntry.type))
                                    a.movq_gx(w, aDst, aEntry.reg);
                                else if (aEntry.reg != aDst)
                                    a.mov_rr(true, aDst, aEntry.reg);
                                break;
                            case kind::Cond:
                                cond_to_reg(aEntry, aDst);
                                break;
                            }
                        }
                        void load_xmm(entry const& aEntry, int aDst)
                        {
                            bool const d = aEntry.type == F64;
                            switch (aEntry.k)
                            {
                            case kind::Const:
                                if (aEntry.bits == 0u)
                                    a.xorpd(aDst, aDst);
                                else
                                    a.sse_rip(d ? 0xF2 : 0xF3, 0x10, aDst, a.constant(d ? aEntry.bits : (aEntry.bits & 0xFFFFFFFFull)));
                                break;
                            case kind::Local:
                                {
                                    auto const& l = iLocals[aEntry.local];
                                    if (l.pinned)
                                    {
                                        if (l.reg != aDst)
                                            a.movapd(aDst, l.reg);
                                    }
                                    else
                                        a.movf_load(d, aDst, RBP, l.disp);
                                }
                                break;
                            case kind::Slot:
                                a.movf_load(d, aDst, RBP, aEntry.disp);
                                break;
                            case kind::Reg:
                                if (aEntry.reg != aDst)
                                    a.movapd(aDst, aEntry.reg);
                                break;
                            case kind::Cond:
                                throw std::logic_error("neos::jit: condition is not a float");
                            }
                        }
                        // an entry's value in a scratch register owned (locked) by the caller
                        int to_gpr(entry const& aEntry, std::uint32_t aAvoid = 0u)
                        {
                            if (aEntry.k == kind::Reg && (aAvoid & (1u << aEntry.reg)) == 0u)
                                return aEntry.reg;
                            auto const r = alloc(false, aAvoid);
                            load_gpr(aEntry, r);
                            release(aEntry);
                            return r;
                        }
                        int to_xmm(entry const& aEntry)
                        {
                            if (aEntry.k == kind::Reg)
                                return aEntry.reg;
                            auto const r = alloc(true);
                            load_xmm(aEntry, r);
                            return r;
                        }
                        // a read only operand (integer)
                        operand operand_of(entry const& aEntry, bool aAllowImmediate)
                        {
                            operand o;
                            bool const w = is_wide(aEntry.type);
                            switch (aEntry.k)
                            {
                            case kind::Const:
                                {
                                    std::int64_t const v = w ? static_cast<std::int64_t>(aEntry.bits) : static_cast<std::int64_t>(static_cast<std::int32_t>(static_cast<std::uint32_t>(aEntry.bits)));
                                    if (aAllowImmediate && fits_i32(v))
                                    {
                                        o.k = operand::Imm;
                                        o.imm = v;
                                    }
                                    else
                                    {
                                        o.k = operand::Reg;
                                        o.reg = temp(false);
                                        load_gpr(aEntry, o.reg);
                                    }
                                }
                                break;
                            case kind::Local:
                                {
                                    auto const& l = iLocals[aEntry.local];
                                    if (l.pinned)
                                    {
                                        o.k = operand::Reg;
                                        o.reg = l.reg;
                                    }
                                    else
                                    {
                                        o.k = operand::Mem;
                                        o.base = RBP;
                                        o.disp = l.disp;
                                    }
                                }
                                break;
                            case kind::Slot:
                                o.k = operand::Mem;
                                o.base = RBP;
                                o.disp = aEntry.disp;
                                break;
                            case kind::Reg:
                                o.k = operand::Reg;
                                o.reg = aEntry.reg;
                                break;
                            case kind::Cond:
                                o.k = operand::Reg;
                                o.reg = temp(false);
                                load_gpr(aEntry, o.reg);
                                break;
                            }
                            return o;
                        }
                        // a read only operand (float): register or memory
                        operand xoperand_of(entry const& aEntry)
                        {
                            operand o;
                            switch (aEntry.k)
                            {
                            case kind::Local:
                                {
                                    auto const& l = iLocals[aEntry.local];
                                    if (l.pinned)
                                    {
                                        o.k = operand::Reg;
                                        o.reg = l.reg;
                                    }
                                    else
                                    {
                                        o.k = operand::Mem;
                                        o.base = RBP;
                                        o.disp = l.disp;
                                    }
                                }
                                break;
                            case kind::Slot:
                                o.k = operand::Mem;
                                o.base = RBP;
                                o.disp = aEntry.disp;
                                break;
                            case kind::Reg:
                                o.k = operand::Reg;
                                o.reg = aEntry.reg;
                                break;
                            case kind::Const:
                                o.k = operand::Rip;
                                o.label = a.constant(aEntry.type == F64 ? aEntry.bits : (aEntry.bits & 0xFFFFFFFFull));
                                break;
                            default:
                                o.k = operand::Reg;
                                o.reg = temp(true);
                                load_xmm(aEntry, o.reg);
                                break;
                            }
                            return o;
                        }
                        // store an entry's value to memory (raw 64 bit slot; integers are zero extended)
                        void store_entry(entry const& aEntry, int aBase, std::int32_t aDisp)
                        {
                            if (is_float(aEntry.type))
                            {
                                if (aEntry.k == kind::Const)
                                {
                                    std::uint64_t const bits = aEntry.type == F64 ? aEntry.bits : (aEntry.bits & 0xFFFFFFFFull);
                                    if (bits <= 0x7FFFFFFFull)
                                        a.store_imm(true, aBase, aDisp, static_cast<std::int32_t>(bits));
                                    else
                                    {
                                        auto const t = alloc(false);
                                        a.mov_ri(t, bits, true);
                                        a.store(true, aBase, aDisp, t);
                                        release_reg(false, t);
                                    }
                                    return;
                                }
                                if (aEntry.k == kind::Slot || (aEntry.k == kind::Local && !iLocals[aEntry.local].pinned))
                                {
                                    auto const t = alloc(false);
                                    a.load(true, t, RBP, aEntry.k == kind::Slot ? aEntry.disp : iLocals[aEntry.local].disp);
                                    a.store(true, aBase, aDisp, t);
                                    release_reg(false, t);
                                    return;
                                }
                                auto const x = aEntry.k == kind::Reg ? aEntry.reg : iLocals[aEntry.local].reg;
                                a.movf_store(aEntry.type == F64, aBase, aDisp, x);
                                return;
                            }
                            switch (aEntry.k)
                            {
                            case kind::Const:
                                {
                                    std::uint64_t const bits = is_wide(aEntry.type) ? aEntry.bits : (aEntry.bits & 0xFFFFFFFFull);
                                    if (bits <= 0x7FFFFFFFull || (is_wide(aEntry.type) && fits_i32(static_cast<std::int64_t>(bits))))
                                        a.store_imm(true, aBase, aDisp, static_cast<std::int32_t>(bits));
                                    else
                                    {
                                        auto const t = alloc(false);
                                        a.mov_ri(t, bits, true);
                                        a.store(true, aBase, aDisp, t);
                                        release_reg(false, t);
                                    }
                                }
                                break;
                            case kind::Reg:
                                a.store(true, aBase, aDisp, aEntry.reg);
                                break;
                            case kind::Local:
                                if (iLocals[aEntry.local].pinned)
                                {
                                    a.store(true, aBase, aDisp, iLocals[aEntry.local].reg);
                                    break;
                                }
                                [[fallthrough]];
                            default:
                                {
                                    auto const t = alloc(false);
                                    load_gpr(aEntry, t);
                                    a.store(true, aBase, aDisp, t);
                                    release_reg(false, t);
                                }
                                break;
                            }
                        }
                        // spill all register entries (before calls)
                        void spill_registers()
                        {
                            for (std::size_t i = 0u; i < iStack.size(); ++i)
                                spill(i);
                        }
                        // canonical state for control flow merges: no entry in a register or referring to a local the block 
                        // (starting at token aBody) writes
                        void flush(std::size_t aBody)
                        {
                            materialize_cond();
                            std::vector<bool> written(iLocals.size(), false);
                            std::size_t depth = 0u;
                            for (std::size_t p = aBody; p < iFunction->codeEnd;)
                            {
                                auto const op = static_cast<std::uint8_t>(iTokens[p]);
                                if (op == 0x02 || op == 0x03 || op == 0x04)
                                    ++depth;
                                else if (op == 0x0B)
                                {
                                    if (depth == 0u)
                                        break;
                                    --depth;
                                }
                                else if ((op == 0x21 || op == 0x22) && iTokens[p + 1u] < written.size())
                                    written[static_cast<std::size_t>(iTokens[p + 1u])] = true;
                                p += 1u + immediate_count(op, iTokens, p);
                            }
                            for (std::size_t i = 0u; i < iStack.size(); ++i)
                            {
                                auto& e = iStack[i];
                                if (e.k == kind::Reg)
                                    spill(i);
                                else if (e.k == kind::Local && written[e.local])
                                    to_slot(i);
                            }
                        }
                        void to_slot(std::size_t aIndex)
                        {
                            auto& e = iStack[aIndex];
                            auto const disp = slot_disp(aIndex);
                            store_entry(e, RBP, disp);
                            e.k = kind::Slot;
                            e.disp = disp;
                        }
                        // stack entries that read a local must be materialized before the local is written
                        void detach_local(std::uint32_t aLocal)
                        {
                            for (std::size_t i = 0u; i < iStack.size(); ++i)
                                if (iStack[i].k == kind::Local && iStack[i].local == aLocal)
                                    to_slot(i);
                        }
                    private:
                        // --- pinned xmm registers across native calls (System V does not preserve xmm8-xmm15)
                        void save_pinned_xmms()
                        {
                            if (Windows)
                                return;
                            for (std::size_t i = 0u; i < iUsedPinnedXmms.size(); ++i)
                                a.movf_store(true, RBP, xmm_native_save_disp(i), iUsedPinnedXmms[i]);
                        }
                        // (pinned registers that native code does not preserve: saved around native calls)
                        void save_pinned_gprs()
                        {
                            for (std::size_t i = 0u; i < iNativeVolatilePinnedGprs.size(); ++i)
                                a.store(true, RBP, gpr_native_save_disp(i), iNativeVolatilePinnedGprs[i]);
                        }
                        void restore_pinned_gprs()
                        {
                            for (std::size_t i = 0u; i < iNativeVolatilePinnedGprs.size(); ++i)
                                a.load(true, iNativeVolatilePinnedGprs[i], RBP, gpr_native_save_disp(i));
                        }
                        void restore_pinned_xmms()
                        {
                            if (Windows)
                                return;
                            for (std::size_t i = 0u; i < iUsedPinnedXmms.size(); ++i)
                                a.movf_load(true, iUsedPinnedXmms[i], RBP, xmm_native_save_disp(i));
                        }
                        // call a native helper: arguments (floats and integers, by position) are staged in the outgoing area
                        struct native_argument { bool isFloat; bool isDouble; };
                        void native_call(void const* aFunction, std::vector<native_argument> const& aArguments, bool aCanTrap)
                        {
                            spill_registers();
                            save_pinned_gprs();
                            int intIndex = 0;
                            int floatIndex = 0;
                            for (std::size_t i = 0u; i < aArguments.size(); ++i)
                            {
                                auto const disp = ShadowSpace + static_cast<std::int32_t>(i) * 8;
                                if (aArguments[i].isFloat)
                                {
                                    int const x = Windows ? static_cast<int>(i) : floatIndex++;
                                    a.movf_load(aArguments[i].isDouble, x, RSP, disp);
                                }
                                else
                                {
                                    int const g = Windows ? NativeIntegerArguments[i] : NativeIntegerArguments[intIndex++];
                                    a.load(true, g, RSP, disp);
                                }
                            }
                            save_pinned_xmms();
                            a.mov_ri(RAX, reinterpret_cast<std::uint64_t>(aFunction), true);
                            a.call_r(RAX);
                            restore_pinned_xmms();
                            restore_pinned_gprs();
                            if (aCanTrap)
                            {
                                a.alu_mi(7, false, R15, ContextTrap, 0);
                                a.jcc(NE, iTrapFromContext);
                            }
                            iOutgoing = std::max(iOutgoing, ShadowSpace + 8 * static_cast<std::int32_t>(aArguments.size()));
                        }
                        // the native argument for the context pointer
                        void stage_context(std::size_t aPosition)
                        {
                            a.store(true, RSP, ShadowSpace + static_cast<std::int32_t>(aPosition) * 8, R15);
                        }
                    private:
                        // --- functions
                        // (code that is rarely executed: what follows a block that is the first in its enclosing block and that ends 
                        // with a branch out of that block (br 1): see control::coldTail)
                        std::optional<std::size_t> cold_tail(std::size_t aPosition) const
                        {
                            // (aPosition: a block (0x02) whose body starts with a block without a result)
                            if (iTokens[aPosition] != 0x02u || aPosition + 3u >= iFunction->codeEnd || iTokens[aPosition + 2u] != 0x02u || iTokens[aPosition + 3u] != 0x40u)
                                return {};
                            auto const childEnd = block_end(aPosition + 4u);
                            if (childEnd < 2u || childEnd >= iFunction->codeEnd || iTokens[childEnd - 2u] != 0x0Cu || iTokens[childEnd - 1u] != 1u)
                                return {};
                            return childEnd;
                        }
                        void choose_pinned_locals(std::size_t aBegin, std::size_t aEnd)
                        {
                            // (locals by their (static) uses, weighted by loop depth (and less within ifs), outside cold code; registers 
                            // that calls do not preserve only for locals used more than the calls (outside cold code) that would save them)
                            std::vector<std::uint64_t> score(iLocals.size(), 0u);
                            std::uint64_t calls = 0u;
                            std::vector<std::uint8_t> loops; // (0 block, 1 loop, 2 if)
                            std::size_t loopDepth = 0u;
                            std::size_t ifDepth = 0u;
                            std::vector<std::pair<std::size_t, std::size_t>> cold; // (cold code: [begin, end))
                            for (std::size_t p = aBegin; p < aEnd;)
                            {
                                while (!cold.empty() && p >= cold.back().second)
                                    cold.pop_back();
                                bool const isCold = !cold.empty() && p >= cold.back().first;
                                auto const op = static_cast<std::uint8_t>(iTokens[p]);
                                auto const weight = (std::uint64_t{ 64 } << (3u * std::min<std::size_t>(loopDepth, 6u))) >> std::min<std::size_t>(ifDepth, 6u);
                                switch (op)
                                {
                                case 0x02:
                                    loops.push_back(0u);
                                    if (!isCold)
                                        if (auto const tail = cold_tail(p))
                                            cold.emplace_back(*tail, block_end(p + 2u));
                                    break;
                                case 0x04:
                                    loops.push_back(2u);
                                    ++ifDepth;
                                    break;
                                case 0x03:
                                    loops.push_back(1u);
                                    ++loopDepth;
                                    break;
                                case 0x0B:
                                    if (!loops.empty())
                                    {
                                        if (loops.back() == 1u)
                                            --loopDepth;
                                        else if (loops.back() == 2u)
                                            --ifDepth;
                                        loops.pop_back();
                                    }
                                    break;
                                case 0x10:
                                    if (!isCold)
                                        calls += weight;
                                    break;
                                case 0x20: case 0x21: case 0x22:
                                    {
                                        auto const l = static_cast<std::size_t>(iTokens[p + 1u]);
                                        if (l < score.size() && !isCold)
                                            score[l] += weight;
                                    }
                                    break;
                                default:
                                    break;
                                }
                                p += 1u + immediate_count(op, iTokens, p);
                            }
                            std::vector<std::size_t> order(iLocals.size());
                            for (std::size_t i = 0u; i < order.size(); ++i)
                                order[i] = i;
                            std::stable_sort(order.begin(), order.end(), [&](std::size_t l, std::size_t r) { return score[l] > score[r]; });
                            std::size_t gprs = 0u;
                            std::size_t xmms = 0u;
                            for (auto l : order)
                            {
                                if (score[l] == 0u)
                                    break;
                                auto& local = iLocals[l];
                                if (is_float(local.type))
                                {
                                    if (xmms < std::size(PinnedXmms))
                                    {
                                        local.pinned = true;
                                        local.reg = PinnedXmms[xmms++];
                                        iUsedPinnedXmms.push_back(local.reg);
                                    }
                                }
                                else if (gprs < std::size(PinnedGprs) && (!native_volatile(PinnedGprs[gprs]) || score[l] > 2u * calls))
                                {
                                    local.pinned = true;
                                    local.reg = PinnedGprs[gprs++];
                                    if (native_volatile(local.reg))
                                        iNativeVolatilePinnedGprs.push_back(local.reg);
                                    else
                                        iUsedPinnedGprs.push_back(local.reg);
                                }
                            }
                        }
                        // (the locals (not parameters) that must be zeroed when the function starts: all except those whose first use 
                        // sets them and whose other uses are within the same region (a block's, loop's, if's or else's body, or the 
                        // function's), which can only be entered at its start)
                        std::vector<bool> locals_to_zero(std::size_t aBegin, std::size_t aEnd, std::size_t aParameters) const
                        {
                            std::vector<bool> zero(iLocals.size(), false);
                            constexpr std::size_t None = ~std::size_t{};
                            std::vector<std::size_t> region(iLocals.size(), None); // (the region of a local's first use, which set it)
                            std::vector<bool> open{ true };                         // (regions: open or closed)
                            std::vector<std::size_t> stack{ 0u };
                            for (std::size_t p = aBegin; p < aEnd;)
                            {
                                auto const op = static_cast<std::uint8_t>(iTokens[p]);
                                switch (op)
                                {
                                case 0x02: case 0x03: case 0x04:
                                    stack.push_back(open.size());
                                    open.push_back(true);
                                    break;
                                case 0x05:
                                    if (stack.size() > 1u)
                                    {
                                        open[stack.back()] = false;
                                        stack.back() = open.size();
                                        open.push_back(true);
                                    }
                                    break;
                                case 0x0B:
                                    if (stack.size() > 1u)
                                    {
                                        open[stack.back()] = false;
                                        stack.pop_back();
                                    }
                                    break;
                                case 0x20: case 0x21: case 0x22:
                                    {
                                        auto const l = static_cast<std::size_t>(iTokens[p + 1u]);
                                        if (l < aParameters || l >= iLocals.size() || zero[l])
                                            break;
                                        if (region[l] == None)
                                        {
                                            if (op == 0x20)
                                                zero[l] = true;
                                            else
                                                region[l] = stack.back();
                                        }
                                        else if (!open[region[l]])
                                            zero[l] = true;
                                    }
                                    break;
                                default:
                                    break;
                                }
                                p += 1u + immediate_count(op, iTokens, p);
                            }
                            return zero;
                        }
                        void function(std::uint32_t aIndex)
                        {
                            reset_function();
                            auto const& f = iModule.functions[aIndex];
                            iFunction = &f;
                            if (f.results->size() > MaxResults)
                                throw unsupported{ "too many results" };
                            for (auto t : *f.parameters)
                                iLocals.push_back(local_info{ t });
                            for (auto t : *f.locals)
                                iLocals.push_back(local_info{ t });
                            choose_pinned_locals(f.codeBegin, f.codeEnd);
                            std::int32_t frameLocals = 0;
                            for (auto& l : iLocals)
                                if (!l.pinned)
                                    l.disp = locals_base() - 8 * (++frameLocals);
                            iSlotsBase = locals_base() - 8 * frameLocals;
                            // prologue
                            a.bind(iFunctionLabels[aIndex]);
                            a.push(RBP);
                            a.mov_rr(true, RBP, RSP);
                            for (auto r : iUsedPinnedGprs)
                                a.push(r);
                            a.sub_rsp_imm32(0);
                            auto const frameSizePatch = a.offset() - 4u;
                            a.alu_rm(7, true, RSP, R15, ContextStackLimit);
                            a.jcc(B, iTrapLabels[TrapStackExhausted]);
                            for (std::size_t i = 0u; i < iUsedPinnedXmms.size(); ++i)
                                a.movf_store(true, RBP, xmm_save_disp(i), iUsedPinnedXmms[i]);
                            auto const zero = locals_to_zero(f.codeBegin, f.codeEnd, f.parameters->size());
                            for (std::size_t i = 0u; i < iLocals.size(); ++i)
                            {
                                auto const& l = iLocals[i];
                                if (i < f.parameters->size())
                                {
                                    std::int32_t const argument = 16 + 8 * static_cast<std::int32_t>(i);
                                    if (l.pinned)
                                    {
                                        if (is_float(l.type))
                                            a.movf_load(l.type == F64, l.reg, RBP, argument);
                                        else
                                            a.load(is_wide(l.type), l.reg, RBP, argument);
                                    }
                                    else
                                    {
                                        a.load(true, RAX, RBP, argument);
                                        a.store(true, RBP, l.disp, RAX);
                                    }
                                }
                                else if (!zero[i])
                                    continue;
                                else if (l.pinned)
                                {
                                    if (is_float(l.type))
                                        a.xorpd(l.reg, l.reg);
                                    else
                                        a.alu_rr(6, false, l.reg, l.reg);
                                }
                                else
                                    a.store_imm(true, RBP, l.disp, 0);
                            }
                            // body
                            iEpilogue = a.new_label();
                            control c;
                            c.kind = control::Function;
                            c.label = iEpilogue;
                            iControl.push_back(c);
                            body(f.codeBegin, f.codeEnd);
                            // epilogue
                            a.bind(iEpilogue);
                            for (std::size_t i = 0u; i < iUsedPinnedXmms.size(); ++i)
                                a.movf_load(true, iUsedPinnedXmms[i], RBP, xmm_save_disp(i));
                            a.lea(RSP, RBP, -pushed_bytes());
                            for (auto r = iUsedPinnedGprs.rbegin(); r != iUsedPinnedGprs.rend(); ++r)
                                a.pop(*r);
                            a.pop(RBP);
                            a.ret();
                            if (a.in_cold())
                                throw std::logic_error("neos::jit: cold code");
                            a.append_cold();
                            // frame size (keeping rsp 16 byte aligned)
                            std::int32_t const raw = -iSlotsBase - pushed_bytes() + 8 * static_cast<std::int32_t>(iMaxDepth) + std::max(iOutgoing, ShadowSpace);
                            std::int32_t const total = (pushed_bytes() + raw + 15) & ~15;
                            a.patch32(frameSizePatch, static_cast<std::uint32_t>(total - pushed_bytes()));
                        }
                        // move the function's results into place (first in rax, others in the context)
                        void transfer_results()
                        {
                            auto const& results = *iFunction->results;
                            materialize_cond();
                            for (std::size_t i = results.size(); i-- > 0u;)
                            {
                                auto const e = pop();
                                if (i == 0u)
                                {
                                    if (is_float(e.type))
                                    {
                                        auto const x = to_xmm(e);
                                        a.movq_gx(e.type == F64, RAX, x);
                                        release_reg(true, x);
                                    }
                                    else
                                    {
                                        evict(false, RAX);
                                        load_gpr(e, RAX);
                                        release(e);
                                    }
                                }
                                else
                                {
                                    store_entry(e, R15, ContextResults + 8 * static_cast<std::int32_t>(i));
                                    release(e);
                                }
                            }
                        }
                        // a block's result value to rax/xmm0 (aPop: false for br_if, which leaves it on the stack); afterwards 
                        // the register is free (the block's end pushes it)
                        void transfer_block_result(control const& aTarget, bool aPop)
                        {
                            if (!aTarget.result)
                                return;
                            bool const xmm = is_float(*aTarget.result);
                            int const target = xmm ? 0 : RAX;
                            if (aPop)
                            {
                                materialize_cond();
                                auto const e = pop();
                                if (!(e.k == kind::Reg && e.reg == target))
                                {
                                    evict(xmm, target);
                                    if (xmm)
                                        load_xmm(e, target);
                                    else
                                        load_gpr(e, target);
                                    release(e);
                                }
                                owner(xmm, target) = Free;
                            }
                            else
                            {
                                auto const e = iStack.back();
                                if (!(e.k == kind::Reg && e.reg == target))
                                {
                                    evict(xmm, target);
                                    if (xmm)
                                        load_xmm(e, target);
                                    else
                                        load_gpr(e, target);
                                    owner(xmm, target) = Free;
                                }
                            }
                        }
                        // the position of the end of the block whose body starts at aBody
                        std::size_t block_end(std::size_t aBody) const
                        {
                            std::size_t depth = 0u;
                            for (std::size_t p = aBody; p < iFunction->codeEnd;)
                            {
                                auto const op = static_cast<std::uint8_t>(iTokens[p]);
                                if (op == 0x02 || op == 0x03 || op == 0x04)
                                    ++depth;
                                else if (op == 0x0B)
                                {
                                    if (depth == 0u)
                                        return p;
                                    --depth;
                                }
                                p += 1u + immediate_count(op, iTokens, p);
                            }
                            return iFunction->codeEnd;
                        }
                        // a block (whose body starts at aBody) whose result is only tested (see control::condition)
                        std::optional<control::condition_target> condition_block(std::size_t aBody)
                        {
                            auto p = block_end(aBody);
                            if (p >= iFunction->codeEnd)
                                return {};
                            ++p;
                            bool const inverted = p < iFunction->codeEnd && iTokens[p] == 0x45u;
                            if (inverted)
                                ++p;
                            if (p + 1u >= iFunction->codeEnd || (iTokens[p] != 0x04u && iTokens[p] != 0x0Du))
                                return {};
                            control::condition_target t;
                            t.isIf = iTokens[p] == 0x04u;
                            t.testEnd = p + 2u;
                            t.next = a.new_label();
                            std::size_t other;
                            if (t.isIf)
                            {
                                if (iTokens[p + 1u] != 0x40u)
                                {
                                    auto const type = static_cast<std::uint8_t>(iTokens[p + 1u]);
                                    if (type != I32 && type != I64 && type != F32 && type != F64)
                                        return {};
                                    t.ifResult = type;
                                }
                                other = a.new_label();      // (the if's else)
                            }
                            else
                            {
                                auto const depth = static_cast<std::size_t>(iTokens[p + 1u]);
                                if (depth >= iControl.size())
                                    return {};
                                t.brTarget = iControl.size() - 1u - depth;
                                auto const& target = iControl[t.brTarget];
                                if (target.kind == control::Function || (target.kind != control::Loop && target.result))
                                    return {};
                                other = target.label;
                            }
                            // (if: true goes to its body (next); br_if: true goes to its target (other); i32.eqz: the reverse)
                            bool const trueIsNext = t.isIf != inverted;
                            t.whenTrue = trueIsNext ? t.next : other;
                            t.whenFalse = trueIsNext ? other : t.next;
                            return t;
                        }
                        // a result delivered to a condition block (see control::condition) (aFallThrough: the block's end; else a branch)
                        void deliver_condition(control::condition_target const& aTarget, entry const& aValue, bool aFallThrough)
                        {
                            auto const finish = [&](std::size_t aOther)
                            {
                                if (!aFallThrough)
                                    a.jmp(aOther);
                            };
                            switch (aValue.k)
                            {
                            case kind::Cond:
                                if (aTarget.whenTrue == aTarget.next)
                                {
                                    jump_on(aValue, false, aTarget.whenFalse);
                                    finish(aTarget.whenTrue);
                                }
                                else
                                {
                                    jump_on(aValue, true, aTarget.whenTrue);
                                    finish(aTarget.whenFalse);
                                }
                                break;
                            case kind::Const:
                                {
                                    auto const destination = (aValue.bits & 0xFFFFFFFFull) != 0u ? aTarget.whenTrue : aTarget.whenFalse;
                                    if (!aFallThrough || destination != aTarget.next)
                                        a.jmp(destination);
                                }
                                break;
                            default:
                                test_value(aValue);
                                if (aTarget.whenTrue == aTarget.next)
                                {
                                    a.jcc(E, aTarget.whenFalse);
                                    finish(aTarget.whenTrue);
                                }
                                else
                                {
                                    a.jcc(NE, aTarget.whenTrue);
                                    finish(aTarget.whenFalse);
                                }
                                break;
                            }
                        }
                        // the end of a condition block: its test follows (aNext is advanced past it)
                        bool condition_end(std::size_t& aNext)
                        {
                            if (iControl.empty() || !iControl.back().condition)
                                return false;
                            auto const c = iControl.back();
                            auto const& t = *c.condition;
                            if (iReachable)
                            {
                                auto const value = pop();
                                deliver_condition(t, value, !c.coldTail);
                                release(value);
                                release_temps();
                            }
                            unwind(c.base);
                            iControl.pop_back();
                            if (c.coldTail)
                                a.end_cold();
                            a.bind(c.label);
                            if (t.isIf)
                            {
                                control i;
                                i.kind = control::If;
                                i.result = t.ifResult;
                                i.base = iStack.size();
                                i.label = a.new_label();
                                i.elseLabel = t.whenTrue == t.next ? t.whenFalse : t.whenTrue;
                                iControl.push_back(i);
                            }
                            else
                                iControl[t.brTarget].branched = true;
                            a.bind(t.next);
                            iReachable = true;
                            aNext = t.testEnd;
                            return true;
                        }
                        // drop the operand stack back to a control frame's base
                        void unwind(std::size_t aBase)
                        {
                            while (iStack.size() > aBase)
                            {
                                auto const e = iStack.back();
                                iStack.pop_back();
                                release(e);
                            }
                        }
                        // a loop's code up to its first exit (see control::headBegin)
                        void find_loop_head(control& aLoop, std::size_t aPosition)
                        {
                            std::size_t constexpr MaxHead = 48u; // (tokens)
                            auto const begin = aPosition + 1u + immediate_count(0x03, iTokens, aPosition);
                            for (std::size_t p = begin; p < iFunction->codeEnd && p - begin < MaxHead;)
                            {
                                auto const op = static_cast<std::uint8_t>(iTokens[p]);
                                switch (op)
                                {
                                case 0x0D:
                                    if (p > begin && iTokens[p + 1u] >= 1u)
                                    {
                                        aLoop.headBegin = begin;
                                        aLoop.headEnd = p;
                                    }
                                    return;
                                case 0x00: case 0x02: case 0x03: case 0x04: case 0x05: case 0x0B: case 0x0C: case 0x0E: case 0x0F: 
                                case 0x11:
                                    return;
                                default:
                                    break;
                                }
                                p += 1u + immediate_count(op, iTokens, p);
                            }
                        }
                        // a branch back to a rotated loop: its head is repeated here, then the loop continues after its exit
                        bool rotate_loop(std::size_t aDepth)
                        {
                            if (aDepth >= iControl.size())
                                return false;
                            auto const& loop = iControl[iControl.size() - 1u - aDepth];
                            if (loop.kind != control::Loop || !loop.bodyLabel || iStack.size() != loop.base)
                                return false;
                            auto const exitDepth = static_cast<std::size_t>(iTokens[loop.headEnd + 1u]) + aDepth;
                            if (exitDepth >= iControl.size())
                                return false;
                            if (auto const& exit = iControl[iControl.size() - 1u - exitDepth]; exit.result || exit.kind == control::Function)
                                return false;
                            auto const bodyLabel = *loop.bodyLabel;
                            for (std::size_t p = loop.headBegin; p < loop.headEnd;)
                            {
                                auto const op = static_cast<std::uint8_t>(iTokens[p]);
                                auto const position = p;
                                p += 1u + immediate_count(op, iTokens, p);
                                instruction(op, position, [&](std::size_t aIndex) { return iTokens[position + 1u + aIndex]; });
                            }
                            // (continue unless the exit is taken)
                            auto const condition = pop();
                            jump_unless(condition, bodyLabel, false);
                            branch(exitDepth);
                            return true;
                        }
                        void branch(std::size_t aDepth)
                        {
                            if (aDepth >= iControl.size())
                                throw unsupported{ "invalid branch depth" };
                            auto& target = iControl[iControl.size() - 1u - aDepth];
                            if (target.kind == control::Function)
                            {
                                transfer_results();
                                a.jmp(iEpilogue);
                                return;
                            }
                            target.branched = true;
                            if (target.condition)
                            {
                                auto const value = pop();
                                deliver_condition(*target.condition, value, false);
                                release(value);
                                release_temps();
                                return;
                            }
                            if (target.kind != control::Loop)
                                transfer_block_result(target, true);
                            a.jmp(target.label);
                        }
                        void conditional_branch(std::size_t aDepth)
                        {
                            if (aDepth >= iControl.size())
                                throw unsupported{ "invalid branch depth" };
                            auto& target = iControl[iControl.size() - 1u - aDepth];
                            auto const condition = pop();
                            if (target.kind == control::Function)
                            {
                                // a conditional return
                                auto const skip = a.new_label();
                                jump_unless(condition, skip, false);
                                auto const saved = iStack;
                                int savedGpr[16], savedXmm[16];
                                std::copy(std::begin(iGprOwner), std::end(iGprOwner), savedGpr);
                                std::copy(std::begin(iXmmOwner), std::end(iXmmOwner), savedXmm);
                                transfer_results();
                                a.jmp(iEpilogue);
                                iStack = saved;
                                std::copy(std::begin(savedGpr), std::end(savedGpr), iGprOwner);
                                std::copy(std::begin(savedXmm), std::end(savedXmm), iXmmOwner);
                                a.bind(skip);
                                return;
                            }
                            target.branched = true;
                            if (target.condition)
                            {
                                // (the result stays for the code that follows: delivered if the branch is taken)
                                auto const skip = a.new_label();
                                jump_unless(condition, skip, false);
                                auto const value = iStack.back();
                                if (value.k == kind::Cond)
                                    throw unsupported{ "condition result" };
                                if (value.k == kind::Const)
                                    deliver_condition(*target.condition, value, false);
                                else
                                {
                                    auto const o = operand_of(value, false);
                                    if (o.k == operand::Reg)
                                        a.test_rr(false, o.reg, o.reg);
                                    else
                                        a.alu_mi(7, false, o.base, o.disp, 0);
                                    release_temps();
                                    a.jcc(NE, target.condition->whenTrue);
                                    a.jmp(target.condition->whenFalse);
                                }
                                a.bind(skip);
                                return;
                            }
                            if (target.kind != control::Loop && target.result)
                            {
                                // test the condition first (its register may be the result register), then copy the result 
                                // to rax/xmm0 on both paths (the moves preserve the flags)
                                entry flags = condition;
                                if (condition.k != kind::Cond)
                                {
                                    test_value(condition);
                                    flags = entry{};
                                    flags.k = kind::Cond;
                                    flags.cc = NE;
                                }
                                transfer_block_result(target, false);
                                jump_on(flags, true, target.label);
                                return;
                            }
                            jump_if(condition, target.label);
                        }
                        // test a condition and jump (consumes the popped condition entry)
                        void jump_if(entry const& aCondition, std::size_t aLabel)
                        {
                            if (aCondition.k == kind::Cond)
                                jump_on(aCondition, true, aLabel);
                            else
                            {
                                test_value(aCondition);
                                a.jcc(NE, aLabel);
                            }
                        }
                        void jump_unless(entry const& aCondition, std::size_t aLabel, bool)
                        {
                            if (aCondition.k == kind::Cond)
                                jump_on(aCondition, false, aLabel);
                            else
                            {
                                test_value(aCondition);
                                a.jcc(E, aLabel);
                            }
                        }
                        void test_value(entry const& aValue)
                        {
                            bool const w = is_wide(aValue.type);
                            auto const o = operand_of(aValue, false);
                            if (o.k == operand::Reg)
                                a.test_rr(w, o.reg, o.reg);
                            else
                                a.alu_mi(7, w, o.base, o.disp, 0);
                            release(aValue);
                            release_temps();
                        }
                    private:
                        // --- instructions
                        void int_binary(int aAluOp, bool aW, bool aCommutative)
                        {
                            materialize_cond();
                            auto rhs = pop();
                            auto lhs = pop();
                            if (aCommutative && lhs.k == kind::Const && rhs.k != kind::Const)
                                std::swap(lhs, rhs);
                            auto const dst = to_gpr(lhs);
                            auto const src = operand_of(rhs, true);
                            switch (src.k)
                            {
                            case operand::Imm:
                                a.alu_ri(aAluOp, aW, dst, static_cast<std::int32_t>(src.imm));
                                break;
                            case operand::Reg:
                                a.alu_rr(aAluOp, aW, dst, src.reg);
                                break;
                            case operand::Mem:
                                a.alu_rm(aAluOp, aW, dst, src.base, src.disp);
                                break;
                            }
                            release(rhs);
                            release_temps();
                            push_reg(aW ? I64 : I32, dst);
                        }
                        void int_multiply(bool aW)
                        {
                            materialize_cond();
                            auto rhs = pop();
                            auto lhs = pop();
                            if (lhs.k == kind::Const && rhs.k != kind::Const)
                                std::swap(lhs, rhs);
                            auto const src = operand_of(rhs, true);
                            int dst;
                            if (src.k == operand::Imm)
                            {
                                if (lhs.k == kind::Reg)
                                    dst = lhs.reg;
                                else
                                {
                                    auto const l = operand_of(lhs, false);
                                    dst = alloc(false);
                                    if (l.k == operand::Reg)
                                        a.imul_rri(aW, dst, l.reg, static_cast<std::int32_t>(src.imm));
                                    else
                                    {
                                        load_gpr(lhs, dst);
                                        a.imul_rri(aW, dst, dst, static_cast<std::int32_t>(src.imm));
                                    }
                                    release_temps();
                                    push_reg(aW ? I64 : I32, dst);
                                    return;
                                }
                                a.imul_rri(aW, dst, dst, static_cast<std::int32_t>(src.imm));
                            }
                            else
                            {
                                dst = to_gpr(lhs);
                                if (src.k == operand::Reg)
                                    a.imul_rr(aW, dst, src.reg);
                                else
                                    a.imul_rm(aW, dst, src.base, src.disp);
                            }
                            release(rhs);
                            release_temps();
                            push_reg(aW ? I64 : I32, dst);
                        }
                        void int_compare(bool aW, std::uint8_t aCc)
                        {
                            materialize_cond();
                            auto rhs = pop();
                            auto lhs = pop();
                            if (lhs.k == kind::Const && rhs.k != kind::Const)
                            {
                                std::swap(lhs, rhs);
                                switch (aCc)
                                {
                                case L: aCc = G; break;
                                case G: aCc = L; break;
                                case LE: aCc = GE; break;
                                case GE: aCc = LE; break;
                                case B: aCc = A; break;
                                case A: aCc = B; break;
                                case BE: aCc = AE; break;
                                case AE: aCc = BE; break;
                                default: break;
                                }
                            }
                            auto l = operand_of(lhs, false);
                            auto const r = operand_of(rhs, true);
                            if (l.k == operand::Mem && r.k == operand::Mem)
                            {
                                auto const t = temp(false);
                                load_gpr(lhs, t);
                                l.k = operand::Reg;
                                l.reg = t;
                            }
                            if (l.k == operand::Reg)
                            {
                                if (r.k == operand::Imm)
                                    a.alu_ri(7, aW, l.reg, static_cast<std::int32_t>(r.imm));
                                else if (r.k == operand::Reg)
                                    a.alu_rr(7, aW, l.reg, r.reg);
                                else
                                    a.alu_rm(7, aW, l.reg, r.base, r.disp);
                            }
                            else
                            {
                                if (r.k == operand::Imm)
                                    a.alu_mi(7, aW, l.base, l.disp, static_cast<std::int32_t>(r.imm));
                                else
                                    a.alu_mr(7, aW, l.base, l.disp, r.reg);
                            }
                            release(lhs);
                            release(rhs);
                            release_temps();
                            push_cond(aCc);
                        }
                        void eqz(bool aW)
                        {
                            if (!aW && !iStack.empty() && iStack.back().k == kind::Cond)
                            {
                                iStack.back().cc = invert(iStack.back().cc);
                                return;
                            }
                            materialize_cond();
                            auto const v = pop();
                            auto const o = operand_of(v, false);
                            if (o.k == operand::Reg)
                                a.test_rr(aW, o.reg, o.reg);
                            else
                                a.alu_mi(7, aW, o.base, o.disp, 0);
                            release(v);
                            release_temps();
                            push_cond(E);
                        }
                        void shift(int aDigit, bool aW)
                        {
                            materialize_cond();
                            auto rhs = pop();
                            auto lhs = pop();
                            if (rhs.k == kind::Const)
                            {
                                auto const dst = to_gpr(lhs);
                                a.shift_ri(aDigit, aW, dst, static_cast<std::uint8_t>(rhs.bits & (aW ? 63u : 31u)));
                                push_reg(aW ? I64 : I32, dst);
                                return;
                            }
                            // the count must be in cl
                            if (!(rhs.k == kind::Reg && rhs.reg == RCX))
                            {
                                if (lhs.k == kind::Reg && lhs.reg == RCX)
                                {
                                    auto const moved = alloc(false, 1u << RCX);
                                    a.mov_rr(true, moved, RCX);
                                    lhs.reg = moved;
                                    owner(false, RCX) = Free;
                                }
                                evict(false, RCX);
                                load_gpr(rhs, RCX);
                                release(rhs);
                                owner(false, RCX) = Locked;
                            }
                            auto const dst = to_gpr(lhs, 1u << RCX);
                            a.shift_cl(aDigit, aW, dst);
                            release_reg(false, RCX);
                            push_reg(aW ? I64 : I32, dst);
                        }
                        void divide(bool aW, bool aSigned, bool aRemainder)
                        {
                            materialize_cond();
                            auto rhs = pop();
                            auto lhs = pop();
                            int const bits = aW ? 64 : 32;
                            if (rhs.k == kind::Const)
                            {
                                std::int64_t const d = aW ? static_cast<std::int64_t>(rhs.bits) : static_cast<std::int64_t>(static_cast<std::int32_t>(static_cast<std::uint32_t>(rhs.bits)));
                                std::uint64_t const ud = aW ? rhs.bits : (rhs.bits & 0xFFFFFFFFull);
                                if (!aSigned && ud != 0u && (ud & (ud - 1u)) == 0u)
                                {
                                    // unsigned power of two
                                    int k = 0;
                                    while ((std::uint64_t{ 1 } << k) != ud)
                                        ++k;
                                    auto const dst = to_gpr(lhs);
                                    if (aRemainder)
                                    {
                                        if (k <= 31)
                                            a.alu_ri(4, aW, dst, static_cast<std::int32_t>(static_cast<std::uint32_t>((std::uint64_t{ 1 } << k) - 1u)));
                                        else
                                        {
                                            auto const t = temp(false);
                                            a.mov_ri(t, (std::uint64_t{ 1 } << k) - 1u, true);
                                            a.alu_rr(4, aW, dst, t);
                                            release_temps();
                                        }
                                    }
                                    else if (k > 0)
                                        a.shift_ri(5, aW, dst, static_cast<std::uint8_t>(k));
                                    push_reg(aW ? I64 : I32, dst);
                                    return;
                                }
                                std::int64_t const minimum = aW ? std::numeric_limits<std::int64_t>::min() : std::numeric_limits<std::int32_t>::min();
                                if (aSigned && d >= 2 && (d & (d - 1)) == 0 && d <= (std::int64_t{ 1 } << 30))
                                {
                                    // signed power of two (truncating): bias negative dividends by d - 1
                                    int k = 0;
                                    while ((std::int64_t{ 1 } << k) != d)
                                        ++k;
                                    auto const x = to_gpr(lhs);
                                    auto const t = alloc(false);
                                    a.mov_rr(true, t, x);
                                    a.shift_ri(7, aW, t, static_cast<std::uint8_t>(bits - 1));
                                    a.shift_ri(5, aW, t, static_cast<std::uint8_t>(bits - k));
                                    a.alu_rr(0, aW, t, x);
                                    if (aRemainder)
                                    {
                                        a.alu_ri(4, aW, t, static_cast<std::int32_t>(-d));
                                        a.alu_rr(5, aW, x, t);
                                        release_reg(false, t);
                                        push_reg(aW ? I64 : I32, x);
                                    }
                                    else
                                    {
                                        a.shift_ri(7, aW, t, static_cast<std::uint8_t>(k));
                                        release_reg(false, x);
                                        push_reg(aW ? I64 : I32, t);
                                    }
                                    return;
                                }
                                if (aSigned && d != 0 && d != 1 && d != -1 && d != minimum)
                                {
                                    // signed: multiply by the magic number
                                    std::int64_t magic;
                                    int s;
                                    if (aW)
                                    {
                                        auto const m = signed_magic<std::uint64_t>(static_cast<std::uint64_t>(d));
                                        magic = static_cast<std::int64_t>(m.first);
                                        s = m.second;
                                    }
                                    else
                                    {
                                        auto const m = signed_magic<std::uint32_t>(static_cast<std::uint32_t>(d));
                                        magic = static_cast<std::int32_t>(m.first);
                                        s = m.second;
                                    }
                                    std::uint32_t const avoid = (1u << RAX) | (1u << RDX);
                                    if (lhs.k == kind::Reg && (lhs.reg == RAX || lhs.reg == RDX))
                                    {
                                        auto const moved = alloc(false, avoid);
                                        a.mov_rr(true, moved, lhs.reg);
                                        owner(false, lhs.reg) = Free;
                                        lhs.reg = moved;
                                    }
                                    evict(false, RAX);
                                    evict(false, RDX);
                                    owner(false, RAX) = Locked;
                                    owner(false, RDX) = Locked;
                                    auto const xo = operand_of(lhs, false);
                                    int x;
                                    if (xo.k == operand::Reg)
                                        x = xo.reg;
                                    else
                                    {
                                        x = temp(false, avoid);
                                        load_gpr(lhs, x);
                                    }
                                    a.mov_ri(RAX, aW ? static_cast<std::uint64_t>(magic) : static_cast<std::uint32_t>(magic), aW);
                                    a.f7(5, aW, x); // imul: rdx:rax = x * magic
                                    if (d > 0 && magic < 0)
                                        a.alu_rr(0, aW, RDX, x);
                                    else if (d < 0 && magic > 0)
                                        a.alu_rr(5, aW, RDX, x);
                                    if (s > 0)
                                        a.shift_ri(7, aW, RDX, static_cast<std::uint8_t>(s));
                                    a.mov_rr(aW, RAX, RDX);
                                    a.shift_ri(5, aW, RAX, static_cast<std::uint8_t>(bits - 1));
                                    a.alu_rr(0, aW, RDX, RAX);
                                    int result = RDX;
                                    if (aRemainder)
                                    {
                                        // x - q * d
                                        if (fits_i32(d))
                                            a.imul_rri(aW, RDX, RDX, static_cast<std::int32_t>(d));
                                        else
                                        {
                                            a.mov_ri(RAX, static_cast<std::uint64_t>(d), true);
                                            a.imul_rr(aW, RDX, RAX);
                                        }
                                        a.mov_rr(true, RAX, x);
                                        a.alu_rr(5, aW, RAX, RDX);
                                        result = RAX;
                                    }
                                    release(lhs);
                                    release_temps();
                                    release_reg(false, result == RAX ? RDX : RAX);
                                    push_reg(aW ? I64 : I32, result);
                                    return;
                                }
                            }
                            // general case: idiv/div with checks
                            std::uint32_t const avoid = (1u << RAX) | (1u << RDX);
                            int divisor;
                            if (rhs.k == kind::Local && iLocals[rhs.local].pinned)
                                divisor = iLocals[rhs.local].reg;
                            else if (rhs.k == kind::Reg && rhs.reg != RAX && rhs.reg != RDX)
                                divisor = rhs.reg;
                            else
                            {
                                divisor = alloc(false, avoid | (lhs.k == kind::Reg ? (1u << lhs.reg) : 0u));
                                load_gpr(rhs, divisor);
                                release(rhs);
                                rhs.k = kind::Reg;
                                rhs.reg = divisor;
                            }
                            if (lhs.k == kind::Reg && lhs.reg == RDX)
                            {
                                evict(false, RAX);
                                a.mov_rr(true, RAX, RDX);
                                owner(false, RDX) = Free;
                                lhs.reg = RAX;
                                owner(false, RAX) = Locked;
                            }
                            evict(false, RDX);
                            if (!(lhs.k == kind::Reg && lhs.reg == RAX))
                            {
                                evict(false, RAX);
                                load_gpr(lhs, RAX);
                                release(lhs);
                            }
                            owner(false, RAX) = Locked;
                            owner(false, RDX) = Locked;
                            a.test_rr(aW, divisor, divisor);
                            a.jcc(E, iTrapLabels[TrapDivideByZero]);
                            if (aSigned)
                            {
                                auto const general = a.new_label();
                                auto const done = a.new_label();
                                a.alu_ri(7, aW, divisor, -1);
                                a.jcc(NE, general);
                                if (aRemainder)
                                    a.alu_rr(6, false, RDX, RDX);
                                else
                                {
                                    a.f7(3, aW, RAX); // neg
                                    a.jcc(O, iTrapLabels[TrapIntegerOverflow]);
                                }
                                a.jmp(done);
                                a.bind(general);
                                a.cqo(aW);
                                a.f7(7, aW, divisor);
                                a.bind(done);
                            }
                            else
                            {
                                a.alu_rr(6, false, RDX, RDX);
                                a.f7(6, aW, divisor);
                            }
                            if (rhs.k == kind::Reg)
                                release_reg(false, rhs.reg);
                            int const result = aRemainder ? RDX : RAX;
                            release_reg(false, aRemainder ? RAX : RDX);
                            if (!aW)
                                a.mov_rr(false, result, result);
                            push_reg(aW ? I64 : I32, result);
                        }
                        void float_binary(bool aDouble, std::uint8_t aOp)
                        {
                            materialize_cond();
                            auto rhs = pop();
                            auto lhs = pop();
                            auto const dst = to_xmm(lhs);
                            auto const src = xoperand_of(rhs);
                            if (src.k == operand::Reg)
                                a.sse_rr(aDouble ? 0xF2 : 0xF3, aOp, false, dst, src.reg);
                            else if (src.k == operand::Rip)
                                a.sse_rip(aDouble ? 0xF2 : 0xF3, aOp, dst, src.label);
                            else
                                a.sse_rm(aDouble ? 0xF2 : 0xF3, aOp, false, dst, src.base, src.disp);
                            release(rhs);
                            release_temps();
                            push_reg(aDouble ? F64 : F32, dst);
                        }
                        void float_compare(bool aDouble, int aRelation) // 0 eq, 1 ne, 2 lt, 3 gt, 4 le, 5 ge
                        {
                            materialize_cond();
                            auto rhs = pop();
                            auto lhs = pop();
                            bool const swapOperands = (aRelation == 2 || aRelation == 4);
                            auto const& first = swapOperands ? rhs : lhs;
                            auto const& second = swapOperands ? lhs : rhs;
                            int firstReg;
                            auto const fo = xoperand_of(first);
                            if (fo.k == operand::Reg)
                                firstReg = fo.reg;
                            else
                            {
                                firstReg = temp(true);
                                load_xmm(first, firstReg);
                            }
                            auto const so = xoperand_of(second);
                            if (so.k == operand::Reg)
                                a.ucomis(aDouble, firstReg, so.reg);
                            else if (so.k == operand::Rip)
                                a.sse_rip(aDouble ? 0x66 : -1, 0x2E, firstReg, so.label);
                            else
                                a.ucomis_m(aDouble, firstReg, so.base, so.disp);
                            release(lhs);
                            release(rhs);
                            release_temps();
                            if (aRelation >= 2)
                                push_cond(aRelation <= 3 ? A : AE);
                            else
                                push_cond(aRelation == 0 ? E : NE, true);
                        }
                        void float_sign(bool aDouble, int aDigit) // abs (6): and with ~sign, neg (7): xor with sign
                        {
                            materialize_cond();
                            auto v = pop();
                            auto const x = to_xmm(v);
                            auto const m = alloc(true);
                            std::uint64_t const sign = aDouble ? 0x8000000000000000ull : 0x80000000ull;
                            a.sse_rip(aDouble ? 0xF2 : 0xF3, 0x10, m, a.constant(aDigit == 6 ? (aDouble ? ~sign : (~sign & 0xFFFFFFFFull)) : sign));
                            a.sse_rr(aDouble ? 0x66 : -1, aDigit == 6 ? 0x54 : 0x57, false, x, m);
                            release_reg(true, m);
                            push_reg(aDouble ? F64 : F32, x);
                        }
                        void float_copysign(bool aDouble)
                        {
                            materialize_cond();
                            auto rhs = pop();
                            auto lhs = pop();
                            auto const l = to_xmm(lhs);
                            auto const r = to_xmm(rhs);
                            auto const m = alloc(true);
                            std::uint64_t const sign = aDouble ? 0x8000000000000000ull : 0x80000000ull;
                            int const prefix = aDouble ? 0x66 : -1;
                            // l = (l & ~sign) | (r & sign)
                            a.sse_rip(aDouble ? 0xF2 : 0xF3, 0x10, m, a.constant(aDouble ? ~sign : (~sign & 0xFFFFFFFFull)));
                            a.sse_rr(prefix, 0x54, false, l, m);
                            a.sse_rip(aDouble ? 0xF2 : 0xF3, 0x10, m, a.constant(sign));
                            a.sse_rr(prefix, 0x54, false, r, m);
                            a.sse_rr(prefix, 0x56, false, l, r);
                            release_reg(true, m);
                            release_reg(true, r);
                            push_reg(aDouble ? F64 : F32, l);
                        }
                        void float_unary_sse(bool aDouble, std::uint8_t aOp) // sqrt
                        {
                            materialize_cond();
                            auto v = pop();
                            auto const x = to_xmm(v);
                            a.sse_rr(aDouble ? 0xF2 : 0xF3, aOp, false, x, x);
                            push_reg(aDouble ? F64 : F32, x);
                        }
                        void float_round(bool aDouble, std::uint8_t aMode, void const* aFallback)
                        {
                            materialize_cond();
                            if (iSse41)
                            {
                                auto v = pop();
                                auto const x = to_xmm(v);
                                a.round(aDouble, x, x, aMode);
                                push_reg(aDouble ? F64 : F32, x);
                            }
                            else
                                helper(aFallback, 1u, aDouble ? F64 : F32, aDouble ? F64 : F32, false);
                        }
                        // call a native helper with aCount operands of type aOperandType from the stack (and optionally the context)
                        void helper(void const* aFunction, std::size_t aCount, std::uint8_t aOperandType, std::uint8_t aResultType, bool aWithContext)
                        {
                            materialize_cond();
                            std::vector<native_argument> arguments;
                            std::vector<entry> operands(aCount);
                            for (std::size_t i = aCount; i-- > 0u;)
                                operands[i] = pop();
                            for (std::size_t i = 0u; i < aCount; ++i)
                            {
                                store_entry(operands[i], RSP, ShadowSpace + 8 * static_cast<std::int32_t>(i));
                                release(operands[i]);
                                arguments.push_back(native_argument{ is_float(aOperandType), aOperandType == F64 });
                            }
                            if (aWithContext)
                            {
                                stage_context(aCount);
                                arguments.push_back(native_argument{ false, false });
                            }
                            native_call(aFunction, arguments, aWithContext);
                            if (is_float(aResultType))
                            {
                                owner(true, 0) = Locked;
                                push_reg(aResultType, 0);
                            }
                            else
                            {
                                owner(false, RAX) = Locked;
                                if (!is_wide(aResultType))
                                    a.mov_rr(false, RAX, RAX);
                                push_reg(aResultType, RAX);
                            }
                        }
                        void convert_to_float(bool aDouble, bool aWideSource, bool aSigned)
                        {
                            materialize_cond();
                            if (aWideSource && !aSigned)
                            {
                                helper(aDouble ? reinterpret_cast<void const*>(&f64_from_u64) : reinterpret_cast<void const*>(&f32_from_u64), 1u, I64, aDouble ? F64 : F32, false);
                                return;
                            }
                            auto v = pop();
                            auto const g = to_gpr(v);
                            bool w = aWideSource;
                            if (!aWideSource && !aSigned)
                            {
                                a.mov_rr(false, g, g); // zero extend
                                w = true;
                            }
                            auto const x = alloc(true);
                            a.xorpd(x, x);
                            a.sse_rr(aDouble ? 0xF2 : 0xF3, 0x2A, w, x, g);
                            release_reg(false, g);
                            push_reg(aDouble ? F64 : F32, x);
                        }
                        void call(std::uint32_t aFunction)
                        {
                            materialize_cond();
                            if (aFunction >= iModule.functions.size())
                                throw unsupported{ "invalid function index" };
                            auto const& callee = iModule.functions[aFunction];
                            auto const& parameters = *callee.parameters;
                            auto const& results = *callee.results;
                            if (results.size() > MaxResults)
                                throw unsupported{ "too many results" };
                            if (callee.checked)
                            {
                                checked_arithmetic(*callee.checked);
                                return;
                            }
                            std::int32_t const base = callee.imported ? ShadowSpace : 0;
                            std::size_t const n = parameters.size();
                            if (iStack.size() < iControl.back().base + n)
                                throw unsupported{ "operand stack underflow" };
                            for (std::size_t i = 0u; i < n; ++i)
                                store_entry(iStack[iStack.size() - n + i], RSP, base + 8 * static_cast<std::int32_t>(i));
                            for (std::size_t i = 0u; i < n; ++i)
                                release(pop());
                            spill_registers();
                            iOutgoing = std::max(iOutgoing, base + 8 * static_cast<std::int32_t>(std::max(n, results.size())));
                            if (!callee.imported)
                            {
                                save_pinned_gprs();
                                a.call(iFunctionLabels[aFunction]);
                                restore_pinned_gprs();
                                for (std::size_t i = 0u; i < results.size(); ++i)
                                {
                                    auto const t = results[i];
                                    if (i == 0u)
                                    {
                                        if (is_float(t))
                                        {
                                            owner(true, 0) = Locked;
                                            a.movq_xg(t == F64, 0, RAX);
                                            push_reg(t, 0);
                                        }
                                        else
                                        {
                                            owner(false, RAX) = Locked;
                                            push_reg(t, RAX);
                                        }
                                    }
                                    else
                                        push_loaded(t, R15, ContextResults + 8 * static_cast<std::int32_t>(i));
                                }
                            }
                            else
                            {
                                save_pinned_xmms();
                                save_pinned_gprs();
                                a.mov_rr(true, NativeIntegerArguments[0], R15);
                                a.mov_ri(NativeIntegerArguments[1], aFunction, false);
                                a.lea(NativeIntegerArguments[2], RSP, ShadowSpace);
                                a.mov_ri(RAX, reinterpret_cast<std::uint64_t>(&host_thunk), true);
                                a.call_r(RAX);
                                restore_pinned_xmms();
                                restore_pinned_gprs();
                                a.test_rr(false, RAX, RAX);
                                a.jcc(NE, iTrapLabels[TrapHostException]);
                                for (std::size_t i = 0u; i < results.size(); ++i)
                                    push_loaded(results[i], RSP, ShadowSpace + 8 * static_cast<std::int32_t>(i));
                            }
                        }
                        // checked integer arithmetic (see jit::checked_arithmetic): the operation, then a trap if it overflowed (OF, or CF 
                        // for unsigned addition and subtraction)
                        void checked_arithmetic(std::uint8_t aCode)
                        {
                            auto const operation = aCode & 3u;
                            bool const w = (aCode & 4u) != 0u;
                            bool const isUnsigned = (aCode & 8u) != 0u;
                            auto const type = w ? I64 : I32;
                            if (operation == 3u)
                            {
                                auto const v = pop();
                                auto const r = to_gpr(v);
                                a.f7(3, w, r); // neg (OF: the least integer)
                                a.jcc(O, iTrapLabels[TrapIntegerOverflow]);
                                push_reg(type, r);
                                return;
                            }
                            if (operation == 2u && isUnsigned)
                            {
                                // (mul: rdx:rax = rax * src; OF and CF if rdx (edx) is not zero)
                                auto rhs = pop();
                                auto lhs = pop();
                                evict(false, RAX);
                                evict(false, RDX);
                                owner(false, RDX) = Locked;
                                load_gpr(lhs, RAX);
                                if (!(lhs.k == kind::Reg && lhs.reg == RAX))
                                    release(lhs);
                                owner(false, RAX) = Locked;
                                auto const src = to_gpr(rhs, (1u << RAX) | (1u << RDX));
                                a.f7(4, w, src);
                                a.jcc(O, iTrapLabels[TrapIntegerOverflow]);
                                release_reg(false, src);
                                release_reg(false, RDX);
                                push_reg(type, RAX);
                                return;
                            }
                            if (operation == 2u)
                                int_multiply(w);
                            else
                                int_binary(operation == 0u ? 0 : 5, w, operation == 0u);
                            a.jcc(isUnsigned ? B : O, iTrapLabels[TrapIntegerOverflow]);
                        }
                        void push_loaded(std::uint8_t aType, int aBase, std::int32_t aDisp)
                        {
                            if (is_float(aType))
                            {
                                auto const x = alloc(true);
                                a.movf_load(aType == F64, x, aBase, aDisp);
                                push_reg(aType, x);
                            }
                            else
                            {
                                auto const r = alloc(false);
                                a.load(is_wide(aType), r, aBase, aDisp);
                                push_reg(aType, r);
                            }
                        }
                        void set_local(std::uint32_t aLocal, bool aTee)
                        {
                            if (aLocal >= iLocals.size())
                                throw unsupported{ "invalid local index" };
                            materialize_cond();
                            auto v = pop();
                            detach_local(aLocal);
                            auto const& l = iLocals[aLocal];
                            if (v.k == kind::Local && v.local == aLocal)
                            {
                                if (aTee)
                                    push(v);
                                return;
                            }
                            if (l.pinned)
                            {
                                if (is_float(l.type))
                                    load_xmm(v, l.reg);
                                else
                                    load_gpr(v, l.reg);
                            }
                            else
                                store_entry(v, RBP, l.disp);
                            if (aTee)
                            {
                                if (v.k == kind::Const || v.k == kind::Reg || v.k == kind::Slot)
                                {
                                    if (v.k == kind::Slot)
                                    {
                                        // (the slot belongs to this stack position)
                                    }
                                    push(v);
                                }
                                else
                                {
                                    entry e;
                                    e.k = kind::Local;
                                    e.type = l.type;
                                    e.local = aLocal;
                                    push(e);
                                }
                            }
                            else
                                release(v);
                        }
                        // local.get L; (i32|i64).const C; (i32|i64).(add|sub); local.(set|tee) L: one add/sub on the local's register 
                        // (or slot); aNext is advanced past the sequence
                        bool update_local(std::size_t aPosition, std::size_t& aNext)
                        {
                            auto const token = [&](std::size_t aIndex) { return aPosition + aIndex < iTokens.size() ? iTokens[aPosition + aIndex] : ~std::uint64_t{}; };
                            auto const local = token(1u);
                            auto const constant = token(2u);
                            auto const arithmetic = token(4u);
                            auto const store = token(5u);
                            if (local >= iLocals.size() || token(6u) != local || (store != 0x21u && store != 0x22u))
                                return false;
                            auto const& l = iLocals[static_cast<std::size_t>(local)];
                            bool const wide = (constant == 0x42u);
                            if (!(constant == 0x41u && l.type == I32 && (arithmetic == 0x6Au || arithmetic == 0x6Bu)) &&
                                !(wide && l.type == I64 && (arithmetic == 0x7Cu || arithmetic == 0x7Du)))
                                return false;
                            auto const value = static_cast<std::int64_t>(token(3u));
                            if (wide && (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()))
                                return false;
                            auto const immediate = static_cast<std::int32_t>(wide ? value : static_cast<std::int32_t>(static_cast<std::uint32_t>(value)));
                            int const op = (arithmetic == 0x6Au || arithmetic == 0x7Cu) ? 0 : 5; // (add or sub)
                            materialize_cond();
                            detach_local(static_cast<std::uint32_t>(local));
                            if (l.pinned)
                                a.alu_ri(op, wide, l.reg, immediate);
                            else
                                a.alu_mi(op, wide, RBP, l.disp, immediate);
                            if (store == 0x22u)
                            {
                                entry e;
                                e.k = kind::Local;
                                e.type = l.type;
                                e.local = static_cast<std::uint32_t>(local);
                                push(e);
                            }
                            aNext = aPosition + 7u;
                            return true;
                        }
                        // x & c (c a constant of at most 31 bits) whose result is only tested (against zero: wrapped and then tested by
                        // br_if, if or eqz; eqz; or compared with zero): test x, c (a condition); aNext is advanced past the tokens it 
                        // replaces
                        bool test_and(bool aW, std::size_t& aNext)
                        {
                            if (iStack.size() < iControl.back().base + 2u || iStack.back().k != kind::Const)
                                return false;
                            auto const bits = iStack.back().bits;
                            std::int64_t const c = aW ? static_cast<std::int64_t>(bits) : static_cast<std::int64_t>(static_cast<std::int32_t>(static_cast<std::uint32_t>(bits)));
                            if (c < 0 || c > 0x7FFFFFFF)
                                return false;
                            auto const token = [&](std::size_t aIndex) { return aNext + aIndex < iTokens.size() ? iTokens[aNext + aIndex] : ~std::uint64_t{}; };
                            auto const test = [](std::uint64_t aOp) { return aOp == 0x0Du || aOp == 0x04u || aOp == 0x45u; };
                            std::uint8_t cc = NE;
                            std::size_t skip = 0u;
                            if (aW && token(0u) == 0xA7u && test(token(1u)))
                                skip = 1u;
                            else if (token(0u) == (aW ? 0x50u : 0x45u))
                            {
                                cc = E;
                                skip = 1u;
                            }
                            else if (!aW && (token(0u) == 0x0Du || token(0u) == 0x04u))
                                skip = 0u;
                            else if (token(0u) == (aW ? 0x42u : 0x41u) && token(1u) == 0u && (token(2u) == (aW ? 0x51u : 0x46u) || token(2u) == (aW ? 0x52u : 0x47u)))
                            {
                                cc = token(2u) == (aW ? 0x51u : 0x46u) ? E : NE;
                                skip = 3u;
                            }
                            else
                                return false;
                            release(pop());
                            auto const value = pop();
                            auto const o = operand_of(value, false);
                            if (o.k == operand::Reg)
                                a.test_ri(o.reg, static_cast<std::int32_t>(c));
                            else
                                a.test_mi(o.base, o.disp, static_cast<std::int32_t>(c));
                            release(value);
                            release_temps();
                            push_cond(cc);
                            aNext += skip;
                            return true;
                        }
                        // a load or store: [address + offset, address + offset + width) must be within linear memory
                        void memory_access(std::uint8_t aOp, std::uint64_t aOffset)
                        {
                            struct access { std::uint8_t width; std::uint8_t type; std::uint8_t opcode0; std::uint8_t opcode1; bool w; int prefix; };
                            static access const sAccesses[] =
                            {
                                { 4, I32, 0x8B, 0, false, -1 }, { 8, I64, 0x8B, 0, true, -1 }, { 4, F32, 0, 0, false, -1 }, { 8, F64, 0, 0, false, -1 },
                                { 1, I32, 0x0F, 0xBE, false, -1 }, { 1, I32, 0x0F, 0xB6, false, -1 },
                                { 2, I32, 0x0F, 0xBF, false, -1 }, { 2, I32, 0x0F, 0xB7, false, -1 },
                                { 1, I64, 0x0F, 0xBE, true, -1 }, { 1, I64, 0x0F, 0xB6, false, -1 },
                                { 2, I64, 0x0F, 0xBF, true, -1 }, { 2, I64, 0x0F, 0xB7, false, -1 },
                                { 4, I64, 0x63, 0, true, -1 }, { 4, I64, 0x8B, 0, false, -1 },
                                { 4, I32, 0x89, 0, false, -1 }, { 8, I64, 0x89, 0, true, -1 }, { 4, F32, 0, 0, false, -1 }, { 8, F64, 0, 0, false, -1 },
                                { 1, I32, 0x88, 0, false, -1 }, { 2, I32, 0x89, 0, false, 0x66 },
                                { 1, I64, 0x88, 0, false, -1 }, { 2, I64, 0x89, 0, false, 0x66 }, { 4, I64, 0x89, 0, false, -1 }
                            };
                            auto const& x = sAccesses[aOp - 0x28u];
                            bool const store = (aOp >= 0x36u);
                            if (aOffset + x.width > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
                                throw unsupported{ "memory offset too large" };
                            auto const disp = static_cast<std::int32_t>(aOffset);
                            materialize_cond();
                            // the value stored (byte stores use AL, CL, DL, BL or R8B-R15B)
                            std::uint32_t constexpr NotByteRegisters = (1u << RSP) | (1u << RBP) | (1u << RSI) | (1u << RDI);
                            std::optional<entry> value;
                            int v = -1;
                            if (store)
                            {
                                value = pop();
                                v = is_float(x.type) ? to_xmm(*value) : to_gpr(*value, x.width == 1u ? NotByteRegisters : 0u);
                            }
                            std::uint32_t const keep = (!store || is_float(x.type)) ? 0u : (1u << v);
                            auto const address = pop();
                            auto const g = to_gpr(address, keep);
                            a.mov_rr(false, g, g); // (zero extend)
                            auto const m = alloc(false, keep | (1u << g));
                            auto const e = alloc(false, keep | (1u << g) | (1u << m));
                            a.load(true, m, R15, ContextMemory);
                            a.lea(e, g, disp + static_cast<std::int32_t>(x.width));
                            a.alu_rm(7, true, e, m, MemorySize); // cmp
                            a.jcc(A, iTrapLabels[TrapOutOfBounds]);
                            release_reg(false, e);
                            a.load(true, m, m, MemoryBase);
                            a.alu_rr(0, true, m, g); // add
                            release_reg(false, g);
                            if (store)
                            {
                                if (is_float(x.type))
                                {
                                    a.movf_store(x.type == F64, m, disp, v);
                                    if (value->k == kind::Reg)
                                        release(*value);
                                    else
                                        release_reg(true, v);
                                }
                                else
                                {
                                    a.op_rm({ x.opcode0 }, x.w, v, m, disp, x.prefix);
                                    release_reg(false, v);
                                }
                                release_reg(false, m);
                                return;
                            }
                            if (is_float(x.type))
                            {
                                auto const f = alloc(true);
                                a.movf_load(x.type == F64, f, m, disp);
                                release_reg(false, m);
                                push_reg(x.type, f);
                            }
                            else
                            {
                                if (x.opcode1 != 0u)
                                    a.op_rm({ x.opcode0, x.opcode1 }, x.w, m, m, disp, x.prefix);
                                else
                                    a.op_rm({ x.opcode0 }, x.w, m, m, disp, x.prefix);
                                push_reg(x.type, m);
                            }
                        }
                        void get_global(std::uint32_t aIndex)
                        {
                            materialize_cond();
                            if (aIndex >= iModule.globals->size() || aIndex >= iModule.globalTypes->size())
                                throw unsupported{ "invalid global index" };
                            auto const type = (*iModule.globalTypes)[aIndex];
                            auto const t = alloc(false);
                            a.load(true, t, R15, ContextGlobals);
                            if (is_float(type))
                            {
                                auto const x = alloc(true);
                                a.movf_load(type == F64, x, t, static_cast<std::int32_t>(aIndex * 8u));
                                release_reg(false, t);
                                push_reg(type, x);
                            }
                            else
                            {
                                a.load(is_wide(type), t, t, static_cast<std::int32_t>(aIndex * 8u));
                                push_reg(type, t);
                            }
                        }
                        void set_global(std::uint32_t aIndex)
                        {
                            materialize_cond();
                            if (aIndex >= iModule.globals->size())
                                throw unsupported{ "invalid global index" };
                            auto v = pop();
                            auto const t = alloc(false);
                            a.load(true, t, R15, ContextGlobals);
                            store_entry(v, t, static_cast<std::int32_t>(aIndex * 8u));
                            release_reg(false, t);
                            release(v);
                        }
                        void select()
                        {
                            auto condition = pop();
                            if (iStack.size() < iControl.back().base + 2u)
                                throw unsupported{ "operand stack underflow" };
                            auto b = pop();
                            auto av = pop();
                            if (b.k == kind::Cond || av.k == kind::Cond)
                                throw unsupported{ "select of a condition" };
                            if (is_float(av.type))
                            {
                                auto const dst = to_xmm(av);
                                auto const bo = xoperand_of(b);
                                auto const keep = a.new_label();
                                if (condition.k == kind::Cond)
                                    jump_on(condition, true, keep);
                                else
                                {
                                    test_value(condition);
                                    a.jcc(NE, keep);
                                }
                                if (bo.k == operand::Reg)
                                    a.movapd(dst, bo.reg);
                                else if (bo.k == operand::Rip)
                                    a.sse_rip(av.type == F64 ? 0xF2 : 0xF3, 0x10, dst, bo.label);
                                else
                                    a.movf_load(av.type == F64, dst, bo.base, bo.disp);
                                a.bind(keep);
                                release(b);
                                release_temps();
                                push_reg(av.type, dst);
                                return;
                            }
                            bool const w = is_wide(av.type);
                            std::uint32_t avoid = 0u;
                            if (condition.k == kind::Reg)
                                avoid |= 1u << condition.reg;
                            auto const dst = to_gpr(av, avoid);
                            auto bo = operand_of(b, false);
                            std::uint8_t takeB;
                            if (condition.k == kind::Cond && !condition.parity)
                                takeB = invert(condition.cc);
                            else if (condition.k == kind::Cond)
                            {
                                auto const c = temp(false, 1u << dst);
                                cond_to_reg(condition, c);
                                a.test_rr(false, c, c);
                                takeB = E;
                            }
                            else
                            {
                                auto const co = operand_of(condition, false);
                                if (co.k == operand::Reg)
                                    a.test_rr(is_wide(condition.type), co.reg, co.reg);
                                else
                                    a.alu_mi(7, is_wide(condition.type), co.base, co.disp, 0);
                                takeB = E;
                            }
                            if (bo.k == operand::Reg)
                                a.cmov_rr(takeB, w, dst, bo.reg);
                            else
                                a.cmov_rm(takeB, w, dst, bo.base, bo.disp);
                            release(b);
                            release(condition);
                            release_temps();
                            push_reg(av.type, dst);
                        }
                    private:
                        void body(std::size_t aBegin, std::size_t aEnd)
                        {
                            std::size_t skipDepth = 0u;
                            for (std::size_t p = aBegin; p < aEnd;)
                            {
                                auto const op = static_cast<std::uint8_t>(iTokens[p]);
                                auto const opPosition = p;
                                auto const next = p + 1u + immediate_count(op, iTokens, p);
                                auto immediate = [&](std::size_t aIndex) { return iTokens[opPosition + 1u + aIndex]; };
                                p = next;
                                if (!iReachable)
                                {
                                    // skip unreachable code up to the end (or else) of the current block
                                    if (op == 0x02 || op == 0x03 || op == 0x04)
                                    {
                                        ++skipDepth;
                                        continue;
                                    }
                                    if (op == 0x0B || op == 0x05)
                                    {
                                        if (skipDepth > 0u)
                                        {
                                            if (op == 0x0B)
                                                --skipDepth;
                                            continue;
                                        }
                                    }
                                    else
                                        continue;
                                }
                                if (op == 0x20u && update_local(opPosition, p))
                                    continue;
                                if ((op == 0x71u || op == 0x83u) && test_and(op == 0x83u, p))
                                    continue;
                                if (op == 0x0Bu && condition_end(p))
                                {
                                    if (iControl.empty())
                                        return;
                                    continue;
                                }
                                instruction(op, opPosition, immediate);
                                if (iControl.empty())
                                    return;
                            }
                            if (!iControl.empty())
                                throw unsupported{ "unterminated function" };
                        }
                        template <typename Immediate>
                        void instruction(std::uint8_t aOp, std::size_t aPosition, Immediate const& aImmediate)
                        {
                            auto block_result = [&](std::uint64_t aBlockType) -> std::optional<std::uint8_t>
                            {
                                if (aBlockType == 0x40u)
                                    return std::nullopt;
                                auto const t = static_cast<std::uint8_t>(aBlockType);
                                if (t != I32 && t != I64 && t != F32 && t != F64)
                                    throw unsupported{ "block type" };
                                return t;
                            };
                            auto constant = [&](std::uint8_t aType, std::uint64_t aBits)
                            {
                                materialize_cond();
                                entry e;
                                e.k = kind::Const;
                                e.type = aType;
                                e.bits = aBits;
                                push(e);
                            };
                            switch (aOp)
                            {
                            case 0x00:
                                a.jmp(iTrapLabels[TrapUnreachable]);
                                unwind(iControl.back().base);
                                iReachable = false;
                                break;
                            case 0x01:
                                break;
                            case 0x02:
                            case 0x03:
                                {
                                    flush(aPosition + 2u);
                                    control c;
                                    c.kind = aOp == 0x02 ? control::Block : control::Loop;
                                    c.result = block_result(aImmediate(0));
                                    c.base = iStack.size();
                                    c.label = a.new_label();
                                    c.bodyBegin = aPosition + 2u;
                                    if (aOp == 0x02 && c.result == I32 && iStack.empty())
                                        c.condition = condition_block(aPosition + 2u);
                                    if (aOp == 0x03)
                                    {
                                        a.bind(c.label);
                                        if (!c.result)
                                            find_loop_head(c, aPosition);
                                    }
                                    iControl.push_back(c);
                                }
                                break;
                            case 0x04:
                                {
                                    auto const result = block_result(aImmediate(0));
                                    auto condition = pop();
                                    flush(aPosition + 2u);
                                    control c;
                                    c.kind = control::If;
                                    c.result = result;
                                    c.base = iStack.size();
                                    c.label = a.new_label();
                                    c.elseLabel = a.new_label();
                                    jump_unless(condition, c.elseLabel, true);
                                    iControl.push_back(c);
                                }
                                break;
                            case 0x05:
                                {
                                    auto& c = iControl.back();
                                    if (c.kind != control::If)
                                        throw unsupported{ "else without if" };
                                    if (iReachable)
                                    {
                                        transfer_block_result(c, true);
                                        a.jmp(c.label);
                                        c.branched = true;
                                    }
                                    unwind(c.base);
                                    a.bind(c.elseLabel);
                                    c.hasElse = true;
                                    iReachable = true;
                                }
                                break;
                            case 0x0B:
                                {
                                    auto c = iControl.back();
                                    if (c.kind == control::Function)
                                    {
                                        if (iReachable)
                                            transfer_results();
                                        iControl.pop_back();
                                        break;
                                    }
                                    if (iReachable)
                                        transfer_block_result(c, true);
                                    unwind(c.base);
                                    iControl.pop_back();
                                    if (c.coldTail)
                                    {
                                        // (the end of a cold tail: back to the block's end)
                                        if (iReachable)
                                            a.jmp(c.label);
                                        a.end_cold();
                                    }
                                    else if (c.kind == control::Block && !iReachable && !a.in_cold() && !iControl.empty() && 
                                        iControl.back().kind == control::Block && iControl.back().bodyBegin == c.bodyBegin - 2u &&
                                        iLastBranch && iLastBranch->first + 2u == aPosition && iLastBranch->second == 1u)
                                    {
                                        // (the start of a cold tail)
                                        iControl.back().coldTail = true;
                                        a.begin_cold();
                                    }
                                    if (c.kind == control::If && !c.hasElse)
                                    {
                                        a.bind(c.elseLabel);
                                        c.branched = true;
                                    }
                                    if (c.kind != control::Loop)
                                        a.bind(c.label);
                                    iReachable = iReachable || (c.kind != control::Loop && c.branched);
                                    if (iReachable && c.result)
                                    {
                                        if (is_float(*c.result))
                                        {
                                            owner(true, 0) = Locked;
                                            push_reg(*c.result, 0);
                                        }
                                        else
                                        {
                                            owner(false, RAX) = Locked;
                                            push_reg(*c.result, RAX);
                                        }
                                    }
                                }
                                break;
                            case 0x0C:
                                iLastBranch = std::make_pair(aPosition, static_cast<std::size_t>(aImmediate(0)));
                                if (auto const depth = static_cast<std::size_t>(aImmediate(0)); depth >= iControl.size() || !iControl[iControl.size() - 1u - depth].condition)
                                    materialize_cond();
                                if (rotate_loop(static_cast<std::size_t>(aImmediate(0))))
                                {
                                    unwind(iControl.back().base);
                                    iReachable = false;
                                    break;
                                }
                                branch(static_cast<std::size_t>(aImmediate(0)));
                                unwind(iControl.back().base);
                                iReachable = false;
                                break;
                            case 0x0D:
                                conditional_branch(static_cast<std::size_t>(aImmediate(0)));
                                if (auto& c = iControl.back(); c.kind == control::Loop && c.headEnd == aPosition && !c.bodyLabel)
                                {
                                    c.bodyLabel = a.new_label();
                                    a.bind(*c.bodyLabel);
                                }
                                break;
                            case 0x0F:
                                materialize_cond();
                                transfer_results();
                                a.jmp(iEpilogue);
                                unwind(iControl.back().base);
                                iReachable = false;
                                break;
                            case 0x10:
                                call(static_cast<std::uint32_t>(aImmediate(0)));
                                break;
                            case 0x1A:
                                release(pop());
                                break;
                            case 0x1B:
                                select();
                                break;
                            case 0x20:
                                {
                                    materialize_cond();
                                    auto const l = static_cast<std::uint32_t>(aImmediate(0));
                                    if (l >= iLocals.size())
                                        throw unsupported{ "invalid local index" };
                                    entry e;
                                    e.k = kind::Local;
                                    e.type = iLocals[l].type;
                                    e.local = l;
                                    push(e);
                                }
                                break;
                            case 0x21:
                                set_local(static_cast<std::uint32_t>(aImmediate(0)), false);
                                break;
                            case 0x22:
                                set_local(static_cast<std::uint32_t>(aImmediate(0)), true);
                                break;
                            case 0x23:
                                get_global(static_cast<std::uint32_t>(aImmediate(0)));
                                break;
                            case 0x24:
                                set_global(static_cast<std::uint32_t>(aImmediate(0)));
                                break;
                            case 0x41:
                                constant(I32, static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int64_t>(aImmediate(0)))));
                                break;
                            case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
                            case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35:
                            case 0x36: case 0x37: case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E:
                                memory_access(static_cast<std::uint8_t>(aOp), aImmediate(1));
                                break;
                            case 0x42:
                                constant(I64, aImmediate(0));
                                break;
                            case 0x43:
                                constant(F32, aImmediate(0) & 0xFFFFFFFFull);
                                break;
                            case 0x44:
                                constant(F64, aImmediate(0));
                                break;
                            // i32 comparison
                            case 0x45: eqz(false); break;
                            case 0x46: int_compare(false, E); break;
                            case 0x47: int_compare(false, NE); break;
                            case 0x48: int_compare(false, L); break;
                            case 0x49: int_compare(false, B); break;
                            case 0x4A: int_compare(false, G); break;
                            case 0x4B: int_compare(false, A); break;
                            case 0x4C: int_compare(false, LE); break;
                            case 0x4D: int_compare(false, BE); break;
                            case 0x4E: int_compare(false, GE); break;
                            case 0x4F: int_compare(false, AE); break;
                            // i64 comparison
                            case 0x50: eqz(true); break;
                            case 0x51: int_compare(true, E); break;
                            case 0x52: int_compare(true, NE); break;
                            case 0x53: int_compare(true, L); break;
                            case 0x54: int_compare(true, B); break;
                            case 0x55: int_compare(true, G); break;
                            case 0x56: int_compare(true, A); break;
                            case 0x57: int_compare(true, LE); break;
                            case 0x58: int_compare(true, BE); break;
                            case 0x59: int_compare(true, GE); break;
                            case 0x5A: int_compare(true, AE); break;
                            // float comparison
                            case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F: case 0x60:
                                float_compare(false, aOp - 0x5B);
                                break;
                            case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66:
                                float_compare(true, aOp - 0x61);
                                break;
                            // i32 arithmetic
                            case 0x6A: int_binary(0, false, true); break;
                            case 0x6B: int_binary(5, false, false); break;
                            case 0x6C: int_multiply(false); break;
                            case 0x6D: divide(false, true, false); break;
                            case 0x6E: divide(false, false, false); break;
                            case 0x6F: divide(false, true, true); break;
                            case 0x70: divide(false, false, true); break;
                            case 0x71: int_binary(4, false, true); break;
                            case 0x72: int_binary(1, false, true); break;
                            case 0x73: int_binary(6, false, true); break;
                            case 0x74: shift(4, false); break;
                            case 0x75: shift(7, false); break;
                            case 0x76: shift(5, false); break;
                            case 0x67: helper(reinterpret_cast<void const*>(&i32_clz), 1u, I32, I32, false); break;
                            case 0x68: helper(reinterpret_cast<void const*>(&i32_ctz), 1u, I32, I32, false); break;
                            case 0x69: helper(reinterpret_cast<void const*>(&i32_popcnt), 1u, I32, I32, false); break;
                            case 0x77: helper(reinterpret_cast<void const*>(&i32_rotl), 2u, I32, I32, false); break;
                            case 0x78: helper(reinterpret_cast<void const*>(&i32_rotr), 2u, I32, I32, false); break;
                            // i64 arithmetic
                            case 0x7C: int_binary(0, true, true); break;
                            case 0x7D: int_binary(5, true, false); break;
                            case 0x7E: int_multiply(true); break;
                            case 0x7F: divide(true, true, false); break;
                            case 0x80: divide(true, false, false); break;
                            case 0x81: divide(true, true, true); break;
                            case 0x82: divide(true, false, true); break;
                            case 0x83: int_binary(4, true, true); break;
                            case 0x84: int_binary(1, true, true); break;
                            case 0x85: int_binary(6, true, true); break;
                            case 0x86: shift(4, true); break;
                            case 0x87: shift(7, true); break;
                            case 0x88: shift(5, true); break;
                            case 0x79: helper(reinterpret_cast<void const*>(&i64_clz), 1u, I64, I64, false); break;
                            case 0x7A: helper(reinterpret_cast<void const*>(&i64_ctz), 1u, I64, I64, false); break;
                            case 0x7B: helper(reinterpret_cast<void const*>(&i64_popcnt), 1u, I64, I64, false); break;
                            case 0x89: helper(reinterpret_cast<void const*>(&i64_rotl), 2u, I64, I64, false); break;
                            case 0x8A: helper(reinterpret_cast<void const*>(&i64_rotr), 2u, I64, I64, false); break;
                            // f32 arithmetic
                            case 0x8B: float_sign(false, 6); break;
                            case 0x8C: float_sign(false, 7); break;
                            case 0x8D: float_round(false, 2, reinterpret_cast<void const*>(&f32_ceil)); break;
                            case 0x8E: float_round(false, 1, reinterpret_cast<void const*>(&f32_floor)); break;
                            case 0x8F: float_round(false, 3, reinterpret_cast<void const*>(&f32_trunc)); break;
                            case 0x90: float_round(false, 0, reinterpret_cast<void const*>(&f32_nearest)); break;
                            case 0x91: float_unary_sse(false, 0x51); break;
                            case 0x92: float_binary(false, 0x58); break;
                            case 0x93: float_binary(false, 0x5C); break;
                            case 0x94: float_binary(false, 0x59); break;
                            case 0x95: float_binary(false, 0x5E); break;
                            case 0x96: helper(reinterpret_cast<void const*>(&f32_min), 2u, F32, F32, false); break;
                            case 0x97: helper(reinterpret_cast<void const*>(&f32_max), 2u, F32, F32, false); break;
                            case 0x98: float_copysign(false); break;
                            // f64 arithmetic
                            case 0x99: float_sign(true, 6); break;
                            case 0x9A: float_sign(true, 7); break;
                            case 0x9B: float_round(true, 2, reinterpret_cast<void const*>(&f64_ceil)); break;
                            case 0x9C: float_round(true, 1, reinterpret_cast<void const*>(&f64_floor)); break;
                            case 0x9D: float_round(true, 3, reinterpret_cast<void const*>(&f64_trunc)); break;
                            case 0x9E: float_round(true, 0, reinterpret_cast<void const*>(&f64_nearest)); break;
                            case 0x9F: float_unary_sse(true, 0x51); break;
                            case 0xA0: float_binary(true, 0x58); break;
                            case 0xA1: float_binary(true, 0x5C); break;
                            case 0xA2: float_binary(true, 0x59); break;
                            case 0xA3: float_binary(true, 0x5E); break;
                            case 0xA4: helper(reinterpret_cast<void const*>(&f64_min), 2u, F64, F64, false); break;
                            case 0xA5: helper(reinterpret_cast<void const*>(&f64_max), 2u, F64, F64, false); break;
                            case 0xA6: float_copysign(true); break;
                            // conversions
                            case 0xA7: // i32.wrap_i64
                                {
                                    materialize_cond();
                                    auto v = pop();
                                    auto const r = to_gpr(v);
                                    a.mov_rr(false, r, r);
                                    push_reg(I32, r);
                                }
                                break;
                            case 0xA8: helper(reinterpret_cast<void const*>(&trunc_f32_i32), 1u, F32, I32, true); break;
                            case 0xA9: helper(reinterpret_cast<void const*>(&trunc_f32_u32), 1u, F32, I32, true); break;
                            case 0xAA: helper(reinterpret_cast<void const*>(&trunc_f64_i32), 1u, F64, I32, true); break;
                            case 0xAB: helper(reinterpret_cast<void const*>(&trunc_f64_u32), 1u, F64, I32, true); break;
                            case 0xAC: // i64.extend_i32_s
                                {
                                    materialize_cond();
                                    auto v = pop();
                                    auto const r = to_gpr(v);
                                    a.movsxd(r, r);
                                    push_reg(I64, r);
                                }
                                break;
                            case 0xAD: // i64.extend_i32_u
                                {
                                    materialize_cond();
                                    auto v = pop();
                                    auto const r = to_gpr(v);
                                    a.mov_rr(false, r, r);
                                    push_reg(I64, r);
                                }
                                break;
                            case 0xAE: helper(reinterpret_cast<void const*>(&trunc_f32_i64), 1u, F32, I64, true); break;
                            case 0xAF: helper(reinterpret_cast<void const*>(&trunc_f32_u64), 1u, F32, I64, true); break;
                            case 0xB0: helper(reinterpret_cast<void const*>(&trunc_f64_i64), 1u, F64, I64, true); break;
                            case 0xB1: helper(reinterpret_cast<void const*>(&trunc_f64_u64), 1u, F64, I64, true); break;
                            case 0xB2: convert_to_float(false, false, true); break;
                            case 0xB3: convert_to_float(false, false, false); break;
                            case 0xB4: convert_to_float(false, true, true); break;
                            case 0xB5: convert_to_float(false, true, false); break;
                            case 0xB6: // f32.demote_f64
                                {
                                    materialize_cond();
                                    auto v = pop();
                                    auto const x = to_xmm(v);
                                    a.sse_rr(0xF2, 0x5A, false, x, x);
                                    push_reg(F32, x);
                                }
                                break;
                            case 0xB7: convert_to_float(true, false, true); break;
                            case 0xB8: convert_to_float(true, false, false); break;
                            case 0xB9: convert_to_float(true, true, true); break;
                            case 0xBA: convert_to_float(true, true, false); break;
                            case 0xBB: // f64.promote_f32
                                {
                                    materialize_cond();
                                    auto v = pop();
                                    auto const x = to_xmm(v);
                                    a.sse_rr(0xF3, 0x5A, false, x, x);
                                    push_reg(F64, x);
                                }
                                break;
                            case 0xBC: // i32.reinterpret_f32
                            case 0xBD: // i64.reinterpret_f64
                                {
                                    materialize_cond();
                                    auto v = pop();
                                    auto const x = to_xmm(v);
                                    auto const g = alloc(false);
                                    a.movq_gx(aOp == 0xBD, g, x);
                                    release_reg(true, x);
                                    push_reg(aOp == 0xBD ? I64 : I32, g);
                                }
                                break;
                            case 0xBE: // f32.reinterpret_i32
                            case 0xBF: // f64.reinterpret_i64
                                {
                                    materialize_cond();
                                    auto v = pop();
                                    auto const g = to_gpr(v);
                                    auto const x = alloc(true);
                                    a.movq_xg(aOp == 0xBF, x, g);
                                    release_reg(false, g);
                                    push_reg(aOp == 0xBF ? F64 : F32, x);
                                }
                                break;
                            case 0xC0: // i32.extend8_s
                            case 0xC1: // i32.extend16_s
                                {
                                    materialize_cond();
                                    auto v = pop();
                                    auto const r = to_gpr(v);
                                    if (aOp == 0xC0)
                                        a.movsx8(r, r);
                                    else
                                        a.movsx16(r, r);
                                    push_reg(I32, r);
                                }
                                break;
                            default:
                                throw unsupported{ "unsupported instruction" };
                            }
                            (void)aPosition;
                        }
                    private:
                        assembler a;
                        module_description const& iModule;
                        std::vector<std::uint64_t> const& iTokens;
                        bool const iSse41;
                        std::size_t iTrapLabels[TrapCount] = {};
                        std::size_t iTrapFromContext = 0u;
                        std::size_t iExit = 0u;
                        std::vector<std::size_t> iFunctionLabels;
                        bytes iCode;
                        // function state
                        function_description const* iFunction = nullptr;
                        std::vector<entry> iStack;
                        std::vector<control> iControl;
                        std::vector<local_info> iLocals;
                        std::vector<int> iUsedPinnedGprs;               ///< (those that native code preserves: saved by the function)
                        std::vector<int> iNativeVolatilePinnedGprs;     ///< (those that native code does not preserve: saved around calls)
                        std::vector<int> iUsedPinnedXmms;
                        int iGprOwner[16] = {};
                        int iXmmOwner[16] = {};
                        std::vector<std::pair<bool, int>> iTemps;
                        std::size_t iMaxDepth = 0u;
                        std::int32_t iSlotsBase = 0;
                        std::int32_t iOutgoing = 0;
                        std::size_t iEpilogue = 0u;
                        bool iReachable = true;
                        std::optional<std::pair<std::size_t, std::size_t>> iLastBranch; ///< (the last br: its position and depth)
                    };

                    void* allocate_executable(bytes const& aCode, std::size_t& aSize)
                    {
                        aSize = aCode.size();
#if defined(_WIN32)
                        void* memory = ::VirtualAlloc(nullptr, aSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                        if (memory == nullptr)
                            return nullptr;
                        std::memcpy(memory, aCode.data(), aCode.size());
                        DWORD old;
                        if (!::VirtualProtect(memory, aSize, PAGE_EXECUTE_READ, &old))
                        {
                            ::VirtualFree(memory, 0, MEM_RELEASE);
                            return nullptr;
                        }
                        ::FlushInstructionCache(::GetCurrentProcess(), memory, aSize);
                        return memory;
#else
                        void* memory = ::mmap(nullptr, aSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                        if (memory == MAP_FAILED)
                            return nullptr;
                        std::memcpy(memory, aCode.data(), aCode.size());
                        if (::mprotect(memory, aSize, PROT_READ | PROT_EXEC) != 0)
                        {
                            ::munmap(memory, aSize);
                            return nullptr;
                        }
                        return memory;
#endif
                    }

                    void free_executable(void* aMemory, std::size_t aSize)
                    {
#if defined(_WIN32)
                        (void)aSize;
                        ::VirtualFree(aMemory, 0, MEM_RELEASE);
#else
                        ::munmap(aMemory, aSize);
#endif
                    }

                    std::uint64_t stack_limit()
                    {
                        // generated code traps rather than overflow the native stack (headroom is left for host functions)
                        constexpr std::uint64_t Headroom = 256u * 1024u;
#if defined(_WIN32)
                        ULONG_PTR low = 0u;
                        ULONG_PTR high = 0u;
                        ::GetCurrentThreadStackLimits(&low, &high);
                        return static_cast<std::uint64_t>(low) + Headroom;
#elif defined(__linux__)
                        pthread_attr_t attributes;
                        if (::pthread_getattr_np(::pthread_self(), &attributes) == 0)
                        {
                            void* address = nullptr;
                            std::size_t size = 0u;
                            ::pthread_attr_getstack(&attributes, &address, &size);
                            ::pthread_attr_destroy(&attributes);
                            return reinterpret_cast<std::uint64_t>(address) + Headroom;
                        }
                        char probe = 0;
                        return reinterpret_cast<std::uint64_t>(&probe) - 512u * 1024u;
#else
                        char probe = 0;
                        return reinterpret_cast<std::uint64_t>(&probe) - 512u * 1024u;
#endif
                    }
                }

                compiled_module::~compiled_module()
                {
                    if (code != nullptr)
                        free_executable(code, codeSize);
                }

                namespace
                {
                    std::uint32_t run(compiled_module& aModule, context& aContext, void const* aFunction)
                    {
#if defined(_WIN32)
                        // generated code uses the native stack: on Windows (1MB thread stacks by default) it runs on a 
                        // fiber with a larger stack
                        constexpr std::size_t StackReserve = 64u * 1024u * 1024u;
                        struct call
                        {
                            compiled_module* module;
                            context* ctx;
                            void const* function;
                            void* caller;
                            std::uint32_t status;
                        } c{ &aModule, &aContext, aFunction, nullptr, NoTrap };
                        bool const converted = !::IsThreadAFiber();
                        c.caller = converted ? ::ConvertThreadToFiber(nullptr) : ::GetCurrentFiber();
                        void* const fiber = c.caller != nullptr ? ::CreateFiberEx(64u * 1024u, StackReserve, FIBER_FLAG_FLOAT_SWITCH,
                            [](void* aCall)
                            {
                                auto& c = *static_cast<call*>(aCall);
                                c.ctx->stackLimit = stack_limit();
                                c.status = c.module->trampoline(c.ctx, c.function);
                                ::SwitchToFiber(c.caller);
                            }, &c) : nullptr;
                        if (fiber != nullptr)
                        {
                            ::SwitchToFiber(fiber);
                            ::DeleteFiber(fiber);
                        }
                        else
                        {
                            aContext.stackLimit = stack_limit();
                            c.status = aModule.trampoline(&aContext, aFunction);
                        }
                        if (converted && c.caller != nullptr)
                            ::ConvertFiberToThread();
                        return c.status;
#else
                        aContext.stackLimit = stack_limit();
                        return aModule.trampoline(&aContext, aFunction);
#endif
                    }
                }

                bool available()
                {
                    return true;
                }

                compiled_module_ptr compile(module_description const& aModule, host_function aHost)
                {
                    compiled_module_ptr result{ new compiled_module{} };
                    result->description = aModule;
                    result->host = std::move(aHost);
                    try
                    {
                        code_generator generator{ result->description };
                        generator.generate(*result);
#if defined(NEOS_JIT_DUMP)
                        if (auto const path = std::getenv("NEOS_JIT_DUMP"))
                            if (auto file = std::fopen(path, "wb"))
                            {
                                std::fwrite(generator.code().data(), 1u, generator.code().size(), file);
                                std::fclose(file);
                            }
#endif
                        result->code = allocate_executable(generator.code(), result->codeSize);
                        if (result->code == nullptr)
                            return {};
                        result->trampoline = reinterpret_cast<std::uint32_t(*)(context*, void const*)>(result->code);
                    }
                    catch (unsupported const&)
                    {
                        return {};
                    }
                    return result;
                }

                std::vector<value> invoke(compiled_module& aModule, std::uint32_t aFunction, std::vector<value> const& aArguments)
                {
                    auto const& function = aModule.description.functions.at(aFunction);
                    if (function.imported)
                    {
                        std::vector<value> buffer{ aArguments };
                        buffer.resize(std::max(buffer.size(), function.results->size()));
                        aModule.host(aFunction, buffer.data());
                        buffer.resize(function.results->size());
                        return buffer;
                    }
                    if (aArguments.size() != function.parameters->size())
                        throw std::runtime_error("wrong number of arguments");
                    context c = {};
                    c.arguments = aArguments.data();
                    c.argumentCount = static_cast<std::uint32_t>(aArguments.size());
                    c.globals = aModule.description.globals->data();
                    c.linearMemory = aModule.description.linearMemory;
                    c.module = &aModule;
                    auto const status = run(aModule, c, static_cast<std::uint8_t const*>(aModule.code) + aModule.functionOffsets[aFunction]);
                    if (status != NoTrap)
                    {
                        if (status == TrapHostException && aModule.exception)
                        {
                            auto const exception = aModule.exception;
                            aModule.exception = nullptr;
                            std::rethrow_exception(exception);
                        }
                        throw std::runtime_error(trap_message(status));
                    }
                    std::vector<value> results{ c.results, c.results + function.results->size() };
                    for (std::size_t i = 0u; i < results.size(); ++i)
                    {
                        auto const t = (*function.results)[i];
                        if (t == I32 || t == F32)
                            results[i] &= 0xFFFFFFFFull;
                    }
                    return results;
                }

#else // not x86-64

                compiled_module::~compiled_module()
                {
                }

                bool available()
                {
                    return false;
                }

                compiled_module_ptr compile(module_description const&, host_function)
                {
                    return {};
                }

                std::vector<value> invoke(compiled_module&, std::uint32_t, std::vector<value> const&)
                {
                    throw std::logic_error("neos::jit: not available");
                }

#endif
            }
        }
    }
}
