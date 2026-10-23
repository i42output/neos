/*
  threads.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/threading.py, Lib/_threading_local.py and
  Modules/_threadmodule.c), Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used under
  the Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from Python and C
  to C++ for the neos Python runtime plugin and adapted to its object model (see runtime/python/NOTICE).
*/

// Threads: the global interpreter lock (GIL: once a thread has been started, one thread at a time runs the program; it is
// released by blocking operations and, at times, for a thread that waits for it), and the modules _thread and threading
// (CPython 3.13's: OS threads that run the program's functions (neos_rt_host::thread_attach))

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "objects.hpp"
#include "values.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        // --- the GIL

        namespace
        {
            std::mutex sGilMutex;
            std::condition_variable sGilCondition;
            std::atomic<bool> sGilActive{ false };
            bool sGilLocked = false;
            std::atomic<bool> sDropRequest{ false };
            std::uint64_t sSwitches = 0u;
            thread_local bool tHolding = false;
            constexpr auto kSwitchInterval = std::chrono::milliseconds{ 5 };
        }

        void gil_acquire()
        {
            if (!sGilActive.load(std::memory_order_acquire) || tHolding)
                return;
            std::unique_lock<std::mutex> lock{ sGilMutex };
            while (sGilLocked)
                if (sGilCondition.wait_for(lock, kSwitchInterval) == std::cv_status::timeout && sGilLocked)
                    sDropRequest.store(true, std::memory_order_relaxed);
            sGilLocked = true;
            tHolding = true;
            ++sSwitches;
            sDropRequest.store(false, std::memory_order_relaxed);
        }

        void gil_release()
        {
            if (!sGilActive.load(std::memory_order_acquire) || !tHolding)
                return;
            {
                std::lock_guard<std::mutex> lock{ sGilMutex };
                sGilLocked = false;
                tHolding = false;
            }
            sGilCondition.notify_all();
        }

        // (another thread waits for it (gil_check's: out of line, so that its check is cheap))
#if defined(_MSC_VER)
        __declspec(noinline)
#else
        __attribute__((noinline))
#endif
        void gil_yield()
        {
            if (!tHolding)
                return;
            // (another thread has waited for it: it runs (this one waits until it has taken it))
            {
                std::unique_lock<std::mutex> lock{ sGilMutex };
                auto const switches = sSwitches;
                sGilLocked = false;
                tHolding = false;
                sGilCondition.notify_all();
                sGilCondition.wait_for(lock, kSwitchInterval * 4, [&]() { return sSwitches != switches; });
            }
            gil_acquire();
        }

        bool threads_started()
        {
            return sGilActive.load(std::memory_order_relaxed);
        }

        void gil_check()
        {
            if (sDropRequest.load(std::memory_order_relaxed))
                gil_yield();
        }

        namespace
        {
            // (the first thread started: the GIL is held by the thread that starts it)
            void gil_activate()
            {
                if (sGilActive.load(std::memory_order_acquire))
                    return;
                std::lock_guard<std::mutex> lock{ sGilMutex };
                sGilLocked = true;
                tHolding = true;
                sGilActive.store(true, std::memory_order_release);
            }

            // (wait for a condition (of aMutex's) with the GIL released; aTimeout: seconds (negative: none); false if it timed out)
            template <typename Predicate>
            bool wait_for(std::mutex& aMutex, std::condition_variable& aCondition, double aTimeout, Predicate aPredicate)
            {
                gil_release();
                bool result = true;
                {
                    std::unique_lock<std::mutex> lock{ aMutex };
                    if (aTimeout < 0.0)
                        aCondition.wait(lock, aPredicate);
                    else
                        result = aCondition.wait_for(lock, std::chrono::duration<double>{ aTimeout }, aPredicate);
                }
                gil_acquire();
                return result;
            }

            // (a timeout argument: None or a number of seconds; aResult: -1 for none)
            bool timeout_of(object* aValue, double& aResult, bool aNegativeIsNone = false)
            {
                aResult = -1.0;
                if (aValue == nullptr || kind_of(aValue) == NEOS_PY_NONE)
                    return true;
                if (!to_double(aValue, aResult))
                    return false;
                if (aResult < 0.0)
                {
                    if (aNegativeIsNone && aResult == -1.0)
                        return true;
                    error("ValueError", aNegativeIsNone ? "timeout value must be a non-negative number" : "'timeout' must be a non-negative number");
                    return false;
                }
                return true;
            }

            bool truth_of(object* aValue, bool aDefault)
            {
                if (aValue == nullptr)
                    return aDefault;
                return truth(aValue) == 1;
            }

            long long thread_ident()
            {
                return static_cast<long long>(std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0x7FFFFFFFFFFFull);
            }

            std::string address(object* aObject) { return object_address(aObject); }

            // (a method's arguments: self and the others)
            bool method_arguments(char const* aFunction, std::vector<object*> const& aArgs, object* aKwargs, std::vector<char const*> const& aNames,
                std::size_t aRequired, std::size_t aPositionalOnly, std::vector<object*>& aResult)
            {
                if (aArgs.empty())
                {
                    error("TypeError", std::string{ aFunction } + "() needs an argument");
                    return false;
                }
                return parse_arguments(aFunction, std::vector<object*>{ aArgs.begin() + 1, aArgs.end() }, aKwargs, aNames, aRequired, aPositionalOnly, aResult);
            }

            // --- locks

            struct lock_object
            {
                header h;
                std::mutex mutex;
                std::condition_variable condition;
                bool locked = false;
                bool reentrant = false;
                long long owner = 0;
                long long count = 0;
            };

            neos_py_type make_lock_type(bool aReentrant);
            neos_py_type sLockType = make_lock_type(false);
            neos_py_type sRLockType = make_lock_type(true);

            bool is_lock(object* aObject) { return aObject != nullptr && (aObject->type == &sLockType.base || aObject->type == &sRLockType.base); }
            lock_object& as_lock(object* aObject) { return *reinterpret_cast<lock_object*>(aObject); }

            object* make_lock(bool aReentrant)
            {
                auto const l = new lock_object{ { { aReentrant ? &sRLockType.base : &sLockType.base } } };
                l->reentrant = aReentrant;
                return &l->h.base;
            }

            // (acquire: true if acquired)
            bool lock_acquire(lock_object& l, bool aBlocking, double aTimeout)
            {
                auto const me = thread_ident();
                {
                    std::lock_guard<std::mutex> lock{ l.mutex };
                    if (l.reentrant && l.locked && l.owner == me)
                    {
                        ++l.count;
                        return true;
                    }
                    if (!l.locked)
                    {
                        l.locked = true;
                        l.owner = me;
                        l.count = 1;
                        return true;
                    }
                }
                if (!aBlocking)
                    return false;
                bool const acquired = wait_for(l.mutex, l.condition, aTimeout, [&]() { return !l.locked; });
                if (!acquired)
                    return false;
                std::lock_guard<std::mutex> lock{ l.mutex };
                if (l.locked)
                    return lock_acquire(l, aBlocking, aTimeout);
                l.locked = true;
                l.owner = me;
                l.count = 1;
                return true;
            }

            bool lock_release(lock_object& l)
            {
                {
                    std::lock_guard<std::mutex> lock{ l.mutex };
                    if (!l.locked || (l.reentrant && l.owner != thread_ident()))
                    {
                        error("RuntimeError", l.reentrant ? "cannot release un-acquired lock" : "release unlocked lock");
                        return false;
                    }
                    if (l.reentrant && --l.count > 0)
                        return true;
                    l.locked = false;
                    l.owner = 0;
                    l.count = 0;
                }
                l.condition.notify_one();
                return true;
            }

            struct native_method
            {
                char const* name;
                object* (*function)(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs);
            };

            object* call_native_method(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& method = *static_cast<native_method const*>(aData);
                auto const& items = as<tuple_object>(aArgs)->items;
                std::vector<object*> const args{ items.begin() + 1, items.end() };
                return method.function(items[0], args, aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty() ? aKwargs : nullptr);
            }

            template <std::size_t N>
            object* native_getattr(object* aSelf, char const* aName, native_method const (&aMethods)[N], char const* aType)
            {
                for (auto const& m : aMethods)
                    if (std::strcmp(m.name, aName) == 0)
                        return make_function(m.name, call_native_method, const_cast<native_method*>(&m), aSelf);
                return error("AttributeError", std::string{ "'" } + aType + "' object has no attribute '" + aName + "'");
            }

            object* m_lock_acquire(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("acquire", aArgs, aKwargs, { "blocking", "timeout" }, 0u, 0u, a))
                    return nullptr;
                bool const blocking = truth_of(a[0], true);
                double timeout = -1.0;
                if (!timeout_of(a[1], timeout, true))
                    return nullptr;
                if (!blocking && timeout >= 0.0)
                    return error("ValueError", "can't specify a timeout for a non-blocking call");
                return make_bool(lock_acquire(as_lock(aSelf), blocking, timeout));
            }

            native_method const sLockMethods[] =
            {
                { "acquire", m_lock_acquire },
                { "acquire_lock", m_lock_acquire },
                { "release", [](object* aSelf, std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (!aArgs.empty())
                            return error("TypeError", "release() takes no arguments (" + std::to_string(aArgs.size()) + " given)");
                        return lock_release(as_lock(aSelf)) ? none() : nullptr;
                    } },
                { "release_lock", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return lock_release(as_lock(aSelf)) ? none() : nullptr; } },
                { "locked", [](object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        std::lock_guard<std::mutex> lock{ as_lock(aSelf).mutex };
                        return make_bool(as_lock(aSelf).locked);
                    } },
                { "__enter__", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return make_bool(lock_acquire(as_lock(aSelf), true, -1.0)); } },
                { "__exit__", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return lock_release(as_lock(aSelf)) ? none() : nullptr; } },
                { "_is_owned", [](object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        auto& l = as_lock(aSelf);
                        std::lock_guard<std::mutex> lock{ l.mutex };
                        return make_bool(l.locked && (!l.reentrant || l.owner == thread_ident()));
                    } }
            };

            neos_py_type make_lock_type(bool aReentrant)
            {
                auto type = aReentrant ? make_type("_thread.RLock", "RLock", retain_object, release_object<lock_object>) :
                    make_type("_thread.lock", "lock", retain_object, release_object<lock_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto& l = as_lock(aSelf);
                    std::lock_guard<std::mutex> lock{ l.mutex };
                    if (l.reentrant)
                        return make_str(std::string{ "<" } + (l.locked ? "locked" : "unlocked") + " _thread.RLock object owner=" + std::to_string(l.owner) +
                            " count=" + std::to_string(l.count) + " at " + address(aSelf) + ">");
                    return make_str(std::string{ "<" } + (l.locked ? "locked" : "unlocked") + " _thread.lock object at " + address(aSelf) + ">");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    return native_getattr(aSelf, aName, sLockMethods, as_lock(aSelf).reentrant ? "_thread.RLock" : "_thread.lock");
                };
                return type;
            }

            // --- conditions, events, semaphores and barriers

            struct condition_object
            {
                header h;
                object* lock = nullptr;
                std::mutex mutex;
                std::condition_variable condition;
                std::list<bool*> waiters;   ///< (each waiter's notified flag)
                ~condition_object() { release(lock); }
            };

            neos_py_type make_condition_type();
            neos_py_type sConditionType = make_condition_type();
            condition_object& as_condition(object* aObject) { return *reinterpret_cast<condition_object*>(aObject); }

            // (wait: releases the condition's lock (all of an RLock's levels) and acquires it again)
            bool condition_wait(condition_object& c, double aTimeout)
            {
                auto& l = as_lock(c.lock);
                {
                    std::lock_guard<std::mutex> lock{ l.mutex };
                    if (!l.locked || (l.reentrant && l.owner != thread_ident()))
                    {
                        error("RuntimeError", "cannot wait on un-acquired lock");
                        return false;
                    }
                }
                bool notified = false;
                {
                    std::lock_guard<std::mutex> lock{ c.mutex };
                    c.waiters.push_back(&notified);
                }
                long long count = 0;
                {
                    std::lock_guard<std::mutex> lock{ l.mutex };
                    count = l.count;
                    l.locked = false;
                    l.owner = 0;
                    l.count = 0;
                }
                l.condition.notify_one();
                bool const result = wait_for(c.mutex, c.condition, aTimeout, [&]() { return notified; });
                {
                    std::lock_guard<std::mutex> lock{ c.mutex };
                    if (!notified)
                        c.waiters.remove(&notified);
                }
                lock_acquire(l, true, -1.0);
                {
                    std::lock_guard<std::mutex> lock{ l.mutex };
                    l.count = count;
                }
                return result;
            }

            void condition_notify(condition_object& c, std::size_t aCount)
            {
                {
                    std::lock_guard<std::mutex> lock{ c.mutex };
                    while (aCount-- > 0u && !c.waiters.empty())
                    {
                        *c.waiters.front() = true;
                        c.waiters.pop_front();
                    }
                }
                c.condition.notify_all();
            }

            bool owned(condition_object& c)
            {
                auto& l = as_lock(c.lock);
                std::lock_guard<std::mutex> lock{ l.mutex };
                return l.locked && (!l.reentrant || l.owner == thread_ident());
            }

            native_method const sConditionMethods[] =
            {
                { "acquire", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object* { return m_lock_acquire(as_condition(aSelf).lock, aArgs, aKwargs); } },
                { "release", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return lock_release(as_lock(as_condition(aSelf).lock)) ? none() : nullptr; } },
                { "__enter__", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return make_bool(lock_acquire(as_lock(as_condition(aSelf).lock), true, -1.0)); } },
                { "__exit__", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return lock_release(as_lock(as_condition(aSelf).lock)) ? none() : nullptr; } },
                { "wait", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("wait", aArgs, aKwargs, { "timeout" }, 0u, 0u, a))
                            return nullptr;
                        double timeout = -1.0;
                        if (!timeout_of(a[0], timeout))
                            return nullptr;
                        auto const r = condition_wait(as_condition(aSelf), timeout);
                        return failed() ? nullptr : make_bool(r);
                    } },
                { "wait_for", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("wait_for", aArgs, aKwargs, { "predicate", "timeout" }, 1u, 0u, a))
                            return nullptr;
                        double timeout = -1.0;
                        if (!timeout_of(a[1], timeout))
                            return nullptr;
                        auto const end = std::chrono::steady_clock::now() + std::chrono::duration<double>{ timeout < 0.0 ? 0.0 : timeout };
                        ref const empty{ make_tuple() };
                        for (;;)
                        {
                            ref result{ call(a[0], empty.get(), nullptr) };
                            if (!result)
                                return nullptr;
                            auto const t = truth(result.get());
                            if (t != 0)
                                return t < 0 ? nullptr : result.release_ownership();
                            double remaining = -1.0;
                            if (timeout >= 0.0)
                            {
                                remaining = std::chrono::duration<double>{ end - std::chrono::steady_clock::now() }.count();
                                if (remaining <= 0.0)
                                    return result.release_ownership();
                            }
                            condition_wait(as_condition(aSelf), remaining);
                            if (failed())
                                return nullptr;
                        }
                    } },
                { "notify", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("notify", aArgs, aKwargs, { "n" }, 0u, 0u, a))
                            return nullptr;
                        long long n = 1;
                        if (a[0] != nullptr && !to_index(a[0], n))
                            return nullptr;
                        if (!owned(as_condition(aSelf)))
                            return error("RuntimeError", "cannot notify on un-acquired lock");
                        condition_notify(as_condition(aSelf), static_cast<std::size_t>(std::max(0ll, n)));
                        return none();
                    } },
                { "notify_all", [](object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        if (!owned(as_condition(aSelf)))
                            return error("RuntimeError", "cannot notify on un-acquired lock");
                        condition_notify(as_condition(aSelf), static_cast<std::size_t>(-1));
                        return none();
                    } }
            };

            neos_py_type make_condition_type()
            {
                auto type = make_type("threading.Condition", "Condition", retain_object, release_object<condition_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto& c = as_condition(aSelf);
                    std::string lock;
                    if (!repr_of(c.lock, lock))
                        return nullptr;
                    std::lock_guard<std::mutex> guard{ c.mutex };
                    return make_str("<Condition(" + lock + ", " + std::to_string(c.waiters.size()) + ")>");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    return native_getattr(aSelf, aName, sConditionMethods, "Condition");
                };
                return type;
            }

            struct event_object
            {
                header h;
                std::mutex mutex;
                std::condition_variable condition;
                bool flag = false;
            };

            neos_py_type make_event_type();
            neos_py_type sEventType = make_event_type();
            event_object& as_event(object* aObject) { return *reinterpret_cast<event_object*>(aObject); }

            native_method const sEventMethods[] =
            {
                { "is_set", [](object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        std::lock_guard<std::mutex> lock{ as_event(aSelf).mutex };
                        return make_bool(as_event(aSelf).flag);
                    } },
                { "isSet", [](object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        std::lock_guard<std::mutex> lock{ as_event(aSelf).mutex };
                        return make_bool(as_event(aSelf).flag);
                    } },
                { "set", [](object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        {
                            std::lock_guard<std::mutex> lock{ as_event(aSelf).mutex };
                            as_event(aSelf).flag = true;
                        }
                        as_event(aSelf).condition.notify_all();
                        return none();
                    } },
                { "clear", [](object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        std::lock_guard<std::mutex> lock{ as_event(aSelf).mutex };
                        as_event(aSelf).flag = false;
                        return none();
                    } },
                { "wait", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("wait", aArgs, aKwargs, { "timeout" }, 0u, 0u, a))
                            return nullptr;
                        double timeout = -1.0;
                        if (!timeout_of(a[0], timeout))
                            return nullptr;
                        auto& e = as_event(aSelf);
                        return make_bool(wait_for(e.mutex, e.condition, timeout, [&]() { return e.flag; }));
                    } }
            };

            neos_py_type make_event_type()
            {
                auto type = make_type("threading.Event", "Event", retain_object, release_object<event_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    std::lock_guard<std::mutex> lock{ as_event(aSelf).mutex };
                    return make_str("<threading.Event at " + address(aSelf) + ": " + (as_event(aSelf).flag ? "set" : "unset") + ">");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    return native_getattr(aSelf, aName, sEventMethods, "Event");
                };
                return type;
            }

            struct semaphore_object
            {
                header h;
                std::mutex mutex;
                std::condition_variable condition;
                long long value = 1;
                long long initial = 1;
                bool bounded = false;
            };

            neos_py_type make_semaphore_type(bool aBounded);
            neos_py_type sSemaphoreType = make_semaphore_type(false);
            neos_py_type sBoundedSemaphoreType = make_semaphore_type(true);
            semaphore_object& as_semaphore(object* aObject) { return *reinterpret_cast<semaphore_object*>(aObject); }

            object* m_semaphore_acquire(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("acquire", aArgs, aKwargs, { "blocking", "timeout" }, 0u, 0u, a))
                    return nullptr;
                bool const blocking = truth_of(a[0], true);
                double timeout = -1.0;
                if (!timeout_of(a[1], timeout))
                    return nullptr;
                if (!blocking && timeout >= 0.0)
                    return error("ValueError", "can't specify timeout for non-blocking acquire");
                auto& s = as_semaphore(aSelf);
                {
                    std::lock_guard<std::mutex> lock{ s.mutex };
                    if (s.value > 0)
                    {
                        --s.value;
                        return make_bool(true);
                    }
                }
                if (!blocking)
                    return make_bool(false);
                gil_release();
                bool acquired = false;
                {
                    std::unique_lock<std::mutex> lock{ s.mutex };
                    auto const ready = [&]() { return s.value > 0; };
                    acquired = timeout < 0.0 ? (s.condition.wait(lock, ready), true) : s.condition.wait_for(lock, std::chrono::duration<double>{ timeout }, ready);
                    if (acquired)
                        --s.value;
                }
                gil_acquire();
                return make_bool(acquired);
            }

            object* m_semaphore_release(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("release", aArgs, aKwargs, { "n" }, 0u, 0u, a))
                    return nullptr;
                long long n = 1;
                if (a[0] != nullptr && !to_index(a[0], n))
                    return nullptr;
                if (n < 1)
                    return error("ValueError", "n must be one or more");
                auto& s = as_semaphore(aSelf);
                {
                    std::lock_guard<std::mutex> lock{ s.mutex };
                    if (s.bounded && s.value + n > s.initial)
                        return error("ValueError", "Semaphore released too many times");
                    s.value += n;
                }
                s.condition.notify_all();
                return none();
            }

            native_method const sSemaphoreMethods[] =
            {
                { "acquire", m_semaphore_acquire },
                { "release", m_semaphore_release },
                { "__enter__", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return m_semaphore_acquire(aSelf, {}, nullptr); } },
                { "__exit__", [](object* aSelf, std::vector<object*> const&, object*) -> object* { return m_semaphore_release(aSelf, {}, nullptr); } },
                { "_value", nullptr }
            };

            neos_py_type make_semaphore_type(bool aBounded)
            {
                auto type = aBounded ? make_type("threading.BoundedSemaphore", "BoundedSemaphore", retain_object, release_object<semaphore_object>) :
                    make_type("threading.Semaphore", "Semaphore", retain_object, release_object<semaphore_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto& s = as_semaphore(aSelf);
                    std::lock_guard<std::mutex> lock{ s.mutex };
                    if (s.bounded)
                        return make_str("<threading.BoundedSemaphore at " + address(aSelf) + ": value=" + std::to_string(s.value) + "/" + std::to_string(s.initial) + ">");
                    return make_str("<threading.Semaphore at " + address(aSelf) + ": value=" + std::to_string(s.value) + ">");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    if (std::strcmp(aName, "_value") == 0)
                    {
                        std::lock_guard<std::mutex> lock{ as_semaphore(aSelf).mutex };
                        return make_int(as_semaphore(aSelf).value);
                    }
                    for (auto const& m : sSemaphoreMethods)
                        if (m.function != nullptr && std::strcmp(m.name, aName) == 0)
                            return make_function(m.name, call_native_method, const_cast<native_method*>(&m), aSelf);
                    return error("AttributeError", std::string{ "'" } + python_type(aSelf)->name + "' object has no attribute '" + aName + "'");
                };
                return type;
            }

            struct barrier_object
            {
                header h;
                std::mutex mutex;
                std::condition_variable condition;
                long long parties = 0;
                long long count = 0;
                long long generation = 0;
                bool broken = false;
                object* action = nullptr;
                double timeout = -1.0;
                ~barrier_object() { release(action); }
            };

            neos_py_type make_barrier_type();
            neos_py_type sBarrierType = make_barrier_type();
            barrier_object& as_barrier(object* aObject) { return *reinterpret_cast<barrier_object*>(aObject); }

            object* broken_barrier()
            {
                return error("threading.BrokenBarrierError", "");
            }

            void barrier_break(barrier_object& b)
            {
                {
                    std::lock_guard<std::mutex> lock{ b.mutex };
                    b.broken = true;
                }
                b.condition.notify_all();
            }

            native_method const sBarrierMethods[] =
            {
                { "wait", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("wait", aArgs, aKwargs, { "timeout" }, 0u, 0u, a))
                            return nullptr;
                        auto& b = as_barrier(aSelf);
                        double timeout = b.timeout;
                        if (a[0] != nullptr && !timeout_of(a[0], timeout))
                            return nullptr;
                        long long index = 0;
                        long long generation = 0;
                        {
                            std::lock_guard<std::mutex> lock{ b.mutex };
                            if (b.broken)
                                return broken_barrier();
                            index = b.count++;
                            generation = b.generation;
                        }
                        if (index == b.parties - 1)
                        {
                            // (the last: the action, and the others are released)
                            if (b.action != nullptr && kind_of(b.action) != NEOS_PY_NONE)
                            {
                                ref const empty{ make_tuple() };
                                ref const r{ call(b.action, empty.get(), nullptr) };
                                if (!r)
                                {
                                    barrier_break(b);
                                    return nullptr;
                                }
                            }
                            {
                                std::lock_guard<std::mutex> lock{ b.mutex };
                                b.count = 0;
                                ++b.generation;
                            }
                            b.condition.notify_all();
                            return make_int(index);
                        }
                        bool const passed = wait_for(b.mutex, b.condition, timeout, [&]() { return b.generation != generation || b.broken; });
                        if (!passed)
                        {
                            barrier_break(b);
                            return broken_barrier();
                        }
                        {
                            std::lock_guard<std::mutex> lock{ b.mutex };
                            if (b.generation == generation && b.broken)
                                return broken_barrier();
                        }
                        return make_int(index);
                    } },
                { "reset", [](object* aSelf, std::vector<object*> const&, object*) -> object*
                    {
                        auto& b = as_barrier(aSelf);
                        {
                            std::lock_guard<std::mutex> lock{ b.mutex };
                            b.broken = false;
                            b.count = 0;
                            ++b.generation;
                        }
                        b.condition.notify_all();
                        return none();
                    } },
                { "abort", [](object* aSelf, std::vector<object*> const&, object*) -> object* { barrier_break(as_barrier(aSelf)); return none(); } }
            };

            neos_py_type make_barrier_type()
            {
                auto type = make_type("threading.Barrier", "Barrier", retain_object, release_object<barrier_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto& b = as_barrier(aSelf);
                    std::lock_guard<std::mutex> lock{ b.mutex };
                    if (b.broken)
                        return make_str("<threading.Barrier at " + address(aSelf) + ": broken>");
                    return make_str("<threading.Barrier at " + address(aSelf) + ": waiters=" + std::to_string(b.count) + "/" + std::to_string(b.parties) + ">");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto& b = as_barrier(aSelf);
                    if (std::strcmp(aName, "parties") == 0)
                        return make_int(b.parties);
                    if (std::strcmp(aName, "n_waiting") == 0)
                    {
                        std::lock_guard<std::mutex> lock{ b.mutex };
                        return make_int(b.count);
                    }
                    if (std::strcmp(aName, "broken") == 0)
                    {
                        std::lock_guard<std::mutex> lock{ b.mutex };
                        return make_bool(b.broken);
                    }
                    return native_getattr(aSelf, aName, sBarrierMethods, "Barrier");
                };
                return type;
            }

            // --- thread-local data

            struct local_object
            {
                header h;
                std::mutex mutex;
                std::map<long long, std::vector<std::pair<std::string, object*>>> values;
                ~local_object() { for (auto& t : values) for (auto& v : t.second) release(v.second); }
            };

            neos_py_type make_local_type();
            neos_py_type sLocalType = make_local_type();

            neos_py_type make_local_type()
            {
                auto type = make_type("_thread._local", "_local", retain_object, release_object<local_object>);
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto& l = *reinterpret_cast<local_object*>(aSelf);
                    std::lock_guard<std::mutex> lock{ l.mutex };
                    for (auto const& v : l.values[thread_ident()])
                        if (v.first == aName)
                        {
                            retain(v.second);
                            return v.second;
                        }
                    return error("AttributeError", std::string{ "'_thread._local' object has no attribute '" } + aName + "'");
                };
                type.setattr = [](neos_py_context*, object* aSelf, char const* aName, object* aValue) -> int
                {
                    auto& l = *reinterpret_cast<local_object*>(aSelf);
                    std::unique_lock<std::mutex> lock{ l.mutex };
                    auto& values = l.values[thread_ident()];
                    for (auto v = values.begin(); v != values.end(); ++v)
                        if (v->first == aName)
                        {
                            auto const old = v->second;
                            if (aValue == nullptr)
                                values.erase(v);
                            else
                            {
                                retain(aValue);
                                v->second = aValue;
                            }
                            lock.unlock();
                            release(old);
                            return 0;
                        }
                    if (aValue == nullptr)
                    {
                        lock.unlock();
                        error("AttributeError", std::string{ "'_thread._local' object has no attribute '" } + aName + "'");
                        return -1;
                    }
                    retain(aValue);
                    values.emplace_back(aName, aValue);
                    return 0;
                };
                return type;
            }

            // --- threading.local (_thread._local) (as CPython's Lib/_threading_local.py: a class (so that programs derive from it) whose 
            // instances' attributes are each thread's own: an instance's __dict__ is the thread's, made when the thread first uses it (and then
            // its class's __init__ is called with its arguments))

            struct local_impl
            {
                std::map<long long, py> dicts;  ///< (each thread's attributes (by its ident))
                py args;
                py kwargs;
            };
            std::map<object*, local_impl>& local_impls()
            {
                static std::map<object*, local_impl> sImpls;
                return sImpls;
            }
            object* sLocalClass = nullptr;

            // (the instance's __dict__: the thread's (made, and __init__ called, if the thread has none))
            void local_patch(py const& aSelf)
            {
                auto& impls = local_impls();
                auto const i = impls.find(aSelf.get());
                if (i == impls.end())
                    return;
                static py const sSetattr = builtin("object").attr("__setattr__");
                auto const ident = thread_ident();
                if (auto const d = i->second.dicts.find(ident); d != i->second.dicts.end())
                {
                    sSetattr(aSelf, py{ "__dict__" }, d->second);
                    return;
                }
                py const dict = py_dict();
                i->second.dicts[ident] = dict;
                sSetattr(aSelf, py{ "__dict__" }, dict);
                std::vector<object*> args{ aSelf.reference() };
                for (auto const& a : i->second.args.items())
                    args.push_back(a.reference());
                py const init = type_of(aSelf).attr("__init__");
                py{ invoke(init.get(), std::move(args), i->second.kwargs.get()) };
            }

            builtin_entry const sLocalMethods[] =
            {
                { "__new__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        return py_guarded([&]()
                        {
                            if (aArgs.empty())
                                py_raise("TypeError", "_local.__new__(): not enough arguments");
                            py const cls = py::borrowed(aArgs[0]);
                            bool const arguments = aArgs.size() > 1u || (aKwargs != nullptr && length(aKwargs) > 0);
                            if (arguments && cls.attr("__init__").is(builtin("object").attr("__init__")))
                                py_raise("TypeError", "Initialization arguments are not supported");
                            py const self = builtin("object").attr("__new__")(cls);
                            auto& impl = local_impls()[self.get()];
                            std::vector<py> args;
                            for (std::size_t i = 1u; i < aArgs.size(); ++i)
                                args.push_back(py::borrowed(aArgs[i]));
                            impl.args = py_tuple(args);
                            impl.kwargs = aKwargs != nullptr ? builtin("dict")(py::borrowed(aKwargs)) : py_dict();
                            // (the thread's dict made in anticipation of __init__ being called (so that it is not called again here))
                            py const dict = py_dict();
                            impl.dicts[thread_ident()] = dict;
                            builtin("object").attr("__setattr__")(self, py{ "__dict__" }, dict);
                            return self;
                        });
                    } },
                { "__getattribute__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        return py_guarded([&]()
                        {
                            if (aArgs.size() != 2u)
                                py_raise("TypeError", "expected 1 argument, got " + std::to_string(aArgs.size() - 1u));
                            py const self = py::borrowed(aArgs[0]);
                            local_patch(self);
                            return builtin("object").attr("__getattribute__")(self, py::borrowed(aArgs[1]));
                        });
                    } },
                { "__setattr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        return py_guarded([&]()
                        {
                            if (aArgs.size() != 3u)
                                py_raise("TypeError", "expected 2 arguments, got " + std::to_string(aArgs.size() - 1u));
                            py const self = py::borrowed(aArgs[0]);
                            if (kind_of(aArgs[1]) == NEOS_PY_STR && as<str_object>(aArgs[1])->value == "__dict__")
                                py_raise("AttributeError", "'" + (type_of(self).get() == sLocalClass ? std::string{ "_thread._local" } : type_of(self).attr("__name__").str()) + 
                                    "' object attribute '__dict__' is read-only");
                            local_patch(self);
                            return builtin("object").attr("__setattr__")(self, py::borrowed(aArgs[1]), py::borrowed(aArgs[2]));
                        });
                    } },
                { "__delattr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        return py_guarded([&]()
                        {
                            if (aArgs.size() != 2u)
                                py_raise("TypeError", "expected 1 argument, got " + std::to_string(aArgs.size() - 1u));
                            py const self = py::borrowed(aArgs[0]);
                            if (kind_of(aArgs[1]) == NEOS_PY_STR && as<str_object>(aArgs[1])->value == "__dict__")
                                py_raise("AttributeError", "'" + (type_of(self).get() == sLocalClass ? std::string{ "_thread._local" } : type_of(self).attr("__name__").str()) + 
                                    "' object attribute '__dict__' is read-only");
                            local_patch(self);
                            return builtin("object").attr("__delattr__")(self, py::borrowed(aArgs[1]));
                        });
                    } },
                { "__del__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (!aArgs.empty())
                            local_impls().erase(aArgs[0]);
                        return new_reference(none());
                    } }
            };

            object* local_class()
            {
                if (sLocalClass == nullptr)
                {
                    std::vector<std::pair<std::string, object*>> ns;
                    for (auto const& m : sLocalMethods)
                        ns.emplace_back(m.name, make_builtin(m));
                    sLocalClass = make_class("_thread", "_local", {}, ns);
                    for (auto& n : ns)
                        release(n.second);
                }
                return sLocalClass != nullptr ? new_reference(sLocalClass) : nullptr;
            }

            // --- threads

            struct thread_state
            {
                header h;
                std::mutex mutex;
                std::condition_variable condition;
                enum state_t { Initial, Started, Finished } state = Initial;
                long long ident = 0;
                bool daemon = false;
                object* thread = nullptr;   ///< (the Thread (borrowed: the running thread holds a reference))
            };

            neos_py_type sThreadStateType = make_type("threading._state", "_state", retain_object, release_object<thread_state>);

            std::mutex sThreadsMutex;
            std::vector<object*> sThreads;              ///< (the Threads that have been started and have not finished (references))
            std::atomic<long long> sThreadCounter{ 0 };
            thread_local object* tCurrentThread = nullptr;  ///< (the running Thread (null: the main thread))
            object* sMainThread = nullptr;
            object* sThreadClass = nullptr;
            object* sTimerClass = nullptr;
            void* sContext = nullptr;

            thread_state* state_of(object* aThread)
            {
                ref const s{ getattr(aThread, "_neos_state") };
                if (!s || s.get()->type != &sThreadStateType.base)
                {
                    if (failed())
                        clear_error();
                    error("RuntimeError", "thread.__init__() not called");
                    return nullptr;
                }
                return reinterpret_cast<thread_state*>(s.get());
            }

            std::string attribute_text(object* aObject, char const* aName)
            {
                ref const value{ getattr(aObject, aName) };
                std::string text;
                if (!value)
                {
                    clear_error();
                    return text;
                }
                str_of(value.get(), text);
                return text;
            }

            void report_exception(object* aThread)
            {
                // (an exception that a thread does not handle: printed (sys.excepthook's format, without the traceback))
                std::string const text = error_text();
                clear_error();
                auto const name = attribute_text(aThread, "name");
                write_standard(2, "Exception in thread " + name + ":\nTraceback (most recent call last):\n" + text + "\n");
            }

            void run_thread(object* aThread, thread_state* aState)
            {
                host()->thread_attach(sContext);
                gil_acquire();
                tCurrentThread = aThread;
                {
                    std::lock_guard<std::mutex> lock{ aState->mutex };
                    aState->ident = thread_ident();
                }
                ref const ident{ make_int(thread_ident()) };
                setattr(aThread, "_ident", ident.get());
                {
                    ref const run{ getattr(aThread, "run") };
                    if (run)
                    {
                        ref const empty{ make_tuple() };
                        ref const r{ call(run.get(), empty.get(), nullptr) };
                        if (!r && failed())
                        {
                            if (std::string_view{ error_text() }.starts_with("SystemExit"))
                                clear_error();
                            else
                                report_exception(aThread);
                        }
                    }
                    else if (failed())
                        report_exception(aThread);
                }
                {
                    std::lock_guard<std::mutex> lock{ aState->mutex };
                    aState->state = thread_state::Finished;
                }
                aState->condition.notify_all();
                {
                    std::lock_guard<std::mutex> lock{ sThreadsMutex };
                    sThreads.erase(std::remove(sThreads.begin(), sThreads.end(), aThread), sThreads.end());
                }
                tCurrentThread = nullptr;
                release(&aState->h.base);
                release(aThread);
                gil_release();
                host()->thread_attach(nullptr);
            }

            bool start_thread(object* aThread)
            {
                auto* const state = state_of(aThread);
                if (state == nullptr)
                    return false;
                {
                    std::lock_guard<std::mutex> lock{ state->mutex };
                    if (state->state != thread_state::Initial)
                    {
                        error("RuntimeError", "threads can only be started once");
                        return false;
                    }
                    state->state = thread_state::Started;
                }
                if (sContext == nullptr)
                    sContext = host()->thread_context();
                gil_activate();
                retain(aThread);
                retain(&state->h.base);
                {
                    std::lock_guard<std::mutex> lock{ sThreadsMutex };
                    sThreads.push_back(aThread);
                }
                try
                {
                    std::thread{ run_thread, aThread, state }.detach();
                }
                catch (...)
                {
                    error("RuntimeError", "can't start new thread");
                    return false;
                }
                return true;
            }

            bool join_thread(thread_state& aState, double aTimeout)
            {
                if (aState.ident == thread_ident() && aState.state == thread_state::Started)
                {
                    error("RuntimeError", "cannot join current thread");
                    return false;
                }
                {
                    std::lock_guard<std::mutex> lock{ aState.mutex };
                    if (aState.state == thread_state::Initial)
                    {
                        error("RuntimeError", "cannot join thread before it is started");
                        return false;
                    }
                }
                wait_for(aState.mutex, aState.condition, aTimeout, [&]() { return aState.state == thread_state::Finished; });
                return true;
            }

            // Thread.__init__(self, group=None, target=None, name=None, args=(), kwargs=None, *, daemon=None)
            object* thread_init(std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!method_arguments("Thread.__init__", aArgs, aKwargs, { "group", "target", "name", "args", "kwargs", "daemon" }, 0u, 0u, a))
                    return nullptr;
                auto const self = aArgs[0];
                if (a[0] != nullptr && kind_of(a[0]) != NEOS_PY_NONE)
                    return error("AssertionError", "group argument must be None for now");
                auto const state = new thread_state{ { { &sThreadStateType.base } } };
                ref const stateRef{ &state->h.base };
                if (setattr(self, "_neos_state", stateRef.get()) < 0)
                    return nullptr;
                std::string name;
                if (a[2] != nullptr && kind_of(a[2]) != NEOS_PY_NONE)
                {
                    if (!str_of(a[2], name))
                        return nullptr;
                }
                else
                {
                    name = "Thread-" + std::to_string(++sThreadCounter);
                    if (a[1] != nullptr && kind_of(a[1]) != NEOS_PY_NONE)
                    {
                        ref const targetName{ getattr(a[1], "__name__") };
                        if (targetName && kind_of(targetName.get()) == NEOS_PY_STR)
                            name += " (" + as<str_object>(targetName.get())->value + ")";
                        else
                            clear_error();
                    }
                }
                bool daemon = false;
                if (a[5] != nullptr && kind_of(a[5]) != NEOS_PY_NONE)
                    daemon = truth(a[5]) == 1;
                else if (tCurrentThread != nullptr)
                {
                    ref const d{ getattr(tCurrentThread, "daemon") };
                    daemon = d && truth(d.get()) == 1;
                    clear_error();
                }
                ref const nameValue{ make_str(name) };
                ref const target{ (a[1] != nullptr ? (retain(a[1]), a[1]) : (retain(none()), none())) };
                ref const args{ a[3] != nullptr ? (retain(a[3]), a[3]) : make_tuple() };
                ref const kwargs{ a[4] != nullptr && kind_of(a[4]) != NEOS_PY_NONE ? (retain(a[4]), a[4]) : make_dict() };
                ref const daemonValue{ make_bool(daemon) };
                if (setattr(self, "_target", target.get()) < 0 || setattr(self, "_args", args.get()) < 0 || setattr(self, "_kwargs", kwargs.get()) < 0 ||
                    setattr(self, "name", nameValue.get()) < 0 || setattr(self, "daemon", daemonValue.get()) < 0 || setattr(self, "_ident", none()) < 0)
                    return nullptr;
                return none();
            }

            builtin_entry const sThreadMethods[] =
            {
                { "__init__", thread_init },
                { "start", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!method_arguments("Thread.start", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        return start_thread(aArgs[0]) ? none() : nullptr;
                    } },
                { "run", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!method_arguments("Thread.run", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        ref const target{ getattr(aArgs[0], "_target") };
                        if (!target)
                            return nullptr;
                        if (kind_of(target.get()) == NEOS_PY_NONE)
                            return none();
                        ref const args{ getattr(aArgs[0], "_args") };
                        ref const kwargs{ getattr(aArgs[0], "_kwargs") };
                        if (!args || !kwargs)
                            return nullptr;
                        ref const tuple{ kind_of(args.get()) == NEOS_PY_TUPLE ? (retain(args.get()), args.get()) : fn_tuple_of(args.get()) };
                        if (!tuple)
                            return nullptr;
                        ref const r{ call(target.get(), tuple.get(), kwargs.get()) };
                        return r ? none() : nullptr;
                    } },
                { "join", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!method_arguments("Thread.join", aArgs, aKwargs, { "timeout" }, 0u, 0u, a))
                            return nullptr;
                        double timeout = -1.0;
                        if (a[0] != nullptr && kind_of(a[0]) != NEOS_PY_NONE && !to_double(a[0], timeout))
                            return nullptr;
                        if (timeout < -0.0 && a[0] != nullptr && kind_of(a[0]) != NEOS_PY_NONE)
                            timeout = 0.0;
                        auto* const state = state_of(aArgs[0]);
                        if (state == nullptr)
                            return nullptr;
                        return join_thread(*state, timeout) ? none() : nullptr;
                    } },
                { "is_alive", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto* const state = state_of(aArgs[0]);
                        if (state == nullptr)
                            return nullptr;
                        std::lock_guard<std::mutex> lock{ state->mutex };
                        return make_bool(state->state == thread_state::Started);
                    } },
                { "getName", [](std::vector<object*> const& aArgs, object*) -> object* { return getattr(aArgs[0], "name"); } },
                { "setName", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return error("TypeError", "setName() takes exactly one argument");
                        return setattr(aArgs[0], "name", aArgs[1]) < 0 ? nullptr : none();
                    } },
                { "isDaemon", [](std::vector<object*> const& aArgs, object*) -> object* { return getattr(aArgs[0], "daemon"); } },
                { "setDaemon", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return error("TypeError", "setDaemon() takes exactly one argument");
                        return setattr(aArgs[0], "daemon", aArgs[1]) < 0 ? nullptr : none();
                    } },
                { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const self = aArgs[0];
                        auto* const state = state_of(self);
                        if (state == nullptr)
                            return nullptr;
                        std::string status;
                        {
                            std::lock_guard<std::mutex> lock{ state->mutex };
                            status = state->state == thread_state::Initial ? "initial" : state->state == thread_state::Started ? "started" : "stopped";
                            if (state->state != thread_state::Initial)
                                status += " " + std::to_string(state->ident);
                        }
                        ref const d{ getattr(self, "daemon") };
                        if (d && truth(d.get()) == 1)
                            status += " daemon";
                        clear_error();
                        ref const cls{ class_of(self) };
                        ref const className{ getattr(cls.get(), "__name__") };
                        std::string const name = className && kind_of(className.get()) == NEOS_PY_STR ? as<str_object>(className.get())->value : std::string{ "Thread" };
                        return make_str("<" + name + "(" + attribute_text(self, "name") + ", " + status + ")>");
                    } }
            };

            // (properties of a Thread: ident and native_id)
            object* thread_class()
            {
                if (sThreadClass != nullptr)
                {
                    retain(sThreadClass);
                    return sThreadClass;
                }
                std::vector<std::pair<std::string, object*>> ns;
                for (auto const& m : sThreadMethods)
                    ns.emplace_back(m.name, make_builtin(m));
                {
                    static builtin_entry const sIdent{ "ident", [](std::vector<object*> const& aArgs, object*) -> object* { return getattr(aArgs[0], "_ident"); } };
                    ref const getter{ make_builtin(sIdent) };
                    ns.emplace_back("ident", b_property({ getter.get() }, nullptr));
                    ns.emplace_back("native_id", b_property({ getter.get() }, nullptr));
                }
                sThreadClass = make_class("threading", "Thread", {}, ns);
                for (auto& n : ns)
                    release(n.second);
                if (sThreadClass != nullptr)
                    retain(sThreadClass);
                return sThreadClass;
            }

            // Timer(interval, function, args=None, kwargs=None)
            builtin_entry const sTimerMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!method_arguments("Timer.__init__", aArgs, aKwargs, { "interval", "function", "args", "kwargs" }, 2u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        ref const r{ thread_init({ self }, nullptr) };
                        if (!r)
                            return nullptr;
                        ref const finished{ &(new event_object{ { { &sEventType.base } } })->h.base };
                        ref const args{ a[2] != nullptr && kind_of(a[2]) != NEOS_PY_NONE ? (retain(a[2]), a[2]) : make_list() };
                        ref const kwargs{ a[3] != nullptr && kind_of(a[3]) != NEOS_PY_NONE ? (retain(a[3]), a[3]) : make_dict() };
                        if (setattr(self, "interval", a[0]) < 0 || setattr(self, "function", a[1]) < 0 || setattr(self, "args", args.get()) < 0 ||
                            setattr(self, "kwargs", kwargs.get()) < 0 || setattr(self, "finished", finished.get()) < 0)
                            return nullptr;
                        return none();
                    } },
                { "cancel", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const finished{ getattr(aArgs[0], "finished") };
                        if (!finished)
                            return nullptr;
                        auto& e = as_event(finished.get());
                        {
                            std::lock_guard<std::mutex> lock{ e.mutex };
                            e.flag = true;
                        }
                        e.condition.notify_all();
                        return none();
                    } },
                { "run", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const self = aArgs[0];
                        ref const finished{ getattr(self, "finished") };
                        ref const interval{ getattr(self, "interval") };
                        if (!finished || !interval)
                            return nullptr;
                        double seconds = 0.0;
                        if (!to_double(interval.get(), seconds))
                            return nullptr;
                        auto& e = as_event(finished.get());
                        bool const cancelled = wait_for(e.mutex, e.condition, seconds, [&]() { return e.flag; });
                        if (!cancelled)
                        {
                            ref const function{ getattr(self, "function") };
                            ref const args{ getattr(self, "args") };
                            ref const kwargs{ getattr(self, "kwargs") };
                            if (!function || !args || !kwargs)
                                return nullptr;
                            ref const tuple{ fn_tuple_of(args.get()) };
                            if (!tuple)
                                return nullptr;
                            ref const r{ call(function.get(), tuple.get(), kwargs.get()) };
                            if (!r)
                                return nullptr;
                        }
                        {
                            std::lock_guard<std::mutex> lock{ e.mutex };
                            e.flag = true;
                        }
                        e.condition.notify_all();
                        return none();
                    } }
            };

            object* timer_class()
            {
                if (sTimerClass != nullptr)
                {
                    retain(sTimerClass);
                    return sTimerClass;
                }
                ref const base{ thread_class() };
                if (!base)
                    return nullptr;
                std::vector<std::pair<std::string, object*>> ns;
                for (auto const& m : sTimerMethods)
                    ns.emplace_back(m.name, make_builtin(m));
                sTimerClass = make_class("threading", "Timer", { base.get() }, ns);
                for (auto& n : ns)
                    release(n.second);
                if (sTimerClass != nullptr)
                    retain(sTimerClass);
                return sTimerClass;
            }

            object* main_thread()
            {
                if (sMainThread == nullptr)
                {
                    ref const base{ thread_class() };
                    if (!base)
                        return nullptr;
                    static builtin_entry const sMainInit{ "__init__", [](std::vector<object*> const& aArgs, object*) -> object* { return none(); } };
                    ref const cls{ make_class("threading", "_MainThread", { base.get() }, { { "__init__", make_builtin(sMainInit) } }) };
                    if (!cls)
                        return nullptr;
                    ref const empty{ make_tuple() };
                    ref thread{ call(cls.get(), empty.get(), nullptr) };
                    if (!thread)
                        return nullptr;
                    auto const state = new thread_state{ { { &sThreadStateType.base } } };
                    state->state = thread_state::Started;
                    state->ident = thread_ident();
                    ref const stateRef{ &state->h.base };
                    ref const name{ make_str("MainThread") };
                    ref const ident{ make_int(state->ident) };
                    setattr(thread.get(), "_neos_state", stateRef.get());
                    setattr(thread.get(), "name", name.get());
                    setattr(thread.get(), "daemon", make_bool(false));
                    setattr(thread.get(), "_ident", ident.get());
                    setattr(thread.get(), "_target", none());
                    sMainThread = thread.release_ownership();
                }
                retain(sMainThread);
                return sMainThread;
            }

            object* current_thread()
            {
                if (tCurrentThread != nullptr)
                {
                    retain(tCurrentThread);
                    return tCurrentThread;
                }
                return main_thread();
            }

            // --- the modules

            // (threading._register_atexit's functions: called (in reverse order) before the program's threads are joined)
            std::vector<object*> sThreadingAtexits;
            bool sThreadsShuttingDown = false;

            builtin_entry const sThreadingFunctions[] =
            {
                { "_register_atexit", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aArgs.empty())
                            return error("TypeError", "_register_atexit() missing 1 required positional argument: 'func'");
                        if (sThreadsShuttingDown)
                            return error("RuntimeError", "can't register atexit after shutdown");
                        ref const partial{ module_attribute("functools", "partial") };
                        if (!partial)
                            return nullptr;
                        std::vector<object*> args;
                        for (auto a : aArgs)
                            args.push_back(new_reference(a));
                        auto const call = invoke(partial.get(), std::move(args), aKwargs);
                        if (call == nullptr)
                            return nullptr;
                        sThreadingAtexits.push_back(call);
                        return new_reference(none());
                    } },
                { "Lock", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        return parse_arguments("Lock", aArgs, aKwargs, {}, 0u, 0u, a) ? make_lock(false) : nullptr;
                    } },
                { "RLock", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        return parse_arguments("RLock", aArgs, aKwargs, {}, 0u, 0u, a) ? make_lock(true) : nullptr;
                    } },
                { "Condition", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("Condition", aArgs, aKwargs, { "lock" }, 0u, 0u, a))
                            return nullptr;
                        auto const c = new condition_object{ { { &sConditionType.base } } };
                        if (a[0] != nullptr && kind_of(a[0]) != NEOS_PY_NONE)
                        {
                            if (!is_lock(a[0]))
                            {
                                release(&c->h.base);
                                return error("TypeError", "Condition(): the lock must be a Lock or an RLock");
                            }
                            retain(a[0]);
                            c->lock = a[0];
                        }
                        else
                            c->lock = make_lock(true);
                        return &c->h.base;
                    } },
                { "Event", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        return parse_arguments("Event", aArgs, aKwargs, {}, 0u, 0u, a) ? &(new event_object{ { { &sEventType.base } } })->h.base : nullptr;
                    } },
                { "Semaphore", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("Semaphore", aArgs, aKwargs, { "value" }, 0u, 0u, a))
                            return nullptr;
                        long long value = 1;
                        if (a[0] != nullptr && !to_index(a[0], value))
                            return nullptr;
                        if (value < 0)
                            return error("ValueError", "semaphore initial value must be >= 0");
                        auto const s = new semaphore_object{ { { &sSemaphoreType.base } } };
                        s->value = s->initial = value;
                        return &s->h.base;
                    } },
                { "BoundedSemaphore", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("BoundedSemaphore", aArgs, aKwargs, { "value" }, 0u, 0u, a))
                            return nullptr;
                        long long value = 1;
                        if (a[0] != nullptr && !to_index(a[0], value))
                            return nullptr;
                        if (value < 0)
                            return error("ValueError", "semaphore initial value must be >= 0");
                        auto const s = new semaphore_object{ { { &sBoundedSemaphoreType.base } } };
                        s->value = s->initial = value;
                        s->bounded = true;
                        return &s->h.base;
                    } },
                { "Barrier", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("Barrier", aArgs, aKwargs, { "parties", "action", "timeout" }, 1u, 0u, a))
                            return nullptr;
                        long long parties = 0;
                        if (!to_index(a[0], parties))
                            return nullptr;
                        if (parties < 1)
                            return error("ValueError", "parties must be >= 1");
                        auto const b = new barrier_object{ { { &sBarrierType.base } } };
                        b->parties = parties;
                        if (a[1] != nullptr)
                        {
                            retain(a[1]);
                            b->action = a[1];
                        }
                        if (a[2] != nullptr && !timeout_of(a[2], b->timeout))
                        {
                            release(&b->h.base);
                            return nullptr;
                        }
                        return &b->h.base;
                    } },
                { "current_thread", [](std::vector<object*> const&, object*) -> object* { return current_thread(); } },
                { "currentThread", [](std::vector<object*> const&, object*) -> object* { return current_thread(); } },
                { "main_thread", [](std::vector<object*> const&, object*) -> object* { return main_thread(); } },
                { "get_ident", [](std::vector<object*> const&, object*) -> object* { return make_int(thread_ident()); } },
                { "get_native_id", [](std::vector<object*> const&, object*) -> object* { return make_int(thread_ident()); } },
                { "active_count", [](std::vector<object*> const&, object*) -> object*
                    {
                        std::lock_guard<std::mutex> lock{ sThreadsMutex };
                        return make_int(static_cast<long long>(sThreads.size()) + 1);
                    } },
                { "activeCount", [](std::vector<object*> const&, object*) -> object*
                    {
                        std::lock_guard<std::mutex> lock{ sThreadsMutex };
                        return make_int(static_cast<long long>(sThreads.size()) + 1);
                    } },
                { "enumerate", [](std::vector<object*> const&, object*) -> object*
                    {
                        std::vector<object*> result{ main_thread() };
                        std::lock_guard<std::mutex> lock{ sThreadsMutex };
                        for (auto t : sThreads)
                            result.push_back((retain(t), t));
                        return make_list(std::move(result));
                    } },
                { "settrace", [](std::vector<object*> const&, object*) -> object* { return none(); } },
                { "setprofile", [](std::vector<object*> const&, object*) -> object* { return none(); } },
                { "stack_size", [](std::vector<object*> const&, object*) -> object* { return make_int(0); } }
            };

            bool threading_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sThreadingFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("local", local_class());
                auto const thread = thread_class();
                if (thread == nullptr)
                    return false;
                aAttributes.emplace_back("Thread", thread);
                auto const timer = timer_class();
                if (timer == nullptr)
                    return false;
                aAttributes.emplace_back("Timer", timer);
                aAttributes.emplace_back("TIMEOUT_MAX", make_float(9223372036.0));
                if (!is_exception_class("threading.BrokenBarrierError"))
                    user_exception_bases()["threading.BrokenBarrierError"] = { "RuntimeError" };
                aAttributes.emplace_back("BrokenBarrierError", exception_class("threading.BrokenBarrierError"));
                aAttributes.emplace_back("ThreadError", exception_class("RuntimeError"));
                return true;
            }

            builtin_entry const sThreadFunctions[] =
            {
                { "allocate_lock", [](std::vector<object*> const&, object*) -> object* { return make_lock(false); } },
                { "allocate", [](std::vector<object*> const&, object*) -> object* { return make_lock(false); } },
                { "RLock", [](std::vector<object*> const&, object*) -> object* { return make_lock(true); } },
                { "get_ident", [](std::vector<object*> const&, object*) -> object* { return make_int(thread_ident()); } },
                { "get_native_id", [](std::vector<object*> const&, object*) -> object* { return make_int(thread_ident()); } },
                { "_count", [](std::vector<object*> const&, object*) -> object*
                    {
                        std::lock_guard<std::mutex> lock{ sThreadsMutex };
                        return make_int(static_cast<long long>(sThreads.size()));
                    } },
                { "start_new_thread", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("start_new_thread", aArgs, aKwargs, { "function", "args", "kwargs" }, 2u, 3u, a))
                            return nullptr;
                        if (kind_of(a[1]) != NEOS_PY_TUPLE)
                            return error("TypeError", "2nd arg must be a tuple");
                        if (a[2] != nullptr && kind_of(a[2]) != NEOS_PY_DICT)
                            return error("TypeError", "optional 3rd arg must be a dictionary");
                        // (a Thread without a name of its own)
                        ref const cls{ thread_class() };
                        if (!cls)
                            return nullptr;
                        ref const empty{ make_tuple() };
                        ref const kwargs{ make_dict() };
                        ref const targetKey{ make_str("target") };
                        ref const argsKey{ make_str("args") };
                        ref const kwargsKey{ make_str("kwargs") };
                        ref const daemonKey{ make_str("daemon") };
                        dict_set(kwargs.get(), targetKey.get(), a[0]);
                        dict_set(kwargs.get(), argsKey.get(), a[1]);
                        if (a[2] != nullptr)
                            dict_set(kwargs.get(), kwargsKey.get(), a[2]);
                        dict_set(kwargs.get(), daemonKey.get(), make_bool(true));
                        ref const thread{ call(cls.get(), empty.get(), kwargs.get()) };
                        if (!thread || !start_thread(thread.get()))
                            return nullptr;
                        auto* const state = state_of(thread.get());
                        // (its identity once it has started)
                        gil_release();
                        {
                            std::unique_lock<std::mutex> lock{ state->mutex };
                            state->condition.wait_for(lock, std::chrono::milliseconds{ 100 }, [&]() { return state->ident != 0; });
                        }
                        gil_acquire();
                        return make_int(state->ident);
                    } },
                { "interrupt_main", [](std::vector<object*> const&, object*) -> object* { return none(); } },
                { "exit", [](std::vector<object*> const&, object*) -> object* { return error("SystemExit", ""); } },
                { "stack_size", [](std::vector<object*> const&, object*) -> object* { return make_int(0); } }
            };

            bool thread_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sThreadFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("_local", local_class());
                aAttributes.emplace_back("LockType", type_object_for("lock", "_thread.lock"));
                aAttributes.emplace_back("TIMEOUT_MAX", make_float(9223372036.0));
                aAttributes.emplace_back("error", exception_class("RuntimeError"));
                return true;
            }

            inline bool const sThreading = register_module("threading", threading_module);
            inline bool const sThread = register_module("_thread", thread_module);
        }

        // (the program has ended: its threads (except daemon threads) finish first)
        void threads_end()
        {
            // (threading._shutdown: the functions registered by threading._register_atexit (e.g. concurrent.futures's), in reverse order)
            if (!sThreadsShuttingDown)
            {
                sThreadsShuttingDown = true;
                for (auto i = sThreadingAtexits.rbegin(); i != sThreadingAtexits.rend(); ++i)
                {
                    ref const args{ make_tuple() };
                    ref const r{ call(*i, args.get(), nullptr) };
                    if (!r)
                        write_unraisable(*i);
                }
            }
            for (;;)
            {
                object* thread = nullptr;
                {
                    std::lock_guard<std::mutex> lock{ sThreadsMutex };
                    for (auto t : sThreads)
                    {
                        ref const d{ getattr(t, "daemon") };
                        bool const daemon = d && truth(d.get()) == 1;
                        if (!d)
                            clear_error();
                        if (!daemon)
                        {
                            thread = t;
                            retain(t);
                            break;
                        }
                    }
                }
                if (thread == nullptr)
                    break;
                ref const t{ thread };
                if (auto* const state = state_of(thread))
                    join_thread(*state, -1.0);
                else
                    clear_error();
            }
            // (daemon threads do not run again: the GIL is not released)
        }
    }
}
