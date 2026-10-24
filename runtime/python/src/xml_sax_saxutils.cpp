/*
  xml_sax_saxutils.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/xml/sax/saxutils.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module xml.sax.saxutils (CPython 3.13's Lib/xml/sax/saxutils.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "xml_sax_saxutils_docs.hpp"

namespace neos::python_runtime::detail::xml_sax_saxutils_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "xml.sax.saxutils.name"))
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
                py_raise("SystemError", std::string{ "xml.sax.saxutils: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"xml.sax.saxutils.name">()))
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
            if (modules_imported("xml.sax.saxutils"))
                return py{ fn_import("xml.sax.saxutils") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "xml.sax.saxutils" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "xml.sax.saxutils", xml_sax_saxutils_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- xml.sax.saxutils (Lib/xml/sax/saxutils.py)

        void make_xml_sax_saxutils()
        {
            static module_writer const& w = writer("xml.sax.saxutils");
            define("xml.sax.saxutils", "__dict_replace", w.function("__dict_replace", "__dict_replace", { py_positional("s"), py_positional("d") }, [](arguments a) -> py
                {
                    py s = a[0];
                    py d = a[1];
                    py key;
                    py value;
                    for (py const& item : iterate{ d.method("items") })
                    {
                        auto const items2 = unpack(item, 2);
                        key = items2[0];
                        value = items2[1];
                        s = s.method("replace", { key, value });
                    }
                    return s;
                }));

            define("xml.sax.saxutils", "escape", w.function("escape", "escape", { py_positional("data"), py_positional("entities", py_dict()) }, [](arguments a) -> py
                {
                    py data = a[0];
                    py entities = a[1];
                    // must do ampersand first
                    data = data.method("replace", { S<"&">(), S<"&amp;">() });
                    data = data.method("replace", { S<">">(), S<"&gt;">() });
                    data = data.method("replace", { S<"<">(), S<"&lt;">() });
                    if (entities.truthy())
                    {
                        data = (G<"xml.sax.saxutils.__dict_replace">())(data, entities);
                    }
                    return data;
                }));

            define("xml.sax.saxutils", "unescape", w.function("unescape", "unescape", { py_positional("data"), py_positional("entities", py_dict()) }, [](arguments a) -> py
                {
                    py data = a[0];
                    py entities = a[1];
                    data = data.method("replace", { S<"&lt;">(), S<"<">() });
                    data = data.method("replace", { S<"&gt;">(), S<">">() });
                    if (entities.truthy())
                    {
                        data = (G<"xml.sax.saxutils.__dict_replace">())(data, entities);
                    }
                    // must do ampersand last
                    return data.method("replace", { S<"&amp;">(), S<"&">() });
                }));

            define("xml.sax.saxutils", "quoteattr", w.function("quoteattr", "quoteattr", { py_positional("data"), py_positional("entities", py_dict()) }, [](arguments a) -> py
                {
                    py data = a[0];
                    py entities = a[1];
                    entities = [&]() -> py { py const d = py_dict(); d.method("update", { entities }); d.set_item(S<"\n">(), S<"&#10;">()); d.set_item(S<"\r">(), S<"&#13;">()); d.set_item(S<"\t">(), S<"&#9;">()); return d; }();
                    data = (G<"xml.sax.saxutils.escape">())(data, entities);
                    if (data.contains((S<"\"">())))
                    {
                        if (data.contains((S<"'">())))
                        {
                            data = (S<"\"%s\"">()) % data.method("replace", { S<"\"">(), S<"&quot;">() });
                        }
                        else
                        {
                            data = (S<"'%s'">()) % data;
                        }
                    }
                    else
                    {
                        data = (S<"\"%s\"">()) % data;
                    }
                    return data;
                }));

            define("xml.sax.saxutils", "_gettextwriter", w.function("_gettextwriter", "_gettextwriter", { py_positional("out"), py_positional("encoding") }, [](arguments a) -> py
                {
                    auto const out = std::make_shared<py>(a[0]);
                    py encoding = a[1];
                    py _wrapper;
                    py buffer;
                    py sys;
                    if ((*out).is_none())
                    {
                        sys = module_of("sys");
                        return sys.attr("stdout");
                    }
                    if (isinstance((*out), Mod<"io">().attr("TextIOBase")))
                    {
                        // use a text writer as is
                        return (*out);
                    }
                    if (isinstance((*out), py_tuple({ Mod<"codecs">().attr("StreamWriter"), Mod<"codecs">().attr("StreamReaderWriter") })))
                    {
                        // use a codecs stream writer as is
                        return (*out);
                    }
                    // wrap a binary writer with TextIOWrapper
                    if (isinstance((*out), Mod<"io">().attr("RawIOBase")))
                    {
                        // Keep the original file open when the TextIOWrapper is
                        // destroyed
                        _wrapper = w.make_class("_wrapper", {  }, {
        { "__class__", (*out).attr("__class__") },
        { "__getattr__", w.method("_wrapper", "__getattr__", { py_positional("name") }, [out](arguments a) -> py
            {
                py self = a[0];
                py name = a[1];
                return (B<"getattr">())((*out), name);
            }) }, }, "_gettextwriter.<locals>._wrapper");
                        buffer = _wrapper();
                        buffer.set_attr("close", w.function("<lambda>", "_gettextwriter.<locals>.<lambda>", {  }, [](arguments a) -> py
        {
            return none();
        }));
                    }
                    else
                    {
                        // This is to handle passed objects that aren't in the
                        // IOBase hierarchy, but just have a write method
                        buffer = (Mod<"io">().attr("BufferedIOBase"))();
                        buffer.set_attr("writable", w.function("<lambda>", "_gettextwriter.<locals>.<lambda>", {  }, [](arguments a) -> py
        {
            return boolean(true);
        }));
                        buffer.set_attr("write", (*out).attr("write"));
                        try
                        {
                            // TextIOWrapper uses this methods to determine
                            // if BOM (for UTF-16, etc) should be added
                            buffer.set_attr("seekable", (*out).attr("seekable"));
                            buffer.set_attr("tell", (*out).attr("tell"));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler3;
                            if (matches(handler3.exception, B<"AttributeError">()))
                            {
                            }
                            else
                                py_reraise(handler3.exception);
                        }
                    }
                    return (Mod<"io">().attr("TextIOWrapper")).call({ buffer }, { { "encoding", encoding }, { "errors", S<"xmlcharrefreplace">() }, { "newline", S<"\n">() }, { "write_through", boolean(true) } });
                }));

                // ContentHandler methods
            define("xml.sax.saxutils", "XMLGenerator", w.make_class("XMLGenerator", { Mod<"xml.sax.handler">().attr("ContentHandler") }, {
                { "__init__", w.method("XMLGenerator", "__init__", { py_positional("out", none()), py_positional("encoding", S<"iso-8859-1">()), py_positional("short_empty_elements", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py out = a[1];
                        py encoding = a[2];
                        py short_empty_elements = a[3];
                        (Mod<"xml.sax.handler">().attr("ContentHandler")).method("__init__", { self });
                        out = (G<"xml.sax.saxutils._gettextwriter">())(out, encoding);
                        self.set_attr("_write", out.attr("write"));
                        self.set_attr("_flush", out.attr("flush"));
                        self.set_attr("_ns_contexts", py_list({ py_dict() }));
                        self.set_attr("_current_context", self.attr("_ns_contexts")[I<-1>()]);
                        self.set_attr("_undeclared_ns_maps", py_list());
                        self.set_attr("_encoding", encoding);
                        self.set_attr("_short_empty_elements", short_empty_elements);
                        self.set_attr("_pending_start_element", boolean(false));
                        return none();
                    }) },
                { "_qname", w.method("XMLGenerator", "_qname", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py prefix;
                        if (name[I<0>()].truthy())
                        {
                            // Per http://www.w3.org/XML/1998/namespace, The 'xml' prefix is
                            // bound by definition to http://www.w3.org/XML/1998/namespace.  It
                            // does not need to be declared and will not usually be found in
                            // self._current_context.
                            if ((S<"http://www.w3.org/XML/1998/namespace">()) == name[I<0>()])
                            {
                                return (S<"xml:">()) + name[I<1>()];
                            }
                            // The name is in a non-empty namespace
                            prefix = self.attr("_current_context")[name[I<0>()]];
                            if (prefix.truthy())
                            {
                                // If it is not the default namespace, prepend the prefix
                                return (prefix + (S<":">())) + name[I<1>()];
                            }
                        }
                        // Return the unqualified name
                        return name[I<1>()];
                    }) },
                { "_finish_pending_start_element", w.method("XMLGenerator", "_finish_pending_start_element", { py_positional("endElement", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py endElement = a[1];
                        if (self.attr("_pending_start_element").truthy())
                        {
                            self.method("_write", { S<">">() });
                            self.set_attr("_pending_start_element", boolean(false));
                        }
                        return none();
                    }) },
                { "startDocument", w.method("XMLGenerator", "startDocument", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_write", { (S<"<?xml version=\"1.0\" encoding=\"%s\"?>\n">()) % self.attr("_encoding") });
                        return none();
                    }) },
                { "endDocument", w.method("XMLGenerator", "endDocument", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_flush");
                        return none();
                    }) },
                { "startPrefixMapping", w.method("XMLGenerator", "startPrefixMapping", { py_positional("prefix"), py_positional("uri") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prefix = a[1];
                        py uri = a[2];
                        self.attr("_ns_contexts").method("append", { self.attr("_current_context").method("copy") });
                        self.attr("_current_context").set_item(uri, prefix);
                        self.attr("_undeclared_ns_maps").method("append", { py_tuple({ prefix, uri }) });
                        return none();
                    }) },
                { "endPrefixMapping", w.method("XMLGenerator", "endPrefixMapping", { py_positional("prefix") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prefix = a[1];
                        self.set_attr("_current_context", self.attr("_ns_contexts")[I<-1>()]);
                        del_item(self.attr("_ns_contexts"), I<-1>());
                        return none();
                    }) },
                { "startElement", w.method("XMLGenerator", "startElement", { py_positional("name"), py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py attrs = a[2];
                        py value;
                        self.method("_finish_pending_start_element");
                        self.method("_write", { (S<"<">()) + name });
                        for (py const& item : iterate{ attrs.method("items") })
                        {
                            auto const items5 = unpack(item, 2);
                            name = items5[0];
                            value = items5[1];
                            self.method("_write", { (S<" %s=%s">()) % py_tuple({ name, (G<"xml.sax.saxutils.quoteattr">())(value) }) });
                        }
                        if (self.attr("_short_empty_elements").truthy())
                        {
                            self.set_attr("_pending_start_element", boolean(true));
                        }
                        else
                        {
                            self.method("_write", { S<">">() });
                        }
                        return none();
                    }) },
                { "endElement", w.method("XMLGenerator", "endElement", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        if (self.attr("_pending_start_element").truthy())
                        {
                            self.method("_write", { S<"/>">() });
                            self.set_attr("_pending_start_element", boolean(false));
                        }
                        else
                        {
                            self.method("_write", { (S<"</%s>">()) % name });
                        }
                        return none();
                    }) },
                { "startElementNS", w.method("XMLGenerator", "startElementNS", { py_positional("name"), py_positional("qname"), py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py qname = a[2];
                        py attrs = a[3];
                        py prefix;
                        py uri;
                        py value;
                        self.method("_finish_pending_start_element");
                        self.method("_write", { (S<"<">()) + self.method("_qname", { name }) });
                        for (py const& item : iterate{ self.attr("_undeclared_ns_maps") })
                        {
                            auto const items7 = unpack(item, 2);
                            prefix = items7[0];
                            uri = items7[1];
                            if (prefix.truthy())
                            {
                                self.method("_write", { (S<" xmlns:%s=%s">()) % py_tuple({ prefix, (G<"xml.sax.saxutils.quoteattr">())(uri) }) });
                            }
                            else
                            {
                                self.method("_write", { (S<" xmlns=%s">()) % (G<"xml.sax.saxutils.quoteattr">())(uri) });
                            }
                        }
                        self.set_attr("_undeclared_ns_maps", py_list());
                        for (py const& item : iterate{ attrs.method("items") })
                        {
                            auto const items9 = unpack(item, 2);
                            name = items9[0];
                            value = items9[1];
                            self.method("_write", { (S<" %s=%s">()) % py_tuple({ self.method("_qname", { name }), (G<"xml.sax.saxutils.quoteattr">())(value) }) });
                        }
                        if (self.attr("_short_empty_elements").truthy())
                        {
                            self.set_attr("_pending_start_element", boolean(true));
                        }
                        else
                        {
                            self.method("_write", { S<">">() });
                        }
                        return none();
                    }) },
                { "endElementNS", w.method("XMLGenerator", "endElementNS", { py_positional("name"), py_positional("qname") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py qname = a[2];
                        if (self.attr("_pending_start_element").truthy())
                        {
                            self.method("_write", { S<"/>">() });
                            self.set_attr("_pending_start_element", boolean(false));
                        }
                        else
                        {
                            self.method("_write", { (S<"</%s>">()) % self.method("_qname", { name }) });
                        }
                        return none();
                    }) },
                { "characters", w.method("XMLGenerator", "characters", { py_positional("content") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py content = a[1];
                        if (content.truthy())
                        {
                            self.method("_finish_pending_start_element");
                            if (!isinstance(content, B<"str">()))
                            {
                                content = (B<"str">())(content, self.attr("_encoding"));
                            }
                            self.method("_write", { (G<"xml.sax.saxutils.escape">())(content) });
                        }
                        return none();
                    }) },
                { "ignorableWhitespace", w.method("XMLGenerator", "ignorableWhitespace", { py_positional("content") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py content = a[1];
                        if (content.truthy())
                        {
                            self.method("_finish_pending_start_element");
                            if (!isinstance(content, B<"str">()))
                            {
                                content = (B<"str">())(content, self.attr("_encoding"));
                            }
                            self.method("_write", { content });
                        }
                        return none();
                    }) },
                { "processingInstruction", w.method("XMLGenerator", "processingInstruction", { py_positional("target"), py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py target = a[1];
                        py data = a[2];
                        self.method("_finish_pending_start_element");
                        self.method("_write", { (S<"<?%s %s?>">()) % py_tuple({ target, data }) });
                        return none();
                    }) }, }));

                // ErrorHandler methods
                // ContentHandler methods
                // DTDHandler methods
                // EntityResolver methods
                // XMLReader methods
                // XMLFilter methods
            define("xml.sax.saxutils", "XMLFilterBase", w.make_class("XMLFilterBase", { Mod<"xml.sax.xmlreader">().attr("XMLReader") }, {
                { "__init__", w.method("XMLFilterBase", "__init__", { py_positional("parent", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parent = a[1];
                        (Mod<"xml.sax.xmlreader">().attr("XMLReader")).method("__init__", { self });
                        self.set_attr("_parent", parent);
                        return none();
                    }) },
                { "error", w.method("XMLFilterBase", "error", { py_positional("exception") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exception = a[1];
                        self.attr("_err_handler").method("error", { exception });
                        return none();
                    }) },
                { "fatalError", w.method("XMLFilterBase", "fatalError", { py_positional("exception") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exception = a[1];
                        self.attr("_err_handler").method("fatalError", { exception });
                        return none();
                    }) },
                { "warning", w.method("XMLFilterBase", "warning", { py_positional("exception") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exception = a[1];
                        self.attr("_err_handler").method("warning", { exception });
                        return none();
                    }) },
                { "setDocumentLocator", w.method("XMLFilterBase", "setDocumentLocator", { py_positional("locator") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py locator = a[1];
                        self.attr("_cont_handler").method("setDocumentLocator", { locator });
                        return none();
                    }) },
                { "startDocument", w.method("XMLFilterBase", "startDocument", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("_cont_handler").method("startDocument");
                        return none();
                    }) },
                { "endDocument", w.method("XMLFilterBase", "endDocument", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("_cont_handler").method("endDocument");
                        return none();
                    }) },
                { "startPrefixMapping", w.method("XMLFilterBase", "startPrefixMapping", { py_positional("prefix"), py_positional("uri") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prefix = a[1];
                        py uri = a[2];
                        self.attr("_cont_handler").method("startPrefixMapping", { prefix, uri });
                        return none();
                    }) },
                { "endPrefixMapping", w.method("XMLFilterBase", "endPrefixMapping", { py_positional("prefix") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prefix = a[1];
                        self.attr("_cont_handler").method("endPrefixMapping", { prefix });
                        return none();
                    }) },
                { "startElement", w.method("XMLFilterBase", "startElement", { py_positional("name"), py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py attrs = a[2];
                        self.attr("_cont_handler").method("startElement", { name, attrs });
                        return none();
                    }) },
                { "endElement", w.method("XMLFilterBase", "endElement", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        self.attr("_cont_handler").method("endElement", { name });
                        return none();
                    }) },
                { "startElementNS", w.method("XMLFilterBase", "startElementNS", { py_positional("name"), py_positional("qname"), py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py qname = a[2];
                        py attrs = a[3];
                        self.attr("_cont_handler").method("startElementNS", { name, qname, attrs });
                        return none();
                    }) },
                { "endElementNS", w.method("XMLFilterBase", "endElementNS", { py_positional("name"), py_positional("qname") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py qname = a[2];
                        self.attr("_cont_handler").method("endElementNS", { name, qname });
                        return none();
                    }) },
                { "characters", w.method("XMLFilterBase", "characters", { py_positional("content") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py content = a[1];
                        self.attr("_cont_handler").method("characters", { content });
                        return none();
                    }) },
                { "ignorableWhitespace", w.method("XMLFilterBase", "ignorableWhitespace", { py_positional("chars") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py chars = a[1];
                        self.attr("_cont_handler").method("ignorableWhitespace", { chars });
                        return none();
                    }) },
                { "processingInstruction", w.method("XMLFilterBase", "processingInstruction", { py_positional("target"), py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py target = a[1];
                        py data = a[2];
                        self.attr("_cont_handler").method("processingInstruction", { target, data });
                        return none();
                    }) },
                { "skippedEntity", w.method("XMLFilterBase", "skippedEntity", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        self.attr("_cont_handler").method("skippedEntity", { name });
                        return none();
                    }) },
                { "notationDecl", w.method("XMLFilterBase", "notationDecl", { py_positional("name"), py_positional("publicId"), py_positional("systemId") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py publicId = a[2];
                        py systemId = a[3];
                        self.attr("_dtd_handler").method("notationDecl", { name, publicId, systemId });
                        return none();
                    }) },
                { "unparsedEntityDecl", w.method("XMLFilterBase", "unparsedEntityDecl", { py_positional("name"), py_positional("publicId"), py_positional("systemId"), py_positional("ndata") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py publicId = a[2];
                        py systemId = a[3];
                        py ndata = a[4];
                        self.attr("_dtd_handler").method("unparsedEntityDecl", { name, publicId, systemId, ndata });
                        return none();
                    }) },
                { "resolveEntity", w.method("XMLFilterBase", "resolveEntity", { py_positional("publicId"), py_positional("systemId") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py publicId = a[1];
                        py systemId = a[2];
                        return self.attr("_ent_handler").method("resolveEntity", { publicId, systemId });
                    }) },
                { "parse", w.method("XMLFilterBase", "parse", { py_positional("source") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py source = a[1];
                        self.attr("_parent").method("setContentHandler", { self });
                        self.attr("_parent").method("setErrorHandler", { self });
                        self.attr("_parent").method("setEntityResolver", { self });
                        self.attr("_parent").method("setDTDHandler", { self });
                        self.attr("_parent").method("parse", { source });
                        return none();
                    }) },
                { "setLocale", w.method("XMLFilterBase", "setLocale", { py_positional("locale") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py locale = a[1];
                        self.attr("_parent").method("setLocale", { locale });
                        return none();
                    }) },
                { "getFeature", w.method("XMLFilterBase", "getFeature", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return self.attr("_parent").method("getFeature", { name });
                    }) },
                { "setFeature", w.method("XMLFilterBase", "setFeature", { py_positional("name"), py_positional("state") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py state = a[2];
                        self.attr("_parent").method("setFeature", { name, state });
                        return none();
                    }) },
                { "getProperty", w.method("XMLFilterBase", "getProperty", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return self.attr("_parent").method("getProperty", { name });
                    }) },
                { "setProperty", w.method("XMLFilterBase", "setProperty", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        self.attr("_parent").method("setProperty", { name, value });
                        return none();
                    }) },
                { "getParent", w.method("XMLFilterBase", "getParent", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_parent");
                    }) },
                { "setParent", w.method("XMLFilterBase", "setParent", { py_positional("parent") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parent = a[1];
                        self.set_attr("_parent", parent);
                        return none();
                    }) }, }));

            // --- Utility functions
            define("xml.sax.saxutils", "prepare_input_source", w.function("prepare_input_source", "prepare_input_source", { py_positional("source"), py_positional("base", S<"">()) }, [](arguments a) -> py
                {
                    py source = a[0];
                    py base = a[1];
                    py basehead;
                    py f;
                    py sysid;
                    py sysidfilename;
                    if (isinstance(source, Mod<"os">().attr("PathLike")))
                    {
                        source = (Mod<"os">().attr("fspath"))(source);
                    }
                    if (isinstance(source, B<"str">()))
                    {
                        source = (Mod<"xml.sax.xmlreader">().attr("InputSource"))(source);
                    }
                    else if (has_attr(source, "read"))
                    {
                        f = source;
                        source = (Mod<"xml.sax.xmlreader">().attr("InputSource"))();
                        if (isinstance(f.method("read", { I<0>() }), B<"str">()))
                        {
                            source.method("setCharacterStream", { f });
                        }
                        else
                        {
                            source.method("setByteStream", { f });
                        }
                        if ((has_attr(f, "name") && isinstance(f.attr("name"), B<"str">())))
                        {
                            source.method("setSystemId", { f.attr("name") });
                        }
                    }
                    if ((source.method("getCharacterStream").is_none() && source.method("getByteStream").is_none()))
                    {
                        sysid = source.method("getSystemId");
                        basehead = (Mod<"os">().attr("path")).method("dirname", { (Mod<"os">().attr("path")).method("normpath", { base }) });
                        sysidfilename = (Mod<"os">().attr("path")).method("join", { basehead, sysid });
                        if ((Mod<"os">().attr("path")).method("isfile", { sysidfilename }).truthy())
                        {
                            source.method("setSystemId", { sysidfilename });
                            f = (B<"open">())(sysidfilename, S<"rb">());
                        }
                        else
                        {
                            source.method("setSystemId", { (Mod<"urllib.parse">().attr("urljoin"))(base, sysid) });
                            f = (Mod<"urllib.request">().attr("urlopen"))(source.method("getSystemId"));
                        }
                        source.method("setByteStream", { f });
                    }
                    return source;
                }));
        }

        bool xml_sax_saxutils_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_xml_sax_saxutils();
                }
                if (auto const doc = xml_sax_saxutils_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("urllib", py{ fn_import("urllib") }.release());
                aAttributes.emplace_back("urllib", py{ fn_import("urllib") }.release());
                aAttributes.emplace_back("io", py{ fn_import("io") }.release());
                aAttributes.emplace_back("codecs", py{ fn_import("codecs") }.release());
                aAttributes.emplace_back("handler", py{ fn_import("xml.sax.handler") }.release());
                aAttributes.emplace_back("xmlreader", py{ fn_import("xml.sax.xmlreader") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("xml.sax.saxutils", xml_sax_saxutils_module);
    }
}
