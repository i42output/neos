/*
  http.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/http/__init__.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module http (CPython 3.13's Lib/http/__init__.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "http_docs.hpp"

namespace neos::python_runtime::detail::http_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "http.name"))
            bool made = false;
        };
        state& the_state()
        {
            static state* sState = new state{};
            return *sState;
        }

        // (its entry: the object, as it is defined (or assigned) now)
        [[maybe_unused]] py const& object_entry(char const* aQualifiedName)
        {
            auto& s = the_state();
            auto const o = s.objects.find(aQualifiedName);
            if (o == s.objects.end())
                py_raise("SystemError", std::string{ "http: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"http.name">()))
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
        [[maybe_unused]] void set_global(char const* aModule, char const* aName, py const& aValue)
        {
            define(aModule, aName, aValue);
            if (modules_imported(aModule))
                py{ fn_import(aModule) }.set_attr(aName, aValue);
        }
        // (globals(): the module's namespace (its __dict__ once it has been imported; while it is being made, its attributes so far))
        [[maybe_unused]] py module_globals()
        {
            if (modules_imported("http"))
                return py{ fn_import("http") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "http" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "http", http_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- http (Lib/http/__init__.py)

        void make_http()
        {
            static module_writer const& w = writer("http");
            define("http", "__all__", py_list({ S<"HTTPStatus">(), S<"HTTPMethod">() }));
                // informational
                // success
                // redirection
                // client error
                // server errors
            define("http", "HTTPStatus", (Mod<"enum">().attr("_simple_enum"))(Mod<"enum">().attr("IntEnum"))(w.make_class("HTTPStatus", {  }, {
                { "__new__", w.method("HTTPStatus", "__new__", { py_positional("value"), py_positional("phrase"), py_positional("description", S<"">()) }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py value = a[1];
                        py phrase = a[2];
                        py description = a[3];
                        py obj;
                        obj = (B<"int">()).method("__new__", { cls, value });
                        obj.set_attr("_value_", value);
                        obj.set_attr("phrase", phrase);
                        obj.set_attr("description", description);
                        return obj;
                    }) },
                { "is_informational", B<"property">()(w.method("HTTPStatus", "is_informational", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean((((I<100>()) <= self) && (self <= (I<199>()))));
                    })) },
                { "is_success", B<"property">()(w.method("HTTPStatus", "is_success", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean((((I<200>()) <= self) && (self <= (I<299>()))));
                    })) },
                { "is_redirection", B<"property">()(w.method("HTTPStatus", "is_redirection", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean((((I<300>()) <= self) && (self <= (I<399>()))));
                    })) },
                { "is_client_error", B<"property">()(w.method("HTTPStatus", "is_client_error", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean((((I<400>()) <= self) && (self <= (I<499>()))));
                    })) },
                { "is_server_error", B<"property">()(w.method("HTTPStatus", "is_server_error", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean((((I<500>()) <= self) && (self <= (I<599>()))));
                    })) },
                { "CONTINUE", py_tuple({ I<100>(), S<"Continue">(), S<"Request received, please continue">() }) },
                { "SWITCHING_PROTOCOLS", py_tuple({ I<101>(), S<"Switching Protocols">(), S<"Switching to new protocol; obey Upgrade header">() }) },
                { "PROCESSING", py_tuple({ I<102>(), S<"Processing">() }) },
                { "EARLY_HINTS", py_tuple({ I<103>(), S<"Early Hints">() }) },
                { "OK", py_tuple({ I<200>(), S<"OK">(), S<"Request fulfilled, document follows">() }) },
                { "CREATED", py_tuple({ I<201>(), S<"Created">(), S<"Document created, URL follows">() }) },
                { "ACCEPTED", py_tuple({ I<202>(), S<"Accepted">(), S<"Request accepted, processing continues off-line">() }) },
                { "NON_AUTHORITATIVE_INFORMATION", py_tuple({ I<203>(), S<"Non-Authoritative Information">(), S<"Request fulfilled from cache">() }) },
                { "NO_CONTENT", py_tuple({ I<204>(), S<"No Content">(), S<"Request fulfilled, nothing follows">() }) },
                { "RESET_CONTENT", py_tuple({ I<205>(), S<"Reset Content">(), S<"Clear input form for further input">() }) },
                { "PARTIAL_CONTENT", py_tuple({ I<206>(), S<"Partial Content">(), S<"Partial content follows">() }) },
                { "MULTI_STATUS", py_tuple({ I<207>(), S<"Multi-Status">() }) },
                { "ALREADY_REPORTED", py_tuple({ I<208>(), S<"Already Reported">() }) },
                { "IM_USED", py_tuple({ I<226>(), S<"IM Used">() }) },
                { "MULTIPLE_CHOICES", py_tuple({ I<300>(), S<"Multiple Choices">(), S<"Object has several resources -- see URI list">() }) },
                { "MOVED_PERMANENTLY", py_tuple({ I<301>(), S<"Moved Permanently">(), S<"Object moved permanently -- see URI list">() }) },
                { "FOUND", py_tuple({ I<302>(), S<"Found">(), S<"Object moved temporarily -- see URI list">() }) },
                { "SEE_OTHER", py_tuple({ I<303>(), S<"See Other">(), S<"Object moved -- see Method and URL list">() }) },
                { "NOT_MODIFIED", py_tuple({ I<304>(), S<"Not Modified">(), S<"Document has not changed since given time">() }) },
                { "USE_PROXY", py_tuple({ I<305>(), S<"Use Proxy">(), S<"You must use proxy specified in Location to access this resource">() }) },
                { "TEMPORARY_REDIRECT", py_tuple({ I<307>(), S<"Temporary Redirect">(), S<"Object moved temporarily -- see URI list">() }) },
                { "PERMANENT_REDIRECT", py_tuple({ I<308>(), S<"Permanent Redirect">(), S<"Object moved permanently -- see URI list">() }) },
                { "BAD_REQUEST", py_tuple({ I<400>(), S<"Bad Request">(), S<"Bad request syntax or unsupported method">() }) },
                { "UNAUTHORIZED", py_tuple({ I<401>(), S<"Unauthorized">(), S<"No permission -- see authorization schemes">() }) },
                { "PAYMENT_REQUIRED", py_tuple({ I<402>(), S<"Payment Required">(), S<"No payment -- see charging schemes">() }) },
                { "FORBIDDEN", py_tuple({ I<403>(), S<"Forbidden">(), S<"Request forbidden -- authorization will not help">() }) },
                { "NOT_FOUND", py_tuple({ I<404>(), S<"Not Found">(), S<"Nothing matches the given URI">() }) },
                { "METHOD_NOT_ALLOWED", py_tuple({ I<405>(), S<"Method Not Allowed">(), S<"Specified method is invalid for this resource">() }) },
                { "NOT_ACCEPTABLE", py_tuple({ I<406>(), S<"Not Acceptable">(), S<"URI not available in preferred format">() }) },
                { "PROXY_AUTHENTICATION_REQUIRED", py_tuple({ I<407>(), S<"Proxy Authentication Required">(), S<"You must authenticate with this proxy before proceeding">() }) },
                { "REQUEST_TIMEOUT", py_tuple({ I<408>(), S<"Request Timeout">(), S<"Request timed out; try again later">() }) },
                { "CONFLICT", py_tuple({ I<409>(), S<"Conflict">(), S<"Request conflict">() }) },
                { "GONE", py_tuple({ I<410>(), S<"Gone">(), S<"URI no longer exists and has been permanently removed">() }) },
                { "LENGTH_REQUIRED", py_tuple({ I<411>(), S<"Length Required">(), S<"Client must specify Content-Length">() }) },
                { "PRECONDITION_FAILED", py_tuple({ I<412>(), S<"Precondition Failed">(), S<"Precondition in headers is false">() }) },
                { "CONTENT_TOO_LARGE", py_tuple({ I<413>(), S<"Content Too Large">(), S<"Content is too large">() }) },
                { "REQUEST_ENTITY_TOO_LARGE", py_tuple({ I<413>(), S<"Content Too Large">(), S<"Content is too large">() }) },
                { "URI_TOO_LONG", py_tuple({ I<414>(), S<"URI Too Long">(), S<"URI is too long">() }) },
                { "REQUEST_URI_TOO_LONG", py_tuple({ I<414>(), S<"URI Too Long">(), S<"URI is too long">() }) },
                { "UNSUPPORTED_MEDIA_TYPE", py_tuple({ I<415>(), S<"Unsupported Media Type">(), S<"Entity body in unsupported format">() }) },
                { "RANGE_NOT_SATISFIABLE", py_tuple({ I<416>(), S<"Range Not Satisfiable">(), S<"Cannot satisfy request range">() }) },
                { "REQUESTED_RANGE_NOT_SATISFIABLE", py_tuple({ I<416>(), S<"Range Not Satisfiable">(), S<"Cannot satisfy request range">() }) },
                { "EXPECTATION_FAILED", py_tuple({ I<417>(), S<"Expectation Failed">(), S<"Expect condition could not be satisfied">() }) },
                { "IM_A_TEAPOT", py_tuple({ I<418>(), S<"I'm a Teapot">(), S<"Server refuses to brew coffee because it is a teapot.">() }) },
                { "MISDIRECTED_REQUEST", py_tuple({ I<421>(), S<"Misdirected Request">(), S<"Server is not able to produce a response">() }) },
                { "UNPROCESSABLE_CONTENT", py_tuple({ I<422>(), S<"Unprocessable Content">() }) },
                { "UNPROCESSABLE_ENTITY", py_tuple({ I<422>(), S<"Unprocessable Content">() }) },
                { "LOCKED", py_tuple({ I<423>(), S<"Locked">() }) },
                { "FAILED_DEPENDENCY", py_tuple({ I<424>(), S<"Failed Dependency">() }) },
                { "TOO_EARLY", py_tuple({ I<425>(), S<"Too Early">() }) },
                { "UPGRADE_REQUIRED", py_tuple({ I<426>(), S<"Upgrade Required">() }) },
                { "PRECONDITION_REQUIRED", py_tuple({ I<428>(), S<"Precondition Required">(), S<"The origin server requires the request to be conditional">() }) },
                { "TOO_MANY_REQUESTS", py_tuple({ I<429>(), S<"Too Many Requests">(), S<"The user has sent too many requests in a given amount of time (\"rate limiting\")">() }) },
                { "REQUEST_HEADER_FIELDS_TOO_LARGE", py_tuple({ I<431>(), S<"Request Header Fields Too Large">(), S<"The server is unwilling to process the request because its header fields are too large">() }) },
                { "UNAVAILABLE_FOR_LEGAL_REASONS", py_tuple({ I<451>(), S<"Unavailable For Legal Reasons">(), S<"The server is denying access to the resource as a consequence of a legal demand">() }) },
                { "INTERNAL_SERVER_ERROR", py_tuple({ I<500>(), S<"Internal Server Error">(), S<"Server got itself in trouble">() }) },
                { "NOT_IMPLEMENTED", py_tuple({ I<501>(), S<"Not Implemented">(), S<"Server does not support this operation">() }) },
                { "BAD_GATEWAY", py_tuple({ I<502>(), S<"Bad Gateway">(), S<"Invalid responses from another server/proxy">() }) },
                { "SERVICE_UNAVAILABLE", py_tuple({ I<503>(), S<"Service Unavailable">(), S<"The server cannot process the request due to a high load">() }) },
                { "GATEWAY_TIMEOUT", py_tuple({ I<504>(), S<"Gateway Timeout">(), S<"The gateway server did not receive a timely response">() }) },
                { "HTTP_VERSION_NOT_SUPPORTED", py_tuple({ I<505>(), S<"HTTP Version Not Supported">(), S<"Cannot fulfill request">() }) },
                { "VARIANT_ALSO_NEGOTIATES", py_tuple({ I<506>(), S<"Variant Also Negotiates">() }) },
                { "INSUFFICIENT_STORAGE", py_tuple({ I<507>(), S<"Insufficient Storage">() }) },
                { "LOOP_DETECTED", py_tuple({ I<508>(), S<"Loop Detected">() }) },
                { "NOT_EXTENDED", py_tuple({ I<510>(), S<"Not Extended">() }) },
                { "NETWORK_AUTHENTICATION_REQUIRED", py_tuple({ I<511>(), S<"Network Authentication Required">(), S<"The client needs to authenticate to gain network access">() }) }, })));

            define("http", "HTTPMethod", (Mod<"enum">().attr("_simple_enum"))(Mod<"enum">().attr("StrEnum"))(w.make_class("HTTPMethod", {  }, {
                { "__new__", w.method("HTTPMethod", "__new__", { py_positional("value"), py_positional("description") }, [](arguments a) -> py
                    {
                        py cls = a[0];
                        py value = a[1];
                        py description = a[2];
                        py obj;
                        obj = (B<"str">()).method("__new__", { cls, value });
                        obj.set_attr("_value_", value);
                        obj.set_attr("description", description);
                        return obj;
                    }) },
                { "__repr__", w.method("HTTPMethod", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"<%s.%s>">()) % py_tuple({ self.attr("__class__").attr("__name__"), self.attr("_name_") });
                    }) },
                { "CONNECT", py_tuple({ S<"CONNECT">(), S<"Establish a connection to the server.">() }) },
                { "DELETE", py_tuple({ S<"DELETE">(), S<"Remove the target.">() }) },
                { "GET", py_tuple({ S<"GET">(), S<"Retrieve the target.">() }) },
                { "HEAD", py_tuple({ S<"HEAD">(), S<"Same as GET, but only retrieve the status line and header section.">() }) },
                { "OPTIONS", py_tuple({ S<"OPTIONS">(), S<"Describe the communication options for the target.">() }) },
                { "PATCH", py_tuple({ S<"PATCH">(), S<"Apply partial modifications to a target.">() }) },
                { "POST", py_tuple({ S<"POST">(), S<"Perform target-specific processing with the request payload.">() }) },
                { "PUT", py_tuple({ S<"PUT">(), S<"Replace the target with the request payload.">() }) },
                { "TRACE", py_tuple({ S<"TRACE">(), S<"Perform a message loop-back test along the path to the target.">() }) }, })));
        }

        bool http_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_http();
                }
                if (auto const doc = http_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("StrEnum", import_attribute("enum", "StrEnum").release());
                aAttributes.emplace_back("IntEnum", import_attribute("enum", "IntEnum").release());
                aAttributes.emplace_back("_simple_enum", import_attribute("enum", "_simple_enum").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("http", http_module);
    }
}
