/*
  xml_dom_pulldom.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/xml/dom/pulldom.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module xml.dom.pulldom (CPython 3.13's Lib/xml/dom/pulldom.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "xml_dom_pulldom_docs.hpp"

namespace neos::python_runtime::detail::xml_dom_pulldom_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "xml.dom.pulldom.name"))
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
                py_raise("SystemError", std::string{ "xml.dom.pulldom: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"xml.dom.pulldom.name">()))
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
            if (modules_imported("xml.dom.pulldom"))
                return py{ fn_import("xml.dom.pulldom") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "xml.dom.pulldom" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "xml.dom.pulldom", xml_dom_pulldom_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- xml.dom.pulldom (Lib/xml/dom/pulldom.py)

        void make_xml_dom_pulldom()
        {
            static module_writer const& w = writer("xml.dom.pulldom");
            define("xml.dom.pulldom", "START_ELEMENT", S<"START_ELEMENT">());
            define("xml.dom.pulldom", "END_ELEMENT", S<"END_ELEMENT">());
            define("xml.dom.pulldom", "COMMENT", S<"COMMENT">());
            define("xml.dom.pulldom", "START_DOCUMENT", S<"START_DOCUMENT">());
            define("xml.dom.pulldom", "END_DOCUMENT", S<"END_DOCUMENT">());
            define("xml.dom.pulldom", "PROCESSING_INSTRUCTION", S<"PROCESSING_INSTRUCTION">());
            define("xml.dom.pulldom", "IGNORABLE_WHITESPACE", S<"IGNORABLE_WHITESPACE">());
            define("xml.dom.pulldom", "CHARACTERS", S<"CHARACTERS">());
            define("xml.dom.pulldom", "PullDOM", w.make_class("PullDOM", { Mod<"xml.sax">().attr("ContentHandler") }, {
                { "_locator", none() },
                { "document", none() },
                { "__init__", w.method("PullDOM", "__init__", { py_positional("documentFactory", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py documentFactory = a[1];
                        py XML_NAMESPACE;
                        XML_NAMESPACE = M("xml.dom", "XML_NAMESPACE");
                        self.set_attr("documentFactory", documentFactory);
                        self.set_attr("firstEvent", py_list({ none(), none() }));
                        self.set_attr("lastEvent", self.attr("firstEvent"));
                        self.set_attr("elementStack", py_list());
                        self.set_attr("push", self.attr("elementStack").attr("append"));
                        try
                        {
                            self.set_attr("pop", self.attr("elementStack").attr("pop"));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler1;
                            if (matches(handler1.exception, B<"AttributeError">()))
                            {
                                // use class' pop instead
                            }
                            else
                                py_reraise(handler1.exception);
                        }
                        self.set_attr("_ns_contexts", py_list({ dict_of({ { XML_NAMESPACE, S<"xml">() } }) }));
                        self.set_attr("_current_context", self.attr("_ns_contexts")[I<-1>()]);
                        self.set_attr("pending_events", py_list());
                        return none();
                    }) },
                { "pop", w.method("PullDOM", "pop", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py result;
                        result = self.attr("elementStack")[I<-1>()];
                        del_item(self.attr("elementStack"), I<-1>());
                        return result;
                    }) },
                { "setDocumentLocator", w.method("PullDOM", "setDocumentLocator", { py_positional("locator") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py locator = a[1];
                        self.set_attr("_locator", locator);
                        return none();
                    }) },
                { "startPrefixMapping", w.method("PullDOM", "startPrefixMapping", { py_positional("prefix"), py_positional("uri") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prefix = a[1];
                        py uri = a[2];
                        if (!has_attr(self, "_xmlns_attrs"))
                        {
                            self.set_attr("_xmlns_attrs", py_list());
                        }
                        self.attr("_xmlns_attrs").method("append", { py_tuple({ [&]() -> py { py const t = prefix; return t.truthy() ? t : S<"xmlns">(); }(), uri }) });
                        self.attr("_ns_contexts").method("append", { self.attr("_current_context").method("copy") });
                        self.attr("_current_context").set_item(uri, [&]() -> py { py const t = prefix; return t.truthy() ? t : none(); }());
                        return none();
                    }) },
                { "endPrefixMapping", w.method("PullDOM", "endPrefixMapping", { py_positional("prefix") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prefix = a[1];
                        self.set_attr("_current_context", self.attr("_ns_contexts").method("pop"));
                        return none();
                    }) },
                { "startElementNS", w.method("PullDOM", "startElementNS", { py_positional("name"), py_positional("tagName"), py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py tagName = a[2];
                        py attrs = a[3];
                        py a_localname;
                        py a_uri;
                        py aname;
                        py attr;
                        py localname;
                        py node;
                        py prefix;
                        py qname;
                        py uri;
                        py value;
                        py xmlns_attrs;
                        py xmlns_uri;
                        xmlns_uri = S<"http://www.w3.org/2000/xmlns/">();
                        xmlns_attrs = (B<"getattr">())(self, S<"_xmlns_attrs">(), none());
                        if (!xmlns_attrs.is_none())
                        {
                            for (py const& item : iterate{ xmlns_attrs })
                            {
                                auto const items3 = unpack(item, 2);
                                aname = items3[0];
                                value = items3[1];
                                attrs.attr("_attrs").set_item(py_tuple({ xmlns_uri, aname }), value);
                            }
                            self.set_attr("_xmlns_attrs", py_list());
                        }
                        auto const items4 = unpack(name, 2);
                        uri = items4[0];
                        localname = items4[1];
                        if (uri.truthy())
                        {
                            // When using namespaces, the reader may or may not
                            // provide us with the original name. If not, create
                            // *a* valid tagName from the current context.
                            if (tagName.is_none())
                            {
                                prefix = self.attr("_current_context")[uri];
                                if (prefix.truthy())
                                {
                                    tagName = (prefix + (S<":">())) + localname;
                                }
                                else
                                {
                                    tagName = localname;
                                }
                            }
                            if (self.attr("document").truthy())
                            {
                                node = self.attr("document").method("createElementNS", { uri, tagName });
                            }
                            else
                            {
                                node = self.method("buildDocument", { uri, tagName });
                            }
                        }
                        // When the tagname is not prefixed, it just appears as
                        // localname
                        else if (self.attr("document").truthy())
                        {
                            node = self.attr("document").method("createElement", { localname });
                        }
                        else
                        {
                            node = self.method("buildDocument", { none(), localname });
                        }
                        for (py const& item : iterate{ attrs.method("items") })
                        {
                            auto const items6 = unpack(item, 2);
                            aname = items6[0];
                            value = items6[1];
                            auto const items7 = unpack(aname, 2);
                            a_uri = items7[0];
                            a_localname = items7[1];
                            if (a_uri == xmlns_uri)
                            {
                                if (a_localname == (S<"xmlns">()))
                                {
                                    qname = a_localname;
                                }
                                else
                                {
                                    qname = (S<"xmlns:">()) + a_localname;
                                }
                                attr = self.attr("document").method("createAttributeNS", { a_uri, qname });
                                node.method("setAttributeNodeNS", { attr });
                            }
                            else if (a_uri.truthy())
                            {
                                prefix = self.attr("_current_context")[a_uri];
                                if (prefix.truthy())
                                {
                                    qname = (prefix + (S<":">())) + a_localname;
                                }
                                else
                                {
                                    qname = a_localname;
                                }
                                attr = self.attr("document").method("createAttributeNS", { a_uri, qname });
                                node.method("setAttributeNodeNS", { attr });
                            }
                            else
                            {
                                attr = self.attr("document").method("createAttribute", { a_localname });
                                node.method("setAttributeNode", { attr });
                            }
                            attr.set_attr("value", value);
                        }
                        self.attr("lastEvent").set_item(I<1>(), py_list({ py_tuple({ G<"xml.dom.pulldom.START_ELEMENT">(), node }), none() }));
                        self.set_attr("lastEvent", self.attr("lastEvent")[I<1>()]);
                        self.method("push", { node });
                        return none();
                    }) },
                { "endElementNS", w.method("PullDOM", "endElementNS", { py_positional("name"), py_positional("tagName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py tagName = a[2];
                        self.attr("lastEvent").set_item(I<1>(), py_list({ py_tuple({ G<"xml.dom.pulldom.END_ELEMENT">(), self.method("pop") }), none() }));
                        self.set_attr("lastEvent", self.attr("lastEvent")[I<1>()]);
                        return none();
                    }) },
                { "startElement", w.method("PullDOM", "startElement", { py_positional("name"), py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py attrs = a[2];
                        py aname;
                        py attr;
                        py node;
                        py value;
                        if (self.attr("document").truthy())
                        {
                            node = self.attr("document").method("createElement", { name });
                        }
                        else
                        {
                            node = self.method("buildDocument", { none(), name });
                        }
                        for (py const& item : iterate{ attrs.method("items") })
                        {
                            auto const items9 = unpack(item, 2);
                            aname = items9[0];
                            value = items9[1];
                            attr = self.attr("document").method("createAttribute", { aname });
                            attr.set_attr("value", value);
                            node.method("setAttributeNode", { attr });
                        }
                        self.attr("lastEvent").set_item(I<1>(), py_list({ py_tuple({ G<"xml.dom.pulldom.START_ELEMENT">(), node }), none() }));
                        self.set_attr("lastEvent", self.attr("lastEvent")[I<1>()]);
                        self.method("push", { node });
                        return none();
                    }) },
                { "endElement", w.method("PullDOM", "endElement", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        self.attr("lastEvent").set_item(I<1>(), py_list({ py_tuple({ G<"xml.dom.pulldom.END_ELEMENT">(), self.method("pop") }), none() }));
                        self.set_attr("lastEvent", self.attr("lastEvent")[I<1>()]);
                        return none();
                    }) },
                { "comment", w.method("PullDOM", "comment", { py_positional("s") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s = a[1];
                        py event;
                        py node;
                        if (self.attr("document").truthy())
                        {
                            node = self.attr("document").method("createComment", { s });
                            self.attr("lastEvent").set_item(I<1>(), py_list({ py_tuple({ G<"xml.dom.pulldom.COMMENT">(), node }), none() }));
                            self.set_attr("lastEvent", self.attr("lastEvent")[I<1>()]);
                        }
                        else
                        {
                            event = py_list({ py_tuple({ G<"xml.dom.pulldom.COMMENT">(), s }), none() });
                            self.attr("pending_events").method("append", { event });
                        }
                        return none();
                    }) },
                { "processingInstruction", w.method("PullDOM", "processingInstruction", { py_positional("target"), py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py target = a[1];
                        py data = a[2];
                        py event;
                        py node;
                        if (self.attr("document").truthy())
                        {
                            node = self.attr("document").method("createProcessingInstruction", { target, data });
                            self.attr("lastEvent").set_item(I<1>(), py_list({ py_tuple({ G<"xml.dom.pulldom.PROCESSING_INSTRUCTION">(), node }), none() }));
                            self.set_attr("lastEvent", self.attr("lastEvent")[I<1>()]);
                        }
                        else
                        {
                            event = py_list({ py_tuple({ G<"xml.dom.pulldom.PROCESSING_INSTRUCTION">(), target, data }), none() });
                            self.attr("pending_events").method("append", { event });
                        }
                        return none();
                    }) },
                { "ignorableWhitespace", w.method("PullDOM", "ignorableWhitespace", { py_positional("chars") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py chars = a[1];
                        py node;
                        node = self.attr("document").method("createTextNode", { chars });
                        self.attr("lastEvent").set_item(I<1>(), py_list({ py_tuple({ G<"xml.dom.pulldom.IGNORABLE_WHITESPACE">(), node }), none() }));
                        self.set_attr("lastEvent", self.attr("lastEvent")[I<1>()]);
                        return none();
                    }) },
                { "characters", w.method("PullDOM", "characters", { py_positional("chars") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py chars = a[1];
                        py node;
                        node = self.attr("document").method("createTextNode", { chars });
                        self.attr("lastEvent").set_item(I<1>(), py_list({ py_tuple({ G<"xml.dom.pulldom.CHARACTERS">(), node }), none() }));
                        self.set_attr("lastEvent", self.attr("lastEvent")[I<1>()]);
                        return none();
                    }) },
                { "startDocument", w.method("PullDOM", "startDocument", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py xml;
                        if (self.attr("documentFactory").is_none())
                        {
                            xml = module_of("xml");
                            self.set_attr("documentFactory", xml.attr("dom").attr("minidom").attr("Document").attr("implementation"));
                        }
                        return none();
                    }) },
                { "buildDocument", w.method("PullDOM", "buildDocument", { py_positional("uri"), py_positional("tagname") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py uri = a[1];
                        py tagname = a[2];
                        py _;
                        py data;
                        py e;
                        py n;
                        py node;
                        py target;
                        node = self.attr("documentFactory").method("createDocument", { uri, tagname, none() });
                        self.set_attr("document", node);
                        self.attr("lastEvent").set_item(I<1>(), py_list({ py_tuple({ G<"xml.dom.pulldom.START_DOCUMENT">(), node }), none() }));
                        self.set_attr("lastEvent", self.attr("lastEvent")[I<1>()]);
                        self.method("push", { node });
                        // Put everything we have seen so far into the document
                        for (py const& item : iterate{ self.attr("pending_events") })
                        {
                            e = item;
                            if (e[I<0>()][I<0>()] == (G<"xml.dom.pulldom.PROCESSING_INSTRUCTION">()))
                            {
                                auto const items11 = unpack(e[I<0>()], 3);
                                _ = items11[0];
                                target = items11[1];
                                data = items11[2];
                                n = self.attr("document").method("createProcessingInstruction", { target, data });
                                e.set_item(I<0>(), py_tuple({ G<"xml.dom.pulldom.PROCESSING_INSTRUCTION">(), n }));
                            }
                            else if (e[I<0>()][I<0>()] == (G<"xml.dom.pulldom.COMMENT">()))
                            {
                                n = self.attr("document").method("createComment", { e[I<0>()][I<1>()] });
                                e.set_item(I<0>(), py_tuple({ G<"xml.dom.pulldom.COMMENT">(), n }));
                            }
                            else
                            {
                                py_raise((B<"AssertionError">())(S<"Unknown pending event ">(), e[I<0>()][I<0>()]));
                            }
                            self.attr("lastEvent").set_item(I<1>(), e);
                            self.set_attr("lastEvent", e);
                        }
                        self.set_attr("pending_events", none());
                        return node.attr("firstChild");
                    }) },
                { "endDocument", w.method("PullDOM", "endDocument", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("lastEvent").set_item(I<1>(), py_list({ py_tuple({ G<"xml.dom.pulldom.END_DOCUMENT">(), self.attr("document") }), none() }));
                        self.method("pop");
                        return none();
                    }) },
                { "clear", w.method("PullDOM", "clear", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("document", none());
                        return none();
                    }) }, }));

            define("xml.dom.pulldom", "ErrorHandler", w.make_class("ErrorHandler", {  }, {
                { "warning", w.method("ErrorHandler", "warning", { py_positional("exception") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exception = a[1];
                        (B<"print">())(exception);
                        return none();
                    }) },
                { "error", w.method("ErrorHandler", "error", { py_positional("exception") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exception = a[1];
                        py_raise(exception);
                    }) },
                { "fatalError", w.method("ErrorHandler", "fatalError", { py_positional("exception") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exception = a[1];
                        py_raise(exception);
                    }) }, }));

            define("xml.dom.pulldom", "DOMEventStream", w.make_class("DOMEventStream", {  }, {
                { "__init__", w.method("DOMEventStream", "__init__", { py_positional("stream"), py_positional("parser"), py_positional("bufsize") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py stream = a[1];
                        py parser = a[2];
                        py bufsize = a[3];
                        self.set_attr("stream", stream);
                        self.set_attr("parser", parser);
                        self.set_attr("bufsize", bufsize);
                        if (!has_attr(self.attr("parser"), "feed"))
                        {
                            self.set_attr("getEvent", self.attr("_slurp"));
                        }
                        self.method("reset");
                        return none();
                    }) },
                { "reset", w.method("DOMEventStream", "reset", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("pulldom", (G<"xml.dom.pulldom.PullDOM">())());
                        // This content handler relies on namespace support
                        self.attr("parser").method("setFeature", { Mod<"xml.sax.handler">().attr("feature_namespaces"), I<1>() });
                        self.attr("parser").method("setContentHandler", { self.attr("pulldom") });
                        return none();
                    }) },
                { "__next__", w.method("DOMEventStream", "__next__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py rc;
                        rc = self.method("getEvent");
                        if (rc.truthy())
                        {
                            return rc;
                        }
                        py_raise(B<"StopIteration">());
                    }) },
                { "__iter__", w.method("DOMEventStream", "__iter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "expandNode", w.method("DOMEventStream", "expandNode", { py_positional("node") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py node = a[1];
                        py cur_node;
                        py event;
                        py parents;
                        py token;
                        event = self.method("getEvent");
                        parents = py_list({ node });
                        while (event.truthy())
                        {
                            auto const items13 = unpack(event, 2);
                            token = items13[0];
                            cur_node = items13[1];
                            if (cur_node.is(node))
                            {
                                return none();
                            }
                            if (token != (G<"xml.dom.pulldom.END_ELEMENT">()))
                            {
                                parents[I<-1>()].method("appendChild", { cur_node });
                            }
                            if (token == (G<"xml.dom.pulldom.START_ELEMENT">()))
                            {
                                parents.method("append", { cur_node });
                            }
                            else if (token == (G<"xml.dom.pulldom.END_ELEMENT">()))
                            {
                                del_item(parents, I<-1>());
                            }
                            event = self.method("getEvent");
                        }
                        return none();
                    }) },
                { "getEvent", w.method("DOMEventStream", "getEvent", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py buf;
                        py rc;
                        // use IncrementalParser interface, so we get the desired
                        // pull effect
                        if (!self.attr("pulldom").attr("firstEvent")[I<1>()].truthy())
                        {
                            self.attr("pulldom").set_attr("lastEvent", self.attr("pulldom").attr("firstEvent"));
                        }
                        while (!self.attr("pulldom").attr("firstEvent")[I<1>()].truthy())
                        {
                            buf = self.attr("stream").method("read", { self.attr("bufsize") });
                            if (!buf.truthy())
                            {
                                self.attr("parser").method("close");
                                return none();
                            }
                            self.attr("parser").method("feed", { buf });
                        }
                        rc = self.attr("pulldom").attr("firstEvent")[I<1>()][I<0>()];
                        self.attr("pulldom").attr("firstEvent").set_item(I<1>(), self.attr("pulldom").attr("firstEvent")[I<1>()][I<1>()]);
                        return rc;
                    }) },
                { "_slurp", w.method("DOMEventStream", "_slurp", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("parser").method("parse", { self.attr("stream") });
                        self.set_attr("getEvent", self.attr("_emit"));
                        return self.method("_emit");
                    }) },
                { "_emit", w.method("DOMEventStream", "_emit", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py rc;
                        rc = self.attr("pulldom").attr("firstEvent")[I<1>()][I<0>()];
                        self.attr("pulldom").attr("firstEvent").set_item(I<1>(), self.attr("pulldom").attr("firstEvent")[I<1>()][I<1>()]);
                        return rc;
                    }) },
                { "clear", w.method("DOMEventStream", "clear", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("pulldom").method("clear");
                        del_attr(self, "pulldom");
                        self.set_attr("parser", none());
                        self.set_attr("stream", none());
                        return none();
                    }) }, }));

            define("xml.dom.pulldom", "SAX2DOM", w.make_class("SAX2DOM", { G<"xml.dom.pulldom.PullDOM">() }, {
                { "startElementNS", w.method("SAX2DOM", "startElementNS", { py_positional("name"), py_positional("tagName"), py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py tagName = a[2];
                        py attrs = a[3];
                        py curNode;
                        py parentNode;
                        (G<"xml.dom.pulldom.PullDOM">()).method("startElementNS", { self, name, tagName, attrs });
                        curNode = self.attr("elementStack")[I<-1>()];
                        parentNode = self.attr("elementStack")[I<-2>()];
                        parentNode.method("appendChild", { curNode });
                        return none();
                    }) },
                { "startElement", w.method("SAX2DOM", "startElement", { py_positional("name"), py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py attrs = a[2];
                        py curNode;
                        py parentNode;
                        (G<"xml.dom.pulldom.PullDOM">()).method("startElement", { self, name, attrs });
                        curNode = self.attr("elementStack")[I<-1>()];
                        parentNode = self.attr("elementStack")[I<-2>()];
                        parentNode.method("appendChild", { curNode });
                        return none();
                    }) },
                { "processingInstruction", w.method("SAX2DOM", "processingInstruction", { py_positional("target"), py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py target = a[1];
                        py data = a[2];
                        py node;
                        py parentNode;
                        (G<"xml.dom.pulldom.PullDOM">()).method("processingInstruction", { self, target, data });
                        node = self.attr("lastEvent")[I<0>()][I<1>()];
                        parentNode = self.attr("elementStack")[I<-1>()];
                        parentNode.method("appendChild", { node });
                        return none();
                    }) },
                { "ignorableWhitespace", w.method("SAX2DOM", "ignorableWhitespace", { py_positional("chars") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py chars = a[1];
                        py node;
                        py parentNode;
                        (G<"xml.dom.pulldom.PullDOM">()).method("ignorableWhitespace", { self, chars });
                        node = self.attr("lastEvent")[I<0>()][I<1>()];
                        parentNode = self.attr("elementStack")[I<-1>()];
                        parentNode.method("appendChild", { node });
                        return none();
                    }) },
                { "characters", w.method("SAX2DOM", "characters", { py_positional("chars") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py chars = a[1];
                        py node;
                        py parentNode;
                        (G<"xml.dom.pulldom.PullDOM">()).method("characters", { self, chars });
                        node = self.attr("lastEvent")[I<0>()][I<1>()];
                        parentNode = self.attr("elementStack")[I<-1>()];
                        parentNode.method("appendChild", { node });
                        return none();
                    }) }, }));

            define("xml.dom.pulldom", "default_bufsize", power((I<2>()), (I<14>())) - (I<20>()));
            define("xml.dom.pulldom", "parse", w.function("parse", "parse", { py_positional("stream_or_string"), py_positional("parser", none()), py_positional("bufsize", none()) }, [](arguments a) -> py
                {
                    py stream_or_string = a[0];
                    py parser = a[1];
                    py bufsize = a[2];
                    py stream;
                    if (bufsize.is_none())
                    {
                        bufsize = G<"xml.dom.pulldom.default_bufsize">();
                    }
                    if (isinstance(stream_or_string, B<"str">()))
                    {
                        stream = (B<"open">())(stream_or_string, S<"rb">());
                    }
                    else
                    {
                        stream = stream_or_string;
                    }
                    if (!parser.truthy())
                    {
                        parser = (Mod<"xml.sax">().attr("make_parser"))();
                    }
                    return (G<"xml.dom.pulldom.DOMEventStream">())(stream, parser, bufsize);
                }));

            define("xml.dom.pulldom", "parseString", w.function("parseString", "parseString", { py_positional("string"), py_positional("parser", none()) }, [](arguments a) -> py
                {
                    py string = a[0];
                    py parser = a[1];
                    py StringIO;
                    py buf;
                    py bufsize;
                    StringIO = M("io", "StringIO");
                    bufsize = py{ string.size() };
                    buf = StringIO(string);
                    if (!parser.truthy())
                    {
                        parser = (Mod<"xml.sax">().attr("make_parser"))();
                    }
                    return (G<"xml.dom.pulldom.DOMEventStream">())(buf, parser, bufsize);
                }));
        }

        bool xml_dom_pulldom_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_xml_dom_pulldom();
                }
                if (auto const doc = xml_dom_pulldom_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("xml", py{ fn_import("xml") }.release());
                aAttributes.emplace_back("xml", py{ fn_import("xml") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("xml.dom.pulldom", xml_dom_pulldom_module);
    }
}
