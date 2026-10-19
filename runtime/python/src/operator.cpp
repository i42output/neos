/*
  operator.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/heapq.py and Lib/copy.py), Copyright (c) 2001-2024 Python
  Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The modules operator, heapq, bisect and copy (CPython 3.13's behaviour)

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            object* borrowed(object* aObject) { retain(aObject); return aObject; }

            object* call_with(object* aCallable, std::vector<object*> aArgs, object* aKwargs = nullptr)    // (steals the arguments)
            {
                ref const args{ make_tuple(std::move(aArgs)) };
                return call(aCallable, args.get(), aKwargs);
            }

            bool exactly(char const* aName, std::vector<object*> const& aArgs, object* aKwargs, std::size_t aCount)
            {
                if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                {
                    error("TypeError", std::string{ aName } + "() takes no keyword arguments");
                    return false;
                }
                if (aArgs.size() != aCount)
                {
                    if (aCount == 1u)
                        error("TypeError", std::string{ aName } + "() takes exactly one argument (" + std::to_string(aArgs.size()) + " given)");
                    else
                        error("TypeError", std::string{ aName } + " expected " + std::to_string(aCount) + " arguments, got " + std::to_string(aArgs.size()));
                    return false;
                }
                return true;
            }

            // --- operator's functions: binary, inplace, comparison and unary operators

            struct operator_entry { char const* name; int kind; int operation; };
            enum : int { Binary, Inplace, Compare, Unary, Other };

            operator_entry const sOperators[] =
            {
                { "add", Binary, NEOS_PY_ADD }, { "sub", Binary, NEOS_PY_SUBTRACT }, { "mul", Binary, NEOS_PY_MULTIPLY }, { "truediv", Binary, NEOS_PY_TRUE_DIVIDE },
                { "floordiv", Binary, NEOS_PY_FLOOR_DIVIDE }, { "mod", Binary, NEOS_PY_REMAINDER }, { "pow", Binary, NEOS_PY_POWER }, { "matmul", Binary, NEOS_PY_MATMUL },
                { "lshift", Binary, NEOS_PY_LSHIFT }, { "rshift", Binary, NEOS_PY_RSHIFT }, { "and_", Binary, NEOS_PY_AND }, { "or_", Binary, NEOS_PY_OR },
                { "xor", Binary, NEOS_PY_XOR },
                { "iadd", Inplace, NEOS_PY_ADD }, { "isub", Inplace, NEOS_PY_SUBTRACT }, { "imul", Inplace, NEOS_PY_MULTIPLY }, { "itruediv", Inplace, NEOS_PY_TRUE_DIVIDE },
                { "ifloordiv", Inplace, NEOS_PY_FLOOR_DIVIDE }, { "imod", Inplace, NEOS_PY_REMAINDER }, { "ipow", Inplace, NEOS_PY_POWER }, { "imatmul", Inplace, NEOS_PY_MATMUL },
                { "ilshift", Inplace, NEOS_PY_LSHIFT }, { "irshift", Inplace, NEOS_PY_RSHIFT }, { "iand", Inplace, NEOS_PY_AND }, { "ior", Inplace, NEOS_PY_OR },
                { "ixor", Inplace, NEOS_PY_XOR }, { "iconcat", Inplace, NEOS_PY_ADD },
                { "lt", Compare, NEOS_PY_LT }, { "le", Compare, NEOS_PY_LE }, { "eq", Compare, NEOS_PY_EQ }, { "ne", Compare, NEOS_PY_NE },
                { "gt", Compare, NEOS_PY_GT }, { "ge", Compare, NEOS_PY_GE },
                { "neg", Unary, NEOS_PY_NEGATIVE }, { "pos", Unary, NEOS_PY_POSITIVE }, { "invert", Unary, NEOS_PY_INVERT }, { "inv", Unary, NEOS_PY_INVERT },
                { "abs", Unary, NEOS_PY_ABSOLUTE }
            };

            object* call_operator(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& entry = *static_cast<operator_entry const*>(aData);
                auto const& args = as<tuple_object>(aArgs)->items;
                if (!exactly(entry.name, args, aKwargs, entry.kind == Unary ? 1u : 2u))
                    return nullptr;
                switch (entry.kind)
                {
                case Binary: return binary(entry.operation, args[0], args[1], false);
                case Inplace:
                    if (std::strcmp(entry.name, "iconcat") == 0 && python_type(args[0]) != nullptr && kind_of(args[0]) != NEOS_PY_LIST &&
                        kind_of(args[0]) != NEOS_PY_TUPLE && kind_of(args[0]) != NEOS_PY_STR && !is_instance(args[0]))
                        return error("TypeError", std::string{ "'" } + type_name(args[0]) + "' object can't be concatenated");
                    return binary(entry.operation, args[0], args[1], true);
                case Compare: return compare(entry.operation, args[0], args[1]);
                default: return unary(entry.operation, args[0]);
                }
            }

            builtin_entry const sOperatorFunctions[] =
            {
                { "not_", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { if (!exactly("not_", aArgs, aKwargs, 1u)) return nullptr; auto const t = truth(aArgs[0]); return t < 0 ? nullptr : make_bool(t == 0); } },
                { "truth", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { if (!exactly("truth", aArgs, aKwargs, 1u)) return nullptr; auto const t = truth(aArgs[0]); return t < 0 ? nullptr : make_bool(t == 1); } },
                { "is_", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return exactly("is_", aArgs, aKwargs, 2u) ? make_bool(aArgs[0] == aArgs[1]) : nullptr; } },
                { "is_not", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return exactly("is_not", aArgs, aKwargs, 2u) ? make_bool(aArgs[0] != aArgs[1]) : nullptr; } },
                { "is_none", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return exactly("is_none", aArgs, aKwargs, 1u) ? make_bool(kind_of(aArgs[0]) == NEOS_PY_NONE) : nullptr; } },
                { "is_not_none", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return exactly("is_not_none", aArgs, aKwargs, 1u) ? make_bool(kind_of(aArgs[0]) != NEOS_PY_NONE) : nullptr; } },
                { "contains", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!exactly("contains", aArgs, aKwargs, 2u))
                            return nullptr;
                        auto const c = contains(aArgs[0], aArgs[1]);
                        return c < 0 ? nullptr : make_bool(c == 1);
                    } },
                { "countOf", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!exactly("countOf", aArgs, aKwargs, 2u))
                            return nullptr;
                        std::vector<object*> items;
                        if (!collect(aArgs[0], items))
                            return nullptr;
                        long long n = 0;
                        bool ok = true;
                        for (auto i : items)
                        {
                            if (ok)
                            {
                                auto const e = equal(i, aArgs[1]);
                                ok = e >= 0;
                                n += e > 0 ? 1 : 0;
                            }
                            release(i);
                        }
                        return ok ? make_int(n) : nullptr;
                    } },
                { "indexOf", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!exactly("indexOf", aArgs, aKwargs, 2u))
                            return nullptr;
                        std::vector<object*> items;
                        if (!collect(aArgs[0], items))
                            return nullptr;
                        long long index = -1;
                        bool ok = true;
                        for (std::size_t i = 0u; i < items.size(); ++i)
                        {
                            if (ok && index < 0)
                            {
                                auto const e = equal(items[i], aArgs[1]);
                                ok = e >= 0;
                                if (e > 0)
                                    index = static_cast<long long>(i);
                            }
                            release(items[i]);
                        }
                        if (!ok)
                            return nullptr;
                        if (index < 0)
                            return error("ValueError", "sequence.index(x): x not in sequence");
                        return make_int(index);
                    } },
                { "getitem", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return exactly("getitem", aArgs, aKwargs, 2u) ? getitem(aArgs[0], aArgs[1]) : nullptr; } },
                { "setitem", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return exactly("setitem", aArgs, aKwargs, 3u) && setitem(aArgs[0], aArgs[1], aArgs[2]) == 0 ? borrowed(none()) : nullptr; } },
                { "delitem", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { return exactly("delitem", aArgs, aKwargs, 2u) && setitem(aArgs[0], aArgs[1], nullptr) == 0 ? borrowed(none()) : nullptr; } },
                { "concat", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!exactly("concat", aArgs, aKwargs, 2u))
                            return nullptr;
                        auto const k = kind_of(builtin_value(aArgs[0]));
                        std::string_view bytes;
                        if (k != NEOS_PY_LIST && k != NEOS_PY_TUPLE && k != NEOS_PY_STR && !bytes_like(aArgs[0], bytes) &&
                            !(is_instance(aArgs[0]) && has_special_method(aArgs[0], "__getitem__")))
                            return error("TypeError", std::string{ "'" } + type_name(aArgs[0]) + "' object can't be concatenated");
                        return binary(NEOS_PY_ADD, aArgs[0], aArgs[1], false);
                    } },
                { "index", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (!exactly("index", aArgs, aKwargs, 1u))
                            return nullptr;
                        if (kind_of(aArgs[0]) == NEOS_PY_INT)
                            return borrowed(aArgs[0]);
                        long long n = 0;
                        return to_index(aArgs[0], n) ? make_int(n) : nullptr;
                    } },
                { "length_hint", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                            return error("TypeError", "length_hint() takes no keyword arguments");
                        if (aArgs.empty() || aArgs.size() > 2u)
                            return error("TypeError", "length_hint expected at most 2 arguments");
                        auto const n = length(aArgs[0]);
                        if (n >= 0)
                            return make_int(n);
                        clear_error();
                        if (aArgs.size() == 2u)
                            return borrowed(aArgs[1]);
                        return make_int(0);
                    } },
                { "call", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aArgs.empty())
                            return error("TypeError", "call() missing 1 required positional argument: 'obj'");
                        std::vector<object*> rest;
                        for (std::size_t i = 1u; i < aArgs.size(); ++i)
                            rest.push_back(borrowed(aArgs[i]));
                        return call_with(aArgs[0], std::move(rest), aKwargs);
                    } }
            };

            // --- itemgetter, attrgetter and methodcaller

            struct getter_object
            {
                header h;
                int kind;                   ///< (0: itemgetter, 1: attrgetter, 2: methodcaller)
                std::vector<object*> items; ///< (the keys, the attribute names, or the method's name and arguments)
                object* kwargs = nullptr;   ///< (methodcaller's)
                ~getter_object() { for (auto i : items) release(i); release(kwargs); }
            };

            neos_py_type make_getter_type(int aKind);
            neos_py_type sItemGetterType = make_getter_type(0);
            neos_py_type sAttrGetterType = make_getter_type(1);
            neos_py_type sMethodCallerType = make_getter_type(2);
            char const* const sGetterNames[] = { "itemgetter", "attrgetter", "methodcaller" };

            // (a dotted attribute name: e.g. "a.b")
            object* dotted_getattr(object* aObject, std::string const& aName)
            {
                ref current{ borrowed(aObject) };
                std::size_t start = 0u;
                for (;;)
                {
                    auto const dot = aName.find('.', start);
                    current = ref{ getattr(current.get(), aName.substr(start, dot == std::string::npos ? std::string::npos : dot - start).c_str()) };
                    if (!current || dot == std::string::npos)
                        return current.release_ownership();
                    start = dot + 1u;
                }
            }

            neos_py_type make_getter_type(int aKind)
            {
                static char const* const sQualified[] = { "operator.itemgetter", "operator.attrgetter", "operator.methodcaller" };
                auto type = make_type(sQualified[aKind], sQualified[aKind] + 9, retain_object, release_object<getter_object>);
                type.call = [](neos_py_context*, object* aSelf, object* aArgs, object* aKwargs) -> object*
                {
                    auto const& g = *reinterpret_cast<getter_object*>(aSelf);
                    auto const& args = as<tuple_object>(aArgs)->items;
                    if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                        return error("TypeError", std::string{ sGetterNames[g.kind] } + "() takes no keyword arguments");
                    if (args.size() != 1u)
                        return error("TypeError", std::string{ sGetterNames[g.kind] } + " expected 1 argument, got " + std::to_string(args.size()));
                    auto const target = args[0];
                    switch (g.kind)
                    {
                    case 0:
                        {
                            if (g.items.size() == 1u)
                                return getitem(target, g.items[0]);
                            std::vector<object*> result;
                            for (auto k : g.items)
                            {
                                auto const item = getitem(target, k);
                                if (item == nullptr)
                                {
                                    for (auto r : result)
                                        release(r);
                                    return nullptr;
                                }
                                result.push_back(item);
                            }
                            return make_tuple(std::move(result));
                        }
                    case 1:
                        {
                            if (g.items.size() == 1u)
                                return dotted_getattr(target, as<str_object>(g.items[0])->value);
                            std::vector<object*> result;
                            for (auto k : g.items)
                            {
                                auto const item = dotted_getattr(target, as<str_object>(k)->value);
                                if (item == nullptr)
                                {
                                    for (auto r : result)
                                        release(r);
                                    return nullptr;
                                }
                                result.push_back(item);
                            }
                            return make_tuple(std::move(result));
                        }
                    default:
                        {
                            ref const method{ getattr(target, as<str_object>(g.items[0])->value.c_str()) };
                            if (!method)
                                return nullptr;
                            std::vector<object*> rest;
                            for (std::size_t i = 1u; i < g.items.size(); ++i)
                                rest.push_back(borrowed(g.items[i]));
                            return call_with(method.get(), std::move(rest), g.kwargs);
                        }
                    }
                };
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& g = *reinterpret_cast<getter_object*>(aSelf);
                    std::string text;
                    if (!join_repr(g.items, text))
                        return nullptr;
                    if (g.kwargs != nullptr)
                        for (auto const& item : as<dict_object>(g.kwargs)->items)
                        {
                            std::string value;
                            if (!repr_of(item.second, value))
                                return nullptr;
                            text += ", " + as<str_object>(item.first)->value + "=" + value;
                        }
                    return make_str(std::string{ "operator." } + sGetterNames[g.kind] + "(" + text + ")");
                };
                return type;
            }

            object* new_getter(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const kind = static_cast<int>(reinterpret_cast<std::uintptr_t>(aData));
                auto const& args = as<tuple_object>(aArgs)->items;
                if (kind != 2 && aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                    return error("TypeError", std::string{ sGetterNames[kind] } + "() takes no keyword arguments");
                if (args.empty())
                    return error("TypeError", kind == 2 ? "methodcaller needs at least one argument, the method name" :
                        std::string{ sGetterNames[kind] } + " expected 1 argument, got 0");
                if (kind != 0)
                    for (std::size_t i = 0u; i < (kind == 1 ? args.size() : 1u); ++i)
                        if (kind_of(args[i]) != NEOS_PY_STR)
                            return error("TypeError", kind == 1 ? "attribute name must be a string" : "method name must be a string");
                static neos_py_type* const sTypes[] = { &sItemGetterType, &sAttrGetterType, &sMethodCallerType };
                auto const g = new getter_object{ { { &sTypes[kind]->base } }, kind };
                for (auto a : args)
                    g->items.push_back(borrowed(a));
                if (kind == 2 && aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                    g->kwargs = borrowed(aKwargs);
                return &g->h.base;
            }

            int getter_instancecheck(neos_py_context*, void* aData, object* aObject)
            {
                static neos_py_type* const sTypes[] = { &sItemGetterType, &sAttrGetterType, &sMethodCallerType };
                return aObject->type == &sTypes[reinterpret_cast<std::uintptr_t>(aData)]->base ? 1 : 0;
            }

            bool operator_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& o : sOperators)
                {
                    aAttributes.emplace_back(o.name, make_function(o.name, call_operator, const_cast<operator_entry*>(&o)));
                    aAttributes.emplace_back(std::string{ "__" } + (o.name[std::strlen(o.name) - 1u] == '_' ? std::string{ o.name, std::strlen(o.name) - 1u } :
                        std::string{ o.name }) + "__", make_function(o.name, call_operator, const_cast<operator_entry*>(&o)));
                }
                for (auto const& f : sOperatorFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                for (std::uintptr_t i = 0u; i < 3u; ++i)
                    aAttributes.emplace_back(sGetterNames[i], context()->type_new(context(), sGetterNames[i], (std::string{ "operator." } + sGetterNames[i]).c_str(),
                        new_getter, reinterpret_cast<void*>(i), getter_instancecheck));
                return true;
            }

            // --- heapq (CPython's heapq.py algorithms)

            int less(object* a, object* b)
            {
                ref const r{ compare(NEOS_PY_LT, a, b) };
                if (!r)
                    return -1;
                return truth(r.get());
            }

            bool heap_list(object* aHeap, std::vector<object*>*& aItems)
            {
                if (kind_of(builtin_value(aHeap)) != NEOS_PY_LIST)
                {
                    error("TypeError", "heap argument must be a list");
                    return false;
                }
                aItems = &as<list_object>(builtin_value(aHeap))->items;
                return true;
            }

            // (_siftdown(heap, startpos, pos))
            bool sift_down(std::vector<object*>& aHeap, std::size_t aStart, std::size_t aPos, bool aMax = false)
            {
                auto const item = aHeap[aPos];
                while (aPos > aStart)
                {
                    auto const parentPos = (aPos - 1u) >> 1;
                    auto const parent = aHeap[parentPos];
                    auto const l = aMax ? less(parent, item) : less(item, parent);
                    if (l < 0)
                        return false;
                    if (l == 1)
                    {
                        aHeap[aPos] = parent;
                        aPos = parentPos;
                        continue;
                    }
                    break;
                }
                aHeap[aPos] = item;
                return true;
            }

            // (_siftup(heap, pos))
            bool sift_up(std::vector<object*>& aHeap, std::size_t aPos, bool aMax = false)
            {
                auto const end = aHeap.size();
                auto const start = aPos;
                auto const item = aHeap[aPos];
                auto child = 2u * aPos + 1u;
                while (child < end)
                {
                    auto const right = child + 1u;
                    if (right < end)
                    {
                        auto const l = aMax ? less(aHeap[right], aHeap[child]) : less(aHeap[child], aHeap[right]);
                        if (l < 0)
                            return false;
                        if (l == 0)
                            child = right;
                    }
                    aHeap[aPos] = aHeap[child];
                    aPos = child;
                    child = 2u * aPos + 1u;
                }
                aHeap[aPos] = item;
                return sift_down(aHeap, start, aPos, aMax);
            }

            object* heap_pop(std::vector<object*>& aHeap, bool aMax = false)
            {
                if (aHeap.empty())
                    return error("IndexError", "index out of range");
                auto const last = aHeap.back();
                aHeap.pop_back();
                if (aHeap.empty())
                    return last;
                auto const result = aHeap[0];
                aHeap[0] = last;
                if (!sift_up(aHeap, 0u, aMax))
                    return nullptr;
                return result;
            }

            // (nsmallest and nlargest: as sorted(iterable, key=key)[:n] (reverse for nlargest))
            object* n_items(std::vector<object*> const& aArgs, object* aKwargs, bool aLargest)
            {
                std::vector<object*> a;
                if (!parse_arguments(aLargest ? "nlargest" : "nsmallest", aArgs, aKwargs, { "n", "iterable", "key" }, 2u, 0u, a))
                    return nullptr;
                long long n = 0;
                if (!to_index(a[0], n))
                    return nullptr;
                ref kwargs{ make_dict() };
                if (a[2] != nullptr)
                {
                    ref const key{ make_str("key") };
                    dict_set(kwargs.get(), key.get(), a[2]);
                }
                if (aLargest)
                {
                    ref const key{ make_str("reverse") };
                    dict_set(kwargs.get(), key.get(), make_bool(true));
                }
                ref sorted{ b_sorted({ a[1] }, kwargs.get()) };
                if (!sorted)
                    return nullptr;
                auto& items = as<list_object>(sorted.get())->items;
                if (n < 0)
                    n = 0;
                while (static_cast<long long>(items.size()) > n)
                {
                    release(items.back());
                    items.pop_back();
                }
                return sorted.release_ownership();
            }

            builtin_entry const sHeapqFunctions[] =
            {
                { "heappush", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::vector<object*>* heap = nullptr;
                        if (!parse_arguments("heappush", aArgs, aKwargs, { "heap", "item" }, 2u, 2u, a) || !heap_list(a[0], heap))
                            return nullptr;
                        heap->push_back(borrowed(a[1]));
                        return sift_down(*heap, 0u, heap->size() - 1u) ? borrowed(none()) : nullptr;
                    } },
                { "heappop", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::vector<object*>* heap = nullptr;
                        if (!parse_arguments("heappop", aArgs, aKwargs, { "heap" }, 1u, 1u, a) || !heap_list(a[0], heap))
                            return nullptr;
                        return heap_pop(*heap);
                    } },
                { "heapify", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::vector<object*>* heap = nullptr;
                        if (!parse_arguments("heapify", aArgs, aKwargs, { "heap" }, 1u, 1u, a) || !heap_list(a[0], heap))
                            return nullptr;
                        for (auto i = heap->size() / 2u; i-- > 0u;)
                            if (!sift_up(*heap, i))
                                return nullptr;
                        return borrowed(none());
                    } },
                { "heapreplace", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::vector<object*>* heap = nullptr;
                        if (!parse_arguments("heapreplace", aArgs, aKwargs, { "heap", "item" }, 2u, 2u, a) || !heap_list(a[0], heap))
                            return nullptr;
                        if (heap->empty())
                            return error("IndexError", "index out of range");
                        auto const result = (*heap)[0];
                        (*heap)[0] = borrowed(a[1]);
                        if (!sift_up(*heap, 0u))
                            return nullptr;
                        return result;
                    } },
                { "heappushpop", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::vector<object*>* heap = nullptr;
                        if (!parse_arguments("heappushpop", aArgs, aKwargs, { "heap", "item" }, 2u, 2u, a) || !heap_list(a[0], heap))
                            return nullptr;
                        if (!heap->empty())
                        {
                            auto const l = less((*heap)[0], a[1]);
                            if (l < 0)
                                return nullptr;
                            if (l == 1)
                            {
                                auto const result = (*heap)[0];
                                (*heap)[0] = borrowed(a[1]);
                                if (!sift_up(*heap, 0u))
                                    return nullptr;
                                return result;
                            }
                        }
                        return borrowed(a[1]);
                    } },
                { "nsmallest", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return n_items(aArgs, aKwargs, false); } },
                { "nlargest", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return n_items(aArgs, aKwargs, true); } },
                { "merge", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        // (a stable merge of sorted iterables: their items sorted (stably) by key)
                        object* key = nullptr;
                        object* reverse = nullptr;
                        if (aKwargs != nullptr)
                            for (auto const& item : as<dict_object>(aKwargs)->items)
                            {
                                auto const& name = as<str_object>(item.first)->value;
                                if (name == "key")
                                    key = item.second;
                                else if (name == "reverse")
                                    reverse = item.second;
                                else
                                    return error("TypeError", "merge() got an unexpected keyword argument '" + name + "'");
                            }
                        ref all{ make_list() };
                        for (auto a : aArgs)
                        {
                            std::vector<object*> items;
                            if (!collect(a, items))
                                return nullptr;
                            auto& target = as<list_object>(all.get())->items;
                            target.insert(target.end(), items.begin(), items.end());
                        }
                        ref kwargs{ make_dict() };
                        if (key != nullptr)
                        {
                            ref const k{ make_str("key") };
                            dict_set(kwargs.get(), k.get(), key);
                        }
                        if (reverse != nullptr)
                        {
                            ref const k{ make_str("reverse") };
                            dict_set(kwargs.get(), k.get(), reverse);
                        }
                        ref const sorted{ b_sorted({ all.get() }, kwargs.get()) };
                        return sorted ? iter(sorted.get()) : nullptr;
                    } },
                { "_heappop_max", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        std::vector<object*>* heap = nullptr;
                        if (aArgs.size() != 1u || !heap_list(aArgs[0], heap))
                            return nullptr;
                        return heap_pop(*heap, true);
                    } },
                { "_heapify_max", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        std::vector<object*>* heap = nullptr;
                        if (aArgs.size() != 1u || !heap_list(aArgs[0], heap))
                            return nullptr;
                        for (auto i = heap->size() / 2u; i-- > 0u;)
                            if (!sift_up(*heap, i, true))
                                return nullptr;
                        return borrowed(none());
                    } }
            };

            bool heapq_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sHeapqFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                return true;
            }

            // --- bisect

            object* bisect(std::vector<object*> const& aArgs, object* aKwargs, char const* aName, bool aRight, bool aInsert)
            {
                std::vector<object*> a;
                if (!parse_arguments(aName, aArgs, aKwargs, { "a", "x", "lo", "hi", "key" }, 2u, 0u, a))
                    return nullptr;
                long long lo = 0, hi = -1;
                if (a[2] != nullptr && !to_index(a[2], lo))
                    return nullptr;
                if (lo < 0)
                    return error("ValueError", "lo must be non-negative");
                if (a[3] != nullptr && kind_of(a[3]) != NEOS_PY_NONE && !to_index(a[3], hi))
                    return nullptr;
                if (hi < 0 && (a[3] == nullptr || kind_of(a[3]) == NEOS_PY_NONE))
                {
                    hi = length(a[0]);
                    if (hi < 0)
                        return nullptr;
                }
                auto const key = a[4] != nullptr && kind_of(a[4]) != NEOS_PY_NONE ? a[4] : nullptr;
                ref x{ borrowed(a[1]) };
                if (key != nullptr && !aInsert)
                {
                    // (bisect: x is a key; insort: key(x))
                }
                else if (key != nullptr)
                {
                    x = ref{ call_with(key, { borrowed(a[1]) }) };
                    if (!x)
                        return nullptr;
                }
                while (lo < hi)
                {
                    auto const mid = (lo + hi) / 2;
                    ref const index{ make_int(mid) };
                    ref item{ getitem(a[0], index.get()) };
                    if (!item)
                        return nullptr;
                    if (key != nullptr)
                    {
                        item = ref{ call_with(key, { item.release_ownership() }) };
                        if (!item)
                            return nullptr;
                    }
                    auto const l = aRight ? less(x.get(), item.get()) : less(item.get(), x.get());
                    if (l < 0)
                        return nullptr;
                    if (aRight ? l == 1 : l == 0)
                        hi = mid;
                    else
                        lo = mid + 1;
                }
                if (!aInsert)
                    return make_int(lo);
                ref const insert{ getattr(a[0], "insert") };
                if (!insert)
                    return nullptr;
                ref const r{ call_with(insert.get(), { make_int(lo), borrowed(a[1]) }) };
                return r ? borrowed(none()) : nullptr;
            }

            builtin_entry const sBisectFunctions[] =
            {
                { "bisect_left", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return bisect(aArgs, aKwargs, "bisect_left", false, false); } },
                { "bisect_right", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return bisect(aArgs, aKwargs, "bisect_right", true, false); } },
                { "bisect", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return bisect(aArgs, aKwargs, "bisect_right", true, false); } },
                { "insort_left", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return bisect(aArgs, aKwargs, "insort_left", false, true); } },
                { "insort_right", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return bisect(aArgs, aKwargs, "insort_right", true, true); } },
                { "insort", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return bisect(aArgs, aKwargs, "insort_right", true, true); } }
            };

            bool bisect_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sBisectFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                return true;
            }

            // --- copy

            object* deep_copy(object* aObject, object* aMemo);
            object* shallow_copy(object* aObject);

            bool is_atomic(object* aObject)
            {
                switch (kind_of(aObject))
                {
                case NEOS_PY_NONE: case NEOS_PY_NOT_IMPLEMENTED: case NEOS_PY_ELLIPSIS: case NEOS_PY_BOOL: case NEOS_PY_INT: case NEOS_PY_FLOAT:
                case NEOS_PY_COMPLEX: case NEOS_PY_STR: case NEOS_PY_RANGE: case NEOS_PY_FUNCTION: case NEOS_PY_SLICE:
                    return true;
                default:
                    break;
                }
                if (is_class(aObject))
                    return true;
                if (auto const t = python_type(aObject))
                {
                    std::string_view const name{ t->name };
                    return name == "type" || name == "function" || name == "bytes" || name == "frozenset" || name == "builtin_function_or_method";
                }
                return false;
            }

            // (a user class's instance: a new instance with a copy of its attributes (and value))
            object* copy_instance(object* aObject, object* aMemo)
            {
                ref const cls{ class_of(aObject) };
                auto const base = builtin_value(aObject);
                ref result;
                if (base != aObject)
                {
                    // (a subclass of a builtin type: a copy of its value)
                    ref copied;
                    if (aMemo != nullptr)
                        copied = ref{ deep_copy(base, aMemo) };
                    else if (kind_of(base) == NEOS_PY_LIST || kind_of(base) == NEOS_PY_DICT)
                        copied = ref{ shallow_copy(base) };
                    else
                        copied = ref::borrowed(base);
                    if (!copied)
                        return nullptr;
                    bool const mutableValue = kind_of(base) == NEOS_PY_LIST || kind_of(base) == NEOS_PY_DICT || 
                        (python_type(base) != nullptr && (std::string_view{ python_type(base)->name } == "set" || std::string_view{ python_type(base)->name } == "bytearray"));
                    ref const args{ mutableValue ? make_tuple() : make_tuple({ borrowed(copied.get()) }) };
                    result = ref{ new_instance(cls.get(), args.get(), nullptr) };
                    if (!result)
                        return nullptr;
                    if (mutableValue)
                    {
                        ref const type{ class_of(base) };
                        ref const init{ getattr(type.get(), "__init__") };
                        if (!init)
                            return nullptr;
                        ref const r{ call_with(init.get(), { borrowed(result.get()), borrowed(copied.get()) }) };
                        if (!r)
                            return nullptr;
                    }
                }
                else
                {
                    ref const empty{ make_tuple() };
                    result = ref{ new_instance(cls.get(), empty.get(), nullptr) };
                    if (!result)
                        return nullptr;
                }
                if (aMemo != nullptr)
                {
                    ref const id{ make_int(static_cast<long long>(reinterpret_cast<std::uintptr_t>(aObject))) };
                    dict_set(aMemo, id.get(), result.get());
                }
                ref const dict{ getattr(aObject, "__dict__") };
                if (!dict)
                    return nullptr;
                for (auto const& item : as<dict_object>(dict.get())->items)
                {
                    ref const v{ aMemo != nullptr ? deep_copy(item.second, aMemo) : borrowed(item.second) };
                    if (!v || setattr(result.get(), as<str_object>(item.first)->value.c_str(), v.get()) < 0)
                        return nullptr;
                }
                return result.release_ownership();
            }

            // (an instance whose class defines __reduce_ex__ or __reduce__: copied as CPython's copy._reconstruct does (a str: the object 
            // itself); aHandled false if it defines neither)
            object* reduce_copy(object* aObject, object* aMemo, bool& aHandled)
            {
                aHandled = false;
                ref reductor{ special_method(aObject, "__reduce_ex__") };
                bool const ex = static_cast<bool>(reductor);
                if (!reductor)
                    reductor = ref{ special_method(aObject, "__reduce__") };
                if (!reductor)
                    return nullptr;
                aHandled = true;
                ref const rv{ ex ? call_with(reductor.get(), { make_int(4) }) : call_with(reductor.get(), {}) };
                if (!rv)
                    return nullptr;
                if (kind_of(rv.get()) == NEOS_PY_STR)
                    return borrowed(aObject);
                if (kind_of(rv.get()) != NEOS_PY_TUPLE || as<tuple_object>(rv.get())->items.size() < 2u)
                    return error("copy.Error", std::string{ "un(deep)copyable object of type " } + type_name(aObject));
                auto const& parts = as<tuple_object>(rv.get())->items;
                auto const part = [&](std::size_t i) -> object* { return i < parts.size() && kind_of(parts[i]) != NEOS_PY_NONE ? parts[i] : nullptr; };
                bool const deep = aMemo != nullptr;
                ref args{ deep ? deep_copy(parts[1], aMemo) : borrowed(parts[1]) };
                if (!args)
                    return nullptr;
                ref y{ call(parts[0], args.get(), nullptr) };
                if (!y)
                    return nullptr;
                if (deep)
                {
                    ref const id{ make_int(static_cast<long long>(reinterpret_cast<std::uintptr_t>(aObject))) };
                    dict_set(aMemo, id.get(), y.get());
                }
                if (auto const s = part(2u))
                {
                    ref const state{ deep ? deep_copy(s, aMemo) : borrowed(s) };
                    if (!state)
                        return nullptr;
                    if (ref const setstate{ getattr(y.get(), "__setstate__") })
                    {
                        if (!ref{ call_with(setstate.get(), { borrowed(state.get()) }) })
                            return nullptr;
                    }
                    else
                    {
                        clear_error();
                        object* dictState = state.get();
                        object* slotState = nullptr;
                        if (kind_of(dictState) == NEOS_PY_TUPLE && as<tuple_object>(dictState)->items.size() == 2u)
                        {
                            slotState = as<tuple_object>(dictState)->items[1];
                            dictState = as<tuple_object>(dictState)->items[0];
                        }
                        for (auto const st : { dictState, slotState })
                            if (st != nullptr && kind_of(st) == NEOS_PY_DICT)
                                for (auto const& item : as<dict_object>(st)->items)
                                    if (kind_of(item.first) == NEOS_PY_STR && setattr(y.get(), as<str_object>(item.first)->value.c_str(), item.second) < 0)
                                        return nullptr;
                    }
                }
                if (auto const listItems = part(3u))
                {
                    ref const append{ getattr(y.get(), "append") };
                    std::vector<object*> items;
                    if (!append || !collect(listItems, items))
                        return nullptr;
                    for (auto i : items)
                    {
                        ref const item{ i };
                        ref const v{ deep ? deep_copy(i, aMemo) : borrowed(i) };
                        if (!v || !ref{ call_with(append.get(), { borrowed(v.get()) }) })
                            return nullptr;
                    }
                }
                if (auto const dictItems = part(4u))
                {
                    std::vector<object*> items;
                    if (!collect(dictItems, items))
                        return nullptr;
                    for (auto i : items)
                    {
                        ref const item{ i };
                        if (kind_of(i) != NEOS_PY_TUPLE || as<tuple_object>(i)->items.size() != 2u)
                            return error("ValueError", "dictitems must yield (key, value) pairs");
                        ref const k{ deep ? deep_copy(as<tuple_object>(i)->items[0], aMemo) : borrowed(as<tuple_object>(i)->items[0]) };
                        ref const v{ deep ? deep_copy(as<tuple_object>(i)->items[1], aMemo) : borrowed(as<tuple_object>(i)->items[1]) };
                        if (!k || !v || setitem(y.get(), k.get(), v.get()) < 0)
                            return nullptr;
                    }
                }
                return y.release_ownership();
            }

            object* shallow_copy(object* aObject);

            object* shallow_copy(object* aObject)
            {
                if (is_atomic(aObject))
                    return borrowed(aObject);
                switch (kind_of(aObject))
                {
                case NEOS_PY_TUPLE:
                    return borrowed(aObject);
                case NEOS_PY_LIST:
                    {
                        auto items = as<list_object>(aObject)->items;
                        for (auto i : items)
                            retain(i);
                        return make_list(std::move(items));
                    }
                case NEOS_PY_DICT:
                    {
                        ref result{ make_dict() };
                        return dict_update(result.get(), aObject) ? result.release_ownership() : nullptr;
                    }
                default:
                    break;
                }
                if (is_instance(aObject))
                {
                    if (ref const method{ special_method(aObject, "__copy__") })
                        return call_with(method.get(), {});
                    bool reduced = false;
                    if (auto const copied = reduce_copy(aObject, nullptr, reduced); reduced)
                        return copied;
                    return copy_instance(aObject, nullptr);
                }
                // (a type of a module: its copy() or __copy__)
                for (auto name : { "__copy__", "copy" })
                {
                    ref const method{ getattr(aObject, name) };
                    if (method)
                        return call_with(method.get(), {});
                    clear_error();
                }
                return error("TypeError", std::string{ "cannot copy '" } + type_name(aObject) + "' object");
            }

            object* deep_copy(object* aObject, object* aMemo)
            {
                if (is_atomic(aObject))
                    return borrowed(aObject);
                ref const id{ make_int(static_cast<long long>(reinterpret_cast<std::uintptr_t>(aObject))) };
                auto const position = dict_find(aMemo, id.get());
                if (position >= 0)
                    return borrowed(as<dict_object>(aMemo)->items[static_cast<std::size_t>(position)].second);
                ref result;
                switch (kind_of(aObject))
                {
                case NEOS_PY_LIST:
                    {
                        result = ref{ make_list() };
                        dict_set(aMemo, id.get(), result.get());
                        auto const items = as<list_object>(aObject)->items;
                        for (auto i : items)
                        {
                            auto const c = deep_copy(i, aMemo);
                            if (c == nullptr)
                                return nullptr;
                            as<list_object>(result.get())->items.push_back(c);
                        }
                        return result.release_ownership();
                    }
                case NEOS_PY_TUPLE:
                    {
                        std::vector<object*> items;
                        bool same = true;
                        for (auto i : as<tuple_object>(aObject)->items)
                        {
                            auto const c = deep_copy(i, aMemo);
                            if (c == nullptr)
                            {
                                for (auto x : items)
                                    release(x);
                                return nullptr;
                            }
                            same = same && c == i;
                            items.push_back(c);
                        }
                        // (a tuple that a copy of an item refers to: that copy's)
                        auto const again = dict_find(aMemo, id.get());
                        if (again >= 0)
                        {
                            for (auto x : items)
                                release(x);
                            return borrowed(as<dict_object>(aMemo)->items[static_cast<std::size_t>(again)].second);
                        }
                        if (same)
                        {
                            for (auto x : items)
                                release(x);
                            return borrowed(aObject);
                        }
                        result = ref{ make_tuple(std::move(items)) };
                        dict_set(aMemo, id.get(), result.get());
                        return result.release_ownership();
                    }
                case NEOS_PY_DICT:
                    {
                        result = ref{ make_dict() };
                        dict_set(aMemo, id.get(), result.get());
                        auto const items = as<dict_object>(aObject)->items;
                        for (auto const& item : items)
                        {
                            ref const k{ deep_copy(item.first, aMemo) };
                            ref const v{ deep_copy(item.second, aMemo) };
                            if (!k || !v)
                                return nullptr;
                            dict_set(result.get(), k.get(), v.get());
                        }
                        return result.release_ownership();
                    }
                default:
                    break;
                }
                if (is_instance(aObject))
                {
                    if (ref const method{ special_method(aObject, "__deepcopy__") })
                    {
                        result = ref{ call_with(method.get(), { borrowed(aMemo) }) };
                        if (result)
                            dict_set(aMemo, id.get(), result.get());
                        return result.release_ownership();
                    }
                    bool reduced = false;
                    if (auto const copied = reduce_copy(aObject, aMemo, reduced); reduced)
                        return copied;
                    return copy_instance(aObject, aMemo);
                }
                // (a type of a module with a __deepcopy__ (e.g. array.array))
                if (python_type(aObject) != nullptr)
                {
                    if (ref const method{ getattr(aObject, "__deepcopy__") })
                    {
                        result = ref{ call_with(method.get(), { borrowed(aMemo) }) };
                        if (result)
                            dict_set(aMemo, id.get(), result.get());
                        return result.release_ownership();
                    }
                    clear_error();
                }
                // (a set, a deque etc.: its type called with a deep copy of its items)
                if (auto const t = python_type(aObject); t != nullptr && t->iter != nullptr)
                {
                    std::vector<object*> items;
                    if (!collect(aObject, items))
                        return nullptr;
                    std::vector<object*> copies;
                    bool ok = true;
                    for (auto i : items)
                    {
                        if (ok)
                        {
                            auto const c = deep_copy(i, aMemo);
                            ok = c != nullptr;
                            if (ok)
                                copies.push_back(c);
                        }
                        release(i);
                    }
                    ref const list{ make_list(std::move(copies)) };
                    if (!ok)
                        return nullptr;
                    ref const cls{ class_of(aObject) };
                    ref kwargs;
                    if (ref const maxlen{ getattr(aObject, "maxlen") })
                    {
                        kwargs = ref{ make_dict() };
                        ref const key{ make_str("maxlen") };
                        dict_set(kwargs.get(), key.get(), maxlen.get());
                    }
                    clear_error();
                    result = ref{ call_with(cls.get(), { borrowed(list.get()) }, kwargs.get()) };
                    if (result)
                        dict_set(aMemo, id.get(), result.get());
                    return result.release_ownership();
                }
                return shallow_copy(aObject);
            }

            builtin_entry const sCopyFunctions[] =
            {
                { "copy", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("copy", aArgs, aKwargs, { "x" }, 1u, 0u, a))
                            return nullptr;
                        return shallow_copy(a[0]);
                    } },
                { "deepcopy", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("deepcopy", aArgs, aKwargs, { "x", "memo" }, 1u, 0u, a))
                            return nullptr;
                        ref memo{ a[1] != nullptr && kind_of(a[1]) == NEOS_PY_DICT ? borrowed(a[1]) : make_dict() };
                        return deep_copy(a[0], memo.get());
                    } },
                { "replace", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aArgs.size() != 1u)
                            return error("TypeError", "replace() takes 1 positional argument");
                        ref const method{ getattr(aArgs[0], "__replace__") };
                        if (!method)
                        {
                            clear_error();
                            ref const r{ getattr(aArgs[0], "_replace") };
                            if (!r)
                            {
                                clear_error();
                                return error("TypeError", std::string{ "replace() does not support " } + type_name(aArgs[0]) + " objects");
                            }
                            return call_with(r.get(), {}, aKwargs);
                        }
                        return call_with(method.get(), {}, aKwargs);
                    } }
            };

            bool copy_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sCopyFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                if (!is_exception_class("copy.Error"))
                    user_exception_bases()["copy.Error"] = { "Exception" };
                aAttributes.emplace_back("Error", exception_class("copy.Error"));
                return true;
            }

            inline bool const sOperator = register_module("operator", operator_module);
            inline bool const sHeapq = register_module("heapq", heapq_module);
            inline bool const sBisect = register_module("bisect", bisect_module);
            inline bool const sCopy = register_module("copy", copy_module);
        }
    }
}
