/*
  http_server.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/http/server.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module http.server (CPython 3.13's Lib/http/server.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "http_server_docs.hpp"

namespace neos::python_runtime::detail::http_server_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "http.server.name"))
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
                py_raise("SystemError", std::string{ "http.server: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"http.server.name">()))
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
            if (modules_imported("http.server"))
                return py{ fn_import("http.server") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "http.server" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "http.server", http_server_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- http.server (Lib/http/server.py)

        void make_http_server()
        {
            static module_writer const& w = writer("http.server");
            // See also:
            // 
            // HTTP Working Group                                        T. Berners-Lee
            // INTERNET-DRAFT                                            R. T. Fielding
            // <draft-ietf-http-v10-spec-00.txt>                     H. Frystyk Nielsen
            // Expires September 8, 1995                                  March 8, 1995
            // 
            // URL: http://www.ics.uci.edu/pub/ietf/http/draft-ietf-http-v10-spec-00.txt
            // 
            // and
            // 
            // Network Working Group                                      R. Fielding
            // Request for Comments: 2616                                       et al
            // Obsoletes: 2068                                              June 1999
            // Category: Standards Track
            // 
            // URL: http://www.faqs.org/rfcs/rfc2616.html
            // Log files
            // ---------
            // 
            // Here's a quote from the NCSA httpd docs about log file format.
            // 
            // | The logfile format is as follows. Each line consists of:
            // |
            // | host rfc931 authuser [DD/Mon/YYYY:hh:mm:ss] "request" ddd bbbb
            // |
            // |        host: Either the DNS name or the IP number of the remote client
            // |        rfc931: Any information returned by identd for this person,
            // |                - otherwise.
            // |        authuser: If user sent a userid for authentication, the user name,
            // |                  - otherwise.
            // |        DD: Day
            // |        Mon: Month (calendar name)
            // |        YYYY: Year
            // |        hh: hour (24-hour format, the machine's timezone)
            // |        mm: minutes
            // |        ss: seconds
            // |        request: The first line of the HTTP request as sent by the client.
            // |        ddd: the status code returned by the server, - if not available.
            // |        bbbb: the total number of bytes sent,
            // |              *not including the HTTP/1.0 header*, - if not available
            // |
            // | You can determine the name of the file accessed through request.
            // 
            // (Actually, the latter is only true if you know the server configuration
            // at the time the request was made!)
            define("http.server", "__version__", S<"0.6">());
            define("http.server", "__all__", py_list({ S<"HTTPServer">(), S<"ThreadingHTTPServer">(), S<"BaseHTTPRequestHandler">(), S<"SimpleHTTPRequestHandler">(), S<"CGIHTTPRequestHandler">() }));
            // Default error message template
            define("http.server", "DEFAULT_ERROR_MESSAGE", S<"<!DOCTYPE HTML>\n<html lang=\"en\">\n    <head>\n        <meta charset=\"utf-8\">\n        <title>Error response</title>\n    </head>\n    <body>\n        <h1>Error response</h1>\n        <p>Error code: %(code)d</p>\n        <p>Message: %(message)s.</p>\n        <p>Error code explanation: %(code)s - %(explain)s.</p>\n    </body>\n</html>\n">());
            define("http.server", "DEFAULT_ERROR_CONTENT_TYPE", S<"text/html;charset=utf-8">());
            // Data larger than this will be read in chunks, to prevent extreme
            // overallocation.
            define("http.server", "_MIN_READ_BUF_SIZE", (I<1>()) << (I<20>()));
            define("http.server", "HTTPServer", w.make_class("HTTPServer", { Mod<"socketserver">().attr("TCPServer") }, {
                { "allow_reuse_address", I<1>() },
                { "server_bind", w.method("HTTPServer", "server_bind", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host;
                        py port;
                        (Mod<"socketserver">().attr("TCPServer")).method("server_bind", { self });
                        auto const items1 = unpack(self.attr("server_address")[B<"slice">()(none(), I<2>(), none())], 2);
                        host = items1[0];
                        port = items1[1];
                        self.set_attr("server_name", (Mod<"socket">().attr("getfqdn"))(host));
                        self.set_attr("server_port", port);
                        return none();
                    }) }, }));

            define("http.server", "ThreadingHTTPServer", w.make_class("ThreadingHTTPServer", { Mod<"socketserver">().attr("ThreadingMixIn"), G<"http.server.HTTPServer">() }, {
                { "daemon_threads", boolean(true) }, }));

                // The Python system version, truncated to its first component.
                // The server software version.  You may want to override this.
                // The format is multiple whitespace-separated strings,
                // where each string is of the form name[/version].
                // The default request version.  This only affects responses up until
                // the point where the request line is parsed, so it mainly decides what
                // the client gets back when sending a malformed request line.
                // Most web servers default to HTTP 0.9, i.e. don't send a status line.
                // https://en.wikipedia.org/wiki/List_of_Unicode_characters#Control_codes
                // Essentially static class variables
                // The version of the HTTP protocol we support.
                // Set this to HTTP/1.1 to enable automatic keepalive
                // MessageClass used to parse headers
                // hack to maintain backwards compatibility
            define("http.server", "BaseHTTPRequestHandler", w.make_class("BaseHTTPRequestHandler", { Mod<"socketserver">().attr("StreamRequestHandler") }, {
                { "sys_version", (S<"Python/">()) + (Mod<"sys">().attr("version")).method("split")[I<0>()] },
                { "server_version", (S<"BaseHTTP/">()) + (G<"http.server.__version__">()) },
                { "error_message_format", G<"http.server.DEFAULT_ERROR_MESSAGE">() },
                { "error_content_type", G<"http.server.DEFAULT_ERROR_CONTENT_TYPE">() },
                { "default_request_version", S<"HTTP/0.9">() },
                { "parse_request", w.method("BaseHTTPRequestHandler", "parse_request", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py base_version_number;
                        py command;
                        py conntype;
                        py err;
                        py expect;
                        py is_http_0_9;
                        py path;
                        py requestline;
                        py version;
                        py version_number;
                        py words;
                        is_http_0_9 = boolean(false);
                        self.set_attr("command", none());
                        py const value2 = self.attr("default_request_version");
                        self.set_attr("request_version", value2);
                        version = value2;
                        self.set_attr("close_connection", boolean(true));
                        requestline = (B<"str">())(self.attr("raw_requestline"), S<"iso-8859-1">());
                        requestline = requestline.method("rstrip", { S<"\r\n">() });
                        self.set_attr("requestline", requestline);
                        words = requestline.method("split");
                        if (py{ words.size() } == (I<0>()))
                        {
                            return boolean(false);
                        }
                        if (py{ words.size() } >= (I<3>()))
                        {
                            version = words[I<-1>()];
                            try
                            {
                                if (!version.method("startswith", { S<"HTTP/">() }).truthy())
                                {
                                    py_raise(B<"ValueError">());
                                }
                                base_version_number = version.method("split", { S<"/">(), I<1>() })[I<1>()];
                                version_number = base_version_number.method("split", { S<".">() });
                                // RFC 2145 section 3.1 says there can be only one "." and
                                // - major and minor numbers MUST be treated as
                                // separate integers;
                                // - HTTP/2.4 is a lower version than HTTP/2.13, which in
                                // turn is lower than HTTP/12.3;
                                // - Leading zeros MUST be ignored by recipients.
                                if (py{ version_number.size() } != (I<2>()))
                                {
                                    py_raise(B<"ValueError">());
                                }
                                if ((B<"any">())([&]() -> py { py const comprehension4 = py_list(); py component; for (py const& item : iterate{ version_number }) { component = item; comprehension4.method("append", { boolean(!component.method("isdigit").truthy()) }); } return comprehension4; }()).truthy())
                                {
                                    py_raise((B<"ValueError">())(S<"non digit in http version">()));
                                }
                                if ((B<"any">())([&]() -> py { py const comprehension5 = py_list(); py component; for (py const& item : iterate{ version_number }) { component = item; comprehension5.method("append", { boolean(py{ component.size() } > (I<10>())) }); } return comprehension5; }()).truthy())
                                {
                                    py_raise((B<"ValueError">())(S<"unreasonable length http version">()));
                                }
                                version_number = py_tuple({ (B<"int">())(version_number[I<0>()]), (B<"int">())(version_number[I<1>()]) });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler3;
                                if (matches(handler3.exception, py_tuple({ B<"ValueError">(), B<"IndexError">() })))
                                {
                                    // Send the error response with a status line and headers.
                                    self.set_attr("request_version", S<"">());
                                    self.method("send_error", { (Mod<"http">().attr("HTTPStatus")).attr("BAD_REQUEST"), (S<"Bad request version (%r)">()) % version });
                                    return boolean(false);
                                }
                                else
                                    py_reraise(handler3.exception);
                            }
                            self.set_attr("request_version", version);
                            if (((version_number >= py_tuple({ I<1>(), I<1>() })) && (self.attr("protocol_version") >= (S<"HTTP/1.1">()))))
                            {
                                self.set_attr("close_connection", boolean(false));
                            }
                            if (version_number >= py_tuple({ I<2>(), I<0>() }))
                            {
                                self.method("send_error", { (Mod<"http">().attr("HTTPStatus")).attr("HTTP_VERSION_NOT_SUPPORTED"), (S<"Invalid HTTP version (%s)">()) % base_version_number });
                                return boolean(false);
                            }
                        }
                        if (!(((I<2>()) <= py{ words.size() }) && (py{ words.size() } <= (I<3>()))))
                        {
                            self.set_attr("request_version", S<"">());
                            self.method("send_error", { (Mod<"http">().attr("HTTPStatus")).attr("BAD_REQUEST"), (S<"Bad request syntax (%r)">()) % requestline });
                            return boolean(false);
                        }
                        auto const items6 = unpack(words[B<"slice">()(none(), I<2>(), none())], 2);
                        command = items6[0];
                        path = items6[1];
                        if (py{ words.size() } == (I<2>()))
                        {
                            self.set_attr("close_connection", boolean(true));
                            if (command != (S<"GET">()))
                            {
                                self.set_attr("request_version", S<"">());
                                self.method("send_error", { (Mod<"http">().attr("HTTPStatus")).attr("BAD_REQUEST"), (S<"Bad HTTP/0.9 request type (%r)">()) % command });
                                return boolean(false);
                            }
                            is_http_0_9 = boolean(true);
                        }
                        auto const items7 = unpack(py_tuple({ command, path }), 2);
                        self.set_attr("command", items7[0]);
                        self.set_attr("path", items7[1]);
                        // gh-87389: The purpose of replacing '//' with '/' is to protect
                        // against open redirect attacks possibly triggered if the path starts
                        // with '//' because http clients treat //path as an absolute URI
                        // without scheme (similar to http://path) rather than a path.
                        if (self.attr("path").method("startswith", { S<"//">() }).truthy())
                        {
                            self.set_attr("path", (S<"/">()) + self.attr("path").method("lstrip", { S<"/">() }));
                        }
                        // For HTTP/0.9, headers are not expected at all.
                        if (is_http_0_9.truthy())
                        {
                            self.set_attr("headers", py_dict());
                            return boolean(true);
                        }
                        // Examine the headers and look for a Connection directive.
                        try
                        {
                            self.set_attr("headers", (Mod<"http.client">().attr("parse_headers")).call({ self.attr("rfile") }, { { "_class", self.attr("MessageClass") } }));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler8;
                            if (matches(handler8.exception, Mod<"http.client">().attr("LineTooLong")))
                            {
                                err = handler8.exception;
                                self.method("send_error", { (Mod<"http">().attr("HTTPStatus")).attr("REQUEST_HEADER_FIELDS_TOO_LARGE"), S<"Line too long">(), (B<"str">())(err) });
                                return boolean(false);
                            }
                            else if (matches(handler8.exception, Mod<"http.client">().attr("HTTPException")))
                            {
                                err = handler8.exception;
                                self.method("send_error", { (Mod<"http">().attr("HTTPStatus")).attr("REQUEST_HEADER_FIELDS_TOO_LARGE"), S<"Too many headers">(), (B<"str">())(err) });
                                return boolean(false);
                            }
                            else
                                py_reraise(handler8.exception);
                        }
                        conntype = self.attr("headers").method("get", { S<"Connection">(), S<"">() });
                        if (conntype.method("lower") == (S<"close">()))
                        {
                            self.set_attr("close_connection", boolean(true));
                        }
                        else if (((conntype.method("lower") == (S<"keep-alive">())) && (self.attr("protocol_version") >= (S<"HTTP/1.1">()))))
                        {
                            self.set_attr("close_connection", boolean(false));
                        }
                        // Examine the headers and look for an Expect directive
                        expect = self.attr("headers").method("get", { S<"Expect">(), S<"">() });
                        if (((expect.method("lower") == (S<"100-continue">())) && (self.attr("protocol_version") >= (S<"HTTP/1.1">())) && (self.attr("request_version") >= (S<"HTTP/1.1">()))))
                        {
                            if (!self.method("handle_expect_100").truthy())
                            {
                                return boolean(false);
                            }
                        }
                        return boolean(true);
                    }) },
                { "handle_expect_100", w.method("BaseHTTPRequestHandler", "handle_expect_100", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("send_response_only", { (Mod<"http">().attr("HTTPStatus")).attr("CONTINUE") });
                        self.method("end_headers");
                        return boolean(true);
                    }) },
                { "handle_one_request", w.method("BaseHTTPRequestHandler", "handle_one_request", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py e;
                        py method;
                        py mname;
                        try
                        {
                            self.set_attr("raw_requestline", self.attr("rfile").method("readline", { I<65537>() }));
                            if (py{ self.attr("raw_requestline").size() } > (I<65536>()))
                            {
                                self.set_attr("requestline", S<"">());
                                self.set_attr("request_version", S<"">());
                                self.set_attr("command", S<"">());
                                self.method("send_error", { (Mod<"http">().attr("HTTPStatus")).attr("REQUEST_URI_TOO_LONG") });
                                return none();
                            }
                            if (!self.attr("raw_requestline").truthy())
                            {
                                self.set_attr("close_connection", boolean(true));
                                return none();
                            }
                            if (!self.method("parse_request").truthy())
                            {
                                // An error code has been sent, just exit
                                return none();
                            }
                            mname = (S<"do_">()) + self.attr("command");
                            if (!(B<"hasattr">())(self, mname).truthy())
                            {
                                self.method("send_error", { (Mod<"http">().attr("HTTPStatus")).attr("NOT_IMPLEMENTED"), (S<"Unsupported method (%r)">()) % self.attr("command") });
                                return none();
                            }
                            method = (B<"getattr">())(self, mname);
                            method();
                            self.attr("wfile").method("flush");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler9;
                            if (matches(handler9.exception, B<"TimeoutError">()))
                            {
                                e = handler9.exception;
                                // a read or a write timed out.  Discard this connection
                                self.method("log_error", { S<"Request timed out: %r">(), e });
                                self.set_attr("close_connection", boolean(true));
                                return none();
                            }
                            else
                                py_reraise(handler9.exception);
                        }
                        return none();
                    }) },
                { "handle", w.method("BaseHTTPRequestHandler", "handle", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("close_connection", boolean(true));
                        self.method("handle_one_request");
                        while (!self.attr("close_connection").truthy())
                        {
                            self.method("handle_one_request");
                        }
                        return none();
                    }) },
                { "send_error", w.method("BaseHTTPRequestHandler", "send_error", { py_positional("code"), py_positional("message", none()), py_positional("explain", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py code = a[1];
                        py message = a[2];
                        py explain = a[3];
                        py body;
                        py content;
                        py longmsg;
                        py shortmsg;
                        try
                        {
                            auto const items12 = unpack(self.attr("responses")[code], 2);
                            shortmsg = items12[0];
                            longmsg = items12[1];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler11;
                            if (matches(handler11.exception, B<"KeyError">()))
                            {
                                auto const items13 = unpack(py_tuple({ S<"?\?\?">(), S<"?\?\?">() }), 2);
                                shortmsg = items13[0];
                                longmsg = items13[1];
                            }
                            else
                                py_reraise(handler11.exception);
                        }
                        if (message.is_none())
                        {
                            message = shortmsg;
                        }
                        if (explain.is_none())
                        {
                            explain = longmsg;
                        }
                        self.method("log_error", { S<"code %d, message %s">(), code, message });
                        self.method("send_response", { code, message });
                        self.method("send_header", { S<"Connection">(), S<"close">() });
                        // Message body is omitted for cases described in:
                        // - RFC7230: 3.3. 1xx, 204(No Content), 304(Not Modified)
                        // - RFC7231: 6.3.6. 205(Reset Content)
                        body = none();
                        if (((code >= (I<200>())) && (!py_tuple({ (Mod<"http">().attr("HTTPStatus")).attr("NO_CONTENT"), (Mod<"http">().attr("HTTPStatus")).attr("RESET_CONTENT"), (Mod<"http">().attr("HTTPStatus")).attr("NOT_MODIFIED") }).contains(code))))
                        {
                            // HTML encode to prevent Cross Site Scripting attacks
                            // (see bug #1100201)
                            content = self.attr("error_message_format") % dict_of({ { S<"code">(), code }, { S<"message">(), (Mod<"html">().attr("escape")).call({ message }, { { "quote", boolean(false) } }) }, { S<"explain">(), (Mod<"html">().attr("escape")).call({ explain }, { { "quote", boolean(false) } }) } });
                            body = content.method("encode", { S<"UTF-8">(), S<"replace">() });
                            self.method("send_header", { S<"Content-Type">(), self.attr("error_content_type") });
                            self.method("send_header", { S<"Content-Length">(), (B<"str">())(py{ body.size() }) });
                        }
                        self.method("end_headers");
                        if (((self.attr("command") != (S<"HEAD">())) && body.truthy()))
                        {
                            self.attr("wfile").method("write", { body });
                        }
                        return none();
                    }) },
                { "send_response", w.method("BaseHTTPRequestHandler", "send_response", { py_positional("code"), py_positional("message", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py code = a[1];
                        py message = a[2];
                        self.method("log_request", { code });
                        self.method("send_response_only", { code, message });
                        self.method("send_header", { S<"Server">(), self.method("version_string") });
                        self.method("send_header", { S<"Date">(), self.method("date_time_string") });
                        return none();
                    }) },
                { "send_response_only", w.method("BaseHTTPRequestHandler", "send_response_only", { py_positional("code"), py_positional("message", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py code = a[1];
                        py message = a[2];
                        if (self.attr("request_version") != (S<"HTTP/0.9">()))
                        {
                            if (message.is_none())
                            {
                                if (self.attr("responses").contains(code))
                                {
                                    message = self.attr("responses")[code][I<0>()];
                                }
                                else
                                {
                                    message = S<"">();
                                }
                            }
                            if (!has_attr(self, "_headers_buffer"))
                            {
                                self.set_attr("_headers_buffer", py_list());
                            }
                            self.attr("_headers_buffer").method("append", { ((S<"%s %d %s\r\n">()) % py_tuple({ self.attr("protocol_version"), code, message })).method("encode", { S<"latin-1">(), S<"strict">() }) });
                        }
                        return none();
                    }) },
                { "send_header", w.method("BaseHTTPRequestHandler", "send_header", { py_positional("keyword"), py_positional("value") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py keyword = a[1];
                        py value = a[2];
                        if (self.attr("request_version") != (S<"HTTP/0.9">()))
                        {
                            if (!has_attr(self, "_headers_buffer"))
                            {
                                self.set_attr("_headers_buffer", py_list());
                            }
                            self.attr("_headers_buffer").method("append", { ((S<"%s: %s\r\n">()) % py_tuple({ keyword, value })).method("encode", { S<"latin-1">(), S<"strict">() }) });
                        }
                        if (keyword.method("lower") == (S<"connection">()))
                        {
                            if (value.method("lower") == (S<"close">()))
                            {
                                self.set_attr("close_connection", boolean(true));
                            }
                            else if (value.method("lower") == (S<"keep-alive">()))
                            {
                                self.set_attr("close_connection", boolean(false));
                            }
                        }
                        return none();
                    }) },
                { "end_headers", w.method("BaseHTTPRequestHandler", "end_headers", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("request_version") != (S<"HTTP/0.9">()))
                        {
                            self.attr("_headers_buffer").method("append", { py{ make_bytes(std::string{ "\x0d\x0a", 2 }) } });
                            self.method("flush_headers");
                        }
                        return none();
                    }) },
                { "flush_headers", w.method("BaseHTTPRequestHandler", "flush_headers", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (has_attr(self, "_headers_buffer"))
                        {
                            self.attr("wfile").method("write", { py{ make_bytes(std::string{ "", 0 }) }.method("join", { self.attr("_headers_buffer") }) });
                            self.set_attr("_headers_buffer", py_list());
                        }
                        return none();
                    }) },
                { "log_request", w.method("BaseHTTPRequestHandler", "log_request", { py_positional("code", S<"-">()), py_positional("size", S<"-">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py code = a[1];
                        py size = a[2];
                        if (isinstance(code, Mod<"http">().attr("HTTPStatus")))
                        {
                            code = code.attr("value");
                        }
                        self.method("log_message", { S<"\"%s\" %s %s">(), self.attr("requestline"), (B<"str">())(code), (B<"str">())(size) });
                        return none();
                    }) },
                { "log_error", w.method("BaseHTTPRequestHandler", "log_error", { py_positional("format") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py format_ = a[1];
                        py args = a[2];
                        call_ex(self.attr("log_message"), { format_ }, args, py{});
                        return none();
                    }, true) },
                { "_control_char_table", (B<"str">()).method("maketrans", { [&]() -> py { py const comprehension14 = py_dict(); py c; for (py const& item : iterate{ (Mod<"itertools">().attr("chain"))((B<"range">())(I<32>()), (B<"range">())(I<127>(), I<160>())) }) { c = item; comprehension14.set_item(c, S(std::string{ "\\x" } + formatted(c, "02x"))); } return comprehension14; }() }) },
                { "_control_char_table", [&]() -> py { py const t = (B<"str">()).method("maketrans", { [&]() -> py { py const comprehension14 = py_dict(); py c; for (py const& item : iterate{ (Mod<"itertools">().attr("chain"))((B<"range">())(I<32>()), (B<"range">())(I<127>(), I<160>())) }) { c = item; comprehension14.set_item(c, S(std::string{ "\\x" } + formatted(c, "02x"))); } return comprehension14; }() }); t.set_item((B<"ord">())(S<"\\">()), S<"\\\\">()); return t; }() },
                { "log_message", w.method("BaseHTTPRequestHandler", "log_message", { py_positional("format") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py format_ = a[1];
                        py args = a[2];
                        py message;
                        message = format_ % args;
                        (Mod<"sys">().attr("stderr")).method("write", { (S<"%s - - [%s] %s\n">()) % py_tuple({ self.method("address_string"), self.method("log_date_time_string"), message.method("translate", { self.attr("_control_char_table") }) }) });
                        return none();
                    }, true) },
                { "version_string", w.method("BaseHTTPRequestHandler", "version_string", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (self.attr("server_version") + (S<" ">())) + self.attr("sys_version");
                    }) },
                { "date_time_string", w.method("BaseHTTPRequestHandler", "date_time_string", { py_positional("timestamp", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py timestamp = a[1];
                        if (timestamp.is_none())
                        {
                            timestamp = (Mod<"time">().attr("time"))();
                        }
                        return (Mod<"email.utils">().attr("formatdate")).call({ timestamp }, { { "usegmt", boolean(true) } });
                    }) },
                { "log_date_time_string", w.method("BaseHTTPRequestHandler", "log_date_time_string", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py day;
                        py hh;
                        py mm;
                        py month;
                        py now;
                        py s;
                        py ss;
                        py x;
                        py y;
                        py year;
                        py z;
                        now = (Mod<"time">().attr("time"))();
                        auto const items15 = unpack((Mod<"time">().attr("localtime"))(now), 9);
                        year = items15[0];
                        month = items15[1];
                        day = items15[2];
                        hh = items15[3];
                        mm = items15[4];
                        ss = items15[5];
                        x = items15[6];
                        y = items15[7];
                        z = items15[8];
                        s = (S<"%02d/%3s/%04d %02d:%02d:%02d">()) % py_tuple({ day, self.attr("monthname")[month], year, hh, mm, ss });
                        return s;
                    }) },
                { "weekdayname", py_list({ S<"Mon">(), S<"Tue">(), S<"Wed">(), S<"Thu">(), S<"Fri">(), S<"Sat">(), S<"Sun">() }) },
                { "monthname", py_list({ none(), S<"Jan">(), S<"Feb">(), S<"Mar">(), S<"Apr">(), S<"May">(), S<"Jun">(), S<"Jul">(), S<"Aug">(), S<"Sep">(), S<"Oct">(), S<"Nov">(), S<"Dec">() }) },
                { "address_string", w.method("BaseHTTPRequestHandler", "address_string", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("client_address")[I<0>()];
                    }) },
                { "protocol_version", S<"HTTP/1.0">() },
                { "MessageClass", Mod<"http.client">().attr("HTTPMessage") },
                { "responses", [&]() -> py { py const comprehension16 = py_dict(); py v; for (py const& item : iterate{ (Mod<"http">().attr("HTTPStatus")).attr("__members__").method("values") }) { v = item; comprehension16.set_item(v, py_tuple({ v.attr("phrase"), v.attr("description") })); } return comprehension16; }() }, }));

            define("http.server", "SimpleHTTPRequestHandler", w.make_class("SimpleHTTPRequestHandler", { G<"http.server.BaseHTTPRequestHandler">() }, {
                { "server_version", (S<"SimpleHTTP/">()) + (G<"http.server.__version__">()) },
                { "index_pages", py_tuple({ S<"index.html">(), S<"index.htm">() }) },
                { "extensions_map", dict_of({ { S<".gz">(), S<"application/gzip">() }, { S<".Z">(), S<"application/octet-stream">() }, { S<".bz2">(), S<"application/x-bzip2">() }, { S<".xz">(), S<"application/x-xz">() } }) },
                { "_encodings_map_default", dict_of({ { S<".gz">(), S<"application/gzip">() }, { S<".Z">(), S<"application/octet-stream">() }, { S<".bz2">(), S<"application/x-bzip2">() }, { S<".xz">(), S<"application/x-xz">() } }) },
                { "__init__", w.method("SimpleHTTPRequestHandler", "__init__", { py_keyword("directory", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py directory = a[1];
                        py args = a[2];
                        py kwargs = a[3];
                        if (directory.is_none())
                        {
                            directory = (Mod<"os">().attr("getcwd"))();
                        }
                        self.set_attr("directory", (Mod<"os">().attr("fspath"))(directory));
                        call_ex((B<"super">()(G<"http.server.SimpleHTTPRequestHandler">(), self)).attr("__init__"), {  }, args, kwargs);
                        return none();
                    }, true, true) },
                { "do_GET", w.method("SimpleHTTPRequestHandler", "do_GET", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f;
                        f = self.method("send_head");
                        if (f.truthy())
                        {
                            {
                                std::optional<py> pending17;
                                [[maybe_unused]] int flow17 = 0;
                                [[maybe_unused]] py result17;
                                try
                                {
                                    self.method("copyfile", { f, self.attr("wfile") });
                                }
                                catch (raised const&)
                                {
                                    pending17 = py_take_exception();
                                }
                                f.method("close");
                                if (pending17)
                                    py_reraise(*pending17);
                                if (flow17 == 1)
                                    return result17;
                            }
                        }
                        return none();
                    }) },
                { "do_HEAD", w.method("SimpleHTTPRequestHandler", "do_HEAD", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py f;
                        f = self.method("send_head");
                        if (f.truthy())
                        {
                            f.method("close");
                        }
                        return none();
                    }) },
                { "send_head", w.method("SimpleHTTPRequestHandler", "send_head", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ctype;
                        py f;
                        py fs;
                        py ims;
                        py index;
                        py last_modif;
                        py new_parts;
                        py new_url;
                        py parts;
                        py path;
                        path = self.method("translate_path", { self.attr("path") });
                        f = none();
                        if ((Mod<"os">().attr("path")).method("isdir", { path }).truthy())
                        {
                            parts = (Mod<"urllib.parse">().attr("urlsplit"))(self.attr("path"));
                            if (!parts.attr("path").method("endswith", { py_tuple({ S<"/">(), S<"%2f">(), S<"%2F">() }) }).truthy())
                            {
                                // redirect browser - doing basically what apache does
                                self.method("send_response", { (Mod<"http">().attr("HTTPStatus")).attr("MOVED_PERMANENTLY") });
                                new_parts = py_tuple({ parts[I<0>()], parts[I<1>()], parts[I<2>()] + (S<"/">()), parts[I<3>()], parts[I<4>()] });
                                new_url = (Mod<"urllib.parse">().attr("urlunsplit"))(new_parts);
                                self.method("send_header", { S<"Location">(), new_url });
                                self.method("send_header", { S<"Content-Length">(), S<"0">() });
                                self.method("end_headers");
                                return none();
                            }
                            bool broke18 = false;
                            for (py const& item : iterate{ self.attr("index_pages") })
                            {
                                index = item;
                                index = (Mod<"os">().attr("path")).method("join", { path, index });
                                if ((Mod<"os">().attr("path")).method("isfile", { index }).truthy())
                                {
                                    path = index;
                                    broke18 = true;
                                    break;
                                }
                            }
                            if (!broke18)
                            {
                                return self.method("list_directory", { path });
                            }
                        }
                        ctype = self.method("guess_type", { path });
                        // check for trailing "/" which should return 404. See Issue17324
                        // The test for this was added in test_httpserver.py
                        // However, some OS platforms accept a trailingSlash as a filename
                        // See discussion on python-dev and Issue34711 regarding
                        // parsing and rejection of filenames with a trailing slash
                        if (path.method("endswith", { S<"/">() }).truthy())
                        {
                            self.method("send_error", { (Mod<"http">().attr("HTTPStatus")).attr("NOT_FOUND"), S<"File not found">() });
                            return none();
                        }
                        try
                        {
                            f = (B<"open">())(path, S<"rb">());
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler19;
                            if (matches(handler19.exception, B<"OSError">()))
                            {
                                self.method("send_error", { (Mod<"http">().attr("HTTPStatus")).attr("NOT_FOUND"), S<"File not found">() });
                                return none();
                            }
                            else
                                py_reraise(handler19.exception);
                        }
                        try
                        {
                            fs = (Mod<"os">().attr("fstat"))(f.method("fileno"));
                            // Use browser cache if possible
                            if ((self.attr("headers").contains((S<"If-Modified-Since">())) && (!self.attr("headers").contains((S<"If-None-Match">())))))
                            {
                                // compare If-Modified-Since and time of last file modification
                                {
                                    bool otherwise21 = false;
                                    try
                                    {
                                        ims = (Mod<"email.utils">().attr("parsedate_to_datetime"))(self.attr("headers")[S<"If-Modified-Since">()]);
                                        otherwise21 = true;
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler21;
                                        if (matches(handler21.exception, py_tuple({ B<"TypeError">(), B<"IndexError">(), B<"OverflowError">(), B<"ValueError">() })))
                                        {
                                            // ignore ill-formed values
                                        }
                                        else
                                            py_reraise(handler21.exception);
                                    }
                                    if (otherwise21)
                                    {
                                        if (ims.attr("tzinfo").is_none())
                                        {
                                            // obsolete format with no timezone, cf.
                                            // https://tools.ietf.org/html/rfc7231#section-7.1.1.1
                                            ims = ims.method("replace", {  }, { { "tzinfo", (Mod<"datetime">().attr("timezone")).attr("utc") } });
                                        }
                                        if (ims.attr("tzinfo").is((Mod<"datetime">().attr("timezone")).attr("utc")))
                                        {
                                            // compare to UTC datetime of last modification
                                            last_modif = (Mod<"datetime">().attr("datetime")).method("fromtimestamp", { fs.attr("st_mtime"), (Mod<"datetime">().attr("timezone")).attr("utc") });
                                            // remove microseconds, like in If-Modified-Since
                                            last_modif = last_modif.method("replace", {  }, { { "microsecond", I<0>() } });
                                            if (last_modif <= ims)
                                            {
                                                self.method("send_response", { (Mod<"http">().attr("HTTPStatus")).attr("NOT_MODIFIED") });
                                                self.method("end_headers");
                                                f.method("close");
                                                return none();
                                            }
                                        }
                                    }
                                }
                            }
                            self.method("send_response", { (Mod<"http">().attr("HTTPStatus")).attr("OK") });
                            self.method("send_header", { S<"Content-type">(), ctype });
                            self.method("send_header", { S<"Content-Length">(), (B<"str">())(fs[I<6>()]) });
                            self.method("send_header", { S<"Last-Modified">(), self.method("date_time_string", { fs.attr("st_mtime") }) });
                            self.method("end_headers");
                            return f;
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler20;
                            {
                                f.method("close");
                                py_reraise(handler20.exception);
                            }
                        }
                        return none();
                    }) },
                { "list_directory", w.method("SimpleHTTPRequestHandler", "list_directory", { py_positional("path") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py displayname;
                        py displaypath;
                        py enc;
                        py encoded;
                        py f;
                        py fullname;
                        py linkname;
                        py list;
                        py name;
                        py r;
                        py title;
                        try
                        {
                            list = (Mod<"os">().attr("listdir"))(path);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler22;
                            if (matches(handler22.exception, B<"OSError">()))
                            {
                                self.method("send_error", { (Mod<"http">().attr("HTTPStatus")).attr("NOT_FOUND"), S<"No permission to list directory">() });
                                return none();
                            }
                            else
                                py_reraise(handler22.exception);
                        }
                        list.method("sort", {  }, { { "key", w.function("<lambda>", "SimpleHTTPRequestHandler.list_directory.<locals>.<lambda>", { py_positional("a") }, [](arguments a) -> py
    {
        py a_ = a[0];
        return a_.method("lower");
    }) } });
                        r = py_list();
                        displaypath = self.attr("path");
                        displaypath = displaypath.method("split", { S<"#">(), I<1>() })[I<0>()];
                        displaypath = displaypath.method("split", { S<"?">(), I<1>() })[I<0>()];
                        try
                        {
                            displaypath = (Mod<"urllib.parse">().attr("unquote")).call({ displaypath }, { { "errors", S<"surrogatepass">() } });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler23;
                            if (matches(handler23.exception, B<"UnicodeDecodeError">()))
                            {
                                displaypath = (Mod<"urllib.parse">().attr("unquote"))(displaypath);
                            }
                            else
                                py_reraise(handler23.exception);
                        }
                        displaypath = (Mod<"html">().attr("escape")).call({ displaypath }, { { "quote", boolean(false) } });
                        enc = (Mod<"sys">().attr("getfilesystemencoding"))();
                        title = S(std::string{ "Directory listing for " } + displaypath.str());
                        r.method("append", { S<"<!DOCTYPE HTML>">() });
                        r.method("append", { S<"<html lang=\"en\">">() });
                        r.method("append", { S<"<head>">() });
                        r.method("append", { S(std::string{ "<meta charset=\"" } + enc.str() + "\">") });
                        r.method("append", { S(std::string{ "<title>" } + title.str() + "</title>\n</head>") });
                        r.method("append", { S(std::string{ "<body>\n<h1>" } + title.str() + "</h1>") });
                        r.method("append", { S<"<hr>\n<ul>">() });
                        for (py const& item : iterate{ list })
                        {
                            name = item;
                            fullname = (Mod<"os">().attr("path")).method("join", { path, name });
                            py const value25 = name;
                            displayname = value25;
                            linkname = value25;
                            // Append / for directories or @ for symbolic links
                            if ((Mod<"os">().attr("path")).method("isdir", { fullname }).truthy())
                            {
                                displayname = name + (S<"/">());
                                linkname = name + (S<"/">());
                            }
                            if ((Mod<"os">().attr("path")).method("islink", { fullname }).truthy())
                            {
                                displayname = name + (S<"@">());
                            }
                            // Note: a link to a directory displays with @ and links with /
                            r.method("append", { (S<"<li><a href=\"%s\">%s</a></li>">()) % py_tuple({ (Mod<"urllib.parse">().attr("quote")).call({ linkname }, { { "errors", S<"surrogatepass">() } }), (Mod<"html">().attr("escape")).call({ displayname }, { { "quote", boolean(false) } }) }) });
                        }
                        r.method("append", { S<"</ul>\n<hr>\n</body>\n</html>\n">() });
                        encoded = (S<"\n">()).method("join", { r }).method("encode", { enc, S<"surrogateescape">() });
                        f = (Mod<"io">().attr("BytesIO"))();
                        f.method("write", { encoded });
                        f.method("seek", { I<0>() });
                        self.method("send_response", { (Mod<"http">().attr("HTTPStatus")).attr("OK") });
                        self.method("send_header", { S<"Content-type">(), (S<"text/html; charset=%s">()) % enc });
                        self.method("send_header", { S<"Content-Length">(), (B<"str">())(py{ encoded.size() }) });
                        self.method("end_headers");
                        return f;
                    }) },
                { "translate_path", w.method("SimpleHTTPRequestHandler", "translate_path", { py_positional("path") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py trailing_slash;
                        py word;
                        py words;
                        // abandon query parameters
                        path = path.method("split", { S<"#">(), I<1>() })[I<0>()];
                        path = path.method("split", { S<"?">(), I<1>() })[I<0>()];
                        // Don't forget explicit trailing slash when normalizing. Issue17324
                        try
                        {
                            path = (Mod<"urllib.parse">().attr("unquote")).call({ path }, { { "errors", S<"surrogatepass">() } });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler26;
                            if (matches(handler26.exception, B<"UnicodeDecodeError">()))
                            {
                                path = (Mod<"urllib.parse">().attr("unquote"))(path);
                            }
                            else
                                py_reraise(handler26.exception);
                        }
                        trailing_slash = path.method("endswith", { S<"/">() });
                        path = (Mod<"posixpath">().attr("normpath"))(path);
                        words = path.method("split", { S<"/">() });
                        words = (B<"filter">())(none(), words);
                        path = self.attr("directory");
                        for (py const& item : iterate{ words })
                        {
                            word = item;
                            if (((Mod<"os">().attr("path")).method("dirname", { word }).truthy() || py_tuple({ Mod<"os">().attr("curdir"), Mod<"os">().attr("pardir") }).contains(word)))
                            {
                                // Ignore components that are not a simple file/directory name
                                continue;
                            }
                            path = (Mod<"os">().attr("path")).method("join", { path, word });
                        }
                        if (trailing_slash.truthy())
                        {
                            path = inplace(NEOS_PY_ADD, path, S<"/">());
                        }
                        return path;
                    }) },
                { "copyfile", w.method("SimpleHTTPRequestHandler", "copyfile", { py_positional("source"), py_positional("outputfile") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py source = a[1];
                        py outputfile = a[2];
                        (Mod<"shutil">().attr("copyfileobj"))(source, outputfile);
                        return none();
                    }) },
                { "guess_type", w.method("SimpleHTTPRequestHandler", "guess_type", { py_positional("path") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py _;
                        py base;
                        py ext;
                        py guess;
                        auto const items28 = unpack((Mod<"posixpath">().attr("splitext"))(path), 2);
                        base = items28[0];
                        ext = items28[1];
                        if (self.attr("extensions_map").contains(ext))
                        {
                            return self.attr("extensions_map")[ext];
                        }
                        ext = ext.method("lower");
                        if (self.attr("extensions_map").contains(ext))
                        {
                            return self.attr("extensions_map")[ext];
                        }
                        auto const items29 = unpack((Mod<"mimetypes">().attr("guess_file_type"))(path), 2);
                        guess = items29[0];
                        _ = items29[1];
                        if (guess.truthy())
                        {
                            return guess;
                        }
                        return S<"application/octet-stream">();
                    }) }, }));

            // Utilities for CGIHTTPRequestHandler
            define("http.server", "_url_collapse_path", w.function("_url_collapse_path", "_url_collapse_path", { py_positional("path") }, [](arguments a) -> py
                {
                    py path = a[0];
                    py _;
                    py collapsed_path;
                    py head_parts;
                    py part;
                    py path_parts;
                    py query;
                    py splitpath;
                    py tail_part;
                    // Query component should not be involved.
                    auto const items30 = unpack(path.method("partition", { S<"?">() }), 3);
                    path = items30[0];
                    _ = items30[1];
                    query = items30[2];
                    path = (Mod<"urllib.parse">().attr("unquote"))(path);
                    // Similar to os.path.split(os.path.normpath(path)) but specific to URL
                    // path semantics rather than local operating system semantics.
                    path_parts = path.method("split", { S<"/">() });
                    head_parts = py_list();
                    for (py const& item : iterate{ path_parts[B<"slice">()(none(), I<-1>(), none())] })
                    {
                        part = item;
                        if (part == (S<"..">()))
                        {
                            head_parts.method("pop");
                        }
                        else if ((part.truthy() && (part != (S<".">()))))
                        {
                            head_parts.method("append", { part });
                        }
                    }
                    if (path_parts.truthy())
                    {
                        tail_part = path_parts.method("pop");
                        if (tail_part.truthy())
                        {
                            if (tail_part == (S<"..">()))
                            {
                                head_parts.method("pop");
                                tail_part = S<"">();
                            }
                            else if (tail_part == (S<".">()))
                            {
                                tail_part = S<"">();
                            }
                        }
                    }
                    else
                    {
                        tail_part = S<"">();
                    }
                    if (query.truthy())
                    {
                        tail_part = (S<"?">()).method("join", { py_tuple({ tail_part, query }) });
                    }
                    splitpath = py_tuple({ (S<"/">()) + (S<"/">()).method("join", { head_parts }), tail_part });
                    collapsed_path = (S<"/">()).method("join", { splitpath });
                    return collapsed_path;
                }));

            define("http.server", "nobody", none());
            define("http.server", "nobody_uid", w.function("nobody_uid", "nobody_uid", {  }, [](arguments a) -> py
                {
                    py pwd;
                    if ((G<"http.server.nobody">()).truthy())
                    {
                        return G<"http.server.nobody">();
                    }
                    try
                    {
                        pwd = module_of("pwd");
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler32;
                        if (matches(handler32.exception, B<"ImportError">()))
                        {
                            return I<-1>();
                        }
                        else
                            py_reraise(handler32.exception);
                    }
                    try
                    {
                        set_global("http.server", "nobody", pwd.method("getpwnam", { S<"nobody">() })[I<2>()]);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler33;
                        if (matches(handler33.exception, B<"KeyError">()))
                        {
                            set_global("http.server", "nobody", (I<1>()) + (B<"max">())([&]() -> py { py const comprehension34 = py_list(); py x; for (py const& item : iterate{ pwd.method("getpwall") }) { x = item; comprehension34.method("append", { x[I<2>()] }); } return comprehension34; }()));
                        }
                        else
                            py_reraise(handler33.exception);
                    }
                    return G<"http.server.nobody">();
                }));

            define("http.server", "executable", w.function("executable", "executable", { py_positional("path") }, [](arguments a) -> py
                {
                    py path = a[0];
                    return (Mod<"os">().attr("access"))(path, Mod<"os">().attr("X_OK"));
                }));

                // Determine platform specifics
                // Make rfile unbuffered -- we need to read one line and then pass
                // the rest to a subprocess, so we can't use buffered input.
            define("http.server", "CGIHTTPRequestHandler", w.make_class("CGIHTTPRequestHandler", { G<"http.server.SimpleHTTPRequestHandler">() }, {
                { "__init__", w.method("CGIHTTPRequestHandler", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        py warnings;
                        warnings = module_of("warnings");
                        warnings.method("_deprecated", { S<"http.server.CGIHTTPRequestHandler">() }, { { "remove", py_tuple({ I<3>(), I<15>() }) } });
                        call_ex((B<"super">()(G<"http.server.CGIHTTPRequestHandler">(), self)).attr("__init__"), {  }, args, kwargs);
                        return none();
                    }, true, true) },
                { "have_fork", (B<"hasattr">())(Mod<"os">(), S<"fork">()) },
                { "rbufsize", I<0>() },
                { "do_POST", w.method("CGIHTTPRequestHandler", "do_POST", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.method("is_cgi").truthy())
                        {
                            self.method("run_cgi");
                        }
                        else
                        {
                            self.method("send_error", { (Mod<"http">().attr("HTTPStatus")).attr("NOT_IMPLEMENTED"), S<"Can only POST to CGI scripts">() });
                        }
                        return none();
                    }) },
                { "send_head", w.method("CGIHTTPRequestHandler", "send_head", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.method("is_cgi").truthy())
                        {
                            return self.method("run_cgi");
                        }
                        else
                        {
                            return (G<"http.server.SimpleHTTPRequestHandler">()).method("send_head", { self });
                        }
                        return none();
                    }) },
                { "is_cgi", w.method("CGIHTTPRequestHandler", "is_cgi", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py collapsed_path;
                        py dir_sep;
                        py head;
                        py tail;
                        collapsed_path = (G<"http.server._url_collapse_path">())(self.attr("path"));
                        dir_sep = collapsed_path.method("find", { S<"/">(), I<1>() });
                        while (((dir_sep > (I<0>())) && (!self.attr("cgi_directories").contains(collapsed_path[B<"slice">()(none(), dir_sep, none())]))))
                        {
                            dir_sep = collapsed_path.method("find", { S<"/">(), dir_sep + (I<1>()) });
                        }
                        if (dir_sep > (I<0>()))
                        {
                            auto const items36 = unpack(py_tuple({ collapsed_path[B<"slice">()(none(), dir_sep, none())], collapsed_path[B<"slice">()(dir_sep + (I<1>()), none(), none())] }), 2);
                            head = items36[0];
                            tail = items36[1];
                            self.set_attr("cgi_info", py_tuple({ head, tail }));
                            return boolean(true);
                        }
                        return boolean(false);
                    }) },
                { "cgi_directories", py_list({ S<"/cgi-bin">(), S<"/htbin">() }) },
                { "is_executable", w.method("CGIHTTPRequestHandler", "is_executable", { py_positional("path") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        return (G<"http.server.executable">())(path);
                    }) },
                { "is_python", w.method("CGIHTTPRequestHandler", "is_python", { py_positional("path") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py path = a[1];
                        py head;
                        py tail;
                        auto const items37 = unpack((Mod<"os">().attr("path")).method("splitext", { path }), 2);
                        head = items37[0];
                        tail = items37[1];
                        return boolean(py_tuple({ S<".py">(), S<".pyw">() }).contains(tail.method("lower")));
                    }) },
                { "run_cgi", w.method("CGIHTTPRequestHandler", "run_cgi", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _;
                        py accept;
                        py args;
                        py authorization;
                        py base64;
                        py binascii;
                        py cmdline;
                        py co;
                        py cookie_str;
                        py cursize;
                        py data;
                        py decoded_query;
                        py delta;
                        py dir;
                        py env;
                        py exitcode;
                        py i;
                        py interp;
                        py ispy;
                        py k;
                        py length;
                        py nbytes;
                        py nextdir;
                        py nextrest;
                        py nobody;
                        py p;
                        py path;
                        py pid;
                        py query;
                        py referer;
                        py rest;
                        py script;
                        py scriptdir;
                        py scriptfile;
                        py scriptname;
                        py status;
                        py stderr_;
                        py stdout_;
                        py sts;
                        py subprocess;
                        py ua;
                        py uqrest;
                        auto const items38 = unpack(self.attr("cgi_info"), 2);
                        dir = items38[0];
                        rest = items38[1];
                        path = (dir + (S<"/">())) + rest;
                        i = path.method("find", { S<"/">(), py{ dir.size() } + (I<1>()) });
                        while (i >= (I<0>()))
                        {
                            nextdir = path[B<"slice">()(none(), i, none())];
                            nextrest = path[B<"slice">()(i + (I<1>()), none(), none())];
                            scriptdir = self.method("translate_path", { nextdir });
                            if ((Mod<"os">().attr("path")).method("isdir", { scriptdir }).truthy())
                            {
                                auto const items40 = unpack(py_tuple({ nextdir, nextrest }), 2);
                                dir = items40[0];
                                rest = items40[1];
                                i = path.method("find", { S<"/">(), py{ dir.size() } + (I<1>()) });
                            }
                            else
                            {
                                break;
                            }
                        }
                        // find an explicit query string, if present.
                        auto const items41 = unpack(rest.method("partition", { S<"?">() }), 3);
                        rest = items41[0];
                        _ = items41[1];
                        query = items41[2];
                        // dissect the part after the directory name into a script name &
                        // a possible additional path, to be stored in PATH_INFO.
                        i = rest.method("find", { S<"/">() });
                        if (i >= (I<0>()))
                        {
                            auto const items42 = unpack(py_tuple({ rest[B<"slice">()(none(), i, none())], rest[B<"slice">()(i, none(), none())] }), 2);
                            script = items42[0];
                            rest = items42[1];
                        }
                        else
                        {
                            auto const items43 = unpack(py_tuple({ rest, S<"">() }), 2);
                            script = items43[0];
                            rest = items43[1];
                        }
                        scriptname = (dir + (S<"/">())) + script;
                        scriptfile = self.method("translate_path", { scriptname });
                        if (!(Mod<"os">().attr("path")).method("exists", { scriptfile }).truthy())
                        {
                            self.method("send_error", { (Mod<"http">().attr("HTTPStatus")).attr("NOT_FOUND"), (S<"No such CGI script (%r)">()) % scriptname });
                            return none();
                        }
                        if (!(Mod<"os">().attr("path")).method("isfile", { scriptfile }).truthy())
                        {
                            self.method("send_error", { (Mod<"http">().attr("HTTPStatus")).attr("FORBIDDEN"), (S<"CGI script is not a plain file (%r)">()) % scriptname });
                            return none();
                        }
                        ispy = self.method("is_python", { scriptname });
                        if ((self.attr("have_fork").truthy() || (!ispy.truthy())))
                        {
                            if (!self.method("is_executable", { scriptfile }).truthy())
                            {
                                self.method("send_error", { (Mod<"http">().attr("HTTPStatus")).attr("FORBIDDEN"), (S<"CGI script is not executable (%r)">()) % scriptname });
                                return none();
                            }
                        }
                        // Reference: https://www6.uniovi.es/~antonio/ncsa_httpd/cgi/env.html
                        // XXX Much of the following could be prepared ahead of time!
                        env = (Mod<"copy">().attr("deepcopy"))(Mod<"os">().attr("environ"));
                        env.set_item(S<"SERVER_SOFTWARE">(), self.method("version_string"));
                        env.set_item(S<"SERVER_NAME">(), self.attr("server").attr("server_name"));
                        env.set_item(S<"GATEWAY_INTERFACE">(), S<"CGI/1.1">());
                        env.set_item(S<"SERVER_PROTOCOL">(), self.attr("protocol_version"));
                        env.set_item(S<"SERVER_PORT">(), (B<"str">())(self.attr("server").attr("server_port")));
                        env.set_item(S<"REQUEST_METHOD">(), self.attr("command"));
                        uqrest = (Mod<"urllib.parse">().attr("unquote"))(rest);
                        env.set_item(S<"PATH_INFO">(), uqrest);
                        env.set_item(S<"PATH_TRANSLATED">(), self.method("translate_path", { uqrest }));
                        env.set_item(S<"SCRIPT_NAME">(), scriptname);
                        env.set_item(S<"QUERY_STRING">(), query);
                        env.set_item(S<"REMOTE_ADDR">(), self.attr("client_address")[I<0>()]);
                        authorization = self.attr("headers").method("get", { S<"authorization">() });
                        if (authorization.truthy())
                        {
                            authorization = authorization.method("split");
                            if (py{ authorization.size() } == (I<2>()))
                            {
                                base64 = module_of("base64");
                                binascii = module_of("binascii");
                                env.set_item(S<"AUTH_TYPE">(), authorization[I<0>()]);
                                if (authorization[I<0>()].method("lower") == (S<"basic">()))
                                {
                                    {
                                        bool otherwise44 = false;
                                        try
                                        {
                                            authorization = authorization[I<1>()].method("encode", { S<"ascii">() });
                                            authorization = base64.method("decodebytes", { authorization }).method("decode", { S<"ascii">() });
                                            otherwise44 = true;
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler44;
                                            if (matches(handler44.exception, py_tuple({ binascii.attr("Error"), B<"UnicodeError">() })))
                                            {
                                            }
                                            else
                                                py_reraise(handler44.exception);
                                        }
                                        if (otherwise44)
                                        {
                                            authorization = authorization.method("split", { S<":">() });
                                            if (py{ authorization.size() } == (I<2>()))
                                            {
                                                env.set_item(S<"REMOTE_USER">(), authorization[I<0>()]);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        // XXX REMOTE_IDENT
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
                        referer = self.attr("headers").method("get", { S<"referer">() });
                        if (referer.truthy())
                        {
                            env.set_item(S<"HTTP_REFERER">(), referer);
                        }
                        accept = self.attr("headers").method("get_all", { S<"accept">(), py_tuple() });
                        env.set_item(S<"HTTP_ACCEPT">(), (S<",">()).method("join", { accept }));
                        ua = self.attr("headers").method("get", { S<"user-agent">() });
                        if (ua.truthy())
                        {
                            env.set_item(S<"HTTP_USER_AGENT">(), ua);
                        }
                        co = (B<"filter">())(none(), self.attr("headers").method("get_all", { S<"cookie">(), py_list() }));
                        cookie_str = (S<", ">()).method("join", { co });
                        if (cookie_str.truthy())
                        {
                            env.set_item(S<"HTTP_COOKIE">(), cookie_str);
                        }
                        // XXX Other HTTP_* headers
                        // Since we're setting the env in the parent, provide empty
                        // values to override previously set values
                        for (py const& item : iterate{ py_tuple({ S<"QUERY_STRING">(), S<"REMOTE_HOST">(), S<"CONTENT_LENGTH">(), S<"HTTP_USER_AGENT">(), S<"HTTP_COOKIE">(), S<"HTTP_REFERER">() }) })
                        {
                            k = item;
                            env.method("setdefault", { k, S<"">() });
                        }
                        self.method("send_response", { (Mod<"http">().attr("HTTPStatus")).attr("OK"), S<"Script output follows">() });
                        self.method("flush_headers");
                        decoded_query = query.method("replace", { S<"+">(), S<" ">() });
                        if (self.attr("have_fork").truthy())
                        {
                            // Unix -- fork as we should
                            args = py_list({ script });
                            if (!decoded_query.contains((S<"=">())))
                            {
                                args.method("append", { decoded_query });
                            }
                            nobody = (G<"http.server.nobody_uid">())();
                            self.attr("wfile").method("flush");
                            pid = (Mod<"os">().attr("fork"))();
                            if (pid != (I<0>()))
                            {
                                // Parent
                                auto const items46 = unpack((Mod<"os">().attr("waitpid"))(pid, I<0>()), 2);
                                pid = items46[0];
                                sts = items46[1];
                                // throw away additional data [see bug #427345]
                                while ((Mod<"select">().attr("select"))(py_list({ self.attr("rfile") }), py_list(), py_list(), I<0>())[I<0>()].truthy())
                                {
                                    if (!self.attr("rfile").method("read", { I<1>() }).truthy())
                                    {
                                        break;
                                    }
                                }
                                exitcode = (Mod<"os">().attr("waitstatus_to_exitcode"))(sts);
                                if (exitcode.truthy())
                                {
                                    self.method("log_error", { S(std::string{ "CGI script exit code " } + exitcode.str()) });
                                }
                                return none();
                            }
                            // Child
                            try
                            {
                                try
                                {
                                    (Mod<"os">().attr("setuid"))(nobody);
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler49;
                                    if (matches(handler49.exception, B<"OSError">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler49.exception);
                                }
                                (Mod<"os">().attr("dup2"))(self.attr("rfile").method("fileno"), I<0>());
                                (Mod<"os">().attr("dup2"))(self.attr("wfile").method("fileno"), I<1>());
                                (Mod<"os">().attr("execve"))(scriptfile, args, env);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler48;
                                {
                                    self.attr("server").method("handle_error", { self.attr("request"), self.attr("client_address") });
                                    (Mod<"os">().attr("_exit"))(I<127>());
                                }
                            }
                        }
                        else
                        {
                            // Non-Unix -- use subprocess
                            subprocess = module_of("subprocess");
                            cmdline = py_list({ scriptfile });
                            if (self.method("is_python", { scriptfile }).truthy())
                            {
                                interp = Mod<"sys">().attr("executable");
                                if (interp.method("lower").method("endswith", { S<"w.exe">() }).truthy())
                                {
                                    // On Windows, use python.exe, not pythonw.exe
                                    interp = interp[B<"slice">()(none(), I<-5>(), none())] + interp[B<"slice">()(I<-4>(), none(), none())];
                                }
                                cmdline = py_list({ interp, S<"-u">() }) + cmdline;
                            }
                            if (!query.contains((S<"=">())))
                            {
                                cmdline.method("append", { query });
                            }
                            self.method("log_message", { S<"command: %s">(), subprocess.method("list2cmdline", { cmdline }) });
                            try
                            {
                                nbytes = (B<"int">())(length);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler50;
                                if (matches(handler50.exception, py_tuple({ B<"TypeError">(), B<"ValueError">() })))
                                {
                                    nbytes = I<0>();
                                }
                                else
                                    py_reraise(handler50.exception);
                            }
                            p = subprocess.method("Popen", { cmdline }, { { "stdin", subprocess.attr("PIPE") }, { "stdout", subprocess.attr("PIPE") }, { "stderr", subprocess.attr("PIPE") }, { "env", env } });
                            if (((self.attr("command").method("lower") == (S<"post">())) && (nbytes > (I<0>()))))
                            {
                                cursize = I<0>();
                                data = self.attr("rfile").method("read", { (B<"min">())(nbytes, G<"http.server._MIN_READ_BUF_SIZE">()) });
                                while (((py{ data.size() } < nbytes) && (py{ data.size() } != cursize)))
                                {
                                    cursize = py{ data.size() };
                                    // This is a geometric increase in read size (never more
                                    // than doubling out the current length of data per loop
                                    // iteration).
                                    delta = (B<"min">())(cursize, nbytes - cursize);
                                    try
                                    {
                                        data = inplace(NEOS_PY_ADD, data, self.attr("rfile").method("read", { delta }));
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler52;
                                        if (matches(handler52.exception, B<"TimeoutError">()))
                                        {
                                            break;
                                        }
                                        else
                                            py_reraise(handler52.exception);
                                    }
                                }
                            }
                            else
                            {
                                data = none();
                            }
                            // throw away additional data [see bug #427345]
                            while ((Mod<"select">().attr("select"))(py_list({ self.attr("rfile").attr("_sock") }), py_list(), py_list(), I<0>())[I<0>()].truthy())
                            {
                                if (!self.attr("rfile").attr("_sock").method("recv", { I<1>() }).truthy())
                                {
                                    break;
                                }
                            }
                            auto const items54 = unpack(p.method("communicate", { data }), 2);
                            stdout_ = items54[0];
                            stderr_ = items54[1];
                            self.attr("wfile").method("write", { stdout_ });
                            if (stderr_.truthy())
                            {
                                self.method("log_error", { S<"%s">(), stderr_ });
                            }
                            p.attr("stderr").method("close");
                            p.attr("stdout").method("close");
                            status = p.attr("returncode");
                            if (status.truthy())
                            {
                                self.method("log_error", { S<"CGI script exit status %#x">(), status });
                            }
                            else
                            {
                                self.method("log_message", { S<"CGI script exited OK">() });
                            }
                        }
                        return none();
                    }) }, }));

            define("http.server", "_get_best_family", w.function("_get_best_family", "_get_best_family", {  }, [](arguments a) -> py
                {
                    py address = a[0];
                    py canonname;
                    py family;
                    py infos;
                    py proto;
                    py sockaddr;
                    py type;
                    infos = call_ex((Mod<"socket">().attr("getaddrinfo")), {  }, address, py{}, { { "type", Mod<"socket">().attr("SOCK_STREAM") }, { "flags", Mod<"socket">().attr("AI_PASSIVE") } });
                    auto const items55 = unpack((B<"next">())((B<"iter">())(infos)), 5);
                    family = items55[0];
                    type = items55[1];
                    proto = items55[2];
                    canonname = items55[3];
                    sockaddr = items55[4];
                    return py_tuple({ family, sockaddr });
                }, true));

            define("http.server", "test", w.function("test", "test", { py_positional("HandlerClass", G<"http.server.BaseHTTPRequestHandler">()), py_positional("ServerClass", G<"http.server.ThreadingHTTPServer">()), py_positional("protocol", S<"HTTP/1.0">()), py_positional("port", I<8000>()), py_positional("bind", none()) }, [](arguments a) -> py
                {
                    py HandlerClass = a[0];
                    py ServerClass = a[1];
                    py protocol = a[2];
                    py port = a[3];
                    py bind = a[4];
                    py addr;
                    py host;
                    py httpd;
                    py url_host;
                    auto const items56 = unpack((G<"http.server._get_best_family">())(bind, port), 2);
                    ServerClass.set_attr("address_family", items56[0]);
                    addr = items56[1];
                    HandlerClass.set_attr("protocol_version", protocol);
                    {
                        py const manager57 = ServerClass(addr, HandlerClass);
                        py const exit57 = pyclass::special_method(manager57, "__exit__");
                        py const value57 = pyclass::special_method(manager57, "__enter__")(manager57);
                        httpd = value57;
                        [[maybe_unused]] int flow57 = 0;
                        [[maybe_unused]] py result57;
                        bool exited57 = false;
                        try
                        {
                            auto const items58 = unpack(httpd.attr("socket").method("getsockname")[B<"slice">()(none(), I<2>(), none())], 2);
                            host = items58[0];
                            port = items58[1];
                            url_host = (host.contains((S<":">())) ? S(std::string{ "[" } + host.str() + "]") : host);
                            (B<"print">())(S(std::string{ "Serving HTTP on " } + host.str() + " port " + port.str() + " (http://" + url_host.str() + ":" + port.str() + "/) ..."));
                            try
                            {
                                httpd.method("serve_forever");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler59;
                                if (matches(handler59.exception, B<"KeyboardInterrupt">()))
                                {
                                    (B<"print">())(S<"\nKeyboard interrupt received, exiting.">());
                                    (Mod<"sys">().attr("exit"))(I<0>());
                                }
                                else
                                    py_reraise(handler59.exception);
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler57;
                            exited57 = true;
                            if (!exit57(manager57, type_of(handler57.exception), handler57.exception, handler57.exception.attr("__traceback__")).truthy())
                                py_reraise(handler57.exception);
                        }
                        if (!exited57)
                            exit57(manager57, none(), none(), none());
                        if (flow57 == 1)
                            return result57;
                    }
                    return none();
                }));

            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool http_server_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_http_server();
                }
                if (auto const doc = http_server_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("copy", py{ fn_import("copy") }.release());
                aAttributes.emplace_back("datetime", py{ fn_import("datetime") }.release());
                aAttributes.emplace_back("email", py{ fn_import("email") }.release());
                aAttributes.emplace_back("html", py{ fn_import("html") }.release());
                aAttributes.emplace_back("http", py{ fn_import("http") }.release());
                aAttributes.emplace_back("io", py{ fn_import("io") }.release());
                aAttributes.emplace_back("itertools", py{ fn_import("itertools") }.release());
                aAttributes.emplace_back("mimetypes", py{ fn_import("mimetypes") }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("posixpath", py{ fn_import("posixpath") }.release());
                aAttributes.emplace_back("select", py{ fn_import("select") }.release());
                aAttributes.emplace_back("shutil", py{ fn_import("shutil") }.release());
                aAttributes.emplace_back("socket", py{ fn_import("socket") }.release());
                aAttributes.emplace_back("socketserver", py{ fn_import("socketserver") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("time", py{ fn_import("time") }.release());
                aAttributes.emplace_back("urllib", py{ fn_import("urllib") }.release());
                aAttributes.emplace_back("HTTPStatus", import_attribute("http", "HTTPStatus").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("http.server", http_server_module);
    }
}
