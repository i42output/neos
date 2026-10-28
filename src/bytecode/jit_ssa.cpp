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
#include <limits>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <memory>
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
                                bool osrThen = false;                   ///< (an if whose then arm has the entry at a loop: its else arm is not reached)
                                std::size_t height = 0u;
                            };
                            // (an entry at a loop: a construct that has the loop (its position), and whether the loop is in its else arm)
                            struct osr_construct
                            {
                                std::size_t position = 0u;
                                bool inElse = false;
                            };
                        public:
                            builder(module_description const& aModule, std::uint32_t aFunction, std::vector<std::uint64_t> const& aTokens,
                                std::vector<std::uint8_t> const& aLocals, std::vector<std::size_t> const& aOrigins, std::vector<optimizer::deopt> const& aDeopts,
                                osr_entry const* aOsr) :
                                iModule{ aModule }, iTokens{ aTokens }, iOrigins{ aOrigins }, iDeopts{ aDeopts }
                            {
                                auto const& f = aModule.functions[aFunction];
                                iFunction.index = aFunction;
                                iFunction.parameters = *f.parameters;
                                iFunction.results = *f.results;
                                iVariableTypes = iFunction.parameters;
                                iVariableTypes.insert(iVariableTypes.end(), aLocals.begin(), aLocals.end());
                                iLocals = iVariableTypes.size();
                                // (about a value for each token, and the phis: room for them (the values are not copied as they are added))
                                iFunction.values.reserve(aTokens.size() + aTokens.size() / 2u);
                                iDefinitions.reserve(aTokens.size());
                                if (aOsr != nullptr)
                                {
                                    // (the entry's parameters: the first variables (the function's parameters, then its own locals))
                                    if (aOsr->parameters.size() > iVariableTypes.size() ||
                                        !std::equal(aOsr->parameters.begin(), aOsr->parameters.end(), iVariableTypes.begin()))
                                        throw unsupported{ "osr entry" };
                                    iFunction.parameters = aOsr->parameters;
                                    osr_path(aOsr->loop);
                                    iSeeking = true;
                                }
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
                            value add(std::uint16_t aOp, std::uint8_t aType, operand_list aOperands, std::uint64_t aImm0 = 0u, std::uint64_t aImm1 = 0u)
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
                            // (one value for each constant (a constant is where it is used))
                            value constant(std::uint16_t aOp, std::uint8_t aType, std::uint64_t aBits)
                            {
                                auto& same = aBits == flat_map64<value>::Empty ? iAllOnes[aOp - 0x41u] : iConstants[aOp - 0x41u][aBits];
                                if (same == value{})
                                    same = add(aOp, aType, {}, aBits) + 1u;
                                return same - 1u;
                            }
                            value zero(std::uint8_t aType)
                            {
                                // (a constant: e.g. the locals' values at the entry, and deopts' locals that are not live)
                                switch (aType)
                                {
                                case I32: return constant(0x41u, I32, 0u);
                                case I64: return constant(0x42u, I64, 0u);
                                case F32: return constant(0x43u, F32, 0u);
                                default: return constant(0x44u, F64, 0u);
                                }
                            }
                            void link(std::uint32_t aFrom, std::uint32_t aTo)
                            {
                                iFunction.blocks[aFrom].successors.push_back(aTo);
                                iFunction.blocks[aTo].predecessors.push_back(aFrom);
                            }
                            void terminate(std::uint16_t aOp, operand_list aOperands, std::vector<std::uint32_t> const& aSuccessors, std::uint64_t aImm0 = 0u)
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
                                if (auto const* d = iDefinitions.find(key(aVariable, aBlock)))
                                    return forwarded(*d);
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
                                else if (!b.loopHeader)
                                {
                                    // (a join (not a loop's header: none of its predecessors' values are its own): a phi only if they differ)
                                    operand_list operands;
                                    value same = None;
                                    bool differ = false;
                                    for (auto p : iFunction.blocks[aBlock].predecessors)
                                    {
                                        auto const o = read(aVariable, p);
                                        operands.push_back(o);
                                        if (same == None)
                                            same = o;
                                        else if (o != same)
                                            differ = true;
                                    }
                                    if (differ)
                                    {
                                        v = phi(aVariable, aBlock);
                                        iFunction.values[v].operands = std::move(operands);
                                    }
                                    else
                                        v = same;
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
                                operand_list operands;
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
                            // --- an entry at a loop (see osr_entry)
                            static bool opens(std::uint8_t aOp) { return aOp == 0x02u || aOp == 0x03u || aOp == 0x04u; }
                            // (the loop (its token's origin aLoop: once, in the function's own code, within no other loop, at the operand stack's
                            // base) and the constructs (blocks and ifs) that it is within)
                            void osr_path(std::size_t aLoop)
                            {
                                struct open_construct
                                {
                                    std::size_t position = 0u;
                                    std::uint8_t op = 0u;
                                    bool inElse = false;
                                };
                                std::vector<open_construct> open;
                                std::size_t target = ~std::size_t{};
                                for (std::size_t p = 0u; p < iTokens.size();)
                                {
                                    auto const op = static_cast<std::uint8_t>(iTokens[p]);
                                    if (op == 0x03u && p < iOrigins.size() && iOrigins[p] == aLoop)
                                    {
                                        if (target != ~std::size_t{})
                                            throw unsupported{ "osr loop" };
                                        target = p;
                                        for (auto const& c : open)
                                        {
                                            if (c.op != 0x02u && c.op != 0x04u)
                                                throw unsupported{ "osr loop" };   // (within another loop)
                                            iOsrPath.push_back(osr_construct{ c.position, c.inElse });
                                        }
                                    }
                                    if (opens(op))
                                        open.push_back(open_construct{ p, op, false });
                                    else if (op == 0x05u && !open.empty())
                                        open.back().inElse = true;
                                    else if (op == 0x0Bu && !open.empty())
                                        open.pop_back();
                                    p += 1u + immediates(op, iTokens, p);
                                }
                                if (target == ~std::size_t{})
                                    throw unsupported{ "osr loop" };
                                iOsrTarget = target;
                            }
                            // (a construct's tokens, from its opening token: after its end)
                            void skip_construct(std::size_t& aPosition, std::size_t aEnd) const
                            {
                                std::size_t depth = 0u;
                                do
                                {
                                    auto const op = static_cast<std::uint8_t>(iTokens[aPosition]);
                                    if (opens(op))
                                        ++depth;
                                    else if (op == 0x0Bu)
                                        --depth;
                                    aPosition += 1u + immediates(op, iTokens, aPosition);
                                } while (depth > 0u && aPosition < aEnd);
                            }
                            // (the code before the entry's loop: not the entry's (the constructs that have the loop are entered, without code);
                            // to the loop)
                            void seek(std::size_t& aPosition, std::size_t aEnd)
                            {
                                while (aPosition < aEnd && aPosition != iOsrTarget)
                                {
                                    auto const op = static_cast<std::uint8_t>(iTokens[aPosition]);
                                    auto const imm = [&](std::size_t aIndex) { return iTokens[aPosition + 1u + aIndex]; };
                                    if (iOsrPathIndex < iOsrPath.size() && iOsrPath[iOsrPathIndex].position == aPosition)
                                    {
                                        auto const inElse = iOsrPath[iOsrPathIndex++].inElse;
                                        frame f;
                                        f.kind = op == 0x04u ? frame::If : frame::Block;
                                        f.results = block_type(imm(0));
                                        for (auto t : f.results)
                                            f.resultVariables.push_back(new_variable(t));
                                        f.end = new_block();
                                        f.height = iStack.size();
                                        aPosition += 1u + immediates(op, iTokens, aPosition);
                                        if (op == 0x04u)
                                        {
                                            f.elseBlock = new_block();
                                            seal(f.elseBlock);
                                            if (inElse)
                                            {
                                                // (its then arm: not the entry's; to its else)
                                                skip(aPosition, aEnd);
                                                if (aPosition >= aEnd || static_cast<std::uint8_t>(iTokens[aPosition]) != 0x05u)
                                                    throw unsupported{ "osr loop" };
                                                aPosition += 1u;
                                                f.hasElse = true;
                                            }
                                            else
                                                f.osrThen = true;
                                        }
                                        iFrames.push_back(f);
                                        continue;
                                    }
                                    if (opens(op))
                                    {
                                        skip_construct(aPosition, aEnd);
                                        continue;
                                    }
                                    if (op == 0x05u || op == 0x0Bu)
                                        throw unsupported{ "osr loop" };
                                    aPosition += 1u + immediates(op, iTokens, aPosition);
                                }
                                if (aPosition != iOsrTarget)
                                    throw unsupported{ "osr loop" };
                                iSeeking = false;
                            }
                            // --- the code
                            void read_code(std::size_t aBegin, std::size_t aEnd)
                            {
                                for (std::size_t p = aBegin; p < aEnd;)
                                {
                                    if (iSeeking)
                                        seek(p, aEnd);
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
                                            iReachable = !f.osrThen;    // (an entry at a loop in its then arm: its else arm is not reached)
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
                                            if (f.kind == frame::Block && iReachable && iFunction.blocks[f.end].predecessors.empty())
                                            {
                                                // (a block that nothing branches to: its code continues where it is (its results are the
                                                // stack's), without a block of its own)
                                                seal(f.end);
                                                break;
                                            }
                                            if (iReachable)
                                            {
                                                branch_values(f);
                                                terminate(Jump, {}, { f.end });
                                            }
                                            if (f.kind == frame::If && !f.hasElse && !f.osrThen)
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
                                                if (!d.frames.empty())
                                                {
                                                    // (frames (see optimizer::deopt::frames): each one's locals (the optimized code's, or
                                                    // constants; a local that its code does not read after its call: 0) and its operand stack
                                                    // (the next part of the operand stack's), then the call's arguments)
                                                    operand_list operands;
                                                    std::size_t height = 0u;
                                                    for (auto const& frame : d.frames)
                                                    {
                                                        auto const& fd = iModule.functions.at(frame.function);
                                                        auto const parameterCount = fd.parameters->size();
                                                        // (the function's own frame: its description's locals are the optimized code's (its own first))
                                                        if (frame.locals.size() > parameterCount + fd.locals->size() || height + frame.depth > iStack.size())
                                                            throw unsupported{ "deopt state" };
                                                        for (std::size_t l = 0u; l < frame.locals.size(); ++l)
                                                        {
                                                            auto const type = l < parameterCount ? (*fd.parameters)[l] : (*fd.locals)[l - parameterCount];
                                                            auto const local = frame.locals[l];
                                                            if (local == optimizer::deopt::NoLocal)
                                                                operands.push_back(add(type == I32 ? 0x41u : type == I64 ? 0x42u : type == F32 ? 0x43u : 0x44u, type, {},
                                                                    frame.constants[l]));
                                                            else if (local >= iLocals || iVariableTypes[local] != type)
                                                                throw unsupported{ "deopt state" };
                                                            else if (l < frame.live.size() && !frame.live[l])
                                                                operands.push_back(zero(type));
                                                            else
                                                                operands.push_back(read(static_cast<variable>(local), iCurrent));
                                                        }
                                                        for (std::uint32_t e = 0u; e < frame.depth; ++e)
                                                            operands.push_back(iStack[height + e]);
                                                        height += frame.depth;
                                                    }
                                                    operands.insert(operands.end(), arguments.begin(), arguments.end());
                                                    terminate(Deopt, std::move(operands), {}, index);
                                                    break;
                                                }
                                                if (d.locals > iLocals || iStack.size() < d.depth)
                                                    throw unsupported{ "deopt state" };
                                                operand_list operands;
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
                                            if (callee < iModule.functions.size() && iModule.functions[callee].imported && iModule.functions[callee].call == import_call::Assume)
                                            {
                                                // (neos::assume: its condition (not a call))
                                                auto const condition = pop();
                                                add(Assume, Void, { condition });
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
                                        iStack.push_back(constant(op, I32, static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int64_t>(imm(0))))));
                                        break;
                                    case 0x42:
                                        iStack.push_back(constant(op, I64, imm(0)));
                                        break;
                                    case 0x43:
                                        iStack.push_back(constant(op, F32, imm(0) & 0xFFFFFFFFull));
                                        break;
                                    case 0x44:
                                        iStack.push_back(constant(op, F64, imm(0)));
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
                                operand_list operands(aParameters.size() + (aIndirect ? 1u : 0u), value{});
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
                            flat_map64<value> iDefinitions;
                            std::vector<bool> iSealed;
                            std::vector<std::vector<std::pair<variable, value>>> iIncomplete;
                            std::vector<std::vector<value>> iPhis;
                            std::vector<value> iForward;
                            std::vector<value> iStack;
                            std::vector<frame> iFrames;
                            std::uint32_t iCurrent = 0u;
                            bool iReachable = true;
                            std::size_t iOrigin = 0u;
                            // (an entry at a loop: see osr_entry)
                            flat_map64<value> iConstants[4];    ///< (see constant: each value + 1)
                            value iAllOnes[4] = {};
                            bool iSeeking = false;
                            std::size_t iOsrTarget = ~std::size_t{};
                            std::vector<osr_construct> iOsrPath;
                            std::size_t iOsrPathIndex = 0u;
                        };

                        // --- optimizations

                        bool pure(instruction const& aInstruction)
                        {
                            auto const op = aInstruction.op;
                            if (aInstruction.constant() || op == Phi || op == Param || op == 0x1Bu)
                                return true;
                            if (op >= 0x45u && op <= 0xC4u)
                            {
                                // (integer division and float to integer truncation may trap (a truncation that cannot: Unchecked))
                                if ((op >= 0x6Du && op <= 0x70u) || (op >= 0x7Fu && op <= 0x82u))
                                    return false;
                                if (((op >= 0xA8u && op <= 0xABu) || (op >= 0xAEu && op <= 0xB1u)) && (aInstruction.flags & Unchecked) == 0u)
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
                            aFunction.usesCurrent = true;
                        }

                        // (the values' uses: counted, unless they have been since the code last changed)
                        void current_uses(function& aFunction)
                        {
                            if (aFunction.usesCurrent && aFunction.uses.size() == aFunction.values.size())
                            {
                                return;
                            }
                            count_uses(aFunction);
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
                                    to.resize(std::max<std::size_t>(aFrom + 1u, 2u * to.size()), None);    // (geometrically: values are added in order)
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

                        // (the bits of integers that may be 1 and that are 1 (e.g. a tagged value (a handle | a tag): its tag is known; a cell's
                        // offset made from it: the handle's (an offset that is 16 byte aligned and below 2^36 (masked): the same offset)): a value
                        // whose bits are all known is a constant; an and that clears no bit that may be 1, an or that sets none that is not
                        // 1, a shift right then left that drops no bit that may be 1, an extension of a wrap that drops none: their operand)
                        // (a cached load (optimizer::MemargCached: its word does not change while what the code checked first is the same) at a
                        // constant address of a fixed external memory: the word's value now)
                        bool fold_cached_loads(function& aFunction, std::function<std::optional<std::uint64_t>(std::uint32_t, std::uint64_t, std::uint32_t)> const& aMemoryWord)
                        {
                            bool changed = false;
                            for (auto b : aFunction.order)
                                for (auto v : aFunction.blocks[b].code)
                                {
                                    auto& i = aFunction.values[v];
                                    if (i.dead || !i.load() || (i.imm0 & optimizer::MemargCached) == 0u || i.operands.size() != 1u || (i.type != I32 && i.type != I64))
                                        continue;
                                    auto const& a = aFunction.values[i.operands[0]];
                                    if (!a.constant())
                                        continue;
                                    std::uint32_t width = 0u;
                                    bool sign = false;
                                    switch (i.op)
                                    {
                                    case 0x28u: case 0x34u: case 0x35u: width = 4u; sign = i.op == 0x34u; break;
                                    case 0x29u: width = 8u; break;
                                    case 0x2Cu: case 0x2Du: case 0x30u: case 0x31u: width = 1u; sign = i.op == 0x2Cu || i.op == 0x30u; break;
                                    case 0x2Eu: case 0x2Fu: case 0x32u: case 0x33u: width = 2u; sign = i.op == 0x2Eu || i.op == 0x32u; break;
                                    default: continue;
                                    }
                                    auto const memory = static_cast<std::uint32_t>(i.imm0 >> 32u);
                                    auto const address = a.imm0 + i.imm1;
                                    if (memory == 0u || address < a.imm0)
                                        continue;
                                    auto const word = aMemoryWord(memory, address, width);
                                    if (!word)
                                        continue;
                                    std::uint64_t value = *word;
                                    if (sign && width < 8u)
                                    {
                                        auto const shift = 64u - 8u * width;
                                        value = static_cast<std::uint64_t>(static_cast<std::int64_t>(value << shift) >> shift);
                                    }
                                    if (i.type == I32)
                                        value &= 0xFFFFFFFFu;
                                    i.op = i.type == I32 ? 0x41u : 0x42u;
                                    i.imm0 = value;
                                    i.imm1 = 0u;
                                    i.flags = 0u;
                                    i.operands.clear();
                                    changed = true;
                                }
                            return changed;
                        }

                        bool fold_known_bits(function& aFunction)
                        {
                            auto& values = aFunction.values;
                            // (a value's bits (may, ones) are read only once it is done: not initialized)
                            std::unique_ptr<std::uint64_t[]> const may{ new std::uint64_t[values.size()] };
                            std::unique_ptr<std::uint64_t[]> const ones{ new std::uint64_t[values.size()] };
                            std::vector<bool> done(values.size(), false);
                            auto const width = [&](value v) -> std::uint64_t { return values[v].type == I32 ? 0xFFFFFFFFull : ~std::uint64_t{}; };
                            replacements replaced;
                            bool changed = false;
                            for (auto b : aFunction.order)
                                for (auto v : aFunction.blocks[b].code)
                                {
                                    auto& i = values[v];
                                    if (!i.dead && (i.op == 0xB3u || i.op == 0xB8u) && i.operands.size() == 1u)
                                    {
                                        // (f32/f64.convert_i32_u of a value whose sign bit is 0: the signed conversion (no zero extension))
                                        auto& o = i.operands[0];
                                        o = replaced.resolved(o);
                                        if (!values[o].constant() && values[o].type == I32 && done[o] && (may[o] & 0x80000000ull) == 0u)
                                        {
                                            i.op = i.op == 0xB3u ? 0xB2u : 0xB7u;
                                            changed = true;
                                        }
                                        continue;
                                    }
                                    if (i.dead || (i.type != I32 && i.type != I64))
                                        continue;
                                    for (auto& o : i.operands)
                                        o = replaced.resolved(o);
                                    auto const mask = width(v);
                                    // (a constant (wherever it is: constants are where they are used): its bits)
                                    auto const m = [&](std::size_t n) { auto const o = i.operands[n]; return values[o].constant() && (values[o].type == I32 || values[o].type == I64) ? values[o].imm0 & width(o) : done[o] ? may[o] : width(o); };
                                    auto const k = [&](std::size_t n) { auto const o = i.operands[n]; return values[o].constant() && (values[o].type == I32 || values[o].type == I64) ? values[o].imm0 & width(o) : done[o] ? ones[o] : std::uint64_t{ 0u }; };
                                    auto const c = [&](std::size_t n) -> std::optional<std::uint64_t>
                                    {
                                        auto const& x = values[i.operands[n]];
                                        if (x.constant())
                                            return x.imm0 & width(i.operands[n]);
                                        return {};
                                    };
                                    std::uint64_t pm = mask, po = 0u;
                                    auto const op = i.op;
                                    if (i.constant())
                                    {
                                        pm = po = i.imm0 & mask;
                                    }
                                    else if (op == Phi)
                                    {
                                        pm = 0u;
                                        po = mask;
                                        for (std::size_t n = 0u; n < i.operands.size(); ++n)
                                        {
                                            if (i.operands[n] == v)
                                                continue;
                                            pm |= m(n);
                                            po &= k(n);
                                        }
                                    }
                                    else if ((op == 0x71u || op == 0x83u) && i.operands.size() == 2u)
                                    {
                                        // (and(or(a, K), M) where K and M have no bit in common: and(a, M))
                                        for (std::size_t n = 0u; n < 2u; ++n)
                                            if (auto const kk = c(1u - n))
                                                for (int depth = 0; depth < 4; ++depth)
                                                {
                                                    auto const& x = values[i.operands[n]];
                                                    if (x.dead || x.op != (op == 0x71u ? 0x72u : 0x84u) || x.operands.size() != 2u)
                                                        break;
                                                    bool rewritten = false;
                                                    for (std::size_t q = 0u; q < 2u; ++q)
                                                        if (auto const& y = values[x.operands[1u - q]]; y.constant() && (y.imm0 & *kk & mask) == 0u)
                                                        {
                                                            i.operands[n] = replaced.resolved(x.operands[q]);
                                                            rewritten = changed = true;
                                                            break;
                                                        }
                                                    if (!rewritten)
                                                        break;
                                                }
                                        pm = m(0) & m(1);
                                        po = k(0) & k(1);
                                        for (std::size_t n = 0u; n < 2u; ++n)
                                            if (auto const kk = c(1u - n); kk && (m(n) & ~*kk & mask) == 0u && values[i.operands[n]].type == i.type)
                                            {
                                                // (an and that clears no bit that may be 1)
                                                replaced.add(v, i.operands[n]);
                                                i.dead = true;
                                                changed = true;
                                                break;
                                            }
                                    }
                                    else if ((op == 0x72u || op == 0x84u) && i.operands.size() == 2u)
                                    {
                                        pm = m(0) | m(1);
                                        po = k(0) | k(1);
                                        for (std::size_t n = 0u; n < 2u; ++n)
                                            if (auto const kk = c(1u - n); kk && (*kk & ~k(n) & mask) == 0u && values[i.operands[n]].type == i.type)
                                            {
                                                // (an or that sets no bit that is not 1)
                                                replaced.add(v, i.operands[n]);
                                                i.dead = true;
                                                changed = true;
                                                break;
                                            }
                                    }
                                    else if ((op == 0x74u || op == 0x86u || op == 0x76u || op == 0x88u) && i.operands.size() == 2u && c(1u))
                                    {
                                        auto const s = static_cast<int>(*c(1u) & (op <= 0x76u ? 31u : 63u));
                                        bool const left = op == 0x74u || op == 0x86u;
                                        pm = (left ? m(0) << s : m(0) >> s) & mask;
                                        po = (left ? k(0) << s : k(0) >> s) & mask;
                                        if (left && s != 0)
                                        {
                                            // (a shift right then left by the same that drops no bit that may be 1)
                                            auto const& x = values[i.operands[0]];
                                            if ((x.op == (op == 0x74u ? 0x76u : 0x88u)) && x.operands.size() == 2u && !x.dead)
                                            {
                                                auto const& xs = values[x.operands[1]];
                                                auto const y = replaced.resolved(x.operands[0]);
                                                auto const ym = done[y] ? may[y] : width(y);
                                                if (xs.constant() && static_cast<int>(xs.imm0 & (op <= 0x76u ? 31u : 63u)) == s && (ym & ((std::uint64_t{ 1u } << s) - 1u)) == 0u && values[y].type == i.type)
                                                {
                                                    replaced.add(v, y);
                                                    i.dead = true;
                                                    changed = true;
                                                }
                                            }
                                        }
                                    }
                                    else if ((op == 0x7Cu || op == 0x6Au) && i.operands.size() == 2u)
                                    {
                                        // (add: a bit of the sum is known where both operands' bits and the carry into it are known: the sums of 
                                        // the operands' largest (the bits that may be 1) and of their smallest (the bits that are 1) values give 
                                        // the carries (each sum's bit is the operands' bits and its carry in, xor'd))
                                        auto const am = m(0), ak = k(0), bm = m(1), bk = k(1);
                                        auto const largest = am + bm, smallest = ak + bk;
                                        auto const carryKnownZero = ~(largest ^ am ^ bm);
                                        auto const carryKnownOne = smallest ^ ak ^ bk;
                                        auto const known = ((~am | ak) & (~bm | bk) & (carryKnownZero | carryKnownOne)) & mask;
                                        pm = ((largest & known) | ~known) & mask;
                                        po = smallest & known & mask;
                                    }
                                    else if (op == 0xA7u && i.operands.size() == 1u)
                                    {
                                        pm = m(0) & 0xFFFFFFFFu;
                                        po = k(0) & 0xFFFFFFFFu;
                                    }
                                    else if (op == 0xADu && i.operands.size() == 1u)
                                    {
                                        pm = m(0) & 0xFFFFFFFFu;
                                        po = k(0) & 0xFFFFFFFFu;
                                        // (an extension of a wrap that drops no bit that may be 1)
                                        auto const& x = values[i.operands[0]];
                                        if (x.op == 0xA7u && !x.dead && x.operands.size() == 1u)
                                        {
                                            auto const y = replaced.resolved(x.operands[0]);
                                            if (values[y].type == I64 && ((done[y] ? may[y] : ~std::uint64_t{}) & ~0xFFFFFFFFull) == 0u)
                                            {
                                                replaced.add(v, y);
                                                i.dead = true;
                                                changed = true;
                                            }
                                        }
                                    }
                                    else if ((op == 0x46u || op == 0x47u || op == 0x51u || op == 0x52u) && i.operands.size() == 2u)
                                    {
                                        // (equality of values whose bits differ where both are known: not equal)
                                        pm = 1u;
                                        auto const a = i.operands[0], bb = i.operands[1];
                                        auto const am = values[a].constant() ? (values[a].imm0 & width(a)) : m(0), ak = values[a].constant() ? (values[a].imm0 & width(a)) : k(0);
                                        auto const bm = values[bb].constant() ? (values[bb].imm0 & width(bb)) : m(1), bk = values[bb].constant() ? (values[bb].imm0 & width(bb)) : k(1);
                                        if (((ak & ~bm) | (bk & ~am)) != 0u)
                                            pm = po = (op == 0x47u || op == 0x52u) ? 1u : 0u;
                                    }
                                    else if (op == 0x2Du || op == 0x31u) pm = 0xFFu;
                                    else if (op == 0x2Fu || op == 0x33u) pm = 0xFFFFu;
                                    else if (op == 0x35u) pm = 0xFFFFFFFFu;
                                    else if (op >= 0x45u && op <= 0x66u) pm = 1u;
                                    if (i.dead)
                                    {
                                        auto const to = replaced.resolved(v);
                                        may[v] = done[to] ? may[to] : width(to);
                                        ones[v] = done[to] ? ones[to] : 0u;
                                        done[v] = true;
                                        continue;
                                    }
                                    po &= pm;
                                    may[v] = pm;
                                    ones[v] = po;
                                    done[v] = true;
                                    // (all of its bits known: a constant (an instruction without effects))
                                    if (pm == po && !i.constant() && op != Phi && op != Param && pure(i))
                                    {
                                        i.op = i.type == I32 ? 0x41u : 0x42u;
                                        i.imm0 = pm;
                                        i.operands.clear();
                                        i.flags = 0u;
                                        changed = true;
                                    }
                                }
                            replaced.apply(aFunction);
                            return changed;
                        }

                        // (what neos::assume claims (ssa::Assume): that a value (defined in the assume's block, before it: every path to the 
                        // assume passes its definition, and its value does not change) is not a constant (x != C, or eqz(x == C)); a phi 
                        // of such values (each not the same constant) is not it either; comparisons of them with the constant are constants)
                        bool fold_assumed(function& aFunction)
                        {
                            auto& values = aFunction.values;
                            std::vector<std::optional<std::uint64_t>> excluded(values.size());
                            bool any = false;
                            auto const not_equal = [&](value aCondition, value& aValue, std::uint64_t& aBits) -> bool
                            {
                                auto const* c = &values[aCondition];
                                bool negated = false;
                                if (c->op == 0x45u && c->operands.size() == 1u)
                                {
                                    c = &values[c->operands[0]];
                                    negated = true;
                                }
                                bool const ne = c->op == 0x47u || c->op == 0x52u;
                                bool const eq = c->op == 0x46u || c->op == 0x51u;
                                if (c->dead || c->operands.size() != 2u || !(negated ? eq : ne))
                                    return false;
                                for (std::size_t n = 0u; n < 2u; ++n)
                                    if (auto const& k = values[c->operands[n]]; k.constant() && !values[c->operands[1u - n]].constant())
                                    {
                                        aValue = c->operands[1u - n];
                                        aBits = k.imm0;
                                        return true;
                                    }
                                return false;
                            };
                            for (auto const& b : aFunction.blocks)
                                for (std::size_t k = 0u; k < b.code.size(); ++k)
                                {
                                    auto const& a = values[b.code[k]];
                                    if (a.dead || a.op != Assume || a.operands.size() != 1u)
                                        continue;
                                    value x = None;
                                    std::uint64_t bits = 0u;
                                    if (!not_equal(a.operands[0], x, bits) || values[x].block != a.block || values[x].op == Phi)
                                        continue;
                                    // (defined before the assume, in its block)
                                    bool before = false;
                                    for (std::size_t j = 0u; j < k; ++j)
                                        if (b.code[j] == x)
                                        {
                                            before = true;
                                            break;
                                        }
                                    if (!before)
                                        continue;
                                    excluded[x] = bits;
                                    any = true;
                                }
                            if (!any)
                                return false;
                            for (bool grew = true; grew;)
                            {
                                grew = false;
                                for (auto bi : aFunction.order)
                                    for (auto v : aFunction.blocks[bi].code)
                                    {
                                        auto const& p = values[v];
                                        if (p.op != Phi)
                                            break;
                                        if (p.dead || excluded[v] || p.operands.empty())
                                            continue;
                                        std::optional<std::uint64_t> common;
                                        bool all = true;
                                        for (auto o : p.operands)
                                        {
                                            if (o == v)
                                                continue;
                                            if (o >= excluded.size() || !excluded[o] || (common && *common != *excluded[o]))
                                            {
                                                all = false;
                                                break;
                                            }
                                            common = excluded[o];
                                        }
                                        if (all && common)
                                        {
                                            excluded[v] = common;
                                            grew = true;
                                        }
                                    }
                            }
                            bool changed = false;
                            for (auto bi : aFunction.order)
                                for (auto v : aFunction.blocks[bi].code)
                                {
                                    auto& i = values[v];
                                    bool const ne = i.op == 0x47u || i.op == 0x52u;
                                    bool const eq = i.op == 0x46u || i.op == 0x51u;
                                    if (i.dead || !(ne || eq) || i.operands.size() != 2u)
                                        continue;
                                    for (std::size_t n = 0u; n < 2u; ++n)
                                    {
                                        auto const x = i.operands[1u - n];
                                        auto const& k = values[i.operands[n]];
                                        if (!k.constant() || x >= excluded.size() || !excluded[x] || *excluded[x] != k.imm0 || values[x].type != k.type)
                                            continue;
                                        // (not an assume's own condition)
                                        bool assumed = false;
                                        for (auto u : aFunction.blocks[bi].code)
                                            if (auto const& a = values[u]; a.op == Assume && !a.dead && !a.operands.empty() &&
                                                (a.operands[0] == v || (values[a.operands[0]].op == 0x45u && !values[a.operands[0]].operands.empty() && values[a.operands[0]].operands[0] == v)))
                                                assumed = true;
                                        if (assumed)
                                            break;
                                        i.op = 0x41u;
                                        i.type = I32;
                                        i.imm0 = ne ? 1u : 0u;
                                        i.operands.clear();
                                        changed = true;
                                        break;
                                    }
                                }
                            return changed;
                        }

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

                        void compact_values(function& aFunction);

                        void eliminate_dead_code(function& aFunction)
                        {
                            // (instructions without effects whose values are not used (those of blocks that cannot be reached: none, and their
                            // operands' uses are not counted))
                            // (a block that cannot be reached: no code (its values' are dead; a constant is where it is used, which may be
                            // elsewhere))
                            for (auto& b : aFunction.blocks)
                                if (!b.reachable && !b.code.empty())
                                {
                                    for (auto v : b.code)
                                        if (!aFunction.values[v].constant())
                                            aFunction.values[v].dead = true;
                                    b.code.clear();
                                }
                            current_uses(aFunction);
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
                            // (the blocks' code without its dead values (the passes that follow do not visit them))
                            std::size_t code = 0u;
                            for (auto& b : aFunction.blocks)
                            {
                                std::erase_if(b.code, [&](value v) { return aFunction.values[v].dead; });
                                code += b.code.size();
                            }
                            // (mostly dead values: the others numbered again (the passes' tables are the code's size))
                            if (aFunction.values.size() > 4u * code + 1024u)
                                compact_values(aFunction);
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

                        // (an integer comparison (i32's or i64's: eqz, eq, ne, lt, gt, le, ge) of constants (aB: eqz's none): its result; not
                        // one: none)
                        std::optional<bool> evaluate_comparison(std::uint16_t aOp, std::uint64_t aA, std::uint64_t aB)
                        {
                            bool const wide = aOp >= 0x50u && aOp <= 0x5Au;
                            if (!wide && !(aOp >= 0x45u && aOp <= 0x4Fu))
                                return {};
                            if (!wide)
                            {
                                aA &= 0xFFFFFFFFu;
                                aB &= 0xFFFFFFFFu;
                            }
                            auto const sa = wide ? static_cast<std::int64_t>(aA) : static_cast<std::int64_t>(static_cast<std::int32_t>(aA));
                            auto const sb = wide ? static_cast<std::int64_t>(aB) : static_cast<std::int64_t>(static_cast<std::int32_t>(aB));
                            switch (aOp - (wide ? 0x50u : 0x45u))
                            {
                            case 0u: return aA == 0u;   // (eqz)
                            case 1u: return aA == aB;
                            case 2u: return aA != aB;
                            case 3u: return sa < sb;
                            case 4u: return aA < aB;
                            case 5u: return sa > sb;
                            case 6u: return aA > aB;
                            case 7u: return sa <= sb;
                            case 8u: return aA <= aB;
                            case 9u: return sa >= sb;
                            case 10u: return aA >= aB;
                            default: return {};
                            }
                        }

                        // (an i64 eq or ne of an and with a mask of high bits (its only use) and a constant (none of whose bits are below the
                        // mask's): of the and's operand shifted right (past the mask's low zeros) and the constant shifted so (no 64 bit
                        // immediate))
                        bool narrow_masked_comparisons(function& aFunction)
                        {
                            bool changed = false;
                            auto& values = aFunction.values;
                            current_uses(aFunction);
                            for (auto b : aFunction.order)
                                for (auto v : aFunction.blocks[b].code)
                                {
                                    if (values[v].dead || (values[v].op != 0x51u && values[v].op != 0x52u) || values[v].operands.size() != 2u)
                                        continue;
                                    for (std::size_t n = 0u; n < 2u; ++n)
                                    {
                                        auto const a = values[v].operands[n];
                                        auto const c = values[v].operands[1u - n];
                                        if (!values[c].constant() || values[c].type != I64 || values[a].dead || values[a].op != 0x83u ||
                                            values[a].type != I64 || values[a].operands.size() != 2u || aFunction.uses[a] != 1u)
                                            continue;
                                        std::size_t const mo = values[values[a].operands[1]].op == 0x42u ? 1u : values[values[a].operands[0]].op == 0x42u ? 0u : 2u;
                                        if (mo == 2u)
                                            continue;
                                        auto const mask = values[values[a].operands[mo]].imm0;
                                        if (mask == 0u || (mask & 1u) != 0u)
                                            continue;
                                        auto const shift = static_cast<unsigned>(std::countr_zero(mask));
                                        if ((mask >> shift) != (~std::uint64_t{} >> shift) || shift < 32u || (values[c].imm0 & ~mask) != 0u)
                                            continue;
                                        auto const x = values[a].operands[1u - mo];
                                        auto const block = values[a].block;
                                        instruction s;
                                        s.op = 0x42u;
                                        s.type = I64;
                                        s.imm0 = shift;
                                        s.block = block;
                                        values.push_back(s);
                                        auto const sv = static_cast<value>(values.size() - 1u);
                                        // (the constant's top bit set (e.g. a NaN box's tag): an arithmetic shift, so that the constant is small
                                        // and negative (an 8 bit immediate))
                                        bool const arithmetic = (values[c].imm0 >> 63u) != 0u;
                                        instruction k;
                                        k.op = 0x42u;
                                        k.type = I64;
                                        k.imm0 = arithmetic ? static_cast<std::uint64_t>(static_cast<std::int64_t>(values[c].imm0) >> shift) : values[c].imm0 >> shift;
                                        k.block = values[v].block;
                                        values.push_back(k);
                                        auto const kv = static_cast<value>(values.size() - 1u);
                                        auto& ai = values[a];
                                        ai.op = arithmetic ? 0x87u : 0x88u;
                                        ai.operands = { x, sv };
                                        values[v].operands[1u - n] = kv;
                                        changed = true;
                                        break;
                                    }
                                }
                            return changed;
                        }

                        // (a branch on i32.eqz(x): a branch on x, its successors swapped (exact for any x: e.g. a float comparison's, whose
                        // inverse is not a comparison (NaN)), so that a comparison's flags decide it)
                        bool invert_branches(function& aFunction)
                        {
                            auto& values = aFunction.values;
                            bool changed = false;
                            for (auto b : aFunction.order)
                            {
                                auto& B = aFunction.blocks[b];
                                if (B.code.empty() || B.successors.size() != 2u)
                                    continue;
                                auto& t = values[B.code.back()];
                                if (t.op != Branch || t.dead || t.operands.size() != 1u)
                                    continue;
                                auto const& c = values[t.operands[0]];
                                if (c.dead || c.op != 0x45u || c.operands.size() != 1u || values[c.operands[0]].type != I32)
                                    continue;
                                t.operands[0] = c.operands[0];
                                std::swap(B.successors[0], B.successors[1]);
                                changed = true;
                            }
                            if (changed)
                                count_uses(aFunction);
                            return changed;
                        }

                        bool simplify(function& aFunction)
                        {
                            bool changed = false;
                            auto& values = aFunction.values;
                            current_uses(aFunction);
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
                                    if (((op >= 0x54u && op <= 0x5Au && (op - 0x54u) % 2u == 0u) || op == 0x51u || op == 0x52u) && i.operands.size() == 2u)
                                    {
                                        // (an i64 unsigned comparison (lt_u gt_u le_u ge_u, eq, ne) of an integer converted to f64 (its bits, which
                                        // are at most -infinity's: it is not a NaN), or of a phi of such values (and constants that are at most that),
                                        // with a constant above that: known)
                                        constexpr std::uint64_t Most = 0xFFF0000000000000ull;
                                        auto const converting = [&](auto const& aSelf, value aValue, int aDepth) -> bool
                                        {
                                            auto const& x = values[aValue];
                                            if (x.constant())
                                                return x.type == I64 && x.imm0 <= Most;
                                            if (x.op == 0xBDu && x.operands.size() == 1u && values[x.operands[0]].op >= 0xB7u && values[x.operands[0]].op <= 0xBAu)
                                                return true;
                                            if (x.op == ssa::Phi && aDepth > 0 && !x.operands.empty())
                                            {
                                                for (auto o : x.operands)
                                                    if (o == aValue || !aSelf(aSelf, o, aDepth - 1))
                                                        return false;
                                                return true;
                                            }
                                            return false;
                                        };
                                        auto const converted = [&](value aValue, int aDepth) { return converting(converting, aValue, aDepth); };
                                        std::optional<bool> known;
                                        auto const& a0 = values[i.operands[0]];
                                        auto const& a1 = values[i.operands[1]];
                                        if (!a0.constant() && a1.constant() && converted(i.operands[0], 3))
                                        {
                                            auto const k = a1.imm0;
                                            if (op == 0x54u && k > Most) known = true;          // (x < k)
                                            else if (op == 0x58u && k >= Most) known = true;    // (x <= k)
                                            else if (op == 0x56u && k >= Most) known = false;   // (x > k)
                                            else if (op == 0x5Au && k > Most) known = false;    // (x >= k)
                                            else if (op == 0x51u && k > Most) known = false;    // (x == k)
                                            else if (op == 0x52u && k > Most) known = true;     // (x != k)
                                        }
                                        else if (!a1.constant() && a0.constant() && converted(i.operands[1], 3))
                                        {
                                            auto const k = a0.imm0;
                                            if (op == 0x56u && k > Most) known = true;          // (k > x)
                                            else if (op == 0x5Au && k >= Most) known = true;    // (k >= x)
                                            else if (op == 0x54u && k >= Most) known = false;   // (k < x)
                                            else if (op == 0x58u && k > Most) known = false;    // (k <= x)
                                            else if (op == 0x51u && k > Most) known = false;    // (k == x)
                                            else if (op == 0x52u && k > Most) known = true;     // (k != x)
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
                                    case 7u:                        // (and: all ones, 0; a boolean's and 1)
                                        if (cy && *cy == ones) same = x;
                                        else if (cx && *cx == ones) same = y;
                                        else if (!wide && cy && *cy == 1u && is_boolean(aFunction, x, 4)) same = x;
                                        else if (!wide && cx && *cx == 1u && is_boolean(aFunction, y, 4)) same = y;
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
                            bool candidates = false;
                            for (auto b : aFunction.order)
                                for (auto v : aFunction.blocks[b].code)
                                {
                                    auto const& i = values[v];
                                    if (i.dead || i.op != 0x83u || !values[i.operands[1]].constant())
                                        continue;
                                    auto const mask = values[i.operands[1]].imm0;
                                    auto const zeros = std::countr_zero(mask);
                                    if (mask != 0u && zeros >= 32 && (mask >> zeros) == (~std::uint64_t{} >> zeros))
                                    {
                                        candidate[v] = zeros;
                                        candidates = true;
                                    }
                                }
                            if (!candidates)
                                return changed;
                            // (each candidate's users (candidate, user), in the code's order)
                            std::vector<std::pair<value, value>> users;
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
                                            users.emplace_back(x, v);
                                    }
                                }
                            std::stable_sort(users.begin(), users.end(), [](auto const& aL, auto const& aR) { return aL.first < aR.first; });
                            for (std::size_t k = 0u; k < users.size();)
                            {
                                auto const x = users[k].first;
                                auto last = k;
                                while (last < users.size() && users[last].first == x)
                                    ++last;
                                if (candidate[x] < 0)
                                {
                                    k = last;
                                    continue;
                                }
                                auto const zeros = static_cast<std::uint64_t>(candidate[x]);
                                auto const block = values[x].block;
                                auto const shift = new_constant(I64, zeros, block);
                                values[x].op = 0x88u;      // (i64.shr_u)
                                values[x].operands[1] = shift;
                                for (; k < last; ++k)
                                {
                                    auto const u = users[k].second;
                                    for (std::size_t o = 0u; o < values[u].operands.size(); ++o)
                                        if (auto const c = values[u].operands[o]; c != x)
                                        {
                                            auto const shifted = new_constant(I64, values[c].imm0 >> zeros, block);
                                            values[u].operands[o] = shifted;
                                        }
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
                            struct key
                            {
                                std::uint64_t words[5];
                                std::uint32_t size = 0u;
                                void push_back(std::uint64_t aWord) { words[size++] = aWord; }
                                bool operator==(key const& aOther) const { return size == aOther.size && std::equal(words, words + size, aOther.words); }
                                std::size_t hash() const noexcept
                                {
                                    std::uint64_t h = 0x9E3779B97F4A7C15ull;
                                    for (std::uint32_t k = 0u; k < size; ++k)
                                        h = (h ^ words[k]) * 0x100000001B3ull + (h >> 29u);
                                    return static_cast<std::size_t>(h ^ (h >> 32u));
                                }
                            };
                            // (open addressing (linear probing): keys are removed in the reverse of the order they were added (the walk's
                            // scopes), so a key's slot is no other key's probe's (those were added after it, and removed first): it is
                            // emptied)
                            struct scoped_table
                            {
                                std::vector<key> keys;
                                std::vector<value> values;
                                std::size_t mask = 0u;
                                std::size_t count = 0u;
                                std::vector<key> added;     // (the keys added, in order: removed as the walk leaves a block)
                                void reset(std::size_t aCapacity)
                                {
                                    std::size_t size = 64u;
                                    while (size < aCapacity * 2u)
                                        size *= 2u;
                                    keys.assign(size, key{});
                                    values.assign(size, None);
                                    mask = size - 1u;
                                    count = 0u;
                                }
                                std::size_t find_slot(key const& aKey) const
                                {
                                    auto i = aKey.hash() & mask;
                                    while (keys[i].size != 0u && !(keys[i] == aKey))
                                        i = (i + 1u) & mask;
                                    return i;
                                }
                                // (the value of the key, and whether it was added (with aValue))
                                std::pair<value, bool> try_emplace(key const& aKey, value aValue)
                                {
                                    if ((count + 1u) * 2u > keys.size())
                                    {
                                        // (larger: the keys added again, in their order)
                                        auto const live = std::move(added);
                                        std::vector<value> liveValues;
                                        liveValues.reserve(live.size());
                                        for (auto const& k : live)
                                            liveValues.push_back(values[find_slot(k)]);
                                        reset(keys.size());
                                        for (std::size_t k = 0u; k < live.size(); ++k)
                                            try_emplace(live[k], liveValues[k]);
                                    }
                                    auto const i = find_slot(aKey);
                                    if (keys[i].size != 0u)
                                        return { values[i], false };
                                    keys[i] = aKey;
                                    values[i] = aValue;
                                    ++count;
                                    added.push_back(aKey);
                                    return { aValue, true };
                                }
                                void pop_to(std::size_t aMark)
                                {
                                    while (added.size() > aMark)
                                    {
                                        auto const i = find_slot(added.back());
                                        keys[i].size = 0u;
                                        --count;
                                        added.pop_back();
                                    }
                                }
                            };
                            scoped_table table;
                            {
                                // (sized for the code's values (values has the dead too): it grows if it needs to)
                                std::size_t code = 0u;
                                for (auto b : order)
                                    code += blocks[b].code.size();
                                table.reset(code);
                            }
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
                                flat_map64<value> constants[4];
                                value allOnes[4] = {};  // (flat_map64's keys are not all ones) (each: the value + 1 (0: none yet))
                                for (std::size_t v = 0u; v < values.size(); ++v)
                                {
                                    auto const& i = values[v];
                                    if (!i.constant() || i.dead)
                                        continue;
                                    auto& same = i.imm0 == flat_map64<value>::Empty ? allOnes[i.op - 0x41u] : constants[i.op - 0x41u][i.imm0];
                                    if (same == value{})
                                        same = static_cast<value>(v) + 1u;
                                    else
                                        replaced.add(static_cast<value>(v), same - 1u);
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
                                key k;
                                k.push_back(aOp | (std::uint64_t{ aType } << 16u));
                                k.push_back(aMemarg >> 32u);
                                k.push_back(aOffset);
                                k.push_back(aEpoch);
                                k.push_back(aAddress);
                                return k;
                            };
                            struct frame { std::uint32_t block; std::size_t next; std::size_t mark; std::size_t knownMark; std::size_t accessMark; std::size_t equalMark; };
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
                            // (the values that branches that dominate a block decided are equal to constants (x == C on the edge): the 
                            // constants, in it)
                            std::vector<value> equalTo(values.size(), None);
                            std::vector<value> equalOrder;
                            auto const constant_of = [&](value aValue) -> std::optional<std::uint64_t>
                            {
                                if (values[aValue].constant())
                                    return values[aValue].imm0;
                                if (aValue < equalTo.size() && equalTo[aValue] != None)
                                    return values[equalTo[aValue]].imm0;
                                return {};
                            };
                            // (an f64 that branches that dominate the block decided is within the range of a signed truncation (i32's or, aWide,
                            // i64's): not NaN, and above the least integer minus one and below the greatest plus one (x >= C, x > C, x < C,
                            // x <= C (or below an integer's conversion), and and of them, and eqz of them))
                            auto const in_range = [&](value aValue, bool aWide) -> bool
                            {
                                double lower = -std::numeric_limits<double>::infinity();
                                double upper = std::numeric_limits<double>::infinity();
                                bool upperInclusive = true;
                                std::function<void(value, bool, int)> consider = [&](value aCondition, bool aTrue, int aDepth)
                                {
                                    auto const& c = values[aCondition];
                                    if (c.dead || aDepth <= 0)
                                        return;
                                    if (c.op == 0x45u && c.operands.size() == 1u)
                                    {
                                        consider(c.operands[0], !aTrue, aDepth - 1);
                                        return;
                                    }
                                    if (c.op == 0x71u && c.operands.size() == 2u && aTrue)
                                    {
                                        consider(c.operands[0], true, aDepth - 1);
                                        consider(c.operands[1], true, aDepth - 1);
                                        return;
                                    }
                                    if (!aTrue || c.op < 0x63u || c.op > 0x66u || c.operands.size() != 2u || replaced.resolved(c.operands[0]) != aValue)
                                        return;
                                    // (a constant, or (an upper bound) an integer's conversion: at most its greatest)
                                    auto const& k = values[replaced.resolved(c.operands[1])];
                                    double bound;
                                    if (k.op == 0x44u)
                                        bound = std::bit_cast<double>(k.imm0);
                                    else if ((c.op == 0x63u || c.op == 0x65u) && (k.op == 0xB7u || k.op == 0xB8u))
                                        bound = k.op == 0xB7u ? 2147483647.0 : 4294967295.0;
                                    else
                                        return;
                                    switch (c.op)
                                    {
                                    case 0x63u: if (bound <= upper) { upper = bound; upperInclusive = false; } break;   // (lt)
                                    case 0x65u: if (bound < upper) { upper = bound; upperInclusive = true; } break;     // (le)
                                    case 0x64u: case 0x66u: lower = std::max(lower, bound); break;                       // (gt, ge)
                                    default: break;
                                    }
                                };
                                for (auto c : knownOrder)
                                    if (known[c] >= 0)
                                        consider(c, known[c] != 0, 4);
                                // (lower: not NaN (a comparison with it is false for NaN))
                                double const least = aWide ? -9223372036854775808.0 : -2147483648.0;
                                double const limit = aWide ? 9223372036854775808.0 : 2147483648.0;
                                return lower > least - 1.0 && (upper < limit || (!upperInclusive && upper <= limit));
                            };
                            // (a value that is a multiple of aAlignment (a power of two))
                            std::function<bool(value, std::uint64_t, int)> aligned = [&](value aValue, std::uint64_t aAlignment, int aDepth) -> bool
                            {
                                auto const& x = values[aValue];
                                if (x.constant())
                                    return (x.imm0 & (aAlignment - 1u)) == 0u;
                                if (aDepth <= 0 || x.operands.size() != 2u)
                                    return false;
                                auto const& c = values[x.operands[1]];
                                switch (x.op)
                                {
                                case 0x74u: case 0x86u:     // (shl by a constant)
                                    return c.constant() && (std::uint64_t{ 1u } << std::min<std::uint64_t>(c.imm0 & 63u, 63u)) % aAlignment == 0u;
                                case 0x6Au: case 0x7Cu:     // (add)
                                    return aligned(x.operands[0], aAlignment, aDepth - 1) && aligned(x.operands[1], aAlignment, aDepth - 1);
                                case 0x71u: case 0x83u:     // (and)
                                    return aligned(x.operands[0], aAlignment, aDepth - 1) || aligned(x.operands[1], aAlignment, aDepth - 1);
                                default:
                                    return false;
                                }
                            };
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
                                stack.push_back(frame{ aBlock, 0u, table.added.size(), knownOrder.size(), accesses.size(), equalOrder.size() });
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
                                            // (a conjunction (i32.and) of comparisons that is true: each is 1; a disjunction (i32.or) that is false:
                                            // each is 0)
                                            if (auto const op = values[condition].op; (op == 0x71u && whenTrue) || (op == 0x72u && !whenTrue))
                                            {
                                                std::vector<value> pending{ condition };
                                                for (int n = 0; !pending.empty() && n < 16; ++n)
                                                {
                                                    auto const term = replaced.resolved(pending.back());
                                                    pending.pop_back();
                                                    auto const& t = values[term];
                                                    if (t.dead || t.constant())
                                                        continue;
                                                    if (t.op == op && t.operands.size() == 2u)
                                                    {
                                                        pending.push_back(t.operands[0]);
                                                        pending.push_back(t.operands[1]);
                                                    }
                                                    else if (t.op >= 0x45u && t.op <= 0x66u && term < known.size() && known[term] < 0)
                                                    {
                                                        known[term] = whenTrue ? 1 : 0;
                                                        knownOrder.push_back(term);
                                                    }
                                                }
                                            }
                                            // (x == C on this edge: x is C)
                                            if (auto const& c = values[condition]; !c.dead && (c.op == 0x46u || c.op == 0x47u || c.op == 0x51u || c.op == 0x52u) && c.operands.size() == 2u &&
                                                whenTrue == (c.op == 0x46u || c.op == 0x51u))
                                                for (std::size_t n = 0u; n < 2u; ++n)
                                                {
                                                    auto const k = replaced.resolved(c.operands[n]);
                                                    auto const x = replaced.resolved(c.operands[1u - n]);
                                                    if (values[k].constant() && !values[x].constant() && x < equalTo.size() && equalTo[x] == None && values[x].type == values[k].type)
                                                    {
                                                        equalTo[x] = k;
                                                        equalOrder.push_back(x);
                                                        break;
                                                    }
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
                                        else if (values[v].op != Phi && o < equalTo.size() && equalTo[o] != None)
                                            o = equalTo[o];
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
                                        // (what its location held (of this epoch: an access of it whose value is known), so that accesses at other
                                        // bases that held other values there are at other addresses (and, aligned, not overlapped))
                                        std::optional<std::uint64_t> held;
                                        bool const alignedBase = c != 0u && width != 0u && aligned(st.operands[0], 8u, 4) && st.imm1 % 8u == 0u;
                                        if (alignedBase)
                                        {
                                            std::size_t scanned = 0u;
                                            for (auto a = accesses.size(); a > 0u && scanned < MaxAccessScan; --a, ++scanned)
                                            {
                                                auto const& e = accesses[a - 1u];
                                                if (e.memoryClass == c && e.epoch == before && e.address == st.operands[0] && e.memory == (st.imm0 >> 32u) &&
                                                    e.offset == st.imm1 && e.width == width)
                                                {
                                                    held = constant_of(e.data);
                                                    break;
                                                }
                                            }
                                        }
                                        if (c != 0u && width != 0u)
                                        {
                                            std::size_t scanned = 0u;
                                            auto const count = accesses.size();
                                            for (auto a = count; a > 0u && scanned < MaxAccessScan; --a, ++scanned)
                                            {
                                                auto const e = accesses[a - 1u];
                                                if (e.memoryClass != c || e.epoch != before || e.memory != (st.imm0 >> 32u))
                                                    continue;
                                                if (e.address != st.operands[0])
                                                {
                                                    // (another base that held another value at the same offset (of the same width): another address; 
                                                    // both aligned: not overlapped)
                                                    if (!held || e.offset != st.imm1 || e.width != width || !aligned(e.address, 8u, 4))
                                                        continue;
                                                    auto const other = constant_of(e.data);
                                                    if (!other || *other == *held)
                                                        continue;
                                                    if (e.loadOp != 0u)
                                                    {
                                                        table.try_emplace(memory_key(e.loadOp, e.type, st.imm0, e.offset, e.address, epoch[c]), e.data);
                                                    }
                                                    auto moved = e;
                                                    moved.epoch = epoch[c];
                                                    accesses.push_back(moved);
                                                    continue;
                                                }
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
                                                    table.try_emplace(memory_key(e.loadOp, e.type, st.imm0, e.offset, e.address, epoch[c]), e.data);
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
                                            table.try_emplace(memory_key(load, values[st.operands[1]].type, st.imm0, st.imm1, st.operands[0], epoch[c]), st.operands[1]);
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
                                    if ((i.op == 0xAAu || i.op == 0xB0u) && (i.flags & Unchecked) == 0u && i.operands.size() == 1u && in_range(i.operands[0], i.op == 0xB0u))
                                        i.flags |= Unchecked;   // (its operand is within its range: it cannot trap)
                                    key k;
                                    if (i.load())
                                        k = memory_key(i.op, i.type, i.imm0, i.imm1, i.operands[0], epoch[class_of(i)]);
                                    else if (i.op == 0x23u)
                                    {
                                        k.push_back(i.op);
                                        k.push_back(i.imm0);
                                        k.push_back(epoch[Globals]);
                                    }
                                    else if (((i.op >= 0x45u && i.op <= 0xC4u) || i.op == 0x1Bu) && i.operands.size() <= 4u)
                                    {
                                        k.push_back(i.op | (std::uint64_t{ i.type } << 16u));
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
                                    if (!inserted && values[where].type == i.type)
                                    {
                                        i.dead = true;
                                        replaced.add(v, where);
                                        data = where;
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
                                table.pop_to(top.mark);
                                for (auto k = knownOrder.size(); k > top.knownMark; --k)
                                    known[knownOrder[k - 1u]] = -1;
                                knownOrder.resize(top.knownMark);
                                for (auto k = equalOrder.size(); k > top.equalMark; --k)
                                    equalTo[equalOrder[k - 1u]] = None;
                                equalOrder.resize(top.equalMark);
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
                        std::vector<std::uint8_t> const& aLocals, std::vector<std::size_t> const& aOrigins, std::vector<optimizer::deopt> const& aDeopts,
                        osr_entry const* aOsr)
                    {
                        builder b{ aModule, aFunction, aTokens, aLocals, aOrigins, aDeopts, aOsr };
                        auto f = b.build();
                        finish(f);
                        return f;
                    }

                    void finish(function& aFunction)
                    {
                        // (the blocks that can be reached, in reverse postorder (a loop's blocks follow its header))
                        auto& blocks = aFunction.blocks;
                        std::size_t wasReachable = 0u;
                        for (auto& b : blocks)
                        {
                            wasReachable += b.reachable ? 1u : 0u;
                            b.reachable = false;
                        }
                        bool pruned = false;
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
                                    pruned = true;
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
                        std::vector<std::uint32_t> work;
                        std::vector<std::uint32_t> members;
                        std::vector<bool> in(blocks.size(), false);
                        for (auto h : aFunction.order)
                        {
                            if (!blocks[h].loopHeader)
                                continue;
                            // (the loop's blocks: backwards from its back edges' sources to it)
                            work.clear();
                            members.clear();
                            for (auto p : blocks[h].predecessors)
                                if (position[p] >= position[h])
                                    work.push_back(p);
                            in[h] = true;
                            members.push_back(h);
                            while (!work.empty())
                            {
                                auto const x = work.back();
                                work.pop_back();
                                if (in[x] || !blocks[x].reachable)
                                    continue;
                                in[x] = true;
                                members.push_back(x);
                                for (auto p : blocks[x].predecessors)
                                    work.push_back(p);
                            }
                            for (auto x : members)
                            {
                                ++blocks[x].loopDepth;
                                in[x] = false;
                            }
                        }
                        // (the uses: counted again, unless they are current (the code is as it was: the blocks that can be reached are too,
                        // as their number shows))
                        if (pruned || postorder.size() != wasReachable || !aFunction.usesCurrent || aFunction.uses.size() != aFunction.values.size())
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
                            current_uses(aFunction);
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
                                // (its phis: used only by the target's phis (their operands for it: the predecessors' operands then))
                                std::vector<value> ownPhis;
                                for (auto v : E.code)
                                {
                                    auto const& i = values[v];
                                    if (i.dead || i.constant())
                                        continue;
                                    if (i.op == Jump)
                                        jump = v;
                                    else if (i.op == Phi && !E.loopHeader)
                                        ownPhis.push_back(v);
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
                                if (clash && !ownPhis.empty())
                                    continue;
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
                                        {
                                            if (!i.dead && i.operands[ie] != None)
                                                --aFunction.uses[i.operands[ie]];
                                            i.operands.erase(i.operands.begin() + static_cast<std::ptrdiff_t>(ie));
                                        }
                                    }
                                    auto& branch = values[P.code.back()];
                                    branch.op = Jump;
                                    for (auto o : branch.operands)
                                        if (o != None)
                                            --aFunction.uses[o];
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
                                if (!ownPhis.empty())
                                {
                                    bool used = false;
                                    for (auto x : ownPhis)
                                    {
                                        std::uint32_t expected = 0u;
                                        for (auto v : S.code)
                                        {
                                            auto const& i = values[v];
                                            if (i.op != Phi)
                                                break;
                                            if (!i.dead && index < i.operands.size() && i.operands[index] == x)
                                                ++expected;
                                        }
                                        if (values[x].operands.size() != E.predecessors.size() || aFunction.uses[x] != expected)
                                            used = true;
                                    }
                                    if (used)
                                        continue;
                                }
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
                                    auto& uses = aFunction.uses;   // (kept current: as count_uses counts them)
                                    if (!i.dead && operand != None)
                                        --uses[operand];
                                    i.operands.erase(i.operands.begin() + static_cast<std::ptrdiff_t>(index));
                                    if (operand != None && values[operand].op == Phi && values[operand].block == e && !values[operand].dead)
                                    {
                                        // (E's phi: its operand for each of E's predecessors)
                                        auto const operands = values[operand].operands;
                                        i.operands.insert(i.operands.begin() + static_cast<std::ptrdiff_t>(index), operands.begin(), operands.end());
                                        if (!i.dead)
                                            for (auto o : operands)
                                                if (o != None)
                                                    ++uses[o];
                                    }
                                    else
                                    {
                                        i.operands.insert(i.operands.begin() + static_cast<std::ptrdiff_t>(index), preds.size(), operand);
                                        if (!i.dead && operand != None)
                                            uses[operand] += static_cast<std::uint32_t>(preds.size());
                                    }
                                }
                                for (auto x : ownPhis)
                                {
                                    for (auto o : values[x].operands)
                                        if (o != None)
                                            --aFunction.uses[o];
                                    values[x].dead = true;
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
                            current_uses(aFunction);
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

                        // (a small block (pure instructions, then a jump) with a phi that has a constant operand, whose values are used only
                        // in it and by its successor's phis: a copy for each of its predecessors (its phis: their operands from it), so that
                        // the copies' code can be folded (e.g. what an inlined call's result is tested for, its result a constant on one path))
                        bool duplicate_tails(function& aFunction)
                        {
                            auto& blocks = aFunction.blocks;
                            auto& values = aFunction.values;
                            current_uses(aFunction);
                            bool changed = false;
                            std::size_t copies = 0u;
                            std::size_t const count = blocks.size();
                            for (std::uint32_t b = 1u; b < count && copies < 32u; ++b)
                            {
                                if (!blocks[b].reachable || blocks[b].loopHeader || blocks[b].successors.size() != 1u)
                                    continue;
                                auto const preds = blocks[b].predecessors;
                                auto const c = blocks[b].successors[0];
                                if (preds.size() < 2u || preds.size() > 4u || c == b || blocks[c].loopHeader)
                                    continue;
                                bool ok = true;
                                for (std::size_t k = 0u; k < preds.size() && ok; ++k)
                                    for (std::size_t q = k + 1u; q < preds.size(); ++q)
                                        if (preds[k] == preds[q] || preds[k] == b)
                                            ok = false;
                                if (!ok || std::count(blocks[c].predecessors.begin(), blocks[c].predecessors.end(), b) != 1)
                                    continue;
                                std::vector<value> phis, body;
                                value jump = None;
                                for (auto v : blocks[b].code)
                                {
                                    auto const& i = values[v];
                                    if (i.dead)
                                        continue;
                                    if (i.op == Phi)
                                        phis.push_back(v);
                                    else if (i.op == Jump)
                                        jump = v;
                                    else if (jump != None || i.terminator() || !pure(i))
                                        ok = false;
                                    else
                                        body.push_back(v);
                                }
                                if (!ok || jump == None || phis.empty() || body.size() > 8u)
                                    continue;
                                bool constantOperand = false;
                                for (auto ph : phis)
                                {
                                    if (values[ph].operands.size() != preds.size())
                                        ok = false;
                                    for (auto o : values[ph].operands)
                                        if (o == None)
                                            ok = false;
                                        else if (values[o].constant())
                                            constantOperand = true;
                                }
                                if (!ok || !constantOperand)
                                    continue;
                                // (its values: used only in it and by its successor's phis)
                                auto const kb = static_cast<std::size_t>(std::find(blocks[c].predecessors.begin(), blocks[c].predecessors.end(), b) - blocks[c].predecessors.begin());
                                std::unordered_map<value, std::uint32_t> local;
                                for (auto v : phis)
                                    local.emplace(v, 0u);
                                for (auto v : body)
                                    local.emplace(v, 0u);
                                for (auto v : body)
                                    for (auto o : values[v].operands)
                                        if (auto l = local.find(o); l != local.end())
                                            ++l->second;
                                for (auto v : blocks[c].code)
                                {
                                    if (values[v].op != Phi)
                                        break;
                                    if (values[v].dead)
                                        continue;
                                    for (std::size_t n = 0u; n < values[v].operands.size(); ++n)
                                        if (auto l = local.find(values[v].operands[n]); l != local.end())
                                        {
                                            if (n != kb)
                                                ok = false;
                                            ++l->second;
                                        }
                                }
                                for (auto const& [v, n] : local)
                                    if (aFunction.uses[v] != n)
                                        ok = false;
                                if (!ok)
                                    continue;
                                // (the copies)
                                std::vector<std::uint32_t> made;
                                std::vector<std::unordered_map<value, value>> maps(preds.size());
                                for (std::size_t k = 0u; k < preds.size(); ++k)
                                {
                                    auto const nb = static_cast<std::uint32_t>(blocks.size());
                                    blocks.push_back(block{});
                                    blocks[nb].predecessors = { preds[k] };
                                    blocks[nb].successors = { c };
                                    blocks[nb].loopDepth = blocks[b].loopDepth;
                                    blocks[nb].reachable = true;
                                    auto& map = maps[k];
                                    for (auto ph : phis)
                                        map[ph] = values[ph].operands[k];
                                    for (auto v : body)
                                    {
                                        instruction x = values[v];
                                        x.block = nb;
                                        for (auto& o : x.operands)
                                            if (auto m = map.find(o); m != map.end())
                                                o = m->second;
                                        values.push_back(std::move(x));
                                        map[v] = static_cast<value>(values.size() - 1u);
                                        blocks[nb].code.push_back(map[v]);
                                    }
                                    instruction j;
                                    j.op = Jump;
                                    j.type = Void;
                                    j.block = nb;
                                    j.origin = values[jump].origin;
                                    values.push_back(std::move(j));
                                    blocks[nb].code.push_back(static_cast<value>(values.size() - 1u));
                                    for (auto& s : blocks[preds[k]].successors)
                                        if (s == b)
                                        {
                                            s = nb;
                                            break;
                                        }
                                    made.push_back(nb);
                                }
                                // (the successor: the copies as its predecessors instead, its phis' operands from them)
                                auto& cp = blocks[c].predecessors;
                                cp.erase(cp.begin() + static_cast<std::ptrdiff_t>(kb));
                                cp.insert(cp.begin() + static_cast<std::ptrdiff_t>(kb), made.begin(), made.end());
                                for (auto v : blocks[c].code)
                                {
                                    if (values[v].op != Phi)
                                        break;
                                    auto& ops = values[v].operands;
                                    if (ops.size() <= kb)
                                        continue;
                                    auto const o = ops[kb];
                                    std::vector<value> from;
                                    for (std::size_t k = 0u; k < preds.size(); ++k)
                                    {
                                        auto const m = maps[k].find(o);
                                        from.push_back(m != maps[k].end() ? m->second : o);
                                    }
                                    ops.erase(ops.begin() + static_cast<std::ptrdiff_t>(kb));
                                    ops.insert(ops.begin() + static_cast<std::ptrdiff_t>(kb), from.begin(), from.end());
                                }
                                for (auto v : blocks[b].code)
                                    values[v].dead = true;
                                blocks[b].predecessors.clear();
                                blocks[b].successors.clear();
                                blocks[b].reachable = false;
                                copies += preds.size();
                                changed = true;
                            }
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
                        // (a phi (of a block that has no other code but comparisons of it with constants and its branch) whose operand from a 
                        // predecessor that jumps to it is a constant, or a boolean made from a condition (i64.add(i64.extend_i32_u(c), k)): the
                        // predecessor goes to where the comparisons (of the block and of its successors that have only comparisons of it, each 
                        // with that one predecessor) lead with that value (or each of the two: it branches on the condition), if the phi is
                        // used nowhere else but in deopts' blocks that the comparisons lead to (e.g. an inlined predicate's result, then 
                        // converted to a condition: true, false, else a deopt))
                        bool thread_known_values(function& aFunction)
                        {
                            auto& blocks = aFunction.blocks;
                            auto& values = aFunction.values;
                            current_uses(aFunction);
                            bool changed = false;
                            // (a comparison of the phi with a constant: its result for the phi's value (none if it is not one))
                            auto const decided = [&](value aCondition, value aPhi, std::uint64_t aValue) -> std::optional<bool>
                            {
                                auto const& c = values[aCondition];
                                if (c.dead)
                                    return {};
                                if ((c.op == 0x45u || c.op == 0x50u) && c.operands.size() == 1u && c.operands[0] == aPhi)
                                    return evaluate_comparison(c.op, aValue, 0u);
                                if (c.operands.size() != 2u)
                                    return {};
                                if (c.operands[0] == aPhi && values[c.operands[1]].constant())
                                    return evaluate_comparison(c.op, aValue, values[c.operands[1]].imm0);
                                if (c.operands[1] == aPhi && values[c.operands[0]].constant())
                                    return evaluate_comparison(c.op, values[c.operands[0]].imm0, aValue);
                                return {};
                            };
                            // (a block of the walk: its code (but constants) a comparison of the phi and its branch on it)
                            auto const comparing = [&](std::uint32_t aBlock, value aPhi, bool aOwn) -> value
                            {
                                auto const& X = blocks[aBlock];
                                if (!X.reachable || X.loopHeader || X.successors.size() != 2u || X.successors[0] == X.successors[1] || X.code.empty())
                                    return None;
                                auto const br = X.code.back();
                                if (values[br].op != Branch || values[br].dead || values[br].operands.size() != 1u)
                                    return None;
                                auto const c = values[br].operands[0];
                                if (values[c].block != aBlock || aFunction.uses[c] != 1u)
                                    return None;
                                for (auto v : X.code)
                                {
                                    auto const& i = values[v];
                                    if (i.dead || i.constant() || v == br || v == c || (aOwn && v == aPhi))
                                        continue;
                                    return None;
                                }
                                return c;
                            };
                            for (std::uint32_t b = 1u; b < blocks.size(); ++b)
                            {
                                auto& B = blocks[b];
                                if (!B.reachable || B.predecessors.size() < 2u)
                                    continue;
                                // (its one phi)
                                value phi = None;
                                for (auto v : B.code)
                                    if (values[v].op == Phi && !values[v].dead)
                                    {
                                        if (phi != None)
                                        {
                                            phi = None;
                                            break;
                                        }
                                        phi = v;
                                    }
                                if (phi == None || values[phi].operands.size() != B.predecessors.size())
                                    continue;
                                if (comparing(b, phi, true) == None)
                                    continue;
                                // (the walk's blocks: B, then successors (of one predecessor) that compare the phi)
                                std::vector<std::uint32_t> walk{ b };
                                std::vector<value> conditions{ comparing(b, phi, true) };
                                for (std::size_t w = 0u; w < walk.size(); ++w)
                                    for (auto s : blocks[walk[w]].successors)
                                        if (s != b && blocks[s].predecessors.size() == 1u && std::find(walk.begin(), walk.end(), s) == walk.end())
                                            if (auto const c = comparing(s, phi, false); c != None)
                                            {
                                                walk.push_back(s);
                                                conditions.push_back(c);
                                            }
                                // (the phi's uses: the walk's comparisons, and deopts in blocks of one predecessor of the walk's)
                                std::uint32_t uses = 0u;
                                for (auto c : conditions)
                                {
                                    auto const& i = values[c];
                                    for (auto o : i.operands)
                                        if (o == phi)
                                            ++uses;
                                }
                                for (auto x : walk)
                                    for (auto s : blocks[x].successors)
                                    {
                                        if (std::find(walk.begin(), walk.end(), s) != walk.end() || blocks[s].predecessors.size() != 1u || !blocks[s].successors.empty())
                                            continue;
                                        for (auto v : blocks[s].code)
                                            if (!values[v].dead)
                                                for (auto o : values[v].operands)
                                                    if (o == phi)
                                                        ++uses;
                                    }
                                if (aFunction.uses[phi] != uses)
                                    continue;
                                // (where the walk leads with a value: the block (not one of the walk's), and the walk's block it is reached from)
                                auto const destination = [&](std::uint64_t aValue) -> std::optional<std::pair<std::uint32_t, std::uint32_t>>
                                {
                                    auto x = b;
                                    for (std::size_t steps = 0u; steps <= walk.size(); ++steps)
                                    {
                                        auto const at = static_cast<std::size_t>(std::find(walk.begin(), walk.end(), x) - walk.begin());
                                        if (at >= walk.size())
                                            return {};
                                        auto const taken = decided(conditions[at], phi, aValue);
                                        if (!taken)
                                            return {};
                                        auto const s = blocks[x].successors[*taken ? 0u : 1u];
                                        if (std::find(walk.begin(), walk.end(), s) == walk.end())
                                        {
                                            // (not a deopt's block that uses the phi (it is reached only through the walk))
                                            if (blocks[s].successors.empty())
                                                for (auto v : blocks[s].code)
                                                    for (auto o : values[v].operands)
                                                        if (o == phi)
                                                            return {};
                                            return std::make_pair(s, x);
                                        }
                                        x = s;
                                    }
                                    return {};
                                };
                                // (a destination's phis: their operands for the predecessor (their operand for the walk's block: not a value of 
                                // the walk's blocks))
                                auto const operands_for = [&](std::uint32_t aTarget, std::uint32_t aFrom, std::vector<value>& aOperands)
                                {
                                    auto const& T = blocks[aTarget];
                                    auto const index = static_cast<std::size_t>(std::find(T.predecessors.begin(), T.predecessors.end(), aFrom) - T.predecessors.begin());
                                    if (index >= T.predecessors.size())
                                        return false;
                                    aOperands.clear();
                                    for (auto v : T.code)
                                    {
                                        auto const& j = values[v];
                                        if (j.op != Phi)
                                            break;
                                        auto const x = index < j.operands.size() ? j.operands[index] : None;
                                        if (x != None && std::find(walk.begin(), walk.end(), values[x].block) != walk.end() && !values[x].constant())
                                            return false;
                                        aOperands.push_back(x);
                                    }
                                    return true;
                                };
                                auto const add_edge = [&](std::uint32_t aFrom, std::uint32_t aTarget, std::vector<value> const& aOperands)
                                {
                                    auto& T = blocks[aTarget];
                                    T.predecessors.push_back(aFrom);
                                    std::size_t k = 0u;
                                    for (auto v : T.code)
                                    {
                                        auto& j = values[v];
                                        if (j.op != Phi)
                                            break;
                                        j.operands.push_back(aOperands[k++]);
                                    }
                                };
                                auto const remove_predecessor = [&](std::size_t aIndex)
                                {
                                    B.predecessors.erase(B.predecessors.begin() + static_cast<std::ptrdiff_t>(aIndex));
                                    auto& j = values[phi];
                                    j.operands.erase(j.operands.begin() + static_cast<std::ptrdiff_t>(aIndex));
                                };
                                auto const free_target = [&](std::uint32_t aFrom, std::uint32_t aTarget)
                                {
                                    return aTarget != aFrom && std::find(blocks[aTarget].predecessors.begin(), blocks[aTarget].predecessors.end(), aFrom) == blocks[aTarget].predecessors.end();
                                };
                                for (std::size_t k = 0u; k < B.predecessors.size() && B.predecessors.size() > 1u;)
                                {
                                    auto const p = B.predecessors[k];
                                    auto& P = blocks[p];
                                    auto const operand = values[phi].operands[k];
                                    bool const jumps = P.successors.size() == 1u && P.successors[0] == b && p != b && !P.code.empty() && values[P.code.back()].op == Jump &&
                                        std::count(B.predecessors.begin(), B.predecessors.end(), p) == 1;
                                    if (!jumps || operand == None)
                                    {
                                        ++k;
                                        continue;
                                    }
                                    auto const& c = values[operand];
                                    std::vector<value> operandsTrue, operandsFalse;
                                    if (c.constant())
                                    {
                                        auto const d = destination(c.imm0);
                                        if (!d || !free_target(p, d->first) || !operands_for(d->first, d->second, operandsTrue))
                                        {
                                            ++k;
                                            continue;
                                        }
                                        add_edge(p, d->first, operandsTrue);
                                        P.successors[0] = d->first;
                                        remove_predecessor(k);
                                        changed = true;
                                        continue;
                                    }
                                    // (a boolean made from a condition)
                                    value condition = None;
                                    std::uint64_t base = 0u;
                                    if (c.op == 0x7Cu && c.operands.size() == 2u)
                                        for (std::size_t n = 0u; n < 2u; ++n)
                                        {
                                            auto const& z = values[c.operands[n]];
                                            auto const& k0 = values[c.operands[1u - n]];
                                            if (!k0.constant() || z.op != 0xADu || z.operands.size() != 1u)
                                                continue;
                                            auto const& x = values[z.operands[0]];
                                            if (x.dead || x.type != I32 || !(x.op >= 0x45u && x.op <= 0x66u))
                                                continue;
                                            condition = z.operands[0];
                                            base = k0.imm0;
                                            break;
                                        }
                                    if (condition == None)
                                    {
                                        ++k;
                                        continue;
                                    }
                                    auto const dTrue = destination(base + 1u);
                                    auto const dFalse = destination(base);
                                    if (!dTrue || !dFalse || dTrue->first == dFalse->first || !free_target(p, dTrue->first) || !free_target(p, dFalse->first) ||
                                        !operands_for(dTrue->first, dTrue->second, operandsTrue) || !operands_for(dFalse->first, dFalse->second, operandsFalse))
                                    {
                                        ++k;
                                        continue;
                                    }
                                    add_edge(p, dTrue->first, operandsTrue);
                                    add_edge(p, dFalse->first, operandsFalse);
                                    auto& jump = values[P.code.back()];
                                    jump.op = Branch;
                                    jump.operands = { condition };
                                    P.successors = { dTrue->first, dFalse->first };
                                    remove_predecessor(k);
                                    changed = true;
                                }
                                if (changed)
                                    count_uses(aFunction);
                            }
                            return changed;
                        }

                        bool thread_branches(function& aFunction)
                        {
                            auto& blocks = aFunction.blocks;
                            auto& values = aFunction.values;
                            current_uses(aFunction);
                            bool changed = false;
                            for (std::uint32_t b = 1u; b < blocks.size(); ++b)
                            {
                                auto& B = blocks[b];
                                if (!B.reachable || B.loopHeader || B.successors.size() != 2u || B.successors[0] == B.successors[1] || B.code.empty())
                                    continue;
                                auto const br = B.code.back();
                                if (values[br].op != Branch || values[br].dead || values[br].operands.size() != 1u)
                                    continue;
                                // (the branch on a phi, or on a comparison (used only by the branch) of a phi with a constant)
                                auto cond = values[br].operands[0];
                                value comparison = None;
                                std::uint64_t comparand = 0u;
                                bool phiFirst = true;
                                if (values[cond].op != Phi && !values[cond].dead && values[cond].block == b && aFunction.uses[cond] == 1u &&
                                    evaluate_comparison(values[cond].op, 0u, 0u))
                                {
                                    auto const& c = values[cond];
                                    if (c.op == 0x45u || c.op == 0x50u)
                                    {
                                        if (c.operands.size() == 1u)
                                        {
                                            comparison = cond;
                                            cond = c.operands[0];
                                        }
                                    }
                                    else if (c.operands.size() == 2u)
                                    {
                                        for (std::size_t n = 0u; n < 2u; ++n)
                                            if (values[c.operands[n]].constant() && values[c.operands[1u - n]].op == Phi)
                                            {
                                                comparison = cond;
                                                comparand = values[c.operands[n]].imm0;
                                                phiFirst = n == 1u;
                                                cond = c.operands[1u - n];
                                                break;
                                            }
                                    }
                                }
                                if (values[cond].op != Phi || values[cond].block != b || values[cond].dead)
                                    continue;
                                if (B.successors[0] == b || B.successors[1] == b)
                                    continue;
                                bool ok = true;
                                for (auto v : B.code)
                                {
                                    auto const& i = values[v];
                                    if (i.dead || i.op == Phi || i.constant() || v == br || v == comparison)
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
                                    std::uint32_t expected = v == cond ? 1u : 0u;   // (the branch's, or the comparison's)
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
                                // (a value that is 0 or 1: a comparison's, or an and, or or of them)
                                auto const boolean = [&](value aValue)
                                {
                                    auto const& x = values[aValue];
                                    if (x.dead || x.type != I32)
                                        return false;
                                    if (x.op >= 0x45u && x.op <= 0x66u)
                                        return true;
                                    if ((x.op == 0x71u || x.op == 0x72u) && x.operands.size() == 2u)
                                        for (auto o : x.operands)
                                        {
                                            auto const& y = values[o];
                                            if (y.dead || y.type != I32 || !(y.op >= 0x45u && y.op <= 0x66u))
                                                return false;
                                        }
                                    return x.op == 0x71u || x.op == 0x72u;
                                };
                                for (std::size_t k = 0u; k < B.predecessors.size();)
                                {
                                    auto const& c = values[values[cond].operands[k]];
                                    auto const p = B.predecessors[k];
                                    std::optional<bool> knownTaken;     // (a boolean compared with neither of its values: known)
                                    if (!c.constant() && comparison != None && std::count(B.predecessors.begin(), B.predecessors.end(), p) == 1)
                                    {
                                        // (a predecessor whose operand is a boolean made from a condition (i64.add(i64.extend_i32_u(c), k)),
                                        // compared (eq, ne) with k or k + 1: it branches on the condition itself (to B's successors))
                                        auto& P = blocks[p];
                                        auto const& cmp = values[comparison];
                                        value condition = None;
                                        bool inverted = false;
                                        bool const jumps = P.successors.size() == 1u && P.successors[0] == b && p != b && !P.code.empty() && values[P.code.back()].op == Jump;
                                        if ((cmp.op == 0x51u || cmp.op == 0x52u) && c.op == 0x7Cu && c.operands.size() == 2u)
                                            for (std::size_t n = 0u; n < 2u; ++n)
                                            {
                                                auto const& z = values[c.operands[n]];
                                                auto const& k0 = values[c.operands[1u - n]];
                                                if (!k0.constant() || z.op != 0xADu || z.operands.size() != 1u || !boolean(z.operands[0]))
                                                    continue;
                                                if (comparand == k0.imm0 + 1u)
                                                    inverted = false;
                                                else if (comparand == k0.imm0)
                                                    inverted = true;
                                                else
                                                {
                                                    knownTaken = cmp.op == 0x52u;
                                                    break;
                                                }
                                                if (!jumps)
                                                    break;
                                                if (cmp.op == 0x52u)
                                                    inverted = !inverted;
                                                condition = z.operands[0];
                                                break;
                                            }
                                        auto const t0 = B.successors[inverted ? 1u : 0u];
                                        auto const t1 = B.successors[inverted ? 0u : 1u];
                                        if (condition != None && t0 != p && t1 != p &&
                                            std::find(blocks[t0].predecessors.begin(), blocks[t0].predecessors.end(), p) == blocks[t0].predecessors.end() &&
                                            std::find(blocks[t1].predecessors.begin(), blocks[t1].predecessors.end(), p) == blocks[t1].predecessors.end())
                                        {
                                            for (auto const t : { t0, t1 })
                                            {
                                                auto const s = t == B.successors[0] ? 0u : 1u;
                                                auto& T = blocks[t];
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
                                            }
                                            auto& jump = values[P.code.back()];
                                            jump.op = Branch;
                                            jump.operands = { condition };
                                            P.successors = { t0, t1 };
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
                                            continue;
                                        }
                                    }
                                    if ((!c.constant() && !knownTaken) || std::count(B.predecessors.begin(), B.predecessors.end(), p) != 1)
                                    {
                                        ++k;
                                        continue;
                                    }
                                    std::uint32_t s;
                                    if (knownTaken)
                                        s = *knownTaken ? 0u : 1u;
                                    else if (comparison == None)
                                        s = (c.imm0 & 0xFFFFFFFFu) != 0u ? 0u : 1u;
                                    else
                                    {
                                        auto const& cmp = values[comparison];
                                        auto const taken = cmp.operands.size() == 1u ? evaluate_comparison(cmp.op, c.imm0, 0u) :
                                            phiFirst ? evaluate_comparison(cmp.op, c.imm0, comparand) : evaluate_comparison(cmp.op, comparand, c.imm0);
                                        s = *taken ? 0u : 1u;
                                    }
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

                        // (what the branches that lead to a block decided about values compared (eq, ne, eqz) with constants: each such value's
                        // possible constants (or the constants that it is not), from every path to the block (a loop's back edge: nothing
                        // known); a branch whose comparison that decides is then a constant's (e.g. a second check of a value that a first
                        // check has narrowed to one or two possibilities). Only values that more than one branch tests are followed.
                        bool decide_branches(function& aFunction)
                        {
                            auto& blocks = aFunction.blocks;
                            auto& values = aFunction.values;
                            auto const& order = aFunction.order;
                            if (order.empty())
                                return false;
                            constexpr std::size_t MaxConstants = 4u;
                            constexpr std::size_t MaxFacts = 32u;
                            struct fact
                            {
                                value subject;
                                bool in;                            ///< (the value is one of the constants (else none of them))
                                std::uint8_t count;
                                std::uint64_t constants[MaxConstants];  ///< (sorted)
                                bool contains(std::uint64_t aConstant) const { return std::find(constants, constants + count, aConstant) != constants + count; }
                                void add(std::uint64_t aConstant)
                                {
                                    auto* const at = std::upper_bound(constants, constants + count, aConstant);
                                    std::copy_backward(at, constants + count, constants + count + 1);
                                    *at = aConstant;
                                    ++count;
                                }
                                void erase(std::uint64_t aConstant)
                                {
                                    auto* const e = std::remove(constants, constants + count, aConstant);
                                    count = static_cast<std::uint8_t>(e - constants);
                                }
                            };
                            using facts = std::vector<fact>;    // (by subject)
                            // (a branch's condition as (subject == constant) (aEqual) or !=)
                            auto const test_of = [&](value aCondition, value& aSubject, std::uint64_t& aConstant, bool& aEqual) -> bool
                            {
                                auto const& c = values[aCondition];
                                if (c.dead)
                                    return false;
                                if ((c.op == 0x45u || c.op == 0x50u) && c.operands.size() == 1u)
                                {
                                    if (values[c.operands[0]].constant())
                                        return false;
                                    aSubject = c.operands[0];
                                    aConstant = 0u;
                                    aEqual = true;
                                    return true;
                                }
                                if ((c.op == 0x46u || c.op == 0x47u || c.op == 0x51u || c.op == 0x52u) && c.operands.size() == 2u)
                                {
                                    for (std::size_t n = 0u; n < 2u; ++n)
                                        if (values[c.operands[n]].constant() && !values[c.operands[1u - n]].constant())
                                        {
                                            aSubject = c.operands[1u - n];
                                            aConstant = values[c.operands[n]].imm0 & (c.op <= 0x47u ? 0xFFFFFFFFull : ~0ull);
                                            aEqual = c.op == 0x46u || c.op == 0x51u;
                                            return true;
                                        }
                                }
                                // (another comparison (0 or 1): itself, as (comparison != 0) (a second test of the same comparison (the same
                                // value: see number_values) is then decided))
                                if (c.op >= 0x45u && c.op <= 0x66u && c.type == I32)
                                {
                                    aSubject = aCondition;
                                    aConstant = 0u;
                                    aEqual = false;
                                    return true;
                                }
                                return false;
                            };
                            // (a branch on a conjunction (i32.and) of comparisons: on its true edge, each is true (!= 0); on a disjunction's
                            // (i32.or) false edge, each is false (== 0))
                            auto const terms_of = [&](value aCondition, std::vector<value>& aTerms) -> bool
                            {
                                auto const op = values[aCondition].op;
                                if (op != 0x71u && op != 0x72u)
                                    return false;
                                aTerms.clear();
                                std::vector<value> pending{ aCondition };
                                while (!pending.empty() && aTerms.size() < 8u)
                                {
                                    auto const v = pending.back();
                                    pending.pop_back();
                                    auto const& x = values[v];
                                    if (x.dead)
                                        return false;
                                    if (x.op == op && x.operands.size() == 2u)
                                    {
                                        pending.push_back(x.operands[0]);
                                        pending.push_back(x.operands[1]);
                                    }
                                    else if (x.op >= 0x45u && x.op <= 0x66u && x.type == I32 && !x.constant())
                                        aTerms.push_back(v);
                                    else
                                        return false;
                                }
                                return pending.empty() && !aTerms.empty();
                            };
                            // (each block's test (of its branch), and the subjects that more than one test has)
                            struct test 
                            { 
                                value subject = None; 
                                std::uint64_t constant = 0u; 
                                bool equal = false; 
                                std::vector<value> terms;   ///< (a conjunction's (on the true edge) or a disjunction's (false edge): see terms_of)
                                bool conjunction = false;
                            };
                            std::vector<test> tests(blocks.size());
                            {
                                std::unordered_map<value, std::uint32_t> counts;
                                for (auto const b : order)
                                {
                                    auto const& B = blocks[b];
                                    if (B.code.empty() || B.successors.size() != 2u || B.successors[0] == B.successors[1])
                                        continue;
                                    auto const& t = values[B.code.back()];
                                    if (t.op != Branch || t.dead || t.operands.size() != 1u)
                                        continue;
                                    test x;
                                    if (test_of(t.operands[0], x.subject, x.constant, x.equal))
                                        ++counts[x.subject];
                                    else if (terms_of(t.operands[0], x.terms))
                                    {
                                        x.conjunction = values[t.operands[0]].op == 0x71u;
                                        for (auto const v : x.terms)
                                            ++counts[v];
                                    }
                                    tests[b] = std::move(x);
                                }
                                bool any = false;
                                for (auto& x : tests)
                                {
                                    if (x.subject != None)
                                    {
                                        if (counts[x.subject] < 2u)
                                            x.subject = None;
                                        else
                                            any = true;
                                    }
                                    std::erase_if(x.terms, [&](value v) { return counts[v] < 2u; });
                                    any = any || !x.terms.empty();
                                }
                                if (!any)
                                    return false;
                            }
                            std::vector<std::uint32_t> position(blocks.size(), ~std::uint32_t{});
                            for (std::uint32_t i = 0u; i < order.size(); ++i)
                                position[order[i]] = i;
                            std::vector<facts> entry(blocks.size());
                            std::vector<std::uint8_t> state(blocks.size(), 0u);    // (1: done and reachable; 2: done, not reachable)
                            auto const find = [](facts& aFacts, value aSubject) -> fact*
                            {
                                auto const at = std::lower_bound(aFacts.begin(), aFacts.end(), aSubject, [](fact const& aFact, value aValue) { return aFact.subject < aValue; });
                                return at != aFacts.end() && at->subject == aSubject ? &*at : nullptr;
                            };
                            // (that a value is a constant (aHolds), or is not, added to facts: false if they contradict it)
                            auto const know = [&](facts& aFacts, value aSubject, std::uint64_t aConstant, bool aHolds) -> bool
                            {
                                bool const holds = aHolds;
                                auto* f = find(aFacts, aSubject);
                                if (f == nullptr)
                                {
                                    if (aFacts.size() >= MaxFacts)
                                        return true;
                                    fact n{ aSubject, holds, 1u, { aConstant } };
                                    auto const at = std::lower_bound(aFacts.begin(), aFacts.end(), aSubject, [](fact const& aFact, value aValue) { return aFact.subject < aValue; });
                                    aFacts.insert(at, n);
                                    return true;
                                }
                                if (holds)
                                {
                                    if (f->in != f->contains(aConstant))
                                        return false;
                                    f->in = true;
                                    f->count = 1u;
                                    f->constants[0] = aConstant;
                                    return true;
                                }
                                if (f->in)
                                {
                                    f->erase(aConstant);
                                    return f->count != 0u;
                                }
                                if (!f->contains(aConstant) && f->count < MaxConstants)
                                    f->add(aConstant);
                                return true;
                            };
                            // (the facts on the edge from aFrom to aTo (aFrom's exit: its entry's, and its branch's): unreachable: false)
                            auto const edge = [&](std::uint32_t aFrom, std::uint32_t aTo, facts& aFacts) -> bool
                            {
                                if (state[aFrom] != 1u)
                                    return false;
                                aFacts = entry[aFrom];
                                auto const& F = blocks[aFrom];
                                if (F.code.empty() || F.successors.size() != 2u || F.successors[0] == F.successors[1])
                                    return true;
                                auto const& t = values[F.code.back()];
                                if (t.op != Branch || t.dead || t.operands.size() != 1u)
                                    return true;
                                auto const& condition = values[t.operands[0]];
                                if (condition.constant())
                                    return ((condition.imm0 & 0xFFFFFFFFu) != 0u) == (F.successors[0] == aTo);
                                auto const& x = tests[aFrom];
                                if (!x.terms.empty() && (F.successors[0] == aTo) == x.conjunction)
                                    for (auto const v : x.terms)
                                        if (!know(aFacts, v, 0u, !x.conjunction))
                                            return false;
                                if (x.subject == None)
                                    return true;
                                return know(aFacts, x.subject, x.constant, (F.successors[0] == aTo) == x.equal);
                            };
                            // (two paths' facts about a value: what is known on either)
                            auto const join = [&](fact const& aA, fact const& aB, fact& aResult) -> bool
                            {
                                aResult.subject = aA.subject;
                                aResult.count = 0u;
                                std::uint64_t buffer[2u * MaxConstants];
                                std::uint64_t* e;
                                if (aA.in && aB.in)
                                {
                                    aResult.in = true;
                                    e = std::set_union(aA.constants, aA.constants + aA.count, aB.constants, aB.constants + aB.count, buffer);
                                    if (static_cast<std::size_t>(e - buffer) > MaxConstants)
                                        return false;
                                }
                                else
                                {
                                    aResult.in = false;
                                    if (!aA.in && !aB.in)
                                        e = std::set_intersection(aA.constants, aA.constants + aA.count, aB.constants, aB.constants + aB.count, buffer);
                                    else
                                    {
                                        auto const& n = aA.in ? aB : aA;
                                        auto const& s = aA.in ? aA : aB;
                                        e = std::set_difference(n.constants, n.constants + n.count, s.constants, s.constants + s.count, buffer);
                                    }
                                    if (e == buffer)
                                        return false;
                                }
                                aResult.count = static_cast<std::uint8_t>(e - buffer);
                                std::copy(buffer, e, aResult.constants);
                                return true;
                            };
                            bool changed = false;
                            value constants01[2] = { None, None };
                            facts incoming, joined;
                            for (auto const b : order)
                            {
                                auto& S = entry[b];
                                auto const& B = blocks[b];
                                bool reachable = false;
                                if (b == order[0])
                                    reachable = true;
                                else
                                {
                                    bool first = true;
                                    bool backEdge = false;
                                    for (auto p : B.predecessors)
                                        if (position[p] != ~std::uint32_t{} && state[p] == 0u)
                                            backEdge = true;
                                    if (backEdge)
                                    {
                                        // (a loop's header: what its back edges bring is not known)
                                        S.clear();
                                        reachable = true;
                                    }
                                    else
                                        for (auto p : B.predecessors)
                                        {
                                            if (position[p] == ~std::uint32_t{})
                                                continue;
                                            if (!edge(p, b, incoming))
                                                continue;
                                            reachable = true;
                                            if (first)
                                            {
                                                S.swap(incoming);
                                                first = false;
                                                continue;
                                            }
                                            joined.clear();
                                            auto in = incoming.begin();
                                            for (auto const& f : S)
                                            {
                                                while (in != incoming.end() && in->subject < f.subject)
                                                    ++in;
                                                if (in == incoming.end())
                                                    break;
                                                if (in->subject != f.subject)
                                                    continue;
                                                fact r;
                                                if (join(f, *in, r))
                                                    joined.push_back(r);
                                            }
                                            S.swap(joined);
                                        }
                                }
                                state[b] = reachable ? 1u : 2u;
                                if (!reachable)
                                    continue;
                                auto const& x = tests[b];
                                if (x.subject == None)
                                    continue;
                                auto const t = B.code.back();
                                if (values[values[t].operands[0]].constant())
                                    continue;
                                auto* f = find(S, x.subject);
                                if (f == nullptr)
                                    continue;
                                std::optional<bool> holds;
                                if (f->in && f->count == 1u && f->constants[0] == x.constant)
                                    holds = true;
                                else if (f->in != f->contains(x.constant))
                                    holds = false;
                                if (!holds)
                                    continue;
                                int const bit = *holds == x.equal ? 1 : 0;
                                if (constants01[bit] == None)
                                {
                                    instruction c;
                                    c.op = 0x41u;
                                    c.type = I32;
                                    c.imm0 = static_cast<std::uint64_t>(bit);
                                    c.block = b;
                                    values.push_back(c);
                                    constants01[bit] = static_cast<value>(values.size() - 1u);
                                }
                                values[t].operands[0] = constants01[bit];
                                changed = true;
                            }
                            return changed;
                        }

                        // (a phi whose operands are all the same conversion (a reinterpretation, an integer's conversion to f64, an extension) of
                        // values (that nothing else uses), or constants that it converts exactly: the conversion of a phi of those values (so that
                        // what uses it can see through it: e.g. a comparison of an integer converted to f64 with a constant is the integer's, and a
                        // value boxed then unboxed is the value))
                        bool sink_conversions(function& aFunction)
                        {
                            auto& blocks = aFunction.blocks;
                            auto& values = aFunction.values;
                            current_uses(aFunction);
                            replacements replaced;
                            bool changed = false;
                            auto const source_type = [](std::uint16_t aOp) -> std::uint8_t
                            {
                                switch (aOp)
                                {
                                case 0xBDu: return F64;     // (i64.reinterpret_f64)
                                case 0xBFu: return I64;     // (f64.reinterpret_i64)
                                case 0xB7u: case 0xB8u: return I32;     // (f64.convert_i32_s, _u)
                                case 0xADu: case 0xACu: return I32;     // (i64.extend_i32_u, _s)
                                default: return Void;
                                }
                            };
                            // (the constant whose conversion (aOp) is aBits (of the conversion's type): its bits)
                            auto const inverse = [](std::uint16_t aOp, std::uint64_t aBits) -> std::optional<std::uint64_t>
                            {
                                switch (aOp)
                                {
                                case 0xBDu: case 0xBFu:
                                    return aBits;
                                case 0xB7u:
                                case 0xB8u:
                                    {
                                        double const d = std::bit_cast<double>(aBits);
                                        if (aOp == 0xB7u ? (d >= -2147483648.0 && d <= 2147483647.0) : (d >= 0.0 && d <= 4294967295.0))
                                            if (std::floor(d) == d && !(d == 0.0 && std::signbit(d)))
                                                return aOp == 0xB7u ? static_cast<std::uint64_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(d))) :
                                                    static_cast<std::uint64_t>(static_cast<std::uint32_t>(d));
                                        return {};
                                    }
                                case 0xADu:
                                    if (aBits <= 0xFFFFFFFFull)
                                        return aBits;
                                    return {};
                                case 0xACu:
                                    if (static_cast<std::int64_t>(aBits) >= -2147483648ll && static_cast<std::int64_t>(aBits) <= 2147483647ll)
                                        return aBits & 0xFFFFFFFFull;
                                    return {};
                                default:
                                    return {};
                                }
                            };
                            for (auto const b : aFunction.order)
                            {
                                auto& B = blocks[b];
                                std::size_t phis = 0u;
                                while (phis < B.code.size() && values[B.code[phis]].op == Phi)
                                    ++phis;
                                std::vector<value> added;
                                for (std::size_t k = 0u; k < phis; ++k)
                                {
                                    auto const p = B.code[k];
                                    auto const& P = values[p];
                                    if (P.dead || P.operands.size() < 2u)
                                        continue;
                                    std::uint16_t op = 0u;
                                    bool ok = true;
                                    bool any = false;
                                    for (auto o : P.operands)
                                    {
                                        if (o == p || values[o].constant())
                                            continue;
                                        auto const& x = values[o];
                                        if (x.dead || x.operands.size() != 1u || source_type(x.op) == Void || (op != 0u && x.op != op) || aFunction.uses[o] != 1u)
                                        {
                                            ok = false;
                                            break;
                                        }
                                        op = x.op;
                                        any = true;
                                    }
                                    if (!ok || !any)
                                        continue;
                                    // (the phi of the conversions' operands)
                                    instruction q;
                                    q.op = Phi;
                                    q.type = source_type(op);
                                    q.block = b;
                                    std::vector<value> operands;
                                    for (auto o : values[p].operands)
                                    {
                                        if (o == p)
                                        {
                                            operands.push_back(None);   // (itself: the new phi)
                                            continue;
                                        }
                                        auto const& x = values[o];
                                        if (x.constant())
                                        {
                                            auto const bits = inverse(op, x.imm0);
                                            if (!bits)
                                            {
                                                ok = false;
                                                break;
                                            }
                                            instruction c;
                                            c.op = q.type == I32 ? 0x41u : q.type == I64 ? 0x42u : q.type == F32 ? 0x43u : 0x44u;
                                            c.type = q.type;
                                            c.imm0 = *bits;
                                            c.block = b;
                                            values.push_back(c);
                                            operands.push_back(static_cast<value>(values.size() - 1u));
                                            continue;
                                        }
                                        operands.push_back(x.operands[0]);
                                    }
                                    if (!ok)
                                        continue;
                                    auto const qIndex = static_cast<value>(values.size());
                                    for (auto& o : operands)
                                        if (o == None)
                                            o = qIndex;
                                    q.operands = std::move(operands);
                                    values.push_back(std::move(q));
                                    // (the conversion of it, after the block's phis)
                                    instruction u;
                                    u.op = op;
                                    u.type = values[p].type;
                                    u.block = b;
                                    u.operands = { qIndex };
                                    values.push_back(std::move(u));
                                    auto const uIndex = static_cast<value>(values.size() - 1u);
                                    values[p].dead = true;
                                    replaced.add(p, uIndex);
                                    added.push_back(qIndex);
                                    added.push_back(uIndex);
                                    changed = true;
                                }
                                if (!added.empty())
                                {
                                    // (the new phis among the phis; their conversions after them)
                                    std::vector<value> code;
                                    code.reserve(B.code.size() + added.size());
                                    code.insert(code.end(), B.code.begin(), B.code.begin() + static_cast<std::ptrdiff_t>(phis));
                                    for (std::size_t n = 0u; n < added.size(); n += 2u)
                                        code.push_back(added[n]);
                                    for (std::size_t n = 1u; n < added.size(); n += 2u)
                                        code.push_back(added[n]);
                                    code.insert(code.end(), B.code.begin() + static_cast<std::ptrdiff_t>(phis), B.code.end());
                                    B.code = std::move(code);
                                }
                            }
                            // (a conversion of a phi of constants (its only use): a phi of the converted constants (e.g. a double of an i32
                            // phi of 1 and 2: no conversion where the paths meet))
                            auto const converted = [](std::uint16_t aOp, std::uint64_t aBits) -> std::optional<std::uint64_t>
                            {
                                switch (aOp)
                                {
                                case 0xBDu: case 0xBFu: return aBits;
                                case 0xB7u: return std::bit_cast<std::uint64_t>(static_cast<double>(static_cast<std::int32_t>(static_cast<std::uint32_t>(aBits))));
                                case 0xB8u: return std::bit_cast<std::uint64_t>(static_cast<double>(static_cast<std::uint32_t>(aBits)));
                                case 0xADu: return aBits & 0xFFFFFFFFull;
                                case 0xACu: return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(static_cast<std::uint32_t>(aBits))));
                                default: return {};
                                }
                            };
                            std::vector<std::pair<std::uint32_t, value>> newPhis;
                            for (auto const b : aFunction.order)
                                for (auto const v : blocks[b].code)
                                {
                                    if (v >= aFunction.uses.size())
                                        continue;
                                    auto const& u = values[v];
                                    if (u.dead || u.op == Phi || u.operands.size() != 1u || source_type(u.op) == Void)
                                        continue;
                                    auto const p = replaced.resolved(u.operands[0]);
                                    if (p >= aFunction.uses.size() || values[p].dead || values[p].op != Phi || aFunction.uses[p] != 1u ||
                                        values[p].type != source_type(u.op) || values[p].operands.empty())
                                        continue;
                                    bool ok = true;
                                    for (auto o : values[p].operands)
                                        if (o != p && (!values[o].constant() || !converted(u.op, values[o].imm0 & (values[o].type == I32 ? 0xFFFFFFFFull : ~0ull))))
                                            ok = false;
                                    if (!ok)
                                        continue;
                                    auto const pb = values[p].block;
                                    auto const qIndex = static_cast<value>(values.size());
                                    instruction q;
                                    q.op = Phi;
                                    q.type = u.type;
                                    q.block = pb;
                                    values.push_back(q);
                                    std::vector<value> operands;
                                    std::vector<value> const sources(values[p].operands.begin(), values[p].operands.end());
                                    for (auto o : sources)
                                    {
                                        if (o == p)
                                        {
                                            operands.push_back(qIndex);
                                            continue;
                                        }
                                        instruction c;
                                        c.type = values[v].type;
                                        c.op = c.type == I32 ? 0x41u : c.type == I64 ? 0x42u : c.type == F32 ? 0x43u : 0x44u;
                                        c.imm0 = *converted(values[v].op, values[o].imm0 & (values[o].type == I32 ? 0xFFFFFFFFull : ~0ull));
                                        c.block = pb;
                                        values.push_back(c);
                                        operands.push_back(static_cast<value>(values.size() - 1u));
                                    }
                                    values[qIndex].operands = operand_list(operands.begin(), operands.end());
                                    values[p].dead = true;
                                    values[v].dead = true;
                                    replaced.add(v, qIndex);
                                    newPhis.emplace_back(pb, qIndex);
                                    changed = true;
                                }
                            for (auto const& [pb, q] : newPhis)
                                blocks[pb].code.insert(blocks[pb].code.begin(), q);
                            replaced.apply(aFunction);
                            return changed;
                        }

                        // (a branch whose successors (of no other predecessor) do the same (the same instructions of the same operands, or of 
                        // their own values in the same places), and go to the same blocks with the same values: to the first (e.g. a property's
                        // get of either of two shapes that have it at the same position))
                        bool merge_equal_successors(function& aFunction)
                        {
                            auto& blocks = aFunction.blocks;
                            auto& values = aFunction.values;
                            bool changed = false;
                            value one = None;
                            std::unordered_map<value, value> mapping;
                            std::vector<value> first, second;
                            auto const code_of = [&](block const& aBlock, std::vector<value>& aCode)
                            {
                                aCode.clear();
                                for (auto v : aBlock.code)
                                    if (!values[v].dead && !values[v].constant())
                                        aCode.push_back(v);
                            };
                            auto const same_value = [&](value aA, value aB) -> bool
                            {
                                if (aA == aB)
                                    return true;
                                if (auto const m = mapping.find(aB); m != mapping.end())
                                    return m->second == aA;
                                auto const& x = values[aA];
                                auto const& y = values[aB];
                                return x.constant() && y.constant() && x.op == y.op && x.imm0 == y.imm0;
                            };
                            for (auto const b : aFunction.order)
                            {
                                auto& B = blocks[b];
                                if (B.code.empty() || B.successors.size() != 2u || B.successors[0] == B.successors[1])
                                    continue;
                                auto const br = B.code.back();
                                if (values[br].op != Branch || values[br].dead || values[br].operands.size() != 1u || values[values[br].operands[0]].constant())
                                    continue;
                                auto const s0 = B.successors[0];
                                auto const s1 = B.successors[1];
                                auto const& S0 = blocks[s0];
                                auto const& S1 = blocks[s1];
                                if (S0.predecessors.size() != 1u || S1.predecessors.size() != 1u || S0.successors != S1.successors || s0 == b || s1 == b)
                                    continue;
                                code_of(S0, first);
                                code_of(S1, second);
                                if (first.size() != second.size() || first.empty())
                                    continue;
                                mapping.clear();
                                bool same = true;
                                for (std::size_t k = 0u; k < first.size() && same; ++k)
                                {
                                    auto const& x = values[first[k]];
                                    auto const& y = values[second[k]];
                                    if (x.op == Phi || x.op != y.op || x.type != y.type || x.imm0 != y.imm0 || x.imm1 != y.imm1 || x.flags != y.flags ||
                                        x.operands.size() != y.operands.size())
                                    {
                                        same = false;
                                        break;
                                    }
                                    for (std::size_t n = 0u; n < x.operands.size(); ++n)
                                        if (!same_value(x.operands[n], y.operands[n]))
                                        {
                                            same = false;
                                            break;
                                        }
                                    mapping[second[k]] = first[k];
                                }
                                if (!same)
                                    continue;
                                // (their successors' phis: the same values from each)
                                for (auto t : S0.successors)
                                {
                                    auto const& T = blocks[t];
                                    auto const i0 = std::find(T.predecessors.begin(), T.predecessors.end(), s0);
                                    auto const i1 = std::find(T.predecessors.begin(), T.predecessors.end(), s1);
                                    if (i0 == T.predecessors.end() || i1 == T.predecessors.end())
                                    {
                                        same = false;
                                        break;
                                    }
                                    auto const k0 = static_cast<std::size_t>(i0 - T.predecessors.begin());
                                    auto const k1 = static_cast<std::size_t>(i1 - T.predecessors.begin());
                                    for (auto w : T.code)
                                    {
                                        auto const& j = values[w];
                                        if (j.op != Phi)
                                            break;
                                        if (j.dead)
                                            continue;
                                        if (k0 >= j.operands.size() || k1 >= j.operands.size() || !same_value(j.operands[k0], j.operands[k1]))
                                        {
                                            same = false;
                                            break;
                                        }
                                    }
                                    if (!same)
                                        break;
                                }
                                if (!same)
                                    continue;
                                if (one == None)
                                {
                                    instruction c;
                                    c.op = 0x41u;
                                    c.type = I32;
                                    c.imm0 = 1u;
                                    c.block = b;
                                    values.push_back(c);
                                    one = static_cast<value>(values.size() - 1u);
                                }
                                values[br].operands[0] = one;
                                changed = true;
                            }
                            return changed;
                        }

                        // (a branch on a comparison that is not just before it (or that has other uses): a copy of the comparison just before
                        // it (so that the code generator fuses them, rather than making its value (0 or 1) and testing that))
                        // (an unchecked access (within its memory) of a 64 bit address that adds (or subtracts) a constant: the constant in its
                        // offset (the same address: it wraps as the add does))
                        void fold_address_offsets(function& aFunction)
                        {
                            auto& values = aFunction.values;
                            for (auto b : aFunction.order)
                                for (auto v : aFunction.blocks[b].code)
                                {
                                    auto& i = values[v];
                                    if (i.dead || !(i.load() || i.store()) || (i.flags & Unchecked) == 0u || (i.imm0 >> 32u) == 0u || i.operands.empty())
                                        continue;
                                    for (int depth = 0; depth < 4; ++depth)
                                    {
                                        auto const& a = values[i.operands[0]];
                                        if (a.dead || a.type != I64 || (a.op != 0x7Cu && a.op != 0x7Du) || a.operands.size() != 2u)
                                            break;
                                        std::size_t const k = values[a.operands[1]].op == 0x42u ? 1u : a.op == 0x7Cu && values[a.operands[0]].op == 0x42u ? 0u : 2u;
                                        if (k == 2u)
                                            break;
                                        auto const c = static_cast<std::int64_t>(values[a.operands[k]].imm0);
                                        auto const offset = static_cast<std::int64_t>(i.imm1) + (a.op == 0x7Cu ? c : -c);
                                        if (c < -0x7FFFFFFFll || c > 0x7FFFFFFFll || offset < 0 || offset > 0x7FFFFFFFll)
                                            break;
                                        i.imm1 = static_cast<std::uint64_t>(offset);
                                        i.operands[0] = a.operands[1u - k];
                                    }
                                }
                        }

                        // (a value without effects (or a load that cannot trap, with nothing after it in its block that may store or call) 
                        // that a block computes, all of whose uses are where only one of its successors leads (that successor's only 
                        // predecessor is it): computed there (e.g. what only a slow path uses is not computed on the fast path))
                        bool sink_values(function& aFunction)
                        {
                            auto& blocks = aFunction.blocks;
                            auto& values = aFunction.values;
                            auto const& order = aFunction.order;
                            if (order.size() < 2u)
                                return false;
                            constexpr std::uint32_t Undefined = ~std::uint32_t{};
                            // (dominators (as number_values), then the dominator tree's intervals (a block dominates another whose interval
                            // is within its own))
                            std::vector<std::uint32_t> position(blocks.size(), Undefined);
                            for (std::uint32_t i = 0u; i < order.size(); ++i)
                                position[order[i]] = i;
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
                            std::vector<std::vector<std::uint32_t>> children(blocks.size());
                            for (std::size_t i = 1u; i < order.size(); ++i)
                                if (idom[order[i]] != Undefined)
                                    children[idom[order[i]]].push_back(order[i]);
                            std::vector<std::uint32_t> enter(blocks.size(), 0u), leave(blocks.size(), 0u);
                            {
                                std::uint32_t clock = 0u;
                                std::vector<std::pair<std::uint32_t, std::size_t>> stack{ { order[0], 0u } };
                                enter[order[0]] = clock++;
                                while (!stack.empty())
                                {
                                    auto& [b, next] = stack.back();
                                    if (next < children[b].size())
                                    {
                                        auto const c = children[b][next++];
                                        enter[c] = clock++;
                                        stack.emplace_back(c, 0u);
                                        continue;
                                    }
                                    leave[b] = clock++;
                                    stack.pop_back();
                                }
                            }
                            auto const dominates = [&](std::uint32_t aA, std::uint32_t aB) { return position[aB] != Undefined && enter[aA] <= enter[aB] && leave[aB] <= leave[aA]; };
                            // (each value's uses' blocks (a phi's operand: its predecessor's))
                            std::vector<std::vector<std::uint32_t>> users(values.size());
                            for (auto b : order)
                                for (auto v : blocks[b].code)
                                {
                                    auto const& i = values[v];
                                    if (i.dead)
                                        continue;
                                    for (std::size_t n = 0u; n < i.operands.size(); ++n)
                                    {
                                        auto const o = i.operands[n];
                                        if (o == None)
                                            continue;
                                        users[o].push_back(i.op == Phi && n < blocks[b].predecessors.size() ? blocks[b].predecessors[n] : b);
                                    }
                                }
                            bool changed = false;
                            for (auto b : order)
                            {
                                auto& A = blocks[b];
                                if (A.successors.size() < 2u || A.code.empty())
                                    continue;
                                auto const terminator = A.code.back();
                                if (values[terminator].op != Branch && values[terminator].op != Switch)
                                    continue;
                                std::vector<std::vector<value>> moved(A.successors.size());
                                bool effects = false;   // (a store or call after (below) the value)
                                for (auto c = A.code.size() - 1u; c-- > 0u;)
                                {
                                    auto const v = A.code[c];
                                    auto& i = values[v];
                                    if (i.dead || i.constant() || i.op == Phi)
                                        continue;
                                    bool const load = i.load();
                                    if (!pure(i) || (load && effects) || (i.flags & Volatile) != 0u)
                                    {
                                        if (i.store() || i.call() || !pure(i))
                                            effects = true;
                                        continue;
                                    }
                                    auto const& u = users[v];
                                    if (u.empty())
                                        continue;
                                    std::optional<std::size_t> target;
                                    for (std::size_t k = 0u; k < A.successors.size() && !target; ++k)
                                    {
                                        auto const s = A.successors[k];
                                        if (s == b || blocks[s].predecessors.size() != 1u)
                                            continue;
                                        if (std::all_of(u.begin(), u.end(), [&](std::uint32_t aBlock) { return aBlock != b && dominates(s, aBlock); }))
                                            target = k;
                                    }
                                    if (!target)
                                        continue;
                                    auto const s = A.successors[*target];
                                    moved[*target].push_back(v);
                                    A.code[c] = None;
                                    i.block = s;
                                    // (its operands are used there now)
                                    for (auto o : i.operands)
                                        if (o != None)
                                            for (auto& x : users[o])
                                                if (x == b)
                                                {
                                                    x = s;
                                                    break;
                                                }
                                    changed = true;
                                }
                                if (!changed)
                                    continue;
                                std::erase(A.code, None);
                                for (std::size_t k = 0u; k < moved.size(); ++k)
                                {
                                    if (moved[k].empty())
                                        continue;
                                    auto& S = blocks[A.successors[k]].code;
                                    std::size_t phis = 0u;
                                    while (phis < S.size() && values[S[phis]].op == Phi)
                                        ++phis;
                                    // (in their order (they were found last first))
                                    S.insert(S.begin() + static_cast<std::ptrdiff_t>(phis), moved[k].rbegin(), moved[k].rend());
                                }
                            }
                            return changed;
                        }

                        // (an i64 eq or ne of a value shifted right by 48 (its only use: e.g. a NaN box's tag) and a constant (of 16 bits, as 
                        // the shift extends): of the value's top 16 bits (see Top16: no shift's value (a register) is made))
                        void fold_top16_comparisons(function& aFunction)
                        {
                            auto& values = aFunction.values;
                            count_uses(aFunction);
                            for (auto b : aFunction.order)
                                for (auto v : aFunction.blocks[b].code)
                                {
                                    auto& i = values[v];
                                    if (i.dead || (i.op != 0x51u && i.op != 0x52u) || i.operands.size() != 2u || i.flags != 0u)
                                        continue;
                                    for (std::size_t n = 0u; n < 2u; ++n)
                                    {
                                        auto const s = i.operands[n];
                                        auto const c = i.operands[1u - n];
                                        auto const& x = values[s];
                                        if (!values[c].constant() || x.dead || (x.op != 0x87u && x.op != 0x88u) || x.type != I64 || x.operands.size() != 2u ||
                                            aFunction.uses[s] != 1u || !values[x.operands[1]].constant() || (values[x.operands[1]].imm0 & 63u) != 48u ||
                                            values[x.operands[0]].constant())
                                            continue;
                                        auto const k = static_cast<std::int64_t>(values[c].imm0);
                                        bool const fits = x.op == 0x88u ? (k >= 0 && k <= 0xFFFF) : (k >= -0x8000 && k <= 0x7FFF);
                                        if (!fits)
                                            break;
                                        auto const y = x.operands[0];
                                        values[s].dead = true;
                                        i.operands = { y, c };
                                        i.flags = Top16;
                                        break;
                                    }
                                }
                        }

                        // (an unchecked access (of a 64 bit address) whose address is a value shifted left by 1 to 3 bits: the value, scaled
                        // (see Scaled: the same address, as the shift wraps))
                        void fold_scaled_indexes(function& aFunction)
                        {
                            auto& values = aFunction.values;
                            for (auto b : aFunction.order)
                                for (auto v : aFunction.blocks[b].code)
                                {
                                    auto& i = values[v];
                                    if (i.dead || !(i.load() || i.store()) || (i.flags & Unchecked) == 0u || (i.flags & Scaled) != 0u || (i.imm0 >> 32u) == 0u || i.operands.empty())
                                        continue;
                                    auto const& a = values[i.operands[0]];
                                    if (a.dead || a.op != 0x86u || a.type != I64 || a.operands.size() != 2u || !values[a.operands[1]].constant())
                                        continue;
                                    auto const shift = values[a.operands[1]].imm0 & 63u;
                                    if (shift < 1u || shift > 3u || values[a.operands[0]].constant())
                                        continue;
                                    i.flags = static_cast<std::uint8_t>(i.flags | (shift << ScaledShift));
                                    i.operands[0] = a.operands[0];
                                }
                        }

                        // (a store (that cannot trap) that a later store in its block (that cannot trap) to the same address (its base and
                        // the bytes) overwrites with nothing between that may read it, trap or leave (a call, a deopt...): dead)
                        void eliminate_dead_stores(function& aFunction)
                        {
                            auto& values = aFunction.values;
                            static constexpr std::uint8_t sWidths[] = { 4, 8, 4, 8, 1, 1, 2, 2, 1, 1, 2, 2, 4, 4, 4, 8, 4, 8, 1, 2, 1, 2, 4 };
                            struct covered { std::uint64_t memory; value address; std::uint64_t offset; std::uint32_t width; std::size_t memoryClass; };
                            std::vector<covered> later;
                            auto const class_of = [](instruction const& aAccess) { return static_cast<std::size_t>((aAccess.imm0 >> optimizer::MemargClassShift) & optimizer::MemargClassMask); };
                            for (auto b : aFunction.order)
                            {
                                later.clear();
                                auto const& code = aFunction.blocks[b].code;
                                for (auto c = code.size(); c-- > 0u;)
                                {
                                    auto& i = values[code[c]];
                                    if (i.dead)
                                        continue;
                                    if (i.store() && !i.operands.empty())
                                    {
                                        bool const unchecked = (i.flags & Unchecked) != 0u && (i.flags & Volatile) == 0u;
                                        if (!unchecked)
                                        {
                                            later.clear();
                                            continue;
                                        }
                                        auto const memory = i.imm0 >> 32u;
                                        std::uint32_t const width = sWidths[i.op - 0x28u];
                                        bool overwritten = false;
                                        for (auto const& l : later)
                                            if (l.memory == memory && l.address == i.operands[0] && l.offset <= i.imm1 && i.imm1 + width <= l.offset + l.width)
                                            {
                                                overwritten = true;
                                                break;
                                            }
                                        if (overwritten)
                                        {
                                            i.dead = true;
                                            continue;
                                        }
                                        if (later.size() < 64u)
                                            later.push_back(covered{ memory, i.operands[0], i.imm1, width, class_of(i) });
                                        continue;
                                    }
                                    if (i.load())
                                    {
                                        if ((i.flags & Unchecked) == 0u)
                                        {
                                            later.clear();
                                            continue;
                                        }
                                        // (what it may read: not overwritten)
                                        auto const memory = i.imm0 >> 32u;
                                        auto const lc = class_of(i);
                                        std::erase_if(later, [&](covered const& l) { return l.memory == memory && (lc == 0u || l.memoryClass == 0u || l.memoryClass == lc); });
                                        continue;
                                    }
                                    if (pure(i) || i.op == 0x23u || i.op == 0x24u)
                                        continue;
                                    later.clear();
                                }
                            }
                        }

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

                    namespace
                    {
                        // (the values numbered again: those of the code (and the constants that it uses), in their order; the others (dead,
                        // or of blocks that cannot be reached) are no longer values, so that the code generator's tables are the code's size)
                        void compact_values(function& aFunction)
                        {
                            auto& values = aFunction.values;
                            std::vector<value> number(values.size(), None);
                            for (auto& b : aFunction.blocks)
                            {
                                if (!b.reachable)
                                {
                                    b.code.clear();
                                    continue;
                                }
                                for (auto v : b.code)
                                {
                                    number[v] = 0u;
                                    for (auto o : values[v].operands)
                                        if (o != None)
                                            number[o] = 0u;
                                }
                            }
                            value next = 0u;
                            for (std::size_t v = 0u; v < values.size(); ++v)
                                if (number[v] != None)
                                    number[v] = next++;
                            if (next == values.size())
                                return;
                            std::vector<instruction> kept;
                            kept.reserve(next);
                            for (std::size_t v = 0u; v < values.size(); ++v)
                                if (number[v] != None)
                                {
                                    kept.push_back(std::move(values[v]));
                                    for (auto& o : kept.back().operands)
                                        if (o != None)
                                            o = number[o];
                                }
                            values = std::move(kept);
                            for (auto& b : aFunction.blocks)
                                for (auto& v : b.code)
                                    v = number[v];
                            count_uses(aFunction);
                        }
                    }

                    void optimize(function& aFunction, module_description const&, std::function<std::uint64_t(std::uint32_t aMemory)> const& aAccessible,
                        std::function<std::optional<std::uint64_t>(std::uint32_t aMemory, std::uint64_t aAddress, std::uint32_t aWidth)> const& aMemoryWord)
                    {
                        // (the values' uses are counted again only after the code changes (a pass that changes nothing: its counts are current (see
                        // current_uses); eliminate_dead_code keeps them so)
                        auto const changes = [&](bool aChanged) { if (aChanged) aFunction.usesCurrent = false; return aChanged; };
                        auto const cached = [&]() { return aMemoryWord && changes(fold_cached_loads(aFunction, aMemoryWord)); };
                        if (changes(fold_assumed(aFunction)))
                            finish(aFunction);
                        eliminate_dead_code(aFunction);
                        for (int pass = 0; pass < 4; ++pass)
                        {
                            bool changed = cached();
                            changed = changes(fold_known_bits(aFunction)) || changed;
                            changed = changes(fold_constants(aFunction)) || changed;
                            finish(aFunction);
                            changed = changes(simplify(aFunction)) || changed;
                            changed = invert_branches(aFunction) || changed;
                            changed = changes(narrow_masked_comparisons(aFunction)) || changed;
                            if (!changed)
                                break;
                            // (the code without its dead values, for the passes that follow)
                            eliminate_dead_code(aFunction);
                        }
                        if (changes(fold_assumed(aFunction)))
                            finish(aFunction);
                        number_values(aFunction);
                        aFunction.usesCurrent = false;
                        changes(decide_branches(aFunction));
                        for (int pass = 0; pass < 2; ++pass)
                        {
                            bool const changed = changes(fold_constants(aFunction));
                            finish(aFunction);
                            if (!changed)
                                break;
                        }
                        if (changes(merge_blocks(aFunction)))
                        {
                            finish(aFunction);
                            eliminate_dead_code(aFunction);
                        }
                        if (changes(thread_jumps(aFunction)))
                        {
                            finish(aFunction);
                            eliminate_dead_code(aFunction);
                        }
                        if (changes(merge_blocks(aFunction)))
                            finish(aFunction);
                        if (changes(sink_equal_operands(aFunction)))
                        {
                            finish(aFunction);
                            eliminate_dead_code(aFunction);
                            if (changes(thread_jumps(aFunction)))
                                finish(aFunction);
                            eliminate_dead_code(aFunction);
                            number_values(aFunction);
                            aFunction.usesCurrent = false;
                            for (int pass = 0; pass < 2; ++pass)
                            {
                                bool const changed = changes(fold_constants(aFunction));
                                finish(aFunction);
                                if (!changed)
                                    break;
                            }
                        }
                        bound_accesses(aFunction, aAccessible);
                        eliminate_dead_code(aFunction);
                        if (changes(thread_jumps(aFunction)))
                        {
                            finish(aFunction);
                            eliminate_dead_code(aFunction);
                        }
                        for (int round = 0; round < 4; ++round)
                        {
                            bool threaded = changes(thread_branches(aFunction));
                            if (threaded)
                                finish(aFunction);
                            if (changes(thread_known_values(aFunction)))
                            {
                                threaded = true;
                                finish(aFunction);
                            }
                            if (changes(duplicate_tails(aFunction)))
                            {
                                threaded = true;
                                finish(aFunction);
                            }
                            threaded = changes(decide_branches(aFunction)) || threaded;
                            threaded = changes(merge_equal_successors(aFunction)) || threaded;
                            if (changes(sink_conversions(aFunction)))
                            {
                                threaded = true;
                                finish(aFunction);
                            }
                            for (int pass = 0; pass < 2; ++pass)
                            {
                                bool changed = cached();
                                changed = changes(fold_known_bits(aFunction)) || changed;
                                changed = changes(fold_constants(aFunction)) || changed;
                                finish(aFunction);
                                changed = changes(simplify(aFunction)) || changed;
                                if (!changed)
                                    break;
                            }
                            bool const merged = changes(merge_blocks(aFunction));
                            if (merged)
                                finish(aFunction);
                            eliminate_dead_code(aFunction);
                            if (changes(thread_jumps(aFunction)))
                            {
                                finish(aFunction);
                                eliminate_dead_code(aFunction);
                            }
                            if (!threaded && !merged)
                                break;
                        }
                        // (assumes: no code (their conditions' code, only theirs: none))
                        for (auto& i : aFunction.values)
                            if (i.op == Assume)
                                i.dead = true;
                        aFunction.usesCurrent = false;
                        sink_values(aFunction);
                        fold_address_offsets(aFunction);
                        eliminate_dead_stores(aFunction);
                        fold_scaled_indexes(aFunction);
                        fold_top16_comparisons(aFunction);
                        count_uses(aFunction);
                        rematerialize_conditions(aFunction);
                        aFunction.usesCurrent = false;
                        eliminate_dead_code(aFunction);
                        compact_values(aFunction);
                    }
                }
            }
        }
    }
}
