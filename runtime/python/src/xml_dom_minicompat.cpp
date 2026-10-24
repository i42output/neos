/*
  xml_dom_minicompat.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/xml/dom/minicompat.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module xml.dom.minicompat (CPython 3.13's Lib/xml/dom/minicompat.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "xml_dom_minicompat_docs.hpp"

namespace neos::python_runtime::detail::xml_dom_minicompat_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "xml.dom.minicompat.name"))
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
                py_raise("SystemError", std::string{ "xml.dom.minicompat: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"xml.dom.minicompat.name">()))
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
            if (modules_imported("xml.dom.minicompat"))
                return py{ fn_import("xml.dom.minicompat") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "xml.dom.minicompat" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "xml.dom.minicompat", xml_dom_minicompat_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- xml.dom.minicompat (Lib/xml/dom/minicompat.py)

        void make_xml_dom_minicompat()
        {
            static module_writer const& w = writer("xml.dom.minicompat");
            // This module should only be imported using "import *".
            // 
            // The following names are defined:
            // 
            // NodeList      -- lightest possible NodeList implementation
            // 
            // EmptyNodeList -- lightest possible NodeList that is guaranteed to
            // remain empty (immutable)
            // 
            // StringTypes   -- tuple of defined string types
            // 
            // defproperty   -- function used in conjunction with GetattrMagic;
            // using these together is needed to make them work
            // as efficiently as possible in both Python 2.2+
            // and older versions.  For example:
            // 
            // class MyClass(GetattrMagic):
            // def _get_myattr(self):
            // return something
            // 
            // defproperty(MyClass, "myattr",
            // "return some value")
            // 
            // For Python 2.2 and newer, this will construct a
            // property object on the class, which avoids
            // needing to override __getattr__().  It will only
            // work for read-only attributes.
            // 
            // For older versions of Python, inheriting from
            // GetattrMagic will use the traditional
            // __getattr__() hackery to achieve the same effect,
            // but less efficiently.
            // 
            // defproperty() should be used for each version of
            // the relevant _get_<property>() function.
            define("xml.dom.minicompat", "__all__", py_list({ S<"NodeList">(), S<"EmptyNodeList">(), S<"StringTypes">(), S<"defproperty">() }));
            define("xml.dom.minicompat", "StringTypes", py_tuple({ B<"str">() }));
                // For backward compatibility
            define("xml.dom.minicompat", "NodeList", w.make_class("NodeList", { B<"list">() }, {
                { "__slots__", py_tuple() },
                { "item", w.method("NodeList", "item", { py_positional("index") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py index = a[1];
                        if (((I<0>()) <= index) && (index < py{ self.size() }))
                        {
                            return self[index];
                        }
                        return none();
                    }) },
                { "_get_length", w.method("NodeList", "_get_length", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py{ self.size() };
                    }) },
                { "_set_length", w.method("NodeList", "_set_length", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py_raise((Mod<"xml.dom">().attr("NoModificationAllowedErr"))(S<"attempt to modify read-only attribute 'length'">()));
                    }) },
                { "length", (B<"property">()).call({ w.method("NodeList", "_get_length", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return py{ self.size() };
                    }), w.method("NodeList", "_set_length", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py_raise((Mod<"xml.dom">().attr("NoModificationAllowedErr"))(S<"attempt to modify read-only attribute 'length'">()));
                    }) }, { { "doc", S<"The number of nodes in the NodeList.">() } }) },
                { "__setstate__", w.method("NodeList", "__setstate__", { py_positional("state") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py state = a[1];
                        if (state.is_none())
                        {
                            state = py_list();
                        }
                        self.set_item(B<"slice">()(none(), none(), none()), state);
                        return none();
                    }) }, }));

            define("xml.dom.minicompat", "EmptyNodeList", w.make_class("EmptyNodeList", { B<"tuple">() }, {
                { "__slots__", py_tuple() },
                { "__add__", w.method("EmptyNodeList", "__add__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py NL;
                        NL = (G<"xml.dom.minicompat.NodeList">())();
                        NL.method("extend", { other });
                        return NL;
                    }) },
                { "__radd__", w.method("EmptyNodeList", "__radd__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        py NL;
                        NL = (G<"xml.dom.minicompat.NodeList">())();
                        NL.method("extend", { other });
                        return NL;
                    }) },
                { "item", w.method("EmptyNodeList", "item", { py_positional("index") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py index = a[1];
                        return none();
                    }) },
                { "_get_length", w.method("EmptyNodeList", "_get_length", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return I<0>();
                    }) },
                { "_set_length", w.method("EmptyNodeList", "_set_length", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py_raise((Mod<"xml.dom">().attr("NoModificationAllowedErr"))(S<"attempt to modify read-only attribute 'length'">()));
                    }) },
                { "length", (B<"property">()).call({ w.method("EmptyNodeList", "_get_length", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return I<0>();
                    }), w.method("EmptyNodeList", "_set_length", { py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py_raise((Mod<"xml.dom">().attr("NoModificationAllowedErr"))(S<"attempt to modify read-only attribute 'length'">()));
                    }) }, { { "doc", S<"The number of nodes in the NodeList.">() } }) }, }));

            define("xml.dom.minicompat", "defproperty", w.function("defproperty", "defproperty", { py_positional("klass"), py_positional("name"), py_positional("doc") }, [](arguments a) -> py
                {
                    py klass = a[0];
                    py name = a[1];
                    py doc = a[2];
                    py get;
                    py prop;
                    py set;
                    get = (B<"getattr">())(klass, (S<"_get_">()) + name);
                    set = w.function("set", "defproperty.<locals>.set", { py_positional("self"), py_positional("value"), py_positional("name", name) }, [](arguments a) -> py
    {
        py self = a[0];
        py value = a[1];
        py name = a[2];
        py_raise((Mod<"xml.dom">().attr("NoModificationAllowedErr"))((S<"attempt to modify read-only attribute ">()) + S(name.repr())));
    });
                    if ((B<"hasattr">())(klass, (S<"_set_">()) + name).truthy())
                        py_raise(B<"AssertionError">()((S<"expected not to find _set_">()) + name));
                    prop = (B<"property">()).call({ get, set }, { { "doc", doc } });
                    (B<"setattr">())(klass, name, prop);
                    return none();
                }));
        }

        bool xml_dom_minicompat_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_xml_dom_minicompat();
                }
                if (auto const doc = xml_dom_minicompat_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("xml", py{ fn_import("xml") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("xml.dom.minicompat", xml_dom_minicompat_module);
    }
}
