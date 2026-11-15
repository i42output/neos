/*
  functools.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/functools.py and Modules/_functoolsmodule.c (partial's
  __reduce__ and __setstate__)), Copyright (c) 2001-2024 Python Software Foundation; All Rights Reserved. They are used
  under the Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF). Changes: translated from
  Python and C to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module functools (CPython 3.13's behaviour): reduce, partial, lru_cache, cache, wraps, update_wrapper, total_ordering,
// cmp_to_key, cached_property and singledispatch

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "objects.hpp"
#include "values.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            object* call_with(object* aCallable, std::vector<object*> aArgs, object* aKwargs = nullptr)    // (steals the arguments)
            {
                ref const args{ make_tuple(std::move(aArgs)) };
                return call(aCallable, args.get(), aKwargs);
            }

            object* borrowed(object* aObject) { retain(aObject); return aObject; }

            // --- partial

            struct partial_object
            {
                header h;
                object* function;
                object* args;       ///< (a tuple)
                object* keywords;   ///< (a dict)
                std::vector<std::pair<std::string, object*>> attributes;
                ~partial_object() { release(function); release(args); release(keywords); for (auto& a : attributes) release(a.second); }
            };

            neos_py_type make_partial_type();
            neos_py_type sPartialType = make_partial_type();
            partial_object& as_partial(object* aObject) { return *reinterpret_cast<partial_object*>(aObject); }
            bool is_partial(object* aObject) { return aObject != nullptr && aObject->type == &sPartialType.base; }

            object* make_partial(object* aFunction, std::vector<object*> const& aArgs, object* aKeywords)
            {
                std::vector<object*> args;
                ref keywords{ make_dict() };
                object* function = aFunction;
                // (a partial (or a subclass's instance) that has no __dict__ of its own: flattened (as CPython's partial_new))
                auto const inner_partial = [](object* aInner) -> partial_object*
                {
                    auto const value = builtin_value(aInner);
                    if (!is_partial(value))
                        return nullptr;
                    if (!is_instance(aInner))
                        return as_partial(value).attributes.empty() ? &as_partial(value) : nullptr;
                    ref const d{ getattr(aInner, "__dict__") };
                    clear_error();
                    return d && kind_of(d.get()) == NEOS_PY_DICT && as<dict_object>(d.get())->items.empty() ? &as_partial(value) : nullptr;
                };
                if (auto const innerPartial = inner_partial(aFunction))
                {
                    auto const& inner = *innerPartial;
                    function = inner.function;
                    for (auto a : as<tuple_object>(inner.args)->items)
                        args.push_back(borrowed(a));
                    dict_update(keywords.get(), inner.keywords);
                }
                for (auto a : aArgs)
                    args.push_back(borrowed(a));
                if (aKeywords != nullptr)
                    dict_update(keywords.get(), aKeywords);
                auto const p = new partial_object{ { { &sPartialType.base } }, borrowed(function), make_tuple(std::move(args)), keywords.release_ownership() };
                return &p->h.base;
            }

            // (a partial's, or the value of an instance of a subclass of partial (null: neither))
            partial_object* partial_of(object* aSelf)
            {
                auto const value = builtin_value(aSelf);
                return is_partial(value) ? &as_partial(value) : nullptr;
            }

            object* not_a_partial(char const* aMethod, object* aSelf)
            {
                return error("TypeError", std::string{ "descriptor '" } + aMethod + "' for 'functools.partial' objects doesn't apply to a '" + type_name(aSelf) + "' object");
            }

            // (pickling (CPython's partial_reduce and partial_setstate): (type(self), (func,), (func, args, keywords, __dict__ or None)); a 
            // subclass's instance's __dict__ is the instance's)
            builtin_entry const sPartialReduce{ "__reduce__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    auto const p = aArgs.empty() ? nullptr : partial_of(aArgs[0]);
                    if (p == nullptr)
                        return aArgs.empty() ? error("TypeError", "descriptor '__reduce__' of 'functools.partial' object needs an argument") : not_a_partial("__reduce__", aArgs[0]);
                    ref const cls{ class_of(aArgs[0]) };
                    ref dict;
                    if (is_instance(aArgs[0]))
                    {
                        ref const d{ getattr(aArgs[0], "__dict__") };
                        if (!d)
                            return nullptr;
                        dict = ref{ borrowed(kind_of(d.get()) == NEOS_PY_DICT && !as<dict_object>(d.get())->items.empty() ? d.get() : none()) };
                    }
                    else if (p->attributes.empty())
                        dict = ref{ borrowed(none()) };
                    else
                    {
                        dict = ref{ make_dict() };
                        for (auto const& a : p->attributes)
                        {
                            ref const key{ make_str(a.first) };
                            dict_set(dict.get(), key.get(), a.second);
                        }
                    }
                    return make_tuple({ borrowed(cls.get()), make_tuple({ borrowed(p->function) }), make_tuple({ borrowed(p->function), borrowed(p->args),
                        borrowed(p->keywords), dict.release_ownership() }) });
                } };
            builtin_entry const sPartialReduceEx{ "__reduce_ex__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    if (aArgs.size() != 2u)
                        return error("TypeError", "__reduce_ex__() takes exactly one argument (" + std::to_string(aArgs.empty() ? 0u : aArgs.size() - 1u) + " given)");
                    return sPartialReduce.function({ aArgs[0] }, nullptr);
                } };
            builtin_entry const sPartialSetstate{ "__setstate__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    if (aArgs.size() != 2u)
                        return error("TypeError", "__setstate__() takes exactly one argument (" + std::to_string(aArgs.empty() ? 0u : aArgs.size() - 1u) + " given)");
                    auto const pp = partial_of(aArgs[0]);
                    if (pp == nullptr)
                        return not_a_partial("__setstate__", aArgs[0]);
                    auto& p = *pp;
                    auto const state = aArgs[1];
                    if (kind_of(builtin_value(state)) != NEOS_PY_TUPLE)
                        return error("TypeError", "invalid partial state");
                    // (a tuple of 4 (callable, tuple, dict or None, dict or None), else "invalid partial state")
                    auto const& items = as<tuple_object>(builtin_value(state))->items;
                    if (items.size() != 4u)
                        return error("TypeError", "invalid partial state");
                    auto const fn = items[0];
                    auto const fnargs = items[1];
                    auto const kw = items[2];
                    auto const dict = items[3];
                    ref const callable{ call_with(fn_builtin("callable"), { borrowed(fn) }) };
                    if (!callable || truth(callable.get()) != 1 || kind_of(builtin_value(fnargs)) != NEOS_PY_TUPLE ||
                        (kw != none() && kind_of(builtin_value(kw)) != NEOS_PY_DICT) || (dict != none() && kind_of(builtin_value(dict)) != NEOS_PY_DICT))
                        return error("TypeError", "invalid partial state");
                    // (a tuple and a dict (not of subclasses): themselves; else copies)
                    ref args;
                    if (kind_of(fnargs) == NEOS_PY_TUPLE)
                        args = ref{ borrowed(fnargs) };
                    else
                    {
                        std::vector<object*> copied;
                        for (auto a : as<tuple_object>(builtin_value(fnargs))->items)
                            copied.push_back(borrowed(a));
                        args = ref{ make_tuple(std::move(copied)) };
                    }
                    ref keywords;
                    if (kind_of(kw) == NEOS_PY_DICT)
                        keywords = ref{ borrowed(kw) };
                    else
                    {
                        keywords = ref{ make_dict() };
                        if (kw != none())
                            dict_update(keywords.get(), kw);
                    }
                    release(p.function);
                    release(p.args);
                    release(p.keywords);
                    p.function = borrowed(fn);
                    p.args = args.release_ownership();
                    p.keywords = keywords.release_ownership();
                    // (its __dict__: the state's (a subclass's instance's: the instance's))
                    if (is_instance(aArgs[0]))
                    {
                        ref const d{ getattr(aArgs[0], "__dict__") };
                        if (!d)
                            return nullptr;
                        ref const cleared{ call_method_named(d.get(), "clear", {}) };
                        if (!cleared || (dict != none() && !dict_update(d.get(), dict)))
                            return nullptr;
                        return borrowed(none());
                    }
                    for (auto& a : p.attributes)
                        release(a.second);
                    p.attributes.clear();
                    if (dict != none() && kind_of(builtin_value(dict)) == NEOS_PY_DICT)
                        for (auto const& item : as<dict_object>(builtin_value(dict))->items)
                            if (kind_of(item.first) == NEOS_PY_STR)
                                p.attributes.emplace_back(as<str_object>(item.first)->value, borrowed(item.second));
                    return borrowed(none());
                } };
            // (as a class's attribute: itself, and of an instance a FutureWarning (CPython 3.13's partial_descr_get))
            builtin_entry const sPartialGet{ "__get__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    if (aArgs.size() < 2u || aArgs.size() > 3u)
                        return error("TypeError", "expected 1 or 2 arguments, got " + std::to_string(aArgs.empty() ? 0u : aArgs.size() - 1u));
                    if (aArgs[1] != none())
                    {
                        ref const warn{ module_attribute("warnings", "warn") };
                        if (!warn || !ref{ invoke(warn.get(), { make_str("functools.partial will be a method descriptor in future Python versions; wrap it in "
                            "staticmethod() if you want to preserve the old behavior"), exception_class("FutureWarning"), make_int(1) }) })
                            return nullptr;
                    }
                    return borrowed(aArgs[0]);
                } };

            // (the type's: its pickling methods and __get__ (unbound))
            object* partial_type_attribute(char const* aName)
            {
                for (auto e : { &sPartialReduce, &sPartialReduceEx, &sPartialSetstate, &sPartialGet })
                    if (std::strcmp(aName, e->name) == 0)
                        return make_function(e->name, call_builtin, const_cast<builtin_entry*>(e));
                return nullptr;
            }

            neos_py_type make_partial_type()
            {
                auto type = make_type("functools.partial", "partial", retain_object, release_object<partial_object>);
                type.call = [](neos_py_context*, object* aSelf, object* aArgs, object* aKwargs) -> object*
                {
                    auto const& p = as_partial(aSelf);
                    std::vector<object*> args;
                    for (auto a : as<tuple_object>(p.args)->items)
                        args.push_back(borrowed(a));
                    for (auto a : as<tuple_object>(aArgs)->items)
                        args.push_back(borrowed(a));
                    ref keywords{ make_dict() };
                    dict_update(keywords.get(), p.keywords);
                    if (aKwargs != nullptr)
                        dict_update(keywords.get(), aKwargs);
                    return call_with(p.function, std::move(args), as<dict_object>(keywords.get())->items.empty() ? nullptr : keywords.get());
                };
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    // (one that contains itself: "..." within its own repr (as CPython's))
                    if (repr_enter(aSelf))
                        return make_str("...");
                    auto const& p = as_partial(aSelf);
                    std::string text;
                    // (its items held: their __repr__ and __str__ can change its args and keywords (as CPython's partial_repr))
                    ref const function{ borrowed(p.function) };
                    ref const args{ borrowed(p.args) };
                    std::vector<std::pair<ref, ref>> keywords;
                    for (auto const& item : as<dict_object>(p.keywords)->items)
                        keywords.emplace_back(ref{ borrowed(item.first) }, ref{ borrowed(item.second) });
                    bool const made = [&]()
                    {
                        if (!repr_of(function.get(), text))
                            return false;
                        for (auto a : as<tuple_object>(args.get())->items)
                        {
                            std::string item;
                            if (!repr_of(a, item))
                                return false;
                            text += ", " + item;
                        }
                        for (auto const& item : keywords)
                        {
                            std::string key;
                            std::string value;
                            if (!str_of(item.first.get(), key) || !repr_of(item.second.get(), value))
                                return false;
                            text += ", " + key + "=" + value;
                        }
                        return true;
                    }();
                    repr_leave(aSelf);
                    if (!made)
                        return nullptr;
                    return make_str("functools.partial(" + text + ")");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& p = as_partial(aSelf);
                    if (std::strcmp(aName, "func") == 0)
                        return borrowed(p.function);
                    if (std::strcmp(aName, "args") == 0)
                        return borrowed(p.args);
                    if (std::strcmp(aName, "keywords") == 0)
                        return borrowed(p.keywords);
                    for (auto const& a : p.attributes)
                        if (a.first == aName)
                            return borrowed(a.second);
                    if (std::strcmp(aName, "__dict__") == 0)
                    {
                        ref d{ make_dict() };
                        for (auto const& a : p.attributes)
                        {
                            ref const key{ make_str(a.first) };
                            dict_set(d.get(), key.get(), a.second);
                        }
                        return d.release_ownership();
                    }
                    for (auto e : { &sPartialReduce, &sPartialReduceEx, &sPartialSetstate })
                        if (std::strcmp(aName, e->name) == 0)
                            return make_function(e->name, call_builtin, const_cast<builtin_entry*>(e), aSelf);
                    return error("AttributeError", std::string{ "'functools.partial' object has no attribute '" } + aName + "'");
                };
                type.setattr = [](neos_py_context*, object* aSelf, char const* aName, object* aValue) -> int
                {
                    auto& p = as_partial(aSelf);
                    // (its __dict__: not deleted (CPython's PyObject_GenericSetDict))
                    if (std::strcmp(aName, "__dict__") == 0 && aValue == nullptr)
                    {
                        error("TypeError", "cannot delete __dict__");
                        return -1;
                    }
                    if (std::strcmp(aName, "func") == 0 || std::strcmp(aName, "args") == 0 || std::strcmp(aName, "keywords") == 0)
                    {
                        error("AttributeError", "readonly attribute");
                        return -1;
                    }
                    for (auto a = p.attributes.begin(); a != p.attributes.end(); ++a)
                        if (a->first == aName)
                        {
                            // (del: the attribute removed)
                            if (aValue == nullptr)
                            {
                                release(a->second);
                                p.attributes.erase(a);
                                return 0;
                            }
                            retain(aValue);
                            release(a->second);
                            a->second = aValue;
                            return 0;
                        }
                    if (aValue == nullptr)
                    {
                        error("AttributeError", std::string{ "'functools.partial' object has no attribute '" } + aName + "'");
                        return -1;
                    }
                    p.attributes.emplace_back(aName, borrowed(aValue));
                    return 0;
                };
                return type;
            }

            object* new_partial(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                auto const& args = as<tuple_object>(aArgs)->items;
                if (args.empty())
                    return error("TypeError", "type 'partial' takes at least one argument");
                ref const callable{ call_with(fn_builtin("callable"), { borrowed(args[0]) }) };
                if (!callable || truth(callable.get()) != 1)
                    return error("TypeError", "the first argument must be callable");
                return make_partial(args[0], std::vector<object*>{ args.begin() + 1, args.end() }, aKwargs);
            }

            int partial_instancecheck(neos_py_context*, void*, object* aObject) { return is_partial(aObject) ? 1 : 0; }

            // --- lru_cache

            struct lru_object
            {
                header h;
                object* function;
                object* cache;          ///< (a dict: the keys in order of use)
                long long maxsize;      ///< (-1: unbounded)
                bool typed;
                long long hits = 0;
                long long misses = 0;
                std::vector<std::pair<std::string, object*>> attributes;
                ~lru_object() { release(function); release(cache); for (auto& a : attributes) release(a.second); }
            };

            neos_py_type make_lru_type();
            neos_py_type sLruType = make_lru_type();
            lru_object& as_lru(object* aObject) { return *reinterpret_cast<lru_object*>(aObject); }

            object* sKeywordMark = nullptr;     // (separates the positional and keyword arguments in a key)

            // (Lib/functools.py's _HashedSeq: a key's tuple with its hash, found once (so that an argument's __hash__ is called once for each use as 
            // an argument to the cached function, however often the cache looks the key up))
            struct hashed_key
            {
                header h;
                object* key;    ///< (a tuple)
                long long hash;
                ~hashed_key() { release(key); }
            };

            neos_py_type make_hashed_key_type();
            neos_py_type sHashedKeyType = make_hashed_key_type();

            neos_py_type make_hashed_key_type()
            {
                auto type = make_type("functools._HashedSeq", "_HashedSeq", retain_object, release_object<hashed_key>);
                type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int
                {
                    *aResult = reinterpret_cast<hashed_key*>(aSelf)->hash;
                    return 0;
                };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    if (aLhs->type != &sHashedKeyType.base || aRhs->type != &sHashedKeyType.base)
                        return borrowed(not_implemented());
                    return compare(aOperation, reinterpret_cast<hashed_key*>(aLhs)->key, reinterpret_cast<hashed_key*>(aRhs)->key);
                };
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    std::string text;
                    return repr_of(reinterpret_cast<hashed_key*>(aSelf)->key, text) ? make_str(text) : nullptr;
                };
                return type;
            }

            // (Lib/functools.py's _make_key: one argument of an int or a str (not typed, no keywords): itself; else the arguments' tuple, hashed 
            // once)
            object* cache_key(lru_object const& aLru, object* aArgs, object* aKwargs)
            {
                if (auto const& args = as<tuple_object>(aArgs)->items; !aLru.typed && args.size() == 1u && (aKwargs == nullptr || as<dict_object>(aKwargs)->items.empty()) &&
                    (kind_of(args[0]) == NEOS_PY_INT || kind_of(args[0]) == NEOS_PY_STR))
                    return borrowed(args[0]);
                if (sKeywordMark == nullptr)
                    sKeywordMark = make_tuple({ make_str("functools._kwd_mark") });
                std::vector<object*> key;
                for (auto a : as<tuple_object>(aArgs)->items)
                    key.push_back(borrowed(a));
                if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                {
                    key.push_back(borrowed(sKeywordMark));
                    for (auto const& item : as<dict_object>(aKwargs)->items)
                    {
                        key.push_back(borrowed(item.first));
                        key.push_back(borrowed(item.second));
                    }
                }
                if (aLru.typed)
                {
                    for (auto a : as<tuple_object>(aArgs)->items)
                        key.push_back(class_of(a));
                    if (aKwargs != nullptr)
                        for (auto const& item : as<dict_object>(aKwargs)->items)
                            key.push_back(class_of(item.second));
                }
                ref const tuple{ make_tuple(std::move(key)) };
                long long hash = 0;
                if (!hash_of(tuple.get(), hash))
                    return nullptr;
                return &(new hashed_key{ { { &sHashedKeyType.base } }, borrowed(tuple.get()), hash })->h.base;
            }

            object* cache_info_class()
            {
                static object* sClass = nullptr;
                if (sClass == nullptr)
                {
                    ref const collections{ fn_import("collections") };
                    ref const namedtuple{ collections ? getattr(collections.get(), "namedtuple") : nullptr };
                    if (!namedtuple)
                        return nullptr;
                    ref const kwargs{ make_dict() };
                    ref const moduleKey{ make_str("module") };
                    ref const moduleName{ make_str("functools") };
                    dict_set(kwargs.get(), moduleKey.get(), moduleName.get());
                    std::vector<object*> fields{ make_str("hits"), make_str("misses"), make_str("maxsize"), make_str("currsize") };
                    sClass = call_with(namedtuple.get(), { make_str("CacheInfo"), make_list(std::move(fields)) }, kwargs.get());
                    if (sClass == nullptr)
                        return nullptr;
                }
                return borrowed(sClass);
            }

            object* lru_method(neos_py_context*, void* aData, object* aArgs, object*)
            {
                auto& lru = as_lru(as<tuple_object>(aArgs)->items.at(0));
                if (aData == nullptr)
                {
                    ref const cls{ cache_info_class() };
                    if (!cls)
                        return nullptr;
                    return call_with(cls.get(), { make_int(lru.hits), make_int(lru.misses), lru.maxsize < 0 ? borrowed(none()) : make_int(lru.maxsize),
                        make_int(static_cast<long long>(as<dict_object>(lru.cache)->items.size())) });
                }
                // (cache_clear)
                auto const old = lru.cache;
                lru.cache = make_dict();
                release(old);
                lru.hits = 0;
                lru.misses = 0;
                return borrowed(none());
            }

            neos_py_type make_lru_type()
            {
                auto type = make_type("functools._lru_cache_wrapper", "_lru_cache_wrapper", retain_object, release_object<lru_object>);
                type.call = [](neos_py_context*, object* aSelf, object* aArgs, object* aKwargs) -> object*
                {
                    auto& lru = as_lru(aSelf);
                    if (lru.maxsize == 0)
                    {
                        ++lru.misses;
                        return call(lru.function, aArgs, aKwargs);
                    }
                    ref const key{ cache_key(lru, aArgs, aKwargs) };
                    if (!key)
                        return nullptr;
                    auto const position = dict_find(lru.cache, key.get());
                    if (position == -2)
                        return nullptr;
                    auto& d = *as<dict_object>(lru.cache);
                    if (position >= 0)
                    {
                        ++lru.hits;
                        ref value{ borrowed(d.items[static_cast<std::size_t>(position)].second) };
                        if (lru.maxsize > 0)
                        {
                            // (the most recently used: the last)
                            ref const k{ borrowed(d.items[static_cast<std::size_t>(position)].first) };
                            dict_erase(d, static_cast<std::size_t>(position));
                            dict_set(lru.cache, k.get(), value.get());
                        }
                        return value.release_ownership();
                    }
                    ++lru.misses;
                    ref value{ call(lru.function, aArgs, aKwargs) };
                    if (!value)
                        return nullptr;
                    // (a recursive call may have cached it)
                    if (dict_find(lru.cache, key.get()) < 0)
                    {
                        dict_set(lru.cache, key.get(), value.get());
                        if (lru.maxsize > 0 && static_cast<long long>(as<dict_object>(lru.cache)->items.size()) > lru.maxsize)
                            dict_erase(*as<dict_object>(lru.cache), 0u);
                    }
                    return value.release_ownership();
                };
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    std::string text;
                    if (!repr_of(as_lru(aSelf).function, text))
                        return nullptr;
                    return make_str("<functools._lru_cache_wrapper object at " + object_address(aSelf) + ">");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto& lru = as_lru(aSelf);
                    // (their docstrings: CPython's (Modules/clinic/_functoolsmodule.c.h), with their text signatures)
                    if (std::strcmp(aName, "cache_info") == 0 || std::strcmp(aName, "cache_clear") == 0)
                    {
                        bool const info = aName[6] == 'i';
                        auto const method = make_function(aName, lru_method, info ? nullptr : reinterpret_cast<void*>(1), aSelf);
                        as<function_object>(method)->doc = info ? "cache_info($self, /)\n--\n\nReport cache statistics" : 
                            "cache_clear($self, /)\n--\n\nClear the cache and cache statistics";
                        return method;
                    }
                    if (std::strcmp(aName, "__wrapped__") == 0)
                        return borrowed(lru.function);
                    // (pickling: by its qualified name (CPython's lru_cache_reduce); copying: itself (lru_cache_copy and lru_cache_deepcopy))
                    if (std::strcmp(aName, "__reduce__") == 0 || std::strcmp(aName, "__reduce_ex__") == 0)
                        return make_function(aName, [](neos_py_context*, void*, object* aArgs, object*) -> object*
                        {
                            return getattr(as<tuple_object>(aArgs)->items.at(0), "__qualname__");
                        }, nullptr, aSelf);
                    if (std::strcmp(aName, "__copy__") == 0 || std::strcmp(aName, "__deepcopy__") == 0)
                        return make_function(aName, [](neos_py_context*, void*, object* aArgs, object*) -> object*
                        {
                            return borrowed(as<tuple_object>(aArgs)->items.at(0));
                        }, nullptr, aSelf);
                    if (std::strcmp(aName, "cache_parameters") == 0)
                        return make_function(aName, [](neos_py_context*, void*, object* aArgs, object*) -> object*
                        {
                            auto const& l = as_lru(as<tuple_object>(aArgs)->items.at(0));
                            ref d{ make_dict() };
                            ref const maxsizeKey{ make_str("maxsize") };
                            ref const typedKey{ make_str("typed") };
                            ref const maxsize{ l.maxsize < 0 ? borrowed(none()) : make_int(l.maxsize) };
                            dict_set(d.get(), maxsizeKey.get(), maxsize.get());
                            dict_set(d.get(), typedKey.get(), make_bool(l.typed));
                            return d.release_ownership();
                        }, nullptr, aSelf);
                    for (auto const& a : lru.attributes)
                        if (a.first == aName)
                            return borrowed(a.second);
                    return getattr(lru.function, aName);
                };
                type.setattr = [](neos_py_context*, object* aSelf, char const* aName, object* aValue) -> int
                {
                    auto& lru = as_lru(aSelf);
                    for (auto& a : lru.attributes)
                        if (a.first == aName)
                        {
                            retain(aValue);
                            release(a.second);
                            a.second = aValue;
                            return 0;
                        }
                    lru.attributes.emplace_back(aName, borrowed(aValue));
                    return 0;
                };
                method_types().insert(&type.base);
                return type;
            }

            object* make_lru(object* aFunction, long long aMaxsize, bool aTyped)
            {
                auto const lru = new lru_object{ { { &sLruType.base } }, borrowed(aFunction), make_dict(), aMaxsize, aTyped };
                return &lru->h.base;
            }

            // (the lru_cache(maxsize, typed) decorator)
            struct lru_parameters { long long maxsize; bool typed; };

            object* lru_decorator(neos_py_context*, void* aData, object* aArgs, object*)
            {
                auto const& p = *static_cast<lru_parameters const*>(aData);
                auto const& args = as<tuple_object>(aArgs)->items;
                if (args.size() != 1u)
                    return error("TypeError", "decorating_function() takes 1 positional argument");
                return make_lru(args[0], p.maxsize, p.typed);
            }

            object* b_lru_cache(std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("lru_cache", aArgs, aKwargs, { "maxsize", "typed" }, 0u, 0u, a))
                    return nullptr;
                bool const typed = a[1] != nullptr && truth(a[1]) == 1;
                long long maxsize = 128;
                if (a[0] != nullptr)
                {
                    if (kind_of(a[0]) == NEOS_PY_NONE)
                        maxsize = -1;
                    else if (is_int_like(a[0]))
                    {
                        if (!to_index(a[0], maxsize))
                            return nullptr;
                        if (maxsize < 0)
                            maxsize = 0;
                    }
                    else
                    {
                        ref const callable{ call_with(fn_builtin("callable"), { borrowed(a[0]) }) };
                        if (callable && truth(callable.get()) == 1)
                        {
                            // (@lru_cache: the function itself)
                            auto const result = make_lru(a[0], 128, typed);
                            ref const wraps{ fn_import("functools") };
                            ref const update{ wraps ? getattr(wraps.get(), "update_wrapper") : nullptr };
                            if (update)
                            {
                                ref const r{ call_with(update.get(), { borrowed(result), borrowed(a[0]) }) };
                            }
                            clear_error();
                            return result;
                        }
                        return error("TypeError", "Expected first argument to be an integer, a callable, or None");
                    }
                }
                auto* const parameters = new lru_parameters{ maxsize, typed };  // (the decorator's: kept)
                return make_function("decorating_function", [](neos_py_context* aContext, void* aData, object* aArgs, object* aKwargs) -> object*
                {
                    ref result{ lru_decorator(aContext, aData, aArgs, aKwargs) };
                    if (!result)
                        return nullptr;
                    ref const module{ fn_import("functools") };
                    ref const update{ module ? getattr(module.get(), "update_wrapper") : nullptr };
                    if (update)
                    {
                        ref const r{ call_with(update.get(), { borrowed(result.get()), borrowed(as<tuple_object>(aArgs)->items[0]) }) };
                    }
                    clear_error();
                    return result.release_ownership();
                }, parameters);
            }

            // --- update_wrapper and wraps

            char const* const sAssigned[] = { "__module__", "__name__", "__qualname__", "__doc__", "__annotations__", "__type_params__" };   // (WRAPPER_ASSIGNMENTS)

            // (Lib/functools.py's update_wrapper: aAssigned and aUpdated null: WRAPPER_ASSIGNMENTS and WRAPPER_UPDATES)
            object* update_wrapper(object* aWrapper, object* aWrapped, object* aAssigned = nullptr, object* aUpdated = nullptr)
            {
                // (a wrapper that is neither a Python function nor an instance of a class: a native function (e.g. a translated module's, which a 
                // Python function would be) or a native wrapper (e.g. lru_cache's or singledispatch's): the attributes that it cannot have set or 
                // updated are skipped)
                bool const native = !is_instance(aWrapper) && std::strcmp(aWrapper->type->name, "python.function") != 0;
                // (for attr in assigned: setattr(wrapper, attr, getattr(wrapped, attr)), unless the wrapped has none (AttributeError))
                std::vector<object*> assigned;
                if (aAssigned == nullptr)
                    for (auto name : sAssigned)
                        assigned.push_back(make_str(name));
                else if (!collect(aAssigned, assigned))
                    return nullptr;
                ref const assignedNames{ make_tuple(std::move(assigned)) };
                for (auto name : as<tuple_object>(assignedNames.get())->items)
                {
                    std::string attribute;
                    if (!str_of(name, attribute))
                        return nullptr;
                    ref const value{ getattr(aWrapped, attribute.c_str()) };
                    if (!value)
                    {
                        ref const exception{ take_exception() };
                        if (exception && exception_is(exception.get(), "AttributeError"))
                            continue;
                        if (exception)
                            reraise_exception(exception.get());
                        return nullptr;
                    }
                    if (setattr(aWrapper, attribute.c_str(), value.get()) < 0)
                    {
                        if (!native)
                            return nullptr;
                        clear_error();
                    }
                }
                // (for attr in updated: getattr(wrapper, attr).update(getattr(wrapped, attr, {})))
                std::vector<object*> updated;
                if (aUpdated == nullptr)
                    updated.push_back(make_str("__dict__"));
                else if (!collect(aUpdated, updated))
                    return nullptr;
                ref const updatedNames{ make_tuple(std::move(updated)) };
                for (auto name : as<tuple_object>(updatedNames.get())->items)
                {
                    std::string attribute;
                    if (!str_of(name, attribute))
                        return nullptr;
                    ref const target{ getattr(aWrapper, attribute.c_str()) };
                    if (!target && native)
                    {
                        clear_error();
                        continue;
                    }
                    if (!target)
                        return nullptr;
                    ref source{ getattr(aWrapped, attribute.c_str()) };
                    if (!source)
                    {
                        ref const exception{ take_exception() };
                        if (!exception || !exception_is(exception.get(), "AttributeError"))
                        {
                            if (exception)
                                reraise_exception(exception.get());
                            return nullptr;
                        }
                        source = ref{ make_dict() };
                    }
                    ref const update{ getattr(target.get(), "update") };
                    ref const result{ update ? call_with(update.get(), { borrowed(source.get()) }) : nullptr };
                    if (!result)
                        return nullptr;
                }
                // (__wrapped__ last: not copied from the wrapped's __dict__ (issue #17482))
                if (setattr(aWrapper, "__wrapped__", aWrapped) < 0)
                {
                    if (!native)
                        return nullptr;
                    clear_error();
                }
                return borrowed(aWrapper);
            }

            // --- total_ordering

            struct ordering_entry { char const* root; char const* name; int kind; char const* function; char const* doc; };

            // (the derived comparisons (CPython's _convert, and its functions (_gt_from_lt, ...)): kind 0: not r and != ; 1: r or ==; 2: not r; 
            // 3: not r or ==; 4: r and !=)
            ordering_entry const sOrderings[] =
            {
                { "__lt__", "__gt__", 0, "_gt_from_lt", "Return a > b.  Computed by @total_ordering from (not a < b) and (a != b)." },
                { "__lt__", "__le__", 1, "_le_from_lt", "Return a <= b.  Computed by @total_ordering from (a < b) or (a == b)." },
                { "__lt__", "__ge__", 2, "_ge_from_lt", "Return a >= b.  Computed by @total_ordering from (not a < b)." },
                { "__le__", "__ge__", 3, "_ge_from_le", "Return a >= b.  Computed by @total_ordering from (not a <= b) or (a == b)." },
                { "__le__", "__lt__", 4, "_lt_from_le", "Return a < b.  Computed by @total_ordering from (a <= b) and (a != b)." },
                { "__le__", "__gt__", 2, "_gt_from_le", "Return a > b.  Computed by @total_ordering from (not a <= b)." },
                { "__gt__", "__lt__", 0, "_lt_from_gt", "Return a < b.  Computed by @total_ordering from (not a > b) and (a != b)." },
                { "__gt__", "__ge__", 1, "_ge_from_gt", "Return a >= b.  Computed by @total_ordering from (a > b) or (a == b)." },
                { "__gt__", "__le__", 2, "_le_from_gt", "Return a <= b.  Computed by @total_ordering from (not a > b)." },
                { "__ge__", "__le__", 3, "_le_from_ge", "Return a <= b.  Computed by @total_ordering from (not a >= b) or (a == b)." },
                { "__ge__", "__gt__", 4, "_gt_from_ge", "Return a > b.  Computed by @total_ordering from (a >= b) and (a != b)." },
                { "__ge__", "__lt__", 2, "_lt_from_ge", "Return a < b.  Computed by @total_ordering from (not a >= b)." }
            };

            object* derived_comparison(neos_py_context*, void* aData, object* aArgs, object*);

            // (the module's function of a derived comparison (one object: the classes that total_ordering completes share it, and it 
            // pickles as functools's (e.g. functools._gt_from_lt)): borrowed)
            object* ordering_function(ordering_entry const& aEntry)
            {
                static object* sFunctions[std::size(sOrderings)] = {};
                auto& f = sFunctions[&aEntry - sOrderings];
                if (f == nullptr)
                {
                    f = make_function(aEntry.function, derived_comparison, const_cast<ordering_entry*>(&aEntry));
                    auto& function = *reinterpret_cast<function_object*>(f);
                    function.module = "functools";
                    function.doc = aEntry.doc;
                }
                return f;
            }

            object* derived_comparison(neos_py_context*, void* aData, object* aArgs, object*)
            {
                auto const& entry = *static_cast<ordering_entry const*>(aData);
                auto const& args = as<tuple_object>(aArgs)->items;
                if (args.size() != 2u)
                    return error("TypeError", std::string{ entry.name } + "() takes 2 positional arguments");
                ref const cls{ class_of(args[0]) };
                ref const root{ getattr(cls.get(), entry.root) };
                if (!root)
                    return nullptr;
                ref result{ call_with(root.get(), { borrowed(args[0]), borrowed(args[1]) }) };
                if (!result || kind_of(result.get()) == NEOS_PY_NOT_IMPLEMENTED)
                    return result.release_ownership();
                auto const t = truth(result.get());
                if (t < 0)
                    return nullptr;
                switch (entry.kind)
                {
                case 0:
                    if (t == 1)
                        return make_bool(false);
                    return compare(NEOS_PY_NE, args[0], args[1]);
                case 1:
                    if (t == 1)
                        return result.release_ownership();
                    return compare(NEOS_PY_EQ, args[0], args[1]);
                case 2:
                    return make_bool(t == 0);
                case 3:
                    if (t == 0)
                        return make_bool(true);
                    return compare(NEOS_PY_EQ, args[0], args[1]);
                default:
                    if (t == 0)
                        return result.release_ownership();
                    return compare(NEOS_PY_NE, args[0], args[1]);
                }
            }

            object* b_total_ordering(std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("total_ordering", aArgs, aKwargs, { "cls" }, 1u, 0u, a))
                    return nullptr;
                auto const cls = a[0];
                // (Lib/functools.py's: roots = {op for op in _convert if getattr(cls, op, None) is not getattr(object, op, None)})
                std::vector<std::string> roots;
                ref const objectType{ type_object_for("object", "object") };
                for (auto name : { "__lt__", "__le__", "__gt__", "__ge__" })
                {
                    ref mine{ getattr(cls, name) };
                    if (!mine)
                        clear_error();
                    ref theirs{ getattr(objectType.get(), name) };
                    if (!theirs)
                        clear_error();
                    if (mine.get() != theirs.get())
                        roots.push_back(name);
                }
                if (roots.empty())
                    return error("ValueError", "must define at least one ordering operation: < > <= >=");
                auto const root = *std::max_element(roots.begin(), roots.end());
                for (auto const& entry : sOrderings)
                    if (root == entry.root && std::find(roots.begin(), roots.end(), entry.name) == roots.end())
                    {
                        if (setattr(cls, entry.name, ordering_function(entry)) < 0)
                            return nullptr;
                    }
                return borrowed(cls);
            }

            // --- cmp_to_key

            struct key_object
            {
                header h;
                object* function;
                object* value;
                ~key_object() { release(function); release(value); }
            };

            neos_py_type make_key_type();
            neos_py_type sKeyType = make_key_type();

            neos_py_type make_key_type()
            {
                auto type = make_type("functools.KeyWrapper", "functools.KeyWrapper", retain_object, release_object<key_object>);
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    if (aLhs->type != &sKeyType.base || aRhs->type != &sKeyType.base)
                        return error("TypeError", "other argument must be K instance");
                    auto const& l = *reinterpret_cast<key_object*>(aLhs);
                    auto const& r = *reinterpret_cast<key_object*>(aRhs);
                    ref const result{ call_with(l.function, { borrowed(l.value), borrowed(r.value) }) };
                    if (!result)
                        return nullptr;
                    ref const zero{ make_int(0) };
                    return compare(aOperation, result.get(), zero.get());
                };
                // (K(obj): obj positional or a keyword (CPython's keyobject_call: "O:K", {"obj"}))
                type.call = [](neos_py_context*, object* aSelf, object* aArgs, object* aKwargs) -> object*
                {
                    std::vector<object*> a;
                    if (!parse_arguments("K", as<tuple_object>(aArgs)->items, aKwargs, { "obj" }, 1u, 0u, a))
                        return nullptr;
                    auto const& k = *reinterpret_cast<key_object*>(aSelf);
                    return &(new key_object{ { { &sKeyType.base } }, borrowed(k.function), borrowed(a[0]) })->h.base;
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    if (std::strcmp(aName, "__text_signature__") == 0)
                        return make_str("(obj)");
                    if (std::strcmp(aName, "obj") == 0)
                    {
                        auto const v = reinterpret_cast<key_object*>(aSelf)->value;
                        return v != nullptr ? borrowed(v) : error("AttributeError", "obj");
                    }
                    return error("AttributeError", std::string{ "'functools.KeyWrapper' object has no attribute '" } + aName + "'");
                };
                type.hash = [](neos_py_context*, object*, long long*) -> int { error("TypeError", "unhashable type: 'functools.KeyWrapper'"); return -1; };
                return type;
            }

            // --- reduce and the module

            builtin_entry const sFunctoolsFunctions[] =
            {
                { "reduce", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                            return error("TypeError", "reduce() takes no keyword arguments");
                        if (aArgs.size() < 2u || aArgs.size() > 3u)
                            return error("TypeError", "reduce expected at least 2 arguments, got " + std::to_string(aArgs.size()));
                        ref const it{ iter(aArgs[1]) };
                        if (!it)
                        {
                            // (a TypeError: "must support iteration"; another exception (e.g. __iter__'s RuntimeError): itself (as CPython's))
                            ref const exception{ take_exception() };
                            if (exception && exception_is(exception.get(), "TypeError"))
                                return error("TypeError", "reduce() arg 2 must support iteration");
                            if (exception)
                                reraise_exception(exception.get());
                            return nullptr;
                        }
                        ref result{ aArgs.size() == 3u ? borrowed(aArgs[2]) : nullptr };
                        for (;;)
                        {
                            ref item{ next(it.get()) };
                            if (!item)
                            {
                                if (failed())
                                    return nullptr;
                                break;
                            }
                            if (!result)
                                result = item;
                            else
                            {
                                result = ref{ call_with(aArgs[0], { result.release_ownership(), item.release_ownership() }) };
                                if (!result)
                                    return nullptr;
                            }
                        }
                        if (!result)
                            return error("TypeError", "reduce() of empty iterable with no initial value");
                        return result.release_ownership();
                    } },
                { "lru_cache", b_lru_cache },
                { "cache", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("cache", aArgs, aKwargs, { "user_function" }, 1u, 0u, a))
                            return nullptr;
                        ref const result{ make_lru(a[0], -1, false) };
                        return update_wrapper(result.get(), a[0]);
                    } },
                { "update_wrapper", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("update_wrapper", aArgs, aKwargs, { "wrapper", "wrapped", "assigned", "updated" }, 2u, 0u, a))
                            return nullptr;
                        return update_wrapper(a[0], a[1], a[2], a[3]);
                    } },
                // (Lib/functools.py's wraps: partial(update_wrapper, wrapped=wrapped, assigned=assigned, updated=updated))
                { "wraps", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("wraps", aArgs, aKwargs, { "wrapped", "assigned", "updated" }, 1u, 0u, a))
                            return nullptr;
                        ref const update{ module_attribute("functools", "update_wrapper") };
                        if (!update)
                            return nullptr;
                        ref const keywords{ make_dict() };
                        ref assigned{ a[1] != nullptr ? borrowed(a[1]) : nullptr };
                        if (!assigned)
                        {
                            std::vector<object*> names;
                            for (auto name : sAssigned)
                                names.push_back(make_str(name));
                            assigned = ref{ make_tuple(std::move(names)) };
                        }
                        ref const updated{ a[2] != nullptr ? borrowed(a[2]) : make_tuple({ make_str("__dict__") }) };
                        std::pair<char const*, object*> const items[] = { { "wrapped", a[0] }, { "assigned", assigned.get() }, { "updated", updated.get() } };
                        for (auto const& [name, value] : items)
                        {
                            ref const key{ make_str(name) };
                            if (dict_set(keywords.get(), key.get(), value) != 0)
                                return nullptr;
                        }
                        return make_partial(update.get(), {}, keywords.get());
                    } },
                { "total_ordering", b_total_ordering },
                { "cmp_to_key", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("cmp_to_key", aArgs, aKwargs, { "mycmp" }, 1u, 0u, a))
                            return nullptr;
                        return &(new key_object{ { { &sKeyType.base } }, borrowed(a[0]), nullptr })->h.base;
                    } }
            };

            // --- partialmethod and singledispatchmethod (CPython's classes of functools.py)

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

            // (getattr(aObject, "__get__", None): its __get__ (the descriptor protocol's: its type's, bound to it), or empty)
            py get_of(py const& aObject)
            {
                py get = attr_or(aObject, "__get__", py{});
                if (!get.empty())
                    return get;
                py const typeGet = attr_or(type_of(aObject), "__get__", py{});
                if (typeGet.empty())
                    return {};
                return make_native_function("__get__", "__get__", "", "functools", [typeGet, aObject](std::vector<py> const& aArgs, object*) -> py
                {
                    std::vector<py> args{ aObject };
                    args.insert(args.end(), aArgs.begin(), aArgs.end());
                    return typeGet.call(args);
                });
            }

            // (a function's __get__(obj, cls): it bound to obj (None: itself))
            py bind_function(py const& aFunction, py const& aObject, py const& aClass)
            {
                if (py const get = get_of(aFunction); !get.empty())
                    return get(aObject, aClass);
                return aObject.is_none() ? aFunction : import_attribute("types", "MethodType")(aFunction, aObject);
            }

            py functools_class(char const* aName, std::vector<std::pair<char const*, py>> const& aNamespace, char const* aDoc)
            {
                py const ns = py_dict();
                ns.set_item(py{ "__module__" }, py{ "functools" });
                ns.set_item(py{ "__qualname__" }, py{ aName });
                ns.set_item(py{ "__doc__" }, py{ aDoc });
                for (auto const& [name, value] : aNamespace)
                    ns.set_item(py{ name }, value);
                return builtin("type")(py{ aName }, py_tuple({ builtin("object") }), ns);
            }

            py functools_method(std::string const& aQualname, std::vector<py_parameter> aParameters, std::function<py(std::vector<py> const&, py const&, py const&)> aBody,
                bool aVarArgs = false, bool aVarKeywords = false, char const* aDoc = "")
            {
                auto const dot = aQualname.rfind('.');
                return make_native_function(dot == std::string::npos ? aQualname : aQualname.substr(dot + 1u), aQualname, aDoc, "functools",
                    [aQualname, aParameters, aBody, aVarArgs, aVarKeywords](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        py rest;
                        py extra;
                        auto const a = py_bind(aQualname, aParameters, aArgs, aKwargs, aVarArgs ? &rest : nullptr, aVarKeywords ? &extra : nullptr);
                        return aBody(a, rest, extra);
                    });
            }

            py isabstractmethod_property(char const* aClass)
            {
                return builtin("property")(functools_method(std::string{ aClass } + ".__isabstractmethod__", { py_positional("self") },
                    [](std::vector<py> const& a, py const&, py const&) { return attr_or(a[0].attr("func"), "__isabstractmethod__", py::boolean(false)); }));
            }

            py& partialmethod_class()
            {
                static py* sClass = new py{};
                return *sClass;
            }

            py make_partialmethod_class()
            {
                py const cls = functools_class("partialmethod", {
                    { "__init__", functools_method("partialmethod.__init__", { py_positional_only("self"), py_positional_only("func") },
                        [](std::vector<py> const& a, py const& aArgs, py const& aKeywords) -> py
                        {
                            auto const& self = a[0];
                            auto const& func = a[1];
                            if (!is_callable(func.get()) && attr_or(func, "__get__", py{}).empty())
                                py_raise("TypeError", func.repr() + " is not callable or a descriptor");
                            if (isinstance(func, partialmethod_class()))
                            {
                                self.set_attr("func", func.attr("func"));
                                self.set_attr("args", func.attr("args") + aArgs);
                                py const keywords = py_dict();
                                keywords.method("update", { func.attr("keywords") });
                                keywords.method("update", { aKeywords });
                                self.set_attr("keywords", keywords);
                            }
                            else
                            {
                                self.set_attr("func", func);
                                self.set_attr("args", aArgs);
                                self.set_attr("keywords", aKeywords);
                            }
                            return py::none();
                        }, true, true) },
                    { "__repr__", functools_method("partialmethod.__repr__", { py_positional("self") }, [](std::vector<py> const& a, py const&, py const&)
                        {
                            py const cls = type_of(a[0]);
                            std::string text;
                            text = a[0].attr("func").repr();
                            for (auto const& v : a[0].attr("args").items())
                                text += ", " + v.repr();
                            for (auto const& item : a[0].attr("keywords").method("items").items())
                                text += ", " + item[py{ 0 }].str() + "=" + item[py{ 1 }].repr();
                            return py{ cls.attr("__module__").str() + "." + cls.attr("__qualname__").str() + "(" + text + ")" };
                        }) },
                    { "_make_unbound_method", functools_method("partialmethod._make_unbound_method", { py_positional("self") }, [](std::vector<py> const& a, py const&, py const&)
                        {
                            py const self = a[0];
                            py const method = make_native_function("_method", "partialmethod._make_unbound_method.<locals>._method", "", "functools",
                                [self](std::vector<py> const& aArgs, object* aKwargs) -> py
                                {
                                    py rest;
                                    py extra;
                                    auto const m = py_bind("partialmethod._make_unbound_method.<locals>._method", { py_positional_only("cls_or_self") }, aArgs, aKwargs,
                                        &rest, &extra);
                                    py const keywords = py_dict();
                                    keywords.method("update", { self.attr("keywords") });
                                    keywords.method("update", { extra });
                                    std::vector<object*> args{ m[0].reference() };
                                    for (auto const& v : self.attr("args").items())
                                        args.push_back(v.reference());
                                    for (auto const& v : rest.items())
                                        args.push_back(v.reference());
                                    return py{ invoke(self.attr("func").get(), std::move(args), keywords.get()) };
                                });
                            method.set_attr("__isabstractmethod__", self.attr("__isabstractmethod__"));
                            method.set_attr("__partialmethod__", self);
                            return method;
                        }) },
                    { "__get__", functools_method("partialmethod.__get__", { py_positional("self"), py_positional("obj"), py_positional("cls", py::none()) },
                        [](std::vector<py> const& a, py const&, py const&)
                        {
                            auto const& self = a[0];
                            py const func = self.attr("func");
                            py const partial = import_attribute("functools", "partial");
                            py const get = get_of(func);
                            py result;
                            if (!get.empty() && !isinstance(func, partial))
                            {
                                py const newFunc = get(a[1], a[2]);
                                if (!newFunc.is(func))
                                {
                                    std::vector<object*> args{ newFunc.reference() };
                                    for (auto const& v : self.attr("args").items())
                                        args.push_back(v.reference());
                                    result = py{ invoke(partial.get(), std::move(args), self.attr("keywords").get()) };
                                    try
                                    {
                                        result.set_attr("__self__", newFunc.attr("__self__"));
                                    }
                                    catch (raised const&)
                                    {
                                        if (!caught({ "AttributeError", "TypeError" }))
                                            throw;
                                    }
                                }
                            }
                            if (result.empty())
                                result = bind_function(self.method("_make_unbound_method"), a[1], a[2]);
                            return result;
                        }) },
                    { "__isabstractmethod__", isabstractmethod_property("partialmethod") },
                    { "__class_getitem__", builtin("classmethod")(import_attribute("types", "GenericAlias")) } },
                    "Method descriptor with partial application of the given arguments\n    and keywords.\n\n    Supports wrapping existing descriptors and handles non-descriptor\n"
                    "    callables as instance methods.\n    ");
                partialmethod_class() = cls;
                return cls;
            }

            // --- cached_property (CPython 3.13's class of Lib/functools.py)

            // (raise aException from None)
            [[noreturn]] void raise_from_none(py const& aException)
            {
                aException.set_attr("__cause__", py::none());
                aException.set_attr("__suppress_context__", py::boolean(true));
                py_raise(aException);
            }

            py& not_found() { static py sNotFound = builtin("object")(); return sNotFound; }   // (_NOT_FOUND)

            py make_cached_property_class()
            {
                py const ns = py_dict();
                ns.set_item(py{ "__module__" }, py{ "functools" });
                ns.set_item(py{ "__qualname__" }, py{ "cached_property" });
                ns.set_item(py{ "__doc__" }, py::none());
                ns.set_item(py{ "__init__" }, functools_method("cached_property.__init__", { py_positional("self"), py_positional("func") },
                    [](std::vector<py> const& a, py const&, py const&) -> py
                    {
                        a[0].set_attr("func", a[1]);
                        a[0].set_attr("attrname", py::none());
                        a[0].set_attr("__doc__", a[1].attr("__doc__"));
                        a[0].set_attr("__module__", a[1].attr("__module__"));
                        return py::none();
                    }));
                ns.set_item(py{ "__set_name__" }, functools_method("cached_property.__set_name__", { py_positional("self"), py_positional("owner"), py_positional("name") },
                    [](std::vector<py> const& a, py const&, py const&) -> py
                    {
                        py const attrname = a[0].attr("attrname");
                        if (attrname.is_none())
                            a[0].set_attr("attrname", a[2]);
                        else if (a[2] != attrname)
                            py_raise("TypeError", "Cannot assign the same cached_property to two different names (" + attrname.repr() + " and " + a[2].repr() + ").");
                        return py::none();
                    }));
                ns.set_item(py{ "__get__" }, functools_method("cached_property.__get__", { py_positional("self"), py_positional("instance"), py_positional("owner", py::none()) },
                    [](std::vector<py> const& a, py const&, py const&) -> py
                    {
                        py const& self = a[0];
                        py const& instance = a[1];
                        if (instance.is_none())
                            return self;
                        py const attrname = self.attr("attrname");
                        if (attrname.is_none())
                            py_raise("TypeError", "Cannot use cached_property instance without calling __set_name__ on it.");
                        py cache;
                        try
                        {
                            cache = instance.attr("__dict__");
                        }
                        catch (raised const&)
                        {
                            // (not all objects have __dict__ (e.g. a class that defines slots))
                            if (!caught({ "AttributeError" }))
                                throw;
                            raise_from_none(builtin("TypeError")(py{ "No '__dict__' attribute on " + type_of(instance).attr("__name__").repr() + " instance to cache " +
                                attrname.repr() + " property." }));
                        }
                        py val = cache.method("get", { attrname, not_found() });
                        if (val.is(not_found()))
                        {
                            val = self.attr("func")(instance);
                            try
                            {
                                cache.set_item(attrname, val);
                            }
                            catch (raised const&)
                            {
                                if (!caught({ "TypeError" }))
                                    throw;
                                raise_from_none(builtin("TypeError")(py{ "The '__dict__' attribute on " + type_of(instance).attr("__name__").repr() +
                                    " instance does not support item assignment for caching " + attrname.repr() + " property." }));
                            }
                        }
                        return val;
                    }));
                ns.set_item(py{ "__class_getitem__" }, builtin("classmethod")(import_attribute("types", "GenericAlias")));
                return builtin("type")(py{ "cached_property" }, py_tuple({ builtin("object") }), ns);
            }

            // --- singledispatch (CPython 3.13's Lib/functools.py: _c3_merge, _c3_mro, _compose_mro, _find_impl and singledispatch)

            bool has_attr(py const& aObject, char const* aName) { return !attr_or(aObject, aName, py{}).empty(); }

            bool sequence_contains(std::vector<py> const& aSequence, py const& aItem, std::size_t aFrom = 0u)
            {
                for (std::size_t i = aFrom; i < aSequence.size(); ++i)
                    if (aSequence[i] == aItem)
                        return true;
                return false;
            }

            // (_c3_merge: the MROs in aSequences merged to a single MRO (the C3 algorithm))
            std::vector<py> c3_merge(std::vector<std::vector<py>> aSequences)
            {
                std::vector<py> result;
                for (;;)
                {
                    std::erase_if(aSequences, [](std::vector<py> const& aSequence) { return aSequence.empty(); });    // (purge empty sequences)
                    if (aSequences.empty())
                        return result;
                    // (a merge candidate among the sequences' heads: one that no sequence has later)
                    py candidate;
                    for (auto const& s1 : aSequences)
                    {
                        candidate = s1[0];
                        for (auto const& s2 : aSequences)
                            if (sequence_contains(s2, candidate, 1u))
                            {
                                candidate = py{};
                                break;
                            }
                        if (!candidate.empty())
                            break;
                    }
                    if (candidate.empty())
                        py_raise("RuntimeError", "Inconsistent hierarchy");
                    result.push_back(candidate);
                    for (auto& seq : aSequences)
                        if (seq[0] == candidate)
                            seq.erase(seq.begin());
                }
            }

            // (_c3_mro: the MRO by the extended C3 linearization: aAbcs (abstract base classes) inserted where their functionality is 
            // introduced)
            std::vector<py> c3_mro(py const& aClass, std::vector<py> aAbcs)
            {
                auto const bases = aClass.attr("__bases__").items();
                std::size_t boundary = 0u;
                for (std::size_t i = 0u; i < bases.size(); ++i)
                    if (has_attr(bases[bases.size() - 1u - i], "__abstractmethods__"))
                    {
                        boundary = bases.size() - i;    // (bases up to the last explicit ABC are considered first)
                        break;
                    }
                std::vector<py> const explicitBases{ bases.begin(), bases.begin() + static_cast<std::ptrdiff_t>(boundary) };
                std::vector<py> abstractBases;
                std::vector<py> const otherBases{ bases.begin() + static_cast<std::ptrdiff_t>(boundary), bases.end() };
                for (auto const& base : aAbcs)
                    if (issubclass(aClass, base) && std::none_of(bases.begin(), bases.end(), [&](py const& b) { return issubclass(b, base); }))
                        abstractBases.push_back(base);  // (aClass introduces the behaviour that the ABC describes: the ABC is in its MRO)
                for (auto const& base : abstractBases)
                    for (auto i = aAbcs.begin(); i != aAbcs.end(); ++i)
                        if (*i == base)
                        {
                            aAbcs.erase(i);
                            break;
                        }
                std::vector<std::vector<py>> sequences{ { aClass } };
                std::vector<py> const* const groups[] = { &explicitBases, &abstractBases, &otherBases };
                for (auto const* group : groups)
                    for (auto const& base : *group)
                        sequences.push_back(c3_mro(base, aAbcs));
                sequences.push_back(explicitBases);
                sequences.push_back(abstractBases);
                sequences.push_back(otherBases);
                return c3_merge(std::move(sequences));
            }

            // (_compose_mro: the MRO of aClass with the relevant abstract base classes (and their bases) of aTypes)
            std::vector<py> compose_mro(py const& aClass, py const& aTypes)
            {
                py const bases = builtin("set")(aClass.attr("__mro__"));
                py const genericAlias = import_attribute("types", "GenericAlias");
                // (not those already in the MRO, or unrelated)
                std::vector<py> types;
                for (auto const& typ : aTypes.items())
                    if (!bases.contains(typ) && has_attr(typ, "__mro__") && !isinstance(typ, genericAlias) && issubclass(aClass, typ))
                        types.push_back(typ);
                // (not those that are strict bases of others (they will be in the MRO anyway))
                std::vector<py> remaining;
                for (auto const& typ : types)
                {
                    bool strict = false;
                    for (auto const& other : types)
                        if (typ != other && other.attr("__mro__").contains(typ))
                        {
                            strict = true;
                            break;
                        }
                    if (!strict)
                        remaining.push_back(typ);
                }
                // (subclasses of the ABCs that aClass also implements: they stabilize the ABCs' order)
                py const typeSet = builtin("set")(py_list(remaining));
                std::vector<py> mro;
                for (auto const& typ : remaining)
                {
                    std::vector<std::vector<py>> found;
                    for (auto const& sub : typ.method("__subclasses__").items())
                        if (!bases.contains(sub) && issubclass(aClass, sub))
                        {
                            std::vector<py> useful;
                            for (auto const& s : sub.attr("__mro__").items())
                                if (typeSet.contains(s))
                                    useful.push_back(s);
                            found.push_back(std::move(useful));
                        }
                    if (found.empty())
                    {
                        mro.push_back(typ);
                        continue;
                    }
                    // (the subclasses with the most useful bases first)
                    std::stable_sort(found.begin(), found.end(), [](std::vector<py> const& a, std::vector<py> const& b) { return a.size() > b.size(); });
                    for (auto const& sub : found)
                        for (auto const& subcls : sub)
                            if (!sequence_contains(mro, subcls))
                                mro.push_back(subcls);
                }
                return c3_mro(aClass, std::move(mro));
            }

            // (_find_impl: the best implementation in aRegistry for aClass (by its MRO with the registry's ABCs); None if none)
            py find_impl(py const& aClass, py const& aRegistry)
            {
                auto const mro = compose_mro(aClass, aRegistry.method("keys"));
                py match;
                py const classMro = aClass.attr("__mro__");
                for (auto const& t : mro)
                {
                    if (!match.empty())
                    {
                        // (match an implicit ABC, and another unrelated, equally matching implicit ABC: no guess)
                        if (aRegistry.contains(t) && !classMro.contains(t) && !classMro.contains(match) && !issubclass(match, t))
                            py_raise("RuntimeError", "Ambiguous dispatch: " + match.str() + " or " + t.str());
                        break;
                    }
                    if (aRegistry.contains(t))
                        match = t;
                }
                return aRegistry.method("get", { match.empty() ? py::none() : match });
            }

            py list_of(std::vector<py> const& aItems) { return py_list(aItems); }

            // (singledispatch(func): a generic function of the type of its first argument)
            py single_dispatch(py const& aFunction)
            {
                struct dispatch_state
                {
                    py registry = py_dict();
                    py dispatchCache = import_attribute("weakref", "WeakKeyDictionary")();
                    py cacheToken = py::none();
                };
                auto const state = std::make_shared<dispatch_state>();
                py const dispatch = functools_method("singledispatch.<locals>.dispatch", { py_positional("cls") },
                    [state](std::vector<py> const& a, py const&, py const&) -> py
                    {
                        auto const& cls = a[0];
                        if (!state->cacheToken.is_none())
                        {
                            py const currentToken = import_attribute("abc", "get_cache_token")();
                            if (state->cacheToken != currentToken)
                            {
                                state->dispatchCache.method("clear");
                                state->cacheToken = currentToken;
                            }
                        }
                        try
                        {
                            return state->dispatchCache[cls];
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "KeyError" }))
                                throw;
                        }
                        py implementation;
                        try
                        {
                            implementation = state->registry[cls];
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "KeyError" }))
                                throw;
                            implementation = find_impl(cls, state->registry);
                        }
                        state->dispatchCache.set_item(cls, implementation);
                        return implementation;
                    }, false, false, "generic_func.dispatch(cls) -> <function implementation>\n\n        Runs the dispatch algorithm to return the best available implementation\n"
                    "        for the given *cls* registered on *generic_func*.\n\n        ");
                auto const union_type = [](py const& aClass)
                {
                    py const origin = import_attribute("typing", "get_origin")(aClass);
                    return origin == import_attribute("typing", "Union") || origin == import_attribute("types", "UnionType");
                };
                auto const valid_dispatch_type = [union_type](py const& aClass)
                {
                    if (isinstance(aClass, builtin("type")))
                        return true;
                    if (!union_type(aClass))
                        return false;
                    for (auto const& arg : import_attribute("typing", "get_args")(aClass).items())
                        if (!isinstance(arg, builtin("type")))
                            return false;
                    return true;
                };
                auto const registerHolder = std::make_shared<py>();
                py const registerFunction = functools_method("singledispatch.<locals>.register", { py_positional("cls"), py_positional("func", py::none()) },
                    [state, union_type, valid_dispatch_type, registerHolder](std::vector<py> const& a, py const&, py const&) -> py
                    {
                        py cls = a[0];
                        py func = a[1];
                        if (valid_dispatch_type(cls))
                        {
                            if (func.is_none())
                            {
                                // (lambda f: register(cls, f))
                                py const target = *registerHolder;
                                return make_native_function("<lambda>", "singledispatch.<locals>.register.<locals>.<lambda>", "", "functools",
                                    [target, cls](std::vector<py> const& aArgs, object* aKwargs) -> py
                                    {
                                        auto const f = py_bind("singledispatch.<locals>.register.<locals>.<lambda>", { py_positional("f") }, aArgs, aKwargs);
                                        return target(cls, f[0]);
                                    });
                            }
                        }
                        else
                        {
                            if (!func.is_none())
                                py_raise("TypeError", "Invalid first argument to `register()`. " + cls.repr() + " is not a class or union type.");
                            py const annotations = attr_or(cls, "__annotations__", py_dict());
                            if (!annotations.truthy())
                                py_raise("TypeError", "Invalid first argument to `register()`: " + cls.repr() + ". Use either `@register(some_class)` or plain "
                                    "`@register` on an annotated function.");
                            func = cls;
                            // (argname, cls = next(iter(get_type_hints(func).items())))
                            py const first = builtin("next")(builtin("iter")(import_attribute("typing", "get_type_hints")(func).method("items")));
                            py const argname = first[py{ 0 }];
                            cls = first[py{ 1 }];
                            if (!valid_dispatch_type(cls))
                            {
                                if (union_type(cls))
                                    py_raise("TypeError", "Invalid annotation for " + argname.repr() + ". " + cls.repr() + " not all arguments are classes.");
                                py_raise("TypeError", "Invalid annotation for " + argname.repr() + ". " + cls.repr() + " is not a class.");
                            }
                        }
                        if (union_type(cls))
                        {
                            for (auto const& arg : import_attribute("typing", "get_args")(cls).items())
                                state->registry.set_item(arg, func);
                        }
                        else
                            state->registry.set_item(cls, func);
                        if (state->cacheToken.is_none() && has_attr(cls, "__abstractmethods__"))
                            state->cacheToken = import_attribute("abc", "get_cache_token")();
                        state->dispatchCache.method("clear");
                        return func;
                    }, false, false, "generic_func.register(cls, func) -> func\n\n        Registers a new implementation for the given *cls* on a *generic_func*.\n\n        ");
                *registerHolder = registerFunction;
                std::string const funcname = attr_or(aFunction, "__name__", py{ "singledispatch function" }).str();
                py const wrapper = make_native_function("wrapper", "singledispatch.<locals>.wrapper", "", "functools",
                    [dispatch, funcname](std::vector<py> const& aArgs, object* aKwargs) -> py
                    {
                        if (aArgs.empty())
                            py_raise("TypeError", funcname + " requires at least 1 positional argument");
                        py const implementation = dispatch(aArgs[0].attr("__class__"));
                        std::vector<object*> args;
                        for (auto const& v : aArgs)
                            args.push_back(v.reference());
                        return py{ invoke(implementation.get(), std::move(args), aKwargs) };
                    });
                state->registry.set_item(builtin("object"), aFunction);
                wrapper.set_attr("register", registerFunction);
                wrapper.set_attr("dispatch", dispatch);
                wrapper.set_attr("registry", import_attribute("types", "MappingProxyType")(state->registry));
                wrapper.set_attr("_clear_cache", state->dispatchCache.attr("clear"));
                import_attribute("functools", "update_wrapper")(wrapper, aFunction);
                return wrapper;
            }

            // (the module's functions of singledispatch: singledispatch, _c3_merge, _c3_mro, _compose_mro and _find_impl)
            void add_single_dispatch(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                aAttributes.emplace_back("singledispatch", functools_method("singledispatch", { py_positional("func") },
                    [](std::vector<py> const& a, py const&, py const&) -> py { return single_dispatch(a[0]); }, false, false,
                    "Single-dispatch generic function decorator.\n\n    Transforms a function into a generic function, which can have different\n"
                    "    behaviours depending upon the type of its first argument. The decorated\n    function acts as the default implementation, and additional\n"
                    "    implementations can be registered using the register() attribute of the\n    generic function.\n    ").reference());
                aAttributes.emplace_back("_c3_merge", functools_method("_c3_merge", { py_positional("sequences") },
                    [](std::vector<py> const& a, py const&, py const&) -> py
                    {
                        // (the sequences (lists) are merged in place, as Lib/functools.py's)
                        py const sequences = a[0];
                        py result = py_list();
                        for (;;)
                        {
                            std::vector<py> purged;
                            for (auto const& s : sequences.items())
                                if (s.truthy())
                                    purged.push_back(s);
                            if (purged.empty())
                                return result;
                            py candidate;
                            for (auto const& s1 : purged)
                            {
                                candidate = s1[py{ 0 }];
                                for (auto const& s2 : purged)
                                {
                                    auto const rest = s2.items();
                                    if (sequence_contains(rest, candidate, 1u))
                                    {
                                        candidate = py{};
                                        break;
                                    }
                                }
                                if (!candidate.empty())
                                    break;
                            }
                            if (candidate.empty())
                                py_raise("RuntimeError", "Inconsistent hierarchy");
                            result.method("append", { candidate });
                            for (auto const& seq : purged)
                                if (seq[py{ 0 }] == candidate)
                                    seq.method("__delitem__", { py{ 0 } });
                        }
                    }).reference());
                aAttributes.emplace_back("_c3_mro", functools_method("_c3_mro", { py_positional("cls"), py_positional("abcs", py::none()) },
                    [](std::vector<py> const& a, py const&, py const&) -> py { return list_of(c3_mro(a[0], a[1].truthy() ? a[1].items() : std::vector<py>{})); }).reference());
                aAttributes.emplace_back("_compose_mro", functools_method("_compose_mro", { py_positional("cls"), py_positional("types") },
                    [](std::vector<py> const& a, py const&, py const&) -> py { return list_of(compose_mro(a[0], a[1])); }).reference());
                aAttributes.emplace_back("_find_impl", functools_method("_find_impl", { py_positional("cls"), py_positional("registry") },
                    [](std::vector<py> const& a, py const&, py const&) -> py { return find_impl(a[0], a[1]); }).reference());
            }

            py make_singledispatchmethod_class()
            {
                return functools_class("singledispatchmethod", {
                    { "__init__", functools_method("singledispatchmethod.__init__", { py_positional("self"), py_positional("func") },
                        [](std::vector<py> const& a, py const&, py const&) -> py
                        {
                            auto const& func = a[1];
                            if (!is_callable(func.get()) && attr_or(func, "__get__", py{}).empty())
                                py_raise("TypeError", func.repr() + " is not callable or a descriptor");
                            a[0].set_attr("dispatcher", import_attribute("functools", "singledispatch")(func));
                            a[0].set_attr("func", func);
                            return py::none();
                        }) },
                    { "register", functools_method("singledispatchmethod.register", { py_positional("self"), py_positional("cls"), py_positional("method", py::none()) },
                        [](std::vector<py> const& a, py const&, py const&)
                        {
                            return a[0].attr("dispatcher").attr("register").call({ a[1] }, { { "func", a[2] } });
                        }, false, false, "generic_method.register(cls, func) -> func\n\n        Registers a new implementation for the given *cls* on a *generic_method*.\n        ") },
                    { "__get__", functools_method("singledispatchmethod.__get__", { py_positional("self"), py_positional("obj"), py_positional("cls", py::none()) },
                        [](std::vector<py> const& a, py const&, py const&)
                        {
                            auto const& self = a[0];
                            py const dispatch = self.attr("dispatcher").attr("dispatch");
                            std::string const funcname = attr_or(self.attr("func"), "__name__", py{ "singledispatchmethod method" }).str();
                            py const obj = a[1];
                            py const cls = a[2];
                            py const method = make_native_function("_method", "singledispatchmethod.__get__.<locals>._method", "", "functools",
                                [dispatch, funcname, obj, cls](std::vector<py> const& aArgs, object* aKwargs) -> py
                                {
                                    if (aArgs.empty())
                                        py_raise("TypeError", funcname + " requires at least 1 positional argument");
                                    py const implementation = bind_function(dispatch(aArgs[0].attr("__class__")), obj, cls);
                                    std::vector<object*> args;
                                    for (auto const& v : aArgs)
                                        args.push_back(v.reference());
                                    return py{ invoke(implementation.get(), std::move(args), aKwargs) };
                                });
                            method.set_attr("__isabstractmethod__", self.attr("__isabstractmethod__"));
                            method.set_attr("register", self.attr("register"));
                            import_attribute("functools", "update_wrapper")(method, self.attr("func"));
                            return method;
                        }) },
                    { "__isabstractmethod__", isabstractmethod_property("singledispatchmethod") } },
                    "Single-dispatch generic method descriptor.\n\n    Supports wrapping existing descriptors.\n    ");
            }

            bool functools_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sFunctoolsFunctions)
                {
                    aAttributes.emplace_back(f.name, make_builtin(f));
                    // (cmp_to_key's docstring: CPython's (Modules/clinic/_functoolsmodule.c.h), with its text signature)
                    if (std::strcmp(f.name, "cmp_to_key") == 0)
                        as<function_object>(aAttributes.back().second)->doc = "cmp_to_key($module, /, mycmp)\n--\n\nConvert a cmp= function into a key= function.\n\n"
                            "  mycmp\n    Function that compares two objects.";
                }
                set_functions_module(aAttributes, "_functools", { "reduce", "cmp_to_key" });
                for (auto const& entry : sOrderings)
                    aAttributes.emplace_back(entry.function, new_reference(ordering_function(entry)));
                static bool const sRegistered = []()
                {
                    register_type_attributes("functools.partial", partial_type_attribute);
                    // (a subclass's instances: func, args and keywords are readonly (CPython's member descriptors))
                    native_base_hooks hooks;
                    hooks.setattr = [](object*, char const* aName, object*) -> int
                    {
                        if (std::strcmp(aName, "func") != 0 && std::strcmp(aName, "args") != 0 && std::strcmp(aName, "keywords") != 0)
                            return 1;
                        error("AttributeError", "readonly attribute");
                        return -1;
                    };
                    native_bases()["functools.partial"] = hooks;
                    return true;
                }();
                (void)sRegistered;
                aAttributes.emplace_back("partial", context()->type_new(context(), "partial", "functools.partial", new_partial, nullptr, partial_instancecheck));
                aAttributes.emplace_back("WRAPPER_ASSIGNMENTS", make_tuple({ make_str("__module__"), make_str("__name__"), make_str("__qualname__"),
                    make_str("__doc__"), make_str("__annotations__"), make_str("__type_params__") }));
                aAttributes.emplace_back("WRAPPER_UPDATES", make_tuple({ make_str("__dict__") }));
                // (_CacheInfo: cache_info()'s class (CPython's functools.py names it so))
                if (auto const cacheInfo = cache_info_class())
                    aAttributes.emplace_back("_CacheInfo", cacheInfo);
                else
                    return false;
                auto const classes = py_guarded([&]()
                {
                    static py const sPartialMethod = make_partialmethod_class();
                    static py const sSingleDispatchMethod = make_singledispatchmethod_class();
                    aAttributes.emplace_back("partialmethod", sPartialMethod.reference());
                    static py const sCachedProperty = make_cached_property_class();
                    aAttributes.emplace_back("cached_property", sCachedProperty.reference());
                    aAttributes.emplace_back("_NOT_FOUND", not_found().reference());
                    add_single_dispatch(aAttributes);
                    // (Lib/functools.py's imports: from types import GenericAlias; from _thread import RLock)
                    aAttributes.emplace_back("GenericAlias", import_attribute("types", "GenericAlias").reference());
                    aAttributes.emplace_back("RLock", import_attribute("_thread", "RLock").reference());
                    aAttributes.emplace_back("singledispatchmethod", sSingleDispatchMethod.reference());
                    return py::none();
                });
                if (classes == nullptr)
                    return false;
                release(classes);
                aAttributes.emplace_back("__all__", make_list({ make_str("update_wrapper"), make_str("wraps"), make_str("WRAPPER_ASSIGNMENTS"), make_str("WRAPPER_UPDATES"),
                    make_str("total_ordering"), make_str("cache"), make_str("cmp_to_key"), make_str("lru_cache"), make_str("reduce"), make_str("partial"),
                    make_str("partialmethod"), make_str("singledispatch"), make_str("singledispatchmethod"), make_str("cached_property") }));
                return true;
            }

            inline bool const sFunctools = register_module("functools", functools_module);
            // (CPython's C module _functools: functools's reduce, cmp_to_key and partial)
            inline bool const sFunctoolsC = register_module("_functools", [](std::vector<std::pair<std::string, object*>>& aAttributes)
                { return c_module_attributes("functools", "_functools", { "partial" }, aAttributes); });
        }
    }
}
