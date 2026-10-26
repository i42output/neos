/*
  jit_optimizer.hpp

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

// The optimizing tier's optimizer (jit_optimizer.cpp): a hot function's code (pre-decoded WebAssembly, as the JIT compiles it)
// rewritten as code that does the same, faster:
// - small functions that it calls (that have no frame of their own: a language's helpers) are inlined;
// - constants are folded and propagated (through locals), and branches whose conditions are constants are resolved (code that
//   cannot be reached is removed);
// - (speculation) in inlined code, globals and words of fixed external memories at constant addresses that it reads (before it
//   writes memory or calls) are taken to have the values that they have now: guards at the start of the inlined code check that
//   they do (if one does not, the call is made instead, and the VM is told (GuardFailedIntrinsic), so that the function can be
//   compiled again without that inlining).
// It knows nothing of the languages whose code it optimizes: only the instructions' semantics.

#pragma once

#include <neos/neos.hpp>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <neos/bytecode/vm/jit.hpp>

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
                    // (a call of the function after the module's functions (its index: the number of the module's functions):
                    // the VM's (an inlined call's guard failed): its argument (i32) is the call's inline site)
                    struct intrinsics
                    {
                        static std::uint32_t guard_failed(module_description const& aModule) { return static_cast<std::uint32_t>(aModule.functions.size()); }
                        // (leaves the optimized code for the function's own (see deopt): its arguments are the call's arguments, then the
                        // deopt's number (an i32 constant); it does not return)
                        static std::uint32_t deopt(module_description const& aModule) { return static_cast<std::uint32_t>(aModule.functions.size() + 1u); }
                    };

                    // (a memory access's memarg flag (its first immediate's low bits): a cached load, whose value may be any that its word has
                    // had (the program does not depend on its being the latest): of a fixed external memory at a constant address, the value
                    // it has when the function is optimized (a constant: without a guard))
                    constexpr std::uint64_t MemargCached = 0x80u;
                    // (a memarg flag: a load of a word that changes often (e.g. an allocator's): its value is never speculated (a guard
                    // would fail))
                    constexpr std::uint64_t MemargVolatile = 0x100u;
                    // (a memarg flag: a load (i64) of an offset within the memory (an address in it, below its size by at least 
                    // MemargOffsetMargin): accesses at it plus less than the margin need not be checked)
                    constexpr std::uint64_t MemargOffset = 0x200u;
                    constexpr std::uint64_t MemargOffsetMargin = 0x10000u;
                    // (a memarg's alias class (its flags' bits 12 to 15): accesses of different classes (other than 0) are of different 
                    // words (a store of one class does not change what a load of another reads); class 0: any word (the default))
                    constexpr unsigned MemargClassShift = 12u;
                    constexpr std::uint64_t MemargClassMask = 0xFu;
                    constexpr std::size_t MemargClasses = 16u;

                    // (where optimized code can leave for the function's own code (deoptimization): an inlined call (of a function without a
                    // frame of its own) whose code has had no effect (and that has not set its parameters) is made by the function's own code
                    // instead (its frame made from the optimized code's values: the function's locals, the operand stack below the call's
                    // arguments, and its arguments), e.g. where a speculation fails, or where the inlined code would call)
                    struct deopt
                    {
                        std::size_t call = 0u;          ///< (the call's position (the outermost inlined call's))
                        std::uint32_t callee = 0u;
                        std::uint32_t arguments = 0u;
                        std::uint32_t depth = 0u;       ///< (the operand stack's height below its arguments)
                        std::uint32_t locals = 0u;      ///< (the function's (its parameters and locals))
                        bool guard = false;             ///< (a guard failed (a speculation's value changed), else the inlined code would call)
                        std::vector<bool> live;         ///< (the function's locals that its own code may read after the call (empty: all))
                    };

                    struct options
                    {
                        bool inlining = true;
                        bool speculation = true;
                        std::set<std::size_t> noInline;         ///< (calls (their tokens' positions) not to inline)
                        std::set<std::size_t> noDeopt;          ///< (inlined calls (their positions) whose code's calls are made (not deopts))
                        std::size_t maxInlineDepth = 4u;
                        std::size_t maxCalleeTokens = 4000u;    ///< (a callee larger than this is not inlined)
                        std::size_t maxFramedCalleeTokens = 600u;   ///< (... nor one with a frame of its own larger than this)
                        std::size_t maxTokens = 60000u;         ///< (the function's code may grow to this, by inlining...)
                        std::size_t maxGrowth = 6u;             ///< (... and to this many times its size (and 2000 tokens))
                        std::size_t maxInlinedTokens = 2000u;   ///< (an inlined call whose code (after folding) is larger is not inlined...)
                        std::size_t maxInlinedCost = 400u;      ///< (... nor one with more instructions that are code (see rewriter::inline_call) ...)
                        std::size_t maxGuards = 16u;            ///< (... nor one with more guards)
                        std::size_t maxInlines = 128u;          ///< (the calls inlined in a function)
                        bool deopt = false;                     ///< (the code leaves for the function's own (see deopt), rather than calls)
                    };

                    // (an inlined call that has guards: the call's position, and its guards)
                    struct inline_site
                    {
                        std::size_t call = 0u;
                        std::size_t guards = 0u;
                    };

                    // (an origin (see result::origins) that is an inline chain's: InlineChainToken | its index (in result::chains; once the
                    // code is installed, in the module's (see compiled_module::inlineChains)))
                    constexpr std::size_t InlineChainToken = 0x80000000u;

                    struct result
                    {
                        std::vector<std::uint64_t> tokens;      ///< (the function's code: [0, size), ending with its end)
                        std::vector<std::size_t> origins;       ///< (each token's origin: the position of the code it came from (an inlined
                                                                ///< call's code: the call's, or, within inlined calls of functions with frames
                                                                ///< of their own, an inline chain (see InlineChainToken)), for the frames of calls)
                        std::vector<std::vector<std::size_t>> chains;   ///< (inline chains: the positions of the code and of the calls that
                                                                        ///< inlined it, innermost first (the frames of a call there))
                        std::vector<std::uint8_t> locals;       ///< (its locals (after its parameters))
                        std::vector<inline_site> sites;         ///< (by the number that GuardFailedIntrinsic's argument is)
                        std::vector<deopt> deopts;              ///< (by their numbers)
                        std::size_t inlined = 0u;
                        std::size_t folded = 0u;
                        std::size_t guards = 0u;
                    };

                    // (an integer instruction's result for constant operands (aY: a binary one's), if it is folded (not if it traps))
                    std::optional<std::uint64_t> fold_integer(std::uint8_t aOp, std::uint64_t aX, std::uint64_t aY);

                    // (the locals (parameters, then locals) of a function that its code may read after each of the positions (of its 
                    // instructions) before it sets them: by position)
                    std::vector<std::vector<bool>> live_locals(module_description const& aModule, std::uint32_t aFunction, std::vector<std::size_t> const& aPositions);

                    // (aInlinable: whether a function may be inlined (its calls have no frame of their own); aMemoryWord: the word at an
                    // address of a fixed external memory (aWidth bytes, zero extended), if it is within it)
                    result optimize(module_description const& aModule, std::uint32_t aFunction, options const& aOptions,
                        std::function<bool(std::uint32_t aFunction)> const& aInlinable,
                        std::function<std::optional<std::uint64_t>(std::uint32_t aMemory, std::uint64_t aAddress, std::uint32_t aWidth)> const& aMemoryWord);
                }
            }
        }
    }
}
