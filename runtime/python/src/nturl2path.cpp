/*
  nturl2path.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/nturl2path.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module nturl2path (CPython 3.13's Lib/nturl2path.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "nturl2path_docs.hpp"

namespace neos::python_runtime::detail::nturl2path_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "nturl2path.name"))
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
                py_raise("SystemError", std::string{ "nturl2path: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"nturl2path.name">()))
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
            if (modules_imported("nturl2path"))
                return py{ fn_import("nturl2path") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "nturl2path" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "nturl2path", nturl2path_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- nturl2path (Lib/nturl2path.py)

        void make_nturl2path()
        {
            static module_writer const& w = writer("nturl2path");
            // Testing is done through test_urllib.
            define("nturl2path", "url2pathname", w.function("url2pathname", "url2pathname", { py_positional("url") }, [](arguments a) -> py
                {
                    py url = a[0];
                    py comp;
                    py drive;
                    py encoding;
                    py error_;
                    py errors;
                    py string;
                    py sys;
                    py tail;
                    py urllib;
                    // e.g.
                    // ///C|/foo/bar/spam.foo
                    // and
                    // ///C:/foo/bar/spam.foo
                    // become
                    // C:\foo\bar\spam.foo
                    string = module_of("string");
                    sys = module_of("sys");
                    urllib = module_of("urllib");
                    encoding = sys.method("getfilesystemencoding");
                    errors = sys.method("getfilesystemencodeerrors");
                    if (url[B<"slice">()(none(), I<3>(), none())] == (S<"///">()))
                    {
                        // URL has an empty authority section, so the path begins on the third
                        // character.
                        url = url[B<"slice">()(I<2>(), none(), none())];
                    }
                    else if (url[B<"slice">()(none(), I<12>(), none())] == (S<"//localhost/">()))
                    {
                        // Skip past 'localhost' authority.
                        url = url[B<"slice">()(I<11>(), none(), none())];
                    }
                    if (url[B<"slice">()(none(), I<3>(), none())] == (S<"///">()))
                    {
                        // Skip past extra slash before UNC drive in URL path.
                        url = url[B<"slice">()(I<1>(), none(), none())];
                    }
                    // Windows itself uses ":" even in URLs.
                    url = url.method("replace", { S<":">(), S<"|">() });
                    if (!url.contains((S<"|">())))
                    {
                        // No drive specifier, just convert slashes
                        // make sure not to convert quoted slashes :-)
                        return urllib.attr("parse").method("unquote", { url.method("replace", { S<"/">(), S<"\\">() }) }, { { "encoding", encoding }, { "errors", errors } });
                    }
                    comp = url.method("split", { S<"|">() });
                    if (((py{ comp.size() } != (I<2>())) || (!string.attr("ascii_letters").contains(comp[I<0>()][I<-1>()]))))
                    {
                        error_ = (S<"Bad URL: ">()) + url;
                        py_raise((B<"OSError">())(error_));
                    }
                    drive = comp[I<0>()][I<-1>()].method("upper");
                    tail = urllib.attr("parse").method("unquote", { comp[I<1>()].method("replace", { S<"/">(), S<"\\">() }) }, { { "encoding", encoding }, { "errors", errors } });
                    return (drive + (S<":">())) + tail;
                }));

            define("nturl2path", "pathname2url", w.function("pathname2url", "pathname2url", { py_positional("p") }, [](arguments a) -> py
                {
                    py p = a[0];
                    py comp;
                    py drive;
                    py encoding;
                    py error_;
                    py errors;
                    py sys;
                    py tail;
                    py urllib;
                    // e.g.
                    // C:\foo\bar\spam.foo
                    // becomes
                    // ///C:/foo/bar/spam.foo
                    sys = module_of("sys");
                    urllib = module_of("urllib");
                    encoding = sys.method("getfilesystemencoding");
                    errors = sys.method("getfilesystemencodeerrors");
                    // First, clean up some special forms. We are going to sacrifice
                    // the additional information anyway
                    p = p.method("replace", { S<"\\">(), S<"/">() });
                    if (p[B<"slice">()(none(), I<4>(), none())] == (S<"//?/">()))
                    {
                        p = p[B<"slice">()(I<4>(), none(), none())];
                        if (p[B<"slice">()(none(), I<4>(), none())].method("upper") == (S<"UNC/">()))
                        {
                            p = (S<"//">()) + p[B<"slice">()(I<4>(), none(), none())];
                        }
                        else if (p[B<"slice">()(I<1>(), I<2>(), none())] != (S<":">()))
                        {
                            py_raise((B<"OSError">())((S<"Bad path: ">()) + p));
                        }
                    }
                    if (!p.contains((S<":">())))
                    {
                        // No DOS drive specified, just quote the pathname
                        return urllib.attr("parse").method("quote", { p }, { { "encoding", encoding }, { "errors", errors } });
                    }
                    comp = p.method("split", { S<":">() }, { { "maxsplit", I<2>() } });
                    if (((py{ comp.size() } != (I<2>())) || (py{ comp[I<0>()].size() } > (I<1>()))))
                    {
                        error_ = (S<"Bad path: ">()) + p;
                        py_raise((B<"OSError">())(error_));
                    }
                    drive = urllib.attr("parse").method("quote", { comp[I<0>()].method("upper") });
                    tail = urllib.attr("parse").method("quote", { comp[I<1>()] }, { { "encoding", encoding }, { "errors", errors } });
                    return (((S<"///">()) + drive) + (S<":">())) + tail;
                }));
        }

        bool nturl2path_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_nturl2path();
                }
                if (auto const doc = nturl2path_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("nturl2path", nturl2path_module);
    }
}
