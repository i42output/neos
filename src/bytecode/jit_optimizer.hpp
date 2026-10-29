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
                    //
                    // Where the inlined code is within inlined calls of functions with frames of their own (e.g. a language's functions), and
                    // has had no effect since the innermost of those called it (though they have), the code leaves for the frames of all of
                    // them (as V8's deoptimizer makes the frames of a FrameState's chain): the function's own frame and those of the inlined 
                    // calls but the innermost are made as they are while the call that each makes runs (each one's code continues after its
                    // call returns: the next frame returns there); the innermost's code makes the call (of the function without a frame of 
                    // its own) whose code left (see deopt::frames).
                    struct deopt
                    {
                        // (a frame that the code leaves for (deopt::frames): a function's, at a call (its position) that it makes)
                        struct frame
                        {
                            std::uint32_t function = 0u;
                            std::size_t position = 0u;      ///< (its call's position (in the function's code))
                            std::uint32_t arguments = 0u;   ///< (its call's (a call_indirect's: and its index))
                            std::uint32_t depth = 0u;       ///< (its operand stack's height below its call's arguments)
                            std::vector<std::uint32_t> locals;  ///< (its locals (parameters first): the optimized code's (NoLocal: constants))
                            std::vector<std::uint64_t> constants;   ///< (by local: those that are constants)
                            std::vector<bool> live;         ///< (its locals that its code may read after its call (empty: all))
                        };
                        static constexpr std::uint32_t NoLocal = ~std::uint32_t{};
                        std::size_t call = 0u;          ///< (the call's position (the outermost inlined call's; frames: the innermost's))
                        std::uint32_t callee = 0u;
                        std::uint32_t arguments = 0u;
                        std::uint32_t depth = 0u;       ///< (the operand stack's height below its arguments)
                        std::uint32_t locals = 0u;      ///< (the function's (its parameters and locals))
                        bool guard = false;             ///< (a guard failed (a speculation's value changed), else the inlined code would call)
                        std::vector<bool> live;         ///< (the function's locals that its own code may read after the call (empty: all))
                        std::vector<frame> frames;      ///< (if not empty: the frames that it leaves for, outermost (the function's own) first;
                                                        ///< the last's call is the call (of a function without a frame of its own) that the
                                                        ///< code makes instead; its values (see the optimizing code generator) are each 
                                                        ///< frame's locals and operand stack, then the call's arguments)
                    };

                    struct options
                    {
                        bool inlining = true;
                        bool speculation = true;
                        std::set<std::size_t> noInline;         ///< (calls (their tokens' positions) not to inline)
                        std::set<std::size_t> noDeopt;          ///< (inlined calls (their positions) whose code's calls are made (not deopts))
                        std::size_t maxInlineDepth = 4u;
                        std::size_t maxCalleeTokens = 6000u;    ///< (a callee larger than this is not inlined)
                        std::size_t maxFramedCalleeTokens = 600u;   ///< (... nor one with a frame of its own larger than this)
                        std::size_t maxTokens = 60000u;         ///< (the function's code may grow to this, by inlining...)
                        std::size_t maxGrowth = 6u;             ///< (... and to this many times its size (and 2000 tokens))
                        std::size_t maxInlinedTokens = 2000u;   ///< (an inlined call whose code (after folding) is larger is not inlined...)
                        std::size_t maxInlinedCost = 400u;      ///< (... nor one with more instructions that are code (see rewriter::inline_call) ...)
                        std::size_t maxGuards = 16u;            ///< (... nor one with more guards)
                        std::size_t maxInlines = 512u;          ///< (the calls inlined in a function)
                        bool deopt = false;                     ///< (the code leaves for the function's own (see deopt), rather than calls)
                        // (where the code can leave for frames of inlined calls (see deopt::frames): whether a function (with aLocals 
                        // locals) can be left for at a call (at aPosition, of aArguments arguments, aDepth below them) that it makes; if 
                        // none, it does not)
                        std::function<bool(std::uint32_t aFunction, std::size_t aPosition, std::uint32_t aArguments, std::uint32_t aDepth,
                            std::uint32_t aLocals)> deoptFrame;
                        std::size_t maxDeoptValues = 4096u;     ///< (a deopt's values (its frames' locals and stacks, and its call's arguments))
                        // (a call (its position: in a function with a frame of its own) that has never been made (its function's code 
                        // has run often enough): the code leaves there (a deopt), rather than inline (or make) it; if none, none are)
                        std::function<bool(std::size_t aPosition)> coldCall;
                        // (the speculations (see result::useful) that an earlier optimization of the function found useful: only those are
                        // made (the optimizer then runs once, unless some of them are not useful now); if none, any are)
                        std::set<std::pair<std::uint64_t, std::uint64_t>> const* usefulHint = nullptr;
                        // (a function's block_ends (made once for it): code that is not reached (e.g. after a branch, or a constant if's
                        // arm) is skipped by them (over its blocks, loops and ifs); if none, by reading it all)
                        std::function<std::uint32_t const*(std::uint32_t aFunction)> blockEnds;
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
                        std::set<std::pair<std::uint64_t, std::uint64_t>> useful;  ///< (the speculations whose values were folded (see options::usefulHint))
                    };

                    // (an integer instruction's result for constant operands (aY: a binary one's), if it is folded (not if it traps))
                    std::optional<std::uint64_t> fold_integer(std::uint8_t aOp, std::uint64_t aX, std::uint64_t aY);

                    // (each block's, loop's and if's end in a function's code: by position (from the code's start), the end's (also from
                    // its start; 0 for other instructions))
                    std::vector<std::uint32_t> block_ends(module_description const& aModule, std::uint32_t aFunction);

                    // (the parameters of a function that its code reads (local.get): a call (whose callee is known) need not pass the others)
                    std::vector<bool> parameters_read(module_description const& aModule, std::uint32_t aFunction);

                    // (the locals (parameters, then locals) of a function that its code may read after each of the positions (of its 
                    // instructions) before it sets them: by position)
                    std::vector<std::vector<bool>> live_locals(module_description const& aModule, std::uint32_t aFunction, std::vector<std::size_t> const& aPositions);
                    // (the same, found once for a function (its code does not change): its instructions' positions, and the locals live
                    // before each (all: none found))
                    struct liveness
                    {
                        std::vector<std::size_t> at;
                        std::size_t count = 0u;     ///< (its locals)
                        std::size_t words = 0u;     ///< (per instruction)
                        std::vector<std::uint64_t> live;
                    };
                    liveness locals_liveness(module_description const& aModule, std::uint32_t aFunction);
                    std::vector<std::vector<bool>> live_locals(liveness const& aLiveness, std::vector<std::size_t> const& aPositions);

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
