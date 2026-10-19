/*
  generators.cpp

  Copyright (c) 2026 Leigh Johnston.  All Rights Reserved.

  This program is free software: you can redistribute it and / or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Generators: a generator function returns __generator(body, name, qualname, arguments...) (see the Python compiler's
// closures.cpp): its body runs on a coroutine of its own (neos_rt_host::coroutine_new) when the generator is resumed;
// __yield(value) suspends it (the value sent in is its result) and __yield_from(iterable) delegates to another iterable
//
// Coroutines: an async def returns __coroutine(body, name, qualname, arguments...) (an asynchronous generator function:
// __async_generator(...)), whose body runs on a coroutine of its own as a generator's does; __await(awaitable) suspends it
// (and the functions that it has called) for each value that the awaitable's iterator yields. A coroutine that is awaited
// before it has started runs on the coroutine of the one that awaits it (as a call). __async_iter(iterable) and 
// __async_with(manager) are the iterator and context manager of async for and async with: their __next__, __enter__ and
// __exit__ await __anext__, __aenter__ and __aexit__.

#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            struct generator_object
            {
                header h;
                std::string name;
                std::string qualname;
                object* function = nullptr;
                object* arguments = nullptr;    ///< (a tuple)
                object* keywords = nullptr;     ///< (a dict or null)
                neos_rt_coroutine* coroutine = nullptr;
                enum state_t { Created, Suspended, Running, Finished } state = Created;
                object* transfer = nullptr;     ///< (the value yielded, or the value sent)
                object* thrown = nullptr;       ///< (an exception to raise where the body yielded)
                object* result = nullptr;       ///< (the body's return value)
                generator_object* resumer = nullptr;    ///< (the generator that resumed it, if any)
                bool dying = false;                     ///< (being destroyed: closed)
                enum kind_t { Generator, Coroutine, AsyncGenerator } kind = Generator;
                bool awaiting = false;                  ///< (suspended by an await (not by an asynchronous generator's yield))
                bool asyncRunning = false;              ///< (an asynchronous generator: an asend, athrow or aclose is being awaited)
                bool hooked = false;                    ///< (an asynchronous generator: its first iteration has begun)
                ~generator_object();
            };

            thread_local generator_object* tCurrent = nullptr;
            bool sProgramEnded = false;

            neos_py_type make_generator_type(generator_object::kind_t aKind);
            neos_py_type sGeneratorType = make_generator_type(generator_object::Generator);
            neos_py_type sCoroutineType = make_generator_type(generator_object::Coroutine);
            neos_py_type sAsyncGeneratorType = make_generator_type(generator_object::AsyncGenerator);

            bool is_generator(object* aObject) { return aObject != nullptr && aObject->type == &sGeneratorType.base; }
            bool is_coroutine_object(object* aObject) { return aObject != nullptr && aObject->type == &sCoroutineType.base; }
            bool is_async_generator(object* aObject) { return aObject != nullptr && aObject->type == &sAsyncGeneratorType.base; }
            char const* kind_name(generator_object const& g)
            {
                return g.kind == generator_object::Coroutine ? "coroutine" : g.kind == generator_object::AsyncGenerator ? "async generator" : "generator";
            }
            generator_object& as_generator(object* aObject) { return *reinterpret_cast<generator_object*>(aObject); }

            void body(void* aGenerator)
            {
                auto& g = *static_cast<generator_object*>(aGenerator);
                g.result = call(g.function, g.arguments, g.keywords);
            }

            // resume a generator (aValue: the value sent; or aThrown: an exception to raise in it): the value it yields (a new
            // reference), or null when it finishes (an exception if it raised one)
            object* resume(generator_object& g, object* aValue, object* aThrown)
            {
                if (g.state == generator_object::Running)
                    return error("ValueError", std::string{ kind_name(g) } + " already executing");
                if (g.state == generator_object::Finished)
                {
                    if (aThrown != nullptr)
                    {
                        fn_raise(aThrown);
                        return nullptr;
                    }
                    return nullptr;
                }
                if (g.state == generator_object::Created)
                {
                    if (aThrown != nullptr)
                    {
                        g.state = generator_object::Finished;
                        fn_raise(aThrown);
                        return nullptr;
                    }
                    if (aValue != nullptr && kind_of(aValue) != NEOS_PY_NONE)
                        return error("TypeError", std::string{ "can't send non-None value to a just-started " } + kind_name(g));
                    g.coroutine = host()->coroutine_new(body, &g);
                    if (g.coroutine == nullptr)
                        return error("MemoryError", "cannot create a generator's stack");
                }
                retain(aValue);
                release(g.transfer);
                g.transfer = aValue;
                retain(aThrown);
                release(g.thrown);
                g.thrown = aThrown;
                g.state = generator_object::Running;
                g.resumer = tCurrent;
                tCurrent = &g;
                // (alive while it runs)
                if (!g.dying)
                    retain(&g.h.base);
                auto const finished = host()->coroutine_resume(g.coroutine);
                tCurrent = g.resumer;
                ref const self{ g.dying ? nullptr : &g.h.base };
                if (finished != 0)
                {
                    g.state = generator_object::Finished;
                    host()->coroutine_delete(g.coroutine);
                    g.coroutine = nullptr;
                    release(g.transfer);
                    g.transfer = nullptr;
                    if (failed())
                    {
                        // (PEP 479: StopIteration raised in a generator is a RuntimeError)
                        if (std::string_view{ error_text() }.starts_with("StopIteration"))
                        {
                            clear_error();
                            return error("RuntimeError", std::string{ kind_name(g) } + " raised StopIteration");
                        }
                        if (g.kind == generator_object::AsyncGenerator && std::string_view{ error_text() }.starts_with("StopAsyncIteration"))
                        {
                            clear_error();
                            return error("RuntimeError", "async generator raised StopAsyncIteration");
                        }
                    }
                    return nullptr;
                }
                g.state = generator_object::Suspended;
                auto const yielded = g.transfer;
                g.transfer = nullptr;
                return yielded;
            }

            // a generator's StopIteration (its return value)
            object* stop(generator_object& g)
            {
                if (failed())
                    return nullptr;
                if (g.result != nullptr && kind_of(g.result) != NEOS_PY_NONE)
                {
                    ref const args{ make_tuple({ (retain(g.result), g.result) }) };
                    ref const cls{ fn_builtin("StopIteration") };
                    ref const e{ call(cls.get(), args.get(), nullptr) };
                    if (e)
                        raise_exception(e.get());
                    return nullptr;
                }
                return error("StopIteration", "");
            }

            // close: GeneratorExit raised where it yielded
            object* close(generator_object& g)
            {
                if (g.state == generator_object::Created)
                {
                    g.state = generator_object::Finished;
                    return none();
                }
                if (g.state != generator_object::Suspended)
                    return none();
                ref const cls{ fn_builtin("GeneratorExit") };
                ref const yielded{ resume(g, nullptr, cls.get()) };
                if (yielded)
                    return error("RuntimeError", "generator ignored GeneratorExit");
                if (failed())
                {
                    std::string_view const text{ error_text() };
                    if (text.starts_with("GeneratorExit") || text.starts_with("StopIteration"))
                        clear_error();
                    else
                        return nullptr;
                }
                return none();
            }

            generator_object::~generator_object()
            {
                // (a suspended generator is closed: its finally clauses run)
                if (state == Suspended && !sProgramEnded && !failed())
                {
                    dying = true;
                    ref const r{ close(*this) };
                    if (failed())
                        clear_error();
                }
                if (coroutine != nullptr)
                    host()->coroutine_delete(coroutine);
                release(function);
                release(arguments);
                release(keywords);
                release(transfer);
                release(thrown);
                release(result);
            }

            object* generator_send(generator_object& g, object* aValue)
            {
                if (g.kind == generator_object::Coroutine && g.state == generator_object::Finished)
                    return error("RuntimeError", "cannot reuse already awaited coroutine");
                if (auto const yielded = resume(g, aValue, nullptr))
                    return yielded;
                return stop(g);
            }

            struct generator_method
            {
                char const* name;
                object* (*function)(object* aSelf, std::vector<object*> const& aArgs);
                std::size_t minimum;
                std::size_t maximum;
            };

            generator_method const sMethods[] =
            {
                { "send", [](object* aSelf, std::vector<object*> const& aArgs) { return generator_send(as_generator(aSelf), aArgs[0]); }, 1u, 1u },
                { "__next__", [](object* aSelf, std::vector<object*> const&) { return generator_send(as_generator(aSelf), nullptr); }, 0u, 0u },
                { "throw", [](object* aSelf, std::vector<object*> const& aArgs) -> object*
                    {
                        auto& g = as_generator(aSelf);
                        if (g.kind == generator_object::Coroutine && g.state == generator_object::Finished)
                            return error("RuntimeError", "cannot reuse already awaited coroutine");
                        ref const yielded{ resume(g, nullptr, aArgs[0]) };
                        if (yielded)
                        {
                            retain(yielded.get());
                            return yielded.get();
                        }
                        return stop(g);
                    }, 1u, 3u },
                { "close", [](object* aSelf, std::vector<object*> const&) { return close(as_generator(aSelf)); }, 0u, 0u },
                { "__iter__", [](object* aSelf, std::vector<object*> const&) { retain(aSelf); return aSelf; }, 0u, 0u }
            };

            object* call_method(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& method = *static_cast<generator_method const*>(aData);
                auto const& items = as<tuple_object>(aArgs)->items;
                std::vector<object*> const args{ items.begin() + 1, items.end() };
                std::string const name = std::string{ as_generator(items[0]).kind == generator_object::Coroutine ? "coroutine." : "generator." } + method.name;
                if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                    return error("TypeError", name + "() takes no keyword arguments");
                if (args.size() < method.minimum || args.size() > method.maximum)
                {
                    if (method.minimum == method.maximum)
                        return error("TypeError", name + (method.minimum == 0u ? "() takes no arguments (" : "() takes exactly one argument (") +
                            std::to_string(args.size()) + " given)");
                    return error("TypeError", std::string{ method.name } + " expected at most " + std::to_string(method.maximum) + " arguments, got " + std::to_string(args.size()));
                }
                return method.function(items[0], args);
            }

            object* async_generator_attribute(object* aSelf, char const* aName);
            object* coroutine_await(neos_py_context*, void*, object* aArgs, object*);

            neos_py_type make_generator_type(generator_object::kind_t aKind)
            {
                char const* const name = aKind == generator_object::Coroutine ? "coroutine" : aKind == generator_object::AsyncGenerator ? "async_generator" : "generator";
                auto type = make_type(name, name, retain_object, release_object<generator_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    char address[32];
                    std::snprintf(address, sizeof(address), "%p", static_cast<void*>(aSelf));
                    auto const& g = as_generator(aSelf);
                    char const* const kind = g.kind == generator_object::Coroutine ? "coroutine" : g.kind == generator_object::AsyncGenerator ? "async_generator" : "generator";
                    return make_str(std::string{ "<" } + kind + " object " + g.qualname + " at " + address + ">");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto& g = as_generator(aSelf);
                    char const* const prefix = g.kind == generator_object::Coroutine ? "cr_" : g.kind == generator_object::AsyncGenerator ? "ag_" : "gi_";
                    std::string_view const attribute{ aName };
                    if (g.kind == generator_object::AsyncGenerator)
                    {
                        if (auto const result = async_generator_attribute(aSelf, aName))
                            return result;
                        if (failed())
                            return nullptr;
                    }
                    else
                        for (auto const& method : sMethods)
                            if (std::strcmp(method.name, aName) == 0 && (g.kind == generator_object::Generator ||
                                (std::strcmp(aName, "__next__") != 0 && std::strcmp(aName, "__iter__") != 0)))
                                return make_function(method.name, call_method, const_cast<generator_method*>(&method), aSelf);
                    if (g.kind == generator_object::Coroutine && attribute == "__await__")
                        return make_function("__await__", coroutine_await, nullptr, aSelf);
                    if (attribute == "__name__")
                        return make_str(g.name);
                    if (attribute == "__qualname__")
                        return make_str(g.qualname);
                    if (attribute.starts_with(prefix))
                    {
                        auto const rest = attribute.substr(3u);
                        if (rest == "running")
                            return make_bool(g.kind == generator_object::AsyncGenerator ? g.asyncRunning : g.state == generator_object::Running);
                        if (rest == "suspended")
                            return make_bool(g.state == generator_object::Suspended);
                        // (its frame: None once it has finished (else a frame of its function's name: its source and line are not known))
                        if (rest == "frame")
                        {
                            if (g.state == generator_object::Finished)
                                return (retain(none()), none());
                            static std::mutex sMutex;
                            static std::set<std::string> sNames;
                            std::lock_guard<std::mutex> lock{ sMutex };
                            auto const& name = *sNames.insert(g.name).first;
                            neos_rt_frame const frame{ name.c_str(), "", 0u, 0u, nullptr, 0u };
                            return make_frame(std::make_shared<std::vector<neos_rt_frame> const>(1u, frame), 0u);
                        }
                        // (its code: its body's)
                        if (rest == "code")
                        {
                            if (auto const code = getattr(g.function, "__code__"))
                                return code;
                            clear_error();
                        }
                        if (rest == "await" || rest == "yieldfrom")
                            return (retain(none()), none());
                    }
                    char const* const typeName = g.kind == generator_object::Coroutine ? "coroutine" : g.kind == generator_object::AsyncGenerator ? "async_generator" : "generator";
                    return error("AttributeError", std::string{ "'" } + typeName + "' object has no attribute '" + aName + "'");
                };
                if (aKind == generator_object::Generator)
                {
                    type.iter = [](neos_py_context*, object* aSelf) -> object* { retain(aSelf); return aSelf; };
                    type.next = [](neos_py_context*, object* aSelf) -> object*
                    {
                        // (null without an exception: exhausted)
                        return resume(as_generator(aSelf), nullptr, nullptr);
                    };
                }
                return type;
            }

            // --- awaiting

            // (the exception being raised: taken (no longer raised))
            ref take_exception()
            {
                std::string const text = error_text();
                clear_error();
                ref result{ fn_caught(text.c_str()) };
                fn_handled();
                return result;
            }

            // (a coroutine's __await__(): its iterator)
            struct coroutine_wrapper_object
            {
                header h;
                object* coroutine;
                ~coroutine_wrapper_object() { release(coroutine); }
            };

            neos_py_type make_coroutine_wrapper_type();
            neos_py_type sCoroutineWrapperType = make_coroutine_wrapper_type();

            object* coroutine_await(neos_py_context*, void*, object* aArgs, object*)
            {
                auto const self = as<tuple_object>(aArgs)->items.at(0u);
                retain(self);
                return &(new coroutine_wrapper_object{ { { &sCoroutineWrapperType.base } }, self })->h.base;
            }

            neos_py_type make_coroutine_wrapper_type()
            {
                auto type = make_type("coroutine_wrapper", "coroutine_wrapper", retain_object, release_object<coroutine_wrapper_object>);
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const coroutine = reinterpret_cast<coroutine_wrapper_object*>(aSelf)->coroutine;
                    for (auto const& method : sMethods)
                        if (std::strcmp(method.name, aName) == 0)
                            return make_function(method.name, call_method, const_cast<generator_method*>(&method), coroutine);
                    return error("AttributeError", std::string{ "'coroutine_wrapper' object has no attribute '" } + aName + "'");
                };
                type.iter = [](neos_py_context*, object* aSelf) -> object* { retain(aSelf); return aSelf; };
                type.next = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto& g = as_generator(reinterpret_cast<coroutine_wrapper_object*>(aSelf)->coroutine);
                    if (g.state == generator_object::Finished)
                        return error("RuntimeError", "cannot reuse already awaited coroutine");
                    if (auto const yielded = resume(g, nullptr, nullptr))
                        return yielded;
                    return stop(g);
                };
                return type;
            }

            // (an asynchronous generator's asend(), athrow() and aclose(): awaitables (their own iterators))
            struct asend_object
            {
                header h;
                object* generator = nullptr;
                object* value = nullptr;        ///< (asend: the value sent)
                object* thrown = nullptr;       ///< (athrow: the exception)
                enum mode_t { Send, Throw, Close } mode = Send;
                enum state_t { Initial, Iterating, Done } state = Initial;
                ~asend_object() { release(generator); release(value); release(thrown); }
            };

            neos_py_type make_asend_type();
            neos_py_type sAsendType = make_asend_type();

            bool is_asend(object* aObject) { return aObject != nullptr && aObject->type == &sAsendType.base; }

            object* make_asend(object* aGenerator, asend_object::mode_t aMode, object* aValue, object* aThrown)
            {
                auto const a = new asend_object{ { { &sAsendType.base } } };
                retain(aGenerator);
                a->generator = aGenerator;
                retain(aValue);
                a->value = aValue;
                retain(aThrown);
                a->thrown = aThrown;
                a->mode = aMode;
                return &a->h.base;
            }

            // (a step of an asend, athrow or aclose: 1 its generator awaits (aValue: the value its awaitable yielded), 0 it is
            // done (aValue: its result), -1 an exception)
            int asend_step(asend_object& a, object* aSent, object* aThrown, ref& aValue)
            {
                if (a.state == asend_object::Done)
                {
                    error("RuntimeError", a.mode == asend_object::Send ? "cannot reuse already awaited __anext__()/asend()" :
                        a.mode == asend_object::Throw ? "cannot reuse already awaited athrow()" : "cannot reuse already awaited aclose()");
                    return -1;
                }
                auto& g = as_generator(a.generator);
                object* sent = aSent;
                ref thrown = ref::borrowed(aThrown);
                if (a.state == asend_object::Initial)
                {
                    if (g.asyncRunning)
                    {
                        error("RuntimeError", a.mode == asend_object::Send ? "anext(): asynchronous generator is already running" :
                            a.mode == asend_object::Throw ? "athrow(): asynchronous generator is already running" : "aclose(): asynchronous generator is already running");
                        return -1;
                    }
                    a.state = asend_object::Iterating;
                    if (!thrown)
                    {
                        sent = a.value;
                        if (a.mode == asend_object::Throw)
                            thrown = ref::borrowed(a.thrown);
                        else if (a.mode == asend_object::Close)
                            thrown = ref{ fn_builtin("GeneratorExit") };
                    }
                    if (a.mode == asend_object::Close && g.state != generator_object::Suspended)
                    {
                        g.state = generator_object::Finished;
                        a.state = asend_object::Done;
                        aValue = ref::borrowed(none());
                        return 0;
                    }
                    if (g.state == generator_object::Finished)
                    {
                        a.state = asend_object::Done;
                        if (a.mode == asend_object::Throw)
                            fn_raise(thrown.get());
                        else
                            error("StopAsyncIteration", "");
                        return -1;
                    }
                }
                g.asyncRunning = true;
                g.awaiting = false;
                ref const yielded{ resume(g, sent != nullptr && kind_of(sent) == NEOS_PY_NONE ? nullptr : sent, thrown.get()) };
                if (yielded && g.awaiting)
                {
                    aValue = yielded;
                    return 1;
                }
                a.state = asend_object::Done;
                g.asyncRunning = false;
                if (yielded)
                {
                    if (a.mode == asend_object::Close)
                    {
                        error("RuntimeError", "async generator ignored GeneratorExit");
                        return -1;
                    }
                    aValue = yielded;
                    return 0;
                }
                if (failed())
                {
                    if (a.mode == asend_object::Close)
                    {
                        std::string_view const text{ error_text() };
                        if (text.starts_with("GeneratorExit") || text.starts_with("StopAsyncIteration"))
                        {
                            clear_error();
                            aValue = ref::borrowed(none());
                            return 0;
                        }
                    }
                    return -1;
                }
                if (a.mode == asend_object::Close)
                {
                    aValue = ref::borrowed(none());
                    return 0;
                }
                error("StopAsyncIteration", "");
                return -1;
            }

            // (a step of awaiting an awaitable's iterator (aSent: the value sent to it; or aThrown: an exception thrown into
            // it): 1 it yielded (aValue), 0 it finished (aValue: its result), -1 an exception)
            int await_step(object* aIterator, object* aSent, object* aThrown, ref& aValue)
            {
                if (is_asend(aIterator))
                    return asend_step(*reinterpret_cast<asend_object*>(aIterator), aSent, aThrown, aValue);
                if (aIterator->type == &sCoroutineWrapperType.base)
                    aIterator = reinterpret_cast<coroutine_wrapper_object*>(aIterator)->coroutine;
                bool const none_sent = aSent == nullptr || kind_of(aSent) == NEOS_PY_NONE;
                if (is_generator(aIterator) || is_coroutine_object(aIterator))
                {
                    auto& inner = as_generator(aIterator);
                    if (inner.kind == generator_object::Coroutine && inner.state == generator_object::Finished)
                    {
                        error("RuntimeError", "cannot reuse already awaited coroutine");
                        return -1;
                    }
                    ref yielded{ resume(inner, none_sent ? nullptr : aSent, aThrown) };
                    if (yielded)
                    {
                        aValue = yielded;
                        return 1;
                    }
                    if (failed())
                        return -1;
                    aValue = ref::borrowed(inner.result != nullptr ? inner.result : none());
                    return 0;
                }
                ref r;
                if (aThrown != nullptr)
                {
                    ref const method{ getattr(aIterator, "throw") };
                    if (!method)
                    {
                        clear_error();
                        fn_raise(aThrown);
                        return -1;
                    }
                    ref const args{ make_tuple({ (retain(aThrown), aThrown) }) };
                    r = ref{ call(method.get(), args.get(), nullptr) };
                }
                else if (!none_sent)
                    r = ref{ call_method_named(aIterator, "send", { (retain(aSent), aSent) }) };
                else if (is_instance(aIterator))
                    r = ref{ call_method_named(aIterator, "__next__", {}) };
                else
                {
                    r = ref{ next(aIterator) };
                    if (!r && !failed())
                    {
                        aValue = ref::borrowed(none());
                        return 0;
                    }
                }
                if (r)
                {
                    aValue = r;
                    return 1;
                }
                if (std::string_view{ error_text() }.starts_with("StopIteration"))
                {
                    ref const e{ take_exception() };
                    ref value{ e ? getattr(e.get(), "value") : nullptr };
                    if (!value)
                    {
                        clear_error();
                        value = ref::borrowed(none());
                    }
                    aValue = value;
                    return 0;
                }
                return -1;
            }

            // (an awaitable's iterator)
            object* awaitable_iterator(object* aAwaitable)
            {
                if (is_coroutine_object(aAwaitable) || is_asend(aAwaitable))
                    return (retain(aAwaitable), aAwaitable);
                ref const method{ getattr(aAwaitable, "__await__") };
                if (!method)
                {
                    clear_error();
                    return error("TypeError", std::string{ "'" } + type_name(aAwaitable) + "' object can't be awaited");
                }
                ref const args{ make_tuple() };
                ref it{ call(method.get(), args.get(), nullptr) };
                if (!it)
                    return nullptr;
                if (is_coroutine_object(it.get()))
                    return error("TypeError", "__await__() returned a coroutine");
                if (!is_generator(it.get()) && !is_asend(it.get()) && it.get()->type != &sCoroutineWrapperType.base)
                {
                    auto const type = python_type(it.get());
                    bool const iterator = (type != nullptr && type->next != nullptr) || kind_of(it.get()) == NEOS_PY_ITERATOR;
                    if (!iterator)
                    {
                        ref const next{ getattr(it.get(), "__next__") };
                        if (!next)
                        {
                            clear_error();
                            return error("TypeError", std::string{ "__await__() returned non-iterator of type '" } + type_name(it.get()) + "'");
                        }
                    }
                }
                return it.release_ownership();
            }

            // (aAwaitable awaited by the coroutine (or asynchronous generator) running: its result)
            object* await_value(object* aAwaitable)
            {
                auto* const g = tCurrent;
                if (g == nullptr || g->kind == generator_object::Generator)
                    return error("RuntimeError", "await outside of a coroutine");
                // (a coroutine not yet started: called on this coroutine)
                if (is_coroutine_object(aAwaitable))
                {
                    auto& inner = as_generator(aAwaitable);
                    if (inner.state == generator_object::Created && !inner.dying)
                    {
                        inner.state = generator_object::Running;
                        ref const self = ref::borrowed(aAwaitable);
                        ref result{ call(inner.function, inner.arguments, inner.keywords) };
                        inner.state = generator_object::Finished;
                        if (!result)
                        {
                            if (failed() && std::string_view{ error_text() }.starts_with("StopIteration"))
                            {
                                clear_error();
                                return error("RuntimeError", "coroutine raised StopIteration");
                            }
                            return nullptr;
                        }
                        retain(result.get());
                        release(inner.result);
                        inner.result = result.get();
                        return result.release_ownership();
                    }
                }
                ref const it{ awaitable_iterator(aAwaitable) };
                if (!it)
                    return nullptr;
                ref sent;
                ref thrown;
                for (;;)
                {
                    ref value;
                    auto const r = await_step(it.get(), sent.get(), thrown.get(), value);
                    if (r < 0)
                        return nullptr;
                    if (r == 0)
                        return value.release_ownership();
                    thrown = ref{};
                    // (its value is yielded by this coroutine (to the one that resumed it))
                    retain(value.get());
                    release(g->transfer);
                    g->transfer = value.get();
                    g->awaiting = true;
                    host()->coroutine_suspend();
                    g->awaiting = false;
                    if (g->thrown != nullptr)
                    {
                        thrown = ref{ g->thrown };
                        g->thrown = nullptr;
                        sent = ref{};
                        // (GeneratorExit: the awaitable is closed, and it propagates)
                        ref const exit{ fn_builtin("GeneratorExit") };
                        if (thrown.get() == exit.get() || (is_instance(thrown.get()) ? false : class_of(thrown.get()) == exit.get()))
                        {
                            if (ref const close{ getattr(it.get(), "close") })
                            {
                                ref const args{ make_tuple() };
                                ref const closed{ call(close.get(), args.get(), nullptr) };
                                if (!closed)
                                    return nullptr;
                            }
                            else
                                clear_error();
                            fn_raise(thrown.get());
                            return nullptr;
                        }
                        continue;
                    }
                    sent = ref{ g->transfer != nullptr ? g->transfer : (retain(none()), none()) };
                    g->transfer = nullptr;
                }
            }

            // --- asynchronous generators' methods

            object* agen_method(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& items = as<tuple_object>(aArgs)->items;
                auto const self = items.at(0u);
                std::string_view const name{ static_cast<char const*>(aData) };
                std::vector<object*> const args{ items.begin() + 1, items.end() };
                if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                    return error("TypeError", std::string{ "async_generator." } + std::string{ name } + "() takes no keyword arguments");
                if (name == "__aiter__")
                    return (retain(self), self);
                if (name == "__anext__")
                    return make_asend(self, asend_object::Send, nullptr, nullptr);
                if (name == "asend")
                {
                    if (args.size() != 1u)
                        return error("TypeError", "async_generator.asend() takes exactly one argument (" + std::to_string(args.size()) + " given)");
                    return make_asend(self, asend_object::Send, args[0], nullptr);
                }
                if (name == "athrow")
                {
                    if (args.empty() || args.size() > 3u)
                        return error("TypeError", "athrow expected at least 1 argument, got " + std::to_string(args.size()));
                    return make_asend(self, asend_object::Throw, nullptr, args[0]);
                }
                if (name == "aclose")
                    return make_asend(self, asend_object::Close, nullptr, nullptr);
                return error("AttributeError", "async_generator");
            }

            object* async_generator_attribute(object* aSelf, char const* aName)
            {
                static char const* const sNames[] = { "__aiter__", "__anext__", "asend", "athrow", "aclose" };
                for (auto n : sNames)
                    if (std::strcmp(n, aName) == 0)
                        return make_function(n, agen_method, const_cast<char*>(n), aSelf);
                return nullptr;
            }

            object* asend_send(asend_object& a, object* aSent, object* aThrown)
            {
                ref value;
                auto const r = asend_step(a, aSent, aThrown, value);
                if (r < 0)
                    return nullptr;
                if (r == 1)
                    return value.release_ownership();
                // (done: StopIteration(result))
                if (kind_of(value.get()) == NEOS_PY_NONE)
                    return error("StopIteration", "");
                ref const args{ make_tuple({ value.release_ownership() }) };
                ref const cls{ fn_builtin("StopIteration") };
                ref const e{ call(cls.get(), args.get(), nullptr) };
                if (e)
                    raise_exception(e.get());
                return nullptr;
            }

            object* asend_method(neos_py_context*, void* aData, object* aArgs, object*)
            {
                auto const& items = as<tuple_object>(aArgs)->items;
                auto& a = *reinterpret_cast<asend_object*>(items.at(0u));
                std::string_view const name{ static_cast<char const*>(aData) };
                if (name == "__await__" || name == "__iter__")
                    return (retain(items[0]), items[0]);
                if (name == "__next__")
                    return asend_send(a, nullptr, nullptr);
                if (name == "send")
                    return asend_send(a, items.size() > 1u ? items[1] : nullptr, nullptr);
                if (name == "throw")
                {
                    if (items.size() < 2u)
                        return error("TypeError", "throw expected at least 1 argument, got 0");
                    return asend_send(a, nullptr, items[1]);
                }
                if (name == "close")
                {
                    a.state = asend_object::Done;
                    return (retain(none()), none());
                }
                return nullptr;
            }

            neos_py_type make_asend_type()
            {
                auto type = make_type("async_generator_asend", "async_generator_asend", retain_object, release_object<asend_object>);
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    static char const* const sNames[] = { "__await__", "__iter__", "__next__", "send", "throw", "close" };
                    for (auto n : sNames)
                        if (std::strcmp(n, aName) == 0)
                            return make_function(n, asend_method, const_cast<char*>(n), aSelf);
                    char const* const typeName = reinterpret_cast<asend_object*>(aSelf)->mode == asend_object::Send ? "async_generator_asend" : "async_generator_athrow";
                    return error("AttributeError", std::string{ "'" } + typeName + "' object has no attribute '" + aName + "'");
                };
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    char address[32];
                    std::snprintf(address, sizeof(address), "%p", static_cast<void*>(aSelf));
                    char const* const typeName = reinterpret_cast<asend_object*>(aSelf)->mode == asend_object::Send ? "async_generator_asend" : "async_generator_athrow";
                    return make_str(std::string{ "<" } + typeName + " object at " + address + ">");
                };
                type.iter = [](neos_py_context*, object* aSelf) -> object* { retain(aSelf); return aSelf; };
                type.next = [](neos_py_context*, object* aSelf) -> object*
                {
                    return asend_send(*reinterpret_cast<asend_object*>(aSelf), nullptr, nullptr);
                };
                return type;
            }

            // --- async for and async with

            struct async_iter_object
            {
                header h;
                object* iterator;   ///< (the asynchronous iterator)
                ~async_iter_object() { release(iterator); }
            };

            neos_py_type make_async_iter_type()
            {
                auto type = make_type("async_iterator", "async_iterator", retain_object, release_object<async_iter_object>);
                type.iter = [](neos_py_context*, object* aSelf) -> object* { retain(aSelf); return aSelf; };
                type.next = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const iterator = reinterpret_cast<async_iter_object*>(aSelf)->iterator;
                    ref awaitable;
                    if (is_async_generator(iterator))
                        awaitable = ref{ make_asend(iterator, asend_object::Send, nullptr, nullptr) };
                    else
                    {
                        ref const anext{ getattr(iterator, "__anext__") };
                        if (!anext)
                        {
                            clear_error();
                            return error("TypeError", std::string{ "'async for' requires an iterator with __anext__ method, got " } + type_name(iterator));
                        }
                        ref const args{ make_tuple() };
                        awaitable = ref{ call(anext.get(), args.get(), nullptr) };
                        if (!awaitable)
                            return nullptr;
                    }
                    auto const value = await_value(awaitable.get());
                    if (value == nullptr && std::string_view{ error_text() }.starts_with("StopAsyncIteration"))
                    {
                        // (exhausted)
                        clear_error();
                        return nullptr;
                    }
                    return value;
                };
                return type;
            }

            neos_py_type sAsyncIterType = make_async_iter_type();

            struct async_with_object
            {
                header h;
                object* manager;
                object* exit;       ///< (its bound __aexit__)
                ~async_with_object() { release(manager); release(exit); }
            };

            object* async_with_enter(neos_py_context*, void*, object* aArgs, object*)
            {
                auto const& w = *reinterpret_cast<async_with_object*>(as<tuple_object>(aArgs)->items.at(0u));
                ref const enter{ getattr(w.manager, "__aenter__") };
                if (!enter)
                    return nullptr;
                ref const args{ make_tuple() };
                ref const awaitable{ call(enter.get(), args.get(), nullptr) };
                if (!awaitable)
                    return nullptr;
                return await_value(awaitable.get());
            }

            object* async_with_exit(neos_py_context*, void*, object* aArgs, object*)
            {
                auto const& items = as<tuple_object>(aArgs)->items;
                auto const& w = *reinterpret_cast<async_with_object*>(items.at(0u));
                std::vector<object*> args;
                for (std::size_t i = 1u; i < items.size(); ++i)
                    args.push_back((retain(items[i]), items[i]));
                ref const tuple{ make_tuple(std::move(args)) };
                ref const awaitable{ call(w.exit, tuple.get(), nullptr) };
                if (!awaitable)
                    return nullptr;
                return await_value(awaitable.get());
            }

            neos_py_type make_async_with_type()
            {
                auto type = make_type("async_context_manager", "async_context_manager", retain_object, release_object<async_with_object>);
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    if (std::strcmp(aName, "__enter__") == 0)
                        return make_function("__enter__", async_with_enter, nullptr, aSelf);
                    if (std::strcmp(aName, "__exit__") == 0)
                        return make_function("__exit__", async_with_exit, nullptr, aSelf);
                    return error("AttributeError", std::string{ "'async_context_manager' object has no attribute '" } + aName + "'");
                };
                return type;
            }

            neos_py_type sAsyncWithType = make_async_with_type();

            object* new_generator(neos_py_type& aType, generator_object::kind_t aKind, std::vector<object*> const& aArgs, object* aKwargs)
            {
                if (aArgs.size() < 3u || kind_of(aArgs[1]) != NEOS_PY_STR || kind_of(aArgs[2]) != NEOS_PY_STR)
                    return error("SystemError", "__generator(body, name, qualname, arguments...)");
                sProgramEnded = false;
                auto const g = new generator_object{ { { &aType.base } } };
                g->kind = aKind;
                g->name = as<str_object>(aArgs[1])->value;
                g->qualname = as<str_object>(aArgs[2])->value;
                retain(aArgs[0]);
                g->function = aArgs[0];
                std::vector<object*> arguments{ aArgs.begin() + 3, aArgs.end() };
                for (auto a : arguments)
                    retain(a);
                g->arguments = make_tuple(std::move(arguments));
                if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                {
                    retain(aKwargs);
                    g->keywords = aKwargs;
                }
                return &g->h.base;
            }
        }

        // __generator(body, name, qualname, arguments...)
        object* b_generator(std::vector<object*> const& aArgs, object* aKwargs)
        {
            return new_generator(sGeneratorType, generator_object::Generator, aArgs, aKwargs);
        }

        // __coroutine(body, name, qualname, arguments...)
        object* b_coroutine(std::vector<object*> const& aArgs, object* aKwargs)
        {
            return new_generator(sCoroutineType, generator_object::Coroutine, aArgs, aKwargs);
        }

        // __async_generator(body, name, qualname, arguments...)
        object* b_async_generator(std::vector<object*> const& aArgs, object* aKwargs)
        {
            return new_generator(sAsyncGeneratorType, generator_object::AsyncGenerator, aArgs, aKwargs);
        }

        // __await(awaitable): its result
        object* b_await(std::vector<object*> const& aArgs, object*)
        {
            if (aArgs.size() != 1u)
                return error("SystemError", "__await(awaitable)");
            return await_value(aArgs[0]);
        }

        // __async_iter(iterable): async for's iterator
        object* b_async_iter(std::vector<object*> const& aArgs, object*)
        {
            if (aArgs.size() != 1u)
                return error("SystemError", "__async_iter(iterable)");
            ref iterator;
            if (is_async_generator(aArgs[0]))
                iterator = ref::borrowed(aArgs[0]);
            else
            {
                ref const aiter{ getattr(aArgs[0], "__aiter__") };
                if (!aiter)
                {
                    clear_error();
                    return error("TypeError", std::string{ "'async for' requires an object with __aiter__ method, got " } + type_name(aArgs[0]));
                }
                ref const args{ make_tuple() };
                iterator = ref{ call(aiter.get(), args.get(), nullptr) };
                if (!iterator)
                    return nullptr;
            }
            return &(new async_iter_object{ { { &sAsyncIterType.base } }, iterator.release_ownership() })->h.base;
        }

        // __async_with(manager): async with's context manager
        object* b_async_with(std::vector<object*> const& aArgs, object*)
        {
            if (aArgs.size() != 1u)
                return error("SystemError", "__async_with(manager)");
            auto const manager = aArgs[0];
            ref enter{ getattr(manager, "__aenter__") };
            ref exit{ enter ? getattr(manager, "__aexit__") : nullptr };
            if (!enter || !exit)
            {
                clear_error();
                return error("TypeError", std::string{ "'" } + type_name(manager) + "' object does not support the asynchronous context manager protocol");
            }
            retain(manager);
            return &(new async_with_object{ { { &sAsyncWithType.base } }, manager, exit.release_ownership() })->h.base;
        }

        // (the iterator of an awaitable, and a step of awaiting it (asyncio's tasks): see await_step)
        object* fn_awaitable_iterator(object* aAwaitable)
        {
            return awaitable_iterator(aAwaitable);
        }

        int fn_await_step(object* aIterator, object* aSent, object* aThrown, object*& aValue)
        {
            ref value;
            auto const r = await_step(aIterator, aSent, aThrown, value);
            aValue = value.release_ownership();
            return r;
        }

        bool fn_is_coroutine(object* aObject)
        {
            return is_coroutine_object(aObject);
        }

        // __yield(value): the value sent (None for next())
        object* b_yield(std::vector<object*> const& aArgs, object*)
        {
            auto* const g = tCurrent;
            if (g == nullptr)
                return error("SystemError", "yield outside of a generator");
            auto const value = aArgs.empty() ? none() : aArgs[0];
            retain(value);
            release(g->transfer);
            g->transfer = value;
            host()->coroutine_suspend();
            // (resumed: an exception thrown into it, or the value sent)
            if (g->thrown != nullptr)
            {
                auto const thrown = g->thrown;
                g->thrown = nullptr;
                fn_raise(thrown);
                release(thrown);
                return nullptr;
            }
            auto const sent = g->transfer != nullptr ? g->transfer : none();
            if (g->transfer == nullptr)
                retain(sent);
            g->transfer = nullptr;
            return sent;
        }

        // __yield_from(iterable): the values of the iterable yielded (a generator's: values sent to this one are sent to it);
        // the result is its return value
        object* b_yield_from(std::vector<object*> const& aArgs, object*)
        {
            if (aArgs.size() != 1u)
                return error("SystemError", "__yield_from(iterable)");
            ref const it{ iter(aArgs[0]) };
            if (!it)
                return nullptr;
            ref sent{ (retain(none()), none()) };
            for (;;)
            {
                ref value;
                if (is_generator(it.get()))
                {
                    auto& inner = as_generator(it.get());
                    value = ref{ resume(inner, kind_of(sent.get()) == NEOS_PY_NONE ? nullptr : sent.get(), nullptr) };
                    if (!value)
                    {
                        if (failed())
                            return nullptr;
                        auto const result = inner.result != nullptr ? inner.result : none();
                        retain(result);
                        return result;
                    }
                }
                else
                {
                    value = ref{ next(it.get()) };
                    if (!value)
                        return failed() ? nullptr : (retain(none()), none());
                }
                std::vector<object*> const args{ value.get() };
                sent = ref{ b_yield(args, nullptr) };
                if (!sent)
                    return nullptr;
            }
        }

        // (a generator that has finished: StopIteration (with its return value) raised; false if not a generator)
        bool generator_stop(object* aIterator)
        {
            if (!is_generator(aIterator))
                return false;
            stop(as_generator(aIterator));
            return true;
        }

        // (the program has ended: suspended generators are not resumed)
        void generators_end()
        {
            sProgramEnded = true;
        }
    }
}
