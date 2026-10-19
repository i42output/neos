/*
  typemodules.cpp

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
*/

// The modules abc and types (CPython 3.13's behaviour and messages)

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "objects.hpp"

namespace neos::python_runtime
{
    namespace detail
    {
        namespace
        {
            // --- abc

            // (the classes registered as virtual subclasses of each abstract class)
            std::map<object*, std::vector<object*>>& abc_registry()
            {
                static std::map<object*, std::vector<object*>> sRegistry;
                return sRegistry;
            }
            long long sInvalidationCounter = 0;

            bool is_type(object* aObject) { return is_class(aObject) || is_type_object(aObject); }

            bool is_abstract(object* aValue)
            {
                ref const flag{ getattr(aValue, "__isabstractmethod__") };
                if (!flag)
                {
                    clear_error();
                    return false;
                }
                auto const t = truth(flag.get());
                if (t < 0)
                    clear_error();
                return t == 1;
            }

            // (the abstract methods of a class (from its namespace's values and its bases' abstract methods): cls.__abstractmethods__)
            bool compute_abstracts(object* aClass, object* aNamespace)
            {
                std::vector<std::string> abstracts;
                auto const add = [&](std::string const& aName)
                {
                    if (std::find(abstracts.begin(), abstracts.end(), aName) == abstracts.end())
                        abstracts.push_back(aName);
                };
                if (aNamespace != nullptr)
                {
                    ref const items{ kind_of(proxied_mapping(aNamespace)) == NEOS_PY_DICT ? new_reference(proxied_mapping(aNamespace)) : b_dict({ aNamespace }, nullptr) };
                    if (!items)
                        return false;
                    for (auto const& item : as<dict_object>(items.get())->items)
                        if (kind_of(item.first) == NEOS_PY_STR && is_abstract(item.second))
                            add(as<str_object>(item.first)->value);
                }
                ref const bases{ getattr(aClass, "__bases__") };
                if (!bases)
                    return false;
                for (auto base : as<tuple_object>(bases.get())->items)
                {
                    ref const names{ getattr(base, "__abstractmethods__") };
                    if (!names)
                    {
                        clear_error();
                        continue;
                    }
                    std::vector<object*> items;
                    if (!collect(names.get(), items))
                        return false;
                    for (auto n : items)
                    {
                        ref const name{ n };
                        if (kind_of(n) != NEOS_PY_STR)
                            continue;
                        ref const value{ getattr(aClass, as<str_object>(n)->value.c_str()) };
                        if (!value)
                        {
                            clear_error();
                            continue;
                        }
                        if (is_abstract(value.get()))
                            add(as<str_object>(n)->value);
                    }
                }
                std::vector<object*> names;
                for (auto const& a : abstracts)
                    names.push_back(make_str(a));
                ref const list{ make_list(std::move(names)) };
                ref const set{ b_frozenset({ list.get() }, nullptr) };
                return set && setattr(aClass, "__abstractmethods__", set.get()) >= 0;
            }

            int abc_subclass_check(object* aClass, object* aSubclass);

            // (issubclass(subclass, cls) without cls's metaclass's check (the classes' MROs) and then with it)
            int plain_subclass(object* aSubclass, object* aBase)
            {
                if (is_class(aSubclass) && is_class(aBase))
                    return is_subclass(aSubclass, aBase) ? 1 : 0;
                ref const r{ b_issubclass({ aSubclass, aBase }, nullptr) };
                return r ? truth(r.get()) : -1;
            }

            int abc_subclass_check(object* aClass, object* aSubclass)
            {
                if (!is_type(aSubclass))
                    return (error("TypeError", "issubclass() arg 1 must be a class"), -1);
                // (its __subclasshook__)
                ref const hook{ getattr(aClass, "__subclasshook__") };
                if (!hook)
                    return -1;
                ref const ok{ invoke(hook.get(), { new_reference(aSubclass) }) };
                if (!ok)
                    return -1;
                if (kind_of(ok.get()) != NEOS_PY_NOT_IMPLEMENTED)
                    return truth(ok.get());
                // (a subclass)
                if (is_class(aSubclass) && is_subclass(aSubclass, aClass))
                    return 1;
                // (a subclass of a registered class)
                auto const registered = abc_registry().find(aClass);
                if (registered != abc_registry().end())
                    for (auto r : std::vector<object*>{ registered->second })
                    {
                        ref const r2{ b_issubclass({ aSubclass, r }, nullptr) };
                        if (!r2)
                            return -1;
                        if (truth(r2.get()) == 1)
                            return 1;
                    }
                // (a subclass of a subclass)
                for (auto s : class_subclasses(aClass))
                {
                    ref const r2{ b_issubclass({ aSubclass, s }, nullptr) };
                    if (!r2)
                        return -1;
                    if (truth(r2.get()) == 1)
                        return 1;
                }
                return 0;
            }

            builtin_entry const sAbcMetaMethods[] =
            {
                { "__new__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        // (mcls, name, bases, namespace, /, **kwargs)
                        if (aArgs.size() != 4u)
                            return error("TypeError", "ABCMeta.__new__() missing required arguments: 'name', 'bases' and 'namespace'");
                        ref const args{ make_tuple({ new_reference(aArgs[0]), new_reference(aArgs[1]), new_reference(aArgs[2]), new_reference(aArgs[3]) }) };
                        ref cls{ fn_type_new(nullptr, nullptr, args.get(), aKwargs) };
                        if (!cls || !compute_abstracts(cls.get(), aArgs[3]))
                            return nullptr;
                        return cls.release_ownership();
                    } },
                { "register", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("register", aArgs, aKwargs, { "subclass" }, 1u, 1u, a))
                            return nullptr;
                        auto const cls = aArgs[0];
                        auto const subclass = a[0];
                        if (!is_type(subclass))
                            return error("TypeError", "Can only register classes");
                        if (plain_subclass(subclass, cls) == 1)
                            return new_reference(subclass);     // (already a subclass)
                        clear_error();
                        // (a cycle: cls a subclass of subclass)
                        ref const cycle{ b_issubclass({ cls, subclass }, nullptr) };
                        if (!cycle)
                            return nullptr;
                        if (truth(cycle.get()) == 1)
                            return error("RuntimeError", "Refusing to create an inheritance cycle");
                        abc_registry()[cls].push_back(new_reference(subclass));
                        ++sInvalidationCounter;
                        return new_reference(subclass);
                    } },
                { "__instancecheck__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("__instancecheck__", aArgs, aKwargs, { "instance" }, 1u, 1u, a))
                            return nullptr;
                        auto const cls = aArgs[0];
                        ref const subclass{ getattr(a[0], "__class__") };
                        ref const subtype{ class_of(a[0]) };
                        if (!subclass)
                            clear_error();
                        for (auto c : { subclass.get(), subtype.get() })
                        {
                            if (c == nullptr || !is_type(c))
                                continue;
                            auto const r = abc_subclass_check(cls, c);
                            if (r < 0)
                                return nullptr;
                            if (r == 1)
                                return make_bool(true);
                            if (subclass.get() == subtype.get())
                                break;
                        }
                        return make_bool(false);
                    } },
                { "__subclasscheck__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("__subclasscheck__", aArgs, aKwargs, { "subclass" }, 1u, 1u, a))
                            return nullptr;
                        auto const r = abc_subclass_check(aArgs[0], a[0]);
                        return r < 0 ? nullptr : make_bool(r == 1);
                    } },
                { "_dump_registry", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("_dump_registry", aArgs, aKwargs, { "file" }, 0u, 0u, a))
                            return nullptr;
                        auto const cls = aArgs[0];
                        std::string text = "Class: " + text_of(ref{ getattr(cls, "__module__") }.get()) + "." + text_of(ref{ getattr(cls, "__qualname__") }.get()) + "\n";
                        text += "Inv. counter: " + std::to_string(sInvalidationCounter) + "\n";
                        std::string registry = "set()";
                        if (auto const r = abc_registry().find(cls); r != abc_registry().end() && !r->second.empty())
                        {
                            registry = "{";
                            for (std::size_t i = 0u; i < r->second.size(); ++i)
                                registry += (i == 0u ? "" : ", ") + repr_text(r->second[i]);
                            registry += "}";
                        }
                        text += "_abc_registry: " + registry + "\n_abc_cache: set()\n_abc_negative_cache: set()\n_abc_negative_cache_version: " + std::to_string(sInvalidationCounter) + "\n";
                        ref const file{ a[0] != nullptr && kind_of(a[0]) != NEOS_PY_NONE ? new_reference(a[0]) : standard_stream(1) };
                        ref const r{ file ? call_method_named(file.get(), "write", { make_str(text) }) : nullptr };
                        return r ? new_reference(none()) : nullptr;
                    } },
                { "_abc_registry_clear", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (auto const r = abc_registry().find(aArgs[0]); r != abc_registry().end())
                        {
                            for (auto c : r->second)
                                release(c);
                            abc_registry().erase(r);
                        }
                        return new_reference(none());
                    } },
                { "_abc_caches_clear", [](std::vector<object*> const&, object*) -> object* { return new_reference(none()); } }
            };

            object* sAbcMeta = nullptr;
            object* abc_meta()
            {
                if (sAbcMeta == nullptr)
                {
                    ref const type{ type_object_for("type", "type") };
                    sAbcMeta = make_class_of("abc", "ABCMeta", { type.get() }, sAbcMetaMethods);
                }
                return sAbcMeta != nullptr ? new_reference(sAbcMeta) : nullptr;
            }

            builtin_entry const sAbcFunctions[] =
            {
                { "abstractmethod", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("abstractmethod", aArgs, aKwargs, { "funcobj" }, 1u, 0u, a))
                            return nullptr;
                        // (a builtin function: recorded as abstract)
                        if (kind_of(a[0]) == NEOS_PY_FUNCTION)
                        {
                            if (abstract_builtins().insert(a[0]).second)
                                retain(a[0]);
                            return new_reference(a[0]);
                        }
                        ref const t{ make_bool(true) };
                        if (setattr(a[0], "__isabstractmethod__", t.get()) < 0)
                            return nullptr;
                        return new_reference(a[0]);
                    } },
                { "get_cache_token", [](std::vector<object*> const&, object*) -> object* { return make_int(sInvalidationCounter); } },
                { "update_abstractmethods", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("update_abstractmethods", aArgs, aKwargs, { "cls" }, 1u, 0u, a))
                            return nullptr;
                        auto const cls = a[0];
                        ref const existing{ getattr(cls, "__abstractmethods__") };
                        if (!existing)
                        {
                            // (not an abstract class: nothing to update)
                            clear_error();
                            return new_reference(cls);
                        }
                        ref const ns{ getattr(cls, "__dict__") };
                        if (!ns || !compute_abstracts(cls, ns.get()))
                            return nullptr;
                        return new_reference(cls);
                    } }
            };

            // (abstractclassmethod, abstractstaticmethod, abstractproperty: classmethod, staticmethod and property whose function is abstract)
            object* abstract_wrapper(std::vector<object*> const& aArgs, object* aKwargs, object* (*aMake)(std::vector<object*> const&, object*))
            {
                if (!aArgs.empty())
                {
                    ref const t{ make_bool(true) };
                    if (setattr(aArgs[0], "__isabstractmethod__", t.get()) < 0)
                        clear_error();
                }
                return aMake(aArgs, aKwargs);
            }

            builtin_entry const sAbcWrappers[] =
            {
                { "abstractclassmethod", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return abstract_wrapper(aArgs, aKwargs, b_classmethod); } },
                { "abstractstaticmethod", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return abstract_wrapper(aArgs, aKwargs, b_staticmethod); } },
                { "abstractproperty", [](std::vector<object*> const& aArgs, object* aKwargs) -> object* { return abstract_wrapper(aArgs, aKwargs, b_property); } }
            };

            bool abc_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                ref const meta{ abc_meta() };
                if (!meta)
                    return false;
                // (ABC: class ABC(metaclass=ABCMeta): __slots__ = ())
                ref const ns{ make_dict() };
                dict_set(ns.get(), ref{ make_str("__slots__") }.get(), ref{ make_tuple() }.get());
                dict_set(ns.get(), ref{ make_str("__module__") }.get(), ref{ make_str("abc") }.get());
                dict_set(ns.get(), ref{ make_str("__doc__") }.get(), ref{ make_str("Helper class that provides a standard way to create an ABC using\ninheritance.") }.get());
                ref const abc{ invoke(meta.get(), { make_str("ABC"), make_tuple(), new_reference(ns.get()) }) };
                if (!abc)
                    return false;
                aAttributes.emplace_back("ABCMeta", new_reference(meta.get()));
                aAttributes.emplace_back("ABC", new_reference(abc.get()));
                for (auto const& f : sAbcFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                for (auto const& f : sAbcWrappers)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                return true;
            }

            // --- types

            // (SimpleNamespace(mapping_or_iterable=(), /, **kwargs))
            builtin_entry const sSimpleNamespaceMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aArgs.size() > 2u)
                            return error("TypeError", "SimpleNamespace expected at most 1 positional argument, got " + std::to_string(aArgs.size() - 1u));
                        auto const self = aArgs[0];
                        auto const set = [&](object* aKey, object* aValue) -> bool
                        {
                            if (kind_of(aKey) != NEOS_PY_STR)
                                return (error("TypeError", std::string{ "keywords must be strings, not '" } + type_name(aKey) + "'"), false);
                            return setattr(self, as<str_object>(aKey)->value.c_str(), aValue) >= 0;
                        };
                        if (aArgs.size() == 2u)
                        {
                            ref const d{ b_dict({ aArgs[1] }, nullptr) };
                            if (!d)
                                return nullptr;
                            for (auto const& item : as<dict_object>(d.get())->items)
                                if (!set(item.first, item.second))
                                    return nullptr;
                        }
                        if (aKwargs != nullptr)
                            for (auto const& item : as<dict_object>(aKwargs)->items)
                                if (!set(item.first, item.second))
                                    return nullptr;
                        return new_reference(none());
                    } },
                { "__repr__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        auto const self = aArgs[0];
                        // (a recursive namespace: ...)
                        static thread_local std::vector<object*> tRunning;
                        ref const cls{ class_of(self) };
                        ref const name{ getattr(cls.get(), "__name__") };
                        std::string const shown = name && text_of(name.get()) != "SimpleNamespace" ? text_of(name.get()) : std::string{ "namespace" };
                        if (std::find(tRunning.begin(), tRunning.end(), self) != tRunning.end())
                            return make_str(shown + "(...)");
                        tRunning.push_back(self);
                        ref const d{ getattr(self, "__dict__") };
                        std::string text = shown + "(";
                        bool ok = static_cast<bool>(d);
                        if (ok)
                        {
                            bool first = true;
                            for (auto const& item : as<dict_object>(d.get())->items)
                            {
                                std::string value;
                                if (!repr_of(item.second, value))
                                {
                                    ok = false;
                                    break;
                                }
                                text += (first ? "" : ", ") + text_of(item.first) + "=" + value;
                                first = false;
                            }
                        }
                        tRunning.pop_back();
                        return ok ? make_str(text + ")") : nullptr;
                    } },
                { "__eq__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return error("TypeError", "expected 1 argument");
                        ref const mine{ class_of(aArgs[0]) };
                        ref const theirs{ class_of(aArgs[1]) };
                        ref const simple{ module_attribute("types", "SimpleNamespace") };
                        auto const isNamespace = [&](object* aClass) { return is_class(aClass) && simple && is_subclass(aClass, simple.get()); };
                        if (!isNamespace(mine.get()) || !isNamespace(theirs.get()))
                            return not_implemented();
                        ref const a{ getattr(aArgs[0], "__dict__") };
                        ref const b{ getattr(aArgs[1], "__dict__") };
                        if (!a || !b)
                            return nullptr;
                        return compare(NEOS_PY_EQ, a.get(), b.get());
                    } },
                { "__reduce__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const cls{ class_of(aArgs[0]) };
                        ref const d{ getattr(aArgs[0], "__dict__") };
                        if (!d)
                            return nullptr;
                        return make_tuple({ new_reference(cls.get()), make_tuple(), new_reference(d.get()) });
                    } },
                { "__replace__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        if (aArgs.size() != 1u)
                            return error("TypeError", "__replace__() takes no positional arguments");
                        ref const cls{ class_of(aArgs[0]) };
                        ref const d{ getattr(aArgs[0], "__dict__") };
                        ref const merged{ make_dict() };
                        if (!d || !dict_update(merged.get(), d.get()) || (aKwargs != nullptr && !dict_update(merged.get(), aKwargs)))
                            return nullptr;
                        return call(cls.get(), nullptr, merged.get());
                    } }
            };

            // (DynamicClassAttribute(fget=None, fset=None, fdel=None, doc=None): a property of instances whose class's attribute is its
            // class's __getattr__'s)
            builtin_entry const sDynamicClassAttributeMethods[] =
            {
                { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!self_arguments("DynamicClassAttribute", aArgs, aKwargs, { "fget", "fset", "fdel", "doc" }, 0u, 0u, a))
                            return nullptr;
                        auto const self = aArgs[0];
                        for (std::size_t i = 0u; i < 3u; ++i)
                            if (setattr(self, std::vector<char const*>{ "fget", "fset", "fdel" }[i], a[i] != nullptr ? a[i] : none()) < 0)
                                return nullptr;
                        ref doc{ a[3] != nullptr ? new_reference(a[3]) : nullptr };
                        if (!doc || kind_of(doc.get()) == NEOS_PY_NONE)
                        {
                            doc = a[0] != nullptr ? ref{ getattr(a[0], "__doc__") } : ref{ new_reference(none()) };
                            if (!doc)
                            {
                                clear_error();
                                doc = ref{ new_reference(none()) };
                            }
                        }
                        ref const overwrite{ make_bool(a[3] == nullptr || kind_of(a[3]) == NEOS_PY_NONE) };
                        ref const abstract{ make_bool(a[0] != nullptr && is_abstract(a[0])) };
                        if (setattr(self, "__doc__", doc.get()) < 0 || setattr(self, "overwrite_doc", overwrite.get()) < 0 || setattr(self, "__isabstractmethod__", abstract.get()) < 0)
                            return nullptr;
                        return new_reference(none());
                    } },
                { "__get__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() < 2u)
                            return error("TypeError", "__get__() expected at least 1 argument");
                        auto const self = aArgs[0];
                        if (kind_of(aArgs[1]) == NEOS_PY_NONE)
                        {
                            if (attribute_truth(self, "__isabstractmethod__") == 1)
                                return new_reference(self);
                            return error("AttributeError", "");
                        }
                        ref const fget{ getattr(self, "fget") };
                        if (!fget)
                            return nullptr;
                        if (kind_of(fget.get()) == NEOS_PY_NONE)
                            return error("AttributeError", "unreadable attribute");
                        return invoke(fget.get(), { new_reference(aArgs[1]) });
                    } },
                { "__set__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 3u)
                            return error("TypeError", "__set__() expected 2 arguments");
                        ref const fset{ getattr(aArgs[0], "fset") };
                        if (!fset)
                            return nullptr;
                        if (kind_of(fset.get()) == NEOS_PY_NONE)
                            return error("AttributeError", "can't set attribute");
                        ref const r{ invoke(fset.get(), { new_reference(aArgs[1]), new_reference(aArgs[2]) }) };
                        return r ? new_reference(none()) : nullptr;
                    } },
                { "__delete__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return error("TypeError", "__delete__() expected 1 argument");
                        ref const fdel{ getattr(aArgs[0], "fdel") };
                        if (!fdel)
                            return nullptr;
                        if (kind_of(fdel.get()) == NEOS_PY_NONE)
                            return error("AttributeError", "can't delete attribute");
                        ref const r{ invoke(fdel.get(), { new_reference(aArgs[1]) }) };
                        return r ? new_reference(none()) : nullptr;
                    } },
                { "getter", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return error("TypeError", "getter() expected 1 argument");
                        ref const cls{ class_of(aArgs[0]) };
                        ref const fset{ getattr(aArgs[0], "fset") };
                        ref const fdel{ getattr(aArgs[0], "fdel") };
                        ref const doc{ getattr(aArgs[0], "__doc__") };
                        ref const overwrite{ getattr(aArgs[0], "overwrite_doc") };
                        if (!fset || !fdel || !doc || !overwrite)
                            return nullptr;
                        ref const fdoc{ truth(overwrite.get()) == 1 ? getattr(aArgs[1], "__doc__") : new_reference(doc.get()) };
                        if (!fdoc)
                            clear_error();
                        ref const result{ invoke(cls.get(), { new_reference(aArgs[1]), new_reference(fset.get()), new_reference(fdel.get()),
                            new_reference(fdoc ? fdoc.get() : none()) }) };
                        if (result)
                            setattr(result.get(), "overwrite_doc", overwrite.get());
                        return result ? new_reference(result.get()) : nullptr;
                    } },
                { "setter", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return error("TypeError", "setter() expected 1 argument");
                        ref const cls{ class_of(aArgs[0]) };
                        ref const fget{ getattr(aArgs[0], "fget") };
                        ref const fdel{ getattr(aArgs[0], "fdel") };
                        ref const doc{ getattr(aArgs[0], "__doc__") };
                        if (!fget || !fdel || !doc)
                            return nullptr;
                        return invoke(cls.get(), { new_reference(fget.get()), new_reference(aArgs[1]), new_reference(fdel.get()), new_reference(doc.get()) });
                    } },
                { "deleter", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        if (aArgs.size() != 2u)
                            return error("TypeError", "deleter() expected 1 argument");
                        ref const cls{ class_of(aArgs[0]) };
                        ref const fget{ getattr(aArgs[0], "fget") };
                        ref const fset{ getattr(aArgs[0], "fset") };
                        ref const doc{ getattr(aArgs[0], "__doc__") };
                        if (!fget || !fset || !doc)
                            return nullptr;
                        return invoke(cls.get(), { new_reference(fget.get()), new_reference(fset.get()), new_reference(aArgs[1]), new_reference(doc.get()) });
                    } }
            };

            // (types.MethodType(function, instance): a bound method)
            object* method_type_call(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                auto const& args = as<tuple_object>(aArgs)->items;
                if (aKwargs != nullptr && !as<dict_object>(aKwargs)->items.empty())
                    return error("TypeError", "method() takes no keyword arguments");
                if (args.size() != 2u)
                    return error("TypeError", "method expected 2 arguments, got " + std::to_string(args.size()));
                if (!is_callable(args[0]))
                    return error("TypeError", "first argument must be callable");
                if (kind_of(args[1]) == NEOS_PY_NONE)
                    return error("TypeError", "instance must not be None");
                return make_bound_method(args[0], args[1]);
            }

            // (types.ModuleType(name, doc=None))
            object* module_type_call(neos_py_context*, void*, object* aArgs, object* aKwargs)
            {
                std::vector<object*> a;
                if (!parse_arguments("module", as<tuple_object>(aArgs)->items, aKwargs, { "name", "doc" }, 1u, 0u, a))
                    return nullptr;
                auto const name = str_of_value(a[0]);
                if (name == nullptr)
                    return error("TypeError", std::string{ "module.__init__() argument 'name' must be str, not " } + type_name(a[0]));
                ref module{ make_empty_module(*name) };
                if (!module)
                    return nullptr;
                if (setattr(module.get(), "__doc__", a[1] != nullptr ? a[1] : none()) < 0 || setattr(module.get(), "__package__", none()) < 0 ||
                    setattr(module.get(), "__loader__", none()) < 0 || setattr(module.get(), "__spec__", none()) < 0)
                    return nullptr;
                return module.release_ownership();
            }

            // (resolve_bases(bases): the bases, with __mro_entries__'s for those that are not classes)
            object* resolve_bases(object* aBases)
            {
                std::vector<object*> items;
                if (!collect(aBases, items))
                    return nullptr;
                std::vector<object*> result;
                bool changed = false;
                for (auto b : items)
                {
                    ref const base{ b };
                    if (is_type(b))
                    {
                        result.push_back(new_reference(b));
                        continue;
                    }
                    ref const entries{ getattr(b, "__mro_entries__") };
                    if (!entries)
                    {
                        clear_error();
                        result.push_back(new_reference(b));
                        continue;
                    }
                    ref const newBases{ invoke(entries.get(), { new_reference(aBases) }) };
                    if (!newBases)
                        return (std::for_each(result.begin(), result.end(), release), nullptr);
                    if (kind_of(newBases.get()) != NEOS_PY_TUPLE)
                    {
                        std::for_each(result.begin(), result.end(), release);
                        return error("TypeError", "__mro_entries__ must return a tuple");
                    }
                    changed = true;
                    for (auto n : as<tuple_object>(newBases.get())->items)
                        result.push_back(new_reference(n));
                }
                if (!changed)
                {
                    std::for_each(result.begin(), result.end(), release);
                    return kind_of(aBases) == NEOS_PY_TUPLE ? new_reference(aBases) : fn_tuple_of(aBases);
                }
                return make_tuple(std::move(result));
            }

            // (prepare_class(name, bases=(), kwds=None): (metaclass, namespace, kwds))
            object* prepare_class(object* aName, object* aBases, object* aKeywords)
            {
                ref kwds{ make_dict() };
                if (aKeywords != nullptr && kind_of(aKeywords) != NEOS_PY_NONE && !dict_update(kwds.get(), aKeywords))
                    return nullptr;
                ref meta;
                ref const metaKey{ make_str("metaclass") };
                if (auto const at = dict_find(kwds.get(), metaKey.get()); at >= 0)
                {
                    meta = ref{ new_reference(as<dict_object>(kwds.get())->items[static_cast<std::size_t>(at)].second) };
                    dict_erase(*as<dict_object>(kwds.get()), static_cast<std::size_t>(at));
                }
                else
                {
                    auto const& bases = as<tuple_object>(aBases)->items;
                    meta = bases.empty() ? ref{ type_object_for("type", "type") } : ref{ class_of(bases[0]) };
                }
                if (is_type(meta.get()))
                {
                    // (the most derived metaclass)
                    for (auto b : as<tuple_object>(aBases)->items)
                    {
                        ref const m{ class_of(b) };
                        if (is_class(m.get()) && (!is_class(meta.get()) || is_subclass(m.get(), meta.get())))
                            meta = ref{ new_reference(m.get()) };
                    }
                }
                ref ns;
                if (ref const prepare{ getattr(meta.get(), "__prepare__") })
                    ns = ref{ invoke(prepare.get(), { new_reference(aName), new_reference(aBases) }, kwds.get()) };
                else
                {
                    clear_error();
                    ns = ref{ make_dict() };
                }
                if (!ns)
                    return nullptr;
                return make_tuple({ new_reference(meta.get()), new_reference(ns.get()), new_reference(kwds.get()) });
            }

            builtin_entry const sTypesFunctions[] =
            {
                { "resolve_bases", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("resolve_bases", aArgs, aKwargs, { "bases" }, 1u, 0u, a))
                            return nullptr;
                        return resolve_bases(a[0]);
                    } },
                { "prepare_class", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("prepare_class", aArgs, aKwargs, { "name", "bases", "kwds" }, 1u, 0u, a))
                            return nullptr;
                        ref const bases{ a[1] != nullptr ? fn_tuple_of(a[1]) : make_tuple() };
                        return bases ? prepare_class(a[0], bases.get(), a[2]) : nullptr;
                    } },
                { "new_class", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("new_class", aArgs, aKwargs, { "name", "bases", "kwds", "exec_body" }, 1u, 0u, a))
                            return nullptr;
                        ref const original{ a[1] != nullptr ? fn_tuple_of(a[1]) : make_tuple() };
                        ref const bases{ original ? resolve_bases(original.get()) : nullptr };
                        ref const prepared{ bases ? prepare_class(a[0], bases.get(), a[2]) : nullptr };
                        if (!prepared)
                            return nullptr;
                        auto const& p = as<tuple_object>(prepared.get())->items;
                        auto const meta = p[0];
                        auto const ns = p[1];
                        if (a[3] != nullptr && kind_of(a[3]) != NEOS_PY_NONE)
                        {
                            ref const r{ invoke(a[3], { new_reference(ns) }) };
                            if (!r)
                                return nullptr;
                        }
                        if (bases.get() != original.get())
                        {
                            ref const key{ make_str("__orig_bases__") };
                            if (setitem(ns, key.get(), original.get()) < 0)
                                return nullptr;
                        }
                        return invoke(meta, { new_reference(a[0]), new_reference(bases.get()), new_reference(ns) }, p[2]);
                    } },
                { "get_original_bases", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("get_original_bases", aArgs, aKwargs, { "cls" }, 1u, 0u, a))
                            return nullptr;
                        if (!is_type(a[0]))
                            return error("TypeError", "Expected an instance of type, not " + std::string{ type_name(a[0]) });
                        if (is_class(a[0]))
                        {
                            ref const d{ getattr(a[0], "__dict__") };
                            ref const key{ make_str("__orig_bases__") };
                            if (d)
                                if (auto const at = dict_find(proxied_mapping(d.get()), key.get()); at >= 0)
                                    return new_reference(as<dict_object>(proxied_mapping(d.get()))->items[static_cast<std::size_t>(at)].second);
                            clear_error();
                        }
                        return getattr(a[0], "__bases__");
                    } },
                { "coroutine", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        std::vector<object*> a;
                        if (!parse_arguments("coroutine", aArgs, aKwargs, { "func" }, 1u, 0u, a))
                            return nullptr;
                        if (!is_callable(a[0]))
                            return error("TypeError", "types.coroutine() expects a callable");
                        return new_reference(a[0]);
                    } }
            };

            bool types_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                auto const type = [](char const* aName) { return type_object_for(aName, aName); };
                aAttributes.emplace_back("FunctionType", type("function"));
                aAttributes.emplace_back("LambdaType", type("function"));
                aAttributes.emplace_back("CodeType", type("code"));
                aAttributes.emplace_back("MappingProxyType", context()->type_new(context(), "mappingproxy", "mappingproxy",
                    [](neos_py_context*, void*, object* aArgs, object* aKwargs) -> object*
                    {
                        // (mappingproxy(mapping))
                        std::vector<object*> a;
                        if (!parse_arguments("mappingproxy", tuple_items(aArgs), aKwargs, { "mapping" }, 1u, 0u, a))
                            return nullptr;
                        auto const k = kind_of(a[0]);
                        if (k == NEOS_PY_LIST || k == NEOS_PY_TUPLE || !has_special_method(a[0], "keys") && k != NEOS_PY_DICT && kind_of(builtin_value(a[0])) != NEOS_PY_DICT)
                            return error("TypeError", std::string{ "mappingproxy() argument must be a mapping, not " } + type_name(a[0]));
                        return make_mapping_proxy(a[0]);
                    }, nullptr, nullptr));
                aAttributes.emplace_back("CellType", type("cell"));
                aAttributes.emplace_back("GeneratorType", type("generator"));
                aAttributes.emplace_back("CoroutineType", type("coroutine"));
                aAttributes.emplace_back("AsyncGeneratorType", type("async_generator"));
                aAttributes.emplace_back("MethodType", context()->type_new(context(), "method", "method", method_type_call, nullptr, nullptr));
                aAttributes.emplace_back("BuiltinFunctionType", type("builtin_function_or_method"));
                aAttributes.emplace_back("BuiltinMethodType", type("builtin_function_or_method"));
                aAttributes.emplace_back("WrapperDescriptorType", type("wrapper_descriptor"));
                aAttributes.emplace_back("MethodWrapperType", type("method-wrapper"));
                aAttributes.emplace_back("MethodDescriptorType", type("method_descriptor"));
                aAttributes.emplace_back("ClassMethodDescriptorType", type("classmethod_descriptor"));
                aAttributes.emplace_back("ModuleType", context()->type_new(context(), "module", "module", module_type_call, nullptr, nullptr));
                aAttributes.emplace_back("TracebackType", type("traceback"));
                aAttributes.emplace_back("FrameType", type("frame"));
                aAttributes.emplace_back("GetSetDescriptorType", type("getset_descriptor"));
                aAttributes.emplace_back("MemberDescriptorType", type("member_descriptor"));
                aAttributes.emplace_back("GenericAlias", context()->type_new(context(), "GenericAlias", "types.GenericAlias", 
                    [](neos_py_context*, void*, object* aArgs, object* aKwargs) -> object*
                    {
                        // (GenericAlias(origin, args, /))
                        if (!no_keywords(aKwargs, "GenericAlias"))
                            return nullptr;
                        auto const& args = as<tuple_object>(aArgs)->items;
                        if (args.size() != 2u)
                            return error("TypeError", "GenericAlias expected 2 arguments, got " + std::to_string(args.size()));
                        return make_generic_alias(args[0], args[1]);
                    }, nullptr, [](neos_py_context*, void*, object* aObject) -> int { return is_generic_alias(aObject) ? 1 : 0; }));
                aAttributes.emplace_back("UnionType", type_object_for("UnionType", "types.UnionType"));
                aAttributes.emplace_back("EllipsisType", type("ellipsis"));
                aAttributes.emplace_back("NoneType", type("NoneType"));
                aAttributes.emplace_back("NotImplementedType", type("NotImplementedType"));
                aAttributes.emplace_back("CapsuleType", type("PyCapsule"));
                aAttributes.emplace_back("SimpleNamespace", make_class_of("types", "SimpleNamespace", {}, sSimpleNamespaceMethods));
                aAttributes.emplace_back("DynamicClassAttribute", make_class_of("types", "DynamicClassAttribute", {}, sDynamicClassAttributeMethods));
                for (auto const& f : sTypesFunctions)
                    aAttributes.emplace_back(f.name, make_builtin(f));
                return !failed();
            }

            // --- numbers (the numeric ABCs: Number, Complex, Real, Rational and Integral, made by ABCMeta as CPython's are)

            builtin_entry const sNotImplementedMethod{ "abstract", [](std::vector<object*> const&, object*) -> object* { return error("NotImplementedError", ""); } };
            builtin_entry const sNoneMethod{ "abstract", [](std::vector<object*> const&, object*) -> object* { return new_reference(none()); } };

            object* one_argument(char const* aName, std::vector<object*> const& aArgs, object* aKwargs, char const* aParameter)
            {
                std::vector<object*> a;
                if (!self_arguments(aName, aArgs, aKwargs, { aParameter }, 1u, 0u, a))
                    return nullptr;
                return a[0];
            }

            object* builtin_call(char const* aName, object* aArgument)  // (a builtin (e.g. float) of aArgument)
            {
                ref const f{ fn_builtin(aName) };
                return f ? invoke(f.get(), { new_reference(aArgument) }) : nullptr;
            }

            builtin_entry const sComplexMethods[] =
            {
                { "__bool__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const zero{ make_int(0) };
                        return compare(NEOS_PY_NE, aArgs[0], zero.get());
                    } },
                { "__sub__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        auto const other = one_argument("__sub__", aArgs, aKwargs, "other");
                        if (other == nullptr)
                            return nullptr;
                        ref const negated{ unary(NEOS_PY_NEGATIVE, other) };
                        return negated ? binary(NEOS_PY_ADD, aArgs[0], negated.get(), false) : nullptr;
                    } },
                { "__rsub__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        auto const other = one_argument("__rsub__", aArgs, aKwargs, "other");
                        if (other == nullptr)
                            return nullptr;
                        ref const negated{ unary(NEOS_PY_NEGATIVE, aArgs[0]) };
                        return negated ? binary(NEOS_PY_ADD, negated.get(), other, false) : nullptr;
                    } }
            };

            builtin_entry const sRealMethods[] =
            {
                { "__divmod__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        auto const other = one_argument("__divmod__", aArgs, aKwargs, "other");
                        if (other == nullptr)
                            return nullptr;
                        ref const q{ binary(NEOS_PY_FLOOR_DIVIDE, aArgs[0], other, false) };
                        ref const r{ q ? binary(NEOS_PY_REMAINDER, aArgs[0], other, false) : nullptr };
                        return r ? make_tuple({ new_reference(q.get()), new_reference(r.get()) }) : nullptr;
                    } },
                { "__rdivmod__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                    {
                        auto const other = one_argument("__rdivmod__", aArgs, aKwargs, "other");
                        if (other == nullptr)
                            return nullptr;
                        ref const q{ binary(NEOS_PY_FLOOR_DIVIDE, other, aArgs[0], false) };
                        ref const r{ q ? binary(NEOS_PY_REMAINDER, other, aArgs[0], false) : nullptr };
                        return r ? make_tuple({ new_reference(q.get()), new_reference(r.get()) }) : nullptr;
                    } },
                { "__complex__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const f{ builtin_call("float", aArgs[0]) };
                        return f ? builtin_call("complex", f.get()) : nullptr;
                    } },
                { "conjugate", [](std::vector<object*> const& aArgs, object*) -> object* { return unary(NEOS_PY_POSITIVE, aArgs[0]); } }
            };

            builtin_entry const sRealProperties[] =
            {
                { "real", [](std::vector<object*> const& aArgs, object*) -> object* { return unary(NEOS_PY_POSITIVE, aArgs[0]); } },
                { "imag", [](std::vector<object*> const&, object*) -> object* { return make_int(0); } }
            };

            builtin_entry const sRationalMethods[] =
            {
                { "__float__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const numerator{ getattr(aArgs[0], "numerator") };
                        ref const n{ numerator ? builtin_call("int", numerator.get()) : nullptr };
                        ref const denominator{ n ? getattr(aArgs[0], "denominator") : nullptr };
                        ref const d{ denominator ? builtin_call("int", denominator.get()) : nullptr };
                        return d ? binary(NEOS_PY_TRUE_DIVIDE, n.get(), d.get(), false) : nullptr;
                    } }
            };

            builtin_entry const sIntegralMethods[] =
            {
                { "__index__", [](std::vector<object*> const& aArgs, object*) -> object* { return builtin_call("int", aArgs[0]); } },
                { "__float__", [](std::vector<object*> const& aArgs, object*) -> object*
                    {
                        ref const i{ builtin_call("int", aArgs[0]) };
                        return i ? builtin_call("float", i.get()) : nullptr;
                    } }
            };

            builtin_entry const sIntegralProperties[] =
            {
                { "numerator", [](std::vector<object*> const& aArgs, object*) -> object* { return unary(NEOS_PY_POSITIVE, aArgs[0]); } },
                { "denominator", [](std::vector<object*> const&, object*) -> object* { return make_int(1); } }
            };

            struct number_class
            {
                char const* name;
                std::vector<char const*> abstracts;             // (abstract methods: they raise NotImplementedError)
                std::vector<char const*> abstractProperties;
                std::vector<builtin_entry const*> methods;
                std::vector<builtin_entry const*> properties;
            };

            template <std::size_t N>
            std::vector<builtin_entry const*> entries(builtin_entry const (&aEntries)[N])
            {
                std::vector<builtin_entry const*> result;
                for (auto const& e : aEntries)
                    result.push_back(&e);
                return result;
            }

            object* make_number_class(object* aMeta, object* aBase, number_class const& aClass)
            {
                ref const ns{ make_dict() };
                auto const set = [&](char const* aName, object* aValue)  // (steals aValue)
                {
                    ref const key{ make_str(aName) };
                    ref const value{ aValue };
                    dict_set(ns.get(), key.get(), value.get());
                };
                set("__module__", make_str("numbers"));
                set("__slots__", make_tuple());
                if (aBase == nullptr)
                    set("__hash__", new_reference(none()));
                for (auto n : aClass.abstracts)
                {
                    auto const f = make_function(n, call_builtin, const_cast<builtin_entry*>(std::strcmp(n, "__complex__") == 0 ? &sNoneMethod : &sNotImplementedMethod));
                    abstract_builtins().insert(f);
                    set(n, f);
                }
                for (auto n : aClass.abstractProperties)
                {
                    ref const f{ make_function(n, call_builtin, const_cast<builtin_entry*>(&sNotImplementedMethod)) };
                    abstract_builtins().insert(new_reference(f.get()));
                    set(n, b_property({ f.get() }, nullptr));
                }
                for (auto m : aClass.methods)
                    set(m->name, make_builtin(*m));
                for (auto p : aClass.properties)
                {
                    ref const f{ make_builtin(*p) };
                    set(p->name, b_property({ f.get() }, nullptr));
                }
                return invoke(aMeta, { make_str(aClass.name), aBase != nullptr ? make_tuple({ new_reference(aBase) }) : make_tuple(), new_reference(ns.get()) });
            }

            bool numbers_module(std::vector<std::pair<std::string, object*>>& aAttributes)
            {
                ref const meta{ abc_meta() };
                if (!meta)
                    return false;
                std::vector<number_class> const classes =
                {
                    { "Number", {}, {}, {}, {} },
                    { "Complex", { "__complex__", "__add__", "__radd__", "__neg__", "__pos__", "__mul__", "__rmul__", "__truediv__", "__rtruediv__", "__pow__",
                        "__rpow__", "__abs__", "conjugate", "__eq__" }, { "real", "imag" }, entries(sComplexMethods), {} },
                    { "Real", { "__float__", "__trunc__", "__floor__", "__ceil__", "__round__", "__floordiv__", "__rfloordiv__", "__mod__", "__rmod__", "__lt__",
                        "__le__" }, {}, entries(sRealMethods), entries(sRealProperties) },
                    { "Rational", {}, { "numerator", "denominator" }, entries(sRationalMethods), {} },
                    { "Integral", { "__int__", "__pow__", "__lshift__", "__rlshift__", "__rshift__", "__rrshift__", "__and__", "__rand__", "__xor__", "__rxor__",
                        "__or__", "__ror__", "__invert__" }, {}, entries(sIntegralMethods), entries(sIntegralProperties) }
                };
                std::vector<object*> made;
                ref base;
                for (auto const& c : classes)
                {
                    ref cls{ make_number_class(meta.get(), base.get(), c) };
                    if (!cls)
                    {
                        for (auto m : made)
                            release(m);
                        return false;
                    }
                    made.push_back(new_reference(cls.get()));
                    aAttributes.emplace_back(c.name, new_reference(cls.get()));
                    base = std::move(cls);
                }
                // (complex, float and int are registered; as are this runtime's fractions.Fraction (CPython's derives from Rational) and decimal.Decimal)
                std::pair<object*, object*> const registrations[] = { { made[1], type_object_for("complex", "complex") }, { made[2], type_object_for("float", "float") },
                    { made[4], type_object_for("int", "int") }, { made[3], module_attribute("fractions", "Fraction") }, { made[0], module_attribute("decimal", "Decimal") } };
                bool ok = true;
                for (auto const& [cls, type] : registrations)
                {
                    if (type == nullptr)
                        ok = false;
                    else if (ok)
                        ok = static_cast<bool>(ref{ call_method_named(cls, "register", { new_reference(type) }) });
                    release(type);
                }
                for (auto m : made)
                    release(m);
                if (!ok)
                    return false;
                aAttributes.emplace_back("__all__", make_list({ make_str("Number"), make_str("Complex"), make_str("Real"), make_str("Rational"), make_str("Integral") }));
                return true;
            }

            inline bool const sNumbers = register_module("numbers", numbers_module);
            inline bool const sAbc = register_module("abc", abc_module);
            inline bool const sTypes = register_module("types", types_module);
        }
    }
}
