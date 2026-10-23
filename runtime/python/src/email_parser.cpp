/*
  email_parser.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/email/parser.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module email.parser (CPython 3.13's Lib/email/parser.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "email_parser_docs.hpp"

namespace neos::python_runtime::detail::email_parser_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "email.parser.name"))
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
                py_raise("SystemError", std::string{ "email.parser: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"email.parser.name">()))
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
            if (modules_imported("email.parser"))
                return py{ fn_import("email.parser") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "email.parser" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "email.parser", email_parser_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- email.parser (Lib/email/parser.py)

        void make_email_parser()
        {
            static module_writer const& w = writer("email.parser");
            // Copyright (C) 2001-2007 Python Software Foundation
            // Author: Barry Warsaw, Thomas Wouters, Anthony Baxter
            // Contact: email-sig@python.org
            define("email.parser", "__all__", py_list({ S<"Parser">(), S<"HeaderParser">(), S<"BytesParser">(), S<"BytesHeaderParser">(), S<"FeedParser">(), S<"BytesFeedParser">() }));
            define("email.parser", "Parser", w.make_class("Parser", {  }, {
                { "__init__", w.method("Parser", "__init__", { py_positional("_class", none()), py_keyword("policy", Mod<"email._policybase">().attr("compat32")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _class = a[1];
                        py policy = a[2];
                        self.set_attr("_class", _class);
                        self.set_attr("policy", policy);
                        return none();
                    }) },
                { "parse", w.method("Parser", "parse", { py_positional("fp"), py_positional("headersonly", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        py headersonly = a[2];
                        py data;
                        py feedparser;
                        feedparser = (Mod<"email.feedparser">().attr("FeedParser")).call({ self.attr("_class") }, { { "policy", self.attr("policy") } });
                        if (headersonly.truthy())
                        {
                            feedparser.method("_set_headersonly");
                        }
                        while (([&]() -> py { data = fp.method("read", { I<8192>() }); return data; }()).truthy())
                        {
                            feedparser.method("feed", { data });
                        }
                        return feedparser.method("close");
                    }) },
                { "parsestr", w.method("Parser", "parsestr", { py_positional("text"), py_positional("headersonly", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py text = a[1];
                        py headersonly = a[2];
                        return self.method("parse", { (Mod<"io">().attr("StringIO"))(text) }, { { "headersonly", headersonly } });
                    }) }, }));

            define("email.parser", "HeaderParser", w.make_class("HeaderParser", { G<"email.parser.Parser">() }, {
                { "parse", w.method("HeaderParser", "parse", { py_positional("fp"), py_positional("headersonly", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        py headersonly = a[2];
                        return (G<"email.parser.Parser">()).method("parse", { self, fp, boolean(true) });
                    }) },
                { "parsestr", w.method("HeaderParser", "parsestr", { py_positional("text"), py_positional("headersonly", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py text = a[1];
                        py headersonly = a[2];
                        return (G<"email.parser.Parser">()).method("parsestr", { self, text, boolean(true) });
                    }) }, }));

            define("email.parser", "BytesParser", w.make_class("BytesParser", {  }, {
                { "__init__", w.method("BytesParser", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kw = a[2];
                        self.set_attr("parser", call_ex((G<"email.parser.Parser">()), {  }, args, kw));
                        return none();
                    }, true, true) },
                { "parse", w.method("BytesParser", "parse", { py_positional("fp"), py_positional("headersonly", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        py headersonly = a[2];
                        fp = (Mod<"io">().attr("TextIOWrapper")).call({ fp }, { { "encoding", S<"ascii">() }, { "errors", S<"surrogateescape">() } });
                        {
                            std::optional<py> pending2;
                            [[maybe_unused]] int flow2 = 0;
                            [[maybe_unused]] py result2;
                            try
                            {
                                result2 = self.attr("parser").method("parse", { fp, headersonly });
                                flow2 = 1;
                                goto finally2;
                            }
                            catch (raised const&)
                            {
                                pending2 = py_take_exception();
                            }
                            finally2:
                            fp.method("detach");
                            if (pending2)
                                py_reraise(*pending2);
                            if (flow2 == 1)
                                return result2;
                        }
                        return none();
                    }) },
                { "parsebytes", w.method("BytesParser", "parsebytes", { py_positional("text"), py_positional("headersonly", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py text = a[1];
                        py headersonly = a[2];
                        text = text.method("decode", { S<"ASCII">() }, { { "errors", S<"surrogateescape">() } });
                        return self.attr("parser").method("parsestr", { text, headersonly });
                    }) }, }));

            define("email.parser", "BytesHeaderParser", w.make_class("BytesHeaderParser", { G<"email.parser.BytesParser">() }, {
                { "parse", w.method("BytesHeaderParser", "parse", { py_positional("fp"), py_positional("headersonly", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        py headersonly = a[2];
                        return (G<"email.parser.BytesParser">()).method("parse", { self, fp }, { { "headersonly", boolean(true) } });
                    }) },
                { "parsebytes", w.method("BytesHeaderParser", "parsebytes", { py_positional("text"), py_positional("headersonly", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py text = a[1];
                        py headersonly = a[2];
                        return (G<"email.parser.BytesParser">()).method("parsebytes", { self, text }, { { "headersonly", boolean(true) } });
                    }) }, }));
        }

        bool email_parser_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_email_parser();
                }
                if (auto const doc = email_parser_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("StringIO", import_attribute("io", "StringIO").release());
                aAttributes.emplace_back("TextIOWrapper", import_attribute("io", "TextIOWrapper").release());
                aAttributes.emplace_back("FeedParser", import_attribute("email.feedparser", "FeedParser").release());
                aAttributes.emplace_back("BytesFeedParser", import_attribute("email.feedparser", "BytesFeedParser").release());
                aAttributes.emplace_back("compat32", import_attribute("email._policybase", "compat32").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("email.parser", email_parser_module);
    }
}
