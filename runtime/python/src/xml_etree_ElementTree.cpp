/*
  xml_etree_ElementTree.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/xml/etree/ElementTree.py), Copyright (c) 2001-2024 Python
  Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/xml/etree/ElementTree.py's copyright: Copyright (c) 1999-2008 by
  Fredrik Lundh. Used under its permission notice (see runtime/python/NOTICE); licensed to PSF under a Contributor
  Agreement.
*/

// The module xml.etree.ElementTree (CPython 3.13's Lib/xml/etree/ElementTree.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "xml_etree_ElementTree_docs.hpp"

namespace neos::python_runtime::detail::xml_etree_ElementTree_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "xml.etree.ElementTree.name"))
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
                py_raise("SystemError", std::string{ "xml.etree.ElementTree: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"xml.etree.ElementTree.name">()))
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
            if (modules_imported("xml.etree.ElementTree"))
                return py{ fn_import("xml.etree.ElementTree") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "xml.etree.ElementTree" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "xml.etree.ElementTree", xml_etree_ElementTree_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- xml.etree.ElementTree (Lib/xml/etree/ElementTree.py)

        void make_xml_etree_ElementTree()
        {
            static module_writer const& w = writer("xml.etree.ElementTree");
            // ---------------------------------------------------------------------
            // Licensed to PSF under a Contributor Agreement.
            // See https://www.python.org/psf/license for licensing details.
            // 
            // ElementTree
            // Copyright (c) 1999-2008 by Fredrik Lundh.  All rights reserved.
            // 
            // fredrik@pythonware.com
            // http://www.pythonware.com
            // --------------------------------------------------------------------
            // The ElementTree toolkit is
            // 
            // Copyright (c) 1999-2008 by Fredrik Lundh
            // 
            // By obtaining, using, and/or copying this software and/or its
            // associated documentation, you agree that you have read, understood,
            // and will comply with the following terms and conditions:
            // 
            // Permission to use, copy, modify, and distribute this software and
            // its associated documentation for any purpose and without fee is
            // hereby granted, provided that the above copyright notice appears in
            // all copies, and that both that copyright notice and this permission
            // notice appear in supporting documentation, and that the name of
            // Secret Labs AB or the author not be used in advertising or publicity
            // pertaining to distribution of the software without specific, written
            // prior permission.
            // 
            // SECRET LABS AB AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD
            // TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANT-
            // ABILITY AND FITNESS.  IN NO EVENT SHALL SECRET LABS AB OR THE AUTHOR
            // BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY
            // DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
            // WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
            // ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
            // OF THIS SOFTWARE.
            // --------------------------------------------------------------------
            define("xml.etree.ElementTree", "__all__", py_list({ S<"Comment">(), S<"dump">(), S<"Element">(), S<"ElementTree">(), S<"fromstring">(), S<"fromstringlist">(), S<"indent">(), S<"iselement">(), S<"iterparse">(), S<"parse">(), S<"ParseError">(), S<"PI">(), S<"ProcessingInstruction">(), S<"QName">(), S<"SubElement">(), S<"tostring">(), S<"tostringlist">(), S<"TreeBuilder">(), S<"VERSION">(), S<"XML">(), S<"XMLID">(), S<"XMLParser">(), S<"XMLPullParser">(), S<"register_namespace">(), S<"canonicalize">(), S<"C14NWriterTarget">() }));
            // public symbols
            define("xml.etree.ElementTree", "VERSION", S<"1.3.0">());
            // The white space characters of the XML specification (see XML 1.0, 2.3).
            define("xml.etree.ElementTree", "_XML_WHITESPACE", S<" \t\r\n">());
            define("xml.etree.ElementTree", "ParseError", w.make_class("ParseError", { B<"SyntaxError">() }, {
 }));

            // --------------------------------------------------------------------
            define("xml.etree.ElementTree", "iselement", w.function("iselement", "iselement", { py_positional("element") }, [](arguments a) -> py
                {
                    py element = a[0];
                    return boolean(has_attr(element, "tag"));
                }));

            define("xml.etree.ElementTree", "Element", w.make_class("Element", {  }, {
                { "tag", none() },
                { "attrib", none() },
                { "text", none() },
                { "tail", none() },
                { "__init__", w.method("Element", "__init__", { py_positional("tag"), py_positional("attrib", py_dict()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tag = a[1];
                        py attrib = a[2];
                        py extra = a[3];
                        if (!isinstance(attrib, B<"dict">()))
                        {
                            py_raise((B<"TypeError">())((S<"attrib must be dict, not %s">()) % py_tuple({ attrib.attr("__class__").attr("__name__") })));
                        }
                        self.set_attr("tag", tag);
                        self.set_attr("attrib", [&]() -> py { py const d = py_dict(); d.method("update", { attrib }); d.method("update", { extra }); return d; }());
                        self.set_attr("_children", py_list());
                        return none();
                    }, false, true) },
                { "__repr__", w.method("Element", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"<%s %r at %#x>">()) % py_tuple({ self.attr("__class__").attr("__name__"), self.attr("tag"), (B<"id">())(self) });
                    }) },
                { "makeelement", w.method("Element", "makeelement", { py_positional("tag"), py_positional("attrib") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tag = a[1];
                        py attrib = a[2];
                        return self.method("__class__", { tag, attrib });
                    }) },
                { "__copy__", w.method("Element", "__copy__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py elem;
                        elem = self.method("makeelement", { self.attr("tag"), self.attr("attrib") });
                        elem.set_attr("text", self.attr("text"));
                        elem.set_attr("tail", self.attr("tail"));
                        elem.set_item(B<"slice">()(none(), none(), none()), self);
                        return elem;
                    }) },
                { "__len__", w.method("Element", "__len__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py{ self.attr("_children").size() };
                    }) },
                { "__bool__", w.method("Element", "__bool__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        (Mod<"warnings">().attr("warn")).call({ S<"Testing an element's truth value will always return True in future versions.  Use specific 'len(elem)' or 'elem is not None' test instead.">(), B<"DeprecationWarning">() }, { { "stacklevel", I<1>() } });
                        return boolean(py{ self.attr("_children").size() } != (I<0>()));
                    }) },
                { "__getitem__", w.method("Element", "__getitem__", { py_positional("index") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py index = a[1];
                        return self.attr("_children")[index];
                    }) },
                { "__setitem__", w.method("Element", "__setitem__", { py_positional("index"), py_positional("element") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py index = a[1];
                        py element = a[2];
                        py elt;
                        if (isinstance(index, B<"slice">()))
                        {
                            for (py const& item : iterate{ element })
                            {
                                elt = item;
                                self.method("_assert_is_element", { elt });
                            }
                        }
                        else
                        {
                            self.method("_assert_is_element", { element });
                        }
                        self.attr("_children").set_item(index, element);
                        return none();
                    }) },
                { "__delitem__", w.method("Element", "__delitem__", { py_positional("index") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py index = a[1];
                        del_item(self.attr("_children"), index);
                        return none();
                    }) },
                { "append", w.method("Element", "append", { py_positional("subelement") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py subelement = a[1];
                        self.method("_assert_is_element", { subelement });
                        self.attr("_children").method("append", { subelement });
                        return none();
                    }) },
                { "extend", w.method("Element", "extend", { py_positional("elements") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py elements = a[1];
                        py element;
                        for (py const& item : iterate{ elements })
                        {
                            element = item;
                            self.method("_assert_is_element", { element });
                            self.attr("_children").method("append", { element });
                        }
                        return none();
                    }) },
                { "insert", w.method("Element", "insert", { py_positional("index"), py_positional("subelement") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py index = a[1];
                        py subelement = a[2];
                        self.method("_assert_is_element", { subelement });
                        self.attr("_children").method("insert", { index, subelement });
                        return none();
                    }) },
                { "_assert_is_element", w.method("Element", "_assert_is_element", { py_positional("e") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py e = a[1];
                        if (!isinstance(e, G<"xml.etree.ElementTree._Element_Py">()))
                        {
                            py_raise((B<"TypeError">())((S<"expected an Element, not %s">()) % (B<"type">())(e).attr("__name__")));
                        }
                        return none();
                    }) },
                { "remove", w.method("Element", "remove", { py_positional("subelement") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py subelement = a[1];
                        // assert iselement(element)
                        self.attr("_children").method("remove", { subelement });
                        return none();
                    }) },
                { "find", w.method("Element", "find", { py_positional("path"), py_positional("namespaces", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py namespaces = a[2];
                        return (Mod<"xml.etree.ElementPath">().attr("find"))(self, path, namespaces);
                    }) },
                { "findtext", w.method("Element", "findtext", { py_positional("path"), py_positional("default", none()), py_positional("namespaces", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py default_ = a[2];
                        py namespaces = a[3];
                        return (Mod<"xml.etree.ElementPath">().attr("findtext"))(self, path, default_, namespaces);
                    }) },
                { "findall", w.method("Element", "findall", { py_positional("path"), py_positional("namespaces", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py namespaces = a[2];
                        return (Mod<"xml.etree.ElementPath">().attr("findall"))(self, path, namespaces);
                    }) },
                { "iterfind", w.method("Element", "iterfind", { py_positional("path"), py_positional("namespaces", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py namespaces = a[2];
                        return (Mod<"xml.etree.ElementPath">().attr("iterfind"))(self, path, namespaces);
                    }) },
                { "clear", w.method("Element", "clear", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("attrib").method("clear");
                        self.set_attr("_children", py_list());
                        py const value3 = none();
                        self.set_attr("text", value3);
                        self.set_attr("tail", value3);
                        return none();
                    }) },
                { "get", w.method("Element", "get", { py_positional("key"), py_positional("default", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py default_ = a[2];
                        return self.attr("attrib").method("get", { key, default_ });
                    }) },
                { "set", w.method("Element", "set", { py_positional("key"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py value = a[2];
                        self.attr("attrib").set_item(key, value);
                        return none();
                    }) },
                { "keys", w.method("Element", "keys", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("attrib").method("keys");
                    }) },
                { "items", w.method("Element", "items", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("attrib").method("items");
                    }) },
                { "iter", w.method("Element", "iter", { py_positional("tag", none()) }, [](arguments a) -> py
                    {
                        return make_native_generator("iter", "Element.iter", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py tag = a[1];
                            py e;
                            if (tag == (S<"*">()))
                            {
                                tag = none();
                            }
                            if ((tag.is_none() || (self.attr("tag") == tag)))
                            {
                                py_yield(self);
                            }
                            for (py const& item : iterate{ self.attr("_children") })
                            {
                                e = item;
                                yield_from(e.method("iter", { tag }));
                            }
                            return none();
                        });
                    }) },
                { "itertext", w.method("Element", "itertext", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("itertext", "Element.itertext", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py e;
                            py t;
                            py tag;
                            tag = self.attr("tag");
                            if (((!isinstance(tag, B<"str">())) && (!tag.is_none())))
                            {
                                return none();
                            }
                            t = self.attr("text");
                            if (t.truthy())
                            {
                                py_yield(t);
                            }
                            for (py const& item : iterate{ self })
                            {
                                e = item;
                                yield_from(e.method("itertext"));
                                t = e.attr("tail");
                                if (t.truthy())
                                {
                                    py_yield(t);
                                }
                            }
                            return none();
                        });
                    }) }, }));

            define("xml.etree.ElementTree", "SubElement", w.function("SubElement", "SubElement", { py_positional("parent"), py_positional("tag"), py_positional("attrib", py_dict()) }, [](arguments a) -> py
                {
                    py parent = a[0];
                    py tag = a[1];
                    py attrib = a[2];
                    py extra = a[3];
                    py element;
                    attrib = [&]() -> py { py const d = py_dict(); d.method("update", { attrib }); d.method("update", { extra }); return d; }();
                    element = parent.method("makeelement", { tag, attrib });
                    parent.method("append", { element });
                    return element;
                }, false, true));

            define("xml.etree.ElementTree", "Comment", w.function("Comment", "Comment", { py_positional("text", none()) }, [](arguments a) -> py
                {
                    py text = a[0];
                    py element;
                    element = (G<"xml.etree.ElementTree.Element">())(G<"xml.etree.ElementTree.Comment">());
                    element.set_attr("text", text);
                    return element;
                }));

            define("xml.etree.ElementTree", "ProcessingInstruction", w.function("ProcessingInstruction", "ProcessingInstruction", { py_positional("target"), py_positional("text", none()) }, [](arguments a) -> py
                {
                    py target = a[0];
                    py text = a[1];
                    py element;
                    element = (G<"xml.etree.ElementTree.Element">())(G<"xml.etree.ElementTree.ProcessingInstruction">());
                    element.set_attr("text", target);
                    if (text.truthy())
                    {
                        element.set_attr("text", (element.attr("text") + (S<" ">())) + text);
                    }
                    return element;
                }));

            define("xml.etree.ElementTree", "PI", G<"xml.etree.ElementTree.ProcessingInstruction">());
            define("xml.etree.ElementTree", "QName", w.make_class("QName", {  }, {
                { "__init__", w.method("QName", "__init__", { py_positional("text_or_uri"), py_positional("tag", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py text_or_uri = a[1];
                        py tag = a[2];
                        if (tag.truthy())
                        {
                            text_or_uri = (S<"{%s}%s">()) % py_tuple({ text_or_uri, tag });
                        }
                        self.set_attr("text", text_or_uri);
                        return none();
                    }) },
                { "__str__", w.method("QName", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("text");
                    }) },
                { "__repr__", w.method("QName", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"<%s %r>">()) % py_tuple({ self.attr("__class__").attr("__name__"), self.attr("text") });
                    }) },
                { "__hash__", w.method("QName", "__hash__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"hash">())(self.attr("text"));
                    }) },
                { "__le__", w.method("QName", "__le__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (isinstance(other, G<"xml.etree.ElementTree.QName">()))
                        {
                            return boolean(self.attr("text") <= other.attr("text"));
                        }
                        return boolean(self.attr("text") <= other);
                    }) },
                { "__lt__", w.method("QName", "__lt__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (isinstance(other, G<"xml.etree.ElementTree.QName">()))
                        {
                            return boolean(self.attr("text") < other.attr("text"));
                        }
                        return boolean(self.attr("text") < other);
                    }) },
                { "__ge__", w.method("QName", "__ge__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (isinstance(other, G<"xml.etree.ElementTree.QName">()))
                        {
                            return boolean(self.attr("text") >= other.attr("text"));
                        }
                        return boolean(self.attr("text") >= other);
                    }) },
                { "__gt__", w.method("QName", "__gt__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (isinstance(other, G<"xml.etree.ElementTree.QName">()))
                        {
                            return boolean(self.attr("text") > other.attr("text"));
                        }
                        return boolean(self.attr("text") > other);
                    }) },
                { "__eq__", w.method("QName", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (isinstance(other, G<"xml.etree.ElementTree.QName">()))
                        {
                            return boolean(self.attr("text") == other.attr("text"));
                        }
                        return boolean(self.attr("text") == other);
                    }) }, }));

            // --------------------------------------------------------------------
            define("xml.etree.ElementTree", "ElementTree", w.make_class("ElementTree", {  }, {
                { "__init__", w.method("ElementTree", "__init__", { py_positional("element", none()), py_positional("file", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py element = a[1];
                        py file = a[2];
                        if (((!element.is_none()) && (!(G<"xml.etree.ElementTree.iselement">())(element).truthy())))
                        {
                            py_raise((B<"TypeError">())((S<"expected an Element, not %s">()) % (B<"type">())(element).attr("__name__")));
                        }
                        self.set_attr("_root", element);
                        if (file.truthy())
                        {
                            self.method("parse", { file });
                        }
                        return none();
                    }) },
                { "getroot", w.method("ElementTree", "getroot", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_root");
                    }) },
                { "_setroot", w.method("ElementTree", "_setroot", { py_positional("element") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py element = a[1];
                        if (!(G<"xml.etree.ElementTree.iselement">())(element).truthy())
                        {
                            py_raise((B<"TypeError">())((S<"expected an Element, not %s">()) % (B<"type">())(element).attr("__name__")));
                        }
                        self.set_attr("_root", element);
                        return none();
                    }) },
                { "parse", w.method("ElementTree", "parse", { py_positional("source"), py_positional("parser", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py source = a[1];
                        py parser = a[2];
                        py close_source;
                        py data;
                        close_source = boolean(false);
                        if (!has_attr(source, "read"))
                        {
                            source = (B<"open">())(source, S<"rb">());
                            close_source = boolean(true);
                        }
                        {
                            std::optional<py> pending6;
                            [[maybe_unused]] int flow6 = 0;
                            [[maybe_unused]] py result6;
                            try
                            {
                                if (parser.is_none())
                                {
                                    // If no parser was specified, create a default XMLParser
                                    parser = (G<"xml.etree.ElementTree.XMLParser">())();
                                    if (has_attr(parser, "_parse_whole"))
                                    {
                                        // The default XMLParser, when it comes from an accelerator,
                                        // can define an internal _parse_whole API for efficiency.
                                        // It can be used to parse the whole source without feeding
                                        // it with chunks.
                                        self.set_attr("_root", parser.method("_parse_whole", { source }));
                                        result6 = self.attr("_root");
                                        flow6 = 1;
                                        goto finally6;
                                    }
                                }
                                while (([&]() -> py { data = source.method("read", { I<65536>() }); return data; }()).truthy())
                                {
                                    parser.method("feed", { data });
                                }
                                self.set_attr("_root", parser.method("close"));
                                result6 = self.attr("_root");
                                flow6 = 1;
                                goto finally6;
                            }
                            catch (raised const&)
                            {
                                pending6 = py_take_exception();
                            }
                            finally6:
                            if (close_source.truthy())
                            {
                                source.method("close");
                            }
                            if (pending6)
                                py_reraise(*pending6);
                            if (flow6 == 1)
                                return result6;
                        }
                        return none();
                    }) },
                { "iter", w.method("ElementTree", "iter", { py_positional("tag", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tag = a[1];
                        // assert self._root is not None
                        return self.attr("_root").method("iter", { tag });
                    }) },
                { "find", w.method("ElementTree", "find", { py_positional("path"), py_positional("namespaces", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py namespaces = a[2];
                        // assert self._root is not None
                        if (path[B<"slice">()(none(), I<1>(), none())] == (S<"/">()))
                        {
                            path = (S<".">()) + path;
                            (Mod<"warnings">().attr("warn")).call({ (S<"This search is broken in 1.3 and earlier, and will be fixed in a future version.  If you rely on the current behaviour, change it to %r">()) % path, B<"FutureWarning">() }, { { "stacklevel", I<1>() } });
                        }
                        return self.attr("_root").method("find", { path, namespaces });
                    }) },
                { "findtext", w.method("ElementTree", "findtext", { py_positional("path"), py_positional("default", none()), py_positional("namespaces", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py default_ = a[2];
                        py namespaces = a[3];
                        // assert self._root is not None
                        if (path[B<"slice">()(none(), I<1>(), none())] == (S<"/">()))
                        {
                            path = (S<".">()) + path;
                            (Mod<"warnings">().attr("warn")).call({ (S<"This search is broken in 1.3 and earlier, and will be fixed in a future version.  If you rely on the current behaviour, change it to %r">()) % path, B<"FutureWarning">() }, { { "stacklevel", I<1>() } });
                        }
                        return self.attr("_root").method("findtext", { path, default_, namespaces });
                    }) },
                { "findall", w.method("ElementTree", "findall", { py_positional("path"), py_positional("namespaces", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py namespaces = a[2];
                        // assert self._root is not None
                        if (path[B<"slice">()(none(), I<1>(), none())] == (S<"/">()))
                        {
                            path = (S<".">()) + path;
                            (Mod<"warnings">().attr("warn")).call({ (S<"This search is broken in 1.3 and earlier, and will be fixed in a future version.  If you rely on the current behaviour, change it to %r">()) % path, B<"FutureWarning">() }, { { "stacklevel", I<1>() } });
                        }
                        return self.attr("_root").method("findall", { path, namespaces });
                    }) },
                { "iterfind", w.method("ElementTree", "iterfind", { py_positional("path"), py_positional("namespaces", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py namespaces = a[2];
                        // assert self._root is not None
                        if (path[B<"slice">()(none(), I<1>(), none())] == (S<"/">()))
                        {
                            path = (S<".">()) + path;
                            (Mod<"warnings">().attr("warn")).call({ (S<"This search is broken in 1.3 and earlier, and will be fixed in a future version.  If you rely on the current behaviour, change it to %r">()) % path, B<"FutureWarning">() }, { { "stacklevel", I<1>() } });
                        }
                        return self.attr("_root").method("iterfind", { path, namespaces });
                    }) },
                { "write", w.method("ElementTree", "write", { py_positional("file_or_filename"), py_positional("encoding", none()), py_positional("xml_declaration", none()), py_positional("default_namespace", none()), py_positional("method", none()), py_keyword("short_empty_elements", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file_or_filename = a[1];
                        py encoding = a[2];
                        py xml_declaration = a[3];
                        py default_namespace = a[4];
                        py method = a[5];
                        py short_empty_elements = a[6];
                        py declared_encoding;
                        py namespaces;
                        py qnames;
                        py serialize;
                        py write;
                        if (self.attr("_root").is_none())
                        {
                            py_raise((B<"TypeError">())(S<"ElementTree not initialized">()));
                        }
                        if (!method.truthy())
                        {
                            method = S<"xml">();
                        }
                        else if (!(G<"xml.etree.ElementTree._serialize">()).contains(method))
                        {
                            py_raise((B<"ValueError">())((S<"unknown method %r">()) % method));
                        }
                        if (!encoding.truthy())
                        {
                            if (method == (S<"c14n">()))
                            {
                                encoding = S<"utf-8">();
                            }
                            else
                            {
                                encoding = S<"us-ascii">();
                            }
                        }
                        {
                            py const manager8 = (G<"xml.etree.ElementTree._get_writer">())(file_or_filename, encoding);
                            py const exit8 = pyclass::special_method(manager8, "__exit__");
                            py const value8 = pyclass::special_method(manager8, "__enter__")(manager8);
                            auto const items9 = unpack(value8, 2);
                            write = items9[0];
                            declared_encoding = items9[1];
                            [[maybe_unused]] int flow8 = 0;
                            [[maybe_unused]] py result8;
                            bool exited8 = false;
                            try
                            {
                                if (((method == (S<"xml">())) && ([&]() -> py { py const t = xml_declaration; return t.truthy() ? t : [&]() -> py { py const t = boolean(xml_declaration.is_none()); return !t.truthy() ? t : [&]() -> py { py const t = boolean(encoding.method("lower") != (S<"unicode">())); return !t.truthy() ? t : boolean(!py_tuple({ S<"utf-8">(), S<"us-ascii">() }).contains(declared_encoding.method("lower"))); }(); }(); }()).truthy()))
                                {
                                    write((S<"<?xml version='1.0' encoding='%s'?>\n">()) % py_tuple({ declared_encoding }));
                                }
                                if (method == (S<"text">()))
                                {
                                    (G<"xml.etree.ElementTree._serialize_text">())(write, self.attr("_root"));
                                }
                                else
                                {
                                    auto const items10 = unpack((G<"xml.etree.ElementTree._namespaces">())(self.attr("_root"), default_namespace), 2);
                                    qnames = items10[0];
                                    namespaces = items10[1];
                                    serialize = (G<"xml.etree.ElementTree._serialize">())[method];
                                    serialize.call({ write, self.attr("_root"), qnames, namespaces }, { { "short_empty_elements", short_empty_elements } });
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler8;
                                exited8 = true;
                                if (!exit8(manager8, type_of(handler8.exception), handler8.exception, handler8.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler8.exception);
                            }
                            finally8:
                            if (!exited8)
                                exit8(manager8, none(), none(), none());
                            if (flow8 == 1)
                                return result8;
                        }
                        return none();
                    }) },
                { "write_c14n", w.method("ElementTree", "write_c14n", { py_positional("file") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        return self.method("write", { file }, { { "method", S<"c14n">() } });
                    }) }, }));

            // --------------------------------------------------------------------
            // serialization support
            define("xml.etree.ElementTree", "_get_writer", (Mod<"contextlib">().attr("contextmanager"))(w.function("_get_writer", "_get_writer", { py_positional("file_or_filename"), py_positional("encoding") }, [](arguments a) -> py
                {
                    return make_native_generator("_get_writer", "_get_writer", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py file_or_filename = a[0];
                        py encoding = a[1];
                        py file;
                        py stack;
                        py write;
                        {
                            bool otherwise11 = false;
                            try
                            {
                                write = file_or_filename.attr("write");
                                otherwise11 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler11;
                                if (matches(handler11.exception, B<"AttributeError">()))
                                {
                                    // file_or_filename is a file name
                                    if (encoding.method("lower") == (S<"unicode">()))
                                    {
                                        encoding = S<"utf-8">();
                                    }
                                    {
                                        py const manager12 = (B<"open">()).call({ file_or_filename, S<"w">() }, { { "encoding", encoding }, { "errors", S<"xmlcharrefreplace">() } });
                                        py const exit12 = pyclass::special_method(manager12, "__exit__");
                                        py const value12 = pyclass::special_method(manager12, "__enter__")(manager12);
                                        file = value12;
                                        [[maybe_unused]] int flow12 = 0;
                                        [[maybe_unused]] py result12;
                                        bool exited12 = false;
                                        try
                                        {
                                            py_yield(py_tuple({ file.attr("write"), encoding }));
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler12;
                                            exited12 = true;
                                            if (!exit12(manager12, type_of(handler12.exception), handler12.exception, handler12.exception.attr("__traceback__")).truthy())
                                                py_reraise(handler12.exception);
                                        }
                                        finally12:
                                        if (!exited12)
                                            exit12(manager12, none(), none(), none());
                                        if (flow12 == 1)
                                            return result12;
                                    }
                                }
                                else
                                    py_reraise(handler11.exception);
                            }
                            if (otherwise11)
                            {
                                // file_or_filename is a file-like object
                                // encoding determines if it is a text or binary writer
                                if (encoding.method("lower") == (S<"unicode">()))
                                {
                                    // use a text writer as is
                                    py_yield(py_tuple({ write, [&]() -> py { py const t = (B<"getattr">())(file_or_filename, S<"encoding">(), none()); return t.truthy() ? t : S<"utf-8">(); }() }));
                                }
                                else
                                {
                                    // wrap a binary writer with TextIOWrapper
                                    {
                                        py const manager13 = (Mod<"contextlib">().attr("ExitStack"))();
                                        py const exit13 = pyclass::special_method(manager13, "__exit__");
                                        py const value13 = pyclass::special_method(manager13, "__enter__")(manager13);
                                        stack = value13;
                                        [[maybe_unused]] int flow13 = 0;
                                        [[maybe_unused]] py result13;
                                        bool exited13 = false;
                                        try
                                        {
                                            if (isinstance(file_or_filename, Mod<"io">().attr("BufferedIOBase")))
                                            {
                                                file = file_or_filename;
                                            }
                                            else if (isinstance(file_or_filename, Mod<"io">().attr("RawIOBase")))
                                            {
                                                file = (Mod<"io">().attr("BufferedWriter"))(file_or_filename);
                                                // Keep the original file open when the BufferedWriter is
                                                // destroyed
                                                stack.method("callback", { file.attr("detach") });
                                            }
                                            else
                                            {
                                                // This is to handle passed objects that aren't in the
                                                // IOBase hierarchy, but just have a write method
                                                file = (Mod<"io">().attr("BufferedIOBase"))();
                                                file.set_attr("writable", w.function("<lambda>", "_get_writer.<locals>.<lambda>", {  }, [](arguments a) -> py
                            {
                                return boolean(true);
                            }));
                                                file.set_attr("write", write);
                                                try
                                                {
                                                    // TextIOWrapper uses this methods to determine
                                                    // if BOM (for UTF-16, etc) should be added
                                                    file.set_attr("seekable", file_or_filename.attr("seekable"));
                                                    file.set_attr("tell", file_or_filename.attr("tell"));
                                                }
                                                catch (raised const&)
                                                {
                                                    pyclass::handler_scope handler14;
                                                    if (matches(handler14.exception, B<"AttributeError">()))
                                                    {
                                                    }
                                                    else
                                                        py_reraise(handler14.exception);
                                                }
                                            }
                                            file = (Mod<"io">().attr("TextIOWrapper")).call({ file }, { { "encoding", encoding }, { "errors", S<"xmlcharrefreplace">() }, { "newline", S<"\n">() } });
                                            // Keep the original file open when the TextIOWrapper is
                                            // destroyed
                                            stack.method("callback", { file.attr("detach") });
                                            py_yield(py_tuple({ file.attr("write"), encoding }));
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler13;
                                            exited13 = true;
                                            if (!exit13(manager13, type_of(handler13.exception), handler13.exception, handler13.exception.attr("__traceback__")).truthy())
                                                py_reraise(handler13.exception);
                                        }
                                        finally13:
                                        if (!exited13)
                                            exit13(manager13, none(), none(), none());
                                        if (flow13 == 1)
                                            return result13;
                                    }
                                }
                            }
                        }
                        return none();
                    });
                })));

            define("xml.etree.ElementTree", "_namespaces", w.function("_namespaces", "_namespaces", { py_positional("elem"), py_positional("default_namespace", none()) }, [](arguments a) -> py
                {
                    py elem = a[0];
                    auto const default_namespace = std::make_shared<py>(a[1]);
                    py add_qname;
                    py key;
                    auto const namespaces = std::make_shared<py>();
                    auto const qnames = std::make_shared<py>();
                    auto const tag = std::make_shared<py>();
                    py text;
                    py value;
                    *qnames = dict_of({ { none(), none() } });
                    // maps uri:s to prefixes
                    *namespaces = py_dict();
                    if ((*default_namespace).truthy())
                    {
                        (*namespaces).set_item((*default_namespace), S<"">());
                    }
                    add_qname = w.function("add_qname", "_namespaces.<locals>.add_qname", { py_positional("qname") }, [default_namespace, namespaces, qnames](arguments a) -> py
    {
        py qname = a[0];
        py prefix;
        py tag;
        py uri;
        try
        {
            if (qname[B<"slice">()(none(), I<1>(), none())] == (S<"{">()))
            {
                auto const items16 = unpack(qname[B<"slice">()(I<1>(), none(), none())].method("rsplit", { S<"}">(), I<1>() }), 2);
                uri = items16[0];
                tag = items16[1];
                prefix = (*namespaces).method("get", { uri });
                if (prefix.is_none())
                {
                    prefix = (G<"xml.etree.ElementTree._namespace_map">()).method("get", { uri });
                    if (prefix.is_none())
                    {
                        prefix = (S<"ns%d">()) % py{ (*namespaces).size() };
                    }
                    if (prefix != (S<"xml">()))
                    {
                        (*namespaces).set_item(uri, prefix);
                    }
                }
                if (prefix.truthy())
                {
                    (*qnames).set_item(qname, (S<"%s:%s">()) % py_tuple({ prefix, tag }));
                }
                else
                {
                    (*qnames).set_item(qname, tag);
                }
            }
            else
            {
                if ((*default_namespace).truthy())
                {
                    // FIXME: can this be handled in XML 1.0?
                    py_raise((B<"ValueError">())(S<"cannot use non-qualified names with default_namespace option">()));
                }
                (*qnames).set_item(qname, qname);
            }
        }
        catch (raised const&)
        {
            pyclass::handler_scope handler15;
            if (matches(handler15.exception, B<"TypeError">()))
            {
                (G<"xml.etree.ElementTree._raise_serialization_error">())(qname);
            }
            else
                py_reraise(handler15.exception);
        }
        return none();
    });
                    // populate qname and namespaces table
                    for (py const& item : iterate{ elem.method("iter") })
                    {
                        elem = item;
                        *tag = elem.attr("tag");
                        if (isinstance((*tag), G<"xml.etree.ElementTree.QName">()))
                        {
                            if (!(*qnames).contains((*tag).attr("text")))
                            {
                                add_qname((*tag).attr("text"));
                            }
                        }
                        else if (isinstance((*tag), B<"str">()))
                        {
                            if (!(*qnames).contains((*tag)))
                            {
                                add_qname((*tag));
                            }
                        }
                        else if (((!(*tag).is_none()) && (!(*tag).is((G<"xml.etree.ElementTree.Comment">()))) && (!(*tag).is((G<"xml.etree.ElementTree.PI">())))))
                        {
                            (G<"xml.etree.ElementTree._raise_serialization_error">())((*tag));
                        }
                        for (py const& item : iterate{ elem.method("items") })
                        {
                            auto const items19 = unpack(item, 2);
                            key = items19[0];
                            value = items19[1];
                            if (isinstance(key, G<"xml.etree.ElementTree.QName">()))
                            {
                                key = key.attr("text");
                            }
                            if (!(*qnames).contains(key))
                            {
                                add_qname(key);
                            }
                            if ((isinstance(value, G<"xml.etree.ElementTree.QName">()) && (!(*qnames).contains(value.attr("text")))))
                            {
                                add_qname(value.attr("text"));
                            }
                        }
                        text = elem.attr("text");
                        if ((isinstance(text, G<"xml.etree.ElementTree.QName">()) && (!(*qnames).contains(text.attr("text")))))
                        {
                            add_qname(text.attr("text"));
                        }
                    }
                    return py_tuple({ (*qnames), (*namespaces) });
                }));

            define("xml.etree.ElementTree", "_serialize_xml", w.function("_serialize_xml", "_serialize_xml", { py_positional("write"), py_positional("elem"), py_positional("qnames"), py_positional("namespaces"), py_positional("short_empty_elements") }, [](arguments a) -> py
                {
                    py write = a[0];
                    py elem = a[1];
                    py qnames = a[2];
                    py namespaces = a[3];
                    py short_empty_elements = a[4];
                    py kwargs = a[5];
                    py e;
                    py items;
                    py k;
                    py tag;
                    py text;
                    py v;
                    tag = elem.attr("tag");
                    text = elem.attr("text");
                    if (tag.is((G<"xml.etree.ElementTree.Comment">())))
                    {
                        write((S<"<!--%s-->">()) % text);
                    }
                    else if (tag.is((G<"xml.etree.ElementTree.ProcessingInstruction">())))
                    {
                        write((S<"<?%s?>">()) % text);
                    }
                    else
                    {
                        tag = qnames[tag];
                        if (tag.is_none())
                        {
                            if (text.truthy())
                            {
                                write((G<"xml.etree.ElementTree._escape_cdata">())(text));
                            }
                            for (py const& item : iterate{ elem })
                            {
                                e = item;
                                (G<"xml.etree.ElementTree._serialize_xml">()).call({ write, e, qnames, none() }, { { "short_empty_elements", short_empty_elements } });
                            }
                        }
                        else
                        {
                            write((S<"<">()) + tag);
                            items = (B<"list">())(elem.method("items"));
                            if ((items.truthy() || namespaces.truthy()))
                            {
                                if (namespaces.truthy())
                                {
                                    for (py const& item : iterate{ (B<"sorted">()).call({ namespaces.method("items") }, { { "key", w.function("<lambda>", "_serialize_xml.<locals>.<lambda>", { py_positional("x") }, [](arguments a) -> py
                    {
                        py x = a[0];
                        return x[I<1>()];
                    }) } }) })
                                    {
                                        auto const items22 = unpack(item, 2);
                                        v = items22[0];
                                        k = items22[1];
                                        if (k.truthy())
                                        {
                                            k = (S<":">()) + k;
                                        }
                                        write((S<" xmlns%s=\"%s\"">()) % py_tuple({ k, (G<"xml.etree.ElementTree._escape_attrib">())(v) }));
                                    }
                                }
                                for (py const& item : iterate{ items })
                                {
                                    auto const items24 = unpack(item, 2);
                                    k = items24[0];
                                    v = items24[1];
                                    if (isinstance(k, G<"xml.etree.ElementTree.QName">()))
                                    {
                                        k = k.attr("text");
                                    }
                                    if (isinstance(v, G<"xml.etree.ElementTree.QName">()))
                                    {
                                        v = qnames[v.attr("text")];
                                    }
                                    else
                                    {
                                        v = (G<"xml.etree.ElementTree._escape_attrib">())(v);
                                    }
                                    write((S<" %s=\"%s\"">()) % py_tuple({ qnames[k], v }));
                                }
                            }
                            if ((text.truthy() || py{ elem.size() }.truthy() || (!short_empty_elements.truthy())))
                            {
                                write(S<">">());
                                if (text.truthy())
                                {
                                    write((G<"xml.etree.ElementTree._escape_cdata">())(text));
                                }
                                for (py const& item : iterate{ elem })
                                {
                                    e = item;
                                    (G<"xml.etree.ElementTree._serialize_xml">()).call({ write, e, qnames, none() }, { { "short_empty_elements", short_empty_elements } });
                                }
                                write(((S<"</">()) + tag) + (S<">">()));
                            }
                            else
                            {
                                write(S<" />">());
                            }
                        }
                    }
                    if (elem.attr("tail").truthy())
                    {
                        write((G<"xml.etree.ElementTree._escape_cdata">())(elem.attr("tail")));
                    }
                    return none();
                }, false, true));

            define("xml.etree.ElementTree", "_CDATA_CONTENT_ELEMENTS", set_of({ S<"script">(), S<"style">(), S<"xmp">(), S<"iframe">(), S<"noembed">(), S<"noframes">(), S<"plaintext">() }));
            define("xml.etree.ElementTree", "HTML_EMPTY", set_of({ S<"area">(), S<"base">(), S<"basefont">(), S<"br">(), S<"col">(), S<"embed">(), S<"frame">(), S<"hr">(), S<"img">(), S<"input">(), S<"isindex">(), S<"link">(), S<"meta">(), S<"param">(), S<"source">(), S<"track">(), S<"wbr">(), S<"plaintext">() }));
            define("xml.etree.ElementTree", "_serialize_html", w.function("_serialize_html", "_serialize_html", { py_positional("write"), py_positional("elem"), py_positional("qnames"), py_positional("namespaces") }, [](arguments a) -> py
                {
                    py write = a[0];
                    py elem = a[1];
                    py qnames = a[2];
                    py namespaces = a[3];
                    py kwargs = a[4];
                    py e;
                    py items;
                    py k;
                    py ltag;
                    py tag;
                    py text;
                    py v;
                    tag = elem.attr("tag");
                    text = elem.attr("text");
                    if (tag.is((G<"xml.etree.ElementTree.Comment">())))
                    {
                        write((S<"<!--%s-->">()) % (G<"xml.etree.ElementTree._escape_cdata">())(text));
                    }
                    else if (tag.is((G<"xml.etree.ElementTree.ProcessingInstruction">())))
                    {
                        write((S<"<?%s?>">()) % (G<"xml.etree.ElementTree._escape_cdata">())(text));
                    }
                    else
                    {
                        tag = qnames[tag];
                        if (tag.is_none())
                        {
                            if (text.truthy())
                            {
                                write((G<"xml.etree.ElementTree._escape_cdata">())(text));
                            }
                            for (py const& item : iterate{ elem })
                            {
                                e = item;
                                (G<"xml.etree.ElementTree._serialize_html">())(write, e, qnames, none());
                            }
                        }
                        else
                        {
                            write((S<"<">()) + tag);
                            items = (B<"list">())(elem.method("items"));
                            if ((items.truthy() || namespaces.truthy()))
                            {
                                if (namespaces.truthy())
                                {
                                    for (py const& item : iterate{ (B<"sorted">()).call({ namespaces.method("items") }, { { "key", w.function("<lambda>", "_serialize_html.<locals>.<lambda>", { py_positional("x") }, [](arguments a) -> py
                    {
                        py x = a[0];
                        return x[I<1>()];
                    }) } }) })
                                    {
                                        auto const items28 = unpack(item, 2);
                                        v = items28[0];
                                        k = items28[1];
                                        if (k.truthy())
                                        {
                                            k = (S<":">()) + k;
                                        }
                                        write((S<" xmlns%s=\"%s\"">()) % py_tuple({ k, (G<"xml.etree.ElementTree._escape_attrib">())(v) }));
                                    }
                                }
                                for (py const& item : iterate{ items })
                                {
                                    auto const items30 = unpack(item, 2);
                                    k = items30[0];
                                    v = items30[1];
                                    if (isinstance(k, G<"xml.etree.ElementTree.QName">()))
                                    {
                                        k = k.attr("text");
                                    }
                                    if (isinstance(v, G<"xml.etree.ElementTree.QName">()))
                                    {
                                        v = qnames[v.attr("text")];
                                    }
                                    else
                                    {
                                        v = (G<"xml.etree.ElementTree._escape_attrib_html">())(v);
                                    }
                                    // FIXME: handle boolean attributes
                                    write((S<" %s=\"%s\"">()) % py_tuple({ qnames[k], v }));
                                }
                            }
                            write(S<">">());
                            ltag = tag.method("lower");
                            if (text.truthy())
                            {
                                if ((G<"xml.etree.ElementTree._CDATA_CONTENT_ELEMENTS">()).contains(ltag))
                                {
                                    write(text);
                                }
                                else
                                {
                                    write((G<"xml.etree.ElementTree._escape_cdata">())(text));
                                }
                            }
                            for (py const& item : iterate{ elem })
                            {
                                e = item;
                                (G<"xml.etree.ElementTree._serialize_html">())(write, e, qnames, none());
                            }
                            if (!(G<"xml.etree.ElementTree.HTML_EMPTY">()).contains(ltag))
                            {
                                write(((S<"</">()) + tag) + (S<">">()));
                            }
                        }
                    }
                    if (elem.attr("tail").truthy())
                    {
                        write((G<"xml.etree.ElementTree._escape_cdata">())(elem.attr("tail")));
                    }
                    return none();
                }, false, true));

            define("xml.etree.ElementTree", "_serialize_text", w.function("_serialize_text", "_serialize_text", { py_positional("write"), py_positional("elem") }, [](arguments a) -> py
                {
                    py write = a[0];
                    py elem = a[1];
                    py part;
                    for (py const& item : iterate{ elem.method("itertext") })
                    {
                        part = item;
                        write(part);
                    }
                    if (elem.attr("tail").truthy())
                    {
                        write(elem.attr("tail"));
                    }
                    return none();
                }));

            define("xml.etree.ElementTree", "_serialize", dict_of({ { S<"xml">(), G<"xml.etree.ElementTree._serialize_xml">() }, { S<"html">(), G<"xml.etree.ElementTree._serialize_html">() }, { S<"text">(), G<"xml.etree.ElementTree._serialize_text">() } }));
            // this optional method is imported at the end of the module
            // "c14n": _serialize_c14n,
            define("xml.etree.ElementTree", "register_namespace", w.function("register_namespace", "register_namespace", { py_positional("prefix"), py_positional("uri") }, [](arguments a) -> py
                {
                    py prefix = a[0];
                    py uri = a[1];
                    py k;
                    py v;
                    if ((Mod<"re">().attr("match"))(S<"ns\\d+$">(), prefix).truthy())
                    {
                        py_raise((B<"ValueError">())(S<"Prefix format reserved for internal use">()));
                    }
                    for (py const& item : iterate{ (B<"list">())((G<"xml.etree.ElementTree._namespace_map">()).method("items")) })
                    {
                        auto const items34 = unpack(item, 2);
                        k = items34[0];
                        v = items34[1];
                        if (((k == uri) || (v == prefix)))
                        {
                            del_item(G<"xml.etree.ElementTree._namespace_map">(), k);
                        }
                    }
                    (G<"xml.etree.ElementTree._namespace_map">()).set_item(uri, prefix);
                    return none();
                }));

            define("xml.etree.ElementTree", "_namespace_map", dict_of({ { S<"http://www.w3.org/XML/1998/namespace">(), S<"xml">() }, { S<"http://www.w3.org/1999/xhtml">(), S<"html">() }, { S<"http://www.w3.org/1999/02/22-rdf-syntax-ns#">(), S<"rdf">() }, { S<"http://schemas.xmlsoap.org/wsdl/">(), S<"wsdl">() }, { S<"http://www.w3.org/2001/XMLSchema">(), S<"xs">() }, { S<"http://www.w3.org/2001/XMLSchema-instance">(), S<"xsi">() }, { S<"http://purl.org/dc/elements/1.1/">(), S<"dc">() } }));
            // "well-known" namespace prefixes
            // xml schema
            // dublin core
            // For tests and troubleshooting
            (G<"xml.etree.ElementTree.register_namespace">()).set_attr("_namespace_map", G<"xml.etree.ElementTree._namespace_map">());
            define("xml.etree.ElementTree", "_raise_serialization_error", w.function("_raise_serialization_error", "_raise_serialization_error", { py_positional("text") }, [](arguments a) -> py
                {
                    py text = a[0];
                    py_raise((B<"TypeError">())((S<"cannot serialize %r (type %s)">()) % py_tuple({ text, (B<"type">())(text).attr("__name__") })));
                }));

            define("xml.etree.ElementTree", "_escape_cdata", w.function("_escape_cdata", "_escape_cdata", { py_positional("text") }, [](arguments a) -> py
                {
                    py text = a[0];
                    try
                    {
                        // it's worth avoiding do-nothing calls for strings that are
                        // shorter than 500 characters, or so.  assume that's, by far,
                        // the most common case in most applications.
                        if (text.contains((S<"&">())))
                        {
                            text = text.method("replace", { S<"&">(), S<"&amp;">() });
                        }
                        if (text.contains((S<"<">())))
                        {
                            text = text.method("replace", { S<"<">(), S<"&lt;">() });
                        }
                        if (text.contains((S<">">())))
                        {
                            text = text.method("replace", { S<">">(), S<"&gt;">() });
                        }
                        return text;
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler35;
                        if (matches(handler35.exception, py_tuple({ B<"TypeError">(), B<"AttributeError">() })))
                        {
                            (G<"xml.etree.ElementTree._raise_serialization_error">())(text);
                        }
                        else
                            py_reraise(handler35.exception);
                    }
                    return none();
                }));

            define("xml.etree.ElementTree", "_escape_attrib", w.function("_escape_attrib", "_escape_attrib", { py_positional("text") }, [](arguments a) -> py
                {
                    py text = a[0];
                    try
                    {
                        if (text.contains((S<"&">())))
                        {
                            text = text.method("replace", { S<"&">(), S<"&amp;">() });
                        }
                        if (text.contains((S<"<">())))
                        {
                            text = text.method("replace", { S<"<">(), S<"&lt;">() });
                        }
                        if (text.contains((S<">">())))
                        {
                            text = text.method("replace", { S<">">(), S<"&gt;">() });
                        }
                        if (text.contains((S<"\"">())))
                        {
                            text = text.method("replace", { S<"\"">(), S<"&quot;">() });
                        }
                        // Although section 2.11 of the XML specification states that CR or
                        // CR LN should be replaced with just LN, it applies only to EOLNs
                        // which take part of organizing file into lines. Within attributes,
                        // we are replacing these with entity numbers, so they do not count.
                        // http://www.w3.org/TR/REC-xml/#sec-line-ends
                        // The current solution, contained in following six lines, was
                        // discussed in issue 17582 and 39011.
                        if (text.contains((S<"\r">())))
                        {
                            text = text.method("replace", { S<"\r">(), S<"&#13;">() });
                        }
                        if (text.contains((S<"\n">())))
                        {
                            text = text.method("replace", { S<"\n">(), S<"&#10;">() });
                        }
                        if (text.contains((S<"\t">())))
                        {
                            text = text.method("replace", { S<"\t">(), S<"&#09;">() });
                        }
                        return text;
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler36;
                        if (matches(handler36.exception, py_tuple({ B<"TypeError">(), B<"AttributeError">() })))
                        {
                            (G<"xml.etree.ElementTree._raise_serialization_error">())(text);
                        }
                        else
                            py_reraise(handler36.exception);
                    }
                    return none();
                }));

            define("xml.etree.ElementTree", "_escape_attrib_html", w.function("_escape_attrib_html", "_escape_attrib_html", { py_positional("text") }, [](arguments a) -> py
                {
                    py text = a[0];
                    try
                    {
                        if (text.contains((S<"&">())))
                        {
                            text = text.method("replace", { S<"&">(), S<"&amp;">() });
                        }
                        if (text.contains((S<">">())))
                        {
                            text = text.method("replace", { S<">">(), S<"&gt;">() });
                        }
                        if (text.contains((S<"\"">())))
                        {
                            text = text.method("replace", { S<"\"">(), S<"&quot;">() });
                        }
                        return text;
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler37;
                        if (matches(handler37.exception, py_tuple({ B<"TypeError">(), B<"AttributeError">() })))
                        {
                            (G<"xml.etree.ElementTree._raise_serialization_error">())(text);
                        }
                        else
                            py_reraise(handler37.exception);
                    }
                    return none();
                }));

            // --------------------------------------------------------------------
            define("xml.etree.ElementTree", "tostring", w.function("tostring", "tostring", { py_positional("element"), py_positional("encoding", none()), py_positional("method", none()), py_keyword("xml_declaration", none()), py_keyword("default_namespace", none()), py_keyword("short_empty_elements", boolean(true)) }, [](arguments a) -> py
                {
                    py element = a[0];
                    py encoding = a[1];
                    py method = a[2];
                    py xml_declaration = a[3];
                    py default_namespace = a[4];
                    py short_empty_elements = a[5];
                    py stream;
                    stream = (encoding == (S<"unicode">()) ? (Mod<"io">().attr("StringIO"))() : (Mod<"io">().attr("BytesIO"))());
                    (G<"xml.etree.ElementTree.ElementTree">())(element).method("write", { stream, encoding }, { { "xml_declaration", xml_declaration }, { "default_namespace", default_namespace }, { "method", method }, { "short_empty_elements", short_empty_elements } });
                    return stream.method("getvalue");
                }));

            define("xml.etree.ElementTree", "_ListDataStream", w.make_class("_ListDataStream", { Mod<"io">().attr("BufferedIOBase") }, {
                { "__init__", w.method("_ListDataStream", "__init__", { py_positional("lst") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py lst = a[1];
                        self.set_attr("lst", lst);
                        return none();
                    }) },
                { "writable", w.method("_ListDataStream", "writable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(true);
                    }) },
                { "seekable", w.method("_ListDataStream", "seekable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(true);
                    }) },
                { "write", w.method("_ListDataStream", "write", { py_positional("b") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py b = a[1];
                        self.attr("lst").method("append", { b });
                        return none();
                    }) },
                { "tell", w.method("_ListDataStream", "tell", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py{ self.attr("lst").size() };
                    }) }, }));

            define("xml.etree.ElementTree", "tostringlist", w.function("tostringlist", "tostringlist", { py_positional("element"), py_positional("encoding", none()), py_positional("method", none()), py_keyword("xml_declaration", none()), py_keyword("default_namespace", none()), py_keyword("short_empty_elements", boolean(true)) }, [](arguments a) -> py
                {
                    py element = a[0];
                    py encoding = a[1];
                    py method = a[2];
                    py xml_declaration = a[3];
                    py default_namespace = a[4];
                    py short_empty_elements = a[5];
                    py lst;
                    py stream;
                    lst = py_list();
                    stream = (G<"xml.etree.ElementTree._ListDataStream">())(lst);
                    (G<"xml.etree.ElementTree.ElementTree">())(element).method("write", { stream, encoding }, { { "xml_declaration", xml_declaration }, { "default_namespace", default_namespace }, { "method", method }, { "short_empty_elements", short_empty_elements } });
                    return lst;
                }));

            define("xml.etree.ElementTree", "dump", w.function("dump", "dump", { py_positional("elem") }, [](arguments a) -> py
                {
                    py elem = a[0];
                    py tail;
                    // debugging
                    if (!isinstance(elem, G<"xml.etree.ElementTree.ElementTree">()))
                    {
                        elem = (G<"xml.etree.ElementTree.ElementTree">())(elem);
                    }
                    elem.method("write", { Mod<"sys">().attr("stdout") }, { { "encoding", S<"unicode">() } });
                    tail = elem.method("getroot").attr("tail");
                    if (((!tail.truthy()) || (tail[I<-1>()] != (S<"\n">()))))
                    {
                        (Mod<"sys">().attr("stdout")).method("write", { S<"\n">() });
                    }
                    return none();
                }));

            define("xml.etree.ElementTree", "indent", w.function("indent", "indent", { py_positional("tree"), py_positional("space", S<"  ">()), py_positional("level", I<0>()) }, [](arguments a) -> py
                {
                    py tree = a[0];
                    auto const space = std::make_shared<py>(a[1]);
                    py level = a[2];
                    auto const _indent_children = std::make_shared<py>();
                    auto const indentations = std::make_shared<py>();
                    if (isinstance(tree, G<"xml.etree.ElementTree.ElementTree">()))
                    {
                        tree = tree.method("getroot");
                    }
                    if (level < (I<0>()))
                    {
                        py_raise((B<"ValueError">())(S(std::string{ "Initial indentation level must be >= 0, got " } + level.str())));
                    }
                    if (!py{ tree.size() }.truthy())
                    {
                        return none();
                    }
                    // Reduce the memory consumption by reusing indentation strings.
                    *indentations = py_list({ (S<"\n">()) + (level * (*space)) });
                    *_indent_children = w.function("_indent_children", "indent.<locals>._indent_children", { py_positional("elem"), py_positional("level") }, [_indent_children, indentations, space](arguments a) -> py
    {
        py elem = a[0];
        py level = a[1];
        py child;
        py child_indentation;
        py child_level;
        child_level = level + (I<1>());
        try
        {
            child_indentation = (*indentations)[child_level];
        }
        catch (raised const&)
        {
            pyclass::handler_scope handler38;
            if (matches(handler38.exception, B<"IndexError">()))
            {
                child_indentation = (*indentations)[level] + (*space);
                (*indentations).method("append", { child_indentation });
            }
            else
                py_reraise(handler38.exception);
        }
        if (((!elem.attr("text").truthy()) || (!elem.attr("text").method("strip", { G<"xml.etree.ElementTree._XML_WHITESPACE">() }).truthy())))
        {
            elem.set_attr("text", child_indentation);
        }
        for (py const& item : iterate{ elem })
        {
            child = item;
            if (py{ child.size() }.truthy())
            {
                (*_indent_children)(child, child_level);
            }
            if (((!child.attr("tail").truthy()) || (!child.attr("tail").method("strip", { G<"xml.etree.ElementTree._XML_WHITESPACE">() }).truthy())))
            {
                child.set_attr("tail", child_indentation);
            }
        }
        // Dedent after the last child by overwriting the previous indentation.
        if (!child.attr("tail").method("strip", { G<"xml.etree.ElementTree._XML_WHITESPACE">() }).truthy())
        {
            child.set_attr("tail", (*indentations)[level]);
        }
        return none();
    });
                    (*_indent_children)(tree, I<0>());
                    return none();
                }));

            // --------------------------------------------------------------------
            // parsing
            define("xml.etree.ElementTree", "parse", w.function("parse", "parse", { py_positional("source"), py_positional("parser", none()) }, [](arguments a) -> py
                {
                    py source = a[0];
                    py parser = a[1];
                    py tree;
                    tree = (G<"xml.etree.ElementTree.ElementTree">())();
                    tree.method("parse", { source, parser });
                    return tree;
                }));

            define("xml.etree.ElementTree", "iterparse", w.function("iterparse", "iterparse", { py_positional("source"), py_positional("events", none()), py_positional("parser", none()) }, [](arguments a) -> py
                {
                    auto const source = std::make_shared<py>(a[0]);
                    py events = a[1];
                    py parser = a[2];
                    py IterParseIterator;
                    auto const close_source = std::make_shared<py>();
                    auto const gen = std::make_shared<py>();
                    auto const it = std::make_shared<py>();
                    py iterator;
                    auto const pullparser = std::make_shared<py>();
                    auto const wr = std::make_shared<py>();
                    // Use the internal, undocumented _parser argument for now; When the
                    // parser argument of iterparse is removed, this can be killed.
                    *pullparser = (G<"xml.etree.ElementTree.XMLPullParser">()).call({  }, { { "events", events }, { "_parser", parser } });
                    if (!has_attr((*source), "read"))
                    {
                        *source = (B<"open">())((*source), S<"rb">());
                        *close_source = boolean(true);
                    }
                    else
                    {
                        *close_source = boolean(false);
                    }
                    iterator = w.function("iterator", "iterparse.<locals>.iterator", { py_positional("source") }, [close_source, pullparser, wr](arguments a) -> py
    {
        return make_native_generator("iterator", "iterparse.<locals>.iterator", w.module, [a = std::vector<py>(a.begin(), a.end()), close_source, pullparser, wr]() -> py
        {
            py source = a[0];
            py data;
            py it;
            py root;
            {
                std::optional<py> pending40;
                [[maybe_unused]] int flow40 = 0;
                [[maybe_unused]] py result40;
                try
                {
                    while (boolean(true).truthy())
                    {
                        yield_from((*pullparser).method("read_events"));
                        // load event buffer
                        data = source.method("read", { (I<16>()) * (I<1024>()) });
                        if (!data.truthy())
                        {
                            break;
                        }
                        (*pullparser).method("feed", { data });
                    }
                    root = (*pullparser).method("_close_and_return_root");
                    yield_from((*pullparser).method("read_events"));
                    it = (*wr)();
                    if (!it.is_none())
                    {
                        it.set_attr("root", root);
                    }
                }
                catch (raised const&)
                {
                    pending40 = py_take_exception();
                }
                finally40:
                if ((*close_source).truthy())
                {
                    source.method("close");
                }
                if (pending40)
                    py_reraise(*pending40);
                if (flow40 == 1)
                    return result40;
            }
            return none();
        });
    });
                    *gen = iterator((*source));
                    IterParseIterator = w.make_class("IterParseIterator", { Mod<"collections.abc">().attr("Iterator") }, {
    { "__next__", (*gen).attr("__next__") },
    { "close", w.method("IterParseIterator", "close", {  }, [close_source, gen, source](arguments a) -> py
        {
            py self = a[0];
            if ((*close_source).truthy())
            {
                (*source).method("close");
            }
            (*gen).method("close");
            return none();
        }) },
    { "__del__", w.method("IterParseIterator", "__del__", {  }, [close_source, source](arguments a) -> py
        {
            py self = a[0];
            if ((*close_source).truthy())
            {
                (*source).method("close");
            }
            return none();
        }) }, }, "iterparse.<locals>.IterParseIterator");
                    *it = IterParseIterator();
                    (*it).set_attr("root", none());
                    *wr = (Mod<"weakref">().attr("ref"))((*it));
                    return (*it);
                }));

            define("xml.etree.ElementTree", "XMLPullParser", w.make_class("XMLPullParser", {  }, {
                { "__init__", w.method("XMLPullParser", "__init__", { py_positional("events", none()), py_keyword("_parser", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py events = a[1];
                        py _parser = a[2];
                        self.set_attr("_events_queue", (Mod<"collections">().attr("deque"))());
                        self.set_attr("_parser", [&]() -> py { py const t = _parser; return t.truthy() ? t : (G<"xml.etree.ElementTree.XMLParser">()).call({  }, { { "target", (G<"xml.etree.ElementTree.TreeBuilder">())() } }); }());
                        // wire up the parser for event reporting
                        if (events.is_none())
                        {
                            events = py_tuple({ S<"end">() });
                        }
                        self.attr("_parser").method("_setevents", { self.attr("_events_queue"), events });
                        return none();
                    }) },
                { "feed", w.method("XMLPullParser", "feed", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py exc;
                        if (self.attr("_parser").is_none())
                        {
                            py_raise((B<"ValueError">())(S<"feed() called after end of stream">()));
                        }
                        if (data.truthy())
                        {
                            try
                            {
                                self.attr("_parser").method("feed", { data });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler42;
                                if (matches(handler42.exception, B<"SyntaxError">()))
                                {
                                    exc = handler42.exception;
                                    self.attr("_events_queue").method("append", { exc });
                                }
                                else
                                    py_reraise(handler42.exception);
                            }
                        }
                        return none();
                    }) },
                { "_close_and_return_root", w.method("XMLPullParser", "_close_and_return_root", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py root;
                        root = self.attr("_parser").method("close");
                        self.set_attr("_parser", none());
                        return root;
                    }) },
                { "close", w.method("XMLPullParser", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_close_and_return_root");
                        return none();
                    }) },
                { "read_events", w.method("XMLPullParser", "read_events", {  }, [](arguments a) -> py
                    {
                        return make_native_generator("read_events", "XMLPullParser.read_events", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py event;
                            py events;
                            events = self.attr("_events_queue");
                            while (events.truthy())
                            {
                                event = events.method("popleft");
                                if (isinstance(event, B<"Exception">()))
                                {
                                    py_raise(event);
                                }
                                else
                                {
                                    py_yield(event);
                                }
                            }
                            return none();
                        });
                    }) },
                { "flush", w.method("XMLPullParser", "flush", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_parser").is_none())
                        {
                            py_raise((B<"ValueError">())(S<"flush() called after end of stream">()));
                        }
                        self.attr("_parser").method("flush");
                        return none();
                    }) }, }));

            define("xml.etree.ElementTree", "XML", w.function("XML", "XML", { py_positional("text"), py_positional("parser", none()) }, [](arguments a) -> py
                {
                    py text = a[0];
                    py parser = a[1];
                    if (!parser.truthy())
                    {
                        parser = (G<"xml.etree.ElementTree.XMLParser">()).call({  }, { { "target", (G<"xml.etree.ElementTree.TreeBuilder">())() } });
                    }
                    parser.method("feed", { text });
                    return parser.method("close");
                }));

            define("xml.etree.ElementTree", "XMLID", w.function("XMLID", "XMLID", { py_positional("text"), py_positional("parser", none()) }, [](arguments a) -> py
                {
                    py text = a[0];
                    py parser = a[1];
                    py elem;
                    py id;
                    py ids;
                    py tree;
                    if (!parser.truthy())
                    {
                        parser = (G<"xml.etree.ElementTree.XMLParser">()).call({  }, { { "target", (G<"xml.etree.ElementTree.TreeBuilder">())() } });
                    }
                    parser.method("feed", { text });
                    tree = parser.method("close");
                    ids = py_dict();
                    for (py const& item : iterate{ tree.method("iter") })
                    {
                        elem = item;
                        id = elem.method("get", { S<"id">() });
                        if (id.truthy())
                        {
                            ids.set_item(id, elem);
                        }
                    }
                    return py_tuple({ tree, ids });
                }));

            // Parse XML document from string constant.  Alias for XML().
            define("xml.etree.ElementTree", "fromstring", G<"xml.etree.ElementTree.XML">());
            define("xml.etree.ElementTree", "fromstringlist", w.function("fromstringlist", "fromstringlist", { py_positional("sequence"), py_positional("parser", none()) }, [](arguments a) -> py
                {
                    py sequence = a[0];
                    py parser = a[1];
                    py text;
                    if (!parser.truthy())
                    {
                        parser = (G<"xml.etree.ElementTree.XMLParser">()).call({  }, { { "target", (G<"xml.etree.ElementTree.TreeBuilder">())() } });
                    }
                    for (py const& item : iterate{ sequence })
                    {
                        text = item;
                        parser.method("feed", { text });
                    }
                    return parser.method("close");
                }));

            // --------------------------------------------------------------------
            define("xml.etree.ElementTree", "TreeBuilder", w.make_class("TreeBuilder", {  }, {
                { "__init__", w.method("TreeBuilder", "__init__", { py_positional("element_factory", none()), py_keyword("comment_factory", none()), py_keyword("pi_factory", none()), py_keyword("insert_comments", boolean(false)), py_keyword("insert_pis", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py element_factory = a[1];
                        py comment_factory = a[2];
                        py pi_factory = a[3];
                        py insert_comments = a[4];
                        py insert_pis = a[5];
                        self.set_attr("_data", py_list());
                        self.set_attr("_elem", py_list());
                        self.set_attr("_last", none());
                        self.set_attr("_root", none());
                        self.set_attr("_tail", none());
                        if (comment_factory.is_none())
                        {
                            comment_factory = G<"xml.etree.ElementTree.Comment">();
                        }
                        self.set_attr("_comment_factory", comment_factory);
                        self.set_attr("insert_comments", insert_comments);
                        if (pi_factory.is_none())
                        {
                            pi_factory = G<"xml.etree.ElementTree.ProcessingInstruction">();
                        }
                        self.set_attr("_pi_factory", pi_factory);
                        self.set_attr("insert_pis", insert_pis);
                        if (element_factory.is_none())
                        {
                            element_factory = G<"xml.etree.ElementTree.Element">();
                        }
                        self.set_attr("_factory", element_factory);
                        return none();
                    }) },
                { "close", w.method("TreeBuilder", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!(py{ self.attr("_elem").size() } == (I<0>())))
                            py_raise(B<"AssertionError">()(S<"missing end tags">()));
                        if (self.attr("_root").is_none())
                            py_raise(B<"AssertionError">()(S<"missing toplevel element">()));
                        return self.attr("_root");
                    }) },
                { "_flush", w.method("TreeBuilder", "_flush", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py text;
                        if (self.attr("_data").truthy())
                        {
                            if (!self.attr("_last").is_none())
                            {
                                text = (S<"">()).method("join", { self.attr("_data") });
                                if (self.attr("_tail").truthy())
                                {
                                    if (!self.attr("_last").attr("tail").is_none())
                                        py_raise(B<"AssertionError">()(S<"internal error (tail)">()));
                                    self.attr("_last").set_attr("tail", text);
                                }
                                else
                                {
                                    if (!self.attr("_last").attr("text").is_none())
                                        py_raise(B<"AssertionError">()(S<"internal error (text)">()));
                                    self.attr("_last").set_attr("text", text);
                                }
                            }
                            self.set_attr("_data", py_list());
                        }
                        return none();
                    }) },
                { "data", w.method("TreeBuilder", "data", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.attr("_data").method("append", { data });
                        return none();
                    }) },
                { "start", w.method("TreeBuilder", "start", { py_positional("tag"), py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tag = a[1];
                        py attrs = a[2];
                        py elem;
                        self.method("_flush");
                        py const value46 = self.method("_factory", { tag, attrs });
                        self.set_attr("_last", value46);
                        elem = value46;
                        if (self.attr("_elem").truthy())
                        {
                            self.attr("_elem")[I<-1>()].method("append", { elem });
                        }
                        else if (self.attr("_root").is_none())
                        {
                            self.set_attr("_root", elem);
                        }
                        self.attr("_elem").method("append", { elem });
                        self.set_attr("_tail", I<0>());
                        return elem;
                    }) },
                { "end", w.method("TreeBuilder", "end", { py_positional("tag") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tag = a[1];
                        self.method("_flush");
                        self.set_attr("_last", self.attr("_elem").method("pop"));
                        if (!(self.attr("_last").attr("tag") == tag))
                            py_raise(B<"AssertionError">()((S<"end tag mismatch (expected %s, got %s)">()) % py_tuple({ self.attr("_last").attr("tag"), tag })));
                        self.set_attr("_tail", I<1>());
                        return self.attr("_last");
                    }) },
                { "comment", w.method("TreeBuilder", "comment", { py_positional("text") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py text = a[1];
                        return self.method("_handle_single", { self.attr("_comment_factory"), self.attr("insert_comments"), text });
                    }) },
                { "pi", w.method("TreeBuilder", "pi", { py_positional("target"), py_positional("text", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py target = a[1];
                        py text = a[2];
                        return self.method("_handle_single", { self.attr("_pi_factory"), self.attr("insert_pis"), target, text });
                    }) },
                { "_handle_single", w.method("TreeBuilder", "_handle_single", { py_positional("factory"), py_positional("insert") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py factory = a[1];
                        py insert = a[2];
                        py args = a[3];
                        py elem;
                        elem = call_ex(factory, {  }, args, py{});
                        if (insert.truthy())
                        {
                            self.method("_flush");
                            self.set_attr("_last", elem);
                            if (self.attr("_elem").truthy())
                            {
                                self.attr("_elem")[I<-1>()].method("append", { elem });
                            }
                            self.set_attr("_tail", I<1>());
                        }
                        return elem;
                    }, true) }, }));

            // also see ElementTree and TreeBuilder
            define("xml.etree.ElementTree", "XMLParser", w.make_class("XMLParser", {  }, {
                { "__init__", w.method("XMLParser", "__init__", { py_keyword("target", none()), py_keyword("encoding", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py target = a[1];
                        py encoding = a[2];
                        py expat;
                        py parser;
                        try
                        {
                            expat = M("xml.parsers", "expat");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler47;
                            if (matches(handler47.exception, B<"ImportError">()))
                            {
                                try
                                {
                                    expat = module_of("pyexpat");
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler48;
                                    if (matches(handler48.exception, B<"ImportError">()))
                                    {
                                        py_raise((B<"ImportError">())(S<"No module named expat; use SimpleXMLTreeBuilder instead">()));
                                    }
                                    else
                                        py_reraise(handler48.exception);
                                }
                            }
                            else
                                py_reraise(handler47.exception);
                        }
                        parser = expat.method("ParserCreate", { encoding, S<"}">() });
                        if (target.is_none())
                        {
                            target = (G<"xml.etree.ElementTree.TreeBuilder">())();
                        }
                        // underscored names are provided for compatibility only
                        py const value49 = parser;
                        self.set_attr("parser", value49);
                        self.set_attr("_parser", value49);
                        py const value50 = target;
                        self.set_attr("target", value50);
                        self.set_attr("_target", value50);
                        self.set_attr("_error", expat.attr("error"));
                        self.set_attr("_names", py_dict());
                        // main callbacks
                        parser.set_attr("DefaultHandlerExpand", self.attr("_default"));
                        if (has_attr(target, "start"))
                        {
                            parser.set_attr("StartElementHandler", self.attr("_start"));
                        }
                        if (has_attr(target, "end"))
                        {
                            parser.set_attr("EndElementHandler", self.attr("_end"));
                        }
                        if (has_attr(target, "start_ns"))
                        {
                            parser.set_attr("StartNamespaceDeclHandler", self.attr("_start_ns"));
                        }
                        if (has_attr(target, "end_ns"))
                        {
                            parser.set_attr("EndNamespaceDeclHandler", self.attr("_end_ns"));
                        }
                        if (has_attr(target, "data"))
                        {
                            parser.set_attr("CharacterDataHandler", target.attr("data"));
                        }
                        // miscellaneous callbacks
                        if (has_attr(target, "comment"))
                        {
                            parser.set_attr("CommentHandler", target.attr("comment"));
                        }
                        if (has_attr(target, "pi"))
                        {
                            parser.set_attr("ProcessingInstructionHandler", target.attr("pi"));
                        }
                        parser.set_attr("StartDoctypeDeclHandler", self.attr("_start_doctype"));
                        // Configure pyexpat: buffering, new-style attribute handling.
                        parser.set_attr("buffer_text", I<1>());
                        parser.set_attr("ordered_attributes", I<1>());
                        self.set_attr("entity", py_dict());
                        try
                        {
                            self.set_attr("version", (S<"Expat %d.%d.%d">()) % expat.attr("version_info"));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler51;
                            if (matches(handler51.exception, B<"AttributeError">()))
                            {
                            }
                            else
                                py_reraise(handler51.exception);
                        }
                        return none();
                    }) },
                { "_setevents", w.method("XMLParser", "_setevents", { py_positional("events_queue"), py_positional("events_to_report") }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        py events_queue = a[1];
                        py events_to_report = a[2];
                        py append;
                        auto const event_name = std::make_shared<py>();
                        py handler;
                        py parser;
                        parser = (*self).attr("_parser");
                        append = events_queue.attr("append");
                        for (py const& item : iterate{ events_to_report })
                        {
                            *event_name = item;
                            if ((*event_name) == (S<"start">()))
                            {
                                parser.set_attr("ordered_attributes", I<1>());
                                handler = w.function("handler", "XMLParser._setevents.<locals>.handler", { py_positional("tag"), py_positional("attrib_in"), py_positional("event", (*event_name)), py_positional("append", append), py_positional("start", (*self).attr("_start")) }, [event_name, self](arguments a) -> py
            {
                py tag = a[0];
                py attrib_in = a[1];
                py event = a[2];
                py append = a[3];
                py start = a[4];
                append(py_tuple({ event, start(tag, attrib_in) }));
                return none();
            });
                                parser.set_attr("StartElementHandler", handler);
                            }
                            else if ((*event_name) == (S<"end">()))
                            {
                                handler = w.function("handler", "XMLParser._setevents.<locals>.handler", { py_positional("tag"), py_positional("event", (*event_name)), py_positional("append", append), py_positional("end", (*self).attr("_end")) }, [event_name, self](arguments a) -> py
            {
                py tag = a[0];
                py event = a[1];
                py append = a[2];
                py end = a[3];
                append(py_tuple({ event, end(tag) }));
                return none();
            });
                                parser.set_attr("EndElementHandler", handler);
                            }
                            else if ((*event_name) == (S<"start-ns">()))
                            {
                                // TreeBuilder does not implement .start_ns()
                                if (has_attr((*self).attr("target"), "start_ns"))
                                {
                                    handler = w.function("handler", "XMLParser._setevents.<locals>.handler", { py_positional("prefix"), py_positional("uri"), py_positional("event", (*event_name)), py_positional("append", append), py_positional("start_ns", (*self).attr("_start_ns")) }, [event_name, self](arguments a) -> py
                {
                    py prefix = a[0];
                    py uri = a[1];
                    py event = a[2];
                    py append = a[3];
                    py start_ns = a[4];
                    append(py_tuple({ event, start_ns(prefix, uri) }));
                    return none();
                });
                                }
                                else
                                {
                                    handler = w.function("handler", "XMLParser._setevents.<locals>.handler", { py_positional("prefix"), py_positional("uri"), py_positional("event", (*event_name)), py_positional("append", append) }, [event_name](arguments a) -> py
                {
                    py prefix = a[0];
                    py uri = a[1];
                    py event = a[2];
                    py append = a[3];
                    append(py_tuple({ event, py_tuple({ [&]() -> py { py const t = prefix; return t.truthy() ? t : S<"">(); }(), [&]() -> py { py const t = uri; return t.truthy() ? t : S<"">(); }() }) }));
                    return none();
                });
                                }
                                parser.set_attr("StartNamespaceDeclHandler", handler);
                            }
                            else if ((*event_name) == (S<"end-ns">()))
                            {
                                // TreeBuilder does not implement .end_ns()
                                if (has_attr((*self).attr("target"), "end_ns"))
                                {
                                    handler = w.function("handler", "XMLParser._setevents.<locals>.handler", { py_positional("prefix"), py_positional("event", (*event_name)), py_positional("append", append), py_positional("end_ns", (*self).attr("_end_ns")) }, [event_name, self](arguments a) -> py
                {
                    py prefix = a[0];
                    py event = a[1];
                    py append = a[2];
                    py end_ns = a[3];
                    append(py_tuple({ event, end_ns(prefix) }));
                    return none();
                });
                                }
                                else
                                {
                                    handler = w.function("handler", "XMLParser._setevents.<locals>.handler", { py_positional("prefix"), py_positional("event", (*event_name)), py_positional("append", append) }, [event_name](arguments a) -> py
                {
                    py prefix = a[0];
                    py event = a[1];
                    py append = a[2];
                    append(py_tuple({ event, none() }));
                    return none();
                });
                                }
                                parser.set_attr("EndNamespaceDeclHandler", handler);
                            }
                            else if ((*event_name) == (S<"comment">()))
                            {
                                handler = w.function("handler", "XMLParser._setevents.<locals>.handler", { py_positional("text"), py_positional("event", (*event_name)), py_positional("append", append), py_positional("self", (*self)) }, [event_name](arguments a) -> py
            {
                py text = a[0];
                py event = a[1];
                py append = a[2];
                py self = a[3];
                append(py_tuple({ event, self.attr("target").method("comment", { text }) }));
                return none();
            });
                                parser.set_attr("CommentHandler", handler);
                            }
                            else if ((*event_name) == (S<"pi">()))
                            {
                                handler = w.function("handler", "XMLParser._setevents.<locals>.handler", { py_positional("pi_target"), py_positional("data"), py_positional("event", (*event_name)), py_positional("append", append), py_positional("self", (*self)) }, [event_name](arguments a) -> py
            {
                py pi_target = a[0];
                py data = a[1];
                py event = a[2];
                py append = a[3];
                py self = a[4];
                append(py_tuple({ event, self.attr("target").method("pi", { pi_target, data }) }));
                return none();
            });
                                parser.set_attr("ProcessingInstructionHandler", handler);
                            }
                            else
                            {
                                py_raise((B<"ValueError">())((S<"unknown event %r">()) % (*event_name)));
                            }
                        }
                        return none();
                    }) },
                { "_raiseerror", w.method("XMLParser", "_raiseerror", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py err;
                        err = (G<"xml.etree.ElementTree.ParseError">())(value);
                        err.set_attr("code", value.attr("code"));
                        err.set_attr("position", py_tuple({ value.attr("lineno"), value.attr("offset") }));
                        py_raise(err);
                    }) },
                { "_fixname", w.method("XMLParser", "_fixname", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py name;
                        try
                        {
                            name = self.attr("_names")[key];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler53;
                            if (matches(handler53.exception, B<"KeyError">()))
                            {
                                name = key;
                                if (name.contains((S<"}">())))
                                {
                                    name = (S<"{">()) + name;
                                }
                                self.attr("_names").set_item(key, name);
                            }
                            else
                                py_reraise(handler53.exception);
                        }
                        return name;
                    }) },
                { "_start_ns", w.method("XMLParser", "_start_ns", { py_positional("prefix"), py_positional("uri") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prefix = a[1];
                        py uri = a[2];
                        return self.attr("target").method("start_ns", { [&]() -> py { py const t = prefix; return t.truthy() ? t : S<"">(); }(), [&]() -> py { py const t = uri; return t.truthy() ? t : S<"">(); }() });
                    }) },
                { "_end_ns", w.method("XMLParser", "_end_ns", { py_positional("prefix") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prefix = a[1];
                        return self.attr("target").method("end_ns", { [&]() -> py { py const t = prefix; return t.truthy() ? t : S<"">(); }() });
                    }) },
                { "_start", w.method("XMLParser", "_start", { py_positional("tag"), py_positional("attr_list") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tag = a[1];
                        py attr_list = a[2];
                        py attrib;
                        py fixname;
                        py i;
                        fixname = self.attr("_fixname");
                        tag = fixname(tag);
                        attrib = py_dict();
                        if (attr_list.truthy())
                        {
                            for (py const& item : iterate{ (B<"range">())(I<0>(), py{ attr_list.size() }, I<2>()) })
                            {
                                i = item;
                                attrib.set_item(fixname(attr_list[i]), attr_list[i + (I<1>())]);
                            }
                        }
                        return self.attr("target").method("start", { tag, attrib });
                    }) },
                { "_end", w.method("XMLParser", "_end", { py_positional("tag") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tag = a[1];
                        return self.attr("target").method("end", { self.method("_fixname", { tag }) });
                    }) },
                { "_default", w.method("XMLParser", "_default", { py_positional("text") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py text = a[1];
                        py data_handler;
                        py err;
                        py expat;
                        py prefix;
                        prefix = text[B<"slice">()(none(), I<1>(), none())];
                        if (prefix == (S<"&">()))
                        {
                            // deal with undefined entities
                            try
                            {
                                data_handler = self.attr("target").attr("data");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler55;
                                if (matches(handler55.exception, B<"AttributeError">()))
                                {
                                    return none();
                                }
                                else
                                    py_reraise(handler55.exception);
                            }
                            try
                            {
                                data_handler(self.attr("entity")[text[B<"slice">()(I<1>(), I<-1>(), none())]]);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler56;
                                if (matches(handler56.exception, B<"KeyError">()))
                                {
                                    expat = M("xml.parsers", "expat");
                                    err = expat.method("error", { (S<"undefined entity %s: line %d, column %d">()) % py_tuple({ text, self.attr("parser").attr("ErrorLineNumber"), self.attr("parser").attr("ErrorColumnNumber") }) });
                                    err.set_attr("code", I<11>());
                                    err.set_attr("lineno", self.attr("parser").attr("ErrorLineNumber"));
                                    err.set_attr("offset", self.attr("parser").attr("ErrorColumnNumber"));
                                    py_raise(err);
                                }
                                else
                                    py_reraise(handler56.exception);
                            }
                        }
                        return none();
                    }) },
                { "_start_doctype", w.method("XMLParser", "_start_doctype", { py_positional("name"), py_positional("system"), py_positional("pubid"), py_positional("has_internal_subset") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py system = a[2];
                        py pubid = a[3];
                        py has_internal_subset = a[4];
                        if (has_attr(self.attr("target"), "doctype"))
                        {
                            self.attr("target").method("doctype", { name, pubid, system });
                        }
                        else if (has_attr(self, "doctype"))
                        {
                            (Mod<"warnings">().attr("warn"))(S<"The doctype() method of XMLParser is ignored.  Define doctype() method on the TreeBuilder target.">(), B<"RuntimeWarning">());
                        }
                        return none();
                    }) },
                { "feed", w.method("XMLParser", "feed", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py v;
                        try
                        {
                            self.attr("parser").method("Parse", { data, boolean(false) });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler57;
                            if (matches(handler57.exception, self.attr("_error")))
                            {
                                v = handler57.exception;
                                self.method("_raiseerror", { v });
                            }
                            else
                                py_reraise(handler57.exception);
                        }
                        return none();
                    }) },
                { "close", w.method("XMLParser", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py close_handler;
                        py v;
                        try
                        {
                            self.attr("parser").method("Parse", { py{ make_bytes(std::string{ "", 0 }) }, boolean(true) });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler58;
                            if (matches(handler58.exception, self.attr("_error")))
                            {
                                v = handler58.exception;
                                self.method("_raiseerror", { v });
                            }
                            else
                                py_reraise(handler58.exception);
                        }
                        {
                            std::optional<py> pending59;
                            [[maybe_unused]] int flow59 = 0;
                            [[maybe_unused]] py result59;
                            try
                            {
                                bool otherwise59 = false;
                                try
                                {
                                    close_handler = self.attr("target").attr("close");
                                    otherwise59 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler59;
                                    if (matches(handler59.exception, B<"AttributeError">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler59.exception);
                                }
                                if (otherwise59)
                                {
                                    result59 = close_handler();
                                    flow59 = 1;
                                    goto finally59;
                                }
                            }
                            catch (raised const&)
                            {
                                pending59 = py_take_exception();
                            }
                            finally59:
                            // get rid of circular references
                            del_attr(self, "parser");
                            del_attr(self, "_parser");
                            del_attr(self, "target");
                            del_attr(self, "_target");
                            if (pending59)
                                py_reraise(*pending59);
                            if (flow59 == 1)
                                return result59;
                        }
                        return none();
                    }) },
                { "flush", w.method("XMLParser", "flush", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py v;
                        py was_enabled;
                        was_enabled = self.attr("parser").method("GetReparseDeferralEnabled");
                        {
                            std::optional<py> pending60;
                            [[maybe_unused]] int flow60 = 0;
                            [[maybe_unused]] py result60;
                            try
                            {
                                try
                                {
                                    self.attr("parser").method("SetReparseDeferralEnabled", { boolean(false) });
                                    self.attr("parser").method("Parse", { py{ make_bytes(std::string{ "", 0 }) }, boolean(false) });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler60;
                                    if (matches(handler60.exception, self.attr("_error")))
                                    {
                                        v = handler60.exception;
                                        self.method("_raiseerror", { v });
                                    }
                                    else
                                        py_reraise(handler60.exception);
                                }
                            }
                            catch (raised const&)
                            {
                                pending60 = py_take_exception();
                            }
                            finally60:
                            self.attr("parser").method("SetReparseDeferralEnabled", { was_enabled });
                            if (pending60)
                                py_reraise(*pending60);
                            if (flow60 == 1)
                                return result60;
                        }
                        return none();
                    }) }, }));

            // --------------------------------------------------------------------
            // C14N 2.0
            define("xml.etree.ElementTree", "canonicalize", w.function("canonicalize", "canonicalize", { py_positional("xml_data", none()), py_keyword("out", none()), py_keyword("from_file", none()) }, [](arguments a) -> py
                {
                    py xml_data = a[0];
                    py out = a[1];
                    py from_file = a[2];
                    py options = a[3];
                    py parser;
                    py sio;
                    if ((xml_data.is_none() && from_file.is_none()))
                    {
                        py_raise((B<"ValueError">())(S<"Either 'xml_data' or 'from_file' must be provided as input">()));
                    }
                    sio = none();
                    if (out.is_none())
                    {
                        py const value61 = (Mod<"io">().attr("StringIO"))();
                        sio = value61;
                        out = value61;
                    }
                    parser = (G<"xml.etree.ElementTree.XMLParser">()).call({  }, { { "target", call_ex((G<"xml.etree.ElementTree.C14NWriterTarget">()), { out.attr("write") }, py{}, options) } });
                    if (!xml_data.is_none())
                    {
                        parser.method("feed", { xml_data });
                        parser.method("close");
                    }
                    else if (!from_file.is_none())
                    {
                        (G<"xml.etree.ElementTree.parse">()).call({ from_file }, { { "parser", parser } });
                    }
                    return (!sio.is_none() ? sio.method("getvalue") : none());
                }, false, true));

            define("xml.etree.ElementTree", "_looks_like_prefix_name", (Mod<"re">()).method("compile", { S<"^\\w+:\\w+$">(), Mod<"re">().attr("UNICODE") }).attr("match"));
            define("xml.etree.ElementTree", "C14NWriterTarget", w.make_class("C14NWriterTarget", {  }, {
                { "__init__", w.method("C14NWriterTarget", "__init__", { py_positional("write"), py_keyword("with_comments", boolean(false)), py_keyword("strip_text", boolean(false)), py_keyword("rewrite_prefixes", boolean(false)), py_keyword("qname_aware_tags", none()), py_keyword("qname_aware_attrs", none()), py_keyword("exclude_attrs", none()), py_keyword("exclude_tags", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py write = a[1];
                        py with_comments = a[2];
                        py strip_text = a[3];
                        py rewrite_prefixes = a[4];
                        py qname_aware_tags = a[5];
                        py qname_aware_attrs = a[6];
                        py exclude_attrs = a[7];
                        py exclude_tags = a[8];
                        self.set_attr("_write", write);
                        self.set_attr("_data", py_list());
                        self.set_attr("_with_comments", with_comments);
                        self.set_attr("_strip_text", strip_text);
                        self.set_attr("_exclude_attrs", (exclude_attrs.truthy() ? (B<"set">())(exclude_attrs) : none()));
                        self.set_attr("_exclude_tags", (exclude_tags.truthy() ? (B<"set">())(exclude_tags) : none()));
                        self.set_attr("_rewrite_prefixes", rewrite_prefixes);
                        if (qname_aware_tags.truthy())
                        {
                            self.set_attr("_qname_aware_tags", (B<"set">())(qname_aware_tags));
                        }
                        else
                        {
                            self.set_attr("_qname_aware_tags", none());
                        }
                        if (qname_aware_attrs.truthy())
                        {
                            self.set_attr("_find_qname_aware_attrs", (B<"set">())(qname_aware_attrs).attr("intersection"));
                        }
                        else
                        {
                            self.set_attr("_find_qname_aware_attrs", none());
                        }
                        // Stack with globally and newly declared namespaces as (uri, prefix) pairs.
                        self.set_attr("_declared_ns_stack", py_list({ py_list({ py_tuple({ S<"http://www.w3.org/XML/1998/namespace">(), S<"xml">() }) }) }));
                        // Stack with user declared namespace prefixes as (uri, prefix) pairs.
                        self.set_attr("_ns_stack", py_list());
                        if (!rewrite_prefixes.truthy())
                        {
                            self.attr("_ns_stack").method("append", { (B<"list">())((G<"xml.etree.ElementTree._namespace_map">()).method("items")) });
                        }
                        self.attr("_ns_stack").method("append", { py_list() });
                        self.set_attr("_prefix_map", py_dict());
                        self.set_attr("_preserve_space", py_list({ boolean(false) }));
                        self.set_attr("_pending_start", none());
                        self.set_attr("_root_seen", boolean(false));
                        self.set_attr("_root_done", boolean(false));
                        self.set_attr("_ignored_depth", I<0>());
                        return none();
                    }) },
                { "_iter_namespaces", w.method("C14NWriterTarget", "_iter_namespaces", { py_positional("ns_stack"), py_positional("_reversed", B<"reversed">()) }, [](arguments a) -> py
                    {
                        return make_native_generator("_iter_namespaces", "C14NWriterTarget._iter_namespaces", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py ns_stack = a[1];
                            py _reversed = a[2];
                            py namespaces;
                            for (py const& item : iterate{ _reversed(ns_stack) })
                            {
                                namespaces = item;
                                if (namespaces.truthy())
                                {
                                    yield_from(namespaces);
                                }
                            }
                            return none();
                        });
                    }) },
                { "_resolve_prefix_name", w.method("C14NWriterTarget", "_resolve_prefix_name", { py_positional("prefixed_name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prefixed_name = a[1];
                        py name;
                        py p;
                        py prefix;
                        py uri;
                        auto const items63 = unpack(prefixed_name.method("split", { S<":">(), I<1>() }), 2);
                        prefix = items63[0];
                        name = items63[1];
                        for (py const& item : iterate{ self.method("_iter_namespaces", { self.attr("_ns_stack") }) })
                        {
                            auto const items65 = unpack(item, 2);
                            uri = items65[0];
                            p = items65[1];
                            if (p == prefix)
                            {
                                return S(std::string{ "{" } + uri.str() + "}" + name.str());
                            }
                        }
                        py_raise((B<"ValueError">())(S(std::string{ "Prefix " } + prefix.str() + " of QName \"" + prefixed_name.str() + "\" is not declared in scope")));
                    }) },
                { "_qname", w.method("C14NWriterTarget", "_qname", { py_positional("qname"), py_positional("uri", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py qname = a[1];
                        py uri = a[2];
                        py prefix;
                        py prefixes_seen;
                        py tag;
                        py u;
                        if (uri.is_none())
                        {
                            auto const items66 = unpack((qname[B<"slice">()(none(), I<1>(), none())] == (S<"{">()) ? qname[B<"slice">()(I<1>(), none(), none())].method("rsplit", { S<"}">(), I<1>() }) : py_tuple({ S<"">(), qname })), 2);
                            uri = items66[0];
                            tag = items66[1];
                        }
                        else
                        {
                            tag = qname;
                        }
                        prefixes_seen = (B<"set">())();
                        for (py const& item : iterate{ self.method("_iter_namespaces", { self.attr("_declared_ns_stack") }) })
                        {
                            auto const items68 = unpack(item, 2);
                            u = items68[0];
                            prefix = items68[1];
                            if (((u == uri) && (!prefixes_seen.contains(prefix))))
                            {
                                return py_tuple({ (prefix.truthy() ? S(prefix.str() + ":" + tag.str()) : tag), tag, uri });
                            }
                            prefixes_seen.method("add", { prefix });
                        }
                        // Not declared yet => add new declaration.
                        if (self.attr("_rewrite_prefixes").truthy())
                        {
                            if (self.attr("_prefix_map").contains(uri))
                            {
                                prefix = self.attr("_prefix_map")[uri];
                            }
                            else
                            {
                                py const value69 = S(std::string{ "n" } + py{ self.attr("_prefix_map").size() }.str());
                                prefix = value69;
                                self.attr("_prefix_map").set_item(uri, value69);
                            }
                            self.attr("_declared_ns_stack")[I<-1>()].method("append", { py_tuple({ uri, prefix }) });
                            return py_tuple({ S(prefix.str() + ":" + tag.str()), tag, uri });
                        }
                        if (((!uri.truthy()) && (!prefixes_seen.contains((S<"">())))))
                        {
                            // No default namespace declared => no prefix needed.
                            return py_tuple({ tag, tag, uri });
                        }
                        for (py const& item : iterate{ self.method("_iter_namespaces", { self.attr("_ns_stack") }) })
                        {
                            auto const items71 = unpack(item, 2);
                            u = items71[0];
                            prefix = items71[1];
                            if (u == uri)
                            {
                                self.attr("_declared_ns_stack")[I<-1>()].method("append", { py_tuple({ uri, prefix }) });
                                return py_tuple({ (prefix.truthy() ? S(prefix.str() + ":" + tag.str()) : tag), tag, uri });
                            }
                        }
                        if (!uri.truthy())
                        {
                            // As soon as a default namespace is defined,
                            // anything that has no namespace (and thus, no prefix) goes there.
                            return py_tuple({ tag, tag, uri });
                        }
                        py_raise((B<"ValueError">())(S(std::string{ "Namespace \"" } + uri.str() + "\" is not declared in scope")));
                    }) },
                { "data", w.method("C14NWriterTarget", "data", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (!self.attr("_ignored_depth").truthy())
                        {
                            self.attr("_data").method("append", { data });
                        }
                        return none();
                    }) },
                { "_flush", w.method("C14NWriterTarget", "_flush", { py_positional("_join_text", (S<"">()).attr("join")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _join_text = a[1];
                        py args;
                        py data;
                        py qname_text;
                        data = _join_text(self.attr("_data"));
                        del_item(self.attr("_data"), B<"slice">()(none(), none(), none()));
                        if ((self.attr("_strip_text").truthy() && (!self.attr("_preserve_space")[I<-1>()].truthy())))
                        {
                            data = data.method("strip", { G<"xml.etree.ElementTree._XML_WHITESPACE">() });
                        }
                        if (!self.attr("_pending_start").is_none())
                        {
                            auto const items72 = unpack(py_tuple({ self.attr("_pending_start"), none() }), 2);
                            args = items72[0];
                            self.set_attr("_pending_start", items72[1]);
                            qname_text = (([&]() -> py { py const t = data; return !t.truthy() ? t : (G<"xml.etree.ElementTree._looks_like_prefix_name">())(data); }()).truthy() ? data : none());
                            call_ex(self.attr("_start"), {  }, (B<"list">()(args) + py_list({ qname_text })), py{});
                            if (!qname_text.is_none())
                            {
                                return none();
                            }
                        }
                        if ((data.truthy() && self.attr("_root_seen").truthy()))
                        {
                            self.method("_write", { (G<"xml.etree.ElementTree._escape_cdata_c14n">())(data) });
                        }
                        return none();
                    }) },
                { "start_ns", w.method("C14NWriterTarget", "start_ns", { py_positional("prefix"), py_positional("uri") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prefix = a[1];
                        py uri = a[2];
                        if (self.attr("_ignored_depth").truthy())
                        {
                            return none();
                        }
                        // we may have to resolve qnames in text content
                        if (self.attr("_data").truthy())
                        {
                            self.method("_flush");
                        }
                        self.attr("_ns_stack")[I<-1>()].method("append", { py_tuple({ uri, prefix }) });
                        return none();
                    }) },
                { "start", w.method("C14NWriterTarget", "start", { py_positional("tag"), py_positional("attrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tag = a[1];
                        py attrs = a[2];
                        py new_namespaces;
                        if (((!self.attr("_exclude_tags").is_none()) && ([&]() -> py { py const t = self.attr("_ignored_depth"); return t.truthy() ? t : boolean(self.attr("_exclude_tags").contains(tag)); }()).truthy()))
                        {
                            {
                                py const target73 = self;
                                target73.set_attr("_ignored_depth", inplace(NEOS_PY_ADD, target73.attr("_ignored_depth"), I<1>()));
                            }
                            return none();
                        }
                        if (self.attr("_data").truthy())
                        {
                            self.method("_flush");
                        }
                        new_namespaces = py_list();
                        self.attr("_declared_ns_stack").method("append", { new_namespaces });
                        if (((!self.attr("_qname_aware_tags").is_none()) && self.attr("_qname_aware_tags").contains(tag)))
                        {
                            // Need to parse text first to see if it requires a prefix declaration.
                            self.set_attr("_pending_start", py_tuple({ tag, attrs, new_namespaces }));
                            return none();
                        }
                        self.method("_start", { tag, attrs, new_namespaces });
                        return none();
                    }) },
                { "_start", w.method("C14NWriterTarget", "_start", { py_positional("tag"), py_positional("attrs"), py_positional("new_namespaces"), py_positional("qname_text", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tag = a[1];
                        py attrs = a[2];
                        py new_namespaces = a[3];
                        py qname_text = a[4];
                        py attr_list;
                        py attr_name;
                        py attr_qname;
                        py k;
                        py parse_qname;
                        py parsed_qnames;
                        py qattrs;
                        py qname;
                        py qnames;
                        py resolved_names;
                        py space_behaviour;
                        py uri;
                        py v;
                        py value;
                        py write;
                        if (((!self.attr("_exclude_attrs").is_none()) && attrs.truthy()))
                        {
                            attrs = [&]() -> py { py const comprehension74 = py_dict(); py k; py v; for (py const& item : iterate{ attrs.method("items") }) { auto const items75 = unpack(item, 2); k = items75[0]; v = items75[1]; if (self.attr("_exclude_attrs").contains(k)) continue; comprehension74.set_item(k, v); } return comprehension74; }();
                        }
                        qnames = B<"set">()(((py_list() + py_list({ tag })) + B<"list">()(attrs)));
                        resolved_names = py_dict();
                        // Resolve prefixes in attribute and tag text.
                        if (!qname_text.is_none())
                        {
                            py const value76 = self.method("_resolve_prefix_name", { qname_text });
                            qname = value76;
                            resolved_names.set_item(qname_text, value76);
                            qnames.method("add", { qname });
                        }
                        if (((!self.attr("_find_qname_aware_attrs").is_none()) && attrs.truthy()))
                        {
                            qattrs = self.method("_find_qname_aware_attrs", { attrs });
                            if (qattrs.truthy())
                            {
                                for (py const& item : iterate{ qattrs })
                                {
                                    attr_name = item;
                                    value = attrs[attr_name];
                                    if ((G<"xml.etree.ElementTree._looks_like_prefix_name">())(value).truthy())
                                    {
                                        py const value78 = self.method("_resolve_prefix_name", { value });
                                        qname = value78;
                                        resolved_names.set_item(value, value78);
                                        qnames.method("add", { qname });
                                    }
                                }
                            }
                            else
                            {
                                qattrs = none();
                            }
                        }
                        else
                        {
                            qattrs = none();
                        }
                        // Assign prefixes in lexicographical order of used URIs.
                        parse_qname = self.attr("_qname");
                        parsed_qnames = [&]() -> py { py const comprehension79 = py_dict(); py n; for (py const& item : iterate{ (B<"sorted">()).call({ qnames }, { { "key", w.function("<lambda>", "C14NWriterTarget._start.<locals>.<lambda>", { py_positional("n") }, [](arguments a) -> py
    {
        py n = a[0];
        return n.method("split", { S<"}">(), I<1>() });
    }) } }) }) { n = item; comprehension79.set_item(n, parse_qname(n)); } return comprehension79; }();
                        // Write namespace declarations in prefix order ...
                        if (new_namespaces.truthy())
                        {
                            attr_list = [&]() -> py { py const comprehension80 = py_list(); py prefix; py uri; for (py const& item : iterate{ new_namespaces }) { auto const items81 = unpack(item, 2); uri = items81[0]; prefix = items81[1]; comprehension80.method("append", { py_tuple({ (prefix.truthy() ? (S<"xmlns:">()) + prefix : S<"xmlns">()), uri }) }); } return comprehension80; }();
                            attr_list.method("sort");
                        }
                        else
                        {
                            // almost always empty
                            attr_list = py_list();
                        }
                        // ... followed by attributes in URI+name order
                        if (attrs.truthy())
                        {
                            for (py const& item : iterate{ (B<"sorted">())(attrs.method("items")) })
                            {
                                auto const items83 = unpack(item, 2);
                                k = items83[0];
                                v = items83[1];
                                if (((!qattrs.is_none()) && qattrs.contains(k) && resolved_names.contains(v)))
                                {
                                    v = parsed_qnames[resolved_names[v]][I<0>()];
                                }
                                auto const items84 = unpack(parsed_qnames[k], 3);
                                attr_qname = items84[0];
                                attr_name = items84[1];
                                uri = items84[2];
                                // No prefix for attributes in default ('') namespace.
                                attr_list.method("append", { py_tuple({ (uri.truthy() ? attr_qname : attr_name), v }) });
                            }
                        }
                        // Honour xml:space attributes.
                        space_behaviour = attrs.method("get", { S<"{http://www.w3.org/XML/1998/namespace}space">() });
                        self.attr("_preserve_space").method("append", { (space_behaviour.truthy() ? boolean(space_behaviour == (S<"preserve">())) : self.attr("_preserve_space")[I<-1>()]) });
                        // Write the tag.
                        write = self.attr("_write");
                        write((S<"<">()) + parsed_qnames[tag][I<0>()]);
                        if (attr_list.truthy())
                        {
                            write((S<"">()).method("join", { [&]() -> py { py const comprehension85 = py_list(); py k; py v; for (py const& item : iterate{ attr_list }) { auto const items86 = unpack(item, 2); k = items86[0]; v = items86[1]; comprehension85.method("append", { S(std::string{ " " } + k.str() + "=\"" + (G<"xml.etree.ElementTree._escape_attrib_c14n">())(v).str() + "\"") }); } return comprehension85; }() }));
                        }
                        write(S<">">());
                        // Write the resolved qname text content.
                        if (!qname_text.is_none())
                        {
                            write((G<"xml.etree.ElementTree._escape_cdata_c14n">())(parsed_qnames[resolved_names[qname_text]][I<0>()]));
                        }
                        self.set_attr("_root_seen", boolean(true));
                        self.attr("_ns_stack").method("append", { py_list() });
                        return none();
                    }) },
                { "end", w.method("C14NWriterTarget", "end", { py_positional("tag") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tag = a[1];
                        if (self.attr("_ignored_depth").truthy())
                        {
                            {
                                py const target87 = self;
                                target87.set_attr("_ignored_depth", inplace(NEOS_PY_SUBTRACT, target87.attr("_ignored_depth"), I<1>()));
                            }
                            return none();
                        }
                        if (self.attr("_data").truthy())
                        {
                            self.method("_flush");
                        }
                        self.method("_write", { S(std::string{ "</" } + self.method("_qname", { tag })[I<0>()].str() + ">") });
                        self.attr("_preserve_space").method("pop");
                        self.set_attr("_root_done", boolean(py{ self.attr("_preserve_space").size() } == (I<1>())));
                        self.attr("_declared_ns_stack").method("pop");
                        self.attr("_ns_stack").method("pop");
                        return none();
                    }) },
                { "comment", w.method("C14NWriterTarget", "comment", { py_positional("text") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py text = a[1];
                        if (!self.attr("_with_comments").truthy())
                        {
                            return none();
                        }
                        if (self.attr("_ignored_depth").truthy())
                        {
                            return none();
                        }
                        if (self.attr("_root_done").truthy())
                        {
                            self.method("_write", { S<"\n">() });
                        }
                        else if ((self.attr("_root_seen").truthy() && self.attr("_data").truthy()))
                        {
                            self.method("_flush");
                        }
                        self.method("_write", { S(std::string{ "<!--" } + (G<"xml.etree.ElementTree._escape_cdata_c14n">())(text).str() + "-->") });
                        if (!self.attr("_root_seen").truthy())
                        {
                            self.method("_write", { S<"\n">() });
                        }
                        return none();
                    }) },
                { "pi", w.method("C14NWriterTarget", "pi", { py_positional("target"), py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py target = a[1];
                        py data = a[2];
                        if (self.attr("_ignored_depth").truthy())
                        {
                            return none();
                        }
                        if (self.attr("_root_done").truthy())
                        {
                            self.method("_write", { S<"\n">() });
                        }
                        else if ((self.attr("_root_seen").truthy() && self.attr("_data").truthy()))
                        {
                            self.method("_flush");
                        }
                        self.method("_write", { (data.truthy() ? S(std::string{ "<?" } + target.str() + " " + (G<"xml.etree.ElementTree._escape_cdata_c14n">())(data).str() + "?>") : S(std::string{ "<?" } + target.str() + "?>")) });
                        if (!self.attr("_root_seen").truthy())
                        {
                            self.method("_write", { S<"\n">() });
                        }
                        return none();
                    }) }, }));

            define("xml.etree.ElementTree", "_escape_cdata_c14n", w.function("_escape_cdata_c14n", "_escape_cdata_c14n", { py_positional("text") }, [](arguments a) -> py
                {
                    py text = a[0];
                    try
                    {
                        // it's worth avoiding do-nothing calls for strings that are
                        // shorter than 500 character, or so.  assume that's, by far,
                        // the most common case in most applications.
                        if (text.contains((S<"&">())))
                        {
                            text = text.method("replace", { S<"&">(), S<"&amp;">() });
                        }
                        if (text.contains((S<"<">())))
                        {
                            text = text.method("replace", { S<"<">(), S<"&lt;">() });
                        }
                        if (text.contains((S<">">())))
                        {
                            text = text.method("replace", { S<">">(), S<"&gt;">() });
                        }
                        if (text.contains((S<"\r">())))
                        {
                            text = text.method("replace", { S<"\r">(), S<"&#xD;">() });
                        }
                        return text;
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler88;
                        if (matches(handler88.exception, py_tuple({ B<"TypeError">(), B<"AttributeError">() })))
                        {
                            (G<"xml.etree.ElementTree._raise_serialization_error">())(text);
                        }
                        else
                            py_reraise(handler88.exception);
                    }
                    return none();
                }));

            define("xml.etree.ElementTree", "_escape_attrib_c14n", w.function("_escape_attrib_c14n", "_escape_attrib_c14n", { py_positional("text") }, [](arguments a) -> py
                {
                    py text = a[0];
                    try
                    {
                        if (text.contains((S<"&">())))
                        {
                            text = text.method("replace", { S<"&">(), S<"&amp;">() });
                        }
                        if (text.contains((S<"<">())))
                        {
                            text = text.method("replace", { S<"<">(), S<"&lt;">() });
                        }
                        if (text.contains((S<"\"">())))
                        {
                            text = text.method("replace", { S<"\"">(), S<"&quot;">() });
                        }
                        if (text.contains((S<"\t">())))
                        {
                            text = text.method("replace", { S<"\t">(), S<"&#x9;">() });
                        }
                        if (text.contains((S<"\n">())))
                        {
                            text = text.method("replace", { S<"\n">(), S<"&#xA;">() });
                        }
                        if (text.contains((S<"\r">())))
                        {
                            text = text.method("replace", { S<"\r">(), S<"&#xD;">() });
                        }
                        return text;
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler89;
                        if (matches(handler89.exception, py_tuple({ B<"TypeError">(), B<"AttributeError">() })))
                        {
                            (G<"xml.etree.ElementTree._raise_serialization_error">())(text);
                        }
                        else
                            py_reraise(handler89.exception);
                    }
                    return none();
                }));

            // --------------------------------------------------------------------
            // Import the C accelerators
            define("xml.etree.ElementTree", "_Element_Py", G<"xml.etree.ElementTree.Element">());
        }

        bool xml_etree_ElementTree_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_xml_etree_ElementTree();
                }
                if (auto const doc = xml_etree_ElementTree_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("warnings", py{ fn_import("warnings") }.release());
                aAttributes.emplace_back("io", py{ fn_import("io") }.release());
                aAttributes.emplace_back("collections", py{ fn_import("collections") }.release());
                aAttributes.emplace_back("collections", py{ fn_import("collections") }.release());
                aAttributes.emplace_back("contextlib", py{ fn_import("contextlib") }.release());
                aAttributes.emplace_back("weakref", py{ fn_import("weakref") }.release());
                aAttributes.emplace_back("ElementPath", py{ fn_import("xml.etree.ElementPath") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("xml.etree.ElementTree", xml_etree_ElementTree_module);
    }
}
