/*
  xml_sax__exceptions.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/xml/sax/_exceptions.py), Copyright (c) 2001-2024 Python
  Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module xml.sax._exceptions (CPython 3.13's Lib/xml/sax/_exceptions.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "xml_sax_exceptions_docs.hpp"

namespace neos::python_runtime::detail::xml_sax__exceptions_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "xml.sax._exceptions.name"))
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
                py_raise("SystemError", std::string{ "xml.sax._exceptions: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"xml.sax._exceptions.name">()))
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
            if (modules_imported("xml.sax._exceptions"))
                return py{ fn_import("xml.sax._exceptions") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "xml.sax._exceptions" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "xml.sax._exceptions", xml_sax_exceptions_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- xml.sax._exceptions (Lib/xml/sax/_exceptions.py)

        void make_xml_sax__exceptions()
        {
            static module_writer const& w = writer("xml.sax._exceptions");
            // ===== SAXEXCEPTION =====
            define("xml.sax._exceptions", "SAXException", w.make_class("SAXException", { B<"Exception">() }, {
                { "__init__", w.method("SAXException", "__init__", { py_positional("msg"), py_positional("exception", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py exception = a[2];
                        self.set_attr("_msg", msg);
                        self.set_attr("_exception", exception);
                        (B<"Exception">()).method("__init__", { self, msg });
                        return none();
                    }) },
                { "getMessage", w.method("SAXException", "getMessage", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_msg");
                    }) },
                { "getException", w.method("SAXException", "getException", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_exception");
                    }) },
                { "__str__", w.method("SAXException", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_msg");
                    }) },
                { "__getitem__", w.method("SAXException", "__getitem__", { py_positional("ix") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ix = a[1];
                        py_raise((B<"AttributeError">())(S<"__getitem__">()));
                    }) }, }));

            // ===== SAXPARSEEXCEPTION =====
            define("xml.sax._exceptions", "SAXParseException", w.make_class("SAXParseException", { G<"xml.sax._exceptions.SAXException">() }, {
                { "__init__", w.method("SAXParseException", "__init__", { py_positional("msg"), py_positional("exception"), py_positional("locator") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py exception = a[2];
                        py locator = a[3];
                        (G<"xml.sax._exceptions.SAXException">()).method("__init__", { self, msg, exception });
                        self.set_attr("_locator", locator);
                        // We need to cache this stuff at construction time.
                        // If this exception is raised, the objects through which we must
                        // traverse to get this information may be deleted by the time
                        // it gets caught.
                        self.set_attr("_systemId", self.attr("_locator").method("getSystemId"));
                        self.set_attr("_colnum", self.attr("_locator").method("getColumnNumber"));
                        self.set_attr("_linenum", self.attr("_locator").method("getLineNumber"));
                        return none();
                    }) },
                { "getColumnNumber", w.method("SAXParseException", "getColumnNumber", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_colnum");
                    }) },
                { "getLineNumber", w.method("SAXParseException", "getLineNumber", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_linenum");
                    }) },
                { "getPublicId", w.method("SAXParseException", "getPublicId", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_locator").method("getPublicId");
                    }) },
                { "getSystemId", w.method("SAXParseException", "getSystemId", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_systemId");
                    }) },
                { "__str__", w.method("SAXParseException", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py colnum;
                        py linenum;
                        py sysid;
                        sysid = self.method("getSystemId");
                        if (sysid.is_none())
                        {
                            sysid = S<"<unknown>">();
                        }
                        linenum = self.method("getLineNumber");
                        if (linenum.is_none())
                        {
                            linenum = S<"?">();
                        }
                        colnum = self.method("getColumnNumber");
                        if (colnum.is_none())
                        {
                            colnum = S<"?">();
                        }
                        return (S<"%s:%s:%s: %s">()) % py_tuple({ sysid, linenum, colnum, self.attr("_msg") });
                    }) }, }));

            // ===== SAXNOTRECOGNIZEDEXCEPTION =====
            define("xml.sax._exceptions", "SAXNotRecognizedException", w.make_class("SAXNotRecognizedException", { G<"xml.sax._exceptions.SAXException">() }, {
 }));

            // ===== SAXNOTSUPPORTEDEXCEPTION =====
            define("xml.sax._exceptions", "SAXNotSupportedException", w.make_class("SAXNotSupportedException", { G<"xml.sax._exceptions.SAXException">() }, {
 }));

            // ===== SAXNOTSUPPORTEDEXCEPTION =====
            define("xml.sax._exceptions", "SAXReaderNotAvailable", w.make_class("SAXReaderNotAvailable", { G<"xml.sax._exceptions.SAXNotSupportedException">() }, {
 }));
        }

        bool xml_sax__exceptions_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_xml_sax__exceptions();
                }
                if (auto const doc = xml_sax_exceptions_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("xml.sax._exceptions", xml_sax__exceptions_module);
    }
}
