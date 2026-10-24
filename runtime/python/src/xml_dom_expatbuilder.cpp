/*
  xml_dom_expatbuilder.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/xml/dom/expatbuilder.py), Copyright (c) 2001-2024 Python
  Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module xml.dom.expatbuilder (CPython 3.13's Lib/xml/dom/expatbuilder.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "xml_dom_expatbuilder_docs.hpp"

namespace neos::python_runtime::detail::xml_dom_expatbuilder_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "xml.dom.expatbuilder.name"))
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
                py_raise("SystemError", std::string{ "xml.dom.expatbuilder: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"xml.dom.expatbuilder.name">()))
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
            if (modules_imported("xml.dom.expatbuilder"))
                return py{ fn_import("xml.dom.expatbuilder") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "xml.dom.expatbuilder" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "xml.dom.expatbuilder", xml_dom_expatbuilder_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- xml.dom.expatbuilder (Lib/xml/dom/expatbuilder.py)

        void make_xml_dom_expatbuilder()
        {
            static module_writer const& w = writer("xml.dom.expatbuilder");
            // Warning!
            // 
            // This module is tightly bound to the implementation details of the
            // minidom DOM and can't be used with other DOM implementations.  This
            // is due, in part, to a lack of appropriate methods in the DOM (there is
            // no way to create Entity and Notation nodes via the DOM Level 2
            // interface), and for performance.  The latter is the cause of some fairly
            // cryptic code.
            // 
            // Performance hacks:
            // 
            // -  .character_data_handler() has an extra case in which continuing
            // data is appended to an existing Text node; this can be a
            // speedup since pyexpat can break up character data into multiple
            // callbacks even though we set the buffer_text attribute on the
            // parser.  This also gives us the advantage that we don't need a
            // separate normalization pass.
            // 
            // -  Determining that a node exists is done using an identity comparison
            // with None rather than a truth test; this avoids searching for and
            // calling any methods on the node object if it exists.  (A rather
            // nice speedup is achieved this way as well!)
            define("xml.dom.expatbuilder", "TEXT_NODE", (Mod<"xml.dom">().attr("Node")).attr("TEXT_NODE"));
            define("xml.dom.expatbuilder", "CDATA_SECTION_NODE", (Mod<"xml.dom">().attr("Node")).attr("CDATA_SECTION_NODE"));
            define("xml.dom.expatbuilder", "DOCUMENT_NODE", (Mod<"xml.dom">().attr("Node")).attr("DOCUMENT_NODE"));
            define("xml.dom.expatbuilder", "FILTER_ACCEPT", (Mod<"xml.dom.xmlbuilder">().attr("DOMBuilderFilter")).attr("FILTER_ACCEPT"));
            define("xml.dom.expatbuilder", "FILTER_REJECT", (Mod<"xml.dom.xmlbuilder">().attr("DOMBuilderFilter")).attr("FILTER_REJECT"));
            define("xml.dom.expatbuilder", "FILTER_SKIP", (Mod<"xml.dom.xmlbuilder">().attr("DOMBuilderFilter")).attr("FILTER_SKIP"));
            define("xml.dom.expatbuilder", "FILTER_INTERRUPT", (Mod<"xml.dom.xmlbuilder">().attr("DOMBuilderFilter")).attr("FILTER_INTERRUPT"));
            define("xml.dom.expatbuilder", "theDOMImplementation", (Mod<"xml.dom.minidom">()).method("getDOMImplementation"));
            // Expat typename -> TypeInfo
            define("xml.dom.expatbuilder", "_typeinfo_map", dict_of({ { S<"CDATA">(), (Mod<"xml.dom.minidom">()).method("TypeInfo", { none(), S<"cdata">() }) }, { S<"ENUM">(), (Mod<"xml.dom.minidom">()).method("TypeInfo", { none(), S<"enumeration">() }) }, { S<"ENTITY">(), (Mod<"xml.dom.minidom">()).method("TypeInfo", { none(), S<"entity">() }) }, { S<"ENTITIES">(), (Mod<"xml.dom.minidom">()).method("TypeInfo", { none(), S<"entities">() }) }, { S<"ID">(), (Mod<"xml.dom.minidom">()).method("TypeInfo", { none(), S<"id">() }) }, { S<"IDREF">(), (Mod<"xml.dom.minidom">()).method("TypeInfo", { none(), S<"idref">() }) }, { S<"IDREFS">(), (Mod<"xml.dom.minidom">()).method("TypeInfo", { none(), S<"idrefs">() }) }, { S<"NMTOKEN">(), (Mod<"xml.dom.minidom">()).method("TypeInfo", { none(), S<"nmtoken">() }) }, { S<"NMTOKENS">(), (Mod<"xml.dom.minidom">()).method("TypeInfo", { none(), S<"nmtokens">() }) } }));
            define("xml.dom.expatbuilder", "ElementInfo", w.make_class("ElementInfo", { B<"object">() }, {
                { "__slots__", py_tuple({ S<"_attr_info">(), S<"_model">(), S<"tagName">() }) },
                { "__init__", w.method("ElementInfo", "__init__", { py_positional("tagName"), py_positional("model", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tagName = a[1];
                        py model = a[2];
                        self.set_attr("tagName", tagName);
                        self.set_attr("_attr_info", py_list());
                        self.set_attr("_model", model);
                        return none();
                    }) },
                { "__getstate__", w.method("ElementInfo", "__getstate__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py_tuple({ self.attr("_attr_info"), self.attr("_model"), self.attr("tagName") });
                    }) },
                { "__setstate__", w.method("ElementInfo", "__setstate__", { py_positional("state") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py state = a[1];
                        auto const items1 = unpack(state, 3);
                        self.set_attr("_attr_info", items1[0]);
                        self.set_attr("_model", items1[1]);
                        self.set_attr("tagName", items1[2]);
                        return none();
                    }) },
                { "getAttributeType", w.method("ElementInfo", "getAttributeType", { py_positional("aname") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py aname = a[1];
                        py info;
                        py t;
                        for (py const& item : iterate{ self.attr("_attr_info") })
                        {
                            info = item;
                            if (info[I<1>()] == aname)
                            {
                                t = info[I<-2>()];
                                if (t[I<0>()] == (S<"(">()))
                                {
                                    return (G<"xml.dom.expatbuilder._typeinfo_map">())[S<"ENUM">()];
                                }
                                else
                                {
                                    return (G<"xml.dom.expatbuilder._typeinfo_map">())[info[I<-2>()]];
                                }
                            }
                        }
                        return Mod<"xml.dom.minidom">().attr("_no_type");
                    }) },
                { "getAttributeTypeNS", w.method("ElementInfo", "getAttributeTypeNS", { py_positional("namespaceURI"), py_positional("localName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespaceURI = a[1];
                        py localName = a[2];
                        return Mod<"xml.dom.minidom">().attr("_no_type");
                    }) },
                { "isElementContent", w.method("ElementInfo", "isElementContent", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py type;
                        if (self.attr("_model").truthy())
                        {
                            type = self.attr("_model")[I<0>()];
                            return boolean(!py_tuple({ (Mod<"xml.parsers.expat">().attr("model")).attr("XML_CTYPE_ANY"), (Mod<"xml.parsers.expat">().attr("model")).attr("XML_CTYPE_MIXED") }).contains(type));
                        }
                        else
                        {
                            return boolean(false);
                        }
                        return none();
                    }) },
                { "isEmpty", w.method("ElementInfo", "isEmpty", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_model").truthy())
                        {
                            return boolean(self.attr("_model")[I<0>()] == (Mod<"xml.parsers.expat">().attr("model")).attr("XML_CTYPE_EMPTY"));
                        }
                        else
                        {
                            return boolean(false);
                        }
                        return none();
                    }) },
                { "isId", w.method("ElementInfo", "isId", { py_positional("aname") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py aname = a[1];
                        py info;
                        for (py const& item : iterate{ self.attr("_attr_info") })
                        {
                            info = item;
                            if (info[I<1>()] == aname)
                            {
                                return boolean(info[I<-2>()] == (S<"ID">()));
                            }
                        }
                        return boolean(false);
                    }) },
                { "isIdNS", w.method("ElementInfo", "isIdNS", { py_positional("euri"), py_positional("ename"), py_positional("auri"), py_positional("aname") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py euri = a[1];
                        py ename = a[2];
                        py auri = a[3];
                        py aname = a[4];
                        return self.method("isId", { py_tuple({ auri, aname }) });
                    }) }, }));

            define("xml.dom.expatbuilder", "_intern", w.function("_intern", "_intern", { py_positional("builder"), py_positional("s") }, [](arguments a) -> py
                {
                    py builder = a[0];
                    py s = a[1];
                    return builder.method("_intern_setdefault", { s, s });
                }));

            define("xml.dom.expatbuilder", "_parse_ns_name", w.function("_parse_ns_name", "_parse_ns_name", { py_positional("builder"), py_positional("name") }, [](arguments a) -> py
                {
                    py builder = a[0];
                    py name = a[1];
                    py intern;
                    py localname;
                    py parts;
                    py prefix;
                    py qname;
                    py uri;
                    if (!name.contains((S<" ">())))
                        py_raise(B<"AssertionError">()());
                    parts = name.method("split", { S<" ">() });
                    intern = builder.attr("_intern_setdefault");
                    if (py{ parts.size() } == (I<3>()))
                    {
                        auto const items4 = unpack(parts, 3);
                        uri = items4[0];
                        localname = items4[1];
                        prefix = items4[2];
                        prefix = intern(prefix, prefix);
                        qname = (S<"%s:%s">()) % py_tuple({ prefix, localname });
                        qname = intern(qname, qname);
                        localname = intern(localname, localname);
                    }
                    else if (py{ parts.size() } == (I<2>()))
                    {
                        auto const items5 = unpack(parts, 2);
                        uri = items5[0];
                        localname = items5[1];
                        prefix = Mod<"xml.dom">().attr("EMPTY_PREFIX");
                        py const value6 = intern(localname, localname);
                        qname = value6;
                        localname = value6;
                    }
                    else
                    {
                        py_raise((B<"ValueError">())((S<"Unsupported syntax: spaces in URIs not supported: %r">()) % name));
                    }
                    return py_tuple({ intern(uri, uri), localname, prefix, qname });
                }));

                // If this ever changes, Namespaces.end_element_handler() needs to
                // be changed to match.
                // 
            define("xml.dom.expatbuilder", "ExpatBuilder", w.make_class("ExpatBuilder", {  }, {
                { "__init__", w.method("ExpatBuilder", "__init__", { py_positional("options", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py options = a[1];
                        if (options.is_none())
                        {
                            options = (Mod<"xml.dom.xmlbuilder">().attr("Options"))();
                        }
                        self.set_attr("_options", options);
                        if (!self.attr("_options").attr("filter").is_none())
                        {
                            self.set_attr("_filter", (G<"xml.dom.expatbuilder.FilterVisibilityController">())(self.attr("_options").attr("filter")));
                        }
                        else
                        {
                            self.set_attr("_filter", none());
                            // This *really* doesn't do anything in this case, so
                            // override it with something fast & minimal.
                            self.set_attr("_finish_start_element", B<"id">());
                        }
                        self.set_attr("_parser", none());
                        self.method("reset");
                        return none();
                    }) },
                { "createParser", w.method("ExpatBuilder", "createParser", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (Mod<"xml.parsers.expat">().attr("ParserCreate"))();
                    }) },
                { "getParser", w.method("ExpatBuilder", "getParser", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("_parser").truthy())
                        {
                            self.set_attr("_parser", self.method("createParser"));
                            self.set_attr("_intern_setdefault", self.attr("_parser").attr("intern").attr("setdefault"));
                            self.attr("_parser").set_attr("buffer_text", boolean(true));
                            self.attr("_parser").set_attr("ordered_attributes", boolean(true));
                            self.attr("_parser").set_attr("specified_attributes", boolean(true));
                            self.method("install", { self.attr("_parser") });
                        }
                        return self.attr("_parser");
                    }) },
                { "reset", w.method("ExpatBuilder", "reset", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("document", (G<"xml.dom.expatbuilder.theDOMImplementation">()).method("createDocument", { Mod<"xml.dom">().attr("EMPTY_NAMESPACE"), none(), none() }));
                        self.set_attr("curNode", self.attr("document"));
                        self.set_attr("_elem_info", self.attr("document").attr("_elem_info"));
                        self.set_attr("_cdata", boolean(false));
                        return none();
                    }) },
                { "install", w.method("ExpatBuilder", "install", { py_positional("parser") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parser = a[1];
                        // This creates circular references!
                        parser.set_attr("StartDoctypeDeclHandler", self.attr("start_doctype_decl_handler"));
                        parser.set_attr("StartElementHandler", self.attr("first_element_handler"));
                        parser.set_attr("EndElementHandler", self.attr("end_element_handler"));
                        parser.set_attr("ProcessingInstructionHandler", self.attr("pi_handler"));
                        if (self.attr("_options").attr("entities").truthy())
                        {
                            parser.set_attr("EntityDeclHandler", self.attr("entity_decl_handler"));
                        }
                        parser.set_attr("NotationDeclHandler", self.attr("notation_decl_handler"));
                        if (self.attr("_options").attr("comments").truthy())
                        {
                            parser.set_attr("CommentHandler", self.attr("comment_handler"));
                        }
                        if (self.attr("_options").attr("cdata_sections").truthy())
                        {
                            parser.set_attr("StartCdataSectionHandler", self.attr("start_cdata_section_handler"));
                            parser.set_attr("EndCdataSectionHandler", self.attr("end_cdata_section_handler"));
                            parser.set_attr("CharacterDataHandler", self.attr("character_data_handler_cdata"));
                        }
                        else
                        {
                            parser.set_attr("CharacterDataHandler", self.attr("character_data_handler"));
                        }
                        parser.set_attr("ExternalEntityRefHandler", self.attr("external_entity_ref_handler"));
                        parser.set_attr("XmlDeclHandler", self.attr("xml_decl_handler"));
                        parser.set_attr("ElementDeclHandler", self.attr("element_decl_handler"));
                        parser.set_attr("AttlistDeclHandler", self.attr("attlist_decl_handler"));
                        return none();
                    }) },
                { "parseFile", w.method("ExpatBuilder", "parseFile", { py_positional("file") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        py buffer;
                        py doc;
                        py first_buffer;
                        py parser;
                        parser = self.method("getParser");
                        first_buffer = boolean(true);
                        try
                        {
                            while (([&]() -> py { buffer = file.method("read", { (I<16>()) * (I<1024>()) }); return buffer; }()).truthy())
                            {
                                parser.method("Parse", { buffer, boolean(false) });
                                if ((first_buffer.truthy() && self.attr("document").attr("documentElement").truthy()))
                                {
                                    self.method("_setup_subset", { buffer });
                                }
                                first_buffer = boolean(false);
                            }
                            parser.method("Parse", { py{ make_bytes(std::string{ "", 0 }) }, boolean(true) });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler7;
                            if (matches(handler7.exception, G<"xml.dom.expatbuilder.ParseEscape">()))
                            {
                            }
                            else
                                py_reraise(handler7.exception);
                        }
                        doc = self.attr("document");
                        self.method("reset");
                        self.set_attr("_parser", none());
                        return doc;
                    }) },
                { "parseString", w.method("ExpatBuilder", "parseString", { py_positional("string") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py string = a[1];
                        py doc;
                        py parser;
                        parser = self.method("getParser");
                        try
                        {
                            parser.method("Parse", { string, boolean(true) });
                            self.method("_setup_subset", { string });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler9;
                            if (matches(handler9.exception, G<"xml.dom.expatbuilder.ParseEscape">()))
                            {
                            }
                            else
                                py_reraise(handler9.exception);
                        }
                        doc = self.attr("document");
                        self.method("reset");
                        self.set_attr("_parser", none());
                        return doc;
                    }) },
                { "_setup_subset", w.method("ExpatBuilder", "_setup_subset", { py_positional("buffer") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py buffer = a[1];
                        py extractor;
                        py subset;
                        if (self.attr("document").attr("doctype").truthy())
                        {
                            extractor = (G<"xml.dom.expatbuilder.InternalSubsetExtractor">())();
                            extractor.method("parseString", { buffer });
                            subset = extractor.method("getSubset");
                            self.attr("document").attr("doctype").set_attr("internalSubset", subset);
                        }
                        return none();
                    }) },
                { "start_doctype_decl_handler", w.method("ExpatBuilder", "start_doctype_decl_handler", { py_positional("doctypeName"), py_positional("systemId"), py_positional("publicId"), py_positional("has_internal_subset") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py doctypeName = a[1];
                        py systemId = a[2];
                        py publicId = a[3];
                        py has_internal_subset = a[4];
                        py doctype;
                        doctype = self.attr("document").attr("implementation").method("createDocumentType", { doctypeName, publicId, systemId });
                        doctype.set_attr("ownerDocument", self.attr("document"));
                        (Mod<"xml.dom.minidom">().attr("_append_child"))(self.attr("document"), doctype);
                        self.attr("document").set_attr("doctype", doctype);
                        if ((self.attr("_filter").truthy() && (self.attr("_filter").method("acceptNode", { doctype }) == (G<"xml.dom.expatbuilder.FILTER_REJECT">()))))
                        {
                            self.attr("document").set_attr("doctype", none());
                            del_item(self.attr("document").attr("childNodes"), I<-1>());
                            doctype = none();
                            self.attr("_parser").set_attr("EntityDeclHandler", none());
                            self.attr("_parser").set_attr("NotationDeclHandler", none());
                        }
                        if (has_internal_subset.truthy())
                        {
                            if (!doctype.is_none())
                            {
                                doctype.attr("entities").set_attr("_seq", py_list());
                                doctype.attr("notations").set_attr("_seq", py_list());
                            }
                            self.attr("_parser").set_attr("CommentHandler", none());
                            self.attr("_parser").set_attr("ProcessingInstructionHandler", none());
                            self.attr("_parser").set_attr("EndDoctypeDeclHandler", self.attr("end_doctype_decl_handler"));
                        }
                        return none();
                    }) },
                { "end_doctype_decl_handler", w.method("ExpatBuilder", "end_doctype_decl_handler", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_options").attr("comments").truthy())
                        {
                            self.attr("_parser").set_attr("CommentHandler", self.attr("comment_handler"));
                        }
                        self.attr("_parser").set_attr("ProcessingInstructionHandler", self.attr("pi_handler"));
                        if (!([&]() -> py { py const t = self.attr("_elem_info"); return t.truthy() ? t : self.attr("_filter"); }()).truthy())
                        {
                            self.set_attr("_finish_end_element", B<"id">());
                        }
                        return none();
                    }) },
                { "pi_handler", w.method("ExpatBuilder", "pi_handler", { py_positional("target"), py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py target = a[1];
                        py data = a[2];
                        py node;
                        node = self.attr("document").method("createProcessingInstruction", { target, data });
                        (Mod<"xml.dom.minidom">().attr("_append_child"))(self.attr("curNode"), node);
                        if ((self.attr("_filter").truthy() && (self.attr("_filter").method("acceptNode", { node }) == (G<"xml.dom.expatbuilder.FILTER_REJECT">()))))
                        {
                            self.attr("curNode").method("removeChild", { node });
                        }
                        return none();
                    }) },
                { "character_data_handler_cdata", w.method("ExpatBuilder", "character_data_handler_cdata", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py childNodes;
                        py node;
                        py value;
                        childNodes = self.attr("curNode").attr("childNodes");
                        if (self.attr("_cdata").truthy())
                        {
                            if ((self.attr("_cdata_continue").truthy() && (childNodes[I<-1>()].attr("nodeType") == (G<"xml.dom.expatbuilder.CDATA_SECTION_NODE">()))))
                            {
                                childNodes[I<-1>()].method("appendData", { data });
                                return none();
                            }
                            node = self.attr("document").method("createCDATASection", { data });
                            self.set_attr("_cdata_continue", boolean(true));
                        }
                        else if ((childNodes.truthy() && (childNodes[I<-1>()].attr("nodeType") == (G<"xml.dom.expatbuilder.TEXT_NODE">()))))
                        {
                            node = childNodes[I<-1>()];
                            value = node.attr("data") + data;
                            node.set_attr("data", value);
                            return none();
                        }
                        else
                        {
                            node = (Mod<"xml.dom.minidom">().attr("Text"))();
                            node.set_attr("data", data);
                            node.set_attr("ownerDocument", self.attr("document"));
                        }
                        (Mod<"xml.dom.minidom">().attr("_append_child"))(self.attr("curNode"), node);
                        return none();
                    }) },
                { "character_data_handler", w.method("ExpatBuilder", "character_data_handler", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py childNodes;
                        py node;
                        childNodes = self.attr("curNode").attr("childNodes");
                        if ((childNodes.truthy() && (childNodes[I<-1>()].attr("nodeType") == (G<"xml.dom.expatbuilder.TEXT_NODE">()))))
                        {
                            node = childNodes[I<-1>()];
                            node.set_attr("data", node.attr("data") + data);
                            return none();
                        }
                        node = (Mod<"xml.dom.minidom">().attr("Text"))();
                        node.set_attr("data", node.attr("data") + data);
                        node.set_attr("ownerDocument", self.attr("document"));
                        (Mod<"xml.dom.minidom">().attr("_append_child"))(self.attr("curNode"), node);
                        return none();
                    }) },
                { "entity_decl_handler", w.method("ExpatBuilder", "entity_decl_handler", { py_positional("entityName"), py_positional("is_parameter_entity"), py_positional("value"), py_positional("base"), py_positional("systemId"), py_positional("publicId"), py_positional("notationName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py entityName = a[1];
                        py is_parameter_entity = a[2];
                        py value = a[3];
                        py base = a[4];
                        py systemId = a[5];
                        py publicId = a[6];
                        py notationName = a[7];
                        py child;
                        py node;
                        if (is_parameter_entity.truthy())
                        {
                            // we don't care about parameter entities for the DOM
                            return none();
                        }
                        if (!self.attr("_options").attr("entities").truthy())
                        {
                            return none();
                        }
                        node = self.attr("document").method("_create_entity", { entityName, publicId, systemId, notationName });
                        if (!value.is_none())
                        {
                            // internal entity
                            // node *should* be readonly, but we'll cheat
                            child = self.attr("document").method("createTextNode", { value });
                            node.attr("childNodes").method("append", { child });
                        }
                        self.attr("document").attr("doctype").attr("entities").attr("_seq").method("append", { node });
                        if ((self.attr("_filter").truthy() && (self.attr("_filter").method("acceptNode", { node }) == (G<"xml.dom.expatbuilder.FILTER_REJECT">()))))
                        {
                            del_item(self.attr("document").attr("doctype").attr("entities").attr("_seq"), I<-1>());
                        }
                        return none();
                    }) },
                { "notation_decl_handler", w.method("ExpatBuilder", "notation_decl_handler", { py_positional("notationName"), py_positional("base"), py_positional("systemId"), py_positional("publicId") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py notationName = a[1];
                        py base = a[2];
                        py systemId = a[3];
                        py publicId = a[4];
                        py node;
                        node = self.attr("document").method("_create_notation", { notationName, publicId, systemId });
                        self.attr("document").attr("doctype").attr("notations").attr("_seq").method("append", { node });
                        if ((self.attr("_filter").truthy() && (self.attr("_filter").method("acceptNode", { node }) == (G<"xml.dom.expatbuilder.FILTER_ACCEPT">()))))
                        {
                            del_item(self.attr("document").attr("doctype").attr("notations").attr("_seq"), I<-1>());
                        }
                        return none();
                    }) },
                { "comment_handler", w.method("ExpatBuilder", "comment_handler", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py node;
                        node = self.attr("document").method("createComment", { data });
                        (Mod<"xml.dom.minidom">().attr("_append_child"))(self.attr("curNode"), node);
                        if ((self.attr("_filter").truthy() && (self.attr("_filter").method("acceptNode", { node }) == (G<"xml.dom.expatbuilder.FILTER_REJECT">()))))
                        {
                            self.attr("curNode").method("removeChild", { node });
                        }
                        return none();
                    }) },
                { "start_cdata_section_handler", w.method("ExpatBuilder", "start_cdata_section_handler", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_cdata", boolean(true));
                        self.set_attr("_cdata_continue", boolean(false));
                        return none();
                    }) },
                { "end_cdata_section_handler", w.method("ExpatBuilder", "end_cdata_section_handler", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_cdata", boolean(false));
                        self.set_attr("_cdata_continue", boolean(false));
                        return none();
                    }) },
                { "external_entity_ref_handler", w.method("ExpatBuilder", "external_entity_ref_handler", { py_positional("context"), py_positional("base"), py_positional("systemId"), py_positional("publicId") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py context = a[1];
                        py base = a[2];
                        py systemId = a[3];
                        py publicId = a[4];
                        return I<1>();
                    }) },
                { "first_element_handler", w.method("ExpatBuilder", "first_element_handler", { py_positional("name"), py_positional("attributes") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py attributes = a[2];
                        if ((self.attr("_filter").is_none() && (!self.attr("_elem_info").truthy())))
                        {
                            self.set_attr("_finish_end_element", B<"id">());
                        }
                        self.method("getParser").set_attr("StartElementHandler", self.attr("start_element_handler"));
                        self.method("start_element_handler", { name, attributes });
                        return none();
                    }) },
                { "start_element_handler", w.method("ExpatBuilder", "start_element_handler", { py_positional("name"), py_positional("attributes") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py attributes = a[2];
                        py a_;
                        py i;
                        py node;
                        py value;
                        node = self.attr("document").method("createElement", { name });
                        (Mod<"xml.dom.minidom">().attr("_append_child"))(self.attr("curNode"), node);
                        self.set_attr("curNode", node);
                        if (attributes.truthy())
                        {
                            for (py const& item : iterate{ (B<"range">())(I<0>(), py{ attributes.size() }, I<2>()) })
                            {
                                i = item;
                                a_ = (Mod<"xml.dom.minidom">().attr("Attr"))(attributes[i], Mod<"xml.dom">().attr("EMPTY_NAMESPACE"), none(), Mod<"xml.dom">().attr("EMPTY_PREFIX"));
                                value = attributes[i + (I<1>())];
                                a_.set_attr("value", value);
                                a_.set_attr("ownerDocument", self.attr("document"));
                                (Mod<"xml.dom.minidom">().attr("_set_attribute_node"))(node, a_);
                            }
                        }
                        if (!node.is(self.attr("document").attr("documentElement")))
                        {
                            self.method("_finish_start_element", { node });
                        }
                        return none();
                    }) },
                { "_finish_start_element", w.method("ExpatBuilder", "_finish_start_element", { py_positional("node") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py node = a[1];
                        py filt;
                        if (self.attr("_filter").truthy())
                        {
                            // To be general, we'd have to call isSameNode(), but this
                            // is sufficient for minidom:
                            if (node.is(self.attr("document").attr("documentElement")))
                            {
                                return none();
                            }
                            filt = self.attr("_filter").method("startContainer", { node });
                            if (filt == (G<"xml.dom.expatbuilder.FILTER_REJECT">()))
                            {
                                // ignore this node & all descendents
                                (G<"xml.dom.expatbuilder.Rejecter">())(self);
                            }
                            else if (filt == (G<"xml.dom.expatbuilder.FILTER_SKIP">()))
                            {
                                // ignore this node, but make it's children become
                                // children of the parent node
                                (G<"xml.dom.expatbuilder.Skipper">())(self);
                            }
                            else
                            {
                                return none();
                            }
                            self.set_attr("curNode", node.attr("parentNode"));
                            node.attr("parentNode").method("removeChild", { node });
                            node.method("unlink");
                        }
                        return none();
                    }) },
                { "end_element_handler", w.method("ExpatBuilder", "end_element_handler", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py curNode;
                        curNode = self.attr("curNode");
                        self.set_attr("curNode", curNode.attr("parentNode"));
                        self.method("_finish_end_element", { curNode });
                        return none();
                    }) },
                { "_finish_end_element", w.method("ExpatBuilder", "_finish_end_element", { py_positional("curNode") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py curNode = a[1];
                        py info;
                        info = self.attr("_elem_info").method("get", { curNode.attr("tagName") });
                        if (info.truthy())
                        {
                            self.method("_handle_white_text_nodes", { curNode, info });
                        }
                        if (self.attr("_filter").truthy())
                        {
                            if (curNode.is(self.attr("document").attr("documentElement")))
                            {
                                return none();
                            }
                            if (self.attr("_filter").method("acceptNode", { curNode }) == (G<"xml.dom.expatbuilder.FILTER_REJECT">()))
                            {
                                self.attr("curNode").method("removeChild", { curNode });
                                curNode.method("unlink");
                            }
                        }
                        return none();
                    }) },
                { "_handle_white_text_nodes", w.method("ExpatBuilder", "_handle_white_text_nodes", { py_positional("node"), py_positional("info") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py node = a[1];
                        py info = a[2];
                        py L;
                        py child;
                        if ((self.attr("_options").attr("whitespace_in_element_content").truthy() || (!info.method("isElementContent").truthy())))
                        {
                            return none();
                        }
                        // We have element type information and should remove ignorable
                        // whitespace; identify for text nodes which contain only
                        // whitespace.
                        L = py_list();
                        for (py const& item : iterate{ node.attr("childNodes") })
                        {
                            child = item;
                            if (((child.attr("nodeType") == (G<"xml.dom.expatbuilder.TEXT_NODE">())) && (!child.attr("data").method("strip", { Mod<"xml.dom.minidom">().attr("_XML_WHITESPACE") }).truthy())))
                            {
                                L.method("append", { child });
                            }
                        }
                        // Remove ignorable whitespace from the tree.
                        for (py const& item : iterate{ L })
                        {
                            child = item;
                            node.method("removeChild", { child });
                        }
                        return none();
                    }) },
                { "element_decl_handler", w.method("ExpatBuilder", "element_decl_handler", { py_positional("name"), py_positional("model") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py model = a[2];
                        py info;
                        info = self.attr("_elem_info").method("get", { name });
                        if (info.is_none())
                        {
                            self.attr("_elem_info").set_item(name, (G<"xml.dom.expatbuilder.ElementInfo">())(name, model));
                        }
                        else
                        {
                            if (!info.attr("_model").is_none())
                                py_raise(B<"AssertionError">()());
                            info.set_attr("_model", model);
                        }
                        return none();
                    }) },
                { "attlist_decl_handler", w.method("ExpatBuilder", "attlist_decl_handler", { py_positional("elem"), py_positional("name"), py_positional("type"), py_positional("default"), py_positional("required") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py elem = a[1];
                        py name = a[2];
                        py type = a[3];
                        py default_ = a[4];
                        py required = a[5];
                        py info;
                        info = self.attr("_elem_info").method("get", { elem });
                        if (info.is_none())
                        {
                            info = (G<"xml.dom.expatbuilder.ElementInfo">())(elem);
                            self.attr("_elem_info").set_item(elem, info);
                        }
                        info.attr("_attr_info").method("append", { py_list({ none(), name, none(), none(), default_, I<0>(), type, required }) });
                        return none();
                    }) },
                { "xml_decl_handler", w.method("ExpatBuilder", "xml_decl_handler", { py_positional("version"), py_positional("encoding"), py_positional("standalone") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py version = a[1];
                        py encoding = a[2];
                        py standalone = a[3];
                        self.attr("document").set_attr("version", version);
                        self.attr("document").set_attr("encoding", encoding);
                        // This is still a little ugly, thanks to the pyexpat API. ;-(
                        if (standalone >= (I<0>()))
                        {
                            if (standalone.truthy())
                            {
                                self.attr("document").set_attr("standalone", boolean(true));
                            }
                            else
                            {
                                self.attr("document").set_attr("standalone", boolean(false));
                            }
                        }
                        return none();
                    }) }, }));

            // Don't include FILTER_INTERRUPT, since that's checked separately
            // where allowed.
            define("xml.dom.expatbuilder", "_ALLOWED_FILTER_RETURNS", py_tuple({ G<"xml.dom.expatbuilder.FILTER_ACCEPT">(), G<"xml.dom.expatbuilder.FILTER_REJECT">(), G<"xml.dom.expatbuilder.FILTER_SKIP">() }));
            define("xml.dom.expatbuilder", "FilterVisibilityController", w.make_class("FilterVisibilityController", { B<"object">() }, {
                { "__slots__", py_tuple({ S<"filter">() }) },
                { "__init__", w.method("FilterVisibilityController", "__init__", { py_positional("filter") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filter = a[1];
                        self.set_attr("filter", filter);
                        return none();
                    }) },
                { "startContainer", w.method("FilterVisibilityController", "startContainer", { py_positional("node") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py node = a[1];
                        py mask;
                        py val;
                        mask = self.attr("_nodetype_mask")[node.attr("nodeType")];
                        if ((self.attr("filter").attr("whatToShow") & mask).truthy())
                        {
                            val = self.attr("filter").method("startContainer", { node });
                            if (val == (G<"xml.dom.expatbuilder.FILTER_INTERRUPT">()))
                            {
                                py_raise(G<"xml.dom.expatbuilder.ParseEscape">());
                            }
                            if (!(G<"xml.dom.expatbuilder._ALLOWED_FILTER_RETURNS">()).contains(val))
                            {
                                py_raise((B<"ValueError">())((S<"startContainer() returned illegal value: ">()) + S(val.repr())));
                            }
                            return val;
                        }
                        else
                        {
                            return G<"xml.dom.expatbuilder.FILTER_ACCEPT">();
                        }
                        return none();
                    }) },
                { "acceptNode", w.method("FilterVisibilityController", "acceptNode", { py_positional("node") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py node = a[1];
                        py child;
                        py mask;
                        py parent;
                        py val;
                        mask = self.attr("_nodetype_mask")[node.attr("nodeType")];
                        if ((self.attr("filter").attr("whatToShow") & mask).truthy())
                        {
                            val = self.attr("filter").method("acceptNode", { node });
                            if (val == (G<"xml.dom.expatbuilder.FILTER_INTERRUPT">()))
                            {
                                py_raise(G<"xml.dom.expatbuilder.ParseEscape">());
                            }
                            if (val == (G<"xml.dom.expatbuilder.FILTER_SKIP">()))
                            {
                                // move all child nodes to the parent, and remove this node
                                parent = node.attr("parentNode");
                                for (py const& item : iterate{ node.attr("childNodes")[B<"slice">()(none(), none(), none())] })
                                {
                                    child = item;
                                    parent.method("appendChild", { child });
                                }
                                // node is handled by the caller
                                return G<"xml.dom.expatbuilder.FILTER_REJECT">();
                            }
                            if (!(G<"xml.dom.expatbuilder._ALLOWED_FILTER_RETURNS">()).contains(val))
                            {
                                py_raise((B<"ValueError">())((S<"acceptNode() returned illegal value: ">()) + S(val.repr())));
                            }
                            return val;
                        }
                        else
                        {
                            return G<"xml.dom.expatbuilder.FILTER_ACCEPT">();
                        }
                        return none();
                    }) },
                { "_nodetype_mask", dict_of({ { (Mod<"xml.dom">().attr("Node")).attr("ELEMENT_NODE"), (Mod<"xml.dom.NodeFilter">().attr("NodeFilter")).attr("SHOW_ELEMENT") }, { (Mod<"xml.dom">().attr("Node")).attr("ATTRIBUTE_NODE"), (Mod<"xml.dom.NodeFilter">().attr("NodeFilter")).attr("SHOW_ATTRIBUTE") }, { (Mod<"xml.dom">().attr("Node")).attr("TEXT_NODE"), (Mod<"xml.dom.NodeFilter">().attr("NodeFilter")).attr("SHOW_TEXT") }, { (Mod<"xml.dom">().attr("Node")).attr("CDATA_SECTION_NODE"), (Mod<"xml.dom.NodeFilter">().attr("NodeFilter")).attr("SHOW_CDATA_SECTION") }, { (Mod<"xml.dom">().attr("Node")).attr("ENTITY_REFERENCE_NODE"), (Mod<"xml.dom.NodeFilter">().attr("NodeFilter")).attr("SHOW_ENTITY_REFERENCE") }, { (Mod<"xml.dom">().attr("Node")).attr("ENTITY_NODE"), (Mod<"xml.dom.NodeFilter">().attr("NodeFilter")).attr("SHOW_ENTITY") }, { (Mod<"xml.dom">().attr("Node")).attr("PROCESSING_INSTRUCTION_NODE"), (Mod<"xml.dom.NodeFilter">().attr("NodeFilter")).attr("SHOW_PROCESSING_INSTRUCTION") }, { (Mod<"xml.dom">().attr("Node")).attr("COMMENT_NODE"), (Mod<"xml.dom.NodeFilter">().attr("NodeFilter")).attr("SHOW_COMMENT") }, { (Mod<"xml.dom">().attr("Node")).attr("DOCUMENT_NODE"), (Mod<"xml.dom.NodeFilter">().attr("NodeFilter")).attr("SHOW_DOCUMENT") }, { (Mod<"xml.dom">().attr("Node")).attr("DOCUMENT_TYPE_NODE"), (Mod<"xml.dom.NodeFilter">().attr("NodeFilter")).attr("SHOW_DOCUMENT_TYPE") }, { (Mod<"xml.dom">().attr("Node")).attr("DOCUMENT_FRAGMENT_NODE"), (Mod<"xml.dom.NodeFilter">().attr("NodeFilter")).attr("SHOW_DOCUMENT_FRAGMENT") }, { (Mod<"xml.dom">().attr("Node")).attr("NOTATION_NODE"), (Mod<"xml.dom.NodeFilter">().attr("NodeFilter")).attr("SHOW_NOTATION") } }) }, }));

            define("xml.dom.expatbuilder", "FilterCrutch", w.make_class("FilterCrutch", { B<"object">() }, {
                { "__slots__", py_tuple({ S<"_builder">(), S<"_level">(), S<"_old_start">(), S<"_old_end">() }) },
                { "__init__", w.method("FilterCrutch", "__init__", { py_positional("builder") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py builder = a[1];
                        py parser;
                        self.set_attr("_level", I<0>());
                        self.set_attr("_builder", builder);
                        parser = builder.attr("_parser");
                        self.set_attr("_old_start", parser.attr("StartElementHandler"));
                        self.set_attr("_old_end", parser.attr("EndElementHandler"));
                        parser.set_attr("StartElementHandler", self.attr("start_element_handler"));
                        parser.set_attr("EndElementHandler", self.attr("end_element_handler"));
                        return none();
                    }) }, }));

            define("xml.dom.expatbuilder", "Rejecter", w.make_class("Rejecter", { G<"xml.dom.expatbuilder.FilterCrutch">() }, {
                { "__slots__", py_tuple() },
                { "__init__", w.method("Rejecter", "__init__", { py_positional("builder") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py builder = a[1];
                        py name;
                        py parser;
                        (G<"xml.dom.expatbuilder.FilterCrutch">()).method("__init__", { self, builder });
                        parser = builder.attr("_parser");
                        for (py const& item : iterate{ py_tuple({ S<"ProcessingInstructionHandler">(), S<"CommentHandler">(), S<"CharacterDataHandler">(), S<"StartCdataSectionHandler">(), S<"EndCdataSectionHandler">(), S<"ExternalEntityRefHandler">() }) })
                        {
                            name = item;
                            (B<"setattr">())(parser, name, none());
                        }
                        return none();
                    }) },
                { "start_element_handler", w.method("Rejecter", "start_element_handler", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        self.set_attr("_level", self.attr("_level") + (I<1>()));
                        return none();
                    }, true) },
                { "end_element_handler", w.method("Rejecter", "end_element_handler", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py parser;
                        if (self.attr("_level") == (I<0>()))
                        {
                            // restore the old handlers
                            parser = self.attr("_builder").attr("_parser");
                            self.attr("_builder").method("install", { parser });
                            parser.set_attr("StartElementHandler", self.attr("_old_start"));
                            parser.set_attr("EndElementHandler", self.attr("_old_end"));
                        }
                        else
                        {
                            self.set_attr("_level", self.attr("_level") - (I<1>()));
                        }
                        return none();
                    }, true) }, }));

            define("xml.dom.expatbuilder", "Skipper", w.make_class("Skipper", { G<"xml.dom.expatbuilder.FilterCrutch">() }, {
                { "__slots__", py_tuple() },
                { "start_element_handler", w.method("Skipper", "start_element_handler", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py node;
                        node = self.attr("_builder").attr("curNode");
                        call_ex(self.attr("_old_start"), {  }, args, py{});
                        if (!self.attr("_builder").attr("curNode").is(node))
                        {
                            self.set_attr("_level", self.attr("_level") + (I<1>()));
                        }
                        return none();
                    }, true) },
                { "end_element_handler", w.method("Skipper", "end_element_handler", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        if (self.attr("_level") == (I<0>()))
                        {
                            // We're popping back out of the node we're skipping, so we
                            // shouldn't need to do anything but reset the handlers.
                            self.attr("_builder").attr("_parser").set_attr("StartElementHandler", self.attr("_old_start"));
                            self.attr("_builder").attr("_parser").set_attr("EndElementHandler", self.attr("_old_end"));
                            self.set_attr("_builder", none());
                        }
                        else
                        {
                            self.set_attr("_level", self.attr("_level") - (I<1>()));
                            call_ex(self.attr("_old_end"), {  }, args, py{});
                        }
                        return none();
                    }, true) }, }));

            // framework document used by the fragment builder.
            // Takes a string for the doctype, subset string, and namespace attrs string.
            define("xml.dom.expatbuilder", "_FRAGMENT_BUILDER_INTERNAL_SYSTEM_ID", S<"http://xml.python.org/entities/fragment-builder/internal">());
            define("xml.dom.expatbuilder", "_FRAGMENT_BUILDER_TEMPLATE", (S<"<!DOCTYPE wrapper\n  %%s [\n  <!ENTITY fragment-builder-internal\n    SYSTEM \"%s\">\n%%s\n]>\n<wrapper %%s\n>&fragment-builder-internal;</wrapper>">()) % (G<"xml.dom.expatbuilder._FRAGMENT_BUILDER_INTERNAL_SYSTEM_ID">()));
            define("xml.dom.expatbuilder", "FragmentBuilder", w.make_class("FragmentBuilder", { G<"xml.dom.expatbuilder.ExpatBuilder">() }, {
                { "__init__", w.method("FragmentBuilder", "__init__", { py_positional("context"), py_positional("options", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py context = a[1];
                        py options = a[2];
                        if (context.attr("nodeType") == (G<"xml.dom.expatbuilder.DOCUMENT_NODE">()))
                        {
                            self.set_attr("originalDocument", context);
                            self.set_attr("context", context);
                        }
                        else
                        {
                            self.set_attr("originalDocument", context.attr("ownerDocument"));
                            self.set_attr("context", context);
                        }
                        (G<"xml.dom.expatbuilder.ExpatBuilder">()).method("__init__", { self, options });
                        return none();
                    }) },
                { "reset", w.method("FragmentBuilder", "reset", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        (G<"xml.dom.expatbuilder.ExpatBuilder">()).method("reset", { self });
                        self.set_attr("fragment", none());
                        return none();
                    }) },
                { "parseFile", w.method("FragmentBuilder", "parseFile", { py_positional("file") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        return self.method("parseString", { file.method("read") });
                    }) },
                { "parseString", w.method("FragmentBuilder", "parseString", { py_positional("string") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py string = a[1];
                        py doctype;
                        py document;
                        py fragment;
                        py ident;
                        py nsattrs;
                        py parser;
                        py subset;
                        self.set_attr("_source", string);
                        parser = self.method("getParser");
                        doctype = self.attr("originalDocument").attr("doctype");
                        ident = S<"">();
                        if (doctype.truthy())
                        {
                            subset = [&]() -> py { py const t = doctype.attr("internalSubset"); return t.truthy() ? t : self.method("_getDeclarations"); }();
                            if (doctype.attr("publicId").truthy())
                            {
                                ident = (S<"PUBLIC \"%s\" \"%s\"">()) % py_tuple({ doctype.attr("publicId"), doctype.attr("systemId") });
                            }
                            else if (doctype.attr("systemId").truthy())
                            {
                                ident = (S<"SYSTEM \"%s\"">()) % doctype.attr("systemId");
                            }
                        }
                        else
                        {
                            subset = S<"">();
                        }
                        nsattrs = self.method("_getNSattrs");
                        document = (G<"xml.dom.expatbuilder._FRAGMENT_BUILDER_TEMPLATE">()) % py_tuple({ ident, subset, nsattrs });
                        try
                        {
                            parser.method("Parse", { document, boolean(true) });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler15;
                            {
                                self.method("reset");
                                py_reraise(handler15.exception);
                            }
                        }
                        fragment = self.attr("fragment");
                        self.method("reset");
                        // #         self._parser = None
                        return fragment;
                    }) },
                { "_getDeclarations", w.method("FragmentBuilder", "_getDeclarations", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py doctype;
                        py entity;
                        py i;
                        py notation;
                        py s;
                        doctype = self.attr("context").attr("ownerDocument").attr("doctype");
                        s = S<"">();
                        if (doctype.truthy())
                        {
                            for (py const& item : iterate{ (B<"range">())(doctype.attr("notations").attr("length")) })
                            {
                                i = item;
                                notation = doctype.attr("notations").method("item", { i });
                                if (s.truthy())
                                {
                                    s = s + (S<"\n  ">());
                                }
                                s = (S<"%s<!NOTATION %s">()) % py_tuple({ s, notation.attr("nodeName") });
                                if (notation.attr("publicId").truthy())
                                {
                                    s = (S<"%s PUBLIC \"%s\"\n             \"%s\">">()) % py_tuple({ s, notation.attr("publicId"), notation.attr("systemId") });
                                }
                                else
                                {
                                    s = (S<"%s SYSTEM \"%s\">">()) % py_tuple({ s, notation.attr("systemId") });
                                }
                            }
                            for (py const& item : iterate{ (B<"range">())(doctype.attr("entities").attr("length")) })
                            {
                                i = item;
                                entity = doctype.attr("entities").method("item", { i });
                                if (s.truthy())
                                {
                                    s = s + (S<"\n  ">());
                                }
                                s = (S<"%s<!ENTITY %s">()) % py_tuple({ s, entity.attr("nodeName") });
                                if (entity.attr("publicId").truthy())
                                {
                                    s = (S<"%s PUBLIC \"%s\"\n             \"%s\"">()) % py_tuple({ s, entity.attr("publicId"), entity.attr("systemId") });
                                }
                                else if (entity.attr("systemId").truthy())
                                {
                                    s = (S<"%s SYSTEM \"%s\"">()) % py_tuple({ s, entity.attr("systemId") });
                                }
                                else
                                {
                                    s = (S<"%s \"%s\"">()) % py_tuple({ s, entity.attr("firstChild").attr("data") });
                                }
                                if (entity.attr("notationName").truthy())
                                {
                                    s = (S<"%s NOTATION %s">()) % py_tuple({ s, entity.attr("notationName") });
                                }
                                s = s + (S<">">());
                            }
                        }
                        return s;
                    }) },
                { "_getNSattrs", w.method("FragmentBuilder", "_getNSattrs", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return S<"">();
                    }) },
                { "external_entity_ref_handler", w.method("FragmentBuilder", "external_entity_ref_handler", { py_positional("context"), py_positional("base"), py_positional("systemId"), py_positional("publicId") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py context = a[1];
                        py base = a[2];
                        py systemId = a[3];
                        py publicId = a[4];
                        py old_cur_node;
                        py old_document;
                        py parser;
                        if (systemId == (G<"xml.dom.expatbuilder._FRAGMENT_BUILDER_INTERNAL_SYSTEM_ID">()))
                        {
                            // this entref is the one that we made to put the subtree
                            // in; all of our given input is parsed in here.
                            old_document = self.attr("document");
                            old_cur_node = self.attr("curNode");
                            parser = self.attr("_parser").method("ExternalEntityParserCreate", { context });
                            // put the real document back, parse into the fragment to return
                            self.set_attr("document", self.attr("originalDocument"));
                            self.set_attr("fragment", self.attr("document").method("createDocumentFragment"));
                            self.set_attr("curNode", self.attr("fragment"));
                            {
                                std::optional<py> pending18;
                                [[maybe_unused]] int flow18 = 0;
                                [[maybe_unused]] py result18;
                                try
                                {
                                    parser.method("Parse", { self.attr("_source"), boolean(true) });
                                }
                                catch (raised const&)
                                {
                                    pending18 = py_take_exception();
                                }
                                finally18:
                                self.set_attr("curNode", old_cur_node);
                                self.set_attr("document", old_document);
                                self.set_attr("_source", none());
                                if (pending18)
                                    py_reraise(*pending18);
                                if (flow18 == 1)
                                    return result18;
                            }
                            return I<-1>();
                        }
                        else
                        {
                            return (G<"xml.dom.expatbuilder.ExpatBuilder">()).method("external_entity_ref_handler", { self, context, base, systemId, publicId });
                        }
                        return none();
                    }) }, }));

                // This only adds some asserts to the original
                // end_element_handler(), so we only define this when -O is not
                // used.  If changing one, be sure to check the other to see if
                // it needs to be changed as well.
                // 
            define("xml.dom.expatbuilder", "Namespaces", w.make_class("Namespaces", {  }, {
                { "_initNamespaces", w.method("Namespaces", "_initNamespaces", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_ns_ordered_prefixes", py_list());
                        return none();
                    }) },
                { "createParser", w.method("Namespaces", "createParser", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parser;
                        parser = (Mod<"xml.parsers.expat">().attr("ParserCreate")).call({  }, { { "namespace_separator", S<" ">() } });
                        parser.set_attr("namespace_prefixes", boolean(true));
                        return parser;
                    }) },
                { "install", w.method("Namespaces", "install", { py_positional("parser") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parser = a[1];
                        (G<"xml.dom.expatbuilder.ExpatBuilder">()).method("install", { self, parser });
                        if (self.attr("_options").attr("namespace_declarations").truthy())
                        {
                            parser.set_attr("StartNamespaceDeclHandler", self.attr("start_namespace_decl_handler"));
                        }
                        return none();
                    }) },
                { "start_namespace_decl_handler", w.method("Namespaces", "start_namespace_decl_handler", { py_positional("prefix"), py_positional("uri") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prefix = a[1];
                        py uri = a[2];
                        self.attr("_ns_ordered_prefixes").method("append", { py_tuple({ prefix, uri }) });
                        return none();
                    }) },
                { "start_element_handler", w.method("Namespaces", "start_element_handler", { py_positional("name"), py_positional("attributes") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py attributes = a[2];
                        py _attrs;
                        py _attrsNS;
                        py a_;
                        py aname;
                        py i;
                        py localname;
                        py node;
                        py prefix;
                        py qname;
                        py uri;
                        py value;
                        if (name.contains((S<" ">())))
                        {
                            auto const items19 = unpack((G<"xml.dom.expatbuilder._parse_ns_name">())(self, name), 4);
                            uri = items19[0];
                            localname = items19[1];
                            prefix = items19[2];
                            qname = items19[3];
                        }
                        else
                        {
                            uri = Mod<"xml.dom">().attr("EMPTY_NAMESPACE");
                            qname = name;
                            localname = none();
                            prefix = Mod<"xml.dom">().attr("EMPTY_PREFIX");
                        }
                        node = (Mod<"xml.dom.minidom">().attr("Element"))(qname, uri, prefix, localname);
                        node.set_attr("ownerDocument", self.attr("document"));
                        (Mod<"xml.dom.minidom">().attr("_append_child"))(self.attr("curNode"), node);
                        self.set_attr("curNode", node);
                        if (self.attr("_ns_ordered_prefixes").truthy())
                        {
                            for (py const& item : iterate{ self.attr("_ns_ordered_prefixes") })
                            {
                                auto const items21 = unpack(item, 2);
                                prefix = items21[0];
                                uri = items21[1];
                                if (prefix.truthy())
                                {
                                    a_ = (Mod<"xml.dom.minidom">().attr("Attr"))((G<"xml.dom.expatbuilder._intern">())(self, (S<"xmlns:">()) + prefix), Mod<"xml.dom">().attr("XMLNS_NAMESPACE"), prefix, S<"xmlns">());
                                }
                                else
                                {
                                    a_ = (Mod<"xml.dom.minidom">().attr("Attr"))(S<"xmlns">(), Mod<"xml.dom">().attr("XMLNS_NAMESPACE"), S<"xmlns">(), Mod<"xml.dom">().attr("EMPTY_PREFIX"));
                                }
                                a_.set_attr("value", uri);
                                a_.set_attr("ownerDocument", self.attr("document"));
                                (Mod<"xml.dom.minidom">().attr("_set_attribute_node"))(node, a_);
                            }
                            del_item(self.attr("_ns_ordered_prefixes"), B<"slice">()(none(), none(), none()));
                        }
                        if (attributes.truthy())
                        {
                            node.method("_ensure_attributes");
                            _attrs = node.attr("_attrs");
                            _attrsNS = node.attr("_attrsNS");
                            for (py const& item : iterate{ (B<"range">())(I<0>(), py{ attributes.size() }, I<2>()) })
                            {
                                i = item;
                                aname = attributes[i];
                                value = attributes[i + (I<1>())];
                                if (aname.contains((S<" ">())))
                                {
                                    auto const items23 = unpack((G<"xml.dom.expatbuilder._parse_ns_name">())(self, aname), 4);
                                    uri = items23[0];
                                    localname = items23[1];
                                    prefix = items23[2];
                                    qname = items23[3];
                                    a_ = (Mod<"xml.dom.minidom">().attr("Attr"))(qname, uri, localname, prefix);
                                    _attrs.set_item(qname, a_);
                                    _attrsNS.set_item(py_tuple({ uri, localname }), a_);
                                }
                                else
                                {
                                    a_ = (Mod<"xml.dom.minidom">().attr("Attr"))(aname, Mod<"xml.dom">().attr("EMPTY_NAMESPACE"), aname, Mod<"xml.dom">().attr("EMPTY_PREFIX"));
                                    _attrs.set_item(aname, a_);
                                    _attrsNS.set_item(py_tuple({ Mod<"xml.dom">().attr("EMPTY_NAMESPACE"), aname }), a_);
                                }
                                a_.set_attr("ownerDocument", self.attr("document"));
                                a_.set_attr("value", value);
                                a_.set_attr("ownerElement", node);
                            }
                        }
                        return none();
                    }) },
                { "end_element_handler", w.method("Namespaces", "end_element_handler", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py curNode;
                        py localname;
                        py prefix;
                        py qname;
                        py uri;
                        curNode = self.attr("curNode");
                        if (name.contains((S<" ">())))
                        {
                            auto const items24 = unpack((G<"xml.dom.expatbuilder._parse_ns_name">())(self, name), 4);
                            uri = items24[0];
                            localname = items24[1];
                            prefix = items24[2];
                            qname = items24[3];
                            if (!([&]() -> py { py const t = boolean(curNode.attr("namespaceURI") == uri); return !t.truthy() ? t : [&]() -> py { py const t = boolean(curNode.attr("localName") == localname); return !t.truthy() ? t : boolean(curNode.attr("prefix") == prefix); }(); }()).truthy())
                                py_raise(B<"AssertionError">()(S<"element stack messed up! (namespace)">()));
                        }
                        else
                        {
                            if (!(curNode.attr("nodeName") == name))
                                py_raise(B<"AssertionError">()(S<"element stack messed up - bad nodeName">()));
                            if (!(curNode.attr("namespaceURI") == (Mod<"xml.dom">().attr("EMPTY_NAMESPACE"))))
                                py_raise(B<"AssertionError">()(S<"element stack messed up - bad namespaceURI">()));
                        }
                        self.set_attr("curNode", curNode.attr("parentNode"));
                        self.method("_finish_end_element", { curNode });
                        return none();
                    }) }, }));

            define("xml.dom.expatbuilder", "ExpatBuilderNS", w.make_class("ExpatBuilderNS", { G<"xml.dom.expatbuilder.Namespaces">(), G<"xml.dom.expatbuilder.ExpatBuilder">() }, {
                { "reset", w.method("ExpatBuilderNS", "reset", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        (G<"xml.dom.expatbuilder.ExpatBuilder">()).method("reset", { self });
                        self.method("_initNamespaces");
                        return none();
                    }) }, }));

            define("xml.dom.expatbuilder", "FragmentBuilderNS", w.make_class("FragmentBuilderNS", { G<"xml.dom.expatbuilder.Namespaces">(), G<"xml.dom.expatbuilder.FragmentBuilder">() }, {
                { "reset", w.method("FragmentBuilderNS", "reset", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        (G<"xml.dom.expatbuilder.FragmentBuilder">()).method("reset", { self });
                        self.method("_initNamespaces");
                        return none();
                    }) },
                { "_getNSattrs", w.method("FragmentBuilderNS", "_getNSattrs", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py L;
                        py attrs;
                        py context;
                        py declname;
                        py prefix;
                        py uri;
                        // XXX This needs to be re-written to walk the ancestors of the
                        // context to build up the namespace information from
                        // declarations, elements, and attributes found in context.
                        // Otherwise we have to store a bunch more data on the DOM
                        // (though that *might* be more reliable -- not clear).
                        attrs = S<"">();
                        context = self.attr("context");
                        L = py_list();
                        while (context.truthy())
                        {
                            if (has_attr(context, "_ns_prefix_uri"))
                            {
                                for (py const& item : iterate{ context.attr("_ns_prefix_uri").method("items") })
                                {
                                    auto const items27 = unpack(item, 2);
                                    prefix = items27[0];
                                    uri = items27[1];
                                    // add every new NS decl from context to L and attrs string
                                    if (L.contains(prefix))
                                    {
                                        continue;
                                    }
                                    L.method("append", { prefix });
                                    if (prefix.truthy())
                                    {
                                        declname = (S<"xmlns:">()) + prefix;
                                    }
                                    else
                                    {
                                        declname = S<"xmlns">();
                                    }
                                    if (attrs.truthy())
                                    {
                                        attrs = (S<"%s\n    %s='%s'">()) % py_tuple({ attrs, declname, uri });
                                    }
                                    else
                                    {
                                        attrs = (S<" %s='%s'">()) % py_tuple({ declname, uri });
                                    }
                                }
                            }
                            context = context.attr("parentNode");
                        }
                        return attrs;
                    }) }, }));

            define("xml.dom.expatbuilder", "ParseEscape", w.make_class("ParseEscape", { B<"Exception">() }, {
 }));

            define("xml.dom.expatbuilder", "InternalSubsetExtractor", w.make_class("InternalSubsetExtractor", { G<"xml.dom.expatbuilder.ExpatBuilder">() }, {
                { "subset", none() },
                { "getSubset", w.method("InternalSubsetExtractor", "getSubset", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("subset");
                    }) },
                { "parseFile", w.method("InternalSubsetExtractor", "parseFile", { py_positional("file") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        try
                        {
                            (G<"xml.dom.expatbuilder.ExpatBuilder">()).method("parseFile", { self, file });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler28;
                            if (matches(handler28.exception, G<"xml.dom.expatbuilder.ParseEscape">()))
                            {
                            }
                            else
                                py_reraise(handler28.exception);
                        }
                        return none();
                    }) },
                { "parseString", w.method("InternalSubsetExtractor", "parseString", { py_positional("string") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py string = a[1];
                        try
                        {
                            (G<"xml.dom.expatbuilder.ExpatBuilder">()).method("parseString", { self, string });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler29;
                            if (matches(handler29.exception, G<"xml.dom.expatbuilder.ParseEscape">()))
                            {
                            }
                            else
                                py_reraise(handler29.exception);
                        }
                        return none();
                    }) },
                { "install", w.method("InternalSubsetExtractor", "install", { py_positional("parser") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parser = a[1];
                        parser.set_attr("StartDoctypeDeclHandler", self.attr("start_doctype_decl_handler"));
                        parser.set_attr("StartElementHandler", self.attr("start_element_handler"));
                        return none();
                    }) },
                { "start_doctype_decl_handler", w.method("InternalSubsetExtractor", "start_doctype_decl_handler", { py_positional("name"), py_positional("publicId"), py_positional("systemId"), py_positional("has_internal_subset") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py publicId = a[2];
                        py systemId = a[3];
                        py has_internal_subset = a[4];
                        py parser;
                        if (has_internal_subset.truthy())
                        {
                            parser = self.method("getParser");
                            self.set_attr("subset", py_list());
                            parser.set_attr("DefaultHandler", self.attr("subset").attr("append"));
                            parser.set_attr("EndDoctypeDeclHandler", self.attr("end_doctype_decl_handler"));
                        }
                        else
                        {
                            py_raise((G<"xml.dom.expatbuilder.ParseEscape">())());
                        }
                        return none();
                    }) },
                { "end_doctype_decl_handler", w.method("InternalSubsetExtractor", "end_doctype_decl_handler", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s;
                        s = (S<"">()).method("join", { self.attr("subset") }).method("replace", { S<"\r\n">(), S<"\n">() }).method("replace", { S<"\r">(), S<"\n">() });
                        self.set_attr("subset", s);
                        py_raise((G<"xml.dom.expatbuilder.ParseEscape">())());
                    }) },
                { "start_element_handler", w.method("InternalSubsetExtractor", "start_element_handler", { py_positional("name"), py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py attrs = a[2];
                        py_raise((G<"xml.dom.expatbuilder.ParseEscape">())());
                    }) }, }));

            define("xml.dom.expatbuilder", "parse", w.function("parse", "parse", { py_positional("file"), py_positional("namespaces", boolean(true)) }, [](arguments a) -> py
                {
                    py file = a[0];
                    py namespaces = a[1];
                    py builder;
                    py fp;
                    py result;
                    if (namespaces.truthy())
                    {
                        builder = (G<"xml.dom.expatbuilder.ExpatBuilderNS">())();
                    }
                    else
                    {
                        builder = (G<"xml.dom.expatbuilder.ExpatBuilder">())();
                    }
                    if (isinstance(file, B<"str">()))
                    {
                        {
                            py const manager30 = (B<"open">())(file, S<"rb">());
                            py const exit30 = pyclass::special_method(manager30, "__exit__");
                            py const value30 = pyclass::special_method(manager30, "__enter__")(manager30);
                            fp = value30;
                            [[maybe_unused]] int flow30 = 0;
                            [[maybe_unused]] py result30;
                            bool exited30 = false;
                            try
                            {
                                result = builder.method("parseFile", { fp });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler30;
                                exited30 = true;
                                if (!exit30(manager30, type_of(handler30.exception), handler30.exception, handler30.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler30.exception);
                            }
                            finally30:
                            if (!exited30)
                                exit30(manager30, none(), none(), none());
                            if (flow30 == 1)
                                return result30;
                        }
                    }
                    else
                    {
                        result = builder.method("parseFile", { file });
                    }
                    return result;
                }));

            define("xml.dom.expatbuilder", "parseString", w.function("parseString", "parseString", { py_positional("string"), py_positional("namespaces", boolean(true)) }, [](arguments a) -> py
                {
                    py string = a[0];
                    py namespaces = a[1];
                    py builder;
                    if (namespaces.truthy())
                    {
                        builder = (G<"xml.dom.expatbuilder.ExpatBuilderNS">())();
                    }
                    else
                    {
                        builder = (G<"xml.dom.expatbuilder.ExpatBuilder">())();
                    }
                    return builder.method("parseString", { string });
                }));

            define("xml.dom.expatbuilder", "parseFragment", w.function("parseFragment", "parseFragment", { py_positional("file"), py_positional("context"), py_positional("namespaces", boolean(true)) }, [](arguments a) -> py
                {
                    py file = a[0];
                    py context = a[1];
                    py namespaces = a[2];
                    py builder;
                    py fp;
                    py result;
                    if (namespaces.truthy())
                    {
                        builder = (G<"xml.dom.expatbuilder.FragmentBuilderNS">())(context);
                    }
                    else
                    {
                        builder = (G<"xml.dom.expatbuilder.FragmentBuilder">())(context);
                    }
                    if (isinstance(file, B<"str">()))
                    {
                        {
                            py const manager31 = (B<"open">())(file, S<"rb">());
                            py const exit31 = pyclass::special_method(manager31, "__exit__");
                            py const value31 = pyclass::special_method(manager31, "__enter__")(manager31);
                            fp = value31;
                            [[maybe_unused]] int flow31 = 0;
                            [[maybe_unused]] py result31;
                            bool exited31 = false;
                            try
                            {
                                result = builder.method("parseFile", { fp });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler31;
                                exited31 = true;
                                if (!exit31(manager31, type_of(handler31.exception), handler31.exception, handler31.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler31.exception);
                            }
                            finally31:
                            if (!exited31)
                                exit31(manager31, none(), none(), none());
                            if (flow31 == 1)
                                return result31;
                        }
                    }
                    else
                    {
                        result = builder.method("parseFile", { file });
                    }
                    return result;
                }));

            define("xml.dom.expatbuilder", "parseFragmentString", w.function("parseFragmentString", "parseFragmentString", { py_positional("string"), py_positional("context"), py_positional("namespaces", boolean(true)) }, [](arguments a) -> py
                {
                    py string = a[0];
                    py context = a[1];
                    py namespaces = a[2];
                    py builder;
                    if (namespaces.truthy())
                    {
                        builder = (G<"xml.dom.expatbuilder.FragmentBuilderNS">())(context);
                    }
                    else
                    {
                        builder = (G<"xml.dom.expatbuilder.FragmentBuilder">())(context);
                    }
                    return builder.method("parseString", { string });
                }));

            define("xml.dom.expatbuilder", "makeBuilder", w.function("makeBuilder", "makeBuilder", { py_positional("options") }, [](arguments a) -> py
                {
                    py options = a[0];
                    if (options.attr("namespaces").truthy())
                    {
                        return (G<"xml.dom.expatbuilder.ExpatBuilderNS">())(options);
                    }
                    else
                    {
                        return (G<"xml.dom.expatbuilder.ExpatBuilder">())(options);
                    }
                    return none();
                }));
        }

        bool xml_dom_expatbuilder_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_xml_dom_expatbuilder();
                }
                if (auto const doc = xml_dom_expatbuilder_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("xmlbuilder", import_attribute("xml.dom", "xmlbuilder").release());
                aAttributes.emplace_back("minidom", import_attribute("xml.dom", "minidom").release());
                aAttributes.emplace_back("Node", import_attribute("xml.dom", "Node").release());
                aAttributes.emplace_back("EMPTY_NAMESPACE", import_attribute("xml.dom", "EMPTY_NAMESPACE").release());
                aAttributes.emplace_back("EMPTY_PREFIX", import_attribute("xml.dom", "EMPTY_PREFIX").release());
                aAttributes.emplace_back("XMLNS_NAMESPACE", import_attribute("xml.dom", "XMLNS_NAMESPACE").release());
                aAttributes.emplace_back("expat", import_attribute("xml.parsers", "expat").release());
                aAttributes.emplace_back("_append_child", import_attribute("xml.dom.minidom", "_append_child").release());
                aAttributes.emplace_back("_set_attribute_node", import_attribute("xml.dom.minidom", "_set_attribute_node").release());
                aAttributes.emplace_back("_XML_WHITESPACE", import_attribute("xml.dom.minidom", "_XML_WHITESPACE").release());
                aAttributes.emplace_back("NodeFilter", import_attribute("xml.dom.NodeFilter", "NodeFilter").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("xml.dom.expatbuilder", xml_dom_expatbuilder_module);
    }
}
