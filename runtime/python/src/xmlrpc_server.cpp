/*
  xmlrpc_server.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/xmlrpc/server.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module xmlrpc.server (CPython 3.13's Lib/xmlrpc/server.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "xmlrpc_server_docs.hpp"

namespace neos::python_runtime::detail::xmlrpc_server_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "xmlrpc.server.name"))
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
                py_raise("SystemError", std::string{ "xmlrpc.server: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"xmlrpc.server.name">()))
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
            if (modules_imported("xmlrpc.server"))
                return py{ fn_import("xmlrpc.server") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "xmlrpc.server" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "xmlrpc.server", xmlrpc_server_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- xmlrpc.server (Lib/xmlrpc/server.py)

        void make_xmlrpc_server()
        {
            static module_writer const& w = writer("xmlrpc.server");
            // Written by Brian Quinlan (brian@sweetapp.com).
            // Based on code written by Fredrik Lundh.
            define("xmlrpc.server", "resolve_dotted_attribute", w.function("resolve_dotted_attribute", "resolve_dotted_attribute", { py_positional("obj"), py_positional("attr"), py_positional("allow_dotted_names", boolean(true)) }, [](arguments a) -> py
                {
                    py obj = a[0];
                    py attr = a[1];
                    py allow_dotted_names = a[2];
                    py attrs;
                    py i;
                    if (allow_dotted_names.truthy())
                    {
                        attrs = attr.method("split", { S<".">() });
                    }
                    else
                    {
                        attrs = py_list({ attr });
                    }
                    for (py const& item : iterate{ attrs })
                    {
                        i = item;
                        if (i.method("startswith", { S<"_">() }).truthy())
                        {
                            py_raise((B<"AttributeError">())((S<"attempt to access private attribute \"%s\"">()) % i));
                        }
                        else
                        {
                            obj = (B<"getattr">())(obj, i);
                        }
                    }
                    return obj;
                }));

            define("xmlrpc.server", "list_public_methods", w.function("list_public_methods", "list_public_methods", { py_positional("obj") }, [](arguments a) -> py
                {
                    py obj = a[0];
                    return [&]() -> py { py const comprehension2 = py_list(); py member; for (py const& item : iterate{ (B<"dir">())(obj) }) { member = item; if (!([&]() -> py { py const t = boolean(!member.method("startswith", { S<"_">() }).truthy()); return !t.truthy() ? t : boolean(callable((B<"getattr">())(obj, member))); }()).truthy()) continue; comprehension2.method("append", { member }); } return comprehension2; }();
                }));

            define("xmlrpc.server", "SimpleXMLRPCDispatcher", w.make_class("SimpleXMLRPCDispatcher", {  }, {
                { "__init__", w.method("SimpleXMLRPCDispatcher", "__init__", { py_positional("allow_none", boolean(false)), py_positional("encoding", none()), py_positional("use_builtin_types", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py allow_none = a[1];
                        py encoding = a[2];
                        py use_builtin_types = a[3];
                        self.set_attr("funcs", py_dict());
                        self.set_attr("instance", none());
                        self.set_attr("allow_none", allow_none);
                        self.set_attr("encoding", [&]() -> py { py const t = encoding; return t.truthy() ? t : S<"utf-8">(); }());
                        self.set_attr("use_builtin_types", use_builtin_types);
                        return none();
                    }) },
                { "register_instance", w.method("SimpleXMLRPCDispatcher", "register_instance", { py_positional("instance"), py_positional("allow_dotted_names", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py instance = a[1];
                        py allow_dotted_names = a[2];
                        self.set_attr("instance", instance);
                        self.set_attr("allow_dotted_names", allow_dotted_names);
                        return none();
                    }) },
                { "register_function", w.method("SimpleXMLRPCDispatcher", "register_function", { py_positional("function", none()), py_positional("name", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py function = a[1];
                        py name = a[2];
                        // decorator factory
                        if (function.is_none())
                        {
                            return (Mod<"functools">().attr("partial")).call({ self.attr("register_function") }, { { "name", name } });
                        }
                        if (name.is_none())
                        {
                            name = function.attr("__name__");
                        }
                        self.attr("funcs").set_item(name, function);
                        return function;
                    }) },
                { "register_introspection_functions", w.method("SimpleXMLRPCDispatcher", "register_introspection_functions", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("funcs").method("update", { dict_of({ { S<"system.listMethods">(), self.attr("system_listMethods") }, { S<"system.methodSignature">(), self.attr("system_methodSignature") }, { S<"system.methodHelp">(), self.attr("system_methodHelp") } }) });
                        return none();
                    }) },
                { "register_multicall_functions", w.method("SimpleXMLRPCDispatcher", "register_multicall_functions", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("funcs").method("update", { dict_of({ { S<"system.multicall">(), self.attr("system_multicall") } }) });
                        return none();
                    }) },
                { "_marshaled_dispatch", w.method("SimpleXMLRPCDispatcher", "_marshaled_dispatch", { py_positional("data"), py_positional("dispatch_method", none()), py_positional("path", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py dispatch_method = a[2];
                        py path = a[3];
                        py exc;
                        py fault;
                        py method;
                        py params;
                        py response;
                        try
                        {
                            auto const items4 = unpack((Mod<"xmlrpc.client">().attr("loads")).call({ data }, { { "use_builtin_types", self.attr("use_builtin_types") } }), 2);
                            params = items4[0];
                            method = items4[1];
                            // generate response
                            if (!dispatch_method.is_none())
                            {
                                response = dispatch_method(method, params);
                            }
                            else
                            {
                                response = self.method("_dispatch", { method, params });
                            }
                            // wrap response in a singleton tuple
                            response = py_tuple({ response });
                            response = (Mod<"xmlrpc.client">().attr("dumps")).call({ response }, { { "methodresponse", I<1>() }, { "allow_none", self.attr("allow_none") }, { "encoding", self.attr("encoding") } });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler3;
                            if (matches(handler3.exception, Mod<"xmlrpc.client">().attr("Fault")))
                            {
                                fault = handler3.exception;
                                response = (Mod<"xmlrpc.client">().attr("dumps")).call({ fault }, { { "allow_none", self.attr("allow_none") }, { "encoding", self.attr("encoding") } });
                            }
                            else if (matches(handler3.exception, B<"BaseException">()))
                            {
                                exc = handler3.exception;
                                response = (Mod<"xmlrpc.client">().attr("dumps")).call({ (Mod<"xmlrpc.client">().attr("Fault"))(I<1>(), (S<"%s:%s">()) % py_tuple({ (B<"type">())(exc), exc })) }, { { "encoding", self.attr("encoding") }, { "allow_none", self.attr("allow_none") } });
                            }
                            else
                                py_reraise(handler3.exception);
                        }
                        return response.method("encode", { self.attr("encoding"), S<"xmlcharrefreplace">() });
                    }) },
                { "system_listMethods", w.method("SimpleXMLRPCDispatcher", "system_listMethods", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py methods;
                        methods = (B<"set">())(self.attr("funcs").method("keys"));
                        if (!self.attr("instance").is_none())
                        {
                            // Instance can implement _listMethod to return a list of
                            // methods
                            if (has_attr(self.attr("instance"), "_listMethods"))
                            {
                                methods = inplace(NEOS_PY_OR, methods, (B<"set">())(self.attr("instance").method("_listMethods")));
                            }
                            // if the instance has a _dispatch method then we
                            // don't have enough information to provide a list
                            // of methods
                            else if (!has_attr(self.attr("instance"), "_dispatch"))
                            {
                                methods = inplace(NEOS_PY_OR, methods, (B<"set">())((G<"xmlrpc.server.list_public_methods">())(self.attr("instance"))));
                            }
                        }
                        return (B<"sorted">())(methods);
                    }) },
                { "system_methodSignature", w.method("SimpleXMLRPCDispatcher", "system_methodSignature", { py_positional("method_name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py method_name = a[1];
                        // See http://xmlrpc.usefulinc.com/doc/sysmethodsig.html
                        return S<"signatures not supported">();
                    }) },
                { "system_methodHelp", w.method("SimpleXMLRPCDispatcher", "system_methodHelp", { py_positional("method_name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py method_name = a[1];
                        py method;
                        py pydoc;
                        method = none();
                        if (self.attr("funcs").contains(method_name))
                        {
                            method = self.attr("funcs")[method_name];
                        }
                        else if (!self.attr("instance").is_none())
                        {
                            // Instance can implement _methodHelp to return help for a method
                            if (has_attr(self.attr("instance"), "_methodHelp"))
                            {
                                return self.attr("instance").method("_methodHelp", { method_name });
                            }
                            // if the instance has a _dispatch method then we
                            // don't have enough information to provide help
                            else if (!has_attr(self.attr("instance"), "_dispatch"))
                            {
                                try
                                {
                                    method = (G<"xmlrpc.server.resolve_dotted_attribute">())(self.attr("instance"), method_name, self.attr("allow_dotted_names"));
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler5;
                                    if (matches(handler5.exception, B<"AttributeError">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler5.exception);
                                }
                            }
                        }
                        // Note that we aren't checking that the method actually
                        // be a callable object of some kind
                        if (method.is_none())
                        {
                            return S<"">();
                        }
                        else
                        {
                            pydoc = module_of("pydoc");
                            return pydoc.method("getdoc", { method });
                        }
                        return none();
                    }) },
                { "system_multicall", w.method("SimpleXMLRPCDispatcher", "system_multicall", { py_positional("call_list") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py call_list = a[1];
                        py call_;
                        py exc;
                        py fault;
                        py method_name;
                        py params;
                        py results;
                        results = py_list();
                        for (py const& item : iterate{ call_list })
                        {
                            call_ = item;
                            method_name = call_[S<"methodName">()];
                            params = call_[S<"params">()];
                            try
                            {
                                // XXX A marshalling error in any response will fail the entire
                                // multicall. If someone cares they should fix this.
                                results.method("append", { py_list({ self.method("_dispatch", { method_name, params }) }) });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler7;
                                if (matches(handler7.exception, Mod<"xmlrpc.client">().attr("Fault")))
                                {
                                    fault = handler7.exception;
                                    results.method("append", { dict_of({ { S<"faultCode">(), fault.attr("faultCode") }, { S<"faultString">(), fault.attr("faultString") } }) });
                                }
                                else if (matches(handler7.exception, B<"BaseException">()))
                                {
                                    exc = handler7.exception;
                                    results.method("append", { dict_of({ { S<"faultCode">(), I<1>() }, { S<"faultString">(), (S<"%s:%s">()) % py_tuple({ (B<"type">())(exc), exc }) } }) });
                                }
                                else
                                    py_reraise(handler7.exception);
                            }
                        }
                        return results;
                    }) },
                { "_dispatch", w.method("SimpleXMLRPCDispatcher", "_dispatch", { py_positional("method"), py_positional("params") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py method = a[1];
                        py params = a[2];
                        py func;
                        {
                            bool otherwise8 = false;
                            try
                            {
                                // call the matching registered function
                                func = self.attr("funcs")[method];
                                otherwise8 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler8;
                                if (matches(handler8.exception, B<"KeyError">()))
                                {
                                }
                                else
                                    py_reraise(handler8.exception);
                            }
                            if (otherwise8)
                            {
                                if (!func.is_none())
                                {
                                    return call_ex(func, {  }, params, py{});
                                }
                                py_raise((B<"Exception">())((S<"method \"%s\" is not supported">()) % method));
                            }
                        }
                        if (!self.attr("instance").is_none())
                        {
                            if (has_attr(self.attr("instance"), "_dispatch"))
                            {
                                // call the `_dispatch` method on the instance
                                return self.attr("instance").method("_dispatch", { method, params });
                            }
                            // call the instance's method directly
                            {
                                bool otherwise9 = false;
                                try
                                {
                                    func = (G<"xmlrpc.server.resolve_dotted_attribute">())(self.attr("instance"), method, self.attr("allow_dotted_names"));
                                    otherwise9 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler9;
                                    if (matches(handler9.exception, B<"AttributeError">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler9.exception);
                                }
                                if (otherwise9)
                                {
                                    if (!func.is_none())
                                    {
                                        return call_ex(func, {  }, params, py{});
                                    }
                                }
                            }
                        }
                        py_raise((B<"Exception">())((S<"method \"%s\" is not supported">()) % method));
                    }) }, }));

                // Class attribute listing the accessible path components;
                // paths not on this list will result in a 404 error.
                // if not None, encode responses larger than this, if possible
                // Override form StreamRequestHandler: full buffering of output
                // and no Nagle.
                // a re to match a gzip Accept-Encoding
            define("xmlrpc.server", "SimpleXMLRPCRequestHandler", w.make_class("SimpleXMLRPCRequestHandler", { Mod<"http.server">().attr("BaseHTTPRequestHandler") }, {
                { "rpc_paths", py_tuple({ S<"/">(), S<"/RPC2">(), S<"/pydoc.css">() }) },
                { "encode_threshold", I<1400>() },
                { "wbufsize", I<-1>() },
                { "disable_nagle_algorithm", boolean(true) },
                { "aepattern", (Mod<"re">()).method("compile", { S<"\n                            \\s* ([^\\s;]+) \\s*            #content-coding\n                            (;\\s* q \\s*=\\s* ([0-9\\.]+))? #q\n                            ">(), (Mod<"re">().attr("VERBOSE")) | (Mod<"re">().attr("IGNORECASE")) }) },
                { "accept_encodings", w.method("SimpleXMLRPCRequestHandler", "accept_encodings", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ae;
                        py e;
                        py match;
                        py r;
                        py v;
                        r = py_dict();
                        ae = self.attr("headers").method("get", { S<"Accept-Encoding">(), S<"">() });
                        for (py const& item : iterate{ ae.method("split", { S<",">() }) })
                        {
                            e = item;
                            match = self.attr("aepattern").method("match", { e });
                            if (match.truthy())
                            {
                                v = match.method("group", { I<3>() });
                                v = (v.truthy() ? (B<"float">())(v) : F<1.0>());
                                r.set_item(match.method("group", { I<1>() }), v);
                            }
                        }
                        return r;
                    }) },
                { "is_rpc_path_valid", w.method("SimpleXMLRPCRequestHandler", "is_rpc_path_valid", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("rpc_paths").truthy())
                        {
                            return boolean(self.attr("rpc_paths").contains(self.attr("path")));
                        }
                        else
                        {
                            // If .rpc_paths is empty, just assume all paths are legal
                            return boolean(true);
                        }
                        return none();
                    }) },
                { "do_POST", w.method("SimpleXMLRPCRequestHandler", "do_POST", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py L;
                        py chunk;
                        py chunk_size;
                        py data;
                        py e;
                        py max_chunk_size;
                        py q;
                        py response;
                        py size_remaining;
                        py trace;
                        // Check that the path is legal
                        if (!self.method("is_rpc_path_valid").truthy())
                        {
                            self.method("report_404");
                            return none();
                        }
                        {
                            bool otherwise11 = false;
                            try
                            {
                                // Get arguments by reading body of request.
                                // We read this in chunks to avoid straining
                                // socket.read(); around the 10 or 15Mb mark, some platforms
                                // begin to have problems (bug #792570).
                                max_chunk_size = ((I<10>()) * (I<1024>())) * (I<1024>());
                                size_remaining = (B<"int">())(self.attr("headers")[S<"content-length">()]);
                                L = py_list();
                                while (size_remaining.truthy())
                                {
                                    chunk_size = (B<"min">())(size_remaining, max_chunk_size);
                                    chunk = self.attr("rfile").method("read", { chunk_size });
                                    if (!chunk.truthy())
                                    {
                                        break;
                                    }
                                    L.method("append", { chunk });
                                    size_remaining = inplace(NEOS_PY_SUBTRACT, size_remaining, py{ L[I<-1>()].size() });
                                }
                                data = py{ make_bytes(std::string{ "", 0 }) }.method("join", { L });
                                data = self.method("decode_request_content", { data });
                                if (data.is_none())
                                {
                                    return none();
                                }
                                // In previous versions of SimpleXMLRPCServer, _dispatch
                                // could be overridden in this class, instead of in
                                // SimpleXMLRPCDispatcher. To maintain backwards compatibility,
                                // check to see if a subclass implements _dispatch and dispatch
                                // using that method if present.
                                response = self.attr("server").method("_marshaled_dispatch", { data, (B<"getattr">())(self, S<"_dispatch">(), none()), self.attr("path") });
                                otherwise11 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler11;
                                if (matches(handler11.exception, B<"Exception">()))
                                {
                                    e = handler11.exception;
                                    // internal error, report as HTTP server error
                                    self.method("send_response", { I<500>() });
                                    // Send information about the exception if requested
                                    if ((has_attr(self.attr("server"), "_send_traceback_header") && self.attr("server").attr("_send_traceback_header").truthy()))
                                    {
                                        self.method("send_header", { S<"X-exception">(), (B<"str">())(e) });
                                        trace = (Mod<"traceback">().attr("format_exc"))();
                                        trace = (B<"str">())(trace.method("encode", { S<"ASCII">(), S<"backslashreplace">() }), S<"ASCII">());
                                        self.method("send_header", { S<"X-traceback">(), trace });
                                    }
                                    self.method("send_header", { S<"Content-length">(), S<"0">() });
                                    self.method("end_headers");
                                }
                                else
                                    py_reraise(handler11.exception);
                            }
                            if (otherwise11)
                            {
                                self.method("send_response", { I<200>() });
                                self.method("send_header", { S<"Content-type">(), S<"text/xml">() });
                                if (!self.attr("encode_threshold").is_none())
                                {
                                    if (py{ response.size() } > self.attr("encode_threshold"))
                                    {
                                        q = self.method("accept_encodings").method("get", { S<"gzip">(), I<0>() });
                                        if (q.truthy())
                                        {
                                            try
                                            {
                                                response = (Mod<"xmlrpc.client">().attr("gzip_encode"))(response);
                                                self.method("send_header", { S<"Content-Encoding">(), S<"gzip">() });
                                            }
                                            catch (raised const&)
                                            {
                                                pyclass::handler_scope handler13;
                                                if (matches(handler13.exception, B<"NotImplementedError">()))
                                                {
                                                }
                                                else
                                                    py_reraise(handler13.exception);
                                            }
                                        }
                                    }
                                }
                                self.method("send_header", { S<"Content-length">(), (B<"str">())(py{ response.size() }) });
                                self.method("end_headers");
                                self.attr("wfile").method("write", { response });
                            }
                        }
                        return none();
                    }) },
                { "decode_request_content", w.method("SimpleXMLRPCRequestHandler", "decode_request_content", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py encoding;
                        encoding = self.attr("headers").method("get", { S<"content-encoding">(), S<"identity">() }).method("lower");
                        if (encoding == (S<"identity">()))
                        {
                            return data;
                        }
                        if (encoding == (S<"gzip">()))
                        {
                            try
                            {
                                return (Mod<"xmlrpc.client">().attr("gzip_decode"))(data);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler14;
                                if (matches(handler14.exception, B<"NotImplementedError">()))
                                {
                                    self.method("send_response", { I<501>(), (S<"encoding %r not supported">()) % encoding });
                                }
                                else if (matches(handler14.exception, B<"ValueError">()))
                                {
                                    self.method("send_response", { I<400>(), S<"error decoding gzip content">() });
                                }
                                else
                                    py_reraise(handler14.exception);
                            }
                        }
                        else
                        {
                            self.method("send_response", { I<501>(), (S<"encoding %r not supported">()) % encoding });
                        }
                        self.method("send_header", { S<"Content-length">(), S<"0">() });
                        self.method("end_headers");
                        return none();
                    }) },
                { "report_404", w.method("SimpleXMLRPCRequestHandler", "report_404", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py response;
                        self.method("send_response", { I<404>() });
                        response = py{ make_bytes(std::string{ "\x4e\x6f\x20\x73\x75\x63\x68\x20\x70\x61\x67\x65", 12 }) };
                        self.method("send_header", { S<"Content-type">(), S<"text/plain">() });
                        self.method("send_header", { S<"Content-length">(), (B<"str">())(py{ response.size() }) });
                        self.method("end_headers");
                        self.attr("wfile").method("write", { response });
                        return none();
                    }) },
                { "log_request", w.method("SimpleXMLRPCRequestHandler", "log_request", { py_positional("code", S<"-">()), py_positional("size", S<"-">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py code = a[1];
                        py size = a[2];
                        if (self.attr("server").attr("logRequests").truthy())
                        {
                            (Mod<"http.server">().attr("BaseHTTPRequestHandler")).method("log_request", { self, code, size });
                        }
                        return none();
                    }) }, }));

                // Warning: this is for debugging purposes only! Never set this to True in
                // production code, as will be sending out sensitive information (exception
                // and stack trace details) when exceptions are raised inside
                // SimpleXMLRPCRequestHandler.do_POST
            define("xmlrpc.server", "SimpleXMLRPCServer", w.make_class("SimpleXMLRPCServer", { Mod<"socketserver">().attr("TCPServer"), G<"xmlrpc.server.SimpleXMLRPCDispatcher">() }, {
                { "allow_reuse_address", boolean(true) },
                { "_send_traceback_header", boolean(false) },
                { "__init__", w.method("SimpleXMLRPCServer", "__init__", { py_positional("addr"), py_positional("requestHandler", G<"xmlrpc.server.SimpleXMLRPCRequestHandler">()), py_positional("logRequests", boolean(true)), py_positional("allow_none", boolean(false)), py_positional("encoding", none()), py_positional("bind_and_activate", boolean(true)), py_positional("use_builtin_types", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py addr = a[1];
                        py requestHandler = a[2];
                        py logRequests = a[3];
                        py allow_none = a[4];
                        py encoding = a[5];
                        py bind_and_activate = a[6];
                        py use_builtin_types = a[7];
                        self.set_attr("logRequests", logRequests);
                        (G<"xmlrpc.server.SimpleXMLRPCDispatcher">()).method("__init__", { self, allow_none, encoding, use_builtin_types });
                        (Mod<"socketserver">().attr("TCPServer")).method("__init__", { self, addr, requestHandler, bind_and_activate });
                        return none();
                    }) }, }));

            define("xmlrpc.server", "MultiPathXMLRPCServer", w.make_class("MultiPathXMLRPCServer", { G<"xmlrpc.server.SimpleXMLRPCServer">() }, {
                { "__init__", w.method("MultiPathXMLRPCServer", "__init__", { py_positional("addr"), py_positional("requestHandler", G<"xmlrpc.server.SimpleXMLRPCRequestHandler">()), py_positional("logRequests", boolean(true)), py_positional("allow_none", boolean(false)), py_positional("encoding", none()), py_positional("bind_and_activate", boolean(true)), py_positional("use_builtin_types", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py addr = a[1];
                        py requestHandler = a[2];
                        py logRequests = a[3];
                        py allow_none = a[4];
                        py encoding = a[5];
                        py bind_and_activate = a[6];
                        py use_builtin_types = a[7];
                        (G<"xmlrpc.server.SimpleXMLRPCServer">()).method("__init__", { self, addr, requestHandler, logRequests, allow_none, encoding, bind_and_activate, use_builtin_types });
                        self.set_attr("dispatchers", py_dict());
                        self.set_attr("allow_none", allow_none);
                        self.set_attr("encoding", [&]() -> py { py const t = encoding; return t.truthy() ? t : S<"utf-8">(); }());
                        return none();
                    }) },
                { "add_dispatcher", w.method("MultiPathXMLRPCServer", "add_dispatcher", { py_positional("path"), py_positional("dispatcher") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py dispatcher = a[2];
                        self.attr("dispatchers").set_item(path, dispatcher);
                        return dispatcher;
                    }) },
                { "get_dispatcher", w.method("MultiPathXMLRPCServer", "get_dispatcher", { py_positional("path") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        return self.attr("dispatchers")[path];
                    }) },
                { "_marshaled_dispatch", w.method("MultiPathXMLRPCServer", "_marshaled_dispatch", { py_positional("data"), py_positional("dispatch_method", none()), py_positional("path", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py dispatch_method = a[2];
                        py path = a[3];
                        py exc;
                        py response;
                        try
                        {
                            response = self.attr("dispatchers")[path].method("_marshaled_dispatch", { data, dispatch_method, path });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler15;
                            if (matches(handler15.exception, B<"BaseException">()))
                            {
                                exc = handler15.exception;
                                // report low level exception back to server
                                // (each dispatcher should have handled their own
                                // exceptions)
                                response = (Mod<"xmlrpc.client">().attr("dumps")).call({ (Mod<"xmlrpc.client">().attr("Fault"))(I<1>(), (S<"%s:%s">()) % py_tuple({ (B<"type">())(exc), exc })) }, { { "encoding", self.attr("encoding") }, { "allow_none", self.attr("allow_none") } });
                                response = response.method("encode", { self.attr("encoding"), S<"xmlcharrefreplace">() });
                            }
                            else
                                py_reraise(handler15.exception);
                        }
                        return response;
                    }) }, }));

            define("xmlrpc.server", "CGIXMLRPCRequestHandler", w.make_class("CGIXMLRPCRequestHandler", { G<"xmlrpc.server.SimpleXMLRPCDispatcher">() }, {
                { "__init__", w.method("CGIXMLRPCRequestHandler", "__init__", { py_positional("allow_none", boolean(false)), py_positional("encoding", none()), py_positional("use_builtin_types", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py allow_none = a[1];
                        py encoding = a[2];
                        py use_builtin_types = a[3];
                        (G<"xmlrpc.server.SimpleXMLRPCDispatcher">()).method("__init__", { self, allow_none, encoding, use_builtin_types });
                        return none();
                    }) },
                { "handle_xmlrpc", w.method("CGIXMLRPCRequestHandler", "handle_xmlrpc", { py_positional("request_text") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py request_text = a[1];
                        py response;
                        response = self.method("_marshaled_dispatch", { request_text });
                        (B<"print">())(S<"Content-Type: text/xml">());
                        (B<"print">())((S<"Content-Length: %d">()) % py{ response.size() });
                        (B<"print">())();
                        (Mod<"sys">().attr("stdout")).method("flush");
                        (Mod<"sys">().attr("stdout")).attr("buffer").method("write", { response });
                        (Mod<"sys">().attr("stdout")).attr("buffer").method("flush");
                        return none();
                    }) },
                { "handle_get", w.method("CGIXMLRPCRequestHandler", "handle_get", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py code;
                        py explain;
                        py message;
                        py response;
                        code = I<400>();
                        auto const items16 = unpack((Mod<"http.server">().attr("BaseHTTPRequestHandler")).attr("responses")[code], 2);
                        message = items16[0];
                        explain = items16[1];
                        response = (Mod<"http.server">().attr("DEFAULT_ERROR_MESSAGE")) % dict_of({ { S<"code">(), code }, { S<"message">(), message }, { S<"explain">(), explain } });
                        response = response.method("encode", { S<"utf-8">() });
                        (B<"print">())((S<"Status: %d %s">()) % py_tuple({ code, message }));
                        (B<"print">())((S<"Content-Type: %s">()) % (Mod<"http.server">().attr("DEFAULT_ERROR_CONTENT_TYPE")));
                        (B<"print">())((S<"Content-Length: %d">()) % py{ response.size() });
                        (B<"print">())();
                        (Mod<"sys">().attr("stdout")).method("flush");
                        (Mod<"sys">().attr("stdout")).attr("buffer").method("write", { response });
                        (Mod<"sys">().attr("stdout")).attr("buffer").method("flush");
                        return none();
                    }) },
                { "handle_request", w.method("CGIXMLRPCRequestHandler", "handle_request", { py_positional("request_text", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py request_text = a[1];
                        py length;
                        if ((request_text.is_none() && ((Mod<"os">().attr("environ")).method("get", { S<"REQUEST_METHOD">(), none() }) == (S<"GET">()))))
                        {
                            self.method("handle_get");
                        }
                        else
                        {
                            // POST data is normally available through stdin
                            try
                            {
                                length = (B<"int">())((Mod<"os">().attr("environ")).method("get", { S<"CONTENT_LENGTH">(), none() }));
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler17;
                                if (matches(handler17.exception, py_tuple({ B<"ValueError">(), B<"TypeError">() })))
                                {
                                    length = I<-1>();
                                }
                                else
                                    py_reraise(handler17.exception);
                            }
                            if (request_text.is_none())
                            {
                                request_text = (Mod<"sys">().attr("stdin")).method("read", { length });
                            }
                            self.method("handle_xmlrpc", { request_text });
                        }
                        return none();
                    }) }, }));

            // -----------------------------------------------------------------------------
            // Self documenting XML-RPC Server.
            define("xmlrpc.server", "_doc_classes", w.function("_doc_classes", "_doc_classes", {  }, [](arguments a) -> py
                {
                    py DocCGIXMLRPCRequestHandler;
                    auto const DocXMLRPCRequestHandler = std::make_shared<py>();
                    py DocXMLRPCServer;
                    auto const ServerHTMLDoc = std::make_shared<py>();
                    auto const XMLRPCDocGenerator = std::make_shared<py>();
                    auto const pydoc = std::make_shared<py>();
                    *pydoc = module_of("pydoc");
                    *ServerHTMLDoc = w.make_class("ServerHTMLDoc", { (*pydoc).attr("HTMLDoc") }, {
    { "markup", w.method("ServerHTMLDoc", "markup", { py_positional("text"), py_positional("escape", none()), py_positional("funcs", py_dict()), py_positional("classes", py_dict()), py_positional("methods", py_dict()) }, [](arguments a) -> py
        {
            py self = a[0];
            py text = a[1];
            py escape = a[2];
            py funcs = a[3];
            py classes = a[4];
            py methods = a[5];
            py all;
            py end;
            py here;
            py match;
            py name;
            py pattern;
            py pep;
            py results;
            py rfc;
            py scheme;
            py selfdot;
            py start;
            py url;
            escape = [&]() -> py { py const t = escape; return t.truthy() ? t : self.attr("escape"); }();
            results = py_list();
            here = I<0>();
            // XXX Note that this regular expression does not allow for the
            // hyperlinking of arbitrary strings being used as method
            // names. Only methods with names consisting of word characters
            // and '.'s are hyperlinked.
            pattern = (Mod<"re">().attr("compile"))(S<"\\b((http|https|ftp)://\\S+[\\w/]|RFC[- ]?(\\d+)|PEP[- ]?(\\d+)|(self\\.)?((?:\\w|\\.)+))\\b">());
            while (([&]() -> py { match = pattern.method("search", { text, here }); return match; }()).truthy())
            {
                auto const items19 = unpack(match.method("span"), 2);
                start = items19[0];
                end = items19[1];
                results.method("append", { escape(text[B<"slice">()(here, start, none())]) });
                auto const items20 = unpack(match.method("groups"), 6);
                all = items20[0];
                scheme = items20[1];
                rfc = items20[2];
                pep = items20[3];
                selfdot = items20[4];
                name = items20[5];
                if (scheme.truthy())
                {
                    url = escape(all).method("replace", { S<"\"">(), S<"&quot;">() });
                    results.method("append", { (S<"<a href=\"%s\">%s</a>">()) % py_tuple({ url, url }) });
                }
                else if (rfc.truthy())
                {
                    url = (S<"https://www.rfc-editor.org/rfc/rfc%d.txt">()) % (B<"int">())(rfc);
                    results.method("append", { (S<"<a href=\"%s\">%s</a>">()) % py_tuple({ url, escape(all) }) });
                }
                else if (pep.truthy())
                {
                    url = (S<"https://peps.python.org/pep-%04d/">()) % (B<"int">())(pep);
                    results.method("append", { (S<"<a href=\"%s\">%s</a>">()) % py_tuple({ url, escape(all) }) });
                }
                else if (text[B<"slice">()(end, end + (I<1>()), none())] == (S<"(">()))
                {
                    results.method("append", { self.method("namelink", { name, methods, funcs, classes }) });
                }
                else if (selfdot.truthy())
                {
                    results.method("append", { (S<"self.<strong>%s</strong>">()) % name });
                }
                else
                {
                    results.method("append", { self.method("namelink", { name, classes }) });
                }
                here = end;
            }
            results.method("append", { escape(text[B<"slice">()(here, none(), none())]) });
            return (S<"">()).method("join", { results });
        }) },
    { "docroutine", w.method("ServerHTMLDoc", "docroutine", { py_positional("object"), py_positional("name"), py_positional("mod", none()), py_positional("funcs", py_dict()), py_positional("classes", py_dict()), py_positional("methods", py_dict()), py_positional("cl", none()) }, [pydoc](arguments a) -> py
        {
            py self = a[0];
            py object_ = a[1];
            py name = a[2];
            py mod = a[3];
            py funcs = a[4];
            py classes = a[5];
            py methods = a[6];
            py cl = a[7];
            py anchor;
            py argspec;
            py decl;
            py doc;
            py docstring;
            py note;
            py title;
            anchor = (([&]() -> py { py const t = [&]() -> py { py const t = cl; return !t.truthy() ? t : cl.attr("__name__"); }(); return t.truthy() ? t : S<"">(); }()) + (S<"-">())) + name;
            note = S<"">();
            title = (S<"<a name=\"%s\"><strong>%s</strong></a>">()) % py_tuple({ self.method("escape", { anchor }), self.method("escape", { name }) });
            if (callable(object_))
            {
                argspec = (B<"str">())((Mod<"inspect">().attr("signature"))(object_));
            }
            else
            {
                argspec = S<"(...)">();
            }
            if (isinstance(object_, B<"tuple">()))
            {
                argspec = [&]() -> py { py const t = object_[I<0>()]; return t.truthy() ? t : argspec; }();
                docstring = [&]() -> py { py const t = object_[I<1>()]; return t.truthy() ? t : S<"">(); }();
            }
            else
            {
                docstring = (*pydoc).method("getdoc", { object_ });
            }
            decl = (title + argspec) + ([&]() -> py { py const t = note; return !t.truthy() ? t : self.method("grey", { (S<"<font face=\"helvetica, arial\">%s</font>">()) % note }); }());
            doc = self.method("markup", { docstring, self.attr("preformat"), funcs, classes, methods });
            doc = [&]() -> py { py const t = doc; return !t.truthy() ? t : (S<"<dd><tt>%s</tt></dd>">()) % doc; }();
            return (S<"<dl><dt>%s</dt>%s</dl>\n">()) % py_tuple({ decl, doc });
        }) },
    { "docserver", w.method("ServerHTMLDoc", "docserver", { py_positional("server_name"), py_positional("package_documentation"), py_positional("methods") }, [](arguments a) -> py
        {
            py self = a[0];
            py server_name = a[1];
            py package_documentation = a[2];
            py methods = a[3];
            py contents;
            py doc;
            py fdict;
            py head;
            py key;
            py method_items;
            py result;
            py value;
            fdict = py_dict();
            for (py const& item : iterate{ methods.method("items") })
            {
                auto const items22 = unpack(item, 2);
                key = items22[0];
                value = items22[1];
                fdict.set_item(key, (S<"#-">()) + key);
                fdict.set_item(value, fdict[key]);
            }
            server_name = self.method("escape", { server_name });
            head = (S<"<big><big><strong>%s</strong></big></big>">()) % server_name;
            result = self.method("heading", { head });
            doc = self.method("markup", { package_documentation, self.attr("preformat"), fdict });
            doc = [&]() -> py { py const t = doc; return !t.truthy() ? t : (S<"<tt>%s</tt>">()) % doc; }();
            result = result + ((S<"<p>%s</p>\n">()) % doc);
            contents = py_list();
            method_items = (B<"sorted">())(methods.method("items"));
            for (py const& item : iterate{ method_items })
            {
                auto const items24 = unpack(item, 2);
                key = items24[0];
                value = items24[1];
                contents.method("append", { self.method("docroutine", { value, key }, { { "funcs", fdict } }) });
            }
            result = result + self.method("bigsection", { S<"Methods">(), S<"functions">(), (S<"">()).method("join", { contents }) });
            return result;
        }) },
    { "page", w.method("ServerHTMLDoc", "page", { py_positional("title"), py_positional("contents") }, [](arguments a) -> py
        {
            py self = a[0];
            py title = a[1];
            py contents = a[2];
            py css_link;
            py css_path;
            css_path = S<"/pydoc.css">();
            css_link = (S<"<link rel=\"stylesheet\" type=\"text/css\" href=\"%s\">">()) % css_path;
            return (S<"    <!DOCTYPE>\n    <html lang=\"en\">\n    <head>\n    <meta charset=\"utf-8\">\n    <title>Python: %s</title>\n    %s</head><body>%s</body></html>">()) % py_tuple({ title, css_link, contents });
        }) }, }, "_doc_classes.<locals>.ServerHTMLDoc");
                    *XMLRPCDocGenerator = w.make_class("XMLRPCDocGenerator", {  }, {
    { "__init__", w.method("XMLRPCDocGenerator", "__init__", {  }, [](arguments a) -> py
        {
            py self = a[0];
            self.set_attr("server_name", S<"XML-RPC Server Documentation">());
            self.set_attr("server_documentation", S<"This server exports the following methods through the XML-RPC protocol.">());
            self.set_attr("server_title", S<"XML-RPC Server Documentation">());
            return none();
        }) },
    { "set_server_title", w.method("XMLRPCDocGenerator", "set_server_title", { py_positional("server_title") }, [](arguments a) -> py
        {
            py self = a[0];
            py server_title = a[1];
            self.set_attr("server_title", server_title);
            return none();
        }) },
    { "set_server_name", w.method("XMLRPCDocGenerator", "set_server_name", { py_positional("server_name") }, [](arguments a) -> py
        {
            py self = a[0];
            py server_name = a[1];
            self.set_attr("server_name", server_name);
            return none();
        }) },
    { "set_server_documentation", w.method("XMLRPCDocGenerator", "set_server_documentation", { py_positional("server_documentation") }, [](arguments a) -> py
        {
            py self = a[0];
            py server_documentation = a[1];
            self.set_attr("server_documentation", server_documentation);
            return none();
        }) },
    { "generate_html_documentation", w.method("XMLRPCDocGenerator", "generate_html_documentation", {  }, [ServerHTMLDoc](arguments a) -> py
        {
            py self = a[0];
            py documentation;
            py documenter;
            py method;
            py method_info;
            py method_name;
            py methods;
            methods = py_dict();
            for (py const& item : iterate{ self.method("system_listMethods") })
            {
                method_name = item;
                if (self.attr("funcs").contains(method_name))
                {
                    method = self.attr("funcs")[method_name];
                }
                else if (!self.attr("instance").is_none())
                {
                    method_info = py_list({ none(), none() });
                    if (has_attr(self.attr("instance"), "_get_method_argstring"))
                    {
                        method_info.set_item(I<0>(), self.attr("instance").method("_get_method_argstring", { method_name }));
                    }
                    if (has_attr(self.attr("instance"), "_methodHelp"))
                    {
                        method_info.set_item(I<1>(), self.attr("instance").method("_methodHelp", { method_name }));
                    }
                    method_info = (B<"tuple">())(method_info);
                    if (method_info != py_tuple({ none(), none() }))
                    {
                        method = method_info;
                    }
                    else if (!has_attr(self.attr("instance"), "_dispatch"))
                    {
                        try
                        {
                            method = (G<"xmlrpc.server.resolve_dotted_attribute">())(self.attr("instance"), method_name);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler26;
                            if (matches(handler26.exception, B<"AttributeError">()))
                            {
                                method = method_info;
                            }
                            else
                                py_reraise(handler26.exception);
                        }
                    }
                    else
                    {
                        method = method_info;
                    }
                }
                else
                {
                    if (!(I<0>()).truthy())
                        py_raise(B<"AssertionError">()(S<"Could not find method in self.functions and no instance installed">()));
                }
                methods.set_item(method_name, method);
            }
            documenter = (*ServerHTMLDoc)();
            documentation = documenter.method("docserver", { self.attr("server_name"), self.attr("server_documentation"), methods });
            return documenter.method("page", { (Mod<"html">().attr("escape"))(self.attr("server_title")), documentation });
        }) }, }, "_doc_classes.<locals>.XMLRPCDocGenerator");
                    *DocXMLRPCRequestHandler = w.make_class("DocXMLRPCRequestHandler", { G<"xmlrpc.server.SimpleXMLRPCRequestHandler">() }, {
    { "_get_css", w.method("DocXMLRPCRequestHandler", "_get_css", { py_positional("url") }, [](arguments a) -> py
        {
            py self = a[0];
            py url = a[1];
            py css_path;
            py fp;
            py path_here;
            path_here = (Mod<"os">().attr("path")).method("dirname", { (Mod<"os">().attr("path")).method("realpath", { Mod<"xmlrpc.server">().attr("__file__") }) });
            css_path = (Mod<"os">().attr("path")).method("join", { path_here, S<"..">(), S<"pydoc_data">(), S<"_pydoc.css">() });
            {
                py const manager27 = (B<"open">()).call({ css_path }, { { "mode", S<"rb">() } });
                py const exit27 = pyclass::special_method(manager27, "__exit__");
                py const value27 = pyclass::special_method(manager27, "__enter__")(manager27);
                fp = value27;
                [[maybe_unused]] int flow27 = 0;
                [[maybe_unused]] py result27;
                bool exited27 = false;
                try
                {
                    result27 = fp.method("read");
                    flow27 = 1;
                    goto finally27;
                }
                catch (raised const&)
                {
                    pyclass::handler_scope handler27;
                    exited27 = true;
                    if (!exit27(manager27, type_of(handler27.exception), handler27.exception, handler27.exception.attr("__traceback__")).truthy())
                        py_reraise(handler27.exception);
                }
                finally27:
                if (!exited27)
                    exit27(manager27, none(), none(), none());
                if (flow27 == 1)
                    return result27;
            }
            return none();
        }) },
    { "do_GET", w.method("DocXMLRPCRequestHandler", "do_GET", {  }, [](arguments a) -> py
        {
            py self = a[0];
            py content_type;
            py response;
            // Check that the path is legal
            if (!self.method("is_rpc_path_valid").truthy())
            {
                self.method("report_404");
                return none();
            }
            if (self.attr("path").method("endswith", { S<".css">() }).truthy())
            {
                content_type = S<"text/css">();
                response = self.method("_get_css", { self.attr("path") });
            }
            else
            {
                content_type = S<"text/html">();
                response = self.attr("server").method("generate_html_documentation").method("encode", { S<"utf-8">() });
            }
            self.method("send_response", { I<200>() });
            self.method("send_header", { S<"Content-Type">(), (S<"%s; charset=UTF-8">()) % content_type });
            self.method("send_header", { S<"Content-length">(), (B<"str">())(py{ response.size() }) });
            self.method("end_headers");
            self.attr("wfile").method("write", { response });
            return none();
        }) }, }, "_doc_classes.<locals>.DocXMLRPCRequestHandler");
                    DocXMLRPCServer = w.make_class("DocXMLRPCServer", { G<"xmlrpc.server.SimpleXMLRPCServer">(), (*XMLRPCDocGenerator) }, {
    { "__init__", w.method("DocXMLRPCServer", "__init__", { py_positional("addr"), py_positional("requestHandler", (*DocXMLRPCRequestHandler)), py_positional("logRequests", boolean(true)), py_positional("allow_none", boolean(false)), py_positional("encoding", none()), py_positional("bind_and_activate", boolean(true)), py_positional("use_builtin_types", boolean(false)) }, [DocXMLRPCRequestHandler, XMLRPCDocGenerator](arguments a) -> py
        {
            py self = a[0];
            py addr = a[1];
            py requestHandler = a[2];
            py logRequests = a[3];
            py allow_none = a[4];
            py encoding = a[5];
            py bind_and_activate = a[6];
            py use_builtin_types = a[7];
            (G<"xmlrpc.server.SimpleXMLRPCServer">()).method("__init__", { self, addr, requestHandler, logRequests, allow_none, encoding, bind_and_activate, use_builtin_types });
            (*XMLRPCDocGenerator).method("__init__", { self });
            return none();
        }) }, }, "_doc_classes.<locals>.DocXMLRPCServer");
                    DocCGIXMLRPCRequestHandler = w.make_class("DocCGIXMLRPCRequestHandler", { G<"xmlrpc.server.CGIXMLRPCRequestHandler">(), (*XMLRPCDocGenerator) }, {
    { "handle_get", w.method("DocCGIXMLRPCRequestHandler", "handle_get", {  }, [](arguments a) -> py
        {
            py self = a[0];
            py response;
            response = self.method("generate_html_documentation").method("encode", { S<"utf-8">() });
            (B<"print">())(S<"Content-Type: text/html">());
            (B<"print">())((S<"Content-Length: %d">()) % py{ response.size() });
            (B<"print">())();
            (Mod<"sys">().attr("stdout")).method("flush");
            (Mod<"sys">().attr("stdout")).attr("buffer").method("write", { response });
            (Mod<"sys">().attr("stdout")).attr("buffer").method("flush");
            return none();
        }) },
    { "__init__", w.method("DocCGIXMLRPCRequestHandler", "__init__", {  }, [XMLRPCDocGenerator](arguments a) -> py
        {
            py self = a[0];
            (G<"xmlrpc.server.CGIXMLRPCRequestHandler">()).method("__init__", { self });
            (*XMLRPCDocGenerator).method("__init__", { self });
            return none();
        }) }, }, "_doc_classes.<locals>.DocCGIXMLRPCRequestHandler");
                    return dict_of({ { S<"ServerHTMLDoc">(), (*ServerHTMLDoc) }, { S<"XMLRPCDocGenerator">(), (*XMLRPCDocGenerator) }, { S<"DocXMLRPCRequestHandler">(), (*DocXMLRPCRequestHandler) }, { S<"DocXMLRPCServer">(), DocXMLRPCServer }, { S<"DocCGIXMLRPCRequestHandler">(), DocCGIXMLRPCRequestHandler } });
                }));

            define("xmlrpc.server", "_doc_classes_made", none());
            define("xmlrpc.server", "__getattr__", w.function("__getattr__", "__getattr__", { py_positional("name") }, [](arguments a) -> py
                {
                    py name = a[0];
                    if (py_tuple({ S<"ServerHTMLDoc">(), S<"XMLRPCDocGenerator">(), S<"DocXMLRPCRequestHandler">(), S<"DocXMLRPCServer">(), S<"DocCGIXMLRPCRequestHandler">() }).contains(name))
                    {
                        if ((G<"xmlrpc.server._doc_classes_made">()).is_none())
                        {
                            set_global("xmlrpc.server", "_doc_classes_made", (G<"xmlrpc.server._doc_classes">())());
                        }
                        return (G<"xmlrpc.server._doc_classes_made">())[name];
                    }
                    py_raise((B<"AttributeError">())(S(std::string{ "module " } + (S<"xmlrpc.server">()).repr() + " has no attribute " + name.repr())));
                }));

            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool xmlrpc_server_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_xmlrpc_server();
                }
                if (auto const doc = xmlrpc_server_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("Fault", import_attribute("xmlrpc.client", "Fault").release());
                aAttributes.emplace_back("dumps", import_attribute("xmlrpc.client", "dumps").release());
                aAttributes.emplace_back("loads", import_attribute("xmlrpc.client", "loads").release());
                aAttributes.emplace_back("gzip_encode", import_attribute("xmlrpc.client", "gzip_encode").release());
                aAttributes.emplace_back("gzip_decode", import_attribute("xmlrpc.client", "gzip_decode").release());
                aAttributes.emplace_back("BaseHTTPRequestHandler", import_attribute("http.server", "BaseHTTPRequestHandler").release());
                aAttributes.emplace_back("partial", import_attribute("functools", "partial").release());
                aAttributes.emplace_back("signature", import_attribute("inspect", "signature").release());
                aAttributes.emplace_back("html", py{ fn_import("html") }.release());
                aAttributes.emplace_back("http", py{ fn_import("http") }.release());
                aAttributes.emplace_back("socketserver", py{ fn_import("socketserver") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("traceback", py{ fn_import("traceback") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("xmlrpc.server", xmlrpc_server_module);
    }
}
