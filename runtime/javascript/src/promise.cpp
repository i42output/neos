/*
  promise.cpp

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

// Promises (ECMAScript's Promise objects, their abstract operations and the job queue)

#include <cstdlib>
#include <deque>
#include "js.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        enum class promise_state : std::uint8_t { Pending, Fulfilled, Rejected };
        enum class reaction_type : std::uint8_t { Fulfill, Reject };

        struct promise_reaction
        {
            value promise = Undefined;      ///< (its capability (all three undefined: none))
            value resolve = Undefined;
            value reject = Undefined;
            reaction_type type = reaction_type::Fulfill;
            value handler = Empty;          ///< (Empty: none)
        };

        class promise_object : public object
        {
        public:
            explicit promise_object(value aPrototype) : object{ aPrototype, object_class::Promise } {}
        public:
            promise_state state = promise_state::Pending;
            value result = Undefined;
            std::vector<promise_reaction> fulfillReactions;
            std::vector<promise_reaction> rejectReactions;
            bool handled = false;
        };

        // (the values that a built-in closure captures (as its function object's internal value))
        class captures : public cell
        {
        public:
            captures() : cell{ cell_type::Internal } {}
        public:
            std::vector<value> values;
            bool flag = false;
            std::uint32_t index = 0u;
            captures* shared = nullptr;     ///< (state shared by several closures (e.g. Promise.all's remaining elements count))
            std::uint32_t count = 0u;
        };

        captures& captured(function_object& aFunction)
        {
            return cell_of<captures>(aFunction.internal);
        }

        value closure(native_function aFunction, std::uint32_t aLength, captures& aCaptures)
        {
            auto const f = make_native(the_names().empty, aLength, aFunction);
            obj(f).internal = value_of(aCaptures);
            return f;
        }

        captures& make_captures(std::initializer_list<value> aValues = {})
        {
            auto& c = the_heap().make<captures>();
            c.values.assign(aValues.begin(), aValues.end());
            return c;
        }

        bool is_promise(value aValue)
        {
            return is_object(aValue) && obj(aValue).cls == object_class::Promise;
        }

        promise_object& as_promise(value aValue)
        {
            return static_cast<promise_object&>(obj(aValue));
        }

        // --- the job queue

        std::deque<std::function<void()>>& jobs()
        {
            static std::deque<std::function<void()>> sJobs;
            return sJobs;
        }

        // (HostPromiseRejectionTracker: the promises rejected that have no handlers (in the order they were rejected))
        std::vector<value>& unhandled_rejections()
        {
            static std::vector<value> sRejections;
            return sRejections;
        }

        // --- the abstract operations

        void trigger_reactions(std::vector<promise_reaction> aReactions, value aArgument);
        void reject_promise(value aPromise, value aReason);

        void fulfill_promise(value aPromise, value aValue)
        {
            auto& p = as_promise(aPromise);
            auto reactions = std::move(p.fulfillReactions);
            p.result = aValue;
            p.fulfillReactions.clear();
            p.rejectReactions.clear();
            p.state = promise_state::Fulfilled;
            trigger_reactions(std::move(reactions), aValue);
        }

        void reject_promise(value aPromise, value aReason)
        {
            auto& p = as_promise(aPromise);
            auto reactions = std::move(p.rejectReactions);
            p.result = aReason;
            p.fulfillReactions.clear();
            p.rejectReactions.clear();
            p.state = promise_state::Rejected;
            if (!p.handled)
                unhandled_rejections().push_back(aPromise);
            trigger_reactions(std::move(reactions), aReason);
        }

        void promise_reaction_job(promise_reaction const& aReaction, value aArgument)
        {
            value result = Undefined;
            bool threw = false;
            if (aReaction.handler == Empty)
            {
                result = aArgument;
                threw = aReaction.type == reaction_type::Reject;
            }
            else
            {
                try
                {
                    value const argument[] = { aArgument };
                    result = call(aReaction.handler, Undefined, argument);
                }
                catch (js_exception const& e)
                {
                    result = e.thrown;
                    threw = true;
                }
            }
            if (aReaction.promise == Undefined && aReaction.resolve == Undefined)
            {
                if (threw)
                    throw js_exception{ result };
                return;
            }
            value const argument[] = { result };
            call(threw ? aReaction.reject : aReaction.resolve, Undefined, argument);
        }

        void trigger_reactions(std::vector<promise_reaction> aReactions, value aArgument)
        {
            for (auto& reaction : aReactions)
                enqueue_job([reaction, aArgument]() { promise_reaction_job(reaction, aArgument); });
        }

        // (the resolving functions: they share their promise and whether it has been resolved)
        value promise_resolve_function(value, arguments aArguments, value, function_object& aSelf)
        {
            auto& c = captured(aSelf);
            auto& shared = *c.shared;
            if (shared.flag)
                return Undefined;
            shared.flag = true;
            auto const promise = shared.values[0];
            auto const resolution = argument(aArguments, 0u);
            if (resolution == promise)
            {
                reject_promise(promise, make_error(the_realm().typeErrorPrototype, ascii("Chaining cycle detected for promise")));
                return Undefined;
            }
            if (!is_object(resolution))
            {
                fulfill_promise(promise, resolution);
                return Undefined;
            }
            value then;
            try
            {
                then = get(resolution, the_names().then);
            }
            catch (js_exception const& e)
            {
                reject_promise(promise, e.thrown);
                return Undefined;
            }
            if (!is_callable(then))
            {
                fulfill_promise(promise, resolution);
                return Undefined;
            }
            // (NewPromiseResolveThenableJob)
            enqueue_job([promise, resolution, then]()
            {
                value resolve, reject;
                create_resolving_functions(promise, resolve, reject);
                try
                {
                    value const arguments[] = { resolve, reject };
                    call(then, resolution, arguments);
                }
                catch (js_exception const& e)
                {
                    value const argument[] = { e.thrown };
                    call(reject, Undefined, argument);
                }
            });
            return Undefined;
        }

        value promise_reject_function(value, arguments aArguments, value, function_object& aSelf)
        {
            auto& c = captured(aSelf);
            auto& shared = *c.shared;
            if (shared.flag)
                return Undefined;
            shared.flag = true;
            reject_promise(shared.values[0], argument(aArguments, 0u));
            return Undefined;
        }

        // (GetCapabilitiesExecutor)
        value capabilities_executor(value, arguments aArguments, value, function_object& aSelf)
        {
            auto& c = captured(aSelf);
            if (c.values[0] != Undefined)
                throw_type_error("Promise executor has already been invoked with a resolve function");
            if (c.values[1] != Undefined)
                throw_type_error("Promise executor has already been invoked with a reject function");
            c.values[0] = argument(aArguments, 0u);
            c.values[1] = argument(aArguments, 1u);
            return Undefined;
        }

        struct capability
        {
            value promise = Undefined;
            value resolve = Undefined;
            value reject = Undefined;
        };

        capability new_capability(value aConstructor)
        {
            capability result;
            result.promise = new_promise_capability(aConstructor, result.resolve, result.reject);
            return result;
        }

        // (IfAbruptRejectPromise: the capability's promise, rejected with the exception)
        value reject_with(capability const& aCapability, value aReason)
        {
            value const argument[] = { aReason };
            call(aCapability.reject, Undefined, argument);
            return aCapability.promise;
        }

        value make_promise(value aPrototype)
        {
            return value_of(the_heap().make<promise_object>(aPrototype));
        }

        // --- the Promise constructor and its functions

        value promise_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            if (aNewTarget == Undefined)
                throw_type_error("Promise constructor cannot be invoked without 'new'");
            auto const executor = argument(aArguments, 0u);
            if (!is_callable(executor))
                throw_type_error("Promise resolver " + describe(executor) + " is not a function");
            auto const promise = make_promise(get_prototype_from_constructor(aNewTarget, &realm::promisePrototype));
            value resolve, reject;
            create_resolving_functions(promise, resolve, reject);
            try
            {
                value const arguments[] = { resolve, reject };
                call(executor, Undefined, arguments);
            }
            catch (js_exception const& e)
            {
                value const argument[] = { e.thrown };
                call(reject, Undefined, argument);
            }
            return promise;
        }

        value get_promise_resolve(value aConstructor)
        {
            auto const resolve = get(aConstructor, ascii("resolve"));
            if (!is_callable(resolve))
                throw_type_error("Promise resolve is not a function");
            return resolve;
        }

        // (the combinators: aPerform iterates, given the iterator, the constructor, the capability and the constructor's resolve)
        template <typename Perform>
        value combinator(value aThis, arguments aArguments, Perform&& aPerform)
        {
            auto const c = aThis;
            auto const cap = new_capability(c);
            value promiseResolve;
            try
            {
                promiseResolve = get_promise_resolve(c);
            }
            catch (js_exception const& e)
            {
                return reject_with(cap, e.thrown);
            }
            iterator_record record;
            try
            {
                record = get_iterator(argument(aArguments, 0u));
            }
            catch (js_exception const& e)
            {
                return reject_with(cap, e.thrown);
            }
            try
            {
                return aPerform(record, c, cap, promiseResolve);
            }
            catch (js_exception const& e)
            {
                if (!record.done)
                    iterator_close_on_throw(record);
                return reject_with(cap, e.thrown);
            }
        }

        // (the state Promise.all, allSettled and any share between their element functions: values (or errors), the remaining count,
        // the capability)
        captures& combinator_state(capability const& aCapability)
        {
            auto& state = make_captures({ aCapability.promise, aCapability.resolve, aCapability.reject });
            state.count = 1u;
            return state;
        }

        value values_array(captures& aState)
        {
            return create_array_from_list(std::span<value const>{ aState.values.data() + 3, aState.values.size() - 3u });
        }

        value all_resolve_element(value, arguments aArguments, value, function_object& aSelf)
        {
            auto& c = captured(aSelf);
            if (c.flag)
                return Undefined;
            c.flag = true;
            auto& state = *c.shared;
            state.values[3u + c.index] = argument(aArguments, 0u);
            if (--state.count == 0u)
            {
                value const argument[] = { values_array(state) };
                return call(state.values[1], Undefined, argument);
            }
            return Undefined;
        }

        value settled_element(value, arguments aArguments, value, function_object& aSelf)
        {
            // (allSettled: the element's resolve and reject functions share whether one has been called (the flag of c.values[0]'s
            // captures))
            auto& c = captured(aSelf);
            auto& called = cell_of<captures>(c.values[0]);
            if (called.flag)
                return Undefined;
            called.flag = true;
            auto& state = *c.shared;
            auto const o = make_object();
            if (c.count == 0u)
            {
                create_data_property_or_throw(o, ascii("status"), ascii("fulfilled"));
                create_data_property_or_throw(o, the_names().value_, argument(aArguments, 0u));
            }
            else
            {
                create_data_property_or_throw(o, ascii("status"), ascii("rejected"));
                create_data_property_or_throw(o, ascii("reason"), argument(aArguments, 0u));
            }
            state.values[3u + c.index] = o;
            if (--state.count == 0u)
            {
                value const argument[] = { values_array(state) };
                return call(state.values[1], Undefined, argument);
            }
            return Undefined;
        }

        value aggregate_error(captures& aState)
        {
            auto const error = make_error(the_realm().aggregateErrorPrototype, ascii("All promises were rejected"));
            property_descriptor d;
            d.valueField = values_array(aState);
            d.writable = true;
            d.enumerable = false;
            d.configurable = true;
            define_property_or_throw(error, the_names().errors, d);
            return error;
        }

        value any_reject_element(value, arguments aArguments, value, function_object& aSelf)
        {
            auto& c = captured(aSelf);
            if (c.flag)
                return Undefined;
            c.flag = true;
            auto& state = *c.shared;
            state.values[3u + c.index] = argument(aArguments, 0u);
            if (--state.count == 0u)
            {
                value const argument[] = { aggregate_error(state) };
                return call(state.values[2], Undefined, argument);
            }
            return Undefined;
        }

        value promise_all(value aThis, arguments aArguments, value, function_object&)
        {
            return combinator(aThis, aArguments, [](iterator_record& aRecord, value aC, capability const& aCap, value aResolve) -> value
            {
                auto& state = combinator_state(aCap);
                std::uint32_t index = 0u;
                for (;;)
                {
                    auto const next = iterator_step_value(aRecord);
                    if (!next)
                    {
                        if (--state.count == 0u)
                        {
                            value const argument[] = { values_array(state) };
                            call(aCap.resolve, Undefined, argument);
                        }
                        return aCap.promise;
                    }
                    state.values.push_back(Undefined);
                    value const argument[] = { *next };
                    auto const nextPromise = call(aResolve, aC, argument);
                    auto& element = make_captures();
                    element.shared = &state;
                    element.index = index;
                    auto const onFulfilled = closure(all_resolve_element, 1u, element);
                    ++state.count;
                    value const handlers[] = { onFulfilled, aCap.reject };
                    invoke(nextPromise, the_names().then, handlers);
                    ++index;
                }
            });
        }

        value promise_all_settled(value aThis, arguments aArguments, value, function_object&)
        {
            return combinator(aThis, aArguments, [](iterator_record& aRecord, value aC, capability const& aCap, value aResolve) -> value
            {
                auto& state = combinator_state(aCap);
                std::uint32_t index = 0u;
                for (;;)
                {
                    auto const next = iterator_step_value(aRecord);
                    if (!next)
                    {
                        if (--state.count == 0u)
                        {
                            value const argument[] = { values_array(state) };
                            call(aCap.resolve, Undefined, argument);
                        }
                        return aCap.promise;
                    }
                    state.values.push_back(Undefined);
                    value const argument[] = { *next };
                    auto const nextPromise = call(aResolve, aC, argument);
                    auto& called = make_captures();
                    auto& fulfilled = make_captures({ value_of(called) });
                    fulfilled.shared = &state;
                    fulfilled.index = index;
                    fulfilled.count = 0u;
                    auto& rejected = make_captures({ value_of(called) });
                    rejected.shared = &state;
                    rejected.index = index;
                    rejected.count = 1u;
                    auto const onFulfilled = closure(settled_element, 1u, fulfilled);
                    auto const onRejected = closure(settled_element, 1u, rejected);
                    ++state.count;
                    value const handlers[] = { onFulfilled, onRejected };
                    invoke(nextPromise, the_names().then, handlers);
                    ++index;
                }
            });
        }

        value promise_any(value aThis, arguments aArguments, value, function_object&)
        {
            return combinator(aThis, aArguments, [](iterator_record& aRecord, value aC, capability const& aCap, value aResolve) -> value
            {
                auto& state = combinator_state(aCap);
                std::uint32_t index = 0u;
                for (;;)
                {
                    auto const next = iterator_step_value(aRecord);
                    if (!next)
                    {
                        if (--state.count == 0u)
                        {
                            value const argument[] = { aggregate_error(state) };
                            call(aCap.reject, Undefined, argument);
                        }
                        return aCap.promise;
                    }
                    state.values.push_back(Undefined);
                    value const argument[] = { *next };
                    auto const nextPromise = call(aResolve, aC, argument);
                    auto& element = make_captures();
                    element.shared = &state;
                    element.index = index;
                    auto const onRejected = closure(any_reject_element, 1u, element);
                    ++state.count;
                    value const handlers[] = { aCap.resolve, onRejected };
                    invoke(nextPromise, the_names().then, handlers);
                    ++index;
                }
            });
        }

        value promise_race(value aThis, arguments aArguments, value, function_object&)
        {
            return combinator(aThis, aArguments, [](iterator_record& aRecord, value aC, capability const& aCap, value aResolve) -> value
            {
                for (;;)
                {
                    auto const next = iterator_step_value(aRecord);
                    if (!next)
                        return aCap.promise;
                    value const argument[] = { *next };
                    auto const nextPromise = call(aResolve, aC, argument);
                    value const handlers[] = { aCap.resolve, aCap.reject };
                    invoke(nextPromise, the_names().then, handlers);
                }
            });
        }

        value promise_reject(value aThis, arguments aArguments, value, function_object&)
        {
            auto const cap = new_capability(aThis);
            value const argument[] = { javascript_runtime::argument(aArguments, 0u) };
            call(cap.reject, Undefined, argument);
            return cap.promise;
        }

        value promise_resolve_static(value aThis, arguments aArguments, value, function_object&)
        {
            if (!is_object(aThis))
                throw_type_error("Promise.resolve called on non-object");
            return promise_resolve(aThis, argument(aArguments, 0u));
        }

        value promise_try(value aThis, arguments aArguments, value, function_object&)
        {
            if (!is_object(aThis))
                throw_type_error("Promise.try called on non-object");
            auto const cap = new_capability(aThis);
            try
            {
                auto const result = call(argument(aArguments, 0u), Undefined, aArguments.size() > 1u ? aArguments.subspan(1u) : arguments{});
                value const argument[] = { result };
                call(cap.resolve, Undefined, argument);
            }
            catch (js_exception const& e)
            {
                value const argument[] = { e.thrown };
                call(cap.reject, Undefined, argument);
            }
            return cap.promise;
        }

        value promise_with_resolvers(value aThis, arguments, value, function_object&)
        {
            auto const cap = new_capability(aThis);
            auto const o = make_object();
            create_data_property_or_throw(o, ascii("promise"), cap.promise);
            create_data_property_or_throw(o, ascii("resolve"), cap.resolve);
            create_data_property_or_throw(o, ascii("reject"), cap.reject);
            return o;
        }

        value promise_species(value aThis, arguments, value, function_object&)
        {
            return aThis;
        }

        // --- Promise.prototype

        value promise_then(value aThis, arguments aArguments, value, function_object&)
        {
            if (!is_promise(aThis))
                throw_type_error("Promise.prototype.then called on incompatible receiver " + describe(aThis));
            auto const c = species_constructor(aThis, the_realm().promise);
            auto const cap = new_capability(c);
            perform_promise_then(aThis, argument(aArguments, 0u), argument(aArguments, 1u), cap.promise, cap.resolve, cap.reject);
            return cap.promise;
        }

        value promise_catch(value aThis, arguments aArguments, value, function_object&)
        {
            value const handlers[] = { Undefined, argument(aArguments, 0u) };
            return invoke(aThis, the_names().then, handlers);
        }

        value then_finally_value(value, arguments, value, function_object& aSelf)
        {
            return captured(aSelf).values[0];
        }

        value then_finally_throw(value, arguments, value, function_object& aSelf)
        {
            throw js_exception{ captured(aSelf).values[0] };
        }

        value then_finally(value, arguments aArguments, value, function_object& aSelf)
        {
            // (values: the constructor, onFinally; flag: the rejected case)
            auto& c = captured(aSelf);
            auto const result = call(c.values[1], Undefined);
            auto const p = promise_resolve(c.values[0], result);
            auto& thrown = make_captures({ argument(aArguments, 0u) });
            auto const f = closure(c.flag ? then_finally_throw : then_finally_value, 0u, thrown);
            value const handler[] = { f };
            return invoke(p, the_names().then, handler);
        }

        value promise_finally(value aThis, arguments aArguments, value, function_object&)
        {
            if (!is_object(aThis))
                throw_type_error("Promise.prototype.finally called on non-object");
            auto const c = species_constructor(aThis, the_realm().promise);
            auto const onFinally = argument(aArguments, 0u);
            value thenFinally = onFinally;
            value catchFinally = onFinally;
            if (is_callable(onFinally))
            {
                auto& fulfilled = make_captures({ c, onFinally });
                thenFinally = closure(then_finally, 1u, fulfilled);
                auto& rejected = make_captures({ c, onFinally });
                rejected.flag = true;
                catchFinally = closure(then_finally, 1u, rejected);
            }
            value const handlers[] = { thenFinally, catchFinally };
            return invoke(aThis, the_names().then, handlers);
        }
    }

    // --- the interface

    void create_resolving_functions(value aPromise, value& aResolve, value& aReject)
    {
        auto& shared = make_captures({ aPromise });
        auto& resolveCaptures = make_captures();
        resolveCaptures.shared = &shared;
        auto& rejectCaptures = make_captures();
        rejectCaptures.shared = &shared;
        aResolve = closure(promise_resolve_function, 1u, resolveCaptures);
        aReject = closure(promise_reject_function, 1u, rejectCaptures);
    }

    value new_promise_capability(value aConstructor, value& aResolve, value& aReject)
    {
        if (!is_constructor(aConstructor))
            throw_type_error(describe(aConstructor) + " is not a constructor");
        if (aConstructor == the_realm().promise)
        {
            // (the intrinsic %Promise%: no observable difference)
            auto const promise = make_promise(the_realm().promisePrototype);
            create_resolving_functions(promise, aResolve, aReject);
            return promise;
        }
        auto& c = make_captures({ Undefined, Undefined });
        auto const executor = closure(capabilities_executor, 2u, c);
        value const argument[] = { executor };
        auto const promise = construct(aConstructor, argument);
        if (!is_callable(c.values[0]))
            throw_type_error("Promise resolve function is not callable");
        if (!is_callable(c.values[1]))
            throw_type_error("Promise reject function is not callable");
        aResolve = c.values[0];
        aReject = c.values[1];
        return promise;
    }

    value promise_resolve(value aConstructor, value aValue)
    {
        if (is_promise(aValue))
        {
            auto const constructor = get(aValue, the_names().constructor);
            if (constructor == aConstructor)
                return aValue;
        }
        value resolve, reject;
        auto const promise = new_promise_capability(aConstructor, resolve, reject);
        value const argument[] = { aValue };
        call(resolve, Undefined, argument);
        return promise;
    }

    value promise_rejected(value aReason)
    {
        auto const promise = make_promise(the_realm().promisePrototype);
        reject_promise(promise, aReason);
        return promise;
    }

    void perform_promise_then(value aPromise, value aOnFulfilled, value aOnRejected, value aResultPromise, value aResolve, value aReject)
    {
        auto& p = as_promise(aPromise);
        promise_reaction fulfill{ aResultPromise, aResolve, aReject, reaction_type::Fulfill, is_callable(aOnFulfilled) ? aOnFulfilled : Empty };
        promise_reaction reject{ aResultPromise, aResolve, aReject, reaction_type::Reject, is_callable(aOnRejected) ? aOnRejected : Empty };
        switch (p.state)
        {
        case promise_state::Pending:
            p.fulfillReactions.push_back(fulfill);
            p.rejectReactions.push_back(reject);
            break;
        case promise_state::Fulfilled:
            {
                auto const result = p.result;
                enqueue_job([fulfill, result]() { promise_reaction_job(fulfill, result); });
            }
            break;
        case promise_state::Rejected:
            {
                if (!p.handled)
                    std::erase(unhandled_rejections(), aPromise);
                auto const result = p.result;
                enqueue_job([reject, result]() { promise_reaction_job(reject, result); });
            }
            break;
        }
        p.handled = true;
    }

    void enqueue_job(value aJob, value aArgument)
    {
        enqueue_job([aJob, aArgument]()
        {
            value const argument[] = { aArgument };
            call(aJob, Undefined, argument);
        });
    }

    void enqueue_job(std::function<void()> aJob)
    {
        jobs().push_back(std::move(aJob));
    }

    bool run_jobs()
    {
        // (an exception that a job does not handle is reported (false); the jobs that follow it still run)
        bool result = true;
        auto& queue = jobs();
        while (!queue.empty())
        {
            auto job = std::move(queue.front());
            queue.pop_front();
            try
            {
                job();
            }
            catch (js_exception const& e)
            {
                report_uncaught(e.thrown);
                result = false;
            }
        }
        // (a promise rejected that no handler handled: reported as an uncaught exception (except by the test262 host, whose tests leave
        // rejected promises))
        static bool const sTest262 = std::getenv("NEOS_TEST262") != nullptr;
        auto& rejections = unhandled_rejections();
        if (!rejections.empty() && result && !sTest262)
        {
            value reason;
            promise_state_of(rejections.front(), reason);
            report_uncaught(reason, true);
            result = false;
        }
        rejections.clear();
        return result;
    }

    void init_promise(realm& aRealm)
    {
        auto const c = make_native("Promise", 1u, promise_constructor, true);
        aRealm.promise = c;
        auto const p = make_object(aRealm.objectPrototype);
        aRealm.promisePrototype = p;
        obj(c).put(the_names().prototype, p, 0u);
        obj(p).put(the_names().constructor, c);
        method(c, "all", 1u, promise_all);
        method(c, "allSettled", 1u, promise_all_settled);
        method(c, "any", 1u, promise_any);
        method(c, "race", 1u, promise_race);
        method(c, "reject", 1u, promise_reject);
        method(c, "resolve", 1u, promise_resolve_static);
        method(c, "try", 1u, promise_try);
        method(c, "withResolvers", 0u, promise_with_resolvers);
        getter(c, aRealm.symSpecies, promise_species);
        method(p, "catch", 1u, promise_catch);
        method(p, "finally", 1u, promise_finally);
        method(p, "then", 2u, promise_then);
        obj(p).put(aRealm.symToStringTag, ascii("Promise"), AttributeConfigurable);
        obj(aRealm.global).put(ascii("Promise"), c);
    }

    // (for the inspector: a promise's state (0 pending, 1 fulfilled, 2 rejected) and result)
    int promise_state_of(value aPromise, value& aResult)
    {
        auto& p = as_promise(aPromise);
        aResult = p.result;
        return static_cast<int>(p.state);
    }
}
