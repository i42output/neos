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
#include <neos/bytecode/vm/vm.hpp>
#include <neos/bytecode/vm/jit.hpp>
#include <neos/bytecode/vm/runtime_plugins.hpp>
#define NEOS_RUNTIME_PLUGIN_HOST
#include <neos/runtime/plugin.h>
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
#endif
#include <list>
#include <mutex>

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
                    reader(std::vector<std::uint8_t> const& aBytes, std::size_t aPosition = 0u) : iBytes{ aBytes }, iPosition{ aPosition } {}
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
                    std::vector<std::uint8_t> const& iBytes;
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
                };

                thread_local activation* tActivation = nullptr;

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
                public:
                    machine(text const& aText, std::ostream& aOutput, std::istream& aInput, std::ostream& aErrorOutput, std::uint64_t& aInstructions, bool aRestricted,
                        compile_service const& aCompileService, std::vector<std::string> const& aArguments) :
                        iSerial{ ++sSerial }, iOutput{ aOutput }, iInput{ aInput }, iErrorOutput{ aErrorOutput }, iInstructions{ aInstructions }, iRestricted{ aRestricted },
                        iCompileService{ aCompileService }, iArguments{ aArguments }
                    {
                        for (auto b : aText)
                            iModule.push_back(static_cast<std::uint8_t>(b));
                        parse();
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
                        module.globalTypes = &iGlobalTypes;
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
                            } };
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
                        std::vector<value> stack;
                        std::vector<frame> frames;
                        activation_scope const activating{ this, nullptr, &frames, &stack };
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
                                        // (the external memory: a 64 bit address)
                                        auto const& external = jit::external_memory();
                                        auto const address = pop() + offset;
                                        if (memoryIndex != 1u || address + width > external.size || address + width < address)
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
                                        auto& external = jit::external_memory();
                                        auto const address = pop() + offset;
                                        if (memoryIndex != 1u || address + width > external.size || address + width < address)
                                            throw trap{ "out of bounds memory access" };
                                        std::memcpy(external.base + address, &bits, width);
                                    }
                                }
                                break;
                            case 0x20: stack.push_back(locals.at(code.u())); break;
                            case 0x21: locals.at(code.u()) = pop(); break;
                            case 0x22: locals.at(code.u()) = stack.back(); break;
                            case 0x23: stack.push_back(iGlobals.at(code.u())); break;
                            case 0x24: iGlobals.at(code.u()) = pop(); break;
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
                            default:
                                throw trap{ "unsupported instruction 0x" + to_hex(op) };
                            }
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
                                    r.byte();
                                    iGlobals.push_back(constant(r));
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
                                if (r.name() == bytecode::FramesSection)
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
                                    for (std::size_t i = 0u; i < length; ++i)
                                        iMemory[offset + i] = r.byte();
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
                        auto token = [&](std::uint64_t aValue) { iTokens.push_back(aValue); iEnds.push_back(0u); iElses.push_back(0u); };
                        std::vector<std::size_t> open;
                        while (r.position() < f.codeEnd)
                        {
                            auto const position = iTokens.size();
                            auto const op = r.byte();
                            token(op);
                            switch (op)
                            {
                            case 0x02: case 0x03: case 0x04:
                                token(r.byte());
                                open.push_back(position);
                                break;
                            case 0x05:
                                iElses[open.back()] = iTokens.size();
                                break;
                            case 0x0B:
                                if (!open.empty())
                                {
                                    iEnds[open.back()] = iTokens.size();
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
                                        iEnds.resize(position);
                                        iElses.resize(position);
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
                                        iEnds.resize(position);
                                        iElses.resize(position);
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
                        Print,
                        Input,
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
                        Runtime
                    };
                    import_kind resolve_import(std::uint32_t aFunction)
                    {
                        static std::unordered_map<std::string_view, import_kind> const sHostFunctions =
                        {
                            { "neo::print", import_kind::Print },
                            { "neo::input", import_kind::Input },
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
                            { "neos::allocate", import_kind::Allocate } // (memory (zeroed) of a size: its address)
                        };
                        auto const& name = *iFunctions.at(aFunction).import;
                        if (auto const host = sHostFunctions.find(name); host != sHostFunctions.end())
                            return host->second;
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
                        case import_kind::Print:
                            iOutput << view_string(as_u32(aBuffer[0])) << std::flush;
                            break;
                        case import_kind::Input:
                            {
                                std::string line;
                                std::getline(iInput, line);
                                if (!line.empty() && line.back() == '\r')
                                    line.pop_back();
                                aBuffer[0] = of_u32(allocate_string(line));
                            }
                            break;
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
                    // (a global's storage (its word): the interpreter and generated code use it (it does not move))
                    value* global_address(std::uint32_t aIndex)
                    {
                        return aIndex < iGlobals.size() ? &iGlobals[aIndex] : nullptr;
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
                    std::vector<std::uint8_t> iModule;
                    std::vector<std::uint64_t> iTokens;   ///< decoded function bodies
                    std::vector<std::size_t> iEnds;       ///< block/loop/if token -> token after its end
                    std::vector<std::size_t> iElses;      ///< if token -> token after its else (0 if none)
                    std::vector<function_type> iTypes;
                    std::vector<function> iFunctions;
                    linear_memory iMemory;
                    jit::memory iMemoryView;   ///< (for the JIT)
                    std::vector<value> iGlobals;
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

                void* host_thread_context()
                {
                    // (the thread's last one is remembered, so that a runtime plugin that asks again (e.g. for each call) does not take the
                    // lock)
                    auto& all = the_thread_contexts();
                    thread_local running* tLast = nullptr;
                    thread_local std::uint64_t tLastGeneration = 0u;
                    if (tLast != nullptr && tLast->program == tRunning.program && tLast->native == tRunning.native &&
                        tLastGeneration == all.generation.load(std::memory_order_acquire))
                        return tLast;
                    std::lock_guard<std::mutex> lock{ all.mutex };
                    tLastGeneration = all.generation.load(std::memory_order_relaxed);
                    return tLast = &all.contexts.try_emplace(std::make_pair(tRunning.program, tRunning.native), tRunning).first->second;
                }

                void host_thread_attach(void* aContext)
                {
                    tRunning = aContext != nullptr ? *static_cast<running*>(aContext) : running{};
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

                void host_external_memory(void* aBase, unsigned long long aSize)
                {
                    auto& external = jit::external_memory();
                    external.base = static_cast<std::uint8_t*>(aBase);
                    external.size = aSize;
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

                // (the end of input is not kept: a stream that has more input later (e.g. a terminal's) can be read again)
                size_t host_std_read(int aStream, char* aBuffer, size_t aCount)
                {
                    auto* const stream = input_stream(aStream);
                    if (stream == nullptr || aBuffer == nullptr)
                        return 0u;
                    stream->read(aBuffer, static_cast<std::streamsize>(aCount));
                    auto const result = static_cast<size_t>(stream->gcount());
                    stream->clear();
                    return result;
                }

                int host_std_getc(int aStream)
                {
                    auto* const stream = input_stream(aStream);
                    if (stream == nullptr)
                        return -1;
                    auto const c = stream->get();
                    if (c == std::istream::traits_type::eof())
                    {
                        stream->clear();
                        return -1;
                    }
                    return static_cast<unsigned char>(std::istream::traits_type::to_char_type(c));
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

                neos_rt_host const sHost{ NEOS_RUNTIME_PLUGIN_VERSION, host_call, host_coroutine_new, host_coroutine_resume, host_coroutine_suspend,
                    host_coroutine_delete, host_thread_context, host_thread_attach, host_load, host_run, host_restricted, host_trap, host_frames,
                    host_function_frame, host_arguments, host_values, host_load_n, host_std_write, host_std_read, host_std_getc, host_std_ungetc,
                    host_std_flush, host_global, host_unload, host_external_memory };
            }

            void const* runtime::host()
            {
                return &sHost;
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
                    jit::compiled_module_ptr native;
                    if (useJit)
                        native = m.compile();
                    iJit = static_cast<bool>(native);
                    auto const previous = tRunning;
                    tRunning = running{ &m, native.get() };
                    struct restore { running previous; ~restore() { tRunning = previous; } } const restoreRunning{ previous };
                    // (then the objects it did not release (it is still running))
                    struct release { machine& m; ~release() { m.release_objects(); } } const releasing{ m };
                    // (when it returns, the plugins finish their work (e.g. threads) while the program's state exists)
                    struct end { ~end() { runtime::program_end(); } } const ending;
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