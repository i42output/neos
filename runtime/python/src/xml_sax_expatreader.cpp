/*
  xml_sax_expatreader.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/xml/sax/expatreader.py), Copyright (c) 2001-2024 Python
  Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module xml.sax.expatreader (CPython 3.13's Lib/xml/sax/expatreader.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "xml_sax_expatreader_docs.hpp"

namespace neos::python_runtime::detail::xml_sax_expatreader_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "xml.sax.expatreader.name"))
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
                py_raise("SystemError", std::string{ "xml.sax.expatreader: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"xml.sax.expatreader.name">()))
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
            if (modules_imported("xml.sax.expatreader"))
                return py{ fn_import("xml.sax.expatreader") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "xml.sax.expatreader" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "xml.sax.expatreader", xml_sax_expatreader_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- xml.sax.expatreader (Lib/xml/sax/expatreader.py)

        void make_xml_sax_expatreader()
        {
            static module_writer const& w = writer("xml.sax.expatreader");
            define("xml.sax.expatreader", "version", S<"0.20">());
            if (!(B<"hasattr">())(Mod<"xml.parsers.expat">(), S<"ParserCreate">()).truthy())
            {
                // (raise SAXReaderNotAvailable('expat not supported', None): the runtime registers the module only where it is supported)
            }
            define("xml.sax.expatreader", "AttributesImpl", Mod<"xml.sax.xmlreader">().attr("AttributesImpl"));
            define("xml.sax.expatreader", "AttributesNSImpl", Mod<"xml.sax.xmlreader">().attr("AttributesNSImpl"));
            // If we're using a sufficiently recent version of Python, we can use
            // weak references to avoid cycles between the parser and content
            // handler, otherwise we'll just have to pretend.
            define("xml.sax.expatreader", "_mkproxy", Mod<"weakref">().attr("proxy"));
            undefine("xml.sax.expatreader", "weakref");
            undefine("xml.sax.expatreader", "_weakref");
            define("xml.sax.expatreader", "_ClosedParser", w.make_class("_ClosedParser", {  }, {
 }));

            // --- ExpatLocator
            define("xml.sax.expatreader", "ExpatLocator", w.make_class("ExpatLocator", { Mod<"xml.sax.xmlreader">().attr("Locator") }, {
                { "__init__", w.method("ExpatLocator", "__init__", { py_positional("parser") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parser = a[1];
                        self.set_attr("_ref", (G<"xml.sax.expatreader._mkproxy">())(parser));
                        return none();
                    }) },
                { "getColumnNumber", w.method("ExpatLocator", "getColumnNumber", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parser;
                        parser = self.attr("_ref");
                        if (parser.attr("_parser").is_none())
                        {
                            return none();
                        }
                        return parser.attr("_parser").attr("ErrorColumnNumber");
                    }) },
                { "getLineNumber", w.method("ExpatLocator", "getLineNumber", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parser;
                        parser = self.attr("_ref");
                        if (parser.attr("_parser").is_none())
                        {
                            return I<1>();
                        }
                        return parser.attr("_parser").attr("ErrorLineNumber");
                    }) },
                { "getPublicId", w.method("ExpatLocator", "getPublicId", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parser;
                        parser = self.attr("_ref");
                        if (parser.is_none())
                        {
                            return none();
                        }
                        return parser.attr("_source").method("getPublicId");
                    }) },
                { "getSystemId", w.method("ExpatLocator", "getSystemId", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parser;
                        parser = self.attr("_ref");
                        if (parser.is_none())
                        {
                            return none();
                        }
                        return parser.attr("_source").method("getSystemId");
                    }) }, }));

            // --- ExpatParser
                // XMLReader methods
                // Redefined setContentHandler to allow changing handlers during parsing
                // IncrementalParser methods
                // Locator methods
                // event handlers
                // this is not used (call directly to ContentHandler)
                // this is not used (call directly to ContentHandler)
            define("xml.sax.expatreader", "ExpatParser", w.make_class("ExpatParser", { Mod<"xml.sax.xmlreader">().attr("IncrementalParser"), Mod<"xml.sax.xmlreader">().attr("Locator") }, {
                { "__init__", w.method("ExpatParser", "__init__", { py_positional("namespaceHandling", I<0>()), py_positional("bufsize", power((I<2>()), (I<16>())) - (I<20>())) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespaceHandling = a[1];
                        py bufsize = a[2];
                        (Mod<"xml.sax.xmlreader">().attr("IncrementalParser")).method("__init__", { self, bufsize });
                        self.set_attr("_source", (Mod<"xml.sax.xmlreader">().attr("InputSource"))());
                        self.set_attr("_parser", none());
                        self.set_attr("_namespaces", namespaceHandling);
                        self.set_attr("_lex_handler_prop", none());
                        self.set_attr("_parsing", boolean(false));
                        self.set_attr("_entity_stack", py_list());
                        self.set_attr("_external_ges", I<0>());
                        self.set_attr("_interning", none());
                        return none();
                    }) },
                { "parse", w.method("ExpatParser", "parse", { py_positional("source") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py source = a[1];
                        source = (Mod<"xml.sax.saxutils">().attr("prepare_input_source"))(source);
                        self.set_attr("_source", source);
                        try
                        {
                            self.method("reset");
                            self.attr("_cont_handler").method("setDocumentLocator", { (G<"xml.sax.expatreader.ExpatLocator">())(self) });
                            (Mod<"xml.sax.xmlreader">().attr("IncrementalParser")).method("parse", { self, source });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler1;
                            {
                                // bpo-30264: Close the source on error to not leak resources:
                                // xml.sax.parse() doesn't give access to the underlying parser
                                // to the caller
                                self.method("_close_source");
                                py_reraise(handler1.exception);
                            }
                        }
                        return none();
                    }) },
                { "prepareParser", w.method("ExpatParser", "prepareParser", { py_positional("source") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py source = a[1];
                        if (!source.method("getSystemId").is_none())
                        {
                            self.attr("_parser").method("SetBase", { source.method("getSystemId") });
                        }
                        return none();
                    }) },
                { "setContentHandler", w.method("ExpatParser", "setContentHandler", { py_positional("handler") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py handler = a[1];
                        (Mod<"xml.sax.xmlreader">().attr("IncrementalParser")).method("setContentHandler", { self, handler });
                        if (self.attr("_parsing").truthy())
                        {
                            self.method("_reset_cont_handler");
                        }
                        return none();
                    }) },
                { "getFeature", w.method("ExpatParser", "getFeature", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        if (name == (Mod<"xml.sax.handler">().attr("feature_namespaces")))
                        {
                            return self.attr("_namespaces");
                        }
                        else if (name == (Mod<"xml.sax.handler">().attr("feature_string_interning")))
                        {
                            return boolean(!self.attr("_interning").is_none());
                        }
                        else if (py_tuple({ Mod<"xml.sax.handler">().attr("feature_validation"), Mod<"xml.sax.handler">().attr("feature_external_pes"), Mod<"xml.sax.handler">().attr("feature_namespace_prefixes") }).contains(name))
                        {
                            return I<0>();
                        }
                        else if (name == (Mod<"xml.sax.handler">().attr("feature_external_ges")))
                        {
                            return self.attr("_external_ges");
                        }
                        py_raise((Mod<"xml.sax._exceptions">().attr("SAXNotRecognizedException"))((S<"Feature '%s' not recognized">()) % name));
                    }) },
                { "setFeature", w.method("ExpatParser", "setFeature", { py_positional("name"), py_positional("state") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py state = a[2];
                        if (self.attr("_parsing").truthy())
                        {
                            py_raise((Mod<"xml.sax._exceptions">().attr("SAXNotSupportedException"))(S<"Cannot set features while parsing">()));
                        }
                        if (name == (Mod<"xml.sax.handler">().attr("feature_namespaces")))
                        {
                            self.set_attr("_namespaces", state);
                        }
                        else if (name == (Mod<"xml.sax.handler">().attr("feature_external_ges")))
                        {
                            self.set_attr("_external_ges", state);
                        }
                        else if (name == (Mod<"xml.sax.handler">().attr("feature_string_interning")))
                        {
                            if (state.truthy())
                            {
                                if (self.attr("_interning").is_none())
                                {
                                    self.set_attr("_interning", py_dict());
                                }
                            }
                            else
                            {
                                self.set_attr("_interning", none());
                            }
                        }
                        else if (name == (Mod<"xml.sax.handler">().attr("feature_validation")))
                        {
                            if (state.truthy())
                            {
                                py_raise((Mod<"xml.sax._exceptions">().attr("SAXNotSupportedException"))(S<"expat does not support validation">()));
                            }
                        }
                        else if (name == (Mod<"xml.sax.handler">().attr("feature_external_pes")))
                        {
                            if (state.truthy())
                            {
                                py_raise((Mod<"xml.sax._exceptions">().attr("SAXNotSupportedException"))(S<"expat does not read external parameter entities">()));
                            }
                        }
                        else if (name == (Mod<"xml.sax.handler">().attr("feature_namespace_prefixes")))
                        {
                            if (state.truthy())
                            {
                                py_raise((Mod<"xml.sax._exceptions">().attr("SAXNotSupportedException"))(S<"expat does not report namespace prefixes">()));
                            }
                        }
                        else
                        {
                            py_raise((Mod<"xml.sax._exceptions">().attr("SAXNotRecognizedException"))((S<"Feature '%s' not recognized">()) % name));
                        }
                        return none();
                    }) },
                { "getProperty", w.method("ExpatParser", "getProperty", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        if (name == (Mod<"xml.sax.handler">().attr("property_lexical_handler")))
                        {
                            return self.attr("_lex_handler_prop");
                        }
                        else if (name == (Mod<"xml.sax.handler">().attr("property_interning_dict")))
                        {
                            return self.attr("_interning");
                        }
                        else if (name == (Mod<"xml.sax.handler">().attr("property_xml_string")))
                        {
                            if (self.attr("_parser").truthy())
                            {
                                if (has_attr(self.attr("_parser"), "GetInputContext"))
                                {
                                    return self.attr("_parser").method("GetInputContext");
                                }
                                else
                                {
                                    py_raise((Mod<"xml.sax._exceptions">().attr("SAXNotRecognizedException"))(S<"This version of expat does not support getting the XML string">()));
                                }
                            }
                            else
                            {
                                py_raise((Mod<"xml.sax._exceptions">().attr("SAXNotSupportedException"))(S<"XML string cannot be returned when not parsing">()));
                            }
                        }
                        py_raise((Mod<"xml.sax._exceptions">().attr("SAXNotRecognizedException"))((S<"Property '%s' not recognized">()) % name));
                    }) },
                { "setProperty", w.method("ExpatParser", "setProperty", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        if (name == (Mod<"xml.sax.handler">().attr("property_lexical_handler")))
                        {
                            self.set_attr("_lex_handler_prop", value);
                            if (self.attr("_parsing").truthy())
                            {
                                self.method("_reset_lex_handler_prop");
                            }
                        }
                        else if (name == (Mod<"xml.sax.handler">().attr("property_interning_dict")))
                        {
                            self.set_attr("_interning", value);
                        }
                        else if (name == (Mod<"xml.sax.handler">().attr("property_xml_string")))
                        {
                            py_raise((Mod<"xml.sax._exceptions">().attr("SAXNotSupportedException"))((S<"Property '%s' cannot be set">()) % name));
                        }
                        else
                        {
                            py_raise((Mod<"xml.sax._exceptions">().attr("SAXNotRecognizedException"))((S<"Property '%s' not recognized">()) % name));
                        }
                        return none();
                    }) },
                { "feed", w.method("ExpatParser", "feed", { py_positional("data"), py_positional("isFinal", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py isFinal = a[2];
                        py e;
                        py exc;
                        if (!self.attr("_parsing").truthy())
                        {
                            self.method("reset");
                            self.set_attr("_parsing", boolean(true));
                            self.attr("_cont_handler").method("startDocument");
                        }
                        try
                        {
                            // The isFinal parameter is internal to the expat reader.
                            // If it is set to true, expat will check validity of the entire
                            // document. When feeding chunks, they are not normally final -
                            // except when invoked from close.
                            self.attr("_parser").method("Parse", { data, isFinal });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler2;
                            if (matches(handler2.exception, Mod<"xml.parsers.expat">().attr("error")))
                            {
                                e = handler2.exception;
                                exc = (Mod<"xml.sax._exceptions">().attr("SAXParseException"))((Mod<"xml.parsers.expat">().attr("ErrorString"))(e.attr("code")), e, self);
                                // FIXME: when to invoke error()?
                                self.attr("_err_handler").method("fatalError", { exc });
                            }
                            else
                                py_reraise(handler2.exception);
                        }
                        return none();
                    }) },
                { "flush", w.method("ExpatParser", "flush", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py e;
                        py exc;
                        py was_enabled;
                        if (self.attr("_parser").is_none())
                        {
                            return none();
                        }
                        was_enabled = self.attr("_parser").method("GetReparseDeferralEnabled");
                        {
                            std::optional<py> pending3;
                            [[maybe_unused]] int flow3 = 0;
                            [[maybe_unused]] py result3;
                            try
                            {
                                try
                                {
                                    self.attr("_parser").method("SetReparseDeferralEnabled", { boolean(false) });
                                    self.attr("_parser").method("Parse", { py{ make_bytes(std::string{ "", 0 }) }, boolean(false) });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler3;
                                    if (matches(handler3.exception, Mod<"xml.parsers.expat">().attr("error")))
                                    {
                                        e = handler3.exception;
                                        exc = (Mod<"xml.sax._exceptions">().attr("SAXParseException"))((Mod<"xml.parsers.expat">().attr("ErrorString"))(e.attr("code")), e, self);
                                        self.attr("_err_handler").method("fatalError", { exc });
                                    }
                                    else
                                        py_reraise(handler3.exception);
                                }
                            }
                            catch (raised const&)
                            {
                                pending3 = py_take_exception();
                            }
                            finally3:
                            self.attr("_parser").method("SetReparseDeferralEnabled", { was_enabled });
                            if (pending3)
                                py_reraise(*pending3);
                            if (flow3 == 1)
                                return result3;
                        }
                        return none();
                    }) },
                { "_close_source", w.method("ExpatParser", "_close_source", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file;
                        py source;
                        source = self.attr("_source");
                        {
                            std::optional<py> pending4;
                            [[maybe_unused]] int flow4 = 0;
                            [[maybe_unused]] py result4;
                            try
                            {
                                file = source.method("getCharacterStream");
                                if (!file.is_none())
                                {
                                    file.method("close");
                                }
                            }
                            catch (raised const&)
                            {
                                pending4 = py_take_exception();
                            }
                            finally4:
                            file = source.method("getByteStream");
                            if (!file.is_none())
                            {
                                file.method("close");
                            }
                            if (pending4)
                                py_reraise(*pending4);
                            if (flow4 == 1)
                                return result4;
                        }
                        return none();
                    }) },
                { "close", w.method("ExpatParser", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parser;
                        if ((self.attr("_entity_stack").truthy() || self.attr("_parser").is_none() || isinstance(self.attr("_parser"), G<"xml.sax.expatreader._ClosedParser">())))
                        {
                            // If we are completing an external entity, do nothing here
                            return none();
                        }
                        {
                            std::optional<py> pending5;
                            [[maybe_unused]] int flow5 = 0;
                            [[maybe_unused]] py result5;
                            try
                            {
                                self.method("feed", { py{ make_bytes(std::string{ "", 0 }) } }, { { "isFinal", boolean(true) } });
                                self.attr("_cont_handler").method("endDocument");
                                self.set_attr("_parsing", boolean(false));
                                // break cycle created by expat handlers pointing to our methods
                                self.set_attr("_parser", none());
                            }
                            catch (raised const&)
                            {
                                pending5 = py_take_exception();
                            }
                            finally5:
                            self.set_attr("_parsing", boolean(false));
                            if (!self.attr("_parser").is_none())
                            {
                                // Keep ErrorColumnNumber and ErrorLineNumber after closing.
                                parser = (G<"xml.sax.expatreader._ClosedParser">())();
                                parser.set_attr("ErrorColumnNumber", self.attr("_parser").attr("ErrorColumnNumber"));
                                parser.set_attr("ErrorLineNumber", self.attr("_parser").attr("ErrorLineNumber"));
                                self.set_attr("_parser", parser);
                            }
                            self.method("_close_source");
                            if (pending5)
                                py_reraise(*pending5);
                            if (flow5 == 1)
                                return result5;
                        }
                        return none();
                    }) },
                { "_reset_cont_handler", w.method("ExpatParser", "_reset_cont_handler", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("_parser").set_attr("ProcessingInstructionHandler", self.attr("_cont_handler").attr("processingInstruction"));
                        self.attr("_parser").set_attr("CharacterDataHandler", self.attr("_cont_handler").attr("characters"));
                        return none();
                    }) },
                { "_reset_lex_handler_prop", w.method("ExpatParser", "_reset_lex_handler_prop", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py lex;
                        py parser;
                        lex = self.attr("_lex_handler_prop");
                        parser = self.attr("_parser");
                        if (lex.is_none())
                        {
                            parser.set_attr("CommentHandler", none());
                            parser.set_attr("StartCdataSectionHandler", none());
                            parser.set_attr("EndCdataSectionHandler", none());
                            parser.set_attr("StartDoctypeDeclHandler", none());
                            parser.set_attr("EndDoctypeDeclHandler", none());
                        }
                        else
                        {
                            parser.set_attr("CommentHandler", lex.attr("comment"));
                            parser.set_attr("StartCdataSectionHandler", lex.attr("startCDATA"));
                            parser.set_attr("EndCdataSectionHandler", lex.attr("endCDATA"));
                            parser.set_attr("StartDoctypeDeclHandler", self.attr("start_doctype_decl"));
                            parser.set_attr("EndDoctypeDeclHandler", lex.attr("endDTD"));
                        }
                        return none();
                    }) },
                { "reset", w.method("ExpatParser", "reset", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_namespaces").truthy())
                        {
                            self.set_attr("_parser", (Mod<"xml.parsers.expat">().attr("ParserCreate")).call({ self.attr("_source").method("getEncoding"), S<" ">() }, { { "intern", self.attr("_interning") } }));
                            self.attr("_parser").set_attr("namespace_prefixes", I<1>());
                            self.attr("_parser").set_attr("StartElementHandler", self.attr("start_element_ns"));
                            self.attr("_parser").set_attr("EndElementHandler", self.attr("end_element_ns"));
                        }
                        else
                        {
                            self.set_attr("_parser", (Mod<"xml.parsers.expat">().attr("ParserCreate")).call({ self.attr("_source").method("getEncoding") }, { { "intern", self.attr("_interning") } }));
                            self.attr("_parser").set_attr("StartElementHandler", self.attr("start_element"));
                            self.attr("_parser").set_attr("EndElementHandler", self.attr("end_element"));
                        }
                        self.method("_reset_cont_handler");
                        self.attr("_parser").set_attr("UnparsedEntityDeclHandler", self.attr("unparsed_entity_decl"));
                        self.attr("_parser").set_attr("NotationDeclHandler", self.attr("notation_decl"));
                        self.attr("_parser").set_attr("StartNamespaceDeclHandler", self.attr("start_namespace_decl"));
                        self.attr("_parser").set_attr("EndNamespaceDeclHandler", self.attr("end_namespace_decl"));
                        self.set_attr("_decl_handler_prop", none());
                        if (self.attr("_lex_handler_prop").truthy())
                        {
                            self.method("_reset_lex_handler_prop");
                        }
                        // self._parser.DefaultHandler =
                        // self._parser.DefaultHandlerExpand =
                        // self._parser.NotStandaloneHandler =
                        self.attr("_parser").set_attr("ExternalEntityRefHandler", self.attr("external_entity_ref"));
                        try
                        {
                            self.attr("_parser").set_attr("SkippedEntityHandler", self.attr("skipped_entity_handler"));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler6;
                            if (matches(handler6.exception, B<"AttributeError">()))
                            {
                                // This pyexpat does not support SkippedEntity
                            }
                            else
                                py_reraise(handler6.exception);
                        }
                        self.attr("_parser").method("SetParamEntityParsing", { Mod<"xml.parsers.expat">().attr("XML_PARAM_ENTITY_PARSING_UNLESS_STANDALONE") });
                        self.set_attr("_parsing", boolean(false));
                        self.set_attr("_entity_stack", py_list());
                        return none();
                    }) },
                { "getColumnNumber", w.method("ExpatParser", "getColumnNumber", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_parser").is_none())
                        {
                            return none();
                        }
                        return self.attr("_parser").attr("ErrorColumnNumber");
                    }) },
                { "getLineNumber", w.method("ExpatParser", "getLineNumber", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_parser").is_none())
                        {
                            return I<1>();
                        }
                        return self.attr("_parser").attr("ErrorLineNumber");
                    }) },
                { "getPublicId", w.method("ExpatParser", "getPublicId", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_source").method("getPublicId");
                    }) },
                { "getSystemId", w.method("ExpatParser", "getSystemId", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_source").method("getSystemId");
                    }) },
                { "start_element", w.method("ExpatParser", "start_element", { py_positional("name"), py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py attrs = a[2];
                        self.attr("_cont_handler").method("startElement", { name, (G<"xml.sax.expatreader.AttributesImpl">())(attrs) });
                        return none();
                    }) },
                { "end_element", w.method("ExpatParser", "end_element", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        self.attr("_cont_handler").method("endElement", { name });
                        return none();
                    }) },
                { "start_element_ns", w.method("ExpatParser", "start_element_ns", { py_positional("name"), py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py attrs = a[2];
                        py aname;
                        py apair;
                        py length;
                        py newattrs;
                        py pair;
                        py parts;
                        py qname;
                        py qnames;
                        py value;
                        pair = name.method("split");
                        if (py{ pair.size() } == (I<1>()))
                        {
                            // no namespace
                            pair = py_tuple({ none(), name });
                        }
                        else if (py{ pair.size() } == (I<3>()))
                        {
                            pair = py_tuple({ pair[I<0>()], pair[I<1>()] });
                        }
                        else
                        {
                            // default namespace
                            pair = (B<"tuple">())(pair);
                        }
                        newattrs = py_dict();
                        qnames = py_dict();
                        for (py const& item : iterate{ attrs.method("items") })
                        {
                            auto const items8 = unpack(item, 2);
                            aname = items8[0];
                            value = items8[1];
                            parts = aname.method("split");
                            length = py{ parts.size() };
                            if (length == (I<1>()))
                            {
                                // no namespace
                                qname = aname;
                                apair = py_tuple({ none(), aname });
                            }
                            else if (length == (I<3>()))
                            {
                                qname = (S<"%s:%s">()) % py_tuple({ parts[I<2>()], parts[I<1>()] });
                                apair = py_tuple({ parts[I<0>()], parts[I<1>()] });
                            }
                            else
                            {
                                // default namespace
                                qname = parts[I<1>()];
                                apair = (B<"tuple">())(parts);
                            }
                            newattrs.set_item(apair, value);
                            qnames.set_item(apair, qname);
                        }
                        self.attr("_cont_handler").method("startElementNS", { pair, none(), (G<"xml.sax.expatreader.AttributesNSImpl">())(newattrs, qnames) });
                        return none();
                    }) },
                { "end_element_ns", w.method("ExpatParser", "end_element_ns", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py pair;
                        pair = name.method("split");
                        if (py{ pair.size() } == (I<1>()))
                        {
                            pair = py_tuple({ none(), name });
                        }
                        else if (py{ pair.size() } == (I<3>()))
                        {
                            pair = py_tuple({ pair[I<0>()], pair[I<1>()] });
                        }
                        else
                        {
                            pair = (B<"tuple">())(pair);
                        }
                        self.attr("_cont_handler").method("endElementNS", { pair, none() });
                        return none();
                    }) },
                { "processing_instruction", w.method("ExpatParser", "processing_instruction", { py_positional("target"), py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py target = a[1];
                        py data = a[2];
                        self.attr("_cont_handler").method("processingInstruction", { target, data });
                        return none();
                    }) },
                { "character_data", w.method("ExpatParser", "character_data", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.attr("_cont_handler").method("characters", { data });
                        return none();
                    }) },
                { "start_namespace_decl", w.method("ExpatParser", "start_namespace_decl", { py_positional("prefix"), py_positional("uri") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prefix = a[1];
                        py uri = a[2];
                        self.attr("_cont_handler").method("startPrefixMapping", { prefix, uri });
                        return none();
                    }) },
                { "end_namespace_decl", w.method("ExpatParser", "end_namespace_decl", { py_positional("prefix") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prefix = a[1];
                        self.attr("_cont_handler").method("endPrefixMapping", { prefix });
                        return none();
                    }) },
                { "start_doctype_decl", w.method("ExpatParser", "start_doctype_decl", { py_positional("name"), py_positional("sysid"), py_positional("pubid"), py_positional("has_internal_subset") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py sysid = a[2];
                        py pubid = a[3];
                        py has_internal_subset = a[4];
                        self.attr("_lex_handler_prop").method("startDTD", { name, pubid, sysid });
                        return none();
                    }) },
                { "unparsed_entity_decl", w.method("ExpatParser", "unparsed_entity_decl", { py_positional("name"), py_positional("base"), py_positional("sysid"), py_positional("pubid"), py_positional("notation_name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py base = a[2];
                        py sysid = a[3];
                        py pubid = a[4];
                        py notation_name = a[5];
                        self.attr("_dtd_handler").method("unparsedEntityDecl", { name, pubid, sysid, notation_name });
                        return none();
                    }) },
                { "notation_decl", w.method("ExpatParser", "notation_decl", { py_positional("name"), py_positional("base"), py_positional("sysid"), py_positional("pubid") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py base = a[2];
                        py sysid = a[3];
                        py pubid = a[4];
                        self.attr("_dtd_handler").method("notationDecl", { name, pubid, sysid });
                        return none();
                    }) },
                { "external_entity_ref", w.method("ExpatParser", "external_entity_ref", { py_positional("context"), py_positional("base"), py_positional("sysid"), py_positional("pubid") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py context = a[1];
                        py base = a[2];
                        py sysid = a[3];
                        py pubid = a[4];
                        py source;
                        if (!self.attr("_external_ges").truthy())
                        {
                            return I<1>();
                        }
                        source = self.attr("_ent_handler").method("resolveEntity", { pubid, sysid });
                        source = (Mod<"xml.sax.saxutils">().attr("prepare_input_source"))(source, [&]() -> py { py const t = self.attr("_source").method("getSystemId"); return t.truthy() ? t : S<"">(); }());
                        self.attr("_entity_stack").method("append", { py_tuple({ self.attr("_parser"), self.attr("_source") }) });
                        self.set_attr("_parser", self.attr("_parser").method("ExternalEntityParserCreate", { context }));
                        self.set_attr("_source", source);
                        try
                        {
                            (Mod<"xml.sax.xmlreader">().attr("IncrementalParser")).method("parse", { self, source });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler9;
                            {
                                return I<0>();
                            }
                        }
                        auto const items10 = unpack(self.attr("_entity_stack")[I<-1>()], 2);
                        self.set_attr("_parser", items10[0]);
                        self.set_attr("_source", items10[1]);
                        del_item(self.attr("_entity_stack"), I<-1>());
                        return I<1>();
                    }) },
                { "skipped_entity_handler", w.method("ExpatParser", "skipped_entity_handler", { py_positional("name"), py_positional("is_pe") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py is_pe = a[2];
                        if (is_pe.truthy())
                        {
                            // The SAX spec requires to report skipped PEs with a '%'
                            name = (S<"%">()) + name;
                        }
                        self.attr("_cont_handler").method("skippedEntity", { name });
                        return none();
                    }) }, }));

            // ---
            define("xml.sax.expatreader", "create_parser", w.function("create_parser", "create_parser", {  }, [](arguments a) -> py
                {
                    py args = a[0];
                    py kwargs = a[1];
                    return call_ex((G<"xml.sax.expatreader.ExpatParser">()), {  }, args, kwargs);
                }, true, true));

            // ---
            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool xml_sax_expatreader_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_xml_sax_expatreader();
                }
                if (auto const doc = xml_sax_expatreader_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                {
                    py const m{ fn_import("xml.sax._exceptions") };
                    bool const all = has_attr(m, "__all__");
                    for (py const& n : iterate{ all ? m.attr("__all__") : builtin("dir")(m) })
                        if (auto const text = n.str(); all || !text.starts_with("_"))
                            aAttributes.emplace_back(text, m.attr(text.c_str()).release());
                }
                aAttributes.emplace_back("feature_validation", import_attribute("xml.sax.handler", "feature_validation").release());
                aAttributes.emplace_back("feature_namespaces", import_attribute("xml.sax.handler", "feature_namespaces").release());
                aAttributes.emplace_back("feature_namespace_prefixes", import_attribute("xml.sax.handler", "feature_namespace_prefixes").release());
                aAttributes.emplace_back("feature_external_ges", import_attribute("xml.sax.handler", "feature_external_ges").release());
                aAttributes.emplace_back("feature_external_pes", import_attribute("xml.sax.handler", "feature_external_pes").release());
                aAttributes.emplace_back("feature_string_interning", import_attribute("xml.sax.handler", "feature_string_interning").release());
                aAttributes.emplace_back("property_xml_string", import_attribute("xml.sax.handler", "property_xml_string").release());
                aAttributes.emplace_back("property_interning_dict", import_attribute("xml.sax.handler", "property_interning_dict").release());
                aAttributes.emplace_back("xmlreader", import_attribute("xml.sax", "xmlreader").release());
                aAttributes.emplace_back("saxutils", import_attribute("xml.sax", "saxutils").release());
                aAttributes.emplace_back("handler", import_attribute("xml.sax", "handler").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("xml.sax.expatreader", xml_sax_expatreader_module);
    }
}
