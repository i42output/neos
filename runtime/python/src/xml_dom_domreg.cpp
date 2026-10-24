/*
  xml_dom_domreg.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/xml/dom/domreg.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module xml.dom.domreg (CPython 3.13's Lib/xml/dom/domreg.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "xml_dom_domreg_docs.hpp"

namespace neos::python_runtime::detail::xml_dom_domreg_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "xml.dom.domreg.name"))
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
                py_raise("SystemError", std::string{ "xml.dom.domreg: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"xml.dom.domreg.name">()))
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
            if (modules_imported("xml.dom.domreg"))
                return py{ fn_import("xml.dom.domreg") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "xml.dom.domreg" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "xml.dom.domreg", xml_dom_domreg_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- xml.dom.domreg (Lib/xml/dom/domreg.py)

        void make_xml_dom_domreg()
        {
            static module_writer const& w = writer("xml.dom.domreg");
            // This is a list of well-known implementations.  Well-known names
            // should be published by posting to xml-sig@python.org, and are
            // subsequently recorded in this file.
            define("xml.dom.domreg", "well_known_implementations", dict_of({ { S<"minidom">(), S<"xml.dom.minidom">() }, { S<"4DOM">(), S<"xml.dom.DOMImplementation">() } }));
            // DOM implementations not officially registered should register
            // themselves with their
            define("xml.dom.domreg", "registered", py_dict());
            define("xml.dom.domreg", "registerDOMImplementation", w.function("registerDOMImplementation", "registerDOMImplementation", { py_positional("name"), py_positional("factory") }, [](arguments a) -> py
                {
                    py name = a[0];
                    py factory = a[1];
                    (G<"xml.dom.domreg.registered">()).set_item(name, factory);
                    return none();
                }));

            define("xml.dom.domreg", "_good_enough", w.function("_good_enough", "_good_enough", { py_positional("dom"), py_positional("features") }, [](arguments a) -> py
                {
                    py dom = a[0];
                    py features = a[1];
                    py f;
                    py v;
                    for (py const& item : iterate{ features })
                    {
                        auto const items2 = unpack(item, 2);
                        f = items2[0];
                        v = items2[1];
                        if (!dom.method("hasFeature", { f, v }).truthy())
                        {
                            return I<0>();
                        }
                    }
                    return I<1>();
                }));

            define("xml.dom.domreg", "getDOMImplementation", w.function("getDOMImplementation", "getDOMImplementation", { py_positional("name", none()), py_positional("features", py_tuple()) }, [](arguments a) -> py
                {
                    py name = a[0];
                    py features = a[1];
                    py creator;
                    py dom;
                    py mod;
                    py os;
                    os = module_of("os");
                    creator = none();
                    mod = (G<"xml.dom.domreg.well_known_implementations">()).method("get", { name });
                    if (mod.truthy())
                    {
                        mod = (B<"__import__">())(mod, py_dict(), py_dict(), py_list({ S<"getDOMImplementation">() }));
                        return mod.method("getDOMImplementation");
                    }
                    else if (name.truthy())
                    {
                        return (G<"xml.dom.domreg.registered">())[name]();
                    }
                    else if (((!(Mod<"sys">().attr("flags")).attr("ignore_environment").truthy()) && os.attr("environ").contains((S<"PYTHON_DOM">()))))
                    {
                        return (G<"xml.dom.domreg.getDOMImplementation">()).call({  }, { { "name", os.attr("environ")[S<"PYTHON_DOM">()] } });
                    }
                    // User did not specify a name, try implementations in arbitrary
                    // order, returning the one that has the required features
                    if (isinstance(features, B<"str">()))
                    {
                        features = (G<"xml.dom.domreg._parse_feature_string">())(features);
                    }
                    for (py const& item : iterate{ (G<"xml.dom.domreg.registered">()).method("values") })
                    {
                        creator = item;
                        dom = creator();
                        if ((G<"xml.dom.domreg._good_enough">())(dom, features).truthy())
                        {
                            return dom;
                        }
                    }
                    for (py const& item : iterate{ (G<"xml.dom.domreg.well_known_implementations">()).method("keys") })
                    {
                        creator = item;
                        try
                        {
                            dom = (G<"xml.dom.domreg.getDOMImplementation">()).call({  }, { { "name", creator } });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler5;
                            if (matches(handler5.exception, B<"Exception">()))
                            {
                                continue;
                            }
                            else
                                py_reraise(handler5.exception);
                        }
                        if ((G<"xml.dom.domreg._good_enough">())(dom, features).truthy())
                        {
                            return dom;
                        }
                    }
                    py_raise((B<"ImportError">())(S<"no suitable DOM implementation found">()));
                }));

            define("xml.dom.domreg", "_parse_feature_string", w.function("_parse_feature_string", "_parse_feature_string", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    py feature;
                    py features;
                    py i;
                    py length;
                    py parts;
                    py v;
                    py version;
                    features = py_list();
                    parts = s.method("split");
                    i = I<0>();
                    length = py{ parts.size() };
                    while (i < length)
                    {
                        feature = parts[i];
                        if ((S<"0123456789">()).contains(feature[I<0>()]))
                        {
                            py_raise((B<"ValueError">())((S<"bad feature name: %r">()) % py_tuple({ feature })));
                        }
                        i = i + (I<1>());
                        version = none();
                        if (i < length)
                        {
                            v = parts[i];
                            if ((S<"0123456789">()).contains(v[I<0>()]))
                            {
                                i = i + (I<1>());
                                version = v;
                            }
                        }
                        features.method("append", { py_tuple({ feature, version }) });
                    }
                    return (B<"tuple">())(features);
                }));
        }

        bool xml_dom_domreg_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_xml_dom_domreg();
                }
                if (auto const doc = xml_dom_domreg_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("xml.dom.domreg", xml_dom_domreg_module);
    }
}
