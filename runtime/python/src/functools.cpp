/*
  functools.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/functools.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module functools (CPython 3.13's behaviour): reduce, partial, lru_cache, cache, wraps, update_wrapper, total_ordering,
// cmp_to_key, cached_property and singledispatch

#include <cstring>
#include <functional>
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
                // (a partial of a partial: flattened)
                if (is_partial(aFunction))
                {
                    auto const& inner = as_partial(aFunction);
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
                    auto const& p = as_partial(aSelf);
                    std::string text;
                    if (!repr_of(p.function, text))
                        return nullptr;
                    for (auto a : as<tuple_object>(p.args)->items)
                    {
                        std::string item;
                        if (!repr_of(a, item))
                            return nullptr;
                        text += ", " + item;
                    }
                    for (auto const& item : as<dict_object>(p.keywords)->items)
                    {
                        std::string value;
                        if (!repr_of(item.second, value))
                            return nullptr;
                        text += ", " + as<str_object>(item.first)->value + "=" + value;
                    }
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
                    return error("AttributeError", std::string{ "'functools.partial' object has no attribute '" } + aName + "'");
                };
                type.setattr = [](neos_py_context*, object* aSelf, char const* aName, object* aValue) -> int
                {
                    auto& p = as_partial(aSelf);
                    if (std::strcmp(aName, "func") == 0 || std::strcmp(aName, "args") == 0 || std::strcmp(aName, "keywords") == 0)
                    {
                        error("AttributeError", "readonly attribute");
                        return -1;
                    }
                    for (auto& a : p.attributes)
                        if (a.first == aName)
                        {
                            retain(aValue);
                            release(a.second);
                            a.second = aValue;
                            return 0;
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

            object* cache_key(lru_object const& aLru, object* aArgs, object* aKwargs)
            {
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
                return make_tuple(std::move(key));
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
                    if (std::strcmp(aName, "cache_info") == 0)
                        return make_function(aName, lru_method, nullptr, aSelf);
                    if (std::strcmp(aName, "cache_clear") == 0)
                        return make_function(aName, lru_method, reinterpret_cast<void*>(1), aSelf);
                    if (std::strcmp(aName, "__wrapped__") == 0)
                        return borrowed(lru.function);
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

            object* update_wrapper(object* aWrapper, object* aWrapped)
            {
                for (auto name : sAssigned)
                {
                    ref const value{ getattr(aWrapped, name) };
                    if (!value)
                    {
                        clear_error();
                        continue;
                    }
                    if (setattr(aWrapper, name, value.get()) < 0)
                        clear_error();
                }
                ref const wrappedDict{ getattr(aWrapped, "__dict__") };
                if (wrappedDict && kind_of(wrappedDict.get()) == NEOS_PY_DICT)
                    for (auto const& item : as<dict_object>(wrappedDict.get())->items)
                        if (setattr(aWrapper, as<str_object>(item.first)->value.c_str(), item.second) < 0)
                            clear_error();
                clear_error();
                if (setattr(aWrapper, "__wrapped__", aWrapped) < 0)
                    clear_error();
                return borrowed(aWrapper);
            }

            object* wraps_decorator(neos_py_context*, void* aData, object* aArgs, object*)
            {
                auto const& args = as<tuple_object>(aArgs)->items;
                if (args.size() != 2u)
                    return error("TypeError", "update_wrapper() missing 1 required positional argument: 'wrapper'");
                (void)aData;
                return update_wrapper(args[1], args[0]);
            }

            // --- total_ordering

            struct ordering_entry { char const* root; char const* name; int kind; };

            // (the derived comparisons (CPython's _convert): kind 0: not r and != ; 1: r or ==; 2: not r; 3: not r or ==; 4: r and !=)
            ordering_entry const sOrderings[] =
            {
                { "__lt__", "__gt__", 0 }, { "__lt__", "__le__", 1 }, { "__lt__", "__ge__", 2 },
                { "__le__", "__ge__", 3 }, { "__le__", "__lt__", 4 }, { "__le__", "__gt__", 2 },
                { "__gt__", "__lt__", 0 }, { "__gt__", "__ge__", 1 }, { "__gt__", "__le__", 2 },
                { "__ge__", "__le__", 3 }, { "__ge__", "__gt__", 4 }, { "__ge__", "__lt__", 2 }
            };

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
                std::vector<std::string> roots;
                for (auto name : { "__lt__", "__le__", "__gt__", "__ge__" })
                {
                    ref const dict{ getattr(cls, "__mro__") };
                    bool defined = false;
                    if (dict && kind_of(dict.get()) == NEOS_PY_TUPLE)
                        for (auto c : as<tuple_object>(dict.get())->items)
                        {
                            if (!is_class(c))
                                continue;
                            ref const d{ getattr(c, "__dict__") };
                            ref const key{ make_str(name) };
                            if (d && kind_of(proxied_mapping(d.get())) == NEOS_PY_DICT && dict_find(proxied_mapping(d.get()), key.get()) >= 0)
                                defined = true;
                        }
                    clear_error();
                    if (defined)
                        roots.push_back(name);
                }
                if (roots.empty())
                    return error("ValueError", "must define at least one ordering operation: < > <= >=");
                auto const root = *std::max_element(roots.begin(), roots.end());
                for (auto const& entry : sOrderings)
                    if (root == entry.root && std::find(roots.begin(), roots.end(), entry.name) == roots.end())
                    {
                        ref const function{ make_function(entry.name, derived_comparison, const_cast<ordering_entry*>(&entry)) };
                        if (setattr(cls, entry.name, function.get()) < 0)
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
                type.call = [](neos_py_context*, object* aSelf, object* aArgs, object*) -> object*
                {
                    auto const& args = as<tuple_object>(aArgs)->items;
                    if (args.size() != 1u)
                        return error("TypeError", "K() takes exactly 1 argument");
                    auto const& k = *reinterpret_cast<key_object*>(aSelf);
                    return &(new key_object{ { { &sKeyType.base } }, borrowed(k.function), borrowed(args[0]) })->h.base;
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
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

            // --- singledispatch

            struct dispatch_object
            {
                header h;
                object* registry;       ///< (a dict: class -> function)
                object* function;       ///< (the default)
                std::vector<std::pair<std::string, object*>> attributes;
                ~dispatch_object() { release(registry); release(function); for (auto& a : attributes) release(a.second); }
            };

            neos_py_type make_dispatch_type();
            neos_py_type sDispatchType = make_dispatch_type();
            dispatch_object& as_dispatch(object* aObject) { return *reinterpret_cast<dispatch_object*>(aObject); }

            // (a class's MRO: of a user class its __mro__; of a builtin type: it (bool: and int) and object)
            bool mro_of(object* aClass, std::vector<ref>& aResult)
            {
                if (is_class(aClass))
                {
                    ref const mro{ getattr(aClass, "__mro__") };
                    if (!mro)
                        return false;
                    for (auto c : as<tuple_object>(mro.get())->items)
                        aResult.push_back(ref::borrowed(c));
                    return true;
                }
                aResult.push_back(ref::borrowed(aClass));
                ref const name{ getattr(aClass, "__name__") };
                clear_error();
                if (name && kind_of(name.get()) == NEOS_PY_STR)
                {
                    auto const& n = as<str_object>(name.get())->value;
                    if (n == "bool")
                        aResult.push_back(ref{ type_object_for("int", "int") });
                    if (n != "object")
                        aResult.push_back(ref{ type_object_for("object", "object") });
                }
                return true;
            }

            object* dispatch(dispatch_object& aDispatch, object* aClass)
            {
                std::vector<ref> mro;
                if (!mro_of(aClass, mro))
                    return nullptr;
                for (auto const& c : mro)
                {
                    auto const position = dict_find(aDispatch.registry, c.get());
                    if (position == -2)
                        return nullptr;
                    if (position >= 0)
                        return borrowed(as<dict_object>(aDispatch.registry)->items[static_cast<std::size_t>(position)].second);
                }
                return borrowed(aDispatch.function);
            }

            object* dispatch_method(neos_py_context*, void* aData, object* aArgs, object*)
            {
                auto const& args = as<tuple_object>(aArgs)->items;
                auto& d = as_dispatch(args.at(0));
                switch (reinterpret_cast<std::uintptr_t>(aData))
                {
                case 0:     // (register(cls, func=None): CPython's; cls a class or a union of classes, or (as a decorator) a function whose first 
                            // parameter's annotation is one of those)
                    {
                        if (args.size() < 2u || args.size() > 3u)
                            return error("TypeError", "register() takes 1 or 2 arguments");
                        return py_guarded([&]() -> py
                        {
                            py const self = py::borrowed(args[0]);
                            py cls = py::borrowed(args[1]);
                            py func = args.size() == 3u ? py::borrowed(args[2]) : py{};
                            auto const union_type = [](py const& aClass)
                            {
                                if (is_union(aClass.get()))
                                    return true;
                                py const origin = import_attribute("typing", "get_origin")(aClass);
                                return origin.is(import_attribute("typing", "Union")) || origin.is(import_attribute("types", "UnionType"));
                            };
                            auto const valid = [&](py const& aClass)
                            {
                                if (isinstance(aClass, builtin("type")))
                                    return true;
                                if (!union_type(aClass))
                                    return false;
                                for (auto const& a : import_attribute("typing", "get_args")(aClass).items())
                                    if (!isinstance(a, builtin("type")))
                                        return false;
                                return true;
                            };
                            if (valid(cls))
                            {
                                if (func.empty())
                                {
                                    // (a decorator (of the pair (dispatcher, cls), which it holds))
                                    py const pair = py_tuple({ self, cls });
                                    return py{ make_function("register", [](neos_py_context*, void* aPair, object* aArgs, object*) -> object*
                                    {
                                        auto const& f = as<tuple_object>(aArgs)->items;
                                        if (f.size() != 2u)
                                            return error("TypeError", "register decorator takes 1 argument");
                                        auto const& p = as<tuple_object>(static_cast<object*>(aPair))->items;
                                        ref const registerMethod{ getattr(p[0], "register") };
                                        if (!registerMethod)
                                            return nullptr;
                                        ref const callArgs{ make_tuple({ borrowed(p[1]), borrowed(f[1]) }) };
                                        return call(registerMethod.get(), callArgs.get(), nullptr);
                                    }, pair.get(), pair.get()) };
                                }
                            }
                            else
                            {
                                if (!func.empty())
                                    py_raise("TypeError", "Invalid first argument to `register()`. " + cls.repr() + " is not a class or union type.");
                                py annotations;
                                try
                                {
                                    annotations = cls.attr("__annotations__");
                                }
                                catch (raised const&)
                                {
                                    if (!caught({ "AttributeError" }))
                                        throw;
                                    py_take_exception();
                                    annotations = py_dict();
                                }
                                if (!annotations.truthy())
                                    py_raise("TypeError", "Invalid first argument to `register()`: " + cls.repr() + ". Use either `@register(some_class)` or plain `@register` on an annotated function.");
                                func = cls;
                                auto const items = builtin("list")(import_attribute("typing", "get_type_hints")(func).method("items")).items();
                                if (items.empty())
                                    py_raise("StopIteration", "");
                                py const argname = items[0][py{ 0 }];
                                cls = items[0][py{ 1 }];
                                if (!valid(cls))
                                {
                                    if (union_type(cls))
                                        py_raise("TypeError", "Invalid annotation for " + argname.repr() + ". " + cls.repr() + " not all arguments are classes.");
                                    py_raise("TypeError", "Invalid annotation for " + argname.repr() + ". " + cls.repr() + " is not a class.");
                                }
                            }
                            if (union_type(cls))
                                for (auto const& a : import_attribute("typing", "get_args")(cls).items())
                                    dict_set(d.registry, a.get(), func.get());
                            else
                                dict_set(d.registry, cls.get(), func.get());
                            return func;
                        });
                    }
                case 1:     // (dispatch(cls))
                    if (args.size() != 2u)
                        return error("TypeError", "dispatch() takes exactly 1 argument");
                    return dispatch(d, args[1]);
                default:
                    return nullptr;
                }
            }

            neos_py_type make_dispatch_type()
            {
                auto type = make_type("functools.singledispatch", "function", retain_object, release_object<dispatch_object>);
                type.call = [](neos_py_context*, object* aSelf, object* aArgs, object* aKwargs) -> object*
                {
                    auto& d = as_dispatch(aSelf);
                    auto const& args = as<tuple_object>(aArgs)->items;
                    if (args.empty())
                    {
                        ref const name{ getattr(d.function, "__name__") };
                        clear_error();
                        std::string const n = name && kind_of(name.get()) == NEOS_PY_STR ? as<str_object>(name.get())->value : "singledispatch function";
                        return error("TypeError", n + " requires at least 1 positional argument");
                    }
                    ref const cls{ class_of(args[0]) };
                    ref const function{ dispatch(d, cls.get()) };
                    if (!function)
                        return nullptr;
                    return call(function.get(), aArgs, aKwargs);
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto& d = as_dispatch(aSelf);
                    if (std::strcmp(aName, "register") == 0)
                        return make_function(aName, dispatch_method, nullptr, aSelf);
                    if (std::strcmp(aName, "dispatch") == 0)
                        return make_function(aName, dispatch_method, reinterpret_cast<void*>(1), aSelf);
                    if (std::strcmp(aName, "registry") == 0)
                        return borrowed(d.registry);
                    if (std::strcmp(aName, "__wrapped__") == 0)
                        return borrowed(d.function);
                    for (auto const& a : d.attributes)
                        if (a.first == aName)
                            return borrowed(a.second);
                    return getattr(d.function, aName);
                };
                type.setattr = [](neos_py_context*, object* aSelf, char const* aName, object* aValue) -> int
                {
                    auto& d = as_dispatch(aSelf);
                    for (auto& a : d.attributes)
                        if (a.first == aName)
                        {
                            retain(aValue);
                            release(a.second);
                            a.second = aValue;
                            return 0;
                        }
                    d.attributes.emplace_back(aName, borrowed(aValue));
                    return 0;
                };
                method_types().insert(&type.base);
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
                            clear_error();
                            return error("TypeError", "reduce() arg 2 must support iteration");
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
                        return update_wrapper(a[0], a[1]);
                    } },
                { "wraps", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("wraps", aArgs, aKwargs, { "wrapped", "assigned", "updated" }, 1u, 0u, a))
                            return nullptr;
                        return make_function("update_wrapper", [](neos_py_context* aContext, void*, object* aArgs, object* aKwargs) -> object*
                        {
                            return wraps_decorator(aContext, nullptr, aArgs, aKwargs);
                        }, nullptr, a[0]);
                    } },
                { "total_ordering", b_total_ordering },
                { "cmp_to_key", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("cmp_to_key", aArgs, aKwargs, { "mycmp" }, 1u, 0u, a))
                            return nullptr;
                        return &(new key_object{ { { &sKeyType.base } }, borrowed(a[0]), nullptr })->h.base;
                    } },
                { "cached_property", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return b_cached_property(aArgs, aKwargs); } },
                { "singledispatch", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("singledispatch", aArgs, aKwargs, { "func" }, 1u, 0u, a))
                            return nullptr;
                        ref registry{ make_dict() };
                        ref const objectType{ type_object_for("object", "object") };
                        dict_set(registry.get(), objectType.get(), a[0]);
                        auto const d = new dispatch_object{ { { &sDispatchType.base } }, registry.release_ownership(), borrowed(a[0]) };
                        ref result{ &d->h.base };
                        ref const r{ update_wrapper(result.get(), a[0]) };
                        return result.release_ownership();
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
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("partial", context()->type_new(context(), "partial", "functools.partial", new_partial, nullptr, partial_instancecheck));
                aAttributes.emplace_back("WRAPPER_ASSIGNMENTS", make_tuple({ make_str("__module__"), make_str("__name__"), make_str("__qualname__"),
                    make_str("__doc__"), make_str("__annotations__"), make_str("__type_params__") }));
                aAttributes.emplace_back("WRAPPER_UPDATES", make_tuple({ make_str("__dict__") }));
                auto const classes = py_guarded([&]()
                {
                    static py const sPartialMethod = make_partialmethod_class();
                    static py const sSingleDispatchMethod = make_singledispatchmethod_class();
                    aAttributes.emplace_back("partialmethod", sPartialMethod.reference());
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
        }
    }
}
