/*
  http_client.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/http/client.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module http.client (CPython 3.13's Lib/http/client.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "http_client_docs.hpp"

namespace neos::python_runtime::detail::http_client_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "http.client.name"))
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
                py_raise("SystemError", std::string{ "http.client: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"http.client.name">()))
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
            if (modules_imported("http.client"))
                return py{ fn_import("http.client") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "http.client" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "http.client", http_client_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- http.client (Lib/http/client.py)

        void make_http_client()
        {
            static module_writer const& w = writer("http.client");
            // HTTPMessage, parse_headers(), and the HTTP status code constants are
            // intentionally omitted for simplicity
            define("http.client", "__all__", py_list({ S<"HTTPResponse">(), S<"HTTPConnection">(), S<"HTTPException">(), S<"NotConnected">(), S<"UnknownProtocol">(), S<"UnknownTransferEncoding">(), S<"UnimplementedFileMode">(), S<"IncompleteRead">(), S<"InvalidURL">(), S<"ImproperConnectionState">(), S<"CannotSendRequest">(), S<"CannotSendHeader">(), S<"ResponseNotReady">(), S<"BadStatusLine">(), S<"LineTooLong">(), S<"RemoteDisconnected">(), S<"error">(), S<"responses">() }));
            define("http.client", "HTTP_PORT", I<80>());
            define("http.client", "HTTPS_PORT", I<443>());
            define("http.client", "_UNKNOWN", S<"UNKNOWN">());
            // connection states
            define("http.client", "_CS_IDLE", S<"Idle">());
            define("http.client", "_CS_REQ_STARTED", S<"Request-started">());
            define("http.client", "_CS_REQ_SENT", S<"Request-sent">());
            // hack to maintain backwards compatibility
            define("http.client", "CONTINUE", (Mod<"http">().attr("HTTPStatus")).attr("CONTINUE"));
            define("http.client", "SWITCHING_PROTOCOLS", (Mod<"http">().attr("HTTPStatus")).attr("SWITCHING_PROTOCOLS"));
            define("http.client", "PROCESSING", (Mod<"http">().attr("HTTPStatus")).attr("PROCESSING"));
            define("http.client", "EARLY_HINTS", (Mod<"http">().attr("HTTPStatus")).attr("EARLY_HINTS"));
            define("http.client", "OK", (Mod<"http">().attr("HTTPStatus")).attr("OK"));
            define("http.client", "CREATED", (Mod<"http">().attr("HTTPStatus")).attr("CREATED"));
            define("http.client", "ACCEPTED", (Mod<"http">().attr("HTTPStatus")).attr("ACCEPTED"));
            define("http.client", "NON_AUTHORITATIVE_INFORMATION", (Mod<"http">().attr("HTTPStatus")).attr("NON_AUTHORITATIVE_INFORMATION"));
            define("http.client", "NO_CONTENT", (Mod<"http">().attr("HTTPStatus")).attr("NO_CONTENT"));
            define("http.client", "RESET_CONTENT", (Mod<"http">().attr("HTTPStatus")).attr("RESET_CONTENT"));
            define("http.client", "PARTIAL_CONTENT", (Mod<"http">().attr("HTTPStatus")).attr("PARTIAL_CONTENT"));
            define("http.client", "MULTI_STATUS", (Mod<"http">().attr("HTTPStatus")).attr("MULTI_STATUS"));
            define("http.client", "ALREADY_REPORTED", (Mod<"http">().attr("HTTPStatus")).attr("ALREADY_REPORTED"));
            define("http.client", "IM_USED", (Mod<"http">().attr("HTTPStatus")).attr("IM_USED"));
            define("http.client", "MULTIPLE_CHOICES", (Mod<"http">().attr("HTTPStatus")).attr("MULTIPLE_CHOICES"));
            define("http.client", "MOVED_PERMANENTLY", (Mod<"http">().attr("HTTPStatus")).attr("MOVED_PERMANENTLY"));
            define("http.client", "FOUND", (Mod<"http">().attr("HTTPStatus")).attr("FOUND"));
            define("http.client", "SEE_OTHER", (Mod<"http">().attr("HTTPStatus")).attr("SEE_OTHER"));
            define("http.client", "NOT_MODIFIED", (Mod<"http">().attr("HTTPStatus")).attr("NOT_MODIFIED"));
            define("http.client", "USE_PROXY", (Mod<"http">().attr("HTTPStatus")).attr("USE_PROXY"));
            define("http.client", "TEMPORARY_REDIRECT", (Mod<"http">().attr("HTTPStatus")).attr("TEMPORARY_REDIRECT"));
            define("http.client", "PERMANENT_REDIRECT", (Mod<"http">().attr("HTTPStatus")).attr("PERMANENT_REDIRECT"));
            define("http.client", "BAD_REQUEST", (Mod<"http">().attr("HTTPStatus")).attr("BAD_REQUEST"));
            define("http.client", "UNAUTHORIZED", (Mod<"http">().attr("HTTPStatus")).attr("UNAUTHORIZED"));
            define("http.client", "PAYMENT_REQUIRED", (Mod<"http">().attr("HTTPStatus")).attr("PAYMENT_REQUIRED"));
            define("http.client", "FORBIDDEN", (Mod<"http">().attr("HTTPStatus")).attr("FORBIDDEN"));
            define("http.client", "NOT_FOUND", (Mod<"http">().attr("HTTPStatus")).attr("NOT_FOUND"));
            define("http.client", "METHOD_NOT_ALLOWED", (Mod<"http">().attr("HTTPStatus")).attr("METHOD_NOT_ALLOWED"));
            define("http.client", "NOT_ACCEPTABLE", (Mod<"http">().attr("HTTPStatus")).attr("NOT_ACCEPTABLE"));
            define("http.client", "PROXY_AUTHENTICATION_REQUIRED", (Mod<"http">().attr("HTTPStatus")).attr("PROXY_AUTHENTICATION_REQUIRED"));
            define("http.client", "REQUEST_TIMEOUT", (Mod<"http">().attr("HTTPStatus")).attr("REQUEST_TIMEOUT"));
            define("http.client", "CONFLICT", (Mod<"http">().attr("HTTPStatus")).attr("CONFLICT"));
            define("http.client", "GONE", (Mod<"http">().attr("HTTPStatus")).attr("GONE"));
            define("http.client", "LENGTH_REQUIRED", (Mod<"http">().attr("HTTPStatus")).attr("LENGTH_REQUIRED"));
            define("http.client", "PRECONDITION_FAILED", (Mod<"http">().attr("HTTPStatus")).attr("PRECONDITION_FAILED"));
            define("http.client", "CONTENT_TOO_LARGE", (Mod<"http">().attr("HTTPStatus")).attr("CONTENT_TOO_LARGE"));
            define("http.client", "REQUEST_ENTITY_TOO_LARGE", (Mod<"http">().attr("HTTPStatus")).attr("REQUEST_ENTITY_TOO_LARGE"));
            define("http.client", "URI_TOO_LONG", (Mod<"http">().attr("HTTPStatus")).attr("URI_TOO_LONG"));
            define("http.client", "REQUEST_URI_TOO_LONG", (Mod<"http">().attr("HTTPStatus")).attr("REQUEST_URI_TOO_LONG"));
            define("http.client", "UNSUPPORTED_MEDIA_TYPE", (Mod<"http">().attr("HTTPStatus")).attr("UNSUPPORTED_MEDIA_TYPE"));
            define("http.client", "RANGE_NOT_SATISFIABLE", (Mod<"http">().attr("HTTPStatus")).attr("RANGE_NOT_SATISFIABLE"));
            define("http.client", "REQUESTED_RANGE_NOT_SATISFIABLE", (Mod<"http">().attr("HTTPStatus")).attr("REQUESTED_RANGE_NOT_SATISFIABLE"));
            define("http.client", "EXPECTATION_FAILED", (Mod<"http">().attr("HTTPStatus")).attr("EXPECTATION_FAILED"));
            define("http.client", "IM_A_TEAPOT", (Mod<"http">().attr("HTTPStatus")).attr("IM_A_TEAPOT"));
            define("http.client", "MISDIRECTED_REQUEST", (Mod<"http">().attr("HTTPStatus")).attr("MISDIRECTED_REQUEST"));
            define("http.client", "UNPROCESSABLE_CONTENT", (Mod<"http">().attr("HTTPStatus")).attr("UNPROCESSABLE_CONTENT"));
            define("http.client", "UNPROCESSABLE_ENTITY", (Mod<"http">().attr("HTTPStatus")).attr("UNPROCESSABLE_ENTITY"));
            define("http.client", "LOCKED", (Mod<"http">().attr("HTTPStatus")).attr("LOCKED"));
            define("http.client", "FAILED_DEPENDENCY", (Mod<"http">().attr("HTTPStatus")).attr("FAILED_DEPENDENCY"));
            define("http.client", "TOO_EARLY", (Mod<"http">().attr("HTTPStatus")).attr("TOO_EARLY"));
            define("http.client", "UPGRADE_REQUIRED", (Mod<"http">().attr("HTTPStatus")).attr("UPGRADE_REQUIRED"));
            define("http.client", "PRECONDITION_REQUIRED", (Mod<"http">().attr("HTTPStatus")).attr("PRECONDITION_REQUIRED"));
            define("http.client", "TOO_MANY_REQUESTS", (Mod<"http">().attr("HTTPStatus")).attr("TOO_MANY_REQUESTS"));
            define("http.client", "REQUEST_HEADER_FIELDS_TOO_LARGE", (Mod<"http">().attr("HTTPStatus")).attr("REQUEST_HEADER_FIELDS_TOO_LARGE"));
            define("http.client", "UNAVAILABLE_FOR_LEGAL_REASONS", (Mod<"http">().attr("HTTPStatus")).attr("UNAVAILABLE_FOR_LEGAL_REASONS"));
            define("http.client", "INTERNAL_SERVER_ERROR", (Mod<"http">().attr("HTTPStatus")).attr("INTERNAL_SERVER_ERROR"));
            define("http.client", "NOT_IMPLEMENTED", (Mod<"http">().attr("HTTPStatus")).attr("NOT_IMPLEMENTED"));
            define("http.client", "BAD_GATEWAY", (Mod<"http">().attr("HTTPStatus")).attr("BAD_GATEWAY"));
            define("http.client", "SERVICE_UNAVAILABLE", (Mod<"http">().attr("HTTPStatus")).attr("SERVICE_UNAVAILABLE"));
            define("http.client", "GATEWAY_TIMEOUT", (Mod<"http">().attr("HTTPStatus")).attr("GATEWAY_TIMEOUT"));
            define("http.client", "HTTP_VERSION_NOT_SUPPORTED", (Mod<"http">().attr("HTTPStatus")).attr("HTTP_VERSION_NOT_SUPPORTED"));
            define("http.client", "VARIANT_ALSO_NEGOTIATES", (Mod<"http">().attr("HTTPStatus")).attr("VARIANT_ALSO_NEGOTIATES"));
            define("http.client", "INSUFFICIENT_STORAGE", (Mod<"http">().attr("HTTPStatus")).attr("INSUFFICIENT_STORAGE"));
            define("http.client", "LOOP_DETECTED", (Mod<"http">().attr("HTTPStatus")).attr("LOOP_DETECTED"));
            define("http.client", "NOT_EXTENDED", (Mod<"http">().attr("HTTPStatus")).attr("NOT_EXTENDED"));
            define("http.client", "NETWORK_AUTHENTICATION_REQUIRED", (Mod<"http">().attr("HTTPStatus")).attr("NETWORK_AUTHENTICATION_REQUIRED"));
            // another hack to maintain backwards compatibility
            // Mapping status codes to official W3C names
            define("http.client", "responses", [&]() -> py { py const comprehension1 = py_dict(); py v; for (py const& item : iterate{ (Mod<"http">().attr("HTTPStatus")).attr("__members__").method("values") }) { v = item; comprehension1.set_item(v, v.attr("phrase")); } return comprehension1; }());
            // maximal line length when calling readline().
            define("http.client", "_MAXLINE", I<65536>());
            define("http.client", "_MAXHEADERS", I<100>());
            // maximal number of interim (1xx) responses tolerated before the final
            // response.  Real servers send at most a few; without a bound, a server
            // streaming "100 Continue" responses would hang getresponse() forever.
            // A socket timeout cannot detect that as data keeps arriving within every
            // timeout window.
            define("http.client", "_MAXINTERIMRESPONSES", I<100>());
            // Data larger than this will be read in chunks, to prevent extreme
            // overallocation.
            define("http.client", "_MIN_READ_BUF_SIZE", (I<1>()) << (I<20>()));
            // Header name/value ABNF (http://tools.ietf.org/html/rfc7230#section-3.2)
            // 
            // VCHAR          = %x21-7E
            // obs-text       = %x80-FF
            // header-field   = field-name ":" OWS field-value OWS
            // field-name     = token
            // field-value    = *( field-content / obs-fold )
            // field-content  = field-vchar [ 1*( SP / HTAB ) field-vchar ]
            // field-vchar    = VCHAR / obs-text
            // 
            // obs-fold       = CRLF 1*( SP / HTAB )
            // ; obsolete line folding
            // ; see Section 3.2.4
            // token          = 1*tchar
            // 
            // tchar          = "!" / "#" / "$" / "%" / "&" / "'" / "*"
            // / "+" / "-" / "." / "^" / "_" / "`" / "|" / "~"
            // / DIGIT / ALPHA
            // ; any VCHAR, except delimiters
            // 
            // VCHAR defined in http://tools.ietf.org/html/rfc5234#appendix-B.1
            // the patterns for both name and value are more lenient than RFC
            // definitions to allow for backwards compatibility
            define("http.client", "_is_legal_header_name", (Mod<"re">()).method("compile", { py{ make_bytes(std::string{ "\x5b\x5e\x3a\x5c\x73\x5d\x5b\x5e\x3a\x5c\x72\x5c\x6e\x5d\x2a", 15 }) } }).attr("fullmatch"));
            define("http.client", "_is_illegal_header_value", (Mod<"re">()).method("compile", { py{ make_bytes(std::string{ "\x5c\x6e\x28\x3f\x21\x5b\x20\x5c\x74\x5d\x29\x7c\x5c\x72\x28\x3f\x21\x5b\x20\x5c\x74\x5c\x6e\x5d\x29", 25 }) } }).attr("search"));
            // These characters are not allowed within HTTP URL paths.
            // See https://tools.ietf.org/html/rfc3986#section-3.3 and the
            // https://tools.ietf.org/html/rfc3986#appendix-A pchar definition.
            // Prevents CVE-2019-9740.  Includes control characters such as \r\n.
            // We don't restrict chars above \x7f as putrequest() limits us to ASCII.
            define("http.client", "_contains_disallowed_url_pchar_re", (Mod<"re">()).method("compile", { S<"[\u0000- ]">() }));
            // Arguably only these _should_ allowed:
            // _is_allowed_url_pchars_re = re.compile(r"^[/!$&'()*+,;=:@%a-zA-Z0-9._~-]+$")
            // We are more lenient for assumed real world compatibility purposes.
            // These characters are not allowed within HTTP method names
            // to prevent http header injection.
            define("http.client", "_contains_disallowed_method_pchar_re", (Mod<"re">()).method("compile", { S<"[\u0000-\u001f]">() }));
            // We always set the Content-Length header for these methods because some
            // servers will otherwise respond with a 411
            define("http.client", "_METHODS_EXPECTING_BODY", set_of({ S<"PATCH">(), S<"POST">(), S<"PUT">() }));
            define("http.client", "_encode", w.function("_encode", "_encode", { py_positional("data"), py_positional("name", S<"data">()) }, [](arguments a) -> py
                {
                    py data = a[0];
                    py name = a[1];
                    py err;
                    try
                    {
                        return data.method("encode", { S<"latin-1">() });
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler2;
                        if (matches(handler2.exception, B<"UnicodeEncodeError">()))
                        {
                            err = handler2.exception;
                            fn_raise_from((B<"UnicodeEncodeError">())(err.attr("encoding"), err.attr("object"), err.attr("start"), err.attr("end"), (S<"%s (%.20r) is not valid Latin-1. Use %s.encode('utf-8') if you want to send it encoded in UTF-8.">()) % py_tuple({ name.method("title"), data[B<"slice">()(err.attr("start"), err.attr("end"), none())], name })).get(), none().get());
                            throw raised{};
                        }
                        else
                            py_reraise(handler2.exception);
                    }
                    return none();
                }));

            define("http.client", "_strip_ipv6_iface", w.function("_strip_ipv6_iface", "_strip_ipv6_iface", { py_positional("enc_name") }, [](arguments a) -> py
                {
                    py enc_name = a[0];
                    py _;
                    py percent;
                    auto const items3 = unpack(enc_name.method("partition", { py{ make_bytes(std::string{ "\x25", 1 }) } }), 3);
                    enc_name = items3[0];
                    percent = items3[1];
                    _ = items3[2];
                    if (percent.truthy())
                    {
                        if (!enc_name.method("startswith", { py{ make_bytes(std::string{ "\x5b", 1 }) } }).truthy())
                            py_raise(B<"AssertionError">()(enc_name));
                        enc_name = inplace(NEOS_PY_ADD, enc_name, py{ make_bytes(std::string{ "\x5d", 1 }) });
                    }
                    return enc_name;
                }));

                // XXX The only usage of this method is in
                // http.server.CGIHTTPRequestHandler.  Maybe move the code there so
                // that it doesn't need to be part of the public API.  The API has
                // never been defined so this could cause backwards compatibility
                // issues.
            define("http.client", "HTTPMessage", w.make_class("HTTPMessage", { Mod<"email.message">().attr("Message") }, {
                { "getallmatchingheaders", w.method("HTTPMessage", "getallmatchingheaders", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py hit;
                        py line;
                        py lst;
                        py n;
                        name = name.method("lower") + (S<":">());
                        n = py{ name.size() };
                        lst = py_list();
                        hit = I<0>();
                        for (py const& item : iterate{ self.method("keys") })
                        {
                            line = item;
                            if (line[B<"slice">()(none(), n, none())].method("lower") == name)
                            {
                                hit = I<1>();
                            }
                            else if (!line[B<"slice">()(none(), I<1>(), none())].method("isspace").truthy())
                            {
                                hit = I<0>();
                            }
                            if (hit.truthy())
                            {
                                lst.method("append", { line });
                            }
                        }
                        return lst;
                    }) }, }));

            define("http.client", "_read_headers", w.function("_read_headers", "_read_headers", { py_positional("fp") }, [](arguments a) -> py
                {
                    py fp = a[0];
                    py headers;
                    py line;
                    headers = py_list();
                    while (boolean(true).truthy())
                    {
                        line = fp.method("readline", { (G<"http.client._MAXLINE">()) + (I<1>()) });
                        if (py{ line.size() } > (G<"http.client._MAXLINE">()))
                        {
                            py_raise((G<"http.client.LineTooLong">())(S<"header line">()));
                        }
                        headers.method("append", { line });
                        if (py{ headers.size() } > (G<"http.client._MAXHEADERS">()))
                        {
                            py_raise((G<"http.client.HTTPException">())((S<"got more than %d headers">()) % (G<"http.client._MAXHEADERS">())));
                        }
                        if (py_tuple({ py{ make_bytes(std::string{ "\x0d\x0a", 2 }) }, py{ make_bytes(std::string{ "\x0a", 1 }) }, py{ make_bytes(std::string{ "", 0 }) } }).contains(line))
                        {
                            break;
                        }
                    }
                    return headers;
                }));

            define("http.client", "_parse_header_lines", w.function("_parse_header_lines", "_parse_header_lines", { py_positional("header_lines"), py_positional("_class", G<"http.client.HTTPMessage">()) }, [](arguments a) -> py
                {
                    py header_lines = a[0];
                    py _class = a[1];
                    py hstring;
                    hstring = py{ make_bytes(std::string{ "", 0 }) }.method("join", { header_lines }).method("decode", { S<"iso-8859-1">() });
                    return (Mod<"email.parser">().attr("Parser")).call({  }, { { "_class", _class } }).method("parsestr", { hstring });
                }));

            define("http.client", "parse_headers", w.function("parse_headers", "parse_headers", { py_positional("fp"), py_positional("_class", G<"http.client.HTTPMessage">()) }, [](arguments a) -> py
                {
                    py fp = a[0];
                    py _class = a[1];
                    py headers;
                    headers = (G<"http.client._read_headers">())(fp);
                    return (G<"http.client._parse_header_lines">())(headers, _class);
                }));

                // See RFC 2616 sec 19.6 and RFC 1945 sec 6 for details.
                // The bytes from the socket object are iso-8859-1 strings.
                // See RFC 2616 sec 2.2 which notes an exception for MIME-encoded
                // text following RFC 2047.  The basic status line parsing only
                // accepts iso-8859-1.
                // These implementations are for the benefit of io.BufferedReader.
                // XXX This class should probably be revised to act more like
                // the "raw stream" that BufferedReader expects.
                // End of "raw stream" methods
                // We override IOBase.__iter__ so that it doesn't check for closed-ness
                // For compatibility with old-style urllib responses.
            define("http.client", "HTTPResponse", w.make_class("HTTPResponse", { Mod<"io">().attr("BufferedIOBase") }, {
                { "__init__", w.method("HTTPResponse", "__init__", { py_positional("sock"), py_positional("debuglevel", I<0>()), py_positional("method", none()), py_positional("url", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sock = a[1];
                        py debuglevel = a[2];
                        py method = a[3];
                        py url = a[4];
                        self.set_attr("fp", sock.method("makefile", { S<"rb">() }));
                        self.set_attr("debuglevel", debuglevel);
                        self.set_attr("_method", method);
                        // The HTTPResponse object is returned via urllib.  The clients
                        // of http and urllib expect different attributes for the
                        // headers.  headers is used here and supports urllib.  msg is
                        // provided as a backwards compatibility layer for http
                        // clients.
                        py const value6 = none();
                        self.set_attr("headers", value6);
                        self.set_attr("msg", value6);
                        // from the Status-Line of the response
                        self.set_attr("version", G<"http.client._UNKNOWN">());
                        self.set_attr("status", G<"http.client._UNKNOWN">());
                        self.set_attr("reason", G<"http.client._UNKNOWN">());
                        self.set_attr("chunked", G<"http.client._UNKNOWN">());
                        self.set_attr("chunk_left", G<"http.client._UNKNOWN">());
                        self.set_attr("length", G<"http.client._UNKNOWN">());
                        self.set_attr("will_close", G<"http.client._UNKNOWN">());
                        return none();
                    }) },
                { "_read_status", w.method("HTTPResponse", "_read_status", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line;
                        py reason;
                        py status;
                        py version;
                        line = (B<"str">())(self.attr("fp").method("readline", { (G<"http.client._MAXLINE">()) + (I<1>()) }), S<"iso-8859-1">());
                        if (py{ line.size() } > (G<"http.client._MAXLINE">()))
                        {
                            py_raise((G<"http.client.LineTooLong">())(S<"status line">()));
                        }
                        if (self.attr("debuglevel") > (I<0>()))
                        {
                            (B<"print">())(S<"reply:">(), S(line.repr()));
                        }
                        if (!line.truthy())
                        {
                            // Presumably, the server closed the connection before
                            // sending a valid response.
                            py_raise((G<"http.client.RemoteDisconnected">())(S<"Remote end closed connection without response">()));
                        }
                        try
                        {
                            auto const items8 = unpack(line.method("split", { none(), I<2>() }), 3);
                            version = items8[0];
                            status = items8[1];
                            reason = items8[2];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler7;
                            if (matches(handler7.exception, B<"ValueError">()))
                            {
                                try
                                {
                                    auto const items10 = unpack(line.method("split", { none(), I<1>() }), 2);
                                    version = items10[0];
                                    status = items10[1];
                                    reason = S<"">();
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler9;
                                    if (matches(handler9.exception, B<"ValueError">()))
                                    {
                                        // empty version will cause next test to fail.
                                        version = S<"">();
                                    }
                                    else
                                        py_reraise(handler9.exception);
                                }
                            }
                            else
                                py_reraise(handler7.exception);
                        }
                        if (!version.method("startswith", { S<"HTTP/">() }).truthy())
                        {
                            self.method("_close_conn");
                            py_raise((G<"http.client.BadStatusLine">())(line));
                        }
                        // The status code is a three-digit number
                        try
                        {
                            status = (B<"int">())(status);
                            if (((status < (I<100>())) || (status > (I<999>()))))
                            {
                                py_raise((G<"http.client.BadStatusLine">())(line));
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler11;
                            if (matches(handler11.exception, B<"ValueError">()))
                            {
                                py_raise((G<"http.client.BadStatusLine">())(line));
                            }
                            else
                                py_reraise(handler11.exception);
                        }
                        return py_tuple({ version, status, reason });
                    }) },
                { "begin", w.method("HTTPResponse", "begin", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _;
                        py hdr;
                        py length;
                        py reason;
                        py skipped_headers;
                        py status;
                        py tr_enc;
                        py val;
                        py version;
                        if (!self.attr("headers").is_none())
                        {
                            // we've already started reading the response
                            return none();
                        }
                        // read until we get a non-100 response
                        bool broke12 = false;
                        for (py const& item : iterate{ (B<"range">())(G<"http.client._MAXINTERIMRESPONSES">()) })
                        {
                            _ = item;
                            auto const items13 = unpack(self.method("_read_status"), 3);
                            version = items13[0];
                            status = items13[1];
                            reason = items13[2];
                            if (status != (G<"http.client.CONTINUE">()))
                            {
                                broke12 = true;
                                break;
                            }
                            // skip the header from the 100 response
                            skipped_headers = (G<"http.client._read_headers">())(self.attr("fp"));
                            if (self.attr("debuglevel") > (I<0>()))
                            {
                                (B<"print">())(S<"headers:">(), skipped_headers);
                            }
                            skipped_headers = none();
                        }
                        if (!broke12)
                        {
                            py_raise((G<"http.client.HTTPException">())(S(std::string{ "got more than " } + (G<"http.client._MAXINTERIMRESPONSES">()).str() + " interim responses")));
                        }
                        py const value14 = status;
                        self.set_attr("code", value14);
                        self.set_attr("status", value14);
                        self.set_attr("reason", reason.method("strip"));
                        if (py_tuple({ S<"HTTP/1.0">(), S<"HTTP/0.9">() }).contains(version))
                        {
                            // Some servers might still return "0.9", treat it as 1.0 anyway
                            self.set_attr("version", I<10>());
                        }
                        else if (version.method("startswith", { S<"HTTP/1.">() }).truthy())
                        {
                            self.set_attr("version", I<11>());
                        }
                        else
                        {
                            py_raise((G<"http.client.UnknownProtocol">())(version));
                        }
                        py const value15 = (G<"http.client.parse_headers">())(self.attr("fp"));
                        self.set_attr("headers", value15);
                        self.set_attr("msg", value15);
                        if (self.attr("debuglevel") > (I<0>()))
                        {
                            for (py const& item : iterate{ self.attr("headers").method("items") })
                            {
                                auto const items17 = unpack(item, 2);
                                hdr = items17[0];
                                val = items17[1];
                                (B<"print">())(S<"header:">(), hdr + (S<":">()), val);
                            }
                        }
                        // are we using the chunked-style of transfer encoding?
                        tr_enc = self.attr("headers").method("get", { S<"transfer-encoding">() });
                        if ((tr_enc.truthy() && (tr_enc.method("lower") == (S<"chunked">()))))
                        {
                            self.set_attr("chunked", boolean(true));
                            self.set_attr("chunk_left", none());
                        }
                        else
                        {
                            self.set_attr("chunked", boolean(false));
                        }
                        // will the connection close at the end of the response?
                        self.set_attr("will_close", self.method("_check_close"));
                        // do we have a Content-Length?
                        // NOTE: RFC 2616, S4.4, #3 says we ignore this if tr_enc is "chunked"
                        self.set_attr("length", none());
                        length = self.attr("headers").method("get", { S<"content-length">() });
                        if ((length.truthy() && (!self.attr("chunked").truthy())))
                        {
                            {
                                bool otherwise18 = false;
                                try
                                {
                                    self.set_attr("length", (B<"int">())(length));
                                    otherwise18 = true;
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler18;
                                    if (matches(handler18.exception, B<"ValueError">()))
                                    {
                                        self.set_attr("length", none());
                                    }
                                    else
                                        py_reraise(handler18.exception);
                                }
                                if (otherwise18)
                                {
                                    if (self.attr("length") < (I<0>()))
                                    {
                                        self.set_attr("length", none());
                                    }
                                }
                            }
                        }
                        else
                        {
                            self.set_attr("length", none());
                        }
                        // does the body have a fixed length? (of zero)
                        if (((status == (G<"http.client.NO_CONTENT">())) || (status == (G<"http.client.NOT_MODIFIED">())) || (((I<100>()) <= status) && (status < (I<200>()))) || (self.attr("_method") == (S<"HEAD">()))))
                        {
                            self.set_attr("length", I<0>());
                        }
                        // if the connection remains open, and we aren't using chunked, and
                        // a content-length was not provided, then assume that the connection
                        // WILL close.
                        if (((!self.attr("will_close").truthy()) && (!self.attr("chunked").truthy()) && self.attr("length").is_none()))
                        {
                            self.set_attr("will_close", boolean(true));
                        }
                        return none();
                    }) },
                { "_check_close", w.method("HTTPResponse", "_check_close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py conn;
                        py pconn;
                        conn = self.attr("headers").method("get", { S<"connection">() });
                        if (self.attr("version") == (I<11>()))
                        {
                            // An HTTP/1.1 proxy is assumed to stay open unless
                            // explicitly closed.
                            if ((conn.truthy() && conn.method("lower").contains((S<"close">()))))
                            {
                                return boolean(true);
                            }
                            return boolean(false);
                        }
                        // Some HTTP/1.0 implementations have support for persistent
                        // connections, using rules different than HTTP/1.1.
                        // For older HTTP, Keep-Alive indicates persistent connection.
                        if (self.attr("headers").method("get", { S<"keep-alive">() }).truthy())
                        {
                            return boolean(false);
                        }
                        // At least Akamai returns a "Connection: Keep-Alive" header,
                        // which was supposed to be sent by the client.
                        if ((conn.truthy() && conn.method("lower").contains((S<"keep-alive">()))))
                        {
                            return boolean(false);
                        }
                        // Proxy-Connection is a netscape hack.
                        pconn = self.attr("headers").method("get", { S<"proxy-connection">() });
                        if ((pconn.truthy() && pconn.method("lower").contains((S<"keep-alive">()))))
                        {
                            return boolean(false);
                        }
                        // otherwise, assume it will close
                        return boolean(true);
                    }) },
                { "_close_conn", w.method("HTTPResponse", "_close_conn", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp;
                        fp = self.attr("fp");
                        self.set_attr("fp", none());
                        fp.method("close");
                        return none();
                    }) },
                { "close", w.method("HTTPResponse", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        {
                            std::optional<py> pending19;
                            [[maybe_unused]] int flow19 = 0;
                            [[maybe_unused]] py result19;
                            try
                            {
                                (B<"super">()(G<"http.client.HTTPResponse">(), self)).method("close");
                            }
                            catch (raised const&)
                            {
                                pending19 = py_take_exception();
                            }
                            finally19:
                            if (self.attr("fp").truthy())
                            {
                                self.method("_close_conn");
                            }
                            if (pending19)
                                py_reraise(*pending19);
                            if (flow19 == 1)
                                return result19;
                        }
                        return none();
                    }) },
                { "flush", w.method("HTTPResponse", "flush", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        (B<"super">()(G<"http.client.HTTPResponse">(), self)).method("flush");
                        if (self.attr("fp").truthy())
                        {
                            self.attr("fp").method("flush");
                        }
                        return none();
                    }) },
                { "readable", w.method("HTTPResponse", "readable", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(true);
                    }) },
                { "isclosed", w.method("HTTPResponse", "isclosed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        // NOTE: it is possible that we will not ever call self.close(). This
                        // case occurs when will_close is TRUE, length is None, and we
                        // read up to the last byte, but NOT past it.
                        // 
                        // IMPLIES: if will_close is FALSE, then self.close() will ALWAYS be
                        // called, meaning self.isclosed() is meaningful.
                        return boolean(self.attr("fp").is_none());
                    }) },
                { "read", w.method("HTTPResponse", "read", { py_positional("amt", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py amt = a[1];
                        py s;
                        if (self.attr("fp").is_none())
                        {
                            return py{ make_bytes(std::string{ "", 0 }) };
                        }
                        if (self.attr("_method") == (S<"HEAD">()))
                        {
                            self.method("_close_conn");
                            return py{ make_bytes(std::string{ "", 0 }) };
                        }
                        if (self.attr("chunked").truthy())
                        {
                            return self.method("_read_chunked", { amt });
                        }
                        if (((!amt.is_none()) && (amt >= (I<0>()))))
                        {
                            if (((!self.attr("length").is_none()) && (amt > self.attr("length"))))
                            {
                                // clip the read to the "end of response"
                                amt = self.attr("length");
                            }
                            s = self.attr("fp").method("read", { amt });
                            if (((!s.truthy()) && amt.truthy()))
                            {
                                // Ideally, we would raise IncompleteRead if the content-length
                                // wasn't satisfied, but it might break compatibility.
                                self.method("_close_conn");
                            }
                            else if (!self.attr("length").is_none())
                            {
                                {
                                    py const target20 = self;
                                    target20.set_attr("length", inplace(NEOS_PY_SUBTRACT, target20.attr("length"), py{ s.size() }));
                                }
                                if (!self.attr("length").truthy())
                                {
                                    self.method("_close_conn");
                                }
                            }
                            return s;
                        }
                        else
                        {
                            // Amount is not given (unbounded read) so we must check self.length
                            if (self.attr("length").is_none())
                            {
                                s = self.attr("fp").method("read");
                            }
                            else
                            {
                                try
                                {
                                    s = self.method("_safe_read", { self.attr("length") });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler21;
                                    if (matches(handler21.exception, G<"http.client.IncompleteRead">()))
                                    {
                                        self.method("_close_conn");
                                        py_reraise(handler21.exception);
                                    }
                                    else
                                        py_reraise(handler21.exception);
                                }
                                self.set_attr("length", I<0>());
                            }
                            self.method("_close_conn");
                            return s;
                        }
                        return none();
                    }) },
                { "readinto", w.method("HTTPResponse", "readinto", { py_positional("b") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py b = a[1];
                        py n;
                        if (self.attr("fp").is_none())
                        {
                            return I<0>();
                        }
                        if (self.attr("_method") == (S<"HEAD">()))
                        {
                            self.method("_close_conn");
                            return I<0>();
                        }
                        if (self.attr("chunked").truthy())
                        {
                            return self.method("_readinto_chunked", { b });
                        }
                        if (!self.attr("length").is_none())
                        {
                            if (py{ b.size() } > self.attr("length"))
                            {
                                // clip the read to the "end of response"
                                b = (B<"memoryview">())(b)[B<"slice">()(I<0>(), self.attr("length"), none())];
                            }
                        }
                        // we do not use _safe_read() here because this may be a .will_close
                        // connection, and the user is reading more bytes than will be provided
                        // (for example, reading in 1k chunks)
                        n = self.attr("fp").method("readinto", { b });
                        if (((!n.truthy()) && b.truthy()))
                        {
                            // Ideally, we would raise IncompleteRead if the content-length
                            // wasn't satisfied, but it might break compatibility.
                            self.method("_close_conn");
                        }
                        else if (!self.attr("length").is_none())
                        {
                            {
                                py const target22 = self;
                                target22.set_attr("length", inplace(NEOS_PY_SUBTRACT, target22.attr("length"), n));
                            }
                            if (!self.attr("length").truthy())
                            {
                                self.method("_close_conn");
                            }
                        }
                        return n;
                    }) },
                { "_read_next_chunk_size", w.method("HTTPResponse", "_read_next_chunk_size", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i;
                        py line;
                        line = self.attr("fp").method("readline", { (G<"http.client._MAXLINE">()) + (I<1>()) });
                        if (py{ line.size() } > (G<"http.client._MAXLINE">()))
                        {
                            py_raise((G<"http.client.LineTooLong">())(S<"chunk size">()));
                        }
                        i = line.method("find", { py{ make_bytes(std::string{ "\x3b", 1 }) } });
                        if (i >= (I<0>()))
                        {
                            line = line[B<"slice">()(none(), i, none())];
                        }
                        try
                        {
                            return (B<"int">())(line, I<16>());
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler23;
                            if (matches(handler23.exception, B<"ValueError">()))
                            {
                                // close the connection as protocol synchronisation is
                                // probably lost
                                self.method("_close_conn");
                                py_reraise(handler23.exception);
                            }
                            else
                                py_reraise(handler23.exception);
                        }
                        return none();
                    }) },
                { "_read_and_discard_trailer", w.method("HTTPResponse", "_read_and_discard_trailer", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line;
                        py trailers_read;
                        trailers_read = I<0>();
                        while (boolean(true).truthy())
                        {
                            line = self.attr("fp").method("readline", { (G<"http.client._MAXLINE">()) + (I<1>()) });
                            if (py{ line.size() } > (G<"http.client._MAXLINE">()))
                            {
                                py_raise((G<"http.client.LineTooLong">())(S<"trailer line">()));
                            }
                            if (!line.truthy())
                            {
                                // a vanishingly small number of sites EOF without
                                // sending the trailer
                                break;
                            }
                            if (py_tuple({ py{ make_bytes(std::string{ "\x0d\x0a", 2 }) }, py{ make_bytes(std::string{ "\x0a", 1 }) }, py{ make_bytes(std::string{ "", 0 }) } }).contains(line))
                            {
                                break;
                            }
                            // Bound the trailer count just as response headers are bounded.
                            // A server streaming trailer lines forever would otherwise hang
                            // the client; a socket timeout cannot detect that as data keeps
                            // arriving within every timeout window.
                            trailers_read = inplace(NEOS_PY_ADD, trailers_read, I<1>());
                            if (trailers_read > (G<"http.client._MAXHEADERS">()))
                            {
                                py_raise((G<"http.client.HTTPException">())(S(std::string{ "got more than " } + (G<"http.client._MAXHEADERS">()).str() + " trailers")));
                            }
                        }
                        return none();
                    }) },
                { "_get_chunk_left", w.method("HTTPResponse", "_get_chunk_left", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py chunk_left;
                        chunk_left = self.attr("chunk_left");
                        if (!chunk_left.truthy())
                        {
                            if (!chunk_left.is_none())
                            {
                                // We are at the end of chunk, discard chunk end
                                self.method("_safe_read", { I<2>() });
                            }
                            try
                            {
                                chunk_left = self.method("_read_next_chunk_size");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler25;
                                if (matches(handler25.exception, B<"ValueError">()))
                                {
                                    py_raise((G<"http.client.IncompleteRead">())(py{ make_bytes(std::string{ "", 0 }) }));
                                }
                                else
                                    py_reraise(handler25.exception);
                            }
                            if (chunk_left == (I<0>()))
                            {
                                // last chunk: 1*("0") [ chunk-extension ] CRLF
                                self.method("_read_and_discard_trailer");
                                // we read everything; close the "file"
                                self.method("_close_conn");
                                chunk_left = none();
                            }
                            self.set_attr("chunk_left", chunk_left);
                        }
                        return chunk_left;
                    }) },
                { "_read_chunked", w.method("HTTPResponse", "_read_chunked", { py_positional("amt", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py amt = a[1];
                        py chunk_left;
                        py exc;
                        py value;
                        if (!(self.attr("chunked") != (G<"http.client._UNKNOWN">())))
                            py_raise(B<"AssertionError">()());
                        if (((!amt.is_none()) && (amt < (I<0>()))))
                        {
                            amt = none();
                        }
                        value = py_list();
                        try
                        {
                            while (!([&]() -> py { chunk_left = self.method("_get_chunk_left"); return chunk_left; }()).is_none())
                            {
                                if (((!amt.is_none()) && (amt <= chunk_left)))
                                {
                                    value.method("append", { self.method("_safe_read", { amt }) });
                                    self.set_attr("chunk_left", chunk_left - amt);
                                    break;
                                }
                                value.method("append", { self.method("_safe_read", { chunk_left }) });
                                if (!amt.is_none())
                                {
                                    amt = inplace(NEOS_PY_SUBTRACT, amt, chunk_left);
                                }
                                self.set_attr("chunk_left", I<0>());
                            }
                            return py{ make_bytes(std::string{ "", 0 }) }.method("join", { value });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler26;
                            if (matches(handler26.exception, G<"http.client.IncompleteRead">()))
                            {
                                exc = handler26.exception;
                                fn_raise_from((G<"http.client.IncompleteRead">())(py{ make_bytes(std::string{ "", 0 }) }.method("join", { value })).get(), exc.get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler26.exception);
                        }
                        return none();
                    }) },
                { "_readinto_chunked", w.method("HTTPResponse", "_readinto_chunked", { py_positional("b") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py b = a[1];
                        py chunk_left;
                        py mvb;
                        py n;
                        py temp_mvb;
                        py total_bytes;
                        if (!(self.attr("chunked") != (G<"http.client._UNKNOWN">())))
                            py_raise(B<"AssertionError">()());
                        total_bytes = I<0>();
                        mvb = (B<"memoryview">())(b);
                        try
                        {
                            while (boolean(true).truthy())
                            {
                                chunk_left = self.method("_get_chunk_left");
                                if (chunk_left.is_none())
                                {
                                    return total_bytes;
                                }
                                if (py{ mvb.size() } <= chunk_left)
                                {
                                    n = self.method("_safe_readinto", { mvb });
                                    self.set_attr("chunk_left", chunk_left - n);
                                    return total_bytes + n;
                                }
                                temp_mvb = mvb[B<"slice">()(none(), chunk_left, none())];
                                n = self.method("_safe_readinto", { temp_mvb });
                                mvb = mvb[B<"slice">()(n, none(), none())];
                                total_bytes = inplace(NEOS_PY_ADD, total_bytes, n);
                                self.set_attr("chunk_left", I<0>());
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler28;
                            if (matches(handler28.exception, G<"http.client.IncompleteRead">()))
                            {
                                py_raise((G<"http.client.IncompleteRead">())((B<"bytes">())(b[B<"slice">()(I<0>(), total_bytes, none())])));
                            }
                            else
                                py_reraise(handler28.exception);
                        }
                        return none();
                    }) },
                { "_safe_read", w.method("HTTPResponse", "_safe_read", { py_positional("amt") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py amt = a[1];
                        py cursize;
                        py data;
                        py delta;
                        cursize = (B<"min">())(amt, G<"http.client._MIN_READ_BUF_SIZE">());
                        data = self.attr("fp").method("read", { cursize });
                        if (py{ data.size() } >= amt)
                        {
                            return data;
                        }
                        if (py{ data.size() } < cursize)
                        {
                            py_raise((G<"http.client.IncompleteRead">())(data, amt - py{ data.size() }));
                        }
                        data = (Mod<"io">().attr("BytesIO"))(data);
                        data.method("seek", { I<0>(), I<2>() });
                        while (boolean(true).truthy())
                        {
                            // This is a geometric increase in read size (never more than
                            // doubling out the current length of data per loop iteration).
                            delta = (B<"min">())(cursize, amt - cursize);
                            data.method("write", { self.attr("fp").method("read", { delta }) });
                            if (data.method("tell") >= amt)
                            {
                                return data.method("getvalue");
                            }
                            cursize = inplace(NEOS_PY_ADD, cursize, delta);
                            if (data.method("tell") < cursize)
                            {
                                py_raise((G<"http.client.IncompleteRead">())(data.method("getvalue"), amt - data.method("tell")));
                            }
                        }
                        return none();
                    }) },
                { "_safe_readinto", w.method("HTTPResponse", "_safe_readinto", { py_positional("b") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py b = a[1];
                        py amt;
                        py n;
                        amt = py{ b.size() };
                        n = self.attr("fp").method("readinto", { b });
                        if (n < amt)
                        {
                            py_raise((G<"http.client.IncompleteRead">())((B<"bytes">())(b[B<"slice">()(none(), n, none())]), amt - n));
                        }
                        return n;
                    }) },
                { "read1", w.method("HTTPResponse", "read1", { py_positional("n", I<-1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n = a[1];
                        py result;
                        if ((self.attr("fp").is_none() || (self.attr("_method") == (S<"HEAD">()))))
                        {
                            return py{ make_bytes(std::string{ "", 0 }) };
                        }
                        if (self.attr("chunked").truthy())
                        {
                            return self.method("_read1_chunked", { n });
                        }
                        if (((!self.attr("length").is_none()) && ([&]() -> py { py const t = boolean(n < (I<0>())); return t.truthy() ? t : boolean(n > self.attr("length")); }()).truthy()))
                        {
                            n = self.attr("length");
                        }
                        result = self.attr("fp").method("read1", { n });
                        if (((!result.truthy()) && n.truthy()))
                        {
                            self.method("_close_conn");
                        }
                        else if (!self.attr("length").is_none())
                        {
                            {
                                py const target31 = self;
                                target31.set_attr("length", inplace(NEOS_PY_SUBTRACT, target31.attr("length"), py{ result.size() }));
                            }
                            if (!self.attr("length").truthy())
                            {
                                self.method("_close_conn");
                            }
                        }
                        return result;
                    }) },
                { "peek", w.method("HTTPResponse", "peek", { py_positional("n", I<-1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n = a[1];
                        if ((self.attr("fp").is_none() || (self.attr("_method") == (S<"HEAD">()))))
                        {
                            return py{ make_bytes(std::string{ "", 0 }) };
                        }
                        if (self.attr("chunked").truthy())
                        {
                            return self.method("_peek_chunked", { n });
                        }
                        return self.attr("fp").method("peek", { n });
                    }) },
                { "readline", w.method("HTTPResponse", "readline", { py_positional("limit", I<-1>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py limit = a[1];
                        py result;
                        if ((self.attr("fp").is_none() || (self.attr("_method") == (S<"HEAD">()))))
                        {
                            return py{ make_bytes(std::string{ "", 0 }) };
                        }
                        if (self.attr("chunked").truthy())
                        {
                            // Fallback to IOBase readline which uses peek() and read()
                            return (B<"super">()(G<"http.client.HTTPResponse">(), self)).method("readline", { limit });
                        }
                        if (((!self.attr("length").is_none()) && ([&]() -> py { py const t = boolean(limit < (I<0>())); return t.truthy() ? t : boolean(limit > self.attr("length")); }()).truthy()))
                        {
                            limit = self.attr("length");
                        }
                        result = self.attr("fp").method("readline", { limit });
                        if (((!result.truthy()) && limit.truthy()))
                        {
                            self.method("_close_conn");
                        }
                        else if (!self.attr("length").is_none())
                        {
                            {
                                py const target32 = self;
                                target32.set_attr("length", inplace(NEOS_PY_SUBTRACT, target32.attr("length"), py{ result.size() }));
                            }
                            if (!self.attr("length").truthy())
                            {
                                self.method("_close_conn");
                            }
                        }
                        return result;
                    }) },
                { "_read1_chunked", w.method("HTTPResponse", "_read1_chunked", { py_positional("n") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n = a[1];
                        py chunk_left;
                        py read;
                        chunk_left = self.method("_get_chunk_left");
                        if ((chunk_left.is_none() || (n == (I<0>()))))
                        {
                            return py{ make_bytes(std::string{ "", 0 }) };
                        }
                        if (!(((I<0>()) <= n) && (n <= chunk_left)))
                        {
                            n = chunk_left;
                        }
                        read = self.attr("fp").method("read1", { n });
                        {
                            py const target33 = self;
                            target33.set_attr("chunk_left", inplace(NEOS_PY_SUBTRACT, target33.attr("chunk_left"), py{ read.size() }));
                        }
                        if (!read.truthy())
                        {
                            py_raise((G<"http.client.IncompleteRead">())(py{ make_bytes(std::string{ "", 0 }) }));
                        }
                        return read;
                    }) },
                { "_peek_chunked", w.method("HTTPResponse", "_peek_chunked", { py_positional("n") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py n = a[1];
                        py chunk_left;
                        try
                        {
                            chunk_left = self.method("_get_chunk_left");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler34;
                            if (matches(handler34.exception, G<"http.client.IncompleteRead">()))
                            {
                                return py{ make_bytes(std::string{ "", 0 }) };
                            }
                            else
                                py_reraise(handler34.exception);
                        }
                        if (chunk_left.is_none())
                        {
                            return py{ make_bytes(std::string{ "", 0 }) };
                        }
                        // peek is allowed to return more than requested.  Just request the
                        // entire chunk, and truncate what we get.
                        return self.attr("fp").method("peek", { chunk_left })[B<"slice">()(none(), chunk_left, none())];
                    }) },
                { "fileno", w.method("HTTPResponse", "fileno", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("fp").method("fileno");
                    }) },
                { "getheader", w.method("HTTPResponse", "getheader", { py_positional("name"), py_positional("default", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py default_ = a[2];
                        py headers;
                        if (self.attr("headers").is_none())
                        {
                            py_raise((G<"http.client.ResponseNotReady">())());
                        }
                        headers = [&]() -> py { py const t = self.attr("headers").method("get_all", { name }); return t.truthy() ? t : default_; }();
                        if ((isinstance(headers, B<"str">()) || (!has_attr(headers, "__iter__"))))
                        {
                            return headers;
                        }
                        else
                        {
                            return (S<", ">()).method("join", { headers });
                        }
                        return none();
                    }) },
                { "getheaders", w.method("HTTPResponse", "getheaders", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("headers").is_none())
                        {
                            py_raise((G<"http.client.ResponseNotReady">())());
                        }
                        return (B<"list">())(self.attr("headers").method("items"));
                    }) },
                { "__iter__", w.method("HTTPResponse", "__iter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "info", w.method("HTTPResponse", "info", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("headers");
                    }) },
                { "geturl", w.method("HTTPResponse", "geturl", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("url");
                    }) },
                { "getcode", w.method("HTTPResponse", "getcode", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("status");
                    }) }, }));

            define("http.client", "_create_https_context", w.function("_create_https_context", "_create_https_context", { py_positional("http_version") }, [](arguments a) -> py
                {
                    py http_version = a[0];
                    py context;
                    context = (Mod<"ssl">().attr("_create_default_https_context"))();
                    // send ALPN extension to indicate HTTP/1.1 protocol
                    if (http_version == (I<11>()))
                    {
                        context.method("set_alpn_protocols", { py_list({ S<"http/1.1">() }) });
                    }
                    // enable PHA for TLS 1.3 connections if available
                    if (!context.attr("post_handshake_auth").is_none())
                    {
                        context.set_attr("post_handshake_auth", boolean(true));
                    }
                    return context;
                }));

            define("http.client", "HTTPConnection", w.make_class("HTTPConnection", {  }, {
                { "_http_vsn", I<11>() },
                { "_http_vsn_str", S<"HTTP/1.1">() },
                { "response_class", G<"http.client.HTTPResponse">() },
                { "default_port", G<"http.client.HTTP_PORT">() },
                { "auto_open", I<1>() },
                { "debuglevel", I<0>() },
                { "_is_textIO", w.static_method("HTTPConnection", "_is_textIO", { py_positional("stream") }, [](arguments a) -> py
                    {
                        py stream = a[0];
                        return boolean(isinstance(stream, Mod<"io">().attr("TextIOBase")));
                    }) },
                { "_get_content_length", w.static_method("HTTPConnection", "_get_content_length", { py_positional("body"), py_positional("method") }, [](arguments a) -> py
                    {
                        py body = a[0];
                        py method = a[1];
                        py mv;
                        if (body.is_none())
                        {
                            // do an explicit check for not None here to distinguish
                            // between unset and set but empty
                            if ((G<"http.client._METHODS_EXPECTING_BODY">()).contains(method.method("upper")))
                            {
                                return I<0>();
                            }
                            else
                            {
                                return none();
                            }
                        }
                        if (has_attr(body, "read"))
                        {
                            // file-like object.
                            return none();
                        }
                        try
                        {
                            // does it implement the buffer protocol (bytes, bytearray, array)?
                            mv = (B<"memoryview">())(body);
                            return mv.attr("nbytes");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler35;
                            if (matches(handler35.exception, B<"TypeError">()))
                            {
                            }
                            else
                                py_reraise(handler35.exception);
                        }
                        if (isinstance(body, B<"str">()))
                        {
                            return py{ body.size() };
                        }
                        return none();
                    }) },
                { "__init__", w.method("HTTPConnection", "__init__", { py_positional("host"), py_positional("port", none()), py_positional("timeout", Mod<"socket">().attr("_GLOBAL_DEFAULT_TIMEOUT")), py_positional("source_address", none()), py_positional("blocksize", I<8192>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py port = a[2];
                        py timeout = a[3];
                        py source_address = a[4];
                        py blocksize = a[5];
                        self.set_attr("timeout", timeout);
                        self.set_attr("source_address", source_address);
                        self.set_attr("blocksize", blocksize);
                        self.set_attr("sock", none());
                        self.set_attr("_buffer", py_list());
                        self.set_attr("_HTTPConnection__response", none());
                        self.set_attr("_HTTPConnection__state", G<"http.client._CS_IDLE">());
                        self.set_attr("_method", none());
                        self.set_attr("_tunnel_host", none());
                        self.set_attr("_tunnel_port", none());
                        self.set_attr("_tunnel_headers", py_dict());
                        self.set_attr("_raw_proxy_headers", none());
                        auto const items36 = unpack(self.method("_get_hostport", { host, port }), 2);
                        self.set_attr("host", items36[0]);
                        self.set_attr("port", items36[1]);
                        self.method("_validate_host", { self.attr("host") });
                        // This is stored as an instance variable to allow unit
                        // tests to replace it with a suitable mockup
                        self.set_attr("_create_connection", Mod<"socket">().attr("create_connection"));
                        return none();
                    }) },
                { "set_tunnel", w.method("HTTPConnection", "set_tunnel", { py_positional("host"), py_positional("port", none()), py_positional("headers", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py port = a[2];
                        py headers = a[3];
                        py encoded_host;
                        if (self.attr("sock").truthy())
                        {
                            py_raise((B<"RuntimeError">())(S<"Can't set up tunnel for established connection">()));
                        }
                        auto const items37 = unpack(self.method("_get_hostport", { host, port }), 2);
                        self.set_attr("_tunnel_host", items37[0]);
                        self.set_attr("_tunnel_port", items37[1]);
                        if (headers.truthy())
                        {
                            self.set_attr("_tunnel_headers", headers.method("copy"));
                        }
                        else
                        {
                            self.attr("_tunnel_headers").method("clear");
                        }
                        if (!(B<"any">())([&]() -> py { py const comprehension38 = py_list(); py header; for (py const& item : iterate{ self.attr("_tunnel_headers") }) { header = item; comprehension38.method("append", { boolean(header.method("lower") == (S<"host">())) }); } return comprehension38; }()).truthy())
                        {
                            encoded_host = self.attr("_tunnel_host").method("encode", { S<"idna">() }).method("decode", { S<"ascii">() });
                            self.attr("_tunnel_headers").set_item(S<"Host">(), (S<"%s:%d">()) % py_tuple({ encoded_host, self.attr("_tunnel_port") }));
                        }
                        return none();
                    }) },
                { "_get_hostport", w.method("HTTPConnection", "_get_hostport", { py_positional("host"), py_positional("port") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py port = a[2];
                        py i;
                        py j;
                        if (port.is_none())
                        {
                            i = host.method("rfind", { S<":">() });
                            j = host.method("rfind", { S<"]">() });
                            if (i > j)
                            {
                                try
                                {
                                    port = (B<"int">())(host[B<"slice">()(i + (I<1>()), none(), none())]);
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler39;
                                    if (matches(handler39.exception, B<"ValueError">()))
                                    {
                                        if (host[B<"slice">()(i + (I<1>()), none(), none())] == (S<"">()))
                                        {
                                            port = self.attr("default_port");
                                        }
                                        else
                                        {
                                            py_raise((G<"http.client.InvalidURL">())((S<"nonnumeric port: '%s'">()) % host[B<"slice">()(i + (I<1>()), none(), none())]));
                                        }
                                    }
                                    else
                                        py_reraise(handler39.exception);
                                }
                                host = host[B<"slice">()(none(), i, none())];
                            }
                            else
                            {
                                port = self.attr("default_port");
                            }
                        }
                        if ((host.truthy() && (host[I<0>()] == (S<"[">())) && (host[I<-1>()] == (S<"]">()))))
                        {
                            host = host[B<"slice">()(I<1>(), I<-1>(), none())];
                        }
                        return py_tuple({ host, port });
                    }) },
                { "set_debuglevel", w.method("HTTPConnection", "set_debuglevel", { py_positional("level") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py level = a[1];
                        self.set_attr("debuglevel", level);
                        return none();
                    }) },
                { "_wrap_ipv6", w.method("HTTPConnection", "_wrap_ipv6", { py_positional("ip") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ip = a[1];
                        if ((ip.contains(py{ make_bytes(std::string{ "\x3a", 1 }) }) && (ip[I<0>()] != py{ make_bytes(std::string{ "\x5b", 1 }) }[I<0>()])))
                        {
                            return (py{ make_bytes(std::string{ "\x5b", 1 }) } + ip) + py{ make_bytes(std::string{ "\x5d", 1 }) };
                        }
                        return ip;
                    }) },
                { "_tunnel", w.method("HTTPConnection", "_tunnel", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py code;
                        py connect;
                        py header;
                        py header_bytes;
                        py headers;
                        py message;
                        py response;
                        py value;
                        py value_bytes;
                        py version;
                        if ((G<"http.client._contains_disallowed_url_pchar_re">()).method("search", { self.attr("_tunnel_host") }).truthy())
                        {
                            py_raise((B<"ValueError">())((S<"Tunnel host can't contain control characters %r">()) % py_tuple({ self.attr("_tunnel_host") })));
                        }
                        connect = py{ make_bytes(std::string{ "\x43\x4f\x4e\x4e\x45\x43\x54\x20\x25\x73\x3a\x25\x64\x20\x25\x73\x0d\x0a", 18 }) } % py_tuple({ self.method("_wrap_ipv6", { self.attr("_tunnel_host").method("encode", { S<"idna">() }) }), self.attr("_tunnel_port"), self.attr("_http_vsn_str").method("encode", { S<"ascii">() }) });
                        headers = py_list({ connect });
                        for (py const& item : iterate{ self.attr("_tunnel_headers").method("items") })
                        {
                            auto const items41 = unpack(item, 2);
                            header = items41[0];
                            value = items41[1];
                            header_bytes = header.method("encode", { S<"latin-1">() });
                            value_bytes = value.method("encode", { S<"latin-1">() });
                            if (!(G<"http.client._is_legal_header_name">())(header_bytes).truthy())
                            {
                                py_raise((B<"ValueError">())((S<"Invalid header name %r">()) % py_tuple({ header_bytes })));
                            }
                            if ((G<"http.client._is_illegal_header_value">())(value_bytes).truthy())
                            {
                                py_raise((B<"ValueError">())((S<"Invalid header value %r">()) % py_tuple({ value_bytes })));
                            }
                            headers.method("append", { py{ make_bytes(std::string{ "\x25\x73\x3a\x20\x25\x73\x0d\x0a", 8 }) } % py_tuple({ header_bytes, value_bytes }) });
                        }
                        headers.method("append", { py{ make_bytes(std::string{ "\x0d\x0a", 2 }) } });
                        // Making a single send() call instead of one per line encourages
                        // the host OS to use a more optimal packet size instead of
                        // potentially emitting a series of small packets.
                        self.method("send", { py{ make_bytes(std::string{ "", 0 }) }.method("join", { headers }) });
                        headers = none();
                        response = self.method("response_class", { self.attr("sock") }, { { "method", self.attr("_method") } });
                        {
                            std::optional<py> pending42;
                            [[maybe_unused]] int flow42 = 0;
                            [[maybe_unused]] py result42;
                            try
                            {
                                auto const items43 = unpack(response.method("_read_status"), 3);
                                version = items43[0];
                                code = items43[1];
                                message = items43[2];
                                self.set_attr("_raw_proxy_headers", (G<"http.client._read_headers">())(response.attr("fp")));
                                if (self.attr("debuglevel") > (I<0>()))
                                {
                                    for (py const& item : iterate{ self.attr("_raw_proxy_headers") })
                                    {
                                        header = item;
                                        (B<"print">())(S<"header:">(), header.method("decode"));
                                    }
                                }
                                if (code != (Mod<"http">().attr("HTTPStatus")).attr("OK"))
                                {
                                    self.method("close");
                                    py_raise((B<"OSError">())(S(std::string{ "Tunnel connection failed: " } + code.str() + " " + message.method("strip").str())));
                                }
                            }
                            catch (raised const&)
                            {
                                pending42 = py_take_exception();
                            }
                            finally42:
                            response.method("close");
                            if (pending42)
                                py_reraise(*pending42);
                            if (flow42 == 1)
                                return result42;
                        }
                        return none();
                    }) },
                { "get_proxy_response_headers", w.method("HTTPConnection", "get_proxy_response_headers", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (!self.attr("_raw_proxy_headers").is_none() ? (G<"http.client._parse_header_lines">())(self.attr("_raw_proxy_headers")) : none());
                    }) },
                { "connect", w.method("HTTPConnection", "connect", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py e;
                        (Mod<"sys">().attr("audit"))(S<"http.client.connect">(), self, self.attr("host"), self.attr("port"));
                        self.set_attr("sock", self.method("_create_connection", { py_tuple({ self.attr("host"), self.attr("port") }), self.attr("timeout"), self.attr("source_address") }));
                        // Might fail in OSs that don't implement TCP_NODELAY
                        try
                        {
                            self.attr("sock").method("setsockopt", { Mod<"socket">().attr("IPPROTO_TCP"), Mod<"socket">().attr("TCP_NODELAY"), I<1>() });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler45;
                            if (matches(handler45.exception, B<"OSError">()))
                            {
                                e = handler45.exception;
                                if (e.attr("errno") != (Mod<"errno">().attr("ENOPROTOOPT")))
                                {
                                    py_reraise(handler45.exception);
                                }
                            }
                            else
                                py_reraise(handler45.exception);
                        }
                        if (self.attr("_tunnel_host").truthy())
                        {
                            self.method("_tunnel");
                        }
                        return none();
                    }) },
                { "close", w.method("HTTPConnection", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py response;
                        py sock;
                        self.set_attr("_HTTPConnection__state", G<"http.client._CS_IDLE">());
                        {
                            std::optional<py> pending46;
                            [[maybe_unused]] int flow46 = 0;
                            [[maybe_unused]] py result46;
                            try
                            {
                                sock = self.attr("sock");
                                if (sock.truthy())
                                {
                                    self.set_attr("sock", none());
                                    sock.method("close");
                                }
                            }
                            catch (raised const&)
                            {
                                pending46 = py_take_exception();
                            }
                            finally46:
                            response = self.attr("_HTTPConnection__response");
                            if (response.truthy())
                            {
                                self.set_attr("_HTTPConnection__response", none());
                                response.method("close");
                            }
                            if (pending46)
                                py_reraise(*pending46);
                            if (flow46 == 1)
                                return result46;
                        }
                        return none();
                    }) },
                { "send", w.method("HTTPConnection", "send", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py d;
                        py datablock;
                        py encode;
                        if (self.attr("sock").is_none())
                        {
                            if (self.attr("auto_open").truthy())
                            {
                                self.method("connect");
                            }
                            else
                            {
                                py_raise((G<"http.client.NotConnected">())());
                            }
                        }
                        if (self.attr("debuglevel") > (I<0>()))
                        {
                            (B<"print">())(S<"send:">(), S(data.repr()));
                        }
                        if (has_attr(data, "read"))
                        {
                            if (self.attr("debuglevel") > (I<0>()))
                            {
                                (B<"print">())(S<"sending a readable">());
                            }
                            encode = self.method("_is_textIO", { data });
                            if ((encode.truthy() && (self.attr("debuglevel") > (I<0>()))))
                            {
                                (B<"print">())(S<"encoding file using iso-8859-1">());
                            }
                            while (([&]() -> py { datablock = data.method("read", { self.attr("blocksize") }); return datablock; }()).truthy())
                            {
                                if (encode.truthy())
                                {
                                    datablock = datablock.method("encode", { S<"iso-8859-1">() });
                                }
                                (Mod<"sys">().attr("audit"))(S<"http.client.send">(), self, datablock);
                                self.attr("sock").method("sendall", { datablock });
                            }
                            return none();
                        }
                        (Mod<"sys">().attr("audit"))(S<"http.client.send">(), self, data);
                        try
                        {
                            self.attr("sock").method("sendall", { data });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler48;
                            if (matches(handler48.exception, B<"TypeError">()))
                            {
                                if (isinstance(data, Mod<"collections.abc">().attr("Iterable")))
                                {
                                    for (py const& item : iterate{ data })
                                    {
                                        d = item;
                                        self.attr("sock").method("sendall", { d });
                                    }
                                }
                                else
                                {
                                    py_raise((B<"TypeError">())((S<"data should be a bytes-like object or an iterable, got %r">()) % (B<"type">())(data)));
                                }
                            }
                            else
                                py_reraise(handler48.exception);
                        }
                        return none();
                    }) },
                { "_output", w.method("HTTPConnection", "_output", { py_positional("s") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s = a[1];
                        self.attr("_buffer").method("append", { s });
                        return none();
                    }) },
                { "_read_readable", w.method("HTTPConnection", "_read_readable", { py_positional("readable") }, [](arguments a) -> py
                    {
                        return make_native_generator("_read_readable", "HTTPConnection._read_readable", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py readable = a[1];
                            py datablock;
                            py encode;
                            if (self.attr("debuglevel") > (I<0>()))
                            {
                                (B<"print">())(S<"reading a readable">());
                            }
                            encode = self.method("_is_textIO", { readable });
                            if ((encode.truthy() && (self.attr("debuglevel") > (I<0>()))))
                            {
                                (B<"print">())(S<"encoding file using iso-8859-1">());
                            }
                            while (([&]() -> py { datablock = readable.method("read", { self.attr("blocksize") }); return datablock; }()).truthy())
                            {
                                if (encode.truthy())
                                {
                                    datablock = datablock.method("encode", { S<"iso-8859-1">() });
                                }
                                py_yield(datablock);
                            }
                            return none();
                        });
                    }) },
                { "_send_output", w.method("HTTPConnection", "_send_output", { py_positional("message_body", none()), py_positional("encode_chunked", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message_body = a[1];
                        py encode_chunked = a[2];
                        py chunk;
                        py chunks;
                        py msg;
                        self.attr("_buffer").method("extend", { py_tuple({ py{ make_bytes(std::string{ "", 0 }) }, py{ make_bytes(std::string{ "", 0 }) } }) });
                        msg = py{ make_bytes(std::string{ "\x0d\x0a", 2 }) }.method("join", { self.attr("_buffer") });
                        del_item(self.attr("_buffer"), B<"slice">()(none(), none(), none()));
                        self.method("send", { msg });
                        if (!message_body.is_none())
                        {
                            // create a consistent interface to message_body
                            if (has_attr(message_body, "read"))
                            {
                                // Let file-like take precedence over byte-like.  This
                                // is needed to allow the current position of mmap'ed
                                // files to be taken into account.
                                chunks = self.method("_read_readable", { message_body });
                            }
                            else
                            {
                                {
                                    bool otherwise51 = false;
                                    try
                                    {
                                        // this is solely to check to see if message_body
                                        // implements the buffer API.  it /would/ be easier
                                        // to capture if PyObject_CheckBuffer was exposed
                                        // to Python.
                                        (B<"memoryview">())(message_body);
                                        otherwise51 = true;
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler51;
                                        if (matches(handler51.exception, B<"TypeError">()))
                                        {
                                            try
                                            {
                                                chunks = (B<"iter">())(message_body);
                                            }
                                            catch (raised const&)
                                            {
                                                pyclass::handler_scope handler52;
                                                if (matches(handler52.exception, B<"TypeError">()))
                                                {
                                                    py_raise((B<"TypeError">())((S<"message_body should be a bytes-like object or an iterable, got %r">()) % (B<"type">())(message_body)));
                                                }
                                                else
                                                    py_reraise(handler52.exception);
                                            }
                                        }
                                        else
                                            py_reraise(handler51.exception);
                                    }
                                    if (otherwise51)
                                    {
                                        // the object implements the buffer interface and
                                        // can be passed directly into socket methods
                                        chunks = py_tuple({ message_body });
                                    }
                                }
                            }
                            for (py const& item : iterate{ chunks })
                            {
                                chunk = item;
                                if (!chunk.truthy())
                                {
                                    if (self.attr("debuglevel") > (I<0>()))
                                    {
                                        (B<"print">())(S<"Zero length chunk ignored">());
                                    }
                                    continue;
                                }
                                if ((encode_chunked.truthy() && (self.attr("_http_vsn") == (I<11>()))))
                                {
                                    // chunked encoding
                                    chunk = (S(formatted(py{ chunk.size() }, "X") + "\r\n").method("encode", { S<"ascii">() }) + chunk) + py{ make_bytes(std::string{ "\x0d\x0a", 2 }) };
                                }
                                self.method("send", { chunk });
                            }
                            if ((encode_chunked.truthy() && (self.attr("_http_vsn") == (I<11>()))))
                            {
                                // end chunked transfer
                                self.method("send", { py{ make_bytes(std::string{ "\x30\x0d\x0a\x0d\x0a", 5 }) } });
                            }
                        }
                        return none();
                    }) },
                { "putrequest", w.method("HTTPConnection", "putrequest", { py_positional("method"), py_positional("url"), py_positional("skip_host", boolean(false)), py_positional("skip_accept_encoding", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py method = a[1];
                        py url = a[2];
                        py skip_host = a[3];
                        py skip_accept_encoding = a[4];
                        py host;
                        py host_enc;
                        py netloc;
                        py netloc_enc;
                        py nil;
                        py port;
                        py request;
                        // if a prior response has been completed, then forget about it.
                        if ((self.attr("_HTTPConnection__response").truthy() && self.attr("_HTTPConnection__response").method("isclosed").truthy()))
                        {
                            self.set_attr("_HTTPConnection__response", none());
                        }
                        // in certain cases, we cannot issue another request on this connection.
                        // this occurs when:
                        // 1) we are in the process of sending a request.   (_CS_REQ_STARTED)
                        // 2) a response to a previous request has signalled that it is going
                        // to close the connection upon completion.
                        // 3) the headers for the previous response have not been read, thus
                        // we cannot determine whether point (2) is true.   (_CS_REQ_SENT)
                        // 
                        // if there is no prior response, then we can request at will.
                        // 
                        // if point (2) is true, then we will have passed the socket to the
                        // response (effectively meaning, "there is no prior response"), and
                        // will open a new one when a new request is made.
                        // 
                        // Note: if a prior response exists, then we *can* start a new request.
                        // We are not allowed to begin fetching the response to this new
                        // request, however, until that prior response is complete.
                        // 
                        if (self.attr("_HTTPConnection__state") == (G<"http.client._CS_IDLE">()))
                        {
                            self.set_attr("_HTTPConnection__state", G<"http.client._CS_REQ_STARTED">());
                        }
                        else
                        {
                            py_raise((G<"http.client.CannotSendRequest">())(self.attr("_HTTPConnection__state")));
                        }
                        self.method("_validate_method", { method });
                        // Save the method for use later in the response phase
                        self.set_attr("_method", method);
                        url = [&]() -> py { py const t = url; return t.truthy() ? t : S<"/">(); }();
                        self.method("_validate_path", { url });
                        request = (S<"%s %s %s">()) % py_tuple({ method, url, self.attr("_http_vsn_str") });
                        self.method("_output", { self.method("_encode_request", { request }) });
                        if (self.attr("_http_vsn") == (I<11>()))
                        {
                            // Issue some standard headers for better HTTP/1.1 compliance
                            if (!skip_host.truthy())
                            {
                                // this header is issued *only* for HTTP/1.1
                                // connections. more specifically, this means it is
                                // only issued when the client uses the new
                                // HTTPConnection() class. backwards-compat clients
                                // will be using HTTP/1.0 and those clients may be
                                // issuing this header themselves. we should NOT issue
                                // it twice; some web servers (such as Apache) barf
                                // when they see two Host: headers
                                // If we need a non-standard port,include it in the
                                // header.  If the request is going through a proxy,
                                // but the host of the actual URL, not the host of the
                                // proxy.
                                netloc = S<"">();
                                if (url.method("startswith", { S<"http">() }).truthy())
                                {
                                    auto const items54 = unpack((Mod<"urllib.parse">().attr("urlsplit"))(url), 5);
                                    nil = items54[0];
                                    netloc = items54[1];
                                    nil = items54[2];
                                    nil = items54[3];
                                    nil = items54[4];
                                }
                                if (netloc.truthy())
                                {
                                    try
                                    {
                                        netloc_enc = netloc.method("encode", { S<"ascii">() });
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler55;
                                        if (matches(handler55.exception, B<"UnicodeEncodeError">()))
                                        {
                                            netloc_enc = netloc.method("encode", { S<"idna">() });
                                        }
                                        else
                                            py_reraise(handler55.exception);
                                    }
                                    self.method("putheader", { S<"Host">(), (G<"http.client._strip_ipv6_iface">())(netloc_enc) });
                                }
                                else
                                {
                                    if (self.attr("_tunnel_host").truthy())
                                    {
                                        host = self.attr("_tunnel_host");
                                        port = self.attr("_tunnel_port");
                                    }
                                    else
                                    {
                                        host = self.attr("host");
                                        port = self.attr("port");
                                    }
                                    try
                                    {
                                        host_enc = host.method("encode", { S<"ascii">() });
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler56;
                                        if (matches(handler56.exception, B<"UnicodeEncodeError">()))
                                        {
                                            host_enc = host.method("encode", { S<"idna">() });
                                        }
                                        else
                                            py_reraise(handler56.exception);
                                    }
                                    // As per RFC 273, IPv6 address should be wrapped with []
                                    // when used as Host header
                                    host_enc = self.method("_wrap_ipv6", { host_enc });
                                    if (host.contains((S<":">())))
                                    {
                                        host_enc = (G<"http.client._strip_ipv6_iface">())(host_enc);
                                    }
                                    if (port == self.attr("default_port"))
                                    {
                                        self.method("putheader", { S<"Host">(), host_enc });
                                    }
                                    else
                                    {
                                        host_enc = host_enc.method("decode", { S<"ascii">() });
                                        self.method("putheader", { S<"Host">(), (S<"%s:%s">()) % py_tuple({ host_enc, port }) });
                                    }
                                }
                            }
                            // note: we are assuming that clients will not attempt to set these
                            // headers since *this* library must deal with the
                            // consequences. this also means that when the supporting
                            // libraries are updated to recognize other forms, then this
                            // code should be changed (removed or updated).
                            // we only want a Content-Encoding of "identity" since we don't
                            // support encodings such as x-gzip or x-deflate.
                            if (!skip_accept_encoding.truthy())
                            {
                                self.method("putheader", { S<"Accept-Encoding">(), S<"identity">() });
                            }
                        }
                        else
                        {
                            // we can accept "chunked" Transfer-Encodings, but no others
                            // NOTE: no TE header implies *only* "chunked"
                            // self.putheader('TE', 'chunked')
                            // if TE is supplied in the header, then it must appear in a
                            // Connection header.
                            // self.putheader('Connection', 'TE')
                            // For HTTP/1.0, the server will assume "not chunked"
                        }
                        return none();
                    }) },
                { "_encode_request", w.method("HTTPConnection", "_encode_request", { py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py request = a[1];
                        return request.method("encode", { S<"ascii">() });
                    }) },
                { "_validate_method", w.method("HTTPConnection", "_validate_method", { py_positional("method") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py method = a[1];
                        py match;
                        // prevent http header injection
                        match = (G<"http.client._contains_disallowed_method_pchar_re">()).method("search", { method });
                        if (match.truthy())
                        {
                            py_raise((B<"ValueError">())(S(std::string{ "method can't contain control characters. " } + method.repr() + " (found at least " + match.method("group").repr() + ")")));
                        }
                        return none();
                    }) },
                { "_validate_path", w.method("HTTPConnection", "_validate_path", { py_positional("url") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py match;
                        // Prevent CVE-2019-9740.
                        match = (G<"http.client._contains_disallowed_url_pchar_re">()).method("search", { url });
                        if (match.truthy())
                        {
                            py_raise((G<"http.client.InvalidURL">())(S(std::string{ "URL can't contain control characters. " } + url.repr() + " (found at least " + match.method("group").repr() + ")")));
                        }
                        return none();
                    }) },
                { "_validate_host", w.method("HTTPConnection", "_validate_host", { py_positional("host") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py match;
                        // Prevent CVE-2019-18348.
                        match = (G<"http.client._contains_disallowed_url_pchar_re">()).method("search", { host });
                        if (match.truthy())
                        {
                            py_raise((G<"http.client.InvalidURL">())(S(std::string{ "URL can't contain control characters. " } + host.repr() + " (found at least " + match.method("group").repr() + ")")));
                        }
                        return none();
                    }) },
                { "putheader", w.method("HTTPConnection", "putheader", { py_positional("header") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py header = a[1];
                        py values = a[2];
                        py i;
                        py one_value;
                        py value;
                        if (self.attr("_HTTPConnection__state") != (G<"http.client._CS_REQ_STARTED">()))
                        {
                            py_raise((G<"http.client.CannotSendHeader">())());
                        }
                        if (has_attr(header, "encode"))
                        {
                            header = header.method("encode", { S<"ascii">() });
                        }
                        if (!(G<"http.client._is_legal_header_name">())(header).truthy())
                        {
                            py_raise((B<"ValueError">())((S<"Invalid header name %r">()) % py_tuple({ header })));
                        }
                        values = (B<"list">())(values);
                        for (py const& item : iterate{ (B<"enumerate">())(values) })
                        {
                            auto const items58 = unpack(item, 2);
                            i = items58[0];
                            one_value = items58[1];
                            if (has_attr(one_value, "encode"))
                            {
                                values.set_item(i, one_value.method("encode", { S<"latin-1">() }));
                            }
                            else if (isinstance(one_value, B<"int">()))
                            {
                                values.set_item(i, (B<"str">())(one_value).method("encode", { S<"ascii">() }));
                            }
                            if ((G<"http.client._is_illegal_header_value">())(values[i]).truthy())
                            {
                                py_raise((B<"ValueError">())((S<"Invalid header value %r">()) % py_tuple({ values[i] })));
                            }
                        }
                        value = py{ make_bytes(std::string{ "\x0d\x0a\x09", 3 }) }.method("join", { values });
                        header = (header + py{ make_bytes(std::string{ "\x3a\x20", 2 }) }) + value;
                        self.method("_output", { header });
                        return none();
                    }, true) },
                { "endheaders", w.method("HTTPConnection", "endheaders", { py_positional("message_body", none()), py_keyword("encode_chunked", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message_body = a[1];
                        py encode_chunked = a[2];
                        if (self.attr("_HTTPConnection__state") == (G<"http.client._CS_REQ_STARTED">()))
                        {
                            self.set_attr("_HTTPConnection__state", G<"http.client._CS_REQ_SENT">());
                        }
                        else
                        {
                            py_raise((G<"http.client.CannotSendHeader">())());
                        }
                        self.method("_send_output", { message_body }, { { "encode_chunked", encode_chunked } });
                        return none();
                    }) },
                { "request", w.method("HTTPConnection", "request", { py_positional("method"), py_positional("url"), py_positional("body", none()), py_positional("headers", py_dict()), py_keyword("encode_chunked", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py method = a[1];
                        py url = a[2];
                        py body = a[3];
                        py headers = a[4];
                        py encode_chunked = a[5];
                        self.method("_send_request", { method, url, body, headers, encode_chunked });
                        return none();
                    }) },
                { "_send_request", w.method("HTTPConnection", "_send_request", { py_positional("method"), py_positional("url"), py_positional("body"), py_positional("headers"), py_positional("encode_chunked") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py method = a[1];
                        py url = a[2];
                        py body = a[3];
                        py headers = a[4];
                        py encode_chunked = a[5];
                        py content_length;
                        py hdr;
                        py header_names;
                        py skips;
                        py value;
                        header_names = (B<"frozenset">())([&]() -> py { py const comprehension59 = py_list(); py k; for (py const& item : iterate{ headers }) { k = item; comprehension59.method("append", { k.method("lower") }); } return comprehension59; }());
                        skips = py_dict();
                        if (header_names.contains((S<"host">())))
                        {
                            skips.set_item(S<"skip_host">(), I<1>());
                        }
                        if (header_names.contains((S<"accept-encoding">())))
                        {
                            skips.set_item(S<"skip_accept_encoding">(), I<1>());
                        }
                        call_ex(self.attr("putrequest"), { method, url }, py{}, skips);
                        // chunked encoding will happen if HTTP/1.1 is used and either
                        // the caller passes encode_chunked=True or the following
                        // conditions hold:
                        // 1. content-length has not been explicitly set
                        // 2. the body is a file or iterable, but not a str or bytes-like
                        // 3. Transfer-Encoding has NOT been explicitly set by the caller
                        if (!header_names.contains((S<"content-length">())))
                        {
                            // only chunk body if not explicitly set for backwards
                            // compatibility, assuming the client code is already handling the
                            // chunking
                            if (!header_names.contains((S<"transfer-encoding">())))
                            {
                                // if content-length cannot be automatically determined, fall
                                // back to chunked encoding
                                encode_chunked = boolean(false);
                                content_length = self.method("_get_content_length", { body, method });
                                if (content_length.is_none())
                                {
                                    if (!body.is_none())
                                    {
                                        if (self.attr("debuglevel") > (I<0>()))
                                        {
                                            (B<"print">())((S<"Unable to determine size of %r">()) % body);
                                        }
                                        encode_chunked = boolean(true);
                                        self.method("putheader", { S<"Transfer-Encoding">(), S<"chunked">() });
                                    }
                                }
                                else
                                {
                                    self.method("putheader", { S<"Content-Length">(), (B<"str">())(content_length) });
                                }
                            }
                        }
                        else
                        {
                            encode_chunked = boolean(false);
                        }
                        for (py const& item : iterate{ headers.method("items") })
                        {
                            auto const items61 = unpack(item, 2);
                            hdr = items61[0];
                            value = items61[1];
                            self.method("putheader", { hdr, value });
                        }
                        if (isinstance(body, B<"str">()))
                        {
                            // RFC 2616 Section 3.7.1 says that text default has a
                            // default charset of iso-8859-1.
                            body = (G<"http.client._encode">())(body, S<"body">());
                        }
                        self.method("endheaders", { body }, { { "encode_chunked", encode_chunked } });
                        return none();
                    }) },
                { "getresponse", w.method("HTTPConnection", "getresponse", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py response;
                        // if a prior response has been completed, then forget about it.
                        if ((self.attr("_HTTPConnection__response").truthy() && self.attr("_HTTPConnection__response").method("isclosed").truthy()))
                        {
                            self.set_attr("_HTTPConnection__response", none());
                        }
                        // if a prior response exists, then it must be completed (otherwise, we
                        // cannot read this response's header to determine the connection-close
                        // behavior)
                        // 
                        // note: if a prior response existed, but was connection-close, then the
                        // socket and response were made independent of this HTTPConnection
                        // object since a new request requires that we open a whole new
                        // connection
                        // 
                        // this means the prior response had one of two states:
                        // 1) will_close: this connection was reset and the prior socket and
                        // response operate independently
                        // 2) persistent: the response was retained and we await its
                        // isclosed() status to become true.
                        // 
                        if (((self.attr("_HTTPConnection__state") != (G<"http.client._CS_REQ_SENT">())) || self.attr("_HTTPConnection__response").truthy()))
                        {
                            py_raise((G<"http.client.ResponseNotReady">())(self.attr("_HTTPConnection__state")));
                        }
                        if (self.attr("debuglevel") > (I<0>()))
                        {
                            response = self.method("response_class", { self.attr("sock"), self.attr("debuglevel") }, { { "method", self.attr("_method") } });
                        }
                        else
                        {
                            response = self.method("response_class", { self.attr("sock") }, { { "method", self.attr("_method") } });
                        }
                        try
                        {
                            try
                            {
                                response.method("begin");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler63;
                                if (matches(handler63.exception, B<"ConnectionError">()))
                                {
                                    self.method("close");
                                    py_reraise(handler63.exception);
                                }
                                else
                                    py_reraise(handler63.exception);
                            }
                            if (!(response.attr("will_close") != (G<"http.client._UNKNOWN">())))
                                py_raise(B<"AssertionError">()());
                            self.set_attr("_HTTPConnection__state", G<"http.client._CS_IDLE">());
                            if (response.attr("will_close").truthy())
                            {
                                // this effectively passes the connection to the response
                                self.method("close");
                            }
                            else
                            {
                                // remember this, so we can tell when it is complete
                                self.set_attr("_HTTPConnection__response", response);
                            }
                            return response;
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler62;
                            {
                                response.method("close");
                                py_reraise(handler62.exception);
                            }
                        }
                        return none();
                    }) }, }));

                // Subclasses that define an __init__ must call Exception.__init__
                // or define self.args.  Otherwise, str() will fail.
            define("http.client", "HTTPException", w.make_class("HTTPException", { B<"Exception">() }, {
 }));

            define("http.client", "NotConnected", w.make_class("NotConnected", { G<"http.client.HTTPException">() }, {
 }));

            define("http.client", "InvalidURL", w.make_class("InvalidURL", { G<"http.client.HTTPException">() }, {
 }));

            define("http.client", "UnknownProtocol", w.make_class("UnknownProtocol", { G<"http.client.HTTPException">() }, {
                { "__init__", w.method("UnknownProtocol", "__init__", { py_positional("version") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py version = a[1];
                        self.set_attr("args", py_tuple({ version }));
                        self.set_attr("version", version);
                        return none();
                    }) }, }));

            define("http.client", "UnknownTransferEncoding", w.make_class("UnknownTransferEncoding", { G<"http.client.HTTPException">() }, {
 }));

            define("http.client", "UnimplementedFileMode", w.make_class("UnimplementedFileMode", { G<"http.client.HTTPException">() }, {
 }));

            define("http.client", "IncompleteRead", w.make_class("IncompleteRead", { G<"http.client.HTTPException">() }, {
                { "__init__", w.method("IncompleteRead", "__init__", { py_positional("partial"), py_positional("expected", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py partial = a[1];
                        py expected = a[2];
                        self.set_attr("args", py_tuple({ partial }));
                        self.set_attr("partial", partial);
                        self.set_attr("expected", expected);
                        return none();
                    }) },
                { "__repr__", w.method("IncompleteRead", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py e;
                        if (!self.attr("expected").is_none())
                        {
                            e = (S<", %i more expected">()) % self.attr("expected");
                        }
                        else
                        {
                            e = S<"">();
                        }
                        return (S<"%s(%i bytes read%s)">()) % py_tuple({ self.attr("__class__").attr("__name__"), py{ self.attr("partial").size() }, e });
                    }) },
                { "__str__", (B<"object">()).attr("__str__") }, }));

            define("http.client", "ImproperConnectionState", w.make_class("ImproperConnectionState", { G<"http.client.HTTPException">() }, {
 }));

            define("http.client", "CannotSendRequest", w.make_class("CannotSendRequest", { G<"http.client.ImproperConnectionState">() }, {
 }));

            define("http.client", "CannotSendHeader", w.make_class("CannotSendHeader", { G<"http.client.ImproperConnectionState">() }, {
 }));

            define("http.client", "ResponseNotReady", w.make_class("ResponseNotReady", { G<"http.client.ImproperConnectionState">() }, {
 }));

            define("http.client", "BadStatusLine", w.make_class("BadStatusLine", { G<"http.client.HTTPException">() }, {
                { "__init__", w.method("BadStatusLine", "__init__", { py_positional("line") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line = a[1];
                        if (!line.truthy())
                        {
                            line = S(line.repr());
                        }
                        self.set_attr("args", py_tuple({ line }));
                        self.set_attr("line", line);
                        return none();
                    }) }, }));

            define("http.client", "LineTooLong", w.make_class("LineTooLong", { G<"http.client.HTTPException">() }, {
                { "__init__", w.method("LineTooLong", "__init__", { py_positional("line_type") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line_type = a[1];
                        (G<"http.client.HTTPException">()).method("__init__", { self, (S<"got more than %d bytes when reading %s">()) % py_tuple({ G<"http.client._MAXLINE">(), line_type }) });
                        return none();
                    }) }, }));

            define("http.client", "RemoteDisconnected", w.make_class("RemoteDisconnected", { B<"ConnectionResetError">(), G<"http.client.BadStatusLine">() }, {
                { "__init__", w.method("RemoteDisconnected", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pos = a[1];
                        py kw = a[2];
                        (G<"http.client.BadStatusLine">()).method("__init__", { self, S<"">() });
                        call_ex((B<"ConnectionResetError">()).attr("__init__"), { self }, pos, kw);
                        return none();
                    }, true, true) }, }));

            // for backwards compatibility
            define("http.client", "error", G<"http.client.HTTPException">());
        }

        bool http_client_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_http_client();
                }
                if (auto const doc = http_client_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("email", py{ fn_import("email") }.release());
                aAttributes.emplace_back("email", py{ fn_import("email") }.release());
                aAttributes.emplace_back("errno", py{ fn_import("errno") }.release());
                aAttributes.emplace_back("http", py{ fn_import("http") }.release());
                aAttributes.emplace_back("io", py{ fn_import("io") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("socket", py{ fn_import("socket") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("collections", py{ fn_import("collections") }.release());
                aAttributes.emplace_back("urlsplit", import_attribute("urllib.parse", "urlsplit").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("http.client", http_client_module);
    }
}
