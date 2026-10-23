/*
  wsgiref_simple_server.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/wsgiref/simple_server.py), Copyright (c) 2001-2024 Python
  Software Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module wsgiref.simple_server (CPython 3.13's Lib/wsgiref/simple_server.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "wsgiref_simple_server_docs.hpp"

namespace neos::python_runtime::detail::wsgiref_simple_server_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "wsgiref.simple_server.name"))
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
                py_raise("SystemError", std::string{ "wsgiref.simple_server: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"wsgiref.simple_server.name">()))
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
            if (modules_imported("wsgiref.simple_server"))
                return py{ fn_import("wsgiref.simple_server") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "wsgiref.simple_server" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "wsgiref.simple_server", wsgiref_simple_server_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- wsgiref.simple_server (Lib/wsgiref/simple_server.py)

        void make_wsgiref_simple_server()
        {
            static module_writer const& w = writer("wsgiref.simple_server");
            define("wsgiref.simple_server", "__version__", S<"0.2">());
            define("wsgiref.simple_server", "__all__", py_list({ S<"WSGIServer">(), S<"WSGIRequestHandler">(), S<"demo_app">(), S<"make_server">() }));
            define("wsgiref.simple_server", "server_version", (S<"WSGIServer/">()) + (G<"wsgiref.simple_server.__version__">()));
            define("wsgiref.simple_server", "sys_version", ((Mod<"platform">().attr("python_implementation"))() + (S<"/">())) + (Mod<"sys">().attr("version")).method("split")[I<0>()]);
            define("wsgiref.simple_server", "software_version", ((G<"wsgiref.simple_server.server_version">()) + (S<" ">())) + (G<"wsgiref.simple_server.sys_version">()));
            define("wsgiref.simple_server", "ServerHandler", w.make_class("ServerHandler", { Mod<"wsgiref.handlers">().attr("SimpleHandler") }, {
                { "server_software", G<"wsgiref.simple_server.software_version">() },
                { "close", w.method("ServerHandler", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        {
                            std::optional<py> pending1;
                            [[maybe_unused]] int flow1 = 0;
                            [[maybe_unused]] py result1;
                            try
                            {
                                self.attr("request_handler").method("log_request", { self.attr("status").method("split", { S<" ">(), I<1>() })[I<0>()], self.attr("bytes_sent") });
                            }
                            catch (raised const&)
                            {
                                pending1 = py_take_exception();
                            }
                            finally1:
                            (Mod<"wsgiref.handlers">().attr("SimpleHandler")).method("close", { self });
                            if (pending1)
                                py_reraise(*pending1);
                            if (flow1 == 1)
                                return result1;
                        }
                        return none();
                    }) }, }));

            define("wsgiref.simple_server", "WSGIServer", w.make_class("WSGIServer", { Mod<"http.server">().attr("HTTPServer") }, {
                { "application", none() },
                { "server_bind", w.method("WSGIServer", "server_bind", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        (Mod<"http.server">().attr("HTTPServer")).method("server_bind", { self });
                        self.method("setup_environ");
                        return none();
                    }) },
                { "setup_environ", w.method("WSGIServer", "setup_environ", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py env;
                        py const value2 = py_dict();
                        env = value2;
                        self.set_attr("base_environ", value2);
                        env.set_item(S<"SERVER_NAME">(), self.attr("server_name"));
                        env.set_item(S<"GATEWAY_INTERFACE">(), S<"CGI/1.1">());
                        env.set_item(S<"SERVER_PORT">(), (B<"str">())(self.attr("server_port")));
                        env.set_item(S<"REMOTE_HOST">(), S<"">());
                        env.set_item(S<"CONTENT_LENGTH">(), S<"">());
                        env.set_item(S<"SCRIPT_NAME">(), S<"">());
                        return none();
                    }) },
                { "get_app", w.method("WSGIServer", "get_app", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("application");
                    }) },
                { "set_app", w.method("WSGIServer", "set_app", { py_positional("application") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py application = a[1];
                        self.set_attr("application", application);
                        return none();
                    }) }, }));

            define("wsgiref.simple_server", "WSGIRequestHandler", w.make_class("WSGIRequestHandler", { Mod<"http.server">().attr("BaseHTTPRequestHandler") }, {
                { "server_version", (S<"WSGIServer/">()) + (G<"wsgiref.simple_server.__version__">()) },
                { "get_environ", w.method("WSGIRequestHandler", "get_environ", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py env;
                        py k;
                        py length;
                        py path;
                        py query;
                        py v;
                        env = self.attr("server").attr("base_environ").method("copy");
                        env.set_item(S<"SERVER_PROTOCOL">(), self.attr("request_version"));
                        env.set_item(S<"SERVER_SOFTWARE">(), self.attr("server_version"));
                        env.set_item(S<"REQUEST_METHOD">(), self.attr("command"));
                        if (self.attr("path").contains((S<"?">())))
                        {
                            auto const items3 = unpack(self.attr("path").method("split", { S<"?">(), I<1>() }), 2);
                            path = items3[0];
                            query = items3[1];
                        }
                        else
                        {
                            auto const items4 = unpack(py_tuple({ self.attr("path"), S<"">() }), 2);
                            path = items4[0];
                            query = items4[1];
                        }
                        env.set_item(S<"PATH_INFO">(), (Mod<"urllib.parse">().attr("unquote"))(path, S<"iso-8859-1">()));
                        env.set_item(S<"QUERY_STRING">(), query);
                        env.set_item(S<"REMOTE_ADDR">(), self.attr("client_address")[I<0>()]);
                        if (self.attr("headers").method("get", { S<"content-type">() }).is_none())
                        {
                            env.set_item(S<"CONTENT_TYPE">(), self.attr("headers").method("get_content_type"));
                        }
                        else
                        {
                            env.set_item(S<"CONTENT_TYPE">(), self.attr("headers")[S<"content-type">()]);
                        }
                        length = self.attr("headers").method("get", { S<"content-length">() });
                        if (length.truthy())
                        {
                            env.set_item(S<"CONTENT_LENGTH">(), length);
                        }
                        for (py const& item : iterate{ self.attr("headers").method("items") })
                        {
                            auto const items6 = unpack(item, 2);
                            k = items6[0];
                            v = items6[1];
                            k = k.method("replace", { S<"-">(), S<"_">() }).method("upper");
                            v = v.method("strip");
                            if (env.contains(k))
                            {
                                continue;
                            }
                            if (env.contains(((S<"HTTP_">()) + k)))
                            {
                                {
                                    py const target7 = env;
                                    py const key7 = (S<"HTTP_">()) + k;
                                    target7.set_item(key7, inplace(NEOS_PY_ADD, target7[key7], (S<",">()) + v));
                                }
                            }
                            else
                            {
                                env.set_item((S<"HTTP_">()) + k, v);
                            }
                        }
                        return env;
                    }) },
                { "get_stderr", w.method("WSGIRequestHandler", "get_stderr", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return Mod<"sys">().attr("stderr");
                    }) },
                { "handle", w.method("WSGIRequestHandler", "handle", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py handler;
                        self.set_attr("raw_requestline", self.attr("rfile").method("readline", { I<65537>() }));
                        if (py{ self.attr("raw_requestline").size() } > (I<65536>()))
                        {
                            self.set_attr("requestline", S<"">());
                            self.set_attr("request_version", S<"">());
                            self.set_attr("command", S<"">());
                            self.method("send_error", { I<414>() });
                            return none();
                        }
                        if (!self.method("parse_request").truthy())
                        {
                            return none();
                        }
                        handler = (G<"wsgiref.simple_server.ServerHandler">()).call({ self.attr("rfile"), self.attr("wfile"), self.method("get_stderr"), self.method("get_environ") }, { { "multithread", boolean(false) } });
                        handler.set_attr("request_handler", self);
                        handler.method("run", { self.attr("server").method("get_app") });
                        return none();
                    }) }, }));

            define("wsgiref.simple_server", "demo_app", w.function("demo_app", "demo_app", { py_positional("environ"), py_positional("start_response") }, [](arguments a) -> py
                {
                    py environ_ = a[0];
                    py start_response = a[1];
                    py StringIO;
                    py h;
                    py k;
                    py stdout_;
                    py v;
                    StringIO = M("io", "StringIO");
                    stdout_ = StringIO();
                    (B<"print">()).call({ S<"Hello world!">() }, { { "file", stdout_ } });
                    (B<"print">()).call({  }, { { "file", stdout_ } });
                    h = (B<"sorted">())(environ_.method("items"));
                    for (py const& item : iterate{ h })
                    {
                        auto const items9 = unpack(item, 2);
                        k = items9[0];
                        v = items9[1];
                        (B<"print">()).call({ k, S<"=">(), S(v.repr()) }, { { "file", stdout_ } });
                    }
                    start_response(S<"200 OK">(), py_list({ py_tuple({ S<"Content-Type">(), S<"text/plain; charset=utf-8">() }) }));
                    return py_list({ stdout_.method("getvalue").method("encode", { S<"utf-8">() }) });
                }));

            define("wsgiref.simple_server", "make_server", w.function("make_server", "make_server", { py_positional("host"), py_positional("port"), py_positional("app"), py_positional("server_class", G<"wsgiref.simple_server.WSGIServer">()), py_positional("handler_class", G<"wsgiref.simple_server.WSGIRequestHandler">()) }, [](arguments a) -> py
                {
                    py host = a[0];
                    py port = a[1];
                    py app = a[2];
                    py server_class = a[3];
                    py handler_class = a[4];
                    py server;
                    server = server_class(py_tuple({ host, port }), handler_class);
                    server.method("set_app", { app });
                    return server;
                }));

            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool wsgiref_simple_server_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_wsgiref_simple_server();
                }
                if (auto const doc = wsgiref_simple_server_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("BaseHTTPRequestHandler", import_attribute("http.server", "BaseHTTPRequestHandler").release());
                aAttributes.emplace_back("HTTPServer", import_attribute("http.server", "HTTPServer").release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("urllib", py{ fn_import("urllib") }.release());
                aAttributes.emplace_back("SimpleHandler", import_attribute("wsgiref.handlers", "SimpleHandler").release());
                aAttributes.emplace_back("python_implementation", import_attribute("platform", "python_implementation").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("wsgiref.simple_server", wsgiref_simple_server_module);
    }
}
