/*
  xml_sax_handler.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/xml/sax/handler.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module xml.sax.handler (CPython 3.13's Lib/xml/sax/handler.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "xml_sax_handler_docs.hpp"

namespace neos::python_runtime::detail::xml_sax_handler_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "xml.sax.handler.name"))
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
                py_raise("SystemError", std::string{ "xml.sax.handler: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"xml.sax.handler.name">()))
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
            if (modules_imported("xml.sax.handler"))
                return py{ fn_import("xml.sax.handler") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "xml.sax.handler" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "xml.sax.handler", xml_sax_handler_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- xml.sax.handler (Lib/xml/sax/handler.py)

        void make_xml_sax_handler()
        {
            static module_writer const& w = writer("xml.sax.handler");
            define("xml.sax.handler", "version", S<"2.0beta">());
            // ============================================================================
            // 
            // HANDLER INTERFACES
            // 
            // ============================================================================
            // ===== ERRORHANDLER =====
            define("xml.sax.handler", "ErrorHandler", w.make_class("ErrorHandler", {  }, {
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
                    }) },
                { "warning", w.method("ErrorHandler", "warning", { py_positional("exception") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exception = a[1];
                        (B<"print">())(exception);
                        return none();
                    }) }, }));

            // ===== CONTENTHANDLER =====
            define("xml.sax.handler", "ContentHandler", w.make_class("ContentHandler", {  }, {
                { "__init__", w.method("ContentHandler", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_locator", none());
                        return none();
                    }) },
                { "setDocumentLocator", w.method("ContentHandler", "setDocumentLocator", { py_positional("locator") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py locator = a[1];
                        self.set_attr("_locator", locator);
                        return none();
                    }) },
                { "startDocument", w.method("ContentHandler", "startDocument", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "endDocument", w.method("ContentHandler", "endDocument", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "startPrefixMapping", w.method("ContentHandler", "startPrefixMapping", { py_positional("prefix"), py_positional("uri") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prefix = a[1];
                        py uri = a[2];
                        return none();
                    }) },
                { "endPrefixMapping", w.method("ContentHandler", "endPrefixMapping", { py_positional("prefix") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prefix = a[1];
                        return none();
                    }) },
                { "startElement", w.method("ContentHandler", "startElement", { py_positional("name"), py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py attrs = a[2];
                        return none();
                    }) },
                { "endElement", w.method("ContentHandler", "endElement", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return none();
                    }) },
                { "startElementNS", w.method("ContentHandler", "startElementNS", { py_positional("name"), py_positional("qname"), py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py qname = a[2];
                        py attrs = a[3];
                        return none();
                    }) },
                { "endElementNS", w.method("ContentHandler", "endElementNS", { py_positional("name"), py_positional("qname") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py qname = a[2];
                        return none();
                    }) },
                { "characters", w.method("ContentHandler", "characters", { py_positional("content") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py content = a[1];
                        return none();
                    }) },
                { "ignorableWhitespace", w.method("ContentHandler", "ignorableWhitespace", { py_positional("whitespace") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py whitespace = a[1];
                        return none();
                    }) },
                { "processingInstruction", w.method("ContentHandler", "processingInstruction", { py_positional("target"), py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py target = a[1];
                        py data = a[2];
                        return none();
                    }) },
                { "skippedEntity", w.method("ContentHandler", "skippedEntity", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return none();
                    }) }, }));

            // ===== DTDHandler =====
            define("xml.sax.handler", "DTDHandler", w.make_class("DTDHandler", {  }, {
                { "notationDecl", w.method("DTDHandler", "notationDecl", { py_positional("name"), py_positional("publicId"), py_positional("systemId") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py publicId = a[2];
                        py systemId = a[3];
                        return none();
                    }) },
                { "unparsedEntityDecl", w.method("DTDHandler", "unparsedEntityDecl", { py_positional("name"), py_positional("publicId"), py_positional("systemId"), py_positional("ndata") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py publicId = a[2];
                        py systemId = a[3];
                        py ndata = a[4];
                        return none();
                    }) }, }));

            // ===== ENTITYRESOLVER =====
            define("xml.sax.handler", "EntityResolver", w.make_class("EntityResolver", {  }, {
                { "resolveEntity", w.method("EntityResolver", "resolveEntity", { py_positional("publicId"), py_positional("systemId") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py publicId = a[1];
                        py systemId = a[2];
                        return systemId;
                    }) }, }));

            // ============================================================================
            // 
            // CORE FEATURES
            // 
            // ============================================================================
            define("xml.sax.handler", "feature_namespaces", S<"http://xml.org/sax/features/namespaces">());
            // true: Perform Namespace processing (default).
            // false: Optionally do not perform Namespace processing
            // (implies namespace-prefixes).
            // access: (parsing) read-only; (not parsing) read/write
            define("xml.sax.handler", "feature_namespace_prefixes", S<"http://xml.org/sax/features/namespace-prefixes">());
            // true: Report the original prefixed names and attributes used for Namespace
            // declarations.
            // false: Do not report attributes used for Namespace declarations, and
            // optionally do not report original prefixed names (default).
            // access: (parsing) read-only; (not parsing) read/write
            define("xml.sax.handler", "feature_string_interning", S<"http://xml.org/sax/features/string-interning">());
            // true: All element names, prefixes, attribute names, Namespace URIs, and
            // local names are interned using the built-in intern function.
            // false: Names are not necessarily interned, although they may be (default).
            // access: (parsing) read-only; (not parsing) read/write
            define("xml.sax.handler", "feature_validation", S<"http://xml.org/sax/features/validation">());
            // true: Report all validation errors (implies external-general-entities and
            // external-parameter-entities).
            // false: Do not report validation errors.
            // access: (parsing) read-only; (not parsing) read/write
            define("xml.sax.handler", "feature_external_ges", S<"http://xml.org/sax/features/external-general-entities">());
            // true: Include all external general (text) entities.
            // false: Do not include external general entities.
            // access: (parsing) read-only; (not parsing) read/write
            define("xml.sax.handler", "feature_external_pes", S<"http://xml.org/sax/features/external-parameter-entities">());
            // true: Include all external parameter entities, including the external
            // DTD subset.
            // false: Do not include any external parameter entities, even the external
            // DTD subset.
            // access: (parsing) read-only; (not parsing) read/write
            define("xml.sax.handler", "all_features", py_list({ G<"xml.sax.handler.feature_namespaces">(), G<"xml.sax.handler.feature_namespace_prefixes">(), G<"xml.sax.handler.feature_string_interning">(), G<"xml.sax.handler.feature_validation">(), G<"xml.sax.handler.feature_external_ges">(), G<"xml.sax.handler.feature_external_pes">() }));
            // ============================================================================
            // 
            // CORE PROPERTIES
            // 
            // ============================================================================
            define("xml.sax.handler", "property_lexical_handler", S<"http://xml.org/sax/properties/lexical-handler">());
            // data type: xml.sax.sax2lib.LexicalHandler
            // description: An optional extension handler for lexical events like comments.
            // access: read/write
            define("xml.sax.handler", "property_declaration_handler", S<"http://xml.org/sax/properties/declaration-handler">());
            // data type: xml.sax.sax2lib.DeclHandler
            // description: An optional extension handler for DTD-related events other
            // than notations and unparsed entities.
            // access: read/write
            define("xml.sax.handler", "property_dom_node", S<"http://xml.org/sax/properties/dom-node">());
            // data type: org.w3c.dom.Node
            // description: When parsing, the current DOM node being visited if this is
            // a DOM iterator; when not parsing, the root DOM node for
            // iteration.
            // access: (parsing) read-only; (not parsing) read/write
            define("xml.sax.handler", "property_xml_string", S<"http://xml.org/sax/properties/xml-string">());
            // data type: String
            // description: The literal string of characters that was the source for
            // the current event.
            // access: read-only
            define("xml.sax.handler", "property_encoding", S<"http://www.python.org/sax/properties/encoding">());
            // data type: String
            // description: The name of the encoding to assume for input data.
            // access: write: set the encoding, e.g. established by a higher-level
            // protocol. May change during parsing (e.g. after
            // processing a META tag)
            // read:  return the current encoding (possibly established through
            // auto-detection.
            // initial value: UTF-8
            // 
            define("xml.sax.handler", "property_interning_dict", S<"http://www.python.org/sax/properties/interning-dict">());
            // data type: Dictionary
            // description: The dictionary used to intern common strings in the document
            // access: write: Request that the parser uses a specific dictionary, to
            // allow interning across different documents
            // read:  return the current interning dictionary, or None
            // 
            define("xml.sax.handler", "all_properties", py_list({ G<"xml.sax.handler.property_lexical_handler">(), G<"xml.sax.handler.property_dom_node">(), G<"xml.sax.handler.property_declaration_handler">(), G<"xml.sax.handler.property_xml_string">(), G<"xml.sax.handler.property_encoding">(), G<"xml.sax.handler.property_interning_dict">() }));
            define("xml.sax.handler", "LexicalHandler", w.make_class("LexicalHandler", {  }, {
                { "comment", w.method("LexicalHandler", "comment", { py_positional("content") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py content = a[1];
                        return none();
                    }) },
                { "startDTD", w.method("LexicalHandler", "startDTD", { py_positional("name"), py_positional("public_id"), py_positional("system_id") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py public_id = a[2];
                        py system_id = a[3];
                        return none();
                    }) },
                { "endDTD", w.method("LexicalHandler", "endDTD", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "startCDATA", w.method("LexicalHandler", "startCDATA", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "endCDATA", w.method("LexicalHandler", "endCDATA", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) }, }));
        }

        bool xml_sax_handler_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_xml_sax_handler();
                }
                if (auto const doc = xml_sax_handler_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("xml.sax.handler", xml_sax_handler_module);
    }
}
