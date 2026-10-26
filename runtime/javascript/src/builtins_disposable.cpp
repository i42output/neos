/*
  builtins_disposable.cpp

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

// Explicit Resource Management: DisposableStack, AsyncDisposableStack, %AsyncIteratorPrototype%[Symbol.asyncDispose] and
// the runtime's side of using and await using declarations (dispose_new, dispose_add and dispose_all).
//
// DisposeResources is written once, as a state machine (dispose_resources) that runs until it must Await a value: its
// drivers either await that value with await_value (compiled code's using declarations, which run in an async function
// when they have async resources; a synchronous stack never awaits) or with promise reactions that continue the machine
// (AsyncDisposableStack.prototype.disposeAsync, a built-in async method, which has no coroutine of its own).

#include "js.hpp"
#include "api.hpp"

namespace neos::javascript_runtime
{
    namespace
    {
        // (a DisposableResource Record)
        struct disposable_resource
        {
            value resourceValue = Undefined;
            value disposeMethod = Undefined;
            bool async = false;                 ///< ([[Kind]]: async-dispose)
        };

        using resource_stack = std::vector<disposable_resource>;           ///< (a cell's (traced))
        using resource_list = rooted_vector<disposable_resource>;           ///< (DisposeResources' (rooted: it is not a cell's member))

        void trace_resources(tracer& aTracer, resource_stack const& aResources)
        {
            for (auto const& r : aResources)
            {
                aTracer.mark(r.resourceValue);
                aTracer.mark(r.disposeMethod);
            }
        }

        // (a DisposeCapability: compiled code's using declarations' resources (of one block))
        class dispose_capability : public cell
        {
        public:
            dispose_capability() : cell{ cell_type::Internal } {}
        public:
            void trace(tracer& aTracer) override
            {
                trace_resources(aTracer, resources);
            }
        public:
            resource_stack resources;
        };

        class disposable_stack_object : public object
        {
        public:
            disposable_stack_object(value aPrototype, bool aAsync) :
                object{ aPrototype, aAsync ? object_class::AsyncDisposableStack : object_class::DisposableStack }
            {
            }
        public:
            void trace(tracer& aTracer) override
            {
                object::trace(aTracer);
                trace_resources(aTracer, resources);
            }
        public:
            bool disposed = false;              ///< ([[DisposableState]] / [[AsyncDisposableState]])
            resource_stack resources;           ///< ([[DisposableResourceStack]])
        };

        // --- DisposeResources

        class dispose_resources
        {
        public:
            dispose_resources(resource_list aResources, value aCompletion, bool aThrew) :
                iResources{ std::move(aResources) }, iCompletion{ aCompletion }, iThrew{ aThrew }
            {
            }
        public:
            // (run until the next Await: its value (none: finished; the completion follows))
            std::optional<value> run()
            {
                while (!iResources.empty())
                {
                    auto const& resource = iResources.back();
                    if (!resource.async && iNeedsAwait && !iHasAwaited)
                    {
                        iNeedsAwait = false;
                        iAwaitingResult = false;
                        return Undefined;
                    }
                    auto const r = resource;
                    iResources.pop_back();
                    if (r.disposeMethod != Undefined)
                    {
                        value result;
                        try
                        {
                            result = call(r.disposeMethod, r.resourceValue);
                        }
                        catch (js_exception const& e)
                        {
                            add_error(e.thrown);
                            continue;
                        }
                        if (r.async)
                        {
                            iHasAwaited = true;
                            iAwaitingResult = true;
                            return result;
                        }
                    }
                    else
                        iNeedsAwait = true;
                }
                if (iNeedsAwait && !iHasAwaited)
                {
                    iNeedsAwait = false;
                    iAwaitingResult = false;
                    return Undefined;
                }
                return std::nullopt;
            }
            // (the Await's completion)
            void resume(bool aThrew, value aValue)
            {
                if (iAwaitingResult && aThrew)
                    add_error(aValue);
                iAwaitingResult = false;
            }
            bool threw() const { return iThrew; }
            value completion() const { return iCompletion; }
            void trace(tracer& aTracer) const
            {
                aTracer.mark(iCompletion);  // (its resources are rooted)
            }
        private:
            void add_error(value aError)
            {
                if (iThrew)
                {
                    auto& error = the_heap().make<object>(the_realm().suppressedErrorPrototype, object_class::Error);
                    error.put(the_names().error, aError, AttributesHidden);
                    error.put(the_names().suppressed, iCompletion, AttributesHidden);
                    iCompletion = value_of(error);
                }
                else
                {
                    iThrew = true;
                    iCompletion = aError;
                }
            }
        private:
            resource_list iResources;           ///< (the resources not disposed yet: disposed from the back)
            value iCompletion;
            bool iThrew;
            bool iNeedsAwait = false;
            bool iHasAwaited = false;
            bool iAwaitingResult = false;       ///< (the pending Await is of a dispose method's result (not of undefined))
        };

        // (DisposeResources where an Await suspends the running async function: its completion's value, or throws)
        value dispose_resources_awaiting(resource_list aResources, value aCompletion, bool aThrew)
        {
            dispose_resources operation{ std::move(aResources), aCompletion, aThrew };
            while (auto const awaited = operation.run())
            {
                try
                {
                    operation.resume(false, await_value(*awaited));
                }
                catch (js_exception const& e)
                {
                    operation.resume(true, e.thrown);
                }
            }
            if (operation.threw())
                throw_value(operation.completion());
            return operation.completion();
        }

        // (DisposeResources in a built-in async method: its Awaits are promise reactions that continue it; its completion settles
        // a promise)
        class dispose_resources_job : public cell
        {
        public:
            dispose_resources_job(resource_list aResources, value aResolve, value aReject) :
                cell{ cell_type::Internal }, operation{ std::move(aResources), Undefined, false }, resolve{ aResolve }, reject{ aReject }
            {
            }
        public:
            void trace(tracer& aTracer) override
            {
                operation.trace(aTracer);
                aTracer.mark(resolve);
                aTracer.mark(reject);
            }
        public:
            dispose_resources operation;
            value resolve;
            value reject;
        };

        value dispose_job_fulfilled(value, arguments aArguments, value, function_object& aSelf);
        value dispose_job_rejected(value, arguments aArguments, value, function_object& aSelf);

        void dispose_job_continue(dispose_resources_job& aJob)
        {
            for (;;)
            {
                auto const awaited = aJob.operation.run();
                if (!awaited)
                {
                    value const argument[] = { aJob.operation.threw() ? aJob.operation.completion() : Undefined };
                    call(aJob.operation.threw() ? aJob.reject : aJob.resolve, Undefined, argument);
                    return;
                }
                // (Await)
                value promise;
                try
                {
                    promise = promise_resolve(the_realm().promise, *awaited);
                }
                catch (js_exception const& e)
                {
                    aJob.operation.resume(true, e.thrown);
                    continue;
                }
                auto const onFulfilled = make_native(the_names().empty, 1u, dispose_job_fulfilled);
                auto const onRejected = make_native(the_names().empty, 1u, dispose_job_rejected);
                obj(onFulfilled).internal() = TagInternal | aJob.handle;
                obj(onRejected).internal() = TagInternal | aJob.handle;
                perform_promise_then(promise, onFulfilled, onRejected);
                return;
            }
        }

        value dispose_job_fulfilled(value, arguments aArguments, value, function_object& aSelf)
        {
            auto& job = cell_of<dispose_resources_job>(aSelf.internal());
            job.operation.resume(false, argument(aArguments, 0u));
            dispose_job_continue(job);
            return Undefined;
        }

        value dispose_job_rejected(value, arguments aArguments, value, function_object& aSelf)
        {
            auto& job = cell_of<dispose_resources_job>(aSelf.internal());
            job.operation.resume(true, argument(aArguments, 0u));
            dispose_job_continue(job);
            return Undefined;
        }

        // --- AddDisposableResource

        value sync_dispose_wrapper(value aThis, arguments, value, function_object& aSelf)
        {
            // (GetDisposeMethod's closure for an await using of an object with only a [Symbol.dispose] method)
            value resolve, reject;
            auto const promise = new_promise_capability(the_realm().promise, resolve, reject);
            try
            {
                call(aSelf.slots[0], aThis);
            }
            catch (js_exception const& e)
            {
                value const argument[] = { e.thrown };
                call(reject, Undefined, argument);
                return promise;
            }
            value const argument[] = { Undefined };
            call(resolve, Undefined, argument);
            return promise;
        }

        // (GetDisposeMethod)
        value get_dispose_method(value aValue, bool aAsync)
        {
            auto const& r = the_realm();
            if (!is_object(aValue))
                throw_type_error(describe(aValue) + " is not an object: it cannot be disposed");
            if (!aAsync)
                return get_method(aValue, r.symDispose);
            auto const asyncMethod = get_method(aValue, r.symAsyncDispose);
            if (asyncMethod != Undefined)
                return asyncMethod;
            auto const syncMethod = get_method(aValue, r.symDispose);
            if (syncMethod == Undefined)
                return Undefined;
            auto const closure = make_native(the_names().empty, 0u, sync_dispose_wrapper);
            static_cast<function_object&>(obj(closure)).slots[0] = syncMethod;
            return closure;
        }

        // (AddDisposableResource without a method)
        void add_disposable_resource(resource_stack& aStack, value aValue, bool aAsync)
        {
            if (is_nullish(aValue))
            {
                if (!aAsync)
                    return;
                aStack.push_back(disposable_resource{ Undefined, Undefined, true });
                return;
            }
            auto const method = get_dispose_method(aValue, aAsync);
            if (method == Undefined)
                throw_type_error(describe(aValue) + " has no " + (aAsync ? "[Symbol.asyncDispose] or [Symbol.dispose]" : "[Symbol.dispose]") + " method");
            aStack.push_back(disposable_resource{ aValue, method, aAsync });
        }

        // --- DisposableStack and AsyncDisposableStack

        disposable_stack_object& this_stack(value aThis, bool aAsync, char const* aMethod)
        {
            auto const cls = aAsync ? object_class::AsyncDisposableStack : object_class::DisposableStack;
            if (!is_object(aThis) || obj(aThis).cls != cls)
                throw_type_error(std::string{ aAsync ? "AsyncDisposableStack" : "DisposableStack" } + ".prototype." + aMethod +
                    " called on incompatible receiver " + describe(aThis));
            return static_cast<disposable_stack_object&>(obj(aThis));
        }

        disposable_stack_object& pending_stack(value aThis, bool aAsync, char const* aMethod)
        {
            auto& s = this_stack(aThis, aAsync, aMethod);
            if (s.disposed)
                throw_reference_error(std::string{ aAsync ? "AsyncDisposableStack" : "DisposableStack" } + " already disposed");
            return s;
        }

        value make_stack(value aNewTarget, bool aAsync)
        {
            auto const prototype = get_prototype_from_constructor(aNewTarget,
                aAsync ? &realm::asyncDisposableStackPrototype : &realm::disposableStackPrototype);
            return value_of(the_heap().make<disposable_stack_object>(prototype, aAsync));
        }

        value disposable_stack_constructor(value, arguments, value aNewTarget, function_object&)
        {
            if (aNewTarget == Undefined)
                throw_type_error("Constructor DisposableStack requires 'new'");
            return make_stack(aNewTarget, false);
        }

        value async_disposable_stack_constructor(value, arguments, value aNewTarget, function_object&)
        {
            if (aNewTarget == Undefined)
                throw_type_error("Constructor AsyncDisposableStack requires 'new'");
            return make_stack(aNewTarget, true);
        }

        value adopt_closure(value, arguments, value, function_object& aSelf)
        {
            value const argument[] = { aSelf.slots[0] };
            return call(aSelf.slots[1], Undefined, argument);
        }

        value stack_adopt(value aThis, arguments aArguments, bool aAsync)
        {
            auto& s = pending_stack(aThis, aAsync, "adopt");
            auto const v = argument(aArguments, 0u);
            auto const onDispose = argument(aArguments, 1u);
            if (!is_callable(onDispose))
                throw_type_error(describe(onDispose) + " is not a function");
            auto const closure = make_native(the_names().empty, 0u, adopt_closure);
            auto& f = static_cast<function_object&>(obj(closure));
            f.slots[0] = v;
            f.slots[1] = onDispose;
            s.resources.push_back(disposable_resource{ Undefined, closure, aAsync });
            return v;
        }

        value stack_defer(value aThis, arguments aArguments, bool aAsync)
        {
            auto& s = pending_stack(aThis, aAsync, "defer");
            auto const onDispose = argument(aArguments, 0u);
            if (!is_callable(onDispose))
                throw_type_error(describe(onDispose) + " is not a function");
            s.resources.push_back(disposable_resource{ Undefined, onDispose, aAsync });
            return Undefined;
        }

        value stack_disposed(value aThis, bool aAsync)
        {
            return boolean(this_stack(aThis, aAsync, "disposed").disposed);
        }

        value stack_move(value aThis, bool aAsync)
        {
            auto& s = pending_stack(aThis, aAsync, "move");
            auto& result = the_heap().make<disposable_stack_object>(
                aAsync ? the_realm().asyncDisposableStackPrototype : the_realm().disposableStackPrototype, aAsync);
            result.resources = std::move(s.resources);
            s.resources.clear();
            s.disposed = true;
            return value_of(result);
        }

        value stack_use(value aThis, arguments aArguments, bool aAsync)
        {
            auto& s = pending_stack(aThis, aAsync, "use");
            auto const v = argument(aArguments, 0u);
            add_disposable_resource(s.resources, v, aAsync);
            return v;
        }

        value disposable_stack_adopt(value aThis, arguments aArguments, value, function_object&)
        {
            return stack_adopt(aThis, aArguments, false);
        }

        value disposable_stack_defer(value aThis, arguments aArguments, value, function_object&)
        {
            return stack_defer(aThis, aArguments, false);
        }

        value disposable_stack_dispose(value aThis, arguments, value, function_object&)
        {
            auto& s = this_stack(aThis, false, "dispose");
            if (s.disposed)
                return Undefined;
            s.disposed = true;
            resource_list resources{ s.resources.begin(), s.resources.end() };
            s.resources.clear();
            // (a DisposableStack's resources are all sync-dispose: it never awaits)
            return dispose_resources_awaiting(std::move(resources), Undefined, false);
        }

        value disposable_stack_disposed(value aThis, arguments, value, function_object&)
        {
            return stack_disposed(aThis, false);
        }

        value disposable_stack_move(value aThis, arguments, value, function_object&)
        {
            return stack_move(aThis, false);
        }

        value disposable_stack_use(value aThis, arguments aArguments, value, function_object&)
        {
            return stack_use(aThis, aArguments, false);
        }

        value async_disposable_stack_adopt(value aThis, arguments aArguments, value, function_object&)
        {
            return stack_adopt(aThis, aArguments, true);
        }

        value async_disposable_stack_defer(value aThis, arguments aArguments, value, function_object&)
        {
            return stack_defer(aThis, aArguments, true);
        }

        value async_disposable_stack_dispose_async(value aThis, arguments, value, function_object&)
        {
            auto const& r = the_realm();
            value resolve, reject;
            auto const promise = new_promise_capability(r.promise, resolve, reject);
            if (!is_object(aThis) || obj(aThis).cls != object_class::AsyncDisposableStack)
            {
                value const argument[] = { make_error(r.typeErrorPrototype,
                    make_string_utf8("AsyncDisposableStack.prototype.disposeAsync called on incompatible receiver " + describe(aThis))) };
                call(reject, Undefined, argument);
                return promise;
            }
            auto& s = static_cast<disposable_stack_object&>(obj(aThis));
            if (s.disposed)
            {
                value const argument[] = { Undefined };
                call(resolve, Undefined, argument);
                return promise;
            }
            s.disposed = true;
            resource_list resources{ s.resources.begin(), s.resources.end() };
            s.resources.clear();
            dispose_job_continue(the_heap().make<dispose_resources_job>(std::move(resources), resolve, reject));
            return promise;
        }

        value async_disposable_stack_disposed(value aThis, arguments, value, function_object&)
        {
            return stack_disposed(aThis, true);
        }

        value async_disposable_stack_move(value aThis, arguments, value, function_object&)
        {
            return stack_move(aThis, true);
        }

        value async_disposable_stack_use(value aThis, arguments aArguments, value, function_object&)
        {
            return stack_use(aThis, aArguments, true);
        }

        // --- %AsyncIteratorPrototype% [ %Symbol.asyncDispose% ]

        value async_iterator_dispose_unwrap(value, arguments, value, function_object&)
        {
            return Undefined;
        }

        value async_iterator_prototype_async_dispose(value aThis, arguments, value, function_object&)
        {
            auto const& r = the_realm();
            value resolve, reject;
            auto const promise = new_promise_capability(r.promise, resolve, reject);
            try
            {
                auto const returnMethod = get_method(aThis, the_names().return_);
                if (returnMethod == Undefined)
                {
                    value const argument[] = { Undefined };
                    call(resolve, Undefined, argument);
                }
                else
                {
                    auto const result = call(returnMethod, aThis);
                    auto const resultWrapper = promise_resolve(r.promise, result);
                    auto const onFulfilled = make_native(the_names().empty, 1u, async_iterator_dispose_unwrap);
                    perform_promise_then(resultWrapper, onFulfilled, Undefined, promise, resolve, reject);
                }
            }
            catch (js_exception const& e)
            {
                value const argument[] = { e.thrown };
                call(reject, Undefined, argument);
            }
            return promise;
        }

        value make_stack_constructor(realm& aRealm, char const* aName, native_function aConstructor, value aPrototype)
        {
            auto const c = make_native(aName, 0u, aConstructor, true);
            obj(c).put(the_names().prototype, aPrototype, 0u);
            obj(aPrototype).put(the_names().constructor, c);
            obj(aPrototype).put(aRealm.symToStringTag, ascii(aName), AttributeConfigurable);
            obj(aRealm.global).put(ascii(aName), c);
            return c;
        }
    }

    // --- the interface (using and await using declarations)

    value dispose_new()
    {
        return value_of(the_heap().make<dispose_capability>());
    }

    void dispose_add(value aCapability, value aValue, bool aAsync)
    {
        add_disposable_resource(cell_of<dispose_capability>(aCapability).resources, aValue, aAsync);
    }

    value dispose_all(value aCapability, value aCompletion, bool aThrew)
    {
        auto& capability = cell_of<dispose_capability>(aCapability);
        resource_list resources{ capability.resources.begin(), capability.resources.end() };
        capability.resources.clear();
        dispose_resources_awaiting(std::move(resources), aCompletion, aThrew);
        return Undefined;
    }

    void init_disposable(realm& aRealm)
    {
        // DisposableStack
        aRealm.disposableStackPrototype = make_object();
        auto const dp = aRealm.disposableStackPrototype;
        make_stack_constructor(aRealm, "DisposableStack", disposable_stack_constructor, dp);
        method(dp, "adopt", 2u, disposable_stack_adopt);
        method(dp, "defer", 1u, disposable_stack_defer);
        auto const dispose = method(dp, "dispose", 0u, disposable_stack_dispose);
        getter(dp, ascii("disposed"), disposable_stack_disposed);
        method(dp, "move", 0u, disposable_stack_move);
        method(dp, "use", 1u, disposable_stack_use);
        obj(dp).put(aRealm.symDispose, dispose);
        // AsyncDisposableStack
        aRealm.asyncDisposableStackPrototype = make_object();
        auto const ap = aRealm.asyncDisposableStackPrototype;
        make_stack_constructor(aRealm, "AsyncDisposableStack", async_disposable_stack_constructor, ap);
        method(ap, "adopt", 2u, async_disposable_stack_adopt);
        method(ap, "defer", 1u, async_disposable_stack_defer);
        auto const disposeAsync = method(ap, "disposeAsync", 0u, async_disposable_stack_dispose_async);
        getter(ap, ascii("disposed"), async_disposable_stack_disposed);
        method(ap, "move", 0u, async_disposable_stack_move);
        method(ap, "use", 1u, async_disposable_stack_use);
        obj(ap).put(aRealm.symAsyncDispose, disposeAsync);
        // %AsyncIteratorPrototype% [ %Symbol.asyncDispose% ] (%IteratorPrototype% [ %Symbol.dispose% ] is builtins_iterator.cpp's)
        method(aRealm.asyncIteratorPrototype, aRealm.symAsyncDispose, 0u, async_iterator_prototype_async_dispose);
    }
}
