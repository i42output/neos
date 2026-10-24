/*
  xml_dom.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/xml/dom/__init__.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module xml.dom (CPython 3.13's Lib/xml/dom/__init__.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "xml_dom_docs.hpp"

namespace neos::python_runtime::detail::xml_dom_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "xml.dom.name"))
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
                py_raise("SystemError", std::string{ "xml.dom: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"xml.dom.name">()))
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
            if (modules_imported("xml.dom"))
                return py{ fn_import("xml.dom") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "xml.dom" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "xml.dom", xml_dom_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- xml.dom (Lib/xml/dom/__init__.py)

        void make_xml_dom()
        {
            static module_writer const& w = writer("xml.dom");
                // DOM implementations may use this as a base class for their own
                // Node implementations.  If they don't, the constants defined here
                // should still be used as the canonical definitions as they match
                // the values given in the W3C recommendation.  Client code can
                // safely refer to these values in all tests of Node.nodeType
                // values.
            define("xml.dom", "Node", w.make_class("Node", {  }, {
                { "__slots__", py_tuple() },
                { "ELEMENT_NODE", I<1>() },
                { "ATTRIBUTE_NODE", I<2>() },
                { "TEXT_NODE", I<3>() },
                { "CDATA_SECTION_NODE", I<4>() },
                { "ENTITY_REFERENCE_NODE", I<5>() },
                { "ENTITY_NODE", I<6>() },
                { "PROCESSING_INSTRUCTION_NODE", I<7>() },
                { "COMMENT_NODE", I<8>() },
                { "DOCUMENT_NODE", I<9>() },
                { "DOCUMENT_TYPE_NODE", I<10>() },
                { "DOCUMENT_FRAGMENT_NODE", I<11>() },
                { "NOTATION_NODE", I<12>() }, }));

            // ExceptionCode
            define("xml.dom", "INDEX_SIZE_ERR", I<1>());
            define("xml.dom", "DOMSTRING_SIZE_ERR", I<2>());
            define("xml.dom", "HIERARCHY_REQUEST_ERR", I<3>());
            define("xml.dom", "WRONG_DOCUMENT_ERR", I<4>());
            define("xml.dom", "INVALID_CHARACTER_ERR", I<5>());
            define("xml.dom", "NO_DATA_ALLOWED_ERR", I<6>());
            define("xml.dom", "NO_MODIFICATION_ALLOWED_ERR", I<7>());
            define("xml.dom", "NOT_FOUND_ERR", I<8>());
            define("xml.dom", "NOT_SUPPORTED_ERR", I<9>());
            define("xml.dom", "INUSE_ATTRIBUTE_ERR", I<10>());
            define("xml.dom", "INVALID_STATE_ERR", I<11>());
            define("xml.dom", "SYNTAX_ERR", I<12>());
            define("xml.dom", "INVALID_MODIFICATION_ERR", I<13>());
            define("xml.dom", "NAMESPACE_ERR", I<14>());
            define("xml.dom", "INVALID_ACCESS_ERR", I<15>());
            define("xml.dom", "VALIDATION_ERR", I<16>());
            define("xml.dom", "DOMException", w.make_class("DOMException", { B<"Exception">() }, {
                { "__init__", w.method("DOMException", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kw = a[2];
                        if (self.attr("__class__").is((G<"xml.dom.DOMException">())))
                        {
                            py_raise((B<"RuntimeError">())(S<"DOMException should not be instantiated directly">()));
                        }
                        call_ex((B<"Exception">()).attr("__init__"), { self }, args, kw);
                        return none();
                    }, true, true) },
                { "_get_code", w.method("DOMException", "_get_code", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("code");
                    }) }, }));

            define("xml.dom", "IndexSizeErr", w.make_class("IndexSizeErr", { G<"xml.dom.DOMException">() }, {
                { "code", G<"xml.dom.INDEX_SIZE_ERR">() }, }));

            define("xml.dom", "DomstringSizeErr", w.make_class("DomstringSizeErr", { G<"xml.dom.DOMException">() }, {
                { "code", G<"xml.dom.DOMSTRING_SIZE_ERR">() }, }));

            define("xml.dom", "HierarchyRequestErr", w.make_class("HierarchyRequestErr", { G<"xml.dom.DOMException">() }, {
                { "code", G<"xml.dom.HIERARCHY_REQUEST_ERR">() }, }));

            define("xml.dom", "WrongDocumentErr", w.make_class("WrongDocumentErr", { G<"xml.dom.DOMException">() }, {
                { "code", G<"xml.dom.WRONG_DOCUMENT_ERR">() }, }));

            define("xml.dom", "InvalidCharacterErr", w.make_class("InvalidCharacterErr", { G<"xml.dom.DOMException">() }, {
                { "code", G<"xml.dom.INVALID_CHARACTER_ERR">() }, }));

            define("xml.dom", "NoDataAllowedErr", w.make_class("NoDataAllowedErr", { G<"xml.dom.DOMException">() }, {
                { "code", G<"xml.dom.NO_DATA_ALLOWED_ERR">() }, }));

            define("xml.dom", "NoModificationAllowedErr", w.make_class("NoModificationAllowedErr", { G<"xml.dom.DOMException">() }, {
                { "code", G<"xml.dom.NO_MODIFICATION_ALLOWED_ERR">() }, }));

            define("xml.dom", "NotFoundErr", w.make_class("NotFoundErr", { G<"xml.dom.DOMException">() }, {
                { "code", G<"xml.dom.NOT_FOUND_ERR">() }, }));

            define("xml.dom", "NotSupportedErr", w.make_class("NotSupportedErr", { G<"xml.dom.DOMException">() }, {
                { "code", G<"xml.dom.NOT_SUPPORTED_ERR">() }, }));

            define("xml.dom", "InuseAttributeErr", w.make_class("InuseAttributeErr", { G<"xml.dom.DOMException">() }, {
                { "code", G<"xml.dom.INUSE_ATTRIBUTE_ERR">() }, }));

            define("xml.dom", "InvalidStateErr", w.make_class("InvalidStateErr", { G<"xml.dom.DOMException">() }, {
                { "code", G<"xml.dom.INVALID_STATE_ERR">() }, }));

            define("xml.dom", "SyntaxErr", w.make_class("SyntaxErr", { G<"xml.dom.DOMException">() }, {
                { "code", G<"xml.dom.SYNTAX_ERR">() }, }));

            define("xml.dom", "InvalidModificationErr", w.make_class("InvalidModificationErr", { G<"xml.dom.DOMException">() }, {
                { "code", G<"xml.dom.INVALID_MODIFICATION_ERR">() }, }));

            define("xml.dom", "NamespaceErr", w.make_class("NamespaceErr", { G<"xml.dom.DOMException">() }, {
                { "code", G<"xml.dom.NAMESPACE_ERR">() }, }));

            define("xml.dom", "InvalidAccessErr", w.make_class("InvalidAccessErr", { G<"xml.dom.DOMException">() }, {
                { "code", G<"xml.dom.INVALID_ACCESS_ERR">() }, }));

            define("xml.dom", "ValidationErr", w.make_class("ValidationErr", { G<"xml.dom.DOMException">() }, {
                { "code", G<"xml.dom.VALIDATION_ERR">() }, }));

                // Based on DOM Level 3 (WD 9 April 2002)
            define("xml.dom", "UserDataHandler", w.make_class("UserDataHandler", {  }, {
                { "NODE_CLONED", I<1>() },
                { "NODE_IMPORTED", I<2>() },
                { "NODE_DELETED", I<3>() },
                { "NODE_RENAMED", I<4>() }, }));

            define("xml.dom", "XML_NAMESPACE", S<"http://www.w3.org/XML/1998/namespace">());
            define("xml.dom", "XMLNS_NAMESPACE", S<"http://www.w3.org/2000/xmlns/">());
            define("xml.dom", "XHTML_NAMESPACE", S<"http://www.w3.org/1999/xhtml">());
            define("xml.dom", "EMPTY_NAMESPACE", none());
            define("xml.dom", "EMPTY_PREFIX", none());
        }

        bool xml_dom_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_xml_dom();
                }
                if (auto const doc = xml_dom_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("getDOMImplementation", import_attribute("xml.dom.domreg", "getDOMImplementation").release());
                aAttributes.emplace_back("registerDOMImplementation", import_attribute("xml.dom.domreg", "registerDOMImplementation").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("xml.dom", xml_dom_module);
    }
}
