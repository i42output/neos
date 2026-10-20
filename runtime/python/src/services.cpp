/*
  services.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/platform.py, Lib/keyword.py, Lib/copyreg.py, Lib/secrets.py,
  Lib/timeit.py and Modules/signalmodule.c), Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They
  are used under the Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated
  from Python and C to C++ for the neos Python runtime plugin and adapted to its object model (see runtime/python/NOTICE).

  Lib/platform.py's copyright and permission notice:

  Copyright (c) 1999-2000, Marc-Andre Lemburg; mailto:mal@lemburg.com
  Copyright (c) 2000-2010, eGenix.com Software GmbH; mailto:info@egenix.com

  Permission to use, copy, modify, and distribute this software and its
  documentation for any purpose and without fee or royalty is hereby granted,
  provided that the above copyright notice appear in all copies and that
  both that copyright notice and this permission notice appear in
  supporting documentation or portions thereof, including modifications,
  that you make.

  EGENIX.COM SOFTWARE GMBH DISCLAIMS ALL WARRANTIES WITH REGARD TO
  THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
  FITNESS, IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL,
  INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING
  FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
  NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
  WITH THE USE OR PERFORMANCE OF THIS SOFTWARE !
*/

// The modules keyword, atexit, gc, copyreg, platform, secrets and timeit (CPython 3.13's behaviour and messages)

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <sys/time.h>
#include <sys/utsname.h>
#endif
#if defined(__GLIBC__)
#include <gnu/libc-version.h>
#endif
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            // --- keyword

            char const* const sKeywords[] = { "False", "None", "True", "and", "as", "assert", "async", "await", "break", "class", "continue", "def",
                "del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal", "not", "or",
                "pass", "raise", "return", "try", "while", "with", "yield" };
            char const* const sSoftKeywords[] = { "_", "case", "match", "type" };

            template <std::size_t N>
            object* keyword_test(std::vector<object*> const& aArgs, char const* const (&aWords)[N], char const* aName)
            {
                if (aArgs.size() != 1u)
                    return error("TypeError", std::string{ aName } + "() takes exactly one argument (" + std::to_string(aArgs.size()) + " given)");
                auto const text = str_of_value(aArgs[0]);
                if (text == nullptr)
                {
                    // (frozenset.__contains__: an unhashable argument raises)
                    long long h = 0;
                    if (!hash_of(aArgs[0], h))
                        return nullptr;
                    return make_bool(false);
                }
                return make_bool(std::any_of(std::begin(aWords), std::end(aWords), [&](char const* w) { return *text == w; }));
            }

            builtin_entry const sKeywordFunctions[] =
            {
                { "iskeyword", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return no_keywords(aKwargs, "iskeyword") ? keyword_test(aArgs, sKeywords, "__contains__") : nullptr;
                    } },
                { "issoftkeyword", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return no_keywords(aKwargs, "issoftkeyword") ? keyword_test(aArgs, sSoftKeywords, "__contains__") : nullptr;
                    } }
            };

            template <std::size_t N>
            object* word_list(char const* const (&aWords)[N])
            {
                std::vector<object*> items;
                for (auto w : aWords)
                    items.push_back(make_str(w));
                return make_list(std::move(items));
            }

            bool keyword_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                aAttributes.emplace_back("kwlist", word_list(sKeywords));
                aAttributes.emplace_back("softkwlist", word_list(sSoftKeywords));
                aAttributes.emplace_back("__all__", make_list({ make_str("iskeyword"), make_str("issoftkeyword"), make_str("kwlist"), make_str("softkwlist") }));
                for (auto const& f : sKeywordFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                return true;
            }

            // --- atexit: the functions that are called when the program ends (the last registered first)

            struct exit_function
            {
                object* function;
                object* args;
                object* kwargs;
            };
            std::recursive_mutex sExitMutex;
            std::vector<exit_function> sExitFunctions;
            bool sExiting = false;
            std::size_t sUnregistered = 0u;     ///< (CPython's _ncallbacks() counts the unregistered functions' slots)

            void clear_exit_functions()
            {
                std::vector<exit_function> functions;
                {
                    std::lock_guard<std::recursive_mutex> lock{ sExitMutex };
                    functions.swap(sExitFunctions);
                    sUnregistered = 0u;
                }
                for (auto& f : functions)
                {
                    release(f.function);
                    release(f.args);
                    release(f.kwargs);
                }
            }

            void call_exit_functions()
            {
                for (;;)
                {
                    exit_function f{};
                    {
                        std::lock_guard<std::recursive_mutex> lock{ sExitMutex };
                        if (sExitFunctions.empty())
                            break;
                        f = sExitFunctions.back();
                        sExitFunctions.pop_back();
                    }
                    ref const function{ f.function };
                    ref const args{ f.args };
                    ref const kwargs{ f.kwargs };
                    ref const r{ call(function.get(), args.get(), kwargs.get()) };
                    if (!r)
                    {
                        // ("Exception ignored in atexit callback <function>:" and the exception's report)
                        if (ref const out{ sys_stdout() })
                        {
                            ref const flushed{ call_method_named(out.get(), "flush", {}) };
                        }
                        std::fflush(stdout);
                        std::string const text = error_text();
                        clear_error();
                        ref const e{ fn_caught(text.c_str()) };
                        fn_handled();
                        clear_error();
                        std::string name;
                        if (!repr_of(function.get(), name))
                            clear_error();
                        std::set<object*> seen;
                        std::string const report = e ? exception_report(e.get(), seen) : text + "\n";
                        std::fprintf(stderr, "Exception ignored in atexit callback %s:\n%s", name.c_str(), report.c_str());
                        std::fflush(stderr);
                    }
                }
            }

            builtin_entry const sAtexitFunctions[] =
            {
                { "register", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aArgs.empty())
                            return error("TypeError", "register() takes at least 1 argument (0 given)");
                        if (!is_callable(aArgs[0]))
                            return error("TypeError", "the first argument must be callable");
                        exit_function f{ new_reference(aArgs[0]), make_tuple({}), nullptr };
                        for (std::size_t i = 1u; i < aArgs.size(); ++i)
                            as<tuple_object>(f.args)->items.push_back(new_reference(aArgs[i]));
                        if (aKwargs != nullptr)
                        {
                            f.kwargs = make_dict();
                            dict_update(f.kwargs, aKwargs);
                        }
                        {
                            std::lock_guard<std::recursive_mutex> lock{ sExitMutex };
                            sExitFunctions.push_back(f);
                        }
                        return new_reference(aArgs[0]);
                    } },
                { "unregister", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_keywords(aKwargs, "unregister"))
                            return nullptr;
                        if (aArgs.size() != 1u)
                            return error("TypeError", "atexit.unregister() takes exactly one argument (" + std::to_string(aArgs.size()) + " given)");
                        std::vector<exit_function> removed;
                        {
                            std::lock_guard<std::recursive_mutex> lock{ sExitMutex };
                            for (std::size_t i = 0u; i < sExitFunctions.size();)
                            {
                                auto const e = equal(sExitFunctions[i].function, aArgs[0]);
                                if (e < 0)
                                    return nullptr;
                                if (e == 1)
                                {
                                    removed.push_back(sExitFunctions[i]);
                                    ++sUnregistered;
                                    sExitFunctions.erase(sExitFunctions.begin() + static_cast<std::ptrdiff_t>(i));
                                }
                                else
                                    ++i;
                            }
                        }
                        for (auto& f : removed)
                        {
                            release(f.function);
                            release(f.args);
                            release(f.kwargs);
                        }
                        return new_reference(none());
                    } },
                { "_clear", [](std::vector<object*> const&, object*) -> object*
                    {
                        clear_exit_functions();
                        return new_reference(none());
                    } },
                { "_ncallbacks", [](std::vector<object*> const&, object*) -> object*
                    {
                        std::lock_guard<std::recursive_mutex> lock{ sExitMutex };
                        return make_int(static_cast<long long>(sExitFunctions.size() + sUnregistered));
                    } },
                { "_run_exitfuncs", [](std::vector<object*> const&, object*) -> object*
                    {
                        call_exit_functions();
                        return new_reference(none());
                    } }
            };

            bool atexit_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sAtexitFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                return true;
            }

            // --- gc (objects are reference counted: there are no cycles to collect)

            bool sGcEnabled = true;
            long long sGcThresholds[3] = { 2000, 10, 10 };
            long long sGcDebug = 0;
            long long sFrozen = 0;

            bool atomic_object(object* aObject)
            {
                auto const k = kind_of(aObject);
                return k == NEOS_PY_NONE || k == NEOS_PY_BOOL || k == NEOS_PY_INT || k == NEOS_PY_FLOAT || k == NEOS_PY_COMPLEX || k == NEOS_PY_STR ||
                    k == NEOS_PY_NOT_IMPLEMENTED || k == NEOS_PY_ELLIPSIS || k == NEOS_PY_RANGE || k == NEOS_PY_SLICE;
            }

            builtin_entry const sGcFunctions[] =
            {
                { "enable", [](std::vector<object*> const&, object*) -> object* { sGcEnabled = true; return new_reference(none()); } },
                { "disable", [](std::vector<object*> const&, object*) -> object* { sGcEnabled = false; return new_reference(none()); } },
                { "isenabled", [](std::vector<object*> const&, object*) -> object* { return make_bool(sGcEnabled); } },
                { "collect", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("collect", aArgs, aKwargs, { "generation" }, 0u, 0u, a))
                            return nullptr;
                        long long generation = 2;
                        if (a[0] != nullptr && !to_index(a[0], generation))
                            return nullptr;
                        if (generation < 0 || generation > 2)
                            return error("ValueError", "invalid generation");
                        return make_int(0);
                    } },
                { "get_count", [](std::vector<object*> const&, object*) -> object* { return make_tuple({ make_int(0), make_int(0), make_int(0) }); } },
                { "get_threshold", [](std::vector<object*> const&, object*) -> object*
                    {
                        return make_tuple({ make_int(sGcThresholds[0]), make_int(sGcThresholds[1]), make_int(sGcThresholds[2]) });
                    } },
                { "set_threshold", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!no_keywords(aKwargs, "set_threshold"))
                            return nullptr;
                        if (aArgs.empty() || aArgs.size() > 3u)
                            return error("TypeError", "set_threshold() takes from 1 to 3 positional arguments but " + std::to_string(aArgs.size()) + " were given");
                        for (std::size_t i = 0u; i < aArgs.size(); ++i)
                            if (!to_index(aArgs[i], sGcThresholds[i]))
                                return nullptr;
                        return new_reference(none());
                    } },
                { "get_debug", [](std::vector<object*> const&, object*) -> object* { return make_int(sGcDebug); } },
                { "set_debug", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("set_debug", aArgs, aKwargs, { "flags" }, 1u, 1u, a) || !to_index(a[0], sGcDebug))
                            return nullptr;
                        return new_reference(none());
                    } },
                { "get_objects", [](std::vector<object*> const&, object*) -> object* { return make_list(); } },
                { "get_stats", [](std::vector<object*> const&, object*) -> object*
                    {
                        std::vector<object*> stats;
                        for (int i = 0; i < 3; ++i)
                        {
                            auto const d = make_dict();
                            for (auto name : { "collections", "collected", "uncollectable" })
                            {
                                ref const key{ make_str(name) };
                                ref const zero{ make_int(0) };
                                dict_set(d, key.get(), zero.get());
                            }
                            stats.push_back(d);
                        }
                        return make_list(std::move(stats));
                    } },
                { "get_referrers", [](std::vector<object*> const&, object*) -> object* { return make_list(); } },
                { "get_referents", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        // (the containers' items)
                        std::vector<object*> result;
                        for (auto a : aArgs)
                        {
                            auto const k = kind_of(a);
                            if (k == NEOS_PY_LIST || k == NEOS_PY_TUPLE)
                                for (auto i : k == NEOS_PY_LIST ? as<list_object>(a)->items : as<tuple_object>(a)->items)
                                    result.push_back(new_reference(i));
                            else if (k == NEOS_PY_DICT)
                                for (auto const& i : as<dict_object>(a)->items)
                                {
                                    result.push_back(new_reference(i.first));
                                    result.push_back(new_reference(i.second));
                                }
                        }
                        return make_list(std::move(result));
                    } },
                { "is_tracked", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("is_tracked", aArgs, aKwargs, { "obj" }, 1u, 1u, a))
                            return nullptr;
                        return make_bool(!atomic_object(a[0]));
                    } },
                { "is_finalized", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("is_finalized", aArgs, aKwargs, { "obj" }, 1u, 1u, a))
                            return nullptr;
                        return make_bool(false);
                    } },
                { "freeze", [](std::vector<object*> const&, object*) -> object* { return new_reference(none()); } },
                { "unfreeze", [](std::vector<object*> const&, object*) -> object* { sFrozen = 0; return new_reference(none()); } },
                { "get_freeze_count", [](std::vector<object*> const&, object*) -> object* { return make_int(sFrozen); } }
            };

            bool gc_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sGcFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("garbage", make_list());
                aAttributes.emplace_back("callbacks", make_list());
                aAttributes.emplace_back("DEBUG_STATS", make_int(1));
                aAttributes.emplace_back("DEBUG_COLLECTABLE", make_int(2));
                aAttributes.emplace_back("DEBUG_UNCOLLECTABLE", make_int(4));
                aAttributes.emplace_back("DEBUG_SAVEALL", make_int(32));
                aAttributes.emplace_back("DEBUG_LEAK", make_int(38));
                return true;
            }

            // --- copyreg (the pickle protocol's helpers: object.__reduce_ex__)

            builtin_entry const sCopyregFunctions[] =
            {
                // __newobj__(cls, *args): cls.__new__(cls, *args)
                { "__newobj__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aArgs.empty())
                            return error("TypeError", "__newobj__() missing 1 required positional argument: 'cls'");
                        ref const constructor{ getattr(aArgs[0], "__new__") };
                        if (!constructor)
                            return nullptr;
                        std::vector<object*> args;
                        for (auto a : aArgs)
                            args.push_back(new_reference(a));
                        return invoke(constructor.get(), std::move(args), aKwargs);
                    } },
                // __newobj_ex__(cls, args, kwargs)
                { "__newobj_ex__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("__newobj_ex__", aArgs, aKwargs, { "cls", "args", "kwargs" }, 3u, 0u, a))
                            return nullptr;
                        ref const constructor{ getattr(a[0], "__new__") };
                        if (!constructor)
                            return nullptr;
                        std::vector<object*> args{ new_reference(a[0]) };
                        std::vector<object*> rest;
                        if (!collect(a[1], rest))
                            return nullptr;
                        args.insert(args.end(), rest.begin(), rest.end());
                        return invoke(constructor.get(), std::move(args), a[2]);
                    } },
                // _reconstructor(cls, base, state)
                { "_reconstructor", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("_reconstructor", aArgs, aKwargs, { "cls", "base", "state" }, 3u, 0u, a))
                            return nullptr;
                        if (is_type_object(a[1]) && type_qualified_name(a[1]) == "object")
                        {
                            ref const constructor{ module_attribute("builtins", "object") };
                            ref const n{ constructor ? getattr(constructor.get(), "__new__") : nullptr };
                            return n ? invoke(n.get(), { new_reference(a[0]) }) : nullptr;
                        }
                        ref const constructor{ getattr(a[1], "__new__") };
                        if (!constructor)
                            return nullptr;
                        ref obj{ invoke(constructor.get(), { new_reference(a[0]), new_reference(a[2]) }) };
                        if (!obj)
                            return nullptr;
                        ref const init{ getattr(a[1], "__init__") };
                        if (init)
                        {
                            ref const r{ invoke(init.get(), { new_reference(obj.get()), new_reference(a[2]) }) };
                            if (!r)
                                return nullptr;
                        }
                        else
                            clear_error();
                        return obj.release_ownership();
                    } },
                // (object.__reduce_ex__(self, protocol))
                { "__reduce_ex__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("__reduce_ex__", aArgs, aKwargs, { "self", "protocol" }, 2u, 2u, a))
                            return nullptr;
                        auto const self = a[0];
                        long long protocol = 0;
                        if (!to_index(a[1], protocol))
                            return nullptr;
                        ref const cls{ class_of(self) };
                        if (!is_class(cls.get()))
                        {
                            std::string name = type_name(self);
                            return error("TypeError", "cannot pickle '" + name + "' object");
                        }
                        ref const getstate{ getattr(self, "__getstate__") };
                        ref const state{ getstate ? invoke(getstate.get(), {}) : nullptr };
                        if (!state)
                            return nullptr;
                        if (protocol >= 2)
                        {
                            std::vector<object*> args{ new_reference(cls.get()) };
                            if (ref const getnewargs{ special_method(self, "__getnewargs__") })
                            {
                                ref const extra{ invoke(getnewargs.get(), {}) };
                                if (!extra)
                                    return nullptr;
                                std::vector<object*> items;
                                if (!collect(extra.get(), items))
                                    return nullptr;
                                args.insert(args.end(), items.begin(), items.end());
                            }
                            ref const value{ builtin_value(self) != self ? new_reference(builtin_value(self)) : nullptr };
                            auto const vk = value ? kind_of(value.get()) : NEOS_PY_NONE;
                            ref const listItems{ vk == NEOS_PY_LIST ? iter(value.get()) : new_reference(none()) };
                            ref const dictItems{ vk == NEOS_PY_DICT ? call_method_named(value.get(), "items", {}) : new_reference(none()) };
                            ref const dictIter{ vk == NEOS_PY_DICT && dictItems ? iter(dictItems.get()) : new_reference(none()) };
                            ref const newobj{ module_attribute("copyreg", "__newobj__") };
                            if (!newobj || !listItems || !dictIter)
                                return nullptr;
                            return make_tuple({ new_reference(newobj.get()), make_tuple(std::move(args)), new_reference(state.get()), new_reference(listItems.get()),
                                new_reference(dictIter.get()) });
                        }
                        // (protocols 0 and 1: copyreg._reconstructor(cls, base, state))
                        ref const reconstructor{ module_attribute("copyreg", "_reconstructor") };
                        if (!reconstructor)
                            return nullptr;
                        ref base{ module_attribute("builtins", "object") };
                        ref baseState{ new_reference(none()) };
                        if (auto const value = builtin_value(self); value != self)
                        {
                            base = ref{ class_of(value) };
                            baseState = ref{ new_reference(value) };
                        }
                        ref const args{ make_tuple({ new_reference(cls.get()), new_reference(base.get()), new_reference(baseState.get()) }) };
                        if (truth(state.get()) == 1)
                            return make_tuple({ new_reference(reconstructor.get()), new_reference(args.get()), new_reference(state.get()) });
                        return make_tuple({ new_reference(reconstructor.get()), new_reference(args.get()) });
                    } },
                { "pickle", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("pickle", aArgs, aKwargs, { "ob_type", "pickle_function", "constructor_ob" }, 2u, 0u, a))
                            return nullptr;
                        if (!is_callable(a[1]))
                            return error("TypeError", "reduction functions must be callable");
                        ref const table{ module_attribute("copyreg", "dispatch_table") };
                        if (!table || dict_set(table.get(), a[0], a[1]) < 0)
                            return nullptr;
                        if (a[2] != nullptr && !is_callable(a[2]))
                            return error("TypeError", "constructors must be callable");
                        return new_reference(none());
                    } },
                { "constructor", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("constructor", aArgs, aKwargs, { "object" }, 1u, 0u, a))
                            return nullptr;
                        if (!is_callable(a[0]))
                            return error("TypeError", "constructors must be callable");
                        return new_reference(none());
                    } }
            };

            bool copyreg_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sCopyregFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("dispatch_table", make_dict());
                aAttributes.emplace_back("_extension_registry", make_dict());
                aAttributes.emplace_back("_inverted_registry", make_dict());
                aAttributes.emplace_back("_extension_cache", make_dict());
                aAttributes.emplace_back("__all__", make_list({ make_str("pickle"), make_str("constructor"), make_str("add_extension"), make_str("remove_extension"),
                    make_str("clear_extension_cache") }));
                return true;
            }

            // --- platform

            struct uname_values
            {
                std::string system, node, release, version, machine;
            };

            uname_values platform_uname()
            {
                uname_values u;
#if defined(_WIN32)
                u.system = "Windows";
                char name[256] = {};
                DWORD size = sizeof(name);
                if (::GetComputerNameA(name, &size))
                    u.node = name;
                using rtl_get_version = LONG (WINAPI*)(PRTL_OSVERSIONINFOW);
                RTL_OSVERSIONINFOW info{ sizeof(info) };
                if (auto const ntdll = ::GetModuleHandleW(L"ntdll.dll"))
                    if (auto const get = reinterpret_cast<rtl_get_version>(::GetProcAddress(ntdll, "RtlGetVersion")))
                        get(&info);
                u.release = info.dwMajorVersion >= 10 ? (info.dwBuildNumber >= 22000 ? "11" : "10") : std::to_string(info.dwMajorVersion);
                u.version = std::to_string(info.dwMajorVersion) + "." + std::to_string(info.dwMinorVersion) + "." + std::to_string(info.dwBuildNumber);
                SYSTEM_INFO si{};
                ::GetNativeSystemInfo(&si);
                u.machine = si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "AMD64" : si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "ARM64" : "x86";
#else
                struct utsname n{};
                if (::uname(&n) == 0)
                {
                    u.system = n.sysname;
                    u.node = n.nodename;
                    u.release = n.release;
                    u.version = n.version;
                    u.machine = n.machine;
                }
#endif
                return u;
            }

            std::string libc_version()
            {
#if defined(__GLIBC__)
                return ::gnu_get_libc_version();
#else
                return {};
#endif
            }

            object* uname_result()
            {
                static object* sResult = nullptr;
                if (sResult == nullptr)
                {
                    auto const u = platform_uname();
                    auto const& t = structseq_type("platform.uname_result", { "system", "node", "release", "version", "machine", "processor" }, 6u);
#if defined(_WIN32)
                    std::string const processor = u.machine;
#else
                    std::string const processor = u.machine;
#endif
                    sResult = make_structseq(t, { make_str(u.system), make_str(u.node), make_str(u.release), make_str(u.version), make_str(u.machine), make_str(processor) });
                }
                return new_reference(sResult);
            }

            std::string uname_field(char const* aField)
            {
                ref const u{ uname_result() };
                ref const v{ getattr(u.get(), aField) };
                return v ? text_of(v.get()) : std::string{};
            }

            std::string platform_text(bool aAliased, bool aTerse)
            {
                auto const u = platform_uname();
#if defined(_WIN32)
                (void)aAliased;
                if (aTerse)
                    return u.system + "-" + u.release;
                return u.system + "-" + u.release + "-" + u.version + "-SP0";
#else
                std::string result = u.system + "-" + u.release + "-" + u.machine;
                if (aTerse)
                    result = u.system + "-" + u.release;
                (void)aAliased;
                if (!aTerse && u.system == "Linux")
                {
                    auto const libc = libc_version();
                    if (!libc.empty())
                        result += "-with-glibc" + libc;
                }
                return result;
#endif
            }

            builtin_entry const sPlatformFunctions[] =
            {
                { "uname", [](std::vector<object*> const&, object*) -> object* { return uname_result(); } },
                { "system", [](std::vector<object*> const&, object*) -> object* { return make_str(uname_field("system")); } },
                { "node", [](std::vector<object*> const&, object*) -> object* { return make_str(uname_field("node")); } },
                { "release", [](std::vector<object*> const&, object*) -> object* { return make_str(uname_field("release")); } },
                { "version", [](std::vector<object*> const&, object*) -> object* { return make_str(uname_field("version")); } },
                { "machine", [](std::vector<object*> const&, object*) -> object* { return make_str(uname_field("machine")); } },
                { "processor", [](std::vector<object*> const&, object*) -> object* { return make_str(uname_field("processor")); } },
                { "platform", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("platform", aArgs, aKwargs, { "aliased", "terse" }, 0u, 0u, a))
                            return nullptr;
                        return make_str(platform_text(a[0] != nullptr && truth(a[0]) == 1, a[1] != nullptr && truth(a[1]) == 1));
                    } },
                { "architecture", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("architecture", aArgs, aKwargs, { "executable", "bits", "linkage" }, 0u, 0u, a))
                            return nullptr;
#if defined(_WIN32)
                        return make_tuple({ make_str(sizeof(void*) == 8 ? "64bit" : "32bit"), make_str("WindowsPE") });
#elif defined(__APPLE__)
                        return make_tuple({ make_str(sizeof(void*) == 8 ? "64bit" : "32bit"), make_str("") });
#else
                        return make_tuple({ make_str(sizeof(void*) == 8 ? "64bit" : "32bit"), make_str("ELF") });
#endif
                    } },
                { "python_version", [](std::vector<object*> const&, object*) -> object* { return make_str("3.13.0"); } },
                { "python_version_tuple", [](std::vector<object*> const&, object*) -> object* { return make_tuple({ make_str("3"), make_str("13"), make_str("0") }); } },
                { "python_implementation", [](std::vector<object*> const&, object*) -> object* { return make_str("neos"); } },
                { "python_branch", [](std::vector<object*> const&, object*) -> object* { return make_str(""); } },
                { "python_revision", [](std::vector<object*> const&, object*) -> object* { return make_str(""); } },
                { "python_build", [](std::vector<object*> const&, object*) -> object* { return make_tuple({ make_str("main"), make_str(__DATE__ " " __TIME__) }); } },
                { "python_compiler", [](std::vector<object*> const&, object*) -> object*
                    {
#if defined(_MSC_VER)
                        return make_str("MSC v." + std::to_string(_MSC_VER) + " 64 bit (AMD64)");
#elif defined(__clang__)
                        return make_str("Clang " __clang_version__);
#elif defined(__GNUC__)
                        return make_str("GCC " __VERSION__);
#else
                        return make_str("");
#endif
                    } },
                { "libc_ver", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("libc_ver", aArgs, aKwargs, { "executable", "lib", "version", "chunksize" }, 0u, 0u, a))
                            return nullptr;
                        auto const v = libc_version();
                        return v.empty() ? make_tuple({ make_str(""), make_str("") }) : make_tuple({ make_str("glibc"), make_str(v) });
                    } },
                { "mac_ver", [](std::vector<object*> const&, object*) -> object*
                    {
                        return make_tuple({ make_str(""), make_tuple({ make_str(""), make_str(""), make_str("") }), make_str("") });
                    } },
                { "win32_ver", [](std::vector<object*> const&, object*) -> object*
                    {
#if defined(_WIN32)
                        auto const u = platform_uname();
                        return make_tuple({ make_str(u.release), make_str(u.version), make_str("SP0"), make_str("Multiprocessor Free") });
#else
                        return make_tuple({ make_str(""), make_str(""), make_str(""), make_str("") });
#endif
                    } },
                { "win32_edition", [](std::vector<object*> const&, object*) -> object* { return new_reference(none()); } },
                { "win32_is_iot", [](std::vector<object*> const&, object*) -> object* { return make_bool(false); } },
                { "java_ver", [](std::vector<object*> const&, object*) -> object*
                    {
                        return make_tuple({ make_str(""), make_str(""), make_tuple({ make_str(""), make_str(""), make_str("") }),
                            make_tuple({ make_str(""), make_str(""), make_str("") }) });
                    } },
                { "freedesktop_os_release", [](std::vector<object*> const&, object*) -> object*
                    {
                        // (/etc/os-release (or /usr/lib/os-release): its NAME=value lines)
                        for (auto path : { "/etc/os-release", "/usr/lib/os-release" })
                            if (std::FILE* f = std::fopen(path, "r"))
                            {
                                ref const result{ make_dict() };
                                for (auto const& [k, v] : { std::pair{ "NAME", "Linux" }, std::pair{ "ID", "linux" }, std::pair{ "PRETTY_NAME", "Linux" } })
                                {
                                    ref const key{ make_str(k) };
                                    ref const value{ make_str(v) };
                                    dict_set(result.get(), key.get(), value.get());
                                }
                                char line[1024];
                                while (std::fgets(line, sizeof(line), f) != nullptr)
                                {
                                    std::string text{ line };
                                    while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                                        text.pop_back();
                                    auto const eq = text.find('=');
                                    if (text.empty() || text[0] == '#' || eq == std::string::npos)
                                        continue;
                                    auto value = text.substr(eq + 1u);
                                    if (value.size() >= 2u && (value.front() == '"' || value.front() == '\'') && value.back() == value.front())
                                        value = value.substr(1u, value.size() - 2u);
                                    ref const key{ make_str(text.substr(0u, eq)) };
                                    ref const v{ make_str(value) };
                                    dict_set(result.get(), key.get(), v.get());
                                }
                                std::fclose(f);
                                return new_reference(result.get());
                            }
                        return raise_os_error(ENOENT, ref{ make_str("/etc/os-release") }.get());
                    } }
            };

            bool platform_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sPlatformFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                return true;
            }

            // --- secrets

            bool random_bytes(std::size_t aCount, std::string& aResult)
            {
                ref const urandom{ module_attribute("os", "urandom") };
                ref const data{ urandom ? invoke(urandom.get(), { make_int(static_cast<long long>(aCount)) }) : nullptr };
                std::string_view bytes;
                if (!data || !bytes_like(data.get(), bytes))
                    return false;
                aResult = std::string{ bytes };
                return true;
            }

            bool token_count(std::vector<object*> const& aArgs, object* aKwargs, char const* aFunction, long long& aCount)
            {
                std::vector<object*> a;
                if (!parse_arguments(aFunction, aArgs, aKwargs, { "nbytes" }, 0u, 0u, a))
                    return false;
                aCount = 32;
                if (a[0] != nullptr && kind_of(a[0]) != NEOS_PY_NONE && !to_index(a[0], aCount))
                    return false;
                if (aCount < 0)
                    return (error("ValueError", "negative argument not allowed"), false);
                return true;
            }

            object* system_random()
            {
                static object* sRandom = nullptr;
                if (sRandom == nullptr)
                {
                    ref const cls{ module_attribute("random", "SystemRandom") };
                    sRandom = cls ? invoke(cls.get(), {}) : nullptr;
                }
                return sRandom != nullptr ? new_reference(sRandom) : nullptr;
            }

            builtin_entry const sSecretsFunctions[] =
            {
                { "token_bytes", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        long long n = 0;
                        std::string data;
                        if (!token_count(aArgs, aKwargs, "token_bytes", n) || !random_bytes(static_cast<std::size_t>(n), data))
                            return nullptr;
                        return make_bytes(std::move(data));
                    } },
                { "token_hex", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        long long n = 0;
                        std::string data;
                        if (!token_count(aArgs, aKwargs, "token_hex", n) || !random_bytes(static_cast<std::size_t>(n), data))
                            return nullptr;
                        static char const sDigits[] = "0123456789abcdef";
                        std::string text;
                        for (unsigned char c : data)
                        {
                            text += sDigits[c >> 4];
                            text += sDigits[c & 0xF];
                        }
                        return make_str(std::move(text));
                    } },
                { "token_urlsafe", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        long long n = 0;
                        std::string data;
                        if (!token_count(aArgs, aKwargs, "token_urlsafe", n) || !random_bytes(static_cast<std::size_t>(n), data))
                            return nullptr;
                        // (base64.urlsafe_b64encode without its padding)
                        static char const sAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
                        std::string text;
                        std::size_t i = 0u;
                        for (; i + 2u < data.size(); i += 3u)
                        {
                            auto const v = (static_cast<unsigned>(static_cast<unsigned char>(data[i])) << 16) | (static_cast<unsigned>(static_cast<unsigned char>(data[i + 1u])) << 8) |
                                static_cast<unsigned char>(data[i + 2u]);
                            text += sAlphabet[(v >> 18) & 63];
                            text += sAlphabet[(v >> 12) & 63];
                            text += sAlphabet[(v >> 6) & 63];
                            text += sAlphabet[v & 63];
                        }
                        if (data.size() - i == 1u)
                        {
                            auto const v = static_cast<unsigned>(static_cast<unsigned char>(data[i])) << 16;
                            text += sAlphabet[(v >> 18) & 63];
                            text += sAlphabet[(v >> 12) & 63];
                        }
                        else if (data.size() - i == 2u)
                        {
                            auto const v = (static_cast<unsigned>(static_cast<unsigned char>(data[i])) << 16) | (static_cast<unsigned>(static_cast<unsigned char>(data[i + 1u])) << 8);
                            text += sAlphabet[(v >> 18) & 63];
                            text += sAlphabet[(v >> 12) & 63];
                            text += sAlphabet[(v >> 6) & 63];
                        }
                        return make_str(std::move(text));
                    } },
                { "randbelow", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("randbelow", aArgs, aKwargs, { "exclusive_upper_bound" }, 1u, 0u, a))
                            return nullptr;
                        ref const zero{ make_int(0) };
                        ref const positive{ compare(NEOS_PY_GT, a[0], zero.get()) };
                        if (!positive)
                            return nullptr;
                        if (truth(positive.get()) != 1)
                            return error("ValueError", "Upper bound must be positive.");
                        ref const random{ system_random() };
                        return random ? call_method_named(random.get(), "randrange", { new_reference(a[0]) }) : nullptr;
                    } },
                { "randbits", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("randbits", aArgs, aKwargs, { "k" }, 1u, 0u, a))
                            return nullptr;
                        ref const random{ system_random() };
                        return random ? call_method_named(random.get(), "getrandbits", { new_reference(a[0]) }) : nullptr;
                    } },
                { "choice", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("choice", aArgs, aKwargs, { "seq" }, 1u, 0u, a))
                            return nullptr;
                        ref const random{ system_random() };
                        return random ? call_method_named(random.get(), "choice", { new_reference(a[0]) }) : nullptr;
                    } },
                { "compare_digest", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("compare_digest", aArgs, aKwargs, { "a", "b" }, 2u, 2u, a))
                            return nullptr;
                        std::string_view x, y;
                        auto const sx = str_of_value(a[0]);
                        auto const sy = str_of_value(a[1]);
                        if (sx != nullptr && sy != nullptr)
                        {
                            // (str: ASCII only)
                            for (auto const* t : { sx, sy })
                                for (unsigned char c : *t)
                                    if (c >= 0x80u)
                                        return error("TypeError", "comparing strings with non-ASCII characters is not supported");
                            x = *sx;
                            y = *sy;
                        }
                        else if (!bytes_like(a[0], x) || !bytes_like(a[1], y))
                            return error("TypeError", std::string{ "unsupported operand types(s) or combination of types: '" } + type_name(a[0]) + "' and '" + type_name(a[1]) + "'");
                        // (in constant time: every byte is compared)
                        unsigned result = x.size() == y.size() ? 0u : 1u;
                        for (std::size_t i = 0u; i < x.size(); ++i)
                            result |= static_cast<unsigned char>(x[i]) ^ static_cast<unsigned char>(i < y.size() ? y[i] : x[i] ^ 1);
                        return make_bool(result == 0u);
                    } }
            };

            bool secrets_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sSecretsFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("SystemRandom", module_attribute("random", "SystemRandom"));
                aAttributes.emplace_back("DEFAULT_ENTROPY", make_int(32));
                aAttributes.emplace_back("__all__", make_list({ make_str("choice"), make_str("randbelow"), make_str("randbits"), make_str("SystemRandom"),
                    make_str("token_bytes"), make_str("token_hex"), make_str("token_urlsafe"), make_str("compare_digest") }));
                return !failed();
            }

            // --- timeit

            std::string reindent(std::string const& aSource, std::size_t aIndent)
            {
                std::string result;
                for (auto c : aSource)
                {
                    result += c;
                    if (c == '\n')
                        result += std::string(aIndent, ' ');
                }
                return result;
            }

            // (Timer(stmt='pass', setup='pass', timer=default_timer, globals=None): callables are timed directly; statements are compiled
            // (into the template's function inner))
            builtin_entry const sTimerMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("Timer", aArgs, aKwargs, { "stmt", "setup", "timer", "globals" }, 0u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        ref const timer{ a[2] != nullptr ? new_reference(a[2]) : module_attribute("time", "perf_counter") };
                        ref const stmt{ a[0] != nullptr ? new_reference(a[0]) : make_str("pass") };
                        ref const setup{ a[1] != nullptr ? new_reference(a[1]) : make_str("pass") };
                        if (!timer || setattr(self, "timer", timer.get()) < 0)
                            return nullptr;
                        auto const stmtText = str_of_value(stmt.get());
                        auto const setupText = str_of_value(setup.get());
                        if (setupText == nullptr && !is_callable(setup.get()))
                            return error("ValueError", "setup is neither a string nor callable");
                        if (stmtText == nullptr && !is_callable(stmt.get()))
                            return error("ValueError", "stmt is neither a string nor callable");
                        if (stmtText == nullptr && setupText == nullptr)
                        {
                            // (both callables: no source)
                            if (setattr(self, "_stmt", stmt.get()) < 0 || setattr(self, "_setup", setup.get()) < 0 || setattr(self, "src", none()) < 0)
                                return nullptr;
                            return new_reference(none());
                        }
                        ref const localNs{ make_dict() };
                        std::string init, setupSource, stmtSource, prefix;
                        if (setupText != nullptr)
                        {
                            prefix = *setupText + "\n";
                            setupSource = reindent(*setupText, 4u);
                        }
                        else
                        {
                            dict_set(localNs.get(), ref{ make_str("_setup") }.get(), setup.get());
                            init += ", _setup=_setup";
                            setupSource = "_setup()";
                        }
                        if (stmtText != nullptr)
                            stmtSource = reindent(*stmtText, 8u);
                        else
                        {
                            dict_set(localNs.get(), ref{ make_str("_stmt") }.get(), stmt.get());
                            init += ", _stmt=_stmt";
                            stmtSource = "_stmt()";
                        }
                        // (the statements are checked: they compile on their own)
                        if (setupText != nullptr)
                        {
                            ref const checked{ b_compile({ setup.get(), ref{ make_str("<timeit-src>") }.get(), ref{ make_str("exec") }.get() }, nullptr) };
                            if (!checked)
                                return nullptr;
                        }
                        if (stmtText != nullptr)
                        {
                            ref const source{ make_str(prefix + *stmtText) };
                            ref const checked{ b_compile({ source.get(), ref{ make_str("<timeit-src>") }.get(), ref{ make_str("exec") }.get() }, nullptr) };
                            if (!checked)
                                return nullptr;
                        }
                        std::string const src = "\ndef inner(_it, _timer" + init + "):\n    " + setupSource + "\n    _t0 = _timer()\n    for _i in _it:\n        " + stmtSource +
                            "\n        pass\n    _t1 = _timer()\n    return _t1 - _t0\n";
                        ref const srcValue{ make_str(src) };
                        if (setattr(self, "src", srcValue.get()) < 0)
                            return nullptr;
                        ref const globalNs{ a[3] != nullptr && kind_of(a[3]) != NEOS_PY_NONE ? new_reference(a[3]) : make_dict() };
                        ref const executed{ b_exec({ srcValue.get(), globalNs.get(), localNs.get() }, nullptr) };
                        if (!executed)
                            return nullptr;
                        ref const key{ make_str("inner") };
                        auto const at = dict_find(localNs.get(), key.get());
                        if (at < 0)
                            return error("SystemError", "timeit: the template did not define inner");
                        if (setattr(self, "inner", as<dict_object>(localNs.get())->items[static_cast<std::size_t>(at)].second) < 0)
                            return nullptr;
                        return new_reference(none());
                    } },
                { "timeit", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("timeit", aArgs, aKwargs, { "number" }, 0u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        long long number = 1000000;
                        if (a[0] != nullptr && !to_index(a[0], number))
                            return nullptr;
                        ref const timer{ getattr(self, "timer") };
                        if (!timer)
                            return nullptr;
                        bool const wasEnabled = sGcEnabled;
                        sGcEnabled = false;
                        ref result;
                        if (ref const stmt{ getattr(self, "_stmt") })
                        {
                            // (callables: called directly)
                            ref const setup{ getattr(self, "_setup") };
                            ref const s{ setup ? invoke(setup.get(), {}) : nullptr };
                            ref const t0{ s ? invoke(timer.get(), {}) : nullptr };
                            bool ok = static_cast<bool>(t0);
                            for (long long i = 0; ok && i < number; ++i)
                                ok = static_cast<bool>(ref{ invoke(stmt.get(), {}) });
                            ref const t1{ ok ? invoke(timer.get(), {}) : nullptr };
                            if (t1)
                                result = ref{ binary(NEOS_PY_SUBTRACT, t1.get(), t0.get(), false) };
                        }
                        else
                        {
                            clear_error();
                            ref const inner{ getattr(self, "inner") };
                            ref const repeat{ module_attribute("itertools", "repeat") };
                            ref const it{ repeat ? invoke(repeat.get(), { new_reference(none()), make_int(number) }) : nullptr };
                            if (inner && it)
                                result = ref{ invoke(inner.get(), { new_reference(it.get()), new_reference(timer.get()) }) };
                        }
                        sGcEnabled = wasEnabled;
                        return result.release_ownership();
                    } },
                { "repeat", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("repeat", aArgs, aKwargs, { "repeat", "number" }, 0u, 0u, a))
                            return nullptr;
                        long long count = 5;
                        if (a[0] != nullptr && !to_index(a[0], count))
                            return nullptr;
                        std::vector<object*> results;
                        for (long long i = 0; i < count; ++i)
                        {
                            auto const t = call_method_named(aArgs[0], "timeit", { a[1] != nullptr ? new_reference(a[1]) : make_int(1000000) });
                            if (t == nullptr)
                            {
                                for (auto r : results)
                                    release(r);
                                return nullptr;
                            }
                            results.push_back(t);
                        }
                        return make_list(std::move(results));
                    } },
                { "autorange", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("autorange", aArgs, aKwargs, { "callback" }, 0u, 0u, a))
                            return nullptr;
                        for (long long i = 1;; i *= 10)
                            for (long long j : { 1, 2, 5 })
                            {
                                auto const number = i * j;
                                ref const taken{ call_method_named(aArgs[0], "timeit", { make_int(number) }) };
                                if (!taken)
                                    return nullptr;
                                if (a[0] != nullptr && truth(a[0]) == 1)
                                {
                                    ref const r{ invoke(a[0], { make_int(number), new_reference(taken.get()) }) };
                                    if (!r)
                                        return nullptr;
                                }
                                double seconds = 0.0;
                                if (!to_double(taken.get(), seconds))
                                    return nullptr;
                                if (seconds >= 0.2)
                                    return make_tuple({ make_int(number), new_reference(taken.get()) });
                            }
                    } },
                { "print_exc", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("print_exc", aArgs, aKwargs, { "file" }, 0u, 0u, a))
                            return nullptr;
                        ref const printExc{ module_attribute("traceback", "print_exc") };
                        if (!printExc)
                            return nullptr;
                        ref const kw{ keywords({ { "file", a[0] } }) };
                        return invoke(printExc.get(), {}, kw.get());
                    } }
            };

            // (timeit(stmt, setup, timer, number, globals) and repeat(stmt, setup, timer, repeat, number, globals))
            object* timer_of(object* aStmt, object* aSetup, object* aTimer, object* aGlobals)
            {
                ref const cls{ module_attribute("timeit", "Timer") };
                ref const kw{ keywords({ { "stmt", aStmt }, { "setup", aSetup }, { "timer", aTimer }, { "globals", aGlobals } }) };
                return cls ? call(cls.get(), nullptr, kw.get()) : nullptr;
            }

            builtin_entry const sTimeitFunctions[] =
            {
                { "timeit", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("timeit", aArgs, aKwargs, { "stmt", "setup", "timer", "number", "globals" }, 0u, 0u, a))
                            return nullptr;
                        ref const t{ timer_of(a[0], a[1], a[2], a[4]) };
                        return t ? call_method_named(t.get(), "timeit", { a[3] != nullptr ? new_reference(a[3]) : make_int(1000000) }) : nullptr;
                    } },
                { "repeat", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("repeat", aArgs, aKwargs, { "stmt", "setup", "timer", "repeat", "number", "globals" }, 0u, 0u, a))
                            return nullptr;
                        ref const t{ timer_of(a[0], a[1], a[2], a[5]) };
                        return t ? call_method_named(t.get(), "repeat", { a[3] != nullptr ? new_reference(a[3]) : make_int(5),
                            a[4] != nullptr ? new_reference(a[4]) : make_int(1000000) }) : nullptr;
                    } }
            };

            bool timeit_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                aAttributes.emplace_back("Timer", make_class_of("timeit", "Timer", {}, sTimerMethods));
                for (auto const& f : sTimeitFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("default_timer", module_attribute("time", "perf_counter"));
                aAttributes.emplace_back("default_number", make_int(1000000));
                aAttributes.emplace_back("default_repeat", make_int(5));
                aAttributes.emplace_back("dummy_src_name", make_str("<timeit-src>"));
                aAttributes.emplace_back("__all__", make_list({ make_str("Timer"), make_str("timeit"), make_str("repeat"), make_str("default_timer") }));
                return !failed();
            }

            // --- signal: Python handlers of signals are called (in the main thread) when the program next checks for them (calls of
            // functions, iteration)

#if defined(_WIN32)
            constexpr int kSignalCount = 23;
#else
            constexpr int kSignalCount = NSIG;
#endif
            std::atomic<bool> sAnyPending{ false };
            std::atomic<bool> sPending[kSignalCount];
            object* sHandlers[kSignalCount] = {};       ///< (the Python handlers (SIG_DFL, SIG_IGN or callables): null until the module is imported)
            object* sHandlersEnum = nullptr;
            object* sSignalsEnum = nullptr;
            object* sDefaultIntHandler = nullptr;
            std::thread::id sMainThread = std::this_thread::get_id();
            bool sSignalModule = false;

            void neos_python_signal(int aSignal)
            {
                if (aSignal > 0 && aSignal < kSignalCount)
                {
                    sPending[aSignal].store(true);
                    sAnyPending.store(true);
                }
#if defined(_WIN32)
                std::signal(aSignal, neos_python_signal);   // (Windows resets the handler)
#endif
            }

            object* signal_member(object* aEnum, long long aValue)
            {
                if (aEnum == nullptr)
                    return make_int(aValue);
                ref const value{ make_int(aValue) };
                ref member{ invoke(aEnum, { new_reference(value.get()) }) };
                if (!member)
                {
                    clear_error();
                    return make_int(aValue);
                }
                return member.release_ownership();
            }

            bool signal_number(object* aValue, int& aSignal)
            {
                long long n = 0;
                if (!to_index(aValue, n))
                    return false;
                if (n < 1 || n >= kSignalCount)
                    return (error("ValueError", "signal number out of range"), false);
                aSignal = static_cast<int>(n);
                return true;
            }

            std::vector<std::pair<char const*, int>> signal_names()
            {
                std::vector<std::pair<char const*, int>> names = {
#if defined(SIGABRT)
                    { "SIGABRT", SIGABRT },
#endif
#if defined(SIGFPE)
                    { "SIGFPE", SIGFPE },
#endif
#if defined(SIGILL)
                    { "SIGILL", SIGILL },
#endif
#if defined(SIGINT)
                    { "SIGINT", SIGINT },
#endif
#if defined(SIGSEGV)
                    { "SIGSEGV", SIGSEGV },
#endif
#if defined(SIGTERM)
                    { "SIGTERM", SIGTERM },
#endif
#if defined(_WIN32)
                    { "SIGBREAK", SIGBREAK },
#else
                    { "SIGALRM", SIGALRM }, { "SIGBUS", SIGBUS }, { "SIGCHLD", SIGCHLD }, { "SIGCONT", SIGCONT }, { "SIGHUP", SIGHUP }, { "SIGIO", SIGIO },
                    { "SIGIOT", SIGIOT }, { "SIGKILL", SIGKILL }, { "SIGPIPE", SIGPIPE }, { "SIGPROF", SIGPROF }, { "SIGQUIT", SIGQUIT }, { "SIGSTOP", SIGSTOP },
                    { "SIGSYS", SIGSYS }, { "SIGTRAP", SIGTRAP }, { "SIGTSTP", SIGTSTP }, { "SIGTTIN", SIGTTIN }, { "SIGTTOU", SIGTTOU }, { "SIGURG", SIGURG },
                    { "SIGUSR1", SIGUSR1 }, { "SIGUSR2", SIGUSR2 }, { "SIGVTALRM", SIGVTALRM }, { "SIGWINCH", SIGWINCH }, { "SIGXCPU", SIGXCPU }, { "SIGXFSZ", SIGXFSZ },
#if defined(__linux__)
                    { "SIGCLD", SIGCHLD }, { "SIGPOLL", SIGIO }, { "SIGPWR", SIGPWR }, { "SIGSTKFLT", SIGSTKFLT }, { "SIGRTMIN", SIGRTMIN }, { "SIGRTMAX", SIGRTMAX },
#endif
#if defined(__APPLE__)
                    { "SIGEMT", SIGEMT }, { "SIGINFO", SIGINFO },
#endif
#endif
                };
                // (by value, then name: the first of the same value is the member, the others its aliases)
                std::sort(names.begin(), names.end(), [](auto const& a, auto const& b) { return a.second < b.second || (a.second == b.second && std::strcmp(a.first, b.first) < 0); });
                return names;
            }

            // (the OS's handler of a signal: Python's (SIG_DFL, SIG_IGN or the C handler that makes it pending))
            bool set_os_handler(int aSignal, object* aHandler)
            {
                long long n = -1;
                if (is_int_like(builtin_value(aHandler)))
                    to_index(builtin_value(aHandler), n);
#if defined(_WIN32)
                auto const previous = std::signal(aSignal, n == 0 ? SIG_DFL : n == 1 ? SIG_IGN : neos_python_signal);
                if (previous == SIG_ERR)
                    return (raise_os_error(EINVAL), false);
#else
                struct sigaction action{};
                action.sa_handler = n == 0 ? SIG_DFL : n == 1 ? SIG_IGN : neos_python_signal;
                sigemptyset(&action.sa_mask);
                action.sa_flags = SA_ONSTACK;
                if (::sigaction(aSignal, &action, nullptr) != 0)
                    return (raise_os_error(errno), false);
#endif
                return true;
            }

            builtin_entry const sSignalFunctions[] =
            {
                { "signal", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        int signal = 0;
                        if (!parse_arguments("signal", aArgs, aKwargs, { "signalnum", "handler" }, 2u, 2u, a) || !signal_number(a[0], signal))
                            return nullptr;
                        if (std::this_thread::get_id() != sMainThread)
                            return error("ValueError", "signal only works in main thread of the main interpreter");
                        auto const handler = a[1];
                        bool const special = is_int_like(builtin_value(handler));
                        long long n = -1;
                        if (special)
                            to_index(builtin_value(handler), n);
                        if (!special && !is_callable(handler))
                            return error("TypeError", "signal handler must be signal.SIG_IGN, signal.SIG_DFL, or a callable object");
                        if (special && n != 0 && n != 1)
                            return error("TypeError", "signal handler must be signal.SIG_IGN, signal.SIG_DFL, or a callable object");
#if !defined(_WIN32)
                        if (signal == SIGKILL || signal == SIGSTOP)
                            return raise_os_error(EINVAL);
#endif
                        if (!set_os_handler(signal, handler))
                            return nullptr;
                        auto const previous = sHandlers[signal];
                        sHandlers[signal] = special ? signal_member(sHandlersEnum, n) : new_reference(handler);
                        return previous != nullptr ? previous : new_reference(none());
                    } },
                { "getsignal", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        int signal = 0;
                        if (!parse_arguments("getsignal", aArgs, aKwargs, { "signalnum" }, 1u, 1u, a) || !signal_number(a[0], signal))
                            return nullptr;
                        return new_reference(sHandlers[signal] != nullptr ? sHandlers[signal] : none());
                    } },
                { "default_int_handler", [](std::vector<object*> const&, object*) -> object* { return error("KeyboardInterrupt", ""); } },
                { "raise_signal", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        int signal = 0;
                        if (!parse_arguments("raise_signal", aArgs, aKwargs, { "signalnum" }, 1u, 1u, a) || !signal_number(a[0], signal))
                            return nullptr;
                        if (std::raise(signal) != 0)
                            return raise_os_error(errno);
                        return signals_check() ? new_reference(none()) : nullptr;
                    } },
                { "strsignal", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        int signal = 0;
                        if (!parse_arguments("strsignal", aArgs, aKwargs, { "signalnum" }, 1u, 1u, a) || !signal_number(a[0], signal))
                            return nullptr;
#if defined(_WIN32)
                        static std::map<int, char const*> const sDescriptions = { { SIGINT, "Interrupt" }, { SIGILL, "Illegal instruction" },
                            { SIGFPE, "Floating-point exception" }, { SIGSEGV, "Segmentation fault" }, { SIGTERM, "Terminated" }, { SIGABRT, "Aborted" } };
                        auto const d = sDescriptions.find(signal);
                        return d == sDescriptions.end() ? new_reference(none()) : make_str(d->second);
#else
                        auto const text = ::strsignal(signal);
                        return text != nullptr ? make_str(text) : new_reference(none());
#endif
                    } },
                { "valid_signals", [](std::vector<object*> const&, object*) -> object*
                    {
                        ref const result{ fn_set() };
#if defined(_WIN32)
                        for (auto const& n : signal_names())
                            fn_set_add(result.get(), ref{ signal_member(sSignalsEnum, n.second) }.get());
#else
                        for (int i = 1; i < kSignalCount; ++i)
                        {
                            if (i == 32 || i == 33)
                                continue;   // (glibc's own)
                            fn_set_add(result.get(), ref{ signal_member(sSignalsEnum, i) }.get());
                        }
#endif
                        return new_reference(result.get());
                    } },
                { "set_wakeup_fd", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("set_wakeup_fd", aArgs, aKwargs, { "fd", "warn_on_full_buffer" }, 1u, 1u, a))
                            return nullptr;
                        static long long sWakeup = -1;
                        long long fd = 0;
                        if (!to_index(a[0], fd))
                            return nullptr;
                        auto const previous = sWakeup;
                        sWakeup = fd;
                        return make_int(previous);
                    } },
#if !defined(_WIN32)
                { "alarm", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long seconds = 0;
                        if (!parse_arguments("alarm", aArgs, aKwargs, { "seconds" }, 1u, 1u, a) || !to_index(a[0], seconds))
                            return nullptr;
                        return make_int(::alarm(static_cast<unsigned>(seconds)));
                    } },
                { "pause", [](std::vector<object*> const&, object*) -> object*
                    {
                        gil_release();
                        ::pause();
                        gil_acquire();
                        return signals_check() ? new_reference(none()) : nullptr;
                    } },
                { "siginterrupt", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        int signal = 0;
                        if (!parse_arguments("siginterrupt", aArgs, aKwargs, { "signalnum", "flag" }, 2u, 2u, a) || !signal_number(a[0], signal))
                            return nullptr;
                        struct sigaction action{};
                        if (::sigaction(signal, nullptr, &action) != 0)
                            return raise_os_error(errno);
                        if (truth(a[1]) == 1)
                            action.sa_flags &= ~SA_RESTART;
                        else
                            action.sa_flags |= SA_RESTART;
                        if (::sigaction(signal, &action, nullptr) != 0)
                            return raise_os_error(errno);
                        return new_reference(none());
                    } },
                { "setitimer", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long which = 0;
                        double seconds = 0.0, interval = 0.0;
                        if (!parse_arguments("setitimer", aArgs, aKwargs, { "which", "seconds", "interval" }, 2u, 3u, a) || !to_index(a[0], which) ||
                            !to_double(a[1], seconds) || (a[2] != nullptr && !to_double(a[2], interval)))
                            return nullptr;
                        auto const timeval_of = [](double aSeconds) { struct timeval t{}; t.tv_sec = static_cast<time_t>(aSeconds); t.tv_usec = static_cast<suseconds_t>((aSeconds - static_cast<double>(t.tv_sec)) * 1e6); return t; };
                        struct itimerval value{ timeval_of(interval), timeval_of(seconds) }, old{};
                        if (::setitimer(static_cast<int>(which), &value, &old) != 0)
                        {
                            ref const cls{ module_exception("signal.itimer_error", { "OSError" }) };
                            ref const e{ invoke(cls.get(), { make_int(errno), make_str(std::strerror(errno)) }) };
                            if (e)
                                raise_exception(e.get());
                            return nullptr;
                        }
                        auto const seconds_of = [](struct timeval const& t) { return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_usec) / 1e6; };
                        return make_tuple({ make_float(seconds_of(old.it_value)), make_float(seconds_of(old.it_interval)) });
                    } },
                { "getitimer", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long which = 0;
                        if (!parse_arguments("getitimer", aArgs, aKwargs, { "which" }, 1u, 1u, a) || !to_index(a[0], which))
                            return nullptr;
                        struct itimerval value{};
                        if (::getitimer(static_cast<int>(which), &value) != 0)
                            return raise_os_error(errno);
                        auto const seconds_of = [](struct timeval const& t) { return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_usec) / 1e6; };
                        return make_tuple({ make_float(seconds_of(value.it_value)), make_float(seconds_of(value.it_interval)) });
                    } },
                { "pthread_kill", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long thread = 0, signal = 0;
                        if (!parse_arguments("pthread_kill", aArgs, aKwargs, { "thread_id", "signalnum" }, 2u, 2u, a) || !to_index(a[0], thread) || !to_index(a[1], signal))
                            return nullptr;
                        if (auto const e = ::pthread_kill(static_cast<pthread_t>(thread), static_cast<int>(signal)); e != 0)
                            return raise_os_error(e);
                        return signals_check() ? new_reference(none()) : nullptr;
                    } },
                { "pthread_sigmask", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        long long how = 0;
                        if (!parse_arguments("pthread_sigmask", aArgs, aKwargs, { "how", "mask" }, 2u, 2u, a) || !to_index(a[0], how))
                            return nullptr;
                        std::vector<object*> items;
                        if (!collect(a[1], items))
                            return nullptr;
                        sigset_t mask, old;
                        sigemptyset(&mask);
                        for (auto i : items)
                        {
                            ref const item{ i };
                            int signal = 0;
                            if (!signal_number(i, signal))
                                return nullptr;
                            sigaddset(&mask, signal);
                        }
                        if (auto const e = ::pthread_sigmask(static_cast<int>(how), &mask, &old); e != 0)
                            return raise_os_error(e);
                        ref const result{ fn_set() };
                        for (int i = 1; i < kSignalCount; ++i)
                            if (sigismember(&old, i) == 1)
                                fn_set_add(result.get(), ref{ signal_member(sSignalsEnum, i) }.get());
                        return new_reference(result.get());
                    } },
                { "sigpending", [](std::vector<object*> const&, object*) -> object*
                    {
                        sigset_t set;
                        if (::sigpending(&set) != 0)
                            return raise_os_error(errno);
                        ref const result{ fn_set() };
                        for (int i = 1; i < kSignalCount; ++i)
                            if (sigismember(&set, i) == 1)
                                fn_set_add(result.get(), ref{ signal_member(sSignalsEnum, i) }.get());
                        return new_reference(result.get());
                    } },
                { "sigwait", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("sigwait", aArgs, aKwargs, { "sigset" }, 1u, 1u, a))
                            return nullptr;
                        std::vector<object*> items;
                        if (!collect(a[0], items))
                            return nullptr;
                        sigset_t set;
                        sigemptyset(&set);
                        for (auto i : items)
                        {
                            ref const item{ i };
                            int signal = 0;
                            if (!signal_number(i, signal))
                                return nullptr;
                            sigaddset(&set, signal);
                        }
                        int received = 0;
                        gil_release();
                        auto const e = ::sigwait(&set, &received);
                        gil_acquire();
                        if (e != 0)
                            return raise_os_error(e);
                        return signal_member(sSignalsEnum, received);
                    } },
#endif
            };

            bool signal_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                ref const intEnum{ module_attribute("enum", "IntEnum") };
                if (!intEnum)
                    return false;
                ref const moduleKw{ keywords({ { "module", ref{ make_str("signal") }.get() } }) };
                auto const make_enum = [&](char const* aName, std::vector<std::pair<char const*, int>> const& aMembers) -> object*
                {
                    std::vector<object*> pairs;
                    for (auto const& m : aMembers)
                        pairs.push_back(make_tuple({ make_str(m.first), make_int(m.second) }));
                    ref const args{ make_tuple({ make_str(aName), make_list(std::move(pairs)) }) };
                    return call(intEnum.get(), args.get(), moduleKw.get());
                };
                auto const names = signal_names();
                if (sSignalsEnum == nullptr)
                {
                    auto members = names;
#if defined(_WIN32)
                    members.push_back({ "CTRL_C_EVENT", 0 });
                    members.push_back({ "CTRL_BREAK_EVENT", 1 });
#endif
                    sSignalsEnum = make_enum("Signals", members);
                    sHandlersEnum = make_enum("Handlers", { { "SIG_DFL", 0 }, { "SIG_IGN", 1 } });
                    if (sSignalsEnum == nullptr || sHandlersEnum == nullptr)
                        return false;
                    // (the handlers that the program starts with: SIGINT's raises KeyboardInterrupt)
                    static builtin_entry const* sDefaultEntry = nullptr;
                    for (auto const& f : sSignalFunctions)
                        if (std::strcmp(f.name, "default_int_handler") == 0)
                            sDefaultEntry = &f;
                    sDefaultIntHandler = make_builtin(*sDefaultEntry);
                    for (int i = 1; i < kSignalCount; ++i)
                    {
#if defined(_WIN32)
                        auto const current = std::signal(i, SIG_DFL);
                        if (current != SIG_ERR)
                            std::signal(i, current);
                        bool const ignored = current == SIG_IGN;
                        bool const valid = current != SIG_ERR;
#else
                        struct sigaction action{};
                        bool const valid = ::sigaction(i, nullptr, &action) == 0;
                        bool const ignored = valid && action.sa_handler == SIG_IGN;
#endif
                        if (valid)
                            sHandlers[i] = signal_member(sHandlersEnum, ignored ? 1 : 0);
                    }
                    if (sHandlers[SIGINT] != nullptr)
                    {
                        release(sHandlers[SIGINT]);
                        sHandlers[SIGINT] = new_reference(sDefaultIntHandler);
                        set_os_handler(SIGINT, sDefaultIntHandler);
                    }
                    sSignalModule = true;
                }
                for (auto const& n : names)
                    aAttributes.emplace_back(n.first, getattr(sSignalsEnum, n.first));
#if defined(_WIN32)
                aAttributes.emplace_back("CTRL_C_EVENT", getattr(sSignalsEnum, "CTRL_C_EVENT"));
                aAttributes.emplace_back("CTRL_BREAK_EVENT", getattr(sSignalsEnum, "CTRL_BREAK_EVENT"));
#endif
                aAttributes.emplace_back("Signals", new_reference(sSignalsEnum));
                aAttributes.emplace_back("Handlers", new_reference(sHandlersEnum));
                aAttributes.emplace_back("SIG_DFL", getattr(sHandlersEnum, "SIG_DFL"));
                aAttributes.emplace_back("SIG_IGN", getattr(sHandlersEnum, "SIG_IGN"));
#if !defined(_WIN32)
                {
                    ref const sigmasks{ make_enum("Sigmasks", { { "SIG_BLOCK", SIG_BLOCK }, { "SIG_UNBLOCK", SIG_UNBLOCK }, { "SIG_SETMASK", SIG_SETMASK } }) };
                    if (!sigmasks)
                        return false;
                    aAttributes.emplace_back("Sigmasks", new_reference(sigmasks.get()));
                    for (auto n : { "SIG_BLOCK", "SIG_UNBLOCK", "SIG_SETMASK" })
                        aAttributes.emplace_back(n, getattr(sigmasks.get(), n));
                }
                aAttributes.emplace_back("ITIMER_REAL", make_int(ITIMER_REAL));
                aAttributes.emplace_back("ITIMER_VIRTUAL", make_int(ITIMER_VIRTUAL));
                aAttributes.emplace_back("ITIMER_PROF", make_int(ITIMER_PROF));
                aAttributes.emplace_back("ItimerError", module_exception("signal.itimer_error", { "OSError" }));
#endif
                aAttributes.emplace_back("NSIG", make_int(kSignalCount));
                for (auto const& f : sSignalFunctions)
                    aAttributes.emplace_back(f.name, std::strcmp(f.name, "default_int_handler") == 0 ? new_reference(sDefaultIntHandler) : make_builtin(f));
                return !failed();
            }

            inline bool const sKeyword = register_module("keyword", keyword_module);
            inline bool const sSignal = register_module("signal", signal_module);
            inline bool const sPlatform = register_module("platform", platform_module);
            inline bool const sSecrets = register_module("secrets", secrets_module);
            inline bool const sTimeit = register_module("timeit", timeit_module);
            inline bool const sCopyreg = register_module("copyreg", copyreg_module);
            inline bool const sAtexit = register_module("atexit", atexit_module);
            inline bool const sGc = register_module("gc", gc_module);
        }

        // (the signals that the interpreter ignores from its start (Modules/signalmodule.c's signal_install_handlers): writing to a pipe or socket
        // whose other end is closed raises BrokenPipeError instead of ending the program, and exceeding the file size limit raises OSError)
        void signals_start()
        {
#if defined(SIGPIPE)
            std::signal(SIGPIPE, SIG_IGN);
#endif
#if defined(SIGXFZ)
            std::signal(SIGXFZ, SIG_IGN);
#endif
#if defined(SIGXFSZ)
            std::signal(SIGXFSZ, SIG_IGN);
#endif
        }

        // (the signals that have arrived: their Python handlers are called (false: a handler raised an exception))
        bool signals_check()
        {
            if (!sAnyPending.load(std::memory_order_relaxed) || std::this_thread::get_id() != sMainThread)
                return true;
            sAnyPending.store(false);
            for (int i = 1; i < kSignalCount; ++i)
                if (sPending[i].exchange(false))
                {
                    auto const handler = sHandlers[i];
                    if (handler == nullptr || !is_callable(handler) || is_int_like(builtin_value(handler)))
                        continue;
                    ref const keep{ new_reference(handler) };
                    ref const r{ invoke(handler, { make_int(i), new_reference(none()) }) };
                    if (!r)
                    {
                        // (the other pending signals: checked again later)
                        for (int j = i + 1; j < kSignalCount; ++j)
                            if (sPending[j].load())
                                sAnyPending.store(true);
                        return false;
                    }
                }
            return true;
        }

        void run_exit_functions()
        {
            {
                std::lock_guard<std::recursive_mutex> lock{ sExitMutex };
                if (sExiting)
                    return;
                sExiting = true;
            }
            call_exit_functions();
            clear_exit_functions();
        }

        // (the program has ended (without an exception that it did not handle): its threads finish, then its exit functions are called)
        void fn_finish()
        {
            // (once: before the program's globals are released, and at its end)
            static std::atomic<bool> sFinished{ false };
            if (sFinished.exchange(true))
                return;
            threads_end();
            run_exit_functions();
        }
    }
}
