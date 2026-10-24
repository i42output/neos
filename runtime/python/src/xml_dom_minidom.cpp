/*
  xml_dom_minidom.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/xml/dom/minidom.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module xml.dom.minidom (CPython 3.13's Lib/xml/dom/minidom.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "xml_dom_minidom_docs.hpp"

namespace neos::python_runtime::detail::xml_dom_minidom_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "xml.dom.minidom.name"))
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
                py_raise("SystemError", std::string{ "xml.dom.minidom: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"xml.dom.minidom.name">()))
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
            if (modules_imported("xml.dom.minidom"))
                return py{ fn_import("xml.dom.minidom") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "xml.dom.minidom" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "xml.dom.minidom", xml_dom_minidom_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- xml.dom.minidom (Lib/xml/dom/minidom.py)

        void make_xml_dom_minidom()
        {
            static module_writer const& w = writer("xml.dom.minidom");
            // This is used by the ID-cache invalidation checks; the list isn't
            // actually complete, since the nodes being checked will never be the
            // DOCUMENT_NODE or DOCUMENT_FRAGMENT_NODE.  (The node being checked is
            // the node being added or removed, not the node being modified.)
            // 
            define("xml.dom.minidom", "_nodeTypes_with_children", py_tuple({ (Mod<"xml.dom">().attr("Node")).attr("ELEMENT_NODE"), (Mod<"xml.dom">().attr("Node")).attr("ENTITY_REFERENCE_NODE") }));
            // The white space characters of the XML specification (see XML 1.0, 2.3).
            define("xml.dom.minidom", "_XML_WHITESPACE", S<" \t\r\n">());
                // Node interfaces from Level 3 (WD 9 April 2002)
                // The "user data" functions use a dictionary that is only present
                // if some user data has been set, so be careful not to assume it
                // exists.
                // minidom-specific API:
                // A Node is its own context manager, to ensure that an unlink() call occurs.
                // This is similar to how a file object works.
            define("xml.dom.minidom", "Node", w.make_class("Node", { Mod<"xml.dom">().attr("Node") }, {
                { "namespaceURI", none() },
                { "parentNode", none() },
                { "ownerDocument", none() },
                { "nextSibling", none() },
                { "previousSibling", none() },
                { "prefix", Mod<"xml.dom">().attr("EMPTY_PREFIX") },
                { "__bool__", w.method("Node", "__bool__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(true);
                    }) },
                { "toxml", w.method("Node", "toxml", { py_positional("encoding", none()), py_positional("standalone", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py encoding = a[1];
                        py standalone = a[2];
                        return self.method("toprettyxml", { S<"">(), S<"">(), encoding, standalone });
                    }) },
                { "toprettyxml", w.method("Node", "toprettyxml", { py_positional("indent", S<"\t">()), py_positional("newl", S<"\n">()), py_positional("encoding", none()), py_positional("standalone", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py indent = a[1];
                        py newl = a[2];
                        py encoding = a[3];
                        py standalone = a[4];
                        py writer;
                        if (encoding.is_none())
                        {
                            writer = (Mod<"io">().attr("StringIO"))();
                        }
                        else
                        {
                            writer = (Mod<"io">().attr("TextIOWrapper")).call({ (Mod<"io">().attr("BytesIO"))() }, { { "encoding", encoding }, { "errors", S<"xmlcharrefreplace">() }, { "newline", S<"\n">() } });
                        }
                        if (self.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("DOCUMENT_NODE"))
                        {
                            // Can pass encoding only to document, to put it into XML header
                            self.method("writexml", { writer, S<"">(), indent, newl, encoding, standalone });
                        }
                        else
                        {
                            self.method("writexml", { writer, S<"">(), indent, newl });
                        }
                        if (encoding.is_none())
                        {
                            return writer.method("getvalue");
                        }
                        else
                        {
                            return writer.method("detach").method("getvalue");
                        }
                        return none();
                    }) },
                { "hasChildNodes", w.method("Node", "hasChildNodes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (B<"bool">())(self.attr("childNodes"));
                    }) },
                { "_get_childNodes", w.method("Node", "_get_childNodes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("childNodes");
                    }) },
                { "_get_firstChild", w.method("Node", "_get_firstChild", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("childNodes").truthy())
                        {
                            return self.attr("childNodes")[I<0>()];
                        }
                        return none();
                    }) },
                { "_get_lastChild", w.method("Node", "_get_lastChild", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("childNodes").truthy())
                        {
                            return self.attr("childNodes")[I<-1>()];
                        }
                        return none();
                    }) },
                { "insertBefore", w.method("Node", "insertBefore", { py_positional("newChild"), py_positional("refChild") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py newChild = a[1];
                        py refChild = a[2];
                        py c;
                        py index;
                        py node;
                        if (newChild.attr("nodeType") == self.attr("DOCUMENT_FRAGMENT_NODE"))
                        {
                            for (py const& item : iterate{ (B<"tuple">())(newChild.attr("childNodes")) })
                            {
                                c = item;
                                self.method("insertBefore", { c, refChild });
                            }
                            // ## The DOM does not clearly specify what to return in this case
                            return newChild;
                        }
                        if (!self.attr("_child_node_types").contains(newChild.attr("nodeType")))
                        {
                            py_raise((Mod<"xml.dom">().attr("HierarchyRequestErr"))((S<"%s cannot be child of %s">()) % py_tuple({ S(newChild.repr()), S(self.repr()) })));
                        }
                        if (!newChild.attr("parentNode").is_none())
                        {
                            newChild.attr("parentNode").method("removeChild", { newChild });
                        }
                        if (refChild.is_none())
                        {
                            self.method("appendChild", { newChild });
                        }
                        else
                        {
                            try
                            {
                                index = self.attr("childNodes").method("index", { refChild });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler2;
                                if (matches(handler2.exception, B<"ValueError">()))
                                {
                                    py_raise((Mod<"xml.dom">().attr("NotFoundErr"))());
                                }
                                else
                                    py_reraise(handler2.exception);
                            }
                            if ((G<"xml.dom.minidom._nodeTypes_with_children">()).contains(newChild.attr("nodeType")))
                            {
                                (G<"xml.dom.minidom._clear_id_cache">())(self);
                            }
                            self.attr("childNodes").method("insert", { index, newChild });
                            newChild.set_attr("nextSibling", refChild);
                            refChild.set_attr("previousSibling", newChild);
                            if (index.truthy())
                            {
                                node = self.attr("childNodes")[index - (I<1>())];
                                node.set_attr("nextSibling", newChild);
                                newChild.set_attr("previousSibling", node);
                            }
                            else
                            {
                                newChild.set_attr("previousSibling", none());
                            }
                            newChild.set_attr("parentNode", self);
                        }
                        return newChild;
                    }) },
                { "appendChild", w.method("Node", "appendChild", { py_positional("node") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py node = a[1];
                        py c;
                        if (node.attr("nodeType") == self.attr("DOCUMENT_FRAGMENT_NODE"))
                        {
                            for (py const& item : iterate{ (B<"tuple">())(node.attr("childNodes")) })
                            {
                                c = item;
                                self.method("appendChild", { c });
                            }
                            // ## The DOM does not clearly specify what to return in this case
                            return node;
                        }
                        if (!self.attr("_child_node_types").contains(node.attr("nodeType")))
                        {
                            py_raise((Mod<"xml.dom">().attr("HierarchyRequestErr"))((S<"%s cannot be child of %s">()) % py_tuple({ S(node.repr()), S(self.repr()) })));
                        }
                        else if ((G<"xml.dom.minidom._nodeTypes_with_children">()).contains(node.attr("nodeType")))
                        {
                            (G<"xml.dom.minidom._clear_id_cache">())(self);
                        }
                        if (!node.attr("parentNode").is_none())
                        {
                            node.attr("parentNode").method("removeChild", { node });
                        }
                        (G<"xml.dom.minidom._append_child">())(self, node);
                        node.set_attr("nextSibling", none());
                        return node;
                    }) },
                { "replaceChild", w.method("Node", "replaceChild", { py_positional("newChild"), py_positional("oldChild") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py newChild = a[1];
                        py oldChild = a[2];
                        py index;
                        py refChild;
                        if (newChild.attr("nodeType") == self.attr("DOCUMENT_FRAGMENT_NODE"))
                        {
                            refChild = oldChild.attr("nextSibling");
                            self.method("removeChild", { oldChild });
                            return self.method("insertBefore", { newChild, refChild });
                        }
                        if (!self.attr("_child_node_types").contains(newChild.attr("nodeType")))
                        {
                            py_raise((Mod<"xml.dom">().attr("HierarchyRequestErr"))((S<"%s cannot be child of %s">()) % py_tuple({ S(newChild.repr()), S(self.repr()) })));
                        }
                        if (newChild.is(oldChild))
                        {
                            return none();
                        }
                        if (!newChild.attr("parentNode").is_none())
                        {
                            newChild.attr("parentNode").method("removeChild", { newChild });
                        }
                        try
                        {
                            index = self.attr("childNodes").method("index", { oldChild });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler4;
                            if (matches(handler4.exception, B<"ValueError">()))
                            {
                                py_raise((Mod<"xml.dom">().attr("NotFoundErr"))());
                            }
                            else
                                py_reraise(handler4.exception);
                        }
                        self.attr("childNodes").set_item(index, newChild);
                        newChild.set_attr("parentNode", self);
                        oldChild.set_attr("parentNode", none());
                        if (((G<"xml.dom.minidom._nodeTypes_with_children">()).contains(newChild.attr("nodeType")) || (G<"xml.dom.minidom._nodeTypes_with_children">()).contains(oldChild.attr("nodeType"))))
                        {
                            (G<"xml.dom.minidom._clear_id_cache">())(self);
                        }
                        newChild.set_attr("nextSibling", oldChild.attr("nextSibling"));
                        newChild.set_attr("previousSibling", oldChild.attr("previousSibling"));
                        oldChild.set_attr("nextSibling", none());
                        oldChild.set_attr("previousSibling", none());
                        if (newChild.attr("previousSibling").truthy())
                        {
                            newChild.attr("previousSibling").set_attr("nextSibling", newChild);
                        }
                        if (newChild.attr("nextSibling").truthy())
                        {
                            newChild.attr("nextSibling").set_attr("previousSibling", newChild);
                        }
                        return oldChild;
                    }) },
                { "removeChild", w.method("Node", "removeChild", { py_positional("oldChild") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py oldChild = a[1];
                        try
                        {
                            self.attr("childNodes").method("remove", { oldChild });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler5;
                            if (matches(handler5.exception, B<"ValueError">()))
                            {
                                py_raise((Mod<"xml.dom">().attr("NotFoundErr"))());
                            }
                            else
                                py_reraise(handler5.exception);
                        }
                        if (!oldChild.attr("nextSibling").is_none())
                        {
                            oldChild.attr("nextSibling").set_attr("previousSibling", oldChild.attr("previousSibling"));
                        }
                        if (!oldChild.attr("previousSibling").is_none())
                        {
                            oldChild.attr("previousSibling").set_attr("nextSibling", oldChild.attr("nextSibling"));
                        }
                        py const value6 = none();
                        oldChild.set_attr("nextSibling", value6);
                        oldChild.set_attr("previousSibling", value6);
                        if ((G<"xml.dom.minidom._nodeTypes_with_children">()).contains(oldChild.attr("nodeType")))
                        {
                            (G<"xml.dom.minidom._clear_id_cache">())(self);
                        }
                        oldChild.set_attr("parentNode", none());
                        return oldChild;
                    }) },
                { "normalize", w.method("Node", "normalize", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py L;
                        py child;
                        py node;
                        L = py_list();
                        for (py const& item : iterate{ self.attr("childNodes") })
                        {
                            child = item;
                            if (child.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("TEXT_NODE"))
                            {
                                if (!child.attr("data").truthy())
                                {
                                    // empty text node; discard
                                    if (L.truthy())
                                    {
                                        L[I<-1>()].set_attr("nextSibling", child.attr("nextSibling"));
                                    }
                                    if (child.attr("nextSibling").truthy())
                                    {
                                        child.attr("nextSibling").set_attr("previousSibling", child.attr("previousSibling"));
                                    }
                                    child.method("unlink");
                                }
                                else if ((L.truthy() && (L[I<-1>()].attr("nodeType") == child.attr("nodeType"))))
                                {
                                    // collapse text node
                                    node = L[I<-1>()];
                                    node.set_attr("data", node.attr("data") + child.attr("data"));
                                    node.set_attr("nextSibling", child.attr("nextSibling"));
                                    if (child.attr("nextSibling").truthy())
                                    {
                                        child.attr("nextSibling").set_attr("previousSibling", node);
                                    }
                                    child.method("unlink");
                                }
                                else
                                {
                                    L.method("append", { child });
                                }
                            }
                            else
                            {
                                L.method("append", { child });
                                if (child.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("ELEMENT_NODE"))
                                {
                                    child.method("normalize");
                                }
                            }
                        }
                        self.attr("childNodes").set_item(B<"slice">()(none(), none(), none()), L);
                        return none();
                    }) },
                { "cloneNode", w.method("Node", "cloneNode", { py_positional("deep") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py deep = a[1];
                        return (G<"xml.dom.minidom._clone_node">())(self, deep, [&]() -> py { py const t = self.attr("ownerDocument"); return t.truthy() ? t : self; }());
                    }) },
                { "isSupported", w.method("Node", "isSupported", { py_positional("feature"), py_positional("version") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py feature = a[1];
                        py version = a[2];
                        return self.attr("ownerDocument").attr("implementation").method("hasFeature", { feature, version });
                    }) },
                { "_get_localName", w.method("Node", "_get_localName", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "isSameNode", w.method("Node", "isSameNode", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        return boolean(self.is(other));
                    }) },
                { "getInterface", w.method("Node", "getInterface", { py_positional("feature") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py feature = a[1];
                        if (self.method("isSupported", { feature, none() }).truthy())
                        {
                            return self;
                        }
                        else
                        {
                            return none();
                        }
                        return none();
                    }) },
                { "getUserData", w.method("Node", "getUserData", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        try
                        {
                            return self.attr("_user_data")[key][I<0>()];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler8;
                            if (matches(handler8.exception, py_tuple({ B<"AttributeError">(), B<"KeyError">() })))
                            {
                                return none();
                            }
                            else
                                py_reraise(handler8.exception);
                        }
                        return none();
                    }) },
                { "setUserData", w.method("Node", "setUserData", { py_positional("key"), py_positional("data"), py_positional("handler") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py data = a[2];
                        py handler = a[3];
                        py d;
                        py old;
                        old = none();
                        try
                        {
                            d = self.attr("_user_data");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler9;
                            if (matches(handler9.exception, B<"AttributeError">()))
                            {
                                d = py_dict();
                                self.set_attr("_user_data", d);
                            }
                            else
                                py_reraise(handler9.exception);
                        }
                        if (d.contains(key))
                        {
                            old = d[key][I<0>()];
                        }
                        if (data.is_none())
                        {
                            // ignore handlers passed for None
                            handler = none();
                            if (!old.is_none())
                            {
                                del_item(d, key);
                            }
                        }
                        else
                        {
                            d.set_item(key, py_tuple({ data, handler }));
                        }
                        return old;
                    }) },
                { "_call_user_data_handler", w.method("Node", "_call_user_data_handler", { py_positional("operation"), py_positional("src"), py_positional("dst") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py operation = a[1];
                        py src = a[2];
                        py dst = a[3];
                        py data;
                        py handler;
                        py key;
                        if (has_attr(self, "_user_data"))
                        {
                            for (py const& item : iterate{ (B<"list">())(self.attr("_user_data").method("items")) })
                            {
                                auto const items11 = unpack(item, 2);
                                key = items11[0];
                                auto const items12 = unpack(items11[1], 2);
                                data = items12[0];
                                handler = items12[1];
                                if (!handler.is_none())
                                {
                                    handler.method("handle", { operation, key, data, src, dst });
                                }
                            }
                        }
                        return none();
                    }) },
                { "unlink", w.method("Node", "unlink", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py child;
                        py const value13 = none();
                        self.set_attr("parentNode", value13);
                        self.set_attr("ownerDocument", value13);
                        if (self.attr("childNodes").truthy())
                        {
                            for (py const& item : iterate{ self.attr("childNodes") })
                            {
                                child = item;
                                child.method("unlink");
                            }
                            self.set_attr("childNodes", (Mod<"xml.dom.minicompat">().attr("NodeList"))());
                        }
                        self.set_attr("previousSibling", none());
                        self.set_attr("nextSibling", none());
                        return none();
                    }) },
                { "__enter__", w.method("Node", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__exit__", w.method("Node", "__exit__", { py_positional("et"), py_positional("ev"), py_positional("tb") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py et = a[1];
                        py ev = a[2];
                        py tb = a[3];
                        self.method("unlink");
                        return none();
                    }) }, }));

            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (Mod<"xml.dom.minicompat">().attr("defproperty")).call({ G<"xml.dom.minidom.Node">(), S<"firstChild">() }, { { "doc", S<"First child node, or None.">() } });
                    return none();
                }))();
            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (Mod<"xml.dom.minicompat">().attr("defproperty")).call({ G<"xml.dom.minidom.Node">(), S<"lastChild">() }, { { "doc", S<"Last child node, or None.">() } });
                    return none();
                }))();
            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (Mod<"xml.dom.minicompat">().attr("defproperty")).call({ G<"xml.dom.minidom.Node">(), S<"localName">() }, { { "doc", S<"Namespace-local name of this node.">() } });
                    return none();
                }))();
            define("xml.dom.minidom", "_append_child", w.function("_append_child", "_append_child", { py_positional("self"), py_positional("node") }, [](arguments a) -> py
                {
                    py self = a[0];
                    py node = a[1];
                    py childNodes;
                    py last;
                    childNodes = self.attr("childNodes");
                    if (childNodes.truthy())
                    {
                        last = childNodes[I<-1>()];
                        node.set_attr("previousSibling", last);
                        last.set_attr("nextSibling", node);
                    }
                    childNodes.method("append", { node });
                    node.set_attr("parentNode", self);
                    return none();
                }));

            define("xml.dom.minidom", "_write_data", w.function("_write_data", "_write_data", { py_positional("writer"), py_positional("text"), py_positional("attr") }, [](arguments a) -> py
                {
                    py writer = a[0];
                    py text = a[1];
                    py attr = a[2];
                    if (!text.truthy())
                    {
                        return none();
                    }
                    // See the comments in ElementTree.py for behavior and
                    // implementation details.
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
                    if (attr.truthy())
                    {
                        if (text.contains((S<"\"">())))
                        {
                            text = text.method("replace", { S<"\"">(), S<"&quot;">() });
                        }
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
                            text = text.method("replace", { S<"\t">(), S<"&#9;">() });
                        }
                    }
                    writer.method("write", { text });
                    return none();
                }));

            define("xml.dom.minidom", "_get_elements_by_tagName_helper", w.function("_get_elements_by_tagName_helper", "_get_elements_by_tagName_helper", { py_positional("parent"), py_positional("name"), py_positional("rc") }, [](arguments a) -> py
                {
                    py parent = a[0];
                    py name = a[1];
                    py rc = a[2];
                    py node;
                    for (py const& item : iterate{ parent.attr("childNodes") })
                    {
                        node = item;
                        if (((node.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("ELEMENT_NODE")) && ([&]() -> py { py const t = boolean(name == (S<"*">())); return t.truthy() ? t : boolean(node.attr("tagName") == name); }()).truthy()))
                        {
                            rc.method("append", { node });
                        }
                        (G<"xml.dom.minidom._get_elements_by_tagName_helper">())(node, name, rc);
                    }
                    return rc;
                }));

            define("xml.dom.minidom", "_get_elements_by_tagName_ns_helper", w.function("_get_elements_by_tagName_ns_helper", "_get_elements_by_tagName_ns_helper", { py_positional("parent"), py_positional("nsURI"), py_positional("localName"), py_positional("rc") }, [](arguments a) -> py
                {
                    py parent = a[0];
                    py nsURI = a[1];
                    py localName = a[2];
                    py rc = a[3];
                    py node;
                    for (py const& item : iterate{ parent.attr("childNodes") })
                    {
                        node = item;
                        if (node.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("ELEMENT_NODE"))
                        {
                            if ((([&]() -> py { py const t = boolean(localName == (S<"*">())); return t.truthy() ? t : boolean(node.attr("localName") == localName); }()).truthy() && ([&]() -> py { py const t = boolean(nsURI == (S<"*">())); return t.truthy() ? t : boolean(node.attr("namespaceURI") == nsURI); }()).truthy()))
                            {
                                rc.method("append", { node });
                            }
                            (G<"xml.dom.minidom._get_elements_by_tagName_ns_helper">())(node, nsURI, localName, rc);
                        }
                    }
                    return rc;
                }));

            define("xml.dom.minidom", "DocumentFragment", w.make_class("DocumentFragment", { G<"xml.dom.minidom.Node">() }, {
                { "nodeType", (G<"xml.dom.minidom.Node">()).attr("DOCUMENT_FRAGMENT_NODE") },
                { "nodeName", S<"#document-fragment">() },
                { "nodeValue", none() },
                { "attributes", none() },
                { "parentNode", none() },
                { "_child_node_types", py_tuple({ (G<"xml.dom.minidom.Node">()).attr("ELEMENT_NODE"), (G<"xml.dom.minidom.Node">()).attr("TEXT_NODE"), (G<"xml.dom.minidom.Node">()).attr("CDATA_SECTION_NODE"), (G<"xml.dom.minidom.Node">()).attr("ENTITY_REFERENCE_NODE"), (G<"xml.dom.minidom.Node">()).attr("PROCESSING_INSTRUCTION_NODE"), (G<"xml.dom.minidom.Node">()).attr("COMMENT_NODE"), (G<"xml.dom.minidom.Node">()).attr("NOTATION_NODE") }) },
                { "__init__", w.method("DocumentFragment", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("childNodes", (Mod<"xml.dom.minicompat">().attr("NodeList"))());
                        return none();
                    }) }, }));

                // nodeValue and value are set elsewhere
            define("xml.dom.minidom", "Attr", w.make_class("Attr", { G<"xml.dom.minidom.Node">() }, {
                { "__slots__", py_tuple({ S<"_name">(), S<"_value">(), S<"namespaceURI">(), S<"_prefix">(), S<"childNodes">(), S<"_localName">(), S<"ownerDocument">(), S<"ownerElement">() }) },
                { "nodeType", (G<"xml.dom.minidom.Node">()).attr("ATTRIBUTE_NODE") },
                { "attributes", none() },
                { "specified", boolean(false) },
                { "_is_id", boolean(false) },
                { "_child_node_types", py_tuple({ (G<"xml.dom.minidom.Node">()).attr("TEXT_NODE"), (G<"xml.dom.minidom.Node">()).attr("ENTITY_REFERENCE_NODE") }) },
                { "__init__", w.method("Attr", "__init__", { py_positional("qName"), py_positional("namespaceURI", Mod<"xml.dom">().attr("EMPTY_NAMESPACE")), py_positional("localName", none()), py_positional("prefix", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py qName = a[1];
                        py namespaceURI = a[2];
                        py localName = a[3];
                        py prefix = a[4];
                        self.set_attr("ownerElement", none());
                        self.set_attr("ownerDocument", none());
                        self.set_attr("_name", qName);
                        self.set_attr("namespaceURI", namespaceURI);
                        self.set_attr("_prefix", prefix);
                        if (!localName.is_none())
                        {
                            self.set_attr("_localName", localName);
                        }
                        self.set_attr("childNodes", (Mod<"xml.dom.minicompat">().attr("NodeList"))());
                        // Add the single child node that represents the value of the attr
                        self.attr("childNodes").method("append", { (G<"xml.dom.minidom.Text">())() });
                        return none();
                    }) },
                { "_get_localName", w.method("Attr", "_get_localName", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        try
                        {
                            return self.attr("_localName");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler17;
                            if (matches(handler17.exception, B<"AttributeError">()))
                            {
                                return self.attr("nodeName").method("split", { S<":">(), I<1>() })[I<-1>()];
                            }
                            else
                                py_reraise(handler17.exception);
                        }
                        return none();
                    }) },
                { "_get_specified", w.method("Attr", "_get_specified", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("specified");
                    }) },
                { "_get_name", w.method("Attr", "_get_name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_name");
                    }) },
                { "_set_name", w.method("Attr", "_set_name", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        self.set_attr("_name", value);
                        if (!self.attr("ownerElement").is_none())
                        {
                            (G<"xml.dom.minidom._clear_id_cache">())(self.attr("ownerElement"));
                        }
                        return none();
                    }) },
                { "nodeName", (B<"property">())(w.method("Attr", "_get_name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_name");
                    }), w.method("Attr", "_set_name", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        self.set_attr("_name", value);
                        if (!self.attr("ownerElement").is_none())
                        {
                            (G<"xml.dom.minidom._clear_id_cache">())(self.attr("ownerElement"));
                        }
                        return none();
                    })) },
                { "name", (B<"property">())(w.method("Attr", "_get_name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_name");
                    }), w.method("Attr", "_set_name", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        self.set_attr("_name", value);
                        if (!self.attr("ownerElement").is_none())
                        {
                            (G<"xml.dom.minidom._clear_id_cache">())(self.attr("ownerElement"));
                        }
                        return none();
                    })) },
                { "_get_value", w.method("Attr", "_get_value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_value");
                    }) },
                { "_set_value", w.method("Attr", "_set_value", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        self.set_attr("_value", value);
                        self.attr("childNodes")[I<0>()].set_attr("data", value);
                        if (!self.attr("ownerElement").is_none())
                        {
                            (G<"xml.dom.minidom._clear_id_cache">())(self.attr("ownerElement"));
                        }
                        self.attr("childNodes")[I<0>()].set_attr("data", value);
                        return none();
                    }) },
                { "nodeValue", (B<"property">())(w.method("Attr", "_get_value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_value");
                    }), w.method("Attr", "_set_value", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        self.set_attr("_value", value);
                        self.attr("childNodes")[I<0>()].set_attr("data", value);
                        if (!self.attr("ownerElement").is_none())
                        {
                            (G<"xml.dom.minidom._clear_id_cache">())(self.attr("ownerElement"));
                        }
                        self.attr("childNodes")[I<0>()].set_attr("data", value);
                        return none();
                    })) },
                { "value", (B<"property">())(w.method("Attr", "_get_value", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_value");
                    }), w.method("Attr", "_set_value", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        self.set_attr("_value", value);
                        self.attr("childNodes")[I<0>()].set_attr("data", value);
                        if (!self.attr("ownerElement").is_none())
                        {
                            (G<"xml.dom.minidom._clear_id_cache">())(self.attr("ownerElement"));
                        }
                        self.attr("childNodes")[I<0>()].set_attr("data", value);
                        return none();
                    })) },
                { "_get_prefix", w.method("Attr", "_get_prefix", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_prefix");
                    }) },
                { "_set_prefix", w.method("Attr", "_set_prefix", { py_positional("prefix") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prefix = a[1];
                        py newName;
                        py nsuri;
                        nsuri = self.attr("namespaceURI");
                        if (prefix == (S<"xmlns">()))
                        {
                            if ((nsuri.truthy() && (nsuri != (Mod<"xml.dom">().attr("XMLNS_NAMESPACE")))))
                            {
                                py_raise((Mod<"xml.dom">().attr("NamespaceErr"))(S<"illegal use of 'xmlns' prefix for the wrong namespace">()));
                            }
                        }
                        self.set_attr("_prefix", prefix);
                        if (prefix.is_none())
                        {
                            newName = self.attr("localName");
                        }
                        else
                        {
                            newName = (S<"%s:%s">()) % py_tuple({ prefix, self.attr("localName") });
                        }
                        if (self.attr("ownerElement").truthy())
                        {
                            (G<"xml.dom.minidom._clear_id_cache">())(self.attr("ownerElement"));
                        }
                        self.set_attr("name", newName);
                        return none();
                    }) },
                { "prefix", (B<"property">())(w.method("Attr", "_get_prefix", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_prefix");
                    }), w.method("Attr", "_set_prefix", { py_positional("prefix") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py prefix = a[1];
                        py newName;
                        py nsuri;
                        nsuri = self.attr("namespaceURI");
                        if (prefix == (S<"xmlns">()))
                        {
                            if ((nsuri.truthy() && (nsuri != (Mod<"xml.dom">().attr("XMLNS_NAMESPACE")))))
                            {
                                py_raise((Mod<"xml.dom">().attr("NamespaceErr"))(S<"illegal use of 'xmlns' prefix for the wrong namespace">()));
                            }
                        }
                        self.set_attr("_prefix", prefix);
                        if (prefix.is_none())
                        {
                            newName = self.attr("localName");
                        }
                        else
                        {
                            newName = (S<"%s:%s">()) % py_tuple({ prefix, self.attr("localName") });
                        }
                        if (self.attr("ownerElement").truthy())
                        {
                            (G<"xml.dom.minidom._clear_id_cache">())(self.attr("ownerElement"));
                        }
                        self.set_attr("name", newName);
                        return none();
                    })) },
                { "unlink", w.method("Attr", "unlink", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py child;
                        py elem;
                        elem = self.attr("ownerElement");
                        if (!elem.is_none())
                        {
                            del_item(elem.attr("_attrs"), self.attr("nodeName"));
                            del_item(elem.attr("_attrsNS"), py_tuple({ self.attr("namespaceURI"), self.attr("localName") }));
                            if (self.attr("_is_id").truthy())
                            {
                                self.set_attr("_is_id", boolean(false));
                                {
                                    py const target18 = elem;
                                    target18.set_attr("_magic_id_nodes", inplace(NEOS_PY_SUBTRACT, target18.attr("_magic_id_nodes"), I<1>()));
                                }
                                {
                                    py const target19 = self.attr("ownerDocument");
                                    target19.set_attr("_magic_id_count", inplace(NEOS_PY_SUBTRACT, target19.attr("_magic_id_count"), I<1>()));
                                }
                            }
                        }
                        for (py const& item : iterate{ self.attr("childNodes") })
                        {
                            child = item;
                            child.method("unlink");
                        }
                        del_item(self.attr("childNodes"), B<"slice">()(none(), none(), none()));
                        return none();
                    }) },
                { "_get_isId", w.method("Attr", "_get_isId", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py doc;
                        py elem;
                        py info;
                        if (self.attr("_is_id").truthy())
                        {
                            return boolean(true);
                        }
                        doc = self.attr("ownerDocument");
                        elem = self.attr("ownerElement");
                        if ((doc.is_none() || elem.is_none()))
                        {
                            return boolean(false);
                        }
                        info = doc.method("_get_elem_info", { elem });
                        if (info.is_none())
                        {
                            return boolean(false);
                        }
                        if (self.attr("namespaceURI").truthy())
                        {
                            return info.method("isIdNS", { self.attr("namespaceURI"), self.attr("localName") });
                        }
                        else
                        {
                            return info.method("isId", { self.attr("nodeName") });
                        }
                        return none();
                    }) },
                { "_get_schemaType", w.method("Attr", "_get_schemaType", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py doc;
                        py elem;
                        py info;
                        doc = self.attr("ownerDocument");
                        elem = self.attr("ownerElement");
                        if ((doc.is_none() || elem.is_none()))
                        {
                            return G<"xml.dom.minidom._no_type">();
                        }
                        info = doc.method("_get_elem_info", { elem });
                        if (info.is_none())
                        {
                            return G<"xml.dom.minidom._no_type">();
                        }
                        if (self.attr("namespaceURI").truthy())
                        {
                            return info.method("getAttributeTypeNS", { self.attr("namespaceURI"), self.attr("localName") });
                        }
                        else
                        {
                            return info.method("getAttributeType", { self.attr("nodeName") });
                        }
                        return none();
                    }) }, }));

            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (Mod<"xml.dom.minicompat">().attr("defproperty")).call({ G<"xml.dom.minidom.Attr">(), S<"isId">() }, { { "doc", S<"True if this attribute is an ID.">() } });
                    return none();
                }))();
            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (Mod<"xml.dom.minicompat">().attr("defproperty")).call({ G<"xml.dom.minidom.Attr">(), S<"localName">() }, { { "doc", S<"Namespace-local name of this attribute.">() } });
                    return none();
                }))();
            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (Mod<"xml.dom.minicompat">().attr("defproperty")).call({ G<"xml.dom.minidom.Attr">(), S<"schemaType">() }, { { "doc", S<"Schema type for this attribute.">() } });
                    return none();
                }))();
                // same as set
            define("xml.dom.minidom", "NamedNodeMap", w.make_class("NamedNodeMap", { B<"object">() }, {
                { "__slots__", py_tuple({ S<"_attrs">(), S<"_attrsNS">(), S<"_ownerElement">() }) },
                { "__init__", w.method("NamedNodeMap", "__init__", { py_positional("attrs"), py_positional("attrsNS"), py_positional("ownerElement") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attrs = a[1];
                        py attrsNS = a[2];
                        py ownerElement = a[3];
                        self.set_attr("_attrs", attrs);
                        self.set_attr("_attrsNS", attrsNS);
                        self.set_attr("_ownerElement", ownerElement);
                        return none();
                    }) },
                { "_get_length", w.method("NamedNodeMap", "_get_length", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py{ self.attr("_attrs").size() };
                    }) },
                { "item", w.method("NamedNodeMap", "item", { py_positional("index") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py index = a[1];
                        try
                        {
                            return self[(B<"list">())(self.attr("_attrs").method("keys"))[index]];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler21;
                            if (matches(handler21.exception, B<"IndexError">()))
                            {
                                return none();
                            }
                            else
                                py_reraise(handler21.exception);
                        }
                        return none();
                    }) },
                { "items", w.method("NamedNodeMap", "items", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py L;
                        py node;
                        L = py_list();
                        for (py const& item : iterate{ self.attr("_attrs").method("values") })
                        {
                            node = item;
                            L.method("append", { py_tuple({ node.attr("nodeName"), node.attr("value") }) });
                        }
                        return L;
                    }) },
                { "itemsNS", w.method("NamedNodeMap", "itemsNS", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py L;
                        py node;
                        L = py_list();
                        for (py const& item : iterate{ self.attr("_attrs").method("values") })
                        {
                            node = item;
                            L.method("append", { py_tuple({ py_tuple({ node.attr("namespaceURI"), node.attr("localName") }), node.attr("value") }) });
                        }
                        return L;
                    }) },
                { "__contains__", w.method("NamedNodeMap", "__contains__", { py_positional("key") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        if (isinstance(key, B<"str">()))
                        {
                            return boolean(self.attr("_attrs").contains(key));
                        }
                        else
                        {
                            return boolean(self.attr("_attrsNS").contains(key));
                        }
                        return none();
                    }) },
                { "keys", w.method("NamedNodeMap", "keys", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_attrs").method("keys");
                    }) },
                { "keysNS", w.method("NamedNodeMap", "keysNS", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_attrsNS").method("keys");
                    }) },
                { "values", w.method("NamedNodeMap", "values", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_attrs").method("values");
                    }) },
                { "get", w.method("NamedNodeMap", "get", { py_positional("name"), py_positional("value", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py value = a[2];
                        return self.attr("_attrs").method("get", { name, value });
                    }) },
                { "__len__", w.method("NamedNodeMap", "_get_length", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py{ self.attr("_attrs").size() };
                    }) },
                { "_cmp", w.method("NamedNodeMap", "_cmp", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (self.attr("_attrs").is((B<"getattr">())(other, S<"_attrs">(), none())))
                        {
                            return I<0>();
                        }
                        else
                        {
                            return boolean((B<"id">())(self) > (B<"id">())(other)) - boolean((B<"id">())(self) < (B<"id">())(other));
                        }
                        return none();
                    }) },
                { "__eq__", w.method("NamedNodeMap", "__eq__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        return boolean(self.method("_cmp", { other }) == (I<0>()));
                    }) },
                { "__ge__", w.method("NamedNodeMap", "__ge__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        return boolean(self.method("_cmp", { other }) >= (I<0>()));
                    }) },
                { "__gt__", w.method("NamedNodeMap", "__gt__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        return boolean(self.method("_cmp", { other }) > (I<0>()));
                    }) },
                { "__le__", w.method("NamedNodeMap", "__le__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        return boolean(self.method("_cmp", { other }) <= (I<0>()));
                    }) },
                { "__lt__", w.method("NamedNodeMap", "__lt__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        return boolean(self.method("_cmp", { other }) < (I<0>()));
                    }) },
                { "__getitem__", w.method("NamedNodeMap", "__getitem__", { py_positional("attname_or_tuple") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attname_or_tuple = a[1];
                        if (isinstance(attname_or_tuple, B<"tuple">()))
                        {
                            return self.attr("_attrsNS")[attname_or_tuple];
                        }
                        else
                        {
                            return self.attr("_attrs")[attname_or_tuple];
                        }
                        return none();
                    }) },
                { "__setitem__", w.method("NamedNodeMap", "__setitem__", { py_positional("attname"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attname = a[1];
                        py value = a[2];
                        py node;
                        if (isinstance(value, B<"str">()))
                        {
                            try
                            {
                                node = self.attr("_attrs")[attname];
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler24;
                                if (matches(handler24.exception, B<"KeyError">()))
                                {
                                    node = (G<"xml.dom.minidom.Attr">())(attname);
                                    node.set_attr("ownerDocument", self.attr("_ownerElement").attr("ownerDocument"));
                                    self.method("setNamedItem", { node });
                                }
                                else
                                    py_reraise(handler24.exception);
                            }
                            node.set_attr("value", value);
                        }
                        else
                        {
                            if (!isinstance(value, G<"xml.dom.minidom.Attr">()))
                            {
                                py_raise((B<"TypeError">())(S<"value must be a string or Attr object">()));
                            }
                            node = value;
                            self.method("setNamedItem", { node });
                        }
                        return none();
                    }) },
                { "getNamedItem", w.method("NamedNodeMap", "getNamedItem", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        try
                        {
                            return self.attr("_attrs")[name];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler25;
                            if (matches(handler25.exception, B<"KeyError">()))
                            {
                                return none();
                            }
                            else
                                py_reraise(handler25.exception);
                        }
                        return none();
                    }) },
                { "getNamedItemNS", w.method("NamedNodeMap", "getNamedItemNS", { py_positional("namespaceURI"), py_positional("localName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespaceURI = a[1];
                        py localName = a[2];
                        try
                        {
                            return self.attr("_attrsNS")[py_tuple({ namespaceURI, localName })];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler26;
                            if (matches(handler26.exception, B<"KeyError">()))
                            {
                                return none();
                            }
                            else
                                py_reraise(handler26.exception);
                        }
                        return none();
                    }) },
                { "removeNamedItem", w.method("NamedNodeMap", "removeNamedItem", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py n;
                        n = self.method("getNamedItem", { name });
                        if (!n.is_none())
                        {
                            (G<"xml.dom.minidom._clear_id_cache">())(self.attr("_ownerElement"));
                            del_item(self.attr("_attrs"), n.attr("nodeName"));
                            del_item(self.attr("_attrsNS"), py_tuple({ n.attr("namespaceURI"), n.attr("localName") }));
                            if (has_attr(n, "ownerElement"))
                            {
                                n.set_attr("ownerElement", none());
                            }
                            return n;
                        }
                        else
                        {
                            py_raise((Mod<"xml.dom">().attr("NotFoundErr"))());
                        }
                        return none();
                    }) },
                { "removeNamedItemNS", w.method("NamedNodeMap", "removeNamedItemNS", { py_positional("namespaceURI"), py_positional("localName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespaceURI = a[1];
                        py localName = a[2];
                        py n;
                        n = self.method("getNamedItemNS", { namespaceURI, localName });
                        if (!n.is_none())
                        {
                            (G<"xml.dom.minidom._clear_id_cache">())(self.attr("_ownerElement"));
                            del_item(self.attr("_attrsNS"), py_tuple({ n.attr("namespaceURI"), n.attr("localName") }));
                            del_item(self.attr("_attrs"), n.attr("nodeName"));
                            if (has_attr(n, "ownerElement"))
                            {
                                n.set_attr("ownerElement", none());
                            }
                            return n;
                        }
                        else
                        {
                            py_raise((Mod<"xml.dom">().attr("NotFoundErr"))());
                        }
                        return none();
                    }) },
                { "setNamedItem", w.method("NamedNodeMap", "setNamedItem", { py_positional("node") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py node = a[1];
                        py old;
                        if (!isinstance(node, G<"xml.dom.minidom.Attr">()))
                        {
                            py_raise((Mod<"xml.dom">().attr("HierarchyRequestErr"))((S<"%s cannot be child of %s">()) % py_tuple({ S(node.repr()), S(self.repr()) })));
                        }
                        old = self.attr("_attrs").method("get", { node.attr("name") });
                        if (old.truthy())
                        {
                            old.method("unlink");
                        }
                        self.attr("_attrs").set_item(node.attr("name"), node);
                        self.attr("_attrsNS").set_item(py_tuple({ node.attr("namespaceURI"), node.attr("localName") }), node);
                        node.set_attr("ownerElement", self.attr("_ownerElement"));
                        (G<"xml.dom.minidom._clear_id_cache">())(node.attr("ownerElement"));
                        return old;
                    }) },
                { "setNamedItemNS", w.method("NamedNodeMap", "setNamedItemNS", { py_positional("node") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py node = a[1];
                        return self.method("setNamedItem", { node });
                    }) },
                { "__delitem__", w.method("NamedNodeMap", "__delitem__", { py_positional("attname_or_tuple") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attname_or_tuple = a[1];
                        py node;
                        node = self[attname_or_tuple];
                        (G<"xml.dom.minidom._clear_id_cache">())(node.attr("ownerElement"));
                        node.method("unlink");
                        return none();
                    }) },
                { "__getstate__", w.method("NamedNodeMap", "__getstate__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py_tuple({ self.attr("_attrs"), self.attr("_attrsNS"), self.attr("_ownerElement") });
                    }) },
                { "__setstate__", w.method("NamedNodeMap", "__setstate__", { py_positional("state") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py state = a[1];
                        auto const items27 = unpack(state, 3);
                        self.set_attr("_attrs", items27[0]);
                        self.set_attr("_attrsNS", items27[1]);
                        self.set_attr("_ownerElement", items27[2]);
                        return none();
                    }) }, }));

            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (Mod<"xml.dom.minicompat">().attr("defproperty")).call({ G<"xml.dom.minidom.NamedNodeMap">(), S<"length">() }, { { "doc", S<"Number of nodes in the NamedNodeMap.">() } });
                    return none();
                }))();
            define("xml.dom.minidom", "AttributeList", G<"xml.dom.minidom.NamedNodeMap">());
            define("xml.dom.minidom", "TypeInfo", w.make_class("TypeInfo", { B<"object">() }, {
                { "__slots__", py_tuple({ S<"namespace">(), S<"name">() }) },
                { "__init__", w.method("TypeInfo", "__init__", { py_positional("namespace"), py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespace_ = a[1];
                        py name = a[2];
                        self.set_attr("namespace", namespace_);
                        self.set_attr("name", name);
                        return none();
                    }) },
                { "__repr__", w.method("TypeInfo", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("namespace").truthy())
                        {
                            return (S<"<%s %r (from %r)>">()) % py_tuple({ self.attr("__class__").attr("__name__"), self.attr("name"), self.attr("namespace") });
                        }
                        else
                        {
                            return (S<"<%s %r>">()) % py_tuple({ self.attr("__class__").attr("__name__"), self.attr("name") });
                        }
                        return none();
                    }) },
                { "_get_name", w.method("TypeInfo", "_get_name", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("name");
                    }) },
                { "_get_namespace", w.method("TypeInfo", "_get_namespace", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("namespace");
                    }) }, }));

            define("xml.dom.minidom", "_no_type", (G<"xml.dom.minidom.TypeInfo">())(none(), none()));
                // DOM Level 3 attributes, based on the 22 Oct 2002 draft
            define("xml.dom.minidom", "Element", w.make_class("Element", { G<"xml.dom.minidom.Node">() }, {
                { "__slots__", py_tuple({ S<"ownerDocument">(), S<"parentNode">(), S<"tagName">(), S<"nodeName">(), S<"prefix">(), S<"namespaceURI">(), S<"_localName">(), S<"childNodes">(), S<"_attrs">(), S<"_attrsNS">(), S<"nextSibling">(), S<"previousSibling">() }) },
                { "nodeType", (G<"xml.dom.minidom.Node">()).attr("ELEMENT_NODE") },
                { "nodeValue", none() },
                { "schemaType", G<"xml.dom.minidom._no_type">() },
                { "_magic_id_nodes", I<0>() },
                { "_child_node_types", py_tuple({ (G<"xml.dom.minidom.Node">()).attr("ELEMENT_NODE"), (G<"xml.dom.minidom.Node">()).attr("PROCESSING_INSTRUCTION_NODE"), (G<"xml.dom.minidom.Node">()).attr("COMMENT_NODE"), (G<"xml.dom.minidom.Node">()).attr("TEXT_NODE"), (G<"xml.dom.minidom.Node">()).attr("CDATA_SECTION_NODE"), (G<"xml.dom.minidom.Node">()).attr("ENTITY_REFERENCE_NODE") }) },
                { "__init__", w.method("Element", "__init__", { py_positional("tagName"), py_positional("namespaceURI", Mod<"xml.dom">().attr("EMPTY_NAMESPACE")), py_positional("prefix", none()), py_positional("localName", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tagName = a[1];
                        py namespaceURI = a[2];
                        py prefix = a[3];
                        py localName = a[4];
                        self.set_attr("ownerDocument", none());
                        self.set_attr("parentNode", none());
                        py const value28 = tagName;
                        self.set_attr("tagName", value28);
                        self.set_attr("nodeName", value28);
                        self.set_attr("prefix", prefix);
                        self.set_attr("namespaceURI", namespaceURI);
                        self.set_attr("childNodes", (Mod<"xml.dom.minicompat">().attr("NodeList"))());
                        py const value29 = none();
                        self.set_attr("nextSibling", value29);
                        self.set_attr("previousSibling", value29);
                        // Attribute dictionaries are lazily created
                        // attributes are double-indexed:
                        // tagName -> Attribute
                        // URI,localName -> Attribute
                        // in the future: consider lazy generation
                        // of attribute objects this is too tricky
                        // for now because of headaches with
                        // namespaces.
                        self.set_attr("_attrs", none());
                        self.set_attr("_attrsNS", none());
                        return none();
                    }) },
                { "_ensure_attributes", w.method("Element", "_ensure_attributes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_attrs").is_none())
                        {
                            self.set_attr("_attrs", py_dict());
                            self.set_attr("_attrsNS", py_dict());
                        }
                        return none();
                    }) },
                { "_get_localName", w.method("Element", "_get_localName", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        try
                        {
                            return self.attr("_localName");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler30;
                            if (matches(handler30.exception, B<"AttributeError">()))
                            {
                                return self.attr("tagName").method("split", { S<":">(), I<1>() })[I<-1>()];
                            }
                            else
                                py_reraise(handler30.exception);
                        }
                        return none();
                    }) },
                { "_get_tagName", w.method("Element", "_get_tagName", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("tagName");
                    }) },
                { "unlink", w.method("Element", "unlink", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attr;
                        if (!self.attr("_attrs").is_none())
                        {
                            for (py const& item : iterate{ (B<"list">())(self.attr("_attrs").method("values")) })
                            {
                                attr = item;
                                attr.method("unlink");
                            }
                        }
                        self.set_attr("_attrs", none());
                        self.set_attr("_attrsNS", none());
                        (G<"xml.dom.minidom.Node">()).method("unlink", { self });
                        return none();
                    }) },
                { "getAttribute", w.method("Element", "getAttribute", { py_positional("attname") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attname = a[1];
                        if (self.attr("_attrs").is_none())
                        {
                            return S<"">();
                        }
                        try
                        {
                            return self.attr("_attrs")[attname].attr("value");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler32;
                            if (matches(handler32.exception, B<"KeyError">()))
                            {
                                return S<"">();
                            }
                            else
                                py_reraise(handler32.exception);
                        }
                        return none();
                    }) },
                { "getAttributeNS", w.method("Element", "getAttributeNS", { py_positional("namespaceURI"), py_positional("localName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespaceURI = a[1];
                        py localName = a[2];
                        if (self.attr("_attrsNS").is_none())
                        {
                            return S<"">();
                        }
                        try
                        {
                            return self.attr("_attrsNS")[py_tuple({ namespaceURI, localName })].attr("value");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler33;
                            if (matches(handler33.exception, B<"KeyError">()))
                            {
                                return S<"">();
                            }
                            else
                                py_reraise(handler33.exception);
                        }
                        return none();
                    }) },
                { "setAttribute", w.method("Element", "setAttribute", { py_positional("attname"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attname = a[1];
                        py value = a[2];
                        py attr;
                        attr = self.method("getAttributeNode", { attname });
                        if (attr.is_none())
                        {
                            attr = (G<"xml.dom.minidom.Attr">())(attname);
                            attr.set_attr("value", value);
                            attr.set_attr("ownerDocument", self.attr("ownerDocument"));
                            self.method("setAttributeNode", { attr });
                        }
                        else if (value != attr.attr("value"))
                        {
                            attr.set_attr("value", value);
                            if (attr.attr("isId").truthy())
                            {
                                (G<"xml.dom.minidom._clear_id_cache">())(self);
                            }
                        }
                        return none();
                    }) },
                { "setAttributeNS", w.method("Element", "setAttributeNS", { py_positional("namespaceURI"), py_positional("qualifiedName"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespaceURI = a[1];
                        py qualifiedName = a[2];
                        py value = a[3];
                        py attr;
                        py localname;
                        py prefix;
                        auto const items34 = unpack((G<"xml.dom.minidom._nssplit">())(qualifiedName), 2);
                        prefix = items34[0];
                        localname = items34[1];
                        attr = self.method("getAttributeNodeNS", { namespaceURI, localname });
                        if (attr.is_none())
                        {
                            attr = (G<"xml.dom.minidom.Attr">())(qualifiedName, namespaceURI, localname, prefix);
                            attr.set_attr("value", value);
                            attr.set_attr("ownerDocument", self.attr("ownerDocument"));
                            self.method("setAttributeNode", { attr });
                        }
                        else
                        {
                            if (value != attr.attr("value"))
                            {
                                attr.set_attr("value", value);
                                if (attr.attr("isId").truthy())
                                {
                                    (G<"xml.dom.minidom._clear_id_cache">())(self);
                                }
                            }
                            if (attr.attr("prefix") != prefix)
                            {
                                attr.set_attr("prefix", prefix);
                                attr.set_attr("nodeName", qualifiedName);
                            }
                        }
                        return none();
                    }) },
                { "getAttributeNode", w.method("Element", "getAttributeNode", { py_positional("attrname") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attrname = a[1];
                        if (self.attr("_attrs").is_none())
                        {
                            return none();
                        }
                        return self.attr("_attrs").method("get", { attrname });
                    }) },
                { "getAttributeNodeNS", w.method("Element", "getAttributeNodeNS", { py_positional("namespaceURI"), py_positional("localName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespaceURI = a[1];
                        py localName = a[2];
                        if (self.attr("_attrsNS").is_none())
                        {
                            return none();
                        }
                        return self.attr("_attrsNS").method("get", { py_tuple({ namespaceURI, localName }) });
                    }) },
                { "setAttributeNode", w.method("Element", "setAttributeNode", { py_positional("attr") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attr = a[1];
                        py old1;
                        py old2;
                        if (!py_tuple({ none(), self }).contains(attr.attr("ownerElement")))
                        {
                            py_raise((Mod<"xml.dom">().attr("InuseAttributeErr"))(S<"attribute node already owned">()));
                        }
                        self.method("_ensure_attributes");
                        old1 = self.attr("_attrs").method("get", { attr.attr("name"), none() });
                        if (!old1.is_none())
                        {
                            self.method("removeAttributeNode", { old1 });
                        }
                        old2 = self.attr("_attrsNS").method("get", { py_tuple({ attr.attr("namespaceURI"), attr.attr("localName") }), none() });
                        if (((!old2.is_none()) && (!old2.is(old1))))
                        {
                            self.method("removeAttributeNode", { old2 });
                        }
                        (G<"xml.dom.minidom._set_attribute_node">())(self, attr);
                        if (!old1.is(attr))
                        {
                            // It might have already been part of this node, in which case
                            // it doesn't represent a change, and should not be returned.
                            return old1;
                        }
                        if (!old2.is(attr))
                        {
                            return old2;
                        }
                        return none();
                    }) },
                { "setAttributeNodeNS", w.method("Element", "setAttributeNode", { py_positional("attr") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attr = a[1];
                        py old1;
                        py old2;
                        if (!py_tuple({ none(), self }).contains(attr.attr("ownerElement")))
                        {
                            py_raise((Mod<"xml.dom">().attr("InuseAttributeErr"))(S<"attribute node already owned">()));
                        }
                        self.method("_ensure_attributes");
                        old1 = self.attr("_attrs").method("get", { attr.attr("name"), none() });
                        if (!old1.is_none())
                        {
                            self.method("removeAttributeNode", { old1 });
                        }
                        old2 = self.attr("_attrsNS").method("get", { py_tuple({ attr.attr("namespaceURI"), attr.attr("localName") }), none() });
                        if (((!old2.is_none()) && (!old2.is(old1))))
                        {
                            self.method("removeAttributeNode", { old2 });
                        }
                        (G<"xml.dom.minidom._set_attribute_node">())(self, attr);
                        if (!old1.is(attr))
                        {
                            // It might have already been part of this node, in which case
                            // it doesn't represent a change, and should not be returned.
                            return old1;
                        }
                        if (!old2.is(attr))
                        {
                            return old2;
                        }
                        return none();
                    }) },
                { "removeAttribute", w.method("Element", "removeAttribute", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py attr;
                        if (self.attr("_attrsNS").is_none())
                        {
                            py_raise((Mod<"xml.dom">().attr("NotFoundErr"))());
                        }
                        try
                        {
                            attr = self.attr("_attrs")[name];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler35;
                            if (matches(handler35.exception, B<"KeyError">()))
                            {
                                py_raise((Mod<"xml.dom">().attr("NotFoundErr"))());
                            }
                            else
                                py_reraise(handler35.exception);
                        }
                        self.method("removeAttributeNode", { attr });
                        return none();
                    }) },
                { "removeAttributeNS", w.method("Element", "removeAttributeNS", { py_positional("namespaceURI"), py_positional("localName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespaceURI = a[1];
                        py localName = a[2];
                        py attr;
                        if (self.attr("_attrsNS").is_none())
                        {
                            py_raise((Mod<"xml.dom">().attr("NotFoundErr"))());
                        }
                        try
                        {
                            attr = self.attr("_attrsNS")[py_tuple({ namespaceURI, localName })];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler36;
                            if (matches(handler36.exception, B<"KeyError">()))
                            {
                                py_raise((Mod<"xml.dom">().attr("NotFoundErr"))());
                            }
                            else
                                py_reraise(handler36.exception);
                        }
                        self.method("removeAttributeNode", { attr });
                        return none();
                    }) },
                { "removeAttributeNode", w.method("Element", "removeAttributeNode", { py_positional("node") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py node = a[1];
                        if (node.is_none())
                        {
                            py_raise((Mod<"xml.dom">().attr("NotFoundErr"))());
                        }
                        try
                        {
                            self.attr("_attrs")[node.attr("name")];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler37;
                            if (matches(handler37.exception, B<"KeyError">()))
                            {
                                py_raise((Mod<"xml.dom">().attr("NotFoundErr"))());
                            }
                            else
                                py_reraise(handler37.exception);
                        }
                        (G<"xml.dom.minidom._clear_id_cache">())(self);
                        node.method("unlink");
                        // Restore this since the node is still useful and otherwise
                        // unlinked
                        node.set_attr("ownerDocument", self.attr("ownerDocument"));
                        return node;
                    }) },
                { "removeAttributeNodeNS", w.method("Element", "removeAttributeNode", { py_positional("node") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py node = a[1];
                        if (node.is_none())
                        {
                            py_raise((Mod<"xml.dom">().attr("NotFoundErr"))());
                        }
                        try
                        {
                            self.attr("_attrs")[node.attr("name")];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler37;
                            if (matches(handler37.exception, B<"KeyError">()))
                            {
                                py_raise((Mod<"xml.dom">().attr("NotFoundErr"))());
                            }
                            else
                                py_reraise(handler37.exception);
                        }
                        (G<"xml.dom.minidom._clear_id_cache">())(self);
                        node.method("unlink");
                        // Restore this since the node is still useful and otherwise
                        // unlinked
                        node.set_attr("ownerDocument", self.attr("ownerDocument"));
                        return node;
                    }) },
                { "hasAttribute", w.method("Element", "hasAttribute", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        if (self.attr("_attrs").is_none())
                        {
                            return boolean(false);
                        }
                        return boolean(self.attr("_attrs").contains(name));
                    }) },
                { "hasAttributeNS", w.method("Element", "hasAttributeNS", { py_positional("namespaceURI"), py_positional("localName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespaceURI = a[1];
                        py localName = a[2];
                        if (self.attr("_attrsNS").is_none())
                        {
                            return boolean(false);
                        }
                        return boolean(self.attr("_attrsNS").contains(py_tuple({ namespaceURI, localName })));
                    }) },
                { "getElementsByTagName", w.method("Element", "getElementsByTagName", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return (G<"xml.dom.minidom._get_elements_by_tagName_helper">())(self, name, (Mod<"xml.dom.minicompat">().attr("NodeList"))());
                    }) },
                { "getElementsByTagNameNS", w.method("Element", "getElementsByTagNameNS", { py_positional("namespaceURI"), py_positional("localName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespaceURI = a[1];
                        py localName = a[2];
                        return (G<"xml.dom.minidom._get_elements_by_tagName_ns_helper">())(self, namespaceURI, localName, (Mod<"xml.dom.minicompat">().attr("NodeList"))());
                    }) },
                { "__repr__", w.method("Element", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"<DOM Element: %s at %#x>">()) % py_tuple({ self.attr("tagName"), (B<"id">())(self) });
                    }) },
                { "writexml", w.method("Element", "writexml", { py_positional("writer"), py_positional("indent", S<"">()), py_positional("addindent", S<"">()), py_positional("newl", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py writer = a[1];
                        py indent = a[2];
                        py addindent = a[3];
                        py newl = a[4];
                        py a_name;
                        py attrs;
                        py node;
                        // indent = current indentation
                        // addindent = indentation to add to higher levels
                        // newl = newline string
                        writer.method("write", { (indent + (S<"<">())) + self.attr("tagName") });
                        attrs = self.method("_get_attributes");
                        for (py const& item : iterate{ attrs.method("keys") })
                        {
                            a_name = item;
                            writer.method("write", { (S<" %s=\"">()) % a_name });
                            (G<"xml.dom.minidom._write_data">())(writer, attrs[a_name].attr("value"), boolean(true));
                            writer.method("write", { S<"\"">() });
                        }
                        if (self.attr("childNodes").truthy())
                        {
                            writer.method("write", { S<">">() });
                            if (((py{ self.attr("childNodes").size() } == (I<1>())) && py_tuple({ (G<"xml.dom.minidom.Node">()).attr("TEXT_NODE"), (G<"xml.dom.minidom.Node">()).attr("CDATA_SECTION_NODE") }).contains(self.attr("childNodes")[I<0>()].attr("nodeType"))))
                            {
                                self.attr("childNodes")[I<0>()].method("writexml", { writer, S<"">(), S<"">(), S<"">() });
                            }
                            else
                            {
                                writer.method("write", { newl });
                                for (py const& item : iterate{ self.attr("childNodes") })
                                {
                                    node = item;
                                    node.method("writexml", { writer, indent + addindent, addindent, newl });
                                }
                                writer.method("write", { indent });
                            }
                            writer.method("write", { (S<"</%s>%s">()) % py_tuple({ self.attr("tagName"), newl }) });
                        }
                        else
                        {
                            writer.method("write", { (S<"/>%s">()) % newl });
                        }
                        return none();
                    }) },
                { "_get_attributes", w.method("Element", "_get_attributes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("_ensure_attributes");
                        return (G<"xml.dom.minidom.NamedNodeMap">())(self.attr("_attrs"), self.attr("_attrsNS"), self);
                    }) },
                { "hasAttributes", w.method("Element", "hasAttributes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("_attrs").truthy())
                        {
                            return boolean(true);
                        }
                        else
                        {
                            return boolean(false);
                        }
                        return none();
                    }) },
                { "setIdAttribute", w.method("Element", "setIdAttribute", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py idAttr;
                        idAttr = self.method("getAttributeNode", { name });
                        self.method("setIdAttributeNode", { idAttr });
                        return none();
                    }) },
                { "setIdAttributeNS", w.method("Element", "setIdAttributeNS", { py_positional("namespaceURI"), py_positional("localName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespaceURI = a[1];
                        py localName = a[2];
                        py idAttr;
                        idAttr = self.method("getAttributeNodeNS", { namespaceURI, localName });
                        self.method("setIdAttributeNode", { idAttr });
                        return none();
                    }) },
                { "setIdAttributeNode", w.method("Element", "setIdAttributeNode", { py_positional("idAttr") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py idAttr = a[1];
                        if ((idAttr.is_none() || (!self.method("isSameNode", { idAttr.attr("ownerElement") }).truthy())))
                        {
                            py_raise((Mod<"xml.dom">().attr("NotFoundErr"))());
                        }
                        if (!(G<"xml.dom.minidom._get_containing_entref">())(self).is_none())
                        {
                            py_raise((Mod<"xml.dom">().attr("NoModificationAllowedErr"))());
                        }
                        if (!idAttr.attr("_is_id").truthy())
                        {
                            idAttr.set_attr("_is_id", boolean(true));
                            {
                                py const target40 = self;
                                target40.set_attr("_magic_id_nodes", inplace(NEOS_PY_ADD, target40.attr("_magic_id_nodes"), I<1>()));
                            }
                            {
                                py const target41 = self.attr("ownerDocument");
                                target41.set_attr("_magic_id_count", inplace(NEOS_PY_ADD, target41.attr("_magic_id_count"), I<1>()));
                            }
                            (G<"xml.dom.minidom._clear_id_cache">())(self);
                        }
                        return none();
                    }) }, }));

            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (Mod<"xml.dom.minicompat">().attr("defproperty")).call({ G<"xml.dom.minidom.Element">(), S<"attributes">() }, { { "doc", S<"NamedNodeMap of attributes on the element.">() } });
                    return none();
                }))();
            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (Mod<"xml.dom.minicompat">().attr("defproperty")).call({ G<"xml.dom.minidom.Element">(), S<"localName">() }, { { "doc", S<"Namespace-local name of this element.">() } });
                    return none();
                }))();
            define("xml.dom.minidom", "_set_attribute_node", w.function("_set_attribute_node", "_set_attribute_node", { py_positional("element"), py_positional("attr") }, [](arguments a) -> py
                {
                    py element = a[0];
                    py attr = a[1];
                    (G<"xml.dom.minidom._clear_id_cache">())(element);
                    element.method("_ensure_attributes");
                    element.attr("_attrs").set_item(attr.attr("name"), attr);
                    element.attr("_attrsNS").set_item(py_tuple({ attr.attr("namespaceURI"), attr.attr("localName") }), attr);
                    // This creates a circular reference, but Element.unlink()
                    // breaks the cycle since the references to the attribute
                    // dictionaries are tossed.
                    attr.set_attr("ownerElement", element);
                    return none();
                }));

            define("xml.dom.minidom", "Childless", w.make_class("Childless", {  }, {
                { "__slots__", py_tuple() },
                { "attributes", none() },
                { "childNodes", (Mod<"xml.dom.minicompat">().attr("EmptyNodeList"))() },
                { "firstChild", none() },
                { "lastChild", none() },
                { "_get_firstChild", w.method("Childless", "_get_firstChild", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "_get_lastChild", w.method("Childless", "_get_lastChild", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "appendChild", w.method("Childless", "appendChild", { py_positional("node") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py node = a[1];
                        py_raise((Mod<"xml.dom">().attr("HierarchyRequestErr"))(self.attr("nodeName") + (S<" nodes cannot have children">())));
                    }) },
                { "hasChildNodes", w.method("Childless", "hasChildNodes", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(false);
                    }) },
                { "insertBefore", w.method("Childless", "insertBefore", { py_positional("newChild"), py_positional("refChild") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py newChild = a[1];
                        py refChild = a[2];
                        py_raise((Mod<"xml.dom">().attr("HierarchyRequestErr"))(self.attr("nodeName") + (S<" nodes do not have children">())));
                    }) },
                { "removeChild", w.method("Childless", "removeChild", { py_positional("oldChild") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py oldChild = a[1];
                        py_raise((Mod<"xml.dom">().attr("NotFoundErr"))(self.attr("nodeName") + (S<" nodes do not have children">())));
                    }) },
                { "normalize", w.method("Childless", "normalize", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "replaceChild", w.method("Childless", "replaceChild", { py_positional("newChild"), py_positional("oldChild") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py newChild = a[1];
                        py oldChild = a[2];
                        py_raise((Mod<"xml.dom">().attr("HierarchyRequestErr"))(self.attr("nodeName") + (S<" nodes do not have children">())));
                    }) }, }));

                // nodeValue is an alias for data
                // nodeName is an alias for target
            define("xml.dom.minidom", "ProcessingInstruction", w.make_class("ProcessingInstruction", { G<"xml.dom.minidom.Childless">(), G<"xml.dom.minidom.Node">() }, {
                { "nodeType", (G<"xml.dom.minidom.Node">()).attr("PROCESSING_INSTRUCTION_NODE") },
                { "__slots__", py_tuple({ S<"target">(), S<"data">() }) },
                { "__init__", w.method("ProcessingInstruction", "__init__", { py_positional("target"), py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py target = a[1];
                        py data = a[2];
                        self.set_attr("target", target);
                        self.set_attr("data", data);
                        return none();
                    }) },
                { "_get_nodeValue", w.method("ProcessingInstruction", "_get_nodeValue", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("data");
                    }) },
                { "_set_nodeValue", w.method("ProcessingInstruction", "_set_nodeValue", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        self.set_attr("data", value);
                        return none();
                    }) },
                { "nodeValue", (B<"property">())(w.method("ProcessingInstruction", "_get_nodeValue", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("data");
                    }), w.method("ProcessingInstruction", "_set_nodeValue", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        self.set_attr("data", value);
                        return none();
                    })) },
                { "_get_nodeName", w.method("ProcessingInstruction", "_get_nodeName", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("target");
                    }) },
                { "_set_nodeName", w.method("ProcessingInstruction", "_set_nodeName", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        self.set_attr("target", value);
                        return none();
                    }) },
                { "nodeName", (B<"property">())(w.method("ProcessingInstruction", "_get_nodeName", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("target");
                    }), w.method("ProcessingInstruction", "_set_nodeName", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        self.set_attr("target", value);
                        return none();
                    })) },
                { "writexml", w.method("ProcessingInstruction", "writexml", { py_positional("writer"), py_positional("indent", S<"">()), py_positional("addindent", S<"">()), py_positional("newl", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py writer = a[1];
                        py indent = a[2];
                        py addindent = a[3];
                        py newl = a[4];
                        writer.method("write", { (S<"%s<?%s %s?>%s">()) % py_tuple({ indent, self.attr("target"), self.attr("data"), newl }) });
                        return none();
                    }) }, }));

            define("xml.dom.minidom", "CharacterData", w.make_class("CharacterData", { G<"xml.dom.minidom.Childless">(), G<"xml.dom.minidom.Node">() }, {
                { "__slots__", py_tuple({ S<"_data">(), S<"ownerDocument">(), S<"parentNode">(), S<"previousSibling">(), S<"nextSibling">() }) },
                { "__init__", w.method("CharacterData", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py const value42 = none();
                        self.set_attr("ownerDocument", value42);
                        self.set_attr("parentNode", value42);
                        py const value43 = none();
                        self.set_attr("previousSibling", value43);
                        self.set_attr("nextSibling", value43);
                        self.set_attr("_data", S<"">());
                        (G<"xml.dom.minidom.Node">()).method("__init__", { self });
                        return none();
                    }) },
                { "_get_length", w.method("CharacterData", "_get_length", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py{ self.attr("data").size() };
                    }) },
                { "__len__", w.method("CharacterData", "_get_length", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py{ self.attr("data").size() };
                    }) },
                { "_get_data", w.method("CharacterData", "_get_data", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_data");
                    }) },
                { "_set_data", w.method("CharacterData", "_set_data", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.set_attr("_data", data);
                        return none();
                    }) },
                { "data", (B<"property">())(w.method("CharacterData", "_get_data", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_data");
                    }), w.method("CharacterData", "_set_data", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.set_attr("_data", data);
                        return none();
                    })) },
                { "nodeValue", (B<"property">())(w.method("CharacterData", "_get_data", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_data");
                    }), w.method("CharacterData", "_set_data", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.set_attr("_data", data);
                        return none();
                    })) },
                { "__repr__", w.method("CharacterData", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data;
                        py dotdotdot;
                        data = self.attr("data");
                        if (py{ data.size() } > (I<10>()))
                        {
                            dotdotdot = S<"...">();
                        }
                        else
                        {
                            dotdotdot = S<"">();
                        }
                        return (S<"<DOM %s node \"%r%s\">">()) % py_tuple({ self.attr("__class__").attr("__name__"), data[B<"slice">()(I<0>(), I<10>(), none())], dotdotdot });
                    }) },
                { "substringData", w.method("CharacterData", "substringData", { py_positional("offset"), py_positional("count") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py offset = a[1];
                        py count = a[2];
                        if (offset < (I<0>()))
                        {
                            py_raise((Mod<"xml.dom">().attr("IndexSizeErr"))(S<"offset cannot be negative">()));
                        }
                        if (offset >= py{ self.attr("data").size() })
                        {
                            py_raise((Mod<"xml.dom">().attr("IndexSizeErr"))(S<"offset cannot be beyond end of data">()));
                        }
                        if (count < (I<0>()))
                        {
                            py_raise((Mod<"xml.dom">().attr("IndexSizeErr"))(S<"count cannot be negative">()));
                        }
                        return self.attr("data")[B<"slice">()(offset, offset + count, none())];
                    }) },
                { "appendData", w.method("CharacterData", "appendData", { py_positional("arg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py arg = a[1];
                        self.set_attr("data", self.attr("data") + arg);
                        return none();
                    }) },
                { "insertData", w.method("CharacterData", "insertData", { py_positional("offset"), py_positional("arg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py offset = a[1];
                        py arg = a[2];
                        if (offset < (I<0>()))
                        {
                            py_raise((Mod<"xml.dom">().attr("IndexSizeErr"))(S<"offset cannot be negative">()));
                        }
                        if (offset >= py{ self.attr("data").size() })
                        {
                            py_raise((Mod<"xml.dom">().attr("IndexSizeErr"))(S<"offset cannot be beyond end of data">()));
                        }
                        if (arg.truthy())
                        {
                            self.set_attr("data", (S<"%s%s%s">()) % py_tuple({ self.attr("data")[B<"slice">()(none(), offset, none())], arg, self.attr("data")[B<"slice">()(offset, none(), none())] }));
                        }
                        return none();
                    }) },
                { "deleteData", w.method("CharacterData", "deleteData", { py_positional("offset"), py_positional("count") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py offset = a[1];
                        py count = a[2];
                        if (offset < (I<0>()))
                        {
                            py_raise((Mod<"xml.dom">().attr("IndexSizeErr"))(S<"offset cannot be negative">()));
                        }
                        if (offset >= py{ self.attr("data").size() })
                        {
                            py_raise((Mod<"xml.dom">().attr("IndexSizeErr"))(S<"offset cannot be beyond end of data">()));
                        }
                        if (count < (I<0>()))
                        {
                            py_raise((Mod<"xml.dom">().attr("IndexSizeErr"))(S<"count cannot be negative">()));
                        }
                        if (count.truthy())
                        {
                            self.set_attr("data", self.attr("data")[B<"slice">()(none(), offset, none())] + self.attr("data")[B<"slice">()(offset + count, none(), none())]);
                        }
                        return none();
                    }) },
                { "replaceData", w.method("CharacterData", "replaceData", { py_positional("offset"), py_positional("count"), py_positional("arg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py offset = a[1];
                        py count = a[2];
                        py arg = a[3];
                        if (offset < (I<0>()))
                        {
                            py_raise((Mod<"xml.dom">().attr("IndexSizeErr"))(S<"offset cannot be negative">()));
                        }
                        if (offset >= py{ self.attr("data").size() })
                        {
                            py_raise((Mod<"xml.dom">().attr("IndexSizeErr"))(S<"offset cannot be beyond end of data">()));
                        }
                        if (count < (I<0>()))
                        {
                            py_raise((Mod<"xml.dom">().attr("IndexSizeErr"))(S<"count cannot be negative">()));
                        }
                        if (count.truthy())
                        {
                            self.set_attr("data", (S<"%s%s%s">()) % py_tuple({ self.attr("data")[B<"slice">()(none(), offset, none())], arg, self.attr("data")[B<"slice">()(offset + count, none(), none())] }));
                        }
                        return none();
                    }) }, }));

            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (Mod<"xml.dom.minicompat">().attr("defproperty")).call({ G<"xml.dom.minidom.CharacterData">(), S<"length">() }, { { "doc", S<"Length of the string data.">() } });
                    return none();
                }))();
                // DOM Level 3 (WD 9 April 2002)
            define("xml.dom.minidom", "Text", w.make_class("Text", { G<"xml.dom.minidom.CharacterData">() }, {
                { "__slots__", py_tuple() },
                { "nodeType", (G<"xml.dom.minidom.Node">()).attr("TEXT_NODE") },
                { "nodeName", S<"#text">() },
                { "attributes", none() },
                { "splitText", w.method("Text", "splitText", { py_positional("offset") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py offset = a[1];
                        py newText;
                        py next;
                        if (((offset < (I<0>())) || (offset > py{ self.attr("data").size() })))
                        {
                            py_raise((Mod<"xml.dom">().attr("IndexSizeErr"))(S<"illegal offset value">()));
                        }
                        newText = self.method("__class__");
                        newText.set_attr("data", self.attr("data")[B<"slice">()(offset, none(), none())]);
                        newText.set_attr("ownerDocument", self.attr("ownerDocument"));
                        next = self.attr("nextSibling");
                        if ((self.attr("parentNode").truthy() && self.attr("parentNode").attr("childNodes").contains(self)))
                        {
                            if (next.is_none())
                            {
                                self.attr("parentNode").method("appendChild", { newText });
                            }
                            else
                            {
                                self.attr("parentNode").method("insertBefore", { newText, next });
                            }
                        }
                        self.set_attr("data", self.attr("data")[B<"slice">()(none(), offset, none())]);
                        return newText;
                    }) },
                { "writexml", w.method("Text", "writexml", { py_positional("writer"), py_positional("indent", S<"">()), py_positional("addindent", S<"">()), py_positional("newl", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py writer = a[1];
                        py indent = a[2];
                        py addindent = a[3];
                        py newl = a[4];
                        (G<"xml.dom.minidom._write_data">())(writer, (S<"%s%s%s">()) % py_tuple({ indent, self.attr("data"), newl }), boolean(false));
                        return none();
                    }) },
                { "_get_wholeText", w.method("Text", "_get_wholeText", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py L;
                        py n;
                        L = py_list({ self.attr("data") });
                        n = self.attr("previousSibling");
                        while (!n.is_none())
                        {
                            if (py_tuple({ (G<"xml.dom.minidom.Node">()).attr("TEXT_NODE"), (G<"xml.dom.minidom.Node">()).attr("CDATA_SECTION_NODE") }).contains(n.attr("nodeType")))
                            {
                                L.method("insert", { I<0>(), n.attr("data") });
                                n = n.attr("previousSibling");
                            }
                            else
                            {
                                break;
                            }
                        }
                        n = self.attr("nextSibling");
                        while (!n.is_none())
                        {
                            if (py_tuple({ (G<"xml.dom.minidom.Node">()).attr("TEXT_NODE"), (G<"xml.dom.minidom.Node">()).attr("CDATA_SECTION_NODE") }).contains(n.attr("nodeType")))
                            {
                                L.method("append", { n.attr("data") });
                                n = n.attr("nextSibling");
                            }
                            else
                            {
                                break;
                            }
                        }
                        return (S<"">()).method("join", { L });
                    }) },
                { "replaceWholeText", w.method("Text", "replaceWholeText", { py_positional("content") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py content = a[1];
                        py n;
                        py next;
                        py parent;
                        parent = self.attr("parentNode");
                        n = self.attr("previousSibling");
                        while (!n.is_none())
                        {
                            if (py_tuple({ (G<"xml.dom.minidom.Node">()).attr("TEXT_NODE"), (G<"xml.dom.minidom.Node">()).attr("CDATA_SECTION_NODE") }).contains(n.attr("nodeType")))
                            {
                                next = n.attr("previousSibling");
                                parent.method("removeChild", { n });
                                n = next;
                            }
                            else
                            {
                                break;
                            }
                        }
                        n = self.attr("nextSibling");
                        if (!content.truthy())
                        {
                            parent.method("removeChild", { self });
                        }
                        while (!n.is_none())
                        {
                            if (py_tuple({ (G<"xml.dom.minidom.Node">()).attr("TEXT_NODE"), (G<"xml.dom.minidom.Node">()).attr("CDATA_SECTION_NODE") }).contains(n.attr("nodeType")))
                            {
                                next = n.attr("nextSibling");
                                parent.method("removeChild", { n });
                                n = next;
                            }
                            else
                            {
                                break;
                            }
                        }
                        if (content.truthy())
                        {
                            self.set_attr("data", content);
                            return self;
                        }
                        else
                        {
                            return none();
                        }
                        return none();
                    }) },
                { "_get_isWhitespaceInElementContent", w.method("Text", "_get_isWhitespaceInElementContent", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py elem;
                        py info;
                        if (self.attr("data").method("strip", { G<"xml.dom.minidom._XML_WHITESPACE">() }).truthy())
                        {
                            return boolean(false);
                        }
                        elem = (G<"xml.dom.minidom._get_containing_element">())(self);
                        if (elem.is_none())
                        {
                            return boolean(false);
                        }
                        info = self.attr("ownerDocument").method("_get_elem_info", { elem });
                        if (info.is_none())
                        {
                            return boolean(false);
                        }
                        else
                        {
                            return info.method("isElementContent");
                        }
                        return none();
                    }) }, }));

            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (Mod<"xml.dom.minicompat">().attr("defproperty")).call({ G<"xml.dom.minidom.Text">(), S<"isWhitespaceInElementContent">() }, { { "doc", S<"True iff this text node contains only whitespace and is in element content.">() } });
                    return none();
                }))();
            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (Mod<"xml.dom.minicompat">().attr("defproperty")).call({ G<"xml.dom.minidom.Text">(), S<"wholeText">() }, { { "doc", S<"The text of all logically-adjacent text nodes.">() } });
                    return none();
                }))();
            define("xml.dom.minidom", "_get_containing_element", w.function("_get_containing_element", "_get_containing_element", { py_positional("node") }, [](arguments a) -> py
                {
                    py node = a[0];
                    py c;
                    c = node.attr("parentNode");
                    while (!c.is_none())
                    {
                        if (c.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("ELEMENT_NODE"))
                        {
                            return c;
                        }
                        c = c.attr("parentNode");
                    }
                    return none();
                }));

            define("xml.dom.minidom", "_get_containing_entref", w.function("_get_containing_entref", "_get_containing_entref", { py_positional("node") }, [](arguments a) -> py
                {
                    py node = a[0];
                    py c;
                    c = node.attr("parentNode");
                    while (!c.is_none())
                    {
                        if (c.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("ENTITY_REFERENCE_NODE"))
                        {
                            return c;
                        }
                        c = c.attr("parentNode");
                    }
                    return none();
                }));

            define("xml.dom.minidom", "Comment", w.make_class("Comment", { G<"xml.dom.minidom.CharacterData">() }, {
                { "nodeType", (G<"xml.dom.minidom.Node">()).attr("COMMENT_NODE") },
                { "nodeName", S<"#comment">() },
                { "__init__", w.method("Comment", "__init__", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        (G<"xml.dom.minidom.CharacterData">()).method("__init__", { self });
                        self.set_attr("_data", data);
                        return none();
                    }) },
                { "writexml", w.method("Comment", "writexml", { py_positional("writer"), py_positional("indent", S<"">()), py_positional("addindent", S<"">()), py_positional("newl", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py writer = a[1];
                        py indent = a[2];
                        py addindent = a[3];
                        py newl = a[4];
                        if (self.attr("data").contains((S<"--">())))
                        {
                            py_raise((B<"ValueError">())(S<"'--' is not allowed in a comment node">()));
                        }
                        writer.method("write", { (S<"%s<!--%s-->%s">()) % py_tuple({ indent, self.attr("data"), newl }) });
                        return none();
                    }) }, }));

            define("xml.dom.minidom", "CDATASection", w.make_class("CDATASection", { G<"xml.dom.minidom.Text">() }, {
                { "__slots__", py_tuple() },
                { "nodeType", (G<"xml.dom.minidom.Node">()).attr("CDATA_SECTION_NODE") },
                { "nodeName", S<"#cdata-section">() },
                { "writexml", w.method("CDATASection", "writexml", { py_positional("writer"), py_positional("indent", S<"">()), py_positional("addindent", S<"">()), py_positional("newl", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py writer = a[1];
                        py indent = a[2];
                        py addindent = a[3];
                        py newl = a[4];
                        if (self.attr("data").method("find", { S<"]]>">() }) >= (I<0>()))
                        {
                            py_raise((B<"ValueError">())(S<"']]>' not allowed in a CDATA section">()));
                        }
                        writer.method("write", { (S<"<![CDATA[%s]]>">()) % self.attr("data") });
                        return none();
                    }) }, }));

            define("xml.dom.minidom", "ReadOnlySequentialNamedNodeMap", w.make_class("ReadOnlySequentialNamedNodeMap", { B<"object">() }, {
                { "__slots__", py_tuple({ S<"_seq">() }) },
                { "__init__", w.method("ReadOnlySequentialNamedNodeMap", "__init__", { py_positional("seq", py_tuple()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py seq = a[1];
                        self.set_attr("_seq", seq);
                        return none();
                    }) },
                { "__len__", w.method("ReadOnlySequentialNamedNodeMap", "__len__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py{ self.attr("_seq").size() };
                    }) },
                { "_get_length", w.method("ReadOnlySequentialNamedNodeMap", "_get_length", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py{ self.attr("_seq").size() };
                    }) },
                { "getNamedItem", w.method("ReadOnlySequentialNamedNodeMap", "getNamedItem", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py n;
                        for (py const& item : iterate{ self.attr("_seq") })
                        {
                            n = item;
                            if (n.attr("nodeName") == name)
                            {
                                return n;
                            }
                        }
                        return none();
                    }) },
                { "getNamedItemNS", w.method("ReadOnlySequentialNamedNodeMap", "getNamedItemNS", { py_positional("namespaceURI"), py_positional("localName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespaceURI = a[1];
                        py localName = a[2];
                        py n;
                        for (py const& item : iterate{ self.attr("_seq") })
                        {
                            n = item;
                            if (((n.attr("namespaceURI") == namespaceURI) && (n.attr("localName") == localName)))
                            {
                                return n;
                            }
                        }
                        return none();
                    }) },
                { "__getitem__", w.method("ReadOnlySequentialNamedNodeMap", "__getitem__", { py_positional("name_or_tuple") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name_or_tuple = a[1];
                        py node;
                        if (isinstance(name_or_tuple, B<"tuple">()))
                        {
                            node = call_ex(self.attr("getNamedItemNS"), {  }, name_or_tuple, py{});
                        }
                        else
                        {
                            node = self.method("getNamedItem", { name_or_tuple });
                        }
                        if (node.is_none())
                        {
                            py_raise((B<"KeyError">())(name_or_tuple));
                        }
                        return node;
                    }) },
                { "item", w.method("ReadOnlySequentialNamedNodeMap", "item", { py_positional("index") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py index = a[1];
                        if (index < (I<0>()))
                        {
                            return none();
                        }
                        try
                        {
                            return self.attr("_seq")[index];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler52;
                            if (matches(handler52.exception, B<"IndexError">()))
                            {
                                return none();
                            }
                            else
                                py_reraise(handler52.exception);
                        }
                        return none();
                    }) },
                { "removeNamedItem", w.method("ReadOnlySequentialNamedNodeMap", "removeNamedItem", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py_raise((Mod<"xml.dom">().attr("NoModificationAllowedErr"))(S<"NamedNodeMap instance is read-only">()));
                    }) },
                { "removeNamedItemNS", w.method("ReadOnlySequentialNamedNodeMap", "removeNamedItemNS", { py_positional("namespaceURI"), py_positional("localName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespaceURI = a[1];
                        py localName = a[2];
                        py_raise((Mod<"xml.dom">().attr("NoModificationAllowedErr"))(S<"NamedNodeMap instance is read-only">()));
                    }) },
                { "setNamedItem", w.method("ReadOnlySequentialNamedNodeMap", "setNamedItem", { py_positional("node") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py node = a[1];
                        py_raise((Mod<"xml.dom">().attr("NoModificationAllowedErr"))(S<"NamedNodeMap instance is read-only">()));
                    }) },
                { "setNamedItemNS", w.method("ReadOnlySequentialNamedNodeMap", "setNamedItemNS", { py_positional("node") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py node = a[1];
                        py_raise((Mod<"xml.dom">().attr("NoModificationAllowedErr"))(S<"NamedNodeMap instance is read-only">()));
                    }) },
                { "__getstate__", w.method("ReadOnlySequentialNamedNodeMap", "__getstate__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py_list({ self.attr("_seq") });
                    }) },
                { "__setstate__", w.method("ReadOnlySequentialNamedNodeMap", "__setstate__", { py_positional("state") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py state = a[1];
                        self.set_attr("_seq", state[I<0>()]);
                        return none();
                    }) }, }));

            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (Mod<"xml.dom.minicompat">().attr("defproperty")).call({ G<"xml.dom.minidom.ReadOnlySequentialNamedNodeMap">(), S<"length">() }, { { "doc", S<"Number of entries in the NamedNodeMap.">() } });
                    return none();
                }))();
            define("xml.dom.minidom", "Identified", w.make_class("Identified", {  }, {
                { "__slots__", py_tuple({ S<"publicId">(), S<"systemId">() }) },
                { "_identified_mixin_init", w.method("Identified", "_identified_mixin_init", { py_positional("publicId"), py_positional("systemId") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py publicId = a[1];
                        py systemId = a[2];
                        self.set_attr("publicId", publicId);
                        self.set_attr("systemId", systemId);
                        return none();
                    }) },
                { "_get_publicId", w.method("Identified", "_get_publicId", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("publicId");
                    }) },
                { "_get_systemId", w.method("Identified", "_get_systemId", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("systemId");
                    }) }, }));

            define("xml.dom.minidom", "DocumentType", w.make_class("DocumentType", { G<"xml.dom.minidom.Identified">(), G<"xml.dom.minidom.Childless">(), G<"xml.dom.minidom.Node">() }, {
                { "nodeType", (G<"xml.dom.minidom.Node">()).attr("DOCUMENT_TYPE_NODE") },
                { "nodeValue", none() },
                { "name", none() },
                { "publicId", none() },
                { "systemId", none() },
                { "internalSubset", none() },
                { "__init__", w.method("DocumentType", "__init__", { py_positional("qualifiedName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py qualifiedName = a[1];
                        py localname;
                        py prefix;
                        self.set_attr("entities", (G<"xml.dom.minidom.ReadOnlySequentialNamedNodeMap">())());
                        self.set_attr("notations", (G<"xml.dom.minidom.ReadOnlySequentialNamedNodeMap">())());
                        if (qualifiedName.truthy())
                        {
                            auto const items53 = unpack((G<"xml.dom.minidom._nssplit">())(qualifiedName), 2);
                            prefix = items53[0];
                            localname = items53[1];
                            self.set_attr("name", localname);
                        }
                        self.set_attr("nodeName", self.attr("name"));
                        return none();
                    }) },
                { "_get_internalSubset", w.method("DocumentType", "_get_internalSubset", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("internalSubset");
                    }) },
                { "cloneNode", w.method("DocumentType", "cloneNode", { py_positional("deep") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py deep = a[1];
                        py clone;
                        py e;
                        py entity;
                        py n;
                        py notation;
                        py operation;
                        if (self.attr("ownerDocument").is_none())
                        {
                            // it's ok
                            clone = (G<"xml.dom.minidom.DocumentType">())(none());
                            clone.set_attr("name", self.attr("name"));
                            clone.set_attr("nodeName", self.attr("name"));
                            operation = (Mod<"xml.dom">().attr("UserDataHandler")).attr("NODE_CLONED");
                            if (deep.truthy())
                            {
                                clone.attr("entities").set_attr("_seq", py_list());
                                clone.attr("notations").set_attr("_seq", py_list());
                                for (py const& item : iterate{ self.attr("notations").attr("_seq") })
                                {
                                    n = item;
                                    notation = (G<"xml.dom.minidom.Notation">())(n.attr("nodeName"), n.attr("publicId"), n.attr("systemId"));
                                    clone.attr("notations").attr("_seq").method("append", { notation });
                                    n.method("_call_user_data_handler", { operation, n, notation });
                                }
                                for (py const& item : iterate{ self.attr("entities").attr("_seq") })
                                {
                                    e = item;
                                    entity = (G<"xml.dom.minidom.Entity">())(e.attr("nodeName"), e.attr("publicId"), e.attr("systemId"), e.attr("notationName"));
                                    entity.set_attr("actualEncoding", e.attr("actualEncoding"));
                                    entity.set_attr("encoding", e.attr("encoding"));
                                    entity.set_attr("version", e.attr("version"));
                                    clone.attr("entities").attr("_seq").method("append", { entity });
                                    e.method("_call_user_data_handler", { operation, e, entity });
                                }
                            }
                            self.method("_call_user_data_handler", { operation, self, clone });
                            return clone;
                        }
                        else
                        {
                            return none();
                        }
                        return none();
                    }) },
                { "writexml", w.method("DocumentType", "writexml", { py_positional("writer"), py_positional("indent", S<"">()), py_positional("addindent", S<"">()), py_positional("newl", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py writer = a[1];
                        py indent = a[2];
                        py addindent = a[3];
                        py newl = a[4];
                        writer.method("write", { S<"<!DOCTYPE ">() });
                        writer.method("write", { self.attr("name") });
                        if (self.attr("publicId").truthy())
                        {
                            writer.method("write", { (S<"%s  PUBLIC '%s'%s  '%s'">()) % py_tuple({ newl, self.attr("publicId"), newl, self.attr("systemId") }) });
                        }
                        else if (self.attr("systemId").truthy())
                        {
                            writer.method("write", { (S<"%s  SYSTEM '%s'">()) % py_tuple({ newl, self.attr("systemId") }) });
                        }
                        if (!self.attr("internalSubset").is_none())
                        {
                            writer.method("write", { S<" [">() });
                            writer.method("write", { self.attr("internalSubset") });
                            writer.method("write", { S<"]">() });
                        }
                        writer.method("write", { (S<">">()) + newl });
                        return none();
                    }) }, }));

            define("xml.dom.minidom", "Entity", w.make_class("Entity", { G<"xml.dom.minidom.Identified">(), G<"xml.dom.minidom.Node">() }, {
                { "attributes", none() },
                { "nodeType", (G<"xml.dom.minidom.Node">()).attr("ENTITY_NODE") },
                { "nodeValue", none() },
                { "actualEncoding", none() },
                { "encoding", none() },
                { "version", none() },
                { "__init__", w.method("Entity", "__init__", { py_positional("name"), py_positional("publicId"), py_positional("systemId"), py_positional("notation") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py publicId = a[2];
                        py systemId = a[3];
                        py notation = a[4];
                        self.set_attr("nodeName", name);
                        self.set_attr("notationName", notation);
                        self.set_attr("childNodes", (Mod<"xml.dom.minicompat">().attr("NodeList"))());
                        self.method("_identified_mixin_init", { publicId, systemId });
                        return none();
                    }) },
                { "_get_actualEncoding", w.method("Entity", "_get_actualEncoding", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("actualEncoding");
                    }) },
                { "_get_encoding", w.method("Entity", "_get_encoding", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("encoding");
                    }) },
                { "_get_version", w.method("Entity", "_get_version", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("version");
                    }) },
                { "appendChild", w.method("Entity", "appendChild", { py_positional("newChild") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py newChild = a[1];
                        py_raise((Mod<"xml.dom">().attr("HierarchyRequestErr"))(S<"cannot append children to an entity node">()));
                    }) },
                { "insertBefore", w.method("Entity", "insertBefore", { py_positional("newChild"), py_positional("refChild") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py newChild = a[1];
                        py refChild = a[2];
                        py_raise((Mod<"xml.dom">().attr("HierarchyRequestErr"))(S<"cannot insert children below an entity node">()));
                    }) },
                { "removeChild", w.method("Entity", "removeChild", { py_positional("oldChild") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py oldChild = a[1];
                        py_raise((Mod<"xml.dom">().attr("HierarchyRequestErr"))(S<"cannot remove children from an entity node">()));
                    }) },
                { "replaceChild", w.method("Entity", "replaceChild", { py_positional("newChild"), py_positional("oldChild") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py newChild = a[1];
                        py oldChild = a[2];
                        py_raise((Mod<"xml.dom">().attr("HierarchyRequestErr"))(S<"cannot replace children of an entity node">()));
                    }) }, }));

            define("xml.dom.minidom", "Notation", w.make_class("Notation", { G<"xml.dom.minidom.Identified">(), G<"xml.dom.minidom.Childless">(), G<"xml.dom.minidom.Node">() }, {
                { "nodeType", (G<"xml.dom.minidom.Node">()).attr("NOTATION_NODE") },
                { "nodeValue", none() },
                { "__init__", w.method("Notation", "__init__", { py_positional("name"), py_positional("publicId"), py_positional("systemId") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py publicId = a[2];
                        py systemId = a[3];
                        self.set_attr("nodeName", name);
                        self.method("_identified_mixin_init", { publicId, systemId });
                        return none();
                    }) }, }));

                // DOM Level 3 (WD 9 April 2002)
                // internal
            define("xml.dom.minidom", "DOMImplementation", w.make_class("DOMImplementation", { Mod<"xml.dom.xmlbuilder">().attr("DOMImplementationLS") }, {
                { "_features", py_list({ py_tuple({ S<"core">(), S<"1.0">() }), py_tuple({ S<"core">(), S<"2.0">() }), py_tuple({ S<"core">(), none() }), py_tuple({ S<"xml">(), S<"1.0">() }), py_tuple({ S<"xml">(), S<"2.0">() }), py_tuple({ S<"xml">(), none() }), py_tuple({ S<"ls-load">(), S<"3.0">() }), py_tuple({ S<"ls-load">(), none() }) }) },
                { "hasFeature", w.method("DOMImplementation", "hasFeature", { py_positional("feature"), py_positional("version") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py feature = a[1];
                        py version = a[2];
                        if (version == (S<"">()))
                        {
                            version = none();
                        }
                        return boolean(self.attr("_features").contains(py_tuple({ feature.method("lower"), version })));
                    }) },
                { "createDocument", w.method("DOMImplementation", "createDocument", { py_positional("namespaceURI"), py_positional("qualifiedName"), py_positional("doctype") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespaceURI = a[1];
                        py qualifiedName = a[2];
                        py doctype = a[3];
                        py add_root_element;
                        py doc;
                        py element;
                        py localname;
                        py prefix;
                        if ((doctype.truthy() && (!doctype.attr("parentNode").is_none())))
                        {
                            py_raise((Mod<"xml.dom">().attr("WrongDocumentErr"))(S<"doctype object owned by another DOM tree">()));
                        }
                        doc = self.method("_create_document");
                        add_root_element = boolean(!([&]() -> py { py const t = boolean(namespaceURI.is_none()); return !t.truthy() ? t : [&]() -> py { py const t = boolean(qualifiedName.is_none()); return !t.truthy() ? t : boolean(doctype.is_none()); }(); }()).truthy());
                        if (((!qualifiedName.truthy()) && add_root_element.truthy()))
                        {
                            // The spec is unclear what to raise here; SyntaxErr
                            // would be the other obvious candidate. Since Xerces raises
                            // InvalidCharacterErr, and since SyntaxErr is not listed
                            // for createDocument, that seems to be the better choice.
                            // XXX: need to check for illegal characters here and in
                            // createElement.
                            // DOM Level III clears this up when talking about the return value
                            // of this function.  If namespaceURI, qName and DocType are
                            // Null the document is returned without a document element
                            // Otherwise if doctype or namespaceURI are not None
                            // Then we go back to the above problem
                            py_raise((Mod<"xml.dom">().attr("InvalidCharacterErr"))(S<"Element with no name">()));
                        }
                        if (add_root_element.truthy())
                        {
                            auto const items56 = unpack((G<"xml.dom.minidom._nssplit">())(qualifiedName), 2);
                            prefix = items56[0];
                            localname = items56[1];
                            if (((prefix == (S<"xml">())) && (namespaceURI != (S<"http://www.w3.org/XML/1998/namespace">()))))
                            {
                                py_raise((Mod<"xml.dom">().attr("NamespaceErr"))(S<"illegal use of 'xml' prefix">()));
                            }
                            if ((prefix.truthy() && (!namespaceURI.truthy())))
                            {
                                py_raise((Mod<"xml.dom">().attr("NamespaceErr"))(S<"illegal use of prefix without namespaces">()));
                            }
                            element = doc.method("createElementNS", { namespaceURI, qualifiedName });
                            if (doctype.truthy())
                            {
                                doc.method("appendChild", { doctype });
                            }
                            doc.method("appendChild", { element });
                        }
                        if (doctype.truthy())
                        {
                            py const value57 = doc;
                            doctype.set_attr("parentNode", value57);
                            doctype.set_attr("ownerDocument", value57);
                        }
                        doc.set_attr("doctype", doctype);
                        doc.set_attr("implementation", self);
                        return doc;
                    }) },
                { "createDocumentType", w.method("DOMImplementation", "createDocumentType", { py_positional("qualifiedName"), py_positional("publicId"), py_positional("systemId") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py qualifiedName = a[1];
                        py publicId = a[2];
                        py systemId = a[3];
                        py doctype;
                        doctype = (G<"xml.dom.minidom.DocumentType">())(qualifiedName);
                        doctype.set_attr("publicId", publicId);
                        doctype.set_attr("systemId", systemId);
                        return doctype;
                    }) },
                { "getInterface", w.method("DOMImplementation", "getInterface", { py_positional("feature") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py feature = a[1];
                        if (self.method("hasFeature", { feature, none() }).truthy())
                        {
                            return self;
                        }
                        else
                        {
                            return none();
                        }
                        return none();
                    }) },
                { "_create_document", w.method("DOMImplementation", "_create_document", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (G<"xml.dom.minidom.Document">())();
                    }) }, }));

            define("xml.dom.minidom", "ElementInfo", w.make_class("ElementInfo", { B<"object">() }, {
                { "__slots__", py_tuple({ S<"tagName">() }) },
                { "__init__", w.method("ElementInfo", "__init__", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        self.set_attr("tagName", name);
                        return none();
                    }) },
                { "getAttributeType", w.method("ElementInfo", "getAttributeType", { py_positional("aname") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py aname = a[1];
                        return G<"xml.dom.minidom._no_type">();
                    }) },
                { "getAttributeTypeNS", w.method("ElementInfo", "getAttributeTypeNS", { py_positional("namespaceURI"), py_positional("localName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespaceURI = a[1];
                        py localName = a[2];
                        return G<"xml.dom.minidom._no_type">();
                    }) },
                { "isElementContent", w.method("ElementInfo", "isElementContent", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(false);
                    }) },
                { "isEmpty", w.method("ElementInfo", "isEmpty", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(false);
                    }) },
                { "isId", w.method("ElementInfo", "isId", { py_positional("aname") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py aname = a[1];
                        return boolean(false);
                    }) },
                { "isIdNS", w.method("ElementInfo", "isIdNS", { py_positional("namespaceURI"), py_positional("localName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespaceURI = a[1];
                        py localName = a[2];
                        return boolean(false);
                    }) },
                { "__getstate__", w.method("ElementInfo", "__getstate__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("tagName");
                    }) },
                { "__setstate__", w.method("ElementInfo", "__setstate__", { py_positional("state") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py state = a[1];
                        self.set_attr("tagName", state);
                        return none();
                    }) }, }));

            define("xml.dom.minidom", "_clear_id_cache", w.function("_clear_id_cache", "_clear_id_cache", { py_positional("node") }, [](arguments a) -> py
                {
                    py node = a[0];
                    if (node.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("DOCUMENT_NODE"))
                    {
                        node.attr("_id_cache").method("clear");
                        node.set_attr("_id_search_stack", none());
                    }
                    else if (node.attr("ownerDocument").truthy())
                    {
                        node.attr("ownerDocument").attr("_id_cache").method("clear");
                        node.attr("ownerDocument").set_attr("_id_search_stack", none());
                    }
                    return none();
                }));

                // Document attributes from Level 3 (WD 9 April 2002)
                // A couple of implementation-specific helpers to create node types
                // not supported by the W3C DOM specs:
                // DOM Level 3 (WD 9 April 2002)
            define("xml.dom.minidom", "Document", w.make_class("Document", { G<"xml.dom.minidom.Node">(), Mod<"xml.dom.xmlbuilder">().attr("DocumentLS") }, {
                { "__slots__", py_tuple({ S<"_elem_info">(), S<"doctype">(), S<"_id_search_stack">(), S<"childNodes">(), S<"_id_cache">() }) },
                { "_child_node_types", py_tuple({ (G<"xml.dom.minidom.Node">()).attr("ELEMENT_NODE"), (G<"xml.dom.minidom.Node">()).attr("PROCESSING_INSTRUCTION_NODE"), (G<"xml.dom.minidom.Node">()).attr("COMMENT_NODE"), (G<"xml.dom.minidom.Node">()).attr("DOCUMENT_TYPE_NODE") }) },
                { "implementation", (G<"xml.dom.minidom.DOMImplementation">())() },
                { "nodeType", (G<"xml.dom.minidom.Node">()).attr("DOCUMENT_NODE") },
                { "nodeName", S<"#document">() },
                { "nodeValue", none() },
                { "attributes", none() },
                { "parentNode", none() },
                { "previousSibling", none() },
                { "nextSibling", none() },
                { "actualEncoding", none() },
                { "encoding", none() },
                { "standalone", none() },
                { "version", none() },
                { "strictErrorChecking", boolean(false) },
                { "errorHandler", none() },
                { "documentURI", none() },
                { "_magic_id_count", I<0>() },
                { "__init__", w.method("Document", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("doctype", none());
                        self.set_attr("childNodes", (Mod<"xml.dom.minicompat">().attr("NodeList"))());
                        // mapping of (namespaceURI, localName) -> ElementInfo
                        // and tagName -> ElementInfo
                        self.set_attr("_elem_info", py_dict());
                        self.set_attr("_id_cache", py_dict());
                        self.set_attr("_id_search_stack", none());
                        return none();
                    }) },
                { "_get_elem_info", w.method("Document", "_get_elem_info", { py_positional("element") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py element = a[1];
                        py key;
                        if (element.attr("namespaceURI").truthy())
                        {
                            key = py_tuple({ element.attr("namespaceURI"), element.attr("localName") });
                        }
                        else
                        {
                            key = element.attr("tagName");
                        }
                        return self.attr("_elem_info").method("get", { key });
                    }) },
                { "_get_actualEncoding", w.method("Document", "_get_actualEncoding", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("actualEncoding");
                    }) },
                { "_get_doctype", w.method("Document", "_get_doctype", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("doctype");
                    }) },
                { "_get_documentURI", w.method("Document", "_get_documentURI", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("documentURI");
                    }) },
                { "_get_encoding", w.method("Document", "_get_encoding", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("encoding");
                    }) },
                { "_get_errorHandler", w.method("Document", "_get_errorHandler", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("errorHandler");
                    }) },
                { "_get_standalone", w.method("Document", "_get_standalone", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("standalone");
                    }) },
                { "_get_strictErrorChecking", w.method("Document", "_get_strictErrorChecking", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("strictErrorChecking");
                    }) },
                { "_get_version", w.method("Document", "_get_version", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("version");
                    }) },
                { "appendChild", w.method("Document", "appendChild", { py_positional("node") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py node = a[1];
                        if (!self.attr("_child_node_types").contains(node.attr("nodeType")))
                        {
                            py_raise((Mod<"xml.dom">().attr("HierarchyRequestErr"))((S<"%s cannot be child of %s">()) % py_tuple({ S(node.repr()), S(self.repr()) })));
                        }
                        if (!node.attr("parentNode").is_none())
                        {
                            // This needs to be done before the next test since this
                            // may *be* the document element, in which case it should
                            // end up re-ordered to the end.
                            node.attr("parentNode").method("removeChild", { node });
                        }
                        if (((node.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("ELEMENT_NODE")) && self.method("_get_documentElement").truthy()))
                        {
                            py_raise((Mod<"xml.dom">().attr("HierarchyRequestErr"))(S<"two document elements disallowed">()));
                        }
                        return (G<"xml.dom.minidom.Node">()).method("appendChild", { self, node });
                    }) },
                { "removeChild", w.method("Document", "removeChild", { py_positional("oldChild") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py oldChild = a[1];
                        try
                        {
                            self.attr("childNodes").method("remove", { oldChild });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler58;
                            if (matches(handler58.exception, B<"ValueError">()))
                            {
                                py_raise((Mod<"xml.dom">().attr("NotFoundErr"))());
                            }
                            else
                                py_reraise(handler58.exception);
                        }
                        py const value59 = none();
                        oldChild.set_attr("nextSibling", value59);
                        oldChild.set_attr("previousSibling", value59);
                        oldChild.set_attr("parentNode", none());
                        if (self.attr("documentElement").is(oldChild))
                        {
                            self.set_attr("documentElement", none());
                        }
                        return oldChild;
                    }) },
                { "_get_documentElement", w.method("Document", "_get_documentElement", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py node;
                        for (py const& item : iterate{ self.attr("childNodes") })
                        {
                            node = item;
                            if (node.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("ELEMENT_NODE"))
                            {
                                return node;
                            }
                        }
                        return none();
                    }) },
                { "unlink", w.method("Document", "unlink", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("doctype").is_none())
                        {
                            self.attr("doctype").method("unlink");
                            self.set_attr("doctype", none());
                        }
                        (G<"xml.dom.minidom.Node">()).method("unlink", { self });
                        return none();
                    }) },
                { "cloneNode", w.method("Document", "cloneNode", { py_positional("deep") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py deep = a[1];
                        py childclone;
                        py clone;
                        py n;
                        if (!deep.truthy())
                        {
                            return none();
                        }
                        clone = self.attr("implementation").method("createDocument", { none(), none(), none() });
                        clone.set_attr("encoding", self.attr("encoding"));
                        clone.set_attr("standalone", self.attr("standalone"));
                        clone.set_attr("version", self.attr("version"));
                        for (py const& item : iterate{ self.attr("childNodes") })
                        {
                            n = item;
                            childclone = (G<"xml.dom.minidom._clone_node">())(n, deep, clone);
                            if (!childclone.attr("ownerDocument").method("isSameNode", { clone }).truthy())
                                py_raise(B<"AssertionError">()());
                            clone.attr("childNodes").method("append", { childclone });
                            if (childclone.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("DOCUMENT_NODE"))
                            {
                                if (!clone.attr("documentElement").is_none())
                                    py_raise(B<"AssertionError">()());
                            }
                            else if (childclone.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("DOCUMENT_TYPE_NODE"))
                            {
                                if (!clone.attr("doctype").is_none())
                                    py_raise(B<"AssertionError">()());
                                clone.set_attr("doctype", childclone);
                            }
                            childclone.set_attr("parentNode", clone);
                        }
                        self.method("_call_user_data_handler", { (Mod<"xml.dom">().attr("UserDataHandler")).attr("NODE_CLONED"), self, clone });
                        return clone;
                    }) },
                { "createDocumentFragment", w.method("Document", "createDocumentFragment", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py d;
                        d = (G<"xml.dom.minidom.DocumentFragment">())();
                        d.set_attr("ownerDocument", self);
                        return d;
                    }) },
                { "createElement", w.method("Document", "createElement", { py_positional("tagName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tagName = a[1];
                        py e;
                        e = (G<"xml.dom.minidom.Element">())(tagName);
                        e.set_attr("ownerDocument", self);
                        return e;
                    }) },
                { "createTextNode", w.method("Document", "createTextNode", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py t;
                        if (!isinstance(data, B<"str">()))
                        {
                            py_raise((B<"TypeError">())(S<"node contents must be a string">()));
                        }
                        t = (G<"xml.dom.minidom.Text">())();
                        t.set_attr("data", data);
                        t.set_attr("ownerDocument", self);
                        return t;
                    }) },
                { "createCDATASection", w.method("Document", "createCDATASection", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py c;
                        if (!isinstance(data, B<"str">()))
                        {
                            py_raise((B<"TypeError">())(S<"node contents must be a string">()));
                        }
                        c = (G<"xml.dom.minidom.CDATASection">())();
                        c.set_attr("data", data);
                        c.set_attr("ownerDocument", self);
                        return c;
                    }) },
                { "createComment", w.method("Document", "createComment", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py c;
                        c = (G<"xml.dom.minidom.Comment">())(data);
                        c.set_attr("ownerDocument", self);
                        return c;
                    }) },
                { "createProcessingInstruction", w.method("Document", "createProcessingInstruction", { py_positional("target"), py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py target = a[1];
                        py data = a[2];
                        py p;
                        p = (G<"xml.dom.minidom.ProcessingInstruction">())(target, data);
                        p.set_attr("ownerDocument", self);
                        return p;
                    }) },
                { "createAttribute", w.method("Document", "createAttribute", { py_positional("qName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py qName = a[1];
                        py a_;
                        a_ = (G<"xml.dom.minidom.Attr">())(qName);
                        a_.set_attr("ownerDocument", self);
                        a_.set_attr("value", S<"">());
                        return a_;
                    }) },
                { "createElementNS", w.method("Document", "createElementNS", { py_positional("namespaceURI"), py_positional("qualifiedName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespaceURI = a[1];
                        py qualifiedName = a[2];
                        py e;
                        py localName;
                        py prefix;
                        auto const items62 = unpack((G<"xml.dom.minidom._nssplit">())(qualifiedName), 2);
                        prefix = items62[0];
                        localName = items62[1];
                        e = (G<"xml.dom.minidom.Element">())(qualifiedName, namespaceURI, prefix);
                        e.set_attr("ownerDocument", self);
                        return e;
                    }) },
                { "createAttributeNS", w.method("Document", "createAttributeNS", { py_positional("namespaceURI"), py_positional("qualifiedName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespaceURI = a[1];
                        py qualifiedName = a[2];
                        py a_;
                        py localName;
                        py prefix;
                        auto const items63 = unpack((G<"xml.dom.minidom._nssplit">())(qualifiedName), 2);
                        prefix = items63[0];
                        localName = items63[1];
                        a_ = (G<"xml.dom.minidom.Attr">())(qualifiedName, namespaceURI, localName, prefix);
                        a_.set_attr("ownerDocument", self);
                        a_.set_attr("value", S<"">());
                        return a_;
                    }) },
                { "_create_entity", w.method("Document", "_create_entity", { py_positional("name"), py_positional("publicId"), py_positional("systemId"), py_positional("notationName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py publicId = a[2];
                        py systemId = a[3];
                        py notationName = a[4];
                        py e;
                        e = (G<"xml.dom.minidom.Entity">())(name, publicId, systemId, notationName);
                        e.set_attr("ownerDocument", self);
                        return e;
                    }) },
                { "_create_notation", w.method("Document", "_create_notation", { py_positional("name"), py_positional("publicId"), py_positional("systemId") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py publicId = a[2];
                        py systemId = a[3];
                        py n;
                        n = (G<"xml.dom.minidom.Notation">())(name, publicId, systemId);
                        n.set_attr("ownerDocument", self);
                        return n;
                    }) },
                { "getElementById", w.method("Document", "getElementById", { py_positional("id") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py id = a[1];
                        py attr;
                        py info;
                        py node;
                        py result;
                        py stack;
                        if (self.attr("_id_cache").contains(id))
                        {
                            return self.attr("_id_cache")[id];
                        }
                        if (!([&]() -> py { py const t = self.attr("_elem_info"); return t.truthy() ? t : self.attr("_magic_id_count"); }()).truthy())
                        {
                            return none();
                        }
                        stack = self.attr("_id_search_stack");
                        if (stack.is_none())
                        {
                            // we never searched before, or the cache has been cleared
                            stack = py_list({ self.attr("documentElement") });
                            self.set_attr("_id_search_stack", stack);
                        }
                        else if (!stack.truthy())
                        {
                            // Previous search was completed and cache is still valid;
                            // no matching node.
                            return none();
                        }
                        result = none();
                        while (stack.truthy())
                        {
                            node = stack.method("pop");
                            // add child elements to stack for continued searching
                            stack.method("extend", { [&]() -> py { py const comprehension65 = py_list(); py child; for (py const& item : iterate{ node.attr("childNodes") }) { child = item; if (!(G<"xml.dom.minidom._nodeTypes_with_children">()).contains(child.attr("nodeType"))) continue; comprehension65.method("append", { child }); } return comprehension65; }() });
                            // check this node
                            info = self.method("_get_elem_info", { node });
                            if (info.truthy())
                            {
                                // We have to process all ID attributes before
                                // returning in order to get all the attributes set to
                                // be IDs using Element.setIdAttribute*().
                                for (py const& item : iterate{ node.attr("attributes").method("values") })
                                {
                                    attr = item;
                                    if (attr.attr("namespaceURI").truthy())
                                    {
                                        if (info.method("isIdNS", { attr.attr("namespaceURI"), attr.attr("localName") }).truthy())
                                        {
                                            self.attr("_id_cache").set_item(attr.attr("value"), node);
                                            if (attr.attr("value") == id)
                                            {
                                                result = node;
                                            }
                                            else if (!node.attr("_magic_id_nodes").truthy())
                                            {
                                                break;
                                            }
                                        }
                                    }
                                    else if (info.method("isId", { attr.attr("name") }).truthy())
                                    {
                                        self.attr("_id_cache").set_item(attr.attr("value"), node);
                                        if (attr.attr("value") == id)
                                        {
                                            result = node;
                                        }
                                        else if (!node.attr("_magic_id_nodes").truthy())
                                        {
                                            break;
                                        }
                                    }
                                    else if (attr.attr("_is_id").truthy())
                                    {
                                        self.attr("_id_cache").set_item(attr.attr("value"), node);
                                        if (attr.attr("value") == id)
                                        {
                                            result = node;
                                        }
                                        else if (node.attr("_magic_id_nodes") == (I<1>()))
                                        {
                                            break;
                                        }
                                    }
                                }
                            }
                            else if (node.attr("_magic_id_nodes").truthy())
                            {
                                for (py const& item : iterate{ node.attr("attributes").method("values") })
                                {
                                    attr = item;
                                    if (attr.attr("_is_id").truthy())
                                    {
                                        self.attr("_id_cache").set_item(attr.attr("value"), node);
                                        if (attr.attr("value") == id)
                                        {
                                            result = node;
                                        }
                                    }
                                }
                            }
                            if (!result.is_none())
                            {
                                break;
                            }
                        }
                        return result;
                    }) },
                { "getElementsByTagName", w.method("Document", "getElementsByTagName", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        return (G<"xml.dom.minidom._get_elements_by_tagName_helper">())(self, name, (Mod<"xml.dom.minicompat">().attr("NodeList"))());
                    }) },
                { "getElementsByTagNameNS", w.method("Document", "getElementsByTagNameNS", { py_positional("namespaceURI"), py_positional("localName") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py namespaceURI = a[1];
                        py localName = a[2];
                        return (G<"xml.dom.minidom._get_elements_by_tagName_ns_helper">())(self, namespaceURI, localName, (Mod<"xml.dom.minicompat">().attr("NodeList"))());
                    }) },
                { "isSupported", w.method("Document", "isSupported", { py_positional("feature"), py_positional("version") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py feature = a[1];
                        py version = a[2];
                        return self.attr("implementation").method("hasFeature", { feature, version });
                    }) },
                { "importNode", w.method("Document", "importNode", { py_positional("node"), py_positional("deep") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py node = a[1];
                        py deep = a[2];
                        if (node.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("DOCUMENT_NODE"))
                        {
                            py_raise((Mod<"xml.dom">().attr("NotSupportedErr"))(S<"cannot import document nodes">()));
                        }
                        else if (node.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("DOCUMENT_TYPE_NODE"))
                        {
                            py_raise((Mod<"xml.dom">().attr("NotSupportedErr"))(S<"cannot import document type nodes">()));
                        }
                        return (G<"xml.dom.minidom._clone_node">())(node, deep, self);
                    }) },
                { "writexml", w.method("Document", "writexml", { py_positional("writer"), py_positional("indent", S<"">()), py_positional("addindent", S<"">()), py_positional("newl", S<"">()), py_positional("encoding", none()), py_positional("standalone", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py writer = a[1];
                        py indent = a[2];
                        py addindent = a[3];
                        py newl = a[4];
                        py encoding = a[5];
                        py standalone = a[6];
                        py declarations;
                        py node;
                        declarations = py_list();
                        if (encoding.truthy())
                        {
                            declarations.method("append", { S(std::string{ "encoding=\"" } + encoding.str() + "\"") });
                        }
                        if (!standalone.is_none())
                        {
                            declarations.method("append", { S(std::string{ "standalone=\"" } + (standalone.truthy() ? S<"yes">() : S<"no">()).str() + "\"") });
                        }
                        writer.method("write", { S(std::string{ "<?xml version=\"1.0\" " } + (S<" ">()).method("join", { declarations }).str() + "?>" + newl.str()) });
                        for (py const& item : iterate{ self.attr("childNodes") })
                        {
                            node = item;
                            node.method("writexml", { writer, indent, addindent, newl });
                        }
                        return none();
                    }) },
                { "renameNode", w.method("Document", "renameNode", { py_positional("n"), py_positional("namespaceURI"), py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n = a[1];
                        py namespaceURI = a[2];
                        py name = a[3];
                        py element;
                        py is_id;
                        py localName;
                        py prefix;
                        if (!n.attr("ownerDocument").is(self))
                        {
                            py_raise((Mod<"xml.dom">().attr("WrongDocumentErr"))((S<"cannot rename nodes from other documents;\nexpected %s,\nfound %s">()) % py_tuple({ self, n.attr("ownerDocument") })));
                        }
                        if (!py_tuple({ (G<"xml.dom.minidom.Node">()).attr("ELEMENT_NODE"), (G<"xml.dom.minidom.Node">()).attr("ATTRIBUTE_NODE") }).contains(n.attr("nodeType")))
                        {
                            py_raise((Mod<"xml.dom">().attr("NotSupportedErr"))(S<"renameNode() only applies to element and attribute nodes">()));
                        }
                        if (namespaceURI != (Mod<"xml.dom">().attr("EMPTY_NAMESPACE")))
                        {
                            if (name.contains((S<":">())))
                            {
                                auto const items69 = unpack(name.method("split", { S<":">(), I<1>() }), 2);
                                prefix = items69[0];
                                localName = items69[1];
                                if (((prefix == (S<"xmlns">())) && (namespaceURI != (Mod<"xml.dom">().attr("XMLNS_NAMESPACE")))))
                                {
                                    py_raise((Mod<"xml.dom">().attr("NamespaceErr"))(S<"illegal use of 'xmlns' prefix">()));
                                }
                            }
                            else
                            {
                                if (((name == (S<"xmlns">())) && (namespaceURI != (Mod<"xml.dom">().attr("XMLNS_NAMESPACE"))) && (n.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("ATTRIBUTE_NODE"))))
                                {
                                    py_raise((Mod<"xml.dom">().attr("NamespaceErr"))(S<"illegal use of the 'xmlns' attribute">()));
                                }
                                prefix = none();
                                localName = name;
                            }
                        }
                        else
                        {
                            prefix = none();
                            localName = none();
                        }
                        if (n.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("ATTRIBUTE_NODE"))
                        {
                            element = n.attr("ownerElement");
                            if (!element.is_none())
                            {
                                is_id = n.attr("_is_id");
                                element.method("removeAttributeNode", { n });
                            }
                        }
                        else
                        {
                            element = none();
                        }
                        n.set_attr("prefix", prefix);
                        n.set_attr("_localName", localName);
                        n.set_attr("namespaceURI", namespaceURI);
                        n.set_attr("nodeName", name);
                        if (n.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("ELEMENT_NODE"))
                        {
                            n.set_attr("tagName", name);
                        }
                        else
                        {
                            // attribute node
                            n.set_attr("name", name);
                            if (!element.is_none())
                            {
                                element.method("setAttributeNode", { n });
                                if (is_id.truthy())
                                {
                                    element.method("setIdAttributeNode", { n });
                                }
                            }
                        }
                        // It's not clear from a semantic perspective whether we should
                        // call the user data handlers for the NODE_RENAMED event since
                        // we're re-using the existing node.  The draft spec has been
                        // interpreted as meaning "no, don't call the handler unless a
                        // new node is created."
                        return n;
                    }) }, }));

            (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                {
                    (Mod<"xml.dom.minicompat">().attr("defproperty")).call({ G<"xml.dom.minidom.Document">(), S<"documentElement">() }, { { "doc", S<"Top-level element of this document.">() } });
                    return none();
                }))();
            define("xml.dom.minidom", "_clone_node", w.function("_clone_node", "_clone_node", { py_positional("node"), py_positional("deep"), py_positional("newOwnerDocument") }, [](arguments a) -> py
                {
                    py node = a[0];
                    py deep = a[1];
                    py newOwnerDocument = a[2];
                    py a_;
                    py attr;
                    py c;
                    py child;
                    py clone;
                    py e;
                    py entity;
                    py n;
                    py notation;
                    py operation;
                    if (node.attr("ownerDocument").method("isSameNode", { newOwnerDocument }).truthy())
                    {
                        operation = (Mod<"xml.dom">().attr("UserDataHandler")).attr("NODE_CLONED");
                    }
                    else
                    {
                        operation = (Mod<"xml.dom">().attr("UserDataHandler")).attr("NODE_IMPORTED");
                    }
                    if (node.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("ELEMENT_NODE"))
                    {
                        clone = newOwnerDocument.method("createElementNS", { node.attr("namespaceURI"), node.attr("nodeName") });
                        for (py const& item : iterate{ node.attr("attributes").method("values") })
                        {
                            attr = item;
                            clone.method("setAttributeNS", { attr.attr("namespaceURI"), attr.attr("nodeName"), attr.attr("value") });
                            a_ = clone.method("getAttributeNodeNS", { attr.attr("namespaceURI"), attr.attr("localName") });
                            a_.set_attr("specified", attr.attr("specified"));
                        }
                        if (deep.truthy())
                        {
                            for (py const& item : iterate{ node.attr("childNodes") })
                            {
                                child = item;
                                c = (G<"xml.dom.minidom._clone_node">())(child, deep, newOwnerDocument);
                                clone.method("appendChild", { c });
                            }
                        }
                    }
                    else if (node.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("DOCUMENT_FRAGMENT_NODE"))
                    {
                        clone = newOwnerDocument.method("createDocumentFragment");
                        if (deep.truthy())
                        {
                            for (py const& item : iterate{ node.attr("childNodes") })
                            {
                                child = item;
                                c = (G<"xml.dom.minidom._clone_node">())(child, deep, newOwnerDocument);
                                clone.method("appendChild", { c });
                            }
                        }
                    }
                    else if (node.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("TEXT_NODE"))
                    {
                        clone = newOwnerDocument.method("createTextNode", { node.attr("data") });
                    }
                    else if (node.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("CDATA_SECTION_NODE"))
                    {
                        clone = newOwnerDocument.method("createCDATASection", { node.attr("data") });
                    }
                    else if (node.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("PROCESSING_INSTRUCTION_NODE"))
                    {
                        clone = newOwnerDocument.method("createProcessingInstruction", { node.attr("target"), node.attr("data") });
                    }
                    else if (node.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("COMMENT_NODE"))
                    {
                        clone = newOwnerDocument.method("createComment", { node.attr("data") });
                    }
                    else if (node.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("ATTRIBUTE_NODE"))
                    {
                        clone = newOwnerDocument.method("createAttributeNS", { node.attr("namespaceURI"), node.attr("nodeName") });
                        clone.set_attr("specified", boolean(true));
                        clone.set_attr("value", node.attr("value"));
                    }
                    else if (node.attr("nodeType") == (G<"xml.dom.minidom.Node">()).attr("DOCUMENT_TYPE_NODE"))
                    {
                        if (node.attr("ownerDocument").is(newOwnerDocument))
                            py_raise(B<"AssertionError">()());
                        operation = (Mod<"xml.dom">().attr("UserDataHandler")).attr("NODE_IMPORTED");
                        clone = newOwnerDocument.attr("implementation").method("createDocumentType", { node.attr("name"), node.attr("publicId"), node.attr("systemId") });
                        clone.set_attr("ownerDocument", newOwnerDocument);
                        if (deep.truthy())
                        {
                            clone.attr("entities").set_attr("_seq", py_list());
                            clone.attr("notations").set_attr("_seq", py_list());
                            for (py const& item : iterate{ node.attr("notations").attr("_seq") })
                            {
                                n = item;
                                notation = (G<"xml.dom.minidom.Notation">())(n.attr("nodeName"), n.attr("publicId"), n.attr("systemId"));
                                notation.set_attr("ownerDocument", newOwnerDocument);
                                clone.attr("notations").attr("_seq").method("append", { notation });
                                if (has_attr(n, "_call_user_data_handler"))
                                {
                                    n.method("_call_user_data_handler", { operation, n, notation });
                                }
                            }
                            for (py const& item : iterate{ node.attr("entities").attr("_seq") })
                            {
                                e = item;
                                entity = (G<"xml.dom.minidom.Entity">())(e.attr("nodeName"), e.attr("publicId"), e.attr("systemId"), e.attr("notationName"));
                                entity.set_attr("actualEncoding", e.attr("actualEncoding"));
                                entity.set_attr("encoding", e.attr("encoding"));
                                entity.set_attr("version", e.attr("version"));
                                entity.set_attr("ownerDocument", newOwnerDocument);
                                clone.attr("entities").attr("_seq").method("append", { entity });
                                if (has_attr(e, "_call_user_data_handler"))
                                {
                                    e.method("_call_user_data_handler", { operation, e, entity });
                                }
                            }
                        }
                    }
                    else
                    {
                        // Note the cloning of Document and DocumentType nodes is
                        // implementation specific.  minidom handles those cases
                        // directly in the cloneNode() methods.
                        py_raise((Mod<"xml.dom">().attr("NotSupportedErr"))((S<"Cannot clone node %s">()) % S(node.repr())));
                    }
                    // Check for _call_user_data_handler() since this could conceivably
                    // used with other DOM implementations (one of the FourThought
                    // DOMs, perhaps?).
                    if (has_attr(node, "_call_user_data_handler"))
                    {
                        node.method("_call_user_data_handler", { operation, node, clone });
                    }
                    return clone;
                }));

            define("xml.dom.minidom", "_nssplit", w.function("_nssplit", "_nssplit", { py_positional("qualifiedName") }, [](arguments a) -> py
                {
                    py qualifiedName = a[0];
                    py fields;
                    fields = qualifiedName.method("split", { S<":">(), I<1>() });
                    if (py{ fields.size() } == (I<2>()))
                    {
                        return fields;
                    }
                    else
                    {
                        return py_tuple({ none(), fields[I<0>()] });
                    }
                    return none();
                }));

            define("xml.dom.minidom", "_do_pulldom_parse", w.function("_do_pulldom_parse", "_do_pulldom_parse", { py_positional("func"), py_positional("args"), py_positional("kwargs") }, [](arguments a) -> py
                {
                    py func = a[0];
                    py args = a[1];
                    py kwargs = a[2];
                    py events;
                    py rootNode;
                    py toktype;
                    events = call_ex(func, {  }, args, kwargs);
                    auto const items75 = unpack(events.method("getEvent"), 2);
                    toktype = items75[0];
                    rootNode = items75[1];
                    events.method("expandNode", { rootNode });
                    events.method("clear");
                    return rootNode;
                }));

            define("xml.dom.minidom", "parse", w.function("parse", "parse", { py_positional("file"), py_positional("parser", none()), py_positional("bufsize", none()) }, [](arguments a) -> py
                {
                    py file = a[0];
                    py parser = a[1];
                    py bufsize = a[2];
                    py expatbuilder;
                    py pulldom;
                    if ((parser.is_none() && (!bufsize.truthy())))
                    {
                        expatbuilder = M("xml.dom", "expatbuilder");
                        return expatbuilder.method("parse", { file });
                    }
                    else
                    {
                        pulldom = M("xml.dom", "pulldom");
                        return (G<"xml.dom.minidom._do_pulldom_parse">())(pulldom.attr("parse"), py_tuple({ file }), dict_of({ { S<"parser">(), parser }, { S<"bufsize">(), bufsize } }));
                    }
                    return none();
                }));

            define("xml.dom.minidom", "parseString", w.function("parseString", "parseString", { py_positional("string"), py_positional("parser", none()) }, [](arguments a) -> py
                {
                    py string = a[0];
                    py parser = a[1];
                    py expatbuilder;
                    py pulldom;
                    if (parser.is_none())
                    {
                        expatbuilder = M("xml.dom", "expatbuilder");
                        return expatbuilder.method("parseString", { string });
                    }
                    else
                    {
                        pulldom = M("xml.dom", "pulldom");
                        return (G<"xml.dom.minidom._do_pulldom_parse">())(pulldom.attr("parseString"), py_tuple({ string }), dict_of({ { S<"parser">(), parser } }));
                    }
                    return none();
                }));

            define("xml.dom.minidom", "getDOMImplementation", w.function("getDOMImplementation", "getDOMImplementation", { py_positional("features", none()) }, [](arguments a) -> py
                {
                    py features = a[0];
                    py f;
                    py v;
                    if (features.truthy())
                    {
                        if (isinstance(features, B<"str">()))
                        {
                            features = (Mod<"xml.dom.domreg">().attr("_parse_feature_string"))(features);
                        }
                        for (py const& item : iterate{ features })
                        {
                            auto const items77 = unpack(item, 2);
                            f = items77[0];
                            v = items77[1];
                            if (!(G<"xml.dom.minidom.Document">()).attr("implementation").method("hasFeature", { f, v }).truthy())
                            {
                                return none();
                            }
                        }
                    }
                    return (G<"xml.dom.minidom.Document">()).attr("implementation");
                }));
        }

        bool xml_dom_minidom_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_xml_dom_minidom();
                }
                if (auto const doc = xml_dom_minidom_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("io", py{ fn_import("io") }.release());
                aAttributes.emplace_back("xml", py{ fn_import("xml") }.release());
                aAttributes.emplace_back("EMPTY_NAMESPACE", import_attribute("xml.dom", "EMPTY_NAMESPACE").release());
                aAttributes.emplace_back("EMPTY_PREFIX", import_attribute("xml.dom", "EMPTY_PREFIX").release());
                aAttributes.emplace_back("XMLNS_NAMESPACE", import_attribute("xml.dom", "XMLNS_NAMESPACE").release());
                aAttributes.emplace_back("domreg", import_attribute("xml.dom", "domreg").release());
                {
                    py const m{ fn_import("xml.dom.minicompat") };
                    bool const all = has_attr(m, "__all__");
                    for (py const& n : iterate{ all ? m.attr("__all__") : builtin("dir")(m) })
                        if (auto const text = n.str(); all || !text.starts_with("_"))
                            aAttributes.emplace_back(text, m.attr(text.c_str()).release());
                }
                aAttributes.emplace_back("DOMImplementationLS", import_attribute("xml.dom.xmlbuilder", "DOMImplementationLS").release());
                aAttributes.emplace_back("DocumentLS", import_attribute("xml.dom.xmlbuilder", "DocumentLS").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("xml.dom.minidom", xml_dom_minidom_module);
    }
}
