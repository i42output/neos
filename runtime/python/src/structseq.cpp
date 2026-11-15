/*
  structseq.cpp

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

  Portions of this file are derived from CPython 3.13 (Objects/structseq.c), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from C to C++ for the neos Python runtime plugin and adapted to its
  object model (see runtime/python/NOTICE).
*/

// Struct sequences (CPython's structseq: e.g. time.struct_time, os.stat_result, sys.version_info): tuples whose items
// are also named attributes (and which can have named attributes that are not items)

#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string_view>
#include <string>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        object* structseq_type_attribute(std::string_view aQualifiedName, char const* aName);

        namespace
        {
            struct structseq_object
            {
                header h;
                structseq_descriptor const* descriptor;
                std::vector<object*> values;    ///< (the items, then the other attributes)
                ~structseq_object() { for (auto v : values) release(v); }
            };

            structseq_object& as_structseq(object* aObject) { return *reinterpret_cast<structseq_object*>(aObject); }

            // (the items as a tuple: a new reference)
            object* items_of(object* aSelf)
            {
                auto const& s = as_structseq(aSelf);
                std::vector<object*> items{ s.values.begin(), s.values.begin() + static_cast<std::ptrdiff_t>(s.descriptor->visible) };
                for (auto i : items)
                    retain(i);
                return make_tuple(std::move(items));
            }

            bool is_structseq(object* aObject);
            bool has_constructor(structseq_descriptor const& aDescriptor);
            bool reduces_as_tuple(structseq_descriptor const& aDescriptor);

            // (__reduce__: (its type, (its items, {its other fields}) (CPython's structseq_reduce)), or (its type, its items) if its type's 
            // constructor takes its items (e.g. os.sched_param's))
            object* structseq_reduce(object* aSelf)
            {
                auto const& s = as_structseq(aSelf);
                ref const type{ class_of(aSelf) };
                ref const items{ items_of(aSelf) };
                if (!type || !items)
                    return nullptr;
                // (a type that pickles as a tuple (e.g. datetime.IsoCalendarDate's: (tuple, (tuple(self),)), as CPython's iso_calendar_date_reduce))
                if (reduces_as_tuple(*s.descriptor))
                {
                    ref const tupleType{ class_of(items.get()) };
                    return make_tuple({ new_reference(tupleType.get()), make_tuple({ new_reference(items.get()) }) });
                }
                if (has_constructor(*s.descriptor))
                    return make_tuple({ new_reference(type.get()), new_reference(items.get()) });
                ref const others{ make_dict() };
                for (auto f = s.descriptor->visible; f < s.descriptor->fields.size() && f < s.values.size(); ++f)
                {
                    ref const key{ make_str(s.descriptor->fields[f]) };
                    if (dict_set(others.get(), key.get(), s.values[f]) != 0)
                        return nullptr;
                }
                return make_tuple({ new_reference(type.get()), make_tuple({ new_reference(items.get()), new_reference(others.get()) }) });
            }

            // (__replace__(**fields): a copy with those fields' values (CPython's structseq_replace); not of a struct sequence with unnamed 
            // fields (fields named as later ones: e.g. os.stat_result's integer times))
            object* structseq_replace(object* aSelf, object* aArgs, object* aKwargs)
            {
                auto const& s = as_structseq(aSelf);
                if (tuple_items(aArgs).size() > 1u)
                    return error("TypeError", "__replace__() takes no positional arguments");
                auto const& fields = s.descriptor->fields;
                for (std::size_t i = 0u; i < fields.size(); ++i)
                    if (std::find(fields.begin() + static_cast<std::ptrdiff_t>(i) + 1, fields.end(), fields[i]) != fields.end())
                        return error("TypeError", "__replace__() is not supported for " + s.descriptor->qualifiedName + " because it has unnamed field(s)");
                std::vector<object*> values;
                std::vector<bool> used(aKwargs != nullptr && kind_of(aKwargs) == NEOS_PY_DICT ? as<dict_object>(aKwargs)->items.size() : 0u, false);
                for (std::size_t f = 0u; f < fields.size(); ++f)
                {
                    object* value = f < s.values.size() ? s.values[f] : none();
                    for (std::size_t k = 0u; k < used.size(); ++k)
                    {
                        auto const& [key, v] = as<dict_object>(aKwargs)->items[k];
                        if (key != nullptr && kind_of(key) == NEOS_PY_STR && as<str_object>(key)->value == fields[f])
                        {
                            value = v;
                            used[k] = true;
                        }
                    }
                    values.push_back(new_reference(value));
                }
                std::vector<object*> unexpected;
                for (std::size_t k = 0u; k < used.size(); ++k)
                    if (!used[k] && as<dict_object>(aKwargs)->items[k].first != nullptr)
                        unexpected.push_back(new_reference(as<dict_object>(aKwargs)->items[k].first));
                if (!unexpected.empty())
                {
                    for (auto v : values)
                        release(v);
                    ref const names{ make_list(std::move(unexpected)) };
                    std::string text;
                    if (!repr_of(names.get(), text))
                        return nullptr;
                    return error("TypeError", "Got unexpected field name(s): " + text);
                }
                return make_structseq(*s.descriptor, std::move(values));
            }
        }

        namespace
        {
            std::mutex& structseq_mutex() { static std::mutex sMutex; return sMutex; }
            std::deque<structseq_descriptor>& structseq_descriptors() { static std::deque<structseq_descriptor> sDescriptors; return sDescriptors; }
        }

        // (the qualified name of a struct sequence's type (a subclass of tuple))
        bool structseq_type_name(std::string_view aQualifiedName)
        {
            std::lock_guard<std::mutex> lock{ structseq_mutex() };
            for (auto const& d : structseq_descriptors())
                if (d.qualifiedName == aQualifiedName)
                    return true;
            return false;
        }

        structseq_descriptor const& structseq_type(std::string const& aQualifiedName, std::vector<std::string> const& aFields, std::size_t aVisible)
        {
            std::lock_guard<std::mutex> lock{ structseq_mutex() };
            auto& sDescriptors = structseq_descriptors();
            for (auto const& d : sDescriptors)
                if (d.qualifiedName == aQualifiedName)
                    return d;
            auto& d = sDescriptors.emplace_back();
            d.qualifiedName = aQualifiedName;
            auto const dot = aQualifiedName.rfind('.');
            d.name = dot == std::string::npos ? aQualifiedName : aQualifiedName.substr(dot + 1u);
            d.fields = aFields;
            d.visible = aVisible;
            d.type = make_type(d.qualifiedName.c_str(), d.name.c_str(), retain_object, release_object<structseq_object>);
            d.type.repr = [](neos_py_context*, object* aSelf) -> object*
            {
                auto const& s = as_structseq(aSelf);
                std::string text = s.descriptor->qualifiedName + "(";
                for (std::size_t i = 0u; i < s.descriptor->visible; ++i)
                {
                    std::string value;
                    if (!repr_of(s.values[i], value))
                        return nullptr;
                    text += (i != 0u ? ", " : "") + s.descriptor->fields[i] + "=" + value;
                }
                return make_str(text + ")");
            };
            d.type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
            {
                auto const& s = as_structseq(aSelf);
                // (the last field of the name: an item can have the name of a later attribute (e.g. os.stat_result's st_atime))
                for (std::size_t i = std::min(s.descriptor->fields.size(), s.values.size()); i-- > 0u;)
                    if (s.descriptor->fields[i] == aName)
                    {
                        retain(s.values[i]);
                        return s.values[i];
                    }
                if (auto const attribute = structseq_type_attribute(s.descriptor->qualifiedName, aName))
                    return attribute;
                if (std::strcmp(aName, "__reduce__") == 0)
                    return make_function("__reduce__", [](neos_py_context*, void*, object* aArgs, object*) -> object*
                        { return structseq_reduce(tuple_items(aArgs).at(0u)); }, nullptr, aSelf);
                // (object.__reduce_ex__: __reduce__ (which a struct sequence overrides), whatever the protocol)
                if (std::strcmp(aName, "__reduce_ex__") == 0)
                    return make_function("__reduce_ex__", [](neos_py_context*, void*, object* aArgs, object*) -> object*
                    {
                        if (tuple_items(aArgs).size() != 2u)
                            return error("TypeError", "__reduce_ex__() takes exactly one argument (" + std::to_string(tuple_items(aArgs).size() - 1u) + " given)");
                        return structseq_reduce(tuple_items(aArgs).at(0u));
                    }, nullptr, aSelf);
                if (std::strcmp(aName, "__replace__") == 0)
                    return make_function("__replace__", [](neos_py_context*, void*, object* aArgs, object* aKwargs) -> object*
                        { return structseq_replace(tuple_items(aArgs).at(0u), aArgs, aKwargs); }, nullptr, aSelf);
                // (a tuple's methods)
                ref const tuple{ items_of(aSelf) };
                return getattr(tuple.get(), aName);
            };
            d.type.getitem = [](neos_py_context*, object* aSelf, object* aKey) -> object*
            {
                ref const tuple{ items_of(aSelf) };
                return getitem(tuple.get(), aKey);
            };
            d.type.length = [](neos_py_context*, object* aSelf) -> long long { return static_cast<long long>(as_structseq(aSelf).descriptor->visible); };
            d.type.iter = [](neos_py_context*, object* aSelf) -> object*
            {
                ref const tuple{ items_of(aSelf) };
                return iter(tuple.get());
            };
            d.type.contains = [](neos_py_context*, object* aSelf, object* aItem) -> int
            {
                ref const tuple{ items_of(aSelf) };
                return contains(tuple.get(), aItem);
            };
            d.type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
            {
                ref const a{ is_structseq(aLhs) ? items_of(aLhs) : (retain(aLhs), aLhs) };
                ref const b{ is_structseq(aRhs) ? items_of(aRhs) : (retain(aRhs), aRhs) };
                if (kind_of(a.get()) != NEOS_PY_TUPLE || kind_of(b.get()) != NEOS_PY_TUPLE)
                    return not_implemented();
                return compare(aOperation, a.get(), b.get());
            };
            d.type.binary = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs, int) -> object*
            {
                ref const a{ is_structseq(aLhs) ? items_of(aLhs) : (retain(aLhs), aLhs) };
                ref const b{ is_structseq(aRhs) ? items_of(aRhs) : (retain(aRhs), aRhs) };
                return binary(aOperation, a.get(), b.get(), false);
            };
            d.type.hash = [](neos_py_context*, object* aSelf, long long* aResult) -> int
            {
                ref const tuple{ items_of(aSelf) };
                return hash_of(tuple.get(), *aResult) ? 0 : -1;
            };
            return d;
        }

        namespace
        {
            bool is_structseq(object* aObject)
            {
                auto const type = python_type(aObject);
                return type != nullptr && type->repr != nullptr && aObject->type->release == &release_object<structseq_object>;
            }
        }

        object* make_structseq(structseq_descriptor const& aDescriptor, std::vector<object*> aValues)
        {
            while (aValues.size() < aDescriptor.fields.size())
                aValues.push_back((retain(none()), none()));
            return &(new structseq_object{ { { &aDescriptor.type.base } }, &aDescriptor, std::move(aValues) })->h.base;
        }

        // (a struct sequence type's own constructor (e.g. os.sched_param's): its type called (aArgs: a tuple))
        using structseq_constructor = object* (*)(structseq_descriptor const& aDescriptor, object* aArgs, object* aKwargs);
        namespace
        {
            std::map<std::string, structseq_constructor, std::less<>>& structseq_constructors() { static std::map<std::string, structseq_constructor, std::less<>> sConstructors; return sConstructors; }
        }
        // (aItemArguments: its arguments are its items (e.g. os.sched_param(sched_priority)): its __reduce__ is (type, items))
        namespace
        {
            std::set<std::string, std::less<>>& item_argument_types() { static std::set<std::string, std::less<>> sTypes; return sTypes; }
        }
        void set_structseq_constructor(structseq_descriptor const& aDescriptor, structseq_constructor aConstructor, bool aItemArguments)
        {
            std::lock_guard<std::mutex> lock{ structseq_mutex() };
            structseq_constructors()[aDescriptor.qualifiedName] = aConstructor;
            if (aItemArguments)
                item_argument_types().insert(aDescriptor.qualifiedName);
        }
        // (a struct sequence type that cannot be called to make one (e.g. sys.flags): "cannot create ... instances")
        namespace
        {
            std::set<std::string, std::less<>>& not_instantiable_types() { static std::set<std::string, std::less<>> sTypes; return sTypes; }
        }
        void set_structseq_not_instantiable(structseq_descriptor const& aDescriptor)
        {
            std::lock_guard<std::mutex> lock{ structseq_mutex() };
            not_instantiable_types().insert(aDescriptor.qualifiedName);
        }
        // (a struct sequence type that pickles as a tuple of its items (e.g. datetime.IsoCalendarDate))
        namespace
        {
            std::set<std::string, std::less<>>& tuple_reduce_types() { static std::set<std::string, std::less<>> sTypes; return sTypes; }
        }
        void set_structseq_reduce_as_tuple(structseq_descriptor const& aDescriptor)
        {
            std::lock_guard<std::mutex> lock{ structseq_mutex() };
            tuple_reduce_types().insert(aDescriptor.qualifiedName);
        }

        namespace
        {
            bool has_constructor(structseq_descriptor const& aDescriptor)
            {
                std::lock_guard<std::mutex> lock{ structseq_mutex() };
                return item_argument_types().contains(aDescriptor.qualifiedName);
            }
            bool reduces_as_tuple(structseq_descriptor const& aDescriptor)
            {
                std::lock_guard<std::mutex> lock{ structseq_mutex() };
                return tuple_reduce_types().contains(aDescriptor.qualifiedName);
            }
        }

        object* structseq_construct(structseq_descriptor const& aDescriptor, object* aArgs, object* aKwargs);

        // (a struct sequence's type called (aArgs: a tuple): its constructor, or structseq_construct; aFound: false if aQualifiedName is 
        // not a struct sequence's type's)
        object* structseq_new(std::string_view aQualifiedName, object* aArgs, object* aKwargs, bool& aFound)
        {
            structseq_descriptor const* descriptor = nullptr;
            structseq_constructor constructor = nullptr;
            bool instantiable = true;
            {
                std::lock_guard<std::mutex> lock{ structseq_mutex() };
                for (auto const& d : structseq_descriptors())
                    if (d.qualifiedName == aQualifiedName)
                        descriptor = &d;
                if (auto const c = structseq_constructors().find(aQualifiedName); c != structseq_constructors().end())
                    constructor = c->second;
                instantiable = !not_instantiable_types().contains(aQualifiedName);
            }
            aFound = descriptor != nullptr;
            if (descriptor == nullptr)
                return nullptr;
            if (!instantiable)
                return error("TypeError", "cannot create '" + descriptor->qualifiedName + "' instances");
            if (constructor != nullptr)
                return constructor(*descriptor, aArgs, aKwargs);
            return structseq_construct(*descriptor, aArgs, aKwargs);
        }

        // (CPython's structseq_new: (sequence, dict=None): the sequence's items, then the other fields' from the dict (None if it has none))
        object* structseq_construct(structseq_descriptor const& aDescriptor, object* aArgs, object* aKwargs)
        {
            auto const* const descriptor = &aDescriptor;
            std::vector<object*> a;
            if (!parse_arguments("structseq", tuple_items(aArgs), aKwargs, { "sequence", "dict" }, 1u, 0u, a))
                return nullptr;
            ref const it{ iter(a[0]) };
            if (!it)
            {
                clear_error();
                return error("TypeError", "constructor requires a sequence");
            }
            ref const sequence{ fn_tuple_of(it.get()) };
            if (!sequence)
                return nullptr;
            auto const& items = tuple_items(sequence.get());
            auto const n = items.size();
            auto const visible = descriptor->visible;
            auto const fields = descriptor->fields.size();
            auto const given = "-sequence (" + std::to_string(n) + "-sequence given)";
            if (n < visible)
                return error("TypeError", descriptor->qualifiedName + "() takes " + (visible == fields ? "a " : "an at least ") + std::to_string(visible) + given);
            if (n > fields)
                return error("TypeError", descriptor->qualifiedName + "() takes " + (visible == fields ? "a " : "an at most ") + std::to_string(fields) + given);
            object* const dict = a.size() > 1u ? a[1] : nullptr;
            if (dict != nullptr && kind_of(dict) != NEOS_PY_DICT)
                return error("TypeError", descriptor->qualifiedName + "() takes a dict as second arg, if any");
            std::vector<object*> values;
            for (auto i : items)
                values.push_back(new_reference(i));
            // (the fields that the sequence does not give: the dict's (its keys only those fields' names), else None)
            std::size_t foundKeys = 0u;
            for (auto f = n; f < fields; ++f)
            {
                object* value = nullptr;
                if (dict != nullptr && !as<dict_object>(dict)->items.empty())
                {
                    ref const key{ make_str(descriptor->fields[f]) };
                    auto const found = dict_find(dict, key.get());
                    if (found == -2)
                    {
                        for (auto v : values)
                            release(v);
                        return nullptr;
                    }
                    if (found >= 0)
                    {
                        value = as<dict_object>(dict)->items[static_cast<std::size_t>(found)].second;
                        ++foundKeys;
                    }
                }
                values.push_back(new_reference(value != nullptr ? value : none()));
            }
            if (dict != nullptr && as<dict_object>(dict)->items.size() > foundKeys)
            {
                for (auto v : values)
                    release(v);
                return error("TypeError", descriptor->qualifiedName + "() got duplicate or unexpected field name(s)");
            }
            return make_structseq(*descriptor, std::move(values));
        }

        // (a struct sequence type's attributes (n_fields, n_sequence_fields, n_unnamed_fields, __match_args__): null (no exception) if 
        // aName is none of them or aQualifiedName no struct sequence's type)
        object* structseq_type_attribute(std::string_view aQualifiedName, char const* aName)
        {
            structseq_descriptor const* descriptor = nullptr;
            {
                std::lock_guard<std::mutex> lock{ structseq_mutex() };
                for (auto const& d : structseq_descriptors())
                    if (d.qualifiedName == aQualifiedName)
                        descriptor = &d;
            }
            if (descriptor == nullptr)
                return nullptr;
            // (an unnamed field: a visible field whose name is a later field's (e.g. os.stat_result's integer st_atime))
            auto const& fields = descriptor->fields;
            auto const unnamed = [&](std::size_t aField)
            {
                return std::find(fields.begin() + static_cast<std::ptrdiff_t>(aField) + 1, fields.end(), fields[aField]) != fields.end();
            };
            if (std::strcmp(aName, "n_fields") == 0)
                return make_int(static_cast<long long>(fields.size()));
            if (std::strcmp(aName, "n_sequence_fields") == 0)
                return make_int(static_cast<long long>(descriptor->visible));
            if (std::strcmp(aName, "n_unnamed_fields") == 0)
            {
                long long count = 0;
                for (std::size_t f = 0u; f < descriptor->visible && f < fields.size(); ++f)
                    if (unnamed(f))
                        ++count;
                return make_int(count);
            }
            if (std::strcmp(aName, "__match_args__") == 0)
            {
                std::vector<object*> names;
                for (std::size_t f = 0u; f < descriptor->visible && f < fields.size(); ++f)
                    if (!unnamed(f))
                        names.push_back(make_str(fields[f]));
                return make_tuple(std::move(names));
            }
            return nullptr;
        }

        // (a field of a struct sequence (any, visible or not): borrowed (null: none); its field set (steals the value))
        object* structseq_field(object* aObject, std::size_t aField)
        {
            if (!is_structseq(aObject) || aField >= as_structseq(aObject).values.size())
                return nullptr;
            return as_structseq(aObject).values[aField];
        }
        void structseq_set_field(object* aObject, std::size_t aField, object* aValue)
        {
            if (!is_structseq(aObject) || aField >= as_structseq(aObject).values.size())
            {
                release(aValue);
                return;
            }
            auto& v = as_structseq(aObject).values[aField];
            auto const previous = v;
            v = aValue;
            release(previous);
        }

        // (dir() of a struct sequence or of its type: CPython 3.13's names besides tuple's (its fields, n_fields, n_sequence_fields, 
        // n_unnamed_fields, __match_args__, __module__ and __replace__); false if it is neither)
        bool structseq_dir(object* aObject, std::vector<std::string>& aNames)
        {
            structseq_descriptor const* descriptor = nullptr;
            if (is_structseq(aObject))
                descriptor = as_structseq(aObject).descriptor;
            else if (is_type_object(aObject))
            {
                std::lock_guard<std::mutex> lock{ structseq_mutex() };
                for (auto const& d : structseq_descriptors())
                    if (d.qualifiedName == type_qualified_name(aObject))
                        descriptor = &d;
            }
            if (descriptor == nullptr)
                return false;
            aNames = descriptor->fields;
            for (auto name : { "n_fields", "n_sequence_fields", "n_unnamed_fields", "__match_args__", "__module__", "__replace__" })
                aNames.emplace_back(name);
            return true;
        }

        bool structseq_items(object* aObject, std::vector<object*>& aItems)
        {
            if (!is_structseq(aObject))
                return false;
            auto const& s = as_structseq(aObject);
            aItems.assign(s.values.begin(), s.values.begin() + static_cast<std::ptrdiff_t>(s.descriptor->visible));
            return true;
        }
    }
}
