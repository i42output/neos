/*
  enum.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/enum.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module enum (CPython 3.13's enum.py: its algorithms, behaviour and messages): EnumType (the metaclass of enumerations),
// EnumDict (their class bodies' namespaces), Enum, ReprEnum, IntEnum, StrEnum, Flag, IntFlag, auto, member, nonmember, property,
// unique, verify, global_enum, ...

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
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
            // --- the module's classes and singletons (made when it is imported)

            struct enum_classes
            {
                object* enumType = nullptr;
                object* enumDict = nullptr;
                object* enumClass = nullptr;        ///< (Enum: null while it is made)
                object* reprEnum = nullptr;
                object* flag = nullptr;             ///< (Flag: null while it is made)
                object* autoClass = nullptr;
                object* memberClass = nullptr;
                object* nonmemberClass = nullptr;
                object* propertyClass = nullptr;
                object* notGiven = nullptr;
                object* autoNull = nullptr;
                object* strict = nullptr;
                object* conform = nullptr;
                object* eject = nullptr;
                object* keep = nullptr;
                object* unique = nullptr;
                object* continuous = nullptr;
                object* namedFlags = nullptr;
                object* enumNew = nullptr;          ///< (Enum.__new__)
            } s;

            // --- helpers

            ref attribute(object* aObject, char const* aName) { return ref{ getattr(aObject, aName) }; }

            // (getattr(object, name, None))
            ref attribute_or_none(object* aObject, char const* aName)
            {
                ref result{ getattr(aObject, aName) };
                if (!result)
                {
                    clear_error();
                    result = ref{ new_reference(none()) };
                }
                return result;
            }

            bool has_attribute(object* aObject, char const* aName)
            {
                ref const result{ getattr(aObject, aName) };
                if (!result)
                    clear_error();
                return static_cast<bool>(result);
            }

            int instance_of(object* aObject, object* aClass)
            {
                if (aClass == nullptr)
                    return 0;
                ref const r{ b_isinstance({ aObject, aClass }, nullptr) };
                return r ? truth(r.get()) : -1;
            }

            bool subclass_of(object* aClass, object* aBase)
            {
                if (aBase == nullptr || !is_class(aClass) || !is_class(aBase))
                    return false;
                return is_subclass(aClass, aBase);
            }

            bool is_none(object* aObject) { return aObject == nullptr || kind_of(aObject) == NEOS_PY_NONE; }

            std::string class_name(object* aClass)
            {
                ref const n{ getattr(aClass, "__name__") };
                return n ? text_of(n.get()) : (clear_error(), std::string{});
            }

            std::string class_qualname(object* aClass)
            {
                ref const n{ getattr(aClass, "__qualname__") };
                return n ? text_of(n.get()) : (clear_error(), class_name(aClass));
            }

            // (a dict's item (borrowed): null if absent (-2 in aFound: an exception (e.g. unhashable)))
            object* dict_item(object* aDict, object* aKey, int* aFound = nullptr)
            {
                auto const at = dict_find(aDict, aKey);
                if (aFound != nullptr)
                    *aFound = at >= 0 ? 1 : at == -2 ? -2 : 0;
                return at >= 0 ? as<dict_object>(aDict)->items[static_cast<std::size_t>(at)].second : nullptr;
            }

            object* dict_item(object* aDict, char const* aKey)
            {
                ref const key{ make_str(aKey) };
                return dict_item(aDict, key.get());
            }

            bool dict_put(object* aDict, char const* aKey, object* aValue)
            {
                ref const key{ make_str(aKey) };
                return dict_set(aDict, key.get(), aValue) >= 0;
            }

            bool dict_remove(object* aDict, char const* aKey)
            {
                ref const key{ make_str(aKey) };
                auto const at = dict_find(aDict, key.get());
                if (at < 0)
                    return false;
                dict_erase(*as<dict_object>(aDict), static_cast<std::size_t>(at));
                return true;
            }

            // (a class attribute that is a dict or a list (e.g. _member_map_): borrowed through a reference)
            ref class_dict(object* aClass, char const* aName) { return attribute(aClass, aName); }

            bool list_append(object* aList, object* aItem)
            {
                if (kind_of(aList) != NEOS_PY_LIST)
                {
                    ref const r{ call_method_named(aList, "append", { new_reference(aItem) }) };
                    return static_cast<bool>(r);
                }
                as<list_object>(aList)->items.push_back(new_reference(aItem));
                return true;
            }

            // (formatting with %: e.g. "%r is not a valid %s")
            std::string format(char const* aFormat, std::vector<object*> aValues)    // (steals the values)
            {
                ref const f{ make_str(aFormat) };
                ref const values{ make_tuple(std::move(aValues)) };
                ref const r{ binary(NEOS_PY_REMAINDER, f.get(), values.get(), false) };
                return r ? text_of(r.get()) : (clear_error(), std::string{});
            }

            object* raise(char const* aType, std::string const& aMessage)
            {
                return error(aType, aMessage);
            }

            bool is_dunder(std::string_view aName)
            {
                return aName.size() > 4u && aName.substr(0u, 2u) == "__" && aName.substr(aName.size() - 2u) == "__" && aName[2] != '_' && aName[aName.size() - 3u] != '_';
            }

            bool is_sunder(std::string_view aName)
            {
                return aName.size() > 2u && aName.front() == '_' && aName.back() == '_' && aName[1] != '_' && aName[aName.size() - 2u] != '_';
            }

            bool is_private(std::string const& aClassName, std::string_view aName)
            {
                auto const pattern = "_" + aClassName + "__";
                return aName.size() > pattern.size() && aName.starts_with(pattern) && (aName.back() != '_' || aName[aName.size() - 2u] != '_');
            }

            bool is_single_bit(object* aValue)
            {
                if (!is_int_like(aValue))
                    return false;
                ref const zero{ make_int(0) };
                if (equal(aValue, zero.get()) == 1)
                    return false;
                ref const one{ make_int(1) };
                ref const less{ binary(NEOS_PY_SUBTRACT, aValue, one.get(), false) };
                ref const masked{ less ? binary(NEOS_PY_AND, aValue, less.get(), false) : nullptr };
                if (!masked)
                {
                    clear_error();
                    return false;
                }
                return equal(masked.get(), zero.get()) == 1;
            }

            // (_is_descriptor: functions, methods, classmethod, staticmethod, property and objects with __get__, __set__ or __delete__)
            bool is_descriptor(object* aObject)
            {
                if (wrapper_type_name(aObject) != nullptr)
                    return true;
                std::string_view const type{ type_name(aObject) };
                if (type == "function" || type == "method" || type == "functools.partialmethod" || type == "cached_property")
                    return true;
                if (type == "partial")
                    return false;
                if (is_instance(aObject))
                    return has_attribute(aObject, "__get__") || has_attribute(aObject, "__set__") || has_attribute(aObject, "__delete__");
                return false;
            }

            bool is_internal_class(std::string const& aClassName, object* aObject)
            {
                if (!is_class(aObject))
                    return false;
                auto const qualname = class_qualname(aObject);
                auto const pattern = aClassName + "." + class_name(aObject);
                return qualname == pattern || qualname.ends_with("." + pattern);
            }

            // (a list of names from a str (separated by commas and whitespace) or an iterable)
            bool names_of(object* aValue, std::vector<object*>& aResult)
            {
                if (auto const text = str_of_value(aValue))
                {
                    std::string t = *text;
                    std::replace(t.begin(), t.end(), ',', ' ');
                    ref const split{ call_method_named(ref{ make_str(t) }.get(), "split", {}) };
                    return split && collect(split.get(), aResult);
                }
                return collect(aValue, aResult);
            }

            // --- auto, member, nonmember

            builtin_entry const sAutoMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("auto", aArgs, aKwargs, { "value" }, 0u, 0u, a))
                            return nullptr;
                        return setattr(aArgs[0], "value", a[0] != nullptr ? a[0] : s.autoNull) < 0 ? nullptr : new_reference(none());
                    } },
                { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const value{ attribute(aArgs[0], "value") };
                        return value ? make_str("auto(" + repr_text(value.get()) + ")") : nullptr;
                    } }
            };

            builtin_entry const sMemberMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("member", aArgs, aKwargs, { "value" }, 1u, 0u, a))
                            return nullptr;
                        return setattr(aArgs[0], "value", a[0]) < 0 ? nullptr : new_reference(none());
                    } }
            };

            // --- EnumDict(cls_name=None): the namespace of an enumeration's class body

            builtin_entry const sEnumDictMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("EnumDict", aArgs, aKwargs, { "cls_name" }, 0u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        ref const names{ make_dict() };
                        ref const last{ make_list() };
                        ref const ignore{ make_list() };
                        ref const autoCalled{ make_bool(false) };
                        if (setattr(self, "_member_names", names.get()) < 0 || setattr(self, "_last_values", last.get()) < 0 || setattr(self, "_ignore", ignore.get()) < 0 ||
                            setattr(self, "_auto_called", autoCalled.get()) < 0 || setattr(self, "_cls_name", a[0] != nullptr ? a[0] : none()) < 0)
                            return nullptr;
                        return new_reference(none());
                    } },
                { "__setitem__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 3u)
                            return raise("TypeError", "__setitem__ expected 2 arguments");
                        auto const self = aArgs[0];
                        ref key{ new_reference(aArgs[1]) };
                        ref value{ new_reference(aArgs[2]) };
                        auto const storage = builtin_value(self);
                        auto const keyText = str_of_value(key.get());
                        if (keyText == nullptr)
                            return dict_set(storage, key.get(), value.get()) < 0 ? nullptr : new_reference(none());
                        std::string const k = *keyText;
                        ref const clsNameValue{ attribute(self, "_cls_name") };
                        ref const memberNames{ attribute(self, "_member_names") };
                        ref const ignore{ attribute(self, "_ignore") };
                        if (!clsNameValue || !memberNames || !ignore)
                            return nullptr;
                        bool const hasClassName = kind_of(clsNameValue.get()) == NEOS_PY_STR;
                        if (hasClassName && is_private(text_of(clsNameValue.get()), k))
                            ;   // (a private name: not a member)
                        else if (is_sunder(k))
                        {
                            static std::set<std::string, std::less<>> const sAllowed = { "_order_", "_generate_next_value_", "_numeric_repr_", "_missing_",
                                "_ignore_", "_iter_member_", "_iter_member_by_value_", "_iter_member_by_def_", "_add_alias_", "_add_value_alias_" };
                            if (!sAllowed.contains(k) && !k.starts_with("_repr_"))
                                return raise("ValueError", "_sunder_ names, such as " + repr_text(key.get()) + ", are reserved for future Enum use");
                            if (k == "_generate_next_value_")
                            {
                                if (attribute_truth(self, "_auto_called") == 1)
                                    return raise("TypeError", "_generate_next_value_ must be defined before members");
                                ref gnv{ new_reference(value.get()) };
                                if (auto const w = wrapper_type_name(value.get()); w != nullptr && std::strcmp(w, "staticmethod") == 0)
                                    gnv = attribute(value.get(), "__func__");
                                if (!gnv || setattr(self, "_generate_next_value", gnv.get()) < 0)
                                    return nullptr;
                            }
                            else if (k == "_ignore_")
                            {
                                std::vector<object*> names;
                                if (!names_of(value.get(), names))
                                    return nullptr;
                                value = ref{ make_list(std::move(names)) };
                                if (setattr(self, "_ignore", value.get()) < 0)
                                    return nullptr;
                                std::vector<object*> already;
                                for (auto n : as<list_object>(value.get())->items)
                                    if (dict_find(memberNames.get(), n) >= 0)
                                        already.push_back(new_reference(n));
                                clear_error();
                                if (!already.empty())
                                {
                                    ref const list{ make_list(std::move(already)) };
                                    ref const set{ b_set({ list.get() }, nullptr) };
                                    return raise("ValueError", "_ignore_ cannot specify already set names: " + repr_text(set.get()));
                                }
                            }
                        }
                        else if (is_dunder(k))
                        {
                            if (k == "__order__")
                                key = ref{ make_str("_order_") };
                        }
                        else if (dict_find(memberNames.get(), key.get()) >= 0)
                        {
                            ref const existing{ getitem(storage, key.get()) };
                            return raise("TypeError", repr_text(key.get()) + " already defined as " + (existing ? repr_text(existing.get()) : std::string{}));
                        }
                        else if (contains(ignore.get(), key.get()) == 1)
                            ;   // (an ignored name)
                        else if (instance_of(value.get(), s.nonmemberClass) == 1)
                            value = attribute(value.get(), "value");
                        else if (is_descriptor(value.get()))
                            ;   // (a method or a descriptor: not a member)
                        else if (hasClassName && is_internal_class(text_of(clsNameValue.get()), value.get()))
                            ;   // (a nested class: not a member)
                        else
                        {
                            // (a member)
                            if (dict_find(storage, key.get()) >= 0)
                            {
                                ref const existing{ getitem(storage, key.get()) };
                                return raise("TypeError", repr_text(key.get()) + " already defined as " + (existing ? repr_text(existing.get()) : std::string{}));
                            }
                            if (instance_of(value.get(), s.memberClass) == 1)
                                value = attribute(value.get(), "value");
                            if (!value)
                                return nullptr;
                            bool nonAutoStore = true;
                            bool single = false;
                            if (instance_of(value.get(), s.autoClass) == 1)
                            {
                                single = true;
                                value = ref{ make_tuple({ new_reference(value.get()) }) };
                            }
                            ref const lastValues{ attribute(self, "_last_values") };
                            if (!lastValues)
                                return nullptr;
                            if (kind_of(builtin_value(value.get())) == NEOS_PY_TUPLE)
                            {
                                auto const& items = as<tuple_object>(builtin_value(value.get()))->items;
                                bool anyAuto = false;
                                for (auto v : items)
                                    anyAuto = anyAuto || instance_of(v, s.autoClass) == 1;
                                if (anyAuto)
                                {
                                    std::vector<object*> valued;
                                    for (auto v : items)
                                    {
                                        ref item{ new_reference(v) };
                                        if (instance_of(v, s.autoClass) == 1)
                                        {
                                            nonAutoStore = false;
                                            ref const current{ attribute(v, "value") };
                                            if (!current)
                                                return (std::for_each(valued.begin(), valued.end(), release), nullptr);
                                            if (current.get() == s.autoNull)
                                            {
                                                ref const generate{ attribute(self, "_generate_next_value") };
                                                ref const copy{ generate ? b_list({ lastValues.get() }, nullptr) : nullptr };
                                                ref const generated{ copy ? invoke(generate.get(), { new_reference(key.get()), make_int(1), make_int(length(memberNames.get())),
                                                    new_reference(copy.get()) }) : nullptr };
                                                if (!generated || setattr(v, "value", generated.get()) < 0)
                                                    return (std::for_each(valued.begin(), valued.end(), release), nullptr);
                                                ref const t{ make_bool(true) };
                                                setattr(self, "_auto_called", t.get());
                                            }
                                            item = attribute(v, "value");
                                            if (!item || !list_append(lastValues.get(), item.get()))
                                                return (std::for_each(valued.begin(), valued.end(), release), nullptr);
                                        }
                                        valued.push_back(new_reference(item.get()));
                                    }
                                    if (single)
                                        value = ref{ valued[0] };
                                    else
                                    {
                                        ref const cls{ class_of(value.get()) };
                                        ref const list{ make_list(std::move(valued)) };
                                        ref made{ is_type_object(cls.get()) ? fn_tuple_of(list.get()) : invoke(cls.get(), { new_reference(list.get()) }) };
                                        if (!made)
                                        {
                                            clear_error();
                                            ref const args{ fn_tuple_of(list.get()) };
                                            made = ref{ call(cls.get(), args.get(), nullptr) };
                                        }
                                        if (!made)
                                            return nullptr;
                                        value = made;
                                    }
                                }
                            }
                            if (dict_set(memberNames.get(), key.get(), none()) < 0)
                                return nullptr;
                            if (nonAutoStore && !list_append(lastValues.get(), value.get()))
                                return nullptr;
                        }
                        return dict_set(storage, key.get(), value.get()) < 0 ? nullptr : new_reference(none());
                    } },
                { "update", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aArgs.size() > 2u)
                            return raise("TypeError", "update expected at most 1 argument");
                        auto const self = aArgs[0];
                        auto const set = [&](object* aKey, object* aValue) -> bool
                        {
                            ref const r{ call_method_named(self, "__setitem__", { new_reference(aKey), new_reference(aValue) }) };
                            return static_cast<bool>(r);
                        };
                        if (aArgs.size() == 2u)
                        {
                            auto const members = aArgs[1];
                            if (ref const keys{ getattr(members, "keys") })
                            {
                                ref const list{ invoke(keys.get(), {}) };
                                std::vector<object*> items;
                                if (!list || !collect(list.get(), items))
                                    return nullptr;
                                for (auto k : items)
                                {
                                    ref const key{ k };
                                    ref const value{ getitem(members, k) };
                                    if (!value || !set(k, value.get()))
                                        return nullptr;
                                }
                            }
                            else
                            {
                                clear_error();
                                std::vector<object*> items;
                                if (!collect(members, items))
                                    return nullptr;
                                for (auto i : items)
                                {
                                    ref const item{ i };
                                    std::vector<object*> pair;
                                    if (!collect(i, pair) || pair.size() != 2u)
                                        return (std::for_each(pair.begin(), pair.end(), release), raise("ValueError", "need 2 values to unpack"));
                                    ref const k{ pair[0] };
                                    ref const v{ pair[1] };
                                    if (!set(k.get(), v.get()))
                                        return nullptr;
                                }
                            }
                        }
                        if (aKwargs != nullptr)
                            for (auto const& item : as<dict_object>(aKwargs)->items)
                                if (!set(item.first, item.second))
                                    return nullptr;
                        return new_reference(none());
                    } }
            };

            object* enum_dict_member_names(std::vector<object*> const& aArgs, object*)
            {
                ref const names{ attribute(aArgs[0], "_member_names") };
                return names ? b_list({ names.get() }, nullptr) : nullptr;
            }

            // --- EnumType

            // (_get_mixins_(class_name, bases): (member type, first enum))
            bool get_mixins(std::string const& aClassName, object* aBases, ref& aMemberType, ref& aFirstEnum);

            // (_find_data_type_)
            bool find_data_type(std::string const& aClassName, object* aBases, ref& aResult)
            {
                std::vector<object*> dataTypes;
                for (auto chain : as<tuple_object>(aBases)->items)
                {
                    object* candidate = nullptr;
                    ref const mro{ attribute(chain, "__mro__") };
                    if (!mro)
                        return false;
                    for (auto base : as<tuple_object>(mro.get())->items)
                    {
                        if (is_type_object(base) && type_qualified_name(base) == "object")
                            continue;
                        if (is_class(base) && s.enumType != nullptr && instance_of(base, s.enumType) == 1)
                        {
                            ref const memberType{ attribute(base, "_member_type_") };
                            if (memberType && !(is_type_object(memberType.get()) && type_qualified_name(memberType.get()) == "object"))
                            {
                                if (std::find(dataTypes.begin(), dataTypes.end(), memberType.get()) == dataTypes.end())
                                    dataTypes.push_back(memberType.get());
                                break;
                            }
                            clear_error();
                            continue;
                        }
                        // (a builtin type (e.g. int) has a __new__ of its own)
                        bool ownNew = is_type_object(base);
                        if (is_class(base))
                        {
                            ref const d{ attribute(base, "__dict__") };
                            ref const newKey{ make_str("__new__") };
                            ref const fieldsKey{ make_str("__dataclass_fields__") };
                            ownNew = d && (dict_find(proxied_mapping(d.get()), newKey.get()) >= 0 || dict_find(proxied_mapping(d.get()), fieldsKey.get()) >= 0);
                        }
                        if (ownNew)
                        {
                            auto const t = candidate != nullptr ? candidate : base;
                            if (std::find(dataTypes.begin(), dataTypes.end(), t) == dataTypes.end())
                                dataTypes.push_back(t);
                            break;
                        }
                        candidate = candidate != nullptr ? candidate : base;
                    }
                }
                if (dataTypes.size() > 1u)
                {
                    std::vector<object*> items;
                    for (auto d : dataTypes)
                        items.push_back(new_reference(d));
                    ref const list{ make_list(std::move(items)) };
                    ref const set{ b_set({ list.get() }, nullptr) };
                    return (raise("TypeError", "too many data types for " + repr_text(ref{ make_str(aClassName) }.get()) + ": " + repr_text(set.get())), false);
                }
                aResult = dataTypes.empty() ? ref{} : ref{ new_reference(dataTypes[0]) };
                return true;
            }

            bool get_mixins(std::string const& aClassName, object* aBases, ref& aMemberType, ref& aFirstEnum)
            {
                auto const& bases = as<tuple_object>(aBases)->items;
                if (bases.empty())
                {
                    aMemberType = ref{ type_object_for("object", "object") };
                    aFirstEnum = s.enumClass != nullptr ? ref{ new_reference(s.enumClass) } : ref{};
                    return true;
                }
                auto const first = bases.back();
                if (!is_class(first) || instance_of(first, s.enumType) != 1)
                    return (raise("TypeError", "new enumerations should be created as `EnumName([mixin_type, ...] [data_type,] enum_type)`"), false);
                ref dataType;
                if (!find_data_type(aClassName, aBases, dataType))
                    return false;
                aMemberType = dataType ? dataType : ref{ type_object_for("object", "object") };
                aFirstEnum = ref{ new_reference(first) };
                return true;
            }

            bool is_object_type(object* aType) { return is_type_object(aType) && type_qualified_name(aType) == "object"; }

            // (_check_for_existing_members_: an enumeration with members cannot be extended)
            bool check_for_existing_members(std::string const& aClassName, object* aBases)
            {
                for (auto chain : as<tuple_object>(aBases)->items)
                {
                    ref const mro{ attribute(chain, "__mro__") };
                    if (!mro)
                        return false;
                    for (auto base : as<tuple_object>(mro.get())->items)
                        if (is_class(base) && instance_of(base, s.enumType) == 1)
                        {
                            ref const names{ attribute(base, "_member_names_") };
                            if (names && length(names.get()) > 0)
                                return (raise("TypeError", "<enum " + repr_text(ref{ make_str(aClassName) }.get()) + "> cannot extend " + repr_text(base)), false);
                            clear_error();
                        }
                }
                return true;
            }

            // (_find_data_repr_)
            ref find_data_repr(object* aBases)
            {
                for (auto chain : as<tuple_object>(aBases)->items)
                {
                    ref const mro{ attribute(chain, "__mro__") };
                    if (!mro)
                    {
                        clear_error();
                        continue;
                    }
                    for (auto base : as<tuple_object>(mro.get())->items)
                    {
                        if (is_object_type(base))
                            continue;
                        if (is_class(base) && instance_of(base, s.enumType) == 1)
                            return attribute_or_none(base, "_value_repr_");
                        if (is_class(base))
                        {
                            ref const d{ attribute(base, "__dict__") };
                            if (!d)
                            {
                                clear_error();
                                continue;
                            }
                            if (auto const r = dict_item(proxied_mapping(d.get()), "__repr__"))
                            {
                                // (a dataclass's repr: its fields')
                                auto const params = dict_item(proxied_mapping(d.get()), "__dataclass_params__");
                                if (dict_item(proxied_mapping(d.get()), "__dataclass_fields__") != nullptr && params != nullptr && attribute_truth(params, "repr") == 1)
                                    return ref{ module_attribute("enum", "_dataclass_repr") };
                                return ref{ new_reference(r) };
                            }
                        }
                    }
                }
                return ref{ new_reference(none()) };
            }

            object* object_new() { return class_function("object", "__new__"); }

            // (_find_new_: the __new__ that makes members, whether it was the class's own, and whether it takes the members' values)
            bool find_new(object* aClassDict, object* aMemberType, object* aFirstEnum, ref& aNew, bool& aSaveNew, bool& aUseArgs)
            {
                ref const objectNew{ object_new() };
                ref theNew{ dict_item(aClassDict, "__new__") != nullptr ? new_reference(dict_item(aClassDict, "__new__")) : nullptr };
                if (theNew && wrapper_type_name(theNew.get()) != nullptr)
                    theNew = attribute(theNew.get(), "__func__");
                aSaveNew = aFirstEnum != nullptr && static_cast<bool>(theNew);
                bool builtinNew = false;
                if (!theNew)
                {
                    for (auto method : { "__new_member__", "__new__" })
                    {
                        for (auto possible : { aMemberType, aFirstEnum })
                        {
                            if (possible == nullptr)
                                continue;
                            ref target{ getattr(possible, method) };
                            if (!target)
                            {
                                clear_error();
                                continue;
                            }
                            if (is_none(target.get()) || target.get() == objectNew.get() || target.get() == s.enumNew)
                                continue;
                            theNew = target;
                            break;
                        }
                        if (theNew)
                            break;
                    }
                    if (!theNew)
                    {
                        theNew = ref{ new_reference(objectNew.get()) };
                        // (a builtin data type's __new__ (e.g. int.__new__) takes the members' values)
                        builtinNew = is_type_object(aMemberType) && !is_object_type(aMemberType);
                    }
                }
                aUseArgs = !(aFirstEnum == nullptr || theNew.get() == s.enumNew || (theNew.get() == objectNew.get() && !builtinNew));
                aNew = theNew;
                return true;
            }

            // (_add_member_(name, member): the member as the class's attribute (through enum.property if a base has a descriptor of its name))
            bool add_member(object* aClass, object* aName, object* aMember)
            {
                ref const memberMap{ class_dict(aClass, "_member_map_") };
                if (!memberMap)
                    return false;
                if (auto const existing = dict_item(memberMap.get(), aName))
                {
                    if (existing != aMember)
                        return (raise("NameError", repr_text(aName) + " is already bound: " + repr_text(existing)), false);
                    return true;
                }
                auto const name = text_of(aName);
                object* foundDescriptor = nullptr;
                std::string descriptorType;
                object* classType = nullptr;
                ref const mro{ attribute(aClass, "__mro__") };
                if (!mro)
                    return false;
                auto const& classes = as<tuple_object>(mro.get())->items;
                std::vector<ref> keep;
                for (std::size_t i = 1u; i < classes.size(); ++i)
                {
                    auto const base = classes[i];
                    if (!is_class(base))
                        continue;
                    ref const d{ attribute(base, "__dict__") };
                    if (!d)
                    {
                        clear_error();
                        continue;
                    }
                    auto const attr = dict_item(proxied_mapping(d.get()), name.c_str());
                    if (attr == nullptr)
                        continue;
                    keep.emplace_back(new_reference(attr));
                    auto const wrapper = wrapper_type_name(attr);
                    bool const isProperty = (wrapper != nullptr && std::strcmp(wrapper, "property") == 0) || (s.propertyClass != nullptr && instance_of(attr, s.propertyClass) == 1) ||
                        instance_of(attr, ref{ module_attribute("types", "DynamicClassAttribute") }.get()) == 1;
                    if (isProperty)
                    {
                        foundDescriptor = attr;
                        classType = base;
                        descriptorType = "enum";
                        break;
                    }
                    if (is_descriptor(attr))
                    {
                        foundDescriptor = attr;
                        if (descriptorType.empty())
                            descriptorType = "desc";
                        if (classType == nullptr)
                            classType = base;
                        continue;
                    }
                    descriptorType = "attr";
                    classType = base;
                }
                if (foundDescriptor != nullptr)
                {
                    // (a redirect: enum.property)
                    ref const redirect{ call(s.propertyClass, nullptr, nullptr) };
                    if (!redirect || setattr(redirect.get(), "member", aMember) < 0)
                        return false;
                    ref const setName{ call_method_named(redirect.get(), "__set_name__", { new_reference(aClass), new_reference(aName) }) };
                    if (!setName)
                        return false;
                    if (descriptorType == "enum" || descriptorType == "desc")
                        for (auto const& [field, source] : { std::pair{ "fget", "fget" }, std::pair{ "fset", "fset" }, std::pair{ "fdel", "fdel" } })
                        {
                            ref const v{ attribute_or_none(foundDescriptor, source) };
                            if (setattr(redirect.get(), field, v.get()) < 0)
                                return false;
                        }
                    ref const type{ make_str(descriptorType) };
                    if (setattr(redirect.get(), "_attr_type", type.get()) < 0 || setattr(redirect.get(), "_cls_type", classType != nullptr ? classType : none()) < 0 ||
                        class_setattr_raw(aClass, name.c_str(), redirect.get()) < 0)
                        return false;
                }
                else if (class_setattr_raw(aClass, name.c_str(), aMember) < 0)
                    return false;
                return dict_set(memberMap.get(), aName, aMember) >= 0;
            }

            // (a member: made by _new_member_ (as _proto_member.__set_name__ makes it))
            bool make_member(object* aClass, object* aName, object* aValue)
            {
                ref const memberType{ attribute(aClass, "_member_type_") };
                ref const newMember{ attribute(aClass, "_new_member_") };
                ref const useArgs{ attribute(aClass, "_use_args_") };
                ref const memberNames{ attribute(aClass, "_member_names_") };
                if (!memberType || !newMember || !useArgs || !memberNames)
                    return false;
                ref args{ kind_of(builtin_value(aValue)) == NEOS_PY_TUPLE ? new_reference(aValue) : make_tuple({ new_reference(aValue) }) };
                if (is_type_object(memberType.get()) && type_qualified_name(memberType.get()) == "tuple")
                    args = ref{ make_tuple({ new_reference(args.get()) }) };    // (a tuple enumeration: wrapped once more)
                auto const argItems = as<tuple_object>(args.get())->items;
                std::vector<object*> callArgs{ new_reference(aClass) };
                if (truth(useArgs.get()) == 1)
                    for (auto a : argItems)
                        callArgs.push_back(new_reference(a));
                ref const memberArgs{ make_tuple(std::move(callArgs)) };
                ref member{ call(newMember.get(), memberArgs.get(), nullptr) };
                if (!member)
                    return false;
                if (!has_attribute(member.get(), "_value_"))
                {
                    if (is_object_type(memberType.get()))
                    {
                        if (setattr(member.get(), "_value_", aValue) < 0)
                            return false;
                    }
                    else
                    {
                        ref const tupleArgs{ make_tuple(std::vector<object*>{}) };
                        for (auto a : argItems)
                            as<tuple_object>(tupleArgs.get())->items.push_back(new_reference(a));
                        ref const made{ call(memberType.get(), tupleArgs.get(), nullptr) };
                        if (!made)
                        {
                            ref const cause{ take_exception() };
                            ref const cls{ exception_class("TypeError") };
                            ref const e{ invoke(cls.get(), { make_str("_value_ not set in __new__, unable to create it") }) };
                            if (e)
                            {
                                setattr(e.get(), "__cause__", cause.get());
                                raise_exception(e.get());
                            }
                            return false;
                        }
                        if (setattr(member.get(), "_value_", made.get()) < 0)
                            return false;
                    }
                }
                ref const value{ attribute(member.get(), "_value_") };
                if (!value || setattr(member.get(), "_name_", aName) < 0 || setattr(member.get(), "__objclass__", aClass) < 0)
                    return false;
                {
                    ref const init{ attribute(member.get(), "__init__") };
                    ref const r{ init ? call(init.get(), args.get(), nullptr) : nullptr };
                    if (!r)
                        return false;
                }
                ref const order{ make_int(length(memberNames.get())) };
                if (setattr(member.get(), "_sort_order_", order.get()) < 0)
                    return false;
                bool const isFlag = s.flag != nullptr && subclass_of(aClass, s.flag);
                if (isFlag && is_int_like(builtin_value(value.get())))
                {
                    ref const mask{ attribute(aClass, "_flag_mask_") };
                    ref const newMask{ mask ? binary(NEOS_PY_OR, mask.get(), value.get(), false) : nullptr };
                    if (!newMask || class_setattr_raw(aClass, "_flag_mask_", newMask.get()) < 0)
                        return false;
                    if (is_single_bit(builtin_value(value.get())))
                    {
                        ref const singles{ attribute(aClass, "_singles_mask_") };
                        ref const newSingles{ singles ? binary(NEOS_PY_OR, singles.get(), value.get(), false) : nullptr };
                        if (!newSingles || class_setattr_raw(aClass, "_singles_mask_", newSingles.get()) < 0)
                            return false;
                    }
                    ref const bits{ call_method_named(newMask.get(), "bit_length", {}) };
                    ref const two{ make_int(2) };
                    ref const power{ bits ? binary(NEOS_PY_POWER, two.get(), bits.get(), false) : nullptr };
                    ref const one{ make_int(1) };
                    ref const all{ power ? binary(NEOS_PY_SUBTRACT, power.get(), one.get(), false) : nullptr };
                    if (!all || class_setattr_raw(aClass, "_all_bits_", all.get()) < 0)
                        return false;
                }
                // (an alias: the member with the same value)
                ref const value2member{ attribute(aClass, "_value2member_map_") };
                ref const memberMap{ attribute(aClass, "_member_map_") };
                if (!value2member || !memberMap)
                    return false;
                int found = 0;
                object* canonical = dict_item(value2member.get(), value.get(), &found);
                if (found == -2)
                {
                    clear_error();
                    for (auto const& item : as<dict_object>(memberMap.get())->items)
                    {
                        ref const v{ attribute(item.second, "_value_") };
                        if (v && equal(v.get(), value.get()) == 1)
                        {
                            canonical = item.second;
                            break;
                        }
                        clear_error();
                    }
                }
                if (canonical != nullptr)
                    member = ref{ new_reference(canonical) };
                else if (!isFlag || (is_int_like(builtin_value(value.get())) && is_single_bit(builtin_value(value.get()))))
                {
                    if (!list_append(memberNames.get(), aName))
                        return false;
                }
                if (!add_member(aClass, aName, member.get()))
                    return false;
                // (the member's value: hashable or not)
                long long h = 0;
                if (hash_of(value.get(), h))
                {
                    if (dict_find(value2member.get(), value.get()) < 0 && dict_set(value2member.get(), value.get(), member.get()) < 0)
                        return false;
                    ref const hashable{ attribute(aClass, "_hashable_values_") };
                    if (!hashable)
                        return false;
                    auto const present = contains(hashable.get(), value.get());
                    if (present < 0 || (present == 0 && !list_append(hashable.get(), value.get())))
                        return false;
                }
                else
                {
                    clear_error();
                    ref const unhashable{ attribute(aClass, "_unhashable_values_") };
                    ref const unhashableMap{ attribute(aClass, "_unhashable_values_map_") };
                    if (!unhashable || !unhashableMap || !list_append(unhashable.get(), value.get()))
                        return false;
                    object* list = dict_item(unhashableMap.get(), aName);
                    if (list == nullptr)
                    {
                        ref const l{ make_list() };
                        if (dict_set(unhashableMap.get(), aName, l.get()) < 0)
                            return false;
                        list = dict_item(unhashableMap.get(), aName);
                    }
                    if (!list_append(list, value.get()))
                        return false;
                }
                return true;
            }

            // (data types' methods for ReprEnum: str() and format() of the member's value; hash() of mixed-in builtin types)
            builtin_entry const sDataStr{ "__str__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    auto const value = builtin_value(aArgs[0]);
                    if (kind_of(value) == NEOS_PY_STR)
                        return new_reference(value);
                    std::string text;
                    return repr_of(value, text) ? make_str(text) : nullptr;
                } };
            builtin_entry const sDataFormat{ "__format__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    if (aArgs.size() != 2u)
                        return raise("TypeError", "__format__ expected 1 argument");
                    ref const format{ fn_builtin("format") };
                    return format ? invoke(format.get(), { new_reference(builtin_value(aArgs[0])), new_reference(aArgs[1]) }) : nullptr;
                } };
            builtin_entry const sDataHash{ "__hash__", [](std::vector<object*> const& aArgs, object*) -> object*
                {
                    long long h = 0;
                    return hash_of(builtin_value(aArgs[0]), h) ? make_int(h) : nullptr;
                } };

            bool own_attribute(object* aClass, char const* aName)
            {
                ref const d{ attribute(aClass, "__dict__") };
                if (!d)
                {
                    clear_error();
                    return false;
                }
                return dict_item(proxied_mapping(d.get()), aName) != nullptr;
            }

            // (EnumType.__new__(metacls, cls, bases, classdict, *, boundary=None, _simple=False, **kwds))
            object* enum_type_new(std::vector<object*> const& aArgs, object* aKwargs)
            {
                if (aArgs.size() != 4u)
                    return raise("TypeError", "EnumType.__new__() takes 4 positional arguments");
                auto const metacls = aArgs[0];
                auto const clsName = aArgs[1];
                auto const bases = aArgs[2];
                auto const enumDict = aArgs[3];
                ref kwds{ make_dict() };
                if (aKwargs != nullptr && !dict_update(kwds.get(), aKwargs))
                    return nullptr;
                ref boundary{ dict_item(kwds.get(), "boundary") != nullptr ? new_reference(dict_item(kwds.get(), "boundary")) : new_reference(none()) };
                dict_remove(kwds.get(), "boundary");
                bool const simple = dict_item(kwds.get(), "_simple") != nullptr && truth(dict_item(kwds.get(), "_simple")) == 1;
                dict_remove(kwds.get(), "_simple");
                if (kind_of(clsName) != NEOS_PY_STR || kind_of(bases) != NEOS_PY_TUPLE)
                    return raise("TypeError", "EnumType.__new__() argument types");
                auto const name = text_of(clsName);
                if (simple)
                {
                    ref const args{ make_tuple({ new_reference(metacls), new_reference(clsName), new_reference(bases), new_reference(enumDict) }) };
                    return fn_type_new(nullptr, nullptr, args.get(), kwds.get());
                }
                auto const storage = builtin_value(enumDict);
                if (kind_of(storage) != NEOS_PY_DICT)
                    return raise("TypeError", "EnumType.__new__() argument 4 must be an EnumDict");
                // (_ignore_: its names are removed)
                {
                    ref const ignoreKey{ make_str("_ignore_") };
                    if (dict_find(storage, ignoreKey.get()) < 0)
                    {
                        ref const empty{ make_list() };
                        dict_set(storage, ignoreKey.get(), empty.get());
                    }
                    ref const ignore{ getitem(storage, ignoreKey.get()) };
                    if (!ignore || !list_append(ignore.get(), ignoreKey.get()))
                        return nullptr;
                    std::vector<object*> names;
                    if (!collect(ignore.get(), names))
                        return nullptr;
                    for (auto n : names)
                    {
                        ref const key{ n };
                        if (auto const at = dict_find(storage, n); at >= 0)
                            dict_erase(*as<dict_object>(storage), static_cast<std::size_t>(at));
                        clear_error();
                    }
                }
                ref memberNamesDict{ attribute(enumDict, "_member_names") };
                if (!memberNamesDict)
                {
                    clear_error();
                    memberNamesDict = ref{ make_dict() };
                }
                std::vector<ref> memberNames;
                for (auto const& item : as<dict_object>(memberNamesDict.get())->items)
                    memberNames.emplace_back(new_reference(item.first));
                {
                    std::vector<std::string> invalid;
                    for (auto const& n : memberNames)
                        if (text_of(n.get()) == "mro" || text_of(n.get()).empty())
                            invalid.push_back(repr_text(n.get()));
                    if (!invalid.empty())
                    {
                        std::string list;
                        for (std::size_t i = 0u; i < invalid.size(); ++i)
                            list += (i == 0u ? "" : ",") + invalid[i];
                        return raise("ValueError", "invalid enum member name(s) " + list);
                    }
                }
                ref order{ dict_item(storage, "_order_") != nullptr ? new_reference(dict_item(storage, "_order_")) : nullptr };
                dict_remove(storage, "_order_");
                ref gnv{ dict_item(storage, "_generate_next_value_") != nullptr ? new_reference(dict_item(storage, "_generate_next_value_")) : nullptr };
                if (gnv && !is_none(gnv.get()) && (wrapper_type_name(gnv.get()) == nullptr || std::strcmp(wrapper_type_name(gnv.get()), "staticmethod") != 0))
                    gnv = ref{ b_staticmethod({ gnv.get() }, nullptr) };
                // (the class's namespace: a dict (without the members' values (they become members)))
                ref const classdict{ make_dict() };
                std::vector<std::pair<ref, ref>> memberValues;
                for (auto const& item : as<dict_object>(storage)->items)
                {
                    if (dict_find(memberNamesDict.get(), item.first) >= 0)
                        memberValues.emplace_back(ref{ new_reference(item.first) }, ref{ new_reference(item.second) });
                    else
                        dict_set(classdict.get(), item.first, item.second);
                }
                // (the members, in the order of their names)
                std::vector<std::pair<ref, ref>> orderedMembers;
                for (auto const& n : memberNames)
                    for (auto const& m : memberValues)
                        if (equal(m.first.get(), n.get()) == 1)
                            orderedMembers.push_back(m);
                if (gnv && !is_none(gnv.get()))
                    dict_put(classdict.get(), "_generate_next_value_", gnv.get());
                ref memberType, firstEnum;
                if (!get_mixins(name, bases, memberType, firstEnum))
                    return nullptr;
                ref theNew;
                bool saveNew = false, useArgs = false;
                if (!find_new(classdict.get(), memberType.get(), firstEnum ? firstEnum.get() : nullptr, theNew, saveNew, useArgs))
                    return nullptr;
                ref const useArgsValue{ make_bool(useArgs) };
                ref const valueRepr{ find_data_repr(bases) };
                ref const flagZero{ make_int(0) };
                ref const boundaryValue{ !is_none(boundary.get()) ? new_reference(boundary.get()) : firstEnum ? attribute_or_none(firstEnum.get(), "_boundary_").release_ownership() :
                    new_reference(none()) };
                bool ok = dict_put(classdict.get(), "_new_member_", theNew.get()) && dict_put(classdict.get(), "_use_args_", useArgsValue.get()) &&
                    dict_put(classdict.get(), "_member_names_", ref{ make_list() }.get()) && dict_put(classdict.get(), "_member_map_", ref{ make_dict() }.get()) &&
                    dict_put(classdict.get(), "_value2member_map_", ref{ make_dict() }.get()) && dict_put(classdict.get(), "_hashable_values_", ref{ make_list() }.get()) &&
                    dict_put(classdict.get(), "_unhashable_values_", ref{ make_list() }.get()) && dict_put(classdict.get(), "_unhashable_values_map_", ref{ make_dict() }.get()) &&
                    dict_put(classdict.get(), "_member_type_", memberType.get()) && dict_put(classdict.get(), "_value_repr_", valueRepr.get()) &&
                    dict_put(classdict.get(), "_boundary_", boundaryValue.get()) && dict_put(classdict.get(), "_flag_mask_", flagZero.get()) &&
                    dict_put(classdict.get(), "_singles_mask_", flagZero.get()) && dict_put(classdict.get(), "_all_bits_", flagZero.get()) &&
                    dict_put(classdict.get(), "_inverted_", none());
                if (!ok)
                    return nullptr;
                // (a mixed-in builtin type's hash (e.g. int's): its value's, as the type is before Enum in the MRO)
                if (is_type_object(memberType.get()) && !is_object_type(memberType.get()) && dict_item(classdict.get(), "__hash__") == nullptr)
                    dict_put(classdict.get(), "__hash__", ref{ make_builtin(sDataHash) }.get());
                auto const inProgress = "_" + name + "__in_progress";
                dict_put(classdict.get(), inProgress.c_str(), ref{ make_bool(true) }.get());
                ref enumClass;
                {
                    ref const args{ make_tuple({ new_reference(metacls), new_reference(clsName), new_reference(bases), new_reference(classdict.get()) }) };
                    enumClass = ref{ fn_type_new(nullptr, nullptr, args.get(), kwds.get()) };
                    if (!enumClass)
                        return nullptr;
                }
                // (the members)
                for (auto const& m : orderedMembers)
                    if (!make_member(enumClass.get(), m.first.get(), m.second.get()))
                        return nullptr;
                class_setattr_raw(enumClass.get(), inProgress.c_str(), nullptr);
                clear_error();
                // (ReprEnum: the data type's str() and format())
                if (s.reprEnum != nullptr && std::find(as<tuple_object>(bases)->items.begin(), as<tuple_object>(bases)->items.end(), s.reprEnum) != as<tuple_object>(bases)->items.end())
                {
                    if (is_object_type(memberType.get()))
                        return raise("TypeError", "ReprEnum subclasses must be mixed with a data type (i.e. int, str, float, etc.)");
                    for (auto const* m : { &sDataFormat, &sDataStr })
                        if (!own_attribute(enumClass.get(), m->name))
                        {
                            ref method;
                            if (is_type_object(memberType.get()))
                                method = ref{ make_builtin(*m) };
                            else
                            {
                                method = attribute(memberType.get(), m->name);
                                if (method && std::strcmp(m->name, "__str__") == 0)
                                    if (ref const objectStr{ class_function("object", "__str__") }; method.get() == objectStr.get())
                                        method = attribute(memberType.get(), "__repr__");
                            }
                            if (!method || class_setattr_raw(enumClass.get(), m->name, method.get()) < 0)
                                return nullptr;
                        }
                }
                // (the first enumeration's repr, str, format and reduce_ex, unless the class has its own or a data type's)
                if (firstEnum)
                    for (auto method : { "__repr__", "__str__", "__format__", "__reduce_ex__" })
                    {
                        if (own_attribute(enumClass.get(), method) || (dict_item(classdict.get(), method) != nullptr))
                            continue;
                        ref const enumMethod{ attribute(firstEnum.get(), method) };
                        ref const found{ attribute(enumClass.get(), method) };
                        ref const objectMethod{ class_function("object", method) };
                        ref const dataMethod{ attribute_or_none(memberType.get(), method) };
                        if (!enumMethod || !found)
                            return nullptr;
                        if (found.get() == dataMethod.get() || found.get() == objectMethod.get())
                            if (class_setattr_raw(enumClass.get(), method, enumMethod.get()) < 0)
                                return nullptr;
                    }
                bool const isFlag = s.flag != nullptr && subclass_of(enumClass.get(), s.flag);
                if (isFlag)
                    for (auto method : { "__or__", "__and__", "__xor__", "__ror__", "__rand__", "__rxor__", "__invert__" })
                        if (dict_item(classdict.get(), method) == nullptr)
                        {
                            ref const flagMethod{ attribute(s.flag, method) };
                            if (!flagMethod || class_setattr_raw(enumClass.get(), method, flagMethod.get()) < 0)
                                return nullptr;
                        }
                if (s.enumClass != nullptr)
                {
                    if (saveNew && class_setattr_raw(enumClass.get(), "__new_member__", theNew.get()) < 0)
                        return nullptr;
                    if (class_setattr_raw(enumClass.get(), "__new__", s.enumNew) < 0)
                        return nullptr;
                }
                // (_order_: a list of names)
                ref orderNames;
                if (order && !is_none(order.get()))
                {
                    std::vector<object*> names;
                    if (!names_of(order.get(), names))
                        return nullptr;
                    orderNames = ref{ make_list(std::move(names)) };
                }
                if ((s.flag == nullptr && name != "Flag") || (s.flag != nullptr && !isFlag))
                {
                    for (auto attr : { "_boundary_", "_flag_mask_", "_singles_mask_", "_all_bits_", "_inverted_" })
                        class_setattr_raw(enumClass.get(), attr, nullptr);
                    clear_error();
                }
                else if (isFlag)
                {
                    // (members defined out of order: iterated in definition order)
                    std::vector<object*> values;
                    ref const it{ iter(enumClass.get()) };
                    if (!it)
                        return nullptr;
                    for (;;)
                    {
                        ref const m{ next(it.get()) };
                        if (!m)
                            break;
                        values.push_back(getattr(m.get(), "_value_"));
                    }
                    if (failed())
                        return (std::for_each(values.begin(), values.end(), release), nullptr);
                    ref const list{ make_list(std::move(values)) };
                    ref const sorted{ b_sorted({ list.get() }, nullptr) };
                    if (!sorted)
                        clear_error();
                    else if (equal(list.get(), sorted.get()) != 1)
                    {
                        ref const byDef{ attribute(enumClass.get(), "_iter_member_by_def_") };
                        ref const d{ attribute(s.flag, "__dict__") };
                        ref const raw{ d ? new_reference(dict_item(proxied_mapping(d.get()), "_iter_member_by_def_")) : nullptr };
                        if (!raw || class_setattr_raw(enumClass.get(), "_iter_member_", raw.get()) < 0)
                            return nullptr;
                    }
                    if (orderNames)
                    {
                        ref const memberMap{ attribute(enumClass.get(), "_member_map_") };
                        std::vector<object*> kept;
                        for (auto o : as<list_object>(orderNames.get())->items)
                        {
                            auto const m = dict_item(memberMap.get(), o);
                            ref const v{ m != nullptr ? attribute(m, "_value_") : ref{} };
                            if (m == nullptr || (v && is_single_bit(builtin_value(v.get()))))
                                kept.push_back(new_reference(o));
                        }
                        orderNames = ref{ make_list(std::move(kept)) };
                    }
                }
                if (orderNames && length(orderNames.get()) > 0)
                {
                    ref const memberMap{ attribute(enumClass.get(), "_member_map_") };
                    ref const names{ attribute(enumClass.get(), "_member_names_") };
                    std::vector<object*> kept;
                    for (auto o : as<list_object>(orderNames.get())->items)
                        if (dict_item(memberMap.get(), o) == nullptr || contains(names.get(), o) == 1)
                            kept.push_back(new_reference(o));
                    ref const filtered{ make_list(std::move(kept)) };
                    if (equal(filtered.get(), names.get()) != 1)
                        return raise("TypeError", "member order does not match _order_:\n  " + repr_text(names.get()) + "\n  " + repr_text(filtered.get()));
                }
                return enumClass.release_ownership();
            }

            object* enum_prepare(std::vector<object*> const& aArgs, object* aKwargs)
            {
                // (metacls, cls, bases, **kwds)
                if (aArgs.size() != 3u)
                    return raise("TypeError", "__prepare__() takes 3 positional arguments");
                (void)aKwargs;
                auto const name = text_of(aArgs[1]);
                ref const bases{ kind_of(aArgs[2]) == NEOS_PY_TUPLE ? new_reference(aArgs[2]) : fn_tuple_of(aArgs[2]) };
                if (!bases || !check_for_existing_members(name, bases.get()))
                    return nullptr;
                ref enumDict{ invoke(s.enumDict, { new_reference(aArgs[1]) }) };
                if (!enumDict)
                    return nullptr;
                ref memberType, firstEnum;
                if (!get_mixins(name, bases.get(), memberType, firstEnum))
                    return nullptr;
                if (firstEnum)
                {
                    ref const gnv{ attribute_or_none(firstEnum.get(), "_generate_next_value_") };
                    ref const r{ call_method_named(enumDict.get(), "__setitem__", { make_str("_generate_next_value_"), new_reference(gnv.get()) }) };
                    if (!r)
                        return nullptr;
                }
                return enumDict.release_ownership();
            }

            // (the members of an enumeration, in definition order: a list)
            object* members_of(object* aClass)
            {
                ref const names{ attribute(aClass, "_member_names_") };
                ref const map{ attribute(aClass, "_member_map_") };
                if (!names || !map)
                    return nullptr;
                std::vector<object*> members;
                for (auto n : as<list_object>(names.get())->items)
                    if (auto const m = dict_item(map.get(), n))
                        members.push_back(new_reference(m));
                return make_list(std::move(members));
            }

            object* create_enum(object* aClass, object* aClassName, object* aNames, object* aModule, object* aQualname, object* aType, object* aStart, object* aBoundary);

            // (EnumType._convert_ (enum.py's): an enumeration of cls's kind of the constants of a module (or of source) whose names pass filter, sorted
            // by value (or, if their values cannot be ordered, by name); its members are exported to the module (or, as_global, by global_enum) and
            // it is the module's variable name)
            object* enum_convert(std::vector<object*> const& aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!self_arguments("_convert_", aArgs, aKwargs, { "name", "module", "filter", "source", "boundary", "as_global" }, 3u, 0u, a))
                    return nullptr;
                auto const cls = aArgs[0];
                ref const modules{ module_attribute("sys", "modules") };
                ref const module{ modules ? getitem(modules.get(), a[1]) : nullptr };
                ref const moduleGlobals{ module ? getattr(module.get(), "__dict__") : nullptr };
                if (!moduleGlobals)
                    return nullptr;
                ref source;
                if (a[3] != nullptr && truth(a[3]) == 1)
                    source = ref{ getattr(a[3], "__dict__") };
                else
                    source = ref{ (retain(moduleGlobals.get()), moduleGlobals.get()) };
                ref const items{ source ? call_method_named(source.get(), "items", {}) : nullptr };
                std::vector<object*> pairs;
                if (!items || !collect(items.get(), pairs))
                    return nullptr;
                std::vector<ref> members;
                for (auto p : pairs)
                {
                    ref const pair{ p };
                    auto const& item = tuple_items(pair.get());
                    ref const filterArgs{ make_tuple({ (retain(item[0]), item[0]) }) };
                    ref const passed{ call(a[2], filterArgs.get(), nullptr) };
                    if (!passed)
                        return nullptr;
                    auto const t = truth(passed.get());
                    if (t < 0)
                        return nullptr;
                    if (t == 1)
                        members.emplace_back(make_tuple({ (retain(item[0]), item[0]), (retain(item[1]), item[1]) }));
                }
                // (sort by value (then name); unless some values aren't comparable, in which case sort by name)
                bool comparable = true;
                ref pending;    ///< (an exception (not TypeError) that a comparison raised)
                auto const less = [&](ref const& aLhs, ref const& aRhs, bool aByValue) -> bool
                {
                    if (!comparable && aByValue)
                        return false;
                    auto const& l = tuple_items(aLhs.get());
                    auto const& r = tuple_items(aRhs.get());
                    if (aByValue)
                    {
                        ref const lt{ compare(NEOS_PY_LT, l[1], r[1]) };
                        ref const gt{ lt ? compare(NEOS_PY_LT, r[1], l[1]) : nullptr };
                        if (!lt || !gt)
                        {
                            ref e{ take_exception() };
                            if (!exception_is(e.get(), "TypeError"))
                                pending = std::move(e);
                            comparable = false;
                            return false;
                        }
                        if (truth(lt.get()) == 1)
                            return true;
                        if (truth(gt.get()) == 1)
                            return false;
                    }
                    ref const nameLt{ compare(NEOS_PY_LT, l[0], r[0]) };
                    return nameLt && truth(nameLt.get()) == 1;
                };
                auto sorted = members;
                std::stable_sort(sorted.begin(), sorted.end(), [&](ref const& aLhs, ref const& aRhs) { return less(aLhs, aRhs, true); });
                if (pending)
                {
                    reraise_exception(pending.get());
                    return nullptr;
                }
                if (!comparable)
                {
                    sorted = members;
                    std::stable_sort(sorted.begin(), sorted.end(), [&](ref const& aLhs, ref const& aRhs) { return less(aLhs, aRhs, false); });
                }
                if (failed())
                    return nullptr;
                std::vector<object*> names;
                for (auto const& m : sorted)
                    names.push_back((retain(m.get()), m.get()));
                ref const nameList{ make_list(std::move(names)) };
                ref result{ create_enum(cls, a[0], nameList.get(), a[1], nullptr, nullptr, nullptr, a[4] != nullptr && truth(a[4]) == 1 ? a[4] : s.keep) };
                if (!result)
                    return nullptr;
                if (a[5] != nullptr && truth(a[5]) == 1)
                {
                    ref const globalEnum{ module_attribute("enum", "global_enum") };
                    ref const globalArgs{ make_tuple({ (retain(result.get()), result.get()) }) };
                    if (!globalEnum || !ref{ call(globalEnum.get(), globalArgs.get(), nullptr) })
                        return nullptr;
                }
                else
                {
                    ref const resultModule{ getattr(result.get(), "__module__") };
                    ref const target{ resultModule ? getitem(modules.get(), resultModule.get()) : nullptr };
                    ref const targetDict{ target ? getattr(target.get(), "__dict__") : nullptr };
                    ref const memberMap{ targetDict ? getattr(result.get(), "__members__") : nullptr };
                    if (!memberMap || !ref{ call_method_named(targetDict.get(), "update", { (retain(memberMap.get()), memberMap.get()) }) })
                        return nullptr;
                }
                if (setitem(moduleGlobals.get(), a[0], result.get()) < 0)
                    return nullptr;
                return result.release_ownership();
            }

            builtin_entry const sEnumTypeMethods[] =
            {
                { "__new__", enum_type_new },
                { "__bool__", [](std::vector<object*> const&, object*) -> object* { return make_bool(true); } },
                { "__call__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        // (cls, value, names=_not_given, *values, module=None, qualname=None, type=None, start=1, boundary=None)
                        if (aArgs.size() < 2u)
                            return raise("TypeError", "EnumType.__call__() missing 1 required positional argument: 'value'");
                        auto const cls = aArgs[0];
                        object* module = nullptr;
                        object* qualname = nullptr;
                        object* type = nullptr;
                        object* start = nullptr;
                        object* boundary = nullptr;
                        object* namesKeyword = nullptr;
                        if (aKwargs != nullptr)
                            for (auto const& item : as<dict_object>(aKwargs)->items)
                            {
                                auto const k = text_of(item.first);
                                if (k == "module") module = item.second;
                                else if (k == "qualname") qualname = item.second;
                                else if (k == "type") type = item.second;
                                else if (k == "start") start = item.second;
                                else if (k == "boundary") boundary = item.second;
                                else if (k == "names") namesKeyword = item.second;
                                else
                                    return raise("TypeError", "EnumType.__call__() got an unexpected keyword argument '" + k + "'");
                            }
                        object* names = aArgs.size() > 2u ? aArgs[2] : namesKeyword;
                        ref const memberMap{ attribute(cls, "_member_map_") };
                        if (!memberMap)
                            return nullptr;
                        if (length(memberMap.get()) > 0)
                        {
                            ref value{ new_reference(aArgs[1]) };
                            if (names != nullptr)
                            {
                                std::vector<object*> all{ new_reference(aArgs[1]), new_reference(names) };
                                for (std::size_t i = 3u; i < aArgs.size(); ++i)
                                    all.push_back(new_reference(aArgs[i]));
                                value = ref{ make_tuple(std::move(all)) };
                            }
                            ref const theNew{ attribute(cls, "__new__") };
                            return theNew ? invoke(theNew.get(), { new_reference(cls), new_reference(value.get()) }) : nullptr;
                        }
                        if (names == nullptr && type == nullptr)
                            return raise("TypeError", repr_text(cls) + " has no members; specify `names=()` if you meant to create a new, empty, enum");
                        return create_enum(cls, aArgs[1], names, module, qualname, type, start, boundary);
                    } },
                { "__contains__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return raise("TypeError", "__contains__ expected 1 argument");
                        auto const cls = aArgs[0];
                        auto const value = aArgs[1];
                        auto const is = instance_of(value, cls);
                        if (is < 0)
                            return nullptr;
                        if (is == 1)
                            return make_bool(true);
                        if (s.flag != nullptr && subclass_of(cls, s.flag))
                        {
                            ref const missing{ attribute(cls, "_missing_") };
                            ref const result{ missing ? invoke(missing.get(), { new_reference(value) }) : nullptr };
                            if (result)
                                return make_bool(instance_of(result.get(), cls) == 1);
                            ref const e{ take_exception() };
                            if (e && !exception_is(e.get(), "ValueError"))
                            {
                                raise_exception(e.get());
                                return nullptr;
                            }
                        }
                        ref const unhashable{ attribute(cls, "_unhashable_values_") };
                        ref const hashable{ attribute(cls, "_hashable_values_") };
                        if (!unhashable || !hashable)
                            return nullptr;
                        auto r = contains(unhashable.get(), value);
                        if (r == 0)
                            r = contains(hashable.get(), value);
                        return r < 0 ? nullptr : make_bool(r == 1);
                    } },
                { "__delattr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return raise("TypeError", "__delattr__ expected 1 argument");
                        ref const memberMap{ attribute(aArgs[0], "_member_map_") };
                        if (memberMap && dict_item(memberMap.get(), aArgs[1]) != nullptr)
                            return raise("AttributeError", repr_text(ref{ make_str(class_name(aArgs[0])) }.get()) + " cannot delete member " + repr_text(aArgs[1]) + ".");
                        clear_error();
                        return class_setattr_raw(aArgs[0], text_of(aArgs[1]).c_str(), nullptr) < 0 ? nullptr : new_reference(none());
                    } },
                { "__setattr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 3u)
                            return raise("TypeError", "__setattr__ expected 2 arguments");
                        // (the class's own members (not its bases'))
                        ref const d{ attribute(aArgs[0], "__dict__") };
                        auto const memberMap = d ? dict_item(proxied_mapping(d.get()), "_member_map_") : nullptr;
                        clear_error();
                        if (memberMap != nullptr && kind_of(memberMap) == NEOS_PY_DICT && dict_item(memberMap, aArgs[1]) != nullptr)
                            return raise("AttributeError", "cannot reassign member " + repr_text(aArgs[1]));
                        return class_setattr_raw(aArgs[0], text_of(aArgs[1]).c_str(), aArgs[2]) < 0 ? nullptr : new_reference(none());
                    } },
                { "__dir__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.empty())
                            return raise("TypeError", "EnumType.__dir__() missing 1 required positional argument: 'cls'");
                        auto const cls = aArgs[0];
                        std::set<std::string> interesting = { "__class__", "__contains__", "__doc__", "__getitem__", "__iter__", "__len__", "__members__", "__module__",
                            "__name__", "__qualname__" };
                        ref const names{ attribute(cls, "_member_names_") };
                        if (names)
                            for (auto n : as<list_object>(names.get())->items)
                                interesting.insert(text_of(n));
                        ref const newMember{ attribute_or_none(cls, "_new_member_") };
                        ref const objectNew{ object_new() };
                        if (newMember.get() != objectNew.get())
                            interesting.insert("__new__");
                        ref const memberType{ attribute(cls, "_member_type_") };
                        if (memberType && !is_object_type(memberType.get()))
                        {
                            ref const d{ b_dir({ memberType.get() }, nullptr) };
                            if (d)
                                for (auto n : as<list_object>(d.get())->items)
                                    interesting.insert(text_of(n));
                        }
                        clear_error();
                        std::vector<object*> result;
                        for (auto const& n : interesting)
                            result.push_back(make_str(n));
                        return make_list(std::move(result));
                    } },
                { "__getitem__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return raise("TypeError", "__getitem__ expected 1 argument");
                        ref const map{ attribute(aArgs[0], "_member_map_") };
                        return map ? getitem(map.get(), aArgs[1]) : nullptr;
                    } },
                { "__iter__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const members{ members_of(aArgs[0]) };
                        return members ? iter(members.get()) : nullptr;
                    } },
                { "__len__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const names{ attribute(aArgs[0], "_member_names_") };
                        return names ? make_int(length(names.get())) : nullptr;
                    } },
                { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const cls = aArgs[0];
                        bool const isFlag = s.flag != nullptr && subclass_of(cls, s.flag);
                        return make_str(std::string{ isFlag ? "<flag " : "<enum " } + repr_text(ref{ make_str(class_name(cls)) }.get()) + ">");
                    } },
                { "__reversed__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const members{ members_of(aArgs[0]) };
                        if (!members)
                            return nullptr;
                        auto& items = as<list_object>(members.get())->items;
                        std::reverse(items.begin(), items.end());
                        return iter(members.get());
                    } },
                { "_create_", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("_create_", aArgs, aKwargs, { "class_name", "names", "module", "qualname", "type", "start", "boundary" }, 2u, 0u, a))
                            return nullptr;
                        return create_enum(aArgs[0], a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
                    } },
                { "_add_member_", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 3u)
                            return raise("TypeError", "_add_member_() takes 2 positional arguments");
                        return add_member(aArgs[0], aArgs[1], aArgs[2]) ? new_reference(none()) : nullptr;
                    } },
                { "_convert_", enum_convert }
            };

            builtin_entry const sEnumTypeClassMethods[] =
            {
                { "__prepare__", enum_prepare },
                { "_check_for_existing_members_", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 3u)
                            return raise("TypeError", "_check_for_existing_members_() takes 2 positional arguments");
                        return check_for_existing_members(text_of(aArgs[1]), aArgs[2]) ? new_reference(none()) : nullptr;
                    } },
                { "_get_mixins_", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 3u)
                            return raise("TypeError", "_get_mixins_() takes 2 positional arguments");
                        ref const bases{ fn_tuple_of(aArgs[2]) };
                        ref memberType, firstEnum;
                        if (!bases || !get_mixins(text_of(aArgs[1]), bases.get(), memberType, firstEnum))
                            return nullptr;
                        return make_tuple({ new_reference(memberType.get()), new_reference(firstEnum ? firstEnum.get() : none()) });
                    } },
                { "_find_data_type_", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 3u)
                            return raise("TypeError", "_find_data_type_() takes 2 positional arguments");
                        ref const bases{ fn_tuple_of(aArgs[2]) };
                        ref result;
                        if (!bases || !find_data_type(text_of(aArgs[1]), bases.get(), result))
                            return nullptr;
                        return new_reference(result ? result.get() : none());
                    } },
                { "_find_data_repr_", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 3u)
                            return raise("TypeError", "_find_data_repr_() takes 2 positional arguments");
                        ref const bases{ fn_tuple_of(aArgs[2]) };
                        return bases ? find_data_repr(bases.get()).release_ownership() : nullptr;
                    } }
            };

            // (the members of a functional API's names: a str of names, a sequence of names, of (name, value) pairs, or a mapping)
            object* create_enum(object* aClass, object* aClassName, object* aNames, object* aModule, object* aQualname, object* aType, object* aStart, object* aBoundary)
            {
                ref const metacls{ class_of(aClass) };
                ref const bases{ aType == nullptr || is_none(aType) ? make_tuple({ new_reference(aClass) }) : make_tuple({ new_reference(aType), new_reference(aClass) }) };
                ref memberType, firstEnum;
                if (!get_mixins(text_of(aClassName), bases.get(), memberType, firstEnum))
                    return nullptr;
                ref const prepare{ attribute(metacls.get(), "__prepare__") };
                ref const classdict{ prepare ? invoke(prepare.get(), { new_reference(aClassName), new_reference(bases.get()) }) : nullptr };
                if (!classdict)
                    return nullptr;
                ref names{ aNames != nullptr ? new_reference(aNames) : new_reference(none()) };
                if (auto const text = str_of_value(names.get()))
                {
                    std::vector<object*> list;
                    if (!names_of(names.get(), list))
                        return nullptr;
                    names = ref{ make_list(std::move(list)) };
                }
                auto const sequence = [](object* aValue) { auto const k = kind_of(builtin_value(aValue)); return k == NEOS_PY_LIST || k == NEOS_PY_TUPLE; };
                if (sequence(names.get()) && length(names.get()) > 0)
                {
                    ref const first{ getitem(names.get(), ref{ make_int(0) }.get()) };
                    if (first && kind_of(first.get()) == NEOS_PY_STR)
                    {
                        std::vector<object*> items;
                        if (!collect(names.get(), items))
                            return nullptr;
                        ref const lastValues{ make_list() };
                        ref const gnv{ attribute(firstEnum.get(), "_generate_next_value_") };
                        std::vector<object*> pairs;
                        long long count = 0;
                        for (auto n : items)
                        {
                            ref const name{ n };
                            ref const copy{ b_list({ lastValues.get() }, nullptr) };
                            ref const value{ gnv && copy ? invoke(gnv.get(), { new_reference(n), aStart != nullptr ? new_reference(aStart) : make_int(1), make_int(count++),
                                new_reference(copy.get()) }) : nullptr };
                            if (!value)
                                return (std::for_each(pairs.begin(), pairs.end(), release), nullptr);
                            list_append(lastValues.get(), value.get());
                            pairs.push_back(make_tuple({ new_reference(n), new_reference(value.get()) }));
                        }
                        names = ref{ make_list(std::move(pairs)) };
                    }
                    clear_error();
                }
                if (is_none(names.get()))
                    names = ref{ make_tuple() };
                {
                    std::vector<object*> items;
                    if (!collect(names.get(), items))
                        return nullptr;
                    for (auto i : items)
                    {
                        ref const item{ i };
                        ref memberName, memberValue;
                        if (kind_of(i) == NEOS_PY_STR)
                        {
                            memberName = ref{ new_reference(i) };
                            memberValue = ref{ getitem(names.get(), i) };
                        }
                        else
                        {
                            std::vector<object*> pair;
                            if (!collect(i, pair))
                                return nullptr;
                            if (pair.size() != 2u)
                            {
                                std::for_each(pair.begin(), pair.end(), release);
                                return raise("ValueError", "too many values to unpack (expected 2)");
                            }
                            memberName = ref{ pair[0] };
                            memberValue = ref{ pair[1] };
                        }
                        if (!memberValue)
                            return nullptr;
                        ref const r{ call_method_named(classdict.get(), "__setitem__", { new_reference(memberName.get()), new_reference(memberValue.get()) }) };
                        if (!r)
                            return nullptr;
                    }
                }
                ref const module{ aModule != nullptr && !is_none(aModule) ? new_reference(aModule) : make_str("__main__") };
                if (dict_set(builtin_value(classdict.get()), ref{ make_str("__module__") }.get(), module.get()) < 0)
                    return nullptr;
                if (aQualname != nullptr && !is_none(aQualname) && dict_set(builtin_value(classdict.get()), ref{ make_str("__qualname__") }.get(), aQualname) < 0)
                    return nullptr;
                ref const theNew{ attribute(metacls.get(), "__new__") };
                ref const kw{ keywords({ { "boundary", aBoundary } }) };
                return theNew ? invoke(theNew.get(), { new_reference(metacls.get()), new_reference(aClassName), new_reference(bases.get()), new_reference(classdict.get()) },
                    kw.get()) : nullptr;
            }

            // --- Enum

            // (Enum.__new__(cls, value): the member of the value)
            object* enum_new(std::vector<object*> const& aArgs, object*)
            {
                if (aArgs.size() != 2u)
                    return raise("TypeError", "Enum.__new__() takes 2 positional arguments");
                auto const cls = aArgs[0];
                auto const value = aArgs[1];
                if (ref const t{ class_of(value) }; t.get() == cls)
                    return new_reference(value);
                ref const value2member{ attribute(cls, "_value2member_map_") };
                ref const memberMap{ attribute(cls, "_member_map_") };
                if (!value2member || !memberMap)
                    return nullptr;
                int found = 0;
                if (auto const m = dict_item(value2member.get(), value, &found))
                    return new_reference(m);
                if (found == -2)
                {
                    // (an unhashable value)
                    clear_error();
                    ref const unhashableMap{ attribute(cls, "_unhashable_values_map_") };
                    if (unhashableMap)
                        for (auto const& item : as<dict_object>(unhashableMap.get())->items)
                            if (contains(item.second, value) == 1)
                                return getitem(memberMap.get(), item.first);
                    clear_error();
                    for (auto const& item : as<dict_object>(memberMap.get())->items)
                    {
                        ref const v{ attribute(item.second, "_value_") };
                        if (v && equal(value, v.get()) == 1)
                            return new_reference(item.second);
                        clear_error();
                    }
                }
                if (length(memberMap.get()) == 0)
                {
                    auto const inProgress = "_" + class_name(cls) + "__in_progress";
                    if (attribute_truth(cls, inProgress.c_str()) == 1)
                        return raise("TypeError", "do not use `super().__new__; call the appropriate __new__ directly");
                    clear_error();
                    return raise("TypeError", repr_text(cls) + " has no members defined");
                }
                ref const missing{ attribute(cls, "_missing_") };
                ref result{ missing ? invoke(missing.get(), { new_reference(value) }) : nullptr };
                ref exception;
                if (!result)
                {
                    exception = take_exception();
                    if (exception && !exception_is(exception.get(), "Exception"))
                    {
                        raise_exception(exception.get());
                        return nullptr;
                    }
                    result = ref{ new_reference(none()) };
                }
                if (instance_of(result.get(), cls) == 1)
                    return result.release_ownership();
                if (s.flag != nullptr && subclass_of(cls, s.flag) && is_int_like(builtin_value(result.get())))
                {
                    ref const boundary{ attribute_or_none(cls, "_boundary_") };
                    if (boundary.get() == s.eject)
                        return result.release_ownership();
                }
                ref const valueError{ invoke(ref{ exception_class("ValueError") }.get(), { make_str(repr_text(value) + " is not a valid " + class_qualname(cls)) }) };
                if (is_none(result.get()) && !exception)
                {
                    raise_exception(valueError.get());
                    return nullptr;
                }
                if (!exception)
                    exception = ref{ invoke(ref{ exception_class("TypeError") }.get(), { make_str("error in " + class_name(cls) + "._missing_: returned " + repr_text(result.get()) +
                        " instead of None or a valid member") }) };
                if (exception && !exception_is(exception.get(), "ValueError"))
                    setattr(exception.get(), "__context__", valueError.get());
                if (exception)
                    raise_exception(exception.get());
                return nullptr;
            }

            std::string value_repr(object* aMember)
            {
                ref const cls{ class_of(aMember) };
                ref const valueRepr{ attribute_or_none(cls.get(), "_value_repr_") };
                ref const value{ attribute(aMember, "_value_") };
                if (!value)
                    return {};
                if (is_none(valueRepr.get()))
                    return repr_text(value.get());
                ref const r{ invoke(valueRepr.get(), { new_reference(value.get()) }) };
                return r ? text_of(r.get()) : std::string{};
            }

            builtin_entry const sEnumMethods[] =
            {
                { "__new__", enum_new },
                { "__init__", [](std::vector<object*> const&, object*) -> object* { return new_reference(none()); } },
                { "_add_alias_", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return raise("TypeError", "_add_alias_() takes 1 positional argument");
                        ref const cls{ class_of(aArgs[0]) };
                        return add_member(cls.get(), aArgs[1], aArgs[0]) ? new_reference(none()) : nullptr;
                    } },
                { "_add_value_alias_", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return raise("TypeError", "_add_value_alias_() takes 1 positional argument");
                        auto const self = aArgs[0];
                        auto const value = aArgs[1];
                        ref const cls{ class_of(self) };
                        ref const value2member{ attribute(cls.get(), "_value2member_map_") };
                        if (!value2member)
                            return nullptr;
                        int found = 0;
                        if (auto const existing = dict_item(value2member.get(), value, &found))
                        {
                            if (existing != self)
                                return raise("ValueError", repr_text(value) + " is already bound: " + repr_text(existing));
                            return new_reference(none());
                        }
                        if (found == -2)
                        {
                            clear_error();
                            ref const memberMap{ attribute(cls.get(), "_member_map_") };
                            for (auto const& item : as<dict_object>(memberMap.get())->items)
                            {
                                ref const v{ attribute(item.second, "_value_") };
                                if (v && equal(v.get(), value) == 1)
                                {
                                    if (item.second != self)
                                        return raise("ValueError", repr_text(value) + " is already bound: " + repr_text(item.second));
                                    return new_reference(none());
                                }
                            }
                            ref const unhashable{ attribute(cls.get(), "_unhashable_values_") };
                            ref const unhashableMap{ attribute(cls.get(), "_unhashable_values_map_") };
                            ref const name{ attribute(self, "_name_") };
                            if (!unhashable || !unhashableMap || !name || !list_append(unhashable.get(), value))
                                return nullptr;
                            object* list = dict_item(unhashableMap.get(), name.get());
                            if (list == nullptr)
                            {
                                dict_set(unhashableMap.get(), name.get(), ref{ make_list() }.get());
                                list = dict_item(unhashableMap.get(), name.get());
                            }
                            return list_append(list, value) ? new_reference(none()) : nullptr;
                        }
                        ref const hashable{ attribute(cls.get(), "_hashable_values_") };
                        if (dict_set(value2member.get(), value, self) < 0 || !hashable || !list_append(hashable.get(), value))
                            return nullptr;
                        return new_reference(none());
                    } },
                { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const self = aArgs[0];
                        ref const cls{ class_of(self) };
                        ref const name{ attribute(self, "_name_") };
                        if (!name)
                            return nullptr;
                        auto const v = value_repr(self);
                        if (failed())
                            return nullptr;
                        return make_str("<" + class_name(cls.get()) + "." + text_of(name.get()) + ": " + v + ">");
                    } },
                { "__str__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const cls{ class_of(aArgs[0]) };
                        ref const name{ attribute(aArgs[0], "_name_") };
                        return name ? make_str(class_name(cls.get()) + "." + text_of(name.get())) : nullptr;
                    } },
                { "__dir__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.empty())
                            return raise("TypeError", "Enum.__dir__() missing 1 required positional argument: 'self'");
                        auto const self = aArgs[0];
                        ref const cls{ class_of(self) };
                        std::set<std::string> interesting = { "__class__", "__doc__", "__eq__", "__hash__", "__module__" };
                        ref const memberMap{ attribute(cls.get(), "_member_map_") };
                        ref const memberType{ attribute(cls.get(), "_member_type_") };
                        if (memberType && !is_object_type(memberType.get()))
                        {
                            ref const d{ b_dir({ builtin_value(self) }, nullptr) };
                            if (d)
                                for (auto n : as<list_object>(d.get())->items)
                                    interesting.insert(text_of(n));
                            clear_error();
                        }
                        if (ref const d{ getattr(self, "__dict__") })
                            for (auto const& item : as<dict_object>(d.get())->items)
                            {
                                auto const n = text_of(item.first);
                                if (!n.empty() && n[0] != '_' && (!memberMap || dict_item(memberMap.get(), item.first) == nullptr))
                                    interesting.insert(n);
                            }
                        clear_error();
                        ref const mro{ attribute(cls.get(), "__mro__") };
                        if (mro)
                            for (auto c : as<tuple_object>(mro.get())->items)
                            {
                                if (!is_class(c))
                                    continue;
                                ref const d{ attribute(c, "__dict__") };
                                if (!d)
                                    continue;
                                for (auto const& item : as<dict_object>(proxied_mapping(d.get()))->items)
                                {
                                    auto const n = text_of(item.first);
                                    if (n.empty() || n[0] == '_')
                                        continue;
                                    bool const isProperty = s.propertyClass != nullptr && instance_of(item.second, s.propertyClass) == 1;
                                    if (isProperty)
                                    {
                                        ref const fget{ attribute_or_none(item.second, "fget") };
                                        if (!is_none(fget.get()) || (memberMap && dict_item(memberMap.get(), item.first) == nullptr))
                                            interesting.insert(n);
                                        else
                                            interesting.erase(n);
                                    }
                                    else if (!memberMap || dict_item(memberMap.get(), item.first) == nullptr)
                                        interesting.insert(n);
                                }
                            }
                        clear_error();
                        std::vector<object*> result;
                        for (auto const& n : interesting)
                            result.push_back(make_str(n));
                        return make_list(std::move(result));
                    } },
                { "__format__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return raise("TypeError", "__format__ expected 1 argument");
                        std::string text;
                        if (!str_of(aArgs[0], text))
                            return nullptr;
                        ref const format{ fn_builtin("format") };
                        return format ? invoke(format.get(), { make_str(text), new_reference(aArgs[1]) }) : nullptr;
                    } },
                { "__hash__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const name{ attribute(aArgs[0], "_name_") };
                        long long h = 0;
                        return name && hash_of(name.get(), h) ? make_int(h) : nullptr;
                    } },
                { "__reduce_ex__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const cls{ class_of(aArgs[0]) };
                        ref const value{ attribute(aArgs[0], "_value_") };
                        return value ? make_tuple({ new_reference(cls.get()), make_tuple({ new_reference(value.get()) }) }) : nullptr;
                    } },
                { "__deepcopy__", [](std::vector<object*> const& aArgs, object*) -> object* { return new_reference(aArgs[0]); } },
                { "__copy__", [](std::vector<object*> const& aArgs, object*) -> object* { return new_reference(aArgs[0]); } }
            };

            builtin_entry const sEnumName{ "name", [](std::vector<object*> const& aArgs, object*) -> object* { return getattr(aArgs[0], "_name_"); } };
            builtin_entry const sEnumValue{ "value", [](std::vector<object*> const& aArgs, object*) -> object* { return getattr(aArgs[0], "_value_"); } };

            builtin_entry const sEnumStatic[] =
            {
                // (_generate_next_value_(name, start, count, last_values): the last value plus one)
                { "_generate_next_value_", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("_generate_next_value_", aArgs, aKwargs, { "name", "start", "count", "last_values" }, 4u, 0u, a))
                            return nullptr;
                        if (length(a[3]) == 0)
                            return new_reference(a[1]);
                        ref const sorted{ b_sorted({ a[3] }, nullptr) };
                        if (!sorted)
                        {
                            ref const e{ take_exception() };
                            return raise("TypeError", "unable to sort non-numeric values");
                        }
                        auto const last = as<list_object>(sorted.get())->items.back();
                        ref const one{ make_int(1) };
                        ref next{ binary(NEOS_PY_ADD, last, one.get(), false) };
                        if (!next)
                        {
                            ref const e{ take_exception() };
                            return raise("TypeError", "unable to increment " + repr_text(last));
                        }
                        return next.release_ownership();
                    } }
            };

            builtin_entry const sEnumClassMethods[] =
            {
                { "_missing_", [](std::vector<object*> const&, object*) -> object* { return new_reference(none()); } }
            };

            // --- StrEnum

            builtin_entry const sStrEnumMethods[] =
            {
                { "__new__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        // (cls, *values): values must already be strs
                        if (aArgs.empty())
                            return raise("TypeError", "StrEnum.__new__() needs a class");
                        std::vector<object*> values{ aArgs.begin() + 1, aArgs.end() };
                        if (values.size() > 3u)
                        {
                            ref const t{ make_tuple(std::vector<object*>{}) };
                            for (auto v : values)
                                as<tuple_object>(t.get())->items.push_back(new_reference(v));
                            return raise("TypeError", "too many arguments for str(): " + repr_text(t.get()));
                        }
                        if (values.size() >= 1u && kind_of(builtin_value(values[0])) != NEOS_PY_STR)
                            return raise("TypeError", repr_text(values[0]) + " is not a string");
                        if (values.size() >= 2u && kind_of(builtin_value(values[1])) != NEOS_PY_STR)
                            return raise("TypeError", "encoding must be a string, not " + repr_text(values[1]));
                        if (values.size() == 3u && kind_of(builtin_value(values[2])) != NEOS_PY_STR)
                            return raise("TypeError", "errors must be a string, not " + repr_text(values[2]));
                        ref const strType{ fn_builtin("str") };
                        ref const args{ make_tuple(std::vector<object*>{}) };
                        for (auto v : values)
                            as<tuple_object>(args.get())->items.push_back(new_reference(v));
                        ref const value{ call(strType.get(), args.get(), nullptr) };
                        if (!value)
                            return nullptr;
                        ref const objectNew{ object_new() };
                        ref member{ invoke(objectNew.get(), { new_reference(aArgs[0]), new_reference(value.get()) }) };
                        if (!member || setattr(member.get(), "_value_", value.get()) < 0)
                            return nullptr;
                        return member.release_ownership();
                    } }
            };

            builtin_entry const sStrEnumStatic[] =
            {
                { "_generate_next_value_", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("_generate_next_value_", aArgs, aKwargs, { "name", "start", "count", "last_values" }, 4u, 0u, a))
                            return nullptr;
                        return call_method_named(a[0], "lower", {});
                    } }
            };

            // --- Flag

            // (_iter_bits_lsb: the bits of a non-negative int, lowest first)
            bool bits_of(object* aValue, std::vector<object*>& aResult)
            {
                ref num{ new_reference(builtin_value(aValue)) };
                if (s.enumClass != nullptr && instance_of(aValue, s.enumClass) == 1)
                {
                    num = attribute(aValue, "value");
                    if (!num)
                        return false;
                }
                ref const zero{ make_int(0) };
                ref const negative{ compare(NEOS_PY_LT, num.get(), zero.get()) };
                if (!negative)
                    return false;
                if (truth(negative.get()) == 1)
                    return (raise("ValueError", repr_text(aValue) + " is not a positive integer"), false);
                for (;;)
                {
                    if (equal(num.get(), zero.get()) == 1)
                        return true;
                    ref const inverted{ unary(NEOS_PY_INVERT, num.get()) };
                    ref const one{ make_int(1) };
                    ref const plus{ inverted ? binary(NEOS_PY_ADD, inverted.get(), one.get(), false) : nullptr };
                    ref const bit{ plus ? binary(NEOS_PY_AND, num.get(), plus.get(), false) : nullptr };
                    if (!bit)
                        return false;
                    aResult.push_back(new_reference(bit.get()));
                    num = ref{ binary(NEOS_PY_XOR, num.get(), bit.get(), false) };
                    if (!num)
                        return false;
                }
            }

            ref int_value(long long aValue) { return ref{ make_int(aValue) }; }
            ref op(int aOperation, object* aLhs, object* aRhs) { return ref{ binary(aOperation, aLhs, aRhs, false) }; }
            int cmp(int aOperation, object* aLhs, object* aRhs)
            {
                ref const r{ compare(aOperation, aLhs, aRhs) };
                return r ? truth(r.get()) : -1;
            }
            ref bit_length(object* aValue) { return ref{ call_method_named(builtin_value(aValue), "bit_length", {}) }; }

            // (enum.bin(num, max_bits=None))
            std::string flag_bin(object* aNumber, object* aMaxBits)
            {
                ref const num{ call_method_named(aNumber, "__index__", {}) };
                if (!num)
                    return {};
                ref const bits{ bit_length(num.get()) };
                ref const ceiling{ bits ? op(NEOS_PY_POWER, int_value(2).get(), bits.get()) : ref{} };
                if (!ceiling)
                    return {};
                ref const binFunction{ fn_builtin("bin") };
                std::string text;
                if (cmp(NEOS_PY_GE, num.get(), int_value(0).get()) == 1)
                {
                    ref const sum{ op(NEOS_PY_ADD, num.get(), ceiling.get()) };
                    ref const b{ invoke(binFunction.get(), { new_reference(sum.get()) }) };
                    text = text_of(b.get());
                    auto const one = text.find('1');
                    if (one != std::string::npos)
                        text[one] = '0';
                }
                else
                {
                    ref const inverted{ unary(NEOS_PY_INVERT, num.get()) };
                    ref const mask{ op(NEOS_PY_SUBTRACT, ceiling.get(), int_value(1).get()) };
                    ref const x{ op(NEOS_PY_ADD, mask.get(), ceiling.get()) };
                    ref const y{ op(NEOS_PY_XOR, inverted.get(), x.get()) };
                    ref const b{ y ? invoke(binFunction.get(), { new_reference(y.get()) }) : nullptr };
                    if (!b)
                        return {};
                    text = text_of(b.get());
                }
                auto const sign = text.substr(0u, 3u);
                auto digits = text.substr(3u);
                if (aMaxBits != nullptr && !is_none(aMaxBits))
                {
                    long long maxBits = 0;
                    if (to_index(aMaxBits, maxBits) && static_cast<long long>(digits.size()) < maxBits)
                    {
                        auto padded = std::string(static_cast<std::size_t>(maxBits), sign.back()) + digits;
                        digits = padded.substr(padded.size() - static_cast<std::size_t>(maxBits));
                    }
                }
                return sign + " " + digits;
            }

            object* flag_iter_members(object* aClass, object* aValue, bool aByDefinition)
            {
                ref const mask{ attribute(aClass, "_flag_mask_") };
                ref const value2member{ attribute(aClass, "_value2member_map_") };
                if (!mask || !value2member)
                    return nullptr;
                ref const masked{ op(NEOS_PY_AND, builtin_value(aValue), mask.get()) };
                std::vector<object*> bits;
                if (!masked || !bits_of(masked.get(), bits))
                    return nullptr;
                std::vector<object*> members;
                for (auto b : bits)
                {
                    ref const bit{ b };
                    auto const m = dict_item(value2member.get(), b);
                    members.push_back(new_reference(m != nullptr ? m : none()));
                }
                if (aByDefinition)
                    std::stable_sort(members.begin(), members.end(), [](object* x, object* y)
                    {
                        long long a = 0, b = 0;
                        attribute_int(x, "_sort_order_", a);
                        attribute_int(y, "_sort_order_", b);
                        return a < b;
                    });
                clear_error();
                ref const list{ make_list(std::move(members)) };
                return iter(list.get());
            }

            // (Flag._missing_(cls, value))
            object* flag_missing(object* aClass, object* aValue)
            {
                if (!is_int_like(builtin_value(aValue)))
                    return raise("ValueError", repr_text(aValue) + " is not a valid " + class_qualname(aClass));
                ref value{ new_reference(builtin_value(aValue)) };
                ref const flagMask{ attribute(aClass, "_flag_mask_") };
                ref const singlesMask{ attribute(aClass, "_singles_mask_") };
                ref const allBits{ attribute(aClass, "_all_bits_") };
                ref const boundary{ attribute_or_none(aClass, "_boundary_") };
                if (!flagMask || !singlesMask || !allBits)
                    return nullptr;
                ref negValue;
                ref const notAll{ unary(NEOS_PY_INVERT, allBits.get()) };
                bool const inRange = cmp(NEOS_PY_LE, notAll.get(), value.get()) == 1 && cmp(NEOS_PY_LE, value.get(), allBits.get()) == 1;
                ref const outside{ op(NEOS_PY_AND, value.get(), op(NEOS_PY_XOR, allBits.get(), flagMask.get()).get()) };
                if (!inRange || truth(outside.get()) == 1)
                {
                    if (boundary.get() == s.strict)
                    {
                        ref const a{ bit_length(value.get()) };
                        ref const b{ bit_length(flagMask.get()) };
                        ref const maxBits{ cmp(NEOS_PY_GT, a.get(), b.get()) == 1 ? a : b };
                        return raise("ValueError", repr_text(aClass) + " invalid value " + repr_text(value.get()) + "\n    given " + flag_bin(value.get(), maxBits.get()) +
                            "\n  allowed " + flag_bin(flagMask.get(), maxBits.get()));
                    }
                    else if (boundary.get() == s.conform)
                        value = op(NEOS_PY_AND, value.get(), flagMask.get());
                    else if (boundary.get() == s.eject)
                        return value.release_ownership();
                    else if (boundary.get() == s.keep)
                    {
                        if (cmp(NEOS_PY_LT, value.get(), int_value(0).get()) == 1)
                        {
                            ref const plus{ op(NEOS_PY_ADD, allBits.get(), int_value(1).get()) };
                            ref const power{ op(NEOS_PY_POWER, int_value(2).get(), bit_length(value.get()).get()) };
                            ref const top{ cmp(NEOS_PY_GT, plus.get(), power.get()) == 1 ? plus : power };
                            value = op(NEOS_PY_ADD, top.get(), value.get());
                        }
                    }
                    else
                        return raise("ValueError", repr_text(aClass) + " unknown flag boundary " + repr_text(boundary.get()));
                }
                if (cmp(NEOS_PY_LT, value.get(), int_value(0).get()) == 1)
                {
                    negValue = value;
                    value = op(NEOS_PY_ADD, op(NEOS_PY_ADD, allBits.get(), int_value(1).get()).get(), value.get());
                }
                ref const unknown{ op(NEOS_PY_AND, value.get(), ref{ unary(NEOS_PY_INVERT, flagMask.get()) }.get()) };
                ref const aliases{ op(NEOS_PY_AND, value.get(), ref{ unary(NEOS_PY_INVERT, singlesMask.get()) }.get()) };
                ref const memberValue{ op(NEOS_PY_AND, value.get(), singlesMask.get()) };
                if (!unknown || !aliases || !memberValue)
                    return nullptr;
                if (truth(unknown.get()) == 1 && boundary.get() != s.keep)
                    return raise("ValueError", class_name(aClass) + "(" + repr_text(value.get()) + ") -->  unknown values " + repr_text(unknown.get()) + " [" +
                        flag_bin(unknown.get(), nullptr) + "]");
                ref const memberType{ attribute(aClass, "_member_type_") };
                ref const objectNew{ object_new() };
                ref pseudo{ is_object_type(memberType.get()) ? invoke(objectNew.get(), { new_reference(aClass) }) :
                    invoke(objectNew.get(), { new_reference(aClass), new_reference(value.get()) }) };
                if (!pseudo)
                    return nullptr;
                if (!has_attribute(pseudo.get(), "_value_") && setattr(pseudo.get(), "_value_", value.get()) < 0)
                    return nullptr;
                ref name{ new_reference(none()) };
                if (truth(memberValue.get()) == 1 || truth(aliases.get()) == 1)
                {
                    std::vector<object*> members;
                    ref combined{ make_int(0) };
                    ref const iterMember{ attribute(aClass, "_iter_member_") };
                    ref const it{ iterMember ? invoke(iterMember.get(), { new_reference(memberValue.get()) }) : nullptr };
                    if (!it)
                        return nullptr;
                    for (;;)
                    {
                        ref const m{ next(it.get()) };
                        if (!m)
                            break;
                        members.push_back(new_reference(m.get()));
                        ref const v{ attribute(m.get(), "_value_") };
                        combined = op(NEOS_PY_OR, combined.get(), v.get());
                    }
                    if (failed())
                        return (std::for_each(members.begin(), members.end(), release), nullptr);
                    ref aliasValue{ new_reference(value.get()) };
                    if (truth(aliases.get()) == 1)
                    {
                        aliasValue = op(NEOS_PY_OR, memberValue.get(), aliases.get());
                        ref const memberMap{ attribute(aClass, "_member_map_") };
                        for (auto const& item : as<dict_object>(memberMap.get())->items)
                        {
                            if (std::find(members.begin(), members.end(), item.second) != members.end())
                                continue;
                            ref const v{ attribute(item.second, "_value_") };
                            if (v && truth(v.get()) == 1 && equal(op(NEOS_PY_AND, v.get(), aliasValue.get()).get(), v.get()) == 1)
                            {
                                members.push_back(new_reference(item.second));
                                combined = op(NEOS_PY_OR, combined.get(), v.get());
                            }
                        }
                    }
                    ref const unknown2{ op(NEOS_PY_XOR, aliasValue.get(), combined.get()) };
                    std::string joined;
                    for (std::size_t i = 0u; i < members.size(); ++i)
                    {
                        ref const n{ attribute(members[i], "_name_") };
                        joined += (i == 0u ? "" : "|") + (n ? text_of(n.get()) : std::string{});
                    }
                    std::for_each(members.begin(), members.end(), release);
                    if (truth(combined.get()) != 1)
                        name = ref{ new_reference(none()) };
                    else if (truth(unknown2.get()) == 1 && boundary.get() == s.strict)
                        return raise("ValueError", repr_text(aClass) + ": no members with value " + repr_text(unknown2.get()));
                    else if (truth(unknown2.get()) == 1)
                    {
                        ref const numericRepr{ attribute(aClass, "_numeric_repr_") };
                        ref const r{ numericRepr ? invoke(numericRepr.get(), { new_reference(unknown2.get()) }) : nullptr };
                        name = ref{ make_str(joined + "|" + (r ? text_of(r.get()) : std::string{})) };
                    }
                    else
                        name = ref{ make_str(joined) };
                }
                if (setattr(pseudo.get(), "_name_", name.get()) < 0)
                    return nullptr;
                ref const value2member{ attribute(aClass, "_value2member_map_") };
                if (auto const existing = dict_item(value2member.get(), value.get()))
                    pseudo = ref{ new_reference(existing) };
                else if (dict_set(value2member.get(), value.get(), pseudo.get()) < 0)
                    return nullptr;
                if (negValue && dict_set(value2member.get(), negValue.get(), pseudo.get()) < 0)
                    return nullptr;
                return pseudo.release_ownership();
            }

            // (Flag._get_value(flag): a flag's value, an int of the member type's, or NotImplemented)
            ref flag_get_value(object* aSelf, object* aFlag)
            {
                ref const cls{ class_of(aSelf) };
                if (instance_of(aFlag, cls.get()) == 1)
                    return attribute(aFlag, "_value_");
                ref const memberType{ attribute(cls.get(), "_member_type_") };
                if (memberType && !is_object_type(memberType.get()) && instance_of(aFlag, memberType.get()) == 1)
                    return ref{ new_reference(aFlag) };
                return ref{ not_implemented() };
            }

            object* flag_binary(std::vector<object*> const& aArgs, int aOperation, char const* aSymbol)
            {
                if (aArgs.size() != 2u)
                    return raise("TypeError", "expected 1 argument");
                auto const self = aArgs[0];
                ref const other{ flag_get_value(self, aArgs[1]) };
                if (!other)
                    return nullptr;
                if (kind_of(other.get()) == NEOS_PY_NOT_IMPLEMENTED)
                    return not_implemented();
                for (auto f : { self, aArgs[1] })
                {
                    ref const v{ flag_get_value(self, f) };
                    if (v && is_none(v.get()))
                        return raise("TypeError", "'" + text_of(f) + "' cannot be combined with other flags with " + aSymbol);
                }
                ref const value{ attribute(self, "_value_") };
                ref const combined{ value ? op(aOperation, value.get(), other.get()) : ref{} };
                ref const cls{ class_of(self) };
                return combined ? invoke(cls.get(), { new_reference(combined.get()) }) : nullptr;
            }

            builtin_entry const sFlagMethods[] =
            {
                { "__contains__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return raise("TypeError", "__contains__ expected 1 argument");
                        ref const cls{ class_of(aArgs[0]) };
                        if (instance_of(aArgs[1], cls.get()) != 1)
                        {
                            ref const other{ class_of(aArgs[1]) };
                            return raise("TypeError", "unsupported operand type(s) for 'in': " + repr_text(ref{ make_str(class_qualname(other.get())) }.get()) + " and " +
                                repr_text(ref{ make_str(class_qualname(cls.get())) }.get()));
                        }
                        ref const mine{ attribute(aArgs[0], "_value_") };
                        ref const theirs{ attribute(aArgs[1], "_value_") };
                        ref const masked{ op(NEOS_PY_AND, theirs.get(), mine.get()) };
                        return masked ? make_bool(equal(masked.get(), theirs.get()) == 1) : nullptr;
                    } },
                { "__iter__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const cls{ class_of(aArgs[0]) };
                        ref const iterMember{ attribute(cls.get(), "_iter_member_") };
                        ref const value{ attribute(aArgs[0], "_value_") };
                        return iterMember && value ? invoke(iterMember.get(), { new_reference(value.get()) }) : nullptr;
                    } },
                { "__len__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const value{ attribute(aArgs[0], "_value_") };
                        return value ? call_method_named(builtin_value(value.get()), "bit_count", {}) : nullptr;
                    } },
                { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const self = aArgs[0];
                        ref const cls{ class_of(self) };
                        ref const name{ attribute(self, "_name_") };
                        if (!name)
                            return nullptr;
                        auto const v = value_repr(self);
                        if (is_none(name.get()))
                            return make_str("<" + class_name(cls.get()) + ": " + v + ">");
                        return make_str("<" + class_name(cls.get()) + "." + text_of(name.get()) + ": " + v + ">");
                    } },
                { "__str__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const self = aArgs[0];
                        ref const cls{ class_of(self) };
                        ref const name{ attribute(self, "_name_") };
                        ref const value{ attribute(self, "_value_") };
                        if (!name || !value)
                            return nullptr;
                        if (is_none(name.get()))
                            return make_str(class_name(cls.get()) + "(" + repr_text(value.get()) + ")");
                        return make_str(class_name(cls.get()) + "." + text_of(name.get()));
                    } },
                { "__bool__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const value{ attribute(aArgs[0], "_value_") };
                        return value ? make_bool(truth(value.get()) == 1) : nullptr;
                    } },
                { "_get_value", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return raise("TypeError", "_get_value() takes 1 positional argument");
                        return flag_get_value(aArgs[0], aArgs[1]).release_ownership();
                    } },
                { "__or__", [](std::vector<object*> const& aArgs, object*) -> object* { return flag_binary(aArgs, NEOS_PY_OR, "|"); } },
                { "__and__", [](std::vector<object*> const& aArgs, object*) -> object* { return flag_binary(aArgs, NEOS_PY_AND, "&"); } },
                { "__xor__", [](std::vector<object*> const& aArgs, object*) -> object* { return flag_binary(aArgs, NEOS_PY_XOR, "^"); } },
                { "__ror__", [](std::vector<object*> const& aArgs, object*) -> object* { return flag_binary(aArgs, NEOS_PY_OR, "|"); } },
                { "__rand__", [](std::vector<object*> const& aArgs, object*) -> object* { return flag_binary(aArgs, NEOS_PY_AND, "&"); } },
                { "__rxor__", [](std::vector<object*> const& aArgs, object*) -> object* { return flag_binary(aArgs, NEOS_PY_XOR, "^"); } },
                { "__invert__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const self = aArgs[0];
                        ref const v{ flag_get_value(self, self) };
                        if (v && is_none(v.get()))
                            return raise("TypeError", "'" + text_of(self) + "' cannot be inverted");
                        ref const inverted{ attribute_or_none(self, "_inverted_") };
                        if (!is_none(inverted.get()))
                            return new_reference(inverted.get());
                        ref const cls{ class_of(self) };
                        ref const boundary{ attribute_or_none(cls.get(), "_boundary_") };
                        ref const value{ attribute(self, "_value_") };
                        ref result;
                        if (boundary.get() == s.eject || boundary.get() == s.keep)
                            result = ref{ invoke(cls.get(), { unary(NEOS_PY_INVERT, value.get()) }) };
                        else
                        {
                            ref const singles{ attribute(cls.get(), "_singles_mask_") };
                            ref const masked{ op(NEOS_PY_AND, singles.get(), ref{ unary(NEOS_PY_INVERT, value.get()) }.get()) };
                            result = masked ? ref{ invoke(cls.get(), { new_reference(masked.get()) }) } : ref{};
                        }
                        if (!result || setattr(self, "_inverted_", result.get()) < 0)
                            return nullptr;
                        return result.release_ownership();
                    } }
            };

            builtin_entry const sFlagStatic[] =
            {
                { "_generate_next_value_", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("_generate_next_value_", aArgs, aKwargs, { "name", "start", "count", "last_values" }, 4u, 0u, a))
                            return nullptr;
                        if (truth(a[2]) != 1)
                            return is_none(a[1]) ? make_int(1) : new_reference(a[1]);
                        ref const maxFunction{ fn_builtin("max") };
                        ref const last{ invoke(maxFunction.get(), { new_reference(a[3]) }) };
                        if (!last)
                            return nullptr;
                        ref const bits{ is_int_like(builtin_value(last.get())) ? bit_length(last.get()) : ref{} };
                        if (!bits)
                        {
                            clear_error();
                            return raise("TypeError", "invalid flag value " + repr_text(last.get()));
                        }
                        return binary(NEOS_PY_POWER, int_value(2).get(), bits.get(), false);
                    } }
            };

            builtin_entry const sFlagClassMethods[] =
            {
                { "_iter_member_by_value_", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        return aArgs.size() == 2u ? flag_iter_members(aArgs[0], aArgs[1], false) : raise("TypeError", "_iter_member_by_value_() takes 1 positional argument");
                    } },
                { "_iter_member_by_def_", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        return aArgs.size() == 2u ? flag_iter_members(aArgs[0], aArgs[1], true) : raise("TypeError", "_iter_member_by_def_() takes 1 positional argument");
                    } },
                { "_missing_", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        return aArgs.size() == 2u ? flag_missing(aArgs[0], aArgs[1]) : raise("TypeError", "_missing_() takes 1 positional argument");
                    } }
            };

            // --- enum.property: a DynamicClassAttribute whose class's attribute is the member of its name

            builtin_entry const sPropertyMethods[] =
            {
                { "__get__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() < 2u)
                            return raise("TypeError", "__get__() expected at least 1 argument");
                        auto const self = aArgs[0];
                        auto const instance = aArgs[1];
                        object* owner = aArgs.size() > 2u ? aArgs[2] : none();
                        ref const name{ attribute_or_none(self, "name") };
                        if (is_none(instance))
                        {
                            ref const member{ attribute_or_none(self, "member") };
                            if (!is_none(member.get()))
                                return new_reference(member.get());
                            return raise("AttributeError", repr_text(owner) + " has no attribute " + repr_text(name.get()));
                        }
                        ref const fget{ attribute_or_none(self, "fget") };
                        if (!is_none(fget.get()))
                            return invoke(fget.get(), { new_reference(instance) });
                        ref const type{ attribute_or_none(self, "_attr_type") };
                        auto const t = kind_of(type.get()) == NEOS_PY_STR ? text_of(type.get()) : std::string{};
                        if (t == "attr")
                        {
                            ref const cls{ attribute_or_none(self, "_cls_type") };
                            return getattr(cls.get(), text_of(name.get()).c_str());
                        }
                        if (t == "desc")
                        {
                            ref const value{ attribute(instance, "_value_") };
                            return value ? getattr(value.get(), text_of(name.get()).c_str()) : nullptr;
                        }
                        ref const memberMap{ is_none(owner) ? ref{} : attribute(owner, "_member_map_") };
                        if (memberMap)
                            if (auto const m = dict_item(memberMap.get(), name.get()))
                                return new_reference(m);
                        clear_error();
                        return raise("AttributeError", repr_text(owner) + " has no attribute " + repr_text(name.get()));
                    } },
                { "__set__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 3u)
                            return raise("TypeError", "__set__() expected 2 arguments");
                        ref const fset{ attribute_or_none(aArgs[0], "fset") };
                        if (!is_none(fset.get()))
                            return invoke(fset.get(), { new_reference(aArgs[1]), new_reference(aArgs[2]) });
                        ref const clsName{ attribute_or_none(aArgs[0], "clsname") };
                        ref const name{ attribute_or_none(aArgs[0], "name") };
                        return raise("AttributeError", "<enum " + repr_text(clsName.get()) + "> cannot set attribute " + repr_text(name.get()));
                    } },
                { "__delete__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return raise("TypeError", "__delete__() expected 1 argument");
                        ref const fdel{ attribute_or_none(aArgs[0], "fdel") };
                        if (!is_none(fdel.get()))
                            return invoke(fdel.get(), { new_reference(aArgs[1]) });
                        ref const clsName{ attribute_or_none(aArgs[0], "clsname") };
                        ref const name{ attribute_or_none(aArgs[0], "name") };
                        return raise("AttributeError", "<enum " + repr_text(clsName.get()) + "> cannot delete attribute " + repr_text(name.get()));
                    } },
                { "__set_name__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 3u)
                            return raise("TypeError", "__set_name__() expected 2 arguments");
                        ref const clsName{ make_str(class_name(aArgs[1])) };
                        if (setattr(aArgs[0], "name", aArgs[2]) < 0 || setattr(aArgs[0], "clsname", clsName.get()) < 0)
                            return nullptr;
                        return new_reference(none());
                    } }
            };

            // --- the module's functions

            object* unique_check(object* aEnumeration, char const* aMessage)
            {
                ref const members{ attribute(aEnumeration, "__members__") };
                ref const items{ members ? call_method_named(members.get(), "items", {}) : nullptr };
                std::vector<object*> pairs;
                if (!items || !collect(items.get(), pairs))
                    return nullptr;
                std::string details;
                for (auto p : pairs)
                {
                    ref const pair{ p };
                    auto const& kv = as<tuple_object>(p)->items;
                    ref const name{ attribute(kv[1], "name") };
                    if (name && equal(kv[0], name.get()) != 1)
                        details += (details.empty() ? "" : ", ") + text_of(kv[0]) + " -> " + text_of(name.get());
                }
                if (!details.empty())
                    return raise("ValueError", std::string{ aMessage } + repr_text(aEnumeration) + ": " + details);
                return new_reference(aEnumeration);
            }

            // (verify(*checks): a decorator)
            builtin_entry const sVerifyMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const checks{ make_tuple(std::vector<object*>{}) };
                        for (std::size_t i = 1u; i < aArgs.size(); ++i)
                            as<tuple_object>(checks.get())->items.push_back(new_reference(aArgs[i]));
                        return setattr(aArgs[0], "checks", checks.get()) < 0 ? nullptr : new_reference(none());
                    } },
                { "__call__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return raise("TypeError", "verify() expected an enumeration");
                        auto const enumeration = aArgs[1];
                        ref const checks{ attribute(aArgs[0], "checks") };
                        if (!checks)
                            return nullptr;
                        auto const clsName = class_name(enumeration);
                        std::string enumType;
                        if (s.flag != nullptr && subclass_of(enumeration, s.flag))
                            enumType = "flag";
                        else if (subclass_of(enumeration, s.enumClass))
                            enumType = "enum";
                        else
                            return raise("TypeError", "the 'verify' decorator only works with Enum and Flag");
                        for (auto check : as<tuple_object>(checks.get())->items)
                        {
                            if (check == s.unique)
                            {
                                ref const r{ unique_check(enumeration, "aliases found in ") };
                                if (!r)
                                    return nullptr;
                            }
                            else if (check == s.continuous)
                            {
                                std::vector<object*> values;
                                ref const members{ members_of(enumeration) };
                                for (auto m : as<list_object>(members.get())->items)
                                    values.push_back(getattr(m, "value"));
                                ref const list{ make_list(std::move(values)) };
                                ref const set{ b_set({ list.get() }, nullptr) };
                                if (length(set.get()) < 2)
                                    continue;
                                ref const sorted{ b_sorted({ set.get() }, nullptr) };
                                if (!sorted)
                                    return nullptr;
                                auto const low = as<list_object>(sorted.get())->items.front();
                                auto const high = as<list_object>(sorted.get())->items.back();
                                std::vector<std::string> missing;
                                long long lo = 0, hi = 0;
                                if (enumType == "flag")
                                {
                                    ref const l{ bit_length(low) };
                                    ref const h{ bit_length(high) };
                                    to_index(l.get(), lo);
                                    to_index(h.get(), hi);
                                    for (long long i = lo; i < hi - 1; ++i)
                                    {
                                        ref const power{ op(NEOS_PY_POWER, int_value(2).get(), int_value(i).get()) };
                                        if (contains(set.get(), power.get()) != 1)
                                            missing.push_back(text_of(power.get()));
                                    }
                                }
                                else
                                {
                                    to_index(low, lo);
                                    to_index(high, hi);
                                    for (long long i = lo + 1; i < hi; ++i)
                                        if (contains(set.get(), int_value(i).get()) != 1)
                                            missing.push_back(std::to_string(i));
                                }
                                if (!missing.empty())
                                {
                                    std::string list2;
                                    for (std::size_t i = 0u; i < missing.size(); ++i)
                                        list2 += (i == 0u ? "" : ", ") + missing[i];
                                    auto message = "invalid " + enumType + " " + repr_text(ref{ make_str(clsName) }.get()) + ": missing values " + list2;
                                    return raise("ValueError", message.substr(0u, 256u));
                                }
                            }
                            else if (check == s.namedFlags)
                            {
                                ref const memberNames{ attribute(enumeration, "_member_names_") };
                                ref const memberMap{ attribute(enumeration, "_member_map_") };
                                std::vector<object*> memberValues;
                                ref const members{ members_of(enumeration) };
                                for (auto m : as<list_object>(members.get())->items)
                                    memberValues.push_back(getattr(m, "value"));
                                ref const valuesList{ make_list(std::move(memberValues)) };
                                std::vector<std::string> missingNames;
                                ref missingValue{ make_int(0) };
                                for (auto const& item : as<dict_object>(memberMap.get())->items)
                                {
                                    if (contains(memberNames.get(), item.first) == 1)
                                        continue;
                                    ref const v{ attribute(item.second, "value") };
                                    if (cmp(NEOS_PY_LT, v.get(), int_value(0).get()) == 1)
                                        continue;
                                    std::vector<object*> bits;
                                    if (!bits_of(v.get(), bits))
                                        return nullptr;
                                    bool missed = false;
                                    for (auto b : bits)
                                    {
                                        ref const bit{ b };
                                        if (contains(valuesList.get(), b) != 1)
                                        {
                                            missed = true;
                                            missingValue = op(NEOS_PY_OR, missingValue.get(), b);
                                        }
                                    }
                                    if (missed)
                                        missingNames.push_back(text_of(item.first));
                                }
                                if (!missingNames.empty())
                                {
                                    std::string alias;
                                    if (missingNames.size() == 1u)
                                        alias = "alias " + missingNames[0] + " is missing";
                                    else
                                    {
                                        std::string list2;
                                        for (std::size_t i = 0u; i + 1u < missingNames.size(); ++i)
                                            list2 += (i == 0u ? "" : ", ") + missingNames[i];
                                        alias = "aliases " + list2 + " and " + missingNames.back() + " are missing";
                                    }
                                    long long mv = 0;
                                    to_index(missingValue.get(), mv);
                                    char hex[32];
                                    std::snprintf(hex, sizeof(hex), "0x%llx", static_cast<unsigned long long>(mv));
                                    std::string const value = is_single_bit(missingValue.get()) ? std::string{ "value " } + hex : std::string{ "combined values of " } + hex;
                                    return raise("ValueError", "invalid Flag " + repr_text(ref{ make_str(clsName) }.get()) + ": " + alias + " " + value +
                                        " [use enum.show_flag_values(value) for details]");
                                }
                            }
                        }
                        return new_reference(enumeration);
                    } }
            };

            std::string module_tail(object* aClass)
            {
                ref const module{ attribute(aClass, "__module__") };
                auto const m = module ? text_of(module.get()) : std::string{};
                return m.substr(m.rfind('.') + 1u);
            }

            builtin_entry const sEnumFunctions[] =
            {
                { "unique", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 1u)
                            return raise("TypeError", "unique() takes 1 positional argument");
                        return unique_check(aArgs[0], "duplicate values found in ");
                    } },
                { "show_flag_values", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 1u)
                            return raise("TypeError", "show_flag_values() takes 1 positional argument");
                        std::vector<object*> bits;
                        if (!bits_of(aArgs[0], bits))
                            return nullptr;
                        return make_list(std::move(bits));
                    } },
                { "bin", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("bin", aArgs, aKwargs, { "num", "max_bits" }, 1u, 0u, a))
                            return nullptr;
                        auto const text = flag_bin(a[0], a[1]);
                        return failed() ? nullptr : make_str(text);
                    } },
                { "global_enum_repr", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const cls{ class_of(aArgs[0]) };
                        ref const name{ attribute(aArgs[0], "_name_") };
                        return name ? make_str(module_tail(cls.get()) + "." + text_of(name.get())) : nullptr;
                    } },
                { "global_flag_repr", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const self = aArgs[0];
                        ref const cls{ class_of(self) };
                        auto const module = module_tail(cls.get());
                        ref const name{ attribute(self, "_name_") };
                        ref const value{ attribute(self, "_value_") };
                        if (!name || !value)
                            return nullptr;
                        if (is_none(name.get()))
                            return make_str(module + "." + class_name(cls.get()) + "(" + repr_text(value.get()) + ")");
                        if (is_single_bit(builtin_value(value.get())))
                            return make_str(module + "." + text_of(name.get()));
                        ref const boundary{ attribute_or_none(cls.get(), "_boundary_") };
                        std::string result;
                        std::size_t start = 0u;
                        auto const names = text_of(name.get());
                        for (;;)
                        {
                            auto const bar = names.find('|', start);
                            auto const n = names.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
                            if (!result.empty())
                                result += "|";
                            result += boundary.get() == s.keep && !n.empty() && std::isdigit(static_cast<unsigned char>(n[0])) ? n : module + "." + n;
                            if (bar == std::string::npos)
                                break;
                            start = bar + 1u;
                        }
                        return make_str(result);
                    } },
                { "global_str", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const name{ attribute(aArgs[0], "_name_") };
                        if (!name)
                            return nullptr;
                        if (is_none(name.get()))
                        {
                            ref const cls{ class_of(aArgs[0]) };
                            ref const value{ attribute(aArgs[0], "_value_") };
                            return make_str(class_name(cls.get()) + "(" + repr_text(value.get()) + ")");
                        }
                        return new_reference(name.get());
                    } },
                { "global_enum", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("global_enum", aArgs, aKwargs, { "cls", "update_str" }, 1u, 0u, a))
                            return nullptr;
                        auto const cls = a[0];
                        bool const isFlag = subclass_of(cls, s.flag);
                        ref const repr{ module_attribute("enum", isFlag ? "global_flag_repr" : "global_enum_repr") };
                        if (!repr || class_setattr_raw(cls, "__repr__", repr.get()) < 0)
                            return nullptr;
                        if (!subclass_of(cls, s.reprEnum) || (a[1] != nullptr && truth(a[1]) == 1))
                        {
                            ref const str{ module_attribute("enum", "global_str") };
                            if (!str || class_setattr_raw(cls, "__str__", str.get()) < 0)
                                return nullptr;
                        }
                        // (the members: names of the module (its namespace))
                        ref const module{ attribute(cls, "__module__") };
                        ref const modules{ module_attribute("sys", "modules") };
                        if (module && modules)
                            if (auto const m = dict_item(modules.get(), module.get()))
                            {
                                ref const members{ attribute(cls, "__members__") };
                                ref const memberMap{ attribute(cls, "_member_map_") };
                                if (memberMap)
                                    for (auto const& item : as<dict_object>(memberMap.get())->items)
                                        setattr(m, text_of(item.first).c_str(), item.second);
                            }
                        clear_error();
                        return new_reference(cls);
                    } },
                { "pickle_by_global_name", [](std::vector<object*> const& aArgs, object*) -> object* { return getattr(aArgs[0], "name"); } },
                { "pickle_by_enum_name", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const cls{ class_of(aArgs[0]) };
                        ref const name{ attribute(aArgs[0], "_name_") };
                        ref const getattrFunction{ fn_builtin("getattr") };
                        return name ? make_tuple({ new_reference(getattrFunction.get()), make_tuple({ new_reference(cls.get()), new_reference(name.get()) }) }) : nullptr;
                    } },
                { "_dataclass_repr", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const self = aArgs[0];
                        ref const fields{ attribute(self, "__dataclass_fields__") };
                        if (!fields)
                            return nullptr;
                        std::string result;
                        for (auto const& item : as<dict_object>(fields.get())->items)
                        {
                            if (attribute_truth(item.second, "repr") != 1)
                                continue;
                            ref const v{ getattr(self, text_of(item.first).c_str()) };
                            if (!v)
                                return nullptr;
                            result += (result.empty() ? "" : ", ") + text_of(item.first) + "=" + repr_text(v.get());
                        }
                        return make_str(result);
                    } }
            };

            // --- making the module's enumerations

            void add_methods(object* aDict, builtin_entry const* aBegin, builtin_entry const* aEnd, object* (*aWrap)(std::vector<object*> const&, object*) = nullptr)
            {
                for (auto m = aBegin; m != aEnd; ++m)
                {
                    ref const f{ make_builtin(*m) };
                    ref const v{ aWrap != nullptr ? aWrap({ f.get() }, nullptr) : new_reference(f.get()) };
                    dict_put(aDict, m->name, v.get());
                }
            }

            // (an enumeration of the module: made by EnumType (its namespace's methods set directly, its members through EnumDict))
            object* make_enum(char const* aName, std::vector<object*> const& aBases, std::function<void(object*)> const& aMethods,
                std::vector<std::pair<char const*, object*>> const& aMembers, object* aBoundary = nullptr)
            {
                std::vector<object*> bases;
                for (auto b : aBases)
                    bases.push_back(new_reference(b));
                ref const basesTuple{ make_tuple(std::move(bases)) };
                ref const prepare{ attribute(s.enumType, "__prepare__") };
                ref const ns{ prepare ? invoke(prepare.get(), { make_str(aName), new_reference(basesTuple.get()) }) : nullptr };
                if (!ns)
                    return nullptr;
                auto const storage = builtin_value(ns.get());
                dict_put(storage, "__module__", ref{ make_str("enum") }.get());
                dict_put(storage, "__qualname__", ref{ make_str(aName) }.get());
                aMethods(storage);
                for (auto const& m : aMembers)
                {
                    ref const r{ call_method_named(ns.get(), "__setitem__", { make_str(m.first), new_reference(m.second) }) };
                    if (!r)
                        return nullptr;
                }
                ref const kw{ keywords({ { "boundary", aBoundary } }) };
                return call(s.enumType, ref{ make_tuple({ make_str(aName), new_reference(basesTuple.get()), new_reference(ns.get()) }) }.get(), kw.get());
            }

            bool enum_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                static builtin_entry const sReprs[] =
                {
                    { "__repr__", [](std::vector<object*> const&, object*) -> object* { return make_str("<not given>"); } }
                };
                static builtin_entry const sAutoNullRepr[] =
                {
                    { "__repr__", [](std::vector<object*> const&, object*) -> object* { return make_str("_auto_null"); } }
                };
                ref const notGivenClass{ make_class_of("enum", "_not_given", {}, sReprs) };
                ref const autoNullClass{ make_class_of("enum", "_auto_null", {}, sAutoNullRepr) };
                if (!notGivenClass || !autoNullClass)
                    return false;
                s.notGiven = call(notGivenClass.get(), nullptr, nullptr);
                s.autoNull = call(autoNullClass.get(), nullptr, nullptr);
                s.autoClass = make_class_of("enum", "auto", {}, sAutoMethods);
                s.memberClass = make_class_of("enum", "member", {}, sMemberMethods);
                s.nonmemberClass = make_class_of("enum", "nonmember", {}, sMemberMethods);
                {
                    ref const dynamic{ module_attribute("types", "DynamicClassAttribute") };
                    if (!dynamic)
                        return false;
                    s.propertyClass = make_class_of("enum", "property", { dynamic.get() }, sPropertyMethods, { { "member", new_reference(none()) },
                        { "_attr_type", new_reference(none()) }, { "_cls_type", new_reference(none()) } });
                }
                {
                    ref const dictType{ type_object_for("dict", "dict") };
                    static builtin_entry const sMemberNames{ "member_names", enum_dict_member_names };
                    ref const getter{ make_builtin(sMemberNames) };
                    s.enumDict = make_class_of("enum", "EnumDict", { dictType.get() }, sEnumDictMethods, { { "member_names", b_property({ getter.get() }, nullptr) } });
                }
                {
                    ref const typeType{ type_object_for("type", "type") };
                    std::vector<std::pair<std::string, object*>> extra;
                    for (auto const& m : sEnumTypeClassMethods)
                    {
                        ref const f{ make_builtin(m) };
                        extra.emplace_back(m.name, b_classmethod({ f.get() }, nullptr));
                    }
                    static builtin_entry const sMembers{ "__members__", [](std::vector<object*> const& aArgs, object*) -> object*
                        {
                            ref const map{ attribute(aArgs[0], "_member_map_") };
                            return map ? make_mapping_proxy(map.get()) : nullptr;
                        } };
                    ref const membersGetter{ make_builtin(sMembers) };
                    extra.emplace_back("__members__", b_property({ membersGetter.get() }, nullptr));
                    s.enumType = make_class_of("enum", "EnumType", { typeType.get() }, sEnumTypeMethods, std::move(extra));
                }
                if (s.autoClass == nullptr || s.memberClass == nullptr || s.nonmemberClass == nullptr || s.propertyClass == nullptr || s.enumDict == nullptr || s.enumType == nullptr)
                    return false;
                // (Enum)
                s.enumNew = make_builtin(sEnumMethods[0]);
                auto const enumMethods = [](object* aDict)
                {
                    add_methods(aDict, std::begin(sEnumMethods), std::end(sEnumMethods));
                    add_methods(aDict, std::begin(sEnumStatic), std::end(sEnumStatic), b_staticmethod);
                    add_methods(aDict, std::begin(sEnumClassMethods), std::end(sEnumClassMethods), b_classmethod);
                    dict_put(aDict, "__new__", s.enumNew);
                    dict_put(aDict, "name", ref{ b_property({ ref{ make_builtin(sEnumName) }.get() }, nullptr) }.get());
                    dict_put(aDict, "value", ref{ b_property({ ref{ make_builtin(sEnumValue) }.get() }, nullptr) }.get());
                    dict_put(aDict, "__doc__", ref{ make_str("Create a collection of name/value pairs.") }.get());
                };
                s.enumClass = make_enum("Enum", {}, enumMethods, {});
                if (s.enumClass == nullptr)
                    return false;
                s.reprEnum = make_enum("ReprEnum", { s.enumClass }, [](object* aDict)
                    {
                        dict_put(aDict, "__doc__", ref{ make_str("Only changes the repr(), leaving str() and format() to the mixed-in type.") }.get());
                    }, {});
                ref const intType{ type_object_for("int", "int") };
                ref const strType{ type_object_for("str", "str") };
                ref const intEnum{ make_enum("IntEnum", { intType.get(), s.reprEnum }, [](object* aDict)
                    {
                        dict_put(aDict, "__doc__", ref{ make_str("Enum where members are also (and must be) ints") }.get());
                    }, {}) };
                ref const strEnum{ make_enum("StrEnum", { strType.get(), s.reprEnum }, [](object* aDict)
                    {
                        add_methods(aDict, std::begin(sStrEnumMethods), std::end(sStrEnumMethods));
                        add_methods(aDict, std::begin(sStrEnumStatic), std::end(sStrEnumStatic), b_staticmethod);
                        dict_put(aDict, "__doc__", ref{ make_str("Enum where members are also (and must be) strings") }.get());
                    }, {}) };
                if (!intEnum || !strEnum)
                    return false;
                // (FlagBoundary: STRICT, CONFORM, EJECT, KEEP)
                std::vector<ref> autos;
                for (int i = 0; i < 4; ++i)
                    autos.emplace_back(call(s.autoClass, nullptr, nullptr));
                ref const flagBoundary{ make_enum("FlagBoundary", { strEnum.get() }, [](object*) {}, { { "STRICT", autos[0].get() }, { "CONFORM", autos[1].get() },
                    { "EJECT", autos[2].get() }, { "KEEP", autos[3].get() } }) };
                if (!flagBoundary)
                    return false;
                s.strict = getattr(flagBoundary.get(), "STRICT");
                s.conform = getattr(flagBoundary.get(), "CONFORM");
                s.eject = getattr(flagBoundary.get(), "EJECT");
                s.keep = getattr(flagBoundary.get(), "KEEP");
                // (Flag(Enum, boundary=STRICT) and IntFlag(int, ReprEnum, Flag, boundary=KEEP))
                auto const flagMethods = [](object* aDict)
                {
                    add_methods(aDict, std::begin(sFlagMethods), std::end(sFlagMethods));
                    add_methods(aDict, std::begin(sFlagStatic), std::end(sFlagStatic), b_staticmethod);
                    add_methods(aDict, std::begin(sFlagClassMethods), std::end(sFlagClassMethods), b_classmethod);
                    ref const byValue{ dict_item(aDict, "_iter_member_by_value_") != nullptr ? new_reference(dict_item(aDict, "_iter_member_by_value_")) : nullptr };
                    if (byValue)
                        dict_put(aDict, "_iter_member_", byValue.get());
                    dict_put(aDict, "_numeric_repr_", ref{ fn_builtin("repr") }.get());
                    dict_put(aDict, "__doc__", ref{ make_str("Support for flags") }.get());
                };
                s.flag = make_enum("Flag", { s.enumClass }, flagMethods, {}, s.strict);
                if (s.flag == nullptr)
                    return false;
                ref const intFlag{ make_enum("IntFlag", { intType.get(), s.reprEnum, s.flag }, [](object* aDict)
                    {
                        dict_put(aDict, "__doc__", ref{ make_str("Support for integer-based Flags") }.get());
                    }, {}, s.keep) };
                if (!intFlag)
                    return false;
                // (EnumCheck(StrEnum): CONTINUOUS, NAMED_FLAGS, UNIQUE)
                ref const continuousValue{ make_str("no skipped integer values") };
                ref const namedFlagsValue{ make_str("multi-flag aliases may not contain unnamed flags") };
                ref const uniqueValue{ make_str("one name per value") };
                ref const enumCheck{ make_enum("EnumCheck", { strEnum.get() }, [](object* aDict)
                    {
                        dict_put(aDict, "__doc__", ref{ make_str("various conditions to check an enumeration for") }.get());
                    }, { { "CONTINUOUS", continuousValue.get() }, { "NAMED_FLAGS", namedFlagsValue.get() }, { "UNIQUE", uniqueValue.get() } }) };
                if (!enumCheck)
                    return false;
                s.continuous = getattr(enumCheck.get(), "CONTINUOUS");
                s.namedFlags = getattr(enumCheck.get(), "NAMED_FLAGS");
                s.unique = getattr(enumCheck.get(), "UNIQUE");
                ref const verify{ make_class_of("enum", "verify", {}, sVerifyMethods) };
                aAttributes.emplace_back("EnumType", new_reference(s.enumType));
                aAttributes.emplace_back("EnumMeta", new_reference(s.enumType));
                aAttributes.emplace_back("EnumDict", new_reference(s.enumDict));
                aAttributes.emplace_back("_EnumDict", new_reference(s.enumDict));
                aAttributes.emplace_back("Enum", new_reference(s.enumClass));
                aAttributes.emplace_back("ReprEnum", new_reference(s.reprEnum));
                aAttributes.emplace_back("IntEnum", new_reference(intEnum.get()));
                aAttributes.emplace_back("StrEnum", new_reference(strEnum.get()));
                aAttributes.emplace_back("Flag", new_reference(s.flag));
                aAttributes.emplace_back("IntFlag", new_reference(intFlag.get()));
                aAttributes.emplace_back("FlagBoundary", new_reference(flagBoundary.get()));
                aAttributes.emplace_back("STRICT", new_reference(s.strict));
                aAttributes.emplace_back("CONFORM", new_reference(s.conform));
                aAttributes.emplace_back("EJECT", new_reference(s.eject));
                aAttributes.emplace_back("KEEP", new_reference(s.keep));
                aAttributes.emplace_back("EnumCheck", new_reference(enumCheck.get()));
                aAttributes.emplace_back("CONTINUOUS", new_reference(s.continuous));
                aAttributes.emplace_back("NAMED_FLAGS", new_reference(s.namedFlags));
                aAttributes.emplace_back("UNIQUE", new_reference(s.unique));
                aAttributes.emplace_back("auto", new_reference(s.autoClass));
                aAttributes.emplace_back("member", new_reference(s.memberClass));
                aAttributes.emplace_back("nonmember", new_reference(s.nonmemberClass));
                aAttributes.emplace_back("property", new_reference(s.propertyClass));
                aAttributes.emplace_back("verify", new_reference(verify.get()));
                aAttributes.emplace_back("_not_given", new_reference(s.notGiven));
                aAttributes.emplace_back("_auto_null", new_reference(s.autoNull));
                for (auto const& f : sEnumFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                aAttributes.emplace_back("__all__", make_list({ make_str("EnumType"), make_str("EnumMeta"), make_str("EnumDict"), make_str("Enum"), make_str("IntEnum"),
                    make_str("StrEnum"), make_str("Flag"), make_str("IntFlag"), make_str("ReprEnum"), make_str("auto"), make_str("unique"), make_str("property"),
                    make_str("verify"), make_str("member"), make_str("nonmember"), make_str("FlagBoundary"), make_str("STRICT"), make_str("CONFORM"), make_str("EJECT"),
                    make_str("KEEP"), make_str("global_flag_repr"), make_str("global_enum_repr"), make_str("global_str"), make_str("global_enum"), make_str("EnumCheck"),
                    make_str("CONTINUOUS"), make_str("NAMED_FLAGS"), make_str("UNIQUE"), make_str("pickle_by_global_name"), make_str("pickle_by_enum_name") }));
                return !failed();
            }

            inline bool const sEnum = register_module("enum", enum_module);
        }
    }
}
