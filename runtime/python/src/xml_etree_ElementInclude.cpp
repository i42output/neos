/*
  xml_etree_ElementInclude.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/xml/etree/ElementInclude.py), Copyright (c) 2001-2024 Python
  Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/xml/etree/ElementInclude.py's copyright: Copyright (c) 2003-2004 by
  Fredrik Lundh. Used under its permission notice (see runtime/python/NOTICE); licensed to PSF under a Contributor
  Agreement.
*/

// The module xml.etree.ElementInclude (CPython 3.13's Lib/xml/etree/ElementInclude.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "xml_etree_ElementInclude_docs.hpp"

namespace neos::python_runtime::detail::xml_etree_ElementInclude_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "xml.etree.ElementInclude.name"))
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
                py_raise("SystemError", std::string{ "xml.etree.ElementInclude: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"xml.etree.ElementInclude.name">()))
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
            if (modules_imported("xml.etree.ElementInclude"))
                return py{ fn_import("xml.etree.ElementInclude") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "xml.etree.ElementInclude" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "xml.etree.ElementInclude", xml_etree_ElementInclude_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- xml.etree.ElementInclude (Lib/xml/etree/ElementInclude.py)

        void make_xml_etree_ElementInclude()
        {
            static module_writer const& w = writer("xml.etree.ElementInclude");
            // 
            // ElementTree
            // $Id: ElementInclude.py 3375 2008-02-13 08:05:08Z fredrik $
            // 
            // limited xinclude support for element trees
            // 
            // history:
            // 2003-08-15 fl   created
            // 2003-11-14 fl   fixed default loader
            // 
            // Copyright (c) 2003-2004 by Fredrik Lundh.  All rights reserved.
            // 
            // fredrik@pythonware.com
            // http://www.pythonware.com
            // 
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
            // Licensed to PSF under a Contributor Agreement.
            // See https://www.python.org/psf/license for licensing details.
            // #
            // Limited XInclude support for the ElementTree package.
            // #
            define("xml.etree.ElementInclude", "XINCLUDE", S<"{http://www.w3.org/2001/XInclude}">());
            define("xml.etree.ElementInclude", "XINCLUDE_INCLUDE", (G<"xml.etree.ElementInclude.XINCLUDE">()) + (S<"include">()));
            define("xml.etree.ElementInclude", "XINCLUDE_FALLBACK", (G<"xml.etree.ElementInclude.XINCLUDE">()) + (S<"fallback">()));
            // For security reasons, the inclusion depth is limited to this read-only value by default.
            define("xml.etree.ElementInclude", "DEFAULT_MAX_INCLUSION_DEPTH", I<6>());
            // #
            // Fatal include error.
            define("xml.etree.ElementInclude", "FatalIncludeError", w.make_class("FatalIncludeError", { B<"SyntaxError">() }, {
 }));

            define("xml.etree.ElementInclude", "LimitedRecursiveIncludeError", w.make_class("LimitedRecursiveIncludeError", { G<"xml.etree.ElementInclude.FatalIncludeError">() }, {
 }));

            // #
            // Default loader.  This loader reads an included resource from disk.
            // 
            // @param href Resource reference.
            // @param parse Parse mode.  Either "xml" or "text".
            // @param encoding Optional text encoding (UTF-8 by default for "text").
            // @return The expanded resource.  If the parse mode is "xml", this
            // is an Element instance.  If the parse mode is "text", this
            // is a string.  If the loader fails, it can return None
            // or raise an OSError exception.
            // @throws OSError If the loader fails to load the resource.
            define("xml.etree.ElementInclude", "default_loader", w.function("default_loader", "default_loader", { py_positional("href"), py_positional("parse"), py_positional("encoding", none()) }, [](arguments a) -> py
                {
                    py href = a[0];
                    py parse = a[1];
                    py encoding = a[2];
                    py data;
                    py file;
                    if (parse == (S<"xml">()))
                    {
                        {
                            py const manager1 = (B<"open">())(href, S<"rb">());
                            py const exit1 = pyclass::special_method(manager1, "__exit__");
                            py const value1 = pyclass::special_method(manager1, "__enter__")(manager1);
                            file = value1;
                            [[maybe_unused]] int flow1 = 0;
                            [[maybe_unused]] py result1;
                            bool exited1 = false;
                            try
                            {
                                data = (Mod<"xml.etree.ElementTree">().attr("parse"))(file).method("getroot");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler1;
                                exited1 = true;
                                if (!exit1(manager1, type_of(handler1.exception), handler1.exception, handler1.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler1.exception);
                            }
                            finally1:
                            if (!exited1)
                                exit1(manager1, none(), none(), none());
                            if (flow1 == 1)
                                return result1;
                        }
                    }
                    else
                    {
                        if (!encoding.truthy())
                        {
                            encoding = S<"UTF-8">();
                        }
                        {
                            py const manager2 = (B<"open">()).call({ href, S<"r">() }, { { "encoding", encoding } });
                            py const exit2 = pyclass::special_method(manager2, "__exit__");
                            py const value2 = pyclass::special_method(manager2, "__enter__")(manager2);
                            file = value2;
                            [[maybe_unused]] int flow2 = 0;
                            [[maybe_unused]] py result2;
                            bool exited2 = false;
                            try
                            {
                                data = file.method("read");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler2;
                                exited2 = true;
                                if (!exit2(manager2, type_of(handler2.exception), handler2.exception, handler2.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler2.exception);
                            }
                            finally2:
                            if (!exited2)
                                exit2(manager2, none(), none(), none());
                            if (flow2 == 1)
                                return result2;
                        }
                    }
                    return data;
                }));

            // #
            // Expand XInclude directives.
            // 
            // @param elem Root Element or any ElementTree of a tree to be expanded
            // @param loader Optional resource loader.  If omitted, it defaults
            // to {@link default_loader}.  If given, it should be a callable
            // that implements the same interface as <b>default_loader</b>.
            // @param base_url The base URL of the original file, to resolve
            // relative include file references.
            // @param max_depth The maximum number of recursive inclusions.
            // Limited to reduce the risk of malicious content explosion.
            // Pass None to disable the limitation.
            // @throws LimitedRecursiveIncludeError If the {@link max_depth} was exceeded.
            // @throws FatalIncludeError If the function fails to include a given
            // resource, or if the tree contains malformed XInclude elements.
            // @throws OSError If the function fails to load a given resource.
            // @throws ValueError If negative {@link max_depth} is passed.
            // @returns None. Modifies tree pointed by {@link elem}
            define("xml.etree.ElementInclude", "include", w.function("include", "include", { py_positional("elem"), py_positional("loader", none()), py_positional("base_url", none()), py_positional("max_depth", G<"xml.etree.ElementInclude.DEFAULT_MAX_INCLUSION_DEPTH">()) }, [](arguments a) -> py
                {
                    py elem = a[0];
                    py loader = a[1];
                    py base_url = a[2];
                    py max_depth = a[3];
                    if (max_depth.is_none())
                    {
                        max_depth = I<-1>();
                    }
                    else if (max_depth < (I<0>()))
                    {
                        py_raise((B<"ValueError">())((S<"expected non-negative depth or None for 'max_depth', got %r">()) % max_depth));
                    }
                    if (has_attr(elem, "getroot"))
                    {
                        elem = elem.method("getroot");
                    }
                    if (loader.is_none())
                    {
                        loader = G<"xml.etree.ElementInclude.default_loader">();
                    }
                    (G<"xml.etree.ElementInclude._include">())(elem, loader, base_url, max_depth, (B<"set">())());
                    return none();
                }));

            define("xml.etree.ElementInclude", "_include", w.function("_include", "_include", { py_positional("elem"), py_positional("loader"), py_positional("base_url"), py_positional("max_depth"), py_positional("_parent_hrefs") }, [](arguments a) -> py
                {
                    py elem = a[0];
                    py loader = a[1];
                    py base_url = a[2];
                    py max_depth = a[3];
                    py _parent_hrefs = a[4];
                    py e;
                    py href;
                    py i;
                    py node;
                    py parse;
                    py text;
                    i = I<0>();
                    while (i < py{ elem.size() })
                    {
                        e = elem[i];
                        if (e.attr("tag") == (G<"xml.etree.ElementInclude.XINCLUDE_INCLUDE">()))
                        {
                            // process xinclude directive
                            href = e.method("get", { S<"href">() });
                            if (base_url.truthy())
                            {
                                href = (Mod<"urllib.parse">().attr("urljoin"))(base_url, href);
                            }
                            parse = e.method("get", { S<"parse">(), S<"xml">() });
                            if (parse == (S<"xml">()))
                            {
                                if (_parent_hrefs.contains(href))
                                {
                                    py_raise((G<"xml.etree.ElementInclude.FatalIncludeError">())((S<"recursive include of %s">()) % href));
                                }
                                if (max_depth == (I<0>()))
                                {
                                    py_raise((G<"xml.etree.ElementInclude.LimitedRecursiveIncludeError">())((S<"maximum xinclude depth reached when including file %s">()) % href));
                                }
                                _parent_hrefs.method("add", { href });
                                node = loader(href, parse);
                                if (node.is_none())
                                {
                                    py_raise((G<"xml.etree.ElementInclude.FatalIncludeError">())((S<"cannot load %r as %r">()) % py_tuple({ href, parse })));
                                }
                                node = (Mod<"copy">().attr("copy"))(node);
                                (G<"xml.etree.ElementInclude._include">())(node, loader, href, max_depth - (I<1>()), _parent_hrefs);
                                _parent_hrefs.method("remove", { href });
                                if (e.attr("tail").truthy())
                                {
                                    node.set_attr("tail", ([&]() -> py { py const t = node.attr("tail"); return t.truthy() ? t : S<"">(); }()) + e.attr("tail"));
                                }
                                elem.set_item(i, node);
                            }
                            else if (parse == (S<"text">()))
                            {
                                text = loader(href, parse, e.method("get", { S<"encoding">() }));
                                if (text.is_none())
                                {
                                    py_raise((G<"xml.etree.ElementInclude.FatalIncludeError">())((S<"cannot load %r as %r">()) % py_tuple({ href, parse })));
                                }
                                if (e.attr("tail").truthy())
                                {
                                    text = inplace(NEOS_PY_ADD, text, e.attr("tail"));
                                }
                                if (i.truthy())
                                {
                                    node = elem[i - (I<1>())];
                                    node.set_attr("tail", ([&]() -> py { py const t = node.attr("tail"); return t.truthy() ? t : S<"">(); }()) + text);
                                }
                                else
                                {
                                    elem.set_attr("text", ([&]() -> py { py const t = elem.attr("text"); return t.truthy() ? t : S<"">(); }()) + text);
                                }
                                del_item(elem, i);
                                continue;
                            }
                            else
                            {
                                py_raise((G<"xml.etree.ElementInclude.FatalIncludeError">())((S<"unknown parse type in xi:include tag (%r)">()) % parse));
                            }
                        }
                        else if (e.attr("tag") == (G<"xml.etree.ElementInclude.XINCLUDE_FALLBACK">()))
                        {
                            py_raise((G<"xml.etree.ElementInclude.FatalIncludeError">())((S<"xi:fallback tag must be child of xi:include (%r)">()) % e.attr("tag")));
                        }
                        else
                        {
                            (G<"xml.etree.ElementInclude._include">())(e, loader, base_url, max_depth, _parent_hrefs);
                        }
                        i = inplace(NEOS_PY_ADD, i, I<1>());
                    }
                    return none();
                }));
        }

        bool xml_etree_ElementInclude_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_xml_etree_ElementInclude();
                }
                if (auto const doc = xml_etree_ElementInclude_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("copy", py{ fn_import("copy") }.release());
                aAttributes.emplace_back("ElementTree", py{ fn_import("xml.etree.ElementTree") }.release());
                aAttributes.emplace_back("urljoin", import_attribute("urllib.parse", "urljoin").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("xml.etree.ElementInclude", xml_etree_ElementInclude_module);
    }
}
