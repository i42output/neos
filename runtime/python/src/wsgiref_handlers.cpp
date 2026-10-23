/*
  wsgiref_handlers.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/wsgiref/handlers.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module wsgiref.handlers (CPython 3.13's Lib/wsgiref/handlers.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "wsgiref_handlers_docs.hpp"

namespace neos::python_runtime::detail::wsgiref_handlers_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "wsgiref.handlers.name"))
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
                py_raise("SystemError", std::string{ "wsgiref.handlers: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"wsgiref.handlers.name">()))
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
            if (modules_imported("wsgiref.handlers"))
                return py{ fn_import("wsgiref.handlers") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "wsgiref.handlers" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "wsgiref.handlers", wsgiref_handlers_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- wsgiref.handlers (Lib/wsgiref/handlers.py)

        void make_wsgiref_handlers()
        {
            static module_writer const& w = writer("wsgiref.handlers");
            define("wsgiref.handlers", "__all__", py_list({ S<"BaseHandler">(), S<"SimpleHandler">(), S<"BaseCGIHandler">(), S<"CGIHandler">(), S<"IISCGIHandler">(), S<"read_environ">() }));
            // Weekday and month names for HTTP date/time formatting; always English!
            define("wsgiref.handlers", "_weekdayname", py_list({ S<"Mon">(), S<"Tue">(), S<"Wed">(), S<"Thu">(), S<"Fri">(), S<"Sat">(), S<"Sun">() }));
            define("wsgiref.handlers", "_monthname", py_list({ none(), S<"Jan">(), S<"Feb">(), S<"Mar">(), S<"Apr">(), S<"May">(), S<"Jun">(), S<"Jul">(), S<"Aug">(), S<"Sep">(), S<"Oct">(), S<"Nov">(), S<"Dec">() }));
            define("wsgiref.handlers", "format_date_time", w.function("format_date_time", "format_date_time", { py_positional("timestamp") }, [](arguments a) -> py
                {
                    py timestamp = a[0];
                    py day;
                    py hh;
                    py mm;
                    py month;
                    py ss;
                    py wd;
                    py y;
                    py year;
                    py z;
                    auto const items1 = unpack((Mod<"time">().attr("gmtime"))(timestamp), 9);
                    year = items1[0];
                    month = items1[1];
                    day = items1[2];
                    hh = items1[3];
                    mm = items1[4];
                    ss = items1[5];
                    wd = items1[6];
                    y = items1[7];
                    z = items1[8];
                    return (S<"%s, %02d %3s %4d %02d:%02d:%02d GMT">()) % py_tuple({ (G<"wsgiref.handlers._weekdayname">())[wd], day, (G<"wsgiref.handlers._monthname">())[month], year, hh, mm, ss });
                }));

            define("wsgiref.handlers", "_is_request", set_of({ S<"SCRIPT_NAME">(), S<"PATH_INFO">(), S<"QUERY_STRING">(), S<"REQUEST_METHOD">(), S<"AUTH_TYPE">(), S<"CONTENT_TYPE">(), S<"CONTENT_LENGTH">(), S<"HTTPS">(), S<"REMOTE_USER">(), S<"REMOTE_IDENT">() }).attr("__contains__"));
            define("wsgiref.handlers", "_needs_transcode", w.function("_needs_transcode", "_needs_transcode", { py_positional("k") }, [](arguments a) -> py
                {
                    py k = a[0];
                    return [&]() -> py { py const t = (G<"wsgiref.handlers._is_request">())(k); return t.truthy() ? t : [&]() -> py { py const t = k.method("startswith", { S<"HTTP_">() }); return t.truthy() ? t : [&]() -> py { py const t = k.method("startswith", { S<"SSL_">() }); return t.truthy() ? t : [&]() -> py { py const t = k.method("startswith", { S<"REDIRECT_">() }); return !t.truthy() ? t : (G<"wsgiref.handlers._needs_transcode">())(k[B<"slice">()(I<9>(), none(), none())]); }(); }(); }(); }();
                }));

            define("wsgiref.handlers", "read_environ", w.function("read_environ", "read_environ", {  }, [](arguments a) -> py
                {
                    py enc;
                    py environ_;
                    py esc;
                    py k;
                    py software;
                    py v;
                    enc = (Mod<"sys">().attr("getfilesystemencoding"))();
                    esc = S<"surrogateescape">();
                    try
                    {
                        (S<"">()).method("encode", { S<"utf-8">(), esc });
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler2;
                        if (matches(handler2.exception, B<"LookupError">()))
                        {
                            esc = S<"replace">();
                        }
                        else
                            py_reraise(handler2.exception);
                    }
                    environ_ = py_dict();
                    // Take the basic environment from native-unicode os.environ. Attempt to
                    // fix up the variables that come from the HTTP request to compensate for
                    // the bytes->unicode decoding step that will already have taken place.
                    for (py const& item : iterate{ (Mod<"os">().attr("environ")).method("items") })
                    {
                        auto const items4 = unpack(item, 2);
                        k = items4[0];
                        v = items4[1];
                        if ((G<"wsgiref.handlers._needs_transcode">())(k).truthy())
                        {
                            // On win32, the os.environ is natively Unicode. Different servers
                            // decode the request bytes using different encodings.
                    #if defined(_WIN32)
                            {
                                software = (Mod<"os">().attr("environ")).method("get", { S<"SERVER_SOFTWARE">(), S<"">() }).method("lower");
                                // On IIS, the HTTP request will be decoded as UTF-8 as long
                                // as the input is a valid UTF-8 sequence. Otherwise it is
                                // decoded using the system code page (mbcs), with no way to
                                // detect this has happened. Because UTF-8 is the more likely
                                // encoding, and mbcs is inherently unreliable (an mbcs string
                                // that happens to be valid UTF-8 will not be decoded as mbcs)
                                // always recreate the original bytes as UTF-8.
                                if (software.method("startswith", { S<"microsoft-iis/">() }).truthy())
                                {
                                    v = v.method("encode", { S<"utf-8">() }).method("decode", { S<"iso-8859-1">() });
                                }
                                // Apache mod_cgi writes bytes-as-unicode (as if ISO-8859-1) direct
                                // to the Unicode environ. No modification needed.
                                else if (software.method("startswith", { S<"apache/">() }).truthy())
                                {
                                }
                                // Python 3's http.server.CGIHTTPRequestHandler decodes
                                // using the urllib.unquote default of UTF-8, amongst other
                                // issues.
                                else if ((software.method("startswith", { S<"simplehttp/">() }).truthy() && software.contains((S<"python/3">()))))
                                {
                                    v = v.method("encode", { S<"utf-8">() }).method("decode", { S<"iso-8859-1">() });
                                }
                                else
                                {
                                    // For other servers, guess that they have written bytes to
                                    // the environ using stdio byte-oriented interfaces, ending up
                                    // with the system code page.
                                    v = v.method("encode", { enc, S<"replace">() }).method("decode", { S<"iso-8859-1">() });
                                }
                            }
                    #else
                            {
                                // Recover bytes from unicode environ, using surrogate escapes
                                // where available (Python 3.1+).
                                v = v.method("encode", { enc, esc }).method("decode", { S<"iso-8859-1">() });
                            }
                    #endif
                        }
                        environ_.set_item(k, v);
                    }
                    return environ_;
                }));

                // Configuration parameters; can override per-subclass or per-instance
                // os_environ is used to supply configuration from the OS environment:
                // by default it's a copy of 'os.environ' as of import time, but you can
                // override this in e.g. your __init__ method.
                // Collaborator classes
                // Error handling (also per-subclass or per-instance)
                // State variables (don't mess with these)
                // XXX Try for chunked encoding if origin server and client is 1.1
                // XXX else: attempt advanced recovery techniques for HTML or text?
                // Pure abstract methods; *must* be overridden in subclasses
            define("wsgiref.handlers", "BaseHandler", w.make_class("BaseHandler", {  }, {
                { "wsgi_version", py_tuple({ I<1>(), I<0>() }) },
                { "wsgi_multithread", boolean(true) },
                { "wsgi_multiprocess", boolean(true) },
                { "wsgi_run_once", boolean(false) },
                { "origin_server", boolean(true) },
                { "http_version", S<"1.0">() },
                { "server_software", none() },
                { "os_environ", (G<"wsgiref.handlers.read_environ">())() },
                { "wsgi_file_wrapper", Mod<"wsgiref.util">().attr("FileWrapper") },
                { "headers_class", Mod<"wsgiref.headers">().attr("Headers") },
                { "traceback_limit", none() },
                { "error_status", S<"500 Internal Server Error">() },
                { "error_headers", py_list({ py_tuple({ S<"Content-Type">(), S<"text/plain">() }) }) },
                { "error_body", py{ make_bytes(std::string{ "\x41\x20\x73\x65\x72\x76\x65\x72\x20\x65\x72\x72\x6f\x72\x20\x6f\x63\x63\x75\x72\x72\x65\x64\x2e\x20\x20\x50\x6c\x65\x61\x73\x65\x20\x63\x6f\x6e\x74\x61\x63\x74\x20\x74\x68\x65\x20\x61\x64\x6d\x69\x6e\x69\x73\x74\x72\x61\x74\x6f\x72\x2e", 59 }) } },
                { "status", none() },
                { "result", none() },
                { "headers_sent", boolean(false) },
                { "headers", none() },
                { "bytes_sent", I<0>() },
                { "run", w.method("BaseHandler", "run", { py_positional("application") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py application = a[1];
                        // Note to self: don't move the close()!  Asynchronous servers shouldn't
                        // call close() from finish_response(), so if you close() anywhere but
                        // the double-error branch here, you'll break asynchronous servers by
                        // prematurely closing.  Async servers must return from 'run()' without
                        // closing if there might still be output to iterate over.
                        try
                        {
                            self.method("setup_environ");
                            self.set_attr("result", application(self.attr("environ"), self.attr("start_response")));
                            self.method("finish_response");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler5;
                            if (matches(handler5.exception, py_tuple({ B<"ConnectionAbortedError">(), B<"BrokenPipeError">(), B<"ConnectionResetError">() })))
                            {
                                // We expect the client to close the connection abruptly from time
                                // to time.
                                return none();
                            }
                            else
                            {
                                try
                                {
                                    self.method("handle_error");
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler6;
                                    {
                                        // If we get an error handling an error, just give up already!
                                        self.method("close");
                                        py_reraise(handler6.exception);
                                    }
                                }
                            }
                        }
                        return none();
                    }) },
                { "setup_environ", w.method("BaseHandler", "setup_environ", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py env;
                        py const value7 = self.attr("os_environ").method("copy");
                        env = value7;
                        self.set_attr("environ", value7);
                        self.method("add_cgi_vars");
                        env.set_item(S<"wsgi.input">(), self.method("get_stdin"));
                        env.set_item(S<"wsgi.errors">(), self.method("get_stderr"));
                        env.set_item(S<"wsgi.version">(), self.attr("wsgi_version"));
                        env.set_item(S<"wsgi.run_once">(), self.attr("wsgi_run_once"));
                        env.set_item(S<"wsgi.url_scheme">(), self.method("get_scheme"));
                        env.set_item(S<"wsgi.multithread">(), self.attr("wsgi_multithread"));
                        env.set_item(S<"wsgi.multiprocess">(), self.attr("wsgi_multiprocess"));
                        if (!self.attr("wsgi_file_wrapper").is_none())
                        {
                            env.set_item(S<"wsgi.file_wrapper">(), self.attr("wsgi_file_wrapper"));
                        }
                        if ((self.attr("origin_server").truthy() && self.attr("server_software").truthy()))
                        {
                            env.method("setdefault", { S<"SERVER_SOFTWARE">(), self.attr("server_software") });
                        }
                        return none();
                    }) },
                { "finish_response", w.method("BaseHandler", "finish_response", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data;
                        {
                            bool otherwise8 = false;
                            try
                            {
                                if (((!self.method("result_is_file").truthy()) || (!self.method("sendfile").truthy())))
                                {
                                    for (py const& item : iterate{ self.attr("result") })
                                    {
                                        data = item;
                                        self.method("write", { data });
                                    }
                                    self.method("finish_content");
                                }
                                otherwise8 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler8;
                                {
                                    // Call close() on the iterable returned by the WSGI application
                                    // in case of an exception.
                                    if (has_attr(self.attr("result"), "close"))
                                    {
                                        self.attr("result").method("close");
                                    }
                                    py_reraise(handler8.exception);
                                }
                            }
                            if (otherwise8)
                            {
                                // We only call close() when no exception is raised, because it
                                // will set status, result, headers, and environ fields to None.
                                // See bpo-29183 for more details.
                                self.method("close");
                            }
                        }
                        return none();
                    }) },
                { "get_scheme", w.method("BaseHandler", "get_scheme", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (Mod<"wsgiref.util">().attr("guess_scheme"))(self.attr("environ"));
                    }) },
                { "set_content_length", w.method("BaseHandler", "set_content_length", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py blocks;
                        {
                            bool otherwise10 = false;
                            try
                            {
                                blocks = py{ self.attr("result").size() };
                                otherwise10 = true;
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler10;
                                if (matches(handler10.exception, py_tuple({ B<"TypeError">(), B<"AttributeError">(), B<"NotImplementedError">() })))
                                {
                                }
                                else
                                    py_reraise(handler10.exception);
                            }
                            if (otherwise10)
                            {
                                if (blocks == (I<1>()))
                                {
                                    self.attr("headers").set_item(S<"Content-Length">(), (B<"str">())(self.attr("bytes_sent")));
                                    return none();
                                }
                            }
                        }
                        return none();
                    }) },
                { "cleanup_headers", w.method("BaseHandler", "cleanup_headers", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("headers").contains((S<"Content-Length">())))
                        {
                            self.method("set_content_length");
                        }
                        return none();
                    }) },
                { "start_response", w.method("BaseHandler", "start_response", { py_positional("status"), py_positional("headers"), py_positional("exc_info", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py status = a[1];
                        py headers = a[2];
                        py exc_info = a[3];
                        py name;
                        py val;
                        if (exc_info.truthy())
                        {
                            {
                                std::optional<py> pending11;
                                [[maybe_unused]] int flow11 = 0;
                                [[maybe_unused]] py result11;
                                try
                                {
                                    if (self.attr("headers_sent").truthy())
                                    {
                                        py_reraise(handled_exception());
                                    }
                                }
                                catch (raised const&)
                                {
                                    pending11 = py_take_exception();
                                }
                                finally11:
                                exc_info = none();
                                if (pending11)
                                    py_reraise(*pending11);
                                if (flow11 == 1)
                                    return result11;
                            }
                        }
                        else if (!self.attr("headers").is_none())
                        {
                            py_raise((B<"AssertionError">())(S<"Headers already set!">()));
                        }
                        self.set_attr("status", status);
                        self.set_attr("headers", self.method("headers_class", { headers }));
                        status = self.method("_convert_string_type", { status, S<"Status">() });
                        self.method("_validate_status", { status });
                        if ((B<"__debug__">()).truthy())
                        {
                            for (py const& item : iterate{ headers })
                            {
                                auto const items13 = unpack(item, 2);
                                name = items13[0];
                                val = items13[1];
                                name = self.method("_convert_string_type", { name, S<"Header name">() });
                                val = self.method("_convert_string_type", { val, S<"Header value">() });
                                if ((Mod<"wsgiref.util">().attr("is_hop_by_hop"))(name).truthy())
                                    py_raise(B<"AssertionError">()(S(std::string{ "Hop-by-hop header, '" } + name.str() + ": " + val.str() + "', not allowed")));
                            }
                        }
                        return self.attr("write");
                    }) },
                { "_validate_status", w.method("BaseHandler", "_validate_status", { py_positional("status") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py status = a[1];
                        if ((Mod<"wsgiref.headers">().attr("_name_disallowed_re")).method("search", { status }).truthy())
                        {
                            py_raise((B<"ValueError">())(S<"Control characters are not allowed in status">()));
                        }
                        if (py{ status.size() } < (I<4>()))
                        {
                            py_raise((B<"AssertionError">())(S<"Status must be at least 4 characters">()));
                        }
                        if (!status[B<"slice">()(none(), I<3>(), none())].method("isdigit").truthy())
                        {
                            py_raise((B<"AssertionError">())(S<"Status message must begin w/3-digit code">()));
                        }
                        if (status[I<3>()] != (S<" ">()))
                        {
                            py_raise((B<"AssertionError">())(S<"Status message must have a space after code">()));
                        }
                        return none();
                    }) },
                { "_convert_string_type", w.method("BaseHandler", "_convert_string_type", { py_positional("value"), py_positional("title") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py value = a[1];
                        py title = a[2];
                        if ((B<"type">())(value).is((B<"str">())))
                        {
                            return value;
                        }
                        py_raise((B<"AssertionError">())((S<"{0} must be of type str (got {1})">()).method("format", { title, S(value.repr()) })));
                    }) },
                { "send_preamble", w.method("BaseHandler", "send_preamble", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("origin_server").truthy())
                        {
                            if (self.method("client_is_modern").truthy())
                            {
                                self.method("_write", { ((S<"HTTP/%s %s\r\n">()) % py_tuple({ self.attr("http_version"), self.attr("status") })).method("encode", { S<"iso-8859-1">() }) });
                                if (!self.attr("headers").contains((S<"Date">())))
                                {
                                    self.method("_write", { ((S<"Date: %s\r\n">()) % (G<"wsgiref.handlers.format_date_time">())((Mod<"time">().attr("time"))())).method("encode", { S<"iso-8859-1">() }) });
                                }
                                if ((self.attr("server_software").truthy() && (!self.attr("headers").contains((S<"Server">())))))
                                {
                                    self.method("_write", { ((S<"Server: %s\r\n">()) % self.attr("server_software")).method("encode", { S<"iso-8859-1">() }) });
                                }
                            }
                        }
                        else
                        {
                            self.method("_write", { ((S<"Status: %s\r\n">()) % self.attr("status")).method("encode", { S<"iso-8859-1">() }) });
                        }
                        return none();
                    }) },
                { "write", w.method("BaseHandler", "write", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (!(B<"type">())(data).is((B<"bytes">())))
                            py_raise(B<"AssertionError">()(S<"write() argument must be a bytes instance">()));
                        if (!self.attr("status").truthy())
                        {
                            py_raise((B<"AssertionError">())(S<"write() before start_response()">()));
                        }
                        else if (!self.attr("headers_sent").truthy())
                        {
                            // Before the first output, send the stored headers
                            self.set_attr("bytes_sent", py{ data.size() });
                            self.method("send_headers");
                        }
                        else
                        {
                            {
                                py const target14 = self;
                                target14.set_attr("bytes_sent", inplace(NEOS_PY_ADD, target14.attr("bytes_sent"), py{ data.size() }));
                            }
                        }
                        // XXX check Content-Length and truncate if too many bytes written?
                        self.method("_write", { data });
                        self.method("_flush");
                        return none();
                    }) },
                { "sendfile", w.method("BaseHandler", "sendfile", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(false);
                    }) },
                { "finish_content", w.method("BaseHandler", "finish_content", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("headers_sent").truthy())
                        {
                            // Only zero Content-Length if not set by the application (so
                            // that HEAD requests can be satisfied properly, see #3839)
                            self.attr("headers").method("setdefault", { S<"Content-Length">(), S<"0">() });
                            self.method("send_headers");
                        }
                        else
                        {
                        }
                        return none();
                    }) },
                { "close", w.method("BaseHandler", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        {
                            std::optional<py> pending15;
                            [[maybe_unused]] int flow15 = 0;
                            [[maybe_unused]] py result15;
                            try
                            {
                                if (has_attr(self.attr("result"), "close"))
                                {
                                    self.attr("result").method("close");
                                }
                            }
                            catch (raised const&)
                            {
                                pending15 = py_take_exception();
                            }
                            finally15:
                            py const value16 = none();
                            self.set_attr("result", value16);
                            self.set_attr("headers", value16);
                            self.set_attr("status", value16);
                            self.set_attr("environ", value16);
                            self.set_attr("bytes_sent", I<0>());
                            self.set_attr("headers_sent", boolean(false));
                            if (pending15)
                                py_reraise(*pending15);
                            if (flow15 == 1)
                                return result15;
                        }
                        return none();
                    }) },
                { "send_headers", w.method("BaseHandler", "send_headers", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("cleanup_headers");
                        self.set_attr("headers_sent", boolean(true));
                        if (((!self.attr("origin_server").truthy()) || self.method("client_is_modern").truthy()))
                        {
                            self.method("send_preamble");
                            self.method("_write", { (B<"bytes">())(self.attr("headers")) });
                        }
                        return none();
                    }) },
                { "result_is_file", w.method("BaseHandler", "result_is_file", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py wrapper;
                        wrapper = self.attr("wsgi_file_wrapper");
                        return [&]() -> py { py const t = boolean(!wrapper.is_none()); return !t.truthy() ? t : boolean(isinstance(self.attr("result"), wrapper)); }();
                    }) },
                { "client_is_modern", w.method("BaseHandler", "client_is_modern", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self.attr("environ")[S<"SERVER_PROTOCOL">()].method("upper") != (S<"HTTP/0.9">()));
                    }) },
                { "log_exception", w.method("BaseHandler", "log_exception", { py_positional("exc_info") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc_info = a[1];
                        py print_exception;
                        py stderr_;
                        {
                            std::optional<py> pending17;
                            [[maybe_unused]] int flow17 = 0;
                            [[maybe_unused]] py result17;
                            try
                            {
                                print_exception = M("traceback", "print_exception");
                                stderr_ = self.method("get_stderr");
                                print_exception(exc_info[I<0>()], exc_info[I<1>()], exc_info[I<2>()], self.attr("traceback_limit"), stderr_);
                                stderr_.method("flush");
                            }
                            catch (raised const&)
                            {
                                pending17 = py_take_exception();
                            }
                            finally17:
                            exc_info = none();
                            if (pending17)
                                py_reraise(*pending17);
                            if (flow17 == 1)
                                return result17;
                        }
                        return none();
                    }) },
                { "handle_error", w.method("BaseHandler", "handle_error", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("log_exception", { (Mod<"sys">().attr("exc_info"))() });
                        if (!self.attr("headers_sent").truthy())
                        {
                            self.set_attr("result", self.method("error_output", { self.attr("environ"), self.attr("start_response") }));
                            self.method("finish_response");
                        }
                        return none();
                    }) },
                { "error_output", w.method("BaseHandler", "error_output", { py_positional("environ"), py_positional("start_response") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py environ_ = a[1];
                        py start_response = a[2];
                        start_response(self.attr("error_status"), self.attr("error_headers")[B<"slice">()(none(), none(), none())], (Mod<"sys">().attr("exc_info"))());
                        return py_list({ self.attr("error_body") });
                    }) },
                { "_write", w.method("BaseHandler", "_write", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "_flush", w.method("BaseHandler", "_flush", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "get_stdin", w.method("BaseHandler", "get_stdin", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "get_stderr", w.method("BaseHandler", "get_stderr", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) },
                { "add_cgi_vars", w.method("BaseHandler", "add_cgi_vars", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py_raise(B<"NotImplementedError">());
                    }) }, }));

            define("wsgiref.handlers", "SimpleHandler", w.make_class("SimpleHandler", { G<"wsgiref.handlers.BaseHandler">() }, {
                { "__init__", w.method("SimpleHandler", "__init__", { py_positional("stdin"), py_positional("stdout"), py_positional("stderr"), py_positional("environ"), py_positional("multithread", boolean(true)), py_positional("multiprocess", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py stdin_ = a[1];
                        py stdout_ = a[2];
                        py stderr_ = a[3];
                        py environ_ = a[4];
                        py multithread = a[5];
                        py multiprocess = a[6];
                        self.set_attr("stdin", stdin_);
                        self.set_attr("stdout", stdout_);
                        self.set_attr("stderr", stderr_);
                        self.set_attr("base_env", environ_);
                        self.set_attr("wsgi_multithread", multithread);
                        self.set_attr("wsgi_multiprocess", multiprocess);
                        return none();
                    }) },
                { "get_stdin", w.method("SimpleHandler", "get_stdin", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("stdin");
                    }) },
                { "get_stderr", w.method("SimpleHandler", "get_stderr", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("stderr");
                    }) },
                { "add_cgi_vars", w.method("SimpleHandler", "add_cgi_vars", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("environ").method("update", { self.attr("base_env") });
                        return none();
                    }) },
                { "_write", w.method("SimpleHandler", "_write", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py result;
                        py warn;
                        result = self.attr("stdout").method("write", { data });
                        if ((result.is_none() || (result == py{ data.size() })))
                        {
                            return none();
                        }
                        warn = M("warnings", "warn");
                        warn(S<"SimpleHandler.stdout.write() should not do partial writes">(), B<"DeprecationWarning">());
                        while (([&]() -> py { data = data[B<"slice">()(result, none(), none())]; return data; }()).truthy())
                        {
                            result = self.attr("stdout").method("write", { data });
                        }
                        return none();
                    }) },
                { "_flush", w.method("SimpleHandler", "_flush", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("stdout").method("flush");
                        self.set_attr("_flush", self.attr("stdout").attr("flush"));
                        return none();
                    }) }, }));

            define("wsgiref.handlers", "BaseCGIHandler", w.make_class("BaseCGIHandler", { G<"wsgiref.handlers.SimpleHandler">() }, {
                { "origin_server", boolean(false) }, }));

                // Do not allow os.environ to leak between requests in Google App Engine
                // and other multi-run CGI use cases.  This is not easily testable.
                // See http://bugs.python.org/issue7250
            define("wsgiref.handlers", "CGIHandler", w.make_class("CGIHandler", { G<"wsgiref.handlers.BaseCGIHandler">() }, {
                { "wsgi_run_once", boolean(true) },
                { "os_environ", py_dict() },
                { "__init__", w.method("CGIHandler", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        (G<"wsgiref.handlers.BaseCGIHandler">()).method("__init__", { self, (Mod<"sys">().attr("stdin")).attr("buffer"), (Mod<"sys">().attr("stdout")).attr("buffer"), Mod<"sys">().attr("stderr"), (G<"wsgiref.handlers.read_environ">())() }, { { "multithread", boolean(false) }, { "multiprocess", boolean(true) } });
                        return none();
                    }) }, }));

                // By default, IIS gives a PATH_INFO that duplicates the SCRIPT_NAME at
                // the front, causing problems for WSGI applications that wish to implement
                // routing. This handler strips any such duplicated path.
                // IIS can be configured to pass the correct PATH_INFO, but this causes
                // another bug where PATH_TRANSLATED is wrong. Luckily this variable is
                // rarely used and is not guaranteed by WSGI. On IIS<7, though, the
                // setting can only be made on a vhost level, affecting all other script
                // mappings, many of which break when exposed to the PATH_TRANSLATED bug.
                // For this reason IIS<7 is almost never deployed with the fix. (Even IIS7
                // rarely uses it because there is still no UI for it.)
                // There is no way for CGI code to tell whether the option was set, so a
                // separate handler class is provided.
            define("wsgiref.handlers", "IISCGIHandler", w.make_class("IISCGIHandler", { G<"wsgiref.handlers.BaseCGIHandler">() }, {
                { "wsgi_run_once", boolean(true) },
                { "os_environ", py_dict() },
                { "__init__", w.method("IISCGIHandler", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py environ_;
                        py path;
                        py script;
                        environ_ = (G<"wsgiref.handlers.read_environ">())();
                        path = environ_.method("get", { S<"PATH_INFO">(), S<"">() });
                        script = environ_.method("get", { S<"SCRIPT_NAME">(), S<"">() });
                        if ((path + (S<"/">())).method("startswith", { script + (S<"/">()) }).truthy())
                        {
                            environ_.set_item(S<"PATH_INFO">(), path[B<"slice">()(py{ script.size() }, none(), none())]);
                        }
                        (G<"wsgiref.handlers.BaseCGIHandler">()).method("__init__", { self, (Mod<"sys">().attr("stdin")).attr("buffer"), (Mod<"sys">().attr("stdout")).attr("buffer"), Mod<"sys">().attr("stderr"), environ_ }, { { "multithread", boolean(false) }, { "multiprocess", boolean(true) } });
                        return none();
                    }) }, }));
        }

        bool wsgiref_handlers_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_wsgiref_handlers();
                }
                if (auto const doc = wsgiref_handlers_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("FileWrapper", import_attribute("wsgiref.util", "FileWrapper").release());
                aAttributes.emplace_back("guess_scheme", import_attribute("wsgiref.util", "guess_scheme").release());
                aAttributes.emplace_back("is_hop_by_hop", import_attribute("wsgiref.util", "is_hop_by_hop").release());
                aAttributes.emplace_back("Headers", import_attribute("wsgiref.headers", "Headers").release());
                aAttributes.emplace_back("_name_disallowed_re", import_attribute("wsgiref.headers", "_name_disallowed_re").release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("time", py{ fn_import("time") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("wsgiref.handlers", wsgiref_handlers_module);
    }
}
