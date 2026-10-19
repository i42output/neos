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

// The queue module (CPython 3.13's Lib/queue.py): Queue, LifoQueue, PriorityQueue and SimpleQueue (synchronized: their
// blocking operations release the GIL), and the exceptions Empty and Full

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <algorithm>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            struct queue_object
            {
                header h;
                enum kind_t { Fifo, Lifo, Priority, Simple } kind = Fifo;
                std::mutex mutex;
                std::condition_variable notEmpty;
                std::condition_variable notFull;
                std::condition_variable allTasksDone;
                std::deque<object*> items;      ///< (a PriorityQueue's: a heap)
                long long maxsize = 0;
                long long unfinished = 0;
                ~queue_object() { for (auto i : items) release(i); }
            };

            neos_py_type make_queue_type(char const* aQualifiedName, char const* aName);
            neos_py_type sQueueType = make_queue_type("queue.Queue", "Queue");
            neos_py_type sLifoQueueType = make_queue_type("queue.LifoQueue", "LifoQueue");
            neos_py_type sPriorityQueueType = make_queue_type("queue.PriorityQueue", "PriorityQueue");
            neos_py_type sSimpleQueueType = make_queue_type("_queue.SimpleQueue", "SimpleQueue");

            queue_object& as_queue(object* aObject) { return *reinterpret_cast<queue_object*>(aObject); }

            // (a < b: -1 if the comparison raised)
            int less(object* aLhs, object* aRhs)
            {
                ref const r{ compare(NEOS_PY_LT, aLhs, aRhs) };
                if (!r)
                    return -1;
                return truth(r.get());
            }

            // (heapq's push and pop on the queue's items (with its mutex locked; comparisons may raise))
            bool heap_push(std::deque<object*>& aHeap, object* aItem)
            {
                aHeap.push_back(aItem);
                auto pos = aHeap.size() - 1u;
                while (pos > 0u)
                {
                    auto const parent = (pos - 1u) / 2u;
                    auto const l = less(aHeap[pos], aHeap[parent]);
                    if (l < 0)
                        return false;
                    if (l == 0)
                        break;
                    std::swap(aHeap[pos], aHeap[parent]);
                    pos = parent;
                }
                return true;
            }

            object* heap_pop(std::deque<object*>& aHeap)
            {
                auto const last = aHeap.back();
                aHeap.pop_back();
                if (aHeap.empty())
                    return last;
                auto const result = aHeap.front();
                aHeap.front() = last;
                // (heapq's _siftup: the smaller child up to a leaf, then _siftdown)
                std::size_t pos = 0u;
                auto const end = aHeap.size();
                auto const item = aHeap[pos];
                auto child = 2u * pos + 1u;
                while (child < end)
                {
                    auto const right = child + 1u;
                    if (right < end && less(aHeap[child], aHeap[right]) != 1)
                        child = right;
                    aHeap[pos] = aHeap[child];
                    pos = child;
                    child = 2u * pos + 1u;
                }
                aHeap[pos] = item;
                while (pos > 0u)
                {
                    auto const parent = (pos - 1u) / 2u;
                    if (less(item, aHeap[parent]) != 1)
                        break;
                    aHeap[pos] = aHeap[parent];
                    pos = parent;
                }
                aHeap[pos] = item;
                return result;
            }

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
            object* full_error() { return error("queue.Full", ""); }

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

            object* put(object* aSelf, object* aItem, bool aBlock, double aTimeout)
            {
                auto& q = as_queue(aSelf);
                std::unique_lock<std::mutex> lock{ q.mutex };
                if (q.maxsize > 0)
                {
                    auto const room = [&]() { return static_cast<long long>(q.items.size()) < q.maxsize; };
                    if (!aBlock)
                    {
                        if (!room())
                            return (lock.unlock(), full_error());
                    }
                    else if (!wait(lock, q.notFull, aTimeout, room))
                        return (lock.unlock(), full_error());
                }
                retain(aItem);
                if (q.kind == queue_object::Priority)
                {
                    if (!heap_push(q.items, aItem))
                    {
                        q.items.erase(std::find(q.items.begin(), q.items.end(), aItem));
                        lock.unlock();
                        release(aItem);
                        return nullptr;
                    }
                }
                else
                    q.items.push_back(aItem);
                ++q.unfinished;
                lock.unlock();
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
                else if (!wait(lock, q.notEmpty, aTimeout, available))
                    return (lock.unlock(), empty_error());
                object* item = nullptr;
                switch (q.kind)
                {
                case queue_object::Lifo:
                    item = q.items.back();
                    q.items.pop_back();
                    break;
                case queue_object::Priority:
                    item = heap_pop(q.items);
                    break;
                default:
                    item = q.items.front();
                    q.items.pop_front();
                    break;
                }
                lock.unlock();
                q.notFull.notify_one();
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
                bool simple;    ///< (a SimpleQueue's too)
                object* (*function)(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs);
            };

            queue_method const sMethods[] =
            {
                { "put", true, [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("put", aArgs, aKwargs, { "item", "block", "timeout" }, 1u, 0u, a))
                            return nullptr;
                        double timeout = -1.0;
                        if (!timeout_of(a[2], timeout))
                            return nullptr;
                        return put(aSelf, a[0], a[1] == nullptr || truth(a[1]) == 1, timeout);
                    } },
                { "put_nowait", true, [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("put_nowait", aArgs, aKwargs, { "item" }, 1u, 0u, a))
                            return nullptr;
                        return put(aSelf, a[0], false, -1.0);
                    } },
                { "get", true, [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("get", aArgs, aKwargs, { "block", "timeout" }, 0u, 0u, a))
                            return nullptr;
                        double timeout = -1.0;
                        if (!timeout_of(a[1], timeout))
                            return nullptr;
                        return get(aSelf, a[0] == nullptr || truth(a[0]) == 1, timeout);
                    } },
                { "get_nowait", true, [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("get_nowait", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        return get(aSelf, false, -1.0);
                    } },
                { "qsize", true, [](object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        std::lock_guard<std::mutex> lock{ as_queue(aSelf).mutex };
                        return make_int(static_cast<long long>(as_queue(aSelf).items.size()));
                    } },
                { "empty", true, [](object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        std::lock_guard<std::mutex> lock{ as_queue(aSelf).mutex };
                        return make_bool(as_queue(aSelf).items.empty());
                    } },
                { "full", false, [](object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        auto& q = as_queue(aSelf);
                        std::lock_guard<std::mutex> lock{ q.mutex };
                        return make_bool(q.maxsize > 0 && static_cast<long long>(q.items.size()) >= q.maxsize);
                    } },
                { "task_done", false, [](object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        auto& q = as_queue(aSelf);
                        {
                            std::lock_guard<std::mutex> lock{ q.mutex };
                            if (q.unfinished <= 0)
                                return error("ValueError", "task_done() called too many times");
                            if (--q.unfinished != 0)
                                return none();
                        }
                        q.allTasksDone.notify_all();
                        return none();
                    } },
                { "join", false, [](object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        auto& q = as_queue(aSelf);
                        std::unique_lock<std::mutex> lock{ q.mutex };
                        wait(lock, q.allTasksDone, -1.0, [&]() { return q.unfinished == 0; });
                        return none();
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
                    auto& q = as_queue(aSelf);
                    bool const simple = q.kind == queue_object::Simple;
                    for (auto const& m : sMethods)
                        if (std::strcmp(m.name, aName) == 0 && (m.simple || !simple))
                            return make_function(m.name, call_method, const_cast<queue_method*>(&m), aSelf);
                    if (!simple && std::strcmp(aName, "maxsize") == 0)
                        return make_int(q.maxsize);
                    if (!simple && std::strcmp(aName, "unfinished_tasks") == 0)
                    {
                        std::lock_guard<std::mutex> lock{ q.mutex };
                        return make_int(q.unfinished);
                    }
                    if (!simple && std::strcmp(aName, "queue") == 0)
                    {
                        std::lock_guard<std::mutex> lock{ q.mutex };
                        std::vector<object*> items{ q.items.begin(), q.items.end() };
                        for (auto i : items)
                            retain(i);
                        return make_list(std::move(items));
                    }
                    return error("AttributeError", std::string{ "'" } + python_type(aSelf)->name + "' object has no attribute '" + aName + "'");
                };
                return type;
            }

            object* construct(neos_py_type& aType, queue_object::kind_t aKind, char const* aName, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (aKind == queue_object::Simple ? !parse_arguments(aName, aArgs, aKwargs, {}, 0u, 0u, a) :
                    !parse_arguments(aName, aArgs, aKwargs, { "maxsize" }, 0u, 0u, a))
                    return nullptr;
                long long maxsize = 0;
                if (aKind != queue_object::Simple && a[0] != nullptr && !to_index(a[0], maxsize))
                    return nullptr;
                auto const q = new queue_object{ { { &aType.base } } };
                q->kind = aKind;
                q->maxsize = maxsize;
                return &q->h.base;
            }

            object* new_queue(void* aData, std::vector<object*> const& aArgs, object* aKwargs)
            {
                auto const kind = static_cast<queue_object::kind_t>(reinterpret_cast<std::uintptr_t>(aData));
                switch (kind)
                {
                case queue_object::Lifo: return construct(sLifoQueueType, kind, "LifoQueue", aArgs, aKwargs);
                case queue_object::Priority: return construct(sPriorityQueueType, kind, "PriorityQueue", aArgs, aKwargs);
                case queue_object::Simple: return construct(sSimpleQueueType, kind, "SimpleQueue", aArgs, aKwargs);
                default: return construct(sQueueType, kind, "Queue", aArgs, aKwargs);
                }
            }

            int instance_of(neos_py_context*, void* aData, object* aObject)
            {
                auto const kind = static_cast<queue_object::kind_t>(reinterpret_cast<std::uintptr_t>(aData));
                auto const type = aObject->type;
                if (type != &sQueueType.base && type != &sLifoQueueType.base && type != &sPriorityQueueType.base && type != &sSimpleQueueType.base)
                    return 0;
                // (LifoQueue and PriorityQueue are Queues)
                return as_queue(aObject).kind == kind || (kind == queue_object::Fifo && as_queue(aObject).kind != queue_object::Simple) ? 1 : 0;
            }

            bool queue_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                struct entry { char const* name; char const* qualified; queue_object::kind_t kind; };
                static entry const sClasses[] = { { "Queue", "queue.Queue", queue_object::Fifo }, { "LifoQueue", "queue.LifoQueue", queue_object::Lifo },
                    { "PriorityQueue", "queue.PriorityQueue", queue_object::Priority }, { "SimpleQueue", "_queue.SimpleQueue", queue_object::Simple } };
                for (auto const& c : sClasses)
                    aAttributes.emplace_back(c.name, context()->type_new(context(), c.name, c.qualified,
                        [](neos_py_context*, void* aData, object* aArgs, object* aKwargs) -> object* { return new_queue(aData, as<tuple_object>(aArgs)->items, aKwargs); },
                        reinterpret_cast<void*>(static_cast<std::uintptr_t>(c.kind)), instance_of));
                for (auto const& e : { std::pair{ "Empty", "_queue.Empty" }, std::pair{ "Full", "queue.Full" } })
                {
                    if (!is_exception_class(e.second))
                        user_exception_bases()[e.second] = { "Exception" };
                    aAttributes.emplace_back(e.first, exception_class(e.second));
                }
                return true;
            }

            inline bool const sQueue = register_module("queue", queue_module);
        }
    }
}
