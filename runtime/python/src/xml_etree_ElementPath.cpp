/*
  xml_etree_ElementPath.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/xml/etree/ElementPath.py), Copyright (c) 2001-2024 Python
  Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE). Lib/xml/etree/ElementPath.py's copyright: Copyright (c) 2003-2009 by
  Fredrik Lundh. Used under its permission notice (see runtime/python/NOTICE); licensed to PSF under a Contributor
  Agreement.
*/

// The module xml.etree.ElementPath (CPython 3.13's Lib/xml/etree/ElementPath.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "xml_etree_ElementPath_docs.hpp"

namespace neos::python_runtime::detail::xml_etree_ElementPath_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "xml.etree.ElementPath.name"))
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
                py_raise("SystemError", std::string{ "xml.etree.ElementPath: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"xml.etree.ElementPath.name">()))
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
            if (modules_imported("xml.etree.ElementPath"))
                return py{ fn_import("xml.etree.ElementPath") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "xml.etree.ElementPath" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "xml.etree.ElementPath", xml_etree_ElementPath_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- xml.etree.ElementPath (Lib/xml/etree/ElementPath.py)

        void make_xml_etree_ElementPath()
        {
            static module_writer const& w = writer("xml.etree.ElementPath");
            // 
            // ElementTree
            // $Id: ElementPath.py 3375 2008-02-13 08:05:08Z fredrik $
            // 
            // limited xpath support for element trees
            // 
            // history:
            // 2003-05-23 fl   created
            // 2003-05-28 fl   added support for // etc
            // 2003-08-27 fl   fixed parsing of periods in element names
            // 2007-09-10 fl   new selection engine
            // 2007-09-12 fl   fixed parent selector
            // 2007-09-13 fl   added iterfind; changed findall to return a list
            // 2007-11-30 fl   added namespaces support
            // 2009-10-30 fl   added child element value filter
            // 
            // Copyright (c) 2003-2009 by Fredrik Lundh.  All rights reserved.
            // 
            // fredrik@pythonware.com
            // http://www.pythonware.com
            // 
            // --------------------------------------------------------------------
            // The ElementTree toolkit is
            // 
            // Copyright (c) 1999-2009 by Fredrik Lundh
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
            // Implementation module for XPath support.  There's usually no reason
            // to import this module directly; the <b>ElementTree</b> does this for
            // you, if needed.
            // #
            define("xml.etree.ElementPath", "xpath_tokenizer_re", (Mod<"re">()).method("compile", { S<"('[^']*'|\\\"[^\\\"]*\\\"|::|//?|\\.\\.|\\(\\)|!=|[/.*:\\[\\]\\(\\)@=])|((?:\\{[^}]+\\})?[^/\\[\\]\\(\\)@!=\\s]+)|\\s+">() }));
            define("xml.etree.ElementPath", "xpath_tokenizer", w.function("xpath_tokenizer", "xpath_tokenizer", { py_positional("pattern"), py_positional("namespaces", none()) }, [](arguments a) -> py
                {
                    return make_native_generator("xpath_tokenizer", "xpath_tokenizer", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                    {
                        py pattern = a[0];
                        py namespaces = a[1];
                        py default_namespace;
                        py parsing_attribute;
                        py prefix;
                        py tag;
                        py token;
                        py ttype;
                        py uri;
                        default_namespace = (namespaces.truthy() ? namespaces.method("get", { S<"">() }) : none());
                        parsing_attribute = boolean(false);
                        for (py const& item : iterate{ (G<"xml.etree.ElementPath.xpath_tokenizer_re">()).method("findall", { pattern }) })
                        {
                            token = item;
                            auto const items2 = unpack(token, 2);
                            ttype = items2[0];
                            tag = items2[1];
                            if ((tag.truthy() && (tag[I<0>()] != (S<"{">()))))
                            {
                                if (tag.contains((S<":">())))
                                {
                                    auto const items3 = unpack(tag.method("split", { S<":">(), I<1>() }), 2);
                                    prefix = items3[0];
                                    uri = items3[1];
                                    try
                                    {
                                        if (!namespaces.truthy())
                                        {
                                            py_raise(B<"KeyError">());
                                        }
                                        py_yield(py_tuple({ ttype, (S<"{%s}%s">()) % py_tuple({ namespaces[prefix], uri }) }));
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler4;
                                        if (matches(handler4.exception, B<"KeyError">()))
                                        {
                                            fn_raise_from((B<"SyntaxError">())((S<"prefix %r not found in prefix map">()) % prefix).get(), none().get());
                                            throw raised{};
                                        }
                                        else
                                            py_reraise(handler4.exception);
                                    }
                                }
                                else if ((default_namespace.truthy() && (!parsing_attribute.truthy())))
                                {
                                    py_yield(py_tuple({ ttype, (S<"{%s}%s">()) % py_tuple({ default_namespace, tag }) }));
                                }
                                else
                                {
                                    py_yield(token);
                                }
                                parsing_attribute = boolean(false);
                            }
                            else
                            {
                                py_yield(token);
                                parsing_attribute = boolean(ttype == (S<"@">()));
                            }
                        }
                        return none();
                    });
                }));

            define("xml.etree.ElementPath", "get_parent_map", w.function("get_parent_map", "get_parent_map", { py_positional("context") }, [](arguments a) -> py
                {
                    py context = a[0];
                    py e;
                    py p;
                    py parent_map;
                    parent_map = context.attr("parent_map");
                    if (parent_map.is_none())
                    {
                        py const value5 = py_dict();
                        context.set_attr("parent_map", value5);
                        parent_map = value5;
                        for (py const& item : iterate{ context.attr("root").method("iter") })
                        {
                            p = item;
                            for (py const& item : iterate{ p })
                            {
                                e = item;
                                parent_map.set_item(e, p);
                            }
                        }
                    }
                    return parent_map;
                }));

            define("xml.etree.ElementPath", "_is_wildcard_tag", w.function("_is_wildcard_tag", "_is_wildcard_tag", { py_positional("tag") }, [](arguments a) -> py
                {
                    py tag = a[0];
                    return [&]() -> py { py const t = boolean(tag[B<"slice">()(none(), I<3>(), none())] == (S<"{*}">())); return t.truthy() ? t : boolean(tag[B<"slice">()(I<-2>(), none(), none())] == (S<"}*">())); }();
                }));

            define("xml.etree.ElementPath", "_prepare_tag", w.function("_prepare_tag", "_prepare_tag", { py_positional("tag") }, [](arguments a) -> py
                {
                    auto const tag = std::make_shared<py>(a[0]);
                    auto const _isinstance = std::make_shared<py>();
                    auto const _str = std::make_shared<py>();
                    auto const no_ns = std::make_shared<py>();
                    auto const ns = std::make_shared<py>();
                    auto const ns_only = std::make_shared<py>();
                    py select;
                    auto const suffix = std::make_shared<py>();
                    auto const items8 = unpack(py_tuple({ B<"isinstance">(), B<"str">() }), 2);
                    *_isinstance = items8[0];
                    *_str = items8[1];
                    if ((*tag) == (S<"{*}*">()))
                    {
                        // Same as '*', but no comments or processing instructions.
                        // It can be a surprise that '*' includes those, but there is no
                        // justification for '{*}*' doing the same.
                        select = w.function("select", "_prepare_tag.<locals>.select", { py_positional("context"), py_positional("result") }, [_isinstance, _str](arguments a) -> py
        {
            return make_native_generator("select", "_prepare_tag.<locals>.select", w.module, [a = std::vector<py>(a.begin(), a.end()), _isinstance, _str]() -> py
            {
                py context = a[0];
                py result = a[1];
                py elem;
                for (py const& item : iterate{ result })
                {
                    elem = item;
                    if ((*_isinstance)(elem.attr("tag"), (*_str)).truthy())
                    {
                        py_yield(elem);
                    }
                }
                return none();
            });
        });
                    }
                    else if ((*tag) == (S<"{}*">()))
                    {
                        // Any tag that is not in a namespace.
                        select = w.function("select", "_prepare_tag.<locals>.select", { py_positional("context"), py_positional("result") }, [_isinstance, _str](arguments a) -> py
        {
            return make_native_generator("select", "_prepare_tag.<locals>.select", w.module, [a = std::vector<py>(a.begin(), a.end()), _isinstance, _str]() -> py
            {
                py context = a[0];
                py result = a[1];
                py el_tag;
                py elem;
                for (py const& item : iterate{ result })
                {
                    elem = item;
                    el_tag = elem.attr("tag");
                    if (((*_isinstance)(el_tag, (*_str)).truthy() && (el_tag[I<0>()] != (S<"{">()))))
                    {
                        py_yield(elem);
                    }
                }
                return none();
            });
        });
                    }
                    else if ((*tag)[B<"slice">()(none(), I<3>(), none())] == (S<"{*}">()))
                    {
                        // The tag in any (or no) namespace.
                        *suffix = (*tag)[B<"slice">()(I<2>(), none(), none())];
                        *no_ns = (B<"slice">())(-py{ (*suffix).size() }, none());
                        *tag = (*tag)[B<"slice">()(I<3>(), none(), none())];
                        select = w.function("select", "_prepare_tag.<locals>.select", { py_positional("context"), py_positional("result") }, [_isinstance, _str, no_ns, suffix, tag](arguments a) -> py
        {
            return make_native_generator("select", "_prepare_tag.<locals>.select", w.module, [a = std::vector<py>(a.begin(), a.end()), _isinstance, _str, no_ns, suffix, tag]() -> py
            {
                py context = a[0];
                py result = a[1];
                py el_tag;
                py elem;
                for (py const& item : iterate{ result })
                {
                    elem = item;
                    el_tag = elem.attr("tag");
                    if (((el_tag == (*tag)) || ([&]() -> py { py const t = (*_isinstance)(el_tag, (*_str)); return !t.truthy() ? t : boolean(el_tag[(*no_ns)] == (*suffix)); }()).truthy()))
                    {
                        py_yield(elem);
                    }
                }
                return none();
            });
        });
                    }
                    else if ((*tag)[B<"slice">()(I<-2>(), none(), none())] == (S<"}*">()))
                    {
                        // Any tag in the given namespace.
                        *ns = (*tag)[B<"slice">()(none(), I<-1>(), none())];
                        *ns_only = (B<"slice">())(none(), py{ (*ns).size() });
                        select = w.function("select", "_prepare_tag.<locals>.select", { py_positional("context"), py_positional("result") }, [_isinstance, _str, ns, ns_only](arguments a) -> py
        {
            return make_native_generator("select", "_prepare_tag.<locals>.select", w.module, [a = std::vector<py>(a.begin(), a.end()), _isinstance, _str, ns, ns_only]() -> py
            {
                py context = a[0];
                py result = a[1];
                py el_tag;
                py elem;
                for (py const& item : iterate{ result })
                {
                    elem = item;
                    el_tag = elem.attr("tag");
                    if (((*_isinstance)(el_tag, (*_str)).truthy() && (el_tag[(*ns_only)] == (*ns))))
                    {
                        py_yield(elem);
                    }
                }
                return none();
            });
        });
                    }
                    else
                    {
                        py_raise((B<"RuntimeError">())(S(std::string{ "internal parser error, got " } + (*tag).str())));
                    }
                    return select;
                }));

            define("xml.etree.ElementPath", "prepare_child", w.function("prepare_child", "prepare_child", { py_positional("next"), py_positional("token") }, [](arguments a) -> py
                {
                    py next = a[0];
                    py token = a[1];
                    py select;
                    auto const select_tag = std::make_shared<py>();
                    auto const tag = std::make_shared<py>();
                    *tag = token[I<1>()];
                    if ((G<"xml.etree.ElementPath._is_wildcard_tag">())((*tag)).truthy())
                    {
                        *select_tag = (G<"xml.etree.ElementPath._prepare_tag">())((*tag));
                        select = w.function("select", "prepare_child.<locals>.select", { py_positional("context"), py_positional("result") }, [select_tag](arguments a) -> py
        {
            py context = a[0];
            py result = a[1];
            py select_child;
            select_child = w.function("select_child", "prepare_child.<locals>.select.<locals>.select_child", { py_positional("result") }, [](arguments a) -> py
    {
        return make_native_generator("select_child", "prepare_child.<locals>.select.<locals>.select_child", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
        {
            py result = a[0];
            py elem;
            for (py const& item : iterate{ result })
            {
                elem = item;
                yield_from(elem);
            }
            return none();
        });
    });
            return (*select_tag)(context, select_child(result));
        });
                    }
                    else
                    {
                        if ((*tag)[B<"slice">()(none(), I<2>(), none())] == (S<"{}">()))
                        {
                            *tag = (*tag)[B<"slice">()(I<2>(), none(), none())];
                        }
                        select = w.function("select", "prepare_child.<locals>.select", { py_positional("context"), py_positional("result") }, [tag](arguments a) -> py
        {
            return make_native_generator("select", "prepare_child.<locals>.select", w.module, [a = std::vector<py>(a.begin(), a.end()), tag]() -> py
            {
                py context = a[0];
                py result = a[1];
                py e;
                py elem;
                for (py const& item : iterate{ result })
                {
                    elem = item;
                    for (py const& item : iterate{ elem })
                    {
                        e = item;
                        if (e.attr("tag") == (*tag))
                        {
                            py_yield(e);
                        }
                    }
                }
                return none();
            });
        });
                    }
                    return select;
                }));

            define("xml.etree.ElementPath", "prepare_star", w.function("prepare_star", "prepare_star", { py_positional("next"), py_positional("token") }, [](arguments a) -> py
                {
                    py next = a[0];
                    py token = a[1];
                    py select;
                    select = w.function("select", "prepare_star.<locals>.select", { py_positional("context"), py_positional("result") }, [](arguments a) -> py
    {
        return make_native_generator("select", "prepare_star.<locals>.select", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
        {
            py context = a[0];
            py result = a[1];
            py elem;
            for (py const& item : iterate{ result })
            {
                elem = item;
                yield_from(elem);
            }
            return none();
        });
    });
                    return select;
                }));

            define("xml.etree.ElementPath", "prepare_self", w.function("prepare_self", "prepare_self", { py_positional("next"), py_positional("token") }, [](arguments a) -> py
                {
                    py next = a[0];
                    py token = a[1];
                    py select;
                    select = w.function("select", "prepare_self.<locals>.select", { py_positional("context"), py_positional("result") }, [](arguments a) -> py
    {
        return make_native_generator("select", "prepare_self.<locals>.select", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
        {
            py context = a[0];
            py result = a[1];
            yield_from(result);
            return none();
        });
    });
                    return select;
                }));

            define("xml.etree.ElementPath", "prepare_descendant", w.function("prepare_descendant", "prepare_descendant", { py_positional("next"), py_positional("token") }, [](arguments a) -> py
                {
                    py next = a[0];
                    py token = a[1];
                    py select;
                    auto const select_tag = std::make_shared<py>();
                    auto const tag = std::make_shared<py>();
                    try
                    {
                        token = next();
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler17;
                        if (matches(handler17.exception, B<"StopIteration">()))
                        {
                            return none();
                        }
                        else
                            py_reraise(handler17.exception);
                    }
                    if (token[I<0>()] == (S<"*">()))
                    {
                        *tag = S<"*">();
                    }
                    else if (!token[I<0>()].truthy())
                    {
                        *tag = token[I<1>()];
                    }
                    else
                    {
                        py_raise((B<"SyntaxError">())(S<"invalid descendant">()));
                    }
                    if ((G<"xml.etree.ElementPath._is_wildcard_tag">())((*tag)).truthy())
                    {
                        *select_tag = (G<"xml.etree.ElementPath._prepare_tag">())((*tag));
                        select = w.function("select", "prepare_descendant.<locals>.select", { py_positional("context"), py_positional("result") }, [select_tag](arguments a) -> py
        {
            py context = a[0];
            py result = a[1];
            py select_child;
            select_child = w.function("select_child", "prepare_descendant.<locals>.select.<locals>.select_child", { py_positional("result") }, [](arguments a) -> py
    {
        return make_native_generator("select_child", "prepare_descendant.<locals>.select.<locals>.select_child", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
        {
            py result = a[0];
            py e;
            py elem;
            for (py const& item : iterate{ result })
            {
                elem = item;
                for (py const& item : iterate{ elem.method("iter") })
                {
                    e = item;
                    if (!e.is(elem))
                    {
                        py_yield(e);
                    }
                }
            }
            return none();
        });
    });
            return (*select_tag)(context, select_child(result));
        });
                    }
                    else
                    {
                        if ((*tag)[B<"slice">()(none(), I<2>(), none())] == (S<"{}">()))
                        {
                            *tag = (*tag)[B<"slice">()(I<2>(), none(), none())];
                        }
                        select = w.function("select", "prepare_descendant.<locals>.select", { py_positional("context"), py_positional("result") }, [tag](arguments a) -> py
        {
            return make_native_generator("select", "prepare_descendant.<locals>.select", w.module, [a = std::vector<py>(a.begin(), a.end()), tag]() -> py
            {
                py context = a[0];
                py result = a[1];
                py e;
                py elem;
                for (py const& item : iterate{ result })
                {
                    elem = item;
                    for (py const& item : iterate{ elem.method("iter", { (*tag) }) })
                    {
                        e = item;
                        if (!e.is(elem))
                        {
                            py_yield(e);
                        }
                    }
                }
                return none();
            });
        });
                    }
                    return select;
                }));

            define("xml.etree.ElementPath", "prepare_parent", w.function("prepare_parent", "prepare_parent", { py_positional("next"), py_positional("token") }, [](arguments a) -> py
                {
                    py next = a[0];
                    py token = a[1];
                    py select;
                    select = w.function("select", "prepare_parent.<locals>.select", { py_positional("context"), py_positional("result") }, [](arguments a) -> py
    {
        return make_native_generator("select", "prepare_parent.<locals>.select", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
        {
            py context = a[0];
            py result = a[1];
            py elem;
            py parent;
            py parent_map;
            py result_map;
            parent_map = (G<"xml.etree.ElementPath.get_parent_map">())(context);
            result_map = py_dict();
            for (py const& item : iterate{ result })
            {
                elem = item;
                if (parent_map.contains(elem))
                {
                    parent = parent_map[elem];
                    if (!result_map.contains(parent))
                    {
                        result_map.set_item(parent, none());
                        py_yield(parent);
                    }
                }
            }
            return none();
        });
    });
                    return select;
                }));

            define("xml.etree.ElementPath", "prepare_predicate", w.function("prepare_predicate", "prepare_predicate", { py_positional("next"), py_positional("token") }, [](arguments a) -> py
                {
                    py next = a[0];
                    py token = a[1];
                    auto const index = std::make_shared<py>();
                    auto const key = std::make_shared<py>();
                    py predicate;
                    py select;
                    py select_negated;
                    py signature;
                    auto const tag = std::make_shared<py>();
                    auto const value = std::make_shared<py>();
                    signature = py_list();
                    predicate = py_list();
                    while ((I<1>()).truthy())
                    {
                        try
                        {
                            token = next();
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler24;
                            if (matches(handler24.exception, B<"StopIteration">()))
                            {
                                return none();
                            }
                            else
                                py_reraise(handler24.exception);
                        }
                        if (token[I<0>()] == (S<"]">()))
                        {
                            break;
                        }
                        if (token == py_tuple({ S<"">(), S<"">() }))
                        {
                            // ignore whitespace
                            continue;
                        }
                        if ((token[I<0>()].truthy() && (S<"'\"">()).contains(token[I<0>()][B<"slice">()(none(), I<1>(), none())])))
                        {
                            token = py_tuple({ S<"'">(), token[I<0>()][B<"slice">()(I<1>(), I<-1>(), none())] });
                        }
                        signature.method("append", { [&]() -> py { py const t = token[I<0>()]; return t.truthy() ? t : S<"-">(); }() });
                        predicate.method("append", { token[I<1>()] });
                    }
                    signature = (S<"">()).method("join", { signature });
                    // use signature to determine predicate type
                    if (signature == (S<"@-">()))
                    {
                        // [@attribute] predicate
                        *key = predicate[I<1>()];
                        select = w.function("select", "prepare_predicate.<locals>.select", { py_positional("context"), py_positional("result") }, [key](arguments a) -> py
        {
            return make_native_generator("select", "prepare_predicate.<locals>.select", w.module, [a = std::vector<py>(a.begin(), a.end()), key]() -> py
            {
                py context = a[0];
                py result = a[1];
                py elem;
                for (py const& item : iterate{ result })
                {
                    elem = item;
                    if (!elem.method("get", { (*key) }).is_none())
                    {
                        py_yield(elem);
                    }
                }
                return none();
            });
        });
                        return select;
                    }
                    if (((signature == (S<"@-='">())) || (signature == (S<"@-!='">()))))
                    {
                        // [@attribute='value'] or [@attribute!='value']
                        *key = predicate[I<1>()];
                        *value = predicate[I<-1>()];
                        select = w.function("select", "prepare_predicate.<locals>.select", { py_positional("context"), py_positional("result") }, [key, value](arguments a) -> py
        {
            return make_native_generator("select", "prepare_predicate.<locals>.select", w.module, [a = std::vector<py>(a.begin(), a.end()), key, value]() -> py
            {
                py context = a[0];
                py result = a[1];
                py elem;
                for (py const& item : iterate{ result })
                {
                    elem = item;
                    if (elem.method("get", { (*key) }) == (*value))
                    {
                        py_yield(elem);
                    }
                }
                return none();
            });
        });
                        select_negated = w.function("select_negated", "prepare_predicate.<locals>.select_negated", { py_positional("context"), py_positional("result") }, [key, value](arguments a) -> py
        {
            return make_native_generator("select_negated", "prepare_predicate.<locals>.select_negated", w.module, [a = std::vector<py>(a.begin(), a.end()), key, value]() -> py
            {
                py context = a[0];
                py result = a[1];
                py attr_value;
                py elem;
                for (py const& item : iterate{ result })
                {
                    elem = item;
                    if (((!([&]() -> py { attr_value = elem.method("get", { (*key) }); return attr_value; }()).is_none()) && (attr_value != (*value))))
                    {
                        py_yield(elem);
                    }
                }
                return none();
            });
        });
                        return (signature.contains((S<"!=">())) ? select_negated : select);
                    }
                    if (((signature == (S<"-">())) && (!(Mod<"re">().attr("match"))(S<"\\-?\\d+$">(), predicate[I<0>()]).truthy())))
                    {
                        // [tag]
                        *tag = predicate[I<0>()];
                        select = w.function("select", "prepare_predicate.<locals>.select", { py_positional("context"), py_positional("result") }, [tag](arguments a) -> py
        {
            return make_native_generator("select", "prepare_predicate.<locals>.select", w.module, [a = std::vector<py>(a.begin(), a.end()), tag]() -> py
            {
                py context = a[0];
                py result = a[1];
                py elem;
                for (py const& item : iterate{ result })
                {
                    elem = item;
                    if (!elem.method("find", { (*tag) }).is_none())
                    {
                        py_yield(elem);
                    }
                }
                return none();
            });
        });
                        return select;
                    }
                    if (((signature == (S<".='">())) || (signature == (S<".!='">())) || ([&]() -> py { py const t = [&]() -> py { py const t = boolean(signature == (S<"-='">())); return t.truthy() ? t : boolean(signature == (S<"-!='">())); }(); return !t.truthy() ? t : boolean(!(Mod<"re">().attr("match"))(S<"\\-?\\d+$">(), predicate[I<0>()]).truthy()); }()).truthy()))
                    {
                        // [.='value'] or [tag='value'] or [.!='value'] or [tag!='value']
                        *tag = predicate[I<0>()];
                        *value = predicate[I<-1>()];
                        if ((*tag).truthy())
                        {
                            select = w.function("select", "prepare_predicate.<locals>.select", { py_positional("context"), py_positional("result") }, [tag, value](arguments a) -> py
            {
                return make_native_generator("select", "prepare_predicate.<locals>.select", w.module, [a = std::vector<py>(a.begin(), a.end()), tag, value]() -> py
                {
                    py context = a[0];
                    py result = a[1];
                    py e;
                    py elem;
                    for (py const& item : iterate{ result })
                    {
                        elem = item;
                        for (py const& item : iterate{ elem.method("findall", { (*tag) }) })
                        {
                            e = item;
                            if ((S<"">()).method("join", { e.method("itertext") }) == (*value))
                            {
                                py_yield(elem);
                                break;
                            }
                        }
                    }
                    return none();
                });
            });
                            select_negated = w.function("select_negated", "prepare_predicate.<locals>.select_negated", { py_positional("context"), py_positional("result") }, [tag, value](arguments a) -> py
            {
                return make_native_generator("select_negated", "prepare_predicate.<locals>.select_negated", w.module, [a = std::vector<py>(a.begin(), a.end()), tag, value]() -> py
                {
                    py context = a[0];
                    py result = a[1];
                    py e;
                    py elem;
                    for (py const& item : iterate{ result })
                    {
                        elem = item;
                        for (py const& item : iterate{ elem.method("iterfind", { (*tag) }) })
                        {
                            e = item;
                            if ((S<"">()).method("join", { e.method("itertext") }) != (*value))
                            {
                                py_yield(elem);
                                break;
                            }
                        }
                    }
                    return none();
                });
            });
                        }
                        else
                        {
                            select = w.function("select", "prepare_predicate.<locals>.select", { py_positional("context"), py_positional("result") }, [value](arguments a) -> py
            {
                return make_native_generator("select", "prepare_predicate.<locals>.select", w.module, [a = std::vector<py>(a.begin(), a.end()), value]() -> py
                {
                    py context = a[0];
                    py result = a[1];
                    py elem;
                    for (py const& item : iterate{ result })
                    {
                        elem = item;
                        if ((S<"">()).method("join", { elem.method("itertext") }) == (*value))
                        {
                            py_yield(elem);
                        }
                    }
                    return none();
                });
            });
                            select_negated = w.function("select_negated", "prepare_predicate.<locals>.select_negated", { py_positional("context"), py_positional("result") }, [value](arguments a) -> py
            {
                return make_native_generator("select_negated", "prepare_predicate.<locals>.select_negated", w.module, [a = std::vector<py>(a.begin(), a.end()), value]() -> py
                {
                    py context = a[0];
                    py result = a[1];
                    py elem;
                    for (py const& item : iterate{ result })
                    {
                        elem = item;
                        if ((S<"">()).method("join", { elem.method("itertext") }) != (*value))
                        {
                            py_yield(elem);
                        }
                    }
                    return none();
                });
            });
                        }
                        return (signature.contains((S<"!=">())) ? select_negated : select);
                    }
                    if (((signature == (S<"-">())) || (signature == (S<"-()">())) || (signature == (S<"-()-">()))))
                    {
                        // [index] or [last()] or [last()-index]
                        if (signature == (S<"-">()))
                        {
                            // [index]
                            *index = (B<"int">())(predicate[I<0>()]) - (I<1>());
                            if ((*index) < (I<0>()))
                            {
                                py_raise((B<"SyntaxError">())(S<"XPath position >= 1 expected">()));
                            }
                        }
                        else
                        {
                            if (predicate[I<0>()] != (S<"last">()))
                            {
                                py_raise((B<"SyntaxError">())(S<"unsupported function">()));
                            }
                            if (signature == (S<"-()-">()))
                            {
                                try
                                {
                                    *index = (B<"int">())(predicate[I<2>()]) - (I<1>());
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler35;
                                    if (matches(handler35.exception, B<"ValueError">()))
                                    {
                                        py_raise((B<"SyntaxError">())(S<"unsupported expression">()));
                                    }
                                    else
                                        py_reraise(handler35.exception);
                                }
                                if ((*index) > (I<-2>()))
                                {
                                    py_raise((B<"SyntaxError">())(S<"XPath offset from last() must be negative">()));
                                }
                            }
                            else
                            {
                                *index = I<-1>();
                            }
                        }
                        select = w.function("select", "prepare_predicate.<locals>.select", { py_positional("context"), py_positional("result") }, [index](arguments a) -> py
        {
            return make_native_generator("select", "prepare_predicate.<locals>.select", w.module, [a = std::vector<py>(a.begin(), a.end()), index]() -> py
            {
                py context = a[0];
                py result = a[1];
                py cache;
                py elem;
                py elems;
                py key;
                py parent;
                py parent_map;
                parent_map = (G<"xml.etree.ElementPath.get_parent_map">())(context);
                cache = py_dict();
                for (py const& item : iterate{ result })
                {
                    elem = item;
                    try
                    {
                        parent = parent_map[elem];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler37;
                        if (matches(handler37.exception, B<"KeyError">()))
                        {
                            continue;
                        }
                        else
                            py_reraise(handler37.exception);
                    }
                    key = py_tuple({ parent, elem.attr("tag") });
                    if (!cache.contains(key))
                    {
                        // FIXME: what if the selector is "*" ?
                        elems = parent.method("findall", { elem.attr("tag") });
                        try
                        {
                            cache.set_item(key, elems[(*index)]);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler38;
                            if (matches(handler38.exception, B<"IndexError">()))
                            {
                                cache.set_item(key, none());
                            }
                            else
                                py_reraise(handler38.exception);
                        }
                    }
                    if (cache[key].is(elem))
                    {
                        py_yield(elem);
                    }
                }
                return none();
            });
        });
                        return select;
                    }
                    py_raise((B<"SyntaxError">())(S<"invalid predicate">()));
                }));

            define("xml.etree.ElementPath", "ops", dict_of({ { S<"">(), G<"xml.etree.ElementPath.prepare_child">() }, { S<"*">(), G<"xml.etree.ElementPath.prepare_star">() }, { S<".">(), G<"xml.etree.ElementPath.prepare_self">() }, { S<"..">(), G<"xml.etree.ElementPath.prepare_parent">() }, { S<"//">(), G<"xml.etree.ElementPath.prepare_descendant">() }, { S<"[">(), G<"xml.etree.ElementPath.prepare_predicate">() } }));
            define("xml.etree.ElementPath", "_cache", py_dict());
            define("xml.etree.ElementPath", "_SelectorContext", w.make_class("_SelectorContext", {  }, {
                { "parent_map", none() },
                { "__init__", w.method("_SelectorContext", "__init__", { py_positional("root") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py root = a[1];
                        self.set_attr("root", root);
                        return none();
                    }) }, }));

            // --------------------------------------------------------------------
            // #
            // Generate all matching objects.
            define("xml.etree.ElementPath", "iterfind", w.function("iterfind", "iterfind", { py_positional("elem"), py_positional("path"), py_positional("namespaces", none()) }, [](arguments a) -> py
                {
                    py elem = a[0];
                    py path = a[1];
                    py namespaces = a[2];
                    py cache_key;
                    py context;
                    py next;
                    py result;
                    py select;
                    py selector;
                    py token;
                    if (path[B<"slice">()(I<-1>(), none(), none())] == (S<"/">()))
                    {
                        path = path + (S<"*">());
                    }
                    cache_key = py_tuple({ path });
                    if (namespaces.truthy())
                    {
                        cache_key = inplace(NEOS_PY_ADD, cache_key, (B<"tuple">())((B<"sorted">())(namespaces.method("items"))));
                    }
                    try
                    {
                        selector = (G<"xml.etree.ElementPath._cache">())[cache_key];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler39;
                        if (matches(handler39.exception, B<"KeyError">()))
                        {
                            if (py{ (G<"xml.etree.ElementPath._cache">()).size() } > (I<100>()))
                            {
                                (G<"xml.etree.ElementPath._cache">()).method("clear");
                            }
                            if (path[B<"slice">()(none(), I<1>(), none())] == (S<"/">()))
                            {
                                py_raise((B<"SyntaxError">())(S<"cannot use absolute path on element">()));
                            }
                            next = (B<"iter">())((G<"xml.etree.ElementPath.xpath_tokenizer">())(path, namespaces)).attr("__next__");
                            try
                            {
                                token = next();
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler40;
                                if (matches(handler40.exception, B<"StopIteration">()))
                                {
                                    return none();
                                }
                                else
                                    py_reraise(handler40.exception);
                            }
                            selector = py_list();
                            while ((I<1>()).truthy())
                            {
                                try
                                {
                                    selector.method("append", { (G<"xml.etree.ElementPath.ops">())[token[I<0>()]](next, token) });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler42;
                                    if (matches(handler42.exception, B<"StopIteration">()))
                                    {
                                        fn_raise_from((B<"SyntaxError">())(S<"invalid path">()).get(), none().get());
                                        throw raised{};
                                    }
                                    else
                                        py_reraise(handler42.exception);
                                }
                                try
                                {
                                    token = next();
                                    if (token[I<0>()] == (S<"/">()))
                                    {
                                        token = next();
                                    }
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler43;
                                    if (matches(handler43.exception, B<"StopIteration">()))
                                    {
                                        break;
                                    }
                                    else
                                        py_reraise(handler43.exception);
                                }
                            }
                            (G<"xml.etree.ElementPath._cache">()).set_item(cache_key, selector);
                        }
                        else
                            py_reraise(handler39.exception);
                    }
                    // execute selector pattern
                    result = py_list({ elem });
                    context = (G<"xml.etree.ElementPath._SelectorContext">())(elem);
                    for (py const& item : iterate{ selector })
                    {
                        select = item;
                        result = select(context, result);
                    }
                    return result;
                }));

            // #
            // Find first matching object.
            define("xml.etree.ElementPath", "find", w.function("find", "find", { py_positional("elem"), py_positional("path"), py_positional("namespaces", none()) }, [](arguments a) -> py
                {
                    py elem = a[0];
                    py path = a[1];
                    py namespaces = a[2];
                    return (B<"next">())((G<"xml.etree.ElementPath.iterfind">())(elem, path, namespaces), none());
                }));

            // #
            // Find all matching objects.
            define("xml.etree.ElementPath", "findall", w.function("findall", "findall", { py_positional("elem"), py_positional("path"), py_positional("namespaces", none()) }, [](arguments a) -> py
                {
                    py elem = a[0];
                    py path = a[1];
                    py namespaces = a[2];
                    return (B<"list">())((G<"xml.etree.ElementPath.iterfind">())(elem, path, namespaces));
                }));

            // #
            // Find text for first matching object.
            define("xml.etree.ElementPath", "findtext", w.function("findtext", "findtext", { py_positional("elem"), py_positional("path"), py_positional("default", none()), py_positional("namespaces", none()) }, [](arguments a) -> py
                {
                    py elem = a[0];
                    py path = a[1];
                    py default_ = a[2];
                    py namespaces = a[3];
                    try
                    {
                        elem = (B<"next">())((G<"xml.etree.ElementPath.iterfind">())(elem, path, namespaces));
                        if (elem.attr("text").is_none())
                        {
                            return S<"">();
                        }
                        return elem.attr("text");
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler45;
                        if (matches(handler45.exception, B<"StopIteration">()))
                        {
                            return default_;
                        }
                        else
                            py_reraise(handler45.exception);
                    }
                    return none();
                }));
        }

        bool xml_etree_ElementPath_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_xml_etree_ElementPath();
                }
                if (auto const doc = xml_etree_ElementPath_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("xml.etree.ElementPath", xml_etree_ElementPath_module);
    }
}
