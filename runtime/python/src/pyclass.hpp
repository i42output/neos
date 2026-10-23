/*
  pyclass.hpp

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

// The helpers of a module whose classes are made (by type) of closures (as the Python modules of CPython's that the runtime implements
// are: concurrent.futures and asyncio): a module_writer makes its functions, methods (their docstrings: the module's, by qualified name)
// and classes; try_except, try_finally and with_ are Python's try and with statements.

#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "objects.hpp"
#include "values.hpp"

namespace neos::python_runtime::detail::pyclass
{
    inline py S(char const* aText) { return py{ aText }; }
    inline py S(std::string aText) { return py{ std::move(aText) }; }

    // --- constants (made once: e.g. S<"text">() is the str "text")

    template <std::size_t N>
    struct fixed_string
    {
        char value[N];
        constexpr fixed_string(char const (&aText)[N]) { for (std::size_t i = 0u; i < N; ++i) value[i] = aText[i]; }
        constexpr std::size_t size() const { return N - 1u; }
    };
    template <fixed_string Text>
    py const& S() { static py const sValue{ std::string{ Text.value, Text.size() } }; return sValue; }
    template <long long Value>
    py const& I() { static py const sValue{ Value }; return sValue; }
    // (an int too big for 64 bits (of its decimal digits))
    template <fixed_string Digits>
    py const& BigI() { static py const sValue = builtin("int")(py{ std::string{ Digits.value, Digits.size() } }); return sValue; }
    template <double Value>
    py const& F() { static py const sValue{ Value }; return sValue; }
    // (a builtin (builtins.name, as it was when first used))
    template <fixed_string Name>
    py const& B() { static py const sValue = builtin(Name.value); return sValue; }
    // (a module (imported when first used))
    template <fixed_string Name>
    py const& Mod() { static py const sValue{ fn_import(Name.value) }; return sValue; }
    inline py none() { return py::none(); }
    inline py boolean(bool aValue) { return py::boolean(aValue); }
    inline py M(char const* aModule, char const* aName) { return import_attribute(aModule, aName); }
    inline bool has_attr(py const& aObject, char const* aName) { return builtin("hasattr")(aObject, S(aName)).truthy(); }
    inline py get_attr(py const& aObject, char const* aName, py const& aDefault) { return builtin("getattr")(aObject, S(aName), aDefault); }
    inline bool callable(py const& aObject) { return builtin("callable")(aObject).truthy(); }

    using body_type = bound_body;
    using namespace_type = std::vector<std::pair<char const*, py>>;

    struct module_writer
    {
        char const* module;                         ///< (its name, e.g. "concurrent.futures._base")
        char const* (*docs)(char const* aName);     ///< (its docstrings (by qualified name: "" is the module's), null: none)

        char const* doc(char const* aName) const
        {
            auto const d = docs != nullptr ? docs(aName) : nullptr;
            return d != nullptr ? d : "";
        }
        // (a function of its parameters (aVarArgs: *args, appended; aVarKeywords: **kwargs, appended); its docstring: the module's (of aQualname))
        py function(std::string const& aName, std::string const& aQualname, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false,
            bool aVarKeywords = false, unsigned aFlags = 0u) const
        {
            auto const signature = std::make_shared<native_signature const>(native_signature{ std::move(aParameters), aVarArgs, aVarKeywords, aFlags });
            return make_bound_function(aName, aQualname, doc(aQualname.c_str()), module, signature, std::move(aBody));
        }
        // (a method: its parameters after self (aClass: its class's qualified name))
        py method(char const* aClass, char const* aName, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false,
            bool aVarKeywords = false) const
        {
            aParameters.insert(aParameters.begin(), py_positional("self"));
            return function(aName, std::string{ aClass } + "." + aName, std::move(aParameters), std::move(aBody), aVarArgs, aVarKeywords);
        }
        // (a class method: its parameters after cls)
        py class_method(char const* aClass, char const* aName, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false,
            bool aVarKeywords = false) const
        {
            aParameters.insert(aParameters.begin(), py_positional("cls"));
            return builtin("classmethod")(function(aName, std::string{ aClass } + "." + aName, std::move(aParameters), std::move(aBody), aVarArgs, aVarKeywords));
        }
        // (a static method)
        py static_method(char const* aClass, char const* aName, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false,
            bool aVarKeywords = false) const
        {
            return builtin("staticmethod")(function(aName, std::string{ aClass } + "." + aName, std::move(aParameters), std::move(aBody), aVarArgs, aVarKeywords));
        }
        // (a property (its getter: of self))
        py property(char const* aClass, char const* aName, std::function<py(py const&)> aGetter) const
        {
            return builtin("property")(method(aClass, aName, {}, [aGetter](arguments a) { return aGetter(a[0]); }));
        }
        // (a class of the module: type(aName, aBases, namespace) (its metaclass: its bases' most derived); its docstring: the module's)
        py make_class(char const* aName, std::vector<py> const& aBases, namespace_type const& aNamespace, char const* aQualname = nullptr) const
        {
            std::vector<py> bases = aBases.empty() ? std::vector<py>{ builtin("object") } : aBases;
            // (bases that are not classes: their __mro_entries__'s (e.g. typing.NamedTuple's), as the class statement resolves them)
            bool resolved = false;
            {
                std::vector<py> entries;
                for (auto const& b : bases)
                    if (!isinstance(b, builtin("type")) && has_attr(b, "__mro_entries__"))
                    {
                        for (py const& e : b.attr("__mro_entries__")(py_tuple(aBases)).items())
                            entries.push_back(e);
                        resolved = true;
                    }
                    else
                        entries.push_back(b);
                if (resolved)
                    bases = std::move(entries);
            }
            py meta = builtin("type");
            for (auto const& b : bases)
            {
                py const t = type_of(b);
                if (!t.is(meta) && issubclass(t, meta))
                    meta = t;
            }
            // (its namespace: its metaclass's __prepare__'s (e.g. an Enum's))
            py const prepare = meta.is(builtin("type")) ? py{} : get_attr(meta, "__prepare__", none());
            py const ns = prepare.empty() || prepare.is_none() ? py_dict() : prepare(S(aName), py_tuple(bases));
            ns.set_item(S("__module__"), S(module));
            ns.set_item(S("__qualname__"), S(aQualname != nullptr ? aQualname : aName));
            if (docs != nullptr)
                if (auto const d = docs(aQualname != nullptr ? aQualname : aName))
                    ns.set_item(S("__doc__"), S(d));
            for (auto const& [name, value] : aNamespace)
                ns.set_item(S(name), value);
            if (resolved)
                ns.set_item(S("__orig_bases__"), py_tuple(aBases));
            return meta(S(aName), py_tuple(bases), ns);
        }
    };

    // (try: aBody() except aClass (a class or a tuple of classes) as e: aHandler(e) (sys.exception() is e while it runs); another
    // exception remains raised)
    template <typename F, typename H>
    inline void try_except(F&& aBody, py const& aClass, H&& aHandler)
    {
        try
        {
            aBody();
        }
        catch (raised const&)
        {
            std::string const text = error_text();
            clear_error();
            ref const caughtException{ fn_caught_natively(text.c_str()) };
            py const e = py::borrowed(caughtException.get());
            if (!isinstance(e, aClass))
            {
                fn_handled();
                py_reraise(e);
            }
            try
            {
                aHandler(e);
            }
            catch (raised const&)
            {
                fn_handled();
                throw;
            }
            fn_handled();
        }
    }
    // (try: aBody() finally: aFinally())
    template <typename F, typename G>
    inline void try_finally(F&& aBody, G&& aFinally)
    {
        try
        {
            aBody();
        }
        catch (raised const&)
        {
            py const e = py_take_exception();
            aFinally();
            py_reraise(e);
        }
        aFinally();
    }
    // (type(aObject).aName, as the with statement finds __enter__ and __exit__; a native type's (whose methods are its objects' only, 
    // not its type's): a function of the object's method, called with the object first (as the type's would be))
    inline py special_method(py const& aObject, char const* aName)
    {
        if (ref const t{ getattr(type_of(aObject).get(), aName) })
            return py::borrowed(t.get());
        clear_error();
        py const bound = aObject.attr(aName);
        return make_native_function(aName, aName, "", "", [bound](std::vector<py> const& aArgs, object* aKwargs) -> py
        {
            std::vector<py> rest;
            if (!aArgs.empty())
                rest.assign(aArgs.begin() + 1, aArgs.end());
            if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                py_raise("TypeError", std::string{ "unexpected keyword arguments" });
            return bound.call(rest);
        });
    }
    // (with aManager as value: aBody(value))
    template <typename F>
    inline void with_(py const& aManager, F&& aBody)
    {
        py const value = aManager.method("__enter__");
        try
        {
            aBody(value);
        }
        catch (raised const&)
        {
            py const e = py_take_exception();
            if (!aManager.method("__exit__", { type_of(e), e, e.attr("__traceback__") }).truthy())
                py_reraise(e);
            return;
        }
        aManager.method("__exit__", { none(), none(), none() });
    }
    // (async with aManager as value: aBody(value) (in a coroutine))
    template <typename F>
    inline void async_with(py const& aManager, F&& aBody)
    {
        py const exit = type_of(aManager).attr("__aexit__");
        py const value = py_await(type_of(aManager).attr("__aenter__")(aManager));
        try
        {
            aBody(value);
        }
        catch (raised const&)
        {
            py const e = py_take_exception();
            if (!py_await(exit(aManager, type_of(e), e, e.attr("__traceback__"))).truthy())
                py_reraise(e);
            return;
        }
        py_await(exit(aManager, none(), none(), none()));
    }
    // (a pair's items (a, b = value))
    inline std::pair<py, py> unpack2(py const& aValue)
    {
        auto const items = aValue.items();
        if (items.size() != 2u)
            py_raise("ValueError", items.size() < 2u ? "not enough values to unpack (expected 2, got " + std::to_string(items.size()) + ")" :
                "too many values to unpack (expected 2)");
        return { items[0], items[1] };
    }
    // (aFormat % aArgs)
    inline py format(char const* aFormat, std::vector<py> const& aArgs) { return py{ aFormat } % py_tuple(aArgs); }

    // --- the helpers of code translated from Python (by py2neos.py)

    // (an except clause: the exception being handled (sys.exception() is it while the handler runs))
    struct handler_scope
    {
        py exception;
        handler_scope()
        {
            std::string const text = error_text();
            clear_error();
            exception = py{ ref{ fn_caught_natively(text.c_str()) } };
        }
        ~handler_scope() { fn_handled(); }
        handler_scope(handler_scope const&) = delete;
        handler_scope& operator=(handler_scope const&) = delete;
    };
    // (the exception being handled (sys.exception()): a bare raise's outside an except clause; RuntimeError if there is none)
    inline py handled_exception()
    {
        py const current = py{ fn_import("sys") }.method("exception", {});
        if (current.is_none())
            py_raise(builtin("RuntimeError")(py{ std::string{ "No active exception to reraise" } }));
        return current;
    }
    // (except aClass: whether the exception matches it (a class or a tuple of classes))
    inline bool matches(py const& aException, py const& aClass) { return fn_exception_matches(aException.get(), aClass.get()); }

    // (for item in iterable: its items, as they are iterated)
    class iterate
    {
    public:
        class iterator
        {
        public:
            iterator() = default;
            explicit iterator(py const& aIterator) : iIterator{ aIterator } { ++*this; }
            py const& operator*() const { return iItem; }
            iterator& operator++()
            {
                auto const n = detail::next(iIterator.get());
                if (n == nullptr)
                {
                    py_checked(!failed());
                    iDone = true;
                    iItem = py{};
                }
                else
                    iItem = py{ ref{ n } };
                return *this;
            }
            bool operator!=(iterator const& aOther) const { return iDone != aOther.iDone; }
        private:
            py iIterator;
            py iItem;
            bool iDone = true;
            friend class iterate;
        };
    public:
        explicit iterate(py const& aIterable) : iIterator{ ref{ detail::iter(aIterable.get()) } } {}
        iterator begin() const
        {
            iterator result;
            result.iIterator = iIterator;
            result.iDone = false;
            ++result;
            return result;
        }
        iterator end() const { return iterator{}; }
    private:
        py iIterator;
    };

    // (a, b, ... = value: its items (Python's messages if there are not aCount))
    inline std::vector<py> unpack(py const& aValue, std::size_t aCount)
    {
        auto items = aValue.items();
        if (items.size() < aCount)
            py_raise("ValueError", "not enough values to unpack (expected " + std::to_string(aCount) + ", got " + std::to_string(items.size()) + ")");
        if (items.size() > aCount)
            py_raise("ValueError", "too many values to unpack (expected " + std::to_string(aCount) + ")");
        return items;
    }

    // (f(*args, **kwargs) with positional and keyword arguments too)
    inline py call_ex(py const& aFunction, std::vector<py> aPositional, py const& aStar, py const& aDoubleStar,
        std::vector<std::pair<char const*, py>> const& aKeywords = {})
    {
        if (!aStar.empty())
            for (auto const& x : iterate{ aStar })
                aPositional.push_back(x);
        std::vector<object*> args;
        for (auto const& a : aPositional)
            args.push_back(a.reference());
        ref kwargs;
        if (!aDoubleStar.empty() || !aKeywords.empty())
        {
            kwargs = ref{ make_dict() };
            if (!aDoubleStar.empty())
                py_checked(dict_update(kwargs.get(), aDoubleStar.get()));
            for (auto const& [name, value] : aKeywords)
            {
                ref const key{ make_str(name) };
                dict_set(kwargs.get(), key.get(), value.get());
            }
        }
        return py{ invoke(aFunction.get(), std::move(args), kwargs.get()) };
    }

    // ({k: v, ...})
    inline py dict_of(std::vector<std::pair<py, py>> const& aItems)
    {
        py const d = py_dict();
        for (auto const& [k, v] : aItems)
            d.set_item(k, v);
        return d;
    }
    // ({a, b, ...})
    inline py set_of(std::vector<py> const& aItems) { return builtin("set")(py_list(aItems)); }
    // (x += y, x |= y etc.: in place)
    inline py inplace(int aOperation, py const& a, py const& b) { return py{ binary(aOperation, a.get(), b.get(), true) }; }
    // (del obj.name, del obj[key])
    inline void del_attr(py const& aObject, char const* aName) { py_checked(setattr(aObject.get(), aName, nullptr) == 0); }
    inline void del_item(py const& aObject, py const& aKey) { py_checked(setitem(aObject.get(), aKey.get(), nullptr) == 0); }
    // (f-strings' fields: format(value, spec))
    inline std::string formatted(py const& aValue, char const* aSpec) { return builtin("format")(aValue, py{ aSpec }).str(); }
    // (yield from iterable: its result)
    inline py yield_from(py const& aIterable) { return py{ b_yield_from({ aIterable.get() }, nullptr) }; }
    // (not x, x is y: as values)
    inline py truth_of(bool aValue) { return boolean(aValue); }
    // (import name: the module)
    inline py module_of(char const* aName) { return py{ fn_import(aName) }; }
}
