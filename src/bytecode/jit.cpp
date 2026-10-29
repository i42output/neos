/*
  jit.cpp

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

#include <neos/neos.hpp>
#include <cstring>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <exception>
#include <algorithm>
#include <bit>
#include <optional>
#include <span>
#include <string>
#include <initializer_list>
#include <array>
#include <chrono>
#include <map>
#include <unordered_set>
#include <memory>
#include <set>
#include <unordered_map>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <neos/bytecode/vm/jit.hpp>
#include <neos/bytecode/bytecode.hpp>
#include "jit_optimizer.hpp"
#include "jit_ssa.hpp"
#define NEOS_RUNTIME_PLUGIN_HOST
#include <neos/runtime/plugin.h>

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
#include <sys/uio.h>
#include <pthread.h>
#include <unistd.h>
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
                        TrapIndirectCall = 8,
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
                        case TrapIndirectCall: return "indirect call type mismatch";
                        default: return "trap";
                        }
                    }

                    constexpr std::size_t MaxResults = 64u;
                    constexpr std::size_t FunctionAlignment = 32u;
                    constexpr std::size_t LoopAlignment = 32u;

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
                        object_table* objects;
                        // (while generated code calls an import: the frame (rbp) of the function that calls it and the token of 
                        // the call, so that its frames can be walked (see walk))
                        std::uint64_t exitFrame;
                        std::uint64_t exitSite;
                        value* deoptBuffer;     ///< (the thread's: where optimized code that leaves for the baseline's puts its values)
                        value results[MaxResults];
                    };
                    constexpr std::size_t MaxDeoptValues = 4096u;

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
                    constexpr std::int32_t ContextExitFrame = static_cast<std::int32_t>(offsetof(context, exitFrame));
                    constexpr std::int32_t ContextExitSite = static_cast<std::int32_t>(offsetof(context, exitSite));
                    constexpr std::int32_t ContextObjects = static_cast<std::int32_t>(offsetof(context, objects));
                    constexpr std::int32_t ContextDeoptBuffer = static_cast<std::int32_t>(offsetof(context, deoptBuffer));
                    constexpr std::int32_t TableSlots = static_cast<std::int32_t>(offsetof(object_table, slots));
                    constexpr std::int32_t TableSize = static_cast<std::int32_t>(offsetof(object_table, size));
                    constexpr std::int32_t TableFree = static_cast<std::int32_t>(offsetof(object_table, free));
                    constexpr std::int32_t SlotObject = static_cast<std::int32_t>(offsetof(object_slot, object));
                    constexpr std::int32_t SlotCount = static_cast<std::int32_t>(offsetof(object_slot, count));
                    constexpr std::int32_t SlotNext = static_cast<std::int32_t>(offsetof(object_slot, next));
                    static_assert(sizeof(object_slot) == 16u);

                    // value types
                    constexpr std::uint8_t I32 = 0x7Fu;
                    constexpr std::uint8_t I64 = 0x7Eu;
                    constexpr std::uint8_t F32 = 0x7Du;
                    constexpr std::uint8_t F64 = 0x7Cu;

                    bool is_float(std::uint8_t aType) { return aType == F32 || aType == F64; }
                    bool is_wide(std::uint8_t aType) { return aType == I64 || aType == F64; }
                }

                constexpr std::uint64_t NoOsrLoop = ~std::uint64_t{};     ///< (see tier_loop)

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
                    // (call_indirect's table: each function's address and type (an import's: none))
                    struct indirect_entry
                    {
                        std::uint64_t target = 0u;
                        std::uint32_t type = ~std::uint32_t{ 0u };
                        std::uint32_t reserved = 0u;
                    };
                    std::vector<indirect_entry> indirect;
                    std::exception_ptr exception;
                    // (the frames of generated code: the offset that each call of a function returns to and the token of the call 
                    // (sorted), and the offset that the trampoline's call returns to)
                    std::vector<std::pair<std::size_t, std::size_t>> callSites;
                    std::size_t trampolineReturn = 0u;
                    std::size_t exitOffset = 0u;    ///< (where a trap leaves generated code (see code_generator::trampoline))
                    // --- the optimizing tier: a function that is called often (its counter (of calls before then) reaches 0) is optimized 
                    // (see jit_optimizer.hpp) and compiled again (its code elsewhere (a chunk): its entry (and its indirect table entry) 
                    // then go there)
                    struct tier_version     ///< (an optimized function's code: its inlined calls that have guards are reported with it)
                    {
                        std::uint32_t function = 0u;
                        std::vector<optimizer::inline_site> sites;
                        std::vector<optimizer::deopt> deopts;
                        mutable std::vector<std::uint32_t> deoptCounts;     ///< (each deopt's (its code left for the baseline's))
                        mutable std::uint32_t deoptsTaken = 0u;             ///< (its deopts' (not guards'): all of them)
                        mutable std::uint64_t calls = 0u;                   ///< (its code's calls (counted if it has deopts that are not guards'))
                        std::uint64_t osrLoop = NoOsrLoop;                  ///< (code that calls continue in from a loop (see osr_entry): the loop)
                    };
                    struct tier_chunk
                    {
                        void* code = nullptr;
                        std::size_t size = 0u;
                        std::vector<std::pair<std::size_t, std::size_t>> callSites;    ///< (as callSites, in it)
                    };
                    bool tiering = false;
                    bool speculation = true;
                    bool inlining = true;
                    bool backend = true;    ///< (the optimizing code generator (else the baseline's))
                    bool deopt = true;      ///< (its code leaves for the baseline's (see optimizer::deopt))
                    std::uint32_t verbose = 0u;
                    std::uint64_t compileMicroseconds = 0u;
                    std::uint64_t phaseMicroseconds[4] = {};
                    std::vector<std::int32_t> counters;
                    std::uint32_t primaryMemory = 0u;                   ///< (the fixed external memory that all of its code accesses (if one): its
                                                                        ///< address is in r14 throughout its code (set by the trampoline; no
                                                                        ///< function saves, sets or uses r14 otherwise); 0: none (each function
                                                                        ///< that accesses one has its address in r14, saved and set by it))
                    std::vector<std::int32_t> loopCounters;             ///< (each function's (with a frame) loops' iterations before it is optimized
                                                                        ///< (see tier_loop))
                    std::int32_t loopIterations = 0;
                    std::vector<std::uint8_t> staleJobs;                ///< (each function's optimizations made in the background that were not
                                                                        ///< installed (the program's memory had changed): then made here)
                    std::vector<std::uint8_t> loopTriggered;            ///< (an optimization that its loops started: its calls not yet made 
                                                                        ///< are not cold (its code after the loop has not run yet))
                    std::vector<std::uint8_t> tierState;                ///< (0: not optimized, 1: optimized, 2: not to be optimized)
                    std::vector<std::uint32_t> recompiles;
                    std::vector<std::set<std::size_t>> noInline;        ///< (calls (their positions) not to inline (their guards failed))
                    std::vector<std::set<std::size_t>> noDeopt;         ///< (inlined calls (their positions) whose code calls rather than deopts
                                                                        ///< (it left there often))
                    std::vector<std::map<std::size_t, std::uint32_t>> guardFailures;
                    std::vector<std::map<std::size_t, std::uint32_t>> deoptStrikes;   ///< (the times that inlined calls' deopts made the function optimized again)
                    std::vector<std::vector<bool>> parametersRead;      ///< (each function's parameters that its code reads (see optimizer::parameters_read):
                                                                        ///< made (once) before the first optimization; optimized code's calls pass only those)
                    std::vector<std::optional<std::set<std::pair<std::uint64_t, std::uint64_t>>>> usefulSpeculations;  ///< (each function's last
                                                                        ///< optimization's useful speculations (see optimizer::options::usefulHint))
                    std::deque<std::vector<std::size_t>> inlineChains;  ///< (optimized code's inline chains (see optimizer::InlineChainToken):
                                                                        ///< the frames of calls within inlined calls, innermost first; added
                                                                        ///< by optimizations (in the background): see chainsMutex)
                    mutable std::mutex chainsMutex;                     ///< (inlineChains' size and elements (a chain's reference remains valid))
                    // (functions' locals' liveness (see optimizer::locals_liveness: their code does not change), found once (by optimizations,
                    // in the background): see livenessMutex)
                    mutable std::unordered_map<std::uint32_t, std::shared_ptr<optimizer::liveness const>> liveness;
                    mutable std::mutex livenessMutex;
                    // (functions' blocks' ends (see optimizer::block_ends), made once each (by optimizations, in the background: the first
                    // made is kept))
                    std::unique_ptr<std::atomic<std::vector<std::uint32_t> const*>[]> blockEnds;
                    std::vector<tier_version const*> currentVersion;
                    std::vector<std::unique_ptr<tier_version>> versions;
                    // (optimization in the background (see tier_worker): a function's optimization (its optimizer and its code) is made on
                    // another thread (installed here); tierState 3 while it is)
                    struct tier_job;
                    bool background = true;
                    std::vector<std::shared_ptr<tier_job>> jobs;        ///< (by function: its optimization in the background)
                    std::uint32_t jobsInFlight = 0u;                    ///< (being made by the worker (see tier_worker::mutex))
                    bool closing = false;                               ///< (being destroyed: no more jobs (see tier_worker::mutex))
                    std::vector<tier_chunk> chunks;
                    std::vector<std::array<std::uint8_t, 14u>> savedEntries;    ///< (the code that an optimized function's entry had)
                    // (calls that continue in optimized code at loops (on-stack replacement, see tier_loop): each loop's (by its token) code
                    // (made when a call that runs the function's baseline code has run the loop often enough, once the function has been
                    // optimized), until it leaves for the baseline's (a deopt in the loop, or a guard): then made again (at most MaxOsrCompiles
                    // times))
                    bool osr = true;
                    struct osr_state
                    {
                        void const* entry = nullptr;
                        tier_version const* version = nullptr;
                        std::uint32_t compiles = 0u;
                        std::uint32_t entries = 0u;
                    };
                    std::unordered_map<std::uint64_t, osr_state> osrLoops;
                    std::unordered_map<std::size_t, bool> inLoop;       ///< (positions of functions' own code: whether each is within a loop (see deopt_taken))
                    // (deoptimization: optimized code that leaves for the function's own code (its frame made as the baseline's is, where a
                    // call (of an inlinable function: an inlined call of the optimized code's) is made): each function's frame, and each such
                    // call's (by its token): the code that makes the call (its arguments in the outgoing area) and the operand stack's slots
                    // below its arguments)
                    struct deopt_frame
                    {
                        struct local
                        {
                            std::uint8_t type = 0u;
                            bool pinned = false;
                            std::int8_t reg = 0;
                            std::int32_t disp = 0;  ///< (not pinned: [rbp + disp])
                        };
                        bool valid = false;
                        std::vector<std::int8_t> pushed;                ///< (the registers pushed after rbp, in order)
                        std::uint32_t frameSize = 0u;                   ///< (rsp's below them)
                        std::int8_t memoryBase = -1;                    ///< (the register that has a fixed external memory's address)
                        std::uint32_t memoryIndex = 0u;
                        std::vector<std::pair<std::int8_t, std::int32_t>> savedXmms;  ///< (the xmms (callee saved) that it saves: where)
                        std::vector<local> locals;                      ///< (parameters first)
                        std::vector<std::pair<std::int8_t, std::int32_t>> callSavedGprs;    ///< (the registers that it saves around its calls
                                                                                            ///< (native code does not preserve them): where)
                        std::vector<std::pair<std::int8_t, std::int32_t>> callSavedXmms;    ///< (... the xmms)
                    };
                    struct deopt_slot
                    {
                        bool inSlot = false;            ///< (else a constant or a local's value: in the code)
                        std::uint8_t type = 0u;
                        std::int32_t disp = 0;          ///< ([rbp + disp])
                        std::int32_t local = -1;        ///< (a local's value (not yet read: the code reads the local when it needs it))
                    };
                    struct deopt_point
                    {
                        std::uint32_t function = 0u;
                        std::uint8_t const* code = nullptr;  ///< (the code that resume and returned are offsets in (null: the module's))
                        std::size_t resume = 0u;        ///< (the code's offset)
                        std::size_t returned = 0u;      ///< (the code's offset where its call returns to (0: none))
                        std::uint32_t arguments = 0u;
                        std::vector<deopt_slot> stack;
                        std::uint32_t const* made = nullptr;    ///< (a call of a function with a frame of its own: set once the code has
                                                                ///< made it (see callFlags))
                    };
                    std::vector<deopt_frame> deoptFrames;
                    std::unordered_map<std::size_t, deopt_point> deoptPoints;
                    // (the calls (deopt points) of functions with frames of their own that their code has made: optimized code leaves
                    // (deopts) at a call that has never been made (once its function has been called often enough, see warm_function),
                    // rather than inline it (as V8's optimizing compiler leaves where its feedback says that code has not run))
                    std::deque<std::uint32_t> callFlags;
                    std::int32_t tierCalls = 0;                         ///< (the calls before a function is optimized)
                    // (deoptFrames and deoptPoints: added to as functions are compiled (see lazy), while optimizations are made in the
                    // background (which read them))
                    mutable std::mutex deoptMutex;
                    // --- functions compiled when they are first called (NEOS_JIT_LAZY, default 1): the module's code has an entry for
                    // each (at functionOffsets: 16 bytes, which first call lazy_compile (with the function's index in r11d), then jump
                    // to its code (jmp [rip+2]; its address)); their code is in regions (chunks), one after another
                    bool lazy = false;
                    struct lazy_region
                    {
                        std::uint8_t* code = nullptr;
                        std::size_t size = 0u;
                        std::size_t used = 0u;
                        std::size_t chunk = 0u;     ///< (its chunk (see chunks): its call sites)
                    };
                    std::vector<lazy_region> lazyRegions;
                    std::uint32_t lazyCompiled = 0u;
                };

                void compiled_module_deleter::operator()(compiled_module* aModule) const
                {
                    delete aModule;
                }

                namespace
                {
                    // (a deopt point, and a function's frame (null: none yet): see compiled_module::deoptMutex)
                    compiled_module::deopt_point const* deopt_point_at(compiled_module const& aModule, std::size_t aPosition)
                    {
                        std::lock_guard<std::mutex> lock{ aModule.deoptMutex };
                        auto const point = aModule.deoptPoints.find(aPosition);
                        return point != aModule.deoptPoints.end() ? &point->second : nullptr;
                    }
                    compiled_module::deopt_frame const* deopt_frame_of(compiled_module const& aModule, std::uint32_t aFunction)
                    {
                        std::lock_guard<std::mutex> lock{ aModule.deoptMutex };
                        return aFunction < aModule.deoptFrames.size() && aModule.deoptFrames[aFunction].valid ? &aModule.deoptFrames[aFunction] : nullptr;
                    }
                    // (the address of an offset of a deopt point's (in its function's code))
                    std::uint64_t deopt_address(compiled_module const& aModule, compiled_module::deopt_point const& aPoint, std::size_t aOffset)
                    {
                        auto const* const code = aPoint.code != nullptr ? aPoint.code : static_cast<std::uint8_t const*>(aModule.code);
                        return reinterpret_cast<std::uint64_t>(code + aOffset);
                    }
                }

                // (an optimization made in the background: the optimizer's result, then its code)
                struct compiled_module::tier_job
                {
                    compiled_module* module = nullptr;
                    std::uint32_t function = 0u;
                    std::chrono::steady_clock::time_point start;
                    optimizer::result result;
                    optimizer::options options;
                    std::optional<std::set<std::pair<std::uint64_t, std::uint64_t>>> hint;   ///< (see options::usefulHint)
                    bool rewritten = false;             ///< (the optimizer optimized it (else it is not optimized))
                    // (the words of the program's memory that its optimizer and its SSA optimizer read (the values that its code may
                    // use: e.g. a property cache's entry's fields, which the runtime changes as the program runs). Made in the background,
                    // they may be of different moments (e.g. an entry's fields as the runtime changes it): it is installed only if each 
                    // word has the value read, then (as if they were read then, by the program's thread))
                    struct memory_read
                    {
                        std::uint32_t memory = 0u;
                        std::uint32_t width = 0u;
                        std::uint64_t address = 0u;
                        std::uint64_t value = 0u;
                    };
                    std::vector<memory_read> reads;
                    bool background = false;            ///< (made in the background (its reads are checked when it is installed))
                    std::uint64_t osrLoop = NoOsrLoop;  ///< (code that a call continues in from a loop (see osr_entry))
                    std::unique_ptr<tier_version> version;
                    module_description description;     ///< (made only for the baseline's code generator: the module's, the function's code
                                                        ///< the optimizer's)
                    std::vector<std::uint8_t> code;
                    std::vector<std::pair<std::size_t, std::size_t>> callSites;
                    bool optimizing = false;            ///< (the optimizing code generator made it (else it is made by the baseline's: when it is
                                                        ///< installed))
                    std::string why;
                    std::uint64_t phases[3] = {};
                    std::atomic<bool> done = false;
                };

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
                        std::uint64_t iEvents = 0u;     ///< (see events)
                    private:
                        static constexpr std::size_t NoFixup = ~std::size_t{};
                        struct label
                        {
                            std::optional<std::size_t> offset;
                            ssa::small_vector<std::size_t, 2> fixups; // positions of rel32 fields (most labels': one or two)
                        };
                        std::vector<label> iLabels;
                        std::map<std::uint64_t, std::size_t> iConstants;
                        // (cold code (rarely executed): emitted to its own section, which follows the function's code)
                        bytes iCold;
                        bool iInCold = false;
                        std::vector<std::size_t> iColdLabels;
                        std::vector<std::pair<std::size_t, std::size_t>> iColdFixups; ///< (label, fixup index)
                        // (the run of code since a barrier (see within_boundary): the instructions that can be lengthened, the 
                        // fixups, and the jumps (with the instructions fused with them))
                        struct lengthenable_instruction
                        {
                            std::size_t start;
                            std::size_t prefixes = 0u;
                        };
                        std::vector<lengthenable_instruction> iRun;
                        std::vector<std::pair<std::size_t, std::size_t>> iRunFixups;
                        std::vector<std::pair<std::size_t, std::size_t>> iRunJumps;
                        std::vector<std::size_t> iRunLabels;
                        std::optional<std::pair<std::size_t, std::size_t>> iFused;  ///< (see fusable)
                        std::optional<std::pair<std::size_t, std::size_t>> iLastJump; ///< (the jmp that code ends with: its offset and label)
                        std::vector<std::size_t> iBoundAfterJump;   ///< (labels bound after that jmp)
                        bytes& out() { return iInCold ? iCold : code; }
                    public:
                        std::size_t offset() const { return iInCold ? iCold.size() : code.size(); }
                        std::size_t new_label() { iLabels.emplace_back(); return iLabels.size() - 1u; }
                        // (changes as code is emitted or a label bound: unchanged, a register has what it had)
                        std::uint64_t events() const { return iEvents; }
                        void bind(std::size_t aLabel)
                        {
                            ++iEvents;
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
                            else
                                iRunLabels.push_back(aLabel);
                        }
                        // (padding to a multiple of aAlignment where execution does not reach it (e.g. before a function): int3s)
                        void align(std::size_t aAlignment)
                        {
                            while (code.size() % aAlignment != 0u)
                                emit8(0xCCu);
                        }
                        // (padding to a multiple of aAlignment where execution reaches it (e.g. before a loop's body, where a branch back
                        // goes: a short loop that is within one 32 byte block runs as much as twice as fast as one that crosses a block's
                        // boundary): nops; code before it does not then move (see within_boundary))
                        void align_executable(std::size_t aAlignment)
                        {
                            if (iInCold)
                                return;
                            static constexpr std::uint8_t Nops[9][9] = {
                                { 0x90 },
                                { 0x66, 0x90 },
                                { 0x0F, 0x1F, 0x00 },
                                { 0x0F, 0x1F, 0x40, 0x00 },
                                { 0x0F, 0x1F, 0x44, 0x00, 0x00 },
                                { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 },
                                { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 },
                                { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },
                                { 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 } };
                            while (code.size() % aAlignment != 0u)
                            {
                                auto const length = std::min<std::size_t>(9u, aAlignment - code.size() % aAlignment);
                                for (std::size_t i = 0u; i < length; ++i)
                                    emit8(Nops[length - 1u][i]);
                            }
                            barrier();
                        }
                        bool in_cold() const { return iInCold; }
                        void begin_cold() { iInCold = true; ++iEvents; }
                        void end_cold() { iInCold = false; ++iEvents; }
                        // (the end of a function: its cold code follows it)
                        void append_cold()
                        {
                            barrier();
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
                            // (the code's next byte, if there is room for it (no allocation: no frame of its own); else as emit_bytes)
                            if (!iInCold && !iLastJump && code.size() != code.capacity()) [[likely]]
                            {
                                ++iEvents;
                                code.push_back(aByte);
                                return;
                            }
                            emit8_slow(aByte);
                        }
#if defined(_MSC_VER)
                        __declspec(noinline)
#else
                        [[gnu::noinline]]
#endif
                        void emit8_slow(std::uint8_t aByte)
                        {
                            emit_bytes(&aByte, 1u);
                        }
                        // (little endian: as emit8 of each byte)
                        void emit32(std::uint32_t aValue) { emit_bytes(&aValue, 4u); }
                        void emit64(std::uint64_t aValue) { emit_bytes(&aValue, 8u); }
#if defined(_MSC_VER)
                        __declspec(noinline)
#else
                        [[gnu::noinline]]
#endif
                        void emit_bytes(void const* aBytes, std::size_t aCount)
                        {
                            static_assert(std::endian::native == std::endian::little);
                            iEvents += aCount;
                            auto const* const bytes = static_cast<std::uint8_t const*>(aBytes);
                            if (iInCold)
                            {
                                iCold.insert(iCold.end(), bytes, bytes + aCount);
                                return;
                            }
                            if (iLastJump) [[unlikely]]
                            {
                                iLastJump.reset();
                                iBoundAfterJump.clear();
                            }
                            code.insert(code.end(), bytes, bytes + aCount);
                        }
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
                            if (aPrefix < 0)
                                lengthenable();
                            if (aPrefix >= 0)
                                emit8(static_cast<std::uint8_t>(aPrefix));
                            rex(aW, aReg, 0, aRm, (aByteRm && aRm >= 4 && aRm < 8) || (aByteReg && aReg >= 4 && aReg < 8));
                            for (auto b : aOpcode)
                                emit8(b);
                            emit8(static_cast<std::uint8_t>(0xC0u | ((aReg & 7) << 3) | (aRm & 7)));
                        }
                        void op_rm(std::initializer_list<std::uint8_t> aOpcode, bool aW, int aReg, int aBase, std::int32_t aDisp, int aPrefix = -1)
                        {
                            if (aPrefix < 0)
                                lengthenable();
                            if (aPrefix >= 0)
                                emit8(static_cast<std::uint8_t>(aPrefix));
                            rex(aW, aReg, 0, aBase);
                            for (auto b : aOpcode)
                                emit8(b);
                            modrm_mem(aReg, aBase, aDisp);
                        }
                        // ([aBase + aIndex << aScale + aDisp] (aIndex not rsp; aScale: 0 to 3); aByteReg: aReg is a byte register (spl, bpl, sil
                        // and dil need a rex))
                        void op_bi(std::initializer_list<std::uint8_t> aOpcode, bool aW, int aReg, int aBase, int aIndex, std::int32_t aDisp, int aPrefix = -1, bool aByteReg = false, unsigned aScale = 0u)
                        {
                            lengthenable();
                            if (aPrefix >= 0)
                                emit8(static_cast<std::uint8_t>(aPrefix));
                            rex(aW, aReg, aIndex, aBase, aByteReg && aReg >= 4 && aReg < 8);
                            for (auto b : aOpcode)
                                emit8(b);
                            auto const sib = static_cast<std::uint8_t>(((aScale & 3u) << 6) | ((aIndex & 7) << 3) | (aBase & 7));
                            if (aDisp == 0 && (aBase & 7) != 5)
                            {
                                // (no displacement (rbp and r13 as the base need one))
                                emit8(static_cast<std::uint8_t>(0x04u | ((aReg & 7) << 3)));
                                emit8(sib);
                            }
                            else if (fits_i8(aDisp))
                            {
                                emit8(static_cast<std::uint8_t>(0x44u | ((aReg & 7) << 3)));
                                emit8(sib);
                                emit8(static_cast<std::uint8_t>(static_cast<std::int8_t>(aDisp)));
                            }
                            else
                            {
                                emit8(static_cast<std::uint8_t>(0x84u | ((aReg & 7) << 3)));
                                emit8(sib);
                                emit32(static_cast<std::uint32_t>(aDisp));
                            }
                        }
                        void op_sib8(std::initializer_list<std::uint8_t> aOpcode, bool aW, int aReg, int aBase, int aIndex)
                        {
                            lengthenable();
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
                                lengthenable();
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
                                lengthenable();
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
                        // (BMI2: aDst = aSrc rotated right by aCount (64 bits; the flags unchanged))
                        void rorx(int aDst, int aSrc, std::uint8_t aCount)
                        {
                            lengthenable();
                            emit8(0xC4u);
                            emit8(static_cast<std::uint8_t>(((aDst & 8) ? 0x00u : 0x80u) | 0x40u | ((aSrc & 8) ? 0x00u : 0x20u) | 0x03u));
                            emit8(0xFBu);   // (W1, no vvvv, L0, F2)
                            emit8(0xF0u);
                            emit8(static_cast<std::uint8_t>(0xC0u | ((aDst & 7) << 3) | (aSrc & 7)));
                            emit8(aCount);
                        }
                        // (cmp aReg's low 16 bits, aImm)
                        void cmp16_ri(int aReg, std::uint16_t aImm)
                        {
                            auto const start = offset();
                            lengthenable();
                            emit8(0x66u);
                            rex(false, 0, 0, aReg);
                            auto const value = static_cast<std::int16_t>(aImm);
                            if (value >= -128 && value <= 127)
                            {
                                emit8(0x83u);
                                emit8(static_cast<std::uint8_t>(0xF8u | (aReg & 7)));
                                emit8(static_cast<std::uint8_t>(static_cast<std::int8_t>(value)));
                            }
                            else
                            {
                                emit8(0x81u);
                                emit8(static_cast<std::uint8_t>(0xF8u | (aReg & 7)));
                                emit8(static_cast<std::uint8_t>(aImm));
                                emit8(static_cast<std::uint8_t>(aImm >> 8u));
                            }
                            fusable(7, start);
                        }
                        void movzx8(int aDst, int aSrc) { op_rr({ 0x0F, 0xB6 }, false, aDst, aSrc, -1, true); }
                        void movzx8_load(int aDst, int aBase, std::int32_t aDisp) { op_rm({ 0x0F, 0xB6 }, false, aDst, aBase, aDisp); }
                        void movsx8(int aDst, int aSrc) { op_rr({ 0x0F, 0xBE }, false, aDst, aSrc, -1, true); }
                        void movsx16(int aDst, int aSrc) { op_rr({ 0x0F, 0xBF }, false, aDst, aSrc); }
                        // arithmetic (aOp: add 0, or 1, and 4, sub 5, xor 6, cmp 7)
                        void alu_rr(int aOp, bool aW, int aDst, int aSrc) { auto const start = offset(); op_rr({ static_cast<std::uint8_t>(aOp * 8 + 1) }, aW, aSrc, aDst); fusable(aOp, start); }
                        void alu_rm(int aOp, bool aW, int aDst, int aBase, std::int32_t aDisp) { auto const start = offset(); op_rm({ static_cast<std::uint8_t>(aOp * 8 + 3) }, aW, aDst, aBase, aDisp); fusable(aOp, start); }
                        void alu_mr(int aOp, bool aW, int aBase, std::int32_t aDisp, int aSrc) { auto const start = offset(); op_rm({ static_cast<std::uint8_t>(aOp * 8 + 1) }, aW, aSrc, aBase, aDisp); fusable(aOp, start); }
                        void alu_ri(int aOp, bool aW, int aDst, std::int32_t aImm)
                        {
                            struct fused { assembler& a; int op; std::size_t start; ~fused() { a.fusable(op, start); } } const fusing{ *this, aOp, offset() };
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
                            struct fused { assembler& a; int op; std::size_t start; ~fused() { a.fusable(op, start); } } const fusing{ *this, aOp, offset() };
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
                        void test_rr(bool aW, int aA, int aB) { auto const start = offset(); op_rr({ 0x85 }, aW, aB, aA); fusable(7, start); }
                        void test_ri(int aReg, std::int32_t aImm) { auto const start = offset(); op_rr({ 0xF7 }, false, 0, aReg); emit32(static_cast<std::uint32_t>(aImm)); fusable(7, start); }
                        void test_mi(int aBase, std::int32_t aDisp, std::int32_t aImm) { auto const start = offset(); op_rm({ 0xF7 }, false, 0, aBase, aDisp); emit32(static_cast<std::uint32_t>(aImm)); fusable(7, start); }
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
                        void ret() { auto const start = offset(); emit8(0xC3u); within_boundary(start); }
                        void call_r(int aReg)
                        {
                            auto const start = offset();
                            rex(false, 0, 0, aReg);
                            emit8(0xFFu);
                            emit8(static_cast<std::uint8_t>(0xD0u | (aReg & 7)));
                            within_boundary(start);
                        }
                        void jmp_r(int aReg)
                        {
                            rex(false, 0, 0, aReg);
                            emit8(0xFFu);
                            emit8(static_cast<std::uint8_t>(0xE0u | (aReg & 7)));
                        }
                        void jmp(std::size_t aLabel)
                        {
                            auto const at = offset();
                            emit8(0xE9u);
                            fixup(aLabel);
                            if (!iInCold)
                                iLastJump = std::make_pair(at, aLabel);
                            within_boundary(at);
                            if (!iInCold && iLastJump)
                                iLastJump->first = code.size() - 5u;
                        }
                        void jcc(std::uint8_t aCc, std::size_t aLabel)
                        {
                            // (with the instruction that it is fused with (cmp, test, add, sub or and, just before it))
                            auto const start = iFused && iFused->second == offset() ? iFused->first : offset();
                            emit8(0x0Fu);
                            emit8(static_cast<std::uint8_t>(0x80u + aCc));
                            fixup(aLabel);
                            within_boundary(start);
                        }
                        void call(std::size_t aLabel) { auto const start = offset(); emit8(0xE8u); fixup(aLabel); within_boundary(start); }
                        // (aDst = the address of aLabel (lea aDst, [rip + rel32]))
                        void lea_label(int aDst, std::size_t aLabel)
                        {
                            rex(true, aDst, 0, 0);
                            emit8(0x8Du);
                            emit8(static_cast<std::uint8_t>(0x05u | ((aDst & 7) << 3)));
                            fixup(aLabel);
                        }
                        void jmp_m(int aBase, std::int32_t aDisp) { op_rm({ 0xFF }, false, 4, aBase, aDisp); }
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
                        // (aReg op [rip + constant] (aOp as alu_rr's: e.g. cmp 7))
                        void alu_rip(int aOp, bool aW, int aReg, std::size_t aLabel)
                        {
                            auto const start = offset();
                            rex(aW, aReg, 0, 0);
                            emit8(static_cast<std::uint8_t>(aOp * 8 + 3));
                            emit8(static_cast<std::uint8_t>(0x05u | ((aReg & 7) << 3)));
                            fixup(aLabel);
                            fusable(aOp, start);
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
                        // (code before here does not move (e.g. a position that will be patched): see within_boundary)
                        void barrier()
                        {
                            iRun.clear();
                            iRunFixups.clear();
                            iRunJumps.clear();
                            iRunLabels.clear();
                            iFused.reset();
                        }
                    private:
                        void fixup(std::size_t aLabel)
                        {
                            iLabels[aLabel].fixups.push_back(offset());
                            if (iInCold)
                                iColdFixups.emplace_back(aLabel, iLabels[aLabel].fixups.size() - 1u);
                            else
                                iRunFixups.emplace_back(aLabel, iLabels[aLabel].fixups.size() - 1u);
                            emit32(0u);
                        }
                        // (an instruction that a jump after it can be moved by: it starts here (see within_boundary))
                        void lengthenable()
                        {
                            if (!iInCold)
                                iRun.push_back(lengthenable_instruction{ code.size() });
                        }
                        // (an instruction that a following jcc can be fused with (cmp, test, add, sub and and): its start and end)
                        void fusable(int aOp, std::size_t aStart)
                        {
                            if (!iInCold && (aOp == 0 || aOp == 4 || aOp == 5 || aOp == 7))
                                iFused = std::make_pair(aStart, offset());
                        }
                        // a jump (and the instruction it is fused with) that crosses or ends at a 32 byte boundary is slow on many Intel 
                        // processors (their microcode's mitigation of the "jump conditional code" erratum): it is moved to the boundary 
                        // by lengthening instructions shortly before it with prefixes that have no effect (segment overrides), so that no 
                        // instruction is added (the labels, fixups and jumps after them move with them); a jump that moves must not then cross 
                        // a boundary that it did not, and a jump that cannot be moved stays
                        void within_boundary(std::size_t aStart)
                        {
                            if (iInCold)
                                return;
                            std::size_t constexpr Boundary = 32u;
                            std::size_t constexpr MaxPrefixes = 2u;     // (added to an instruction: more slow its decoding (and the loop))
                            std::size_t constexpr Reach = 256u;         // (how far before it instructions are lengthened (bytes))
                            auto const crosses = [&](std::size_t aBegin, std::size_t aEnd) 
                            { 
                                return aBegin / Boundary != (aEnd - 1u) / Boundary || aEnd % Boundary == 0u; 
                            };
                            auto const end = code.size();
                            if (end <= aStart || !crosses(aStart, end) || end - aStart >= Boundary)
                            {
                                if (end > aStart)
                                    iRunJumps.emplace_back(aStart, end);
                                return;
                            }
                            auto const needed = Boundary - aStart % Boundary;
                            // (the instructions before it (latest first) that are not within a jump (nor fused with one))
                            thread_local std::vector<std::pair<std::size_t, std::size_t>> tPlan; // (run index, prefixes) (scratch kept for reuse (one per jump compiled))
                            tPlan.clear();
                            std::size_t planned = 0u;
                            auto jump = iRunJumps.size();
                            for (auto i = iRun.size(); i > 0u && planned < needed; --i)
                            {
                                auto const& instruction = iRun[i - 1u];
                                if (instruction.start >= aStart)
                                    continue;
                                if (aStart - instruction.start > Reach)
                                    break;
                                while (jump > 0u && iRunJumps[jump - 1u].first > instruction.start)
                                    --jump;
                                if ((jump > 0u && instruction.start < iRunJumps[jump - 1u].second) || instruction.prefixes >= MaxPrefixes)
                                    continue;
                                auto const count = std::min(MaxPrefixes - instruction.prefixes, needed - planned);
                                tPlan.emplace_back(i - 1u, count);
                                planned += count;
                            }
                            if (planned < needed)
                            {
                                iRunJumps.emplace_back(aStart, end);
                                return;
                            }
                            auto const first = iRun[tPlan.back().first].start;
                            // (the bytes inserted before a position (aStarting: an instruction's start, whose own prefixes are its))
                            auto const shift = [&](std::size_t aPosition, bool aStarting = false)
                            {
                                std::size_t result = 0u;
                                for (auto const& p : tPlan)
                                    if (iRun[p.first].start < aPosition || (!aStarting && iRun[p.first].start == aPosition))
                                        result += p.second;
                                return result;
                            };
                            for (auto j = iRunJumps.size(); j > 0u && iRunJumps[j - 1u].first >= first; --j)
                                if (auto const& moving = iRunJumps[j - 1u]; !crosses(moving.first, moving.second) && 
                                    crosses(moving.first + shift(moving.first), moving.second + shift(moving.first)))
                                {
                                    iRunJumps.emplace_back(aStart, end);
                                    return;
                                }
                            // (the code from the first instruction lengthened, with its prefixes)
                            thread_local bytes tMoved;
                            tMoved.clear();
                            tMoved.reserve(end - first + needed);
                            std::size_t next = tPlan.size();
                            for (auto position = first; position < end; ++position)
                            {
                                while (next > 0u && iRun[tPlan[next - 1u].first].start == position)
                                {
                                    tMoved.insert(tMoved.end(), tPlan[next - 1u].second, 0x2Eu);
                                    --next;
                                }
                                tMoved.push_back(code[position]);
                            }
                            for (auto f = iRunFixups.size(); f > 0u; --f)
                            {
                                auto& position = iLabels[iRunFixups[f - 1u].first].fixups[iRunFixups[f - 1u].second];
                                if (position == NoFixup)
                                    continue;
                                if (position < first)
                                    break;
                                position += shift(position);
                            }
                            for (auto l = iRunLabels.size(); l > 0u; --l)
                            {
                                auto& offset = iLabels[iRunLabels[l - 1u]].offset;
                                if (!offset || *offset < first)
                                    break;
                                *offset += shift(*offset, true);
                            }
                            for (auto j = iRunJumps.size(); j > 0u && iRunJumps[j - 1u].first >= first; --j)
                            {
                                auto const s = shift(iRunJumps[j - 1u].first);
                                iRunJumps[j - 1u].first += s;
                                iRunJumps[j - 1u].second += s;
                            }
                            if (iFused && iFused->first >= first)
                                iFused = std::make_pair(iFused->first + shift(iFused->first, true), iFused->second + shift(iFused->second));
                            for (auto const& p : tPlan)
                                iRun[p.first].prefixes += p.second;
                            for (auto i = iRun.size(); i > 0u && iRun[i - 1u].start >= first; --i)
                                iRun[i - 1u].start += shift(iRun[i - 1u].start, true);
                            code.resize(first);
                            code.insert(code.end(), tMoved.begin(), tMoved.end());
                            iRunJumps.emplace_back(aStart + needed, end + needed);
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
                    constexpr int MemoryBaseRegister = R14;     ///< (a fixed external memory's address, in a function that accesses it)
                    constexpr int PinnedXmms[] = { 8, 9, 10, 11, 12, 13, 14, 15 };

                    constexpr int Free = -1;
                    constexpr int Locked = -2;
                    constexpr int Unavailable = -3;

                    bool query_sse41()
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

                    bool query_bmi2()
                    {
#if defined(NEOS_JIT_NO_BMI2)
                        return false; // (testing: shifts)
#elif defined(_MSC_VER)
                        int r[4];
                        __cpuid(r, 0);
                        if (r[0] < 7)
                            return false;
                        __cpuidex(r, 7, 0);
                        return ((r[1] >> 8) & 1) != 0;
#else
                        unsigned a, b, c, d;
                        if (!__get_cpuid_count(7u, 0u, &a, &b, &c, &d))
                            return false;
                        return ((b >> 8) & 1u) != 0u;
#endif
                    }

                    bool has_bmi2()
                    {
                        static bool const sBmi2 = query_bmi2();
                        return sBmi2;
                    }

                    bool has_sse41()
                    {
                        // (asked once: CPUID can be slow (e.g. in a virtual machine, where it exits to the hypervisor), and a program that
                        // compiles code as it runs (e.g. each eval) compiles many modules)
                        static bool const sSse41 = query_sse41();
                        return sSse41;
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
                    std::uint64_t trunc_f64_u32(double a, context* c) { return truncate<std::uint32_t>(a, c); }
                    std::uint64_t trunc_f32_i64(float a, context* c) { return truncate<std::int64_t>(a, c); }
                    std::uint64_t trunc_f32_u64(float a, context* c) { return truncate<std::uint64_t>(a, c); }
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

                    // --- imports called directly (see import_call): the conversions around a runtime plugin function's call, and the
                    // host's object functions; each has host_thunk's signature (aError: the plugin function's error (null: none)) and 
                    // result (1: an exception (it traps))

                    template <typename Function>
                    std::uint32_t guarded(context* aContext, Function const& aFunction) noexcept
                    {
                        try
                        {
                            aFunction(aContext->module->description);
                            return 0u;
                        }
                        catch (...)
                        {
                            aContext->module->exception = std::current_exception();
                            return 1u;
                        }
                    }

                    // (the arguments (in aBuffer) as the C function takes them: objects' handles become their addresses)
                    std::uint32_t plugin_arguments(context* aContext, std::uint32_t aFunction, value* aBuffer, char const*) noexcept
                    {
                        return guarded(aContext, [&](module_description const& aModule)
                        {
                            auto const& host = aModule.host;
                            auto const& parameters = aModule.functions[aFunction].plugin->parameters;
                            for (std::size_t i = 0u; i < parameters.size(); ++i)
                            {
                                auto& v = aBuffer[i];
                                switch (parameters[i])
                                {
                                case NEOS_RT_BOOL:
                                    v = static_cast<std::uint32_t>(v) != 0u ? 1u : 0u;
                                    break;
                                case NEOS_RT_CHAR:
                                    v = static_cast<value>(static_cast<std::int64_t>(static_cast<signed char>(static_cast<std::uint8_t>(v))));
                                    break;
                                case NEOS_RT_OBJECT:
                                    v = static_cast<value>(reinterpret_cast<std::uintptr_t>(host.objectPointer(host.host, static_cast<std::uint32_t>(v))));
                                    break;
                                case NEOS_RT_STRING:
                                    v = static_cast<value>(reinterpret_cast<std::uintptr_t>(host.stringPointer(host.host, static_cast<std::uint32_t>(v))));
                                    break;
                                case NEOS_RT_INTEGER:
                                    // (an object: its handle (shifted) becomes its address, with the low bit set)
                                    if ((v & 1u) != 0u)
                                        v = static_cast<value>(reinterpret_cast<std::uintptr_t>(host.objectPointer(host.host, static_cast<std::uint32_t>(v >> 1u)))) | 1u;
                                    break;
                                default:
                                    break;
                                }
                            }
                        });
                    }

                    // (the C function's error, else its result (in aBuffer[0]) as a value: an object becomes a handle for its new 
                    // reference)
                    std::uint32_t plugin_result(context* aContext, std::uint32_t aFunction, value* aBuffer, char const* aError) noexcept
                    {
                        return guarded(aContext, [&](module_description const& aModule)
                        {
                            auto const& host = aModule.host;
                            if (aError != nullptr)
                            {
                                host.pluginError(host.host, aFunction, aError);
                                aBuffer[0] = 0u;
                                return;
                            }
                            auto& v = aBuffer[0];
                            switch (aModule.functions[aFunction].plugin->result)
                            {
                            case NEOS_RT_BOOL:
                                v = (v & 0xFFu) != 0u ? 1u : 0u;
                                break;
                            case NEOS_RT_CHAR:
                                v = static_cast<value>(static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<signed char>(static_cast<std::uint8_t>(v)))));
                                break;
                            case NEOS_RT_OBJECT:
                                v = static_cast<value>(host.objectHandle(host.host, reinterpret_cast<void*>(static_cast<std::uintptr_t>(v))));
                                break;
                            case NEOS_RT_STRING:
                                v = static_cast<value>(host.stringAddress(host.host, reinterpret_cast<char const*>(static_cast<std::uintptr_t>(v))));
                                break;
                            case NEOS_RT_INTEGER:
                                // (an object: a handle for the new reference (shifted), with the low bit set)
                                if ((v & 1u) != 0u)
                                    v = (static_cast<value>(host.objectHandle(host.host, reinterpret_cast<void*>(static_cast<std::uintptr_t>(v & ~value{ 1u })))) << 1u) | 1u;
                                break;
                            default:
                                break;
                            }
                        });
                    }

                    std::uint32_t object_retain(context* aContext, std::uint32_t, value* aBuffer, char const*) noexcept
                    {
                        return guarded(aContext, [&](module_description const& aModule) { aModule.host.objectRetain(aModule.host.host, static_cast<std::uint32_t>(aBuffer[0])); });
                    }

                    std::uint32_t object_release(context* aContext, std::uint32_t, value* aBuffer, char const*) noexcept
                    {
                        return guarded(aContext, [&](module_description const& aModule) { aModule.host.objectRelease(aModule.host.host, static_cast<std::uint32_t>(aBuffer[0])); });
                    }

                    // --- the slow paths of the object table's uses that generated code makes itself (see object_table)

                    // (an argument (aValue) that is an object's handle (aInteger: an integer's value that can be one) that generated
                    // code did not find in the table: the host's (which traps if it is not valid))
                    std::uint32_t host_object_argument(context* aContext, std::uint32_t, value* aValue, char const* aInteger) noexcept
                    {
                        return guarded(aContext, [&](module_description const& aModule)
                        {
                            auto const& host = aModule.host;
                            auto& v = *aValue;
                            if (aInteger == nullptr)
                                v = static_cast<value>(reinterpret_cast<std::uintptr_t>(host.objectPointer(host.host, static_cast<std::uint32_t>(v))));
                            else if ((v & 1u) != 0u)
                                v = static_cast<value>(reinterpret_cast<std::uintptr_t>(host.objectPointer(host.host, static_cast<std::uint32_t>(v >> 1u)))) | 1u;
                        });
                    }

                    // (a string argument (aValue: its address) that generated code did not find in memory: the host's (which traps))
                    std::uint32_t host_string_argument(context* aContext, std::uint32_t, value* aValue, char const*) noexcept
                    {
                        return guarded(aContext, [&](module_description const& aModule)
                        {
                            *aValue = static_cast<value>(reinterpret_cast<std::uintptr_t>(aModule.host.stringPointer(aModule.host.host, static_cast<std::uint32_t>(*aValue))));
                        });
                    }

                    // (a string result (in aBuffer[0]): copied into memory (its address))
                    std::uint32_t host_string_result(context* aContext, std::uint32_t, value* aBuffer, char const*) noexcept
                    {
                        return guarded(aContext, [&](module_description const& aModule)
                        {
                            aBuffer[0] = static_cast<value>(aModule.host.stringAddress(aModule.host.host, reinterpret_cast<char const*>(static_cast<std::uintptr_t>(aBuffer[0]))));
                        });
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

                    // (an instruction that the baseline's code generator has (see code_generator::instruction))
                    bool baseline_instruction(std::uint8_t aOp)
                    {
                        if (aOp <= 0x05u || (aOp >= 0x0Bu && aOp <= 0x0Du) || aOp == 0x0Fu || aOp == 0x10u || aOp == 0x11u || aOp == 0x1Au || aOp == 0x1Bu)
                            return true;
                        return (aOp >= 0x20u && aOp <= 0x24u) || (aOp >= 0x28u && aOp <= 0x3Eu) || (aOp >= 0x41u && aOp <= 0xC1u);
                    }

                    // (the optimizing tier's helpers, that generated code calls: a function's counter has reached 0 (its optimized 
                    // code's address, or null to go on with its code); a guard of an optimized function's inlined call failed)
                    void const* tier_up(context* aContext, std::uint32_t aFunction) noexcept;
                    // (a function's loop counter has reached 0; aLoop: a loop (its token) that the call can continue in optimized code from
                    // (else NoOsrLoop): that code's address (see code_generator::loop_counter), or null to go on with its code)
                    constexpr std::size_t MaxOsrLocals = 64u;       ///< (a function's parameters and locals: the most that a loop's optimized code takes)
                    void const* tier_loop(context* aContext, std::uint32_t aFunction, std::uint64_t aLoop) noexcept;
                    // (a function compiled when it is first called (see compiled_module::lazy): its code's address (null if it cannot be
                    // compiled: the module's exception))
                    void const* lazy_compile(context* aContext, std::uint32_t aFunction) noexcept;
                    void guard_failed(context* aContext, compiled_module::tier_version const* aVersion, std::uint32_t aSite) noexcept;
                    void deopt_taken(context* aContext, compiled_module::tier_version const* aVersion, std::uint32_t aDeopt) noexcept;

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
                            std::size_t position = 0u;          ///< (a block's or a loop's token)
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
                        // (aSingle: the code of one function (see generate_function and generate_lazy): no labels for the others)
                        code_generator(module_description const& aModule, bool aSingle = false) :
                            iModule{ aModule }, iTokens{ *aModule.tokens }, iSse41{ has_sse41() }, iSingle{ aSingle }
                        {
                            for (std::size_t i = 0u; i < TrapCount; ++i)
                                iTrapLabels[i] = a.new_label();
                            iTrapFromContext = a.new_label();
                            iExit = a.new_label();
                            iTrampolineReturn = a.new_label();
                            if (aSingle)
                                iSingleLabel = a.new_label();
                            else
                                for (std::size_t f = 0u; f < aModule.functions.size(); ++f)
                                    iFunctionLabels.push_back(a.new_label());
                        }
                    public:
                        void generate(compiled_module& aOutput)
                        {
                            iIndirect = aOutput.indirect.data();
                            if (aOutput.tiering)
                            {
                                iCounters = aOutput.counters.data();
                                iLoopCounters = aOutput.loopCounters.data();
                                iOsr = aOutput.osr;
                                aOutput.deoptFrames.resize(iModule.functions.size());
                                iDeoptFrames = &aOutput.deoptFrames;
                                iCallFlags = &aOutput.callFlags;
                            }
                            aOutput.primaryMemory = iPrimaryMemory = primary_memory();
                            trampoline();
                            for (std::uint32_t f = 0u; f < iModule.functions.size(); ++f)
                                if (!iModule.functions[f].imported)
                                    function(f);
                            trap_stubs();
                            a.emit_constants();
                            a.resolve();
                            for (std::uint32_t f = 0u; f < iModule.functions.size(); ++f)
                                aOutput.functionOffsets.push_back(iModule.functions[f].imported ? 0u : a.label_offset(iFunctionLabels[f]));
                            for (auto const& site : iCallSites)
                                aOutput.callSites.emplace_back(a.label_offset(site.first), site.second);
                            std::sort(aOutput.callSites.begin(), aOutput.callSites.end());
                            for (auto& d : iDeoptPoints)
                            {
                                d.point.resume = a.label_offset(d.label);
                                if (d.returnLabel)
                                    d.point.returned = a.label_offset(*d.returnLabel);
                                aOutput.deoptPoints[d.position] = std::move(d.point);
                            }
                            aOutput.trampolineReturn = a.label_offset(iTrampolineReturn);
                            aOutput.exitOffset = a.label_offset(iExit);
                            iCode = std::move(a.code);
                        }
                        // (the optimizing tier: one function (aModule's code is elsewhere: this one's calls are through its indirect table,
                        // and its traps leave through its exit); aOrigins: the positions that its tokens' calls are reported at)
                        void generate_function(compiled_module& aModule, std::uint32_t aFunction, std::vector<std::size_t> const& aOrigins,
                            compiled_module::tier_version const* aVersion, std::vector<std::pair<std::size_t, std::size_t>>& aCallSites)
                        {
                            iIndirect = aModule.indirect.data();
                            iTier = &aModule;
                            iPrimaryMemory = aModule.primaryMemory;
                            iOrigins = &aOrigins;
                            iTierVersion = aVersion;
                            function(aFunction);
                            auto const exit = reinterpret_cast<std::uint64_t>(static_cast<std::uint8_t const*>(aModule.code) + aModule.exitOffset);
                            for (std::uint32_t t = 1u; t < TrapCount; ++t)
                            {
                                a.bind(iTrapLabels[t]);
                                a.store_imm(false, R15, ContextTrap, static_cast<std::int32_t>(t));
                                a.mov_ri(RAX, t, false);
                                a.mov_ri(RCX, exit, true);
                                a.jmp_r(RCX);
                            }
                            a.bind(iTrapFromContext);
                            a.load(false, RAX, R15, ContextTrap);
                            a.mov_ri(RCX, exit, true);
                            a.jmp_r(RCX);
                            a.bind(iExit);
                            a.bind(iTrampolineReturn);
                            a.emit_constants();
                            a.resolve();
                            for (auto const& site : iCallSites)
                                aCallSites.emplace_back(a.label_offset(site.first), site.second);
                            std::sort(aCallSites.begin(), aCallSites.end());
                            iCode = std::move(a.code);
                        }
                        // (the module's code when its functions are compiled when they are first called (see compiled_module::lazy):
                        // the trampoline, each function's entry, and lazy_compile's thunk (the entries' first jump))
                        void generate_entries(compiled_module& aOutput)
                        {
                            iIndirect = aOutput.indirect.data();
                            if (aOutput.tiering)
                                aOutput.deoptFrames.resize(iModule.functions.size());
                            aOutput.primaryMemory = iPrimaryMemory = primary_memory();
                            trampoline();
                            a.barrier();
                            // (lazy_compile(context, the function (r11d)): its code's address (null: it could not be compiled (an exception)),
                            // which the entry then jumps to; at an entry, the caller's registers that native code does not preserve are saved
                            // by it, except its xmms (on System V: see tier_entry))
                            a.align(FunctionAlignment);
                            auto const thunk = a.new_label();
                            a.bind(thunk);
                            a.push(RBP);
                            a.mov_rr(true, RBP, RSP);
                            a.sub_rsp_imm32(Windows ? ShadowSpace : ShadowSpace + 128);
                            if (!Windows)
                                for (int x = 8; x < 16; ++x)
                                    a.movdqu_store(RSP, ShadowSpace + (x - 8) * 16, x);
                            a.mov_rr(true, NativeIntegerArguments[0], R15);
                            a.mov_rr(false, NativeIntegerArguments[1], R11);
                            a.mov_ri(RAX, reinterpret_cast<std::uint64_t>(&lazy_compile), true);
                            a.call_r(RAX);
                            if (!Windows)
                                for (int x = 8; x < 16; ++x)
                                    a.movdqu_load(x, RSP, ShadowSpace + (x - 8) * 16);
                            a.mov_rr(true, RSP, RBP);
                            a.pop(RBP);
                            a.test_rr(true, RAX, RAX);
                            a.jcc(E, iTrapLabels[TrapHostException]);
                            a.jmp_r(RAX);
                            a.barrier();
                            auto const thunkOffset = a.label_offset(thunk);
                            // (each entry: mov r11d, its index; jmp the thunk (then, once it is compiled: jmp [rip+2]; its code's address))
                            for (std::uint32_t f = 0u; f < iModule.functions.size(); ++f)
                            {
                                if (iModule.functions[f].imported)
                                    continue;
                                a.align(16u);
                                a.barrier();
                                a.bind(iFunctionLabels[f]);
                                auto const start = a.offset();
                                a.emit8(0x41u);
                                a.emit8(0xBBu);
                                a.emit32(f);
                                a.emit8(0xE9u);
                                a.emit32(static_cast<std::uint32_t>(static_cast<std::int64_t>(thunkOffset) - static_cast<std::int64_t>(start + 11u)));
                                for (int i = 0; i < 5; ++i)
                                    a.emit8(0xCCu);
                                a.barrier();
                            }
                            trap_stubs();
                            a.emit_constants();
                            a.resolve();
                            for (std::uint32_t f = 0u; f < iModule.functions.size(); ++f)
                                aOutput.functionOffsets.push_back(iModule.functions[f].imported ? 0u : a.label_offset(iFunctionLabels[f]));
                            aOutput.trampolineReturn = a.label_offset(iTrampolineReturn);
                            aOutput.exitOffset = a.label_offset(iExit);
                            iCode = std::move(a.code);
                        }
                        // (a function compiled when it is first called (see compiled_module::lazy): as generate's (its calls through the 
                        // indirect table, its traps leaving through the module's exit); its call sites, deopt points (their offsets in its
                        // code) and frame)
                        struct lazy_function
                        {
                            std::size_t entry = 0u;
                            std::vector<std::pair<std::size_t, std::size_t>> callSites;
                            std::vector<std::pair<std::size_t, compiled_module::deopt_point>> deoptPoints;
                            compiled_module::deopt_frame frame;
                        };
                        void generate_lazy(compiled_module& aModule, std::uint32_t aFunction, lazy_function& aOutput)
                        {
                            iIndirect = aModule.indirect.data();
                            iLazy = true;
                            if (aModule.tiering)
                            {
                                iCounters = aModule.counters.data();
                                iLoopCounters = aModule.loopCounters.data();
                                iOsr = aModule.osr;
                                iCallFlags = &aModule.callFlags;
                                iLazyFrame = &aOutput.frame;
                            }
                            iPrimaryMemory = aModule.primaryMemory;
                            function(aFunction);
                            auto const exit = reinterpret_cast<std::uint64_t>(static_cast<std::uint8_t const*>(aModule.code) + aModule.exitOffset);
                            for (std::uint32_t t = 1u; t < TrapCount; ++t)
                            {
                                a.bind(iTrapLabels[t]);
                                a.store_imm(false, R15, ContextTrap, static_cast<std::int32_t>(t));
                                a.mov_ri(RAX, t, false);
                                a.mov_ri(RCX, exit, true);
                                a.jmp_r(RCX);
                            }
                            a.bind(iTrapFromContext);
                            a.load(false, RAX, R15, ContextTrap);
                            a.mov_ri(RCX, exit, true);
                            a.jmp_r(RCX);
                            a.bind(iExit);
                            a.bind(iTrampolineReturn);
                            a.emit_constants();
                            a.resolve();
                            aOutput.entry = a.label_offset(iSingleLabel);
                            for (auto const& site : iCallSites)
                                aOutput.callSites.emplace_back(a.label_offset(site.first), site.second);
                            std::sort(aOutput.callSites.begin(), aOutput.callSites.end());
                            for (auto& d : iDeoptPoints)
                            {
                                d.point.resume = a.label_offset(d.label);
                                if (d.returnLabel)
                                    d.point.returned = a.label_offset(*d.returnLabel);
                                aOutput.deoptPoints.emplace_back(d.position, std::move(d.point));
                            }
                            iCode = std::move(a.code);
                        }
                        std::size_t function_offset(std::uint32_t aFunction) const { return a.label_offset(function_label(aFunction)); }
                        bytes& code() { return iCode; }
                    private:
                        std::size_t function_label(std::uint32_t aFunction) const
                        {
                            if (iSingle)
                            {
                                if (aFunction != iFunctionIndex)
                                    throw std::logic_error("neos::jit: another function's label");
                                return iSingleLabel;
                            }
                            return iFunctionLabels[aFunction];
                        }
                        // --- module level code
                        // (the fixed external memory that all of the module's code accesses (if it accesses one, and only one): see 
                        // compiled_module::primaryMemory)
                        std::uint32_t primary_memory() const
                        {
                            std::uint32_t primary = 0u;
                            for (auto const& f : iModule.functions)
                            {
                                if (f.imported)
                                    continue;
                                for (std::size_t p = f.codeBegin; p < f.codeEnd;)
                                {
                                    auto const op = static_cast<std::uint8_t>(iTokens[p]);
                                    if (op >= 0x28u && op <= 0x3Eu)
                                        if (auto const memory = static_cast<std::uint32_t>(iTokens[p + 1u] >> 32u); memory != 0u)
                                        {
                                            if (memory > NEOS_RT_EXTERNAL_MEMORIES || !external_memory(memory).fixed || (primary != 0u && memory != primary))
                                                return 0u;
                                            primary = memory;
                                        }
                                    p += 1u + immediate_count(op, iTokens, p);
                                }
                            }
                            return primary;
                        }
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
                            if (iPrimaryMemory != 0u)
                                a.mov_ri(MemoryBaseRegister, reinterpret_cast<std::uint64_t>(external_memory(iPrimaryMemory).base), true);
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
                            a.bind(iTrampolineReturn);
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
                            iMemoryBase = -1;
                            iMemoryBaseIndex = 0u;
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
                            iTeeCache.reset();
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
                                    if (!aXmm && iTeeCache && iTeeCache->reg == regs[i])
                                        iTeeCache.reset();
                                    return regs[i];
                                }
                            // spill the deepest register entry
                            if (!aXmm)
                                iTeeCache.reset();
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
                            thread_local std::vector<bool> tWritten; // (scratch kept for reuse (one per block, loop and if compiled))
                            tWritten.assign(iLocals.size(), false);
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
                                else if ((op == 0x21 || op == 0x22) && iTokens[p + 1u] < tWritten.size())
                                    tWritten[static_cast<std::size_t>(iTokens[p + 1u])] = true;
                                p += 1u + immediate_count(op, iTokens, p);
                            }
                            for (std::size_t i = 0u; i < iStack.size(); ++i)
                            {
                                auto& e = iStack[i];
                                if (e.k == kind::Reg)
                                    spill(i);
                                else if (e.k == kind::Local && tWritten[e.local])
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
                            std::int32_t stackIndex = 0;
                            for (std::size_t i = 0u; i < aArguments.size(); ++i)
                            {
                                auto const disp = ShadowSpace + static_cast<std::int32_t>(i) * 8;
                                bool const inRegister = Windows ? i < std::size(NativeIntegerArguments) :
                                    aArguments[i].isFloat ? floatIndex < 8 : intIndex < static_cast<int>(std::size(NativeIntegerArguments));
                                if (!inRegister)
                                {
                                    // (an argument passed on the stack (beyond those in registers): moved down from where it was staged to
                                    // where the callee finds it (the stack arguments follow the shadow space (Windows) or start at rsp
                                    // (System V)); the arguments before it have been taken, so the move overwrites none still needed)
                                    auto const slot = Windows ? disp - 8 * static_cast<std::int32_t>(std::size(NativeIntegerArguments)) : 8 * stackIndex++;
                                    if (slot != disp)
                                    {
                                        a.load(true, RAX, RSP, disp);
                                        a.store(true, RSP, slot, RAX);
                                    }
                                }
                                else if (aArguments[i].isFloat)
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
                        // (the position that a call at a token is reported at (see compiled_module::callSites))
                        std::size_t origin(std::size_t aPosition) const
                        {
                            return iOrigins != nullptr && aPosition < iOrigins->size() ? (*iOrigins)[aPosition] : aPosition;
                        }
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
                        // (the next of PinnedGprs that may hold a local (not the fixed external memory's address))
                        std::size_t& skip_memory_base(std::size_t& aGprs) const
                        {
                            while (aGprs < std::size(PinnedGprs) && PinnedGprs[aGprs] == iMemoryBase)
                                ++aGprs;
                            return aGprs;
                        }
                        void choose_pinned_locals(std::size_t aBegin, std::size_t aEnd)
                        {
                            // (locals by their (static) uses, weighted by loop depth (and less within ifs), outside cold code; registers 
                            // that calls do not preserve only for locals used more than the calls (outside cold code) that would save them)
                            thread_local std::vector<std::uint64_t> tScore; // (scratch kept for reuse (one per function compiled))
                            tScore.assign(iLocals.size(), 0u);
                            std::uint64_t calls = 0u;
                            thread_local std::vector<std::uint8_t> tLoops; // (0 block, 1 loop, 2 if)
                            tLoops.clear();
                            std::size_t loopDepth = 0u;
                            std::size_t ifDepth = 0u;
                            thread_local std::vector<std::pair<std::size_t, std::size_t>> tCold; // (cold code: [begin, end))
                            tCold.clear();
                            for (std::size_t p = aBegin; p < aEnd;)
                            {
                                while (!tCold.empty() && p >= tCold.back().second)
                                    tCold.pop_back();
                                bool const isCold = !tCold.empty() && p >= tCold.back().first;
                                auto const op = static_cast<std::uint8_t>(iTokens[p]);
                                auto const weight = (std::uint64_t{ 64 } << (3u * std::min<std::size_t>(loopDepth, 6u))) >> std::min<std::size_t>(ifDepth, 6u);
                                switch (op)
                                {
                                case 0x02:
                                    tLoops.push_back(0u);
                                    if (!isCold)
                                        if (auto const tail = cold_tail(p))
                                            tCold.emplace_back(*tail, block_end(p + 2u));
                                    break;
                                case 0x04:
                                    tLoops.push_back(2u);
                                    ++ifDepth;
                                    break;
                                case 0x03:
                                    tLoops.push_back(1u);
                                    ++loopDepth;
                                    break;
                                case 0x0B:
                                    if (!tLoops.empty())
                                    {
                                        if (tLoops.back() == 1u)
                                            --loopDepth;
                                        else if (tLoops.back() == 2u)
                                            --ifDepth;
                                        tLoops.pop_back();
                                    }
                                    break;
                                case 0x10:
                                    if (!isCold)
                                        calls += weight;
                                    break;
                                case 0x20: case 0x21: case 0x22:
                                    {
                                        auto const l = static_cast<std::size_t>(iTokens[p + 1u]);
                                        if (l < tScore.size() && !isCold)
                                            tScore[l] += weight;
                                    }
                                    break;
                                default:
                                    break;
                                }
                                p += 1u + immediate_count(op, iTokens, p);
                            }
                            thread_local std::vector<std::size_t> tOrder;
                            tOrder.assign(iLocals.size(), 0u);
                            for (std::size_t i = 0u; i < tOrder.size(); ++i)
                                tOrder[i] = i;
                            std::stable_sort(tOrder.begin(), tOrder.end(), [&](std::size_t l, std::size_t r) { return tScore[l] > tScore[r]; });
                            std::size_t gprs = 0u;
                            std::size_t xmms = 0u;
                            for (auto l : tOrder)
                            {
                                if (tScore[l] == 0u)
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
                                else if (skip_memory_base(gprs) < std::size(PinnedGprs) && (!native_volatile(PinnedGprs[gprs]) || tScore[l] > 2u * calls))
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
                            thread_local std::vector<std::size_t> tRegion; // (the region of a local's first use, which set it) (scratch kept for reuse (one per function compiled))
                            thread_local std::vector<bool> tOpen;          // (regions: open or closed)
                            thread_local std::vector<std::size_t> tStack;
                            tRegion.assign(iLocals.size(), None);
                            tOpen.assign(1u, true);
                            tStack.assign(1u, 0u);
                            for (std::size_t p = aBegin; p < aEnd;)
                            {
                                auto const op = static_cast<std::uint8_t>(iTokens[p]);
                                switch (op)
                                {
                                case 0x02: case 0x03: case 0x04:
                                    tStack.push_back(tOpen.size());
                                    tOpen.push_back(true);
                                    break;
                                case 0x05:
                                    if (tStack.size() > 1u)
                                    {
                                        tOpen[tStack.back()] = false;
                                        tStack.back() = tOpen.size();
                                        tOpen.push_back(true);
                                    }
                                    break;
                                case 0x0B:
                                    if (tStack.size() > 1u)
                                    {
                                        tOpen[tStack.back()] = false;
                                        tStack.pop_back();
                                    }
                                    break;
                                case 0x20: case 0x21: case 0x22:
                                    {
                                        auto const l = static_cast<std::size_t>(iTokens[p + 1u]);
                                        if (l < aParameters || l >= iLocals.size() || zero[l])
                                            break;
                                        if (tRegion[l] == None)
                                        {
                                            if (op == 0x20)
                                                zero[l] = true;
                                            else
                                                tRegion[l] = tStack.back();
                                        }
                                        else if (!tOpen[tRegion[l]])
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
                        // (the optimizing tier: the function's counter (of calls before it is optimized) is decremented; at 0, tier_up (its
                        // optimized code is jumped to, if it has it). Its 18 bytes (at the function's entry: not moved) are replaced by a jump to 
                        // its optimized code when it has it)
                        void tier_entry(std::uint32_t aIndex)
                        {
                            a.barrier();
                            auto const cold = a.new_label();
                            auto const resume = a.new_label();
                            a.emit8(0x48u);     // movabs rax, counter
                            a.emit8(0xB8u);
                            a.emit64(reinterpret_cast<std::uint64_t>(&iCounters[aIndex]));
                            a.emit8(0xFFu);     // dec dword [rax]
                            a.emit8(0x08u);
                            a.jcc(E, cold);
                            a.barrier();
                            a.bind(resume);
                            a.begin_cold();
                            a.bind(cold);
                            // (at the entry: the arguments are on the stack (the caller's outgoing area); the caller's pinned registers that 
                            // native code does not preserve are saved by it, except its xmms (on System V))
                            a.push(RBP);
                            a.mov_rr(true, RBP, RSP);
                            a.sub_rsp_imm32(Windows ? ShadowSpace : ShadowSpace + 128);
                            if (!Windows)
                                for (int x = 8; x < 16; ++x)
                                    a.movdqu_store(RSP, ShadowSpace + (x - 8) * 16, x);
                            a.mov_rr(true, NativeIntegerArguments[0], R15);
                            a.mov_ri(NativeIntegerArguments[1], aIndex, false);
                            a.mov_ri(RAX, reinterpret_cast<std::uint64_t>(&tier_up), true);
                            a.call_r(RAX);
                            if (!Windows)
                                for (int x = 8; x < 16; ++x)
                                    a.movdqu_load(x, RSP, ShadowSpace + (x - 8) * 16);
                            a.mov_rr(true, RSP, RBP);
                            a.pop(RBP);
                            a.test_rr(true, RAX, RAX);
                            a.jcc(E, resume);
                            a.jmp_r(RAX);
                            a.end_cold();
                        }
                        // (the optimizing tier: a loop's head (of a function with a frame of its own, in its own code): the function's loop
                        // counter is decremented; at 0, tier_loop (the function is optimized: its later calls run its optimized code). At a 
                        // loop's head, the operand stack is in memory (see flush): RAX is free; every register that native code does not
                        // preserve is saved around the call. A loop that the call can continue in optimized code from (see osr_loop): if
                        // tier_loop returns that code's address, its locals are its arguments (as a call's), and its results the function's
                        // (aRotated: a rotated loop's (its counter is at its body, after its head))
                        void loop_counter(std::size_t aLoop, control const* aRotated)
                        {
                            if (iLoopCounters == nullptr || iTier != nullptr || iFunction->frameless)
                                return;
                            iTeeCache.reset();
                            bool const osr = osr_loop(aRotated);
                            if (osr)
                                iOutgoing = std::max(iOutgoing, 8 * static_cast<std::int32_t>(iLocals.size()));
                            a.barrier();
                            auto const cold = a.new_label();
                            auto const resume = a.new_label();
                            a.mov_ri(RAX, reinterpret_cast<std::uint64_t>(&iLoopCounters[iFunctionIndex]), true);
                            a.emit8(0xFFu);     // dec dword [rax]
                            a.emit8(0x08u);
                            a.jcc(E, cold);
                            a.barrier();
                            a.bind(resume);
                            // (cold code (unless the loop is in cold code: then where it is, jumped over))
                            slow_path(cold, resume, [&]()
                            {
                                static constexpr int Saved[] = { RAX, RCX, RDX, RSI, RDI, R8, R9, R10, R11 };
                                constexpr std::int32_t GprsAt = ShadowSpace;
                                constexpr std::int32_t XmmsAt = GprsAt + 8 * static_cast<std::int32_t>(std::size(Saved));
                                constexpr std::int32_t Size = (XmmsAt + 16 * 16 + 15) & ~15;   // (rsp stays aligned)
                                constexpr std::int32_t R11At = GprsAt + 8 * 8;
                                static_assert(Saved[8] == R11);
                                auto const restore = [&]()
                                {
                                    for (int x = 0; x < 16; ++x)
                                        a.movdqu_load(x, RSP, XmmsAt + 16 * x);
                                    for (std::size_t i = 0u; i < std::size(Saved); ++i)
                                        a.load(true, Saved[i], RSP, GprsAt + 8 * static_cast<std::int32_t>(i));
                                    a.lea(RSP, RSP, Size);
                                };
                                a.sub_rsp_imm32(Size);
                                for (std::size_t i = 0u; i < std::size(Saved); ++i)
                                    a.store(true, RSP, GprsAt + 8 * static_cast<std::int32_t>(i), Saved[i]);
                                for (int x = 0; x < 16; ++x)
                                    a.movdqu_store(RSP, XmmsAt + 16 * x, x);
                                a.mov_rr(true, NativeIntegerArguments[0], R15);
                                a.mov_ri(NativeIntegerArguments[1], iFunctionIndex, false);
                                a.mov_ri(NativeIntegerArguments[2], osr ? aLoop : NoOsrLoop, true);
                                a.mov_ri(RAX, reinterpret_cast<std::uint64_t>(&tier_loop), true);
                                a.call_r(RAX);
                                if (osr)
                                {
                                    auto const stay = a.new_label();
                                    a.test_rr(true, RAX, RAX);
                                    a.jcc(E, stay);
                                    // (the call continues in optimized code: its locals (their values here) are that code's arguments; its
                                    // results are this call's)
                                    a.store(true, RSP, R11At, RAX);
                                    restore();
                                    for (std::size_t i = 0u; i < iLocals.size(); ++i)
                                    {
                                        auto const& l = iLocals[i];
                                        auto const disp = 8 * static_cast<std::int32_t>(i);
                                        if (l.pinned)
                                        {
                                            if (is_float(l.type))
                                                a.movf_store(true, RSP, disp, l.reg);
                                            else
                                                a.store(true, RSP, disp, l.reg);
                                        }
                                        else
                                        {
                                            a.load(true, RAX, RBP, l.disp);
                                            a.store(true, RSP, disp, RAX);
                                        }
                                    }
                                    a.call_r(R11);
                                    // (where it returns to: a frame making a call (see walk), at the loop)
                                    iCallSites.emplace_back(a.new_label(), origin(aLoop));
                                    a.bind(iCallSites.back().first);
                                    a.jmp(iEpilogue);
                                    a.bind(stay);
                                }
                                restore();
                            });
                        }
                        // (whether a call can continue in optimized code from a loop of its function's (see tier_loop): one whose head is
                        // at the operand stack's base, within no other loop (nor a block whose result is a condition); a rotated loop's head
                        // (its code before its counter) only reads (the optimized code runs it again))
                        bool osr_loop(control const* aRotated) const
                        {
                            if (!iStack.empty() || iLocals.size() > MaxOsrLocals || !iOsr)
                                return false;
                            for (auto const& c : iControl)
                                if (&c != aRotated && (c.kind == control::Loop || c.condition))
                                    return false;
                            if (aRotated != nullptr)
                                for (std::size_t p = aRotated->headBegin; p < aRotated->headEnd;)
                                {
                                    auto const op = static_cast<std::uint8_t>(iTokens[p]);
                                    bool const reads = op == 0x20u || op == 0x23u || op == 0x1Au || op == 0x1Bu || (op >= 0x28u && op <= 0x35u) ||
                                        (op >= 0x41u && op <= 0xC4u);
                                    if (!reads)
                                        return false;
                                    p += 1u + immediate_count(op, iTokens, p);
                                }
                            return true;
                        }
                        void function(std::uint32_t aIndex)
                        {
                            reset_function();
                            auto const& f = iModule.functions[aIndex];
                            iFunction = &f;
                            iFunctionIndex = aIndex;
                            if (f.results->size() > MaxResults)
                                throw unsupported{ "too many results" };
                            for (auto t : *f.parameters)
                                iLocals.push_back(local_info{ t });
                            for (auto t : *f.locals)
                                iLocals.push_back(local_info{ t });
                            // (a fixed external memory that its code accesses (the first): its address in a register (callee saved: 
                            // the function saves it))
                            for (std::size_t p = f.codeBegin; p < f.codeEnd;)
                            {
                                auto const op = static_cast<std::uint8_t>(iTokens[p]);
                                if (op >= 0x28u && op <= 0x3Eu)
                                    if (auto const memory = static_cast<std::uint32_t>(iTokens[p + 1u] >> 32u);
                                        memory != 0u && memory <= NEOS_RT_EXTERNAL_MEMORIES && external_memory(memory).fixed)
                                    {
                                        iMemoryBase = MemoryBaseRegister;
                                        iMemoryBaseIndex = memory;
                                        break;
                                    }
                                p += 1u + immediate_count(op, iTokens, p);
                            }
                            if (iPrimaryMemory != 0u)
                            {
                                // (the module's: in r14 throughout its code (neither saved nor set here; r14 holds nothing else))
                                iMemoryBase = MemoryBaseRegister;
                                iMemoryBaseIndex = iPrimaryMemory;
                            }
                            choose_pinned_locals(f.codeBegin, f.codeEnd);
                            if (iMemoryBase >= 0 && iPrimaryMemory == 0u)
                                iUsedPinnedGprs.push_back(iMemoryBase);
                            // (a parameter's home is its argument's slot (the caller's outgoing arguments, which are the callee's): it is not
                            // copied)
                            std::int32_t frameLocals = 0;
                            for (std::size_t i = 0u; i < iLocals.size(); ++i)
                                if (!iLocals[i].pinned)
                                    iLocals[i].disp = i < f.parameters->size() ? 16 + 8 * static_cast<std::int32_t>(i) : locals_base() - 8 * (++frameLocals);
                            iSlotsBase = locals_base() - 8 * frameLocals;
                            // prologue (aligned, so that the speed of its code (e.g. its loops' jumps) does not depend on the code before it)
                            a.align(FunctionAlignment);
                            a.bind(function_label(aIndex));
                            // (a function that the optimizing tier inlines (it has no frame of its own: a language's helper) is not optimized
                            // itself: its callers are)
                            if (iCounters != nullptr && !iModule.functions[aIndex].frameless)
                                tier_entry(aIndex);
                            a.push(RBP);
                            a.mov_rr(true, RBP, RSP);
                            for (auto r : iUsedPinnedGprs)
                                a.push(r);
                            a.sub_rsp_imm32(0);
                            auto const frameSizePatch = a.offset() - 4u;
                            a.barrier(); // (it is patched: it does not move)
                            a.alu_rm(7, true, RSP, R15, ContextStackLimit);
                            a.jcc(B, iTrapLabels[TrapStackExhausted]);
                            if (iMemoryBase >= 0 && iPrimaryMemory == 0u)
                                a.mov_ri(iMemoryBase, reinterpret_cast<std::uint64_t>(external_memory(iMemoryBaseIndex).base), true);
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
                            if (iDeoptFrames != nullptr || iLazyFrame != nullptr)
                            {
                                auto& d = iDeoptFrames != nullptr ? (*iDeoptFrames)[aIndex] : *iLazyFrame;
                                d.valid = true;
                                for (auto r : iUsedPinnedGprs)
                                    d.pushed.push_back(static_cast<std::int8_t>(r));
                                d.frameSize = static_cast<std::uint32_t>(total - pushed_bytes());
                                d.memoryBase = static_cast<std::int8_t>(iMemoryBase);
                                d.memoryIndex = iMemoryBaseIndex;
                                for (std::size_t i = 0u; i < iUsedPinnedXmms.size(); ++i)
                                    d.savedXmms.emplace_back(static_cast<std::int8_t>(iUsedPinnedXmms[i]), xmm_save_disp(i));
                                for (auto const& l : iLocals)
                                    d.locals.push_back(compiled_module::deopt_frame::local{ l.type, l.pinned, static_cast<std::int8_t>(l.reg), l.disp });
                                for (std::size_t i = 0u; i < iNativeVolatilePinnedGprs.size(); ++i)
                                    d.callSavedGprs.emplace_back(static_cast<std::int8_t>(iNativeVolatilePinnedGprs[i]), gpr_native_save_disp(i));
                                if (!Windows)
                                    for (std::size_t i = 0u; i < iUsedPinnedXmms.size(); ++i)
                                        d.callSavedXmms.emplace_back(static_cast<std::int8_t>(iUsedPinnedXmms[i]), xmm_native_save_disp(i));
                            }
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
                                // (the peepholes that the head's first compilation used)
                                if (op == 0x20u && position + 7u <= loop.headEnd && update_local(position, p))
                                    continue;
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
                            std::optional<std::size_t> narrowDone;
                            if (aW)
                            {
                                // (operands below 2^32 (most are): a 32 bit division, which is much quicker (signed or unsigned, they are
                                // non-negative))
                                auto const wide = a.new_label();
                                narrowDone = a.new_label();
                                auto const t = alloc(false, avoid | (1u << divisor));
                                a.mov_rr(true, t, RAX);
                                a.alu_rr(1, true, t, divisor);
                                a.shift_ri(5, true, t, 32u);
                                release_reg(false, t);
                                a.jcc(NE, wide);
                                a.alu_rr(6, false, RDX, RDX);
                                a.f7(6, false, divisor);
                                a.jmp(*narrowDone);
                                a.bind(wide);
                            }
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
                            if (narrowDone)
                                a.bind(*narrowDone);
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
                            thread_local std::vector<native_argument> tArguments; // (scratch kept for reuse (one per helper call compiled))
                            thread_local std::vector<entry> tOperands;
                            tArguments.clear();
                            tOperands.assign(aCount, entry{});
                            for (std::size_t i = aCount; i-- > 0u;)
                                tOperands[i] = pop();
                            for (std::size_t i = 0u; i < aCount; ++i)
                            {
                                store_entry(tOperands[i], RSP, ShadowSpace + 8 * static_cast<std::int32_t>(i));
                                release(tOperands[i]);
                                tArguments.push_back(native_argument{ is_float(aOperandType), aOperandType == F64 });
                            }
                            if (aWithContext)
                            {
                                stage_context(aCount);
                                tArguments.push_back(native_argument{ false, false });
                            }
                            native_call(aFunction, tArguments, aWithContext);
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
                        void truncate_f64(bool aWide)
                        {
                            // (i32/i64.trunc_f64_s: cvttsd2si; its "integer indefinite" result (the minimum) is a trap unless the value is the
                            // minimum (NaN: an invalid conversion; else an overflow))
                            materialize_cond();
                            auto v = pop();
                            auto const x = to_xmm(v);
                            auto const r = alloc(false);
                            // (registers are allocated before the branch: an allocation may spill, which both paths must see)
                            auto const t = aWide ? alloc(false) : -1;
                            auto const m = alloc(true);
                            a.sse_rr(0xF2, 0x2C, aWide, r, x);
                            auto const done = a.new_label();
                            if (aWide)
                            {
                                a.mov_ri(t, 0x8000000000000000ull, true);
                                a.alu_rr(7, true, r, t);
                                release_reg(false, t);
                            }
                            else
                                a.alu_ri(7, false, r, std::numeric_limits<std::int32_t>::min());
                            a.jcc(NE, done);
                            a.xorpd(m, m);
                            a.sse_rr(0xF2, 0x2A, aWide, m, r);
                            a.ucomis(true, x, m);
                            a.jcc(P, iTrapLabels[TrapInvalidConversion]);
                            a.jcc(NE, iTrapLabels[TrapIntegerOverflow]);
                            release_reg(true, m);
                            a.bind(done);
                            release_reg(true, x);
                            push_reg(aWide ? I64 : I32, r);
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
                        void call(std::uint32_t aFunction, std::size_t aPosition)
                        {
                            materialize_cond();
                            if (iTierVersion != nullptr && aFunction == optimizer::intrinsics::guard_failed(iModule))
                            {
                                // (an optimized function's inlined call's guard failed: its inline site (an i32) is reported)
                                auto const site = pop();
                                store_entry(site, RSP, ShadowSpace + 16);
                                release(site);
                                spill_registers();
                                stage_context(0u);
                                a.mov_ri(RAX, reinterpret_cast<std::uint64_t>(iTierVersion), true);
                                a.store(true, RSP, ShadowSpace + 8, RAX);
                                native_call(reinterpret_cast<void const*>(&guard_failed), { { false, false }, { false, false }, { false, false } }, false);
                                return;
                            }
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
                            if (callee.call == import_call::Assume)
                            {
                                // (neos::assume: no code (its operand is not used))
                                if (iStack.size() < iControl.back().base + 1u)
                                    throw unsupported{ "operand stack underflow" };
                                release(pop());
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
                            if (iCounters != nullptr && iTier == nullptr && !callee.imported && callee.inlinable)
                            {
                                // (where optimized code that inlined the call can leave for this code (see compiled_module::deopt_point))
                                pending_deopt_point d;
                                d.position = aPosition;
                                d.label = a.new_label();
                                d.point.function = iFunctionIndex;
                                d.point.arguments = static_cast<std::uint32_t>(n);
                                for (auto const& e : iStack)
                                    d.point.stack.push_back(compiled_module::deopt_slot{ e.k == kind::Slot, e.type, e.disp, e.k == kind::Local ? static_cast<std::int32_t>(e.local) : -1 });
                                a.bind(d.label);
                                call_made(d.point);
                                iDeoptPoints.push_back(std::move(d));
                            }
                            iOutgoing = std::max(iOutgoing, base + 8 * static_cast<std::int32_t>(std::max(n, results.size())));
                            // (an import (except neos::object_retain, which cannot call the program): the frame that calls it and the
                            // call, so that its frames can be walked while it runs (see walk))
                            if (callee.imported && callee.call != import_call::ObjectRetain)
                            {
                                a.store(true, R15, ContextExitFrame, RBP);
                                a.store_imm(true, R15, ContextExitSite, static_cast<std::int32_t>(origin(aPosition)));
                            }
                            if (!callee.imported)
                            {
                                // (System V: the callee may call native code, which does not preserve xmm8-xmm15: they are saved here, around
                                // the call (as the pinned registers that native code does not preserve are))
                                save_pinned_xmms();
                                save_pinned_gprs();
                                if (iTier != nullptr || iLazy)
                                {
                                    // (the optimizing tier, or a function compiled when it is first called: through the indirect table (the
                                    // function's optimized code, if it has it))
                                    a.mov_ri(R11, reinterpret_cast<std::uint64_t>(&static_cast<compiled_module::indirect_entry const*>(iIndirect)[aFunction].target), true);
                                    a.op_rm({ 0xFFu }, false, 2, R11, 0);   // call [r11]
                                }
                                else
                                    a.call(iFunctionLabels[aFunction]);
                                // (where it returns to: a frame making this call (see walk))
                                iCallSites.emplace_back(a.new_label(), origin(aPosition));
                                a.bind(iCallSites.back().first);
                                if (!iDeoptPoints.empty() && iDeoptPoints.back().position == aPosition && !iDeoptPoints.back().returnLabel)
                                    iDeoptPoints.back().returnLabel = iCallSites.back().first;
                                restore_pinned_xmms();
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
                            else if (direct(callee))
                                direct_call(aFunction, callee);
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
                        // (a deopt point's call, in a function with a frame of its own: its flag is set when it is made (here: where a deopt
                        // resumes too), see compiled_module::callFlags; rax is free (the call's arguments are in the outgoing area))
                        void call_made(compiled_module::deopt_point& aPoint)
                        {
                            if (iCallFlags == nullptr || iFunction == nullptr || iFunction->frameless)
                                return;
                            iCallFlags->push_back(0u);
                            aPoint.made = &iCallFlags->back();
                            a.mov_ri(RAX, reinterpret_cast<std::uint64_t>(aPoint.made), true);
                            a.store_imm(false, RAX, 0, 1);
                        }
                        // call_indirect (of a function of the program (the table is its functions) of a type; its index is the last 
                        // operand): its index and type checked against the table (see compiled_module::indirect), then called as call calls
                        void call_indirect(std::uint32_t aType, std::size_t aPosition)
                        {
                            materialize_cond();
                            if (aType >= iModule.types.size())
                                throw unsupported{ "invalid type index" };
                            auto const& parameters = *iModule.types[aType].first;
                            auto const& results = *iModule.types[aType].second;
                            if (results.size() > MaxResults)
                                throw unsupported{ "too many results" };
                            std::size_t const n = parameters.size();
                            if (iStack.size() < iControl.back().base + n + 1u)
                                throw unsupported{ "operand stack underflow" };
                            // (the arguments, then the index after them, in the outgoing area)
                            for (std::size_t i = 0u; i <= n; ++i)
                                store_entry(iStack[iStack.size() - n - 1u + i], RSP, 8 * static_cast<std::int32_t>(i));
                            for (std::size_t i = 0u; i <= n; ++i)
                                release(pop());
                            spill_registers();
                            if (iCounters != nullptr && iTier == nullptr)
                            {
                                // (where optimized code that inlined the call (its index a constant) can leave for this code (its arguments
                                // are the call's, then the index))
                                pending_deopt_point d;
                                d.position = aPosition;
                                d.label = a.new_label();
                                d.point.function = iFunctionIndex;
                                d.point.arguments = static_cast<std::uint32_t>(n + 1u);
                                for (auto const& e : iStack)
                                    d.point.stack.push_back(compiled_module::deopt_slot{ e.k == kind::Slot, e.type, e.disp, e.k == kind::Local ? static_cast<std::int32_t>(e.local) : -1 });
                                a.bind(d.label);
                                call_made(d.point);
                                iDeoptPoints.push_back(std::move(d));
                            }
                            iOutgoing = std::max(iOutgoing, 8 * static_cast<std::int32_t>(std::max(n + 1u, results.size())));
                            save_pinned_xmms();     // (System V: see call)
                            save_pinned_gprs();
                            a.load(false, RAX, RSP, 8 * static_cast<std::int32_t>(n));
                            a.alu_ri(7, false, RAX, static_cast<std::int32_t>(iModule.functions.size()));
                            a.jcc(AE, iTrapLabels[TrapIndirectCall]);
                            a.shift_ri(4, true, RAX, 4u);
                            a.mov_ri(R11, reinterpret_cast<std::uint64_t>(iIndirect), true);
                            a.alu_rr(0, true, R11, RAX);
                            a.alu_mi(7, false, R11, 8, static_cast<std::int32_t>(aType));
                            a.jcc(NE, iTrapLabels[TrapIndirectCall]);
                            a.load(true, R11, R11, 0);
                            a.call_r(R11);
                            // (where it returns to: a frame making this call (see walk))
                            iCallSites.emplace_back(a.new_label(), origin(aPosition));
                            a.bind(iCallSites.back().first);
                            if (!iDeoptPoints.empty() && iDeoptPoints.back().position == aPosition && !iDeoptPoints.back().returnLabel)
                                iDeoptPoints.back().returnLabel = iCallSites.back().first;
                            restore_pinned_xmms();
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
                        // (an import that is called directly (see import_call), not through the host: a runtime plugin function that
                        // takes and returns no strings (they are copied by the host))
                        static bool direct(function_description const& aCallee)
                        {
                            if (aCallee.call == import_call::ObjectRetain || aCallee.call == import_call::ObjectRelease)
                                return aCallee.parameters->size() == 1u && aCallee.results->empty();
                            if (aCallee.call != import_call::Plugin || !aCallee.plugin || aCallee.plugin->function == nullptr)
                                return false;
                            // (arguments beyond those in registers are passed on the stack (see native_call))
                            return aCallee.plugin->parameters.size() <= NEOS_RT_MAX_PARAMETERS;
                        }
                        // the call of an import called directly: its arguments are in the outgoing area (its first result is put 
                        // there too)
                        void direct_call(std::uint32_t aFunction, function_description const& aCallee)
                        {
                            bool const table = iModule.objects != nullptr;
                            if (aCallee.call != import_call::Plugin)
                            {
                                if (table)
                                    object_reference(aFunction, aCallee.call == import_call::ObjectRetain);
                                else
                                    direct_helper(aCallee.call == import_call::ObjectRetain ? &object_retain : &object_release, aFunction, false);
                                return;
                            }
                            auto const& f = *aCallee.plugin;
                            auto const converted = [](std::uint8_t aType)
                            {
                                return aType == NEOS_RT_BOOL || aType == NEOS_RT_CHAR || aType == NEOS_RT_OBJECT || aType == NEOS_RT_INTEGER || aType == NEOS_RT_STRING;
                            };
                            auto const handle = [](std::uint8_t aType) { return aType == NEOS_RT_OBJECT || aType == NEOS_RT_INTEGER; };
                            // (the arguments are converted here if the table is available (objects' handles become their addresses))
                            if (table)
                            {
                                for (std::size_t i = 0u; i < f.parameters.size(); ++i)
                                {
                                    auto const disp = ShadowSpace + 8 * static_cast<std::int32_t>(i);
                                    if (handle(f.parameters[i]))
                                        object_argument(aFunction, disp, f.parameters[i] == NEOS_RT_INTEGER);
                                    else if (f.parameters[i] == NEOS_RT_STRING)
                                        string_argument(aFunction, disp);
                                    else if (converted(f.parameters[i]))
                                        byte_value(disp, f.parameters[i] == NEOS_RT_BOOL, true);
                                }
                            }
                            else if (std::any_of(f.parameters.begin(), f.parameters.end(), converted))
                                direct_helper(&plugin_arguments, aFunction, false);
                            thread_local std::vector<native_argument> tArguments; // (scratch kept for reuse (one per import call compiled))
                            tArguments.clear();
                            for (auto const p : f.parameters)
                                tArguments.push_back(native_argument{ p == NEOS_RT_F32 || p == NEOS_RT_F64, p == NEOS_RT_F64 });
                            native_call(f.function, tArguments, false);
                            if (f.result == NEOS_RT_F32 || f.result == NEOS_RT_F64)
                                a.movf_store(f.result == NEOS_RT_F64, RSP, ShadowSpace, 0);
                            else if (f.result != NEOS_RT_VOID)
                                a.store(true, RSP, ShadowSpace, RAX);
                            // (its plugin's error function: called if its error pending flag (if it has one) is set)
                            bool const pending = table && f.error != nullptr && f.errorPending != nullptr;
                            if (f.error != nullptr && !pending)
                                native_call(reinterpret_cast<void const*>(f.error), {}, false); // (rax: its error)
                            if (table && (f.error != nullptr || converted(f.result)))
                            {
                                // (the result is converted here (an object's address becomes a handle for its new reference); an error 
                                // is the host's)
                                auto const resume = a.new_label();
                                auto const result = [&]()
                                {
                                    if (handle(f.result))
                                        object_result(aFunction, f.result == NEOS_RT_INTEGER, resume);
                                    else if (f.result == NEOS_RT_STRING)
                                        direct_helper(&host_string_result, aFunction, false);
                                    else if (converted(f.result))
                                        byte_value(ShadowSpace, f.result == NEOS_RT_BOOL, false);
                                };
                                if (f.error != nullptr)
                                {
                                    auto const error = a.new_label();
                                    if (pending)
                                    {
                                        auto const check = a.new_label();
                                        auto const none = a.new_label();
                                        a.mov_ri(RAX, reinterpret_cast<std::uint64_t>(f.errorPending), true);
                                        a.alu_mi(7, sizeof(long) == 8u, RAX, 0, 0);
                                        a.jcc(NE, check);
                                        a.bind(none);
                                        result();
                                        slow_path(check, none, [&]()
                                        {
                                            native_call(reinterpret_cast<void const*>(f.error), {}, false);
                                            a.test_rr(true, RAX, RAX);
                                            a.jcc(NE, error);
                                        });
                                    }
                                    else
                                    {
                                        a.test_rr(true, RAX, RAX);
                                        a.jcc(NE, error);
                                        result();
                                    }
                                    slow_path(error, resume, [&]() { direct_helper(&plugin_result, aFunction, true); });
                                }
                                else
                                    result();
                                a.bind(resume);
                            }
                            else if (f.error != nullptr || converted(f.result))
                                direct_helper(&plugin_result, aFunction, f.error != nullptr);
                            auto const& results = *aCallee.results;
                            for (std::size_t i = 0u; i < results.size(); ++i)
                                push_loaded(results[i], RSP, ShadowSpace + 8 * static_cast<std::int32_t>(i));
                        }
                        // (a helper of a direct call (see plugin_arguments): its error argument is rax if aError, else null; a 
                        // trap if it fails)
                        void direct_helper(std::uint32_t (*aHelper)(context*, std::uint32_t, value*, char const*), std::uint32_t aFunction, bool aError, 
                            std::int32_t aDisp = ShadowSpace, std::uint32_t aFlag = 0u)
                        {
                            save_pinned_xmms();
                            save_pinned_gprs();
                            if (aError)
                                a.mov_rr(true, NativeIntegerArguments[3], RAX);
                            else
                                a.mov_ri(NativeIntegerArguments[3], aFlag, false);
                            a.mov_rr(true, NativeIntegerArguments[0], R15);
                            a.mov_ri(NativeIntegerArguments[1], aFunction, false);
                            a.lea(NativeIntegerArguments[2], RSP, aDisp);
                            a.mov_ri(RAX, reinterpret_cast<std::uint64_t>(aHelper), true);
                            a.call_r(RAX);
                            restore_pinned_xmms();
                            restore_pinned_gprs();
                            a.test_rr(false, RAX, RAX);
                            a.jcc(NE, iTrapLabels[TrapHostException]);
                        }
                        // --- the object table (see object_table), used here rather than through the host (the registers used are 
                        // scratch registers: free at a call)
                        // (code that is rarely executed (at aLabel; it continues at aResume): cold code, if this is not)
                        template <typename Code>
                        void slow_path(std::size_t aLabel, std::size_t aResume, Code const& aCode)
                        {
                            bool const cold = !a.in_cold();
                            auto const skip = a.new_label();
                            if (cold)
                                a.begin_cold();
                            else
                                a.jmp(skip);
                            a.bind(aLabel);
                            aCode();
                            a.jmp(aResume);
                            if (cold)
                                a.end_cold();
                            else
                                a.bind(skip);
                        }
                        // (rdx: the slot of the handle in eax (rcx: the table); else to aInvalid (an invalid handle))
                        void object_slot_of(std::size_t aInvalid)
                        {
                            a.load(true, RCX, R15, ContextObjects);
                            a.alu_rm(7, false, RAX, RCX, TableSize);
                            a.jcc(AE, aInvalid);
                            a.mov_rr(true, RDX, RAX);
                            a.shift_ri(4, true, RDX, 4);
                            a.alu_rm(0, true, RDX, RCX, TableSlots);
                        }
                        // (the argument at aDisp: an object's handle (aInteger: an integer's value, which is one if its low bit is 
                        // set (shifted)) becomes its address)
                        void object_argument(std::uint32_t aFunction, std::int32_t aDisp, bool aInteger)
                        {
                            auto const resume = a.new_label();
                            auto const store = a.new_label();
                            auto const slow = a.new_label();
                            if (aInteger)
                            {
                                a.load(true, RAX, RSP, aDisp);
                                a.test_ri(RAX, 1);
                                a.jcc(E, resume);
                                a.shift_ri(5, true, RAX, 1);
                                a.mov_rr(false, RAX, RAX);
                            }
                            else
                                a.load(false, RAX, RSP, aDisp);
                            a.test_rr(false, RAX, RAX);
                            a.jcc(E, store);    // (no object: null)
                            object_slot_of(slow);
                            a.alu_mi(7, false, RDX, SlotCount, 0);
                            a.jcc(E, slow);
                            a.load(true, RAX, RDX, SlotObject);
                            if (aInteger)
                                a.alu_ri(1, true, RAX, 1);
                            a.bind(store);
                            a.store(true, RSP, aDisp, RAX);
                            slow_path(slow, resume, [&]() { direct_helper(&host_object_argument, aFunction, false, aDisp, aInteger ? 1u : 0u); });
                            a.bind(resume);
                        }
                        // (a string argument at aDisp (its address): the address of its text, in place (it is NUL terminated: see the 
                        // VM's string_pointer); else the host's (which traps))
                        void string_argument(std::uint32_t aFunction, std::int32_t aDisp)
                        {
                            auto const resume = a.new_label();
                            auto const slow = a.new_label();
                            a.load(false, RAX, RSP, aDisp);                 // (its address)
                            a.load(true, RCX, R15, ContextMemory);
                            a.load(true, R8, RCX, MemorySize);
                            a.load(true, RCX, RCX, MemoryBase);
                            a.mov_rr(true, RDX, RAX);
                            a.alu_ri(0, true, RDX, static_cast<std::int32_t>(bytecode::StringHeaderSize));
                            a.alu_rr(7, true, RDX, R8);
                            a.jcc(A, slow);                                 // (its header is not in memory)
                            a.alu_rr(0, true, RAX, RCX);                    // (its header)
                            a.load(false, R9, RAX, static_cast<std::int32_t>(offsetof(bytecode::string_header, bytes)));
                            a.alu_rr(0, true, RDX, R9);                     // (the offset of its end)
                            a.alu_rr(7, true, RDX, R8);
                            a.jcc(AE, slow);
                            a.alu_rr(0, true, RDX, RCX);
                            a.movzx8_load(R9, RDX, 0);
                            a.test_rr(false, R9, R9);
                            a.jcc(NE, slow);                                // (not NUL terminated)
                            a.alu_ri(0, true, RAX, static_cast<std::int32_t>(bytecode::StringHeaderSize));
                            a.store(true, RSP, aDisp, RAX);
                            slow_path(slow, resume, [&]() { direct_helper(&host_string_argument, aFunction, false, aDisp); });
                            a.bind(resume);
                        }
                        // (a bool (aBool) or char at aDisp, as a C function takes it (aArgument) or as a value (see plugin_arguments
                        // and plugin_result))
                        void byte_value(std::int32_t aDisp, bool aBool, bool aArgument)
                        {
                            a.load(false, RAX, RSP, aDisp);
                            if (aBool)
                            {
                                if (!aArgument)
                                    a.movzx8(RAX, RAX);
                                a.test_rr(false, RAX, RAX);
                                a.setcc(NE, RAX);
                                a.movzx8(RAX, RAX);
                            }
                            else
                            {
                                a.movsx8(RAX, RAX);
                                if (aArgument)
                                    a.movsxd(RAX, RAX);
                            }
                            a.store(true, RSP, aDisp, RAX);
                        }
                        // (the result: an object's address (aInteger: an integer's value, which is one if its low bit is set) becomes 
                        // a handle for its new reference (a new slot), then to aResume)
                        void object_result(std::uint32_t aFunction, bool aInteger, std::size_t aResume)
                        {
                            auto const slow = a.new_label();
                            a.load(true, RDX, RSP, ShadowSpace);
                            if (aInteger)
                            {
                                a.test_ri(RDX, 1);
                                a.jcc(E, aResume);
                                a.alu_ri(4, true, RDX, -2);
                            }
                            a.test_rr(true, RDX, RDX);
                            a.jcc(E, aResume);   // (no object: no handle (0, or 1 for an integer))
                            a.load(true, RCX, R15, ContextObjects);
                            a.load(false, RAX, RCX, TableFree);
                            a.test_rr(false, RAX, RAX);
                            a.jcc(E, slow);     // (no free slot: the host's (which grows the table))
                            a.mov_rr(true, R8, RAX);
                            a.shift_ri(4, true, R8, 4);
                            a.alu_rm(0, true, R8, RCX, TableSlots);
                            a.load(false, R9, R8, SlotNext);
                            a.store(false, RCX, TableFree, R9);
                            a.store(true, R8, SlotObject, RDX);
                            a.store_imm(false, R8, SlotCount, 1);
                            a.store_imm(false, R8, SlotNext, 0);
                            if (aInteger)
                            {
                                a.shift_ri(4, true, RAX, 1);
                                a.alu_ri(1, true, RAX, 1);
                            }
                            a.store(true, RSP, ShadowSpace, RAX);
                            slow_path(slow, aResume, [&]() { direct_helper(&plugin_result, aFunction, false); });
                        }
                        // neos::object_retain and neos::object_release (of the handle in the outgoing area): a slot's count (a 
                        // release of its last reference frees it, and its object's reference is released)
                        void object_reference(std::uint32_t aFunction, bool aRetain)
                        {
                            auto const resume = a.new_label();
                            auto const slow = a.new_label();
                            a.load(false, RAX, RSP, ShadowSpace);
                            a.test_rr(false, RAX, RAX);
                            a.jcc(E, resume);
                            object_slot_of(slow);
                            a.load(false, R8, RDX, SlotCount);
                            a.test_rr(false, R8, R8);
                            a.jcc(E, slow);     // (an invalid handle: the host's (which traps))
                            if (aRetain)
                                a.alu_mi(0, false, RDX, SlotCount, 1);
                            else
                            {
                                auto const last = a.new_label();
                                a.alu_ri(7, false, R8, 1);
                                a.jcc(E, last);
                                a.alu_mi(5, false, RDX, SlotCount, 1);
                                slow_path(last, resume, [&]()
                                {
                                    // (the last reference: the slot is freed, then its object's reference released)
                                    a.load(true, R8, RDX, SlotObject);
                                    a.load(false, R9, RCX, TableFree);
                                    a.store_imm(true, RDX, SlotObject, 0);
                                    a.store_imm(false, RDX, SlotCount, 0);
                                    a.store(false, RDX, SlotNext, R9);
                                    a.store(false, RCX, TableFree, RAX);
                                    // (its object's type's release, called directly (as a runtime plugin's function is))
                                    save_pinned_xmms();
                                    save_pinned_gprs();
                                    a.mov_rr(true, NativeIntegerArguments[0], R8);
                                    a.load(true, RAX, R8, 0);
                                    a.load(true, RAX, RAX, static_cast<std::int32_t>(offsetof(neos_rt_object_type, release)));
                                    a.call_r(RAX);
                                    restore_pinned_xmms();
                                    restore_pinned_gprs();
                                });
                            }
                            slow_path(slow, resume, [&]() { direct_helper(aRetain ? &object_retain : &object_release, aFunction, false); });
                            a.bind(resume);
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
                        void memory_access(std::uint8_t aOp, std::uint64_t aOffset, std::uint32_t aMemory)
                        {
                            // (memory 0: the module's (a 32 bit address); memories 1 to NEOS_RT_EXTERNAL_MEMORIES: the external memories (a 64 bit
                            // address))
                            if (aMemory > NEOS_RT_EXTERNAL_MEMORIES)
                                throw unsupported{ "memory index" };
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
                            if (aMemory == 0u)
                                a.mov_rr(false, g, g); // (zero extend)
                            auto const m = alloc(false, keep | (1u << g));
                            auto const e = alloc(false, keep | (1u << g) | (1u << m));
                            if (aMemory != 0u && external_memory(aMemory).fixed)
                            {
                                // (a fixed external memory: its size and address are constants (its address in a register, if the 
                                // function has it in one); [address, address + disp + width) within it: address + disp + width - 1 (without
                                // carry) below its size)
                                auto const& fixed = external_memory(aMemory);
                                auto const size = static_cast<std::uint64_t>(fixed.size);
                                auto const end = static_cast<std::uint64_t>(disp) + x.width;
                                if (size >= end)
                                {
                                    // (the address at most size - (disp + width))
                                    auto const limit = size - end;
                                    if (limit <= 0x7FFFFFFFull)
                                        a.alu_ri(7, true, g, static_cast<std::int32_t>(limit)); // cmp
                                    else
                                        a.alu_rip(7, true, g, a.constant(limit)); // cmp
                                    a.jcc(A, iTrapLabels[TrapOutOfBounds]);
                                }
                                else
                                    a.jmp(iTrapLabels[TrapOutOfBounds]);
                                if (!is_float(x.type) && iMemoryBase >= 0 && aMemory == iMemoryBaseIndex)
                                {
                                    // ([base + address + disp])
                                    release_reg(false, e);
                                    if (store)
                                    {
                                        a.op_bi({ x.opcode0 }, x.w, v, iMemoryBase, g, disp, x.prefix, x.width == 1u);
                                        release_reg(false, v);
                                        release_reg(false, g);
                                        release_reg(false, m);
                                        return;
                                    }
                                    if (x.opcode1 != 0u)
                                        a.op_bi({ x.opcode0, x.opcode1 }, x.w, m, iMemoryBase, g, disp, x.prefix);
                                    else
                                        a.op_bi({ x.opcode0 }, x.w, m, iMemoryBase, g, disp, x.prefix);
                                    release_reg(false, g);
                                    push_reg(x.type, m);
                                    return;
                                }
                                a.mov_rr(true, m, g);
                                if (iMemoryBase >= 0 && aMemory == iMemoryBaseIndex)
                                    a.alu_rr(0, true, m, iMemoryBase); // add
                                else
                                {
                                    a.mov_ri(e, reinterpret_cast<std::uint64_t>(fixed.base), true);
                                    a.alu_rr(0, true, m, e); // add
                                }
                                release_reg(false, e);
                                release_reg(false, g);
                            }
                            else
                            {
                                if (aMemory == 0u)
                                    a.load(true, m, R15, ContextMemory);
                                else
                                    a.mov_ri(m, reinterpret_cast<std::uint64_t>(&external_memory(aMemory)), true);
                                a.lea(e, g, disp + static_cast<std::int32_t>(x.width));
                                a.alu_rm(7, true, e, m, MemorySize); // cmp
                                a.jcc(A, iTrapLabels[TrapOutOfBounds]);
                                release_reg(false, e);
                                a.load(true, m, m, MemoryBase);
                                a.alu_rr(0, true, m, g); // add
                                release_reg(false, g);
                            }
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
                            // (the tee cache survives only constants, integer equality comparisons, conditional branches and local.gets)
                            if (iTeeCache && !(aOp == 0x41 || aOp == 0x42 || aOp == 0x46 || aOp == 0x47 || aOp == 0x51 || aOp == 0x52 || aOp == 0x0D || aOp == 0x20))
                                iTeeCache.reset();
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
                                    c.position = aPosition;
                                    if (aOp == 0x02 && c.result == I32 && iStack.empty())
                                        c.condition = condition_block(aPosition + 2u);
                                    if (aOp == 0x03)
                                    {
                                        if (!c.result)
                                            find_loop_head(c, aPosition);
                                        if (c.headEnd == 0u)
                                            a.align_executable(LoopAlignment);   // (its head is where a branch back goes)
                                        a.bind(c.label);
                                        if (c.headEnd == 0u)
                                            loop_counter(aPosition, nullptr);
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
                                    a.align_executable(LoopAlignment);   // (a rotated loop's body is where a branch back goes)
                                    a.bind(*c.bodyLabel);
                                    loop_counter(c.position, &c);
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
                                call(static_cast<std::uint32_t>(aImmediate(0)), aPosition);
                                break;
                            case 0x11:
                                call_indirect(static_cast<std::uint32_t>(aImmediate(0)), aPosition);
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
                                    if (iTeeCache && iTeeCache->local == l && iReachable)
                                    {
                                        auto const r = iTeeCache->reg;
                                        iTeeCache.reset();
                                        if (owner(false, r) == Free)
                                        {
                                            owner(false, r) = Locked;
                                            push_reg(iLocals[l].type, r);
                                            break;
                                        }
                                    }
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
                                if (auto const l = static_cast<std::uint32_t>(aImmediate(0)); !iLocals[l].pinned && !is_float(iLocals[l].type) && !iStack.empty() &&
                                    iStack.back().k == kind::Reg)
                                    iTeeCache = tee_cache{ l, iStack.back().reg };
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
                                memory_access(static_cast<std::uint8_t>(aOp), aImmediate(1), static_cast<std::uint32_t>(aImmediate(0) >> 32u));
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
                            case 0xAA: truncate_f64(false); break;
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
                            case 0xB0: truncate_f64(true); break;
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
                        void const* iIndirect = nullptr;    // (call_indirect's table (see compiled_module::indirect))
                        std::size_t iTrapFromContext = 0u;
                        std::size_t iExit = 0u;
                        std::size_t iTrampolineReturn = 0u;
                        std::vector<std::size_t> iFunctionLabels;
                        std::vector<std::pair<std::size_t, std::size_t>> iCallSites; ///< (labels where calls return, and their tokens)
                        bytes iCode;
                        std::int32_t* iCounters = nullptr;              ///< (the optimizing tier's counters (see compiled_module::counters))
                        std::int32_t* iLoopCounters = nullptr;          ///< (... and its loop counters)
                        compiled_module* iTier = nullptr;               ///< (the optimizing tier: the module whose function is compiled)
                        std::vector<std::size_t> const* iOrigins = nullptr;
                        compiled_module::tier_version const* iTierVersion = nullptr;
                        struct pending_deopt_point
                        {
                            std::size_t position = 0u;
                            std::size_t label = 0u;
                            std::optional<std::size_t> returnLabel;
                            compiled_module::deopt_point point;
                        };
                        std::vector<compiled_module::deopt_frame>* iDeoptFrames = nullptr;
                        compiled_module::deopt_frame* iLazyFrame = nullptr;     ///< (see generate_lazy)
                        bool const iSingle = false;
                        std::size_t iSingleLabel = 0u;
                        bool iLazy = false;
                        bool iOsr = false;      ///< (loops that a call can continue in optimized code from: see loop_counter)
                        std::vector<pending_deopt_point> iDeoptPoints;
                        std::deque<std::uint32_t>* iCallFlags = nullptr;   ///< (see compiled_module::callFlags)
                        std::uint32_t iFunctionIndex = 0u;
                        // function state
                        function_description const* iFunction = nullptr;
                        std::vector<entry> iStack;
                        std::vector<control> iControl;
                        std::vector<local_info> iLocals;
                        std::vector<int> iUsedPinnedGprs;               ///< (those that native code preserves: saved by the function)
                        std::vector<int> iNativeVolatilePinnedGprs;     ///< (those that native code does not preserve: saved around calls)
                        std::vector<int> iUsedPinnedXmms;
                        int iMemoryBase = -1;                           ///< (the register that holds a fixed external memory's address (else -1)...)
                        std::uint32_t iMemoryBaseIndex = 0u;            ///< (... that memory's index)
                        std::uint32_t iPrimaryMemory = 0u;              ///< (see compiled_module::primaryMemory)
                        int iGprOwner[16] = {};
                        int iXmmOwner[16] = {};
                        std::vector<std::pair<bool, int>> iTemps;
                        // (a local (not pinned) that local.tee set from a scratch register, while the register still holds its value: 
                        // a local.get of it that follows (with only comparisons, constants and conditional branches between) takes the 
                        // register rather than loading the local, e.g. a call's result that is checked and then used)
                        struct tee_cache
                        {
                            std::uint32_t local;
                            int reg;
                        };
                        std::optional<tee_cache> iTeeCache;
                        std::size_t iMaxDepth = 0u;
                        std::int32_t iSlotsBase = 0;
                        std::int32_t iOutgoing = 0;
                        std::size_t iEpilogue = 0u;
                        bool iReachable = true;
                        std::optional<std::pair<std::size_t, std::size_t>> iLastBranch; ///< (the last br: its position and depth)
                    };

                    // --- the optimizing tier's code generator: a function in SSA form (see jit_ssa.hpp), its values in registers (linear
                    // scan register allocation over its blocks' order (a value is live from its definition to its last use: one interval;
                    // a value that is live across a call is in a register that calls preserve, or in the frame)), its instructions selected
                    // (a comparison fused with the branch that uses it; constants as immediates; a memory access's address in its
                    // instruction (base + index + offset); no check of an access that cannot be out of bounds). Its frames, calls, traps
                    // and results are as the baseline's (see code_generator)

                    class optimizing_generator
                    {
                    private:
                        using value = ssa::value;
                        struct location
                        {
                            enum kind_t : std::uint8_t { None, Register, Slot } kind = None;
                            int reg = 0;
                            std::int32_t disp = 0;  ///< (a slot: [rbp + disp])
                        };
                        struct interval
                        {
                            std::uint32_t start = ~std::uint32_t{};
                            std::uint32_t end = 0u;
                            bool crossesCall = false;
                            bool crossesHotCall = false;    ///< (a call that is not in cold code (see cold_blocks): a callee saved register then)
                            bool xmm = false;
                            value v = ssa::None;
                        };
                        struct pending_condition
                        {
                            value compare = ssa::None;
                            std::uint8_t cc = 0u;
                            bool floatEq = false;   ///< (float ==: E and not P)
                            bool floatNe = false;   ///< (float !=: NE or P)
                        };
#if defined(_WIN32)
                        static constexpr int CallerSavedGprs[] = { R8, R9, R10 };
                        static constexpr int CalleeSavedGprs[] = { RBX, RSI, RDI, R12, R13, R14 };
#else
                        static constexpr int CallerSavedGprs[] = { RSI, RDI, R8, R9, R10 };
                        static constexpr int CalleeSavedGprs[] = { RBX, R12, R13, R14 };
#endif
                        static constexpr int CallerSavedXmms[] = { 2, 3, 4, 5, 6, 7 };
                        static constexpr int CalleeSavedXmms[] = { 8, 9, 10, 11, 12, 13, 14, 15 };
                        static constexpr std::size_t MaxValues = 60000u;
                    public:
                        optimizing_generator(module_description const& aModule, compiled_module& aTier, compiled_module::tier_version const* aVersion, ssa::function& aFunction) :
                            iModule{ aModule }, iTier{ aTier }, iVersion{ aVersion }, f{ aFunction }, iSse41{ has_sse41() }, iBmi2{ has_bmi2() }
                        {
                            for (std::size_t i = 0u; i < TrapCount; ++i)
                                iTrapLabels[i] = a.new_label();
                            iTrapFromContext = a.new_label();
                            iEpilogue = a.new_label();
                        }
                    public:
                        static constexpr std::int32_t LeafFrameLimit = 16384;   ///< (see emit_function)
                        void generate(std::vector<std::pair<std::size_t, std::size_t>>& aCallSites)
                        {
                            if (f.values.size() > MaxValues)
                                throw unsupported{ "too large" };
                            split_critical_edges();
                            choose_memory_base();
                            check_supported();
                            defer_conditions();
                            number();
                            liveness();
                            allocate();
                            homes();
                            saves_at_calls();
                            live_xmms_at_calls();
                            emit_function();
                            for (auto const& [v, label] : iDeoptExits)
                            {
                                a.bind(label);
                                deopt_exit(v);
                            }
                            auto const exit = reinterpret_cast<std::uint64_t>(static_cast<std::uint8_t const*>(iTier.code) + iTier.exitOffset);
                            for (std::uint32_t t = 1u; t < TrapCount; ++t)
                            {
                                a.bind(iTrapLabels[t]);
                                a.store_imm(false, R15, ContextTrap, static_cast<std::int32_t>(t));
                                a.mov_ri(RAX, t, false);
                                a.mov_ri(RCX, exit, true);
                                a.jmp_r(RCX);
                            }
                            a.bind(iTrapFromContext);
                            a.load(false, RAX, R15, ContextTrap);
                            a.mov_ri(RCX, exit, true);
                            a.jmp_r(RCX);
                            a.emit_constants();
                            a.resolve();
                            for (auto const& site : iCallSites)
                                aCallSites.emplace_back(a.label_offset(site.first), site.second);
                            std::sort(aCallSites.begin(), aCallSites.end());
                            iCode = std::move(a.code);
                        }
                        bytes& code() { return iCode; }
                    private:
                        // --- preparation
                        void split_critical_edges()
                        {
                            // (an edge from a block with more than one successor to a block with phis: through a block of its own, which
                            // has the phis' moves)
                            auto const count = static_cast<std::uint32_t>(f.blocks.size());
                            for (std::uint32_t b = 0u; b < count; ++b)
                            {
                                if (!f.blocks[b].reachable || f.blocks[b].successors.size() < 2u)
                                    continue;
                                for (std::size_t s = 0u; s < f.blocks[b].successors.size(); ++s)
                                {
                                    auto const to = f.blocks[b].successors[s];
                                    bool const phis = !f.blocks[to].code.empty() && f.values[f.blocks[to].code.front()].op == ssa::Phi;
                                    if (!phis)
                                        continue;
                                    auto const middle = static_cast<std::uint32_t>(f.blocks.size());
                                    f.blocks.emplace_back();
                                    ssa::instruction jump;
                                    jump.op = ssa::Jump;
                                    jump.block = middle;
                                    f.values.push_back(jump);
                                    f.blocks[middle].code.push_back(static_cast<value>(f.values.size() - 1u));
                                    f.blocks[middle].successors.push_back(to);
                                    f.blocks[middle].predecessors.push_back(b);
                                    f.blocks[middle].reachable = true;
                                    f.blocks[b].successors[s] = middle;
                                    // (the first of the target's predecessors that is this block and has not been replaced)
                                    auto& preds = f.blocks[to].predecessors;
                                    for (auto& p : preds)
                                        if (p == b)
                                        {
                                            p = middle;
                                            break;
                                        }
                                }
                            }
                            ssa::finish(f);
                        }
                        void choose_memory_base()
                        {
                            // (the module's (see compiled_module::primaryMemory): in r14 throughout its code)
                            if (iTier.primaryMemory != 0u)
                            {
                                iMemoryBase = MemoryBaseRegister;
                                iMemoryBaseIndex = iTier.primaryMemory;
                                return;
                            }
                            // (the first fixed external memory that it accesses: its address in r14)
                            for (auto const& i : f.values)
                                if (!i.dead && (i.load() || i.store()))
                                    if (auto const memory = static_cast<std::uint32_t>(i.imm0 >> 32u); memory != 0u && memory <= NEOS_RT_EXTERNAL_MEMORIES && external_memory(memory).fixed)
                                    {
                                        iMemoryBase = MemoryBaseRegister;
                                        iMemoryBaseIndex = memory;
                                        return;
                                    }
                        }
                        // (an instruction that the code generator does not have: the function is compiled by the baseline's)
                        void check_supported()
                        {
                            for (auto b : f.order)
                                for (auto v : f.blocks[b].code)
                                {
                                    auto const& i = f.values[v];
                                    if (i.dead)
                                        continue;
                                    if (i.op == 0x10u)
                                    {
                                        auto const callee = static_cast<std::uint32_t>(i.imm0);
                                        if (callee == optimizer::intrinsics::guard_failed(iModule))
                                        {
                                            if (iVersion == nullptr)
                                                throw unsupported{ "intrinsic" };
                                            continue;
                                        }
                                        auto const& c = iModule.functions[callee];
                                        if (c.results->size() > MaxResults)
                                            throw unsupported{ "results" };
                                        if (c.imported && !plain_plugin(c))
                                        {
                                            std::string why = "import f" + std::to_string(callee) + ": call " + std::to_string(static_cast<int>(c.call)) + (c.checked ? " checked" : "");
                                            if (c.plugin)
                                            {
                                                why += " plugin(";
                                                for (auto t : c.plugin->parameters)
                                                    why += std::to_string(static_cast<int>(t)) + " ";
                                                why += "-> " + std::to_string(static_cast<int>(c.plugin->result)) + ")";
                                            }
                                            throw unsupported{ why };
                                        }
                                    }
                                    else if (i.op == 0x11u)
                                    {
                                        if (iModule.types[static_cast<std::size_t>(i.imm0)].second->size() > MaxResults)
                                            throw unsupported{ "results" };
                                    }
                                    else if (i.op == ssa::Switch && f.blocks[b].successors.size() > 4096u)
                                        throw unsupported{ "switch" };
                                    else if ((i.load() || i.store()) && i.imm1 > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max() - 16))
                                        throw unsupported{ "offset" };
                                    else if ((i.load() || i.store()) && (i.imm0 >> 32u) > NEOS_RT_EXTERNAL_MEMORIES)
                                        throw unsupported{ "memory" };
                                }
                            if (f.results.size() > MaxResults)
                                throw unsupported{ "results" };
                        }
                        // (a runtime plugin function that is called directly, whose arguments and result need no conversion)
                        static bool plain_plugin(function_description const& aCallee)
                        {
                            if (aCallee.call != import_call::Plugin || !aCallee.plugin || aCallee.plugin->function == nullptr || aCallee.checked)
                                return false;
                            auto const plain = [](std::uint8_t aType)
                            {
                                return aType == NEOS_RT_I32 || aType == NEOS_RT_U32 || aType == NEOS_RT_I64 || aType == NEOS_RT_U64 || aType == NEOS_RT_SIZE ||
                                    aType == NEOS_RT_F32 || aType == NEOS_RT_F64;
                            };
                            for (auto p : aCallee.plugin->parameters)
                                if (!plain(p))
                                    return false;
                            return (aCallee.plugin->result == NEOS_RT_VOID || plain(aCallee.plugin->result)) && aCallee.plugin->parameters.size() <= NEOS_RT_MAX_PARAMETERS &&
                                aCallee.parameters->size() == aCallee.plugin->parameters.size() && aCallee.results->size() <= 1u;
                        }
                        // (an instruction that is a call of native code (a helper): its operands' types)
                        static void const* helper_of(std::uint16_t aOp)
                        {
                            switch (aOp)
                            {
                            case 0x69: return reinterpret_cast<void const*>(&i32_popcnt);
                            case 0x7B: return reinterpret_cast<void const*>(&i64_popcnt);
                            case 0x96: return reinterpret_cast<void const*>(&f32_min);
                            case 0x97: return reinterpret_cast<void const*>(&f32_max);
                            case 0xA4: return reinterpret_cast<void const*>(&f64_min);
                            case 0xA5: return reinterpret_cast<void const*>(&f64_max);
                            case 0x8D: return reinterpret_cast<void const*>(&f32_ceil);
                            case 0x8E: return reinterpret_cast<void const*>(&f32_floor);
                            case 0x8F: return reinterpret_cast<void const*>(&f32_trunc);
                            case 0x90: return reinterpret_cast<void const*>(&f32_nearest);
                            case 0x9B: return reinterpret_cast<void const*>(&f64_ceil);
                            case 0x9C: return reinterpret_cast<void const*>(&f64_floor);
                            case 0x9D: return reinterpret_cast<void const*>(&f64_trunc);
                            case 0x9E: return reinterpret_cast<void const*>(&f64_nearest);
                            case 0xA8: return reinterpret_cast<void const*>(&trunc_f32_i32);
                            case 0xA9: return reinterpret_cast<void const*>(&trunc_f32_u32);
                            case 0xAB: return reinterpret_cast<void const*>(&trunc_f64_u32);
                            case 0xAE: return reinterpret_cast<void const*>(&trunc_f32_i64);
                            case 0xAF: return reinterpret_cast<void const*>(&trunc_f32_u64);
                            case 0xB1: return reinterpret_cast<void const*>(&trunc_f64_u64);
                            case 0xB5: return reinterpret_cast<void const*>(&f32_from_u64);
                            case 0xBA: return reinterpret_cast<void const*>(&f64_from_u64);
                            default: return nullptr;
                            }
                        }
                        bool rounding_inline(std::uint16_t aOp) const { return iSse41 && ((aOp >= 0x8Du && aOp <= 0x90u) || (aOp >= 0x9Bu && aOp <= 0x9Eu)); }
                        static bool helper_traps(std::uint16_t aOp) { return (aOp >= 0xA8u && aOp <= 0xABu) || (aOp >= 0xAEu && aOp <= 0xB1u); }
                        bool is_call(ssa::instruction const& aInstruction) const
                        {
                            if (aInstruction.call())
                                return true;
                            if (aInstruction.op >= 0x45u && aInstruction.op <= 0xC4u && helper_of(aInstruction.op) != nullptr && !rounding_inline(aInstruction.op))
                                return true;
                            return false;
                        }
                        bool needs_location(value aValue) const
                        {
                            auto const& i = f.values[aValue];
                            return !i.dead && i.type != ssa::Void && !i.constant();
                        }
                        static bool is_float_type(std::uint8_t aType) { return aType == ssa::F32 || aType == ssa::F64; }
                        static bool is_wide_type(std::uint8_t aType) { return aType == ssa::I64 || aType == ssa::F64; }
                        // --- positions (each instruction's: 2 apart; a block's start and end)
                        void number()
                        {
                            iPosition.assign(f.values.size(), 0u);
                            iBlockStart.assign(f.blocks.size(), 0u);
                            iBlockEnd.assign(f.blocks.size(), 0u);
                            auto const cold = cold_blocks();
                            std::uint32_t p = 2u;
                            for (auto b : f.order)
                            {
                                iBlockStart[b] = p;
                                p += 2u;
                                for (auto v : f.blocks[b].code)
                                {
                                    if (f.values[v].op == ssa::Phi)
                                        iPosition[v] = iBlockStart[b];
                                    else
                                    {
                                        iPosition[v] = p;
                                        p += 2u;
                                    }
                                    if (!f.values[v].dead && is_call(f.values[v]))
                                    {
                                        iCalls.push_back(iPosition[v]);
                                        if (!cold[b])
                                            iHotCalls.push_back(iPosition[v]);
                                    }
                                }
                                iBlockEnd[b] = p;
                                p += 2u;
                            }
                        }
                        // --- liveness: each value's interval (from its definition to its last use; through a loop, if it is live at its
                        // header)
                        void liveness()
                        {
                            iHome.assign(f.values.size(), false);
                            iDeoptAtLocation.assign(f.values.size(), false);
                            auto const n = f.values.size();
                            auto const words = (n + 63u) / 64u;
                            // (each block's live values at its start (once its block is done: see liveInDone), words for each block)
                            auto const liveIn = std::make_unique_for_overwrite<std::uint64_t[]>(f.blocks.size() * words);
                            std::vector<bool> liveInDone(f.blocks.size(), false);
                            iIntervals.assign(n, interval{});
                            for (std::size_t v = 0u; v < n; ++v)
                            {
                                iIntervals[v].v = static_cast<value>(v);
                                iIntervals[v].xmm = is_float_type(f.values[v].type);
                            }
                            // (each loop header's last block (in the order): its loop's end)
                            std::vector<std::uint32_t> orderIndex(f.blocks.size(), 0u);
                            for (std::uint32_t i = 0u; i < f.order.size(); ++i)
                                orderIndex[f.order[i]] = i;
                            std::vector<std::uint32_t> loopEnd(f.blocks.size(), 0u);
                            {
                                std::vector<std::uint32_t> work;
                                std::vector<std::uint32_t> members;
                                std::vector<bool> in(f.blocks.size(), false);
                                for (auto h : f.order)
                                {
                                    if (!f.blocks[h].loopHeader)
                                        continue;
                                    std::uint32_t last = orderIndex[h];
                                    work.clear();
                                    members.clear();
                                    for (auto p : f.blocks[h].predecessors)
                                        if (orderIndex[p] >= orderIndex[h])
                                            work.push_back(p);
                                    in[h] = true;
                                    members.push_back(h);
                                    while (!work.empty())
                                    {
                                        auto const x = work.back();
                                        work.pop_back();
                                        if (in[x] || !f.blocks[x].reachable)
                                            continue;
                                        in[x] = true;
                                        members.push_back(x);
                                        last = std::max(last, orderIndex[x]);
                                        for (auto p : f.blocks[x].predecessors)
                                            work.push_back(p);
                                    }
                                    for (auto x : members)
                                        in[x] = false;
                                    loopEnd[h] = iBlockEnd[f.order[last]];
                                }
                            }
                            std::vector<std::uint64_t> live(words);
                            auto const set = [&](std::vector<std::uint64_t>& aSet, value aValue) { aSet[aValue / 64u] |= std::uint64_t{ 1u } << (aValue % 64u); };
                            auto const reset = [&](std::vector<std::uint64_t>& aSet, value aValue) { aSet[aValue / 64u] &= ~(std::uint64_t{ 1u } << (aValue % 64u)); };
                            auto const extend = [&](value aValue, std::uint32_t aFrom, std::uint32_t aTo)
                            {
                                auto& i = iIntervals[aValue];
                                i.start = std::min(i.start, aFrom);
                                i.end = std::max(i.end, aTo);
                            };
                            for (auto o = f.order.size(); o-- > 0u;)
                            {
                                auto const b = f.order[o];
                                auto const& block = f.blocks[b];
                                std::fill(live.begin(), live.end(), 0u);
                                for (auto s : block.successors)
                                {
                                    if (liveInDone[s])
                                        for (std::size_t w = 0u; w < words; ++w)
                                            live[w] |= liveIn[s * words + w];
                                    // (the phis' operands from this block)
                                    auto const& to = f.blocks[s];
                                    auto const index = static_cast<std::size_t>(std::find(to.predecessors.begin(), to.predecessors.end(), b) - to.predecessors.begin());
                                    for (auto v : to.code)
                                    {
                                        auto const& i = f.values[v];
                                        if (i.op != ssa::Phi)
                                            break;
                                        if (!i.dead && index < i.operands.size() && needs_location(i.operands[index]))
                                            set(live, i.operands[index]);
                                    }
                                }
                                for (std::size_t w = 0u; w < words; ++w)
                                    for (auto bits = live[w]; bits != 0u; bits &= bits - 1u)
                                        extend(static_cast<value>(w * 64u + static_cast<std::size_t>(std::countr_zero(bits))), iBlockStart[b], iBlockEnd[b]);
                                for (auto c = block.code.size(); c-- > 0u;)
                                {
                                    auto const v = block.code[c];
                                    auto const& i = f.values[v];
                                    if (i.dead || i.op == ssa::Phi)
                                        continue;
                                    if (needs_location(v))
                                    {
                                        // (its definition: its interval's start (in SSA form a value is used only where its definition 
                                        // dominates, after it in the order: the block's start that its uses here extended it to is not))
                                        iIntervals[v].start = iPosition[v];
                                        iIntervals[v].end = std::max(iIntervals[v].end, iPosition[v] + 1u);
                                        reset(live, v);
                                    }
                                    if (i.op == ssa::Deopt)
                                    {
                                        // (a deopt whose block has no other code (that would need a location): its values are read where 
                                        // they are at its predecessors' ends (they are live there; not in its block, which may be laid out 
                                        // far away: the path to it runs no other code))
                                        bool atLocation = !block.predecessors.empty() && !block.loopHeader;
                                        for (auto w : block.code)
                                            if (w != v && !f.values[w].dead && needs_location(w))
                                                atLocation = false;
                                        if (atLocation)
                                        {
                                            iDeoptAtLocation[v] = true;
                                            for (auto op : i.operands)
                                                if (needs_location(op))
                                                    set(live, op);
                                            continue;
                                        }
                                        // (else its values: read from their homes (each is stored there when it is made: see home): they
                                        // are not live here)
                                        for (auto op : i.operands)
                                            if (needs_location(op))
                                                iHome[op] = true;
                                        continue;
                                    }
                                    for (auto op : i.operands)
                                        if (needs_location(op))
                                        {
                                            extend(op, iBlockStart[b], iPosition[v]);
                                            set(live, op);
                                        }
                                }
                                for (auto v : block.code)
                                {
                                    auto const& i = f.values[v];
                                    if (i.op != ssa::Phi)
                                        break;
                                    if (i.dead)
                                        continue;
                                    iIntervals[v].start = std::min(iIntervals[v].start, iBlockStart[b]);
                                    iIntervals[v].end = std::max(iIntervals[v].end, iBlockStart[b] + 1u);
                                    // (its register is written at the ends of its block's predecessors (the moves to it))
                                    for (auto p : block.predecessors)
                                        extend(v, iBlockEnd[p] - 1u, iBlockEnd[p]);
                                    reset(live, v);
                                }
                                if (block.loopHeader)
                                    for (std::size_t w = 0u; w < words; ++w)
                                        for (auto bits = live[w]; bits != 0u; bits &= bits - 1u)
                                            extend(static_cast<value>(w * 64u + static_cast<std::size_t>(std::countr_zero(bits))), iBlockStart[b], loopEnd[b]);
                                std::copy(live.begin(), live.end(), liveIn.get() + b * words);
                                liveInDone[b] = true;
                            }
                            // (the calls that each interval spans)
                            std::sort(iCalls.begin(), iCalls.end());
                            std::sort(iHotCalls.begin(), iHotCalls.end());
                            for (auto& i : iIntervals)
                            {
                                if (i.start > i.end)
                                    continue;
                                auto const c = std::upper_bound(iCalls.begin(), iCalls.end(), i.start);
                                i.crossesCall = c != iCalls.end() && *c < i.end;
                                auto const h = std::upper_bound(iHotCalls.begin(), iHotCalls.end(), i.start);
                                i.crossesHotCall = h != iHotCalls.end() && *h < i.end;
                            }
                        }
                        // --- register allocation
                        void allocate()
                        {
                            iLocations.assign(f.values.size(), location{});
                            std::vector<interval*> order;
                            for (auto& i : iIntervals)
                                if (i.start <= i.end && needs_location(i.v))
                                    order.push_back(&i);
                            std::sort(order.begin(), order.end(), [](interval const* x, interval const* y) { return x->start < y->start || (x->start == y->start && x->v < y->v); });
                            // (each value's spill cost: its definition and uses, each weighed by its block's loop depth (8 times for each))
                            std::vector<double> weight(f.values.size(), 0.0);
                            {
                                auto const scale = [&](std::uint32_t aBlock) { return static_cast<double>(1u << (3u * std::min<std::uint32_t>(f.blocks[aBlock].loopDepth, 4u))); };
                                for (auto b : f.order)
                                {
                                    auto const k = scale(b);
                                    for (auto v : f.blocks[b].code)
                                    {
                                        auto const& x = f.values[v];
                                        if (x.dead)
                                            continue;
                                        weight[v] += k;
                                        if (x.op == ssa::Phi)
                                        {
                                            for (std::size_t o = 0u; o < x.operands.size() && o < f.blocks[b].predecessors.size(); ++o)
                                                if (x.operands[o] != ssa::None)
                                                    weight[x.operands[o]] += scale(f.blocks[b].predecessors[o]);
                                            continue;
                                        }
                                        if (x.op == ssa::Deopt)
                                            continue;
                                        for (auto o : x.operands)
                                            if (o != ssa::None)
                                                weight[o] += k;
                                    }
                                }
                            }
                            // (each value that only a phi uses: the phi (see the hints below))
                            std::vector<value> phiUser(f.values.size(), ssa::None);
                            for (auto b : f.order)
                                for (auto v : f.blocks[b].code)
                                    if (auto const& x = f.values[v]; x.op == ssa::Phi && !x.dead)
                                        for (auto o : x.operands)
                                            if (o != ssa::None && o < phiUser.size() && f.uses[o] == 1u)
                                                phiUser[o] = v;
                            std::vector<interval*> active;
                            std::vector<bool> gprFree(16, false), xmmFree(16, false);
                            for (auto r : CallerSavedGprs)
                                gprFree[r] = true;
                            for (auto r : CalleeSavedGprs)
                                if (r != iMemoryBase)
                                    gprFree[r] = true;
                            for (auto r : CallerSavedXmms)
                                xmmFree[r] = true;
                            for (auto r : CalleeSavedXmms)
                                xmmFree[r] = true;
                            auto const calleeSaved = [&](bool aXmm, int aReg)
                            {
                                if (aXmm)
                                    return aReg >= 8;
                                for (auto r : CalleeSavedGprs)
                                    if (r == aReg)
                                        return true;
                                return false;
                            };
                            for (auto* i : order)
                            {
                                // (intervals that have ended: their registers are free)
                                for (auto a = active.begin(); a != active.end();)
                                    if ((*a)->end <= i->start)
                                    {
                                        auto const& l = iLocations[(*a)->v];
                                        ((*a)->xmm ? xmmFree : gprFree)[l.reg] = true;
                                        a = active.erase(a);
                                    }
                                    else
                                        ++a;
                                // (a parameter that is not used (deopts read it from its argument's slot): its slot)
                                if (f.values[i->v].op == ssa::Param && i->end <= i->start + 1u)
                                {
                                    spill(i->v);
                                    continue;
                                }
                                auto& free = i->xmm ? xmmFree : gprFree;
                                int reg = -1;
                                auto const pick = [&](int const* aRegs, std::size_t aCount)
                                {
                                    for (std::size_t r = 0u; r < aCount && reg < 0; ++r)
                                        if (free[aRegs[r]])
                                            reg = aRegs[r];
                                };
                                // (a phi: the register of one of its operands (no move on that edge), if it is free; a value that only a phi
                                // uses (allocated: e.g. a loop's, for its back edge): the phi's (of the class that it would choose))
                                {
                                    auto const hinted = [&](value aOther)
                                    {
                                        if (reg >= 0 || aOther == ssa::None || aOther >= iLocations.size())
                                            return;
                                        auto const& l = iLocations[aOther];
                                        if (l.kind == location::Register && is_float_type(f.values[aOther].type) == i->xmm && free[l.reg] &&
                                            (!i->crossesHotCall || calleeSaved(i->xmm, l.reg)))
                                            reg = l.reg;
                                    };
                                    auto const& x = f.values[i->v];
                                    if (x.op == ssa::Phi)
                                        for (auto o : x.operands)
                                            hinted(o);
                                    hinted(phiUser[i->v]);
                                }
                                // (an interval across calls: a callee saved register, else a caller saved one (saved around the calls (see
                                // saves_at_calls)), else the frame)
                                if (i->xmm)
                                {
                                    if (!i->crossesHotCall)
                                        pick(CallerSavedXmms, std::size(CallerSavedXmms));
                                    pick(CalleeSavedXmms, std::size(CalleeSavedXmms));
                                    pick(CallerSavedXmms, std::size(CallerSavedXmms));
                                }
                                else
                                {
                                    if (!i->crossesHotCall)
                                        pick(CallerSavedGprs, std::size(CallerSavedGprs));
                                    pick(CalleeSavedGprs, std::size(CalleeSavedGprs));
                                    pick(CallerSavedGprs, std::size(CallerSavedGprs));
                                }
                                if (reg < 0)
                                {
                                    // (none free: the interval (this or an active one) whose spill costs least (its uses' weight, for the 
                                    // length that it is in the frame), else that ends last, is in the frame)
                                    interval* victim = nullptr;
                                    auto const cost = [&](interval const* x) { return weight[x->v] / static_cast<double>(std::max<std::uint32_t>(x->end - i->start, 1u)); };
                                    for (auto* x : active)
                                        if (x->xmm == i->xmm && (victim == nullptr || cost(x) < cost(victim) || (cost(x) == cost(victim) && x->end > victim->end)))
                                            victim = x;
                                    if (victim != nullptr && (cost(victim) < cost(i) || (cost(victim) == cost(i) && victim->end > i->end)))
                                    {
                                        reg = iLocations[victim->v].reg;
                                        spill(victim->v);
                                        active.erase(std::find(active.begin(), active.end(), victim));
                                    }
                                    else
                                    {
                                        spill(i->v);
                                        continue;
                                    }
                                }
                                else
                                    free[reg] = false;
                                iLocations[i->v] = location{ location::Register, reg, 0 };
                                if (calleeSaved(i->xmm, reg))
                                {
                                    if (i->xmm)
                                        iUsedCalleeXmms[reg] = true;
                                    else
                                        iUsedCalleeGprs[reg] = true;
                                }
                                active.push_back(i);
                            }
                        }
                        // (the values that deopts read: in their homes, slots that they are stored in when they are made (a value in the
                        // frame: its slot; a parameter in a register: its argument's slot), so that they need not be live until the deopts)
                        void homes()
                        {
                            for (std::size_t v = 0u; v < f.values.size(); ++v)
                            {
                                if (!iHome[v])
                                    continue;
                                auto const& l = iLocations[v];
                                if (l.kind == location::Slot)
                                    continue;
                                if (f.values[v].op == ssa::Param)
                                    continue;
                                iHomeSlots.emplace(static_cast<value>(v), 0);
                            }
                        }
                        std::int32_t home_of(value aValue) const
                        {
                            auto const& i = f.values[aValue];
                            if (auto const h = iHomeSlots.find(aValue); h != iHomeSlots.end())
                                return h->second;
                            if (where(aValue).kind == location::Slot)
                                return where(aValue).disp;
                            return 16 + 8 * static_cast<std::int32_t>(i.imm0);   // (a parameter: its argument's slot)
                        }
                        void store_home(value aValue)
                        {
                            auto const h = iHomeSlots.find(aValue);
                            if (h == iHomeSlots.end() || where(aValue).kind != location::Register)
                                return;
                            if (is_float_type(f.values[aValue].type))
                                a.movf_store(true, RBP, h->second, where(aValue).reg);
                            else
                                a.store(true, RBP, h->second, where(aValue).reg);
                        }
                        // (the values in caller saved registers across each call: saved before it (in slots of their own) and restored after it)
                        void saves_at_calls()
                        {
                            auto const callerSaved = [](bool aXmm, int aReg)
                            {
                                if (aXmm)
                                    return aReg < 8;
                                for (auto r : CallerSavedGprs)
                                    if (r == aReg)
                                        return true;
                                return false;
                            };
                            for (auto const& i : iIntervals)
                                if (i.crossesCall && needs_location(i.v) && where(i.v).kind == location::Register && callerSaved(i.xmm, where(i.v).reg))
                                    for (auto c = std::upper_bound(iCalls.begin(), iCalls.end(), i.start); c != iCalls.end() && *c < i.end; ++c)
                                    {
                                        // (not a value defined by the call, or after it (an interval starts at its block's start))
                                        if (iPosition[i.v] >= *c)
                                            continue;
                                        iSavesAtCalls[*c].push_back(i.v);
                                        iCallSaveSlots.emplace(i.v, 0);
                                    }
                        }
                        void save_at_call(value v)
                        {
                            if (auto const s = iSavesAtCalls.find(iPosition[v]); s != iSavesAtCalls.end())
                                for (auto x : s->second)
                                {
                                    auto const disp = iCallSaveSlots.at(x);
                                    if (is_float_type(f.values[x].type))
                                        a.movf_store(true, RBP, disp, where(x).reg);
                                    else
                                        a.store(true, RBP, disp, where(x).reg);
                                }
                        }
                        void restore_at_call(value v)
                        {
                            if (auto const s = iSavesAtCalls.find(iPosition[v]); s != iSavesAtCalls.end())
                                for (auto x : s->second)
                                {
                                    auto const disp = iCallSaveSlots.at(x);
                                    if (is_float_type(f.values[x].type))
                                        a.movf_load(true, where(x).reg, RBP, disp);
                                    else
                                        a.load(true, where(x).reg, RBP, disp);
                                }
                        }
                        // (System V: the xmms (8-15) that hold values across each call (native code does not preserve them))
                        void live_xmms_at_calls()
                        {
                            if (Windows)
                                return;
                            for (auto const& i : iIntervals)
                                if (i.xmm && i.crossesCall && needs_location(i.v) && where(i.v).kind == location::Register && where(i.v).reg >= 8)
                                    for (auto c = std::upper_bound(iCalls.begin(), iCalls.end(), i.start); c != iCalls.end() && *c < i.end; ++c)
                                        iLiveXmmsAtCalls[*c] |= 1u << where(i.v).reg;
                        }
                        void spill(value aValue)
                        {
                            auto const& i = f.values[aValue];
                            if (i.op == ssa::Param)
                            {
                                // (a parameter's slot: its argument's)
                                iLocations[aValue] = location{ location::Slot, 0, 16 + 8 * static_cast<std::int32_t>(i.imm0) };
                                return;
                            }
                            iLocations[aValue] = location{ location::Slot, 0, 0 };
                            iSpills.push_back(aValue);
                        }
                        // --- the frame: [rbp - 8 * (pushed)]: the callee saved registers that it uses; then the xmms that it saves (those
                        // that native calls do not preserve (System V), and its callee saved ones); then the values in the frame; then the
                        // outgoing area (rsp)
                        void layout()
                        {
                            iPushed.clear();
                            for (auto r : CalleeSavedGprs)
                                if ((iUsedCalleeGprs[r] || r == iMemoryBase) && !(r == iMemoryBase && iTier.primaryMemory != 0u))
                                    iPushed.push_back(r);
                            std::int32_t offset = 8 * static_cast<std::int32_t>(iPushed.size());
                            for (int x = 8; x < 16; ++x)
                                if (iUsedCalleeXmms[x])
                                {
                                    offset += 8;
                                    iXmmSave[x] = -offset;
                                }
                            if (!Windows)
                                for (int x = 8; x < 16; ++x)
                                {
                                    offset += 8;
                                    iXmmNativeSave[x] = -offset;
                                }
                            for (auto v : iSpills)
                            {
                                offset += 8;
                                iLocations[v].disp = -offset;
                            }
                            for (auto& [v, disp] : iCallSaveSlots)
                            {
                                offset += 8;
                                disp = -offset;
                            }
                            for (auto& [v, disp] : iHomeSlots)
                            {
                                offset += 8;
                                disp = -offset;
                            }
                            iFrameBelowPushed = offset - 8 * static_cast<std::int32_t>(iPushed.size());
                        }
                        // --- values
                        bool constant(value aValue) const { return f.values[aValue].constant(); }
                        std::uint64_t bits(value aValue) const
                        {
                            auto const& i = f.values[aValue];
                            return (i.type == ssa::I32 || i.type == ssa::F32) ? (i.imm0 & 0xFFFFFFFFull) : i.imm0;
                        }
                        location const& where(value aValue) const { return iLocations[aValue]; }
                        bool in_register(value aValue) const { return !constant(aValue) && where(aValue).kind == location::Register; }
                        // (an integer value in a register: its, or aScratch (loaded))
                        int gpr(value aValue, int aScratch)
                        {
                            if (constant(aValue))
                            {
                                a.mov_ri(aScratch, bits(aValue), is_wide_type(f.values[aValue].type));
                                return aScratch;
                            }
                            auto const& l = where(aValue);
                            if (l.kind == location::Register)
                                return l.reg;
                            if (just_stored(aValue, aScratch))
                                return aScratch;
                            if (just_stored(aValue, iStoredFrom.reg))
                            {
                                // (another register has it: a move (as the load: an i32's zero extended), not a load of what was just stored)
                                a.mov_rr(is_wide_type(f.values[aValue].type), aScratch, iStoredFrom.reg);
                                return aScratch;
                            }
                            a.load(is_wide_type(f.values[aValue].type), aScratch, RBP, l.disp);
                            return aScratch;
                        }
                        void gpr_into(value aValue, int aReg)
                        {
                            auto const r = gpr(aValue, aReg);
                            if (r != aReg)
                                a.mov_rr(true, aReg, r);
                        }
                        int xmm(value aValue, int aScratch)
                        {
                            auto const type = f.values[aValue].type;
                            if (constant(aValue))
                            {
                                auto const b = bits(aValue);
                                if (b == 0u)
                                    a.xorpd(aScratch, aScratch);
                                else
                                    a.sse_rip(type == ssa::F64 ? 0xF2 : 0xF3, 0x10, aScratch, a.constant(b));
                                return aScratch;
                            }
                            auto const& l = where(aValue);
                            if (l.kind == location::Register)
                                return l.reg;
                            if (just_stored(aValue, aScratch))
                                return aScratch;
                            if (just_stored(aValue, iStoredFrom.reg))
                            {
                                a.movapd(aScratch, iStoredFrom.reg);
                                return aScratch;
                            }
                            a.movf_load(type == ssa::F64, aScratch, RBP, l.disp);
                            return aScratch;
                        }
                        void xmm_into(value aValue, int aReg)
                        {
                            auto const x = xmm(aValue, aReg);
                            if (x != aReg)
                                a.movapd(aReg, x);
                        }
                        // (the register that a value is computed into: its, or aScratch (then stored: see defined))
                        int target(value aValue, int aScratch) const
                        {
                            auto const& l = where(aValue);
                            return l.kind == location::Register ? l.reg : aScratch;
                        }
                        void defined(value aValue, int aReg)
                        {
                            auto const& l = where(aValue);
                            auto const type = f.values[aValue].type;
                            if (l.kind == location::Register)
                            {
                                if (l.reg != aReg)
                                {
                                    if (is_float_type(type))
                                        a.movapd(l.reg, aReg);
                                    else
                                        a.mov_rr(true, l.reg, aReg);
                                }
                                return;
                            }
                            if (l.kind == location::Slot)
                            {
                                if (is_float_type(type))
                                    a.movf_store(type == ssa::F64, RBP, l.disp, aReg);
                                else
                                    a.store(true, RBP, l.disp, aReg);
                                // (the register has its value until code is emitted (see gpr, xmm))
                                iStoredFrom = stored_from{ aValue, aReg, a.events() };
                            }
                        }
                        // (a value just stored to its slot from a register (nothing emitted since): the register has it)
                        struct stored_from
                        {
                            value v = ssa::None;
                            int reg = 0;
                            std::uint64_t events = 0u;
                        };
                        stored_from iStoredFrom;
                        bool just_stored(value aValue, int aReg) const { return iStoredFrom.v == aValue && iStoredFrom.reg == aReg && iStoredFrom.events == a.events(); }
                        // (a value as an operand of an integer instruction: an immediate (aImmediate: if it fits), a register or a slot)
                        struct operand
                        {
                            enum kind_t { Immediate, Register, Memory } kind;
                            std::int32_t imm = 0;
                            int reg = 0;
                            std::int32_t disp = 0;
                        };
                        operand operand_of(value aValue, int aScratch, bool aImmediate = true)
                        {
                            if (constant(aValue))
                            {
                                auto const b = bits(aValue);
                                bool const wide = is_wide_type(f.values[aValue].type);
                                if (aImmediate && (wide ? fits_i32(static_cast<std::int64_t>(b)) : true))
                                    return operand{ operand::Immediate, static_cast<std::int32_t>(static_cast<std::uint32_t>(b)) };
                                a.mov_ri(aScratch, b, wide);
                                return operand{ operand::Register, 0, aScratch };
                            }
                            auto const& l = where(aValue);
                            if (l.kind == location::Register)
                                return operand{ operand::Register, 0, l.reg };
                            return operand{ operand::Memory, 0, 0, l.disp };
                        }
                        void alu(int aOp, bool aW, int aDst, operand const& aOperand)
                        {
                            switch (aOperand.kind)
                            {
                            case operand::Immediate: a.alu_ri(aOp, aW, aDst, aOperand.imm); break;
                            case operand::Register: a.alu_rr(aOp, aW, aDst, aOperand.reg); break;
                            default: a.alu_rm(aOp, aW, aDst, RBP, aOperand.disp); break;
                            }
                        }
                        // --- code
                        void emit_function()
                        {
                            iDeferred.resize(f.values.size(), false);
                            layout();
                            iBlockLabels.assign(f.blocks.size(), 0u);
                            for (auto b : f.order)
                                iBlockLabels[b] = a.new_label();
                            // prologue
                            a.push(RBP);
                            a.mov_rr(true, RBP, RSP);
                            for (auto r : iPushed)
                                a.push(r);
                            a.sub_rsp_imm32(0);
                            auto const frameSizePatch = a.offset() - 4u;
                            a.barrier();
                            // (a function that makes no calls (a leaf, with a frame within the stack's headroom (see stack_limit)): the stack
                            // is not checked (its callers' frames were))
                            // (one whose calls are all in cold code (e.g. a cache's miss): checked before each of them instead)
                            iCallStackChecks = !iCalls.empty() && iHotCalls.empty() && iFrameBelowPushed <= LeafFrameLimit;
                            if ((!iCalls.empty() || iFrameBelowPushed > LeafFrameLimit) && !iCallStackChecks)
                            {
                                a.alu_rm(7, true, RSP, R15, ContextStackLimit);
                                a.jcc(B, iTrapLabels[TrapStackExhausted]);
                            }
                            if (iVersion != nullptr && std::any_of(iVersion->deopts.begin(), iVersion->deopts.end(), [](auto const& d) { return !d.guard; }))
                            {
                                // (its calls are counted: a deopt that it seldom takes (as a share of its calls) does not make it optimized
                                // again (see deopt_taken))
                                auto const counter = reinterpret_cast<std::uint64_t>(&iVersion->calls);
                                if (counter < 0x80000000u)
                                {
                                    // (add qword [counter], 1: an absolute address (one instruction))
                                    a.emit8(0x48u);
                                    a.emit8(0x83u);
                                    a.emit8(0x04u);
                                    a.emit8(0x25u);
                                    a.emit32(static_cast<std::uint32_t>(counter));
                                    a.emit8(0x01u);
                                }
                                else
                                {
                                    a.mov_ri(R11, counter, true);
                                    a.alu_mi(0, true, R11, 0, 1);
                                }
                            }
                            if (iMemoryBase >= 0 && iTier.primaryMemory == 0u)
                                a.mov_ri(iMemoryBase, reinterpret_cast<std::uint64_t>(external_memory(iMemoryBaseIndex).base), true);
                            for (int x = 8; x < 16; ++x)
                                if (iUsedCalleeXmms[x])
                                    a.movf_store(true, RBP, iXmmSave[x], x);
                            auto emission = emission_order();
                            forward_empty_jumps(emission);
                            for (std::size_t o = 0u; o < emission.size(); ++o)
                            {
                                auto const b = emission[o];
                                iNext = o + 1u < emission.size() ? emission[o + 1u] : ~std::uint32_t{};
                                if (f.blocks[b].loopHeader)
                                    a.align_executable(LoopAlignment);
                                a.bind(iBlockLabels[b]);
                                auto const& code = f.blocks[b].code;
                                if (!iHomeSlots.empty())
                                    for (auto v : code)
                                    {
                                        if (f.values[v].op != ssa::Phi)
                                            break;
                                        if (!f.values[v].dead)
                                            store_home(v);
                                    }
                                for (std::size_t c = 0u; c < code.size(); ++c)
                                {
                                    auto const v = code[c];
                                    if (f.values[v].dead || f.values[v].op == ssa::Phi)
                                        continue;
                                    iCurrentBlock = b;
                                    // (the next instruction that has code: not dead, nor a constant (they are where they are used))
                                    iNextInstruction = ssa::None;
                                    for (auto n = c + 1u; n < code.size(); ++n)
                                        if (!f.values[code[n]].dead && !f.values[code[n]].constant())
                                        {
                                            iNextInstruction = code[n];
                                            break;
                                        }
                                    instruction(v);
                                    if (!iHomeSlots.empty() && !iDeferred[v])
                                        store_home(v);
                                }
                            }
                            // epilogue
                            a.bind(iEpilogue);
                            for (int x = 8; x < 16; ++x)
                                if (iUsedCalleeXmms[x])
                                    a.movf_load(true, x, RBP, iXmmSave[x]);
                            a.lea(RSP, RBP, -8 * static_cast<std::int32_t>(iPushed.size()));
                            for (auto r = iPushed.rbegin(); r != iPushed.rend(); ++r)
                                a.pop(*r);
                            a.pop(RBP);
                            a.ret();
                            a.append_cold();
                            std::int32_t const pushed = 8 * static_cast<std::int32_t>(iPushed.size());
                            std::int32_t const raw = iFrameBelowPushed + std::max(iOutgoing, ShadowSpace);
                            std::int32_t const total = (pushed + raw + 15) & ~15;
                            a.patch32(frameSizePatch, static_cast<std::uint32_t>(total - pushed));
                        }
                        // (the order the blocks' code is in: the blocks that are likely to run (in the order), then those that are not (a deopt's,
                        // a trap's, one that calls an import (a language's runtime: e.g. a cache's miss), and those that only they lead to), so
                        // that the code that runs is together (fewer of the instruction cache's lines); the registers' allocation is by the
                        // order (the code of a path is the same wherever its blocks are))
                        std::vector<std::uint32_t> emission_order() const
                        {
                            auto const cold = cold_blocks();
                            std::vector<std::uint32_t> result;
                            result.reserve(f.order.size());
                            for (auto b : f.order)
                                if (!cold[b])
                                    result.push_back(b);
                            for (auto b : f.order)
                                if (cold[b])
                                    result.push_back(b);
                            return result;
                        }
                        // (a call (at aOrigin: see ssa::instruction::origin) that the function's own code (the baseline's, whose frames it has)
                        // has never made (see compiled_module::callFlags): likely not to be made)
                        bool never_made(std::size_t aOrigin) const
                        {
                            auto position = aOrigin;
                            if ((aOrigin & optimizer::InlineChainToken) != 0u && (aOrigin >> 32u) == 0u)
                            {
                                auto const index = aOrigin & ~optimizer::InlineChainToken;
                                std::lock_guard<std::mutex> lock{ iTier.chainsMutex };
                                if (index >= iTier.inlineChains.size() || iTier.inlineChains[index].empty())
                                    return false;
                                position = iTier.inlineChains[index].front();
                            }
                            auto const* const point = deopt_point_at(iTier, position);
                            return point != nullptr && point->made != nullptr && *point->made == 0u;
                        }
                        // (the blocks that are not likely to run (see emission_order))
                        std::vector<bool> cold_blocks() const
                        {
                            std::vector<bool> cold(f.blocks.size(), false);
                            for (auto b : f.order)
                            {
                                if (b == f.order.front() || f.blocks[b].loopHeader)
                                    continue;
                                for (auto v : f.blocks[b].code)
                                {
                                    auto const& i = f.values[v];
                                    if (i.dead)
                                        continue;
                                    if (i.op == ssa::Deopt || i.op == ssa::Trap ||
                                        (i.op == 0x10u && i.imm0 < iModule.functions.size() && iModule.functions[static_cast<std::size_t>(i.imm0)].imported) ||
                                        ((i.op == 0x10u || i.op == 0x11u) && never_made(i.origin)))
                                    {
                                        cold[b] = true;
                                        break;
                                    }
                                }
                            }
                            // (and those whose successors are all cold (e.g. a block of a cold path's phis' moves: its jump is then not between
                            // the likely blocks), then those whose predecessors are all cold (in the order: a loop's back edges are not followed))
                            for (auto o = f.order.size(); o-- > 0u;)
                            {
                                auto const b = f.order[o];
                                auto const& block = f.blocks[b];
                                if (cold[b] || b == f.order.front() || block.loopHeader || block.successors.empty())
                                    continue;
                                bool all = true;
                                for (auto s : block.successors)
                                    if (!cold[s])
                                        all = false;
                                cold[b] = all;
                            }
                            for (auto b : f.order)
                            {
                                auto const& block = f.blocks[b];
                                if (cold[b] || b == f.order.front() || block.loopHeader || block.predecessors.empty())
                                    continue;
                                bool all = true;
                                for (auto p : block.predecessors)
                                    if (!cold[p])
                                        all = false;
                                cold[b] = all;
                            }
                            return cold;
                        }
                        // (a block whose code is only a jump that moves nothing (e.g. an edge's of phis whose operands are where they are (see
                        // split_critical_edges)): it has no code; its label is its successor's, and a branch to it is to its successor)
                        std::vector<std::uint32_t> iForward;
                        std::uint32_t forwarded(std::uint32_t aBlock) const
                        {
                            for (std::size_t hops = 0u; aBlock < iForward.size() && iForward[aBlock] != aBlock && hops < iForward.size(); ++hops)
                                aBlock = iForward[aBlock];
                            return aBlock;
                        }
                        void forward_empty_jumps(std::vector<std::uint32_t>& aEmission)
                        {
                            iForward.resize(f.blocks.size());
                            for (std::uint32_t b = 0u; b < f.blocks.size(); ++b)
                                iForward[b] = b;
                            for (auto b : aEmission)
                            {
                                auto const& block = f.blocks[b];
                                if (b == f.order.front() || block.loopHeader || block.successors.size() != 1u || block.successors[0] == b)
                                    continue;
                                std::size_t live = 0u;
                                bool jump = false;
                                for (auto v : block.code)
                                    if (!f.values[v].dead)
                                    {
                                        ++live;
                                        jump = f.values[v].op == ssa::Jump;
                                    }
                                if (live != 1u || !jump)
                                    continue;
                                auto const to = block.successors[0];
                                auto const& target = f.blocks[to];
                                auto const index = static_cast<std::size_t>(std::find(target.predecessors.begin(), target.predecessors.end(), b) - target.predecessors.begin());
                                bool moves = false;
                                for (auto v : target.code)
                                {
                                    auto const& i = f.values[v];
                                    if (i.op != ssa::Phi)
                                        break;
                                    if (i.dead || index >= i.operands.size())
                                        continue;
                                    auto const s = i.operands[index];
                                    if (constant(s) || where(s).kind != where(v).kind || where(s).reg != where(v).reg || where(s).disp != where(v).disp)
                                    {
                                        moves = true;
                                        break;
                                    }
                                }
                                if (!moves)
                                    iForward[b] = to;
                            }
                            for (auto b : aEmission)
                                if (auto const to = forwarded(b); to != b)
                                    iBlockLabels[b] = iBlockLabels[to];
                            std::erase_if(aEmission, [&](std::uint32_t aBlock) { return forwarded(aBlock) != aBlock; });
                        }
                        // (a deopt (see optimizer::deopt): its values to the thread's deopt buffer; reported; this frame left (as its epilogue
                        // leaves it); the function's frame made as the baseline's code has it where it makes the call (its locals, its
                        // operand stack's slots and the call's arguments from the buffer); then the baseline's code that makes the call)
                        void deopt_exit(value v)
                        {
                            auto const& i = f.values[v];
                            auto const& d = iVersion->deopts.at(static_cast<std::size_t>(i.imm0));
                            auto const* const pointFound = deopt_point_at(iTier, d.call);
                            auto const* const frameFound = pointFound != nullptr ? deopt_frame_of(iTier, pointFound->function) : nullptr;
                            if (frameFound == nullptr)
                                throw unsupported{ "deopt point" };
                            auto const& point = *pointFound;
                            auto const& frame = *frameFound;
                            // (a deopt that leaves for the function's own baseline frame: its values put in the buffer, then the code that such
                            // deopts share (the call of deopt_taken, this frame left, the baseline's frame made and its locals set), which jumps
                            // to its own continuation (its operand stack and arguments, then the baseline's code) (its address: in the buffer's
                            // last slot))
                            bool const shared = d.frames.empty() && (iDeoptCommonFrame == nullptr || iDeoptCommonFrame == &frame);
                            a.load(true, R11, R15, ContextDeoptBuffer);
                            for (std::size_t o = 0u; o < i.operands.size(); ++o)
                            {
                                auto const x = i.operands[o];
                                auto const disp = 8 * static_cast<std::int32_t>(o);
                                auto const type = f.values[x].type;
                                bool const narrow = type == ssa::I32 || type == ssa::F32;
                                if (constant(x))
                                {
                                    a.mov_ri(RAX, narrow ? (bits(x) & 0xFFFFFFFFull) : bits(x), true);
                                    a.store(true, R11, disp, RAX);
                                }
                                else if (iHome[x] && !iDeoptAtLocation[v])
                                {
                                    a.load(!narrow, RAX, RBP, home_of(x));
                                    a.store(true, R11, disp, RAX);
                                }
                                else if (where(x).kind == location::Register)
                                {
                                    if (is_float_type(type))
                                        a.movf_store(type == ssa::F64, R11, disp, where(x).reg);
                                    else if (type == ssa::I32)
                                    {
                                        a.mov_rr(false, RAX, where(x).reg);
                                        a.store(true, R11, disp, RAX);
                                    }
                                    else
                                        a.store(true, R11, disp, where(x).reg);
                                }
                                else
                                {
                                    a.load(!narrow, RAX, RBP, where(x).disp);
                                    a.store(true, R11, disp, RAX);
                                }
                            }
                            std::size_t continuation = 0u;
                            if (shared)
                            {
                                continuation = a.new_label();
                                a.lea_label(RAX, continuation);
                                a.store(true, R11, static_cast<std::int32_t>(8u * MaxDeoptValues), RAX);
                                a.mov_ri(NativeIntegerArguments[2], i.imm0, false);
                                if (iDeoptCommon)
                                {
                                    a.jmp(*iDeoptCommon);
                                    a.bind(continuation);
                                    deopt_continuation(point, frame);
                                    return;
                                }
                                iDeoptCommon = a.new_label();
                                iDeoptCommonFrame = &frame;
                                a.bind(*iDeoptCommon);
                            }
                            // (reported: the native call does not preserve xmm8-15 (System V): saved around it)
                            if (!Windows)
                                for (int x = 8; x < 16; ++x)
                                    a.movf_store(true, RBP, iXmmNativeSave[x], x);
                            a.mov_rr(true, NativeIntegerArguments[0], R15);
                            a.mov_ri(NativeIntegerArguments[1], reinterpret_cast<std::uint64_t>(iVersion), true);
                            if (!shared)
                                a.mov_ri(NativeIntegerArguments[2], i.imm0, false);
                            a.mov_ri(RAX, reinterpret_cast<std::uint64_t>(&deopt_taken), true);
                            a.call_r(RAX);
                            if (!Windows)
                                for (int x = 8; x < 16; ++x)
                                    a.movf_load(true, x, RBP, iXmmNativeSave[x]);
                            // (this frame left)
                            for (int x = 8; x < 16; ++x)
                                if (iUsedCalleeXmms[x])
                                    a.movf_load(true, x, RBP, iXmmSave[x]);
                            a.lea(RSP, RBP, -8 * static_cast<std::int32_t>(iPushed.size()));
                            for (auto r = iPushed.rbegin(); r != iPushed.rend(); ++r)
                                a.pop(*r);
                            a.pop(RBP);
                            if (!d.frames.empty())
                            {
                                frames_exit(d);
                                return;
                            }
                            // (the baseline's frame (as its prologue makes it))
                            a.push(RBP);
                            a.mov_rr(true, RBP, RSP);
                            for (auto r : frame.pushed)
                                a.push(r);
                            a.sub_rsp_imm32(static_cast<std::int32_t>(frame.frameSize));
                            if (frame.memoryBase >= 0)
                                a.mov_ri(frame.memoryBase, reinterpret_cast<std::uint64_t>(external_memory(frame.memoryIndex).base), true);
                            for (auto const& [x, disp] : frame.savedXmms)
                                a.movf_store(true, RBP, disp, x);
                            a.load(true, RCX, R15, ContextDeoptBuffer);
                            for (std::size_t o = 0u; o < frame.locals.size(); ++o)
                            {
                                auto const& l = frame.locals[o];
                                auto const disp = 8 * static_cast<std::int32_t>(o);
                                if (l.pinned)
                                {
                                    if (is_float_type(l.type))
                                        a.movf_load(l.type == ssa::F64, l.reg, RCX, disp);
                                    else
                                        a.load(is_wide_type(l.type), l.reg, RCX, disp);
                                }
                                else
                                {
                                    a.load(true, RAX, RCX, disp);
                                    a.store(true, RBP, l.disp, RAX);
                                }
                            }
                            if (shared)
                            {
                                a.jmp_m(RCX, static_cast<std::int32_t>(8u * MaxDeoptValues));
                                a.bind(continuation);
                            }
                            deopt_continuation(point, frame);
                        }
                        // (a deopt's continuation, once the baseline's frame is made and its locals set (rcx: the buffer): its operand stack 
                        // and arguments (after the locals in the buffer), then the baseline's code)
                        void deopt_continuation(compiled_module::deopt_point const& point, compiled_module::deopt_frame const& frame)
                        {
                            auto o = frame.locals.size();
                            for (auto const& e : point.stack)
                            {
                                auto const disp = 8 * static_cast<std::int32_t>(o++);
                                if (e.inSlot)
                                {
                                    a.load(true, RAX, RCX, disp);
                                    a.store(true, RBP, e.disp, RAX);
                                }
                                else if (e.local >= 0 && static_cast<std::size_t>(e.local) < frame.locals.size())
                                {
                                    // (a local's value that the code reads later (when the local is written, or the entry is used): the
                                    // local has it (the optimized code's local may be another value by then, or none (no longer used)))
                                    auto const& l = frame.locals[static_cast<std::size_t>(e.local)];
                                    if (l.pinned)
                                    {
                                        if (is_float_type(l.type))
                                            a.movf_load(l.type == ssa::F64, l.reg, RCX, disp);
                                        else
                                            a.load(is_wide_type(l.type), l.reg, RCX, disp);
                                    }
                                    else
                                    {
                                        a.load(true, RAX, RCX, disp);
                                        a.store(true, RBP, l.disp, RAX);
                                    }
                                }
                            }
                            for (std::uint32_t arg = 0u; arg < point.arguments; ++arg)
                            {
                                auto const disp = 8 * static_cast<std::int32_t>(o++);
                                a.load(true, RAX, RCX, disp);
                                a.store(true, RSP, 8 * static_cast<std::int32_t>(arg), RAX);
                            }
                            a.mov_ri(RAX, deopt_address(iTier, point, point.resume), true);
                            a.jmp_r(RAX);
                        }
                        // (the frames of a deopt that leaves for inlined calls' frames (see optimizer::deopt::frames), made (after this frame
                        // is left) from its values (in the deopt buffer: each frame's locals and operand stack, then the call's arguments), as
                        // the baseline's code makes them: each but the last (the innermost) as its call (the next's) has it while it runs (the
                        // next's return address is where it returns to), the last at its call (which it then makes). A register's value that
                        // a frame has (its pinned locals', its memory's base) is the one that the next frame's prologue saves (it is restored
                        // when that one returns), and the one that the frame's code restores after its call (registers that it saves around 
                        // its calls); the registers have the last frame's values (and, those that no frame has, the values they have now:
                        // the optimized code's caller's))
                        void frames_exit(optimizer::deopt const& aDeopt)
                        {
                            struct register_value
                            {
                                enum kind_t : std::uint8_t { Now, Buffer, Constant } kind = Now;
                                std::int32_t disp = 0;      ///< (Buffer: its value's, in the buffer)
                                bool wide = true;
                                std::uint64_t bits = 0u;    ///< (Constant)
                            };
                            std::array<register_value, 16> gprs{};
                            std::array<register_value, 16> xmms{};
                            // (RAX: a register's value; RCX: the buffer)
                            auto const gpr_value = [&](register_value const& aValue, int aRegister)
                            {
                                switch (aValue.kind)
                                {
                                case register_value::Buffer:
                                    a.load(aValue.wide, RAX, RCX, aValue.disp);
                                    break;
                                case register_value::Constant:
                                    a.mov_ri(RAX, aValue.bits, true);
                                    break;
                                default:
                                    a.mov_rr(true, RAX, aRegister);
                                    break;
                                }
                            };
                            auto const store_xmm = [&](register_value const& aValue, int aXmm, std::int32_t aDisp)
                            {
                                if (aValue.kind == register_value::Buffer)
                                {
                                    a.load(true, RAX, RCX, aValue.disp);
                                    a.store(true, RBP, aDisp, RAX);
                                }
                                else
                                    a.movf_store(true, RBP, aDisp, aXmm);
                            };
                            a.load(true, RCX, R15, ContextDeoptBuffer);
                            std::int32_t o = 0;
                            compiled_module::deopt_point const* previous = nullptr;
                            for (auto const& d : aDeopt.frames)
                            {
                                auto const* const pointFound = deopt_point_at(iTier, d.position);
                                auto const* const frameFound = deopt_frame_of(iTier, d.function);
                                if (pointFound == nullptr || frameFound == nullptr)
                                    throw unsupported{ "deopt point" };
                                auto const& point = *pointFound;
                                auto const& frame = *frameFound;
                                if (previous != nullptr)
                                {
                                    // (the previous frame's call: it returns there)
                                    a.mov_ri(RAX, deopt_address(iTier, *previous, previous->returned), true);
                                    a.push(RAX);
                                }
                                a.push(RBP);
                                a.mov_rr(true, RBP, RSP);
                                for (auto r : frame.pushed)
                                {
                                    if (gprs[r].kind == register_value::Now)
                                        a.push(r);
                                    else
                                    {
                                        gpr_value(gprs[r], r);
                                        a.push(RAX);
                                    }
                                }
                                a.sub_rsp_imm32(static_cast<std::int32_t>(frame.frameSize));
                                for (auto const& [x, disp] : frame.savedXmms)
                                    store_xmm(xmms[x], x, disp);
                                if (frame.memoryBase >= 0)
                                    gprs[frame.memoryBase] = register_value{ register_value::Constant, 0, true,
                                        reinterpret_cast<std::uint64_t>(external_memory(frame.memoryIndex).base) };
                                auto const to_local = [&](std::size_t aLocal, std::int32_t aDisp)
                                {
                                    auto const& l = frame.locals[aLocal];
                                    if (l.pinned)
                                    {
                                        auto& r = is_float_type(l.type) ? xmms[l.reg] : gprs[l.reg];
                                        r = register_value{ register_value::Buffer, aDisp, is_wide_type(l.type), 0u };
                                    }
                                    else
                                    {
                                        a.load(true, RAX, RCX, aDisp);
                                        a.store(true, RBP, l.disp, RAX);
                                    }
                                };
                                for (std::size_t l = 0u; l < frame.locals.size(); ++l)
                                    to_local(l, 8 * o++);
                                for (auto const& e : point.stack)
                                {
                                    auto const disp = 8 * o++;
                                    if (e.inSlot)
                                    {
                                        a.load(true, RAX, RCX, disp);
                                        a.store(true, RBP, e.disp, RAX);
                                    }
                                    else if (e.local >= 0 && static_cast<std::size_t>(e.local) < frame.locals.size())
                                        to_local(static_cast<std::size_t>(e.local), disp);
                                }
                                if (&d != &aDeopt.frames.back())
                                {
                                    // (what its code restores after its call)
                                    for (auto const& [r, disp] : frame.callSavedGprs)
                                    {
                                        gpr_value(gprs[r], r);
                                        a.store(true, RBP, disp, RAX);
                                    }
                                    for (auto const& [x, disp] : frame.callSavedXmms)
                                        store_xmm(xmms[x], x, disp);
                                }
                                previous = &point;
                            }
                            // (the last frame's call: its arguments)
                            for (std::uint32_t arg = 0u; arg < aDeopt.arguments; ++arg)
                            {
                                a.load(true, RAX, RCX, 8 * o++);
                                a.store(true, RSP, 8 * static_cast<std::int32_t>(arg), RAX);
                            }
                            // (the registers' values)
                            for (int x = 0; x < 16; ++x)
                                if (xmms[x].kind == register_value::Buffer)
                                    a.movf_load(xmms[x].wide, x, RCX, xmms[x].disp);
                            for (int r = 0; r < 16; ++r)
                                if (r != RCX && gprs[r].kind != register_value::Now)
                                {
                                    if (gprs[r].kind == register_value::Buffer)
                                        a.load(gprs[r].wide, r, RCX, gprs[r].disp);
                                    else
                                        a.mov_ri(r, gprs[r].bits, true);
                                }
                            a.mov_ri(RAX, deopt_address(iTier, *previous, previous->resume), true);
                            a.jmp_r(RAX);
                        }
                        // (the moves of the phis of the successor (of the block that ends with a jump to it): a parallel move)
                        void phi_moves(std::uint32_t aFrom, std::uint32_t aTo)
                        {
                            auto const& to = f.blocks[aTo];
                            auto const index = static_cast<std::size_t>(std::find(to.predecessors.begin(), to.predecessors.end(), aFrom) - to.predecessors.begin());
                            struct move
                            {
                                value source;
                                value destination;
                            };
                            std::vector<move> moves;
                            for (auto v : to.code)
                            {
                                auto const& i = f.values[v];
                                if (i.op != ssa::Phi)
                                    break;
                                if (i.dead || index >= i.operands.size())
                                    continue;
                                auto const s = i.operands[index];
                                if (!constant(s) && where(s).kind == where(v).kind && where(s).reg == where(v).reg && where(s).disp == where(v).disp)
                                    continue;
                                moves.push_back(move{ s, v });
                            }
                            // (a move whose destination is not another's source first; a cycle: one source to a scratch register)
                            std::vector<bool> done(moves.size(), false);
                            std::size_t remaining = moves.size();
                            std::vector<std::pair<value, int>> scratch;    // (sources moved to scratch registers)
                            auto const blocked = [&](std::size_t aMove)
                            {
                                auto const& d = where(moves[aMove].destination);
                                bool const dx = is_float_type(f.values[moves[aMove].destination].type);
                                for (std::size_t m = 0u; m < moves.size(); ++m)
                                {
                                    if (done[m] || m == aMove)
                                        continue;
                                    auto const s = moves[m].source;
                                    if (constant(s) || std::any_of(scratch.begin(), scratch.end(), [&](auto const& aScratch) { return aScratch.first == s; }))
                                        continue;
                                    auto const& l = where(s);
                                    if (l.kind == d.kind && (d.kind == location::Register ? (l.reg == d.reg && is_float_type(f.values[s].type) == dx) : l.disp == d.disp))
                                        return true;
                                }
                                return false;
                            };
                            auto const perform = [&](std::size_t aMove)
                            {
                                auto const s = moves[aMove].source;
                                auto const d = moves[aMove].destination;
                                bool const x = is_float_type(f.values[d].type);
                                // (a source that was moved to a scratch register to break a cycle)
                                for (auto const& [sv, reg] : scratch)
                                    if (sv == s)
                                    {
                                        defined(d, reg);
                                        return;
                                    }
                                // (to a slot: from the source's register, or a constant that fits an immediate (not through a scratch register))
                                if (where(d).kind == location::Slot && in_register(s))
                                {
                                    defined(d, where(s).reg);
                                    return;
                                }
                                if (!x && where(d).kind == location::Slot && constant(s) && (is_wide_type(f.values[s].type) ?
                                    fits_i32(static_cast<std::int64_t>(bits(s))) : bits(s) < 0x80000000u))   // (an i32's: zero extended, as a register's)
                                {
                                    a.store_imm(true, RBP, where(d).disp, static_cast<std::int32_t>(bits(s)));
                                    return;
                                }
                                if (x)
                                {
                                    auto const r = target(d, 1);
                                    xmm_into(s, r);
                                    defined(d, r);
                                }
                                else
                                {
                                    auto const r = target(d, R11);
                                    gpr_into(s, r);
                                    defined(d, r);
                                }
                            };
                            while (remaining != 0u)
                            {
                                bool progress = false;
                                for (std::size_t m = 0u; m < moves.size(); ++m)
                                    if (!done[m] && !blocked(m))
                                    {
                                        perform(m);
                                        done[m] = true;
                                        --remaining;
                                        progress = true;
                                    }
                                if (progress)
                                    continue;
                                // (a cycle: a source (of a move not done) to a scratch register; the moves from it use that)
                                for (std::size_t m = 0u; m < moves.size(); ++m)
                                    if (!done[m])
                                    {
                                        auto const s = moves[m].source;
                                        bool const x = is_float_type(f.values[s].type);
                                        int const reg = x ? 0 : RAX;
                                        for (auto const& sc : scratch)
                                            if (sc.second == reg)
                                                throw unsupported{ "phi cycles" };
                                        if (x)
                                            xmm_into(s, reg);
                                        else
                                            gpr_into(s, reg);
                                        scratch.emplace_back(s, reg);
                                        break;
                                    }
                                // (its location no longer blocks the moves to it: the moves from it use the scratch register)
                            }
                        }
                        void jump_to(std::uint32_t aTarget)
                        {
                            phi_moves(iCurrentBlock, aTarget);
                            if (forwarded(aTarget) != iNext)
                                a.jmp(iBlockLabels[aTarget]);
                        }
                        // (a comparison that the next instruction (a branch) uses (only): its condition, not its value)
                        bool fused(value aValue) const
                        {
                            if (aValue == iForceFused)
                                return true;
                            if (iNextInstruction == ssa::None)
                                return false;
                            auto const& next = f.values[iNextInstruction];
                            return next.op == ssa::Branch && next.operands[0] == aValue && f.uses[aValue] == 1u;
                        }
                        // (a branch on a condition that is ands, ors and eqzs (each used only by the one that uses it) of comparisons (each
                        // used only by it): the comparisons' branches (short circuited, see branch); they are moved (they have no effects) to
                        // the block's end (before its branch), after its other instructions (so that their operands are live there), unless
                        // a call is among those)
                        static bool comparison(std::uint16_t aOp) { return (aOp >= 0x45u && aOp <= 0x66u); }
                        static bool connective(std::uint16_t aOp) { return aOp == 0x71u || aOp == 0x72u || aOp == 0x45u; }
                        void defer_conditions()
                        {
                            iDeferred.assign(f.values.size(), false);
                            for (std::uint32_t b = 0u; b < f.blocks.size(); ++b)
                            {
                                auto& block = f.blocks[b];
                                if (!block.reachable || block.successors.size() != 2u)
                                    continue;
                                auto& code = block.code;
                                std::size_t last = code.size();
                                while (last > 0u && f.values[code[last - 1u]].dead)
                                    --last;
                                if (last == 0u || f.values[code[last - 1u]].op != ssa::Branch)
                                    continue;
                                auto const root = f.values[code[last - 1u]].operands[0];
                                std::vector<value> tree;
                                std::size_t connectives = 0u;
                                bool valid = true;
                                std::vector<bool> inBlock(f.values.size(), false);
                                for (std::size_t c = 0u; c + 1u < last; ++c)
                                    inBlock[code[c]] = true;
                                // (the tree: its connectives' operands are its nodes; its leaves are comparisons, or (an or's operand, or a
                                // comparison that is not deferred (used elsewhere)) other i32s (tested: not 0), which are not deferred (an and's:
                                // not others (a & b is not a && b unless they are 0 or 1)))
                                std::vector<value> pending{ root };
                                std::vector<value> underOr;
                                while (!pending.empty() && valid)
                                {
                                    auto const v = pending.back();
                                    pending.pop_back();
                                    auto const& i = f.values[v];
                                    if (v >= inBlock.size() || !inBlock[v] || i.dead || i.type != ssa::I32 || f.uses[v] != 1u || !comparison(i.op) && !connective(i.op))
                                    {
                                        // (a value leaf: an or's operand; or an and's that is 0 or 1 (a comparison: a & b is a && b))
                                        if (v != root && !i.dead && i.type == ssa::I32 && i.op != ssa::Phi &&
                                            (std::find(underOr.begin(), underOr.end(), v) != underOr.end() || comparison(i.op)))
                                            continue;
                                        valid = false;
                                        break;
                                    }
                                    tree.push_back(v);
                                    bool const node = (i.op == 0x71u || i.op == 0x72u) || (i.op == 0x45u && i.operands[0] < inBlock.size() &&
                                        inBlock[i.operands[0]] && f.uses[i.operands[0]] == 1u && f.values[i.operands[0]].type == ssa::I32 &&
                                        (comparison(f.values[i.operands[0]].op) || connective(f.values[i.operands[0]].op)));
                                    if (node)
                                    {
                                        if (i.op != 0x45u)
                                            ++connectives;
                                        for (auto o : i.operands)
                                        {
                                            pending.push_back(o);
                                            if (i.op == 0x72u)
                                                underOr.push_back(o);
                                        }
                                    }
                                }
                                if (!valid || connectives == 0u)
                                    continue;
                                // (the tree's nodes are moved after the block's other instructions: not if a call is among those (after the
                                // first of them))
                                std::vector<bool> member(f.values.size(), false);
                                for (auto v : tree)
                                    member[v] = true;
                                std::size_t first = last;
                                for (std::size_t c = 0u; c + 1u < last; ++c)
                                    if (member[code[c]])
                                    {
                                        first = c;
                                        break;
                                    }
                                for (std::size_t c = first; c + 1u < last && valid; ++c)
                                    if (!member[code[c]] && !f.values[code[c]].dead && (is_call(f.values[code[c]]) || f.values[code[c]].op == ssa::Deopt))
                                        valid = false;
                                if (!valid)
                                    continue;
                                std::vector<value> reordered;
                                reordered.reserve(code.size());
                                for (std::size_t c = 0u; c + 1u < last; ++c)
                                    if (!member[code[c]])
                                        reordered.push_back(code[c]);
                                for (std::size_t c = 0u; c + 1u < last; ++c)
                                    if (member[code[c]])
                                        reordered.push_back(code[c]);
                                for (std::size_t c = last - 1u; c < code.size(); ++c)
                                    reordered.push_back(code[c]);
                                code = std::move(reordered);
                                for (auto v : tree)
                                    iDeferred[v] = true;
                            }
                        }
                        // (a deferred condition's code (see defer_conditions): branches to aTrue or aFalse (aNext: the label that is bound
                        // next, if either))
                        void deferred_condition(value v, std::size_t aTrue, std::size_t aFalse, std::optional<std::size_t> aNext)
                        {
                            auto const& i = f.values[v];
                            if (!iDeferred[v])
                            {
                                // (an i32 that is not a comparison (see defer_conditions): true if it is not 0)
                                std::uint8_t cc = NE;
                                if (auto const flags = flags_of(v))
                                    cc = *flags;
                                else
                                {
                                    auto const r = gpr(v, RAX);
                                    a.test_rr(false, r, r);
                                }
                                if (aNext == aFalse)
                                    a.jcc(cc, aTrue);
                                else
                                {
                                    a.jcc(invert(cc), aFalse);
                                    if (aNext != aTrue)
                                        a.jmp(aTrue);
                                }
                                return;
                            }
                            bool const node = i.op == 0x71u || i.op == 0x72u || (i.op == 0x45u && iDeferred[i.operands[0]]);
                            iDeferred[v] = false;
                            if (node && i.op == 0x45u)
                            {
                                deferred_condition(i.operands[0], aFalse, aTrue, aNext);
                                return;
                            }
                            if (node)
                            {
                                auto const middle = a.new_label();
                                if (i.op == 0x71u)
                                    deferred_condition(i.operands[0], middle, aFalse, middle);
                                else
                                    deferred_condition(i.operands[0], aTrue, middle, middle);
                                a.bind(middle);
                                deferred_condition(i.operands[1], aTrue, aFalse, aNext);
                                return;
                            }
                            iForceFused = v;
                            instruction(v);
                            iForceFused = ssa::None;
                            if (iPending.compare != v)
                                throw std::logic_error("neos::jit: deferred condition");
                            auto const pending = iPending;
                            iPending = {};
                            if (pending.floatEq)
                            {
                                // (equal: E and not P)
                                a.jcc(P, aFalse);
                                if (aNext == aFalse)
                                    a.jcc(E, aTrue);
                                else
                                {
                                    a.jcc(NE, aFalse);
                                    if (aNext != aTrue)
                                        a.jmp(aTrue);
                                }
                                return;
                            }
                            if (pending.floatNe)
                            {
                                // (not equal: NE or P)
                                a.jcc(P, aTrue);
                                if (aNext == aFalse)
                                    a.jcc(NE, aTrue);
                                else
                                {
                                    a.jcc(E, aFalse);
                                    if (aNext != aTrue)
                                        a.jmp(aTrue);
                                }
                                return;
                            }
                            if (aNext == aFalse)
                                a.jcc(pending.cc, aTrue);
                            else
                            {
                                a.jcc(invert(pending.cc), aFalse);
                                if (aNext != aTrue)
                                    a.jmp(aTrue);
                            }
                        }
                        void instruction(value v)
                        {
                            if (iCallStackChecks && is_call(f.values[v]) && !f.values[v].dead)
                            {
                                a.alu_rm(7, true, RSP, R15, ContextStackLimit);
                                a.jcc(B, iTrapLabels[TrapStackExhausted]);
                            }
                            if (!iSavesAtCalls.empty() && is_call(f.values[v]) && !f.values[v].dead)
                            {
                                save_at_call(v);
                                instruction_of(v);
                                restore_at_call(v);
                                return;
                            }
                            instruction_of(v);
                        }
                        void instruction_of(value v)
                        {
                            auto const& i = f.values[v];
                            auto const op = i.op;
                            if (i.constant())
                                return;     // (constants are where they are used)
                            if (iDeferred[v])
                                return;     // (see defer_and_branch)
                            switch (op)
                            {
                            case ssa::Param:
                                if (where(v).kind == location::Register)
                                {
                                    auto const type = i.type;
                                    auto const disp = 16 + 8 * static_cast<std::int32_t>(i.imm0);
                                    if (is_float_type(type))
                                        a.movf_load(type == ssa::F64, where(v).reg, RBP, disp);
                                    else
                                        a.load(is_wide_type(type), where(v).reg, RBP, disp);
                                }
                                return;
                            case ssa::CallResult:
                                {
                                    auto const disp = ContextResults + 8 * static_cast<std::int32_t>(i.imm0);
                                    if (is_float_type(i.type))
                                    {
                                        auto const r = target(v, 0);
                                        a.movf_load(i.type == ssa::F64, r, R15, disp);
                                        defined(v, r);
                                    }
                                    else
                                    {
                                        auto const r = target(v, RAX);
                                        a.load(is_wide_type(i.type), r, R15, disp);
                                        defined(v, r);
                                    }
                                }
                                return;
                            case ssa::Jump:
                                jump_to(f.blocks[iCurrentBlock].successors[0]);
                                return;
                            case ssa::Branch:
                                branch(v);
                                return;
                            case ssa::Switch:
                                switch_(v);
                                return;
                            case ssa::Return:
                                return_(v);
                                return;
                            case ssa::Trap:
                                a.jmp(iTrapLabels[TrapUnreachable]);
                                return;
                            case ssa::Deopt:
                                iDeoptExits.emplace_back(v, a.new_label());
                                a.jmp(iDeoptExits.back().second);
                                return;
                            case 0x10:
                                call(v);
                                return;
                            case 0x11:
                                call_indirect(v);
                                return;
                            case 0x1B:
                                select(v);
                                return;
                            case 0x23:
                                {
                                    a.load(true, RCX, R15, ContextGlobals);
                                    auto const disp = 8 * static_cast<std::int32_t>(i.imm0);
                                    if (is_float_type(i.type))
                                    {
                                        auto const r = target(v, 0);
                                        a.movf_load(i.type == ssa::F64, r, RCX, disp);
                                        defined(v, r);
                                    }
                                    else
                                    {
                                        auto const r = target(v, RAX);
                                        a.load(is_wide_type(i.type), r, RCX, disp);
                                        defined(v, r);
                                    }
                                }
                                return;
                            case 0x24:
                                {
                                    auto const x = i.operands[0];
                                    auto const type = f.values[x].type;
                                    auto const disp = 8 * static_cast<std::int32_t>(i.imm0);
                                    a.load(true, RCX, R15, ContextGlobals);
                                    if (is_float_type(type))
                                        a.movf_store(type == ssa::F64, RCX, disp, xmm(x, 0));
                                    else
                                        a.store(true, RCX, disp, gpr(x, RAX));
                                }
                                return;
                            default:
                                break;
                            }
                            if (i.load() || i.store())
                            {
                                memory(v);
                                return;
                            }
                            numeric(v);
                        }
                        // --- control
                        void branch(value v)
                        {
                            auto const& i = f.values[v];
                            auto const& successors = f.blocks[iCurrentBlock].successors;
                            auto const whenTrue = forwarded(successors[0]);
                            auto const whenFalse = forwarded(successors[1]);
                            if (iDeferred[i.operands[0]])
                            {
                                // (a condition of comparisons: their branches (see defer_conditions))
                                std::optional<std::size_t> next;
                                if (whenTrue == iNext)
                                    next = iBlockLabels[whenTrue];
                                else if (whenFalse == iNext)
                                    next = iBlockLabels[whenFalse];
                                deferred_condition(i.operands[0], iBlockLabels[whenTrue], iBlockLabels[whenFalse], next);
                                return;
                            }
                            std::uint8_t cc = NE;
                            bool floatEq = false;
                            bool floatNe = false;
                            if (iPending.compare == i.operands[0])
                            {
                                cc = iPending.cc;
                                floatEq = iPending.floatEq;
                                floatNe = iPending.floatNe;
                                iPending = {};
                            }
                            else if (auto const flags = flags_of(i.operands[0]))
                                cc = *flags;
                            else
                            {
                                auto const c = gpr(i.operands[0], RAX);
                                a.test_rr(false, c, c);
                            }
                            // (successors have no phis (edges to blocks with phis are split))
                            auto const t = iBlockLabels[whenTrue];
                            auto const e = iBlockLabels[whenFalse];
                            if (floatEq)
                            {
                                a.jcc(P, e);
                                a.jcc(E, t);
                                if (whenFalse != iNext)
                                    a.jmp(e);
                                return;
                            }
                            if (floatNe)
                            {
                                a.jcc(P, t);
                                a.jcc(NE, t);
                                if (whenFalse != iNext)
                                    a.jmp(e);
                                return;
                            }
                            if (whenTrue == iNext)
                                a.jcc(invert(cc), e);
                            else
                            {
                                a.jcc(cc, t);
                                if (whenFalse != iNext)
                                    a.jmp(e);
                            }
                        }
                        void switch_(value v)
                        {
                            auto const& i = f.values[v];
                            auto const& successors = f.blocks[iCurrentBlock].successors;
                            auto const index = gpr(i.operands[0], RAX);
                            if (index != RAX)
                                a.mov_rr(false, RAX, index);
                            auto const targets = successors.size() - 1u;
                            if (targets >= 8u)
                            {
                                // (a binary search of the targets' indices)
                                switch_search(0u, targets, successors);
                                return;
                            }
                            for (std::size_t t = 0u; t < targets; ++t)
                            {
                                a.alu_ri(7, false, RAX, static_cast<std::int32_t>(t));
                                a.jcc(E, iBlockLabels[successors[t]]);
                            }
                            if (forwarded(successors.back()) != iNext)
                                a.jmp(iBlockLabels[successors.back()]);
                        }
                        void switch_search(std::size_t aBegin, std::size_t aEnd, decltype(ssa::block::successors) const& aSuccessors)
                        {
                            if (aEnd - aBegin <= 4u)
                            {
                                for (auto t = aBegin; t < aEnd; ++t)
                                {
                                    a.alu_ri(7, false, RAX, static_cast<std::int32_t>(t));
                                    a.jcc(E, iBlockLabels[aSuccessors[t]]);
                                }
                                a.jmp(iBlockLabels[aSuccessors.back()]);
                                return;
                            }
                            auto const middle = (aBegin + aEnd) / 2u;
                            auto const upper = a.new_label();
                            a.alu_ri(7, false, RAX, static_cast<std::int32_t>(middle));
                            a.jcc(AE, upper);
                            switch_search(aBegin, middle, aSuccessors);
                            a.bind(upper);
                            switch_search(middle, aEnd, aSuccessors);
                        }
                        void return_(value v)
                        {
                            auto const& i = f.values[v];
                            for (std::size_t r = i.operands.size(); r-- > 0u;)
                            {
                                auto const x = i.operands[r];
                                auto const type = f.values[x].type;
                                if (r == 0u)
                                {
                                    if (is_float_type(type))
                                        a.movq_gx(type == ssa::F64, RAX, xmm(x, 0));
                                    else
                                        gpr_into(x, RAX);
                                }
                                else if (is_float_type(type))
                                    a.movf_store(type == ssa::F64, R15, ContextResults + 8 * static_cast<std::int32_t>(r), xmm(x, 0));
                                else
                                    a.store(true, R15, ContextResults + 8 * static_cast<std::int32_t>(r), gpr(x, RCX));
                            }
                            a.jmp(iEpilogue);
                        }
                        // --- calls
                        // (a value stored to the outgoing area, as an argument (a word))
                        // (rax's constant, while a call's arguments are stored (see argument): reset before them)
                        std::optional<std::uint64_t> iArgumentRax;
                        void argument(value aValue, std::int32_t aDisp)
                        {
                            auto const type = f.values[aValue].type;
                            if (constant(aValue))
                            {
                                auto const b = bits(aValue);
                                if (fits_i32(static_cast<std::int64_t>(b)))
                                    a.store_imm(true, RSP, aDisp, static_cast<std::int32_t>(b));
                                else
                                {
                                    // (e.g. the same constant for several arguments: rax has it)
                                    if (iArgumentRax != b)
                                        a.mov_ri(RAX, b, true);
                                    iArgumentRax = b;
                                    a.store(true, RSP, aDisp, RAX);
                                }
                                return;
                            }
                            iArgumentRax.reset();
                            if (is_float_type(type))
                                a.movf_store(type == ssa::F64, RSP, aDisp, xmm(aValue, 0));
                            else
                                a.store(true, RSP, aDisp, gpr(aValue, RAX));
                        }
                        void result(value v)
                        {
                            auto const type = f.values[v].type;
                            if (type == ssa::Void || f.values[v].dead)
                                return;
                            if (is_float_type(type))
                            {
                                auto const r = target(v, 0);
                                a.movq_xg(type == ssa::F64, r, RAX);
                                defined(v, r);
                            }
                            else
                                defined(v, RAX);
                        }
                        void call(value v)
                        {
                            auto const& i = f.values[v];
                            auto const callee = static_cast<std::uint32_t>(i.imm0);
                            if (callee == optimizer::intrinsics::guard_failed(iModule))
                            {
                                // (an inlined call's guard failed (aVersion, the site))
                                value const arguments[] = { ssa::None, ssa::None, i.operands[0] };
                                native_call(reinterpret_cast<void const*>(&guard_failed), arguments, v, true, reinterpret_cast<std::uint64_t>(iVersion));
                                return;
                            }
                            auto const& c = iModule.functions[callee];
                            if (c.imported)
                            {
                                plugin_call(v, callee, c);
                                return;
                            }
                            iArgumentRax.reset();
                            // (the parameters that the callee reads (it is known: its code is the same in every tier): the others are not 
                            // passed)
                            static std::vector<bool> const sAll;
                            auto const& read = callee < iTier.parametersRead.size() ? iTier.parametersRead[callee] : sAll;
                            for (std::size_t p = 0u; p < i.operands.size(); ++p)
                                if (p >= read.size() || read[p])
                                    argument(i.operands[p], 8 * static_cast<std::int32_t>(p));
                            iOutgoing = std::max(iOutgoing, 8 * static_cast<std::int32_t>(std::max(i.operands.size(), c.results->size())));
                            // (System V: the callee may call native code, which does not preserve xmm8-xmm15: those that hold values across
                            // the call are saved around it)
                            auto const saved = save_xmms(v);
                            a.mov_ri(R11, reinterpret_cast<std::uint64_t>(&iTier.indirect[callee].target), true);
                            a.op_rm({ 0xFFu }, false, 2, R11, 0);
                            iCallSites.emplace_back(a.new_label(), i.origin);
                            a.bind(iCallSites.back().first);
                            restore_xmms(saved);
                            result(v);
                        }
                        void call_indirect(value v)
                        {
                            auto const& i = f.values[v];
                            auto const type = static_cast<std::uint32_t>(i.imm0);
                            auto const n = i.operands.size() - 1u;
                            iArgumentRax.reset();
                            for (std::size_t p = 0u; p < n; ++p)
                                argument(i.operands[p], 8 * static_cast<std::int32_t>(p));
                            auto const& results = *iModule.types[type].second;
                            iOutgoing = std::max(iOutgoing, 8 * static_cast<std::int32_t>(std::max(n + 1u, results.size())));
                            auto const index = gpr(i.operands[n], RAX);
                            if (index != RAX)
                                a.mov_rr(false, RAX, index);
                            a.alu_ri(7, false, RAX, static_cast<std::int32_t>(iModule.functions.size()));
                            a.jcc(AE, iTrapLabels[TrapIndirectCall]);
                            a.shift_ri(4, true, RAX, 4u);
                            a.mov_ri(R11, reinterpret_cast<std::uint64_t>(iTier.indirect.data()), true);
                            a.alu_rr(0, true, R11, RAX);
                            a.alu_mi(7, false, R11, 8, static_cast<std::int32_t>(type));
                            a.jcc(NE, iTrapLabels[TrapIndirectCall]);
                            a.load(true, R11, R11, 0);
                            auto const saved = save_xmms(v);    // (System V: see call)
                            a.call_r(R11);
                            iCallSites.emplace_back(a.new_label(), i.origin);
                            a.bind(iCallSites.back().first);
                            restore_xmms(saved);
                            result(v);
                        }
                        // (a native function's call: its arguments (ssa::None: the context (the first), or aImmediate (the second)); its
                        // result (of v's type) in rax or xmm0; the xmms that native code does not preserve (System V) saved around it)
                        void native_call(void const* aFunction, std::span<value const> aArguments, value v, bool aWithContext, std::uint64_t aImmediate = 0u,
                            bool aCanTrap = false)
                        {
                            iArgumentRax.reset();
                            for (std::size_t p = 0u; p < aArguments.size(); ++p)
                            {
                                auto const disp = ShadowSpace + 8 * static_cast<std::int32_t>(p);
                                if (aArguments[p] == ssa::None)
                                {
                                    if (p == 0u && aWithContext)
                                        a.store(true, RSP, disp, R15);
                                    else
                                    {
                                        a.mov_ri(RAX, aImmediate, true);
                                        a.store(true, RSP, disp, RAX);
                                        iArgumentRax.reset();
                                    }
                                }
                                else
                                    argument(aArguments[p], disp);
                            }
                            native_arguments(aArguments);
                            iOutgoing = std::max(iOutgoing, ShadowSpace + 8 * static_cast<std::int32_t>(aArguments.size() + 1u));
                            auto const saved = save_xmms(v);
                            a.mov_ri(RAX, reinterpret_cast<std::uint64_t>(aFunction), true);
                            a.call_r(RAX);
                            restore_xmms(saved);
                            if (aCanTrap)
                            {
                                a.alu_mi(7, false, R15, ContextTrap, 0);
                                a.jcc(NE, iTrapFromContext);
                            }
                        }
                        // (a native call's arguments (staged in the outgoing area, after its shadow space): to their registers, or (beyond those)
                        // to the stack (Windows: after its shadow space; System V: from rsp), as the native calling convention has them (the
                        // stack arguments that are moved there overwrite only staged arguments that are in registers by then))
                        void native_arguments(std::span<value const> aArguments)
                        {
                            int intIndex = 0;
                            int floatIndex = 0;
                            std::int32_t stackIndex = 0;
                            iArgumentRax.reset();
                            for (std::size_t p = 0u; p < aArguments.size(); ++p)
                            {
                                auto const disp = ShadowSpace + 8 * static_cast<std::int32_t>(p);
                                bool const isFloat = aArguments[p] != ssa::None && is_float_type(f.values[aArguments[p]].type);
                                bool const inRegister = Windows ? p < std::size(NativeIntegerArguments) :
                                    isFloat ? floatIndex < 8 : intIndex < static_cast<int>(std::size(NativeIntegerArguments));
                                if (!inRegister)
                                {
                                    auto const slot = Windows ? disp - 8 * static_cast<std::int32_t>(std::size(NativeIntegerArguments)) : 8 * stackIndex++;
                                    if (slot != disp)
                                    {
                                        a.load(true, RAX, RSP, disp);
                                        a.store(true, RSP, slot, RAX);
                                    }
                                }
                                else if (isFloat)
                                {
                                    int const x = Windows ? static_cast<int>(p) : floatIndex++;
                                    a.movf_load(f.values[aArguments[p]].type == ssa::F64, x, RSP, disp);
                                }
                                else
                                {
                                    int const g = Windows ? NativeIntegerArguments[p] : NativeIntegerArguments[intIndex++];
                                    a.load(true, g, RSP, disp);
                                }
                            }
                        }
                        // (System V: the values in xmm8-xmm15 that are live across the call (native code does not preserve them))
                        std::vector<int> save_xmms(value v)
                        {
                            std::vector<int> saved;
                            if (Windows)
                                return saved;
                            if (auto const live = iLiveXmmsAtCalls.find(iPosition[v]); live != iLiveXmmsAtCalls.end())
                                for (int x = 8; x < 16; ++x)
                                    if ((live->second & (1u << x)) != 0u)
                                        saved.push_back(x);
                            for (auto x : saved)
                                a.movf_store(true, RBP, iXmmNativeSave[x], x);
                            return saved;
                        }
                        void restore_xmms(std::vector<int> const& aSaved)
                        {
                            for (auto x : aSaved)
                                a.movf_load(true, x, RBP, iXmmNativeSave[x]);
                        }
                        void plugin_call(value v, std::uint32_t aFunction, function_description const& aCallee)
                        {
                            auto const& i = f.values[v];
                            auto const& p = *aCallee.plugin;
                            // (its frames can be walked while it runs (see walk))
                            a.store(true, R15, ContextExitFrame, RBP);
                            a.store_imm(true, R15, ContextExitSite, static_cast<std::int32_t>(i.origin));
                            native_call(p.function, i.operands, v, false);
                            // (its result in rax (an integer) or xmm0: (it is stored for the error's helper))
                            bool const floatResult = p.result == NEOS_RT_F32 || p.result == NEOS_RT_F64;
                            if (floatResult)
                                a.movf_store(p.result == NEOS_RT_F64, RSP, ShadowSpace, 0);
                            else if (p.result != NEOS_RT_VOID)
                                a.store(true, RSP, ShadowSpace, RAX);
                            if (p.error != nullptr)
                            {
                                // (its error (if it may have one: its plugin's error pending flag)): the host's (a trap if it throws)
                                auto const error = a.new_label();
                                auto const resume = a.new_label();
                                if (p.errorPending != nullptr && iModule.objects != nullptr)
                                {
                                    a.mov_ri(RAX, reinterpret_cast<std::uint64_t>(p.errorPending), true);
                                    a.alu_mi(7, sizeof(long) == 8u, RAX, 0, 0);
                                    a.jcc(NE, error);
                                }
                                else
                                    a.jmp(error);
                                a.bind(resume);
                                a.begin_cold();
                                a.bind(error);
                                auto const saved = save_xmms(v);
                                a.mov_ri(RAX, reinterpret_cast<std::uint64_t>(p.error), true);
                                a.call_r(RAX);
                                a.test_rr(true, RAX, RAX);
                                auto const none = a.new_label();
                                a.jcc(E, none);
                                a.mov_rr(true, NativeIntegerArguments[3], RAX);
                                a.mov_rr(true, NativeIntegerArguments[0], R15);
                                a.mov_ri(NativeIntegerArguments[1], aFunction, false);
                                a.lea(NativeIntegerArguments[2], RSP, ShadowSpace);
                                a.mov_ri(RAX, reinterpret_cast<std::uint64_t>(&plugin_result), true);
                                a.call_r(RAX);
                                a.test_rr(false, RAX, RAX);
                                a.jcc(NE, iTrapLabels[TrapHostException]);
                                a.bind(none);
                                restore_xmms(saved);
                                a.jmp(resume);
                                a.end_cold();
                            }
                            if (i.type == ssa::Void || i.dead)
                                return;
                            if (is_float_type(i.type))
                            {
                                auto const r = target(v, 0);
                                a.movf_load(i.type == ssa::F64, r, RSP, ShadowSpace);
                                defined(v, r);
                            }
                            else
                            {
                                auto const r = target(v, RAX);
                                a.load(is_wide_type(i.type), r, RSP, ShadowSpace);
                                defined(v, r);
                            }
                        }
                        // --- memory
                        void memory(value v)
                        {
                            auto const& i = f.values[v];
                            auto const op = static_cast<std::uint8_t>(i.op);
                            bool const store = i.store();
                            struct access { std::uint8_t width; std::uint8_t type; std::uint8_t opcode0; std::uint8_t opcode1; bool w; int prefix; };
                            static access const sAccesses[] =
                            {
                                { 4, I32, 0x8B, 0, false, -1 }, { 8, I64, 0x8B, 0, true, -1 }, { 4, F32, 0x10, 0, false, 0xF3 }, { 8, F64, 0x10, 0, false, 0xF2 },
                                { 1, I32, 0x0F, 0xBE, false, -1 }, { 1, I32, 0x0F, 0xB6, false, -1 },
                                { 2, I32, 0x0F, 0xBF, false, -1 }, { 2, I32, 0x0F, 0xB7, false, -1 },
                                { 1, I64, 0x0F, 0xBE, true, -1 }, { 1, I64, 0x0F, 0xB6, false, -1 },
                                { 2, I64, 0x0F, 0xBF, true, -1 }, { 2, I64, 0x0F, 0xB7, false, -1 },
                                { 4, I64, 0x63, 0, true, -1 }, { 4, I64, 0x8B, 0, false, -1 },
                                { 4, I32, 0x89, 0, false, -1 }, { 8, I64, 0x89, 0, true, -1 }, { 4, F32, 0x11, 0, false, 0xF3 }, { 8, F64, 0x11, 0, false, 0xF2 },
                                { 1, I32, 0x88, 0, false, -1 }, { 2, I32, 0x89, 0, false, 0x66 },
                                { 1, I64, 0x88, 0, false, -1 }, { 2, I64, 0x89, 0, false, 0x66 }, { 4, I64, 0x89, 0, false, -1 }
                            };
                            auto const& x = sAccesses[op - 0x28u];
                            auto const memoryIndex = static_cast<std::uint32_t>(i.imm0 >> 32u);
                            auto const disp = static_cast<std::int32_t>(i.imm1);
                            // (the address: in a register (rdx if it is not in one); a constant address within a fixed memory: in the displacement)
                            int base = -1;
                            int index = -1;
                            std::int32_t offset = disp;
                            auto const address = i.operands[0];
                            bool const fixed = memoryIndex != 0u && external_memory(memoryIndex).fixed;
                            auto const scale = static_cast<unsigned>((i.flags & ssa::Scaled) >> ssa::ScaledShift);
                            bool const within = scale == 0u && fixed && constant(address) && bits(address) <= external_memory(memoryIndex).size &&
                                bits(address) + static_cast<std::uint64_t>(disp) + x.width <= external_memory(memoryIndex).size;
                            bool const unchecked = (i.flags & ssa::Unchecked) != 0u || within;
                            if (scale != 0u && (!fixed || !unchecked))
                                throw std::logic_error("neos::jit: scaled access");
                            if (within && fits_i32(static_cast<std::int64_t>(bits(address) + static_cast<std::uint64_t>(disp))))
                            {
                                // (within it: no check)
                                offset = static_cast<std::int32_t>(bits(address) + static_cast<std::uint64_t>(disp));
                                if (iMemoryBase >= 0 && memoryIndex == iMemoryBaseIndex)
                                    base = iMemoryBase;
                                else
                                {
                                    a.mov_ri(RCX, reinterpret_cast<std::uint64_t>(external_memory(memoryIndex).base), true);
                                    base = RCX;
                                }
                            }
                            else
                            {
                                index = gpr(address, RDX);
                                if (memoryIndex == 0u)
                                {
                                    if (index != RDX)
                                        a.mov_rr(false, RDX, index);    // (zero extend: a 32 bit address)
                                    else
                                        a.mov_rr(false, RDX, RDX);
                                    index = RDX;
                                    a.load(true, RCX, R15, ContextMemory);
                                    a.lea(RAX, RDX, disp + static_cast<std::int32_t>(x.width));
                                    a.alu_rm(7, true, RAX, RCX, MemorySize);
                                    a.jcc(A, iTrapLabels[TrapOutOfBounds]);
                                    a.load(true, RCX, RCX, MemoryBase);
                                    base = RCX;
                                }
                                else if (fixed)
                                {
                                    auto const& m = external_memory(memoryIndex);
                                    if (!unchecked)
                                    {
                                        // (the address at most size - (disp + width))
                                        auto const size = static_cast<std::uint64_t>(m.size);
                                        auto const end = static_cast<std::uint64_t>(static_cast<std::uint32_t>(disp)) + x.width;
                                        if (disp < 0 || size < end)
                                            a.jmp(iTrapLabels[TrapOutOfBounds]);
                                        else
                                        {
                                            auto const limit = size - end;
                                            if (limit <= 0x7FFFFFFFull)
                                                a.alu_ri(7, true, index, static_cast<std::int32_t>(limit));
                                            else
                                                a.alu_rip(7, true, index, a.constant(limit));
                                            a.jcc(A, iTrapLabels[TrapOutOfBounds]);
                                        }
                                    }
                                    if (iMemoryBase >= 0 && memoryIndex == iMemoryBaseIndex)
                                        base = iMemoryBase;
                                    else
                                    {
                                        a.mov_ri(RCX, reinterpret_cast<std::uint64_t>(m.base), true);
                                        base = RCX;
                                    }
                                }
                                else
                                {
                                    a.mov_ri(RCX, reinterpret_cast<std::uint64_t>(&external_memory(memoryIndex)), true);
                                    a.lea(RAX, index, disp + static_cast<std::int32_t>(x.width));
                                    a.alu_rm(7, true, RAX, RCX, MemorySize);
                                    a.jcc(A, iTrapLabels[TrapOutOfBounds]);
                                    a.load(true, RCX, RCX, MemoryBase);
                                    base = RCX;
                                }
                            }
                            auto const access_at = [&](std::initializer_list<std::uint8_t> aOpcode, bool aW, int aReg, int aPrefix, bool aByte)
                            {
                                if (index >= 0)
                                    a.op_bi(aOpcode, aW, aReg, base, index, offset, aPrefix, aByte, scale);
                                else
                                    a.op_rm(aOpcode, aW, aReg, base, offset, aPrefix);
                            };
                            if (store)
                            {
                                auto const value = i.operands[1];
                                if (is_float_type(x.type))
                                {
                                    auto const r = xmm(value, 0);
                                    access_at({ 0x0Fu, x.opcode0 }, false, r, x.prefix, false);
                                    return;
                                }
                                if (constant(value) && (x.width < 8u || fits_i32(static_cast<std::int64_t>(bits(value)))))
                                {
                                    // (a constant: an immediate)
                                    auto const b = bits(value);
                                    access_at({ x.width == 1u ? std::uint8_t{ 0xC6u } : std::uint8_t{ 0xC7u } }, x.w, 0, x.prefix, false);
                                    if (x.width == 1u)
                                        a.emit8(static_cast<std::uint8_t>(b));
                                    else if (x.width == 2u)
                                    {
                                        a.emit8(static_cast<std::uint8_t>(b));
                                        a.emit8(static_cast<std::uint8_t>(b >> 8u));
                                    }
                                    else
                                        a.emit32(static_cast<std::uint32_t>(b));
                                    return;
                                }
                                int r = gpr(value, RAX);
                                if (x.width == 1u && r >= 4 && r < 8)
                                {
                                    // (spl, bpl, sil and dil: through al)
                                    a.mov_rr(false, RAX, r);
                                    r = RAX;
                                }
                                access_at({ x.opcode0 }, x.w, r, x.prefix, false);
                                return;
                            }
                            if (is_float_type(x.type))
                            {
                                auto const r = target(v, 0);
                                access_at({ 0x0Fu, x.opcode0 }, false, r, x.prefix, false);
                                defined(v, r);
                                return;
                            }
                            auto const r = target(v, RAX);
                            if (x.opcode1 != 0u)
                                access_at({ x.opcode0, x.opcode1 }, x.w, r, x.prefix, false);
                            else
                                access_at({ x.opcode0 }, x.w, r, x.prefix, false);
                            defined(v, r);
                        }
                        // --- select
                        void select(value v)
                        {
                            auto const& i = f.values[v];
                            auto const x = i.operands[0];
                            auto const y = i.operands[1];
                            auto const c = gpr(i.operands[2], RCX);
                            if (is_float_type(i.type))
                            {
                                bool const d = i.type == ssa::F64;
                                auto const skip = a.new_label();
                                xmm_into(x, 0);
                                a.test_rr(false, c, c);
                                a.jcc(NE, skip);
                                xmm_into(y, 0);
                                a.bind(skip);
                                (void)d;
                                defined(v, 0);
                                return;
                            }
                            bool const w = is_wide_type(i.type);
                            a.test_rr(false, c, c);
                            gpr_into(x, RAX);   // (mov: the flags stay)
                            auto const o = operand_of(y, R11, false);
                            if (o.kind == operand::Register)
                                a.cmov_rr(E, w, RAX, o.reg);
                            else
                                a.cmov_rm(E, w, RAX, RBP, o.disp);
                            defined(v, RAX);
                        }
                        // --- numeric instructions
                        void numeric(value v)
                        {
                            auto const& i = f.values[v];
                            auto const op = static_cast<std::uint8_t>(i.op);
                            if (auto const* h = helper_of(op); h != nullptr && !rounding_inline(op))
                            {
                                bool const traps = helper_traps(op);
                                std::vector<value> arguments = i.operands;
                                // (truncations: the context after the value)
                                if (traps)
                                    arguments.push_back(ssa::None);
                                helper_call(h, arguments, v, traps);
                                return;
                            }
                            if (op == 0x45u || op == 0x50u)
                            {
                                // eqz
                                auto const x = gpr(i.operands[0], RAX);
                                a.test_rr(op == 0x50u, x, x);
                                condition(v, E);
                                return;
                            }
                            if ((op >= 0x46u && op <= 0x4Fu) || (op >= 0x51u && op <= 0x5Au))
                            {
                                static std::uint8_t const sCcs[] = { E, NE, L, B, G, A, LE, BE, GE, AE };
                                static std::uint8_t const sSwapped[] = { E, NE, G, A, L, B, GE, AE, LE, BE };
                                bool const w = op >= 0x51u;
                                auto first = i.operands[0];
                                auto second = i.operands[1];
                                auto cc = sCcs[op - (w ? 0x51u : 0x46u)];
                                if ((i.flags & ssa::Top16) != 0u)
                                {
                                    // (the first's top 16 bits and the constant's low 16 bits (see ssa::Top16): BMI2's rotate, else a shift)
                                    auto const x = gpr(first, RAX);
                                    if (iBmi2)
                                        a.rorx(RAX, x, 48u);
                                    else
                                    {
                                        if (x != RAX)
                                            a.mov_rr(true, RAX, x);
                                        a.shift_ri(5, true, RAX, 48u);
                                    }
                                    a.cmp16_ri(RAX, static_cast<std::uint16_t>(bits(second)));
                                    condition(v, cc);
                                    return;
                                }
                                if (constant(first) && !constant(second))
                                {
                                    // (a constant first: second, then the constant (the relation swapped))
                                    std::swap(first, second);
                                    cc = sSwapped[op - (w ? 0x51u : 0x46u)];
                                }
                                auto const x = gpr(first, RAX);
                                if (w && constant(second) && !fits_i32(static_cast<std::int64_t>(bits(second))))
                                    a.alu_rip(7, true, x, a.constant(bits(second)));    // (a 64 bit constant: from the constants (cmp x, [rip + c]))
                                else
                                    alu(7, w, x, operand_of(second, RCX));
                                condition(v, cc);
                                return;
                            }
                            if ((op >= 0x5Bu && op <= 0x60u) || (op >= 0x61u && op <= 0x66u))
                            {
                                bool const d = op >= 0x61u;
                                int const relation = op - (d ? 0x61u : 0x5Bu);   // 0 eq, 1 ne, 2 lt, 3 gt, 4 le, 5 ge
                                bool const swap = relation == 2 || relation == 4;
                                auto first = swap ? i.operands[1] : i.operands[0];
                                auto second = swap ? i.operands[0] : i.operands[1];
                                if (relation <= 1 && constant(first) && !constant(second))
                                    std::swap(first, second);   // (equality: the constant second)
                                auto const fx = xmm(first, 0);
                                if (constant(second))
                                    a.sse_rip(d ? 0x66 : -1, 0x2E, fx, a.constant(d ? bits(second) : (bits(second) & 0xFFFFFFFFull)));    // (ucomis x, [rip + c])
                                else
                                {
                                    auto const sx = xmm(second, 1);
                                    a.ucomis(d, fx, sx);
                                }
                                if (relation >= 2)
                                    condition(v, relation <= 3 ? A : AE);
                                else
                                    float_equality(v, relation == 0);
                                return;
                            }
                            bool const isInt32 = op >= 0x67u && op <= 0x78u;
                            bool const isInt64 = op >= 0x79u && op <= 0x8Au;
                            if (isInt32 || isInt64)
                            {
                                integer(v, op, isInt64);
                                return;
                            }
                            if ((op >= 0x8Bu && op <= 0x98u) || (op >= 0x99u && op <= 0xA6u))
                            {
                                floating(v, op);
                                return;
                            }
                            conversion(v, op);
                        }
                        // (a comparison's result: its condition (if the branch that follows uses it), else 0 or 1)
                        void condition(value v, std::uint8_t aCc)
                        {
                            if (fused(v))
                            {
                                iPending = pending_condition{ v, aCc };
                                return;
                            }
                            auto const r = target(v, RAX);
                            a.setcc(aCc, r);
                            a.movzx8(r, r);
                            defined(v, r);
                            // (the flags have its condition until code is emitted (see flags_of))
                            iFlags = flags_from{ v, aCc, a.events() };
                        }
                        // (a comparison whose value was just made (nothing emitted since: setcc, movzx and a store do not change the flags):
                        // its condition, in the flags)
                        struct flags_from
                        {
                            value v = ssa::None;
                            std::uint8_t cc = 0u;
                            std::uint64_t events = 0u;
                        };
                        flags_from iFlags;
                        std::optional<std::uint8_t> flags_of(value aValue) const
                        {
                            if (iFlags.v == aValue && iFlags.events == a.events())
                                return iFlags.cc;
                            return {};
                        }
                        void float_equality(value v, bool aEqual)
                        {
                            if (fused(v))
                            {
                                iPending = pending_condition{ v, aEqual ? E : NE, aEqual, !aEqual };
                                return;
                            }
                            auto const r = target(v, RAX);
                            if (aEqual)
                            {
                                a.setcc(E, r);
                                a.setcc(NP, RCX);
                                a.alu_rr(4, false, r, RCX);
                            }
                            else
                            {
                                a.setcc(NE, r);
                                a.setcc(P, RCX);
                                a.alu_rr(1, false, r, RCX);
                            }
                            a.movzx8(r, r);
                            defined(v, r);
                        }
                        void integer(value v, std::uint8_t aOp, bool aW)
                        {
                            auto const& i = f.values[v];
                            auto const base = aW ? 0x7Cu : 0x6Au;
                            if (aOp == (aW ? 0x79u : 0x67u) || aOp == (aW ? 0x7Au : 0x68u))
                            {
                                // clz / ctz: bsr / bsf (0: the width)
                                bool const clz = aOp == (aW ? 0x79u : 0x67u);
                                auto const x = gpr(i.operands[0], RAX);
                                auto const r = target(v, RAX);
                                auto const zero = a.new_label();
                                auto const done = a.new_label();
                                a.op_rr({ 0x0Fu, static_cast<std::uint8_t>(clz ? 0xBDu : 0xBCu) }, aW, RCX, x);
                                a.jcc(E, zero);
                                if (clz)
                                    a.alu_ri(6, aW, RCX, aW ? 63 : 31);
                                a.mov_rr(aW, r, RCX);
                                a.jmp(done);
                                a.bind(zero);
                                a.mov_ri(r, aW ? 64u : 32u, false);
                                a.bind(done);
                                defined(v, r);
                                return;
                            }
                            auto const k = aOp - base;     // (0 add, 1 sub, 2 mul, 3 div_s, 4 div_u, 5 rem_s, 6 rem_u, 7 and, 8 or, 9 xor, 10 shl, 11 shr_s, 12 shr_u, 13 rotl, 14 rotr)
                            auto const x = i.operands[0];
                            auto const y = i.operands[1];
                            if (k >= 3u && k <= 6u)
                            {
                                divide(v, aW, k == 3u || k == 5u, k >= 5u);
                                return;
                            }
                            if (k >= 10u)
                            {
                                static int const sDigits[] = { 4, 7, 5, 0, 1 };
                                auto const digit = sDigits[k - 10u];
                                auto const r = target(v, RAX);
                                if (constant(y))
                                {
                                    gpr_into(x, r);
                                    a.shift_ri(digit, aW, r, static_cast<std::uint8_t>(bits(y) & (aW ? 63u : 31u)));
                                }
                                else
                                {
                                    gpr_into(y, RCX);
                                    gpr_into(x, r);
                                    a.shift_cl(digit, aW, r);
                                }
                                defined(v, r);
                                return;
                            }
                            bool const commutative = k == 0u || k == 2u || k == 7u || k == 8u || k == 9u;
                            if (k == 7u && aW && constant(y) && bits(y) == 0xFFFFFFFFull)
                            {
                                // (x & 0xFFFFFFFF: its low 32 bits (zero extended))
                                auto const r = target(v, RAX);
                                if (in_register(x))
                                    a.mov_rr(false, r, where(x).reg);
                                else if (!constant(x) && where(x).kind == location::Slot)
                                    a.load(false, r, RBP, where(x).disp);   // (its low 32 bits: a 32 bit load zero extends)
                                else
                                {
                                    gpr_into(x, r);
                                    a.mov_rr(false, r, r);
                                }
                                defined(v, r);
                                return;
                            }
                            if (k == 0u && aW && constant(y) && fits_i32(static_cast<std::int64_t>(bits(y))) && in_register(x))
                            {
                                // (x + c: lea)
                                auto const r = target(v, RAX);
                                if (r != where(x).reg)
                                {
                                    a.lea(r, where(x).reg, static_cast<std::int32_t>(bits(y)));
                                    defined(v, r);
                                    return;
                                }
                            }
                            int r = target(v, RAX);
                            // (the result's register is y's: x in it would overwrite y)
                            if (in_register(y) && where(y).reg == r && !(in_register(x) && where(x).reg == r))
                            {
                                if (commutative)
                                {
                                    auto const o = operand_of(x, RCX);
                                    if (k == 2u)
                                        mul(aW, r, o);
                                    else
                                        alu(k == 0u ? 0 : k == 7u ? 4 : k == 8u ? 1 : 6, aW, r, o);
                                    defined(v, r);
                                    return;
                                }
                                r = RAX;
                            }
                            gpr_into(x, r);
                            auto const o = operand_of(y, RCX);
                            if (k == 2u)
                                mul(aW, r, o);
                            else
                                alu(k == 0u ? 0 : k == 1u ? 5 : k == 7u ? 4 : k == 8u ? 1 : 6, aW, r, o);
                            defined(v, r);
                        }
                        void mul(bool aW, int aDst, operand const& aOperand)
                        {
                            switch (aOperand.kind)
                            {
                            case operand::Immediate: a.imul_rri(aW, aDst, aDst, aOperand.imm); break;
                            case operand::Register: a.imul_rr(aW, aDst, aOperand.reg); break;
                            default: a.imul_rm(aW, aDst, RBP, aOperand.disp); break;
                            }
                        }
                        void divide(value v, bool aW, bool aSigned, bool aRemainder)
                        {
                            auto const& i = f.values[v];
                            gpr_into(i.operands[1], RCX);
                            gpr_into(i.operands[0], RAX);
                            a.test_rr(aW, RCX, RCX);
                            a.jcc(E, iTrapLabels[TrapDivideByZero]);
                            auto const done = a.new_label();
                            if (aSigned)
                            {
                                // (the minimum / -1: an overflow (its remainder: 0))
                                auto const normal = a.new_label();
                                a.alu_ri(7, aW, RCX, -1);
                                a.jcc(NE, normal);
                                if (aRemainder)
                                {
                                    a.alu_rr(6, false, RDX, RDX);
                                    a.jmp(done);
                                }
                                else
                                {
                                    if (aW)
                                    {
                                        a.mov_ri(RDX, 0x8000000000000000ull, true);
                                        a.alu_rr(7, true, RAX, RDX);
                                    }
                                    else
                                        a.alu_ri(7, false, RAX, std::numeric_limits<std::int32_t>::min());
                                    a.jcc(E, iTrapLabels[TrapIntegerOverflow]);
                                }
                                a.bind(normal);
                                a.cqo(aW);
                                a.f7(7, aW, RCX);
                            }
                            else
                            {
                                a.alu_rr(6, false, RDX, RDX);
                                a.f7(6, aW, RCX);
                            }
                            a.bind(done);
                            defined(v, aRemainder ? RDX : RAX);
                        }
                        void floating(value v, std::uint8_t aOp)
                        {
                            auto const& i = f.values[v];
                            bool const d = aOp >= 0x99u;
                            auto const k = aOp - (d ? 0x99u : 0x8Bu);  // (0 abs, 1 neg, 2 ceil, 3 floor, 4 trunc, 5 nearest, 6 sqrt, 7 add, 8 sub, 9 mul, 10 div, 11 min, 12 max, 13 copysign)
                            int const prefix = d ? 0xF2 : 0xF3;
                            auto r = target(v, 0);
                            if (k >= 7u && k <= 10u)
                            {
                                static std::uint8_t const sOps[] = { 0x58, 0x5C, 0x59, 0x5E };
                                auto const y = i.operands[1];
                                if (constant(y))
                                {
                                    // (a constant: the operand is the constant's (in the code's constants))
                                    xmm_into(i.operands[0], r);
                                    a.sse_rip(prefix, sOps[k - 7u], r, a.constant(d ? bits(y) : (bits(y) & 0xFFFFFFFFull)));
                                    defined(v, r);
                                    return;
                                }
                                if (in_register(y) && where(y).reg == r && !(in_register(i.operands[0]) && where(i.operands[0]).reg == r))
                                {
                                    if (k == 7u || k == 9u)
                                    {
                                        // (add, mul: commutative (a NaN's payload is either operand's, as wasm allows): into the register that
                                        // the second operand is in)
                                        auto const xx = xmm(i.operands[0], 1);
                                        a.sse_rr(prefix, sOps[k - 7u], false, r, xx);
                                        defined(v, r);
                                        return;
                                    }
                                    r = 0;
                                }
                                xmm_into(i.operands[0], r);
                                auto const yx = xmm(y, 1);
                                a.sse_rr(prefix, sOps[k - 7u], false, r, yx);
                                defined(v, r);
                                return;
                            }
                            if (k == 0u || k == 1u)
                            {
                                std::uint64_t const sign = d ? 0x8000000000000000ull : 0x80000000ull;
                                xmm_into(i.operands[0], r);
                                a.sse_rip(prefix, 0x10, 1, a.constant(k == 0u ? (d ? ~sign : (~sign & 0xFFFFFFFFull)) : sign));
                                a.sse_rr(d ? 0x66 : -1, k == 0u ? 0x54 : 0x57, false, r, 1);
                                defined(v, r);
                                return;
                            }
                            if (k == 6u)
                            {
                                auto const x = xmm(i.operands[0], 1);
                                a.sse_rr(prefix, 0x51, false, r, x);
                                defined(v, r);
                                return;
                            }
                            if (k >= 2u && k <= 5u)
                            {
                                // (SSE 4.1: round (ceil 2, floor 1, trunc 3, nearest 0))
                                static std::uint8_t const sModes[] = { 2, 1, 3, 0 };
                                auto const x = xmm(i.operands[0], 1);
                                a.round(d, r, x, sModes[k - 2u]);
                                defined(v, r);
                                return;
                            }
                            throw unsupported{ "float instruction" };
                        }
                        void conversion(value v, std::uint8_t aOp)
                        {
                            auto const& i = f.values[v];
                            auto const x = i.operands[0];
                            switch (aOp)
                            {
                            case 0xA7: // i32.wrap_i64
                            case 0xAD: // i64.extend_i32_u
                                {
                                    auto const r = target(v, RAX);
                                    auto const g = gpr(x, r);
                                    a.mov_rr(false, r, g);
                                    defined(v, r);
                                }
                                return;
                            case 0xAC: // i64.extend_i32_s
                                {
                                    auto const r = target(v, RAX);
                                    auto const g = gpr(x, r);
                                    a.movsxd(r, g);
                                    defined(v, r);
                                }
                                return;
                            case 0xC0:
                            case 0xC1:
                                {
                                    auto const r = target(v, RAX);
                                    auto const g = gpr(x, RAX);
                                    if (g != RAX)
                                        a.mov_rr(false, RAX, g);
                                    if (aOp == 0xC0)
                                        a.movsx8(r, RAX);
                                    else
                                        a.movsx16(r, RAX);
                                    defined(v, r);
                                }
                                return;
                            case 0xAA: // i32.trunc_f64_s
                            case 0xB0: // i64.trunc_f64_s
                                if ((f.values[v].flags & ssa::Unchecked) != 0u)
                                {
                                    // (its operand is within its range (see ssa::Unchecked): it cannot trap)
                                    auto const xr = xmm(x, 0);
                                    auto const r = target(v, RAX);
                                    a.sse_rr(0xF2, 0x2C, aOp == 0xB0, r, xr);
                                    defined(v, r);
                                    return;
                                }
                                {
                                    bool const wide = aOp == 0xB0;
                                    auto const xr = xmm(x, 0);
                                    auto const r = target(v, RAX);
                                    a.sse_rr(0xF2, 0x2C, wide, RCX, xr);
                                    auto const done = a.new_label();
                                    if (wide)
                                    {
                                        a.mov_ri(RDX, 0x8000000000000000ull, true);
                                        a.alu_rr(7, true, RCX, RDX);
                                    }
                                    else
                                        a.alu_ri(7, false, RCX, std::numeric_limits<std::int32_t>::min());
                                    a.jcc(NE, done);
                                    a.xorpd(1, 1);
                                    a.sse_rr(0xF2, 0x2A, wide, 1, RCX);
                                    a.ucomis(true, xr, 1);
                                    a.jcc(P, iTrapLabels[TrapInvalidConversion]);
                                    a.jcc(NE, iTrapLabels[TrapIntegerOverflow]);
                                    a.bind(done);
                                    a.mov_rr(wide, r, RCX);
                                    defined(v, r);
                                }
                                return;
                            case 0xB2: case 0xB3: case 0xB4: // f32.convert_i32_s/u, i64_s
                            case 0xB7: case 0xB8: case 0xB9: // f64.convert_i32_s/u, i64_s
                                {
                                    bool const d = aOp >= 0xB7;
                                    bool const wideSource = aOp == 0xB4 || aOp == 0xB9;
                                    bool const isSigned = aOp == 0xB2 || aOp == 0xB4 || aOp == 0xB7 || aOp == 0xB9;
                                    auto const g = gpr(x, RAX);
                                    bool w = wideSource;
                                    int source = g;
                                    if (!wideSource && !isSigned)
                                    {
                                        // (an unsigned i32: zero extended (a 32-bit load's register is), then converted as an i64)
                                        auto const producer = f.values[x].op;
                                        if (!(producer == 0x28u || producer == 0x2Du || producer == 0x2Fu) || constant(x))
                                        {
                                            a.mov_rr(false, RAX, g);
                                            source = RAX;
                                        }
                                        w = true;
                                    }
                                    auto const r = target(v, 0);
                                    a.xorpd(r, r);
                                    a.sse_rr(d ? 0xF2 : 0xF3, 0x2A, w, r, source);
                                    defined(v, r);
                                }
                                return;
                            case 0xB6: // f32.demote_f64
                            case 0xBB: // f64.promote_f32
                                {
                                    auto const xr = xmm(x, 1);
                                    auto const r = target(v, 0);
                                    a.sse_rr(aOp == 0xB6 ? 0xF2 : 0xF3, 0x5A, false, r, xr);
                                    defined(v, r);
                                }
                                return;
                            case 0xBC: // i32.reinterpret_f32
                            case 0xBD: // i64.reinterpret_f64
                                {
                                    auto const r = target(v, RAX);
                                    if (constant(x))
                                        a.mov_ri(r, bits(x), aOp == 0xBD);
                                    else if (where(x).kind == location::Slot)
                                        a.load(aOp == 0xBD, r, RBP, where(x).disp);
                                    else
                                        a.movq_gx(aOp == 0xBD, r, where(x).reg);
                                    defined(v, r);
                                }
                                return;
                            case 0xBE: // f32.reinterpret_i32
                            case 0xBF: // f64.reinterpret_i64
                                {
                                    auto const r = target(v, 0);
                                    if (!constant(x) && where(x).kind == location::Slot)
                                        a.movf_load(aOp == 0xBF, r, RBP, where(x).disp);
                                    else
                                        a.movq_xg(aOp == 0xBF, r, gpr(x, RAX));
                                    defined(v, r);
                                }
                                return;
                            default:
                                throw unsupported{ "conversion" };
                            }
                        }
                        void helper_call(void const* aFunction, std::vector<value> const& aArguments, value v, bool aCanTrap)
                        {
                            // (the context argument (ssa::None) is the last: as the helpers take it)
                            iArgumentRax.reset();
                            for (std::size_t p = 0u; p < aArguments.size(); ++p)
                            {
                                auto const disp = ShadowSpace + 8 * static_cast<std::int32_t>(p);
                                if (aArguments[p] == ssa::None)
                                    a.store(true, RSP, disp, R15);
                                else
                                    argument(aArguments[p], disp);
                            }
                            native_arguments(aArguments);
                            iOutgoing = std::max(iOutgoing, ShadowSpace + 8 * static_cast<std::int32_t>(aArguments.size() + 1u));
                            auto const saved = save_xmms(v);
                            a.mov_ri(RAX, reinterpret_cast<std::uint64_t>(aFunction), true);
                            a.call_r(RAX);
                            restore_xmms(saved);
                            if (aCanTrap)
                            {
                                a.alu_mi(7, false, R15, ContextTrap, 0);
                                a.jcc(NE, iTrapFromContext);
                            }
                            auto const type = f.values[v].type;
                            if (is_float_type(type))
                                defined(v, 0);
                            else
                                defined(v, RAX);
                        }
                    private:
                        assembler a;
                        module_description const& iModule;
                        compiled_module& iTier;
                        compiled_module::tier_version const* iVersion;
                        ssa::function& f;
                        bool const iSse41;
                        bool const iBmi2;          ///< (BMI2: rorx (see ssa::Top16))
                        std::size_t iTrapLabels[TrapCount] = {};
                        std::size_t iTrapFromContext = 0u;
                        std::size_t iEpilogue = 0u;
                        bytes iCode;
                        int iMemoryBase = -1;
                        std::uint32_t iMemoryBaseIndex = 0u;
                        std::vector<std::uint32_t> iPosition;
                        std::vector<std::uint32_t> iBlockStart;
                        std::vector<std::uint32_t> iBlockEnd;
                        std::vector<std::uint32_t> iCalls;
                        std::vector<std::uint32_t> iHotCalls;   ///< (the calls that are not in cold code)
                        std::vector<interval> iIntervals;
                        std::vector<location> iLocations;
                        std::vector<value> iSpills;
                        bool iUsedCalleeGprs[16] = {};
                        bool iUsedCalleeXmms[16] = {};
                        std::vector<int> iPushed;
                        std::int32_t iXmmSave[16] = {};
                        std::int32_t iXmmNativeSave[16] = {};
                        // (the code that the deopts that leave for the function's own baseline frame share (see deopt_exit): its label, and
                        // the frame it makes)
                        std::optional<std::size_t> iDeoptCommon;
                        compiled_module::deopt_frame const* iDeoptCommonFrame = nullptr;
                        std::int32_t iFrameBelowPushed = 0;
                        std::int32_t iOutgoing = 0;
                        std::vector<std::size_t> iBlockLabels;     ///< (each block's label (one in the order))
                        std::uint32_t iCurrentBlock = 0u;
                        std::uint32_t iNext = 0u;
                        value iNextInstruction = ssa::None;
                        pending_condition iPending;
                        std::vector<std::pair<value, std::size_t>> iDeoptExits;    ///< (the deopts, and their code's labels)
                        std::vector<bool> iDeferred;                ///< (see defer_and_branch)
                        std::unordered_map<std::uint32_t, std::vector<value>> iSavesAtCalls;   ///< (by the calls' positions: see saves_at_calls)
                        std::unordered_map<value, std::int32_t> iCallSaveSlots;
                        std::vector<bool> iHome;                                ///< (values that deopts read (from their homes))
                        std::vector<bool> iDeoptAtLocation;                     ///< (deopts that read their values where they are (see liveness))
                        std::unordered_map<value, std::int32_t> iHomeSlots;     ///< (values in registers: their homes)
                        bool iCallStackChecks = false;  ///< (the stack is checked before each call, not in the prologue (see emit_function))
                        value iForceFused = ssa::None;
                        std::vector<std::pair<std::size_t, std::size_t>> iCallSites;
                        std::unordered_map<std::uint32_t, std::uint32_t> iLiveXmmsAtCalls;
                    };

                    // (code's start within its pages (a multiple of 64 bytes, below a page): different for each allocation, so that the
                    // code that runs first (a function's entry: its blocks that run come first) is not at the same offset in its page as
                    // the others' (their cache lines would then compete for the same sets of the instruction cache))
                    std::size_t executable_offset()
                    {
                        static std::atomic<std::uint32_t> sNext = 0u;
                        return static_cast<std::size_t>((sNext.fetch_add(1u, std::memory_order_relaxed) * 37u) % 64u) * 64u;
                    }

                    void* allocate_executable(bytes const& aCode, std::size_t& aSize)
                    {
                        aSize = aCode.size();
                        auto const offset = executable_offset();
                        auto const total = aSize + offset;
#if defined(_WIN32)
                        void* memory = ::VirtualAlloc(nullptr, total, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                        if (memory == nullptr)
                            return nullptr;
                        auto* const code = static_cast<std::uint8_t*>(memory) + offset;
                        std::memcpy(code, aCode.data(), aCode.size());
                        DWORD old;
                        if (!::VirtualProtect(memory, total, PAGE_EXECUTE_READ, &old))
                        {
                            ::VirtualFree(memory, 0, MEM_RELEASE);
                            return nullptr;
                        }
                        ::FlushInstructionCache(::GetCurrentProcess(), memory, total);
                        return code;
#else
                        void* memory = ::mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                        if (memory == MAP_FAILED)
                            return nullptr;
                        auto* const code = static_cast<std::uint8_t*>(memory) + offset;
                        std::memcpy(code, aCode.data(), aCode.size());
                        if (::mprotect(memory, total, PROT_READ | PROT_EXEC) != 0)
                        {
                            ::munmap(memory, total);
                            return nullptr;
                        }
                        return code;
#endif
                    }

                    void free_executable(void* aMemory, std::size_t aSize)
                    {
                        // (the allocation's start: the page that the code is in (its offset is below a page))
                        auto const address = reinterpret_cast<std::uintptr_t>(aMemory);
                        auto const base = address & ~std::uintptr_t{ 4095u };
#if defined(_WIN32)
                        (void)aSize;
                        ::VirtualFree(reinterpret_cast<void*>(base), 0, MEM_RELEASE);
#else
                        ::munmap(reinterpret_cast<void*>(base), aSize + (address - base));
#endif
                    }

                    // --- the optimizing tier

                    // (aCount bytes at an address, if they can be read (e.g. not a page of a region that is reserved but not committed))
                    bool read_memory(void const* aAddress, void* aBuffer, std::size_t aCount)
                    {
#if defined(_WIN32)
                        SIZE_T read = 0u;
                        return ::ReadProcessMemory(::GetCurrentProcess(), aAddress, aBuffer, aCount, &read) && read == aCount;
#else
                        struct iovec local{ aBuffer, aCount };
                        struct iovec remote{ const_cast<void*>(aAddress), aCount };
                        return ::process_vm_readv(::getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(aCount);
#endif
                    }

                    // (code that runs is changed: aCount bytes at aAddress)
                    void patch_code(void* aAddress, void const* aBytes, std::size_t aCount)
                    {
#if defined(_WIN32)
                        DWORD old;
                        if (!::VirtualProtect(aAddress, aCount, PAGE_EXECUTE_READWRITE, &old))
                            throw std::runtime_error("neos::jit: cannot patch code");
                        std::memcpy(aAddress, aBytes, aCount);
                        ::VirtualProtect(aAddress, aCount, old, &old);
                        ::FlushInstructionCache(::GetCurrentProcess(), aAddress, aCount);
#else
                        auto const pageSize = static_cast<std::uintptr_t>(::sysconf(_SC_PAGESIZE));
                        auto const first = reinterpret_cast<std::uintptr_t>(aAddress) & ~(pageSize - 1u);
                        auto const last = (reinterpret_cast<std::uintptr_t>(aAddress) + aCount + pageSize - 1u) & ~(pageSize - 1u);
                        // (the code that is patched is not running (this thread runs the tier's helper; generated code is not shared by 
                        // threads that run at the same time))
                        if (::mprotect(reinterpret_cast<void*>(first), last - first, PROT_READ | PROT_WRITE) != 0)
                            throw std::runtime_error("neos::jit: cannot patch code");
                        std::memcpy(aAddress, aBytes, aCount);
                        if (::mprotect(reinterpret_cast<void*>(first), last - first, PROT_READ | PROT_EXEC) != 0)
                            std::abort();
#endif
                    }

                    constexpr std::int32_t NotAgain = std::numeric_limits<std::int32_t>::max();
                    constexpr std::uint32_t MaxRecompiles = 16u;        ///< (then without speculation)
                    constexpr std::int32_t WarmCalls = 64;              ///< (a function's calls before a call it has never made is cold (see callFlags))
                    constexpr std::int32_t RecompileCalls = 1000;       ///< (calls before a function whose guard failed is optimized again)
                    constexpr std::int32_t RecompileCallsAfterDeopts = 200; ///< (... whose code left often for its own (not a guard's))
                    constexpr std::uint32_t DeoptsBeforeReoptimizing = 16u; ///< (a deopt that is not a guard's: its code left for the baseline's)
                    constexpr std::uint32_t DeoptStrikes = 2u;              ///< (an inlined call whose deopts made the function optimized again this often:
                                                                            ///< its code calls rather than deopts (e.g. a cache's miss: optimized again, 
                                                                            ///< the code knows the cache's new entry; again: it calls))
                    constexpr std::uint64_t CallsPerSeldomDeopt = 1000u;    ///< (an optimized function's calls per deopt (at least) that do not make it
                                                                            ///< optimized again)

                    // (the optimizer, on any thread (an optimization's job runs it in the background, while the program runs): it reads 
                    // the module's code (which does not change), and the program's memory and globals (each word's value is one that it
                    // has had: speculations' guards check their values when the code runs; cached loads' values may be any that their 
                    // words have had))
                    optimizer::result run_optimizer(compiled_module& aModule, std::uint32_t aFunction, optimizer::options const& aOptions,
                        std::vector<compiled_module::tier_job::memory_read>* aReads = nullptr)
                    {
                        auto& m = aModule;
                        auto const inlinable = [&](std::uint32_t aCallee) { return m.description.functions[aCallee].inlinable; };
                        // (the program's memory is read safely (its pages may not be committed); a page that has been read once is read
                        // directly (here, a committed page does not become unreadable: a page is decommitted by madvise (it reads as 
                        // zeros); on Windows, a decommitted page cannot be read: always read safely))
#if !defined(_WIN32)
                        std::unordered_set<std::uintptr_t> readable;
#endif
                        auto const memoryWord = [&](std::uint32_t aMemory, std::uint64_t aAddress, std::uint32_t aWidth) -> std::optional<std::uint64_t>
                        {
                            auto const& x = external_memory(aMemory);
                            if (!x.fixed || aWidth > 8u || aAddress > x.size || x.size - aAddress < aWidth)
                                return {};
                            auto const* const address = x.base + aAddress;
                            std::uint64_t word = 0u;
                            auto const logged = [&](std::uint64_t aWord) -> std::optional<std::uint64_t>
                            {
                                if (aReads != nullptr)
                                    aReads->push_back({ aMemory, aWidth, aAddress, aWord });
                                return aWord;
                            };
#if !defined(_WIN32)
                            constexpr std::uintptr_t PageMask = ~std::uintptr_t{ 0xFFFu };
                            auto const first = reinterpret_cast<std::uintptr_t>(address) & PageMask;
                            auto const last = (reinterpret_cast<std::uintptr_t>(address) + aWidth - 1u) & PageMask;
                            if (readable.contains(first) && readable.contains(last))
                            {
                                std::memcpy(&word, address, aWidth);
                                return logged(word);
                            }
#endif
                            if (!read_memory(address, &word, aWidth))
                                return {};
#if !defined(_WIN32)
                            readable.insert(first);
                            readable.insert(last);
#endif
                            return logged(word);
                        };
                        return optimizer::optimize(m.description, aFunction, aOptions, inlinable, memoryWord);
                    }

                    // (a frame (of a function: its code's) that optimized code can leave for at a call (see optimizer::deopt::frames): one
                    // that the baseline made (where it is left for at the call, and where the call returns to); the baseline's frames and 
                    // deopt points do not change once its code is made)
                    bool deopt_frame_valid(compiled_module const& m, std::uint32_t aFunction, std::size_t aPosition, std::uint32_t aArguments, std::uint32_t aDepth,
                        std::uint32_t aLocals)
                    {
                        auto const* const point = deopt_point_at(m, aPosition);
                        auto const* const frame = deopt_frame_of(m, aFunction);
                        return point != nullptr && point->function == aFunction && point->arguments == aArguments &&
                            point->stack.size() == aDepth && point->returned != 0u && frame != nullptr && frame->locals.size() == aLocals;
                    }

                    // (whether an optimizer's result's deopts can leave for the baseline's frames)
                    bool deopts_valid(compiled_module const& m, std::uint32_t f, optimizer::result const& aResult)
                    {
                        for (auto const& x : aResult.deopts)
                        {
                            if (!x.frames.empty())
                            {
                                std::size_t values = x.arguments;
                                for (auto const& frame : x.frames)
                                {
                                    values += frame.locals.size() + frame.depth;
                                    if (!deopt_frame_valid(m, frame.function, frame.position, frame.arguments, frame.depth, static_cast<std::uint32_t>(frame.locals.size())))
                                        return false;
                                }
                                if (x.frames.front().function != f || values > MaxDeoptValues)
                                    return false;
                                continue;
                            }
                            auto const* const point = deopt_point_at(m, x.call);
                            auto const* const frame = deopt_frame_of(m, f);
                            if (point == nullptr || frame == nullptr || point->function != f || point->arguments != x.arguments ||
                                point->stack.size() != x.depth || frame->locals.size() != x.locals ||
                                std::size_t{ x.locals } + x.depth + x.arguments > MaxDeoptValues)
                                return false;
                        }
                        return true;
                    }

                    // (a function's blocks' ends (see compiled_module::blockEnds))
                    std::uint32_t const* block_ends_of(compiled_module const& m, std::uint32_t f)
                    {
                        auto& slot = m.blockEnds[f];
                        if (auto const* known = slot.load(std::memory_order_acquire))
                            return known->data();
                        auto made = std::make_unique<std::vector<std::uint32_t> const>(optimizer::block_ends(m.description, f));
                        std::vector<std::uint32_t> const* expected = nullptr;
                        if (slot.compare_exchange_strong(expected, made.get(), std::memory_order_acq_rel, std::memory_order_acquire))
                            return made.release()->data();
                        return expected->data();
                    }

                    // (an optimization's first part (here, on the program's thread): its job, with the optimizer's options)
                    std::shared_ptr<compiled_module::tier_job> prepare_optimization(compiled_module& aModule, std::uint32_t aFunction)
                    {
                        auto& m = aModule;
                        auto const f = aFunction;
                        auto job = std::make_shared<compiled_module::tier_job>();
                        job->module = &m;
                        job->function = f;
                        job->start = std::chrono::steady_clock::now();
                        auto& options = job->options;
                        options.noInline = m.noInline[f];
                        options.noDeopt = m.noDeopt[f];
                        options.speculation = m.speculation && m.recompiles[f] < MaxRecompiles;
                        options.inlining = m.inlining;
                        // (deopts: where the baseline's code has a frame to leave for (else none))
                        // (a function optimized again more than twice (its deopts were taken): without deopts (its slow paths call))
                        options.deopt = m.backend && m.deopt && deopt_frame_of(m, f) != nullptr;
                        bool const fromLoop = f < m.loopTriggered.size() && m.loopTriggered[f] != 0u;
                        if (options.deopt)
                        {
                            options.deoptFrame = [&m](std::uint32_t aFunction, std::size_t aPosition, std::uint32_t aArguments, std::uint32_t aDepth,
                                std::uint32_t aLocals)
                            {
                                return deopt_frame_valid(m, aFunction, aPosition, aArguments, aDepth, aLocals);
                            };
                            // (a call that has never been made, in a function that has been called often enough: optimized code leaves 
                            // there (see compiled_module::callFlags))
                            // (the call's flag and the functions' counters and states are the program's (the optimizer may run while it 
                            // runs): words read as they are (a call made, or a function optimized, since: its code calls, or deopts once))
                            // (an optimization that the function's loops started: none (its code after its loops has not run yet))
                            if (!fromLoop)
                                options.coldCall = [&m](std::size_t aPosition)
                                {
                                    auto const* const point = deopt_point_at(m, aPosition);
                                    if (point == nullptr || point->made == nullptr || *point->made != 0u)
                                        return false;
                                    auto const g = point->function;
                                    if (g >= m.counters.size())
                                        return false;
                                    if (m.tierState[g] != 0u || m.recompiles[g] != 0u)
                                        return true;
                                    return m.counters[g] <= m.tierCalls - std::min(m.tierCalls, WarmCalls);
                                };
                        }
                        options.maxDeoptValues = MaxDeoptValues;
                        options.blockEnds = [&m](std::uint32_t aFunction) { return block_ends_of(m, aFunction); };
                        if (m.usefulSpeculations.size() < m.description.functions.size())
                            m.usefulSpeculations.resize(m.description.functions.size());
                        if (options.speculation && m.usefulSpeculations[f])
                            job->hint = *m.usefulSpeculations[f];
                        if (m.parametersRead.empty())
                        {
                            // (from the module's own code (an optimization's description has its function's optimized code))
                            m.parametersRead.resize(m.description.functions.size());
                            for (std::uint32_t g = 0u; g < m.description.functions.size(); ++g)
                                m.parametersRead[g] = optimizer::parameters_read(m.description, g);
                        }
                        return job;
                    }

                    // (an optimization's optimizer (on any thread: see run_optimizer): whether it optimized the function (its result, and
                    // the job's version and description: the function's code the optimizer's))
                    bool rewrite_optimization(compiled_module::tier_job& aJob)
                    {
                        auto& m = *aJob.module;
                        auto const f = aJob.function;
                        auto& options = aJob.options;
                        auto& r = aJob.result;
                        options.usefulHint = aJob.hint ? &*aJob.hint : nullptr;
                        aJob.reads.clear();
                        r = run_optimizer(m, f, options, &aJob.reads);
                        if (!r.deopts.empty() && !deopts_valid(m, f, r))
                        {
                            if (m.verbose != 0u)
                                std::fprintf(stderr, "[tier2] f%u: no deopts (the baseline's frame does not match)\n", f);
                            options.deopt = false;
                            aJob.reads.clear();
                            r = run_optimizer(m, f, options, &aJob.reads);
                        }
                        options.usefulHint = nullptr;
                        aJob.hint.reset();
                        // (code that the optimizer did not change: optimized only by the optimizing code generator (its registers))
                        if (r.tokens.empty() || (r.inlined == 0u && r.folded == 0u && !m.backend))
                        {
                            if (m.verbose != 0u)
                                std::fprintf(stderr, "[tier2] f%u: not optimized\n", f);
                            return false;
                        }
                        if (!r.chains.empty())
                        {
                            // (its inline chains: the module's (their indices in its origins); none if there are too many (the outermost
                            // calls' positions instead: their frames only))
                            std::lock_guard<std::mutex> lock{ m.chainsMutex };
                            auto const base = m.inlineChains.size();
                            bool const room = base + r.chains.size() < optimizer::InlineChainToken;
                            for (auto& o : r.origins)
                                if ((o & optimizer::InlineChainToken) != 0u)
                                {
                                    auto const index = o & ~optimizer::InlineChainToken;
                                    o = room ? (optimizer::InlineChainToken | (base + index)) : r.chains[index].back();
                                }
                            if (room)
                                for (auto& c : r.chains)
                                    m.inlineChains.push_back(std::move(c));
                            r.chains.clear();
                        }
                        aJob.version = std::make_unique<compiled_module::tier_version>();
                        aJob.version->function = f;
                        aJob.version->sites = r.sites;
                        aJob.version->deopts = r.deopts;
                        aJob.version->deoptCounts.assign(r.deopts.size(), 0u);
                        aJob.phases[0] = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - aJob.start).count());
                        aJob.rewritten = true;
                        return true;
                    }

                    // (the locals that the function's own code reads after each of its optimized code's deopts' calls (the others are not
                    // the deopts'), and after each of their frames' (their functions' code after their calls); from the module's own code
                    // (which does not change): made where the optimized code is (in the background), not by the program's thread)
                    std::shared_ptr<optimizer::liveness const> liveness_of(compiled_module const& m, std::uint32_t f)
                    {
                        {
                            std::lock_guard<std::mutex> lock{ m.livenessMutex };
                            if (auto const known = m.liveness.find(f); known != m.liveness.end())
                                return known->second;
                        }
                        auto found = std::make_shared<optimizer::liveness const>(optimizer::locals_liveness(m.description, f));
                        std::lock_guard<std::mutex> lock{ m.livenessMutex };
                        return m.liveness.try_emplace(f, std::move(found)).first->second;
                    }

                    void deopts_live(compiled_module const& m, std::uint32_t f, optimizer::result& r)
                    {
                        if (!r.deopts.empty())
                        {
                            std::vector<std::size_t> positions;
                            for (auto const& x : r.deopts)
                                if (x.frames.empty())
                                    positions.push_back(x.call);
                            auto live = optimizer::live_locals(*liveness_of(m, f), positions);
                            for (std::size_t k = 0u, p = 0u; k < r.deopts.size(); ++k)
                                if (r.deopts[k].frames.empty())
                                    r.deopts[k].live = std::move(live[p++]);
                            // (and each frame's (its function's code after its call))
                            std::map<std::uint32_t, std::vector<std::pair<optimizer::deopt::frame*, std::size_t>>> framesOf;
                            for (auto& x : r.deopts)
                                for (auto& frame : x.frames)
                                    framesOf[frame.function].emplace_back(&frame, frame.position);
                            for (auto& [function, frames] : framesOf)
                            {
                                std::vector<std::size_t> framePositions;
                                for (auto const& e : frames)
                                    framePositions.push_back(e.second);
                                auto frameLive = optimizer::live_locals(*liveness_of(m, function), framePositions);
                                for (std::size_t k = 0u; k < frames.size(); ++k)
                                    frames[k].first->live = std::move(frameLive[k]);
                            }
                        }
                    }

                    // (an optimization's second part (on any thread: it reads only what does not change): its code (by the optimizing code
                    // generator; else none (it is made when it is installed)))
                    void generate_optimization(compiled_module::tier_job& aJob)
                    {
                        if (!aJob.module->backend)
                            return;
                        auto& r = aJob.result;
                        auto const f = aJob.function;
                        // (the module's description: the SSA builder takes the optimizer's code itself, and the rest reads only other functions'
                        // descriptions (and the function's parameters and results), which the optimizer does not change)
                        auto const& d = aJob.module->description;
                        try
                        {
                            auto const from = std::chrono::steady_clock::now();
                            deopts_live(*aJob.module, f, r);
                            std::optional<ssa::osr_entry> osr;
                            if (aJob.osrLoop != NoOsrLoop)
                            {
                                // (from the loop: the function's parameters and its own locals are the code's parameters)
                                osr.emplace();
                                osr->loop = static_cast<std::size_t>(aJob.osrLoop);
                                osr->parameters = *d.functions[f].parameters;
                                osr->parameters.insert(osr->parameters.end(), d.functions[f].locals->begin(), d.functions[f].locals->end());
                            }
                            auto function = ssa::build(d, f, r.tokens, r.locals, r.origins, r.deopts, osr ? &*osr : nullptr);
                            auto const built_at = std::chrono::steady_clock::now();
                            // (cached loads' words: read safely (this runs while the program runs: its pages' states may change))
                            auto const memoryWord = [&aJob](std::uint32_t aMemory, std::uint64_t aAddress, std::uint32_t aWidth) -> std::optional<std::uint64_t>
                            {
                                if (aMemory == 0u || aMemory > NEOS_RT_EXTERNAL_MEMORIES)
                                    return {};
                                auto const& x = external_memory(aMemory);
                                if (!x.fixed || aWidth > 8u || aAddress > x.size || x.size - aAddress < aWidth)
                                    return {};
                                std::uint64_t word = 0u;
                                if (!read_memory(x.base + aAddress, &word, aWidth))
                                    return {};
                                aJob.reads.push_back({ aMemory, aWidth, aAddress, word });
                                return word;
                            };
                            ssa::optimize(function, d, [](std::uint32_t aMemory) -> std::uint64_t
                            {
                                if (aMemory == 0u || aMemory > NEOS_RT_EXTERNAL_MEMORIES || !external_memory(aMemory).fixed)
                                    return 0u;
                                return external_memory(aMemory).size;
                            }, memoryWord);
                            auto const ssa_at = std::chrono::steady_clock::now();
                            optimizing_generator generator{ d, *aJob.module, aJob.version.get(), function };
                            generator.generate(aJob.callSites);
                            auto const generated_at = std::chrono::steady_clock::now();
                            auto const us = [](auto aFrom, auto aTo) { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(aTo - aFrom).count()); };
                            aJob.phases[1] = us(from, built_at);
                            aJob.phases[2] = us(built_at, ssa_at) + us(ssa_at, generated_at);
                            aJob.code = std::move(generator.code());
                            aJob.optimizing = true;
                        }
                        catch (ssa::unsupported const& e)
                        {
                            aJob.why = e.what();
                            aJob.callSites.clear();
                        }
                        catch (unsupported const& e)
                        {
                            aJob.why = e.what();
                            aJob.callSites.clear();
                        }
                        catch (...)
                        {
                            aJob.why = "error";
                            aJob.callSites.clear();
                        }
                    }

                    constexpr std::uint8_t MaxStaleJobs = 2u;   ///< (a function's optimizations made in the background that were not installed
                                                                ///< (see compiled_module::staleJobs): then made here)

                    // (whether the words that an optimization (made in the background) read have the values it read (here, on the program's 
                    // thread: they do not change while this runs; a page that has been read once is read directly))
                    bool reads_current(compiled_module::tier_job const& aJob)
                    {
                        std::unordered_set<std::uintptr_t> readable;
                        for (auto const& x : aJob.reads)
                        {
                            auto const& memory = external_memory(x.memory);
                            if (!memory.fixed || x.width > 8u || x.address > memory.size || memory.size - x.address < x.width)
                                return false;
                            auto const* const address = memory.base + x.address;
                            constexpr std::uintptr_t PageMask = ~std::uintptr_t{ 0xFFFu };
                            auto const first = reinterpret_cast<std::uintptr_t>(address) & PageMask;
                            auto const last = (reinterpret_cast<std::uintptr_t>(address) + x.width - 1u) & PageMask;
                            std::uint64_t word = 0u;
                            if (readable.contains(first) && readable.contains(last))
                                std::memcpy(&word, address, x.width);
                            else
                            {
                                if (!read_memory(address, &word, x.width))
                                    return false;
                                readable.insert(first);
                                readable.insert(last);
                            }
                            if (word != x.value)
                                return false;
                        }
                        return true;
                    }

                    // (an optimization's last part (here): its code installed (the function's calls go there))
                    void const* install_optimization(compiled_module& aModule, compiled_module::tier_job& aJob)
                    {
                        auto& m = aModule;
                        auto const f = aJob.function;
                        auto& r = aJob.result;
                        auto& version = aJob.version;
                        if (aJob.options.speculation)
                            m.usefulSpeculations[f] = std::move(r.useful);
                        if (!aJob.rewritten)
                        {
                            // (not optimized)
                            m.tierState[f] = 2u;
                            return nullptr;
                        }
                        if (aJob.background && !reads_current(aJob))
                        {
                            // (the memory that it read has changed: optimized again)
                            ++m.staleJobs[f];
                            m.counters[f] = 1;
                            m.loopCounters[f] = 1;
                            if (m.verbose != 0u)
                                std::fprintf(stderr, "[tier2] f%u: not installed (the memory it read has changed)\n", f);
                            return nullptr;
                        }
                        compiled_module::tier_chunk chunk;
                        chunk.callSites = std::move(aJob.callSites);
                        bytes code = std::move(aJob.code);
                        std::size_t entry = 0u;
                        bool const optimizing = aJob.optimizing;
                        if (!optimizing)
                        {
                            if (m.verbose != 0u && m.backend)
                                std::fprintf(stderr, "[tier2] f%u: baseline code generator (%s)\n", f, aJob.why.c_str());
                            if (!r.deopts.empty())
                            {
                                // (the baseline's code generator has no deopts: optimized again without them)
                                aJob.options.deopt = false;
                                r = run_optimizer(m, f, aJob.options);
                                if (r.tokens.empty())
                                {
                                    m.tierState[f] = 2u;
                                    return nullptr;
                                }
                                version->sites = r.sites;
                                version->deopts.clear();
                                version->deoptCounts.clear();
                            }
                            // (the baseline's code generator reads the function's code from the description: the optimizer's)
                            auto& d = aJob.description;
                            d = m.description;
                            d.tokens = &r.tokens;
                            d.functions[f].codeBegin = 0u;
                            d.functions[f].codeEnd = r.tokens.size();
                            d.functions[f].locals = &r.locals;
                            chunk.callSites.clear();
                            code_generator generator{ d, true };
                            generator.generate_function(m, f, r.origins, version.get(), chunk.callSites);
                            entry = generator.function_offset(f);
                            code = std::move(generator.code());
                        }
                        chunk.code = allocate_executable(code, chunk.size);
                        if (chunk.code == nullptr)
                        {
                            m.tierState[f] = 2u;
                            return nullptr;
                        }
                        auto* const optimized = static_cast<std::uint8_t*>(chunk.code) + entry;
                        m.chunks.push_back(std::move(chunk));
                        // (calls of it go there: its indirect table entry and its entry (a jump: jmp [rip]; its address))
                        auto* const baseline = static_cast<std::uint8_t*>(m.code) + m.functionOffsets[f];
                        std::memcpy(m.savedEntries[f].data(), baseline, m.savedEntries[f].size());
                        std::uint8_t jump[14] = { 0xFFu, 0x25u, 0u, 0u, 0u, 0u };
                        auto const target = reinterpret_cast<std::uint64_t>(optimized);
                        std::memcpy(jump + 6, &target, sizeof(target));
                        patch_code(baseline, jump, sizeof(jump));
                        m.indirect[f].target = target;
                        m.tierState[f] = 1u;
                        m.currentVersion[f] = version.get();
                        m.versions.push_back(std::move(version));
                        for (std::size_t i = 0u; i < 3u; ++i)
                            m.phaseMicroseconds[i] += aJob.phases[i];
                        if (m.verbose != 0u)
                        {
                            auto const us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - aJob.start).count();
                            m.compileMicroseconds += static_cast<std::uint64_t>(aJob.phases[0] + aJob.phases[1] + aJob.phases[2]);
                            std::fprintf(stderr, "[tier2] f%u%s: %zu inlined, %zu folded, %zu guards; %zu tokens (was %zu), %zu bytes; %lld us after (total %llu us compiling)\n", f, optimizing ? " (ssa)" : "", r.inlined, r.folded, r.guards,
                                r.tokens.size(), m.description.functions[f].codeEnd - m.description.functions[f].codeBegin, code.size(), static_cast<long long>(us),
                                static_cast<unsigned long long>(m.compileMicroseconds));
                            if (m.verbose >= 2u)
                            {
                                std::size_t frames = 0u;
                                for (auto const& x : r.deopts)
                                    frames += x.frames.empty() ? 0u : 1u;
                                std::fprintf(stderr, "[tier2] f%u: %zu deopts (%zu with frames)\n", f, r.deopts.size(), frames);
                            }
                            std::fprintf(stderr, "[tier2] phases (total): optimizer %llu, ssa build %llu, ssa optimize+generate %llu us\n",
                                (unsigned long long)m.phaseMicroseconds[0], (unsigned long long)m.phaseMicroseconds[1], (unsigned long long)m.phaseMicroseconds[2]);
                        }
                        return optimized;
                    }

                    // (the thread that makes optimized code (see compiled_module::background): jobs in the order they were made)
                    class tier_worker
                    {
                    public:
                        // (never destroyed: modules may be destroyed after static objects are (each waits for its own jobs))
                        static tier_worker& instance()
                        {
                            static tier_worker* const sWorker = new tier_worker{};
                            return *sWorker;
                        }
                    public:
                        void add(std::shared_ptr<compiled_module::tier_job> aJob)
                        {
                            {
                                std::lock_guard<std::mutex> lock{ iMutex };
                                if (!iStarted)
                                {
                                    // (a thread for each processor but the program's (at most 8): optimizations are made sooner (a program's
                                    // functions are optimized as it starts, while it runs their baseline code))
                                    auto const processors = std::thread::hardware_concurrency();
                                    auto const threads = std::clamp<unsigned>(processors > 1u ? processors - 1u : 1u, 1u, 8u);
                                    for (unsigned t = 0u; t < threads; ++t)
                                        std::thread{ [this]() { run(); } }.detach();
                                    iStarted = true;
                                }
                                iQueue.push_back(std::move(aJob));
                            }
                            iWork.notify_one();
                        }
                        // (a module is destroyed: its jobs are not made (it waits for the one being made))
                        void closing(compiled_module& aModule)
                        {
                            std::unique_lock<std::mutex> lock{ iMutex };
                            aModule.closing = true;
                            iQueue.erase(std::remove_if(iQueue.begin(), iQueue.end(), [&](auto const& j) { return j->module == &aModule; }), iQueue.end());
                            iIdle.wait(lock, [&]() { return aModule.jobsInFlight == 0u; });
                        }
                    private:
                        void run()
                        {
                            for (;;)
                            {
                                std::shared_ptr<compiled_module::tier_job> job;
                                {
                                    std::unique_lock<std::mutex> lock{ iMutex };
                                    iWork.wait(lock, [&]() { return !iQueue.empty(); });
                                    job = std::move(iQueue.front());
                                    iQueue.pop_front();
                                    if (job->module->closing)
                                        continue;
                                    ++job->module->jobsInFlight;
                                }
                                try
                                {
                                    if (rewrite_optimization(*job))
                                        generate_optimization(*job);
                                }
                                catch (...)
                                {
                                    job->rewritten = false;
                                }
                                {
                                    std::lock_guard<std::mutex> lock{ iMutex };
                                    --job->module->jobsInFlight;
                                    job->done.store(true, std::memory_order_release);
                                }
                                iIdle.notify_all();
                            }
                        }
                    private:
                        std::mutex iMutex;
                        std::condition_variable iWork;
                        std::condition_variable iIdle;
                        std::deque<std::shared_ptr<compiled_module::tier_job>> iQueue;
                        bool iStarted = false;
                    };

                    constexpr std::int32_t PollCalls = 100;     ///< (calls before a function whose optimization is being made looks again)

                    void const* optimize_function(compiled_module& aModule, std::uint32_t aFunction)
                    {
                        auto& m = aModule;
                        auto const f = aFunction;
                        if (m.tierState[f] == 3u)
                        {
                            // (being made in the background: installed if it has been made)
                            auto job = m.jobs[f];
                            if (!job || !job->done.load(std::memory_order_acquire))
                            {
                                m.counters[f] = PollCalls;
                                return nullptr;
                            }
                            m.jobs[f].reset();
                            m.tierState[f] = 0u;
                            return install_optimization(m, *job);
                        }
                        if (m.tierState[f] != 0u)
                            return nullptr;
                        auto job = prepare_optimization(m, f);
                        if (m.background && m.backend)
                        {
                            // (its optimizer and its code in the background (unless its optimizations made there were not installed: 
                            // here))
                            job->background = m.staleJobs[f] < MaxStaleJobs;
                            if (!job->background)
                            {
                                if (rewrite_optimization(*job))
                                    generate_optimization(*job);
                                return install_optimization(m, *job);
                            }
                            m.tierState[f] = 3u;
                            m.jobs[f] = job;
                            tier_worker::instance().add(job);
                            m.counters[f] = PollCalls;
                            return nullptr;
                        }
                        if (rewrite_optimization(*job))
                            generate_optimization(*job);
                        return install_optimization(m, *job);
                    }

                    void const* lazy_compile(context* aContext, std::uint32_t aFunction) noexcept
                    {
                        auto& m = *aContext->module;
                        try
                        {
                            code_generator::lazy_function compiled;
                            {
                                code_generator generator{ m.description, true };
                                generator.generate_lazy(m, aFunction, compiled);
                                compiled.frame.valid = compiled.frame.valid && m.tiering;
                                bytes& code = generator.code();
                                // (in the last region, after the code there (a new region if it has no room))
                                constexpr std::size_t RegionSize = std::size_t{ 1u } << 20u;
                                constexpr std::size_t Alignment = 64u;
                                if (m.lazyRegions.empty() || m.lazyRegions.back().size - m.lazyRegions.back().used < code.size() + Alignment)
                                {
                                    auto const size = std::max(RegionSize, (code.size() + Alignment + 4095u) & ~std::size_t{ 4095u });
                                    bytes empty(size, 0xCCu);
                                    compiled_module::tier_chunk chunk;
                                    chunk.code = allocate_executable(empty, chunk.size);
                                    if (chunk.code == nullptr)
                                        throw std::bad_alloc{};
                                    compiled_module::lazy_region region;
                                    region.code = static_cast<std::uint8_t*>(chunk.code);
                                    region.size = chunk.size;
                                    region.chunk = m.chunks.size();
                                    m.chunks.push_back(std::move(chunk));
                                    m.lazyRegions.push_back(region);
                                }
                                auto& region = m.lazyRegions.back();
                                auto const at = (region.used + Alignment - 1u) & ~(Alignment - 1u);
                                patch_code(region.code + at, code.data(), code.size());
                                region.used = at + code.size();
                                auto& sites = m.chunks[region.chunk].callSites;
                                for (auto const& site : compiled.callSites)
                                    sites.emplace_back(at + site.first, site.second);
                                {
                                    std::lock_guard<std::mutex> lock{ m.deoptMutex };
                                    for (auto& [position, point] : compiled.deoptPoints)
                                    {
                                        point.code = region.code;
                                        point.resume += at;
                                        if (point.returned != 0u)
                                            point.returned += at;
                                        m.deoptPoints[position] = std::move(point);
                                    }
                                    if (compiled.frame.valid && aFunction < m.deoptFrames.size())
                                        m.deoptFrames[aFunction] = std::move(compiled.frame);
                                }
                                compiled.entry += at;
                                compiled.entry = reinterpret_cast<std::uintptr_t>(region.code + compiled.entry);
                            }
                            // (its entry: a jump to its code (jmp [rip+2]; int3 int3; its address))
                            auto* const entry = static_cast<std::uint8_t*>(m.code) + m.functionOffsets[aFunction];
                            std::uint8_t jump[16] = { 0xFFu, 0x25u, 0x02u, 0u, 0u, 0u, 0xCCu, 0xCCu };
                            auto const target = static_cast<std::uint64_t>(compiled.entry);
                            std::memcpy(jump + 8, &target, sizeof(target));
                            patch_code(entry, jump, sizeof(jump));
                            if (m.indirect[aFunction].target == reinterpret_cast<std::uint64_t>(entry))
                                m.indirect[aFunction].target = target;
                            ++m.lazyCompiled;
                            return reinterpret_cast<void const*>(target);
                        }
                        catch (std::exception const& e)
                        {
                            m.exception = std::make_exception_ptr(std::runtime_error(std::string{ "neos::jit: function " } + std::to_string(aFunction) + " cannot be compiled (" + e.what() + ")"));
                        }
                        catch (...)
                        {
                            m.exception = std::current_exception();
                        }
                        return nullptr;
                    }

                    void const* tier_up(context* aContext, std::uint32_t aFunction) noexcept
                    {
                        auto& m = *aContext->module;
                        m.counters[aFunction] = NotAgain;   // (optimize_function sets it again if it is to look again)
                        try
                        {
                            return optimize_function(m, aFunction);
                        }
                        catch (...)
                        {
                            m.tierState[aFunction] = 2u;
                            return nullptr;
                        }
                    }

                    constexpr std::int32_t PollIterations = 1000;   ///< (a loop's iterations before a function whose optimization is being made
                                                                    ///< looks again)

                    constexpr std::uint32_t MaxOsrCompiles = 6u;    ///< (a loop's code that calls continue in (see osr_entry): made again at most)
                    constexpr std::uint32_t MaxOsrEntries = 64u;    ///< (... entered at most (a call that left it for the baseline's continues in
                                                                    ///< a frame of its own, within the one that entered it))

                    // (code that a call running an optimized function's baseline code continues in from a loop (see code_generator::
                    // loop_counter): optimized as the function is (here: the call runs the loop once it is made), from the loop (the 
                    // function's parameters and its own locals are its parameters: see ssa::osr_entry); null if it cannot be)
                    void const* osr_entry(compiled_module& aModule, std::uint32_t aFunction, std::uint64_t aLoop)
                    {
                        auto& m = aModule;
                        auto const f = aFunction;
                        auto& state = m.osrLoops[aLoop];
                        if (state.entry != nullptr)
                            return ++state.entries <= MaxOsrEntries ? state.entry : nullptr;
                        if (state.compiles >= MaxOsrCompiles || state.entries >= MaxOsrEntries)
                            return nullptr;
                        ++state.compiles;
                        auto job = prepare_optimization(m, f);
                        job->osrLoop = aLoop;
                        if (rewrite_optimization(*job))
                            generate_optimization(*job);
                        if (!job->optimizing)
                        {
                            // (not again)
                            state.compiles = MaxOsrCompiles;
                            if (m.verbose != 0u)
                                std::fprintf(stderr, "[tier2] f%u: no code from the loop at %llu (%s)\n", f, static_cast<unsigned long long>(aLoop), job->why.c_str());
                            return nullptr;
                        }
                        compiled_module::tier_chunk chunk;
                        chunk.callSites = std::move(job->callSites);
                        chunk.code = allocate_executable(job->code, chunk.size);
                        if (chunk.code == nullptr)
                            return nullptr;
                        state.entry = chunk.code;
                        m.chunks.push_back(std::move(chunk));
                        job->version->osrLoop = aLoop;
                        state.version = job->version.get();
                        m.versions.push_back(std::move(job->version));
                        ++state.entries;
                        if (m.verbose != 0u)
                            std::fprintf(stderr, "[tier2] f%u (osr at %llu): %zu inlined, %zu folded, %zu guards; %zu tokens, %zu bytes\n", f,
                                static_cast<unsigned long long>(aLoop), job->result.inlined, job->result.folded, job->result.guards, job->result.tokens.size(),
                                job->code.size());
                        return state.entry;
                    }

                    // (an optimized loop's code (see osr_entry) left for the baseline's (a deopt in the loop, a guard that failed): made
                    // again (with the function's latest options) when the call has run the loop often enough again)
                    void osr_left(compiled_module& aModule, compiled_module::tier_version const* aVersion)
                    {
                        auto& m = aModule;
                        if (auto const s = m.osrLoops.find(aVersion->osrLoop); s != m.osrLoops.end() && s->second.version == aVersion)
                        {
                            s->second.entry = nullptr;
                            s->second.version = nullptr;
                        }
                        m.loopCounters[aVersion->function] = m.loopIterations;
                    }

                    // (a function's loops have run often enough (see code_generator::loop_counter): it is optimized (its later calls run
                    // its optimized code); the call whose loop it is continues in optimized code from the loop (aLoop, unless NoOsrLoop), 
                    // once the function has been optimized (see osr_entry), else in its own code)
                    void const* tier_loop(context* aContext, std::uint32_t aFunction, std::uint64_t aLoop) noexcept
                    {
                        auto& m = *aContext->module;
                        auto const f = aFunction;
                        m.loopCounters[f] = NotAgain;
                        try
                        {
                            bool const optimized = m.tierState[f] == 1u;
                            if (m.tierState[f] == 0u)
                            {
                                m.loopTriggered[f] = 1u;
                                optimize_function(m, f);
                                m.loopTriggered[f] = 0u;
                            }
                            else if (m.tierState[f] == 3u)
                                optimize_function(m, f);
                            // (being made: the loop looks again (as do the function's calls))
                            if (m.tierState[f] == 3u)
                                m.loopCounters[f] = PollIterations;
                            if (aLoop != NoOsrLoop && m.osr && m.tierState[f] == 1u)
                            {
                                // (just optimized: later calls run its optimized code, so a loop that runs its baseline code again is a call's
                                // that has not ended (it continues in optimized code then))
                                if (!optimized)
                                {
                                    m.loopCounters[f] = PollIterations;
                                    return nullptr;
                                }
                                return osr_entry(m, f, aLoop);
                            }
                        }
                        catch (...)
                        {
                            m.loopTriggered[f] = 0u;
                            if (m.tierState[f] != 1u)
                                m.tierState[f] = 2u;
                        }
                        return nullptr;
                    }

                    void guard_failed(context* aContext, compiled_module::tier_version const* aVersion, std::uint32_t aSite) noexcept
                    {
                        auto& m = *aContext->module;
                        auto const f = aVersion->function;
                        try
                        {
                            // (an inlined call whose guards fail again is not inlined)
                            if (aSite < aVersion->sites.size())
                            {
                                auto const call = aVersion->sites[aSite].call;
                                if (++m.guardFailures[f][call] >= 2u)
                                    m.noInline[f].insert(call);
                            }
                            if (m.loopCounters[f] == NotAgain)
                                m.loopCounters[f] = m.loopIterations;
                            if (aVersion->osrLoop != NoOsrLoop)
                            {
                                osr_left(m, aVersion);
                                return;
                            }
                            if (m.currentVersion[f] != aVersion || m.tierState[f] != 1u)
                                return;
                            // (the function's own code again (its optimized code may be running: it stays), until it is optimized again (with 
                            // the values that the guards' words have then))
                            auto* const baseline = static_cast<std::uint8_t*>(m.code) + m.functionOffsets[f];
                            patch_code(baseline, m.savedEntries[f].data(), m.savedEntries[f].size());
                            m.indirect[f].target = reinterpret_cast<std::uint64_t>(baseline);
                            m.tierState[f] = 0u;
                            m.currentVersion[f] = nullptr;
                            ++m.recompiles[f];
                            m.counters[f] = RecompileCalls;
                            m.loopCounters[f] = m.loopIterations;
                            if (m.verbose != 0u)
                                std::fprintf(stderr, "[tier2] f%u: a guard failed (site %u)\n", f, aSite);
                        }
                        catch (...)
                        {
                        }
                    }

                    // (a position of a function's code that is within one of its loops)
                    bool in_loop(module_description const& aModule, std::uint32_t aFunction, std::size_t aPosition)
                    {
                        auto const& f = aModule.functions[aFunction];
                        auto const& tokens = *aModule.tokens;
                        if (aPosition < f.codeBegin || aPosition >= f.codeEnd)
                            return false;
                        std::vector<bool> loops;   // (the open blocks: whether each is a loop)
                        std::size_t open = 0u;      // (... and the loops among them)
                        for (std::size_t p = f.codeBegin; p < aPosition;)
                        {
                            auto const op = static_cast<std::uint8_t>(tokens[p]);
                            if (op == 0x02u || op == 0x03u || op == 0x04u)
                            {
                                loops.push_back(op == 0x03u);
                                open += op == 0x03u ? 1u : 0u;
                            }
                            else if (op == 0x0Bu && !loops.empty())
                            {
                                open -= loops.back() ? 1u : 0u;
                                loops.pop_back();
                            }
                            p += 1u + immediate_count(op, tokens, p);
                        }
                        return open != 0u;
                    }

                    void deopt_taken(context* aContext, compiled_module::tier_version const* aVersion, std::uint32_t aDeopt) noexcept
                    {
                        // (the optimized code left for the baseline's: a call that is left at again is not inlined; the function's own code
                        // again (as guard_failed))
                        auto& m = *aContext->module;
                        auto const f = aVersion->function;
                        try
                        {
                            // (a guard that failed (its speculation's value changed): the function is optimized again (with the value it has
                            // then); else (the inlined code would have called (e.g. a property cache's miss)) not until it has left there often)
                            if (aDeopt >= aVersion->deopts.size())
                                return;
                            auto const& d = aVersion->deopts[aDeopt];
                            // (the call continues in the function's own code: its loops count again (it may continue in optimized code
                            // from one, see tier_loop))
                            if (m.loopCounters[f] == NotAgain)
                                m.loopCounters[f] = m.loopIterations;
                            // (a deopt within a loop of the function's code: its call's remaining iterations are the baseline's (its code is
                            // not entered again until its next call): optimized again at once, the call that left made (not left at))
                            bool looping = false;
                            if (!d.guard)
                            {
                                // (found once for each position)
                                auto const position = d.frames.empty() ? d.call : d.frames.front().position;
                                auto [known, added] = m.inLoop.try_emplace(position, false);
                                if (added)
                                    known->second = in_loop(m.description, f, position);
                                looping = known->second;
                            }
                            if (!d.guard && !looping)
                            {
                                ++aVersion->deoptCounts[aDeopt];
                                if (++aVersion->deoptsTaken < DeoptsBeforeReoptimizing)
                                    return;
                                // (seldom (as a share of its calls since it was optimized, or since its deopts were last counted again):
                                // the code stays (a deopt then costs less than the calls would), its deopts counted again)
                                if (aVersion->calls / aVersion->deoptsTaken >= CallsPerSeldomDeopt)
                                {
                                    aVersion->deoptsTaken = 0u;
                                    aVersion->calls = 0u;
                                    std::fill(aVersion->deoptCounts.begin(), aVersion->deoptCounts.end(), 0u);
                                    return;
                                }
                            }
                            // (optimized again; the inlined code that left often (e.g. a cache's misses, an allocator's slow path), and the
                            // version's other such deopts that it has left at: a strike each (see DeoptStrikes: then it calls instead); a guard
                            // that fails again: not inlined)
                            std::set<std::size_t> struck;
                            if (!d.guard)
                                struck.insert(d.call);
                            else if (++m.guardFailures[f][d.call] >= 2u)
                                m.noInline[f].insert(d.call);
                            for (std::size_t k = 0u; k < aVersion->deopts.size(); ++k)
                                if (!aVersion->deopts[k].guard && aVersion->deoptCounts[k] >= 1u)
                                    struck.insert(aVersion->deopts[k].call);
                            if (looping)
                            {
                                ++aVersion->deoptCounts[aDeopt];
                                m.noDeopt[f].insert(d.call);
                            }
                            for (auto const call : struck)
                                if (++m.deoptStrikes[f][call] >= DeoptStrikes)
                                    m.noDeopt[f].insert(call);
                            if (aVersion->osrLoop != NoOsrLoop)
                            {
                                osr_left(m, aVersion);
                                if (m.verbose != 0u)
                                    std::fprintf(stderr, "[tier2] f%u (osr): deopt %u (call@%zu%s)\n", f, aDeopt, d.call, d.guard ? ", a guard" : "");
                                return;
                            }
                            if (m.currentVersion[f] != aVersion || m.tierState[f] != 1u)
                                return;
                            auto* const baseline = static_cast<std::uint8_t*>(m.code) + m.functionOffsets[f];
                            patch_code(baseline, m.savedEntries[f].data(), m.savedEntries[f].size());
                            m.indirect[f].target = reinterpret_cast<std::uint64_t>(baseline);
                            m.tierState[f] = 0u;
                            m.currentVersion[f] = nullptr;
                            ++m.recompiles[f];
                            // (a guard's: once its words' values have settled; else (calls that its code had not made, now made): soon)
                            m.counters[f] = d.guard ? RecompileCalls : RecompileCallsAfterDeopts;
                            m.loopCounters[f] = m.loopIterations;
                            if (m.verbose != 0u)
                                std::fprintf(stderr, "[tier2] f%u: deopt %u (call@%zu%s%s)\n", f, aDeopt, d.call, d.guard ? ", a guard" : "",
                                    d.frames.empty() ? "" : ", frames");
                        }
                        catch (...)
                        {
                        }
                    }

                    thread_local std::uint64_t tCoroutineStack = 0u;
#if defined(_WIN32)
                    thread_local std::uint32_t tFiberDepth = 0u;    // (calls running on the fiber that run made: further calls run on it)
#else
                    thread_local std::uint64_t tThreadStackLimit = 0u;  // (the thread's (found once: pthread_getattr_np is not cheap))
#endif

                    std::uint64_t stack_limit()
                    {
                        // generated code traps rather than overflow the native stack (headroom is left for host functions)
                        constexpr std::uint64_t Headroom = 256u * 1024u;
                        if (tCoroutineStack != 0u)
                            return tCoroutineStack + Headroom;
#if !defined(_WIN32)
                        if (tThreadStackLimit != 0u)
                            return tThreadStackLimit;
#endif
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
                            tThreadStackLimit = reinterpret_cast<std::uint64_t>(address) + Headroom;
                            return tThreadStackLimit;
                        }
                        char probe = 0;
                        return reinterpret_cast<std::uint64_t>(&probe) - 512u * 1024u;
#else
                        char probe = 0;
                        return reinterpret_cast<std::uint64_t>(&probe) - 512u * 1024u;
#endif
                    }
                }

                void set_coroutine_stack(std::uint64_t aLow)
                {
                    tCoroutineStack = aLow;
                }

                compiled_module::~compiled_module()
                {
                    if (tiering && background)
                        tier_worker::instance().closing(*this);
                    if (blockEnds != nullptr)
                        for (std::size_t f = 0u; f < description.functions.size(); ++f)
                            delete blockEnds[f].load();
                    if (code != nullptr)
                        free_executable(code, codeSize);
                    for (auto const& c : chunks)
                        free_executable(c.code, c.size);
                }

                namespace
                {
                    std::uint32_t run(compiled_module& aModule, context& aContext, void const* aFunction)
                    {
#if defined(_WIN32)
                        // generated code uses the native stack: on Windows (1MB thread stacks by default) it runs on a 
                        // fiber with a larger stack (one: a call that it makes (e.g. a runtime plugin's call of a function of 
                        // the program) runs on it too)
                        if (tFiberDepth != 0u)
                        {
                            aContext.stackLimit = stack_limit();
                            return aModule.trampoline(&aContext, aFunction);
                        }
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
                                ++tFiberDepth;
                                c.status = c.module->trampoline(c.ctx, c.function);
                                --tFiberDepth;
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
                    result->blockEnds = std::make_unique<std::atomic<std::vector<std::uint32_t> const*>[]>(aModule.functions.size());
                    result->host = std::move(aHost);
                    try
                    {
                        // (call_indirect's table: its types now, its addresses once the code is where it runs)
                        result->indirect.resize(result->description.functions.size());
                        for (std::size_t f = 0u; f < result->description.functions.size(); ++f)
                            if (!result->description.functions[f].imported)
                                result->indirect[f].type = result->description.functions[f].type;
                        // (the optimizing tier (NEOS_TIER2: 0 off; NEOS_TIER2_CALLS: the calls before a function is optimized; 0 without:
                        // NEOS_TIER2_SPECULATE speculation, NEOS_TIER2_INLINE inlining, NEOS_TIER2_SSA the optimizing code generator, 
                        // NEOS_TIER2_DEOPT deoptimization, NEOS_TIER2_BACKGROUND compiling in the background, NEOS_TIER2_OSR calls continuing in
                        // optimized code at loops; NEOS_TIER2_VERBOSE 1: what it does))
                        {
                            auto const setting = [](char const* aName) -> std::optional<long long>
                            {
                                if (auto const text = std::getenv(aName); text != nullptr && *text != '\0')
                                    return std::strtoll(text, nullptr, 10);
                                return {};
                            };
                            auto& m = *result;
                            m.tiering = setting("NEOS_TIER2").value_or(1) != 0;
                            m.speculation = setting("NEOS_TIER2_SPECULATE").value_or(1) != 0;
                            m.inlining = setting("NEOS_TIER2_INLINE").value_or(1) != 0;
                            m.backend = setting("NEOS_TIER2_SSA").value_or(1) != 0;
                            m.deopt = setting("NEOS_TIER2_DEOPT").value_or(1) != 0;
                            m.background = setting("NEOS_TIER2_BACKGROUND").value_or(1) != 0;
                            m.osr = m.backend && setting("NEOS_TIER2_OSR").value_or(1) != 0;
                            m.verbose = static_cast<std::uint32_t>(setting("NEOS_TIER2_VERBOSE").value_or(0));
                            auto const calls = static_cast<std::int32_t>(std::clamp<long long>(setting("NEOS_TIER2_CALLS").value_or(2000), 1, NotAgain));
                            auto const n = m.description.functions.size();
                            m.counters.assign(n, calls);
                            m.tierCalls = calls;
                            m.loopIterations = static_cast<std::int32_t>(std::clamp<long long>(setting("NEOS_TIER2_LOOPS").value_or(10000), 1, NotAgain));
                            m.loopCounters.assign(n, m.loopIterations);
                            m.loopTriggered.assign(n, 0u);
                            m.staleJobs.assign(n, 0u);
                            m.tierState.assign(n, 0u);
                            m.recompiles.assign(n, 0u);
                            m.noInline.resize(n);
                            m.noDeopt.resize(n);
                            m.guardFailures.resize(n);
                            m.deoptStrikes.resize(n);
                            m.currentVersion.assign(n, nullptr);
                            m.savedEntries.resize(n);
                            m.jobs.resize(n);
                        }
                        // (functions compiled when they are first called (NEOS_JIT_LAZY: 0, all now): those that are not called are not
                        // compiled (e.g. most of a large program's libraries); a module with an instruction that the code generator does not
                        // have is not compiled (as when all are compiled now: the interpreter runs it))
                        if (auto const text = std::getenv("NEOS_JIT_LAZY"); text == nullptr || *text == '\0' || std::strtoll(text, nullptr, 10) != 0)
                        {
                            result->lazy = true;
                            auto const& tokens = *result->description.tokens;
                            for (auto const& f : result->description.functions)
                                if (!f.imported)
                                    for (std::size_t p = f.codeBegin; p < f.codeEnd;)
                                    {
                                        auto const op = static_cast<std::uint8_t>(tokens[p]);
                                        if (!baseline_instruction(op))
                                            throw unsupported{ "unsupported instruction" };
                                        p += 1u + immediate_count(op, tokens, p);
                                    }
                        }
                        code_generator generator{ result->description };
                        if (result->lazy)
                            generator.generate_entries(*result);
                        else
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
                        for (std::size_t f = 0u; f < result->indirect.size(); ++f)
                            if (!result->description.functions[f].imported)
                                result->indirect[f].target = reinterpret_cast<std::uint64_t>(static_cast<std::uint8_t const*>(result->code) + result->functionOffsets[f]);
                    }
                    catch (unsupported const&)
                    {
                        return {};
                    }
                    return result;
                }

                std::vector<value> invoke(compiled_module& aModule, std::uint32_t aFunction, std::span<value const> aArguments, invocation* aInvocation)
                {
                    auto const& function = aModule.description.functions.at(aFunction);
                    if (function.imported)
                    {
                        std::vector<value> buffer{ aArguments.begin(), aArguments.end() };
                        buffer.resize(std::max(buffer.size(), function.results->size()));
                        aModule.host(aFunction, buffer.data());
                        buffer.resize(function.results->size());
                        return buffer;
                    }
                    std::vector<value> results(function.results->size());
                    invoke_into(aModule, aFunction, aArguments, results.data(), aInvocation);
                    return results;
                }

                std::size_t invoke_into(compiled_module& aModule, std::uint32_t aFunction, std::span<value const> aArguments, value* aResults, invocation* aInvocation)
                {
                    auto const& function = aModule.description.functions.at(aFunction);
                    if (function.imported)
                    {
                        auto const results = invoke(aModule, aFunction, aArguments, aInvocation);
                        std::copy(results.begin(), results.end(), aResults);
                        return results.size();
                    }
                    if (aArguments.size() != function.parameters->size())
                        throw std::runtime_error("wrong number of arguments");
                    // (its results are not zeroed: the function writes them before they are read)
                    context c;
                    std::memset(&c, 0, offsetof(context, results));
                    c.arguments = aArguments.data();
                    c.argumentCount = static_cast<std::uint32_t>(aArguments.size());
                    c.globals = aModule.description.globals->data();
                    c.linearMemory = aModule.description.linearMemory;
                    c.module = &aModule;
                    c.objects = aModule.description.objects;
                    thread_local std::unique_ptr<value[]> tDeoptBuffer;
                    if (!tDeoptBuffer)
                        tDeoptBuffer = std::make_unique<value[]>(MaxDeoptValues + 1u);    // (and the slot of a deopt's continuation: see deopt_exit)
                    c.deoptBuffer = tDeoptBuffer.get();
                    struct invoked
                    {
                        invocation* i;
                        ~invoked() { if (i != nullptr) i->context = nullptr; }
                    } const invoking{ aInvocation };
                    if (aInvocation != nullptr)
                        aInvocation->context = &c;
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
                    auto const count = function.results->size();
                    for (std::size_t i = 0u; i < count; ++i)
                    {
                        auto const t = (*function.results)[i];
                        aResults[i] = (t == I32 || t == F32) ? (c.results[i] & 0xFFFFFFFFull) : c.results[i];
                    }
                    return count;
                }

                void walk(compiled_module const& aModule, invocation const& aInvocation, std::function<bool(std::size_t aToken, std::uint64_t aId)> const& aVisit)
                {
                    // (generated functions' frames are a chain (rbp: the caller's rbp, then the return address); the chain ends at the 
                    // trampoline's call; a frame that is not one of them (not within the invocation's stack, or a return address that 
                    // is not a call's) ends the walk)
                    auto const* const c = static_cast<context const*>(aInvocation.context);
                    if (c == nullptr || c->exitFrame == 0u)
                        return;
                    auto const code = reinterpret_cast<std::uint64_t>(aModule.code);
                    auto const limit = c->savedStackPointer;
                    auto frame = c->exitFrame;
                    auto token = static_cast<std::size_t>(c->exitSite);
                    for (std::size_t depth = 0u; depth < 1000000u; ++depth)
                    {
                        if (((token & optimizer::InlineChainToken) != 0u && (token & ~std::size_t{ 0xFFFFFFFFu }) == 0u) || (token >> 31u) == 0x1FFFFFFFFull)
                        {
                            // (a call within inlined calls (of functions with frames of their own): the frames of the code and of the 
                            // calls that inlined it (innermost first: each the frame's address plus twice its depth in the inlined calls))
                            auto const index = static_cast<std::size_t>(token & 0x7FFFFFFFu);
                            std::vector<std::size_t> const* found = nullptr;
                            {
                                std::lock_guard<std::mutex> lock{ aModule.chainsMutex };
                                if (index < aModule.inlineChains.size())
                                    found = &aModule.inlineChains[index];
                            }
                            if (found == nullptr)
                                return;
                            auto const& chain = *found;
                            for (std::size_t k = 0u; k < chain.size(); ++k)
                                if (!aVisit(chain[k], frame + 2u * static_cast<std::uint64_t>(chain.size() - 1u - k)))
                                    return;
                        }
                        else if (!aVisit(token, frame))
                            return;
                        if (frame == 0u || (frame & 7u) != 0u || frame >= limit)
                            return;
                        auto const caller = *reinterpret_cast<std::uint64_t const*>(frame);
                        auto const returnAddress = *reinterpret_cast<std::uint64_t const*>(frame + 8u);
                        // (in the module's code, or in an optimized function's (a chunk))
                        auto const* sites = &aModule.callSites;
                        std::size_t offset = 0u;
                        if (returnAddress >= code && returnAddress - code < aModule.codeSize)
                        {
                            offset = static_cast<std::size_t>(returnAddress - code);
                            if (offset == aModule.trampolineReturn)
                                return;
                        }
                        else
                        {
                            sites = nullptr;
                            for (auto const& c : aModule.chunks)
                                if (returnAddress >= reinterpret_cast<std::uint64_t>(c.code) && returnAddress - reinterpret_cast<std::uint64_t>(c.code) < c.size)
                                {
                                    sites = &c.callSites;
                                    offset = static_cast<std::size_t>(returnAddress - reinterpret_cast<std::uint64_t>(c.code));
                                    break;
                                }
                            if (sites == nullptr)
                                return;
                        }
                        auto const site = std::lower_bound(sites->begin(), sites->end(), std::make_pair(offset, std::size_t{}));
                        if (site == sites->end() || site->first != offset || caller <= frame)
                            return;
                        token = site->second;
                        frame = caller;
                    }
                }

#else // not x86-64

                void set_coroutine_stack(std::uint64_t)
                {
                }

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

                std::vector<value> invoke(compiled_module&, std::uint32_t, std::span<value const>, invocation*)
                {
                    throw std::logic_error("neos::jit: not available");
                }

                std::size_t invoke_into(compiled_module&, std::uint32_t, std::span<value const>, value*, invocation*)
                {
                    throw std::logic_error("neos::jit: not available");
                }

                void walk(compiled_module const&, invocation const&, std::function<bool(std::size_t aToken, std::uint64_t aId)> const&)
                {
                }

#endif

                memory& external_memory(std::uint64_t aIndex)
                {
                    static memory sExternal[NEOS_RT_EXTERNAL_MEMORIES + 1u];   // ([0]: none (no size))
                    return sExternal[aIndex >= 1u && aIndex <= NEOS_RT_EXTERNAL_MEMORIES ? aIndex : 0u];
                }
            }
        }
    }
}
