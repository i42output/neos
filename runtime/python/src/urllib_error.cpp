/*
  urllib_error.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/urllib/error.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module urllib.error (CPython 3.13's Lib/urllib/error.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "urllib_error_docs.hpp"

namespace neos::python_runtime::detail::urllib_error_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "urllib.error.name"))
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
                py_raise("SystemError", std::string{ "urllib.error: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"urllib.error.name">()))
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
            if (modules_imported("urllib.error"))
                return py{ fn_import("urllib.error") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "urllib.error" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "urllib.error", urllib_error_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- urllib.error (Lib/urllib/error.py)

        void make_urllib_error()
        {
            static module_writer const& w = writer("urllib.error");
            define("urllib.error", "__all__", py_list({ S<"URLError">(), S<"HTTPError">(), S<"ContentTooShortError">() }));
                // URLError is a sub-type of OSError, but it doesn't share any of
                // the implementation.  need to override __init__ and __str__.
                // It sets self.args for compatibility with other OSError
                // subclasses, but args doesn't have the typical format with errno in
                // slot 0 and strerror in slot 1.  This may be better than nothing.
            define("urllib.error", "URLError", w.make_class("URLError", { B<"OSError">() }, {
                { "__init__", w.method("URLError", "__init__", { py_positional("reason"), py_positional("filename", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py reason = a[1];
                        py filename = a[2];
                        self.set_attr("args", py_tuple({ reason }));
                        self.set_attr("reason", reason);
                        if (!filename.is_none())
                        {
                            self.set_attr("filename", filename);
                        }
                        return none();
                    }) },
                { "__str__", w.method("URLError", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"<urlopen error %s>">()) % self.attr("reason");
                    }) }, }));

                // since URLError specifies a .reason attribute, HTTPError should also
                // provide this attribute. See issue13211 for discussion.
            define("urllib.error", "HTTPError", w.make_class("HTTPError", { G<"urllib.error.URLError">(), Mod<"urllib.response">().attr("addinfourl") }, {
                { "_HTTPError__super_init", (Mod<"urllib.response">().attr("addinfourl")).attr("__init__") },
                { "__init__", w.method("HTTPError", "__init__", { py_positional("url"), py_positional("code"), py_positional("msg"), py_positional("hdrs"), py_positional("fp") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py code = a[2];
                        py msg = a[3];
                        py hdrs = a[4];
                        py fp = a[5];
                        self.set_attr("code", code);
                        self.set_attr("msg", msg);
                        self.set_attr("hdrs", hdrs);
                        self.set_attr("fp", fp);
                        self.set_attr("filename", url);
                        if (fp.is_none())
                        {
                            fp = (Mod<"io">().attr("BytesIO"))();
                        }
                        self.method("_HTTPError__super_init", { fp, hdrs, url, code });
                        return none();
                    }) },
                { "__str__", w.method("HTTPError", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"HTTP Error %s: %s">()) % py_tuple({ self.attr("code"), self.attr("msg") });
                    }) },
                { "__repr__", w.method("HTTPError", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"<HTTPError %s: %r>">()) % py_tuple({ self.attr("code"), self.attr("msg") });
                    }) },
                { "reason", B<"property">()(w.method("HTTPError", "reason", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("msg");
                    })) },
                { "headers", B<"property">()(w.method("HTTPError", "headers", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("hdrs");
                    }), w.method("HTTPError", "headers", { py_positional("headers") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py headers = a[1];
                        self.set_attr("hdrs", headers);
                        return none();
                    })) }, }));

            define("urllib.error", "ContentTooShortError", w.make_class("ContentTooShortError", { G<"urllib.error.URLError">() }, {
                { "__init__", w.method("ContentTooShortError", "__init__", { py_positional("message"), py_positional("content") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message = a[1];
                        py content = a[2];
                        (G<"urllib.error.URLError">()).method("__init__", { self, message });
                        self.set_attr("content", content);
                        return none();
                    }) }, }));
        }

        bool urllib_error_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_urllib_error();
                }
                if (auto const doc = urllib_error_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("io", py{ fn_import("io") }.release());
                aAttributes.emplace_back("urllib", py{ fn_import("urllib") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("urllib.error", urllib_error_module);
    }
}
