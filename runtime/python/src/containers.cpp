/*
  containers.cpp

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

// list's, tuple's and dict's methods (those that objects.cpp does not have), and dict's views (dict_keys, dict_values and dict_items)

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            // --- dict views

            struct view_object
            {
                header h;
                object* dict;
                int which;      ///< (0: keys, 1: values, 2: items)
                ~view_object() { release(dict); }
            };

            struct view_iterator_object
            {
                header h;
                object* dict;
                int which;
                std::size_t index = 0u;
                std::size_t size = 0u;
                ~view_iterator_object() { release(dict); }
            };

            char const* const sViewNames[] = { "dict_keys", "dict_values", "dict_items" };
            char const* const sIteratorNames[] = { "dict_keyiterator", "dict_valueiterator", "dict_itemiterator" };

            neos_py_type make_view_type(int aWhich);
            neos_py_type make_view_iterator_type(int aWhich);
            neos_py_type sViewTypes[] = { make_view_type(0), make_view_type(1), make_view_type(2) };
            neos_py_type sViewIteratorTypes[] = { make_view_iterator_type(0), make_view_iterator_type(1), make_view_iterator_type(2) };

            view_object& as_view(object* aObject) { return *reinterpret_cast<view_object*>(aObject); }

            bool is_view(object* aObject, int aWhich = -1)
            {
                if (aObject == nullptr)
                    return false;
                for (int i = 0; i < 3; ++i)
                    if (aObject->type == &sViewTypes[i].base && (aWhich == -1 || aWhich == i))
                        return true;
                return false;
            }

            object* make_view(object* aDict, int aWhich)
            {
                retain(aDict);
                return &(new view_object{ { { &sViewTypes[aWhich].base } }, aDict, aWhich })->h.base;
            }

            object* view_item(dict_object const& d, std::size_t aIndex, int aWhich)
            {
                auto const& item = d.items[aIndex];
                if (aWhich == 0)
                    return (retain(item.first), item.first);
                if (aWhich == 1)
                    return (retain(item.second), item.second);
                retain(item.first);
                retain(item.second);
                return make_tuple({ item.first, item.second });
            }

            // (a view as a set (keys and items))
            object* view_set(object* aView)
            {
                std::vector<object*> args{ aView };
                return b_set(args, nullptr);
            }

            neos_py_type make_view_iterator_type(int aWhich)
            {
                auto type = make_type(sIteratorNames[aWhich], sIteratorNames[aWhich], retain_object, release_object<view_iterator_object>);
                type.iter = [](neos_py_context*, object* aSelf) -> object* { retain(aSelf); return aSelf; };
                type.next = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto& it = *reinterpret_cast<view_iterator_object*>(aSelf);
                    if (it.dict == nullptr)
                        return nullptr;
                    auto const& d = *as<dict_object>(it.dict);
                    if (d.items.size() != it.size)
                    {
                        it.size = std::numeric_limits<std::size_t>::max();
                        return error("RuntimeError", "dictionary changed size during iteration");
                    }
                    if (it.index >= d.items.size())
                    {
                        release(it.dict);
                        it.dict = nullptr;
                        return nullptr;
                    }
                    return view_item(d, it.index++, it.which);
                };
                type.length = [](neos_py_context*, object* aSelf) -> long long
                {
                    auto const& it = *reinterpret_cast<view_iterator_object*>(aSelf);
                    return it.dict == nullptr || it.size != as<dict_object>(it.dict)->items.size() ? 0 : static_cast<long long>(it.size - it.index);
                };
                return type;
            }

            neos_py_type make_view_type(int aWhich)
            {
                auto type = make_type(sViewNames[aWhich], sViewNames[aWhich], retain_object, release_object<view_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& v = as_view(aSelf);
                    std::vector<object*> items;
                    auto const& d = *as<dict_object>(v.dict);
                    for (std::size_t i = 0u; i < d.items.size(); ++i)
                        items.push_back(view_item(d, i, v.which));
                    ref const list{ make_list(std::move(items)) };
                    std::string text;
                    if (!repr_of(list.get(), text))
                        return nullptr;
                    return make_str(std::string{ sViewNames[v.which] } + "(" + text + ")");
                };
                type.length = [](neos_py_context*, object* aSelf) -> long long { return static_cast<long long>(as<dict_object>(as_view(aSelf).dict)->items.size()); };
                type.iter = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& v = as_view(aSelf);
                    retain(v.dict);
                    return &(new view_iterator_object{ { { &sViewIteratorTypes[v.which].base } }, v.dict, v.which, 0u, as<dict_object>(v.dict)->items.size() })->h.base;
                };
                type.contains = [](neos_py_context*, object* aSelf, object* aItem) -> int
                {
                    auto const& v = as_view(aSelf);
                    auto const& d = *as<dict_object>(v.dict);
                    if (v.which == 0)
                        return contains(v.dict, aItem);
                    if (v.which == 2)
                    {
                        if (kind_of(aItem) != NEOS_PY_TUPLE || as<tuple_object>(aItem)->items.size() != 2u)
                            return 0;
                        auto const position = dict_find(v.dict, as<tuple_object>(aItem)->items[0]);
                        if (position == -2)
                            return -1;
                        if (position < 0)
                            return 0;
                        return equal(d.items[static_cast<std::size_t>(position)].second, as<tuple_object>(aItem)->items[1]);
                    }
                    for (auto const& item : d.items)
                    {
                        auto const e = equal(item.second, aItem);
                        if (e != 0)
                            return e;
                    }
                    return 0;
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    auto const& v = as_view(aSelf);
                    if (std::strcmp(aName, "mapping") == 0)
                        return error("AttributeError", "'mappingproxy' is not supported");
                    if (std::strcmp(aName, "__reversed__") == 0)
                        return make_function(aName, [](neos_py_context*, void*, object* aArgs, object*) -> object*
                        {
                            auto const& v = as_view(as<tuple_object>(aArgs)->items[0]);
                            auto const& d = *as<dict_object>(v.dict);
                            std::vector<object*> items;
                            for (std::size_t i = d.items.size(); i-- > 0u;)
                                items.push_back(view_item(d, i, v.which));
                            ref const list{ make_list(std::move(items)) };
                            return iter(list.get());
                        }, nullptr, aSelf);
                    if (v.which != 1 && std::strcmp(aName, "isdisjoint") == 0)
                    {
                        ref const s{ view_set(aSelf) };
                        return s ? getattr(s.get(), aName) : nullptr;
                    }
                    return error("AttributeError", std::string{ "'" } + sViewNames[v.which] + "' object has no attribute '" + aName + "'");
                };
                if (aWhich != 1)
                {
                    type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                    {
                        // (as sets)
                        ref const lhs{ is_view(aLhs) && as_view(aLhs).which != 1 ? view_set(aLhs) : (retain(aLhs), aLhs) };
                        ref const rhs{ is_view(aRhs) && as_view(aRhs).which != 1 ? view_set(aRhs) : (retain(aRhs), aRhs) };
                        if (!lhs || !rhs)
                            return nullptr;
                        auto const k = python_type(lhs.get()) != nullptr ? std::string_view{ python_type(lhs.get())->name } : std::string_view{};
                        auto const rk = python_type(rhs.get()) != nullptr ? std::string_view{ python_type(rhs.get())->name } : std::string_view{};
                        bool const sets = (k == "set" || k == "frozenset") && (rk == "set" || rk == "frozenset");
                        if (!sets)
                            return (retain(not_implemented()), not_implemented());
                        return compare(aOperation, lhs.get(), rhs.get());
                    };
                    type.binary = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs, int) -> object*
                    {
                        if (aOperation != NEOS_PY_AND && aOperation != NEOS_PY_OR && aOperation != NEOS_PY_XOR && aOperation != NEOS_PY_SUBTRACT)
                            return (retain(not_implemented()), not_implemented());
                        // (the result is a set: the operands as sets (any iterable))
                        ref const lhs{ is_view(aLhs) ? view_set(aLhs) : view_set(aLhs) };
                        ref const rhs{ is_view(aRhs) ? view_set(aRhs) : view_set(aRhs) };
                        if (!lhs || !rhs)
                            return nullptr;
                        return binary(aOperation, lhs.get(), rhs.get(), false);
                    };
                }
                return type;
            }

            // --- list's, tuple's and dict's methods

            struct container_method_entry
            {
                int kind;
                char const* name;
                object* (*function)(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs);
            };

            object* sequence_index_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                bool const list = kind_of(aSelf) == NEOS_PY_LIST;
                std::vector<object*> a;
                if (!parse_arguments(list ? "list.index" : "tuple.index", aArgs, aKwargs, { "value", "start", "stop" }, 1u, 3u, a))
                    return nullptr;
                auto const& items = list ? as<list_object>(aSelf)->items : as<tuple_object>(aSelf)->items;
                auto const n = static_cast<long long>(items.size());
                long long start = 0, stop = std::numeric_limits<long long>::max();
                for (int i = 1; i <= 2; ++i)
                    if (a[i] != nullptr && !to_index(a[i], i == 1 ? start : stop))
                    {
                        clear_error();
                        return error("TypeError", "slice indices must be integers or have an __index__ method");
                    }
                if (start < 0)
                    start = std::max(0ll, start + n);
                if (stop < 0)
                    stop = std::max(0ll, stop + n);
                for (auto i = start; i < std::min(stop, static_cast<long long>(items.size())); ++i)
                {
                    auto const e = equal(items[static_cast<std::size_t>(i)], a[0]);
                    if (e < 0)
                        return nullptr;
                    if (e == 1)
                        return make_int(i);
                }
                if (!list)
                    return error("ValueError", "tuple.index(x): x not in tuple");
                std::string value;
                if (!repr_of(a[0], value))
                    return nullptr;
                return error("ValueError", value + " is not in list");
            }

            object* list_sort_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                if (!aArgs.empty())
                    return error("TypeError", "sort() takes no positional arguments");
                std::vector<object*> a;
                if (!parse_arguments("sort", aArgs, aKwargs, { "key", "reverse" }, 0u, 0u, a))
                    return nullptr;
                // (sorted's algorithm: a copy, which then replaces the items)
                ref const copy{ make_list(as<list_object>(aSelf)->items) };
                for (auto i : as<list_object>(copy.get())->items)
                    retain(i);
                ref const sorted{ b_sorted({ copy.get() }, aKwargs) };
                if (!sorted)
                    return nullptr;
                std::swap(as<list_object>(aSelf)->items, as<list_object>(sorted.get())->items);
                return none();
            }

            // (whether a subclass of dict overrides keys() (dict.update then uses it))
            bool has_keys_override(object* aObject)
            {
                ref const keys{ special_method(aObject, "keys") };
                return static_cast<bool>(keys);
            }

            // (dict.update's and dict()'s argument: a mapping (keys()) or an iterable of pairs)
            bool dict_merge(object* aDict, object* aOther)
            {
                if (kind_of(aOther) == NEOS_PY_DICT || (kind_of(builtin_value(aOther)) == NEOS_PY_DICT && !has_keys_override(aOther)))
                {
                    auto const items = as<dict_object>(builtin_value(aOther))->items;
                    for (auto const& item : items)
                        if (dict_set(aDict, item.first, item.second) != 0)
                            return false;
                    return true;
                }
                ref const keys{ getattr(aOther, "keys") };
                if (keys)
                {
                    ref const empty{ make_tuple() };
                    ref const keyList{ call(keys.get(), empty.get(), nullptr) };
                    std::vector<object*> items;
                    if (!keyList || !collect(keyList.get(), items))
                        return false;
                    bool ok = true;
                    for (auto k : items)
                    {
                        if (ok)
                        {
                            ref const value{ getitem(aOther, k) };
                            ok = value && dict_set(aDict, k, value.get()) == 0;
                        }
                        release(k);
                    }
                    return ok;
                }
                clear_error();
                ref const it{ iter(aOther) };
                if (!it)
                {
                    clear_error();
                    error("TypeError", std::string{ "'" } + type_name(aOther) + "' object is not iterable");
                    return false;
                }
                for (long long index = 0;; ++index)
                {
                    ref const element{ next(it.get()) };
                    if (!element)
                        return !failed();
                    std::vector<object*> pair;
                    ref const pairIt{ iter(element.get()) };
                    if (!pairIt)
                    {
                        clear_error();
                        error("TypeError", "cannot convert dictionary update sequence element #" + std::to_string(index) + " to a sequence");
                        return false;
                    }
                    if (!collect(element.get(), pair))
                        return false;
                    std::vector<ref> owned;
                    for (auto p : pair)
                        owned.emplace_back(p);
                    if (pair.size() != 2u)
                    {
                        error("ValueError", "dictionary update sequence element #" + std::to_string(index) + " has length " + std::to_string(pair.size()) + "; 2 is required");
                        return false;
                    }
                    if (dict_set(aDict, pair[0], pair[1]) != 0)
                        return false;
                }
            }

            object* dict_update_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                if (aArgs.size() > 1u)
                    return error("TypeError", "update expected at most 1 argument, got " + std::to_string(aArgs.size()));
                if (!aArgs.empty() && !dict_merge(aSelf, aArgs[0]))
                    return nullptr;
                if (aKwargs != nullptr && !dict_merge(aSelf, aKwargs))
                    return nullptr;
                return none();
            }

            object* dict_pop_method(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("pop", aArgs, aKwargs, { "key", "default" }, 1u, 2u, a))
                    return nullptr;
                auto const position = dict_find(aSelf, a[0]);
                if (position == -2)
                    return nullptr;
                if (position < 0)
                {
                    if (a[1] != nullptr)
                        return (retain(a[1]), a[1]);
                    raise_key_error(a[0]);
                    return nullptr;
                }
                auto& d = *as<dict_object>(aSelf);
                auto const value = d.items[static_cast<std::size_t>(position)].second;
                retain(value);
                dict_erase(d, static_cast<std::size_t>(position));
                return value;
            }

            container_method_entry const sContainerMethods[] =
            {
                { NEOS_PY_LIST, "index", sequence_index_method },
                { NEOS_PY_TUPLE, "index", sequence_index_method },
                { NEOS_PY_LIST, "sort", list_sort_method },
                { NEOS_PY_LIST, "remove", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("list.remove", aArgs, aKwargs, { "value" }, 1u, 1u, a))
                            return nullptr;
                        auto& items = as<list_object>(aSelf)->items;
                        for (std::size_t i = 0u; i < items.size(); ++i)
                        {
                            auto const e = equal(items[i], a[0]);
                            if (e < 0)
                                return nullptr;
                            if (e == 1)
                            {
                                auto const item = items[i];
                                items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
                                release(item);
                                return none();
                            }
                        }
                        return error("ValueError", "list.remove(x): x not in list");
                    } },
                { NEOS_PY_LIST, "clear", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("list.clear", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        std::vector<object*> items;
                        std::swap(items, as<list_object>(aSelf)->items);
                        for (auto i : items)
                            release(i);
                        return none();
                    } },
                { NEOS_PY_DICT, "keys", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { std::vector<object*> a; return parse_arguments("dict.keys", aArgs, aKwargs, {}, 0u, 0u, a) ? make_view(aSelf, 0) : nullptr; } },
                { NEOS_PY_DICT, "values", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { std::vector<object*> a; return parse_arguments("dict.values", aArgs, aKwargs, {}, 0u, 0u, a) ? make_view(aSelf, 1) : nullptr; } },
                { NEOS_PY_DICT, "items", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    { std::vector<object*> a; return parse_arguments("dict.items", aArgs, aKwargs, {}, 0u, 0u, a) ? make_view(aSelf, 2) : nullptr; } },
                { NEOS_PY_DICT, "pop", dict_pop_method },
                { NEOS_PY_DICT, "popitem", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("dict.popitem", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        auto& d = *as<dict_object>(aSelf);
                        if (d.items.empty())
                            return error("KeyError", "'popitem(): dictionary is empty'");
                        auto const result = view_item(d, d.items.size() - 1u, 2);
                        dict_erase(d, d.items.size() - 1u);
                        return result;
                    } },
                { NEOS_PY_DICT, "setdefault", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("setdefault", aArgs, aKwargs, { "key", "default" }, 1u, 2u, a))
                            return nullptr;
                        auto const position = dict_find(aSelf, a[0]);
                        if (position == -2)
                            return nullptr;
                        if (position >= 0)
                        {
                            auto const value = as<dict_object>(aSelf)->items[static_cast<std::size_t>(position)].second;
                            return (retain(value), value);
                        }
                        auto const value = a[1] != nullptr ? a[1] : none();
                        if (dict_set(aSelf, a[0], value) != 0)
                            return nullptr;
                        return (retain(value), value);
                    } },
                { NEOS_PY_DICT, "update", dict_update_method },
                { NEOS_PY_DICT, "clear", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("dict.clear", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        auto& d = *as<dict_object>(aSelf);
                        auto items = std::move(d.items);
                        d.items.clear();
                        d.hashes.clear();
                        d.index.clear();
                        for (auto& i : items)
                        {
                            release(i.first);
                            release(i.second);
                        }
                        return none();
                    } },
                { NEOS_PY_DICT, "copy", [](object* aSelf, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("dict.copy", aArgs, aKwargs, {}, 0u, 0u, a))
                            return nullptr;
                        ref result{ make_dict() };
                        if (!dict_merge(result.get(), aSelf))
                            return nullptr;
                        return result.release_ownership();
                    } },
                { NEOS_PY_DICT, "fromkeys", [](object*, std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("fromkeys", aArgs, aKwargs, { "iterable", "value" }, 1u, 2u, a))
                            return nullptr;
                        std::vector<object*> keys;
                        if (!collect(a[0], keys))
                            return nullptr;
                        ref result{ make_dict() };
                        bool ok = true;
                        for (auto k : keys)
                        {
                            ok = ok && dict_set(result.get(), k, a[1] != nullptr ? a[1] : none()) == 0;
                            release(k);
                        }
                        return ok ? result.release_ownership() : nullptr;
                    } },
                { NEOS_PY_DICT, "__getitem__", [](object* aSelf, std::vector<object*> const& aArgs, object*) -> object*
                    { return aArgs.size() == 1u ? getitem(aSelf, aArgs[0]) : error("TypeError", "dict.__getitem__() takes exactly one argument (" + std::to_string(aArgs.size()) + " given)"); } },
                { NEOS_PY_DICT, "__contains__", [](object* aSelf, std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 1u)
                            return error("TypeError", "dict.__contains__() takes exactly one argument (" + std::to_string(aArgs.size()) + " given)");
                        auto const c = contains(aSelf, aArgs[0]);
                        return c < 0 ? nullptr : make_bool(c == 1);
                    } },
                { NEOS_PY_LIST, "__getitem__", [](object* aSelf, std::vector<object*> const& aArgs, object*) -> object*
                    { return aArgs.size() == 1u ? getitem(aSelf, aArgs[0]) : error("TypeError", "list.__getitem__() takes exactly one argument (" + std::to_string(aArgs.size()) + " given)"); } },
                { NEOS_PY_TUPLE, "__getitem__", [](object* aSelf, std::vector<object*> const& aArgs, object*) -> object*
                    { return aArgs.size() == 1u ? getitem(aSelf, aArgs[0]) : error("TypeError", "tuple.__getitem__() takes exactly one argument (" + std::to_string(aArgs.size()) + " given)"); } }
            };


            // --- the special methods of the builtin types (e.g. dict.__setitem__, and super().__setitem__ in a subclass of dict)

            enum protocol_operation : int
            {
                GetItem, SetItem, DelItem, Len, Iter, Contains, Repr, Str, Hash, Bool, Index, Int, Float, Lt, Le, Eq, Ne, Gt, Ge, 
                Neg, Pos, Invert, Abs, Binary, ReflectedBinary = Binary + 16, InplaceBinary = ReflectedBinary + 16, ProtocolEnd = InplaceBinary + 16
            };

            struct protocol_entry { char const* name; int operation; };

            protocol_entry const sProtocol[] =
            {
                { "__getitem__", GetItem }, { "__setitem__", SetItem }, { "__delitem__", DelItem }, { "__len__", Len }, { "__iter__", Iter }, 
                { "__contains__", Contains }, { "__repr__", Repr }, { "__str__", Str }, { "__hash__", Hash }, { "__bool__", Bool }, { "__index__", Index },
                { "__int__", Int }, { "__float__", Float }, { "__lt__", Lt }, { "__le__", Le }, { "__eq__", Eq }, { "__ne__", Ne }, { "__gt__", Gt }, { "__ge__", Ge },
                { "__neg__", Neg }, { "__pos__", Pos }, { "__invert__", Invert }, { "__abs__", Abs },
                { "__add__", Binary + NEOS_PY_ADD }, { "__sub__", Binary + NEOS_PY_SUBTRACT }, { "__mul__", Binary + NEOS_PY_MULTIPLY }, 
                { "__truediv__", Binary + NEOS_PY_TRUE_DIVIDE }, { "__floordiv__", Binary + NEOS_PY_FLOOR_DIVIDE }, { "__mod__", Binary + NEOS_PY_REMAINDER },
                { "__pow__", Binary + NEOS_PY_POWER }, { "__lshift__", Binary + NEOS_PY_LSHIFT }, { "__rshift__", Binary + NEOS_PY_RSHIFT },
                { "__and__", Binary + NEOS_PY_AND }, { "__or__", Binary + NEOS_PY_OR }, { "__xor__", Binary + NEOS_PY_XOR },
                { "__radd__", ReflectedBinary + NEOS_PY_ADD }, { "__rsub__", ReflectedBinary + NEOS_PY_SUBTRACT }, { "__rmul__", ReflectedBinary + NEOS_PY_MULTIPLY },
                { "__rtruediv__", ReflectedBinary + NEOS_PY_TRUE_DIVIDE }, { "__rfloordiv__", ReflectedBinary + NEOS_PY_FLOOR_DIVIDE }, 
                { "__rmod__", ReflectedBinary + NEOS_PY_REMAINDER }, { "__rpow__", ReflectedBinary + NEOS_PY_POWER }, { "__rand__", ReflectedBinary + NEOS_PY_AND },
                { "__ror__", ReflectedBinary + NEOS_PY_OR }, { "__rxor__", ReflectedBinary + NEOS_PY_XOR },
                { "__iadd__", InplaceBinary + NEOS_PY_ADD }, { "__imul__", InplaceBinary + NEOS_PY_MULTIPLY }, { "__ior__", InplaceBinary + NEOS_PY_OR }
            };

            // (whether a builtin kind has a special method)
            bool has_protocol(int aKind, int aOperation)
            {
                bool const number = aKind == NEOS_PY_INT || aKind == NEOS_PY_BOOL || aKind == NEOS_PY_FLOAT || aKind == NEOS_PY_COMPLEX;
                bool const integer = aKind == NEOS_PY_INT || aKind == NEOS_PY_BOOL;
                bool const container = aKind == NEOS_PY_STR || aKind == NEOS_PY_LIST || aKind == NEOS_PY_TUPLE || aKind == NEOS_PY_DICT || aKind == NEOS_PY_RANGE;
                bool const sequence = aKind == NEOS_PY_STR || aKind == NEOS_PY_LIST || aKind == NEOS_PY_TUPLE;
                switch (aOperation)
                {
                case GetItem: case Len: case Iter: case Contains: return container;
                case SetItem: case DelItem: return aKind == NEOS_PY_LIST || aKind == NEOS_PY_DICT;
                case Repr: case Str: case Eq: case Ne: case Lt: case Le: case Gt: case Ge: return aKind != NEOS_PY_OTHER;
                case Hash: return aKind != NEOS_PY_OTHER && aKind != NEOS_PY_LIST && aKind != NEOS_PY_DICT && aKind != NEOS_PY_SLICE;
                case Bool: case Float: case Neg: case Pos: case Abs: return number;
                case Int: return number && aKind != NEOS_PY_COMPLEX;
                case Index: case Invert: return integer;
                default:
                    break;
                }
                auto const op = (aOperation - Binary) % 16;
                if (aOperation >= InplaceBinary)
                    return (aKind == NEOS_PY_LIST && (op == NEOS_PY_ADD || op == NEOS_PY_MULTIPLY)) || (aKind == NEOS_PY_DICT && op == NEOS_PY_OR);
                if (op == NEOS_PY_ADD || op == NEOS_PY_MULTIPLY)
                    return number || sequence;
                if (op == NEOS_PY_REMAINDER)
                    return number || aKind == NEOS_PY_STR;
                if (op == NEOS_PY_AND || op == NEOS_PY_XOR || op == NEOS_PY_LSHIFT || op == NEOS_PY_RSHIFT)
                    return integer;
                if (op == NEOS_PY_OR)
                    return integer || aKind == NEOS_PY_DICT;
                return number;
            }

            object* call_protocol(neos_py_context*, void* aData, neos_py_object* aArgs, neos_py_object* aKwargs)
            {
                auto const& entry = *static_cast<protocol_entry const*>(aData);
                auto const& args = as<tuple_object>(aArgs)->items;
                if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                    return error("TypeError", std::string{ entry.name } + "() takes no keyword arguments");
                std::size_t const arity = entry.operation == SetItem ? 3u : entry.operation >= Binary || entry.operation == GetItem || 
                    entry.operation == DelItem || entry.operation == Contains || (entry.operation >= Lt && entry.operation <= Ge) ? 2u : 1u;
                if (args.size() != arity)
                    return error("TypeError", "expected " + std::to_string(arity - 1u) + (arity == 2u ? " argument, got " : " arguments, got ") + 
                        std::to_string(args.size() - std::min<std::size_t>(args.size(), 1u)));
                auto const self = builtin_value(args[0]);
                switch (entry.operation)
                {
                case GetItem: return getitem(self, args[1]);
                case SetItem: return setitem(self, args[1], args[2]) == 0 ? none() : nullptr;
                case DelItem: return setitem(self, args[1], nullptr) == 0 ? none() : nullptr;
                case Len: { auto const n = length(self); return n < 0 ? nullptr : make_int(n); }
                case Iter: return iter(self);
                case Contains: { auto const c = contains(self, args[1]); return c < 0 ? nullptr : make_bool(c == 1); }
                case Repr: { std::string t; return repr_of(self, t) ? make_str(t) : nullptr; }
                case Str: { std::string t; return str_of(self, t) ? make_str(t) : nullptr; }
                case Hash: { long long h = 0; return hash_of(self, h) ? make_int(h) : nullptr; }
                case Bool: { auto const t = truth(self); return t < 0 ? nullptr : make_bool(t == 1); }
                case Index: case Int:
                    {
                        if (kind_of(self) == NEOS_PY_INT)
                            return (retain(self), self);
                        if (kind_of(self) == NEOS_PY_BOOL)
                            return make_int(self == make_bool(true) ? 1 : 0);
                        double d = 0.0;
                        if (!to_double(self, d))
                            return nullptr;
                        return make_int(static_cast<long long>(d));
                    }
                case Float: { double d = 0.0; return to_double(self, d) ? make_float(d) : nullptr; }
                case Lt: case Le: case Eq: case Ne: case Gt: case Ge:
                    {
                        static int const sOperations[] = { NEOS_PY_LT, NEOS_PY_LE, NEOS_PY_EQ, NEOS_PY_NE, NEOS_PY_GT, NEOS_PY_GE };
                        return compare(sOperations[entry.operation - Lt], self, builtin_value(args[1]));
                    }
                case Neg: return unary(NEOS_PY_NEGATIVE, self);
                case Pos: return unary(NEOS_PY_POSITIVE, self);
                case Invert: return unary(NEOS_PY_INVERT, self);
                case Abs: return unary(NEOS_PY_ABSOLUTE, self);
                default:
                    break;
                }
                auto const op = (entry.operation - Binary) % 16;
                if (entry.operation >= InplaceBinary)
                {
                    ref const r{ binary(op, self, builtin_value(args[1]), true) };
                    if (r && r.get() == self)
                        return (retain(args[0]), args[0]);
                    return r ? (retain(r.get()), r.get()) : nullptr;
                }
                if (entry.operation >= ReflectedBinary)
                    return binary(op, builtin_value(args[1]), self, false);
                return binary(op, self, builtin_value(args[1]), false);
            }

            char const* kind_name(int aKind) { return aKind == NEOS_PY_LIST ? "list" : aKind == NEOS_PY_TUPLE ? "tuple" : "dict"; }

            object* call_container_method(neos_py_context*, void* aData, neos_py_object* aArgs, neos_py_object* aKwargs)
            {
                auto const& entry = *static_cast<container_method_entry const*>(aData);
                auto const& args = as<tuple_object>(aArgs)->items;
                auto const kwargs = aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty() ? aKwargs : nullptr;
                if (std::strcmp(entry.name, "fromkeys") == 0)
                    return entry.function(nullptr, args, kwargs);   // (a class method)
                if (args.empty())
                    return error("TypeError", std::string{ "unbound method " } + kind_name(entry.kind) + "." + entry.name + "() needs an argument");
                if (kind_of(args[0]) != entry.kind)
                    return error("TypeError", std::string{ "descriptor '" } + entry.name + "' for '" + kind_name(entry.kind) + "' objects doesn't apply to a '" +
                        type_name(args[0]) + "' object");
                std::vector<object*> rest{ args.begin() + 1, args.end() };
                return entry.function(args[0], rest, kwargs);
            }
        }

        object* container_method(object* aSelf, int aKind, char const* aName)
        {
            for (auto const& entry : sContainerMethods)
                if (entry.kind == aKind && std::strcmp(entry.name, aName) == 0)
                    return make_function(entry.name, call_container_method, const_cast<container_method_entry*>(&entry),
                        std::strcmp(aName, "fromkeys") == 0 ? nullptr : aSelf);
            return nullptr;
        }

        native_method container_method_function(int aKind, char const* aName)
        {
            // (by name: made once (the first of a kind's, as container_method finds it))
            static std::unordered_map<std::string_view, std::vector<container_method_entry const*>> const sFunctions = []()
            {
                std::unordered_map<std::string_view, std::vector<container_method_entry const*>> result;
                for (auto const& entry : sContainerMethods)
                    if (std::strcmp(entry.name, "fromkeys") != 0)
                        result[entry.name].push_back(&entry);
                return result;
            }();
            if (auto const f = sFunctions.find(aName); f != sFunctions.end())
                for (auto const* entry : f->second)
                    if (entry->kind == aKind)
                        return entry->function;
            return nullptr;
        }

        object* dict_iterator(object* aDict, int aWhich)
        {
            retain(aDict);
            return &(new view_iterator_object{ { { &sViewIteratorTypes[aWhich].base } }, aDict, aWhich, 0u, as<dict_object>(aDict)->items.size() })->h.base;
        }

        object* protocol_method(object* aSelf, int aKind, char const* aName)
        {
            if (aName[0] != '_' || aName[1] != '_')
                return nullptr;
            for (auto const& entry : sProtocol)
                if (std::strcmp(entry.name, aName) == 0)
                {
                    if (aKind >= 0 && !has_protocol(aKind, entry.operation))
                        return nullptr;
                    return make_function(entry.name, call_protocol, const_cast<protocol_entry*>(&entry), aSelf);
                }
            return nullptr;
        }

        bool dict_update(object* aDict, object* aOther)
        {
            return dict_merge(aDict, aOther);
        }
    }
}
