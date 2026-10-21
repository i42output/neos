/*
  builtins_atomics.cpp

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

// Atomics (there is one agent: the operations are atomic because nothing else accesses the memory)

#include <algorithm>
#include <chrono>
#include <thread>
#include "js.hpp"
#include "api.hpp"
#include "typed_arrays.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        // (ValidateIntegerTypedArray)
        typed_array_object& validate_integer_typed_array(value aValue, bool aWaitable)
        {
            auto& t = validate_typed_array(aValue);
            switch (t.type)
            {
            case element_type::Int32:
            case element_type::BigInt64:
                return t;
            case element_type::Int8:
            case element_type::Uint8:
            case element_type::Int16:
            case element_type::Uint16:
            case element_type::Uint32:
            case element_type::BigUint64:
                if (!aWaitable)
                    return t;
                throw_type_error("Atomics.wait and Atomics.notify require an Int32Array or a BigInt64Array");
            default:
                throw_type_error("Atomics operations require an integer typed array");
            }
        }

        // (ValidateAtomicAccess: the byte index in the buffer)
        std::size_t validate_atomic_access(typed_array_object& aTypedArray, value aRequestIndex)
        {
            auto const length = static_cast<double>(aTypedArray.length());
            auto const accessIndex = to_index(aRequestIndex);
            if (accessIndex >= length)
                throw_range_error("Invalid atomic access index");
            return static_cast<std::size_t>(accessIndex) * aTypedArray.element_size() + aTypedArray.byteOffset;
        }

        // (RevalidateAtomicAccess)
        void revalidate_atomic_access(typed_array_object& aTypedArray, std::size_t aByteIndex)
        {
            if (aTypedArray.out_of_bounds())
                throw_type_error(aTypedArray.buffer_object().detached ? "Cannot perform Atomics operation on a detached ArrayBuffer" : "typed array is out of bounds");
            if (aByteIndex + aTypedArray.element_size() - 1u >= aTypedArray.buffer_object().byte_length())
                throw_range_error("Invalid atomic access index");
        }

        // (the coerced value of an operand: a BigInt or 𝔽(ToIntegerOrInfinity))
        value coerce(typed_array_object const& aTypedArray, value aValue)
        {
            if (aTypedArray.bigint())
                return to_bigint(aValue);
            return number(to_integer_or_infinity(aValue));
        }

        enum operation : std::uint32_t { Add, And, Exchange, Or, Sub, Xor };

        // (AtomicReadModifyWrite: the callee's nativeId is the operation)
        value atomics_read_modify_write(value, arguments aArguments, value, function_object& aCallee)
        {
            agent_yield();
            auto const ta = argument(aArguments, 0u);
            auto& t = validate_integer_typed_array(ta, false);
            auto const byteIndex = validate_atomic_access(t, argument(aArguments, 1u));
            auto const v = coerce(t, argument(aArguments, 2u));
            revalidate_atomic_access(t, byteIndex);
            auto* const bytes = t.buffer_object().bytes() + byteIndex;
            auto const size = t.element_size();
            auto const old = element_value(t.type, bytes);
            std::uint8_t operand[8];
            auto const op = static_cast<operation>(aCallee.nativeId);
            if (op == Add || op == Sub)
            {
                if (t.bigint())
                {
                    auto const x = bigint_to_uint64(old);
                    auto const y = bigint_to_uint64(v);
                    auto result = op == Add ? x + y : x - y;
                    if constexpr (std::endian::native != std::endian::little)
                        result = std::byteswap(result);
                    std::memcpy(bytes, &result, 8u);
                }
                else
                {
                    auto const x = as_number(old);
                    auto const y = as_number(v);
                    store_element(t.type, number(op == Add ? x + y : x - y), bytes);
                }
                return old;
            }
            store_element(t.type, v, operand);
            for (std::size_t i = 0u; i < size; ++i)
            {
                switch (op)
                {
                case And:
                    bytes[i] &= operand[i];
                    break;
                case Or:
                    bytes[i] |= operand[i];
                    break;
                case Xor:
                    bytes[i] ^= operand[i];
                    break;
                default:
                    bytes[i] = operand[i];
                    break;
                }
            }
            return old;
        }

        value atomics_compare_exchange(value, arguments aArguments, value, function_object&)
        {
            agent_yield();
            auto& t = validate_integer_typed_array(argument(aArguments, 0u), false);
            auto const byteIndex = validate_atomic_access(t, argument(aArguments, 1u));
            auto const expected = coerce(t, argument(aArguments, 2u));
            auto const replacement = coerce(t, argument(aArguments, 3u));
            revalidate_atomic_access(t, byteIndex);
            auto const size = t.element_size();
            std::uint8_t expectedBytes[8];
            std::uint8_t replacementBytes[8];
            store_element(t.type, expected, expectedBytes);
            store_element(t.type, replacement, replacementBytes);
            auto* const bytes = t.buffer_object().bytes() + byteIndex;
            auto const old = element_value(t.type, bytes);
            if (std::memcmp(bytes, expectedBytes, size) == 0)
                std::memcpy(bytes, replacementBytes, size);
            return old;
        }

        value atomics_is_lock_free(value, arguments aArguments, value, function_object&)
        {
            auto const n = to_integer_or_infinity(argument(aArguments, 0u));
            return boolean(n == 1.0 || n == 2.0 || n == 4.0 || n == 8.0);
        }

        value atomics_load(value, arguments aArguments, value, function_object&)
        {
            agent_yield();  // (an agent that spins on a location lets the others run)
            auto& t = validate_integer_typed_array(argument(aArguments, 0u), false);
            auto const byteIndex = validate_atomic_access(t, argument(aArguments, 1u));
            revalidate_atomic_access(t, byteIndex);
            return element_value(t.type, t.buffer_object().bytes() + byteIndex);
        }

        value atomics_store(value, arguments aArguments, value, function_object&)
        {
            auto& t = validate_integer_typed_array(argument(aArguments, 0u), false);
            auto const byteIndex = validate_atomic_access(t, argument(aArguments, 1u));
            auto const v = coerce(t, argument(aArguments, 2u));
            revalidate_atomic_access(t, byteIndex);
            store_element(t.type, v, t.buffer_object().bytes() + byteIndex);
            return v;
        }

        // (the waiters (Atomics.waitAsync's: the one agent cannot be suspended while it waits for itself), in the order they
        // began waiting)
        struct waiter
        {
            value buffer;
            std::size_t byteIndex;
            value resolve;
            std::chrono::steady_clock::time_point timeoutTime;
            bool finite;
            bool sync = false;          ///< (Atomics.wait's: an agent (or the main program) blocked until it is notified)
            bool notified = false;
        };

        std::vector<std::shared_ptr<waiter>>& waiters()
        {
            static std::vector<std::shared_ptr<waiter>> sWaiters;
            return sWaiters;
        }

        root_tracer const sWaiterRoots{ [](tracer& aTracer)
        {
            for (auto const& w : waiters())
            {
                aTracer.mark(w->buffer);
                aTracer.mark(w->resolve);
            }
        } };

        // (aResolve: a waiter's (no longer in the list: its value is on the caller's stack))
        void resolve_waiter(value aResolve, char const* aResult)
        {
            value const argument[] = { ascii(aResult) };
            call(aResolve, Undefined, argument);
        }

        // (the timeout of a waiter: a job that runs (again) until its time has come or it has been notified)
        void enqueue_timeout_job(std::shared_ptr<waiter> aWaiter)
        {
            enqueue_job([aWaiter]()
            {
                auto& list = waiters();
                auto const existing = std::find(list.begin(), list.end(), aWaiter);
                if (existing == list.end())
                    return;
                auto const now = std::chrono::steady_clock::now();
                if (now < aWaiter->timeoutTime)
                {
                    if (in_agent())
                    {
                        // (a test262 agent: it blocks (the others run) until the waiter is notified or times out)
                        auto const deadline = monotonic_ms() + std::chrono::duration<double, std::milli>(aWaiter->timeoutTime - now).count();
                        agent_block([aWaiter]()
                        {
                            auto const& l = waiters();
                            return std::find(l.begin(), l.end(), aWaiter) == l.end() || std::chrono::steady_clock::now() >= aWaiter->timeoutTime;
                        }, deadline);
                        enqueue_timeout_job(aWaiter);
                        return;
                    }
                    agent_yield();  // (the agents run (one may notify it))
                    std::this_thread::sleep_for(std::min<std::chrono::steady_clock::duration>(aWaiter->timeoutTime - now, std::chrono::milliseconds{ 1 }));
                    enqueue_timeout_job(aWaiter);
                    return;
                }
                auto const resolve = aWaiter->resolve;
                list.erase(existing);
                resolve_waiter(resolve, "timed-out");
            });
        }

        value atomics_notify(value, arguments aArguments, value, function_object&)
        {
            agent_yield();
            auto& t = validate_integer_typed_array(argument(aArguments, 0u), true);
            auto const byteIndex = validate_atomic_access(t, argument(aArguments, 1u));
            auto const count = argument(aArguments, 2u);
            double c = std::numeric_limits<double>::infinity();
            if (count != Undefined)
                c = std::max(to_integer_or_infinity(count), 0.0);
            if (!t.buffer_object().shared())
                return number(0.0);
            std::vector<std::shared_ptr<waiter>> notified;
            value_list resolves;    // (the asynchronous waiters' resolving functions (no longer in the list: rooted here))
            auto& list = waiters();
            for (auto i = list.begin(); i != list.end() && static_cast<double>(notified.size()) < c;)
                if ((*i)->buffer == t.buffer && (*i)->byteIndex == byteIndex)
                {
                    notified.push_back(*i);
                    if (!(*i)->sync)
                        resolves.push_back((*i)->resolve);
                    i = list.erase(i);
                }
                else
                    ++i;
            for (auto const& w : notified)
                if (w->sync)
                    w->notified = true;
            for (auto const resolve : resolves)
                resolve_waiter(resolve, "ok");
            return number(static_cast<double>(notified.size()));
        }

        // (DoWait: synchronously the agent (or the main program) blocks while the other agents (test262's) run; with no other agents nothing
        // can notify it: it waits for its timeout)
        value do_wait(arguments aArguments, bool aAsync)
        {
            auto& t = validate_integer_typed_array(argument(aArguments, 0u), true);
            if (!t.buffer_object().shared())
                throw_type_error("Atomics.wait requires a shared typed array");
            auto const byteIndex = validate_atomic_access(t, argument(aArguments, 1u));
            value expected;
            if (t.type == element_type::BigInt64)
                expected = make_bigint(bigint_to_int64(to_bigint(argument(aArguments, 2u))));
            else
                expected = number(static_cast<double>(to_int32(argument(aArguments, 2u))));
            auto const timeoutNumber = to_number(argument(aArguments, 3u));
            double realTimeout;
            if (std::isnan(timeoutNumber) || timeoutNumber == std::numeric_limits<double>::infinity())
                realTimeout = std::numeric_limits<double>::infinity();
            else if (timeoutNumber == -std::numeric_limits<double>::infinity())
                realTimeout = 0.0;
            else
                realTimeout = std::max(timeoutNumber, 0.0);
            auto const witness = element_value(t.type, t.buffer_object().bytes() + byteIndex);
            bool const equal = t.type == element_type::BigInt64 ? bigint_equal(expected, witness) : expected == witness;
            auto const result_object = [](value aAsyncValue, value aValue)
            {
                auto const result = make_object();
                create_data_property_or_throw(result, ascii("async"), aAsyncValue);
                create_data_property_or_throw(result, the_names().value_, aValue);
                return result;
            };
            if (!aAsync)
            {
                if (!equal)
                    return ascii("not-equal");
                if (std::isinf(realTimeout) && !agents_running())
                    throw_type_error("Atomics.wait would wait forever: there is no other agent to notify it");
                auto w = std::make_shared<waiter>(waiter{ t.buffer, byteIndex, Undefined, {}, !std::isinf(realTimeout) });
                w->sync = true;
                waiters().push_back(w);
                std::optional<double> deadline;
                if (w->finite)
                    deadline = monotonic_ms() + std::min(realTimeout, 1e12);
                agent_block([w]() { return w->notified; }, deadline);
                if (w->notified)
                    return ascii("ok");
                auto& list = waiters();
                if (auto const existing = std::find(list.begin(), list.end(), w); existing != list.end())
                    list.erase(existing);
                return ascii("timed-out");
            }
            if (!equal)
                return result_object(False, ascii("not-equal"));
            if (realTimeout == 0.0)
                return result_object(False, ascii("timed-out"));
            value resolve = Undefined;
            value reject = Undefined;
            auto const promise = new_promise_capability(the_realm().promise, resolve, reject);
            auto w = std::make_shared<waiter>(waiter{ t.buffer, byteIndex, resolve, {}, !std::isinf(realTimeout) });
            if (w->finite)
                w->timeoutTime = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double, std::milli>{ std::min(realTimeout, 1e12) });
            waiters().push_back(w);
            if (w->finite)
                enqueue_timeout_job(w);
            return result_object(True, promise);
        }

        value atomics_wait(value, arguments aArguments, value, function_object&)
        {
            return do_wait(aArguments, false);
        }

        value atomics_wait_async(value, arguments aArguments, value, function_object&)
        {
            return do_wait(aArguments, true);
        }

        value atomics_pause(value, arguments aArguments, value, function_object&)
        {
            auto const n = argument(aArguments, 0u);
            if (n != Undefined && !is_integral_number(n))
                throw_type_error("Atomics.pause argument must be undefined or an integral number");
            return Undefined;
        }
    }

    void init_atomics(realm& aRealm)
    {
        auto const atomics = make_object();
        auto const rmw = [&](char const* aName, operation aOperation)
        {
            as_function(method(atomics, aName, 3u, atomics_read_modify_write))->nativeId = aOperation;
        };
        rmw("add", Add);
        rmw("and", And);
        method(atomics, "compareExchange", 4u, atomics_compare_exchange);
        rmw("exchange", Exchange);
        method(atomics, "isLockFree", 1u, atomics_is_lock_free);
        method(atomics, "load", 2u, atomics_load);
        method(atomics, "notify", 3u, atomics_notify);
        rmw("or", Or);
        method(atomics, "pause", 0u, atomics_pause);
        method(atomics, "store", 3u, atomics_store);
        rmw("sub", Sub);
        method(atomics, "wait", 4u, atomics_wait);
        method(atomics, "waitAsync", 4u, atomics_wait_async);
        rmw("xor", Xor);
        obj(atomics).put(aRealm.symToStringTag, ascii("Atomics"), AttributeConfigurable);
        obj(aRealm.global).put(ascii("Atomics"), atomics);
    }
}
