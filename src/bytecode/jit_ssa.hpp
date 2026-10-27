/*
  jit_ssa.hpp

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

// The optimizing tier's intermediate representation (jit_ssa.cpp): a function (pre-decoded WebAssembly, as the optimizer
// (jit_optimizer.hpp) leaves it) in static single assignment form: basic blocks of instructions, each of which defines (at most)
// one value; locals and the operand stack become values (phis where control flow merges). Its optimizations (constant folding,
// global value numbering, redundant load elimination, the bounds of values (whose accesses of a fixed external memory need no
// check)) are the instructions' semantics only. The JIT (jit.cpp) allocates its registers and emits its code.

#pragma once

#include <neos/neos.hpp>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <vector>
#include <neos/bytecode/vm/jit.hpp>
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
                    using value = std::uint32_t;
                    constexpr value None = ~value{};

                    constexpr std::uint8_t I32 = 0x7Fu;
                    constexpr std::uint8_t I64 = 0x7Eu;
                    constexpr std::uint8_t F32 = 0x7Du;
                    constexpr std::uint8_t F64 = 0x7Cu;
                    constexpr std::uint8_t Void = 0x40u;

                    struct unsupported : std::runtime_error { using std::runtime_error::runtime_error; };

                    // (an instruction's operation: a WebAssembly opcode (a constant (0x41-0x44), a numeric instruction, a load or
                    // store, select, global.get or global.set, call or call_indirect), or one of these)
                    enum operation : std::uint16_t
                    {
                        Param = 0x100,      ///< (a parameter (imm0: its index))
                        Phi,                ///< (operands: by predecessor)
                        CallResult,         ///< (a call's result after its first (operand: the call; imm0: its index): just after the call)
                        Jump,               ///< (to the block's successor)
                        Branch,             ///< (operand: the condition (i32); successors: if true, if false)
                        Switch,             ///< (operand: the index (i32); successors: the targets, then the default)
                        Return,             ///< (operands: the results)
                        Trap,               ///< (imm0: the trap: unreachable)
                        Deopt,              ///< (leaves for the function's own code (see optimizer::deopt; imm0: its number): operands: the
                                            ///< function's locals, the operand stack below the call's arguments, the arguments)
                        Assume              ///< (neos::assume (see import_call::Assume): operand: a condition that is not 0 here (no code))
                    };

                    enum flag : std::uint8_t
                    {
                        Unchecked = 0x01u,  ///< (a memory access that is within its memory: not checked)
                        Volatile = 0x02u,   ///< (not to be removed (e.g. a load that may trap))
                        Scaled = 0x0Cu,     ///< (an unchecked access's address: its operand shifted left by these bits (>> 2: 1 to 3), as an
                                            ///< address's index is scaled)
                        Top16 = 0x10u       ///< (an i64 eq or ne: of its first operand's top 16 bits (>> 48) and its second's (a constant's)
                                            ///< low 16 bits)
                    };
                    constexpr unsigned ScaledShift = 2u;

                    struct instruction
                    {
                        std::uint16_t op = 0u;
                        std::uint8_t type = Void;   ///< (its value's type (Void: none))
                        std::uint8_t flags = 0u;
                        std::vector<value> operands;
                        std::uint64_t imm0 = 0u;    ///< (a constant's bits; a memory access's memarg; the function, global, type or parameter)
                        std::uint64_t imm1 = 0u;    ///< (a memory access's offset)
                        std::size_t origin = 0u;    ///< (the position that its call is reported at)
                        std::uint32_t block = 0u;
                        bool dead = false;
                        bool constant() const { return op >= 0x41u && op <= 0x44u; }
                        bool terminator() const { return op >= Jump && op <= Deopt; }
                        bool load() const { return op >= 0x28u && op <= 0x35u; }
                        bool store() const { return op >= 0x36u && op <= 0x3Eu; }
                        bool call() const { return op == 0x10u || op == 0x11u; }
                    };

                    struct block
                    {
                        std::vector<value> code;                ///< (its instructions: phis first; its terminator last)
                        std::vector<std::uint32_t> predecessors;
                        std::vector<std::uint32_t> successors;
                        std::uint32_t loopDepth = 0u;
                        bool loopHeader = false;
                        bool reachable = false;
                    };

                    struct function
                    {
                        std::uint32_t index = 0u;
                        std::vector<instruction> values;        ///< (an instruction's value is its index)
                        std::vector<block> blocks;              ///< ([0]: the entry)
                        std::vector<std::uint8_t> parameters;
                        std::vector<std::uint8_t> results;
                        std::vector<std::uint32_t> order;       ///< (the blocks, as the code is laid out (reverse postorder))
                        std::vector<std::uint32_t> uses;        ///< (each value's uses)
                    };

                    // (a function's code (aTokens, its locals aLocals (after its parameters), and aOrigins: its tokens' origins); throws
                    // unsupported if it uses an instruction that the representation does not have)
                    function build(module_description const& aModule, std::uint32_t aFunction, std::vector<std::uint64_t> const& aTokens,
                        std::vector<std::uint8_t> const& aLocals, std::vector<std::size_t> const& aOrigins, std::vector<optimizer::deopt> const& aDeopts = {});
                    // (aAccessible: the size of a fixed external memory (0: not fixed): its accesses that are within it are not checked; 
                    // aMemoryWord: a word of a fixed external memory (its value now, if it can be read): cached loads (optimizer::MemargCached)
                    // at constant addresses are constants)
                    void optimize(function& aFunction, module_description const& aModule, std::function<std::uint64_t(std::uint32_t aMemory)> const& aAccessible = {},
                        std::function<std::optional<std::uint64_t>(std::uint32_t aMemory, std::uint64_t aAddress, std::uint32_t aWidth)> const& aMemoryWord = {});
                    // (the blocks' order (reverse postorder) and the values' uses, after the code changes)
                    void finish(function& aFunction);
                }
            }
        }
    }
}
