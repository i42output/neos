/*
  urllib_response.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/urllib/response.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module urllib.response (CPython 3.13's Lib/urllib/response.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "urllib_response_docs.hpp"

namespace neos::python_runtime::detail::urllib_response_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "urllib.response.name"))
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
                py_raise("SystemError", std::string{ "urllib.response: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"urllib.response.name">()))
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
            if (modules_imported("urllib.response"))
                return py{ fn_import("urllib.response") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "urllib.response" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "urllib.response", urllib_response_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- urllib.response (Lib/urllib/response.py)

        void make_urllib_response()
        {
            static module_writer const& w = writer("urllib.response");
            define("urllib.response", "__all__", py_list({ S<"addbase">(), S<"addclosehook">(), S<"addinfo">(), S<"addinfourl">() }));
                // XXX Add a method to expose the timeout on the underlying socket?
            define("urllib.response", "addbase", w.make_class("addbase", { Mod<"tempfile">().attr("_TemporaryFileWrapper") }, {
                { "__init__", w.method("addbase", "__init__", { py_positional("fp") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        (B<"super">())(G<"urllib.response.addbase">(), self).method("__init__", { fp, S<"<urllib response>">() }, { { "delete", boolean(false) } });
                        // Keep reference around as this was part of the original API.
                        self.set_attr("fp", fp);
                        return none();
                    }) },
                { "__repr__", w.method("addbase", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"<%s at %r whose fp = %r>">()) % py_tuple({ self.attr("__class__").attr("__name__"), (B<"id">())(self), self.attr("file") });
                    }) },
                { "__enter__", w.method("addbase", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("fp").attr("closed").truthy())
                        {
                            py_raise((B<"ValueError">())(S<"I/O operation on closed file">()));
                        }
                        return self;
                    }) },
                { "__exit__", w.method("addbase", "__exit__", { py_positional("type"), py_positional("value"), py_positional("traceback") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py type = a[1];
                        py value = a[2];
                        py traceback = a[3];
                        self.method("close");
                        return none();
                    }) }, }));

            define("urllib.response", "addclosehook", w.make_class("addclosehook", { G<"urllib.response.addbase">() }, {
                { "__init__", w.method("addclosehook", "__init__", { py_positional("fp"), py_positional("closehook") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        py closehook = a[2];
                        py hookargs = a[3];
                        (B<"super">())(G<"urllib.response.addclosehook">(), self).method("__init__", { fp });
                        self.set_attr("closehook", closehook);
                        self.set_attr("hookargs", hookargs);
                        return none();
                    }, true) },
                { "close", w.method("addclosehook", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py closehook;
                        py hookargs;
                        {
                            std::optional<py> pending1;
                            [[maybe_unused]] int flow1 = 0;
                            [[maybe_unused]] py result1;
                            try
                            {
                                closehook = self.attr("closehook");
                                hookargs = self.attr("hookargs");
                                if (closehook.truthy())
                                {
                                    self.set_attr("closehook", none());
                                    self.set_attr("hookargs", none());
                                    call_ex(closehook, {  }, hookargs, py{});
                                }
                            }
                            catch (raised const&)
                            {
                                pending1 = py_take_exception();
                            }
                            finally1:
                            (B<"super">())(G<"urllib.response.addclosehook">(), self).method("close");
                            if (pending1)
                                py_reraise(*pending1);
                            if (flow1 == 1)
                                return result1;
                        }
                        return none();
                    }) }, }));

            define("urllib.response", "addinfo", w.make_class("addinfo", { G<"urllib.response.addbase">() }, {
                { "__init__", w.method("addinfo", "__init__", { py_positional("fp"), py_positional("headers") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        py headers = a[2];
                        (B<"super">())(G<"urllib.response.addinfo">(), self).method("__init__", { fp });
                        self.set_attr("headers", headers);
                        return none();
                    }) },
                { "info", w.method("addinfo", "info", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("headers");
                    }) }, }));

            define("urllib.response", "addinfourl", w.make_class("addinfourl", { G<"urllib.response.addinfo">() }, {
                { "__init__", w.method("addinfourl", "__init__", { py_positional("fp"), py_positional("headers"), py_positional("url"), py_positional("code", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        py headers = a[2];
                        py url = a[3];
                        py code = a[4];
                        (B<"super">())(G<"urllib.response.addinfourl">(), self).method("__init__", { fp, headers });
                        self.set_attr("url", url);
                        self.set_attr("code", code);
                        return none();
                    }) },
                { "status", B<"property">()(w.method("addinfourl", "status", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("code");
                    })) },
                { "getcode", w.method("addinfourl", "getcode", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("code");
                    }) },
                { "geturl", w.method("addinfourl", "geturl", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("url");
                    }) }, }));
        }

        bool urllib_response_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_urllib_response();
                }
                if (auto const doc = urllib_response_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("tempfile", py{ fn_import("tempfile") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("urllib.response", urllib_response_module);
    }
}
