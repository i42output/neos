/*
  collections_abc.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/_collections_abc.py and Lib/collections/abc.py), Copyright (c)
  2001-2024 Python Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License
  Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin
  and adapted to its object model (see runtime/python/NOTICE).

  Lib/_collections_abc.py: Copyright 2007 Google, Inc. All Rights Reserved. Licensed to PSF under a Contributor Agreement.
*/

// The modules _collections_abc and collections.abc (CPython 3.13's _collections_abc.py and collections/abc.py): the ABCs of collections
// (classes of abc.ABCMeta, with their abstract methods, mixin methods, __subclasshook__s and registered builtin types)

#include <algorithm>
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
        char const* const sModule = "collections.abc";

        std::map<std::string, object*, std::less<>>& abc_classes()
        {
            static std::map<std::string, object*, std::less<>> sClasses;
            return sClasses;
        }

        py abc_class(char const* aName)
        {
            auto const c = abc_classes().find(aName);
            if (c == abc_classes().end())
                py_raise("NameError", "name '" + std::string{ aName } + "' is not defined");
            return py::borrowed(c->second);
        }
        py none() { return py::none(); }
        py boolean(bool aValue) { return py::boolean(aValue); }
        py not_implemented_value() { return py::borrowed(not_implemented()); }

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

        bool is_instance_of(py const& aObject, char const* aClass) { return isinstance(aObject, abc_class(aClass)); }

        // (a builtin type of the MRO (not a class): whether it has the method (0: no; 1: yes; -1: it is None (e.g. list.__hash__)), from the names 
        // that dir() has for its instances)
        int builtin_type_has(py const& aType, char const* aMethod)
        {
            auto const& name = type_qualified_name(aType.get());
            if (std::strcmp(aMethod, "__hash__") == 0)
                return unhashable_builtin_type(name) ? -1 : 1;
            if (std::strcmp(aMethod, "__call__") == 0)
                return name == "builtin_function_or_method" || name == "function" || name == "method" || name == "type" || name == "functools.partial" ? 1 : 0;
            if (name == "object")
                return 0;
            // (the types of the views of dicts (dir() does not show their slots))
            static std::map<std::string, std::vector<std::string>, std::less<>> const sViews = {
                { "dict_keys", { "__iter__", "__len__", "__contains__", "__reversed__" } },
                { "dict_items", { "__iter__", "__len__", "__contains__", "__reversed__" } },
                { "dict_values", { "__iter__", "__len__", "__reversed__" } },
                { "memoryview", { "__buffer__", "__getitem__", "__iter__", "__len__", "__hash__" } } };
            if (auto const view = sViews.find(name); view != sViews.end())
                return std::find(view->second.begin(), view->second.end(), aMethod) != view->second.end() ? 1 : 0;
            return builtin("dir")(aType).contains(py{ aMethod }) ? 1 : 0;
        }

        // (_check_methods(C, *methods))
        py check_methods(py const& aClass, std::vector<char const*> const& aMethods)
        {
            auto const mro = aClass.attr("__mro__").items();
            for (auto method : aMethods)
            {
                bool found = false;
                for (auto const& b : mro)
                {
                    if (is_type_object(b.get()))
                    {
                        auto const has = builtin_type_has(b, method);
                        if (has < 0)
                            return not_implemented_value();
                        if (has > 0)
                        {
                            found = true;
                            break;
                        }
                        continue;
                    }
                    py const d = b.attr("__dict__");
                    if (d.contains(py{ method }))
                    {
                        if (d[py{ method }].is_none())
                            return not_implemented_value();
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return not_implemented_value();
            }
            return boolean(true);
        }

        // (the __subclasshook__s: cls is the class: _check_methods(C, its methods); otherwise NotImplemented)
        struct hook_spec
        {
            char const* cls;
            std::vector<char const*> methods;
        };

        hook_spec const sHooks[] =
        {
            { "Hashable", { "__hash__" } },
            { "Awaitable", { "__await__" } },
            { "Coroutine", { "__await__", "send", "throw", "close" } },
            { "AsyncIterable", { "__aiter__" } },
            { "AsyncIterator", { "__anext__", "__aiter__" } },
            { "AsyncGenerator", { "__aiter__", "__anext__", "asend", "athrow", "aclose" } },
            { "Iterable", { "__iter__" } },
            { "Iterator", { "__iter__", "__next__" } },
            { "Reversible", { "__reversed__", "__iter__" } },
            { "Generator", { "__iter__", "__next__", "send", "throw", "close" } },
            { "Sized", { "__len__" } },
            { "Container", { "__contains__" } },
            { "Collection", { "__len__", "__iter__", "__contains__" } },
            { "Buffer", { "__buffer__" } },
            { "Callable", { "__call__" } }
        };

        template <std::size_t I>
        object* subclass_hook(std::vector<object*> const& aArgs, object* aKwargs)
        {
            return py_guarded([&]()
            {
                auto const a = arguments("__subclasshook__", aArgs, aKwargs, { "cls", "C" }, 2u);
                auto const c = abc_classes().find(sHooks[I].cls);
                if (c != abc_classes().end() && a[0].get() == c->second)
                    return check_methods(a[1], sHooks[I].methods);
                return not_implemented_value();
            });
        }

        // (a generator whose body is C++)
        py generator(char const* aName, std::string const& aQualname, std::function<py()> aBody)
        {
            return make_native_generator(aName, aQualname, sModule, std::move(aBody));
        }

        py coroutine(char const* aName, std::string const& aQualname, std::function<py()> aBody)
        {
            return make_native_coroutine(aName, aQualname, sModule, std::move(aBody));
        }

        // (throw(typ, val=None, tb=None)'s abstract body: the exception raised)
        [[noreturn]] void raise_thrown(py const& aType, py aValue, py const& aTraceback)
        {
            if (aValue.empty() || aValue.is_none())
            {
                if (aTraceback.empty() || aTraceback.is_none())
                    py_raise(aType);
                aValue = aType();
            }
            if (!aTraceback.empty() && !aTraceback.is_none())
                aValue = aValue.method("with_traceback", { aTraceback });
            py_raise(aValue);
        }

        // (an empty generator: def __iter__(self): while False: yield None)
        py empty_generator(char const* aName, char const* aQualname) { return generator(aName, aQualname, []() { return none(); }); }

        bool exception_caught(std::initializer_list<char const*> aClasses) { return caught(aClasses); }

        // (raise aClass from None)
        [[noreturn]] void raise_from_none(char const* aClass)
        {
            py const e{ make_exception(aClass, {}) };
            e.set_attr("__cause__", none());
            e.set_attr("__suppress_context__", boolean(true));
            py_raise(e);
        }

        // --- the classes' methods

        builtin_entry const sHashableMethods[] =
        {
            { "__hash__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { arguments("Hashable.__hash__", aArgs, aKwargs, { "self" }, 1u); return py{ 0 }; }); } },
            { "__subclasshook__", subclass_hook<0> }
        };

        builtin_entry const sAwaitableMethods[] =
        {
            { "__await__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        arguments("Awaitable.__await__", aArgs, aKwargs, { "self" }, 1u);
                        return generator("__await__", "Awaitable.__await__", []() { py_yield(none()); return none(); });
                    });
                } },
            { "__subclasshook__", subclass_hook<1> }
        };

        builtin_entry const sCoroutineMethods[] =
        {
            { "send", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() -> py { arguments("Coroutine.send", aArgs, aKwargs, { "self", "value" }, 2u); py_raise("StopIteration", ""); }); } },
            { "throw", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]() -> py
                    {
                        auto const a = arguments("Coroutine.throw", aArgs, aKwargs, { "self", "typ", "val", "tb" }, 2u);
                        raise_thrown(a[1], a[2], a[3]);
                    });
                } },
            { "close", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("Coroutine.close", aArgs, aKwargs, { "self" }, 1u);
                        try
                        {
                            a[0].method("throw", { builtin("GeneratorExit") });
                        }
                        catch (raised const&)
                        {
                            if (!exception_caught({ "GeneratorExit", "StopIteration" }))
                                throw;
                            return none();
                        }
                        py_raise("RuntimeError", "coroutine ignored GeneratorExit");
                    });
                } },
            { "__subclasshook__", subclass_hook<2> }
        };

        builtin_entry const sAsyncIterableMethods[] =
        {
            { "__aiter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { arguments("AsyncIterable.__aiter__", aArgs, aKwargs, { "self" }, 1u); return abc_class("AsyncIterator")(); }); } },
            { "__subclasshook__", subclass_hook<3> }
        };

        builtin_entry const sAsyncIteratorMethods[] =
        {
            { "__anext__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        arguments("AsyncIterator.__anext__", aArgs, aKwargs, { "self" }, 1u);
                        return coroutine("__anext__", "AsyncIterator.__anext__", []() -> py { py_raise("StopAsyncIteration", ""); });
                    });
                } },
            { "__aiter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return arguments("AsyncIterator.__aiter__", aArgs, aKwargs, { "self" }, 1u)[0]; }); } },
            { "__subclasshook__", subclass_hook<4> }
        };

        builtin_entry const sAsyncGeneratorMethods[] =
        {
            { "__anext__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("AsyncGenerator.__anext__", aArgs, aKwargs, { "self" }, 1u)[0];
                        return coroutine("__anext__", "AsyncGenerator.__anext__", [self]() { return py_await(self.method("asend", { none() })); });
                    });
                } },
            { "asend", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        arguments("AsyncGenerator.asend", aArgs, aKwargs, { "self", "value" }, 2u);
                        return coroutine("asend", "AsyncGenerator.asend", []() -> py { py_raise("StopAsyncIteration", ""); });
                    });
                } },
            { "athrow", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("AsyncGenerator.athrow", aArgs, aKwargs, { "self", "typ", "val", "tb" }, 2u);
                        return coroutine("athrow", "AsyncGenerator.athrow", [a]() -> py { raise_thrown(a[1], a[2], a[3]); });
                    });
                } },
            { "aclose", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("AsyncGenerator.aclose", aArgs, aKwargs, { "self" }, 1u)[0];
                        return coroutine("aclose", "AsyncGenerator.aclose", [self]() -> py
                        {
                            try
                            {
                                py_await(self.method("athrow", { builtin("GeneratorExit") }));
                            }
                            catch (raised const&)
                            {
                                if (!exception_caught({ "GeneratorExit", "StopAsyncIteration" }))
                                    throw;
                                return none();
                            }
                            py_raise("RuntimeError", "asynchronous generator ignored GeneratorExit");
                        });
                    });
                } },
            { "__subclasshook__", subclass_hook<5> }
        };

        builtin_entry const sIterableMethods[] =
        {
            { "__iter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { arguments("Iterable.__iter__", aArgs, aKwargs, { "self" }, 1u); return empty_generator("__iter__", "Iterable.__iter__"); }); } },
            { "__subclasshook__", subclass_hook<6> }
        };

        builtin_entry const sIteratorMethods[] =
        {
            { "__next__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() -> py { arguments("Iterator.__next__", aArgs, aKwargs, { "self" }, 1u); py_raise("StopIteration", ""); }); } },
            { "__iter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return arguments("Iterator.__iter__", aArgs, aKwargs, { "self" }, 1u)[0]; }); } },
            { "__subclasshook__", subclass_hook<7> }
        };

        builtin_entry const sReversibleMethods[] =
        {
            { "__reversed__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { arguments("Reversible.__reversed__", aArgs, aKwargs, { "self" }, 1u); return empty_generator("__reversed__", "Reversible.__reversed__"); }); } },
            { "__subclasshook__", subclass_hook<8> }
        };

        builtin_entry const sGeneratorMethods[] =
        {
            { "__next__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return arguments("Generator.__next__", aArgs, aKwargs, { "self" }, 1u)[0].method("send", { none() }); }); } },
            { "send", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() -> py { arguments("Generator.send", aArgs, aKwargs, { "self", "value" }, 2u); py_raise("StopIteration", ""); }); } },
            { "throw", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]() -> py
                    {
                        auto const a = arguments("Generator.throw", aArgs, aKwargs, { "self", "typ", "val", "tb" }, 2u);
                        raise_thrown(a[1], a[2], a[3]);
                    });
                } },
            { "close", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("Generator.close", aArgs, aKwargs, { "self" }, 1u);
                        try
                        {
                            a[0].method("throw", { builtin("GeneratorExit") });
                        }
                        catch (raised const&)
                        {
                            if (!exception_caught({ "GeneratorExit", "StopIteration" }))
                                throw;
                            return none();
                        }
                        py_raise("RuntimeError", "generator ignored GeneratorExit");
                    });
                } },
            { "__subclasshook__", subclass_hook<9> }
        };

        builtin_entry const sSizedMethods[] =
        {
            { "__len__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { arguments("Sized.__len__", aArgs, aKwargs, { "self" }, 1u); return py{ 0 }; }); } },
            { "__subclasshook__", subclass_hook<10> }
        };

        builtin_entry const sContainerMethods[] =
        {
            { "__contains__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { arguments("Container.__contains__", aArgs, aKwargs, { "self", "x" }, 2u); return boolean(false); }); } },
            { "__subclasshook__", subclass_hook<11> }
        };

        builtin_entry const sCollectionMethods[] =
        {
            { "__subclasshook__", subclass_hook<12> }
        };

        builtin_entry const sBufferMethods[] =
        {
            { "__buffer__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() -> py { arguments("Buffer.__buffer__", aArgs, aKwargs, { "self", "flags" }, 2u, 2u); py_raise("NotImplementedError", ""); }); } },
            { "__subclasshook__", subclass_hook<13> }
        };

        // (_is_param_expr(obj))
        bool is_param_expr(py const& aObject)
        {
            if (aObject.get() == &sEllipsis.base)
                return true;
            if (isinstance(aObject, builtin("list")))
                return true;
            py const t = type_of(aObject);
            if (!(t.attr("__module__") == py{ "typing" }))
                return false;
            py const name = t.attr("__name__");
            return name == py{ "ParamSpec" } || name == py{ "_ConcatenateGenericAlias" };
        }

        // (_type_repr(obj))
        py type_repr(py const& aObject)
        {
            if (isinstance(aObject, builtin("type")))
            {
                if (aObject.attr("__module__") == py{ "builtins" })
                    return aObject.attr("__qualname__");
                return py{ aObject.attr("__module__").str() + "." + aObject.attr("__qualname__").str() };
            }
            if (aObject.get() == &sEllipsis.base)
                return py{ "..." };
            if (isinstance(aObject, import_attribute("types", "FunctionType")))
                return aObject.attr("__name__");
            return builtin("repr")(aObject);
        }

        object* sCallableGenericAlias = nullptr;

        builtin_entry const sCallableGenericAliasMethods[] =
        {
            { "__new__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("_CallableGenericAlias.__new__", aArgs, aKwargs, { "cls", "origin", "args" }, 3u);
                        py args = a[2];
                        if (!(isinstance(args, builtin("tuple")) && args.size() == 2))
                            py_raise("TypeError", "Callable must be used as Callable[[arg, ...], result].");
                        py const tArgs = args[py{ 0 }];
                        py const tResult = args[py{ 1 }];
                        if (isinstance(tArgs, py_tuple({ builtin("tuple"), builtin("list") })))
                        {
                            auto items = tArgs.items();
                            items.push_back(tResult);
                            args = py_tuple(items);
                        }
                        else if (!is_param_expr(tArgs))
                            py_raise("TypeError", "Expected a list of types, an ellipsis, ParamSpec, or Concatenate. Got " + tArgs.str());
                        return builtin("super")(py::borrowed(sCallableGenericAlias), a[0]).method("__new__", { a[0], a[1], args });
                    });
                } },
            { "__repr__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("_CallableGenericAlias.__repr__", aArgs, aKwargs, { "self" }, 1u)[0];
                        auto const args = self.attr("__args__").items();
                        if (args.size() == 2u && is_param_expr(args[0]))
                            return builtin("super")(py::borrowed(sCallableGenericAlias), self).method("__repr__");
                        std::vector<py> parts;
                        for (std::size_t i = 0u; i + 1u < args.size(); ++i)
                            parts.push_back(type_repr(args[i]));
                        return py{ "collections.abc.Callable[[" + py{ ", " }.method("join", { py_list(parts) }).str() + "], " + type_repr(args.back()).str() + "]" };
                    });
                } },
            { "__reduce__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("_CallableGenericAlias.__reduce__", aArgs, aKwargs, { "self" }, 1u)[0];
                        py args = self.attr("__args__");
                        auto const items = args.items();
                        if (!(items.size() == 2u && is_param_expr(items[0])))
                            args = py_tuple({ py_list(std::vector<py>{ items.begin(), items.end() - 1 }), items.back() });
                        return py_tuple({ py::borrowed(sCallableGenericAlias), py_tuple({ abc_class("Callable"), args }) });
                    });
                } },
            { "__getitem__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("_CallableGenericAlias.__getitem__", aArgs, aKwargs, { "self", "item" }, 2u);
                        py item = a[1];
                        if (!isinstance(item, builtin("tuple")))
                            item = py_tuple({ item });
                        py newArgs = builtin("super")(py::borrowed(sCallableGenericAlias), a[0]).method("__getitem__", { item }).attr("__args__");
                        auto const items = newArgs.items();
                        if (!items.empty() && !isinstance(items[0], py_tuple({ builtin("tuple"), builtin("list") })))
                            newArgs = py_tuple({ py_tuple(std::vector<py>{ items.begin(), items.end() - 1 }), items.back() });
                        return py::borrowed(sCallableGenericAlias)(abc_class("Callable"), builtin("tuple")(newArgs));
                    });
                } }
        };

        builtin_entry const sCallableMethods[] =
        {
            { "__call__", [](std::vector<object*> const& aArgs, object*) -> object*
                { return py_guarded([&]() { if (aArgs.empty()) py_raise("TypeError", "Callable.__call__() missing 1 required positional argument: 'self'"); return boolean(false); }); } },
            { "__subclasshook__", subclass_hook<14> }
        };

        // --- Set

        long long length_of(py const& aObject) { return aObject.size(); }

        bool set_le(py const& aSelf, py const& aOther)
        {
            if (length_of(aSelf) > length_of(aOther))
                return false;
            for (auto const& e : aSelf.items())
                if (!aOther.contains(e))
                    return false;
            return true;
        }

        bool set_ge(py const& aSelf, py const& aOther)
        {
            if (length_of(aSelf) < length_of(aOther))
                return false;
            for (auto const& e : aOther.items())
                if (!aSelf.contains(e))
                    return false;
            return true;
        }

        py from_iterable(py const& aSelf, py const& aIterable) { return aSelf.method("_from_iterable", { aIterable }); }

        // (value for value in aSource if (value in aTest) == aIn)
        py filtered(char const* aQualname, py const& aSource, py const& aTest, bool aIn)
        {
            return generator("<genexpr>", aQualname, [aSource, aTest, aIn]()
            {
                for (auto const& value : aSource.items())
                    if (aTest.contains(value) == aIn)
                        py_yield(value);
                return none();
            });
        }

        // (Set.__sub__/__rsub__/__xor__'s other: a Set (from an iterable), or NotImplemented (empty))
        py set_operand(py const& aSelf, py const& aOther)
        {
            if (is_instance_of(aOther, "Set"))
                return aOther;
            if (!is_instance_of(aOther, "Iterable"))
                return {};
            return from_iterable(aSelf, aOther);
        }

        object* set_and(std::vector<object*> const& aArgs, object* aKwargs)
        {
            return py_guarded([&]()
            {
                auto const a = arguments("Set.__and__", aArgs, aKwargs, { "self", "other" }, 2u);
                if (!is_instance_of(a[1], "Iterable"))
                    return not_implemented_value();
                return from_iterable(a[0], filtered("Set.__and__.<locals>.<genexpr>", a[1], a[0], true));
            });
        }

        object* set_or(std::vector<object*> const& aArgs, object* aKwargs)
        {
            return py_guarded([&]()
            {
                auto const a = arguments("Set.__or__", aArgs, aKwargs, { "self", "other" }, 2u);
                if (!is_instance_of(a[1], "Iterable"))
                    return not_implemented_value();
                py const self = a[0];
                py const other = a[1];
                py const chain = generator("<genexpr>", "Set.__or__.<locals>.<genexpr>", [self, other]()
                {
                    for (auto const& s : { self, other })
                        for (auto const& e : s.items())
                            py_yield(e);
                    return none();
                });
                return from_iterable(self, chain);
            });
        }

        object* set_xor(std::vector<object*> const& aArgs, object* aKwargs)
        {
            return py_guarded([&]()
            {
                auto const a = arguments("Set.__xor__", aArgs, aKwargs, { "self", "other" }, 2u);
                py const other = set_operand(a[0], a[1]);
                if (other.empty())
                    return not_implemented_value();
                return (a[0] - other) | (other - a[0]);
            });
        }

        builtin_entry const sSetMethods[] =
        {
            { "__le__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("Set.__le__", aArgs, aKwargs, { "self", "other" }, 2u);
                        return is_instance_of(a[1], "Set") ? boolean(set_le(a[0], a[1])) : not_implemented_value();
                    });
                } },
            { "__lt__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("Set.__lt__", aArgs, aKwargs, { "self", "other" }, 2u);
                        if (!is_instance_of(a[1], "Set"))
                            return not_implemented_value();
                        if (!(length_of(a[0]) < length_of(a[1])))
                            return boolean(false);
                        return a[0].method("__le__", { a[1] });
                    });
                } },
            { "__gt__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("Set.__gt__", aArgs, aKwargs, { "self", "other" }, 2u);
                        if (!is_instance_of(a[1], "Set"))
                            return not_implemented_value();
                        if (!(length_of(a[0]) > length_of(a[1])))
                            return boolean(false);
                        return a[0].method("__ge__", { a[1] });
                    });
                } },
            { "__ge__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("Set.__ge__", aArgs, aKwargs, { "self", "other" }, 2u);
                        return is_instance_of(a[1], "Set") ? boolean(set_ge(a[0], a[1])) : not_implemented_value();
                    });
                } },
            { "__eq__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("Set.__eq__", aArgs, aKwargs, { "self", "other" }, 2u);
                        if (!is_instance_of(a[1], "Set"))
                            return not_implemented_value();
                        if (length_of(a[0]) != length_of(a[1]))
                            return boolean(false);
                        return a[0].method("__le__", { a[1] });
                    });
                } },
            { "_from_iterable", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("_from_iterable", aArgs, aKwargs, { "cls", "it" }, 2u);
                        return a[0](a[1]);
                    });
                } },
            { "__and__", set_and },
            { "__rand__", set_and },
            { "isdisjoint", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("Set.isdisjoint", aArgs, aKwargs, { "self", "other" }, 2u);
                        for (auto const& value : a[1].items())
                            if (a[0].contains(value))
                                return boolean(false);
                        return boolean(true);
                    });
                } },
            { "__or__", set_or },
            { "__ror__", set_or },
            { "__sub__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("Set.__sub__", aArgs, aKwargs, { "self", "other" }, 2u);
                        py const other = set_operand(a[0], a[1]);
                        if (other.empty())
                            return not_implemented_value();
                        return from_iterable(a[0], filtered("Set.__sub__.<locals>.<genexpr>", a[0], other, false));
                    });
                } },
            { "__rsub__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("Set.__rsub__", aArgs, aKwargs, { "self", "other" }, 2u);
                        py const other = set_operand(a[0], a[1]);
                        if (other.empty())
                            return not_implemented_value();
                        return from_iterable(a[0], filtered("Set.__rsub__.<locals>.<genexpr>", other, a[0], false));
                    });
                } },
            { "__xor__", set_xor },
            { "__rxor__", set_xor },
            { "_hash", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("Set._hash", aArgs, aKwargs, { "self" }, 1u)[0];
                        py const max = import_attribute("sys", "maxsize");
                        py const mask = py{ 2 } * max + py{ 1 };
                        py const n{ self.size() };
                        py h = py{ 1927868237 } * (n + py{ 1 });
                        h = h & mask;
                        auto const hash = builtin("hash");
                        for (auto const& x : self.items())
                        {
                            py const hx = hash(x);
                            h = h ^ ((hx ^ (hx << py{ 16 }) ^ py{ 89869747 }) * py{ 3644798167ll });
                            h = h & mask;
                        }
                        h = h ^ ((h >> py{ 11 }) ^ (h >> py{ 25 }));
                        h = h * py{ 69069 } + py{ 907133923 };
                        h = h & mask;
                        if (h > max)
                            h = h - (mask + py{ 1 });
                        if (h == py{ -1 })
                            h = py{ 590923713 };
                        return h;
                    });
                } }
        };

        builtin_entry const sMutableSetMethods[] =
        {
            { "add", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() -> py { arguments("MutableSet.add", aArgs, aKwargs, { "self", "value" }, 2u); py_raise("NotImplementedError", ""); }); } },
            { "discard", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() -> py { arguments("MutableSet.discard", aArgs, aKwargs, { "self", "value" }, 2u); py_raise("NotImplementedError", ""); }); } },
            { "remove", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("MutableSet.remove", aArgs, aKwargs, { "self", "value" }, 2u);
                        if (!a[0].contains(a[1]))
                            py_raise(builtin("KeyError")(a[1]));
                        a[0].method("discard", { a[1] });
                        return none();
                    });
                } },
            { "pop", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("MutableSet.pop", aArgs, aKwargs, { "self" }, 1u)[0];
                        py const it = builtin("iter")(self);
                        auto const value = detail::next(it.get());
                        if (value == nullptr)
                        {
                            if (failed())
                                throw raised{};
                            raise_from_none("KeyError");
                        }
                        py const v{ value };
                        self.method("discard", { v });
                        return v;
                    });
                } },
            { "clear", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("MutableSet.clear", aArgs, aKwargs, { "self" }, 1u)[0];
                        try
                        {
                            for (;;)
                                self.method("pop");
                        }
                        catch (raised const&)
                        {
                            if (!exception_caught({ "KeyError" }))
                                throw;
                        }
                        return none();
                    });
                } },
            { "__ior__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("MutableSet.__ior__", aArgs, aKwargs, { "self", "it" }, 2u);
                        for (auto const& value : a[1].items())
                            a[0].method("add", { value });
                        return a[0];
                    });
                } },
            { "__iand__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("MutableSet.__iand__", aArgs, aKwargs, { "self", "it" }, 2u);
                        for (auto const& value : (a[0] - a[1]).items())
                            a[0].method("discard", { value });
                        return a[0];
                    });
                } },
            { "__ixor__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("MutableSet.__ixor__", aArgs, aKwargs, { "self", "it" }, 2u);
                        if (a[1].is(a[0]))
                            a[0].method("clear");
                        else
                        {
                            py it = a[1];
                            if (!is_instance_of(it, "Set"))
                                it = from_iterable(a[0], it);
                            for (auto const& value : it.items())
                            {
                                if (a[0].contains(value))
                                    a[0].method("discard", { value });
                                else
                                    a[0].method("add", { value });
                            }
                        }
                        return a[0];
                    });
                } },
            { "__isub__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("MutableSet.__isub__", aArgs, aKwargs, { "self", "it" }, 2u);
                        if (a[1].is(a[0]))
                            a[0].method("clear");
                        else
                            for (auto const& value : a[1].items())
                                a[0].method("discard", { value });
                        return a[0];
                    });
                } }
        };

        // --- mappings

        builtin_entry const sMappingMethods[] =
        {
            { "__getitem__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() -> py { arguments("Mapping.__getitem__", aArgs, aKwargs, { "self", "key" }, 2u); py_raise("KeyError", ""); }); } },
            { "get", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("Mapping.get", aArgs, aKwargs, { "self", "key", "default" }, 2u);
                        try
                        {
                            return a[0][a[1]];
                        }
                        catch (raised const&)
                        {
                            if (!exception_caught({ "KeyError" }))
                                throw;
                            return a[2].empty() ? none() : a[2];
                        }
                    });
                } },
            { "__contains__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("Mapping.__contains__", aArgs, aKwargs, { "self", "key" }, 2u);
                        try
                        {
                            a[0][a[1]];
                        }
                        catch (raised const&)
                        {
                            if (!exception_caught({ "KeyError" }))
                                throw;
                            return boolean(false);
                        }
                        return boolean(true);
                    });
                } },
            { "keys", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return abc_class("KeysView")(arguments("Mapping.keys", aArgs, aKwargs, { "self" }, 1u)[0]); }); } },
            { "items", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return abc_class("ItemsView")(arguments("Mapping.items", aArgs, aKwargs, { "self" }, 1u)[0]); }); } },
            { "values", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return abc_class("ValuesView")(arguments("Mapping.values", aArgs, aKwargs, { "self" }, 1u)[0]); }); } },
            { "__eq__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("Mapping.__eq__", aArgs, aKwargs, { "self", "other" }, 2u);
                        if (!is_instance_of(a[1], "Mapping"))
                            return not_implemented_value();
                        auto const dict = builtin("dict");
                        return boolean(dict(a[0].method("items")) == dict(a[1].method("items")));
                    });
                } }
        };

        builtin_entry const sMappingViewMethods[] =
        {
            { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("MappingView.__init__", aArgs, aKwargs, { "self", "mapping" }, 2u);
                        a[0].set_attr("_mapping", a[1]);
                        return none();
                    });
                } },
            { "__len__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return py{ arguments("MappingView.__len__", aArgs, aKwargs, { "self" }, 1u)[0].attr("_mapping").size() }; }); } },
            { "__repr__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("MappingView.__repr__", aArgs, aKwargs, { "self" }, 1u)[0];
                        return py{ "{0.__class__.__name__}({0._mapping!r})" }.method("format", { self });
                    });
                } }
        };

        object* view_from_iterable(std::vector<object*> const& aArgs, object* aKwargs)
        {
            return py_guarded([&]() { return builtin("set")(arguments("_from_iterable", aArgs, aKwargs, { "cls", "it" }, 2u)[1]); });
        }

        builtin_entry const sKeysViewMethods[] =
        {
            { "_from_iterable", view_from_iterable },
            { "__contains__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("KeysView.__contains__", aArgs, aKwargs, { "self", "key" }, 2u);
                        return boolean(a[0].attr("_mapping").contains(a[1]));
                    });
                } },
            { "__iter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("KeysView.__iter__", aArgs, aKwargs, { "self" }, 1u)[0];
                        return generator("__iter__", "KeysView.__iter__", [self]() { return py{ b_yield_from({ self.attr("_mapping").get() }, nullptr) }; });
                    });
                } }
        };

        builtin_entry const sItemsViewMethods[] =
        {
            { "_from_iterable", view_from_iterable },
            { "__contains__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("ItemsView.__contains__", aArgs, aKwargs, { "self", "item" }, 2u);
                        auto const parts = a[1].items();
                        if (parts.size() != 2u)
                            py_raise("ValueError", parts.size() < 2u ? "not enough values to unpack (expected 2, got " + std::to_string(parts.size()) + ")" :
                                "too many values to unpack (expected 2)");
                        py v;
                        try
                        {
                            v = a[0].attr("_mapping")[parts[0]];
                        }
                        catch (raised const&)
                        {
                            if (!exception_caught({ "KeyError" }))
                                throw;
                            return boolean(false);
                        }
                        return boolean(v.is(parts[1]) || v == parts[1]);
                    });
                } },
            { "__iter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("ItemsView.__iter__", aArgs, aKwargs, { "self" }, 1u)[0];
                        return generator("__iter__", "ItemsView.__iter__", [self]()
                        {
                            py const mapping = self.attr("_mapping");
                            py const it = builtin("iter")(mapping);
                            for (;;)
                            {
                                auto const key = detail::next(it.get());
                                if (key == nullptr)
                                {
                                    if (failed())
                                        throw raised{};
                                    break;
                                }
                                py const k{ key };
                                py_yield(py_tuple({ k, mapping[k] }));
                            }
                            return none();
                        });
                    });
                } }
        };

        builtin_entry const sValuesViewMethods[] =
        {
            { "__contains__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("ValuesView.__contains__", aArgs, aKwargs, { "self", "value" }, 2u);
                        py const mapping = a[0].attr("_mapping");
                        for (auto const& key : mapping.items())
                        {
                            py const v = mapping[key];
                            if (v.is(a[1]) || v == a[1])
                                return boolean(true);
                        }
                        return boolean(false);
                    });
                } },
            { "__iter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("ValuesView.__iter__", aArgs, aKwargs, { "self" }, 1u)[0];
                        return generator("__iter__", "ValuesView.__iter__", [self]()
                        {
                            py const mapping = self.attr("_mapping");
                            py const it = builtin("iter")(mapping);
                            for (;;)
                            {
                                auto const key = detail::next(it.get());
                                if (key == nullptr)
                                {
                                    if (failed())
                                        throw raised{};
                                    break;
                                }
                                py const k{ key };
                                py_yield(mapping[k]);
                            }
                            return none();
                        });
                    });
                } }
        };

        object* sMarker = nullptr;      // (MutableMapping.__marker)

        builtin_entry const sMutableMappingMethods[] =
        {
            { "__setitem__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() -> py { arguments("MutableMapping.__setitem__", aArgs, aKwargs, { "self", "key", "value" }, 3u); py_raise("KeyError", ""); }); } },
            { "__delitem__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() -> py { arguments("MutableMapping.__delitem__", aArgs, aKwargs, { "self", "key" }, 2u); py_raise("KeyError", ""); }); } },
            { "pop", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("MutableMapping.pop", aArgs, aKwargs, { "self", "key", "default" }, 2u);
                        py value;
                        try
                        {
                            value = a[0][a[1]];
                        }
                        catch (raised const&)
                        {
                            if (a[2].empty() || a[2].get() == sMarker)
                                throw;
                            if (!exception_caught({ "KeyError" }))
                                throw;
                            return a[2];
                        }
                        py_checked(setitem(a[0].get(), a[1].get(), nullptr) == 0);
                        return value;
                    });
                } },
            { "popitem", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("MutableMapping.popitem", aArgs, aKwargs, { "self" }, 1u)[0];
                        py const it = builtin("iter")(self);
                        auto const first = detail::next(it.get());
                        if (first == nullptr)
                        {
                            if (failed())
                                throw raised{};
                            raise_from_none("KeyError");
                        }
                        py const key{ first };
                        py const value = self[key];
                        py_checked(setitem(self.get(), key.get(), nullptr) == 0);
                        return py_tuple({ key, value });
                    });
                } },
            { "clear", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("MutableMapping.clear", aArgs, aKwargs, { "self" }, 1u)[0];
                        try
                        {
                            for (;;)
                                self.method("popitem");
                        }
                        catch (raised const&)
                        {
                            if (!exception_caught({ "KeyError" }))
                                throw;
                        }
                        return none();
                    });
                } },
            { "update", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        // (update(self, other=(), /, **kwds))
                        if (aArgs.empty())
                            py_raise("TypeError", "MutableMapping.update() missing 1 required positional argument: 'self'");
                        if (aArgs.size() > 2u)
                            py_raise("TypeError", "MutableMapping.update() takes from 1 to 2 positional arguments but " + std::to_string(aArgs.size()) + " were given");
                        py const self = py::borrowed(aArgs[0]);
                        if (aArgs.size() == 2u)
                        {
                            py const other = py::borrowed(aArgs[1]);
                            if (is_instance_of(other, "Mapping"))
                            {
                                for (auto const& key : other.items())
                                    self.set_item(key, other[key]);
                            }
                            else if (builtin("hasattr")(other, py{ "keys" }).truthy())
                            {
                                for (auto const& key : other.method("keys").items())
                                    self.set_item(key, other[key]);
                            }
                            else
                                for (auto const& item : other.items())
                                {
                                    auto const kv = item.items();
                                    if (kv.size() != 2u)
                                        py_raise("ValueError", kv.size() < 2u ? "not enough values to unpack (expected 2, got " + std::to_string(kv.size()) + ")" :
                                            "too many values to unpack (expected 2)");
                                    self.set_item(kv[0], kv[1]);
                                }
                        }
                        if (aKwargs != nullptr)
                            for (auto const& [key, value] : as<dict_object>(aKwargs)->items)
                                self.set_item(py::borrowed(key), py::borrowed(value));
                        return none();
                    });
                } },
            { "setdefault", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("MutableMapping.setdefault", aArgs, aKwargs, { "self", "key", "default" }, 2u);
                        py const d = a[2].empty() ? none() : a[2];
                        try
                        {
                            return a[0][a[1]];
                        }
                        catch (raised const&)
                        {
                            if (!exception_caught({ "KeyError" }))
                                throw;
                            a[0].set_item(a[1], d);
                        }
                        return d;
                    });
                } }
        };

        // --- sequences

        builtin_entry const sSequenceMethods[] =
        {
            { "__getitem__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() -> py { arguments("Sequence.__getitem__", aArgs, aKwargs, { "self", "index" }, 2u); py_raise("IndexError", ""); }); } },
            { "__iter__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("Sequence.__iter__", aArgs, aKwargs, { "self" }, 1u)[0];
                        return generator("__iter__", "Sequence.__iter__", [self]()
                        {
                            long long i = 0;
                            for (;;)
                            {
                                py v;
                                try
                                {
                                    v = self[py{ i }];
                                }
                                catch (raised const&)
                                {
                                    if (!exception_caught({ "IndexError" }))
                                        throw;
                                    return none();
                                }
                                py_yield(v);
                                ++i;
                            }
                        });
                    });
                } },
            { "__contains__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("Sequence.__contains__", aArgs, aKwargs, { "self", "value" }, 2u);
                        py const it = builtin("iter")(a[0]);
                        for (;;)
                        {
                            auto const n = detail::next(it.get());
                            if (n == nullptr)
                            {
                                if (failed())
                                    throw raised{};
                                return boolean(false);
                            }
                            py const v{ n };
                            if (v.is(a[1]) || v == a[1])
                                return boolean(true);
                        }
                    });
                } },
            { "__reversed__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("Sequence.__reversed__", aArgs, aKwargs, { "self" }, 1u)[0];
                        return generator("__reversed__", "Sequence.__reversed__", [self]()
                        {
                            for (long long i = self.size(); i-- > 0;)
                                py_yield(self[py{ i }]);
                            return none();
                        });
                    });
                } },
            { "index", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("Sequence.index", aArgs, aKwargs, { "self", "value", "start", "stop" }, 2u);
                        py start = a[2].empty() ? py{ 0 } : a[2];
                        py stop = a[3].empty() ? none() : a[3];
                        if (!start.is_none() && start < py{ 0 })
                            start = builtin("max")(py{ a[0].size() } + start, py{ 0 });
                        if (!stop.is_none() && stop < py{ 0 })
                            stop = stop + py{ a[0].size() };
                        py i = start;
                        while (stop.is_none() || i < stop)
                        {
                            py v;
                            try
                            {
                                v = a[0][i];
                            }
                            catch (raised const&)
                            {
                                if (!exception_caught({ "IndexError" }))
                                    throw;
                                break;
                            }
                            if (v.is(a[1]) || v == a[1])
                                return i;
                            i = i + py{ 1 };
                        }
                        py_raise(builtin("ValueError"));
                    });
                } },
            { "count", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("Sequence.count", aArgs, aKwargs, { "self", "value" }, 2u);
                        long long n = 0;
                        for (auto const& v : a[0].items())
                            if (v.is(a[1]) || v == a[1])
                                ++n;
                        return py{ n };
                    });
                } }
        };

        builtin_entry const sMutableSequenceMethods[] =
        {
            { "__setitem__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() -> py { arguments("MutableSequence.__setitem__", aArgs, aKwargs, { "self", "index", "value" }, 3u); py_raise("IndexError", ""); }); } },
            { "__delitem__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() -> py { arguments("MutableSequence.__delitem__", aArgs, aKwargs, { "self", "index" }, 2u); py_raise("IndexError", ""); }); } },
            { "insert", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() -> py { arguments("MutableSequence.insert", aArgs, aKwargs, { "self", "index", "value" }, 3u); py_raise("IndexError", ""); }); } },
            { "append", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("MutableSequence.append", aArgs, aKwargs, { "self", "value" }, 2u);
                        a[0].method("insert", { py{ a[0].size() }, a[1] });
                        return none();
                    });
                } },
            { "clear", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("MutableSequence.clear", aArgs, aKwargs, { "self" }, 1u)[0];
                        try
                        {
                            for (;;)
                                self.method("pop");
                        }
                        catch (raised const&)
                        {
                            if (!exception_caught({ "IndexError" }))
                                throw;
                        }
                        return none();
                    });
                } },
            { "reverse", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        py const self = arguments("MutableSequence.reverse", aArgs, aKwargs, { "self" }, 1u)[0];
                        long long const n = self.size();
                        for (long long i = 0; i < n / 2; ++i)
                        {
                            py const a = self[py{ n - i - 1 }];
                            py const b = self[py{ i }];
                            self.set_item(py{ i }, a);
                            self.set_item(py{ n - i - 1 }, b);
                        }
                        return none();
                    });
                } },
            { "extend", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("MutableSequence.extend", aArgs, aKwargs, { "self", "values" }, 2u);
                        py values = a[1];
                        if (values.is(a[0]))
                            values = builtin("list")(values);
                        py const it = builtin("iter")(values);
                        for (;;)
                        {
                            auto const n = detail::next(it.get());
                            if (n == nullptr)
                            {
                                if (failed())
                                    throw raised{};
                                break;
                            }
                            a[0].method("append", { py{ n } });
                        }
                        return none();
                    });
                } },
            { "pop", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("MutableSequence.pop", aArgs, aKwargs, { "self", "index" }, 1u);
                        py const index = a[1].empty() ? py{ -1 } : a[1];
                        py const v = a[0][index];
                        py_checked(setitem(a[0].get(), index.get(), nullptr) == 0);
                        return v;
                    });
                } },
            { "remove", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("MutableSequence.remove", aArgs, aKwargs, { "self", "value" }, 2u);
                        py const index = a[0].method("index", { a[1] });
                        py_checked(setitem(a[0].get(), index.get(), nullptr) == 0);
                        return none();
                    });
                } },
            { "__iadd__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("MutableSequence.__iadd__", aArgs, aKwargs, { "self", "values" }, 2u);
                        a[0].method("extend", { a[1] });
                        return a[0];
                    });
                } }
        };

        // --- ByteString (deprecated: its metaclass warns)

        void byte_string_deprecated()
        {
            import_attribute("warnings", "_deprecated").call({ py{ "collections.abc.ByteString" } }, { { "remove", py_tuple({ py{ 3 }, py{ 17 } }) } });
        }

        builtin_entry const sDeprecateByteStringMetaMethods[] =
        {
            { "__new__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        if (aArgs.size() != 4u)
                            py_raise("TypeError", "_DeprecateByteStringMeta.__new__() missing required arguments: 'name', 'bases' and 'namespace'");
                        if (!(py::borrowed(aArgs[1]) == py{ "ByteString" }))
                            byte_string_deprecated();
                        std::vector<object*> args;
                        for (auto a : aArgs)
                            args.push_back(new_reference(a));
                        return py{ invoke(import_attribute("abc", "ABCMeta").attr("__new__").get(), std::move(args), aKwargs) };
                    });
                } },
            { "__instancecheck__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("__instancecheck__", aArgs, aKwargs, { "cls", "instance" }, 2u);
                        byte_string_deprecated();
                        return import_attribute("abc", "ABCMeta").attr("__instancecheck__")(a[0], a[1]);
                    });
                } }
        };

        // --- the module

        // (a class of the module (aMeta(name, bases, namespace)): its methods (those named in aAbstract are abstract, those in aClassMethods are
        // classmethods), and other attributes)
        py make_abc_of(char const* aName, std::vector<char const*> const& aBases, builtin_entry const* aMethods, std::size_t aCount, 
            std::vector<char const*> const& aAbstract, std::vector<char const*> const& aClassMethods, std::vector<std::pair<char const*, py>> const& aExtra, 
            py aMeta, char const* aDoc)
        {
            if (aMeta.empty())
                aMeta = import_attribute("abc", "ABCMeta");
            py const ns = py_dict();
            ns.set_item(py{ "__module__" }, py{ sModule });
            ns.set_item(py{ "__qualname__" }, py{ aName });
            ns.set_item(py{ "__slots__" }, py_tuple());
            if (aDoc != nullptr)
                ns.set_item(py{ "__doc__" }, py{ aDoc });
            auto const abstractmethod = import_attribute("abc", "abstractmethod");
            auto const classmethod = builtin("classmethod");
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
                    f = abstractmethod(f);
                if (std::strcmp(m.name, "__subclasshook__") == 0 || named(aClassMethods, m.name))
                    f = classmethod(f);
                ns.set_item(py{ m.name }, f);
            }
            for (auto const& [name, value] : aExtra)
                ns.set_item(py{ name }, value);
            std::vector<py> bases;
            for (auto b : aBases)
                bases.push_back(abc_class(b));
            py const cls = aMeta(py{ aName }, py_tuple(bases), ns);
            abc_classes()[aName] = cls.reference();
            return cls;
        }

        template <std::size_t N>
        py make_abc(char const* aName, std::vector<char const*> const& aBases, builtin_entry const (&aMethods)[N], std::vector<char const*> const& aAbstract,
            std::vector<char const*> const& aClassMethods = {}, std::vector<std::pair<char const*, py>> const& aExtra = {}, py aMeta = {}, char const* aDoc = nullptr)
        {
            return make_abc_of(aName, aBases, aMethods, N, aAbstract, aClassMethods, aExtra, std::move(aMeta), aDoc);
        }

        // (the types of builtin objects that are registered with the ABCs)
        std::vector<std::pair<char const*, py>> builtin_types()
        {
            auto const iter = builtin("iter");
            std::vector<std::pair<char const*, py>> result;
            result.emplace_back("bytes_iterator", type_of(iter(py{ make_bytes({}) })));
            result.emplace_back("bytearray_iterator", type_of(iter(py{ make_bytearray({}) })));
            result.emplace_back("dict_keyiterator", type_of(iter(py_dict().method("keys"))));
            result.emplace_back("dict_valueiterator", type_of(iter(py_dict().method("values"))));
            result.emplace_back("dict_itemiterator", type_of(iter(py_dict().method("items"))));
            result.emplace_back("list_iterator", type_of(iter(py_list())));
            result.emplace_back("list_reverseiterator", type_of(iter(builtin("reversed")(py_list()))));
            result.emplace_back("range_iterator", type_of(iter(builtin("range")(py{ 0 }))));
            py longRange;
            try
            {
                longRange = type_of(iter(builtin("range")(py{ 1 } << py{ 1000 })));
            }
            catch (raised const&)
            {
                take_exception();
                longRange = result.back().second;
            }
            result.emplace_back("longrange_iterator", longRange);
            result.emplace_back("set_iterator", type_of(iter(builtin("set")())));
            result.emplace_back("str_iterator", type_of(iter(py{ "" })));
            result.emplace_back("tuple_iterator", type_of(iter(py_tuple())));
            result.emplace_back("zip_iterator", type_of(iter(builtin("zip")())));
            result.emplace_back("dict_keys", type_of(py_dict().method("keys")));
            result.emplace_back("dict_values", type_of(py_dict().method("values")));
            result.emplace_back("dict_items", type_of(py_dict().method("items")));
            result.emplace_back("mappingproxy", import_attribute("types", "MappingProxyType"));
            result.emplace_back("generator", import_attribute("types", "GeneratorType"));
            result.emplace_back("coroutine", import_attribute("types", "CoroutineType"));
            result.emplace_back("async_generator", import_attribute("types", "AsyncGeneratorType"));
            return result;
        }

        builtin_entry const sModuleFunctions[] =
        {
            { "_check_methods", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    return py_guarded([&]()
                    {
                        if (aArgs.empty())
                            py_raise("TypeError", "_check_methods() missing 1 required positional argument: 'C'");
                        std::vector<std::string> names;
                        for (std::size_t i = 1u; i < aArgs.size(); ++i)
                            names.push_back(py::borrowed(aArgs[i]).str());
                        std::vector<char const*> methods;
                        for (auto const& n : names)
                            methods.push_back(n.c_str());
                        return check_methods(py::borrowed(aArgs[0]), methods);
                    });
                } },
            { "_is_param_expr", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return boolean(is_param_expr(arguments("_is_param_expr", aArgs, aKwargs, { "obj" }, 1u)[0])); }); } },
            { "_type_repr", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return type_repr(arguments("_type_repr", aArgs, aKwargs, { "obj" }, 1u)[0]); }); } }
        };

        object* sModuleObject = nullptr;

        bool make_abcs()
        {
            if (!abc_classes().empty())
                return true;
            auto const result = py_guarded([&]()
            {
                auto const genericAlias = import_attribute("types", "GenericAlias");
                py const classGetitem = builtin("classmethod")(genericAlias);
                auto const types = builtin_types();
                auto const typeNamed = [&](char const* aName) -> py
                {
                    for (auto const& t : types)
                        if (std::strcmp(t.first, aName) == 0)
                            return t.second;
                    return none();
                };
                make_abc("Hashable", {}, sHashableMethods, { "__hash__" });
                make_abc("Awaitable", {}, sAwaitableMethods, { "__await__" }, {}, { { "__class_getitem__", classGetitem } });
                make_abc("Coroutine", { "Awaitable" }, sCoroutineMethods, { "send", "throw" }).method("register", { typeNamed("coroutine") });
                make_abc("AsyncIterable", {}, sAsyncIterableMethods, { "__aiter__" }, {}, { { "__class_getitem__", classGetitem } });
                make_abc("AsyncIterator", { "AsyncIterable" }, sAsyncIteratorMethods, { "__anext__" });
                make_abc("AsyncGenerator", { "AsyncIterator" }, sAsyncGeneratorMethods, { "asend", "athrow" }).method("register", { typeNamed("async_generator") });
                make_abc("Iterable", {}, sIterableMethods, { "__iter__" }, {}, { { "__class_getitem__", classGetitem } });
                py const iterator = make_abc("Iterator", { "Iterable" }, sIteratorMethods, { "__next__" });
                for (auto name : { "bytes_iterator", "bytearray_iterator", "dict_keyiterator", "dict_valueiterator", "dict_itemiterator", "list_iterator",
                    "list_reverseiterator", "range_iterator", "longrange_iterator", "set_iterator", "str_iterator", "tuple_iterator", "zip_iterator" })
                    iterator.method("register", { typeNamed(name) });
                make_abc("Reversible", { "Iterable" }, sReversibleMethods, { "__reversed__" });
                make_abc("Generator", { "Iterator" }, sGeneratorMethods, { "send", "throw" }).method("register", { typeNamed("generator") });
                make_abc("Sized", {}, sSizedMethods, { "__len__" });
                make_abc("Container", {}, sContainerMethods, { "__contains__" }, {}, { { "__class_getitem__", classGetitem } });
                make_abc("Collection", { "Sized", "Iterable", "Container" }, sCollectionMethods, {});
                make_abc("Buffer", {}, sBufferMethods, { "__buffer__" });
                {
                    py const ns = py_dict();
                    ns.set_item(py{ "__module__" }, py{ sModule });
                    ns.set_item(py{ "__qualname__" }, py{ "_CallableGenericAlias" });
                    ns.set_item(py{ "__slots__" }, py_tuple());
                    ns.set_item(py{ "__doc__" }, py{ " Represent `Callable[argtypes, resulttype]`.\n\n    This sets ``__args__`` to a tuple containing the flattened\n"
                        "    ``argtypes`` followed by ``resulttype``.\n\n    Example: ``Callable[[int, str], float]`` sets ``__args__`` to\n    ``(int, str, float)``.\n    " });
                    for (auto const& m : sCallableGenericAliasMethods)
                    {
                        py f{ make_builtin(m) };
                        if (std::strcmp(m.name, "__new__") == 0)
                            f = builtin("staticmethod")(f);
                        ns.set_item(py{ m.name }, f);
                    }
                    py const cls = builtin("type")(py{ "_CallableGenericAlias" }, py_tuple({ genericAlias }), ns);
                    sCallableGenericAlias = cls.reference();
                }
                make_abc("Callable", {}, sCallableMethods, { "__call__" }, {}, { { "__class_getitem__", builtin("classmethod")(py::borrowed(sCallableGenericAlias)) } });
                make_abc("Set", { "Collection" }, sSetMethods, {}, { "_from_iterable" }, {}, {},
                    "A set is a finite, iterable container.\n\n    This class provides concrete generic implementations of all\n"
                    "    methods except for __contains__, __iter__ and __len__.\n\n    To override the comparisons (presumably for speed, as the\n"
                    "    semantics are fixed), redefine __le__ and __ge__,\n    then the other operations will automatically follow suit.\n    ").method("register",
                    { builtin("frozenset") });
                make_abc("MutableSet", { "Set" }, sMutableSetMethods, { "add", "discard" }, {}, {}, {},
                    "A mutable set is a finite, iterable container.\n\n    This class provides concrete generic implementations of all\n"
                    "    methods except for __contains__, __iter__, __len__,\n    add(), and discard().\n\n    To override the comparisons (presumably for speed, as the\n"
                    "    semantics are fixed), all you have to do is redefine __le__ and\n    then the other operations will automatically follow suit.\n    ").method("register",
                    { builtin("set") });
                make_abc("Mapping", { "Collection" }, sMappingMethods, { "__getitem__" }, {}, { { "__abc_tpflags__", py{ 1 << 6 } }, { "__reversed__", none() } }, {},
                    "A Mapping is a generic container for associating key/value\n    pairs.\n\n    This class provides concrete generic implementations of all\n"
                    "    methods except for __getitem__, __iter__, and __len__.\n    ").method("register", { typeNamed("mappingproxy") });
                {
                    make_abc("MappingView", { "Sized" }, sMappingViewMethods, {}, {}, { { "__class_getitem__", classGetitem }, 
                        { "__slots__", py_tuple({ py{ "_mapping" } }) } });
                }
                make_abc("KeysView", { "MappingView", "Set" }, sKeysViewMethods, {}, { "_from_iterable" }).method("register", { typeNamed("dict_keys") });
                make_abc("ItemsView", { "MappingView", "Set" }, sItemsViewMethods, {}, { "_from_iterable" }).method("register", { typeNamed("dict_items") });
                make_abc("ValuesView", { "MappingView", "Collection" }, sValuesViewMethods, {}).method("register", { typeNamed("dict_values") });
                sMarker = builtin("object")().release();
                make_abc("MutableMapping", { "Mapping" }, sMutableMappingMethods, { "__setitem__", "__delitem__" }, {},
                    { { "_MutableMapping__marker", py::borrowed(sMarker) } }, {},
                    "A MutableMapping is a generic container for associating\n    key/value pairs.\n\n    This class provides concrete generic implementations of all\n"
                    "    methods except for __getitem__, __setitem__, __delitem__,\n    __iter__, and __len__.\n\n    ").method("register", { builtin("dict") });
                py const sequence = make_abc("Sequence", { "Reversible", "Collection" }, sSequenceMethods, { "__getitem__" }, {}, { { "__abc_tpflags__", py{ 1 << 5 } } }, {},
                    "All the operations on a read-only sequence.\n\n    Concrete subclasses must override __new__ or __init__,\n    __getitem__, and __len__.\n    ");
                for (auto t : { "tuple", "str", "range", "memoryview" })
                    sequence.method("register", { builtin(t) });
                {
                    py const ns = py_dict();
                    ns.set_item(py{ "__module__" }, py{ sModule });
                    ns.set_item(py{ "__qualname__" }, py{ "_DeprecateByteStringMeta" });
                    for (auto const& m : sDeprecateByteStringMetaMethods)
                    {
                        py f{ make_builtin(m) };
                        if (std::strcmp(m.name, "__new__") == 0)
                            f = builtin("staticmethod")(f);
                        ns.set_item(py{ m.name }, f);
                    }
                    py const meta = builtin("type")(py{ "_DeprecateByteStringMeta" }, py_tuple({ import_attribute("abc", "ABCMeta") }), ns);
                    abc_classes()["_DeprecateByteStringMeta"] = meta.reference();
                    py const byteString = make_abc_of("ByteString", { "Sequence" }, nullptr, 0u, {}, {}, {}, meta,
                        "Deprecated ABC serving as a common supertype of ``bytes`` and ``bytearray``.\n\n    This ABC is scheduled for removal in Python 3.17.\n"
                        "    Use ``isinstance(obj, collections.abc.Buffer)`` to test if ``obj``\n    implements the buffer protocol at runtime. For use in type annotations,\n"
                        "    either use ``Buffer`` or a union that explicitly specifies the types your\n    code supports (e.g., ``bytes | bytearray | memoryview``).\n    ");
                    byteString.method("register", { builtin("bytes") });
                    byteString.method("register", { builtin("bytearray") });
                }
                py const mutableSequence = make_abc("MutableSequence", { "Sequence" }, sMutableSequenceMethods, { "__setitem__", "__delitem__", "insert" }, {}, {}, {},
                    "All the operations on a read-write sequence.\n\n    Concrete subclasses must provide __new__ or __init__,\n"
                    "    __getitem__, __setitem__, __delitem__, __len__, and insert().\n\n    ");
                mutableSequence.method("register", { builtin("list") });
                mutableSequence.method("register", { builtin("bytearray") });
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        char const* const sAll[] = { "Awaitable", "Coroutine", "AsyncIterable", "AsyncIterator", "AsyncGenerator", "Hashable", "Iterable", "Iterator", "Generator",
            "Reversible", "Sized", "Container", "Callable", "Collection", "Set", "MutableSet", "Mapping", "MutableMapping", "MappingView", "KeysView", "ItemsView",
            "ValuesView", "Sequence", "MutableSequence", "ByteString", "Buffer" };

        std::vector<object*> all_names()
        {
            std::vector<object*> all;
            for (auto name : sAll)
                all.push_back(make_str(name));
            return all;
        }

        bool collections_abc_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            if (!make_abcs())
                return false;
            for (auto const& f : sModuleFunctions)
                aAttributes.emplace_back(f.name, make_builtin(f));
            for (auto name : sAll)
                aAttributes.emplace_back(name, abc_class(name).reference());
            aAttributes.emplace_back("_CallableGenericAlias", new_reference(sCallableGenericAlias));
            aAttributes.emplace_back("_DeprecateByteStringMeta", abc_class("_DeprecateByteStringMeta").reference());
            auto const extra = py_guarded([&]()
            {
                for (auto const& [name, type] : builtin_types())
                    aAttributes.emplace_back(name, type.reference());
                aAttributes.emplace_back("GenericAlias", import_attribute("types", "GenericAlias").reference());
                aAttributes.emplace_back("EllipsisType", type_of(py::borrowed(&sEllipsis.base)).reference());
                aAttributes.emplace_back("FunctionType", import_attribute("types", "FunctionType").reference());
                aAttributes.emplace_back("ABCMeta", import_attribute("abc", "ABCMeta").reference());
                aAttributes.emplace_back("abstractmethod", import_attribute("abc", "abstractmethod").reference());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.reference());
                return py::none();
            });
            if (extra == nullptr)
                return false;
            release(extra);
            aAttributes.emplace_back("__all__", make_list(all_names()));
            return true;
        }

        // (collections.abc: from _collections_abc import *, __all__ and _CallableGenericAlias)
        bool abc_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            if (!make_abcs())
                return false;
            for (auto name : sAll)
                aAttributes.emplace_back(name, abc_class(name).reference());
            aAttributes.emplace_back("_CallableGenericAlias", new_reference(sCallableGenericAlias));
            aAttributes.emplace_back("__all__", make_list(all_names()));
            return true;
        }

        inline bool const sCollectionsAbcPrivate = register_module("_collections_abc", collections_abc_module);
        inline bool const sCollectionsAbc = register_module("collections.abc", abc_module);
    }
}
