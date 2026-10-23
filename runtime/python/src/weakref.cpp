/*
  weakref.cpp

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

  Portions of this file are derived from CPython 3.13 (Objects/weakrefobject.c), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE).
*/

// Weak references (CPython 3.13's weakrefobject.c: ReferenceType, ProxyType and CallableProxyType, an object's weak references cleared and their
// callbacks called as it is destroyed), and the modules _weakref, _weakrefset and weakref (weakref.py: WeakMethod, KeyedRef, WeakValueDictionary,
// WeakKeyDictionary, WeakSet and finalize)

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "values.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        std::atomic<std::size_t> gWeaklyReferenced{ 0u };

        namespace
        {
            enum weakref_kind { Reference, Proxy, CallableProxy };

            struct weakref_object;

            // (the weak references of the objects that have them (in CPython's order: the basic reference (one without a callback), the basic proxy,
            // then the others))
            std::recursive_mutex sMutex;
            std::unordered_map<object*, std::vector<weakref_object*>> sReferents;
            // (the types of the objects that have (or had) weak references: an object of another type has none)
            std::array<std::atomic<neos_rt_object_type const*>, 32u> sTypes{};
            std::atomic<bool> sTypesOverflowed{ false };

            struct weakref_object
            {
                header h;
                weakref_kind kind;
                object* referent = nullptr;     ///< (not owned; null: dead)
                object* callback = nullptr;
                object* owner = nullptr;        ///< (not owned: the instance of a class that derives from ReferenceType whose value it is)
                bool hashed = false;
                long long hash = 0;
                void detach();
                ~weakref_object()
                {
                    detach();
                    release(callback);
                }
            };

            void weakref_object::detach()
            {
                std::lock_guard<std::recursive_mutex> lock{ sMutex };
                if (referent == nullptr)
                    return;
                auto const r = sReferents.find(referent);
                if (r != sReferents.end())
                {
                    std::erase(r->second, this);
                    if (r->second.empty())
                    {
                        sReferents.erase(r);
                        gWeaklyReferenced.fetch_sub(1u, std::memory_order_relaxed);
                    }
                }
                referent = nullptr;
            }

            void note_type(neos_rt_object_type const* aType)
            {
                for (auto& t : sTypes)
                {
                    auto const existing = t.load(std::memory_order_relaxed);
                    if (existing == aType)
                        return;
                    if (existing == nullptr)
                    {
                        t.store(aType, std::memory_order_relaxed);
                        return;
                    }
                }
                sTypesOverflowed.store(true, std::memory_order_relaxed);
            }

            bool noted_type(neos_rt_object_type const* aType)
            {
                if (sTypesOverflowed.load(std::memory_order_relaxed))
                    return true;
                for (auto const& t : sTypes)
                {
                    auto const existing = t.load(std::memory_order_relaxed);
                    if (existing == aType)
                        return true;
                    if (existing == nullptr)
                        return false;
                }
                return false;
            }

            neos_py_type make_weakref_type(weakref_kind aKind);
            neos_py_type sReferenceType = make_weakref_type(Reference);
            neos_py_type sProxyType = make_weakref_type(Proxy);
            neos_py_type sCallableProxyType = make_weakref_type(CallableProxy);

            bool is_weakref(object* aObject)
            {
                return aObject != nullptr && (aObject->type == &sReferenceType.base || aObject->type == &sProxyType.base || aObject->type == &sCallableProxyType.base);
            }
            bool is_proxy(object* aObject) { return aObject != nullptr && (aObject->type == &sProxyType.base || aObject->type == &sCallableProxyType.base); }
            weakref_object& as_weakref(object* aObject) { return *reinterpret_cast<weakref_object*>(aObject); }

            // (a weak reference's referent: a new reference, or null (dead))
            object* referent_of(weakref_object& aWeakref)
            {
                std::lock_guard<std::recursive_mutex> lock{ sMutex };
                if (aWeakref.referent == nullptr)
                    return nullptr;
                // (a referent that is being destroyed is dead)
                auto& references = reinterpret_cast<header*>(aWeakref.referent)->references;
                for (auto n = references.load(std::memory_order_acquire); n > 0;)
                    if (references.compare_exchange_weak(n, n + 1, std::memory_order_acq_rel))
                        return aWeakref.referent;
                return nullptr;
            }

            // (the object that represents a weak reference: its owner (an instance of a subclass), if it has one)
            object* representative(weakref_object& aWeakref) { return aWeakref.owner != nullptr ? aWeakref.owner : &aWeakref.h.base; }

            bool weakly_referenceable(object* aObject)
            {
                switch (kind_of(aObject))
                {
                case NEOS_PY_OTHER:
                case NEOS_PY_FUNCTION:
                case NEOS_PY_ITERATOR:
                    break;
                default:
                    return false;
                }
                if (is_weakref(aObject))
                    return false;
                if (is_instance(aObject))
                {
                    // (its class and its bases each with __slots__ without __weakref__ (or __dict__): none)
                    ref const cls{ class_of(aObject) };
                    ref const mro{ getattr(cls.get(), "__mro__") };
                    if (!mro || kind_of(mro.get()) != NEOS_PY_TUPLE)
                    {
                        clear_error();
                        return true;
                    }
                    for (auto c : as<tuple_object>(mro.get())->items)
                    {
                        if (!is_class(c))
                            continue;
                        ref const ns{ getattr(c, "__dict__") };
                        ref const key{ make_str("__slots__") };
                        ref const slots{ ns ? getitem(ns.get(), key.get()) : nullptr };
                        if (!slots)
                        {
                            clear_error();
                            return true;
                        }
                        std::vector<object*> names;
                        bool weak = false;
                        if (kind_of(slots.get()) == NEOS_PY_STR)
                            weak = as<str_object>(slots.get())->value == "__weakref__" || as<str_object>(slots.get())->value == "__dict__";
                        else if (collect(slots.get(), names))
                            for (auto n : names)
                            {
                                weak = weak || (kind_of(n) == NEOS_PY_STR && (as<str_object>(n)->value == "__weakref__" || as<str_object>(n)->value == "__dict__"));
                                release(n);
                            }
                        clear_error();
                        if (weak)
                            return true;
                    }
                    return false;
                }
                static char const* const sNot[] = { "bytes", "bytearray", "object", "exception", "decimal.Decimal", "datetime.date", "datetime.datetime",
                    "datetime.time", "datetime.timedelta", "datetime.timezone", "property", "types.SimpleNamespace", "collections.defaultdict", "mappingproxy",
                    "cell", "range_iterator", "list_iterator", "tuple_iterator", "str_iterator", "dict_keyiterator", "dict_valueiterator", "dict_itemiterator",
                    "set_iterator", "bytes_iterator", "bytearray_iterator", "slice", "staticmethod", "classmethod" };
                std::string_view const name{ type_name(aObject) };
                if (name.starts_with("itertools."))
                    return false;
                return std::none_of(std::begin(sNot), std::end(sNot), [&](char const* n) { return name == n; });
            }

            bool is_basic(weakref_object const* aWeakref, weakref_kind aKind)
            {
                return aWeakref->callback == nullptr && aWeakref->owner == nullptr && (aKind == Reference ? aWeakref->kind == Reference : aWeakref->kind != Reference);
            }

            // (get_or_create_weakref: aShared: a callback-less reference or proxy is the object's one)
            object* weak_reference(weakref_kind aKind, object* aObject, object* aCallback, bool aShared)
            {
                if (!weakly_referenceable(aObject))
                    return error("TypeError", std::string{ "cannot create weak reference to '" } + type_name(aObject) + "' object");
                if (aCallback == none())
                    aCallback = nullptr;
                std::lock_guard<std::recursive_mutex> lock{ sMutex };
                auto& list = sReferents[aObject];
                if (list.empty())
                {
                    gWeaklyReferenced.fetch_add(1u, std::memory_order_relaxed);
                    note_type(aObject->type);
                }
                // (the basic reference and proxy)
                weakref_object* basicRef = !list.empty() && is_basic(list[0], Reference) ? list[0] : nullptr;
                std::size_t const proxyIndex = basicRef != nullptr ? 1u : 0u;
                weakref_object* basicProxy = list.size() > proxyIndex && is_basic(list[proxyIndex], Proxy) ? list[proxyIndex] : nullptr;
                if (aShared && aCallback == nullptr)
                {
                    auto const candidate = aKind == Reference ? basicRef : basicProxy;
                    if (candidate != nullptr && candidate->h.references.load(std::memory_order_acquire) > 0)
                        return new_reference(&candidate->h.base);
                }
                auto const& type = aKind == Reference ? sReferenceType : aKind == Proxy ? sProxyType : sCallableProxyType;
                auto const result = new weakref_object{ { { &type.base } }, aKind, aObject, aCallback != nullptr ? new_reference(aCallback) : nullptr };
                if (aShared && aCallback == nullptr && aKind == Reference)
                    list.insert(list.begin(), result);
                else if (aShared && aCallback == nullptr)
                    list.insert(list.begin() + (basicRef != nullptr ? 1 : 0), result);
                else
                    list.insert(list.begin() + (basicRef != nullptr ? 1 : 0) + (basicProxy != nullptr ? 1 : 0), result);
                return &result->h.base;
            }

            // --- ReferenceType, ProxyType and CallableProxyType

            object* dead_proxy() { return error("ReferenceError", "weakly-referenced object no longer exists"); }

            // (a proxy's referent (a new reference; null: ReferenceError), or another object (a new reference))
            object* unwrapped(object* aObject)
            {
                if (!is_proxy(aObject))
                    return new_reference(aObject);
                auto const r = referent_of(as_weakref(aObject));
                return r != nullptr ? r : dead_proxy();
            }

            std::string referent_name(object* aReferent)
            {
                // (_PyObject_LookupSpecial(__name__): a class's, a function's or a method's name)
                if (!is_class(aReferent) && !is_type_object(aReferent) && kind_of(aReferent) != NEOS_PY_FUNCTION &&
                    std::string_view{ type_name(aReferent) } != "function" && std::string_view{ type_name(aReferent) } != "method")
                    return {};
                ref const name{ getattr(aReferent, "__name__") };
                if (!name || kind_of(name.get()) != NEOS_PY_STR)
                {
                    clear_error();
                    return {};
                }
                return as<str_object>(name.get())->value;
            }

            template <typename F>
            auto with_referent(object* aSelf, F aFunction) -> decltype(aFunction(static_cast<object*>(nullptr)))
            {
                ref const r{ referent_of(as_weakref(aSelf)) };
                if (!r)
                    return (dead_proxy(), decltype(aFunction(static_cast<object*>(nullptr))){});
                return aFunction(r.get());
            }

            neos_py_type make_weakref_type(weakref_kind aKind)
            {
                auto const qualified = aKind == Reference ? "weakref.ReferenceType" : aKind == Proxy ? "weakref.ProxyType" : "weakref.CallableProxyType";
                auto type = make_type(qualified, qualified, retain_object, release_object<weakref_object>);
                if (aKind == Reference)
                {
                    type.repr = [](neos_py_context*, object* aSelf) -> object*
                    {
                        ref const r{ referent_of(as_weakref(aSelf)) };
                        if (!r)
                            return make_str("<weakref at " + object_address(aSelf) + "; dead>");
                        auto const name = referent_name(r.get());
                        return make_str("<weakref at " + object_address(aSelf) + "; to '" + std::string{ type_name(r.get()) } + "' at " + object_address(r.get()) +
                            (name.empty() ? std::string{} : " (" + name + ")") + ">");
                    };
                    type.call = [](neos_py_context*, object* aSelf, object* aArgs, object* aKwargs) -> object*
                    {
                        if (!no_keywords(aKwargs, "weakref"))
                            return nullptr;
                        if (auto const n = as<tuple_object>(aArgs)->items.size(); n != 0u)
                            return error("TypeError", "weakref expected 0 arguments, got " + std::to_string(n));
                        auto const r = referent_of(as_weakref(aSelf));
                        return r != nullptr ? r : new_reference(none());
                    };
                    type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int
                    {
                        auto& self = as_weakref(aSelf);
                        if (!self.hashed)
                        {
                            ref const r{ referent_of(self) };
                            if (!r)
                            {
                                error("TypeError", "weak object has gone away");
                                return -1;
                            }
                            if (!hash_of(r.get(), self.hash))
                                return -1;
                            self.hashed = true;
                        }
                        *aResult = self.hash;
                        return 0;
                    };
                    type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                    {
                        if ((aOperation != NEOS_PY_EQ && aOperation != NEOS_PY_NE) || !is_weakref(aLhs) || !is_weakref(aRhs))
                            return not_implemented();
                        ref const a{ referent_of(as_weakref(aLhs)) };
                        ref const b{ referent_of(as_weakref(aRhs)) };
                        if (!a || !b)
                            return make_bool((aLhs == aRhs) == (aOperation == NEOS_PY_EQ));
                        return compare(aOperation, a.get(), b.get());
                    };
                    type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                    {
                        if (std::strcmp(aName, "__callback__") == 0)
                            return new_reference(as_weakref(aSelf).callback != nullptr ? as_weakref(aSelf).callback : none());
                        return nullptr;
                    };
                    return type;
                }
                // (a proxy: its operations are its referent's)
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    ref const r{ referent_of(as_weakref(aSelf)) };
                    if (!r)
                        return make_str("<weakproxy at " + object_address(aSelf) + "; dead>");
                    return make_str("<weakproxy at " + object_address(aSelf) + "; to '" + std::string{ type_name(r.get()) } + "' at " + object_address(r.get()) + ">");
                };
                type.str = [](neos_py_context*, object* aSelf) -> object*
                {
                    return with_referent(aSelf, [](object* r) -> object* { std::string text; return str_of(r, text) ? make_str(text) : nullptr; });
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    return with_referent(aSelf, [aName](object* r) -> object* { return getattr(r, aName); });
                };
                type.setattr = [](neos_py_context*, object* aSelf, char const* aName, object* aValue) -> int
                {
                    ref const r{ referent_of(as_weakref(aSelf)) };
                    if (!r)
                        return (dead_proxy(), -1);
                    return setattr(r.get(), aName, aValue);
                };
                if (aKind == CallableProxy)
                    type.call = [](neos_py_context*, object* aSelf, object* aArgs, object* aKwargs) -> object*
                    {
                        return with_referent(aSelf, [&](object* r) -> object* { return call(r, aArgs, aKwargs); });
                    };
                type.getitem = [](neos_py_context*, object* aSelf, object* aKey) -> object*
                {
                    ref const key{ unwrapped(aKey) };
                    if (!key)
                        return nullptr;
                    return with_referent(aSelf, [&](object* r) -> object* { return getitem(r, key.get()); });
                };
                type.setitem = [](neos_py_context*, object* aSelf, object* aKey, object* aValue) -> int
                {
                    ref const r{ referent_of(as_weakref(aSelf)) };
                    if (!r)
                        return (dead_proxy(), -1);
                    return setitem(r.get(), aKey, aValue);
                };
                type.binary = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs, int aInplace) -> object*
                {
                    ref const a{ unwrapped(aLhs) };
                    if (!a)
                        return nullptr;
                    ref const b{ unwrapped(aRhs) };
                    if (!b)
                        return nullptr;
                    return binary(aOperation, a.get(), b.get(), aInplace != 0);
                };
                type.unary = [](neos_py_context*, int aOperation, object* aSelf) -> object*
                {
                    return with_referent(aSelf, [&](object* r) -> object* { return unary(aOperation, r); });
                };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    ref const a{ unwrapped(aLhs) };
                    if (!a)
                        return nullptr;
                    ref const b{ unwrapped(aRhs) };
                    if (!b)
                        return nullptr;
                    return compare(aOperation, a.get(), b.get());
                };
                type.truth = [](neos_py_context*, object* aSelf) -> int
                {
                    ref const r{ referent_of(as_weakref(aSelf)) };
                    if (!r)
                        return (dead_proxy(), -1);
                    return truth(r.get());
                };
                type.length = [](neos_py_context*, object* aSelf) -> long long
                {
                    ref const r{ referent_of(as_weakref(aSelf)) };
                    if (!r)
                        return (dead_proxy(), -1);
                    return length(r.get());
                };
                type.iter = [](neos_py_context*, object* aSelf) -> object*
                {
                    return with_referent(aSelf, [](object* r) -> object* { return iter(r); });
                };
                type.next = [](neos_py_context*, object* aSelf) -> object*
                {
                    return with_referent(aSelf, [](object* r) -> object*
                    {
                        auto const t = python_type(r);
                        if (t == nullptr || (t->next == nullptr && kind_of(r) != NEOS_PY_ITERATOR))
                            return error("TypeError", std::string{ "Weakref proxy referenced a non-iterator '" } + type_name(r) + "' object");
                        return next(r);
                    });
                };
                type.contains = [](neos_py_context*, object* aSelf, object* aItem) -> int
                {
                    ref const r{ referent_of(as_weakref(aSelf)) };
                    if (!r)
                        return (dead_proxy(), -1);
                    return contains(r.get(), aItem);
                };
                type.index = [](neos_py_context*, object* aSelf, long long* aResult) -> int
                {
                    ref const r{ referent_of(as_weakref(aSelf)) };
                    if (!r)
                        return (dead_proxy(), -1);
                    return to_index(r.get(), *aResult) ? 0 : -1;
                };
                type.to_float = [](neos_py_context*, object* aSelf, double* aResult) -> int
                {
                    ref const r{ referent_of(as_weakref(aSelf)) };
                    if (!r)
                        return (dead_proxy(), -1);
                    return to_double(r.get(), *aResult) ? 0 : -1;
                };
                type.hash = [](neos_py_context*, object* aSelf, long long*) -> int
                {
                    error("TypeError", std::string{ "unhashable type: '" } + type_name(aSelf) + "'");
                    return -1;
                };
                return type;
            }

            // (an object's weak references (their representatives): new references)
            std::vector<object*> weak_references_of(object* aObject)
            {
                std::lock_guard<std::recursive_mutex> lock{ sMutex };
                std::vector<object*> result;
                if (auto const r = sReferents.find(aObject); r != sReferents.end())
                    for (auto w : r->second)
                        if (w->h.references.load(std::memory_order_acquire) > 0)
                            result.push_back(new_reference(representative(*w)));
                return result;
            }

            // --- the hooks of the classes that derive from ReferenceType (e.g. WeakMethod)

            object* subclass_reference(object* aArgs, object* aKwargs, object* aOwner)
            {
                auto const& args = tuple_items(aArgs);
                if (args.empty() || args.size() > 2u)
                    return error("TypeError", std::string{ "__new__ expected " } + (args.empty() ? "at least 1 argument, got 0" : "at most 2 arguments, got " +
                        std::to_string(args.size())));
                (void)aKwargs;
                auto const result = weak_reference(Reference, args[0], args.size() > 1u ? args[1] : nullptr, false);
                if (result != nullptr)
                    as_weakref(result).owner = aOwner;
                return result;
            }

            void disown_reference(object* aValue)
            {
                if (aValue != nullptr && aValue->type == &sReferenceType.base)
                    as_weakref(aValue).owner = nullptr;
            }

            object* reference_new(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                auto const& args = tuple_items(aArgs);
                if (!no_keywords(aKwargs, "ref"))
                    return nullptr;
                if (args.empty() || args.size() > 2u)
                    return error("TypeError", std::string{ "__new__ expected " } + (args.empty() ? "at least 1 argument, got 0" : "at most 2 arguments, got " +
                        std::to_string(args.size())));
                return weak_reference(Reference, args[0], args.size() > 1u ? args[1] : nullptr, true);
            }

            object* cannot_create(neos_py_context*, void* aData, object*, object*)
            {
                return error("TypeError", std::string{ "cannot create '" } + static_cast<char const*>(aData) + "' instances");
            }

            int reference_instancecheck(neos_py_context*, void*, object* aObject) { return aObject != nullptr && aObject->type == &sReferenceType.base ? 1 : 0; }
            int proxy_instancecheck(neos_py_context*, void*, object* aObject) { return aObject != nullptr && aObject->type == &sProxyType.base ? 1 : 0; }
            int callable_proxy_instancecheck(neos_py_context*, void*, object* aObject) { return aObject != nullptr && aObject->type == &sCallableProxyType.base ? 1 : 0; }

            py reference_type() { return py{ context()->type_new(context(), "ReferenceType", "weakref.ReferenceType", reference_new, nullptr, reference_instancecheck) }; }
            py proxy_type()
            {
                return py{ context()->type_new(context(), "ProxyType", "weakref.ProxyType", cannot_create, const_cast<char*>("weakref.ProxyType"), proxy_instancecheck) };
            }
            py callable_proxy_type()
            {
                return py{ context()->type_new(context(), "CallableProxyType", "weakref.CallableProxyType", cannot_create, const_cast<char*>("weakref.CallableProxyType"),
                    callable_proxy_instancecheck) };
            }

            bool const sHooks = []()
            {
                native_bases()["weakref.ReferenceType"] = native_base_hooks{ subclass_reference, disown_reference };
                return true;
            }();

            py weak_ref(py const& aObject, py const& aCallback = py{}) { return py{ weak_reference(Reference, aObject.get(), aCallback.empty() ? nullptr : aCallback.get(), true) }; }

            // --- _weakref

            builtin_entry const sWeakrefFunctions[] =
            {
                { "getweakrefcount", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("getweakrefcount", aArgs, aKwargs, { "object" }, 1u, 1u, a))
                            return nullptr;
                        std::lock_guard<std::recursive_mutex> lock{ sMutex };
                        auto const r = sReferents.find(a[0]);
                        return make_int(r == sReferents.end() ? 0 : static_cast<long long>(r->second.size()));
                    } },
                { "getweakrefs", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("getweakrefs", aArgs, aKwargs, { "object" }, 1u, 1u, a))
                            return nullptr;
                        return make_list(weak_references_of(a[0]));
                    } },
                { "proxy", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("proxy", aArgs, aKwargs, { "object", "callback" }, 1u, 2u, a))
                            return nullptr;
                        return weak_reference(is_callable(a[0]) ? CallableProxy : Proxy, a[0], a[1], true);
                    } },
                { "_remove_dead_weakref", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("_remove_dead_weakref", aArgs, aKwargs, { "dct", "key" }, 2u, 2u, a))
                            return nullptr;
                        if (kind_of(a[0]) != NEOS_PY_DICT)
                            return error("TypeError", std::string{ "_remove_dead_weakref() argument 1 must be dict, not " } + type_name(a[0]));
                        auto const position = dict_find(a[0], a[1]);
                        if (position == -2)
                            return nullptr;
                        if (position >= 0)
                        {
                            auto const value = builtin_value(as<dict_object>(a[0])->items[static_cast<std::size_t>(position)].second);
                            if (!is_weakref(value))
                                return error("TypeError", "not a weakref");
                            ref const r{ referent_of(as_weakref(value)) };
                            if (!r)
                                dict_erase(*as<dict_object>(a[0]), static_cast<std::size_t>(position));
                        }
                        return new_reference(none());
                    } }
            };

            bool weakref_internal_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sWeakrefFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                return py_guarded([&]()
                {
                    auto const ref = reference_type();
                    aAttributes.emplace_back("ref", ref.reference());
                    aAttributes.emplace_back("ReferenceType", ref.reference());
                    aAttributes.emplace_back("ProxyType", proxy_type().release());
                    aAttributes.emplace_back("CallableProxyType", callable_proxy_type().release());
                    return py::none();
                }) != nullptr && (release(none()), true);
            }

            inline bool const sWeakrefInternal = register_module("_weakref", weakref_internal_module);

            // --- weakref (weakref.py)

            std::vector<py> self_arguments_of(char const* aFunction, std::vector<object*> const& aArgs, object* aKwargs, std::vector<char const*> const& aNames,
                std::size_t aRequired, std::size_t aPositionalOnly = 0u)
            {
                if (aArgs.empty())
                    py_raise("TypeError", std::string{ aFunction } + "() missing 1 required positional argument: 'self'");
                // (scratch kept for reuse (a parse is made per WeakValueDictionary, WeakKeyDictionary and WeakSet operation))
                thread_local std::vector<object*> tRest;
                tRest.assign(aArgs.begin() + 1, aArgs.end());
                thread_local std::vector<object*> tA;
                tA.clear();
                py_checked(parse_arguments(aFunction, tRest, aKwargs, aNames, aRequired, aPositionalOnly, tA));
                std::vector<py> result{ py::borrowed(aArgs[0]) };
                for (auto v : tA)
                    result.push_back(v != nullptr ? py::borrowed(v) : py{});
                return result;
            }

            struct weak_class
            {
                object* keyedRef = nullptr;
                object* weakMethod = nullptr;
                object* weakValueDictionary = nullptr;
                object* weakKeyDictionary = nullptr;
                object* weakSet = nullptr;
                object* finalize = nullptr;
                object* info = nullptr;
            } sClasses;

            py data_of(py const& aSelf) { return aSelf.attr("data"); }

            void commit_removals(py const& aSelf, bool aDictionaryValues)
            {
                auto const pending = aSelf.attr("_pending_removals");
                auto const d = aSelf.attr("data");
                while (pending.size() > 0)
                {
                    auto const key = pending.method("pop");
                    if (aDictionaryValues)
                        import_attribute("_weakref", "_remove_dead_weakref")(d, key);
                    else if (isinstance(d, builtin("set")))
                        d.method("discard", { key });
                    else
                    {
                        try
                        {
                            d.method("__delitem__", { key });
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "KeyError" }))
                                throw;
                        }
                    }
                }
            }

            void commit_if_pending(py const& aSelf, bool aDictionaryValues)
            {
                if (aSelf.attr("_pending_removals").truthy())
                    commit_removals(aSelf, aDictionaryValues);
            }

            py snapshot(std::vector<py> const& aItems) { return py{ iter(py_list(aItems).get()) }; }

            py repr_at(py const& aSelf)
            {
                return py{ "<" + aSelf.attr("__class__").attr("__name__").str() + " at 0x" +
                    [&]() { char text[32]; std::snprintf(text, sizeof text, "%llx", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(aSelf.get())));
                        return std::string{ text }; }() + ">" };
            }

            // (the callback that removes a dead weak reference from a container (a weak reference to it))
            py remover(py const& aSelf, char const* aQualname, int aKind)
            {
                auto const selfref = weak_ref(aSelf);
                return make_native_function("remove", aQualname, "", "weakref", [selfref, aKind](std::vector<py> const& aArgs, object*) -> py
                {
                    if (aArgs.empty())
                        py_raise("TypeError", "remove() missing 1 required positional argument");
                    auto const wr = aArgs[0];
                    auto const self = selfref();
                    if (self.is_none())
                        return py::none();
                    if (self.attr("_iterating").truthy())
                        self.attr("_pending_removals").method("append", { aKind == 0 ? wr.attr("key") : wr });
                    else if (aKind == 0)
                        import_attribute("_weakref", "_remove_dead_weakref")(self.attr("data"), wr.attr("key"));
                    else if (aKind == 1)
                    {
                        try
                        {
                            self.attr("data").method("__delitem__", { wr });
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "KeyError" }))
                                throw;
                        }
                    }
                    else
                        self.attr("data").method("discard", { wr });
                    return py::none();
                });
            }

            py keyed_ref(py const& aValue, py const& aCallback, py const& aKey) { return py::borrowed(sClasses.keyedRef)(aValue, aCallback, aKey); }

            void init_container(py const& aSelf, py const& aData, char const* aQualname, int aKind)
            {
                aSelf.set_attr("data", aData);
                aSelf.set_attr("_remove", remover(aSelf, aQualname, aKind));
                aSelf.set_attr("_pending_removals", py_list());
                aSelf.set_attr("_iterating", builtin("set")());
            }

            // (WeakValueDictionary)

            py wvd_update(py const& aSelf, py aOther, object* aKwargs)
            {
                commit_if_pending(aSelf, true);
                auto const d = data_of(aSelf);
                auto const remove = aSelf.attr("_remove");
                if (!aOther.empty() && !aOther.is_none())
                {
                    if (auto const items = getattr(aOther.get(), "items"); items == nullptr)
                    {
                        clear_error();
                        aOther = builtin("dict")(aOther);
                    }
                    else
                        release(items);
                    for (auto const& item : aOther.method("items").items())
                        d.set_item(item[py{ 0 }], keyed_ref(item[py{ 1 }], remove, item[py{ 0 }]));
                }
                if (aKwargs != nullptr)
                    for (auto const& [key, value] : as<dict_object>(aKwargs)->items)
                        d.set_item(py::borrowed(key), keyed_ref(py::borrowed(value), remove, py::borrowed(key)));
                return py::none();
            }

            builtin_entry const sWeakValueDictionaryMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        if (aArgs.size() > 2u)
                            py_raise("TypeError", "WeakValueDictionary.__init__() takes from 1 to 2 positional arguments but " + std::to_string(aArgs.size()) + " were given");
                        auto const self = py::borrowed(aArgs.at(0u));
                        init_container(self, py_dict(), "WeakValueDictionary.__init__.<locals>.remove", 0);
                        return wvd_update(self, aArgs.size() > 1u ? py::borrowed(aArgs[1]) : py_tuple(), aKwargs);
                    }); } },
                { "_commit_removals", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py { commit_removals(py::borrowed(aArgs.at(0u)), true); return py::none(); }); } },
                { "__getitem__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__getitem__", aArgs, aKwargs, { "key" }, 1u, 1u);
                        commit_if_pending(a[0], true);
                        auto const o = data_of(a[0])[a[1]]();
                        if (o.is_none())
                            py_raise(py{ exception_class("KeyError") }(a[1]));
                        return o;
                    }); } },
                { "__delitem__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__delitem__", aArgs, aKwargs, { "key" }, 1u, 1u);
                        commit_if_pending(a[0], true);
                        data_of(a[0]).method("__delitem__", { a[1] });
                        return py::none();
                    }); } },
                { "__len__", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        auto const self = py::borrowed(aArgs.at(0u));
                        commit_if_pending(self, true);
                        return py{ data_of(self).size() };
                    }); } },
                { "__contains__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__contains__", aArgs, aKwargs, { "key" }, 1u, 1u);
                        commit_if_pending(a[0], true);
                        py o;
                        try
                        {
                            o = data_of(a[0])[a[1]]();
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "KeyError" }))
                                throw;
                            return py::boolean(false);
                        }
                        return py::boolean(!o.is_none());
                    }); } },
                { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py { return repr_at(py::borrowed(aArgs.at(0u))); }); } },
                { "__setitem__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__setitem__", aArgs, aKwargs, { "key", "value" }, 2u, 2u);
                        commit_if_pending(a[0], true);
                        data_of(a[0]).set_item(a[1], keyed_ref(a[2], a[0].attr("_remove"), a[1]));
                        return py::none();
                    }); } },
                { "copy", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        auto const self = py::borrowed(aArgs.at(0u));
                        commit_if_pending(self, true);
                        auto const result = py::borrowed(sClasses.weakValueDictionary)();
                        for (auto const& item : data_of(self).method("items").items())
                        {
                            auto const o = item[py{ 1 }]();
                            if (!o.is_none())
                                result.set_item(item[py{ 0 }], o);
                        }
                        return result;
                    }); } },
                { "__deepcopy__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__deepcopy__", aArgs, aKwargs, { "memo" }, 1u);
                        commit_if_pending(a[0], true);
                        auto const result = a[0].attr("__class__")();
                        auto const deepcopy = import_attribute("copy", "deepcopy");
                        for (auto const& item : data_of(a[0]).method("items").items())
                        {
                            auto const o = item[py{ 1 }]();
                            if (!o.is_none())
                                result.set_item(deepcopy(item[py{ 0 }], a[1]), o);
                        }
                        return result;
                    }); } },
                { "get", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("get", aArgs, aKwargs, { "key", "default" }, 1u);
                        commit_if_pending(a[0], true);
                        auto const fallback = a[2].empty() ? py::none() : a[2];
                        py wr;
                        try
                        {
                            wr = data_of(a[0])[a[1]];
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "KeyError" }))
                                throw;
                            return fallback;
                        }
                        auto const o = wr();
                        return o.is_none() ? fallback : o;
                    }); } },
                { "items", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        auto const self = py::borrowed(aArgs.at(0u));
                        commit_if_pending(self, true);
                        std::vector<py> result;
                        for (auto const& item : data_of(self).method("items").items())
                            if (auto const v = item[py{ 1 }](); !v.is_none())
                                result.push_back(py_tuple({ item[py{ 0 }], v }));
                        return snapshot(result);
                    }); } },
                { "keys", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        auto const self = py::borrowed(aArgs.at(0u));
                        commit_if_pending(self, true);
                        std::vector<py> result;
                        for (auto const& item : data_of(self).method("items").items())
                            if (!item[py{ 1 }]().is_none())
                                result.push_back(item[py{ 0 }]);
                        return snapshot(result);
                    }); } },
                { "itervaluerefs", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        auto const self = py::borrowed(aArgs.at(0u));
                        commit_if_pending(self, true);
                        return snapshot(data_of(self).method("values").items());
                    }); } },
                { "values", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        auto const self = py::borrowed(aArgs.at(0u));
                        commit_if_pending(self, true);
                        std::vector<py> result;
                        for (auto const& wr : data_of(self).method("values").items())
                            if (auto const o = wr(); !o.is_none())
                                result.push_back(o);
                        return snapshot(result);
                    }); } },
                { "popitem", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        auto const self = py::borrowed(aArgs.at(0u));
                        commit_if_pending(self, true);
                        for (;;)
                        {
                            auto const item = data_of(self).method("popitem");
                            auto const o = item[py{ 1 }]();
                            if (!o.is_none())
                                return py_tuple({ item[py{ 0 }], o });
                        }
                    }); } },
                { "pop", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        if (aArgs.size() < 2u || aArgs.size() > 3u)
                            py_raise("TypeError", aArgs.size() < 2u ? "WeakValueDictionary.pop() missing 1 required positional argument: 'key'" :
                                "WeakValueDictionary.pop() takes from 2 to 3 positional arguments but " + std::to_string(aArgs.size()) + " were given");
                        auto const self = py::borrowed(aArgs[0]);
                        auto const key = py::borrowed(aArgs[1]);
                        commit_if_pending(self, true);
                        py o;
                        try
                        {
                            o = data_of(self).method("pop", { key })();
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "KeyError" }))
                                throw;
                            o = py::none();
                        }
                        if (o.is_none())
                        {
                            if (aArgs.size() > 2u)
                                return py::borrowed(aArgs[2]);
                            py_raise(py{ exception_class("KeyError") }(key));
                        }
                        return o;
                    }); } },
                { "setdefault", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("setdefault", aArgs, aKwargs, { "key", "default" }, 1u);
                        auto const fallback = a[2].empty() ? py::none() : a[2];
                        py o;
                        try
                        {
                            o = data_of(a[0])[a[1]]();
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "KeyError" }))
                                throw;
                            o = py::none();
                        }
                        if (o.is_none())
                        {
                            commit_if_pending(a[0], true);
                            data_of(a[0]).set_item(a[1], keyed_ref(fallback, a[0].attr("_remove"), a[1]));
                            return fallback;
                        }
                        return o;
                    }); } },
                { "update", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        if (aArgs.size() > 2u)
                            py_raise("TypeError", "WeakValueDictionary.update() takes from 1 to 2 positional arguments but " + std::to_string(aArgs.size()) + " were given");
                        return wvd_update(py::borrowed(aArgs.at(0u)), aArgs.size() > 1u ? py::borrowed(aArgs[1]) : py{}, aKwargs);
                    }); } },
                { "valuerefs", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        auto const self = py::borrowed(aArgs.at(0u));
                        commit_if_pending(self, true);
                        return builtin("list")(data_of(self).method("values"));
                    }); } },
                { "__ior__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__ior__", aArgs, aKwargs, { "other" }, 1u, 1u);
                        a[0].method("update", { a[1] });
                        return a[0];
                    }); } },
                { "__or__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__or__", aArgs, aKwargs, { "other" }, 1u, 1u);
                        if (!isinstance(a[1], import_attribute("collections.abc", "Mapping")))
                            return py::borrowed(not_implemented());
                        auto const c = a[0].method("copy");
                        c.method("update", { a[1] });
                        return c;
                    }); } },
                { "__ror__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__ror__", aArgs, aKwargs, { "other" }, 1u, 1u);
                        if (!isinstance(a[1], import_attribute("collections.abc", "Mapping")))
                            return py::borrowed(not_implemented());
                        auto const c = a[0].attr("__class__")();
                        c.method("update", { a[1] });
                        c.method("update", { a[0] });
                        return c;
                    }); } }
            };

            // (KeyedRef)

            builtin_entry const sKeyedRefMethods[] =
            {
                { "__new__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        std::vector<object*> a;
                        py_checked(parse_arguments("__new__", aArgs, aKwargs, { "type", "ob", "callback", "key" }, 4u, 0u, a));
                        auto const self = reference_type().attr("__new__")(py::borrowed(a[0]), py::borrowed(a[1]), py::borrowed(a[2]));
                        self.set_attr("key", py::borrowed(a[3]));
                        return self;
                    }); } },
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__init__", aArgs, aKwargs, { "ob", "callback", "key" }, 3u);
                        return py::none();
                    }); } }
            };

            // (WeakMethod)

            builtin_entry const sWeakMethodMethods[] =
            {
                { "__new__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        std::vector<object*> a;
                        py_checked(parse_arguments("__new__", aArgs, aKwargs, { "cls", "meth", "callback" }, 2u, 0u, a));
                        auto const cls = py::borrowed(a[0]);
                        auto const meth = py::borrowed(a[1]);
                        auto const callback = a[2] != nullptr ? py::borrowed(a[2]) : py::none();
                        py obj, func;
                        try
                        {
                            obj = meth.attr("__self__");
                            func = meth.attr("__func__");
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "AttributeError" }))
                                throw;
                            py_raise("TypeError", "argument should be a bound method, not " + type_of(meth).repr());
                        }
                        // (_cb: the self-weakref avoids a reference cycle)
                        auto const selfWr = std::make_shared<py>();
                        auto const cb = make_native_function("_cb", "WeakMethod.__new__.<locals>._cb", "", "weakref", [selfWr, callback](std::vector<py> const&, object*) -> py
                        {
                            if (selfWr->empty())
                                return py::none();
                            auto const self = (*selfWr)();
                            if (self.is_none())
                                return py::none();
                            if (self.attr("_alive").truthy())
                            {
                                self.set_attr("_alive", py::boolean(false));
                                if (!callback.is_none())
                                    callback(self);
                            }
                            return py::none();
                        });
                        auto const self = reference_type().attr("__new__")(cls, obj, cb);
                        self.set_attr("_func_ref", weak_ref(func, cb));
                        self.set_attr("_meth_type", type_of(meth));
                        self.set_attr("_alive", py::boolean(true));
                        *selfWr = weak_ref(self);
                        return self;
                    }); } },
                { "__init__", [](std::vector<object*> const&, object*) -> object* { return py_guarded([&]() -> py { return py::none(); }); } },
                { "__call__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__call__", aArgs, aKwargs, {}, 0u);
                        auto const obj = py::borrowed(builtin_value(a[0].get()))();
                        auto const func = a[0].attr("_func_ref")();
                        if (obj.is_none() || func.is_none())
                            return py::none();
                        return a[0].attr("_meth_type")(func, obj);
                    }); } },
                { "__eq__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__eq__", aArgs, aKwargs, { "other" }, 1u, 1u);
                        if (!isinstance(a[1], py::borrowed(sClasses.weakMethod)))
                            return py::borrowed(not_implemented());
                        if (!a[0].attr("_alive").truthy() || !a[1].attr("_alive").truthy())
                            return py::boolean(a[0].is(a[1]));
                        auto const same = py::borrowed(builtin_value(a[0].get())) == py::borrowed(builtin_value(a[1].get()));
                        return py::boolean(same && a[0].attr("_func_ref") == a[1].attr("_func_ref"));
                    }); } },
                { "__ne__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__ne__", aArgs, aKwargs, { "other" }, 1u, 1u);
                        if (!isinstance(a[1], py::borrowed(sClasses.weakMethod)))
                            return py::borrowed(not_implemented());
                        if (!a[0].attr("_alive").truthy() || !a[1].attr("_alive").truthy())
                            return py::boolean(!a[0].is(a[1]));
                        auto const differ = py::borrowed(builtin_value(a[0].get())) != py::borrowed(builtin_value(a[1].get()));
                        return py::boolean(differ || a[0].attr("_func_ref") != a[1].attr("_func_ref"));
                    }); } },
                { "__hash__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__hash__", aArgs, aKwargs, {}, 0u);
                        return builtin("hash")(py::borrowed(builtin_value(a[0].get())));
                    }); } }
            };

            // (WeakKeyDictionary)

            py wkd_update(py const& aSelf, py aOther, object* aKwargs)
            {
                auto const d = data_of(aSelf);
                auto const remove = aSelf.attr("_remove");
                if (!aOther.empty() && !aOther.is_none())
                {
                    if (auto const items = getattr(aOther.get(), "items"); items == nullptr)
                    {
                        clear_error();
                        aOther = builtin("dict")(aOther);
                    }
                    else
                        release(items);
                    for (auto const& item : aOther.method("items").items())
                        d.set_item(weak_ref(item[py{ 0 }], remove), item[py{ 1 }]);
                }
                if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                    wkd_update(aSelf, py::borrowed(aKwargs), nullptr);
                return py::none();
            }

            builtin_entry const sWeakKeyDictionaryMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__init__", aArgs, aKwargs, { "dict" }, 0u);
                        init_container(a[0], py_dict(), "WeakKeyDictionary.__init__.<locals>.remove", 1);
                        a[0].set_attr("_dirty_len", py::boolean(false));
                        if (!a[1].empty() && !a[1].is_none())
                            a[0].method("update", { a[1] });
                        return py::none();
                    }); } },
                { "_commit_removals", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py { commit_removals(py::borrowed(aArgs.at(0u)), false); return py::none(); }); } },
                { "_scrub_removals", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        auto const self = py::borrowed(aArgs.at(0u));
                        auto const d = data_of(self);
                        std::vector<py> kept;
                        for (auto const& k : self.attr("_pending_removals").items())
                            if (d.contains(k))
                                kept.push_back(k);
                        self.set_attr("_pending_removals", py_list(kept));
                        self.set_attr("_dirty_len", py::boolean(false));
                        return py::none();
                    }); } },
                { "__delitem__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__delitem__", aArgs, aKwargs, { "key" }, 1u, 1u);
                        a[0].set_attr("_dirty_len", py::boolean(true));
                        data_of(a[0]).method("__delitem__", { weak_ref(a[1]) });
                        return py::none();
                    }); } },
                { "__getitem__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__getitem__", aArgs, aKwargs, { "key" }, 1u, 1u);
                        return data_of(a[0])[weak_ref(a[1])];
                    }); } },
                { "__len__", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        auto const self = py::borrowed(aArgs.at(0u));
                        if (self.attr("_dirty_len").truthy() && self.attr("_pending_removals").truthy())
                            self.method("_scrub_removals");
                        return py{ data_of(self).size() - self.attr("_pending_removals").size() };
                    }); } },
                { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py { return repr_at(py::borrowed(aArgs.at(0u))); }); } },
                { "__setitem__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__setitem__", aArgs, aKwargs, { "key", "value" }, 2u, 2u);
                        data_of(a[0]).set_item(weak_ref(a[1], a[0].attr("_remove")), a[2]);
                        return py::none();
                    }); } },
                { "copy", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        auto const self = py::borrowed(aArgs.at(0u));
                        auto const result = py::borrowed(sClasses.weakKeyDictionary)();
                        for (auto const& item : data_of(self).method("items").items())
                            if (auto const o = item[py{ 0 }](); !o.is_none())
                                result.set_item(o, item[py{ 1 }]);
                        return result;
                    }); } },
                { "__deepcopy__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__deepcopy__", aArgs, aKwargs, { "memo" }, 1u);
                        auto const result = a[0].attr("__class__")();
                        auto const deepcopy = import_attribute("copy", "deepcopy");
                        for (auto const& item : data_of(a[0]).method("items").items())
                            if (auto const o = item[py{ 0 }](); !o.is_none())
                                result.set_item(o, deepcopy(item[py{ 1 }], a[1]));
                        return result;
                    }); } },
                { "get", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("get", aArgs, aKwargs, { "key", "default" }, 1u);
                        return data_of(a[0]).method("get", { weak_ref(a[1]), a[2].empty() ? py::none() : a[2] });
                    }); } },
                { "__contains__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__contains__", aArgs, aKwargs, { "key" }, 1u, 1u);
                        py wr;
                        try
                        {
                            wr = weak_ref(a[1]);
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "TypeError" }))
                                throw;
                            return py::boolean(false);
                        }
                        return py::boolean(data_of(a[0]).contains(wr));
                    }); } },
                { "items", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        std::vector<py> result;
                        for (auto const& item : data_of(py::borrowed(aArgs.at(0u))).method("items").items())
                            if (auto const key = item[py{ 0 }](); !key.is_none())
                                result.push_back(py_tuple({ key, item[py{ 1 }] }));
                        return snapshot(result);
                    }); } },
                { "keys", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        std::vector<py> result;
                        for (auto const& wr : data_of(py::borrowed(aArgs.at(0u))).items())
                            if (auto const o = wr(); !o.is_none())
                                result.push_back(o);
                        return snapshot(result);
                    }); } },
                { "values", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        std::vector<py> result;
                        for (auto const& item : data_of(py::borrowed(aArgs.at(0u))).method("items").items())
                            if (!item[py{ 0 }]().is_none())
                                result.push_back(item[py{ 1 }]);
                        return snapshot(result);
                    }); } },
                { "keyrefs", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py { return builtin("list")(data_of(py::borrowed(aArgs.at(0u)))); }); } },
                { "popitem", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        auto const self = py::borrowed(aArgs.at(0u));
                        self.set_attr("_dirty_len", py::boolean(true));
                        for (;;)
                        {
                            auto const item = data_of(self).method("popitem");
                            if (auto const o = item[py{ 0 }](); !o.is_none())
                                return py_tuple({ o, item[py{ 1 }] });
                        }
                    }); } },
                { "pop", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        if (aArgs.size() < 2u || aArgs.size() > 3u)
                            py_raise("TypeError", aArgs.size() < 2u ? "WeakKeyDictionary.pop() missing 1 required positional argument: 'key'" :
                                "WeakKeyDictionary.pop() takes from 2 to 3 positional arguments but " + std::to_string(aArgs.size()) + " were given");
                        auto const self = py::borrowed(aArgs[0]);
                        self.set_attr("_dirty_len", py::boolean(true));
                        std::vector<py> args{ weak_ref(py::borrowed(aArgs[1])) };
                        if (aArgs.size() > 2u)
                            args.push_back(py::borrowed(aArgs[2]));
                        return data_of(self).method("pop", args);
                    }); } },
                { "setdefault", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("setdefault", aArgs, aKwargs, { "key", "default" }, 1u);
                        return data_of(a[0]).method("setdefault", { weak_ref(a[1], a[0].attr("_remove")), a[2].empty() ? py::none() : a[2] });
                    }); } },
                { "update", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        if (aArgs.size() > 2u)
                            py_raise("TypeError", "WeakKeyDictionary.update() takes from 1 to 2 positional arguments but " + std::to_string(aArgs.size()) + " were given");
                        return wkd_update(py::borrowed(aArgs.at(0u)), aArgs.size() > 1u ? py::borrowed(aArgs[1]) : py{}, aKwargs);
                    }); } },
                { "__ior__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__ior__", aArgs, aKwargs, { "other" }, 1u, 1u);
                        a[0].method("update", { a[1] });
                        return a[0];
                    }); } },
                { "__or__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__or__", aArgs, aKwargs, { "other" }, 1u, 1u);
                        if (!isinstance(a[1], import_attribute("collections.abc", "Mapping")))
                            return py::borrowed(not_implemented());
                        auto const c = a[0].method("copy");
                        c.method("update", { a[1] });
                        return c;
                    }); } },
                { "__ror__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__ror__", aArgs, aKwargs, { "other" }, 1u, 1u);
                        if (!isinstance(a[1], import_attribute("collections.abc", "Mapping")))
                            return py::borrowed(not_implemented());
                        auto const c = a[0].attr("__class__")();
                        c.method("update", { a[1] });
                        c.method("update", { a[0] });
                        return c;
                    }); } }
            };

            // (WeakSet)

            py refs_of(py const& aItems, py const& aCallback = py{})
            {
                std::vector<py> refs;
                for (auto const& item : aItems.items())
                    refs.push_back(weak_ref(item, aCallback));
                return py_list(refs);
            }

            py weak_set(py const& aSelf, py const& aItems) { return aSelf.attr("__class__")(aItems); }

            builtin_entry const sWeakSetMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__init__", aArgs, aKwargs, { "data" }, 0u);
                        init_container(a[0], builtin("set")(), "WeakSet.__init__.<locals>._remove", 2);
                        if (!a[1].empty() && !a[1].is_none())
                            a[0].method("update", { a[1] });
                        return py::none();
                    }); } },
                { "_commit_removals", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py { commit_removals(py::borrowed(aArgs.at(0u)), false); return py::none(); }); } },
                { "__iter__", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        std::vector<py> result;
                        for (auto const& wr : data_of(py::borrowed(aArgs.at(0u))).items())
                            if (auto const o = wr(); !o.is_none())
                                result.push_back(o);
                        return snapshot(result);
                    }); } },
                { "__len__", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        auto const self = py::borrowed(aArgs.at(0u));
                        return py{ data_of(self).size() - self.attr("_pending_removals").size() };
                    }); } },
                { "__contains__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__contains__", aArgs, aKwargs, { "item" }, 1u, 1u);
                        py wr;
                        try
                        {
                            wr = weak_ref(a[1]);
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "TypeError" }))
                                throw;
                            return py::boolean(false);
                        }
                        return py::boolean(data_of(a[0]).contains(wr));
                    }); } },
                { "__reduce__", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        auto const self = py::borrowed(aArgs.at(0u));
                        return py_tuple({ self.attr("__class__"), py_tuple({ builtin("list")(self) }), self.method("__getstate__") });
                    }); } },
                { "add", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("add", aArgs, aKwargs, { "item" }, 1u, 1u);
                        commit_if_pending(a[0], false);
                        data_of(a[0]).method("add", { weak_ref(a[1], a[0].attr("_remove")) });
                        return py::none();
                    }); } },
                { "clear", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        auto const self = py::borrowed(aArgs.at(0u));
                        commit_if_pending(self, false);
                        data_of(self).method("clear");
                        return py::none();
                    }); } },
                { "copy", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py { auto const self = py::borrowed(aArgs.at(0u)); return weak_set(self, self); }); } },
                { "pop", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        auto const self = py::borrowed(aArgs.at(0u));
                        commit_if_pending(self, false);
                        for (;;)
                        {
                            py wr;
                            try
                            {
                                wr = data_of(self).method("pop");
                            }
                            catch (raised const&)
                            {
                                if (!caught({ "KeyError" }))
                                    throw;
                                py_raise(py{ exception_class("KeyError") }(py{ "pop from empty WeakSet" }));
                            }
                            if (auto const o = wr(); !o.is_none())
                                return o;
                        }
                    }); } },
                { "remove", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("remove", aArgs, aKwargs, { "item" }, 1u, 1u);
                        commit_if_pending(a[0], false);
                        data_of(a[0]).method("remove", { weak_ref(a[1]) });
                        return py::none();
                    }); } },
                { "discard", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("discard", aArgs, aKwargs, { "item" }, 1u, 1u);
                        commit_if_pending(a[0], false);
                        data_of(a[0]).method("discard", { weak_ref(a[1]) });
                        return py::none();
                    }); } },
                { "update", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("update", aArgs, aKwargs, { "other" }, 1u, 1u);
                        commit_if_pending(a[0], false);
                        for (auto const& element : a[1].items())
                            a[0].method("add", { element });
                        return py::none();
                    }); } },
                { "__ior__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__ior__", aArgs, aKwargs, { "other" }, 1u, 1u);
                        a[0].method("update", { a[1] });
                        return a[0];
                    }); } },
                { "difference", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("difference", aArgs, aKwargs, { "other" }, 1u, 1u);
                        auto const result = a[0].method("copy");
                        result.method("difference_update", { a[1] });
                        return result;
                    }); } },
                { "difference_update", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("difference_update", aArgs, aKwargs, { "other" }, 1u, 1u);
                        a[0].method("__isub__", { a[1] });
                        return py::none();
                    }); } },
                { "__isub__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__isub__", aArgs, aKwargs, { "other" }, 1u, 1u);
                        commit_if_pending(a[0], false);
                        if (a[0].is(a[1]))
                            data_of(a[0]).method("clear");
                        else
                            data_of(a[0]).method("difference_update", { refs_of(a[1]) });
                        return a[0];
                    }); } },
                { "intersection", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("intersection", aArgs, aKwargs, { "other" }, 1u, 1u);
                        std::vector<py> items;
                        for (auto const& item : a[1].items())
                            if (a[0].contains(item))
                                items.push_back(item);
                        return weak_set(a[0], py_list(items));
                    }); } },
                { "intersection_update", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("intersection_update", aArgs, aKwargs, { "other" }, 1u, 1u);
                        a[0].method("__iand__", { a[1] });
                        return py::none();
                    }); } },
                { "__iand__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__iand__", aArgs, aKwargs, { "other" }, 1u, 1u);
                        commit_if_pending(a[0], false);
                        data_of(a[0]).method("intersection_update", { refs_of(a[1]) });
                        return a[0];
                    }); } },
                { "issubset", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("issubset", aArgs, aKwargs, { "other" }, 1u, 1u);
                        return data_of(a[0]).method("issubset", { refs_of(a[1]) });
                    }); } },
                { "__lt__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__lt__", aArgs, aKwargs, { "other" }, 1u, 1u);
                        return py::boolean(data_of(a[0]) < builtin("set")(refs_of(a[1])));
                    }); } },
                { "issuperset", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("issuperset", aArgs, aKwargs, { "other" }, 1u, 1u);
                        return data_of(a[0]).method("issuperset", { refs_of(a[1]) });
                    }); } },
                { "__gt__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__gt__", aArgs, aKwargs, { "other" }, 1u, 1u);
                        return py::boolean(data_of(a[0]) > builtin("set")(refs_of(a[1])));
                    }); } },
                { "__eq__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__eq__", aArgs, aKwargs, { "other" }, 1u, 1u);
                        if (!isinstance(a[1], a[0].attr("__class__")))
                            return py::borrowed(not_implemented());
                        return py::boolean(data_of(a[0]) == builtin("set")(refs_of(a[1])));
                    }); } },
                { "symmetric_difference", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("symmetric_difference", aArgs, aKwargs, { "other" }, 1u, 1u);
                        auto const result = a[0].method("copy");
                        result.method("symmetric_difference_update", { a[1] });
                        return result;
                    }); } },
                { "symmetric_difference_update", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("symmetric_difference_update", aArgs, aKwargs, { "other" }, 1u, 1u);
                        a[0].method("__ixor__", { a[1] });
                        return py::none();
                    }); } },
                { "__ixor__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__ixor__", aArgs, aKwargs, { "other" }, 1u, 1u);
                        commit_if_pending(a[0], false);
                        if (a[0].is(a[1]))
                            data_of(a[0]).method("clear");
                        else
                            data_of(a[0]).method("symmetric_difference_update", { refs_of(a[1], a[0].attr("_remove")) });
                        return a[0];
                    }); } },
                { "union", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("union", aArgs, aKwargs, { "other" }, 1u, 1u);
                        std::vector<py> items = a[0].items();
                        for (auto const& item : a[1].items())
                            items.push_back(item);
                        return weak_set(a[0], py_list(items));
                    }); } },
                { "isdisjoint", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("isdisjoint", aArgs, aKwargs, { "other" }, 1u, 1u);
                        return py::boolean(a[0].method("intersection", { a[1] }).size() == 0);
                    }); } },
                { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py { return py{ data_of(py::borrowed(aArgs.at(0u))).repr() }; }); } }
            };

            // (finalize)

            py finalize_class() { return py::borrowed(sClasses.finalize); }
            py registry() { return finalize_class().attr("_registry"); }

            py registry_get(py const& aSelf) { return registry().method("get", { aSelf }); }

            builtin_entry const sFinalizeMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        if (aArgs.size() < 3u)
                            py_raise("TypeError", std::string{ "finalize.__init__() missing " } + (aArgs.size() < 2u ? "2 required positional arguments: 'obj' and 'func'" :
                                "1 required positional argument: 'func'"));
                        auto const self = py::borrowed(aArgs[0]);
                        auto const cls = finalize_class();
                        if (!cls.attr("_registered_with_atexit").truthy())
                        {
                            import_attribute("atexit", "register")(cls.attr("_exitfunc"));
                            cls.set_attr("_registered_with_atexit", py::boolean(true));
                        }
                        auto const info = py::borrowed(sClasses.info)();
                        info.set_attr("weakref", weak_ref(py::borrowed(aArgs[1]), self));
                        info.set_attr("func", py::borrowed(aArgs[2]));
                        info.set_attr("args", py_tuple(std::vector<py>{}));
                        {
                            std::vector<py> args;
                            for (std::size_t i = 3u; i < aArgs.size(); ++i)
                                args.push_back(py::borrowed(aArgs[i]));
                            info.set_attr("args", py_tuple(args));
                        }
                        info.set_attr("kwargs", aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty() ? builtin("dict")(py::borrowed(aKwargs)) : py::none());
                        info.set_attr("atexit", py::boolean(true));
                        info.set_attr("index", builtin("next")(cls.attr("_index_iter")));
                        registry().set_item(self, info);
                        cls.set_attr("_dirty", py::boolean(true));
                        return py::none();
                    }); } },
                { "__call__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("__call__", aArgs, aKwargs, { "_" }, 0u);
                        auto const info = registry().method("pop", { a[0], py::none() });
                        if (info.truthy() && !finalize_class().attr("_shutdown").truthy())
                        {
                            auto const kwargs = info.attr("kwargs");
                            std::vector<std::pair<char const*, py>> keywords;
                            std::vector<std::string> names;
                            if (kwargs.truthy())
                                for (auto const& item : kwargs.method("items").items())
                                    names.push_back(item[py{ 0 }].str());
                            std::size_t i = 0u;
                            if (kwargs.truthy())
                                for (auto const& item : kwargs.method("items").items())
                                    keywords.emplace_back(names[i++].c_str(), item[py{ 1 }]);
                            return info.attr("func").call(info.attr("args").items(), keywords);
                        }
                        return py::none();
                    }); } },
                { "detach", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("detach", aArgs, aKwargs, {}, 0u);
                        auto const info = registry_get(a[0]);
                        auto const obj = info.truthy() ? info.attr("weakref")() : info;
                        if (!obj.is_none() && registry().method("pop", { a[0], py::none() }).truthy())
                        {
                            auto const kwargs = info.attr("kwargs");
                            return py_tuple({ obj, info.attr("func"), info.attr("args"), kwargs.truthy() ? kwargs : py_dict() });
                        }
                        return py::none();
                    }); } },
                { "peek", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return py_guarded([&]() -> py
                    {
                        auto const a = self_arguments_of("peek", aArgs, aKwargs, {}, 0u);
                        auto const info = registry_get(a[0]);
                        auto const obj = info.truthy() ? info.attr("weakref")() : info;
                        if (!obj.is_none())
                        {
                            auto const kwargs = info.attr("kwargs");
                            return py_tuple({ obj, info.attr("func"), info.attr("args"), kwargs.truthy() ? kwargs : py_dict() });
                        }
                        return py::none();
                    }); } },
                { "_alive", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py { return py::boolean(registry().contains(py::borrowed(aArgs.at(0u)))); }); } },
                { "_atexit_get", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        auto const info = registry_get(py::borrowed(aArgs.at(0u)));
                        return py::boolean(info.truthy() && info.attr("atexit").truthy());
                    }); } },
                { "_atexit_set", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        auto const info = registry_get(py::borrowed(aArgs.at(0u)));
                        if (info.truthy())
                            info.set_attr("atexit", py::boolean(py::borrowed(aArgs.at(1u)).truthy()));
                        return py::none();
                    }); } },
                { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object* { return py_guarded([&]() -> py
                    {
                        auto const self = py::borrowed(aArgs.at(0u));
                        auto const info = registry_get(self);
                        auto const obj = info.truthy() ? info.attr("weakref")() : py::none();
                        auto const hex = [](object* aObject) { char text[32]; std::snprintf(text, sizeof text, "%#llx",
                            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(aObject))); return std::string{ text }; };
                        auto const name = type_of(self).attr("__name__").str();
                        if (obj.is_none())
                            return py{ "<" + name + " object at " + hex(self.get()) + "; dead>" };
                        return py{ "<" + name + " object at " + hex(self.get()) + "; for " + type_of(obj).attr("__name__").repr() + " at " + hex(obj.get()) + ">" };
                    }); } }
            };

            std::vector<py> select_for_exit()
            {
                std::vector<std::pair<long long, py>> live;
                for (auto const& item : registry().method("items").items())
                    if (item[py{ 1 }].attr("atexit").truthy())
                        live.emplace_back(item[py{ 1 }].attr("index").as_index(), item[py{ 0 }]);
                std::stable_sort(live.begin(), live.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
                std::vector<py> result;
                for (auto& l : live)
                    result.push_back(l.second);
                return result;
            }

            builtin_entry const sSelectForExit{ "_select_for_exit", [](std::vector<object*> const&, object*) -> object* { return py_guarded([&]() -> py { return py_list(select_for_exit()); }); } };
            builtin_entry const sExitFunc{ "_exitfunc", [](std::vector<object*> const&, object*) -> object* { return py_guarded([&]() -> py
                {
                    auto const cls = finalize_class();
                    try
                    {
                        if (registry().truthy())
                        {
                            std::optional<std::vector<py>> pending;
                            for (;;)
                            {
                                if (!pending || cls.attr("_dirty").truthy())
                                {
                                    pending = select_for_exit();
                                    cls.set_attr("_dirty", py::boolean(false));
                                }
                                if (pending->empty())
                                    break;
                                auto const f = pending->back();
                                pending->pop_back();
                                try
                                {
                                    f();
                                }
                                catch (raised const&)
                                {
                                    ref const e{ take_exception() };
                                    std::set<object*> seen;
                                    std::string const report = e ? exception_report(e.get(), seen) : std::string{};
                                    std::fprintf(stderr, "%s", report.c_str());
                                    std::fflush(stderr);
                                }
                            }
                        }
                    }
                    catch (raised const&)
                    {
                        cls.set_attr("_shutdown", py::boolean(true));
                        throw;
                    }
                    cls.set_attr("_shutdown", py::boolean(true));
                    return py::none();
                }); } };

            object* class_getitem()
            {
                static builtin_entry const sClassGetItem{ "__class_getitem__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return error("TypeError", "__class_getitem__ expected 1 argument");
                        return make_generic_alias(aArgs[0], aArgs[1]);
                    } };
                ref const function{ make_builtin(sClassGetItem) };
                return b_classmethod({ function.get() }, nullptr);
            }

            py property_of(py const& aClass, char const* aGetter, char const* aSetter)
            {
                std::vector<object*> args{ aClass.attr(aGetter).get() };
                py setter;
                if (aSetter != nullptr)
                {
                    setter = aClass.attr(aSetter);
                    args.push_back(setter.get());
                }
                auto getter = aClass.attr(aGetter);
                args[0] = getter.get();
                return py{ b_property(args, nullptr) };
            }

            void set_class_attribute(py const& aClass, char const* aName, py const& aValue) { py_checked(class_setattr_raw(aClass.get(), aName, aValue.get()) == 0); }
            void delete_class_attribute(py const& aClass, char const* aName) { py_checked(class_setattr_raw(aClass.get(), aName, nullptr) == 0); }

            bool make_classes()
            {
                if (sClasses.finalize != nullptr)
                    return true;
                return py_guarded([&]()
                {
                    // (types: method objects can be made (WeakMethod's _meth_type))
                    py const types{ fn_import("types") };
                    auto const referenceType = reference_type();
                    auto const mutableMapping = import_attribute("collections.abc", "MutableMapping");
                    py const keyedRef{ make_class_of("weakref", "KeyedRef", { referenceType.get() }, sKeyedRefMethods) };
                    py const weakMethod{ make_class_of("weakref", "WeakMethod", { referenceType.get() }, sWeakMethodMethods) };
                    py const wvd{ make_class_of("weakref", "WeakValueDictionary", { mutableMapping.get() }, sWeakValueDictionaryMethods) };
                    set_class_attribute(wvd, "__copy__", wvd.attr("copy"));
                    set_class_attribute(wvd, "__iter__", wvd.attr("keys"));
                    py const wkd{ make_class_of("weakref", "WeakKeyDictionary", { mutableMapping.get() }, sWeakKeyDictionaryMethods) };
                    set_class_attribute(wkd, "__copy__", wkd.attr("copy"));
                    set_class_attribute(wkd, "__iter__", wkd.attr("keys"));
                    // (their abstract methods (MutableMapping's) are implemented)
                    for (auto const& c : { wvd, wkd })
                        import_attribute("abc", "update_abstractmethods")(c);
                    py const weakSet{ make_class_of("_weakrefset", "WeakSet", {}, sWeakSetMethods, { { "__class_getitem__", class_getitem() } }) };
                    for (auto const& [alias, name] : { std::pair{ "__sub__", "difference" }, std::pair{ "__and__", "intersection" }, std::pair{ "__le__", "issubset" },
                        std::pair{ "__ge__", "issuperset" }, std::pair{ "__xor__", "symmetric_difference" }, std::pair{ "__or__", "union" } })
                        set_class_attribute(weakSet, alias, weakSet.attr(name));
                    if (ref const registerMethod{ module_attribute("collections.abc", "MutableSet") }; registerMethod)
                    {
                        if (ref const r{ getattr(registerMethod.get(), "register") })
                        {
                            ref const args{ make_tuple({ weakSet.reference() }) };
                            ref const registered{ call(r.get(), args.get(), nullptr) };
                        }
                    }
                    clear_error();
                    py const info{ make_class("weakref", "_Info", {}, {}) };
                    set_class_attribute(info, "__qualname__", py{ "finalize._Info" });
                    py const finalize{ make_class_of("weakref", "finalize", {}, sFinalizeMethods, { { "_registry", make_dict() }, { "_shutdown", make_bool(false) },
                        { "_dirty", make_bool(false) }, { "_registered_with_atexit", make_bool(false) } }) };
                    set_class_attribute(finalize, "_index_iter", import_attribute("itertools", "count")());
                    set_class_attribute(finalize, "_Info", info);
                    set_class_attribute(finalize, "alive", property_of(finalize, "_alive", nullptr));
                    set_class_attribute(finalize, "atexit", property_of(finalize, "_atexit_get", "_atexit_set"));
                    for (auto name : { "_alive", "_atexit_get", "_atexit_set" })
                        delete_class_attribute(finalize, name);
                    set_class_attribute(finalize, "_select_for_exit", py{ b_classmethod({ py{ make_builtin(sSelectForExit) }.get() }, nullptr) });
                    set_class_attribute(finalize, "_exitfunc", py{ b_classmethod({ py{ make_builtin(sExitFunc) }.get() }, nullptr) });
                    sClasses.keyedRef = keyedRef.reference();
                    sClasses.weakMethod = weakMethod.reference();
                    sClasses.weakValueDictionary = wvd.reference();
                    sClasses.weakKeyDictionary = wkd.reference();
                    sClasses.weakSet = weakSet.reference();
                    sClasses.info = info.reference();
                    sClasses.finalize = finalize.reference();
                    return py::none();
                }) != nullptr && (release(none()), true);
            }

            bool weakrefset_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                if (!make_classes())
                    return false;
                aAttributes.emplace_back("WeakSet", new_reference(sClasses.weakSet));
                aAttributes.emplace_back("ref", py_guarded([]() { return reference_type(); }));
                std::vector<object*> all{ make_str("WeakSet") };
                aAttributes.emplace_back("__all__", make_list(std::move(all)));
                return true;
            }

            inline bool const sWeakrefset = register_module("_weakrefset", weakrefset_module);

            bool weakref_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                if (!weakref_internal_module(aAttributes) || !make_classes())
                    return false;
                auto const types = py_guarded([]() { return py_tuple({ proxy_type(), callable_proxy_type() }); });
                if (types == nullptr)
                    return false;
                aAttributes.emplace_back("ProxyTypes", types);
                aAttributes.emplace_back("WeakMethod", new_reference(sClasses.weakMethod));
                aAttributes.emplace_back("KeyedRef", new_reference(sClasses.keyedRef));
                aAttributes.emplace_back("WeakValueDictionary", new_reference(sClasses.weakValueDictionary));
                aAttributes.emplace_back("WeakKeyDictionary", new_reference(sClasses.weakKeyDictionary));
                aAttributes.emplace_back("WeakSet", new_reference(sClasses.weakSet));
                aAttributes.emplace_back("finalize", new_reference(sClasses.finalize));
                std::vector<object*> all;
                for (auto name : { "ref", "proxy", "getweakrefcount", "getweakrefs", "WeakKeyDictionary", "ReferenceType", "ProxyType", "CallableProxyType",
                    "ProxyTypes", "WeakValueDictionary", "WeakSet", "WeakMethod", "finalize" })
                    all.push_back(make_str(name));
                aAttributes.emplace_back("__all__", make_list(std::move(all)));
                return true;
            }

            inline bool const sWeakref = register_module("weakref", weakref_module);
        }

        // (an object is destroyed (its references are 0): its weak references are dead; their callbacks are called (with an exception that is
        // being raised kept))
        void clear_weak_references(neos_py_object* aObject)
        {
            if (!noted_type(aObject->type))
                return;
            std::vector<weakref_object*> list;
            {
                std::lock_guard<std::recursive_mutex> lock{ sMutex };
                auto const r = sReferents.find(aObject);
                if (r == sReferents.end())
                    return;
                list = std::move(r->second);
                sReferents.erase(r);
                gWeaklyReferenced.fetch_sub(1u, std::memory_order_relaxed);
                for (auto w : list)
                    w->referent = nullptr;
            }
            // (the weak references that have callbacks, alive, with their callbacks)
            std::vector<std::pair<object*, object*>> callbacks;
            {
                std::lock_guard<std::recursive_mutex> lock{ sMutex };
                for (auto w : list)
                {
                    if (w->callback == nullptr)
                        continue;
                    auto& references = w->h.references;
                    bool alive = false;
                    for (auto n = references.load(std::memory_order_acquire); n > 0 && !alive;)
                        alive = references.compare_exchange_weak(n, n + 1, std::memory_order_acq_rel);
                    if (!alive)
                        continue;
                    auto const callback = w->callback;
                    w->callback = nullptr;
                    if (w->owner != nullptr)
                        retain(w->owner);
                    callbacks.emplace_back(&w->h.base, callback);
                }
            }
            if (callbacks.empty())
                return;
            ref const pending{ failed() ? take_exception() : ref{} };
            for (auto& [w, callback] : callbacks)
            {
                ref const owned{ w };
                ref const ownedCallback{ callback };
                auto const owner = as_weakref(w).owner;
                ref const ownedOwner{ owner };
                ref const args{ make_tuple({ new_reference(owner != nullptr ? owner : w) }) };
                ref const result{ call(callback, args.get(), nullptr) };
                if (!result)
                    write_unraisable(callback);
            }
            if (pending)
                fn_raise(pending.get());
        }
    }
}
