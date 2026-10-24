/*
  plistlib.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/plistlib.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module plistlib (CPython 3.13's Lib/plistlib.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "plistlib_docs.hpp"

namespace neos::python_runtime::detail::plistlib_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "plistlib.name"))
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
                py_raise("SystemError", std::string{ "plistlib: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"plistlib.name">()))
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
            if (modules_imported("plistlib"))
                return py{ fn_import("plistlib") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "plistlib" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "plistlib", plistlib_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- plistlib (Lib/plistlib.py)

        void make_plistlib()
        {
            static module_writer const& w = writer("plistlib");
            define("plistlib", "__all__", py_list({ S<"InvalidFileException">(), S<"FMT_XML">(), S<"FMT_BINARY">(), S<"load">(), S<"dump">(), S<"loads">(), S<"dumps">(), S<"UID">() }));
            define("plistlib", "PlistFormat", (Mod<"enum">()).method("Enum", { S<"PlistFormat">(), S<"FMT_XML FMT_BINARY">() }, { { "module", S<"plistlib">() } }));
            define("plistlib", "FMT_XML", (G<"plistlib.PlistFormat">()).attr("FMT_XML"));
            define("plistlib", "FMT_BINARY", (G<"plistlib.PlistFormat">()).attr("FMT_BINARY"));
            // Data larger than this will be read in chunks, to prevent extreme
            // overallocation.
            define("plistlib", "_MIN_READ_BUF_SIZE", (I<1>()) << (I<20>()));
            define("plistlib", "UID", w.make_class("UID", {  }, {
                { "__init__", w.method("UID", "__init__", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (!isinstance(data, B<"int">()))
                        {
                            py_raise((B<"TypeError">())(S<"data must be an int">()));
                        }
                        if (data >= ((I<1>()) << (I<64>())))
                        {
                            py_raise((B<"ValueError">())(S<"UIDs cannot be >= 2**64">()));
                        }
                        if (data < (I<0>()))
                        {
                            py_raise((B<"ValueError">())(S<"UIDs must be positive">()));
                        }
                        self.set_attr("data", data);
                        return none();
                    }) },
                { "__index__", w.method("UID", "__index__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("data");
                    }) },
                { "__repr__", w.method("UID", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s(%s)">()) % py_tuple({ self.attr("__class__").attr("__name__"), S(self.attr("data").repr()) });
                    }) },
                { "__reduce__", w.method("UID", "__reduce__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py_tuple({ self.attr("__class__"), py_tuple({ self.attr("data") }) });
                    }) },
                { "__eq__", w.method("UID", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (!isinstance(other, G<"plistlib.UID">()))
                        {
                            return py::borrowed(not_implemented());
                        }
                        return boolean(self.attr("data") == other.attr("data"));
                    }) },
                { "__hash__", w.method("UID", "__hash__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"hash">())(self.attr("data"));
                    }) }, }));

            // 
            // XML support
            // 
            // XML 'header'
            define("plistlib", "PLISTHEADER", py{ make_bytes(std::string{ "\x3c\x3f\x78\x6d\x6c\x20\x76\x65\x72\x73\x69\x6f\x6e\x3d\x22\x31\x2e\x30\x22\x20\x65\x6e\x63\x6f\x64\x69\x6e\x67\x3d\x22\x55\x54\x46\x2d\x38\x22\x3f\x3e\x0a\x3c\x21\x44\x4f\x43\x54\x59\x50\x45\x20\x70\x6c\x69\x73\x74\x20\x50\x55\x42\x4c\x49\x43\x20\x22\x2d\x2f\x2f\x41\x70\x70\x6c\x65\x2f\x2f\x44\x54\x44\x20\x50\x4c\x49\x53\x54\x20\x31\x2e\x30\x2f\x2f\x45\x4e\x22\x20\x22\x68\x74\x74\x70\x3a\x2f\x2f\x77\x77\x77\x2e\x61\x70\x70\x6c\x65\x2e\x63\x6f\x6d\x2f\x44\x54\x44\x73\x2f\x50\x72\x6f\x70\x65\x72\x74\x79\x4c\x69\x73\x74\x2d\x31\x2e\x30\x2e\x64\x74\x64\x22\x3e\x0a", 142 }) });
            // Regex to find any control chars, except for \t \n and \r
            define("plistlib", "_controlCharPat", (Mod<"re">()).method("compile", { S<"[\\x00\\x01\\x02\\x03\\x04\\x05\\x06\\x07\\x08\\x0b\\x0c\\x0e\\x0f\\x10\\x11\\x12\\x13\\x14\\x15\\x16\\x17\\x18\\x19\\x1a\\x1b\\x1c\\x1d\\x1e\\x1f]">() }));
            define("plistlib", "_encode_base64", w.function("_encode_base64", "_encode_base64", { py_positional("s"), py_positional("maxlinelength", I<76>()) }, [](arguments a) -> py
                {
                    py s = a[0];
                    py maxlinelength = a[1];
                    py chunk;
                    py i;
                    py maxbinsize;
                    py pieces;
                    maxbinsize = floordiv(maxlinelength, (I<4>())) * (I<3>());
                    pieces = py_list();
                    for (py const& item : iterate{ (B<"range">())(I<0>(), py{ s.size() }, maxbinsize) })
                    {
                        i = item;
                        chunk = s[B<"slice">()(i, i + maxbinsize, none())];
                        pieces.method("append", { (Mod<"binascii">().attr("b2a_base64"))(chunk) });
                    }
                    return py{ make_bytes(std::string{ "", 0 }) }.method("join", { pieces });
                }));

            define("plistlib", "_decode_base64", w.function("_decode_base64", "_decode_base64", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    if (isinstance(s, B<"str">()))
                    {
                        return (Mod<"binascii">().attr("a2b_base64"))(s.method("encode", { S<"utf-8">() }));
                    }
                    else
                    {
                        return (Mod<"binascii">().attr("a2b_base64"))(s);
                    }
                    return none();
                }));

            // Contents should conform to a subset of ISO 8601
            // (in particular, YYYY '-' MM '-' DD 'T' HH ':' MM ':' SS 'Z'.  Smaller units
            // may be omitted with #  a loss of precision)
            define("plistlib", "_dateParser", (Mod<"re">()).method("compile", { S<"(?P<year>\\d\\d\\d\\d)(?:-(?P<month>\\d\\d)(?:-(?P<day>\\d\\d)(?:T(?P<hour>\\d\\d)(?::(?P<minute>\\d\\d)(?::(?P<second>\\d\\d))?)?)?)?)?Z">(), Mod<"re">().attr("ASCII") }));
            define("plistlib", "_date_from_string", w.function("_date_from_string", "_date_from_string", { py_positional("s"), py_positional("aware_datetime") }, [](arguments a) -> py
                {
                    py s = a[0];
                    py aware_datetime = a[1];
                    py gd;
                    py key;
                    py lst;
                    py order;
                    py val;
                    order = py_tuple({ S<"year">(), S<"month">(), S<"day">(), S<"hour">(), S<"minute">(), S<"second">() });
                    gd = (G<"plistlib._dateParser">()).method("match", { s }).method("groupdict");
                    lst = py_list();
                    for (py const& item : iterate{ order })
                    {
                        key = item;
                        val = gd[key];
                        if (val.is_none())
                        {
                            break;
                        }
                        lst.method("append", { (B<"int">())(val) });
                    }
                    if (aware_datetime.truthy())
                    {
                        return call_ex((Mod<"datetime">().attr("datetime")), {  }, lst, py{}, { { "tzinfo", Mod<"datetime">().attr("UTC") } });
                    }
                    return call_ex((Mod<"datetime">().attr("datetime")), {  }, lst, py{});
                }));

            define("plistlib", "_date_to_string", w.function("_date_to_string", "_date_to_string", { py_positional("d"), py_positional("aware_datetime") }, [](arguments a) -> py
                {
                    py d = a[0];
                    py aware_datetime = a[1];
                    if (aware_datetime.truthy())
                    {
                        d = d.method("astimezone", { Mod<"datetime">().attr("UTC") });
                    }
                    return (S<"%04d-%02d-%02dT%02d:%02d:%02dZ">()) % py_tuple({ d.attr("year"), d.attr("month"), d.attr("day"), d.attr("hour"), d.attr("minute"), d.attr("second") });
                }));

            define("plistlib", "_dict_items", w.function("_dict_items", "_dict_items", { py_positional("d"), py_positional("sort_keys"), py_positional("skipkeys") }, [](arguments a) -> py
                {
                    py d = a[0];
                    py sort_keys = a[1];
                    py skipkeys = a[2];
                    py items;
                    py key;
                    items = d.method("items");
                    if (sort_keys.truthy())
                    {
                        if (skipkeys.truthy())
                        {
                            items = [&]() -> py { py const comprehension3 = py_list(); py item_; for (py const& item : iterate{ items }) { item_ = item; if (!isinstance(item_[I<0>()], B<"str">())) continue; comprehension3.method("append", { item_ }); } return comprehension3; }();
                            items.method("sort");
                        }
                        else
                        {
                            for (py const& item : iterate{ d })
                            {
                                key = item;
                                if (!isinstance(key, B<"str">()))
                                {
                                    py_raise((B<"TypeError">())(S<"keys must be strings">()));
                                }
                            }
                            items = (B<"sorted">())(items);
                        }
                    }
                    return items;
                }));

            define("plistlib", "_escape", w.function("_escape", "_escape", { py_positional("text") }, [](arguments a) -> py
                {
                    py text = a[0];
                    py m;
                    m = (G<"plistlib._controlCharPat">()).method("search", { text });
                    if (!m.is_none())
                    {
                        py_raise((B<"ValueError">())(S<"strings can't contain control characters; use bytes instead">()));
                    }
                    text = text.method("replace", { S<"\r\n">(), S<"\n">() });
                    text = text.method("replace", { S<"\r">(), S<"\n">() });
                    text = text.method("replace", { S<"&">(), S<"&amp;">() });
                    text = text.method("replace", { S<"<">(), S<"&lt;">() });
                    text = text.method("replace", { S<">">(), S<"&gt;">() });
                    return text;
                }));

                // element handlers
            define("plistlib", "_PlistParser", w.make_class("_PlistParser", {  }, {
                { "__init__", w.method("_PlistParser", "__init__", { py_positional("dict_type"), py_positional("aware_datetime", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dict_type = a[1];
                        py aware_datetime = a[2];
                        self.set_attr("stack", py_list());
                        self.set_attr("current_key", none());
                        self.set_attr("root", none());
                        self.set_attr("_dict_type", dict_type);
                        self.set_attr("_aware_datetime", aware_datetime);
                        return none();
                    }) },
                { "parse", w.method("_PlistParser", "parse", { py_positional("fileobj") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fileobj = a[1];
                        self.set_attr("parser", (Mod<"xml.parsers.expat">().attr("ParserCreate"))());
                        self.attr("parser").set_attr("StartElementHandler", self.attr("handle_begin_element"));
                        self.attr("parser").set_attr("EndElementHandler", self.attr("handle_end_element"));
                        self.attr("parser").set_attr("CharacterDataHandler", self.attr("handle_data"));
                        self.attr("parser").set_attr("EntityDeclHandler", self.attr("handle_entity_decl"));
                        self.attr("parser").method("ParseFile", { fileobj });
                        return self.attr("root");
                    }) },
                { "handle_entity_decl", w.method("_PlistParser", "handle_entity_decl", { py_positional("entity_name"), py_positional("is_parameter_entity"), py_positional("value"), py_positional("base"), py_positional("system_id"), py_positional("public_id"), py_positional("notation_name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py entity_name = a[1];
                        py is_parameter_entity = a[2];
                        py value = a[3];
                        py base = a[4];
                        py system_id = a[5];
                        py public_id = a[6];
                        py notation_name = a[7];
                        py_raise((G<"plistlib.InvalidFileException">())(S<"XML entity declarations are not supported in plist files">()));
                    }) },
                { "handle_begin_element", w.method("_PlistParser", "handle_begin_element", { py_positional("element"), py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py element = a[1];
                        py attrs = a[2];
                        py handler;
                        self.set_attr("data", py_list());
                        handler = (B<"getattr">())(self, (S<"begin_">()) + element, none());
                        if (!handler.is_none())
                        {
                            handler(attrs);
                        }
                        return none();
                    }) },
                { "handle_end_element", w.method("_PlistParser", "handle_end_element", { py_positional("element") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py element = a[1];
                        py handler;
                        handler = (B<"getattr">())(self, (S<"end_">()) + element, none());
                        if (!handler.is_none())
                        {
                            handler();
                        }
                        return none();
                    }) },
                { "handle_data", w.method("_PlistParser", "handle_data", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.attr("data").method("append", { data });
                        return none();
                    }) },
                { "add_object", w.method("_PlistParser", "add_object", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        if (!self.attr("current_key").is_none())
                        {
                            if (!isinstance(self.attr("stack")[I<-1>()], B<"dict">()))
                            {
                                py_raise((B<"ValueError">())((S<"unexpected element at line %d">()) % self.attr("parser").attr("CurrentLineNumber")));
                            }
                            self.attr("stack")[I<-1>()].set_item(self.attr("current_key"), value);
                            self.set_attr("current_key", none());
                        }
                        else if (!self.attr("stack").truthy())
                        {
                            // this is the root object
                            self.set_attr("root", value);
                        }
                        else
                        {
                            if (!isinstance(self.attr("stack")[I<-1>()], B<"list">()))
                            {
                                py_raise((B<"ValueError">())((S<"unexpected element at line %d">()) % self.attr("parser").attr("CurrentLineNumber")));
                            }
                            self.attr("stack")[I<-1>()].method("append", { value });
                        }
                        return none();
                    }) },
                { "get_data", w.method("_PlistParser", "get_data", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data;
                        data = (S<"">()).method("join", { self.attr("data") });
                        self.set_attr("data", py_list());
                        return data;
                    }) },
                { "begin_dict", w.method("_PlistParser", "begin_dict", { py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attrs = a[1];
                        py d;
                        d = self.method("_dict_type");
                        self.method("add_object", { d });
                        self.attr("stack").method("append", { d });
                        return none();
                    }) },
                { "end_dict", w.method("_PlistParser", "end_dict", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("current_key").truthy())
                        {
                            py_raise((B<"ValueError">())((S<"missing value for key '%s' at line %d">()) % py_tuple({ self.attr("current_key"), self.attr("parser").attr("CurrentLineNumber") })));
                        }
                        self.attr("stack").method("pop");
                        return none();
                    }) },
                { "end_key", w.method("_PlistParser", "end_key", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if ((self.attr("current_key").truthy() || (!isinstance(self.attr("stack")[I<-1>()], B<"dict">()))))
                        {
                            py_raise((B<"ValueError">())((S<"unexpected key at line %d">()) % self.attr("parser").attr("CurrentLineNumber")));
                        }
                        self.set_attr("current_key", self.method("get_data"));
                        return none();
                    }) },
                { "begin_array", w.method("_PlistParser", "begin_array", { py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attrs = a[1];
                        py a_;
                        a_ = py_list();
                        self.method("add_object", { a_ });
                        self.attr("stack").method("append", { a_ });
                        return none();
                    }) },
                { "end_array", w.method("_PlistParser", "end_array", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("stack").method("pop");
                        return none();
                    }) },
                { "end_true", w.method("_PlistParser", "end_true", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("add_object", { boolean(true) });
                        return none();
                    }) },
                { "end_false", w.method("_PlistParser", "end_false", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("add_object", { boolean(false) });
                        return none();
                    }) },
                { "end_integer", w.method("_PlistParser", "end_integer", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py raw;
                        raw = self.method("get_data");
                        if ((raw.method("startswith", { S<"0x">() }).truthy() || raw.method("startswith", { S<"0X">() }).truthy()))
                        {
                            self.method("add_object", { (B<"int">())(raw, I<16>()) });
                        }
                        else
                        {
                            self.method("add_object", { (B<"int">())(raw) });
                        }
                        return none();
                    }) },
                { "end_real", w.method("_PlistParser", "end_real", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("add_object", { (B<"float">())(self.method("get_data")) });
                        return none();
                    }) },
                { "end_string", w.method("_PlistParser", "end_string", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("add_object", { self.method("get_data") });
                        return none();
                    }) },
                { "end_data", w.method("_PlistParser", "end_data", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("add_object", { (G<"plistlib._decode_base64">())(self.method("get_data")) });
                        return none();
                    }) },
                { "end_date", w.method("_PlistParser", "end_date", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("add_object", { (G<"plistlib._date_from_string">()).call({ self.method("get_data") }, { { "aware_datetime", self.attr("_aware_datetime") } }) });
                        return none();
                    }) }, }));

            define("plistlib", "_DumbXMLWriter", w.make_class("_DumbXMLWriter", {  }, {
                { "__init__", w.method("_DumbXMLWriter", "__init__", { py_positional("file"), py_positional("indent_level", I<0>()), py_positional("indent", S<"\t">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        py indent_level = a[2];
                        py indent = a[3];
                        self.set_attr("file", file);
                        self.set_attr("stack", py_list());
                        self.set_attr("_indent_level", indent_level);
                        self.set_attr("indent", indent);
                        return none();
                    }) },
                { "begin_element", w.method("_DumbXMLWriter", "begin_element", { py_positional("element") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py element = a[1];
                        self.attr("stack").method("append", { element });
                        self.method("writeln", { (S<"<%s>">()) % element });
                        {
                            py const target5 = self;
                            target5.set_attr("_indent_level", inplace(NEOS_PY_ADD, target5.attr("_indent_level"), I<1>()));
                        }
                        return none();
                    }) },
                { "end_element", w.method("_DumbXMLWriter", "end_element", { py_positional("element") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py element = a[1];
                        if (!(self.attr("_indent_level") > (I<0>())))
                            py_raise(B<"AssertionError">()());
                        if (!(self.attr("stack").method("pop") == element))
                            py_raise(B<"AssertionError">()());
                        {
                            py const target6 = self;
                            target6.set_attr("_indent_level", inplace(NEOS_PY_SUBTRACT, target6.attr("_indent_level"), I<1>()));
                        }
                        self.method("writeln", { (S<"</%s>">()) % element });
                        return none();
                    }) },
                { "simple_element", w.method("_DumbXMLWriter", "simple_element", { py_positional("element"), py_positional("value", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py element = a[1];
                        py value = a[2];
                        if (!value.is_none())
                        {
                            value = (G<"plistlib._escape">())(value);
                            self.method("writeln", { (S<"<%s>%s</%s>">()) % py_tuple({ element, value, element }) });
                        }
                        else
                        {
                            self.method("writeln", { (S<"<%s/>">()) % element });
                        }
                        return none();
                    }) },
                { "writeln", w.method("_DumbXMLWriter", "writeln", { py_positional("line") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line = a[1];
                        if (line.truthy())
                        {
                            // plist has fixed encoding of utf-8
                            // XXX: is this test needed?
                            if (isinstance(line, B<"str">()))
                            {
                                line = line.method("encode", { S<"utf-8">() });
                            }
                            self.attr("file").method("write", { self.attr("_indent_level") * self.attr("indent") });
                            self.attr("file").method("write", { line });
                        }
                        self.attr("file").method("write", { py{ make_bytes(std::string{ "\x0a", 1 }) } });
                        return none();
                    }) }, }));

            define("plistlib", "_PlistWriter", w.make_class("_PlistWriter", { G<"plistlib._DumbXMLWriter">() }, {
                { "__init__", w.method("_PlistWriter", "__init__", { py_positional("file"), py_positional("indent_level", I<0>()), py_positional("indent", py{ make_bytes(std::string{ "\x09", 1 }) }), py_positional("writeHeader", I<1>()), py_positional("sort_keys", boolean(true)), py_positional("skipkeys", boolean(false)), py_positional("aware_datetime", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        py indent_level = a[2];
                        py indent = a[3];
                        py writeHeader = a[4];
                        py sort_keys = a[5];
                        py skipkeys = a[6];
                        py aware_datetime = a[7];
                        if (writeHeader.truthy())
                        {
                            file.method("write", { G<"plistlib.PLISTHEADER">() });
                        }
                        (G<"plistlib._DumbXMLWriter">()).method("__init__", { self, file, indent_level, indent });
                        self.set_attr("_sort_keys", sort_keys);
                        self.set_attr("_skipkeys", skipkeys);
                        self.set_attr("_aware_datetime", aware_datetime);
                        return none();
                    }) },
                { "write", w.method("_PlistWriter", "write", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        self.method("writeln", { S<"<plist version=\"1.0\">">() });
                        self.method("write_value", { value });
                        self.method("writeln", { S<"</plist>">() });
                        return none();
                    }) },
                { "write_value", w.method("_PlistWriter", "write_value", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        if (isinstance(value, B<"str">()))
                        {
                            self.method("simple_element", { S<"string">(), value });
                        }
                        else if (value.is(boolean(true)))
                        {
                            self.method("simple_element", { S<"true">() });
                        }
                        else if (value.is(boolean(false)))
                        {
                            self.method("simple_element", { S<"false">() });
                        }
                        else if (isinstance(value, B<"int">()))
                        {
                            if ((((I<-1>()) << (I<63>())) <= value) && (value < ((I<1>()) << (I<64>()))))
                            {
                                self.method("simple_element", { S<"integer">(), (S<"%d">()) % value });
                            }
                            else
                            {
                                py_raise((B<"OverflowError">())(value));
                            }
                        }
                        else if (isinstance(value, B<"float">()))
                        {
                            self.method("simple_element", { S<"real">(), S(value.repr()) });
                        }
                        else if (isinstance(value, B<"dict">()))
                        {
                            self.method("write_dict", { value });
                        }
                        else if (isinstance(value, py_tuple({ B<"bytes">(), B<"bytearray">() })))
                        {
                            self.method("write_bytes", { value });
                        }
                        else if (isinstance(value, Mod<"datetime">().attr("datetime")))
                        {
                            self.method("simple_element", { S<"date">(), (G<"plistlib._date_to_string">())(value, self.attr("_aware_datetime")) });
                        }
                        else if (isinstance(value, py_tuple({ B<"tuple">(), B<"list">() })))
                        {
                            self.method("write_array", { value });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())((S<"unsupported type: %s">()) % (B<"type">())(value)));
                        }
                        return none();
                    }) },
                { "write_bytes", w.method("_PlistWriter", "write_bytes", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py line;
                        py maxlinelength;
                        self.method("begin_element", { S<"data">() });
                        {
                            py const target7 = self;
                            target7.set_attr("_indent_level", inplace(NEOS_PY_SUBTRACT, target7.attr("_indent_level"), I<1>()));
                        }
                        maxlinelength = (B<"max">())(I<16>(), (I<76>()) - py{ (self.attr("indent") * self.attr("_indent_level")).method("expandtabs").size() });
                        for (py const& item : iterate{ (G<"plistlib._encode_base64">())(data, maxlinelength).method("split", { py{ make_bytes(std::string{ "\x0a", 1 }) } }) })
                        {
                            line = item;
                            if (line.truthy())
                            {
                                self.method("writeln", { line });
                            }
                        }
                        {
                            py const target9 = self;
                            target9.set_attr("_indent_level", inplace(NEOS_PY_ADD, target9.attr("_indent_level"), I<1>()));
                        }
                        self.method("end_element", { S<"data">() });
                        return none();
                    }) },
                { "write_dict", w.method("_PlistWriter", "write_dict", { py_positional("d") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py d = a[1];
                        py items;
                        py key;
                        py value;
                        if (d.truthy())
                        {
                            self.method("begin_element", { S<"dict">() });
                            items = (G<"plistlib._dict_items">())(d, self.attr("_sort_keys"), self.attr("_skipkeys"));
                            for (py const& item : iterate{ items })
                            {
                                auto const items11 = unpack(item, 2);
                                key = items11[0];
                                value = items11[1];
                                if (!isinstance(key, B<"str">()))
                                {
                                    if (self.attr("_skipkeys").truthy())
                                    {
                                        continue;
                                    }
                                    py_raise((B<"TypeError">())(S<"keys must be strings">()));
                                }
                                self.method("simple_element", { S<"key">(), key });
                                self.method("write_value", { value });
                            }
                            self.method("end_element", { S<"dict">() });
                        }
                        else
                        {
                            self.method("simple_element", { S<"dict">() });
                        }
                        return none();
                    }) },
                { "write_array", w.method("_PlistWriter", "write_array", { py_positional("array") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py array = a[1];
                        py value;
                        if (array.truthy())
                        {
                            self.method("begin_element", { S<"array">() });
                            for (py const& item : iterate{ array })
                            {
                                value = item;
                                self.method("write_value", { value });
                            }
                            self.method("end_element", { S<"array">() });
                        }
                        else
                        {
                            self.method("simple_element", { S<"array">() });
                        }
                        return none();
                    }) }, }));

            define("plistlib", "_is_fmt_xml", w.function("_is_fmt_xml", "_is_fmt_xml", { py_positional("header") }, [](arguments a) -> py
                {
                    py header = a[0];
                    py bom;
                    py encoding;
                    py pfx;
                    py prefix;
                    py prefixes;
                    py start;
                    prefixes = py_tuple({ py{ make_bytes(std::string{ "\x3c\x3f\x78\x6d\x6c", 5 }) }, py{ make_bytes(std::string{ "\x3c\x70\x6c\x69\x73\x74", 6 }) } });
                    for (py const& item : iterate{ prefixes })
                    {
                        pfx = item;
                        if (header.method("startswith", { pfx }).truthy())
                        {
                            return boolean(true);
                        }
                    }
                    // Also check for alternative XML encodings, this is slightly
                    // overkill because the Apple tools (and plistlib) will not
                    // generate files with these encodings.
                    for (py const& item : iterate{ py_tuple({ py_tuple({ Mod<"codecs">().attr("BOM_UTF8"), S<"utf-8">() }), py_tuple({ Mod<"codecs">().attr("BOM_UTF16_BE"), S<"utf-16-be">() }), py_tuple({ Mod<"codecs">().attr("BOM_UTF16_LE"), S<"utf-16-le">() }) }) })
                    {
                        auto const items15 = unpack(item, 2);
                        bom = items15[0];
                        encoding = items15[1];
                        // expat does not support utf-32
                        // (codecs.BOM_UTF32_BE, "utf-32-be"),
                        // (codecs.BOM_UTF32_LE, "utf-32-le"),
                        if (!header.method("startswith", { bom }).truthy())
                        {
                            continue;
                        }
                        for (py const& item : iterate{ prefixes })
                        {
                            start = item;
                            prefix = bom + start.method("decode", { S<"ascii">() }).method("encode", { encoding });
                            if (header[B<"slice">()(none(), py{ prefix.size() }, none())] == prefix)
                            {
                                return boolean(true);
                            }
                        }
                    }
                    return boolean(false);
                }));

            // 
            // Binary Plist
            // 
            define("plistlib", "InvalidFileException", w.make_class("InvalidFileException", { B<"ValueError">() }, {
                { "__init__", w.method("InvalidFileException", "__init__", { py_positional("message", S<"Invalid file">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        (B<"ValueError">()).method("__init__", { self, message });
                        return none();
                    }) }, }));

            define("plistlib", "_BINARY_FORMAT", dict_of({ { I<1>(), S<"B">() }, { I<2>(), S<"H">() }, { I<4>(), S<"L">() }, { I<8>(), S<"Q">() } }));
            define("plistlib", "_undefined", (B<"object">())());
            define("plistlib", "_BinaryPlistParser", w.make_class("_BinaryPlistParser", {  }, {
                { "__init__", w.method("_BinaryPlistParser", "__init__", { py_positional("dict_type"), py_positional("aware_datetime", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dict_type = a[1];
                        py aware_datetime = a[2];
                        self.set_attr("_dict_type", dict_type);
                        self.set_attr("_aware_datime", aware_datetime);
                        return none();
                    }) },
                { "parse", w.method("_BinaryPlistParser", "parse", { py_positional("fp") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        py num_objects;
                        py offset_size;
                        py offset_table_offset;
                        py top_object;
                        py trailer;
                        try
                        {
                            // The basic file format:
                            // HEADER
                            // object...
                            // refid->offset...
                            // TRAILER
                            self.set_attr("_fp", fp);
                            self.attr("_fp").method("seek", { I<-32>(), Mod<"os">().attr("SEEK_END") });
                            trailer = self.attr("_fp").method("read", { I<32>() });
                            if (py{ trailer.size() } != (I<32>()))
                            {
                                py_raise((G<"plistlib.InvalidFileException">())());
                            }
                            auto const items18 = unpack((Mod<"struct">().attr("unpack"))(S<">6xBBQQQ">(), trailer), 5);
                            offset_size = items18[0];
                            self.set_attr("_ref_size", items18[1]);
                            num_objects = items18[2];
                            top_object = items18[3];
                            offset_table_offset = items18[4];
                            self.attr("_fp").method("seek", { offset_table_offset });
                            self.set_attr("_object_offsets", self.method("_read_ints", { num_objects, offset_size }));
                            self.set_attr("_objects", py_list({ G<"plistlib._undefined">() }) * num_objects);
                            return self.method("_read_object", { top_object });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler17;
                            if (matches(handler17.exception, py_tuple({ B<"OSError">(), B<"IndexError">(), Mod<"struct">().attr("error"), B<"OverflowError">(), B<"ValueError">() })))
                            {
                                py_raise((G<"plistlib.InvalidFileException">())());
                            }
                            else
                                py_reraise(handler17.exception);
                        }
                        return none();
                    }) },
                { "_get_size", w.method("_BinaryPlistParser", "_get_size", { py_positional("tokenL") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tokenL = a[1];
                        py f;
                        py m;
                        py s;
                        if (tokenL == (I<15>()))
                        {
                            m = self.attr("_fp").method("read", { I<1>() })[I<0>()] & (I<3>());
                            s = (I<1>()) << m;
                            f = (S<">">()) + (G<"plistlib._BINARY_FORMAT">())[s];
                            return (Mod<"struct">().attr("unpack"))(f, self.attr("_fp").method("read", { s }))[I<0>()];
                        }
                        return tokenL;
                    }) },
                { "_read", w.method("_BinaryPlistParser", "_read", { py_positional("size") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        py cursize;
                        py data;
                        py delta;
                        cursize = (B<"min">())(size, G<"plistlib._MIN_READ_BUF_SIZE">());
                        data = self.attr("_fp").method("read", { cursize });
                        while (boolean(true).truthy())
                        {
                            if (py{ data.size() } != cursize)
                            {
                                py_raise(G<"plistlib.InvalidFileException">());
                            }
                            if (cursize == size)
                            {
                                return data;
                            }
                            delta = (B<"min">())(cursize, size - cursize);
                            data = inplace(NEOS_PY_ADD, data, self.attr("_fp").method("read", { delta }));
                            cursize = inplace(NEOS_PY_ADD, cursize, delta);
                        }
                        return none();
                    }) },
                { "_read_ints", w.method("_BinaryPlistParser", "_read_ints", { py_positional("n"), py_positional("size") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n = a[1];
                        py size = a[2];
                        py data;
                        data = self.method("_read", { size * n });
                        if ((G<"plistlib._BINARY_FORMAT">()).contains(size))
                        {
                            return (Mod<"struct">().attr("unpack"))(S(std::string{ ">" } + n.str() + (G<"plistlib._BINARY_FORMAT">())[size].str()), data);
                        }
                        else
                        {
                            if (!size.truthy())
                            {
                                py_raise((G<"plistlib.InvalidFileException">())());
                            }
                            return (B<"tuple">())([&]() -> py { py const comprehension20 = py_list(); py i; for (py const& item : iterate{ (B<"range">())(I<0>(), size * n, size) }) { i = item; comprehension20.method("append", { (B<"int">()).method("from_bytes", { data[B<"slice">()(i, i + size, none())], S<"big">() }) }); } return comprehension20; }());
                        }
                        return none();
                    }) },
                { "_read_refs", w.method("_BinaryPlistParser", "_read_refs", { py_positional("n") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n = a[1];
                        return self.method("_read_ints", { n, self.attr("_ref_size") });
                    }) },
                { "_read_object", w.method("_BinaryPlistParser", "_read_object", { py_positional("ref") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ref_ = a[1];
                        py data;
                        py epoch;
                        py f;
                        py k;
                        py key_refs;
                        py o;
                        py obj_refs;
                        py offset;
                        py result;
                        py s;
                        py token;
                        py tokenH;
                        py tokenL;
                        py x;
                        result = self.attr("_objects")[ref_];
                        if (!result.is((G<"plistlib._undefined">())))
                        {
                            return result;
                        }
                        offset = self.attr("_object_offsets")[ref_];
                        self.attr("_fp").method("seek", { offset });
                        token = self.attr("_fp").method("read", { I<1>() })[I<0>()];
                        auto const items21 = unpack(py_tuple({ token & (I<240>()), token & (I<15>()) }), 2);
                        tokenH = items21[0];
                        tokenL = items21[1];
                        if (token == (I<0>()))
                        {
                            result = none();
                        }
                        else if (token == (I<8>()))
                        {
                            result = boolean(false);
                        }
                        else if (token == (I<9>()))
                        {
                            result = boolean(true);
                        }
                        // The referenced source code also mentions URL (0x0c, 0x0d) and
                        // UUID (0x0e), but neither can be generated using the Cocoa libraries.
                        else if (token == (I<15>()))
                        {
                            result = py{ make_bytes(std::string{ "", 0 }) };
                        }
                        else if (tokenH == (I<16>()))
                        {
                            result = (B<"int">()).method("from_bytes", { self.attr("_fp").method("read", { (I<1>()) << tokenL }), S<"big">() }, { { "signed", boolean(tokenL >= (I<3>())) } });
                        }
                        else if (token == (I<34>()))
                        {
                            result = (Mod<"struct">().attr("unpack"))(S<">f">(), self.attr("_fp").method("read", { I<4>() }))[I<0>()];
                        }
                        else if (token == (I<35>()))
                        {
                            result = (Mod<"struct">().attr("unpack"))(S<">d">(), self.attr("_fp").method("read", { I<8>() }))[I<0>()];
                        }
                        else if (token == (I<51>()))
                        {
                            f = (Mod<"struct">().attr("unpack"))(S<">d">(), self.attr("_fp").method("read", { I<8>() }))[I<0>()];
                            // timestamp 0 of binary plists corresponds to 1/1/2001
                            // (year of Mac OS X 10.0), instead of 1/1/1970.
                            if (self.attr("_aware_datime").truthy())
                            {
                                epoch = (Mod<"datetime">().attr("datetime")).call({ I<2001>(), I<1>(), I<1>() }, { { "tzinfo", Mod<"datetime">().attr("UTC") } });
                            }
                            else
                            {
                                epoch = (Mod<"datetime">().attr("datetime"))(I<2001>(), I<1>(), I<1>());
                            }
                            result = epoch + (Mod<"datetime">().attr("timedelta")).call({  }, { { "seconds", f } });
                        }
                        else if (tokenH == (I<64>()))
                        {
                            s = self.method("_get_size", { tokenL });
                            result = self.method("_read", { s });
                        }
                        else if (tokenH == (I<80>()))
                        {
                            s = self.method("_get_size", { tokenL });
                            data = self.method("_read", { s });
                            result = data.method("decode", { S<"ascii">() });
                        }
                        else if (tokenH == (I<96>()))
                        {
                            s = self.method("_get_size", { tokenL }) * (I<2>());
                            data = self.method("_read", { s });
                            result = data.method("decode", { S<"utf-16be">() });
                        }
                        else if (tokenH == (I<128>()))
                        {
                            // used by Key-Archiver plist files
                            result = (G<"plistlib.UID">())((B<"int">()).method("from_bytes", { self.attr("_fp").method("read", { (I<1>()) + tokenL }), S<"big">() }));
                        }
                        else if (tokenH == (I<160>()))
                        {
                            s = self.method("_get_size", { tokenL });
                            obj_refs = self.method("_read_refs", { s });
                            result = py_list();
                            self.attr("_objects").set_item(ref_, result);
                            for (py const& item : iterate{ obj_refs })
                            {
                                x = item;
                                result.method("append", { self.method("_read_object", { x }) });
                            }
                        }
                        // tokenH == 0xB0 is documented as 'ordset', but is not actually
                        // implemented in the Apple reference code.
                        // tokenH == 0xC0 is documented as 'set', but sets cannot be used in
                        // plists.
                        else if (tokenH == (I<208>()))
                        {
                            s = self.method("_get_size", { tokenL });
                            key_refs = self.method("_read_refs", { s });
                            obj_refs = self.method("_read_refs", { s });
                            result = self.method("_dict_type");
                            self.attr("_objects").set_item(ref_, result);
                            try
                            {
                                for (py const& item : iterate{ (B<"zip">())(key_refs, obj_refs) })
                                {
                                    auto const items25 = unpack(item, 2);
                                    k = items25[0];
                                    o = items25[1];
                                    result.set_item(self.method("_read_object", { k }), self.method("_read_object", { o }));
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler23;
                                if (matches(handler23.exception, B<"TypeError">()))
                                {
                                    py_raise((G<"plistlib.InvalidFileException">())());
                                }
                                else
                                    py_reraise(handler23.exception);
                            }
                        }
                        else
                        {
                            py_raise((G<"plistlib.InvalidFileException">())());
                        }
                        self.attr("_objects").set_item(ref_, result);
                        return result;
                    }) }, }));

            define("plistlib", "_count_to_size", w.function("_count_to_size", "_count_to_size", { py_positional("count") }, [](arguments a) -> py
                {
                    py count = a[0];
                    if (count < ((I<1>()) << (I<8>())))
                    {
                        return I<1>();
                    }
                    else if (count < ((I<1>()) << (I<16>())))
                    {
                        return I<2>();
                    }
                    else if (count < ((I<1>()) << (I<32>())))
                    {
                        return I<4>();
                    }
                    else
                    {
                        return I<8>();
                    }
                    return none();
                }));

            define("plistlib", "_scalars", py_tuple({ B<"str">(), B<"int">(), B<"float">(), Mod<"datetime">().attr("datetime"), B<"bytes">() }));
            define("plistlib", "_BinaryPlistWriter", w.make_class("_BinaryPlistWriter", { B<"object">() }, {
                { "__init__", w.method("_BinaryPlistWriter", "__init__", { py_positional("fp"), py_positional("sort_keys"), py_positional("skipkeys"), py_positional("aware_datetime", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        py sort_keys = a[2];
                        py skipkeys = a[3];
                        py aware_datetime = a[4];
                        self.set_attr("_fp", fp);
                        self.set_attr("_sort_keys", sort_keys);
                        self.set_attr("_skipkeys", skipkeys);
                        self.set_attr("_aware_datetime", aware_datetime);
                        return none();
                    }) },
                { "write", w.method("_BinaryPlistWriter", "write", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py num_objects;
                        py obj;
                        py offset_format;
                        py offset_size;
                        py offset_table_offset;
                        py sort_version;
                        py top_object;
                        py trailer;
                        self.set_attr("_objlist", py_list());
                        // Mappings from object->objectid
                        // First dict has (type(object), object) as the key,
                        // second dict is used when object is not hashable and
                        // has id(object) as the key.
                        self.set_attr("_objtable", py_dict());
                        self.set_attr("_objidtable", py_dict());
                        // Create list of all objects in the plist
                        self.method("_flatten", { value });
                        // Size of object references in serialized containers
                        // depends on the number of objects in the plist.
                        num_objects = py{ self.attr("_objlist").size() };
                        self.set_attr("_object_offsets", py_list({ I<0>() }) * num_objects);
                        self.set_attr("_ref_size", (G<"plistlib._count_to_size">())(num_objects));
                        self.set_attr("_ref_format", (G<"plistlib._BINARY_FORMAT">())[self.attr("_ref_size")]);
                        // Write file header
                        self.attr("_fp").method("write", { py{ make_bytes(std::string{ "\x62\x70\x6c\x69\x73\x74\x30\x30", 8 }) } });
                        // Write object list
                        for (py const& item : iterate{ self.attr("_objlist") })
                        {
                            obj = item;
                            self.method("_write_object", { obj });
                        }
                        // Write refnum->object offset table
                        top_object = self.method("_getrefnum", { value });
                        offset_table_offset = self.attr("_fp").method("tell");
                        offset_size = (G<"plistlib._count_to_size">())(offset_table_offset);
                        offset_format = (S<">">()) + ((G<"plistlib._BINARY_FORMAT">())[offset_size] * num_objects);
                        self.attr("_fp").method("write", { call_ex((Mod<"struct">().attr("pack")), { offset_format }, self.attr("_object_offsets"), py{}) });
                        // Write trailer
                        sort_version = I<0>();
                        trailer = py_tuple({ sort_version, offset_size, self.attr("_ref_size"), num_objects, top_object, offset_table_offset });
                        self.attr("_fp").method("write", { call_ex((Mod<"struct">().attr("pack")), { S<">5xBBBQQQ">() }, trailer, py{}) });
                        return none();
                    }) },
                { "_flatten", w.method("_BinaryPlistWriter", "_flatten", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py items;
                        py k;
                        py keys;
                        py o;
                        py refnum;
                        py v;
                        py values;
                        if (isinstance(value, G<"plistlib._scalars">()))
                        {
                            if (self.attr("_objtable").contains(py_tuple({ (B<"type">())(value), value })))
                            {
                                return none();
                            }
                        }
                        else if (self.attr("_objidtable").contains((B<"id">())(value)))
                        {
                            return none();
                        }
                        // Add to objectreference map
                        refnum = py{ self.attr("_objlist").size() };
                        self.attr("_objlist").method("append", { value });
                        if (isinstance(value, G<"plistlib._scalars">()))
                        {
                            self.attr("_objtable").set_item(py_tuple({ (B<"type">())(value), value }), refnum);
                        }
                        else
                        {
                            self.attr("_objidtable").set_item((B<"id">())(value), refnum);
                        }
                        // And finally recurse into containers
                        if (isinstance(value, B<"dict">()))
                        {
                            keys = py_list();
                            values = py_list();
                            items = (G<"plistlib._dict_items">())(value, self.attr("_sort_keys"), self.attr("_skipkeys"));
                            for (py const& item : iterate{ items })
                            {
                                auto const items28 = unpack(item, 2);
                                k = items28[0];
                                v = items28[1];
                                if (!isinstance(k, B<"str">()))
                                {
                                    if (self.attr("_skipkeys").truthy())
                                    {
                                        continue;
                                    }
                                    py_raise((B<"TypeError">())(S<"keys must be strings">()));
                                }
                                keys.method("append", { k });
                                values.method("append", { v });
                            }
                            for (py const& item : iterate{ (Mod<"itertools">().attr("chain"))(keys, values) })
                            {
                                o = item;
                                self.method("_flatten", { o });
                            }
                        }
                        else if (isinstance(value, py_tuple({ B<"list">(), B<"tuple">() })))
                        {
                            for (py const& item : iterate{ value })
                            {
                                o = item;
                                self.method("_flatten", { o });
                            }
                        }
                        return none();
                    }) },
                { "_getrefnum", w.method("_BinaryPlistWriter", "_getrefnum", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        if (isinstance(value, G<"plistlib._scalars">()))
                        {
                            return self.attr("_objtable")[py_tuple({ (B<"type">())(value), value })];
                        }
                        else
                        {
                            return self.attr("_objidtable")[(B<"id">())(value)];
                        }
                        return none();
                    }) },
                { "_write_size", w.method("_BinaryPlistWriter", "_write_size", { py_positional("token"), py_positional("size") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py token = a[1];
                        py size = a[2];
                        if (size < (I<15>()))
                        {
                            self.attr("_fp").method("write", { (Mod<"struct">().attr("pack"))(S<">B">(), token | size) });
                        }
                        else if (size < ((I<1>()) << (I<8>())))
                        {
                            self.attr("_fp").method("write", { (Mod<"struct">().attr("pack"))(S<">BBB">(), token | (I<15>()), I<16>(), size) });
                        }
                        else if (size < ((I<1>()) << (I<16>())))
                        {
                            self.attr("_fp").method("write", { (Mod<"struct">().attr("pack"))(S<">BBH">(), token | (I<15>()), I<17>(), size) });
                        }
                        else if (size < ((I<1>()) << (I<32>())))
                        {
                            self.attr("_fp").method("write", { (Mod<"struct">().attr("pack"))(S<">BBL">(), token | (I<15>()), I<18>(), size) });
                        }
                        else
                        {
                            self.attr("_fp").method("write", { (Mod<"struct">().attr("pack"))(S<">BBQ">(), token | (I<15>()), I<19>(), size) });
                        }
                        return none();
                    }) },
                { "_write_object", w.method("_BinaryPlistWriter", "_write_object", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py dt;
                        py f;
                        py k;
                        py keyRefs;
                        py offset;
                        py ref_;
                        py refs;
                        py rootItems;
                        py s;
                        py t;
                        py v;
                        py valRefs;
                        ref_ = self.method("_getrefnum", { value });
                        self.attr("_object_offsets").set_item(ref_, self.attr("_fp").method("tell"));
                        if (value.is_none())
                        {
                            self.attr("_fp").method("write", { py{ make_bytes(std::string{ "\x00", 1 }) } });
                        }
                        else if (value.is(boolean(false)))
                        {
                            self.attr("_fp").method("write", { py{ make_bytes(std::string{ "\x08", 1 }) } });
                        }
                        else if (value.is(boolean(true)))
                        {
                            self.attr("_fp").method("write", { py{ make_bytes(std::string{ "\x09", 1 }) } });
                        }
                        else if (isinstance(value, B<"int">()))
                        {
                            if (value < (I<0>()))
                            {
                                try
                                {
                                    self.attr("_fp").method("write", { (Mod<"struct">().attr("pack"))(S<">Bq">(), I<19>(), value) });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler31;
                                    if (matches(handler31.exception, Mod<"struct">().attr("error")))
                                    {
                                        fn_raise_from((B<"OverflowError">())(value).get(), none().get());
                                        throw raised{};
                                    }
                                    else
                                        py_reraise(handler31.exception);
                                }
                            }
                            else if (value < ((I<1>()) << (I<8>())))
                            {
                                self.attr("_fp").method("write", { (Mod<"struct">().attr("pack"))(S<">BB">(), I<16>(), value) });
                            }
                            else if (value < ((I<1>()) << (I<16>())))
                            {
                                self.attr("_fp").method("write", { (Mod<"struct">().attr("pack"))(S<">BH">(), I<17>(), value) });
                            }
                            else if (value < ((I<1>()) << (I<32>())))
                            {
                                self.attr("_fp").method("write", { (Mod<"struct">().attr("pack"))(S<">BL">(), I<18>(), value) });
                            }
                            else if (value < ((I<1>()) << (I<63>())))
                            {
                                self.attr("_fp").method("write", { (Mod<"struct">().attr("pack"))(S<">BQ">(), I<19>(), value) });
                            }
                            else if (value < ((I<1>()) << (I<64>())))
                            {
                                self.attr("_fp").method("write", { py{ make_bytes(std::string{ "\x14", 1 }) } + value.method("to_bytes", { I<16>(), S<"big">() }, { { "signed", boolean(true) } }) });
                            }
                            else
                            {
                                py_raise((B<"OverflowError">())(value));
                            }
                        }
                        else if (isinstance(value, B<"float">()))
                        {
                            self.attr("_fp").method("write", { (Mod<"struct">().attr("pack"))(S<">Bd">(), I<35>(), value) });
                        }
                        else if (isinstance(value, Mod<"datetime">().attr("datetime")))
                        {
                            if (self.attr("_aware_datetime").truthy())
                            {
                                dt = value.method("astimezone", { Mod<"datetime">().attr("UTC") });
                                offset = dt - (Mod<"datetime">().attr("datetime")).call({ I<2001>(), I<1>(), I<1>() }, { { "tzinfo", Mod<"datetime">().attr("UTC") } });
                                f = offset.method("total_seconds");
                            }
                            else
                            {
                                f = (value - (Mod<"datetime">().attr("datetime"))(I<2001>(), I<1>(), I<1>())).method("total_seconds");
                            }
                            self.attr("_fp").method("write", { (Mod<"struct">().attr("pack"))(S<">Bd">(), I<51>(), f) });
                        }
                        else if (isinstance(value, py_tuple({ B<"bytes">(), B<"bytearray">() })))
                        {
                            self.method("_write_size", { I<64>(), py{ value.size() } });
                            self.attr("_fp").method("write", { value });
                        }
                        else if (isinstance(value, B<"str">()))
                        {
                            try
                            {
                                t = value.method("encode", { S<"ascii">() });
                                self.method("_write_size", { I<80>(), py{ value.size() } });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler32;
                                if (matches(handler32.exception, B<"UnicodeEncodeError">()))
                                {
                                    t = value.method("encode", { S<"utf-16be">() });
                                    self.method("_write_size", { I<96>(), floordiv(py{ t.size() }, (I<2>())) });
                                }
                                else
                                    py_reraise(handler32.exception);
                            }
                            self.attr("_fp").method("write", { t });
                        }
                        else if (isinstance(value, G<"plistlib.UID">()))
                        {
                            if (value.attr("data") < (I<0>()))
                            {
                                py_raise((B<"ValueError">())(S<"UIDs must be positive">()));
                            }
                            else if (value.attr("data") < ((I<1>()) << (I<8>())))
                            {
                                self.attr("_fp").method("write", { (Mod<"struct">().attr("pack"))(S<">BB">(), I<128>(), value) });
                            }
                            else if (value.attr("data") < ((I<1>()) << (I<16>())))
                            {
                                self.attr("_fp").method("write", { (Mod<"struct">().attr("pack"))(S<">BH">(), I<129>(), value) });
                            }
                            else if (value.attr("data") < ((I<1>()) << (I<32>())))
                            {
                                self.attr("_fp").method("write", { (Mod<"struct">().attr("pack"))(S<">BL">(), I<131>(), value) });
                            }
                            else if (value.attr("data") < ((I<1>()) << (I<64>())))
                            {
                                self.attr("_fp").method("write", { (Mod<"struct">().attr("pack"))(S<">BQ">(), I<135>(), value) });
                            }
                            else
                            {
                                py_raise((B<"OverflowError">())(value));
                            }
                        }
                        else if (isinstance(value, py_tuple({ B<"list">(), B<"tuple">() })))
                        {
                            refs = [&]() -> py { py const comprehension33 = py_list(); py o; for (py const& item : iterate{ value }) { o = item; comprehension33.method("append", { self.method("_getrefnum", { o }) }); } return comprehension33; }();
                            s = py{ refs.size() };
                            self.method("_write_size", { I<160>(), s });
                            self.attr("_fp").method("write", { call_ex((Mod<"struct">().attr("pack")), { (S<">">()) + (self.attr("_ref_format") * s) }, refs, py{}) });
                        }
                        else if (isinstance(value, B<"dict">()))
                        {
                            auto const items34 = unpack(py_tuple({ py_list(), py_list() }), 2);
                            keyRefs = items34[0];
                            valRefs = items34[1];
                            rootItems = (G<"plistlib._dict_items">())(value, self.attr("_sort_keys"), self.attr("_skipkeys"));
                            for (py const& item : iterate{ rootItems })
                            {
                                auto const items36 = unpack(item, 2);
                                k = items36[0];
                                v = items36[1];
                                if (!isinstance(k, B<"str">()))
                                {
                                    if (self.attr("_skipkeys").truthy())
                                    {
                                        continue;
                                    }
                                    py_raise((B<"TypeError">())(S<"keys must be strings">()));
                                }
                                keyRefs.method("append", { self.method("_getrefnum", { k }) });
                                valRefs.method("append", { self.method("_getrefnum", { v }) });
                            }
                            s = py{ keyRefs.size() };
                            self.method("_write_size", { I<208>(), s });
                            self.attr("_fp").method("write", { call_ex((Mod<"struct">().attr("pack")), { (S<">">()) + (self.attr("_ref_format") * s) }, keyRefs, py{}) });
                            self.attr("_fp").method("write", { call_ex((Mod<"struct">().attr("pack")), { (S<">">()) + (self.attr("_ref_format") * s) }, valRefs, py{}) });
                        }
                        else
                        {
                            py_raise((B<"TypeError">())(value));
                        }
                        return none();
                    }) }, }));

            define("plistlib", "_is_fmt_binary", w.function("_is_fmt_binary", "_is_fmt_binary", { py_positional("header") }, [](arguments a) -> py
                {
                    py header = a[0];
                    return boolean(header[B<"slice">()(none(), I<8>(), none())] == py{ make_bytes(std::string{ "\x62\x70\x6c\x69\x73\x74\x30\x30", 8 }) });
                }));

            // 
            // Generic bits
            // 
            define("plistlib", "_FORMATS", dict_of({ { G<"plistlib.FMT_XML">(), (B<"dict">()).call({  }, { { "detect", G<"plistlib._is_fmt_xml">() }, { "parser", G<"plistlib._PlistParser">() }, { "writer", G<"plistlib._PlistWriter">() } }) }, { G<"plistlib.FMT_BINARY">(), (B<"dict">()).call({  }, { { "detect", G<"plistlib._is_fmt_binary">() }, { "parser", G<"plistlib._BinaryPlistParser">() }, { "writer", G<"plistlib._BinaryPlistWriter">() } }) } }));
            define("plistlib", "load", w.function("load", "load", { py_positional("fp"), py_keyword("fmt", none()), py_keyword("dict_type", B<"dict">()), py_keyword("aware_datetime", boolean(false)) }, [](arguments a) -> py
                {
                    py fp = a[0];
                    py fmt = a[1];
                    py dict_type = a[2];
                    py aware_datetime = a[3];
                    py P_;
                    py header;
                    py info;
                    py p;
                    if (fmt.is_none())
                    {
                        header = fp.method("read", { I<32>() });
                        fp.method("seek", { I<0>() });
                        bool broke37 = false;
                        for (py const& item : iterate{ (G<"plistlib._FORMATS">()).method("values") })
                        {
                            info = item;
                            if (info[S<"detect">()](header).truthy())
                            {
                                P_ = info[S<"parser">()];
                                broke37 = true;
                                break;
                            }
                        }
                        if (!broke37)
                        {
                            py_raise((G<"plistlib.InvalidFileException">())());
                        }
                    }
                    else
                    {
                        P_ = (G<"plistlib._FORMATS">())[fmt][S<"parser">()];
                    }
                    p = P_.call({  }, { { "dict_type", dict_type }, { "aware_datetime", aware_datetime } });
                    return p.method("parse", { fp });
                }));

            define("plistlib", "loads", w.function("loads", "loads", { py_positional("value"), py_keyword("fmt", none()), py_keyword("dict_type", B<"dict">()), py_keyword("aware_datetime", boolean(false)) }, [](arguments a) -> py
                {
                    py value = a[0];
                    py fmt = a[1];
                    py dict_type = a[2];
                    py aware_datetime = a[3];
                    py fp;
                    if (isinstance(value, B<"str">()))
                    {
                        if (fmt == (G<"plistlib.FMT_BINARY">()))
                        {
                            py_raise((B<"TypeError">())(S<"value must be bytes-like object when fmt is FMT_BINARY">()));
                        }
                        value = value.method("encode");
                    }
                    fp = (Mod<"io">().attr("BytesIO"))(value);
                    return (G<"plistlib.load">()).call({ fp }, { { "fmt", fmt }, { "dict_type", dict_type }, { "aware_datetime", aware_datetime } });
                }));

            define("plistlib", "dump", w.function("dump", "dump", { py_positional("value"), py_positional("fp"), py_keyword("fmt", G<"plistlib.FMT_XML">()), py_keyword("sort_keys", boolean(true)), py_keyword("skipkeys", boolean(false)), py_keyword("aware_datetime", boolean(false)) }, [](arguments a) -> py
                {
                    py value = a[0];
                    py fp = a[1];
                    py fmt = a[2];
                    py sort_keys = a[3];
                    py skipkeys = a[4];
                    py aware_datetime = a[5];
                    py writer;
                    if (!(G<"plistlib._FORMATS">()).contains(fmt))
                    {
                        py_raise((B<"ValueError">())((S<"Unsupported format: %r">()) % py_tuple({ fmt })));
                    }
                    writer = (G<"plistlib._FORMATS">())[fmt][S<"writer">()].call({ fp }, { { "sort_keys", sort_keys }, { "skipkeys", skipkeys }, { "aware_datetime", aware_datetime } });
                    writer.method("write", { value });
                    return none();
                }));

            define("plistlib", "dumps", w.function("dumps", "dumps", { py_positional("value"), py_keyword("fmt", G<"plistlib.FMT_XML">()), py_keyword("skipkeys", boolean(false)), py_keyword("sort_keys", boolean(true)), py_keyword("aware_datetime", boolean(false)) }, [](arguments a) -> py
                {
                    py value = a[0];
                    py fmt = a[1];
                    py skipkeys = a[2];
                    py sort_keys = a[3];
                    py aware_datetime = a[4];
                    py fp;
                    fp = (Mod<"io">().attr("BytesIO"))();
                    (G<"plistlib.dump">()).call({ value, fp }, { { "fmt", fmt }, { "skipkeys", skipkeys }, { "sort_keys", sort_keys }, { "aware_datetime", aware_datetime } });
                    return fp.method("getvalue");
                }));
        }

        bool plistlib_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_plistlib();
                }
                if (auto const doc = plistlib_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("binascii", py{ fn_import("binascii") }.release());
                aAttributes.emplace_back("codecs", py{ fn_import("codecs") }.release());
                aAttributes.emplace_back("datetime", py{ fn_import("datetime") }.release());
                aAttributes.emplace_back("enum", py{ fn_import("enum") }.release());
                aAttributes.emplace_back("BytesIO", import_attribute("io", "BytesIO").release());
                aAttributes.emplace_back("itertools", py{ fn_import("itertools") }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("struct", py{ fn_import("struct") }.release());
                aAttributes.emplace_back("ParserCreate", import_attribute("xml.parsers.expat", "ParserCreate").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("plistlib", plistlib_module);
    }
}
