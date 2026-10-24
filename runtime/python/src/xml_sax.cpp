/*
  xml_sax.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/xml/sax/__init__.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module xml.sax (CPython 3.13's Lib/xml/sax/__init__.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "xml_sax_docs.hpp"

namespace neos::python_runtime::detail::xml_sax_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "xml.sax.name"))
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
                py_raise("SystemError", std::string{ "xml.sax: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"xml.sax.name">()))
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
            if (modules_imported("xml.sax"))
                return py{ fn_import("xml.sax") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "xml.sax" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "xml.sax", xml_sax_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- xml.sax (Lib/xml/sax/__init__.py)

        void make_xml_sax()
        {
            static module_writer const& w = writer("xml.sax");
            define("xml.sax", "parse", w.function("parse", "parse", { py_positional("source"), py_positional("handler"), py_positional("errorHandler", (Mod<"xml.sax.handler">().attr("ErrorHandler"))()) }, [](arguments a) -> py
                {
                    py source = a[0];
                    py handler = a[1];
                    py errorHandler = a[2];
                    py parser;
                    parser = (G<"xml.sax.make_parser">())();
                    parser.method("setContentHandler", { handler });
                    parser.method("setErrorHandler", { errorHandler });
                    parser.method("parse", { source });
                    return none();
                }));

            define("xml.sax", "parseString", w.function("parseString", "parseString", { py_positional("string"), py_positional("handler"), py_positional("errorHandler", (Mod<"xml.sax.handler">().attr("ErrorHandler"))()) }, [](arguments a) -> py
                {
                    py string = a[0];
                    py handler = a[1];
                    py errorHandler = a[2];
                    py inpsrc;
                    py io;
                    py parser;
                    io = module_of("io");
                    if (errorHandler.is_none())
                    {
                        errorHandler = (Mod<"xml.sax.handler">().attr("ErrorHandler"))();
                    }
                    parser = (G<"xml.sax.make_parser">())();
                    parser.method("setContentHandler", { handler });
                    parser.method("setErrorHandler", { errorHandler });
                    inpsrc = (Mod<"xml.sax.xmlreader">().attr("InputSource"))();
                    if (isinstance(string, B<"str">()))
                    {
                        inpsrc.method("setCharacterStream", { io.method("StringIO", { string }) });
                    }
                    else
                    {
                        inpsrc.method("setByteStream", { io.method("BytesIO", { string }) });
                    }
                    parser.method("parse", { inpsrc });
                    return none();
                }));

            // this is the parser list used by the make_parser function if no
            // alternatives are given as parameters to the function
            define("xml.sax", "default_parser_list", py_list({ S<"xml.sax.expatreader">() }));
            // tell modulefinder that importing sax potentially imports expatreader
            define("xml.sax", "_false", I<0>());
            if ((G<"xml.sax._false">()).truthy())
            {
            }
            if (((!(Mod<"sys">().attr("flags")).attr("ignore_environment").truthy()) && (Mod<"os">().attr("environ")).contains((S<"PY_SAX_PARSER">()))))
            {
                define("xml.sax", "default_parser_list", (Mod<"os">().attr("environ"))[S<"PY_SAX_PARSER">()].method("split", { S<",">() }));
            }
            undefine("xml.sax", "os");
            undefine("xml.sax", "sys");
            define("xml.sax", "make_parser", w.function("make_parser", "make_parser", { py_positional("parser_list", py_tuple()) }, [](arguments a) -> py
                {
                    py parser_list = a[0];
                    py parser_name;
                    py sys;
                    for (py const& item : iterate{ (B<"list">())(parser_list) + (G<"xml.sax.default_parser_list">()) })
                    {
                        parser_name = item;
                        try
                        {
                            return (G<"xml.sax._create_parser">())(parser_name);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler2;
                            if (matches(handler2.exception, B<"ImportError">()))
                            {
                                sys = module_of("sys");
                                if (sys.attr("modules").contains(parser_name))
                                {
                                    // The parser module was found, but importing it
                                    // failed unexpectedly, pass this exception through
                                    py_reraise(handler2.exception);
                                }
                            }
                            else if (matches(handler2.exception, Mod<"xml.sax._exceptions">().attr("SAXReaderNotAvailable")))
                            {
                                // The parser module detected that it won't work properly,
                                // so try the next one
                            }
                            else
                                py_reraise(handler2.exception);
                        }
                    }
                    py_raise((Mod<"xml.sax._exceptions">().attr("SAXReaderNotAvailable"))(S<"No parsers found">(), none()));
                }));

            // --- Internal utility methods used by make_parser
            define("xml.sax", "_create_parser", w.function("_create_parser", "_create_parser", { py_positional("parser_name") }, [](arguments a) -> py
                {
                    py parser_name = a[0];
                    py drv_module;
                    drv_module = (B<"__import__">())(parser_name, py_dict(), py_dict(), py_list({ S<"create_parser">() }));
                    return drv_module.method("create_parser");
                }));
        }

        bool xml_sax_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_xml_sax();
                }
                if (auto const doc = xml_sax_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("InputSource", import_attribute("xml.sax.xmlreader", "InputSource").release());
                aAttributes.emplace_back("ContentHandler", import_attribute("xml.sax.handler", "ContentHandler").release());
                aAttributes.emplace_back("ErrorHandler", import_attribute("xml.sax.handler", "ErrorHandler").release());
                aAttributes.emplace_back("SAXException", import_attribute("xml.sax._exceptions", "SAXException").release());
                aAttributes.emplace_back("SAXNotRecognizedException", import_attribute("xml.sax._exceptions", "SAXNotRecognizedException").release());
                aAttributes.emplace_back("SAXParseException", import_attribute("xml.sax._exceptions", "SAXParseException").release());
                aAttributes.emplace_back("SAXNotSupportedException", import_attribute("xml.sax._exceptions", "SAXNotSupportedException").release());
                aAttributes.emplace_back("SAXReaderNotAvailable", import_attribute("xml.sax._exceptions", "SAXReaderNotAvailable").release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("xml.sax", xml_sax_module);
    }
}
