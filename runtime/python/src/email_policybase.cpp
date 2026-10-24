/*
  email_policybase.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/_policybase.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email._policybase (CPython 3.13's Lib/email/_policybase.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_policybase_docs.hpp"

namespace neos::python_runtime::detail::email__policybase_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email._policybase.name"))
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
                py_raise("SystemError", std::string{ "email._policybase: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email._policybase.name">()))
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
        [[maybe_unused]] void set_global(char const* aModule, char const* aName, py const& aValue)
        {
            define(aModule, aName, aValue);
            if (modules_imported(aModule))
                py{ fn_import(aModule) }.set_attr(aName, aValue);
        }
        // (globals(): the module's namespace (its __dict__ once it has been imported; while it is being made, its attributes so far))
        [[maybe_unused]] py module_globals()
        {
            if (modules_imported("email._policybase"))
                return py{ fn_import("email._policybase") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email._policybase" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email._policybase", email_policybase_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email._policybase (Lib/email/_policybase.py)

        void make_email__policybase()
        {
            static module_writer const& w = writer("email._policybase");
            define("email._policybase", "__all__", py_list({ S<"Policy">(), S<"Compat32">(), S<"compat32">() }));
            define("email._policybase", "_PolicyBase", w.make_class("_PolicyBase", {  }, {
                { "__init__", w.method("_PolicyBase", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py kw = a[1];
                        py name;
                        py value;
                        for (py const& item : iterate{ kw.method("items") })
                        {
                            auto const items2 = unpack(item, 2);
                            name = items2[0];
                            value = items2[1];
                            if ((B<"hasattr">())(self, name).truthy())
                            {
                                (B<"super">())(G<"email._policybase._PolicyBase">(), self).method("__setattr__", { name, value });
                            }
                            else
                            {
                                py_raise((B<"TypeError">())((S<"{!r} is an invalid keyword argument for {}">()).method("format", { name, self.attr("__class__").attr("__name__") })));
                            }
                        }
                        return none();
                    }, false, true) },
                { "__repr__", w.method("_PolicyBase", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args;
                        args = [&]() -> py { py const comprehension3 = py_list(); py name; py value; for (py const& item : iterate{ self.attr("__dict__").method("items") }) { auto const items4 = unpack(item, 2); name = items4[0]; value = items4[1]; comprehension3.method("append", { (S<"{}={!r}">()).method("format", { name, value }) }); } return comprehension3; }();
                        return (S<"{}({})">()).method("format", { self.attr("__class__").attr("__name__"), (S<", ">()).method("join", { args }) });
                    }) },
                { "clone", w.method("_PolicyBase", "clone", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py kw = a[1];
                        py attr;
                        py newpolicy;
                        py value;
                        newpolicy = self.attr("__class__").method("__new__", { self.attr("__class__") });
                        for (py const& item : iterate{ self.attr("__dict__").method("items") })
                        {
                            auto const items6 = unpack(item, 2);
                            attr = items6[0];
                            value = items6[1];
                            (B<"object">()).method("__setattr__", { newpolicy, attr, value });
                        }
                        for (py const& item : iterate{ kw.method("items") })
                        {
                            auto const items8 = unpack(item, 2);
                            attr = items8[0];
                            value = items8[1];
                            if (!(B<"hasattr">())(self, attr).truthy())
                            {
                                py_raise((B<"TypeError">())((S<"{!r} is an invalid keyword argument for {}">()).method("format", { attr, self.attr("__class__").attr("__name__") })));
                            }
                            (B<"object">()).method("__setattr__", { newpolicy, attr, value });
                        }
                        return newpolicy;
                    }, false, true) },
                { "__setattr__", w.method("_PolicyBase", "__setattr__", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        py msg;
                        if ((B<"hasattr">())(self, name).truthy())
                        {
                            msg = S<"{!r} object attribute {!r} is read-only">();
                        }
                        else
                        {
                            msg = S<"{!r} object has no attribute {!r}">();
                        }
                        py_raise((B<"AttributeError">())(msg.method("format", { self.attr("__class__").attr("__name__"), name })));
                    }) },
                { "__add__", w.method("_PolicyBase", "__add__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        return call_ex(self.attr("clone"), {  }, py{}, other.attr("__dict__"));
                    }) }, }));

            define("email._policybase", "_append_doc", w.function("_append_doc", "_append_doc", { py_positional("doc"), py_positional("added_doc") }, [](arguments a) -> py
                {
                    py doc = a[0];
                    py added_doc = a[1];
                    doc = doc.method("rsplit", { S<"\n">(), I<1>() })[I<0>()];
                    added_doc = added_doc.method("split", { S<"\n">(), I<1>() })[I<1>()];
                    return (doc + (S<"\n">())) + added_doc;
                }));

            define("email._policybase", "_extend_docstrings", w.function("_extend_docstrings", "_extend_docstrings", { py_positional("cls") }, [](arguments a) -> py
                {
                    py cls = a[0];
                    py attr;
                    py c;
                    py doc;
                    py name;
                    if ((cls.attr("__doc__").truthy() && cls.attr("__doc__").method("startswith", { S<"+">() }).truthy()))
                    {
                        cls.set_attr("__doc__", (G<"email._policybase._append_doc">())(cls.attr("__bases__")[I<0>()].attr("__doc__"), cls.attr("__doc__")));
                    }
                    for (py const& item : iterate{ cls.attr("__dict__").method("items") })
                    {
                        auto const items10 = unpack(item, 2);
                        name = items10[0];
                        attr = items10[1];
                        if ((attr.attr("__doc__").truthy() && attr.attr("__doc__").method("startswith", { S<"+">() }).truthy()))
                        {
                            for (py const& item : iterate{ [&]() -> py { py const comprehension11 = py_list(); py base; py c; for (py const& item : iterate{ cls.attr("__bases__") }) { base = item; for (py const& item : iterate{ base.method("mro") }) { c = item; comprehension11.method("append", { c }); } } return comprehension11; }() })
                            {
                                c = item;
                                doc = (B<"getattr">())((B<"getattr">())(c, name), S<"__doc__">());
                                if (doc.truthy())
                                {
                                    attr.set_attr("__doc__", (G<"email._policybase._append_doc">())(doc, attr.attr("__doc__")));
                                    break;
                                }
                            }
                        }
                    }
                    return cls;
                }));

            define("email._policybase", "Policy", w.make_class("Policy", { G<"email._policybase._PolicyBase">() }, {
                { "raise_on_defect", boolean(false) },
                { "linesep", S<"\n">() },
                { "cte_type", S<"8bit">() },
                { "max_line_length", I<78>() },
                { "mangle_from_", boolean(false) },
                { "message_factory", none() },
                { "verify_generated_headers", boolean(true) },
                { "handle_defect", w.method("Policy", "handle_defect", { py_positional("obj"), py_positional("defect") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py obj = a[1];
                        py defect = a[2];
                        if (self.attr("raise_on_defect").truthy())
                        {
                            py_raise(defect);
                        }
                        self.method("register_defect", { obj, defect });
                        return none();
                    }) },
                { "register_defect", w.method("Policy", "register_defect", { py_positional("obj"), py_positional("defect") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py obj = a[1];
                        py defect = a[2];
                        obj.attr("defects").method("append", { defect });
                        return none();
                    }) },
                { "header_max_count", w.method("Policy", "header_max_count", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return none();
                    }) },
                { "header_source_parse", (Mod<"abc">().attr("abstractmethod"))(w.method("Policy", "header_source_parse", { py_positional("sourcelines") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sourcelines = a[1];
                        py_raise(B<"NotImplementedError">());
                    })) },
                { "header_store_parse", (Mod<"abc">().attr("abstractmethod"))(w.method("Policy", "header_store_parse", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        py_raise(B<"NotImplementedError">());
                    })) },
                { "header_fetch_parse", (Mod<"abc">().attr("abstractmethod"))(w.method("Policy", "header_fetch_parse", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        py_raise(B<"NotImplementedError">());
                    })) },
                { "fold", (Mod<"abc">().attr("abstractmethod"))(w.method("Policy", "fold", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        py_raise(B<"NotImplementedError">());
                    })) },
                { "fold_binary", (Mod<"abc">().attr("abstractmethod"))(w.method("Policy", "fold_binary", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        py_raise(B<"NotImplementedError">());
                    })) }, }));

            define("email._policybase", "Compat32", (G<"email._policybase._extend_docstrings">())(w.make_class("Compat32", { G<"email._policybase.Policy">() }, {
                { "mangle_from_", boolean(true) },
                { "_sanitize_header", w.method("Compat32", "_sanitize_header", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        if (!isinstance(value, B<"str">()))
                        {
                            // Assume it is already a header object
                            return value;
                        }
                        if ((Mod<"email.utils">().attr("_has_surrogates"))(value).truthy())
                        {
                            return (Mod<"email.header">().attr("Header")).call({ value }, { { "charset", Mod<"email.charset">().attr("UNKNOWN8BIT") }, { "header_name", name } });
                        }
                        else
                        {
                            return value;
                        }
                        return none();
                    }) },
                { "header_source_parse", w.method("Compat32", "header_source_parse", { py_positional("sourcelines") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sourcelines = a[1];
                        py name;
                        py value;
                        auto const items13 = unpack(sourcelines[I<0>()].method("split", { S<":">(), I<1>() }), 2);
                        name = items13[0];
                        value = items13[1];
                        value = (S<"">()).method("join", { B<"tuple">()(((py_list() + py_list({ value })) + B<"list">()(sourcelines[B<"slice">()(I<1>(), none(), none())]))) }).method("lstrip", { S<" \t\r\n">() });
                        return py_tuple({ name, value.method("rstrip", { S<"\r\n">() }) });
                    }) },
                { "header_store_parse", w.method("Compat32", "header_store_parse", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        return py_tuple({ name, value });
                    }) },
                { "header_fetch_parse", w.method("Compat32", "header_fetch_parse", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        return self.method("_sanitize_header", { name, value });
                    }) },
                { "fold", w.method("Compat32", "fold", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        return self.method("_fold", { name, value }, { { "sanitize", boolean(true) } });
                    }) },
                { "fold_binary", w.method("Compat32", "fold_binary", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        py folded;
                        folded = self.method("_fold", { name, value }, { { "sanitize", boolean(self.attr("cte_type") == (S<"7bit">())) } });
                        return folded.method("encode", { S<"ascii">(), S<"surrogateescape">() });
                    }) },
                { "_fold", w.method("Compat32", "_fold", { py_positional("name"), py_positional("value"), py_positional("sanitize") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        py sanitize = a[3];
                        py h;
                        py maxlinelen;
                        py parts;
                        parts = py_list();
                        parts.method("append", { (S<"%s: ">()) % name });
                        if (isinstance(value, B<"str">()))
                        {
                            if ((Mod<"email.utils">().attr("_has_surrogates"))(value).truthy())
                            {
                                if (sanitize.truthy())
                                {
                                    h = (Mod<"email.header">().attr("Header")).call({ value }, { { "charset", Mod<"email.charset">().attr("UNKNOWN8BIT") }, { "header_name", name } });
                                }
                                else
                                {
                                    // If we have raw 8bit data in a string, we have no idea
                                    // what the encoding is.  There is no safe way to split this
                                    // string.  If it's ascii-subset, then we could do a normal
                                    // ascii split, but if it's multibyte then we could break the
                                    // string.  There's no way to know so the least harm seems to
                                    // be to not split the string and risk it being too long.
                                    parts.method("append", { value });
                                    h = none();
                                }
                            }
                            else
                            {
                                h = (Mod<"email.header">().attr("Header")).call({ value }, { { "header_name", name } });
                            }
                        }
                        else
                        {
                            // Assume it is a Header-like object.
                            h = value;
                        }
                        if (!h.is_none())
                        {
                            // The Header class interprets a value of None for maxlinelen as the
                            // default value of 78, as recommended by RFC 5322 section 2.1.1.
                            maxlinelen = I<0>();
                            if (!self.attr("max_line_length").is_none())
                            {
                                maxlinelen = self.attr("max_line_length");
                            }
                            parts.method("append", { h.method("encode", {  }, { { "linesep", self.attr("linesep") }, { "maxlinelen", maxlinelen } }) });
                        }
                        parts.method("append", { self.attr("linesep") });
                        return (S<"">()).method("join", { parts });
                    }) }, })));

            define("email._policybase", "compat32", (G<"email._policybase.Compat32">())());
        }

        bool email__policybase_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email__policybase();
                }
                if (auto const doc = email_policybase_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("abc", py{ fn_import("abc") }.release());
                aAttributes.emplace_back("header", import_attribute("email", "header").release());
                aAttributes.emplace_back("_charset", import_attribute("email", "charset").release());
                aAttributes.emplace_back("_has_surrogates", import_attribute("email.utils", "_has_surrogates").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email._policybase", email__policybase_module);
    }
}
