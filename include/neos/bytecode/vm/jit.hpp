/*
  jit.hpp

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

#pragma once

#include <neos/neos.hpp>
#include <cstdint>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <span>
#include <string_view>

namespace neos
{
    namespace bytecode
    {
        namespace vm
        {
            namespace jit
            {
                // Compiles the (pre-decoded) WebAssembly functions of a module to native code.
                // Supported targets: x86-64 (Windows x64 and System V ABIs); elsewhere available() is false and the
                // interpreter is used.

                using value = std::uint64_t;

                // checked integer arithmetic: the VM's functions neos::checked_<op>_<type> (op: add, sub, mul or neg; type: i32, 
                // u32, i64 or u64) return the result of the operation, or trap (integer overflow) if it overflows the type; a code:
                // its operation (0 add, 1 sub, 2 mul, 3 neg) | 4 (64 bits) | 8 (unsigned)
                inline std::optional<std::uint8_t> checked_arithmetic(std::string_view aName)
                {
                    std::string_view constexpr Prefix = "neos::checked_";
                    if (!aName.starts_with(Prefix))
                        return {};
                    aName.remove_prefix(Prefix.size());
                    static std::string_view const sOperations[] = { "add_", "sub_", "mul_", "neg_" };
                    static std::string_view const sTypes[] = { "i32", "i64", "u32", "u64" };
                    for (std::uint8_t o = 0u; o < 4u; ++o)
                        if (aName.starts_with(sOperations[o]))
                            for (std::uint8_t t = 0u; t < 4u; ++t)
                                if (aName.substr(4u) == sTypes[t] && !(o == 3u && t >= 2u))
                                    return static_cast<std::uint8_t>(o | (t << 2u));
                    return {};
                }

                // a runtime plugin function that generated code calls directly (see neos/runtime/plugin.h): its C function, its C
                // signature (neos_rt_type values) and its plugin's error function (null if it has none)
                struct plugin_function
                {
                    void const* function = nullptr;
                    std::vector<std::uint8_t> parameters;
                    std::uint8_t result = 0u;
                    char const* (*error)() = nullptr;
                    long const* errorPending = nullptr;     ///< (if not null: error is called only if it is nonzero)
                    bool common = false;                    ///< (NEOS_RT_COMMON: optimized code calls it (it is not a slow path to deoptimize at))
                };

                // how generated code calls an imported function
                enum class import_call : std::uint8_t
                {
                    Host,           ///< through the host function (compile's aHost)
                    Plugin,         ///< a runtime plugin function (function_description::plugin), directly if its signature allows
                    ObjectRetain,   ///< neos::object_retain (host_interface::objectRetain)
                    ObjectRelease   ///< neos::object_release (host_interface::objectRelease)
                };

                struct function_description
                {
                    std::vector<std::uint8_t> const* parameters = nullptr; ///< value types
                    std::vector<std::uint8_t> const* results = nullptr;    ///< value types
                    std::vector<std::uint8_t> const* locals = nullptr;     ///< value types of locals declared by the body
                    std::uint32_t type = 0u;                               ///< (its type's index: call_indirect's)
                    bool imported = false;
                    std::optional<std::uint8_t> checked;                   ///< (an import that is checked integer arithmetic)
                    import_call call = import_call::Host;                  ///< (an import)
                    std::optional<plugin_function> plugin;                 ///< (import_call::Plugin)
                    std::size_t codeBegin = 0u; ///< token index
                    std::size_t codeEnd = 0u;   ///< token index
                    bool inlinable = false;     ///< (the optimizing tier may inline its calls)
                    bool frameless = false;     ///< (it has no frame of its own (e.g. a language's helper): it is not optimized itself)
                };

                // linear memory (updated by the VM when it moves or grows)
                struct memory
                {
                    std::uint8_t* base = nullptr;
                    std::uint64_t size = 0u;
                    bool fixed = false;     ///< (an external memory whose base and size never change: code uses them as constants)
                };

                // the host's objects (see neos/runtime/plugin.h): a handle is the index of a slot (0: no object), which counts the 
                // references the program holds (a slot that is in use holds one reference to its object); a free slot's count is 
                // 0 and its next is the next free slot (0: none). Generated code uses the slots itself (and calls the host's object 
                // functions (see host_interface) for the rest: a new slot when none is free, and errors)
                struct object_slot
                {
                    void* object = nullptr;
                    std::uint32_t count = 0u;
                    std::uint32_t next = 0u;
                };
                struct object_table
                {
                    object_slot* slots = nullptr;   ///< (updated by the VM when they move or grow)
                    std::uint32_t size = 0u;
                    std::uint32_t free = 0u;        ///< (the first free slot: 0 if none)
                };

                // the host's objects (see neos/runtime/plugin.h) and runtime plugins' errors, for the imports generated code calls 
                // directly (each function may throw: the call traps)
                struct host_interface
                {
                    void* host = nullptr;
                    void* (*objectPointer)(void* aHost, std::uint32_t aHandle) = nullptr;       ///< (an object's address; null for 0)
                    std::uint32_t (*objectHandle)(void* aHost, void* aObject) = nullptr;        ///< (a handle for a new reference; 0 for null)
                    void (*objectRetain)(void* aHost, std::uint32_t aHandle) = nullptr;
                    void (*objectRelease)(void* aHost, std::uint32_t aHandle) = nullptr;
                    // (the error a runtime plugin function (aFunction) raised: thrown, or kept for the program (the function's result 
                    // is then zero))
                    void (*pluginError)(void* aHost, std::uint32_t aFunction, char const* aError) = nullptr;
                    // (strings: a string in memory (its address) as a C function takes it, in place (linear memory does not move: valid
                    // while the string is); and a C function's string result copied into memory (its address))
                    char const* (*stringPointer)(void* aHost, std::uint32_t aAddress) = nullptr;
                    std::uint32_t (*stringAddress)(void* aHost, char const* aText) = nullptr;
                };

                struct module_description
                {
                    std::vector<function_description> functions;
                    std::vector<std::pair<std::vector<std::uint8_t> const*, std::vector<std::uint8_t> const*>> types; ///< (parameters and results: call_indirect's)
                    std::vector<std::uint64_t> const* tokens = nullptr; ///< decoded code: an opcode token followed by its immediates
                    std::vector<value>* globals = nullptr;
                    std::vector<std::uint8_t> const* globalTypes = nullptr;
                    std::vector<std::uint8_t> const* fixedGlobals = nullptr;   ///< (globals whose values do not change again (see neos_rt_host::fix_global): nonzero)
                    memory const* linearMemory = nullptr;
                    object_table* objects = nullptr;    ///< (null: generated code calls the host's object functions)
                    host_interface host;
                };

                // (an imported function: aBuffer holds its arguments and receives its results; its size is the larger of 
                // their counts)
                using host_function = std::function<void(std::uint32_t aFunction, value* aBuffer)>;

                class compiled_module;
                struct compiled_module_deleter { void operator()(compiled_module* aModule) const; };
                using compiled_module_ptr = std::unique_ptr<compiled_module, compiled_module_deleter>;

                // an invocation of generated code (see invoke): while it calls an import, its frames can be walked
                struct invocation
                {
                    void const* context = nullptr;  ///< (the generated code's, while it runs)
                };

                bool available();
                // returns null if the module uses an instruction the JIT does not support (the interpreter is then used)
                compiled_module_ptr compile(module_description const& aModule, host_function aHost);
                // (the external memories (memory indices 1 to NEOS_RT_EXTERNAL_MEMORIES: see neos_rt_host::external_memory_at), process-wide;
                // an index beyond them: a memory of no size (an access traps))
                memory& external_memory(std::uint64_t aIndex);
                // traps are thrown as std::runtime_error (exceptions thrown by host functions propagate unchanged)
                std::vector<value> invoke(compiled_module& aModule, std::uint32_t aFunction, std::span<value const> aArguments, 
                    invocation* aInvocation = nullptr);
                // (as invoke: its results in aResults (as many as the function has: their count), with nothing allocated for them)
                std::size_t invoke_into(compiled_module& aModule, std::uint32_t aFunction, std::span<value const> aArguments, value* aResults,
                    invocation* aInvocation = nullptr);
                // (the frames of an invocation that is calling an import (from within that call), innermost first: aVisit(token, id) 
                // for each (the token of the call it is making, and an id of the frame while it runs), until aVisit returns false)
                void walk(compiled_module const& aModule, invocation const& aInvocation, std::function<bool(std::size_t aToken, std::uint64_t aId)> const& aVisit);
                // (the lowest address of the stack of the coroutine that the calling thread is running (0: none, the thread's own 
                // stack): generated code's stack limit)
                void set_coroutine_stack(std::uint64_t aLow);
            }
        }
    }
}
