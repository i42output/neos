/*
  jit_ssa.cpp

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

// The optimizing tier's SSA form (see jit_ssa.hpp). Construction follows the method of Braun, Buchwald, Hack, Leissa, Mallon and
// Zwinkau, "Simple and Efficient Construction of Static Single Assignment Form" (CC 2013): a local's value is looked up in the
// block that reads it, and its predecessors (phis where they merge), as the code is read; a block is sealed when its predecessors are
// known (structured control flow: at the end of its construct).

#include <neos/neos.hpp>
#include <algorithm>
#include <cmath>
#include <array>
#include <bit>
#include <optional>
#include <string>
#include <unordered_map>
#include <neos/runtime/plugin.h>
#include "jit_ssa.hpp"
#include "jit_optimizer.hpp"

namespace neos
{
    namespace bytecode
    {
        namespace vm
        {
            namespace jit
            {
                namespace ssa
                {
                    namespace
                    {
                        std::size_t immediates(std::uint8_t aOp, std::vector<std::uint64_t> const& aTokens, std::size_t aPosition)
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

                        // (a numeric instruction's operands (their number and type) and result type)
                        struct numeric
                        {
                            std::uint8_t operand = 0u;
                            std::uint8_t count = 0u;
                            std::uint8_t result = 0u;
                        };
                        numeric numeric_of(std::uint8_t aOp)
                        {
                            if (aOp == 0x45u) return { I32, 1u, I32 };
                            if (aOp >= 0x46u && aOp <= 0x4Fu) return { I32, 2u, I32 };
                            if (aOp == 0x50u) return { I64, 1u, I32 };
                            if (aOp >= 0x51u && aOp <= 0x5Au) return { I64, 2u, I32 };
                            if (aOp >= 0x5Bu && aOp <= 0x60u) return { F32, 2u, I32 };
                            if (aOp >= 0x61u && aOp <= 0x66u) return { F64, 2u, I32 };
                            if (aOp >= 0x67u && aOp <= 0x69u) return { I32, 1u, I32 };
                            if (aOp >= 0x6Au && aOp <= 0x78u) return { I32, 2u, I32 };
                            if (aOp >= 0x79u && aOp <= 0x7Bu) return { I64, 1u, I64 };
                            if (aOp >= 0x7Cu && aOp <= 0x8Au) return { I64, 2u, I64 };
                            if (aOp >= 0x8Bu && aOp <= 0x91u) return { F32, 1u, F32 };
                            if (aOp >= 0x92u && aOp <= 0x98u) return { F32, 2u, F32 };
                            if (aOp >= 0x99u && aOp <= 0x9Fu) return { F64, 1u, F64 };
                            if (aOp >= 0xA0u && aOp <= 0xA6u) return { F64, 2u, F64 };
                            switch (aOp)
                            {
                            case 0xA7: return { I64, 1u, I32 };
                            case 0xA8: case 0xA9: return { F32, 1u, I32 };
                            case 0xAA: case 0xAB: return { F64, 1u, I32 };
                            case 0xAC: case 0xAD: return { I32, 1u, I64 };
                            case 0xAE: case 0xAF: return { F32, 1u, I64 };
                            case 0xB0: case 0xB1: return { F64, 1u, I64 };
                            case 0xB2: case 0xB3: return { I32, 1u, F32 };
                            case 0xB4: case 0xB5: return { I64, 1u, F32 };
                            case 0xB6: return { F64, 1u, F32 };
                            case 0xB7: case 0xB8: return { I32, 1u, F64 };
                            case 0xB9: case 0xBA: return { I64, 1u, F64 };
                            case 0xBB: return { F32, 1u, F64 };
                            case 0xBC: return { F32, 1u, I32 };
                            case 0xBD: return { F64, 1u, I64 };
                            case 0xBE: return { I32, 1u, F32 };
                            case 0xBF: return { I64, 1u, F64 };
                            case 0xC0: case 0xC1: return { I32, 1u, I32 };
                            default: return {};
                            }
                        }

                        std::uint8_t load_type(std::uint8_t aOp)
                        {
                            static std::uint8_t const sTypes[] = { I32, I64, F32, F64, I32, I32, I32, I32, I64, I64, I64, I64, I64, I64 };
                            return sTypes[aOp - 0x28u];
                        }

                        std::uint8_t store_type(std::uint8_t aOp)
                        {
                            static std::uint8_t const sTypes[] = { I32, I64, F32, F64, I32, I32, I64, I64, I64 };
                            return sTypes[aOp - 0x36u];
                        }

                        class builder
                        {
                        private:
                            using variable = std::uint32_t;
                            struct frame
                            {
                                enum kind_t { Function, Block, Loop, If } kind = Block;
                                std::vector<std::uint8_t> results;
                                std::vector<variable> resultVariables;  ///< (the values that branches to its end take)
                                std::uint32_t end = 0u;                 ///< (the block after it (a loop's: its header, which branches go to))
                                std::uint32_t elseBlock = 0u;           ///< (an if's else (while its then arm is read))
                                bool hasElse = false;
                                std::size_t height = 0u;
                            };
                        public:
                            builder(module_description const& aModule, std::uint32_t aFunction, std::vector<std::uint64_t> const& aTokens,
                                std::vector<std::uint8_t> const& aLocals, std::vector<std::size_t> const& aOrigins, std::vector<optimizer::deopt> const& aDeopts) :
                                iModule{ aModule }, iTokens{ aTokens }, iOrigins{ aOrigins }, iDeopts{ aDeopts }
                            {
                                auto const& f = aModule.functions[aFunction];
                                iFunction.index = aFunction;
                                iFunction.parameters = *f.parameters;
                                iFunction.results = *f.results;
                                iVariableTypes = iFunction.parameters;
                                iVariableTypes.insert(iVariableTypes.end(), aLocals.begin(), aLocals.end());
                                iLocals = iVariableTypes.size();
                            }
                        public:
                            function build()
                            {
                                auto const entry = new_block();
                                seal(entry);
                                iCurrent = entry;
                                // (the parameters; the locals (0))
                                for (std::size_t i = 0u; i < iFunction.parameters.size(); ++i)
                                {
                                    auto const p = add(Param, iFunction.parameters[i], {}, i);
                                    write(static_cast<variable>(i), entry, p);
                                }
                                for (std::size_t i = iFunction.parameters.size(); i < iLocals; ++i)
                                    write(static_cast<variable>(i), entry, zero(iVariableTypes[i]));
                                frame fn;
                                fn.kind = frame::Function;
                                fn.results = iFunction.results;
                                for (auto t : fn.results)
                                    fn.resultVariables.push_back(new_variable(t));
                                fn.end = new_block();
                                iFrames.push_back(fn);
                                read_code(0u, iTokens.size());
                                // (the function's end: its results)
                                auto const end = iFrames.back();
                                iFrames.pop_back();
                                seal(end.end);
                                iCurrent = end.end;
                                if (!iFunction.blocks[end.end].predecessors.empty())
                                {
                                    std::vector<value> results;
                                    for (auto v : end.resultVariables)
                                        results.push_back(read(v, end.end));
                                    terminate(Return, results, {});
                                }
                                else
                                    terminate(Trap, {}, {}, 1u);
                                resolve();
                                return std::move(iFunction);
                            }
                        private:
                            // --- blocks and instructions
                            std::uint32_t new_block()
                            {
                                iFunction.blocks.emplace_back();
                                iSealed.push_back(false);
                                iIncomplete.emplace_back();
                                iPhis.emplace_back();
                                return static_cast<std::uint32_t>(iFunction.blocks.size() - 1u);
                            }
                            value add(std::uint16_t aOp, std::uint8_t aType, std::vector<value> aOperands, std::uint64_t aImm0 = 0u, std::uint64_t aImm1 = 0u)
                            {
                                instruction i;
                                i.op = aOp;
                                i.type = aType;
                                i.operands = std::move(aOperands);
                                i.imm0 = aImm0;
                                i.imm1 = aImm1;
                                i.origin = iOrigin;
                                i.block = iCurrent;
                                iFunction.values.push_back(std::move(i));
                                auto const v = static_cast<value>(iFunction.values.size() - 1u);
                                iFunction.blocks[iCurrent].code.push_back(v);
                                return v;
                            }
                            value zero(std::uint8_t aType)
                            {
                                switch (aType)
                                {
                                case I32: return add(0x41u, I32, {}, 0u);
                                case I64: return add(0x42u, I64, {}, 0u);
                                case F32: return add(0x43u, F32, {}, 0u);
                                default: return add(0x44u, F64, {}, 0u);
                                }
                            }
                            void link(std::uint32_t aFrom, std::uint32_t aTo)
                            {
                                iFunction.blocks[aFrom].successors.push_back(aTo);
                                iFunction.blocks[aTo].predecessors.push_back(aFrom);
                            }
                            void terminate(std::uint16_t aOp, std::vector<value> aOperands, std::vector<std::uint32_t> const& aSuccessors, std::uint64_t aImm0 = 0u)
                            {
                                add(aOp, Void, std::move(aOperands), aImm0);
                                for (auto s : aSuccessors)
                                    link(iCurrent, s);
                                iReachable = false;
                            }
                            // --- variables (locals, and the values that blocks end with)
                            variable new_variable(std::uint8_t aType)
                            {
                                iVariableTypes.push_back(aType);
                                return static_cast<variable>(iVariableTypes.size() - 1u);
                            }
                            static std::uint64_t key(variable aVariable, std::uint32_t aBlock) { return (std::uint64_t{ aBlock } << 32u) | aVariable; }
                            void write(variable aVariable, std::uint32_t aBlock, value aValue)
                            {
                                iDefinitions[key(aVariable, aBlock)] = aValue;
                            }
                            value read(variable aVariable, std::uint32_t aBlock)
                            {
                                if (auto const d = iDefinitions.find(key(aVariable, aBlock)); d != iDefinitions.end())
                                    return forwarded(d->second);
                                return read_recursive(aVariable, aBlock);
                            }
                            value read_recursive(variable aVariable, std::uint32_t aBlock)
                            {
                                value v;
                                auto const& b = iFunction.blocks[aBlock];
                                if (!iSealed[aBlock])
                                {
                                    v = phi(aVariable, aBlock);
                                    iIncomplete[aBlock].emplace_back(aVariable, v);
                                }
                                else if (b.predecessors.size() == 1u)
                                    v = read(aVariable, b.predecessors[0]);
                                else if (b.predecessors.empty())
                                {
                                    // (an unreachable block: any value)
                                    auto const save = iCurrent;
                                    iCurrent = aBlock;
                                    v = zero_in(aBlock, iVariableTypes[aVariable]);
                                    iCurrent = save;
                                }
                                else
                                {
                                    v = phi(aVariable, aBlock);
                                    write(aVariable, aBlock, v);
                                    v = phi_operands(aVariable, v);
                                }
                                write(aVariable, aBlock, v);
                                return v;
                            }
                            value zero_in(std::uint32_t aBlock, std::uint8_t aType)
                            {
                                // (a constant at the start of a block (with its phis))
                                instruction i;
                                i.op = aType == I32 ? 0x41u : aType == I64 ? 0x42u : aType == F32 ? 0x43u : 0x44u;
                                i.type = aType;
                                i.block = aBlock;
                                iFunction.values.push_back(std::move(i));
                                auto const v = static_cast<value>(iFunction.values.size() - 1u);
                                iPhis[aBlock].push_back(v);
                                return v;
                            }
                            value phi(variable aVariable, std::uint32_t aBlock)
                            {
                                instruction i;
                                i.op = Phi;
                                i.type = iVariableTypes[aVariable];
                                i.block = aBlock;
                                iFunction.values.push_back(std::move(i));
                                auto const v = static_cast<value>(iFunction.values.size() - 1u);
                                iPhis[aBlock].push_back(v);
                                return v;
                            }
                            value phi_operands(variable aVariable, value aPhi)
                            {
                                auto const block = iFunction.values[aPhi].block;
                                std::vector<value> operands;
                                for (auto p : iFunction.blocks[block].predecessors)
                                    operands.push_back(read(aVariable, p));
                                iFunction.values[aPhi].operands = std::move(operands);
                                return trivial(aPhi);
                            }
                            value trivial(value aPhi)
                            {
                                value same = None;
                                for (auto o : iFunction.values[aPhi].operands)
                                {
                                    o = forwarded(o);
                                    if (o == same || o == aPhi)
                                        continue;
                                    if (same != None)
                                        return aPhi;
                                    same = o;
                                }
                                if (same == None)
                                    return aPhi;    // (unreachable, or only itself: left as it is)
                                forward(aPhi, same);
                                return same;
                            }
                            void seal(std::uint32_t aBlock)
                            {
                                for (auto const& [v, p] : iIncomplete[aBlock])
                                    phi_operands(v, p);
                                iIncomplete[aBlock].clear();
                                iSealed[aBlock] = true;
                            }
                            void forward(value aFrom, value aTo)
                            {
                                if (iForward.size() <= aFrom)
                                    iForward.resize(aFrom + 1u, None);
                                iForward[aFrom] = aTo;
                                iFunction.values[aFrom].dead = true;
                            }
                            value forwarded(value aValue) const
                            {
                                while (aValue < iForward.size() && iForward[aValue] != None)
                                    aValue = iForward[aValue];
                                return aValue;
                            }
                            void resolve()
                            {
                                // (the phis first in their blocks; operands that were replaced, replaced)
                                for (std::size_t b = 0u; b < iFunction.blocks.size(); ++b)
                                {
                                    auto& code = iFunction.blocks[b].code;
                                    code.insert(code.begin(), iPhis[b].begin(), iPhis[b].end());
                                }
                                for (auto& i : iFunction.values)
                                    for (auto& o : i.operands)
                                        o = forwarded(o);
                            }
                            // --- the operand stack
                            value pop()
                            {
                                if (iStack.size() <= iFrames.back().height)
                                    throw unsupported{ "operand stack underflow" };
                                auto const v = iStack.back();
                                iStack.pop_back();
                                return v;
                            }
                            frame& target(std::uint64_t aDepth)
                            {
                                if (aDepth >= iFrames.size())
                                    throw unsupported{ "branch depth" };
                                return iFrames[iFrames.size() - 1u - static_cast<std::size_t>(aDepth)];
                            }
                            // (a branch's values (the stack's top values) are its target's)
                            void branch_values(frame const& aTarget)
                            {
                                if (aTarget.kind == frame::Loop)
                                    return;
                                auto const n = aTarget.resultVariables.size();
                                if (iStack.size() < n)
                                    throw unsupported{ "branch values" };
                                for (std::size_t i = 0u; i < n; ++i)
                                    write(aTarget.resultVariables[i], iCurrent, iStack[iStack.size() - n + i]);
                            }
                            std::vector<std::uint8_t> block_type(std::uint64_t aType) const
                            {
                                if (aType == Void)
                                    return {};
                                auto const t = static_cast<std::uint8_t>(aType);
                                if (t != I32 && t != I64 && t != F32 && t != F64)
                                    throw unsupported{ "block type" };
                                return { t };
                            }
                            void skip(std::size_t& aPosition, std::size_t aEnd)
                            {
                                // (unreachable code: up to its block's else or end)
                                std::size_t depth = 0u;
                                while (aPosition < aEnd)
                                {
                                    auto const op = static_cast<std::uint8_t>(iTokens[aPosition]);
                                    if (op == 0x02u || op == 0x03u || op == 0x04u)
                                        ++depth;
                                    else if (op == 0x0Bu || op == 0x05u)
                                    {
                                        if (depth == 0u)
                                            return;
                                        if (op == 0x0Bu)
                                            --depth;
                                    }
                                    aPosition += 1u + immediates(op, iTokens, aPosition);
                                }
                            }
                            // --- the code
                            void read_code(std::size_t aBegin, std::size_t aEnd)
                            {
                                for (std::size_t p = aBegin; p < aEnd;)
                                {
                                    if (!iReachable)
                                    {
                                        skip(p, aEnd);
                                        if (p >= aEnd)
                                            return;
                                    }
                                    auto const op = static_cast<std::uint8_t>(iTokens[p]);
                                    auto const position = p;
                                    p += 1u + immediates(op, iTokens, p);
                                    auto const imm = [&](std::size_t aIndex) { return iTokens[position + 1u + aIndex]; };
                                    iOrigin = position < iOrigins.size() ? iOrigins[position] : position;
                                    switch (op)
                                    {
                                    case 0x00:
                                        terminate(Trap, {}, {}, 1u);
                                        break;
                                    case 0x01:
                                        break;
                                    case 0x02:
                                        {
                                            frame f;
                                            f.kind = frame::Block;
                                            f.results = block_type(imm(0));
                                            for (auto t : f.results)
                                                f.resultVariables.push_back(new_variable(t));
                                            f.end = new_block();
                                            f.height = iStack.size();
                                            iFrames.push_back(f);
                                        }
                                        break;
                                    case 0x03:
                                        {
                                            frame f;
                                            f.kind = frame::Loop;
                                            f.results = block_type(imm(0));
                                            f.end = new_block();
                                            f.height = iStack.size();
                                            iFunction.blocks[f.end].loopHeader = true;
                                            terminate(Jump, {}, { f.end });
                                            iCurrent = f.end;
                                            iReachable = true;
                                            iFrames.push_back(f);
                                        }
                                        break;
                                    case 0x04:
                                        {
                                            auto const condition = pop();
                                            frame f;
                                            f.kind = frame::If;
                                            f.results = block_type(imm(0));
                                            for (auto t : f.results)
                                                f.resultVariables.push_back(new_variable(t));
                                            f.end = new_block();
                                            f.height = iStack.size();
                                            auto const thenBlock = new_block();
                                            f.elseBlock = new_block();
                                            terminate(Branch, { condition }, { thenBlock, f.elseBlock });
                                            seal(thenBlock);
                                            seal(f.elseBlock);
                                            iCurrent = thenBlock;
                                            iReachable = true;
                                            iFrames.push_back(f);
                                        }
                                        break;
                                    case 0x05:
                                        {
                                            auto& f = iFrames.back();
                                            if (f.kind != frame::If)
                                                throw unsupported{ "else without if" };
                                            if (iReachable)
                                            {
                                                branch_values(f);
                                                terminate(Jump, {}, { f.end });
                                            }
                                            iStack.resize(f.height);
                                            f.hasElse = true;
                                            iCurrent = f.elseBlock;
                                            iReachable = true;
                                        }
                                        break;
                                    case 0x0B:
                                        {
                                            auto f = iFrames.back();
                                            if (f.kind == frame::Function)
                                            {
                                                if (iReachable)
                                                {
                                                    branch_values(f);
                                                    terminate(Jump, {}, { f.end });
                                                }
                                                iStack.resize(f.height);
                                                return;
                                            }
                                            iFrames.pop_back();
                                            if (f.kind == frame::Loop)
                                            {
                                                // (its header's predecessors are known: the code continues (the loop's values are the stack's))
                                                seal(f.end);
                                                break;
                                            }
                                            if (iReachable)
                                            {
                                                branch_values(f);
                                                terminate(Jump, {}, { f.end });
                                            }
                                            if (f.kind == frame::If && !f.hasElse)
                                            {
                                                // (no else: its else block goes to its end)
                                                iCurrent = f.elseBlock;
                                                iReachable = true;
                                                terminate(Jump, {}, { f.end });
                                            }
                                            iStack.resize(f.height);
                                            seal(f.end);
                                            iCurrent = f.end;
                                            iReachable = !iFunction.blocks[f.end].predecessors.empty();
                                            if (iReachable)
                                                for (auto v : f.resultVariables)
                                                    iStack.push_back(read(v, f.end));
                                        }
                                        break;
                                    case 0x0C:
                                        {
                                            auto& t = target(imm(0));
                                            branch_values(t);
                                            terminate(Jump, {}, { t.end });
                                        }
                                        break;
                                    case 0x0D:
                                        {
                                            auto const condition = pop();
                                            auto& t = target(imm(0));
                                            branch_values(t);
                                            auto const next = new_block();
                                            terminate(Branch, { condition }, { t.end, next });
                                            seal(next);
                                            iCurrent = next;
                                            iReachable = true;
                                        }
                                        break;
                                    case 0x0E:
                                        {
                                            auto const index = pop();
                                            auto const count = static_cast<std::size_t>(imm(0));
                                            std::vector<std::uint32_t> targets;
                                            for (std::size_t i = 0u; i <= count; ++i)
                                            {
                                                auto& t = target(imm(1u + i));
                                                branch_values(t);
                                                targets.push_back(t.end);
                                            }
                                            terminate(Switch, { index }, targets);
                                        }
                                        break;
                                    case 0x0F:
                                        {
                                            auto& t = iFrames.front();
                                            branch_values(t);
                                            terminate(Jump, {}, { t.end });
                                        }
                                        break;
                                    case 0x10:
                                        {
                                            auto const callee = static_cast<std::uint32_t>(imm(0));
                                            std::vector<std::uint8_t> parameters;
                                            std::vector<std::uint8_t> results;
                                            if (callee == optimizer::intrinsics::deopt(iModule))
                                            {
                                                // (the function's own code makes the call: the function's locals, the operand stack below
                                                // the call's arguments, and its arguments)
                                                auto const number = pop();
                                                if (!iFunction.values[number].constant() || iFunction.values[number].imm0 >= iDeopts.size())
                                                    throw unsupported{ "deopt" };
                                                auto const index = iFunction.values[number].imm0;
                                                auto const& d = iDeopts[static_cast<std::size_t>(index)];
                                                std::vector<value> arguments(d.arguments);
                                                for (auto a = arguments.size(); a-- > 0u;)
                                                    arguments[a] = pop();
                                                if (d.locals > iLocals || iStack.size() < d.depth)
                                                    throw unsupported{ "deopt state" };
                                                std::vector<value> operands;
                                                operands.reserve(d.locals + d.depth + d.arguments);
                                                // (a local that the function's own code does not read before it sets it: 0)
                                                for (std::uint32_t l = 0u; l < d.locals; ++l)
                                                    operands.push_back(l < d.live.size() && !d.live[l] ? zero(iVariableTypes[l]) : read(static_cast<variable>(l), iCurrent));
                                                for (std::uint32_t e = 0u; e < d.depth; ++e)
                                                    operands.push_back(iStack[e]);
                                                operands.insert(operands.end(), arguments.begin(), arguments.end());
                                                terminate(Deopt, std::move(operands), {}, index);
                                                break;
                                            }
                                            if (callee == optimizer::intrinsics::guard_failed(iModule))
                                                parameters = { I32 };
                                            else if (callee < iModule.functions.size())
                                            {
                                                parameters = *iModule.functions[callee].parameters;
                                                results = *iModule.functions[callee].results;
                                            }
                                            else
                                                throw unsupported{ "function index" };
                                            call(0x10u, callee, 0u, parameters, results, false);
                                        }
                                        break;
                                    case 0x11:
                                        {
                                            auto const type = static_cast<std::size_t>(imm(0));
                                            if (type >= iModule.types.size())
                                                throw unsupported{ "type index" };
                                            call(0x11u, imm(0), imm(1), *iModule.types[type].first, *iModule.types[type].second, true);
                                        }
                                        break;
                                    case 0x1A:
                                        pop();
                                        break;
                                    case 0x1B:
                                        {
                                            auto const c = pop();
                                            auto const y = pop();
                                            auto const x = pop();
                                            iStack.push_back(add(0x1Bu, iFunction.values[x].type, { x, y, c }));
                                        }
                                        break;
                                    case 0x20:
                                        {
                                            auto const l = static_cast<std::size_t>(imm(0));
                                            if (l >= iLocals)
                                                throw unsupported{ "local index" };
                                            iStack.push_back(read(static_cast<variable>(l), iCurrent));
                                        }
                                        break;
                                    case 0x21:
                                    case 0x22:
                                        {
                                            auto const l = static_cast<std::size_t>(imm(0));
                                            if (l >= iLocals)
                                                throw unsupported{ "local index" };
                                            auto const v = pop();
                                            write(static_cast<variable>(l), iCurrent, v);
                                            if (op == 0x22u)
                                                iStack.push_back(v);
                                        }
                                        break;
                                    case 0x23:
                                        {
                                            auto const g = static_cast<std::size_t>(imm(0));
                                            if (g >= iModule.globals->size() || g >= iModule.globalTypes->size())
                                                throw unsupported{ "global index" };
                                            iStack.push_back(add(0x23u, (*iModule.globalTypes)[g], {}, imm(0)));
                                        }
                                        break;
                                    case 0x24:
                                        {
                                            auto const v = pop();
                                            add(0x24u, Void, { v }, imm(0));
                                        }
                                        break;
                                    case 0x41:
                                        iStack.push_back(add(op, I32, {}, static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int64_t>(imm(0))))));
                                        break;
                                    case 0x42:
                                        iStack.push_back(add(op, I64, {}, imm(0)));
                                        break;
                                    case 0x43:
                                        iStack.push_back(add(op, F32, {}, imm(0) & 0xFFFFFFFFull));
                                        break;
                                    case 0x44:
                                        iStack.push_back(add(op, F64, {}, imm(0)));
                                        break;
                                    default:
                                        if (op >= 0x28u && op <= 0x35u)
                                        {
                                            auto const address = pop();
                                            iStack.push_back(add(op, load_type(op), { address }, imm(0), imm(1)));
                                        }
                                        else if (op >= 0x36u && op <= 0x3Eu)
                                        {
                                            auto const v = pop();
                                            auto const address = pop();
                                            add(op, Void, { address, v }, imm(0), imm(1));
                                        }
                                        else if (auto const n = numeric_of(op); n.count != 0u)
                                        {
                                            if (n.count == 2u)
                                            {
                                                auto const y = pop();
                                                auto const x = pop();
                                                iStack.push_back(add(op, n.result, { x, y }));
                                            }
                                            else
                                            {
                                                auto const x = pop();
                                                iStack.push_back(add(op, n.result, { x }));
                                            }
                                        }
                                        else
                                            throw unsupported{ "instruction" };
                                        break;
                                    }
                                }
                                throw unsupported{ "unterminated function" };
                            }
                            void call(std::uint16_t aOp, std::uint64_t aImm0, std::uint64_t aImm1, std::vector<std::uint8_t> const& aParameters,
                                std::vector<std::uint8_t> const& aResults, bool aIndirect)
                            {
                                std::vector<value> operands(aParameters.size() + (aIndirect ? 1u : 0u));
                                for (auto i = operands.size(); i-- > 0u;)
                                    operands[i] = pop();
                                auto const c = add(aOp, aResults.empty() ? Void : aResults[0], std::move(operands), aImm0, aImm1);
                                if (!aResults.empty())
                                    iStack.push_back(c);
                                for (std::size_t i = 1u; i < aResults.size(); ++i)
                                    iStack.push_back(add(CallResult, aResults[i], { c }, i));
                            }
                        private:
                            module_description const& iModule;
                            std::vector<std::uint64_t> const& iTokens;
                            std::vector<std::size_t> const& iOrigins;
                            std::vector<optimizer::deopt> const& iDeopts;
                            function iFunction;
                            std::vector<std::uint8_t> iVariableTypes;
                            std::size_t iLocals = 0u;
                            std::unordered_map<std::uint64_t, value> iDefinitions;
                            std::vector<bool> iSealed;
                            std::vector<std::vector<std::pair<variable, value>>> iIncomplete;
                            std::vector<std::vector<value>> iPhis;
                            std::vector<value> iForward;
                            std::vector<value> iStack;
                            std::vector<frame> iFrames;
                            std::uint32_t iCurrent = 0u;
                            bool iReachable = true;
                            std::size_t iOrigin = 0u;
                        };

                        // --- optimizations

                        bool pure(instruction const& aInstruction)
                        {
                            auto const op = aInstruction.op;
                            if (aInstruction.constant() || op == Phi || op == Param || op == 0x1Bu)
                                return true;
                            if (op >= 0x45u && op <= 0xC4u)
                            {
                                // (integer division and float to integer truncation may trap)
                                if ((op >= 0x6Du && op <= 0x70u) || (op >= 0x7Fu && op <= 0x82u))
                                    return false;
                                if ((op >= 0xA8u && op <= 0xABu) || (op >= 0xAEu && op <= 0xB1u))
                                    return false;
                                return true;
                            }
                            // (a load that cannot trap)
                            if (aInstruction.load() && (aInstruction.flags & Unchecked) != 0u)
                                return true;
                            return false;
                        }

                        void count_uses(function& aFunction)
                        {
                            aFunction.uses.assign(aFunction.values.size(), 0u);
                            for (auto const& b : aFunction.blocks)
                                if (b.reachable)
                                    for (auto v : b.code)
                                        if (!aFunction.values[v].dead)
                                            for (auto o : aFunction.values[v].operands)
                                                ++aFunction.uses[o];
                        }

                        // (a branch whose condition is a constant: a jump (its other successor loses it as a predecessor))
                        void remove_edge(function& aFunction, std::uint32_t aFrom, std::size_t aSuccessor)
                        {
                            auto& from = aFunction.blocks[aFrom];
                            auto const to = from.successors[aSuccessor];
                            from.successors.erase(from.successors.begin() + static_cast<std::ptrdiff_t>(aSuccessor));
                            auto& target = aFunction.blocks[to];
                            auto const p = std::find(target.predecessors.begin(), target.predecessors.end(), aFrom);
                            auto const index = static_cast<std::size_t>(p - target.predecessors.begin());
                            target.predecessors.erase(p);
                            for (auto v : target.code)
                            {
                                auto& i = aFunction.values[v];
                                if (i.op != Phi)
                                    break;
                                if (index < i.operands.size())
                                    i.operands.erase(i.operands.begin() + static_cast<std::ptrdiff_t>(index));
                            }
                        }

                        // (values replaced by others: their uses' operands are replaced (all at once, at the end of a pass))
                        struct replacements
                        {
                            std::vector<value> to;
                            void add(value aFrom, value aTo)
                            {
                                if (to.size() <= aFrom)
                                    to.resize(aFrom + 1u, None);
                                to[aFrom] = aTo;
                            }
                            value resolved(value aValue) const
                            {
                                for (std::size_t hops = 0u; aValue < to.size() && to[aValue] != None && hops < 1000000u; ++hops)
                                    aValue = to[aValue];
                                return aValue;
                            }
                            void apply(function& aFunction) const
                            {
                                if (to.empty())
                                    return;
                                for (auto& i : aFunction.values)
                                    for (auto& o : i.operands)
                                        o = resolved(o);
                            }
                        };

                        bool fold_constants(function& aFunction)
                        {
                            bool changed = false;
                            replacements replaced;
                            auto const replace_uses = [&](function&, value aFrom, value aTo) { replaced.add(aFrom, aTo); };
                            for (auto b : aFunction.order)
                                for (auto v : aFunction.blocks[b].code)
                                {
                                    auto& i = aFunction.values[v];
                                    if (i.dead)
                                        continue;
                                    for (auto& o : i.operands)
                                        o = replaced.resolved(o);
                                    auto const constant = [&](std::size_t aOperand) -> instruction const*
                                    {
                                        auto const& o = aFunction.values[i.operands[aOperand]];
                                        return o.constant() ? &o : nullptr;
                                    };
                                    if (i.op >= 0x45u && i.op <= 0xC4u)
                                    {
                                        auto const n = numeric_of(static_cast<std::uint8_t>(i.op));
                                        if (n.operand == F32 || n.operand == F64)
                                            if (i.op != 0xBCu && i.op != 0xBDu)
                                                continue;
                                        auto const* x = constant(0u);
                                        auto const* y = n.count == 2u ? constant(1u) : x;
                                        if (x == nullptr || y == nullptr)
                                            continue;
                                        if (auto const r = optimizer::fold_integer(static_cast<std::uint8_t>(i.op), x->imm0, n.count == 2u ? y->imm0 : 0u))
                                        {
                                            auto const type = i.type;
                                            i.op = type == I32 ? 0x41u : type == I64 ? 0x42u : type == F32 ? 0x43u : 0x44u;
                                            i.imm0 = *r & ((type == I32 || type == F32) ? 0xFFFFFFFFull : ~std::uint64_t{});
                                            i.operands.clear();
                                            changed = true;
                                        }
                                    }
                                    else if (i.op == 0x1Bu)
                                    {
                                        if (auto const* c = constant(2u))
                                        {
                                            auto const chosen = i.operands[(c->imm0 & 0xFFFFFFFFu) != 0u ? 0u : 1u];
                                            i.dead = true;
                                            replace_uses(aFunction, v, chosen);
                                            changed = true;
                                        }
                                    }
                                    else if (i.op == Branch)
                                    {
                                        if (auto const* c = constant(0u))
                                        {
                                            // (the successor that is not taken is no longer one)
                                            remove_edge(aFunction, b, (c->imm0 & 0xFFFFFFFFu) != 0u ? 1u : 0u);
                                            i.op = Jump;
                                            i.operands.clear();
                                            changed = true;
                                        }
                                    }
                                    else if (i.op == Phi)
                                    {
                                        // (a phi whose operands are all the same value (or itself))
                                        value same = None;
                                        bool trivial = true;
                                        for (auto o : i.operands)
                                        {
                                            if (o == v || o == same)
                                                continue;
                                            if (same != None)
                                            {
                                                trivial = false;
                                                break;
                                            }
                                            same = o;
                                        }
                                        if (trivial && same != None)
                                        {
                                            i.dead = true;
                                            replace_uses(aFunction, v, same);
                                            changed = true;
                                        }
                                    }
                                }
                            replaced.apply(aFunction);
                            return changed;
                        }

                        void eliminate_dead_code(function& aFunction)
                        {
                            // (instructions without effects whose values are not used (those of blocks that cannot be reached: none, and their
                            // operands' uses are not counted))
                            for (auto& b : aFunction.blocks)
                                if (!b.reachable)
                                    for (auto v : b.code)
                                        if (!aFunction.values[v].constant())    // (a constant is where it is used)
                                            aFunction.values[v].dead = true;
                            count_uses(aFunction);
                            std::vector<value> work;
                            for (auto const& b : aFunction.blocks)
                                if (b.reachable)
                                    for (auto v : b.code)
                                        if (!aFunction.values[v].dead && aFunction.uses[v] == 0u && pure(aFunction.values[v]) && aFunction.values[v].op != Param)
                                            work.push_back(v);
                            while (!work.empty())
                            {
                                auto const v = work.back();
                                work.pop_back();
                                auto& i = aFunction.values[v];
                                if (i.dead)
                                    continue;
                                i.dead = true;
                                for (auto o : i.operands)
                                    if (--aFunction.uses[o] == 0u && pure(aFunction.values[o]) && aFunction.values[o].op != Param)
                                        work.push_back(o);
                            }
                        }

                        // (algebraic simplifications: x + 0, x * 1, x & ~0 ... are x; x & 0, x * 0 are 0; eqz of a comparison is its inverse
                        // (fused with the branch that uses it); (x & a high mask) == c: (x >> its zeros) == c >> them (a shift and a
                        // small immediate, not two wide constants))
                        // (a value that is 0 or 1 (an i32): a comparison's, or such values' and, or or xor (to aDepth))
                        bool is_boolean(function const& aFunction, value aValue, int aDepth)
                        {
                            auto const& i = aFunction.values[aValue];
                            if (i.type != I32)
                                return false;
                            if ((i.op >= 0x45u && i.op <= 0x66u) && i.op != 0x50u)
                                return true;
                            if (i.op == 0x50u)
                                return true;    // (i64.eqz)
                            if (i.constant())
                                return (i.imm0 & 0xFFFFFFFFu) <= 1u;
                            if ((i.op == 0x71u || i.op == 0x72u || i.op == 0x73u) && aDepth > 0 && i.operands.size() == 2u)
                                return is_boolean(aFunction, i.operands[0], aDepth - 1) && is_boolean(aFunction, i.operands[1], aDepth - 1);
                            if (i.op == ssa::Phi && aDepth > 0)
                            {
                                for (auto o : i.operands)
                                    if (o == aValue || !is_boolean(aFunction, o, aDepth - 1))
                                        return false;
                                return !i.operands.empty();
                            }
                            return false;
                        }

                        bool simplify(function& aFunction)
                        {
                            bool changed = false;
                            auto& values = aFunction.values;
                            count_uses(aFunction);
                            replacements replaced;
                            auto const new_constant = [&](std::uint8_t aType, std::uint64_t aBits, std::uint32_t aBlock) -> value
                            {
                                instruction c;
                                c.op = aType == I32 ? 0x41u : 0x42u;
                                c.type = aType;
                                c.imm0 = aType == I32 ? (aBits & 0xFFFFFFFFu) : aBits;
                                c.block = aBlock;
                                values.push_back(c);
                                return static_cast<value>(values.size() - 1u);
                            };
                            auto const integer_comparison = [](std::uint16_t aOp) { return (aOp >= 0x46u && aOp <= 0x4Fu) || (aOp >= 0x51u && aOp <= 0x5Au); };
                            auto const inverse = [](std::uint16_t aOp) -> std::uint16_t
                            {
                                // (eq ne lt_s lt_u gt_s gt_u le_s le_u ge_s ge_u)
                                static constexpr std::uint8_t sInverse[] = { 1, 0, 8, 9, 6, 7, 4, 5, 2, 3 };
                                auto const base = aOp >= 0x51u ? 0x51u : 0x46u;
                                return static_cast<std::uint16_t>(base + sInverse[aOp - base]);
                            };
                            for (auto b : aFunction.order)
                                for (std::size_t n = 0u; n < aFunction.blocks[b].code.size(); ++n)
                                {
                                    auto const v = aFunction.blocks[b].code[n];
                                    if (values[v].dead)
                                        continue;
                                    for (auto& o : values[v].operands)
                                        o = replaced.resolved(o);
                                    auto& i = values[v];
                                    auto const op = i.op;
                                    if ((op == 0xBFu || op == 0xBDu) && i.operands.size() == 1u)
                                    {
                                        // (a reinterpretation of a reinterpretation: the value)
                                        auto const& x = values[i.operands[0]];
                                        if (x.op == (op == 0xBFu ? 0xBDu : 0xBFu) && x.operands.size() == 1u)
                                        {
                                            i.dead = true;
                                            replaced.add(v, x.operands[0]);
                                            changed = true;
                                        }
                                        continue;
                                    }
                                    if (op >= 0x54u && op <= 0x5Au && (op - 0x54u) % 2u == 0u && i.operands.size() == 2u)
                                    {
                                        // (an i64 unsigned comparison (lt_u gt_u le_u ge_u) of an integer converted to f64 (its bits, which are
                                        // at most -infinity's: it is not a NaN) with a constant above that: known)
                                        constexpr std::uint64_t Most = 0xFFF0000000000000ull;
                                        auto const converted = [&](value aValue)
                                        {
                                            auto const& x = values[aValue];
                                            return x.op == 0xBDu && x.operands.size() == 1u && values[x.operands[0]].op >= 0xB7u && values[x.operands[0]].op <= 0xBAu;
                                        };
                                        std::optional<bool> known;
                                        auto const& a0 = values[i.operands[0]];
                                        auto const& a1 = values[i.operands[1]];
                                        if (converted(i.operands[0]) && a1.constant())
                                        {
                                            auto const k = a1.imm0;
                                            if (op == 0x54u && k > Most) known = true;          // (x < k)
                                            else if (op == 0x58u && k >= Most) known = true;    // (x <= k)
                                            else if (op == 0x56u && k >= Most) known = false;   // (x > k)
                                            else if (op == 0x5Au && k > Most) known = false;    // (x >= k)
                                        }
                                        else if (converted(i.operands[1]) && a0.constant())
                                        {
                                            auto const k = a0.imm0;
                                            if (op == 0x56u && k > Most) known = true;          // (k > x)
                                            else if (op == 0x5Au && k >= Most) known = true;    // (k >= x)
                                            else if (op == 0x54u && k >= Most) known = false;   // (k < x)
                                            else if (op == 0x58u && k > Most) known = false;    // (k <= x)
                                        }
                                        if (known)
                                        {
                                            i.op = 0x41u;
                                            i.type = I32;
                                            i.imm0 = *known ? 1u : 0u;
                                            i.operands.clear();
                                            changed = true;
                                            continue;
                                        }
                                    }
                                    if (op >= 0x61u && op <= 0x66u && i.operands.size() == 2u)
                                    {
                                        // (an f64 comparison of an i32 converted to f64 with a constant that is an integer within the i32's range:
                                        // the i32's comparison with it)
                                        auto x = i.operands[0];
                                        auto c = i.operands[1];
                                        bool swapped = false;
                                        if (values[x].constant())
                                        {
                                            std::swap(x, c);
                                            swapped = true;
                                        }
                                        auto const& cx = values[x];
                                        if (values[c].op == 0x44u && (cx.op == 0xB7u || cx.op == 0xB8u) && cx.operands.size() == 1u)
                                        {
                                            double const d = std::bit_cast<double>(values[c].imm0);
                                            bool const sign = cx.op == 0xB7u;
                                            bool const within = d == d && std::floor(d) == d && (sign ? (d >= -2147483648.0 && d <= 2147483647.0) : (d >= 0.0 && d <= 4294967295.0));
                                            if (within)
                                            {
                                                // (eq ne lt gt le ge -> i32's; the constant first: lt and gt (le and ge) swapped)
                                                static constexpr std::uint16_t sSigned[] = { 0x46u, 0x47u, 0x48u, 0x4Au, 0x4Cu, 0x4Eu };
                                                static constexpr std::uint16_t sUnsigned[] = { 0x46u, 0x47u, 0x49u, 0x4Bu, 0x4Du, 0x4Fu };
                                                static constexpr std::size_t sSwapped[] = { 0u, 1u, 3u, 2u, 5u, 4u };
                                                auto const k = swapped ? sSwapped[op - 0x61u] : static_cast<std::size_t>(op - 0x61u);
                                                auto const bits = sign ? static_cast<std::uint64_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(d))) : static_cast<std::uint64_t>(static_cast<std::uint32_t>(d));
                                                auto const operand = cx.operands[0];
                                                auto const constant = new_constant(I32, bits, b);
                                                auto& j = values[v];
                                                j.op = sign ? sSigned[k] : sUnsigned[k];
                                                j.operands = { operand, constant };
                                                changed = true;
                                                continue;
                                            }
                                        }
                                    }
                                    if (op == 0x45u)
                                    {
                                        auto const& x = values[i.operands[0]];
                                        if (integer_comparison(x.op))
                                        {
                                            auto const operands = x.operands;
                                            i.op = inverse(x.op);
                                            values[v].operands = operands;
                                            changed = true;
                                        }
                                        else if (x.op == 0x45u && integer_comparison(values[x.operands[0]].op))
                                        {
                                            i.dead = true;
                                            replaced.add(v, x.operands[0]);
                                            changed = true;
                                        }
                                        continue;
                                    }
                                    if ((op == 0x51u || op == 0x52u) && i.operands.size() == 2u)
                                    {
                                        // (a boolean (0 or 1) made a word (zero extended, plus K), compared with a constant C: the boolean (C is
                                        // K + 1), its inverse (C is K) or a constant)
                                        auto x = i.operands[0];
                                        auto c = i.operands[1];
                                        if (values[x].constant())
                                            std::swap(x, c);
                                        if (values[c].constant() && !values[x].constant() && values[x].op == 0x7Cu && values[x].operands.size() == 2u)
                                        {
                                            auto z = values[x].operands[0];
                                            auto k = values[x].operands[1];
                                            if (values[z].constant())
                                                std::swap(z, k);
                                            if (values[k].constant() && values[z].op == 0xADu && is_boolean(aFunction, values[z].operands[0], 4))
                                            {
                                                auto const b = values[z].operands[0];
                                                auto const kk = values[k].imm0;
                                                auto const cc = values[c].imm0;
                                                bool const eq = op == 0x51u;
                                                if (cc == kk + 1u || cc == kk)
                                                {
                                                    if ((cc == kk + 1u) == eq)
                                                    {
                                                        i.dead = true;
                                                        replaced.add(v, b);
                                                    }
                                                    else
                                                    {
                                                        i.op = 0x45u;
                                                        i.operands = { b };
                                                    }
                                                }
                                                else
                                                {
                                                    i.op = 0x41u;
                                                    i.imm0 = eq ? 0u : 1u;
                                                    i.operands.clear();
                                                }
                                                changed = true;
                                                continue;
                                            }
                                        }
                                    }
                                    if (!((op >= 0x6Au && op <= 0x78u) || (op >= 0x7Cu && op <= 0x8Au)) && op != 0x51u && op != 0x52u)
                                        continue;
                                    if (i.operands.size() != 2u)
                                        continue;
                                    auto const wide = op >= 0x7Cu || op == 0x51u || op == 0x52u;
                                    auto const ones = wide ? ~std::uint64_t{} : std::uint64_t{ 0xFFFFFFFFu };
                                    if (op == 0x51u || op == 0x52u)
                                        continue;   // (see below)
                                    auto const base = wide ? 0x7Cu : 0x6Au;
                                    auto const k = op - base;     // (0 add, 1 sub, 2 mul, 7 and, 8 or, 9 xor, 10 shl, 11 shr_s, 12 shr_u, 13 rotl, 14 rotr)
                                    auto const x = i.operands[0];
                                    auto const y = i.operands[1];
                                    auto const cx = values[x].constant() ? std::optional<std::uint64_t>{ values[x].imm0 & ones } : std::nullopt;
                                    auto const cy = values[y].constant() ? std::optional<std::uint64_t>{ values[y].imm0 & ones } : std::nullopt;
                                    if (cx && cy)
                                        continue;   // (folded)
                                    if (wide && (k == 7u || k == 10u))
                                    {
                                        // (bits: (a | C1) & C2 is a & C2 if C1 and C2 have no bit in common; (a & C1) & C2 is a & (C1 & C2);
                                        // ((a >> K) & M) << K (logical shifts) is a & (M << K))
                                        auto const constant_of = [&](value aValue) -> std::optional<std::uint64_t>
                                        {
                                            return values[aValue].constant() ? std::optional<std::uint64_t>{ values[aValue].imm0 } : std::nullopt;
                                        };
                                        auto const split = [&](value aValue, std::uint16_t aOp, value& aOther, std::uint64_t& aConstant)
                                        {
                                            auto const& w = values[aValue];
                                            if (w.dead || w.op != aOp || w.operands.size() != 2u)
                                                return false;
                                            if (auto const c0 = constant_of(w.operands[1]))
                                            {
                                                aOther = w.operands[0];
                                                aConstant = *c0;
                                                return true;
                                            }
                                            if (auto const c1 = constant_of(w.operands[0]))
                                            {
                                                aOther = w.operands[1];
                                                aConstant = *c1;
                                                return true;
                                            }
                                            return false;
                                        };
                                        if (k == 7u && (cx || cy))
                                        {
                                            auto const operand = cy ? x : y;
                                            auto const c2 = cy ? *cy : *cx;
                                            value inner = None;
                                            std::uint64_t c1 = 0u;
                                            if (split(operand, 0x84u, inner, c1) && (c1 & c2) == 0u)
                                            {
                                                values[v].operands = { inner, cy ? y : x };
                                                changed = true;
                                                continue;
                                            }
                                            if (split(operand, 0x83u, inner, c1))
                                            {
                                                auto const constant = new_constant(I64, c1 & c2, b);
                                                values[v].operands = { inner, constant };
                                                changed = true;
                                                continue;
                                            }
                                        }
                                        if (k == 10u && cy)
                                        {
                                            auto const shift = *cy & 63u;
                                            value masked = None;
                                            std::uint64_t mask = 0u;
                                            value shifted = None;
                                            std::uint64_t by = 0u;
                                            if (split(x, 0x83u, masked, mask) && split(masked, 0x88u, shifted, by) && (by & 63u) == shift && 
                                                values[values[masked].operands[1]].constant())
                                            {
                                                auto const constant = new_constant(I64, mask << shift, b);
                                                auto& j = values[v];
                                                j.op = 0x83u;
                                                j.operands = { shifted, constant };
                                                changed = true;
                                                continue;
                                            }
                                        }
                                    }
                                    value same = None;
                                    std::optional<std::uint64_t> result;
                                    switch (k)
                                    {
                                    case 0u: case 8u: case 9u:      // (add, or, xor: 0)
                                        if (cy && *cy == 0u) same = x;
                                        else if (cx && *cx == 0u) same = y;
                                        break;
                                    case 1u: case 10u: case 11u: case 12u: case 13u: case 14u:     // (sub, shifts, rotates: by 0)
                                        if (cy && (k == 1u ? *cy : (*cy & (wide ? 63u : 31u))) == 0u) same = x;
                                        break;
                                    case 2u:                        // (mul: 1, 0)
                                        if (cy && *cy == 1u) same = x;
                                        else if (cx && *cx == 1u) same = y;
                                        else if ((cy && *cy == 0u) || (cx && *cx == 0u)) result = 0u;
                                        break;
                                    case 7u:                        // (and: all ones, 0)
                                        if (cy && *cy == ones) same = x;
                                        else if (cx && *cx == ones) same = y;
                                        else if ((cy && *cy == 0u) || (cx && *cx == 0u)) result = 0u;
                                        break;
                                    default:
                                        break;
                                    }
                                    if (same != None)
                                    {
                                        i.dead = true;
                                        replaced.add(v, same);
                                        changed = true;
                                    }
                                    else if (result)
                                    {
                                        i.op = wide ? 0x42u : 0x41u;
                                        i.imm0 = *result;
                                        i.operands.clear();
                                        changed = true;
                                    }
                                }
                            replaced.apply(aFunction);
                            // ((x & M) compared (== or !=) only with constants C within M, M: ones then (at least 32) zeros: x >> the zeros,
                            // compared with C >> them)
                            std::vector<std::int32_t> candidate(values.size(), -1);     // (-1: not one; else its zeros)
                            for (auto b : aFunction.order)
                                for (auto v : aFunction.blocks[b].code)
                                {
                                    auto const& i = values[v];
                                    if (i.dead || i.op != 0x83u || !values[i.operands[1]].constant())
                                        continue;
                                    auto const mask = values[i.operands[1]].imm0;
                                    auto const zeros = std::countr_zero(mask);
                                    if (mask != 0u && zeros >= 32 && (mask >> zeros) == (~std::uint64_t{} >> zeros))
                                        candidate[v] = zeros;
                                }
                            std::vector<std::vector<value>> users(values.size());
                            for (auto b : aFunction.order)
                                for (auto v : aFunction.blocks[b].code)
                                {
                                    auto const& i = values[v];
                                    if (i.dead)
                                        continue;
                                    for (std::size_t o = 0u; o < i.operands.size(); ++o)
                                    {
                                        auto const x = i.operands[o];
                                        if (x >= candidate.size() || candidate[x] < 0)
                                            continue;
                                        auto const other = i.operands.size() == 2u ? i.operands[1u - o] : None;
                                        bool const fits = (i.op == 0x51u || i.op == 0x52u) && other != None && values[other].constant() &&
                                            (values[other].imm0 & ~values[values[x].operands[1]].imm0) == 0u;
                                        if (!fits)
                                            candidate[x] = -1;
                                        else
                                            users[x].push_back(v);
                                    }
                                }
                            for (value x = 0u; x < candidate.size(); ++x)
                            {
                                if (candidate[x] < 0 || users[x].empty())
                                    continue;
                                auto const zeros = static_cast<std::uint64_t>(candidate[x]);
                                auto const block = values[x].block;
                                auto const shift = new_constant(I64, zeros, block);
                                values[x].op = 0x88u;      // (i64.shr_u)
                                values[x].operands[1] = shift;
                                for (auto u : users[x])
                                    for (std::size_t o = 0u; o < values[u].operands.size(); ++o)
                                        if (auto const c = values[u].operands[o]; c != x)
                                        {
                                            auto const shifted = new_constant(I64, values[c].imm0 >> zeros, block);
                                            values[u].operands[o] = shifted;
                                        }
                                changed = true;
                            }
                            return changed;
                        }

                        // (global value numbering: an instruction that computes what one that dominates it does (the same operation of the same
                        // operands) is replaced by it; loads (and global.get) also need the same memory: no store or call between them (within
                        // an extended basic block); a load of what a store stored there (in the same memory) is the stored value)
                        void number_values(function& aFunction)
                        {
                            auto& blocks = aFunction.blocks;
                            auto& values = aFunction.values;
                            auto const& order = aFunction.order;
                            if (order.empty())
                                return;
                            // (dominators (Cooper, Harvey and Kennedy's iterative method), by reverse postorder)
                            std::vector<std::uint32_t> position(blocks.size(), ~std::uint32_t{});
                            for (std::uint32_t i = 0u; i < order.size(); ++i)
                                position[order[i]] = i;
                            constexpr std::uint32_t Undefined = ~std::uint32_t{};
                            std::vector<std::uint32_t> idom(blocks.size(), Undefined);
                            idom[order[0]] = order[0];
                            for (bool changed = true; changed;)
                            {
                                changed = false;
                                for (std::size_t i = 1u; i < order.size(); ++i)
                                {
                                    auto const b = order[i];
                                    std::uint32_t d = Undefined;
                                    for (auto p : blocks[b].predecessors)
                                    {
                                        if (position[p] == Undefined || idom[p] == Undefined)
                                            continue;
                                        if (d == Undefined)
                                        {
                                            d = p;
                                            continue;
                                        }
                                        auto x = p;
                                        auto y = d;
                                        while (x != y)
                                        {
                                            while (position[x] > position[y])
                                                x = idom[x];
                                            while (position[y] > position[x])
                                                y = idom[y];
                                        }
                                        d = x;
                                    }
                                    if (d != Undefined && idom[b] != d)
                                    {
                                        idom[b] = d;
                                        changed = true;
                                    }
                                }
                            }
                            // (a block's children in the dominator tree, by reverse postorder: a join's predecessors (but a loop's back
                            // edges) are visited before it)
                            std::vector<std::vector<std::uint32_t>> children(blocks.size());
                            for (std::size_t i = 1u; i < order.size(); ++i)
                                if (idom[order[i]] != Undefined)
                                    children[idom[order[i]]].push_back(order[i]);
                            std::vector<bool> exited(blocks.size(), false);
                            // (the table of values by their keys: (operation, type, immediates, epoch (memory's), operands); scoped by the
                            // dominator tree)
                            struct key_hash
                            {
                                std::size_t operator()(std::vector<std::uint64_t> const& aKey) const noexcept
                                {
                                    std::uint64_t h = 0x9E3779B97F4A7C15ull;
                                    for (auto k : aKey)
                                        h = (h ^ k) * 0x100000001B3ull + (h >> 29u);
                                    return static_cast<std::size_t>(h);
                                }
                            };
                            std::unordered_map<std::vector<std::uint64_t>, value, key_hash> table;
                            table.reserve(values.size());
                            std::vector<std::vector<std::uint64_t>> added;      // (the keys added, in order: removed as the walk leaves a block)
                            // (memory's epochs: one for each alias class (see optimizer::MemargClassShift: class 0's changes with any store),
                            // and globals')
                            constexpr std::size_t Globals = optimizer::MemargClasses;
                            using epoch_set = std::array<std::uint64_t, optimizer::MemargClasses + 1u>;
                            std::uint64_t epochs = 0u;
                            std::vector<epoch_set> exitEpoch(blocks.size(), epoch_set{});
                            auto const class_of = [](instruction const& aAccess) { return static_cast<std::size_t>((aAccess.imm0 >> optimizer::MemargClassShift) & optimizer::MemargClassMask); };
                            replacements replaced;
                            // (constants of the same type and bits: one value (so that instructions of them compare equal))
                            {
                                std::unordered_map<std::uint64_t, value> constants[4];
                                for (std::size_t v = 0u; v < values.size(); ++v)
                                {
                                    auto const& i = values[v];
                                    if (!i.constant() || i.dead)
                                        continue;
                                    auto& same = constants[i.op - 0x41u];
                                    if (auto const [where, inserted] = same.try_emplace(i.imm0, static_cast<value>(v)); !inserted)
                                        replaced.add(static_cast<value>(v), where->second);
                                }
                            }
                            auto const commutative = [](std::uint16_t aOp)
                            {
                                switch (aOp)
                                {
                                case 0x46u: case 0x47u: case 0x51u: case 0x52u: case 0x5Bu: case 0x5Cu: case 0x61u: case 0x62u:     // (eq, ne)
                                case 0x6Au: case 0x6Cu: case 0x71u: case 0x72u: case 0x73u:                                         // (i32 add, mul, and, or, xor)
                                case 0x7Cu: case 0x7Eu: case 0x83u: case 0x84u: case 0x85u:                                         // (i64's)
                                    return true;
                                default:
                                    return false;
                                }
                            };
                            auto const memory_key = [](std::uint16_t aOp, std::uint8_t aType, std::uint64_t aMemarg, std::uint64_t aOffset, value aAddress, std::uint64_t aEpoch)
                            {
                                return std::vector<std::uint64_t>{ aOp | (std::uint64_t{ aType } << 16u), aMemarg >> 32u, aOffset, aEpoch, aAddress };
                            };
                            struct frame { std::uint32_t block; std::size_t next; std::size_t mark; std::size_t knownMark; std::size_t accessMark; };
                            // (the memory accesses made (in the blocks being walked: those that dominate the block being numbered), with
                            // the epoch (of their class) that they were made in: an access at an address (its base, and its offset and
                            // width) is what the memory there is after a later store to the same base that does not overlap it (the 
                            // store's epoch is the access's again: see store_to); a load that a store of a constant covers is a constant)
                            struct access
                            {
                                std::uint64_t epoch;
                                std::size_t memoryClass;
                                value address;
                                std::uint64_t memory;   ///< (the memarg's high bits: its memory)
                                std::uint64_t offset;
                                std::uint32_t width;
                                std::uint16_t loadOp;   ///< (the load that reads what it accessed (its width): 0, none)
                                std::uint8_t type;
                                value data;             ///< (the value loaded or stored)
                                bool stored;
                                bool truncated = false; ///< (an i64's low 32 bits (i64.store32): i64.load32_u's are the value's, masked)
                            };
                            std::vector<access> accesses;
                            constexpr std::size_t MaxAccessScan = 48u;
                            // (the bytes of a value that are known: its bits, and how many of its low bytes they are (8: all))
                            auto const known_bytes = [&](value aValue, std::uint64_t& aBits) -> std::uint32_t
                            {
                                auto const& d = values[aValue];
                                if (d.constant())
                                {
                                    aBits = d.imm0;
                                    return 8u;
                                }
                                if (d.op == 0x84u && d.operands.size() == 2u)
                                {
                                    for (std::size_t n = 0u; n < 2u; ++n)
                                    {
                                        auto const& c = values[d.operands[n]];
                                        auto const& o = values[d.operands[1u - n]];
                                        if (c.constant() && o.op == 0x86u && o.operands.size() == 2u && values[o.operands[1]].constant())
                                        {
                                            auto const shift = values[o.operands[1]].imm0 & 63u;
                                            aBits = c.imm0;
                                            return static_cast<std::uint32_t>(shift / 8u);
                                        }
                                    }
                                }
                                return 0u;
                            };
                            auto const width_of = [](std::uint16_t aOp) -> std::uint32_t
                            {
                                switch (aOp)
                                {
                                case 0x28u: case 0x2Au: case 0x36u: case 0x38u: case 0x34u: case 0x35u: case 0x3Eu: return 4u;
                                case 0x29u: case 0x2Bu: case 0x37u: case 0x39u: return 8u;
                                case 0x2Cu: case 0x2Du: case 0x30u: case 0x31u: case 0x3Au: case 0x3Cu: return 1u;
                                case 0x2Eu: case 0x2Fu: case 0x32u: case 0x33u: case 0x3Bu: case 0x3Du: return 2u;
                                default: return 0u;
                                }
                            };
                            std::vector<frame> stack;
                            // (the conditions that branches that dominate a block decided (the block on its true or false edge): constants in it)
                            std::vector<std::int8_t> known(values.size(), -1);
                            std::vector<value> knownOrder;
                            value constants01[2] = { None, None };
                            auto const constant01 = [&](int aBit)
                            {
                                if (constants01[aBit] == None)
                                {
                                    instruction c;
                                    c.op = 0x41u;
                                    c.type = I32;
                                    c.imm0 = static_cast<std::uint64_t>(aBit);
                                    values.push_back(c);
                                    constants01[aBit] = static_cast<value>(values.size() - 1u);
                                }
                                return constants01[aBit];
                            };
                            auto const enter = [&](std::uint32_t aBlock)
                            {
                                auto const& b = blocks[aBlock];
                                epoch_set epoch;
                                if (b.predecessors.size() == 1u && idom[aBlock] == b.predecessors[0] && aBlock != order[0])
                                    epoch = exitEpoch[b.predecessors[0]];
                                else
                                {
                                    // (a join that every path from its dominator reaches without a store (of a class) or call (its predecessors 
                                    // end with its dominator's memory (of that class)): its dominator's memory (that class's))
                                    bool const dominated = aBlock != order[0] && !b.predecessors.empty() && idom[aBlock] != Undefined;
                                    for (std::size_t c = 0u; c < epoch.size(); ++c)
                                    {
                                        bool same = dominated;
                                        for (auto p : b.predecessors)
                                            if (!same || !exited[p] || exitEpoch[p][c] != exitEpoch[idom[aBlock]][c])
                                                same = false;
                                        epoch[c] = same ? exitEpoch[idom[aBlock]][c] : ++epochs;
                                    }
                                }
                                stack.push_back(frame{ aBlock, 0u, added.size(), knownOrder.size(), accesses.size() });
                                if (b.predecessors.size() == 1u && aBlock != order[0])
                                {
                                    auto const& from = blocks[b.predecessors[0]];
                                    if (!from.code.empty() && from.successors.size() == 2u && from.successors[0] != from.successors[1])
                                        if (auto const& t = values[from.code.back()]; t.op == Branch && !t.dead)
                                        {
                                            // (on the false edge it is 0; on the true edge not 0: 1 if it is a comparison's (0 or 1))
                                            auto const condition = replaced.resolved(t.operands[0]);
                                            bool const whenTrue = from.successors[0] == aBlock;
                                            bool const boolean = values[condition].op >= 0x45u && values[condition].op <= 0x66u;
                                            if (condition < known.size() && known[condition] < 0 && (!whenTrue || boolean) && !values[condition].constant())
                                            {
                                                known[condition] = whenTrue ? 1 : 0;
                                                knownOrder.push_back(condition);
                                            }
                                        }
                                }
                                for (auto v : b.code)
                                {
                                    auto& x = values[v];
                                    if (x.dead)
                                        continue;
                                    for (std::size_t n = 0u; n < x.operands.size(); ++n)
                                    {
                                        auto o = replaced.resolved(values[v].operands[n]);
                                        if (values[v].op != Phi && o < known.size() && known[o] >= 0)
                                            o = constant01(known[o]);
                                        values[v].operands[n] = o;
                                    }
                                    auto& i = values[v];
                                    if (i.store())
                                    {
                                        auto const st = values[v];   // (a copy: constants may be made, moving the values)
                                        // (its class's memory changes (and class 0's); a store of class 0: all classes')
                                        auto const c = class_of(st);
                                        auto const before = epoch[c];
                                        if (c == 0u)
                                        {
                                            auto const g = epoch[Globals];
                                            epoch.fill(++epochs);
                                            epoch[Globals] = g;
                                        }
                                        else
                                        {
                                            epoch[c] = ++epochs;
                                            epoch[0] = ++epochs;
                                        }
                                        // (the accesses of its class at its base that it does not overlap are as they were (another base may be the same
                                        // address: not known))
                                        auto const width = width_of(st.op);
                                        if (c != 0u && width != 0u)
                                        {
                                            std::size_t scanned = 0u;
                                            auto const count = accesses.size();
                                            for (auto a = count; a > 0u && scanned < MaxAccessScan; --a, ++scanned)
                                            {
                                                auto const e = accesses[a - 1u];
                                                if (e.memoryClass != c || e.epoch != before || e.address != st.operands[0] || e.memory != (st.imm0 >> 32u))
                                                    continue;
                                                if (e.offset < st.imm1 + width && st.imm1 < e.offset + e.width)
                                                {
                                                    // (a store of known bytes that it overlaps: those before it and after it, as stores of constants)
                                                    std::uint64_t bits = 0u;
                                                    auto const knownBytes = e.stored ? known_bytes(e.data, bits) : 0u;
                                                    auto const piece = [&](std::uint64_t aFirst, std::uint64_t aLast)
                                                    {
                                                        // ([aFirst, aLast): bytes of the access (relative to its offset))
                                                        if (aFirst >= aLast || aLast > knownBytes)
                                                            return;
                                                        auto pieceBits = bits >> (8u * aFirst);
                                                        auto const pieceWidth = static_cast<std::uint32_t>(aLast - aFirst);
                                                        if (pieceWidth < 8u)
                                                            pieceBits &= (std::uint64_t{ 1u } << (8u * pieceWidth)) - 1u;
                                                        instruction m;
                                                        m.op = 0x42u;
                                                        m.type = I64;
                                                        m.imm0 = pieceBits;
                                                        m.block = aBlock;
                                                        values.push_back(m);
                                                        accesses.push_back(access{ epoch[c], c, e.address, e.memory, e.offset + aFirst, pieceWidth, 0u, I64, 
                                                            static_cast<value>(values.size() - 1u), true });
                                                    };
                                                    if (knownBytes != 0u)
                                                    {
                                                        auto const storeFirst = st.imm1 > e.offset ? st.imm1 - e.offset : 0u;
                                                        auto const storeLast = std::min<std::uint64_t>(st.imm1 + width - e.offset, e.width);
                                                        piece(0u, std::min<std::uint64_t>(storeFirst, e.width));
                                                        piece(storeLast, e.width);
                                                    }
                                                    continue;
                                                }
                                                if (e.loadOp != 0u)
                                                {
                                                    auto kk = memory_key(e.loadOp, e.type, st.imm0, e.offset, e.address, epoch[c]);
                                                    if (table.try_emplace(kk, e.data).second)
                                                        added.push_back(std::move(kk));
                                                }
                                                auto moved = e;
                                                moved.epoch = epoch[c];
                                                accesses.push_back(moved);
                                            }
                                        }
                                        // (what a later load of it (of the same width) is)
                                        std::uint16_t load = 0u;
                                        switch (st.op)
                                        {
                                        case 0x36u: load = 0x28u; break;
                                        case 0x37u: load = 0x29u; break;
                                        case 0x38u: load = 0x2Au; break;
                                        case 0x39u: load = 0x2Bu; break;
                                        default: break;
                                        }
                                        if (load != 0u)
                                        {
                                            auto k = memory_key(load, values[st.operands[1]].type, st.imm0, st.imm1, st.operands[0], epoch[c]);
                                            if (table.try_emplace(k, st.operands[1]).second)
                                                added.push_back(std::move(k));
                                        }
                                        if (c != 0u && width != 0u)
                                            accesses.push_back(access{ epoch[c], c, st.operands[0], st.imm0 >> 32u, st.imm1, width, load, values[st.operands[1]].type, 
                                                st.operands[1], true, st.op == 0x3Eu });
                                        continue;
                                    }
                                    if (i.call())
                                    {
                                        epoch.fill(++epochs);
                                        continue;
                                    }
                                    if (i.op == 0x24u)
                                    {
                                        epoch[Globals] = ++epochs;
                                        continue;
                                    }
                                    std::vector<std::uint64_t> k;
                                    if (i.load())
                                        k = memory_key(i.op, i.type, i.imm0, i.imm1, i.operands[0], epoch[class_of(i)]);
                                    else if (i.op == 0x23u)
                                        k = { i.op, i.imm0, epoch[Globals] };
                                    else if ((i.op >= 0x45u && i.op <= 0xC4u) || i.op == 0x1Bu)
                                    {
                                        k = { i.op | (std::uint64_t{ i.type } << 16u) };
                                        if (commutative(i.op) && i.operands.size() == 2u)
                                        {
                                            k.push_back(std::min(i.operands[0], i.operands[1]));
                                            k.push_back(std::max(i.operands[0], i.operands[1]));
                                        }
                                        else
                                            for (auto o : i.operands)
                                                k.push_back(o);
                                    }
                                    else
                                        continue;
                                    bool const isLoad = i.load();
                                    auto const [where, inserted] = table.try_emplace(k, v);
                                    value data = v;
                                    if (inserted)
                                        added.push_back(std::move(k));
                                    else if (values[where->second].type == i.type)
                                    {
                                        i.dead = true;
                                        replaced.add(v, where->second);
                                        data = where->second;
                                    }
                                    if (!isLoad)
                                        continue;
                                    auto const loadClass = class_of(values[v]);
                                    auto const loadWidth = width_of(values[v].op);
                                    if (loadClass == 0u || loadWidth == 0u)
                                        continue;
                                    if (inserted && (values[v].imm0 & 0x100u) == 0u && (values[v].flags & Volatile) == 0u)
                                    {
                                        // (what a store (of this epoch, at the same base) that covers it stored: a constant's bits; an i64's
                                        // low 32 bits (i64.store32, read by i64.load32_u): the value, masked)
                                        std::size_t scanned = 0u;
                                        for (auto a = accesses.size(); a > 0u && scanned < MaxAccessScan; --a, ++scanned)
                                        {
                                            auto const e = accesses[a - 1u];
                                            auto const& x = values[v];
                                            if (e.memoryClass != loadClass || e.epoch != epoch[loadClass] || e.address != x.operands[0] || e.memory != (x.imm0 >> 32u))
                                                continue;
                                            if (!(e.offset <= x.imm1 && x.imm1 + loadWidth <= e.offset + e.width))
                                            {
                                                if (e.offset < x.imm1 + loadWidth && x.imm1 < e.offset + e.width)
                                                    break;  // (an overlap that does not cover it: what it reads is not known)
                                                continue;
                                            }
                                            if (!e.stored)
                                                continue;
                                            if (e.truncated && x.op == 0x35u && e.offset == x.imm1)
                                            {
                                                instruction m;
                                                m.op = 0x42u;
                                                m.type = I64;
                                                m.imm0 = 0xFFFFFFFFu;
                                                m.block = values[v].block;
                                                values.push_back(m);
                                                auto& y = values[v];
                                                y.op = 0x83u;
                                                y.flags = 0u;
                                                y.imm0 = 0u;
                                                y.imm1 = 0u;
                                                y.operands = { e.data, static_cast<value>(values.size() - 1u) };
                                                break;
                                            }
                                            std::uint64_t bits = 0u;
                                            auto const knownBytes = known_bytes(e.data, bits);
                                            if (x.imm1 + loadWidth > e.offset + knownBytes)
                                                break;
                                            // (little endian: the bytes at its offset)
                                            if (e.width < 8u)
                                                bits &= (std::uint64_t{ 1u } << (8u * e.width)) - 1u;
                                            bits >>= 8u * (x.imm1 - e.offset);
                                            if (loadWidth < 8u)
                                                bits &= (std::uint64_t{ 1u } << (8u * loadWidth)) - 1u;
                                            bool const signedLoad = x.op == 0x2Cu || x.op == 0x2Eu || x.op == 0x30u || x.op == 0x32u || x.op == 0x34u;
                                            if (signedLoad && loadWidth < 8u && ((bits >> (8u * loadWidth - 1u)) & 1u) != 0u)
                                                bits |= ~((std::uint64_t{ 1u } << (8u * loadWidth)) - 1u);
                                            auto& y = values[v];
                                            y.op = y.type == I32 ? 0x41u : y.type == I64 ? 0x42u : y.type == F32 ? 0x43u : 0x44u;
                                            if (y.type == I32 || y.type == F32)
                                                bits &= 0xFFFFFFFFu;
                                            y.imm0 = bits;
                                            y.imm1 = 0u;
                                            y.flags = 0u;
                                            y.operands.clear();
                                            break;
                                        }
                                    }
                                    {
                                        auto const& x = values[v];
                                        if (x.load())
                                            accesses.push_back(access{ epoch[loadClass], loadClass, x.operands[0], x.imm0 >> 32u, x.imm1, loadWidth, x.op, x.type, data, false });
                                    }
                                }
                                exitEpoch[aBlock] = epoch;
                                exited[aBlock] = true;
                            };
                            enter(order[0]);
                            while (!stack.empty())
                            {
                                auto& top = stack.back();
                                if (top.next < children[top.block].size())
                                {
                                    auto const child = children[top.block][top.next++];
                                    enter(child);
                                    continue;
                                }
                                for (auto k = added.size(); k > top.mark; --k)
                                    table.erase(added[k - 1u]);
                                added.resize(top.mark);
                                for (auto k = knownOrder.size(); k > top.knownMark; --k)
                                    known[knownOrder[k - 1u]] = -1;
                                knownOrder.resize(top.knownMark);
                                accesses.resize(top.accessMark);
                                stack.pop_back();
                            }
                            replaced.apply(aFunction);
                        }

                        // (the values' bounds (unsigned: each value is at most its bound): memory accesses whose addresses' bounds are within
                        // their (fixed) memories are not checked)
                        void bound_accesses(function& aFunction, std::function<std::uint64_t(std::uint32_t aMemory)> const& aAccessible)
                        {
                            constexpr std::uint64_t Unknown = ~std::uint64_t{};
                            auto& values = aFunction.values;
                            std::vector<std::uint64_t> bound(values.size(), Unknown);
                            std::vector<bool> known(values.size(), false);
                            auto const add = [](std::uint64_t aX, std::uint64_t aY) { return aX > Unknown - aY ? Unknown : aX + aY; };
                            auto const mask_of = [](std::uint64_t aX) { return aX == 0u ? 0u : Unknown >> std::countl_zero(aX); };
                            for (auto b : aFunction.order)
                                for (auto v : aFunction.blocks[b].code)
                                {
                                    auto& i = values[v];
                                    if (i.dead)
                                        continue;
                                    auto const width = i.type == I32 ? std::uint64_t{ 0xFFFFFFFFu } : Unknown;
                                    auto const x = [&](std::size_t aOperand) -> std::uint64_t
                                    {
                                        // (a constant (wherever it is: constants are where they are used): its value)
                                        auto const& o = values[i.operands[aOperand]];
                                        if (o.op == 0x41u)
                                            return o.imm0 & 0xFFFFFFFFu;
                                        if (o.op == 0x42u)
                                            return std::min(o.imm0, width);
                                        return known[i.operands[aOperand]] ? bound[i.operands[aOperand]] : width;
                                    };
                                    auto const constant_operand = [&](std::size_t aOperand) -> std::optional<std::uint64_t>
                                    {
                                        auto const& o = values[i.operands[aOperand]];
                                        if (o.constant())
                                            return o.imm0;
                                        return {};
                                    };
                                    std::uint64_t r = width;
                                    switch (i.op)
                                    {
                                    case 0x41u: r = i.imm0 & 0xFFFFFFFFu; break;
                                    case 0x42u: r = i.imm0; break;
                                    case 0x71u: case 0x83u: r = std::min(x(0u), x(1u)); break;                     // (and)
                                    case 0x72u: case 0x84u: case 0x73u: case 0x85u: r = mask_of(std::max(x(0u), x(1u))); break;  // (or, xor)
                                    case 0x6Au: case 0x7Cu: r = add(x(0u), x(1u)); break;                          // (add (if it does not wrap))
                                    case 0x74u: case 0x86u:                                                        // (shl by a constant)
                                        if (auto const k = constant_operand(1u))
                                        {
                                            auto const shift = *k & (i.type == I32 ? 31u : 63u);
                                            auto const y = x(0u);
                                            if (shift == 0u || y <= (width >> shift))
                                                r = y << shift;
                                        }
                                        break;
                                    case 0x76u: case 0x88u:                                                        // (shr_u)
                                        if (auto const k = constant_operand(1u))
                                            r = x(0u) >> (*k & (i.type == I32 ? 31u : 63u));
                                        else
                                            r = x(0u);
                                        break;
                                    case 0x70u: case 0x82u:                                                        // (rem_u)
                                        if (auto const k = constant_operand(1u); k && *k != 0u)
                                            r = std::min(x(0u), *k - 1u);
                                        else
                                            r = x(0u);
                                        break;
                                    case 0xADu: r = x(0u) & 0xFFFFFFFFu; break;                                    // (i64.extend_i32_u)
                                    case 0xA7u: r = std::min<std::uint64_t>(x(0u), 0xFFFFFFFFu); break;           // (i32.wrap_i64)
                                    case 0x2Du: case 0x31u: r = 0xFFu; break;                                      // (load8_u)
                                    case 0x2Fu: case 0x33u: r = 0xFFFFu; break;                                    // (load16_u)
                                    case 0x35u: r = 0xFFFFFFFFu; break;                                            // (i64.load32_u)
                                    case 0x29u:                                                                    // (i64.load: an offset (see MemargOffset))
                                        if ((i.imm0 & optimizer::MemargOffset) != 0u && aAccessible)
                                            if (auto const size = aAccessible(static_cast<std::uint32_t>(i.imm0 >> 32u)); size >= optimizer::MemargOffsetMargin)
                                                r = size - optimizer::MemargOffsetMargin;
                                        break;
                                    case 0x45u: case 0x46u: case 0x47u: case 0x48u: case 0x49u: case 0x4Au: case 0x4Bu: case 0x4Cu: case 0x4Du:
                                    case 0x4Eu: case 0x4Fu: case 0x50u: case 0x51u: case 0x52u: case 0x53u: case 0x54u: case 0x55u: case 0x56u:
                                    case 0x57u: case 0x58u: case 0x59u: case 0x5Au: case 0x5Bu: case 0x5Cu: case 0x5Du: case 0x5Eu: case 0x5Fu:
                                    case 0x60u: case 0x61u: case 0x62u: case 0x63u: case 0x64u: case 0x65u: case 0x66u:
                                        r = 1u; break;                                                             // (comparisons)
                                    case 0x1Bu: r = std::max(x(0u), x(1u)); break;                                 // (select)
                                    case Phi:
                                    {
                                        // (operands not yet bounded (back edges): unknown)
                                        r = 0u;
                                        for (auto o : i.operands)
                                            r = std::max(r, values[o].op == 0x41u ? (values[o].imm0 & 0xFFFFFFFFu) : values[o].op == 0x42u ? values[o].imm0 : known[o] ? bound[o] : width);
                                        break;
                                    }
                                    default:
                                        break;
                                    }
                                    bound[v] = std::min(r, width);
                                    known[v] = true;
                                    if ((i.load() || i.store()) && aAccessible)
                                    {
                                        auto const memory = static_cast<std::uint32_t>(i.imm0 >> 32u);
                                        if (memory == 0u)
                                            continue;
                                        auto const size = aAccessible(memory);
                                        static constexpr std::uint8_t sWidths[] = { 4, 8, 4, 8, 1, 1, 2, 2, 1, 1, 2, 2, 4, 4, 4, 8, 4, 8, 1, 2, 1, 2, 4 };
                                        auto const last = add(add(x(0u), i.imm1), sWidths[i.op - 0x28u]);
                                        if (size != 0u && last <= size)
                                            i.flags |= Unchecked;
                                    }
                                }
                        }
                    }

                    function build(module_description const& aModule, std::uint32_t aFunction, std::vector<std::uint64_t> const& aTokens,
                        std::vector<std::uint8_t> const& aLocals, std::vector<std::size_t> const& aOrigins, std::vector<optimizer::deopt> const& aDeopts)
                    {
                        builder b{ aModule, aFunction, aTokens, aLocals, aOrigins, aDeopts };
                        auto f = b.build();
                        finish(f);
                        return f;
                    }

                    void finish(function& aFunction)
                    {
                        // (the blocks that can be reached, in reverse postorder (a loop's blocks follow its header))
                        auto& blocks = aFunction.blocks;
                        for (auto& b : blocks)
                            b.reachable = false;
                        std::vector<std::uint32_t> postorder;
                        std::vector<std::pair<std::uint32_t, std::size_t>> stack{ { 0u, 0u } };
                        blocks[0].reachable = true;
                        while (!stack.empty())
                        {
                            auto& [b, next] = stack.back();
                            if (next < blocks[b].successors.size())
                            {
                                auto const s = blocks[b].successors[next++];
                                if (!blocks[s].reachable)
                                {
                                    blocks[s].reachable = true;
                                    stack.emplace_back(s, 0u);
                                }
                                continue;
                            }
                            postorder.push_back(b);
                            stack.pop_back();
                        }
                        aFunction.order.assign(postorder.rbegin(), postorder.rend());
                        // (predecessors that cannot be reached: not predecessors (nor their phis' operands))
                        for (std::uint32_t b = 0u; b < blocks.size(); ++b)
                        {
                            if (!blocks[b].reachable)
                                continue;
                            auto& preds = blocks[b].predecessors;
                            for (std::size_t p = preds.size(); p-- > 0u;)
                                if (!blocks[preds[p]].reachable)
                                {
                                    preds.erase(preds.begin() + static_cast<std::ptrdiff_t>(p));
                                    for (auto v : blocks[b].code)
                                    {
                                        auto& i = aFunction.values[v];
                                        if (i.op != Phi)
                                            break;
                                        if (p < i.operands.size())
                                            i.operands.erase(i.operands.begin() + static_cast<std::ptrdiff_t>(p));
                                    }
                                }
                        }
                        // (loop depths: a block within a loop is one that can reach its header's back edge from the header)
                        std::vector<std::uint32_t> position(blocks.size(), 0u);
                        for (std::uint32_t i = 0u; i < aFunction.order.size(); ++i)
                            position[aFunction.order[i]] = i;
                        // (loop headers: the targets of back edges (from blocks that follow them in the order), as the code is now (e.g.
                        // after jumps are threaded: a loop's first block may no longer be its header))
                        for (auto& b : blocks)
                        {
                            b.loopDepth = 0u;
                            b.loopHeader = false;
                        }
                        for (auto h : aFunction.order)
                            for (auto p : blocks[h].predecessors)
                                if (blocks[p].reachable && position[p] >= position[h])
                                    blocks[h].loopHeader = true;
                        for (auto h : aFunction.order)
                        {
                            if (!blocks[h].loopHeader)
                                continue;
                            // (the loop's blocks: backwards from its back edges' sources to it)
                            std::vector<std::uint32_t> work;
                            std::vector<bool> in(blocks.size(), false);
                            for (auto p : blocks[h].predecessors)
                                if (position[p] >= position[h])
                                    work.push_back(p);
                            in[h] = true;
                            while (!work.empty())
                            {
                                auto const x = work.back();
                                work.pop_back();
                                if (in[x] || !blocks[x].reachable)
                                    continue;
                                in[x] = true;
                                for (auto p : blocks[x].predecessors)
                                    work.push_back(p);
                            }
                            for (std::uint32_t x = 0u; x < blocks.size(); ++x)
                                if (in[x])
                                    ++blocks[x].loopDepth;
                        }
                        count_uses(aFunction);
                    }

                    namespace
                    {
                        // (blocks that only jump (no phis, no code but constants): their predecessors jump to their successor instead)
                        bool thread_jumps(function& aFunction)
                        {
                            auto& blocks = aFunction.blocks;
                            auto& values = aFunction.values;
                            bool changed = false;
                            for (std::uint32_t e = 1u; e < blocks.size(); ++e)
                            {
                                auto& E = blocks[e];
                                if (!E.reachable || E.successors.size() != 1u || E.predecessors.empty())
                                    continue;
                                auto const target = E.successors[0];
                                if (target == e)
                                    continue;
                                bool only = true;
                                value jump = None;
                                for (auto v : E.code)
                                {
                                    auto const& i = values[v];
                                    if (i.dead || i.constant())
                                        continue;
                                    if (i.op == Jump)
                                        jump = v;
                                    else
                                    {
                                        only = false;
                                        break;
                                    }
                                }
                                if (!only || jump == None)
                                    continue;
                                auto& S = blocks[target];
                                bool phis = false;
                                for (auto v : S.code)
                                {
                                    if (values[v].op != Phi)
                                        break;
                                    if (!values[v].dead)
                                        phis = true;
                                }
                                // (a predecessor that is already the target's: not (the target's phis would need it twice))
                                bool clash = false;
                                for (auto p : E.predecessors)
                                    if (p == target || std::find(S.predecessors.begin(), S.predecessors.end(), p) != S.predecessors.end())
                                        clash = true;
                                if (clash)
                                {
                                    // (E's only predecessor branches to E and to the target, whose phis have the same operands for both: it 
                                    // jumps to the target (the branch's condition no longer used))
                                    if (E.predecessors.size() != 1u)
                                        continue;
                                    auto const p = E.predecessors[0];
                                    auto& P = blocks[p];
                                    if (P.successors.size() != 2u || P.code.empty() || values[P.code.back()].op != Branch ||
                                        !((P.successors[0] == e && P.successors[1] == target) || (P.successors[1] == e && P.successors[0] == target)))
                                        continue;
                                    auto const ie = static_cast<std::size_t>(std::find(S.predecessors.begin(), S.predecessors.end(), e) - S.predecessors.begin());
                                    auto const ip = static_cast<std::size_t>(std::find(S.predecessors.begin(), S.predecessors.end(), p) - S.predecessors.begin());
                                    if (ie >= S.predecessors.size() || ip >= S.predecessors.size())
                                        continue;
                                    bool same = true;
                                    for (auto v : S.code)
                                    {
                                        auto const& i = values[v];
                                        if (i.op != Phi)
                                            break;
                                        if (!i.dead && (ie >= i.operands.size() || ip >= i.operands.size() || i.operands[ie] != i.operands[ip]))
                                            same = false;
                                    }
                                    if (!same)
                                        continue;
                                    S.predecessors.erase(S.predecessors.begin() + static_cast<std::ptrdiff_t>(ie));
                                    for (auto v : S.code)
                                    {
                                        auto& i = values[v];
                                        if (i.op != Phi)
                                            break;
                                        if (ie < i.operands.size())
                                            i.operands.erase(i.operands.begin() + static_cast<std::ptrdiff_t>(ie));
                                    }
                                    auto& branch = values[P.code.back()];
                                    branch.op = Jump;
                                    branch.operands.clear();
                                    P.successors.assign(1u, target);
                                    E.predecessors.clear();
                                    E.successors.clear();
                                    E.reachable = false;
                                    values[jump].dead = true;
                                    changed = true;
                                    continue;
                                }
                                auto const index = static_cast<std::size_t>(std::find(S.predecessors.begin(), S.predecessors.end(), e) - S.predecessors.begin());
                                if (index >= S.predecessors.size())
                                    continue;
                                auto const preds = E.predecessors;
                                for (auto p : preds)
                                    for (auto& s : blocks[p].successors)
                                        if (s == e)
                                            s = target;
                                // (the target's predecessors: E's instead of E (its phis' operand for E for each of them))
                                S.predecessors.erase(S.predecessors.begin() + static_cast<std::ptrdiff_t>(index));
                                S.predecessors.insert(S.predecessors.begin() + static_cast<std::ptrdiff_t>(index), preds.begin(), preds.end());
                                for (auto v : S.code)
                                {
                                    auto& i = values[v];
                                    if (i.op != Phi)
                                        break;
                                    if (index >= i.operands.size())
                                        continue;
                                    auto const operand = i.operands[index];
                                    i.operands.erase(i.operands.begin() + static_cast<std::ptrdiff_t>(index));
                                    i.operands.insert(i.operands.begin() + static_cast<std::ptrdiff_t>(index), preds.size(), operand);
                                }
                                E.predecessors.clear();
                                E.successors.clear();
                                E.reachable = false;
                                values[jump].dead = true;
                                changed = true;
                            }
                            return changed;
                        }
                    }

                    namespace
                    {
                        // (a phi whose operands are the same computation (the same operations of the same operands: e.g. a property's load
                        // after each of two shape checks), each its predecessor's (and used only there): one computation, in the phi's block
                        // (the predecessors' are removed); loads (that are after their blocks' last stores and calls) too)
                        bool sink_equal_operands(function& aFunction)
                        {
                            auto& blocks = aFunction.blocks;
                            count_uses(aFunction);
                            std::vector<std::uint32_t> position(aFunction.values.size(), 0u);
                            std::vector<std::uint32_t> lastEffect(blocks.size(), 0u);  // (1 + the position of its last store or call; 0: none)
                            for (std::uint32_t b = 0u; b < blocks.size(); ++b)
                            {
                                if (!blocks[b].reachable)
                                    continue;
                                auto const& code = blocks[b].code;
                                for (std::uint32_t n = 0u; n < code.size(); ++n)
                                {
                                    auto const& i = aFunction.values[code[n]];
                                    position[code[n]] = n;
                                    if (!i.dead && !i.terminator() && !pure(i) && !i.load() && i.op != 0x23u)
                                        lastEffect[b] = n + 1u;
                                }
                            }
                            auto const sinkable = [&](value aValue, std::uint32_t aBlock)
                            {
                                auto const& i = aFunction.values[aValue];
                                if (i.dead || i.block != aBlock || i.op == Phi || i.op == Param || i.constant() || i.terminator() || i.call() || i.store())
                                    return false;
                                if (aValue >= aFunction.uses.size() || aValue >= position.size() || aFunction.uses[aValue] != 1u)
                                    return false;
                                if (!pure(i) && !i.load() && i.op != 0x23u)
                                    return false;
                                return position[aValue] >= lastEffect[aBlock];
                            };
                            std::function<bool(value, value, std::uint32_t, std::uint32_t, int)> equal = [&](value aX, value aY, std::uint32_t aBX, std::uint32_t aBY, int aDepth) -> bool
                            {
                                if (aX == aY)
                                    return aFunction.values[aX].block != aBX && aFunction.values[aX].block != aBY || aFunction.values[aX].constant();
                                if (aDepth == 0 || !sinkable(aX, aBX) || !sinkable(aY, aBY))
                                    return false;
                                auto const& x = aFunction.values[aX];
                                auto const& y = aFunction.values[aY];
                                if (x.op != y.op || x.type != y.type || x.flags != y.flags || x.imm0 != y.imm0 || x.imm1 != y.imm1 || x.operands.size() != y.operands.size())
                                    return false;
                                for (std::size_t o = 0u; o < x.operands.size(); ++o)
                                    if (!equal(x.operands[o], y.operands[o], aBX, aBY, aDepth - 1))
                                        return false;
                                return true;
                            };
                            bool changed = false;
                            replacements replaced;
                            auto const order = aFunction.order;
                            for (auto j : order)
                            {
                                if (blocks[j].predecessors.size() < 2u || blocks[j].loopHeader)
                                    continue;
                                for (std::size_t n = 0u; n < blocks[j].code.size(); ++n)
                                {
                                    auto const p = blocks[j].code[n];
                                    if (aFunction.values[p].op != Phi)
                                        break;
                                    if (aFunction.values[p].dead || aFunction.values[p].operands.size() != blocks[j].predecessors.size())
                                        continue;
                                    auto const operands = aFunction.values[p].operands;
                                    auto const preds = blocks[j].predecessors;
                                    // (the predecessors whose operands are the first sinkable one's computation)
                                    std::vector<std::size_t> chosen;
                                    for (std::size_t k0 = 0u; k0 < operands.size() && chosen.size() < 2u; ++k0)
                                    {
                                        chosen.clear();
                                        if (!sinkable(operands[k0], preds[k0]) || std::count(preds.begin(), preds.end(), preds[k0]) != 1)
                                            continue;
                                        chosen.push_back(k0);
                                        for (std::size_t k = k0 + 1u; k < operands.size(); ++k)
                                            if (std::count(preds.begin(), preds.end(), preds[k]) == 1 && equal(operands[k0], operands[k], preds[k0], preds[k], 6))
                                                chosen.push_back(k);
                                    }
                                    if (chosen.size() < 2u)
                                        continue;
                                    auto const k0 = chosen[0];
                                    bool const all = chosen.size() == preds.size();
                                    // (the block that the computation is in: the phi's, or (some of its predecessors') a new one between them
                                    // and it (its phis: the phi's block's phis' operands for them))
                                    std::uint32_t target = j;
                                    std::vector<value> inserted;
                                    if (!all)
                                    {
                                        target = static_cast<std::uint32_t>(blocks.size());
                                        blocks.emplace_back();
                                        auto& N = blocks[target];
                                        N.reachable = true;
                                        N.loopDepth = blocks[j].loopDepth;
                                        for (auto k : chosen)
                                        {
                                            N.predecessors.push_back(preds[k]);
                                            for (auto& succ : blocks[preds[k]].successors)
                                                if (succ == j)
                                                {
                                                    succ = target;
                                                    break;
                                                }
                                        }
                                        N.successors.push_back(j);
                                    }
                                    std::function<value(value)> copy = [&](value aValue) -> value
                                    {
                                        if (aFunction.values[aValue].block != preds[k0] || aFunction.values[aValue].constant())
                                            return aValue;
                                        auto i = aFunction.values[aValue];
                                        for (auto& o : i.operands)
                                            o = copy(o);
                                        i.block = target;
                                        aFunction.values.push_back(i);
                                        auto const result = static_cast<value>(aFunction.values.size() - 1u);
                                        inserted.push_back(result);
                                        return result;
                                    };
                                    auto const root = copy(operands[k0]);
                                    std::function<void(value, std::uint32_t)> remove = [&](value aValue, std::uint32_t aBlock)
                                    {
                                        auto& i = aFunction.values[aValue];
                                        if (i.block != aBlock || i.constant() || i.dead)
                                            return;
                                        i.dead = true;
                                        auto const ops = i.operands;
                                        for (auto o : ops)
                                            remove(o, aBlock);
                                    };
                                    for (auto k : chosen)
                                        remove(operands[k], preds[k]);
                                    if (all)
                                    {
                                        aFunction.values[p].dead = true;
                                        replaced.add(p, root);
                                        std::size_t phis = 0u;
                                        while (phis < blocks[j].code.size() && aFunction.values[blocks[j].code[phis]].op == Phi)
                                            ++phis;
                                        auto& code = blocks[j].code;
                                        code.insert(code.begin() + static_cast<std::ptrdiff_t>(phis), inserted.begin(), inserted.end());
                                    }
                                    else
                                    {
                                        // (the phi's block's phis: the new block's operand (a phi of the new block if the chosen operands 
                                        // differ; the computation for the phi) instead of the chosen predecessors')
                                        std::vector<value> newPhis;
                                        std::vector<bool> isChosen(preds.size(), false);
                                        for (auto k : chosen)
                                            isChosen[k] = true;
                                        for (auto w : blocks[j].code)
                                        {
                                            if (aFunction.values[w].op != Phi)
                                                break;
                                            auto const ops = aFunction.values[w].operands;
                                            value incoming = None;
                                            if (w == p)
                                                incoming = root;
                                            else if (ops.size() == preds.size())
                                            {
                                                bool same = true;
                                                for (auto k : chosen)
                                                    if (ops[k] != ops[k0])
                                                        same = false;
                                                if (same)
                                                    incoming = ops[k0];
                                                else
                                                {
                                                    instruction phi;
                                                    phi.op = Phi;
                                                    phi.type = aFunction.values[w].type;
                                                    phi.block = target;
                                                    phi.dead = aFunction.values[w].dead;
                                                    for (auto k : chosen)
                                                        phi.operands.push_back(ops[k]);
                                                    aFunction.values.push_back(phi);
                                                    incoming = static_cast<value>(aFunction.values.size() - 1u);
                                                    newPhis.push_back(incoming);
                                                }
                                            }
                                            std::vector<value> kept;
                                            for (std::size_t k = 0u; k < ops.size(); ++k)
                                                if (k >= isChosen.size() || !isChosen[k])
                                                    kept.push_back(ops[k]);
                                            kept.push_back(incoming);
                                            aFunction.values[w].operands = std::move(kept);
                                        }
                                        std::vector<std::uint32_t> keptPreds;
                                        for (std::size_t k = 0u; k < preds.size(); ++k)
                                            if (!isChosen[k])
                                                keptPreds.push_back(preds[k]);
                                        keptPreds.push_back(target);
                                        blocks[j].predecessors = std::move(keptPreds);
                                        instruction jump;
                                        jump.op = Jump;
                                        jump.type = Void;
                                        jump.block = target;
                                        aFunction.values.push_back(jump);
                                        auto& code = blocks[target].code;
                                        code = newPhis;
                                        code.insert(code.end(), inserted.begin(), inserted.end());
                                        code.push_back(static_cast<value>(aFunction.values.size() - 1u));
                                        lastEffect.resize(blocks.size(), 0u);
                                    }
                                    position.resize(aFunction.values.size(), 0u);
                                    changed = true;
                                    // (the phi's block's operands have changed: its phis again)
                                    n = static_cast<std::size_t>(-1);
                                    if (blocks[j].predecessors.size() < 2u)
                                        break;
                                }
                            }
                            replaced.apply(aFunction);
                            return changed;
                        }

                        // (a block that jumps to a block that has no other predecessor: the two are one (the second's code (its phis are 
                        // trivial: their operands) follows the first's))
                        bool merge_blocks(function& aFunction)
                        {
                            auto& blocks = aFunction.blocks;
                            auto& values = aFunction.values;
                            bool changed = false;
                            replacements replaced;
                            for (auto x : aFunction.order)
                            {
                                for (;;)
                                {
                                    auto& X = blocks[x];
                                    if (!X.reachable || X.successors.size() != 1u || X.code.empty())
                                        break;
                                    auto const jump = X.code.back();
                                    if (values[jump].op != Jump || values[jump].dead)
                                        break;
                                    auto const y = X.successors[0];
                                    if (y == x || y == 0u)
                                        break;
                                    auto& Y = blocks[y];
                                    if (!Y.reachable || Y.predecessors.size() != 1u || Y.predecessors[0] != x || Y.loopHeader)
                                        break;
                                    values[jump].dead = true;
                                    X.code.pop_back();
                                    for (auto v : Y.code)
                                    {
                                        auto& i = values[v];
                                        if (i.op == Phi)
                                        {
                                            if (!i.dead && !i.operands.empty())
                                                replaced.add(v, i.operands[0]);
                                            i.dead = true;
                                            continue;
                                        }
                                        i.block = x;
                                        X.code.push_back(v);
                                    }
                                    Y.code.clear();
                                    X.successors = Y.successors;
                                    for (auto s : Y.successors)
                                        for (auto& p : blocks[s].predecessors)
                                            if (p == y)
                                                p = x;
                                    Y.successors.clear();
                                    Y.predecessors.clear();
                                    Y.reachable = false;
                                    changed = true;
                                }
                            }
                            replaced.apply(aFunction);
                            return changed;
                        }

                        // (a block (not a loop's header) that has only phis (and constants) and a branch on one of them: a predecessor whose
                        // operand of it is a constant goes to the successor that the constant chooses instead (its phis used only by the 
                        // branch and its successors' phis: their operands for the predecessor are the phis' operands for it))
                        bool thread_branches(function& aFunction)
                        {
                            auto& blocks = aFunction.blocks;
                            auto& values = aFunction.values;
                            count_uses(aFunction);
                            bool changed = false;
                            for (std::uint32_t b = 1u; b < blocks.size(); ++b)
                            {
                                auto& B = blocks[b];
                                if (!B.reachable || B.loopHeader || B.successors.size() != 2u || B.successors[0] == B.successors[1] || B.code.empty())
                                    continue;
                                auto const br = B.code.back();
                                if (values[br].op != Branch || values[br].dead || values[br].operands.size() != 1u)
                                    continue;
                                auto const cond = values[br].operands[0];
                                if (values[cond].op != Phi || values[cond].block != b || values[cond].dead)
                                    continue;
                                if (B.successors[0] == b || B.successors[1] == b)
                                    continue;
                                bool ok = true;
                                for (auto v : B.code)
                                {
                                    auto const& i = values[v];
                                    if (i.dead || i.op == Phi || i.constant() || v == br)
                                        continue;
                                    ok = false;
                                    break;
                                }
                                if (!ok)
                                    continue;
                                // (its phis' uses: the branch's and its successors' phis' (their operands for it))
                                std::vector<std::size_t> indices;
                                for (auto t : B.successors)
                                {
                                    auto const& T = blocks[t];
                                    auto const at = std::find(T.predecessors.begin(), T.predecessors.end(), b);
                                    if (at == T.predecessors.end() || std::count(T.predecessors.begin(), T.predecessors.end(), b) != 1)
                                    {
                                        ok = false;
                                        break;
                                    }
                                    indices.push_back(static_cast<std::size_t>(at - T.predecessors.begin()));
                                }
                                if (!ok)
                                    continue;
                                for (auto v : B.code)
                                {
                                    auto const& i = values[v];
                                    if (i.dead || i.op != Phi)
                                        continue;
                                    std::uint32_t expected = v == cond ? 1u : 0u;
                                    for (std::size_t s = 0u; s < 2u; ++s)
                                        for (auto w : blocks[B.successors[s]].code)
                                        {
                                            auto const& j = values[w];
                                            if (j.op != Phi)
                                                break;
                                            if (!j.dead && indices[s] < j.operands.size() && j.operands[indices[s]] == v)
                                                ++expected;
                                        }
                                    if (aFunction.uses[v] != expected)
                                    {
                                        ok = false;
                                        break;
                                    }
                                }
                                if (!ok)
                                    continue;
                                for (std::size_t k = 0u; k < B.predecessors.size();)
                                {
                                    auto const& c = values[values[cond].operands[k]];
                                    auto const p = B.predecessors[k];
                                    if (!c.constant() || std::count(B.predecessors.begin(), B.predecessors.end(), p) != 1)
                                    {
                                        ++k;
                                        continue;
                                    }
                                    auto const s = (c.imm0 & 0xFFFFFFFFu) != 0u ? 0u : 1u;
                                    auto const t = B.successors[s];
                                    auto& T = blocks[t];
                                    if (p == t || std::find(T.predecessors.begin(), T.predecessors.end(), p) != T.predecessors.end())
                                    {
                                        ++k;
                                        continue;
                                    }
                                    // (the target's phis: an operand for the predecessor (its operand for B, or a phi of B's operand for it))
                                    T.predecessors.push_back(p);
                                    for (auto w : T.code)
                                    {
                                        auto& j = values[w];
                                        if (j.op != Phi)
                                            break;
                                        auto x = indices[s] < j.operands.size() ? j.operands[indices[s]] : None;
                                        if (x != None && values[x].op == Phi && values[x].block == b)
                                            x = values[x].operands[k];
                                        j.operands.push_back(x);
                                    }
                                    for (auto& succ : blocks[p].successors)
                                        if (succ == b)
                                        {
                                            succ = t;
                                            break;
                                        }
                                    B.predecessors.erase(B.predecessors.begin() + static_cast<std::ptrdiff_t>(k));
                                    for (auto w : B.code)
                                    {
                                        auto& j = values[w];
                                        if (j.op != Phi)
                                            break;
                                        if (k < j.operands.size())
                                            j.operands.erase(j.operands.begin() + static_cast<std::ptrdiff_t>(k));
                                    }
                                    changed = true;
                                }
                                if (B.predecessors.empty())
                                {
                                    // (unreachable now)
                                    for (std::size_t s = B.successors.size(); s-- > 0u;)
                                        remove_edge(aFunction, b, s);
                                    B.reachable = false;
                                    for (auto v : B.code)
                                        values[v].dead = true;
                                }
                            }
                            return changed;
                        }

                        // (a branch on a comparison that is not just before it (or that has other uses): a copy of the comparison just before
                        // it (so that the code generator fuses them, rather than making its value (0 or 1) and testing that))
                        void rematerialize_conditions(function& aFunction)
                        {
                            auto& values = aFunction.values;
                            auto const comparison = [](std::uint16_t aOp) { return aOp >= 0x45u && aOp <= 0x66u; };
                            for (auto b : aFunction.order)
                            {
                                auto& code = aFunction.blocks[b].code;
                                if (code.empty())
                                    continue;
                                auto const t = code.back();
                                if (values[t].op != Branch || values[t].dead || values[t].operands.empty())
                                    continue;
                                auto const condition = values[t].operands[0];
                                auto const& c = values[condition];
                                if (c.dead || !comparison(c.op))
                                    continue;
                                // (the last instruction before the branch that has code)
                                value previous = None;
                                for (auto k = code.size() - 1u; k-- > 0u;)
                                    if (!values[code[k]].dead && !values[code[k]].constant())
                                    {
                                        previous = code[k];
                                        break;
                                    }
                                if (previous == condition && aFunction.uses[condition] == 1u)
                                    continue;
                                instruction copy = c;
                                copy.block = b;
                                auto const v = static_cast<value>(values.size());
                                values.push_back(std::move(copy));
                                code.insert(code.end() - 1, v);
                                values[t].operands[0] = v;
                            }
                        }
                    }

                    void optimize(function& aFunction, module_description const&, std::function<std::uint64_t(std::uint32_t aMemory)> const& aAccessible)
                    {
                        for (int pass = 0; pass < 4; ++pass)
                        {
                            bool changed = fold_constants(aFunction);
                            finish(aFunction);
                            changed = simplify(aFunction) || changed;
                            if (!changed)
                                break;
                        }
                        number_values(aFunction);
                        for (int pass = 0; pass < 2; ++pass)
                        {
                            bool const changed = fold_constants(aFunction);
                            finish(aFunction);
                            if (!changed)
                                break;
                        }
                        if (merge_blocks(aFunction))
                        {
                            finish(aFunction);
                            eliminate_dead_code(aFunction);
                        }
                        if (thread_jumps(aFunction))
                        {
                            finish(aFunction);
                            eliminate_dead_code(aFunction);
                        }
                        if (merge_blocks(aFunction))
                            finish(aFunction);
                        if (sink_equal_operands(aFunction))
                        {
                            finish(aFunction);
                            eliminate_dead_code(aFunction);
                            if (thread_jumps(aFunction))
                                finish(aFunction);
                            eliminate_dead_code(aFunction);
                            number_values(aFunction);
                            for (int pass = 0; pass < 2; ++pass)
                            {
                                bool const changed = fold_constants(aFunction);
                                finish(aFunction);
                                if (!changed)
                                    break;
                            }
                        }
                        bound_accesses(aFunction, aAccessible);
                        eliminate_dead_code(aFunction);
                        if (thread_jumps(aFunction))
                        {
                            finish(aFunction);
                            eliminate_dead_code(aFunction);
                        }
                        for (int round = 0; round < 4; ++round)
                        {
                            bool const threaded = thread_branches(aFunction);
                            if (threaded)
                                finish(aFunction);
                            for (int pass = 0; pass < 2; ++pass)
                            {
                                bool changed = fold_constants(aFunction);
                                finish(aFunction);
                                changed = simplify(aFunction) || changed;
                                if (!changed)
                                    break;
                            }
                            bool const merged = merge_blocks(aFunction);
                            if (merged)
                                finish(aFunction);
                            eliminate_dead_code(aFunction);
                            if (thread_jumps(aFunction))
                            {
                                finish(aFunction);
                                eliminate_dead_code(aFunction);
                            }
                            if (!threaded && !merged)
                                break;
                        }
                        count_uses(aFunction);
                        rematerialize_conditions(aFunction);
                        eliminate_dead_code(aFunction);
                    }
                }
            }
        }
    }
}
