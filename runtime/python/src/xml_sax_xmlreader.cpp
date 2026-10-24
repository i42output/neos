/*
  xml_sax_xmlreader.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/xml/sax/xmlreader.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module xml.sax.xmlreader (CPython 3.13's Lib/xml/sax/xmlreader.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "xml_sax_xmlreader_docs.hpp"

namespace neos::python_runtime::detail::xml_sax_xmlreader_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "xml.sax.xmlreader.name"))
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
                py_raise("SystemError", std::string{ "xml.sax.xmlreader: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"xml.sax.xmlreader.name">()))
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
            if (modules_imported("xml.sax.xmlreader"))
                return py{ fn_import("xml.sax.xmlreader") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "xml.sax.xmlreader" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "xml.sax.xmlreader", xml_sax_xmlreader_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- xml.sax.xmlreader (Lib/xml/sax/xmlreader.py)

        void make_xml_sax_xmlreader()
        {
            static module_writer const& w = writer("xml.sax.xmlreader");
            // ===== XMLREADER =====
            define("xml.sax.xmlreader", "XMLReader", w.make_class("XMLReader", {  }, {
                { "__init__", w.method("XMLReader", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_cont_handler", (Mod<"xml.sax.handler">().attr("ContentHandler"))());
                        self.set_attr("_dtd_handler", (Mod<"xml.sax.handler">().attr("DTDHandler"))());
                        self.set_attr("_ent_handler", (Mod<"xml.sax.handler">().attr("EntityResolver"))());
                        self.set_attr("_err_handler", (Mod<"xml.sax.handler">().attr("ErrorHandler"))());
                        return none();
                    }) },
                { "parse", w.method("XMLReader", "parse", { py_positional("source") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py source = a[1];
                        py_raise((B<"NotImplementedError">())(S<"This method must be implemented!">()));
                    }) },
                { "getContentHandler", w.method("XMLReader", "getContentHandler", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_cont_handler");
                    }) },
                { "setContentHandler", w.method("XMLReader", "setContentHandler", { py_positional("handler") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py handler = a[1];
                        self.set_attr("_cont_handler", handler);
                        return none();
                    }) },
                { "getDTDHandler", w.method("XMLReader", "getDTDHandler", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_dtd_handler");
                    }) },
                { "setDTDHandler", w.method("XMLReader", "setDTDHandler", { py_positional("handler") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py handler = a[1];
                        self.set_attr("_dtd_handler", handler);
                        return none();
                    }) },
                { "getEntityResolver", w.method("XMLReader", "getEntityResolver", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_ent_handler");
                    }) },
                { "setEntityResolver", w.method("XMLReader", "setEntityResolver", { py_positional("resolver") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py resolver = a[1];
                        self.set_attr("_ent_handler", resolver);
                        return none();
                    }) },
                { "getErrorHandler", w.method("XMLReader", "getErrorHandler", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_err_handler");
                    }) },
                { "setErrorHandler", w.method("XMLReader", "setErrorHandler", { py_positional("handler") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py handler = a[1];
                        self.set_attr("_err_handler", handler);
                        return none();
                    }) },
                { "setLocale", w.method("XMLReader", "setLocale", { py_positional("locale") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py locale = a[1];
                        py_raise((Mod<"xml.sax._exceptions">().attr("SAXNotSupportedException"))(S<"Locale support not implemented">()));
                    }) },
                { "getFeature", w.method("XMLReader", "getFeature", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py_raise((Mod<"xml.sax._exceptions">().attr("SAXNotRecognizedException"))((S<"Feature '%s' not recognized">()) % name));
                    }) },
                { "setFeature", w.method("XMLReader", "setFeature", { py_positional("name"), py_positional("state") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py state = a[2];
                        py_raise((Mod<"xml.sax._exceptions">().attr("SAXNotRecognizedException"))((S<"Feature '%s' not recognized">()) % name));
                    }) },
                { "getProperty", w.method("XMLReader", "getProperty", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py_raise((Mod<"xml.sax._exceptions">().attr("SAXNotRecognizedException"))((S<"Property '%s' not recognized">()) % name));
                    }) },
                { "setProperty", w.method("XMLReader", "setProperty", { py_positional("name"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        py_raise((Mod<"xml.sax._exceptions">().attr("SAXNotRecognizedException"))((S<"Property '%s' not recognized">()) % name));
                    }) }, }));

            define("xml.sax.xmlreader", "IncrementalParser", w.make_class("IncrementalParser", { G<"xml.sax.xmlreader.XMLReader">() }, {
                { "__init__", w.method("IncrementalParser", "__init__", { py_positional("bufsize", power((I<2>()), (I<16>()))) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py bufsize = a[1];
                        self.set_attr("_bufsize", bufsize);
                        (G<"xml.sax.xmlreader.XMLReader">()).method("__init__", { self });
                        return none();
                    }) },
                { "parse", w.method("IncrementalParser", "parse", { py_positional("source") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py source = a[1];
                        py buffer;
                        py file;
                        py saxutils;
                        saxutils = Mod<"xml.sax.saxutils">();
                        source = saxutils.method("prepare_input_source", { source });
                        self.method("prepareParser", { source });
                        file = source.method("getCharacterStream");
                        if (file.is_none())
                        {
                            file = source.method("getByteStream");
                        }
                        while (([&]() -> py { buffer = file.method("read", { self.attr("_bufsize") }); return buffer; }()).truthy())
                        {
                            self.method("feed", { buffer });
                        }
                        self.method("close");
                        return none();
                    }) },
                { "feed", w.method("IncrementalParser", "feed", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py_raise((B<"NotImplementedError">())(S<"This method must be implemented!">()));
                    }) },
                { "prepareParser", w.method("IncrementalParser", "prepareParser", { py_positional("source") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py source = a[1];
                        py_raise((B<"NotImplementedError">())(S<"prepareParser must be overridden!">()));
                    }) },
                { "close", w.method("IncrementalParser", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise((B<"NotImplementedError">())(S<"This method must be implemented!">()));
                    }) },
                { "reset", w.method("IncrementalParser", "reset", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise((B<"NotImplementedError">())(S<"This method must be implemented!">()));
                    }) }, }));

            // ===== LOCATOR =====
            define("xml.sax.xmlreader", "Locator", w.make_class("Locator", {  }, {
                { "getColumnNumber", w.method("Locator", "getColumnNumber", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return I<-1>();
                    }) },
                { "getLineNumber", w.method("Locator", "getLineNumber", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return I<-1>();
                    }) },
                { "getPublicId", w.method("Locator", "getPublicId", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "getSystemId", w.method("Locator", "getSystemId", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) }, }));

            // ===== INPUTSOURCE =====
            define("xml.sax.xmlreader", "InputSource", w.make_class("InputSource", {  }, {
                { "__init__", w.method("InputSource", "__init__", { py_positional("system_id", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py system_id = a[1];
                        self.set_attr("_InputSource__system_id", system_id);
                        self.set_attr("_InputSource__public_id", none());
                        self.set_attr("_InputSource__encoding", none());
                        self.set_attr("_InputSource__bytefile", none());
                        self.set_attr("_InputSource__charfile", none());
                        return none();
                    }) },
                { "setPublicId", w.method("InputSource", "setPublicId", { py_positional("public_id") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py public_id = a[1];
                        self.set_attr("_InputSource__public_id", public_id);
                        return none();
                    }) },
                { "getPublicId", w.method("InputSource", "getPublicId", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_InputSource__public_id");
                    }) },
                { "setSystemId", w.method("InputSource", "setSystemId", { py_positional("system_id") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py system_id = a[1];
                        self.set_attr("_InputSource__system_id", system_id);
                        return none();
                    }) },
                { "getSystemId", w.method("InputSource", "getSystemId", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_InputSource__system_id");
                    }) },
                { "setEncoding", w.method("InputSource", "setEncoding", { py_positional("encoding") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py encoding = a[1];
                        self.set_attr("_InputSource__encoding", encoding);
                        return none();
                    }) },
                { "getEncoding", w.method("InputSource", "getEncoding", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_InputSource__encoding");
                    }) },
                { "setByteStream", w.method("InputSource", "setByteStream", { py_positional("bytefile") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py bytefile = a[1];
                        self.set_attr("_InputSource__bytefile", bytefile);
                        return none();
                    }) },
                { "getByteStream", w.method("InputSource", "getByteStream", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_InputSource__bytefile");
                    }) },
                { "setCharacterStream", w.method("InputSource", "setCharacterStream", { py_positional("charfile") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py charfile = a[1];
                        self.set_attr("_InputSource__charfile", charfile);
                        return none();
                    }) },
                { "getCharacterStream", w.method("InputSource", "getCharacterStream", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_InputSource__charfile");
                    }) }, }));

            // ===== ATTRIBUTESIMPL =====
            define("xml.sax.xmlreader", "AttributesImpl", w.make_class("AttributesImpl", {  }, {
                { "__init__", w.method("AttributesImpl", "__init__", { py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attrs = a[1];
                        self.set_attr("_attrs", attrs);
                        return none();
                    }) },
                { "getLength", w.method("AttributesImpl", "getLength", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py{ self.attr("_attrs").size() };
                    }) },
                { "getType", w.method("AttributesImpl", "getType", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return S<"CDATA">();
                    }) },
                { "getValue", w.method("AttributesImpl", "getValue", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return self.attr("_attrs")[name];
                    }) },
                { "getValueByQName", w.method("AttributesImpl", "getValueByQName", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return self.attr("_attrs")[name];
                    }) },
                { "getNameByQName", w.method("AttributesImpl", "getNameByQName", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        if (!self.attr("_attrs").contains(name))
                        {
                            py_raise((B<"KeyError">())(name));
                        }
                        return name;
                    }) },
                { "getQNameByName", w.method("AttributesImpl", "getQNameByName", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        if (!self.attr("_attrs").contains(name))
                        {
                            py_raise((B<"KeyError">())(name));
                        }
                        return name;
                    }) },
                { "getNames", w.method("AttributesImpl", "getNames", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"list">())(self.attr("_attrs").method("keys"));
                    }) },
                { "getQNames", w.method("AttributesImpl", "getQNames", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"list">())(self.attr("_attrs").method("keys"));
                    }) },
                { "__len__", w.method("AttributesImpl", "__len__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py{ self.attr("_attrs").size() };
                    }) },
                { "__getitem__", w.method("AttributesImpl", "__getitem__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return self.attr("_attrs")[name];
                    }) },
                { "keys", w.method("AttributesImpl", "keys", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"list">())(self.attr("_attrs").method("keys"));
                    }) },
                { "__contains__", w.method("AttributesImpl", "__contains__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return boolean(self.attr("_attrs").contains(name));
                    }) },
                { "get", w.method("AttributesImpl", "get", { py_positional("name"), py_positional("alternative", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py alternative = a[2];
                        return self.attr("_attrs").method("get", { name, alternative });
                    }) },
                { "copy", w.method("AttributesImpl", "copy", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("__class__", { self.attr("_attrs") });
                    }) },
                { "items", w.method("AttributesImpl", "items", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"list">())(self.attr("_attrs").method("items"));
                    }) },
                { "values", w.method("AttributesImpl", "values", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"list">())(self.attr("_attrs").method("values"));
                    }) }, }));

            // ===== ATTRIBUTESNSIMPL =====
            define("xml.sax.xmlreader", "AttributesNSImpl", w.make_class("AttributesNSImpl", { G<"xml.sax.xmlreader.AttributesImpl">() }, {
                { "__init__", w.method("AttributesNSImpl", "__init__", { py_positional("attrs"), py_positional("qnames") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attrs = a[1];
                        py qnames = a[2];
                        self.set_attr("_attrs", attrs);
                        self.set_attr("_qnames", qnames);
                        return none();
                    }) },
                { "getValueByQName", w.method("AttributesNSImpl", "getValueByQName", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py nsname;
                        py qname;
                        for (py const& item : iterate{ self.attr("_qnames").method("items") })
                        {
                            auto const items3 = unpack(item, 2);
                            nsname = items3[0];
                            qname = items3[1];
                            if (qname == name)
                            {
                                return self.attr("_attrs")[nsname];
                            }
                        }
                        py_raise((B<"KeyError">())(name));
                    }) },
                { "getNameByQName", w.method("AttributesNSImpl", "getNameByQName", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py nsname;
                        py qname;
                        for (py const& item : iterate{ self.attr("_qnames").method("items") })
                        {
                            auto const items5 = unpack(item, 2);
                            nsname = items5[0];
                            qname = items5[1];
                            if (qname == name)
                            {
                                return nsname;
                            }
                        }
                        py_raise((B<"KeyError">())(name));
                    }) },
                { "getQNameByName", w.method("AttributesNSImpl", "getQNameByName", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return self.attr("_qnames")[name];
                    }) },
                { "getQNames", w.method("AttributesNSImpl", "getQNames", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"list">())(self.attr("_qnames").method("values"));
                    }) },
                { "copy", w.method("AttributesNSImpl", "copy", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("__class__", { self.attr("_attrs"), self.attr("_qnames") });
                    }) }, }));

            define("xml.sax.xmlreader", "_test", w.function("_test", "_test", {  }, [](arguments a) -> py
                {
                    (G<"xml.sax.xmlreader.XMLReader">())();
                    (G<"xml.sax.xmlreader.IncrementalParser">())();
                    (G<"xml.sax.xmlreader.Locator">())();
                    return none();
                }));

            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool xml_sax_xmlreader_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_xml_sax_xmlreader();
                }
                if (auto const doc = xml_sax_xmlreader_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("handler", py{ fn_import("xml.sax.handler") }.release());
                aAttributes.emplace_back("SAXNotSupportedException", import_attribute("xml.sax._exceptions", "SAXNotSupportedException").release());
                aAttributes.emplace_back("SAXNotRecognizedException", import_attribute("xml.sax._exceptions", "SAXNotRecognizedException").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("xml.sax.xmlreader", xml_sax_xmlreader_module);
    }
}
