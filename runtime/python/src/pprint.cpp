/*
  pprint.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/pprint.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module pprint (CPython 3.13's pprint.py: PrettyPrinter (its dispatch of the types whose reprs it formats: dict, list, tuple, set,
// frozenset, str, bytes, bytearray, mappingproxy, SimpleNamespace, OrderedDict, defaultdict, Counter, ChainMap, deque, UserDict, UserList,
// UserString, dataclasses), pprint, pformat, pp, saferepr, isreadable, isrecursive)

#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        object* sPrettyPrinter = nullptr;
        object* sSafeKey = nullptr;

        py none() { return py::none(); }
        py boolean(bool aValue) { return py::boolean(aValue); }
        py id_of(py const& aObject) { return builtin("id")(aObject); }
        py spaces(long long aCount) { return py{ std::string(static_cast<std::size_t>(aCount > 0 ? aCount : 0), ' ') }; }
        void write(py const& aStream, py const& aText) { aStream.method("write", { aText }); }
        void write(py const& aStream, std::string const& aText) { aStream.method("write", { py{ aText } }); }
        void write(py const& aStream, char const* aText) { aStream.method("write", { py{ aText } }); }
        long long length_of(py const& aObject) { return aObject.size(); }

        std::vector<py> arguments(char const* aFunction, std::vector<object*> const& aArgs, object* aKwargs, std::vector<char const*> const& aNames,
            std::size_t aRequired, std::vector<char const*> const& aKeywordOnly = {}, std::size_t aPositionalOnly = 0u)
        {
            if (aArgs.size() > aNames.size())
                py_raise("TypeError", std::string{ aFunction } + "() takes " + std::to_string(aNames.size()) + " positional argument" + (aNames.size() == 1u ? "" : "s") +
                    " but " + std::to_string(aArgs.size()) + (aArgs.size() == 1u ? " was" : " were") + " given");
            std::vector<char const*> names = aNames;
            names.insert(names.end(), aKeywordOnly.begin(), aKeywordOnly.end());
            std::vector<object*> a;
            py_checked(parse_arguments(aFunction, aArgs, aKwargs, names, aRequired, aPositionalOnly, a));
            std::vector<py> result;
            for (auto v : a)
                result.push_back(v != nullptr ? py::borrowed(v) : py{});
            return result;
        }

        py value_or(py const& aValue, py const& aDefault) { return aValue.empty() ? aDefault : aValue; }

        // (_recursion(object))
        py recursion(py const& aObject)
        {
            return py{ "<Recursion on " + type_of(aObject).attr("__name__").str() + " with id=" + id_of(aObject).str() + ">" };
        }

        // (a type's __repr__ is that of the builtin type aName (e.g. dict.__repr__))
        bool repr_is(py const& aRepr, char const* aType)
        {
            return !aRepr.is_none() && aRepr == builtin(aType).attr("__repr__");
        }

        // --- _safe_key

        builtin_entry const sSafeKeyMethods[] =
        {
            { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("_safe_key.__init__", aArgs, aKwargs, { "self", "obj" }, 2u);
                        a[0].set_attr("obj", a[1]);
                        return none();
                    });
                } },
            { "__lt__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("_safe_key.__lt__", aArgs, aKwargs, { "self", "other" }, 2u);
                        py const lhs = a[0].attr("obj");
                        py const rhs = a[1].attr("obj");
                        try
                        {
                            return py{ compare(NEOS_PY_LT, lhs.get(), rhs.get()) };
                        }
                        catch (raised const&)
                        {
                            if (!caught({ "TypeError" }))
                                throw;
                            auto const str = builtin("str");
                            return py{ compare(NEOS_PY_LT, py_tuple({ str(type_of(lhs)), id_of(lhs) }).get(), py_tuple({ str(type_of(rhs)), id_of(rhs) }).get()) };
                        }
                    });
                } }
        };

        py safe_key(py const& aObject) { return py::borrowed(sSafeKey)(aObject); }

        // (_safe_tuple: the key of a dict's item)
        py safe_tuple()
        {
            static object* sFunction = nullptr;
            if (sFunction == nullptr)
                sFunction = make_native_function("_safe_tuple", "_safe_tuple", "Helper function for comparing 2-tuples", "pprint",
                    [](std::vector<py> const& aArgs, object*) -> py
                    {
                        if (aArgs.size() != 1u)
                            py_raise("TypeError", "_safe_tuple() takes 1 positional argument but " + std::to_string(aArgs.size()) + " were given");
                        return py_tuple({ safe_key(aArgs[0][py{ 0 }]), safe_key(aArgs[0][py{ 1 }]) });
                    }).release();
            return py::borrowed(sFunction);
        }

        py sorted_items(py const& aMapping)
        {
            return builtin("sorted").call({ aMapping.method("items") }, { { "key", safe_tuple() } });
        }

        // --- PrettyPrinter

        // (PrettyPrinter._safe_repr: (repr, readable, recursive))
        py safe_repr(py const& aSelf, py const& aObject, py const& aContext, py const& aMaxLevels, py aLevel)
        {
            py const typ = type_of(aObject);
            for (auto scalar : { "str", "bytes", "bytearray", "float", "complex", "bool" })
                if (typ.is(builtin(scalar)))
                    return py_tuple({ py{ aObject.repr() }, boolean(true), boolean(false) });
            if (typ.is(type_of(none())))
                return py_tuple({ py{ aObject.repr() }, boolean(true), boolean(false) });
            py const r = builtin("getattr")(typ, py{ "__repr__" }, none());
            if (issubclass(typ, builtin("int")) && repr_is(r, "int"))
            {
                if (aSelf.attr("_underscore_numbers").truthy())
                    return py_tuple({ builtin("format")(aObject, py{ "_d" }), boolean(true), boolean(false) });
                return py_tuple({ py{ aObject.repr() }, boolean(true), boolean(false) });
            }
            if (issubclass(typ, builtin("dict")) && repr_is(r, "dict"))
            {
                if (!aObject.truthy())
                    return py_tuple({ py{ "{}" }, boolean(true), boolean(false) });
                py const objid = id_of(aObject);
                if (aMaxLevels.truthy() && aLevel >= aMaxLevels)
                    return py_tuple({ py{ "{...}" }, boolean(false), boolean(aContext.contains(objid)) });
                if (aContext.contains(objid))
                    return py_tuple({ recursion(aObject), boolean(false), boolean(true) });
                aContext.set_item(objid, py{ 1 });
                bool readable = true;
                bool recursive = false;
                std::vector<py> components;
                aLevel = aLevel + py{ 1 };
                py const items = aSelf.attr("_sort_dicts").truthy() ? sorted_items(aObject) : aObject.method("items");
                for (auto const& item : items.items())
                {
                    py const k = aSelf.method("format", { item[py{ 0 }], aContext, aMaxLevels, aLevel });
                    py const v = aSelf.method("format", { item[py{ 1 }], aContext, aMaxLevels, aLevel });
                    components.push_back(py{ k[py{ 0 }].str() + ": " + v[py{ 0 }].str() });
                    readable = readable && k[py{ 1 }].truthy() && v[py{ 1 }].truthy();
                    if (k[py{ 2 }].truthy() || v[py{ 2 }].truthy())
                        recursive = true;
                }
                aContext.method("__delitem__", { objid });
                return py_tuple({ py{ "{" + py{ ", " }.method("join", { py_list(components) }).str() + "}" }, boolean(readable), boolean(recursive) });
            }
            bool const isList = issubclass(typ, builtin("list")) && repr_is(r, "list");
            bool const isTuple = issubclass(typ, builtin("tuple")) && repr_is(r, "tuple");
            if (isList || isTuple)
            {
                std::string open;
                std::string close;
                if (issubclass(typ, builtin("list")))
                {
                    if (!aObject.truthy())
                        return py_tuple({ py{ "[]" }, boolean(true), boolean(false) });
                    open = "[";
                    close = "]";
                }
                else if (aObject.size() == 1)
                {
                    open = "(";
                    close = ",)";
                }
                else
                {
                    if (!aObject.truthy())
                        return py_tuple({ py{ "()" }, boolean(true), boolean(false) });
                    open = "(";
                    close = ")";
                }
                py const objid = id_of(aObject);
                if (aMaxLevels.truthy() && aLevel >= aMaxLevels)
                    return py_tuple({ py{ open + "..." + close }, boolean(false), boolean(aContext.contains(objid)) });
                if (aContext.contains(objid))
                    return py_tuple({ recursion(aObject), boolean(false), boolean(true) });
                aContext.set_item(objid, py{ 1 });
                bool readable = true;
                bool recursive = false;
                std::vector<py> components;
                aLevel = aLevel + py{ 1 };
                for (auto const& o : aObject.items())
                {
                    py const f = aSelf.method("format", { o, aContext, aMaxLevels, aLevel });
                    components.push_back(f[py{ 0 }]);
                    if (!f[py{ 1 }].truthy())
                        readable = false;
                    if (f[py{ 2 }].truthy())
                        recursive = true;
                }
                aContext.method("__delitem__", { objid });
                return py_tuple({ py{ open + py{ ", " }.method("join", { py_list(components) }).str() + close }, boolean(readable), boolean(recursive) });
            }
            py const rep = builtin("repr")(aObject);
            bool const readable = rep.truthy() && !rep.method("startswith", { py{ "<" } }).truthy();
            return py_tuple({ rep, rep.truthy() ? boolean(readable) : rep, boolean(false) });
        }

        // (PrettyPrinter._repr)
        py pp_repr(py const& aSelf, py const& aObject, py const& aContext, py const& aLevel)
        {
            py const f = aSelf.method("format", { aObject, aContext.method("copy"), aSelf.attr("_depth"), aLevel });
            if (!f[py{ 1 }].truthy())
                aSelf.set_attr("_readable", boolean(false));
            if (f[py{ 2 }].truthy())
                aSelf.set_attr("_recursive", boolean(true));
            return f[py{ 0 }];
        }

        py self_repr(py const& aSelf, py const& aObject, py const& aContext, py const& aLevel) { return aSelf.method("_repr", { aObject, aContext, aLevel }); }

        void self_format(py const& aSelf, py const& aObject, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            aSelf.method("_format", { aObject, aStream, py{ aIndent }, py{ aAllowance }, aContext, aLevel });
        }

        bool is_dataclass_instance(py const& aObject)
        {
            auto const hasattr = builtin("hasattr");
            return hasattr(type_of(aObject), py{ "__dataclass_fields__" }).truthy() && !isinstance(aObject, builtin("type"));
        }

        // (PrettyPrinter._format)
        void pp_format(py const& aSelf, py const& aObject, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            py const objid = id_of(aObject);
            if (aContext.contains(objid))
            {
                write(aStream, recursion(aObject));
                aSelf.set_attr("_recursive", boolean(true));
                aSelf.set_attr("_readable", boolean(false));
                return;
            }
            py const rep = self_repr(aSelf, aObject, aContext, aLevel);
            long long const maxWidth = aSelf.attr("_width").as_index() - aIndent - aAllowance;
            if (rep.size() > maxWidth)
            {
                py const p = aSelf.attr("_dispatch").method("get", { type_of(aObject).attr("__repr__"), none() });
                if (!p.is_none())
                {
                    aContext.set_item(objid, py{ 1 });
                    p(aSelf, aObject, aStream, py{ aIndent }, py{ aAllowance }, aContext, aLevel + py{ 1 });
                    aContext.method("__delitem__", { objid });
                    return;
                }
                if (is_dataclass_instance(aObject) && aObject.attr("__dataclass_params__").attr("repr").truthy())
                {
                    py const method = aObject.attr("__repr__");
                    auto const hasattr = builtin("hasattr");
                    if (hasattr(method, py{ "__wrapped__" }).truthy() && method.attr("__wrapped__").attr("__qualname__").contains(py{ "__create_fn__" }))
                    {
                        aContext.set_item(objid, py{ 1 });
                        aSelf.method("_pprint_dataclass", { aObject, aStream, py{ aIndent }, py{ aAllowance }, aContext, aLevel + py{ 1 } });
                        aContext.method("__delitem__", { objid });
                        return;
                    }
                }
            }
            write(aStream, rep);
        }

        // (PrettyPrinter._format_dict_items)
        void format_dict_items(py const& aSelf, py const& aItems, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            aIndent += aSelf.attr("_indent_per_level").as_index();
            std::string const delimnl = ",\n" + std::string(static_cast<std::size_t>(aIndent), ' ');
            auto const items = aItems.items();
            for (std::size_t i = 0u; i < items.size(); ++i)
            {
                bool const last = i + 1u == items.size();
                py const rep = self_repr(aSelf, items[i][py{ 0 }], aContext, aLevel);
                write(aStream, rep);
                write(aStream, ": ");
                self_format(aSelf, items[i][py{ 1 }], aStream, aIndent + rep.size() + 2, last ? aAllowance : 1, aContext, aLevel);
                if (!last)
                    write(aStream, delimnl);
            }
        }

        // (PrettyPrinter._format_namespace_items)
        void format_namespace_items(py const& aSelf, py const& aItems, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            std::string const delimnl = ",\n" + std::string(static_cast<std::size_t>(aIndent), ' ');
            auto const items = aItems.items();
            for (std::size_t i = 0u; i < items.size(); ++i)
            {
                bool const last = i + 1u == items.size();
                py const key = items[i][py{ 0 }];
                py const ent = items[i][py{ 1 }];
                write(aStream, key);
                write(aStream, "=");
                if (aContext.contains(id_of(ent)))
                    write(aStream, "...");
                else
                    self_format(aSelf, ent, aStream, aIndent + key.size() + 1, last ? aAllowance : 1, aContext, aLevel);
                if (!last)
                    write(aStream, delimnl);
            }
        }

        // (PrettyPrinter._format_items)
        void format_items(py const& aSelf, py const& aItems, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            long long const perLevel = aSelf.attr("_indent_per_level").as_index();
            aIndent += perLevel;
            if (perLevel > 1)
                write(aStream, spaces(perLevel - 1));
            std::string const delimnl = ",\n" + std::string(static_cast<std::size_t>(aIndent), ' ');
            std::string delim;
            long long maxWidth = aSelf.attr("_width").as_index() - aIndent + 1;
            long long width = maxWidth;
            py const it = builtin("iter")(aItems);
            auto const next_item = [&]() -> py
            {
                auto const n = detail::next(it.get());
                if (n == nullptr)
                {
                    if (failed())
                        throw raised{};
                    return {};
                }
                return py{ n };
            };
            py nextEnt = next_item();
            if (nextEnt.empty())
                return;
            bool const compact = aSelf.attr("_compact").truthy();
            bool last = false;
            while (!last)
            {
                py const ent = nextEnt;
                nextEnt = next_item();
                if (nextEnt.empty())
                {
                    last = true;
                    maxWidth -= aAllowance;
                    width -= aAllowance;
                }
                if (compact)
                {
                    py const rep = self_repr(aSelf, ent, aContext, aLevel);
                    long long const w = rep.size() + 2;
                    if (width < w)
                    {
                        width = maxWidth;
                        if (!delim.empty())
                            delim = delimnl;
                    }
                    if (width >= w)
                    {
                        width -= w;
                        write(aStream, delim);
                        delim = ", ";
                        write(aStream, rep);
                        continue;
                    }
                }
                write(aStream, delim);
                delim = delimnl;
                self_format(aSelf, ent, aStream, aIndent, last ? aAllowance : 1, aContext, aLevel);
            }
        }

        void self_format_items(py const& aSelf, py const& aItems, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            aSelf.method("_format_items", { aItems, aStream, py{ aIndent }, py{ aAllowance }, aContext, aLevel });
        }

        void self_format_dict_items(py const& aSelf, py const& aItems, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            aSelf.method("_format_dict_items", { aItems, aStream, py{ aIndent }, py{ aAllowance }, aContext, aLevel });
        }

        void self_format_namespace_items(py const& aSelf, py const& aItems, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            aSelf.method("_format_namespace_items", { aItems, aStream, py{ aIndent }, py{ aAllowance }, aContext, aLevel });
        }

        std::string class_name(py const& aObject) { return aObject.attr("__class__").attr("__name__").str(); }

        // (the dispatched formatters: (self, object, stream, indent, allowance, context, level))
        using formatter = void (*)(py const& aSelf, py const& aObject, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel);

        void pprint_dict(py const& aSelf, py const& aObject, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            write(aStream, "{");
            long long const perLevel = aSelf.attr("_indent_per_level").as_index();
            if (perLevel > 1)
                write(aStream, spaces(perLevel - 1));
            if (length_of(aObject) != 0)
            {
                py const items = aSelf.attr("_sort_dicts").truthy() ? sorted_items(aObject) : aObject.method("items");
                self_format_dict_items(aSelf, items, aStream, aIndent, aAllowance + 1, aContext, aLevel);
            }
            write(aStream, "}");
        }

        void pprint_ordered_dict(py const& aSelf, py const& aObject, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            if (length_of(aObject) == 0)
            {
                write(aStream, py{ aObject.repr() });
                return;
            }
            auto const name = class_name(aObject);
            write(aStream, name + "(");
            self_format(aSelf, builtin("list")(aObject.method("items")), aStream, aIndent + static_cast<long long>(py{ name }.size()) + 1, aAllowance + 1, aContext, aLevel);
            write(aStream, ")");
        }

        void pprint_list(py const& aSelf, py const& aObject, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            write(aStream, "[");
            self_format_items(aSelf, aObject, aStream, aIndent, aAllowance + 1, aContext, aLevel);
            write(aStream, "]");
        }

        void pprint_tuple(py const& aSelf, py const& aObject, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            write(aStream, "(");
            std::string const endchar = length_of(aObject) == 1 ? ",)" : ")";
            self_format_items(aSelf, aObject, aStream, aIndent, aAllowance + static_cast<long long>(endchar.size()), aContext, aLevel);
            write(aStream, endchar);
        }

        void pprint_set(py const& aSelf, py const& aObject, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            if (length_of(aObject) == 0)
            {
                write(aStream, py{ aObject.repr() });
                return;
            }
            py const typ = aObject.attr("__class__");
            std::string endchar;
            if (typ.is(builtin("set")))
            {
                write(aStream, "{");
                endchar = "}";
            }
            else
            {
                py const name = typ.attr("__name__");
                write(aStream, name.str() + "({");
                endchar = "})";
                aIndent += name.size() + 1;
            }
            py const items = builtin("sorted").call({ aObject }, { { "key", py::borrowed(sSafeKey) } });
            self_format_items(aSelf, items, aStream, aIndent, aAllowance + static_cast<long long>(endchar.size()), aContext, aLevel);
            write(aStream, endchar);
        }

        void pprint_str(py const& aSelf, py const& aObject, py const& aStream, long long aIndent, long long aAllowance, py const&, py const& aLevel)
        {
            if (length_of(aObject) == 0)
            {
                write(aStream, py{ aObject.repr() });
                return;
            }
            std::vector<py> chunks;
            auto const lines = aObject.method("splitlines", { boolean(true) }).items();
            bool const top = aLevel == py{ 1 };
            if (top)
            {
                aIndent += 1;
                aAllowance += 1;
            }
            long long const maxWidth = aSelf.attr("_width").as_index() - aIndent;
            long long maxWidth1 = maxWidth;
            auto const repr = builtin("repr");
            py rep;
            auto const findall = import_attribute("re", "findall");
            for (std::size_t i = 0u; i < lines.size(); ++i)
            {
                py const& line = lines[i];
                rep = repr(line);
                if (i + 1u == lines.size())
                    maxWidth1 -= aAllowance;
                if (rep.size() <= maxWidth1)
                    chunks.push_back(rep);
                else
                {
                    // (alternating non-space and space strings)
                    auto parts = findall(py{ "\\S*\\s*" }, line).items();
                    if (!parts.empty())
                        parts.pop_back();   // (the empty last part)
                    long long maxWidth2 = maxWidth;
                    py current{ "" };
                    for (std::size_t j = 0u; j < parts.size(); ++j)
                    {
                        py const candidate = current + parts[j];
                        if (j + 1u == parts.size() && i + 1u == lines.size())
                            maxWidth2 -= aAllowance;
                        if (repr(candidate).size() > maxWidth2)
                        {
                            if (current.truthy())
                                chunks.push_back(repr(current));
                            current = parts[j];
                        }
                        else
                            current = candidate;
                    }
                    if (current.truthy())
                        chunks.push_back(repr(current));
                }
            }
            if (chunks.size() == 1u)
            {
                write(aStream, rep);
                return;
            }
            if (top)
                write(aStream, "(");
            for (std::size_t i = 0u; i < chunks.size(); ++i)
            {
                if (i > 0u)
                    write(aStream, "\n" + std::string(static_cast<std::size_t>(aIndent), ' '));
                write(aStream, chunks[i]);
            }
            if (top)
                write(aStream, ")");
        }

        // (_wrap_bytes_repr)
        std::vector<py> wrap_bytes_repr(py const& aObject, long long aWidth, long long aAllowance)
        {
            std::vector<py> result;
            auto const repr = builtin("repr");
            py current{ make_bytes({}) };
            long long const n = aObject.size();
            long long const last = n / 4 * 4;
            auto const slice = builtin("slice");
            for (long long i = 0; i < n; i += 4)
            {
                py const part = aObject[slice(py{ i }, py{ i + 4 })];
                py const candidate = current + part;
                if (i == last)
                    aWidth -= aAllowance;
                if (repr(candidate).size() > aWidth)
                {
                    if (current.truthy())
                        result.push_back(repr(current));
                    current = part;
                }
                else
                    current = candidate;
            }
            if (current.truthy())
                result.push_back(repr(current));
            return result;
        }

        void pprint_bytes(py const& aSelf, py const& aObject, py const& aStream, long long aIndent, long long aAllowance, py const&, py const& aLevel)
        {
            if (length_of(aObject) <= 4)
            {
                write(aStream, py{ aObject.repr() });
                return;
            }
            bool const parens = aLevel == py{ 1 };
            if (parens)
            {
                aIndent += 1;
                aAllowance += 1;
                write(aStream, "(");
            }
            std::string delim;
            for (auto const& rep : wrap_bytes_repr(aObject, aSelf.attr("_width").as_index() - aIndent, aAllowance))
            {
                write(aStream, delim);
                write(aStream, rep);
                if (delim.empty())
                    delim = "\n" + std::string(static_cast<std::size_t>(aIndent), ' ');
            }
            if (parens)
                write(aStream, ")");
        }

        void pprint_bytearray(py const& aSelf, py const& aObject, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            write(aStream, "bytearray(");
            aSelf.method("_pprint_bytes", { builtin("bytes")(aObject), aStream, py{ aIndent + 10 }, py{ aAllowance + 1 }, aContext, aLevel + py{ 1 } });
            write(aStream, ")");
        }

        void pprint_mappingproxy(py const& aSelf, py const& aObject, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            write(aStream, "mappingproxy(");
            self_format(aSelf, aObject.method("copy"), aStream, aIndent + 13, aAllowance + 1, aContext, aLevel);
            write(aStream, ")");
        }

        void pprint_simplenamespace(py const& aSelf, py const& aObject, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            std::string const name = type_of(aObject).is(import_attribute("types", "SimpleNamespace")) ? std::string{ "namespace" } : class_name(aObject);
            aIndent += py{ name }.size() + 1;
            py const items = aObject.attr("__dict__").method("items");
            write(aStream, name + "(");
            self_format_namespace_items(aSelf, items, aStream, aIndent, aAllowance, aContext, aLevel);
            write(aStream, ")");
        }

        void pprint_dataclass(py const& aSelf, py const& aObject, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            auto const name = class_name(aObject);
            aIndent += py{ name }.size() + 1;
            std::vector<py> items;
            for (auto const& f : import_attribute("dataclasses", "fields")(aObject).items())
                if (f.attr("repr").truthy())
                    items.push_back(py_tuple({ f.attr("name"), builtin("getattr")(aObject, f.attr("name")) }));
            write(aStream, name + "(");
            self_format_namespace_items(aSelf, py_list(items), aStream, aIndent, aAllowance, aContext, aLevel);
            write(aStream, ")");
        }

        void pprint_default_dict(py const& aSelf, py const& aObject, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            if (length_of(aObject) == 0)
            {
                write(aStream, py{ aObject.repr() });
                return;
            }
            py const rdf = self_repr(aSelf, aObject.attr("default_factory"), aContext, aLevel);
            auto const name = class_name(aObject);
            aIndent += py{ name }.size() + 1;
            write(aStream, name + "(" + rdf.str() + ",\n" + std::string(static_cast<std::size_t>(aIndent), ' '));
            aSelf.method("_pprint_dict", { aObject, aStream, py{ aIndent }, py{ aAllowance + 1 }, aContext, aLevel });
            write(aStream, ")");
        }

        void pprint_counter(py const& aSelf, py const& aObject, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            if (length_of(aObject) == 0)
            {
                write(aStream, py{ aObject.repr() });
                return;
            }
            auto const name = class_name(aObject);
            write(aStream, name + "({");
            long long const perLevel = aSelf.attr("_indent_per_level").as_index();
            if (perLevel > 1)
                write(aStream, spaces(perLevel - 1));
            py const items = aObject.method("most_common");
            self_format_dict_items(aSelf, items, aStream, aIndent + py{ name }.size() + 1, aAllowance + 2, aContext, aLevel);
            write(aStream, "})");
        }

        void pprint_chain_map(py const& aSelf, py const& aObject, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            auto const maps = aObject.attr("maps").items();
            if (maps.empty())
            {
                write(aStream, py{ aObject.repr() });
                return;
            }
            auto const name = class_name(aObject);
            write(aStream, name + "(");
            aIndent += py{ name }.size() + 1;
            for (std::size_t i = 0u; i < maps.size(); ++i)
            {
                if (i + 1u == maps.size())
                {
                    self_format(aSelf, maps[i], aStream, aIndent, aAllowance + 1, aContext, aLevel);
                    write(aStream, ")");
                }
                else
                {
                    self_format(aSelf, maps[i], aStream, aIndent, 1, aContext, aLevel);
                    write(aStream, ",\n" + std::string(static_cast<std::size_t>(aIndent), ' '));
                }
            }
        }

        void pprint_deque(py const& aSelf, py const& aObject, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            if (length_of(aObject) == 0)
            {
                write(aStream, py{ aObject.repr() });
                return;
            }
            auto const name = class_name(aObject);
            write(aStream, name + "(");
            aIndent += py{ name }.size() + 1;
            write(aStream, "[");
            if (aObject.attr("maxlen").is_none())
            {
                self_format_items(aSelf, aObject, aStream, aIndent, aAllowance + 2, aContext, aLevel);
                write(aStream, "])");
            }
            else
            {
                self_format_items(aSelf, aObject, aStream, aIndent, 2, aContext, aLevel);
                py const rml = self_repr(aSelf, aObject.attr("maxlen"), aContext, aLevel);
                write(aStream, "],\n" + std::string(static_cast<std::size_t>(aIndent), ' ') + "maxlen=" + rml.str() + ")");
            }
        }

        void pprint_user_data(py const& aSelf, py const& aObject, py const& aStream, long long aIndent, long long aAllowance, py const& aContext, py const& aLevel)
        {
            self_format(aSelf, aObject.attr("data"), aStream, aIndent, aAllowance, aContext, aLevel - py{ 1 });
        }

        // (a method whose body is a formatter: (self, object, stream, indent, allowance, context, level))
        template <formatter F>
        object* formatter_method(std::vector<object*> const& aArgs, object* aKwargs)
        {
            return py_guarded([&]()
            {
                auto const a = arguments("PrettyPrinter._pprint", aArgs, aKwargs, { "self", "object", "stream", "indent", "allowance", "context", "level" }, 7u);
                F(a[0], a[1], a[2], a[3].as_index(), a[4].as_index(), a[5], a[6]);
                return none();
            });
        }

        builtin_entry const sPrettyPrinterMethods[] =
        {
            { "__init__", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("PrettyPrinter.__init__", aArgs, aKwargs, { "self", "indent", "width", "depth", "stream" }, 1u,
                            { "compact", "sort_dicts", "underscore_numbers" });
                        py const self = a[0];
                        auto const int_ = builtin("int");
                        py const indent = int_(value_or(a[1], py{ 1 }));
                        py const width = int_(value_or(a[2], py{ 80 }));
                        py const depth = value_or(a[3], none());
                        if (indent < py{ 0 })
                            py_raise("ValueError", "indent must be >= 0");
                        if (!depth.is_none() && depth <= py{ 0 })
                            py_raise("ValueError", "depth must be > 0");
                        if (!width.truthy())
                            py_raise("ValueError", "width must be != 0");
                        self.set_attr("_depth", depth);
                        self.set_attr("_indent_per_level", indent);
                        self.set_attr("_width", width);
                        py const stream = value_or(a[4], none());
                        self.set_attr("_stream", !stream.is_none() ? stream : import_attribute("sys", "stdout"));
                        self.set_attr("_compact", boolean(value_or(a[5], boolean(false)).truthy()));
                        self.set_attr("_sort_dicts", value_or(a[6], boolean(true)));
                        self.set_attr("_underscore_numbers", value_or(a[7], boolean(false)));
                        return none();
                    });
                } },
            { "pprint", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("PrettyPrinter.pprint", aArgs, aKwargs, { "self", "object" }, 2u);
                        py const stream = a[0].attr("_stream");
                        if (!stream.is_none())
                        {
                            self_format(a[0], a[1], stream, 0, 0, py_dict(), py{ 0 });
                            write(stream, "\n");
                        }
                        return none();
                    });
                } },
            { "pformat", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("PrettyPrinter.pformat", aArgs, aKwargs, { "self", "object" }, 2u);
                        py const sio = import_attribute("io", "StringIO")();
                        self_format(a[0], a[1], sio, 0, 0, py_dict(), py{ 0 });
                        return sio.method("getvalue");
                    });
                } },
            { "isrecursive", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("PrettyPrinter.isrecursive", aArgs, aKwargs, { "self", "object" }, 2u);
                        return a[0].method("format", { a[1], py_dict(), py{ 0 }, py{ 0 } })[py{ 2 }];
                    });
                } },
            { "isreadable", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("PrettyPrinter.isreadable", aArgs, aKwargs, { "self", "object" }, 2u);
                        py const f = a[0].method("format", { a[1], py_dict(), py{ 0 }, py{ 0 } });
                        py const readable = f[py{ 1 }];
                        return readable.truthy() ? boolean(!f[py{ 2 }].truthy()) : readable;
                    });
                } },
            { "_format", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("PrettyPrinter._format", aArgs, aKwargs, { "self", "object", "stream", "indent", "allowance", "context", "level" }, 7u);
                        pp_format(a[0], a[1], a[2], a[3].as_index(), a[4].as_index(), a[5], a[6]);
                        return none();
                    });
                } },
            { "_pprint_dataclass", formatter_method<pprint_dataclass> },
            { "_pprint_dict", formatter_method<pprint_dict> },
            { "_pprint_ordered_dict", formatter_method<pprint_ordered_dict> },
            { "_pprint_list", formatter_method<pprint_list> },
            { "_pprint_tuple", formatter_method<pprint_tuple> },
            { "_pprint_set", formatter_method<pprint_set> },
            { "_pprint_str", formatter_method<pprint_str> },
            { "_pprint_bytes", formatter_method<pprint_bytes> },
            { "_pprint_bytearray", formatter_method<pprint_bytearray> },
            { "_pprint_mappingproxy", formatter_method<pprint_mappingproxy> },
            { "_pprint_simplenamespace", formatter_method<pprint_simplenamespace> },
            { "_pprint_default_dict", formatter_method<pprint_default_dict> },
            { "_pprint_counter", formatter_method<pprint_counter> },
            { "_pprint_chain_map", formatter_method<pprint_chain_map> },
            { "_pprint_deque", formatter_method<pprint_deque> },
            { "_pprint_user_dict", formatter_method<pprint_user_data> },
            { "_pprint_user_list", formatter_method<pprint_user_data> },
            { "_pprint_user_string", formatter_method<pprint_user_data> },
            { "_format_dict_items", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("PrettyPrinter._format_dict_items", aArgs, aKwargs, { "self", "items", "stream", "indent", "allowance", "context", "level" }, 7u);
                        format_dict_items(a[0], a[1], a[2], a[3].as_index(), a[4].as_index(), a[5], a[6]);
                        return none();
                    });
                } },
            { "_format_namespace_items", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("PrettyPrinter._format_namespace_items", aArgs, aKwargs, { "self", "items", "stream", "indent", "allowance", "context", "level" }, 7u);
                        format_namespace_items(a[0], a[1], a[2], a[3].as_index(), a[4].as_index(), a[5], a[6]);
                        return none();
                    });
                } },
            { "_format_items", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("PrettyPrinter._format_items", aArgs, aKwargs, { "self", "items", "stream", "indent", "allowance", "context", "level" }, 7u);
                        format_items(a[0], a[1], a[2], a[3].as_index(), a[4].as_index(), a[5], a[6]);
                        return none();
                    });
                } },
            { "_repr", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("PrettyPrinter._repr", aArgs, aKwargs, { "self", "object", "context", "level" }, 4u);
                        return pp_repr(a[0], a[1], a[2], a[3]);
                    });
                } },
            { "format", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("PrettyPrinter.format", aArgs, aKwargs, { "self", "object", "context", "maxlevels", "level" }, 5u);
                        return a[0].method("_safe_repr", { a[1], a[2], a[3], a[4] });
                    });
                } },
            { "_safe_repr", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("PrettyPrinter._safe_repr", aArgs, aKwargs, { "self", "object", "context", "maxlevels", "level" }, 5u);
                        return safe_repr(a[0], a[1], a[2], a[3], a[4]);
                    });
                } }
        };

        // (pprint(object, stream=None, indent=1, width=80, depth=None, *, compact=False, sort_dicts=True, underscore_numbers=False))
        py printer_of(std::vector<py> const& a, std::size_t aFirst, bool aStream)
        {
            std::vector<std::pair<char const*, py>> keywords;
            std::size_t i = aFirst;
            if (aStream)
                keywords.emplace_back("stream", value_or(a[i++], none()));
            keywords.emplace_back("indent", value_or(a[i++], py{ 1 }));
            keywords.emplace_back("width", value_or(a[i++], py{ 80 }));
            keywords.emplace_back("depth", value_or(a[i++], none()));
            keywords.emplace_back("compact", value_or(a[i++], boolean(false)));
            keywords.emplace_back("sort_dicts", value_or(a[i++], boolean(true)));
            keywords.emplace_back("underscore_numbers", value_or(a[i++], boolean(false)));
            return py::borrowed(sPrettyPrinter).call({}, keywords);
        }

        py safe_repr_of(py const& aObject)
        {
            py const printer = py::borrowed(sPrettyPrinter)();
            return printer.method("_safe_repr", { aObject, py_dict(), none(), py{ 0 } });
        }

        builtin_entry const sPprintFunctions[] =
        {
            { "pprint", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("pprint", aArgs, aKwargs, { "object", "stream", "indent", "width", "depth" }, 1u,
                            { "compact", "sort_dicts", "underscore_numbers" });
                        printer_of(a, 1u, true).method("pprint", { a[0] });
                        return none();
                    });
                } },
            { "pformat", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("pformat", aArgs, aKwargs, { "object", "indent", "width", "depth" }, 1u, { "compact", "sort_dicts", "underscore_numbers" });
                        return printer_of(a, 1u, false).method("pformat", { a[0] });
                    });
                } },
            { "pp", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        // (pprint(object, *args, sort_dicts=sort_dicts, **kwargs), sort_dicts=False)
                        if (aArgs.empty())
                            py_raise("TypeError", "pp() missing 1 required positional argument: 'object'");
                        ref kwargs{ make_dict() };
                        if (aKwargs != nullptr)
                            py_checked(dict_update(kwargs.get(), aKwargs));
                        ref const key{ make_str("sort_dicts") };
                        if (dict_find(kwargs.get(), key.get()) < 0)
                        {
                            ref const value{ make_bool(false) };
                            py_checked(dict_set(kwargs.get(), key.get(), value.get()) == 0);
                        }
                        std::vector<object*> args;
                        for (auto a : aArgs)
                            args.push_back(new_reference(a));
                        return py{ invoke(import_attribute("pprint", "pprint").get(), std::move(args), kwargs.get()) };
                    });
                } },
            { "saferepr", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return safe_repr_of(arguments("saferepr", aArgs, aKwargs, { "object" }, 1u)[0])[py{ 0 }]; }); } },
            { "isreadable", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return safe_repr_of(arguments("isreadable", aArgs, aKwargs, { "object" }, 1u)[0])[py{ 1 }]; }); } },
            { "isrecursive", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return safe_repr_of(arguments("isrecursive", aArgs, aKwargs, { "object" }, 1u)[0])[py{ 2 }]; }); } },
            { "_recursion", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                { return py_guarded([&]() { return recursion(arguments("_recursion", aArgs, aKwargs, { "object" }, 1u)[0]); }); } },
            { "_wrap_bytes_repr", [](std::vector<object*> const& aArgs, object* aKwargs) -> object*
                {
                    return py_guarded([&]()
                    {
                        auto const a = arguments("_wrap_bytes_repr", aArgs, aKwargs, { "object", "width", "allowance" }, 3u);
                        return builtin("iter")(py_list(wrap_bytes_repr(a[0], a[1].as_index(), a[2].as_index())));
                    });
                } }
        };

        bool make_pprint_classes()
        {
            if (sPrettyPrinter != nullptr)
                return true;
            auto const result = py_guarded([&]()
            {
                py const safeKey{ make_class_of("pprint", "_safe_key", {}, sSafeKeyMethods, { { "__doc__", make_str(
                    "Helper function for key functions when sorting unorderable objects.\n\n"
                    "    The wrapped-object will fallback to a Py2.x style comparison for\n"
                    "    unorderable types (sorting first comparing the type name and then by\n"
                    "    the obj ids).  Does not work recursively, so dict.items() must have\n"
                    "    _safe_key applied to both the key and the value.\n\n    ") } }) };
                py const printer{ make_class_of("pprint", "PrettyPrinter", {}, sPrettyPrinterMethods) };
                // (_dispatch: the formatters of the types whose reprs are their types')
                py const dispatch = py_dict();
                py const namespace_ = printer.attr("__dict__");
                auto const add = [&](py const& aType, char const* aFormatter) { dispatch.set_item(aType.attr("__repr__"), namespace_[py{ aFormatter }]); };
                // (a type that the module collections does not have (yet): not dispatched)
                auto const collection = [&](char const* aName) -> py
                {
                    py const collections{ fn_import("collections") };
                    return builtin("getattr")(collections, py{ aName }, py::none());
                };
                add(builtin("dict"), "_pprint_dict");
                add(import_attribute("collections", "OrderedDict"), "_pprint_ordered_dict");
                add(builtin("list"), "_pprint_list");
                add(builtin("tuple"), "_pprint_tuple");
                add(builtin("set"), "_pprint_set");
                add(builtin("frozenset"), "_pprint_set");
                add(builtin("str"), "_pprint_str");
                add(builtin("bytes"), "_pprint_bytes");
                add(builtin("bytearray"), "_pprint_bytearray");
                add(import_attribute("types", "MappingProxyType"), "_pprint_mappingproxy");
                add(import_attribute("types", "SimpleNamespace"), "_pprint_simplenamespace");
                add(import_attribute("collections", "defaultdict"), "_pprint_default_dict");
                add(import_attribute("collections", "Counter"), "_pprint_counter");
                add(import_attribute("collections", "ChainMap"), "_pprint_chain_map");
                add(import_attribute("collections", "deque"), "_pprint_deque");
                for (auto const& [name, formatter] : { std::pair{ "UserDict", "_pprint_user_dict" }, std::pair{ "UserList", "_pprint_user_list" },
                    std::pair{ "UserString", "_pprint_user_string" } })
                    if (py const type = collection(name); !type.is_none())
                        add(type, formatter);
                py_checked(class_setattr_raw(printer.get(), "_dispatch", dispatch.get()) == 0);
                sSafeKey = safeKey.reference();
                sPrettyPrinter = printer.reference();
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        bool pprint_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            if (!make_pprint_classes())
                return false;
            for (auto const& f : sPprintFunctions)
                aAttributes.emplace_back(f.name, make_builtin(f));
            aAttributes.emplace_back("PrettyPrinter", new_reference(sPrettyPrinter));
            aAttributes.emplace_back("_safe_key", new_reference(sSafeKey));
            aAttributes.emplace_back("_safe_tuple", safe_tuple().reference());
            {
                auto const scalars = py_guarded([&]()
                {
                    return builtin("frozenset")(py_list({ builtin("str"), builtin("bytes"), builtin("bytearray"), builtin("float"), builtin("complex"),
                        builtin("bool"), type_of(none()) }));
                });
                if (scalars == nullptr)
                    return false;
                aAttributes.emplace_back("_builtin_scalars", scalars);
            }
            for (auto const& [name, module] : { std::pair{ "_collections", "collections" }, std::pair{ "_sys", "sys" }, std::pair{ "_types", "types" },
                std::pair{ "re", "re" } })
            {
                auto const m = fn_import(module);
                if (m == nullptr)
                    return false;
                aAttributes.emplace_back(name, m);
            }
            auto const stringIO = module_attribute("io", "StringIO");
            if (stringIO == nullptr)
                return false;
            aAttributes.emplace_back("_StringIO", stringIO);
            std::vector<object*> all;
            for (auto name : { "pprint", "pformat", "isreadable", "isrecursive", "saferepr", "PrettyPrinter", "pp" })
                all.push_back(make_str(name));
            aAttributes.emplace_back("__all__", make_list(std::move(all)));
            return true;
        }

        inline bool const sPprint = register_module("pprint", pprint_module);
    }
}
