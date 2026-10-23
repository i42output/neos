/*
  email_headerregistry.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/headerregistry.py), Copyright (c) 2001-2024 Python
  Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email.headerregistry (CPython 3.13's Lib/email/headerregistry.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_headerregistry_docs.hpp"

namespace neos::python_runtime::detail::email_headerregistry_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email.headerregistry.name"))
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
                py_raise("SystemError", std::string{ "email.headerregistry: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email.headerregistry.name">()))
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
            if (modules_imported("email.headerregistry"))
                return py{ fn_import("email.headerregistry") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email.headerregistry" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email.headerregistry", email_headerregistry_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email.headerregistry (Lib/email/headerregistry.py)

        void make_email_headerregistry()
        {
            static module_writer const& w = writer("email.headerregistry");
            define("email.headerregistry", "Address", w.make_class("Address", {  }, {
                { "__init__", w.method("Address", "__init__", { py_positional("display_name", S<"">()), py_positional("username", S<"">()), py_positional("domain", S<"">()), py_positional("addr_spec", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py display_name = a[1];
                        py username = a[2];
                        py domain = a[3];
                        py addr_spec = a[4];
                        py a_s;
                        py inputs;
                        py rest;
                        inputs = (S<"">()).method("join", { (B<"filter">())(none(), py_tuple({ display_name, username, domain, addr_spec })) });
                        if ((inputs.contains((S<"\r">())) || inputs.contains((S<"\n">()))))
                        {
                            py_raise((B<"ValueError">())(S<"invalid arguments; address parts cannot contain CR or LF">()));
                        }
                        // This clause with its potential 'raise' may only happen when an
                        // application program creates an Address object using an addr_spec
                        // keyword.  The email library code itself must always supply username
                        // and domain.
                        if (!addr_spec.is_none())
                        {
                            if ((username.truthy() || domain.truthy()))
                            {
                                py_raise((B<"TypeError">())(S<"addrspec specified when username and/or domain also specified">()));
                            }
                            auto const items1 = unpack((Mod<"email._header_value_parser">().attr("get_addr_spec"))(addr_spec), 2);
                            a_s = items1[0];
                            rest = items1[1];
                            if (rest.truthy())
                            {
                                py_raise((B<"ValueError">())((S<"Invalid addr_spec; only '{}' could be parsed from '{}'">()).method("format", { a_s, addr_spec })));
                            }
                            if (a_s.attr("all_defects").truthy())
                            {
                                py_raise(a_s.attr("all_defects")[I<0>()]);
                            }
                            username = a_s.attr("local_part");
                            domain = a_s.attr("domain");
                        }
                        self.set_attr("_display_name", display_name);
                        self.set_attr("_username", username);
                        self.set_attr("_domain", domain);
                        return none();
                    }) },
                { "display_name", B<"property">()(w.method("Address", "display_name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_display_name");
                    })) },
                { "username", B<"property">()(w.method("Address", "username", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_username");
                    })) },
                { "domain", B<"property">()(w.method("Address", "domain", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_domain");
                    })) },
                { "addr_spec", B<"property">()(w.method("Address", "addr_spec", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py lp;
                        lp = self.attr("username");
                        if (!(Mod<"email._header_value_parser">().attr("DOT_ATOM_ENDS")).method("isdisjoint", { lp }).truthy())
                        {
                            lp = (Mod<"email._header_value_parser">().attr("quote_string"))(lp);
                        }
                        if (self.attr("domain").truthy())
                        {
                            return (lp + (S<"@">())) + self.attr("domain");
                        }
                        if (!lp.truthy())
                        {
                            return S<"<>">();
                        }
                        return lp;
                    })) },
                { "__repr__", w.method("Address", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"{}(display_name={!r}, username={!r}, domain={!r})">()).method("format", { self.attr("__class__").attr("__name__"), self.attr("display_name"), self.attr("username"), self.attr("domain") });
                    }) },
                { "__str__", w.method("Address", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py addr_spec;
                        py disp;
                        disp = self.attr("display_name");
                        if (!(Mod<"email._header_value_parser">().attr("SPECIALS")).method("isdisjoint", { disp }).truthy())
                        {
                            disp = (Mod<"email._header_value_parser">().attr("quote_string"))(disp);
                        }
                        if (disp.truthy())
                        {
                            addr_spec = (self.attr("addr_spec") == (S<"<>">()) ? S<"">() : self.attr("addr_spec"));
                            return (S<"{} <{}>">()).method("format", { disp, addr_spec });
                        }
                        return self.attr("addr_spec");
                    }) },
                { "__eq__", w.method("Address", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (!isinstance(other, G<"email.headerregistry.Address">()))
                        {
                            return py::borrowed(not_implemented());
                        }
                        return [&]() -> py { py const t = boolean(self.attr("display_name") == other.attr("display_name")); return !t.truthy() ? t : [&]() -> py { py const t = boolean(self.attr("username") == other.attr("username")); return !t.truthy() ? t : boolean(self.attr("domain") == other.attr("domain")); }(); }();
                    }) }, }));

            define("email.headerregistry", "Group", w.make_class("Group", {  }, {
                { "__init__", w.method("Group", "__init__", { py_positional("display_name", none()), py_positional("addresses", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py display_name = a[1];
                        py addresses = a[2];
                        self.set_attr("_display_name", display_name);
                        self.set_attr("_addresses", (addresses.truthy() ? (B<"tuple">())(addresses) : (B<"tuple">())()));
                        return none();
                    }) },
                { "display_name", B<"property">()(w.method("Group", "display_name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_display_name");
                    })) },
                { "addresses", B<"property">()(w.method("Group", "addresses", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_addresses");
                    })) },
                { "__repr__", w.method("Group", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"{}(display_name={!r}, addresses={!r}">()).method("format", { self.attr("__class__").attr("__name__"), self.attr("display_name"), self.attr("addresses") });
                    }) },
                { "__str__", w.method("Group", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py adrstr;
                        py disp;
                        if ((self.attr("display_name").is_none() && (py{ self.attr("addresses").size() } == (I<1>()))))
                        {
                            return (B<"str">())(self.attr("addresses")[I<0>()]);
                        }
                        disp = self.attr("display_name");
                        if (((!disp.is_none()) && (!(Mod<"email._header_value_parser">().attr("SPECIALS")).method("isdisjoint", { disp }).truthy())))
                        {
                            disp = (Mod<"email._header_value_parser">().attr("quote_string"))(disp);
                        }
                        adrstr = (S<", ">()).method("join", { [&]() -> py { py const comprehension2 = py_list(); py x; for (py const& item : iterate{ self.attr("addresses") }) { x = item; comprehension2.method("append", { (B<"str">())(x) }); } return comprehension2; }() });
                        adrstr = (adrstr.truthy() ? (S<" ">()) + adrstr : adrstr);
                        return (S<"{}:{};">()).method("format", { disp, adrstr });
                    }) },
                { "__eq__", w.method("Group", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (!isinstance(other, G<"email.headerregistry.Group">()))
                        {
                            return py::borrowed(not_implemented());
                        }
                        return [&]() -> py { py const t = boolean(self.attr("display_name") == other.attr("display_name")); return !t.truthy() ? t : boolean(self.attr("addresses") == other.attr("addresses")); }();
                    }) }, }));

            // Header Classes #
            define("email.headerregistry", "BaseHeader", w.make_class("BaseHeader", { B<"str">() }, {
                { "__new__", w.method("BaseHeader", "__new__", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py name = a[1];
                        py value = a[2];
                        py kwds;
                        py self;
                        kwds = dict_of({ { S<"defects">(), py_list() } });
                        cls.method("parse", { value, kwds });
                        if ((Mod<"email.utils">().attr("_has_surrogates"))(kwds[S<"decoded">()]).truthy())
                        {
                            kwds.set_item(S<"decoded">(), (Mod<"email.utils">().attr("_sanitize"))(kwds[S<"decoded">()]));
                        }
                        self = (B<"str">()).method("__new__", { cls, kwds[S<"decoded">()] });
                        del_item(kwds, S<"decoded">());
                        call_ex(self.attr("init"), { name }, py{}, kwds);
                        return self;
                    }) },
                { "init", w.method("BaseHeader", "init", { py_positional("name"), py_keyword("parse_tree"), py_keyword("defects") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py parse_tree = a[2];
                        py defects = a[3];
                        self.set_attr("_name", name);
                        self.set_attr("_parse_tree", parse_tree);
                        self.set_attr("_defects", defects);
                        return none();
                    }) },
                { "name", B<"property">()(w.method("BaseHeader", "name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_name");
                    })) },
                { "defects", B<"property">()(w.method("BaseHeader", "defects", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"tuple">())(self.attr("_defects"));
                    })) },
                { "__reduce__", w.method("BaseHeader", "__reduce__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py_tuple({ G<"email.headerregistry._reconstruct_header">(), py_tuple({ self.attr("__class__").attr("__name__"), self.attr("__class__").attr("__bases__"), (B<"str">())(self) }), self.method("__getstate__") });
                    }) },
                { "_reconstruct", w.class_method("BaseHeader", "_reconstruct", { py_positional("value") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py value = a[1];
                        return (B<"str">()).method("__new__", { cls, value });
                    }) },
                { "fold", w.method("BaseHeader", "fold", { py_keyword("policy") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py policy = a[1];
                        py header;
                        // At some point we need to put fws here if it was in the source.
                        header = (Mod<"email._header_value_parser">().attr("Header"))(py_list({ (Mod<"email._header_value_parser">().attr("HeaderLabel"))(py_list({ (Mod<"email._header_value_parser">().attr("ValueTerminal"))(self.attr("name"), S<"header-name">()), (Mod<"email._header_value_parser">().attr("ValueTerminal"))(S<":">(), S<"header-sep">()) })) }));
                        if (self.attr("_parse_tree").truthy())
                        {
                            header.method("append", { (Mod<"email._header_value_parser">().attr("CFWSList"))(py_list({ (Mod<"email._header_value_parser">().attr("WhiteSpaceTerminal"))(S<" ">(), S<"fws">()) })) });
                        }
                        header.method("append", { self.attr("_parse_tree") });
                        return header.method("fold", {  }, { { "policy", policy } });
                    }) }, }));

            define("email.headerregistry", "_reconstruct_header", w.function("_reconstruct_header", "_reconstruct_header", { py_positional("cls_name"), py_positional("bases"), py_positional("value") }, [](arguments a) -> py
                {
                    py cls_name = a[0];
                    py bases = a[1];
                    py value = a[2];
                    return (B<"type">())(cls_name, bases, py_dict()).method("_reconstruct", { value });
                }));

            define("email.headerregistry", "UnstructuredHeader", w.make_class("UnstructuredHeader", {  }, {
                { "max_count", none() },
                { "value_parser", (B<"staticmethod">())(Mod<"email._header_value_parser">().attr("get_unstructured")) },
                { "parse", w.class_method("UnstructuredHeader", "parse", { py_positional("value"), py_positional("kwds") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py value = a[1];
                        py kwds = a[2];
                        kwds.set_item(S<"parse_tree">(), cls.method("value_parser", { value }));
                        kwds.set_item(S<"decoded">(), (B<"str">())(kwds[S<"parse_tree">()]));
                        return none();
                    }) }, }));

            define("email.headerregistry", "UniqueUnstructuredHeader", w.make_class("UniqueUnstructuredHeader", { G<"email.headerregistry.UnstructuredHeader">() }, {
                { "max_count", I<1>() }, }));

                // This is used only for folding, not for creating 'decoded'.
            define("email.headerregistry", "DateHeader", w.make_class("DateHeader", {  }, {
                { "max_count", none() },
                { "value_parser", (B<"staticmethod">())(Mod<"email._header_value_parser">().attr("get_unstructured")) },
                { "parse", w.class_method("DateHeader", "parse", { py_positional("value"), py_positional("kwds") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py value = a[1];
                        py kwds = a[2];
                        if (!value.truthy())
                        {
                            kwds[S<"defects">()].method("append", { (Mod<"email.errors">().attr("HeaderMissingRequiredValue"))() });
                            kwds.set_item(S<"datetime">(), none());
                            kwds.set_item(S<"decoded">(), S<"">());
                            kwds.set_item(S<"parse_tree">(), (Mod<"email._header_value_parser">().attr("TokenList"))());
                            return none();
                        }
                        if (isinstance(value, B<"str">()))
                        {
                            kwds.set_item(S<"decoded">(), value);
                            try
                            {
                                value = (Mod<"email.utils">().attr("parsedate_to_datetime"))(value);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler3;
                                if (matches(handler3.exception, B<"ValueError">()))
                                {
                                    kwds[S<"defects">()].method("append", { (Mod<"email.errors">().attr("InvalidDateDefect"))(S<"Invalid date value or format">()) });
                                    kwds.set_item(S<"datetime">(), none());
                                    kwds.set_item(S<"parse_tree">(), (Mod<"email._header_value_parser">().attr("TokenList"))());
                                    return none();
                                }
                                else
                                    py_reraise(handler3.exception);
                            }
                        }
                        kwds.set_item(S<"datetime">(), value);
                        kwds.set_item(S<"decoded">(), (Mod<"email.utils">().attr("format_datetime"))(kwds[S<"datetime">()]));
                        kwds.set_item(S<"parse_tree">(), cls.method("value_parser", { kwds[S<"decoded">()] }));
                        return none();
                    }) },
                { "init", w.method("DateHeader", "init", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kw = a[2];
                        self.set_attr("_datetime", kw.method("pop", { S<"datetime">() }));
                        call_ex((B<"super">()(G<"email.headerregistry.DateHeader">(), self)).attr("init"), {  }, args, kw);
                        return none();
                    }, true, true) },
                { "datetime", B<"property">()(w.method("DateHeader", "datetime", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_datetime");
                    })) }, }));

            define("email.headerregistry", "UniqueDateHeader", w.make_class("UniqueDateHeader", { G<"email.headerregistry.DateHeader">() }, {
                { "max_count", I<1>() }, }));

            define("email.headerregistry", "AddressHeader", w.make_class("AddressHeader", {  }, {
                { "max_count", none() },
                { "value_parser", w.static_method("AddressHeader", "value_parser", { py_positional("value") }, [](arguments a) -> py
                    {
                        py value = a[0];
                        py address_list;
                        auto const items4 = unpack((Mod<"email._header_value_parser">().attr("get_address_list"))(value), 2);
                        address_list = items4[0];
                        value = items4[1];
                        if (value.truthy())
                            py_raise(B<"AssertionError">()(S<"this should not happen">()));
                        return address_list;
                    }) },
                { "parse", w.class_method("AddressHeader", "parse", { py_positional("value"), py_positional("kwds") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py value = a[1];
                        py kwds = a[2];
                        py addr;
                        py address_list;
                        py defects;
                        py groups;
                        if (isinstance(value, B<"str">()))
                        {
                            // We are translating here from the RFC language (address/mailbox)
                            // to our API language (group/address).
                            py const value5 = cls.method("value_parser", { value });
                            kwds.set_item(S<"parse_tree">(), value5);
                            address_list = value5;
                            groups = py_list();
                            for (py const& item : iterate{ address_list.attr("addresses") })
                            {
                                addr = item;
                                groups.method("append", { (G<"email.headerregistry.Group">())(addr.attr("display_name"), [&]() -> py { py const comprehension7 = py_list(); py mb; for (py const& item : iterate{ addr.attr("all_mailboxes") }) { mb = item; comprehension7.method("append", { (G<"email.headerregistry.Address">())([&]() -> py { py const t = mb.attr("display_name"); return t.truthy() ? t : S<"">(); }(), [&]() -> py { py const t = mb.attr("local_part"); return t.truthy() ? t : S<"">(); }(), [&]() -> py { py const t = mb.attr("domain"); return t.truthy() ? t : S<"">(); }()) }); } return comprehension7; }()) });
                            }
                            defects = (B<"list">())(address_list.attr("all_defects"));
                        }
                        else
                        {
                            // Assume it is Address/Group stuff
                            if (!has_attr(value, "__iter__"))
                            {
                                value = py_list({ value });
                            }
                            groups = [&]() -> py { py const comprehension8 = py_list(); py item_; for (py const& item : iterate{ value }) { item_ = item; comprehension8.method("append", { (!has_attr(item_, "addresses") ? (G<"email.headerregistry.Group">())(none(), py_list({ item_ })) : item_) }); } return comprehension8; }();
                            defects = py_list();
                        }
                        kwds.set_item(S<"groups">(), groups);
                        kwds.set_item(S<"defects">(), defects);
                        kwds.set_item(S<"decoded">(), (S<", ">()).method("join", { [&]() -> py { py const comprehension9 = py_list(); py item_; for (py const& item : iterate{ groups }) { item_ = item; comprehension9.method("append", { (B<"str">())(item_) }); } return comprehension9; }() }));
                        if (!kwds.contains((S<"parse_tree">())))
                        {
                            kwds.set_item(S<"parse_tree">(), cls.method("value_parser", { kwds[S<"decoded">()] }));
                        }
                        return none();
                    }) },
                { "init", w.method("AddressHeader", "init", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kw = a[2];
                        self.set_attr("_groups", (B<"tuple">())(kw.method("pop", { S<"groups">() })));
                        self.set_attr("_addresses", none());
                        call_ex((B<"super">()(G<"email.headerregistry.AddressHeader">(), self)).attr("init"), {  }, args, kw);
                        return none();
                    }, true, true) },
                { "groups", B<"property">()(w.method("AddressHeader", "groups", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_groups");
                    })) },
                { "addresses", B<"property">()(w.method("AddressHeader", "addresses", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_addresses").is_none())
                        {
                            self.set_attr("_addresses", (B<"tuple">())([&]() -> py { py const comprehension10 = py_list(); py address; py group; for (py const& item : iterate{ self.attr("_groups") }) { group = item; for (py const& item : iterate{ group.attr("addresses") }) { address = item; comprehension10.method("append", { address }); } } return comprehension10; }()));
                        }
                        return self.attr("_addresses");
                    })) }, }));

            define("email.headerregistry", "UniqueAddressHeader", w.make_class("UniqueAddressHeader", { G<"email.headerregistry.AddressHeader">() }, {
                { "max_count", I<1>() }, }));

            define("email.headerregistry", "SingleAddressHeader", w.make_class("SingleAddressHeader", { G<"email.headerregistry.AddressHeader">() }, {
                { "address", B<"property">()(w.method("SingleAddressHeader", "address", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (py{ self.attr("addresses").size() } != (I<1>()))
                        {
                            py_raise((B<"ValueError">())((S<"value of single address header {} is not a single address">()).method("format", { self.attr("name") })));
                        }
                        return self.attr("addresses")[I<0>()];
                    })) }, }));

            define("email.headerregistry", "UniqueSingleAddressHeader", w.make_class("UniqueSingleAddressHeader", { G<"email.headerregistry.SingleAddressHeader">() }, {
                { "max_count", I<1>() }, }));

            define("email.headerregistry", "MIMEVersionHeader", w.make_class("MIMEVersionHeader", {  }, {
                { "max_count", I<1>() },
                { "value_parser", (B<"staticmethod">())(Mod<"email._header_value_parser">().attr("parse_mime_version")) },
                { "parse", w.class_method("MIMEVersionHeader", "parse", { py_positional("value"), py_positional("kwds") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py value = a[1];
                        py kwds = a[2];
                        py parse_tree;
                        py const value11 = cls.method("value_parser", { value });
                        kwds.set_item(S<"parse_tree">(), value11);
                        parse_tree = value11;
                        kwds.set_item(S<"decoded">(), (B<"str">())(parse_tree));
                        kwds[S<"defects">()].method("extend", { parse_tree.attr("all_defects") });
                        kwds.set_item(S<"major">(), (parse_tree.attr("minor").is_none() ? none() : parse_tree.attr("major")));
                        kwds.set_item(S<"minor">(), parse_tree.attr("minor"));
                        if (!parse_tree.attr("minor").is_none())
                        {
                            kwds.set_item(S<"version">(), (S<"{}.{}">()).method("format", { kwds[S<"major">()], kwds[S<"minor">()] }));
                        }
                        else
                        {
                            kwds.set_item(S<"version">(), none());
                        }
                        return none();
                    }) },
                { "init", w.method("MIMEVersionHeader", "init", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kw = a[2];
                        self.set_attr("_version", kw.method("pop", { S<"version">() }));
                        self.set_attr("_major", kw.method("pop", { S<"major">() }));
                        self.set_attr("_minor", kw.method("pop", { S<"minor">() }));
                        call_ex((B<"super">()(G<"email.headerregistry.MIMEVersionHeader">(), self)).attr("init"), {  }, args, kw);
                        return none();
                    }, true, true) },
                { "major", B<"property">()(w.method("MIMEVersionHeader", "major", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_major");
                    })) },
                { "minor", B<"property">()(w.method("MIMEVersionHeader", "minor", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_minor");
                    })) },
                { "version", B<"property">()(w.method("MIMEVersionHeader", "version", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_version");
                    })) }, }));

                // Mixin that handles the params dict.  Must be subclassed and
                // a property value_parser for the specific header provided.
            define("email.headerregistry", "ParameterizedMIMEHeader", w.make_class("ParameterizedMIMEHeader", {  }, {
                { "max_count", I<1>() },
                { "parse", w.class_method("ParameterizedMIMEHeader", "parse", { py_positional("value"), py_positional("kwds") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py value = a[1];
                        py kwds = a[2];
                        py parse_tree;
                        py const value12 = cls.method("value_parser", { value });
                        kwds.set_item(S<"parse_tree">(), value12);
                        parse_tree = value12;
                        kwds.set_item(S<"decoded">(), (B<"str">())(parse_tree));
                        kwds[S<"defects">()].method("extend", { parse_tree.attr("all_defects") });
                        if (parse_tree.attr("params").is_none())
                        {
                            kwds.set_item(S<"params">(), py_dict());
                        }
                        else
                        {
                            // The MIME RFCs specify that parameter ordering is arbitrary.
                            kwds.set_item(S<"params">(), [&]() -> py { py const comprehension13 = py_dict(); py name; py value; for (py const& item : iterate{ parse_tree.attr("params") }) { auto const items14 = unpack(item, 2); name = items14[0]; value = items14[1]; comprehension13.set_item((Mod<"email.utils">().attr("_sanitize"))(name).method("lower"), (Mod<"email.utils">().attr("_sanitize"))(value)); } return comprehension13; }());
                        }
                        return none();
                    }) },
                { "init", w.method("ParameterizedMIMEHeader", "init", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kw = a[2];
                        self.set_attr("_params", kw.method("pop", { S<"params">() }));
                        call_ex((B<"super">()(G<"email.headerregistry.ParameterizedMIMEHeader">(), self)).attr("init"), {  }, args, kw);
                        return none();
                    }, true, true) },
                { "params", B<"property">()(w.method("ParameterizedMIMEHeader", "params", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (Mod<"types">().attr("MappingProxyType"))(self.attr("_params"));
                    })) }, }));

            define("email.headerregistry", "ContentTypeHeader", w.make_class("ContentTypeHeader", { G<"email.headerregistry.ParameterizedMIMEHeader">() }, {
                { "value_parser", (B<"staticmethod">())(Mod<"email._header_value_parser">().attr("parse_content_type_header")) },
                { "init", w.method("ContentTypeHeader", "init", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kw = a[2];
                        call_ex((B<"super">()(G<"email.headerregistry.ContentTypeHeader">(), self)).attr("init"), {  }, args, kw);
                        self.set_attr("_maintype", (Mod<"email.utils">().attr("_sanitize"))(self.attr("_parse_tree").attr("maintype")));
                        self.set_attr("_subtype", (Mod<"email.utils">().attr("_sanitize"))(self.attr("_parse_tree").attr("subtype")));
                        return none();
                    }, true, true) },
                { "maintype", B<"property">()(w.method("ContentTypeHeader", "maintype", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_maintype");
                    })) },
                { "subtype", B<"property">()(w.method("ContentTypeHeader", "subtype", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_subtype");
                    })) },
                { "content_type", B<"property">()(w.method("ContentTypeHeader", "content_type", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (self.attr("maintype") + (S<"/">())) + self.attr("subtype");
                    })) }, }));

            define("email.headerregistry", "ContentDispositionHeader", w.make_class("ContentDispositionHeader", { G<"email.headerregistry.ParameterizedMIMEHeader">() }, {
                { "value_parser", (B<"staticmethod">())(Mod<"email._header_value_parser">().attr("parse_content_disposition_header")) },
                { "init", w.method("ContentDispositionHeader", "init", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kw = a[2];
                        py cd;
                        call_ex((B<"super">()(G<"email.headerregistry.ContentDispositionHeader">(), self)).attr("init"), {  }, args, kw);
                        cd = self.attr("_parse_tree").attr("content_disposition");
                        self.set_attr("_content_disposition", (cd.is_none() ? cd : (Mod<"email.utils">().attr("_sanitize"))(cd)));
                        return none();
                    }, true, true) },
                { "content_disposition", B<"property">()(w.method("ContentDispositionHeader", "content_disposition", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_content_disposition");
                    })) }, }));

            define("email.headerregistry", "ContentTransferEncodingHeader", w.make_class("ContentTransferEncodingHeader", {  }, {
                { "max_count", I<1>() },
                { "value_parser", (B<"staticmethod">())(Mod<"email._header_value_parser">().attr("parse_content_transfer_encoding_header")) },
                { "parse", w.class_method("ContentTransferEncodingHeader", "parse", { py_positional("value"), py_positional("kwds") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py value = a[1];
                        py kwds = a[2];
                        py parse_tree;
                        py const value15 = cls.method("value_parser", { value });
                        kwds.set_item(S<"parse_tree">(), value15);
                        parse_tree = value15;
                        kwds.set_item(S<"decoded">(), (B<"str">())(parse_tree));
                        kwds[S<"defects">()].method("extend", { parse_tree.attr("all_defects") });
                        return none();
                    }) },
                { "init", w.method("ContentTransferEncodingHeader", "init", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kw = a[2];
                        call_ex((B<"super">()(G<"email.headerregistry.ContentTransferEncodingHeader">(), self)).attr("init"), {  }, args, kw);
                        self.set_attr("_cte", (Mod<"email.utils">().attr("_sanitize"))(self.attr("_parse_tree").attr("cte")));
                        return none();
                    }, true, true) },
                { "cte", B<"property">()(w.method("ContentTransferEncodingHeader", "cte", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_cte");
                    })) }, }));

            define("email.headerregistry", "MessageIDHeader", w.make_class("MessageIDHeader", {  }, {
                { "max_count", I<1>() },
                { "value_parser", (B<"staticmethod">())(Mod<"email._header_value_parser">().attr("parse_message_id")) },
                { "parse", w.class_method("MessageIDHeader", "parse", { py_positional("value"), py_positional("kwds") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py value = a[1];
                        py kwds = a[2];
                        py parse_tree;
                        py const value16 = cls.method("value_parser", { value });
                        kwds.set_item(S<"parse_tree">(), value16);
                        parse_tree = value16;
                        kwds.set_item(S<"decoded">(), (B<"str">())(parse_tree));
                        kwds[S<"defects">()].method("extend", { parse_tree.attr("all_defects") });
                        return none();
                    }) }, }));

            define("email.headerregistry", "ReferencesHeader", w.make_class("ReferencesHeader", {  }, {
                { "max_count", I<1>() },
                { "value_parser", (B<"staticmethod">())(Mod<"email._header_value_parser">().attr("parse_message_ids")) },
                { "parse", w.class_method("ReferencesHeader", "parse", { py_positional("value"), py_positional("kwds") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py value = a[1];
                        py kwds = a[2];
                        py parse_tree;
                        py const value17 = cls.method("value_parser", { value });
                        kwds.set_item(S<"parse_tree">(), value17);
                        parse_tree = value17;
                        kwds.set_item(S<"decoded">(), (B<"str">())(parse_tree));
                        kwds[S<"defects">()].method("extend", { parse_tree.attr("all_defects") });
                        return none();
                    }) }, }));

            // The header factory #
            define("email.headerregistry", "_default_header_map", dict_of({ { S<"subject">(), G<"email.headerregistry.UniqueUnstructuredHeader">() }, { S<"date">(), G<"email.headerregistry.UniqueDateHeader">() }, { S<"resent-date">(), G<"email.headerregistry.DateHeader">() }, { S<"orig-date">(), G<"email.headerregistry.UniqueDateHeader">() }, { S<"sender">(), G<"email.headerregistry.UniqueSingleAddressHeader">() }, { S<"resent-sender">(), G<"email.headerregistry.SingleAddressHeader">() }, { S<"to">(), G<"email.headerregistry.UniqueAddressHeader">() }, { S<"resent-to">(), G<"email.headerregistry.AddressHeader">() }, { S<"cc">(), G<"email.headerregistry.UniqueAddressHeader">() }, { S<"resent-cc">(), G<"email.headerregistry.AddressHeader">() }, { S<"bcc">(), G<"email.headerregistry.UniqueAddressHeader">() }, { S<"resent-bcc">(), G<"email.headerregistry.AddressHeader">() }, { S<"from">(), G<"email.headerregistry.UniqueAddressHeader">() }, { S<"resent-from">(), G<"email.headerregistry.AddressHeader">() }, { S<"reply-to">(), G<"email.headerregistry.UniqueAddressHeader">() }, { S<"mime-version">(), G<"email.headerregistry.MIMEVersionHeader">() }, { S<"content-type">(), G<"email.headerregistry.ContentTypeHeader">() }, { S<"content-disposition">(), G<"email.headerregistry.ContentDispositionHeader">() }, { S<"content-transfer-encoding">(), G<"email.headerregistry.ContentTransferEncodingHeader">() }, { S<"message-id">(), G<"email.headerregistry.MessageIDHeader">() }, { S<"in-reply-to">(), G<"email.headerregistry.ReferencesHeader">() }, { S<"references">(), G<"email.headerregistry.ReferencesHeader">() } }));
            define("email.headerregistry", "HeaderRegistry", w.make_class("HeaderRegistry", {  }, {
                { "__init__", w.method("HeaderRegistry", "__init__", { py_positional("base_class", G<"email.headerregistry.BaseHeader">()), py_positional("default_class", G<"email.headerregistry.UnstructuredHeader">()), py_positional("use_default_map", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py base_class = a[1];
                        py default_class = a[2];
                        py use_default_map = a[3];
                        self.set_attr("registry", py_dict());
                        self.set_attr("base_class", base_class);
                        self.set_attr("default_class", default_class);
                        if (use_default_map.truthy())
                        {
                            self.attr("registry").method("update", { G<"email.headerregistry._default_header_map">() });
                        }
                        return none();
                    }) },
                { "map_to_type", w.method("HeaderRegistry", "map_to_type", { py_positional("name"), py_positional("cls") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py cls = a[2];
                        self.attr("registry").set_item(name.method("lower"), cls);
                        return none();
                    }) },
                { "__getitem__", w.method("HeaderRegistry", "__getitem__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py cls;
                        cls = self.attr("registry").method("get", { name.method("lower"), self.attr("default_class") });
                        return (B<"type">())((S<"_">()) + cls.attr("__name__"), py_tuple({ cls, self.attr("base_class") }), py_dict());
                    }) },
                { "__call__", w.method("HeaderRegistry", "__call__", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        return self[name](name, value);
                    }) }, }));
        }

        bool email_headerregistry_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email_headerregistry();
                }
                if (auto const doc = email_headerregistry_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("MappingProxyType", import_attribute("types", "MappingProxyType").release());
                aAttributes.emplace_back("utils", import_attribute("email", "utils").release());
                aAttributes.emplace_back("errors", import_attribute("email", "errors").release());
                aAttributes.emplace_back("parser", import_attribute("email", "_header_value_parser").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email.headerregistry", email_headerregistry_module);
    }
}
