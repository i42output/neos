/*
  generators.cpp

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

// Generators, async functions and async generators
//
// A generator function (or async function) is compiled as two functions: the function itself, which binds its parameters and
// then calls start_function (javascript::start) with its environment, and its body (the "body" function), which start_function's
// generator (or async function's state) runs on a coroutine of its own (neos_rt_host::coroutine_new) when it is resumed. yield
// (javascript::yield) and await (javascript::await, and the runtime's own awaits, e.g. for await's) suspend the coroutine: the
// code that resumed it continues. A generator is resumed by next, throw and return; an async function (or async generator) by
// the reaction jobs of the promise it awaits.
//
// A suspension's resumption is a completion: normal (the value sent in is the yield's (or await's) value), throw (the value is
// thrown where it suspended) or return (javascript::yield returns ReturnCompletion: the compiled code then returns resume_value()
// through its finally blocks).

#include <deque>
#include "js.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        enum class generator_kind : std::uint8_t { Generator, AsyncFunction, AsyncGenerator };
        enum class generator_state : std::uint8_t { SuspendedStart, SuspendedYield, Executing, DrainingQueue, Completed };
        enum completion_type : int { Normal = 0, Throw = 1, Return = 2 };

        struct completion
        {
            int type = Normal;
            value v = Undefined;
        };

        struct request_record
        {
            completion c;
            value promise = Undefined;
            value resolve = Undefined;
            value reject = Undefined;
        };

        class generator_object : public object
        {
        public:
            generator_object(value aPrototype, generator_kind aKind) :
                object{ aPrototype, aKind == generator_kind::Generator ? object_class::Generator : aKind == generator_kind::AsyncGenerator ? object_class::AsyncGenerator : object_class::Ordinary },
                kind{ aKind }
            {
            }
            ~generator_object()
            {
                // (a generator that cannot be resumed again (the garbage collector frees it): its coroutine (and stack) are discarded)
                if (coroutine != nullptr)
                    host()->coroutine_delete(coroutine);
            }
        public:
            void trace(tracer& aTracer) override
            {
                object::trace(aTracer);
                aTracer.mark(callee);
                aTracer.mark(thisValue);
                aTracer.mark(environment);
                aTracer.mark(savedArguments);
                aTracer.mark(nativeValues);
                aTracer.mark(resumption.v);
                aTracer.mark(yielded);
                aTracer.mark(result);
                aTracer.mark(returnValue);
                aTracer.mark(promise);
                aTracer.mark(resolve);
                aTracer.mark(reject);
                for (auto const& r : queue)
                {
                    aTracer.mark(r.c.v);
                    aTracer.mark(r.promise);
                    aTracer.mark(r.resolve);
                    aTracer.mark(r.reject);
                }
                // (a suspended body: its stack (and the interpreter's frames, if it is interpreted))
                if (suspended != nullptr)
                    aTracer.suspended_stack(suspended, &link, coroutine);
            }
        public:
            generator_kind kind;
            generator_state state = generator_state::SuspendedStart;
            // (its body: a function of the program with the generator function's environment)
            value callee = Undefined;
            value thisValue = Undefined;
            value environment = Undefined;
            std::uint32_t body = 0u;
            void* program = nullptr;
            neos_rt_coroutine* coroutine = nullptr;
            std::vector<value> savedArguments;  ///< (its part of the extra arguments stack, while it is suspended)
            std::function<value()> native;      ///< (a built-in's body (an async built-in), instead of a compiled function)
            std::vector<value> nativeValues;    ///< (the values that native captures)
            // (its stack: the base of its coroutine's stack (once it has run), the segments of its stack while it is suspended (the
            // innermost first, ending with link), and link (whose previous is the segment of the code that resumed it, while it runs))
            void const* stackBase = nullptr;
            stack_segment* suspended = nullptr;
            stack_segment link;
            // (resumption: how it is resumed)
            completion resumption;
            // (suspension: the value a generator yielded; raw: an iterator result (yield* passes its iterator's results on))
            value yielded = Undefined;
            bool raw = false;
            // (the body's completion)
            bool threw = false;
            value result = Undefined;
            value returnValue = Undefined;      ///< (resume_value)
            // (an async function's promise)
            value promise = Undefined;
            value resolve = Undefined;
            value reject = Undefined;
            // (an async generator's requests)
            std::deque<request_record> queue;
        };

        thread_local generator_object* tCurrent = nullptr;

        root_tracer const sGeneratorRoots{ [](tracer& aTracer)
        {
            aTracer.mark(tCurrent);
        } };

        generator_object& as_generator(value aValue)
        {
            return static_cast<generator_object&>(obj(aValue));
        }

        // --- coroutines

#if defined(_MSC_VER)
        __declspec(noinline)
#else
        [[gnu::noinline]]
#endif
        void body_run(generator_object& g)
        {
            try
            {
                if (g.native)
                    g.result = g.native();
                else
                {
                    value const argument[] = { g.environment };
                    g.result = call_compiled_index(g.program, g.body, g.callee, g.thisValue, argument, Undefined);
                }
                g.threw = false;
            }
            catch (js_exception const& e)
            {
                g.result = e.thrown;
                g.threw = true;
            }
            catch (std::bad_alloc const&)
            {
                g.result = Undefined;
                try
                {
                    g.result = make_error(the_realm().rangeErrorPrototype, ascii("Out of memory"));
                }
                catch (...)
                {
                }
                g.threw = true;
            }
            catch (...)
            {
                g.result = Undefined;
                g.threw = true;
            }
        }

        void body_entry(void* aGenerator)
        {
            // (the base of the coroutine's stack (for the garbage collector): body_run's frames are below it)
            auto& g = *static_cast<generator_object*>(aGenerator);
            char volatile base = 0;
            g.stackBase = const_cast<char const*>(&base);
            set_stack_base(g.stackBase);
            body_run(g);
        }

        // (resume (or start) its body: true if the body finished)
        bool run(generator_object& g)
        {
            auto const& h = *host();
            if (g.coroutine == nullptr)
            {
                g.coroutine = h.coroutine_new(&body_entry, &g);
                if (g.coroutine == nullptr)
                    throw_range_error("cannot create a coroutine for a generator or async function");
            }
            auto& extra = extra_arguments();
            auto const base = extra.size();
            extra.insert(extra.end(), g.savedArguments.begin(), g.savedArguments.end());
            g.savedArguments.clear();
            auto* const previous = tCurrent;
            tCurrent = &g;
            // (while the coroutine runs, the live part of this stack is a segment (its values (and the callers' in registers) are roots);
            // the coroutine's segments follow its own (link))
            NEOS_JS_SPILL_REGISTERS();
            stack_segment resumer{ stack_pointer(), stack_base(), stack_segments() };
            g.link.previous = &resumer;
            stack_segments() = g.suspended != nullptr ? g.suspended : &g.link;
            if (g.stackBase != nullptr)
                set_stack_base(g.stackBase);
            int const finished = h.coroutine_resume(g.coroutine);
            set_stack_base(resumer.high);
            stack_segments() = resumer.previous;
            g.link.previous = nullptr;
            tCurrent = previous;
            if (finished != 0)
            {
                h.coroutine_delete(g.coroutine);
                g.coroutine = nullptr;
            }
            else
                g.savedArguments.assign(extra.begin() + static_cast<std::ptrdiff_t>(base), extra.end());
            extra.resize(base);
            return finished != 0;
        }

        // (suspend the running body: its resumption follows)
        completion suspend(generator_object& g)
        {
            // (while it is suspended, the live part of its stack is a segment, which the generator's trace scans)
            NEOS_JS_SPILL_REGISTERS();
            stack_segment here{ stack_pointer(), stack_base(), stack_segments() };
            g.suspended = &here;
            host()->coroutine_suspend();
            g.suspended = nullptr;
            stack_segments() = here.previous;
            return g.resumption;
        }

        generator_object& current(char const* aWhat)
        {
            if (tCurrent == nullptr)
                throw_syntax_error(std::string{ aWhat } + " is only valid in generators and async functions");
            return *tCurrent;
        }

        // --- async functions and async generators

        void async_generator_drain_queue(generator_object& g);

        void async_generator_complete_step(generator_object& g, completion const& aCompletion, bool aDone)
        {
            auto next = g.queue.front();
            g.queue.pop_front();
            if (aCompletion.type == Throw)
            {
                value const argument[] = { aCompletion.v };
                call(next.reject, Undefined, argument);
            }
            else
            {
                value const argument[] = { create_iter_result_object(aCompletion.v, aDone) };
                call(next.resolve, Undefined, argument);
            }
        }

        value async_generator_return_fulfilled(value, arguments aArguments, value, function_object& aSelf)
        {
            auto& g = as_generator(aSelf.internal);
            async_generator_complete_step(g, completion{ Normal, argument(aArguments, 0u) }, true);
            async_generator_drain_queue(g);
            return Undefined;
        }

        value async_generator_return_rejected(value, arguments aArguments, value, function_object& aSelf)
        {
            auto& g = as_generator(aSelf.internal);
            async_generator_complete_step(g, completion{ Throw, argument(aArguments, 0u) }, true);
            async_generator_drain_queue(g);
            return Undefined;
        }

        value native_closure(native_function aFunction, generator_object& g)
        {
            auto const f = make_native(the_names().empty, 1u, aFunction);
            obj(f).internal = value_of(g);
            return f;
        }

        void async_generator_await_return(generator_object& g)
        {
            auto const& next = g.queue.front();
            value promise;
            try
            {
                promise = promise_resolve(the_realm().promise, next.c.v);
            }
            catch (js_exception const& e)
            {
                async_generator_complete_step(g, completion{ Throw, e.thrown }, true);
                async_generator_drain_queue(g);
                return;
            }
            perform_promise_then(promise, native_closure(async_generator_return_fulfilled, g), native_closure(async_generator_return_rejected, g));
        }

        void async_generator_drain_queue(generator_object& g)
        {
            while (!g.queue.empty())
            {
                auto c = g.queue.front().c;
                if (c.type == Return)
                {
                    async_generator_await_return(g);
                    return;
                }
                if (c.type == Normal)
                    c.v = Undefined;
                async_generator_complete_step(g, c, true);
            }
            g.state = generator_state::Completed;
        }

        // (its body ran (and suspended or finished))
        void ran(generator_object& g, bool aFinished)
        {
            if (!aFinished)
                return;
            auto const result = g.result;
            g.result = Undefined;
            if (g.kind == generator_kind::AsyncFunction)
            {
                g.state = generator_state::Completed;
                value const argument[] = { result };
                call(g.threw ? g.reject : g.resolve, Undefined, argument);
            }
            else if (g.kind == generator_kind::AsyncGenerator)
            {
                g.state = generator_state::DrainingQueue;
                async_generator_complete_step(g, completion{ g.threw ? Throw : Normal, result }, true);
                async_generator_drain_queue(g);
            }
        }

        void async_generator_resume(generator_object& g, completion const& aCompletion)
        {
            g.state = generator_state::Executing;
            g.resumption = aCompletion;
            ran(g, run(g));
        }

        value await_fulfilled(value, arguments aArguments, value, function_object& aSelf)
        {
            auto& g = as_generator(aSelf.internal);
            g.resumption = completion{ Normal, argument(aArguments, 0u) };
            ran(g, run(g));
            return Undefined;
        }

        value await_rejected(value, arguments aArguments, value, function_object& aSelf)
        {
            auto& g = as_generator(aSelf.internal);
            g.resumption = completion{ Throw, argument(aArguments, 0u) };
            ran(g, run(g));
            return Undefined;
        }

        // (AsyncGeneratorUnwrapYieldResumption, as a completion)
        completion async_generator_unwrap(completion const& aResumption)
        {
            if (aResumption.type != Return)
                return aResumption;
            try
            {
                return completion{ Return, await_value(aResumption.v) };
            }
            catch (js_exception const& e)
            {
                return completion{ Throw, e.thrown };
            }
        }

        // (AsyncGeneratorYield, as a completion)
        completion async_generator_yield(generator_object& g, value aValue)
        {
            async_generator_complete_step(g, completion{ Normal, aValue }, false);
            if (!g.queue.empty())
                return async_generator_unwrap(g.queue.front().c);
            g.state = generator_state::SuspendedYield;
            return async_generator_unwrap(suspend(g));
        }

        // (GeneratorYield (a sync generator's): its resumption)
        completion generator_yield(generator_object& g, value aValue, bool aRaw)
        {
            g.yielded = aValue;
            g.raw = aRaw;
            return suspend(g);
        }

        // (a resumption as yield's result: a throw is thrown; a return returns ReturnCompletion (the value is resume_value's))
        value resumed(generator_object& g, completion const& aResumption)
        {
            switch (aResumption.type)
            {
            case Throw:
                throw js_exception{ aResumption.v };
            case Return:
                g.returnValue = aResumption.v;
                return ReturnCompletion;
            default:
                return aResumption.v;
            }
        }

        // --- %GeneratorPrototype% and %AsyncGeneratorPrototype%

        generator_object& validate_generator(value aGenerator)
        {
            if (!is_object(aGenerator) || obj(aGenerator).cls != object_class::Generator)
                throw_type_error("next method called on incompatible receiver " + describe(aGenerator));
            return as_generator(aGenerator);
        }

        value generator_next(value aThis, arguments aArguments, value, function_object&)
        {
            return generator_resume(aThis, argument(aArguments, 0u), Normal);
        }

        value generator_return(value aThis, arguments aArguments, value, function_object&)
        {
            return generator_resume(aThis, argument(aArguments, 0u), Return);
        }

        value generator_throw(value aThis, arguments aArguments, value, function_object&)
        {
            return generator_resume(aThis, argument(aArguments, 0u), Throw);
        }

        // (an async generator's method: its promise, for a request of the type aType)
        value async_generator_request(value aThis, value aValue, int aType)
        {
            value resolve, reject;
            auto const promise = new_promise_capability(the_realm().promise, resolve, reject);
            if (!is_object(aThis) || obj(aThis).cls != object_class::AsyncGenerator)
            {
                value const argument[] = { make_error(the_realm().typeErrorPrototype, ascii("AsyncGenerator method called on incompatible receiver")) };
                call(reject, Undefined, argument);
                return promise;
            }
            auto& g = as_generator(aThis);
            auto state = g.state;
            if (aType == Normal && state == generator_state::Completed)
            {
                value const argument[] = { create_iter_result_object(Undefined, true) };
                call(resolve, Undefined, argument);
                return promise;
            }
            if (aType == Throw)
            {
                if (state == generator_state::SuspendedStart)
                {
                    g.state = generator_state::Completed;
                    state = generator_state::Completed;
                }
                if (state == generator_state::Completed)
                {
                    value const argument[] = { aValue };
                    call(reject, Undefined, argument);
                    return promise;
                }
            }
            completion const c{ aType, aValue };
            g.queue.push_back(request_record{ c, promise, resolve, reject });
            if (aType == Return && (state == generator_state::SuspendedStart || state == generator_state::Completed))
            {
                g.state = generator_state::DrainingQueue;
                async_generator_await_return(g);
            }
            else if (state == generator_state::SuspendedYield || (aType == Normal && state == generator_state::SuspendedStart))
                async_generator_resume(g, c);
            return promise;
        }

        value async_generator_next(value aThis, arguments aArguments, value, function_object&)
        {
            return async_generator_request(aThis, argument(aArguments, 0u), Normal);
        }

        value async_generator_return(value aThis, arguments aArguments, value, function_object&)
        {
            return async_generator_request(aThis, argument(aArguments, 0u), Return);
        }

        value async_generator_throw(value aThis, arguments aArguments, value, function_object&)
        {
            return async_generator_request(aThis, argument(aArguments, 0u), Throw);
        }

        // --- %AsyncFromSyncIteratorPrototype%

        class async_from_sync_iterator : public object
        {
        public:
            explicit async_from_sync_iterator(value aPrototype) : object{ aPrototype, object_class::AsyncFromSyncIterator } {}
        public:
            void trace(tracer& aTracer) override
            {
                object::trace(aTracer);
                aTracer.mark(sync);
            }
        public:
            iterator_record sync;
        };

        value async_from_sync_fulfilled(value, arguments aArguments, value, function_object& aSelf)
        {
            // (internal: an object whose internal value is done (a boolean))
            return create_iter_result_object(argument(aArguments, 0u), obj(aSelf.internal).internal == True);
        }

        value async_from_sync_rejected(value, arguments aArguments, value, function_object& aSelf)
        {
            // (internal: the async-from-sync iterator: its sync iterator is closed and the error rethrown)
            auto& o = static_cast<async_from_sync_iterator&>(obj(aSelf.internal));
            iterator_close_on_throw(o.sync);
            throw js_exception{ argument(aArguments, 0u) };
        }

        value async_from_sync_continuation(value aResult, value aPromise, value aResolve, value aReject, async_from_sync_iterator& aIterator, bool aCloseOnRejection)
        {
            auto const rejectWith = [&](value aReason)
            {
                value const argument[] = { aReason };
                call(aReject, Undefined, argument);
                return aPromise;
            };
            bool done;
            value v;
            try
            {
                done = iterator_complete(aResult);
                v = iterator_value(aResult);
            }
            catch (js_exception const& e)
            {
                return rejectWith(e.thrown);
            }
            value wrapper;
            try
            {
                wrapper = promise_resolve(the_realm().promise, v);
            }
            catch (js_exception const& e)
            {
                if (!done && aCloseOnRejection)
                    iterator_close_on_throw(aIterator.sync);
                return rejectWith(e.thrown);
            }
            auto const doneObject = make_object(Null);
            obj(doneObject).internal = boolean(done);
            auto const onFulfilled = make_native(the_names().empty, 1u, async_from_sync_fulfilled);
            obj(onFulfilled).internal = doneObject;
            value onRejected = Undefined;
            if (!done && aCloseOnRejection)
            {
                onRejected = make_native(the_names().empty, 1u, async_from_sync_rejected);
                obj(onRejected).internal = value_of(aIterator);
            }
            perform_promise_then(wrapper, onFulfilled, onRejected, aPromise, aResolve, aReject);
            return aPromise;
        }

        value async_from_sync_next(value aThis, arguments aArguments, value, function_object&)
        {
            auto& o = static_cast<async_from_sync_iterator&>(obj(aThis));
            value resolve, reject;
            auto const promise = new_promise_capability(the_realm().promise, resolve, reject);
            value result;
            try
            {
                result = aArguments.empty() ? iterator_next(o.sync) : iterator_next(o.sync, aArguments[0]);
            }
            catch (js_exception const& e)
            {
                value const argument[] = { e.thrown };
                call(reject, Undefined, argument);
                return promise;
            }
            return async_from_sync_continuation(result, promise, resolve, reject, o, true);
        }

        value async_from_sync_return(value aThis, arguments aArguments, value, function_object&)
        {
            auto& o = static_cast<async_from_sync_iterator&>(obj(aThis));
            value resolve, reject;
            auto const promise = new_promise_capability(the_realm().promise, resolve, reject);
            value result;
            try
            {
                auto const returnMethod = get_method(o.sync.iterator, the_names().return_);
                if (returnMethod == Undefined)
                {
                    value const result[] = { create_iter_result_object(argument(aArguments, 0u), true) };
                    call(resolve, Undefined, result);
                    return promise;
                }
                result = call(returnMethod, o.sync.iterator, aArguments.empty() ? arguments{} : aArguments.first(1u));
                if (!is_object(result))
                    throw_type_error("iterator.return() did not return an object");
            }
            catch (js_exception const& e)
            {
                value const argument[] = { e.thrown };
                call(reject, Undefined, argument);
                return promise;
            }
            return async_from_sync_continuation(result, promise, resolve, reject, o, false);
        }

        value async_from_sync_throw(value aThis, arguments aArguments, value, function_object&)
        {
            auto& o = static_cast<async_from_sync_iterator&>(obj(aThis));
            value resolve, reject;
            auto const promise = new_promise_capability(the_realm().promise, resolve, reject);
            value result;
            try
            {
                auto const throwMethod = get_method(o.sync.iterator, the_names().throw_);
                if (throwMethod == Undefined)
                {
                    o.sync.done = true;
                    iterator_close(o.sync);
                    throw_type_error("The iterator does not provide a 'throw' method");
                }
                result = call(throwMethod, o.sync.iterator, aArguments.empty() ? arguments{} : aArguments.first(1u));
                if (!is_object(result))
                    throw_type_error("iterator.throw() did not return an object");
            }
            catch (js_exception const& e)
            {
                value const argument[] = { e.thrown };
                call(reject, Undefined, argument);
                return promise;
            }
            return async_from_sync_continuation(result, promise, resolve, reject, o, true);
        }

        value generator_function_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            return create_dynamic_function(aArguments, aNewTarget, 1);
        }

        value async_function_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            return create_dynamic_function(aArguments, aNewTarget, 2);
        }

        value async_generator_function_constructor(value, arguments aArguments, value aNewTarget, function_object&)
        {
            return create_dynamic_function(aArguments, aNewTarget, 3);
        }

        // (a generator function's prototype object (GeneratorFunction.prototype): its constructor and prototype)
        value function_prototype_object(realm& aRealm, char const* aName, std::uint32_t aLength, native_function aConstructor,
            value aInstancePrototype, char const* aTag)
        {
            auto const c = make_native(aName, aLength, aConstructor, true);
            obj(c).proto = aRealm.function;
            auto const p = make_object(aRealm.functionPrototype);
            obj(c).put(the_names().prototype, p, 0u);
            obj(p).put(the_names().constructor, c, AttributeConfigurable);
            if (aInstancePrototype != Undefined)
            {
                obj(p).put(the_names().prototype, aInstancePrototype, AttributeConfigurable);
                obj(aInstancePrototype).put(the_names().constructor, p, AttributeConfigurable);
            }
            obj(p).put(aRealm.symToStringTag, ascii(aTag), AttributeConfigurable);
            return c;
        }
    }

    // --- the interface

    value await_value(value aValue)
    {
        auto& g = current("await");
        if (g.kind == generator_kind::Generator)
            throw_syntax_error("await is only valid in async functions");
        auto const promise = promise_resolve(the_realm().promise, aValue);
        perform_promise_then(promise, native_closure(await_fulfilled, g), native_closure(await_rejected, g));
        auto const resumption = suspend(g);
        if (resumption.type == Throw)
            throw js_exception{ resumption.v };
        return resumption.v;
    }

    value generator_resume(value aGenerator, value aValue, int aMode)
    {
        auto& g = validate_generator(aGenerator);
        if (g.state == generator_state::Executing)
            throw_type_error("Generator is already running");
        if (g.state == generator_state::SuspendedStart && aMode != Normal)
            g.state = generator_state::Completed;
        if (g.state == generator_state::Completed)
        {
            if (aMode == Throw)
                throw js_exception{ aValue };
            return create_iter_result_object(aMode == Return ? aValue : Undefined, true);
        }
        g.resumption = completion{ aMode, aValue };
        g.state = generator_state::Executing;
        bool const finished = run(g);
        if (finished)
        {
            g.state = generator_state::Completed;
            auto const result = g.result;
            g.result = Undefined;
            if (g.threw)
                throw js_exception{ result };
            return create_iter_result_object(result, true);
        }
        g.state = generator_state::SuspendedYield;
        auto const yielded = g.yielded;
        g.yielded = Undefined;
        return g.raw ? yielded : create_iter_result_object(yielded, false);
    }

    iterator_record create_async_from_sync_iterator(iterator_record const& aSyncRecord)
    {
        auto& o = the_heap().make<async_from_sync_iterator>(the_realm().asyncFromSyncIteratorPrototype);
        o.sync = aSyncRecord;
        auto const iterator = value_of(o);
        return iterator_record{ iterator, get(iterator, the_names().next), false, true };
    }

    value start_function(value aCallee, value aThis, value aEnvironment, std::uint32_t aBody, std::int32_t aFlags)
    {
        auto const& r = the_realm();
        bool const generator = (aFlags & 1) != 0;
        bool const async = (aFlags & 2) != 0;
        auto& callee = static_cast<function_object&>(obj(aCallee));
        auto const kind = generator ? (async ? generator_kind::AsyncGenerator : generator_kind::Generator) : generator_kind::AsyncFunction;
        value prototype = Null;
        if (generator)
        {
            prototype = get(aCallee, the_names().prototype);
            if (!is_object(prototype))
                prototype = async ? r.asyncGeneratorPrototype : r.generatorPrototype;
        }
        auto& g = the_heap().make<generator_object>(prototype, kind);
        g.callee = aCallee;
        g.thisValue = aThis;
        g.environment = aEnvironment;
        g.body = aBody;
        g.program = callee.program;
        if (kind != generator_kind::AsyncFunction)
            return value_of(g);
        // (an async function: its body runs now (until it awaits))
        g.promise = new_promise_capability(r.promise, g.resolve, g.reject);
        g.state = generator_state::Executing;
        ran(g, run(g));
        return g.promise;
    }

    value start_function_of(void* aProgram, value aEnvironment, std::uint32_t aBody)
    {
        // (code (e.g. a module's) that runs as an async function's body: its promise)
        auto const& r = the_realm();
        auto& g = the_heap().make<generator_object>(Null, generator_kind::AsyncFunction);
        g.environment = aEnvironment;
        g.body = aBody;
        g.program = aProgram;
        g.promise = new_promise_capability(r.promise, g.resolve, g.reject);
        g.state = generator_state::Executing;
        ran(g, run(g));
        return g.promise;
    }

    void const* running_generator()
    {
        return tCurrent;
    }

    value async_native(std::function<value()> aBody, std::initializer_list<value> aCaptured)
    {
        // (a built-in that runs as an async function (e.g. Array.fromAsync): its body may await (await_value); its promise)
        auto const& r = the_realm();
        auto& g = the_heap().make<generator_object>(Null, generator_kind::AsyncFunction);
        g.native = std::move(aBody);
        g.nativeValues.assign(aCaptured.begin(), aCaptured.end());
        g.promise = new_promise_capability(r.promise, g.resolve, g.reject);
        g.state = generator_state::Executing;
        ran(g, run(g));
        return g.promise;
    }

    value yield_value(value aValue)
    {
        auto& g = current("yield");
        if (g.kind == generator_kind::AsyncGenerator)
            return resumed(g, async_generator_yield(g, await_value(aValue)));
        return resumed(g, generator_yield(g, aValue, false));
    }

    value yield_delegate(value aIterable)
    {
        auto& g = current("yield*");
        bool const async = g.kind == generator_kind::AsyncGenerator;
        auto record = get_iterator(aIterable, async);
        auto const iterator = record.iterator;
        completion received;
        auto const yield = [&](value aInnerResult)
        {
            if (async)
                received = async_generator_yield(g, iterator_value(aInnerResult));
            else
                received = generator_yield(g, aInnerResult, true);
        };
        for (;;)
        {
            if (received.type == Normal)
            {
                value const argument[] = { received.v };
                auto innerResult = call(record.nextMethod, iterator, argument);
                if (async)
                    innerResult = await_value(innerResult);
                if (!is_object(innerResult))
                    throw_type_error("Iterator result " + describe(innerResult) + " is not an object");
                if (iterator_complete(innerResult))
                    return iterator_value(innerResult);
                yield(innerResult);
            }
            else if (received.type == Throw)
            {
                auto const throwMethod = get_method(iterator, the_names().throw_);
                if (throwMethod != Undefined)
                {
                    value const argument[] = { received.v };
                    auto innerResult = call(throwMethod, iterator, argument);
                    if (async)
                        innerResult = await_value(innerResult);
                    if (!is_object(innerResult))
                        throw_type_error("Iterator result " + describe(innerResult) + " is not an object");
                    if (iterator_complete(innerResult))
                        return iterator_value(innerResult);
                    yield(innerResult);
                }
                else
                {
                    iterator_close(record);
                    throw_type_error("The iterator does not provide a 'throw' method");
                }
            }
            else
            {
                auto const returnMethod = get_method(iterator, the_names().return_);
                if (returnMethod == Undefined)
                {
                    auto v = received.v;
                    if (async)
                        v = await_value(v);
                    g.returnValue = v;
                    return ReturnCompletion;
                }
                value const argument[] = { received.v };
                auto innerResult = call(returnMethod, iterator, argument);
                if (async)
                    innerResult = await_value(innerResult);
                if (!is_object(innerResult))
                    throw_type_error("Iterator result " + describe(innerResult) + " is not an object");
                if (iterator_complete(innerResult))
                {
                    g.returnValue = iterator_value(innerResult);
                    return ReturnCompletion;
                }
                yield(innerResult);
            }
        }
    }

    value resume_value()
    {
        auto& g = current("return");
        auto const result = g.returnValue;
        g.returnValue = Undefined;
        return result;
    }

    void init_generator(realm& aRealm)
    {
        // generators
        auto const generatorPrototype = make_object(aRealm.iteratorPrototype);
        aRealm.generatorPrototype = generatorPrototype;
        method(generatorPrototype, "next", 1u, generator_next);
        method(generatorPrototype, "return", 1u, generator_return);
        method(generatorPrototype, "throw", 1u, generator_throw);
        obj(generatorPrototype).put(aRealm.symToStringTag, ascii("Generator"), AttributeConfigurable);
        aRealm.generatorFunction = function_prototype_object(aRealm, "GeneratorFunction", 1u, generator_function_constructor, generatorPrototype, "GeneratorFunction");
        aRealm.generatorFunctionPrototype = get(aRealm.generatorFunction, the_names().prototype);
        // async functions
        aRealm.asyncFunction = function_prototype_object(aRealm, "AsyncFunction", 1u, async_function_constructor, Undefined, "AsyncFunction");
        aRealm.asyncFunctionPrototype = get(aRealm.asyncFunction, the_names().prototype);
        // async generators
        auto const asyncGeneratorPrototype = make_object(aRealm.asyncIteratorPrototype);
        aRealm.asyncGeneratorPrototype = asyncGeneratorPrototype;
        method(asyncGeneratorPrototype, "next", 1u, async_generator_next);
        method(asyncGeneratorPrototype, "return", 1u, async_generator_return);
        method(asyncGeneratorPrototype, "throw", 1u, async_generator_throw);
        obj(asyncGeneratorPrototype).put(aRealm.symToStringTag, ascii("AsyncGenerator"), AttributeConfigurable);
        aRealm.asyncGeneratorFunction = function_prototype_object(aRealm, "AsyncGeneratorFunction", 1u, async_generator_function_constructor,
            asyncGeneratorPrototype, "AsyncGeneratorFunction");
        aRealm.asyncGeneratorFunctionPrototype = get(aRealm.asyncGeneratorFunction, the_names().prototype);
        // %AsyncFromSyncIteratorPrototype%
        auto const asyncFromSync = make_object(aRealm.asyncIteratorPrototype);
        aRealm.asyncFromSyncIteratorPrototype = asyncFromSync;
        method(asyncFromSync, "next", 1u, async_from_sync_next);
        method(asyncFromSync, "return", 1u, async_from_sync_return);
        method(asyncFromSync, "throw", 1u, async_from_sync_throw);
    }
}
