/*
  api.cpp

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

// The functions that compiled code calls (javascript::f): values are i64s (see js.hpp); a function that throws returns the
// value Exception, with the exception pending (javascript::catch takes it)

#include <algorithm>
#include <typeinfo>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <unordered_set>
#include <cstddef>
#include <neos/language/javascript.hpp>
#include "js.hpp"
#include "api.hpp"
#include "gc.hpp"
#include "typed_arrays.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        neos_rt_host const* sHost = nullptr;

        // (proper tail calls: a strict function's call in tail position leaves the call pending and returns (Exception, with no exception
        // pending); the code that called the function (the runtime here, or a compiled call site) then makes the call (so the stack
        // does not grow))
        struct tail_call_record
        {
            bool pending = false;
            value callee = Undefined;
            value thisValue = Undefined;
            value_list arguments;
        };
        // (the calls' state: the isolate's (its thread's: an isolate runs on one thread at a time), not thread_locals (the runtime is
        // a module loaded at run time: a thread_local's access is not cheap))
        struct call_state
        {
            value pending = Undefined;
            std::vector<value> extraArguments;
            tail_call_record tail;
            bool deferTail = false;     ///< (the next compiled call leaves its tail call pending (the trampoline makes it))
            std::uint32_t nativeTailCalls = 0u;     ///< (built-ins' tail calls made as calls, running (see tail_call))
        };
        constexpr std::uint32_t NativeTailCalls = 256u;
        isolate_local<call_state> const sCallState;
        inline call_state& calls() { return *sCallState; }
        root_tracer const sTailRoots{ [](tracer& aTracer)
        {
            // (a pending tail call's callee and this (its arguments are a rooted list))
            if (current_isolate() == nullptr)
                return;
            aTracer.mark(calls().tail.callee);
            aTracer.mark(calls().tail.thisValue);
        } };
        inline constexpr value TailPending = TagSpecial | 7u;

        value run_tail_calls()
        {
            // (the trampoline: the pending tail call, and the tail calls that it makes in turn)
            value r = TailPending;
            while (r == TailPending)
            {
                auto const callee = calls().tail.callee;
                auto const thisValue = calls().tail.thisValue;
                auto const arguments = std::move(calls().tail.arguments);
                calls().tail = {};
                auto const* f = is_object(callee) ? as_function(callee) : nullptr;
                calls().deferTail = f != nullptr && f->kind == function_kind::Compiled;
                r = javascript_runtime::call(callee, thisValue, arguments);
                calls().deferTail = false;
            }
            return r;
        }

        // (the layout of the heap that compiled code reads (neos::language::javascript::layout) is the runtime's)
        void check_layout()
        {
            static std::atomic<bool> sChecked = false;     // (once (an isolate that checks it at the same time as another checks it too))
            if (sChecked.exchange(true))
                return;
            namespace jsl = neos::language::javascript::layout;
            auto const& o = obj(the_realm().objectPrototype);
            auto const at = [&](void const* aField)
            {
                return static_cast<std::size_t>(static_cast<std::byte const*>(aField) - reinterpret_cast<std::byte const*>(&o));
            };
            bool const ok =
                at(&o.proto) == jsl::ObjectProto &&
                at(o.properties.shape_field()) == jsl::ObjectShape &&
                at(o.properties.values_field()) == jsl::ObjectValues &&
                at(o.properties.values_field()) + sizeof(std::uint64_t) == jsl::ObjectValueCount &&
                at(o.properties.values_field()) + sizeof(std::uint64_t) + sizeof(std::uint32_t) == jsl::ObjectValueCapacity &&
                at(o.properties.dictionary_field()) == jsl::ObjectDictionary &&
                at(&o.cls) == jsl::ObjectClass && sizeof(o.cls) == 1u &&
                at(&o.extensible) == jsl::ObjectExtensible && sizeof(o.extensible) == 1u &&
                offsetof(property, setter) == jsl::PropertySetter && AttributesDefault == jsl::AttributesDefault &&
                sizeof(property) == jsl::PropertySize &&
                offsetof(property, key) == jsl::PropertyKey &&
                offsetof(property, data) == jsl::PropertyData &&
                offsetof(property, attributes) == jsl::PropertyAttributes &&
                AttributeWritable == jsl::AttributeWritable &&
                AttributeAccessor == jsl::AttributeAccessor &&
                at(&o.handle) == jsl::CellHandle && sizeof(o.handle) == 4u &&
                ((sizeof(object) + 7u) & ~std::size_t{ 7u }) == jsl::ObjectSize &&
                ((sizeof(array_object) + 7u) & ~std::size_t{ 7u }) == jsl::ArraySize && MaxCellValues == jsl::MaxLiteralProperties &&
                offsetof(allocation_state, freeLists) == jsl::AllocFreeLists &&
                offsetof(allocation_state, allocatedBytes) == jsl::AllocBytes &&
                offsetof(allocation_state, compiledLimit) == jsl::AllocLimit && offsetof(allocation_state, megamorphic) == jsl::AllocMegamorphic &&
                offsetof(allocation_state, bump) == jsl::AllocBump && offsetof(allocation_state, bumpLimit) == jsl::AllocBumpLimit &&
                offsetof(allocation_state, plainArrays) == jsl::AllocPlainArrays &&
                MegamorphicEntries == jsl::MegamorphicEntries && sizeof(megamorphic_entry) == jsl::MegamorphicEntrySize &&
                offsetof(megamorphic_entry, key) == jsl::MegamorphicKey && offsetof(megamorphic_entry, position) == jsl::MegamorphicPosition &&
                offsetof(megamorphic_entry, reserved) == jsl::MegamorphicWritable;
            environment const probe{ Undefined, 1u };
            auto const field = [](auto const& aCell, void const* aField)
            {
                return static_cast<std::size_t>(static_cast<std::byte const*>(aField) - reinterpret_cast<std::byte const*>(&aCell));
            };
            function_object const f{ Undefined, function_kind::Native };
            bool const environmentOk = field(probe, &probe.parent) == jsl::EnvironmentParent &&
                field(probe, &probe.slots) == jsl::EnvironmentSlots && field(f, &f.environment) == jsl::FunctionEnvironment &&
                field(f, &f.dispatch) == jsl::FunctionDispatch && static_cast<std::uint32_t>(object_class::Function) == jsl::ClassFunction &&
                field(f, &f.cls) == jsl::ObjectClass;
            if (!environmentOk)
                std::fprintf(stderr, "JavaScript runtime: environment parent %zu, slots %zu; function environment %zu\n", field(probe, &probe.parent),
                    field(probe, &probe.slots), field(f, &f.environment));
            javascript_runtime::string const text{};
            bool const stringOk = field(text, &text.length) == jsl::StringLength && sizeof(text.length) == 4u &&
                field(text, &text.charsOffset) == jsl::StringChars;
            if (!stringOk)
                std::fprintf(stderr, "JavaScript runtime: string length %zu, chars %zu\n", field(text, &text.length), field(text, &text.charsOffset));
            array_object const array{ Undefined };
            bool const arrayOk = field(array, &array.cls) == jsl::ObjectClass && static_cast<std::uint32_t>(object_class::Array) == jsl::ClassArray &&
                static_cast<std::uint32_t>(object_class::HTMLDDA) == jsl::ClassHTMLDDA &&
                field(array, array.elements.offset_field()) == jsl::ArrayElements && field(array, &array.sparse) == jsl::ArraySparse &&
                field(array, &array.length) == jsl::ArrayLength && sizeof(array.length) == 4u &&
                field(array, &array.lengthWritable) == jsl::ArrayLengthWritable && sizeof(array.lengthWritable) == 1u &&
                sizeof(array.sparse) == 1u && sizeof(array.cls) == 1u;
            if (!ok || !arrayOk)
                std::fprintf(stderr, "JavaScript runtime: object prototype %zu, shape %zu, values %zu, dictionary %zu, class %zu, extensible %zu; array elements %zu, length %zu, sparse %zu\n",
                    at(&o.proto), at(o.properties.shape_field()), at(o.properties.values_field()), at(o.properties.dictionary_field()), at(&o.cls),
                    at(&o.extensible), field(array, array.elements.offset_field()), field(array, &array.length), field(array, &array.sparse));
            if (!ok || !environmentOk || !arrayOk || !stringOk)
            {
                std::fputs("JavaScript runtime: the heap's layout is not the one compiled code reads (neos::language::javascript::layout)\n", stderr);
                std::abort();
            }
        }

        template <typename F>
        value guard(F&& aFunction)
        {
            try
            {
                return aFunction();
            }
            catch (js_exception const& e)
            {
                calls().pending = e.thrown;
                return Exception;
            }
            catch (std::bad_alloc const&)
            {
                try
                {
                    calls().pending = make_error(the_realm().rangeErrorPrototype, ascii("Out of memory"));
                }
                catch (...)
                {
                }
                return Exception;
            }
        }

        // (a value from compiled code: a handle that refers to nothing is undefined)
        value checked(value aValue)
        {
            return valid(aValue) ? aValue : Undefined;
        }

        value environment_at(value aEnvironment, std::int32_t aHops)
        {
            auto e = aEnvironment;
            for (; aHops > 0; --aHops)
                e = cell_of<environment>(e).parent;
            return e;
        }


        std::string name_text(value aName)
        {
            return utf8(aName);
        }
    }

    neos_rt_host const* host()
    {
        return sHost;
    }

    void write_standard(int aStream, std::string_view aText)
    {
        if (sHost != nullptr && sHost->version >= 9u && sHost->std_write != nullptr)
            sHost->std_write(aStream, aText.data(), aText.size());
        else
            std::fwrite(aText.data(), 1u, aText.size(), aStream == 2 ? stderr : stdout);
    }

    void flush_standard(int aStream)
    {
        if (sHost != nullptr && sHost->version >= 9u && sHost->std_flush != nullptr)
            sHost->std_flush(aStream);
        else
            std::fflush(aStream == 2 ? stderr : stdout);
    }

    std::unordered_map<value, global_binding>& global_lexicals()
    {
        return the_realm().lexicals;
    }

    void set_host(neos_rt_host const* aHost)
    {
        sHost = aHost;
        // (the heap's region, as the external memory that compiled code reads: before the programs that use it are compiled (they 
        // use its address and size as constants))
        if (aHost != nullptr)
            publish_heap();
    }

    void* current_program()
    {
        return sHost != nullptr ? sHost->thread_context() : nullptr;
    }

    value& pending_exception()
    {
        return calls().pending;
    }

    std::vector<value>& extra_arguments()
    {
        return calls().extraArguments;
    }

    value call_compiled(function_object& aFunction, value aThis, arguments aArguments, value aNewTarget)
    {
        return call_compiled_index(aFunction.program, aFunction.index, value_of(aFunction), aThis, aArguments, aNewTarget);
    }

    value call_compiled_index(void* aProgram, std::uint32_t aIndex, value aCallee, value aThis, arguments aArguments, value aNewTarget)
    {
        // (the arguments after the first four are pushed on the extra arguments stack while it runs)
        bool const deferTail = std::exchange(calls().deferTail, false);
        auto& extra = calls().extraArguments;
        auto const base = extra.size();
        for (std::size_t i = RegisterArguments; i < aArguments.size(); ++i)
            extra.push_back(aArguments[i]);
        neos_rt_type const types[] = { NEOS_RT_I64, NEOS_RT_I64, NEOS_RT_I64, NEOS_RT_I32, NEOS_RT_I64, NEOS_RT_I64, NEOS_RT_I64, NEOS_RT_I64 };
        neos_rt_value values[8];
        values[0].i64 = static_cast<long long>(aCallee);
        values[1].i64 = static_cast<long long>(aThis);
        values[2].i64 = static_cast<long long>(aNewTarget);
        values[3].i32 = static_cast<int>(aArguments.size());
        for (std::size_t i = 0u; i < 4u; ++i)
            values[4u + i].i64 = static_cast<long long>(i < aArguments.size() ? aArguments[i] : Undefined);
        neos_rt_value result{};
#if defined(_WIN32)
        // (generated code runs on a fiber of its own: this stack's live part is scanned while it runs)
        NEOS_JS_SPILL_REGISTERS();
        stack_switch const switching{ stack_pointer() };
#endif
        void* previous = nullptr;
        if (aProgram != nullptr && aProgram != current_program())
        {
            previous = current_program();
            sHost->thread_attach(aProgram);
        }
        auto const trap = sHost->call(aIndex, 8u, types, values, NEOS_RT_I64, &result);
        if (previous != nullptr)
            sHost->thread_attach(previous);
        extra.resize(base);
        if (trap != nullptr)
        {
            std::string const message{ trap };
            if (message.find("stack") != std::string::npos)
                throw_range_error("Maximum call stack size exceeded");
            throw_error(the_realm().errorPrototype, "internal error: " + message);
        }
        auto const r = static_cast<value>(result.i64);
        if (r == Exception && calls().tail.pending)
            return deferTail ? TailPending : run_tail_calls();
        if (r == Exception)
        {
            auto const thrown = calls().pending;
            calls().pending = Undefined;
            throw js_exception{ thrown };
        }
        return r;
    }

    namespace api
    {
        // --- initialization

        value init(std::int32_t aDispatcher)
        {
            return guard([&]()
            {
                (void)aDispatcher;
                // (the thread's isolate (made now if it has none): its program is the first that uses it (programs that it compiles
                // (e.g. eval's) are run by it, on its thread))
                if (auto& h = the_heap(); h.mainProgram == nullptr)
                    h.mainProgram = current_program();
                if (!realm_initialized())
                    initialize_realm();
                check_layout();
                publish_heap();
                return Undefined;
            });
        }

        value string(char const* aText)
        {
            return intern(utf16(aText != nullptr ? std::string_view{ aText } : std::string_view{}));
        }

        value global_this()
        {
            return the_realm().global;
        }

        unsigned long long* program_word(std::int32_t aGlobal);
        void fix_program_word(std::int32_t aGlobal);

        value fixed_global(value aValue, std::int32_t aGlobal)
        {
            // (the running program's global aGlobal: aValue, which it keeps (see neos_rt_host::fix_global), e.g. a literal's string)
            *program_word(aGlobal) = aValue;
            fix_program_word(aGlobal);
            return Undefined;
        }

        std::int32_t finish(value aResult)
        {
            // (the script's function returned: an exception it did not handle is reported (the program's exit status is then 1); else the
            // jobs it queued run)
            if (aResult == Exception)
            {
                report_uncaught(calls().pending);
                calls().pending = Undefined;
                return 1;
            }
            return run_jobs() ? 0 : 1;
        }

        // --- exceptions

        value throw_(value aValue)
        {
            calls().pending = checked(aValue);
            return Exception;
        }

        value catch_()
        {
            auto const result = calls().pending;
            calls().pending = Undefined;
            return result;
        }

        value throw_tdz(value aName)
        {
            return guard([&]() -> value { throw_reference_error("Cannot access '" + name_text(aName) + "' before initialization"); });
        }

        value throw_const(value aName)
        {
            (void)aName;
            return guard([&]() -> value { throw_type_error("Assignment to constant variable."); });
        }

        value throw_this(value)
        {
            return guard([&]() -> value { throw_reference_error("Must call super constructor in derived class before accessing 'this' or returning from derived constructor"); });
        }

        value throw_super_twice()
        {
            return guard([&]() -> value { throw_reference_error("Super constructor may only be called once"); });
        }

        value throw_super_delete()
        {
            return guard([&]() -> value { throw_reference_error("Unsupported reference to 'super'"); });
        }

        value throw_invalid_assignment()
        {
            return guard([&]() -> value { throw_reference_error("Invalid left-hand side in assignment"); });
        }

        // --- globals

        value global_declare(value aName, std::int32_t aKind)
        {
            // (aKind: 0 var, 1 function, 2 let, 3 const or class; | 8: only the checks (GlobalDeclarationInstantiation checks every
            // declaration before it creates any))
            return guard([&]() -> value
            {
                auto const name = intern(aName);
                auto& g = obj(the_realm().global);
                auto& lexicals = global_lexicals();
                bool const checkOnly = (aKind & 8) != 0;
                aKind &= 7;
                if (checkOnly)
                {
                    if (lexicals.contains(name))
                        throw_syntax_error("Identifier '" + name_text(name) + "' has already been declared");
                    auto const existing = g.get_own_property(name);
                    if (aKind >= 2)
                    {
                        if (existing && !existing->configurable.value_or(false))
                            throw_syntax_error("Identifier '" + name_text(name) + "' has already been declared");
                    }
                    else if (aKind == 1)
                    {
                        // (CanDeclareGlobalFunction)
                        if (!existing ? !g.is_extensible() : (!existing->configurable.value_or(false) &&
                            (existing->is_accessor() || !existing->writable.value_or(false) || !existing->enumerable.value_or(false))))
                            throw_type_error("Cannot declare global function " + name_text(name));
                    }
                    else if (!existing && !g.is_extensible())
                        throw_type_error("Cannot declare global variable " + name_text(name));     // (CanDeclareGlobalVar)
                    return Undefined;
                }
                if (aKind >= 2)
                {
                    if (lexicals.contains(name))
                        throw_syntax_error("Identifier '" + name_text(name) + "' has already been declared");
                    if (auto const existing = g.get_own_property(name); existing && !existing->configurable.value_or(false))
                        throw_syntax_error("Identifier '" + name_text(name) + "' has already been declared");
                    auto& binding = lexicals.emplace(name, global_binding{ Empty, aKind == 3 }).first->second;
                    // (a binding that the program reads and writes in place: its value is the program's word (its mirror))
                    if (auto const m = the_realm().mirrors.find(name); m != the_realm().mirrors.end() && !m->second.empty())
                        binding.slot = m->second.front();
                    global_mirror_update(the_realm(), name);
                    return Undefined;
                }
                if (lexicals.contains(name))
                    throw_syntax_error("Identifier '" + name_text(name) + "' has already been declared");
                if (aKind == 0)
                {
                    if (!g.get_own_property(name) && g.is_extensible())
                        g.define_own_property(name, property_descriptor::data(Undefined, AttributeWritable | AttributeEnumerable));
                }
                return Undefined;
            });
        }

        value global_define_function(value aName, value aFunction)
        {
            return guard([&]() -> value
            {
                auto const name = intern(aName);
                auto& g = obj(the_realm().global);
                auto const existing = g.get_own_property(name);
                if (!existing || existing->configurable.value_or(false))
                    define_property_or_throw(the_realm().global, name, property_descriptor::data(aFunction, AttributeWritable | AttributeEnumerable));
                else
                {
                    if (existing->is_accessor() || !(existing->writable.value_or(false) && existing->enumerable.value_or(false)))
                        throw_type_error("Cannot redefine global function " + name_text(name));
                    property_descriptor d;
                    d.valueField = aFunction;
                    define_property_or_throw(the_realm().global, name, d);
                }
                return Undefined;
            });
        }

        value global_get(value aName)
        {
            return guard([&]() -> value
            {
                auto const name = intern(aName);
                auto& lexicals = global_lexicals();
                if (auto const b = lexicals.find(name); b != lexicals.end())
                {
                    if (b->second.get() == Empty)
                        throw_reference_error("Cannot access '" + name_text(name) + "' before initialization");
                    return b->second.get();
                }
                auto const global = the_realm().global;
                // (an own data property of an ordinary global object: its value (as [[HasProperty]] and [[Get]] would find it))
                if (auto& g = obj(global); g.cls == object_class::Ordinary)
                    if (auto const p = g.own(name); p != nullptr && !p->accessor())
                        return p->data();
                if (!has_property(global, name))
                    throw_reference_error(name_text(name) + " is not defined");
                return get(global, name);
            });
        }

        std::int32_t intrinsics_original(std::int32_t aMask)
        {
            // (the compiler's intrinsics (bits of aMask: 1 << its number): whether Math.<name>, as compiled code would find it, is the built-in
            // function, and finding it runs no code: Math is a data property of the global object (not shadowed by a global lexical
            // declaration) whose value is an ordinary object with the function as its own data property)
            static char const* const sNames[] = { nullptr, "imul", "floor", "ceil", "trunc", "sqrt", "abs" };
            auto const mathName = ascii("Math");
            if (global_lexicals().contains(mathName))
                return 0;
            auto const math = obj(the_realm().global).own(mathName);
            if (math == nullptr || math->accessor() || !is_object(math->data()) || obj(math->data()).cls != object_class::Ordinary)
                return 0;
            for (std::uint32_t i = 1u; i < std::size(sNames); ++i)
                if ((static_cast<std::uint32_t>(aMask) & (1u << i)) != 0u)
                {
                    auto const f = obj(math->data()).own(ascii(sNames[i]));
                    if (f == nullptr || f->accessor() || !math_intrinsic(f->data(), i))
                        return 0;
                }
            return 1;
        }

        value global_get_typeof(value aName)
        {
            return guard([&]() -> value
            {
                auto const name = intern(aName);
                auto& lexicals = global_lexicals();
                if (auto const b = lexicals.find(name); b != lexicals.end())
                {
                    if (b->second.get() == Empty)
                        throw_reference_error("Cannot access '" + name_text(name) + "' before initialization");
                    return b->second.get();
                }
                auto const global = the_realm().global;
                if (!has_property(global, name))
                    return Undefined;
                return get(global, name);
            });
        }

        std::int32_t global_resolved(value aName)
        {
            // (a strict assignment's reference, resolved before its value is evaluated: global_set's flags (strict; 2: unresolvable))
            auto const name = intern(aName);
            if (global_lexicals().contains(name) || has_property(the_realm().global, name))
                return 1;
            return 3;
        }

        value global_set(value aName, value aValue, std::int32_t aStrict)
        {
            // (aStrict: 1 strict; 2 the reference was unresolvable (strict: a ReferenceError))
            return guard([&]() -> value
            {
                auto const name = intern(aName);
                if ((aStrict & 2) != 0)
                    throw_reference_error(name_text(name) + " is not defined");
                auto& lexicals = global_lexicals();
                if (auto const b = lexicals.find(name); b != lexicals.end())
                {
                    if (b->second.get() == Empty)
                        throw_reference_error("Cannot access '" + name_text(name) + "' before initialization");
                    if (b->second.constant)
                        throw_type_error("Assignment to constant variable.");
                    b->second.set(aValue);
                    global_mirror_update(the_realm(), name);
                    return aValue;
                }
                auto const global = the_realm().global;
                if ((aStrict & 1) != 0 && !has_property(global, name))
                    throw_reference_error(name_text(name) + " is not defined");
                set(global, name, aValue, (aStrict & 1) != 0);
                return aValue;
            });
        }

        value global_init(value aName, value aValue)
        {
            auto const name = intern(aName);
            global_lexicals()[name].set(aValue);
            global_mirror_update(the_realm(), name);
            return aValue;
        }

        unsigned long long* program_word(std::int32_t aGlobal)
        {
            // (a global of the running program (see neos_rt_host::global): the program cannot run without it)
            auto const* h = host();
            auto* const word = h != nullptr && h->version >= 10u && h->global != nullptr ? h->global(static_cast<unsigned>(aGlobal)) : nullptr;
            if (word == nullptr)
            {
                std::fprintf(stderr, "javascript: the host does not provide the program's globals\n");
                std::abort();
            }
            return word;
        }

        void fix_program_word(std::int32_t aGlobal)
        {
            // (a global of the running program's that keeps its value (see neos_rt_host::fix_global))
            auto const* h = host();
            if (h != nullptr && h->version >= 15u && h->fix_global != nullptr)
                h->fix_global(static_cast<unsigned>(aGlobal));
        }

        // (the programs' globals that the runtime uses (until their programs end: see program_ended): mirrors of global bindings (see
        // global_mirror) and words whose values are kept (that a program may compare with others, e.g. the function that a call site
        // calls directly: a value that was collected could be the same as a new one))
        struct program_word_use
        {
            void* program;
            unsigned long long* word;
            realm* mirrorRealm;     ///< (a mirror: its realm and name; else the word's value is kept)
            value name;
        };
        std::vector<program_word_use>& program_words()
        {
            static isolate_local<std::vector<program_word_use>> sWords;
            return *sWords;
        }
        root_tracer const sWordRoots{ [](tracer& aTracer)
        {
            for (auto const& w : program_words())
            {
                if (w.mirrorRealm == nullptr)
                    aTracer.mark(static_cast<value>(*w.word));
                else
                    aTracer.mark(w.name);
            }
        } };

        value global_root(std::int32_t aGlobal)
        {
            program_words().push_back(program_word_use{ current_program(), program_word(aGlobal), nullptr, 0u });
            return Undefined;
        }

        value global_mirror(value aName, std::int32_t aGlobal)
        {
            // (the program's global aGlobal mirrors the global binding aName (see realm::mirrors): reading it is then a read of the
            // global (the program reads the binding the slow way while the global is Empty))
            auto const name = intern(aName);
            // (the program writes the global lexical bindings that it declares in place: it cannot run without its mirrors)
            auto* const word = program_word(aGlobal);
            auto& r = the_realm();
            if (auto& g = obj(r.global); !g.mirrored)
            {
                g.mirrored = true;
                g.properties.make_dictionary();     // (the property caches do not use it)
            }
            r.mirrors[name].push_back(word);
            program_words().push_back(program_word_use{ current_program(), word, &r, name });
            global_mirror_update(r, name);
            return Undefined;
        }

        value global_delete(value aName)
        {
            return guard([&]() -> value
            {
                auto const name = intern(aName);
                if (global_lexicals().contains(name))
                    return False;
                return boolean(obj(the_realm().global).delete_property(name));
            });
        }

        // --- environments

        value env_new(value aParent, std::int32_t aSize)
        {
            auto& e = the_heap().make<environment>(aParent, static_cast<std::uint32_t>(aSize));
            return TagInternal | e.handle;
        }

        value env_copy(value aEnvironment)
        {
            auto& source = cell_of<environment>(aEnvironment);
            auto& e = the_heap().make<environment>(source.parent, static_cast<std::uint32_t>(source.slots.size()));
            std::copy(source.slots.begin(), source.slots.end(), e.slots.begin());
            e.names = source.names;
            e.withObject = source.withObject;
            return TagInternal | e.handle;
        }

        value env_get(value aEnvironment, std::int32_t aHops, std::int32_t aSlot)
        {
            return cell_of<environment>(environment_at(aEnvironment, aHops)).slots[static_cast<std::size_t>(aSlot)];
        }

        value env_get_checked(value aEnvironment, std::int32_t aHops, std::int32_t aSlot, value aName)
        {
            auto const v = env_get(aEnvironment, aHops, aSlot);
            if (v == Empty)
                return throw_tdz(aName);
            return v;
        }

        value env_get_this(value aEnvironment, std::int32_t aHops, std::int32_t aSlot, value aName)
        {
            auto const v = env_get(aEnvironment, aHops, aSlot);
            if (v == Empty)
                return throw_this(aName);
            return v;
        }

        value env_set(value aEnvironment, std::int32_t aHops, std::int32_t aSlot, value aValue)
        {
            cell_of<environment>(environment_at(aEnvironment, aHops)).slots[static_cast<std::size_t>(aSlot)] = aValue;
            return aValue;
        }

        value env_set_checked(value aEnvironment, std::int32_t aHops, std::int32_t aSlot, value aValue, value aName)
        {
            auto& slot = cell_of<environment>(environment_at(aEnvironment, aHops)).slots[static_cast<std::size_t>(aSlot)];
            if (slot == Empty)
                return throw_tdz(aName);
            slot = aValue;
            return aValue;
        }

        value env_bind_this(value aEnvironment, std::int32_t aHops, std::int32_t aSlot, value aValue)
        {
            auto& slot = cell_of<environment>(environment_at(aEnvironment, aHops)).slots[static_cast<std::size_t>(aSlot)];
            if (slot != Empty)
                return throw_super_twice();
            slot = aValue;
            return aValue;
        }

        value with_has(value aObject, value aName)
        {
            // (HasBinding of an object environment: the object has the property and its @@unscopables do not exclude it)
            return guard([&]() -> value
            {
                auto const name = intern(aName);
                if (!has_property(aObject, name))
                    return False;
                auto const unscopables = get(aObject, the_realm().symUnscopables);
                if (is_object(unscopables) && to_boolean(get(unscopables, name)))
                    return False;
                return True;
            });
        }

        // --- functions

        value closure(std::int32_t aNumber, std::int32_t aIndex, value aEnvironment, std::int32_t aFlags, value aName, std::int32_t aLength, value aSource)
        {
            return guard([&]() -> value
            {
                return make_closure(static_cast<std::uint32_t>(aNumber), static_cast<std::uint32_t>(aIndex), aEnvironment, aFlags, aName,
                    static_cast<std::uint32_t>(aLength), aSource);
            });
        }

        value function_env(value aCallee)
        {
            if (auto* f = as_function(aCallee))
                return f->environment;
            return Undefined;
        }

        value function_home(value aCallee)
        {
            if (auto* f = as_function(aCallee))
                return f->homeObject;
            return Undefined;
        }

        value set_home(value aFunction, value aHome)
        {
            if (auto* f = as_function(aFunction))
                f->homeObject = aHome;
            return aFunction;
        }

        value name_function(value aFunction, value aName)
        {
            return guard([&]() -> value
            {
                // (an anonymous function's name: unless it has a name of its own (a class's static name))
                if (auto* f = as_function(aFunction))
                {
                    auto existing = f->own(the_names().name);
                    if (existing == nullptr || (!existing->accessor() && is_string(existing->data()) && string_length(existing->data()) == 0u))
                        set_function_name(aFunction, to_property_key(aName));
                }
                return aFunction;
            });
        }

        std::int32_t callee_number(value aCallee)
        {
            // (the program's own function (that can be called): its number for the dispatcher; else -1)
            auto* f = as_function(aCallee);
            if (f == nullptr || f->kind != function_kind::Compiled || f->flavour == function_flavour::ClassConstructor || f->program != current_program())
                return -1;
            return static_cast<std::int32_t>(f->nativeId);
        }

        value push_arg(value aValue)
        {
            calls().extraArguments.push_back(aValue);
            return Undefined;
        }

        value pop_args(std::int32_t aCount)
        {
            calls().extraArguments.resize(calls().extraArguments.size() - static_cast<std::size_t>(aCount));
            return Undefined;
        }

        std::int32_t args_base(std::int32_t aCount)
        {
            if (aCount <= static_cast<std::int32_t>(RegisterArguments))
                return static_cast<std::int32_t>(calls().extraArguments.size());
            return static_cast<std::int32_t>(calls().extraArguments.size() - static_cast<std::size_t>(aCount - static_cast<std::int32_t>(RegisterArguments)));
        }

        value argument(std::int32_t aCount, std::int32_t aBase, std::int32_t aIndex)
        {
            if (aIndex >= aCount)
                return Undefined;
            return calls().extraArguments[static_cast<std::size_t>(aBase + aIndex - static_cast<std::int32_t>(RegisterArguments))];
        }

        namespace
        {
            value_list argument_list(std::int32_t aCount, value a0, value a1, value a2, value a3, std::int32_t aBase)
            {
                value_list result;
                result.reserve(static_cast<std::size_t>(std::max(aCount, 0)));
                value const registers[] = { a0, a1, a2, a3 };
                for (std::int32_t i = 0; i < aCount; ++i)
                    result.push_back(i < 4 ? registers[i] : calls().extraArguments[static_cast<std::size_t>(aBase + i - 4)]);
                return result;
            }
        }

        namespace
        {
            // (a built-in (native) function of the realm that is running: called directly (its realm need not be made the running one))
            function_object* direct_native(value aCallee)
            {
                if (!is_object(aCallee))
                    return nullptr;
                auto& o = obj(aCallee);
                if (o.cls != object_class::Function)
                    return nullptr;
                auto& f = static_cast<function_object&>(o);
                if (f.kind != function_kind::Native || f.functionRealm != &the_realm())
                    return nullptr;
                return &f;
            }
        }

        value call(value aCallee, value aThis, std::int32_t aCount, value a0, value a1, value a2, value a3)
        {
            if (aCount <= 4)
                if (auto* const f = direct_native(aCallee))
                    return guard([&]() -> value
                    {
                        value const registers[] = { a0, a1, a2, a3 };
                        return f->native(aThis, arguments{ registers, static_cast<std::size_t>(std::max(aCount, 0)) }, Undefined, *f);
                    });
            return guard([&]() -> value
            {
                if (aCount <= 4)
                {
                    // (the arguments: on the native stack (which the collector scans))
                    value const registers[] = { a0, a1, a2, a3 };
                    return javascript_runtime::call(aCallee, aThis, arguments{ registers, static_cast<std::size_t>(std::max(aCount, 0)) });
                }
                auto const base = static_cast<std::int32_t>(calls().extraArguments.size()) - std::max(0, aCount - 4);
                auto const list = argument_list(aCount, a0, a1, a2, a3, base);
                return javascript_runtime::call(aCallee, aThis, list);
            });
        }

        value tail_call(value aCallee, value aThis, std::int32_t aCount, value a0, value a1, value a2, value a3)
        {
            // (a call in tail position of a built-in: made now (nested), while there are few such calls running (the stack grows only by 
            // their frames: as compiled code makes the program's tail calls as calls (see TailCallDepth)); else pending (the function 
            // returns, then its caller makes the call))
            if (auto const* f = is_object(aCallee) ? as_function(aCallee) : nullptr; f != nullptr && f->kind == function_kind::Native &&
                calls().nativeTailCalls < NativeTailCalls)
            {
                ++calls().nativeTailCalls;
                auto const result = call(aCallee, aThis, aCount, a0, a1, a2, a3);
                --calls().nativeTailCalls;
                return result;
            }
            auto const base = static_cast<std::int32_t>(calls().extraArguments.size()) - std::max(0, aCount - 4);
            calls().tail.arguments = argument_list(aCount, a0, a1, a2, a3, base);
            calls().tail.callee = aCallee;
            calls().tail.thisValue = aThis;
            calls().tail.pending = true;
            return Exception;
        }

        value tail_call_spread(value aCallee, value aThis, value aArguments)
        {
            auto const& elements = as_array(aArguments)->elements;
            calls().tail.arguments.assign(elements.begin(), elements.end());
            calls().tail.callee = aCallee;
            calls().tail.thisValue = aThis;
            calls().tail.pending = true;
            return Exception;
        }

        value tail_resolve()
        {
            // (a compiled call site's call returned Exception: the callee's pending tail call is made (else the exception propagates))
            if (!calls().tail.pending)
                return Exception;
            return guard([&]() -> value { return run_tail_calls(); });
        }

        value call_spread(value aCallee, value aThis, value aArguments)
        {
            return guard([&]() -> value
            {
                auto const& elements = as_array(aArguments)->elements;
                return javascript_runtime::call(aCallee, aThis, value_list{ elements.begin(), elements.end() });
            });
        }

        value construct(value aCallee, value aArguments)
        {
            return guard([&]() -> value
            {
                auto const& elements = as_array(aArguments)->elements;
                value_list list{ elements.begin(), elements.end() };
                for (auto& v : list)
                    if (v == Empty)
                        v = Undefined;
                return javascript_runtime::construct(aCallee, list);
            });
        }

        std::int32_t construct_number(value aCallee)
        {
            // (new: a constructor that is the program's own compiled function (of the current realm): its number for the dispatcher (the
            // compiled code calls it as function_object::construct does, with construct_this's this); else -1 (the runtime constructs))
            auto* f = as_function(aCallee);
            if (f == nullptr || f->kind != function_kind::Compiled || !f->isConstructor || f->program != current_program() ||
                f->functionRealm != &the_realm())
                return -1;
            return static_cast<std::int32_t>(f->nativeId);
        }

        value make_this(value aCallee);

        value construct_this(value aCallee)
        {
            // (the this of a construct_number constructor (as function_object::construct makes it): a derived class's is Empty (its
            // constructor binds it))
            return guard([&]() -> value
            {
                return make_this(aCallee);
            });
        }

        value make_this(value aCallee)
        {
            {
                auto& f = *as_function(aCallee);
                if (f.derived)
                    return Empty;
                // (its properties' storage: as much as the object it made last has (allocated once))
                if (is_object(f.lastConstructed))
                    f.constructedProperties = std::max<std::uint32_t>(f.constructedProperties, static_cast<std::uint32_t>(obj(f.lastConstructed).properties.size()));
                // (OrdinaryCreateFromConstructor: its prototype (its own data property: found in its table))
                value prototype;
                if (f.prototypeShape != nullptr && f.properties.layout() == f.prototypeShape)
                    prototype = f.properties.value_at(f.prototypePosition);
                else if (auto const p = f.properties.find(the_names().prototype); p != nullptr && !p->accessor())
                {
                    prototype = p->data();
                    f.prototypeShape = f.properties.layout();
                    f.prototypePosition = p->position();
                }
                else
                    prototype = javascript_runtime::get(aCallee, the_names().prototype);
                if (!is_object(prototype))
                    prototype = function_realm(aCallee).objectPrototype;
                auto const thisValue = value_of(the_heap().make_with<javascript_runtime::object>(std::min<std::uint32_t>(f.constructedProperties, MaxCellValues), prototype));
                f.lastConstructed = thisValue;
                if (f.flavour == function_flavour::ClassConstructor)
                    initialize_instance_elements(thisValue, aCallee);
                return thisValue;
            }
        }

        value char_code_at(value aCallee, value aThis, value aPosition)
        {
            // (s.charCodeAt(i): the built-in's result if the callee is it; else the callee is called)
            value result;
            if (javascript_runtime::char_code_at(aCallee, aThis, aPosition, result))
                return result;
            return guard([&]() -> value
            {
                value const argument[] = { aPosition };
                return javascript_runtime::call(aCallee, aThis, argument);
            });
        }

        value slice_direct(value aCallee, value aThis, value aStart, value aEnd)
        {
            // (s.slice(start, end) whose callee is the realm's %String.prototype.slice% (a direct built-in: see direct_builtin_function))
            return guard([&]() -> value { return string_slice_direct(aCallee, aThis, aStart, aEnd); });
        }

        value test_direct(value aCallee, value aThis, value aString)
        {
            // (r.test(s) whose callee is the realm's %RegExp.prototype.test%)
            return guard([&]() -> value { return regexp_test_direct(aCallee, aThis, aString); });
        }

        // (a direct built-in that the runtime has no function of its own for: called (the callee: the realm's built-in, a native))
        template <typename... Arguments>
        value native_direct(value aCallee, value aThis, Arguments... aArguments)
        {
            return guard([&]() -> value
            {
                auto& f = *as_function(aCallee);
                value const parameters[] = { aArguments..., Undefined };
                return f.native(aThis, arguments{ parameters, sizeof...(Arguments) }, Undefined, f);
            });
        }
        value native_direct0(value aCallee, value aThis) { return native_direct(aCallee, aThis); }
        value native_direct1(value aCallee, value aThis, value a0) { return native_direct(aCallee, aThis, a0); }
        value native_direct2(value aCallee, value aThis, value a0, value a1) { return native_direct(aCallee, aThis, a0, a1); }
        value native_direct3(value aCallee, value aThis, value a0, value a1, value a2) { return native_direct(aCallee, aThis, a0, a1, a2); }

        value direct_builtin_function(std::int32_t aBuiltin)
        {
            // (the realm's direct built-ins (by number: the compiler's function_generator::DirectBuiltins'): compiled code's method calls
            // whose callees are them call the runtime's functions (their work) directly)
            auto const& r = the_realm();
            switch (aBuiltin)
            {
            case 0:
                return r.stringSlice;
            case 1:
                return r.regexpTest;
            case 2:
                return r.arrayPop;
            case 3:
                return r.stringIndexOf;
            case 4:
                return r.hasOwnProperty;
            case 5:
                return r.regexpExec;
            case 6:
                return r.objectAssign;
            default:
                return Undefined;
            }
        }

        value char_code_at_function()
        {
            // (the realm's %String.prototype.charCodeAt%: compiled code's s.charCodeAt(i) is the built-in's if the callee is it)
            return the_realm().charCodeAt;
        }

        value array_push_function()
        {
            // (the realm's %Array.prototype.push%: compiled code's a.push(x) is its own if the callee is it (see the push stub))
            return the_realm().arrayPush;
        }

        value array_prototype_value()
        {
            // (the realm's %Array.prototype% (the push stub's arrays' prototype))
            return the_realm().arrayPrototype;
        }

        value array_push_one(value aCallee, value aThis, value aValue)
        {
            // (a.push(x) that the push stub does not do itself: the callee called)
            return guard([&]() -> value
            {
                value const argument[] = { aValue };
                return javascript_runtime::call(aCallee, aThis, argument);
            });
        }

        value function_call_function()
        {
            // (the realm's %Function.prototype.call%: compiled code's f.call(t, ...) calls f itself if the callee is it)
            return the_realm().functionCall;
        }

        value char_code_at_tail(value aCallee, value aThis, value aPosition)
        {
            // (char_code_at in a tail position: a callee that is not the built-in is a tail call)
            value result;
            if (javascript_runtime::char_code_at(aCallee, aThis, aPosition, result))
                return result;
            calls().tail.arguments.assign(1u, aPosition);
            calls().tail.callee = aCallee;
            calls().tail.thisValue = aThis;
            calls().tail.pending = true;
            return Exception;
        }

        value sloppy_this(value aThis)
        {
            if (is_nullish(aThis))
                return the_realm().global;
            if (!is_object(aThis))
                return to_object(aThis);
            return aThis;
        }

        value arguments_object(value aCallee, std::int32_t aCount, value a0, value a1, value a2, value a3, std::int32_t aBase, std::int32_t aMapped)
        {
            return guard([&]() -> value
            {
                auto const list = argument_list(aCount, a0, a1, a2, a3, aBase);
                return make_arguments_object(aCallee, list, aMapped != 0);
            });
        }

        value arguments_map(value aArguments, value aEnvironment, std::int32_t aIndex, std::int32_t aSlot)
        {
            std::uint32_t const slot = static_cast<std::uint32_t>(aSlot);
            return map_arguments(aArguments, aEnvironment, static_cast<std::uint32_t>(aIndex), std::span<std::uint32_t const>{ &slot, 1u });
        }

        value rest(std::int32_t aCount, std::int32_t aStart, value a0, value a1, value a2, value a3, std::int32_t aBase)
        {
            return guard([&]() -> value
            {
                auto const list = argument_list(aCount, a0, a1, a2, a3, aBase);
                value_list elements;
                for (std::int32_t i = aStart; i < aCount; ++i)
                    elements.push_back(list[static_cast<std::size_t>(i)]);
                return make_array(std::move(elements));
            });
        }

        value derived_result(value aResult, value aThis)
        {
            return guard([&]() -> value
            {
                if (is_object(aResult))
                    return aResult;
                // (these errors are thrown once the callee's context is gone: they are its caller's realm's)
                realm_scope const callerRealm{ &caller_realm() };
                if (aResult != Undefined)
                    throw_type_error("Derived constructors may only return object or undefined");
                if (aThis == Empty)
                    throw_reference_error("Must call super constructor in derived class before accessing 'this' or returning from derived constructor");
                return aThis;
            });
        }

        // --- properties

        value get(value aObject, value aKey)
        {
            return guard([&]() -> value
            {
                if (auto* a = as_array(aObject); a != nullptr && is_number(aKey))
                {
                    auto const n = as_number(aKey);
                    if (n >= 0.0 && n < 4294967295.0)
                    {
                        auto const index = static_cast<std::uint32_t>(n);
                        value result;
                        if (static_cast<double>(index) == n && a->dense_get(index, result))
                            return result;
                    }
                }
                else if (auto* t = as_typed_array(aObject); t != nullptr && is_number(aKey))
                    return typed_array_get_number_key(*t, as_number(aKey));
                if (is_nullish(aObject))
                    throw_type_error("Cannot read properties of " + std::string{ aObject == Null ? "null" : "undefined" } + " (reading '" + describe_key(aKey) + "')");
                return get_v(aObject, to_property_key(aKey));
            });
        }

        value concat_string_number(value aString, value aNumber, std::int32_t aNumberFirst)
        {
            // (a string + a number (aNumberFirst: a number + a string))
            return guard([&]() -> value { return concat_number(aString, as_number(aNumber), aNumberFirst != 0); });
        }

        value get_named(value aObject, value aKey)
        {
            return guard([&]() -> value
            {
                if (is_object(aObject))
                    return obj(aObject).get(aKey, aObject);
                if (is_nullish(aObject))
                    throw_type_error("Cannot read properties of " + std::string{ aObject == Null ? "null" : "undefined" } + " (reading '" + utf8(aKey) + "')");
                return get_v(aObject, aKey);
            });
        }

        // (the lookup (the cache did not match): remembered if the objects are ordinary (bar the typed array) and have shapes)
#if defined(_MSC_VER)
        __declspec(noinline)
#else
        [[gnu::noinline]]
#endif
        bool typed_array_length_lookup(typed_array_object const& aTypedArray, typed_array_length_cache& aCache, realm const& aRealm)
        {
            auto& cache = aCache;
            auto const& r = aRealm;
            cache.depth = 0u;
            object const* o = &aTypedArray;
            for (std::size_t d = 1u; d < typed_array_length_cache::MaxDepth; ++d)
            {
                if (o->proto == Null)
                    return false;
                o = &obj(o->proto);
                if (!o->ordinaryGet || o->properties.layout() == nullptr)
                    return false;
                if (auto const p = o->properties.find(the_names().length))
                {
                    if (!p.accessor() || p.data() != r.typedArrayLength || r.typedArrayLength == Undefined)
                        return false;
                    cache.shapes[d] = o->properties.layout();
                    cache.objects[d] = o;
                    cache.depth = d + 1u;
                    cache.position = p.position();
                    cache.accessor = o->properties.value_at(p.position());
                    return true;
                }
                cache.shapes[d] = o->properties.layout();
                cache.objects[d] = o;
            }
            return false;
        }

        inline bool typed_array_length_is_getter(typed_array_object const& aTypedArray)
        {
            // (the typed array has no length of its own: it has no properties (it has no shape: its [[Get]] is not the ordinary one))
            if (!aTypedArray.properties.none() && aTypedArray.properties.find(the_names().length))
                return false;
            auto& r = the_realm();
            auto& cache = r.typedArrayLengthCache;
            // (the cache's objects and shapes, to the getter's)
            if (cache.depth >= 2u)
            {
                object const* o = &aTypedArray;
                bool same = true;
                for (std::size_t d = 1u; d < cache.depth; ++d)
                {
                    if (o->proto == Null)
                    {
                        same = false;
                        break;
                    }
                    o = &obj(o->proto);
                    if (o != cache.objects[d] || o->properties.layout() != cache.shapes[d] || cache.shapes[d] == nullptr)
                    {
                        same = false;
                        break;
                    }
                }
                if (same)
                {
                    auto const accessor = o->properties.value_at(cache.position);
                    if (accessor == cache.accessor && cell_of<accessor_pair>(accessor).getter == r.typedArrayLength)
                        return true;
                }
            }
            return typed_array_length_lookup(aTypedArray, cache, r);
        }

        value get_length_site(value aObject, std::int32_t aSite);

        value get_length(value aObject)
        {
            // (x.length: a string's or a (non-sparse) array's length directly; a typed array's (if it is its prototypes' getter's); else 
            // as get_named gets it)
            if (is_string(aObject))
                return number(static_cast<double>(string_length(aObject)));
            if (auto const* a = as_array(aObject); a != nullptr && !a->sparse)
                return number(static_cast<double>(a->length));
            if (auto const* t = as_typed_array(aObject); t != nullptr && typed_array_length_is_getter(*t))
                return number(static_cast<double>(t->length()));
            return get_named(aObject, the_names().length);
        }

        value set(value aObject, value aKey, value aValue, std::int32_t aStrict)
        {
            return guard([&]() -> value
            {
                if (auto* a = as_array(aObject); a != nullptr && is_number(aKey) && !a->sparse)
                {
                    auto const n = as_number(aKey);
                    if (n >= 0.0 && n < 4294967295.0)
                    {
                        auto const index = static_cast<std::uint32_t>(n);
                        if (static_cast<double>(index) == n && index < a->elements.size() && a->elements[index] != Empty)
                        {
                            a->elements[index] = aValue;
                            return aValue;
                        }
                    }
                }
                else if (auto* t = as_typed_array(aObject); t != nullptr && is_number(aKey))
                {
                    typed_array_set_number_key(*t, as_number(aKey), aValue);
                    return aValue;
                }
                if (is_nullish(aObject))
                    throw_type_error("Cannot set properties of " + std::string{ aObject == Null ? "null" : "undefined" } + " (setting '" + describe_key(aKey) + "')");
                put_value(aObject, to_property_key(aKey), aValue, aStrict != 0);
                return aValue;
            });
        }

        value set_named(value aObject, value aKey, value aValue, std::int32_t aStrict)
        {
            return guard([&]() -> value
            {
                if (is_nullish(aObject))
                    throw_type_error("Cannot set properties of " + std::string{ aObject == Null ? "null" : "undefined" } + " (setting '" + utf8(aKey) + "')");
                put_value(aObject, aKey, aValue, aStrict != 0);
                return aValue;
            });
        }

        // --- property caches: a compiled get or set of a named property (a site: the compiler numbers a program's from 0) remembers
        // where its last lookup found the property, by the shapes of the objects (see shape (js.hpp)): the receiver's own property at
        // a position, a prototype's (the prototype, and the shapes of those that do not have it) or (a set) the property that the
        // receiver adds; and (a get) the value's number for the dispatcher if it is a function of the program's. A program's sites are
        // in a table in the region (property_sites makes it; its offset is in a global of the program's) that compiled code reads
        // (see neos::language::javascript::layout): it uses an entry that fits (the receiver's shape and the key), and calls these
        // functions if none does. What an entry remembers is checked when it is used, so it cannot give a wrong result; the values that
        // it refers to (the prototypes, the function) are kept alive while it does

        namespace
        {
            namespace jsl = neos::language::javascript::layout;

            struct site
            {
                value key;
                std::uint64_t shape;            ///< (the receiver's (Add: before it adds the property); a shape const*)
                std::uint8_t kind;              ///< (jsl::SiteOwn ...)
                std::uint8_t depth;             ///< (the prototypes that are checked)
                std::uint16_t reserved;
                std::uint32_t position;         ///< (the property's position in its holder's table)
                struct link
                {
                    value object;
                    std::uint64_t shape;
                } prototypes[2];
                std::uint64_t added;            ///< (Add: the receiver's shape with the property)
                value function;                 ///< (a get's value, if it is a function of the program's (to call)...)
                std::int32_t number;            ///< (... whose number this is (for the dispatcher); else -1)
                std::uint32_t reserved2;
                std::uint64_t dispatch;         ///< (... and its dispatch key (see jsl::FunctionDispatch); else 0)
            };
            static_assert(std::is_standard_layout_v<site> && sizeof(site) == jsl::SiteEntrySize);
            static_assert(offsetof(site, key) == jsl::SiteKey && offsetof(site, shape) == jsl::SiteShape);
            static_assert(offsetof(site, kind) == jsl::SiteKind && offsetof(site, depth) == jsl::SiteDepth);
            static_assert(offsetof(site, position) == jsl::SitePosition && offsetof(site, prototypes) == jsl::SitePrototype);
            static_assert(offsetof(site, added) == jsl::SiteAdded && offsetof(site, function) == jsl::SiteFunction);
            static_assert(offsetof(site, number) == jsl::SiteNumber && offsetof(site, dispatch) == jsl::SiteDispatch);

            inline std::uint64_t shape_word(shape const* aShape) { return reinterpret_cast<std::uintptr_t>(aShape); }

            // (the programs' tables)
            struct site_tables
            {
                std::unordered_map<void*, region_vector<site>> tables;
                void* lastProgram = nullptr;
                region_vector<site>* lastTable = nullptr;
            };
            site_tables& the_site_tables()
            {
                static isolate_local<site_tables> sTables;
                return *sTables;
            }
            root_tracer const sSiteRoots{ [](tracer& aTracer)
            {
                for (auto const& [program, table] : the_site_tables().tables)
                    for (auto const& s : table)
                    {
                        if (s.kind != jsl::SiteNone)
                            for (auto const& p : s.prototypes)
                                if (p.object != 0u)
                                    aTracer.mark(p.object);
                        if (s.function != 0u)
                            aTracer.mark(s.function);
                    }
            } };

            // (the running program's site (null: none (its table is not made yet)))
            site* site_of(std::int32_t aSite)
            {
                auto& t = the_site_tables();
                auto* const program = current_program();
                if (program != t.lastProgram || t.lastTable == nullptr)
                {
                    auto const existing = t.tables.find(program);
                    if (existing == t.tables.end())
                        return nullptr;
                    t.lastProgram = program;
                    t.lastTable = &existing->second;
                }
                auto const index = static_cast<std::size_t>(static_cast<std::uint32_t>(aSite)) * jsl::SiteEntries;
                return index < t.lastTable->size() ? &(*t.lastTable)[index] : nullptr;
            }

            // (an entry remembered: it replaces the site's entry for the same receiver's shape and key, else it is the first (the
            // others move down: the last is forgotten))
            void remember(std::int32_t aSite, site const& aEntry)
            {
                auto* const entries = site_of(aSite);
                if (entries == nullptr)
                    return;
                // (a megamorphic site: as it is; its first entry's misses (see get_named_cached): kept)
                if (entries[0].kind == jsl::SiteMegamorphic)
                    return;
                auto const misses = entries[0].reserved;
                // (a call's site (no key): its entries are by function)
                bool const call = aEntry.key == 0u;
                std::size_t i = 0u;
                while (i + 1u < jsl::SiteEntries && !(call ? entries[i].function == aEntry.function : 
                    (entries[i].shape == aEntry.shape && entries[i].key == aEntry.key)))
                    ++i;
                for (; i > 0u; --i)
                    entries[i] = entries[i - 1u];
                entries[0] = aEntry;
                entries[0].reserved = misses;
            }

            // (the receiver's prototypes are the site's (the objects (their types' gets ordinary), of the same shapes); a get's holder
            // (the last) has the property: if it has a shape, its shape is compared, else (a dictionary, e.g. a prototype of many 
            // methods) its property at the position is compared (see holder_value))
            bool prototypes_match(object const& aReceiver, site const& aSite, bool aHolder)
            {
                auto p = aReceiver.proto;
                for (std::uint8_t i = 0u; i < aSite.depth; ++i)
                {
                    if (p != aSite.prototypes[i].object)
                        return false;
                    auto const& o = obj(p);
                    if (!o.ordinaryGet && !array_type(o))
                        return false;
                    if (!(aHolder && i + 1u == aSite.depth) && shape_word(o.properties.layout()) != aSite.prototypes[i].shape)
                        return false;
                    p = o.proto;
                }
                return true;
            }

            // (an entry's holder's property's value: a holder with a shape (aShape): the value at the position, if the holder's shape 
            // is that; else (a dictionary) the key's data property at the position, if it is still there)
            bool holder_value(object const& aHolder, std::uint64_t aShape, std::uint32_t aPosition, value aKey, value& aResult)
            {
                return aHolder.properties.cached_value(aShape, aPosition, aKey, aResult);
            }

            // (a function's dispatch key for a site (see jsl::SiteDispatch): 0 if it has none (e.g. a class constructor))
            std::uint64_t site_dispatch(value aFunction)
            {
                auto const* f = as_function(aFunction);
                return f != nullptr && f->dispatch != ~std::uint64_t{} ? f->dispatch : 0u;
            }

            // (a get's holder (the receiver's prototype, or %String.prototype%): watched (see watch); the entry has its validity word (its 
            // offset in the region, and its value): compiled code checks it rather than the holder's shape, then knows its properties)
            void watch_holder(site& aEntry, object& aHolder)
            {
                if (auto const* word = watch(aHolder))
                {
                    aEntry.added = region_offset(word);
                    aEntry.prototypes[1].shape = *word;
                }
            }

            // (a get's site: where the lookup found the property (if it can be remembered) and the function that it found)
            void remember_get(std::int32_t aSite, value aObject, value aKey, value aResult)
            {
                site e{};
                e.key = aKey;
                e.number = -1;
                [&]()
                {
                    if (is_object(aObject))
                    {
                        // (an object whose get is the ordinary one: an ordinary object, or an array (a key that is not its length
                        // or an index))
                        bool const arrayKey = aKey == the_names().length || array_index(aKey).has_value();
                        auto const cacheable = [&](object const& aCandidate)
                        {
                            return aCandidate.ordinaryGet || (array_type(aCandidate) && !arrayKey);
                        };
                        auto& o = obj(aObject);
                        if (!cacheable(o))
                            return;
                        auto const* layout = o.properties.layout();
                        if (layout == nullptr)
                        {
                            // (no shape (e.g. an object of many properties): its own property, if it has it (the object itself is
                            // the entry's))
                            if (o.mirrored)
                                return;
                            if (auto const p = o.properties.find(aKey); p && !p->accessor())
                            {
                                e.kind = jsl::SiteSelf;
                                e.prototypes[0] = { aObject, 0u };
                                e.position = p.position();
                            }
                            return;
                        }
                        e.shape = shape_word(layout);
                        if (auto const p = o.properties.find(aKey))
                        {
                            if (p->accessor())
                                return;
                            e.kind = jsl::SiteOwn;
                            e.position = p.position();
                            // (and the megamorphic cache's (for sites whose entries do not have it); a watched object's property is not
                            // one that a set can store (its sets are the runtime's: see watch))
                            auto& state = the_heap().the_allocation_state();
                            auto* const table = reinterpret_cast<megamorphic_entry*>(gRegionBase + state.megamorphic);
                            table[megamorphic_index(e.shape, aKey)] = megamorphic_entry{ e.shape, aKey, e.position, p->writable() && layout->validity == nullptr ? 1u : 0u, 0u };
                            return;
                        }
                        auto prototype = o.proto;
                        for (std::uint8_t depth = 0u; depth < 2u && prototype != Null; ++depth)
                        {
                            auto& holder = obj(prototype);
                            if (!cacheable(holder))
                                return;
                            auto const p = holder.properties.find(aKey);
                            if (p && !p->accessor() && depth == 0u)
                                watch_holder(e, holder);
                            auto const* holderLayout = holder.properties.layout();
                            e.prototypes[depth] = { prototype, shape_word(holderLayout) };
                            if (!p && holderLayout == nullptr)
                                return;     // (a prototype that does not have it is compared by its shape)
                            if (p)
                            {
                                if (p->accessor())
                                    return;
                                e.kind = jsl::SiteHolder;
                                e.depth = depth + 1u;
                                e.position = p.position();
                                return;
                            }
                            prototype = holder.proto;
                        }
                    }
                    else if (is_string(aObject) && aKey != the_names().length && !array_index(aKey))
                    {
                        // (a string's property (not its length or an index: a named property): %String.prototype%'s (the realm's:
                        // a program's code runs in the realm that it was evaluated in, so it is its sites' (compiled code uses the
                        // entry's)))
                        auto const prototype = the_realm().stringPrototype;
                        auto& holder = obj(prototype);
                        if (auto const p = holder.properties.find(aKey); p && !p->accessor())
                        {
                            watch_holder(e, holder);
                            e.kind = jsl::SiteString;
                            e.prototypes[0] = { prototype, shape_word(holder.properties.layout()) };
                            e.position = p.position();
                        }
                    }
                }();
                if (e.kind == jsl::SiteNone)
                {
                    e.shape = 0u;
                    e.prototypes[0] = {};
                    e.prototypes[1] = {};
                }
                // (a method: called through the dispatcher (as callee_number finds))
                if (auto const number = callee_number(aResult); number >= 0)
                {
                    e.function = aResult;
                    e.number = number;
                    e.dispatch = site_dispatch(aResult);
                }
                else if (e.kind != jsl::SiteNone && as_function(aResult) != nullptr)
                {
                    // (a function that is not the program's (e.g. a built-in): its number is -1 (the method's call is the runtime's,
                    // without asking it for the number))
                    e.function = aResult;
                    e.number = -1;
                }
                if (e.kind != jsl::SiteNone || e.function != 0u)
                    remember(aSite, e);
            }

            // (a site's entry that fits a get (the receiver's shape and the key): its property's value)
            bool cached_get(site const* aEntries, value aObject, value aKey, value& aResult)
            {
                if (aEntries == nullptr)
                    return false;
                for (std::size_t i = 0u; i < jsl::SiteEntries; ++i)
                {
                    auto const& e = aEntries[i];
                    if (e.key != aKey || e.kind == jsl::SiteNone)
                        continue;
                    if (is_object(aObject))
                    {
                        auto& o = obj(aObject);
                        if (e.kind == jsl::SiteSelf)
                        {
                            if (aObject == e.prototypes[0].object && holder_value(o, 0u, e.position, aKey, aResult))
                                return true;
                            continue;
                        }
                        if (shape_word(o.properties.layout()) != e.shape || e.shape == 0u)
                            continue;
                        if (e.kind == jsl::SiteOwn && o.properties.cached_value(e.shape, e.position, aKey, aResult))
                            return true;
                        if (e.kind == jsl::SiteHolder && prototypes_match(o, e, true) &&
                            holder_value(obj(e.prototypes[e.depth - 1u].object), e.prototypes[e.depth - 1u].shape, e.position, aKey, aResult))
                            return true;
                    }
                    else if (is_string(aObject) && e.kind == jsl::SiteString && e.prototypes[0].object == the_realm().stringPrototype)
                    {
                        if (holder_value(obj(e.prototypes[0].object), e.prototypes[0].shape, e.position, aKey, aResult))
                            return true;
                    }
                }
                return false;
            }
        }

        std::int32_t callee_number_cached(value aCallee, std::int32_t aSite)
        {
            // (callee_number, remembered in a call's site (its entries have only functions and their numbers))
            auto const number = callee_number(aCallee);
            if (number >= 0)
            {
                site e{};
                e.function = aCallee;
                e.number = number;
                e.dispatch = site_dispatch(aCallee);
                remember(aSite, e);
            }
            return number;
        }

        value construct_this_cached(value aCallee, std::int32_t aSite)
        {
            // (construct_this; a new expression's site's entry for the constructor (see construct_number_cached) then has what compiled 
            // code needs to make its objects itself (SiteConstruct), if they are ordinary objects made as it makes them (it has made 
            // one before: the storage that its objects' properties are given is known) whose prototype is its prototype property, a data
            // property (in its table of a shape))
            return guard([&]() -> value
            {
                auto& f = *as_function(aCallee);
                bool const before = is_object(f.lastConstructed);
                auto const thisValue = make_this(aCallee);
                if (!before || f.derived || f.flavour == function_flavour::ClassConstructor || f.prototypeShape == nullptr ||
                    f.properties.layout() != f.prototypeShape || !is_object(f.properties.at(f.prototypePosition).data()))
                    return thisValue;
                auto* const entries = site_of(aSite);
                if (entries == nullptr)
                    return thisValue;
                for (std::size_t i = 0u; i < jsl::SiteEntries; ++i)
                {
                    auto& e = entries[i];
                    if (e.function != aCallee || e.key != 0u)
                        continue;
                    auto& h = the_heap();
                    constexpr std::size_t base = (sizeof(object) + 7u) & ~std::size_t{ 7u };
                    auto const size = base + std::min<std::size_t>(f.constructedProperties, MaxCellValues) * sizeof(value);
                    auto const sizeClass = h.size_class(size);
                    auto const slotSize = h.class_size(sizeClass);
                    e.kind = jsl::SiteConstruct;
                    e.depth = static_cast<std::uint8_t>(sizeClass);
                    e.shape = shape_word(f.prototypeShape);
                    e.position = f.prototypePosition;
                    e.added = h.object_template(sizeClass, (slotSize - base) / sizeof(value));
                    e.reserved2 = slotSize;
                    break;
                }
                return thisValue;
            });
        }

        value array_literal(std::int32_t aSite, std::int32_t aCount)
        {
            // (an array literal's array: aCount elements (undefined: compiled code sets them), in its cell (room for as many, at least 4);
            // its site's entry then has what compiled code needs to make its arrays itself (SiteArray))
            return guard([&]() -> value
            {
                auto& h = the_heap();
                constexpr std::size_t base = (sizeof(array_object) + 7u) & ~std::size_t{ 7u };
                auto const count = static_cast<std::size_t>(std::clamp<std::int32_t>(aCount, 0, static_cast<std::int32_t>(jsl::MaxLiteralElements)));
                auto const room = std::max<std::size_t>(count, 4u);
                auto& a = h.make_with<array_object>(room, the_realm().arrayPrototype);
                a.properties.forget_storage();
                auto* const cell = reinterpret_cast<std::byte*>(&a);
                auto const sizeClass = h.size_class(base + room * sizeof(value));
                auto const slotSize = h.class_size(sizeClass);
                auto const capacity = (slotSize - base) / sizeof(value);
                a.elements.use_storage(reinterpret_cast<value*>(cell + base), capacity);
                for (std::size_t i = 0u; i < count; ++i)
                    a.elements.push_back(Undefined);
                a.length = static_cast<std::uint32_t>(count);
                if (auto* const entries = site_of(aSite); entries != nullptr && entries[0].kind == jsl::SiteNone)
                {
                    auto& e = entries[0];
                    e.kind = jsl::SiteArray;
                    e.depth = static_cast<std::uint8_t>(sizeClass);
                    e.added = h.array_template(sizeClass, capacity);
                    e.reserved2 = slotSize;
                    e.prototypes[0].object = the_realm().arrayPrototype;
                }
                return value_of(a);
            });
        }

        value object_literal_remember(value aObject, std::int32_t aSite, std::int32_t aCount)
        {
            // (an object literal's object, made as the runtime makes it (its properties data properties, in its cell): its site's entry
            // then has what compiled code needs to make its objects itself (SiteLiteral))
            if (!is_object(aObject))
                return Undefined;
            auto& o = obj(aObject);
            auto* const s = o.properties.layout();
            constexpr std::size_t base = (sizeof(object) + 7u) & ~std::size_t{ 7u };
            if (&typeid(o) != &typeid(object) || s == nullptr || o.properties.size() != static_cast<std::size_t>(aCount) || o.has_private_elements() ||
                o.proto != the_realm().objectPrototype || !o.extensible)
                return Undefined;
            for (auto const& slot : s->slots())
                if (slot.attributes != AttributesDefault)
                    return Undefined;
            auto* const cell = reinterpret_cast<std::byte*>(&o);
            if (*static_cast<std::uint64_t const*>(o.properties.values_field()) != region_offset(cell + base))
                return Undefined;   // (its values are not in its cell)
            auto& h = the_heap();
            auto const sizeClass = h.size_class(base + static_cast<std::size_t>(aCount) * sizeof(value));
            auto const slotSize = h.class_size(sizeClass);
            if (h.slot_size_of(cell) != slotSize)
                return Undefined;
            auto* const entries = site_of(aSite);
            if (entries == nullptr || entries[0].kind != jsl::SiteNone)
                return Undefined;
            auto& e = entries[0];
            e.kind = jsl::SiteLiteral;
            e.depth = static_cast<std::uint8_t>(sizeClass);
            e.shape = shape_word(s);
            e.added = h.object_template(sizeClass, (slotSize - base) / sizeof(value));
            e.reserved2 = slotSize;
            e.prototypes[0].object = o.proto;
            return Undefined;
        }

        // (a cell's memory for compiled code, when it cannot take it itself (see heap::allocate_cell): its region offset)
        std::uint64_t allocate_slot(std::uint32_t aSize)
        {
            try
            {
                return region_offset(the_heap().allocate_cell(aSize));
            }
            catch (...)
            {
                std::fprintf(stderr, "neos: out of memory\n");
                std::abort();
            }
        }

        value allocation_state_offset()
        {
            // (compiled code's allocation state (see allocation_state): its region offset)
            return static_cast<value>(region_offset(&the_heap().the_allocation_state()));
        }

        std::int32_t construct_number_cached(value aCallee, std::int32_t aSite)
        {
            // (construct_number, remembered in a new expression's site (its entries have only functions and their numbers))
            auto const number = construct_number(aCallee);
            if (number >= 0)
            {
                site e{};
                e.function = aCallee;
                e.number = number;
                e.dispatch = site_dispatch(aCallee);
                remember(aSite, e);
            }
            return number;
        }

        value property_sites(std::int32_t aCount, std::int32_t aGlobal)
        {
            // (the program's sites (see compiled code's property caches): its table (empty entries), whose offset is its global aGlobal)
            return guard([&]() -> value
            {
                auto& t = the_site_tables();
                auto& table = t.tables[current_program()];
                table.clear();
                table.resize(static_cast<std::size_t>(std::max(aCount, 1)) * jsl::SiteEntries, site{ 0u, 0u, jsl::SiteNone, 0u, 0u, 0u, {}, 0u, 0u, -1, 0u });
                t.lastProgram = nullptr;
                t.lastTable = nullptr;
                *program_word(aGlobal) = region_offset(table.data());
                fix_program_word(aGlobal);
                publish_heap();
                return Undefined;
            });
        }

        value get_length_site(value aObject, std::int32_t aSite)
        {
            // (x.length where compiled code's path did not have it (see the length stub): an array's (not sparse) marks its site (its
            // first entry's kind: SiteArray), so that its code then has the array's path)
            if (auto const* a = as_array(aObject); a != nullptr && !a->sparse)
                if (auto* const entries = site_of(aSite); entries != nullptr && entries[0].kind == jsl::SiteNone)
                    entries[0].kind = jsl::SiteArray;
            return get_length(aObject);
        }

        // (the megamorphic cache's: an own data property of an object of a shape (see megamorphic_entry)); the site remembers it)
        bool megamorphic_get(std::int32_t aSite, value aObject, value aKey, value& aResult)
        {
            if (!is_object(aObject))
                return false;
            auto const& o = obj(aObject);
            auto const* layout = o.properties.layout();
            if (layout == nullptr)
                return false;
            auto const shape = shape_word(layout);
            auto const& state = the_heap().the_allocation_state();
            auto const& e = reinterpret_cast<megamorphic_entry const*>(gRegionBase + state.megamorphic)[megamorphic_index(shape, aKey)];
            if (e.shape != shape || e.key != aKey || !o.properties.cached_value(shape, e.position, aKey, aResult))
                return false;
            site entry{};
            entry.key = aKey;
            entry.number = -1;
            entry.kind = jsl::SiteOwn;
            entry.shape = shape;
            entry.position = e.position;
            remember(aSite, entry);
            return true;
        }

        value get_named_cached(value aObject, value aKey, std::int32_t aSite)
        {
            value result;
            auto* const entries = site_of(aSite);
            if (cached_get(entries, aObject, aKey, result))
                return result;
            // (a site that misses often (its receivers have had more shapes than its entries): megamorphic (its gets look in the 
            // megamorphic cache))
            constexpr std::uint16_t MegamorphicMisses = 16u;
            if (entries != nullptr && entries[0].kind != jsl::SiteMegamorphic && entries[jsl::SiteEntries - 1u].kind != jsl::SiteNone &&
                ++entries[0].reserved >= MegamorphicMisses)
            {
                site e{};
                e.key = aKey;
                e.kind = jsl::SiteMegamorphic;
                e.number = -1;
                entries[0] = e;
                for (std::size_t i = 1u; i < jsl::SiteEntries; ++i)
                    entries[i] = site{ 0u, 0u, jsl::SiteNone, 0u, 0u, 0u, {}, 0u, 0u, -1, 0u, 0u };
            }
            if (megamorphic_get(aSite, aObject, aKey, result))
                return result;
            result = get_named(aObject, aKey);
            if (result != Exception)
                remember_get(aSite, aObject, aKey, result);
            return result;
        }

        value get_cached(value aObject, value aKey, std::int32_t aSite)
        {
            // (o[k]: as get_named_cached if the key is an interned string (a property key, as a site's entry has); else as get)
            value result;
            if (cached_get(site_of(aSite), aObject, aKey, result))
                return result;
            if (megamorphic_get(aSite, aObject, aKey, result))
                return result;
            result = get(aObject, aKey);
            if (result != Exception && is_string(aKey) && cell_of<javascript_runtime::string>(aKey).interned)
                remember_get(aSite, aObject, aKey, result);
            return result;
        }

        value set_named_cached(value aObject, value aKey, value aValue, std::int32_t aStrict, std::int32_t aSite)
        {
            shape const* before = nullptr;
            if (is_object(aObject))
            {
                auto& o = obj(aObject);
                auto const* layout = o.properties.layout();
                if (layout != nullptr && ordinary_type(o) && !o.mirrored)
                {
                    before = layout;
                    auto* const entries = site_of(aSite);
                    if (entries != nullptr && entries[0].kind == jsl::SiteMegamorphic)
                    {
                        // (a megamorphic site: a writable own data property in the megamorphic cache)
                        auto const shape = shape_word(layout);
                        auto const& state = the_heap().the_allocation_state();
                        auto const& m = reinterpret_cast<megamorphic_entry const*>(gRegionBase + state.megamorphic)[megamorphic_index(shape, aKey)];
                        if (m.shape == shape && m.key == aKey && m.reserved != 0u && o.properties.cached_store(shape, m.position, aValue))
                            return aValue;
                    }
                    else if (entries != nullptr)
                    {
                        // (a site whose entries are all used that misses often: megamorphic (as get_named_cached's))
                        bool hit = entries[jsl::SiteEntries - 1u].kind == jsl::SiteNone;
                        for (std::size_t i = 0u; i < jsl::SiteEntries; ++i)
                            if (entries[i].key == aKey && entries[i].shape == shape_word(layout))
                                hit = true;
                        constexpr std::uint16_t MegamorphicMisses = 16u;
                        if (!hit && ++entries[0].reserved >= MegamorphicMisses)
                        {
                            site e{};
                            e.key = aKey;
                            e.kind = jsl::SiteMegamorphic;
                            e.number = -1;
                            entries[0] = e;
                            for (std::size_t i = 1u; i < jsl::SiteEntries; ++i)
                                entries[i] = site{ 0u, 0u, jsl::SiteNone, 0u, 0u, 0u, {}, 0u, 0u, -1, 0u, 0u };
                        }
                    }
                    for (std::size_t i = 0u; entries != nullptr && i < jsl::SiteEntries; ++i)
                    {
                        auto const* e = &entries[i];
                        if (e->key != aKey || e->shape != shape_word(layout))
                            continue;
                        // (Own: a writable data property (its shape's) at the position)
                        if (e->kind == jsl::SiteOwn && o.properties.cached_store(e->shape, e->position, aValue))
                            return aValue;
                        if (e->kind == jsl::SiteAdd && o.extensible && prototypes_match(o, *e, false) &&
                            (e->depth == 0u ? o.proto : obj(e->prototypes[e->depth - 1u].object).proto) == Null)
                        {
                            o.properties.add(aValue, reinterpret_cast<shape*>(e->added));
                            return aValue;
                        }
                    }
                }
            }
            auto const result = set_named(aObject, aKey, aValue, aStrict);
            if (result == Exception || before == nullptr)
                return result;
            // (remembered: a writable data property that the receiver has, or the one that it added)
            auto& o = obj(aObject);
            auto const* after = o.properties.layout();
            // (a watched object's sets are the runtime's (see watch))
            if (before->validity != nullptr || (after != nullptr && after->validity != nullptr))
                return result;
            site e{};
            e.key = aKey;
            e.shape = shape_word(before);
            e.number = -1;
            if (after == before)
            {
                auto const p = o.properties.find(aKey);
                if (!p || (p->attributes() & (AttributeAccessor | AttributeWritable)) != AttributeWritable)
                    return result;
                e.kind = jsl::SiteOwn;
                e.position = p.position();
                // (and the megamorphic cache's (a writable data property))
                auto& state = the_heap().the_allocation_state();
                auto* const table = reinterpret_cast<megamorphic_entry*>(gRegionBase + state.megamorphic);
                table[megamorphic_index(e.shape, aKey)] = megamorphic_entry{ e.shape, aKey, e.position, 1u, 0u };
            }
            else
            {
                if (after == nullptr || after->parent != before || after->key != aKey || after->attributes != AttributesDefault)
                    return result;
                // (no prototype (each of them ordinary) has the property; the last's prototype is null)
                auto prototype = o.proto;
                std::uint8_t depth = 0u;
                for (; prototype != Null; ++depth)
                {
                    if (depth == 2u)
                        return result;
                    auto& p = obj(prototype);
                    auto const* layout = p.properties.layout();
                    if (layout == nullptr || !ordinary_type(p) || p.properties.find(aKey))
                        return result;
                    e.prototypes[depth] = { prototype, shape_word(layout) };
                    prototype = p.proto;
                }
                e.kind = jsl::SiteAdd;
                e.depth = depth;
                e.position = static_cast<std::uint32_t>(o.properties.size() - 1u);
                e.added = shape_word(after);
            }
            remember(aSite, e);
            return result;
        }

        value delete_(value aObject, value aKey, std::int32_t aStrict)
        {
            return guard([&]() -> value
            {
                auto const o = to_object(aObject);
                auto const key = to_property_key(aKey);
                bool const deleted = obj(o).delete_property(key);
                if (!deleted && aStrict != 0)
                    throw_type_error("Cannot delete property '" + describe_key(key) + "' of " + describe(aObject));
                return boolean(deleted);
            });
        }

        value super_base(value aHome)
        {
            // (GetSuperBase: the home object's prototype (when the super reference is made))
            return guard([&]() -> value
            {
                if (!is_object(aHome))
                    throw_syntax_error("'super' keyword unexpected here");
                return obj(aHome).get_prototype_of();
            });
        }

        value super_get(value aBase, value aKey, value aThis)
        {
            // (aBase: the reference's super base (see super_base))
            return guard([&]() -> value
            {
                auto const prototype = aBase;
                if (is_nullish(prototype))
                    throw_type_error("Cannot read properties of " + std::string{ prototype == Null ? "null" : "undefined" });
                return obj(prototype).get(to_property_key(aKey), aThis);
            });
        }

        value super_set(value aBase, value aKey, value aValue, value aThis)
        {
            return guard([&]() -> value
            {
                auto const prototype = aBase;
                if (is_nullish(prototype))
                    throw_type_error("Cannot set properties of " + std::string{ prototype == Null ? "null" : "undefined" });
                if (!obj(prototype).set(to_property_key(aKey), aValue, aThis))
                    throw_type_error("Cannot assign to read only property '" + describe_key(aKey) + "' of object");
                return aValue;
            });
        }

        value super_set_sloppy(value aBase, value aKey, value aValue, value aThis)
        {
            // (sloppy code (an object literal's method): a failed set is not an error)
            return guard([&]() -> value
            {
                auto const prototype = aBase;
                if (is_nullish(prototype))
                    throw_type_error("Cannot set properties of " + std::string{ prototype == Null ? "null" : "undefined" });
                obj(prototype).set(to_property_key(aKey), aValue, aThis);
                return aValue;
            });
        }

        value require_object_coercible(value aValue)
        {
            return guard([&]() -> value
            {
                if (is_nullish(aValue))
                    throw_type_error("Cannot destructure '" + describe(aValue) + "' as it is " + (aValue == Null ? "null." : "undefined."));
                return aValue;
            });
        }

        // --- conversions and operators

        value typeof_(value aValue)
        {
            return type_of(aValue);
        }

        std::int32_t to_boolean_(value aValue)
        {
            return javascript_runtime::to_boolean(aValue) ? 1 : 0;
        }

        value to_number_(value aValue)
        {
            return guard([&]() -> value { return number(javascript_runtime::to_number(aValue)); });
        }

        value to_numeric_(value aValue)
        {
            return guard([&]() -> value { return javascript_runtime::to_numeric(aValue); });
        }

        value to_string_(value aValue)
        {
            return guard([&]() -> value { return javascript_runtime::to_string(aValue); });
        }

        value to_object_(value aValue)
        {
            return guard([&]() -> value { return javascript_runtime::to_object(aValue); });
        }

        value to_property_key_(value aValue)
        {
            return guard([&]() -> value { return javascript_runtime::to_property_key(aValue); });
        }

        value concat(value aLeft, value aRight)
        {
            return guard([&]() -> value { return javascript_runtime::concat(aLeft, aRight); });
        }

        value binary(int aOperation, value aLeft, value aRight)
        {
            return guard([&]() -> value { return binary_operation(aOperation, aLeft, aRight); });
        }

        value unary(int aOperation, value aValue)
        {
            return guard([&]() -> value { return unary_operation(aOperation, aValue); });
        }

        std::int32_t strict_equal(value aLeft, value aRight)
        {
            return strictly_equal(aLeft, aRight) ? 1 : 0;
        }

        // --- literals

        value object()
        {
            return make_object();
        }

        value object_with(std::int32_t aCount)
        {
            // (an object literal's object: room in it for its properties' values)
            return guard([&]() -> value
            {
                return value_of(the_heap().make_with<javascript_runtime::object>(static_cast<std::size_t>(std::clamp<std::int32_t>(aCount, 0, static_cast<std::int32_t>(MaxCellValues))),
                    the_realm().objectPrototype));
            });
        }

        value object_define(value aObject, value aKey, value aValue)
        {
            return guard([&]() -> value
            {
                create_data_property_or_throw(aObject, to_property_key(aKey), aValue);
                return Undefined;
            });
        }

        value object_proto(value aObject, value aPrototype)
        {
            if (is_object(aPrototype) || aPrototype == Null)
                obj(aObject).set_prototype_of(aPrototype);
            return Undefined;
        }

        value object_spread(value aObject, value aSource)
        {
            return guard([&]() -> value
            {
                copy_data_properties(aObject, aSource);
                return Undefined;
            });
        }

        value object_rest(value aSource, value aExcluded)
        {
            return guard([&]() -> value
            {
                auto const result = make_object();
                auto const& excluded = as_array(aExcluded)->elements;
                copy_data_properties(result, aSource, excluded);
                return result;
            });
        }

        value define_method(value aObject, value aKey, value aFunction, std::int32_t aKind, std::int32_t aEnumerable)
        {
            // (aKind: 0 method, 1 getter, 2 setter)
            return guard([&]() -> value
            {
                auto const key = to_property_key(aKey);
                set_home(aFunction, aObject);
                set_function_name(aFunction, key, aKind == 1 ? "get" : aKind == 2 ? "set" : nullptr);
                std::uint8_t const attributes = static_cast<std::uint8_t>(AttributeConfigurable | (aEnumerable != 0 ? AttributeEnumerable : 0u));
                if (aKind == 0)
                    define_property_or_throw(aObject, key, property_descriptor::data(aFunction, static_cast<std::uint8_t>(attributes | AttributeWritable)));
                else
                {
                    property_descriptor d;
                    if (aKind == 1)
                        d.getter = aFunction;
                    else
                        d.setter = aFunction;
                    d.enumerable = aEnumerable != 0;
                    d.configurable = true;
                    define_property_or_throw(aObject, key, d);
                }
                return Undefined;
            });
        }

        value array(std::int32_t aCapacity)
        {
            auto const result = make_array();
            as_array(result)->elements.reserve(static_cast<std::size_t>(std::max(aCapacity, 0)));
            return result;
        }

        value array_push(value aArray, value aValue)
        {
            auto* a = as_array(aArray);
            a->elements.push_back(aValue);
            ++a->length;
            return Undefined;
        }

        value array_hole(value aArray)
        {
            auto* a = as_array(aArray);
            a->elements.push_back(Empty);
            ++a->length;
            return Undefined;
        }

        value array_spread(value aArray, value aIterable)
        {
            return guard([&]() -> value
            {
                auto* a = as_array(aArray);
                auto record = get_iterator(aIterable);
                while (auto const next = iterator_step_value(record))
                {
                    a->elements.push_back(*next);
                    ++a->length;
                }
                return Undefined;
            });
        }

        value template_object(std::int32_t aSite, value aCooked, value aRaw)
        {
            return guard([&]() -> value
            {
                auto const key = (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(current_program())) << 20) ^ static_cast<std::uint64_t>(aSite);
                auto& templates = the_realm().templates;
                if (auto const existing = templates.find(key); existing != templates.end())
                    return existing->second;
                set_integrity_level(aRaw, true);
                obj(aCooked).define_own_property(the_names().raw, property_descriptor::data(aRaw, 0u));
                set_integrity_level(aCooked, true);
                templates.emplace(key, aCooked);
                retain_program(current_program());
                return aCooked;
            });
        }

        value bigint(char const* aDigits)
        {
            return guard([&]() -> value { return parse_bigint_literal(aDigits); });
        }

        value regexp(value aPattern, value aFlags)
        {
            return guard([&]() -> value { return make_regexp(aPattern, aFlags); });
        }

        // --- iteration

        value iterator(value aIterable, std::int32_t aAsync)
        {
            return guard([&]() -> value { return make_iterator_record(get_iterator(aIterable, aAsync != 0)); });
        }

        value iterator_step(value aRecord)
        {
            return guard([&]() -> value
            {
                auto& record = iterator_record_of(aRecord);
                if (record.done)
                    return Empty;
                if (auto const next = iterator_step_value(record))
                    return *next;
                return Empty;
            });
        }

        value iterator_fast_next(value aRecord)
        {
            // (for-of: the iterator's next method if it is a compiled function (compiled code then calls it, and gets its results' done
            // and value, itself: as iterator_step does), else Empty (iterator_step))
            auto& record = iterator_record_of(aRecord);
            if (record.async || record.done)
                return Empty;
            auto* const f = as_function(record.nextMethod);
            if (f == nullptr || f->kind != function_kind::Compiled)
                return Empty;
            return record.nextMethod;
        }

        value iterator_object(value aRecord)
        {
            return iterator_record_of(aRecord).iterator;
        }

        value iterator_result_error(value aResult)
        {
            return guard([&]() -> value
            {
                throw_type_error("Iterator result " + describe(aResult) + " is not an object");
                return Undefined;
            });
        }

        value iterator_value(value aRecord)
        {
            // (destructuring: the next value, undefined once done)
            return guard([&]() -> value
            {
                auto& record = iterator_record_of(aRecord);
                if (record.done)
                    return Undefined;
                if (auto const next = iterator_step_value(record))
                    return *next;
                return Undefined;
            });
        }

        value iterator_rest(value aRecord)
        {
            return guard([&]() -> value
            {
                auto& record = iterator_record_of(aRecord);
                value_list elements;
                while (!record.done)
                    if (auto const next = iterator_step_value(record))
                        elements.push_back(*next);
                return make_array(std::move(elements));
            });
        }

        value iterator_close(value aRecord)
        {
            return guard([&]() -> value
            {
                auto& record = iterator_record_of(aRecord);
                record.done = true;
                javascript_runtime::iterator_close(record);
                return Undefined;
            });
        }

        value iterator_close_if_open(value aRecord)
        {
            return guard([&]() -> value
            {
                auto& record = iterator_record_of(aRecord);
                if (!record.done)
                {
                    record.done = true;
                    javascript_runtime::iterator_close(record);
                }
                return Undefined;
            });
        }

        value iterator_close_throw(value aRecord)
        {
            // (an exception is pending: the iterator is closed (unless it is done or failed) and its errors are ignored)
            auto const pending = calls().pending;
            auto& record = iterator_record_of(aRecord);
            if (!record.done)
            {
                record.done = true;
                iterator_close_on_throw(record);
            }
            calls().pending = pending;
            return Undefined;
        }

        // --- classes

        value private_name(value aDescription)
        {
            return guard([&]() -> value { return make_private_name(aDescription); });
        }

        value class_prototype_(value aHeritage)
        {
            return guard([&]() -> value { return class_prototype(aHeritage == Empty ? Empty : checked(aHeritage)); });
        }

        value class_constructor_(value aConstructor, value aPrototype, value aHeritage, value aName, value aSource)
        {
            return guard([&]() -> value { return class_constructor(aConstructor, aPrototype, aHeritage == Empty ? Empty : checked(aHeritage), aName, aSource); });
        }

        value class_method(value aConstructor, value aHome, value aName, value aFunction, std::int32_t aKind, std::int32_t aStatic)
        {
            return guard([&]() -> value { class_private_method(aConstructor, aHome, aName, aFunction, aKind, aStatic != 0); return Undefined; });
        }

        value class_field_(value aConstructor, value aHome, value aName, value aInitializer, std::int32_t aStatic)
        {
            return guard([&]() -> value { class_field(aConstructor, aHome, aName, aInitializer, aStatic != 0); return Undefined; });
        }

        value class_static_block_(value aConstructor, value aFunction)
        {
            return guard([&]() -> value { class_static_block(aConstructor, aFunction); return Undefined; });
        }

        value class_finish_(value aConstructor)
        {
            return guard([&]() -> value { return class_finish(aConstructor); });
        }

        value super_constructor_(value aConstructor)
        {
            return guard([&]() -> value { return super_constructor(aConstructor); });
        }

        value super_construct_(value aParent, value aNewTarget, value aArguments)
        {
            return guard([&]() -> value { return super_construct(aParent, checked(aNewTarget), aArguments); });
        }

        value initialize_fields(value aObject, value aConstructor)
        {
            return guard([&]() -> value { initialize_instance_elements(aObject, aConstructor); return Undefined; });
        }

        value private_get_(value aObject, value aName)
        {
            return guard([&]() -> value { return private_get(checked(aObject), aName); });
        }

        value private_set_(value aObject, value aName, value aValue)
        {
            return guard([&]() -> value { private_set(checked(aObject), aName, checked(aValue)); return checked(aValue); });
        }

        value private_in_(value aName, value aObject)
        {
            return guard([&]() -> value { return boolean(private_in(aName, checked(aObject))); });
        }

        // --- generators and async functions

        value start(value aCallee, value aThis, value aEnvironment, std::int32_t aBody, std::int32_t aFlags)
        {
            return guard([&]() -> value { return start_function(aCallee, aThis, aEnvironment, static_cast<std::uint32_t>(aBody), aFlags); });
        }

        value yield(value aValue)
        {
            return guard([&]() -> value { return yield_value(checked(aValue)); });
        }

        value yield_star(value aIterable)
        {
            return guard([&]() -> value { return yield_delegate(checked(aIterable)); });
        }

        value resume_value_()
        {
            return guard([&]() -> value { return resume_value(); });
        }

        value await(value aValue)
        {
            return guard([&]() -> value { return await_value(checked(aValue)); });
        }

        value rejected_promise()
        {
            // (an async function's parameters threw: it returns a rejected promise)
            auto const reason = calls().pending;
            calls().pending = Undefined;
            return guard([&]() -> value { return promise_rejected(reason); });
        }

        // --- using and await using declarations

        value dispose_new_()
        {
            return guard([&]() -> value { return dispose_new(); });
        }

        value dispose_add_(value aCapability, value aValue, std::int32_t aHint)
        {
            return guard([&]() -> value { dispose_add(aCapability, checked(aValue), aHint != 0); return Undefined; });
        }

        value dispose_all_(value aCapability, value aCompletion, std::int32_t aThrew)
        {
            return guard([&]() -> value { return dispose_all(aCapability, checked(aCompletion), aThrew != 0); });
        }

        value for_in(value aObject)
        {
            return guard([&]() -> value { return make_for_in_iterator(aObject); });
        }

        value for_in_next(value aIterator)
        {
            return guard([&]() -> value { return for_in_step(aIterator); });
        }
    }

    namespace api
    {
        value unsupported(char const* aWhat)
        {
            return guard([&]() -> value { throw_syntax_error(std::string{ "unsupported: " } + aWhat); });
        }

        constexpr auto I64 = NEOS_RT_I64;
        constexpr auto I32 = NEOS_RT_I32;
        constexpr auto STR = NEOS_RT_STRING;

#define NEOS_JS_BINARY(aName, aOperation) { aName, NEOS_RT_FUNCTION(+[](value l, value r) -> value { return binary(aOperation, l, r); }), I64, 2u, { I64, I64 }, 0u }
#define NEOS_JS_UNARY(aName, aOperation) { aName, NEOS_RT_FUNCTION(+[](value v) -> value { return unary(aOperation, v); }), I64, 1u, { I64 }, 0u }

        neos_rt_function const sFunctions[] =
        {
            { "init", NEOS_RT_FUNCTION(init), I64, 1u, { I32 }, 0u },
            { "string", NEOS_RT_FUNCTION(string), I64, 1u, { STR }, 0u },
            { "global_this", NEOS_RT_FUNCTION(global_this), I64, 0u, {}, 0u },
            { "finish", NEOS_RT_FUNCTION(finish), I32, 1u, { I64 }, 0u },
            { "throw", NEOS_RT_FUNCTION(throw_), I64, 1u, { I64 }, 0u },
            { "catch", NEOS_RT_FUNCTION(catch_), I64, 0u, {}, 0u },
            { "throw_tdz", NEOS_RT_FUNCTION(throw_tdz), I64, 1u, { I64 }, 0u },
            { "throw_const", NEOS_RT_FUNCTION(throw_const), I64, 1u, { I64 }, 0u },
            { "throw_this", NEOS_RT_FUNCTION(throw_this), I64, 1u, { I64 }, 0u },
            { "throw_super_twice", NEOS_RT_FUNCTION(throw_super_twice), I64, 0u, {}, 0u },
            { "throw_super_delete", NEOS_RT_FUNCTION(throw_super_delete), I64, 0u, {}, 0u },
            { "throw_invalid_assignment", NEOS_RT_FUNCTION(throw_invalid_assignment), I64, 0u, {}, 0u },
            { "global_declare", NEOS_RT_FUNCTION(global_declare), I64, 2u, { I64, I32 }, 0u },
            { "global_define_function", NEOS_RT_FUNCTION(global_define_function), I64, 2u, { I64, I64 }, 0u },
            { "global_get", NEOS_RT_FUNCTION(global_get), I64, 1u, { I64 }, 0u },
            { "global_get_typeof", NEOS_RT_FUNCTION(global_get_typeof), I64, 1u, { I64 }, 0u },
            { "intrinsics_original", NEOS_RT_FUNCTION(intrinsics_original), I32, 1u, { I32 }, 0u },
            { "global_resolved", NEOS_RT_FUNCTION(global_resolved), I32, 1u, { I64 }, 0u },
            { "global_set", NEOS_RT_FUNCTION(global_set), I64, 3u, { I64, I64, I32 }, 0u },
            { "global_init", NEOS_RT_FUNCTION(global_init), I64, 2u, { I64, I64 }, 0u },
            { "global_mirror", NEOS_RT_FUNCTION(global_mirror), I64, 2u, { I64, I32 }, 0u },
            { "global_root", NEOS_RT_FUNCTION(global_root), I64, 1u, { I32 }, 0u },
            { "concat_string_number", NEOS_RT_FUNCTION(concat_string_number), I64, 3u, { I64, I64, I32 }, 0u },
            { "get_length", NEOS_RT_FUNCTION(get_length), I64, 1u, { I64 }, 0u },
            { "get_length_site", NEOS_RT_FUNCTION(get_length_site), I64, 2u, { I64, I32 }, 0u },
            { "global_delete", NEOS_RT_FUNCTION(global_delete), I64, 1u, { I64 }, 0u },
            { "env_new", NEOS_RT_FUNCTION(env_new), I64, 2u, { I64, I32 }, 0u },
            { "env_copy", NEOS_RT_FUNCTION(env_copy), I64, 1u, { I64 }, 0u },
            { "env_get", NEOS_RT_FUNCTION(env_get), I64, 3u, { I64, I32, I32 }, 0u },
            { "env_get_checked", NEOS_RT_FUNCTION(env_get_checked), I64, 4u, { I64, I32, I32, I64 }, 0u },
            { "env_get_this", NEOS_RT_FUNCTION(env_get_this), I64, 4u, { I64, I32, I32, I64 }, 0u },
            { "env_set", NEOS_RT_FUNCTION(env_set), I64, 4u, { I64, I32, I32, I64 }, 0u },
            { "env_set_checked", NEOS_RT_FUNCTION(env_set_checked), I64, 5u, { I64, I32, I32, I64, I64 }, 0u },
            { "env_bind_this", NEOS_RT_FUNCTION(env_bind_this), I64, 4u, { I64, I32, I32, I64 }, 0u },
            { "with_has", NEOS_RT_FUNCTION(with_has), I64, 2u, { I64, I64 }, 0u },
            { "closure", NEOS_RT_FUNCTION(closure), I64, 7u, { I32, I32, I64, I32, I64, I32, I64 }, 0u },
            { "function_env", NEOS_RT_FUNCTION(function_env), I64, 1u, { I64 }, 0u },
            { "function_home", NEOS_RT_FUNCTION(function_home), I64, 1u, { I64 }, 0u },
            { "set_home", NEOS_RT_FUNCTION(set_home), I64, 2u, { I64, I64 }, 0u },
            { "name_function", NEOS_RT_FUNCTION(name_function), I64, 2u, { I64, I64 }, 0u },
            { "callee_number", NEOS_RT_FUNCTION(callee_number), I32, 1u, { I64 }, 0u },
            { "push_arg", NEOS_RT_FUNCTION(push_arg), I64, 1u, { I64 }, 0u },
            { "pop_args", NEOS_RT_FUNCTION(pop_args), I64, 1u, { I32 }, 0u },
            { "args_base", NEOS_RT_FUNCTION(args_base), I32, 1u, { I32 }, 0u },
            { "argument", NEOS_RT_FUNCTION(argument), I64, 3u, { I32, I32, I32 }, 0u },
            { "call", NEOS_RT_FUNCTION(call), I64, 7u, { I64, I64, I32, I64, I64, I64, I64 }, 0u },
            { "tail_call", NEOS_RT_FUNCTION(tail_call), I64, 7u, { I64, I64, I32, I64, I64, I64, I64 }, 0u },
            { "tail_call_spread", NEOS_RT_FUNCTION(tail_call_spread), I64, 3u, { I64, I64, I64 }, 0u },
            { "tail_resolve", NEOS_RT_FUNCTION(tail_resolve), I64, 0u, {}, 0u },
            { "call_spread", NEOS_RT_FUNCTION(call_spread), I64, 3u, { I64, I64, I64 }, 0u },
            { "construct", NEOS_RT_FUNCTION(construct), I64, 2u, { I64, I64 }, 0u },
            { "construct_number", NEOS_RT_FUNCTION(construct_number), I32, 1u, { I64 }, 0u },
            { "construct_this", NEOS_RT_FUNCTION(construct_this), I64, 1u, { I64 }, 0u },
            { "char_code_at", NEOS_RT_FUNCTION(char_code_at), I64, 3u, { I64, I64, I64 }, 0u },
            { "char_code_at_tail", NEOS_RT_FUNCTION(char_code_at_tail), I64, 3u, { I64, I64, I64 }, 0u },
            { "char_code_at_function", NEOS_RT_FUNCTION(char_code_at_function), I64, 0u, {}, 0u },
            { "slice_direct", NEOS_RT_FUNCTION(slice_direct), I64, 4u, { I64, I64, I64, I64 }, 0u },
            { "test_direct", NEOS_RT_FUNCTION(test_direct), I64, 3u, { I64, I64, I64 }, 0u },
            { "direct_builtin_function", NEOS_RT_FUNCTION(direct_builtin_function), I64, 1u, { I32 }, 0u },
            { "native_direct0", NEOS_RT_FUNCTION(native_direct0), I64, 2u, { I64, I64 }, 0u },
            { "native_direct1", NEOS_RT_FUNCTION(native_direct1), I64, 3u, { I64, I64, I64 }, 0u },
            { "native_direct2", NEOS_RT_FUNCTION(native_direct2), I64, 4u, { I64, I64, I64, I64 }, 0u },
            { "native_direct3", NEOS_RT_FUNCTION(native_direct3), I64, 5u, { I64, I64, I64, I64, I64 }, 0u },
            { "array_push_function", NEOS_RT_FUNCTION(array_push_function), I64, 0u, {}, 0u },
            { "array_prototype_value", NEOS_RT_FUNCTION(array_prototype_value), I64, 0u, {}, 0u },
            { "array_push_one", NEOS_RT_FUNCTION(array_push_one), I64, 3u, { I64, I64, I64 }, 0u },
            { "function_call_function", NEOS_RT_FUNCTION(function_call_function), I64, 0u, {}, 0u },
            { "sloppy_this", NEOS_RT_FUNCTION(sloppy_this), I64, 1u, { I64 }, 0u },
            { "arguments_object", NEOS_RT_FUNCTION(arguments_object), I64, 8u, { I64, I32, I64, I64, I64, I64, I32, I32 }, 0u },
            { "arguments_map", NEOS_RT_FUNCTION(arguments_map), I64, 4u, { I64, I64, I32, I32 }, 0u },
            { "rest", NEOS_RT_FUNCTION(rest), I64, 7u, { I32, I32, I64, I64, I64, I64, I32 }, 0u },
            { "derived_result", NEOS_RT_FUNCTION(derived_result), I64, 2u, { I64, I64 }, 0u },
            { "get", NEOS_RT_FUNCTION(get), I64, 2u, { I64, I64 }, 0u },
            { "get_named", NEOS_RT_FUNCTION(get_named), I64, 2u, { I64, I64 }, 0u },
            { "get_named_cached", NEOS_RT_FUNCTION(get_named_cached), I64, 3u, { I64, I64, I32 }, 0u },
            { "property_sites", NEOS_RT_FUNCTION(property_sites), I64, 2u, { I32, I32 }, 0u },
            { "fixed_global", NEOS_RT_FUNCTION(fixed_global), I64, 2u, { I64, I32 }, 0u },
            { "get_cached", NEOS_RT_FUNCTION(get_cached), I64, 3u, { I64, I64, I32 }, 0u },
            { "callee_number_cached", NEOS_RT_FUNCTION(callee_number_cached), I32, 2u, { I64, I32 }, 0u },
            { "construct_number_cached", NEOS_RT_FUNCTION(construct_number_cached), I32, 2u, { I64, I32 }, 0u },
            { "construct_this_cached", NEOS_RT_FUNCTION(construct_this_cached), I64, 2u, { I64, I32 }, 0u },
            { "allocation_state", NEOS_RT_FUNCTION(allocation_state_offset), I64, 0u, {}, 0u },
            { "allocate_slot", NEOS_RT_FUNCTION(allocate_slot), I64, 1u, { I32 }, NEOS_RT_COMMON },
            { "array_literal", NEOS_RT_FUNCTION(array_literal), I64, 2u, { I32, I32 }, 0u },
            { "object_literal_remember", NEOS_RT_FUNCTION(object_literal_remember), I64, 3u, { I64, I32, I32 }, 0u },
            { "set", NEOS_RT_FUNCTION(set), I64, 4u, { I64, I64, I64, I32 }, 0u },
            { "set_named", NEOS_RT_FUNCTION(set_named), I64, 4u, { I64, I64, I64, I32 }, 0u },
            { "set_named_cached", NEOS_RT_FUNCTION(set_named_cached), I64, 5u, { I64, I64, I64, I32, I32 }, 0u },
            { "delete", NEOS_RT_FUNCTION(delete_), I64, 3u, { I64, I64, I32 }, 0u },
            { "super_base", NEOS_RT_FUNCTION(super_base), I64, 1u, { I64 }, 0u },
            { "super_get", NEOS_RT_FUNCTION(super_get), I64, 3u, { I64, I64, I64 }, 0u },
            { "super_set", NEOS_RT_FUNCTION(super_set), I64, 4u, { I64, I64, I64, I64 }, 0u },
            { "super_set_sloppy", NEOS_RT_FUNCTION(super_set_sloppy), I64, 4u, { I64, I64, I64, I64 }, 0u },
            { "require_object_coercible", NEOS_RT_FUNCTION(require_object_coercible), I64, 1u, { I64 }, 0u },
            { "typeof", NEOS_RT_FUNCTION(typeof_), I64, 1u, { I64 }, 0u },
            { "to_boolean", NEOS_RT_FUNCTION(to_boolean_), I32, 1u, { I64 }, 0u },
            { "to_number", NEOS_RT_FUNCTION(to_number_), I64, 1u, { I64 }, 0u },
            { "to_numeric", NEOS_RT_FUNCTION(to_numeric_), I64, 1u, { I64 }, 0u },
            { "to_string", NEOS_RT_FUNCTION(to_string_), I64, 1u, { I64 }, 0u },
            { "to_object", NEOS_RT_FUNCTION(to_object_), I64, 1u, { I64 }, 0u },
            { "to_property_key", NEOS_RT_FUNCTION(to_property_key_), I64, 1u, { I64 }, 0u },
            { "concat", NEOS_RT_FUNCTION(concat), I64, 2u, { I64, I64 }, 0u },
            { "strict_equal", NEOS_RT_FUNCTION(strict_equal), I32, 2u, { I64, I64 }, 0u },
            NEOS_JS_BINARY("add", 0), NEOS_JS_BINARY("subtract", 1), NEOS_JS_BINARY("multiply", 2), NEOS_JS_BINARY("divide", 3),
            NEOS_JS_BINARY("remainder", 4), NEOS_JS_BINARY("exponent", 5), NEOS_JS_BINARY("shift_left", 6), NEOS_JS_BINARY("shift_right", 7),
            NEOS_JS_BINARY("shift_right_unsigned", 8), NEOS_JS_BINARY("bitwise_and", 9), NEOS_JS_BINARY("bitwise_or", 10), NEOS_JS_BINARY("bitwise_xor", 11),
            NEOS_JS_BINARY("less_than", 12), NEOS_JS_BINARY("greater_than", 13), NEOS_JS_BINARY("less_equal", 14), NEOS_JS_BINARY("greater_equal", 15),
            NEOS_JS_BINARY("equal", 16), NEOS_JS_BINARY("not_equal", 17), NEOS_JS_BINARY("strict_equal_value", 18), NEOS_JS_BINARY("strict_not_equal", 19),
            NEOS_JS_BINARY("instance_of", 20), NEOS_JS_BINARY("in", 21),
            NEOS_JS_UNARY("negate", 0), NEOS_JS_UNARY("bitwise_not", 1), NEOS_JS_UNARY("increment", 2), NEOS_JS_UNARY("decrement", 3),
            { "object", NEOS_RT_FUNCTION(object), I64, 0u, {}, 0u },
            { "object_with", NEOS_RT_FUNCTION(object_with), I64, 1u, { I32 }, 0u },
            { "object_define", NEOS_RT_FUNCTION(object_define), I64, 3u, { I64, I64, I64 }, 0u },
            { "object_proto", NEOS_RT_FUNCTION(object_proto), I64, 2u, { I64, I64 }, 0u },
            { "object_spread", NEOS_RT_FUNCTION(object_spread), I64, 2u, { I64, I64 }, 0u },
            { "object_rest", NEOS_RT_FUNCTION(object_rest), I64, 2u, { I64, I64 }, 0u },
            { "define_method", NEOS_RT_FUNCTION(define_method), I64, 5u, { I64, I64, I64, I32, I32 }, 0u },
            { "array", NEOS_RT_FUNCTION(array), I64, 1u, { I32 }, 0u },
            { "array_push", NEOS_RT_FUNCTION(array_push), I64, 2u, { I64, I64 }, 0u },
            { "array_hole", NEOS_RT_FUNCTION(array_hole), I64, 1u, { I64 }, 0u },
            { "array_spread", NEOS_RT_FUNCTION(array_spread), I64, 2u, { I64, I64 }, 0u },
            { "template_object", NEOS_RT_FUNCTION(template_object), I64, 3u, { I32, I64, I64 }, 0u },
            { "bigint", NEOS_RT_FUNCTION(bigint), I64, 1u, { STR }, 0u },
            { "regexp", NEOS_RT_FUNCTION(regexp), I64, 2u, { I64, I64 }, 0u },
            { "iterator", NEOS_RT_FUNCTION(iterator), I64, 2u, { I64, I32 }, 0u },
            { "iterator_step", NEOS_RT_FUNCTION(iterator_step), I64, 1u, { I64 }, 0u },
            { "iterator_fast_next", NEOS_RT_FUNCTION(iterator_fast_next), I64, 1u, { I64 }, 0u },
            { "iterator_object", NEOS_RT_FUNCTION(iterator_object), I64, 1u, { I64 }, 0u },
            { "iterator_result_error", NEOS_RT_FUNCTION(iterator_result_error), I64, 1u, { I64 }, 0u },
            { "iterator_value", NEOS_RT_FUNCTION(iterator_value), I64, 1u, { I64 }, 0u },
            { "iterator_rest", NEOS_RT_FUNCTION(iterator_rest), I64, 1u, { I64 }, 0u },
            { "iterator_close", NEOS_RT_FUNCTION(iterator_close), I64, 1u, { I64 }, 0u },
            { "private_name", NEOS_RT_FUNCTION(private_name), I64, 1u, { I64 }, 0u },
            { "class_prototype", NEOS_RT_FUNCTION(class_prototype_), I64, 1u, { I64 }, 0u },
            { "class_constructor", NEOS_RT_FUNCTION(class_constructor_), I64, 5u, { I64, I64, I64, I64, I64 }, 0u },
            { "class_method", NEOS_RT_FUNCTION(class_method), I64, 6u, { I64, I64, I64, I64, I32, I32 }, 0u },
            { "class_field", NEOS_RT_FUNCTION(class_field_), I64, 5u, { I64, I64, I64, I64, I32 }, 0u },
            { "class_static_block", NEOS_RT_FUNCTION(class_static_block_), I64, 2u, { I64, I64 }, 0u },
            { "class_finish", NEOS_RT_FUNCTION(class_finish_), I64, 1u, { I64 }, 0u },
            { "super_constructor", NEOS_RT_FUNCTION(super_constructor_), I64, 1u, { I64 }, 0u },
            { "super_construct", NEOS_RT_FUNCTION(super_construct_), I64, 3u, { I64, I64, I64 }, 0u },
            { "initialize_fields", NEOS_RT_FUNCTION(initialize_fields), I64, 2u, { I64, I64 }, 0u },
            { "private_get", NEOS_RT_FUNCTION(private_get_), I64, 2u, { I64, I64 }, 0u },
            { "private_set", NEOS_RT_FUNCTION(private_set_), I64, 3u, { I64, I64, I64 }, 0u },
            { "private_in", NEOS_RT_FUNCTION(private_in_), I64, 2u, { I64, I64 }, 0u },
            { "env_names", NEOS_RT_FUNCTION(env_names), I64, 2u, { I64, I64 }, 0u },
            { "env_with", NEOS_RT_FUNCTION(env_with), I64, 2u, { I64, I64 }, 0u },
            { "dyn_get", NEOS_RT_FUNCTION(dyn_get), I64, 3u, { I64, I64, I32 }, 0u },
            { "dyn_set", NEOS_RT_FUNCTION(dyn_set), I64, 4u, { I64, I64, I64, I32 }, 0u },
            { "dyn_delete", NEOS_RT_FUNCTION(dyn_delete), I64, 2u, { I64, I64 }, 0u },
            { "dyn_this", NEOS_RT_FUNCTION(dyn_this), I64, 2u, { I64, I64 }, 0u },
            { "dyn_reference", NEOS_RT_FUNCTION(dyn_reference), I64, 2u, { I64, I64 }, 0u },
            { "dyn_reference_get", NEOS_RT_FUNCTION(dyn_reference_get), I64, 3u, { I64, I64, I32 }, 0u },
            { "dyn_reference_this", NEOS_RT_FUNCTION(dyn_reference_this), I64, 1u, { I64 }, 0u },
            { "dyn_reference_set", NEOS_RT_FUNCTION(dyn_reference_set), I64, 4u, { I64, I64, I64, I32 }, 0u },
            { "eval_declare", NEOS_RT_FUNCTION(eval_declare), I64, 3u, { I64, I64, I32 }, 0u },
            { "eval_function", NEOS_RT_FUNCTION(eval_function), I64, 3u, { I64, I64, I64 }, 0u },
            { "eval_var_set", NEOS_RT_FUNCTION(eval_var_set), I64, 3u, { I64, I64, I64 }, 0u },
            { "eval_direct", NEOS_RT_FUNCTION(eval_direct), I64, 8u, { I64, I64, I32, I32, I64, I64, I64, I64 }, 0u },
            { "eval_this", NEOS_RT_FUNCTION(eval_this), I64, 0u, {}, 0u },
            { "eval_this_derived", NEOS_RT_FUNCTION(eval_this_derived), I64, 1u, { I64 }, 0u },
            { "eval_bind_this", NEOS_RT_FUNCTION(eval_bind_this), I64, 2u, { I64, I64 }, 0u },
            { "eval_internal", NEOS_RT_FUNCTION(eval_internal), I64, 2u, { I64, I64 }, 0u },
            { "eval_new_target", NEOS_RT_FUNCTION(eval_new_target), I64, 0u, {}, 0u },
            { "eval_home", NEOS_RT_FUNCTION(eval_home), I64, 0u, {}, 0u },
            { "eval_environment", NEOS_RT_FUNCTION(eval_environment), I64, 0u, {}, 0u },
            { "eval_complete", NEOS_RT_FUNCTION(eval_complete), I64, 1u, { I64 }, 0u },
            { "module_define", NEOS_RT_FUNCTION(module_define), I64, 2u, { I32, I64 }, 0u },
            { "module_main", NEOS_RT_FUNCTION(module_main), I32, 0u, {}, 0u },
            { "import_get", NEOS_RT_FUNCTION(import_get), I64, 4u, { I64, I32, I32, I64 }, 0u },
            { "import_meta", NEOS_RT_FUNCTION(import_meta), I64, 1u, { I64 }, 0u },
            { "import_dynamic", NEOS_RT_FUNCTION(import_dynamic_), I64, 2u, { I64, I64 }, 0u },
            { "start", NEOS_RT_FUNCTION(start), I64, 5u, { I64, I64, I64, I32, I32 }, 0u },
            { "yield", NEOS_RT_FUNCTION(yield), I64, 1u, { I64 }, 0u },
            { "yield_star", NEOS_RT_FUNCTION(yield_star), I64, 1u, { I64 }, 0u },
            { "resume_value", NEOS_RT_FUNCTION(resume_value_), I64, 0u, {}, 0u },
            { "await", NEOS_RT_FUNCTION(await), I64, 1u, { I64 }, 0u },
            { "rejected_promise", NEOS_RT_FUNCTION(rejected_promise), I64, 0u, {}, 0u },
            { "iterator_close_if_open", NEOS_RT_FUNCTION(iterator_close_if_open), I64, 1u, { I64 }, 0u },
            { "iterator_close_throw", NEOS_RT_FUNCTION(iterator_close_throw), I64, 1u, { I64 }, 0u },
            { "for_in", NEOS_RT_FUNCTION(for_in), I64, 1u, { I64 }, 0u },
            { "for_in_next", NEOS_RT_FUNCTION(for_in_next), I64, 1u, { I64 }, 0u },
            { "dispose_new", NEOS_RT_FUNCTION(dispose_new_), I64, 0u, {}, 0u },
            { "dispose_add", NEOS_RT_FUNCTION(dispose_add_), I64, 3u, { I64, I64, I32 }, 0u },
            { "dispose_all", NEOS_RT_FUNCTION(dispose_all_), I64, 3u, { I64, I64, I32 }, 0u },
        };
#undef NEOS_JS_BINARY
#undef NEOS_JS_UNARY
    }

    namespace
    {
        // (the programs whose code something refers to (a function they made, or a template object of theirs): they stay loaded)
        struct retained
        {
            std::unordered_set<void*> programs;
            void* last = nullptr;   // (retained: (one that is retained is not unloaded while its isolate lives, so its address is not reused))
        };
        retained& retained_programs()
        {
            static isolate_local<retained> sPrograms;
            return *sPrograms;
        }
    }

    void retain_program(void* aProgram)
    {
        auto& r = retained_programs();
        if (aProgram == r.last)
            return;
        r.programs.insert(aProgram);
        r.last = aProgram;
    }

    bool program_retained(void* aProgram)
    {
        return retained_programs().programs.contains(aProgram);
    }

    std::span<neos_rt_function const> api_functions()
    {
        return api::sFunctions;
    }

    void program_ended()
    {
        auto* const program = current_program();
        program_ended(program);
        // (the end of the program whose isolate it is: the isolate ends (its cells, its state, its pages), and with it the thread's state
        // that refers to it)
        if (auto* const h = current_isolate(); h != nullptr && h->mainProgram == program)
        {
            calls().pending = Undefined;
            calls().extraArguments.clear();
            calls().extraArguments.shrink_to_fit();
            calls().tail = {};
            calls().deferTail = false;
            isolate_ending();
            end_isolate();
        }
    }

    void program_ended(void* aProgram)
    {
        // (the program has ended (its globals are about to go): its mirrors are no longer kept (a global lexical binding whose value was
        // its mirror has its value back) and the values of its words that were kept no longer are)
        auto* const program = aProgram;
        auto& words = api::program_words();
        for (auto const& w : words)
        {
            if (w.program != program || w.mirrorRealm == nullptr)
                continue;
            auto& r = *w.mirrorRealm;
            if (auto const m = r.mirrors.find(w.name); m != r.mirrors.end())
            {
                std::erase(m->second, w.word);
                if (m->second.empty())
                    r.mirrors.erase(m);
            }
            if (auto const b = r.lexicals.find(w.name); b != r.lexicals.end() && b->second.slot == w.word)
            {
                b->second.stored = static_cast<value>(*w.word);
                b->second.slot = nullptr;
            }
        }
        std::erase_if(words, [&](api::program_word_use const& w) { return w.program == program; });
    }
}
