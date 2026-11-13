/*
  vm.cpp

  Copyright (c) 2025 Leigh Johnston.  All Rights Reserved.

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
#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <sstream>
#include <cstring>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <set>
#include <charconv>
#include <cstdlib>
#include <iostream>
#include <span>
#include <boost/container/small_vector.hpp>
#include <neos/bytecode/bytecode.hpp>
#include <neos/bytecode/vm/vm.hpp>
#include <neos/bytecode/vm/jit.hpp>
#include <neos/bytecode/vm/runtime_plugins.hpp>
#define NEOS_RUNTIME_PLUGIN_HOST
#include <neos/runtime/plugin.h>
#include <neos/runtime/debug.h>
#include <neos/debug.hpp>
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
#include <ucontext.h>
#include <unistd.h>
#endif
#include <cctype>
#include <cstdio>
#include <list>
#include <mutex>
#include <thread>

// coroutines' switches (see host_coroutine_resume) on x86-64 System V (ELF): the registers that a call preserves, MXCSR and the x87
// control word, saved on the stack being left (its stack pointer stored in *aSave), then restored from the stack switched to (aTo) 
// (ucontext's swapcontext also saves and restores the signal mask: a system call each switch)
#if !defined(_WIN32) && defined(__x86_64__) && (defined(__linux__) || defined(__FreeBSD__))
#define NEOS_VM_SWITCH_CONTEXT
extern "C" void neos_vm_switch_context(void** aSave, void* aTo);
extern "C" void neos_vm_coroutine_trampoline();
asm(R"(
    .text
    .p2align 4
    .globl neos_vm_switch_context
    .hidden neos_vm_switch_context
    .type neos_vm_switch_context, @function
neos_vm_switch_context:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $8, %rsp
    stmxcsr (%rsp)
    fnstcw 4(%rsp)
    movq %rsp, (%rdi)
    movq %rsi, %rsp
    ldmxcsr (%rsp)
    fldcw 4(%rsp)
    addq $8, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
    .size neos_vm_switch_context, .-neos_vm_switch_context

    .p2align 4
    .globl neos_vm_coroutine_trampoline
    .hidden neos_vm_coroutine_trampoline
    .type neos_vm_coroutine_trampoline, @function
neos_vm_coroutine_trampoline:
    movq %r12, %rdi
    andq $-16, %rsp
    callq *%r13
    ud2
    .size neos_vm_coroutine_trampoline, .-neos_vm_coroutine_trampoline
)");
#endif

namespace neos
{
    namespace bytecode
    {
        namespace vm
        {
            namespace
            {
                struct trap : std::runtime_error { using std::runtime_error::runtime_error; };
                // (a trap that a runtime plugin requested (see neos_rt_host::trap): the calling thread's)
                thread_local std::optional<std::string> tTrapRequest;
                // (the debugger ended the program while the calling thread was in a plugin's call (reading its input (see debug_read_again),
                // or throwing an exception (see host_debug_thrown)): an error that the plugin then raises ends the program, not the language's
                // handling of it)
                thread_local bool tDebugTerminating = false;
                void debug_terminate();
                // (neos::exit(status): the program stops, with an exit status)
                struct exit_request { std::int32_t status; };

                // WebAssembly linear memory: its address space (up to 4 GiB) is reserved once and pages are committed as it 
                // grows, so it never moves and growing it neither copies it nor fills it (new pages are zero)
                class linear_memory
                {
                public:
                    linear_memory() = default;
                    linear_memory(linear_memory const&) = delete;
                    linear_memory& operator=(linear_memory const&) = delete;
                    ~linear_memory()
                    {
                        if (iBase == nullptr)
                            return;
#if defined(_WIN32)
                        ::VirtualFree(iBase, 0u, MEM_RELEASE);
#else
                        ::munmap(iBase, iReserved);
#endif
                    }
                public:
                    std::uint8_t* data() { return iBase; }
                    std::uint8_t const* data() const { return iBase; }
                    std::size_t size() const { return iSize; }
                    bool empty() const { return iSize == 0u; }
                    std::uint8_t& operator[](std::size_t aIndex) { return iBase[aIndex]; }
                    std::uint8_t const& operator[](std::size_t aIndex) const { return iBase[aIndex]; }
                    void resize(std::size_t aSize)
                    {
                        if (aSize > iCommitted)
                        {
                            reserve();
                            if (aSize > iReserved)
                                throw trap{ "out of memory" };
                            std::size_t constexpr Granularity = 65536u;
                            // (at least doubling, so growing in small steps makes few system calls)
                            auto const commit = std::min(iReserved, std::max((aSize + Granularity - 1u) / Granularity * Granularity, iCommitted * 2u));
#if defined(_WIN32)
                            if (::VirtualAlloc(iBase + iCommitted, commit - iCommitted, MEM_COMMIT, PAGE_READWRITE) == nullptr)
                                throw trap{ "out of memory" };
#else
                            if (::mprotect(iBase + iCommitted, commit - iCommitted, PROT_READ | PROT_WRITE) != 0)
                                throw trap{ "out of memory" };
#endif
                            iCommitted = commit;
                        }
                        else if (aSize < iSize)
                            std::memset(iBase + aSize, 0, iSize - aSize); // (as if shrunk and grown again)
                        iSize = aSize;
                    }
                private:
                    void reserve()
                    {
                        if (iBase != nullptr)
                            return;
                        // (the most a 32-bit address can reach; less if the address space is smaller or the reservation fails)
                        std::size_t size = sizeof(void*) >= 8u ? (std::size_t{ 1u } << 32u) : (std::size_t{ 1u } << 29u);
                        for (; size >= (std::size_t{ 1u } << 24u); size /= 2u)
                        {
#if defined(_WIN32)
                            auto const base = ::VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
                            if (base != nullptr)
#else
                            auto const base = ::mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                            if (base != MAP_FAILED)
#endif
                            {
                                iBase = static_cast<std::uint8_t*>(base);
                                iReserved = size;
                                return;
                            }
                        }
                        throw trap{ "out of memory" };
                    }
                private:
                    std::uint8_t* iBase = nullptr;
                    std::size_t iReserved = 0u;
                    std::size_t iCommitted = 0u;
                    std::size_t iSize = 0u;
                };

                using value = std::uint64_t;

                std::int32_t as_i32(value v) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(v)); }
                std::uint32_t as_u32(value v) { return static_cast<std::uint32_t>(v); }
                std::int64_t as_i64(value v) { return static_cast<std::int64_t>(v); }
                float as_f32(value v) { float f; auto const u = static_cast<std::uint32_t>(v); std::memcpy(&f, &u, sizeof(f)); return f; }
                double as_f64(value v) { double d; std::memcpy(&d, &v, sizeof(d)); return d; }
                value of_i32(std::int32_t i) { return static_cast<std::uint32_t>(i); }
                value of_u32(std::uint32_t i) { return i; }
                value of_i64(std::int64_t i) { return static_cast<std::uint64_t>(i); }
                value of_f32(float f) { std::uint32_t u; std::memcpy(&u, &f, sizeof(u)); return u; }
                value of_f64(double d) { value v; std::memcpy(&v, &d, sizeof(v)); return v; }
                value of_bool(bool b) { return b ? 1u : 0u; }

                class reader
                {
                public:
                    reader(std::span<std::uint8_t const> aBytes, std::size_t aPosition = 0u) : iBytes{ aBytes }, iPosition{ aPosition } {}
                public:
                    std::size_t position() const { return iPosition; }
                    void seek(std::size_t aPosition) { iPosition = aPosition; }
                    std::uint8_t byte()
                    {
                        if (iPosition >= iBytes.size())
                            throw exceptions::out_of_text();
                        return iBytes[iPosition++];
                    }
                    std::uint64_t u()
                    {
                        std::uint64_t result = 0u;
                        std::uint32_t shift = 0u;
                        std::uint8_t b;
                        do
                        {
                            b = byte();
                            result |= static_cast<std::uint64_t>(b & 0x7Fu) << shift;
                            shift += 7u;
                        } while ((b & 0x80u) != 0u);
                        return result;
                    }
                    std::int64_t s()
                    {
                        std::int64_t result = 0;
                        std::uint32_t shift = 0u;
                        std::uint8_t b;
                        do
                        {
                            b = byte();
                            result |= static_cast<std::int64_t>(b & 0x7Fu) << shift;
                            shift += 7u;
                        } while ((b & 0x80u) != 0u);
                        if (shift < 64u && (b & 0x40u) != 0u)
                            result |= -(static_cast<std::int64_t>(1) << shift);
                        return result;
                    }
                    template <typename T>
                    T raw()
                    {
                        T result;
                        if (iPosition + sizeof(T) > iBytes.size())
                            throw exceptions::out_of_text();
                        std::memcpy(&result, &iBytes[iPosition], sizeof(T));
                        iPosition += sizeof(T);
                        return result;
                    }
                    std::string name()
                    {
                        auto const length = u();
                        if (iPosition + length > iBytes.size())
                            throw exceptions::out_of_text();
                        std::string result{ reinterpret_cast<char const*>(&iBytes[iPosition]), length };
                        iPosition += length;
                        return result;
                    }
                private:
                    std::span<std::uint8_t const> iBytes;
                    std::size_t iPosition;
                };

                // function bodies are decoded once: each opcode and each immediate becomes one token so that executing an 
                // instruction does not decode LEB128 immediates (code positions are token indices)
                class decoded_reader
                {
                public:
                    decoded_reader(std::vector<std::uint64_t> const& aTokens) : iTokens{ aTokens }, iPosition{ 0u } {}
                public:
                    std::size_t position() const { return iPosition; }
                    void seek(std::size_t aPosition) { iPosition = aPosition; }
                    std::uint8_t byte() { return static_cast<std::uint8_t>(iTokens[iPosition++]); }
                    std::uint64_t u() { return iTokens[iPosition++]; }
                    std::int64_t s() { return static_cast<std::int64_t>(iTokens[iPosition++]); }
                    template <typename T>
                    T raw()
                    {
                        T result;
                        std::memcpy(&result, &iTokens[iPosition++], sizeof(T));
                        return result;
                    }
                private:
                    std::vector<std::uint64_t> const& iTokens;
                    std::size_t iPosition;
                };

                struct function_type
                {
                    std::vector<std::uint8_t> parameters;
                    std::vector<std::uint8_t> results;
                };

                struct function
                {
                    std::uint32_t type = 0u;
                    std::optional<std::string> import;
                    std::vector<std::uint8_t> locals;
                    std::size_t codeBegin = 0u; ///< (token index)
                    std::size_t codeEnd = 0u;   ///< (token index)
                };

                struct label
                {
                    std::size_t continuation;
                    std::size_t height;
                    std::size_t arity;
                    bool loop;
                };

                struct frame
                {
                    std::uint32_t function;
                    std::vector<value> locals;
                    std::size_t base;
                    std::vector<label> labels;
                    std::size_t pc;
                    std::size_t callSite = 0u;  ///< (the token of the call it is making)
                    std::uint64_t serial = 0u;  ///< (identifies it while it runs)
                };

                thread_local std::uint64_t tFrameSerial = 0u;

                class machine;

                // (linking (see bytecode::LinkedSection): a translation unit after the first run (the running program's 
                // neos::link_run); the linked value of a global of another translation unit (see neos_rt_host::link))
                void link_run(std::uint32_t aUnit);
                bool run_pending_unit();
                int host_link(char const* aName, char const* aLanguage, neos_rt_type* aType, neos_rt_value* aValue);

                // (a NEOS_RT_BIG_INTEGER's value (its hexadecimal digits: see neos_rt_link_type), rounded to nearest (ties to even))
                double big_integer_value(char const* aDigits)
                {
                    std::string_view const digits{ aDigits != nullptr ? aDigits : "" };
                    double result = 0.0;
                    if (std::from_chars(digits.data(), digits.data() + digits.size(), result, std::chars_format::hex).ec == std::errc::result_out_of_range)
                        result = !digits.empty() && digits[0] == '-' ? -HUGE_VAL : HUGE_VAL;
                    return result;
                }

                // the programs that a thread is running: each invocation of a program's function (by the thread, a runtime plugin 
                // or a coroutine), innermost first, so that its frames can be walked (see neos_rt_host::frames)
                struct activation
                {
                    activation* previous = nullptr;
                    machine const* program = nullptr;
                    jit::compiled_module const* native = nullptr;
                    std::vector<frame> const* frames = nullptr;     ///< (the interpreter's)
                    std::vector<value> const* stack = nullptr;      ///< (the interpreter's operand stack)
                    jit::invocation invocation;                     ///< (generated code's)
                    void* context = nullptr;                        ///< (its program's (see neos_rt_host::thread_context), once known)
                    std::size_t position = 0u;                      ///< (the interpreter's, when debugging: the token of the instruction it is running)
                };

                thread_local activation* tActivation = nullptr;

                // the debugger (see neos/debug.hpp): the session of the programs that the thread runs (none: not debugged); how the 
                // thread is stepping; whether it stops at the first statement it runs; whether the debugger is running code on it 
                // (e.g. evaluating a breakpoint's condition) or it is stopped (its statements do not stop); whether it has stopped for 
                // the error being thrown (so that the activations it unwinds do not stop for it again)
                thread_local debug::session* tDebugSession = nullptr;
                struct debug_step
                {
                    debug::action mode = debug::action::Continue;
                    std::size_t depth = 0u;
                };
                thread_local debug_step tDebugStep;
                thread_local bool tDebugEntry = false;
                thread_local bool tDebugBusy = false;
                thread_local bool tDebugErrorStopped = false;
                thread_local bool tDebugProgramThread = false;  ///< (the program's own thread (see thread::execute): the debugger ends the program 
                                                                ///< there; another thread that it ends stops with an error)
                inline constexpr std::uint8_t DebugStatement = 0x01u;
                inline constexpr std::uint8_t DebugBreakpoint = 0x02u;

                struct activation_scope
                {
                    activation a;
                    activation_scope(machine const* aProgram, jit::compiled_module const* aNative, std::vector<frame> const* aFrames,
                        std::vector<value> const* aStack = nullptr)
                    {
                        a.previous = tActivation;
                        a.program = aProgram;
                        a.native = aNative;
                        a.frames = aFrames;
                        a.stack = aStack;
                        tActivation = &a;
                    }
                    ~activation_scope() { tActivation = a.previous; }
                    activation_scope(activation_scope const&) = delete;
                    activation_scope& operator=(activation_scope const&) = delete;
                };

                class machine
                {
                public:
                    static constexpr std::size_t MaxCallDepth = 100000u;
                    static constexpr std::size_t StackLowFrames = 64u;  ///< (neos::stack_low: the interpreter's frames near MaxCallDepth)
                public:
                    machine(text const& aText, std::ostream& aOutput, std::istream& aInput, std::ostream& aErrorOutput, std::uint64_t& aInstructions, bool aRestricted,
                        compile_service const& aCompileService, std::vector<std::string> const& aArguments) :
                        iSerial{ ++sSerial }, iOutput{ aOutput }, iInput{ aInput }, iErrorOutput{ aErrorOutput }, iInstructions{ aInstructions }, iRestricted{ aRestricted },
                        iCompileService{ aCompileService }, iArguments{ aArguments }, iThread{ std::this_thread::get_id() }
                    {
                        // (the text is read only while it is parsed (its bodies' tokens, its data copied to the memory): not copied)
                        iModule = std::span<std::uint8_t const>{ reinterpret_cast<std::uint8_t const*>(aText.data()), aText.size() };
                        parse();
                        iModule = {};
                    }
                    ~machine()
                    {
                        release_objects();
                    }
                public:
                    // (objects that the program did not release: released while it can still be called (their finalizers can call 
                    // its functions), the last made first (those made before them, e.g. the functions and modules that finalizers use, 
                    // stay valid while they run); any that finalizers make, when it ends)
                    void release_objects()
                    {
                        for (std::size_t i = iObjects.size(); i-- > 1u;)
                            if (i < iObjects.size() && iObjects[i].count != 0u)
                            {
                                auto* const object = static_cast<neos_rt_object*>(iObjects[i].object);
                                iObjects[i] = {};
                                object->type->release(object);
                            }
                    }
                    std::optional<std::uint32_t> export_function(std::string const& aName) const
                    {
                        auto existing = iFunctionExports.find(aName);
                        if (existing != iFunctionExports.end())
                            return existing->second;
                        return std::nullopt;
                    }
                    // compile the module to native code (null if not supported: the interpreter is then used)
                    jit::compiled_module_ptr compile()
                    {
                        if (!jit::available())
                            return {};
                        jit::module_description module;
                        for (auto const& f : iFunctions)
                        {
                            jit::function_description d;
                            // (a function may be inlined: one without a frame of its own (that the compiler did not name: e.g. a language's 
                            // helper) has none where it is inlined; one with a frame, its frame (see the optimizer's inline chains))
                            if (auto const index = static_cast<std::size_t>(&f - iFunctions.data()); !f.import && (index >= iFrames.size() || !iFrames[index].frame))
                                d.inlinable = d.frameless = true;
                            else if (!f.import)
                                d.inlinable = true;
                            d.parameters = &iTypes.at(f.type).parameters;
                            d.results = &iTypes.at(f.type).results;
                            d.type = f.type;
                            d.locals = &f.locals;
                            d.imported = f.import.has_value();
                            if (d.imported)
                            {
                                d.checked = jit::checked_arithmetic(*f.import);
                                if (*f.import == "neos::object_retain")
                                    d.call = jit::import_call::ObjectRetain;
                                else if (*f.import == "neos::object_release")
                                    d.call = jit::import_call::ObjectRelease;
                                else if (*f.import == "neos::assume")
                                    d.call = jit::import_call::Assume;
                                else if (*f.import == "neos::stack_low")
                                    d.call = jit::import_call::StackLow;
                                else if (f.import->starts_with(std::string{ bytecode::LinkNamespace } + "::") || *f.import == "neos::link_run")
                                    ; // (the VM's (see link_call and link_run))
                                else if (!d.checked)
                                    d.plugin = plugin_function(*f.import);
                                if (d.plugin)
                                    d.call = jit::import_call::Plugin;
                            }
                            d.codeBegin = f.codeBegin;
                            d.codeEnd = f.codeEnd;
                            module.functions.push_back(d);
                        }
                        for (auto const& t : iTypes)
                            module.types.emplace_back(&t.parameters, &t.results);
                        module.tokens = &iTokens;
                        module.globals = &iGlobals;
                        iFixedGlobals.resize(iGlobals.size(), 0u);
                        module.fixedGlobals = &iFixedGlobals;
                        module.globalTypes = &iGlobalTypes;
                        module.threadGlobals = iAnyThreadGlobals ? &iThreadGlobals : nullptr;
                        module.linearMemory = &iMemoryView;
                        module.objects = &iObjectTable;
                        module.host = jit::host_interface{ this,
                            [](void* aHost, std::uint32_t aHandle) { return static_cast<machine*>(aHost)->object_pointer(aHandle); },
                            [](void* aHost, void* aObject) { return static_cast<machine*>(aHost)->object_handle(aObject); },
                            [](void* aHost, std::uint32_t aHandle) { static_cast<machine*>(aHost)->retain_object(aHandle); },
                            [](void* aHost, std::uint32_t aHandle) { static_cast<machine*>(aHost)->release_object(aHandle); },
                            [](void* aHost, std::uint32_t aFunction, char const* aError) { static_cast<machine*>(aHost)->plugin_error(aFunction, aError); },
                            [](void* aHost, std::uint32_t aAddress) 
                            { 
                                auto const pointer = static_cast<machine*>(aHost)->string_pointer(aAddress);
                                if (pointer == nullptr)
                                    throw trap{ "a string that is not NUL terminated" };
                                return pointer;
                            },
                            [](void* aHost, char const* aText) 
                            { 
                                return static_cast<machine*>(aHost)->allocate_string(aText != nullptr ? std::string_view{ aText } : std::string_view{}); 
                            },
                            [](void* aHost) { return static_cast<machine*>(aHost)->thread_globals(); } };
                        return jit::compile(module, [this](std::uint32_t aFunction, value* aBuffer)
                        {
                            call_import(aFunction, aBuffer);
                        });
                    }
                    // (a function of the program, run as native code)
                    std::vector<value> invoke(jit::compiled_module& aNative, std::uint32_t aFunction, std::span<value const> aArguments)
                    {
                        activation_scope activating{ this, &aNative, nullptr };
                        return jit::invoke(aNative, aFunction, aArguments, &activating.a.invocation);
                    }
                    std::size_t invoke_into(jit::compiled_module& aNative, std::uint32_t aFunction, std::span<value const> aArguments, value* aResults)
                    {
                        activation_scope activating{ this, &aNative, nullptr };
                        return jit::invoke_into(aNative, aFunction, aArguments, aResults, &activating.a.invocation);
                    }
                    // (the frames of an invocation of its functions, innermost first: aVisit(function, line, id) for each that has a 
                    // frame (see FramesSection), until it returns false (an interpreted frame's id is odd; native code's is its frame's 
                    // address, which is even))
                    void walk(activation const& aActivation, std::function<bool(std::uint32_t aFunction, std::uint32_t aLine, std::uint64_t aPosition, 
                        std::uint64_t aId)> const& aVisit) const
                    {
                        auto const visit = [&](std::size_t aToken, std::uint64_t aId) -> bool
                        {
                            // (the function, line and position of a call: looked up once (a thread's cache of the calls it walked last))
                            struct call_site
                            {
                                std::uint64_t program = 0u;
                                std::size_t token = 0u;
                                std::optional<std::uint32_t> function;
                                std::uint32_t line = 0u;
                                std::uint64_t position = 0u;
                            };
                            thread_local std::array<call_site, 256u> tCallSites;
                            auto& site = tCallSites[(aToken ^ (aToken >> 8u) ^ iSerial) % tCallSites.size()];
                            if (site.program != iSerial || site.token != aToken)
                            {
                                site.program = iSerial;
                                site.token = aToken;
                                site.function = function_at(aToken);
                                if (site.function && (*site.function >= iFrames.size() || !iFrames[*site.function].frame))
                                    site.function.reset();
                                site.line = site.function ? line_at(*site.function, aToken) : 0u;
                                auto const p = std::lower_bound(iPositions.begin(), iPositions.end(), std::make_pair(aToken, std::uint64_t{ 0u }));
                                site.position = p != iPositions.end() && p->first == aToken ? p->second : 0u;
                            }
                            if (!site.function)
                                return true;
                            return aVisit(*site.function, site.line, site.position, aId);
                        };
                        if (aActivation.frames != nullptr)
                        {
                            for (auto f = aActivation.frames->rbegin(); f != aActivation.frames->rend(); ++f)
                                if (!visit(f->callSite, (f->serial << 1u) | 1u))
                                    return;
                        }
                        else if (aActivation.native != nullptr)
                            jit::walk(*aActivation.native, aActivation.invocation, visit);
                    }
                    // (a function's frame: its name and source, and its first line; empty if it has none)
                    struct frame_info
                    {
                        bool frame = false;
                        std::string name;
                        std::uint32_t source = 0u;
                        std::uint32_t firstLine = 0u;
                    };
                    frame_info const& frame_of(std::uint32_t aFunction) const
                    {
                        return iFrames.at(aFunction);
                    }
                    std::size_t frame_count() const
                    {
                        return iFrames.size();
                    }
                    // (the position of the call at a token (see PositionMarker): 0 if none)
                    std::uint64_t position_at(std::size_t aToken) const
                    {
                        auto const p = std::lower_bound(iPositions.begin(), iPositions.end(), std::make_pair(aToken, std::uint64_t{ 0u }));
                        return p != iPositions.end() && p->first == aToken ? p->second : 0u;
                    }
                    std::string const& source_name(std::uint32_t aSource) const
                    {
                        static std::string const sNone;
                        return aSource < iSources.size() ? iSources[aSource] : sNone;
                    }
                    // (the function whose code a token is in)
                    std::optional<std::uint32_t> function_at(std::size_t aToken) const
                    {
                        auto const next = std::upper_bound(iCodeOrder.begin(), iCodeOrder.end(), aToken, 
                            [&](std::size_t aPosition, std::uint32_t aFunction) { return aPosition < iFunctions[aFunction].codeBegin; });
                        if (next == iCodeOrder.begin())
                            return {};
                        auto const f = *std::prev(next);
                        if (aToken >= iFunctions[f].codeEnd)
                            return {};
                        return f;
                    }
                    // (the line of the code of a function at a token: that of the line marker before it (see bytecode::LineMarker); else 
                    // the function's first line)
                    std::uint32_t line_at(std::uint32_t aFunction, std::size_t aToken) const
                    {
                        auto const& f = iFunctions[aFunction];
                        auto const next = std::upper_bound(iLines.begin(), iLines.end(), std::make_pair(aToken, std::numeric_limits<std::uint32_t>::max()));
                        if (next != iLines.begin() && std::prev(next)->first >= f.codeBegin)
                            return std::prev(next)->second;
                        return aFunction < iFrames.size() ? iFrames[aFunction].firstLine : 0u;
                    }
                    function_type const& type_of(std::uint32_t aFunction) const
                    {
                        return iTypes.at(iFunctions.at(aFunction).type);
                    }
                    // (the interpreter's: see iBlocks)
                    void block_tables()
                    {
                        std::call_once(iBlockTables, [this]()
                        {
                            iEnds.assign(iTokens.size(), 0u);
                            iElses.assign(iTokens.size(), 0u);
                            for (auto const& b : iBlocks)
                            {
                                iEnds[b.at] = b.end;
                                iElses[b.at] = b.elseAt;
                            }
                        });
                    }
                    std::vector<value> invoke(std::uint32_t aFunction, std::vector<value> aArguments)
                    {
                        if (iFunctions.at(aFunction).import)
                        {
                            auto const& type = type_of(aFunction);
                            aArguments.resize(std::max(aArguments.size(), type.results.size()));
                            call_import(aFunction, aArguments.data());
                            aArguments.resize(type.results.size());
                            return aArguments;
                        }
                        block_tables();
                        std::vector<value> stack;
                        std::vector<frame> frames;
                        activation_scope activating{ this, nullptr, &frames, &stack };
                        if (iDebug != nullptr)
                            debug_prepare();
                        decoded_reader code{ iTokens };
                        auto enter = [&](std::uint32_t aCallee, std::vector<value> aCallArguments)
                        {
                            if (frames.size() >= MaxCallDepth)
                                throw trap{ "call stack exhausted" };
                            auto const& callee = iFunctions.at(aCallee);
                            frames.push_back(frame{ aCallee, std::move(aCallArguments), stack.size(), {}, callee.codeBegin, callee.codeBegin, ++tFrameSerial });
                            frames.back().locals.resize(frames.back().locals.size() + callee.locals.size(), 0u);
                            code.seek(callee.codeBegin);
                        };
                        auto leave = [&]() -> bool // returns true when the outermost function returns
                        {
                            auto const& returning = frames.back();
                            auto const arity = type_of(returning.function).results.size();
                            std::copy(stack.end() - static_cast<std::ptrdiff_t>(arity), stack.end(), stack.begin() + static_cast<std::ptrdiff_t>(returning.base));
                            stack.resize(returning.base + arity);
                            frames.pop_back();
                            if (frames.empty())
                                return true;
                            code.seek(frames.back().pc);
                            return false;
                        };
                        enter(aFunction, std::move(aArguments));
                        auto pop = [&]() -> value
                        {
                            if (stack.size() <= frames.back().base)
                                throw trap{ "stack underflow" };
                            auto const v = stack.back();
                            stack.pop_back();
                            return v;
                        };
                        auto branch = [&](std::size_t aDepth) -> bool // returns true if the function returns
                        {
                            auto& labels = frames.back().labels;
                            if (aDepth >= labels.size())
                                return true;
                            auto const target = labels[labels.size() - 1u - aDepth];
                            auto const arity = target.loop ? 0u : target.arity;
                            if (arity == 0u)
                                stack.resize(target.height);
                            else
                            {
                                auto const result = stack.back(); // (block results are at most one value)
                                stack.resize(target.height);
                                stack.push_back(result);
                            }
                            labels.resize(labels.size() - aDepth - (target.loop ? 0u : 1u));
                            code.seek(target.continuation);
                            return false;
                        };
                        auto block_arity = [&](std::uint8_t aBlockType) -> std::size_t
                        {
                            return aBlockType == 0x40u ? 0u : 1u;
                        };
                        auto binary = [&](auto aOperation)
                        {
                            auto const rhs = pop();
                            auto const lhs = pop();
                            stack.push_back(aOperation(lhs, rhs));
                        };
                        auto unary = [&](auto aOperation)
                        {
                            stack.push_back(aOperation(pop()));
                        };
                        try
                        {
                            for (;;)
                            {
                                ++iInstructions;
                                auto& current = frames.back();
                                auto& f = iFunctions[current.function];
                                auto& labels = current.labels;
                                auto& locals = current.locals;
                                auto const opPosition = code.position();
                                if (opPosition >= f.codeEnd)
                                {
                                    if (leave())
                                        return stack;
                                    continue;
                                }
                                if (iDebug != nullptr) [[unlikely]]
                                {
                                    activating.a.position = opPosition;
                                    if (iDebugMarks[opPosition] != 0u)
                                        debug_statement(activating.a, opPosition);
                                }
                                auto const op = code.byte();
                                switch (op)
                                {
                                case 0x00: throw trap{ "unreachable executed" };
                                case 0x01: break;
                                case 0x02:
                                    {
                                        auto const bt = code.byte();
                                        labels.push_back({ iEnds[opPosition], stack.size(), block_arity(bt), false });
                                    }
                                    break;
                                case 0x03:
                                    {
                                        code.byte();
                                        labels.push_back({ code.position(), stack.size(), 0u, true });
                                    }
                                    break;
                                case 0x04:
                                    {
                                        auto const bt = code.byte();
                                        auto const condition = as_i32(pop());
                                        if (condition != 0)
                                            labels.push_back({ iEnds[opPosition], stack.size(), block_arity(bt), false });
                                        else if (iElses[opPosition] != 0u)
                                        {
                                            labels.push_back({ iEnds[opPosition], stack.size(), block_arity(bt), false });
                                            code.seek(iElses[opPosition]);
                                        }
                                        else
                                            code.seek(iEnds[opPosition]);
                                    }
                                    break;
                                case 0x05: // reached else at end of then-branch
                                    {
                                        auto const target = labels.back();
                                        labels.pop_back();
                                        code.seek(target.continuation);
                                    }
                                    break;
                                case 0x0B:
                                    if (labels.empty())
                                    {
                                        if (leave())
                                            return stack;
                                        break;
                                    }
                                    labels.pop_back();
                                    break;
                                case 0x0C:
                                    if (branch(static_cast<std::size_t>(code.u())) && leave())
                                        return stack;
                                    break;
                                case 0x0D:
                                    {
                                        auto const depth = static_cast<std::size_t>(code.u());
                                        if (as_i32(pop()) != 0 && branch(depth) && leave())
                                            return stack;
                                    }
                                    break;
                                case 0x0E:
                                    {
                                        // (br_table: the depths (the table's, then the default's) follow their count; the index chooses one)
                                        auto const count = static_cast<std::size_t>(code.u());
                                        auto const tableAt = code.position();
                                        auto const index = static_cast<std::uint32_t>(as_i32(pop()));
                                        code.seek(tableAt + std::min<std::size_t>(index, count));
                                        auto const depth = static_cast<std::size_t>(code.u());
                                        code.seek(tableAt + count + 1u);
                                        if (branch(depth) && leave())
                                            return stack;
                                    }
                                    break;
                                case 0x0F:
                                    if (leave())
                                        return stack;
                                    break;
                                case 0x10:
                                    {
                                        current.callSite = opPosition;
                                        auto const callee = static_cast<std::uint32_t>(code.u());
                                        auto const& calleeType = type_of(callee);
                                        if (stack.size() < current.base + calleeType.parameters.size())
                                            throw trap{ "stack underflow" };
                                        auto const parameterCount = calleeType.parameters.size();
                                        if (iFunctions.at(callee).import)
                                        {
                                            // (the arguments are replaced by the results)
                                            auto const resultCount = calleeType.results.size();
                                            auto const base = stack.size() - parameterCount;
                                            stack.resize(base + std::max(parameterCount, resultCount));
                                            call_import(callee, stack.data() + base);
                                            stack.resize(base + resultCount);
                                        }
                                        else
                                        {
                                            std::vector<value> arguments{ stack.end() - static_cast<std::ptrdiff_t>(parameterCount), stack.end() };
                                            stack.resize(stack.size() - parameterCount);
                                            current.pc = code.position();
                                            enter(callee, std::move(arguments)); // note: invalidates 'current'
                                        }
                                    }
                                    break;
                                case 0x11:
                                    {
                                        // (call_indirect: of a function of the program (the table is its functions) whose type is the 
                                        // instruction's; its index is the last operand)
                                        current.callSite = opPosition;
                                        auto const typeIndex = static_cast<std::uint32_t>(code.u());
                                        code.u();
                                        auto const callee = as_u32(pop());
                                        if (callee >= iFunctions.size() || iFunctions[callee].import || iFunctions[callee].type != typeIndex)
                                            throw trap{ "indirect call type mismatch" };
                                        auto const parameterCount = type_of(callee).parameters.size();
                                        if (stack.size() < current.base + parameterCount)
                                            throw trap{ "stack underflow" };
                                        std::vector<value> arguments{ stack.end() - static_cast<std::ptrdiff_t>(parameterCount), stack.end() };
                                        stack.resize(stack.size() - parameterCount);
                                        current.pc = code.position();
                                        enter(callee, std::move(arguments)); // note: invalidates 'current'
                                    }
                                    break;
                                case 0x1A: pop(); break;
                                case 0x1B:
                                    {
                                        auto const condition = as_i32(pop());
                                        auto const b = pop();
                                        auto const a = pop();
                                        stack.push_back(condition != 0 ? a : b);
                                    }
                                    break;
                                // memory (align, offset): [address + offset, address + offset + width) must be within memory
                                case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F:
                                case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35:
                                    {
                                        static std::uint8_t const sWidths[] = { 4, 8, 4, 8, 1, 1, 2, 2, 1, 1, 2, 2, 4, 4 };
                                        auto const memoryIndex = code.u() >> 32u;
                                        auto const offset = code.u();
                                        auto const width = sWidths[op - 0x28u];
                                        std::uint8_t const* source;
                                        if (memoryIndex == 0u)
                                        {
                                            auto const address = static_cast<std::uint64_t>(as_u32(pop())) + offset;
                                            if (address + width > iMemory.size())
                                                throw trap{ "out of bounds memory access" };
                                            source = &iMemory[address];
                                        }
                                        else
                                        {
                                            // (an external memory: a 64 bit address)
                                            auto const& external = jit::external_memory(memoryIndex);
                                            auto const address = pop() + offset;
                                            if (address + width > external.size || address + width < address)
                                                throw trap{ "out of bounds memory access" };
                                            source = external.base + address;
                                        }
                                        std::uint64_t bits = 0u;
                                        std::memcpy(&bits, source, width);
                                        switch (op)
                                        {
                                        case 0x2C: bits = of_i32(static_cast<std::int8_t>(bits)); break;
                                        case 0x2E: bits = of_i32(static_cast<std::int16_t>(bits)); break;
                                        case 0x30: bits = of_i64(static_cast<std::int8_t>(bits)); break;
                                        case 0x32: bits = of_i64(static_cast<std::int16_t>(bits)); break;
                                        case 0x34: bits = of_i64(static_cast<std::int32_t>(bits)); break;
                                        default: break;
                                        }
                                        stack.push_back(bits);
                                    }
                                    break;
                                case 0x36: case 0x37: case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E:
                                    {
                                        static std::uint8_t const sWidths[] = { 4, 8, 4, 8, 1, 2, 1, 2, 4 };
                                        auto const memoryIndex = code.u() >> 32u;
                                        auto const offset = code.u();
                                        auto const width = sWidths[op - 0x36u];
                                        auto const bits = pop();
                                        if (memoryIndex == 0u)
                                        {
                                            auto const address = static_cast<std::uint64_t>(as_u32(pop())) + offset;
                                            if (address + width > iMemory.size())
                                                throw trap{ "out of bounds memory access" };
                                            std::memcpy(&iMemory[address], &bits, width);
                                        }
                                        else
                                        {
                                            auto& external = jit::external_memory(memoryIndex);
                                            auto const address = pop() + offset;
                                            if (address + width > external.size || address + width < address)
                                                throw trap{ "out of bounds memory access" };
                                            std::memcpy(external.base + address, &bits, width);
                                        }
                                    }
                                    break;
                                case 0x20: stack.push_back(locals.at(code.u())); break;
                                case 0x21: locals.at(code.u()) = pop(); break;
                                case 0x22: locals.at(code.u()) = stack.back(); break;
                                case 0x23:
                                    {
                                        auto const g = code.u();
                                        stack.push_back(iThreadGlobals.at(g) != 0u ? thread_globals()[g] : iGlobals.at(g));
                                    }
                                    break;
                                case 0x24:
                                    {
                                        auto const g = code.u();
                                        (iThreadGlobals.at(g) != 0u ? thread_globals()[g] : iGlobals.at(g)) = pop();
                                    }
                                    break;
                                case 0x41: stack.push_back(of_i32(static_cast<std::int32_t>(code.s()))); break;
                                case 0x42: stack.push_back(of_i64(code.s())); break;
                                case 0x43: stack.push_back(of_f32(code.raw<float>())); break;
                                case 0x44: stack.push_back(of_f64(code.raw<double>())); break;
                                // i32 comparison
                                case 0x45: unary([](value a) { return of_bool(as_i32(a) == 0); }); break;
                                case 0x46: binary([](value a, value b) { return of_bool(as_i32(a) == as_i32(b)); }); break;
                                case 0x47: binary([](value a, value b) { return of_bool(as_i32(a) != as_i32(b)); }); break;
                                case 0x48: binary([](value a, value b) { return of_bool(as_i32(a) < as_i32(b)); }); break;
                                case 0x49: binary([](value a, value b) { return of_bool(as_u32(a) < as_u32(b)); }); break;
                                case 0x4A: binary([](value a, value b) { return of_bool(as_i32(a) > as_i32(b)); }); break;
                                case 0x4B: binary([](value a, value b) { return of_bool(as_u32(a) > as_u32(b)); }); break;
                                case 0x4C: binary([](value a, value b) { return of_bool(as_i32(a) <= as_i32(b)); }); break;
                                case 0x4D: binary([](value a, value b) { return of_bool(as_u32(a) <= as_u32(b)); }); break;
                                case 0x4E: binary([](value a, value b) { return of_bool(as_i32(a) >= as_i32(b)); }); break;
                                case 0x4F: binary([](value a, value b) { return of_bool(as_u32(a) >= as_u32(b)); }); break;
                                // i64 comparison
                                case 0x50: unary([](value a) { return of_bool(a == 0u); }); break;
                                case 0x51: binary([](value a, value b) { return of_bool(a == b); }); break;
                                case 0x52: binary([](value a, value b) { return of_bool(a != b); }); break;
                                case 0x53: binary([](value a, value b) { return of_bool(as_i64(a) < as_i64(b)); }); break;
                                case 0x54: binary([](value a, value b) { return of_bool(a < b); }); break;
                                case 0x55: binary([](value a, value b) { return of_bool(as_i64(a) > as_i64(b)); }); break;
                                case 0x56: binary([](value a, value b) { return of_bool(a > b); }); break;
                                case 0x57: binary([](value a, value b) { return of_bool(as_i64(a) <= as_i64(b)); }); break;
                                case 0x58: binary([](value a, value b) { return of_bool(a <= b); }); break;
                                case 0x59: binary([](value a, value b) { return of_bool(as_i64(a) >= as_i64(b)); }); break;
                                case 0x5A: binary([](value a, value b) { return of_bool(a >= b); }); break;
                                // f32/f64 comparison
                                case 0x5B: binary([](value a, value b) { return of_bool(as_f32(a) == as_f32(b)); }); break;
                                case 0x5C: binary([](value a, value b) { return of_bool(as_f32(a) != as_f32(b)); }); break;
                                case 0x5D: binary([](value a, value b) { return of_bool(as_f32(a) < as_f32(b)); }); break;
                                case 0x5E: binary([](value a, value b) { return of_bool(as_f32(a) > as_f32(b)); }); break;
                                case 0x5F: binary([](value a, value b) { return of_bool(as_f32(a) <= as_f32(b)); }); break;
                                case 0x60: binary([](value a, value b) { return of_bool(as_f32(a) >= as_f32(b)); }); break;
                                case 0x61: binary([](value a, value b) { return of_bool(as_f64(a) == as_f64(b)); }); break;
                                case 0x62: binary([](value a, value b) { return of_bool(as_f64(a) != as_f64(b)); }); break;
                                case 0x63: binary([](value a, value b) { return of_bool(as_f64(a) < as_f64(b)); }); break;
                                case 0x64: binary([](value a, value b) { return of_bool(as_f64(a) > as_f64(b)); }); break;
                                case 0x65: binary([](value a, value b) { return of_bool(as_f64(a) <= as_f64(b)); }); break;
                                case 0x66: binary([](value a, value b) { return of_bool(as_f64(a) >= as_f64(b)); }); break;
                                // i32 arithmetic
                                case 0x6A: binary([](value a, value b) { return of_u32(as_u32(a) + as_u32(b)); }); break;
                                case 0x6B: binary([](value a, value b) { return of_u32(as_u32(a) - as_u32(b)); }); break;
                                case 0x6C: binary([](value a, value b) { return of_u32(as_u32(a) * as_u32(b)); }); break;
                                case 0x6D: binary([](value a, value b) 
                                    { 
                                        if (as_i32(b) == 0) throw trap{ "integer divide by zero" };
                                        if (as_i32(a) == std::numeric_limits<std::int32_t>::min() && as_i32(b) == -1) throw trap{ "integer overflow" };
                                        return of_i32(as_i32(a) / as_i32(b)); 
                                    }); break;
                                case 0x6E: binary([](value a, value b) { if (as_u32(b) == 0u) throw trap{ "integer divide by zero" }; return of_u32(as_u32(a) / as_u32(b)); }); break;
                                case 0x6F: binary([](value a, value b) 
                                    { 
                                        if (as_i32(b) == 0) throw trap{ "integer divide by zero" };
                                        if (as_i32(b) == -1) return of_i32(0);
                                        return of_i32(as_i32(a) % as_i32(b)); 
                                    }); break;
                                case 0x70: binary([](value a, value b) { if (as_u32(b) == 0u) throw trap{ "integer divide by zero" }; return of_u32(as_u32(a) % as_u32(b)); }); break;
                                case 0x71: binary([](value a, value b) { return of_u32(as_u32(a) & as_u32(b)); }); break;
                                case 0x72: binary([](value a, value b) { return of_u32(as_u32(a) | as_u32(b)); }); break;
                                case 0x73: binary([](value a, value b) { return of_u32(as_u32(a) ^ as_u32(b)); }); break;
                                case 0x74: binary([](value a, value b) { return of_u32(as_u32(a) << (as_u32(b) & 31u)); }); break;
                                case 0x75: binary([](value a, value b) { return of_i32(as_i32(a) >> (as_u32(b) & 31u)); }); break;
                                case 0x76: binary([](value a, value b) { return of_u32(as_u32(a) >> (as_u32(b) & 31u)); }); break;
                                case 0x67: unary([](value a) { std::uint32_t n = 0u; while (n < 32u && (as_u32(a) & (0x80000000u >> n)) == 0u) ++n; return of_u32(n); }); break;
                                case 0x68: unary([](value a) { std::uint32_t n = 0u; while (n < 32u && (as_u32(a) & (1u << n)) == 0u) ++n; return of_u32(n); }); break;
                                case 0x69: unary([](value a) { std::uint32_t n = 0u; for (auto x = as_u32(a); x != 0u; x &= x - 1u) ++n; return of_u32(n); }); break;
                                case 0x77: binary([](value a, value b) { auto const s = as_u32(b) & 31u; return of_u32(s == 0u ? as_u32(a) : (as_u32(a) << s) | (as_u32(a) >> (32u - s))); }); break;
                                case 0x78: binary([](value a, value b) { auto const s = as_u32(b) & 31u; return of_u32(s == 0u ? as_u32(a) : (as_u32(a) >> s) | (as_u32(a) << (32u - s))); }); break;
                                case 0x79: unary([](value a) { std::uint64_t n = 0u; while (n < 64u && (a & (0x8000000000000000ull >> n)) == 0u) ++n; return value{ n }; }); break;
                                case 0x7A: unary([](value a) { std::uint64_t n = 0u; while (n < 64u && (a & (1ull << n)) == 0u) ++n; return value{ n }; }); break;
                                case 0x7B: unary([](value a) { std::uint64_t n = 0u; for (auto x = a; x != 0u; x &= x - 1u) ++n; return value{ n }; }); break;
                                case 0x89: binary([](value a, value b) { auto const s = b & 63u; return value{ s == 0u ? a : (a << s) | (a >> (64u - s)) }; }); break;
                                case 0x8A: binary([](value a, value b) { auto const s = b & 63u; return value{ s == 0u ? a : (a >> s) | (a << (64u - s)) }; }); break;
                                // i64 arithmetic
                                case 0x7C: binary([](value a, value b) { return a + b; }); break;
                                case 0x7D: binary([](value a, value b) { return a - b; }); break;
                                case 0x7E: binary([](value a, value b) { return a * b; }); break;
                                case 0x7F: binary([](value a, value b) 
                                    { 
                                        if (b == 0u) throw trap{ "integer divide by zero" };
                                        if (as_i64(a) == std::numeric_limits<std::int64_t>::min() && as_i64(b) == -1) throw trap{ "integer overflow" };
                                        return of_i64(as_i64(a) / as_i64(b)); 
                                    }); break;
                                case 0x80: binary([](value a, value b) { if (b == 0u) throw trap{ "integer divide by zero" }; return a / b; }); break;
                                case 0x81: binary([](value a, value b) 
                                    { 
                                        if (b == 0u) throw trap{ "integer divide by zero" };
                                        if (as_i64(b) == -1) return of_i64(0);
                                        return of_i64(as_i64(a) % as_i64(b)); 
                                    }); break;
                                case 0x82: binary([](value a, value b) { if (b == 0u) throw trap{ "integer divide by zero" }; return a % b; }); break;
                                case 0x83: binary([](value a, value b) { return a & b; }); break;
                                case 0x84: binary([](value a, value b) { return a | b; }); break;
                                case 0x85: binary([](value a, value b) { return a ^ b; }); break;
                                case 0x86: binary([](value a, value b) { return a << (b & 63u); }); break;
                                case 0x87: binary([](value a, value b) { return of_i64(as_i64(a) >> (b & 63u)); }); break;
                                case 0x88: binary([](value a, value b) { return a >> (b & 63u); }); break;
                                // f32 arithmetic
                                case 0x8B: unary([](value a) { return of_f32(std::fabs(as_f32(a))); }); break;
                                case 0x8C: unary([](value a) { return of_f32(-as_f32(a)); }); break;
                                case 0x8D: unary([](value a) { return of_f32(std::ceil(as_f32(a))); }); break;
                                case 0x8E: unary([](value a) { return of_f32(std::floor(as_f32(a))); }); break;
                                case 0x8F: unary([](value a) { return of_f32(std::trunc(as_f32(a))); }); break;
                                case 0x90: unary([](value a) { return of_f32(std::nearbyint(as_f32(a))); }); break;
                                case 0x91: unary([](value a) { return of_f32(std::sqrt(as_f32(a))); }); break;
                                case 0x92: binary([](value a, value b) { return of_f32(as_f32(a) + as_f32(b)); }); break;
                                case 0x93: binary([](value a, value b) { return of_f32(as_f32(a) - as_f32(b)); }); break;
                                case 0x94: binary([](value a, value b) { return of_f32(as_f32(a) * as_f32(b)); }); break;
                                case 0x95: binary([](value a, value b) { return of_f32(as_f32(a) / as_f32(b)); }); break;
                                case 0x96: binary([](value a, value b) { return of_f32(wasm_min(as_f32(a), as_f32(b))); }); break;
                                case 0x97: binary([](value a, value b) { return of_f32(wasm_max(as_f32(a), as_f32(b))); }); break;
                                case 0x98: binary([](value a, value b) { return of_f32(std::copysign(as_f32(a), as_f32(b))); }); break;
                                // f64 arithmetic
                                case 0x99: unary([](value a) { return of_f64(std::fabs(as_f64(a))); }); break;
                                case 0x9A: unary([](value a) { return of_f64(-as_f64(a)); }); break;
                                case 0x9B: unary([](value a) { return of_f64(std::ceil(as_f64(a))); }); break;
                                case 0x9C: unary([](value a) { return of_f64(std::floor(as_f64(a))); }); break;
                                case 0x9D: unary([](value a) { return of_f64(std::trunc(as_f64(a))); }); break;
                                case 0x9E: unary([](value a) { return of_f64(std::nearbyint(as_f64(a))); }); break;
                                case 0x9F: unary([](value a) { return of_f64(std::sqrt(as_f64(a))); }); break;
                                case 0xA0: binary([](value a, value b) { return of_f64(as_f64(a) + as_f64(b)); }); break;
                                case 0xA1: binary([](value a, value b) { return of_f64(as_f64(a) - as_f64(b)); }); break;
                                case 0xA2: binary([](value a, value b) { return of_f64(as_f64(a) * as_f64(b)); }); break;
                                case 0xA3: binary([](value a, value b) { return of_f64(as_f64(a) / as_f64(b)); }); break;
                                case 0xA4: binary([](value a, value b) { return of_f64(wasm_min(as_f64(a), as_f64(b))); }); break;
                                case 0xA5: binary([](value a, value b) { return of_f64(wasm_max(as_f64(a), as_f64(b))); }); break;
                                case 0xA6: binary([](value a, value b) { return of_f64(std::copysign(as_f64(a), as_f64(b))); }); break;
                                // conversions
                                case 0xA7: unary([](value a) { return of_u32(static_cast<std::uint32_t>(a)); }); break;
                                case 0xA8: unary([](value a) { return of_i32(truncate<std::int32_t>(as_f32(a))); }); break;
                                case 0xA9: unary([](value a) { return of_u32(truncate<std::uint32_t>(as_f32(a))); }); break;
                                case 0xAA: unary([](value a) { return of_i32(truncate<std::int32_t>(as_f64(a))); }); break;
                                case 0xAB: unary([](value a) { return of_u32(truncate<std::uint32_t>(as_f64(a))); }); break;
                                case 0xAC: unary([](value a) { return of_i64(as_i32(a)); }); break;
                                case 0xAD: unary([](value a) { return static_cast<value>(as_u32(a)); }); break;
                                case 0xAE: unary([](value a) { return of_i64(truncate<std::int64_t>(as_f32(a))); }); break;
                                case 0xAF: unary([](value a) { return static_cast<value>(truncate<std::uint64_t>(as_f32(a))); }); break;
                                case 0xB0: unary([](value a) { return of_i64(truncate<std::int64_t>(as_f64(a))); }); break;
                                case 0xB1: unary([](value a) { return static_cast<value>(truncate<std::uint64_t>(as_f64(a))); }); break;
                                case 0xB2: unary([](value a) { return of_f32(static_cast<float>(as_i32(a))); }); break;
                                case 0xB3: unary([](value a) { return of_f32(static_cast<float>(as_u32(a))); }); break;
                                case 0xB4: unary([](value a) { return of_f32(static_cast<float>(as_i64(a))); }); break;
                                case 0xB5: unary([](value a) { return of_f32(static_cast<float>(a)); }); break;
                                case 0xB6: unary([](value a) { return of_f32(static_cast<float>(as_f64(a))); }); break;
                                case 0xB7: unary([](value a) { return of_f64(static_cast<double>(as_i32(a))); }); break;
                                case 0xB8: unary([](value a) { return of_f64(static_cast<double>(as_u32(a))); }); break;
                                case 0xB9: unary([](value a) { return of_f64(static_cast<double>(as_i64(a))); }); break;
                                case 0xBA: unary([](value a) { return of_f64(static_cast<double>(a)); }); break;
                                case 0xBB: unary([](value a) { return of_f64(static_cast<double>(as_f32(a))); }); break;
                                case 0xBC: case 0xBD: case 0xBE: case 0xBF: break; // (reinterpretations: values are their bits)
                                case 0xC0: unary([](value a) { return of_i32(static_cast<std::int8_t>(as_u32(a))); }); break;
                                case 0xC1: unary([](value a) { return of_i32(static_cast<std::int16_t>(as_u32(a))); }); break;
                                case 0xC2: unary([](value a) { return of_i64(static_cast<std::int8_t>(a)); }); break;
                                case 0xC3: unary([](value a) { return of_i64(static_cast<std::int16_t>(a)); }); break;
                                case 0xC4: unary([](value a) { return of_i64(static_cast<std::int32_t>(a)); }); break;
                                // (prefix 0xFC: the saturating conversions; memory.copy and memory.fill)
                                case 0xFC:
                                    switch (auto const sub = code.u())
                                    {
                                    case 0x00: unary([](value a) { return of_i32(saturate<std::int32_t>(as_f32(a))); }); break;
                                    case 0x01: unary([](value a) { return of_u32(saturate<std::uint32_t>(as_f32(a))); }); break;
                                    case 0x02: unary([](value a) { return of_i32(saturate<std::int32_t>(as_f64(a))); }); break;
                                    case 0x03: unary([](value a) { return of_u32(saturate<std::uint32_t>(as_f64(a))); }); break;
                                    case 0x04: unary([](value a) { return of_i64(saturate<std::int64_t>(as_f32(a))); }); break;
                                    case 0x05: unary([](value a) { return static_cast<value>(saturate<std::uint64_t>(as_f32(a))); }); break;
                                    case 0x06: unary([](value a) { return of_i64(saturate<std::int64_t>(as_f64(a))); }); break;
                                    case 0x07: unary([](value a) { return static_cast<value>(saturate<std::uint64_t>(as_f64(a))); }); break;
                                    case 0x0A:
                                        {
                                            auto const targetMemory = code.u();
                                            auto const sourceMemory = code.u();
                                            auto const count = pop();
                                            auto const source = pop();
                                            auto const target = pop();
                                            auto const n = targetMemory == 0u || sourceMemory == 0u ? static_cast<std::uint64_t>(as_u32(count)) : count;
                                            auto* to = memory_range(targetMemory, target, n);
                                            auto const* from = memory_range(sourceMemory, source, n);
                                            if (n != 0u)
                                                std::memmove(to, from, static_cast<std::size_t>(n));
                                        }
                                        break;
                                    case 0x0B:
                                        {
                                            auto const memoryIndex = code.u();
                                            auto const count = pop();
                                            auto const byte = pop();
                                            auto const target = pop();
                                            auto const n = memoryIndex == 0u ? static_cast<std::uint64_t>(as_u32(count)) : count;
                                            auto* to = memory_range(memoryIndex, target, n);
                                            if (n != 0u)
                                                std::memset(to, static_cast<int>(as_u32(byte) & 0xFFu), static_cast<std::size_t>(n));
                                        }
                                        break;
                                    default:
                                        throw trap{ "unsupported instruction 0xfc 0x" + to_hex(static_cast<std::uint8_t>(sub)) };
                                    }
                                    break;
                                // (prefix 0xFE: atomic accesses (see jit::atomic_access))
                                case 0xFE:
                                    {
                                        auto const sub = code.u();
                                        auto const memarg = code.u();
                                        auto const offset = code.u();
                                        if (sub == 0x03u)
                                        {
                                            std::atomic_thread_fence(std::memory_order_seq_cst);
                                            break;
                                        }
                                        auto const width = jit::atomic_width(sub);
                                        value operand = 0u;
                                        value replacement = 0u;
                                        if (sub == 0x48u || (sub > 0x48u && sub <= 0x4Eu))
                                        {
                                            replacement = pop();
                                            operand = pop();
                                        }
                                        else if (sub >= 0x17u)
                                            operand = pop();
                                        auto const memoryIndex = memarg >> 32u;
                                        std::uint8_t* at;
                                        if (memoryIndex == 0u)
                                        {
                                            auto const address = static_cast<std::uint64_t>(as_u32(pop())) + offset;
                                            if (address + width > iMemory.size())
                                                throw trap{ "out of bounds memory access" };
                                            at = &iMemory[address];
                                        }
                                        else
                                        {
                                            auto const& external = jit::external_memory(memoryIndex);
                                            auto const address = pop() + offset;
                                            if (address + width > external.size || address + width < address)
                                                throw trap{ "out of bounds memory access" };
                                            at = external.base + address;
                                        }
                                        if ((reinterpret_cast<std::uintptr_t>(at) & (width - 1u)) != 0u)
                                            throw trap{ "unaligned atomic" };
                                        auto const result = jit::atomic_access(at, sub, operand, replacement);
                                        if (sub < 0x17u || sub >= 0x1Eu)
                                            stack.push_back(jit::atomic_wide(sub) ? result : of_u32(static_cast<std::uint32_t>(result)));
                                    }
                                    break;
                                default:
                                    throw trap{ "unsupported instruction 0x" + to_hex(op) };
                                }
                            }
                        }
                        catch (std::exception const& e)
                        {
                            // (debugging: the program stops for the error (before its frames are unwound))
                            if (iDebug != nullptr)
                                debug_error(activating.a, e.what());
                            throw;
                        }
                    }
                    void resize_memory(std::size_t aSize)
                    {
                        iMemory.resize(aSize);
                        iMemoryView = jit::memory{ iMemory.data(), iMemory.size() };
                    }
                    std::uint32_t global(std::string const& aName) const
                    {
                        auto existing = iGlobalExports.find(aName);
                        if (existing == iGlobalExports.end())
                            return 0u;
                        return as_u32(iGlobals.at(existing->second));
                    }
                private:
                    // (a memory's bytes at an address (memory 0: a 32 bit address; an external memory: a 64 bit one), checked)
                    std::uint8_t* memory_range(std::uint64_t aMemory, value aAddress, std::uint64_t aCount)
                    {
                        if (aMemory == 0u)
                        {
                            auto const address = static_cast<std::uint64_t>(as_u32(aAddress));
                            if (address + aCount > iMemory.size())
                                throw trap{ "out of bounds memory access" };
                            return iMemory.data() + address;
                        }
                        auto const& external = jit::external_memory(aMemory);
                        if (aAddress + aCount > external.size || aAddress + aCount < aAddress)
                            throw trap{ "out of bounds memory access" };
                        return external.base + aAddress;
                    }
                    // (a saturating conversion: NaN is 0, a value beyond the type's range its bound)
                    template <typename T, typename F>
                    static T saturate(F aValue)
                    {
                        if (std::isnan(aValue))
                            return T{ 0 };
                        auto const t = static_cast<long double>(std::trunc(aValue));
                        long double const lowest = static_cast<long double>(std::numeric_limits<T>::min());
                        long double const limit = static_cast<long double>(std::numeric_limits<T>::max()) + 1.0L;
                        if (t < lowest)
                            return std::numeric_limits<T>::min();
                        if (t >= limit)
                            return std::numeric_limits<T>::max();
                        return static_cast<T>(t);
                    }
                    template <typename T, typename F>
                    static T truncate(F aValue)
                    {
                        if (std::isnan(aValue))
                            throw trap{ "invalid conversion to integer" };
                        auto const t = static_cast<long double>(std::trunc(aValue));
                        long double const lowest = static_cast<long double>(std::numeric_limits<T>::min());
                        long double const limit = static_cast<long double>(std::numeric_limits<T>::max()) + 1.0L;
                        if (t < lowest || t >= limit)
                            throw trap{ "integer overflow" };
                        return static_cast<T>(t);
                    }
                    template <typename F>
                    static F wasm_min(F aLhs, F aRhs)
                    {
                        if (std::isnan(aLhs) || std::isnan(aRhs))
                            return std::numeric_limits<F>::quiet_NaN();
                        if (aLhs == aRhs)
                            return std::signbit(aLhs) ? aLhs : aRhs;
                        return aLhs < aRhs ? aLhs : aRhs;
                    }
                    template <typename F>
                    static F wasm_max(F aLhs, F aRhs)
                    {
                        if (std::isnan(aLhs) || std::isnan(aRhs))
                            return std::numeric_limits<F>::quiet_NaN();
                        if (aLhs == aRhs)
                            return std::signbit(aLhs) ? aRhs : aLhs;
                        return aLhs > aRhs ? aLhs : aRhs;
                    }
                    static std::string to_hex(std::uint32_t aValue)
                    {
                        std::ostringstream oss;
                        oss << std::hex << aValue;
                        return oss.str();
                    }
                    void parse()
                    {
                        reader r{ iModule };
                        if (r.raw<std::uint32_t>() != 0x6D736100u || r.raw<std::uint32_t>() != 1u)
                            throw exceptions::invalid_instruction();
                        std::vector<std::uint32_t> definedTypes;
                        while (r.position() < iModule.size())
                        {
                            auto const id = r.byte();
                            auto const size = static_cast<std::size_t>(r.u());
                            auto const end = r.position() + size;
                            switch (id)
                            {
                            case 1:
                                for (auto n = r.u(); n > 0u; --n)
                                {
                                    if (r.byte() != 0x60u)
                                        throw exceptions::invalid_instruction();
                                    function_type t;
                                    for (auto p = r.u(); p > 0u; --p)
                                        t.parameters.push_back(r.byte());
                                    for (auto p = r.u(); p > 0u; --p)
                                        t.results.push_back(r.byte());
                                    iTypes.push_back(t);
                                }
                                break;
                            case 2:
                                for (auto n = r.u(); n > 0u; --n)
                                {
                                    r.name();
                                    auto const field = r.name();
                                    if (r.byte() != 0x00u)
                                        throw trap{ "unsupported import kind" };
                                    function f;
                                    f.type = static_cast<std::uint32_t>(r.u());
                                    f.import = field;
                                    iImportTypes[field] = f.type;
                                    iFunctions.push_back(f);
                                }
                                break;
                            case 3:
                                for (auto n = r.u(); n > 0u; --n)
                                    definedTypes.push_back(static_cast<std::uint32_t>(r.u()));
                                break;
                            case 5:
                                for (auto n = r.u(); n > 0u; --n)
                                {
                                    auto const flags = r.byte();
                                    resize_memory(static_cast<std::size_t>(r.u()) * 65536u);
                                    if (flags & 1u)
                                        r.u();
                                }
                                break;
                            case 6:
                                for (auto n = r.u(); n > 0u; --n)
                                {
                                    iGlobalTypes.push_back(r.byte());
                                    auto const flags = r.byte();
                                    iThreadGlobals.push_back((flags & bytecode::GlobalPerThread) != 0u ? 1u : 0u);
                                    iAnyThreadGlobals = iAnyThreadGlobals || iThreadGlobals.back() != 0u;
                                    iGlobals.push_back(constant(r));
                                    iGlobalInitials.push_back(iGlobals.back());
                                }
                                break;
                            case 7:
                                for (auto n = r.u(); n > 0u; --n)
                                {
                                    auto const name = r.name();
                                    auto const kind = r.byte();
                                    auto const index = static_cast<std::uint32_t>(r.u());
                                    if (kind == 0x00u)
                                        iFunctionExports[name] = index;
                                    else if (kind == 0x03u)
                                        iGlobalExports[name] = index;
                                }
                                break;
                            case 10:
                                {
                                    // (the bodies' tokens: at most one for each of the section's bytes (room for them all: not grown by
                                    // doubling, which copies them and holds both copies at once))
                                    iTokens.reserve(iTokens.size() + size);
                                    auto const count = r.u();
                                    for (std::uint64_t i = 0u; i < count; ++i)
                                    {
                                        function f;
                                        f.type = definedTypes.at(i);
                                        auto const bodySize = static_cast<std::size_t>(r.u());
                                        auto const bodyEnd = r.position() + bodySize;
                                        for (auto groups = r.u(); groups > 0u; --groups)
                                        {
                                            auto const n = r.u();
                                            auto const t = r.byte();
                                            f.locals.insert(f.locals.end(), static_cast<std::size_t>(n), t);
                                        }
                                        f.codeBegin = r.position();
                                        f.codeEnd = bodyEnd;
                                        match_blocks(f);
                                        iFunctions.push_back(f);
                                        r.seek(bodyEnd);
                                    }
                                }
                                break;
                            case 0:
                                if (auto const sectionName = r.name(); sectionName == bytecode::LinkedSection)
                                {
                                    for (auto n = r.u(); n > 0u; --n)
                                    {
                                        auto language = r.name();
                                        auto const length = static_cast<std::size_t>(r.u());
                                        if (r.position() + length > iModule.size())
                                            throw exceptions::out_of_text();
                                        auto const* const begin = reinterpret_cast<std::byte const*>(iModule.data() + r.position());
                                        iLinkedModules.emplace_back(std::move(language), text{ begin, begin + length });
                                        r.seek(r.position() + length);
                                    }
                                }
                                else if (sectionName == bytecode::LinksSection)
                                {
                                    for (auto n = r.u(); n > 0u; --n)
                                    {
                                        link_export l;
                                        l.name = r.name();
                                        l.language = r.name();
                                        l.function = static_cast<std::uint32_t>(r.u());
                                        l.signature = r.name();
                                        iOwnLinks.push_back(std::move(l));
                                    }
                                }
                                else if (sectionName == bytecode::FramesSection)
                                {
                                    for (auto n = r.u(); n > 0u; --n)
                                        iSources.push_back(r.name());
                                    auto const imports = static_cast<std::size_t>(std::count_if(iFunctions.begin(), iFunctions.end(), [](function const& f) { return f.import.has_value(); }));
                                    auto const count = r.u();
                                    iFrames.resize(imports + count);
                                    for (std::uint64_t i = 0u; i < count; ++i)
                                    {
                                        auto& info = iFrames[imports + i];
                                        info.name = r.name();
                                        info.frame = !info.name.empty();
                                        info.source = static_cast<std::uint32_t>(r.u());
                                        info.firstLine = static_cast<std::uint32_t>(r.u());
                                    }
                                }
                                else if (sectionName == bytecode::VariablesSection)
                                {
                                    iHasVariables = true;
                                    for (auto n = r.u(); n > 0u; --n)
                                    {
                                        auto const function = static_cast<std::uint32_t>(r.u());
                                        auto& variables = function == bytecode::NoFunction ? iGlobalVariables : iVariables[function];
                                        for (auto m = r.u(); m > 0u; --m)
                                        {
                                            variable_info v;
                                            v.name = r.name();
                                            v.flags = r.byte();
                                            v.storage = r.byte();
                                            v.index = static_cast<std::uint32_t>(r.u());
                                            v.firstLine = static_cast<std::uint32_t>(r.u());
                                            v.lastLine = static_cast<std::uint32_t>(r.u());
                                            v.language = r.name();
                                            v.type = r.name();
                                            variables.push_back(std::move(v));
                                        }
                                    }
                                }
                                break;
                            case 11:
                                for (auto n = r.u(); n > 0u; --n)
                                {
                                    if (r.u() != 0u)
                                        throw trap{ "unsupported data segment" };
                                    auto const offset = static_cast<std::size_t>(as_u32(constant(r)));
                                    auto const length = static_cast<std::size_t>(r.u());
                                    if (offset + length > iMemory.size())
                                        throw trap{ "data segment out of bounds" };
                                    if (r.position() + length > iModule.size())
                                        throw exceptions::out_of_text();
                                    std::memcpy(iMemory.data() + offset, iModule.data() + r.position(), length);
                                    r.seek(r.position() + length);
                                }
                                break;
                            default:
                                break;
                            }
                            r.seek(end);
                        }
                        for (std::uint32_t f = 0u; f < iFunctions.size(); ++f)
                            if (!iFunctions[f].import)
                                iCodeOrder.push_back(f);
                        std::sort(iCodeOrder.begin(), iCodeOrder.end(), [&](std::uint32_t l, std::uint32_t r) { return iFunctions[l].codeBegin < iFunctions[r].codeBegin; });
                        iFrames.resize(std::max(iFrames.size(), iFunctions.size()));
                    }
                    static value constant(reader& r)
                    {
                        value result = 0u;
                        switch (r.byte())
                        {
                        case 0x41: result = of_i32(static_cast<std::int32_t>(r.s())); break;
                        case 0x42: result = of_i64(r.s()); break;
                        case 0x43: result = of_f32(r.raw<float>()); break;
                        case 0x44: result = of_f64(r.raw<double>()); break;
                        default: throw trap{ "unsupported constant expression" };
                        }
                        if (r.byte() != 0x0Bu)
                            throw trap{ "unsupported constant expression" };
                        return result;
                    }
                    void match_blocks(function& f)
                    {
                        // decode the body into tokens, matching blocks with their ends (and ifs with their elses)
                        reader r{ iModule, f.codeBegin };
                        auto const begin = iTokens.size();
                        auto token = [&](std::uint64_t aValue) { iTokens.push_back(aValue); };
                        std::vector<std::size_t> open;  // (the blocks (see iBlocks) that are open)
                        while (r.position() < f.codeEnd)
                        {
                            auto const position = iTokens.size();
                            auto const op = r.byte();
                            token(op);
                            switch (op)
                            {
                            case 0x02: case 0x03: case 0x04:
                                token(r.byte());
                                open.push_back(iBlocks.size());
                                iBlocks.push_back(block_tokens{ position, 0u, 0u });
                                break;
                            case 0x05:
                                iBlocks[open.back()].elseAt = iTokens.size();
                                break;
                            case 0x0B:
                                if (!open.empty())
                                {
                                    iBlocks[open.back()].end = iTokens.size();
                                    open.pop_back();
                                }
                                break;
                            case 0x0C: case 0x0D: case 0x10: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24:
                                token(r.u());
                                break;
                            case 0x0E:
                                {
                                    auto const n = r.u();
                                    token(n);
                                    for (auto i = n + 1u; i > 0u; --i)
                                        token(r.u());
                                }
                                break;
                            case 0x11:
                                token(r.u());
                                token(r.u());
                                break;
                            case 0x3F: case 0x40:
                                token(r.byte());
                                break;
                            case 0xFC:
                                {
                                    // (the prefixed instruction, then its immediates: memory.copy's two memory indices, memory.fill's one;
                                    // the others that have immediates are not supported (their execution traps))
                                    auto const sub = r.u();
                                    token(sub);
                                    if (sub == 0x0Au)
                                    {
                                        token(r.byte());
                                        token(r.byte());
                                    }
                                    else if (sub == 0x0Bu)
                                        token(r.byte());
                                    else if (sub > 0x07u)
                                        throw trap{ "unsupported instruction 0xfc 0x" + to_hex(static_cast<std::uint8_t>(sub)) };
                                }
                                break;
                            case 0xFE:
                                {
                                    // (prefix 0xFE (atomic accesses): the instruction, then its memarg (as a load's: its flags (with the memory
                                    // index in their high 32 bits) and offset); atomic.fence's: its reserved byte, then 0 (three immediates each))
                                    auto const sub = r.u();
                                    token(sub);
                                    if (sub == 0x03u)
                                    {
                                        token(r.byte());
                                        token(0u);
                                    }
                                    else if (sub >= 0x10u && sub <= 0x4Eu)
                                    {
                                        auto const flags = r.u();
                                        std::uint64_t const memoryIndex = (flags & 0x40u) != 0u ? r.u() : 0u;
                                        token((flags & ~std::uint64_t{ 0x40u } & 0xFFFFFFFFull) | (memoryIndex << 32u));
                                        token(r.u());
                                    }
                                    else
                                        throw trap{ "unsupported instruction 0xfe 0x" + to_hex(static_cast<std::uint8_t>(sub)) };
                                }
                                break;
                            case 0x41:
                                token(static_cast<std::uint64_t>(r.s()));
                                break;
                            case 0x42:
                                {
                                    auto const v = static_cast<std::uint64_t>(r.s());
                                    // (a line marker (and its drop): removed; the line of the code that follows it)
                                    // (a position marker (and its drop): removed; the position of the call that follows it)
                                    if (bytecode::is_position_marker(v) && r.position() < f.codeEnd && iModule[r.position()] == 0x1Au)
                                    {
                                        r.byte();
                                        iTokens.resize(position);
                                        if (auto const p = v & ~bytecode::PositionMarkerMask; p != 0u)
                                        {
                                            if (!iPositions.empty() && iPositions.back().first == position)
                                                iPositions.back().second = p;
                                            else
                                                iPositions.emplace_back(position, p);
                                        }
                                        break;
                                    }
                                    if (bytecode::is_line_marker(v) && r.position() < f.codeEnd && iModule[r.position()] == 0x1Au)
                                    {
                                        r.byte();
                                        iTokens.resize(position);
                                        auto const line = static_cast<std::uint32_t>(v & 0xFFFFFFFFu);
                                        if (!iLines.empty() && iLines.back().first == position)
                                            iLines.back().second = line;
                                        else
                                            iLines.emplace_back(position, line);
                                        break;
                                    }
                                    token(v);
                                }
                                break;
                            case 0x43:
                                {
                                    std::uint64_t bits = 0u;
                                    auto const v = r.raw<float>();
                                    std::memcpy(&bits, &v, sizeof(v));
                                    token(bits);
                                }
                                break;
                            case 0x44:
                                {
                                    std::uint64_t bits = 0u;
                                    auto const v = r.raw<double>();
                                    std::memcpy(&bits, &v, sizeof(v));
                                    token(bits);
                                }
                                break;
                            default:
                                if (op >= 0x28u && op <= 0x3Eu)
                                {
                                    // (a memarg: its flags (alignment), memory index (multi-memory: if flag 0x40) and offset; the
                                    // flags token's high 32 bits: the memory index)
                                    auto const flags = r.u();
                                    std::uint64_t const memoryIndex = (flags & 0x40u) != 0u ? r.u() : 0u;
                                    token((flags & ~std::uint64_t{ 0x40u } & 0xFFFFFFFFull) | (memoryIndex << 32u));
                                    token(r.u());
                                }
                                break;
                            }
                        }
                        f.codeBegin = begin;
                        f.codeEnd = iTokens.size();
                    }
                    // host functions (module "env")
                    std::string read_string(std::uint32_t aAddress)
                    {
                        return std::string{ view_string(aAddress) };
                    }
                    // a string in memory as a NUL terminated string (if it is one; valid until memory is next allocated)
                    char const* string_pointer(std::uint32_t aAddress) const
                    {
                        auto const text = view_string(aAddress);
                        auto const end = static_cast<std::size_t>(aAddress) + StringHeaderSize + text.size();
                        return end < iMemory.size() && iMemory[end] == 0u ? text.data() : nullptr;
                    }
                    string_header header_of(std::uint32_t aAddress) const
                    {
                        if (static_cast<std::size_t>(aAddress) + StringHeaderSize > iMemory.size())
                            throw trap{ "out of bounds memory access" };
                        string_header header;
                        std::memcpy(&header, &iMemory[aAddress], sizeof(header));
                        return header;
                    }
                    // a string in memory (valid until memory is next allocated)
                    std::string_view view_string(std::uint32_t aAddress) const
                    {
                        auto const length = header_of(aAddress).bytes;
                        if (static_cast<std::size_t>(aAddress) + StringHeaderSize + length > iMemory.size())
                            throw trap{ "out of bounds memory access" };
                        return std::string_view{ reinterpret_cast<char const*>(&iMemory[aAddress + StringHeaderSize]), length };
                    }
                    // memory (zeroed) of a size (e.g. an object's)
                    std::uint32_t allocate(std::uint32_t aBytes)
                    {
                        if (!iHeap)
                            iHeap = global("__heap_base");
                        auto const address = *iHeap;
                        auto const end = (static_cast<std::uint64_t>(address) + aBytes + 7u) & ~std::uint64_t{ 7u };
                        if (end > 0xFFFFFFFFu)
                            throw trap{ "out of memory" };
                        *iHeap = static_cast<std::uint32_t>(end);
                        if (*iHeap > iMemory.size())
                            resize_memory((*iHeap / 65536u + 1u) * 65536u);
                        std::memset(&iMemory[address], 0, static_cast<std::size_t>(end - address));
                        return address;
                    }
                    // memory for a string (its characters are then written)
                    std::uint32_t new_string(std::uint64_t aBytes, std::uint32_t aUtf16Units, std::uint32_t aCodePoints)
                    {
                        // (a string's length in bytes fits in an i32)
                        if (aBytes > 0x7FFFFFFFu)
                            throw trap{ "string too long" };
                        if (!iHeap)
                            iHeap = global("__heap_base");
                        auto const address = *iHeap;
                        auto const end = (static_cast<std::uint64_t>(address) + StringHeaderSize + aBytes + 1u + 7u) & ~std::uint64_t{ 7u };
                        if (end > 0xFFFFFFFFu)
                            throw trap{ "out of memory" };
                        *iHeap = static_cast<std::uint32_t>(end);
                        if (*iHeap > iMemory.size())
                            resize_memory((*iHeap / 65536u + 1u) * 65536u);
                        string_header const header{ static_cast<std::uint32_t>(aBytes), aUtf16Units, aCodePoints };
                        std::memcpy(&iMemory[address], &header, sizeof(header));
                        iMemory[address + StringHeaderSize + header.bytes] = 0u;
                        return address;
                    }
                    // the concatenation of two strings in memory
                    std::uint32_t concatenate_strings(std::uint32_t aLhs, std::uint32_t aRhs)
                    {
                        auto const lhs = header_of(aLhs);
                        auto const rhs = header_of(aRhs);
                        view_string(aLhs);
                        view_string(aRhs);
                        auto const address = new_string(static_cast<std::uint64_t>(lhs.bytes) + rhs.bytes, lhs.utf16Units + rhs.utf16Units,
                            lhs.codePoints + rhs.codePoints);
                        std::memmove(&iMemory[address + StringHeaderSize], &iMemory[aLhs + StringHeaderSize], lhs.bytes);
                        std::memmove(&iMemory[address + StringHeaderSize + lhs.bytes], &iMemory[aRhs + StringHeaderSize], rhs.bytes);
                        return address;
                    }
                    // a string (if any) followed by an integer in decimal (ASCII: one byte, one UTF-16 code unit and one code point 
                    // a digit)
                    std::uint32_t concatenate_integer(std::optional<std::uint32_t> aString, std::int64_t aInteger)
                    {
                        char digits[24];
                        auto const length = static_cast<std::uint32_t>(std::to_chars(std::begin(digits), std::end(digits), aInteger).ptr - digits);
                        string_header lhs{};
                        if (aString)
                        {
                            lhs = header_of(*aString);
                            view_string(*aString);
                        }
                        auto const address = new_string(static_cast<std::uint64_t>(lhs.bytes) + length, lhs.utf16Units + length, lhs.codePoints + length);
                        if (aString)
                            std::memmove(&iMemory[address + StringHeaderSize], &iMemory[*aString + StringHeaderSize], lhs.bytes);
                        std::memcpy(&iMemory[address + StringHeaderSize + lhs.bytes], digits, length);
                        return address;
                    }
                    // objects (see neos/runtime/plugin.h): a handle is the index of a slot that counts the references the 
                    // program holds to it (the slot holds one reference to the object)
                    void* object_pointer(std::uint32_t aHandle) const
                    {
                        if (aHandle == 0u)
                            return nullptr;
                        if (aHandle >= iObjects.size() || iObjects[aHandle].count == 0u)
                            throw trap{ "invalid object handle" };
                        return iObjects[aHandle].object;
                    }
                    std::uint32_t object_handle(void* aObject)
                    {
                        if (aObject == nullptr)
                            return 0u;
                        std::uint32_t handle;
                        if (iObjectTable.free != 0u)
                        {
                            handle = iObjectTable.free;
                            iObjectTable.free = iObjects[handle].next;
                        }
                        else
                        {
                            if (iObjects.size() > 0x7FFFFFFFu)
                                throw trap{ "too many objects" };
                            handle = static_cast<std::uint32_t>(iObjects.size());
                            iObjects.emplace_back();
                            iObjectTable.slots = iObjects.data();
                            iObjectTable.size = static_cast<std::uint32_t>(iObjects.size());
                        }
                        iObjects[handle] = jit::object_slot{ aObject, 1u, 0u };
                        return handle;
                    }
                    void retain_object(std::uint32_t aHandle)
                    {
                        if (aHandle == 0u)
                            return;
                        object_pointer(aHandle);
                        ++iObjects[aHandle].count;
                    }
                    void release_object(std::uint32_t aHandle)
                    {
                        if (aHandle == 0u)
                            return;
                        auto* const object = static_cast<neos_rt_object*>(object_pointer(aHandle));
                        if (--iObjects[aHandle].count != 0u)
                            return;
                        iObjects[aHandle] = jit::object_slot{ nullptr, 0u, iObjectTable.free };
                        iObjectTable.free = aHandle;
                        object->type->release(object);
                    }
                    std::uint32_t allocate_string(std::string_view aValue)
                    {
                        // (a value in memory is copied first: memory can move when it grows)
                        if (!iMemory.empty() && aValue.data() >= reinterpret_cast<char const*>(iMemory.data()) &&
                            aValue.data() < reinterpret_cast<char const*>(iMemory.data() + iMemory.size()))
                            return allocate_string(std::string{ aValue });
                        auto const header = string_header_of(aValue);
                        auto const address = new_string(aValue.size(), header.utf16Units, header.codePoints);
                        std::memcpy(&iMemory[address + StringHeaderSize], aValue.data(), aValue.size());
                        return address;
                    }
                public:
                    // a program that handles the errors of runtime plugins exports a global (an i32) named __neos_error: a 
                    // plugin's error is stored there (as a string) rather than trapping, and the plugin call's result is zero
                    std::optional<std::uint32_t> error_global() const
                    {
                        // (found once (while the exports and globals do not change): a plugin's call of a function of the program reads it)
                        if (iErrorGlobalFor != std::make_pair(iGlobalExports.size(), iGlobals.size()))
                        {
                            auto const existing = iGlobalExports.find("__neos_error");
                            iErrorGlobal = existing == iGlobalExports.end() || existing->second >= iGlobals.size() ? std::nullopt : std::optional<std::uint32_t>{ existing->second };
                            iErrorGlobalFor = std::make_pair(iGlobalExports.size(), iGlobals.size());
                        }
                        return iErrorGlobal;
                    }
                    // (the error that the program did not handle (see error_global): taken)
                    std::optional<std::string> take_error()
                    {
                        if (auto const g = error_global(); g && as_u32(iGlobals[*g]) != 0u)
                        {
                            auto result = read_string(as_u32(iGlobals[*g]));
                            iGlobals[*g] = 0u;
                            return result;
                        }
                        return {};
                    }
                    // a call of a function of the program by a runtime plugin (see neos/runtime/plugin.h: neos_rt_host)
                    char const* call_from_plugin(jit::compiled_module* aNative, unsigned aFunction, unsigned aCount, neos_rt_type const* aParameters,
                        neos_rt_value const* aArguments, neos_rt_type aResult, neos_rt_value* aResultValue)
                    {
                        // (a call's arguments and their handles: on the stack (a call of a few arguments allocates none))
                        constexpr unsigned Few = 8u;
                        value fewArguments[Few];
                        std::uint32_t fewHandles[Few];
                        std::vector<value> manyArguments;
                        std::vector<std::uint32_t> manyHandles;
                        if (aCount > Few)
                        {
                            manyArguments.resize(aCount);
                            manyHandles.resize(aCount);
                        }
                        value* const arguments = aCount > Few ? manyArguments.data() : fewArguments;
                        std::uint32_t* const handles = aCount > Few ? manyHandles.data() : fewHandles;
                        std::size_t handleCount = 0u;
                        auto const release_handles = [&]()
                        {
                            for (std::size_t h = 0u; h < handleCount; ++h)
                                release_object(handles[h]);
                            handleCount = 0u;
                        };
                        try
                        {
                            for (unsigned i = 0u; i < aCount; ++i)
                            {
                                auto const& a = aArguments[i];
                                switch (aParameters[i])
                                {
                                case NEOS_RT_BOOL: arguments[i] = of_bool(a.b != 0); break;
                                case NEOS_RT_CHAR:
                                case NEOS_RT_I32: arguments[i] = of_i32(a.i32); break;
                                case NEOS_RT_U32: arguments[i] = of_u32(a.u32); break;
                                case NEOS_RT_I64: arguments[i] = of_i64(a.i64); break;
                                case NEOS_RT_U64:
                                case NEOS_RT_SIZE: arguments[i] = a.u64; break;
                                case NEOS_RT_F32: arguments[i] = of_f32(a.f32); break;
                                case NEOS_RT_F64: arguments[i] = of_f64(a.f64); break;
                                case NEOS_RT_STRING: arguments[i] = of_u32(allocate_string(a.string != nullptr ? std::string_view{ a.string } : std::string_view{})); break;
                                case NEOS_RT_OBJECT:
                                    // (a handle of its own for the call)
                                    if (a.object != nullptr)
                                        a.object->type->retain(a.object);
                                    handles[handleCount] = object_handle(a.object);
                                    arguments[i] = of_u32(handles[handleCount++]);
                                    break;
                                case NEOS_RT_INTEGER:
                                    if ((a.u64 & 1u) != 0u)
                                    {
                                        // (an object (its address with the low bit set): a handle of its own for the call)
                                        auto* const object = reinterpret_cast<neos_rt_object*>(static_cast<std::uintptr_t>(a.u64 & ~std::uint64_t{ 1u }));
                                        object->type->retain(object);
                                        handles[handleCount] = object_handle(object);
                                        arguments[i] = (static_cast<value>(handles[handleCount++]) << 1u) | 1u;
                                    }
                                    else
                                        arguments[i] = a.u64;
                                    break;
                                default:
                                    throw trap{ "invalid argument type" };
                                }
                            }
                            if (aFunction >= iFunctions.size())
                                throw trap{ "invalid callback" };
                            auto const& type = type_of(aFunction);
                            if (type.parameters.size() != aCount)
                                throw trap{ "invalid callback" };
                            // (an error of the program's that is pending (e.g. while the program releases objects as it handles it, which a 
                            // plugin's release of one calls the program for): not the call's; the call runs without it, and it is pending 
                            // again afterwards)
                            auto const errorGlobal = error_global();
                            value pendingError = 0u;
                            if (errorGlobal)
                                std::swap(pendingError, iGlobals[*errorGlobal]);
                            struct pending_restore
                            {
                                std::vector<value>& globals;
                                std::optional<std::uint32_t> global;
                                value error;
                                ~pending_restore() { if (global && error != 0u) globals[*global] = error; }
                            } const restore{ iGlobals, errorGlobal, pendingError };
                            // (native code's results: on the stack)
                            value fewResults[4] = {};
                            std::vector<value> manyResults;
                            std::size_t resultCount = 0u;
                            if (aNative != nullptr && type.results.size() <= 4u)
                                resultCount = invoke_into(*aNative, aFunction, std::span<value const>{ arguments, aCount }, fewResults);
                            else
                            {
                                manyResults = aNative != nullptr ? invoke(*aNative, aFunction, std::span<value const>{ arguments, aCount }) :
                                    invoke(aFunction, std::vector<value>{ arguments, arguments + aCount });
                                resultCount = manyResults.size();
                            }
                            value const* const results = manyResults.empty() ? fewResults : manyResults.data();
                            release_handles();
                            // (an error that the program did not handle is the call's)
                            if (auto const g = errorGlobal; g && as_u32(iGlobals[*g]) != 0u)
                            {
                                auto& message = call_message();
                                message = read_string(as_u32(iGlobals[*g]));
                                iGlobals[*g] = 0u;
                                return message.c_str();
                            }
                            value const r = resultCount == 0u ? 0u : results[0];
                            switch (aResult)
                            {
                            case NEOS_RT_VOID: break;
                            case NEOS_RT_BOOL: aResultValue->b = static_cast<std::uint32_t>(r) != 0u ? 1 : 0; break;
                            case NEOS_RT_CHAR:
                            case NEOS_RT_I32: aResultValue->i32 = as_i32(r); break;
                            case NEOS_RT_U32: aResultValue->u32 = as_u32(r); break;
                            case NEOS_RT_I64: aResultValue->i64 = as_i64(r); break;
                            case NEOS_RT_U64:
                            case NEOS_RT_SIZE: aResultValue->u64 = r; break;
                            case NEOS_RT_F32: aResultValue->f32 = as_f32(r); break;
                            case NEOS_RT_F64: aResultValue->f64 = as_f64(r); break;
                            case NEOS_RT_STRING:
                                {
                                    thread_local std::string tString;
                                    tString = read_string(as_u32(r));
                                    aResultValue->string = tString.c_str();
                                }
                                break;
                            case NEOS_RT_OBJECT:
                                {
                                    // (the program's new reference becomes the plugin's)
                                    auto* const object = static_cast<neos_rt_object*>(object_pointer(as_u32(r)));
                                    if (object != nullptr)
                                        object->type->retain(object);
                                    release_object(as_u32(r));
                                    aResultValue->object = object;
                                }
                                break;
                            case NEOS_RT_INTEGER:
                                if ((r & 1u) != 0u)
                                {
                                    // (an object: the program's new reference becomes the plugin's)
                                    auto const handle = static_cast<std::uint32_t>(r >> 1u);
                                    auto* const object = static_cast<neos_rt_object*>(object_pointer(handle));
                                    object->type->retain(object);
                                    release_object(handle);
                                    aResultValue->u64 = static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(object)) | 1u;
                                }
                                else
                                    aResultValue->u64 = r;
                                break;
                            }
                            return nullptr;
                        }
                        catch (std::exception const& e)
                        {
                            release_handles();
                            auto& message = call_message();
                            message = e.what();
                            return message.c_str();
                        }
                    }
                    // (the message of the error of a call from a plugin (its thread's): found only when there is one)
                    static std::string& call_message()
                    {
                        thread_local std::string tMessage;
                        return tMessage;
                    }
                private:
                    // the imported functions the VM provides (any other import is a runtime plugin's)
                    enum class import_kind : std::uint8_t
                    {
                        Unresolved,
                        Length,
                        ToInteger,
                        ToString,
                        StringConcat,
                        Fmod,
                        StringCompare,
                        StringFromI64,
                        StringConcatI64,
                        ObjectRetain,
                        ObjectRelease,
                        Raise,
                        Exit,
                        Allocate,
                        Checked,
                        Assume,
                        StackLow,
                        LinkRun,
                        Link,
                        Runtime
                    };
                    import_kind resolve_import(std::uint32_t aFunction)
                    {
                        static std::unordered_map<std::string_view, import_kind> const sHostFunctions =
                        {
                            { "neo::length", import_kind::Length },
                            { "neo::to_integer", import_kind::ToInteger },
                            { "neo::to_string", import_kind::ToString },
                            { "neos::string_concat", import_kind::StringConcat },
                            { "neos::fmod", import_kind::Fmod },
                            { "neos::string_compare", import_kind::StringCompare },
                            { "neos::string_from_i64", import_kind::StringFromI64 },
                            { "neos::string_concat_i64", import_kind::StringConcatI64 },
                            { "neos::object_retain", import_kind::ObjectRetain },
                            { "neos::object_release", import_kind::ObjectRelease },
                            { "neos::raise", import_kind::Raise },  // (an error: the program stops)
                            { "neos::exit", import_kind::Exit },    // (the program stops: its exit status)
                            { "neos::allocate", import_kind::Allocate }, // (memory (zeroed) of a size: its address)
                            { "neos::assume", import_kind::Assume },    // (the program's claim that its operand is not 0: nothing is done)
                            { "neos::stack_low", import_kind::StackLow },   // (nonzero if the call stack is near its end: see jit::StackLowMargin)
                            { "neos::link_run", import_kind::LinkRun }  // (a translation unit after the first runs: see link_run)
                        };
                        auto const& name = *iFunctions.at(aFunction).import;
                        if (auto const host = sHostFunctions.find(name); host != sHostFunctions.end())
                            return host->second;
                        if (name.starts_with(std::string{ bytecode::LinkNamespace } + "::"))
                            return import_kind::Link;
                        if (jit::checked_arithmetic(name))
                            return import_kind::Checked;
                        // a function of a runtime plugin (called through libffi)
                        auto const type = iImportTypes.find(name);
                        if (type == iImportTypes.end())
                            throw trap{ "unresolved import '" + name + "'" };
                        try
                        {
                            auto const& t = iTypes.at(type->second);
                            auto const& f = runtime::resolve(name, t.parameters, t.results);
                            if (iRestricted && runtime::restricted(f))
                                throw trap{ "'" + name + "' is restricted: it can access the host environment and the program is running in restricted mode" };
                            iImportRuntime[aFunction] = &f;
                        }
                        catch (trap const&)
                        {
                            throw;
                        }
                        catch (std::exception const& e)
                        {
                            throw trap{ e.what() };
                        }
                        return import_kind::Runtime;
                    }
                    // checked integer arithmetic (see jit::checked_arithmetic): a trap if the result overflows
                    static value checked_arithmetic(std::uint8_t aCode, value aLhs, value aRhs)
                    {
                        auto const operation = aCode & 3u;
                        bool const wide = (aCode & 4u) != 0u;
                        bool const isUnsigned = (aCode & 8u) != 0u;
                        if (!wide)
                        {
                            // (in 64 bits: the result must be within the 32-bit type's range)
                            std::int64_t const l = isUnsigned ? static_cast<std::int64_t>(static_cast<std::uint32_t>(aLhs)) : static_cast<std::int64_t>(static_cast<std::int32_t>(static_cast<std::uint32_t>(aLhs)));
                            std::int64_t const r = isUnsigned ? static_cast<std::int64_t>(static_cast<std::uint32_t>(aRhs)) : static_cast<std::int64_t>(static_cast<std::int32_t>(static_cast<std::uint32_t>(aRhs)));
                            std::int64_t const result = operation == 0u ? l + r : operation == 1u ? l - r : operation == 2u ? l * r : -l;
                            bool const fits = isUnsigned ? (result >= 0 && result <= 0xFFFFFFFFll) : 
                                (result >= std::numeric_limits<std::int32_t>::min() && result <= std::numeric_limits<std::int32_t>::max());
                            if (!fits)
                                throw trap{ "integer overflow" };
                            return static_cast<value>(static_cast<std::uint32_t>(result));
                        }
                        if (isUnsigned)
                        {
                            std::uint64_t const l = aLhs, r = aRhs;
                            bool const overflow = operation == 0u ? l + r < l : operation == 1u ? l < r : (l != 0u && (l * r) / l != r);
                            if (overflow)
                                throw trap{ "integer overflow" };
                            return operation == 0u ? l + r : operation == 1u ? l - r : l * r;
                        }
                        auto const l = static_cast<std::int64_t>(aLhs), r = static_cast<std::int64_t>(aRhs);
                        auto const min = std::numeric_limits<std::int64_t>::min();
                        std::int64_t const result = static_cast<std::int64_t>(operation == 0u ? aLhs + aRhs : operation == 1u ? aLhs - aRhs : operation == 2u ? aLhs * aRhs : 0u - aLhs);
                        bool overflow;
                        switch (operation)
                        {
                        case 0u: overflow = ((l ^ result) & (r ^ result)) < 0; break;
                        case 1u: overflow = ((l ^ r) & (l ^ result)) < 0; break;
                        case 2u: overflow = l != 0 && ((l == -1 && r == min) || (r == -1 && l == min) || result / l != r); break;
                        default: overflow = l == min; break;
                        }
                        if (overflow)
                            throw trap{ "integer overflow" };
                        return static_cast<value>(result);
                    }
                    // the error a runtime plugin function raised: a trap, unless the program handles plugins' errors (see 
                    // error_global)
                    void plugin_error(std::uint32_t aFunction, char const* aError)
                    {
                        (void)aFunction;
                        if (tDebugTerminating)
                        {
                            tDebugTerminating = false;
                            debug_terminate();
                        }
                        // (a trap that the plugin requested: the program stops, even if it handles plugins' errors)
                        if (tTrapRequest)
                        {
                            auto const message = std::move(*tTrapRequest);
                            tTrapRequest.reset();
                            throw trap{ message };
                        }
                        auto const g = error_global();
                        if (!g)
                            throw runtime::plugin_error(aError);
                        if (as_u32(iGlobals[*g]) == 0u)
                            iGlobals[*g] = of_u32(allocate_string(aError));
                    }
                    // a runtime plugin function that native code can call directly (none if it is not one or cannot be called: it 
                    // is then called through call_import, which reports the error when it is called)
                    std::optional<jit::plugin_function> plugin_function(std::string const& aImport) const
                    {
                        if (!runtime::available())
                            return {};
                        // (an import of a namespace that has no runtime plugin (e.g. a host function: neo::print, neos::string_concat) is not 
                        // looked for as a plugin's (else it is looked for, and not found (thrown), each time a program is compiled))
                        auto const separator = aImport.rfind("::");
                        if (separator == std::string::npos || !runtime::plugin_available(aImport.substr(0u, separator)))
                            return {};
                        auto const type = iImportTypes.find(aImport);
                        if (type == iImportTypes.end())
                            return {};
                        try
                        {
                            auto const& t = iTypes.at(type->second);
                            auto const& f = runtime::resolve(aImport, t.parameters, t.results);
                            if (iRestricted && runtime::restricted(f))
                                return {};
                            jit::plugin_function result;
                            result.function = runtime::c_function(f, result.parameters, result.result, result.error);
                            result.errorPending = runtime::error_pending(f);
                            result.common = runtime::common(f);
                            return result;
                        }
                        catch (...)
                        {
                            return {};
                        }
                    }
                    // an imported function (resolved on its first call): aBuffer holds its arguments and receives its result
                    void call_import(std::uint32_t aFunction, value* aBuffer)
                    {
                        if (iImportKinds.size() <= aFunction)
                        {
                            iImportKinds.resize(iFunctions.size(), import_kind::Unresolved);
                            iImportRuntime.resize(iFunctions.size(), nullptr);
                        }
                        auto kind = iImportKinds[aFunction];
                        if (kind == import_kind::Unresolved)
                            iImportKinds[aFunction] = kind = resolve_import(aFunction);
                        switch (kind)
                        {
                        case import_kind::Length:
                            aBuffer[0] = of_u32(static_cast<std::uint32_t>(view_string(as_u32(aBuffer[0])).size()));
                            break;
                        case import_kind::ToInteger:
                            try
                            {
                                aBuffer[0] = of_i32(std::stoi(read_string(as_u32(aBuffer[0]))));
                            }
                            catch (...)
                            {
                                aBuffer[0] = of_i32(0);
                            }
                            break;
                        case import_kind::ToString:
                            aBuffer[0] = of_u32(allocate_string(std::to_string(as_i32(aBuffer[0]))));
                            break;
                        case import_kind::StringConcat:
                            aBuffer[0] = of_u32(concatenate_strings(as_u32(aBuffer[0]), as_u32(aBuffer[1])));
                            break;
                        case import_kind::Fmod:
                            aBuffer[0] = of_f64(std::fmod(as_f64(aBuffer[0]), as_f64(aBuffer[1])));
                            break;
                        case import_kind::StringCompare:
                            {
                                auto const c = view_string(as_u32(aBuffer[0])).compare(view_string(as_u32(aBuffer[1])));
                                aBuffer[0] = of_i32(c < 0 ? -1 : c > 0 ? 1 : 0);
                            }
                            break;
                        case import_kind::StringFromI64:
                            aBuffer[0] = of_u32(concatenate_integer(std::nullopt, as_i64(aBuffer[0])));
                            break;
                        case import_kind::StringConcatI64:
                            aBuffer[0] = of_u32(concatenate_integer(as_u32(aBuffer[0]), as_i64(aBuffer[1])));
                            break;
                        case import_kind::ObjectRetain:
                            retain_object(as_u32(aBuffer[0]));
                            break;
                        case import_kind::ObjectRelease:
                            release_object(as_u32(aBuffer[0]));
                            break;
                        case import_kind::Runtime:
                            try
                            {
                                runtime::call(*iImportRuntime[aFunction], aBuffer, iMarshaller);
                            }
                            catch (runtime::plugin_error const& e)
                            {
                                plugin_error(aFunction, e.what());
                                if (!type_of(aFunction).results.empty())
                                    aBuffer[0] = 0u;
                            }
                            break;
                        case import_kind::Raise:
                            throw trap{ read_string(as_u32(aBuffer[0])) };
                        case import_kind::Exit:
                            throw exit_request{ static_cast<std::int32_t>(as_i32(aBuffer[0])) };
                        case import_kind::Allocate:
                            aBuffer[0] = of_u32(allocate(as_u32(aBuffer[0])));
                            break;
                        case import_kind::Assume:
                            break;
                        case import_kind::StackLow:
                            {
                                // (the interpreter's: its frames near the limit (generated code's: see jit::StackLowMargin))
                                auto const* const frames = tActivation != nullptr ? tActivation->frames : nullptr;
                                aBuffer[0] = of_u32(frames != nullptr && frames->size() + StackLowFrames >= MaxCallDepth ? 1u : 0u);
                            }
                            break;
                        case import_kind::LinkRun:
                            link_run(as_u32(aBuffer[0]));
                            break;
                        case import_kind::Link:
                            link_call(aFunction, aBuffer);
                            break;
                        case import_kind::Checked:
                            {
                                auto const code = *jit::checked_arithmetic(*iFunctions.at(aFunction).import);
                                aBuffer[0] = checked_arithmetic(code, aBuffer[0], (code & 3u) == 3u ? 0u : aBuffer[1]);
                            }
                            break;
                        default:
                            break;
                        }
                    }
                private:
                public:
                    std::ostream& output() const { return iOutput; }
                    std::istream& input() const { return iInput; }
                    std::ostream& error_output() const { return iErrorOutput; }
                    bool restricted() const { return iRestricted; }
                    compile_service const& source_compiler() const { return iCompileService; }    ///< (see neos_rt_host::load)
                    std::vector<std::string> const& arguments() const { return iArguments; }    ///< (see neos_rt_host::arguments)
                    // linking (see bytecode::LinkedSection and bytecode::LinksSection)
                    struct link_export
                    {
                        std::string name;
                        std::string language;
                        std::uint32_t function = 0u;
                        std::string signature;
                        machine* program = nullptr;
                        jit::compiled_module* native = nullptr;
                    };
                    // (the modules of the translation units after the first, by their indices: their languages and texts)
                    std::pair<std::string, text> const& linked_module(std::uint32_t aUnit) const
                    {
                        if (aUnit >= iLinkedModules.size())
                            throw trap{ "invalid translation unit" };
                        return iLinkedModules[aUnit];
                    }
                    // (the program that it is linked with (the thread's: the one that it runs first), whose links are its functions' and
                    // those of the translation units that it runs)
                    machine* linker() const { return iLinker; }
                    std::vector<link_export> const& links() const { return iLinks; }
                    // (the program of the translation unit that it is (or whose code loaded it))
                    machine* unit() const { return iUnit; }
                    void set_unit(machine* aUnit) { iUnit = aUnit; }
                    // (the programs of the translation units after the first (see link_run), in the order they ran: their 
                    // languages, programs and native code)
                    struct linked_unit
                    {
                        std::string language;
                        machine* program = nullptr;
                        jit::compiled_module* native = nullptr;
                    };
                    std::vector<linked_unit> const& units() const { return iUnits; }
                    void add_unit(std::string const& aLanguage, machine* aProgram, jit::compiled_module* aNative) { iUnits.push_back(linked_unit{ aLanguage, aProgram, aNative }); }
                    // (the translation units after the first that have begun to run (see link_run), by their indices: a unit runs once)
                    std::size_t linked_module_count() const { return iLinkedModules.size(); }
                    bool unit_started(std::uint32_t aUnit) const { return aUnit < iStartedUnits.size() && iStartedUnits[aUnit]; }
                    bool start_unit(std::uint32_t aUnit)
                    {
                        if (unit_started(aUnit))
                            return false;
                        if (iStartedUnits.size() <= aUnit)
                            iStartedUnits.resize(aUnit + 1u, false);
                        iStartedUnits[aUnit] = true;
                        return true;
                    }
                    jit::compiled_module* linker_native() const { return iLinker != nullptr ? iLinker->iNative : nullptr; }
                    void set_linker(machine* aLinker, jit::compiled_module* aNative)
                    {
                        iLinker = aLinker;
                        iNative = aNative;
                        if (aLinker != nullptr)
                            for (auto l : iOwnLinks)
                            {
                                l.program = this;
                                l.native = aNative;
                                aLinker->iLinks.push_back(std::move(l));
                            }
                    }
                private:
                    // (an import of another translation unit's function (see bytecode::LinkNamespace): "link::<name>#<link signature>"; 
                    // its arguments are linked values, as is its result)
                    void link_call(std::uint32_t aFunction, value* aBuffer)
                    {
                        auto const& import = *iFunctions.at(aFunction).import;
                        auto const prefix = std::string_view{ bytecode::LinkNamespace }.size() + 2u;
                        auto const hash = import.find('#');
                        auto const arrow = hash == std::string::npos ? std::string::npos : import.find('>', hash);
                        if (arrow == std::string::npos || arrow + 1u >= import.size())
                            throw trap{ "invalid import '" + import + "'" };
                        std::string const name = import.substr(prefix, hash - prefix);
                        std::string_view const parameters{ import.data() + hash + 1u, arrow - hash - 1u };
                        char const resultCode = import[arrow + 1u];
                        neos_rt_type type = NEOS_RT_VOID;
                        neos_rt_value v = {};
                        // (none of the translation units that have run has it: the next that has not yet run runs now (see 
                        // run_pending_unit), then the name is linked again)
                        while (host_link(name.c_str(), nullptr, &type, &v) == 0)
                            if (!run_pending_unit())
                                throw trap{ "'" + name + "' is not defined (no translation unit of the program has a global of the name)" };
                        struct release_value
                        {
                            neos_rt_type& type;
                            neos_rt_value& value;
                            ~release_value() { if (type == NEOS_RT_OBJECT && value.object != nullptr) value.object->type->release(value.object); }
                        };
                        release_value const releasing{ type, v };
                        neos_rt_type resultType = NEOS_RT_VOID;
                        neos_rt_value result = {};
                        release_value const releasingResult{ resultType, result };
                        auto const* const linkType = type == NEOS_RT_OBJECT && v.object != nullptr ? reinterpret_cast<neos_rt_link_type const*>(v.object->type) : nullptr;
                        if (linkType != nullptr && linkType->callable(v.object) != 0)
                        {
                            std::vector<neos_rt_type> types(parameters.size());
                            std::vector<neos_rt_value> arguments(parameters.size());
                            for (std::size_t i = 0u; i < parameters.size(); ++i)
                            {
                                auto& t = types[i];
                                auto& a = arguments[i];
                                switch (parameters[i])
                                {
                                case 'b': t = NEOS_RT_BOOL; a.b = as_u32(aBuffer[i]) != 0u ? 1 : 0; break;
                                case 'i': t = NEOS_RT_I64; a.i64 = as_i32(aBuffer[i]); break;
                                case 'u': t = NEOS_RT_I64; a.i64 = as_u32(aBuffer[i]); break;
                                case 'I': t = NEOS_RT_I64; a.i64 = as_i64(aBuffer[i]); break;
                                case 'U': t = NEOS_RT_I64; a.i64 = static_cast<long long>(aBuffer[i]); break;
                                case 'f': t = NEOS_RT_F64; a.f64 = as_f32(aBuffer[i]); break;
                                case 'F': t = NEOS_RT_F64; a.f64 = as_f64(aBuffer[i]); break;
                                case 's':
                                    {
                                        t = NEOS_RT_STRING;
                                        auto const pointer = string_pointer(as_u32(aBuffer[i]));
                                        a.string = pointer != nullptr ? pointer : "";
                                    }
                                    break;
                                default: throw trap{ "invalid import '" + import + "'" };
                                }
                            }
                            if (auto const error = linkType->call(v.object, static_cast<unsigned>(parameters.size()), types.data(), arguments.data(), &resultType, &result))
                                throw trap{ error };
                        }
                        else if (parameters.empty())
                        {
                            // (a value that is not a function: the call's result (borrowed))
                            resultType = type;
                            result = v;
                            if (resultType == NEOS_RT_OBJECT && result.object != nullptr)
                                result.object->type->retain(result.object);
                        }
                        else
                            throw trap{ "'" + name + "' is not a function" };
                        auto const wrong = [&](char const* aWhat) -> trap
                        {
                            static char const* const sKinds[] = { "no value", "a boolean", "", "", "", "an integer", "", "", "", "a float", "a string", "an object", "", 
                                "an integer that does not fit in 64 bits" };
                            auto const kind = static_cast<std::size_t>(resultType) < std::size(sKinds) ? sKinds[resultType] : "";
                            return trap{ "'" + name + "' returned " + kind + ", not " + aWhat };
                        };
                        auto const integer = [&]() -> std::int64_t
                        {
                            switch (resultType)
                            {
                            case NEOS_RT_I64: return result.i64;
                            case NEOS_RT_BOOL: return result.b != 0 ? 1 : 0;
                            case NEOS_RT_F64:
                                if (std::trunc(result.f64) == result.f64 && std::abs(result.f64) < 9.2e18)
                                    return static_cast<std::int64_t>(result.f64);
                                break;
                            default:
                                break;
                            }
                            throw wrong("an integer");
                        };
                        auto const real = [&]() -> double
                        {
                            switch (resultType)
                            {
                            case NEOS_RT_F64: return result.f64;
                            case NEOS_RT_I64: return static_cast<double>(result.i64);
                            case NEOS_RT_BIG_INTEGER: return big_integer_value(result.string);
                            default: throw wrong("a float");
                            }
                        };
                        switch (resultCode)
                        {
                        case 'v': break;
                        case 'b':
                            if (resultType != NEOS_RT_BOOL && resultType != NEOS_RT_I64)
                                throw wrong("a boolean");
                            aBuffer[0] = of_bool(resultType == NEOS_RT_BOOL ? result.b != 0 : result.i64 != 0);
                            break;
                        case 'i': aBuffer[0] = of_i32(static_cast<std::int32_t>(integer())); break;
                        case 'u': aBuffer[0] = of_u32(static_cast<std::uint32_t>(integer())); break;
                        case 'I': case 'U': aBuffer[0] = of_i64(integer()); break;
                        case 'f': aBuffer[0] = of_f32(static_cast<float>(real())); break;
                        case 'F': aBuffer[0] = of_f64(real()); break;
                        case 's':
                            if (resultType != NEOS_RT_STRING)
                                throw wrong("a string");
                            aBuffer[0] = of_u32(allocate_string(result.string != nullptr ? std::string_view{ result.string } : std::string_view{}));
                            break;
                        default: throw trap{ "invalid import '" + import + "'" };
                        }
                    }
                public:
                    // (the calling thread's globals (see bytecode::GlobalPerThread): the program's own thread's are the module's; another thread's
                    // (one that a runtime plugin attached: see neos_rt_host::thread_attach), its own, from the globals' initial values when it 
                    // first uses them (the others are the module's: they are not used there))
                    value* thread_globals()
                    {
                        if (!iAnyThreadGlobals || std::this_thread::get_id() == iThread)
                            return iGlobals.data();
                        thread_local std::unordered_map<std::uint64_t, std::vector<value>> tGlobals;
                        auto& globals = tGlobals[iSerial];
                        if (globals.size() != iGlobalInitials.size())
                            globals = iGlobalInitials;
                        return globals.data();
                    }
                    // (a global's storage (its word): the interpreter and generated code use it (it does not move); one that each thread has
                    // its own of: the calling thread's)
                    value* global_address(std::uint32_t aIndex)
                    {
                        if (aIndex < iThreadGlobals.size() && iThreadGlobals[aIndex] != 0u)
                            return &thread_globals()[aIndex];     // (the calling thread's (see thread_globals))
                        return aIndex < iGlobals.size() ? &iGlobals[aIndex] : nullptr;
                    }
                    // (a global whose value does not change again (see neos_rt_host::fix_global): code optimized after this uses it as a constant)
                    void fix_global(std::uint32_t aIndex)
                    {
                        if (aIndex < iGlobals.size() && aIndex < iFixedGlobals.size())
                            iFixedGlobals[aIndex] = 1u;
                    }
                private:
                    static inline std::atomic<std::uint64_t> sSerial{ 0u };
                    std::uint64_t const iSerial;    ///< (identifies the program (see walk): a program loaded at the address of one that has ended is another)
                    std::ostream& iOutput;
                    std::istream& iInput;
                    std::ostream& iErrorOutput;
                    std::uint64_t& iInstructions;
                    bool const iRestricted; ///< runtime plugin functions that can change the host environment cannot be called
                    compile_service const iCompileService;
                    std::vector<std::string> const iArguments;
                    std::thread::id const iThread;     ///< (the program's own (see thread_globals))
                    std::span<std::uint8_t const> iModule;    ///< (the text, while it is parsed (see the constructor))
                    std::vector<std::uint64_t> iTokens;   ///< decoded function bodies
                    std::vector<std::size_t> iEnds;       ///< block/loop/if token -> token after its end (see block_tables)
                    std::vector<std::size_t> iElses;      ///< if token -> token after its else (0 if none)
                    // (the blocks, loops and ifs: their tokens, and the tokens after their ends and elses (iEnds and iElses are made from
                    // them when the interpreter first runs: the generated code does not use them))
                    struct block_tokens
                    {
                        std::size_t at;
                        std::size_t end;
                        std::size_t elseAt;
                    };
                    std::vector<block_tokens> iBlocks;
                    std::once_flag iBlockTables;
                    std::vector<function_type> iTypes;
                    std::vector<function> iFunctions;
                    linear_memory iMemory;
                    jit::memory iMemoryView;   ///< (for the JIT)
                    std::vector<value> iGlobals;
                    std::vector<std::uint8_t> iFixedGlobals;    ///< (see fix_global)
                    std::vector<std::uint8_t> iThreadGlobals;   ///< (see thread_globals: nonzero for a global that each thread has its own of)
                    bool iAnyThreadGlobals = false;
                    std::vector<value> iGlobalInitials;
                    std::vector<std::uint8_t> iGlobalTypes;
                    std::unordered_map<std::string, std::uint32_t> iImportTypes;
                    std::vector<import_kind> iImportKinds;
                    std::vector<runtime::function const*> iImportRuntime;
                    runtime::marshaller const iMarshaller
                    {
                        [this](std::uint32_t aAddress) { return read_string(aAddress); },
                        [this](std::uint32_t aAddress) { return string_pointer(aAddress); },
                        [this](std::string_view aString) { return allocate_string(aString); },
                        [this](std::uint32_t aHandle) { return object_pointer(aHandle); },
                        [this](void* aObject) { return object_handle(aObject); }
                    };
                    std::vector<jit::object_slot> iObjects = std::vector<jit::object_slot>(1u); ///< (0 is no object)
                    jit::object_table iObjectTable = { iObjects.data(), 1u, 0u }; ///< (the slots, as generated code uses them)
                    std::unordered_map<std::string, std::uint32_t> iFunctionExports;
                    std::vector<std::pair<std::string, text>> iLinkedModules;
                    std::vector<link_export> iOwnLinks;
                    std::vector<link_export> iLinks;
                    machine* iLinker = nullptr;
                    jit::compiled_module* iNative = nullptr;   ///< (its native code, if it runs as native code (see set_linker))
                    machine* iUnit = nullptr;
                    std::vector<linked_unit> iUnits;
                    std::vector<bool> iStartedUnits;
                    std::unordered_map<std::string, std::uint32_t> iGlobalExports;
                    mutable std::optional<std::uint32_t> iErrorGlobal;    ///< (error_global's)
                    mutable std::pair<std::size_t, std::size_t> iErrorGlobalFor{ ~std::size_t{ 0u }, 0u };
                    std::optional<std::uint32_t> iHeap;
                    // (frames: the functions' (see FramesSection), the names of their sources, the defined functions in the order of 
                    // their code, and the lines of their code (the token where each line begins))
                    std::vector<frame_info> iFrames;
                    std::vector<std::string> iSources;
                    std::vector<std::uint32_t> iCodeOrder;
                    std::vector<std::pair<std::size_t, std::uint32_t>> iLines;
                    std::vector<std::pair<std::size_t, std::uint64_t>> iPositions;  ///< (token -> its position (see bytecode::PositionMarker))
                    // the debugger (see neos/debug.hpp)
                    friend class debug_stop;
                public:
                    // (a variable of a function (or a global of the module), as the compiler described it (see VariablesSection))
                    struct variable_info
                    {
                        std::string name;
                        std::uint8_t flags = 0u;
                        std::uint8_t storage = bytecode::VariableLocal;
                        std::uint32_t index = 0u;
                        std::uint32_t firstLine = 0u;
                        std::uint32_t lastLine = 0u;
                        std::string language;
                        std::string type;
                    };
                public:
                    debug::session* debug_session() const 
                    { 
                        return iDebug; 
                    }
                    void set_debug_session(debug::session* aSession)
                    {
                        iDebug = aSession;
                        iDebugVersion = 0u;
                    }
                    std::vector<variable_info> const* variables_of(std::uint32_t aFunction) const
                    {
                        auto const existing = iVariables.find(aFunction);
                        return existing != iVariables.end() ? &existing->second : nullptr;
                    }
                    std::vector<variable_info> const& global_variables() const
                    {
                        return iGlobalVariables;
                    }
                    bool has_variables() const
                    {
                        return iHasVariables;
                    }
                    std::vector<std::uint8_t> const& local_types(std::uint32_t aFunction) const
                    {
                        return iFunctions.at(aFunction).locals;
                    }
                    std::uint32_t debug_line(std::size_t aToken) const
                    {
                        return aToken < iDebugTokenLines.size() ? iDebugTokenLines[aToken] : 0u;
                    }
                    value* debug_global(std::uint32_t aGlobal)
                    {
                        if (aGlobal >= iGlobals.size())
                            return nullptr;
                        return iAnyThreadGlobals && iThreadGlobals.at(aGlobal) != 0u ? &thread_globals()[aGlobal] : &iGlobals[aGlobal];
                    }
                    std::string debug_string(std::uint32_t aAddress) const
                    {
                        try
                        {
                            return std::string{ view_string(aAddress) };
                        }
                        catch (...)
                        {
                            return {};
                        }
                    }
                    // (debugging: aSize bytes of the program's memory at aAddress: false if they are not in it)
                    bool debug_read(std::uint32_t aMemory, std::uint64_t aAddress, void* aBuffer, std::size_t aSize) const
                    {
                        if (aMemory != 0u)
                        {
                            // (an external memory (see neos_rt_host::external_memory_at): its region can have pages that are not accessible
                            // (e.g. reserved, not committed), so they are copied by the system, which fails rather than faults (the debugger 
                            // reads what a variable's value says is an address, which can be anything))
                            auto const& external = jit::external_memory(aMemory);
                            if (external.base == nullptr || aAddress > external.size || aSize > external.size - aAddress)
                                return false;
                            return debug_copy(aBuffer, external.base + aAddress, aSize);
                        }
                        if (aAddress > iMemory.size() || aSize > iMemory.size() - aAddress)
                            return false;
                        std::memcpy(aBuffer, iMemory.data() + aAddress, aSize);
                        return true;
                    }
                    static bool debug_copy(void* aBuffer, void const* aSource, std::size_t aSize)
                    {
#if defined(_WIN32)
                        SIZE_T copied = 0u;
                        return ::ReadProcessMemory(::GetCurrentProcess(), aSource, aBuffer, aSize, &copied) != FALSE && copied == aSize;
#else
                        // (through a pipe: write fails with EFAULT if the source is not accessible)
                        static std::mutex sMutex;
                        static int sPipe[2] = { -1, -1 };
                        std::lock_guard lock{ sMutex };
                        if (sPipe[0] == -1 && ::pipe(sPipe) != 0)
                        {
                            sPipe[0] = sPipe[1] = -1;
                            return false;
                        }
                        auto* const destination = static_cast<char*>(aBuffer);
                        auto const* const source = static_cast<char const*>(aSource);
                        for (std::size_t done = 0u; done < aSize;)
                        {
                            auto const chunk = std::min<std::size_t>(aSize - done, 4096u);
                            auto const written = ::write(sPipe[1], source + done, chunk);
                            if (written <= 0)
                                return false;
                            for (std::size_t got = 0u; got < static_cast<std::size_t>(written);)
                            {
                                auto const r = ::read(sPipe[0], destination + done + got, static_cast<std::size_t>(written) - got);
                                if (r <= 0)
                                    return false;
                                got += static_cast<std::size_t>(r);
                            }
                            done += static_cast<std::size_t>(written);
                        }
                        return true;
#endif
                    }
                    // (debugging: a literal of an enumeration type ("enum<size>{<literal>,...}") of the program's variables: as it is, else 
                    // ignoring case (e.g. Ada's, whose names are case insensitive); as the type names it)
                    std::optional<std::string> debug_enum_literal(std::string_view aName) const
                    {
                        auto const search = [&](bool aIgnoreCase) -> std::optional<std::string>
                        {
                            auto const same = [&](std::string_view aLiteral)
                            {
                                return aLiteral.size() == aName.size() && (aIgnoreCase ? 
                                    std::equal(aLiteral.begin(), aLiteral.end(), aName.begin(), [](char a, char b) 
                                        { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }) : 
                                    aLiteral == aName);
                            };
                            auto const in = [&](std::string_view aType) -> std::optional<std::string>
                            {
                                for (auto at = aType.find("enum"); at != std::string_view::npos; at = aType.find("enum", at + 4u))
                                {
                                    auto const open = aType.find('{', at);
                                    auto const close = aType.find('}', open);
                                    if (open == std::string_view::npos || close == std::string_view::npos)
                                        break;
                                    auto literals = aType.substr(open + 1u, close - open - 1u);
                                    for (std::size_t start = 0u; start <= literals.size();)
                                    {
                                        auto const comma = std::min(literals.find(',', start), literals.size());
                                        if (same(literals.substr(start, comma - start)))
                                            return std::string{ literals.substr(start, comma - start) };
                                        start = comma + 1u;
                                    }
                                }
                                return {};
                            };
                            for (auto const& v : iGlobalVariables)
                                if (auto const literal = in(v.type))
                                    return literal;
                            for (auto const& [function, variables] : iVariables)
                                for (auto const& v : variables)
                                    if (auto const literal = in(v.type))
                                        return literal;
                            return {};
                        };
                        if (auto const literal = search(false))
                            return literal;
                        return search(true);
                    }
                    // (debugging: a type named "%<n>" (see VariablesSection): its description)
                    std::string_view debug_named_type(std::string_view aType) const
                    {
                        for (int i = 0; i < 8 && aType.starts_with('%'); ++i)
                        {
                            auto const name = "%type" + std::string{ aType.substr(1u) };
                            bool found = false;
                            for (auto const& v : iGlobalVariables)
                                if (v.name == name)
                                {
                                    aType = v.type;
                                    found = true;
                                    break;
                                }
                            if (!found)
                                break;
                        }
                        return aType;
                    }
                    void debug_prepare();
                    void debug_statement(activation& aActivation, std::size_t aToken);
                    void debug_error(activation& aActivation, std::string const& aMessage);
                private:
                    debug::session* iDebug = tDebugSession;
                    std::atomic<std::uint64_t> iDebugVersion = 0u;
                    std::mutex iDebugMutex;
                    std::vector<std::uint8_t> iDebugMarks;              ///< (each token's: DebugStatement (a statement begins), DebugBreakpoint)
                    std::vector<std::uint32_t> iDebugTokenLines;        ///< (a statement's token: its line)
                    std::map<std::uint32_t, std::map<std::uint32_t, std::vector<std::size_t>>> iDebugLines; ///< (source -> line -> its statements' tokens)
                    std::unordered_map<std::size_t, std::vector<std::pair<std::uint32_t, bool>>> iDebugBreakpoints;  ///< (token -> the breakpoints there 
                                                                        ///< (and whether at its column: it stops even if its line is the previous statement's))
                    std::unordered_map<std::uint32_t, std::vector<variable_info>> iVariables;
                    std::vector<variable_info> iGlobalVariables;
                    bool iHasVariables = false;
                };
            }

            namespace
            {
                std::optional<std::string> environment_variable(char const* aName)
                {
#if defined(_MSC_VER)
                    char* buffer = nullptr;
                    std::size_t size = 0u;
                    if (::_dupenv_s(&buffer, &size, aName) != 0 || buffer == nullptr)
                        return {};
                    std::string const result{ buffer };
                    std::free(buffer);
                    return result;
#else
                    if (auto const value = std::getenv(aName))
                        return std::string{ value };
                    return {};
#endif
                }
            }

            namespace
            {
                // (the program that the thread is running: plugins call its functions)
                struct running
                {
                    machine* program = nullptr;
                    jit::compiled_module* native = nullptr;
                };
                thread_local running tRunning;

                char const* host_call(unsigned aFunction, unsigned aCount, neos_rt_type const* aParameters, neos_rt_value const* aArguments,
                    neos_rt_type aResult, neos_rt_value* aResultValue)
                {
                    if (tRunning.program == nullptr)
                        return "no program is running";
                    return tRunning.program->call_from_plugin(tRunning.native, aFunction, aCount, aParameters, aArguments, aResult, aResultValue);
                }

                // --- coroutines (a stack of their own: Windows fibers, else neos_vm_switch_context (or ucontext))

                struct coroutine
                {
                    void (*entry)(void*) = nullptr;
                    void* data = nullptr;
                    bool finished = false;
                    coroutine* previous = nullptr;  ///< (the coroutine that resumed it (null: the thread))
                    activation* activations = nullptr;  ///< (the invocations within it, while it is suspended)
                    activation* resumerActivations = nullptr; ///< (the invocations of the code that resumed it)
                    std::optional<running> suspendedRunning;    ///< (the program running within it when it was suspended)
                    running resumerRunning;                     ///< (the program of the code that resumed it)
#if defined(_WIN32)
                    void* fiber = nullptr;
                    void* resumer = nullptr;        ///< (the fiber that resumed it)
                    void* suspended = nullptr;      ///< (the fiber that suspended it: its own, or one of generated code within it)
#else
                    static constexpr std::size_t StackSize = 8u * 1024u * 1024u;
                    static constexpr std::size_t GuardSize = 64u * 1024u;
#if defined(NEOS_VM_SWITCH_CONTEXT)
                    void* sp = nullptr;             ///< (its stack pointer, while it is suspended (see neos_vm_switch_context))
                    void* resumerSp = nullptr;      ///< (the stack pointer of the code that resumed it, while it runs)
#else
                    ucontext_t context{};
                    ucontext_t resumer{};
#endif
                    void* stack = nullptr;
#endif
                };

                thread_local coroutine* tCoroutine = nullptr;

#if !defined(_WIN32)
                // (coroutines' stacks: those of coroutines deleted are reused (mapping one takes system calls))
                struct free_stacks
                {
                    std::mutex mutex;
                    std::vector<void*> stacks;
                };
                free_stacks& the_free_stacks()
                {
                    static auto& sFreeStacks = *new free_stacks{};  // (never destroyed: coroutines can be deleted as the process ends)
                    return sFreeStacks;
                }
                constexpr std::size_t MaxFreeStacks = 16u;

                void* acquire_stack()
                {
                    {
                        auto& free = the_free_stacks();
                        std::lock_guard<std::mutex> lock{ free.mutex };
                        if (!free.stacks.empty())
                        {
                            auto const stack = free.stacks.back();
                            free.stacks.pop_back();
                            return stack;
                        }
                    }
                    auto const stack = ::mmap(nullptr, coroutine::StackSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                    if (stack == MAP_FAILED)
                        return nullptr;
                    ::mprotect(stack, coroutine::GuardSize, PROT_NONE);
                    return stack;
                }

                void release_stack(void* aStack)
                {
                    {
                        auto& free = the_free_stacks();
                        std::lock_guard<std::mutex> lock{ free.mutex };
                        if (free.stacks.size() < MaxFreeStacks)
                        {
                            free.stacks.push_back(aStack);
                            return;
                        }
                    }
                    ::munmap(aStack, coroutine::StackSize);
                }
#endif

#if defined(_WIN32)
                void __stdcall coroutine_start(void* aCoroutine)
                {
                    auto& c = *static_cast<coroutine*>(aCoroutine);
                    c.entry(c.data);
                    c.finished = true;
                    ::SwitchToFiber(c.resumer);
                }
#elif defined(NEOS_VM_SWITCH_CONTEXT)
                // (its first resume returns to neos_vm_coroutine_trampoline, which calls this (r13) with it (r12))
                void coroutine_start(void* aCoroutine)
                {
                    auto& c = *static_cast<coroutine*>(aCoroutine);
                    c.entry(c.data);
                    c.finished = true;
                    neos_vm_switch_context(&c.sp, c.resumerSp);
                    std::abort();   // (a finished coroutine is not resumed)
                }
#else
                void coroutine_start(unsigned aLow, unsigned aHigh)
                {
                    auto& c = *reinterpret_cast<coroutine*>((static_cast<std::uintptr_t>(aHigh) << 32u) | aLow);
                    c.entry(c.data);
                    c.finished = true;
                    // (returns to its uc_link: the context that resumed it)
                }
#endif

#if !defined(_WIN32)
                std::uint64_t coroutine_stack_low(coroutine const* aCoroutine)
                {
                    return aCoroutine != nullptr ? reinterpret_cast<std::uint64_t>(aCoroutine->stack) + coroutine::GuardSize : 0u;
                }
#endif

                neos_rt_coroutine* host_coroutine_new(void (*aEntry)(void*), void* aData)
                {
                    auto* const c = new coroutine{};
                    c->entry = aEntry;
                    c->data = aData;
#if defined(_WIN32)
                    c->fiber = ::CreateFiberEx(64u * 1024u, 8u * 1024u * 1024u, FIBER_FLAG_FLOAT_SWITCH, coroutine_start, c);
                    if (c->fiber == nullptr)
                    {
                        delete c;
                        return nullptr;
                    }
#else
                    c->stack = acquire_stack();
                    if (c->stack == nullptr)
                    {
                        delete c;
                        return nullptr;
                    }
#if defined(NEOS_VM_SWITCH_CONTEXT)
                    // (its stack, as neos_vm_switch_context leaves one: its first resume returns to neos_vm_coroutine_trampoline)
                    auto* frame = reinterpret_cast<std::uint64_t*>((reinterpret_cast<std::uintptr_t>(c->stack) + coroutine::StackSize) & ~std::uintptr_t{ 15u });
                    *--frame = 0u;                                                                      // (no return address)
                    *--frame = reinterpret_cast<std::uint64_t>(&neos_vm_coroutine_trampoline);
                    *--frame = 0u;                                                                      // rbp
                    *--frame = 0u;                                                                      // rbx
                    *--frame = reinterpret_cast<std::uint64_t>(c);                                      // r12
                    *--frame = reinterpret_cast<std::uint64_t>(static_cast<void(*)(void*)>(&coroutine_start)); // r13
                    *--frame = 0u;                                                                      // r14
                    *--frame = 0u;                                                                      // r15
                    *--frame = (std::uint64_t{ 0x037Fu } << 32u) | 0x1F80u;                             // (MXCSR and the x87 control word: their defaults)
                    c->sp = frame;
#else
                    ::getcontext(&c->context);
                    c->context.uc_stack.ss_sp = static_cast<char*>(c->stack) + coroutine::GuardSize;
                    c->context.uc_stack.ss_size = coroutine::StackSize - coroutine::GuardSize;
                    c->context.uc_link = &c->resumer;
                    auto const address = reinterpret_cast<std::uintptr_t>(c);
                    ::makecontext(&c->context, reinterpret_cast<void (*)()>(coroutine_start), 2, static_cast<unsigned>(address & 0xFFFFFFFFu), 
                        static_cast<unsigned>(address >> 32u));
#endif
#endif
                    return reinterpret_cast<neos_rt_coroutine*>(c);
                }

                int host_coroutine_resume(neos_rt_coroutine* aCoroutine)
                {
                    auto& c = *reinterpret_cast<coroutine*>(aCoroutine);
                    if (c.finished)
                        return 1;
                    c.previous = tCoroutine;
                    tCoroutine = &c;
                    c.resumerActivations = tActivation;
                    tActivation = c.activations;
                    // (the program running in it is its own: the resumer's is restored when it suspends (or finishes))
                    c.resumerRunning = tRunning;
                    if (c.suspendedRunning)
                        tRunning = *c.suspendedRunning;
#if defined(_WIN32)
                    bool const converted = !::IsThreadAFiber();
                    c.resumer = converted ? ::ConvertThreadToFiber(nullptr) : ::GetCurrentFiber();
                    ::SwitchToFiber(c.suspended != nullptr ? c.suspended : c.fiber);
                    if (converted)
                        ::ConvertFiberToThread();
#else
                    jit::set_coroutine_stack(coroutine_stack_low(&c));
#if defined(NEOS_VM_SWITCH_CONTEXT)
                    neos_vm_switch_context(&c.resumerSp, c.sp);
#else
                    ::swapcontext(&c.resumer, &c.context);
#endif
                    jit::set_coroutine_stack(coroutine_stack_low(c.previous));
#endif
                    c.activations = tActivation;
                    tActivation = c.resumerActivations;
                    tCoroutine = c.previous;
                    c.suspendedRunning = tRunning;
                    tRunning = c.resumerRunning;
                    return c.finished ? 1 : 0;
                }

                void host_coroutine_suspend()
                {
                    auto* const c = tCoroutine;
                    if (c == nullptr)
                        return;
#if defined(_WIN32)
                    c->suspended = ::GetCurrentFiber();
                    ::SwitchToFiber(c->resumer);
#elif defined(NEOS_VM_SWITCH_CONTEXT)
                    neos_vm_switch_context(&c->sp, c->resumerSp);
#else
                    ::swapcontext(&c->context, &c->resumer);
#endif
                }

                void host_coroutine_delete(neos_rt_coroutine* aCoroutine)
                {
                    auto* const c = reinterpret_cast<coroutine*>(aCoroutine);
                    if (c == nullptr)
                        return;
#if defined(_WIN32)
                    ::DeleteFiber(c->fiber);
#else
                    release_stack(c->stack);
#endif
                    delete c;
                }

                // --- threads

                // (one context for each program and its native code (keyed, not searched: a program that compiles source (e.g. each eval)
                // adds one for each program it compiles); a program's is removed when it is unloaded)
                struct thread_contexts
                {
                    std::mutex mutex;
                    std::map<std::pair<machine*, jit::compiled_module*>, running> contexts;
                    std::atomic<std::uint64_t> generation = 0u; // (changed when one is removed)
                };

                thread_contexts& the_thread_contexts()
                {
                    static thread_contexts sContexts;
                    return sContexts;
                }

                // (the thread's last one (see host_thread_context))
                thread_local running* tLastContext = nullptr;
                thread_local std::uint64_t tLastContextGeneration = 0u;

#if defined(_MSC_VER)
                __declspec(noinline)
#else
                [[gnu::noinline]]
#endif
                void* find_thread_context()
                {
                    auto& all = the_thread_contexts();
                    std::lock_guard<std::mutex> lock{ all.mutex };
                    tLastContextGeneration = all.generation.load(std::memory_order_relaxed);
                    return tLastContext = &all.contexts.try_emplace(std::make_pair(tRunning.program, tRunning.native), tRunning).first->second;
                }

                void* host_thread_context()
                {
                    // (the thread's last one is remembered, so that a runtime plugin that asks again (e.g. for each call) does not take the
                    // lock (and finding it (the lock taken) is not inline: the remembered one's check saves no registers))
                    auto& all = the_thread_contexts();
                    auto* const last = tLastContext;
                    if (last != nullptr && last->program == tRunning.program && last->native == tRunning.native &&
                        tLastContextGeneration == all.generation.load(std::memory_order_acquire)) [[likely]]
                        return last;
                    return find_thread_context();
                }

                void host_thread_attach(void* aContext)
                {
                    tRunning = aContext != nullptr ? *static_cast<running*>(aContext) : running{};
                    // (debugging: the thread's programs are its program's session's)
                    tDebugSession = tRunning.program != nullptr ? tRunning.program->debug_session() : nullptr;
                    tDebugStep = debug_step{};
                    if (tDebugSession != nullptr)
                        tDebugSession->thread_id();
                }

                // --- programs compiled while a program runs (they stay until the process ends: their functions may be called)

                // (the calling thread's (see set_compile_service): a thread takes it when it is constructed)
                compile_service& the_compile_service()
                {
                    thread_local compile_service tService;
                    return tService;
                }

                struct loaded_program
                {
                    std::uint64_t instructions = 0u;
                    std::unique_ptr<machine> program;
                    jit::compiled_module_ptr native;
                    std::optional<std::uint32_t> entry;
                };

                std::mutex& loaded_mutex()
                {
                    static std::mutex sMutex;
                    return sMutex;
                }

                std::list<loaded_program>& loaded_programs()
                {
                    static std::list<loaded_program> sPrograms;
                    return sPrograms;
                }

                void* host_load_n(char const* aLanguage, char const* aSource, size_t aSourceLength, char const* aSourceName, char const* aOptions,
                    char const** aError)
                {
                    thread_local std::string tError;
                    auto fail = [&](std::string aMessage) -> void*
                    {
                        tError = std::move(aMessage);
                        if (aError != nullptr)
                            *aError = tError.c_str();
                        return nullptr;
                    };
                    if (tRunning.program == nullptr)
                        return fail("no program is running");
                    if (!tRunning.program->source_compiler())
                        return fail("no compiler");
                    // (a program running in restricted mode: source that it compiles could change the host environment)
                    if (tRunning.program->restricted())
                        return fail("restricted");
                    std::optional<text> compiled;
                    std::string error;
                    {
                        std::lock_guard<std::mutex> lock{ loaded_mutex() };
                        // (the running program's context is restored: the compiler's language plugins may call the runtime)
                        auto const running = tRunning;
                        try
                        {
                            compiled = tRunning.program->source_compiler()(aLanguage, std::string{ aSource, aSourceLength }, aSourceName != nullptr ? aSourceName : "",
                                aOptions != nullptr ? aOptions : "", error);
                        }
                        catch (std::exception const& e)
                        {
                            error = e.what();
                        }
                        tRunning = running;
                    }
                    if (!compiled)
                        return fail(error.empty() ? std::string{ "compilation failed" } : error);
                    try
                    {
                        std::lock_guard<std::mutex> lock{ loaded_mutex() };
                        auto& p = loaded_programs().emplace_back();
                        p.program = std::make_unique<machine>(*compiled, tRunning.program->output(), tRunning.program->input(), tRunning.program->error_output(),
                            p.instructions, tRunning.program->restricted(), tRunning.program->source_compiler(), tRunning.program->arguments());
                        p.entry = p.program->export_function("main");
                        if (tRunning.native != nullptr)
                            p.native = p.program->compile();
                        p.program->set_linker(tRunning.program->linker(), p.native.get());
                        p.program->set_unit(tRunning.program->unit());
                        running const context{ p.program.get(), p.native.get() };
                        auto const previous = tRunning;
                        tRunning = context;
                        auto const result = host_thread_context();
                        tRunning = previous;
                        return result;
                    }
                    catch (std::exception const& e)
                    {
                        return fail(e.what());
                    }
                }

                void* host_load(char const* aLanguage, char const* aSource, char const* aSourceName, char const* aOptions, char const** aError)
                {
                    return host_load_n(aLanguage, aSource, std::strlen(aSource), aSourceName, aOptions, aError);
                }

                char const* host_run(void* aContext, neos_rt_type aResult, neos_rt_value* aResultValue)
                {
                    thread_local std::string tError;
                    auto const& context = *static_cast<running*>(aContext);
                    std::optional<std::uint32_t> entry;
                    {
                        std::lock_guard<std::mutex> lock{ loaded_mutex() };
                        // (the latest first: a program that compiles source usually runs it at once)
                        for (auto p = loaded_programs().rbegin(); p != loaded_programs().rend(); ++p)
                            if (p->program.get() == context.program)
                            {
                                entry = p->entry;
                                break;
                            }
                    }
                    if (!entry)
                        return "the program has no entry point";
                    auto const previous = tRunning;
                    tRunning = context;
                    struct restore { running previous; ~restore() { tRunning = previous; } } const restoreRunning{ previous };
                    try
                    {
                        auto const results = context.native ? context.program->invoke(*context.native, *entry, {}) : context.program->invoke(*entry, {});
                        // (an error that it did not handle)
                        if (auto const e = context.program->take_error())
                        {
                            tError = *e;
                            return tError.c_str();
                        }
                        if (aResultValue != nullptr && aResult != NEOS_RT_VOID && !results.empty())
                        {
                            switch (aResult)
                            {
                            case NEOS_RT_I64:
                            case NEOS_RT_INTEGER: aResultValue->i64 = as_i64(results[0]); break;
                            case NEOS_RT_F64: aResultValue->f64 = as_f64(results[0]); break;
                            default: aResultValue->i32 = as_i32(results[0]); break;
                            }
                        }
                    }
                    catch (std::exception const& e)
                    {
                        tError = e.what();
                        return tError.c_str();
                    }
                    return nullptr;
                }

                void link_run(std::uint32_t aUnit)
                {
                    // a translation unit after the first (see bytecode::LinkedSection): its module is loaded (as 
                    // neos_rt_host::load's are), a program of its own, then its entry point runs
                    if (tRunning.program == nullptr)
                        throw trap{ "no program is running" };
                    auto& program = *tRunning.program;
                    auto const& linked = program.linked_module(aUnit);
                    // (it has run already: its code called by an earlier unit's linked a name that no unit that had run had (see 
                    // link_call))
                    if (!program.start_unit(aUnit))
                        return;
                    running context;
                    {
                        std::lock_guard<std::mutex> lock{ loaded_mutex() };
                        auto& p = loaded_programs().emplace_back();
                        p.program = std::make_unique<machine>(linked.second, program.output(), program.input(), program.error_output(),
                            p.instructions, program.restricted(), program.source_compiler(), program.arguments());
                        p.entry = p.program->export_function("main");
                        if (tRunning.native != nullptr)
                            p.native = p.program->compile();
                        p.program->set_linker(program.linker(), p.native.get());
                        p.program->set_unit(p.program.get());
                        context = running{ p.program.get(), p.native.get() };
                        if (program.linker() != nullptr)
                            program.linker()->add_unit(linked.first, p.program.get(), p.native.get());
                    }
                    auto const previous = tRunning;
                    tRunning = context;
                    auto* const handle = host_thread_context();
                    tRunning = previous;
                    if (auto const error = host_run(handle, NEOS_RT_VOID, nullptr))
                        throw trap{ error };
                }

                void host_external_memory_at(unsigned aIndex, void* aBase, unsigned long long aSize, unsigned aFlags)
                {
                    if (aIndex < 1u || aIndex > NEOS_RT_EXTERNAL_MEMORIES)
                        return;
                    auto& external = jit::external_memory(aIndex);
                    if (external.fixed)
                        return;     // (code may have its address and size as constants)
                    external.base = static_cast<std::uint8_t*>(aBase);
                    external.size = aSize;
                    external.fixed = (aFlags & NEOS_RT_EXTERNAL_MEMORY_FIXED) != 0u;
                }

                bool run_pending_unit()
                {
                    // the next translation unit after the first that has not yet run (of the running program's linked program), run now 
                    // (its turn brought forward: e.g. a library whose functions the first unit's code calls): false if there is none
                    auto* const linker = tRunning.program != nullptr ? tRunning.program->linker() : nullptr;
                    if (linker == nullptr)
                        return false;
                    for (std::uint32_t u = 0u; u < linker->linked_module_count(); ++u)
                        if (!linker->unit_started(u))
                        {
                            auto const previous = tRunning;
                            struct restore { running previous; ~restore() { tRunning = previous; } } const restoreRunning{ previous };
                            tRunning = running{ linker, linker->linker_native() };
                            link_run(u);
                            return true;
                        }
                    return false;
                }

                void* host_shared_memory(unsigned long long aOffset, unsigned long long aSize)
                {
                    // the shared memory (see neos_rt_host::shared_memory): reserved when a plugin first asks for a range of it; the 
                    // plugins' ranges (offset, size) do not overlap
                    static std::mutex sMutex;
                    static std::uint8_t* sBase = nullptr;
                    static std::map<unsigned long long, unsigned long long> sRanges;
                    std::lock_guard<std::mutex> lock{ sMutex };
                    if (aSize == 0u || aOffset >= NEOS_RT_SHARED_MEMORY_SIZE || aSize > NEOS_RT_SHARED_MEMORY_SIZE - aOffset)
                        return nullptr;
                    auto const next = sRanges.lower_bound(aOffset);
                    if (next != sRanges.end() && next->first < aOffset + aSize)
                        return nullptr;
                    if (next != sRanges.begin() && std::prev(next)->first + std::prev(next)->second > aOffset)
                        return nullptr;
                    if (sBase == nullptr)
                    {
#if defined(_WIN32)
                        sBase = static_cast<std::uint8_t*>(::VirtualAlloc(nullptr, NEOS_RT_SHARED_MEMORY_SIZE, MEM_RESERVE, PAGE_NOACCESS));
#else
                        // (reserved inaccessible: the plugins make their pages accessible as they use them (so that the reservation does 
                        // not count as the process's data))
                        auto const p = ::mmap(nullptr, NEOS_RT_SHARED_MEMORY_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
                        sBase = p == MAP_FAILED ? nullptr : static_cast<std::uint8_t*>(p);
#endif
                        if (sBase == nullptr)
                            return nullptr;
                    }
                    sRanges.emplace(aOffset, aSize);
                    return sBase;
                }

                void host_external_memory(void* aBase, unsigned long long aSize)
                {
                    host_external_memory_at(1u, aBase, aSize, 0u);
                }

                void host_unload(void* aContext)
                {
                    // (a program that load compiled, that is no longer needed: nothing refers to its code (its context is then no longer
                    // valid); not while it runs)
                    if (aContext == nullptr)
                        return;
                    auto const context = *static_cast<running*>(aContext);
                    if (context.program == tRunning.program)
                        return;
                    for (auto* a = tActivation; a != nullptr; a = a->previous)
                        if (a->program == context.program)
                            return;
                    std::unique_ptr<machine> program;
                    jit::compiled_module_ptr native;
                    {
                        std::lock_guard<std::mutex> lock{ loaded_mutex() };
                        auto& programs = loaded_programs();
                        auto p = std::find_if(programs.rbegin(), programs.rend(), [&](loaded_program const& l) { return l.program.get() == context.program; });
                        if (p == programs.rend())
                            return;
                        program = std::move(p->program);
                        native = std::move(p->native);
                        programs.erase(std::next(p).base());
                    }
                    {
                        auto& all = the_thread_contexts();
                        std::lock_guard<std::mutex> lock{ all.mutex };
                        std::erase_if(all.contexts, [&](auto const& c) { return c.first.first == context.program; });
                        all.generation.fetch_add(1u, std::memory_order_release);
                    }
                    native.reset();
                    program.reset();
                }

                int host_restricted()
                {
                    return tRunning.program != nullptr && tRunning.program->restricted() ? 1 : 0;
                }

                void host_trap(char const* aMessage)
                {
                    tTrapRequest = aMessage != nullptr ? aMessage : "trap";
                }

                // --- frames

                void* context_of(machine const* aProgram, jit::compiled_module const* aNative)
                {
                    auto const previous = tRunning;
                    tRunning = running{ const_cast<machine*>(aProgram), const_cast<jit::compiled_module*>(aNative) };
                    auto const result = host_thread_context();
                    tRunning = previous;
                    return result;
                }

                unsigned host_frames(neos_rt_frame* aFrames, unsigned aCount)
                {
                    unsigned count = 0u;
                    auto* a = tActivation;
                    for (auto* c = tCoroutine;;)
                    {
                        for (; a != nullptr; a = a->previous)
                        {
                            if (count == aCount)
                                return count;
                            a->program->walk(*a, [&](std::uint32_t aFunction, std::uint32_t aLine, std::uint64_t aPosition, std::uint64_t aId)
                            {
                                auto const& info = a->program->frame_of(aFunction);
                                if (a->context == nullptr)
                                    a->context = context_of(a->program, a->native);
                                // (its position: if it is on its line)
                                bool const positioned = aPosition != 0u && bytecode::position_line(aPosition) == aLine;
                                aFrames[count++] = neos_rt_frame{ info.name.c_str(), a->program->source_name(info.source).c_str(), aLine, info.firstLine, a->context, aId,
                                    positioned ? bytecode::position_end_line(aPosition) : 0u, positioned ? bytecode::position_column(aPosition) + 1u : 0u,
                                    positioned ? bytecode::position_end_column(aPosition) + 1u : 0u };
                                return count < aCount;
                            });
                        }
                        // (a coroutine's frames: then those of the code that resumed it)
                        if (c == nullptr)
                            break;
                        a = c->resumerActivations;
                        c = c->previous;
                    }
                    return count;
                }

                // (the interpreter's values of activations: their frames' locals and their operand stacks)
                void visit_values(activation const* aActivation, void (*aVisit)(void* aData, unsigned long long const* aValues, size_t aCount), void* aData)
                {
                    static_assert(sizeof(value) == sizeof(unsigned long long));
                    for (auto* a = aActivation; a != nullptr; a = a->previous)
                    {
                        if (a->frames != nullptr)
                            for (auto const& f : *a->frames)
                                if (!f.locals.empty())
                                    aVisit(aData, reinterpret_cast<unsigned long long const*>(f.locals.data()), f.locals.size());
                        if (a->stack != nullptr && !a->stack->empty())
                            aVisit(aData, reinterpret_cast<unsigned long long const*>(a->stack->data()), a->stack->size());
                    }
                }

                unsigned long long* host_global(unsigned aGlobal)
                {
                    if (tRunning.program == nullptr)
                        return nullptr;
                    return reinterpret_cast<unsigned long long*>(tRunning.program->global_address(aGlobal));
                }

                void host_fix_global(unsigned aGlobal)
                {
                    if (tRunning.program != nullptr)
                        tRunning.program->fix_global(aGlobal);
                }

                void host_watched_changed(void const* aAddress)
                {
                    jit::watched_changed(aAddress);
                }

                // --- the debugger (see neos/debug.hpp)

                // (the number of the interpreter's frames that the calling thread is running (across the coroutines that resumed 
                // the one it is running): how deep a step is)
                std::size_t debug_depth()
                {
                    std::size_t depth = 0u;
                    auto* a = tActivation;
                    for (auto* c = tCoroutine;;)
                    {
                        for (; a != nullptr; a = a->previous)
                            if (a->frames != nullptr)
                                depth += a->frames->size();
                        if (c == nullptr)
                            break;
                        a = c->resumerActivations;
                        c = c->previous;
                    }
                    return depth;
                }

                // (a frame of the calling thread, innermost first: its program, its function, the token of the instruction it is 
                // running (or the call it is making) and its locals (the interpreter's))
                struct debug_frame
                {
                    machine* program = nullptr;
                    std::uint32_t function = 0u;
                    std::size_t token = 0u;
                    std::vector<value> const* locals = nullptr;
                };

                std::vector<debug_frame> debug_frames()
                {
                    std::vector<debug_frame> result;
                    auto* a = tActivation;
                    for (auto* c = tCoroutine;;)
                    {
                        for (; a != nullptr; a = a->previous)
                        {
                            if (a->frames == nullptr || a->program == nullptr)
                                continue;
                            auto* const program = const_cast<machine*>(a->program);
                            bool innermost = true;
                            for (auto f = a->frames->rbegin(); f != a->frames->rend(); ++f)
                            {
                                auto const token = innermost ? a->position : f->callSite;
                                innermost = false;
                                auto const& info = f->function < program->frame_count() ? program->frame_of(f->function) : machine::frame_info{};
                                // (not the program's entry point (the compiler's: e.g. "__neos_main__", that calls the top level's code))
                                if (!info.frame || info.name.starts_with("__neos"))
                                    continue;
                                result.push_back(debug_frame{ program, f->function, token, &f->locals });
                            }
                        }
                        if (c == nullptr)
                            break;
                        a = c->resumerActivations;
                        c = c->previous;
                    }
                    return result;
                }

                // (a value as text: shortest round trip for floats; strings quoted and escaped)
                std::string debug_number(double aValue)
                {
                    if (std::isnan(aValue))
                        return "nan";
                    if (std::isinf(aValue))
                        return aValue < 0.0 ? "-inf" : "inf";
                    char buffer[64];
                    auto const [p, ec] = std::to_chars(buffer, buffer + sizeof(buffer), aValue);
                    return std::string{ buffer, ec == std::errc{} ? p : buffer };
                }

                std::string debug_quoted(std::string_view aText, std::size_t aLimit = 1024u)
                {
                    std::string result = "\"";
                    for (std::size_t i = 0u; i < aText.size(); ++i)
                    {
                        if (result.size() > aLimit)
                        {
                            result += "...";
                            break;
                        }
                        auto const c = aText[i];
                        switch (c)
                        {
                        case '"': result += "\\\""; break;
                        case '\\': result += "\\\\"; break;
                        case '\n': result += "\\n"; break;
                        case '\r': result += "\\r"; break;
                        case '\t': result += "\\t"; break;
                        default:
                            if (static_cast<unsigned char>(c) < 0x20u)
                            {
                                char escape[8];
                                std::snprintf(escape, sizeof(escape), "\\x%02x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                                result += escape;
                            }
                            else
                                result += c;
                        }
                    }
                    result += "\"";
                    return result;
                }

                std::string debug_code_point(std::uint32_t aCodePoint)
                {
                    std::string utf8;
                    if (aCodePoint < 0x80u)
                        utf8 += static_cast<char>(aCodePoint);
                    else if (aCodePoint < 0x800u)
                    {
                        utf8 += static_cast<char>(0xC0u | (aCodePoint >> 6u));
                        utf8 += static_cast<char>(0x80u | (aCodePoint & 0x3Fu));
                    }
                    else if (aCodePoint < 0x10000u)
                    {
                        utf8 += static_cast<char>(0xE0u | (aCodePoint >> 12u));
                        utf8 += static_cast<char>(0x80u | ((aCodePoint >> 6u) & 0x3Fu));
                        utf8 += static_cast<char>(0x80u | (aCodePoint & 0x3Fu));
                    }
                    else if (aCodePoint < 0x110000u)
                    {
                        utf8 += static_cast<char>(0xF0u | (aCodePoint >> 18u));
                        utf8 += static_cast<char>(0x80u | ((aCodePoint >> 12u) & 0x3Fu));
                        utf8 += static_cast<char>(0x80u | ((aCodePoint >> 6u) & 0x3Fu));
                        utf8 += static_cast<char>(0x80u | (aCodePoint & 0x3Fu));
                    }
                    std::string result = "'";
                    result += aCodePoint == '\'' ? "\\'" : aCodePoint == '\\' ? "\\\\" : utf8;
                    result += "'";
                    return std::to_string(aCodePoint) + " " + result;
                }

                // (a WebAssembly value type's name: a local that the compiler did not describe)
                char const* debug_wasm_type(std::uint8_t aType)
                {
                    switch (aType)
                    {
                    case 0x7F: return "i32";
                    case 0x7E: return "i64";
                    case 0x7D: return "f32";
                    case 0x7C: return "f64";
                    default: return "?";
                    }
                }

                // (a value's truth, as its text shows it: e.g. a breakpoint's condition)
                bool debug_true(std::string const& aText)
                {
                    static std::set<std::string> const sFalse{ "", "0", "-0", "0.0", "-0.0", "false", "False", "FALSE", "nil", "None", "null", "undefined", "nan", "\"\"", "''" };
                    return sFalse.find(aText) == sFalse.end();
                }

                // a stopped program, as the thread that stopped sees it (see debug::i_stop): the program's other threads are parked 
                // (see debug::session::park), so what is asked of them runs on them (debug::session::run_on)
                class debug_stop : public debug::i_stop
                {
                private:
                    // (a value that has parts: where its parts are found)
                    struct held_value
                    {
                        std::uint32_t thread = 0u;
                        machine* program = nullptr;
                        std::string language;
                        std::string type;
                        value word = 0u;
                    };
                public:
                    debug_stop(debug::session& aSession, debug::stop_reason aReason, std::optional<std::uint32_t> aBreakpoint, std::string aMessage) :
                        iSession{ aSession }, iReason{ aReason }, iBreakpoint{ aBreakpoint }, iMessage{ std::move(aMessage) }, iThread{ aSession.thread_id() }
                    {
                    }
                public:
                    debug::stop_reason reason() const override
                    {
                        return iReason;
                    }
                    std::string const& message() const override
                    {
                        return iMessage;
                    }
                    std::optional<std::uint32_t> breakpoint() const override
                    {
                        return iBreakpoint;
                    }
                    std::uint32_t thread() const override
                    {
                        return iThread;
                    }
                    std::vector<debug::thread_info> threads() override
                    {
                        std::vector<debug::thread_info> result;
                        auto add = [&](std::uint32_t aId, std::string aState)
                        {
                            debug::thread_info t;
                            t.id = aId;
                            t.state = std::move(aState);
                            auto const f = frames(aId);
                            if (!f.empty())
                                t.top = f.front();
                            result.push_back(std::move(t));
                        };
                        add(iThread, "stopped");
                        for (auto const id : iSession.parked_threads())
                            if (id != iThread)
                                add(id, "stopped");
                        std::sort(result.begin(), result.end(), [](auto const& lhs, auto const& rhs) { return lhs.id < rhs.id; });
                        return result;
                    }
                    std::vector<debug::frame> frames(std::optional<std::uint32_t> aThread) override
                    {
                        std::vector<debug::frame> result;
                        on(aThread, [&]()
                        {
                            for (auto const& f : debug_frames())
                                result.push_back(describe(f));
                        });
                        return result;
                    }
                    std::vector<debug::variable> locals(std::size_t aFrame, std::optional<std::uint32_t> aThread) override
                    {
                        std::vector<debug::variable> result;
                        for (auto& [v, flags] : frame_variables(aFrame, aThread))
                            if ((flags & bytecode::VariableHidden) == 0u)
                                result.push_back(std::move(v));
                        return result;
                    }
                    std::vector<debug::variable> globals(std::size_t aFrame, std::optional<std::uint32_t> aThread) override
                    {
                        // (the module's globals, then the names of the frame's scopes (see VariableScope))
                        std::vector<debug::variable> result;
                        auto const thread = aThread.value_or(iThread);
                        on(aThread, [&]()
                        {
                            auto const frames = debug_frames();
                            if (aFrame >= frames.size())
                                return;
                            auto& program = *frames[aFrame].program;
                            for (auto const& v : program.global_variables())
                            {
                                if ((v.flags & bytecode::VariableHidden) != 0u)
                                    continue;
                                if (v.storage == bytecode::VariableAddress)
                                    result.push_back(make_variable(v.name, thread, program, v.language, v.type, v.index));
                                else if (v.storage == bytecode::VariableGlobal)
                                    if (auto const* g = program.debug_global(v.index))
                                        result.push_back(make_variable(v.name, thread, program, v.language, v.type, *g));
                            }
                        });
                        for (auto& v : module_variables(aFrame, aThread))
                            if (std::none_of(result.begin(), result.end(), [&](auto const& e) { return e.name == v.name; }))
                                result.push_back(std::move(v));
                        for (auto const& [v, flags] : frame_variables(aFrame, aThread))
                            if ((flags & bytecode::VariableScope) != 0u && v.reference != 0u)
                                for (auto& part : parts(v.reference, 0u, ~std::size_t{ 0u }))
                                    if (std::none_of(result.begin(), result.end(), [&](auto const& e) { return e.name == part.name; }))
                                        result.push_back(std::move(part));
                        return result;
                    }
                    std::vector<debug::variable> parts(std::uint64_t aReference, std::size_t aStart, std::size_t aCount) override
                    {
                        std::vector<debug::variable> result;
                        if (aReference == 0u || aReference > iValues.size())
                            return result;
                        auto const held = iValues[aReference - 1u];
                        if (held.language.empty())
                        {
                            // (the VM's: an object in memory, or a pointer)
                            std::size_t index = 0u;
                            memory_parts(*held.program, held.type, held.word, [&](std::string const& aName, std::string const& aType, value aWord)
                            {
                                if (index++ < aStart)
                                    return true;
                                if (result.size() == aCount)
                                    return false;
                                result.push_back(make_variable(aName, held.thread, *held.program, {}, aType, aWord));
                                return true;
                            });
                            return result;
                        }
                        on(held.thread, [&]()
                        {
                            auto const* hooks = static_cast<neos_rt_debug const*>(runtime::debug_hooks(held.language));
                            if (hooks == nullptr || hooks->parts == nullptr)
                                return;
                            std::vector<neos_rt_debug_part> parts;
                            std::size_t const batch = 64u;
                            for (std::size_t start = aStart; aCount > result.size();)
                            {
                                parts.assign(std::min(batch, aCount - result.size()), neos_rt_debug_part{});
                                auto const count = hooks->parts(held.type.c_str(), held.word, start, parts.size(), parts.data());
                                // (the parts' strings are valid only until the hooks are next called: copied first)
                                std::vector<std::tuple<std::string, std::string, value>> copies;
                                for (std::size_t i = 0u; i < count && i < parts.size(); ++i)
                                    copies.emplace_back(parts[i].name != nullptr ? parts[i].name : "", parts[i].type != nullptr ? parts[i].type : "", parts[i].value);
                                for (auto const& [name, type, word] : copies)
                                    result.push_back(make_variable(name, held.thread, *held.program, held.language, type, word));
                                if (count < parts.size())
                                    break;
                                start += count;
                            }
                        });
                        return result;
                    }
                    std::optional<debug::variable> evaluate(std::size_t aFrame, std::string const& aExpression, std::string& aError, std::optional<std::uint32_t> aThread) override
                    {
                        // (a variable or a part of one; else an expression of them (see expression))
                        if (parse_path(aExpression))
                            return evaluate_path(aFrame, aExpression, aError, aThread);
                        return expression(aFrame, aExpression, aError, aThread);
                    }
                private:
                    // (a variable or a part of one: a name, then .<name>, [<index>] or ["<key>"] for each part)
                    std::optional<debug::variable> evaluate_path(std::size_t aFrame, std::string const& aExpression, std::string& aError, std::optional<std::uint32_t> aThread)
                    {
                        auto const path = parse_path(aExpression);
                        if (!path)
                        {
                            aError = "Cannot evaluate \"" + aExpression + "\": expected a variable or a part of one (e.g. a.b[2])";
                            return {};
                        }
                        // (a name as it is, else ignoring case (as gdb's: e.g. Ada's, whose names are case insensitive))
                        auto const same = [](std::string_view aName, std::string_view aWanted, bool aIgnoreCase)
                        {
                            return aIgnoreCase ? 
                                aName.size() == aWanted.size() && std::equal(aName.begin(), aName.end(), aWanted.begin(), [](char a, char b) 
                                    { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }) :
                                aName == aWanted;
                        };
                        std::optional<debug::variable> current;
                        for (bool const ignoreCase : { false, true })
                        {
                            for (auto const& [v, flags] : frame_variables(aFrame, aThread))
                                if (same(v.name, path->front(), ignoreCase))
                                    current = v;
                            if (!current)
                                for (auto const& v : globals(aFrame, aThread))
                                    if (same(v.name, path->front(), ignoreCase))
                                        current = v;
                            if (!current)
                                for (auto const& v : module_variables(aFrame, aThread))
                                    if (same(v.name, path->front(), ignoreCase))
                                        current = v;
                            if (current)
                                break;
                        }
                        if (!current)
                        {
                            aError = "No symbol \"" + path->front() + "\" in current context.";
                            return {};
                        }
                        auto name = path->front();
                        for (std::size_t i = 1u; i < path->size(); ++i)
                        {
                            auto const& part = (*path)[i];
                            std::optional<debug::variable> found;
                            if (current->reference != 0u)
                            {
                                auto const allParts = parts(current->reference, 0u, ~std::size_t{ 0u });
                                for (auto const& p : allParts)
                                    if (p.name == part || "[" + part + "]" == p.name || "." + part == p.name || "[" + debug_quoted(part) + "]" == p.name || 
                                        "'" + part + "'" == p.name || debug_quoted(part) == p.name)
                                    {
                                        found = p;
                                        break;
                                    }
                                if (!found)
                                    for (auto const& p : allParts)
                                        if (same(p.name, part, true) || same(p.name, "." + part, true))
                                        {
                                            found = p;
                                            break;
                                        }
                            }
                            if (!found)
                            {
                                aError = "\"" + name + "\" has no part \"" + part + "\".";
                                return {};
                            }
                            name += part.front() == '[' ? part : "." + part;
                            current = found;
                        }
                        current->name = aExpression;
                        return current;
                    }
                    // an expression of variables (and parts of them) and literals, as the debugger evaluates it (the program's code does not 
                    // run): numbers, strings ("..." or '...'), true, false, nil (None, null, undefined); unary - and ! (not); * / % + - (strings: 
                    // + concatenates); < <= > >= == != (= /= === !== ~=); && (and) || (or); parentheses. A value is its text as the variable shows it: a number,
                    // a quoted string, a boolean or nil; anything else compares by its text
                    struct operand
                    {
                        enum class kind { Number, String, Boolean, Nil, Other } k = kind::Nil;
                        double number = 0.0;
                        bool integral = false;
                        std::int64_t integer = 0;
                        std::string text;
                        bool boolean = false;
                    };
                    static operand operand_of(debug::variable const& aVariable)
                    {
                        operand result;
                        auto const& v = aVariable.value;
                        if (v == "true" || v == "True" || v == "TRUE")
                        {
                            result.k = operand::kind::Boolean;
                            result.boolean = true;
                            return result;
                        }
                        if (v == "false" || v == "False" || v == "FALSE")
                        {
                            result.k = operand::kind::Boolean;
                            return result;
                        }
                        if (v == "nil" || v == "None" || v == "null" || v == "undefined" || v == "<no value>")
                            return result;
                        if (v.size() >= 2u && (v.front() == '"' || v.front() == '\'') && v.back() == v.front())
                        {
                            result.k = operand::kind::String;
                            for (std::size_t i = 1u; i + 1u < v.size(); ++i)
                            {
                                if (v[i] == '\\' && i + 2u < v.size())
                                {
                                    ++i;
                                    switch (v[i])
                                    {
                                    case 'n': result.text += '\n'; break;
                                    case 't': result.text += '\t'; break;
                                    case 'r': result.text += '\r'; break;
                                    default: result.text += v[i]; break;
                                    }
                                }
                                else
                                    result.text += v[i];
                            }
                            return result;
                        }
                        // (a number: its text, up to a space (e.g. a C char's "65 'A'"))
                        auto const number = v.substr(0u, v.find(' '));
                        std::int64_t i = 0;
                        if (auto const [p, ec] = std::from_chars(number.data(), number.data() + number.size(), i); ec == std::errc{} && p == number.data() + number.size() && !number.empty())
                        {
                            result.k = operand::kind::Number;
                            result.integral = true;
                            result.integer = i;
                            result.number = static_cast<double>(i);
                            return result;
                        }
                        double d = 0.0;
                        if (auto const [p, ec] = std::from_chars(number.data(), number.data() + number.size(), d); ec == std::errc{} && p == number.data() + number.size() && !number.empty())
                        {
                            result.k = operand::kind::Number;
                            result.number = d;
                            return result;
                        }
                        result.k = operand::kind::Other;
                        result.text = v;
                        return result;
                    }
                    static std::string text_of(operand const& aOperand)
                    {
                        switch (aOperand.k)
                        {
                        case operand::kind::Number: return aOperand.integral ? std::to_string(aOperand.integer) : debug_number(aOperand.number);
                        case operand::kind::String: return debug_quoted(aOperand.text);
                        case operand::kind::Boolean: return aOperand.boolean ? "true" : "false";
                        case operand::kind::Nil: return "nil";
                        default: return aOperand.text;
                        }
                    }
                    static bool truth(operand const& aOperand)
                    {
                        switch (aOperand.k)
                        {
                        case operand::kind::Number: return aOperand.number != 0.0;
                        case operand::kind::String: return !aOperand.text.empty();
                        case operand::kind::Boolean: return aOperand.boolean;
                        case operand::kind::Nil: return false;
                        default: return debug_true(aOperand.text);
                        }
                    }
                    std::optional<debug::variable> expression(std::size_t aFrame, std::string const& aExpression, std::string& aError, std::optional<std::uint32_t> aThread)
                    {
                        // (its tokens)
                        struct token
                        {
                            enum class kind { Operand, Operator, Open, Close } k;
                            std::string text;
                        };
                        std::vector<token> tokens;
                        std::size_t i = 0u;
                        auto const n = aExpression.size();
                        auto const is_name = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' || static_cast<unsigned char>(c) >= 0x80u; };
                        while (i < n)
                        {
                            auto const c = aExpression[i];
                            if (std::isspace(static_cast<unsigned char>(c)))
                            {
                                ++i;
                                continue;
                            }
                            if (c == '(' || c == ')')
                            {
                                tokens.push_back({ c == '(' ? token::kind::Open : token::kind::Close, std::string(1u, c) });
                                ++i;
                                continue;
                            }
                            if (c == '"' || c == '\'')
                            {
                                auto const start = i++;
                                while (i < n && aExpression[i] != c)
                                    i += aExpression[i] == '\\' ? 2u : 1u;
                                if (i >= n)
                                {
                                    aError = "Unterminated string in \"" + aExpression + "\".";
                                    return {};
                                }
                                ++i;
                                tokens.push_back({ token::kind::Operand, aExpression.substr(start, i - start) });
                                continue;
                            }
                            if (std::isdigit(static_cast<unsigned char>(c)) || (c == '.' && i + 1u < n && std::isdigit(static_cast<unsigned char>(aExpression[i + 1u]))))
                            {
                                auto const start = i;
                                while (i < n && (std::isalnum(static_cast<unsigned char>(aExpression[i])) || aExpression[i] == '.' || 
                                    ((aExpression[i] == '+' || aExpression[i] == '-') && (aExpression[i - 1u] == 'e' || aExpression[i - 1u] == 'E'))))
                                    ++i;
                                tokens.push_back({ token::kind::Operand, aExpression.substr(start, i - start) });
                                continue;
                            }
                            if (is_name(c))
                            {
                                // (a variable, or a part of one: the longest path; an index in [] or, right after a name or another
                                // index, in () (e.g. Ada's: there are no calls here))
                                auto const start = i;
                                std::string closers;
                                while (i < n)
                                {
                                    auto const ch = aExpression[i];
                                    if (!closers.empty())
                                    {
                                        if (ch == closers.back())
                                            closers.pop_back();
                                        else if (ch == '[')
                                            closers.push_back(']');
                                        ++i;
                                        continue;
                                    }
                                    auto const sofar = std::string_view{ aExpression }.substr(start, i - start);
                                    if (ch == '[' || (ch == '(' && sofar != "and" && sofar != "or" && sofar != "not" && (is_name(aExpression[i - 1u]) || aExpression[i - 1u] == ']' || aExpression[i - 1u] == ')')))
                                        closers.push_back(ch == '[' ? ']' : ')');
                                    else if (!is_name(ch) && ch != '.')
                                        break;
                                    ++i;
                                }
                                auto const word = aExpression.substr(start, i - start);
                                if (word == "and" || word == "or" || word == "not")
                                    tokens.push_back({ token::kind::Operator, word == "and" ? "&&" : word == "or" ? "||" : "!" });
                                else
                                    tokens.push_back({ token::kind::Operand, word });
                                continue;
                            }
                            static char const* const sOperators[] = { "===", "!==", "&&", "||", "==", "!=", "~=", "/=", "<=", ">=", "<", ">", "=", "+", "-", "*", "/", "%", "!" };
                            bool found = false;
                            for (auto const* op : sOperators)
                                if (aExpression.compare(i, std::strlen(op), op) == 0)
                                {
                                    std::string text = op;
                                    // (equality as languages write it: there is no assignment here, so Ada's (and Pascal's) = and /= too)
                                    if (text == "===" || text == "=")
                                        text = "==";
                                    else if (text == "!==" || text == "~=" || text == "/=")
                                        text = "!=";
                                    tokens.push_back({ token::kind::Operator, text });
                                    i += std::strlen(op);
                                    found = true;
                                    break;
                                }
                            if (!found)
                            {
                                aError = "Cannot evaluate \"" + aExpression + "\": unexpected '" + std::string(1u, c) + "'.";
                                return {};
                            }
                        }
                        // (precedence climbing)
                        std::size_t at = 0u;
                        std::string error;
                        std::function<std::optional<operand>(int)> parse;
                        auto const primary = [&]() -> std::optional<operand>
                        {
                            if (at >= tokens.size())
                            {
                                error = "Cannot evaluate \"" + aExpression + "\": incomplete expression.";
                                return {};
                            }
                            auto const t = tokens[at++];
                            if (t.k == token::kind::Open)
                            {
                                auto inner = parse(0);
                                if (!inner)
                                    return {};
                                if (at >= tokens.size() || tokens[at].k != token::kind::Close)
                                {
                                    error = "Cannot evaluate \"" + aExpression + "\": expected ')'.";
                                    return {};
                                }
                                ++at;
                                return inner;
                            }
                            if (t.k == token::kind::Operator && (t.text == "-" || t.text == "!"))
                            {
                                auto inner = parse(7);
                                if (!inner)
                                    return {};
                                operand result;
                                if (t.text == "!")
                                {
                                    result.k = operand::kind::Boolean;
                                    result.boolean = !truth(*inner);
                                    return result;
                                }
                                if (inner->k != operand::kind::Number)
                                {
                                    error = "Cannot negate " + text_of(*inner) + ".";
                                    return {};
                                }
                                result = *inner;
                                result.number = -result.number;
                                result.integer = -result.integer;
                                return result;
                            }
                            if (t.k != token::kind::Operand)
                            {
                                error = "Cannot evaluate \"" + aExpression + "\": unexpected '" + t.text + "'.";
                                return {};
                            }
                            // (a literal, or a variable's value)
                            static std::set<std::string> const sTrue{ "true", "True" };
                            static std::set<std::string> const sFalse{ "false", "False" };
                            static std::set<std::string> const sNil{ "nil", "None", "null", "undefined" };
                            debug::variable literal;
                            if (std::isdigit(static_cast<unsigned char>(t.text[0])) || t.text[0] == '.' || t.text[0] == '"' || t.text[0] == '\'' || 
                                sTrue.contains(t.text) || sFalse.contains(t.text) || sNil.contains(t.text))
                            {
                                literal.value = t.text;
                                auto result = operand_of(literal);
                                if (result.k == operand::kind::Other)
                                {
                                    error = "Invalid number \"" + t.text + "\".";
                                    return {};
                                }
                                return result;
                            }
                            auto const v = evaluate_path(aFrame, t.text, error, aThread);
                            if (!v)
                            {
                                // (not a variable: a literal of an enumeration type (whose values show as their literals))
                                std::optional<std::string> enumLiteral;
                                if (auto const* program = frame_program(aFrame, aThread); program != nullptr && parse_path(t.text) && parse_path(t.text)->size() == 1u)
                                    enumLiteral = program->debug_enum_literal(t.text);
                                if (!enumLiteral)
                                    return {};
                                error.clear();
                                debug::variable literalValue;
                                literalValue.value = *enumLiteral;
                                return operand_of(literalValue);
                            }
                            return operand_of(*v);
                        };
                        auto const precedence = [](std::string const& aOperator)
                        {
                            if (aOperator == "||") return 1;
                            if (aOperator == "&&") return 2;
                            if (aOperator == "==" || aOperator == "!=") return 3;
                            if (aOperator == "<" || aOperator == "<=" || aOperator == ">" || aOperator == ">=") return 4;
                            if (aOperator == "+" || aOperator == "-") return 5;
                            if (aOperator == "*" || aOperator == "/" || aOperator == "%") return 6;
                            return -1;
                        };
                        auto const apply = [&](std::string const& aOperator, operand const& aLeft, operand const& aRight) -> std::optional<operand>
                        {
                            operand result;
                            if (aOperator == "&&" || aOperator == "||")
                            {
                                result.k = operand::kind::Boolean;
                                result.boolean = aOperator == "&&" ? truth(aLeft) && truth(aRight) : truth(aLeft) || truth(aRight);
                                return result;
                            }
                            if (aOperator == "==" || aOperator == "!=")
                            {
                                bool equal = false;
                                if (aLeft.k == operand::kind::Number && aRight.k == operand::kind::Number)
                                    equal = aLeft.integral && aRight.integral ? aLeft.integer == aRight.integer : aLeft.number == aRight.number;
                                else if (aLeft.k == aRight.k)
                                    equal = aLeft.k == operand::kind::Boolean ? aLeft.boolean == aRight.boolean : aLeft.k == operand::kind::Nil || aLeft.text == aRight.text;
                                result.k = operand::kind::Boolean;
                                result.boolean = aOperator == "==" ? equal : !equal;
                                return result;
                            }
                            if (aLeft.k == operand::kind::String && aRight.k == operand::kind::String)
                            {
                                if (aOperator == "+")
                                {
                                    result.k = operand::kind::String;
                                    result.text = aLeft.text + aRight.text;
                                    return result;
                                }
                                if (precedence(aOperator) == 4)
                                {
                                    auto const c = aLeft.text.compare(aRight.text);
                                    result.k = operand::kind::Boolean;
                                    result.boolean = aOperator == "<" ? c < 0 : aOperator == "<=" ? c <= 0 : aOperator == ">" ? c > 0 : c >= 0;
                                    return result;
                                }
                            }
                            if (aLeft.k != operand::kind::Number || aRight.k != operand::kind::Number)
                            {
                                error = "Cannot apply '" + aOperator + "' to " + text_of(aLeft) + " and " + text_of(aRight) + ".";
                                return {};
                            }
                            if (precedence(aOperator) == 4)
                            {
                                result.k = operand::kind::Boolean;
                                auto const l = aLeft.number;
                                auto const r = aRight.number;
                                result.boolean = aOperator == "<" ? l < r : aOperator == "<=" ? l <= r : aOperator == ">" ? l > r : l >= r;
                                return result;
                            }
                            result.k = operand::kind::Number;
                            bool const integral = aLeft.integral && aRight.integral && aOperator != "/";
                            if (integral)
                            {
                                if (aOperator == "%" && aRight.integer == 0)
                                {
                                    error = "Division by zero.";
                                    return {};
                                }
                                result.integral = true;
                                result.integer = aOperator == "+" ? aLeft.integer + aRight.integer : aOperator == "-" ? aLeft.integer - aRight.integer :
                                    aOperator == "*" ? aLeft.integer * aRight.integer : aLeft.integer % aRight.integer;
                                result.number = static_cast<double>(result.integer);
                                return result;
                            }
                            result.number = aOperator == "+" ? aLeft.number + aRight.number : aOperator == "-" ? aLeft.number - aRight.number :
                                aOperator == "*" ? aLeft.number * aRight.number : aOperator == "/" ? aLeft.number / aRight.number : std::fmod(aLeft.number, aRight.number);
                            return result;
                        };
                        parse = [&](int aMinimum) -> std::optional<operand>
                        {
                            auto left = primary();
                            if (!left)
                                return {};
                            while (at < tokens.size() && tokens[at].k == token::kind::Operator)
                            {
                                auto const op = tokens[at].text;
                                auto const p = precedence(op);
                                if (p < 0 || p < aMinimum)
                                    break;
                                ++at;
                                auto right = parse(p + 1);
                                if (!right)
                                    return {};
                                left = apply(op, *left, *right);
                                if (!left)
                                    return {};
                            }
                            return left;
                        };
                        auto const result = parse(0);
                        if (!result)
                        {
                            aError = error;
                            return {};
                        }
                        if (at != tokens.size())
                        {
                            aError = "Cannot evaluate \"" + aExpression + "\": unexpected '" + tokens[at].text + "'.";
                            return {};
                        }
                        debug::variable v;
                        v.name = aExpression;
                        v.value = text_of(*result);
                        v.type = result->k == operand::kind::Number ? "number" : result->k == operand::kind::String ? "string" : 
                            result->k == operand::kind::Boolean ? "bool" : result->k == operand::kind::Nil ? "nil" : "";
                        return v;
                    }
                private:
                    // (the variables of the top level of a frame's module (see VariableModule): those of the outermost frame of its program that has 
                    // them, if it is not the frame itself)
                    std::vector<debug::variable> module_variables(std::size_t aFrame, std::optional<std::uint32_t> aThread)
                    {
                        std::optional<std::size_t> outer;
                        on(aThread, [&]()
                        {
                            auto const frames = debug_frames();
                            if (aFrame >= frames.size())
                                return;
                            for (auto i = frames.size(); i-- > aFrame + 1u;)
                                if (frames[i].program == frames[aFrame].program)
                                    if (auto const* variables = frames[i].program->variables_of(frames[i].function); variables != nullptr &&
                                        std::any_of(variables->begin(), variables->end(), [](auto const& v) { return (v.flags & bytecode::VariableModule) != 0u; }))
                                    {
                                        outer = i;
                                        return;
                                    }
                        });
                        std::vector<debug::variable> result;
                        if (outer)
                            for (auto& [v, flags] : frame_variables(*outer, aThread))
                                if ((flags & bytecode::VariableModule) != 0u)
                                    result.push_back(std::move(v));
                        return result;
                    }
                    // (the variables that a frame can see (its parameters first) with their flags (see VariablesSection): of those of the 
                    // same name, the innermost (declared last))
                    // (the program whose code a frame is)
                    machine const* frame_program(std::size_t aFrame, std::optional<std::uint32_t> aThread)
                    {
                        machine const* result = nullptr;
                        on(aThread, [&]()
                        {
                            auto const frames = debug_frames();
                            if (aFrame < frames.size())
                                result = frames[aFrame].program;
                        });
                        return result;
                    }
                    std::vector<std::pair<debug::variable, std::uint8_t>> frame_variables(std::size_t aFrame, std::optional<std::uint32_t> aThread)
                    {
                        std::vector<std::pair<debug::variable, std::uint8_t>> result;
                        auto const thread = aThread.value_or(iThread);
                        on(aThread, [&]()
                        {
                            auto const frames = debug_frames();
                            if (aFrame >= frames.size())
                                return;
                            auto const& f = frames[aFrame];
                            auto& program = *f.program;
                            auto const* variables = program.variables_of(f.function);
                            if (variables == nullptr)
                            {
                                if (program.has_variables())
                                    return;
                                // (the compiler did not describe the function's variables: its locals, by their indices (its parameters first))
                                auto const& type = program.type_of(f.function);
                                auto const& localTypes = program.local_types(f.function);
                                for (std::size_t i = 0u; f.locals != nullptr && i < f.locals->size(); ++i)
                                {
                                    bool const parameter = i < type.parameters.size();
                                    auto const wasmType = parameter ? type.parameters[i] : i - type.parameters.size() < localTypes.size() ? 
                                        localTypes[i - type.parameters.size()] : std::uint8_t{ 0u };
                                    auto v = make_variable("$" + std::to_string(i), thread, program, {}, debug_wasm_type(wasmType), (*f.locals)[i]);
                                    v.argument = parameter;
                                    result.emplace_back(std::move(v), parameter ? bytecode::VariableParameter : std::uint8_t{ 0u });
                                }
                                return;
                            }
                            auto const line = program.line_at(f.function, f.token);
                            std::vector<machine::variable_info const*> visible;
                            for (auto const& v : *variables)
                            {
                                if (v.firstLine != 0u && line != 0u && (line < v.firstLine || (v.lastLine != 0u && line > v.lastLine)))
                                    continue;
                                auto existing = std::find_if(visible.begin(), visible.end(), [&](auto const* e) { return e->name == v.name; });
                                if (existing == visible.end())
                                    visible.push_back(&v);
                                else if (v.firstLine >= (*existing)->firstLine)
                                    *existing = &v;
                            }
                            std::stable_partition(visible.begin(), visible.end(), [](auto const* v) { return (v->flags & bytecode::VariableParameter) != 0u; });
                            for (auto const* v : visible)
                            {
                                std::optional<value> word;
                                if (v->storage == bytecode::VariableLocal && f.locals != nullptr && v->index < f.locals->size())
                                    word = (*f.locals)[v->index];
                                else if (v->storage == bytecode::VariableAddress)
                                    word = v->index;
                                else if (v->storage == bytecode::VariableGlobal)
                                    if (auto const* g = program.debug_global(v->index))
                                        word = *g;
                                if (!word)
                                    continue;
                                auto variable = make_variable(v->name, thread, program, v->language, v->type, *word);
                                variable.argument = (v->flags & bytecode::VariableParameter) != 0u;
                                result.emplace_back(std::move(variable), v->flags);
                            }
                        });
                        return result;
                    }
                    // (the VM's types of objects in memory (see VariablesSection): a scalar's size; the top level parts of a structure's 
                    // members; reading a scalar)
                    static std::size_t scalar_size(std::string_view aType)
                    {
                        if (aType.starts_with("enum"))
                        {
                            auto rest = aType.substr(4u);
                            return static_cast<std::size_t>(parse_number(rest));
                        }
                        if (aType == "i8" || aType == "u8" || aType == "cchar" || aType == "bool")
                            return 1u;
                        if (aType == "i16" || aType == "u16")
                            return 2u;
                        if (aType == "i32" || aType == "u32" || aType == "f32" || aType == "char")
                            return 4u;
                        if (aType == "i64" || aType == "u64" || aType == "f64" || aType.starts_with('*'))
                            return 8u;
                        return 0u;
                    }
                    static std::vector<std::string_view> split_members(std::string_view aMembers)
                    {
                        std::vector<std::string_view> result;
                        int depth = 0;
                        std::size_t start = 0u;
                        for (std::size_t i = 0u; i < aMembers.size(); ++i)
                        {
                            auto const c = aMembers[i];
                            if (c == '{' || c == '[')
                                ++depth;
                            else if (c == '}' || c == ']')
                                --depth;
                            else if (c == ';' && depth == 0)
                            {
                                if (i > start)
                                    result.push_back(aMembers.substr(start, i - start));
                                start = i + 1u;
                            }
                        }
                        if (start < aMembers.size())
                            result.push_back(aMembers.substr(start));
                        return result;
                    }
                    static std::uint64_t parse_number(std::string_view& aText)
                    {
                        std::uint64_t result = 0u;
                        bool negative = !aText.empty() && aText.front() == '-';
                        if (negative)
                            aText.remove_prefix(1u);
                        while (!aText.empty() && std::isdigit(static_cast<unsigned char>(aText.front())))
                        {
                            result = result * 10u + static_cast<std::uint64_t>(aText.front() - '0');
                            aText.remove_prefix(1u);
                        }
                        return negative ? static_cast<std::uint64_t>(-static_cast<std::int64_t>(result)) : result;
                    }
                    // (an object at an address (type "@<offset>:<type>"): its type and address; or a value of a scalar or pointer type as is)
                    // (a type's memory (a prefix "#<memory>:": an external memory's index; none: the program's own memory, 0): removed)
                    static std::uint32_t memory_of(std::string_view& aType)
                    {
                        if (!aType.starts_with('#'))
                            return 0u;
                        auto rest = aType.substr(1u);
                        auto const memory = static_cast<std::uint32_t>(parse_number(rest));
                        if (!rest.starts_with(':'))
                            return 0u;
                        aType = rest.substr(1u);
                        return memory;
                    }
                    static std::string in_memory(std::uint32_t aMemory, std::string_view aType)
                    {
                        return (aMemory != 0u ? "#" + std::to_string(aMemory) + ":" : std::string{}) + std::string{ aType };
                    }
                    static bool located(machine const& aProgram, std::string_view aType, value aWord, std::string& aObjectType, std::uint64_t& aAddress, 
                        std::uint32_t& aMemory)
                    {
                        aType = aProgram.debug_named_type(aType);
                        aMemory = memory_of(aType);
                        aType = aProgram.debug_named_type(aType);
                        if (!aType.starts_with('@'))
                            return false;
                        aType.remove_prefix(1u);
                        auto const offset = parse_number(aType);
                        if (!aType.starts_with(':'))
                            return false;
                        aType.remove_prefix(1u);
                        aObjectType = std::string{ aProgram.debug_named_type(aType) };
                        aAddress = aWord + offset;
                        return true;
                    }
                    static std::optional<value> read_scalar(machine const& aProgram, std::uint32_t aMemory, std::string_view aType, std::uint64_t aAddress)
                    {
                        auto const size = scalar_size(aType);
                        if (size == 0u)
                            return {};
                        std::uint64_t bits = 0u;
                        if (!aProgram.debug_read(aMemory, aAddress, &bits, size))
                            return {};
                        if (aType == "f32")
                            return static_cast<value>(static_cast<std::uint32_t>(bits));
                        return bits;
                    }
                    // (an array object of type "[<count>*<size>]<type>" (its elements at its address, indexed from 0) or "[@<dimensions>*<size>]<type>" 
                    // (at its address a descriptor: its elements' offset from it, then each dimension's first and last index (i64s)): its elements' 
                    // address, its dimensions (first index and length), its elements' size and type)
                    struct array_shape
                    {
                        std::uint64_t elements = 0u;
                        std::vector<std::pair<std::int64_t, std::uint64_t>> dimensions;
                        std::uint64_t size = 0u;
                        std::string type;
                        bool described = false;
                        std::uint64_t count() const
                        {
                            std::uint64_t result = 1u;
                            for (auto const& d : dimensions)
                                result *= d.second;
                            return result;
                        }
                        std::string index(std::uint64_t aElement) const
                        {
                            // (its index in each dimension (the last varies fastest))
                            std::vector<std::int64_t> indexes(dimensions.size());
                            for (std::size_t d = dimensions.size(); d-- > 0u;)
                            {
                                auto const length = std::max<std::uint64_t>(dimensions[d].second, 1u);
                                indexes[d] = dimensions[d].first + static_cast<std::int64_t>(aElement % length);
                                aElement /= length;
                            }
                            std::string result = "[";
                            for (std::size_t d = 0u; d < indexes.size(); ++d)
                                result += (d != 0u ? "," : "") + std::to_string(indexes[d]);
                            return result + "]";
                        }
                    };
                    static std::optional<array_shape> array_at(machine const& aProgram, std::uint32_t aMemory, std::string_view aType, std::uint64_t aAddress)
                    {
                        if (!aType.starts_with('['))
                            return {};
                        aType.remove_prefix(1u);
                        array_shape result;
                        result.described = aType.starts_with('@');
                        if (result.described)
                            aType.remove_prefix(1u);
                        auto const number = parse_number(aType);
                        if (!aType.starts_with('*'))
                            return {};
                        aType.remove_prefix(1u);
                        result.size = parse_number(aType);
                        if (!aType.starts_with(']'))
                            return {};
                        aType.remove_prefix(1u);
                        result.type = std::string{ aProgram.debug_named_type(aType) };
                        if (!result.described)
                        {
                            result.elements = aAddress;
                            result.dimensions.emplace_back(0, number);
                            return result;
                        }
                        if (number == 0u || number > 8u || !aProgram.debug_read(aMemory, aAddress, &result.elements, 8u))
                            return {};
                        result.elements += aAddress;
                        for (std::uint64_t d = 0u; d < number; ++d)
                        {
                            std::int64_t bounds[2] = {};
                            if (!aProgram.debug_read(aMemory, aAddress + 8u + d * 16u, bounds, sizeof(bounds)))
                                return {};
                            result.dimensions.emplace_back(bounds[0], bounds[1] < bounds[0] ? 0u : static_cast<std::uint64_t>(bounds[1] - bounds[0]) + 1u);
                        }
                        return result;
                    }
                    std::string memory_type_name(machine const& aProgram, std::string_view aType) const
                    {
                        aType = aProgram.debug_named_type(aType);
                        memory_of(aType);
                        aType = aProgram.debug_named_type(aType);
                        if (aType.starts_with('@'))
                        {
                            auto const colon = aType.find(':');
                            return colon == std::string_view::npos ? std::string{ aType } : memory_type_name(aProgram, aType.substr(colon + 1u));
                        }
                        if (aType.starts_with('*'))
                            return memory_type_name(aProgram, aType.substr(1u)) + "*";
                        if (aType.starts_with('['))
                        {
                            auto rest = aType.substr(1u);
                            bool const described = rest.starts_with('@');
                            if (described)
                                rest.remove_prefix(1u);
                            auto const count = parse_number(rest);
                            auto const close = rest.find(']');
                            return close == std::string_view::npos ? std::string{ aType } : memory_type_name(aProgram, rest.substr(close + 1u)) + 
                                (described ? "[" + std::string(count > 1u ? static_cast<std::size_t>(count) - 1u : 0u, ',') + "]" : "[" + std::to_string(count) + "]");
                        }
                        if (auto const brace = aType.find('{'); brace != std::string_view::npos)
                            return brace == 0u ? std::string{ "struct" } : std::string{ aType.substr(0u, brace) };
                        if (aType.starts_with("enum"))
                            return "enum";
                        return aType == "cchar" ? std::string{ "char" } : std::string{ aType };
                    }
                    // (the text of a value of a memory type: aDepth levels of a structure's or array's parts are shown)
                    std::string memory_text(machine& aProgram, std::string_view aType, value aWord, std::size_t aDepth)
                    {
                        std::string objectType;
                        std::uint64_t address = 0u;
                        std::uint32_t memory = 0u;
                        if (located(aProgram, aType, aWord, objectType, address, memory))
                        {
                            if (scalar_size(objectType) != 0u)
                            {
                                auto const v = read_scalar(aProgram, memory, objectType, address);
                                if (!v)
                                    return "<invalid address>";
                                return objectType.starts_with('*') ? memory_text(aProgram, in_memory(memory, objectType), *v, aDepth) : make_variable({}, iThread, aProgram, {}, objectType, *v).value;
                            }
                            if (objectType.starts_with('['))
                            {
                                auto const array = array_at(aProgram, memory, objectType, address);
                                if (!array)
                                    return "<invalid address>";
                                auto const count = array->count();
                                if (array->type == "cchar" && array->dimensions.size() == 1u)
                                {
                                    // (a char array: its characters (a fixed one's: to its first NUL))
                                    std::string text;
                                    for (std::uint64_t i = 0u; i < count && i < 256u; ++i)
                                    {
                                        char c = 0;
                                        if (!aProgram.debug_read(memory, array->elements + i, &c, 1u) || (c == '\0' && !array->described))
                                            break;
                                        text += c;
                                    }
                                    return debug_quoted(text) + (array->described && count > 256u ? "..." : "");
                                }
                                if (aDepth == 0u)
                                    return "{...}";
                                std::string text = "{";
                                for (std::uint64_t i = 0u; i < count && i < 16u; ++i)
                                    text += (i != 0u ? ", " : "") + memory_text(aProgram, in_memory(memory, "@" + std::to_string(i * array->size) + ":" + array->type), array->elements, aDepth - 1u);
                                if (count > 16u)
                                    text += ", ...";
                                return text + "}";
                            }
                            if (auto const brace = objectType.find('{'); brace != std::string::npos)
                            {
                                if (aDepth == 0u)
                                    return "{...}";
                                std::string text = "{";
                                bool first = true;
                                std::string_view members{ objectType };
                                members = members.substr(brace + 1u, members.size() - brace - 2u);
                                for (auto const member : split_members(members))
                                {
                                    auto const at = member.find('@');
                                    if (at == std::string_view::npos)
                                        continue;
                                    text += (first ? "" : ", ") + std::string{ member.substr(0u, at) } + " = " + 
                                        memory_text(aProgram, in_memory(memory, member.substr(at)), address, aDepth - 1u);
                                    first = false;
                                }
                                return text + "}";
                            }
                            return "?";
                        }
                        auto type = aProgram.debug_named_type(aType);
                        memory = memory_of(type);
                        type = aProgram.debug_named_type(type);
                        if (type.starts_with('*'))
                        {
                            // (a pointer: its address (a char pointer's: and its string))
                            std::ostringstream oss;
                            oss << "0x" << std::hex << aWord;
                            if (aProgram.debug_named_type(type.substr(1u)) == "cchar" && aWord != 0u)
                            {
                                std::string text;
                                for (std::uint64_t i = 0u; i < 256u; ++i)
                                {
                                    char c = 0;
                                    if (!aProgram.debug_read(memory, aWord + i, &c, 1u) || c == '\0')
                                        break;
                                    text += c;
                                }
                                oss << " " << debug_quoted(text);
                            }
                            return oss.str();
                        }
                        return make_variable({}, iThread, aProgram, {}, std::string{ type }, aWord).value;
                    }
                    // (the parts of a value of a memory type: a pointer's object (*), an array's elements, a structure's members)
                    template <typename Visitor>
                    void memory_parts(machine& aProgram, std::string_view aType, value aWord, Visitor&& aVisitor)
                    {
                        std::string objectType;
                        std::uint64_t address = 0u;
                        std::uint32_t memory = 0u;
                        if (!located(aProgram, aType, aWord, objectType, address, memory))
                        {
                            auto type = aProgram.debug_named_type(aType);
                            memory = memory_of(type);
                            type = aProgram.debug_named_type(type);
                            if (type.starts_with('*') && aWord != 0u)
                            {
                                auto const target = std::string{ aProgram.debug_named_type(type.substr(1u)) };
                                if (target != "void" && !target.empty() && target != "function")
                                    aVisitor("*", in_memory(memory, "@0:" + std::string{ type.substr(1u) }), aWord);
                            }
                            return;
                        }
                        if (scalar_size(objectType) != 0u)
                        {
                            if (objectType.starts_with('*'))
                                if (auto const v = read_scalar(aProgram, memory, objectType, address); v && *v != 0u)
                                    aVisitor("*", in_memory(memory, "@0:" + objectType.substr(1u)), *v);
                            return;
                        }
                        if (objectType.starts_with('['))
                        {
                            auto const array = array_at(aProgram, memory, objectType, address);
                            if (!array)
                                return;
                            auto const count = array->count();
                            for (std::uint64_t i = 0u; i < count && i < 100000u; ++i)
                                if (!aVisitor(array->index(i), in_memory(memory, "@" + std::to_string(i * array->size) + ":" + array->type), array->elements))
                                    return;
                            return;
                        }
                        if (auto const brace = objectType.find('{'); brace != std::string::npos)
                        {
                            std::string_view members{ objectType };
                            members = members.substr(brace + 1u, members.size() - brace - 2u);
                            for (auto const member : split_members(members))
                            {
                                auto const at = member.find('@');
                                if (at != std::string_view::npos && !aVisitor(std::string{ member.substr(0u, at) }, in_memory(memory, member.substr(at)), address))
                                    return;
                            }
                        }
                    }
                    template <typename Request>
                    bool on(std::optional<std::uint32_t> aThread, Request&& aRequest)
                    {
                        if (!aThread || *aThread == iThread)
                        {
                            aRequest();
                            return true;
                        }
                        return iSession.run_on(*aThread, std::function<void()>{ std::forward<Request>(aRequest) });
                    }
                    static std::optional<std::vector<std::string>> parse_path(std::string const& aExpression)
                    {
                        std::vector<std::string> result;
                        std::size_t i = 0u;
                        auto const n = aExpression.size();
                        auto skip = [&]() { while (i < n && std::isspace(static_cast<unsigned char>(aExpression[i]))) ++i; };
                        auto identifier = [&]() -> std::optional<std::string>
                        {
                            skip();
                            auto const start = i;
                            while (i < n && (std::isalnum(static_cast<unsigned char>(aExpression[i])) || aExpression[i] == '_' || aExpression[i] == '$' || 
                                static_cast<unsigned char>(aExpression[i]) >= 0x80u))
                                ++i;
                            if (i == start || std::isdigit(static_cast<unsigned char>(aExpression[start])))
                                return {};
                            return aExpression.substr(start, i - start);
                        };
                        auto root = identifier();
                        if (!root)
                            return {};
                        result.push_back(*root);
                        for (;;)
                        {
                            skip();
                            if (i == n)
                                return result;
                            if (aExpression[i] == '.')
                            {
                                ++i;
                                auto member = identifier();
                                if (!member)
                                    return {};
                                result.push_back(*member);
                            }
                            else if (aExpression[i] == '[' || aExpression[i] == '(')
                            {
                                auto const close = aExpression[i] == '[' ? ']' : ')';
                                ++i;
                                skip();
                                if (i < n && (aExpression[i] == '"' || aExpression[i] == '\''))
                                {
                                    auto const quote = aExpression[i++];
                                    std::string key;
                                    while (i < n && aExpression[i] != quote)
                                    {
                                        if (aExpression[i] == '\\' && i + 1u < n)
                                            ++i;
                                        key += aExpression[i++];
                                    }
                                    if (i == n)
                                        return {};
                                    ++i;
                                    result.push_back(key);
                                }
                                else
                                {
                                    auto const start = i;
                                    if (i < n && aExpression[i] == '-')
                                        ++i;
                                    while (i < n && std::isdigit(static_cast<unsigned char>(aExpression[i])))
                                        ++i;
                                    if (i == start)
                                        return {};
                                    result.push_back(aExpression.substr(start, i - start));
                                }
                                skip();
                                if (i == n || aExpression[i] != close)
                                    return {};
                                ++i;
                            }
                            else
                                return {};
                        }
                    }
                    debug::frame describe(debug_frame const& aFrame) const
                    {
                        auto const& program = *aFrame.program;
                        auto const& info = program.frame_of(aFrame.function);
                        debug::frame result;
                        result.function = info.name;
                        result.source = program.source_name(info.source);
                        result.line = program.line_at(aFrame.function, aFrame.token);
                        if (auto const position = program.position_at(aFrame.token); position != 0u && bytecode::position_line(position) == result.line)
                        {
                            result.column = bytecode::position_column(position) + 1u;
                            result.endLine = bytecode::position_end_line(position);
                            result.endColumn = bytecode::position_end_column(position) + 1u;
                        }
                        if (auto const* variables = program.variables_of(aFrame.function))
                            for (auto const& v : *variables)
                                if (!v.language.empty())
                                {
                                    result.language = v.language;
                                    break;
                                }
                        return result;
                    }
                    debug::variable make_variable(std::string aName, std::uint32_t aThread, machine& aProgram, std::string const& aLanguage, std::string const& aType, value aWord)
                    {
                        debug::variable result;
                        result.name = std::move(aName);
                        // (a runtime plugin's object (see NEOS_RT_OBJECT): its handle; an integer of any size (NEOS_RT_INTEGER): a small one 
                        // shifted left one bit, else its object's handle so (with the low bit set): the plugin is given the object)
                        std::string type = aType;
                        if (!aLanguage.empty() && (aType == "object" || aType == "integer"))
                        {
                            if (aType == "integer" && (aWord & 1u) == 0u)
                            {
                                result.type = "int";
                                result.value = std::to_string(static_cast<std::int64_t>(aWord) >> 1);
                                return result;
                            }
                            void* object = nullptr;
                            try
                            {
                                object = aProgram.object_pointer(aType == "integer" ? static_cast<std::uint32_t>(aWord >> 1u) : as_u32(aWord));
                            }
                            catch (...)
                            {
                            }
                            if (object == nullptr)
                            {
                                result.value = "<no value>";
                                result.type = aType;
                                return result;
                            }
                            aWord = static_cast<value>(reinterpret_cast<std::uintptr_t>(object));
                            type = "pointer";
                        }
                        if (!aLanguage.empty())
                        {
                            auto const* hooks = static_cast<neos_rt_debug const*>(runtime::debug_hooks(aLanguage));
                            if (hooks != nullptr && hooks->describe != nullptr)
                            {
                                char const* typeName = nullptr;
                                auto const text = hooks->describe(type.c_str(), aWord, 256u, &typeName);
                                result.value = text != nullptr ? text : "";
                                result.type = typeName != nullptr ? typeName : type;
                                if (hooks->part_count != nullptr)
                                    if (auto const count = hooks->part_count(type.c_str(), aWord); count > 0u)
                                    {
                                        iValues.push_back(held_value{ aThread, &aProgram, aLanguage, type, aWord });
                                        result.reference = iValues.size();
                                        result.partCount = count;
                                    }
                                return result;
                            }
                            // (the language's runtime plugin cannot describe it)
                            std::ostringstream oss;
                            oss << "0x" << std::hex << aWord;
                            result.value = oss.str();
                            result.type = aLanguage + ":" + type;
                            return result;
                        }
                        // (the VM's types of objects in memory (see VariablesSection))
                        auto const named = std::string{ aProgram.debug_named_type(aType) };
                        if (!named.empty() && (named.front() == '#' || named.front() == '@' || named.front() == '*' || named.front() == '[' || (named.find('{') != std::string::npos && !named.starts_with("enum"))))
                        {
                            result.type = memory_type_name(aProgram, named);
                            result.value = memory_text(aProgram, named, aWord, 1u);
                            std::size_t count = 0u;
                            memory_parts(aProgram, named, aWord, [&](std::string const&, std::string const&, value) { ++count; return true; });
                            if (count > 0u)
                            {
                                iValues.push_back(held_value{ aThread, &aProgram, {}, named, aWord });
                                result.reference = iValues.size();
                                result.partCount = count;
                            }
                            return result;
                        }
                        result.type = aType;
                        if (named.starts_with("enum"))
                        {
                            // (an enumeration: its literal of the position)
                            auto const brace = named.find('{');
                            result.type = "enum";
                            if (brace == std::string::npos)
                            {
                                result.value = std::to_string(aWord);
                                return result;
                            }
                            std::string_view literals{ named };
                            literals = literals.substr(brace + 1u, literals.size() - brace - 2u);
                            std::size_t position = 0u;
                            for (std::size_t start = 0u; start <= literals.size(); ++position)
                            {
                                auto const comma = literals.find(',', start);
                                auto const literal = literals.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
                                if (position == static_cast<std::size_t>(aWord & 0xFFFFFFFFu))
                                {
                                    result.value = std::string{ literal };
                                    return result;
                                }
                                if (comma == std::string_view::npos)
                                    break;
                                start = comma + 1u;
                            }
                            result.value = std::to_string(aWord);
                            return result;
                        }
                        if (aType == "i8")
                            result.value = std::to_string(static_cast<std::int8_t>(aWord));
                        else if (aType == "u8")
                            result.value = std::to_string(static_cast<std::uint8_t>(aWord));
                        else if (aType == "i16")
                            result.value = std::to_string(static_cast<std::int16_t>(aWord));
                        else if (aType == "u16")
                            result.value = std::to_string(static_cast<std::uint16_t>(aWord));
                        else if (aType == "cchar")
                        {
                            auto const c = static_cast<std::int8_t>(aWord);
                            static char const* const sEscapes[] = { "\\0", "\\x01", "\\x02", "\\x03", "\\x04", "\\x05", "\\x06", "\\a", "\\b", "\\t", "\\n", "\\v", "\\f", "\\r" };
                            std::string text;
                            if (c >= 0x20 && c < 0x7F)
                                text = c == '\'' ? "\\'" : c == '\\' ? "\\\\" : std::string(1u, static_cast<char>(c));
                            else if (c >= 0 && c < 14)
                                text = sEscapes[c];
                            else
                            {
                                char escape[8];
                                std::snprintf(escape, sizeof(escape), "\\x%02x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                                text = escape;
                            }
                            result.value = std::to_string(c) + " '" + text + "'";
                        }
                        else if (aType == "i32")
                            result.value = std::to_string(as_i32(aWord));
                        else if (aType == "u32")
                            result.value = std::to_string(as_u32(aWord));
                        else if (aType == "i64")
                            result.value = std::to_string(as_i64(aWord));
                        else if (aType == "u64")
                            result.value = std::to_string(aWord);
                        else if (aType == "f32")
                            result.value = debug_number(as_f32(aWord));
                        else if (aType == "f64")
                            result.value = debug_number(as_f64(aWord));
                        else if (aType == "bool")
                            result.value = as_u32(aWord) != 0u ? "true" : "false";
                        else if (aType == "char")
                            result.value = debug_code_point(as_u32(aWord));
                        else if (aType == "string")
                            result.value = debug_quoted(aProgram.debug_string(as_u32(aWord)));
                        else
                        {
                            std::ostringstream oss;
                            oss << "0x" << std::hex << aWord;
                            result.value = oss.str();
                        }
                        return result;
                    }
                private:
                    debug::session& iSession;
                    debug::stop_reason const iReason;
                    std::optional<std::uint32_t> const iBreakpoint;
                    std::string const iMessage;
                    std::uint32_t const iThread;
                    std::vector<held_value> iValues;
                };

                // (the debugger ends the program: its own thread exits; another thread stops with an error (as a trap); not while an exception 
                // is being thrown (e.g. in a destructor))
                void debug_terminate()
                {
                    if (std::uncaught_exceptions() > 0)
                        return;
                    if (tDebugProgramThread)
                        throw exit_request{ EXIT_FAILURE };
                    tDebugErrorStopped = true;
                    throw trap{ "terminated (by the debugger)" };
                }

                // (the calling thread stops: its session's front end decides how it resumes)
                void debug_stop_thread(debug::session& aSession, debug::stop_reason aReason, std::optional<std::uint32_t> aBreakpoint, std::string const& aMessage)
                {
                    debug::action action = debug::action::Continue;
                    {
                        debug_stop stop{ aSession, aReason, aBreakpoint, aMessage };
                        tDebugBusy = true;
                        struct not_busy { ~not_busy() { tDebugBusy = false; } } const notBusy;
                        action = aSession.stop(stop);
                    }
                    tDebugStep = debug_step{ action, debug_depth() };
                    if (action == debug::action::Terminate)
                        debug_terminate();
                }

                // (a breakpoint's condition, as the frame that reached it evaluates it: an error stops the program (as gdb's))
                bool debug_condition(debug::session& aSession, std::string const& aCondition)
                {
                    debug_stop probe{ aSession, debug::stop_reason::Breakpoint, {}, {} };
                    tDebugBusy = true;
                    struct not_busy { ~not_busy() { tDebugBusy = false; } } const notBusy;
                    std::string error;
                    auto const result = probe.evaluate(0u, aCondition, error, {});
                    return !result || debug_true(result->value);
                }
            }

            namespace
            {
                // (a runtime plugin: an error that the program does not handle (see neos_rt_debugger::error))
                int host_debug_error(char const* aMessage)
                {
                    auto* const session = tDebugSession;
                    if (session == nullptr)
                        return 0;
                    // (the debugger is ending the program (e.g. "kill" where the error was thrown): the plugin need not report the error)
                    if (session->terminate_requested())
                        return 1;
                    if (tDebugBusy || tDebugErrorStopped || !session->break_on_error())
                        return 0;
                    tDebugErrorStopped = true;
                    debug::action action = debug::action::Continue;
                    {
                        debug_stop stop{ *session, debug::stop_reason::Error, {}, aMessage != nullptr ? aMessage : "error" };
                        tDebugBusy = true;
                        struct not_busy { ~not_busy() { tDebugBusy = false; } } const notBusy;
                        action = session->stop(stop);
                    }
                    tDebugStep = debug_step{ action, debug_depth() };
                    return action == debug::action::Terminate ? 1 : 0;
                }

                // (a runtime plugin: whether an exception thrown could stop the program (a catchpoint))
                int host_debug_catching()
                {
                    auto* const session = tDebugSession;
                    return session != nullptr && !tDebugBusy && session->catching() && !session->terminate_requested() ? 1 : 0;
                }

                // (a runtime plugin: an exception that the program's code throws, where it is thrown (see neos_rt_debugger::thrown))
                int host_debug_thrown(char const* aType, char const* aMessage, int aControl)
                {
                    if (host_debug_catching() == 0)
                        return 0;
                    auto* const session = tDebugSession;
                    std::string const type = aType != nullptr ? aType : "";
                    std::string const message = aMessage != nullptr ? aMessage : "";
                    for (auto const id : session->catchpoints_for(type, aControl != 0))
                    {
                        if (!session->hit(id, [&](std::string const& aCondition) { return debug_condition(*session, aCondition); }))
                            continue;
                        debug::action action = debug::action::Continue;
                        {
                            debug_stop stop{ *session, debug::stop_reason::Throw, id, message.empty() ? type : type + ": " + message };
                            tDebugBusy = true;
                            struct not_busy { ~not_busy() { tDebugBusy = false; } } const notBusy;
                            action = session->stop(stop);
                        }
                        tDebugStep = debug_step{ action, debug_depth() };
                        if (action != debug::action::Terminate)
                            return 0;
                        tDebugTerminating = true;
                        return 1;
                    }
                    return 0;
                }

                neos_rt_debugger const sDebugger{ NEOS_RUNTIME_DEBUG_VERSION, host_debug_error, host_debug_catching, host_debug_thrown };

                void machine::debug_prepare()
                {
                    auto const version = iDebug->version();
                    if (version == iDebugVersion.load(std::memory_order_acquire) && !iDebugMarks.empty())
                        return;
                    std::vector<std::pair<std::uint32_t, std::uint32_t>> bound;
                    {
                        std::lock_guard lock{ iDebugMutex };
                        if (version == iDebugVersion.load(std::memory_order_acquire) && !iDebugMarks.empty())
                            return;
                        if (iDebugMarks.empty())
                        {
                            // (the statements: those of functions that have frames (the compiler's helpers have none))
                            iDebugMarks.assign(iTokens.size() + 1u, 0u);
                            iDebugTokenLines.assign(iTokens.size() + 1u, 0u);
                            for (auto const& [token, line] : iLines)
                            {
                                auto const f = function_at(token);
                                // (nor those of hidden functions (as debug_frames hides their frames: e.g. a C thread's start, "__neos_start_thrd"))
                                if (!f || *f >= iFrames.size() || !iFrames[*f].frame || iFrames[*f].name.starts_with("__neos") || token >= iDebugMarks.size())
                                    continue;
                                iDebugMarks[token] |= DebugStatement;
                                iDebugTokenLines[token] = line;
                                iDebugLines[iFrames[*f].source][line].push_back(token);
                            }
                        }
                        for (auto& mark : iDebugMarks)
                            mark &= ~DebugBreakpoint;
                        iDebugBreakpoints.clear();
                        // (a breakpoint is on the first line at or after its line that has statements: on each of them)
                        for (auto const& b : iDebug->breakpoints())
                        {
                            if (!b.enabled || b.exception)
                                continue;
                            for (auto const& [source, lines] : iDebugLines)
                            {
                                if (source >= iSources.size() || !debug::same_source(b.requested.source, iSources[source]))
                                    continue;
                                auto const l = lines.lower_bound(b.requested.line);
                                if (l == lines.end())
                                    continue;
                                // (a column: the line's statements that begin there (else at the first column after it), if their positions are known)
                                auto tokens = l->second;
                                if (b.requested.column != 0u && l->first == b.requested.line)
                                {
                                    std::optional<std::uint32_t> best;
                                    for (auto const token : tokens)
                                        if (auto const position = position_at(token); position != 0u && bytecode::position_line(position) == l->first)
                                            if (auto const column = bytecode::position_column(position) + 1u; column >= b.requested.column && (!best || column < *best))
                                                best = column;
                                    if (best)
                                        std::erase_if(tokens, [&](std::size_t aToken)
                                        {
                                            auto const position = position_at(aToken);
                                            return position == 0u || bytecode::position_column(position) + 1u != *best;
                                        });
                                }
                                for (auto const token : tokens)
                                {
                                    iDebugMarks[token] |= DebugBreakpoint;
                                    iDebugBreakpoints[token].emplace_back(b.id, b.requested.column != 0u);
                                }
                                bound.emplace_back(b.id, l->first);
                            }
                        }
                        iDebugVersion.store(version, std::memory_order_release);
                    }
                    for (auto const& [id, line] : bound)
                        iDebug->bound(id, line);
                }

                // (debugging: a statement begins (see DebugStatement): the program stops if a breakpoint is there, or as it steps, 
                // or as its session asks (pause, terminate, another thread stopped); a breakpoint (and a step) stops only on a new 
                // line: one that is not the line of the thread's previous statement in the same frame)
                void machine::debug_statement(activation& aActivation, std::size_t aToken)
                {
                    if (tDebugBusy)
                        return;
                    auto& session = *iDebug;
                    tDebugErrorStopped = false;
                    if (session.version() != iDebugVersion.load(std::memory_order_acquire))
                        debug_prepare();
                    struct last_statement
                    {
                        std::uint64_t serial = 0u;
                        std::uint32_t line = 0u;
                    };
                    thread_local last_statement tLast;
                    auto const serial = aActivation.frames != nullptr && !aActivation.frames->empty() ? aActivation.frames->back().serial : 0u;
                    auto const line = debug_line(aToken);
                    bool const newLine = serial != tLast.serial || line != tLast.line;
                    tLast = last_statement{ serial, line };
                    if (session.terminate_requested())
                        debug_terminate();
                    if (session.any_stopped())
                    {
                        tDebugBusy = true;
                        session.park();
                        tDebugBusy = false;
                        if (session.terminate_requested())
                            debug_terminate();
                    }
                    std::optional<debug::stop_reason> reason;
                    std::optional<std::uint32_t> hit;
                    if (tDebugEntry)
                    {
                        tDebugEntry = false;
                        reason = debug::stop_reason::Entry;
                    }
                    else if (session.pause_requested())
                        reason = debug::stop_reason::Pause;
                    else
                    {
                        if ((iDebugMarks[aToken] & DebugBreakpoint) != 0u)
                        {
                            std::vector<std::pair<std::uint32_t, bool>> breakpoints;
                            {
                                std::lock_guard lock{ iDebugMutex };
                                if (auto const existing = iDebugBreakpoints.find(aToken); existing != iDebugBreakpoints.end())
                                    breakpoints = existing->second;
                            }
                            for (auto const& [id, exact] : breakpoints)
                                if ((newLine || exact) && session.hit(id, [&](std::string const& aCondition) { return debug_condition(session, aCondition); }))
                                {
                                    reason = debug::stop_reason::Breakpoint;
                                    hit = id;
                                    break;
                                }
                        }
                        if (!reason && tDebugStep.mode != debug::action::Continue && newLine)
                        {
                            auto const depth = debug_depth();
                            switch (tDebugStep.mode)
                            {
                            case debug::action::StepInto:
                                reason = debug::stop_reason::Step;
                                break;
                            case debug::action::StepOver:
                                if (depth <= tDebugStep.depth)
                                    reason = debug::stop_reason::Step;
                                break;
                            case debug::action::StepOut:
                                if (depth < tDebugStep.depth)
                                    reason = debug::stop_reason::Step;
                                break;
                            default:
                                break;
                            }
                        }
                    }
                    if (reason)
                        debug_stop_thread(session, *reason, hit, {});
                }

                // (debugging: an error is thrown: the program stops (once, before its frames are unwound) if its session breaks on errors)
                void machine::debug_error(activation& aActivation, std::string const& aMessage)
                {
                    (void)aActivation;
                    if (tDebugBusy || tDebugErrorStopped || !iDebug->break_on_error() || iDebug->terminate_requested())
                        return;
                    tDebugErrorStopped = true;
                    debug_stop_thread(*iDebug, debug::stop_reason::Error, {}, aMessage);
                }

                void host_values(neos_rt_coroutine* aCoroutine, void (*aVisit)(void* aData, unsigned long long const* aValues, size_t aCount), void* aData)
                {
                    if (aVisit == nullptr)
                        return;
                    if (aCoroutine != nullptr)
                    {
                        auto const& c = *reinterpret_cast<coroutine const*>(aCoroutine);
                        if (!c.finished && c.activations != nullptr)
                            visit_values(c.activations, aVisit, aData);
                        return;
                    }
                    // (the running code's: then that of the code that resumed the coroutine running, and so on)
                    visit_values(tActivation, aVisit, aData);
                    for (auto* c = tCoroutine; c != nullptr; c = c->previous)
                        visit_values(c->resumerActivations, aVisit, aData);
                }

                unsigned host_function_frame(void* aContext, unsigned aFunction, neos_rt_frame* aFrame)
                {
                    auto const& context = aContext != nullptr ? *static_cast<running const*>(aContext) : tRunning;
                    if (context.program == nullptr || aFrame == nullptr)
                        return 0u;
                    try
                    {
                        auto const& info = context.program->frame_of(aFunction);
                        if (!info.frame)
                            return 0u;
                        *aFrame = neos_rt_frame{ info.name.c_str(), context.program->source_name(info.source).c_str(), 0u, info.firstLine, 
                            aContext != nullptr ? aContext : context_of(context.program, context.native), 0u };
                        return 1u;
                    }
                    catch (std::out_of_range const&)
                    {
                        return 0u;
                    }
                }

                // (the calling thread's (see set_program_arguments): a thread takes them when it is constructed)
                std::vector<std::string>& program_arguments()
                {
                    thread_local std::vector<std::string> tArguments;
                    return tArguments;
                }

                unsigned host_arguments(char const** aArguments, unsigned aCount)
                {
                    // (the running program's (with none running, the calling thread's), as strings that are valid until the process ends)
                    auto const& arguments = tRunning.program != nullptr ? tRunning.program->arguments() : program_arguments();
                    static std::mutex sMutex;
                    static std::set<std::string> sStrings;
                    std::lock_guard<std::mutex> lock{ sMutex };
                    for (unsigned i = 0u; i < aCount && i < arguments.size(); ++i)
                        aArguments[i] = sStrings.insert(arguments[i]).first->c_str();
                    return static_cast<unsigned>(arguments.size());
                }

                // --- standard streams (the running program's: see neos_rt_host::std_write)

                std::ostream* output_stream(int aStream)
                {
                    if (aStream == 1)
                        return tRunning.program != nullptr ? &tRunning.program->output() : &std::cout;
                    if (aStream == 2)
                        return tRunning.program != nullptr ? &tRunning.program->error_output() : &std::cerr;
                    return nullptr;
                }

                std::istream* input_stream(int aStream)
                {
                    if (aStream == 0)
                        return tRunning.program != nullptr ? &tRunning.program->input() : &std::cin;
                    return nullptr;
                }

                size_t host_std_write(int aStream, char const* aData, size_t aCount)
                {
                    auto* const stream = output_stream(aStream);
                    if (stream == nullptr || (aData == nullptr && aCount != 0u) || !stream->write(aData, static_cast<std::streamsize>(aCount)))
                        return 0u;
                    return aCount;
                }

                // (debugging: a read of the program's input that failed: if Ctrl+C or a stop of the program interrupted it (see 
                // debug::blocking_read), the thread stops there, as Ctrl+C pauses the program (or parks, if another thread has stopped 
                // it), then reads again (true), the same on every platform; else it is the end of input (false))
                bool debug_read_again(std::istream& aStream, std::uint32_t aInterrupts, bool aInterrupted)
                {
                    auto* const session = tDebugSession;
                    if (session == nullptr || tDebugBusy)
                        return false;
#if defined(_WIN32)
                    // (Ctrl+C also aborts a console read itself; its handler runs on a thread of its own, which can be just after)
                    for (int wait = 0; wait < 25 && !aInterrupted && session->interrupts() == aInterrupts && !session->any_stopped(); ++wait)
                        std::this_thread::sleep_for(std::chrono::milliseconds{ 10 });
#endif
                    if (!aInterrupted && session->interrupts() == aInterrupts && !session->any_stopped() && !session->pause_requested())
                        return false;
                    aStream.clear();
                    if (&aStream == &std::cin)
                        std::clearerr(stdin);
                    // (not thrown from here (a plugin's call): an end of the program is the end of input, and it ends at its next statement)
                    if (session->terminate_requested())
                    {
                        tDebugTerminating = true;
                        return false;
                    }
                    if (session->any_stopped())
                        session->park();
                    else if (session->pause_requested())
                    {
                        debug::action action = debug::action::Continue;
                        {
                            debug_stop stop{ *session, debug::stop_reason::Pause, {}, {} };
                            tDebugBusy = true;
                            struct not_busy { ~not_busy() { tDebugBusy = false; } } const notBusy;
                            action = session->stop(stop);
                        }
                        tDebugStep = debug_step{ action, debug_depth() };
                    }
                    if (session->terminate_requested())
                    {
                        tDebugTerminating = true;
                        return false;
                    }
                    return true;
                }

                // (the end of input is not kept: a stream that has more input later (e.g. a terminal's) can be read again)
                size_t host_std_read(int aStream, char* aBuffer, size_t aCount)
                {
                    auto* const stream = input_stream(aStream);
                    if (stream == nullptr || aBuffer == nullptr)
                        return 0u;
                    size_t result = 0u;
                    for (;;)
                    {
                        auto const interrupts = tDebugSession != nullptr ? tDebugSession->interrupts() : 0u;
                        debug::blocking_read reading{ tDebugSession != nullptr };
                        stream->read(aBuffer + result, static_cast<std::streamsize>(aCount - result));
                        bool const interrupted = reading.finish();
                        result += static_cast<size_t>(stream->gcount());
                        stream->clear();
                        if (result == aCount || !debug_read_again(*stream, interrupts, interrupted))
                            return result;
                    }
                }

                int host_std_getc(int aStream)
                {
                    auto* const stream = input_stream(aStream);
                    if (stream == nullptr)
                        return -1;
                    for (;;)
                    {
                        auto const interrupts = tDebugSession != nullptr ? tDebugSession->interrupts() : 0u;
                        debug::blocking_read reading{ tDebugSession != nullptr };
                        auto const c = stream->get();
                        bool const interrupted = reading.finish();
                        if (c != std::istream::traits_type::eof())
                            return static_cast<unsigned char>(std::istream::traits_type::to_char_type(c));
                        stream->clear();
                        if (!debug_read_again(*stream, interrupts, interrupted))
                            return -1;
                    }
                }

                int host_std_ungetc(int aCharacter, int aStream)
                {
                    auto* const stream = input_stream(aStream);
                    if (stream == nullptr || aCharacter < 0 || !stream->putback(static_cast<char>(aCharacter)))
                    {
                        if (stream != nullptr)
                            stream->clear();
                        return -1;
                    }
                    return aCharacter;
                }

                int host_std_flush(int aStream)
                {
                    if (aStream == 0)
                        return 0;
                    auto* const stream = output_stream(aStream);
                    return stream != nullptr && stream->flush() ? 0 : -1;
                }

                // (a linked object (see neos_rt_link_type) of a function that the code of a language that has no runtime plugin defines (see 
                // bytecode::LinksSection))
                struct linked_function
                {
                    neos_rt_object base;
                    std::atomic<std::uint32_t> references{ 1u };
                    machine::link_export link;
                };

                char const* call_linked_function(neos_rt_object* aObject, unsigned aCount, neos_rt_type const* aTypes, neos_rt_value const* aArguments,
                    neos_rt_type* aResultType, neos_rt_value* aResult)
                {
                    auto const& l = reinterpret_cast<linked_function*>(aObject)->link;
                    thread_local std::string tError;
                    auto const fail = [&](std::string aMessage) { tError = "TypeError: " + std::move(aMessage); return tError.c_str(); };
                    auto const arrow = l.signature.find('>');
                    std::string_view const parameters{ l.signature.data(), arrow };
                    if (aCount != parameters.size())
                        return fail(l.name + "() takes " + std::to_string(parameters.size()) + " argument" + (parameters.size() == 1u ? "" : "s") + 
                            " (" + std::to_string(aCount) + " given)");
                    std::vector<neos_rt_type> types(aCount);
                    std::vector<neos_rt_value> arguments(aCount);
                    for (unsigned i = 0u; i < aCount; ++i)
                    {
                        auto const& a = aArguments[i];
                        auto const t = aTypes[i];
                        auto& v = arguments[i];
                        auto const argument = [&](char const* aWhat) { return fail(l.name + "(): argument " + std::to_string(i + 1u) + " must be " + aWhat); };
                        bool const integral = t == NEOS_RT_I64 || t == NEOS_RT_BOOL || (t == NEOS_RT_F64 && std::trunc(a.f64) == a.f64 && std::abs(a.f64) < 9.2e18);
                        std::int64_t const integer = t == NEOS_RT_I64 ? a.i64 : t == NEOS_RT_BOOL ? (a.b != 0 ? 1 : 0) : t == NEOS_RT_F64 && integral ? static_cast<std::int64_t>(a.f64) : 0;
                        bool const numeric = t == NEOS_RT_I64 || t == NEOS_RT_F64 || t == NEOS_RT_BIG_INTEGER;
                        double const real = t == NEOS_RT_F64 ? a.f64 : t == NEOS_RT_BIG_INTEGER ? big_integer_value(a.string) : static_cast<double>(a.i64);
                        switch (parameters[i])
                        {
                        case 'b':
                            if (t != NEOS_RT_BOOL && t != NEOS_RT_I64)
                                return argument("a boolean");
                            types[i] = NEOS_RT_BOOL;
                            v.b = (t == NEOS_RT_BOOL ? a.b != 0 : a.i64 != 0) ? 1 : 0;
                            break;
                        case 'i': if (!integral) return argument("an integer"); types[i] = NEOS_RT_I32; v.i32 = static_cast<int>(integer); break;
                        case 'u': if (!integral) return argument("an integer"); types[i] = NEOS_RT_U32; v.u32 = static_cast<unsigned>(integer); break;
                        case 'I': if (!integral) return argument("an integer"); types[i] = NEOS_RT_I64; v.i64 = integer; break;
                        case 'U': if (!integral) return argument("an integer"); types[i] = NEOS_RT_U64; v.u64 = static_cast<unsigned long long>(integer); break;
                        case 'f': if (!numeric) return argument("a number"); types[i] = NEOS_RT_F32; v.f32 = static_cast<float>(real); break;
                        case 'F': if (!numeric) return argument("a number"); types[i] = NEOS_RT_F64; v.f64 = real; break;
                        case 's': if (t != NEOS_RT_STRING) return argument("a string"); types[i] = NEOS_RT_STRING; v.string = a.string; break;
                        default: return fail("invalid link signature");
                        }
                    }
                    neos_rt_type resultType = NEOS_RT_VOID;
                    switch (l.signature[arrow + 1u])
                    {
                    case 'b': resultType = NEOS_RT_BOOL; break;
                    case 'i': resultType = NEOS_RT_I32; break;
                    case 'u': resultType = NEOS_RT_U32; break;
                    case 'I': resultType = NEOS_RT_I64; break;
                    case 'U': resultType = NEOS_RT_U64; break;
                    case 'f': resultType = NEOS_RT_F32; break;
                    case 'F': resultType = NEOS_RT_F64; break;
                    case 's': resultType = NEOS_RT_STRING; break;
                    default: break;
                    }
                    neos_rt_value result = {};
                    // (called on its own program)
                    auto const previous = tRunning;
                    tRunning = running{ l.program, l.native };
                    struct restore { running previous; ~restore() { tRunning = previous; } } const restoreRunning{ previous };
                    if (auto const error = l.program->call_from_plugin(l.native, l.function, aCount, types.data(), arguments.data(), resultType, &result))
                        return error;
                    switch (resultType)
                    {
                    case NEOS_RT_BOOL: *aResultType = NEOS_RT_BOOL; aResult->b = result.b; break;
                    case NEOS_RT_I32: *aResultType = NEOS_RT_I64; aResult->i64 = result.i32; break;
                    case NEOS_RT_U32: *aResultType = NEOS_RT_I64; aResult->i64 = result.u32; break;
                    case NEOS_RT_I64: *aResultType = NEOS_RT_I64; aResult->i64 = result.i64; break;
                    case NEOS_RT_U64: *aResultType = NEOS_RT_I64; aResult->i64 = static_cast<long long>(result.u64); break;
                    case NEOS_RT_F32: *aResultType = NEOS_RT_F64; aResult->f64 = result.f32; break;
                    case NEOS_RT_F64: *aResultType = NEOS_RT_F64; aResult->f64 = result.f64; break;
                    case NEOS_RT_STRING: *aResultType = NEOS_RT_STRING; aResult->string = result.string; break;
                    default: *aResultType = NEOS_RT_VOID; break;
                    }
                    return nullptr;
                }

                // (the linked objects' types: by their languages)
                neos_rt_link_type const* linked_function_type(std::string const& aLanguage)
                {
                    struct link_type
                    {
                        std::string language;
                        neos_rt_link_type type;
                    };
                    static std::mutex sMutex;
                    static std::map<std::string, std::unique_ptr<link_type>> sTypes;
                    std::lock_guard<std::mutex> lock{ sMutex };
                    auto& t = sTypes[aLanguage];
                    if (t == nullptr)
                    {
                        t = std::make_unique<link_type>();
                        t->language = aLanguage;
                        t->type.base.name = "neos.linked_function";
                        t->type.base.retain = [](neos_rt_object* aObject) { reinterpret_cast<linked_function*>(aObject)->references.fetch_add(1u, std::memory_order_relaxed); };
                        t->type.base.release = [](neos_rt_object* aObject)
                        {
                            auto* const f = reinterpret_cast<linked_function*>(aObject);
                            if (f->references.fetch_sub(1u, std::memory_order_acq_rel) == 1u)
                                delete f;
                        };
                        t->type.language = t->language.c_str();
                        t->type.callable = [](neos_rt_object*) { return 1; };
                        t->type.call = call_linked_function;
                    }
                    return &t->type;
                }

                int host_link(char const* aName, char const* aLanguage, neos_rt_type* aType, neos_rt_value* aValue)
                {
                    // the translation units other than the caller's, in turn, each's program running: the first's (the program's: the code of
                    // its languages), then the others' (a program of its own each: see link_run)
                    auto* const linker = tRunning.program != nullptr ? tRunning.program->linker() : nullptr;
                    auto* const caller = tRunning.program != nullptr ? tRunning.program->unit() : nullptr;
                    if (linker == nullptr)
                        return runtime::link(aName, aLanguage, aType, aValue) ? 1 : 0;
                    if (aName == nullptr)
                        return 0;
                    auto const previous = tRunning;
                    struct restore { running previous; ~restore() { tRunning = previous; } } const restoreRunning{ previous };
                    tRunning = running{ linker, linker->linker_native() };
                    if (runtime::link(aName, caller == linker ? aLanguage : nullptr, aType, aValue))
                        return 1;
                    for (auto const& u : linker->units())
                    {
                        if (u.program == caller)
                            continue;
                        tRunning = running{ u.program, u.native };
                        if (runtime::link_of(aName, u.language, aType, aValue))
                            return 1;
                    }
                    tRunning = previous;
                    // (then a function of the program's modules (see bytecode::LinksSection))
                    for (auto const& l : linker->links())
                        if (l.name == aName && (aLanguage == nullptr || l.language != aLanguage || l.program->unit() != caller))
                        {
                            auto* const f = new linked_function{ { &linked_function_type(l.language)->base }, 1u, l };
                            *aType = NEOS_RT_OBJECT;
                            aValue->object = &f->base;
                            return 1;
                        }
                    return 0;
                }

                unsigned long long host_stack_limit()
                {
                    return jit::thread_stack_limit();
                }

                neos_rt_host const sHost{ NEOS_RUNTIME_PLUGIN_VERSION, host_call, host_coroutine_new, host_coroutine_resume, host_coroutine_suspend,
                    host_coroutine_delete, host_thread_context, host_thread_attach, host_load, host_run, host_restricted, host_trap, host_frames,
                    host_function_frame, host_arguments, host_values, host_load_n, host_std_write, host_std_read, host_std_getc, host_std_ungetc,
                    host_std_flush, host_global, host_unload, host_external_memory, host_external_memory_at, host_fix_global, host_link, host_watched_changed, 
                    host_shared_memory, host_stack_limit };
            }

            void const* runtime::host()
            {
                return &sHost;
            }

            void const* runtime::debugger()
            {
                return &sDebugger;
            }

            void set_program_arguments(std::vector<std::string> aArguments)
            {
                program_arguments() = std::move(aArguments);
            }

            void set_compile_service(compile_service aService)
            {
                the_compile_service() = std::move(aService);
            }

            thread::thread(text const& aText, std::ostream& aOutput, std::istream& aInput, std::ostream& aError, bool aRestricted) :
                iText{ aText },
                iOutput{ aOutput },
                iInput{ aInput },
                iErrorOutput{ aError },
                iCompileService{ the_compile_service() },
                iArguments{ program_arguments() },
                iSession{ debug::current_session() },
                iRestricted{ aRestricted }
            {
                // start executing only once all members have been constructed
                static_cast<std::thread&>(*this) = std::thread{ [this]() { execute(); } };
            }

            void thread::execute()
            {
                auto const start = std::chrono::steady_clock::now();
                try
                {
                    if (iText.empty())
                        throw exceptions::no_text();
                    // (debugging: the program's machines (and those it loads) are its session's; it stops at its first statement if its
                    // session asks)
                    tDebugSession = iSession.get();
                    tDebugStep = debug_step{};
                    tDebugEntry = iSession != nullptr && iSession->stop_on_entry();
                    tDebugProgramThread = true;
                    if (iSession != nullptr)
                        iSession->thread_id();  // (the program's own thread is its session's thread 1)
                    struct debug_end { ~debug_end() { tDebugSession = nullptr; tDebugEntry = false; tDebugProgramThread = false; } } const debugEnd;
                    machine m{ iText, iOutput, iInput, iErrorOutput, iInstructions, iRestricted, iCompileService, iArguments };
                    auto const entry = m.export_function("main");
                    if (!entry)
                        throw std::runtime_error("no entry point (main)");
                    // native code (JIT) except when debugging: debug builds interpret (NEOS_JIT=0/1 overrides)
#if defined(NDEBUG)
                    bool useJit = true;
#else
                    bool useJit = false;
#endif
                    if (auto const setting = environment_variable("NEOS_JIT"))
                        useJit = *setting != "0";
                    // (debugging: the program is interpreted)
                    if (iSession != nullptr)
                        useJit = false;
                    jit::compiled_module_ptr native;
                    if (useJit)
                        native = m.compile();
                    iJit = static_cast<bool>(native);
                    m.set_linker(&m, native.get());
                    m.set_unit(&m);
                    auto const previous = tRunning;
                    tRunning = running{ &m, native.get() };
                    struct restore { running previous; ~restore() { tRunning = previous; } } const restoreRunning{ previous };
                    // (then the objects it did not release (it is still running))
                    struct release { machine& m; ~release() { m.release_objects(); } } const releasing{ m };
                    // (when it returns, the plugins finish their work (e.g. threads) while the program's state exists)
                    // (debugging: the code that runs as the plugins finish (e.g. atexit's) does not stop)
                    struct end { ~end() { tDebugBusy = true; runtime::program_end(); tDebugBusy = false; } } const ending;
                    auto const results = native ? m.invoke(*native, *entry, {}) : m.invoke(*entry, {});
                    auto const& type = m.type_of(*entry);
                    if (!results.empty())
                    {
                        switch (type.results[0])
                        {
                        case 0x7F: iResult = language::data<language::i32>{ as_i32(results[0]) }; break;
                        case 0x7E: iResult = language::data<language::i64>{ as_i64(results[0]) }; break;
                        case 0x7D: iResult = language::data<language::f32>{ as_f32(results[0]) }; break;
                        case 0x7C: iResult = language::data<language::f64>{ as_f64(results[0]) }; break;
                        }
                    }
                }
                catch (exit_request const& e)
                {
                    iExitStatus = e.status;
                }
                catch (std::exception const& e)
                {
                    // (an error: reported on the standard error stream (the program's output is flushed first))
                    iError = e.what();
                    iOutput.flush();
                    iErrorOutput << "Runtime error: " << e.what() << std::endl;
                }
                iExecutionTime = std::chrono::steady_clock::now() - start;
            }

            std::string thread::metrics() const
            {
                std::ostringstream oss;
                oss << "Instructions executed: ";
                if (iJit)
                    oss << "(native code)";
                else
                    oss << iInstructions;
                oss << ", execution time: " <<
                    std::chrono::duration_cast<std::chrono::microseconds>(iExecutionTime).count() / 1000.0 << "ms" << std::endl;
                return oss.str();
            }
        }
    }
}
