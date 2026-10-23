/*
  wsgiref_headers.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/wsgiref/headers.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module wsgiref.headers (CPython 3.13's Lib/wsgiref/headers.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "wsgiref_headers_docs.hpp"

namespace neos::python_runtime::detail::wsgiref_headers_module
{
    namespace
    {
        using pyclass::S;
        using pyclass::I;
        using pyclass::BigI;
        using pyclass::F;
        using pyclass::B;
        using pyclass::Mod;
        using pyclass::none;
        using pyclass::boolean;
        using pyclass::M;
        using pyclass::has_attr;
        using pyclass::get_attr;
        using pyclass::callable;
        using pyclass::module_writer;
        using pyclass::namespace_type;
        using pyclass::try_except;
        using pyclass::try_finally;
        using pyclass::with_;
        using pyclass::unpack2;
        using pyclass::format;
        using pyclass::handler_scope;
        using pyclass::matches;
        using pyclass::iterate;
        using pyclass::unpack;
        using pyclass::call_ex;
        using pyclass::dict_of;
        using pyclass::set_of;
        using pyclass::inplace;
        using pyclass::del_attr;
        using pyclass::del_item;
        using pyclass::formatted;
        using pyclass::yield_from;
        using pyclass::truth_of;
        using pyclass::module_of;
        using pyclass::handled_exception;
        // (the module's objects (made once, and never destroyed: they outlive the program's objects))
        struct state
        {
            std::vector<std::pair<std::string, py>> attributes;     ///< (the module's attributes, in order)
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "wsgiref.headers.name"))
            bool made = false;
        };
        state& the_state()
        {
            static state* sState = new state{};
            return *sState;
        }

        // (its entry: the object, as it is defined (or assigned) now)
        py const& object_entry(char const* aQualifiedName)
        {
            auto& s = the_state();
            auto const o = s.objects.find(aQualifiedName);
            if (o == s.objects.end())
                py_raise("SystemError", std::string{ "wsgiref.headers: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"wsgiref.headers.name">()))
        template <pyclass::fixed_string Name>
        py const& G() { static py const* const sEntry = &object_entry(Name.value); return *sEntry; }
        // (an object made: one of the module's attributes)
        void define(char const* aModule, char const* aName, py const& aValue)
        {
            auto& s = the_state();
            s.objects[std::string{ aModule } + "." + aName] = aValue;
            for (auto& a : s.attributes)
                if (a.first == aName)
                {
                    a.second = aValue;
                    return;
                }
            s.attributes.emplace_back(aName, aValue);
        }
        // (del name: no longer one of the module's attributes (its object remains, for the module's functions))
        [[maybe_unused]] void undefine(char const* aModule, char const* aName)
        {
            auto& s = the_state();
            (void)aModule;
            std::erase_if(s.attributes, [&](auto const& a) { return a.first == aName; });
        }
        // (a module's global assigned (global name; name = value): its object, and the module's attribute (if it has been imported))
        void set_global(char const* aModule, char const* aName, py const& aValue)
        {
            define(aModule, aName, aValue);
            if (modules_imported(aModule))
                py{ fn_import(aModule) }.set_attr(aName, aValue);
        }
        // (globals(): the module's namespace (its __dict__ once it has been imported; while it is being made, its attributes so far))
        [[maybe_unused]] py module_globals()
        {
            if (modules_imported("wsgiref.headers"))
                return py{ fn_import("wsgiref.headers") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "wsgiref.headers" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "wsgiref.headers", wsgiref_headers_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- wsgiref.headers (Lib/wsgiref/headers.py)

        void make_wsgiref_headers()
        {
            static module_writer const& w = writer("wsgiref.headers");
            // Regular expression that matches `special' characters in parameters, the
            // existence of which force quoting of the parameter value.
            define("wsgiref.headers", "tspecials", (Mod<"re">()).method("compile", { S<"[ \\(\\)<>@,;:\\\\\"/\\[\\]\\?=]">() }));
            // Disallowed characters for headers and values.
            // HTAB (\x09) is allowed in header values, but
            // not in header names. (RFC 9110 Section 5.5)
            define("wsgiref.headers", "_name_disallowed_re", (Mod<"re">()).method("compile", { S<"[\\x00-\\x1F\\x7F]">() }));
            define("wsgiref.headers", "_value_disallowed_re", (Mod<"re">()).method("compile", { S<"[\\x00-\\x08\\x0A-\\x1F\\x7F]">() }));
            define("wsgiref.headers", "_formatparam", w.function("_formatparam", "_formatparam", { py_positional("param"), py_positional("value", none()), py_positional("quote", I<1>()) }, [](arguments a) -> py
                {
                    py param = a[0];
                    py value = a[1];
                    py quote = a[2];
                    if (((!value.is_none()) && (py{ value.size() } > (I<0>()))))
                    {
                        if ((quote.truthy() || (G<"wsgiref.headers.tspecials">()).method("search", { value }).truthy()))
                        {
                            value = value.method("replace", { S<"\\">(), S<"\\\\">() }).method("replace", { S<"\"">(), S<"\\\"">() });
                            return (S<"%s=\"%s\"">()) % py_tuple({ param, value });
                        }
                        else
                        {
                            return (S<"%s=%s">()) % py_tuple({ param, value });
                        }
                    }
                    else
                    {
                        return param;
                    }
                    return none();
                }));

            define("wsgiref.headers", "Headers", w.make_class("Headers", {  }, {
                { "__init__", w.method("Headers", "__init__", { py_positional("headers", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py headers = a[1];
                        py k;
                        py v;
                        headers = (!headers.is_none() ? headers : py_list());
                        if (!(B<"type">())(headers).is((B<"list">())))
                        {
                            py_raise((B<"TypeError">())(S<"Headers must be a list of name/value tuples">()));
                        }
                        self.set_attr("_headers", headers);
                        if ((B<"__debug__">()).truthy())
                        {
                            for (py const& item : iterate{ headers })
                            {
                                auto const items2 = unpack(item, 2);
                                k = items2[0];
                                v = items2[1];
                                self.method("_convert_string_type", { k }, { { "name", boolean(true) } });
                                self.method("_convert_string_type", { v }, { { "name", boolean(false) } });
                            }
                        }
                        return none();
                    }) },
                { "_convert_string_type", w.method("Headers", "_convert_string_type", { py_positional("value"), py_keyword("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py name = a[2];
                        py regex;
                        if ((B<"type">())(value).is((B<"str">())))
                        {
                            regex = (name.truthy() ? G<"wsgiref.headers._name_disallowed_re">() : G<"wsgiref.headers._value_disallowed_re">());
                            if (regex.method("search", { value }).truthy())
                            {
                                py_raise((B<"ValueError">())(S<"Control characters not allowed in headers">()));
                            }
                            return value;
                        }
                        py_raise((B<"AssertionError">())((S<"Header names/values must be of type str (got {0})">()).method("format", { S(value.repr()) })));
                    }) },
                { "__len__", w.method("Headers", "__len__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py{ self.attr("_headers").size() };
                    }) },
                { "__setitem__", w.method("Headers", "__setitem__", { py_positional("name"), py_positional("val") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py val = a[2];
                        del_item(self, name);
                        self.attr("_headers").method("append", { py_tuple({ self.method("_convert_string_type", { name }, { { "name", boolean(true) } }), self.method("_convert_string_type", { val }, { { "name", boolean(false) } }) }) });
                        return none();
                    }) },
                { "__delitem__", w.method("Headers", "__delitem__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        name = self.method("_convert_string_type", { name.method("lower") }, { { "name", boolean(true) } });
                        self.attr("_headers").set_item(B<"slice">()(none(), none(), none()), [&]() -> py { py const comprehension3 = py_list(); py kv; for (py const& item : iterate{ self.attr("_headers") }) { kv = item; if (!(kv[I<0>()].method("lower") != name)) continue; comprehension3.method("append", { kv }); } return comprehension3; }());
                        return none();
                    }) },
                { "__getitem__", w.method("Headers", "__getitem__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return self.method("get", { name });
                    }) },
                { "__contains__", w.method("Headers", "__contains__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return boolean(!self.method("get", { name }).is_none());
                    }) },
                { "get_all", w.method("Headers", "get_all", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        name = self.method("_convert_string_type", { name.method("lower") }, { { "name", boolean(true) } });
                        return [&]() -> py { py const comprehension4 = py_list(); py kv; for (py const& item : iterate{ self.attr("_headers") }) { kv = item; if (!(kv[I<0>()].method("lower") == name)) continue; comprehension4.method("append", { kv[I<1>()] }); } return comprehension4; }();
                    }) },
                { "get", w.method("Headers", "get", { py_positional("name"), py_positional("default", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py default_ = a[2];
                        py k;
                        py v;
                        name = self.method("_convert_string_type", { name.method("lower") }, { { "name", boolean(true) } });
                        for (py const& item : iterate{ self.attr("_headers") })
                        {
                            auto const items6 = unpack(item, 2);
                            k = items6[0];
                            v = items6[1];
                            if (k.method("lower") == name)
                            {
                                return v;
                            }
                        }
                        return default_;
                    }) },
                { "keys", w.method("Headers", "keys", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const comprehension7 = py_list(); py k; py v; for (py const& item : iterate{ self.attr("_headers") }) { auto const items8 = unpack(item, 2); k = items8[0]; v = items8[1]; comprehension7.method("append", { k }); } return comprehension7; }();
                    }) },
                { "values", w.method("Headers", "values", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return [&]() -> py { py const comprehension9 = py_list(); py k; py v; for (py const& item : iterate{ self.attr("_headers") }) { auto const items10 = unpack(item, 2); k = items10[0]; v = items10[1]; comprehension9.method("append", { v }); } return comprehension9; }();
                    }) },
                { "items", w.method("Headers", "items", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_headers")[B<"slice">()(none(), none(), none())];
                    }) },
                { "__repr__", w.method("Headers", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s(%r)">()) % py_tuple({ self.attr("__class__").attr("__name__"), self.attr("_headers") });
                    }) },
                { "__str__", w.method("Headers", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"\r\n">()).method("join", { ([&]() -> py { py const comprehension11 = py_list(); py kv; for (py const& item : iterate{ self.attr("_headers") }) { kv = item; comprehension11.method("append", { (S<"%s: %s">()) % kv }); } return comprehension11; }()) + py_list({ S<"">(), S<"">() }) });
                    }) },
                { "__bytes__", w.method("Headers", "__bytes__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"str">())(self).method("encode", { S<"iso-8859-1">() });
                    }) },
                { "setdefault", w.method("Headers", "setdefault", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        py result;
                        result = self.method("get", { name });
                        if (result.is_none())
                        {
                            self.attr("_headers").method("append", { py_tuple({ self.method("_convert_string_type", { name }, { { "name", boolean(true) } }), self.method("_convert_string_type", { value }, { { "name", boolean(false) } }) }) });
                            return value;
                        }
                        else
                        {
                            return result;
                        }
                        return none();
                    }) },
                { "add_header", w.method("Headers", "add_header", { py_positional("_name"), py_positional("_value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _name = a[1];
                        py _value = a[2];
                        py _params = a[3];
                        py k;
                        py parts;
                        py v;
                        parts = py_list();
                        if (!_value.is_none())
                        {
                            _value = self.method("_convert_string_type", { _value }, { { "name", boolean(false) } });
                            parts.method("append", { _value });
                        }
                        for (py const& item : iterate{ _params.method("items") })
                        {
                            auto const items13 = unpack(item, 2);
                            k = items13[0];
                            v = items13[1];
                            k = self.method("_convert_string_type", { k }, { { "name", boolean(true) } });
                            if (v.is_none())
                            {
                                parts.method("append", { k.method("replace", { S<"_">(), S<"-">() }) });
                            }
                            else
                            {
                                v = self.method("_convert_string_type", { v }, { { "name", boolean(false) } });
                                parts.method("append", { (G<"wsgiref.headers._formatparam">())(k.method("replace", { S<"_">(), S<"-">() }), v) });
                            }
                        }
                        self.attr("_headers").method("append", { py_tuple({ self.method("_convert_string_type", { _name }, { { "name", boolean(true) } }), (S<"; ">()).method("join", { parts }) }) });
                        return none();
                    }, false, true) }, }));
        }

        bool wsgiref_headers_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_wsgiref_headers();
                }
                if (auto const doc = wsgiref_headers_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("wsgiref.headers", wsgiref_headers_module);
    }
}
