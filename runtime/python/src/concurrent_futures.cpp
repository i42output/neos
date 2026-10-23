/*
  concurrent_futures.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/concurrent/__init__.py, Lib/concurrent/futures/__init__.py,
  Lib/concurrent/futures/_base.py and Lib/concurrent/futures/thread.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE). Lib/concurrent/futures's copyright: Copyright 2009 Brian Quinlan. All Rights
  Reserved. Licensed to PSF under a Contributor Agreement.
*/

// The package concurrent and its package concurrent.futures (CPython 3.13's Lib/concurrent/futures): the modules _base (Future,
// Executor, wait, as_completed, the exceptions and the waiters) and thread (ThreadPoolExecutor and its workers); their classes are
// made (by type) of closures, so a program's subclasses override their methods. (ProcessPoolExecutor (concurrent.futures.process)
// is not available: concurrent.futures.ProcessPoolExecutor raises AttributeError.)

#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "concurrent_futures_base_docs.hpp"
#include "concurrent_futures_thread_docs.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        using pyclass::S;
        using pyclass::none;
        using pyclass::boolean;
        using pyclass::M;
        using pyclass::callable;
        using pyclass::module_writer;
        using pyclass::try_except;
        using pyclass::try_finally;
        using pyclass::with_;
        using pyclass::format;

        module_writer const sBase{ "concurrent.futures._base", concurrent_futures_base_docs::doc };
        module_writer const sThread{ "concurrent.futures.thread", concurrent_futures_thread_docs::doc };

        // (the module _base's objects: made once (and never destroyed: they outlive the program's objects))
        struct base_state
        {
            py error;
            py cancelledError;
            py invalidStateError;
            py brokenExecutor;
            py logger;
            py waiter;
            py asCompletedWaiter;
            py firstCompletedWaiter;
            py allCompletedWaiter;
            py acquireFutures;
            py doneAndNotDone;
            py future;
            py executor;
            py wait;
            py asCompleted;
            py resultOrCancel;
            py yieldFinishedFutures;
            py createAndInstallWaiters;
            bool made = false;
        };
        base_state& base()
        {
            static base_state* sState = new base_state{};
            return *sState;
        }

        char const* const PENDING = "PENDING";
        char const* const RUNNING = "RUNNING";
        char const* const CANCELLED = "CANCELLED";
        char const* const CANCELLED_AND_NOTIFIED = "CANCELLED_AND_NOTIFIED";
        char const* const FINISHED = "FINISHED";
        char const* const FIRST_COMPLETED = "FIRST_COMPLETED";
        char const* const FIRST_EXCEPTION = "FIRST_EXCEPTION";
        char const* const ALL_COMPLETED = "ALL_COMPLETED";
        char const* const AS_COMPLETED = "_AS_COMPLETED";

        // (a future's _state (a str))
        std::string state_of(py const& aFuture) { return aFuture.attr("_state").str(); }
        bool is_one_of(std::string const& aState, std::initializer_list<char const*> aStates)
        {
            for (auto s : aStates)
                if (aState == s)
                    return true;
            return false;
        }
        py description_of(std::string const& aState)
        {
            if (aState == PENDING) return S("pending");
            if (aState == RUNNING) return S("running");
            if (aState == FINISHED) return S("finished");
            return S("cancelled");
        }

        py exception_with_doc(char const* aQualifiedName, std::vector<std::string> aBases, char const* aName)
        {
            (void)aName;    // (its docstring: none (a builtin exception class's attributes cannot be set))
            return py{ module_exception(aQualifiedName, std::move(aBases)) };
        }

        // (the logger's exception(): in an except block)
        void log_exception(char const* aMessage, py const& aArg)
        {
            base().logger.method("exception", { S(aMessage), aArg });
        }

        // --- the waiters

        void make_waiters()
        {
            auto& s = base();
            s.waiter = sBase.make_class("_Waiter", {}, {
                { "__init__", sBase.method("_Waiter", "__init__", {}, [](arguments a)
                    {
                        a[0].set_attr("event", M("threading", "Event")());
                        a[0].set_attr("finished_futures", py_list());
                        return none();
                    }) },
                { "add_result", sBase.method("_Waiter", "add_result", { py_positional("future") }, [](arguments a)
                    {
                        a[0].attr("finished_futures").method("append", { a[1] });
                        return none();
                    }) },
                { "add_exception", sBase.method("_Waiter", "add_exception", { py_positional("future") }, [](arguments a)
                    {
                        a[0].attr("finished_futures").method("append", { a[1] });
                        return none();
                    }) },
                { "add_cancelled", sBase.method("_Waiter", "add_cancelled", { py_positional("future") }, [](arguments a)
                    {
                        a[0].attr("finished_futures").method("append", { a[1] });
                        return none();
                    }) } });
            // (_AsCompletedWaiter: each addition sets the event (under its lock))
            auto const asCompleted = [](char const* aName)
            {
                return sBase.method("_AsCompletedWaiter", aName, { py_positional("future") }, [aName](arguments a)
                {
                    with_(a[0].attr("lock"), [&](py const&)
                    {
                        builtin("super")(base().asCompletedWaiter, a[0]).method(aName, { a[1] });
                        a[0].attr("event").method("set");
                    });
                    return none();
                });
            };
            s.asCompletedWaiter = sBase.make_class("_AsCompletedWaiter", { s.waiter }, {
                { "__init__", sBase.method("_AsCompletedWaiter", "__init__", {}, [](arguments a)
                    {
                        builtin("super")(base().asCompletedWaiter, a[0]).method("__init__");
                        a[0].set_attr("lock", M("threading", "Lock")());
                        return none();
                    }) },
                { "add_result", asCompleted("add_result") },
                { "add_exception", asCompleted("add_exception") },
                { "add_cancelled", asCompleted("add_cancelled") } });
            auto const firstCompleted = [](char const* aName)
            {
                return sBase.method("_FirstCompletedWaiter", aName, { py_positional("future") }, [aName](arguments a)
                {
                    builtin("super")(base().firstCompletedWaiter, a[0]).method(aName, { a[1] });
                    a[0].attr("event").method("set");
                    return none();
                });
            };
            s.firstCompletedWaiter = sBase.make_class("_FirstCompletedWaiter", { s.waiter }, {
                { "add_result", firstCompleted("add_result") },
                { "add_exception", firstCompleted("add_exception") },
                { "add_cancelled", firstCompleted("add_cancelled") } });
            s.allCompletedWaiter = sBase.make_class("_AllCompletedWaiter", { s.waiter }, {
                { "__init__", sBase.method("_AllCompletedWaiter", "__init__", { py_positional("num_pending_calls"), py_positional("stop_on_exception") },
                    [](arguments a)
                    {
                        a[0].set_attr("num_pending_calls", a[1]);
                        a[0].set_attr("stop_on_exception", a[2]);
                        a[0].set_attr("lock", M("threading", "Lock")());
                        builtin("super")(base().allCompletedWaiter, a[0]).method("__init__");
                        return none();
                    }) },
                { "_decrement_pending_calls", sBase.method("_AllCompletedWaiter", "_decrement_pending_calls", {}, [](arguments a)
                    {
                        with_(a[0].attr("lock"), [&](py const&)
                        {
                            a[0].set_attr("num_pending_calls", a[0].attr("num_pending_calls") - py{ 1 });
                            if (!a[0].attr("num_pending_calls").truthy())
                                a[0].attr("event").method("set");
                        });
                        return none();
                    }) },
                { "add_result", sBase.method("_AllCompletedWaiter", "add_result", { py_positional("future") }, [](arguments a)
                    {
                        builtin("super")(base().allCompletedWaiter, a[0]).method("add_result", { a[1] });
                        a[0].method("_decrement_pending_calls");
                        return none();
                    }) },
                { "add_exception", sBase.method("_AllCompletedWaiter", "add_exception", { py_positional("future") }, [](arguments a)
                    {
                        builtin("super")(base().allCompletedWaiter, a[0]).method("add_exception", { a[1] });
                        if (a[0].attr("stop_on_exception").truthy())
                            a[0].attr("event").method("set");
                        else
                            a[0].method("_decrement_pending_calls");
                        return none();
                    }) },
                { "add_cancelled", sBase.method("_AllCompletedWaiter", "add_cancelled", { py_positional("future") }, [](arguments a)
                    {
                        builtin("super")(base().allCompletedWaiter, a[0]).method("add_cancelled", { a[1] });
                        a[0].method("_decrement_pending_calls");
                        return none();
                    }) } });
            s.acquireFutures = sBase.make_class("_AcquireFutures", {}, {
                { "__init__", sBase.method("_AcquireFutures", "__init__", { py_positional("futures") }, [](arguments a)
                    {
                        a[0].set_attr("futures", builtin("sorted").call({ a[1] }, { { "key", builtin("id") } }));
                        return none();
                    }) },
                { "__enter__", sBase.method("_AcquireFutures", "__enter__", {}, [](arguments a)
                    {
                        for (auto const& future : a[0].attr("futures").items())
                            future.attr("_condition").method("acquire");
                        return none();
                    }) },
                { "__exit__", sBase.method("_AcquireFutures", "__exit__", {}, [](arguments a)
                    {
                        for (auto const& future : a[0].attr("futures").items())
                            future.attr("_condition").method("release");
                        return none();
                    }, true) } });
        }

        py create_and_install_waiters(py const& fs, py const& aReturnWhen)
        {
            auto& s = base();
            py waiter;
            std::string const returnWhen = isinstance(aReturnWhen, builtin("str")) ? aReturnWhen.str() : std::string{};
            if (returnWhen == AS_COMPLETED)
                waiter = s.asCompletedWaiter();
            else if (returnWhen == FIRST_COMPLETED)
                waiter = s.firstCompletedWaiter();
            else
            {
                long long pendingCount = 0;
                for (auto const& f : fs.items())
                    if (!is_one_of(state_of(f), { CANCELLED_AND_NOTIFIED, FINISHED }))
                        ++pendingCount;
                if (returnWhen == FIRST_EXCEPTION)
                    waiter = s.allCompletedWaiter.call({ py{ pendingCount } }, { { "stop_on_exception", boolean(true) } });
                else if (returnWhen == ALL_COMPLETED)
                    waiter = s.allCompletedWaiter.call({ py{ pendingCount } }, { { "stop_on_exception", boolean(false) } });
                else
                    py_raise("ValueError", "Invalid return condition: " + aReturnWhen.repr());
            }
            for (auto const& f : fs.items())
                f.attr("_waiters").method("append", { waiter });
            return waiter;
        }

        // (_yield_finished_futures(fs, waiter, ref_collect): yields the finished futures one by one in reverse order (removing waiter from
        // each one's waiters and the future from each set of ref_collect first))
        void yield_finished_futures(py const& fs, py const& aWaiter, std::vector<py> const& aRefCollect)
        {
            while (fs.truthy())
            {
                {
                    py const f = fs[py{ -1 }];
                    for (auto const& futuresSet : aRefCollect)
                        futuresSet.method("remove", { f });
                    with_(f.attr("_condition"), [&](py const&) { f.attr("_waiters").method("remove", { aWaiter }); });
                }
                py_yield(fs.method("pop"));
            }
        }

        py result_or_cancel(py const& aFuture, py const& aTimeout)
        {
            py result;
            try_finally([&]() { result = aFuture.method("result", { aTimeout }); }, [&]() { aFuture.method("cancel"); });
            return result;
        }

        // --- Future

        void future_invoke_callbacks(py const& self)
        {
            py const callbacks = self.attr("_done_callbacks");
            for (long long i = 0; i < callbacks.size(); ++i)
            {
                py const callback = callbacks[py{ i }];
                try_except([&]() { callback(self); }, builtin("Exception"), [&](py const&) { log_exception("exception calling callback for %r", self); });
            }
        }

        // (__get_result: its exception raised, or its result)
        py future_get_result(py const& self)
        {
            py const exception = self.attr("_exception");
            if (!exception.is_none())
                py_raise(exception);
            return self.attr("_result");
        }

        void make_future()
        {
            auto& s = base();
            s.future = sBase.make_class("Future", {}, {
                { "__init__", sBase.method("Future", "__init__", {}, [](arguments a)
                    {
                        auto const& self = a[0];
                        self.set_attr("_condition", M("threading", "Condition")());
                        self.set_attr("_state", S(PENDING));
                        self.set_attr("_result", none());
                        self.set_attr("_exception", none());
                        self.set_attr("_waiters", py_list());
                        self.set_attr("_done_callbacks", py_list());
                        return none();
                    }) },
                { "_invoke_callbacks", sBase.method("Future", "_invoke_callbacks", {}, [](arguments a)
                    {
                        future_invoke_callbacks(a[0]);
                        return none();
                    }) },
                { "__repr__", sBase.method("Future", "__repr__", {}, [](arguments a)
                    {
                        auto const& self = a[0];
                        py result;
                        with_(self.attr("_condition"), [&](py const&)
                        {
                            auto const state = state_of(self);
                            py const name = type_of(self).attr("__name__");
                            py const id = builtin("id")(self);
                            if (state == FINISHED)
                            {
                                if (self.attr("_exception").truthy())
                                    result = format("<%s at %#x state=%s raised %s>", { name, id, description_of(state), type_of(self.attr("_exception")).attr("__name__") });
                                else
                                    result = format("<%s at %#x state=%s returned %s>", { name, id, description_of(state), type_of(self.attr("_result")).attr("__name__") });
                                return;
                            }
                            result = format("<%s at %#x state=%s>", { name, id, description_of(state) });
                        });
                        return result;
                    }) },
                { "cancel", sBase.method("Future", "cancel", {}, [](arguments a)
                    {
                        auto const& self = a[0];
                        py result;
                        with_(self.attr("_condition"), [&](py const&)
                        {
                            auto const state = state_of(self);
                            if (is_one_of(state, { RUNNING, FINISHED }))
                            {
                                result = boolean(false);
                                return;
                            }
                            if (is_one_of(state, { CANCELLED, CANCELLED_AND_NOTIFIED }))
                            {
                                result = boolean(true);
                                return;
                            }
                            self.set_attr("_state", S(CANCELLED));
                            self.attr("_condition").method("notify_all");
                        });
                        if (!result.empty())
                            return result;
                        self.method("_invoke_callbacks");
                        return boolean(true);
                    }) },
                { "cancelled", sBase.method("Future", "cancelled", {}, [](arguments a)
                    {
                        py result;
                        with_(a[0].attr("_condition"), [&](py const&) { result = boolean(is_one_of(state_of(a[0]), { CANCELLED, CANCELLED_AND_NOTIFIED })); });
                        return result;
                    }) },
                { "running", sBase.method("Future", "running", {}, [](arguments a)
                    {
                        py result;
                        with_(a[0].attr("_condition"), [&](py const&) { result = boolean(state_of(a[0]) == RUNNING); });
                        return result;
                    }) },
                { "done", sBase.method("Future", "done", {}, [](arguments a)
                    {
                        py result;
                        with_(a[0].attr("_condition"), [&](py const&) { result = boolean(is_one_of(state_of(a[0]), { CANCELLED, CANCELLED_AND_NOTIFIED, FINISHED })); });
                        return result;
                    }) },
                { "_Future__get_result", sBase.method("Future", "__get_result", {}, [](arguments a) { return future_get_result(a[0]); }) },
                { "add_done_callback", sBase.method("Future", "add_done_callback", { py_positional("fn") }, [](arguments a)
                    {
                        auto const& self = a[0];
                        bool added = false;
                        with_(self.attr("_condition"), [&](py const&)
                        {
                            if (!is_one_of(state_of(self), { CANCELLED, CANCELLED_AND_NOTIFIED, FINISHED }))
                            {
                                self.attr("_done_callbacks").method("append", { a[1] });
                                added = true;
                            }
                        });
                        if (!added)
                            try_except([&]() { a[1](self); }, builtin("Exception"), [&](py const&) { log_exception("exception calling callback for %r", self); });
                        return none();
                    }) },
                { "result", sBase.method("Future", "result", { py_positional("timeout", none()) }, [](arguments a)
                    {
                        auto const& self = a[0];
                        py result;
                        with_(self.attr("_condition"), [&](py const&)
                        {
                            auto state = state_of(self);
                            if (is_one_of(state, { CANCELLED, CANCELLED_AND_NOTIFIED }))
                                py_raise(base().cancelledError());
                            if (state == FINISHED)
                            {
                                result = self.method("_Future__get_result");
                                return;
                            }
                            self.attr("_condition").method("wait", { a[1] });
                            state = state_of(self);
                            if (is_one_of(state, { CANCELLED, CANCELLED_AND_NOTIFIED }))
                                py_raise(base().cancelledError());
                            if (state == FINISHED)
                            {
                                result = self.method("_Future__get_result");
                                return;
                            }
                            py_raise(builtin("TimeoutError")());
                        });
                        return result;
                    }) },
                { "exception", sBase.method("Future", "exception", { py_positional("timeout", none()) }, [](arguments a)
                    {
                        auto const& self = a[0];
                        py result;
                        with_(self.attr("_condition"), [&](py const&)
                        {
                            auto state = state_of(self);
                            if (is_one_of(state, { CANCELLED, CANCELLED_AND_NOTIFIED }))
                                py_raise(base().cancelledError());
                            if (state == FINISHED)
                            {
                                result = self.attr("_exception");
                                return;
                            }
                            self.attr("_condition").method("wait", { a[1] });
                            state = state_of(self);
                            if (is_one_of(state, { CANCELLED, CANCELLED_AND_NOTIFIED }))
                                py_raise(base().cancelledError());
                            if (state == FINISHED)
                            {
                                result = self.attr("_exception");
                                return;
                            }
                            py_raise(builtin("TimeoutError")());
                        });
                        return result;
                    }) },
                { "set_running_or_notify_cancel", sBase.method("Future", "set_running_or_notify_cancel", {}, [](arguments a)
                    {
                        auto const& self = a[0];
                        py result;
                        with_(self.attr("_condition"), [&](py const&)
                        {
                            auto const state = state_of(self);
                            if (state == CANCELLED)
                            {
                                self.set_attr("_state", S(CANCELLED_AND_NOTIFIED));
                                for (auto const& waiter : self.attr("_waiters").items())
                                    waiter.method("add_cancelled", { self });
                                result = boolean(false);
                                return;
                            }
                            if (state == PENDING)
                            {
                                self.set_attr("_state", S(RUNNING));
                                result = boolean(true);
                                return;
                            }
                            base().logger.method("critical", { S("Future %s in unexpected state: %s"), builtin("id")(self), self.attr("_state") });
                            py_raise("RuntimeError", "Future in unexpected state");
                        });
                        return result;
                    }) },
                { "set_result", sBase.method("Future", "set_result", { py_positional("result") }, [](arguments a)
                    {
                        auto const& self = a[0];
                        with_(self.attr("_condition"), [&](py const&)
                        {
                            if (is_one_of(state_of(self), { CANCELLED, CANCELLED_AND_NOTIFIED, FINISHED }))
                                py_raise(base().invalidStateError(S("{}: {!r}").method("format", { self.attr("_state"), self })));
                            self.set_attr("_result", a[1]);
                            self.set_attr("_state", S(FINISHED));
                            for (auto const& waiter : self.attr("_waiters").items())
                                waiter.method("add_result", { self });
                            self.attr("_condition").method("notify_all");
                        });
                        self.method("_invoke_callbacks");
                        return none();
                    }) },
                { "set_exception", sBase.method("Future", "set_exception", { py_positional("exception") }, [](arguments a)
                    {
                        auto const& self = a[0];
                        with_(self.attr("_condition"), [&](py const&)
                        {
                            if (is_one_of(state_of(self), { CANCELLED, CANCELLED_AND_NOTIFIED, FINISHED }))
                                py_raise(base().invalidStateError(S("{}: {!r}").method("format", { self.attr("_state"), self })));
                            self.set_attr("_exception", a[1]);
                            self.set_attr("_state", S(FINISHED));
                            for (auto const& waiter : self.attr("_waiters").items())
                                waiter.method("add_exception", { self });
                            self.attr("_condition").method("notify_all");
                        });
                        self.method("_invoke_callbacks");
                        return none();
                    }) },
                { "__class_getitem__", builtin("classmethod")(M("types", "GenericAlias")) } });
        }

        // --- Executor

        void make_executor()
        {
            auto& s = base();
            s.executor = sBase.make_class("Executor", {}, {
                { "submit", sBase.function("submit", "Executor.submit", { py_positional_only("self"), py_positional_only("fn") }, [](arguments) -> py
                    {
                        py_raise(builtin("NotImplementedError")());
                    }, true, true) },
                { "map", sBase.method("Executor", "map", { py_positional("fn"), py_keyword("timeout", none()), py_keyword("chunksize", py{ 1 }) }, 
                    [](arguments a)
                    {
                        auto const& self = a[0];
                        py const timeout = a[2];
                        py endTime;
                        if (!timeout.is_none())
                            endTime = timeout + M("time", "monotonic")();
                        py const fs = py_list();
                        py const zipped = builtin("zip").call(a[4].items());
                        for (auto const& item : zipped.items())
                        {
                            std::vector<py> submitArgs{ a[1] };
                            for (auto const& x : item.items())
                                submitArgs.push_back(x);
                            fs.method("append", { self.attr("submit").call(submitArgs) });
                        }
                        // (the futures are submitted before the first value is asked for)
                        return make_native_generator("result_iterator", "Executor.map.<locals>.result_iterator", sBase.module, [fs, timeout, endTime]()
                        {
                            try_finally([&]()
                            {
                                fs.method("reverse");
                                while (fs.truthy())
                                {
                                    if (timeout.is_none())
                                        py_yield(result_or_cancel(fs.method("pop"), none()));
                                    else
                                        py_yield(result_or_cancel(fs.method("pop"), endTime - M("time", "monotonic")()));
                                }
                            }, [&]()
                            {
                                for (auto const& future : fs.items())
                                    future.method("cancel");
                            });
                            return none();
                        });
                    }, true) },
                { "shutdown", sBase.method("Executor", "shutdown", { py_positional("wait", boolean(true)), py_keyword("cancel_futures", boolean(false)) },
                    [](arguments) { return none(); }) },
                { "__enter__", sBase.method("Executor", "__enter__", {}, [](arguments a) { return a[0]; }) },
                { "__exit__", sBase.method("Executor", "__exit__", { py_positional("exc_type"), py_positional("exc_val"), py_positional("exc_tb") },
                    [](arguments a)
                    {
                        a[0].method("shutdown", {}, { { "wait", boolean(true) } });
                        return boolean(false);
                    }) } });
        }

        // --- wait and as_completed

        void make_base_functions()
        {
            auto& s = base();
            s.wait = sBase.function("wait", "wait", { py_positional("fs"), py_positional("timeout", none()), py_positional("return_when", S(ALL_COMPLETED)) },
                [](arguments a)
                {
                    auto& s = base();
                    py const fs = builtin("set")(a[0]);
                    py const timeout = a[1];
                    py const returnWhen = a[2];
                    py done;
                    py notDone;
                    py waiter;
                    py result;
                    with_(s.acquireFutures(fs), [&](py const&)
                    {
                        done = builtin("set")();
                        for (auto const& f : fs.items())
                            if (is_one_of(state_of(f), { CANCELLED_AND_NOTIFIED, FINISHED }))
                                done.method("add", { f });
                        notDone = fs - done;
                        if (returnWhen == S(FIRST_COMPLETED) && done.truthy())
                        {
                            result = s.doneAndNotDone(done, notDone);
                            return;
                        }
                        else if (returnWhen == S(FIRST_EXCEPTION) && done.truthy())
                        {
                            for (auto const& f : done.items())
                                if (!f.method("cancelled").truthy() && !f.method("exception").is_none())
                                {
                                    result = s.doneAndNotDone(done, notDone);
                                    return;
                                }
                        }
                        if (done.size() == fs.size())
                        {
                            result = s.doneAndNotDone(done, notDone);
                            return;
                        }
                        waiter = create_and_install_waiters(fs, returnWhen);
                    });
                    if (!result.empty())
                        return result;
                    waiter.attr("event").method("wait", { timeout });
                    for (auto const& f : fs.items())
                        with_(f.attr("_condition"), [&](py const&) { f.attr("_waiters").method("remove", { waiter }); });
                    done.method("update", { waiter.attr("finished_futures") });
                    return s.doneAndNotDone(done, fs - done);
                });
            s.asCompleted = sBase.function("as_completed", "as_completed", { py_positional("fs"), py_positional("timeout", none()) }, [](arguments a)
                {
                    py const fsArg = a[0];
                    py const timeout = a[1];
                    return make_native_generator("as_completed", "as_completed", sBase.module, [fsArg, timeout]()
                    {
                        auto& s = base();
                        py endTime;
                        if (!timeout.is_none())
                            endTime = timeout + M("time", "monotonic")();
                        py const fs = builtin("set")(fsArg);
                        auto const totalFutures = fs.size();
                        py finished;
                        py pending;
                        py waiter;
                        with_(s.acquireFutures(fs), [&](py const&)
                        {
                            finished = builtin("set")();
                            for (auto const& f : fs.items())
                                if (is_one_of(state_of(f), { CANCELLED_AND_NOTIFIED, FINISHED }))
                                    finished.method("add", { f });
                            pending = fs - finished;
                            waiter = create_and_install_waiters(fs, S(AS_COMPLETED));
                        });
                        finished = builtin("list")(finished);
                        try_finally([&]()
                        {
                            yield_finished_futures(finished, waiter, { fs });
                            while (pending.truthy())
                            {
                                py waitTimeout;
                                if (timeout.is_none())
                                    waitTimeout = none();
                                else
                                {
                                    waitTimeout = endTime - M("time", "monotonic")();
                                    if (waitTimeout < py{ 0 })
                                        py_raise(builtin("TimeoutError")(format("%d (of %d) futures unfinished", { py{ pending.size() }, py{ totalFutures } })));
                                }
                                waiter.attr("event").method("wait", { waitTimeout });
                                py finishedNow;
                                with_(waiter.attr("lock"), [&](py const&)
                                {
                                    finishedNow = waiter.attr("finished_futures");
                                    waiter.set_attr("finished_futures", py_list());
                                    waiter.attr("event").method("clear");
                                });
                                // (reversed: in the order they finished)
                                finishedNow.method("reverse");
                                yield_finished_futures(finishedNow, waiter, { fs, pending });
                            }
                        }, [&]()
                        {
                            // (the waiter removed from the unfinished futures)
                            for (auto const& f : fs.items())
                                with_(f.attr("_condition"), [&](py const&) { f.attr("_waiters").method("remove", { waiter }); });
                        });
                        return none();
                    });
                });
            s.resultOrCancel = sBase.function("_result_or_cancel", "_result_or_cancel", { py_positional("fut"), py_positional("timeout", none()) },
                [](arguments a) { return result_or_cancel(a[0], a[1]); });
            s.createAndInstallWaiters = sBase.function("_create_and_install_waiters", "_create_and_install_waiters", { py_positional("fs"),
                py_positional("return_when") }, [](arguments a) { return create_and_install_waiters(a[0], a[1]); });
            s.yieldFinishedFutures = sBase.function("_yield_finished_futures", "_yield_finished_futures", { py_positional("fs"), py_positional("waiter"),
                py_positional("ref_collect") }, [](arguments a)
                {
                    py const fs = a[0];
                    py const waiter = a[1];
                    py const refCollect = a[2];
                    return make_native_generator("_yield_finished_futures", "_yield_finished_futures", sBase.module, [fs, waiter, refCollect]()
                    {
                        yield_finished_futures(fs, waiter, refCollect.items());
                        return none();
                    });
                });
        }

        void make_base()
        {
            auto& s = base();
            if (s.made)
                return;
            s.made = true;
            s.logger = M("logging", "getLogger")(S("concurrent.futures"));
            s.error = exception_with_doc("concurrent.futures._base.Error", { "Exception" }, "Error");
            s.cancelledError = exception_with_doc("concurrent.futures._base.CancelledError", { "concurrent.futures._base.Error" }, "CancelledError");
            s.invalidStateError = exception_with_doc("concurrent.futures._base.InvalidStateError", { "concurrent.futures._base.Error" }, "InvalidStateError");
            s.brokenExecutor = exception_with_doc("concurrent.futures._base.BrokenExecutor", { "RuntimeError" }, "BrokenExecutor");
            s.doneAndNotDone = M("collections", "namedtuple").call({ S("DoneAndNotDoneFutures"), S("done not_done") }, { { "module", S(sBase.module) } });
            make_waiters();
            make_future();
            make_executor();
            make_base_functions();
        }

        bool base_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            return py_guarded([&]()
            {
                make_base();
                auto& s = base();
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                add("__doc__", none());
                add("__author__", S("Brian Quinlan (brian@sweetapp.com)"));
                add("FIRST_COMPLETED", S(FIRST_COMPLETED));
                add("FIRST_EXCEPTION", S(FIRST_EXCEPTION));
                add("ALL_COMPLETED", S(ALL_COMPLETED));
                add("_AS_COMPLETED", S(AS_COMPLETED));
                add("PENDING", S(PENDING));
                add("RUNNING", S(RUNNING));
                add("CANCELLED", S(CANCELLED));
                add("CANCELLED_AND_NOTIFIED", S(CANCELLED_AND_NOTIFIED));
                add("FINISHED", S(FINISHED));
                add("_FUTURE_STATES", py_list({ S(PENDING), S(RUNNING), S(CANCELLED), S(CANCELLED_AND_NOTIFIED), S(FINISHED) }));
                py const descriptions = py_dict();
                for (auto state : { PENDING, RUNNING, CANCELLED, CANCELLED_AND_NOTIFIED, FINISHED })
                    descriptions.set_item(S(state), description_of(state));
                add("_STATE_TO_DESCRIPTION_MAP", descriptions);
                add("LOGGER", s.logger);
                add("Error", s.error);
                add("CancelledError", s.cancelledError);
                add("TimeoutError", builtin("TimeoutError"));
                add("InvalidStateError", s.invalidStateError);
                add("BrokenExecutor", s.brokenExecutor);
                add("_Waiter", s.waiter);
                add("_AsCompletedWaiter", s.asCompletedWaiter);
                add("_FirstCompletedWaiter", s.firstCompletedWaiter);
                add("_AllCompletedWaiter", s.allCompletedWaiter);
                add("_AcquireFutures", s.acquireFutures);
                add("_create_and_install_waiters", s.createAndInstallWaiters);
                add("_yield_finished_futures", s.yieldFinishedFutures);
                add("as_completed", s.asCompleted);
                add("DoneAndNotDoneFutures", s.doneAndNotDone);
                add("wait", s.wait);
                add("_result_or_cancel", s.resultOrCancel);
                add("Future", s.future);
                add("Executor", s.executor);
                for (auto m : { "collections", "logging", "threading", "time", "types" })
                    add(m, py{ fn_import(m) });
                return none();
            }) != nullptr;
        }

        // --- concurrent.futures.thread

        struct thread_state
        {
            py threadsQueues;           ///< (_threads_queues: a WeakKeyDictionary of the workers' threads and their queues)
            py globalShutdownLock;
            bool shutdown = false;      ///< (_shutdown)
            py workItem;
            py worker;
            py brokenThreadPool;
            py threadPoolExecutor;
            py counter;
            py pythonExit;
            bool made = false;
        };
        thread_state& thread()
        {
            static thread_state* sState = new thread_state{};
            return *sState;
        }

        // (the module's _shutdown: its attribute too)
        void set_shutdown()
        {
            thread().shutdown = true;
            py{ fn_import("concurrent.futures.thread") }.set_attr("_shutdown", boolean(true));
        }

        void make_thread()
        {
            auto& t = thread();
            if (t.made)
                return;
            t.made = true;
            make_base();
            t.threadsQueues = M("weakref", "WeakKeyDictionary")();
            t.globalShutdownLock = M("threading", "Lock")();
            t.pythonExit = sThread.function("_python_exit", "_python_exit", {}, [](arguments)
                {
                    auto& t = thread();
                    with_(t.globalShutdownLock, [&](py const&) { set_shutdown(); });
                    py const items = builtin("list")(t.threadsQueues.method("items"));
                    for (auto const& item : items.items())
                        item[py{ 1 }].method("put", { none() });
                    for (auto const& item : items.items())
                        item[py{ 0 }].method("join");
                    return none();
                });
            // (called before the program's non-daemon threads are joined)
            M("threading", "_register_atexit")(t.pythonExit);
            t.workItem = sThread.make_class("_WorkItem", {}, {
                { "__init__", sThread.method("_WorkItem", "__init__", { py_positional("future"), py_positional("fn"), py_positional("args"), py_positional("kwargs") },
                    [](arguments a)
                    {
                        a[0].set_attr("future", a[1]);
                        a[0].set_attr("fn", a[2]);
                        a[0].set_attr("args", a[3]);
                        a[0].set_attr("kwargs", a[4]);
                        return none();
                    }) },
                { "run", sThread.method("_WorkItem", "run", {}, [](arguments a)
                    {
                        auto const& self = a[0];
                        if (!self.attr("future").method("set_running_or_notify_cancel").truthy())
                            return none();
                        py result;
                        bool failed = false;
                        try_except([&]()
                        {
                            ref const r{ call(self.attr("fn").get(), builtin("tuple")(self.attr("args")).get(), self.attr("kwargs").get()) };
                            result = py{ r };
                        }, builtin("BaseException"), [&](py const& exc)
                        {
                            failed = true;
                            self.attr("future").method("set_exception", { exc });
                        });
                        if (!failed)
                            self.attr("future").method("set_result", { result });
                        return none();
                    }) },
                { "__class_getitem__", builtin("classmethod")(M("types", "GenericAlias")) } });
            t.worker = sThread.function("_worker", "_worker", { py_positional("executor_reference"), py_positional("work_queue"), py_positional("initializer"),
                py_positional("initargs") }, [](arguments a)
                {
                    auto const& executorReference = a[0];
                    auto const& workQueue = a[1];
                    if (!a[2].is_none())
                    {
                        bool failed = false;
                        try_except([&]() { a[2].call(a[3].items()); }, builtin("BaseException"), [&](py const&)
                        {
                            failed = true;
                            M("concurrent.futures._base", "LOGGER").method("critical", { S("Exception in initializer:") }, { { "exc_info", boolean(true) } });
                            py const executor = executorReference();
                            if (!executor.is_none())
                                executor.method("_initializer_failed");
                        });
                        if (failed)
                            return none();
                    }
                    try_except([&]()
                    {
                        for (;;)
                        {
                            py workItem;
                            try_except([&]() { workItem = workQueue.method("get_nowait"); }, M("queue", "Empty"), [&](py const&)
                            {
                                // (the queue is empty: the idle count incremented)
                                {
                                    py const executor = executorReference();
                                    if (!executor.is_none())
                                        executor.attr("_idle_semaphore").method("release");
                                }
                                workItem = workQueue.method("get", {}, { { "block", boolean(true) } });
                            });
                            if (!workItem.is_none())
                            {
                                workItem.method("run");
                                continue;
                            }
                            py const executor = executorReference();
                            // (it exits if the program is ending, the executor has been collected or it has been shut down)
                            if (thread().shutdown || executor.is_none() || executor.attr("_shutdown").truthy())
                            {
                                if (!executor.is_none())
                                    executor.set_attr("_shutdown", boolean(true));
                                // (the other workers notified)
                                workQueue.method("put", { none() });
                                return;
                            }
                        }
                    }, builtin("BaseException"), [&](py const&)
                    {
                        M("concurrent.futures._base", "LOGGER").method("critical", { S("Exception in worker") }, { { "exc_info", boolean(true) } });
                    });
                    return none();
                });
            t.brokenThreadPool = py{ module_exception("concurrent.futures.thread.BrokenThreadPool", { "concurrent.futures._base.BrokenExecutor" }) };
            t.counter = M("itertools", "count")().attr("__next__");
            t.threadPoolExecutor = sThread.make_class("ThreadPoolExecutor", { base().executor }, {
                { "_counter", t.counter },
                { "__init__", sThread.method("ThreadPoolExecutor", "__init__", { py_positional("max_workers", none()), py_positional("thread_name_prefix", S("")),
                    py_positional("initializer", none()), py_positional("initargs", py_tuple()) }, [](arguments a)
                    {
                        auto const& self = a[0];
                        py maxWorkers = a[1];
                        if (maxWorkers.is_none())
                        {
                            // (process_cpu_count + 4, at most 32)
                            py count = M("os", "process_cpu_count")();
                            if (!count.truthy())
                                count = py{ 1 };
                            maxWorkers = builtin("min")(py{ 32 }, count + py{ 4 });
                        }
                        if (maxWorkers <= py{ 0 })
                            py_raise("ValueError", "max_workers must be greater than 0");
                        if (!a[3].is_none() && !callable(a[3]))
                            py_raise("TypeError", "initializer must be a callable");
                        self.set_attr("_max_workers", maxWorkers);
                        self.set_attr("_work_queue", M("queue", "SimpleQueue")());
                        self.set_attr("_idle_semaphore", M("threading", "Semaphore")(py{ 0 }));
                        self.set_attr("_threads", builtin("set")());
                        self.set_attr("_broken", boolean(false));
                        self.set_attr("_shutdown", boolean(false));
                        self.set_attr("_shutdown_lock", M("threading", "Lock")());
                        self.set_attr("_thread_name_prefix", a[2].truthy() ? a[2] : format("ThreadPoolExecutor-%d", { self.attr("_counter")() }));
                        self.set_attr("_initializer", a[3]);
                        self.set_attr("_initargs", a[4]);
                        return none();
                    }) },
                { "submit", [&]()
                    {
                        py const f = sThread.function("submit", "ThreadPoolExecutor.submit", { py_positional_only("self"), py_positional_only("fn") },
                            [](arguments a)
                            {
                                auto const& self = a[0];
                                py result;
                                with_(self.attr("_shutdown_lock"), [&](py const&)
                                {
                                    with_(thread().globalShutdownLock, [&](py const&)
                                    {
                                        if (self.attr("_broken").truthy())
                                            py_raise(thread().brokenThreadPool(self.attr("_broken")));
                                        if (self.attr("_shutdown").truthy())
                                            py_raise("RuntimeError", "cannot schedule new futures after shutdown");
                                        if (thread().shutdown)
                                            py_raise("RuntimeError", "cannot schedule new futures after interpreter shutdown");
                                        py const f = base().future();
                                        py const w = thread().workItem(f, a[1], a[2], a[3]);
                                        self.attr("_work_queue").method("put", { w });
                                        self.method("_adjust_thread_count");
                                        result = f;
                                    });
                                });
                                return result;
                            }, true, true);
                        f.set_attr("__doc__", base().executor.attr("submit").attr("__doc__"));
                        return f;
                    }() },
                { "_adjust_thread_count", sThread.method("ThreadPoolExecutor", "_adjust_thread_count", {}, [](arguments a)
                    {
                        auto const& self = a[0];
                        // (an idle thread: no new thread)
                        if (self.attr("_idle_semaphore").method("acquire", {}, { { "timeout", py{ 0 } } }).truthy())
                            return none();
                        // (the executor collected: the weak reference's callback wakes the workers)
                        py const q = self.attr("_work_queue");
                        py const weakrefCallback = make_native_function("weakref_cb", "ThreadPoolExecutor._adjust_thread_count.<locals>.weakref_cb", "", sThread.module,
                            [q](std::vector<py> const&, object*) { q.method("put", { none() }); return none(); });
                        auto const numThreads = self.attr("_threads").size();
                        if (py{ numThreads } < self.attr("_max_workers"))
                        {
                            py const prefix = self.attr("_thread_name_prefix");
                            py const threadName = format("%s_%d", { prefix.truthy() ? prefix : self, py{ numThreads } });
                            py const t = M("threading", "Thread").call({}, { { "name", threadName }, { "target", thread().worker },
                                { "args", py_tuple({ M("weakref", "ref")(self, weakrefCallback), q, self.attr("_initializer"), self.attr("_initargs") }) } });
                            t.method("start");
                            self.attr("_threads").method("add", { t });
                            thread().threadsQueues.set_item(t, q);
                        }
                        return none();
                    }) },
                { "_initializer_failed", sThread.method("ThreadPoolExecutor", "_initializer_failed", {}, [](arguments a)
                    {
                        auto const& self = a[0];
                        with_(self.attr("_shutdown_lock"), [&](py const&)
                        {
                            self.set_attr("_broken", S("A thread initializer failed, the thread pool is not usable anymore"));
                            // (the work queue drained: its pending futures failed)
                            for (;;)
                            {
                                py workItem;
                                bool empty = false;
                                try_except([&]() { workItem = self.attr("_work_queue").method("get_nowait"); }, M("queue", "Empty"), [&](py const&) { empty = true; });
                                if (empty)
                                    break;
                                if (!workItem.is_none())
                                    workItem.attr("future").method("set_exception", { thread().brokenThreadPool(self.attr("_broken")) });
                            }
                        });
                        return none();
                    }) },
                { "shutdown", [&]()
                    {
                        py const f = sThread.method("ThreadPoolExecutor", "shutdown", { py_positional("wait", boolean(true)), py_keyword("cancel_futures", boolean(false)) },
                            [](arguments a)
                            {
                                auto const& self = a[0];
                                with_(self.attr("_shutdown_lock"), [&](py const&)
                                {
                                    self.set_attr("_shutdown", boolean(true));
                                    if (a[2].truthy())
                                    {
                                        // (the work items drained: their futures cancelled)
                                        for (;;)
                                        {
                                            py workItem;
                                            bool empty = false;
                                            try_except([&]() { workItem = self.attr("_work_queue").method("get_nowait"); }, M("queue", "Empty"),
                                                [&](py const&) { empty = true; });
                                            if (empty)
                                                break;
                                            if (!workItem.is_none())
                                                workItem.attr("future").method("cancel");
                                        }
                                    }
                                    // (a wake-up: the threads calling _work_queue.get(block=True) do not block)
                                    self.attr("_work_queue").method("put", { none() });
                                });
                                if (a[1].truthy())
                                    for (auto const& t : self.attr("_threads").items())
                                        t.method("join");
                                return none();
                            });
                        f.set_attr("__doc__", base().executor.attr("shutdown").attr("__doc__"));
                        return f;
                    }() } });
        }

        bool thread_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            return py_guarded([&]()
            {
                make_thread();
                auto& t = thread();
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                if (auto const d = concurrent_futures_thread_docs::doc(""))
                    add("__doc__", S(d));
                add("__author__", S("Brian Quinlan (brian@sweetapp.com)"));
                add("_base", py{ fn_import("concurrent.futures._base") });
                add("_threads_queues", t.threadsQueues);
                add("_shutdown", boolean(t.shutdown));
                add("_global_shutdown_lock", t.globalShutdownLock);
                add("_python_exit", t.pythonExit);
                add("_WorkItem", t.workItem);
                add("_worker", t.worker);
                add("BrokenThreadPool", t.brokenThreadPool);
                add("ThreadPoolExecutor", t.threadPoolExecutor);
                for (auto m : { "itertools", "queue", "threading", "types", "weakref", "os" })
                    add(m, py{ fn_import(m) });
                return none();
            }) != nullptr;
        }

        // --- concurrent.futures

        bool futures_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            return py_guarded([&]()
            {
                make_base();
                auto& s = base();
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                add("__doc__", S("Execute computations asynchronously using threads or processes."));
                add("__author__", S("Brian Quinlan (brian@sweetapp.com)"));
                add("FIRST_COMPLETED", S(FIRST_COMPLETED));
                add("FIRST_EXCEPTION", S(FIRST_EXCEPTION));
                add("ALL_COMPLETED", S(ALL_COMPLETED));
                add("CancelledError", s.cancelledError);
                add("TimeoutError", builtin("TimeoutError"));
                add("InvalidStateError", s.invalidStateError);
                add("BrokenExecutor", s.brokenExecutor);
                add("Future", s.future);
                add("Executor", s.executor);
                add("wait", s.wait);
                add("as_completed", s.asCompleted);
                py const all = py_tuple({ S("FIRST_COMPLETED"), S("FIRST_EXCEPTION"), S("ALL_COMPLETED"), S("CancelledError"), S("TimeoutError"),
                    S("InvalidStateError"), S("BrokenExecutor"), S("Future"), S("Executor"), S("wait"), S("as_completed"), S("ProcessPoolExecutor"),
                    S("ThreadPoolExecutor") });
                add("__all__", all);
                add("__dir__", make_native_function("__dir__", "__dir__", "", "concurrent.futures", [all](std::vector<py> const&, object*)
                    {
                        return all + py_tuple({ S("__author__"), S("__doc__") });
                    }));
                // (ThreadPoolExecutor: concurrent.futures.thread's (imported when it is first asked for))
                add("__getattr__", make_native_function("__getattr__", "__getattr__", "", "concurrent.futures", [](std::vector<py> const& a, object*) -> py
                    {
                        if (a.size() == 1u && isinstance(a[0], builtin("str")) && a[0].str() == "ThreadPoolExecutor")
                        {
                            py const te = M("concurrent.futures.thread", "ThreadPoolExecutor");
                            py{ fn_import("concurrent.futures") }.set_attr("ThreadPoolExecutor", te);
                            return te;
                        }
                        py_raise("AttributeError", "module 'concurrent.futures' has no attribute " + (a.empty() ? std::string{} : a[0].repr()));
                    }));
                add("_base", py{ fn_import("concurrent.futures._base") });
                return none();
            }) != nullptr;
        }

        inline bool const sConcurrent = register_module("concurrent", [](std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                aAttributes.emplace_back("__doc__", new_reference(detail::none()));
                return true;
            });
        inline bool const sFutures = register_module("concurrent.futures", futures_module);
        inline bool const sFuturesBase = register_module("concurrent.futures._base", base_module);
        inline bool const sFuturesThread = register_module("concurrent.futures.thread", thread_module);
    }
}
