/*
  asyncio_sync.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/asyncio/locks.py, Lib/asyncio/queues.py, Lib/asyncio/timeouts.py,
  Lib/asyncio/taskgroups.py, Lib/asyncio/streams.py, Lib/asyncio/staggered.py and Lib/asyncio/subprocess.py), Copyright (c)
  2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2
  (see runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE).
*/

// asyncio's modules locks, queues, timeouts, taskgroups, streams, staggered and subprocess (translated from Python by py2neos, a Python-to-C++ translator).

#include "asyncio.hpp"

namespace neos::python_runtime::detail::asyncio
{
    namespace
    {
        // --- locks (Lib/asyncio/locks.py)

        void make_locks()
        {
            static module_writer const& w = writer("locks");
            define("locks", "__all__", py_tuple({ S<"Lock">(), S<"Event">(), S<"Condition">(), S<"Semaphore">(), S<"BoundedSemaphore">(), S<"Barrier">() }));
            define("locks", "_ContextManagerMixin", w.make_class("_ContextManagerMixin", {  }, {
                { "__aenter__", async_method(w, "_ContextManagerMixin", "__aenter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_await(self.method("acquire"));
                        // We have no use for the "as ..."  clause in the with
                        // statement for locks.
                        return none();
                    }) },
                { "__aexit__", async_method(w, "_ContextManagerMixin", "__aexit__", { py_positional("exc_type"), py_positional("exc"), py_positional("tb") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc_type = a[1];
                        py exc = a[2];
                        py tb = a[3];
                        self.method("release");
                        return none();
                    }) }, }));

            define("locks", "Lock", w.make_class("Lock", { G<"locks._ContextManagerMixin">(), G<"mixins._LoopBoundMixin">() }, {
                { "__init__", w.method("Lock", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_waiters", none());
                        self.set_attr("_locked", boolean(false));
                        return none();
                    }) },
                { "__repr__", w.method("Lock", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py extra;
                        py res;
                        res = (B<"super">()(G<"locks.Lock">(), self)).method("__repr__");
                        extra = (self.attr("_locked").truthy() ? S<"locked">() : S<"unlocked">());
                        if (self.attr("_waiters").truthy())
                        {
                            extra = S(extra.str() + ", waiters:" + py{ self.attr("_waiters").size() }.str());
                        }
                        return S(std::string{ "<" } + res[B<"slice">()(I<1>(), I<-1>(), none())].str() + " [" + extra.str() + "]>");
                    }) },
                { "locked", w.method("Lock", "locked", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_locked");
                    }) },
                { "acquire", async_method(w, "Lock", "acquire", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fut;
                        // Implement fair scheduling, where thread always waits
                        // its turn. Jumping the queue if all are cancelled is an optimization.
                        if (((!self.attr("_locked").truthy()) && ([&]() -> py { py const t = boolean(self.attr("_waiters").is_none()); return t.truthy() ? t : (B<"all">())([&]() -> py { py const result = py_list(); py w; for (py const& item : iterate{ self.attr("_waiters") }) { w = item; result.method("append", { w.method("cancelled") }); } return result; }()); }()).truthy()))
                        {
                            self.set_attr("_locked", boolean(true));
                            return boolean(true);
                        }
                        if (self.attr("_waiters").is_none())
                        {
                            self.set_attr("_waiters", (Mod<"collections">().attr("deque"))());
                        }
                        fut = self.method("_get_loop").method("create_future");
                        self.attr("_waiters").method("append", { fut });
                        try
                        {
                            {
                                std::optional<py> pending2;
                                [[maybe_unused]] int flow2 = 0;
                                [[maybe_unused]] py result2;
                                try
                                {
                                    py_await(fut);
                                }
                                catch (raised const&)
                                {
                                    pending2 = py_take_exception();
                                }
                                self.attr("_waiters").method("remove", { fut });
                                if (pending2)
                                    py_reraise(*pending2);
                                if (flow2 == 1)
                                    return result2;
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler1;
                            if (matches(handler1.exception, G<"exceptions.CancelledError">()))
                            {
                                // Currently the only exception designed be able to occur here.
                                // Ensure the lock invariant: If lock is not claimed (or about
                                // to be claimed by us) and there is a Task in waiters,
                                // ensure that the Task at the head will run.
                                if (!self.attr("_locked").truthy())
                                {
                                    self.method("_wake_up_first");
                                }
                                py_reraise(handler1.exception);
                            }
                            else
                                py_reraise(handler1.exception);
                        }
                        // assert self._locked is False
                        self.set_attr("_locked", boolean(true));
                        return boolean(true);
                    }) },
                { "release", w.method("Lock", "release", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_locked").truthy())
                        {
                            self.set_attr("_locked", boolean(false));
                            self.method("_wake_up_first");
                        }
                        else
                        {
                            py_raise((B<"RuntimeError">())(S<"Lock is not acquired.">()));
                        }
                        return none();
                    }) },
                { "_wake_up_first", w.method("Lock", "_wake_up_first", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fut;
                        if (!self.attr("_waiters").truthy())
                        {
                            return none();
                        }
                        try
                        {
                            fut = (B<"next">())((B<"iter">())(self.attr("_waiters")));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler3;
                            if (matches(handler3.exception, B<"StopIteration">()))
                            {
                                return none();
                            }
                            else
                                py_reraise(handler3.exception);
                        }
                        // .done() means that the waiter is already set to wake up.
                        if (!fut.method("done").truthy())
                        {
                            fut.method("set_result", { boolean(true) });
                        }
                        return none();
                    }) }, }));

            define("locks", "Event", w.make_class("Event", { G<"mixins._LoopBoundMixin">() }, {
                { "__init__", w.method("Event", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_waiters", (Mod<"collections">().attr("deque"))());
                        self.set_attr("_value", boolean(false));
                        return none();
                    }) },
                { "__repr__", w.method("Event", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py extra;
                        py res;
                        res = (B<"super">()(G<"locks.Event">(), self)).method("__repr__");
                        extra = (self.attr("_value").truthy() ? S<"set">() : S<"unset">());
                        if (self.attr("_waiters").truthy())
                        {
                            extra = S(extra.str() + ", waiters:" + py{ self.attr("_waiters").size() }.str());
                        }
                        return S(std::string{ "<" } + res[B<"slice">()(I<1>(), I<-1>(), none())].str() + " [" + extra.str() + "]>");
                    }) },
                { "is_set", w.method("Event", "is_set", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_value");
                    }) },
                { "set", w.method("Event", "set", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fut;
                        if (!self.attr("_value").truthy())
                        {
                            self.set_attr("_value", boolean(true));
                            for (py const& item : iterate{ self.attr("_waiters") })
                            {
                                fut = item;
                                if (!fut.method("done").truthy())
                                {
                                    fut.method("set_result", { boolean(true) });
                                }
                            }
                        }
                        return none();
                    }) },
                { "clear", w.method("Event", "clear", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_value", boolean(false));
                        return none();
                    }) },
                { "wait", async_method(w, "Event", "wait", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fut;
                        if (self.attr("_value").truthy())
                        {
                            return boolean(true);
                        }
                        fut = self.method("_get_loop").method("create_future");
                        self.attr("_waiters").method("append", { fut });
                        {
                            std::optional<py> pending5;
                            [[maybe_unused]] int flow5 = 0;
                            [[maybe_unused]] py result5;
                            try
                            {
                                py_await(fut);
                                result5 = boolean(true);
                                flow5 = 1;
                                goto finally5;
                            }
                            catch (raised const&)
                            {
                                pending5 = py_take_exception();
                            }
                            finally5:
                            self.attr("_waiters").method("remove", { fut });
                            if (pending5)
                                py_reraise(*pending5);
                            if (flow5 == 1)
                                return result5;
                        }
                        return none();
                    }) }, }));

            define("locks", "Condition", w.make_class("Condition", { G<"locks._ContextManagerMixin">(), G<"mixins._LoopBoundMixin">() }, {
                { "__init__", w.method("Condition", "__init__", { py_positional("lock", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py lock = a[1];
                        if (lock.is_none())
                        {
                            lock = (G<"locks.Lock">())();
                        }
                        self.set_attr("_lock", lock);
                        // Export the lock's locked(), acquire() and release() methods.
                        self.set_attr("locked", lock.attr("locked"));
                        self.set_attr("acquire", lock.attr("acquire"));
                        self.set_attr("release", lock.attr("release"));
                        self.set_attr("_waiters", (Mod<"collections">().attr("deque"))());
                        return none();
                    }) },
                { "__repr__", w.method("Condition", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py extra;
                        py res;
                        res = (B<"super">()(G<"locks.Condition">(), self)).method("__repr__");
                        extra = (self.method("locked").truthy() ? S<"locked">() : S<"unlocked">());
                        if (self.attr("_waiters").truthy())
                        {
                            extra = S(extra.str() + ", waiters:" + py{ self.attr("_waiters").size() }.str());
                        }
                        return S(std::string{ "<" } + res[B<"slice">()(I<1>(), I<-1>(), none())].str() + " [" + extra.str() + "]>");
                    }) },
                { "wait", async_method(w, "Condition", "wait", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py e;
                        py err;
                        py fut;
                        if (!self.method("locked").truthy())
                        {
                            py_raise((B<"RuntimeError">())(S<"cannot wait on un-acquired lock">()));
                        }
                        fut = self.method("_get_loop").method("create_future");
                        self.method("release");
                        try
                        {
                            {
                                std::optional<py> pending7;
                                [[maybe_unused]] int flow7 = 0;
                                [[maybe_unused]] py result7;
                                try
                                {
                                    self.attr("_waiters").method("append", { fut });
                                    {
                                        std::optional<py> pending8;
                                        [[maybe_unused]] int flow8 = 0;
                                        [[maybe_unused]] py result8;
                                        try
                                        {
                                            py_await(fut);
                                            result8 = boolean(true);
                                            flow8 = 1;
                                            goto finally8;
                                        }
                                        catch (raised const&)
                                        {
                                            pending8 = py_take_exception();
                                        }
                                        finally8:
                                        self.attr("_waiters").method("remove", { fut });
                                        if (pending8)
                                            py_reraise(*pending8);
                                        if (flow8 == 1)
                                            { result7 = result8; flow7 = 1; goto finally7; }
                                    }
                                }
                                catch (raised const&)
                                {
                                    pending7 = py_take_exception();
                                }
                                finally7:
                                // Must re-acquire lock even if wait is cancelled.
                                // We only catch CancelledError here, since we don't want any
                                // other (fatal) errors with the future to cause us to spin.
                                err = none();
                                while (boolean(true).truthy())
                                {
                                    try
                                    {
                                        py_await(self.method("acquire"));
                                        break;
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler10;
                                        if (matches(handler10.exception, G<"exceptions.CancelledError">()))
                                        {
                                            e = handler10.exception;
                                            err = e;
                                        }
                                        else
                                            py_reraise(handler10.exception);
                                    }
                                }
                                if (!err.is_none())
                                {
                                    {
                                        std::optional<py> pending11;
                                        [[maybe_unused]] int flow11 = 0;
                                        [[maybe_unused]] py result11;
                                        try
                                        {
                                            py_raise(err);
                                        }
                                        catch (raised const&)
                                        {
                                            pending11 = py_take_exception();
                                        }
                                        err = none();
                                        if (pending11)
                                            py_reraise(*pending11);
                                        if (flow11 == 1)
                                            return result11;
                                    }
                                }
                                if (pending7)
                                    py_reraise(*pending7);
                                if (flow7 == 1)
                                    return result7;
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler6;
                            if (matches(handler6.exception, B<"BaseException">()))
                            {
                                // Any error raised out of here _may_ have occurred after this Task
                                // believed to have been successfully notified.
                                // Make sure to notify another Task instead.  This may result
                                // in a "spurious wakeup", which is allowed as part of the
                                // Condition Variable protocol.
                                self.method("_notify", { I<1>() });
                                py_reraise(handler6.exception);
                            }
                            else
                                py_reraise(handler6.exception);
                        }
                        return none();
                    }) },
                { "wait_for", async_method(w, "Condition", "wait_for", { py_positional("predicate") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py predicate = a[1];
                        py result;
                        result = predicate();
                        while (!result.truthy())
                        {
                            py_await(self.method("wait"));
                            result = predicate();
                        }
                        return result;
                    }) },
                { "notify", w.method("Condition", "notify", { py_positional("n", I<1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n = a[1];
                        if (!self.method("locked").truthy())
                        {
                            py_raise((B<"RuntimeError">())(S<"cannot notify on un-acquired lock">()));
                        }
                        self.method("_notify", { n });
                        return none();
                    }) },
                { "_notify", w.method("Condition", "_notify", { py_positional("n") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n = a[1];
                        py fut;
                        py idx;
                        idx = I<0>();
                        for (py const& item : iterate{ self.attr("_waiters") })
                        {
                            fut = item;
                            if (idx >= n)
                            {
                                break;
                            }
                            if (!fut.method("done").truthy())
                            {
                                idx = inplace(NEOS_PY_ADD, idx, I<1>());
                                fut.method("set_result", { boolean(false) });
                            }
                        }
                        return none();
                    }) },
                { "notify_all", w.method("Condition", "notify_all", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("notify", { py{ self.attr("_waiters").size() } });
                        return none();
                    }) }, }));

            define("locks", "Semaphore", w.make_class("Semaphore", { G<"locks._ContextManagerMixin">(), G<"mixins._LoopBoundMixin">() }, {
                { "__init__", w.method("Semaphore", "__init__", { py_positional("value", I<1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        if (value < (I<0>()))
                        {
                            py_raise((B<"ValueError">())(S<"Semaphore initial value must be >= 0">()));
                        }
                        self.set_attr("_waiters", none());
                        self.set_attr("_value", value);
                        return none();
                    }) },
                { "__repr__", w.method("Semaphore", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py extra;
                        py res;
                        res = (B<"super">()(G<"locks.Semaphore">(), self)).method("__repr__");
                        extra = (self.method("locked").truthy() ? S<"locked">() : S(std::string{ "unlocked, value:" } + self.attr("_value").str()));
                        if (self.attr("_waiters").truthy())
                        {
                            extra = S(extra.str() + ", waiters:" + py{ self.attr("_waiters").size() }.str());
                        }
                        return S(std::string{ "<" } + res[B<"slice">()(I<1>(), I<-1>(), none())].str() + " [" + extra.str() + "]>");
                    }) },
                { "locked", w.method("Semaphore", "locked", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        // Due to state, or FIFO rules (must allow others to run first).
                        return [&]() -> py { py const t = boolean(self.attr("_value") == (I<0>())); return t.truthy() ? t : (B<"any">())([&]() -> py { py const result = py_list(); py w; for (py const& item : iterate{ [&]() -> py { py const t = self.attr("_waiters"); return t.truthy() ? t : py_tuple(); }() }) { w = item; result.method("append", { boolean(!w.method("cancelled").truthy()) }); } return result; }()); }();
                    }) },
                { "acquire", async_method(w, "Semaphore", "acquire", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fut;
                        if (!self.method("locked").truthy())
                        {
                            // Maintain FIFO, wait for others to start even if _value > 0.
                            {
                                py const target14 = self;
                                target14.set_attr("_value", inplace(NEOS_PY_SUBTRACT, target14.attr("_value"), I<1>()));
                            }
                            return boolean(true);
                        }
                        if (self.attr("_waiters").is_none())
                        {
                            self.set_attr("_waiters", (Mod<"collections">().attr("deque"))());
                        }
                        fut = self.method("_get_loop").method("create_future");
                        self.attr("_waiters").method("append", { fut });
                        {
                            std::optional<py> pending15;
                            [[maybe_unused]] int flow15 = 0;
                            [[maybe_unused]] py result15;
                            try
                            {
                                try
                                {
                                    {
                                        std::optional<py> pending16;
                                        [[maybe_unused]] int flow16 = 0;
                                        [[maybe_unused]] py result16;
                                        try
                                        {
                                            py_await(fut);
                                        }
                                        catch (raised const&)
                                        {
                                            pending16 = py_take_exception();
                                        }
                                        self.attr("_waiters").method("remove", { fut });
                                        if (pending16)
                                            py_reraise(*pending16);
                                        if (flow16 == 1)
                                            { result15 = result16; flow15 = 1; goto finally15; }
                                    }
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler15;
                                    if (matches(handler15.exception, G<"exceptions.CancelledError">()))
                                    {
                                        // Currently the only exception designed be able to occur here.
                                        if ((fut.method("done").truthy() && (!fut.method("cancelled").truthy())))
                                        {
                                            // Our Future was successfully set to True via _wake_up_next(),
                                            // but we are not about to successfully acquire(). Therefore we
                                            // must undo the bookkeeping already done and attempt to wake
                                            // up someone else.
                                            {
                                                py const target17 = self;
                                                target17.set_attr("_value", inplace(NEOS_PY_ADD, target17.attr("_value"), I<1>()));
                                            }
                                        }
                                        py_reraise(handler15.exception);
                                    }
                                    else
                                        py_reraise(handler15.exception);
                                }
                            }
                            catch (raised const&)
                            {
                                pending15 = py_take_exception();
                            }
                            finally15:
                            // New waiters may have arrived but had to wait due to FIFO.
                            // Wake up as many as are allowed.
                            while (self.attr("_value") > (I<0>()))
                            {
                                if (!self.method("_wake_up_next").truthy())
                                {
                                    break;
                                }
                            }
                            if (pending15)
                                py_reraise(*pending15);
                            if (flow15 == 1)
                                return result15;
                        }
                        return boolean(true);
                    }) },
                { "release", w.method("Semaphore", "release", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        {
                            py const target19 = self;
                            target19.set_attr("_value", inplace(NEOS_PY_ADD, target19.attr("_value"), I<1>()));
                        }
                        self.method("_wake_up_next");
                        return none();
                    }) },
                { "_wake_up_next", w.method("Semaphore", "_wake_up_next", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fut;
                        if (!self.attr("_waiters").truthy())
                        {
                            return boolean(false);
                        }
                        for (py const& item : iterate{ self.attr("_waiters") })
                        {
                            fut = item;
                            if (!fut.method("done").truthy())
                            {
                                {
                                    py const target21 = self;
                                    target21.set_attr("_value", inplace(NEOS_PY_SUBTRACT, target21.attr("_value"), I<1>()));
                                }
                                fut.method("set_result", { boolean(true) });
                                // `fut` is now `done()` and not `cancelled()`.
                                return boolean(true);
                            }
                        }
                        return boolean(false);
                    }) }, }));

            define("locks", "BoundedSemaphore", w.make_class("BoundedSemaphore", { G<"locks.Semaphore">() }, {
                { "__init__", w.method("BoundedSemaphore", "__init__", { py_positional("value", I<1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        self.set_attr("_bound_value", value);
                        (B<"super">()(G<"locks.BoundedSemaphore">(), self)).method("__init__", { value });
                        return none();
                    }) },
                { "release", w.method("BoundedSemaphore", "release", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_value") >= self.attr("_bound_value"))
                        {
                            py_raise((B<"ValueError">())(S<"BoundedSemaphore released too many times">()));
                        }
                        (B<"super">()(G<"locks.BoundedSemaphore">(), self)).method("release");
                        return none();
                    }) }, }));

            define("locks", "_BarrierState", w.make_class("_BarrierState", { Mod<"enum">().attr("Enum") }, {
                { "FILLING", S<"filling">() },
                { "DRAINING", S<"draining">() },
                { "RESETTING", S<"resetting">() },
                { "BROKEN", S<"broken">() }, }));

            define("locks", "Barrier", w.make_class("Barrier", { G<"mixins._LoopBoundMixin">() }, {
                { "__init__", w.method("Barrier", "__init__", { py_positional("parties") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parties = a[1];
                        if (parties < (I<1>()))
                        {
                            py_raise((B<"ValueError">())(S<"parties must be >= 1">()));
                        }
                        self.set_attr("_cond", (G<"locks.Condition">())());
                        self.set_attr("_parties", parties);
                        self.set_attr("_state", (G<"locks._BarrierState">()).attr("FILLING"));
                        self.set_attr("_count", I<0>());
                        return none();
                    }) },
                { "__repr__", w.method("Barrier", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py extra;
                        py res;
                        res = (B<"super">()(G<"locks.Barrier">(), self)).method("__repr__");
                        extra = S(self.attr("_state").attr("value").str());
                        if (!self.attr("broken").truthy())
                        {
                            extra = inplace(NEOS_PY_ADD, extra, S(std::string{ ", waiters:" } + self.attr("n_waiting").str() + "/" + self.attr("parties").str()));
                        }
                        return S(std::string{ "<" } + res[B<"slice">()(I<1>(), I<-1>(), none())].str() + " [" + extra.str() + "]>");
                    }) },
                { "__aenter__", async_method(w, "Barrier", "__aenter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py_await(self.method("wait"));
                    }) },
                { "__aexit__", async_method(w, "Barrier", "__aexit__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        return none();
                    }, true) },
                { "wait", async_method(w, "Barrier", "wait", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py index;
                        {
                            py const manager22 = self.attr("_cond");
                            py const exit22 = pyclass::special_method(manager22, "__aexit__");
                            py const value22 = py_await(pyclass::special_method(manager22, "__aenter__")(manager22));
                            [[maybe_unused]] int flow22 = 0;
                            [[maybe_unused]] py result22;
                            bool exited22 = false;
                            try
                            {
                                py_await(self.method("_block"));
                                {
                                    std::optional<py> pending23;
                                    [[maybe_unused]] int flow23 = 0;
                                    [[maybe_unused]] py result23;
                                    try
                                    {
                                        index = self.attr("_count");
                                        {
                                            py const target24 = self;
                                            target24.set_attr("_count", inplace(NEOS_PY_ADD, target24.attr("_count"), I<1>()));
                                        }
                                        if ((index + (I<1>())) == self.attr("_parties"))
                                        {
                                            // We release the barrier
                                            py_await(self.method("_release"));
                                        }
                                        else
                                        {
                                            py_await(self.method("_wait"));
                                        }
                                        result23 = index;
                                        flow23 = 1;
                                        goto finally23;
                                    }
                                    catch (raised const&)
                                    {
                                        pending23 = py_take_exception();
                                    }
                                    finally23:
                                    {
                                        py const target25 = self;
                                        target25.set_attr("_count", inplace(NEOS_PY_SUBTRACT, target25.attr("_count"), I<1>()));
                                    }
                                    // Wake up any tasks waiting for barrier to drain.
                                    self.method("_exit");
                                    if (pending23)
                                        py_reraise(*pending23);
                                    if (flow23 == 1)
                                        { result22 = result23; flow22 = 1; goto finally22; }
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler22;
                                exited22 = true;
                                if (!py_await(exit22(manager22, type_of(handler22.exception), handler22.exception, handler22.exception.attr("__traceback__"))).truthy())
                                    py_reraise(handler22.exception);
                            }
                            finally22:
                            if (!exited22)
                                py_await(exit22(manager22, none(), none(), none()));
                            if (flow22 == 1)
                                return result22;
                        }
                        return none();
                    }) },
                { "_block", async_method(w, "Barrier", "_block", {  }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        py_await((*self).attr("_cond").method("wait_for", { w.function("<lambda>", "Barrier._block.<locals>.<lambda>", {  }, [self](arguments a) -> py
    {
        return boolean(!py_tuple({ (G<"locks._BarrierState">()).attr("DRAINING"), (G<"locks._BarrierState">()).attr("RESETTING") }).contains((*self).attr("_state")));
    }) }));
                        // see if the barrier is in a broken state
                        if ((*self).attr("_state").is((G<"locks._BarrierState">()).attr("BROKEN")))
                        {
                            py_raise((G<"exceptions.BrokenBarrierError">())(S<"Barrier aborted">()));
                        }
                        return none();
                    }) },
                { "_release", async_method(w, "Barrier", "_release", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_state", (G<"locks._BarrierState">()).attr("DRAINING"));
                        self.attr("_cond").method("notify_all");
                        return none();
                    }) },
                { "_wait", async_method(w, "Barrier", "_wait", {  }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        py_await((*self).attr("_cond").method("wait_for", { w.function("<lambda>", "Barrier._wait.<locals>.<lambda>", {  }, [self](arguments a) -> py
    {
        return boolean(!(*self).attr("_state").is((G<"locks._BarrierState">()).attr("FILLING")));
    }) }));
                        if (py_tuple({ (G<"locks._BarrierState">()).attr("BROKEN"), (G<"locks._BarrierState">()).attr("RESETTING") }).contains((*self).attr("_state")))
                        {
                            py_raise((G<"exceptions.BrokenBarrierError">())(S<"Abort or reset of barrier">()));
                        }
                        return none();
                    }) },
                { "_exit", w.method("Barrier", "_exit", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_count") == (I<0>()))
                        {
                            if (py_tuple({ (G<"locks._BarrierState">()).attr("RESETTING"), (G<"locks._BarrierState">()).attr("DRAINING") }).contains(self.attr("_state")))
                            {
                                self.set_attr("_state", (G<"locks._BarrierState">()).attr("FILLING"));
                            }
                            self.attr("_cond").method("notify_all");
                        }
                        return none();
                    }) },
                { "reset", async_method(w, "Barrier", "reset", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        {
                            py const manager26 = self.attr("_cond");
                            py const exit26 = pyclass::special_method(manager26, "__aexit__");
                            py const value26 = py_await(pyclass::special_method(manager26, "__aenter__")(manager26));
                            [[maybe_unused]] int flow26 = 0;
                            [[maybe_unused]] py result26;
                            bool exited26 = false;
                            try
                            {
                                if (self.attr("_count") > (I<0>()))
                                {
                                    if (!self.attr("_state").is((G<"locks._BarrierState">()).attr("RESETTING")))
                                    {
                                        // reset the barrier, waking up tasks
                                        self.set_attr("_state", (G<"locks._BarrierState">()).attr("RESETTING"));
                                    }
                                }
                                else
                                {
                                    self.set_attr("_state", (G<"locks._BarrierState">()).attr("FILLING"));
                                }
                                self.attr("_cond").method("notify_all");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler26;
                                exited26 = true;
                                if (!py_await(exit26(manager26, type_of(handler26.exception), handler26.exception, handler26.exception.attr("__traceback__"))).truthy())
                                    py_reraise(handler26.exception);
                            }
                            if (!exited26)
                                py_await(exit26(manager26, none(), none(), none()));
                            if (flow26 == 1)
                                return result26;
                        }
                        return none();
                    }) },
                { "abort", async_method(w, "Barrier", "abort", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        {
                            py const manager27 = self.attr("_cond");
                            py const exit27 = pyclass::special_method(manager27, "__aexit__");
                            py const value27 = py_await(pyclass::special_method(manager27, "__aenter__")(manager27));
                            [[maybe_unused]] int flow27 = 0;
                            [[maybe_unused]] py result27;
                            bool exited27 = false;
                            try
                            {
                                self.set_attr("_state", (G<"locks._BarrierState">()).attr("BROKEN"));
                                self.attr("_cond").method("notify_all");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler27;
                                exited27 = true;
                                if (!py_await(exit27(manager27, type_of(handler27.exception), handler27.exception, handler27.exception.attr("__traceback__"))).truthy())
                                    py_reraise(handler27.exception);
                            }
                            if (!exited27)
                                py_await(exit27(manager27, none(), none(), none()));
                            if (flow27 == 1)
                                return result27;
                        }
                        return none();
                    }) },
                { "parties", B<"property">()(w.method("Barrier", "parties", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_parties");
                    })) },
                { "n_waiting", B<"property">()(w.method("Barrier", "n_waiting", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_state").is((G<"locks._BarrierState">()).attr("FILLING")))
                        {
                            return self.attr("_count");
                        }
                        return I<0>();
                    })) },
                { "broken", B<"property">()(w.method("Barrier", "broken", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self.attr("_state").is((G<"locks._BarrierState">()).attr("BROKEN")));
                    })) }, }));
        }

        // --- queues (Lib/asyncio/queues.py)

        void make_queues()
        {
            static module_writer const& w = writer("queues");
            define("queues", "__all__", py_tuple({ S<"Queue">(), S<"PriorityQueue">(), S<"LifoQueue">(), S<"QueueFull">(), S<"QueueEmpty">(), S<"QueueShutDown">() }));
            define("queues", "QueueEmpty", w.make_class("QueueEmpty", { B<"Exception">() }, {
 }));

            define("queues", "QueueFull", w.make_class("QueueFull", { B<"Exception">() }, {
 }));

            define("queues", "QueueShutDown", w.make_class("QueueShutDown", { B<"Exception">() }, {
 }));

                // These three are overridable in subclasses.
                // End of the overridable methods.
            define("queues", "Queue", w.make_class("Queue", { G<"mixins._LoopBoundMixin">() }, {
                { "__init__", w.method("Queue", "__init__", { py_positional("maxsize", I<0>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py maxsize = a[1];
                        self.set_attr("_maxsize", maxsize);
                        // Futures.
                        self.set_attr("_getters", (Mod<"collections">().attr("deque"))());
                        // Futures.
                        self.set_attr("_putters", (Mod<"collections">().attr("deque"))());
                        self.set_attr("_unfinished_tasks", I<0>());
                        self.set_attr("_finished", (G<"locks.Event">())());
                        self.attr("_finished").method("set");
                        self.method("_init", { maxsize });
                        self.set_attr("_is_shutdown", boolean(false));
                        return none();
                    }) },
                { "_init", w.method("Queue", "_init", { py_positional("maxsize") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py maxsize = a[1];
                        self.set_attr("_queue", (Mod<"collections">().attr("deque"))());
                        return none();
                    }) },
                { "_get", w.method("Queue", "_get", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_queue").method("popleft");
                    }) },
                { "_put", w.method("Queue", "_put", { py_positional("item") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py item = a[1];
                        self.attr("_queue").method("append", { item });
                        return none();
                    }) },
                { "_wakeup_next", w.method("Queue", "_wakeup_next", { py_positional("waiters") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py waiters = a[1];
                        py waiter;
                        while (waiters.truthy())
                        {
                            waiter = waiters.method("popleft");
                            if (!waiter.method("done").truthy())
                            {
                                waiter.method("set_result", { none() });
                                break;
                            }
                        }
                        return none();
                    }) },
                { "__repr__", w.method("Queue", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return S(std::string{ "<" } + (B<"type">())(self).attr("__name__").str() + " at " + formatted((B<"id">())(self), "#x") + " " + self.method("_format").str() + ">");
                    }) },
                { "__str__", w.method("Queue", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return S(std::string{ "<" } + (B<"type">())(self).attr("__name__").str() + " " + self.method("_format").str() + ">");
                    }) },
                { "__class_getitem__", (B<"classmethod">())(Mod<"types">().attr("GenericAlias")) },
                { "_format", w.method("Queue", "_format", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result;
                        result = S(std::string{ "maxsize=" } + self.attr("_maxsize").repr());
                        if ((B<"getattr">())(self, S<"_queue">(), none()).truthy())
                        {
                            result = inplace(NEOS_PY_ADD, result, S(std::string{ " _queue=" } + (B<"list">())(self.attr("_queue")).repr()));
                        }
                        if (self.attr("_getters").truthy())
                        {
                            result = inplace(NEOS_PY_ADD, result, S(std::string{ " _getters[" } + py{ self.attr("_getters").size() }.str() + "]"));
                        }
                        if (self.attr("_putters").truthy())
                        {
                            result = inplace(NEOS_PY_ADD, result, S(std::string{ " _putters[" } + py{ self.attr("_putters").size() }.str() + "]"));
                        }
                        if (self.attr("_unfinished_tasks").truthy())
                        {
                            result = inplace(NEOS_PY_ADD, result, S(std::string{ " tasks=" } + self.attr("_unfinished_tasks").str()));
                        }
                        if (self.attr("_is_shutdown").truthy())
                        {
                            result = inplace(NEOS_PY_ADD, result, S<" shutdown">());
                        }
                        return result;
                    }) },
                { "qsize", w.method("Queue", "qsize", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py{ self.attr("_queue").size() };
                    }) },
                { "maxsize", B<"property">()(w.method("Queue", "maxsize", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_maxsize");
                    })) },
                { "empty", w.method("Queue", "empty", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(!self.attr("_queue").truthy());
                    }) },
                { "full", w.method("Queue", "full", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_maxsize") <= (I<0>()))
                        {
                            return boolean(false);
                        }
                        else
                        {
                            return boolean(self.method("qsize") >= self.attr("_maxsize"));
                        }
                        return none();
                    }) },
                { "put", async_method(w, "Queue", "put", { py_positional("item") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py item = a[1];
                        py putter;
                        while (self.method("full").truthy())
                        {
                            if (self.attr("_is_shutdown").truthy())
                            {
                                py_raise(G<"queues.QueueShutDown">());
                            }
                            putter = self.method("_get_loop").method("create_future");
                            self.attr("_putters").method("append", { putter });
                            try
                            {
                                py_await(putter);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler3;
                                {
                                    putter.method("cancel");
                                    try
                                    {
                                        // Clean self._putters from canceled putters.
                                        self.attr("_putters").method("remove", { putter });
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler4;
                                        if (matches(handler4.exception, B<"ValueError">()))
                                        {
                                            // The putter could be removed from self._putters by a
                                            // previous get_nowait call or a shutdown call.
                                        }
                                        else
                                            py_reraise(handler4.exception);
                                    }
                                    if (((!self.method("full").truthy()) && (!putter.method("cancelled").truthy())))
                                    {
                                        // We were woken up by get_nowait(), but can't take
                                        // the call.  Wake up the next in line.
                                        self.method("_wakeup_next", { self.attr("_putters") });
                                    }
                                    py_reraise(handler3.exception);
                                }
                            }
                        }
                        return self.method("put_nowait", { item });
                    }) },
                { "put_nowait", w.method("Queue", "put_nowait", { py_positional("item") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py item = a[1];
                        if (self.attr("_is_shutdown").truthy())
                        {
                            py_raise(G<"queues.QueueShutDown">());
                        }
                        if (self.method("full").truthy())
                        {
                            py_raise(G<"queues.QueueFull">());
                        }
                        self.method("_put", { item });
                        {
                            py const target5 = self;
                            target5.set_attr("_unfinished_tasks", inplace(NEOS_PY_ADD, target5.attr("_unfinished_tasks"), I<1>()));
                        }
                        self.attr("_finished").method("clear");
                        self.method("_wakeup_next", { self.attr("_getters") });
                        return none();
                    }) },
                { "get", async_method(w, "Queue", "get", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py getter;
                        while (self.method("empty").truthy())
                        {
                            if ((self.attr("_is_shutdown").truthy() && self.method("empty").truthy()))
                            {
                                py_raise(G<"queues.QueueShutDown">());
                            }
                            getter = self.method("_get_loop").method("create_future");
                            self.attr("_getters").method("append", { getter });
                            try
                            {
                                py_await(getter);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler7;
                                {
                                    getter.method("cancel");
                                    try
                                    {
                                        // Clean self._getters from canceled getters.
                                        self.attr("_getters").method("remove", { getter });
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler8;
                                        if (matches(handler8.exception, B<"ValueError">()))
                                        {
                                            // The getter could be removed from self._getters by a
                                            // previous put_nowait call, or a shutdown call.
                                        }
                                        else
                                            py_reraise(handler8.exception);
                                    }
                                    if (((!self.method("empty").truthy()) && (!getter.method("cancelled").truthy())))
                                    {
                                        // We were woken up by put_nowait(), but can't take
                                        // the call.  Wake up the next in line.
                                        self.method("_wakeup_next", { self.attr("_getters") });
                                    }
                                    py_reraise(handler7.exception);
                                }
                            }
                        }
                        return self.method("get_nowait");
                    }) },
                { "get_nowait", w.method("Queue", "get_nowait", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py item;
                        if (self.method("empty").truthy())
                        {
                            if (self.attr("_is_shutdown").truthy())
                            {
                                py_raise(G<"queues.QueueShutDown">());
                            }
                            py_raise(G<"queues.QueueEmpty">());
                        }
                        item = self.method("_get");
                        self.method("_wakeup_next", { self.attr("_putters") });
                        return item;
                    }) },
                { "task_done", w.method("Queue", "task_done", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_unfinished_tasks") <= (I<0>()))
                        {
                            py_raise((B<"ValueError">())(S<"task_done() called too many times">()));
                        }
                        {
                            py const target9 = self;
                            target9.set_attr("_unfinished_tasks", inplace(NEOS_PY_SUBTRACT, target9.attr("_unfinished_tasks"), I<1>()));
                        }
                        if (self.attr("_unfinished_tasks") == (I<0>()))
                        {
                            self.attr("_finished").method("set");
                        }
                        return none();
                    }) },
                { "join", async_method(w, "Queue", "join", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_unfinished_tasks") > (I<0>()))
                        {
                            py_await(self.attr("_finished").method("wait"));
                        }
                        return none();
                    }) },
                { "shutdown", w.method("Queue", "shutdown", { py_positional("immediate", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py immediate = a[1];
                        py getter;
                        py putter;
                        self.set_attr("_is_shutdown", boolean(true));
                        if (immediate.truthy())
                        {
                            while (!self.method("empty").truthy())
                            {
                                self.method("_get");
                                if (self.attr("_unfinished_tasks") > (I<0>()))
                                {
                                    {
                                        py const target11 = self;
                                        target11.set_attr("_unfinished_tasks", inplace(NEOS_PY_SUBTRACT, target11.attr("_unfinished_tasks"), I<1>()));
                                    }
                                }
                            }
                            if (self.attr("_unfinished_tasks") == (I<0>()))
                            {
                                self.attr("_finished").method("set");
                            }
                        }
                        // All getters need to re-check queue-empty to raise ShutDown
                        while (self.attr("_getters").truthy())
                        {
                            getter = self.attr("_getters").method("popleft");
                            if (!getter.method("done").truthy())
                            {
                                getter.method("set_result", { none() });
                            }
                        }
                        while (self.attr("_putters").truthy())
                        {
                            putter = self.attr("_putters").method("popleft");
                            if (!putter.method("done").truthy())
                            {
                                putter.method("set_result", { none() });
                            }
                        }
                        return none();
                    }) }, }));

            define("queues", "PriorityQueue", w.make_class("PriorityQueue", { G<"queues.Queue">() }, {
                { "_init", w.method("PriorityQueue", "_init", { py_positional("maxsize") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py maxsize = a[1];
                        self.set_attr("_queue", py_list());
                        return none();
                    }) },
                { "_put", w.method("PriorityQueue", "_put", { py_positional("item"), py_positional("heappush", Mod<"heapq">().attr("heappush")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py item = a[1];
                        py heappush = a[2];
                        heappush(self.attr("_queue"), item);
                        return none();
                    }) },
                { "_get", w.method("PriorityQueue", "_get", { py_positional("heappop", Mod<"heapq">().attr("heappop")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py heappop = a[1];
                        return heappop(self.attr("_queue"));
                    }) }, }));

            define("queues", "LifoQueue", w.make_class("LifoQueue", { G<"queues.Queue">() }, {
                { "_init", w.method("LifoQueue", "_init", { py_positional("maxsize") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py maxsize = a[1];
                        self.set_attr("_queue", py_list());
                        return none();
                    }) },
                { "_put", w.method("LifoQueue", "_put", { py_positional("item") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py item = a[1];
                        self.attr("_queue").method("append", { item });
                        return none();
                    }) },
                { "_get", w.method("LifoQueue", "_get", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_queue").method("pop");
                    }) }, }));
        }

        // --- timeouts (Lib/asyncio/timeouts.py)

        void make_timeouts()
        {
            static module_writer const& w = writer("timeouts");
            define("timeouts", "__all__", py_tuple({ S<"Timeout">(), S<"timeout">(), S<"timeout_at">() }));
            define("timeouts", "_State", w.make_class("_State", { Mod<"enum">().attr("Enum") }, {
                { "CREATED", S<"created">() },
                { "ENTERED", S<"active">() },
                { "EXPIRING", S<"expiring">() },
                { "EXPIRED", S<"expired">() },
                { "EXITED", S<"finished">() }, }));

            define("timeouts", "Timeout", w.make_class("Timeout", {  }, {
                { "__init__", w.method("Timeout", "__init__", { py_positional("when") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py when = a[1];
                        self.set_attr("_state", (G<"timeouts._State">()).attr("CREATED"));
                        self.set_attr("_timeout_handler", none());
                        self.set_attr("_task", none());
                        self.set_attr("_when", when);
                        return none();
                    }) },
                { "when", w.method("Timeout", "when", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_when");
                    }) },
                { "reschedule", w.method("Timeout", "reschedule", { py_positional("when") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py when = a[1];
                        py loop;
                        if (!self.attr("_state").is((G<"timeouts._State">()).attr("ENTERED")))
                        {
                            if (self.attr("_state").is((G<"timeouts._State">()).attr("CREATED")))
                            {
                                py_raise((B<"RuntimeError">())(S<"Timeout has not been entered">()));
                            }
                            py_raise((B<"RuntimeError">())(S(std::string{ "Cannot change state of " } + self.attr("_state").attr("value").str() + " Timeout")));
                        }
                        self.set_attr("_when", when);
                        if (!self.attr("_timeout_handler").is_none())
                        {
                            self.attr("_timeout_handler").method("cancel");
                        }
                        if (when.is_none())
                        {
                            self.set_attr("_timeout_handler", none());
                        }
                        else
                        {
                            loop = (G<"events.get_running_loop">())();
                            if (when <= loop.method("time"))
                            {
                                self.set_attr("_timeout_handler", loop.method("call_soon", { self.attr("_on_timeout") }));
                            }
                            else
                            {
                                self.set_attr("_timeout_handler", loop.method("call_at", { when, self.attr("_on_timeout") }));
                            }
                        }
                        return none();
                    }) },
                { "expired", w.method("Timeout", "expired", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(py_tuple({ (G<"timeouts._State">()).attr("EXPIRING"), (G<"timeouts._State">()).attr("EXPIRED") }).contains(self.attr("_state")));
                    }) },
                { "__repr__", w.method("Timeout", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py info;
                        py info_str;
                        py when;
                        info = py_list({ S<"">() });
                        if (self.attr("_state").is((G<"timeouts._State">()).attr("ENTERED")))
                        {
                            when = (!self.attr("_when").is_none() ? (B<"round">())(self.attr("_when"), I<3>()) : none());
                            info.method("append", { S(std::string{ "when=" } + when.str()) });
                        }
                        info_str = (S<" ">()).method("join", { info });
                        return S(std::string{ "<Timeout [" } + self.attr("_state").attr("value").str() + "]" + info_str.str() + ">");
                    }) },
                { "__aenter__", async_method(w, "Timeout", "__aenter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py task;
                        if (!self.attr("_state").is((G<"timeouts._State">()).attr("CREATED")))
                        {
                            py_raise((B<"RuntimeError">())(S<"Timeout has already been entered">()));
                        }
                        task = (G<"tasks.current_task">())();
                        if (task.is_none())
                        {
                            py_raise((B<"RuntimeError">())(S<"Timeout should be used inside a task">()));
                        }
                        self.set_attr("_state", (G<"timeouts._State">()).attr("ENTERED"));
                        self.set_attr("_task", task);
                        self.set_attr("_cancelling", self.attr("_task").method("cancelling"));
                        self.method("reschedule", { self.attr("_when") });
                        return self;
                    }) },
                { "__aexit__", async_method(w, "Timeout", "__aexit__", { py_positional("exc_type"), py_positional("exc_val"), py_positional("exc_tb") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc_type = a[1];
                        py exc_val = a[2];
                        py exc_tb = a[3];
                        py exc;
                        if (!py_tuple({ (G<"timeouts._State">()).attr("ENTERED"), (G<"timeouts._State">()).attr("EXPIRING") }).contains(self.attr("_state")))
                            py_raise(B<"AssertionError">()());
                        if (!self.attr("_timeout_handler").is_none())
                        {
                            self.attr("_timeout_handler").method("cancel");
                            self.set_attr("_timeout_handler", none());
                        }
                        if (self.attr("_state").is((G<"timeouts._State">()).attr("EXPIRING")))
                        {
                            self.set_attr("_state", (G<"timeouts._State">()).attr("EXPIRED"));
                            if (((self.attr("_task").method("uncancel") <= self.attr("_cancelling")) && (!exc_type.is_none())))
                            {
                                // Since there are no new cancel requests, we're
                                // handling this.
                                if (issubclass(exc_type, G<"exceptions.CancelledError">()))
                                {
                                    fn_raise_from((B<"TimeoutError">()).get(), exc_val.get());
                                    throw raised{};
                                }
                                else if (!exc_val.is_none())
                                {
                                    self.method("_insert_timeout_error", { exc_val });
                                    if (isinstance(exc_val, B<"ExceptionGroup">()))
                                    {
                                        for (py const& item : iterate{ exc_val.attr("exceptions") })
                                        {
                                            exc = item;
                                            self.method("_insert_timeout_error", { exc });
                                        }
                                    }
                                }
                            }
                        }
                        else if (self.attr("_state").is((G<"timeouts._State">()).attr("ENTERED")))
                        {
                            self.set_attr("_state", (G<"timeouts._State">()).attr("EXITED"));
                        }
                        return none();
                    }) },
                { "_on_timeout", w.method("Timeout", "_on_timeout", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("_state").is((G<"timeouts._State">()).attr("ENTERED")))
                            py_raise(B<"AssertionError">()());
                        self.attr("_task").method("cancel");
                        self.set_attr("_state", (G<"timeouts._State">()).attr("EXPIRING"));
                        // drop the reference early
                        self.set_attr("_timeout_handler", none());
                        return none();
                    }) },
                { "_insert_timeout_error", w.static_method("Timeout", "_insert_timeout_error", { py_positional("exc_val") }, [](arguments a) -> py
                    {
                        py exc_val = a[0];
                        py te;
                        while (!exc_val.attr("__context__").is_none())
                        {
                            if (isinstance(exc_val.attr("__context__"), G<"exceptions.CancelledError">()))
                            {
                                te = (B<"TimeoutError">())();
                                py const value3 = exc_val.attr("__context__");
                                te.set_attr("__context__", value3);
                                te.set_attr("__cause__", value3);
                                exc_val.set_attr("__context__", te);
                                break;
                            }
                            exc_val = exc_val.attr("__context__");
                        }
                        return none();
                    }) }, }));

            define("timeouts", "timeout", w.function("timeout", "timeout", { py_positional("delay") }, [](arguments a) -> py
                {
                    py delay = a[0];
                    py loop;
                    loop = (G<"events.get_running_loop">())();
                    return (G<"timeouts.Timeout">())((!delay.is_none() ? loop.method("time") + delay : none()));
                }));

            define("timeouts", "timeout_at", w.function("timeout_at", "timeout_at", { py_positional("when") }, [](arguments a) -> py
                {
                    py when = a[0];
                    return (G<"timeouts.Timeout">())(when);
                }));
        }

        // --- taskgroups (Lib/asyncio/taskgroups.py)

        void make_taskgroups()
        {
            static module_writer const& w = writer("taskgroups");
            // Adapted with permission from the EdgeDB project;
            // license: PSFL.
            define("taskgroups", "__all__", py_tuple({ S<"TaskGroup">() }));
                // Since Python 3.8 Tasks propagate all exceptions correctly,
                // except for KeyboardInterrupt and SystemExit which are
                // still considered special.
            define("taskgroups", "TaskGroup", w.make_class("TaskGroup", {  }, {
                { "__init__", w.method("TaskGroup", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_entered", boolean(false));
                        self.set_attr("_exiting", boolean(false));
                        self.set_attr("_aborting", boolean(false));
                        self.set_attr("_loop", none());
                        self.set_attr("_parent_task", none());
                        self.set_attr("_parent_cancel_requested", boolean(false));
                        self.set_attr("_tasks", (B<"set">())());
                        self.set_attr("_errors", py_list());
                        self.set_attr("_base_error", none());
                        self.set_attr("_on_completed_fut", none());
                        return none();
                    }) },
                { "__repr__", w.method("TaskGroup", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py info;
                        py info_str;
                        info = py_list({ S<"">() });
                        if (self.attr("_tasks").truthy())
                        {
                            info.method("append", { S(std::string{ "tasks=" } + py{ self.attr("_tasks").size() }.str()) });
                        }
                        if (self.attr("_errors").truthy())
                        {
                            info.method("append", { S(std::string{ "errors=" } + py{ self.attr("_errors").size() }.str()) });
                        }
                        if (self.attr("_aborting").truthy())
                        {
                            info.method("append", { S<"cancelling">() });
                        }
                        else if (self.attr("_entered").truthy())
                        {
                            info.method("append", { S<"entered">() });
                        }
                        info_str = (S<" ">()).method("join", { info });
                        return S(std::string{ "<TaskGroup" } + info_str.str() + ">");
                    }) },
                { "__aenter__", async_method(w, "TaskGroup", "__aenter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_entered").truthy())
                        {
                            py_raise((B<"RuntimeError">())(S(std::string{ "TaskGroup " } + self.repr() + " has already been entered")));
                        }
                        if (self.attr("_loop").is_none())
                        {
                            self.set_attr("_loop", (G<"events.get_running_loop">())());
                        }
                        self.set_attr("_parent_task", (G<"tasks.current_task">())(self.attr("_loop")));
                        if (self.attr("_parent_task").is_none())
                        {
                            py_raise((B<"RuntimeError">())(S(std::string{ "TaskGroup " } + self.repr() + " cannot determine the parent task")));
                        }
                        self.set_attr("_entered", boolean(true));
                        return self;
                    }) },
                { "__aexit__", async_method(w, "TaskGroup", "__aexit__", { py_positional("et"), py_positional("exc"), py_positional("tb") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py et = a[1];
                        py exc = a[2];
                        py tb = a[3];
                        tb = none();
                        {
                            std::optional<py> pending1;
                            [[maybe_unused]] int flow1 = 0;
                            [[maybe_unused]] py result1;
                            try
                            {
                                result1 = py_await(self.method("_aexit", { et, exc }));
                                flow1 = 1;
                                goto finally1;
                            }
                            catch (raised const&)
                            {
                                pending1 = py_take_exception();
                            }
                            finally1:
                            // Exceptions are heavy objects that can have object
                            // cycles (bad for GC); let's not keep a reference to
                            // a bunch of them. It would be nicer to use a try/finally
                            // in __aexit__ directly but that introduced some diff noise
                            self.set_attr("_parent_task", none());
                            self.set_attr("_errors", none());
                            self.set_attr("_base_error", none());
                            exc = none();
                            if (pending1)
                                py_reraise(*pending1);
                            if (flow1 == 1)
                                return result1;
                        }
                        return none();
                    }) },
                { "_aexit", async_method(w, "TaskGroup", "_aexit", { py_positional("et"), py_positional("exc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py et = a[1];
                        py exc = a[2];
                        py ex;
                        py propagate_cancellation_error;
                        self.set_attr("_exiting", boolean(true));
                        if (((!exc.is_none()) && self.method("_is_base_error", { exc }).truthy() && self.attr("_base_error").is_none()))
                        {
                            self.set_attr("_base_error", exc);
                        }
                        if (((!et.is_none()) && issubclass(et, G<"exceptions.CancelledError">())))
                        {
                            propagate_cancellation_error = exc;
                        }
                        else
                        {
                            propagate_cancellation_error = none();
                        }
                        if (!et.is_none())
                        {
                            if (!self.attr("_aborting").truthy())
                            {
                                // Our parent task is being cancelled:
                                // 
                                // async with TaskGroup() as g:
                                // g.create_task(...)
                                // await ...  # <- CancelledError
                                // 
                                // or there's an exception in "async with":
                                // 
                                // async with TaskGroup() as g:
                                // g.create_task(...)
                                // 1 / 0
                                // 
                                self.method("_abort");
                            }
                        }
                        // We use while-loop here because "self._on_completed_fut"
                        // can be cancelled multiple times if our parent task
                        // is being cancelled repeatedly (or even once, when
                        // our own cancellation is already in progress)
                        while (self.attr("_tasks").truthy())
                        {
                            if (self.attr("_on_completed_fut").is_none())
                            {
                                self.set_attr("_on_completed_fut", self.attr("_loop").method("create_future"));
                            }
                            try
                            {
                                py_await(self.attr("_on_completed_fut"));
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler3;
                                if (matches(handler3.exception, G<"exceptions.CancelledError">()))
                                {
                                    ex = handler3.exception;
                                    if (!self.attr("_aborting").truthy())
                                    {
                                        // Our parent task is being cancelled:
                                        // 
                                        // async def wrapper():
                                        // async with TaskGroup() as g:
                                        // g.create_task(foo)
                                        // 
                                        // "wrapper" is being cancelled while "foo" is
                                        // still running.
                                        propagate_cancellation_error = ex;
                                        self.method("_abort");
                                    }
                                }
                                else
                                    py_reraise(handler3.exception);
                            }
                            self.set_attr("_on_completed_fut", none());
                        }
                        if (self.attr("_tasks").truthy())
                            py_raise(B<"AssertionError">()());
                        if (!self.attr("_base_error").is_none())
                        {
                            {
                                std::optional<py> pending4;
                                [[maybe_unused]] int flow4 = 0;
                                [[maybe_unused]] py result4;
                                try
                                {
                                    py_raise(self.attr("_base_error"));
                                }
                                catch (raised const&)
                                {
                                    pending4 = py_take_exception();
                                }
                                exc = none();
                                if (pending4)
                                    py_reraise(*pending4);
                                if (flow4 == 1)
                                    return result4;
                            }
                        }
                        if (self.attr("_parent_cancel_requested").truthy())
                        {
                            // If this flag is set we *must* call uncancel().
                            if (self.attr("_parent_task").method("uncancel") == (I<0>()))
                            {
                                // If there are no pending cancellations left,
                                // don't propagate CancelledError.
                                propagate_cancellation_error = none();
                            }
                        }
                        // Propagate CancelledError if there is one, except if there
                        // are other errors -- those have priority.
                        {
                            std::optional<py> pending5;
                            [[maybe_unused]] int flow5 = 0;
                            [[maybe_unused]] py result5;
                            try
                            {
                                if (((!propagate_cancellation_error.is_none()) && (!self.attr("_errors").truthy())))
                                {
                                    {
                                        std::optional<py> pending6;
                                        [[maybe_unused]] int flow6 = 0;
                                        [[maybe_unused]] py result6;
                                        try
                                        {
                                            py_raise(propagate_cancellation_error);
                                        }
                                        catch (raised const&)
                                        {
                                            pending6 = py_take_exception();
                                        }
                                        exc = none();
                                        if (pending6)
                                            py_reraise(*pending6);
                                        if (flow6 == 1)
                                            { result5 = result6; flow5 = 1; goto finally5; }
                                    }
                                }
                            }
                            catch (raised const&)
                            {
                                pending5 = py_take_exception();
                            }
                            finally5:
                            propagate_cancellation_error = none();
                            if (pending5)
                                py_reraise(*pending5);
                            if (flow5 == 1)
                                return result5;
                        }
                        if (((!et.is_none()) && (!issubclass(et, G<"exceptions.CancelledError">()))))
                        {
                            self.attr("_errors").method("append", { exc });
                        }
                        if (self.attr("_errors").truthy())
                        {
                            // If the parent task is being cancelled from the outside
                            // of the taskgroup, un-cancel and re-cancel the parent task,
                            // which will keep the cancel count stable.
                            if (self.attr("_parent_task").method("cancelling").truthy())
                            {
                                self.attr("_parent_task").method("uncancel");
                                self.attr("_parent_task").method("cancel");
                            }
                            {
                                std::optional<py> pending7;
                                [[maybe_unused]] int flow7 = 0;
                                [[maybe_unused]] py result7;
                                try
                                {
                                    fn_raise_from((B<"BaseExceptionGroup">())(S<"unhandled errors in a TaskGroup">(), self.attr("_errors")).get(), none().get());
                                    throw raised{};
                                }
                                catch (raised const&)
                                {
                                    pending7 = py_take_exception();
                                }
                                exc = none();
                                if (pending7)
                                    py_reraise(*pending7);
                                if (flow7 == 1)
                                    return result7;
                            }
                        }
                        return none();
                    }) },
                { "create_task", w.method("TaskGroup", "create_task", { py_positional("coro"), py_keyword("name", none()), py_keyword("context", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py coro = a[1];
                        py name = a[2];
                        py context = a[3];
                        py task;
                        if (!self.attr("_entered").truthy())
                        {
                            coro.method("close");
                            py_raise((B<"RuntimeError">())(S(std::string{ "TaskGroup " } + self.repr() + " has not been entered")));
                        }
                        if ((self.attr("_exiting").truthy() && (!self.attr("_tasks").truthy())))
                        {
                            coro.method("close");
                            py_raise((B<"RuntimeError">())(S(std::string{ "TaskGroup " } + self.repr() + " is finished")));
                        }
                        if (self.attr("_aborting").truthy())
                        {
                            coro.method("close");
                            py_raise((B<"RuntimeError">())(S(std::string{ "TaskGroup " } + self.repr() + " is shutting down")));
                        }
                        if (context.is_none())
                        {
                            task = self.attr("_loop").method("create_task", { coro }, { { "name", name } });
                        }
                        else
                        {
                            task = self.attr("_loop").method("create_task", { coro }, { { "name", name }, { "context", context } });
                        }
                        // Always schedule the done callback even if the task is
                        // already done (e.g. if the coro was able to complete eagerly),
                        // otherwise if the task completes with an exception then it will cancel
                        // the current task too early. gh-128550, gh-128588
                        self.attr("_tasks").method("add", { task });
                        task.method("add_done_callback", { self.attr("_on_task_done") });
                        {
                            std::optional<py> pending8;
                            [[maybe_unused]] int flow8 = 0;
                            [[maybe_unused]] py result8;
                            try
                            {
                                result8 = task;
                                flow8 = 1;
                                goto finally8;
                            }
                            catch (raised const&)
                            {
                                pending8 = py_take_exception();
                            }
                            finally8:
                            // gh-128552: prevent a refcycle of
                            // task.exception().__traceback__->TaskGroup.create_task->task
                            task = none();
                            if (pending8)
                                py_reraise(*pending8);
                            if (flow8 == 1)
                                return result8;
                        }
                        return none();
                    }) },
                { "_is_base_error", w.method("TaskGroup", "_is_base_error", { py_positional("exc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        if (!isinstance(exc, B<"BaseException">()))
                            py_raise(B<"AssertionError">()());
                        return boolean(isinstance(exc, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })));
                    }) },
                { "_abort", w.method("TaskGroup", "_abort", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py t;
                        self.set_attr("_aborting", boolean(true));
                        for (py const& item : iterate{ self.attr("_tasks") })
                        {
                            t = item;
                            if (!t.method("done").truthy())
                            {
                                t.method("cancel");
                            }
                        }
                        return none();
                    }) },
                { "_on_task_done", w.method("TaskGroup", "_on_task_done", { py_positional("task") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py task = a[1];
                        py exc;
                        self.attr("_tasks").method("discard", { task });
                        if (((!self.attr("_on_completed_fut").is_none()) && (!self.attr("_tasks").truthy())))
                        {
                            if (!self.attr("_on_completed_fut").method("done").truthy())
                            {
                                self.attr("_on_completed_fut").method("set_result", { boolean(true) });
                            }
                        }
                        if (task.method("cancelled").truthy())
                        {
                            return none();
                        }
                        exc = task.method("exception");
                        if (exc.is_none())
                        {
                            return none();
                        }
                        self.attr("_errors").method("append", { exc });
                        if ((self.method("_is_base_error", { exc }).truthy() && self.attr("_base_error").is_none()))
                        {
                            self.set_attr("_base_error", exc);
                        }
                        if (self.attr("_parent_task").method("done").truthy())
                        {
                            // Not sure if this case is possible, but we want to handle
                            // it anyways.
                            self.attr("_loop").method("call_exception_handler", { dict_of({ { S<"message">(), S(std::string{ "Task " } + task.repr() + " has errored out but its parent task " + self.attr("_parent_task").str() + " is already completed") }, { S<"exception">(), exc }, { S<"task">(), task } }) });
                            return none();
                        }
                        if (((!self.attr("_aborting").truthy()) && (!self.attr("_parent_cancel_requested").truthy())))
                        {
                            // If parent task *is not* being cancelled, it means that we want
                            // to manually cancel it to abort whatever is being run right now
                            // in the TaskGroup.  But we want to mark parent task as
                            // "not cancelled" later in __aexit__.  Example situation that
                            // we need to handle:
                            // 
                            // async def foo():
                            // try:
                            // async with TaskGroup() as g:
                            // g.create_task(crash_soon())
                            // await something  # <- this needs to be canceled
                            // #    by the TaskGroup, e.g.
                            // #    foo() needs to be cancelled
                            // except Exception:
                            // # Ignore any exceptions raised in the TaskGroup
                            // pass
                            // await something_else     # this line has to be called
                            // # after TaskGroup is finished.
                            self.method("_abort");
                            self.set_attr("_parent_cancel_requested", boolean(true));
                            self.attr("_parent_task").method("cancel");
                        }
                        return none();
                    }) }, }));
        }

        // --- streams (Lib/asyncio/streams.py)

        void make_streams()
        {
            static module_writer const& w = writer("streams");
            define("streams", "__all__", py_tuple({ S<"StreamReader">(), S<"StreamWriter">(), S<"StreamReaderProtocol">(), S<"open_connection">(), S<"start_server">() }));
            if ((B<"hasattr">())(Mod<"socket">(), S<"AF_UNIX">()).truthy())
            {
                define("streams", "__all__", inplace(NEOS_PY_ADD, object_of("streams.__all__"), py_tuple({ S<"open_unix_connection">(), S<"start_unix_server">() })));
            }
            define("streams", "_DEFAULT_LIMIT", power((I<2>()), (I<16>())));
            define("streams", "open_connection", async_function(w, "open_connection", { py_positional("host", none()), py_positional("port", none()), py_keyword("limit", G<"streams._DEFAULT_LIMIT">()) }, [](arguments a) -> py
                {
                    py host = a[0];
                    py port = a[1];
                    py limit = a[2];
                    py kwds = a[3];
                    py _;
                    py loop;
                    auto const protocol = std::make_shared<py>();
                    py reader;
                    py transport;
                    py writer;
                    loop = (G<"events.get_running_loop">())();
                    reader = (G<"streams.StreamReader">()).call({  }, { { "limit", limit }, { "loop", loop } });
                    *protocol = (G<"streams.StreamReaderProtocol">()).call({ reader }, { { "loop", loop } });
                    auto const items1 = unpack(py_await(call_ex(loop.attr("create_connection"), { w.function("<lambda>", "open_connection.<locals>.<lambda>", {  }, [protocol](arguments a) -> py
    {
        return (*protocol);
    }), host, port }, py{}, kwds)), 2);
                    transport = items1[0];
                    _ = items1[1];
                    writer = (G<"streams.StreamWriter">())(transport, (*protocol), reader, loop);
                    return py_tuple({ reader, writer });
                }, false, true));

            define("streams", "start_server", async_function(w, "start_server", { py_positional("client_connected_cb"), py_positional("host", none()), py_positional("port", none()), py_keyword("limit", G<"streams._DEFAULT_LIMIT">()) }, [](arguments a) -> py
                {
                    auto const client_connected_cb = std::make_shared<py>(a[0]);
                    py host = a[1];
                    py port = a[2];
                    auto const limit = std::make_shared<py>(a[3]);
                    py kwds = a[4];
                    py factory;
                    auto const loop = std::make_shared<py>();
                    *loop = (G<"events.get_running_loop">())();
                    factory = w.function("factory", "start_server.<locals>.factory", {  }, [client_connected_cb, limit, loop](arguments a) -> py
    {
        py protocol;
        py reader;
        reader = (G<"streams.StreamReader">()).call({  }, { { "limit", (*limit) }, { "loop", (*loop) } });
        protocol = (G<"streams.StreamReaderProtocol">()).call({ reader, (*client_connected_cb) }, { { "loop", (*loop) } });
        return protocol;
    });
                    return py_await(call_ex((*loop).attr("create_server"), { factory, host, port }, py{}, kwds));
                }, false, true));

            if ((B<"hasattr">())(Mod<"socket">(), S<"AF_UNIX">()).truthy())
            {
                define("streams", "open_unix_connection", async_function(w, "open_unix_connection", { py_positional("path", none()), py_keyword("limit", G<"streams._DEFAULT_LIMIT">()) }, [](arguments a) -> py
                    {
                        py path = a[0];
                        py limit = a[1];
                        py kwds = a[2];
                        py _;
                        py loop;
                        auto const protocol = std::make_shared<py>();
                        py reader;
                        py transport;
                        py writer;
                        loop = (G<"events.get_running_loop">())();
                        reader = (G<"streams.StreamReader">()).call({  }, { { "limit", limit }, { "loop", loop } });
                        *protocol = (G<"streams.StreamReaderProtocol">()).call({ reader }, { { "loop", loop } });
                        auto const items2 = unpack(py_await(call_ex(loop.attr("create_unix_connection"), { w.function("<lambda>", "open_unix_connection.<locals>.<lambda>", {  }, [protocol](arguments a) -> py
    {
        return (*protocol);
    }), path }, py{}, kwds)), 2);
                        transport = items2[0];
                        _ = items2[1];
                        writer = (G<"streams.StreamWriter">())(transport, (*protocol), reader, loop);
                        return py_tuple({ reader, writer });
                    }, false, true));

                define("streams", "start_unix_server", async_function(w, "start_unix_server", { py_positional("client_connected_cb"), py_positional("path", none()), py_keyword("limit", G<"streams._DEFAULT_LIMIT">()) }, [](arguments a) -> py
                    {
                        auto const client_connected_cb = std::make_shared<py>(a[0]);
                        py path = a[1];
                        auto const limit = std::make_shared<py>(a[2]);
                        py kwds = a[3];
                        py factory;
                        auto const loop = std::make_shared<py>();
                        *loop = (G<"events.get_running_loop">())();
                        factory = w.function("factory", "start_unix_server.<locals>.factory", {  }, [client_connected_cb, limit, loop](arguments a) -> py
    {
        py protocol;
        py reader;
        reader = (G<"streams.StreamReader">()).call({  }, { { "limit", (*limit) }, { "loop", (*loop) } });
        protocol = (G<"streams.StreamReaderProtocol">()).call({ reader, (*client_connected_cb) }, { { "loop", (*loop) } });
        return protocol;
    });
                        return py_await(call_ex((*loop).attr("create_unix_server"), { factory, path }, py{}, kwds));
                    }, false, true));

            }
            define("streams", "FlowControlMixin", w.make_class("FlowControlMixin", { G<"protocols.Protocol">() }, {
                { "__init__", w.method("FlowControlMixin", "__init__", { py_positional("loop", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py loop = a[1];
                        if (loop.is_none())
                        {
                            self.set_attr("_loop", (G<"events.get_event_loop">())());
                        }
                        else
                        {
                            self.set_attr("_loop", loop);
                        }
                        self.set_attr("_paused", boolean(false));
                        self.set_attr("_drain_waiters", (Mod<"collections">().attr("deque"))());
                        self.set_attr("_connection_lost", boolean(false));
                        return none();
                    }) },
                { "pause_writing", w.method("FlowControlMixin", "pause_writing", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_paused").truthy())
                            py_raise(B<"AssertionError">()());
                        self.set_attr("_paused", boolean(true));
                        if (self.attr("_loop").method("get_debug").truthy())
                        {
                            (G<"log.logger">()).method("debug", { S<"%r pauses writing">(), self });
                        }
                        return none();
                    }) },
                { "resume_writing", w.method("FlowControlMixin", "resume_writing", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py waiter;
                        if (!self.attr("_paused").truthy())
                            py_raise(B<"AssertionError">()());
                        self.set_attr("_paused", boolean(false));
                        if (self.attr("_loop").method("get_debug").truthy())
                        {
                            (G<"log.logger">()).method("debug", { S<"%r resumes writing">(), self });
                        }
                        for (py const& item : iterate{ self.attr("_drain_waiters") })
                        {
                            waiter = item;
                            if (!waiter.method("done").truthy())
                            {
                                waiter.method("set_result", { none() });
                            }
                        }
                        return none();
                    }) },
                { "connection_lost", w.method("FlowControlMixin", "connection_lost", { py_positional("exc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        py waiter;
                        self.set_attr("_connection_lost", boolean(true));
                        // Wake up the writer(s) if currently paused.
                        if (!self.attr("_paused").truthy())
                        {
                            return none();
                        }
                        for (py const& item : iterate{ self.attr("_drain_waiters") })
                        {
                            waiter = item;
                            if (!waiter.method("done").truthy())
                            {
                                if (exc.is_none())
                                {
                                    waiter.method("set_result", { none() });
                                }
                                else
                                {
                                    waiter.method("set_exception", { exc });
                                }
                            }
                        }
                        return none();
                    }) },
                { "_drain_helper", async_method(w, "FlowControlMixin", "_drain_helper", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py waiter;
                        if (self.attr("_connection_lost").truthy())
                        {
                            py_raise((B<"ConnectionResetError">())(S<"Connection lost">()));
                        }
                        if (!self.attr("_paused").truthy())
                        {
                            return none();
                        }
                        waiter = self.attr("_loop").method("create_future");
                        self.attr("_drain_waiters").method("append", { waiter });
                        {
                            std::optional<py> pending5;
                            [[maybe_unused]] int flow5 = 0;
                            [[maybe_unused]] py result5;
                            try
                            {
                                py_await(waiter);
                            }
                            catch (raised const&)
                            {
                                pending5 = py_take_exception();
                            }
                            self.attr("_drain_waiters").method("remove", { waiter });
                            if (pending5)
                                py_reraise(*pending5);
                            if (flow5 == 1)
                                return result5;
                        }
                        return none();
                    }) },
                { "_get_close_waiter", w.method("FlowControlMixin", "_get_close_waiter", { py_positional("stream") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py stream = a[1];
                        py_raise(B<"NotImplementedError">());
                    }) }, }));

            define("streams", "StreamReaderProtocol", w.make_class("StreamReaderProtocol", { G<"streams.FlowControlMixin">(), G<"protocols.Protocol">() }, {
                { "_source_traceback", none() },
                { "__init__", w.method("StreamReaderProtocol", "__init__", { py_positional("stream_reader"), py_positional("client_connected_cb", none()), py_positional("loop", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py stream_reader = a[1];
                        py client_connected_cb = a[2];
                        py loop = a[3];
                        (B<"super">()(G<"streams.StreamReaderProtocol">(), self)).method("__init__", {  }, { { "loop", loop } });
                        if (!stream_reader.is_none())
                        {
                            self.set_attr("_stream_reader_wr", (Mod<"weakref">().attr("ref"))(stream_reader));
                            self.set_attr("_source_traceback", stream_reader.attr("_source_traceback"));
                        }
                        else
                        {
                            self.set_attr("_stream_reader_wr", none());
                        }
                        if (!client_connected_cb.is_none())
                        {
                            // This is a stream created by the `create_server()` function.
                            // Keep a strong reference to the reader until a connection
                            // is established.
                            self.set_attr("_strong_reader", stream_reader);
                        }
                        self.set_attr("_reject_connection", boolean(false));
                        self.set_attr("_task", none());
                        self.set_attr("_transport", none());
                        self.set_attr("_client_connected_cb", client_connected_cb);
                        self.set_attr("_over_ssl", boolean(false));
                        self.set_attr("_closed", self.attr("_loop").method("create_future"));
                        return none();
                    }) },
                { "_stream_reader", B<"property">()(w.method("StreamReaderProtocol", "_stream_reader", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_stream_reader_wr").is_none())
                        {
                            return none();
                        }
                        return self.method("_stream_reader_wr");
                    })) },
                { "_replace_transport", w.method("StreamReaderProtocol", "_replace_transport", { py_positional("transport") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py transport = a[1];
                        py loop;
                        loop = self.attr("_loop");
                        self.set_attr("_transport", transport);
                        self.set_attr("_over_ssl", boolean(!transport.method("get_extra_info", { S<"sslcontext">() }).is_none()));
                        return none();
                    }) },
                { "connection_made", w.method("StreamReaderProtocol", "connection_made", { py_positional("transport") }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        auto const transport = std::make_shared<py>(a[1]);
                        py callback;
                        py context;
                        py reader;
                        py res;
                        py writer;
                        if ((*self).attr("_reject_connection").truthy())
                        {
                            context = dict_of({ { S<"message">(), S<"An open stream was garbage collected prior to establishing network connection; call \"stream.close()\" explicitly.">() } });
                            if ((*self).attr("_source_traceback").truthy())
                            {
                                context.set_item(S<"source_traceback">(), (*self).attr("_source_traceback"));
                            }
                            (*self).attr("_loop").method("call_exception_handler", { context });
                            (*transport).method("abort");
                            return none();
                        }
                        (*self).set_attr("_transport", (*transport));
                        reader = (*self).attr("_stream_reader");
                        if (!reader.is_none())
                        {
                            reader.method("set_transport", { (*transport) });
                        }
                        (*self).set_attr("_over_ssl", boolean(!(*transport).method("get_extra_info", { S<"sslcontext">() }).is_none()));
                        if (!(*self).attr("_client_connected_cb").is_none())
                        {
                            writer = (G<"streams.StreamWriter">())((*transport), (*self), reader, (*self).attr("_loop"));
                            res = (*self).method("_client_connected_cb", { reader, writer });
                            if ((G<"coroutines.iscoroutine">())(res).truthy())
                            {
                                callback = w.function("callback", "StreamReaderProtocol.connection_made.<locals>.callback", { py_positional("task") }, [self, transport](arguments a) -> py
            {
                py task = a[0];
                py exc;
                if (task.method("cancelled").truthy())
                {
                    (*transport).method("close");
                    return none();
                }
                exc = task.method("exception");
                if (!exc.is_none())
                {
                    (*self).attr("_loop").method("call_exception_handler", { dict_of({ { S<"message">(), S<"Unhandled exception in client_connected_cb">() }, { S<"exception">(), exc }, { S<"transport">(), (*transport) } }) });
                    (*transport).method("close");
                }
                return none();
            });
                                (*self).set_attr("_task", (*self).attr("_loop").method("create_task", { res }));
                                (*self).attr("_task").method("add_done_callback", { callback });
                            }
                            (*self).set_attr("_strong_reader", none());
                        }
                        return none();
                    }) },
                { "connection_lost", w.method("StreamReaderProtocol", "connection_lost", { py_positional("exc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        py reader;
                        reader = self.attr("_stream_reader");
                        if (!reader.is_none())
                        {
                            if (exc.is_none())
                            {
                                reader.method("feed_eof");
                            }
                            else
                            {
                                reader.method("set_exception", { exc });
                            }
                        }
                        if (!self.attr("_closed").method("done").truthy())
                        {
                            if (exc.is_none())
                            {
                                self.attr("_closed").method("set_result", { none() });
                            }
                            else
                            {
                                self.attr("_closed").method("set_exception", { exc });
                            }
                        }
                        (B<"super">()(G<"streams.StreamReaderProtocol">(), self)).method("connection_lost", { exc });
                        self.set_attr("_stream_reader_wr", none());
                        self.set_attr("_stream_writer", none());
                        self.set_attr("_task", none());
                        self.set_attr("_transport", none());
                        return none();
                    }) },
                { "data_received", w.method("StreamReaderProtocol", "data_received", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py reader;
                        reader = self.attr("_stream_reader");
                        if (!reader.is_none())
                        {
                            reader.method("feed_data", { data });
                        }
                        return none();
                    }) },
                { "eof_received", w.method("StreamReaderProtocol", "eof_received", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py reader;
                        reader = self.attr("_stream_reader");
                        if (!reader.is_none())
                        {
                            reader.method("feed_eof");
                        }
                        if (self.attr("_over_ssl").truthy())
                        {
                            // Prevent a warning in SSLProtocol.eof_received:
                            // "returning true from eof_received()
                            // has no effect when using ssl"
                            return boolean(false);
                        }
                        return boolean(true);
                    }) },
                { "_get_close_waiter", w.method("StreamReaderProtocol", "_get_close_waiter", { py_positional("stream") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py stream = a[1];
                        return self.attr("_closed");
                    }) },
                { "__del__", w.method("StreamReaderProtocol", "__del__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py closed;
                        {
                            bool otherwise6 = false;
                            try
                            {
                                closed = self.attr("_closed");
                                otherwise6 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler6;
                                if (matches(handler6.exception, B<"AttributeError">()))
                                {
                                }
                                else
                                    py_reraise(handler6.exception);
                            }
                            if (otherwise6)
                            {
                                if ((closed.method("done").truthy() && (!closed.method("cancelled").truthy())))
                                {
                                    closed.method("exception");
                                }
                            }
                        }
                        return none();
                    }) }, }));

            define("streams", "StreamWriter", w.make_class("StreamWriter", {  }, {
                { "__init__", w.method("StreamWriter", "__init__", { py_positional("transport"), py_positional("protocol"), py_positional("reader"), py_positional("loop") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py transport = a[1];
                        py protocol = a[2];
                        py reader = a[3];
                        py loop = a[4];
                        self.set_attr("_transport", transport);
                        self.set_attr("_protocol", protocol);
                        // drain() expects that the reader has an exception() method
                        if (!([&]() -> py { py const t = boolean(reader.is_none()); return t.truthy() ? t : boolean(isinstance(reader, G<"streams.StreamReader">())); }()).truthy())
                            py_raise(B<"AssertionError">()());
                        self.set_attr("_reader", reader);
                        self.set_attr("_loop", loop);
                        self.set_attr("_complete_fut", self.attr("_loop").method("create_future"));
                        self.attr("_complete_fut").method("set_result", { none() });
                        return none();
                    }) },
                { "__repr__", w.method("StreamWriter", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py info;
                        info = py_list({ self.attr("__class__").attr("__name__"), S(std::string{ "transport=" } + self.attr("_transport").repr()) });
                        if (!self.attr("_reader").is_none())
                        {
                            info.method("append", { S(std::string{ "reader=" } + self.attr("_reader").repr()) });
                        }
                        return (S<"<{}>">()).method("format", { (S<" ">()).method("join", { info }) });
                    }) },
                { "transport", B<"property">()(w.method("StreamWriter", "transport", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_transport");
                    })) },
                { "write", w.method("StreamWriter", "write", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.attr("_transport").method("write", { data });
                        return none();
                    }) },
                { "writelines", w.method("StreamWriter", "writelines", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.attr("_transport").method("writelines", { data });
                        return none();
                    }) },
                { "write_eof", w.method("StreamWriter", "write_eof", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_transport").method("write_eof");
                    }) },
                { "can_write_eof", w.method("StreamWriter", "can_write_eof", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_transport").method("can_write_eof");
                    }) },
                { "close", w.method("StreamWriter", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_transport").method("close");
                    }) },
                { "is_closing", w.method("StreamWriter", "is_closing", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_transport").method("is_closing");
                    }) },
                { "wait_closed", async_method(w, "StreamWriter", "wait_closed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_await(self.attr("_protocol").method("_get_close_waiter", { self }));
                        return none();
                    }) },
                { "get_extra_info", w.method("StreamWriter", "get_extra_info", { py_positional("name"), py_positional("default", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py default_ = a[2];
                        return self.attr("_transport").method("get_extra_info", { name, default_ });
                    }) },
                { "drain", async_method(w, "StreamWriter", "drain", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc;
                        if (!self.attr("_reader").is_none())
                        {
                            exc = self.attr("_reader").method("exception");
                            if (!exc.is_none())
                            {
                                py_raise(exc);
                            }
                        }
                        if (self.attr("_transport").method("is_closing").truthy())
                        {
                            // Wait for protocol.connection_lost() call
                            // Raise connection closing error if any,
                            // ConnectionResetError otherwise
                            // Yield to the event loop so connection_lost() may be
                            // called.  Without this, _drain_helper() would return
                            // immediately, and code that calls
                            // write(...); await drain()
                            // in a loop would never call connection_lost(), so it
                            // would not see an error when the socket is closed.
                            py_await((G<"tasks.sleep">())(I<0>()));
                        }
                        py_await(self.attr("_protocol").method("_drain_helper"));
                        return none();
                    }) },
                { "start_tls", async_method(w, "StreamWriter", "start_tls", { py_positional("sslcontext"), py_keyword("server_hostname", none()), py_keyword("ssl_handshake_timeout", none()), py_keyword("ssl_shutdown_timeout", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sslcontext = a[1];
                        py server_hostname = a[2];
                        py ssl_handshake_timeout = a[3];
                        py ssl_shutdown_timeout = a[4];
                        py new_transport;
                        py protocol;
                        py server_side;
                        server_side = boolean(!self.attr("_protocol").attr("_client_connected_cb").is_none());
                        protocol = self.attr("_protocol");
                        py_await(self.method("drain"));
                        new_transport = py_await(self.attr("_loop").method("start_tls", { self.attr("_transport"), protocol, sslcontext }, { { "server_side", server_side }, { "server_hostname", server_hostname }, { "ssl_handshake_timeout", ssl_handshake_timeout }, { "ssl_shutdown_timeout", ssl_shutdown_timeout } }));
                        self.set_attr("_transport", new_transport);
                        protocol.method("_replace_transport", { new_transport });
                        return none();
                    }) },
                { "__del__", w.method("StreamWriter", "__del__", { py_positional("warnings", Mod<"warnings">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py warnings = a[1];
                        if (!self.attr("_transport").method("is_closing").truthy())
                        {
                            if (self.attr("_loop").method("is_closed").truthy())
                            {
                                warnings.method("warn", { S<"loop is closed">(), B<"ResourceWarning">() });
                            }
                            else
                            {
                                self.method("close");
                                warnings.method("warn", { S(std::string{ "unclosed " } + self.repr()), B<"ResourceWarning">() });
                            }
                        }
                        return none();
                    }) }, }));

            define("streams", "StreamReader", w.make_class("StreamReader", {  }, {
                { "_source_traceback", none() },
                { "__init__", w.method("StreamReader", "__init__", { py_positional("limit", G<"streams._DEFAULT_LIMIT">()), py_positional("loop", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py limit = a[1];
                        py loop = a[2];
                        if (limit <= (I<0>()))
                        {
                            py_raise((B<"ValueError">())(S<"Limit cannot be <= 0">()));
                        }
                        self.set_attr("_limit", limit);
                        if (loop.is_none())
                        {
                            self.set_attr("_loop", (G<"events.get_event_loop">())());
                        }
                        else
                        {
                            self.set_attr("_loop", loop);
                        }
                        self.set_attr("_buffer", (B<"bytearray">())());
                        self.set_attr("_eof", boolean(false));
                        self.set_attr("_waiter", none());
                        self.set_attr("_exception", none());
                        self.set_attr("_transport", none());
                        self.set_attr("_paused", boolean(false));
                        if (self.attr("_loop").method("get_debug").truthy())
                        {
                            self.set_attr("_source_traceback", (G<"format_helpers.extract_stack">())((Mod<"sys">().attr("_getframe"))(I<0>())));
                        }
                        return none();
                    }) },
                { "__repr__", w.method("StreamReader", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py info;
                        info = py_list({ S<"StreamReader">() });
                        if (self.attr("_buffer").truthy())
                        {
                            info.method("append", { S(py{ self.attr("_buffer").size() }.str() + " bytes") });
                        }
                        if (self.attr("_eof").truthy())
                        {
                            info.method("append", { S<"eof">() });
                        }
                        if (self.attr("_limit") != (G<"streams._DEFAULT_LIMIT">()))
                        {
                            info.method("append", { S(std::string{ "limit=" } + self.attr("_limit").str()) });
                        }
                        if (self.attr("_waiter").truthy())
                        {
                            info.method("append", { S(std::string{ "waiter=" } + self.attr("_waiter").repr()) });
                        }
                        if (self.attr("_exception").truthy())
                        {
                            info.method("append", { S(std::string{ "exception=" } + self.attr("_exception").repr()) });
                        }
                        if (self.attr("_transport").truthy())
                        {
                            info.method("append", { S(std::string{ "transport=" } + self.attr("_transport").repr()) });
                        }
                        if (self.attr("_paused").truthy())
                        {
                            info.method("append", { S<"paused">() });
                        }
                        return (S<"<{}>">()).method("format", { (S<" ">()).method("join", { info }) });
                    }) },
                { "exception", w.method("StreamReader", "exception", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_exception");
                    }) },
                { "set_exception", w.method("StreamReader", "set_exception", { py_positional("exc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc = a[1];
                        py waiter;
                        self.set_attr("_exception", exc);
                        waiter = self.attr("_waiter");
                        if (!waiter.is_none())
                        {
                            self.set_attr("_waiter", none());
                            if (!waiter.method("cancelled").truthy())
                            {
                                waiter.method("set_exception", { exc });
                            }
                        }
                        return none();
                    }) },
                { "_wakeup_waiter", w.method("StreamReader", "_wakeup_waiter", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py waiter;
                        waiter = self.attr("_waiter");
                        if (!waiter.is_none())
                        {
                            self.set_attr("_waiter", none());
                            if (!waiter.method("cancelled").truthy())
                            {
                                waiter.method("set_result", { none() });
                            }
                        }
                        return none();
                    }) },
                { "set_transport", w.method("StreamReader", "set_transport", { py_positional("transport") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py transport = a[1];
                        if (!self.attr("_transport").is_none())
                            py_raise(B<"AssertionError">()(S<"Transport already set">()));
                        self.set_attr("_transport", transport);
                        return none();
                    }) },
                { "_maybe_resume_transport", w.method("StreamReader", "_maybe_resume_transport", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if ((self.attr("_paused").truthy() && (py{ self.attr("_buffer").size() } <= self.attr("_limit"))))
                        {
                            self.set_attr("_paused", boolean(false));
                            self.attr("_transport").method("resume_reading");
                        }
                        return none();
                    }) },
                { "feed_eof", w.method("StreamReader", "feed_eof", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_eof", boolean(true));
                        self.method("_wakeup_waiter");
                        return none();
                    }) },
                { "at_eof", w.method("StreamReader", "at_eof", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const t = self.attr("_eof"); return !t.truthy() ? t : boolean(!self.attr("_buffer").truthy()); }();
                    }) },
                { "feed_data", w.method("StreamReader", "feed_data", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (self.attr("_eof").truthy())
                            py_raise(B<"AssertionError">()(S<"feed_data after feed_eof">()));
                        if (!data.truthy())
                        {
                            return none();
                        }
                        self.attr("_buffer").method("extend", { data });
                        self.method("_wakeup_waiter");
                        if (((!self.attr("_transport").is_none()) && (!self.attr("_paused").truthy()) && (py{ self.attr("_buffer").size() } > ((I<2>()) * self.attr("_limit")))))
                        {
                            {
                                bool otherwise7 = false;
                                try
                                {
                                    self.attr("_transport").method("pause_reading");
                                    otherwise7 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler7;
                                    if (matches(handler7.exception, B<"NotImplementedError">()))
                                    {
                                        // The transport can't be paused.
                                        // We'll just have to buffer all data.
                                        // Forget the transport so we don't keep trying.
                                        self.set_attr("_transport", none());
                                    }
                                    else
                                        py_reraise(handler7.exception);
                                }
                                if (otherwise7)
                                {
                                    self.set_attr("_paused", boolean(true));
                                }
                            }
                        }
                        return none();
                    }) },
                { "_wait_for_data", async_method(w, "StreamReader", "_wait_for_data", { py_positional("func_name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py func_name = a[1];
                        // StreamReader uses a future to link the protocol feed_data() method
                        // to a read coroutine. Running two read coroutines at the same time
                        // would have an unexpected behaviour. It would not possible to know
                        // which coroutine would get the next data.
                        if (!self.attr("_waiter").is_none())
                        {
                            py_raise((B<"RuntimeError">())(S(func_name.str() + "() called while another coroutine is already waiting for incoming data")));
                        }
                        if (self.attr("_eof").truthy())
                            py_raise(B<"AssertionError">()(S<"_wait_for_data after EOF">()));
                        // Waiting for data while paused will make deadlock, so prevent it.
                        // This is essential for readexactly(n) for case when n > self._limit.
                        if (self.attr("_paused").truthy())
                        {
                            self.set_attr("_paused", boolean(false));
                            self.attr("_transport").method("resume_reading");
                        }
                        self.set_attr("_waiter", self.attr("_loop").method("create_future"));
                        {
                            std::optional<py> pending8;
                            [[maybe_unused]] int flow8 = 0;
                            [[maybe_unused]] py result8;
                            try
                            {
                                py_await(self.attr("_waiter"));
                            }
                            catch (raised const&)
                            {
                                pending8 = py_take_exception();
                            }
                            self.set_attr("_waiter", none());
                            if (pending8)
                                py_reraise(*pending8);
                            if (flow8 == 1)
                                return result8;
                        }
                        return none();
                    }) },
                { "readline", async_method(w, "StreamReader", "readline", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py e;
                        py line;
                        py sep;
                        py seplen;
                        sep = py{ make_bytes(std::string{ "\x0a", 1 }) };
                        seplen = py{ sep.size() };
                        try
                        {
                            line = py_await(self.method("readuntil", { sep }));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler9;
                            if (matches(handler9.exception, G<"exceptions.IncompleteReadError">()))
                            {
                                e = handler9.exception;
                                return e.attr("partial");
                            }
                            else if (matches(handler9.exception, G<"exceptions.LimitOverrunError">()))
                            {
                                e = handler9.exception;
                                if (self.attr("_buffer").method("startswith", { sep, e.attr("consumed") }).truthy())
                                {
                                    del_item(self.attr("_buffer"), B<"slice">()(none(), e.attr("consumed") + seplen, none()));
                                }
                                else
                                {
                                    self.attr("_buffer").method("clear");
                                }
                                self.method("_maybe_resume_transport");
                                py_raise((B<"ValueError">())(e.attr("args")[I<0>()]));
                            }
                            else
                                py_reraise(handler9.exception);
                        }
                        return line;
                    }) },
                { "readuntil", async_method(w, "StreamReader", "readuntil", { py_positional("separator", py{ make_bytes(std::string{ "\x0a", 1 }) }) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py separator = a[1];
                        py buflen;
                        py chunk;
                        py end;
                        py isep;
                        py match_end;
                        py match_start;
                        py max_seplen;
                        py min_seplen;
                        py offset;
                        py sep;
                        if (isinstance(separator, B<"tuple">()))
                        {
                            // Makes sure shortest matches wins
                            separator = (B<"sorted">()).call({ separator }, { { "key", B<"len">() } });
                        }
                        else
                        {
                            separator = py_list({ separator });
                        }
                        if (!separator.truthy())
                        {
                            py_raise((B<"ValueError">())(S<"Separator should contain at least one element">()));
                        }
                        min_seplen = py{ separator[I<0>()].size() };
                        max_seplen = py{ separator[I<-1>()].size() };
                        if (min_seplen == (I<0>()))
                        {
                            py_raise((B<"ValueError">())(S<"Separator should be at least one-byte string">()));
                        }
                        if (!self.attr("_exception").is_none())
                        {
                            py_raise(self.attr("_exception"));
                        }
                        // Consume whole buffer except last bytes, which length is
                        // one less than max_seplen. Let's check corner cases with
                        // separator[-1]='SEPARATOR':
                        // * we have received almost complete separator (without last
                        // byte). i.e buffer='some textSEPARATO'. In this case we
                        // can safely consume max_seplen - 1 bytes.
                        // * last byte of buffer is first byte of separator, i.e.
                        // buffer='abcdefghijklmnopqrS'. We may safely consume
                        // everything except that last byte, but this require to
                        // analyze bytes of buffer that match partial separator.
                        // This is slow and/or require FSM. For this case our
                        // implementation is not optimal, since require rescanning
                        // of data that is known to not belong to separator. In
                        // real world, separator will not be so long to notice
                        // performance problems. Even when reading MIME-encoded
                        // messages :)
                        // `offset` is the number of bytes from the beginning of the buffer
                        // where there is no occurrence of any `separator`.
                        offset = I<0>();
                        // Loop until we find a `separator` in the buffer, exceed the buffer size,
                        // or an EOF has happened.
                        while (boolean(true).truthy())
                        {
                            buflen = py{ self.attr("_buffer").size() };
                            // Check if we now have enough data in the buffer for shortest
                            // separator to fit.
                            if ((buflen - offset) >= min_seplen)
                            {
                                match_start = none();
                                match_end = none();
                                for (py const& item : iterate{ separator })
                                {
                                    sep = item;
                                    isep = self.attr("_buffer").method("find", { sep, offset });
                                    if (isep != (I<-1>()))
                                    {
                                        // `separator` is in the buffer. `match_start` and
                                        // `match_end` will be used later to retrieve the
                                        // data.
                                        end = isep + py{ sep.size() };
                                        if ((match_end.is_none() || (end < match_end)))
                                        {
                                            match_end = end;
                                            match_start = isep;
                                        }
                                    }
                                }
                                if (!match_end.is_none())
                                {
                                    break;
                                }
                                // see upper comment for explanation.
                                offset = (B<"max">())(I<0>(), (buflen + (I<1>())) - max_seplen);
                                if (offset > self.attr("_limit"))
                                {
                                    py_raise((G<"exceptions.LimitOverrunError">())(S<"Separator is not found, and chunk exceed the limit">(), offset));
                                }
                            }
                            // Complete message (with full separator) may be present in buffer
                            // even when EOF flag is set. This may happen when the last chunk
                            // adds data which makes separator be found. That's why we check for
                            // EOF *after* inspecting the buffer.
                            if (self.attr("_eof").truthy())
                            {
                                chunk = (B<"bytes">())(self.attr("_buffer"));
                                self.attr("_buffer").method("clear");
                                py_raise((G<"exceptions.IncompleteReadError">())(chunk, none()));
                            }
                            // _wait_for_data() will resume reading if stream was paused.
                            py_await(self.method("_wait_for_data", { S<"readuntil">() }));
                        }
                        if (match_start > self.attr("_limit"))
                        {
                            py_raise((G<"exceptions.LimitOverrunError">())(S<"Separator is found, but chunk is longer than limit">(), match_start));
                        }
                        chunk = self.attr("_buffer")[B<"slice">()(none(), match_end, none())];
                        del_item(self.attr("_buffer"), B<"slice">()(none(), match_end, none()));
                        self.method("_maybe_resume_transport");
                        return (B<"bytes">())(chunk);
                    }) },
                { "read", async_method(w, "StreamReader", "read", { py_positional("n", I<-1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n = a[1];
                        py block;
                        py blocks;
                        py data;
                        if (!self.attr("_exception").is_none())
                        {
                            py_raise(self.attr("_exception"));
                        }
                        if (n == (I<0>()))
                        {
                            return py{ make_bytes(std::string{ "", 0 }) };
                        }
                        if (n < (I<0>()))
                        {
                            // This used to just loop creating a new waiter hoping to
                            // collect everything in self._buffer, but that would
                            // deadlock if the subprocess sends more than self.limit
                            // bytes.  So just call self.read(self._limit) until EOF.
                            blocks = py_list();
                            while (boolean(true).truthy())
                            {
                                block = py_await(self.method("read", { self.attr("_limit") }));
                                if (!block.truthy())
                                {
                                    break;
                                }
                                blocks.method("append", { block });
                            }
                            return py{ make_bytes(std::string{ "", 0 }) }.method("join", { blocks });
                        }
                        if (((!self.attr("_buffer").truthy()) && (!self.attr("_eof").truthy())))
                        {
                            py_await(self.method("_wait_for_data", { S<"read">() }));
                        }
                        // This will work right even if buffer is less than n bytes
                        data = (B<"bytes">())((B<"memoryview">())(self.attr("_buffer"))[B<"slice">()(none(), n, none())]);
                        del_item(self.attr("_buffer"), B<"slice">()(none(), n, none()));
                        self.method("_maybe_resume_transport");
                        return data;
                    }) },
                { "readexactly", async_method(w, "StreamReader", "readexactly", { py_positional("n") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n = a[1];
                        py data;
                        py incomplete;
                        if (n < (I<0>()))
                        {
                            py_raise((B<"ValueError">())(S<"readexactly size can not be less than zero">()));
                        }
                        if (!self.attr("_exception").is_none())
                        {
                            py_raise(self.attr("_exception"));
                        }
                        if (n == (I<0>()))
                        {
                            return py{ make_bytes(std::string{ "", 0 }) };
                        }
                        while (py{ self.attr("_buffer").size() } < n)
                        {
                            if (self.attr("_eof").truthy())
                            {
                                incomplete = (B<"bytes">())(self.attr("_buffer"));
                                self.attr("_buffer").method("clear");
                                py_raise((G<"exceptions.IncompleteReadError">())(incomplete, n));
                            }
                            py_await(self.method("_wait_for_data", { S<"readexactly">() }));
                        }
                        if (py{ self.attr("_buffer").size() } == n)
                        {
                            data = (B<"bytes">())(self.attr("_buffer"));
                            self.attr("_buffer").method("clear");
                        }
                        else
                        {
                            data = (B<"bytes">())((B<"memoryview">())(self.attr("_buffer"))[B<"slice">()(none(), n, none())]);
                            del_item(self.attr("_buffer"), B<"slice">()(none(), n, none()));
                        }
                        self.method("_maybe_resume_transport");
                        return data;
                    }) },
                { "__aiter__", w.method("StreamReader", "__aiter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__anext__", async_method(w, "StreamReader", "__anext__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py val;
                        val = py_await(self.method("readline"));
                        if (val == py{ make_bytes(std::string{ "", 0 }) })
                        {
                            py_raise(B<"StopAsyncIteration">());
                        }
                        return val;
                    }) }, }));
        }

        // --- staggered (Lib/asyncio/staggered.py)

        void make_staggered()
        {
            static module_writer const& w = writer("staggered");
            define("staggered", "__all__", py_tuple({ S<"staggered_race">() }));
            define("staggered", "staggered_race", async_function(w, "staggered_race", { py_positional("coro_fns"), py_positional("delay"), py_keyword("loop", none()) }, [](arguments a) -> py
                {
                    py coro_fns = a[0];
                    auto const delay = std::make_shared<py>(a[1]);
                    auto const loop = std::make_shared<py>(a[2]);
                    auto const enum_coro_fns = std::make_shared<py>();
                    py ex;
                    auto const exceptions = std::make_shared<py>();
                    py first_task;
                    py ok_to_start;
                    auto const on_completed_fut = std::make_shared<py>();
                    py propagate_cancellation_error;
                    auto const run_one_coro = std::make_shared<py>();
                    auto const running_tasks = std::make_shared<py>();
                    py task;
                    auto const task_done = std::make_shared<py>();
                    auto const unhandled_exceptions = std::make_shared<py>();
                    auto const winner_index = std::make_shared<py>();
                    auto const winner_result = std::make_shared<py>();
                    // TODO: when we have aiter() and anext(), allow async iterables in coro_fns.
                    *loop = [&]() -> py { py const t = (*loop); return t.truthy() ? t : (G<"events.get_running_loop">())(); }();
                    *enum_coro_fns = (B<"enumerate">())(coro_fns);
                    *winner_result = none();
                    *winner_index = none();
                    *unhandled_exceptions = py_list();
                    *exceptions = py_list();
                    *running_tasks = (B<"set">())();
                    *on_completed_fut = none();
                    *task_done = w.function("task_done", "staggered_race.<locals>.task_done", { py_positional("task") }, [on_completed_fut, running_tasks, unhandled_exceptions](arguments a) -> py
    {
        py task = a[0];
        py exc;
        (*running_tasks).method("discard", { task });
        if (((!(*on_completed_fut).is_none()) && (!(*on_completed_fut).method("done").truthy()) && (!(*running_tasks).truthy())))
        {
            (*on_completed_fut).method("set_result", { none() });
        }
        if (task.method("cancelled").truthy())
        {
            return none();
        }
        exc = task.method("exception");
        if (exc.is_none())
        {
            return none();
        }
        (*unhandled_exceptions).method("append", { exc });
        return none();
    });
                    *run_one_coro = async_function(w, "staggered_race.<locals>.run_one_coro", { py_positional("ok_to_start"), py_positional("previous_failed") }, [delay, enum_coro_fns, exceptions, loop, run_one_coro, running_tasks, task_done, winner_index, winner_result](arguments a) -> py
    {
        py ok_to_start = a[0];
        py previous_failed = a[1];
        py coro_fn;
        py current_task;
        py e;
        py next_ok_to_start;
        py next_task;
        py result;
        py t;
        py this_failed;
        py this_index;
        py_await(ok_to_start.method("wait"));
        // Wait for the previous task to finish, or for delay seconds
        if (!previous_failed.is_none())
        {
            {
                py const manager1 = (Mod<"contextlib">().attr("suppress"))(G<"exceptions.TimeoutError">());
                py const exit1 = pyclass::special_method(manager1, "__exit__");
                py const value1 = pyclass::special_method(manager1, "__enter__")(manager1);
                [[maybe_unused]] int flow1 = 0;
                [[maybe_unused]] py result1;
                bool exited1 = false;
                try
                {
                    // Use asyncio.wait_for() instead of asyncio.wait() here, so
                    // that if we get cancelled at this point, Event.wait() is also
                    // cancelled, otherwise there will be a "Task destroyed but it is
                    // pending" later.
                    py_await((G<"tasks.wait_for">())(previous_failed.method("wait"), (*delay)));
                }
                catch (raised const&)
                {
                    pyclass::handler_scope handler1;
                    exited1 = true;
                    if (!exit1(manager1, type_of(handler1.exception), handler1.exception, handler1.exception.attr("__traceback__")).truthy())
                        py_reraise(handler1.exception);
                }
                if (!exited1)
                    exit1(manager1, none(), none(), none());
                if (flow1 == 1)
                    return result1;
            }
        }
        // Get the next coroutine to run
        try
        {
            auto const items3 = unpack((B<"next">())((*enum_coro_fns)), 2);
            this_index = items3[0];
            coro_fn = items3[1];
        }
        catch (raised const&)
        {
            pyclass::handler_scope handler2;
            if (matches(handler2.exception, B<"StopIteration">()))
            {
                return none();
            }
            else
                py_reraise(handler2.exception);
        }
        // Start task that will run the next coroutine
        this_failed = (G<"locks.Event">())();
        next_ok_to_start = (G<"locks.Event">())();
        next_task = (*loop).method("create_task", { (*run_one_coro)(next_ok_to_start, this_failed) });
        (*running_tasks).method("add", { next_task });
        next_task.method("add_done_callback", { (*task_done) });
        // next_task has been appended to running_tasks so next_task is ok to
        // start.
        next_ok_to_start.method("set");
        // Prepare place to put this coroutine's exceptions if not won
        (*exceptions).method("append", { none() });
        if (!(py{ (*exceptions).size() } == (this_index + (I<1>()))))
            py_raise(B<"AssertionError">()());
        {
            bool otherwise4 = false;
            try
            {
                result = py_await(coro_fn());
                otherwise4 = true;
            }
            catch (raised const&)
            {
                pyclass::handler_scope handler4;
                if (matches(handler4.exception, py_tuple({ B<"SystemExit">(), B<"KeyboardInterrupt">() })))
                {
                    py_reraise(handler4.exception);
                }
                else if (matches(handler4.exception, B<"BaseException">()))
                {
                    e = handler4.exception;
                    (*exceptions).set_item(this_index, e);
                    this_failed.method("set");
                }
                else
                    py_reraise(handler4.exception);
            }
            if (otherwise4)
            {
                // Store winner's results
                if (!(*winner_index).is_none())
                    py_raise(B<"AssertionError">()());
                *winner_index = this_index;
                *winner_result = result;
                // Cancel all other tasks. We take care to not cancel the current
                // task as well. If we do so, then since there is no `await` after
                // here and CancelledError are usually thrown at one, we will
                // encounter a curious corner case where the current task will end
                // up as done() == True, cancelled() == False, exception() ==
                // asyncio.CancelledError. This behavior is specified in
                // https://bugs.python.org/issue30048
                current_task = (G<"tasks.current_task">())((*loop));
                for (py const& item : iterate{ (*running_tasks) })
                {
                    t = item;
                    if (!t.is(current_task))
                    {
                        t.method("cancel");
                    }
                }
            }
        }
        return none();
    });
                    propagate_cancellation_error = none();
                    {
                        std::optional<py> pending6;
                        [[maybe_unused]] int flow6 = 0;
                        [[maybe_unused]] py result6;
                        try
                        {
                            ok_to_start = (G<"locks.Event">())();
                            first_task = (*loop).method("create_task", { (*run_one_coro)(ok_to_start, none()) });
                            (*running_tasks).method("add", { first_task });
                            first_task.method("add_done_callback", { (*task_done) });
                            // first_task has been appended to running_tasks so first_task is ok to start.
                            ok_to_start.method("set");
                            propagate_cancellation_error = none();
                            // Make sure no tasks are left running if we leave this function
                            while ((*running_tasks).truthy())
                            {
                                *on_completed_fut = (*loop).method("create_future");
                                try
                                {
                                    py_await((*on_completed_fut));
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler8;
                                    if (matches(handler8.exception, G<"exceptions.CancelledError">()))
                                    {
                                        ex = handler8.exception;
                                        propagate_cancellation_error = ex;
                                        for (py const& item : iterate{ (*running_tasks) })
                                        {
                                            task = item;
                                            call_ex(task.attr("cancel"), {  }, ex.attr("args"), py{});
                                        }
                                    }
                                    else
                                        py_reraise(handler8.exception);
                                }
                                *on_completed_fut = none();
                            }
                            if (((B<"__debug__">()).truthy() && (*unhandled_exceptions).truthy()))
                            {
                                // If run_one_coro raises an unhandled exception, it's probably a
                                // programming error, and I want to see it.
                                py_raise((B<"ExceptionGroup">())(S<"staggered race failed">(), (*unhandled_exceptions)));
                            }
                            if (!propagate_cancellation_error.is_none())
                            {
                                py_raise(propagate_cancellation_error);
                            }
                            result6 = py_tuple({ (*winner_result), (*winner_index), (*exceptions) });
                            flow6 = 1;
                            goto finally6;
                        }
                        catch (raised const&)
                        {
                            pending6 = py_take_exception();
                        }
                        finally6:
                        *exceptions = none();
                        propagate_cancellation_error = none();
                        *unhandled_exceptions = none();
                        if (pending6)
                            py_reraise(*pending6);
                        if (flow6 == 1)
                            return result6;
                    }
                    return none();
                }));
        }

        // --- subprocess (Lib/asyncio/subprocess.py)

        void make_subprocess()
        {
            static module_writer const& w = writer("subprocess");
            define("subprocess", "__all__", py_tuple({ S<"create_subprocess_exec">(), S<"create_subprocess_shell">() }));
            define("subprocess", "PIPE", py{ -1 } /* subprocess.PIPE */);
            define("subprocess", "STDOUT", py{ -2 } /* subprocess.STDOUT */);
            define("subprocess", "DEVNULL", py{ -3 } /* subprocess.DEVNULL */);
            define("subprocess", "SubprocessStreamProtocol", w.make_class("SubprocessStreamProtocol", { G<"streams.FlowControlMixin">(), G<"protocols.SubprocessProtocol">() }, {
                { "__init__", w.method("SubprocessStreamProtocol", "__init__", { py_positional("limit"), py_positional("loop") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py limit = a[1];
                        py loop = a[2];
                        (B<"super">()(G<"subprocess.SubprocessStreamProtocol">(), self)).method("__init__", {  }, { { "loop", loop } });
                        self.set_attr("_limit", limit);
                        py const value1 = none();
                        self.set_attr("stdin", value1);
                        self.set_attr("stdout", value1);
                        self.set_attr("stderr", value1);
                        self.set_attr("_transport", none());
                        self.set_attr("_process_exited", boolean(false));
                        self.set_attr("_pipe_fds", py_list());
                        self.set_attr("_stdin_closed", self.attr("_loop").method("create_future"));
                        return none();
                    }) },
                { "__repr__", w.method("SubprocessStreamProtocol", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py info;
                        info = py_list({ self.attr("__class__").attr("__name__") });
                        if (!self.attr("stdin").is_none())
                        {
                            info.method("append", { S(std::string{ "stdin=" } + self.attr("stdin").repr()) });
                        }
                        if (!self.attr("stdout").is_none())
                        {
                            info.method("append", { S(std::string{ "stdout=" } + self.attr("stdout").repr()) });
                        }
                        if (!self.attr("stderr").is_none())
                        {
                            info.method("append", { S(std::string{ "stderr=" } + self.attr("stderr").repr()) });
                        }
                        return (S<"<{}>">()).method("format", { (S<" ">()).method("join", { info }) });
                    }) },
                { "connection_made", w.method("SubprocessStreamProtocol", "connection_made", { py_positional("transport") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py transport = a[1];
                        py stderr_transport;
                        py stdin_transport;
                        py stdout_transport;
                        self.set_attr("_transport", transport);
                        stdout_transport = transport.method("get_pipe_transport", { I<1>() });
                        if (!stdout_transport.is_none())
                        {
                            self.set_attr("stdout", (G<"streams.StreamReader">()).call({  }, { { "limit", self.attr("_limit") }, { "loop", self.attr("_loop") } }));
                            self.attr("stdout").method("set_transport", { stdout_transport });
                            self.attr("_pipe_fds").method("append", { I<1>() });
                        }
                        stderr_transport = transport.method("get_pipe_transport", { I<2>() });
                        if (!stderr_transport.is_none())
                        {
                            self.set_attr("stderr", (G<"streams.StreamReader">()).call({  }, { { "limit", self.attr("_limit") }, { "loop", self.attr("_loop") } }));
                            self.attr("stderr").method("set_transport", { stderr_transport });
                            self.attr("_pipe_fds").method("append", { I<2>() });
                        }
                        stdin_transport = transport.method("get_pipe_transport", { I<0>() });
                        if (!stdin_transport.is_none())
                        {
                            self.set_attr("stdin", (G<"streams.StreamWriter">()).call({ stdin_transport }, { { "protocol", self }, { "reader", none() }, { "loop", self.attr("_loop") } }));
                        }
                        return none();
                    }) },
                { "pipe_data_received", w.method("SubprocessStreamProtocol", "pipe_data_received", { py_positional("fd"), py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        py data = a[2];
                        py reader;
                        if (fd == (I<1>()))
                        {
                            reader = self.attr("stdout");
                        }
                        else if (fd == (I<2>()))
                        {
                            reader = self.attr("stderr");
                        }
                        else
                        {
                            reader = none();
                        }
                        if (!reader.is_none())
                        {
                            reader.method("feed_data", { data });
                        }
                        return none();
                    }) },
                { "pipe_connection_lost", w.method("SubprocessStreamProtocol", "pipe_connection_lost", { py_positional("fd"), py_positional("exc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        py exc = a[2];
                        py pipe;
                        py reader;
                        if (fd == (I<0>()))
                        {
                            pipe = self.attr("stdin");
                            if (!pipe.is_none())
                            {
                                pipe.method("close");
                            }
                            self.method("connection_lost", { exc });
                            if (exc.is_none())
                            {
                                self.attr("_stdin_closed").method("set_result", { none() });
                            }
                            else
                            {
                                self.attr("_stdin_closed").method("set_exception", { exc });
                                // Since calling `wait_closed()` is not mandatory,
                                // we shouldn't log the traceback if this is not awaited.
                                self.attr("_stdin_closed").set_attr("_log_traceback", boolean(false));
                            }
                            return none();
                        }
                        if (fd == (I<1>()))
                        {
                            reader = self.attr("stdout");
                        }
                        else if (fd == (I<2>()))
                        {
                            reader = self.attr("stderr");
                        }
                        else
                        {
                            reader = none();
                        }
                        if (!reader.is_none())
                        {
                            if (exc.is_none())
                            {
                                reader.method("feed_eof");
                            }
                            else
                            {
                                reader.method("set_exception", { exc });
                            }
                        }
                        if (self.attr("_pipe_fds").contains(fd))
                        {
                            self.attr("_pipe_fds").method("remove", { fd });
                        }
                        self.method("_maybe_close_transport");
                        return none();
                    }) },
                { "process_exited", w.method("SubprocessStreamProtocol", "process_exited", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_process_exited", boolean(true));
                        self.method("_maybe_close_transport");
                        return none();
                    }) },
                { "_maybe_close_transport", w.method("SubprocessStreamProtocol", "_maybe_close_transport", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (((py{ self.attr("_pipe_fds").size() } == (I<0>())) && self.attr("_process_exited").truthy()))
                        {
                            self.attr("_transport").method("close");
                            self.set_attr("_transport", none());
                        }
                        return none();
                    }) },
                { "_get_close_waiter", w.method("SubprocessStreamProtocol", "_get_close_waiter", { py_positional("stream") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py stream = a[1];
                        if (stream.is(self.attr("stdin")))
                        {
                            return self.attr("_stdin_closed");
                        }
                        return none();
                    }) }, }));

            define("subprocess", "Process", w.make_class("Process", {  }, {
                { "__init__", w.method("Process", "__init__", { py_positional("transport"), py_positional("protocol"), py_positional("loop") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py transport = a[1];
                        py protocol = a[2];
                        py loop = a[3];
                        self.set_attr("_transport", transport);
                        self.set_attr("_protocol", protocol);
                        self.set_attr("_loop", loop);
                        self.set_attr("stdin", protocol.attr("stdin"));
                        self.set_attr("stdout", protocol.attr("stdout"));
                        self.set_attr("stderr", protocol.attr("stderr"));
                        self.set_attr("pid", transport.method("get_pid"));
                        return none();
                    }) },
                { "__repr__", w.method("Process", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return S(std::string{ "<" } + self.attr("__class__").attr("__name__").str() + " " + self.attr("pid").str() + ">");
                    }) },
                { "returncode", B<"property">()(w.method("Process", "returncode", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_transport").method("get_returncode");
                    })) },
                { "wait", async_method(w, "Process", "wait", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py_await(self.attr("_transport").method("_wait"));
                    }) },
                { "send_signal", w.method("Process", "send_signal", { py_positional("signal") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py signal = a[1];
                        self.attr("_transport").method("send_signal", { signal });
                        return none();
                    }) },
                { "terminate", w.method("Process", "terminate", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("_transport").method("terminate");
                        return none();
                    }) },
                { "kill", w.method("Process", "kill", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("_transport").method("kill");
                        return none();
                    }) },
                { "_feed_stdin", async_method(w, "Process", "_feed_stdin", { py_positional("input") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py input = a[1];
                        py debug;
                        py exc;
                        debug = self.attr("_loop").method("get_debug");
                        try
                        {
                            if (!input.is_none())
                            {
                                self.attr("stdin").method("write", { input });
                                if (debug.truthy())
                                {
                                    (G<"log.logger">()).method("debug", { S<"%r communicate: feed stdin (%s bytes)">(), self, py{ input.size() } });
                                }
                            }
                            py_await(self.attr("stdin").method("drain"));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler2;
                            if (matches(handler2.exception, py_tuple({ B<"BrokenPipeError">(), B<"ConnectionResetError">() })))
                            {
                                exc = handler2.exception;
                                // communicate() ignores BrokenPipeError and ConnectionResetError.
                                // write() and drain() can raise these exceptions.
                                if (debug.truthy())
                                {
                                    (G<"log.logger">()).method("debug", { S<"%r communicate: stdin got %r">(), self, exc });
                                }
                            }
                            else
                                py_reraise(handler2.exception);
                        }
                        if (debug.truthy())
                        {
                            (G<"log.logger">()).method("debug", { S<"%r communicate: close stdin">(), self });
                        }
                        self.attr("stdin").method("close");
                        return none();
                    }) },
                { "_noop", async_method(w, "Process", "_noop", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "_read_stream", async_method(w, "Process", "_read_stream", { py_positional("fd") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fd = a[1];
                        py name;
                        py output;
                        py stream;
                        py transport;
                        transport = self.attr("_transport").method("get_pipe_transport", { fd });
                        if (fd == (I<2>()))
                        {
                            stream = self.attr("stderr");
                        }
                        else
                        {
                            if (!(fd == (I<1>())))
                                py_raise(B<"AssertionError">()());
                            stream = self.attr("stdout");
                        }
                        if (self.attr("_loop").method("get_debug").truthy())
                        {
                            name = (fd == (I<1>()) ? S<"stdout">() : S<"stderr">());
                            (G<"log.logger">()).method("debug", { S<"%r communicate: read %s">(), self, name });
                        }
                        output = py_await(stream.method("read"));
                        if (self.attr("_loop").method("get_debug").truthy())
                        {
                            name = (fd == (I<1>()) ? S<"stdout">() : S<"stderr">());
                            (G<"log.logger">()).method("debug", { S<"%r communicate: close %s">(), self, name });
                        }
                        transport.method("close");
                        return output;
                    }) },
                { "communicate", async_method(w, "Process", "communicate", { py_positional("input", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py input = a[1];
                        py stderr_;
                        py stdin_;
                        py stdout_;
                        if (!self.attr("stdin").is_none())
                        {
                            stdin_ = self.method("_feed_stdin", { input });
                        }
                        else
                        {
                            stdin_ = self.method("_noop");
                        }
                        if (!self.attr("stdout").is_none())
                        {
                            stdout_ = self.method("_read_stream", { I<1>() });
                        }
                        else
                        {
                            stdout_ = self.method("_noop");
                        }
                        if (!self.attr("stderr").is_none())
                        {
                            stderr_ = self.method("_read_stream", { I<2>() });
                        }
                        else
                        {
                            stderr_ = self.method("_noop");
                        }
                        auto const items3 = unpack(py_await((G<"tasks.gather">())(stdin_, stdout_, stderr_)), 3);
                        stdin_ = items3[0];
                        stdout_ = items3[1];
                        stderr_ = items3[2];
                        py_await(self.method("wait"));
                        return py_tuple({ stdout_, stderr_ });
                    }) }, }));

            define("subprocess", "create_subprocess_shell", async_function(w, "create_subprocess_shell", { py_positional("cmd"), py_positional("stdin", none()), py_positional("stdout", none()), py_positional("stderr", none()), py_positional("limit", G<"streams._DEFAULT_LIMIT">()) }, [](arguments a) -> py
                {
                    py cmd = a[0];
                    py stdin_ = a[1];
                    py stdout_ = a[2];
                    py stderr_ = a[3];
                    auto const limit = std::make_shared<py>(a[4]);
                    py kwds = a[5];
                    auto const loop = std::make_shared<py>();
                    py protocol;
                    py protocol_factory;
                    py transport;
                    *loop = (G<"events.get_running_loop">())();
                    protocol_factory = w.function("<lambda>", "create_subprocess_shell.<locals>.<lambda>", {  }, [limit, loop](arguments a) -> py
    {
        return (G<"subprocess.SubprocessStreamProtocol">()).call({  }, { { "limit", (*limit) }, { "loop", (*loop) } });
    });
                    auto const items4 = unpack(py_await(call_ex((*loop).attr("subprocess_shell"), { protocol_factory, cmd }, py{}, kwds, { { "stdin", stdin_ }, { "stdout", stdout_ }, { "stderr", stderr_ } })), 2);
                    transport = items4[0];
                    protocol = items4[1];
                    return (G<"subprocess.Process">())(transport, protocol, (*loop));
                }, false, true));

            define("subprocess", "create_subprocess_exec", async_function(w, "create_subprocess_exec", { py_positional("program"), py_keyword("stdin", none()), py_keyword("stdout", none()), py_keyword("stderr", none()), py_keyword("limit", G<"streams._DEFAULT_LIMIT">()) }, [](arguments a) -> py
                {
                    py program = a[0];
                    py stdin_ = a[1];
                    py stdout_ = a[2];
                    py stderr_ = a[3];
                    auto const limit = std::make_shared<py>(a[4]);
                    py args = a[5];
                    py kwds = a[6];
                    auto const loop = std::make_shared<py>();
                    py protocol;
                    py protocol_factory;
                    py transport;
                    *loop = (G<"events.get_running_loop">())();
                    protocol_factory = w.function("<lambda>", "create_subprocess_exec.<locals>.<lambda>", {  }, [limit, loop](arguments a) -> py
    {
        return (G<"subprocess.SubprocessStreamProtocol">()).call({  }, { { "limit", (*limit) }, { "loop", (*loop) } });
    });
                    auto const items5 = unpack(py_await(call_ex((*loop).attr("subprocess_exec"), { protocol_factory, program }, args, kwds, { { "stdin", stdin_ }, { "stdout", stdout_ }, { "stderr", stderr_ } })), 2);
                    transport = items5[0];
                    protocol = items5[1];
                    return (G<"subprocess.Process">())(transport, protocol, (*loop));
                }, true, true));
        }
    }

    void make_sync()
    {
        make_locks();
        make_queues();
        make_timeouts();
        make_taskgroups();
        make_streams();
        make_staggered();
        make_subprocess();
    }
}
