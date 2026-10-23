/*
  asyncio.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/asyncio/__init__.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE).
*/

// The package asyncio and its modules: their registration, the package's objects (made once) and the helpers its parts share (see
// asyncio.hpp).

#include <cstring>
#include "asyncio.hpp"
#include "asyncio__asyncio_docs.hpp"
#include "asyncio_base_events_docs.hpp"
#include "asyncio_base_futures_docs.hpp"
#include "asyncio_base_subprocess_docs.hpp"
#include "asyncio_base_tasks_docs.hpp"
#include "asyncio_constants_docs.hpp"
#include "asyncio_coroutines_docs.hpp"
#include "asyncio_events_docs.hpp"
#include "asyncio_exceptions_docs.hpp"
#include "asyncio_format_helpers_docs.hpp"
#include "asyncio_futures_docs.hpp"
#include "asyncio_locks_docs.hpp"
#include "asyncio_log_docs.hpp"
#include "asyncio_mixins_docs.hpp"
#include "asyncio_protocols_docs.hpp"
#include "asyncio_queues_docs.hpp"
#include "asyncio_runners_docs.hpp"
#include "asyncio_selector_events_docs.hpp"
#include "asyncio_staggered_docs.hpp"
#include "asyncio_streams_docs.hpp"
#include "asyncio_subprocess_docs.hpp"
#include "asyncio_taskgroups_docs.hpp"
#include "asyncio_tasks_docs.hpp"
#include "asyncio_threads_docs.hpp"
#include "asyncio_timeouts_docs.hpp"
#include "asyncio_transports_docs.hpp"
#include "asyncio_trsock_docs.hpp"
#include "asyncio_unix_events_docs.hpp"

namespace neos::python_runtime::detail::asyncio
{
    state& the_state()
    {
        static state* sState = new state{};
        return *sState;
    }

    py object_of(char const* aQualifiedName)
    {
        auto& s = the_state();
        auto const o = s.objects.find(aQualifiedName);
        if (o == s.objects.end())
            py_raise("SystemError", std::string{ "asyncio: no object " } + aQualifiedName);
        return o->second;
    }

    py const& object_entry(char const* aQualifiedName)
    {
        auto& s = the_state();
        auto const o = s.objects.find(aQualifiedName);
        if (o == s.objects.end())
            py_raise("SystemError", std::string{ "asyncio: no object " } + aQualifiedName);
        return o->second;
    }

    void define(char const* aModule, char const* aName, py const& aValue)
    {
        auto& s = the_state();
        auto& attributes = s.modules[aModule];
        for (auto& a : attributes)
            if (a.first == aName)
            {
                a.second = aValue;
                s.objects[std::string{ aModule } + "." + aName] = aValue;
                return;
            }
        attributes.emplace_back(aName, aValue);
        s.objects[std::string{ aModule } + "." + aName] = aValue;
    }

    void set_global(char const* aModule, char const* aName, py const& aValue)
    {
        define(aModule, aName, aValue);
        std::string const name = std::string{ "asyncio." } + aModule;
        if (modules_imported(name.c_str()))
            py{ fn_import(name.c_str()) }.set_attr(aName, aValue);
    }

    module_writer const& writer(char const* aModule)
    {
        struct entry { char const* name; module_writer writer; };
        static entry const sWriters[] =
        {
            { "_asyncio", { "_asyncio", asyncio__asyncio_docs::doc } },
            { "base_events", { "asyncio.base_events", asyncio_base_events_docs::doc } },
            { "base_futures", { "asyncio.base_futures", asyncio_base_futures_docs::doc } },
            { "base_subprocess", { "asyncio.base_subprocess", asyncio_base_subprocess_docs::doc } },
            { "base_tasks", { "asyncio.base_tasks", asyncio_base_tasks_docs::doc } },
            { "constants", { "asyncio.constants", asyncio_constants_docs::doc } },
            { "coroutines", { "asyncio.coroutines", asyncio_coroutines_docs::doc } },
            { "events", { "asyncio.events", asyncio_events_docs::doc } },
            { "exceptions", { "asyncio.exceptions", asyncio_exceptions_docs::doc } },
            { "format_helpers", { "asyncio.format_helpers", asyncio_format_helpers_docs::doc } },
            { "futures", { "asyncio.futures", asyncio_futures_docs::doc } },
            { "locks", { "asyncio.locks", asyncio_locks_docs::doc } },
            { "log", { "asyncio.log", asyncio_log_docs::doc } },
            { "mixins", { "asyncio.mixins", asyncio_mixins_docs::doc } },
            { "protocols", { "asyncio.protocols", asyncio_protocols_docs::doc } },
            { "queues", { "asyncio.queues", asyncio_queues_docs::doc } },
            { "runners", { "asyncio.runners", asyncio_runners_docs::doc } },
            { "selector_events", { "asyncio.selector_events", asyncio_selector_events_docs::doc } },
            { "staggered", { "asyncio.staggered", asyncio_staggered_docs::doc } },
            { "streams", { "asyncio.streams", asyncio_streams_docs::doc } },
            { "subprocess", { "asyncio.subprocess", asyncio_subprocess_docs::doc } },
            { "taskgroups", { "asyncio.taskgroups", asyncio_taskgroups_docs::doc } },
            { "tasks", { "asyncio.tasks", asyncio_tasks_docs::doc } },
            { "threads", { "asyncio.threads", asyncio_threads_docs::doc } },
            { "timeouts", { "asyncio.timeouts", asyncio_timeouts_docs::doc } },
            { "transports", { "asyncio.transports", asyncio_transports_docs::doc } },
            { "trsock", { "asyncio.trsock", asyncio_trsock_docs::doc } },
            { "unix_events", { "asyncio.unix_events", asyncio_unix_events_docs::doc } }
        };
        for (auto const& w : sWriters)
            if (std::strcmp(w.name, aModule) == 0)
                return w.writer;
        static module_writer const sNone{ "asyncio", nullptr };
        return sNone;
    }

    void make_asyncio()
    {
        auto& s = the_state();
        if (s.made || s.making)
            return;
        s.making = true;
        make_core();
        make_loop();
        make_windows_events();
        make_sync();
        s.making = false;
        s.made = true;
    }

    py coroutine(module_writer const& aWriter, std::string const& aQualname, std::function<py()> aBody)
    {
        auto const dot = aQualname.rfind('.');
        std::string const name = dot == std::string::npos ? aQualname : aQualname.substr(dot + 1u);
        return make_native_coroutine(name, aQualname, aWriter.module, std::move(aBody));
    }

    py async_method(module_writer const& aWriter, char const* aClass, char const* aName, std::vector<py_parameter> aParameters, pyclass::body_type aBody,
        bool aVarArgs, bool aVarKeywords)
    {
        aParameters.insert(aParameters.begin(), py_positional("self"));
        std::string const qualname = std::string{ aClass } + "." + aName;
        return aWriter.function(aName, qualname, std::move(aParameters), [aBody, qualname, &aWriter](arguments a)
        {
            return coroutine(aWriter, qualname, [aBody, args = std::vector<py>(a.begin(), a.end())]() { return aBody(arguments{ args }); });
        }, aVarArgs, aVarKeywords, CO_COROUTINE);
    }

    py async_function(module_writer const& aWriter, char const* aName, std::vector<py_parameter> aParameters, pyclass::body_type aBody, bool aVarArgs,
        bool aVarKeywords)
    {
        std::string const qualname{ aName };
        return aWriter.function(aName, qualname, std::move(aParameters), [aBody, qualname, &aWriter](arguments a)
        {
            return coroutine(aWriter, qualname, [aBody, args = std::vector<py>(a.begin(), a.end())]() { return aBody(arguments{ args }); });
        }, aVarArgs, aVarKeywords, CO_COROUTINE);
    }

    py closure(module_writer const& aWriter, std::string const& aName, std::string const& aQualname, native_body aBody)
    {
        return make_native_function(aName, aQualname, "", aWriter.module, std::move(aBody));
    }

    namespace
    {
        // (a module of the package: its objects (aModule: its name in the package (e.g. "events"); "": the package's))
        bool asyncio_module(std::vector<std::pair<std::string, object*>>& aAttributes, char const* aModule)
        {
            return py_guarded([&]()
            {
                make_asyncio();
                auto& s = the_state();
                if (*aModule == '\0')
                {
                    // (the package: its modules' __all__ (each module imported: an attribute of the package))
                    aAttributes.emplace_back("__doc__", make_str("The asyncio package, tracking PEP 3156."));
                    aAttributes.emplace_back("sys", fn_import("sys"));
                    static char const* const sModules[] = { "base_events", "coroutines", "events", "exceptions", "futures", "locks", "protocols",
                        "runners", "queues", "streams", "subprocess", "tasks", "taskgroups", "timeouts", "threads", "transports",
#if defined(_WIN32)
                        "windows_events"
#else
                        "unix_events"
#endif
                    };
                    std::vector<py> all;
                    for (auto m : sModules)
                    {
                        auto const attributes = s.modules.find(m);
                        if (attributes == s.modules.end())
                            continue;
                        for (auto const& [name, value] : attributes->second)
                            if (name == "__all__")
                                for (auto const& n : value.items())
                                {
                                    all.push_back(n);
                                    auto const text = n.str();
                                    for (auto const& [name2, value2] : attributes->second)
                                        if (name2 == text)
                                            aAttributes.emplace_back(text, value2.reference());
                                }
                    }
                    for (auto const& [m, attributes] : s.modules)
                    {
                        if (m == "_asyncio")
                            continue;
                        py const submodule{ fn_import(("asyncio." + m).c_str()) };
                        aAttributes.emplace_back(m, submodule.reference());
                    }
                    aAttributes.emplace_back("__all__", py_tuple(all).release());
                    return none();
                }
                auto const attributes = s.modules.find(aModule);
                if (attributes != s.modules.end())
                    for (auto const& [name, value] : attributes->second)
                        aAttributes.emplace_back(name, value.reference());
                return none();
            }) != nullptr;
        }

#define NEOS_ASYNCIO_MODULE(name) \
        inline bool const sAsyncio_##name = register_module("asyncio." #name, [](std::vector<std::pair<std::string, object*>>& aAttributes) \
            { return asyncio_module(aAttributes, #name); });
        NEOS_ASYNCIO_MODULE(base_events)
        NEOS_ASYNCIO_MODULE(base_futures)
        NEOS_ASYNCIO_MODULE(base_subprocess)
        NEOS_ASYNCIO_MODULE(base_tasks)
        NEOS_ASYNCIO_MODULE(constants)
        NEOS_ASYNCIO_MODULE(coroutines)
        NEOS_ASYNCIO_MODULE(events)
        NEOS_ASYNCIO_MODULE(exceptions)
        NEOS_ASYNCIO_MODULE(format_helpers)
        NEOS_ASYNCIO_MODULE(futures)
        NEOS_ASYNCIO_MODULE(locks)
        NEOS_ASYNCIO_MODULE(log)
        NEOS_ASYNCIO_MODULE(mixins)
        NEOS_ASYNCIO_MODULE(protocols)
        NEOS_ASYNCIO_MODULE(queues)
        NEOS_ASYNCIO_MODULE(runners)
        NEOS_ASYNCIO_MODULE(selector_events)
        NEOS_ASYNCIO_MODULE(staggered)
        NEOS_ASYNCIO_MODULE(streams)
        NEOS_ASYNCIO_MODULE(subprocess)
        NEOS_ASYNCIO_MODULE(taskgroups)
        NEOS_ASYNCIO_MODULE(tasks)
        NEOS_ASYNCIO_MODULE(threads)
        NEOS_ASYNCIO_MODULE(timeouts)
        NEOS_ASYNCIO_MODULE(transports)
        NEOS_ASYNCIO_MODULE(trsock)
#if defined(_WIN32)
        NEOS_ASYNCIO_MODULE(windows_events)
#else
        NEOS_ASYNCIO_MODULE(unix_events)
#endif
#undef NEOS_ASYNCIO_MODULE
        inline bool const sAsyncio = register_module("asyncio", [](std::vector<std::pair<std::string, object*>>& aAttributes)
            { return asyncio_module(aAttributes, ""); });
        inline bool const sAsyncioC = register_module("_asyncio", [](std::vector<std::pair<std::string, object*>>& aAttributes)
            { return asyncio_module(aAttributes, "_asyncio"); });
    }
}
