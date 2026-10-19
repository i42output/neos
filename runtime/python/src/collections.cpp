/*
  collections.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/collections/__init__.py), Copyright (c) 2001-2024 Python
  Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module collections (CPython 3.13's behaviour): namedtuple, deque, defaultdict, OrderedDict, Counter and ChainMap

#include <algorithm>
#include <cstring>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            object* empty_tuple() { return make_tuple(); }

            object* call_with(object* aCallable, std::vector<object*> aArgs, object* aKwargs = nullptr)    // (steals the arguments)
            {
                ref const args{ make_tuple(std::move(aArgs)) };
                return call(aCallable, args.get(), aKwargs);
            }

            std::string class_name_of(object* aObject)
            {
                ref const cls{ class_of(aObject) };
                ref const name{ getattr(cls.get(), "__name__") };
                if (name && kind_of(name.get()) == NEOS_PY_STR)
                    return as<str_object>(name.get())->value;
                clear_error();
                return type_name(aObject);
            }

            std::string repr_text(object* aObject)
            {
                std::string text;
                if (!repr_of(aObject, text))
                    return {};
                return text;
            }

            object* dict_type() { return type_object_for("dict", "dict"); }

            // --- namedtuple

            struct namedtuple_info
            {
                std::string name;
                std::vector<std::string> fields;
                std::vector<object*> defaults;  ///< (of the last fields)
            };

            std::string quoted_list(std::vector<std::string> const& aNames)
            {
                // ('x', 'x' and 'y', 'x', 'y', and 'z')
                std::string result;
                for (std::size_t i = 0u; i < aNames.size(); ++i)
                {
                    if (i != 0u)
                        result += aNames.size() == 2u ? " and " : i + 1u == aNames.size() ? ", and " : ", ";
                    result += "'" + aNames[i] + "'";
                }
                return result;
            }

            object* namedtuple_new(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& info = *static_cast<namedtuple_info const*>(aData);
                auto const& args = as<tuple_object>(aArgs)->items;
                std::string const function = info.name + ".__new__()";
                if (args.empty())
                    return error("TypeError", function + " missing 1 required positional argument: '_cls'");
                auto const n = info.fields.size();
                if (args.size() - 1u > n)
                {
                    auto const required = n - info.defaults.size();
                    return error("TypeError", function + " takes " + (required == n ? std::to_string(n + 1u) : "from " + std::to_string(required + 1u) + " to " +
                        std::to_string(n + 1u)) + " positional arguments but " + std::to_string(args.size()) + " were given");
                }
                std::vector<object*> values(n, nullptr);
                for (std::size_t i = 1u; i < args.size(); ++i)
                    values[i - 1u] = args[i];
                if (aKwargs != nullptr)
                    for (auto const& item : as<dict_object>(aKwargs)->items)
                    {
                        auto const& name = as<str_object>(item.first)->value;
                        auto const field = std::find(info.fields.begin(), info.fields.end(), name);
                        if (field == info.fields.end())
                            return error("TypeError", function + " got an unexpected keyword argument '" + name + "'");
                        auto const index = static_cast<std::size_t>(field - info.fields.begin());
                        if (values[index] != nullptr)
                            return error("TypeError", function + " got multiple values for argument '" + name + "'");
                        values[index] = item.second;
                    }
                std::vector<std::string> missing;
                for (std::size_t i = 0u; i < n; ++i)
                    if (values[i] == nullptr)
                    {
                        auto const fromEnd = n - i;
                        if (fromEnd <= info.defaults.size())
                            values[i] = info.defaults[info.defaults.size() - fromEnd];
                        else
                            missing.push_back(info.fields[i]);
                    }
                if (!missing.empty())
                    return error("TypeError", function + " missing " + std::to_string(missing.size()) + " required positional argument" +
                        (missing.size() == 1u ? "" : "s") + ": " + quoted_list(missing));
                for (auto v : values)
                    retain(v);
                ref const tuple{ make_tuple(std::move(values)) };
                ref const newArgs{ make_tuple({ (retain(tuple.get()), tuple.get()) }) };
                return new_instance(args[0], newArgs.get(), nullptr);
            }

            object* namedtuple_field(neos_py_context*, void* aData, object* aArgs, object*)
            {
                auto const& args = as<tuple_object>(aArgs)->items;
                auto const index = reinterpret_cast<std::uintptr_t>(aData);
                auto const value = builtin_value(args.at(0));
                if (kind_of(value) != NEOS_PY_TUPLE || index >= as<tuple_object>(value)->items.size())
                    return error("TypeError", "descriptor requires a tuple");
                auto const item = as<tuple_object>(value)->items[index];
                return (retain(item), item);
            }

            object* namedtuple_make(neos_py_context*, void* aData, object* aArgs, object*)
            {
                auto const& info = *static_cast<namedtuple_info const*>(aData);
                auto const& args = as<tuple_object>(aArgs)->items;
                if (args.size() != 2u)
                    return error("TypeError", info.name + "._make() takes 2 positional arguments but " + std::to_string(args.size()) + " were given");
                std::vector<object*> items;
                if (!collect(args[1], items))
                    return nullptr;
                auto const count = items.size();
                ref const tuple{ make_tuple(std::move(items)) };
                if (count != info.fields.size())
                    return error("TypeError", "Expected " + std::to_string(info.fields.size()) + " arguments, got " + std::to_string(count));
                ref const newArgs{ make_tuple({ (retain(tuple.get()), tuple.get()) }) };
                return new_instance(args[0], newArgs.get(), nullptr);
            }

            object* namedtuple_replace(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& info = *static_cast<namedtuple_info const*>(aData);
                auto const& args = as<tuple_object>(aArgs)->items;
                if (args.size() != 1u)
                    return error("TypeError", info.name + "._replace() takes 1 positional argument but " + std::to_string(args.size()) + " were given");
                auto const value = builtin_value(args[0]);
                std::vector<object*> items = as<tuple_object>(value)->items;
                std::vector<std::string> unexpected;
                if (aKwargs != nullptr)
                    for (auto const& item : as<dict_object>(aKwargs)->items)
                    {
                        auto const& name = as<str_object>(item.first)->value;
                        auto const field = std::find(info.fields.begin(), info.fields.end(), name);
                        if (field == info.fields.end())
                            unexpected.push_back(name);
                        else
                            items[static_cast<std::size_t>(field - info.fields.begin())] = item.second;
                    }
                if (!unexpected.empty())
                {
                    std::string list = "[";
                    for (std::size_t i = 0u; i < unexpected.size(); ++i)
                        list += (i != 0u ? ", '" : "'") + unexpected[i] + "'";
                    return error("TypeError", "Got unexpected field names: " + list + "]");
                }
                for (auto i : items)
                    retain(i);
                ref const tuple{ make_tuple(std::move(items)) };
                ref const cls{ class_of(args[0]) };
                ref const newArgs{ make_tuple({ (retain(tuple.get()), tuple.get()) }) };
                return new_instance(cls.get(), newArgs.get(), nullptr);
            }

            object* namedtuple_asdict(neos_py_context*, void* aData, object* aArgs, object*)
            {
                auto const& info = *static_cast<namedtuple_info const*>(aData);
                auto const value = builtin_value(as<tuple_object>(aArgs)->items.at(0));
                ref result{ make_dict() };
                for (std::size_t i = 0u; i < info.fields.size(); ++i)
                {
                    ref const key{ make_str(info.fields[i]) };
                    dict_set(result.get(), key.get(), as<tuple_object>(value)->items[i]);
                }
                return result.release_ownership();
            }

            object* namedtuple_repr(neos_py_context*, void* aData, object* aArgs, object*)
            {
                auto const& info = *static_cast<namedtuple_info const*>(aData);
                auto const self = as<tuple_object>(aArgs)->items.at(0);
                auto const value = builtin_value(self);
                std::string text = class_name_of(self) + "(";
                for (std::size_t i = 0u; i < info.fields.size(); ++i)
                {
                    std::string item;
                    if (!repr_of(as<tuple_object>(value)->items[i], item))
                        return nullptr;
                    text += (i != 0u ? ", " : "") + info.fields[i] + "=" + item;
                }
                return make_str(text + ")");
            }

            object* namedtuple_getnewargs(neos_py_context*, void*, object* aArgs, object*)
            {
                auto const value = builtin_value(as<tuple_object>(aArgs)->items.at(0));
                auto items = as<tuple_object>(value)->items;
                for (auto i : items)
                    retain(i);
                return make_tuple(std::move(items));
            }

            bool is_keyword(std::string const& aName)
            {
                static std::set<std::string> const sKeywords = { "False", "None", "True", "and", "as", "assert", "async", "await", "break", "class", "continue",
                    "def", "del", "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda", "nonlocal", "not", "or",
                    "pass", "raise", "return", "try", "while", "with", "yield" };
                return sKeywords.contains(aName);
            }

            bool is_identifier(std::string const& aName)
            {
                ref const text{ make_str(aName) };
                ref const result{ call_method_named(text.get(), "isidentifier", {}) };
                return result && truth(result.get()) == 1;
            }

            object* b_namedtuple(std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (aArgs.size() > 2u)
                    return error("TypeError", "namedtuple() takes 2 positional arguments but " + std::to_string(aArgs.size()) + " were given");
                if (!parse_arguments("namedtuple", aArgs, aKwargs, { "typename", "field_names", "rename", "defaults", "module" }, 2u, 0u, a))
                    return nullptr;
                std::string typeName;
                if (!str_of(a[0], typeName))
                    return nullptr;
                std::vector<std::string> fields;
                if (kind_of(a[1]) == NEOS_PY_STR)
                {
                    std::string names = as<str_object>(a[1])->value;
                    std::replace(names.begin(), names.end(), ',', ' ');
                    ref const text{ make_str(names) };
                    ref const parts{ call_method_named(text.get(), "split", {}) };
                    if (!parts)
                        return nullptr;
                    for (auto p : as<list_object>(parts.get())->items)
                        fields.push_back(as<str_object>(p)->value);
                }
                else
                {
                    std::vector<object*> items;
                    if (!collect(a[1], items))
                        return nullptr;
                    for (auto i : items)
                    {
                        std::string name;
                        bool const ok = str_of(i, name);
                        release(i);
                        if (!ok)
                            return nullptr;
                        fields.push_back(name);
                    }
                }
                bool const rename = a[2] != nullptr && truth(a[2]) == 1;
                if (rename)
                {
                    std::set<std::string> seen;
                    for (std::size_t i = 0u; i < fields.size(); ++i)
                    {
                        auto const& name = fields[i];
                        if (!is_identifier(name) || is_keyword(name) || name.starts_with("_") || seen.contains(name))
                            fields[i] = "_" + std::to_string(i);
                        seen.insert(fields[i]);
                    }
                }
                std::vector<std::string> all{ typeName };
                all.insert(all.end(), fields.begin(), fields.end());
                for (auto const& name : all)
                {
                    if (!is_identifier(name))
                        return error("ValueError", "Type names and field names must be valid identifiers: '" + name + "'");
                    if (is_keyword(name))
                        return error("ValueError", "Type names and field names cannot be a keyword: '" + name + "'");
                }
                std::set<std::string> seen;
                for (auto const& name : fields)
                {
                    if (name.starts_with("_") && !rename)
                        return error("ValueError", "Field names cannot start with an underscore: '" + name + "'");
                    if (seen.contains(name))
                        return error("ValueError", "Encountered duplicate field name: '" + name + "'");
                    seen.insert(name);
                }
                auto* const info = new namedtuple_info{ typeName, fields, {} };   // (kept for the class's lifetime: the program's)
                ref fieldDefaults{ make_dict() };
                if (a[3] != nullptr && kind_of(a[3]) != NEOS_PY_NONE)
                {
                    if (!collect(a[3], info->defaults))
                        return nullptr;
                    if (info->defaults.size() > fields.size())
                        return error("TypeError", "Got more default values than field names");
                    for (std::size_t i = 0u; i < info->defaults.size(); ++i)
                    {
                        ref const key{ make_str(fields[fields.size() - info->defaults.size() + i]) };
                        dict_set(fieldDefaults.get(), key.get(), info->defaults[i]);
                    }
                }
                std::vector<std::pair<std::string, object*>> ns;
                {
                    ref const function{ make_function("__new__", namedtuple_new, info) };
                    ns.emplace_back("__new__", b_staticmethod({ function.get() }, nullptr));
                }
                for (std::size_t i = 0u; i < fields.size(); ++i)
                {
                    ref const getter{ make_function(fields[i], namedtuple_field, reinterpret_cast<void*>(static_cast<std::uintptr_t>(i))) };
                    // (a _tuplegetter: read-only)
                    ref const setter{ make_function(fields[i], [](neos_py_context*, void*, object*, object*) -> object* 
                        { return error("AttributeError", "can't set attribute"); }, nullptr) };
                    ns.emplace_back(fields[i], b_property({ getter.get(), setter.get() }, nullptr));
                }
                {
                    ref const function{ make_function("_make", namedtuple_make, info) };
                    ns.emplace_back("_make", b_classmethod({ function.get() }, nullptr));
                }
                ns.emplace_back("_replace", make_function("_replace", namedtuple_replace, info));
                ns.emplace_back("_asdict", make_function("_asdict", namedtuple_asdict, info));
                ns.emplace_back("__repr__", make_function("__repr__", namedtuple_repr, info));
                ns.emplace_back("__getnewargs__", make_function("__getnewargs__", namedtuple_getnewargs, info));
                std::vector<object*> fieldNames;
                for (auto const& f : fields)
                    fieldNames.push_back(make_str(f));
                ns.emplace_back("_fields", make_tuple(fieldNames));
                for (auto f : fieldNames)
                    retain(f);
                ns.emplace_back("__match_args__", make_tuple(std::move(fieldNames)));
                ns.emplace_back("_field_defaults", fieldDefaults.release_ownership());
                ns.emplace_back("__slots__", make_tuple());
                std::string doc = typeName + "(";
                for (std::size_t i = 0u; i < fields.size(); ++i)
                    doc += (i != 0u ? ", " : "") + fields[i];
                ns.emplace_back("__doc__", make_str(doc + ")"));
                std::string module = "__main__";
                if (a[4] != nullptr && kind_of(a[4]) == NEOS_PY_STR)
                    module = as<str_object>(a[4])->value;
                ref const tupleType{ type_object_for("tuple", "tuple") };
                auto const cls = make_class(module, typeName, { tupleType.get() }, ns);
                for (auto& n : ns)
                    release(n.second);
                return cls;
            }

            // --- defaultdict

            builtin_entry const sDefaultDictMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        auto const self = aArgs.at(0);
                        object* factory = aArgs.size() > 1u ? aArgs[1] : none();
                        if (kind_of(factory) != NEOS_PY_NONE)
                        {
                            ref const callable{ call_with(fn_builtin("callable"), { (retain(factory), factory) }) };
                            if (!callable || truth(callable.get()) != 1)
                                return error("TypeError", "first argument must be callable or None");
                        }
                        if (setattr(self, "default_factory", factory) < 0)
                            return nullptr;
                        if (aArgs.size() > 3u)
                            return error("TypeError", "dict expected at most 1 argument, got " + std::to_string(aArgs.size() - 2u));
                        if (aArgs.size() == 3u && !dict_update(builtin_value(self), aArgs[2]))
                            return nullptr;
                        if (aKwargs != nullptr && !dict_update(builtin_value(self), aKwargs))
                            return nullptr;
                        return none();
                    } },
                { "__missing__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return error("TypeError", "__missing__() takes exactly one argument (" + std::to_string(aArgs.size() - 1u) + " given)");
                        ref const factory{ getattr(aArgs[0], "default_factory") };
                        if (!factory)
                            return nullptr;
                        if (kind_of(factory.get()) == NEOS_PY_NONE)
                        {
                            raise_key_error(aArgs[1]);
                            return nullptr;
                        }
                        ref value{ call_with(factory.get(), {}) };
                        if (!value || setitem(aArgs[0], aArgs[1], value.get()) < 0)
                            return nullptr;
                        return value.release_ownership();
                    } },
                { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const factory{ getattr(aArgs.at(0), "default_factory") };
                        if (!factory)
                            return nullptr;
                        std::string factoryText, dictText;
                        if (!repr_of(factory.get(), factoryText) || !repr_of(builtin_value(aArgs[0]), dictText))
                            return nullptr;
                        return make_str(class_name_of(aArgs[0]) + "(" + factoryText + ", " + dictText + ")");
                    } },
                { "copy", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const factory{ getattr(aArgs.at(0), "default_factory") };
                        ref const cls{ class_of(aArgs[0]) };
                        if (!factory)
                            return nullptr;
                        return call_with(cls.get(), { (retain(factory.get()), factory.get()), (retain(builtin_value(aArgs[0])), builtin_value(aArgs[0])) });
                    } }
            };

            // --- OrderedDict

            builtin_entry const sOrderedDictMethods[] =
            {
                { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const value = builtin_value(aArgs.at(0));
                        if (as<dict_object>(value)->items.empty())
                            return make_str(class_name_of(aArgs[0]) + "()");
                        std::string text;
                        if (!repr_of(value, text))
                            return nullptr;
                        return make_str(class_name_of(aArgs[0]) + "(" + text + ")");
                    } },
                { "move_to_end", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::vector<object*> const rest{ aArgs.begin() + 1, aArgs.end() };
                        if (!parse_arguments("move_to_end", rest, aKwargs, { "key", "last" }, 1u, 0u, a))
                            return nullptr;
                        auto const value = builtin_value(aArgs[0]);
                        auto const position = dict_find(value, a[0]);
                        if (position == -2)
                            return nullptr;
                        if (position < 0)
                        {
                            raise_key_error(a[0]);
                            return nullptr;
                        }
                        bool const last = a[1] == nullptr || truth(a[1]) == 1;
                        auto& d = *as<dict_object>(value);
                        auto const item = d.items[static_cast<std::size_t>(position)];
                        ref const key{ (retain(item.first), item.first) };
                        ref const v{ (retain(item.second), item.second) };
                        dict_erase(d, static_cast<std::size_t>(position));
                        if (last)
                            dict_set(value, key.get(), v.get());
                        else
                        {
                            // (to the front: the others after it)
                            auto items = std::move(d.items);
                            d.items.clear();
                            d.hashes.clear();
                            d.index.clear();
                            dict_set(value, key.get(), v.get());
                            for (auto& i : items)
                            {
                                dict_set(value, i.first, i.second);
                                release(i.first);
                                release(i.second);
                            }
                        }
                        return none();
                    } },
                { "popitem", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::vector<object*> const rest{ aArgs.begin() + 1, aArgs.end() };
                        if (!parse_arguments("popitem", rest, aKwargs, { "last" }, 0u, 0u, a))
                            return nullptr;
                        auto& d = *as<dict_object>(builtin_value(aArgs[0]));
                        if (d.items.empty())
                            return error("KeyError", "'dictionary is empty'");
                        bool const last = a[0] == nullptr || truth(a[0]) == 1;
                        auto const index = last ? d.items.size() - 1u : 0u;
                        auto const item = d.items[index];
                        retain(item.first);
                        retain(item.second);
                        dict_erase(d, index);
                        return make_tuple({ item.first, item.second });
                    } },
                { "__eq__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return error("TypeError", "expected 1 argument");
                        auto const lhs = builtin_value(aArgs[0]);
                        auto const rhs = builtin_value(aArgs[1]);
                        if (kind_of(rhs) != NEOS_PY_DICT)
                            return (retain(not_implemented()), not_implemented());
                        ref const equal{ compare(NEOS_PY_EQ, lhs, rhs) };
                        if (!equal || truth(equal.get()) != 1)
                            return equal ? make_bool(false) : nullptr;
                        // (two OrderedDicts: also their order)
                        ref const otherClass{ class_of(aArgs[1]) };
                        ref const mro{ getattr(otherClass.get(), "__mro__") };
                        clear_error();
                        bool ordered = false;
                        if (mro && kind_of(mro.get()) == NEOS_PY_TUPLE)
                            for (auto c : as<tuple_object>(mro.get())->items)
                            {
                                ref const name{ getattr(c, "__name__") };
                                if (name && kind_of(name.get()) == NEOS_PY_STR && as<str_object>(name.get())->value == "OrderedDict")
                                    ordered = true;
                            }
                        clear_error();
                        if (!ordered)
                            return make_bool(true);
                        auto const& l = as<dict_object>(lhs)->items;
                        auto const& r = as<dict_object>(rhs)->items;
                        for (std::size_t i = 0u; i < l.size(); ++i)
                        {
                            auto const e = detail::equal(l[i].first, r[i].first);
                            if (e < 0)
                                return nullptr;
                            if (e == 0)
                                return make_bool(false);
                        }
                        return make_bool(true);
                    } },
                { "copy", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const cls{ class_of(aArgs.at(0)) };
                        return call_with(cls.get(), { (retain(builtin_value(aArgs[0])), builtin_value(aArgs[0])) });
                    } },
                { "__reversed__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const& d = *as<dict_object>(builtin_value(aArgs.at(0)));
                        std::vector<object*> keys;
                        for (auto i = d.items.rbegin(); i != d.items.rend(); ++i)
                            keys.push_back((retain(i->first), i->first));
                        ref const list{ make_list(std::move(keys)) };
                        return iter(list.get());
                    } }
            };

            // --- Counter

            object* counter_class();

            bool is_counter(object* aObject)
            {
                auto const cls = user_class_of(aObject);
                if (cls == nullptr)
                    return false;
                ref const counter{ counter_class() };
                ref const result{ b_issubclass({ cls, counter.get() }, nullptr) };
                return result && truth(result.get()) == 1;
            }

            object* new_counter() { ref const cls{ counter_class() }; return call_with(cls.get(), {}); }

            // (the count of a key: 0 if absent)
            object* count_of(object* aCounter, object* aKey)
            {
                auto const position = dict_find(builtin_value(aCounter), aKey);
                if (position == -2)
                    return nullptr;
                if (position < 0)
                    return make_int(0);
                auto const value = as<dict_object>(builtin_value(aCounter))->items[static_cast<std::size_t>(position)].second;
                return (retain(value), value);
            }

            int sign_of(object* aValue)
            {
                ref const zero{ make_int(0) };
                ref const gt{ compare(NEOS_PY_GT, aValue, zero.get()) };
                if (!gt)
                    return -2;
                if (truth(gt.get()) == 1)
                    return 1;
                ref const lt{ compare(NEOS_PY_LT, aValue, zero.get()) };
                if (!lt)
                    return -2;
                return truth(lt.get()) == 1 ? -1 : 0;
            }

            // (Counter.update and Counter.subtract: aSign 1 or -1)
            bool counter_add(object* aSelf, object* aSource, int aSign)
            {
                auto const self = builtin_value(aSelf);
                auto const add = [&](object* aKey, object* aCount) -> bool
                {
                    ref const current{ count_of(aSelf, aKey) };
                    if (!current)
                        return false;
                    ref const sum{ binary(aSign > 0 ? NEOS_PY_ADD : NEOS_PY_SUBTRACT, current.get(), aCount, false) };
                    return sum && dict_set(self, aKey, sum.get()) == 0;
                };
                auto const source = builtin_value(aSource);
                if (kind_of(source) == NEOS_PY_DICT)
                {
                    auto const items = as<dict_object>(source)->items;
                    for (auto const& item : items)
                        if (!add(item.first, item.second))
                            return false;
                    return true;
                }
                ref const keys{ getattr(aSource, "keys") };
                if (keys)
                {
                    ref const keyList{ call_with(keys.get(), {}) };
                    std::vector<object*> items;
                    if (!keyList || !collect(keyList.get(), items))
                        return false;
                    std::vector<ref> owned;
                    for (auto i : items)
                        owned.emplace_back(i);
                    for (auto k : items)
                    {
                        ref const value{ getitem(aSource, k) };
                        if (!value || !add(k, value.get()))
                            return false;
                    }
                    return true;
                }
                clear_error();
                std::vector<object*> items;
                if (!collect(aSource, items))
                    return false;
                std::vector<ref> owned;
                for (auto i : items)
                    owned.emplace_back(i);
                ref const one{ make_int(1) };
                for (auto i : items)
                    if (!add(i, one.get()))
                        return false;
                return true;
            }

            object* counter_update(std::vector<object*> const& aArgs, object* aKwargs, int aSign, char const* aName)
            {
                if (aArgs.size() > 2u)
                    return error("TypeError", std::string{ "expected at most 1 argument, got " } + std::to_string(aArgs.size() - 1u));
                if (aArgs.size() == 2u && kind_of(aArgs[1]) != NEOS_PY_NONE && !counter_add(aArgs[0], aArgs[1], aSign))
                    return nullptr;
                if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty() && !counter_add(aArgs[0], aKwargs, aSign))
                    return nullptr;
                (void)aName;
                return none();
            }

            // (the items in decreasing order of count (stable))
            bool most_common(object* aSelf, std::vector<std::pair<object*, object*>>& aResult)
            {
                aResult = as<dict_object>(builtin_value(aSelf))->items;
                bool failure = false;
                std::stable_sort(aResult.begin(), aResult.end(), [&](auto const& a, auto const& b)
                {
                    if (failure)
                        return false;
                    ref const gt{ compare(NEOS_PY_GT, a.second, b.second) };
                    if (!gt)
                    {
                        failure = true;
                        return false;
                    }
                    return truth(gt.get()) == 1;
                });
                return !failure;
            }

            // (Counter's binary operations: aOperation 0: +, 1: -, 2: | (max), 3: & (min))
            object* counter_binary(object* aLhs, object* aRhs, int aOperation)
            {
                if (!is_counter(aRhs))
                    return (retain(not_implemented()), not_implemented());
                ref result{ new_counter() };
                if (!result)
                    return nullptr;
                auto const target = builtin_value(result.get());
                auto const lhsItems = as<dict_object>(builtin_value(aLhs))->items;
                for (auto const& item : lhsItems)
                {
                    ref const other{ count_of(aRhs, item.first) };
                    if (!other)
                        return nullptr;
                    ref value;
                    switch (aOperation)
                    {
                    case 0: value = ref{ binary(NEOS_PY_ADD, item.second, other.get(), false) }; break;
                    case 1: value = ref{ binary(NEOS_PY_SUBTRACT, item.second, other.get(), false) }; break;
                    default:
                        {
                            ref const lt{ compare(NEOS_PY_LT, item.second, other.get()) };
                            if (!lt)
                                return nullptr;
                            bool const less = truth(lt.get()) == 1;
                            value = ref::borrowed(aOperation == 2 ? (less ? other.get() : item.second) : (less ? item.second : other.get()));
                        }
                    }
                    if (!value)
                        return nullptr;
                    auto const s = sign_of(value.get());
                    if (s == -2)
                        return nullptr;
                    if (s > 0)
                        dict_set(target, item.first, value.get());
                }
                if (aOperation != 3)
                {
                    auto const rhsItems = as<dict_object>(builtin_value(aRhs))->items;
                    for (auto const& item : rhsItems)
                    {
                        auto const c = contains(builtin_value(aLhs), item.first);
                        if (c < 0)
                            return nullptr;
                        if (c == 1)
                            continue;
                        auto const s = sign_of(item.second);
                        if (s == -2)
                            return nullptr;
                        if (aOperation == 1)
                        {
                            if (s < 0)
                            {
                                ref const zero{ make_int(0) };
                                ref const negated{ binary(NEOS_PY_SUBTRACT, zero.get(), item.second, false) };
                                if (!negated)
                                    return nullptr;
                                dict_set(target, item.first, negated.get());
                            }
                        }
                        else if (s > 0)
                            dict_set(target, item.first, item.second);
                    }
                }
                return result.release_ownership();
            }

            // (+counter and -counter)
            object* counter_unary(object* aSelf, bool aNegative)
            {
                ref result{ new_counter() };
                if (!result)
                    return nullptr;
                auto const items = as<dict_object>(builtin_value(aSelf))->items;
                for (auto const& item : items)
                {
                    auto const s = sign_of(item.second);
                    if (s == -2)
                        return nullptr;
                    if (!aNegative && s > 0)
                        dict_set(builtin_value(result.get()), item.first, item.second);
                    else if (aNegative && s < 0)
                    {
                        ref const zero{ make_int(0) };
                        ref const negated{ binary(NEOS_PY_SUBTRACT, zero.get(), item.second, false) };
                        if (!negated)
                            return nullptr;
                        dict_set(builtin_value(result.get()), item.first, negated.get());
                    }
                }
                return result.release_ownership();
            }

            // (in place: the result's items replace the counter's)
            object* counter_inplace(object* aSelf, object* aOther, int aOperation)
            {
                ref const result{ counter_binary(aSelf, aOther, aOperation) };
                if (!result || kind_of(result.get()) == NEOS_PY_NOT_IMPLEMENTED)
                    return result ? (retain(result.get()), result.get()) : nullptr;
                auto& d = *as<dict_object>(builtin_value(aSelf));
                auto items = std::move(d.items);
                d.items.clear();
                d.hashes.clear();
                d.index.clear();
                for (auto& i : items)
                {
                    release(i.first);
                    release(i.second);
                }
                for (auto const& item : as<dict_object>(builtin_value(result.get()))->items)
                    dict_set(builtin_value(aSelf), item.first, item.second);
                return (retain(aSelf), aSelf);
            }

            // (Counter comparisons: missing counts are zero; aOperation: ==, <=, >=)
            object* counter_compare(object* aLhs, object* aRhs, int aOperation, bool aStrict)
            {
                if (!is_counter(aRhs))
                    return (retain(not_implemented()), not_implemented());
                for (auto const& pair : { std::pair{ aLhs, aRhs }, std::pair{ aRhs, aLhs } })
                {
                    auto const items = as<dict_object>(builtin_value(pair.first))->items;
                    for (auto const& item : items)
                    {
                        ref const mine{ count_of(aLhs, item.first) };
                        ref const theirs{ count_of(aRhs, item.first) };
                        if (!mine || !theirs)
                            return nullptr;
                        ref const r{ compare(aOperation, mine.get(), theirs.get()) };
                        if (!r)
                            return nullptr;
                        if (truth(r.get()) != 1)
                            return make_bool(false);
                    }
                }
                if (aStrict)
                {
                    ref const equal{ counter_compare(aLhs, aRhs, NEOS_PY_EQ, false) };
                    if (!equal)
                        return nullptr;
                    return make_bool(truth(equal.get()) != 1);
                }
                return make_bool(true);
            }

            builtin_entry const sCounterMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return counter_update(aArgs, aKwargs, 1, "__init__"); } },
                { "update", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return counter_update(aArgs, aKwargs, 1, "update"); } },
                { "subtract", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return counter_update(aArgs, aKwargs, -1, "subtract"); } },
                { "__missing__", [](std::vector<object*> const& aArgs, object*) -> object* { (void)aArgs; return make_int(0); } },
                { "__delitem__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const value = builtin_value(aArgs.at(0));
                        auto const position = dict_find(value, aArgs.at(1));
                        if (position == -2)
                            return nullptr;
                        if (position >= 0)
                            dict_erase(*as<dict_object>(value), static_cast<std::size_t>(position));
                        return none();
                    } },
                { "total", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref total{ make_int(0) };
                        for (auto const& item : as<dict_object>(builtin_value(aArgs.at(0)))->items)
                        {
                            total = ref{ binary(NEOS_PY_ADD, total.get(), item.second, false) };
                            if (!total)
                                return nullptr;
                        }
                        return total.release_ownership();
                    } },
                { "most_common", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        std::vector<object*> const rest{ aArgs.begin() + 1, aArgs.end() };
                        if (!parse_arguments("most_common", rest, aKwargs, { "n" }, 0u, 0u, a))
                            return nullptr;
                        std::vector<std::pair<object*, object*>> items;
                        if (!most_common(aArgs[0], items))
                            return nullptr;
                        if (a[0] != nullptr && kind_of(a[0]) != NEOS_PY_NONE)
                        {
                            long long n = 0;
                            if (!to_index(a[0], n))
                                return nullptr;
                            items.resize(static_cast<std::size_t>(std::clamp<long long>(n, 0, static_cast<long long>(items.size()))));
                        }
                        std::vector<object*> result;
                        for (auto const& i : items)
                        {
                            retain(i.first);
                            retain(i.second);
                            result.push_back(make_tuple({ i.first, i.second }));
                        }
                        return make_list(std::move(result));
                    } },
                { "elements", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        std::vector<object*> result;
                        for (auto const& item : as<dict_object>(builtin_value(aArgs.at(0)))->items)
                        {
                            long long n = 0;
                            if (!to_index(item.second, n))
                            {
                                for (auto r : result)
                                    release(r);
                                return nullptr;
                            }
                            for (long long i = 0; i < n; ++i)
                                result.push_back((retain(item.first), item.first));
                        }
                        ref const list{ make_list(std::move(result)) };
                        return iter(list.get());
                    } },
                { "copy", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const cls{ class_of(aArgs.at(0)) };
                        return call_with(cls.get(), { (retain(aArgs[0]), aArgs[0]) });
                    } },
                { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const self = aArgs.at(0);
                        auto const name = class_name_of(self);
                        if (as<dict_object>(builtin_value(self))->items.empty())
                            return make_str(name + "()");
                        std::vector<std::pair<object*, object*>> items;
                        if (!most_common(self, items))
                        {
                            clear_error();
                            items = as<dict_object>(builtin_value(self))->items;
                        }
                        std::string text = name + "({";
                        for (std::size_t i = 0u; i < items.size(); ++i)
                        {
                            std::string k, v;
                            if (!repr_of(items[i].first, k) || !repr_of(items[i].second, v))
                                return nullptr;
                            text += (i != 0u ? ", " : "") + k + ": " + v;
                        }
                        return make_str(text + "})");
                    } },
                { "__add__", [](std::vector<object*> const& aArgs, object*) -> object* { return counter_binary(aArgs.at(0), aArgs.at(1), 0); } },
                { "__sub__", [](std::vector<object*> const& aArgs, object*) -> object* { return counter_binary(aArgs.at(0), aArgs.at(1), 1); } },
                { "__or__", [](std::vector<object*> const& aArgs, object*) -> object* { return counter_binary(aArgs.at(0), aArgs.at(1), 2); } },
                { "__and__", [](std::vector<object*> const& aArgs, object*) -> object* { return counter_binary(aArgs.at(0), aArgs.at(1), 3); } },
                { "__iadd__", [](std::vector<object*> const& aArgs, object*) -> object* { return counter_inplace(aArgs.at(0), aArgs.at(1), 0); } },
                { "__isub__", [](std::vector<object*> const& aArgs, object*) -> object* { return counter_inplace(aArgs.at(0), aArgs.at(1), 1); } },
                { "__ior__", [](std::vector<object*> const& aArgs, object*) -> object* { return counter_inplace(aArgs.at(0), aArgs.at(1), 2); } },
                { "__iand__", [](std::vector<object*> const& aArgs, object*) -> object* { return counter_inplace(aArgs.at(0), aArgs.at(1), 3); } },
                { "__pos__", [](std::vector<object*> const& aArgs, object*) -> object* { return counter_unary(aArgs.at(0), false); } },
                { "__neg__", [](std::vector<object*> const& aArgs, object*) -> object* { return counter_unary(aArgs.at(0), true); } },
                { "__eq__", [](std::vector<object*> const& aArgs, object*) -> object* { return counter_compare(aArgs.at(0), aArgs.at(1), NEOS_PY_EQ, false); } },
                { "__ne__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const equal{ counter_compare(aArgs.at(0), aArgs.at(1), NEOS_PY_EQ, false) };
                        if (!equal || kind_of(equal.get()) == NEOS_PY_NOT_IMPLEMENTED)
                            return equal ? (retain(equal.get()), equal.get()) : nullptr;
                        return make_bool(truth(equal.get()) != 1);
                    } },
                { "__le__", [](std::vector<object*> const& aArgs, object*) -> object* { return counter_compare(aArgs.at(0), aArgs.at(1), NEOS_PY_LE, false); } },
                { "__lt__", [](std::vector<object*> const& aArgs, object*) -> object* { return counter_compare(aArgs.at(0), aArgs.at(1), NEOS_PY_LE, true); } },
                { "__ge__", [](std::vector<object*> const& aArgs, object*) -> object* { return counter_compare(aArgs.at(0), aArgs.at(1), NEOS_PY_GE, false); } },
                { "__gt__", [](std::vector<object*> const& aArgs, object*) -> object* { return counter_compare(aArgs.at(0), aArgs.at(1), NEOS_PY_GE, true); } }
            };

            object* sCounterClass = nullptr;

            object* counter_class()
            {
                if (sCounterClass == nullptr)
                {
                    std::vector<std::pair<std::string, object*>> ns;
                    for (auto const& m : sCounterMethods)
                        ns.emplace_back(m.name, make_builtin(m));
                    static builtin_entry const sFromKeys{ "fromkeys", [](std::vector<object*> const&, object*) -> object*
                        { return error("NotImplementedError", "Counter.fromkeys() is undefined.  Use Counter(iterable) instead."); } };
                    ref const fromkeys{ make_builtin(sFromKeys) };
                    ns.emplace_back("fromkeys", b_classmethod({ fromkeys.get() }, nullptr));
                    ns.emplace_back("__hash__", (retain(none()), none()));
                    ref const base{ dict_type() };
                    sCounterClass = make_class("collections", "Counter", { base.get() }, ns);
                    for (auto& n : ns)
                        release(n.second);
                }
                retain(sCounterClass);
                return sCounterClass;
            }

            // --- deque

            struct deque_object
            {
                header h;
                std::deque<object*> items;
                long long maxlen = -1;          ///< (-1: unbounded)
                std::size_t state = 0u;         ///< (changes with each mutation (its iterators check it))
                ~deque_object() { for (auto i : items) release(i); }
            };

            struct deque_iterator_object
            {
                header h;
                object* deque;
                std::size_t index = 0u;
                std::size_t state;
                bool reversed;
                ~deque_iterator_object() { release(deque); }
            };

            neos_py_type make_deque_type();
            neos_py_type make_deque_iterator_type(bool aReversed);
            neos_py_type sDequeType = make_deque_type();
            neos_py_type sDequeIteratorType = make_deque_iterator_type(false);
            neos_py_type sDequeReverseIteratorType = make_deque_iterator_type(true);

            deque_object& as_deque(object* aObject) { return *reinterpret_cast<deque_object*>(aObject); }
            bool is_deque(object* aObject) { return aObject != nullptr && aObject->type == &sDequeType.base; }

            void deque_trim(deque_object& d, bool aFront)
            {
                while (d.maxlen >= 0 && static_cast<long long>(d.items.size()) > d.maxlen)
                {
                    if (aFront)
                    {
                        release(d.items.front());
                        d.items.pop_front();
                    }
                    else
                    {
                        release(d.items.back());
                        d.items.pop_back();
                    }
                }
            }

            void deque_append(deque_object& d, object* aItem, bool aLeft)
            {
                ++d.state;
                if (d.maxlen == 0)
                    return;
                retain(aItem);
                if (aLeft)
                    d.items.push_front(aItem);
                else
                    d.items.push_back(aItem);
                deque_trim(d, !aLeft);
            }

            object* make_deque(std::vector<object*> const& aItems, long long aMaxlen)
            {
                auto const d = new deque_object{ { { &sDequeType.base } } };
                d->maxlen = aMaxlen;
                for (auto i : aItems)
                    deque_append(*d, i, false);
                return &d->h.base;
            }

            bool deque_extend(object* aSelf, object* aIterable, bool aLeft)
            {
                std::vector<object*> items;
                if (!collect(aIterable == aSelf ? (retain(aSelf), make_list(std::vector<object*>{ as_deque(aSelf).items.begin(), as_deque(aSelf).items.end() })) : aIterable, items))
                    return false;
                for (auto i : items)
                {
                    deque_append(as_deque(aSelf), i, aLeft);
                    release(i);
                }
                return true;
            }

            bool deque_index(deque_object const& d, object* aIndex, std::size_t& aResult)
            {
                long long i = 0;
                if (!is_int_like(aIndex) && !(kind_of(aIndex) == NEOS_PY_OTHER && to_index(aIndex, i)))
                {
                    clear_error();
                    error("TypeError", std::string{ "sequence index must be integer, not '" } + type_name(aIndex) + "'");
                    return false;
                }
                if (!to_index(aIndex, i))
                    return false;
                auto const n = static_cast<long long>(d.items.size());
                if (i < 0)
                    i += n;
                if (i < 0 || i >= n)
                {
                    error("IndexError", "deque index out of range");
                    return false;
                }
                aResult = static_cast<std::size_t>(i);
                return true;
            }

            struct deque_method
            {
                char const* name;
                object* (*function)(object* aSelf, std::vector<object*> const& aArgs, object* aKwargs);
            };

            bool exact(char const* aName, std::vector<object*> const& aArgs, object* aKwargs, std::size_t aCount)
            {
                if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                {
                    error("TypeError", std::string{ "deque." } + aName + "() takes no keyword arguments");
                    return false;
                }
                if (aArgs.size() != aCount)
                {
                    if (aCount == 0u)
                        error("TypeError", std::string{ "deque." } + aName + "() takes no arguments (" + std::to_string(aArgs.size()) + " given)");
                    else
                        error("TypeError", std::string{ "deque." } + aName + "() takes exactly one argument (" + std::to_string(aArgs.size()) + " given)");
                    return false;
                }
                return true;
            }

            deque_method const sDequeMethods[] =
            {
                { "append", [](object* s, std::vector<object*> const& a, object* k) -> object*
                    { if (!exact("append", a, k, 1u)) return nullptr; deque_append(as_deque(s), a[0], false); return none(); } },
                { "appendleft", [](object* s, std::vector<object*> const& a, object* k) -> object*
                    { if (!exact("appendleft", a, k, 1u)) return nullptr; deque_append(as_deque(s), a[0], true); return none(); } },
                { "pop", [](object* s, std::vector<object*> const& a, object* k) -> object*
                    {
                        if (!exact("pop", a, k, 0u))
                            return nullptr;
                        auto& d = as_deque(s);
                        if (d.items.empty())
                            return error("IndexError", "pop from an empty deque");
                        ++d.state;
                        auto const item = d.items.back();
                        d.items.pop_back();
                        return item;
                    } },
                { "popleft", [](object* s, std::vector<object*> const& a, object* k) -> object*
                    {
                        if (!exact("popleft", a, k, 0u))
                            return nullptr;
                        auto& d = as_deque(s);
                        if (d.items.empty())
                            return error("IndexError", "pop from an empty deque");
                        ++d.state;
                        auto const item = d.items.front();
                        d.items.pop_front();
                        return item;
                    } },
                { "extend", [](object* s, std::vector<object*> const& a, object* k) -> object*
                    { return exact("extend", a, k, 1u) && deque_extend(s, a[0], false) ? none() : nullptr; } },
                { "extendleft", [](object* s, std::vector<object*> const& a, object* k) -> object*
                    { return exact("extendleft", a, k, 1u) && deque_extend(s, a[0], true) ? none() : nullptr; } },
                { "clear", [](object* s, std::vector<object*> const& a, object* k) -> object*
                    {
                        if (!exact("clear", a, k, 0u))
                            return nullptr;
                        auto& d = as_deque(s);
                        ++d.state;
                        std::deque<object*> items;
                        std::swap(items, d.items);
                        for (auto i : items)
                            release(i);
                        return none();
                    } },
                { "copy", [](object* s, std::vector<object*> const& a, object* k) -> object*
                    {
                        if (!exact("copy", a, k, 0u))
                            return nullptr;
                        auto const& d = as_deque(s);
                        return make_deque(std::vector<object*>{ d.items.begin(), d.items.end() }, d.maxlen);
                    } },
                { "__copy__", [](object* s, std::vector<object*> const& a, object* k) -> object*
                    {
                        if (!exact("__copy__", a, k, 0u))
                            return nullptr;
                        auto const& d = as_deque(s);
                        return make_deque(std::vector<object*>{ d.items.begin(), d.items.end() }, d.maxlen);
                    } },
                { "count", [](object* s, std::vector<object*> const& a, object* k) -> object*
                    {
                        if (!exact("count", a, k, 1u))
                            return nullptr;
                        std::vector<object*> const items{ as_deque(s).items.begin(), as_deque(s).items.end() };
                        long long n = 0;
                        for (auto i : items)
                        {
                            auto const e = equal(i, a[0]);
                            if (e < 0)
                                return nullptr;
                            n += e;
                        }
                        return make_int(n);
                    } },
                { "index", [](object* s, std::vector<object*> const& a, object* k) -> object*
                    {
                        std::vector<object*> args;
                        if (!parse_arguments("index", a, k, { "value", "start", "stop" }, 1u, 3u, args))
                            return nullptr;
                        auto const& d = as_deque(s);
                        auto const n = static_cast<long long>(d.items.size());
                        long long start = 0, stop = n;
                        if ((args[1] != nullptr && !to_index(args[1], start)) || (args[2] != nullptr && !to_index(args[2], stop)))
                            return nullptr;
                        if (start < 0)
                            start = std::max(0ll, start + n);
                        if (stop < 0)
                            stop = std::max(0ll, stop + n);
                        stop = std::min(stop, n);
                        for (auto i = start; i < stop; ++i)
                        {
                            auto const e = equal(d.items[static_cast<std::size_t>(i)], args[0]);
                            if (e < 0)
                                return nullptr;
                            if (e == 1)
                                return make_int(i);
                        }
                        std::string text;
                        if (!repr_of(args[0], text))
                            return nullptr;
                        return error("ValueError", text + " is not in deque");
                    } },
                { "insert", [](object* s, std::vector<object*> const& a, object* k) -> object*
                    {
                        if (k != nullptr && !as<dict_object>(k)->items.empty())
                            return error("TypeError", "deque.insert() takes no keyword arguments");
                        if (a.size() != 2u)
                            return error("TypeError", "insert expected 2 arguments, got " + std::to_string(a.size()));
                        auto& d = as_deque(s);
                        if (d.maxlen >= 0 && static_cast<long long>(d.items.size()) >= d.maxlen)
                            return error("IndexError", "deque already at its maximum size");
                        long long i = 0;
                        if (!to_index(a[0], i))
                            return nullptr;
                        auto const n = static_cast<long long>(d.items.size());
                        if (i < 0)
                            i = std::max(0ll, i + n);
                        i = std::min(i, n);
                        ++d.state;
                        retain(a[1]);
                        d.items.insert(d.items.begin() + i, a[1]);
                        return none();
                    } },
                { "remove", [](object* s, std::vector<object*> const& a, object* k) -> object*
                    {
                        if (!exact("remove", a, k, 1u))
                            return nullptr;
                        auto& d = as_deque(s);
                        for (std::size_t i = 0u; i < d.items.size(); ++i)
                        {
                            auto const e = equal(d.items[i], a[0]);
                            if (e < 0)
                                return nullptr;
                            if (e == 1)
                            {
                                ++d.state;
                                auto const item = d.items[i];
                                d.items.erase(d.items.begin() + static_cast<std::ptrdiff_t>(i));
                                release(item);
                                return none();
                            }
                        }
                        std::string text;
                        if (!repr_of(a[0], text))
                            return nullptr;
                        return error("ValueError", text + " is not in deque");
                    } },
                { "reverse", [](object* s, std::vector<object*> const& a, object* k) -> object*
                    {
                        if (!exact("reverse", a, k, 0u))
                            return nullptr;
                        auto& d = as_deque(s);
                        ++d.state;
                        std::reverse(d.items.begin(), d.items.end());
                        return none();
                    } },
                { "rotate", [](object* s, std::vector<object*> const& a, object* k) -> object*
                    {
                        if (k != nullptr && !as<dict_object>(k)->items.empty())
                            return error("TypeError", "deque.rotate() takes no keyword arguments");
                        if (a.size() > 1u)
                            return error("TypeError", "rotate expected at most 1 argument, got " + std::to_string(a.size()));
                        long long n = 1;
                        if (!a.empty() && !to_index(a[0], n))
                            return nullptr;
                        auto& d = as_deque(s);
                        auto const size = static_cast<long long>(d.items.size());
                        if (size > 1)
                        {
                            ++d.state;
                            n %= size;
                            if (n < 0)
                                n += size;
                            std::rotate(d.items.begin(), d.items.begin() + (size - n), d.items.end());
                        }
                        return none();
                    } },
                { "__reversed__", [](object* s, std::vector<object*> const& a, object* k) -> object*
                    {
                        if (!exact("__reversed__", a, k, 0u))
                            return nullptr;
                        retain(s);
                        return &(new deque_iterator_object{ { { &sDequeReverseIteratorType.base } }, s, 0u, as_deque(s).state, true })->h.base;
                    } }
            };

            object* call_deque_method(neos_py_context*, void* aData, object* aArgs, object* aKwargs)
            {
                auto const& method = *static_cast<deque_method const*>(aData);
                auto const& items = as<tuple_object>(aArgs)->items;
                if (items.empty() || !is_deque(items[0]))
                    return error("TypeError", std::string{ "descriptor '" } + method.name + "' for 'collections.deque' objects doesn't apply to this object");
                std::vector<object*> const args{ items.begin() + 1, items.end() };
                return method.function(items[0], args, aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty() ? aKwargs : nullptr);
            }

            neos_py_type make_deque_iterator_type(bool aReversed)
            {
                auto type = make_type(aReversed ? "_collections._deque_reverse_iterator" : "_collections._deque_iterator",
                    aReversed ? "_deque_reverse_iterator" : "_deque_iterator", retain_object, release_object<deque_iterator_object>);
                type.iter = [](neos_py_context*, object* aSelf) -> object* { retain(aSelf); return aSelf; };
                type.next = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto& it = *reinterpret_cast<deque_iterator_object*>(aSelf);
                    auto const& d = as_deque(it.deque);
                    if (d.state != it.state)
                    {
                        it.index = std::numeric_limits<std::size_t>::max() / 2u;
                        return error("RuntimeError", "deque mutated during iteration");
                    }
                    if (it.index >= d.items.size())
                        return nullptr;
                    auto const item = it.reversed ? d.items[d.items.size() - 1u - it.index] : d.items[it.index];
                    ++it.index;
                    return (retain(item), item);
                };
                type.length = [](neos_py_context*, object* aSelf) -> long long
                {
                    auto const& it = *reinterpret_cast<deque_iterator_object*>(aSelf);
                    auto const n = as_deque(it.deque).items.size();
                    return it.index >= n ? 0 : static_cast<long long>(n - it.index);
                };
                return type;
            }

            // (the items of a deque as a list (for comparisons and arithmetic))
            object* deque_list(object* aSelf)
            {
                auto const& d = as_deque(aSelf);
                std::vector<object*> items{ d.items.begin(), d.items.end() };
                for (auto i : items)
                    retain(i);
                return make_list(std::move(items));
            }

            neos_py_type make_deque_type()
            {
                auto type = make_type("collections.deque", "deque", retain_object, release_object<deque_object>);
                type.repr = [](neos_py_context*, object* aSelf) -> object*
                {
                    auto const& d = as_deque(aSelf);
                    ref const list{ deque_list(aSelf) };
                    std::string text;
                    if (!repr_of(list.get(), text))
                        return nullptr;
                    if (d.maxlen >= 0)
                        return make_str("deque(" + text + ", maxlen=" + std::to_string(d.maxlen) + ")");
                    return make_str("deque(" + text + ")");
                };
                type.getattr = [](neos_py_context*, object* aSelf, char const* aName) -> object*
                {
                    for (auto const& m : sDequeMethods)
                        if (std::strcmp(m.name, aName) == 0)
                            return make_function(m.name, call_deque_method, const_cast<deque_method*>(&m), aSelf);
                    if (std::strcmp(aName, "maxlen") == 0)
                        return as_deque(aSelf).maxlen < 0 ? (retain(none()), none()) : make_int(as_deque(aSelf).maxlen);
                    return error("AttributeError", std::string{ "'collections.deque' object has no attribute '" } + aName + "'");
                };
                type.length = [](neos_py_context*, object* aSelf) -> long long { return static_cast<long long>(as_deque(aSelf).items.size()); };
                type.hash = [](neos_py_context*, object*, long long*) -> int { error("TypeError", "unhashable type: 'collections.deque'"); return -1; };
                type.truth = [](neos_py_context*, object* aSelf) -> int { return as_deque(aSelf).items.empty() ? 0 : 1; };
                type.iter = [](neos_py_context*, object* aSelf) -> object*
                {
                    retain(aSelf);
                    return &(new deque_iterator_object{ { { &sDequeIteratorType.base } }, aSelf, 0u, as_deque(aSelf).state, false })->h.base;
                };
                type.contains = [](neos_py_context*, object* aSelf, object* aItem) -> int
                {
                    std::vector<object*> const items{ as_deque(aSelf).items.begin(), as_deque(aSelf).items.end() };
                    for (auto i : items)
                    {
                        auto const e = equal(i, aItem);
                        if (e != 0)
                            return e;
                    }
                    return 0;
                };
                type.getitem = [](neos_py_context*, object* aSelf, object* aKey) -> object*
                {
                    if (kind_of(aKey) == NEOS_PY_SLICE)
                        return error("TypeError", "sequence index must be integer, not 'slice'");
                    std::size_t i = 0u;
                    if (!deque_index(as_deque(aSelf), aKey, i))
                        return nullptr;
                    auto const item = as_deque(aSelf).items[i];
                    return (retain(item), item);
                };
                type.setitem = [](neos_py_context*, object* aSelf, object* aKey, object* aValue) -> int
                {
                    auto& d = as_deque(aSelf);
                    std::size_t i = 0u;
                    if (!deque_index(d, aKey, i))
                        return -1;
                    if (aValue == nullptr)
                    {
                        ++d.state;
                        auto const item = d.items[i];
                        d.items.erase(d.items.begin() + static_cast<std::ptrdiff_t>(i));
                        release(item);
                        return 0;
                    }
                    retain(aValue);
                    release(d.items[i]);
                    d.items[i] = aValue;
                    return 0;
                };
                type.compare = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs) -> object*
                {
                    if (!is_deque(aLhs) || !is_deque(aRhs))
                        return (retain(not_implemented()), not_implemented());
                    ref const lhs{ deque_list(aLhs) };
                    ref const rhs{ deque_list(aRhs) };
                    return compare(aOperation, lhs.get(), rhs.get());
                };
                type.binary = [](neos_py_context*, int aOperation, object* aLhs, object* aRhs, int aInplace) -> object*
                {
                    if (!is_deque(aLhs))
                    {
                        if (aOperation == NEOS_PY_MULTIPLY && is_deque(aRhs))
                            std::swap(aLhs, aRhs);
                        else
                            return (retain(not_implemented()), not_implemented());
                    }
                    auto& d = as_deque(aLhs);
                    if (aOperation == NEOS_PY_ADD)
                    {
                        if (!is_deque(aRhs))
                        {
                            if (aInplace)
                                return deque_extend(aLhs, aRhs, false) ? (retain(aLhs), aLhs) : nullptr;
                            return error("TypeError", std::string{ "can only concatenate deque (not \"" } + type_name(aRhs) + "\") to deque");
                        }
                        if (aInplace)
                            return deque_extend(aLhs, aRhs, false) ? (retain(aLhs), aLhs) : nullptr;
                        std::vector<object*> items{ d.items.begin(), d.items.end() };
                        items.insert(items.end(), as_deque(aRhs).items.begin(), as_deque(aRhs).items.end());
                        return make_deque(items, d.maxlen);
                    }
                    if (aOperation == NEOS_PY_MULTIPLY && is_int_like(aRhs))
                    {
                        long long n = 0;
                        if (!to_index(aRhs, n))
                            return nullptr;
                        std::vector<object*> const original{ d.items.begin(), d.items.end() };
                        std::vector<object*> items;
                        for (long long r = 0; r < n; ++r)
                            items.insert(items.end(), original.begin(), original.end());
                        if (!aInplace)
                            return make_deque(items, d.maxlen);
                        for (auto i : items)
                            retain(i);
                        for (auto i : d.items)
                            release(i);
                        d.items.clear();
                        ++d.state;
                        for (auto i : items)
                        {
                            deque_append(d, i, false);
                            release(i);
                        }
                        return (retain(aLhs), aLhs);
                    }
                    return (retain(not_implemented()), not_implemented());
                };
                return type;
            }

            object* new_deque(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("deque", as<tuple_object>(aArgs)->items, aKwargs, { "iterable", "maxlen" }, 0u, 0u, a))
                    return nullptr;
                long long maxlen = -1;
                if (a[1] != nullptr && kind_of(a[1]) != NEOS_PY_NONE)
                {
                    if (!to_index(a[1], maxlen))
                        return nullptr;
                    if (maxlen < 0)
                        return error("ValueError", "maxlen must be non-negative");
                }
                ref result{ make_deque({}, maxlen) };
                if (a[0] != nullptr && !deque_extend(result.get(), a[0], false))
                    return nullptr;
                return result.release_ownership();
            }

            int deque_instancecheck(neos_py_context*, void*, object* aObject) { return is_deque(aObject) ? 1 : 0; }

            // --- ChainMap

            object* maps_of(object* aSelf)
            {
                return getattr(aSelf, "maps");
            }

            // (the keys of all the maps: the last map's first)
            bool chain_keys(object* aSelf, std::vector<object*>& aKeys)
            {
                ref const maps{ maps_of(aSelf) };
                if (!maps)
                    return false;
                ref const merged{ make_dict() };
                std::vector<object*> mapList;
                if (!collect(maps.get(), mapList))
                    return false;
                std::vector<ref> owned;
                for (auto m : mapList)
                    owned.emplace_back(m);
                for (auto m = mapList.rbegin(); m != mapList.rend(); ++m)
                {
                    std::vector<object*> keys;
                    if (!collect(*m, keys))
                        return false;
                    for (auto k : keys)
                    {
                        dict_set(merged.get(), k, none());
                        release(k);
                    }
                }
                for (auto const& item : as<dict_object>(merged.get())->items)
                    aKeys.push_back((retain(item.first), item.first));
                return true;
            }

            // (the first map that has a key: null (no exception) if none)
            object* chain_find(object* aSelf, object* aKey, bool& aFailed)
            {
                aFailed = false;
                ref const maps{ maps_of(aSelf) };
                std::vector<object*> mapList;
                if (!maps || !collect(maps.get(), mapList))
                {
                    aFailed = true;
                    return nullptr;
                }
                std::vector<ref> owned;
                for (auto m : mapList)
                    owned.emplace_back(m);
                for (auto m : mapList)
                {
                    auto const c = contains(m, aKey);
                    if (c < 0)
                    {
                        aFailed = true;
                        return nullptr;
                    }
                    if (c == 1)
                        return getitem(m, aKey);
                }
                return nullptr;
            }

            object* chain_first_map(object* aSelf)
            {
                ref const maps{ maps_of(aSelf) };
                if (!maps)
                    return nullptr;
                ref const zero{ make_int(0) };
                return getitem(maps.get(), zero.get());
            }

            builtin_entry const sChainMapMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        std::vector<object*> maps;
                        for (std::size_t i = 1u; i < aArgs.size(); ++i)
                            maps.push_back((retain(aArgs[i]), aArgs[i]));
                        if (maps.empty())
                            maps.push_back(make_dict());
                        ref const list{ make_list(std::move(maps)) };
                        return setattr(aArgs.at(0), "maps", list.get()) < 0 ? nullptr : none();
                    } },
                { "__getitem__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        bool failed = false;
                        if (auto const r = chain_find(aArgs.at(0), aArgs.at(1), failed))
                            return r;
                        if (failed)
                            return nullptr;
                        ref const missing{ special_method(aArgs[0], "__missing__") };
                        if (missing)
                            return call_with(missing.get(), { (retain(aArgs[1]), aArgs[1]) });
                        raise_key_error(aArgs[1]);
                        return nullptr;
                    } },
                { "get", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() < 2u || aArgs.size() > 3u)
                            return error("TypeError", "get expected 1 or 2 arguments");
                        bool failed = false;
                        if (auto const r = chain_find(aArgs[0], aArgs[1], failed))
                            return r;
                        if (failed)
                            return nullptr;
                        auto const d = aArgs.size() == 3u ? aArgs[2] : none();
                        return (retain(d), d);
                    } },
                { "__contains__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        bool failed = false;
                        ref const r{ chain_find(aArgs.at(0), aArgs.at(1), failed) };
                        return failed ? nullptr : make_bool(static_cast<bool>(r));
                    } },
                { "__len__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        std::vector<object*> keys;
                        if (!chain_keys(aArgs.at(0), keys))
                            return nullptr;
                        for (auto k : keys)
                            release(k);
                        return make_int(static_cast<long long>(keys.size()));
                    } },
                { "__iter__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        std::vector<object*> keys;
                        if (!chain_keys(aArgs.at(0), keys))
                            return nullptr;
                        ref const list{ make_list(std::move(keys)) };
                        return iter(list.get());
                    } },
                { "keys", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        std::vector<object*> keys;
                        if (!chain_keys(aArgs.at(0), keys))
                            return nullptr;
                        ref const d{ make_dict() };
                        for (auto k : keys)
                        {
                            dict_set(d.get(), k, none());
                            release(k);
                        }
                        return call_method_named(d.get(), "keys", {});
                    } },
                { "items", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        std::vector<object*> keys;
                        if (!chain_keys(aArgs.at(0), keys))
                            return nullptr;
                        ref const d{ make_dict() };
                        for (auto k : keys)
                        {
                            ref const key{ k };
                            ref const value{ getitem(aArgs[0], k) };
                            if (!value)
                                return nullptr;
                            dict_set(d.get(), k, value.get());
                        }
                        return call_method_named(d.get(), "items", {});
                    } },
                { "values", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        std::vector<object*> keys;
                        if (!chain_keys(aArgs.at(0), keys))
                            return nullptr;
                        ref const d{ make_dict() };
                        for (auto k : keys)
                        {
                            ref const key{ k };
                            ref const value{ getitem(aArgs[0], k) };
                            if (!value)
                                return nullptr;
                            dict_set(d.get(), k, value.get());
                        }
                        return call_method_named(d.get(), "values", {});
                    } },
                { "__setitem__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const first{ chain_first_map(aArgs.at(0)) };
                        return first && setitem(first.get(), aArgs.at(1), aArgs.at(2)) == 0 ? none() : nullptr;
                    } },
                { "__delitem__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const first{ chain_first_map(aArgs.at(0)) };
                        if (!first)
                            return nullptr;
                        if (setitem(first.get(), aArgs.at(1), nullptr) == 0)
                            return none();
                        clear_error();
                        std::string key;
                        repr_of(aArgs[1], key);
                        return error("KeyError", "\"Key not found in the first mapping: " + key + "\"");
                    } },
                { "pop", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const first{ chain_first_map(aArgs.at(0)) };
                        if (!first)
                            return nullptr;
                        std::vector<object*> args;
                        for (std::size_t i = 1u; i < aArgs.size(); ++i)
                            args.push_back((retain(aArgs[i]), aArgs[i]));
                        auto const r = call_method_named(first.get(), "pop", std::move(args));
                        if (r == nullptr && std::string_view{ error_text() }.starts_with("KeyError"))
                        {
                            clear_error();
                            std::string key;
                            repr_of(aArgs[1], key);
                            return error("KeyError", "\"Key not found in the first mapping: " + key + "\"");
                        }
                        return r;
                    } },
                { "clear", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const first{ chain_first_map(aArgs.at(0)) };
                        return first ? call_method_named(first.get(), "clear", {}) : nullptr;
                    } },
                { "new_child", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        ref child{ aArgs.size() > 1u && kind_of(aArgs[1]) != NEOS_PY_NONE ? (retain(aArgs[1]), aArgs[1]) : make_dict() };
                        if (aKwargs != nullptr && !dict_update(builtin_value(child.get()), aKwargs))
                            return nullptr;
                        ref const maps{ maps_of(aArgs[0]) };
                        std::vector<object*> mapList;
                        if (!maps || !collect(maps.get(), mapList))
                            return nullptr;
                        mapList.insert(mapList.begin(), child.release_ownership());
                        ref const cls{ class_of(aArgs[0]) };
                        return call_with(cls.get(), std::move(mapList));
                    } },
                { "copy", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const maps{ maps_of(aArgs.at(0)) };
                        std::vector<object*> mapList;
                        if (!maps || !collect(maps.get(), mapList))
                            return nullptr;
                        ref const copy{ call_method_named(mapList[0], "copy", {}) };
                        if (!copy)
                        {
                            for (auto m : mapList)
                                release(m);
                            return nullptr;
                        }
                        release(mapList[0]);
                        mapList[0] = (retain(copy.get()), copy.get());
                        ref const cls{ class_of(aArgs[0]) };
                        return call_with(cls.get(), std::move(mapList));
                    } },
                { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const maps{ maps_of(aArgs.at(0)) };
                        std::vector<object*> mapList;
                        if (!maps || !collect(maps.get(), mapList))
                            return nullptr;
                        std::string text;
                        bool const ok = join_repr(mapList, text);
                        for (auto m : mapList)
                            release(m);
                        return ok ? make_str(class_name_of(aArgs[0]) + "(" + text + ")") : nullptr;
                    } },
                { "__bool__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const maps{ maps_of(aArgs.at(0)) };
                        std::vector<object*> mapList;
                        if (!maps || !collect(maps.get(), mapList))
                            return nullptr;
                        bool any = false;
                        for (auto m : mapList)
                        {
                            any = any || truth(m) == 1;
                            release(m);
                        }
                        return make_bool(any);
                    } },
                { "__eq__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const items{ call_method_named(aArgs.at(0), "items", {}) };
                        if (!items)
                            return nullptr;
                        ref const mine{ b_dict({ items.get() }, nullptr) };
                        ref const theirs{ kind_of(builtin_value(aArgs.at(1))) == NEOS_PY_DICT ? (retain(builtin_value(aArgs[1])), builtin_value(aArgs[1])) :
                            (has_special_method(aArgs[1], "keys") ? b_dict({ aArgs[1] }, nullptr) : nullptr) };
                        if (!mine)
                            return nullptr;
                        if (!theirs)
                        {
                            clear_error();
                            return (retain(not_implemented()), not_implemented());
                        }
                        return compare(NEOS_PY_EQ, mine.get(), theirs.get());
                    } }
            };

            object* parents_getter(std::vector<object*> const& aArgs, object*)
            {
                ref const maps{ maps_of(aArgs.at(0)) };
                std::vector<object*> mapList;
                if (!maps || !collect(maps.get(), mapList))
                    return nullptr;
                if (!mapList.empty())
                {
                    release(mapList.front());
                    mapList.erase(mapList.begin());
                }
                ref const cls{ class_of(aArgs[0]) };
                return call_with(cls.get(), std::move(mapList));
            }


            // --- the module

            object* class_with(char const* aName, builtin_entry const* aMethods, std::size_t aCount, object* aBase,
                std::vector<std::pair<std::string, object*>> aExtra = {})
            {
                std::vector<std::pair<std::string, object*>> ns = std::move(aExtra);
                for (std::size_t i = 0u; i < aCount; ++i)
                    ns.emplace_back(aMethods[i].name, make_builtin(aMethods[i]));
                std::vector<object*> bases;
                if (aBase != nullptr)
                    bases.push_back(aBase);
                auto const cls = make_class("collections", aName, bases, ns);
                for (auto& n : ns)
                    release(n.second);
                return cls;
            }

            builtin_entry const sCollectionsFunctions[] =
            {
                { "namedtuple", b_namedtuple }
            };

            bool collections_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                for (auto const& f : sCollectionsFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                auto const deque = context()->type_new(context(), "deque", "collections.deque", new_deque, nullptr, deque_instancecheck);
                // (deque's special methods (e.g. deque.__repr__): its slots)
                observed_types().try_emplace("collections.deque", &sDequeType);
                aAttributes.emplace_back("deque", deque);
                ref const dictBase{ dict_type() };
                {
                    static builtin_entry const sFromKeys{ "fromkeys", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        // (OrderedDict.fromkeys(iterable, value=None): the class's)
                        std::vector<object*> a;
                        std::vector<object*> const rest{ aArgs.begin() + 1, aArgs.end() };
                        if (!parse_arguments("fromkeys", rest, aKwargs, { "iterable", "value" }, 1u, 0u, a))
                            return nullptr;
                        ref result{ call_with(aArgs[0], {}) };
                        std::vector<object*> keys;
                        if (!result || !collect(a[0], keys))
                            return nullptr;
                        bool ok = true;
                        for (auto k : keys)
                        {
                            ok = ok && setitem(result.get(), k, a[1] != nullptr ? a[1] : none()) == 0;
                            release(k);
                        }
                        return ok ? result.release_ownership() : nullptr;
                    } };
                    ref const fromkeys{ make_builtin(sFromKeys) };
                    std::vector<std::pair<std::string, object*>> extra{ { "fromkeys", b_classmethod({ fromkeys.get() }, nullptr) } };
                    aAttributes.emplace_back("OrderedDict", class_with("OrderedDict", sOrderedDictMethods, std::size(sOrderedDictMethods), dictBase.get(), std::move(extra)));
                }
                // (default_factory: the class's (an instance's, once __init__ sets it))
                aAttributes.emplace_back("defaultdict", class_with("defaultdict", sDefaultDictMethods, std::size(sDefaultDictMethods), dictBase.get(),
                    { { "default_factory", new_reference(none()) } }));
                aAttributes.emplace_back("Counter", counter_class());
                if (auto const abc = fn_import("collections.abc"))
                {
                    aAttributes.emplace_back("abc", abc);
                    // (_collections_abc.MutableSequence.register(deque))
                    ref const mutableSequence{ getattr(abc, "MutableSequence") };
                    ref const r{ mutableSequence ? call_method_named(mutableSequence.get(), "register", { new_reference(deque) }) : nullptr };
                    if (!r)
                        return false;
                }
                else
                    return false;
                {
                    static builtin_entry const sParents{ "parents", parents_getter };
                    ref const getter{ make_builtin(sParents) };
                    std::vector<std::pair<std::string, object*>> extra{ { "parents", b_property({ getter.get() }, nullptr) }, { "__hash__", (retain(none()), none()) } };
                    aAttributes.emplace_back("ChainMap", class_with("ChainMap", sChainMapMethods, std::size(sChainMapMethods), nullptr, std::move(extra)));
                }
                return true;
            }

            inline bool const sCollections = register_module("collections", collections_module);
        }
    }
}
