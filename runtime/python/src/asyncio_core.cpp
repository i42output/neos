/*
  asyncio_core.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/asyncio/constants.py, Lib/asyncio/exceptions.py,
  Lib/asyncio/log.py, Lib/asyncio/coroutines.py, Lib/asyncio/format_helpers.py, Lib/asyncio/base_futures.py,
  Lib/asyncio/base_tasks.py, Lib/asyncio/events.py, Lib/asyncio/mixins.py and Modules/_asynciomodule.c (the running loop and
  get_event_loop)), Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python
  Software Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from Python and C to C++ for
  the neos Python runtime plugin and adapted to its object model (see runtime/python/NOTICE). Lib/asyncio/constants.py and
  Lib/asyncio/events.py contain code from uvloop (https://github.com/MagicStack/uvloop/tree/v0.16.0), Copyright (c)
  2015-2021 MagicStack Inc. (SPDX-License-Identifier: PSF-2.0 AND (MIT OR Apache-2.0); see runtime/python/NOTICE).
*/

// asyncio's modules constants, exceptions, log, coroutines, format_helpers, base_futures, base_tasks, events and mixins. The running
// loop of each thread is native (as _asynciomodule.c's is): get_running_loop, _get_running_loop, _set_running_loop and get_event_loop
// are _asyncio's. (In debug mode a callback's or future's source traceback is the program's frames: the runtime's own functions have
// none (so asyncio's internal frames, which CPython removes from them, are not there).)

#include <cstring>
#include "asyncio.hpp"

namespace neos::python_runtime::detail::asyncio
{
    namespace
    {
        // --- the running loop (_asynciomodule.c's: each thread's)

        thread_local object* tRunningLoop = nullptr;    ///< (a new reference)

        // (a policy, the global event loop policy (events._event_loop_policy): its module's attribute too)
        py& event_loop_policy()
        {
            static py* sPolicy = new py{};
            return *sPolicy;
        }
        void set_event_loop_policy(py const& aPolicy)
        {
            event_loop_policy() = aPolicy;
            define("events", "_event_loop_policy", aPolicy.empty() ? none() : aPolicy);
            if (modules_imported("asyncio.events"))
                py{ fn_import("asyncio.events") }.set_attr("_event_loop_policy", aPolicy.empty() ? none() : aPolicy);
        }
        py get_event_loop_policy()
        {
            if (event_loop_policy().empty())
            {
                // (_init_event_loop_policy: the default policy (made once, under events._lock))
                with_(object_of("events._lock"), [&](py const&)
                {
                    if (event_loop_policy().empty())
#if defined(_WIN32)
                        set_event_loop_policy(object_of("windows_events.DefaultEventLoopPolicy")());
#else
                        set_event_loop_policy(object_of("unix_events.DefaultEventLoopPolicy")());
#endif
                });
            }
            return event_loop_policy();
        }

        // --- coroutines

        bool is_debug_mode()
        {
            py const flags = M("sys", "flags");
            if (flags.attr("dev_mode").truthy())
                return true;
            if (flags.attr("ignore_environment").truthy())
                return false;
            return M("os", "environ").method("get", { S("PYTHONASYNCIODEBUG") }).truthy();
        }

        py format_coroutine(py const& coro)
        {
            // (its name: Cython's coroutines sometimes have no proper __qualname__ or __name__)
            std::string coroName;
            if (has_attr(coro, "__qualname__") && coro.attr("__qualname__").truthy())
                coroName = coro.attr("__qualname__").str();
            else if (has_attr(coro, "__name__") && coro.attr("__name__").truthy())
                coroName = coro.attr("__name__").str();
            else
                coroName = "<" + type_of(coro).attr("__name__").str() + " without __name__>";
            coroName += "()";
            auto const isRunning = [&]()
            {
                py running;
                try_except([&]() { running = coro.attr("cr_running"); }, builtin("AttributeError"), [&](py const&)
                {
                    try_except([&]() { running = coro.attr("gi_running"); }, builtin("AttributeError"), [&](py const&) { running = boolean(false); });
                });
                return running.truthy();
            };
            py coroCode = none();
            if (has_attr(coro, "cr_code") && coro.attr("cr_code").truthy())
                coroCode = coro.attr("cr_code");
            else if (has_attr(coro, "gi_code") && coro.attr("gi_code").truthy())
                coroCode = coro.attr("gi_code");
            if (!coroCode.truthy())
            {
                // (builtin types might not have __qualname__ or __name__)
                if (isRunning())
                    return S((coroName + " running").c_str());
                return S(coroName.c_str());
            }
            py coroFrame = none();
            if (has_attr(coro, "gi_frame") && coro.attr("gi_frame").truthy())
                coroFrame = coro.attr("gi_frame");
            else if (has_attr(coro, "cr_frame") && coro.attr("cr_frame").truthy())
                coroFrame = coro.attr("cr_frame");
            // (Cython's coroutines can have a fake code object without a proper co_filename)
            py filename = coroCode.attr("co_filename");
            if (!filename.truthy())
                filename = S("<empty co_filename>");
            if (!coroFrame.is_none())
                return S((coroName + " running at " + filename.str() + ":" + coroFrame.attr("f_lineno").str()).c_str());
            return S((coroName + " done, defined at " + filename.str() + ":" + coroCode.attr("co_firstlineno").str()).c_str());
        }

        // --- format_helpers

        py get_function_source(py func)
        {
            func = M("inspect", "unwrap")(func);
            if (M("inspect", "isfunction")(func).truthy())
            {
                // (a native function (e.g. one of asyncio's): its source is not Python's)
                py const code = get_attr(func, "__code__", none());
                if (code.is_none() || code.attr("co_filename").str() == "<native>")
                    return none();
                return py_tuple({ code.attr("co_filename"), code.attr("co_firstlineno") });
            }
            if (isinstance(func, M("functools", "partial")) || isinstance(func, M("functools", "partialmethod")))
                return get_function_source(func.attr("func"));
            return none();
        }

        std::string format_args_and_kwargs(py const& args, py const& kwargs, bool debug)
        {
            // (only in debug mode: arguments may contain sensitive information)
            if (!debug)
                return "()";
            std::string items;
            py const reprlibRepr = M("reprlib", "repr");
            if (!args.is_none() && args.truthy())
                for (auto const& arg : args.items())
                    items += (items.empty() ? "" : ", ") + reprlibRepr(arg).str();
            if (!kwargs.is_none() && kwargs.truthy())
                for (auto const& item : kwargs.method("items").items())
                    items += (items.empty() ? "" : ", ") + item[py{ 0 }].str() + "=" + reprlibRepr(item[py{ 1 }]).str();
            return "(" + items + ")";
        }

        std::string format_callback(py const& func, py const& args, py const& kwargs, bool debug, std::string const& aSuffix = {})
        {
            if (isinstance(func, M("functools", "partial")))
            {
                auto const suffix = format_args_and_kwargs(args, kwargs, debug) + aSuffix;
                return format_callback(func.attr("func"), func.attr("args"), func.attr("keywords"), debug, suffix);
            }
            std::string funcRepr;
            if (has_attr(func, "__qualname__") && func.attr("__qualname__").truthy())
                funcRepr = func.attr("__qualname__").str();
            else if (has_attr(func, "__name__") && func.attr("__name__").truthy())
                funcRepr = func.attr("__name__").str();
            else
                funcRepr = func.repr();
            funcRepr += format_args_and_kwargs(args, kwargs, debug);
            return funcRepr + aSuffix;
        }

        std::string format_callback_source(py const& func, py const& args, bool debug)
        {
            auto funcRepr = format_callback(func, args, none(), debug);
            py const source = get_function_source(func);
            if (source.truthy())
                funcRepr += " at " + source[py{ 0 }].str() + ":" + source[py{ 1 }].str();
            return funcRepr;
        }

        py extract_stack(py f, py limit)
        {
            // (the frames of the program: the runtime's functions have none)
            if (f.is_none())
                f = M("sys", "_getframe")();
            if (limit.is_none())
                limit = object_of("constants.DEBUG_STACK_DEPTH");
            py const traceback{ fn_import("traceback") };
            py const stack = traceback.attr("StackSummary").attr("extract").call({ traceback.attr("walk_stack")(f) }, { { "limit", limit },
                { "lookup_lines", boolean(false) } });
            stack.method("reverse");
            return stack;
        }

        // --- base_futures

        char const* const PENDING = "PENDING";
        char const* const CANCELLED = "CANCELLED";
        char const* const FINISHED = "FINISHED";

        std::string format_callbacks(py const& cb)
        {
            auto const size = cb.size();
            auto const formatCb = [](py const& callback) { return format_callback_source(callback, py_tuple(), false); };
            std::string text;
            if (size == 1)
                text = formatCb(cb[py{ 0 }][py{ 0 }]);
            else if (size == 2)
                text = formatCb(cb[py{ 0 }][py{ 0 }]) + ", " + formatCb(cb[py{ 1 }][py{ 0 }]);
            else if (size > 2)
                text = formatCb(cb[py{ 0 }][py{ 0 }]) + ", <" + std::to_string(size - 2) + " more>, " + formatCb(cb[py{ -1 }][py{ 0 }]);
            return "cb=[" + text + "]";
        }

        py future_repr_info(py const& future)
        {
            py const info = py_list({ future.attr("_state").method("lower") });
            if (future.attr("_state").str() == FINISHED)
            {
                if (!future.attr("_exception").is_none())
                    info.method("append", { S(("exception=" + future.attr("_exception").repr()).c_str()) });
                else
                {
                    // (reprlib limits the length of the output, especially for very long strings)
                    info.method("append", { S(("result=" + M("reprlib", "repr")(future.attr("_result")).str()).c_str()) });
                }
            }
            if (future.attr("_callbacks").truthy())
                info.method("append", { S(format_callbacks(future.attr("_callbacks")).c_str()) });
            if (future.attr("_source_traceback").truthy())
            {
                py const frame = future.attr("_source_traceback")[py{ -1 }];
                info.method("append", { S(("created at " + frame[py{ 0 }].str() + ":" + frame[py{ 1 }].str()).c_str()) });
            }
            return info;
        }

        // --- base_tasks

        py task_repr_info(py const& task)
        {
            py const info = future_repr_info(task);
            if (task.method("cancelling").truthy() && !task.method("done").truthy())
                info.set_item(py{ 0 }, S("cancelling"));     // (the status replaced)
            info.method("insert", { py{ 1 }, S("name=%r") % task.method("get_name") });
            if (!task.attr("_fut_waiter").is_none())
                info.method("insert", { py{ 2 }, S(("wait_for=" + task.attr("_fut_waiter").repr()).c_str()) });
            if (task.attr("_coro").truthy())
                info.method("insert", { py{ 2 }, S(("coro=<" + format_coroutine(task.attr("_coro")).str() + ">").c_str()) });
            return info;
        }

        py task_get_stack(py const& task, py limit)
        {
            py const frames = py_list();
            py const coro = task.attr("_coro");
            py f = none();
            if (has_attr(coro, "cr_frame"))
                f = coro.attr("cr_frame");          // ('async def' coroutines)
            else if (has_attr(coro, "gi_frame"))
                f = coro.attr("gi_frame");          // (legacy coroutines)
            else if (has_attr(coro, "ag_frame"))
                f = coro.attr("ag_frame");          // (asynchronous generators)
            if (!f.is_none())
            {
                while (!f.is_none())
                {
                    if (!limit.is_none())
                    {
                        if (limit <= py{ 0 })
                            break;
                        limit = limit - py{ 1 };
                    }
                    frames.method("append", { f });
                    f = f.attr("f_back");
                }
                frames.method("reverse");
            }
            else if (!task.attr("_exception").is_none())
            {
                py tb = task.attr("_exception").attr("__traceback__");
                while (!tb.is_none())
                {
                    if (!limit.is_none())
                    {
                        if (limit <= py{ 0 })
                            break;
                        limit = limit - py{ 1 };
                    }
                    frames.method("append", { tb.attr("tb_frame") });
                    tb = tb.attr("tb_next");
                }
            }
            return frames;
        }

        void task_print_stack(py const& task, py const& limit, py const& file)
        {
            py const extractedList = py_list();
            py const checked = builtin("set")();
            for (auto const& f : task.method("get_stack", {}, { { "limit", limit } }).items())
            {
                py const lineno = f.attr("f_lineno");
                py const co = f.attr("f_code");
                py const filename = co.attr("co_filename");
                py const name = co.attr("co_name");
                if (!checked.contains(filename))
                {
                    checked.method("add", { filename });
                    M("linecache", "checkcache")(filename);
                }
                py const line = M("linecache", "getline")(filename, lineno, f.attr("f_globals"));
                extractedList.method("append", { py_tuple({ filename, lineno, name, line }) });
            }
            py const exc = task.attr("_exception");
            py const print = builtin("print");
            if (!extractedList.truthy())
                print.call({ S(("No stack for " + task.repr()).c_str()) }, { { "file", file } });
            else if (!exc.is_none())
                print.call({ S(("Traceback for " + task.repr() + " (most recent call last):").c_str()) }, { { "file", file } });
            else
                print.call({ S(("Stack for " + task.repr() + " (most recent call last):").c_str()) }, { { "file", file } });
            M("traceback", "print_list").call({ extractedList }, { { "file", file } });
            if (!exc.is_none())
                for (auto const& line : M("traceback", "format_exception_only")(type_of(exc), exc).items())
                    print.call({ line }, { { "file", file }, { "end", S("") } });
        }

        // --- events: Handle and TimerHandle

        bool loop_debug(py const& aLoop) { return aLoop.method("get_debug").truthy(); }

        py handle_repr_info(py const& self)
        {
            py const info = py_list({ type_of(self).attr("__name__") });
            if (self.attr("_cancelled").truthy())
                info.method("append", { S("cancelled") });
            if (!self.attr("_callback").is_none())
                info.method("append", { S(format_callback_source(self.attr("_callback"), self.attr("_args"), loop_debug(self.attr("_loop"))).c_str()) });
            if (self.attr("_source_traceback").truthy())
            {
                py const frame = self.attr("_source_traceback")[py{ -1 }];
                info.method("append", { S(("created at " + frame[py{ 0 }].str() + ":" + frame[py{ 1 }].str()).c_str()) });
            }
            return info;
        }

        void handle_init(py const& self, py const& aCallback, py const& aArgs, py const& aLoop, py aContext)
        {
            if (aContext.is_none())
                aContext = py{ context_copy_current() };
            self.set_attr("_context", aContext);
            self.set_attr("_loop", aLoop);
            self.set_attr("_callback", aCallback);
            self.set_attr("_args", aArgs);
            self.set_attr("_cancelled", boolean(false));
            self.set_attr("_repr", none());
            if (loop_debug(aLoop))
                self.set_attr("_source_traceback", extract_stack(none(), none()));
            else
                self.set_attr("_source_traceback", none());
        }

        void handle_cancel(py const& self)
        {
            if (!self.attr("_cancelled").truthy())
            {
                self.set_attr("_cancelled", boolean(true));
                // (debug mode: its representation kept (with the callback and its arguments), e.g. for "Executing <Handle...> took 2.5 seconds")
                if (loop_debug(self.attr("_loop")))
                    self.set_attr("_repr", S(self.repr().c_str()));
                self.set_attr("_callback", none());
                self.set_attr("_args", none());
            }
        }

        void handle_run(py const& self)
        {
            py const context = self.attr("_context");
            py const callback = self.attr("_callback");
            py const args = self.attr("_args");
            try_except([&]()
            {
                // (self._context.run(self._callback, *self._args))
                if (context_is(context.get()))
                {
                    py_checked(context_run_enter(context.get()));
                    ref result{ call(callback.get(), builtin("tuple")(args).get(), nullptr) };
                    ref pending;
                    if (!result)
                        pending = ref{ take_exception() };
                    bool const exited = context_run_exit(context.get());
                    if (pending)
                    {
                        if (exited)
                            reraise_exception(pending.get());
                        throw raised{};
                    }
                    py_checked(exited);
                }
                else
                {
                    std::vector<py> runArgs{ callback };
                    for (auto const& a : args.items())
                        runArgs.push_back(a);
                    context.attr("run").call(runArgs);
                }
            }, builtin("BaseException"), [&](py const& exc)
            {
                if (isinstance(exc, py_tuple({ builtin("SystemExit"), builtin("KeyboardInterrupt") })))
                    py_reraise(exc);
                auto const cb = format_callback_source(callback, args, loop_debug(self.attr("_loop")));
                py const context = py_dict();
                context.set_item(S("message"), S(("Exception in callback " + cb).c_str()));
                context.set_item(S("exception"), exc);
                context.set_item(S("handle"), self);
                if (self.attr("_source_traceback").truthy())
                    context.set_item(S("source_traceback"), self.attr("_source_traceback"));
                self.attr("_loop").method("call_exception_handler", { context });
            });
        }

        void make_handles()
        {
            auto& s = the_state();
            auto const& ev = writer("events");
            s.handle = ev.make_class("Handle", {}, {
                { "__slots__", py_tuple({ S("_callback"), S("_args"), S("_cancelled"), S("_loop"), S("_source_traceback"), S("_repr"), S("__weakref__"),
                    S("_context") }) },
                { "__init__", ev.method("Handle", "__init__", { py_positional("callback"), py_positional("args"), py_positional("loop"),
                    py_positional("context", none()) }, [](arguments a)
                    {
                        handle_init(a[0], a[1], a[2], a[3], a[4]);
                        return none();
                    }) },
                { "_repr_info", ev.method("Handle", "_repr_info", {}, [](arguments a) { return handle_repr_info(a[0]); }) },
                { "__repr__", ev.method("Handle", "__repr__", {}, [](arguments a)
                    {
                        if (!a[0].attr("_repr").is_none())
                            return a[0].attr("_repr");
                        return S("<{}>").method("format", { S(" ").method("join", { a[0].method("_repr_info") }) });
                    }) },
                { "get_context", ev.method("Handle", "get_context", {}, [](arguments a) { return a[0].attr("_context"); }) },
                { "cancel", ev.method("Handle", "cancel", {}, [](arguments a)
                    {
                        handle_cancel(a[0]);
                        return none();
                    }) },
                { "cancelled", ev.method("Handle", "cancelled", {}, [](arguments a) { return a[0].attr("_cancelled"); }) },
                { "_run", ev.method("Handle", "_run", {}, [](arguments a)
                    {
                        handle_run(a[0]);
                        return none();
                    }) } });
            define("events", "Handle", s.handle);
            auto const timerCompare = [](char const* aName, int aOperation, bool aOrEqual)
            {
                return writer("events").method("TimerHandle", aName, { py_positional("other") }, [aOperation, aOrEqual](arguments a)
                {
                    if (!isinstance(a[1], the_state().timerHandle))
                        return py::borrowed(not_implemented());
                    bool result = py_compare(aOperation, a[0].attr("_when"), a[1].attr("_when")).truthy();
                    if (!result && aOrEqual)
                        result = a[0].method("__eq__", { a[1] }).truthy();
                    return boolean(result);
                });
            };
            s.timerHandle = ev.make_class("TimerHandle", { s.handle }, {
                { "__slots__", py_list({ S("_scheduled"), S("_when") }) },
                { "__init__", ev.method("TimerHandle", "__init__", { py_positional("when"), py_positional("callback"), py_positional("args"), py_positional("loop"),
                    py_positional("context", none()) }, [](arguments a)
                    {
                        // (super().__init__(callback, args, loop, context): its source traceback has no frame of the runtime's to remove)
                        handle_init(a[0], a[2], a[3], a[4], a[5]);
                        a[0].set_attr("_when", a[1]);
                        a[0].set_attr("_scheduled", boolean(false));
                        return none();
                    }) },
                { "_repr_info", ev.method("TimerHandle", "_repr_info", {}, [](arguments a)
                    {
                        py const info = handle_repr_info(a[0]);
                        auto const pos = a[0].attr("_cancelled").truthy() ? 2 : 1;
                        info.method("insert", { py{ pos }, S(("when=" + a[0].attr("_when").str()).c_str()) });
                        return info;
                    }) },
                { "__hash__", ev.method("TimerHandle", "__hash__", {}, [](arguments a) { return builtin("hash")(a[0].attr("_when")); }) },
                { "__lt__", timerCompare("__lt__", NEOS_PY_LT, false) },
                { "__le__", timerCompare("__le__", NEOS_PY_LT, true) },
                { "__gt__", timerCompare("__gt__", NEOS_PY_GT, false) },
                { "__ge__", timerCompare("__ge__", NEOS_PY_GT, true) },
                { "__eq__", ev.method("TimerHandle", "__eq__", { py_positional("other") }, [](arguments a)
                    {
                        if (!isinstance(a[1], the_state().timerHandle))
                            return py::borrowed(not_implemented());
                        return boolean(a[0].attr("_when") == a[1].attr("_when") && a[0].attr("_callback") == a[1].attr("_callback") &&
                            a[0].attr("_args") == a[1].attr("_args") && a[0].attr("_cancelled") == a[1].attr("_cancelled"));
                    }) },
                { "cancel", ev.method("TimerHandle", "cancel", {}, [](arguments a)
                    {
                        if (!a[0].attr("_cancelled").truthy())
                            a[0].attr("_loop").method("_timer_handle_cancelled", { a[0] });
                        handle_cancel(a[0]);
                        return none();
                    }) },
                { "when", ev.method("TimerHandle", "when", {}, [](arguments a) { return a[0].attr("_when"); }) } });
            define("events", "TimerHandle", s.timerHandle);
        }

        // --- events: the abstract classes and the policies

        void make_abstract_events()
        {
            auto const& ev = writer("events");
            define("events", "AbstractServer", ev.make_class("AbstractServer", {}, {
                { "close", ev.method("AbstractServer", "close", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "close_clients", ev.method("AbstractServer", "close_clients", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "abort_clients", ev.method("AbstractServer", "abort_clients", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "get_loop", ev.method("AbstractServer", "get_loop", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "is_serving", ev.method("AbstractServer", "is_serving", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "start_serving", async_method(ev, "AbstractServer", "start_serving", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "serve_forever", async_method(ev, "AbstractServer", "serve_forever", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "wait_closed", async_method(ev, "AbstractServer", "wait_closed", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "__aenter__", async_method(ev, "AbstractServer", "__aenter__", {}, [](arguments a)
                    {
                        return a[0];
                    }) },
                { "__aexit__", async_method(ev, "AbstractServer", "__aexit__", {}, [](arguments a)
                    {
                        a[0].method("close");
                        py_await(a[0].method("wait_closed"));
                        return none();
                    }, true) },
            }));

            define("events", "AbstractEventLoop", ev.make_class("AbstractEventLoop", {}, {
                { "run_forever", ev.method("AbstractEventLoop", "run_forever", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "run_until_complete", ev.method("AbstractEventLoop", "run_until_complete", { py_positional("future") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "stop", ev.method("AbstractEventLoop", "stop", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "is_running", ev.method("AbstractEventLoop", "is_running", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "is_closed", ev.method("AbstractEventLoop", "is_closed", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "close", ev.method("AbstractEventLoop", "close", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "shutdown_asyncgens", async_method(ev, "AbstractEventLoop", "shutdown_asyncgens", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "shutdown_default_executor", async_method(ev, "AbstractEventLoop", "shutdown_default_executor", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "_timer_handle_cancelled", ev.method("AbstractEventLoop", "_timer_handle_cancelled", { py_positional("handle") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "call_soon", ev.method("AbstractEventLoop", "call_soon", { py_positional("callback"), py_keyword("context", none()) }, [](arguments a)
                    {
                        std::vector<py> args{ py{ 0 }, a[1] };
                        for (auto const& x : a[3].items())
                            args.push_back(x);
                        return a[0].attr("call_later").call(args, { { "context", a[2] } });
                    }, true) },
                { "call_later", ev.method("AbstractEventLoop", "call_later", { py_positional("delay"), py_positional("callback"), py_keyword("context", none()) }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }, true) },
                { "call_at", ev.method("AbstractEventLoop", "call_at", { py_positional("when"), py_positional("callback"), py_keyword("context", none()) }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }, true) },
                { "time", ev.method("AbstractEventLoop", "time", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "create_future", ev.method("AbstractEventLoop", "create_future", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "create_task", ev.method("AbstractEventLoop", "create_task", { py_positional("coro") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }, true, true) },
                { "call_soon_threadsafe", ev.method("AbstractEventLoop", "call_soon_threadsafe", { py_positional("callback"), py_keyword("context", none()) }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }, true) },
                { "run_in_executor", ev.method("AbstractEventLoop", "run_in_executor", { py_positional("executor"), py_positional("func") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }, true) },
                { "set_default_executor", ev.method("AbstractEventLoop", "set_default_executor", { py_positional("executor") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "getaddrinfo", async_method(ev, "AbstractEventLoop", "getaddrinfo", { py_positional("host"), py_positional("port"), py_keyword("family", py{ 0 }), py_keyword("type", py{ 0 }), py_keyword("proto", py{ 0 }), py_keyword("flags", py{ 0 }) }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "getnameinfo", async_method(ev, "AbstractEventLoop", "getnameinfo", { py_positional("sockaddr"), py_positional("flags", py{ 0 }) }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "create_connection", async_method(ev, "AbstractEventLoop", "create_connection", { py_positional("protocol_factory"), py_positional("host", none()), py_positional("port", none()), py_keyword("ssl", none()), py_keyword("family", py{ 0 }), py_keyword("proto", py{ 0 }), py_keyword("flags", py{ 0 }), py_keyword("sock", none()), py_keyword("local_addr", none()), py_keyword("server_hostname", none()), py_keyword("ssl_handshake_timeout", none()), py_keyword("ssl_shutdown_timeout", none()), py_keyword("happy_eyeballs_delay", none()), py_keyword("interleave", none()) }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "create_server", async_method(ev, "AbstractEventLoop", "create_server", { py_positional("protocol_factory"), py_positional("host", none()), py_positional("port", none()), py_keyword("family", M("socket", "AF_UNSPEC")), py_keyword("flags", M("socket", "AI_PASSIVE")), py_keyword("sock", none()), py_keyword("backlog", py{ 100 }), py_keyword("ssl", none()), py_keyword("reuse_address", none()), py_keyword("reuse_port", none()), py_keyword("keep_alive", none()), py_keyword("ssl_handshake_timeout", none()), py_keyword("ssl_shutdown_timeout", none()), py_keyword("start_serving", boolean(true)) }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "sendfile", async_method(ev, "AbstractEventLoop", "sendfile", { py_positional("transport"), py_positional("file"), py_positional("offset", py{ 0 }), py_positional("count", none()), py_keyword("fallback", boolean(true)) }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "start_tls", async_method(ev, "AbstractEventLoop", "start_tls", { py_positional("transport"), py_positional("protocol"), py_positional("sslcontext"), py_keyword("server_side", boolean(false)), py_keyword("server_hostname", none()), py_keyword("ssl_handshake_timeout", none()), py_keyword("ssl_shutdown_timeout", none()) }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "create_unix_connection", async_method(ev, "AbstractEventLoop", "create_unix_connection", { py_positional("protocol_factory"), py_positional("path", none()), py_keyword("ssl", none()), py_keyword("sock", none()), py_keyword("server_hostname", none()), py_keyword("ssl_handshake_timeout", none()), py_keyword("ssl_shutdown_timeout", none()) }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "create_unix_server", async_method(ev, "AbstractEventLoop", "create_unix_server", { py_positional("protocol_factory"), py_positional("path", none()), py_keyword("sock", none()), py_keyword("backlog", py{ 100 }), py_keyword("ssl", none()), py_keyword("ssl_handshake_timeout", none()), py_keyword("ssl_shutdown_timeout", none()), py_keyword("start_serving", boolean(true)) }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "connect_accepted_socket", async_method(ev, "AbstractEventLoop", "connect_accepted_socket", { py_positional("protocol_factory"), py_positional("sock"), py_keyword("ssl", none()), py_keyword("ssl_handshake_timeout", none()), py_keyword("ssl_shutdown_timeout", none()) }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "create_datagram_endpoint", async_method(ev, "AbstractEventLoop", "create_datagram_endpoint", { py_positional("protocol_factory"), py_positional("local_addr", none()), py_positional("remote_addr", none()), py_keyword("family", py{ 0 }), py_keyword("proto", py{ 0 }), py_keyword("flags", py{ 0 }), py_keyword("reuse_address", none()), py_keyword("reuse_port", none()), py_keyword("allow_broadcast", none()), py_keyword("sock", none()) }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "connect_read_pipe", async_method(ev, "AbstractEventLoop", "connect_read_pipe", { py_positional("protocol_factory"), py_positional("pipe") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "connect_write_pipe", async_method(ev, "AbstractEventLoop", "connect_write_pipe", { py_positional("protocol_factory"), py_positional("pipe") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "subprocess_shell", async_method(ev, "AbstractEventLoop", "subprocess_shell", { py_positional("protocol_factory"), py_positional("cmd"), py_keyword("stdin", py{ -1 }), py_keyword("stdout", py{ -1 }), py_keyword("stderr", py{ -1 }) }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }, true, true) },
                { "subprocess_exec", async_method(ev, "AbstractEventLoop", "subprocess_exec", { py_positional("protocol_factory"), py_keyword("stdin", py{ -1 }), py_keyword("stdout", py{ -1 }), py_keyword("stderr", py{ -1 }) }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }, true, true) },
                { "add_reader", ev.method("AbstractEventLoop", "add_reader", { py_positional("fd"), py_positional("callback") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }, true) },
                { "remove_reader", ev.method("AbstractEventLoop", "remove_reader", { py_positional("fd") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "add_writer", ev.method("AbstractEventLoop", "add_writer", { py_positional("fd"), py_positional("callback") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }, true) },
                { "remove_writer", ev.method("AbstractEventLoop", "remove_writer", { py_positional("fd") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "sock_recv", async_method(ev, "AbstractEventLoop", "sock_recv", { py_positional("sock"), py_positional("nbytes") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "sock_recv_into", async_method(ev, "AbstractEventLoop", "sock_recv_into", { py_positional("sock"), py_positional("buf") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "sock_recvfrom", async_method(ev, "AbstractEventLoop", "sock_recvfrom", { py_positional("sock"), py_positional("bufsize") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "sock_recvfrom_into", async_method(ev, "AbstractEventLoop", "sock_recvfrom_into", { py_positional("sock"), py_positional("buf"), py_positional("nbytes", py{ 0 }) }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "sock_sendall", async_method(ev, "AbstractEventLoop", "sock_sendall", { py_positional("sock"), py_positional("data") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "sock_sendto", async_method(ev, "AbstractEventLoop", "sock_sendto", { py_positional("sock"), py_positional("data"), py_positional("address") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "sock_connect", async_method(ev, "AbstractEventLoop", "sock_connect", { py_positional("sock"), py_positional("address") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "sock_accept", async_method(ev, "AbstractEventLoop", "sock_accept", { py_positional("sock") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "sock_sendfile", async_method(ev, "AbstractEventLoop", "sock_sendfile", { py_positional("sock"), py_positional("file"), py_positional("offset", py{ 0 }), py_positional("count", none()), py_keyword("fallback", none()) }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "add_signal_handler", ev.method("AbstractEventLoop", "add_signal_handler", { py_positional("sig"), py_positional("callback") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }, true) },
                { "remove_signal_handler", ev.method("AbstractEventLoop", "remove_signal_handler", { py_positional("sig") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "set_task_factory", ev.method("AbstractEventLoop", "set_task_factory", { py_positional("factory") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "get_task_factory", ev.method("AbstractEventLoop", "get_task_factory", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "get_exception_handler", ev.method("AbstractEventLoop", "get_exception_handler", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "set_exception_handler", ev.method("AbstractEventLoop", "set_exception_handler", { py_positional("handler") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "default_exception_handler", ev.method("AbstractEventLoop", "default_exception_handler", { py_positional("context") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "call_exception_handler", ev.method("AbstractEventLoop", "call_exception_handler", { py_positional("context") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "get_debug", ev.method("AbstractEventLoop", "get_debug", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "set_debug", ev.method("AbstractEventLoop", "set_debug", { py_positional("enabled") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
            }));

            define("events", "AbstractEventLoopPolicy", ev.make_class("AbstractEventLoopPolicy", {}, {
                { "get_event_loop", ev.method("AbstractEventLoopPolicy", "get_event_loop", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "set_event_loop", ev.method("AbstractEventLoopPolicy", "set_event_loop", { py_positional("loop") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "new_event_loop", ev.method("AbstractEventLoopPolicy", "new_event_loop", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "get_child_watcher", ev.method("AbstractEventLoopPolicy", "get_child_watcher", {}, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
                { "set_child_watcher", ev.method("AbstractEventLoopPolicy", "set_child_watcher", { py_positional("watcher") }, [](arguments) -> py { py_raise(builtin("NotImplementedError")()); }) },
            }));
        }

        void make_policies()
        {
            auto const& ev = writer("events");
            // (BaseDefaultEventLoopPolicy: each thread has its event loop (made by default only for the main thread))
            py const local = ev.make_class("_Local", { M("threading", "local") }, { { "_loop", none() }, { "_set_called", boolean(false) } },
                "BaseDefaultEventLoopPolicy._Local");
            define("events", "BaseDefaultEventLoopPolicy", ev.make_class("BaseDefaultEventLoopPolicy", { object_of("events.AbstractEventLoopPolicy") }, {
                { "_loop_factory", none() },
                { "_Local", local },
                { "__init__", ev.method("BaseDefaultEventLoopPolicy", "__init__", {}, [](arguments a)
                    {
                        a[0].set_attr("_local", a[0].attr("_Local")());
                        return none();
                    }) },
                { "get_event_loop", ev.method("BaseDefaultEventLoopPolicy", "get_event_loop", {}, [](arguments a)
                    {
                        auto const& self = a[0];
                        py const local = self.attr("_local");
                        if (local.attr("_loop").is_none() && !local.attr("_set_called").truthy() &&
                            M("threading", "current_thread")().is(M("threading", "main_thread")()))
                        {
                            // (the warning is attached to the program's line outside asyncio (the runtime's functions have no frames))
                            M("warnings", "warn").call({ S("There is no current event loop"), builtin("DeprecationWarning") }, { { "stacklevel", py{ 1 } } });
                            self.method("set_event_loop", { self.method("new_event_loop") });
                        }
                        if (local.attr("_loop").is_none())
                            py_raise("RuntimeError", "There is no current event loop in thread " + M("threading", "current_thread")().attr("name").repr() + ".");
                        return local.attr("_loop");
                    }) },
                { "set_event_loop", ev.method("BaseDefaultEventLoopPolicy", "set_event_loop", { py_positional("loop") }, [](arguments a)
                    {
                        a[0].attr("_local").set_attr("_set_called", boolean(true));
                        if (!a[1].is_none() && !isinstance(a[1], object_of("events.AbstractEventLoop")))
                            py_raise("TypeError", "loop must be an instance of AbstractEventLoop or None, not '" + type_of(a[1]).attr("__name__").str() + "'");
                        a[0].attr("_local").set_attr("_loop", a[1]);
                        return none();
                    }) },
                { "new_event_loop", ev.method("BaseDefaultEventLoopPolicy", "new_event_loop", {}, [](arguments a)
                    {
                        return a[0].method("_loop_factory");
                    }) } }));
        }

        void make_events_functions()
        {
            auto const& ev = writer("events");
            define("events", "__all__", py_tuple({ S("AbstractEventLoopPolicy"), S("AbstractEventLoop"), S("AbstractServer"), S("Handle"), S("TimerHandle"),
                S("get_event_loop_policy"), S("set_event_loop_policy"), S("get_event_loop"), S("set_event_loop"), S("new_event_loop"), S("get_child_watcher"),
                S("set_child_watcher"), S("_set_running_loop"), S("get_running_loop"), S("_get_running_loop") }));
            for (auto m : { "contextvars", "os", "signal", "socket", "sys", "threading" })
                define("events", m, py{ fn_import(m) });
            define("events", "_event_loop_policy", none());
            define("events", "_lock", M("threading", "Lock")());
            py const runningLoopClass = ev.make_class("_RunningLoop", { M("threading", "local") }, { { "loop_pid", py_tuple({ none(), none() }) } });
            define("events", "_RunningLoop", runningLoopClass);
            define("events", "_running_loop", runningLoopClass());
            // (_asyncio's: the running loop (native))
            auto const& c = writer("_asyncio");
            py const getRunningLoop = c.function("get_running_loop", "get_running_loop", {}, [](arguments) { return get_running_loop(); });
            py const getRunningLoopRaw = c.function("_get_running_loop", "_get_running_loop", {}, [](arguments)
                {
                    auto const loop = running_loop();
                    return loop != nullptr ? py::borrowed(loop) : none();
                });
            py const setRunningLoop = c.function("_set_running_loop", "_set_running_loop", { py_positional_only("loop") }, [](arguments a)
                {
                    set_running_loop(a[0].is_none() ? nullptr : a[0].get());
                    return none();
                });
            py const getEventLoop = c.function("get_event_loop", "get_event_loop", {}, [](arguments) { return get_event_loop(); });
            for (auto const& [name, f] : { std::pair{ "get_running_loop", getRunningLoop }, std::pair{ "_get_running_loop", getRunningLoopRaw },
                std::pair{ "_set_running_loop", setRunningLoop }, std::pair{ "get_event_loop", getEventLoop } })
            {
                define("events", name, f);
                define("events", (std::string{ "_c_" } + (name[0] == '_' ? name : name)).c_str(), f);
                define("_asyncio", name, f);
            }
            // (the Python implementations' aliases: the same functions)
            define("events", "_py__get_running_loop", getRunningLoopRaw);
            define("events", "_py__set_running_loop", setRunningLoop);
            define("events", "_py_get_running_loop", getRunningLoop);
            define("events", "_py_get_event_loop", getEventLoop);
            define("events", "_init_event_loop_policy", ev.function("_init_event_loop_policy", "_init_event_loop_policy", {}, [](arguments)
                {
                    get_event_loop_policy();
                    return none();
                }));
            define("events", "get_event_loop_policy", ev.function("get_event_loop_policy", "get_event_loop_policy", {}, [](arguments)
                {
                    return get_event_loop_policy();
                }));
            define("events", "set_event_loop_policy", ev.function("set_event_loop_policy", "set_event_loop_policy", { py_positional("policy") },
                [](arguments a)
                {
                    if (!a[0].is_none() && !isinstance(a[0], object_of("events.AbstractEventLoopPolicy")))
                        py_raise("TypeError", "policy must be an instance of AbstractEventLoopPolicy or None, not '" + type_of(a[0]).attr("__name__").str() + "'");
                    set_event_loop_policy(a[0].is_none() ? py{} : a[0]);
                    return none();
                }));
            define("events", "set_event_loop", ev.function("set_event_loop", "set_event_loop", { py_positional("loop") }, [](arguments a)
                {
                    get_event_loop_policy().method("set_event_loop", { a[0] });
                    return none();
                }));
            define("events", "new_event_loop", ev.function("new_event_loop", "new_event_loop", {}, [](arguments)
                {
                    return get_event_loop_policy().method("new_event_loop");
                }));
            define("events", "get_child_watcher", ev.function("get_child_watcher", "get_child_watcher", {}, [](arguments)
                {
                    return get_event_loop_policy().method("get_child_watcher");
                }));
            define("events", "set_child_watcher", ev.function("set_child_watcher", "set_child_watcher", { py_positional("watcher") }, [](arguments a)
                {
                    return get_event_loop_policy().method("set_child_watcher", { a[0] });
                }));
        }

        // --- the modules constants, exceptions, log, coroutines, format_helpers, base_futures, base_tasks and mixins

        void make_constants()
        {
            define("constants", "enum", py{ fn_import("enum") });
            define("constants", "LOG_THRESHOLD_FOR_CONNLOST_WRITES", py{ 5 });
            define("constants", "ACCEPT_RETRY_DELAY", py{ 1 });
            define("constants", "DEBUG_STACK_DEPTH", py{ 10 });
            define("constants", "SSL_HANDSHAKE_TIMEOUT", py{ 60.0 });
            define("constants", "SSL_SHUTDOWN_TIMEOUT", py{ 30.0 });
            define("constants", "SENDFILE_FALLBACK_READBUFFER_SIZE", py{ 1024 * 256 });
            define("constants", "FLOW_CONTROL_HIGH_WATER_SSL_READ", py{ 256 });
            define("constants", "FLOW_CONTROL_HIGH_WATER_SSL_WRITE", py{ 512 });
            define("constants", "THREAD_JOIN_TIMEOUT", py{ 300 });
            // (the enum breaks circular dependencies between base_events and sslproto)
            define("constants", "_SendfileMode", M("enum", "Enum").call({ S("_SendfileMode"), S("UNSUPPORTED TRY_NATIVE FALLBACK") },
                { { "module", S("asyncio.constants") }, { "qualname", S("_SendfileMode") } }));
        }

        void make_exceptions()
        {
            auto& s = the_state();
            auto const& ex = writer("exceptions");
            define("exceptions", "__all__", py_tuple({ S("BrokenBarrierError"), S("CancelledError"), S("InvalidStateError"), S("TimeoutError"),
                S("IncompleteReadError"), S("LimitOverrunError"), S("SendfileNotAvailableError") }));
            s.cancelledError = ex.make_class("CancelledError", { builtin("BaseException") }, {});
            define("exceptions", "CancelledError", s.cancelledError);
            define("exceptions", "TimeoutError", builtin("TimeoutError"));
            s.invalidStateError = ex.make_class("InvalidStateError", { builtin("Exception") }, {});
            define("exceptions", "InvalidStateError", s.invalidStateError);
            define("exceptions", "SendfileNotAvailableError", ex.make_class("SendfileNotAvailableError", { builtin("RuntimeError") }, {}));
            define("exceptions", "IncompleteReadError", ex.make_class("IncompleteReadError", { builtin("EOFError") }, {
                { "__init__", ex.method("IncompleteReadError", "__init__", { py_positional("partial"), py_positional("expected") }, [](arguments a)
                    {
                        auto const rExpected = a[2].is_none() ? std::string{ "undefined" } : a[2].repr();
                        builtin("super")(object_of("exceptions.IncompleteReadError"), a[0]).method("__init__",
                            { S((std::to_string(a[1].size()) + " bytes read on a total of " + rExpected + " expected bytes").c_str()) });
                        a[0].set_attr("partial", a[1]);
                        a[0].set_attr("expected", a[2]);
                        return none();
                    }) },
                { "__reduce__", ex.method("IncompleteReadError", "__reduce__", {}, [](arguments a)
                    {
                        return py_tuple({ type_of(a[0]), py_tuple({ a[0].attr("partial"), a[0].attr("expected") }) });
                    }) } }));
            define("exceptions", "LimitOverrunError", ex.make_class("LimitOverrunError", { builtin("Exception") }, {
                { "__init__", ex.method("LimitOverrunError", "__init__", { py_positional("message"), py_positional("consumed") }, [](arguments a)
                    {
                        builtin("super")(object_of("exceptions.LimitOverrunError"), a[0]).method("__init__", { a[1] });
                        a[0].set_attr("consumed", a[2]);
                        return none();
                    }) },
                { "__reduce__", ex.method("LimitOverrunError", "__reduce__", {}, [](arguments a)
                    {
                        return py_tuple({ type_of(a[0]), py_tuple({ a[0].attr("args")[py{ 0 }], a[0].attr("consumed") }) });
                    }) } }));
            define("exceptions", "BrokenBarrierError", ex.make_class("BrokenBarrierError", { builtin("RuntimeError") }, {}));
        }

        void make_coroutines()
        {
            auto const& co = writer("coroutines");
            define("coroutines", "__all__", py_tuple({ S("iscoroutinefunction"), S("iscoroutine") }));
            for (auto m : { "collections", "inspect", "os", "sys", "types" })
                define("coroutines", m, py{ fn_import(m) });
            define("coroutines", "_is_debug_mode", co.function("_is_debug_mode", "_is_debug_mode", {}, [](arguments) { return boolean(is_debug_mode()); }));
            define("coroutines", "_is_coroutine", builtin("object")());
            define("coroutines", "iscoroutinefunction", co.function("iscoroutinefunction", "iscoroutinefunction", { py_positional("func") }, [](arguments a)
                {
                    return boolean(M("inspect", "iscoroutinefunction")(a[0]).truthy() ||
                        get_attr(a[0], "_is_coroutine", none()).is(object_of("coroutines._is_coroutine")));
                }));
            define("coroutines", "_COROUTINE_TYPES", py_tuple({ M("types", "CoroutineType"), M("collections.abc", "Coroutine") }));
            define("coroutines", "_iscoroutine_typecache", builtin("set")());
            define("coroutines", "iscoroutine", co.function("iscoroutine", "iscoroutine", { py_positional("obj") }, [](arguments a)
                {
                    return boolean(is_coroutine(a[0]));
                }));
            define("coroutines", "_format_coroutine", co.function("_format_coroutine", "_format_coroutine", { py_positional("coro") }, [](arguments a)
                {
                    return format_coroutine(a[0]);
                }));
        }

        void make_format_helpers()
        {
            auto const& fh = writer("format_helpers");
            for (auto m : { "functools", "inspect", "reprlib", "sys", "traceback" })
                define("format_helpers", m, py{ fn_import(m) });
            define("format_helpers", "_get_function_source", fh.function("_get_function_source", "_get_function_source", { py_positional("func") },
                [](arguments a) { return get_function_source(a[0]); }));
            define("format_helpers", "_format_callback_source", fh.function("_format_callback_source", "_format_callback_source", { py_positional("func"),
                py_positional("args"), py_keyword("debug", boolean(false)) }, [](arguments a)
                {
                    return S(format_callback_source(a[0], a[1], a[2].truthy()).c_str());
                }));
            define("format_helpers", "_format_args_and_kwargs", fh.function("_format_args_and_kwargs", "_format_args_and_kwargs", { py_positional("args"),
                py_positional("kwargs"), py_keyword("debug", boolean(false)) }, [](arguments a)
                {
                    return S(format_args_and_kwargs(a[0], a[1], a[2].truthy()).c_str());
                }));
            define("format_helpers", "_format_callback", fh.function("_format_callback", "_format_callback", { py_positional("func"), py_positional("args"),
                py_positional("kwargs"), py_keyword("debug", boolean(false)), py_keyword("suffix", S("")) }, [](arguments a)
                {
                    return S(format_callback(a[0], a[1], a[2], a[3].truthy(), a[4].str()).c_str());
                }));
            define("format_helpers", "extract_stack", fh.function("extract_stack", "extract_stack", { py_positional("f", none()), py_positional("limit", none()) },
                [](arguments a) { return extract_stack(a[0], a[1]); }));
        }

        void make_base_futures()
        {
            auto const& bf = writer("base_futures");
            define("base_futures", "__all__", py_tuple());
            define("base_futures", "reprlib", py{ fn_import("reprlib") });
            define("base_futures", "_PENDING", S(PENDING));
            define("base_futures", "_CANCELLED", S(CANCELLED));
            define("base_futures", "_FINISHED", S(FINISHED));
            define("base_futures", "isfuture", bf.function("isfuture", "isfuture", { py_positional("obj") }, [](arguments a)
                {
                    return boolean(is_future(a[0]));
                }));
            define("base_futures", "_format_callbacks", bf.function("_format_callbacks", "_format_callbacks", { py_positional("cb") }, [](arguments a)
                {
                    return S(format_callbacks(a[0]).c_str());
                }));
            define("base_futures", "_future_repr_info", bf.function("_future_repr_info", "_future_repr_info", { py_positional("future") },
                [](arguments a) { return future_repr_info(a[0]); }));
            define("base_futures", "_future_repr", M("reprlib", "recursive_repr")()(bf.function("_future_repr", "_future_repr", { py_positional("future") },
                [](arguments a)
                {
                    py const info = S(" ").method("join", { future_repr_info(a[0]) });
                    return S(("<" + type_of(a[0]).attr("__name__").str() + " " + info.str() + ">").c_str());
                })));
        }

        void make_base_tasks()
        {
            auto const& bt = writer("base_tasks");
            for (auto m : { "linecache", "reprlib", "traceback" })
                define("base_tasks", m, py{ fn_import(m) });
            define("base_tasks", "_task_repr_info", bt.function("_task_repr_info", "_task_repr_info", { py_positional("task") }, [](arguments a)
                {
                    return task_repr_info(a[0]);
                }));
            define("base_tasks", "_task_repr", M("reprlib", "recursive_repr")()(bt.function("_task_repr", "_task_repr", { py_positional("task") },
                [](arguments a)
                {
                    py const info = S(" ").method("join", { task_repr_info(a[0]) });
                    return S(("<" + type_of(a[0]).attr("__name__").str() + " " + info.str() + ">").c_str());
                })));
            define("base_tasks", "_task_get_stack", bt.function("_task_get_stack", "_task_get_stack", { py_positional("task"), py_positional("limit") },
                [](arguments a) { return task_get_stack(a[0], a[1]); }));
            define("base_tasks", "_task_print_stack", bt.function("_task_print_stack", "_task_print_stack", { py_positional("task"), py_positional("limit"),
                py_positional("file") }, [](arguments a)
                {
                    task_print_stack(a[0], a[1], a[2]);
                    return none();
                }));
        }

        void make_log()
        {
            auto& s = the_state();
            define("log", "logging", py{ fn_import("logging") });
            // (the logger is named after the package)
            s.logger = M("logging", "getLogger")(S("asyncio"));
            define("log", "logger", s.logger);
        }

        void make_mixins()
        {
            auto const& mx = writer("mixins");
            define("mixins", "threading", py{ fn_import("threading") });
            define("mixins", "_global_lock", M("threading", "Lock")());
            define("mixins", "_LoopBoundMixin", mx.make_class("_LoopBoundMixin", {}, {
                { "_loop", none() },
                { "_get_loop", mx.method("_LoopBoundMixin", "_get_loop", {}, [](arguments a)
                    {
                        auto const& self = a[0];
                        auto const loop = running_loop();
                        py const running = loop != nullptr ? py::borrowed(loop) : none();
                        if (self.attr("_loop").is_none())
                            with_(object_of("mixins._global_lock"), [&](py const&)
                            {
                                if (self.attr("_loop").is_none())
                                    self.set_attr("_loop", running);
                            });
                        if (!running.is(self.attr("_loop")))
                            py_raise("RuntimeError", self.repr() + " is bound to a different event loop");
                        return running;
                    }) } }));
        }
    }

    // --- the running loop (_asyncio's)

    object* running_loop()
    {
        return tRunningLoop;
    }
    void set_running_loop(object* aLoop)
    {
        if (aLoop != nullptr)
            retain(aLoop);
        auto const old = tRunningLoop;
        tRunningLoop = aLoop;
        release(old);
    }
    py get_running_loop()
    {
        auto const loop = running_loop();
        if (loop == nullptr)
            py_raise("RuntimeError", "no running event loop");
        return py::borrowed(loop);
    }
    py get_event_loop()
    {
        // (in a coroutine or a callback: the running loop)
        if (auto const loop = running_loop())
            return py::borrowed(loop);
        return get_event_loop_policy().method("get_event_loop");
    }

    bool is_future(py const& aObject)
    {
        // (a Future, or an object that advertises itself as duck-type compatible by setting _asyncio_future_blocking)
        auto const& s = the_state();
        if (!s.future.empty() && isinstance(aObject, s.future))
            return true;
        return has_attr(type_of(aObject), "_asyncio_future_blocking") && !aObject.attr("_asyncio_future_blocking").is_none();
    }

    bool is_coroutine(py const& aObject)
    {
        if (fn_is_coroutine(aObject.get()))
            return true;
        py const cache = object_of("coroutines._iscoroutine_typecache");
        py const type = type_of(aObject);
        if (cache.contains(type))
            return true;
        if (isinstance(aObject, object_of("coroutines._COROUTINE_TYPES")))
        {
            // (at most 100 types cached)
            if (cache.size() < 100)
                cache.method("add", { type });
            return true;
        }
        return false;
    }

    py future_loop(py const& aFuture)
    {
        // (its get_loop(), if it has one; otherwise its _loop)
        py getLoop;
        try_except([&]() { getLoop = aFuture.attr("get_loop"); }, builtin("AttributeError"), [&](py const&) {});
        if (!getLoop.empty())
            return getLoop();
        return aFuture.attr("_loop");
    }

    py call_soon(py const& aLoop, py const& aCallback, std::vector<py> const& aArgs, py const& aContext)
    {
        std::vector<py> args{ aCallback };
        args.insert(args.end(), aArgs.begin(), aArgs.end());
        if (aContext.empty())
            return aLoop.attr("call_soon").call(args);
        return aLoop.attr("call_soon").call(args, { { "context", aContext } });
    }

    void make_core()
    {
        make_constants();
        make_exceptions();
        make_log();
        make_coroutines();
        make_format_helpers();
        make_base_futures();
        make_base_tasks();
        make_handles();
        make_abstract_events();
        make_policies();
        make_events_functions();
        make_mixins();
        make_futures_and_tasks();
    }
}
