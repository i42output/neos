/*
  jit_optimizer.cpp

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

// The optimizing tier's optimizer (see jit_optimizer.hpp). The code is rewritten in one pass, as it would run: an abstract
// interpretation of the operand stack (each value a constant or not, and the tokens of the instruction that pushed it) and of the
// locals (each a constant or not), so that the order of the code's effects is kept as it is. A constant that an instruction
// consumes is removed (its tokens are deleted: a constant has no effects) when the instruction is folded or resolved.

#include <neos/neos.hpp>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <neos/runtime/plugin.h>
#include <map>
#include "jit_optimizer.hpp"

namespace neos
{
    namespace bytecode
    {
        namespace vm
        {
            namespace jit
            {
                namespace optimizer
                {
                    namespace
                    {
                        constexpr std::uint8_t I32 = 0x7Fu;
                        constexpr std::uint8_t I64 = 0x7Eu;
                        constexpr std::uint8_t F32 = 0x7Du;
                        constexpr std::uint8_t F64 = 0x7Cu;
                        constexpr std::uint8_t NoResult = 0x40u;

                        struct cannot_optimize : std::runtime_error { using std::runtime_error::runtime_error; };

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

                        // (a numeric instruction: its operands' type, their number and its result's type)
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

                        // (a load: its width, result type and whether it sign extends; a store: its width and value's type)
                        struct access
                        {
                            std::uint8_t width;
                            std::uint8_t type;
                            bool sign;
                        };
                        access access_of(std::uint8_t aOp)
                        {
                            static access const sAccesses[] =
                            {
                                { 4, I32, false }, { 8, I64, false }, { 4, F32, false }, { 8, F64, false },
                                { 1, I32, true }, { 1, I32, false }, { 2, I32, true }, { 2, I32, false },
                                { 1, I64, true }, { 1, I64, false }, { 2, I64, true }, { 2, I64, false }, { 4, I64, true }, { 4, I64, false },
                                { 4, I32, false }, { 8, I64, false }, { 4, F32, false }, { 8, F64, false },
                                { 1, I32, false }, { 2, I32, false }, { 1, I64, false }, { 2, I64, false }, { 4, I64, false }
                            };
                            return sAccesses[aOp - 0x28u];
                        }

                        std::uint64_t mask_of(std::uint8_t aType) { return (aType == I32 || aType == F32) ? 0xFFFFFFFFull : ~std::uint64_t{}; }

                        // (an integer instruction's result for constant operands (none if it traps, or is not folded))
                        // (a floating point operation's result (its bits), if its operands are constants: not if it is NaN (its bits are
                        // the code's), nor a conversion that traps)
                        std::optional<std::uint64_t> fold_float(std::uint8_t aOp, std::uint64_t aX, std::uint64_t aY)
                        {
                            auto const f32 = [](std::uint64_t aBits) { return std::bit_cast<float>(static_cast<std::uint32_t>(aBits)); };
                            auto const f64 = [](std::uint64_t aBits) { return std::bit_cast<double>(aBits); };
                            auto const r32 = [](float aValue) -> std::optional<std::uint64_t>
                            {
                                if (std::isnan(aValue))
                                    return {};
                                return std::uint64_t{ std::bit_cast<std::uint32_t>(aValue) };
                            };
                            auto const r64 = [](double aValue) -> std::optional<std::uint64_t>
                            {
                                if (std::isnan(aValue))
                                    return {};
                                return std::bit_cast<std::uint64_t>(aValue);
                            };
                            auto const b = [](bool aValue) { return std::optional<std::uint64_t>{ aValue ? 1u : 0u }; };
                            auto const x32 = f32(aX), y32 = f32(aY);
                            auto const x64 = f64(aX), y64 = f64(aY);
                            // (min and max: not of NaNs or zeros (their signs))
                            auto const ordered32 = !std::isnan(x32) && !std::isnan(y32) && !(x32 == 0.0f && y32 == 0.0f);
                            auto const ordered64 = !std::isnan(x64) && !std::isnan(y64) && !(x64 == 0.0 && y64 == 0.0);
                            // (a truncation: if it is within the result's range)
                            auto const trunc_in = [](double aValue, double aLow, double aHigh) { return !std::isnan(aValue) && std::trunc(aValue) >= aLow && std::trunc(aValue) < aHigh; };
                            switch (aOp)
                            {
                            case 0x5B: return b(x32 == y32);
                            case 0x5C: return b(x32 != y32);
                            case 0x5D: return b(x32 < y32);
                            case 0x5E: return b(x32 > y32);
                            case 0x5F: return b(x32 <= y32);
                            case 0x60: return b(x32 >= y32);
                            case 0x61: return b(x64 == y64);
                            case 0x62: return b(x64 != y64);
                            case 0x63: return b(x64 < y64);
                            case 0x64: return b(x64 > y64);
                            case 0x65: return b(x64 <= y64);
                            case 0x66: return b(x64 >= y64);
                            case 0x8B: return r32(std::fabs(x32));
                            case 0x8C: return r32(-x32);
                            case 0x8D: return r32(std::ceil(x32));
                            case 0x8E: return r32(std::floor(x32));
                            case 0x8F: return r32(std::trunc(x32));
                            case 0x90: return r32(std::nearbyint(x32));
                            case 0x91: return r32(std::sqrt(x32));
                            case 0x92: return r32(x32 + y32);
                            case 0x93: return r32(x32 - y32);
                            case 0x94: return r32(x32 * y32);
                            case 0x95: return r32(x32 / y32);
                            case 0x96: if (!ordered32) return {}; return r32(std::min(x32, y32));
                            case 0x97: if (!ordered32) return {}; return r32(std::max(x32, y32));
                            case 0x98: return r32(std::copysign(x32, y32));
                            case 0x99: return r64(std::fabs(x64));
                            case 0x9A: return r64(-x64);
                            case 0x9B: return r64(std::ceil(x64));
                            case 0x9C: return r64(std::floor(x64));
                            case 0x9D: return r64(std::trunc(x64));
                            case 0x9E: return r64(std::nearbyint(x64));
                            case 0x9F: return r64(std::sqrt(x64));
                            case 0xA0: return r64(x64 + y64);
                            case 0xA1: return r64(x64 - y64);
                            case 0xA2: return r64(x64 * y64);
                            case 0xA3: return r64(x64 / y64);
                            case 0xA4: if (!ordered64) return {}; return r64(std::min(x64, y64));
                            case 0xA5: if (!ordered64) return {}; return r64(std::max(x64, y64));
                            case 0xA6: return r64(std::copysign(x64, y64));
                            case 0xA8: if (!trunc_in(x32, -2147483648.0, 2147483648.0)) return {}; return std::uint64_t{ static_cast<std::uint32_t>(static_cast<std::int32_t>(x32)) };
                            case 0xA9: if (!trunc_in(x32, 0.0, 4294967296.0)) return {}; return std::uint64_t{ static_cast<std::uint32_t>(x32) };
                            case 0xAA: if (!trunc_in(x64, -2147483648.0, 2147483648.0)) return {}; return std::uint64_t{ static_cast<std::uint32_t>(static_cast<std::int32_t>(x64)) };
                            case 0xAB: if (!trunc_in(x64, 0.0, 4294967296.0)) return {}; return std::uint64_t{ static_cast<std::uint32_t>(x64) };
                            case 0xAE: if (!trunc_in(x32, -9223372036854775808.0, 9223372036854775808.0)) return {}; return static_cast<std::uint64_t>(static_cast<std::int64_t>(x32));
                            case 0xAF: if (!trunc_in(x32, 0.0, 18446744073709551616.0)) return {}; return static_cast<std::uint64_t>(x32);
                            case 0xB0: if (!trunc_in(x64, -9223372036854775808.0, 9223372036854775808.0)) return {}; return static_cast<std::uint64_t>(static_cast<std::int64_t>(x64));
                            case 0xB1: if (!trunc_in(x64, 0.0, 18446744073709551616.0)) return {}; return static_cast<std::uint64_t>(x64);
                            case 0xB2: return r32(static_cast<float>(static_cast<std::int32_t>(static_cast<std::uint32_t>(aX))));
                            case 0xB3: return r32(static_cast<float>(static_cast<std::uint32_t>(aX)));
                            case 0xB4: return r32(static_cast<float>(static_cast<std::int64_t>(aX)));
                            case 0xB5: return r32(static_cast<float>(aX));
                            case 0xB6: return r32(static_cast<float>(x64));
                            case 0xB7: return r64(static_cast<double>(static_cast<std::int32_t>(static_cast<std::uint32_t>(aX))));
                            case 0xB8: return r64(static_cast<double>(static_cast<std::uint32_t>(aX)));
                            case 0xB9: return r64(static_cast<double>(static_cast<std::int64_t>(aX)));
                            case 0xBA: return r64(static_cast<double>(aX));
                            case 0xBB: return r64(static_cast<double>(x32));
                            default:
                                return {};
                            }
                        }

                        std::optional<std::uint64_t> fold(std::uint8_t aOp, std::uint64_t aX, std::uint64_t aY)
                        {
                            auto const x32 = static_cast<std::uint32_t>(aX);
                            auto const y32 = static_cast<std::uint32_t>(aY);
                            auto const sx32 = static_cast<std::int32_t>(x32);
                            auto const sy32 = static_cast<std::int32_t>(y32);
                            auto const sx64 = static_cast<std::int64_t>(aX);
                            auto const sy64 = static_cast<std::int64_t>(aY);
                            auto const b = [](bool aValue) { return std::optional<std::uint64_t>{ aValue ? 1u : 0u }; };
                            auto const u32 = [](std::uint32_t aValue) { return std::optional<std::uint64_t>{ aValue }; };
                            switch (aOp)
                            {
                            case 0x45: return b(x32 == 0u);
                            case 0x46: return b(x32 == y32);
                            case 0x47: return b(x32 != y32);
                            case 0x48: return b(sx32 < sy32);
                            case 0x49: return b(x32 < y32);
                            case 0x4A: return b(sx32 > sy32);
                            case 0x4B: return b(x32 > y32);
                            case 0x4C: return b(sx32 <= sy32);
                            case 0x4D: return b(x32 <= y32);
                            case 0x4E: return b(sx32 >= sy32);
                            case 0x4F: return b(x32 >= y32);
                            case 0x50: return b(aX == 0u);
                            case 0x51: return b(aX == aY);
                            case 0x52: return b(aX != aY);
                            case 0x53: return b(sx64 < sy64);
                            case 0x54: return b(aX < aY);
                            case 0x55: return b(sx64 > sy64);
                            case 0x56: return b(aX > aY);
                            case 0x57: return b(sx64 <= sy64);
                            case 0x58: return b(aX <= aY);
                            case 0x59: return b(sx64 >= sy64);
                            case 0x5A: return b(aX >= aY);
                            case 0x67: return u32(static_cast<std::uint32_t>(std::countl_zero(x32)));
                            case 0x68: return u32(static_cast<std::uint32_t>(std::countr_zero(x32)));
                            case 0x69: return u32(static_cast<std::uint32_t>(std::popcount(x32)));
                            case 0x6A: return u32(x32 + y32);
                            case 0x6B: return u32(x32 - y32);
                            case 0x6C: return u32(x32 * y32);
                            case 0x6D:
                                if (y32 == 0u || (sx32 == std::numeric_limits<std::int32_t>::min() && sy32 == -1))
                                    return {};
                                return u32(static_cast<std::uint32_t>(sx32 / sy32));
                            case 0x6E:
                                if (y32 == 0u)
                                    return {};
                                return u32(x32 / y32);
                            case 0x6F:
                                if (y32 == 0u)
                                    return {};
                                return u32(sy32 == -1 ? 0u : static_cast<std::uint32_t>(sx32 % sy32));
                            case 0x70:
                                if (y32 == 0u)
                                    return {};
                                return u32(x32 % y32);
                            case 0x71: return u32(x32 & y32);
                            case 0x72: return u32(x32 | y32);
                            case 0x73: return u32(x32 ^ y32);
                            case 0x74: return u32(x32 << (y32 & 31u));
                            case 0x75: return u32(static_cast<std::uint32_t>(sx32 >> (y32 & 31u)));
                            case 0x76: return u32(x32 >> (y32 & 31u));
                            case 0x77: return u32(std::rotl(x32, static_cast<int>(y32 & 31u)));
                            case 0x78: return u32(std::rotr(x32, static_cast<int>(y32 & 31u)));
                            case 0x79: return static_cast<std::uint64_t>(std::countl_zero(aX));
                            case 0x7A: return static_cast<std::uint64_t>(std::countr_zero(aX));
                            case 0x7B: return static_cast<std::uint64_t>(std::popcount(aX));
                            case 0x7C: return aX + aY;
                            case 0x7D: return aX - aY;
                            case 0x7E: return aX * aY;
                            case 0x7F:
                                if (aY == 0u || (sx64 == std::numeric_limits<std::int64_t>::min() && sy64 == -1))
                                    return {};
                                return static_cast<std::uint64_t>(sx64 / sy64);
                            case 0x80:
                                if (aY == 0u)
                                    return {};
                                return aX / aY;
                            case 0x81:
                                if (aY == 0u)
                                    return {};
                                return sy64 == -1 ? 0u : static_cast<std::uint64_t>(sx64 % sy64);
                            case 0x82:
                                if (aY == 0u)
                                    return {};
                                return aX % aY;
                            case 0x83: return aX & aY;
                            case 0x84: return aX | aY;
                            case 0x85: return aX ^ aY;
                            case 0x86: return aX << (aY & 63u);
                            case 0x87: return static_cast<std::uint64_t>(sx64 >> (aY & 63u));
                            case 0x88: return aX >> (aY & 63u);
                            case 0x89: return std::rotl(aX, static_cast<int>(aY & 63u));
                            case 0x8A: return std::rotr(aX, static_cast<int>(aY & 63u));
                            case 0xA7: return u32(x32);
                            case 0xAC: return static_cast<std::uint64_t>(static_cast<std::int64_t>(sx32));
                            case 0xAD: return static_cast<std::uint64_t>(x32);
                            case 0xBC: return u32(x32);    // (reinterpretations: the same bits)
                            case 0xBD: return aX;
                            case 0xBE: return u32(x32);
                            case 0xBF: return aX;
                            case 0xC0: return u32(static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(x32))));
                            case 0xC1: return u32(static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(x32))));
                            default:
                                return fold_float(aOp, aX, aY);
                            }
                        }

                        // (the speculations (their numbers) that a constant came from: a set in the rewriter's pool (0: none))
                        using provenance = std::uint32_t;

                        // (a local's value, if it is known: a constant)
                        struct known
                        {
                            std::uint64_t bits = 0u;
                            provenance from = 0u;
                        };
                        using knowledge = std::vector<std::optional<known>>;

                        // (a speculation's key: a global's, or a word's (its load and address))
                        using speculation_key = std::pair<std::uint64_t, std::uint64_t>;

                        // (a value on the (abstract) operand stack: a constant (and the tokens that push it) or not)
                        struct slot
                        {
                            std::uint8_t type = I32;
                            bool constant = false;
                            std::uint64_t bits = 0u;
                            provenance from = 0u;
                            std::size_t begin = 0u;     ///< (a constant's tokens: [begin, end))
                            std::size_t end = 0u;
                        };

                        // (an inlined call)
                        struct inline_context
                        {
                            std::uint32_t callee = 0u;
                            std::size_t call = 0u;                      ///< (the call's position (its origin, for the code inlined))
                            std::size_t site = 0u;                      ///< (the call's own position (in the code that makes it: e.g. an inlined call's))
                            std::size_t frame = 0u;                     ///< (its block's index in the frames)
                            std::vector<std::uint32_t> locals;          ///< (the callee's locals (parameters first): the caller's locals)
                            std::vector<std::optional<known>> constantParameters;   ///< (parameters whose arguments are constants, that it does not set)
                            std::uint64_t startEffects = 0u;            ///< (the effects (see rewriter::iEffects) at its start: none since then
                                                                        ///< (memory or global written, a call) if they are the same)
                            bool parametersSet = false;                 ///< (it sets a parameter (its call cannot be made again: no deopt))
                            bool outerEffectFree = true;                ///< (the outermost inlined call had had no effect when it started)
                            std::uint32_t depth = 0u;                   ///< (the operand stack's height below its arguments)
                            std::vector<std::uint64_t> guards;          ///< (the guards' code)
                            std::size_t guardCount = 0u;
                            std::optional<std::uint32_t> indirect;      ///< (a call_indirect's (its index a constant: the callee's): the index)
                        };

                        struct frame
                        {
                            enum kind_t { Function, Block, Loop, If, ConstantIf, Inline } kind = Block;
                            std::uint8_t result = NoResult;
                            std::size_t height = 0u;                    ///< (the operand stack's height at its start)
                            bool branched = false;                      ///< (something branches to its end)
                            bool liveArm = true;                        ///< (ConstantIf: the arm that is (true: then, false: else) kept)
                            bool inElse = false;
                            std::vector<std::uint32_t> written;         ///< (locals it sets)
                            knowledge entry;            ///< (If: the locals' constants at its start)
                            knowledge thenExit;
                            bool thenReachable = false;
                            std::optional<std::size_t> inlined;         ///< (Inline: its inline_context)
                            std::uint64_t startEffects = 0u;            ///< (the effects at its start (see rewriter::iEffects))
                            std::uint64_t branchEffects = 0u;           ///< (the most of the branches to it)
                            std::uint64_t thenEffects = 0u;             ///< (If: at its then arm's end)
                            // (its result (not a loop's), if every path that reaches its end has the same constant (an integer) for it)
                            std::optional<std::uint64_t> resultBits;
                            bool resultVaries = false;
                            std::uint32_t resultFrom = 0u;              ///< (provenance)
                        };

                        class rewriter
                        {
                        public:
                            rewriter(module_description const& aModule, std::uint32_t aFunction, options const& aOptions,
                                std::function<bool(std::uint32_t)> const& aInlinable,
                                std::function<std::optional<std::uint64_t>(std::uint32_t, std::uint64_t, std::uint32_t)> const& aMemoryWord,
                                std::set<speculation_key> const* aAllowed) :
                                iModule{ aModule }, iTokens{ *aModule.tokens }, iFunction{ aFunction }, iOptions{ aOptions },
                                iInlinable{ aInlinable }, iMemoryWord{ aMemoryWord }, iAllowed{ aAllowed }
                            {
                            }
                            std::set<speculation_key> const& useful() const { return iUseful; }
                            std::size_t speculations() const { return iSpeculations.size(); }
                        public:
                            result run()
                            {
                                auto const& f = iModule.functions[iFunction];
                                for (auto t : *f.parameters)
                                    iLocalTypes.push_back(t);
                                for (auto t : *f.locals)
                                    iLocalTypes.push_back(t);
                                iConstants.assign(iLocalTypes.size(), std::nullopt);
                                for (std::size_t i = f.parameters->size(); i < iLocalTypes.size(); ++i)
                                    iConstants[i] = known{};     // (a local (not a parameter) starts as 0)
                                iParameters = f.parameters->size();
                                // (inlined calls refer to their contexts while calls within them are inlined: they do not move)
                                iInlines.reserve(iOptions.maxInlineDepth + 1u);
                                iMaxTokens = std::min(iOptions.maxTokens, (f.codeEnd - f.codeBegin) * iOptions.maxGrowth + 4000u);
                                frame fn;
                                fn.kind = frame::Function;
                                fn.result = f.results->empty() ? NoResult : (*f.results)[0];
                                iFrames.push_back(fn);
                                std::vector<std::uint32_t> identity(iLocalTypes.size());
                                for (std::uint32_t i = 0u; i < identity.size(); ++i)
                                    identity[i] = i;
                                process(f.codeBegin, f.codeEnd, nullptr, identity);
                                if (!iFrames.empty())
                                    throw cannot_optimize{ "unterminated function" };
                                dead_stores();
                                result r;
                                r.tokens.reserve(iOut.size());
                                r.origins.reserve(iOut.size());
                                for (std::size_t i = 0u; i < iOut.size(); ++i)
                                    if (!iDeleted[i])
                                    {
                                        r.tokens.push_back(iOut[i]);
                                        r.origins.push_back(iOrigins[i]);
                                    }
                                r.chains = std::move(iChains);
                                r.locals.assign(iLocalTypes.begin() + static_cast<std::ptrdiff_t>(iParameters), iLocalTypes.end());
                                r.sites = std::move(iSites);
                                r.deopts = std::move(iDeopts);
                                r.inlined = iInlined;
                                r.folded = iFolded;
                                r.guards = iGuards;
                                return r;
                            }
                        private:
                            // --- output
                            std::size_t emit(std::uint64_t aToken)
                            {
                                iOut.push_back(aToken);
                                iOrigins.push_back(iOrigin);
                                iDeleted.push_back(false);
                                if (iOut.size() > iMaxTokens)
                                    iInliningOff = true;
                                return iOut.size() - 1u;
                            }
                            void erase(std::size_t aBegin, std::size_t aEnd)
                            {
                                for (auto i = aBegin; i < aEnd; ++i)
                                    iDeleted[i] = true;
                            }
                            static std::uint64_t const_token(std::uint8_t aType, std::uint64_t aBits)
                            {
                                if (aType == I32)
                                    return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(static_cast<std::uint32_t>(aBits))));
                                if (aType == F32)
                                    return aBits & 0xFFFFFFFFull;
                                return aBits;
                            }
                            static std::uint8_t const_op(std::uint8_t aType)
                            {
                                switch (aType)
                                {
                                case I32: return 0x41u;
                                case I64: return 0x42u;
                                case F32: return 0x43u;
                                default: return 0x44u;
                                }
                            }
                            // (provenances)
                            provenance single(std::uint32_t aSpeculation)
                            {
                                iPool.push_back({ aSpeculation });
                                return static_cast<provenance>(iPool.size() - 1u);
                            }
                            provenance merged(provenance aX, provenance aY)
                            {
                                if (aX == 0u || aX == aY)
                                    return aY;
                                if (aY == 0u)
                                    return aX;
                                std::vector<std::uint32_t> both = iPool[aX];
                                both.insert(both.end(), iPool[aY].begin(), iPool[aY].end());
                                std::sort(both.begin(), both.end());
                                both.erase(std::unique(both.begin(), both.end()), both.end());
                                iPool.push_back(std::move(both));
                                return static_cast<provenance>(iPool.size() - 1u);
                            }
                            void push_constant(std::uint8_t aType, std::uint64_t aBits, provenance aFrom = 0u)
                            {
                                slot s;
                                s.type = aType;
                                s.constant = true;
                                s.bits = aBits & mask_of(aType);
                                s.from = std::move(aFrom);
                                s.begin = emit(const_op(aType));
                                emit(const_token(aType, s.bits));
                                s.end = iOut.size();
                                iStack.push_back(s);
                            }
                            void push(std::uint8_t aType)
                            {
                                slot s;
                                s.type = aType;
                                iStack.push_back(s);
                            }
                            slot pop()
                            {
                                if (iStack.size() <= iFrames.back().height)
                                    throw cannot_optimize{ "operand stack underflow" };
                                auto s = iStack.back();
                                iStack.pop_back();
                                return s;
                            }
                            void unwind()
                            {
                                iStack.resize(std::min(iStack.size(), iFrames.back().height));
                            }
                            // --- locals
                            void written(std::uint32_t aLocal)
                            {
                                for (auto& f : iFrames)
                                    f.written.push_back(aLocal);
                            }
                            void forget_written(frame const& aFrame)
                            {
                                for (auto l : aFrame.written)
                                    iConstants[l] = std::nullopt;
                            }
                            std::uint32_t new_local(std::uint8_t aType)
                            {
                                iLocalTypes.push_back(aType);
                                iConstants.push_back(std::nullopt);
                                return static_cast<std::uint32_t>(iLocalTypes.size() - 1u);
                            }
                            // (a constant that came from speculations has been folded: they are useful)
                            void used(provenance aFrom)
                            {
                                if (aFrom == 0u)
                                    return;
                                for (auto const s : iPool[aFrom])
                                    iUseful.insert(iSpeculations[s]);
                            }
                            std::optional<std::uint32_t> speculation(speculation_key const& aKey)
                            {
                                if (iAllowed != nullptr && !iAllowed->contains(aKey))
                                    return {};
                                iSpeculations.push_back(aKey);
                                return static_cast<std::uint32_t>(iSpeculations.size() - 1u);
                            }
                            // --- effects (an inlined call's guards are checked at its start: they can be only of what it reads before
                            // anything that it does could change it)
                            void effect()
                            {
                                ++iEffects;
                            }
                            bool effect_free(inline_context const& aContext) const { return iEffects == aContext.startEffects; }
                            // (a branch to a frame: the effects of the paths that reach its end)
                            void branches_to(frame& aFrame)
                            {
                                aFrame.branchEffects = std::max(aFrame.branchEffects, iEffects);
                                result_to(aFrame);
                            }
                            // (a path to a frame's end (a branch to it, or its end): its result's value (the operand stack's top))
                            void result_to(frame& aFrame)
                            {
                                if (aFrame.kind == frame::Loop || aFrame.result == NoResult || aFrame.resultVaries)
                                    return;
                                if (iStack.size() <= aFrame.height || !iStack.back().constant || (aFrame.result != I32 && aFrame.result != I64))
                                {
                                    aFrame.resultVaries = true;
                                    return;
                                }
                                auto const& top = iStack.back();
                                if (!aFrame.resultBits)
                                {
                                    aFrame.resultBits = top.bits;
                                    aFrame.resultFrom = top.from;
                                }
                                else if (*aFrame.resultBits != top.bits)
                                    aFrame.resultVaries = true;
                                else
                                    aFrame.resultFrom = merged(aFrame.resultFrom, top.from);
                            }
                            // (deopt: whether the code can leave here for the function's own code (which makes the outermost inlined call 
                            // (aOutermost))
                            bool can_deopt(inline_context const& aOutermost, bool aEffectFree) const
                            {
                                return iOptions.deopt && aEffectFree && !aOutermost.parametersSet && iDeopts.size() < 60000u;
                            }
                            // (its code: the outermost inlined call's arguments, then the deopt's number, and the intrinsic (it does not return))
                            void emit_deopt(inline_context const& aOutermost, bool aGuard)
                            {
                                auto const arguments = emit_deopt_call(aOutermost);
                                auto const& f = iModule.functions[iFunction];
                                iDeopts.push_back(deopt{ aOutermost.call, aOutermost.callee, arguments, aOutermost.depth,
                                    static_cast<std::uint32_t>(f.parameters->size() + f.locals->size()), aGuard });
                            }
                            // (the deopt's code: the inlined call's arguments (a call_indirect's: then its index), the deopt's number (the next),
                            // and the intrinsic (it does not return)): the arguments' number
                            std::uint32_t emit_deopt_call(inline_context const& aCall)
                            {
                                auto const& parameters = *iModule.functions[aCall.callee].parameters;
                                for (std::size_t i = 0u; i < parameters.size(); ++i)
                                {
                                    if (aCall.constantParameters[i])
                                    {
                                        emit(const_op(parameters[i]));
                                        emit(const_token(parameters[i], aCall.constantParameters[i]->bits));
                                    }
                                    else
                                    {
                                        emit(0x20u);
                                        emit(aCall.locals[i]);
                                    }
                                }
                                if (aCall.indirect)
                                {
                                    emit(0x41u);
                                    emit(*aCall.indirect);
                                }
                                emit(0x41u);
                                emit(iDeopts.size());
                                emit(0x10u);
                                emit(intrinsics::deopt(iModule));
                                emit(0x00u);
                                return static_cast<std::uint32_t>(parameters.size() + (aCall.indirect ? 1u : 0u));
                            }
                            // (a call within inlined calls of functions without frames of their own (e.g. a language's helpers' slow paths)
                            // that are within inlined calls of functions with frames of their own (e.g. the language's functions) that have
                            // had effects: if the innermost of the calls without frames has had none, the code leaves for the frames of the
                            // function and of the calls with frames (see deopt::frames): the innermost makes that call)
                            bool emit_frames_deopt(bool aAtStart = false)
                            {
                                if (!iOptions.deopt || !iOptions.deoptFrame || iInlines.size() < 2u || iDeopts.size() >= 60000u)
                                    return false;
                                std::size_t framed = 0u;
                                while (framed < iInlines.size() && !iModule.functions[iInlines[framed].callee].frameless)
                                    ++framed;
                                if (framed == 0u || framed == iInlines.size())
                                    return false;
                                for (auto i = framed; i < iInlines.size(); ++i)
                                    if (!iModule.functions[iInlines[i].callee].frameless)
                                        return false;
                                auto const& made = iInlines[framed];
                                // (at its start (aAtStart), a call that sets its parameters has not yet: it can be made again)
                                if (!effect_free(made) || (made.parametersSet && !aAtStart) || iOptions.noDeopt.contains(made.site) || iOptions.noDeopt.contains(iInlines[0].call))
                                    return false;
                                auto const arguments_of = [&](inline_context const& aCall)
                                {
                                    return static_cast<std::uint32_t>(iModule.functions[aCall.callee].parameters->size() + (aCall.indirect ? 1u : 0u));
                                };
                                auto const& f = iModule.functions[iFunction];
                                auto const ownLocals = static_cast<std::uint32_t>(f.parameters->size() + f.locals->size());
                                std::vector<deopt::frame> frames;
                                deopt::frame own;
                                own.function = iFunction;
                                own.position = iInlines[0].site;
                                own.arguments = arguments_of(iInlines[0]);
                                own.depth = iInlines[0].depth;
                                for (std::uint32_t l = 0u; l < ownLocals; ++l)
                                    own.locals.push_back(l);
                                own.constants.assign(ownLocals, 0u);
                                frames.push_back(std::move(own));
                                for (std::size_t j = 0u; j < framed; ++j)
                                {
                                    auto const& c = iInlines[j];
                                    auto const& next = iInlines[j + 1u];
                                    if (next.depth < c.depth)
                                        return false;
                                    deopt::frame frame;
                                    frame.function = c.callee;
                                    frame.position = next.site;
                                    frame.arguments = arguments_of(next);
                                    frame.depth = next.depth - c.depth;
                                    frame.locals = c.locals;
                                    frame.constants.assign(c.locals.size(), 0u);
                                    for (std::size_t i = 0u; i < c.locals.size(); ++i)
                                        if (c.locals[i] == NoLocal)
                                        {
                                            if (i >= c.constantParameters.size() || !c.constantParameters[i])
                                                return false;
                                            frame.constants[i] = c.constantParameters[i]->bits;
                                        }
                                    frames.push_back(std::move(frame));
                                }
                                // (locals whose values are known constants here: the constants (their code may have been removed))
                                for (auto& frame : frames)
                                    for (std::size_t l = 0u; l < frame.locals.size(); ++l)
                                        if (auto const local = frame.locals[l]; local != NoLocal && local < iConstants.size() && iConstants[local])
                                        {
                                            frame.constants[l] = iConstants[local]->bits;
                                            frame.locals[l] = NoLocal;
                                        }
                                std::size_t values = arguments_of(made);
                                for (auto const& frame : frames)
                                {
                                    values += frame.locals.size() + frame.depth;
                                    if (!iOptions.deoptFrame(frame.function, frame.position, frame.arguments, frame.depth, static_cast<std::uint32_t>(frame.locals.size())))
                                        return false;
                                }
                                if (values > iOptions.maxDeoptValues)
                                    return false;
                                auto const arguments = emit_deopt_call(made);
                                deopt d{ made.site, made.callee, arguments, made.depth, ownLocals, false };
                                d.frames = std::move(frames);
                                iDeopts.push_back(std::move(d));
                                return true;
                            }
                            inline_context* speculating()
                            {
                                if (!iOptions.speculation || iInlines.empty() || !effect_free(iInlines.back()))
                                    return nullptr;
                                // (not within a loop of the inlined code (its guards are checked once))
                                for (auto f = iFrames.size(); f-- > iInlines.back().frame;)
                                    if (iFrames[f].kind == frame::Loop)
                                        return nullptr;
                                return &iInlines.back();
                            }
                            // --- the code
                            std::size_t end_of(std::size_t aPosition, std::size_t aEnd) const
                            {
                                // (aPosition: after a block, loop or if: the position of its end)
                                std::size_t depth = 0u;
                                for (auto p = aPosition; p < aEnd;)
                                {
                                    auto const op = static_cast<std::uint8_t>(iTokens[p]);
                                    if (op == 0x02u || op == 0x03u || op == 0x04u)
                                        ++depth;
                                    else if (op == 0x0Bu)
                                    {
                                        if (depth == 0u)
                                            return p;
                                        --depth;
                                    }
                                    p += 1u + immediates(op, iTokens, p);
                                }
                                throw cannot_optimize{ "unterminated block" };
                            }
                            void loop_writes(std::size_t aBegin, std::size_t aEnd, std::vector<std::uint32_t> const& aLocals)
                            {
                                // (the locals that a loop sets: not constants at its start (it can be entered again))
                                for (auto p = aBegin; p < aEnd;)
                                {
                                    auto const op = static_cast<std::uint8_t>(iTokens[p]);
                                    if (op == 0x21u || op == 0x22u)
                                    {
                                        auto const l = static_cast<std::size_t>(iTokens[p + 1u]);
                                        if (l < aLocals.size() && aLocals[l] != NoLocal)
                                            iConstants[aLocals[l]] = std::nullopt;
                                    }
                                    else if (op == 0x10u)
                                    {
                                        // (an inlined call within it sets its own (new) locals at its start)
                                    }
                                    p += 1u + immediates(op, iTokens, p);
                                }
                            }
                            static constexpr std::uint32_t NoLocal = ~std::uint32_t{};
                            std::uint32_t local_of(std::vector<std::uint32_t> const& aLocals, std::uint64_t aIndex) const
                            {
                                if (aIndex >= aLocals.size())
                                    throw cannot_optimize{ "invalid local index" };
                                return aLocals[static_cast<std::size_t>(aIndex)];
                            }
                            std::uint8_t block_type(std::uint64_t aType) const
                            {
                                auto const t = static_cast<std::uint8_t>(aType);
                                if (aType == NoResult || t == I32 || t == I64 || t == F32 || t == F64)
                                    return static_cast<std::uint8_t>(aType);
                                throw cannot_optimize{ "block type" };
                            }
                            void begin_frame(frame::kind_t aKind, std::uint8_t aResult)
                            {
                                frame f;
                                f.kind = aKind;
                                f.result = aResult;
                                f.height = iStack.size();
                                f.startEffects = iEffects;
                                iFrames.push_back(std::move(f));
                            }
                            frame& target(std::uint64_t aDepth)
                            {
                                if (aDepth >= iFrames.size())
                                    throw cannot_optimize{ "invalid branch depth" };
                                return iFrames[iFrames.size() - 1u - static_cast<std::size_t>(aDepth)];
                            }
                            // (a frame's end: after its code (aReachable: whether its code's end is reached))
                            void end_frame(bool aReachable)
                            {
                                if (aReachable)
                                    result_to(iFrames.back());
                                auto f = std::move(iFrames.back());
                                iFrames.pop_back();
                                iStack.resize(std::min(iStack.size(), f.height));
                                bool reachable = aReachable;
                                if (f.kind == frame::If)
                                {
                                    // (the locals' constants: those that both arms (or the then arm and the if's start) end with)
                                    knowledge const& other = f.inElse ? f.thenExit : f.entry;
                                    bool const otherReachable = f.inElse ? f.thenReachable : true;
                                    if (!aReachable)
                                    {
                                        auto const size = iConstants.size();
                                        iConstants = other;
                                        iConstants.resize(size);    // (locals made since its start: not known)
                                    }
                                    else if (otherReachable)
                                        for (std::size_t i = 0u; i < iConstants.size(); ++i)
                                            if (i >= other.size() || !iConstants[i] || !other[i] || iConstants[i]->bits != other[i]->bits)
                                                iConstants[i] = std::nullopt;
                                            else
                                                iConstants[i]->from = merged(iConstants[i]->from, other[i]->from);
                                    reachable = aReachable || otherReachable;
                                }
                                if (f.branched)
                                {
                                    forget_written(f);
                                    if (f.kind != frame::Loop)
                                        reachable = true;
                                }
                                if (f.kind != frame::Loop)
                                {
                                    // (the effects of the paths that reach its end: its end's, its branches', and (an if) its other arm's)
                                    std::uint64_t effects = f.branched ? f.branchEffects : 0u;
                                    if (aReachable)
                                        effects = std::max(effects, iEffects);
                                    if (f.kind == frame::If)
                                        effects = std::max(effects, f.inElse ? (f.thenReachable ? f.thenEffects : 0u) : f.startEffects);
                                    iEffects = effects;
                                }
                                iReachable = reachable;
                                if (reachable && f.result != NoResult)
                                {
                                    if (f.kind != frame::Loop && !f.resultVaries && f.resultBits)
                                    {
                                        // (every path has the same constant for its result: the constant (its code's value dropped))
                                        emit(0x1Au);
                                        push_constant(f.result, *f.resultBits, f.resultFrom);
                                    }
                                    else
                                        push(f.result);
                                }
                            }
                            void skip_to_end_or_else(std::size_t& aPosition, std::size_t aEnd, bool aStopAtElse)
                            {
                                // (unreachable code: skipped, up to the end (or else) of the code's block)
                                std::size_t depth = 0u;
                                while (aPosition < aEnd)
                                {
                                    auto const op = static_cast<std::uint8_t>(iTokens[aPosition]);
                                    if (op == 0x02u || op == 0x03u || op == 0x04u)
                                        ++depth;
                                    else if (op == 0x0Bu || (op == 0x05u && aStopAtElse))
                                    {
                                        if (depth == 0u)
                                            return;
                                        if (op == 0x0Bu)
                                            --depth;
                                    }
                                    aPosition += 1u + immediates(op, iTokens, aPosition);
                                }
                            }
                            // (returns whether the code's end is reached)
                            bool process(std::size_t aBegin, std::size_t aEnd, inline_context* aInline, std::vector<std::uint32_t> const& aLocals)
                            {
                                auto const depthBase = iFrames.size();  // (the frames of this code: those above its own (the function's, or the inlined call's))
                                for (std::size_t p = aBegin; p < aEnd;)
                                {
                                    auto const op = static_cast<std::uint8_t>(iTokens[p]);
                                    auto const position = p;
                                    auto const next = p + 1u + immediates(op, iTokens, p);
                                    auto const imm = [&](std::size_t aIndex) { return iTokens[position + 1u + aIndex]; };
                                    if (aInline == nullptr)
                                        iOrigin = position;
                                    else if (iFramedInlines != 0u)
                                        iOrigin = chain_origin(position);
                                    if (!iReachable)
                                    {
                                        // (after a branch: up to the end (or else) of its block)
                                        skip_to_end_or_else(p, aEnd, true);
                                        if (p >= aEnd)
                                            break;
                                        iReachable = true;   // (reached: the else or end that follows is processed)
                                        iUnreachableEnd = true;
                                        continue;
                                    }
                                    bool const unreachableEnd = std::exchange(iUnreachableEnd, false);
                                    p = next;
                                    switch (op)
                                    {
                                    case 0x00:
                                        emit(op);
                                        unwind();
                                        iReachable = false;
                                        break;
                                    case 0x01:
                                        break;
                                    case 0x02:
                                        emit(op);
                                        emit(imm(0));
                                        begin_frame(frame::Block, block_type(imm(0)));
                                        break;
                                    case 0x03:
                                        {
                                            auto const loopEnd = end_of(p, aEnd);
                                            loop_writes(p, loopEnd, aLocals);
                                            // (its start can be reached again after its effects)
                                            for (auto q = p; q < loopEnd;)
                                            {
                                                auto const o = static_cast<std::uint8_t>(iTokens[q]);
                                                if ((o >= 0x36u && o <= 0x3Eu) || o == 0x24u || o == 0x10u || o == 0x11u)
                                                {
                                                    effect();
                                                    break;
                                                }
                                                q += 1u + immediates(o, iTokens, q);
                                            }
                                            emit(op);
                                            emit(imm(0));
                                            begin_frame(frame::Loop, block_type(imm(0)));
                                        }
                                        break;
                                    case 0x04:
                                        {
                                            auto const condition = pop();
                                            auto const type = block_type(imm(0));
                                            if (condition.constant)
                                            {
                                                // (a constant condition: a block of the arm that runs)
                                                used(condition.from);
                                                erase(condition.begin, condition.end);
                                                ++iFolded;
                                                emit(0x02u);
                                                emit(type);
                                                begin_frame(frame::ConstantIf, type);
                                                iFrames.back().liveArm = (condition.bits & 0xFFFFFFFFu) != 0u;
                                                if (!iFrames.back().liveArm)
                                                {
                                                    // (the then arm: skipped (to the else, or the end))
                                                    skip_to_end_or_else(p, aEnd, true);
                                                    if (p < aEnd && static_cast<std::uint8_t>(iTokens[p]) == 0x05u)
                                                    {
                                                        iFrames.back().inElse = true;
                                                        p += 1u;
                                                    }
                                                }
                                                break;
                                            }
                                            emit(op);
                                            emit(type);
                                            begin_frame(frame::If, type);
                                            iFrames.back().entry = iConstants;
                                        }
                                        break;
                                    case 0x05:
                                        {
                                            auto& f = iFrames.back();
                                            if (f.kind == frame::ConstantIf)
                                            {
                                                // (the then arm (kept) has ended: the else arm is skipped)
                                                skip_to_end_or_else(p, aEnd, false);
                                                iReachable = !unreachableEnd;
                                                break;
                                            }
                                            if (f.kind != frame::If)
                                                throw cannot_optimize{ "else without if" };
                                            if (!unreachableEnd)
                                                result_to(f);
                                            f.thenExit = iConstants;
                                            f.thenReachable = !unreachableEnd;
                                            f.thenEffects = iEffects;
                                            iEffects = f.startEffects;
                                            f.inElse = true;
                                            iConstants = f.entry;
                                            iConstants.resize(iLocalTypes.size());   // (locals made since its start (by the then arm): not known)
                                            unwind();
                                            emit(op);
                                        }
                                        break;
                                    case 0x0B:
                                        {
                                            if (iFrames.size() == depthBase)
                                            {
                                                // (the end of this code: the function's, or the inlined call's (its block ends it))
                                                if (aInline == nullptr)
                                                {
                                                    emit(op);
                                                    iFrames.pop_back();
                                                }
                                                return !unreachableEnd;
                                            }
                                            emit(op);
                                            end_frame(!unreachableEnd);
                                        }
                                        break;
                                    case 0x0C:
                                        {
                                            auto& t = target(imm(0));
                                            t.branched = true;
                                            branches_to(t);
                                            emit(op);
                                            emit(imm(0));
                                            unwind();
                                            iReachable = false;
                                        }
                                        break;
                                    case 0x0D:
                                        {
                                            auto const condition = pop();
                                            if (condition.constant)
                                            {
                                                used(condition.from);
                                                erase(condition.begin, condition.end);
                                                ++iFolded;
                                                if ((condition.bits & 0xFFFFFFFFu) == 0u)
                                                    break;      // (never taken)
                                                target(imm(0)).branched = true;
                                                branches_to(target(imm(0)));
                                                emit(0x0Cu);
                                                emit(imm(0));
                                                unwind();
                                                iReachable = false;
                                                break;
                                            }
                                            target(imm(0)).branched = true;
                                            branches_to(target(imm(0)));
                                            emit(op);
                                            emit(imm(0));
                                            // (its value (if its target has one) stays on the stack: not a constant now (its tokens are not
                                            // removed: the branch uses it))
                                            if (target(imm(0)).kind != frame::Loop && target(imm(0)).result != NoResult && !iStack.empty())
                                                iStack.back().constant = false;
                                        }
                                        break;
                                    case 0x0E:
                                        {
                                            pop();
                                            auto const count = static_cast<std::size_t>(imm(0));
                                            emit(op);
                                            emit(imm(0));
                                            for (std::size_t i = 0u; i <= count; ++i)
                                            {
                                                target(imm(1u + i)).branched = true;
                                                branches_to(target(imm(1u + i)));
                                                emit(imm(1u + i));
                                            }
                                            unwind();
                                            iReachable = false;
                                        }
                                        break;
                                    case 0x0F:
                                        if (aInline != nullptr)
                                        {
                                            // (the inlined call's return: a branch to its block's end)
                                            auto const depth = iFrames.size() - 1u - aInline->frame;
                                            iFrames[aInline->frame].branched = true;
                                            branches_to(iFrames[aInline->frame]);
                                            emit(0x0Cu);
                                            emit(depth);
                                        }
                                        else
                                            emit(op);
                                        unwind();
                                        iReachable = false;
                                        break;
                                    case 0x10:
                                        call(static_cast<std::uint32_t>(imm(0)), position, aInline);
                                        break;
                                    case 0x11:
                                        {
                                            auto const type = static_cast<std::size_t>(imm(0));
                                            if (type >= iModule.types.size())
                                                throw cannot_optimize{ "invalid type" };
                                            auto const index = pop();
                                            // (the table is the functions: a constant index of a function of the type is a call of it)
                                            if (index.constant && index.bits < iModule.functions.size() && !iModule.functions[index.bits].imported &&
                                                iModule.functions[index.bits].type == type)
                                            {
                                                erase(index.begin, index.end);
                                                used(index.from);
                                                ++iFolded;
                                                iIndirect = static_cast<std::uint32_t>(index.bits);
                                                call(static_cast<std::uint32_t>(index.bits), position, aInline);
                                                iIndirect.reset();
                                                break;
                                            }
                                            for (std::size_t i = 0u; i < iModule.types[type].first->size(); ++i)
                                                pop();
                                            emit(op);
                                            emit(imm(0));
                                            emit(imm(1));
                                            effect();
                                            for (auto t : *iModule.types[type].second)
                                                push(t);
                                        }
                                        break;
                                    case 0x1A:
                                        {
                                            auto const v = pop();
                                            if (v.constant)
                                                erase(v.begin, v.end);
                                            else
                                                emit(op);
                                        }
                                        break;
                                    case 0x1B:
                                        {
                                            auto const c = pop();
                                            auto const y = pop();
                                            auto const x = pop();
                                            if (c.constant && x.constant && y.constant)
                                            {
                                                erase(x.begin, x.end);
                                                erase(y.begin, y.end);
                                                erase(c.begin, c.end);
                                                ++iFolded;
                                                used(c.from);
                                                auto const& chosen = (c.bits & 0xFFFFFFFFu) != 0u ? x : y;
                                                push_constant(chosen.type, chosen.bits, chosen.from);
                                                break;
                                            }
                                            emit(op);
                                            push(x.type);
                                        }
                                        break;
                                    case 0x20:
                                        {
                                            if (aInline != nullptr && imm(0) < aInline->constantParameters.size() && aInline->constantParameters[static_cast<std::size_t>(imm(0))])
                                            {
                                                auto const& parameters = *iModule.functions[aInline->callee].parameters;
                                                auto const& k = *aInline->constantParameters[static_cast<std::size_t>(imm(0))];
                                                push_constant(parameters[static_cast<std::size_t>(imm(0))], k.bits, k.from);
                                                break;
                                            }
                                            auto const l = local_of(aLocals, imm(0));
                                            if (iConstants[l])
                                            {
                                                push_constant(iLocalTypes[l], iConstants[l]->bits, iConstants[l]->from);
                                                ++iFolded;
                                                break;
                                            }
                                            emit(op);
                                            emit(l);
                                            push(iLocalTypes[l]);
                                        }
                                        break;
                                    case 0x21:
                                    case 0x22:
                                        {
                                            auto const l = local_of(aLocals, imm(0));
                                            if (l == NoLocal)
                                                throw cannot_optimize{ "a constant parameter is set" };
                                            auto v = pop();
                                            iConstants[l] = v.constant ? std::optional<known>{ known{ v.bits, v.from } } : std::nullopt;
                                            written(l);
                                            // (a constant's tee: its set, and the constant (again))
                                            auto const at = emit(v.constant ? 0x21u : op);
                                            emit(l);
                                            if (v.constant)
                                                iConstantSets.push_back(constant_set{ l, at, v.begin, v.end });
                                            if (op == 0x22u)
                                            {
                                                if (v.constant)
                                                    push_constant(v.type, v.bits, v.from);
                                                else
                                                    iStack.push_back(v);
                                            }
                                        }
                                        break;
                                    case 0x23:
                                        {
                                            auto const g = static_cast<std::size_t>(imm(0));
                                            if (g >= iModule.globals->size() || g >= iModule.globalTypes->size())
                                                throw cannot_optimize{ "invalid global" };
                                            auto const type = (*iModule.globalTypes)[g];
                                            if (iModule.fixedGlobals != nullptr && g < iModule.fixedGlobals->size() && (*iModule.fixedGlobals)[g] != 0u &&
                                                type != F32 && type != F64)
                                            {
                                                // (a global whose value does not change again: its value)
                                                ++iFolded;
                                                push_constant(type, (*iModule.globals)[g] & mask_of(type));
                                                break;
                                            }
                                            std::optional<std::uint32_t> id;
                                            if (auto* c = speculating(); c != nullptr && type != F32 && type != F64 && (id = speculation({ 1u, g })))
                                            {
                                                // (its value now: a guard checks that it has it)
                                                auto const value = (*iModule.globals)[g] & mask_of(type);
                                                c->guards.push_back(0x23u);
                                                c->guards.push_back(g);
                                                c->guards.push_back(const_op(type));
                                                c->guards.push_back(const_token(type, value));
                                                c->guards.push_back(type == I32 ? 0x47u : 0x52u);
                                                c->guards.push_back(0x0Du);
                                                c->guards.push_back(0u);    // (to the call (see inline_call))
                                                ++c->guardCount;
                                                ++iGuards;
                                                push_constant(type, value, single(*id));
                                                break;
                                            }
                                            emit(op);
                                            emit(imm(0));
                                            push(type);
                                        }
                                        break;
                                    case 0x24:
                                        pop();
                                        emit(op);
                                        emit(imm(0));
                                        effect();
                                        break;
                                    case 0x41:
                                        push_constant(I32, static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int64_t>(imm(0)))));
                                        break;
                                    case 0x42:
                                        push_constant(I64, imm(0));
                                        break;
                                    case 0x43:
                                        push_constant(F32, imm(0) & 0xFFFFFFFFull);
                                        break;
                                    case 0x44:
                                        push_constant(F64, imm(0));
                                        break;
                                    default:
                                        if (op >= 0x28u && op <= 0x35u)
                                            load(op, imm(0), imm(1));
                                        else if (op >= 0x36u && op <= 0x3Eu)
                                        {
                                            pop();
                                            pop();
                                            emit(op);
                                            emit(imm(0));
                                            emit(imm(1));
                                            effect();
                                        }
                                        else if (auto const n = numeric_of(op); n.count != 0u)
                                            arithmetic(op, n);
                                        else
                                            throw cannot_optimize{ "instruction" };
                                        break;
                                    }
                                }
                                throw cannot_optimize{ "unterminated code" };
                            }
                            void load(std::uint8_t aOp, std::uint64_t aMemarg, std::uint64_t aOffset)
                            {
                                auto const address = pop();
                                auto const x = access_of(aOp);
                                auto const memory = static_cast<std::uint32_t>(aMemarg >> 32u);
                                if (address.constant && (aMemarg & MemargCached) != 0u && memory != 0u && memory <= NEOS_RT_EXTERNAL_MEMORIES && x.type != F32 && x.type != F64 &&
                                    external_memory(memory).fixed)
                                {
                                    // (a cached load: the value it has now (anywhere: it need not be the latest))
                                    auto const at = address.bits + aOffset;
                                    if (at >= address.bits)
                                        if (auto const word = iMemoryWord(memory, at, x.width))
                                        {
                                            std::uint64_t value = *word;
                                            if (x.sign)
                                            {
                                                auto const shift = 64u - 8u * x.width;
                                                value = static_cast<std::uint64_t>(static_cast<std::int64_t>(value << shift) >> shift);
                                            }
                                            value &= mask_of(x.type);
                                            used(address.from);
                                            erase(address.begin, address.end);
                                            ++iFolded;
                                            push_constant(x.type, value, address.from);
                                            return;
                                        }
                                }
                                if (address.constant && memory != 0u && memory <= NEOS_RT_EXTERNAL_MEMORIES && x.type != F32 && x.type != F64 && (aMemarg & MemargVolatile) == 0u)
                                    if (auto* c = speculating(); c != nullptr && external_memory(memory).fixed)
                                    {
                                        auto const at = address.bits + aOffset;
                                        std::optional<std::uint32_t> id;
                                        if (at >= address.bits)     // (no wrap)
                                            if (auto const word = iMemoryWord(memory, at, x.width); word && (id = speculation({ 2u | (std::uint64_t{ aOp } << 8u) | (std::uint64_t{ memory } << 16u), at })))
                                            {
                                                used(address.from);
                                                // (its value now: a guard checks that it has it (the guard reads the same width))
                                                std::uint64_t value = *word;
                                                if (x.sign)
                                                {
                                                    auto const shift = 64u - 8u * x.width;
                                                    value = static_cast<std::uint64_t>(static_cast<std::int64_t>(value << shift) >> shift);
                                                }
                                                value &= mask_of(x.type);
                                                erase(address.begin, address.end);
                                                c->guards.push_back(0x42u);
                                                c->guards.push_back(address.bits);
                                                c->guards.push_back(aOp);
                                                c->guards.push_back(aMemarg);
                                                c->guards.push_back(aOffset);
                                                c->guards.push_back(const_op(x.type));
                                                c->guards.push_back(const_token(x.type, value));
                                                c->guards.push_back(x.type == I32 ? 0x47u : 0x52u);
                                                c->guards.push_back(0x0Du);
                                                c->guards.push_back(0u);
                                                ++c->guardCount;
                                                ++iGuards;
                                                push_constant(x.type, value, single(*id));
                                                return;
                                            }
                                    }
                                emit(aOp);
                                emit(aMemarg);
                                emit(aOffset);
                                push(x.type);
                            }
                            void arithmetic(std::uint8_t aOp, numeric const& aNumeric)
                            {
                                if (aNumeric.count == 2u)
                                {
                                    auto const y = pop();
                                    auto const x = pop();
                                    if (x.constant && y.constant)
                                        if (auto const r = fold(aOp, x.bits, y.bits))
                                        {
                                            erase(x.begin, x.end);
                                            erase(y.begin, y.end);
                                            ++iFolded;
                                            used(x.from);
                                            used(y.from);
                                            push_constant(aNumeric.result, *r, merged(x.from, y.from));
                                            return;
                                        }
                                    // (x & 0 / x * 0 ... are not folded: x's code may have effects)
                                    emit(aOp);
                                    push(aNumeric.result);
                                    return;
                                }
                                auto const x = pop();
                                if (x.constant)
                                    if (auto const r = fold(aOp, x.bits, 0u))
                                    {
                                        erase(x.begin, x.end);
                                        ++iFolded;
                                        used(x.from);
                                        push_constant(aNumeric.result, *r, x.from);
                                        return;
                                    }
                                emit(aOp);
                                push(aNumeric.result);
                            }
                            // --- calls
                            bool inlinable(std::uint32_t aCallee, std::size_t aPosition) const
                            {
                                if (!iOptions.inlining || iInliningOff || iInlined >= iOptions.maxInlines || iInlines.size() >= std::min<std::size_t>(iOptions.maxInlineDepth, iInlines.capacity()))
                                    return false;
                                if (aCallee >= iModule.functions.size() || aCallee == iFunction)
                                    return false;
                                auto const& f = iModule.functions[aCallee];
                                if (f.imported || f.results->size() > 1u || f.codeEnd - f.codeBegin > (f.frameless ? iOptions.maxCalleeTokens : iOptions.maxFramedCalleeTokens))
                                    return false;
                                if (iOptions.noInline.contains(aPosition))
                                    return false;
                                for (auto const& c : iInlines)
                                    if (c.callee == aCallee)
                                        return false;   // (recursion)
                                return iInlinable(aCallee);
                            }
                            void call(std::uint32_t aCallee, std::size_t aPosition, inline_context* aInline)
                            {
                                if (aCallee >= iModule.functions.size())
                                    throw cannot_optimize{ "invalid function" };
                                auto const& callee = iModule.functions[aCallee];
                                if (callee.imported && callee.call == import_call::Assume)
                                {
                                    // (neos::assume: not a call (no effect; see ssa::Assume))
                                    pop();
                                    emit(0x10u);
                                    emit(aCallee);
                                    return;
                                }
                                // (the origin of an inlined call's code is the outermost call's)
                                auto const callPosition = aInline != nullptr ? iInlines.front().call : aPosition;
                                // (a call that the function's own code has never made (of a function without a frame of its own, in the code
                                // of one with a frame of its own): the code leaves there (deopt) rather than inline it)
                                if (iOptions.deopt && iOptions.coldCall && callee.frameless && !callee.imported && !callee.checked &&
                                    (iInlines.empty() || !iModule.functions[iInlines.back().callee].frameless) &&
                                    inlinable(aCallee, aInline != nullptr ? iInlines.front().call : aPosition) && iOptions.coldCall(aPosition))
                                {
                                    snapshot const before = take_snapshot();
                                    if (inline_call(aCallee, aInline != nullptr ? callPosition : aPosition, aPosition, true))
                                        return;
                                    restore(before);
                                }
                                if (inlinable(aCallee, aInline != nullptr ? iInlines.front().call : aPosition) && !iLarge.contains(aCallee))
                                {
                                    // (inlined if its code, after folding, is small enough; else called)
                                    snapshot const before = take_snapshot();
                                    if (inline_call(aCallee, aInline != nullptr ? callPosition : aPosition, aPosition))
                                        return;
                                    restore(before);
                                }
                                if (aInline != nullptr && !iInlines.empty() && iModule.functions[iInlines.back().callee].frameless &&
                                    !iOptions.noDeopt.contains(iInlines.front().call) &&
                                    can_deopt(iInlines.front(), effect_free(iInlines.front())) && !callee.checked && !(callee.plugin && callee.plugin->common))
                                {
                                    // (a call within inlined code (of a function without a frame of its own: e.g. a language's helper's slow
                                    // path) that has had no effect: the function's own code makes the outermost inlined call instead)
                                    emit_deopt(iInlines.front(), false);
                                    unwind();
                                    iReachable = false;
                                    return;
                                }
                                if (aInline != nullptr && !iInlines.empty() && iModule.functions[iInlines.back().callee].frameless && !callee.checked &&
                                    !(callee.plugin && callee.plugin->common) && emit_frames_deopt())
                                {
                                    unwind();
                                    iReachable = false;
                                    return;
                                }
                                for (std::size_t i = 0u; i < callee.parameters->size(); ++i)
                                    pop();
                                emit(0x10u);
                                emit(aCallee);
                                if (!callee.imported || !callee.checked)
                                    effect();
                                for (auto t : *callee.results)
                                    push(t);
                            }
                            // (the state before an inlined call, for when it is not inlined after all)
                            struct snapshot
                            {
                                std::size_t out;
                                std::vector<bool> deleted;
                                std::vector<slot> stack;
                                knowledge constants;
                                std::size_t locals;
                                std::size_t constantSets;
                                std::size_t sites;
                                std::size_t deopts;
                                std::uint64_t effects;
                                std::size_t guards;
                                std::size_t inlined;
                                std::size_t folded;
                                std::vector<std::size_t> written;
                                std::size_t origin;
                            };
                            snapshot take_snapshot() const
                            {
                                snapshot s{ iOut.size(), iDeleted, iStack, iConstants, iLocalTypes.size(), iConstantSets.size(), iSites.size(), iDeopts.size(), iEffects, iGuards, iInlined, iFolded, {}, iOrigin };
                                for (auto const& f : iFrames)
                                    s.written.push_back(f.written.size());
                                return s;
                            }
                            void restore(snapshot const& aSnapshot)
                            {
                                iOut.resize(aSnapshot.out);
                                iOrigins.resize(aSnapshot.out);
                                iDeleted = aSnapshot.deleted;
                                iStack = aSnapshot.stack;
                                iConstants = aSnapshot.constants;
                                iLocalTypes.resize(aSnapshot.locals);
                                iConstantSets.resize(aSnapshot.constantSets);
                                iSites.resize(aSnapshot.sites);
                                iDeopts.resize(aSnapshot.deopts);
                                iEffects = aSnapshot.effects;
                                iGuards = aSnapshot.guards;
                                iInlined = aSnapshot.inlined;
                                iFolded = aSnapshot.folded;
                                for (std::size_t i = 0u; i < iFrames.size() && i < aSnapshot.written.size(); ++i)
                                    iFrames[i].written.resize(aSnapshot.written[i]);
                                iOrigin = aSnapshot.origin;
                                iReachable = true;
                                iUnreachableEnd = false;
                                // (the code that grew too large was not kept: inlining is on again, unless the code is still too large)
                                iInliningOff = iOut.size() > iMaxTokens;
                            }
                            // (false if it is not inlined after all (the caller restores the state before it))
                            // (the origin of code (its position) within inlined calls of which one (at least) is of a function with a frame
                            // of its own: the inline chain (the code's position, then the calls')
                            std::size_t chain_origin(std::size_t aPosition)
                            {
                                std::vector<std::size_t> chain;
                                chain.reserve(iInlines.size() + 1u);
                                chain.push_back(aPosition);
                                for (auto c = iInlines.rbegin(); c != iInlines.rend(); ++c)
                                    chain.push_back(c->site);
                                auto existing = iChainIds.find(chain);
                                if (existing != iChainIds.end())
                                    return InlineChainToken | existing->second;
                                auto const id = static_cast<std::size_t>(iChains.size());
                                if (id >= InlineChainToken)
                                    throw cannot_optimize{ "inline chains" };
                                iChainIds.emplace(chain, id);
                                iChains.push_back(std::move(chain));
                                return InlineChainToken | id;
                            }
                            bool inline_call(std::uint32_t aCallee, std::size_t aPosition, std::size_t aSite, bool aCold = false)
                            {
                                auto const& callee = iModule.functions[aCallee];
                                auto const& parameters = *callee.parameters;
                                auto const saveOrigin = iOrigin;
                                iOrigin = aPosition;
                                struct framed_scope
                                {
                                    std::size_t& count;
                                    bool framed;
                                    framed_scope(std::size_t& aCount, bool aFramed) : count{ aCount }, framed{ aFramed } { if (framed) ++count; }
                                    ~framed_scope() { if (framed) --count; }
                                } const framedScope{ iFramedInlines, !callee.frameless };
                                inline_context c;
                                c.site = aSite;
                                c.indirect = iIndirect;     // (its deopts make the call_indirect)
                                iIndirect.reset();
                                c.callee = aCallee;
                                c.call = aPosition;
                                c.constantParameters.assign(parameters.size(), std::nullopt);
                                c.depth = static_cast<std::uint32_t>(iStack.size() >= parameters.size() ? iStack.size() - parameters.size() : 0u);
                                c.outerEffectFree = iInlines.empty() || effect_free(iInlines.front());
                                c.startEffects = iEffects;
                                // (the parameters that it sets: not constants)
                                std::vector<bool> set(parameters.size(), false);
                                for (auto p = callee.codeBegin; p < callee.codeEnd;)
                                {
                                    auto const op = static_cast<std::uint8_t>(iTokens[p]);
                                    if ((op == 0x21u || op == 0x22u) && iTokens[p + 1u] < parameters.size())
                                        set[static_cast<std::size_t>(iTokens[p + 1u])] = true;
                                    p += 1u + immediates(op, iTokens, p);
                                }
                                for (auto x : set)
                                    c.parametersSet = c.parametersSet || x;
                                // (its arguments (the last first): constants are its; the others are stored in new locals)
                                std::vector<std::uint32_t> locals(parameters.size() + callee.locals->size(), NoLocal);
                                std::vector<std::optional<std::uint64_t>> arguments(parameters.size());
                                std::vector<provenance> argumentsFrom(parameters.size());
                                for (auto i = parameters.size(); i-- > 0u;)
                                {
                                    auto const a = pop();
                                    if (a.constant)
                                    {
                                        erase(a.begin, a.end);
                                        arguments[i] = a.bits;
                                        argumentsFrom[i] = a.from;
                                        if (!set[i])
                                        {
                                            c.constantParameters[i] = known{ a.bits, a.from };
                                            continue;
                                        }
                                    }
                                    locals[i] = new_local(parameters[i]);
                                    if (!a.constant)
                                    {
                                        emit(0x21u);
                                        emit(locals[i]);
                                        written(locals[i]);
                                    }
                                }
                                for (std::size_t i = 0u; i < callee.locals->size(); ++i)
                                    locals[parameters.size() + i] = new_local((*callee.locals)[i]);
                                // (its code, in a block (its function's: a branch to its end (or a return) is the end of the call))
                                auto const result = callee.results->empty() ? NoResult : (*callee.results)[0];
                                auto const blockAt = iOut.size();
                                emit(0x02u);
                                emit(result);
                                begin_frame(frame::Inline, result);
                                c.frame = iFrames.size() - 1u;
                                // (its parameters with constant arguments that it sets, and its locals (from 0))
                                for (std::size_t i = 0u; i < parameters.size(); ++i)
                                    if (arguments[i] && locals[i] != NoLocal)
                                    {
                                        emit(const_op(parameters[i]));
                                        emit(const_token(parameters[i], *arguments[i]));
                                        emit(0x21u);
                                        emit(locals[i]);
                                        iConstants[locals[i]] = known{ *arguments[i] & mask_of(parameters[i]), argumentsFrom[i] };
                                        written(locals[i]);
                                    }
                                    else if (locals[i] != NoLocal)
                                        iConstants[locals[i]] = std::nullopt;
                                for (std::size_t i = parameters.size(); i < locals.size(); ++i)
                                {
                                    auto const l = locals[i];
                                    auto const at = emit(const_op(iLocalTypes[l]));
                                    emit(0u);
                                    auto const set = emit(0x21u);
                                    emit(l);
                                    iConstants[l] = known{};
                                    written(l);
                                    iConstantSets.push_back(constant_set{ l, set, at, at + 2u });
                                }
                                c.locals = locals;
                                iInlines.push_back(std::move(c));
                                ++iInlined;
                                auto& context = iInlines.back();
                                auto const deoptsBefore = iDeopts.size();
                                // (a call that has never been made (aCold): the code leaves at its start, if it can (else it is inlined))
                                bool left = false;
                                if (aCold)
                                {
                                    if (iInlines.size() == 1u)
                                    {
                                        if (!iOptions.noDeopt.contains(context.call) && iDeopts.size() < 60000u)
                                        {
                                            emit_deopt(context, false);
                                            left = true;
                                        }
                                    }
                                    else
                                        left = emit_frames_deopt(true);
                                    if (left)
                                    {
                                        unwind();
                                        iReachable = false;
                                    }
                                }
                                bool const reached = left ? false : process(callee.codeBegin, callee.codeEnd, &context, context.locals);
                                // (its block's end)
                                emit(0x0Bu);
                                end_frame(reached);
                                if (!iReachable && (iDeopts.size() == deoptsBefore || iInlines.back().guardCount != 0u))
                                {
                                    // (a call that does not return (unless it leaves for the function's own code (deopt): e.g. a slow
                                    // path's function, whose call the function's own code makes)): called
                                    iInlines.pop_back();
                                    iOrigin = saveOrigin;
                                    return false;
                                }
                                auto done = std::move(iInlines.back());
                                iInlines.pop_back();
                                {
                                    // (its size: its tokens, and its instructions that are likely to be code (not constants, locals' gets and
                                    // sets (registers), blocks' structure or drops))
                                    std::size_t size = 0u;
                                    std::size_t cost = 0u;
                                    for (auto i = blockAt; i < iOut.size();)
                                    {
                                        if (iDeleted[i])
                                        {
                                            ++i;
                                            continue;
                                        }
                                        auto const op = static_cast<std::uint8_t>(iOut[i]);
                                        auto const n = immediates(op, iOut, i);
                                        for (std::size_t k = 0u; k <= n && i + k < iOut.size(); ++k)
                                            size += iDeleted[i + k] ? 0u : 1u;
                                        switch (op)
                                        {
                                        case 0x01: case 0x02: case 0x03: case 0x05: case 0x0B: case 0x1A:
                                        case 0x20: case 0x21: case 0x22: case 0x41: case 0x42: case 0x43: case 0x44:
                                            break;
                                        default:
                                            ++cost;
                                            break;
                                        }
                                        i += 1u + n;
                                    }
                                    bool const large = size > iOptions.maxInlinedTokens || cost > iOptions.maxInlinedCost || done.guardCount > iOptions.maxGuards;
                                    if (large)
                                    {
                                        // (and not inlined again (its code is as large where it is called again))
                                        iLarge.insert(aCallee);
                                        iOrigin = saveOrigin;
                                        return false;
                                    }
                                }
                                if (done.guardCount != 0u)
                                {
                                    // (its guards: checked first (if one fails, the call is made: its arguments are constants, or its
                                    // parameters' locals))
                                    //   block (result) ;; done
                                    //     block        ;; the call
                                    //       guards (br_if 0)
                                    //       block (result) ... end   ;; the inlined code
                                    //       br 1
                                    //     end
                                    //     the guard failed; the call
                                    //   end
                                    std::vector<std::uint64_t> prefix{ 0x02u, result, 0x02u, NoResult };
                                    prefix.insert(prefix.end(), done.guards.begin(), done.guards.end());
                                    iOut.insert(iOut.begin() + static_cast<std::ptrdiff_t>(blockAt), prefix.begin(), prefix.end());
                                    iOrigins.insert(iOrigins.begin() + static_cast<std::ptrdiff_t>(blockAt), prefix.size(), aPosition);
                                    iDeleted.insert(iDeleted.begin() + static_cast<std::ptrdiff_t>(blockAt), prefix.size(), false);
                                    for (auto& s : iConstantSets)
                                        if (s.set >= blockAt)
                                        {
                                            s.set += prefix.size();
                                            s.begin += prefix.size();
                                            s.end += prefix.size();
                                        }
                                    // (the stack's constants are not within the inlined code (they are before it, or after it))
                                    for (auto& s : iStack)
                                        if (s.constant && s.begin >= blockAt)
                                        {
                                            s.begin += prefix.size();
                                            s.end += prefix.size();
                                        }
                                    emit(0x0Cu);
                                    emit(1u);
                                    emit(0x0Bu);
                                    // (the guard failed: the function's own code makes the (outermost) call (deopt), else it is made here)
                                    auto const* outermost = iInlines.empty() ? &done : &iInlines.front();
                                    bool const deopting = can_deopt(*outermost, iInlines.empty() ? true : done.outerEffectFree);
                                    if (deopting)
                                        emit_deopt(*outermost, true);
                                    else
                                    {
                                        emit(0x41u);
                                        emit(iSites.size());
                                        emit(0x10u);
                                        emit(intrinsics::guard_failed(iModule));
                                        for (std::size_t i = 0u; i < parameters.size(); ++i)
                                        {
                                            if (arguments[i])
                                            {
                                                emit(const_op(parameters[i]));
                                                emit(const_token(parameters[i], *arguments[i]));
                                            }
                                            else
                                            {
                                                emit(0x20u);
                                                emit(done.locals[i]);
                                            }
                                        }
                                        emit(0x10u);
                                        emit(aCallee);
                                    }
                                    emit(0x0Bu);
                                    // (its result: the construct's (the inlined code's, or the call's): not a constant (the inlined code's
                                    // constant's tokens are within it))
                                    if (result != NoResult && !iStack.empty() && iStack.back().constant)
                                    {
                                        iStack.pop_back();
                                        push(result);
                                    }
                                    iSites.push_back(inline_site{ aPosition, done.guardCount });
                                    // (the call: its effects (as the inlined code's); a deopt does not return)
                                    if (!deopting)
                                        effect();
                                }
                                iOrigin = saveOrigin;
                                return true;
                            }
                            // --- dead stores: a constant stored to a local that is not read (removed)
                            void dead_stores()
                            {
                                std::vector<std::size_t> reads(iLocalTypes.size(), 0u);
                                for (std::size_t p = 0u; p < iOut.size();)
                                {
                                    auto const op = static_cast<std::uint8_t>(iOut[p]);
                                    auto const n = immediates(op, iOut, p);
                                    if (!iDeleted[p] && (op == 0x20u || op == 0x22u) && iOut[p + 1u] < reads.size())
                                        ++reads[static_cast<std::size_t>(iOut[p + 1u])];
                                    p += 1u + n;
                                }
                                // (a deopt reads the function's own locals)
                                auto const& f = iModule.functions[iFunction];
                                auto const own = iDeopts.empty() ? 0u : f.parameters->size() + f.locals->size();
                                for (auto const& s : iConstantSets)
                                    if (s.local >= own && reads[s.local] == 0u && !iDeleted[s.set] && s.end > s.begin && !iDeleted[s.begin])
                                    {
                                        erase(s.begin, s.end);
                                        erase(s.set, s.set + 2u);
                                    }
                            }
                        private:
                            struct constant_set
                            {
                                std::uint32_t local;
                                std::size_t set;        ///< (the local.set's position)
                                std::size_t begin;      ///< (the constant's tokens)
                                std::size_t end;
                            };
                            module_description const& iModule;
                            std::vector<std::uint64_t> const& iTokens;
                            std::uint32_t iFunction;
                            options const& iOptions;
                            std::function<bool(std::uint32_t)> const& iInlinable;
                            std::function<std::optional<std::uint64_t>(std::uint32_t, std::uint64_t, std::uint32_t)> const& iMemoryWord;
                            std::vector<std::uint64_t> iOut;
                            std::vector<std::size_t> iOrigins;
                            std::size_t iFramedInlines = 0u;            ///< (the inlined calls being processed of functions with frames of their own)
                            std::map<std::vector<std::size_t>, std::size_t> iChainIds;
                            std::vector<std::vector<std::size_t>> iChains;
                            std::vector<bool> iDeleted;
                            std::size_t iOrigin = 0u;
                            std::vector<std::uint8_t> iLocalTypes;
                            std::size_t iParameters = 0u;
                            knowledge iConstants;
                            std::set<speculation_key> const* iAllowed = nullptr;    ///< (the speculations that may be made (null: all))
                            std::set<speculation_key> iUseful;                     ///< (the speculations that constants that were folded came from)
                            std::vector<std::vector<std::uint32_t>> iPool{ {} };
                            std::vector<speculation_key> iSpeculations;
                            std::vector<slot> iStack;
                            std::vector<frame> iFrames;
                            std::vector<inline_context> iInlines;
                            std::vector<constant_set> iConstantSets;
                            std::vector<inline_site> iSites;
                            std::vector<deopt> iDeopts;
                            std::uint64_t iEffects = 0u;            ///< (the effects on the code's path (the most of the paths that join): see effect)
                            bool iReachable = true;
                            bool iUnreachableEnd = false;
                            bool iInliningOff = false;
                            std::optional<std::uint32_t> iIndirect;     ///< (the call being made is a call_indirect's (its index))
                            std::set<std::uint32_t> iLarge;             ///< (functions whose inlined code was too large)
                            std::size_t iMaxTokens = 0u;
                            std::size_t iInlined = 0u;
                            std::size_t iFolded = 0u;
                            std::size_t iGuards = 0u;
                        };
                    }

                    std::optional<std::uint64_t> fold_integer(std::uint8_t aOp, std::uint64_t aX, std::uint64_t aY)
                    {
                        return fold(aOp, aX, aY);
                    }

                    std::vector<std::vector<bool>> live_locals(module_description const& aModule, std::uint32_t aFunction, std::vector<std::size_t> const& aPositions)
                    {
                        auto const& f = aModule.functions[aFunction];
                        auto const& tokens = *aModule.tokens;
                        auto const count = f.parameters->size() + f.locals->size();
                        std::vector<std::vector<bool>> result(aPositions.size(), std::vector<bool>(count, true));
                        // (its instructions: their positions; their control structure (each block's, loop's and if's else and end))
                        std::vector<std::size_t> at;
                        for (auto p = f.codeBegin; p < f.codeEnd;)
                        {
                            at.push_back(p);
                            auto const op = static_cast<std::uint8_t>(tokens[p]);
                            p += 1u + immediates(op, tokens, p);
                        }
                        auto const n = at.size();
                        if (n == 0u || n > 200000u || count == 0u)
                            return result;
                        auto const op_of = [&](std::size_t i) { return static_cast<std::uint8_t>(tokens[at[i]]); };
                        auto const imm = [&](std::size_t i, std::size_t k) { return tokens[at[i] + 1u + k]; };
                        constexpr std::size_t None = ~std::size_t{};
                        std::vector<std::size_t> endOf(n, None), elseOf(n, None);
                        std::vector<std::vector<std::size_t>> successors(n);
                        struct control { std::size_t start; std::uint8_t op; };
                        std::vector<control> controls;
                        controls.push_back(control{ None, 0u });    // (the function's)
                        {
                            std::vector<control> stack;
                            stack.push_back(control{ None, 0u });
                            for (std::size_t i = 0u; i < n; ++i)
                            {
                                auto const op = op_of(i);
                                if (op == 0x02u || op == 0x03u || op == 0x04u)
                                    stack.push_back(control{ i, op });
                                else if (op == 0x05u && stack.size() > 1u)
                                    elseOf[stack.back().start] = i;
                                else if (op == 0x0Bu)
                                {
                                    if (stack.size() > 1u)
                                        endOf[stack.back().start] = i;
                                    stack.pop_back();
                                    if (stack.empty())
                                        break;
                                }
                            }
                        }
                        // (each instruction's successors: a branch's target is its block's (or if's) end, or its loop's start)
                        {
                            std::vector<control> stack;
                            stack.push_back(control{ None, 0u });
                            auto const target = [&](std::uint64_t aDepth) -> std::size_t
                            {
                                if (aDepth >= stack.size())
                                    return None;
                                auto const& c = stack[stack.size() - 1u - static_cast<std::size_t>(aDepth)];
                                if (c.start == None)
                                    return None;    // (the function's end: none (its locals are not read after it))
                                return c.op == 0x03u ? c.start + 1u : endOf[c.start];
                            };
                            for (std::size_t i = 0u; i < n; ++i)
                            {
                                auto const op = op_of(i);
                                auto& s = successors[i];
                                auto const next = [&]() { if (i + 1u < n) s.push_back(i + 1u); };
                                auto const to = [&](std::size_t aTarget) { if (aTarget != None && aTarget < n) s.push_back(aTarget); };
                                switch (op)
                                {
                                case 0x02u: case 0x03u:
                                    stack.push_back(control{ i, op });
                                    next();
                                    break;
                                case 0x04u:
                                    stack.push_back(control{ i, op });
                                    next();
                                    to(elseOf[i] != None ? elseOf[i] + 1u : endOf[i]);
                                    break;
                                case 0x05u:
                                    to(stack.size() > 1u ? endOf[stack.back().start] : None);
                                    break;
                                case 0x0Bu:
                                    if (stack.size() > 1u)
                                        stack.pop_back();
                                    next();
                                    break;
                                case 0x0Cu:
                                    to(target(imm(i, 0u)));
                                    break;
                                case 0x0Du:
                                    next();
                                    to(target(imm(i, 0u)));
                                    break;
                                case 0x0Eu:
                                    for (std::size_t k = 0u; k <= static_cast<std::size_t>(imm(i, 0u)); ++k)
                                        to(target(imm(i, 1u + k)));
                                    break;
                                case 0x00u: case 0x0Fu:
                                    break;
                                default:
                                    next();
                                    break;
                                }
                            }
                        }
                        // (backwards, until nothing changes: a local is live before an instruction if it reads it, or if it is live after 
                        // it and the instruction does not set it)
                        auto const words = (count + 63u) / 64u;
                        std::vector<std::uint64_t> live(n * words, 0u);
                        std::vector<std::uint64_t> out(words);
                        for (bool changed = true; changed;)
                        {
                            changed = false;
                            for (std::size_t i = n; i-- > 0u;)
                            {
                                std::fill(out.begin(), out.end(), 0u);
                                for (auto s : successors[i])
                                    for (std::size_t w = 0u; w < words; ++w)
                                        out[w] |= live[s * words + w];
                                auto const op = op_of(i);
                                if (op == 0x20u || op == 0x21u || op == 0x22u)
                                {
                                    auto const l = static_cast<std::size_t>(imm(i, 0u));
                                    if (l < count)
                                    {
                                        if (op == 0x20u)
                                            out[l / 64u] |= std::uint64_t{ 1u } << (l % 64u);
                                        else
                                            out[l / 64u] &= ~(std::uint64_t{ 1u } << (l % 64u));
                                    }
                                }
                                for (std::size_t w = 0u; w < words; ++w)
                                    if (live[i * words + w] != out[w])
                                    {
                                        live[i * words + w] = out[w];
                                        changed = true;
                                    }
                            }
                        }
                        for (std::size_t k = 0u; k < aPositions.size(); ++k)
                        {
                            auto const i = static_cast<std::size_t>(std::lower_bound(at.begin(), at.end(), aPositions[k]) - at.begin());
                            if (i >= n || at[i] != aPositions[k])
                                continue;   // (not an instruction's: all live)
                            for (std::size_t l = 0u; l < count; ++l)
                                result[k][l] = ((live[i * words + l / 64u] >> (l % 64u)) & 1u) != 0u;
                        }
                        return result;
                    }

                    result optimize(module_description const& aModule, std::uint32_t aFunction, options const& aOptions,
                        std::function<bool(std::uint32_t aFunction)> const& aInlinable,
                        std::function<std::optional<std::uint64_t>(std::uint32_t aMemory, std::uint64_t aAddress, std::uint32_t aWidth)> const& aMemoryWord)
                    {
                        try
                        {
                            // (with speculation: again, with only the speculations whose values were folded (a guard of a value that is not
                            // folded costs more than it saves))
                            rewriter first{ aModule, aFunction, aOptions, aInlinable, aMemoryWord, nullptr };
                            auto r = first.run();
                            if (first.speculations() == 0u || first.useful().size() == first.speculations())
                                return r;
                            auto const useful = first.useful();
                            rewriter second{ aModule, aFunction, aOptions, aInlinable, aMemoryWord, &useful };
                            return second.run();
                        }
                        catch (cannot_optimize const&)
                        {
                            return {};
                        }
                    }
                }
            }
        }
    }
}
