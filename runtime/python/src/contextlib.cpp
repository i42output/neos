/*
  contextlib.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/contextlib.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module contextlib (CPython 3.13's contextlib.py: AbstractContextManager, AbstractAsyncContextManager, ContextDecorator,
// AsyncContextDecorator, contextmanager, asynccontextmanager, closing, aclosing, redirect_stdout, redirect_stderr, suppress, ExitStack,
// AsyncExitStack, nullcontext, chdir)

#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        char const* const sModule = "contextlib";

        std::map<std::string, object*, std::less<>>& classes()
        {
            static std::map<std::string, object*, std::less<>> sClasses;
            return sClasses;
        }

        py cls(char const* aName)
        {
            auto const c = classes().find(aName);
            if (c == classes().end())
                py_raise("NameError", "name '" + std::string{ aName } + "' is not defined");
            return py::borrowed(c->second);
        }
        py none() { return py::none(); }
        py boolean(bool aValue) { return py::boolean(aValue); }

        std::vector<py> arguments(char const* aFunction, std::vector<object*> const& aArgs, object* aKwargs, std::vector<char const*> const& aNames,
            std::size_t aRequired, std::size_t aPositionalOnly = 0u)
        {
            if (aArgs.size() > aNames.size())
                py_raise("TypeError", std::string{ aFunction } + "() takes " + std::to_string(aNames.size()) + " positional argument" + (aNames.size() == 1u ? "" : "s") +
                    " but " + std::to_string(aArgs.size()) + (aArgs.size() == 1u ? " was" : " were") + " given");
            std::vector<object*> a;
            py_checked(parse_arguments(aFunction, aArgs, aKwargs, aNames, aRequired, aPositionalOnly, a));
            std::vector<py> result;
            for (auto v : a)
                result.push_back(v != nullptr ? py::borrowed(v) : py{});
            return result;
        }

        // (a method's self and the rest of its positional arguments (*args))
        std::pair<py, std::vector<py>> self_and_rest(char const* aFunction, std::vector<object*> const& aArgs)
        {
            if (aArgs.empty())
                py_raise("TypeError", std::string{ aFunction } + "() missing 1 required positional argument: 'self'");
            std::vector<py> rest;
            for (std::size_t i = 1u; i < aArgs.size(); ++i)
                rest.push_back(py::borrowed(aArgs[i]));
            return { py::borrowed(aArgs[0]), rest };
        }

        // (aFunction(*aArgs, **aKwargs))
        py call_with(py const& aFunction, std::vector<py> const& aArgs, py const& aKwargs = {})
        {
            std::vector<object*> args;
            for (auto const& a : aArgs)
                args.push_back(a.reference());
            return py{ invoke(aFunction.get(), std::move(args), aKwargs.empty() || aKwargs.is_none() ? nullptr : aKwargs.get()) };
        }

        py kwargs_of(object* aKwargs)
        {
            py const result = py_dict();
            if (aKwargs != nullptr)
                py_checked(dict_update(result.get(), aKwargs));
            return result;
        }

        // (raise aException from None)
        [[noreturn]] void raise_from_none(py const& aException)
        {
            aException.set_attr("__cause__", none());
            aException.set_attr("__suppress_context__", boolean(true));
            py_raise(aException);
        }

        [[noreturn]] void raise_runtime_error_from_none(char const* aMessage) { raise_from_none(builtin("RuntimeError")(py{ aMessage })); }

        // (raise aException, then (finally) aFinally)
        [[noreturn]] void raise_then(py const& aException, std::function<void()> const& aFinally)
        {
            try
            {
                py_raise(aException);
            }
            catch (raised const&)
            {
                py const pending = py_take_exception();
                aFinally();
                py_reraise(pending);
            }
        }

        // (with aManager: aBody())
        py with_context(py const& aManager, std::function<py()> const& aBody)
        {
            py const type = type_of(aManager);
            py enter;
            py exit;
            try
            {
                enter = type.attr("__enter__");
                exit = type.attr("__exit__");
            }
            catch (raised const&)
            {
                if (!caught({ "AttributeError" }))
                    throw;
                py_raise("TypeError", "'" + type.attr("__name__").str() + "' object does not support the context manager protocol");
            }
            enter(aManager);
            py result;
            try
            {
                result = aBody();
            }
            catch (raised const&)
            {
                std::string const text = error_text();
                clear_error();
                ref const e{ fn_caught(text.c_str()) };
                py suppress;
                try
                {
                    suppress = exit(aManager, type_of(py::borrowed(e.get())), py::borrowed(e.get()), py::borrowed(e.get()).attr("__traceback__"));
                }
                catch (raised const&)
                {
                    fn_handled();
                    throw;
                }
                fn_handled();
                if (suppress.truthy())
                    return none();
                py_reraise(py::borrowed(e.get()));
            }
            exit(aManager, none(), none(), none());
            return result;
        }

        // (async with aManager: aBody() (within a coroutine))
        py async_with_context(py const& aManager, std::function<py()> const& aBody)
        {
            py const type = type_of(aManager);
            py enter;
            py exit;
            try
            {
                enter = type.attr("__aenter__");
                exit = type.attr("__aexit__");
            }
            catch (raised const&)
            {
                if (!caught({ "AttributeError" }))
                    throw;
                py_raise("TypeError", "'" + type.attr("__name__").str() + "' object does not support the asynchronous context manager protocol");
            }
            py_await(enter(aManager));
            py result;
            try
            {
                result = aBody();
            }
            catch (raised const&)
            {
                std::string const text = error_text();
                clear_error();
                ref const e{ fn_caught(text.c_str()) };
                py suppress;
                try
                {
                    suppress = py_await(exit(aManager, type_of(py::borrowed(e.get())), py::borrowed(e.get()), py::borrowed(e.get()).attr("__traceback__")));
                }
                catch (raised const&)
                {
                    fn_handled();
                    throw;
                }
                fn_handled();
                if (suppress.truthy())
                    return none();
                py_reraise(py::borrowed(e.get()));
            }
            py_await(exit(aManager, none(), none(), none()));
            return result;
        }

        py coroutine(char const* aName, std::string const& aQualname, std::function<py()> aBody)
        {
            return make_native_coroutine(aName, aQualname, sModule, std::move(aBody));
        }

        py wraps(py const& aWrapped, py const& aWrapper) { return import_attribute("functools", "wraps")(aWrapped)(aWrapper); }

        // --- AbstractContextManager and AbstractAsyncContextManager

        builtin_entry const sAbstractContextManagerMethods[] =
        {
            { "__enter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return arguments("AbstractContextManager.__enter__", aArgs, aKwargs, { "self" }, 1u)[0]; }); } },
            { "__exit__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { arguments("AbstractContextManager.__exit__", aArgs, aKwargs, { "self", "exc_type", "exc_value", "traceback" }, 4u); return none(); }); } },
            { "__subclasshook__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("__subclasshook__", aArgs, aKwargs, { "cls", "C" }, 2u);
                        if (a[0].is(cls("AbstractContextManager")))
                            return import_attribute("_collections_abc", "_check_methods")(a[1], py{ "__enter__" }, py{ "__exit__" });
                        return py::borrowed(not_implemented());
                    });
                } }
        };

        builtin_entry const sAbstractAsyncContextManagerMethods[] =
        {
            { "__aenter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("AbstractAsyncContextManager.__aenter__", aArgs, aKwargs, { "self" }, 1u)[0];
                        return coroutine("__aenter__", "AbstractAsyncContextManager.__aenter__", [self]() { return self; });
                    });
                } },
            { "__aexit__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        arguments("AbstractAsyncContextManager.__aexit__", aArgs, aKwargs, { "self", "exc_type", "exc_value", "traceback" }, 4u);
                        return coroutine("__aexit__", "AbstractAsyncContextManager.__aexit__", []() { return none(); });
                    });
                } },
            { "__subclasshook__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("__subclasshook__", aArgs, aKwargs, { "cls", "C" }, 2u);
                        if (a[0].is(cls("AbstractAsyncContextManager")))
                            return import_attribute("_collections_abc", "_check_methods")(a[1], py{ "__aenter__" }, py{ "__aexit__" });
                        return py::borrowed(not_implemented());
                    });
                } }
        };

        // --- ContextDecorator and AsyncContextDecorator

        builtin_entry const sContextDecoratorMethods[] =
        {
            { "_recreate_cm", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return arguments("ContextDecorator._recreate_cm", aArgs, aKwargs, { "self" }, 1u)[0]; }); } },
            { "__call__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("ContextDecorator.__call__", aArgs, aKwargs, { "self", "func" }, 2u);
                        py const self = a[0];
                        py const func = a[1];
                        py const inner = make_native_function("inner", "ContextDecorator.__call__.<locals>.inner", "", sModule,
                            [self, func](std::vector<py> const& aInnerArgs, object* aInnerKwargs) -> py
                            {
                                py const kwds = kwargs_of(aInnerKwargs);
                                return with_context(self.method("_recreate_cm"), [&]() { return call_with(func, aInnerArgs, kwds); });
                            });
                        return wraps(func, inner);
                    });
                } }
        };

        builtin_entry const sAsyncContextDecoratorMethods[] =
        {
            { "_recreate_cm", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return arguments("AsyncContextDecorator._recreate_cm", aArgs, aKwargs, { "self" }, 1u)[0]; }); } },
            { "__call__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("AsyncContextDecorator.__call__", aArgs, aKwargs, { "self", "func" }, 2u);
                        py const self = a[0];
                        py const func = a[1];
                        py const inner = make_native_function("inner", "AsyncContextDecorator.__call__.<locals>.inner", "", sModule,
                            [self, func](std::vector<py> const& aInnerArgs, object* aInnerKwargs) -> py
                            {
                                py const kwds = kwargs_of(aInnerKwargs);
                                return coroutine("inner", "AsyncContextDecorator.__call__.<locals>.inner", [self, func, aInnerArgs, kwds]()
                                {
                                    return async_with_context(self.method("_recreate_cm"), [&]() { return py_await(call_with(func, aInnerArgs, kwds)); });
                                });
                            });
                        return wraps(func, inner);
                    });
                } }
        };

        // --- _GeneratorContextManagerBase, _GeneratorContextManager and _AsyncGeneratorContextManager

        builtin_entry const sGeneratorContextManagerBaseMethods[] =
        {
            { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("_GeneratorContextManagerBase.__init__", aArgs, aKwargs, { "self", "func", "args", "kwds" }, 4u);
                        py const self = a[0];
                        self.set_attr("gen", call_with(a[1], a[2].items(), a[3]));
                        self.set_attr("func", a[1]);
                        self.set_attr("args", a[2]);
                        self.set_attr("kwds", a[3]);
                        py doc = builtin("getattr")(a[1], py{ "__doc__" }, none());
                        if (doc.is_none())
                            doc = type_of(self).attr("__doc__");
                        self.set_attr("__doc__", doc);
                        return none();
                    });
                } },
            { "_recreate_cm", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("_GeneratorContextManagerBase._recreate_cm", aArgs, aKwargs, { "self" }, 1u)[0];
                        return self.attr("__class__")(self.attr("func"), self.attr("args"), self.attr("kwds"));
                    });
                } }
        };

        void forget_arguments(py const& aSelf)
        {
            for (auto name : { "args", "kwds", "func" })
                py_checked(setattr(aSelf.get(), name, nullptr) == 0);
        }

        // (the end of an __exit__ or __aexit__ whose generator did not stop: RuntimeError(aMessage), then (finally) the generator is closed)
        [[noreturn]] void did_not_stop(py const& aGenerator, char const* aMessage, bool aAsync)
        {
            raise_then(builtin("RuntimeError")(py{ aMessage }), [&]()
            {
                if (aAsync)
                    py_await(aGenerator.method("aclose"));
                else
                    aGenerator.method("close");
            });
        }

        // (__exit__'s and __aexit__'s handling of the exception that throw() or athrow() raised: False (it is the one passed in); otherwise it
        // is raised again)
        py exit_exception(py const& aValue, py const& aTraceback, char const* aStop, bool aAsync)
        {
            py const exc = py_take_exception();
            if (isinstance(exc, builtin(aStop)))
                return boolean(!exc.is(aValue));
            if (isinstance(exc, builtin("RuntimeError")))
            {
                if (exc.is(aValue))
                {
                    exc.set_attr("__traceback__", aTraceback);
                    return boolean(false);
                }
                py const stops = aAsync ? py_tuple({ builtin("StopIteration"), builtin("StopAsyncIteration") }) : builtin("StopIteration");
                if (isinstance(aValue, stops) && exc.attr("__cause__").is(aValue))
                {
                    aValue.set_attr("__traceback__", aTraceback);
                    return boolean(false);
                }
                py_reraise(exc);
            }
            if (!exc.is(aValue))
                py_reraise(exc);
            exc.set_attr("__traceback__", aTraceback);
            return boolean(false);
        }

        builtin_entry const sGeneratorContextManagerMethods[] =
        {
            { "__enter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("_GeneratorContextManager.__enter__", aArgs, aKwargs, { "self" }, 1u)[0];
                        forget_arguments(self);
                        try
                        {
                            return builtin("next")(self.attr("gen"));
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "StopIteration" }))
                                throw;
                            raise_runtime_error_from_none("generator didn't yield");
                        }
                    });
                } },
            { "__exit__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("_GeneratorContextManager.__exit__", aArgs, aKwargs, { "self", "typ", "value", "traceback" }, 4u);
                        py const gen = a[0].attr("gen");
                        if (a[1].is_none())
                        {
                            try
                            {
                                builtin("next")(gen);
                            }
                            catch (raised const&)
                            {
                                if (!caught({ "StopIteration" }))
                                    throw;
                                return boolean(false);
                            }
                            did_not_stop(gen, "generator didn't stop", false);
                        }
                        py value = a[2];
                        if (value.is_none())
                            value = a[1]();
                        try
                        {
                            gen.method("throw", { value });
                        }
                        catch (raised const&)
                        {
                            return exit_exception(value, a[3], "StopIteration", false);
                        }
                        did_not_stop(gen, "generator didn't stop after throw()", false);
                    });
                } }
        };

        builtin_entry const sAsyncGeneratorContextManagerMethods[] =
        {
            { "__aenter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("_AsyncGeneratorContextManager.__aenter__", aArgs, aKwargs, { "self" }, 1u)[0];
                        return coroutine("__aenter__", "_AsyncGeneratorContextManager.__aenter__", [self]() -> py
                        {
                            forget_arguments(self);
                            try
                            {
                                return py_await(builtin("anext")(self.attr("gen")));
                            }
                            catch (raised const&)
                            {
                                if (!caught({ "StopAsyncIteration" }))
                                    throw;
                                raise_runtime_error_from_none("generator didn't yield");
                            }
                        });
                    });
                } },
            { "__aexit__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("_AsyncGeneratorContextManager.__aexit__", aArgs, aKwargs, { "self", "typ", "value", "traceback" }, 4u);
                        return coroutine("__aexit__", "_AsyncGeneratorContextManager.__aexit__", [a]() -> py
                        {
                            py const gen = a[0].attr("gen");
                            if (a[1].is_none())
                            {
                                try
                                {
                                    py_await(builtin("anext")(gen));
                                }
                                catch (raised const&)
                                {
                                    if (!caught({ "StopAsyncIteration" }))
                                        throw;
                                    return boolean(false);
                                }
                                did_not_stop(gen, "generator didn't stop", true);
                            }
                            py value = a[2];
                            if (value.is_none())
                                value = a[1]();
                            try
                            {
                                py_await(gen.method("athrow", { value }));
                            }
                            catch (raised const&)
                            {
                                return exit_exception(value, a[3], "StopAsyncIteration", true);
                            }
                            did_not_stop(gen, "generator didn't stop after athrow()", true);
                        });
                    });
                } }
        };

        // --- closing, aclosing, redirect_stdout, redirect_stderr, suppress, nullcontext and chdir

        builtin_entry const sClosingMethods[] =
        {
            { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("closing.__init__", aArgs, aKwargs, { "self", "thing" }, 2u);
                        a[0].set_attr("thing", a[1]);
                        return none();
                    });
                } },
            { "__enter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return arguments("closing.__enter__", aArgs, aKwargs, { "self" }, 1u)[0].attr("thing"); }); } },
            { "__exit__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const [self, rest] = self_and_rest("closing.__exit__", aArgs);
                        self.attr("thing").method("close");
                        return none();
                    });
                } }
        };

        builtin_entry const sAclosingMethods[] =
        {
            { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("aclosing.__init__", aArgs, aKwargs, { "self", "thing" }, 2u);
                        a[0].set_attr("thing", a[1]);
                        return none();
                    });
                } },
            { "__aenter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("aclosing.__aenter__", aArgs, aKwargs, { "self" }, 1u)[0];
                        return coroutine("__aenter__", "aclosing.__aenter__", [self]() { return self.attr("thing"); });
                    });
                } },
            { "__aexit__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = self_and_rest("aclosing.__aexit__", aArgs).first;
                        return coroutine("__aexit__", "aclosing.__aexit__", [self]() { py_await(self.attr("thing").method("aclose")); return none(); });
                    });
                } }
        };

        builtin_entry const sRedirectStreamMethods[] =
        {
            { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("_RedirectStream.__init__", aArgs, aKwargs, { "self", "new_target" }, 2u);
                        a[0].set_attr("_new_target", a[1]);
                        a[0].set_attr("_old_targets", py_list());
                        return none();
                    });
                } },
            { "__enter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("_RedirectStream.__enter__", aArgs, aKwargs, { "self" }, 1u)[0];
                        py const sys{ fn_import("sys") };
                        auto const stream = self.attr("_stream").str();
                        self.attr("_old_targets").method("append", { sys.attr(stream.c_str()) });
                        sys.set_attr(stream.c_str(), self.attr("_new_target"));
                        return self.attr("_new_target");
                    });
                } },
            { "__exit__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("_RedirectStream.__exit__", aArgs, aKwargs, { "self", "exctype", "excinst", "exctb" }, 4u);
                        py const sys{ fn_import("sys") };
                        sys.set_attr(a[0].attr("_stream").str().c_str(), a[0].attr("_old_targets").method("pop"));
                        return none();
                    });
                } }
        };

        builtin_entry const sSuppressMethods[] =
        {
            { "__init__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const [self, rest] = self_and_rest("suppress.__init__", aArgs);
                        self.set_attr("_exceptions", py_tuple(rest));
                        return none();
                    });
                } },
            { "__enter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { arguments("suppress.__enter__", aArgs, aKwargs, { "self" }, 1u); return none(); }); } },
            { "__exit__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("suppress.__exit__", aArgs, aKwargs, { "self", "exctype", "excinst", "exctb" }, 4u);
                        if (a[1].is_none())
                            return none();
                        py const exceptions = a[0].attr("_exceptions");
                        if (issubclass(a[1], exceptions))
                            return boolean(true);
                        if (issubclass(a[1], builtin("BaseExceptionGroup")))
                        {
                            py const split = a[2].method("split", { exceptions });
                            py const rest = split[py{ 1 }];
                            if (rest.is_none())
                                return boolean(true);
                            py_raise(rest);
                        }
                        return boolean(false);
                    });
                } }
        };

        builtin_entry const sNullContextMethods[] =
        {
            { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("nullcontext.__init__", aArgs, aKwargs, { "self", "enter_result" }, 1u);
                        a[0].set_attr("enter_result", a[1].empty() ? none() : a[1]);
                        return none();
                    });
                } },
            { "__enter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return arguments("nullcontext.__enter__", aArgs, aKwargs, { "self" }, 1u)[0].attr("enter_result"); }); } },
            { "__exit__", [](std::vector<object*> const& aArgs, object*) -> object*
                { return py_guarded([&]() { self_and_rest("nullcontext.__exit__", aArgs); return none(); }); } },
            { "__aenter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("nullcontext.__aenter__", aArgs, aKwargs, { "self" }, 1u)[0];
                        return coroutine("__aenter__", "nullcontext.__aenter__", [self]() { return self.attr("enter_result"); });
                    });
                } },
            { "__aexit__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    return py_guarded([&]()
                    {
                        self_and_rest("nullcontext.__aexit__", aArgs);
                        return coroutine("__aexit__", "nullcontext.__aexit__", []() { return none(); });
                    });
                } }
        };

        builtin_entry const sChdirMethods[] =
        {
            { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("chdir.__init__", aArgs, aKwargs, { "self", "path" }, 2u);
                        a[0].set_attr("path", a[1]);
                        a[0].set_attr("_old_cwd", py_list());
                        return none();
                    });
                } },
            { "__enter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("chdir.__enter__", aArgs, aKwargs, { "self" }, 1u)[0];
                        self.attr("_old_cwd").method("append", { import_attribute("os", "getcwd")() });
                        import_attribute("os", "chdir")(self.attr("path"));
                        return none();
                    });
                } },
            { "__exit__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = self_and_rest("chdir.__exit__", aArgs).first;
                        import_attribute("os", "chdir")(self.attr("_old_cwd").method("pop"));
                        return none();
                    });
                } }
        };

        // --- _BaseExitStack, ExitStack and AsyncExitStack

        // (_create_cb_wrapper(callback, /, *args, **kwds) and _create_async_cb_wrapper: _exit_wrapper(exc_type, exc, tb))
        py callback_wrapper(std::vector<object*> const& aArgs, object* aKwargs, bool aAsync)
        {
            char const* const name = aAsync ? "_create_async_cb_wrapper" : "_create_cb_wrapper";
            if (aArgs.empty())
                py_raise("TypeError", std::string{ name } + "() missing 1 required positional argument: 'callback'");
            py const callback = py::borrowed(aArgs[0]);
            std::vector<py> args;
            for (std::size_t i = 1u; i < aArgs.size(); ++i)
                args.push_back(py::borrowed(aArgs[i]));
            py const kwds = kwargs_of(aKwargs);
            std::string const qualname = std::string{ aAsync ? "AsyncExitStack." : "_BaseExitStack." } + name + ".<locals>._exit_wrapper";
            return make_native_function("_exit_wrapper", qualname, "", sModule, [callback, args, kwds, aAsync, qualname](std::vector<py> const& aWrapperArgs, object*) -> py
            {
                if (aWrapperArgs.size() != 3u)
                    py_raise("TypeError", "_exit_wrapper() takes 3 positional arguments but " + std::to_string(aWrapperArgs.size()) + " were given");
                if (!aAsync)
                {
                    call_with(callback, args, kwds);
                    return none();
                }
                return coroutine("_exit_wrapper", qualname, [callback, args, kwds]() { py_await(call_with(callback, args, kwds)); return none(); });
            });
        }

        // (MethodType(cm_exit, cm))
        py exit_wrapper(std::vector<object*> const& aArgs, object* aKwargs, char const* aName)
        {
            auto const a = arguments(aName, aArgs, aKwargs, { "cm", "cm_exit" }, 2u);
            return import_attribute("types", "MethodType")(a[1], a[0]);
        }

        void push_exit_callback(py const& aSelf, py const& aCallback, bool aSync)
        {
            aSelf.method("_push_exit_callback", { aCallback, boolean(aSync) });
        }

        builtin_entry const sBaseExitStackMethods[] =
        {
            { "_create_exit_wrapper", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return exit_wrapper(aArgs, aKwargs, "_create_exit_wrapper"); }); } },
            { "_create_cb_wrapper", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return callback_wrapper(aArgs, aKwargs, false); }); } },
            { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("_BaseExitStack.__init__", aArgs, aKwargs, { "self" }, 1u)[0];
                        self.set_attr("_exit_callbacks", import_attribute("collections", "deque")());
                        return none();
                    });
                } },
            { "pop_all", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("_BaseExitStack.pop_all", aArgs, aKwargs, { "self" }, 1u)[0];
                        py const newStack = type_of(self)();
                        newStack.set_attr("_exit_callbacks", self.attr("_exit_callbacks"));
                        self.set_attr("_exit_callbacks", import_attribute("collections", "deque")());
                        return newStack;
                    });
                } },
            { "push", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("_BaseExitStack.push", aArgs, aKwargs, { "self", "exit" }, 2u);
                        py exitMethod;
                        try
                        {
                            exitMethod = type_of(a[1]).attr("__exit__");
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "AttributeError" }))
                                throw;
                            push_exit_callback(a[0], a[1], true);
                            return a[1];
                        }
                        a[0].method("_push_cm_exit", { a[1], exitMethod });
                        return a[1];
                    });
                } },
            { "enter_context", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("_BaseExitStack.enter_context", aArgs, aKwargs, { "self", "cm" }, 2u);
                        py const type = type_of(a[1]);
                        py enter;
                        py exit;
                        try
                        {
                            enter = type.attr("__enter__");
                            exit = type.attr("__exit__");
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "AttributeError" }))
                                throw;
                            raise_from_none(builtin("TypeError")(py{ "'" + type.attr("__module__").str() + "." + type.attr("__qualname__").str() +
                                "' object does not support the context manager protocol" }));
                        }
                        py const result = enter(a[1]);
                        a[0].method("_push_cm_exit", { a[1], exit });
                        return result;
                    });
                } },
            { "callback", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        if (aArgs.size() < 2u)
                            py_raise("TypeError", "_BaseExitStack.callback() missing 1 required positional argument: 'callback'");
                        py const self = py::borrowed(aArgs[0]);
                        std::vector<object*> const rest{ aArgs.begin() + 1, aArgs.end() };
                        std::vector<object*> args;
                        for (auto r : rest)
                            args.push_back(new_reference(r));
                        py const wrapper{ invoke(self.attr("_create_cb_wrapper").get(), std::move(args), aKwargs) };
                        wrapper.set_attr("__wrapped__", py::borrowed(aArgs[1]));
                        push_exit_callback(self, wrapper, true);
                        return py::borrowed(aArgs[1]);
                    });
                } },
            { "_push_cm_exit", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("_BaseExitStack._push_cm_exit", aArgs, aKwargs, { "self", "cm", "cm_exit" }, 3u);
                        push_exit_callback(a[0], a[0].method("_create_exit_wrapper", { a[1], a[2] }), true);
                        return none();
                    });
                } },
            { "_push_exit_callback", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("_BaseExitStack._push_exit_callback", aArgs, aKwargs, { "self", "callback", "is_sync" }, 2u);
                        a[0].attr("_exit_callbacks").method("append", { py_tuple({ a[2].empty() ? boolean(true) : a[2], a[1] }) });
                        return none();
                    });
                } }
        };

        // (ExitStack.__exit__ and AsyncExitStack.__aexit__ (aAsync: awaiting the callbacks that are not synchronous))
        py unwind(py const& aSelf, py exc, bool aAsync)
        {
            bool const receivedExc = !exc.is_none();
            py const frameExc = import_attribute("sys", "exception")();
            auto const fixExceptionContext = [&](py newExc, py const& aOldExc)
            {
                for (;;)
                {
                    py const excContext = newExc.attr("__context__");
                    if (excContext.is_none() || excContext.is(aOldExc))
                        return;
                    if (excContext.is(frameExc))
                        break;
                    newExc = excContext;
                }
                newExc.set_attr("__context__", aOldExc);
            };
            bool suppressedExc = false;
            bool pendingRaise = false;
            py const callbacks = aSelf.attr("_exit_callbacks");
            while (callbacks.truthy())
            {
                py const entry = callbacks.method("pop");
                bool const isSync = entry[py{ 0 }].truthy();
                py const cb = entry[py{ 1 }];
                try
                {
                    py const details = exc.is_none() ? py_tuple({ none(), none(), none() }) : py_tuple({ type_of(exc), exc, exc.attr("__traceback__") });
                    py result = call_with(cb, details.items());
                    if (!isSync)
                        result = py_await(result);
                    if (result.truthy())
                    {
                        suppressedExc = true;
                        pendingRaise = false;
                        exc = none();
                    }
                }
                catch (raised const&)
                {
                    py const newExc = py_take_exception();
                    fixExceptionContext(newExc, exc);
                    pendingRaise = true;
                    exc = newExc;
                }
            }
            if (pendingRaise)
                py_reraise(exc);
            return boolean(receivedExc && suppressedExc);
        }

        builtin_entry const sExitStackMethods[] =
        {
            { "__enter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return arguments("ExitStack.__enter__", aArgs, aKwargs, { "self" }, 1u)[0]; }); } },
            { "__exit__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const [self, details] = self_and_rest("ExitStack.__exit__", aArgs);
                        if (details.size() < 2u)
                            py_raise("IndexError", "tuple index out of range");
                        return unwind(self, details[1], false);
                    });
                } },
            { "close", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("ExitStack.close", aArgs, aKwargs, { "self" }, 1u)[0];
                        self.method("__exit__", { none(), none(), none() });
                        return none();
                    });
                } }
        };

        builtin_entry const sAsyncExitStackMethods[] =
        {
            { "_create_async_exit_wrapper", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return exit_wrapper(aArgs, aKwargs, "_create_async_exit_wrapper"); }); } },
            { "_create_async_cb_wrapper", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return callback_wrapper(aArgs, aKwargs, true); }); } },
            { "enter_async_context", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("AsyncExitStack.enter_async_context", aArgs, aKwargs, { "self", "cm" }, 2u);
                        return coroutine("enter_async_context", "AsyncExitStack.enter_async_context", [a]() -> py
                        {
                            py const type = type_of(a[1]);
                            py enter;
                            py exit;
                            try
                            {
                                enter = type.attr("__aenter__");
                                exit = type.attr("__aexit__");
                            }
                            catch (raised const&)
                            {
                                if (!caught({ "AttributeError" }))
                                    throw;
                                raise_from_none(builtin("TypeError")(py{ "'" + type.attr("__module__").str() + "." + type.attr("__qualname__").str() +
                                    "' object does not support the asynchronous context manager protocol" }));
                            }
                            py const result = py_await(enter(a[1]));
                            a[0].method("_push_async_cm_exit", { a[1], exit });
                            return result;
                        });
                    });
                } },
            { "push_async_exit", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("AsyncExitStack.push_async_exit", aArgs, aKwargs, { "self", "exit" }, 2u);
                        py exitMethod;
                        try
                        {
                            exitMethod = type_of(a[1]).attr("__aexit__");
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "AttributeError" }))
                                throw;
                            push_exit_callback(a[0], a[1], false);
                            return a[1];
                        }
                        a[0].method("_push_async_cm_exit", { a[1], exitMethod });
                        return a[1];
                    });
                } },
            { "push_async_callback", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        if (aArgs.size() < 2u)
                            py_raise("TypeError", "AsyncExitStack.push_async_callback() missing 1 required positional argument: 'callback'");
                        py const self = py::borrowed(aArgs[0]);
                        std::vector<object*> args;
                        for (std::size_t i = 1u; i < aArgs.size(); ++i)
                            args.push_back(new_reference(aArgs[i]));
                        py const wrapper{ invoke(self.attr("_create_async_cb_wrapper").get(), std::move(args), aKwargs) };
                        wrapper.set_attr("__wrapped__", py::borrowed(aArgs[1]));
                        push_exit_callback(self, wrapper, false);
                        return py::borrowed(aArgs[1]);
                    });
                } },
            { "aclose", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("AsyncExitStack.aclose", aArgs, aKwargs, { "self" }, 1u)[0];
                        return coroutine("aclose", "AsyncExitStack.aclose", [self]() { py_await(self.method("__aexit__", { none(), none(), none() })); return none(); });
                    });
                } },
            { "_push_async_cm_exit", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("AsyncExitStack._push_async_cm_exit", aArgs, aKwargs, { "self", "cm", "cm_exit" }, 3u);
                        push_exit_callback(a[0], a[0].method("_create_async_exit_wrapper", { a[1], a[2] }), false);
                        return none();
                    });
                } },
            { "__aenter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("AsyncExitStack.__aenter__", aArgs, aKwargs, { "self" }, 1u)[0];
                        return coroutine("__aenter__", "AsyncExitStack.__aenter__", [self]() { return self; });
                    });
                } },
            { "__aexit__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const [self, details] = self_and_rest("AsyncExitStack.__aexit__", aArgs);
                        if (details.size() < 2u)
                            py_raise("IndexError", "tuple index out of range");
                        py const exc = details[1];
                        py const owner = self;
                        return coroutine("__aexit__", "AsyncExitStack.__aexit__", [owner, exc]() { return unwind(owner, exc, true); });
                    });
                } }
        };

        // --- the module

        // (a class: aMethods (those in aStatic are staticmethods, those in aClass classmethods (__subclasshook__ is), those in aAbstract abstract),
        // with other attributes; its metaclass is its bases')
        py make_contextlib_class_of(char const* aName, std::vector<char const*> const& aBases, builtin_entry const* aMethods, std::size_t aCount, char const* aDoc,
            std::vector<char const*> const& aAbstract, std::vector<char const*> const& aStatic, std::vector<std::pair<char const*, py>> const& aExtra)
        {
            py const ns = py_dict();
            ns.set_item(py{ "__module__" }, py{ sModule });
            ns.set_item(py{ "__qualname__" }, py{ aName });
            if (aDoc != nullptr)
                ns.set_item(py{ "__doc__" }, py{ aDoc });
            auto const named = [](std::vector<char const*> const& aNames, char const* aMethod)
            {
                for (auto n : aNames)
                    if (std::strcmp(n, aMethod) == 0)
                        return true;
                return false;
            };
            for (std::size_t i = 0u; i < aCount; ++i)
            {
                auto const& m = aMethods[i];
                py f{ make_builtin(m) };
                if (named(aAbstract, m.name))
                    f = import_attribute("abc", "abstractmethod")(f);
                if (std::strcmp(m.name, "__subclasshook__") == 0)
                    f = builtin("classmethod")(f);
                else if (named(aStatic, m.name))
                    f = builtin("staticmethod")(f);
                ns.set_item(py{ m.name }, f);
            }
            for (auto const& [name, value] : aExtra)
                ns.set_item(py{ name }, value);
            std::vector<py> bases;
            py meta = builtin("type");
            for (auto b : aBases)
            {
                py const base = std::strcmp(b, "ABC") == 0 ? import_attribute("abc", "ABC") : std::strcmp(b, "object") == 0 ? builtin("object") : cls(b);
                bases.push_back(base);
                py const t = type_of(base);
                if (!t.is(meta) && issubclass(t, meta))
                    meta = t;
            }
            py const result = meta(py{ aName }, py_tuple(bases), ns);
            classes()[aName] = result.reference();
            return result;
        }

        template <std::size_t N>
        py make_contextlib_class(char const* aName, std::vector<char const*> const& aBases, builtin_entry const (&aMethods)[N], char const* aDoc,
            std::vector<char const*> const& aAbstract = {}, std::vector<char const*> const& aStatic = {}, std::vector<std::pair<char const*, py>> const& aExtra = {})
        {
            return make_contextlib_class_of(aName, aBases, aMethods, N, aDoc, aAbstract, aStatic, aExtra);
        }

        // (contextmanager(func) and asynccontextmanager(func): wraps(func)(helper), whose helper(*args, **kwds) makes the class's manager)
        py context_manager_decorator(py const& aFunction, char const* aClass, char const* aDecorator)
        {
            std::string const className = aClass;
            py const helper = make_native_function("helper", std::string{ aDecorator } + ".<locals>.helper", "", sModule,
                [aFunction, className](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    return cls(className.c_str())(aFunction, py_tuple(aArgs), kwargs_of(aKwargs));
                });
            return wraps(aFunction, helper);
        }

        builtin_entry const sContextlibFunctions[] =
        {
            { "contextmanager", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("contextmanager", aArgs, aKwargs, { "func" }, 1u);
                        return context_manager_decorator(a[0], "_GeneratorContextManager", "contextmanager");
                    });
                } },
            { "asynccontextmanager", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("asynccontextmanager", aArgs, aKwargs, { "func" }, 1u);
                        return context_manager_decorator(a[0], "_AsyncGeneratorContextManager", "asynccontextmanager");
                    });
                } }
        };

        bool make_classes()
        {
            if (!classes().empty())
                return true;
            auto const result = py_guarded([&]()
            {
                py const classGetitem = builtin("classmethod")(import_attribute("types", "GenericAlias"));
                std::vector<std::pair<char const*, py>> const abstractExtra{ { "__class_getitem__", classGetitem }, { "__slots__", py_tuple() } };
                make_contextlib_class("AbstractContextManager", { "ABC" }, sAbstractContextManagerMethods, "An abstract base class for context managers.",
                    { "__exit__" }, {}, abstractExtra);
                make_contextlib_class("AbstractAsyncContextManager", { "ABC" }, sAbstractAsyncContextManagerMethods, "An abstract base class for asynchronous context managers.",
                    { "__aexit__" }, {}, abstractExtra);
                make_contextlib_class("ContextDecorator", { "object" }, sContextDecoratorMethods, "A base class or mixin that enables context managers to work as decorators.");
                make_contextlib_class("AsyncContextDecorator", { "object" }, sAsyncContextDecoratorMethods,
                    "A base class or mixin that enables async context managers to work as decorators.");
                make_contextlib_class("_GeneratorContextManagerBase", { "object" }, sGeneratorContextManagerBaseMethods, "Shared functionality for @contextmanager and @asynccontextmanager.");
                make_contextlib_class("_GeneratorContextManager", { "_GeneratorContextManagerBase", "AbstractContextManager", "ContextDecorator" },
                    sGeneratorContextManagerMethods, "Helper for @contextmanager decorator.");
                make_contextlib_class("_AsyncGeneratorContextManager", { "_GeneratorContextManagerBase", "AbstractAsyncContextManager", "AsyncContextDecorator" },
                    sAsyncGeneratorContextManagerMethods, "Helper for @asynccontextmanager decorator.");
                make_contextlib_class("closing", { "AbstractContextManager" }, sClosingMethods,
                    "Context to automatically close something at the end of a block.\n\n    Code like this:\n\n        with closing(<module>.open(<arguments>)) as f:\n"
                    "            <block>\n\n    is equivalent to this:\n\n        f = <module>.open(<arguments>)\n        try:\n            <block>\n        finally:\n"
                    "            f.close()\n\n    ");
                make_contextlib_class("aclosing", { "AbstractAsyncContextManager" }, sAclosingMethods,
                    "Async context manager for safely finalizing an asynchronously cleaned-up\n    resource such as an async generator, calling its ``aclose()`` method.\n\n"
                    "    Code like this:\n\n        async with aclosing(<module>.fetch(<arguments>)) as agen:\n            <block>\n\n    is equivalent to this:\n\n"
                    "        agen = <module>.fetch(<arguments>)\n        try:\n            <block>\n        finally:\n            await agen.aclose()\n\n    ");
                make_contextlib_class("_RedirectStream", { "AbstractContextManager" }, sRedirectStreamMethods, nullptr, {}, {}, { { "_stream", none() } });
                make_contextlib_class_of("redirect_stdout", { "_RedirectStream" }, nullptr, 0u,
                    "Context manager for temporarily redirecting stdout to another file.\n\n        # How to send help() to stderr\n        with redirect_stdout(sys.stderr):\n"
                    "            help(dir)\n\n        # How to write help() to a file\n        with open('help.txt', 'w') as f:\n            with redirect_stdout(f):\n"
                    "                help(pow)\n    ", {}, {}, { { "_stream", py{ "stdout" } } });
                make_contextlib_class_of("redirect_stderr", { "_RedirectStream" }, nullptr, 0u, "Context manager for temporarily redirecting stderr to another file.", {}, {},
                    { { "_stream", py{ "stderr" } } });
                make_contextlib_class("suppress", { "AbstractContextManager" }, sSuppressMethods,
                    "Context manager to suppress specified exceptions\n\n    After the exception is suppressed, execution proceeds with the next\n"
                    "    statement following the with statement.\n\n         with suppress(FileNotFoundError):\n             os.remove(somefile)\n"
                    "         # Execution still resumes here if the file was already removed\n    ");
                make_contextlib_class("_BaseExitStack", { "object" }, sBaseExitStackMethods, "A base class for ExitStack and AsyncExitStack.", {},
                    { "_create_exit_wrapper", "_create_cb_wrapper" });
                make_contextlib_class("ExitStack", { "_BaseExitStack", "AbstractContextManager" }, sExitStackMethods,
                    "Context manager for dynamic management of a stack of exit callbacks.\n\n    For example:\n        with ExitStack() as stack:\n"
                    "            files = [stack.enter_context(open(fname)) for fname in filenames]\n            # All opened files will automatically be closed at the end of\n"
                    "            # the with statement, even if attempts to open files later\n            # in the list raise an exception.\n    ");
                make_contextlib_class("AsyncExitStack", { "_BaseExitStack", "AbstractAsyncContextManager" }, sAsyncExitStackMethods,
                    "Async context manager for dynamic management of a stack of exit\n    callbacks.\n\n    For example:\n        async with AsyncExitStack() as stack:\n"
                    "            connections = [await stack.enter_async_context(get_connection())\n                for i in range(5)]\n"
                    "            # All opened connections will automatically be released at the\n            # end of the async with statement, even if attempts to open a\n"
                    "            # connection later in the list raise an exception.\n    ", {}, { "_create_async_exit_wrapper", "_create_async_cb_wrapper" });
                make_contextlib_class("nullcontext", { "AbstractContextManager", "AbstractAsyncContextManager" }, sNullContextMethods,
                    "Context manager that does no additional processing.\n\n    Used as a stand-in for a normal context manager, when a particular\n"
                    "    block of code is only sometimes used with a normal context manager:\n\n    cm = optional_cm if condition else nullcontext()\n    with cm:\n"
                    "        # Perform operation, using optional_cm if condition is True\n    ");
                make_contextlib_class("chdir", { "AbstractContextManager" }, sChdirMethods, "Non thread-safe context manager to change the current working directory.");
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        bool contextlib_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            if (!make_classes())
                return false;
            for (auto const& f : sContextlibFunctions)
                aAttributes.emplace_back(f.name, make_builtin(f));
            for (auto const& [name, c] : classes())
                aAttributes.emplace_back(name, new_reference(c));
            std::vector<object*> all;
            for (auto name : { "asynccontextmanager", "contextmanager", "closing", "nullcontext", "AbstractContextManager", "AbstractAsyncContextManager",
                "AsyncExitStack", "ContextDecorator", "ExitStack", "redirect_stdout", "redirect_stderr", "suppress", "aclosing", "chdir" })
                all.push_back(make_str(name));
            aAttributes.emplace_back("__all__", make_list(std::move(all)));
            for (auto const& [name, module] : { std::pair{ "abc", "abc" }, std::pair{ "os", "os" }, std::pair{ "sys", "sys" }, std::pair{ "_collections_abc", "_collections_abc" } })
            {
                auto const m = fn_import(module);
                if (m == nullptr)
                    return false;
                aAttributes.emplace_back(name, m);
            }
            for (auto const& [name, module, attribute] : { std::tuple{ "deque", "collections", "deque" }, std::tuple{ "wraps", "functools", "wraps" },
                std::tuple{ "MethodType", "types", "MethodType" }, std::tuple{ "GenericAlias", "types", "GenericAlias" } })
            {
                auto const value = module_attribute(module, attribute);
                if (value == nullptr)
                    return false;
                aAttributes.emplace_back(name, value);
            }
            return true;
        }

        inline bool const sContextlib = register_module("contextlib", contextlib_module);
    }
}
