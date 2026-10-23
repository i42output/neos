/*
  typing.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/typing.py and Objects/typevarobject.c), Copyright (c) 2001-2024
  Python Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2
  (see runtime/python/LICENSE.PSF). Changes: translated from Python and C to C++ for the neos Python runtime plugin and
  adapted to its object model (see runtime/python/NOTICE).
*/

// The module typing (CPython 3.13's typing.py, and its C part (_typing: TypeVar, ParamSpec, TypeVarTuple, ParamSpecArgs, ParamSpecKwargs,
// TypeAliasType, Generic, NoDefault)): the special forms (Any, Union, Optional, Literal, ClassVar, Final, Annotated, ...), the generic
// aliases (List, Dict, Callable, Tuple, ...), Generic and Protocol, ForwardRef and get_type_hints, NamedTuple, TypedDict, NewType, and the
// decorators and helpers (cast, overload, final, override, runtime_checkable, ...); its classes are made (by type) of closures

#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "typing_docs.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        char const* const sModule = "typing";

        // (the module's objects: made once (and never destroyed: they outlive the program's objects))
        struct module_state
        {
            std::map<std::string, py, std::less<>> classes;     ///< (the module's classes, by name)
            std::map<std::string, py, std::less<>> objects;     ///< (its other objects (special forms, aliases, ...), by name)
            std::vector<std::string> order;                     ///< (the objects' names, in order of their making)
            std::map<std::string, py, std::less<>> caches;      ///< (_tp_cache's: of each function, a dict)
            py sentinel;
            py noDefault;
            py overloadRegistry;
            py cleanups;
            bool made = false;
        };
        module_state& state()
        {
            static module_state* sState = new module_state{};
            return *sState;
        }

        py cls(char const* aName)
        {
            auto const c = state().classes.find(aName);
            if (c == state().classes.end())
                py_raise("NameError", "name '" + std::string{ aName } + "' is not defined");
            return c->second;
        }
        py obj(char const* aName)
        {
            auto const o = state().objects.find(aName);
            if (o != state().objects.end())
                return o->second;
            return cls(aName);
        }
        void define(char const* aName, py const& aValue)
        {
            auto& s = state();
            if (!s.objects.contains(aName))
                s.order.push_back(aName);
            s.objects[aName] = aValue;
        }

        py none() { return py::none(); }
        py boolean(bool aValue) { return py::boolean(aValue); }
        py ellipsis() { return py::borrowed(&sEllipsis.base); }
        bool is_str(py const& aValue) { return kind_of(aValue.get()) == NEOS_PY_STR; }
        bool is_tuple(py const& aValue) { return isinstance(aValue, builtin("tuple")); }
        bool is_type(py const& aValue) { return isinstance(aValue, builtin("type")); }
        py types_attr(char const* aName) { return import_attribute("types", aName); }
        py none_type() { return type_of(none()); }
        py not_implemented_value() { return py::borrowed(not_implemented()); }

        py attr_or(py const& aObject, char const* aName, py const& aDefault)
        {
            try
            {
                return aObject.attr(aName);
            }
            catch (raised const&)
            {
                if (!caught({ "AttributeError" }))
                    throw;
                return aDefault;
            }
        }
        bool has_attr(py const& aObject, char const* aName)
        {
            try
            {
                aObject.attr(aName);
                return true;
            }
            catch (raised const&)
            {
                if (!caught({ "AttributeError" }))
                    throw;
                return false;
            }
        }
        bool in_tuple(py const& aItem, std::vector<py> const& aItems)
        {
            for (auto const& i : aItems)
                if (aItem.is(i) || aItem == i)
                    return true;
            return false;
        }
        py tuple_of(py const& aValue) { return is_tuple(aValue) ? aValue : py_tuple({ aValue }); }
        py slice(py const& aSequence, std::optional<long long> aStart, std::optional<long long> aStop)
        {
            py const start = aStart ? py{ *aStart } : none();
            py const stop = aStop ? py{ *aStop } : none();
            return aSequence[py{ make_slice(start.get(), stop.get(), nullptr) }];
        }
        py concat(std::vector<py> const& aParts)
        {
            std::vector<py> items;
            for (auto const& p : aParts)
                for (auto const& i : p.items())
                    items.push_back(i);
            return py_tuple(items);
        }

        [[noreturn]] void raise_from(py const& aException, py const& aCause)
        {
            aException.set_attr("__cause__", aCause);
            aException.set_attr("__suppress_context__", boolean(true));
            py_raise(aException);
        }

        // (a closure as a function of the module)
        py function(std::string const& aName, std::string const& aQualname, native_body aBody, char const* aDoc = "")
        {
            return make_native_function(aName, aQualname, aDoc, sModule, std::move(aBody));
        }

        using body_type = std::function<py(std::vector<py> const&)>;

        // (a function of its parameters (aVarArgs: *args, appended; aVarKeywords: **kwargs, appended))
        py bound_function(std::string const& aName, std::string const& aQualname, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false,
            bool aVarKeywords = false, char const* aDoc = "")
        {
            return function(aName, aQualname, [aQualname, aParameters, aBody, aVarArgs, aVarKeywords](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                py rest;
                py extra;
                auto a = py_bind(aQualname, aParameters, aArgs, aKwargs, aVarArgs ? &rest : nullptr, aVarKeywords ? &extra : nullptr);
                if (aVarArgs)
                    a.push_back(rest);
                if (aVarKeywords)
                    a.push_back(extra);
                return aBody(a);
            }, aDoc);
        }
        py module_function(char const* aName, std::vector<py_parameter> aParameters, body_type aBody, char const* aDoc = "", bool aVarArgs = false,
            bool aVarKeywords = false)
        {
            return bound_function(aName, aName, std::move(aParameters), std::move(aBody), aVarArgs, aVarKeywords, aDoc);
        }
        // (a method: its parameters after self)
        py method(char const* aClass, char const* aName, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false, bool aVarKeywords = false)
        {
            aParameters.insert(aParameters.begin(), py_positional_only("self"));
            return bound_function(aName, std::string{ aClass } + "." + aName, std::move(aParameters), std::move(aBody), aVarArgs, aVarKeywords);
        }
        py property_of(char const* aClass, char const* aName, std::function<py(py const&)> aGetter)
        {
            return builtin("property")(method(aClass, aName, {}, [aGetter](std::vector<py> const& a) { return aGetter(a[0]); }));
        }
        py classmethod_of(py const& aFunction) { return builtin("classmethod")(aFunction); }
        py staticmethod_of(py const& aFunction) { return builtin("staticmethod")(aFunction); }

        using namespace_type = std::vector<std::pair<char const*, py>>;

        // (a class of the module: aMetaclass(aName, aBases, namespace, **aKeywords))
        py make_typing_class(char const* aName, std::vector<py> const& aBases, namespace_type const& aNamespace, char const* aDoc = nullptr, py aMetaclass = {},
            std::vector<std::pair<char const*, py>> const& aKeywords = {}, char const* aModule = sModule)
        {
            py const ns = py_dict();
            ns.set_item(py{ "__module__" }, py{ aModule });
            ns.set_item(py{ "__qualname__" }, py{ aName });
            if (aDoc != nullptr)
                ns.set_item(py{ "__doc__" }, py{ aDoc });
            for (auto const& [name, value] : aNamespace)
                ns.set_item(py{ name }, value);
            std::vector<py> const bases = aBases.empty() ? std::vector<py>{ builtin("object") } : aBases;
            py meta = aMetaclass.empty() ? builtin("type") : aMetaclass;
            if (aMetaclass.empty())
                for (auto const& b : bases)
                {
                    py const t = type_of(b);
                    if (!t.is(meta) && issubclass(t, meta))
                        meta = t;
                }
            py const result = meta.call({ py{ aName }, py_tuple(bases), ns }, aKeywords);
            state().classes[aName] = result;
            return result;
        }

        // (the module of the code that called the module's function (_caller: sys._getframemodulename; here the innermost Python frame's))
        py caller(py const& aDefault = py{ "__main__" })
        {
            try
            {
                return import_attribute("sys", "_getframe")(py{ 0 }).attr("f_globals").method("get", { py{ "__name__" }, aDefault });
            }
            catch (raised const&)
            {
                if (!caught({ "AttributeError", "ValueError" }))
                    throw;
                return none();
            }
        }

        // --- _tp_cache: the results of a function, by its arguments (unhashable arguments: not cached)

        py tp_cache(char const* aFunction, std::vector<py> const& aArgs, bool aTyped, std::function<py()> const& aCompute)
        {
            auto& s = state();
            auto& cache = s.caches[aFunction];
            if (cache.empty())
            {
                cache = py_dict();
                py const c = cache;
                s.cleanups.method("append", { function("cache_clear", std::string{ aFunction } + ".cache_clear", [c](std::vector<py> const&, object*) -> py
                {
                    c.method("clear");
                    return none();
                }) });
            }
            std::vector<py> key = aArgs;
            if (aTyped)
                for (auto const& a : aArgs)
                    key.push_back(type_of(a));
            py const k = py_tuple(key);
            try
            {
                if (cache.contains(k))
                    return cache[k];
            }
            catch (raised const&)
            {
                if (!caught({ "TypeError" }))
                    throw;
                return aCompute();
            }
            py result = aCompute();
            cache.set_item(k, result);
            return result;
        }

        // --- the module's functions (typing.py's)

        bool is_instance_of(py const& aObject, char const* aClass) { return isinstance(aObject, cls(aClass)); }
        bool is_generic_alias(py const& aObject) { return is_instance_of(aObject, "_GenericAlias"); }
        bool is_types_generic_alias(py const& aObject) { return isinstance(aObject, types_attr("GenericAlias")); }
        bool is_union_type(py const& aObject) { return isinstance(aObject, types_attr("UnionType")); }

        py type_convert(py const& aArg, py const& aModule = none(), bool aAllowSpecialForms = false)
        {
            if (aArg.is_none())
                return none_type();
            if (is_str(aArg))
                return cls("ForwardRef").call({ aArg }, { { "module", aModule }, { "is_class", boolean(aAllowSpecialForms) } });
            return aArg;
        }

        py type_check(py aArg, std::string const& aMessage, bool aIsArgument = true, py const& aModule = none(), bool aAllowSpecialForms = false)
        {
            std::vector<py> invalid{ obj("Generic"), obj("Protocol") };
            if (!aAllowSpecialForms)
            {
                invalid.push_back(obj("ClassVar"));
                if (aIsArgument)
                    invalid.push_back(obj("Final"));
            }
            aArg = type_convert(aArg, aModule, aAllowSpecialForms);
            if (is_generic_alias(aArg) && in_tuple(aArg.attr("__origin__"), invalid))
                py_raise("TypeError", aArg.str() + " is not valid as type argument");
            if (in_tuple(aArg, { obj("Any"), obj("LiteralString"), obj("NoReturn"), obj("Never"), obj("Self"), obj("TypeAlias") }))
                return aArg;
            if (aAllowSpecialForms && in_tuple(aArg, { obj("ClassVar"), obj("Final") }))
                return aArg;
            if (is_instance_of(aArg, "_SpecialForm") || in_tuple(aArg, { obj("Generic"), obj("Protocol") }))
                py_raise("TypeError", "Plain " + aArg.str() + " is not valid as type argument");
            if (type_of(aArg).is(builtin("tuple")))
            {
                std::string text = aArg.repr();
                py const truncated = slice(py{ text }, std::nullopt, 100);
                py_raise("TypeError", aMessage + " Got " + truncated.str() + ".");
            }
            return aArg;
        }

        bool is_param_expr(py const& aArg)
        {
            return aArg.is(ellipsis()) || isinstance(aArg, builtin("tuple")) || isinstance(aArg, builtin("list")) || is_instance_of(aArg, "ParamSpec") ||
                is_instance_of(aArg, "_ConcatenateGenericAlias");
        }

        bool should_unflatten_callable_args(py const& aType, py const& aArgs)
        {
            return aType.attr("__origin__").is(import_attribute("collections.abc", "Callable")) && !(aArgs.size() == 2 && is_param_expr(aArgs[py{ 0 }]));
        }

        std::string type_repr(py const& aObject)
        {
            if (is_type(aObject))
            {
                if (aObject.attr("__module__") == py{ "builtins" })
                    return aObject.attr("__qualname__").str();
                return aObject.attr("__module__").str() + "." + aObject.attr("__qualname__").str();
            }
            if (aObject.is(ellipsis()))
                return "...";
            if (isinstance(aObject, types_attr("FunctionType")))
                return aObject.attr("__name__").str();
            if (isinstance(aObject, builtin("tuple")))
            {
                std::string text;
                for (auto const& t : aObject.items())
                    text += (text.empty() ? "" : ", ") + type_repr(t);
                return "[" + text + "]";
            }
            return aObject.repr();
        }

        bool is_unpacked_typevartuple(py const& aObject)
        {
            return !is_type(aObject) && attr_or(aObject, "__typing_is_unpacked_typevartuple__", boolean(false)).truthy();
        }

        bool is_typevar_like(py const& aObject)
        {
            return is_instance_of(aObject, "TypeVar") || is_instance_of(aObject, "ParamSpec") || is_unpacked_typevartuple(aObject);
        }

        py collect_type_parameters(py const& aArgs, bool aEnforceDefaultOrdering = true)
        {
            bool defaultEncountered = false;
            bool typeVarTupleEncountered = false;
            py const parameters = py_list();
            for (auto const& t : aArgs.items())
            {
                if (is_type(t))
                    ;
                else if (isinstance(t, builtin("tuple")))
                {
                    for (auto const& x : t.items())
                        for (auto const& collected : collect_type_parameters(py_list({ x })).items())
                            if (!parameters.contains(collected))
                                parameters.method("append", { collected });
                }
                else if (has_attr(t, "__typing_subst__"))
                {
                    if (!parameters.contains(t))
                    {
                        if (aEnforceDefaultOrdering)
                        {
                            bool const hasDefault = t.method("has_default").truthy();
                            if (typeVarTupleEncountered && hasDefault)
                                py_raise("TypeError", "Type parameter with a default follows TypeVarTuple");
                            if (hasDefault)
                                defaultEncountered = true;
                            else if (defaultEncountered)
                                py_raise("TypeError", "Type parameter " + t.repr() + " without a default follows type parameter with a default");
                        }
                        parameters.method("append", { t });
                    }
                }
                else
                {
                    if (is_unpacked_typevartuple(t))
                        typeVarTupleEncountered = true;
                    for (auto const& x : attr_or(t, "__parameters__", py_tuple()).items())
                        if (!parameters.contains(x))
                            parameters.method("append", { x });
                }
            }
            return py_tuple(parameters.items());
        }

        void check_generic_specialization(py const& aClass, py const& aArguments)
        {
            auto expectedLen = aClass.attr("__parameters__").size();
            if (expectedLen == 0)
                py_raise("TypeError", aClass.str() + " is not a generic class");
            auto const actualLen = aArguments.size();
            if (actualLen != expectedLen)
            {
                std::string expectVal;
                if (actualLen < expectedLen)
                {
                    if (aClass.attr("__parameters__")[py{ actualLen }].method("has_default").truthy())
                        return;
                    for (auto const& p : aClass.attr("__parameters__").items())
                        if (p.method("has_default").truthy())
                            --expectedLen;
                    expectVal = "at least " + std::to_string(expectedLen);
                }
                else
                    expectVal = std::to_string(expectedLen);
                py_raise("TypeError", std::string{ "Too " } + (actualLen > expectedLen ? "many" : "few") + " arguments for " + aClass.str() + "; actual " +
                    std::to_string(actualLen) + ", expected " + expectVal);
            }
        }

        std::vector<py> unpack_args(std::vector<py> const& aArgs)
        {
            std::vector<py> newargs;
            for (auto const& arg : aArgs)
            {
                py const subargs = attr_or(arg, "__typing_unpacked_tuple_args__", none());
                if (!subargs.is_none() && !(subargs.truthy() && subargs[py{ -1 }].is(ellipsis())))
                    for (auto const& s : subargs.items())
                        newargs.push_back(s);
                else
                    newargs.push_back(arg);
            }
            return newargs;
        }

        std::vector<py> deduplicate_unhashable(std::vector<py> const& aParams)
        {
            std::vector<py> result;
            for (auto const& t : aParams)
            {
                bool found = false;
                for (auto const& r : result)
                    if (r == t)
                        found = true;
                if (!found)
                    result.push_back(t);
            }
            return result;
        }

        // (_deduplicate: the parameters (by dict.fromkeys) without duplicates)
        std::vector<py> deduplicate(std::vector<py> const& aParams, bool aUnhashableFallback = false)
        {
            try
            {
                return builtin("dict").attr("fromkeys")(py_list(aParams)).items();
            }
            catch (raised const&)
            {
                if (!aUnhashableFallback || !caught({ "TypeError" }))
                    throw;
                return deduplicate_unhashable(aParams);
            }
        }

        bool compare_args_orderless(py const& aFirst, py const& aSecond)
        {
            auto const first = deduplicate_unhashable(aFirst.items());
            py const t = py_list(deduplicate_unhashable(aSecond.items()));
            try
            {
                for (auto const& e : first)
                    t.method("remove", { e });
            }
            catch (raised const&)
            {
                if (!caught({ "ValueError" }))
                    throw;
                return false;
            }
            return !t.truthy();
        }

        py remove_dups_flatten(py const& aParameters)
        {
            std::vector<py> params;
            for (auto const& p : aParameters.items())
            {
                if (is_instance_of(p, "_UnionGenericAlias") || is_union_type(p))
                {
                    for (auto const& a : p.attr("__args__").items())
                        params.push_back(a);
                }
                else
                    params.push_back(p);
            }
            return py_tuple(deduplicate(params, true));
        }

        py flatten_literal_params(py const& aParameters)
        {
            std::vector<py> params;
            for (auto const& p : aParameters.items())
            {
                if (is_instance_of(p, "_LiteralGenericAlias"))
                {
                    for (auto const& a : p.attr("__args__").items())
                        params.push_back(a);
                }
                else
                    params.push_back(p);
            }
            return py_tuple(params);
        }

        py union_of(py const& aLeft, py const& aRight) { return obj("Union")[py_tuple({ aLeft, aRight })]; }

        py eval_type(py t, py const& aGlobals, py const& aLocals, py const& aTypeParams, py const& aRecursiveGuard);

        // --- ForwardRef

        std::string rewrite_star_unpack(std::string const& aArg)
        {
            if (aArg.starts_with("*"))
                return "(" + aArg + ",)[0]";
            return aArg;
        }

        py forward_ref_evaluate(py const& self, py aGlobals, py aLocals, py const& aTypeParams, py const& aRecursiveGuard)
        {
            if (aRecursiveGuard.contains(self.attr("__forward_arg__")))
                return self;
            if (!self.attr("__forward_evaluated__").truthy() || !aLocals.is(aGlobals))
            {
                if (aGlobals.is_none() && aLocals.is_none())
                    aGlobals = aLocals = py_dict();
                else if (aGlobals.is_none())
                    aGlobals = aLocals;
                else if (aLocals.is_none())
                    aLocals = aGlobals;
                py const module = self.attr("__forward_module__");
                if (!module.is_none())
                    aGlobals = attr_or(import_attribute("sys", "modules").method("get", { module, none() }), "__dict__", aGlobals);
                if (aTypeParams.truthy())
                {
                    aGlobals = builtin("dict")(aGlobals);
                    aLocals = builtin("dict")(aLocals);
                    for (auto const& param : aTypeParams.items())
                    {
                        py const name = param.attr("__name__");
                        if (!self.attr("__forward_is_class__").truthy() || !aGlobals.contains(name))
                        {
                            aGlobals.set_item(name, param);
                            aLocals.method("pop", { name, none() });
                        }
                    }
                }
                py const value = builtin("eval")(self.attr("__forward_code__"), aGlobals, aLocals);
                py const t = type_check(value, "Forward references must evaluate to types.", self.attr("__forward_is_argument__").truthy(), none(),
                    self.attr("__forward_is_class__").truthy());
                py const guard = aRecursiveGuard | builtin("frozenset")(py_list({ self.attr("__forward_arg__") }));
                self.set_attr("__forward_value__", eval_type(t, aGlobals, aLocals, aTypeParams, guard));
                self.set_attr("__forward_evaluated__", boolean(true));
            }
            return self.attr("__forward_value__");
        }

        py eval_type(py t, py const& aGlobals, py const& aLocals, py const& aTypeParams, py const& aRecursiveGuard)
        {
            if (is_instance_of(t, "ForwardRef"))
                return forward_ref_evaluate(t, aGlobals, aLocals, aTypeParams, aRecursiveGuard);
            if (is_generic_alias(t) || is_types_generic_alias(t) || is_union_type(t))
            {
                if (is_types_generic_alias(t))
                {
                    std::vector<py> args;
                    for (auto const& arg : t.attr("__args__").items())
                        args.push_back(is_str(arg) ? cls("ForwardRef")(arg) : arg);
                    py const argsTuple = py_tuple(args);
                    bool const isUnpacked = attr_or(t, "__unpacked__", boolean(false)).truthy();
                    if (should_unflatten_callable_args(t, argsTuple))
                        t = t.attr("__origin__")[py_tuple({ slice(argsTuple, std::nullopt, -1), argsTuple[py{ -1 }] })];
                    else
                        t = t.attr("__origin__")[argsTuple];
                    if (isUnpacked)
                        t = obj("Unpack")[t];
                }
                std::vector<py> evArgs;
                for (auto const& a : t.attr("__args__").items())
                    evArgs.push_back(eval_type(a, aGlobals, aLocals, aTypeParams, aRecursiveGuard));
                py const ev = py_tuple(evArgs);
                if (ev == t.attr("__args__"))
                    return t;
                if (is_types_generic_alias(t))
                    return types_attr("GenericAlias")(t.attr("__origin__"), ev);
                if (is_union_type(t))
                    return import_attribute("functools", "reduce")(import_attribute("operator", "or_"), ev);
                return t.method("copy_with", { ev });
            }
            return t;
        }

        // --- the type variables' substitutions (typing.py's)

        py typevar_subst(py const& self, py aArg)
        {
            aArg = type_check(aArg, "Parameters to generic types must be types.", true);
            if ((is_generic_alias(aArg) && aArg.attr("__origin__").is(obj("Unpack"))) ||
                (is_types_generic_alias(aArg) && attr_or(aArg, "__unpacked__", boolean(false)).truthy()))
                py_raise("TypeError", aArg.str() + " is not valid as type argument");
            (void)self;
            return aArg;
        }

        py typevartuple_prepare_subst(py const& self, py const& aAlias, py const& aArgs)
        {
            py const params = aAlias.attr("__parameters__");
            auto const typevartupleIndex = params.method("index", { self }).as_index();
            for (auto const& param : slice(params, typevartupleIndex + 1, std::nullopt).items())
                if (is_instance_of(param, "TypeVarTuple"))
                    py_raise("TypeError", "More than one TypeVarTuple parameter in " + aAlias.str());
            auto const alen = aArgs.size();
            auto const plen = params.size();
            auto left = typevartupleIndex;
            auto right = plen - typevartupleIndex - 1;
            std::optional<long long> varTupleIndex;
            py fillarg = none();
            auto const args = aArgs.items();
            for (std::size_t k = 0u; k < args.size(); ++k)
            {
                auto const& arg = args[k];
                if (!is_type(arg))
                {
                    py const subargs = attr_or(arg, "__typing_unpacked_tuple_args__", none());
                    if (subargs.truthy() && subargs.size() == 2 && subargs[py{ -1 }].is(ellipsis()))
                    {
                        if (varTupleIndex)
                            py_raise("TypeError", "More than one unpacked arbitrary-length tuple argument");
                        varTupleIndex = static_cast<long long>(k);
                        fillarg = subargs[py{ 0 }];
                    }
                }
            }
            if (varTupleIndex)
            {
                left = std::min(left, *varTupleIndex);
                right = std::min(right, alen - *varTupleIndex - 1);
            }
            else if (left + right > alen)
                py_raise("TypeError", "Too few arguments for " + aAlias.str() + "; actual " + std::to_string(alen) + ", expected at least " + std::to_string(plen - 1));
            py replacement;
            if (left == alen - right && self.method("has_default").truthy())
                replacement = py_tuple(unpack_args({ self.attr("__default__") }));
            else
                replacement = slice(aArgs, left, alen - right);
            std::vector<py> result;
            for (long long i = 0; i < left; ++i)
                result.push_back(args[static_cast<std::size_t>(i)]);
            for (long long i = 0; i < typevartupleIndex - left; ++i)
                result.push_back(fillarg);
            result.push_back(replacement);
            for (long long i = 0; i < plen - right - left - typevartupleIndex - 1; ++i)
                result.push_back(fillarg);
            for (auto i = alen - right; i < alen; ++i)
                result.push_back(args[static_cast<std::size_t>(i)]);
            return py_tuple(result);
        }

        py paramspec_subst(py const& self, py aArg)
        {
            (void)self;
            if (isinstance(aArg, builtin("list")) || isinstance(aArg, builtin("tuple")))
            {
                std::vector<py> checked;
                for (auto const& a : aArg.items())
                    checked.push_back(type_check(a, "Expected a type."));
                aArg = py_tuple(checked);
            }
            else if (!is_param_expr(aArg))
                py_raise("TypeError", "Expected a list of types, an ellipsis, ParamSpec, or Concatenate. Got " + aArg.str());
            return aArg;
        }

        py paramspec_prepare_subst(py const& self, py const& aAlias, py aArgs)
        {
            py const params = aAlias.attr("__parameters__");
            auto const i = params.method("index", { self }).as_index();
            if (i == aArgs.size() && self.method("has_default").truthy())
                aArgs = concat({ aArgs, py_tuple({ self.attr("__default__") }) });
            if (i >= aArgs.size())
                py_raise("TypeError", "Too few arguments for " + aAlias.str());
            if (params.size() == 1 && !is_param_expr(aArgs[py{ 0 }]))
                aArgs = py_tuple({ aArgs });
            else if (isinstance(aArgs[py{ i }], builtin("list")))
                aArgs = concat({ slice(aArgs, std::nullopt, i), py_tuple({ builtin("tuple")(aArgs[py{ i }]) }), slice(aArgs, i + 1, std::nullopt) });
            return aArgs;
        }

        // --- Generic's (_generic_class_getitem and _generic_init_subclass)

        py generic_class_getitem(py const& aClass, py aArgs)
        {
            return tp_cache("_generic_class_getitem", { aClass, aArgs }, false, [&]() -> py
            {
                std::vector<py> converted;
                for (auto const& p : tuple_of(aArgs).items())
                    converted.push_back(type_convert(p));
                py args = py_tuple(converted);
                bool const genericOrProtocol = aClass.is(obj("Generic")) || aClass.is(obj("Protocol"));
                if (genericOrProtocol)
                {
                    if (!args.truthy())
                        py_raise("TypeError", "Parameter list to " + aClass.attr("__qualname__").str() + "[...] cannot be empty");
                    for (auto const& p : args.items())
                        if (!is_typevar_like(p))
                            py_raise("TypeError", "Parameters to " + aClass.attr("__name__").str() + "[...] must all be type variables or parameter specification variables.");
                    if (builtin("set")(args).size() != args.size())
                        py_raise("TypeError", "Parameters to " + aClass.attr("__name__").str() + "[...] must all be unique");
                }
                else
                {
                    py parameters;
                    try
                    {
                        parameters = aClass.attr("__parameters__");
                    }
                    catch (raised const&)
                    {
                        if (!caught({ "AttributeError" }))
                            throw;
                        py const e = py_take_exception();
                        py const initSubclass = attr_or(aClass, "__init_subclass__", none());
                        if (!initSubclass.is_none() && !(initSubclass == obj("Generic").attr("__init_subclass__")))
                            e.method("add_note", { py{ "Note: this exception may have been caused by " + attr_or(initSubclass, "__qualname__", py{ "" }).repr() +
                                " (or the '__init_subclass__' method on a superclass) not calling 'super().__init_subclass__()'" } });
                        py_reraise(e);
                    }
                    for (auto const& param : parameters.items())
                    {
                        py const prepare = attr_or(param, "__typing_prepare_subst__", none());
                        if (!prepare.is_none())
                            args = prepare(aClass, args);
                    }
                    check_generic_specialization(aClass, args);
                    std::vector<py> newArgs;
                    auto const params = parameters.items();
                    auto const values = args.items();
                    for (std::size_t i = 0u; i < params.size() && i < values.size(); ++i)
                    {
                        if (is_instance_of(params[i], "TypeVarTuple"))
                        {
                            for (auto const& v : values[i].items())
                                newArgs.push_back(v);
                        }
                        else
                            newArgs.push_back(values[i]);
                    }
                    args = py_tuple(newArgs);
                }
                return cls("_GenericAlias")(aClass, args);
            });
        }

        void generic_init_subclass(py const& aClass, py const& aArgs, py const& aKwargs)
        {
            py const super = builtin("super")(obj("Generic"), aClass);
            {
                std::vector<object*> args;
                for (auto const& a : aArgs.items())
                    args.push_back(a.reference());
                py{ invoke(super.attr("__init_subclass__").get(), std::move(args), aKwargs.size() > 0 ? aKwargs.get() : nullptr) };
            }
            py tvars = py_list();
            bool error;
            bool const hasOrigBases = aClass.attr("__dict__").contains(py{ "__orig_bases__" });
            if (hasOrigBases)
                error = in_tuple(obj("Generic"), aClass.attr("__orig_bases__").items());
            else
                error = in_tuple(obj("Generic"), aClass.attr("__bases__").items()) && aClass.attr("__name__").str() != "Protocol" &&
                    !type_of(aClass).is(cls("_TypedDictMeta"));
            if (error)
                py_raise("TypeError", "Cannot inherit from plain Generic");
            if (hasOrigBases)
            {
                tvars = collect_type_parameters(aClass.attr("__orig_bases__"));
                py gvars;
                for (auto const& base : aClass.attr("__orig_bases__").items())
                    if (is_generic_alias(base) && base.attr("__origin__").is(obj("Generic")))
                    {
                        // (class C[T, ...]: its type parameters (see __generic_base))
                        if (base.attr("__dict__").contains(py{ "__type_params__" }))
                            aClass.set_attr("__type_params__", base.attr("__dict__")[py{ "__type_params__" }]);
                        if (!gvars.empty())
                            py_raise("TypeError", "Cannot inherit from Generic[...] multiple times.");
                        gvars = base.attr("__parameters__");
                    }
                if (!gvars.empty())
                {
                    py const tvarset = builtin("set")(tvars);
                    py const gvarset = builtin("set")(gvars);
                    if (!py_compare(NEOS_PY_LE, tvarset, gvarset).truthy())
                    {
                        std::string sVars;
                        for (auto const& t : tvars.items())
                            if (!gvarset.contains(t))
                                sVars += (sVars.empty() ? "" : ", ") + t.str();
                        std::string sArgs;
                        for (auto const& g : gvars.items())
                            sArgs += (sArgs.empty() ? "" : ", ") + g.str();
                        py_raise("TypeError", "Some type variables (" + sVars + ") are not listed in Generic[" + sArgs + "]");
                    }
                    tvars = gvars;
                }
            }
            aClass.set_attr("__parameters__", builtin("tuple")(tvars));
        }

        bool is_dunder(std::string const& aAttribute) { return aAttribute.size() >= 4u && aAttribute.starts_with("__") && aAttribute.ends_with("__"); }

        // --- the special forms

        // (a special form's subscription (its function's): aParameters as given)
        using special_form_function = std::function<py(py const& aSelf, py const& aParameters)>;
        std::map<std::string, special_form_function, std::less<>>& special_form_functions()
        {
            static std::map<std::string, special_form_function, std::less<>>* sFunctions = new std::map<std::string, special_form_function, std::less<>>{};
            return *sFunctions;
        }

        // (a special form: an instance of _SpecialForm (or aClass) whose subscription is aFunction)
        py make_special_form(char const* aName, char const* aDoc, special_form_function aFunction, char const* aClass = "_SpecialForm")
        {
            special_form_functions()[aName] = std::move(aFunction);
            std::string const name = aName;
            // (a _TypedCacheSpecialForm's function: of its parameters (as a tuple))
            bool const variadic = std::strcmp(aClass, "_TypedCacheSpecialForm") == 0;
            py const getitem = function(aName, aName, [name, variadic](std::vector<py> const& aArgs, object*) -> py
            {
                if (aArgs.empty())
                    py_raise("TypeError", name + "() missing 1 required positional argument: 'self'");
                py const parameters = variadic ? py_tuple(std::vector<py>{ aArgs.begin() + 1, aArgs.end() }) : aArgs.at(1);
                return special_form_functions().at(name)(aArgs[0], parameters);
            }, aDoc);
            py const form = cls(aClass)(getitem);
            define(aName, form);
            return form;
        }

        [[noreturn]] void not_subscriptable(py const& self)
        {
            py_raise("TypeError", self.str() + " is not subscriptable");
        }

        // --- the generic aliases (_BaseGenericAlias's methods, which the classes that derive from it call as their super()'s)

        void base_generic_alias_init(py const& self, py const& aOrigin, bool aInst, py const& aName)
        {
            self.set_attr("_inst", boolean(aInst));
            self.set_attr("_name", aName);
            self.set_attr("__origin__", aOrigin);
            self.set_attr("__slots__", none());
        }

        py base_generic_alias_mro_entries(py const& self, py const& aBases)
        {
            std::vector<py> res;
            py const origin = self.attr("__origin__");
            if (!aBases.contains(origin))
                res.push_back(origin);
            auto const i = aBases.method("index", { self }).as_index();
            bool broke = false;
            for (auto const& b : slice(aBases, i + 1, std::nullopt).items())
            {
                if (is_instance_of(b, "_BaseGenericAlias"))
                {
                    broke = true;
                    break;
                }
                if (!is_type(b))
                {
                    py const meth = attr_or(b, "__mro_entries__", none());
                    py const newBases = meth.truthy() ? meth(aBases) : none();
                    if (is_tuple(newBases))
                    {
                        bool found = false;
                        for (auto const& b2 : newBases.items())
                            if (is_type(b2) && issubclass(b2, obj("Generic")))
                                found = true;
                        if (found)
                        {
                            broke = true;
                            break;
                        }
                    }
                }
                else if (issubclass(b, obj("Generic")))
                {
                    broke = true;
                    break;
                }
            }
            if (!broke)
                res.push_back(obj("Generic"));
            return py_tuple(res);
        }

        py base_generic_alias_getattr(py const& self, py const& aAttribute)
        {
            std::string const attribute = aAttribute.str();
            if (attribute == "__name__" || attribute == "__qualname__")
            {
                py const name = self.attr("_name");
                return name.truthy() ? name : self.attr("__origin__").attr("__name__");
            }
            if (self.attr("__dict__").contains(py{ "__origin__" }) && !is_dunder(attribute))
                return builtin("getattr")(self.attr("__origin__"), aAttribute);
            py_raise(builtin("AttributeError")(aAttribute));
        }

        void generic_alias_init(py const& self, py const& aOrigin, py aArgs, bool aInst, py const& aName)
        {
            base_generic_alias_init(self, aOrigin, aInst, aName);
            aArgs = tuple_of(aArgs);
            std::vector<py> args;
            for (auto const& a : aArgs.items())
                args.push_back(a.is(cls("_TypingEllipsis")) ? ellipsis() : a);
            self.set_attr("__args__", py_tuple(args));
            bool const enforce = aOrigin.is(obj("Generic")) || aOrigin.is(obj("Protocol"));
            self.set_attr("__parameters__", collect_type_parameters(aArgs, enforce));
            if (!aName.truthy())
                self.set_attr("__module__", aOrigin.attr("__module__"));
        }

        py generic_alias_repr(py const& self)
        {
            std::string name;
            if (self.attr("_name").truthy())
                name = "typing." + self.attr("_name").str();
            else
                name = type_repr(self.attr("__origin__"));
            std::string args;
            py const a = self.attr("__args__");
            if (a.truthy())
            {
                for (auto const& t : a.items())
                    args += (args.empty() ? "" : ", ") + type_repr(t);
            }
            else
                args = "()";
            return py{ name + "[" + args + "]" };
        }

        py generic_alias_reduce(py const& self)
        {
            py origin;
            if (self.attr("_name").truthy())
                origin = obj(self.attr("_name").str().c_str());
            else
                origin = self.attr("__origin__");
            py args = builtin("tuple")(self.attr("__args__"));
            if (args.size() == 1 && !is_tuple(args[py{ 0 }]))
                args = args[py{ 0 }];
            return py_tuple({ import_attribute("operator", "getitem"), py_tuple({ origin, args }) });
        }

        py generic_alias_make_substitution(py const& self, py const& aArgs, py const& aNewArgByParam)
        {
            std::vector<py> newArgs;
            for (auto const& oldArg : aArgs.items())
            {
                if (is_type(oldArg))
                {
                    newArgs.push_back(oldArg);
                    continue;
                }
                py newArg;
                py const substfunc = attr_or(oldArg, "__typing_subst__", none());
                if (substfunc.truthy())
                    newArg = substfunc(aNewArgByParam[oldArg]);
                else
                {
                    py const subparams = attr_or(oldArg, "__parameters__", py_tuple());
                    if (!subparams.truthy())
                        newArg = oldArg;
                    else
                    {
                        std::vector<py> subargs;
                        for (auto const& x : subparams.items())
                        {
                            if (is_instance_of(x, "TypeVarTuple"))
                            {
                                for (auto const& v : aNewArgByParam[x].items())
                                    subargs.push_back(v);
                            }
                            else
                                subargs.push_back(aNewArgByParam[x]);
                        }
                        newArg = oldArg[py_tuple(subargs)];
                    }
                }
                if (self.attr("__origin__") == import_attribute("collections.abc", "Callable") && is_tuple(newArg))
                {
                    for (auto const& v : newArg.items())
                        newArgs.push_back(v);
                }
                else if (is_unpacked_typevartuple(oldArg))
                {
                    for (auto const& v : newArg.items())
                        newArgs.push_back(v);
                }
                else if (is_tuple(oldArg))
                    newArgs.push_back(py_tuple(generic_alias_make_substitution(self, oldArg, aNewArgByParam).items()));
                else
                    newArgs.push_back(newArg);
            }
            return py_list(newArgs);
        }

        py generic_alias_determine_new_args(py const& self, py aArgs)
        {
            py const params = self.attr("__parameters__");
            for (auto const& param : params.items())
            {
                py const prepare = attr_or(param, "__typing_prepare_subst__", none());
                if (!prepare.is_none())
                    aArgs = prepare(self, aArgs);
            }
            auto const alen = aArgs.size();
            auto const plen = params.size();
            if (alen != plen)
                py_raise("TypeError", std::string{ "Too " } + (alen > plen ? "many" : "few") + " arguments for " + self.str() + "; actual " + std::to_string(alen) +
                    ", expected " + std::to_string(plen));
            py const newArgByParam = builtin("dict")(builtin("zip")(params, aArgs));
            return builtin("tuple")(self.method("_make_substitution", { self.attr("__args__"), newArgByParam }));
        }

        py generic_alias_getitem(py const& self, py const& aArgs)
        {
            return tp_cache("_GenericAlias.__getitem__", { self, aArgs }, false, [&]() -> py
            {
                py const origin = self.attr("__origin__");
                if (origin.is(obj("Generic")) || origin.is(obj("Protocol")))
                    py_raise("TypeError", "Cannot subscript already-subscripted " + self.str());
                if (!self.attr("__parameters__").truthy())
                    py_raise("TypeError", self.str() + " is not a generic class");
                std::vector<py> converted;
                for (auto const& p : tuple_of(aArgs).items())
                    converted.push_back(type_convert(p));
                py const args = py_tuple(unpack_args(converted));
                py const newArgs = self.method("_determine_new_args", { args });
                return self.method("copy_with", { newArgs });
            });
        }

        py generic_alias_mro_entries(py const& self, py const& aBases)
        {
            py const origin = self.attr("__origin__");
            if (is_instance_of(origin, "_SpecialForm"))
                py_raise("TypeError", "Cannot subclass " + self.repr());
            if (self.attr("_name").truthy())
                return base_generic_alias_mro_entries(self, aBases);
            if (origin.is(obj("Generic")))
            {
                if (aBases.contains(obj("Protocol")))
                    return py_tuple();
                auto const i = aBases.method("index", { self }).as_index();
                for (auto const& b : slice(aBases, i + 1, std::nullopt).items())
                    if (is_instance_of(b, "_BaseGenericAlias") && !b.is(self))
                        return py_tuple();
            }
            return py_tuple({ origin });
        }

        py special_generic_alias_getitem(py const& self, py const& aParams)
        {
            return tp_cache("_SpecialGenericAlias.__getitem__", { self, aParams }, false, [&]() -> py
            {
                std::vector<py> checked;
                for (auto const& p : tuple_of(aParams).items())
                    checked.push_back(type_check(p, "Parameters to generic types must be types."));
                py params = py_tuple(checked);
                py const defaults = self.attr("_defaults");
                auto const nparams = self.attr("_nparams").as_index();
                if (defaults.truthy() && params.size() < nparams && params.size() + defaults.size() >= nparams)
                    params = concat({ params, slice(defaults, params.size() - nparams, std::nullopt) });
                auto const actualLen = params.size();
                if (actualLen != nparams)
                {
                    std::string expected;
                    if (defaults.truthy())
                        expected = "at least " + std::to_string(nparams - defaults.size());
                    else
                        expected = std::to_string(nparams);
                    if (nparams == 0)
                        py_raise("TypeError", self.str() + " is not a generic class");
                    py_raise("TypeError", std::string{ "Too " } + (actualLen > nparams ? "many" : "few") + " arguments for " + self.str() + "; actual " +
                        std::to_string(actualLen) + ", expected " + expected);
                }
                return self.method("copy_with", { params });
            });
        }

        py special_generic_alias_subclasscheck(py const& self, py const& aClass)
        {
            if (is_instance_of(aClass, "_SpecialGenericAlias"))
                return boolean(issubclass(aClass.attr("__origin__"), self.attr("__origin__")));
            if (!is_generic_alias(aClass))
                return boolean(issubclass(aClass, self.attr("__origin__")));
            py_raise("TypeError", "Subscripted generics cannot be used with class and instance checks");
        }

        // --- the classes

        py union_method(char const* aClass, char const* aName, bool aReflected)
        {
            return method(aClass, aName, { py_positional("other") }, [aReflected](std::vector<py> const& a)
            {
                return aReflected ? union_of(a[1], a[0]) : union_of(a[0], a[1]);
            });
        }

        // (_typing's classes cannot be subclassed)
        py final_init_subclass(char const* aClass)
        {
            std::string const name = aClass;
            return classmethod_of(function("__init_subclass__", name + ".__init_subclass__", [name](std::vector<py> const&, object*) -> py
            {
                py_raise("TypeError", "type 'typing." + name + "' is not an acceptable base type");
            }));
        }

        py mro_entries_error(char const* aClass, char const* aWhat)
        {
            std::string const what = aWhat;
            return method(aClass, "__mro_entries__", { py_positional("bases") }, [what](std::vector<py> const&) -> py
            {
                py_raise("TypeError", "Cannot subclass an instance of " + what);
            });
        }

        // (a type variable's repr: its name, with its variance)
        py variance_repr(py const& self)
        {
            if (self.attr("__infer_variance__").truthy())
                return self.attr("__name__");
            char const* const prefix = self.attr("__covariant__").truthy() ? "+" : self.attr("__contravariant__").truthy() ? "-" : "~";
            return py{ prefix + self.attr("__name__").str() };
        }

        void check_name(char const* aFunction, py const& aName)
        {
            if (!is_str(aName))
                py_raise("TypeError", std::string{ aFunction } + "() argument 'name' must be str, not " + type_of(aName).attr("__name__").str());
        }

        void init_variance(py const& self, py const& aCovariant, py const& aContravariant, py const& aInferVariance)
        {
            bool const covariant = aCovariant.truthy();
            bool const contravariant = aContravariant.truthy();
            bool const infer = aInferVariance.truthy();
            if (covariant && contravariant)
                py_raise("ValueError", "Bivariant types are not supported.");
            if (infer && (covariant || contravariant))
                py_raise("ValueError", "Variance cannot be specified with infer_variance.");
            self.set_attr("__covariant__", boolean(covariant));
            self.set_attr("__contravariant__", boolean(contravariant));
            self.set_attr("__infer_variance__", boolean(infer));
        }

        void set_caller_module(py const& self)
        {
            py const module = caller(py{ "__main__" });
            if (!module.is_none())
                self.set_attr("__module__", module);
        }

        py has_default_method(char const* aClass)
        {
            return method(aClass, "has_default", {}, [](std::vector<py> const& a) { return boolean(!a[0].attr("__default__").is(state().noDefault)); });
        }

        void make_typing_core_classes()
        {
            auto& s = state();
            s.sentinel = make_typing_class("_Sentinel", {}, { { "__slots__", py_tuple() },
                { "__repr__", method("_Sentinel", "__repr__", {}, [](std::vector<py> const&) { return py{ "<sentinel>" }; }) } })();
            define("_sentinel", s.sentinel);

            // (NoDefault)
            py const noDefaultType = make_typing_class("NoDefaultType", {}, {
                { "__slots__", py_tuple() },
                { "__repr__", method("NoDefaultType", "__repr__", {}, [](std::vector<py> const&) { return py{ "typing.NoDefault" }; }) },
                { "__reduce__", method("NoDefaultType", "__reduce__", {}, [](std::vector<py> const&) { return py{ "NoDefault" }; }) } },
                typing_docs::doc("NoDefault"), {}, {}, "builtins");
            s.noDefault = noDefaultType();
            define("NoDefault", s.noDefault);

            // (_Final and _NotIterable)
            make_typing_class("_Final", {}, {
                { "__slots__", py_tuple({ py{ "__weakref__" } }) },
                { "__init_subclass__", classmethod_of(function("__init_subclass__", "_Final.__init_subclass__", [](std::vector<py> const&, object* aKwargs) -> py
                    {
                        if (aKwargs == nullptr || !py::borrowed(aKwargs).contains(py{ "_root" }))
                            py_raise("TypeError", "Cannot subclass special typing classes");
                        return none();
                    })) } },
                "Mixin to prohibit subclassing.");
            make_typing_class("_NotIterable", {}, { { "__slots__", py_tuple() }, { "__iter__", none() } },
                "Mixin to prevent iteration, without being compatible with Iterable.\n\n    That is, we could do::\n\n        def __iter__(self): raise TypeError()\n\n"
                "    But this would make users of this mixin duck type-compatible with\n    collections.abc.Iterable - isinstance(foo, Iterable) would be True.\n\n"
                "    Luckily, we can instead prevent iteration by setting __iter__ to None, which\n    is treated specially.\n    ");
            std::vector<std::pair<char const*, py>> const root{ { "_root", boolean(true) } };

            // (_SpecialForm and _TypedCacheSpecialForm)
            make_typing_class("_SpecialForm", { cls("_Final"), cls("_NotIterable") }, {
                { "__slots__", py_tuple({ py{ "_name" }, py{ "__doc__" }, py{ "_getitem" } }) },
                { "__init__", method("_SpecialForm", "__init__", { py_positional("getitem") }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("_getitem", a[1]);
                        a[0].set_attr("_name", a[1].attr("__name__"));
                        a[0].set_attr("__doc__", a[1].attr("__doc__"));
                        return none();
                    }) },
                { "__getattr__", method("_SpecialForm", "__getattr__", { py_positional("item") }, [](std::vector<py> const& a) -> py
                    {
                        std::string const item = a[1].str();
                        if (item == "__name__" || item == "__qualname__")
                            return a[0].attr("_name");
                        py_raise(builtin("AttributeError")(a[1]));
                    }) },
                { "__mro_entries__", method("_SpecialForm", "__mro_entries__", { py_positional("bases") }, [](std::vector<py> const& a) -> py
                    {
                        py_raise("TypeError", "Cannot subclass " + a[0].repr());
                    }) },
                { "__repr__", method("_SpecialForm", "__repr__", {}, [](std::vector<py> const& a) { return py{ "typing." + a[0].attr("_name").str() }; }) },
                { "__reduce__", method("_SpecialForm", "__reduce__", {}, [](std::vector<py> const& a) { return a[0].attr("_name"); }) },
                { "__call__", method("_SpecialForm", "__call__", {}, [](std::vector<py> const& a) -> py
                    {
                        py_raise("TypeError", "Cannot instantiate " + a[0].repr());
                    }, true, true) },
                { "__or__", union_method("_SpecialForm", "__or__", false) },
                { "__ror__", union_method("_SpecialForm", "__ror__", true) },
                { "__instancecheck__", method("_SpecialForm", "__instancecheck__", { py_positional("obj") }, [](std::vector<py> const& a) -> py
                    {
                        py_raise("TypeError", a[0].str() + " cannot be used with isinstance()");
                    }) },
                { "__subclasscheck__", method("_SpecialForm", "__subclasscheck__", { py_positional("cls") }, [](std::vector<py> const& a) -> py
                    {
                        py_raise("TypeError", a[0].str() + " cannot be used with issubclass()");
                    }) },
                { "__getitem__", method("_SpecialForm", "__getitem__", { py_positional("parameters") }, [](std::vector<py> const& a)
                    {
                        return tp_cache("_SpecialForm.__getitem__", { a[0], a[1] }, false, [&]() { return a[0].attr("_getitem")(a[0], a[1]); });
                    }) } },
                nullptr, {}, root);
            make_typing_class("_TypedCacheSpecialForm", { cls("_SpecialForm") }, {
                { "__getitem__", method("_TypedCacheSpecialForm", "__getitem__", { py_positional("parameters") }, [](std::vector<py> const& a)
                    {
                        std::vector<py> args{ a[0] };
                        for (auto const& p : tuple_of(a[1]).items())
                            args.push_back(p);
                        return a[0].attr("_getitem").call(args);
                    }) } },
                nullptr, {}, root);

            // (Any)
            make_typing_class("_AnyMeta", { builtin("type") }, {
                { "__instancecheck__", method("_AnyMeta", "__instancecheck__", { py_positional("obj") }, [](std::vector<py> const& a) -> py
                    {
                        if (a[0].is(obj("Any")))
                            py_raise("TypeError", "typing.Any cannot be used with isinstance()");
                        return boolean(issubclass(type_of(a[1]), a[0]));
                    }) },
                { "__repr__", method("_AnyMeta", "__repr__", {}, [](std::vector<py> const& a) -> py
                    {
                        if (a[0].is(obj("Any")))
                            return py{ "typing.Any" };
                        return py{ "<class '" + a[0].attr("__module__").str() + "." + a[0].attr("__qualname__").str() + "'>" };
                    }) } });
            make_typing_class("Any", {}, {
                { "__new__", staticmethod_of(function("__new__", "Any.__new__", [](std::vector<py> const& aArgs, object*) -> py
                    {
                        if (aArgs.empty())
                            py_raise("TypeError", "Any.__new__(): not enough arguments");
                        if (aArgs[0].is(obj("Any")))
                            py_raise("TypeError", "Any cannot be instantiated");
                        return builtin("object").attr("__new__")(aArgs[0]);
                    })) } },
                typing_docs::doc("Any"), cls("_AnyMeta"));

            // (ForwardRef)
            std::vector<py> forwardSlots;
            for (auto n : { "__forward_arg__", "__forward_code__", "__forward_evaluated__", "__forward_value__", "__forward_is_argument__", "__forward_is_class__",
                "__forward_module__" })
                forwardSlots.emplace_back(n);
            make_typing_class("ForwardRef", { cls("_Final") }, {
                { "__slots__", py_tuple(forwardSlots) },
                { "__init__", method("ForwardRef", "__init__", { py_positional("arg"), py_positional("is_argument", boolean(true)), py_positional("module", none()),
                    py_keyword("is_class", boolean(false)) }, [](std::vector<py> const& a) -> py
                    {
                        auto const& self = a[0];
                        py const& arg = a[1];
                        if (!is_str(arg))
                            py_raise("TypeError", "Forward reference must be a string -- got " + arg.repr());
                        py code;
                        try
                        {
                            code = builtin("compile")(py{ rewrite_star_unpack(arg.str()) }, py{ "<string>" }, py{ "eval" });
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "SyntaxError" }))
                                throw;
                            py_raise("SyntaxError", "Forward reference must be an expression -- got " + arg.repr());
                        }
                        self.set_attr("__forward_arg__", arg);
                        self.set_attr("__forward_code__", code);
                        self.set_attr("__forward_evaluated__", boolean(false));
                        self.set_attr("__forward_value__", none());
                        self.set_attr("__forward_is_argument__", a[2]);
                        self.set_attr("__forward_is_class__", a[4]);
                        self.set_attr("__forward_module__", a[3]);
                        return none();
                    }) },
                { "_evaluate", method("ForwardRef", "_evaluate", { py_positional("globalns"), py_positional("localns"), py_positional("type_params", state().sentinel),
                    py_keyword("recursive_guard") }, [](std::vector<py> const& a)
                    {
                        py typeParams = a[3];
                        if (typeParams.is(state().sentinel))
                        {
                            import_attribute("warnings", "warn").call({ py{ "Failing to pass a value to the 'type_params' parameter of 'typing.ForwardRef._evaluate' is "
                                "deprecated, as it leads to incorrect behaviour when calling typing.ForwardRef._evaluate on a stringified annotation that references a "
                                "PEP 695 type parameter. It will be disallowed in Python 3.15." } }, { { "category", builtin("DeprecationWarning") }, { "stacklevel", py{ 1 } } });
                            typeParams = py_tuple();
                        }
                        return forward_ref_evaluate(a[0], a[1], a[2], typeParams, a[4]);
                    }) },
                { "__eq__", method("ForwardRef", "__eq__", { py_positional("other") }, [](std::vector<py> const& a) -> py
                    {
                        auto const& self = a[0];
                        auto const& other = a[1];
                        if (!is_instance_of(other, "ForwardRef"))
                            return not_implemented_value();
                        if (self.attr("__forward_evaluated__").truthy() && other.attr("__forward_evaluated__").truthy())
                            return boolean(self.attr("__forward_arg__") == other.attr("__forward_arg__") && self.attr("__forward_value__") == other.attr("__forward_value__"));
                        return boolean(self.attr("__forward_arg__") == other.attr("__forward_arg__") && self.attr("__forward_module__") == other.attr("__forward_module__"));
                    }) },
                { "__hash__", method("ForwardRef", "__hash__", {}, [](std::vector<py> const& a)
                    {
                        return builtin("hash")(py_tuple({ a[0].attr("__forward_arg__"), a[0].attr("__forward_module__") }));
                    }) },
                { "__or__", union_method("ForwardRef", "__or__", false) },
                { "__ror__", union_method("ForwardRef", "__ror__", true) },
                { "__repr__", method("ForwardRef", "__repr__", {}, [](std::vector<py> const& a)
                    {
                        py const module = a[0].attr("__forward_module__");
                        std::string const moduleRepr = module.is_none() ? "" : ", module=" + module.repr();
                        return py{ "ForwardRef(" + a[0].attr("__forward_arg__").repr() + moduleRepr + ")" };
                    }) } },
                typing_docs::doc("ForwardRef"), {}, root);

            // (TypeVar)
            make_typing_class("TypeVar", {}, {
                { "__init__", method("TypeVar", "__init__", { py_positional("name"), py_keyword("bound", none()), py_keyword("covariant", boolean(false)),
                    py_keyword("contravariant", boolean(false)), py_keyword("infer_variance", boolean(false)), py_keyword("default", state().noDefault) },
                    [](std::vector<py> const& a) -> py
                    {
                        auto const& self = a[0];
                        check_name("typevar", a[1]);
                        py const constraints = a[7];
                        init_variance(self, a[3], a[4], a[5]);
                        py bound = a[2];
                        if (!bound.is_none())
                            bound = type_check(bound, "Bound must be a type.");
                        if (constraints.size() == 1)
                            py_raise("TypeError", "A single constraint is not allowed");
                        if (constraints.size() > 0 && !bound.is_none())
                            py_raise("TypeError", "Constraints cannot be combined with bound=...");
                        self.set_attr("__name__", a[1]);
                        self.set_attr("__bound__", bound);
                        self.set_attr("__constraints__", constraints);
                        self.set_attr("__default__", a[6]);
                        set_caller_module(self);
                        return none();
                    }, true) },
                { "__init_subclass__", final_init_subclass("TypeVar") },
                { "__repr__", method("TypeVar", "__repr__", {}, [](std::vector<py> const& a) { return variance_repr(a[0]); }) },
                { "__typing_subst__", method("TypeVar", "__typing_subst__", { py_positional("arg") }, [](std::vector<py> const& a) { return typevar_subst(a[0], a[1]); }) },
                { "__typing_prepare_subst__", method("TypeVar", "__typing_prepare_subst__", { py_positional("alias"), py_positional("args") }, [](std::vector<py> const& a) -> py
                    {
                        py const params = a[1].attr("__parameters__");
                        auto const i = params.method("index", { a[0] }).as_index();
                        auto const argsLen = a[2].size();
                        if (i < argsLen)
                            return a[2];
                        if (i == argsLen && !a[0].attr("__default__").is(state().noDefault))
                            return concat({ a[2], py_tuple({ a[0].attr("__default__") }) });
                        py_raise("TypeError", "Too few arguments for " + a[1].str() + "; actual " + std::to_string(argsLen) + ", expected at least " + std::to_string(i + 1));
                    }) },
                { "has_default", has_default_method("TypeVar") },
                { "__or__", union_method("TypeVar", "__or__", false) },
                { "__ror__", union_method("TypeVar", "__ror__", true) },
                { "__mro_entries__", mro_entries_error("TypeVar", "TypeVar") },
                { "__reduce__", method("TypeVar", "__reduce__", {}, [](std::vector<py> const& a) { return a[0].attr("__name__"); }) } },
                typing_docs::doc("TypeVar"));

            // (ParamSpecArgs, ParamSpecKwargs and ParamSpec)
            for (auto const& [name, suffix] : { std::pair{ "ParamSpecArgs", "args" }, std::pair{ "ParamSpecKwargs", "kwargs" } })
            {
                std::string const className = name;
                std::string const attribute = suffix;
                make_typing_class(name, {}, {
                    { "__init__", method(name, "__init__", { py_positional("origin") }, [](std::vector<py> const& a)
                        {
                            a[0].set_attr("__origin__", a[1]);
                            return none();
                        }) },
                    { "__init_subclass__", final_init_subclass(name) },
                    { "__repr__", method(name, "__repr__", {}, [attribute](std::vector<py> const& a)
                        {
                            return py{ a[0].attr("__origin__").attr("__name__").str() + "." + attribute };
                        }) },
                    { "__eq__", method(name, "__eq__", { py_positional("other") }, [className](std::vector<py> const& a) -> py
                        {
                            if (!is_instance_of(a[1], className.c_str()))
                                return not_implemented_value();
                            return boolean(a[0].attr("__origin__") == a[1].attr("__origin__"));
                        }) },
                    { "__mro_entries__", mro_entries_error(name, name) } },
                    typing_docs::doc(name));
            }
            make_typing_class("ParamSpec", {}, {
                { "__init__", method("ParamSpec", "__init__", { py_positional("name"), py_keyword("bound", none()), py_keyword("covariant", boolean(false)),
                    py_keyword("contravariant", boolean(false)), py_keyword("infer_variance", boolean(false)), py_keyword("default", state().noDefault) },
                    [](std::vector<py> const& a) -> py
                    {
                        auto const& self = a[0];
                        check_name("paramspec", a[1]);
                        init_variance(self, a[3], a[4], a[5]);
                        py bound = a[2];
                        if (!bound.is_none())
                            bound = type_check(bound, "Bound must be a type.");
                        self.set_attr("__name__", a[1]);
                        self.set_attr("__bound__", bound);
                        self.set_attr("__default__", a[6]);
                        set_caller_module(self);
                        return none();
                    }) },
                { "__init_subclass__", final_init_subclass("ParamSpec") },
                { "args", property_of("ParamSpec", "args", [](py const& self) { return cls("ParamSpecArgs")(self); }) },
                { "kwargs", property_of("ParamSpec", "kwargs", [](py const& self) { return cls("ParamSpecKwargs")(self); }) },
                { "__repr__", method("ParamSpec", "__repr__", {}, [](std::vector<py> const& a) { return variance_repr(a[0]); }) },
                { "__typing_subst__", method("ParamSpec", "__typing_subst__", { py_positional("arg") }, [](std::vector<py> const& a) { return paramspec_subst(a[0], a[1]); }) },
                { "__typing_prepare_subst__", method("ParamSpec", "__typing_prepare_subst__", { py_positional("alias"), py_positional("args") }, [](std::vector<py> const& a)
                    {
                        return paramspec_prepare_subst(a[0], a[1], a[2]);
                    }) },
                { "has_default", has_default_method("ParamSpec") },
                { "__or__", union_method("ParamSpec", "__or__", false) },
                { "__ror__", union_method("ParamSpec", "__ror__", true) },
                { "__mro_entries__", mro_entries_error("ParamSpec", "ParamSpec") },
                { "__reduce__", method("ParamSpec", "__reduce__", {}, [](std::vector<py> const& a) { return a[0].attr("__name__"); }) } },
                typing_docs::doc("ParamSpec"));

            // (TypeVarTuple)
            make_typing_class("TypeVarTuple", {}, {
                { "__init__", method("TypeVarTuple", "__init__", { py_positional("name"), py_keyword("default", state().noDefault) }, [](std::vector<py> const& a) -> py
                    {
                        check_name("typevartuple", a[1]);
                        a[0].set_attr("__name__", a[1]);
                        a[0].set_attr("__default__", a[2]);
                        set_caller_module(a[0]);
                        return none();
                    }) },
                { "__init_subclass__", final_init_subclass("TypeVarTuple") },
                { "__iter__", method("TypeVarTuple", "__iter__", {}, [](std::vector<py> const& a) { return builtin("iter")(py_list({ obj("Unpack")[a[0]] })); }) },
                { "__repr__", method("TypeVarTuple", "__repr__", {}, [](std::vector<py> const& a) { return a[0].attr("__name__"); }) },
                { "__typing_subst__", method("TypeVarTuple", "__typing_subst__", { py_positional("arg") }, [](std::vector<py> const&) -> py
                    {
                        py_raise("TypeError", "Substitution of bare TypeVarTuple is not supported");
                    }) },
                { "__typing_prepare_subst__", method("TypeVarTuple", "__typing_prepare_subst__", { py_positional("alias"), py_positional("args") },
                    [](std::vector<py> const& a) { return typevartuple_prepare_subst(a[0], a[1], a[2]); }) },
                { "has_default", has_default_method("TypeVarTuple") },
                { "__mro_entries__", mro_entries_error("TypeVarTuple", "TypeVarTuple") },
                { "__reduce__", method("TypeVarTuple", "__reduce__", {}, [](std::vector<py> const& a) { return a[0].attr("__name__"); }) } },
                typing_docs::doc("TypeVarTuple"));

            // (TypeAliasType: its value is made when it is first read (by _evaluator, of its type parameters) if it was not given)
            make_typing_class("TypeAliasType", {}, {
                { "__init__", method("TypeAliasType", "__init__", { py_positional("name"), py_positional("value"), py_keyword("type_params", py_tuple()) },
                    [](std::vector<py> const& a) -> py
                    {
                        check_name("typealias", a[1]);
                        if (!is_tuple(a[3]))
                            py_raise("TypeError", "type_params must be a tuple");
                        a[0].set_attr("__name__", a[1]);
                        a[0].set_attr("_value", a[2]);
                        a[0].set_attr("_evaluator", none());
                        a[0].set_attr("__type_params__", a[3]);
                        a[0].set_attr("__parameters__", collect_type_parameters(a[3], false));
                        set_caller_module(a[0]);
                        return none();
                    }) },
                { "__init_subclass__", final_init_subclass("TypeAliasType") },
                { "__value__", property_of("TypeAliasType", "__value__", [](py const& self)
                    {
                        py const evaluator = self.attr("_evaluator");
                        if (!evaluator.is_none())
                        {
                            self.set_attr("_value", evaluator.call(self.attr("__type_params__").items()));
                            self.set_attr("_evaluator", none());
                        }
                        return self.attr("_value");
                    }) },
                { "__getitem__", method("TypeAliasType", "__getitem__", { py_positional("parameters") }, [](std::vector<py> const& a) -> py
                    {
                        if (!a[0].attr("__type_params__").truthy())
                            py_raise("TypeError", "Only generic type aliases are subscriptable");
                        return types_attr("GenericAlias")(a[0], tuple_of(a[1]));
                    }) },
                { "__repr__", method("TypeAliasType", "__repr__", {}, [](std::vector<py> const& a) { return a[0].attr("__name__"); }) },
                { "__reduce__", method("TypeAliasType", "__reduce__", {}, [](std::vector<py> const& a) { return a[0].attr("__name__"); }) },
                // (its unions are types.UnionType's (CPython's _Py_union_type_or))
                { "__or__", method("TypeAliasType", "__or__", { py_positional("other") }, [](std::vector<py> const& a) -> py
                    {
                        if (!is_union_operand(a[1].get()))
                            return not_implemented_value();
                        return py{ make_union(a[0].get(), a[1].get()) };
                    }) },
                { "__ror__", method("TypeAliasType", "__ror__", { py_positional("other") }, [](std::vector<py> const& a) -> py
                    {
                        if (!is_union_operand(a[1].get()))
                            return not_implemented_value();
                        return py{ make_union(a[1].get(), a[0].get()) };
                    }) },
                { "__mro_entries__", mro_entries_error("TypeAliasType", "TypeAliasType") } },
                typing_docs::doc("TypeAliasType"));

            // (Generic)
            make_typing_class("Generic", {}, {
                { "__slots__", py_tuple() },
                { "_is_protocol", boolean(false) },
                { "__class_getitem__", classmethod_of(function("__class_getitem__", "Generic.__class_getitem__", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const a = py_bind("Generic.__class_getitem__", { py_positional_only("cls"), py_positional_only("params") }, aArgs, aKwargs);
                        return generic_class_getitem(a[0], a[1]);
                    })) },
                { "__init_subclass__", classmethod_of(function("__init_subclass__", "Generic.__init_subclass__", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        if (aArgs.empty())
                            py_raise("TypeError", "Generic.__init_subclass__(): not enough arguments");
                        std::vector<py> const rest{ aArgs.begin() + 1, aArgs.end() };
                        generic_init_subclass(aArgs[0], py_tuple(rest), aKwargs != nullptr ? py::borrowed(aKwargs) : py_dict());
                        return none();
                    })) } },
                typing_docs::doc("Generic"));
            define("Generic", cls("Generic"));
            define("_idfunc", module_function("_idfunc", { py_positional_only("x") }, [](std::vector<py> const& a) { return a[0]; }));

            // (_BaseGenericAlias)
            make_typing_class("_BaseGenericAlias", { cls("_Final") }, {
                { "__init__", method("_BaseGenericAlias", "__init__", { py_positional("origin"), py_keyword("inst", boolean(true)), py_keyword("name", none()) },
                    [](std::vector<py> const& a)
                    {
                        base_generic_alias_init(a[0], a[1], a[2].truthy(), a[3]);
                        return none();
                    }) },
                { "__call__", method("_BaseGenericAlias", "__call__", {}, [](std::vector<py> const& a) -> py
                    {
                        auto const& self = a[0];
                        if (!self.attr("_inst").truthy())
                            py_raise("TypeError", "Type " + self.attr("_name").str() + " cannot be instantiated; use " + self.attr("__origin__").attr("__name__").str() +
                                "() instead");
                        std::vector<object*> args;
                        for (auto const& v : a[1].items())
                            args.push_back(v.reference());
                        py const result{ invoke(self.attr("__origin__").get(), std::move(args), a[2].size() > 0 ? a[2].get() : nullptr) };
                        try
                        {
                            result.set_attr("__orig_class__", self);
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "Exception" }))
                                throw;
                        }
                        return result;
                    }, true, true) },
                { "__mro_entries__", method("_BaseGenericAlias", "__mro_entries__", { py_positional("bases") }, [](std::vector<py> const& a)
                    {
                        return base_generic_alias_mro_entries(a[0], a[1]);
                    }) },
                { "__getattr__", method("_BaseGenericAlias", "__getattr__", { py_positional("attr") }, [](std::vector<py> const& a)
                    {
                        return base_generic_alias_getattr(a[0], a[1]);
                    }) },
                { "__setattr__", method("_BaseGenericAlias", "__setattr__", { py_positional("attr"), py_positional("val") }, [](std::vector<py> const& a)
                    {
                        std::string const attribute = a[1].str();
                        if (is_dunder(attribute) || attribute == "_name" || attribute == "_inst" || attribute == "_nparams" || attribute == "_defaults")
                            builtin("object").attr("__setattr__")(a[0], a[1], a[2]);
                        else
                            builtin("setattr")(a[0].attr("__origin__"), a[1], a[2]);
                        return none();
                    }) },
                { "__instancecheck__", method("_BaseGenericAlias", "__instancecheck__", { py_positional("obj") }, [](std::vector<py> const& a)
                    {
                        return a[0].method("__subclasscheck__", { type_of(a[1]) });
                    }) },
                { "__subclasscheck__", method("_BaseGenericAlias", "__subclasscheck__", { py_positional("cls") }, [](std::vector<py> const&) -> py
                    {
                        py_raise("TypeError", "Subscripted generics cannot be used with class and instance checks");
                    }) },
                { "__dir__", method("_BaseGenericAlias", "__dir__", {}, [](std::vector<py> const& a)
                    {
                        py const names = builtin("set")(builtin("object").attr("__dir__")(a[0]));
                        for (auto const& n : builtin("dir")(a[0].attr("__origin__")).items())
                            if (!is_dunder(n.str()))
                                names.method("add", { n });
                        return builtin("list")(names);
                    }) } },
                typing_docs::doc("_BaseGenericAlias"), {}, root);

            // (_GenericAlias)
            make_typing_class("_GenericAlias", { cls("_BaseGenericAlias") }, {
                { "__init__", method("_GenericAlias", "__init__", { py_positional("origin"), py_positional("args"), py_keyword("inst", boolean(true)),
                    py_keyword("name", none()) }, [](std::vector<py> const& a)
                    {
                        generic_alias_init(a[0], a[1], a[2], a[3].truthy(), a[4]);
                        return none();
                    }) },
                { "__eq__", method("_GenericAlias", "__eq__", { py_positional("other") }, [](std::vector<py> const& a) -> py
                    {
                        if (!is_generic_alias(a[1]))
                            return not_implemented_value();
                        return boolean(a[0].attr("__origin__") == a[1].attr("__origin__") && a[0].attr("__args__") == a[1].attr("__args__"));
                    }) },
                { "__hash__", method("_GenericAlias", "__hash__", {}, [](std::vector<py> const& a)
                    {
                        return builtin("hash")(py_tuple({ a[0].attr("__origin__"), a[0].attr("__args__") }));
                    }) },
                { "__or__", union_method("_GenericAlias", "__or__", false) },
                { "__ror__", union_method("_GenericAlias", "__ror__", true) },
                { "__getitem__", method("_GenericAlias", "__getitem__", { py_positional("args") }, [](std::vector<py> const& a) { return generic_alias_getitem(a[0], a[1]); }) },
                { "_determine_new_args", method("_GenericAlias", "_determine_new_args", { py_positional("args") }, [](std::vector<py> const& a)
                    {
                        return generic_alias_determine_new_args(a[0], a[1]);
                    }) },
                { "_make_substitution", method("_GenericAlias", "_make_substitution", { py_positional("args"), py_positional("new_arg_by_param") }, [](std::vector<py> const& a)
                    {
                        return generic_alias_make_substitution(a[0], a[1], a[2]);
                    }) },
                { "copy_with", method("_GenericAlias", "copy_with", { py_positional("args") }, [](std::vector<py> const& a)
                    {
                        return a[0].attr("__class__").call({ a[0].attr("__origin__"), a[1] }, { { "name", a[0].attr("_name") }, { "inst", a[0].attr("_inst") } });
                    }) },
                { "__repr__", method("_GenericAlias", "__repr__", {}, [](std::vector<py> const& a) { return generic_alias_repr(a[0]); }) },
                { "__reduce__", method("_GenericAlias", "__reduce__", {}, [](std::vector<py> const& a) { return generic_alias_reduce(a[0]); }) },
                { "__mro_entries__", method("_GenericAlias", "__mro_entries__", { py_positional("bases") }, [](std::vector<py> const& a)
                    {
                        return generic_alias_mro_entries(a[0], a[1]);
                    }) },
                { "__iter__", method("_GenericAlias", "__iter__", {}, [](std::vector<py> const& a) { return builtin("iter")(py_list({ obj("Unpack")[a[0]] })); }) } },
                typing_docs::doc("_GenericAlias"), {}, root);

            // (_SpecialGenericAlias and the aliases that derive from it)
            make_typing_class("_SpecialGenericAlias", { cls("_NotIterable"), cls("_BaseGenericAlias") }, {
                { "__init__", method("_SpecialGenericAlias", "__init__", { py_positional("origin"), py_positional("nparams"), py_keyword("inst", boolean(true)),
                    py_keyword("name", none()), py_keyword("defaults", py_tuple()) }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        auto const& origin = a[1];
                        py name = a[4];
                        if (name.is_none())
                            name = origin.attr("__name__");
                        base_generic_alias_init(self, origin, a[3].truthy(), name);
                        self.set_attr("_nparams", a[2]);
                        self.set_attr("_defaults", a[5]);
                        if (origin.attr("__module__") == py{ "builtins" })
                            self.set_attr("__doc__", py{ "Deprecated alias to " + origin.attr("__qualname__").str() + "." });
                        else
                            self.set_attr("__doc__", py{ "Deprecated alias to " + origin.attr("__module__").str() + "." + origin.attr("__qualname__").str() + "." });
                        return none();
                    }) },
                { "__getitem__", method("_SpecialGenericAlias", "__getitem__", { py_positional("params") }, [](std::vector<py> const& a)
                    {
                        return special_generic_alias_getitem(a[0], a[1]);
                    }) },
                { "copy_with", method("_SpecialGenericAlias", "copy_with", { py_positional("params") }, [](std::vector<py> const& a)
                    {
                        return cls("_GenericAlias").call({ a[0].attr("__origin__"), a[1] }, { { "name", a[0].attr("_name") }, { "inst", a[0].attr("_inst") } });
                    }) },
                { "__repr__", method("_SpecialGenericAlias", "__repr__", {}, [](std::vector<py> const& a) { return py{ "typing." + a[0].attr("_name").str() }; }) },
                { "__subclasscheck__", method("_SpecialGenericAlias", "__subclasscheck__", { py_positional("cls") }, [](std::vector<py> const& a)
                    {
                        return special_generic_alias_subclasscheck(a[0], a[1]);
                    }) },
                { "__reduce__", method("_SpecialGenericAlias", "__reduce__", {}, [](std::vector<py> const& a) { return a[0].attr("_name"); }) },
                { "__or__", union_method("_SpecialGenericAlias", "__or__", false) },
                { "__ror__", union_method("_SpecialGenericAlias", "__ror__", true) } },
                nullptr, {}, root);
            make_typing_class("_DeprecatedGenericAlias", { cls("_SpecialGenericAlias") }, {
                { "__init__", method("_DeprecatedGenericAlias", "__init__", { py_positional("origin"), py_positional("nparams"), py_keyword("removal_version"),
                    py_keyword("inst", boolean(true)), py_keyword("name", none()) }, [](std::vector<py> const& a)
                    {
                        cls("_SpecialGenericAlias").attr("__init__").call({ a[0], a[1], a[2] }, { { "inst", a[4] }, { "name", a[5] } });
                        a[0].set_attr("_removal_version", a[3]);
                        return none();
                    }) },
                { "__instancecheck__", method("_DeprecatedGenericAlias", "__instancecheck__", { py_positional("inst") }, [](std::vector<py> const& a)
                    {
                        py const deprecated = attr_or(py{ fn_import("warnings") }, "_deprecated", py{});
                        if (!deprecated.empty())
                            deprecated.call({ py{ a[0].attr("__module__").str() + "." + a[0].attr("_name").str() } }, { { "remove", a[0].attr("_removal_version") } });
                        return a[0].method("__subclasscheck__", { type_of(a[1]) });
                    }) } },
                nullptr, {}, root);
            make_typing_class("_CallableGenericAlias", { cls("_NotIterable"), cls("_GenericAlias") }, {
                { "__repr__", method("_CallableGenericAlias", "__repr__", {}, [](std::vector<py> const& a)
                    {
                        py const args = a[0].attr("__args__");
                        if (args.size() == 2 && is_param_expr(args[py{ 0 }]))
                            return generic_alias_repr(a[0]);
                        std::string text;
                        for (auto const& t : slice(args, std::nullopt, -1).items())
                            text += (text.empty() ? "" : ", ") + type_repr(t);
                        return py{ "typing.Callable[[" + text + "], " + type_repr(args[py{ -1 }]) + "]" };
                    }) },
                { "__reduce__", method("_CallableGenericAlias", "__reduce__", {}, [](std::vector<py> const& a)
                    {
                        py args = a[0].attr("__args__");
                        if (!(args.size() == 2 && is_param_expr(args[py{ 0 }])))
                            args = py_tuple({ builtin("list")(slice(args, std::nullopt, -1)), args[py{ -1 }] });
                        return py_tuple({ import_attribute("operator", "getitem"), py_tuple({ obj("Callable"), args }) });
                    }) } },
                nullptr, {}, root);
            make_typing_class("_CallableType", { cls("_SpecialGenericAlias") }, {
                { "copy_with", method("_CallableType", "copy_with", { py_positional("params") }, [](std::vector<py> const& a)
                    {
                        return cls("_CallableGenericAlias").call({ a[0].attr("__origin__"), a[1] }, { { "name", a[0].attr("_name") }, { "inst", a[0].attr("_inst") } });
                    }) },
                { "__getitem__", method("_CallableType", "__getitem__", { py_positional("params") }, [](std::vector<py> const& a)
                    {
                        py const params = a[1];
                        if (!is_tuple(params) || params.size() != 2)
                            py_raise("TypeError", "Callable must be used as Callable[[arg, ...], result].");
                        py const args = params[py{ 0 }];
                        py const result = params[py{ 1 }];
                        py const p = isinstance(args, builtin("list")) ? py_tuple({ builtin("tuple")(args), result }) : py_tuple({ args, result });
                        return a[0].method("__getitem_inner__", { p });
                    }) },
                { "__getitem_inner__", method("_CallableType", "__getitem_inner__", { py_positional("params") }, [](std::vector<py> const& a)
                    {
                        return tp_cache("_CallableType.__getitem_inner__", { a[0], a[1] }, false, [&]() -> py
                        {
                            py args = a[1][py{ 0 }];
                            py const result = type_check(a[1][py{ 1 }], "Callable[args, result]: result must be a type.");
                            if (args.is(ellipsis()))
                                return a[0].method("copy_with", { py_tuple({ cls("_TypingEllipsis"), result }) });
                            std::vector<py> converted;
                            for (auto const& arg : tuple_of(args).items())
                                converted.push_back(type_convert(arg));
                            converted.push_back(result);
                            return a[0].method("copy_with", { py_tuple(converted) });
                        });
                    }) } },
                nullptr, {}, root);
            make_typing_class("_TupleType", { cls("_SpecialGenericAlias") }, {
                { "__getitem__", method("_TupleType", "__getitem__", { py_positional("params") }, [](std::vector<py> const& a)
                    {
                        return tp_cache("_TupleType.__getitem__", { a[0], a[1] }, false, [&]() -> py
                        {
                            py const params = tuple_of(a[1]);
                            std::vector<py> checked;
                            if (params.size() >= 2 && params[py{ -1 }].is(ellipsis()))
                            {
                                for (auto const& p : slice(params, std::nullopt, -1).items())
                                    checked.push_back(type_check(p, "Tuple[t, ...]: t must be a type."));
                                checked.push_back(cls("_TypingEllipsis"));
                                return a[0].method("copy_with", { py_tuple(checked) });
                            }
                            for (auto const& p : params.items())
                                checked.push_back(type_check(p, "Tuple[t0, t1, ...]: each t must be a type."));
                            return a[0].method("copy_with", { py_tuple(checked) });
                        });
                    }) } },
                nullptr, {}, root);
            make_typing_class("_UnionGenericAlias", { cls("_NotIterable"), cls("_GenericAlias") }, {
                { "copy_with", method("_UnionGenericAlias", "copy_with", { py_positional("params") }, [](std::vector<py> const& a) { return obj("Union")[a[1]]; }) },
                { "__eq__", method("_UnionGenericAlias", "__eq__", { py_positional("other") }, [](std::vector<py> const& a) -> py
                    {
                        if (!is_instance_of(a[1], "_UnionGenericAlias") && !is_union_type(a[1]))
                            return not_implemented_value();
                        try
                        {
                            return boolean(builtin("set")(a[0].attr("__args__")) == builtin("set")(a[1].attr("__args__")));
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "TypeError" }))
                                throw;
                            return boolean(compare_args_orderless(a[0].attr("__args__"), a[1].attr("__args__")));
                        }
                    }) },
                { "__hash__", method("_UnionGenericAlias", "__hash__", {}, [](std::vector<py> const& a)
                    {
                        return builtin("hash")(builtin("frozenset")(a[0].attr("__args__")));
                    }) },
                { "__repr__", method("_UnionGenericAlias", "__repr__", {}, [](std::vector<py> const& a)
                    {
                        py const args = a[0].attr("__args__");
                        if (args.size() == 2)
                        {
                            if (args[py{ 0 }].is(none_type()))
                                return py{ "typing.Optional[" + type_repr(args[py{ 1 }]) + "]" };
                            if (args[py{ 1 }].is(none_type()))
                                return py{ "typing.Optional[" + type_repr(args[py{ 0 }]) + "]" };
                        }
                        return generic_alias_repr(a[0]);
                    }) },
                { "__instancecheck__", method("_UnionGenericAlias", "__instancecheck__", { py_positional("obj") }, [](std::vector<py> const& a)
                    {
                        for (auto const& arg : a[0].attr("__args__").items())
                            if (isinstance(a[1], arg))
                                return boolean(true);
                        return boolean(false);
                    }) },
                { "__subclasscheck__", method("_UnionGenericAlias", "__subclasscheck__", { py_positional("cls") }, [](std::vector<py> const& a)
                    {
                        for (auto const& arg : a[0].attr("__args__").items())
                            if (issubclass(a[1], arg))
                                return boolean(true);
                        return boolean(false);
                    }) },
                { "__reduce__", method("_UnionGenericAlias", "__reduce__", {}, [](std::vector<py> const& a)
                    {
                        py const reduced = generic_alias_reduce(a[0]);
                        return py_tuple({ reduced[py{ 0 }], py_tuple({ obj("Union"), reduced[py{ 1 }][py{ 1 }] }) });
                    }) } },
                nullptr, {}, root);
            auto const value_and_type = [](py const& aArgs)
            {
                std::vector<py> pairs;
                for (auto const& p : aArgs.items())
                    pairs.push_back(py_tuple({ p, type_of(p) }));
                return py_list(pairs);
            };
            make_typing_class("_LiteralGenericAlias", { cls("_GenericAlias") }, {
                { "__eq__", method("_LiteralGenericAlias", "__eq__", { py_positional("other") }, [value_and_type](std::vector<py> const& a) -> py
                    {
                        if (!is_instance_of(a[1], "_LiteralGenericAlias"))
                            return not_implemented_value();
                        return boolean(builtin("set")(value_and_type(a[0].attr("__args__"))) == builtin("set")(value_and_type(a[1].attr("__args__"))));
                    }) },
                { "__hash__", method("_LiteralGenericAlias", "__hash__", {}, [value_and_type](std::vector<py> const& a)
                    {
                        return builtin("hash")(builtin("frozenset")(value_and_type(a[0].attr("__args__"))));
                    }) } },
                nullptr, {}, root);
            make_typing_class("_ConcatenateGenericAlias", { cls("_GenericAlias") }, {
                { "copy_with", method("_ConcatenateGenericAlias", "copy_with", { py_positional("params") }, [](std::vector<py> const& a) -> py
                    {
                        py params = a[1];
                        py const last = params[py{ -1 }];
                        if (isinstance(last, builtin("list")) || is_tuple(last))
                            return concat({ slice(params, std::nullopt, -1), builtin("tuple")(last) });
                        if (is_instance_of(last, "_ConcatenateGenericAlias"))
                            params = concat({ slice(params, std::nullopt, -1), last.attr("__args__") });
                        return a[0].attr("__class__").call({ a[0].attr("__origin__"), params }, { { "name", a[0].attr("_name") }, { "inst", a[0].attr("_inst") } });
                    }) } },
                nullptr, {}, root);
            make_typing_class("_UnpackGenericAlias", { cls("_GenericAlias") }, {
                { "__repr__", method("_UnpackGenericAlias", "__repr__", {}, [](std::vector<py> const& a)
                    {
                        return py{ "typing.Unpack[" + type_repr(a[0].attr("__args__")[py{ 0 }]) + "]" };
                    }) },
                { "__getitem__", method("_UnpackGenericAlias", "__getitem__", { py_positional("args") }, [](std::vector<py> const& a)
                    {
                        if (a[0].attr("__typing_is_unpacked_typevartuple__").truthy())
                            return a[1];
                        return generic_alias_getitem(a[0], a[1]);
                    }) },
                { "__typing_unpacked_tuple_args__", property_of("_UnpackGenericAlias", "__typing_unpacked_tuple_args__", [](py const& self) -> py
                    {
                        py const arg = self.attr("__args__")[py{ 0 }];
                        if (is_generic_alias(arg) || is_types_generic_alias(arg))
                        {
                            if (!arg.attr("__origin__").is(builtin("tuple")))
                                py_raise("TypeError", "Unpack[...] must be used with a tuple type");
                            return arg.attr("__args__");
                        }
                        return none();
                    }) },
                { "__typing_is_unpacked_typevartuple__", property_of("_UnpackGenericAlias", "__typing_is_unpacked_typevartuple__", [](py const& self)
                    {
                        return boolean(is_instance_of(self.attr("__args__")[py{ 0 }], "TypeVarTuple"));
                    }) } },
                nullptr, {}, root);
            make_typing_class("_TypingEllipsis", {}, {}, "Internal placeholder for ... (ellipsis).");
        }

        // --- the special forms' functions

        py single_type_alias(py const& self, py const& aParameters, std::string const& aMessage, bool aAllowSpecialForms = false)
        {
            py const item = type_check(aParameters, aMessage, true, none(), aAllowSpecialForms);
            return cls("_GenericAlias")(self, py_tuple({ item }));
        }

        py union_getitem(py const& self, py aParameters)
        {
            if (aParameters == py_tuple())
                py_raise("TypeError", "Cannot take a Union of no types.");
            aParameters = tuple_of(aParameters);
            std::vector<py> checked;
            for (auto const& p : aParameters.items())
                checked.push_back(type_check(p, "Union[arg, ...]: each arg must be a type."));
            py const parameters = remove_dups_flatten(py_tuple(checked));
            if (parameters.size() == 1)
                return parameters[py{ 0 }];
            if (parameters.size() == 2 && parameters.contains(none_type()))
                return cls("_UnionGenericAlias").call({ self, parameters }, { { "name", py{ "Optional" } } });
            return cls("_UnionGenericAlias")(self, parameters);
        }

        void make_special_forms()
        {
            for (auto name : { "NoReturn", "Never", "Self", "LiteralString", "TypeAlias" })
                make_special_form(name, typing_docs::doc(name), [](py const& self, py const&) -> py { not_subscriptable(self); });
            make_special_form("ClassVar", typing_docs::doc("ClassVar"), [](py const& self, py const& aParameters)
            {
                return single_type_alias(self, aParameters, self.str() + " accepts only single type.", true);
            });
            make_special_form("Final", typing_docs::doc("Final"), [](py const& self, py const& aParameters)
            {
                return single_type_alias(self, aParameters, self.str() + " accepts only single type.", true);
            });
            make_special_form("Union", typing_docs::doc("Union"), union_getitem);
            make_special_form("Optional", typing_docs::doc("Optional"), [](py const& self, py const& aParameters)
            {
                py const arg = type_check(aParameters, self.str() + " requires a single type.");
                return obj("Union")[py_tuple({ arg, none_type() })];
            });
            make_special_form("Literal", typing_docs::doc("Literal"), [](py const& self, py const& aParameters)
            {
                std::vector<py> args{ self };
                for (auto const& p : aParameters.items())
                    args.push_back(p);
                return tp_cache("Literal", args, true, [&]() -> py
                {
                    py const parameters = flatten_literal_params(aParameters);
                    std::vector<py> pairs;
                    for (auto const& p : parameters.items())
                        pairs.push_back(py_tuple({ p, type_of(p) }));
                    std::vector<py> deduplicated;
                    for (auto const& pair : deduplicate(pairs, true))
                        deduplicated.push_back(pair[py{ 0 }]);
                    return cls("_LiteralGenericAlias")(self, py_tuple(deduplicated));
                });
            }, "_TypedCacheSpecialForm");
            make_special_form("Concatenate", typing_docs::doc("Concatenate"), [](py const& self, py aParameters)
            {
                if (aParameters == py_tuple())
                    py_raise("TypeError", "Cannot take a Concatenate of no types.");
                aParameters = tuple_of(aParameters);
                py const last = aParameters[py{ -1 }];
                if (!(last.is(ellipsis()) || is_instance_of(last, "ParamSpec")))
                    py_raise("TypeError", "The last parameter to Concatenate should be a ParamSpec variable or ellipsis.");
                std::vector<py> parameters;
                for (auto const& p : slice(aParameters, std::nullopt, -1).items())
                    parameters.push_back(type_check(p, "Concatenate[arg, ...]: each arg must be a type."));
                parameters.push_back(last);
                return cls("_ConcatenateGenericAlias")(self, py_tuple(parameters));
            });
            for (auto name : { "TypeGuard", "TypeIs" })
                make_special_form(name, typing_docs::doc(name), [](py const& self, py const& aParameters)
                {
                    return single_type_alias(self, aParameters, self.str() + " accepts only single type.");
                });
            make_special_form("Unpack", typing_docs::doc("Unpack"), [](py const& self, py const& aParameters)
            {
                py const item = type_check(aParameters, self.str() + " accepts only single type.");
                return cls("_UnpackGenericAlias").call({}, { { "origin", self }, { "args", py_tuple({ item }) } });
            });
            for (auto name : { "Required", "NotRequired", "ReadOnly" })
                make_special_form(name, typing_docs::doc(name), [](py const& self, py const& aParameters)
                {
                    return single_type_alias(self, aParameters, self.attr("_name").str() + " accepts only a single type.");
                });
            make_special_form("Annotated", typing_docs::doc("Annotated"), [](py const& self, py const& aParameters)
            {
                std::vector<py> args{ self };
                for (auto const& p : aParameters.items())
                    args.push_back(p);
                return tp_cache("Annotated", args, true, [&]() -> py
                {
                    if (aParameters.size() < 2)
                        py_raise("TypeError", "Annotated[...] should be used with at least two arguments (a type and an annotation).");
                    if (is_unpacked_typevartuple(aParameters[py{ 0 }]))
                        py_raise("TypeError", "Annotated[...] should not be used with an unpacked TypeVarTuple");
                    py const origin = type_check(aParameters[py{ 0 }], "Annotated[t, ...]: t must be a type.", true, none(), true);
                    py const metadata = slice(aParameters, 1, std::nullopt);
                    return cls("_AnnotatedAlias")(origin, metadata);
                });
            }, "_TypedCacheSpecialForm");
        }

        // --- Protocol

        std::set<std::string> const sTypingInternals{ "__parameters__", "__orig_bases__", "__orig_class__", "_is_protocol", "_is_runtime_protocol",
            "__protocol_attrs__", "__non_callable_proto_members__", "__type_params__" };
        std::set<std::string> const sSpecialNames{ "__abstractmethods__", "__annotations__", "__dict__", "__doc__", "__init__", "__module__", "__new__", "__slots__",
            "__subclasshook__", "__weakref__", "__class_getitem__", "__match_args__", "__static_attributes__", "__firstlineno__" };

        bool excluded_attribute(std::string const& aName)
        {
            return sTypingInternals.contains(aName) || sSpecialNames.contains(aName) || aName == "_MutableMapping__marker";
        }

        py get_protocol_attrs(py const& aClass)
        {
            py const attrs = builtin("set")();
            auto const mro = aClass.attr("__mro__").items();
            for (std::size_t i = 0u; i + 1u < mro.size(); ++i)
            {
                auto const& base = mro[i];
                std::string const name = base.attr("__name__").str();
                if (name == "Protocol" || name == "Generic")
                    continue;
                py const annotations = attr_or(base, "__annotations__", py_dict());
                std::vector<py> names = builtin("list")(base.attr("__dict__")).items();
                for (auto const& n : builtin("list")(annotations).items())
                    names.push_back(n);
                for (auto const& attr : names)
                {
                    std::string const a = attr.str();
                    if (!a.starts_with("_abc_") && !excluded_attribute(a))
                        attrs.method("add", { attr });
                }
            }
            return attrs;
        }

        // (type.__subclasscheck__(cls, other) and type.__instancecheck__(cls, instance): the class's MRO (not the metaclass's hooks))
        bool plain_subclass(py const& aOther, py const& aClass)
        {
            if (aOther.is(aClass))
                return true;
            if (is_class(aOther.get()) && is_class(aClass.get()))
                return is_subclass(aOther.get(), aClass.get());
            return false;
        }

        py& no_init_or_replace_init()
        {
            static py* sFunction = new py{};
            return *sFunction;
        }
        py& proto_hook()
        {
            static py* sHook = new py{};
            return *sHook;
        }

        bool allow_reckless_class_checks()
        {
            py const module = caller(none());
            return module.is_none() || module == py{ "abc" } || module == py{ "functools" };
        }

        void type_check_issubclass_arg_1(py const& aArg)
        {
            if (!is_type(aArg))
                py_raise("TypeError", "issubclass() arg 1 must be a class");
        }

        py proto_allowlist()
        {
            static py* sAllowlist = nullptr;
            if (sAllowlist == nullptr)
            {
                py const d = py_dict();
                std::vector<py> abc;
                for (auto n : { "Callable", "Awaitable", "Iterable", "Iterator", "AsyncIterable", "AsyncIterator", "Hashable", "Sized", "Container", "Collection",
                    "Reversible", "Buffer" })
                    abc.emplace_back(n);
                d.set_item(py{ "collections.abc" }, py_list(abc));
                d.set_item(py{ "contextlib" }, py_list({ py{ "AbstractContextManager" }, py{ "AbstractAsyncContextManager" } }));
                sAllowlist = new py{ d };
            }
            return *sAllowlist;
        }

        py runtime_checkable(py const& aClass)
        {
            if (!issubclass(aClass, obj("Generic")) || !attr_or(aClass, "_is_protocol", boolean(false)).truthy())
                py_raise("TypeError", "@runtime_checkable can be only applied to protocol classes, got " + aClass.repr());
            aClass.set_attr("_is_runtime_protocol", boolean(true));
            py const nonCallable = builtin("set")();
            aClass.set_attr("__non_callable_proto_members__", nonCallable);
            for (auto const& attr : aClass.attr("__protocol_attrs__").items())
            {
                bool callable = false;
                try
                {
                    callable = is_callable(builtin("getattr")(aClass, attr, none()).get());
                }
                catch (raised const&)
                {
                    if (!caught({ "Exception" }))
                        throw;
                    py const cause = py_take_exception();
                    raise_from(builtin("TypeError")(py{ "Failed to determine whether protocol member " + attr.repr() + " is a method member" }), cause);
                }
                if (!callable)
                    nonCallable.method("add", { attr });
            }
            return aClass;
        }

        void make_protocol_classes()
        {
            std::vector<std::pair<char const*, py>> const root{ { "_root", boolean(true) } };
            no_init_or_replace_init() = function("_no_init_or_replace_init", "_no_init_or_replace_init", [](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                if (aArgs.empty())
                    py_raise("TypeError", "_no_init_or_replace_init() missing 1 required positional argument: 'self'");
                auto const& self = aArgs[0];
                py const klass = type_of(self);
                if (klass.attr("_is_protocol").truthy())
                    py_raise("TypeError", "Protocols cannot be instantiated");
                if (!klass.attr("__init__").is(no_init_or_replace_init()))
                    return none();
                bool found = false;
                for (auto const& base : klass.attr("__mro__").items())
                {
                    py const init = base.attr("__dict__").method("get", { py{ "__init__" }, no_init_or_replace_init() });
                    if (!init.is(no_init_or_replace_init()))
                    {
                        klass.set_attr("__init__", init);
                        found = true;
                        break;
                    }
                }
                if (!found)
                    klass.set_attr("__init__", builtin("object").attr("__init__"));
                std::vector<object*> args;
                for (auto const& a : aArgs)
                    args.push_back(a.reference());
                py{ invoke(klass.attr("__init__").get(), std::move(args), aKwargs) };
                return none();
            });
            define("_no_init_or_replace_init", no_init_or_replace_init());
            py const abcMeta = import_attribute("abc", "ABCMeta");
            make_typing_class("_ProtocolMeta", { abcMeta }, {
                { "__new__", staticmethod_of(function("__new__", "_ProtocolMeta.__new__", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const a = py_bind("_ProtocolMeta.__new__", { py_positional_only("mcls"), py_positional_only("name"), py_positional_only("bases"),
                            py_positional_only("namespace") }, aArgs, aKwargs, nullptr, nullptr);
                        (void)a;
                        return none();
                    })) },
                { "__init__", method("_ProtocolMeta", "__init__", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        std::vector<object*> args;
                        for (auto const& v : a[1].items())
                            args.push_back(v.reference());
                        py{ invoke(builtin("super")(cls("_ProtocolMeta"), self).attr("__init__").get(), std::move(args), a[2].size() > 0 ? a[2].get() : nullptr) };
                        if (attr_or(self, "_is_protocol", boolean(false)).truthy())
                            self.set_attr("__protocol_attrs__", get_protocol_attrs(self));
                        return none();
                    }, true, true) },
                { "__subclasscheck__", method("_ProtocolMeta", "__subclasscheck__", { py_positional("other") }, [](std::vector<py> const& a) -> py
                    {
                        auto const& self = a[0];
                        auto const& other = a[1];
                        if (self.is(obj("Protocol")))
                            return boolean(plain_subclass(other, self));
                        if (attr_or(self, "_is_protocol", boolean(false)).truthy() && !allow_reckless_class_checks())
                        {
                            if (!attr_or(self, "_is_runtime_protocol", boolean(false)).truthy())
                            {
                                type_check_issubclass_arg_1(other);
                                py_raise("TypeError", "Instance and class checks can only be used with @runtime_checkable protocols");
                            }
                            py const nonCallable = self.attr("__non_callable_proto_members__");
                            if (nonCallable.truthy() && self.attr("__dict__").method("get", { py{ "__subclasshook__" } }).is(proto_hook()))
                            {
                                type_check_issubclass_arg_1(other);
                                std::string const text = builtin("str")(builtin("sorted")(nonCallable)).str();
                                py_raise("TypeError", "Protocols with non-method members don't support issubclass(). Non-method members: " + text.substr(1u, text.size() - 2u) + ".");
                            }
                        }
                        return import_attribute("abc", "ABCMeta").attr("__subclasscheck__")(self, other);
                    }) },
                { "__instancecheck__", method("_ProtocolMeta", "__instancecheck__", { py_positional("instance") }, [](std::vector<py> const& a) -> py
                    {
                        auto const& self = a[0];
                        auto const& instance = a[1];
                        if (self.is(obj("Protocol")))
                            return boolean(plain_subclass(type_of(instance), self));
                        if (!attr_or(self, "_is_protocol", boolean(false)).truthy())
                            return import_attribute("abc", "ABCMeta").attr("__instancecheck__")(self, instance);
                        if (!attr_or(self, "_is_runtime_protocol", boolean(false)).truthy() && !allow_reckless_class_checks())
                            py_raise("TypeError", "Instance and class checks can only be used with @runtime_checkable protocols");
                        if (import_attribute("abc", "ABCMeta").attr("__instancecheck__")(self, instance).truthy())
                            return boolean(true);
                        py const getattrStatic = import_attribute("inspect", "getattr_static");
                        for (auto const& attr : self.attr("__protocol_attrs__").items())
                        {
                            py value;
                            try
                            {
                                value = getattrStatic(instance, attr);
                            }
                            catch (raised const&)
                            {
                                if (!caught({ "AttributeError" }))
                                    throw;
                                return boolean(false);
                            }
                            if (value.is_none() && !self.attr("__non_callable_proto_members__").contains(attr))
                                return boolean(false);
                        }
                        return boolean(true);
                    }) } });
            // (_ProtocolMeta.__new__: its checks, then ABCMeta's)
            cls("_ProtocolMeta").set_attr("__new__", staticmethod_of(function("__new__", "_ProtocolMeta.__new__", [](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                auto const a = py_bind("_ProtocolMeta.__new__", { py_positional_only("mcls"), py_positional_only("name"), py_positional_only("bases"),
                    py_positional_only("namespace") }, aArgs, aKwargs, nullptr, nullptr);
                py const& bases = a[2];
                auto const protocol = state().classes.find("Protocol");
                if (a[1] == py{ "Protocol" } && bases == py_tuple({ obj("Generic") }))
                    ;
                else if (protocol != state().classes.end() && bases.contains(protocol->second))
                {
                    for (auto const& base : bases.items())
                    {
                        bool allowed = base.is(builtin("object")) || base.is(obj("Generic"));
                        if (!allowed)
                        {
                            py const names = proto_allowlist().method("get", { attr_or(base, "__module__", none()), py_list() });
                            allowed = names.contains(base.attr("__name__")) || (issubclass(base, obj("Generic")) && attr_or(base, "_is_protocol", boolean(false)).truthy());
                        }
                        if (!allowed)
                            py_raise("TypeError", "Protocols can only inherit from other protocols, got " + base.repr());
                    }
                }
                py const super = builtin("super")(cls("_ProtocolMeta"), a[0]);
                return py{ invoke(super.attr("__new__").get(), { a[0].reference(), a[1].reference(), a[2].reference(), a[3].reference() }, aKwargs) };
            })));
            proto_hook() = classmethod_of(function("_proto_hook", "_proto_hook", [](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                auto const a = py_bind("_proto_hook", { py_positional("cls"), py_positional("other") }, aArgs, aKwargs);
                auto const& klass = a[0];
                auto const& other = a[1];
                if (!klass.attr("__dict__").method("get", { py{ "_is_protocol" }, boolean(false) }).truthy())
                    return not_implemented_value();
                for (auto const& attr : klass.attr("__protocol_attrs__").items())
                {
                    bool found = false;
                    for (auto const& base : other.attr("__mro__").items())
                    {
                        py const d = base.attr("__dict__");
                        if (d.contains(attr))
                        {
                            if (d[attr].is_none())
                                return not_implemented_value();
                            found = true;
                            break;
                        }
                        py const annotations = attr_or(base, "__annotations__", py_dict());
                        if (isinstance(annotations, import_attribute("collections.abc", "Mapping")) && annotations.contains(attr) && issubclass(other, obj("Generic")) &&
                            attr_or(other, "_is_protocol", boolean(false)).truthy())
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                        return not_implemented_value();
                }
                return boolean(true);
            }));
            define("_proto_hook", proto_hook());
            make_typing_class("Protocol", { obj("Generic") }, {
                { "__slots__", py_tuple() },
                { "_is_protocol", boolean(true) },
                { "_is_runtime_protocol", boolean(false) },
                { "__init_subclass__", classmethod_of(function("__init_subclass__", "Protocol.__init_subclass__", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        if (aArgs.empty())
                            py_raise("TypeError", "Protocol.__init_subclass__(): not enough arguments");
                        auto const& klass = aArgs[0];
                        std::vector<object*> args;
                        for (std::size_t i = 1u; i < aArgs.size(); ++i)
                            args.push_back(aArgs[i].reference());
                        py{ invoke(builtin("super")(obj("Protocol"), klass).attr("__init_subclass__").get(), std::move(args), aKwargs) };
                        if (!klass.attr("__dict__").method("get", { py{ "_is_protocol" }, boolean(false) }).truthy())
                        {
                            bool isProtocol = false;
                            for (auto const& b : klass.attr("__bases__").items())
                                if (b.is(obj("Protocol")))
                                    isProtocol = true;
                            klass.set_attr("_is_protocol", boolean(isProtocol));
                        }
                        if (!klass.attr("__dict__").contains(py{ "__subclasshook__" }))
                            klass.set_attr("__subclasshook__", proto_hook());
                        // (cls.__init__ is Protocol.__init__: object's (no class of its MRO but object defines it))
                        bool inherited = true;
                        for (auto const& base : klass.attr("__mro__").items())
                            if (!base.is(builtin("object")) && base.attr("__dict__").contains(py{ "__init__" }))
                                inherited = false;
                        if (klass.attr("_is_protocol").truthy() && inherited)
                            klass.set_attr("__init__", no_init_or_replace_init());
                        return none();
                    })) } },
                typing_docs::doc("Protocol"), cls("_ProtocolMeta"));
            define("Protocol", cls("Protocol"));

            // (_AnnotatedAlias)
            make_typing_class("_AnnotatedAlias", { cls("_NotIterable"), cls("_GenericAlias") }, {
                { "__init__", method("_AnnotatedAlias", "__init__", { py_positional("origin"), py_positional("metadata") }, [](std::vector<py> const& a)
                    {
                        py origin = a[1];
                        py metadata = a[2];
                        if (is_instance_of(origin, "_AnnotatedAlias"))
                        {
                            metadata = origin.attr("__metadata__") + metadata;
                            origin = origin.attr("__origin__");
                        }
                        generic_alias_init(a[0], origin, origin, true, py{ "Annotated" });
                        a[0].set_attr("__metadata__", metadata);
                        return none();
                    }) },
                { "copy_with", method("_AnnotatedAlias", "copy_with", { py_positional("params") }, [](std::vector<py> const& a)
                    {
                        return cls("_AnnotatedAlias")(a[1][py{ 0 }], a[0].attr("__metadata__"));
                    }) },
                { "__repr__", method("_AnnotatedAlias", "__repr__", {}, [](std::vector<py> const& a)
                    {
                        std::string metadata;
                        for (auto const& m : a[0].attr("__metadata__").items())
                            metadata += (metadata.empty() ? "" : ", ") + m.repr();
                        return py{ "typing.Annotated[" + type_repr(a[0].attr("__origin__")) + ", " + metadata + "]" };
                    }) },
                { "__reduce__", method("_AnnotatedAlias", "__reduce__", {}, [](std::vector<py> const& a)
                    {
                        return py_tuple({ import_attribute("operator", "getitem"), py_tuple({ obj("Annotated"), py_tuple({ a[0].attr("__origin__") }) + a[0].attr("__metadata__") }) });
                    }) },
                { "__eq__", method("_AnnotatedAlias", "__eq__", { py_positional("other") }, [](std::vector<py> const& a) -> py
                    {
                        if (!is_instance_of(a[1], "_AnnotatedAlias"))
                            return not_implemented_value();
                        return boolean(a[0].attr("__origin__") == a[1].attr("__origin__") && a[0].attr("__metadata__") == a[1].attr("__metadata__"));
                    }) },
                { "__hash__", method("_AnnotatedAlias", "__hash__", {}, [](std::vector<py> const& a)
                    {
                        return builtin("hash")(py_tuple({ a[0].attr("__origin__"), a[0].attr("__metadata__") }));
                    }) },
                { "__getattr__", method("_AnnotatedAlias", "__getattr__", { py_positional("attr") }, [](std::vector<py> const& a)
                    {
                        std::string const attribute = a[1].str();
                        if (attribute == "__name__" || attribute == "__qualname__")
                            return py{ "Annotated" };
                        return base_generic_alias_getattr(a[0], a[1]);
                    }) },
                { "__mro_entries__", method("_AnnotatedAlias", "__mro_entries__", { py_positional("bases") }, [](std::vector<py> const& a)
                    {
                        return py_tuple({ a[0].attr("__origin__") });
                    }) } },
                typing_docs::doc("_AnnotatedAlias"), {}, root);
        }

        // (a class whose bases may be generic aliases (their __mro_entries__, as a class statement resolves them))
        py make_generic_class(char const* aName, std::vector<py> const& aBases, namespace_type aNamespace, char const* aDoc, py const& aMetaclass = {})
        {
            py const original = py_tuple(aBases);
            py const resolved = types_attr("resolve_bases")(original);
            if (!resolved.is(original) && !(resolved == original))
                aNamespace.emplace_back("__orig_bases__", original);
            return make_typing_class(aName, resolved.items(), aNamespace, aDoc, aMetaclass);
        }

        py abstract(py const& aFunction) { return import_attribute("abc", "abstractmethod")(aFunction); }
        py abstract_method(char const* aClass, char const* aName, std::vector<py_parameter> aParameters = {})
        {
            return abstract(method(aClass, aName, std::move(aParameters), [](std::vector<py> const&) { return none(); }));
        }
        py abstract_property(char const* aClass, char const* aName)
        {
            return builtin("property")(abstract_method(aClass, aName));
        }

        // --- get_type_hints and the other functions

        py strip_annotations(py const& t)
        {
            if (is_instance_of(t, "_AnnotatedAlias"))
                return strip_annotations(t.attr("__origin__"));
            if (has_attr(t, "__origin__") && in_tuple(t.attr("__origin__"), { obj("Required"), obj("NotRequired"), obj("ReadOnly") }))
                return strip_annotations(t.attr("__args__")[py{ 0 }]);
            auto const stripped_args = [&]()
            {
                std::vector<py> args;
                for (auto const& a : t.attr("__args__").items())
                    args.push_back(strip_annotations(a));
                return py_tuple(args);
            };
            if (is_generic_alias(t))
            {
                py const stripped = stripped_args();
                if (stripped == t.attr("__args__"))
                    return t;
                return t.method("copy_with", { stripped });
            }
            if (is_types_generic_alias(t))
            {
                py const stripped = stripped_args();
                if (stripped == t.attr("__args__"))
                    return t;
                return types_attr("GenericAlias")(t.attr("__origin__"), stripped);
            }
            if (is_union_type(t))
            {
                py const stripped = stripped_args();
                if (stripped == t.attr("__args__"))
                    return t;
                return import_attribute("functools", "reduce")(import_attribute("operator", "or_"), stripped);
            }
            return t;
        }

        py strip_all(py const& aHints, bool aIncludeExtras)
        {
            if (aIncludeExtras)
                return aHints;
            py const result = py_dict();
            for (auto const& item : aHints.method("items").items())
                result.set_item(item[py{ 0 }], strip_annotations(item[py{ 1 }]));
            return result;
        }

        py get_type_hints(py const& aObject, py aGlobals, py aLocals, bool aIncludeExtras)
        {
            if (attr_or(aObject, "__no_type_check__", none()).truthy())
                return py_dict();
            py const emptyGuard = builtin("frozenset")();
            if (is_type(aObject))
            {
                py const hints = py_dict();
                auto const mro = aObject.attr("__mro__").items();
                for (auto b = mro.rbegin(); b != mro.rend(); ++b)
                {
                    auto const& base = *b;
                    py baseGlobals;
                    if (aGlobals.is_none())
                        baseGlobals = attr_or(import_attribute("sys", "modules").method("get", { attr_or(base, "__module__", none()), none() }), "__dict__", py_dict());
                    else
                        baseGlobals = aGlobals;
                    py annotations = base.attr("__dict__").method("get", { py{ "__annotations__" }, py_dict() });
                    if (isinstance(annotations, types_attr("GetSetDescriptorType")))
                        annotations = py_dict();
                    py baseLocals = aLocals.is_none() ? builtin("dict")(builtin("vars")(base)) : aLocals;
                    if (aLocals.is_none() && aGlobals.is_none())
                        std::swap(baseGlobals, baseLocals);
                    for (auto const& item : annotations.method("items").items())
                    {
                        py value = item[py{ 1 }];
                        if (value.is_none())
                            value = none_type();
                        if (is_str(value))
                            value = cls("ForwardRef").call({ value }, { { "is_argument", boolean(false) }, { "is_class", boolean(true) } });
                        value = eval_type(value, baseGlobals, baseLocals, attr_or(base, "__type_params__", py_tuple()), emptyGuard);
                        hints.set_item(item[py{ 0 }], value);
                    }
                }
                return strip_all(hints, aIncludeExtras);
            }
            if (aGlobals.is_none())
            {
                if (isinstance(aObject, types_attr("ModuleType")))
                    aGlobals = aObject.attr("__dict__");
                else
                {
                    py ns = aObject;
                    std::set<object*> seen{ ns.get() };
                    while (has_attr(ns, "__wrapped__"))
                    {
                        ns = ns.attr("__wrapped__");
                        if (!seen.insert(ns.get()).second)
                            py_raise("ValueError", "wrapper loop when unwrapping " + aObject.repr());
                    }
                    aGlobals = attr_or(ns, "__globals__", py_dict());
                }
                if (aLocals.is_none())
                    aLocals = aGlobals;
            }
            else if (aLocals.is_none())
                aLocals = aGlobals;
            py hints = attr_or(aObject, "__annotations__", none());
            if (hints.is_none())
            {
                for (auto t : { "FunctionType", "BuiltinFunctionType", "MethodType", "ModuleType", "WrapperDescriptorType", "MethodWrapperType", "MethodDescriptorType" })
                    if (isinstance(aObject, types_attr(t)))
                        return py_dict();
                py_raise("TypeError", aObject.repr() + " is not a module, class, method, or function.");
            }
            hints = builtin("dict")(hints);
            py const typeParams = attr_or(aObject, "__type_params__", py_tuple());
            for (auto const& item : hints.method("items").items())
            {
                py value = item[py{ 1 }];
                if (value.is_none())
                    value = none_type();
                if (is_str(value))
                    value = cls("ForwardRef").call({ value }, { { "is_argument", boolean(!isinstance(aObject, types_attr("ModuleType"))) }, { "is_class", boolean(false) } });
                hints.set_item(item[py{ 0 }], eval_type(value, aGlobals, aLocals, typeParams, emptyGuard));
            }
            return strip_all(hints, aIncludeExtras);
        }

        py get_origin(py const& tp)
        {
            if (is_instance_of(tp, "_AnnotatedAlias"))
                return obj("Annotated");
            if (is_instance_of(tp, "_BaseGenericAlias") || is_types_generic_alias(tp) || is_instance_of(tp, "ParamSpecArgs") || is_instance_of(tp, "ParamSpecKwargs"))
                return tp.attr("__origin__");
            if (tp.is(obj("Generic")))
                return obj("Generic");
            if (is_union_type(tp))
                return types_attr("UnionType");
            return none();
        }

        py get_args(py const& tp)
        {
            if (is_instance_of(tp, "_AnnotatedAlias"))
                return py_tuple({ tp.attr("__origin__") }) + tp.attr("__metadata__");
            if (is_generic_alias(tp) || is_types_generic_alias(tp))
            {
                py res = tp.attr("__args__");
                if (should_unflatten_callable_args(tp, res))
                    res = py_tuple({ builtin("list")(slice(res, std::nullopt, -1)), res[py{ -1 }] });
                return res;
            }
            if (is_union_type(tp))
                return tp.attr("__args__");
            return py_tuple();
        }

        py no_type_check(py const& aArg)
        {
            if (is_type(aArg))
                for (auto const& key : builtin("dir")(aArg).items())
                {
                    py const value = builtin("getattr")(aArg, key);
                    if (!has_attr(value, "__qualname__") || value.attr("__qualname__").str() != aArg.attr("__qualname__").str() + "." + attr_or(value, "__name__", py{ "" }).str() ||
                        !(attr_or(value, "__module__", none()) == aArg.attr("__module__")))
                        continue;
                    if (isinstance(value, types_attr("FunctionType")))
                        value.set_attr("__no_type_check__", boolean(true));
                    if (isinstance(value, types_attr("MethodType")))
                        value.attr("__func__").set_attr("__no_type_check__", boolean(true));
                    if (is_type(value))
                        no_type_check(value);
                }
            try
            {
                aArg.set_attr("__no_type_check__", boolean(true));
            }
            catch (raised const&)
            {
                if (!caught({ "TypeError" }))
                    throw;
            }
            return aArg;
        }

        // --- NamedTuple

        py make_nmtuple(py const& aName, py const& aTypes, py const& aModule, py const& aDefaults = py_tuple())
        {
            std::vector<py> fields;
            py const types = py_dict();
            for (auto const& item : aTypes.items())
            {
                auto const pair = item.items();
                if (pair.size() != 2u)
                    py_raise("ValueError", "too many values to unpack (expected 2)");
                fields.push_back(pair[0]);
                types.set_item(pair[0], type_check(pair[1], "field " + pair[0].str() + " annotation must be a type"));
            }
            py const nmTpl = import_attribute("collections", "namedtuple").call({ aName, py_list(fields) }, { { "defaults", aDefaults }, { "module", aModule } });
            nmTpl.set_attr("__annotations__", types);
            try
            {
                nmTpl.attr("__new__").set_attr("__annotations__", types);
            }
            catch (raised const&)
            {
                if (!caught({ "AttributeError", "TypeError" }))
                    throw;
            }
            return nmTpl;
        }

        std::set<std::string> const sProhibited{ "__new__", "__init__", "__slots__", "__getnewargs__", "_fields", "_field_defaults", "_make", "_replace", "_asdict", "_source" };
        std::set<std::string> const sSpecial{ "__module__", "__name__", "__annotations__" };

        py named_tuple_meta_new(py const& aTypename, py const& aBases, py const& aNamespace)
        {
            for (auto const& base : aBases.items())
                if (!base.is(obj("_NamedTuple")) && !base.is(obj("Generic")))
                    py_raise("TypeError", "can only inherit from a NamedTuple type and Generic");
            std::vector<py> bases;
            bool generic = false;
            for (auto const& base : aBases.items())
            {
                bases.push_back(base.is(obj("_NamedTuple")) ? builtin("tuple") : base);
                if (base.is(obj("Generic")))
                    generic = true;
            }
            py const types = aNamespace.method("get", { py{ "__annotations__" }, py_dict() });
            std::vector<std::string> defaultNames;
            for (auto const& fieldName : builtin("list")(types).items())
            {
                if (aNamespace.contains(fieldName))
                    defaultNames.push_back(fieldName.str());
                else if (!defaultNames.empty())
                {
                    std::string names;
                    for (auto const& n : defaultNames)
                        names += (names.empty() ? "" : ", ") + n;
                    py_raise("TypeError", "Non-default namedtuple field " + fieldName.str() + " cannot follow default field" + (defaultNames.size() > 1u ? "s" : "") + " " + names);
                }
            }
            std::vector<py> defaults;
            for (auto const& n : defaultNames)
                defaults.push_back(aNamespace[py{ n }]);
            py const nmTpl = make_nmtuple(aTypename, types.method("items"), aNamespace[py{ "__module__" }], py_list(defaults));
            if (!(nmTpl.attr("__bases__") == py_tuple(bases)))
            {
                try
                {
                    nmTpl.set_attr("__bases__", py_tuple(bases));
                }
                catch (raised const&)
                {
                    if (!caught({ "Exception" }))
                        throw;
                }
            }
            if (generic)
                nmTpl.set_attr("__class_getitem__", classmethod_of(function("__class_getitem__", "_generic_class_getitem", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                {
                    auto const a = py_bind("_generic_class_getitem", { py_positional("cls"), py_positional("args") }, aArgs, aKwargs);
                    return generic_class_getitem(a[0], a[1]);
                })));
            py const fields = nmTpl.attr("_fields");
            for (auto const& item : aNamespace.method("items").items())
            {
                py const key = item[py{ 0 }];
                py const value = item[py{ 1 }];
                std::string const k = key.str();
                if (sProhibited.contains(k))
                    py_raise("AttributeError", "Cannot overwrite NamedTuple attribute " + k);
                else if (!sSpecial.contains(k))
                {
                    if (!fields.contains(key))
                        nmTpl.set_attr(k.c_str(), value);
                    py const setName = attr_or(type_of(value), "__set_name__", py{});
                    if (!setName.empty())
                    {
                        try
                        {
                            setName(value, nmTpl, key);
                        }
                        catch (raised const&)
                        {
                            py const e = py_take_exception();
                            attr_or(e, "add_note", py{}).empty() ? void() : (void)e.method("add_note", { py{ "Error calling __set_name__ on " +
                                type_of(value).attr("__name__").repr() + " instance " + key.repr() + " in " + aTypename.repr() } });
                            py_reraise(e);
                        }
                    }
                }
            }
            if (generic)
                nmTpl.method("__init_subclass__");
            return nmTpl;
        }

        // --- TypedDict

        std::vector<py> typeddict_qualifiers(py annotationType)
        {
            std::vector<py> result;
            for (;;)
            {
                py const origin = get_origin(annotationType);
                if (origin.is(obj("Annotated")))
                {
                    py const args = get_args(annotationType);
                    if (args.truthy())
                        annotationType = args[py{ 0 }];
                    else
                        break;
                }
                else if (origin.is(obj("Required")) || origin.is(obj("NotRequired")) || origin.is(obj("ReadOnly")))
                {
                    result.push_back(origin);
                    auto const args = get_args(annotationType).items();
                    if (args.size() != 1u)
                        py_raise("ValueError", "too many values to unpack (expected 1)");
                    annotationType = args[0];
                }
                else
                    break;
            }
            return result;
        }

        py typed_dict_meta_new(py const& aMetaclass, py const& aName, py const& aBases, py const& aNamespace, bool aTotal)
        {
            (void)aMetaclass;
            for (auto const& base : aBases.items())
                if (!type_of(base).is(cls("_TypedDictMeta")) && !base.is(obj("Generic")))
                    py_raise("TypeError", "cannot inherit from both a TypedDict type and a non-TypedDict base class");
            std::vector<py> bases;
            for (auto const& b : aBases.items())
                if (issubclass(b, obj("Generic")))
                {
                    bases.push_back(obj("Generic"));
                    break;
                }
            bases.push_back(builtin("dict"));
            py const tpDict = builtin("type").attr("__new__")(cls("_TypedDictMeta"), aName, py_tuple(bases), aNamespace);
            if (!has_attr(tpDict, "__orig_bases__"))
                tpDict.set_attr("__orig_bases__", aBases);
            py const annotations = py_dict();
            py ownAnnotations = aNamespace.method("get", { py{ "__annotations__" }, py_dict() });
            {
                py const checked = py_dict();
                for (auto const& item : ownAnnotations.method("items").items())
                    checked.set_item(item[py{ 0 }], type_check(item[py{ 1 }], "TypedDict('Name', {f0: t0, f1: t1, ...}); each t must be a type", true, tpDict.attr("__module__")));
                ownAnnotations = checked;
            }
            py requiredKeys = builtin("set")();
            py optionalKeys = builtin("set")();
            py const readonlyKeys = builtin("set")();
            py const mutableKeys = builtin("set")();
            for (auto const& base : aBases.items())
            {
                py const d = base.attr("__dict__");
                annotations.method("update", { d.method("get", { py{ "__annotations__" }, py_dict() }) });
                py const baseRequired = d.method("get", { py{ "__required_keys__" }, builtin("set")() });
                requiredKeys = requiredKeys | baseRequired;
                optionalKeys = optionalKeys - baseRequired;
                py const baseOptional = d.method("get", { py{ "__optional_keys__" }, builtin("set")() });
                requiredKeys = requiredKeys - baseOptional;
                optionalKeys = optionalKeys | baseOptional;
                readonlyKeys.method("update", { d.method("get", { py{ "__readonly_keys__" }, py_tuple() }) });
                mutableKeys.method("update", { d.method("get", { py{ "__mutable_keys__" }, py_tuple() }) });
            }
            annotations.method("update", { ownAnnotations });
            for (auto const& item : ownAnnotations.method("items").items())
            {
                py const key = item[py{ 0 }];
                auto const qualifiers = typeddict_qualifiers(item[py{ 1 }]);
                auto const has = [&](char const* aName) { for (auto const& q : qualifiers) if (q.is(obj(aName))) return true; return false; };
                bool const isRequired = has("Required") ? true : has("NotRequired") ? false : aTotal;
                if (isRequired)
                {
                    requiredKeys.method("add", { key });
                    optionalKeys.method("discard", { key });
                }
                else
                {
                    optionalKeys.method("add", { key });
                    requiredKeys.method("discard", { key });
                }
                if (has("ReadOnly"))
                {
                    if (mutableKeys.contains(key))
                        py_raise("TypeError", "Cannot override mutable key " + key.repr() + " with read-only key");
                    readonlyKeys.method("add", { key });
                }
                else
                {
                    mutableKeys.method("add", { key });
                    readonlyKeys.method("discard", { key });
                }
            }
            tpDict.set_attr("__annotations__", annotations);
            tpDict.set_attr("__required_keys__", builtin("frozenset")(requiredKeys));
            tpDict.set_attr("__optional_keys__", builtin("frozenset")(optionalKeys));
            tpDict.set_attr("__readonly_keys__", builtin("frozenset")(readonlyKeys));
            tpDict.set_attr("__mutable_keys__", builtin("frozenset")(mutableKeys));
            tpDict.set_attr("__total__", boolean(aTotal));
            return tpDict;
        }

        void make_other_classes()
        {
            auto& s = state();
            // (NamedTuple)
            make_typing_class("NamedTupleMeta", { builtin("type") }, {
                { "__new__", staticmethod_of(function("__new__", "NamedTupleMeta.__new__", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const a = py_bind("NamedTupleMeta.__new__", { py_positional("cls"), py_positional("typename"), py_positional("bases"), py_positional("ns") },
                            aArgs, aKwargs);
                        return named_tuple_meta_new(a[1], a[2], a[3]);
                    })) } });
            py const namedTuple = module_function("NamedTuple", { py_positional_only("typename"), py_positional_only("fields", s.sentinel) }, [](std::vector<py> const& a) -> py
            {
                auto const& sentinel = state().sentinel;
                py fields = a[1];
                py const& kwargs = a[2];
                std::string deprecatedThing;
                std::string deprecationMessage;
                std::string const typename_ = a[0].str();
                if (fields.is(sentinel))
                {
                    if (kwargs.truthy())
                    {
                        deprecatedThing = "Creating NamedTuple classes using keyword arguments";
                        deprecationMessage = "{name} is deprecated and will be disallowed in Python {remove}. Use the class-based or functional syntax instead.";
                    }
                    else
                    {
                        deprecatedThing = "Failing to pass a value for the 'fields' parameter";
                        deprecationMessage = "{name} is deprecated and will be disallowed in Python {remove}. To create a NamedTuple class with 0 fields using the "
                            "functional syntax, pass an empty list, e.g. `" + typename_ + " = NamedTuple(" + a[0].repr() + ", [])`.";
                    }
                }
                else if (fields.is_none())
                {
                    if (kwargs.truthy())
                        py_raise("TypeError", "Cannot pass `None` as the 'fields' parameter and also specify fields using keyword arguments");
                    deprecatedThing = "Passing `None` as the 'fields' parameter";
                    deprecationMessage = "{name} is deprecated and will be disallowed in Python {remove}. To create a NamedTuple class with 0 fields using the "
                        "functional syntax, pass an empty list, e.g. `" + typename_ + " = NamedTuple(" + a[0].repr() + ", [])`.";
                }
                else if (kwargs.truthy())
                    py_raise("TypeError", "Either list of fields or keywords can be provided to NamedTuple, not both");
                if (fields.is(sentinel) || fields.is_none())
                {
                    py const deprecated = attr_or(py{ fn_import("warnings") }, "_deprecated", py{});
                    if (!deprecated.empty())
                        deprecated.call({ py{ deprecatedThing } }, { { "message", py{ deprecationMessage } }, { "remove", py_tuple({ py{ 3 }, py{ 15 } }) } });
                    fields = kwargs.method("items");
                }
                py const nt = make_nmtuple(a[0], fields, caller());
                nt.set_attr("__orig_bases__", py_tuple({ obj("NamedTuple") }));
                return nt;
            }, typing_docs::doc("NamedTuple"), false, true);
            define("NamedTuple", namedTuple);
            {
                py const ns = py_dict();
                ns.set_item(py{ "__module__" }, py{ sModule });
                define("_NamedTuple", builtin("type").attr("__new__")(cls("NamedTupleMeta"), py{ "NamedTuple" }, py_tuple(), ns));
            }
            namedTuple.set_attr("__mro_entries__", function("_namedtuple_mro_entries", "_namedtuple_mro_entries", [](std::vector<py> const&, object*)
            {
                return py_tuple({ obj("_NamedTuple") });
            }));

            // (TypedDict)
            make_typing_class("_TypedDictMeta", { builtin("type") }, {
                { "__new__", staticmethod_of(function("__new__", "_TypedDictMeta.__new__", [](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const a = py_bind("_TypedDictMeta.__new__", { py_positional("cls"), py_positional("name"), py_positional("bases"), py_positional("ns"),
                            py_positional("total", boolean(true)) }, aArgs, aKwargs);
                        return typed_dict_meta_new(a[0], a[1], a[2], a[3], a[4].truthy());
                    })) },
                { "__call__", builtin("dict") },
                { "__subclasscheck__", method("_TypedDictMeta", "__subclasscheck__", { py_positional("other") }, [](std::vector<py> const&) -> py
                    {
                        py_raise("TypeError", "TypedDict does not support instance and class checks");
                    }) },
                { "__instancecheck__", method("_TypedDictMeta", "__subclasscheck__", { py_positional("other") }, [](std::vector<py> const&) -> py
                    {
                        py_raise("TypeError", "TypedDict does not support instance and class checks");
                    }) } });
            py const typedDict = module_function("TypedDict", { py_positional_only("typename"), py_positional_only("fields", s.sentinel), py_keyword("total", boolean(true)) },
                [](std::vector<py> const& a)
                {
                    py fields = a[1];
                    if (fields.is(state().sentinel) || fields.is_none())
                    {
                        std::string const thing = fields.is(state().sentinel) ? "Failing to pass a value for the 'fields' parameter" : "Passing `None` as the 'fields' parameter";
                        std::string const example = "`" + a[0].str() + " = TypedDict(" + a[0].repr() + ", {{}})`";
                        std::string const message = "{name} is deprecated and will be disallowed in Python {remove}. To create a TypedDict class with 0 fields using the "
                            "functional syntax, pass an empty dictionary, e.g. " + example + ".";
                        py const deprecated = attr_or(py{ fn_import("warnings") }, "_deprecated", py{});
                        if (!deprecated.empty())
                            deprecated.call({ py{ thing } }, { { "message", py{ message } }, { "remove", py_tuple({ py{ 3 }, py{ 15 } }) } });
                        fields = py_dict();
                    }
                    py const ns = py_dict();
                    ns.set_item(py{ "__annotations__" }, builtin("dict")(fields));
                    py const module = caller();
                    if (!module.is_none())
                        ns.set_item(py{ "__module__" }, module);
                    py const td = cls("_TypedDictMeta").call({ a[0], py_tuple(), ns }, { { "total", a[2] } });
                    td.set_attr("__orig_bases__", py_tuple({ obj("TypedDict") }));
                    return td;
                }, typing_docs::doc("TypedDict"));
            define("TypedDict", typedDict);
            {
                py const ns = py_dict();
                ns.set_item(py{ "__module__" }, py{ sModule });
                define("_TypedDict", builtin("type").attr("__new__")(cls("_TypedDictMeta"), py{ "TypedDict" }, py_tuple(), ns));
            }
            typedDict.set_attr("__mro_entries__", function("<lambda>", "<lambda>", [](std::vector<py> const&, object*) { return py_tuple({ obj("_TypedDict") }); }));

            // (NewType)
            make_typing_class("NewType", {}, {
                { "__call__", staticmethod_of(obj("_idfunc")) },
                { "__init__", method("NewType", "__init__", { py_positional("name"), py_positional("tp") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py name = a[1];
                        self.set_attr("__qualname__", name);
                        if (name.contains(py{ "." }))
                            name = name.method("rpartition", { py{ "." } })[py{ -1 }];
                        self.set_attr("__name__", name);
                        self.set_attr("__supertype__", a[2]);
                        py const module = caller();
                        if (!(module == py{ "typing" }))
                            self.set_attr("__module__", module);
                        return none();
                    }) },
                { "__mro_entries__", method("NewType", "__mro_entries__", { py_positional("bases") }, [](std::vector<py> const& a)
                    {
                        std::string const superclassName = a[0].attr("__name__").str();
                        py const dummy = make_typing_class("Dummy", {}, { { "__init_subclass__", classmethod_of(function("__init_subclass__",
                            "NewType.__mro_entries__.<locals>.Dummy.__init_subclass__", [superclassName](std::vector<py> const& aArgs, object*) -> py
                            {
                                std::string const subclassName = aArgs.at(0).attr("__name__").str();
                                py_raise("TypeError", "Cannot subclass an instance of NewType. Perhaps you were looking for: `" + subclassName + " = NewType(" +
                                    py{ subclassName }.repr() + ", " + superclassName + ")`");
                            })) } });
                        dummy.set_attr("__qualname__", py{ "NewType.__mro_entries__.<locals>.Dummy" });
                        state().classes.erase("Dummy");
                        return py_tuple({ dummy });
                    }) },
                { "__repr__", method("NewType", "__repr__", {}, [](std::vector<py> const& a)
                    {
                        return py{ a[0].attr("__module__").str() + "." + a[0].attr("__qualname__").str() };
                    }) },
                { "__reduce__", method("NewType", "__reduce__", {}, [](std::vector<py> const& a) { return a[0].attr("__qualname__"); }) },
                { "__or__", union_method("NewType", "__or__", false) },
                { "__ror__", union_method("NewType", "__ror__", true) } },
                typing_docs::doc("NewType"));
        }

        // (the type variables and aliases of the module)
        py typing_typevar(char const* aName, std::vector<py> const& aConstraints = {}, std::vector<std::pair<char const*, py>> const& aOptions = {})
        {
            std::vector<py> args{ py{ aName } };
            args.insert(args.end(), aConstraints.begin(), aConstraints.end());
            py const t = cls("TypeVar").call(args, aOptions);
            t.set_attr("__module__", py{ sModule });
            return t;
        }

        py special_alias(py const& aOrigin, long long aParams, std::vector<std::pair<char const*, py>> const& aOptions = {}, char const* aClass = "_SpecialGenericAlias")
        {
            return cls(aClass).call({ aOrigin, py{ aParams } }, aOptions);
        }

        void make_aliases()
        {
            py const abc = py{ fn_import("collections.abc") };
            define("T", typing_typevar("T"));
            define("KT", typing_typevar("KT"));
            define("VT", typing_typevar("VT"));
            define("T_co", typing_typevar("T_co", {}, { { "covariant", boolean(true) } }));
            define("V_co", typing_typevar("V_co", {}, { { "covariant", boolean(true) } }));
            define("VT_co", typing_typevar("VT_co", {}, { { "covariant", boolean(true) } }));
            define("T_contra", typing_typevar("T_contra", {}, { { "contravariant", boolean(true) } }));
            define("CT_co", typing_typevar("CT_co", {}, { { "covariant", boolean(true) }, { "bound", builtin("type") } }));
            define("AnyStr", typing_typevar("AnyStr", { builtin("bytes"), builtin("str") }));
            for (auto const& [name, params] : std::initializer_list<std::pair<char const*, long long>>{ { "Hashable", 0 }, { "Awaitable", 1 }, { "Coroutine", 3 },
                { "AsyncIterable", 1 }, { "AsyncIterator", 1 }, { "Iterable", 1 }, { "Iterator", 1 }, { "Reversible", 1 }, { "Sized", 0 }, { "Container", 1 },
                { "Collection", 1 } })
                define(name, special_alias(abc.attr(name), params));
            define("Callable", special_alias(abc.attr("Callable"), 2, {}, "_CallableType"));
            obj("Callable").set_attr("__doc__", py{ typing_docs::doc("Callable") });
            define("AbstractSet", special_alias(abc.attr("Set"), 1, { { "name", py{ "AbstractSet" } } }));
            define("MutableSet", special_alias(abc.attr("MutableSet"), 1));
            define("Mapping", special_alias(abc.attr("Mapping"), 2));
            define("MutableMapping", special_alias(abc.attr("MutableMapping"), 2));
            define("Sequence", special_alias(abc.attr("Sequence"), 1));
            define("MutableSequence", special_alias(abc.attr("MutableSequence"), 1));
            {
                py const byteString = attr_or(abc, "ByteString", py{});
                if (!byteString.empty())
                    define("ByteString", cls("_DeprecatedGenericAlias").call({ byteString, py{ 0 } }, { { "removal_version", py_tuple({ py{ 3 }, py{ 17 } }) } }));
            }
            define("Tuple", special_alias(builtin("tuple"), -1, { { "inst", boolean(false) }, { "name", py{ "Tuple" } } }, "_TupleType"));
            obj("Tuple").set_attr("__doc__", py{ typing_docs::doc("Tuple") });
            define("List", special_alias(builtin("list"), 1, { { "inst", boolean(false) }, { "name", py{ "List" } } }));
            define("Deque", special_alias(import_attribute("collections", "deque"), 1, { { "name", py{ "Deque" } } }));
            define("Set", special_alias(builtin("set"), 1, { { "inst", boolean(false) }, { "name", py{ "Set" } } }));
            define("FrozenSet", special_alias(builtin("frozenset"), 1, { { "inst", boolean(false) }, { "name", py{ "FrozenSet" } } }));
            define("MappingView", special_alias(abc.attr("MappingView"), 1));
            define("KeysView", special_alias(abc.attr("KeysView"), 1));
            define("ItemsView", special_alias(abc.attr("ItemsView"), 2));
            define("ValuesView", special_alias(abc.attr("ValuesView"), 1));
            define("Dict", special_alias(builtin("dict"), 2, { { "inst", boolean(false) }, { "name", py{ "Dict" } } }));
            define("DefaultDict", special_alias(import_attribute("collections", "defaultdict"), 2, { { "name", py{ "DefaultDict" } } }));
            define("OrderedDict", special_alias(import_attribute("collections", "OrderedDict"), 2));
            define("Counter", special_alias(import_attribute("collections", "Counter"), 1));
            define("ChainMap", special_alias(import_attribute("collections", "ChainMap"), 2));
            define("Generator", special_alias(abc.attr("Generator"), 3, { { "defaults", py_tuple({ none_type(), none_type() }) } }));
            define("AsyncGenerator", special_alias(abc.attr("AsyncGenerator"), 2, { { "defaults", py_tuple({ none_type() }) } }));
            define("Type", special_alias(builtin("type"), 1, { { "inst", boolean(false) }, { "name", py{ "Type" } } }));
            obj("Type").set_attr("__doc__", py{ typing_docs::doc("Type") });
            define("Pattern", special_alias(import_attribute("re", "Pattern"), 1));
            define("Match", special_alias(import_attribute("re", "Match"), 1));
            define("ContextManager", special_alias(import_attribute("contextlib", "AbstractContextManager"), 2, { { "name", py{ "ContextManager" } },
                { "defaults", py_tuple({ builtin("bool") | none() }) } }));
            define("AsyncContextManager", special_alias(import_attribute("contextlib", "AbstractAsyncContextManager"), 2, { { "name", py{ "AsyncContextManager" } },
                { "defaults", py_tuple({ builtin("bool") | none() }) } }));

            // (the protocols SupportsInt, ...)
            for (auto const& [name, method_] : std::initializer_list<std::pair<char const*, char const*>>{ { "SupportsInt", "__int__" }, { "SupportsFloat", "__float__" },
                { "SupportsComplex", "__complex__" }, { "SupportsBytes", "__bytes__" }, { "SupportsIndex", "__index__" } })
            {
                make_typing_class(name, { obj("Protocol") }, { { "__slots__", py_tuple() }, { method_, abstract_method(name, method_) } }, typing_docs::doc(name));
                runtime_checkable(cls(name));
                define(name, cls(name));
            }
            for (auto const& [name, method_] : std::initializer_list<std::pair<char const*, char const*>>{ { "SupportsAbs", "__abs__" }, { "SupportsRound", "__round__" } })
            {
                py const t = cls("TypeVar").call({ py{ "T" } }, { { "infer_variance", boolean(true) } });
                t.set_attr("__module__", py{ sModule });
                std::vector<py_parameter> parameters;
                if (std::strcmp(method_, "__round__") == 0)
                    parameters.push_back(py_positional("ndigits", py{ 0 }));
                make_generic_class(name, { obj("Protocol"), obj("Generic")[t] }, { { "__slots__", py_tuple() }, { "__type_params__", py_tuple({ t }) },
                    { method_, abstract_method(name, method_, parameters) } }, typing_docs::doc(name));
                runtime_checkable(cls(name));
                define(name, cls(name));
            }

            // (IO, BinaryIO and TextIO)
            namespace_type io{ { "__slots__", py_tuple() } };
            for (auto p : { "mode", "name", "closed" })
                io.emplace_back(p, abstract_property("IO", p));
            for (auto m : { "close", "fileno", "flush", "isatty", "readable", "seekable", "tell", "writable" })
                io.emplace_back(m, abstract_method("IO", m));
            io.emplace_back("read", abstract_method("IO", "read", { py_positional("n", py{ -1 }) }));
            io.emplace_back("readline", abstract_method("IO", "readline", { py_positional("limit", py{ -1 }) }));
            io.emplace_back("readlines", abstract_method("IO", "readlines", { py_positional("hint", py{ -1 }) }));
            io.emplace_back("seek", abstract_method("IO", "seek", { py_positional("offset"), py_positional("whence", py{ 0 }) }));
            io.emplace_back("truncate", abstract_method("IO", "truncate", { py_positional("size", none()) }));
            io.emplace_back("write", abstract_method("IO", "write", { py_positional("s") }));
            io.emplace_back("writelines", abstract_method("IO", "writelines", { py_positional("lines") }));
            io.emplace_back("__enter__", abstract_method("IO", "__enter__"));
            io.emplace_back("__exit__", abstract_method("IO", "__exit__", { py_positional("type"), py_positional("value"), py_positional("traceback") }));
            make_generic_class("IO", { obj("Generic")[obj("AnyStr")] }, io, typing_docs::doc("IO"));
            make_generic_class("BinaryIO", { cls("IO")[builtin("bytes")] }, { { "__slots__", py_tuple() },
                { "write", abstract_method("BinaryIO", "write", { py_positional("s") }) }, { "__enter__", abstract_method("BinaryIO", "__enter__") } },
                typing_docs::doc("BinaryIO"));
            namespace_type text{ { "__slots__", py_tuple() } };
            for (auto p : { "buffer", "encoding", "errors", "line_buffering", "newlines" })
                text.emplace_back(p, abstract_property("TextIO", p));
            text.emplace_back("__enter__", abstract_method("TextIO", "__enter__"));
            make_generic_class("TextIO", { cls("IO")[builtin("str")] }, text, typing_docs::doc("TextIO"));
            for (auto n : { "IO", "BinaryIO", "TextIO" })
                define(n, cls(n));

            // (_IdentityCallable and _Func)
            make_typing_class("_IdentityCallable", { obj("Protocol") }, { { "__call__", method("_IdentityCallable", "__call__", { py_positional_only("arg") },
                [](std::vector<py> const&) { return ellipsis(); }) } });
            {
                py const func = cls("TypeAliasType")(py{ "_Func" }, obj("Callable")[py_tuple({ ellipsis(), obj("Any") })]);
                func.set_attr("__module__", py{ sModule });
                define("_Func", func);
            }
        }

        void make_functions()
        {
            auto& s = state();
            define("cast", module_function("cast", { py_positional("typ"), py_positional("val") }, [](std::vector<py> const& a) { return a[1]; }, typing_docs::doc("cast")));
            define("assert_type", module_function("assert_type", { py_positional_only("val"), py_positional_only("typ") }, [](std::vector<py> const& a) { return a[0]; },
                typing_docs::doc("assert_type")));
            define("get_type_hints", module_function("get_type_hints", { py_positional("obj"), py_positional("globalns", none()), py_positional("localns", none()),
                py_positional("include_extras", boolean(false)) }, [](std::vector<py> const& a) { return get_type_hints(a[0], a[1], a[2], a[3].truthy()); },
                typing_docs::doc("get_type_hints")));
            define("_strip_annotations", module_function("_strip_annotations", { py_positional("t") }, [](std::vector<py> const& a) { return strip_annotations(a[0]); }));
            define("get_origin", module_function("get_origin", { py_positional("tp") }, [](std::vector<py> const& a) { return get_origin(a[0]); }, typing_docs::doc("get_origin")));
            define("get_args", module_function("get_args", { py_positional("tp") }, [](std::vector<py> const& a) { return get_args(a[0]); }, typing_docs::doc("get_args")));
            define("is_typeddict", module_function("is_typeddict", { py_positional("tp") }, [](std::vector<py> const& a)
                { return boolean(isinstance(a[0], cls("_TypedDictMeta"))); }, typing_docs::doc("is_typeddict")));
            define("assert_never", module_function("assert_never", { py_positional_only("arg") }, [](std::vector<py> const& a) -> py
                {
                    std::string value = a[0].repr();
                    if (py{ value }.size() > 100)
                        value = slice(py{ value }, std::nullopt, 100).str() + "...";
                    py_raise("AssertionError", "Expected code to be unreachable, but got: " + value);
                }, typing_docs::doc("assert_never")));
            define("no_type_check", module_function("no_type_check", { py_positional("arg") }, [](std::vector<py> const& a) { return no_type_check(a[0]); },
                typing_docs::doc("no_type_check")));
            define("no_type_check_decorator", module_function("no_type_check_decorator", { py_positional("decorator") }, [](std::vector<py> const& a)
                {
                    py const deprecated = attr_or(py{ fn_import("warnings") }, "_deprecated", py{});
                    if (!deprecated.empty())
                        deprecated.call({ py{ "typing.no_type_check_decorator" } }, { { "remove", py_tuple({ py{ 3 }, py{ 15 } }) } });
                    py const decorator = a[0];
                    py const wrapped = function("wrapped_decorator", "no_type_check_decorator.<locals>.wrapped_decorator", [decorator](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        std::vector<object*> args;
                        for (auto const& v : aArgs)
                            args.push_back(v.reference());
                        return no_type_check(py{ invoke(decorator.get(), std::move(args), aKwargs) });
                    });
                    return import_attribute("functools", "wraps")(decorator)(wrapped);
                }, typing_docs::doc("no_type_check_decorator")));
            s.overloadRegistry = import_attribute("collections", "defaultdict")(import_attribute("functools", "partial")(import_attribute("collections", "defaultdict"),
                builtin("dict")));
            define("_overload_registry", s.overloadRegistry);
            define("_overload_dummy", module_function("_overload_dummy", {}, [](std::vector<py> const&) -> py
                {
                    py_raise("NotImplementedError", "You should not call an overloaded function. A series of @overload-decorated functions outside a stub module should "
                        "always be followed by an implementation that is not @overload-ed.");
                }, "Helper for @overload to raise when called.", true, true));
            define("overload", module_function("overload", { py_positional("func") }, [](std::vector<py> const& a)
                {
                    py const f = attr_or(a[0], "__func__", a[0]);
                    try
                    {
                        state().overloadRegistry[f.attr("__module__")][f.attr("__qualname__")].set_item(f.attr("__code__").attr("co_firstlineno"), a[0]);
                    }
                    catch (raised const&)
                    {
                        if (!caught({ "AttributeError" }))
                            throw;
                    }
                    return obj("_overload_dummy");
                }, typing_docs::doc("overload")));
            define("get_overloads", module_function("get_overloads", { py_positional("func") }, [](std::vector<py> const& a)
                {
                    py const f = attr_or(a[0], "__func__", a[0]);
                    auto const& registry = state().overloadRegistry;
                    if (!registry.contains(f.attr("__module__")))
                        return py_list();
                    py const modDict = registry[f.attr("__module__")];
                    if (!modDict.contains(f.attr("__qualname__")))
                        return py_list();
                    return builtin("list")(modDict[f.attr("__qualname__")].method("values"));
                }, typing_docs::doc("get_overloads")));
            define("clear_overloads", module_function("clear_overloads", {}, [](std::vector<py> const&)
                {
                    state().overloadRegistry.method("clear");
                    return none();
                }, typing_docs::doc("clear_overloads")));
            define("final", module_function("final", { py_positional("f") }, [](std::vector<py> const& a)
                {
                    try
                    {
                        a[0].set_attr("__final__", boolean(true));
                    }
                    catch (raised const&)
                    {
                        if (!caught({ "AttributeError", "TypeError" }))
                            throw;
                    }
                    return a[0];
                }, typing_docs::doc("final")));
            define("runtime_checkable", module_function("runtime_checkable", { py_positional("cls") }, [](std::vector<py> const& a) { return runtime_checkable(a[0]); },
                typing_docs::doc("runtime_checkable")));
            define("reveal_type", module_function("reveal_type", { py_positional_only("obj") }, [](std::vector<py> const& a)
                {
                    builtin("print").call({ py{ "Runtime type is " + type_of(a[0]).attr("__name__").repr() } }, { { "file", import_attribute("sys", "stderr") } });
                    return a[0];
                }, typing_docs::doc("reveal_type")));
            define("dataclass_transform", module_function("dataclass_transform", { py_keyword("eq_default", boolean(true)), py_keyword("order_default", boolean(false)),
                py_keyword("kw_only_default", boolean(false)), py_keyword("frozen_default", boolean(false)), py_keyword("field_specifiers", py_tuple()) },
                [](std::vector<py> const& a)
                {
                    py const transform = py_dict();
                    static char const* const sNames[] = { "eq_default", "order_default", "kw_only_default", "frozen_default", "field_specifiers", "kwargs" };
                    for (std::size_t i = 0u; i < 6u; ++i)
                        transform.set_item(py{ sNames[i] }, a[i]);
                    return function("decorator", "dataclass_transform.<locals>.decorator", [transform](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        auto const d = py_bind("dataclass_transform.<locals>.decorator", { py_positional("cls_or_fn") }, aArgs, aKwargs);
                        d[0].set_attr("__dataclass_transform__", transform);
                        return d[0];
                    });
                }, typing_docs::doc("dataclass_transform"), false, true));
            define("override", module_function("override", { py_positional_only("method") }, [](std::vector<py> const& a)
                {
                    try
                    {
                        a[0].set_attr("__override__", boolean(true));
                    }
                    catch (raised const&)
                    {
                        if (!caught({ "AttributeError", "TypeError" }))
                            throw;
                    }
                    return a[0];
                }, typing_docs::doc("override")));
            define("is_protocol", module_function("is_protocol", { py_positional_only("tp") }, [](std::vector<py> const& a)
                {
                    return boolean(is_type(a[0]) && attr_or(a[0], "_is_protocol", boolean(false)).truthy() && !(a[0] == obj("Protocol")));
                }, typing_docs::doc("is_protocol")));
            define("get_protocol_members", module_function("get_protocol_members", { py_positional_only("tp") }, [](std::vector<py> const& a) -> py
                {
                    if (!(is_type(a[0]) && attr_or(a[0], "_is_protocol", boolean(false)).truthy() && !(a[0] == obj("Protocol"))))
                        py_raise("TypeError", a[0].repr() + " is not a Protocol");
                    return builtin("frozenset")(a[0].attr("__protocol_attrs__"));
                }, typing_docs::doc("get_protocol_members")));
            // (the module's internal functions)
            define("_type_check", module_function("_type_check", { py_positional("arg"), py_positional("msg"), py_positional("is_argument", boolean(true)),
                py_positional("module", none()), py_keyword("allow_special_forms", boolean(false)) }, [](std::vector<py> const& a)
                {
                    return type_check(a[0], a[1].str(), a[2].truthy(), a[3], a[4].truthy());
                }));
            define("_type_convert", module_function("_type_convert", { py_positional("arg"), py_positional("module", none()), py_keyword("allow_special_forms", boolean(false)) },
                [](std::vector<py> const& a) { return type_convert(a[0], a[1], a[2].truthy()); }));
            define("_type_repr", module_function("_type_repr", { py_positional("obj") }, [](std::vector<py> const& a) { return py{ type_repr(a[0]) }; }));
            define("_collect_type_parameters", module_function("_collect_type_parameters", { py_positional("args"), py_keyword("enforce_default_ordering", boolean(true)) },
                [](std::vector<py> const& a) { return collect_type_parameters(a[0], a[1].truthy()); }));
            define("_collect_parameters", obj("_collect_type_parameters"));
            define("_eval_type", module_function("_eval_type", { py_positional("t"), py_positional("globalns"), py_positional("localns"), py_positional("type_params", s.sentinel),
                py_keyword("recursive_guard", builtin("frozenset")()) }, [](std::vector<py> const& a)
                {
                    py typeParams = a[3];
                    if (typeParams.is(state().sentinel))
                    {
                        import_attribute("warnings", "warn").call({ py{ "Failing to pass a value to the 'type_params' parameter of 'typing._eval_type' is deprecated, as it "
                            "leads to incorrect behaviour when calling typing._eval_type on a stringified annotation that references a PEP 695 type parameter. It will be "
                            "disallowed in Python 3.15." } }, { { "category", builtin("DeprecationWarning") }, { "stacklevel", py{ 1 } } });
                        typeParams = py_tuple();
                    }
                    return eval_type(a[0], a[1], a[2], typeParams, a[4]);
                }));
            define("_rewrite_star_unpack", module_function("_rewrite_star_unpack", { py_positional("arg") }, [](std::vector<py> const& a)
                { return py{ rewrite_star_unpack(a[0].str()) }; }));
            define("_is_unpacked_typevartuple", module_function("_is_unpacked_typevartuple", { py_positional("x") }, [](std::vector<py> const& a)
                { return boolean(is_unpacked_typevartuple(a[0])); }));
            define("_is_typevar_like", module_function("_is_typevar_like", { py_positional("x") }, [](std::vector<py> const& a) { return boolean(is_typevar_like(a[0])); }));
            define("_typevar_subst", module_function("_typevar_subst", { py_positional("self"), py_positional("arg") }, [](std::vector<py> const& a)
                { return typevar_subst(a[0], a[1]); }));
            define("_typevartuple_prepare_subst", module_function("_typevartuple_prepare_subst", { py_positional("self"), py_positional("alias"), py_positional("args") },
                [](std::vector<py> const& a) { return typevartuple_prepare_subst(a[0], a[1], a[2]); }));
            define("_paramspec_subst", module_function("_paramspec_subst", { py_positional("self"), py_positional("arg") }, [](std::vector<py> const& a)
                { return paramspec_subst(a[0], a[1]); }));
            define("_paramspec_prepare_subst", module_function("_paramspec_prepare_subst", { py_positional("self"), py_positional("alias"), py_positional("args") },
                [](std::vector<py> const& a) { return paramspec_prepare_subst(a[0], a[1], a[2]); }));
            define("_generic_class_getitem", module_function("_generic_class_getitem", { py_positional("cls"), py_positional("args") }, [](std::vector<py> const& a)
                { return generic_class_getitem(a[0], a[1]); }));
            define("_generic_init_subclass", module_function("_generic_init_subclass", { py_positional("cls") }, [](std::vector<py> const& a)
                {
                    generic_init_subclass(a[0], a[1], a[2]);
                    return none();
                }, "", true, true));
            define("_make_union", module_function("_make_union", { py_positional("left"), py_positional("right") }, [](std::vector<py> const& a) { return union_of(a[0], a[1]); }));
            define("_get_protocol_attrs", module_function("_get_protocol_attrs", { py_positional("cls") }, [](std::vector<py> const& a) { return get_protocol_attrs(a[0]); }));
            define("_caller", module_function("_caller", { py_positional("depth", py{ 1 }), py_positional("default", py{ "__main__" }) }, [](std::vector<py> const& a)
                { return caller(a[1]); }));
            define("_make_nmtuple", module_function("_make_nmtuple", { py_positional("name"), py_positional("types"), py_positional("module"), py_positional("defaults", py_tuple()) },
                [](std::vector<py> const& a) { return make_nmtuple(a[0], a[1], a[2], a[3]); }));
        }

        bool typing_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = state();
                if (!s.made)
                {
                    s.made = true;
                    s.cleanups = py_list();
                    make_typing_core_classes();
                    make_special_forms();
                    make_protocol_classes();
                    make_other_classes();
                    make_functions();
                    make_aliases();
                    define("Text", builtin("str"));
                    define("TYPE_CHECKING", boolean(false));
                    define("_cleanups", s.cleanups);
                    define("_caches", py_dict());
                    std::vector<py> excluded;
                    for (auto const& n : sTypingInternals)
                        excluded.emplace_back(n);
                    py const typingInternals = builtin("frozenset")(py_list(excluded));
                    excluded.clear();
                    for (auto const& n : sSpecialNames)
                        excluded.emplace_back(n);
                    py const specialNames = builtin("frozenset")(py_list(excluded));
                    define("_TYPING_INTERNALS", typingInternals);
                    define("_SPECIAL_NAMES", specialNames);
                    define("EXCLUDED_ATTRIBUTES", typingInternals | specialNames | builtin("frozenset")(py_list({ py{ "_MutableMapping__marker" } })));
                    define("_PROTO_ALLOWLIST", proto_allowlist());
                    define("_ASSERT_NEVER_REPR_MAX_LENGTH", py{ 100 });
                    define("_alias", cls("_SpecialGenericAlias"));
                }
                for (auto const& [name, c] : s.classes)
                    if (!s.objects.contains(name))
                        aAttributes.emplace_back(name, c.reference());
                for (auto const& name : s.order)
                    aAttributes.emplace_back(name, s.objects.at(name).reference());
                static char const* const sAll[] = { "Annotated", "Any", "Callable", "ClassVar", "Concatenate", "Final", "ForwardRef", "Generic", "Literal", "Optional",
                    "ParamSpec", "Protocol", "Tuple", "Type", "TypeVar", "TypeVarTuple", "Union", "AbstractSet", "ByteString", "Container", "ContextManager", "Hashable",
                    "ItemsView", "Iterable", "Iterator", "KeysView", "Mapping", "MappingView", "MutableMapping", "MutableSequence", "MutableSet", "Sequence", "Sized",
                    "ValuesView", "Awaitable", "AsyncIterator", "AsyncIterable", "Coroutine", "Collection", "AsyncGenerator", "AsyncContextManager", "Reversible",
                    "SupportsAbs", "SupportsBytes", "SupportsComplex", "SupportsFloat", "SupportsIndex", "SupportsInt", "SupportsRound", "ChainMap", "Counter", "Deque",
                    "Dict", "DefaultDict", "List", "OrderedDict", "Set", "FrozenSet", "NamedTuple", "TypedDict", "Generator", "BinaryIO", "IO", "Match", "Pattern",
                    "TextIO", "AnyStr", "assert_type", "assert_never", "cast", "clear_overloads", "dataclass_transform", "final", "get_args", "get_origin",
                    "get_overloads", "get_protocol_members", "get_type_hints", "is_protocol", "is_typeddict", "LiteralString", "Never", "NewType", "no_type_check",
                    "no_type_check_decorator", "NoDefault", "NoReturn", "NotRequired", "overload", "override", "ParamSpecArgs", "ParamSpecKwargs", "ReadOnly",
                    "Required", "reveal_type", "runtime_checkable", "Self", "Text", "TYPE_CHECKING", "TypeAlias", "TypeGuard", "TypeIs", "TypeAliasType", "Unpack" };
                std::vector<py> all;
                for (auto n : sAll)
                    all.emplace_back(n);
                aAttributes.emplace_back("__all__", py_list(all).release());
                aAttributes.emplace_back("__doc__", make_str("\nThe typing module: Support for gradual typing as defined by PEP 484 and subsequent PEPs.\n\n"
                    "Among other things, the module includes the following:\n* Generic, Protocol, and internal machinery to support generic aliases.\n"
                    "  All subscripted types like X[int], Union[int, str] are generic aliases.\n* Various \"special forms\" that have unique meanings in type annotations:\n"
                    "  Any, Never, ClassVar, Self, Concatenate, Unpack, and others.\n* Classes whose instances can be type arguments to generic classes and functions:\n"
                    "  TypeVar, ParamSpec, TypeVarTuple.\n* Public helper functions: get_type_hints, overload, cast, final, and others.\n"
                    "* Several protocols to support duck-typing:\n  SupportsFloat, SupportsIndex, SupportsAbs, and others.\n* Special types: NewType, NamedTuple, TypedDict.\n"
                    "* Deprecated aliases for builtin types and collections.abc ABCs.\n\nAny name not present in __all__ is an implementation detail\n"
                    "that may be changed without notice. Use at your own risk!\n"));
                for (auto module : { "collections", "copyreg", "functools", "operator", "sys", "types" })
                    aAttributes.emplace_back(module, py{ fn_import(module) }.release());
                for (auto const& [name, module, attribute] : { std::tuple{ "abstractmethod", "abc", "abstractmethod" }, std::tuple{ "ABCMeta", "abc", "ABCMeta" },
                    std::tuple{ "defaultdict", "collections", "defaultdict" }, std::tuple{ "WrapperDescriptorType", "types", "WrapperDescriptorType" },
                    std::tuple{ "MethodWrapperType", "types", "MethodWrapperType" }, std::tuple{ "MethodDescriptorType", "types", "MethodDescriptorType" },
                    std::tuple{ "GenericAlias", "types", "GenericAlias" } })
                    aAttributes.emplace_back(name, import_attribute(module, attribute).release());
                return none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sTyping = register_module("typing", typing_module);
    }

    // --- type parameters (PEP 695: TypeVar, TypeVarTuple, ParamSpec) and type aliases (TypeAliasType): typing's

    // __type_param(name, kind) (kind 0: TypeVar, 1: TypeVarTuple, 2: ParamSpec)
    object* b_type_param(std::vector<object*> const& aArgs, object*)
    {
        if (aArgs.size() != 2u || kind_of(aArgs[0]) != NEOS_PY_STR)
            return error("SystemError", "__type_param(name, kind)");
        return py_guarded([&]()
        {
            py{ fn_import("typing") };
            py const name = py::borrowed(aArgs[0]);
            long long const kind = py::borrowed(aArgs[1]).as_index();
            py const parameter = kind == 1 ? cls("TypeVarTuple")(name) : cls(kind == 2 ? "ParamSpec" : "TypeVar").call({ name }, { { "infer_variance", boolean(true) } });
            parameter.set_attr("__module__", py{ "typing" });
            return parameter;
        });
    }

    // (an instance of typing.TypeAliasType (an operand of types.UnionType's unions))
    bool is_type_alias_type(object* aObject)
    {
        auto const& classes = state().classes;
        auto const c = classes.find("TypeAliasType");
        return c != classes.end() && user_class_of(aObject) == c->second.get();
    }

    // (types.GenericAlias's alias[item]: its new arguments (CPython's _Py_subs_parameters))
    object* types_generic_alias_subs(object* aAlias, object* aItem)
    {
        return py_guarded([&]()
        {
            py{ fn_import("typing") };
            py const self = py::borrowed(aAlias);
            py const params = self.attr("__parameters__");
            if (!params.truthy())
                py_raise("TypeError", self.repr() + " is not a generic class");
            py args = py_tuple(unpack_args(tuple_of(py::borrowed(aItem)).items()));
            for (auto const& param : params.items())
            {
                py const prepare = attr_or(param, "__typing_prepare_subst__", py::none());
                if (!prepare.is_none())
                    args = prepare(self, args);
            }
            auto const alen = args.size();
            auto const plen = params.size();
            if (alen != plen)
                py_raise("TypeError", std::string{ "Too " } + (alen > plen ? "many" : "few") + " arguments for " + self.repr() + "; actual " + std::to_string(alen) +
                    ", expected " + std::to_string(plen));
            py const newArgByParam = builtin("dict")(builtin("zip")(params, args));
            return builtin("tuple")(generic_alias_make_substitution(self, self.attr("__args__"), newArgByParam));
        });
    }

    // __generic_base(parameters): class C[T, ...](bases)'s last positional base: Generic[T, ...] (a TypeVarTuple unpacked) (whose 
    // __type_params__ are the class's)
    object* b_generic_base(std::vector<object*> const& aArgs, object*)
    {
        if (aArgs.size() != 1u || kind_of(aArgs[0]) != NEOS_PY_TUPLE)
            return error("SystemError", "__generic_base(parameters)");
        return py_guarded([&]()
        {
            py{ fn_import("typing") };
            py const parameters = py::borrowed(aArgs[0]);
            std::vector<py> arguments;
            for (auto const& p : parameters.items())
                arguments.push_back(is_instance_of(p, "TypeVarTuple") ? obj("Unpack")[p] : p);
            py const base = obj("Generic")[py_tuple(arguments)];
            base.set_attr("__type_params__", parameters);
            return base;
        });
    }

    // __generic_function(parameters, function): def f[T, ...](...)'s function (its __type_params__)
    object* b_generic_function(std::vector<object*> const& aArgs, object*)
    {
        if (aArgs.size() != 2u || kind_of(aArgs[0]) != NEOS_PY_TUPLE)
            return error("SystemError", "__generic_function(parameters, function)");
        return py_guarded([&]()
        {
            py const function = py::borrowed(aArgs[1]);
            function.set_attr("__type_params__", py::borrowed(aArgs[0]));
            return function;
        });
    }

    // __type_alias(name, maker, parameters) (its value: maker(*parameters), when it is first read)
    object* b_type_alias(std::vector<object*> const& aArgs, object*)
    {
        if (aArgs.size() != 3u || kind_of(aArgs[0]) != NEOS_PY_STR || kind_of(aArgs[2]) != NEOS_PY_TUPLE)
            return error("SystemError", "__type_alias(name, maker, parameters)");
        return py_guarded([&]()
        {
            py{ fn_import("typing") };
            py const alias = cls("TypeAliasType").call({ py::borrowed(aArgs[0]), py::none() }, { { "type_params", py::borrowed(aArgs[2]) } });
            alias.set_attr("_evaluator", py::borrowed(aArgs[1]));
            return alias;
        });
    }
}
