/*
  asyncio.hpp

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

// The package asyncio (CPython 3.13's Lib/asyncio and Modules/_asynciomodule.c): its modules' objects are made once (by make_asyncio,
// in the order of their dependencies, so that the modules' imports of each other are not circular) and each module (asyncio.events,
// asyncio.futures, ...) has its objects as its attributes. Their classes are made (by type) of closures (pyclass.hpp), so a program's
// subclasses override their methods; asyncio.Future and asyncio.Task are _asyncio's (as CPython's C implementations are: their
// __module__ is _asyncio).

#pragma once

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"

namespace neos::python_runtime::detail::asyncio
{
    using pyclass::S;
    using pyclass::I;
    using pyclass::F;
    using pyclass::B;
    using pyclass::Mod;
    using pyclass::none;
    using pyclass::boolean;
    using pyclass::M;
    using pyclass::has_attr;
    using pyclass::get_attr;
    using pyclass::callable;
    using pyclass::module_writer;
    using pyclass::namespace_type;
    using pyclass::try_except;
    using pyclass::try_finally;
    using pyclass::with_;
    using pyclass::async_with;
    using pyclass::unpack2;
    using pyclass::format;
    using pyclass::handler_scope;
    using pyclass::matches;
    using pyclass::iterate;
    using pyclass::unpack;
    using pyclass::call_ex;
    using pyclass::dict_of;
    using pyclass::set_of;
    using pyclass::inplace;
    using pyclass::del_attr;
    using pyclass::del_item;
    using pyclass::formatted;
    using pyclass::yield_from;
    using pyclass::truth_of;
    using pyclass::module_of;

    // (the package's objects (made once, and never destroyed: they outlive the program's objects))
    struct state
    {
        std::map<std::string, std::vector<std::pair<std::string, py>>, std::less<>> modules; ///< (each module's attributes, in order (by its name, e.g. "events"))
        std::map<std::string, py, std::less<>> objects;     ///< (the objects, by qualified name (e.g. "events.Handle"))
        // (those used most)
        py cancelledError;
        py invalidStateError;
        py future;
        py task;
        py handle;
        py timerHandle;
        py logger;
        py copyContext;
        py currentTasks;            ///< (tasks._current_tasks: {loop: task})
        py scheduledTasks;          ///< (tasks._scheduled_tasks: a WeakSet)
        py eagerTasks;              ///< (tasks._eager_tasks: a set)
        bool made = false;
        bool making = false;
    };
    state& the_state();

    // (an object of the package (by qualified name, e.g. "events.Handle"))
    py object_of(char const* aQualifiedName);
    // (its entry: the object, as it is defined (or assigned) now)
    py const& object_entry(char const* aQualifiedName);
    // (an object of the package: its entry, found once (G<"events.Handle">()))
    template <pyclass::fixed_string Name>
    py const& G() { static py const* const sEntry = &object_entry(Name.value); return *sEntry; }
    // (an object made: one of its module's attributes (aName); and an object of the package (its qualified name: aModule.aName))
    void define(char const* aModule, char const* aName, py const& aValue);
    // (a module's global assigned (global name; name = value): its object, and its module's attribute (if the module has been imported))
    void set_global(char const* aModule, char const* aName, py const& aValue);
    // (the package's objects: made (if they have not been))
    void make_asyncio();

    // (the modules' writers (their names and docstrings))
    module_writer const& writer(char const* aModule);

    // (the parts of the package, in the order they are made (asyncio_core.cpp, asyncio_loop.cpp, asyncio_sync.cpp))
    void make_core();           // (constants, exceptions, log, coroutines, format_helpers, base_futures, base_tasks, events, mixins; then:)
    void make_futures_and_tasks();  // (futures, tasks and _asyncio (asyncio_futures.cpp))
    void make_loop();           // (protocols, transports, trsock, base_events, selector_events, base_subprocess, unix_events, runners, threads)
    void make_windows_events(); // (windows_events (Windows): its selector event loop (asyncio_windows.cpp))
    void make_sync();           // (locks, queues, timeouts, taskgroups, streams, staggered, subprocess)

    // (the running loop of this thread (_asyncio's): borrowed; null if none)
    object* running_loop();
    void set_running_loop(object* aLoop);
    py get_running_loop();      // (RuntimeError if none)
    py get_event_loop();
    // (asyncio.futures.isfuture)
    bool is_future(py const& aObject);
    // (asyncio.coroutines.iscoroutine)
    bool is_coroutine(py const& aObject);
    // (futures._get_loop)
    py future_loop(py const& aFuture);
    // (the loop's call_soon(callback, *args, context=context) (context empty: none given))
    py call_soon(py const& aLoop, py const& aCallback, std::vector<py> const& aArgs, py const& aContext = {});
    // (tasks.ensure_future)
    py ensure_future(py const& aCoroOrFuture, py const& aLoop = {});

    // (a coroutine whose body is C++ (it awaits with py_await), as an async def's: its function's arguments bound)
    py coroutine(module_writer const& aWriter, std::string const& aQualname, std::function<py()> aBody);
    // (async def methods and functions)
    py async_method(module_writer const& aWriter, char const* aClass, char const* aName, std::vector<py_parameter> aParameters, pyclass::body_type aBody,
        bool aVarArgs = false, bool aVarKeywords = false);
    py async_function(module_writer const& aWriter, char const* aName, std::vector<py_parameter> aParameters, pyclass::body_type aBody, bool aVarArgs = false,
        bool aVarKeywords = false);
    // (a closure as a function (no docstring): its qualified name, and its arguments (all of them: positional and keyword))
    py closure(module_writer const& aWriter, std::string const& aName, std::string const& aQualname, native_body aBody);
}
