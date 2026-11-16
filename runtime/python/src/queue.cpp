/*
  queue.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/queue.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The queue module (CPython 3.13's Lib/queue.py): Queue, LifoQueue and PriorityQueue (Lib/queue.py's classes: their _init, _qsize, _put and
// _get can be overridden, and their mutex and conditions are the threading module's), SimpleQueue (the C one: synchronized, its blocking get
// releases the GIL), and the exceptions Empty, Full and ShutDown

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            // (a SimpleQueue (CPython's Modules/_queuemodule.c))
            struct queue_object
            {
                header h;
                std::mutex mutex;
                std::condition_variable notEmpty;
                std::deque<object*> items;
                ~queue_object() { for (auto i : items) release(i); }
            };

            neos_py_type make_queue_type(char const* aQualifiedName, char const* aName);
            neos_py_type sSimpleQueueType = make_queue_type("_queue.SimpleQueue", "SimpleQueue");

            queue_object& as_queue(object* aObject) { return *reinterpret_cast<queue_object*>(aObject); }

            bool timeout_of(object* aValue, double& aResult)
            {
                aResult = -1.0;
                if (aValue == nullptr || kind_of(aValue) == NEOS_PY_NONE)
                    return true;
                if (!to_double(aValue, aResult))
                    return false;
                if (aResult < 0.0)
                {
                    error("ValueError", "'timeout' must be a non-negative number");
                    return false;
                }
                return true;
            }

            object* empty_error() { return error("_queue.Empty", ""); }

            // (wait with the GIL released: false if it timed out)
            template <typename Predicate>
            bool wait(std::unique_lock<std::mutex>& aLock, std::condition_variable& aCondition, double aTimeout, Predicate aPredicate)
            {
                if (aPredicate())
                    return true;
                aLock.unlock();
                gil_release();
                aLock.lock();
                bool result = true;
                if (aTimeout < 0.0)
                    aCondition.wait(aLock, aPredicate);
                else
                    result = aCondition.wait_for(aLock, std::chrono::duration<double>{ aTimeout }, aPredicate);
                aLock.unlock();
                gil_acquire();
                aLock.lock();
                return result && aPredicate();
            }

            // (the block and timeout arguments are ignored: it never blocks)
            object* put(object* aSelf, object* aItem)
            {
                auto& q = as_queue(aSelf);
                retain(aItem);
                {
                    std::lock_guard<std::mutex> lock{ q.mutex };
                    q.items.push_back(aItem);
                }
                q.notEmpty.notify_one();
                return none();
            }

            object* get(object* aSelf, bool aBlock, double aTimeout)
            {
                auto& q = as_queue(aSelf);
                std::unique_lock<std::mutex> lock{ q.mutex };
                auto const available = [&]() { return !q.items.empty(); };
                if (!aBlock)
                {
                    if (!available())
                        return (lock.unlock(), empty_error());
                }
                else
                {
                    wait(lock, q.notEmpty, aTimeout, available);
                    if (!available())
                        return (lock.unlock(), empty_error());
                }
                auto const item = q.items.front();
                q.items.pop_front();
                lock.unlock();
                if (failed())
                {
                    release(item);
                    return nullptr;
                }
                return item;
            }

            struct queue_method
            {
                char const* name;
                object* (*function)(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs);
            };

            queue_method const sMethods[] =
            {
                { "put", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("put", aArgs, aKwargs, { "item", "block", "timeout" }, 1u, 0u, a))
                            return nullptr;
                        return put(aSelf, a[0]);
                    } },
                { "put_nowait", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("put_nowait", aArgs, aKwargs, { "item" }, 1u, 0u, a))
                            return nullptr;
                        return put(aSelf, a[0]);
                    } },
                { "get", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("get", aArgs, aKwargs, { "block", "timeout" }, 0u, 0u, a))
                            return nullptr;
                        // (not blocking: the timeout is ignored)
                        bool const block = a[0] == nullptr || truth(a[0]) == 1;
                        double timeout = -1.0;
                        if (block && !timeout_of(a[1], timeout))
                            return nullptr;
                        return get(aSelf, block, timeout);
                    } },
                { "get_nowait", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("get_nowait", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        return get(aSelf, false, -1.0);
                    } },
                { "qsize", [](object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        std::lock_guard<std::mutex> lock{ as_queue(aSelf).mutex };
                        return make_int(static_cast<long long>(as_queue(aSelf).items.size()));
                    } },
                { "empty", [](object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        std::lock_guard<std::mutex> lock{ as_queue(aSelf).mutex };
                        return make_bool(as_queue(aSelf).items.empty());
                    } }
            };

            object* call_method(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& method = *static_cast<queue_method const*>(aData);
                auto const& items = as<tuple_object>(aArgs)->items;
                std::vector<object*> const args{ items.begin() + 1, items.end() };
                return method.function(items[0], args, aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty() ? aKwargs : nullptr);
            }

            neos_py_type make_queue_type(char const* aQualifiedName, char const* aName)
            {
                auto type = make_type(aQualifiedName, aName, retain_object, release_object<queue_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    return make_str(std::string{ "<" } + aSelf->type->name + " object at " + object_address(aSelf) + ">");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    for (auto const& m : sMethods)
                        if (std::strcmp(m.name, aName) == 0)
                            return make_function(m.name, call_method, const_cast<queue_method*>(&m), aSelf);
                    return error("AttributeError", std::string{ "'" } + python_type(aSelf)->name + "' object has no attribute '" + aName + "'");
                };
                type.setattr = [](neos_py_context*, object* aSelf, char const* aName, object*) -> int
                {
                    error("AttributeError", std::string{ "'" } + python_type(aSelf)->name + "' object attribute '" + aName + "' is read-only");
                    return -1;
                };
                return type;
            }

            object* new_simple_queue(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("SimpleQueue", as<tuple_object>(aArgs)->items, aKwargs, {}, 0u, 0u, a))
                    return nullptr;
                auto const q = new queue_object{ { { &sSimpleQueueType.base } } };
                return &q->h.base;
            }

            int instance_of(neos_py_context*, void*, object* aObject)
            {
                return aObject->type == &sSimpleQueueType.base ? 1 : 0;
            }

            // (Queue, LifoQueue and PriorityQueue: Lib/queue.py's, as they are (the methods of a queue that waits: with its mutex held))
            std::vector<py> py_queue_classes()
            {
                using namespace pyclass;
                static module_writer const w{ "queue", nullptr };
                auto const shut_down = []() { py_raise(M("queue", "ShutDown")()); };
                py queue = w.make_class("Queue", {}, {
                    { "__doc__", S("Create a queue object with a given maximum size.\n\n    If maxsize is <= 0, the queue size is infinite.\n    ") },
                    { "__init__", w.method("Queue", "__init__", { py_positional("maxsize", I<0>()) }, [](arguments a) -> py
                        {
                            py self = a[0];
                            py maxsize = a[1];
                            self.set_attr("maxsize", maxsize);
                            self.method("_init", { maxsize });
                            // mutex must be held whenever the queue is mutating.  All methods
                            // that acquire mutex must release it before returning.  mutex
                            // is shared between the three conditions, so acquiring and
                            // releasing the conditions also acquires and releases mutex.
                            self.set_attr("mutex", (Mod<"threading">().attr("Lock"))());
                            // Notify not_empty whenever an item is added to the queue; a
                            // thread waiting to get is notified then.
                            self.set_attr("not_empty", (Mod<"threading">().attr("Condition"))(self.attr("mutex")));
                            // Notify not_full whenever an item is removed from the queue;
                            // a thread waiting to put is notified then.
                            self.set_attr("not_full", (Mod<"threading">().attr("Condition"))(self.attr("mutex")));
                            // Notify all_tasks_done whenever the number of unfinished tasks
                            // drops to zero; thread waiting to join() is notified to resume
                            self.set_attr("all_tasks_done", (Mod<"threading">().attr("Condition"))(self.attr("mutex")));
                            self.set_attr("unfinished_tasks", I<0>());
                            // Queue shutdown state
                            self.set_attr("is_shutdown", boolean(false));
                            return py::none();
                        }) },
                    { "task_done", w.method("Queue", "task_done", {}, [](arguments a) -> py
                        {
                            py self = a[0];
                            with_(self.attr("all_tasks_done"), [&](py const&)
                            {
                                py const unfinished = self.attr("unfinished_tasks") - I<1>();
                                if (unfinished <= I<0>())
                                {
                                    if (unfinished < I<0>())
                                        py_raise("ValueError", std::string{ "task_done() called too many times" });
                                    self.attr("all_tasks_done").method("notify_all");
                                }
                                self.set_attr("unfinished_tasks", unfinished);
                            });
                            return py::none();
                        }) },
                    { "join", w.method("Queue", "join", {}, [](arguments a) -> py
                        {
                            py self = a[0];
                            with_(self.attr("all_tasks_done"), [&](py const&)
                            {
                                while (self.attr("unfinished_tasks").truthy())
                                    self.attr("all_tasks_done").method("wait");
                            });
                            return py::none();
                        }) },
                    { "qsize", w.method("Queue", "qsize", {}, [](arguments a) -> py
                        {
                            py self = a[0];
                            py result;
                            with_(self.attr("mutex"), [&](py const&) { result = self.method("_qsize"); });
                            return result;
                        }) },
                    { "empty", w.method("Queue", "empty", {}, [](arguments a) -> py
                        {
                            py self = a[0];
                            py result;
                            with_(self.attr("mutex"), [&](py const&) { result = boolean(!self.method("_qsize").truthy()); });
                            return result;
                        }) },
                    { "full", w.method("Queue", "full", {}, [](arguments a) -> py
                        {
                            py self = a[0];
                            py result;
                            with_(self.attr("mutex"), [&](py const&)
                            {
                                py const maxsize = self.attr("maxsize");
                                result = boolean(I<0>() < maxsize && maxsize <= self.method("_qsize"));
                            });
                            return result;
                        }) },
                    { "put", w.method("Queue", "put", { py_positional("item"), py_positional("block", boolean(true)), py_positional("timeout", py::none()) },
                        [shut_down](arguments a) -> py
                        {
                            py self = a[0];
                            py item = a[1];
                            py block = a[2];
                            py timeout = a[3];
                            with_(self.attr("not_full"), [&](py const&)
                            {
                                if (self.attr("is_shutdown").truthy())
                                    shut_down();
                                if (self.attr("maxsize") > I<0>())
                                {
                                    if (!block.truthy())
                                    {
                                        if (self.method("_qsize") >= self.attr("maxsize"))
                                            py_raise(M("queue", "Full")());
                                    }
                                    else if (timeout.is_none())
                                    {
                                        while (self.method("_qsize") >= self.attr("maxsize"))
                                        {
                                            self.attr("not_full").method("wait");
                                            if (self.attr("is_shutdown").truthy())
                                                shut_down();
                                        }
                                    }
                                    else if (timeout < I<0>())
                                        py_raise("ValueError", std::string{ "'timeout' must be a non-negative number" });
                                    else
                                    {
                                        py const endtime = (Mod<"time">().attr("monotonic"))() + timeout;
                                        while (self.method("_qsize") >= self.attr("maxsize"))
                                        {
                                            py const remaining = endtime - (Mod<"time">().attr("monotonic"))();
                                            if (remaining <= py{ 0.0 })
                                                py_raise(M("queue", "Full")());
                                            self.attr("not_full").method("wait", { remaining });
                                            if (self.attr("is_shutdown").truthy())
                                                shut_down();
                                        }
                                    }
                                }
                                self.method("_put", { item });
                                self.set_attr("unfinished_tasks", self.attr("unfinished_tasks") + I<1>());
                                self.attr("not_empty").method("notify");
                            });
                            return py::none();
                        }) },
                    { "get", w.method("Queue", "get", { py_positional("block", boolean(true)), py_positional("timeout", py::none()) },
                        [shut_down](arguments a) -> py
                        {
                            py self = a[0];
                            py block = a[1];
                            py timeout = a[2];
                            py item;
                            with_(self.attr("not_empty"), [&](py const&)
                            {
                                if (self.attr("is_shutdown").truthy() && !self.method("_qsize").truthy())
                                    shut_down();
                                if (!block.truthy())
                                {
                                    if (!self.method("_qsize").truthy())
                                        py_raise(M("queue", "Empty")());
                                }
                                else if (timeout.is_none())
                                {
                                    while (!self.method("_qsize").truthy())
                                    {
                                        self.attr("not_empty").method("wait");
                                        if (self.attr("is_shutdown").truthy() && !self.method("_qsize").truthy())
                                            shut_down();
                                    }
                                }
                                else if (timeout < I<0>())
                                    py_raise("ValueError", std::string{ "'timeout' must be a non-negative number" });
                                else
                                {
                                    py const endtime = (Mod<"time">().attr("monotonic"))() + timeout;
                                    while (!self.method("_qsize").truthy())
                                    {
                                        py const remaining = endtime - (Mod<"time">().attr("monotonic"))();
                                        if (remaining <= py{ 0.0 })
                                            py_raise(M("queue", "Empty")());
                                        self.attr("not_empty").method("wait", { remaining });
                                        if (self.attr("is_shutdown").truthy() && !self.method("_qsize").truthy())
                                            shut_down();
                                    }
                                }
                                item = self.method("_get");
                                self.attr("not_full").method("notify");
                            });
                            return item;
                        }) },
                    { "put_nowait", w.method("Queue", "put_nowait", { py_positional("item") }, [](arguments a) -> py
                        {
                            return a[0].method("put", { a[1] }, { { "block", boolean(false) } });
                        }) },
                    { "get_nowait", w.method("Queue", "get_nowait", {}, [](arguments a) -> py
                        {
                            return a[0].method("get", {}, { { "block", boolean(false) } });
                        }) },
                    { "shutdown", w.method("Queue", "shutdown", { py_positional("immediate", boolean(false)) }, [](arguments a) -> py
                        {
                            py self = a[0];
                            py immediate = a[1];
                            with_(self.attr("mutex"), [&](py const&)
                            {
                                self.set_attr("is_shutdown", boolean(true));
                                if (immediate.truthy())
                                {
                                    while (self.method("_qsize").truthy())
                                    {
                                        self.method("_get");
                                        if (self.attr("unfinished_tasks") > I<0>())
                                            self.set_attr("unfinished_tasks", self.attr("unfinished_tasks") - I<1>());
                                    }
                                    // release all blocked threads in `join()`
                                    self.attr("all_tasks_done").method("notify_all");
                                }
                                // All getters need to re-check queue-empty to raise ShutDown
                                self.attr("not_empty").method("notify_all");
                                self.attr("not_full").method("notify_all");
                            });
                            return py::none();
                        }) },
                    // Override these methods to implement other queue organizations
                    // (e.g. stack or priority queue).
                    // These will only be called with appropriate locks held

                    // Initialize the queue representation
                    { "_init", w.method("Queue", "_init", { py_positional("maxsize") }, [](arguments a) -> py
                        {
                            a[0].set_attr("queue", (Mod<"collections">().attr("deque"))());
                            return py::none();
                        }) },
                    { "_qsize", w.method("Queue", "_qsize", {}, [](arguments a) -> py
                        {
                            return py{ a[0].attr("queue").size() };
                        }) },
                    // Put a new item in the queue
                    { "_put", w.method("Queue", "_put", { py_positional("item") }, [](arguments a) -> py
                        {
                            a[0].attr("queue").method("append", { a[1] });
                            return py::none();
                        }) },
                    // Get an item from the queue
                    { "_get", w.method("Queue", "_get", {}, [](arguments a) -> py
                        {
                            return a[0].attr("queue").method("popleft");
                        }) },
                    { "__class_getitem__", B<"classmethod">()(M("types", "GenericAlias")) } });
                py priorityQueue = w.make_class("PriorityQueue", { queue }, {
                    { "__doc__", S("Variant of Queue that retrieves open entries in priority order (lowest first).\n\n    Entries are typically tuples of the form:  (priority number, data).\n    ") },
                    { "_init", w.method("PriorityQueue", "_init", { py_positional("maxsize") }, [](arguments a) -> py
                        {
                            a[0].set_attr("queue", py_list({}));
                            return py::none();
                        }) },
                    { "_qsize", w.method("PriorityQueue", "_qsize", {}, [](arguments a) -> py
                        {
                            return py{ a[0].attr("queue").size() };
                        }) },
                    { "_put", w.method("PriorityQueue", "_put", { py_positional("item") }, [](arguments a) -> py
                        {
                            (Mod<"heapq">().attr("heappush"))(a[0].attr("queue"), a[1]);
                            return py::none();
                        }) },
                    { "_get", w.method("PriorityQueue", "_get", {}, [](arguments a) -> py
                        {
                            return (Mod<"heapq">().attr("heappop"))(a[0].attr("queue"));
                        }) } });
                py lifoQueue = w.make_class("LifoQueue", { queue }, {
                    { "__doc__", S("Variant of Queue that retrieves most recently added entries first.") },
                    { "_init", w.method("LifoQueue", "_init", { py_positional("maxsize") }, [](arguments a) -> py
                        {
                            a[0].set_attr("queue", py_list({}));
                            return py::none();
                        }) },
                    { "_qsize", w.method("LifoQueue", "_qsize", {}, [](arguments a) -> py
                        {
                            return py{ a[0].attr("queue").size() };
                        }) },
                    { "_put", w.method("LifoQueue", "_put", { py_positional("item") }, [](arguments a) -> py
                        {
                            a[0].attr("queue").method("append", { a[1] });
                            return py::none();
                        }) },
                    { "_get", w.method("LifoQueue", "_get", {}, [](arguments a) -> py
                        {
                            return a[0].attr("queue").method("pop");
                        }) } });
                return { queue, priorityQueue, lifoQueue };
            }

            // (_PySimpleQueue: Lib/queue.py's pure Python SimpleQueue (a deque and a threading.Semaphore), as it is (it is not reentrant))
            object* py_simple_queue_class()
            {
                using namespace pyclass;
                static module_writer const w{ "queue", nullptr };
                py cls = w.make_class("_PySimpleQueue", {}, {
                    { "__doc__", S("Simple, unbounded FIFO queue.\n\n    This pure Python implementation is not reentrant.\n    ") },
                    { "__init__", w.method("_PySimpleQueue", "__init__", {}, [](arguments a) -> py
                        {
                            a[0].set_attr("_queue", (Mod<"collections">().attr("deque"))());
                            a[0].set_attr("_count", (Mod<"threading">().attr("Semaphore"))(I<0>()));
                            return py::none();
                        }) },
                    { "put", w.method("_PySimpleQueue", "put", { py_positional("item"), py_positional("block", boolean(true)), py_positional("timeout", py::none()) }, 
                        [](arguments a) -> py
                        {
                            a[0].attr("_queue").method("append", { a[1] });
                            a[0].attr("_count").method("release");
                            return py::none();
                        }) },
                    { "get", w.method("_PySimpleQueue", "get", { py_positional("block", boolean(true)), py_positional("timeout", py::none()) }, [](arguments a) -> py
                        {
                            py timeout = a[2];
                            if (!a[1].truthy())
                                timeout = py::none();
                            else if (!timeout.is_none() && timeout < I<0>())
                                py_raise("ValueError", "'timeout' must be a non-negative number");
                            if (!a[0].attr("_count").method("acquire", { a[1], timeout }).truthy())
                                py_raise(M("queue", "Empty")());
                            return a[0].attr("_queue").method("popleft");
                        }) },
                    { "put_nowait", w.method("_PySimpleQueue", "put_nowait", { py_positional("item") }, [](arguments a) -> py
                        {
                            return a[0].method("put", { a[1] }, { { "block", boolean(false) } });
                        }) },
                    { "get_nowait", w.method("_PySimpleQueue", "get_nowait", {}, [](arguments a) -> py
                        {
                            return a[0].method("get", {}, { { "block", boolean(false) } });
                        }) },
                    { "empty", w.method("_PySimpleQueue", "empty", {}, [](arguments a) -> py
                        {
                            return boolean(a[0].attr("_queue").size() == 0);
                        }) },
                    { "qsize", w.method("_PySimpleQueue", "qsize", {}, [](arguments a) -> py
                        {
                            return py{ a[0].attr("_queue").size() };
                        }) },
                    { "__class_getitem__", B<"classmethod">()(M("types", "GenericAlias")) } });
                return cls.release();
            }

            bool queue_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& e : { std::pair{ "Empty", "_queue.Empty" }, std::pair{ "Full", "queue.Full" }, std::pair{ "ShutDown", "queue.ShutDown" } })
                {
                    if (!is_exception_class(e.second))
                        user_exception_bases()[e.second] = { "Exception" };
                    aAttributes.emplace_back(e.first, exception_class(e.second));
                }
                auto classes = py_queue_classes();
                aAttributes.emplace_back("Queue", classes[0].release());
                aAttributes.emplace_back("PriorityQueue", classes[1].release());
                aAttributes.emplace_back("LifoQueue", classes[2].release());
                aAttributes.emplace_back("_PySimpleQueue", py_simple_queue_class());
                aAttributes.emplace_back("SimpleQueue", context()->type_new(context(), "SimpleQueue", "_queue.SimpleQueue", new_simple_queue, nullptr, instance_of));
                return true;
            }

            inline bool const sQueue = register_module("queue", queue_module);

            // (_queue (CPython's Modules/_queuemodule.c: the C SimpleQueue and Empty that Lib/queue.py imports): queue's SimpleQueue and Empty,
            // the same objects)
            bool queue_c_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto name : { "SimpleQueue", "Empty" })
                {
                    auto const value = module_attribute("queue", name);
                    if (value == nullptr)
                        return false;
                    aAttributes.emplace_back(name, value);
                }
                aAttributes.emplace_back("__doc__", make_str("C implementation of the Python queue module.\nThis module is an implementation detail, please do not use it directly."));
                return true;
            }

            inline bool const sQueueC = register_module("_queue", queue_c_module);
        }
    }
}
