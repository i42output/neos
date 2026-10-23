/*
  wsgiref_types.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/wsgiref/types.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module wsgiref.types (CPython 3.13's Lib/wsgiref/types.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "wsgiref_types_docs.hpp"

namespace neos::python_runtime::detail::wsgiref_types_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "wsgiref.types.name"))
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
                py_raise("SystemError", std::string{ "wsgiref.types: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"wsgiref.types.name">()))
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
            if (modules_imported("wsgiref.types"))
                return py{ fn_import("wsgiref.types") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "wsgiref.types" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "wsgiref.types", wsgiref_types_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- wsgiref.types (Lib/wsgiref/types.py)

        void make_wsgiref_types()
        {
            static module_writer const& w = writer("wsgiref.types");
            define("wsgiref.types", "__all__", py_list({ S<"StartResponse">(), S<"WSGIEnvironment">(), S<"WSGIApplication">(), S<"InputStream">(), S<"ErrorStream">(), S<"FileWrapper">() }));
            define("wsgiref.types", "_ExcInfo", (B<"tuple">())[py_tuple({ (B<"type">())[B<"BaseException">()], B<"BaseException">(), Mod<"types">().attr("TracebackType") })]);
            define("wsgiref.types", "_OptExcInfo", (G<"wsgiref.types._ExcInfo">()) | (B<"tuple">())[py_tuple({ none(), none(), none() })]);
            define("wsgiref.types", "StartResponse", w.make_class("StartResponse", { Mod<"typing">().attr("Protocol") }, {
                { "__call__", w.method("StartResponse", "__call__", { py_positional_only("status"), py_positional_only("headers"), py_positional_only("exc_info", B<"Ellipsis">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py status = a[1];
                        py headers = a[2];
                        py exc_info = a[3];
                        return none();
                    }) }, }));

            define("wsgiref.types", "WSGIEnvironment", (B<"dict">())[py_tuple({ B<"str">(), Mod<"typing">().attr("Any") })]);
            define("wsgiref.types", "WSGIApplication", (Mod<"collections.abc">().attr("Callable"))[py_tuple({ py_list({ G<"wsgiref.types.WSGIEnvironment">(), G<"wsgiref.types.StartResponse">() }), (Mod<"collections.abc">().attr("Iterable"))[B<"bytes">()] })]);
            define("wsgiref.types", "InputStream", w.make_class("InputStream", { Mod<"typing">().attr("Protocol") }, {
                { "read", w.method("InputStream", "read", { py_positional_only("size", B<"Ellipsis">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        return none();
                    }) },
                { "readline", w.method("InputStream", "readline", { py_positional_only("size", B<"Ellipsis">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        return none();
                    }) },
                { "readlines", w.method("InputStream", "readlines", { py_positional_only("hint", B<"Ellipsis">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py hint = a[1];
                        return none();
                    }) },
                { "__iter__", w.method("InputStream", "__iter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) }, }));

            define("wsgiref.types", "ErrorStream", w.make_class("ErrorStream", { Mod<"typing">().attr("Protocol") }, {
                { "flush", w.method("ErrorStream", "flush", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "write", w.method("ErrorStream", "write", { py_positional_only("s") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s = a[1];
                        return none();
                    }) },
                { "writelines", w.method("ErrorStream", "writelines", { py_positional_only("seq") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py seq = a[1];
                        return none();
                    }) }, }));

            define("wsgiref.types", "_Readable", w.make_class("_Readable", { Mod<"typing">().attr("Protocol") }, {
                { "read", w.method("_Readable", "read", { py_positional_only("size", B<"Ellipsis">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        return none();
                    }) }, }));

            // Optional: def close(self) -> object: ...
            define("wsgiref.types", "FileWrapper", w.make_class("FileWrapper", { Mod<"typing">().attr("Protocol") }, {
                { "__call__", w.method("FileWrapper", "__call__", { py_positional_only("file"), py_positional_only("block_size", B<"Ellipsis">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        py block_size = a[2];
                        return none();
                    }) }, }));
        }

        bool wsgiref_types_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_wsgiref_types();
                }
                if (auto const doc = wsgiref_types_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("Callable", import_attribute("collections.abc", "Callable").release());
                aAttributes.emplace_back("Iterable", import_attribute("collections.abc", "Iterable").release());
                aAttributes.emplace_back("Iterator", import_attribute("collections.abc", "Iterator").release());
                aAttributes.emplace_back("TracebackType", import_attribute("types", "TracebackType").release());
                aAttributes.emplace_back("Any", import_attribute("typing", "Any").release());
                aAttributes.emplace_back("Protocol", import_attribute("typing", "Protocol").release());
                aAttributes.emplace_back("TypeAlias", import_attribute("typing", "TypeAlias").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("wsgiref.types", wsgiref_types_module);
    }
}
