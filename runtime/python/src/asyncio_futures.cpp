/*
  asyncio_futures.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/asyncio/futures.py, Lib/asyncio/tasks.py and
  Modules/_asynciomodule.c), Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under the
  Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from Python and C to C++
  for the neos Python runtime plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// asyncio's modules futures and tasks, and _asyncio: Future and Task (_asyncio's: classes made of closures, as futures.py's and tasks.py's
// are, so that programs derive from them), a future's iterator (native, as _asynciomodule.c's FutureIter is: awaiting a future makes no
// generator), and a task's step (it resumes its coroutine directly (fn_await_step): a coroutine's return makes no StopIteration).

#include <cstring>
#include <memory>
#include "asyncio.hpp"

namespace neos::python_runtime::detail::asyncio
{
    namespace
    {
        char const* const PENDING = "PENDING";
        char const* const CANCELLED = "CANCELLED";
        char const* const FINISHED = "FINISHED";

        std::string state_of(py const& aFuture) { return aFuture.attr("_state").str(); }

        // --- the iterator of a future (_asynciomodule.c's FutureIter: its first step yields the future (if it is not done), its next
        // returns its result (StopIteration))

        struct future_iter_object
        {
            header h;
            object* future = nullptr;
            bool yielded = false;
            ~future_iter_object() { release(future); }
        };

        neos_py_type make_future_iter_type();
        neos_py_type sFutureIterType = make_future_iter_type();

        future_iter_object& as_future_iter(object* aObject) { return *reinterpret_cast<future_iter_object*>(aObject); }

        object* future_iter_next(object* aSelf)
        {
            auto& it = as_future_iter(aSelf);
            if (it.future == nullptr)
                return nullptr;
            bool const awaited = awaiting_value();
            return py_guarded([&]() -> py
            {
                py const fut = py::borrowed(it.future);
                if (!it.yielded)
                {
                    it.yielded = true;
                    if (state_of(fut) == PENDING)
                    {
                        // (it tells the task to wait for its completion)
                        fut.set_attr("_asyncio_future_blocking", boolean(true));
                        return fut;
                    }
                }
                if (state_of(fut) == PENDING)
                    py_raise("RuntimeError", "await wasn't used with future");
                py result = fut.method("result");
                release(it.future);
                it.future = nullptr;
                // (an await: its result given without a StopIteration)
                if (awaited)
                {
                    finish_with_value(result.release());
                    return py{};
                }
                py_raise(builtin("StopIteration")(result));
            });
        }

        neos_py_type make_future_iter_type()
        {
            auto type = make_type("_asyncio.FutureIter", "FutureIter", retain_object, release_object<future_iter_object>);
            type.iter = [](neos_py_context*, object* aSelf) -> object* { return new_reference(aSelf); };
            type.next = [](neos_py_context*, object* aSelf) -> object* { return future_iter_next(aSelf); };
            type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
            {
                std::string_view const name{ aName };
                if (name == "send")
                    return make_function("send", [](neos_py_context*, void*, object* aArgs, object*) -> object*
                    {
                        auto const& args = as<tuple_object>(aArgs)->items;
                        if (args.size() != 2u)
                            return error("TypeError", "send() takes exactly one argument (" + std::to_string(args.size() - 1u) + " given)");
                        if (kind_of(args[1]) != NEOS_PY_NONE)
                            return error("TypeError", "can't send non-None value to a FutureIter");
                        auto const r = future_iter_next(args[0]);
                        return r != nullptr || failed() ? r : error("StopIteration", "");
                    }, nullptr, aSelf);
                if (name == "throw")
                    return make_function("throw", [](neos_py_context*, void*, object* aArgs, object*) -> object*
                    {
                        auto const& args = as<tuple_object>(aArgs)->items;
                        if (args.size() < 2u || args.size() > 4u)
                            return error("TypeError", "throw expected at least 1 argument, got " + std::to_string(args.size() - 1u));
                        auto& it = as_future_iter(args[0]);
                        release(it.future);
                        it.future = nullptr;
                        // (raise type(value) (or the exception))
                        ref exc{ new_reference(args[1]) };
                        if (args.size() >= 3u && kind_of(args[2]) != NEOS_PY_NONE)
                        {
                            exc = ref{ invoke(args[1], { new_reference(args[2]) }) };
                            if (!exc)
                                return nullptr;
                        }
                        fn_raise(exc.get());
                        return nullptr;
                    }, nullptr, aSelf);
                if (name == "close")
                    return make_function("close", [](neos_py_context*, void*, object* aArgs, object*) -> object*
                    {
                        auto& it = as_future_iter(as<tuple_object>(aArgs)->items[0]);
                        release(it.future);
                        it.future = nullptr;
                        return new_reference(detail::none());
                    }, nullptr, aSelf);
                return error("AttributeError", std::string{ "'_asyncio.FutureIter' object has no attribute '" } + aName + "'");
            };
            return type;
        }

        py future_iter(py const& aFuture)
        {
            auto const it = new future_iter_object{ { { &sFutureIterType.base } } };
            it->future = aFuture.reference();
            return py{ &it->h.base };
        }

        // --- Future

        // (__schedule_callbacks: the callbacks scheduled (call_soon), and cleared)
        void future_schedule_callbacks(py const& self)
        {
            py const callbacks = self.attr("_callbacks");
            if (!callbacks.truthy())
                return;
            py const copy = builtin("list")(callbacks);
            callbacks.method("clear");
            py const loop = self.attr("_loop");
            for (auto const& item : copy.items())
            {
                auto const [callback, ctx] = unpack2(item);
                call_soon(loop, callback, { self }, ctx);
            }
        }

        py make_cancelled_error(py const& self)
        {
            // (once only, when the cancellation is handled: it erases the saved exception (its context))
            py const saved = self.attr("_cancelled_exc");
            if (!saved.is_none())
            {
                self.set_attr("_cancelled_exc", none());
                return saved;
            }
            py const message = self.attr("_cancel_message");
            if (message.is_none())
                return the_state().cancelledError();
            return the_state().cancelledError(message);
        }

        bool future_cancel(py const& self, py const& aMessage)
        {
            self.set_attr("_Future__log_traceback", boolean(false));
            if (state_of(self) != PENDING)
                return false;
            self.set_attr("_state", S(CANCELLED));
            self.set_attr("_cancel_message", aMessage);
            future_schedule_callbacks(self);
            return true;
        }

        void future_set_result(py const& self, py const& aResult)
        {
            if (state_of(self) != PENDING)
                py_raise(the_state().invalidStateError(S("invalid state")));
            self.set_attr("_result", aResult);
            self.set_attr("_state", S(FINISHED));
            future_schedule_callbacks(self);
        }

        void future_set_exception(py const& self, py exception)
        {
            if (state_of(self) != PENDING)
                py_raise(the_state().invalidStateError(S("invalid state")));
            if (isinstance(exception, builtin("type")))
                exception = exception();
            if (isinstance(exception, builtin("StopIteration")))
            {
                py const newExc = builtin("RuntimeError")(S("StopIteration interacts badly with generators and cannot be raised into a Future"));
                newExc.set_attr("__cause__", exception);
                newExc.set_attr("__context__", exception);
                exception = newExc;
            }
            self.set_attr("_exception", exception);
            self.set_attr("_exception_tb", exception.attr("__traceback__"));
            self.set_attr("_state", S(FINISHED));
            future_schedule_callbacks(self);
            self.set_attr("_Future__log_traceback", boolean(true));
        }

        void make_future()
        {
            auto& s = the_state();
            auto const& c = writer("_asyncio");
            s.future = c.make_class("Future", {}, {
                // (the class's variables: the defaults of its instances')
                { "_state", S(PENDING) },
                { "_result", none() },
                { "_exception", none() },
                { "_loop", none() },
                { "_source_traceback", none() },
                { "_cancel_message", none() },
                { "_cancelled_exc", none() },
                // (_asyncio_future_blocking: its presence declares that a class implements the Future protocol (a subclass sets it to None to declare
                // that it does not); __await__ sets it, so that a task's step can tell `await future` (correct) from `yield future` (incorrect))
                { "_asyncio_future_blocking", boolean(false) },
                { "_Future__log_traceback", boolean(false) },
                { "__init__", c.method("Future", "__init__", { py_keyword("loop", none()) }, [](arguments a)
                    {
                        auto const& self = a[0];
                        self.set_attr("_loop", a[1].is_none() ? get_event_loop() : a[1]);
                        self.set_attr("_callbacks", py_list());
                        if (self.attr("_loop").method("get_debug").truthy())
                            self.set_attr("_source_traceback", object_of("format_helpers.extract_stack")());
                        return none();
                    }) },
                { "__repr__", c.method("Future", "__repr__", {}, [](arguments a) { return object_of("base_futures._future_repr")(a[0]); }) },
                { "__del__", c.method("Future", "__del__", {}, [](arguments a)
                    {
                        auto const& self = a[0];
                        // (set_exception() was not called, or result() or exception() has retrieved the exception)
                        if (!self.attr("_Future__log_traceback").truthy())
                            return none();
                        py const context = py_dict();
                        context.set_item(S("message"), S((type_of(self).attr("__name__").str() + " exception was never retrieved").c_str()));
                        context.set_item(S("exception"), self.attr("_exception"));
                        context.set_item(S("future"), self);
                        if (self.attr("_source_traceback").truthy())
                            context.set_item(S("source_traceback"), self.attr("_source_traceback"));
                        self.attr("_loop").method("call_exception_handler", { context });
                        return none();
                    }) },
                { "__class_getitem__", builtin("classmethod")(M("types", "GenericAlias")) },
                { "_log_traceback", builtin("property")(c.function("_log_traceback", "Future._log_traceback", { py_positional("self") },
                    [](arguments a) { return a[0].attr("_Future__log_traceback"); }),
                    c.function("_log_traceback", "Future._log_traceback", { py_positional("self"), py_positional("val") }, [](arguments a)
                    {
                        if (a[1].truthy())
                            py_raise("ValueError", "_log_traceback can only be set to False");
                        a[0].set_attr("_Future__log_traceback", boolean(false));
                        return none();
                    })) },
                { "get_loop", c.method("Future", "get_loop", {}, [](arguments a)
                    {
                        py const loop = a[0].attr("_loop");
                        if (loop.is_none())
                            py_raise("RuntimeError", "Future object is not initialized.");
                        return loop;
                    }) },
                { "_make_cancelled_error", c.method("Future", "_make_cancelled_error", {}, [](arguments a) { return make_cancelled_error(a[0]); }) },
                { "cancel", c.method("Future", "cancel", { py_positional("msg", none()) }, [](arguments a)
                    {
                        return boolean(future_cancel(a[0], a[1]));
                    }) },
                { "_Future__schedule_callbacks", c.method("Future", "__schedule_callbacks", {}, [](arguments a)
                    {
                        future_schedule_callbacks(a[0]);
                        return none();
                    }) },
                { "cancelled", c.method("Future", "cancelled", {}, [](arguments a) { return boolean(state_of(a[0]) == CANCELLED); }) },
                { "done", c.method("Future", "done", {}, [](arguments a) { return boolean(state_of(a[0]) != PENDING); }) },
                { "result", c.method("Future", "result", {}, [](arguments a)
                    {
                        auto const& self = a[0];
                        auto const state = state_of(self);
                        if (state == CANCELLED)
                            py_raise(self.method("_make_cancelled_error"));
                        if (state != FINISHED)
                            py_raise(the_state().invalidStateError(S("Result is not set.")));
                        self.set_attr("_Future__log_traceback", boolean(false));
                        py const exception = self.attr("_exception");
                        if (!exception.is_none())
                            py_raise(exception.method("with_traceback", { get_attr(self, "_exception_tb", none()) }));
                        return self.attr("_result");
                    }) },
                { "exception", c.method("Future", "exception", {}, [](arguments a)
                    {
                        auto const& self = a[0];
                        auto const state = state_of(self);
                        if (state == CANCELLED)
                            py_raise(self.method("_make_cancelled_error"));
                        if (state != FINISHED)
                            py_raise(the_state().invalidStateError(S("Exception is not set.")));
                        self.set_attr("_Future__log_traceback", boolean(false));
                        return self.attr("_exception");
                    }) },
                { "add_done_callback", c.method("Future", "add_done_callback", { py_positional("fn"), py_keyword("context", none()) }, [](arguments a)
                    {
                        auto const& self = a[0];
                        if (state_of(self) != PENDING)
                            call_soon(self.attr("_loop"), a[1], { self }, a[2]);
                        else
                            self.attr("_callbacks").method("append", { py_tuple({ a[1], a[2].is_none() ? py{ context_copy_current() } : a[2] }) });
                        return none();
                    }) },
                { "remove_done_callback", c.method("Future", "remove_done_callback", { py_positional("fn") }, [](arguments a)
                    {
                        auto const& self = a[0];
                        py const callbacks = self.attr("_callbacks");
                        py const filtered = py_list();
                        for (auto const& item : callbacks.items())
                            if (item[py{ 0 }] != a[1])
                                filtered.method("append", { item });
                        auto const removed = callbacks.size() - filtered.size();
                        if (removed > 0)
                            callbacks.set_item(builtin("slice")(none()), filtered);
                        return py{ removed };
                    }) },
                { "set_result", c.method("Future", "set_result", { py_positional("result") }, [](arguments a)
                    {
                        future_set_result(a[0], a[1]);
                        return none();
                    }) },
                { "set_exception", c.method("Future", "set_exception", { py_positional("exception") }, [](arguments a)
                    {
                        future_set_exception(a[0], a[1]);
                        return none();
                    }) },
                { "__await__", c.method("Future", "__await__", {}, [](arguments a) { return future_iter(a[0]); }) },
                { "__iter__", c.method("Future", "__iter__", {}, [](arguments a) { return future_iter(a[0]); }) } });
            define("_asyncio", "Future", s.future);
        }

        // --- futures' functions

        py convert_future_exc(py const& exc)
        {
            py const excClass = type_of(exc);
            if (excClass.is(M("concurrent.futures", "CancelledError")))
                return the_state().cancelledError.call(exc.attr("args").items()).method("with_traceback", { exc.attr("__traceback__") });
            if (excClass.is(M("concurrent.futures", "InvalidStateError")))
                return the_state().invalidStateError.call(exc.attr("args").items()).method("with_traceback", { exc.attr("__traceback__") });
            return exc;
        }

        void set_concurrent_future_state(py const& concurrent, py const& source)
        {
            // (a concurrent.futures.Future's state copied from a future)
            if (source.method("cancelled").truthy())
                concurrent.method("cancel");
            if (!concurrent.method("set_running_or_notify_cancel").truthy())
                return;
            py const exception = source.method("exception");
            if (!exception.is_none())
                concurrent.method("set_exception", { convert_future_exc(exception) });
            else
                concurrent.method("set_result", { source.method("result") });
        }

        void copy_future_state(py const& source, py const& dest)
        {
            // (another future's state copied (it may be a concurrent.futures.Future))
            if (dest.method("cancelled").truthy())
                return;
            if (source.method("cancelled").truthy())
                dest.method("cancel");
            else
            {
                py const exception = source.method("exception");
                if (!exception.is_none())
                    dest.method("set_exception", { convert_future_exc(exception) });
                else
                    dest.method("set_result", { source.method("result") });
            }
        }

        void chain_future(py const& source, py const& destination)
        {
            // (when source completes, so does destination (its result or exception copied); destination cancelled: source cancelled too)
            py const concurrentFuture = M("concurrent.futures", "Future");
            if (!is_future(source) && !isinstance(source, concurrentFuture))
                py_raise("TypeError", "A future is required for source argument");
            if (!is_future(destination) && !isinstance(destination, concurrentFuture))
                py_raise("TypeError", "A future is required for destination argument");
            py const sourceLoop = is_future(source) ? future_loop(source) : none();
            py const destLoop = is_future(destination) ? future_loop(destination) : none();
            auto const& f = writer("futures");
            py const setState = closure(f, "_set_state", "_chain_future.<locals>._set_state", [](std::vector<py> const& a, object*)
            {
                if (is_future(a[0]))
                    copy_future_state(a[1], a[0]);
                else
                    set_concurrent_future_state(a[0], a[1]);
                return none();
            });
            py const callCheckCancel = closure(f, "_call_check_cancel", "_chain_future.<locals>._call_check_cancel",
                [source, sourceLoop](std::vector<py> const& a, object*)
                {
                    if (a[0].method("cancelled").truthy())
                    {
                        auto const running = running_loop();
                        if (sourceLoop.is_none() || sourceLoop.get() == running)
                            source.method("cancel");
                        else
                            sourceLoop.method("call_soon_threadsafe", { source.attr("cancel") });
                    }
                    return none();
                });
            py const callSetState = closure(f, "_call_set_state", "_chain_future.<locals>._call_set_state",
                [destination, destLoop, setState](std::vector<py> const& a, object*)
                {
                    if (destination.method("cancelled").truthy() && !destLoop.is_none() && destLoop.method("is_closed").truthy())
                        return none();
                    auto const running = running_loop();
                    if (destLoop.is_none() || destLoop.get() == running)
                        setState(destination, a[0]);
                    else
                    {
                        if (destLoop.method("is_closed").truthy())
                            return none();
                        destLoop.method("call_soon_threadsafe", { setState, destination, a[0] });
                    }
                    return none();
                });
            destination.method("add_done_callback", { callCheckCancel });
            source.method("add_done_callback", { callSetState });
        }

        void make_futures_functions()
        {
            auto& s = the_state();
            auto const& f = writer("futures");
            define("futures", "__all__", py_tuple({ S("Future"), S("wrap_future"), S("isfuture") }));
            for (auto m : { "concurrent", "contextvars", "logging", "sys" })
                define("futures", m, py{ fn_import(m) });
            define("futures", "GenericAlias", M("types", "GenericAlias"));
            define("futures", "isfuture", object_of("base_futures.isfuture"));
            define("futures", "_PENDING", S(PENDING));
            define("futures", "_CANCELLED", S(CANCELLED));
            define("futures", "_FINISHED", S(FINISHED));
            define("futures", "STACK_DEBUG", M("logging", "DEBUG") - py{ 1 });
            define("futures", "Future", s.future);
            define("futures", "_PyFuture", s.future);
            define("futures", "_CFuture", s.future);
            define("futures", "_get_loop", f.function("_get_loop", "_get_loop", { py_positional("fut") }, [](arguments a) { return future_loop(a[0]); }));
            define("futures", "_set_result_unless_cancelled", f.function("_set_result_unless_cancelled", "_set_result_unless_cancelled", { py_positional("fut"),
                py_positional("result") }, [](arguments a)
                {
                    if (!a[0].method("cancelled").truthy())
                        a[0].method("set_result", { a[1] });
                    return none();
                }));
            define("futures", "_convert_future_exc", f.function("_convert_future_exc", "_convert_future_exc", { py_positional("exc") },
                [](arguments a) { return convert_future_exc(a[0]); }));
            define("futures", "_set_concurrent_future_state", f.function("_set_concurrent_future_state", "_set_concurrent_future_state",
                { py_positional("concurrent"), py_positional("source") }, [](arguments a)
                {
                    set_concurrent_future_state(a[0], a[1]);
                    return none();
                }));
            define("futures", "_copy_future_state", f.function("_copy_future_state", "_copy_future_state", { py_positional("source"), py_positional("dest") },
                [](arguments a)
                {
                    copy_future_state(a[0], a[1]);
                    return none();
                }));
            define("futures", "_chain_future", f.function("_chain_future", "_chain_future", { py_positional("source"), py_positional("destination") },
                [](arguments a)
                {
                    chain_future(a[0], a[1]);
                    return none();
                }));
            define("futures", "wrap_future", f.function("wrap_future", "wrap_future", { py_positional("future"), py_keyword("loop", none()) },
                [](arguments a)
                {
                    if (is_future(a[0]))
                        return a[0];
                    if (!isinstance(a[0], M("concurrent.futures", "Future")))
                        py_raise(builtin("AssertionError")(S(("concurrent.futures.Future is expected, got " + a[0].repr()).c_str())));
                    py const loop = a[1].is_none() ? get_event_loop() : a[1];
                    py const newFuture = loop.method("create_future");
                    chain_future(a[0], newFuture);
                    return newFuture;
                }));
        }

        // --- the tasks' registry (_asynciomodule.c's: the tasks scheduled (a WeakSet), the eager tasks (a set), and each loop's current task)

        py dict_get(py const& aDict, py const& aKey)
        {
            auto const position = dict_find(aDict.get(), aKey.get());
            py_checked(position != -2);
            return position >= 0 ? py::borrowed(as<dict_object>(aDict.get())->items[static_cast<std::size_t>(position)].second) : none();
        }

        void enter_task(py const& aLoop, py const& aTask)
        {
            auto const& s = the_state();
            py const current = dict_get(s.currentTasks, aLoop);
            if (!current.is_none())
                py_raise("RuntimeError", "Cannot enter into task " + aTask.repr() + " while another task " + current.repr() + " is being executed.");
            py_checked(dict_set(s.currentTasks.get(), aLoop.get(), aTask.get()) == 0);
        }
        void leave_task(py const& aLoop, py const& aTask)
        {
            auto const& s = the_state();
            py const current = dict_get(s.currentTasks, aLoop);
            if (!current.is(aTask))
                py_raise("RuntimeError", "Leaving task " + aTask.repr() + " does not match the current task " + current.repr() + ".");
            s.currentTasks.method("__delitem__", { aLoop });
        }
        py swap_current_task(py const& aLoop, py const& aTask)
        {
            auto const& s = the_state();
            py const previous = dict_get(s.currentTasks, aLoop);
            if (aTask.is_none())
                s.currentTasks.method("pop", { aLoop, none() });
            else
                py_checked(dict_set(s.currentTasks.get(), aLoop.get(), aTask.get()) == 0);
            return previous;
        }
        void register_task(py const& aTask) { the_state().scheduledTasks.method("add", { aTask }); }
        void unregister_task(py const& aTask) { the_state().scheduledTasks.method("discard", { aTask }); }
        void register_eager_task(py const& aTask) { the_state().eagerTasks.method("add", { aTask }); }
        void unregister_eager_task(py const& aTask) { the_state().eagerTasks.method("discard", { aTask }); }

        // --- Task

        py future_method(char const* aName) { return the_state().future.attr(aName); }

        void task_step_run_and_handle_result(py const& self, py const& aExc);

        void task_step(py const& self, py exc)
        {
            auto& s = the_state();
            if (state_of(self) != PENDING)
                py_raise(s.invalidStateError(S(("_step(): already done: " + self.repr() + " " + (exc.empty() ? std::string{ "None" } : exc.repr())).c_str())));
            if (self.attr("_must_cancel").truthy())
            {
                if (exc.empty() || !isinstance(exc, s.cancelledError))
                    exc = self.method("_make_cancelled_error");
                self.set_attr("_must_cancel", boolean(false));
            }
            self.set_attr("_fut_waiter", none());
            py const loop = self.attr("_loop");
            enter_task(loop, self);
            try_finally([&]() { task_step_run_and_handle_result(self, exc); }, [&]() { leave_task(loop, self); });
        }

        // (a task's step, as a callback (_asynciomodule.c's TaskStepMethWrapper: its __self__ the task), and its argument (an exception, or none))
        struct task_step_object
        {
            header h;
            object* task = nullptr;
            object* arg = nullptr;
            ~task_step_object() { release(task); release(arg); }
        };

        neos_py_type make_task_step_type()
        {
            auto type = make_type("_asyncio.TaskStepMethWrapper", "TaskStepMethWrapper", retain_object, release_object<task_step_object>);
            type.repr = [](neos_py_context*, object* aSelf) -> object* { return make_str("<_asyncio.TaskStepMethWrapper object at " + object_address(aSelf) + ">"); };
            type.call = [](neos_py_context*, object* aSelf, object* aArgs, object* aKwargs) -> object*
            {
                if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                    return error("TypeError", "function takes no keyword arguments");
                if (aArgs != nullptr && !as<tuple_object>(aArgs)->items.empty())
                    return error("TypeError", "function takes no positional arguments");
                auto const& step = *reinterpret_cast<task_step_object*>(aSelf);
                return py_guarded([&]()
                {
                    task_step(py::borrowed(step.task), step.arg != nullptr ? py::borrowed(step.arg) : py{});
                    return none();
                });
            };
            type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
            {
                if (std::string_view{ aName } == "__self__")
                    return new_reference(reinterpret_cast<task_step_object*>(aSelf)->task);
                return error("AttributeError", std::string{ "'_asyncio.TaskStepMethWrapper' object has no attribute '" } + aName + "'");
            };
            return type;
        }
        neos_py_type sTaskStepType = make_task_step_type();

        py step_method(py const& self, py const& aArg = {})
        {
            auto const step = new task_step_object{ { { &sTaskStepType.base } } };
            step->task = self.reference();
            step->arg = aArg.empty() ? nullptr : aArg.reference();
            return py{ &step->h.base };
        }

        // (a task's wakeup, as a future's done callback: a builtin method of the task (_asynciomodule.c's task_wakeup))
        py wakeup_method(py const& self)
        {
            return py{ make_function("task_wakeup", [](neos_py_context*, void*, object* aArgs, object*) -> object*
            {
                auto const& args = as<tuple_object>(aArgs)->items;
                if (args.size() != 2u)
                    return error("TypeError", "task_wakeup() takes exactly one argument (" + std::to_string(args.size() - 1u) + " given)");
                return py_guarded([&]()
                {
                    py const task = py::borrowed(args[0]);
                    // (the future's result is not passed to the step: Future.__await__ does not need it)
                    bool failed = false;
                    try_except([&]() { py::borrowed(args[1]).method("result"); }, builtin("BaseException"), [&](py const& exc)
                    {
                        failed = true;
                        task_step(task, exc);   // (this may be a cancellation)
                    });
                    if (!failed)
                        task_step(task, none());
                    return none();
                });
            }, nullptr, self.get()) };
        }

        void task_step_run_and_handle_result(py const& self, py const& aExc)
        {
            auto& s = the_state();
            py const coro = self.attr("_coro");
            py const context = self.attr("_context");
            py const loop = self.attr("_loop");
            // (send(None), or throw(exc): its coroutine resumed)
            object* value = nullptr;
            auto const r = fn_await_step(coro.get(), nullptr, aExc.empty() || aExc.is_none() ? nullptr : aExc.get(), value);
            if (r == 0)
            {
                py const result{ ref{ value } };
                if (self.attr("_must_cancel").truthy())
                {
                    // (the task was cancelled just before its coroutine stopped)
                    self.set_attr("_must_cancel", boolean(false));
                    future_method("cancel").call({ self }, { { "msg", self.attr("_cancel_message") } });
                }
                else
                    future_method("set_result")(self, result);
                return;
            }
            if (r < 0)
            {
                py const exc = py_take_exception();
                if (isinstance(exc, s.cancelledError))
                {
                    // (the original exception saved, to chain it later)
                    self.set_attr("_cancelled_exc", exc);
                    future_method("cancel")(self);
                }
                else if (isinstance(exc, py_tuple({ builtin("KeyboardInterrupt"), builtin("SystemExit") })))
                {
                    future_method("set_exception")(self, exc);
                    py_reraise(exc);
                }
                else
                    future_method("set_exception")(self, exc);
                return;
            }
            py const result{ ref{ value } };
            py blocking = none();
            if (!result.is_none())
                blocking = get_attr(result, "_asyncio_future_blocking", none());
            auto const reschedule = [&](py const& aNewExc)
            {
                loop.attr("call_soon").call({ step_method(self, aNewExc) }, { { "context", context } });
            };
            if (!blocking.is_none())
            {
                // (a future yielded: it must come from Future.__iter__())
                if (!future_loop(result).is(loop))
                    reschedule(builtin("RuntimeError")(S(("Task " + self.repr() + " got Future " + result.repr() + " attached to a different loop").c_str())));
                else if (blocking.truthy())
                {
                    if (result.is(self))
                        reschedule(builtin("RuntimeError")(S(("Task cannot await on itself: " + self.repr()).c_str())));
                    else
                    {
                        result.set_attr("_asyncio_future_blocking", boolean(false));
                        result.method("add_done_callback", { wakeup_method(self) }, { { "context", context } });
                        self.set_attr("_fut_waiter", result);
                        if (self.attr("_must_cancel").truthy())
                            if (result.method("cancel", {}, { { "msg", self.attr("_cancel_message") } }).truthy())
                                self.set_attr("_must_cancel", boolean(false));
                    }
                }
                else
                    reschedule(builtin("RuntimeError")(S(("yield was used instead of yield from in task " + self.repr() + " with " + result.repr()).c_str())));
            }
            else if (result.is_none())
                reschedule({});     // (a bare yield relinquishes control for one iteration of the loop)
            else if (M("inspect", "isgenerator")(result).truthy())
                reschedule(builtin("RuntimeError")(S(("yield was used instead of yield from for generator in task " + self.repr() + " with " +
                    result.repr()).c_str())));
            else
                reschedule(builtin("RuntimeError")(S(("Task got bad yield: " + result.repr()).c_str())));
        }

        void task_eager_start(py const& self)
        {
            py const loop = self.attr("_loop");
            py const prevTask = swap_current_task(loop, self);
            try_finally([&]()
            {
                register_eager_task(self);
                try_finally([&]()
                {
                    py const run = py{ make_function("__step_run_and_handle_result", [](neos_py_context*, void*, object* aArgs, object*) -> object*
                    {
                        auto const& args = as<tuple_object>(aArgs)->items;
                        return py_guarded([&]()
                        {
                            task_step_run_and_handle_result(py::borrowed(args[0]), py::borrowed(args[1]));
                            return none();
                        });
                    }, nullptr, self.get()) };
                    self.attr("_context").method("run", { run, none() });
                }, [&]() { unregister_eager_task(self); });
            }, [&]()
            {
                try_finally([&]()
                {
                    py const curtask = swap_current_task(loop, prevTask);
                    if (!curtask.is(self))
                        py_raise(builtin("AssertionError")());
                }, [&]()
                {
                    if (self.method("done").truthy())
                        self.set_attr("_coro", none());
                    else
                        register_task(self);
                });
            });
        }

        void make_task()
        {
            auto& s = the_state();
            auto const& c = writer("_asyncio");
            s.task = c.make_class("Task", { s.future }, {
                // (False: no message if the task is destroyed while it is pending)
                { "_log_destroy_pending", boolean(true) },
                { "__init__", c.method("Task", "__init__", { py_positional("coro"), py_keyword("loop", none()), py_keyword("name", none()),
                    py_keyword("context", none()), py_keyword("eager_start", boolean(false)) }, [](arguments a)
                    {
                        auto const& self = a[0];
                        future_method("__init__").call({ self }, { { "loop", a[2] } });
                        if (!is_coroutine(a[1]))
                        {
                            // (raised after Future.__init__(): __del__ needs its attributes; no message for a pending task)
                            self.set_attr("_log_destroy_pending", boolean(false));
                            py_raise("TypeError", "a coroutine was expected, got " + a[1].repr());
                        }
                        if (a[3].is_none())
                            self.set_attr("_name", S(("Task-" + object_of("tasks._task_name_counter")().str()).c_str()));
                        else
                            self.set_attr("_name", builtin("str")(a[3]));
                        self.set_attr("_num_cancels_requested", py{ 0 });
                        self.set_attr("_must_cancel", boolean(false));
                        self.set_attr("_fut_waiter", none());
                        self.set_attr("_coro", a[1]);
                        if (a[4].is_none())
                            self.set_attr("_context", py{ context_copy_current() });
                        else if (!context_is(a[4].get()))
                        {
                            self.set_attr("_log_destroy_pending", boolean(false));
                            py_raise("TypeError", "a contextvars.Context was expected, got " + type_of(a[4]).attr("__name__").str());
                        }
                        else
                            self.set_attr("_context", a[4]);
                        py const loop = self.attr("_loop");
                        if (a[5].truthy() && loop.method("is_running").truthy())
                        {
                            try_except([&]() { task_eager_start(self); }, builtin("BaseException"), [&](py const& e)
                            {
                                self.set_attr("_log_destroy_pending", boolean(false));
                                py_reraise(e);
                            });
                        }
                        else
                        {
                            loop.attr("call_soon").call({ step_method(self) }, { { "context", self.attr("_context") } });
                            register_task(self);
                        }
                        return none();
                    }) },
                { "__del__", c.method("Task", "__del__", {}, [](arguments a)
                    {
                        auto const& self = a[0];
                        if (state_of(self) == PENDING && self.attr("_log_destroy_pending").truthy())
                        {
                            py const context = py_dict();
                            context.set_item(S("task"), self);
                            context.set_item(S("message"), S("Task was destroyed but it is pending!"));
                            if (self.attr("_source_traceback").truthy())
                                context.set_item(S("source_traceback"), self.attr("_source_traceback"));
                            self.attr("_loop").method("call_exception_handler", { context });
                        }
                        future_method("__del__")(self);
                        return none();
                    }) },
                { "__class_getitem__", builtin("classmethod")(M("types", "GenericAlias")) },
                { "__repr__", c.method("Task", "__repr__", {}, [](arguments a) { return object_of("base_tasks._task_repr")(a[0]); }) },
                { "get_coro", c.method("Task", "get_coro", {}, [](arguments a) { return a[0].attr("_coro"); }) },
                { "get_context", c.method("Task", "get_context", {}, [](arguments a) { return a[0].attr("_context"); }) },
                { "get_name", c.method("Task", "get_name", {}, [](arguments a) { return a[0].attr("_name"); }) },
                { "set_name", c.method("Task", "set_name", { py_positional("value") }, [](arguments a)
                    {
                        a[0].set_attr("_name", builtin("str")(a[1]));
                        return none();
                    }) },
                { "set_result", c.method("Task", "set_result", { py_positional("result") }, [](arguments) -> py
                    {
                        py_raise("RuntimeError", "Task does not support set_result operation");
                    }) },
                { "set_exception", c.method("Task", "set_exception", { py_positional("exception") }, [](arguments) -> py
                    {
                        py_raise("RuntimeError", "Task does not support set_exception operation");
                    }) },
                { "get_stack", c.method("Task", "get_stack", { py_keyword("limit", none()) }, [](arguments a)
                    {
                        return object_of("base_tasks._task_get_stack")(a[0], a[1]);
                    }) },
                { "print_stack", c.method("Task", "print_stack", { py_keyword("limit", none()), py_keyword("file", none()) }, [](arguments a)
                    {
                        return object_of("base_tasks._task_print_stack")(a[0], a[1], a[2]);
                    }) },
                { "cancel", c.method("Task", "cancel", { py_positional("msg", none()) }, [](arguments a)
                    {
                        auto const& self = a[0];
                        self.set_attr("_log_traceback", boolean(false));
                        if (self.method("done").truthy())
                            return boolean(false);
                        self.set_attr("_num_cancels_requested", self.attr("_num_cancels_requested") + py{ 1 });
                        // (its waiter cancelled: it may be a task that catches and ignores the cancellation, so it may have to be cancelled
                        // again later)
                        py const waiter = self.attr("_fut_waiter");
                        if (!waiter.is_none())
                            if (waiter.method("cancel", {}, { { "msg", a[1] } }).truthy())
                                return boolean(true);
                        // (its step is already scheduled)
                        self.set_attr("_must_cancel", boolean(true));
                        self.set_attr("_cancel_message", a[1]);
                        return boolean(true);
                    }) },
                { "cancelling", c.method("Task", "cancelling", {}, [](arguments a) { return a[0].attr("_num_cancels_requested"); }) },
                { "uncancel", c.method("Task", "uncancel", {}, [](arguments a)
                    {
                        auto const& self = a[0];
                        if (self.attr("_num_cancels_requested") > py{ 0 })
                        {
                            self.set_attr("_num_cancels_requested", self.attr("_num_cancels_requested") - py{ 1 });
                            if (self.attr("_num_cancels_requested") == py{ 0 })
                                self.set_attr("_must_cancel", boolean(false));
                        }
                        return self.attr("_num_cancels_requested");
                    }) } });
            define("_asyncio", "Task", s.task);
        }

        // --- tasks' functions

        char const* const FIRST_COMPLETED = "FIRST_COMPLETED";
        char const* const FIRST_EXCEPTION = "FIRST_EXCEPTION";
        char const* const ALL_COMPLETED = "ALL_COMPLETED";

        py release_waiter_function()
        {
            return object_of("tasks._release_waiter");
        }

        // (_wait: the futures waited for (until return_when, or the timeout): (done, pending))
        py wait_impl(py const& fs, py const& timeout, py const& returnWhen, py const& loop)
        {
            py const waiter = loop.method("create_future");
            py timeoutHandle = none();
            if (!timeout.is_none())
                timeoutHandle = loop.method("call_later", { timeout, release_waiter_function(), waiter });
            auto counter = std::make_shared<long long>(fs.size());
            std::string const when = returnWhen.str();
            py const onCompletion = closure(writer("tasks"), "_on_completion", "_wait.<locals>._on_completion",
                [counter, when, waiter, timeoutHandle](std::vector<py> const& a, object*)
                {
                    auto const& f = a[0];
                    --*counter;
                    if (*counter <= 0 || when == FIRST_COMPLETED || (when == FIRST_EXCEPTION && !f.method("cancelled").truthy() &&
                        !f.method("exception").is_none()))
                    {
                        if (!timeoutHandle.is_none())
                            timeoutHandle.method("cancel");
                        if (!waiter.method("done").truthy())
                            waiter.method("set_result", { none() });
                    }
                    return none();
                });
            for (auto const& f : fs.items())
                f.method("add_done_callback", { onCompletion });
            try_finally([&]() { py_await(waiter); }, [&]()
            {
                if (!timeoutHandle.is_none())
                    timeoutHandle.method("cancel");
                for (auto const& f : fs.items())
                    f.method("remove_done_callback", { onCompletion });
            });
            py const done = builtin("set")();
            py const pending = builtin("set")();
            for (auto const& f : fs.items())
                (f.method("done").truthy() ? done : pending).method("add", { f });
            return py_tuple({ done, pending });
        }

        void cancel_and_wait(py const& fut)
        {
            // (the future cancelled, and waited for until it completes (a waiter: so that this is reliably cancellable))
            py const loop = get_running_loop();
            py const waiter = loop.method("create_future");
            py const cb = M("functools", "partial")(release_waiter_function(), waiter);
            fut.method("add_done_callback", { cb });
            try_finally([&]()
            {
                fut.method("cancel");
                py_await(waiter);
            }, [&]() { fut.method("remove_done_callback", { cb }); });
        }

        py sleep_impl(py const& delay, py const& result)
        {
            if (delay <= py{ 0 })
            {
                // (__sleep0: a bare yield (the task's step knows how to handle it): an iteration of the loop skipped)
                py_yield(none());
                return result;
            }
            if (M("math", "isnan")(delay).truthy())
                py_raise("ValueError", "Invalid delay: NaN (not a number)");
            py const loop = get_running_loop();
            py const future = loop.method("create_future");
            py const h = loop.method("call_later", { delay, object_of("futures._set_result_unless_cancelled"), future, result });
            py value;
            try_finally([&]() { value = py_await(future); }, [&]() { h.method("cancel"); });
            return value;
        }

        // (ensure_future)
        py ensure_future_impl(py coroOrFuture, py loop)
        {
            if (is_future(coroOrFuture))
            {
                if (!loop.empty() && !loop.is_none() && !loop.is(future_loop(coroOrFuture)))
                    py_raise("ValueError", "The future belongs to a different loop than the one specified as the loop argument");
                return coroOrFuture;
            }
            bool shouldClose = true;
            if (!is_coroutine(coroOrFuture))
            {
                if (M("inspect", "isawaitable")(coroOrFuture).truthy())
                {
                    py const awaitable = coroOrFuture;
                    coroOrFuture = coroutine(writer("tasks"), "ensure_future.<locals>._wrap_awaitable", [awaitable]() { return py_await(awaitable); });
                    shouldClose = false;
                }
                else
                    py_raise("TypeError", "An asyncio.Future, a coroutine or an awaitable is required");
            }
            if (loop.empty() || loop.is_none())
                loop = get_event_loop();
            py task;
            try_except([&]() { task = loop.method("create_task", { coroOrFuture }); }, builtin("RuntimeError"), [&](py const& e)
            {
                if (shouldClose)
                    coroOrFuture.method("close");
                py_reraise(e);
            });
            return task;
        }

        py gather_impl(std::vector<py> const& coros, bool returnExceptions)
        {
            auto& s = the_state();
            if (coros.empty())
            {
                py const loop = get_event_loop();
                py const outer = loop.method("create_future");
                outer.method("set_result", { py_list() });
                return outer;
            }
            struct gather_state
            {
                py outer;
                py children;
                long long nfuts = 0;
                long long nfinished = 0;
                bool returnExceptions = false;
            };
            auto const g = std::make_shared<gather_state>();
            g->outer = none();      // (bpo-46672)
            g->children = py_list();
            g->returnExceptions = returnExceptions;
            py const doneCallback = closure(writer("tasks"), "_done_callback", "gather.<locals>._done_callback", [g](std::vector<py> const& a, object*)
            {
                auto& s = the_state();
                py const fut = a[0];
                ++g->nfinished;
                if (g->outer.is_none() || g->outer.method("done").truthy())
                {
                    if (!fut.method("cancelled").truthy())
                        fut.method("exception");    // (its exception marked retrieved)
                    return none();
                }
                if (!g->returnExceptions)
                {
                    if (fut.method("cancelled").truthy())
                    {
                        // (cancelled first: its exception() raises a CancelledError instead of returning it)
                        g->outer.method("set_exception", { fut.method("_make_cancelled_error") });
                        return none();
                    }
                    py const exc = fut.method("exception");
                    if (!exc.is_none())
                    {
                        g->outer.method("set_exception", { exc });
                        return none();
                    }
                }
                if (g->nfinished == g->nfuts)
                {
                    // (all the futures are done: their results)
                    py const results = py_list();
                    py last;
                    for (auto const& child : g->children.items())
                    {
                        last = child;
                        py res;
                        if (child.method("cancelled").truthy())
                        {
                            // (its exception() would raise: a CancelledError added to the results instead (with no __context__))
                            py const message = child.attr("_cancel_message");
                            res = s.cancelledError(message.is_none() ? S("") : message);
                        }
                        else
                        {
                            res = child.method("exception");
                            if (res.is_none())
                                res = child.method("result");
                        }
                        results.method("append", { res });
                    }
                    if (g->outer.attr("_cancel_requested").truthy())
                    {
                        // (gather is being cancelled: the cancellation propagated, whatever return_exceptions is (issue 32684))
                        g->outer.method("set_exception", { last.method("_make_cancelled_error") });
                    }
                    else
                        g->outer.method("set_result", { results });
                }
                return none();
            });
            py const argToFut = py_dict();
            py const doneFuts = py_list();
            py loop = none();
            for (auto const& arg : coros)
            {
                py fut;
                if (!argToFut.contains(arg))
                {
                    fut = ensure_future_impl(arg, loop);
                    if (loop.is_none())
                        loop = future_loop(fut);
                    // ('arg' was not a future: a future made for it (which the caller cannot control): no "destroy pending task" warning)
                    if (!fut.is(arg))
                        fut.set_attr("_log_destroy_pending", boolean(false));
                    ++g->nfuts;
                    argToFut.set_item(arg, fut);
                    if (fut.method("done").truthy())
                        doneFuts.method("append", { fut });
                    else
                        fut.method("add_done_callback", { doneCallback });
                }
                else
                    fut = argToFut[arg];    // (a duplicate)
                g->children.method("append", { fut });
            }
            g->outer = object_of("tasks._GatheringFuture").call({ g->children }, { { "loop", loop } });
            // (the done callbacks of the futures that are already done: run once the gathering future has been made)
            for (auto const& fut : doneFuts.items())
                doneCallback(fut);
            (void)s;
            return g->outer;
        }

        py shield_impl(py const& arg)
        {
            py const inner = ensure_future_impl(arg, {});
            if (inner.method("done").truthy())
                return inner;   // (a shortcut)
            py const loop = future_loop(inner);
            py const outer = loop.method("create_future");
            auto const& t = writer("tasks");
            py const innerDoneCallback = closure(t, "_inner_done_callback", "shield.<locals>._inner_done_callback", [outer](std::vector<py> const& a, object*)
            {
                py const inner = a[0];
                if (outer.method("cancelled").truthy())
                {
                    if (!inner.method("cancelled").truthy())
                        inner.method("exception");      // (its result marked retrieved)
                    return none();
                }
                if (inner.method("cancelled").truthy())
                    outer.method("cancel");
                else
                {
                    py const exc = inner.method("exception");
                    if (!exc.is_none())
                        outer.method("set_exception", { exc });
                    else
                        outer.method("set_result", { inner.method("result") });
                }
                return none();
            });
            py const outerDoneCallback = closure(t, "_outer_done_callback", "shield.<locals>._outer_done_callback",
                [inner, innerDoneCallback](std::vector<py> const&, object*)
                {
                    if (!inner.method("done").truthy())
                        inner.method("remove_done_callback", { innerDoneCallback });
                    return none();
                });
            inner.method("add_done_callback", { innerDoneCallback });
            outer.method("add_done_callback", { outerDoneCallback });
            return outer;
        }

        void make_tasks_functions()
        {
            auto& s = the_state();
            auto const& t = writer("tasks");
            auto const& c = writer("_asyncio");
            define("tasks", "__all__", py_tuple({ S("Task"), S("create_task"), S("FIRST_COMPLETED"), S("FIRST_EXCEPTION"), S("ALL_COMPLETED"), S("wait"),
                S("wait_for"), S("as_completed"), S("sleep"), S("gather"), S("shield"), S("ensure_future"), S("run_coroutine_threadsafe"), S("current_task"),
                S("all_tasks"), S("create_eager_task_factory"), S("eager_task_factory"), S("_register_task"), S("_unregister_task"), S("_enter_task"),
                S("_leave_task") }));
            for (auto m : { "concurrent", "contextvars", "functools", "inspect", "itertools", "math", "types", "weakref" })
                define("tasks", m, py{ fn_import(m) });
            define("tasks", "GenericAlias", M("types", "GenericAlias"));
            // (task names: itertools.count (thread-safe))
            define("tasks", "_task_name_counter", M("itertools", "count")(py{ 1 }).attr("__next__"));
            // (the registry)
            s.scheduledTasks = M("weakref", "WeakSet")();
            s.eagerTasks = builtin("set")();
            s.currentTasks = py_dict();
            for (auto m : { "tasks", "_asyncio" })
            {
                define(m, "_scheduled_tasks", s.scheduledTasks);
                define(m, "_eager_tasks", s.eagerTasks);
                define(m, "_current_tasks", s.currentTasks);
            }
            py const currentTask = c.function("current_task", "current_task", { py_positional("loop", none()) }, [](arguments a)
                {
                    py const loop = a[0].is_none() ? get_running_loop() : a[0];
                    return dict_get(the_state().currentTasks, loop);
                });
            py const registerTask = c.function("_register_task", "_register_task", { py_positional("task") }, [](arguments a)
                {
                    register_task(a[0]);
                    return none();
                });
            py const registerEagerTask = c.function("_register_eager_task", "_register_eager_task", { py_positional("task") }, [](arguments a)
                {
                    register_eager_task(a[0]);
                    return none();
                });
            py const unregisterTask = c.function("_unregister_task", "_unregister_task", { py_positional("task") }, [](arguments a)
                {
                    unregister_task(a[0]);
                    return none();
                });
            py const unregisterEagerTask = c.function("_unregister_eager_task", "_unregister_eager_task", { py_positional("task") }, [](arguments a)
                {
                    unregister_eager_task(a[0]);
                    return none();
                });
            py const enterTask = c.function("_enter_task", "_enter_task", { py_positional("loop"), py_positional("task") }, [](arguments a)
                {
                    enter_task(a[0], a[1]);
                    return none();
                });
            py const leaveTask = c.function("_leave_task", "_leave_task", { py_positional("loop"), py_positional("task") }, [](arguments a)
                {
                    leave_task(a[0], a[1]);
                    return none();
                });
            py const swapCurrentTask = c.function("_swap_current_task", "_swap_current_task", { py_positional("loop"), py_positional("task") },
                [](arguments a) { return swap_current_task(a[0], a[1]); });
            for (auto const& [name, f] : { std::pair{ "current_task", currentTask }, std::pair{ "_register_task", registerTask },
                std::pair{ "_register_eager_task", registerEagerTask }, std::pair{ "_unregister_task", unregisterTask },
                std::pair{ "_unregister_eager_task", unregisterEagerTask }, std::pair{ "_enter_task", enterTask }, std::pair{ "_leave_task", leaveTask },
                std::pair{ "_swap_current_task", swapCurrentTask } })
            {
                define("tasks", name, f);
                define("tasks", (std::string{ "_py_" } + name).c_str(), f);
                define("tasks", (std::string{ "_c_" } + name).c_str(), f);
                define("_asyncio", name, f);
            }
            define("tasks", "all_tasks", t.function("all_tasks", "all_tasks", { py_positional("loop", none()) }, [](arguments a)
                {
                    auto& s = the_state();
                    py const loop = a[0].is_none() ? get_running_loop() : a[0];
                    // (the eager tasks first: an eager task that "graduates" to a regular task in another thread is not missed)
                    py const eagerTasks = builtin("list")(s.eagerTasks);
                    py const scheduledTasks = builtin("list")(s.scheduledTasks);
                    py const result = builtin("set")();
                    for (auto const& list : { scheduledTasks, eagerTasks })
                        for (auto const& task : list.items())
                            if (future_loop(task).is(loop) && !task.method("done").truthy())
                                result.method("add", { task });
                    return result;
                }));
            define("tasks", "Task", s.task);
            define("tasks", "_PyTask", s.task);
            define("tasks", "_CTask", s.task);
            define("tasks", "create_task", t.function("create_task", "create_task", { py_positional("coro"), py_keyword("name", none()), py_keyword("context", none()) },
                [](arguments a)
                {
                    py const loop = get_running_loop();
                    // (the legacy API if no context is given)
                    if (a[2].is_none())
                        return loop.method("create_task", { a[0] }, { { "name", a[1] } });
                    return loop.method("create_task", { a[0] }, { { "name", a[1] }, { "context", a[2] } });
                }));
            define("tasks", "FIRST_COMPLETED", S(FIRST_COMPLETED));
            define("tasks", "FIRST_EXCEPTION", S(FIRST_EXCEPTION));
            define("tasks", "ALL_COMPLETED", S(ALL_COMPLETED));
            define("tasks", "wait", async_function(t, "wait", { py_positional("fs"), py_keyword("timeout", none()), py_keyword("return_when", S(ALL_COMPLETED)) },
                [](arguments a)
                {
                    if (is_future(a[0]) || is_coroutine(a[0]))
                        py_raise("TypeError", "expect a list of futures, not " + type_of(a[0]).attr("__name__").str());
                    if (!a[0].truthy())
                        py_raise("ValueError", "Set of Tasks/Futures is empty.");
                    if (!py_tuple({ S(FIRST_COMPLETED), S(FIRST_EXCEPTION), S(ALL_COMPLETED) }).contains(a[2]))
                        py_raise("ValueError", "Invalid return_when value: " + a[2].str());
                    py const fs = builtin("set")(a[0]);
                    for (auto const& f : fs.items())
                        if (is_coroutine(f))
                            py_raise("TypeError", "Passing coroutines is forbidden, use tasks explicitly.");
                    return wait_impl(fs, a[1], a[2], get_running_loop());
                }));
            define("tasks", "_release_waiter", t.function("_release_waiter", "_release_waiter", { py_positional("waiter") }, [](arguments a)
                {
                    if (!a[0].method("done").truthy())
                        a[0].method("set_result", { none() });
                    return none();
                }, true));
            define("tasks", "wait_for", async_function(t, "wait_for", { py_positional("fut"), py_positional("timeout") }, [](arguments a)
                {
                    py fut = a[0];
                    py const timeout = a[1];
                    // (timeout <= 0: the future is not started if it has not been (e.g. a coroutine))
                    if (!timeout.is_none() && timeout <= py{ 0 })
                    {
                        fut = ensure_future_impl(fut, {});
                        if (fut.method("done").truthy())
                            return fut.method("result");
                        cancel_and_wait(fut);
                        py result;
                        try_except([&]() { result = fut.method("result"); }, the_state().cancelledError, [&](py const& exc)
                        {
                            fn_raise_from(builtin("TimeoutError").get(), exc.get());
                            throw raised{};
                        });
                        return result;
                    }
                    py result;
                    async_with(object_of("timeouts.timeout")(timeout), [&](py const&) { result = py_await(fut); });
                    return result;
                }));
            define("tasks", "_wait", async_function(t, "_wait", { py_positional("fs"), py_positional("timeout"), py_positional("return_when"), py_positional("loop") },
                [](arguments a)
                {
                    if (!a[0].truthy())
                        py_raise(builtin("AssertionError")(S("Set of Futures is empty.")));
                    return wait_impl(a[0], a[1], a[2], a[3]);
                }));
            define("tasks", "_cancel_and_wait", async_function(t, "_cancel_and_wait", { py_positional("fut") }, [](arguments a)
                {
                    cancel_and_wait(a[0]);
                    return none();
                }));
            define("tasks", "_AsCompletedIterator", t.make_class("_AsCompletedIterator", {}, {
                { "__init__", t.method("_AsCompletedIterator", "__init__", { py_positional("aws"), py_positional("timeout") }, [](arguments a)
                    {
                        auto const& self = a[0];
                        self.set_attr("_done", object_of("queues.Queue")());
                        self.set_attr("_timeout_handle", none());
                        py const loop = get_event_loop();
                        py const todo = builtin("set")();
                        for (auto const& aw : builtin("set")(a[1]).items())
                            todo.method("add", { ensure_future_impl(aw, loop) });
                        for (auto const& f : todo.items())
                            f.method("add_done_callback", { self.attr("_handle_completion") });
                        if (todo.truthy() && !a[2].is_none())
                            self.set_attr("_timeout_handle", loop.method("call_later", { a[2], self.attr("_handle_timeout") }));
                        self.set_attr("_todo", todo);
                        self.set_attr("_todo_left", py{ todo.size() });
                        return none();
                    }) },
                { "__aiter__", t.method("_AsCompletedIterator", "__aiter__", {}, [](arguments a) { return a[0]; }) },
                { "__iter__", t.method("_AsCompletedIterator", "__iter__", {}, [](arguments a) { return a[0]; }) },
                { "__anext__", async_method(t, "_AsCompletedIterator", "__anext__", {}, [](arguments a)
                    {
                        auto const& self = a[0];
                        if (!self.attr("_todo_left").truthy())
                            py_raise(builtin("StopAsyncIteration")());
                        self.set_attr("_todo_left", self.attr("_todo_left") - py{ 1 });
                        return py_await(self.method("_wait_for_one"));
                    }) },
                { "__next__", t.method("_AsCompletedIterator", "__next__", {}, [](arguments a)
                    {
                        auto const& self = a[0];
                        if (!self.attr("_todo_left").truthy())
                            py_raise(builtin("StopIteration")());
                        self.set_attr("_todo_left", self.attr("_todo_left") - py{ 1 });
                        return self.method("_wait_for_one", {}, { { "resolve", boolean(true) } });
                    }) },
                { "_handle_timeout", t.method("_AsCompletedIterator", "_handle_timeout", {}, [](arguments a)
                    {
                        auto const& self = a[0];
                        for (auto const& f : self.attr("_todo").items())
                        {
                            f.method("remove_done_callback", { self.attr("_handle_completion") });
                            self.attr("_done").method("put_nowait", { none() });    // (a sentinel for _wait_for_one())
                        }
                        self.attr("_todo").method("clear");
                        return none();
                    }) },
                { "_handle_completion", t.method("_AsCompletedIterator", "_handle_completion", { py_positional("f") }, [](arguments a)
                    {
                        auto const& self = a[0];
                        if (!self.attr("_todo").truthy())
                            return none();      // (_handle_timeout() was first)
                        self.attr("_todo").method("remove", { a[1] });
                        self.attr("_done").method("put_nowait", { a[1] });
                        if (!self.attr("_todo").truthy() && !self.attr("_timeout_handle").is_none())
                            self.attr("_timeout_handle").method("cancel");
                        return none();
                    }) },
                { "_wait_for_one", async_method(t, "_AsCompletedIterator", "_wait_for_one", { py_positional("resolve", boolean(false)) }, [](arguments a)
                    {
                        // (the next future to be done (resolve: its result, or its exception raised))
                        py const f = py_await(a[0].attr("_done").method("get"));
                        if (f.is_none())
                            py_raise(builtin("TimeoutError")());    // (_handle_timeout()'s)
                        return a[1].truthy() ? f.method("result") : f;
                    }) } }));
            define("tasks", "as_completed", t.function("as_completed", "as_completed", { py_positional("fs"), py_keyword("timeout", none()) }, [](arguments a)
                {
                    if (M("inspect", "isawaitable")(a[0]).truthy())
                        py_raise("TypeError", "expects an iterable of awaitables, not " + type_of(a[0]).attr("__name__").str());
                    return object_of("tasks._AsCompletedIterator")(a[0], a[1]);
                }));
            define("tasks", "__sleep0", M("types", "coroutine")(t.function("__sleep0", "__sleep0", {}, [](arguments)
                {
                    return make_native_generator("__sleep0", "__sleep0", "asyncio.tasks", []() { py_yield(none()); return none(); });
                })));
            define("tasks", "sleep", async_function(t, "sleep", { py_positional("delay"), py_positional("result", none()) }, [](arguments a)
                {
                    return sleep_impl(a[0], a[1]);
                }));
            define("tasks", "ensure_future", t.function("ensure_future", "ensure_future", { py_positional("coro_or_future"), py_keyword("loop", none()) },
                [](arguments a) { return ensure_future_impl(a[0], a[1]); }));
            define("tasks", "_GatheringFuture", t.make_class("_GatheringFuture", { s.future }, {
                { "__init__", t.method("_GatheringFuture", "__init__", { py_positional("children"), py_keyword("loop") }, [](arguments a)
                    {
                        if (a[2].is_none())
                            py_raise(builtin("AssertionError")());
                        future_method("__init__").call({ a[0] }, { { "loop", a[2] } });
                        a[0].set_attr("_children", a[1]);
                        a[0].set_attr("_cancel_requested", boolean(false));
                        return none();
                    }) },
                { "cancel", t.method("_GatheringFuture", "cancel", { py_positional("msg", none()) }, [](arguments a)
                    {
                        auto const& self = a[0];
                        if (self.method("done").truthy())
                            return boolean(false);
                        bool ret = false;
                        for (auto const& child : self.attr("_children").items())
                            if (child.method("cancel", {}, { { "msg", a[1] } }).truthy())
                                ret = true;
                        // (a child task actually cancelled: the cancellation request propagated, whatever return_exceptions is (issue 32684))
                        if (ret)
                            self.set_attr("_cancel_requested", boolean(true));
                        return boolean(ret);
                    }) } }));
            define("tasks", "gather", t.function("gather", "gather", { py_keyword("return_exceptions", boolean(false)) }, [](arguments a)
                {
                    return gather_impl(a[1].items(), a[0].truthy());
                }, true));
            define("tasks", "shield", t.function("shield", "shield", { py_positional("arg") }, [](arguments a) { return shield_impl(a[0]); }));
            define("tasks", "run_coroutine_threadsafe", t.function("run_coroutine_threadsafe", "run_coroutine_threadsafe", { py_positional("coro"), py_positional("loop") },
                [](arguments a)
                {
                    if (!is_coroutine(a[0]))
                        py_raise("TypeError", "A coroutine object is required");
                    py const future = M("concurrent.futures", "Future")();
                    py const coro = a[0];
                    py const loop = a[1];
                    py const callback = closure(writer("tasks"), "callback", "run_coroutine_threadsafe.<locals>.callback", [coro, loop, future](std::vector<py> const&, object*)
                    {
                        try_except([&]() { chain_future(ensure_future_impl(coro, loop), future); }, builtin("BaseException"), [&](py const& exc)
                        {
                            if (!isinstance(exc, py_tuple({ builtin("SystemExit"), builtin("KeyboardInterrupt") })))
                                if (future.method("set_running_or_notify_cancel").truthy())
                                    future.method("set_exception", { exc });
                            py_reraise(exc);
                        });
                        return none();
                    });
                    loop.method("call_soon_threadsafe", { callback });
                    return future;
                }));
            define("tasks", "create_eager_task_factory", t.function("create_eager_task_factory", "create_eager_task_factory",
                { py_positional("custom_task_constructor") }, [](arguments a)
                {
                    py const constructor = a[0];
                    return writer("tasks").function("factory", "create_eager_task_factory.<locals>.factory", { py_positional("loop"), py_positional("coro"),
                        py_keyword("name", none()), py_keyword("context", none()) }, [constructor](arguments b)
                        {
                            return constructor.call({ b[1] }, { { "loop", b[0] }, { "name", b[2] }, { "context", b[3] }, { "eager_start", boolean(true) } });
                        });
                }));
            define("tasks", "eager_task_factory", object_of("tasks.create_eager_task_factory")(s.task));
            define("_asyncio", "__doc__", S("Accelerator module for asyncio"));
        }
    }

    py ensure_future(py const& aCoroOrFuture, py const& aLoop)
    {
        return ensure_future_impl(aCoroOrFuture, aLoop);
    }

    void make_futures_and_tasks()
    {
        make_future();
        make_futures_functions();
        make_task();
        make_tasks_functions();
    }
}
