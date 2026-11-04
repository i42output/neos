/*
  lib_threads.cpp

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

// The C language's runtime: threads (<threads.h> (7.28) and POSIX's <pthread.h>, <semaphore.h>, <sched.h>; their functions in C are
// in languages/packages/c/lib/threads.c). A thread is an OS thread attached to the program (neos_rt_host::thread_attach) that calls
// the program's function: its own stack in the C memory, its own block of thread-local objects (a copy of the template that the
// program's entry point gives: see rt_tls) and its own errno; the program's globals that are a thread's state (the C compiler's: see
// bytecode::GlobalPerThread) are its own. Mutexes, condition variables, read-write locks, barriers and semaphores are objects of the
// runtime's, whose handles are in the C objects' first word (0: not yet made: a statically initialized one is made when it is first
// used). A thread's id (thrd_t, pthread_t): 1 for the program's own thread, then 2, 3, ...

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include "core.hpp"

namespace neos::c_runtime
{
    namespace
    {
        // (POSIX's error numbers (as Linux has them: see <errno.h>))
        constexpr std::int32_t Eperm = 1;
        constexpr std::int32_t Esrch = 3;
        constexpr std::int32_t Eagain = 11;
        constexpr std::int32_t Ebusy = 16;
        constexpr std::int32_t Einval = 22;
        constexpr std::int32_t Edeadlk = 35;
        constexpr std::int32_t Etimedout = 110;

        // --- threads

        struct thread_record
        {
            std::uint64_t id = 0u;
            std::mutex mutex;
            std::condition_variable done;
            bool finished = false;
            bool detached = false;
            bool joining = false;
            std::int64_t result = 0;
            std::atomic<bool> cancelled = false;
            bool exiting = false;           ///< (it exits (thrd_exit, pthread_exit): the program's code stops (a trap), its result is exitValue)
            std::int64_t exitValue = 0;
            std::thread thread;
        };

        std::mutex& threads_mutex()
        {
            static std::mutex sMutex;
            return sMutex;
        }
        std::unordered_map<std::uint64_t, std::shared_ptr<thread_record>>& threads()
        {
            static std::unordered_map<std::uint64_t, std::shared_ptr<thread_record>> sThreads;
            return sThreads;
        }
        std::atomic<std::uint64_t> gNextThread = 2u;
        std::atomic<int> gRunning = 0;              ///< (threads other than the program's own that have not finished)
        std::mutex gRunningMutex;
        std::condition_variable gNoneRunning;

        thread_local std::uint64_t tThread = 1u;    ///< (the calling thread's id)
        thread_local address tErrno = language::c::layout::Errno;
        thread_local thread_record* tRecord = nullptr;

        // (the block of thread-local objects: its template (see rt_tls) and the global that has a thread's block's address)
        address gTlsTemplate = 0u;
        std::uint64_t gTlsSize = 0u;
        unsigned gTlsGlobal = 0u;

        // --- thread-specific storage (tss_t, pthread_key_t): keys (from 1), each thread's values

        struct key_info
        {
            bool used = false;
            address destructor = 0u;
        };
        std::mutex& keys_mutex()
        {
            static std::mutex sMutex;
            return sMutex;
        }
        std::vector<key_info>& keys()
        {
            static std::vector<key_info> sKeys(1u);
            return sKeys;
        }
        thread_local std::unordered_map<std::uint32_t, std::uint64_t> tValues;

        // (a thread's values' destructors called (each non-null value's, the value made null first), again while values are left (at most
        // four times, PTHREAD_DESTRUCTOR_ITERATIONS))
        void destroy_values()
        {
            for (int round = 0; round < 4 && !tValues.empty(); ++round)
            {
                std::vector<std::pair<address, std::uint64_t>> calls;
                {
                    std::lock_guard lock{ keys_mutex() };
                    for (auto& [key, value] : tValues)
                        if (value != 0u && key < keys().size() && keys()[key].used && keys()[key].destructor != 0u)
                        {
                            calls.emplace_back(keys()[key].destructor, value);
                            value = 0u;
                        }
                }
                std::erase_if(tValues, [](auto const& aValue) { return aValue.second == 0u; });
                if (calls.empty())
                    break;
                for (auto const& [destructor, value] : calls)
                    call_function(destructor, { static_cast<std::int64_t>(value) }, NEOS_RT_VOID, { NEOS_RT_I64 });
            }
            tValues.clear();
        }

        // --- the runtime's objects: handles (from 1) of objects that do not move (a table of chunks, read without a lock)

        struct object
        {
            virtual ~object() = default;
        };

        constexpr std::size_t ChunkSize = 1024u;
        constexpr std::size_t MaxChunks = 4096u;
        std::atomic<object*>* gChunks[MaxChunks] = {};
        std::mutex& objects_mutex()
        {
            static std::mutex sMutex;
            return sMutex;
        }
        std::vector<std::uint32_t>& free_handles()
        {
            static std::vector<std::uint32_t> sFree;
            return sFree;
        }
        std::uint32_t gNextHandle = 1u;

        std::uint32_t new_handle(object* aObject)
        {
            std::lock_guard lock{ objects_mutex() };
            std::uint32_t handle;
            if (!free_handles().empty())
            {
                handle = free_handles().back();
                free_handles().pop_back();
            }
            else
            {
                if (gNextHandle >= ChunkSize * MaxChunks)
                    return 0u;
                handle = gNextHandle++;
            }
            auto& chunk = gChunks[handle / ChunkSize];
            if (chunk == nullptr)
            {
                chunk = new std::atomic<object*>[ChunkSize];
                for (std::size_t i = 0u; i < ChunkSize; ++i)
                    chunk[i].store(nullptr, std::memory_order_relaxed);
            }
            chunk[handle % ChunkSize].store(aObject, std::memory_order_release);
            return handle;
        }

        object* object_of(std::uint32_t aHandle)
        {
            if (aHandle == 0u || aHandle >= ChunkSize * MaxChunks)
                return nullptr;
            auto* const chunk = gChunks[aHandle / ChunkSize];
            return chunk != nullptr ? chunk[aHandle % ChunkSize].load(std::memory_order_acquire) : nullptr;
        }

        void delete_handle(std::uint32_t aHandle)
        {
            object* o = nullptr;
            {
                std::lock_guard lock{ objects_mutex() };
                if (aHandle == 0u || aHandle >= gNextHandle)
                    return;
                o = gChunks[aHandle / ChunkSize][aHandle % ChunkSize].exchange(nullptr, std::memory_order_acq_rel);
                if (o != nullptr)
                    free_handles().push_back(aHandle);
            }
            delete o;
        }

        // (the object of a C object (its first word: its handle), made (by aMake) if it has none yet; null if it cannot be)
        template <typename T, typename Make>
        T* object_at(std::int64_t aObject, Make const& aMake)
        {
            if (aObject == 0)
                return nullptr;
            std::atomic_ref<std::uint32_t> word{ *at<std::uint32_t>(static_cast<address>(aObject)) };
            if (auto const handle = word.load(std::memory_order_acquire); handle != 0u)
                return static_cast<T*>(object_of(handle));
            auto* const made = aMake();
            auto const handle = new_handle(made);
            if (handle == 0u)
            {
                delete made;
                return nullptr;
            }
            std::uint32_t expected = 0u;
            if (word.compare_exchange_strong(expected, handle, std::memory_order_acq_rel))
                return made;
            delete_handle(handle);  // (another thread made one first)
            return static_cast<T*>(object_of(expected));
        }

        // (the object of a C object that has one (made by its init function), else null)
        template <typename T>
        T* existing_at(std::int64_t aObject)
        {
            if (aObject == 0)
                return nullptr;
            std::atomic_ref<std::uint32_t> word{ *at<std::uint32_t>(static_cast<address>(aObject)) };
            return static_cast<T*>(object_of(word.load(std::memory_order_acquire)));
        }

        void destroy_at(std::int64_t aObject)
        {
            if (aObject == 0)
                return;
            std::atomic_ref<std::uint32_t> word{ *at<std::uint32_t>(static_cast<address>(aObject)) };
            delete_handle(word.exchange(0u, std::memory_order_acq_rel));
        }

        // (an absolute time (a struct timespec's seconds and nanoseconds since the epoch, as CLOCK_REALTIME and TIME_UTC count))
        std::chrono::system_clock::time_point time_point_of(std::int64_t aSeconds, std::int64_t aNanoseconds)
        {
            return std::chrono::system_clock::time_point{ std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::seconds{ aSeconds } + std::chrono::nanoseconds{ aNanoseconds }) };
        }

        // --- mutexes: normal (0), recursive (1), error checking (2) (each can be locked with a timeout)

        struct mutex_object : object
        {
            explicit mutex_object(int aType) : type{ aType } {}
            int const type;
            std::mutex mutex;
            std::condition_variable_any released;
            std::uint64_t owner = 0u;
            std::uint32_t count = 0u;

            std::int32_t lock(std::optional<std::chrono::system_clock::time_point> aUntil = {})
            {
                std::unique_lock lock{ mutex };
                if (owner == tThread && count != 0u)
                {
                    if (type == 1)
                    {
                        ++count;
                        return 0;
                    }
                    if (type == 2)
                        return Edeadlk;
                }
                if (aUntil)
                {
                    if (!released.wait_until(lock, *aUntil, [&]() { return count == 0u; }))
                        return Etimedout;
                }
                else
                    released.wait(lock, [&]() { return count == 0u; });
                owner = tThread;
                count = 1u;
                return 0;
            }
            std::int32_t try_lock()
            {
                std::lock_guard lock{ mutex };
                if (count != 0u)
                {
                    if (owner == tThread && type == 1)
                    {
                        ++count;
                        return 0;
                    }
                    return Ebusy;
                }
                owner = tThread;
                count = 1u;
                return 0;
            }
            std::int32_t unlock()
            {
                {
                    std::lock_guard lock{ mutex };
                    if (count == 0u)
                        return Eperm;
                    // (a normal mutex that another thread unlocks: unlocked (as most implementations do))
                    if (owner != tThread && type != 0)
                        return Eperm;
                    if (--count != 0u)
                        return 0;
                    owner = 0u;
                }
                released.notify_one();
                return 0;
            }
            bool held() 
            {
                std::lock_guard lock{ mutex };
                return count != 0u && owner == tThread;
            }
            // (for a condition variable's wait: released entirely, then locked again as it was)
            std::uint32_t release_all()
            {
                std::uint32_t held;
                {
                    std::lock_guard lock{ mutex };
                    held = count;
                    count = 0u;
                    owner = 0u;
                }
                released.notify_one();
                return held;
            }
            void reacquire(std::uint32_t aCount)
            {
                std::unique_lock lock{ mutex };
                released.wait(lock, [&]() { return count == 0u; });
                owner = tThread;
                count = aCount;
            }
        };

        mutex_object* mutex_at(std::int64_t aMutex)
        {
            // (a mutex's C object: its handle, then its type (as a static initializer gives it))
            return object_at<mutex_object>(aMutex, [&]() { return new mutex_object{ *at<std::int32_t>(static_cast<address>(aMutex) + 4u) }; });
        }

        // --- condition variables (a wait releases the mutex and waits in one step: no signal between them is missed; a signal wakes the
        // thread that has waited longest)

        struct condition_object : object
        {
            struct waiter
            {
                bool signalled = false;
            };
            std::mutex mutex;
            std::condition_variable signalled;
            std::list<waiter*> waiters;

            std::int32_t wait(mutex_object& aMutex, std::optional<std::chrono::system_clock::time_point> aUntil = {})
            {
                std::unique_lock lock{ mutex };
                if (!aMutex.held())
                    return Eperm;
                waiter self;
                waiters.push_back(&self);
                auto const held = aMutex.release_all();
                bool timedOut = false;
                auto const woken = [&]() { return self.signalled; };
                if (aUntil)
                    timedOut = !signalled.wait_until(lock, *aUntil, woken);
                else
                    signalled.wait(lock, woken);
                if (timedOut)
                    waiters.remove(&self);
                lock.unlock();
                aMutex.reacquire(held);
                return timedOut ? Etimedout : 0;
            }
            void signal(bool aAll)
            {
                {
                    std::lock_guard lock{ mutex };
                    if (waiters.empty())
                        return;
                    do
                    {
                        waiters.front()->signalled = true;
                        waiters.pop_front();
                    } while (aAll && !waiters.empty());
                }
                signalled.notify_all();
            }
        };

        condition_object* condition_at(std::int64_t aCondition)
        {
            return object_at<condition_object>(aCondition, []() { return new condition_object{}; });
        }

        // --- read-write locks, barriers, semaphores

        struct rwlock_object : object
        {
            std::shared_timed_mutex lock;
            std::mutex mutex;
            std::unordered_map<std::uint64_t, std::uint32_t> readers;     ///< (the threads that hold it to read: how often)
            std::uint64_t writer = 0u;
        };

        rwlock_object* rwlock_at(std::int64_t aLock)
        {
            return object_at<rwlock_object>(aLock, []() { return new rwlock_object{}; });
        }

        struct barrier_object : object
        {
            explicit barrier_object(std::uint32_t aCount) : count{ aCount } {}
            std::uint32_t const count;
            std::mutex mutex;
            std::condition_variable all;
            std::uint32_t arrived = 0u;
            std::uint64_t generation = 0u;
        };

        struct semaphore_object : object
        {
            explicit semaphore_object(std::uint32_t aValue) : value{ aValue } {}
            std::mutex mutex;
            std::condition_variable posted;
            std::uint32_t value;
        };

        semaphore_object* semaphore_at(std::int64_t aSemaphore)
        {
            return object_at<semaphore_object>(aSemaphore, [&]() { return new semaphore_object{ *at<std::uint32_t>(static_cast<address>(aSemaphore) + 4u) }; });
        }

        // --- the native functions

        std::int64_t c_errno_location()
        {
            return static_cast<std::int64_t>(tErrno);
        }

        // (a new thread: it calls the wrapper (the C library's: it calls the thread's function (aFunction) with its argument (aArgument)
        // and gives its result, unless the thread exits); its id at aThread)
        std::int32_t c_thread_create(std::int64_t aThread, std::int64_t aWrapper, std::int64_t aFunction, std::int64_t aArgument, std::int64_t aStackSize,
            std::int32_t aDetached)
        {
            auto* const h = host();
            if (h == nullptr || h->version < 10u || h->thread_context == nullptr || h->thread_attach == nullptr || h->global == nullptr)
                return Eagain;
            auto* const program = h->thread_context();
            if (program == nullptr)
                return Eagain;
            std::uint64_t stackSize = std::uint64_t{ 8u } << 20u;
            if (auto const s = std::getenv("NEOS_C_STACK"))
                stackSize = std::max<std::uint64_t>(std::strtoull(s, nullptr, 0), std::uint64_t{ 256u } << 10u);
            if (aStackSize > 0)
                stackSize = std::max<std::uint64_t>(static_cast<std::uint64_t>(aStackSize), std::uint64_t{ 64u } << 10u);
            auto record = std::make_shared<thread_record>();
            record->id = gNextThread++;
            record->detached = aDetached != 0;
            {
                std::lock_guard lock{ threads_mutex() };
                threads()[record->id] = record;
            }
            if (aThread != 0)
                *at<std::uint64_t>(static_cast<address>(aThread)) = record->id;
            ++gRunning;
            try
            {
                std::lock_guard lock{ record->mutex };   // (the thread waits for its std::thread to be set)
                record->thread = std::thread{ [h, program, record, aWrapper, aFunction, aArgument, stackSize]()
                {
                    {
                        std::lock_guard wait{ record->mutex };
                    }
                    h->thread_attach(program);
                    tThread = record->id;
                    tRecord = record.get();
                    // (its state: its errno, its stack, its block of thread-local objects)
                    auto const errnoCell = allocate(16u);
                    if (errnoCell != 0u)
                        tErrno = errnoCell;
                    unsigned spGlobal = 0u;
                    unsigned limitGlobal = 0u;
                    stack_globals(spGlobal, limitGlobal);
                    auto const stack = take_pages(stackSize);
                    if (auto* sp = h->global(spGlobal))
                        *sp = stack + stackSize;
                    if (auto* limit = h->global(limitGlobal))
                        *limit = stack + 4096u;
                    address tls = 0u;
                    if (gTlsSize != 0u)
                    {
                        tls = allocate_aligned(64u, gTlsSize);
                        if (tls != 0u)
                            std::memcpy(at<std::uint8_t>(tls), at<std::uint8_t>(gTlsTemplate), static_cast<std::size_t>(gTlsSize));
                        if (auto* global = h->global(gTlsGlobal))
                            *global = tls;
                    }
                    std::int64_t result = 0;
                    neos_rt_value arguments[2]{};
                    neos_rt_type types[2] = { NEOS_RT_I64, NEOS_RT_I64 };
                    arguments[0].i64 = aFunction;
                    arguments[1].i64 = aArgument;
                    neos_rt_value value{};
                    if (auto const error = h->call(function_of(static_cast<address>(aWrapper)), 2u, types, arguments, NEOS_RT_I64, &value); 
                        error != nullptr && record->exiting)
                        value.i64 = record->exitValue;
                    else if (error != nullptr)
                    {
                        // (a runtime error in a thread: the program's, as a C program's that faults: it ends)
                        std_flush(1);
                        std::fprintf(stderr, "Runtime error: %s\n", error);
                        std::fflush(stderr);
                        std::_Exit(1);
                    }
                    result = value.i64;
                    destroy_values();
                    if (tls != 0u)
                        deallocate(tls);
                    if (stack != 0u)
                        give_pages(stack, stackSize);
                    h->thread_attach(nullptr);
                    {
                        std::lock_guard lock{ gRunningMutex };
                        --gRunning;
                    }
                    gNoneRunning.notify_all();
                    bool detached;
                    {
                        std::lock_guard lock{ record->mutex };
                        record->result = result;
                        record->finished = true;
                        detached = record->detached;
                    }
                    record->done.notify_all();
                    if (errnoCell != 0u)
                        deallocate(errnoCell);
                    if (detached)
                    {
                        // (no one joins it: forgotten (its std::thread detached))
                        std::lock_guard lock{ threads_mutex() };
                        if (record->thread.joinable())
                            record->thread.detach();
                        threads().erase(record->id);
                    }
                } };
            }
            catch (...)
            {
                --gRunning;
                std::lock_guard lock{ threads_mutex() };
                threads().erase(record->id);
                return Eagain;
            }
            return 0;
        }

        std::shared_ptr<thread_record> thread_of(std::int64_t aThread)
        {
            std::lock_guard lock{ threads_mutex() };
            auto const found = threads().find(static_cast<std::uint64_t>(aThread));
            return found != threads().end() ? found->second : nullptr;
        }

        std::int32_t c_thread_join(std::int64_t aThread, std::int64_t aResult)
        {
            if (static_cast<std::uint64_t>(aThread) == tThread)
                return Edeadlk;
            auto record = thread_of(aThread);
            if (record == nullptr)
                return Esrch;
            {
                std::unique_lock lock{ record->mutex };
                if (record->detached || record->joining)
                    return Einval;
                record->joining = true;
                record->done.wait(lock, [&]() { return record->finished; });
            }
            if (record->thread.joinable())
                record->thread.join();
            if (aResult != 0)
                *at<std::int64_t>(static_cast<address>(aResult)) = record->result;
            std::lock_guard lock{ threads_mutex() };
            threads().erase(record->id);
            return 0;
        }

        std::int32_t c_thread_detach(std::int64_t aThread)
        {
            auto record = thread_of(aThread);
            if (record == nullptr)
                return Esrch;
            bool finished;
            {
                std::lock_guard lock{ record->mutex };
                if (record->detached || record->joining)
                    return Einval;
                record->detached = true;
                finished = record->finished;
            }
            if (finished)
            {
                if (record->thread.joinable())
                    record->thread.join();
                std::lock_guard lock{ threads_mutex() };
                threads().erase(record->id);
            }
            return 0;
        }

        std::int64_t c_thread_self()
        {
            return static_cast<std::int64_t>(tThread);
        }

        std::int32_t c_thread_main()
        {
            return tThread == 1u ? 1 : 0;
        }

        std::int32_t c_thread_sleep(std::int64_t aSeconds, std::int64_t aNanoseconds)
        {
            if (aSeconds < 0 || aNanoseconds < 0 || aNanoseconds >= 1000000000)
                return Einval;
            std::this_thread::sleep_for(std::chrono::seconds{ aSeconds } + std::chrono::nanoseconds{ aNanoseconds });
            return 0;
        }

        void c_thread_yield()
        {
            std::this_thread::yield();
        }

        std::int32_t c_thread_cancel(std::int64_t aThread)
        {
            auto record = thread_of(aThread);
            if (record == nullptr)
                return Esrch;
            record->cancelled = true;
            return 0;
        }

        // (a thread (not the program's) exits (thrd_exit, pthread_exit) with the result aValue: its code stops (a trap that its
        // wrapper's call returns: see c_thread_create))
        void c_thread_exit(std::int64_t aValue)
        {
            if (tRecord == nullptr)
                return;
            tRecord->exiting = true;
            tRecord->exitValue = aValue;
            trap("thread exit");
        }

        // (the program's thread exits (thrd_exit, pthread_exit): the program ends when the other threads have finished)
        void c_thread_wait_others()
        {
            std::unique_lock lock{ gRunningMutex };
            gNoneRunning.wait(lock, [&]() { return gRunning.load() == (tThread != 1u ? 1 : 0); });
        }

        std::int32_t c_thread_cancelled()
        {
            return tRecord != nullptr && tRecord->cancelled.load() ? 1 : 0;
        }

        // (the program's entry point: the template of the blocks of thread-local objects, and the global that has a thread's block's
        // address)
        void rt_tls(std::int64_t aTemplate, std::int64_t aSize, std::int32_t aGlobal)
        {
            gTlsTemplate = static_cast<address>(aTemplate);
            gTlsSize = static_cast<std::uint64_t>(aSize);
            gTlsGlobal = static_cast<unsigned>(aGlobal);
        }

        // mutexes
        std::int32_t c_mutex_init(std::int64_t aMutex, std::int32_t aType)
        {
            *at<std::uint32_t>(static_cast<address>(aMutex)) = 0u;
            *at<std::int32_t>(static_cast<address>(aMutex) + 4u) = aType;
            return mutex_at(aMutex) != nullptr ? 0 : Eagain;
        }
        std::int32_t c_mutex_lock(std::int64_t aMutex)
        {
            auto* m = mutex_at(aMutex);
            return m != nullptr ? m->lock() : Einval;
        }
        std::int32_t c_mutex_trylock(std::int64_t aMutex)
        {
            auto* m = mutex_at(aMutex);
            return m != nullptr ? m->try_lock() : Einval;
        }
        std::int32_t c_mutex_timedlock(std::int64_t aMutex, std::int64_t aSeconds, std::int64_t aNanoseconds)
        {
            auto* m = mutex_at(aMutex);
            return m != nullptr ? m->lock(time_point_of(aSeconds, aNanoseconds)) : Einval;
        }
        std::int32_t c_mutex_unlock(std::int64_t aMutex)
        {
            auto* m = mutex_at(aMutex);
            return m != nullptr ? m->unlock() : Einval;
        }
        std::int32_t c_object_destroy(std::int64_t aObject)
        {
            destroy_at(aObject);
            return 0;
        }

        // condition variables
        std::int32_t c_cond_init(std::int64_t aCondition)
        {
            *at<std::uint32_t>(static_cast<address>(aCondition)) = 0u;
            return condition_at(aCondition) != nullptr ? 0 : Eagain;
        }
        std::int32_t c_cond_wait(std::int64_t aCondition, std::int64_t aMutex)
        {
            auto* c = condition_at(aCondition);
            auto* m = mutex_at(aMutex);
            return c != nullptr && m != nullptr ? c->wait(*m) : Einval;
        }
        std::int32_t c_cond_timedwait(std::int64_t aCondition, std::int64_t aMutex, std::int64_t aSeconds, std::int64_t aNanoseconds)
        {
            auto* c = condition_at(aCondition);
            auto* m = mutex_at(aMutex);
            return c != nullptr && m != nullptr ? c->wait(*m, time_point_of(aSeconds, aNanoseconds)) : Einval;
        }
        std::int32_t c_cond_signal(std::int64_t aCondition)
        {
            if (auto* c = condition_at(aCondition))
                c->signal(false);
            return 0;
        }
        std::int32_t c_cond_broadcast(std::int64_t aCondition)
        {
            if (auto* c = condition_at(aCondition))
                c->signal(true);
            return 0;
        }

        // read-write locks
        std::int32_t c_rwlock_init(std::int64_t aLock)
        {
            *at<std::uint32_t>(static_cast<address>(aLock)) = 0u;
            return rwlock_at(aLock) != nullptr ? 0 : Eagain;
        }
        std::int32_t c_rwlock_lock(std::int64_t aLock, std::int32_t aWrite, std::int32_t aTry)
        {
            auto* l = rwlock_at(aLock);
            if (l == nullptr)
                return Einval;
            {
                std::lock_guard lock{ l->mutex };
                if (l->writer == tThread)
                    return Edeadlk;
                if (aWrite != 0 && l->readers.contains(tThread))
                    return Edeadlk;
            }
            if (aWrite != 0)
            {
                if (aTry != 0 ? !l->lock.try_lock() : (l->lock.lock(), false))
                    return Ebusy;
                std::lock_guard lock{ l->mutex };
                l->writer = tThread;
            }
            else
            {
                if (aTry != 0 ? !l->lock.try_lock_shared() : (l->lock.lock_shared(), false))
                    return Ebusy;
                std::lock_guard lock{ l->mutex };
                ++l->readers[tThread];
            }
            return 0;
        }
        std::int32_t c_rwlock_unlock(std::int64_t aLock)
        {
            auto* l = rwlock_at(aLock);
            if (l == nullptr)
                return Einval;
            std::lock_guard lock{ l->mutex };
            if (l->writer == tThread)
            {
                l->writer = 0u;
                l->lock.unlock();
                return 0;
            }
            auto const found = l->readers.find(tThread);
            if (found == l->readers.end())
                return Eperm;
            if (--found->second == 0u)
                l->readers.erase(found);
            l->lock.unlock_shared();
            return 0;
        }

        // barriers (the result: 1 for one thread of each round (PTHREAD_BARRIER_SERIAL_THREAD's), else 0)
        std::int32_t c_barrier_init(std::int64_t aBarrier, std::int32_t aCount)
        {
            if (aCount <= 0)
                return Einval;
            *at<std::uint32_t>(static_cast<address>(aBarrier)) = 0u;
            return object_at<barrier_object>(aBarrier, [&]() { return new barrier_object{ static_cast<std::uint32_t>(aCount) }; }) != nullptr ? 0 : Eagain;
        }
        std::int32_t c_barrier_wait(std::int64_t aBarrier)
        {
            auto* b = existing_at<barrier_object>(aBarrier);
            if (b == nullptr)
                return -Einval;
            std::unique_lock lock{ b->mutex };
            auto const generation = b->generation;
            if (++b->arrived == b->count)
            {
                b->arrived = 0u;
                ++b->generation;
                lock.unlock();
                b->all.notify_all();
                return 1;
            }
            b->all.wait(lock, [&]() { return b->generation != generation; });
            return 0;
        }

        // semaphores (a semaphore's C object: its handle, then its initial value (see sem_init))
        std::int32_t c_sem_init(std::int64_t aSemaphore, std::int32_t aValue)
        {
            if (aValue < 0)
                return Einval;
            *at<std::uint32_t>(static_cast<address>(aSemaphore)) = 0u;
            *at<std::uint32_t>(static_cast<address>(aSemaphore) + 4u) = static_cast<std::uint32_t>(aValue);
            return semaphore_at(aSemaphore) != nullptr ? 0 : Eagain;
        }
        std::int32_t c_sem_wait(std::int64_t aSemaphore, std::int32_t aTry)
        {
            auto* s = semaphore_at(aSemaphore);
            if (s == nullptr)
                return Einval;
            std::unique_lock lock{ s->mutex };
            if (aTry != 0)
            {
                if (s->value == 0u)
                    return Eagain;
            }
            else
                s->posted.wait(lock, [&]() { return s->value != 0u; });
            --s->value;
            return 0;
        }
        std::int32_t c_sem_timedwait(std::int64_t aSemaphore, std::int64_t aSeconds, std::int64_t aNanoseconds)
        {
            auto* s = semaphore_at(aSemaphore);
            if (s == nullptr)
                return Einval;
            std::unique_lock lock{ s->mutex };
            if (!s->posted.wait_until(lock, time_point_of(aSeconds, aNanoseconds), [&]() { return s->value != 0u; }))
                return Etimedout;
            --s->value;
            return 0;
        }
        std::int32_t c_sem_post(std::int64_t aSemaphore)
        {
            auto* s = semaphore_at(aSemaphore);
            if (s == nullptr)
                return Einval;
            {
                std::lock_guard lock{ s->mutex };
                ++s->value;
            }
            s->posted.notify_one();
            return 0;
        }
        std::int32_t c_sem_getvalue(std::int64_t aSemaphore, std::int64_t aValue)
        {
            auto* s = semaphore_at(aSemaphore);
            if (s == nullptr)
                return Einval;
            std::lock_guard lock{ s->mutex };
            *at<std::int32_t>(static_cast<address>(aValue)) = static_cast<std::int32_t>(s->value);
            return 0;
        }

        // thread-specific storage
        std::int32_t c_key_create(std::int64_t aKey, std::int64_t aDestructor)
        {
            std::lock_guard lock{ keys_mutex() };
            auto& k = keys();
            std::uint32_t key = 1u;
            while (key < k.size() && k[key].used)
                ++key;
            if (key == k.size())
                k.emplace_back();
            k[key].used = true;
            k[key].destructor = static_cast<address>(aDestructor);
            *at<std::uint32_t>(static_cast<address>(aKey)) = key;
            return 0;
        }
        std::int32_t c_key_delete(std::int32_t aKey)
        {
            std::lock_guard lock{ keys_mutex() };
            auto const key = static_cast<std::uint32_t>(aKey);
            if (key == 0u || key >= keys().size() || !keys()[key].used)
                return Einval;
            keys()[key] = key_info{};
            return 0;
        }
        std::int64_t c_key_get(std::int32_t aKey)
        {
            auto const found = tValues.find(static_cast<std::uint32_t>(aKey));
            return found != tValues.end() ? static_cast<std::int64_t>(found->second) : 0;
        }
        std::int32_t c_key_set(std::int32_t aKey, std::int64_t aValue)
        {
            {
                std::lock_guard lock{ keys_mutex() };
                auto const key = static_cast<std::uint32_t>(aKey);
                if (key == 0u || key >= keys().size() || !keys()[key].used)
                    return Einval;
            }
            tValues[static_cast<std::uint32_t>(aKey)] = static_cast<std::uint64_t>(aValue);
            return 0;
        }

        function_table const sThreadFunctions
        {
            { "rt.tls", NEOS_RT_FUNCTION(rt_tls), NEOS_RT_VOID, 3u, { NEOS_RT_I64, NEOS_RT_I64, NEOS_RT_I32 }, 0u },
            library("__errno_location", NEOS_RT_FUNCTION(c_errno_location)),
            library("__neos_thread_create", NEOS_RT_FUNCTION(c_thread_create)), library("__neos_thread_join", NEOS_RT_FUNCTION(c_thread_join)),
            library("__neos_thread_detach", NEOS_RT_FUNCTION(c_thread_detach)), library("__neos_thread_self", NEOS_RT_FUNCTION(c_thread_self)),
            library("__neos_thread_main", NEOS_RT_FUNCTION(c_thread_main)), library("__neos_thread_sleep", NEOS_RT_FUNCTION(c_thread_sleep)),
            library("__neos_thread_yield", NEOS_RT_FUNCTION(c_thread_yield)), library("__neos_thread_cancel", NEOS_RT_FUNCTION(c_thread_cancel)),
            library("__neos_thread_cancelled", NEOS_RT_FUNCTION(c_thread_cancelled)), library("__neos_thread_wait_others", NEOS_RT_FUNCTION(c_thread_wait_others)),
            library("__neos_thread_exit", NEOS_RT_FUNCTION(c_thread_exit)),
            library("__neos_mutex_init", NEOS_RT_FUNCTION(c_mutex_init)), library("__neos_mutex_lock", NEOS_RT_FUNCTION(c_mutex_lock)),
            library("__neos_mutex_trylock", NEOS_RT_FUNCTION(c_mutex_trylock)), library("__neos_mutex_timedlock", NEOS_RT_FUNCTION(c_mutex_timedlock)),
            library("__neos_mutex_unlock", NEOS_RT_FUNCTION(c_mutex_unlock)), library("__neos_object_destroy", NEOS_RT_FUNCTION(c_object_destroy)),
            library("__neos_cond_init", NEOS_RT_FUNCTION(c_cond_init)), library("__neos_cond_wait", NEOS_RT_FUNCTION(c_cond_wait)),
            library("__neos_cond_timedwait", NEOS_RT_FUNCTION(c_cond_timedwait)), library("__neos_cond_signal", NEOS_RT_FUNCTION(c_cond_signal)),
            library("__neos_cond_broadcast", NEOS_RT_FUNCTION(c_cond_broadcast)),
            library("__neos_rwlock_init", NEOS_RT_FUNCTION(c_rwlock_init)), library("__neos_rwlock_lock", NEOS_RT_FUNCTION(c_rwlock_lock)),
            library("__neos_rwlock_unlock", NEOS_RT_FUNCTION(c_rwlock_unlock)),
            library("__neos_barrier_init", NEOS_RT_FUNCTION(c_barrier_init)), library("__neos_barrier_wait", NEOS_RT_FUNCTION(c_barrier_wait)),
            library("__neos_sem_init", NEOS_RT_FUNCTION(c_sem_init)), library("__neos_sem_wait", NEOS_RT_FUNCTION(c_sem_wait)),
            library("__neos_sem_timedwait", NEOS_RT_FUNCTION(c_sem_timedwait)), library("__neos_sem_post", NEOS_RT_FUNCTION(c_sem_post)),
            library("__neos_sem_getvalue", NEOS_RT_FUNCTION(c_sem_getvalue)),
            library("__neos_key_create", NEOS_RT_FUNCTION(c_key_create)), library("__neos_key_delete", NEOS_RT_FUNCTION(c_key_delete)),
            library("__neos_key_get", NEOS_RT_FUNCTION(c_key_get)), library("__neos_key_set", NEOS_RT_FUNCTION(c_key_set))
        };
    }

    address thread_errno()
    {
        return tErrno;
    }

    void end_threads(int aStatus)
    {
        // (the program ends while threads other than the calling one run: the process ends now (they cannot be stopped), its output
        // flushed; else the program's thread ends it as usual)
        if (tThread == 1u && gRunning.load() == 0)
            return;
        std_flush(1);
        std_flush(2);
        std::fflush(nullptr);
        std::_Exit(aStatus);
    }
}
