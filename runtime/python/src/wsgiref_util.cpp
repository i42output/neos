/*
  wsgiref_util.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/wsgiref/util.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module wsgiref.util (CPython 3.13's Lib/wsgiref/util.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "wsgiref_util_docs.hpp"

namespace neos::python_runtime::detail::wsgiref_util_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "wsgiref.util.name"))
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
                py_raise("SystemError", std::string{ "wsgiref.util: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"wsgiref.util.name">()))
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
            if (modules_imported("wsgiref.util"))
                return py{ fn_import("wsgiref.util") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "wsgiref.util" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "wsgiref.util", wsgiref_util_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- wsgiref.util (Lib/wsgiref/util.py)

        void make_wsgiref_util()
        {
            static module_writer const& w = writer("wsgiref.util");
            define("wsgiref.util", "__all__", py_list({ S<"FileWrapper">(), S<"guess_scheme">(), S<"application_uri">(), S<"request_uri">(), S<"shift_path_info">(), S<"setup_testing_defaults">(), S<"is_hop_by_hop">() }));
            define("wsgiref.util", "FileWrapper", w.make_class("FileWrapper", {  }, {
                { "__init__", w.method("FileWrapper", "__init__", { py_positional("filelike"), py_positional("blksize", I<8192>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filelike = a[1];
                        py blksize = a[2];
                        self.set_attr("filelike", filelike);
                        self.set_attr("blksize", blksize);
                        if (has_attr(filelike, "close"))
                        {
                            self.set_attr("close", filelike.attr("close"));
                        }
                        return none();
                    }) },
                { "__iter__", w.method("FileWrapper", "__iter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__next__", w.method("FileWrapper", "__next__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data;
                        data = self.attr("filelike").method("read", { self.attr("blksize") });
                        if (data.truthy())
                        {
                            return data;
                        }
                        py_raise(B<"StopIteration">());
                    }) }, }));

            define("wsgiref.util", "guess_scheme", w.function("guess_scheme", "guess_scheme", { py_positional("environ") }, [](arguments a) -> py
                {
                    py environ_ = a[0];
                    if (py_tuple({ S<"yes">(), S<"on">(), S<"1">() }).contains(environ_.method("get", { S<"HTTPS">() })))
                    {
                        return S<"https">();
                    }
                    else
                    {
                        return S<"http">();
                    }
                    return none();
                }));

            define("wsgiref.util", "application_uri", w.function("application_uri", "application_uri", { py_positional("environ") }, [](arguments a) -> py
                {
                    py environ_ = a[0];
                    py quote;
                    py url;
                    url = environ_[S<"wsgi.url_scheme">()] + (S<"://">());
                    quote = M("urllib.parse", "quote");
                    if (environ_.method("get", { S<"HTTP_HOST">() }).truthy())
                    {
                        url = inplace(NEOS_PY_ADD, url, environ_[S<"HTTP_HOST">()]);
                    }
                    else
                    {
                        url = inplace(NEOS_PY_ADD, url, environ_[S<"SERVER_NAME">()]);
                        if (environ_[S<"wsgi.url_scheme">()] == (S<"https">()))
                        {
                            if (environ_[S<"SERVER_PORT">()] != (S<"443">()))
                            {
                                url = inplace(NEOS_PY_ADD, url, (S<":">()) + environ_[S<"SERVER_PORT">()]);
                            }
                        }
                        else if (environ_[S<"SERVER_PORT">()] != (S<"80">()))
                        {
                            url = inplace(NEOS_PY_ADD, url, (S<":">()) + environ_[S<"SERVER_PORT">()]);
                        }
                    }
                    url = inplace(NEOS_PY_ADD, url, quote.call({ [&]() -> py { py const t = environ_.method("get", { S<"SCRIPT_NAME">() }); return t.truthy() ? t : S<"/">(); }() }, { { "encoding", S<"latin1">() } }));
                    return url;
                }));

            define("wsgiref.util", "request_uri", w.function("request_uri", "request_uri", { py_positional("environ"), py_positional("include_query", boolean(true)) }, [](arguments a) -> py
                {
                    py environ_ = a[0];
                    py include_query = a[1];
                    py path_info;
                    py quote;
                    py url;
                    url = (G<"wsgiref.util.application_uri">())(environ_);
                    quote = M("urllib.parse", "quote");
                    path_info = quote.call({ environ_.method("get", { S<"PATH_INFO">(), S<"">() }) }, { { "safe", S<"/;=,">() }, { "encoding", S<"latin1">() } });
                    if (!environ_.method("get", { S<"SCRIPT_NAME">() }).truthy())
                    {
                        url = inplace(NEOS_PY_ADD, url, path_info[B<"slice">()(I<1>(), none(), none())]);
                    }
                    else
                    {
                        url = inplace(NEOS_PY_ADD, url, path_info);
                    }
                    if ((include_query.truthy() && environ_.method("get", { S<"QUERY_STRING">() }).truthy()))
                    {
                        url = inplace(NEOS_PY_ADD, url, (S<"?">()) + environ_[S<"QUERY_STRING">()]);
                    }
                    return url;
                }));

            define("wsgiref.util", "shift_path_info", w.function("shift_path_info", "shift_path_info", { py_positional("environ") }, [](arguments a) -> py
                {
                    py environ_ = a[0];
                    py name;
                    py path_info;
                    py path_parts;
                    py script_name;
                    path_info = environ_.method("get", { S<"PATH_INFO">(), S<"">() });
                    if (!path_info.truthy())
                    {
                        return none();
                    }
                    path_parts = path_info.method("split", { S<"/">() });
                    path_parts.set_item(B<"slice">()(I<1>(), I<-1>(), none()), [&]() -> py { py const comprehension1 = py_list(); py p; for (py const& item : iterate{ path_parts[B<"slice">()(I<1>(), I<-1>(), none())] }) { p = item; if (!([&]() -> py { py const t = p; return !t.truthy() ? t : boolean(p != (S<".">())); }()).truthy()) continue; comprehension1.method("append", { p }); } return comprehension1; }());
                    name = path_parts[I<1>()];
                    del_item(path_parts, I<1>());
                    script_name = environ_.method("get", { S<"SCRIPT_NAME">(), S<"">() });
                    script_name = (Mod<"posixpath">().attr("normpath"))((script_name + (S<"/">())) + name);
                    if (script_name.method("endswith", { S<"/">() }).truthy())
                    {
                        script_name = script_name[B<"slice">()(none(), I<-1>(), none())];
                    }
                    if (((!name.truthy()) && (!script_name.method("endswith", { S<"/">() }).truthy())))
                    {
                        script_name = inplace(NEOS_PY_ADD, script_name, S<"/">());
                    }
                    environ_.set_item(S<"SCRIPT_NAME">(), script_name);
                    environ_.set_item(S<"PATH_INFO">(), (S<"/">()).method("join", { path_parts }));
                    // Special case: '/.' on PATH_INFO doesn't get stripped,
                    // because we don't strip the last element of PATH_INFO
                    // if there's only one path part left.  Instead of fixing this
                    // above, we fix it here so that PATH_INFO gets normalized to
                    // an empty string in the environ.
                    if (name == (S<".">()))
                    {
                        name = none();
                    }
                    return name;
                }));

            define("wsgiref.util", "setup_testing_defaults", w.function("setup_testing_defaults", "setup_testing_defaults", { py_positional("environ") }, [](arguments a) -> py
                {
                    py environ_ = a[0];
                    py BytesIO;
                    py StringIO;
                    environ_.method("setdefault", { S<"SERVER_NAME">(), S<"127.0.0.1">() });
                    environ_.method("setdefault", { S<"SERVER_PROTOCOL">(), S<"HTTP/1.0">() });
                    environ_.method("setdefault", { S<"HTTP_HOST">(), environ_[S<"SERVER_NAME">()] });
                    environ_.method("setdefault", { S<"REQUEST_METHOD">(), S<"GET">() });
                    if (((!environ_.contains((S<"SCRIPT_NAME">()))) && (!environ_.contains((S<"PATH_INFO">())))))
                    {
                        environ_.method("setdefault", { S<"SCRIPT_NAME">(), S<"">() });
                        environ_.method("setdefault", { S<"PATH_INFO">(), S<"/">() });
                    }
                    environ_.method("setdefault", { S<"wsgi.version">(), py_tuple({ I<1>(), I<0>() }) });
                    environ_.method("setdefault", { S<"wsgi.run_once">(), I<0>() });
                    environ_.method("setdefault", { S<"wsgi.multithread">(), I<0>() });
                    environ_.method("setdefault", { S<"wsgi.multiprocess">(), I<0>() });
                    StringIO = M("io", "StringIO");
                    BytesIO = M("io", "BytesIO");
                    environ_.method("setdefault", { S<"wsgi.input">(), BytesIO() });
                    environ_.method("setdefault", { S<"wsgi.errors">(), StringIO() });
                    environ_.method("setdefault", { S<"wsgi.url_scheme">(), (G<"wsgiref.util.guess_scheme">())(environ_) });
                    if (environ_[S<"wsgi.url_scheme">()] == (S<"http">()))
                    {
                        environ_.method("setdefault", { S<"SERVER_PORT">(), S<"80">() });
                    }
                    else if (environ_[S<"wsgi.url_scheme">()] == (S<"https">()))
                    {
                        environ_.method("setdefault", { S<"SERVER_PORT">(), S<"443">() });
                    }
                    return none();
                }));

            define("wsgiref.util", "_hoppish", set_of({ S<"connection">(), S<"keep-alive">(), S<"proxy-authenticate">(), S<"proxy-authorization">(), S<"te">(), S<"trailers">(), S<"transfer-encoding">(), S<"upgrade">() }).attr("__contains__"));
            define("wsgiref.util", "is_hop_by_hop", w.function("is_hop_by_hop", "is_hop_by_hop", { py_positional("header_name") }, [](arguments a) -> py
                {
                    py header_name = a[0];
                    return (G<"wsgiref.util._hoppish">())(header_name.method("lower"));
                }));
        }

        bool wsgiref_util_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_wsgiref_util();
                }
                if (auto const doc = wsgiref_util_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("posixpath", py{ fn_import("posixpath") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("wsgiref.util", wsgiref_util_module);
    }
}
