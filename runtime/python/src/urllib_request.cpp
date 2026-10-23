/*
  urllib_request.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/urllib/request.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module urllib.request (CPython 3.13's Lib/urllib/request.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "urllib_request_docs.hpp"

namespace neos::python_runtime::detail::urllib_request_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "urllib.request.name"))
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
                py_raise("SystemError", std::string{ "urllib.request: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"urllib.request.name">()))
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
            if (modules_imported("urllib.request"))
                return py{ fn_import("urllib.request") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "urllib.request" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "urllib.request", urllib_request_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- urllib.request (Lib/urllib/request.py)

        void make_urllib_request()
        {
            static module_writer const& w = writer("urllib.request");
            // XXX issues:
            // If an authentication error handler that tries to perform
            // authentication for some reason but fails, how should the error be
            // signalled?  The client needs to know the HTTP error code.  But if
            // the handler knows that the problem was, e.g., that it didn't know
            // that hash algo that requested in the challenge, it would be good to
            // pass that information along to the client, too.
            // ftp errors aren't handled cleanly
            // check digest against correct (i.e. non-apache) implementation
            // Possible extensions:
            // complex proxies  XXX not sure what exactly was meant by this
            // abstract factory for opener
            // check for SSL
            define("urllib.request", "_have_ssl", boolean(false));
            define("urllib.request", "__all__", py_list({ S<"Request">(), S<"OpenerDirector">(), S<"BaseHandler">(), S<"HTTPDefaultErrorHandler">(), S<"HTTPRedirectHandler">(), S<"HTTPCookieProcessor">(), S<"ProxyHandler">(), S<"HTTPPasswordMgr">(), S<"HTTPPasswordMgrWithDefaultRealm">(), S<"HTTPPasswordMgrWithPriorAuth">(), S<"AbstractBasicAuthHandler">(), S<"HTTPBasicAuthHandler">(), S<"ProxyBasicAuthHandler">(), S<"AbstractDigestAuthHandler">(), S<"HTTPDigestAuthHandler">(), S<"ProxyDigestAuthHandler">(), S<"HTTPHandler">(), S<"FileHandler">(), S<"FTPHandler">(), S<"CacheFTPHandler">(), S<"DataHandler">(), S<"UnknownHandler">(), S<"HTTPErrorProcessor">(), S<"urlopen">(), S<"install_opener">(), S<"build_opener">(), S<"pathname2url">(), S<"url2pathname">(), S<"getproxies">(), S<"urlretrieve">(), S<"urlcleanup">(), S<"URLopener">(), S<"FancyURLopener">() }));
            // Classes
            // Functions
            // Legacy interface
            // used in User-Agent header sent
            define("urllib.request", "__version__", (S<"%d.%d">()) % (Mod<"sys">().attr("version_info"))[B<"slice">()(none(), I<2>(), none())]);
            define("urllib.request", "_opener", none());
            define("urllib.request", "urlopen", w.function("urlopen", "urlopen", { py_positional("url"), py_positional("data", none()), py_positional("timeout", Mod<"socket">().attr("_GLOBAL_DEFAULT_TIMEOUT")), py_keyword("context", none()) }, [](arguments a) -> py
                {
                    py url = a[0];
                    py data = a[1];
                    py timeout = a[2];
                    py context = a[3];
                    py https_handler;
                    py opener;
                    if (context.truthy())
                    {
                        https_handler = (G<"urllib.request.HTTPSHandler">()).call({  }, { { "context", context } });
                        opener = (G<"urllib.request.build_opener">())(https_handler);
                    }
                    else if ((G<"urllib.request._opener">()).is_none())
                    {
                        py const value1 = (G<"urllib.request.build_opener">())();
                        set_global("urllib.request", "_opener", value1);
                        opener = value1;
                    }
                    else
                    {
                        opener = G<"urllib.request._opener">();
                    }
                    return opener.method("open", { url, data, timeout });
                }));

            define("urllib.request", "install_opener", w.function("install_opener", "install_opener", { py_positional("opener") }, [](arguments a) -> py
                {
                    py opener = a[0];
                    set_global("urllib.request", "_opener", opener);
                    return none();
                }));

            define("urllib.request", "_url_tempfiles", py_list());
            define("urllib.request", "urlretrieve", w.function("urlretrieve", "urlretrieve", { py_positional("url"), py_positional("filename", none()), py_positional("reporthook", none()), py_positional("data", none()) }, [](arguments a) -> py
                {
                    py url = a[0];
                    py filename = a[1];
                    py reporthook = a[2];
                    py data = a[3];
                    py block;
                    py blocknum;
                    py bs;
                    py fp;
                    py headers;
                    py path;
                    py read;
                    py result;
                    py size;
                    py tfp;
                    py url_type;
                    auto const items2 = unpack((Mod<"urllib.parse">().attr("_splittype"))(url), 2);
                    url_type = items2[0];
                    path = items2[1];
                    {
                        py const manager3 = (Mod<"contextlib">().attr("closing"))((G<"urllib.request.urlopen">())(url, data));
                        py const exit3 = pyclass::special_method(manager3, "__exit__");
                        py const value3 = pyclass::special_method(manager3, "__enter__")(manager3);
                        fp = value3;
                        [[maybe_unused]] int flow3 = 0;
                        [[maybe_unused]] py result3;
                        bool exited3 = false;
                        try
                        {
                            headers = fp.method("info");
                            // Just return the local path and the "headers" for file://
                            // URLs. No sense in performing a copy unless requested.
                            if (((url_type == (S<"file">())) && (!filename.truthy())))
                            {
                                result3 = py_tuple({ (Mod<"os">().attr("path")).method("normpath", { path }), headers });
                                flow3 = 1;
                                goto finally3;
                            }
                            // Handle temporary file setup.
                            if (filename.truthy())
                            {
                                tfp = (B<"open">())(filename, S<"wb">());
                            }
                            else
                            {
                                tfp = (Mod<"tempfile">().attr("NamedTemporaryFile")).call({  }, { { "delete", boolean(false) } });
                                filename = tfp.attr("name");
                                (G<"urllib.request._url_tempfiles">()).method("append", { filename });
                            }
                            {
                                py const manager4 = tfp;
                                py const exit4 = pyclass::special_method(manager4, "__exit__");
                                py const value4 = pyclass::special_method(manager4, "__enter__")(manager4);
                                [[maybe_unused]] int flow4 = 0;
                                [[maybe_unused]] py result4;
                                bool exited4 = false;
                                try
                                {
                                    result = py_tuple({ filename, headers });
                                    bs = (I<1024>()) * (I<8>());
                                    size = I<-1>();
                                    read = I<0>();
                                    blocknum = I<0>();
                                    if (headers.contains((S<"content-length">())))
                                    {
                                        size = (B<"int">())(headers[S<"Content-Length">()]);
                                    }
                                    if (reporthook.truthy())
                                    {
                                        reporthook(blocknum, bs, size);
                                    }
                                    while (([&]() -> py { block = fp.method("read", { bs }); return block; }()).truthy())
                                    {
                                        read = inplace(NEOS_PY_ADD, read, py{ block.size() });
                                        tfp.method("write", { block });
                                        blocknum = inplace(NEOS_PY_ADD, blocknum, I<1>());
                                        if (reporthook.truthy())
                                        {
                                            reporthook(blocknum, bs, size);
                                        }
                                    }
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler4;
                                    exited4 = true;
                                    if (!exit4(manager4, type_of(handler4.exception), handler4.exception, handler4.exception.attr("__traceback__")).truthy())
                                        py_reraise(handler4.exception);
                                }
                                finally4:
                                if (!exited4)
                                    exit4(manager4, none(), none(), none());
                                if (flow4 == 1)
                                    { result3 = result4; flow3 = 1; goto finally3; }
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler3;
                            exited3 = true;
                            if (!exit3(manager3, type_of(handler3.exception), handler3.exception, handler3.exception.attr("__traceback__")).truthy())
                                py_reraise(handler3.exception);
                        }
                        finally3:
                        if (!exited3)
                            exit3(manager3, none(), none(), none());
                        if (flow3 == 1)
                            return result3;
                    }
                    if (((size >= (I<0>())) && (read < size)))
                    {
                        py_raise((Mod<"urllib.error">().attr("ContentTooShortError"))((S<"retrieval incomplete: got only %i out of %i bytes">()) % py_tuple({ read, size }), result));
                    }
                    return result;
                }));

            define("urllib.request", "urlcleanup", w.function("urlcleanup", "urlcleanup", {  }, [](arguments a) -> py
                {
                    py temp_file;
                    for (py const& item : iterate{ G<"urllib.request._url_tempfiles">() })
                    {
                        temp_file = item;
                        try
                        {
                            (Mod<"os">().attr("unlink"))(temp_file);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler7;
                            if (matches(handler7.exception, B<"OSError">()))
                            {
                            }
                            else
                                py_reraise(handler7.exception);
                        }
                    }
                    del_item(G<"urllib.request._url_tempfiles">(), B<"slice">()(none(), none(), none()));
                    if ((G<"urllib.request._opener">()).truthy())
                    {
                        set_global("urllib.request", "_opener", none());
                    }
                    return none();
                }));

            // copied from cookielib.py
            define("urllib.request", "_cut_port_re", (Mod<"re">()).method("compile", { S<":\\d+$">(), Mod<"re">().attr("ASCII") }));
            define("urllib.request", "request_host", w.function("request_host", "request_host", { py_positional("request") }, [](arguments a) -> py
                {
                    py request = a[0];
                    py host;
                    py url;
                    url = request.attr("full_url");
                    host = (Mod<"urllib.parse">().attr("urlparse"))(url)[I<1>()];
                    if (host == (S<"">()))
                    {
                        host = request.method("get_header", { S<"Host">(), S<"">() });
                    }
                    // remove port, if present
                    host = (G<"urllib.request._cut_port_re">()).method("sub", { S<"">(), host, I<1>() });
                    return host.method("lower");
                }));

            define("urllib.request", "Request", w.make_class("Request", {  }, {
                { "__init__", w.method("Request", "__init__", { py_positional("url"), py_positional("data", none()), py_positional("headers", py_dict()), py_positional("origin_req_host", none()), py_positional("unverifiable", boolean(false)), py_positional("method", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py data = a[2];
                        py headers = a[3];
                        py origin_req_host = a[4];
                        py unverifiable = a[5];
                        py method = a[6];
                        py key;
                        py value;
                        self.set_attr("full_url", url);
                        self.set_attr("headers", py_dict());
                        self.set_attr("unredirected_hdrs", py_dict());
                        self.set_attr("_data", none());
                        self.set_attr("data", data);
                        self.set_attr("_tunnel_host", none());
                        for (py const& item : iterate{ headers.method("items") })
                        {
                            auto const items9 = unpack(item, 2);
                            key = items9[0];
                            value = items9[1];
                            self.method("add_header", { key, value });
                        }
                        if (origin_req_host.is_none())
                        {
                            origin_req_host = (G<"urllib.request.request_host">())(self);
                        }
                        self.set_attr("origin_req_host", origin_req_host);
                        self.set_attr("unverifiable", unverifiable);
                        if (method.truthy())
                        {
                            self.set_attr("method", method);
                        }
                        return none();
                    }) },
                { "full_url", B<"property">()(w.method("Request", "full_url", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("fragment").truthy())
                        {
                            return (S<"{}#{}">()).method("format", { self.attr("_full_url"), self.attr("fragment") });
                        }
                        return self.attr("_full_url");
                    }), w.method("Request", "full_url", { py_positional("url") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        self.set_attr("_full_url", (Mod<"urllib.parse">().attr("unwrap"))(url));
                        auto const items10 = unpack((Mod<"urllib.parse">().attr("_splittag"))(self.attr("_full_url")), 2);
                        self.set_attr("_full_url", items10[0]);
                        self.set_attr("fragment", items10[1]);
                        self.method("_parse");
                        return none();
                    }), w.method("Request", "full_url", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("_full_url", none());
                        self.set_attr("fragment", none());
                        self.set_attr("selector", S<"">());
                        return none();
                    })) },
                { "data", B<"property">()(w.method("Request", "data", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_data");
                    }), w.method("Request", "data", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        if (data != self.attr("_data"))
                        {
                            self.set_attr("_data", data);
                            // issue 16464
                            // if we change data we need to remove content-length header
                            // (cause it's most probably calculated for previous value)
                            if (self.method("has_header", { S<"Content-length">() }).truthy())
                            {
                                self.method("remove_header", { S<"Content-length">() });
                            }
                        }
                        return none();
                    }), w.method("Request", "data", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("data", none());
                        return none();
                    })) },
                { "_parse", w.method("Request", "_parse", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py rest;
                        auto const items11 = unpack((Mod<"urllib.parse">().attr("_splittype"))(self.attr("_full_url")), 2);
                        self.set_attr("type", items11[0]);
                        rest = items11[1];
                        if (self.attr("type").is_none())
                        {
                            py_raise((B<"ValueError">())((S<"unknown url type: %r">()) % self.attr("full_url")));
                        }
                        auto const items12 = unpack((Mod<"urllib.parse">().attr("_splithost"))(rest), 2);
                        self.set_attr("host", items12[0]);
                        self.set_attr("selector", items12[1]);
                        if (self.attr("host").truthy())
                        {
                            self.set_attr("host", (Mod<"urllib.parse">().attr("unquote"))(self.attr("host")));
                        }
                        return none();
                    }) },
                { "get_method", w.method("Request", "get_method", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py default_method;
                        default_method = (!self.attr("data").is_none() ? S<"POST">() : S<"GET">());
                        return (B<"getattr">())(self, S<"method">(), default_method);
                    }) },
                { "get_full_url", w.method("Request", "get_full_url", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("full_url");
                    }) },
                { "set_proxy", w.method("Request", "set_proxy", { py_positional("host"), py_positional("type") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py type = a[2];
                        if (((self.attr("type") == (S<"https">())) && (!self.attr("_tunnel_host").truthy())))
                        {
                            self.set_attr("_tunnel_host", self.attr("host"));
                        }
                        else
                        {
                            self.set_attr("type", type);
                            self.set_attr("selector", self.attr("full_url"));
                        }
                        self.set_attr("host", host);
                        return none();
                    }) },
                { "has_proxy", w.method("Request", "has_proxy", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return boolean(self.attr("selector") == self.attr("full_url"));
                    }) },
                { "add_header", w.method("Request", "add_header", { py_positional("key"), py_positional("val") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py val = a[2];
                        self.attr("headers").set_item(key.method("capitalize"), val);
                        return none();
                    }) },
                { "add_unredirected_header", w.method("Request", "add_unredirected_header", { py_positional("key"), py_positional("val") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py key = a[1];
                        py val = a[2];
                        self.attr("unredirected_hdrs").set_item(key.method("capitalize"), val);
                        return none();
                    }) },
                { "has_header", w.method("Request", "has_header", { py_positional("header_name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py header_name = a[1];
                        return [&]() -> py { py const t = boolean(self.attr("headers").contains(header_name)); return t.truthy() ? t : boolean(self.attr("unredirected_hdrs").contains(header_name)); }();
                    }) },
                { "get_header", w.method("Request", "get_header", { py_positional("header_name"), py_positional("default", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py header_name = a[1];
                        py default_ = a[2];
                        return self.attr("headers").method("get", { header_name, self.attr("unredirected_hdrs").method("get", { header_name, default_ }) });
                    }) },
                { "remove_header", w.method("Request", "remove_header", { py_positional("header_name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py header_name = a[1];
                        self.attr("headers").method("pop", { header_name, none() });
                        self.attr("unredirected_hdrs").method("pop", { header_name, none() });
                        return none();
                    }) },
                { "header_items", w.method("Request", "header_items", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py hdrs;
                        hdrs = [&]() -> py { py const d = py_dict(); d.method("update", { self.attr("unredirected_hdrs") }); d.method("update", { self.attr("headers") }); return d; }();
                        return (B<"list">())(hdrs.method("items"));
                    }) }, }));

            define("urllib.request", "OpenerDirector", w.make_class("OpenerDirector", {  }, {
                { "__init__", w.method("OpenerDirector", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py client_version;
                        client_version = (S<"Python-urllib/%s">()) % (G<"urllib.request.__version__">());
                        self.set_attr("addheaders", py_list({ py_tuple({ S<"User-agent">(), client_version }) }));
                        // self.handlers is retained only for backward compatibility
                        self.set_attr("handlers", py_list());
                        // manage the individual handlers
                        self.set_attr("handle_open", py_dict());
                        self.set_attr("handle_error", py_dict());
                        self.set_attr("process_response", py_dict());
                        self.set_attr("process_request", py_dict());
                        return none();
                    }) },
                { "add_handler", w.method("OpenerDirector", "add_handler", { py_positional("handler") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py handler = a[1];
                        py added;
                        py condition;
                        py handlers;
                        py i;
                        py j;
                        py kind;
                        py lookup;
                        py meth;
                        py protocol;
                        if (!has_attr(handler, "add_parent"))
                        {
                            py_raise((B<"TypeError">())((S<"expected BaseHandler instance, got %r">()) % (B<"type">())(handler)));
                        }
                        added = boolean(false);
                        for (py const& item : iterate{ (B<"dir">())(handler) })
                        {
                            meth = item;
                            if (py_list({ S<"redirect_request">(), S<"do_open">(), S<"proxy_open">() }).contains(meth))
                            {
                                // oops, coincidental match
                                continue;
                            }
                            i = meth.method("find", { S<"_">() });
                            protocol = meth[B<"slice">()(none(), i, none())];
                            condition = meth[B<"slice">()(i + (I<1>()), none(), none())];
                            if (condition.method("startswith", { S<"error">() }).truthy())
                            {
                                j = (condition.method("find", { S<"_">() }) + i) + (I<1>());
                                kind = meth[B<"slice">()(j + (I<1>()), none(), none())];
                                try
                                {
                                    kind = (B<"int">())(kind);
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler14;
                                    if (matches(handler14.exception, B<"ValueError">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler14.exception);
                                }
                                lookup = self.attr("handle_error").method("get", { protocol, py_dict() });
                                self.attr("handle_error").set_item(protocol, lookup);
                            }
                            else if (condition == (S<"open">()))
                            {
                                kind = protocol;
                                lookup = self.attr("handle_open");
                            }
                            else if (condition == (S<"response">()))
                            {
                                kind = protocol;
                                lookup = self.attr("process_response");
                            }
                            else if (condition == (S<"request">()))
                            {
                                kind = protocol;
                                lookup = self.attr("process_request");
                            }
                            else
                            {
                                continue;
                            }
                            handlers = lookup.method("setdefault", { kind, py_list() });
                            if (handlers.truthy())
                            {
                                (Mod<"bisect">().attr("insort"))(handlers, handler);
                            }
                            else
                            {
                                handlers.method("append", { handler });
                            }
                            added = boolean(true);
                        }
                        if (added.truthy())
                        {
                            (Mod<"bisect">().attr("insort"))(self.attr("handlers"), handler);
                            handler.method("add_parent", { self });
                        }
                        return none();
                    }) },
                { "close", w.method("OpenerDirector", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "_call_chain", w.method("OpenerDirector", "_call_chain", { py_positional("chain"), py_positional("kind"), py_positional("meth_name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py chain = a[1];
                        py kind = a[2];
                        py meth_name = a[3];
                        py args = a[4];
                        py func;
                        py handler;
                        py handlers;
                        py result;
                        handlers = chain.method("get", { kind, py_tuple() });
                        for (py const& item : iterate{ handlers })
                        {
                            handler = item;
                            func = (B<"getattr">())(handler, meth_name);
                            result = call_ex(func, {  }, args, py{});
                            if (!result.is_none())
                            {
                                return result;
                            }
                        }
                        return none();
                    }, true) },
                { "open", w.method("OpenerDirector", "open", { py_positional("fullurl"), py_positional("data", none()), py_positional("timeout", Mod<"socket">().attr("_GLOBAL_DEFAULT_TIMEOUT")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fullurl = a[1];
                        py data = a[2];
                        py timeout = a[3];
                        py meth;
                        py meth_name;
                        py processor;
                        py protocol;
                        py req;
                        py response;
                        if (isinstance(fullurl, B<"str">()))
                        {
                            req = (G<"urllib.request.Request">())(fullurl, data);
                        }
                        else
                        {
                            req = fullurl;
                            if (!data.is_none())
                            {
                                req.set_attr("data", data);
                            }
                        }
                        req.set_attr("timeout", timeout);
                        protocol = req.attr("type");
                        // pre-process request
                        meth_name = protocol + (S<"_request">());
                        for (py const& item : iterate{ self.attr("process_request").method("get", { protocol, py_list() }) })
                        {
                            processor = item;
                            meth = (B<"getattr">())(processor, meth_name);
                            req = meth(req);
                        }
                        (Mod<"sys">().attr("audit"))(S<"urllib.Request">(), req.attr("full_url"), req.attr("data"), req.attr("headers"), req.method("get_method"));
                        response = self.method("_open", { req, data });
                        // post-process response
                        meth_name = protocol + (S<"_response">());
                        for (py const& item : iterate{ self.attr("process_response").method("get", { protocol, py_list() }) })
                        {
                            processor = item;
                            meth = (B<"getattr">())(processor, meth_name);
                            response = meth(req, response);
                        }
                        return response;
                    }) },
                { "_open", w.method("OpenerDirector", "_open", { py_positional("req"), py_positional("data", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py data = a[2];
                        py protocol;
                        py result;
                        result = self.method("_call_chain", { self.attr("handle_open"), S<"default">(), S<"default_open">(), req });
                        if (result.truthy())
                        {
                            return result;
                        }
                        protocol = req.attr("type");
                        result = self.method("_call_chain", { self.attr("handle_open"), protocol, protocol + (S<"_open">()), req });
                        if (result.truthy())
                        {
                            return result;
                        }
                        return self.method("_call_chain", { self.attr("handle_open"), S<"unknown">(), S<"unknown_open">(), req });
                    }) },
                { "error", w.method("OpenerDirector", "error", { py_positional("proto") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py proto = a[1];
                        py args = a[2];
                        py dict;
                        py http_err;
                        py meth_name;
                        py orig_args;
                        py result;
                        if (py_tuple({ S<"http">(), S<"https">() }).contains(proto))
                        {
                            // XXX http[s] protocols are special-cased
                            dict = self.attr("handle_error")[S<"http">()];
                            proto = args[I<2>()];
                            meth_name = (S<"http_error_%s">()) % proto;
                            http_err = I<1>();
                            orig_args = args;
                        }
                        else
                        {
                            dict = self.attr("handle_error");
                            meth_name = proto + (S<"_error">());
                            http_err = I<0>();
                        }
                        args = py_tuple({ dict, proto, meth_name }) + args;
                        result = call_ex(self.attr("_call_chain"), {  }, args, py{});
                        if (result.truthy())
                        {
                            return result;
                        }
                        if (http_err.truthy())
                        {
                            args = py_tuple({ dict, S<"default">(), S<"http_error_default">() }) + orig_args;
                            return call_ex(self.attr("_call_chain"), {  }, args, py{});
                        }
                        return none();
                    }, true) }, }));

            // XXX probably also want an abstract factory that knows when it makes
            // sense to skip a superclass in favor of a subclass and when it might
            // make sense to include both
            define("urllib.request", "build_opener", w.function("build_opener", "build_opener", {  }, [](arguments a) -> py
                {
                    py handlers = a[0];
                    py check;
                    py default_classes;
                    py h;
                    py klass;
                    py opener;
                    py skip;
                    opener = (G<"urllib.request.OpenerDirector">())();
                    default_classes = py_list({ G<"urllib.request.ProxyHandler">(), G<"urllib.request.UnknownHandler">(), G<"urllib.request.HTTPHandler">(), G<"urllib.request.HTTPDefaultErrorHandler">(), G<"urllib.request.HTTPRedirectHandler">(), G<"urllib.request.FTPHandler">(), G<"urllib.request.FileHandler">(), G<"urllib.request.HTTPErrorProcessor">(), G<"urllib.request.DataHandler">() });
                    if (has_attr(Mod<"http.client">(), "HTTPSConnection"))
                    {
                        default_classes.method("append", { G<"urllib.request.HTTPSHandler">() });
                    }
                    skip = (B<"set">())();
                    for (py const& item : iterate{ default_classes })
                    {
                        klass = item;
                        for (py const& item : iterate{ handlers })
                        {
                            check = item;
                            if (isinstance(check, B<"type">()))
                            {
                                if (issubclass(check, klass))
                                {
                                    skip.method("add", { klass });
                                }
                            }
                            else if (isinstance(check, klass))
                            {
                                skip.method("add", { klass });
                            }
                        }
                    }
                    for (py const& item : iterate{ skip })
                    {
                        klass = item;
                        default_classes.method("remove", { klass });
                    }
                    for (py const& item : iterate{ default_classes })
                    {
                        klass = item;
                        opener.method("add_handler", { klass() });
                    }
                    for (py const& item : iterate{ handlers })
                    {
                        h = item;
                        if (isinstance(h, B<"type">()))
                        {
                            h = h();
                        }
                        opener.method("add_handler", { h });
                    }
                    return opener;
                }, true));

            define("urllib.request", "BaseHandler", w.make_class("BaseHandler", {  }, {
                { "handler_order", I<500>() },
                { "add_parent", w.method("BaseHandler", "add_parent", { py_positional("parent") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py parent = a[1];
                        self.set_attr("parent", parent);
                        return none();
                    }) },
                { "close", w.method("BaseHandler", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return none();
                    }) },
                { "__lt__", w.method("BaseHandler", "__lt__", { py_positional("other") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py other = a[1];
                        if (!has_attr(other, "handler_order"))
                        {
                            // Try to preserve the old behavior of having custom classes
                            // inserted after default ones (works only for custom user
                            // classes which are not aware of handler_order).
                            return boolean(true);
                        }
                        return boolean(self.attr("handler_order") < other.attr("handler_order"));
                    }) }, }));

            define("urllib.request", "HTTPErrorProcessor", w.make_class("HTTPErrorProcessor", { G<"urllib.request.BaseHandler">() }, {
                { "handler_order", I<1000>() },
                { "http_response", w.method("HTTPErrorProcessor", "http_response", { py_positional("request"), py_positional("response") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py request = a[1];
                        py response = a[2];
                        py code;
                        py hdrs;
                        py msg;
                        auto const items23 = unpack(py_tuple({ response.attr("code"), response.attr("msg"), response.method("info") }), 3);
                        code = items23[0];
                        msg = items23[1];
                        hdrs = items23[2];
                        // According to RFC 2616, "2xx" code indicates that the client's
                        // request was successfully received, understood, and accepted.
                        if (!(((I<200>()) <= code) && (code < (I<300>()))))
                        {
                            response = self.attr("parent").method("error", { S<"http">(), request, response, code, msg, hdrs });
                        }
                        return response;
                    }) },
                { "https_response", w.method("HTTPErrorProcessor", "http_response", { py_positional("request"), py_positional("response") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py request = a[1];
                        py response = a[2];
                        py code;
                        py hdrs;
                        py msg;
                        auto const items23 = unpack(py_tuple({ response.attr("code"), response.attr("msg"), response.method("info") }), 3);
                        code = items23[0];
                        msg = items23[1];
                        hdrs = items23[2];
                        // According to RFC 2616, "2xx" code indicates that the client's
                        // request was successfully received, understood, and accepted.
                        if (!(((I<200>()) <= code) && (code < (I<300>()))))
                        {
                            response = self.attr("parent").method("error", { S<"http">(), request, response, code, msg, hdrs });
                        }
                        return response;
                    }) }, }));

            define("urllib.request", "HTTPDefaultErrorHandler", w.make_class("HTTPDefaultErrorHandler", { G<"urllib.request.BaseHandler">() }, {
                { "http_error_default", w.method("HTTPDefaultErrorHandler", "http_error_default", { py_positional("req"), py_positional("fp"), py_positional("code"), py_positional("msg"), py_positional("hdrs") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py fp = a[2];
                        py code = a[3];
                        py msg = a[4];
                        py hdrs = a[5];
                        py_raise((Mod<"urllib.error">().attr("HTTPError"))(req.attr("full_url"), code, msg, hdrs, fp));
                    }) }, }));

                // maximum number of redirections to any single URL
                // this is needed because of the state that cookies introduce
                // maximum total number of redirections (regardless of URL) before
                // assuming we're in a loop
                // Implementation note: To avoid the server sending us into an
                // infinite loop, the request object needs to track what URLs we
                // have already seen.  Do this by adding a handler-specific
                // attribute to the Request object.
            define("urllib.request", "HTTPRedirectHandler", w.make_class("HTTPRedirectHandler", { G<"urllib.request.BaseHandler">() }, {
                { "max_repeats", I<4>() },
                { "max_redirections", I<10>() },
                { "redirect_request", w.method("HTTPRedirectHandler", "redirect_request", { py_positional("req"), py_positional("fp"), py_positional("code"), py_positional("msg"), py_positional("headers"), py_positional("newurl") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py fp = a[2];
                        py code = a[3];
                        py msg = a[4];
                        py headers = a[5];
                        py newurl = a[6];
                        py CONTENT_HEADERS;
                        py m;
                        py newheaders;
                        m = req.method("get_method");
                        if (!([&]() -> py { py const t = [&]() -> py { py const t = boolean(py_tuple({ I<301>(), I<302>(), I<303>(), I<307>(), I<308>() }).contains(code)); return !t.truthy() ? t : boolean(py_tuple({ S<"GET">(), S<"HEAD">() }).contains(m)); }(); return t.truthy() ? t : [&]() -> py { py const t = boolean(py_tuple({ I<301>(), I<302>(), I<303>() }).contains(code)); return !t.truthy() ? t : boolean(m == (S<"POST">())); }(); }()).truthy())
                        {
                            py_raise((Mod<"urllib.error">().attr("HTTPError"))(req.attr("full_url"), code, msg, headers, fp));
                        }
                        // Strictly (according to RFC 2616), 301 or 302 in response to
                        // a POST MUST NOT cause a redirection without confirmation
                        // from the user (of urllib.request, in this case).  In practice,
                        // essentially all clients do redirect in this case, so we do
                        // the same.
                        // Be conciliant with URIs containing a space.  This is mainly
                        // redundant with the more complete encoding done in http_error_302(),
                        // but it is kept for compatibility with other callers.
                        newurl = newurl.method("replace", { S<" ">(), S<"%20">() });
                        CONTENT_HEADERS = py_tuple({ S<"content-length">(), S<"content-type">() });
                        newheaders = [&]() -> py { py const comprehension24 = py_dict(); py k; py v; for (py const& item : iterate{ req.attr("headers").method("items") }) { auto const items25 = unpack(item, 2); k = items25[0]; v = items25[1]; if (CONTENT_HEADERS.contains(k.method("lower"))) continue; comprehension24.set_item(k, v); } return comprehension24; }();
                        return (G<"urllib.request.Request">()).call({ newurl }, { { "method", (m == (S<"HEAD">()) ? S<"HEAD">() : S<"GET">()) }, { "headers", newheaders }, { "origin_req_host", req.attr("origin_req_host") }, { "unverifiable", boolean(true) } });
                    }) },
                { "http_error_302", w.method("HTTPRedirectHandler", "http_error_302", { py_positional("req"), py_positional("fp"), py_positional("code"), py_positional("msg"), py_positional("headers") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py fp = a[2];
                        py code = a[3];
                        py msg = a[4];
                        py headers = a[5];
                        py new_;
                        py newurl;
                        py urlparts;
                        py visited;
                        if (headers.contains((S<"location">())))
                        {
                            newurl = headers[S<"location">()];
                        }
                        else if (headers.contains((S<"uri">())))
                        {
                            newurl = headers[S<"uri">()];
                        }
                        else
                        {
                            return none();
                        }
                        // fix a possible malformed URL
                        urlparts = (Mod<"urllib.parse">().attr("urlparse"))(newurl);
                        // For security reasons we don't allow redirection to anything other
                        // than http, https or ftp.
                        if (!py_tuple({ S<"http">(), S<"https">(), S<"ftp">(), S<"">() }).contains(urlparts.attr("scheme")))
                        {
                            py_raise((Mod<"urllib.error">().attr("HTTPError"))(newurl, code, (S<"%s - Redirection to url '%s' is not allowed">()) % py_tuple({ msg, newurl }), headers, fp));
                        }
                        if (((!urlparts.attr("path").truthy()) && urlparts.attr("netloc").truthy()))
                        {
                            urlparts = (B<"list">())(urlparts);
                            urlparts.set_item(I<2>(), S<"/">());
                        }
                        newurl = (Mod<"urllib.parse">().attr("urlunparse"))(urlparts);
                        // http.client.parse_headers() decodes as ISO-8859-1.  Recover the
                        // original bytes and percent-encode non-ASCII bytes, and any special
                        // characters such as the space.
                        newurl = (Mod<"urllib.parse">().attr("quote")).call({ newurl }, { { "encoding", S<"iso-8859-1">() }, { "safe", Mod<"string">().attr("punctuation") } });
                        newurl = (Mod<"urllib.parse">().attr("urljoin"))(req.attr("full_url"), newurl);
                        // XXX Probably want to forget about the state of the current
                        // request, although that might interact poorly with other
                        // handlers that also use handler-specific request attributes
                        new_ = self.method("redirect_request", { req, fp, code, msg, headers, newurl });
                        if (new_.is_none())
                        {
                            return none();
                        }
                        // loop detection
                        // .redirect_dict has a key url if url was previously visited.
                        if (has_attr(req, "redirect_dict"))
                        {
                            py const value26 = req.attr("redirect_dict");
                            visited = value26;
                            new_.set_attr("redirect_dict", value26);
                            if (((visited.method("get", { newurl, I<0>() }) >= self.attr("max_repeats")) || (py{ visited.size() } >= self.attr("max_redirections"))))
                            {
                                py_raise((Mod<"urllib.error">().attr("HTTPError"))(req.attr("full_url"), code, self.attr("inf_msg") + msg, headers, fp));
                            }
                        }
                        else
                        {
                            py const value27 = py_dict();
                            visited = value27;
                            new_.set_attr("redirect_dict", value27);
                            req.set_attr("redirect_dict", value27);
                        }
                        visited.set_item(newurl, visited.method("get", { newurl, I<0>() }) + (I<1>()));
                        // Don't close the fp until we are sure that we won't use it
                        // with HTTPError.
                        fp.method("read");
                        fp.method("close");
                        return self.attr("parent").method("open", { new_ }, { { "timeout", req.attr("timeout") } });
                    }) },
                { "http_error_301", w.method("HTTPRedirectHandler", "http_error_302", { py_positional("req"), py_positional("fp"), py_positional("code"), py_positional("msg"), py_positional("headers") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py fp = a[2];
                        py code = a[3];
                        py msg = a[4];
                        py headers = a[5];
                        py new_;
                        py newurl;
                        py urlparts;
                        py visited;
                        if (headers.contains((S<"location">())))
                        {
                            newurl = headers[S<"location">()];
                        }
                        else if (headers.contains((S<"uri">())))
                        {
                            newurl = headers[S<"uri">()];
                        }
                        else
                        {
                            return none();
                        }
                        // fix a possible malformed URL
                        urlparts = (Mod<"urllib.parse">().attr("urlparse"))(newurl);
                        // For security reasons we don't allow redirection to anything other
                        // than http, https or ftp.
                        if (!py_tuple({ S<"http">(), S<"https">(), S<"ftp">(), S<"">() }).contains(urlparts.attr("scheme")))
                        {
                            py_raise((Mod<"urllib.error">().attr("HTTPError"))(newurl, code, (S<"%s - Redirection to url '%s' is not allowed">()) % py_tuple({ msg, newurl }), headers, fp));
                        }
                        if (((!urlparts.attr("path").truthy()) && urlparts.attr("netloc").truthy()))
                        {
                            urlparts = (B<"list">())(urlparts);
                            urlparts.set_item(I<2>(), S<"/">());
                        }
                        newurl = (Mod<"urllib.parse">().attr("urlunparse"))(urlparts);
                        // http.client.parse_headers() decodes as ISO-8859-1.  Recover the
                        // original bytes and percent-encode non-ASCII bytes, and any special
                        // characters such as the space.
                        newurl = (Mod<"urllib.parse">().attr("quote")).call({ newurl }, { { "encoding", S<"iso-8859-1">() }, { "safe", Mod<"string">().attr("punctuation") } });
                        newurl = (Mod<"urllib.parse">().attr("urljoin"))(req.attr("full_url"), newurl);
                        // XXX Probably want to forget about the state of the current
                        // request, although that might interact poorly with other
                        // handlers that also use handler-specific request attributes
                        new_ = self.method("redirect_request", { req, fp, code, msg, headers, newurl });
                        if (new_.is_none())
                        {
                            return none();
                        }
                        // loop detection
                        // .redirect_dict has a key url if url was previously visited.
                        if (has_attr(req, "redirect_dict"))
                        {
                            py const value26 = req.attr("redirect_dict");
                            visited = value26;
                            new_.set_attr("redirect_dict", value26);
                            if (((visited.method("get", { newurl, I<0>() }) >= self.attr("max_repeats")) || (py{ visited.size() } >= self.attr("max_redirections"))))
                            {
                                py_raise((Mod<"urllib.error">().attr("HTTPError"))(req.attr("full_url"), code, self.attr("inf_msg") + msg, headers, fp));
                            }
                        }
                        else
                        {
                            py const value27 = py_dict();
                            visited = value27;
                            new_.set_attr("redirect_dict", value27);
                            req.set_attr("redirect_dict", value27);
                        }
                        visited.set_item(newurl, visited.method("get", { newurl, I<0>() }) + (I<1>()));
                        // Don't close the fp until we are sure that we won't use it
                        // with HTTPError.
                        fp.method("read");
                        fp.method("close");
                        return self.attr("parent").method("open", { new_ }, { { "timeout", req.attr("timeout") } });
                    }) },
                { "http_error_303", w.method("HTTPRedirectHandler", "http_error_302", { py_positional("req"), py_positional("fp"), py_positional("code"), py_positional("msg"), py_positional("headers") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py fp = a[2];
                        py code = a[3];
                        py msg = a[4];
                        py headers = a[5];
                        py new_;
                        py newurl;
                        py urlparts;
                        py visited;
                        if (headers.contains((S<"location">())))
                        {
                            newurl = headers[S<"location">()];
                        }
                        else if (headers.contains((S<"uri">())))
                        {
                            newurl = headers[S<"uri">()];
                        }
                        else
                        {
                            return none();
                        }
                        // fix a possible malformed URL
                        urlparts = (Mod<"urllib.parse">().attr("urlparse"))(newurl);
                        // For security reasons we don't allow redirection to anything other
                        // than http, https or ftp.
                        if (!py_tuple({ S<"http">(), S<"https">(), S<"ftp">(), S<"">() }).contains(urlparts.attr("scheme")))
                        {
                            py_raise((Mod<"urllib.error">().attr("HTTPError"))(newurl, code, (S<"%s - Redirection to url '%s' is not allowed">()) % py_tuple({ msg, newurl }), headers, fp));
                        }
                        if (((!urlparts.attr("path").truthy()) && urlparts.attr("netloc").truthy()))
                        {
                            urlparts = (B<"list">())(urlparts);
                            urlparts.set_item(I<2>(), S<"/">());
                        }
                        newurl = (Mod<"urllib.parse">().attr("urlunparse"))(urlparts);
                        // http.client.parse_headers() decodes as ISO-8859-1.  Recover the
                        // original bytes and percent-encode non-ASCII bytes, and any special
                        // characters such as the space.
                        newurl = (Mod<"urllib.parse">().attr("quote")).call({ newurl }, { { "encoding", S<"iso-8859-1">() }, { "safe", Mod<"string">().attr("punctuation") } });
                        newurl = (Mod<"urllib.parse">().attr("urljoin"))(req.attr("full_url"), newurl);
                        // XXX Probably want to forget about the state of the current
                        // request, although that might interact poorly with other
                        // handlers that also use handler-specific request attributes
                        new_ = self.method("redirect_request", { req, fp, code, msg, headers, newurl });
                        if (new_.is_none())
                        {
                            return none();
                        }
                        // loop detection
                        // .redirect_dict has a key url if url was previously visited.
                        if (has_attr(req, "redirect_dict"))
                        {
                            py const value26 = req.attr("redirect_dict");
                            visited = value26;
                            new_.set_attr("redirect_dict", value26);
                            if (((visited.method("get", { newurl, I<0>() }) >= self.attr("max_repeats")) || (py{ visited.size() } >= self.attr("max_redirections"))))
                            {
                                py_raise((Mod<"urllib.error">().attr("HTTPError"))(req.attr("full_url"), code, self.attr("inf_msg") + msg, headers, fp));
                            }
                        }
                        else
                        {
                            py const value27 = py_dict();
                            visited = value27;
                            new_.set_attr("redirect_dict", value27);
                            req.set_attr("redirect_dict", value27);
                        }
                        visited.set_item(newurl, visited.method("get", { newurl, I<0>() }) + (I<1>()));
                        // Don't close the fp until we are sure that we won't use it
                        // with HTTPError.
                        fp.method("read");
                        fp.method("close");
                        return self.attr("parent").method("open", { new_ }, { { "timeout", req.attr("timeout") } });
                    }) },
                { "http_error_307", w.method("HTTPRedirectHandler", "http_error_302", { py_positional("req"), py_positional("fp"), py_positional("code"), py_positional("msg"), py_positional("headers") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py fp = a[2];
                        py code = a[3];
                        py msg = a[4];
                        py headers = a[5];
                        py new_;
                        py newurl;
                        py urlparts;
                        py visited;
                        if (headers.contains((S<"location">())))
                        {
                            newurl = headers[S<"location">()];
                        }
                        else if (headers.contains((S<"uri">())))
                        {
                            newurl = headers[S<"uri">()];
                        }
                        else
                        {
                            return none();
                        }
                        // fix a possible malformed URL
                        urlparts = (Mod<"urllib.parse">().attr("urlparse"))(newurl);
                        // For security reasons we don't allow redirection to anything other
                        // than http, https or ftp.
                        if (!py_tuple({ S<"http">(), S<"https">(), S<"ftp">(), S<"">() }).contains(urlparts.attr("scheme")))
                        {
                            py_raise((Mod<"urllib.error">().attr("HTTPError"))(newurl, code, (S<"%s - Redirection to url '%s' is not allowed">()) % py_tuple({ msg, newurl }), headers, fp));
                        }
                        if (((!urlparts.attr("path").truthy()) && urlparts.attr("netloc").truthy()))
                        {
                            urlparts = (B<"list">())(urlparts);
                            urlparts.set_item(I<2>(), S<"/">());
                        }
                        newurl = (Mod<"urllib.parse">().attr("urlunparse"))(urlparts);
                        // http.client.parse_headers() decodes as ISO-8859-1.  Recover the
                        // original bytes and percent-encode non-ASCII bytes, and any special
                        // characters such as the space.
                        newurl = (Mod<"urllib.parse">().attr("quote")).call({ newurl }, { { "encoding", S<"iso-8859-1">() }, { "safe", Mod<"string">().attr("punctuation") } });
                        newurl = (Mod<"urllib.parse">().attr("urljoin"))(req.attr("full_url"), newurl);
                        // XXX Probably want to forget about the state of the current
                        // request, although that might interact poorly with other
                        // handlers that also use handler-specific request attributes
                        new_ = self.method("redirect_request", { req, fp, code, msg, headers, newurl });
                        if (new_.is_none())
                        {
                            return none();
                        }
                        // loop detection
                        // .redirect_dict has a key url if url was previously visited.
                        if (has_attr(req, "redirect_dict"))
                        {
                            py const value26 = req.attr("redirect_dict");
                            visited = value26;
                            new_.set_attr("redirect_dict", value26);
                            if (((visited.method("get", { newurl, I<0>() }) >= self.attr("max_repeats")) || (py{ visited.size() } >= self.attr("max_redirections"))))
                            {
                                py_raise((Mod<"urllib.error">().attr("HTTPError"))(req.attr("full_url"), code, self.attr("inf_msg") + msg, headers, fp));
                            }
                        }
                        else
                        {
                            py const value27 = py_dict();
                            visited = value27;
                            new_.set_attr("redirect_dict", value27);
                            req.set_attr("redirect_dict", value27);
                        }
                        visited.set_item(newurl, visited.method("get", { newurl, I<0>() }) + (I<1>()));
                        // Don't close the fp until we are sure that we won't use it
                        // with HTTPError.
                        fp.method("read");
                        fp.method("close");
                        return self.attr("parent").method("open", { new_ }, { { "timeout", req.attr("timeout") } });
                    }) },
                { "http_error_308", w.method("HTTPRedirectHandler", "http_error_302", { py_positional("req"), py_positional("fp"), py_positional("code"), py_positional("msg"), py_positional("headers") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py fp = a[2];
                        py code = a[3];
                        py msg = a[4];
                        py headers = a[5];
                        py new_;
                        py newurl;
                        py urlparts;
                        py visited;
                        if (headers.contains((S<"location">())))
                        {
                            newurl = headers[S<"location">()];
                        }
                        else if (headers.contains((S<"uri">())))
                        {
                            newurl = headers[S<"uri">()];
                        }
                        else
                        {
                            return none();
                        }
                        // fix a possible malformed URL
                        urlparts = (Mod<"urllib.parse">().attr("urlparse"))(newurl);
                        // For security reasons we don't allow redirection to anything other
                        // than http, https or ftp.
                        if (!py_tuple({ S<"http">(), S<"https">(), S<"ftp">(), S<"">() }).contains(urlparts.attr("scheme")))
                        {
                            py_raise((Mod<"urllib.error">().attr("HTTPError"))(newurl, code, (S<"%s - Redirection to url '%s' is not allowed">()) % py_tuple({ msg, newurl }), headers, fp));
                        }
                        if (((!urlparts.attr("path").truthy()) && urlparts.attr("netloc").truthy()))
                        {
                            urlparts = (B<"list">())(urlparts);
                            urlparts.set_item(I<2>(), S<"/">());
                        }
                        newurl = (Mod<"urllib.parse">().attr("urlunparse"))(urlparts);
                        // http.client.parse_headers() decodes as ISO-8859-1.  Recover the
                        // original bytes and percent-encode non-ASCII bytes, and any special
                        // characters such as the space.
                        newurl = (Mod<"urllib.parse">().attr("quote")).call({ newurl }, { { "encoding", S<"iso-8859-1">() }, { "safe", Mod<"string">().attr("punctuation") } });
                        newurl = (Mod<"urllib.parse">().attr("urljoin"))(req.attr("full_url"), newurl);
                        // XXX Probably want to forget about the state of the current
                        // request, although that might interact poorly with other
                        // handlers that also use handler-specific request attributes
                        new_ = self.method("redirect_request", { req, fp, code, msg, headers, newurl });
                        if (new_.is_none())
                        {
                            return none();
                        }
                        // loop detection
                        // .redirect_dict has a key url if url was previously visited.
                        if (has_attr(req, "redirect_dict"))
                        {
                            py const value26 = req.attr("redirect_dict");
                            visited = value26;
                            new_.set_attr("redirect_dict", value26);
                            if (((visited.method("get", { newurl, I<0>() }) >= self.attr("max_repeats")) || (py{ visited.size() } >= self.attr("max_redirections"))))
                            {
                                py_raise((Mod<"urllib.error">().attr("HTTPError"))(req.attr("full_url"), code, self.attr("inf_msg") + msg, headers, fp));
                            }
                        }
                        else
                        {
                            py const value27 = py_dict();
                            visited = value27;
                            new_.set_attr("redirect_dict", value27);
                            req.set_attr("redirect_dict", value27);
                        }
                        visited.set_item(newurl, visited.method("get", { newurl, I<0>() }) + (I<1>()));
                        // Don't close the fp until we are sure that we won't use it
                        // with HTTPError.
                        fp.method("read");
                        fp.method("close");
                        return self.attr("parent").method("open", { new_ }, { { "timeout", req.attr("timeout") } });
                    }) },
                { "inf_msg", S<"The HTTP server returned a redirect error that would lead to an infinite loop.\nThe last 30x error message was:\n">() }, }));

            define("urllib.request", "_parse_proxy", w.function("_parse_proxy", "_parse_proxy", { py_positional("proxy") }, [](arguments a) -> py
                {
                    py proxy = a[0];
                    py authority;
                    py end;
                    py host_separator;
                    py hostport;
                    py password;
                    py r_scheme;
                    py scheme;
                    py user;
                    py userinfo;
                    auto const items28 = unpack((Mod<"urllib.parse">().attr("_splittype"))(proxy), 2);
                    scheme = items28[0];
                    r_scheme = items28[1];
                    if (!r_scheme.method("startswith", { S<"/">() }).truthy())
                    {
                        // authority
                        scheme = none();
                        authority = proxy;
                    }
                    else
                    {
                        // URL
                        if (!r_scheme.method("startswith", { S<"//">() }).truthy())
                        {
                            py_raise((B<"ValueError">())((S<"proxy URL with no authority: %r">()) % proxy));
                        }
                        // We have an authority, so for RFC 3986-compliant URLs (by ss 3.
                        // and 3.3.), path is empty or starts with '/'
                        if (r_scheme.contains((S<"@">())))
                        {
                            host_separator = r_scheme.method("find", { S<"@">() });
                            end = r_scheme.method("find", { S<"/">(), host_separator });
                        }
                        else
                        {
                            end = r_scheme.method("find", { S<"/">(), I<2>() });
                        }
                        if (end == (I<-1>()))
                        {
                            end = none();
                        }
                        authority = r_scheme[B<"slice">()(I<2>(), end, none())];
                    }
                    auto const items29 = unpack((Mod<"urllib.parse">().attr("_splituser"))(authority), 2);
                    userinfo = items29[0];
                    hostport = items29[1];
                    if (!userinfo.is_none())
                    {
                        auto const items30 = unpack((Mod<"urllib.parse">().attr("_splitpasswd"))(userinfo), 2);
                        user = items30[0];
                        password = items30[1];
                    }
                    else
                    {
                        py const value31 = none();
                        user = value31;
                        password = value31;
                    }
                    return py_tuple({ scheme, user, password, hostport });
                }));

                // Proxies must be in front
            define("urllib.request", "ProxyHandler", w.make_class("ProxyHandler", { G<"urllib.request.BaseHandler">() }, {
                { "handler_order", I<100>() },
                { "__init__", w.method("ProxyHandler", "__init__", { py_positional("proxies", none()) }, [](arguments a) -> py
                    {
                        auto const self = std::make_shared<py>(a[0]);
                        py proxies = a[1];
                        py type;
                        auto const url = std::make_shared<py>();
                        if (proxies.is_none())
                        {
                            proxies = (G<"urllib.request.getproxies">())();
                        }
                        if (!has_attr(proxies, "keys"))
                            py_raise(B<"AssertionError">()(S<"proxies must be a mapping">()));
                        (*self).set_attr("proxies", proxies);
                        for (py const& item : iterate{ proxies.method("items") })
                        {
                            auto const items33 = unpack(item, 2);
                            type = items33[0];
                            *url = items33[1];
                            type = type.method("lower");
                            (B<"setattr">())((*self), (S<"%s_open">()) % type, w.function("<lambda>", "ProxyHandler.__init__.<locals>.<lambda>", { py_positional("r"), py_positional("proxy", (*url)), py_positional("type", type), py_positional("meth", (*self).attr("proxy_open")) }, [self, url](arguments a) -> py
        {
            py r = a[0];
            py proxy = a[1];
            py type = a[2];
            py meth = a[3];
            return meth(r, proxy, type);
        }));
                        }
                        return none();
                    }) },
                { "proxy_open", w.method("ProxyHandler", "proxy_open", { py_positional("req"), py_positional("proxy"), py_positional("type") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py proxy = a[2];
                        py type = a[3];
                        py creds;
                        py hostport;
                        py orig_type;
                        py password;
                        py proxy_type;
                        py user;
                        py user_pass;
                        orig_type = req.attr("type");
                        auto const items34 = unpack((G<"urllib.request._parse_proxy">())(proxy), 4);
                        proxy_type = items34[0];
                        user = items34[1];
                        password = items34[2];
                        hostport = items34[3];
                        if (proxy_type.is_none())
                        {
                            proxy_type = orig_type;
                        }
                        if ((req.attr("host").truthy() && (G<"urllib.request.proxy_bypass">())(req.attr("host")).truthy()))
                        {
                            return none();
                        }
                        if ((user.truthy() && password.truthy()))
                        {
                            user_pass = (S<"%s:%s">()) % py_tuple({ (Mod<"urllib.parse">().attr("unquote"))(user), (Mod<"urllib.parse">().attr("unquote"))(password) });
                            creds = (Mod<"base64">().attr("b64encode"))(user_pass.method("encode")).method("decode", { S<"ascii">() });
                            req.method("add_header", { S<"Proxy-authorization">(), (S<"Basic ">()) + creds });
                        }
                        hostport = (Mod<"urllib.parse">().attr("unquote"))(hostport);
                        req.method("set_proxy", { hostport, proxy_type });
                        if (((orig_type == proxy_type) || (orig_type == (S<"https">()))))
                        {
                            // let other handlers take care of it
                            return none();
                        }
                        else
                        {
                            // need to start over, because the other handlers don't
                            // grok the proxy's URL type
                            // e.g. if we have a constructor arg proxies like so:
                            // {'http': 'ftp://proxy.example.com'}, we may end up turning
                            // a request for http://acme.example.com/a into one for
                            // ftp://proxy.example.com/a
                            return self.attr("parent").method("open", { req }, { { "timeout", req.attr("timeout") } });
                        }
                        return none();
                    }) }, }));

            define("urllib.request", "HTTPPasswordMgr", w.make_class("HTTPPasswordMgr", {  }, {
                { "__init__", w.method("HTTPPasswordMgr", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("passwd", py_dict());
                        return none();
                    }) },
                { "add_password", w.method("HTTPPasswordMgr", "add_password", { py_positional("realm"), py_positional("uri"), py_positional("user"), py_positional("passwd") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py realm = a[1];
                        py uri = a[2];
                        py user = a[3];
                        py passwd = a[4];
                        py default_port;
                        py reduced_uri;
                        if (isinstance(uri, B<"str">()))
                        {
                            uri = py_list({ uri });
                        }
                        if (!self.attr("passwd").contains(realm))
                        {
                            self.attr("passwd").set_item(realm, py_dict());
                        }
                        for (py const& item : iterate{ py_tuple({ boolean(true), boolean(false) }) })
                        {
                            default_port = item;
                            reduced_uri = (B<"tuple">())([&]() -> py { py const comprehension36 = py_list(); py u; for (py const& item : iterate{ uri }) { u = item; comprehension36.method("append", { self.method("_reduce_uri_with_scheme", { u, default_port }) }); } return comprehension36; }());
                            self.attr("passwd")[realm].set_item(reduced_uri, py_tuple({ user, passwd }));
                        }
                        return none();
                    }) },
                { "find_user_password", w.method("HTTPPasswordMgr", "find_user_password", { py_positional("realm"), py_positional("authuri") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py realm = a[1];
                        py authuri = a[2];
                        py authinfo;
                        py default_port;
                        py domains;
                        py reduced_authuri;
                        py uri;
                        py uris;
                        domains = self.attr("passwd").method("get", { realm, py_dict() });
                        for (py const& item : iterate{ py_tuple({ boolean(true), boolean(false) }) })
                        {
                            default_port = item;
                            reduced_authuri = self.method("_reduce_uri_with_scheme", { authuri, default_port });
                            for (py const& item : iterate{ domains.method("items") })
                            {
                                auto const items39 = unpack(item, 2);
                                uris = items39[0];
                                authinfo = items39[1];
                                for (py const& item : iterate{ uris })
                                {
                                    uri = item;
                                    if (self.method("_is_suburi_with_scheme", { uri, reduced_authuri }).truthy())
                                    {
                                        return authinfo;
                                    }
                                }
                            }
                        }
                        return py_tuple({ none(), none() });
                    }) },
                { "reduce_uri", w.method("HTTPPasswordMgr", "reduce_uri", { py_positional("uri"), py_positional("default_port", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py uri = a[1];
                        py default_port = a[2];
                        py authority;
                        py dport;
                        py host;
                        py parts;
                        py path;
                        py port;
                        py scheme;
                        // note HTTP URLs do not have a userinfo component
                        parts = (Mod<"urllib.parse">().attr("urlsplit"))(uri);
                        if (parts[I<1>()].truthy())
                        {
                            // URI
                            scheme = parts[I<0>()];
                            authority = parts[I<1>()];
                            path = [&]() -> py { py const t = parts[I<2>()]; return t.truthy() ? t : S<"/">(); }();
                        }
                        else
                        {
                            // host or host:port
                            scheme = none();
                            authority = uri;
                            path = S<"/">();
                        }
                        auto const items41 = unpack((Mod<"urllib.parse">().attr("_splitport"))(authority), 2);
                        host = items41[0];
                        port = items41[1];
                        if ((default_port.truthy() && port.is_none() && (!scheme.is_none())))
                        {
                            dport = dict_of({ { S<"http">(), I<80>() }, { S<"https">(), I<443>() } }).method("get", { scheme });
                            if (!dport.is_none())
                            {
                                authority = (S<"%s:%d">()) % py_tuple({ host, dport });
                            }
                        }
                        return py_tuple({ authority, path });
                    }) },
                { "_reduce_uri_with_scheme", w.method("HTTPPasswordMgr", "_reduce_uri_with_scheme", { py_positional("uri"), py_positional("default_port", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py uri = a[1];
                        py default_port = a[2];
                        py parts;
                        py scheme;
                        parts = (Mod<"urllib.parse">().attr("urlsplit"))(uri);
                        scheme = (parts[I<1>()].truthy() ? parts[I<0>()] : none());
                        return B<"tuple">()(((py_list() + py_list({ [&]() -> py { py const t = scheme; return t.truthy() ? t : none(); }() })) + B<"list">()(self.method("reduce_uri", { uri, default_port }))));
                    }) },
                { "_is_suburi_with_scheme", w.method("HTTPPasswordMgr", "_is_suburi_with_scheme", { py_positional("base"), py_positional("test") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py base = a[1];
                        py test = a[2];
                        if (((!base[I<0>()].is_none()) && (!test[I<0>()].is_none()) && (base[I<0>()] != test[I<0>()])))
                        {
                            return boolean(false);
                        }
                        return self.method("is_suburi", { base[B<"slice">()(I<1>(), none(), none())], test[B<"slice">()(I<1>(), none(), none())] });
                    }) },
                { "is_suburi", w.method("HTTPPasswordMgr", "is_suburi", { py_positional("base"), py_positional("test") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py base = a[1];
                        py test = a[2];
                        py prefix;
                        if (base == test)
                        {
                            return boolean(true);
                        }
                        if (base[I<0>()] != test[I<0>()])
                        {
                            return boolean(false);
                        }
                        prefix = base[I<1>()];
                        if (prefix[B<"slice">()(I<-1>(), none(), none())] != (S<"/">()))
                        {
                            prefix = inplace(NEOS_PY_ADD, prefix, S<"/">());
                        }
                        return test[I<1>()].method("startswith", { prefix });
                    }) }, }));

            define("urllib.request", "HTTPPasswordMgrWithDefaultRealm", w.make_class("HTTPPasswordMgrWithDefaultRealm", { G<"urllib.request.HTTPPasswordMgr">() }, {
                { "find_user_password", w.method("HTTPPasswordMgrWithDefaultRealm", "find_user_password", { py_positional("realm"), py_positional("authuri") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py realm = a[1];
                        py authuri = a[2];
                        py password;
                        py user;
                        auto const items42 = unpack((G<"urllib.request.HTTPPasswordMgr">()).method("find_user_password", { self, realm, authuri }), 2);
                        user = items42[0];
                        password = items42[1];
                        if (!user.is_none())
                        {
                            return py_tuple({ user, password });
                        }
                        return (G<"urllib.request.HTTPPasswordMgr">()).method("find_user_password", { self, none(), authuri });
                    }) }, }));

            define("urllib.request", "HTTPPasswordMgrWithPriorAuth", w.make_class("HTTPPasswordMgrWithPriorAuth", { G<"urllib.request.HTTPPasswordMgrWithDefaultRealm">() }, {
                { "__init__", w.method("HTTPPasswordMgrWithPriorAuth", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("authenticated", py_dict());
                        (B<"super">()(G<"urllib.request.HTTPPasswordMgrWithPriorAuth">(), self)).method("__init__");
                        return none();
                    }) },
                { "add_password", w.method("HTTPPasswordMgrWithPriorAuth", "add_password", { py_positional("realm"), py_positional("uri"), py_positional("user"), py_positional("passwd"), py_positional("is_authenticated", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py realm = a[1];
                        py uri = a[2];
                        py user = a[3];
                        py passwd = a[4];
                        py is_authenticated = a[5];
                        self.method("update_authenticated", { uri, is_authenticated });
                        // Add a default for prior auth requests
                        if (!realm.is_none())
                        {
                            (B<"super">()(G<"urllib.request.HTTPPasswordMgrWithPriorAuth">(), self)).method("add_password", { none(), uri, user, passwd });
                        }
                        (B<"super">()(G<"urllib.request.HTTPPasswordMgrWithPriorAuth">(), self)).method("add_password", { realm, uri, user, passwd });
                        return none();
                    }) },
                { "update_authenticated", w.method("HTTPPasswordMgrWithPriorAuth", "update_authenticated", { py_positional("uri"), py_positional("is_authenticated", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py uri = a[1];
                        py is_authenticated = a[2];
                        py default_port;
                        py reduced_uri;
                        py u;
                        if (isinstance(uri, B<"str">()))
                        {
                            uri = py_list({ uri });
                        }
                        for (py const& item : iterate{ py_tuple({ boolean(true), boolean(false) }) })
                        {
                            default_port = item;
                            for (py const& item : iterate{ uri })
                            {
                                u = item;
                                reduced_uri = self.method("_reduce_uri_with_scheme", { u, default_port });
                                self.attr("authenticated").set_item(reduced_uri, is_authenticated);
                            }
                        }
                        return none();
                    }) },
                { "is_authenticated", w.method("HTTPPasswordMgrWithPriorAuth", "is_authenticated", { py_positional("authuri") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py authuri = a[1];
                        py default_port;
                        py reduced_authuri;
                        py uri;
                        for (py const& item : iterate{ py_tuple({ boolean(true), boolean(false) }) })
                        {
                            default_port = item;
                            reduced_authuri = self.method("_reduce_uri_with_scheme", { authuri, default_port });
                            for (py const& item : iterate{ self.attr("authenticated") })
                            {
                                uri = item;
                                if (self.method("_is_suburi_with_scheme", { uri, reduced_authuri }).truthy())
                                {
                                    return self.attr("authenticated")[uri];
                                }
                            }
                        }
                        return none();
                    }) }, }));

                // XXX this allows for multiple auth-schemes, but will stupidly pick
                // the last one with a realm specified.
                // allow for double- and single-quoted realm values
                // (single quotes are a violation of the RFC, but appear in the wild)
                // realm=xxx
                // realm='xxx'
                // realm="xxx"
                // XXX could pre-emptively send auth info already accepted (RFC 2617,
                // end of section 2, and section 1.2 immediately after "credentials"
                // production).
            define("urllib.request", "AbstractBasicAuthHandler", w.make_class("AbstractBasicAuthHandler", {  }, {
                { "rx", (Mod<"re">()).method("compile", { S<"(?:^|,)[ \t]*([^ \t,]+)[ \t]+realm=([\"']?)([^\"']*)\\2">(), Mod<"re">().attr("I") }) },
                { "__init__", w.method("AbstractBasicAuthHandler", "__init__", { py_positional("password_mgr", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py password_mgr = a[1];
                        if (password_mgr.is_none())
                        {
                            password_mgr = (G<"urllib.request.HTTPPasswordMgr">())();
                        }
                        self.set_attr("passwd", password_mgr);
                        self.set_attr("add_password", self.attr("passwd").attr("add_password"));
                        return none();
                    }) },
                { "_parse_realm", w.method("AbstractBasicAuthHandler", "_parse_realm", { py_positional("header") }, [](arguments a) -> py
                    {
                        return make_native_generator("_parse_realm", "AbstractBasicAuthHandler._parse_realm", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py header = a[1];
                            py found_challenge;
                            py mo;
                            py quote;
                            py realm;
                            py scheme;
                            found_challenge = boolean(false);
                            for (py const& item : iterate{ (G<"urllib.request.AbstractBasicAuthHandler">()).attr("rx").method("finditer", { header }) })
                            {
                                mo = item;
                                auto const items48 = unpack(mo.method("groups"), 3);
                                scheme = items48[0];
                                quote = items48[1];
                                realm = items48[2];
                                if (!py_list({ S<"\"">(), S<"'">() }).contains(quote))
                                {
                                    (Mod<"warnings">().attr("warn"))(S<"Basic Auth Realm was unquoted">(), B<"UserWarning">(), I<3>());
                                }
                                py_yield(py_tuple({ scheme, realm }));
                                found_challenge = boolean(true);
                            }
                            if (!found_challenge.truthy())
                            {
                                if (header.truthy())
                                {
                                    scheme = header.method("split")[I<0>()];
                                }
                                else
                                {
                                    scheme = S<"">();
                                }
                                py_yield(py_tuple({ scheme, none() }));
                            }
                            return none();
                        });
                    }) },
                { "http_error_auth_reqed", w.method("AbstractBasicAuthHandler", "http_error_auth_reqed", { py_positional("authreq"), py_positional("host"), py_positional("req"), py_positional("headers") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py authreq = a[1];
                        py host = a[2];
                        py req = a[3];
                        py headers = a[4];
                        py header;
                        py realm;
                        py scheme;
                        py unsupported;
                        headers = headers.method("get_all", { authreq });
                        if (!headers.truthy())
                        {
                            // no header found
                            return none();
                        }
                        unsupported = none();
                        for (py const& item : iterate{ headers })
                        {
                            header = item;
                            for (py const& item : iterate{ self.method("_parse_realm", { header }) })
                            {
                                auto const items51 = unpack(item, 2);
                                scheme = items51[0];
                                realm = items51[1];
                                if (scheme.method("lower") != (S<"basic">()))
                                {
                                    unsupported = scheme;
                                    continue;
                                }
                                if (!realm.is_none())
                                {
                                    // Use the first matching Basic challenge.
                                    // Ignore following challenges even if they use the Basic
                                    // scheme.
                                    return self.method("retry_http_basic_auth", { host, req, realm });
                                }
                            }
                        }
                        if (!unsupported.is_none())
                        {
                            py_raise((B<"ValueError">())((S<"AbstractBasicAuthHandler does not support the following scheme: %r">()) % py_tuple({ scheme })));
                        }
                        return none();
                    }) },
                { "retry_http_basic_auth", w.method("AbstractBasicAuthHandler", "retry_http_basic_auth", { py_positional("host"), py_positional("req"), py_positional("realm") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py req = a[2];
                        py realm = a[3];
                        py auth;
                        py pw;
                        py raw;
                        py user;
                        auto const items52 = unpack(self.attr("passwd").method("find_user_password", { realm, host }), 2);
                        user = items52[0];
                        pw = items52[1];
                        if (!pw.is_none())
                        {
                            raw = (S<"%s:%s">()) % py_tuple({ user, pw });
                            auth = (S<"Basic ">()) + (Mod<"base64">().attr("b64encode"))(raw.method("encode")).method("decode", { S<"ascii">() });
                            if (req.method("get_header", { self.attr("auth_header"), none() }) == auth)
                            {
                                return none();
                            }
                            req.method("add_unredirected_header", { self.attr("auth_header"), auth });
                            return self.attr("parent").method("open", { req }, { { "timeout", req.attr("timeout") } });
                        }
                        else
                        {
                            return none();
                        }
                        return none();
                    }) },
                { "http_request", w.method("AbstractBasicAuthHandler", "http_request", { py_positional("req") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py auth_str;
                        py credentials;
                        py passwd;
                        py user;
                        if (((!has_attr(self.attr("passwd"), "is_authenticated")) || (!self.attr("passwd").method("is_authenticated", { req.attr("full_url") }).truthy())))
                        {
                            return req;
                        }
                        if (!req.method("has_header", { S<"Authorization">() }).truthy())
                        {
                            auto const items53 = unpack(self.attr("passwd").method("find_user_password", { none(), req.attr("full_url") }), 2);
                            user = items53[0];
                            passwd = items53[1];
                            credentials = (S<"{0}:{1}">()).method("format", { user, passwd }).method("encode");
                            auth_str = (Mod<"base64">().attr("standard_b64encode"))(credentials).method("decode");
                            req.method("add_unredirected_header", { S<"Authorization">(), (S<"Basic {}">()).method("format", { auth_str.method("strip") }) });
                        }
                        return req;
                    }) },
                { "http_response", w.method("AbstractBasicAuthHandler", "http_response", { py_positional("req"), py_positional("response") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py response = a[2];
                        if (has_attr(self.attr("passwd"), "is_authenticated"))
                        {
                            if (((I<200>()) <= response.attr("code")) && (response.attr("code") < (I<300>())))
                            {
                                self.attr("passwd").method("update_authenticated", { req.attr("full_url"), boolean(true) });
                            }
                            else
                            {
                                self.attr("passwd").method("update_authenticated", { req.attr("full_url"), boolean(false) });
                            }
                        }
                        return response;
                    }) },
                { "https_request", w.method("AbstractBasicAuthHandler", "http_request", { py_positional("req") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py auth_str;
                        py credentials;
                        py passwd;
                        py user;
                        if (((!has_attr(self.attr("passwd"), "is_authenticated")) || (!self.attr("passwd").method("is_authenticated", { req.attr("full_url") }).truthy())))
                        {
                            return req;
                        }
                        if (!req.method("has_header", { S<"Authorization">() }).truthy())
                        {
                            auto const items53 = unpack(self.attr("passwd").method("find_user_password", { none(), req.attr("full_url") }), 2);
                            user = items53[0];
                            passwd = items53[1];
                            credentials = (S<"{0}:{1}">()).method("format", { user, passwd }).method("encode");
                            auth_str = (Mod<"base64">().attr("standard_b64encode"))(credentials).method("decode");
                            req.method("add_unredirected_header", { S<"Authorization">(), (S<"Basic {}">()).method("format", { auth_str.method("strip") }) });
                        }
                        return req;
                    }) },
                { "https_response", w.method("AbstractBasicAuthHandler", "http_response", { py_positional("req"), py_positional("response") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py response = a[2];
                        if (has_attr(self.attr("passwd"), "is_authenticated"))
                        {
                            if (((I<200>()) <= response.attr("code")) && (response.attr("code") < (I<300>())))
                            {
                                self.attr("passwd").method("update_authenticated", { req.attr("full_url"), boolean(true) });
                            }
                            else
                            {
                                self.attr("passwd").method("update_authenticated", { req.attr("full_url"), boolean(false) });
                            }
                        }
                        return response;
                    }) }, }));

            define("urllib.request", "HTTPBasicAuthHandler", w.make_class("HTTPBasicAuthHandler", { G<"urllib.request.AbstractBasicAuthHandler">(), G<"urllib.request.BaseHandler">() }, {
                { "auth_header", S<"Authorization">() },
                { "http_error_401", w.method("HTTPBasicAuthHandler", "http_error_401", { py_positional("req"), py_positional("fp"), py_positional("code"), py_positional("msg"), py_positional("headers") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py fp = a[2];
                        py code = a[3];
                        py msg = a[4];
                        py headers = a[5];
                        py response;
                        py url;
                        url = req.attr("full_url");
                        response = self.method("http_error_auth_reqed", { S<"www-authenticate">(), url, req, headers });
                        return response;
                    }) }, }));

            define("urllib.request", "ProxyBasicAuthHandler", w.make_class("ProxyBasicAuthHandler", { G<"urllib.request.AbstractBasicAuthHandler">(), G<"urllib.request.BaseHandler">() }, {
                { "auth_header", S<"Proxy-authorization">() },
                { "http_error_407", w.method("ProxyBasicAuthHandler", "http_error_407", { py_positional("req"), py_positional("fp"), py_positional("code"), py_positional("msg"), py_positional("headers") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py fp = a[2];
                        py code = a[3];
                        py msg = a[4];
                        py headers = a[5];
                        py authority;
                        py response;
                        authority = req.attr("host");
                        response = self.method("http_error_auth_reqed", { S<"proxy-authenticate">(), authority, req, headers });
                        return response;
                    }) }, }));

            // Return n random bytes.
            define("urllib.request", "_randombytes", Mod<"os">().attr("urandom"));
                // Digest authentication is specified in RFC 2617.
                // XXX The client does not inspect the Authentication-Info header
                // in a successful response.
                // XXX It should be possible to test this implementation against
                // a mock server that just generates a static set of challenges.
                // XXX qop="auth-int" supports is shaky
            define("urllib.request", "AbstractDigestAuthHandler", w.make_class("AbstractDigestAuthHandler", {  }, {
                { "__init__", w.method("AbstractDigestAuthHandler", "__init__", { py_positional("passwd", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py passwd = a[1];
                        if (passwd.is_none())
                        {
                            passwd = (G<"urllib.request.HTTPPasswordMgr">())();
                        }
                        self.set_attr("passwd", passwd);
                        self.set_attr("add_password", self.attr("passwd").attr("add_password"));
                        self.set_attr("retried", I<0>());
                        self.set_attr("nonce_count", I<0>());
                        self.set_attr("last_nonce", none());
                        return none();
                    }) },
                { "reset_retry_count", w.method("AbstractDigestAuthHandler", "reset_retry_count", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("retried", I<0>());
                        return none();
                    }) },
                { "http_error_auth_reqed", w.method("AbstractDigestAuthHandler", "http_error_auth_reqed", { py_positional("auth_header"), py_positional("host"), py_positional("req"), py_positional("headers") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py auth_header = a[1];
                        py host = a[2];
                        py req = a[3];
                        py headers = a[4];
                        py authreq;
                        py scheme;
                        authreq = headers.method("get", { auth_header, none() });
                        if (self.attr("retried") > (I<5>()))
                        {
                            // Don't fail endlessly - if we failed once, we'll probably
                            // fail a second time. Hm. Unless the Password Manager is
                            // prompting for the information. Crap. This isn't great
                            // but it's better than the current 'repeat until recursion
                            // depth exceeded' approach <wink>
                            py_raise((Mod<"urllib.error">().attr("HTTPError"))(req.attr("full_url"), I<401>(), S<"digest auth failed">(), headers, none()));
                        }
                        else
                        {
                            {
                                py const target54 = self;
                                target54.set_attr("retried", inplace(NEOS_PY_ADD, target54.attr("retried"), I<1>()));
                            }
                        }
                        if (authreq.truthy())
                        {
                            scheme = authreq.method("split")[I<0>()];
                            if (scheme.method("lower") == (S<"digest">()))
                            {
                                return self.method("retry_http_digest_auth", { req, authreq });
                            }
                            else if (scheme.method("lower") != (S<"basic">()))
                            {
                                py_raise((B<"ValueError">())((S<"AbstractDigestAuthHandler does not support the following scheme: '%s'">()) % scheme));
                            }
                        }
                        return none();
                    }) },
                { "retry_http_digest_auth", w.method("AbstractDigestAuthHandler", "retry_http_digest_auth", { py_positional("req"), py_positional("auth") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py auth = a[2];
                        py auth_val;
                        py chal;
                        py challenge;
                        py resp;
                        py token;
                        auto const items55 = unpack(auth.method("split", { S<" ">(), I<1>() }), 2);
                        token = items55[0];
                        challenge = items55[1];
                        chal = (G<"urllib.request.parse_keqv_list">())((B<"filter">())(none(), (G<"urllib.request.parse_http_list">())(challenge)));
                        auth = self.method("get_authorization", { req, chal });
                        if (auth.truthy())
                        {
                            auth_val = (S<"Digest %s">()) % auth;
                            if (req.attr("headers").method("get", { self.attr("auth_header"), none() }) == auth_val)
                            {
                                return none();
                            }
                            req.method("add_unredirected_header", { self.attr("auth_header"), auth_val });
                            resp = self.attr("parent").method("open", { req }, { { "timeout", req.attr("timeout") } });
                            return resp;
                        }
                        return none();
                    }) },
                { "get_cnonce", w.method("AbstractDigestAuthHandler", "get_cnonce", { py_positional("nonce") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py nonce = a[1];
                        py b;
                        py dig;
                        py s;
                        s = (S<"%s:%s:%s:">()) % py_tuple({ self.attr("nonce_count"), nonce, (Mod<"time">().attr("ctime"))() });
                        b = s.method("encode", { S<"ascii">() }) + (G<"urllib.request._randombytes">())(I<8>());
                        dig = (Mod<"hashlib">().attr("sha1"))(b).method("hexdigest");
                        return dig[B<"slice">()(none(), I<16>(), none())];
                    }) },
                { "get_authorization", w.method("AbstractDigestAuthHandler", "get_authorization", { py_positional("req"), py_positional("chal") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py chal = a[2];
                        py A1;
                        py A2;
                        py H;
                        py KD;
                        py algorithm;
                        py base;
                        py cnonce;
                        py entdig;
                        py ncvalue;
                        py nonce;
                        py noncebit;
                        py opaque;
                        py pw;
                        py qop;
                        py realm;
                        py respdig;
                        py user;
                        try
                        {
                            realm = chal[S<"realm">()];
                            nonce = chal[S<"nonce">()];
                            qop = chal.method("get", { S<"qop">() });
                            algorithm = chal.method("get", { S<"algorithm">(), S<"MD5">() });
                            // mod_digest doesn't send an opaque, even though it isn't
                            // supposed to be optional
                            opaque = chal.method("get", { S<"opaque">(), none() });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler56;
                            if (matches(handler56.exception, B<"KeyError">()))
                            {
                                return none();
                            }
                            else
                                py_reraise(handler56.exception);
                        }
                        auto const items57 = unpack(self.method("get_algorithm_impls", { algorithm }), 2);
                        H = items57[0];
                        KD = items57[1];
                        if (H.is_none())
                        {
                            return none();
                        }
                        auto const items58 = unpack(self.attr("passwd").method("find_user_password", { realm, req.attr("full_url") }), 2);
                        user = items58[0];
                        pw = items58[1];
                        if (user.is_none())
                        {
                            return none();
                        }
                        // XXX not implemented yet
                        if (!req.attr("data").is_none())
                        {
                            entdig = self.method("get_entity_digest", { req.attr("data"), chal });
                        }
                        else
                        {
                            entdig = none();
                        }
                        A1 = (S<"%s:%s:%s">()) % py_tuple({ user, realm, pw });
                        A2 = (S<"%s:%s">()) % py_tuple({ req.method("get_method"), req.attr("selector") });
                        // XXX selector: what about proxies and full urls
                        // NOTE: As per  RFC 2617, when server sends "auth,auth-int", the client could use either `auth`
                        // or `auth-int` to the response back. we use `auth` to send the response back.
                        if (qop.is_none())
                        {
                            respdig = KD(H(A1), (S<"%s:%s">()) % py_tuple({ nonce, H(A2) }));
                        }
                        else if (qop.method("split", { S<",">() }).contains((S<"auth">())))
                        {
                            if (nonce == self.attr("last_nonce"))
                            {
                                {
                                    py const target59 = self;
                                    target59.set_attr("nonce_count", inplace(NEOS_PY_ADD, target59.attr("nonce_count"), I<1>()));
                                }
                            }
                            else
                            {
                                self.set_attr("nonce_count", I<1>());
                                self.set_attr("last_nonce", nonce);
                            }
                            ncvalue = (S<"%08x">()) % self.attr("nonce_count");
                            cnonce = self.method("get_cnonce", { nonce });
                            noncebit = (S<"%s:%s:%s:%s:%s">()) % py_tuple({ nonce, ncvalue, cnonce, S<"auth">(), H(A2) });
                            respdig = KD(H(A1), noncebit);
                        }
                        else
                        {
                            // XXX handle auth-int.
                            py_raise((Mod<"urllib.error">().attr("URLError"))((S<"qop '%s' is not supported.">()) % qop));
                        }
                        // XXX should the partial digests be encoded too?
                        base = (S<"username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", response=\"%s\"">()) % py_tuple({ user, realm, nonce, req.attr("selector"), respdig });
                        if (opaque.truthy())
                        {
                            base = inplace(NEOS_PY_ADD, base, (S<", opaque=\"%s\"">()) % opaque);
                        }
                        if (entdig.truthy())
                        {
                            base = inplace(NEOS_PY_ADD, base, (S<", digest=\"%s\"">()) % entdig);
                        }
                        base = inplace(NEOS_PY_ADD, base, (S<", algorithm=\"%s\"">()) % algorithm);
                        if (qop.truthy())
                        {
                            base = inplace(NEOS_PY_ADD, base, (S<", qop=auth, nc=%s, cnonce=\"%s\"">()) % py_tuple({ ncvalue, cnonce }));
                        }
                        return base;
                    }) },
                { "get_algorithm_impls", w.method("AbstractDigestAuthHandler", "get_algorithm_impls", { py_positional("algorithm") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py algorithm = a[1];
                        auto const H = std::make_shared<py>();
                        py KD;
                        if (algorithm == (S<"MD5">()))
                        {
                            *H = w.function("<lambda>", "AbstractDigestAuthHandler.get_algorithm_impls.<locals>.<lambda>", { py_positional("x") }, [](arguments a) -> py
        {
            py x = a[0];
            return (Mod<"hashlib">().attr("md5"))(x.method("encode", { S<"ascii">() })).method("hexdigest");
        });
                        }
                        else if (algorithm == (S<"SHA">()))
                        {
                            *H = w.function("<lambda>", "AbstractDigestAuthHandler.get_algorithm_impls.<locals>.<lambda>", { py_positional("x") }, [](arguments a) -> py
        {
            py x = a[0];
            return (Mod<"hashlib">().attr("sha1"))(x.method("encode", { S<"ascii">() })).method("hexdigest");
        });
                        }
                        else
                        {
                            // XXX MD5-sess
                            py_raise((B<"ValueError">())((S<"Unsupported digest authentication algorithm %r">()) % algorithm));
                        }
                        KD = w.function("<lambda>", "AbstractDigestAuthHandler.get_algorithm_impls.<locals>.<lambda>", { py_positional("s"), py_positional("d") }, [H](arguments a) -> py
    {
        py s = a[0];
        py d = a[1];
        return (*H)((S<"%s:%s">()) % py_tuple({ s, d }));
    });
                        return py_tuple({ (*H), KD });
                    }) },
                { "get_entity_digest", w.method("AbstractDigestAuthHandler", "get_entity_digest", { py_positional("data"), py_positional("chal") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py chal = a[2];
                        return none();
                    }) }, }));

            define("urllib.request", "HTTPDigestAuthHandler", w.make_class("HTTPDigestAuthHandler", { G<"urllib.request.BaseHandler">(), G<"urllib.request.AbstractDigestAuthHandler">() }, {
                { "auth_header", S<"Authorization">() },
                { "handler_order", I<490>() },
                { "http_error_401", w.method("HTTPDigestAuthHandler", "http_error_401", { py_positional("req"), py_positional("fp"), py_positional("code"), py_positional("msg"), py_positional("headers") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py fp = a[2];
                        py code = a[3];
                        py msg = a[4];
                        py headers = a[5];
                        py host;
                        py retry;
                        host = (Mod<"urllib.parse">().attr("urlparse"))(req.attr("full_url"))[I<1>()];
                        retry = self.method("http_error_auth_reqed", { S<"www-authenticate">(), host, req, headers });
                        self.method("reset_retry_count");
                        return retry;
                    }) }, }));

            define("urllib.request", "ProxyDigestAuthHandler", w.make_class("ProxyDigestAuthHandler", { G<"urllib.request.BaseHandler">(), G<"urllib.request.AbstractDigestAuthHandler">() }, {
                { "auth_header", S<"Proxy-Authorization">() },
                { "handler_order", I<490>() },
                { "http_error_407", w.method("ProxyDigestAuthHandler", "http_error_407", { py_positional("req"), py_positional("fp"), py_positional("code"), py_positional("msg"), py_positional("headers") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py fp = a[2];
                        py code = a[3];
                        py msg = a[4];
                        py headers = a[5];
                        py host;
                        py retry;
                        host = req.attr("host");
                        retry = self.method("http_error_auth_reqed", { S<"proxy-authenticate">(), host, req, headers });
                        self.method("reset_retry_count");
                        return retry;
                    }) }, }));

            define("urllib.request", "AbstractHTTPHandler", w.make_class("AbstractHTTPHandler", { G<"urllib.request.BaseHandler">() }, {
                { "__init__", w.method("AbstractHTTPHandler", "__init__", { py_positional("debuglevel", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py debuglevel = a[1];
                        self.set_attr("_debuglevel", (!debuglevel.is_none() ? debuglevel : (Mod<"http.client">().attr("HTTPConnection")).attr("debuglevel")));
                        return none();
                    }) },
                { "set_http_debuglevel", w.method("AbstractHTTPHandler", "set_http_debuglevel", { py_positional("level") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py level = a[1];
                        self.set_attr("_debuglevel", level);
                        return none();
                    }) },
                { "_get_content_length", w.method("AbstractHTTPHandler", "_get_content_length", { py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py request = a[1];
                        return (Mod<"http.client">().attr("HTTPConnection")).method("_get_content_length", { request.attr("data"), request.method("get_method") });
                    }) },
                { "do_request_", w.method("AbstractHTTPHandler", "do_request_", { py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py request = a[1];
                        py content_length;
                        py data;
                        py host;
                        py msg;
                        py name;
                        py scheme;
                        py sel;
                        py sel_host;
                        py sel_path;
                        py value;
                        host = request.attr("host");
                        if (!host.truthy())
                        {
                            py_raise((Mod<"urllib.error">().attr("URLError"))(S<"no host given">()));
                        }
                        if (!request.attr("data").is_none())
                        {
                            data = request.attr("data");
                            if (isinstance(data, B<"str">()))
                            {
                                msg = S<"POST data should be bytes, an iterable of bytes, or a file object. It cannot be of type str.">();
                                py_raise((B<"TypeError">())(msg));
                            }
                            if (!request.method("has_header", { S<"Content-type">() }).truthy())
                            {
                                request.method("add_unredirected_header", { S<"Content-type">(), S<"application/x-www-form-urlencoded">() });
                            }
                            if (((!request.method("has_header", { S<"Content-length">() }).truthy()) && (!request.method("has_header", { S<"Transfer-encoding">() }).truthy())))
                            {
                                content_length = self.method("_get_content_length", { request });
                                if (!content_length.is_none())
                                {
                                    request.method("add_unredirected_header", { S<"Content-length">(), (B<"str">())(content_length) });
                                }
                                else
                                {
                                    request.method("add_unredirected_header", { S<"Transfer-encoding">(), S<"chunked">() });
                                }
                            }
                        }
                        sel_host = host;
                        if (request.method("has_proxy").truthy())
                        {
                            auto const items60 = unpack((Mod<"urllib.parse">().attr("_splittype"))(request.attr("selector")), 2);
                            scheme = items60[0];
                            sel = items60[1];
                            auto const items61 = unpack((Mod<"urllib.parse">().attr("_splithost"))(sel), 2);
                            sel_host = items61[0];
                            sel_path = items61[1];
                        }
                        if (!request.method("has_header", { S<"Host">() }).truthy())
                        {
                            request.method("add_unredirected_header", { S<"Host">(), sel_host });
                        }
                        for (py const& item : iterate{ self.attr("parent").attr("addheaders") })
                        {
                            auto const items63 = unpack(item, 2);
                            name = items63[0];
                            value = items63[1];
                            name = name.method("capitalize");
                            if (!request.method("has_header", { name }).truthy())
                            {
                                request.method("add_unredirected_header", { name, value });
                            }
                        }
                        return request;
                    }) },
                { "do_open", w.method("AbstractHTTPHandler", "do_open", { py_positional("http_class"), py_positional("req") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py http_class = a[1];
                        py req = a[2];
                        py http_conn_args = a[3];
                        py err;
                        py h;
                        py headers;
                        py host;
                        py proxy_auth_hdr;
                        py r;
                        py tunnel_headers;
                        host = req.attr("host");
                        if (!host.truthy())
                        {
                            py_raise((Mod<"urllib.error">().attr("URLError"))(S<"no host given">()));
                        }
                        // will parse host:port
                        h = call_ex(http_class, { host }, py{}, http_conn_args, { { "timeout", req.attr("timeout") } });
                        h.method("set_debuglevel", { self.attr("_debuglevel") });
                        headers = (B<"dict">())(req.attr("unredirected_hdrs"));
                        headers.method("update", { req.attr("headers") });
                        // TODO(jhylton): Should this be redesigned to handle
                        // persistent connections?
                        // We want to make an HTTP/1.1 request, but the addinfourl
                        // class isn't prepared to deal with a persistent connection.
                        // It will try to read all remaining data from the socket,
                        // which will block while the server waits for the next request.
                        // So make sure the connection gets closed after the (only)
                        // request.
                        headers.set_item(S<"Connection">(), S<"close">());
                        headers = [&]() -> py { py const comprehension64 = py_dict(); py name; py val; for (py const& item : iterate{ headers.method("items") }) { auto const items65 = unpack(item, 2); name = items65[0]; val = items65[1]; comprehension64.set_item(name.method("title"), val); } return comprehension64; }();
                        if (req.attr("_tunnel_host").truthy())
                        {
                            tunnel_headers = py_dict();
                            proxy_auth_hdr = S<"Proxy-Authorization">();
                            if (headers.contains(proxy_auth_hdr))
                            {
                                tunnel_headers.set_item(proxy_auth_hdr, headers[proxy_auth_hdr]);
                                // Proxy-Authorization should not be sent to origin
                                // server.
                                del_item(headers, proxy_auth_hdr);
                            }
                            h.method("set_tunnel", { req.attr("_tunnel_host") }, { { "headers", tunnel_headers } });
                        }
                        try
                        {
                            try
                            {
                                h.method("request", { req.method("get_method"), req.attr("selector"), req.attr("data"), headers }, { { "encode_chunked", req.method("has_header", { S<"Transfer-encoding">() }) } });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler67;
                                if (matches(handler67.exception, B<"OSError">()))
                                {
                                    err = handler67.exception;
                                    py_raise((Mod<"urllib.error">().attr("URLError"))(err));
                                }
                                else
                                    py_reraise(handler67.exception);
                            }
                            r = h.method("getresponse");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler66;
                            {
                                h.method("close");
                                py_reraise(handler66.exception);
                            }
                        }
                        // If the server does not send us a 'Connection: close' header,
                        // HTTPConnection assumes the socket should be left open. Manually
                        // mark the socket to be closed when this response object goes away.
                        if (h.attr("sock").truthy())
                        {
                            h.attr("sock").method("close");
                            h.set_attr("sock", none());
                        }
                        r.set_attr("url", req.method("get_full_url"));
                        // This line replaces the .msg attribute of the HTTPResponse
                        // with .headers, because urllib clients expect the response to
                        // have the reason in .msg.  It would be good to mark this
                        // attribute is deprecated and get then to use info() or
                        // .headers.
                        r.set_attr("msg", r.attr("reason"));
                        return r;
                    }, false, true) }, }));

            define("urllib.request", "HTTPHandler", w.make_class("HTTPHandler", { G<"urllib.request.AbstractHTTPHandler">() }, {
                { "http_open", w.method("HTTPHandler", "http_open", { py_positional("req") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        return self.method("do_open", { Mod<"http.client">().attr("HTTPConnection"), req });
                    }) },
                { "http_request", (G<"urllib.request.AbstractHTTPHandler">()).attr("do_request_") }, }));

            if ((B<"hasattr">())(Mod<"http.client">(), S<"HTTPSConnection">()).truthy())
            {
                define("urllib.request", "HTTPSHandler", w.make_class("HTTPSHandler", { G<"urllib.request.AbstractHTTPHandler">() }, {
                    { "__init__", w.method("HTTPSHandler", "__init__", { py_positional("debuglevel", none()), py_positional("context", none()), py_positional("check_hostname", none()) }, [](arguments a) -> py
                        {
                            py self = a[0];
                            py debuglevel = a[1];
                            py context = a[2];
                            py check_hostname = a[3];
                            py http_version;
                            debuglevel = (!debuglevel.is_none() ? debuglevel : (Mod<"http.client">().attr("HTTPSConnection")).attr("debuglevel"));
                            (G<"urllib.request.AbstractHTTPHandler">()).method("__init__", { self, debuglevel });
                            if (context.is_none())
                            {
                                http_version = (Mod<"http.client">().attr("HTTPSConnection")).attr("_http_vsn");
                                context = (Mod<"http.client">().attr("_create_https_context"))(http_version);
                            }
                            if (!check_hostname.is_none())
                            {
                                context.set_attr("check_hostname", check_hostname);
                            }
                            self.set_attr("_context", context);
                            return none();
                        }) },
                    { "https_open", w.method("HTTPSHandler", "https_open", { py_positional("req") }, [](arguments a) -> py
                        {
                            py self = a[0];
                            py req = a[1];
                            return self.method("do_open", { Mod<"http.client">().attr("HTTPSConnection"), req }, { { "context", self.attr("_context") } });
                        }) },
                    { "https_request", (G<"urllib.request.AbstractHTTPHandler">()).attr("do_request_") }, }));

                (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                    {
                        (G<"urllib.request.__all__">()).method("append", { S<"HTTPSHandler">() });
                        return none();
                    }))();
            }
            define("urllib.request", "HTTPCookieProcessor", w.make_class("HTTPCookieProcessor", { G<"urllib.request.BaseHandler">() }, {
                { "__init__", w.method("HTTPCookieProcessor", "__init__", { py_positional("cookiejar", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cookiejar = a[1];
                        py http;
                        http = module_of("http");
                        if (cookiejar.is_none())
                        {
                            cookiejar = http.attr("cookiejar").method("CookieJar");
                        }
                        self.set_attr("cookiejar", cookiejar);
                        return none();
                    }) },
                { "http_request", w.method("HTTPCookieProcessor", "http_request", { py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py request = a[1];
                        self.attr("cookiejar").method("add_cookie_header", { request });
                        return request;
                    }) },
                { "http_response", w.method("HTTPCookieProcessor", "http_response", { py_positional("request"), py_positional("response") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py request = a[1];
                        py response = a[2];
                        self.attr("cookiejar").method("extract_cookies", { response, request });
                        return response;
                    }) },
                { "https_request", w.method("HTTPCookieProcessor", "http_request", { py_positional("request") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py request = a[1];
                        self.attr("cookiejar").method("add_cookie_header", { request });
                        return request;
                    }) },
                { "https_response", w.method("HTTPCookieProcessor", "http_response", { py_positional("request"), py_positional("response") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py request = a[1];
                        py response = a[2];
                        self.attr("cookiejar").method("extract_cookies", { response, request });
                        return response;
                    }) }, }));

            define("urllib.request", "UnknownHandler", w.make_class("UnknownHandler", { G<"urllib.request.BaseHandler">() }, {
                { "unknown_open", w.method("UnknownHandler", "unknown_open", { py_positional("req") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py type;
                        type = req.attr("type");
                        py_raise((Mod<"urllib.error">().attr("URLError"))((S<"unknown url type: %s">()) % type));
                    }) }, }));

            define("urllib.request", "parse_keqv_list", w.function("parse_keqv_list", "parse_keqv_list", { py_positional("l") }, [](arguments a) -> py
                {
                    py l = a[0];
                    py elt;
                    py k;
                    py parsed;
                    py v;
                    parsed = py_dict();
                    for (py const& item : iterate{ l })
                    {
                        elt = item;
                        auto const items69 = unpack(elt.method("split", { S<"=">(), I<1>() }), 2);
                        k = items69[0];
                        v = items69[1];
                        if (((v[I<0>()] == (S<"\"">())) && (v[I<-1>()] == (S<"\"">()))))
                        {
                            v = v[B<"slice">()(I<1>(), I<-1>(), none())];
                        }
                        parsed.set_item(k, v);
                    }
                    return parsed;
                }));

            define("urllib.request", "parse_http_list", w.function("parse_http_list", "parse_http_list", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    py cur;
                    py escape;
                    py part;
                    py quote;
                    py res;
                    res = py_list();
                    part = S<"">();
                    py const value70 = boolean(false);
                    escape = value70;
                    quote = value70;
                    for (py const& item : iterate{ s })
                    {
                        cur = item;
                        if (escape.truthy())
                        {
                            part = inplace(NEOS_PY_ADD, part, cur);
                            escape = boolean(false);
                            continue;
                        }
                        if (quote.truthy())
                        {
                            if (cur == (S<"\\">()))
                            {
                                escape = boolean(true);
                                continue;
                            }
                            else if (cur == (S<"\"">()))
                            {
                                quote = boolean(false);
                            }
                            part = inplace(NEOS_PY_ADD, part, cur);
                            continue;
                        }
                        if (cur == (S<",">()))
                        {
                            res.method("append", { part });
                            part = S<"">();
                            continue;
                        }
                        if (cur == (S<"\"">()))
                        {
                            quote = boolean(true);
                        }
                        part = inplace(NEOS_PY_ADD, part, cur);
                    }
                    // append last part
                    if (part.truthy())
                    {
                        res.method("append", { part });
                    }
                    return [&]() -> py { py const comprehension72 = py_list(); py part; for (py const& item : iterate{ res }) { part = item; comprehension72.method("append", { part.method("strip") }); } return comprehension72; }();
                }));

                // Use local file or FTP depending on form of URL
                // names for the localhost
                // not entirely sure what the rules are here
            define("urllib.request", "FileHandler", w.make_class("FileHandler", { G<"urllib.request.BaseHandler">() }, {
                { "file_open", w.method("FileHandler", "file_open", { py_positional("req") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py url;
                        url = req.attr("selector");
                        if (((url[B<"slice">()(none(), I<2>(), none())] == (S<"//">())) && (url[B<"slice">()(I<2>(), I<3>(), none())] != (S<"/">())) && ([&]() -> py { py const t = req.attr("host"); return !t.truthy() ? t : boolean(req.attr("host") != (S<"localhost">())); }()).truthy()))
                        {
                            if (!self.method("get_names").contains(req.attr("host")))
                            {
                                py_raise((Mod<"urllib.error">().attr("URLError"))(S<"file:// scheme is supported only on localhost">()));
                            }
                        }
                        else
                        {
                            return self.method("open_local_file", { req });
                        }
                        return none();
                    }) },
                { "names", none() },
                { "get_names", w.method("FileHandler", "get_names", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if ((G<"urllib.request.FileHandler">()).attr("names").is_none())
                        {
                            try
                            {
                                (G<"urllib.request.FileHandler">()).set_attr("names", (B<"tuple">())((Mod<"socket">().attr("gethostbyname_ex"))(S<"localhost">())[I<2>()] + (Mod<"socket">().attr("gethostbyname_ex"))((Mod<"socket">().attr("gethostname"))())[I<2>()]));
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler73;
                                if (matches(handler73.exception, Mod<"socket">().attr("gaierror")))
                                {
                                    (G<"urllib.request.FileHandler">()).set_attr("names", py_tuple({ (Mod<"socket">().attr("gethostbyname"))(S<"localhost">()) }));
                                }
                                else
                                    py_reraise(handler73.exception);
                            }
                        }
                        return (G<"urllib.request.FileHandler">()).attr("names");
                    }) },
                { "open_local_file", w.method("FileHandler", "open_local_file", { py_positional("req") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py email;
                        py exp;
                        py filename;
                        py headers;
                        py host;
                        py localfile;
                        py mimetypes;
                        py modified;
                        py mtype;
                        py origurl;
                        py port;
                        py size;
                        py stats;
                        email = module_of("email");
                        mimetypes = module_of("mimetypes");
                        host = req.attr("host");
                        filename = req.attr("selector");
                        localfile = (G<"urllib.request.url2pathname">())(filename);
                        try
                        {
                            stats = (Mod<"os">().attr("stat"))(localfile);
                            size = stats.attr("st_size");
                            modified = email.attr("utils").method("formatdate", { stats.attr("st_mtime") }, { { "usegmt", boolean(true) } });
                            mtype = mimetypes.method("guess_type", { filename })[I<0>()];
                            headers = email.method("message_from_string", { (S<"Content-type: %s\nContent-length: %d\nLast-modified: %s\n">()) % py_tuple({ [&]() -> py { py const t = mtype; return t.truthy() ? t : S<"text/plain">(); }(), size, modified }) });
                            if (host.truthy())
                            {
                                auto const items75 = unpack((Mod<"urllib.parse">().attr("_splitport"))(host), 2);
                                host = items75[0];
                                port = items75[1];
                            }
                            if (((!host.truthy()) || ([&]() -> py { py const t = boolean(!port.truthy()); return !t.truthy() ? t : boolean(self.method("get_names").contains((G<"urllib.request._safe_gethostbyname">())(host))); }()).truthy()))
                            {
                                if (host.truthy())
                                {
                                    origurl = ((S<"file://">()) + host) + filename;
                                }
                                else
                                {
                                    origurl = (S<"file://">()) + filename;
                                }
                                return (Mod<"urllib.response">().attr("addinfourl"))((B<"open">())(localfile, S<"rb">()), headers, origurl);
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler74;
                            if (matches(handler74.exception, B<"OSError">()))
                            {
                                exp = handler74.exception;
                                py_raise((Mod<"urllib.error">().attr("URLError"))(exp));
                            }
                            else
                                py_reraise(handler74.exception);
                        }
                        py_raise((Mod<"urllib.error">().attr("URLError"))(S<"file not on local host">()));
                    }) }, }));

            define("urllib.request", "_safe_gethostbyname", w.function("_safe_gethostbyname", "_safe_gethostbyname", { py_positional("host") }, [](arguments a) -> py
                {
                    py host = a[0];
                    try
                    {
                        return (Mod<"socket">().attr("gethostbyname"))(host);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler76;
                        if (matches(handler76.exception, Mod<"socket">().attr("gaierror")))
                        {
                            return none();
                        }
                        else
                            py_reraise(handler76.exception);
                    }
                    return none();
                }));

            define("urllib.request", "FTPHandler", w.make_class("FTPHandler", { G<"urllib.request.BaseHandler">() }, {
                { "ftp_open", w.method("FTPHandler", "ftp_open", { py_positional("req") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py attr;
                        py attrs;
                        py dirs;
                        py exp;
                        py file;
                        py fp;
                        py ftplib;
                        py fw;
                        py headers;
                        py host;
                        py mimetypes;
                        py msg;
                        py mtype;
                        py passwd;
                        py path;
                        py port;
                        py retrlen;
                        py type;
                        py user;
                        py value;
                        ftplib = module_of("ftplib");
                        mimetypes = module_of("mimetypes");
                        host = req.attr("host");
                        if (!host.truthy())
                        {
                            py_raise((Mod<"urllib.error">().attr("URLError"))(S<"ftp error: no host given">()));
                        }
                        auto const items77 = unpack((Mod<"urllib.parse">().attr("_splitport"))(host), 2);
                        host = items77[0];
                        port = items77[1];
                        if (port.is_none())
                        {
                            port = ftplib.attr("FTP_PORT");
                        }
                        else
                        {
                            port = (B<"int">())(port);
                        }
                        // username/password handling
                        auto const items78 = unpack((Mod<"urllib.parse">().attr("_splituser"))(host), 2);
                        user = items78[0];
                        host = items78[1];
                        if (user.truthy())
                        {
                            auto const items79 = unpack((Mod<"urllib.parse">().attr("_splitpasswd"))(user), 2);
                            user = items79[0];
                            passwd = items79[1];
                        }
                        else
                        {
                            passwd = none();
                        }
                        host = (Mod<"urllib.parse">().attr("unquote"))(host);
                        user = [&]() -> py { py const t = user; return t.truthy() ? t : S<"">(); }();
                        passwd = [&]() -> py { py const t = passwd; return t.truthy() ? t : S<"">(); }();
                        try
                        {
                            host = (Mod<"socket">().attr("gethostbyname"))(host);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler80;
                            if (matches(handler80.exception, B<"OSError">()))
                            {
                                msg = handler80.exception;
                                py_raise((Mod<"urllib.error">().attr("URLError"))(msg));
                            }
                            else
                                py_reraise(handler80.exception);
                        }
                        auto const items81 = unpack((Mod<"urllib.parse">().attr("_splitattr"))(req.attr("selector")), 2);
                        path = items81[0];
                        attrs = items81[1];
                        dirs = path.method("split", { S<"/">() });
                        dirs = (B<"list">())((B<"map">())(Mod<"urllib.parse">().attr("unquote"), dirs));
                        auto const items82 = unpack(py_tuple({ dirs[B<"slice">()(none(), I<-1>(), none())], dirs[I<-1>()] }), 2);
                        dirs = items82[0];
                        file = items82[1];
                        if ((dirs.truthy() && (!dirs[I<0>()].truthy())))
                        {
                            dirs = dirs[B<"slice">()(I<1>(), none(), none())];
                        }
                        fw = none();
                        try
                        {
                            fw = self.method("connect_ftp", { user, passwd, host, port, dirs, req.attr("timeout") });
                            type = [&]() -> py { py const t = [&]() -> py { py const t = file; return !t.truthy() ? t : S<"I">(); }(); return t.truthy() ? t : S<"D">(); }();
                            for (py const& item : iterate{ attrs })
                            {
                                attr = item;
                                auto const items85 = unpack((Mod<"urllib.parse">().attr("_splitvalue"))(attr), 2);
                                attr = items85[0];
                                value = items85[1];
                                if (((attr.method("lower") == (S<"type">())) && py_tuple({ S<"a">(), S<"A">(), S<"i">(), S<"I">(), S<"d">(), S<"D">() }).contains(value)))
                                {
                                    type = value.method("upper");
                                }
                            }
                            auto const items86 = unpack(fw.method("retrfile", { file, type }), 2);
                            fp = items86[0];
                            retrlen = items86[1];
                            headers = S<"">();
                            mtype = mimetypes.method("guess_type", { req.attr("full_url") })[I<0>()];
                            if (mtype.truthy())
                            {
                                headers = inplace(NEOS_PY_ADD, headers, (S<"Content-type: %s\n">()) % mtype);
                            }
                            if (((!retrlen.is_none()) && (retrlen >= (I<0>()))))
                            {
                                headers = inplace(NEOS_PY_ADD, headers, (S<"Content-length: %d\n">()) % retrlen);
                            }
                            headers = (Mod<"email">().attr("message_from_string"))(headers);
                            return (Mod<"urllib.response">().attr("addinfourl"))(fp, headers, req.attr("full_url"));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler83;
                            if (matches(handler83.exception, B<"Exception">()))
                            {
                                exp = handler83.exception;
                                if (((!fw.is_none()) && (!fw.attr("keepalive").truthy())))
                                {
                                    fw.method("close");
                                }
                                if (isinstance(exp, ftplib.attr("all_errors")))
                                {
                                    fn_raise_from((Mod<"urllib.error">().attr("URLError"))(exp).get(), exp.get());
                                    throw raised{};
                                }
                                py_reraise(handler83.exception);
                            }
                            else
                                py_reraise(handler83.exception);
                        }
                        return none();
                    }) },
                { "connect_ftp", w.method("FTPHandler", "connect_ftp", { py_positional("user"), py_positional("passwd"), py_positional("host"), py_positional("port"), py_positional("dirs"), py_positional("timeout") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py user = a[1];
                        py passwd = a[2];
                        py host = a[3];
                        py port = a[4];
                        py dirs = a[5];
                        py timeout = a[6];
                        return (G<"urllib.request.ftpwrapper">()).call({ user, passwd, host, port, dirs, timeout }, { { "persistent", boolean(false) } });
                    }) }, }));

                // XXX would be nice to have pluggable cache strategies
                // XXX this stuff is definitely not thread safe
            define("urllib.request", "CacheFTPHandler", w.make_class("CacheFTPHandler", { G<"urllib.request.FTPHandler">() }, {
                { "__init__", w.method("CacheFTPHandler", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("cache", py_dict());
                        self.set_attr("timeout", py_dict());
                        self.set_attr("soonest", I<0>());
                        self.set_attr("delay", I<60>());
                        self.set_attr("max_conns", I<16>());
                        return none();
                    }) },
                { "setTimeout", w.method("CacheFTPHandler", "setTimeout", { py_positional("t") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py t = a[1];
                        self.set_attr("delay", t);
                        return none();
                    }) },
                { "setMaxConns", w.method("CacheFTPHandler", "setMaxConns", { py_positional("m") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py m = a[1];
                        self.set_attr("max_conns", m);
                        return none();
                    }) },
                { "connect_ftp", w.method("CacheFTPHandler", "connect_ftp", { py_positional("user"), py_positional("passwd"), py_positional("host"), py_positional("port"), py_positional("dirs"), py_positional("timeout") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py user = a[1];
                        py passwd = a[2];
                        py host = a[3];
                        py port = a[4];
                        py dirs = a[5];
                        py timeout = a[6];
                        py conn;
                        py key;
                        key = py_tuple({ user, host, port, (S<"/">()).method("join", { dirs }), timeout });
                        conn = self.attr("cache").method("get", { key });
                        if ((conn.is_none() || (!conn.attr("keepalive").truthy())))
                        {
                            if (!conn.is_none())
                            {
                                conn.method("close");
                            }
                            py const value87 = (G<"urllib.request.ftpwrapper">())(user, passwd, host, port, dirs, timeout);
                            conn = value87;
                            self.attr("cache").set_item(key, value87);
                        }
                        self.attr("timeout").set_item(key, (Mod<"time">().attr("time"))() + self.attr("delay"));
                        self.method("check_cache");
                        return conn;
                    }) },
                { "check_cache", w.method("CacheFTPHandler", "check_cache", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py k;
                        py t;
                        py v;
                        t = (Mod<"time">().attr("time"))();
                        if (self.attr("soonest") <= t)
                        {
                            for (py const& item : iterate{ (B<"list">())(self.attr("timeout").method("items")) })
                            {
                                auto const items89 = unpack(item, 2);
                                k = items89[0];
                                v = items89[1];
                                if (v < t)
                                {
                                    self.attr("cache")[k].method("close");
                                    del_item(self.attr("cache"), k);
                                    del_item(self.attr("timeout"), k);
                                }
                            }
                        }
                        self.set_attr("soonest", (B<"min">())((B<"list">())(self.attr("timeout").method("values"))));
                        // then check the size
                        if (py{ self.attr("cache").size() } == self.attr("max_conns"))
                        {
                            for (py const& item : iterate{ (B<"list">())(self.attr("timeout").method("items")) })
                            {
                                auto const items91 = unpack(item, 2);
                                k = items91[0];
                                v = items91[1];
                                if (v == self.attr("soonest"))
                                {
                                    del_item(self.attr("cache"), k);
                                    del_item(self.attr("timeout"), k);
                                    break;
                                }
                            }
                            self.set_attr("soonest", (B<"min">())((B<"list">())(self.attr("timeout").method("values"))));
                        }
                        return none();
                    }) },
                { "clear_cache", w.method("CacheFTPHandler", "clear_cache", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py conn;
                        for (py const& item : iterate{ self.attr("cache").method("values") })
                        {
                            conn = item;
                            conn.method("close");
                        }
                        self.attr("cache").method("clear");
                        self.attr("timeout").method("clear");
                        return none();
                    }) }, }));

            define("urllib.request", "DataHandler", w.make_class("DataHandler", { G<"urllib.request.BaseHandler">() }, {
                { "data_open", w.method("DataHandler", "data_open", { py_positional("req") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py req = a[1];
                        py data;
                        py headers;
                        py mediatype;
                        py scheme;
                        py url;
                        url = req.attr("full_url");
                        auto const items93 = unpack(url.method("split", { S<":">(), I<1>() }), 2);
                        scheme = items93[0];
                        data = items93[1];
                        auto const items94 = unpack(data.method("split", { S<",">(), I<1>() }), 2);
                        mediatype = items94[0];
                        data = items94[1];
                        // Disallow control characters within mediatype.
                        if ((Mod<"re">().attr("search"))(S<"[\\x00-\\x1F\\x7F]">(), mediatype).truthy())
                        {
                            py_raise((B<"ValueError">())(S<"Control characters not allowed in data: mediatype">()));
                        }
                        // even base64 encoded data URLs might be quoted so unquote in any case:
                        data = (Mod<"urllib.parse">().attr("unquote_to_bytes"))(data);
                        if (mediatype.method("endswith", { S<";base64">() }).truthy())
                        {
                            data = (Mod<"base64">().attr("decodebytes"))(data);
                            mediatype = mediatype[B<"slice">()(none(), I<-7>(), none())];
                        }
                        if (!mediatype.truthy())
                        {
                            mediatype = S<"text/plain;charset=US-ASCII">();
                        }
                        headers = (Mod<"email">().attr("message_from_string"))((S<"Content-type: %s\nContent-length: %d\n">()) % py_tuple({ mediatype, py{ data.size() } }));
                        return (Mod<"urllib.response">().attr("addinfourl"))((Mod<"io">().attr("BytesIO"))(data), headers, url);
                    }) }, }));

            // Code move from the old urllib module
            define("urllib.request", "MAXFTPCACHE", I<10>());
            // Helper for non-unix systems
#if defined(_WIN32)
#else
            define("urllib.request", "url2pathname", w.function("url2pathname", "url2pathname", { py_positional("pathname") }, [](arguments a) -> py
                {
                    py pathname = a[0];
                    py encoding;
                    py errors;
                    if (pathname[B<"slice">()(none(), I<3>(), none())] == (S<"///">()))
                    {
                        // URL has an empty authority section, so the path begins on the
                        // third character.
                        pathname = pathname[B<"slice">()(I<2>(), none(), none())];
                    }
                    else if (pathname[B<"slice">()(none(), I<12>(), none())] == (S<"//localhost/">()))
                    {
                        // Skip past 'localhost' authority.
                        pathname = pathname[B<"slice">()(I<11>(), none(), none())];
                    }
                    encoding = (Mod<"sys">().attr("getfilesystemencoding"))();
                    errors = (Mod<"sys">().attr("getfilesystemencodeerrors"))();
                    return (Mod<"urllib.parse">().attr("unquote")).call({ pathname }, { { "encoding", encoding }, { "errors", errors } });
                }));

            define("urllib.request", "pathname2url", w.function("pathname2url", "pathname2url", { py_positional("pathname") }, [](arguments a) -> py
                {
                    py pathname = a[0];
                    py encoding;
                    py errors;
                    if (pathname[B<"slice">()(none(), I<2>(), none())] == (S<"//">()))
                    {
                        // Add explicitly empty authority to avoid interpreting the path
                        // as authority.
                        pathname = (S<"//">()) + pathname;
                    }
                    encoding = (Mod<"sys">().attr("getfilesystemencoding"))();
                    errors = (Mod<"sys">().attr("getfilesystemencodeerrors"))();
                    return (Mod<"urllib.parse">().attr("quote")).call({ pathname }, { { "encoding", encoding }, { "errors", errors } });
                }));

#endif
            define("urllib.request", "ftpcache", py_dict());
                // Constructor
                // Undocumented feature: you can use a different
                // ftp cache by assigning to the .ftpcache member;
                // in case you want logically independent URL openers
                // XXX This is not threadsafe.  Bah.
                // External interface
                // External interface
                // Each method named open_<type> knows how to open that type of URL
                // cert and key file means the user wants to authenticate.
                // enable TLS 1.3 PHA implicitly even for custom contexts.
            define("urllib.request", "URLopener", w.make_class("URLopener", {  }, {
                { "_URLopener__tempfiles", none() },
                { "version", (S<"Python-urllib/%s">()) % (G<"urllib.request.__version__">()) },
                { "__init__", w.method("URLopener", "__init__", { py_positional("proxies", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py proxies = a[1];
                        py x509 = a[2];
                        py msg;
                        msg = (S<"%(class)s style of invoking requests is deprecated. Use newer urlopen functions/methods">()) % dict_of({ { S<"class">(), self.attr("__class__").attr("__name__") } });
                        (Mod<"warnings">().attr("warn")).call({ msg, B<"DeprecationWarning">() }, { { "stacklevel", I<2>() } });
                        if (proxies.is_none())
                        {
                            proxies = (G<"urllib.request.getproxies">())();
                        }
                        if (!has_attr(proxies, "keys"))
                            py_raise(B<"AssertionError">()(S<"proxies must be a mapping">()));
                        self.set_attr("proxies", proxies);
                        self.set_attr("key_file", x509.method("get", { S<"key_file">() }));
                        self.set_attr("cert_file", x509.method("get", { S<"cert_file">() }));
                        self.set_attr("addheaders", py_list({ py_tuple({ S<"User-Agent">(), self.attr("version") }), py_tuple({ S<"Accept">(), S<"*/*">() }) }));
                        self.set_attr("_URLopener__tempfiles", py_list());
                        self.set_attr("_URLopener__unlink", Mod<"os">().attr("unlink"));
                        self.set_attr("tempcache", none());
                        // Undocumented feature: if you assign {} to tempcache,
                        // it is used to cache files retrieved with
                        // self.retrieve().  This is not enabled by default
                        // since it does not work for changing documents (and I
                        // haven't got the logic to check expiration headers
                        // yet).
                        self.set_attr("ftpcache", G<"urllib.request.ftpcache">());
                        return none();
                    }, false, true) },
                { "__del__", w.method("URLopener", "__del__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("close");
                        return none();
                    }) },
                { "close", w.method("URLopener", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("cleanup");
                        return none();
                    }) },
                { "cleanup", w.method("URLopener", "cleanup", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file;
                        if (self.attr("_URLopener__tempfiles").truthy())
                        {
                            for (py const& item : iterate{ self.attr("_URLopener__tempfiles") })
                            {
                                file = item;
                                try
                                {
                                    self.method("_URLopener__unlink", { file });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler96;
                                    if (matches(handler96.exception, B<"OSError">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler96.exception);
                                }
                            }
                            del_item(self.attr("_URLopener__tempfiles"), B<"slice">()(none(), none(), none()));
                        }
                        if (self.attr("tempcache").truthy())
                        {
                            self.attr("tempcache").method("clear");
                        }
                        return none();
                    }) },
                { "addheader", w.method("URLopener", "addheader", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        self.attr("addheaders").method("append", { args });
                        return none();
                    }, true) },
                { "open", w.method("URLopener", "open", { py_positional("fullurl"), py_positional("data", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fullurl = a[1];
                        py data = a[2];
                        py filename;
                        py fp;
                        py headers;
                        py host;
                        py msg;
                        py name;
                        py proxy;
                        py proxyhost;
                        py selector;
                        py url;
                        py urltype;
                        fullurl = (Mod<"urllib.parse">().attr("unwrap"))((Mod<"urllib.parse">().attr("_to_bytes"))(fullurl));
                        fullurl = (Mod<"urllib.parse">().attr("quote")).call({ fullurl }, { { "safe", S<"%/:=&?~#+!$,;'@()*[]|">() } });
                        if ((self.attr("tempcache").truthy() && self.attr("tempcache").contains(fullurl)))
                        {
                            auto const items97 = unpack(self.attr("tempcache")[fullurl], 2);
                            filename = items97[0];
                            headers = items97[1];
                            fp = (B<"open">())(filename, S<"rb">());
                            return (Mod<"urllib.response">().attr("addinfourl"))(fp, headers, fullurl);
                        }
                        auto const items98 = unpack((Mod<"urllib.parse">().attr("_splittype"))(fullurl), 2);
                        urltype = items98[0];
                        url = items98[1];
                        if (!urltype.truthy())
                        {
                            urltype = S<"file">();
                        }
                        if (self.attr("proxies").contains(urltype))
                        {
                            proxy = self.attr("proxies")[urltype];
                            auto const items99 = unpack((Mod<"urllib.parse">().attr("_splittype"))(proxy), 2);
                            urltype = items99[0];
                            proxyhost = items99[1];
                            auto const items100 = unpack((Mod<"urllib.parse">().attr("_splithost"))(proxyhost), 2);
                            host = items100[0];
                            selector = items100[1];
                            url = py_tuple({ host, fullurl });
                        }
                        else
                        {
                            proxy = none();
                        }
                        name = (S<"open_">()) + urltype;
                        self.set_attr("type", urltype);
                        name = name.method("replace", { S<"-">(), S<"_">() });
                        if (((!(B<"hasattr">())(self, name).truthy()) || (name == (S<"open_local_file">()))))
                        {
                            if (proxy.truthy())
                            {
                                return self.method("open_unknown_proxy", { proxy, fullurl, data });
                            }
                            else
                            {
                                return self.method("open_unknown", { fullurl, data });
                            }
                        }
                        try
                        {
                            if (data.is_none())
                            {
                                return (B<"getattr">())(self, name)(url);
                            }
                            else
                            {
                                return (B<"getattr">())(self, name)(url, data);
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler101;
                            if (matches(handler101.exception, py_tuple({ Mod<"urllib.error">().attr("HTTPError"), Mod<"urllib.error">().attr("URLError") })))
                            {
                                py_reraise(handler101.exception);
                            }
                            else if (matches(handler101.exception, B<"OSError">()))
                            {
                                msg = handler101.exception;
                                fn_raise_from((B<"OSError">())(S<"socket error">(), msg).get(), msg.get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler101.exception);
                        }
                        return none();
                    }) },
                { "open_unknown", w.method("URLopener", "open_unknown", { py_positional("fullurl"), py_positional("data", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fullurl = a[1];
                        py data = a[2];
                        py type;
                        py url;
                        auto const items102 = unpack((Mod<"urllib.parse">().attr("_splittype"))(fullurl), 2);
                        type = items102[0];
                        url = items102[1];
                        py_raise((B<"OSError">())(S<"url error">(), S<"unknown url type">(), type));
                    }) },
                { "open_unknown_proxy", w.method("URLopener", "open_unknown_proxy", { py_positional("proxy"), py_positional("fullurl"), py_positional("data", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py proxy = a[1];
                        py fullurl = a[2];
                        py data = a[3];
                        py type;
                        py url;
                        auto const items103 = unpack((Mod<"urllib.parse">().attr("_splittype"))(fullurl), 2);
                        type = items103[0];
                        url = items103[1];
                        py_raise((B<"OSError">())(S<"url error">(), (S<"invalid proxy for %s">()) % type, proxy));
                    }) },
                { "retrieve", w.method("URLopener", "retrieve", { py_positional("url"), py_positional("filename", none()), py_positional("reporthook", none()), py_positional("data", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py filename = a[2];
                        py reporthook = a[3];
                        py data = a[4];
                        py block;
                        py blocknum;
                        py bs;
                        py fd;
                        py fp;
                        py garbage;
                        py hdrs;
                        py headers;
                        py path;
                        py read;
                        py result;
                        py size;
                        py suffix;
                        py tfp;
                        py type;
                        py url1;
                        url = (Mod<"urllib.parse">().attr("unwrap"))((Mod<"urllib.parse">().attr("_to_bytes"))(url));
                        if ((self.attr("tempcache").truthy() && self.attr("tempcache").contains(url)))
                        {
                            return self.attr("tempcache")[url];
                        }
                        auto const items104 = unpack((Mod<"urllib.parse">().attr("_splittype"))(url), 2);
                        type = items104[0];
                        url1 = items104[1];
                        if ((filename.is_none() && ([&]() -> py { py const t = boolean(!type.truthy()); return t.truthy() ? t : boolean(type == (S<"file">())); }()).truthy()))
                        {
                            try
                            {
                                fp = self.method("open_local_file", { url1 });
                                hdrs = fp.method("info");
                                fp.method("close");
                                return py_tuple({ (G<"urllib.request.url2pathname">())((Mod<"urllib.parse">().attr("_splithost"))(url1)[I<1>()]), hdrs });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler105;
                                if (matches(handler105.exception, B<"OSError">()))
                                {
                                }
                                else
                                    py_reraise(handler105.exception);
                            }
                        }
                        fp = self.method("open", { url, data });
                        {
                            std::optional<py> pending106;
                            [[maybe_unused]] int flow106 = 0;
                            [[maybe_unused]] py result106;
                            try
                            {
                                headers = fp.method("info");
                                if (filename.truthy())
                                {
                                    tfp = (B<"open">())(filename, S<"wb">());
                                }
                                else
                                {
                                    auto const items107 = unpack((Mod<"urllib.parse">().attr("_splittype"))(url), 2);
                                    garbage = items107[0];
                                    path = items107[1];
                                    auto const items108 = unpack((Mod<"urllib.parse">().attr("_splithost"))([&]() -> py { py const t = path; return t.truthy() ? t : S<"">(); }()), 2);
                                    garbage = items108[0];
                                    path = items108[1];
                                    auto const items109 = unpack((Mod<"urllib.parse">().attr("_splitquery"))([&]() -> py { py const t = path; return t.truthy() ? t : S<"">(); }()), 2);
                                    path = items109[0];
                                    garbage = items109[1];
                                    auto const items110 = unpack((Mod<"urllib.parse">().attr("_splitattr"))([&]() -> py { py const t = path; return t.truthy() ? t : S<"">(); }()), 2);
                                    path = items110[0];
                                    garbage = items110[1];
                                    suffix = (Mod<"os">().attr("path")).method("splitext", { path })[I<1>()];
                                    auto const items111 = unpack((Mod<"tempfile">().attr("mkstemp"))(suffix), 2);
                                    fd = items111[0];
                                    filename = items111[1];
                                    self.attr("_URLopener__tempfiles").method("append", { filename });
                                    tfp = (Mod<"os">().attr("fdopen"))(fd, S<"wb">());
                                }
                                {
                                    std::optional<py> pending112;
                                    [[maybe_unused]] int flow112 = 0;
                                    [[maybe_unused]] py result112;
                                    try
                                    {
                                        result = py_tuple({ filename, headers });
                                        if (!self.attr("tempcache").is_none())
                                        {
                                            self.attr("tempcache").set_item(url, result);
                                        }
                                        bs = (I<1024>()) * (I<8>());
                                        size = I<-1>();
                                        read = I<0>();
                                        blocknum = I<0>();
                                        if (headers.contains((S<"content-length">())))
                                        {
                                            size = (B<"int">())(headers[S<"Content-Length">()]);
                                        }
                                        if (reporthook.truthy())
                                        {
                                            reporthook(blocknum, bs, size);
                                        }
                                        while (([&]() -> py { block = fp.method("read", { bs }); return block; }()).truthy())
                                        {
                                            read = inplace(NEOS_PY_ADD, read, py{ block.size() });
                                            tfp.method("write", { block });
                                            blocknum = inplace(NEOS_PY_ADD, blocknum, I<1>());
                                            if (reporthook.truthy())
                                            {
                                                reporthook(blocknum, bs, size);
                                            }
                                        }
                                    }
                                    catch (raised const&)
                                    {
                                        pending112 = py_take_exception();
                                    }
                                    finally112:
                                    tfp.method("close");
                                    if (pending112)
                                        py_reraise(*pending112);
                                    if (flow112 == 1)
                                        { result106 = result112; flow106 = 1; goto finally106; }
                                }
                            }
                            catch (raised const&)
                            {
                                pending106 = py_take_exception();
                            }
                            finally106:
                            fp.method("close");
                            if (pending106)
                                py_reraise(*pending106);
                            if (flow106 == 1)
                                return result106;
                        }
                        // raise exception if actual size does not match content-length header
                        if (((size >= (I<0>())) && (read < size)))
                        {
                            py_raise((Mod<"urllib.error">().attr("ContentTooShortError"))((S<"retrieval incomplete: got only %i out of %i bytes">()) % py_tuple({ read, size }), result));
                        }
                        return result;
                    }) },
                { "_open_generic_http", w.method("URLopener", "_open_generic_http", { py_positional("connection_factory"), py_positional("url"), py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py connection_factory = a[1];
                        py url = a[2];
                        py data = a[3];
                        py auth;
                        py header;
                        py headers;
                        py host;
                        py http_conn;
                        py proxy_auth;
                        py proxy_passwd;
                        py realhost;
                        py response;
                        py rest;
                        py selector;
                        py urltype;
                        py user_passwd;
                        py value;
                        user_passwd = none();
                        proxy_passwd = none();
                        if (isinstance(url, B<"str">()))
                        {
                            auto const items114 = unpack((Mod<"urllib.parse">().attr("_splithost"))(url), 2);
                            host = items114[0];
                            selector = items114[1];
                            if (host.truthy())
                            {
                                auto const items115 = unpack((Mod<"urllib.parse">().attr("_splituser"))(host), 2);
                                user_passwd = items115[0];
                                host = items115[1];
                                host = (Mod<"urllib.parse">().attr("unquote"))(host);
                            }
                            realhost = host;
                        }
                        else
                        {
                            auto const items116 = unpack(url, 2);
                            host = items116[0];
                            selector = items116[1];
                            // check whether the proxy contains authorization information
                            auto const items117 = unpack((Mod<"urllib.parse">().attr("_splituser"))(host), 2);
                            proxy_passwd = items117[0];
                            host = items117[1];
                            // now we proceed with the url we want to obtain
                            auto const items118 = unpack((Mod<"urllib.parse">().attr("_splittype"))(selector), 2);
                            urltype = items118[0];
                            rest = items118[1];
                            url = rest;
                            user_passwd = none();
                            if (urltype.method("lower") != (S<"http">()))
                            {
                                realhost = none();
                            }
                            else
                            {
                                auto const items119 = unpack((Mod<"urllib.parse">().attr("_splithost"))(rest), 2);
                                realhost = items119[0];
                                rest = items119[1];
                                if (realhost.truthy())
                                {
                                    auto const items120 = unpack((Mod<"urllib.parse">().attr("_splituser"))(realhost), 2);
                                    user_passwd = items120[0];
                                    realhost = items120[1];
                                }
                                if (user_passwd.truthy())
                                {
                                    selector = (S<"%s://%s%s">()) % py_tuple({ urltype, realhost, rest });
                                }
                                if ((G<"urllib.request.proxy_bypass">())(realhost).truthy())
                                {
                                    host = realhost;
                                }
                            }
                        }
                        if (!host.truthy())
                        {
                            py_raise((B<"OSError">())(S<"http error">(), S<"no host given">()));
                        }
                        if (proxy_passwd.truthy())
                        {
                            proxy_passwd = (Mod<"urllib.parse">().attr("unquote"))(proxy_passwd);
                            proxy_auth = (Mod<"base64">().attr("b64encode"))(proxy_passwd.method("encode")).method("decode", { S<"ascii">() });
                        }
                        else
                        {
                            proxy_auth = none();
                        }
                        if (user_passwd.truthy())
                        {
                            user_passwd = (Mod<"urllib.parse">().attr("unquote"))(user_passwd);
                            auth = (Mod<"base64">().attr("b64encode"))(user_passwd.method("encode")).method("decode", { S<"ascii">() });
                        }
                        else
                        {
                            auth = none();
                        }
                        http_conn = connection_factory(host);
                        headers = py_dict();
                        if (proxy_auth.truthy())
                        {
                            headers.set_item(S<"Proxy-Authorization">(), (S<"Basic %s">()) % proxy_auth);
                        }
                        if (auth.truthy())
                        {
                            headers.set_item(S<"Authorization">(), (S<"Basic %s">()) % auth);
                        }
                        if (realhost.truthy())
                        {
                            headers.set_item(S<"Host">(), realhost);
                        }
                        // Add Connection:close as we don't support persistent connections yet.
                        // This helps in closing the socket and avoiding ResourceWarning
                        headers.set_item(S<"Connection">(), S<"close">());
                        for (py const& item : iterate{ self.attr("addheaders") })
                        {
                            auto const items122 = unpack(item, 2);
                            header = items122[0];
                            value = items122[1];
                            headers.set_item(header, value);
                        }
                        if (!data.is_none())
                        {
                            headers.set_item(S<"Content-Type">(), S<"application/x-www-form-urlencoded">());
                            http_conn.method("request", { S<"POST">(), selector, data, headers });
                        }
                        else
                        {
                            http_conn.method("request", { S<"GET">(), selector }, { { "headers", headers } });
                        }
                        try
                        {
                            response = http_conn.method("getresponse");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler123;
                            if (matches(handler123.exception, Mod<"http.client">().attr("BadStatusLine")))
                            {
                                // something went wrong with the HTTP status line
                                py_raise((Mod<"urllib.error">().attr("URLError"))(S<"http protocol error: bad status line">()));
                            }
                            else
                                py_reraise(handler123.exception);
                        }
                        // According to RFC 2616, "2xx" code indicates that the client's
                        // request was successfully received, understood, and accepted.
                        if (((I<200>()) <= response.attr("status")) && (response.attr("status") < (I<300>())))
                        {
                            return (Mod<"urllib.response">().attr("addinfourl"))(response, response.attr("msg"), (S<"http:">()) + url, response.attr("status"));
                        }
                        else
                        {
                            return self.method("http_error", { url, response.attr("fp"), response.attr("status"), response.attr("reason"), response.attr("msg"), data });
                        }
                        return none();
                    }) },
                { "open_http", w.method("URLopener", "open_http", { py_positional("url"), py_positional("data", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py data = a[2];
                        return self.method("_open_generic_http", { Mod<"http.client">().attr("HTTPConnection"), url, data });
                    }) },
                { "http_error", w.method("URLopener", "http_error", { py_positional("url"), py_positional("fp"), py_positional("errcode"), py_positional("errmsg"), py_positional("headers"), py_positional("data", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py fp = a[2];
                        py errcode = a[3];
                        py errmsg = a[4];
                        py headers = a[5];
                        py data = a[6];
                        py method;
                        py name;
                        py result;
                        // First check if there's a specific handler for this error
                        name = (S<"http_error_%d">()) % errcode;
                        if ((B<"hasattr">())(self, name).truthy())
                        {
                            method = (B<"getattr">())(self, name);
                            if (data.is_none())
                            {
                                result = method(url, fp, errcode, errmsg, headers);
                            }
                            else
                            {
                                result = method(url, fp, errcode, errmsg, headers, data);
                            }
                            if (result.truthy())
                            {
                                return result;
                            }
                        }
                        return self.method("http_error_default", { url, fp, errcode, errmsg, headers });
                    }) },
                { "http_error_default", w.method("URLopener", "http_error_default", { py_positional("url"), py_positional("fp"), py_positional("errcode"), py_positional("errmsg"), py_positional("headers") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py fp = a[2];
                        py errcode = a[3];
                        py errmsg = a[4];
                        py headers = a[5];
                        fp.method("close");
                        py_raise((Mod<"urllib.error">().attr("HTTPError"))(url, errcode, errmsg, headers, none()));
                    }) },
                { "open_file", w.method("URLopener", "open_file", { py_positional("url") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        if (!isinstance(url, B<"str">()))
                        {
                            py_raise((Mod<"urllib.error">().attr("URLError"))(S<"file error: proxy support for file protocol currently not implemented">()));
                        }
                        if (((url[B<"slice">()(none(), I<2>(), none())] == (S<"//">())) && (url[B<"slice">()(I<2>(), I<3>(), none())] != (S<"/">())) && (url[B<"slice">()(I<2>(), I<12>(), none())].method("lower") != (S<"localhost/">()))))
                        {
                            py_raise((B<"ValueError">())(S<"file:// scheme is supported only on localhost">()));
                        }
                        else
                        {
                            return self.method("open_local_file", { url });
                        }
                        return none();
                    }) },
                { "open_local_file", w.method("URLopener", "open_local_file", { py_positional("url") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py e;
                        py email;
                        py file;
                        py headers;
                        py host;
                        py localname;
                        py mimetypes;
                        py modified;
                        py mtype;
                        py port;
                        py size;
                        py stats;
                        py urlfile;
                        email = module_of("email");
                        mimetypes = module_of("mimetypes");
                        auto const items124 = unpack((Mod<"urllib.parse">().attr("_splithost"))(url), 2);
                        host = items124[0];
                        file = items124[1];
                        localname = (G<"urllib.request.url2pathname">())(file);
                        try
                        {
                            stats = (Mod<"os">().attr("stat"))(localname);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler125;
                            if (matches(handler125.exception, B<"OSError">()))
                            {
                                e = handler125.exception;
                                py_raise((Mod<"urllib.error">().attr("URLError"))(e.attr("strerror"), e.attr("filename")));
                            }
                            else
                                py_reraise(handler125.exception);
                        }
                        size = stats.attr("st_size");
                        modified = email.attr("utils").method("formatdate", { stats.attr("st_mtime") }, { { "usegmt", boolean(true) } });
                        mtype = mimetypes.method("guess_type", { url })[I<0>()];
                        headers = email.method("message_from_string", { (S<"Content-Type: %s\nContent-Length: %d\nLast-modified: %s\n">()) % py_tuple({ [&]() -> py { py const t = mtype; return t.truthy() ? t : S<"text/plain">(); }(), size, modified }) });
                        if (!host.truthy())
                        {
                            urlfile = file;
                            if (file[B<"slice">()(none(), I<1>(), none())] == (S<"/">()))
                            {
                                urlfile = (S<"file://">()) + file;
                            }
                            return (Mod<"urllib.response">().attr("addinfourl"))((B<"open">())(localname, S<"rb">()), headers, urlfile);
                        }
                        auto const items126 = unpack((Mod<"urllib.parse">().attr("_splitport"))(host), 2);
                        host = items126[0];
                        port = items126[1];
                        if (((!port.truthy()) && (py_tuple({ (G<"urllib.request.localhost">())() }) + (G<"urllib.request.thishost">())()).contains((Mod<"socket">().attr("gethostbyname"))(host))))
                        {
                            urlfile = file;
                            if (file[B<"slice">()(none(), I<1>(), none())] == (S<"/">()))
                            {
                                urlfile = (S<"file://">()) + file;
                            }
                            else if (file[B<"slice">()(none(), I<2>(), none())] == (S<"./">()))
                            {
                                py_raise((B<"ValueError">())((S<"local file url may start with / or file:. Unknown url of type: %s">()) % url));
                            }
                            return (Mod<"urllib.response">().attr("addinfourl"))((B<"open">())(localname, S<"rb">()), headers, urlfile);
                        }
                        py_raise((Mod<"urllib.error">().attr("URLError"))(S<"local file error: not on local host">()));
                    }) },
                { "open_ftp", w.method("URLopener", "open_ftp", { py_positional("url") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py attr;
                        py attrs;
                        py dirs;
                        py exp;
                        py file;
                        py fp;
                        py ftplib;
                        py headers;
                        py host;
                        py k;
                        py key;
                        py mimetypes;
                        py mtype;
                        py passwd;
                        py path;
                        py port;
                        py retrlen;
                        py type;
                        py user;
                        py v;
                        py value;
                        if (!isinstance(url, B<"str">()))
                        {
                            py_raise((Mod<"urllib.error">().attr("URLError"))(S<"ftp error: proxy support for ftp protocol currently not implemented">()));
                        }
                        mimetypes = module_of("mimetypes");
                        auto const items127 = unpack((Mod<"urllib.parse">().attr("_splithost"))(url), 2);
                        host = items127[0];
                        path = items127[1];
                        if (!host.truthy())
                        {
                            py_raise((Mod<"urllib.error">().attr("URLError"))(S<"ftp error: no host given">()));
                        }
                        auto const items128 = unpack((Mod<"urllib.parse">().attr("_splitport"))(host), 2);
                        host = items128[0];
                        port = items128[1];
                        auto const items129 = unpack((Mod<"urllib.parse">().attr("_splituser"))(host), 2);
                        user = items129[0];
                        host = items129[1];
                        if (user.truthy())
                        {
                            auto const items130 = unpack((Mod<"urllib.parse">().attr("_splitpasswd"))(user), 2);
                            user = items130[0];
                            passwd = items130[1];
                        }
                        else
                        {
                            passwd = none();
                        }
                        host = (Mod<"urllib.parse">().attr("unquote"))(host);
                        user = (Mod<"urllib.parse">().attr("unquote"))([&]() -> py { py const t = user; return t.truthy() ? t : S<"">(); }());
                        passwd = (Mod<"urllib.parse">().attr("unquote"))([&]() -> py { py const t = passwd; return t.truthy() ? t : S<"">(); }());
                        host = (Mod<"socket">().attr("gethostbyname"))(host);
                        if (!port.truthy())
                        {
                            ftplib = module_of("ftplib");
                            port = ftplib.attr("FTP_PORT");
                        }
                        else
                        {
                            port = (B<"int">())(port);
                        }
                        auto const items131 = unpack((Mod<"urllib.parse">().attr("_splitattr"))(path), 2);
                        path = items131[0];
                        attrs = items131[1];
                        path = (Mod<"urllib.parse">().attr("unquote"))(path);
                        dirs = path.method("split", { S<"/">() });
                        auto const items132 = unpack(py_tuple({ dirs[B<"slice">()(none(), I<-1>(), none())], dirs[I<-1>()] }), 2);
                        dirs = items132[0];
                        file = items132[1];
                        if ((dirs.truthy() && (!dirs[I<0>()].truthy())))
                        {
                            dirs = dirs[B<"slice">()(I<1>(), none(), none())];
                        }
                        if ((dirs.truthy() && (!dirs[I<0>()].truthy())))
                        {
                            dirs.set_item(I<0>(), S<"/">());
                        }
                        key = py_tuple({ user, host, port, (S<"/">()).method("join", { dirs }) });
                        // XXX thread unsafe!
                        if (py{ self.attr("ftpcache").size() } > (G<"urllib.request.MAXFTPCACHE">()))
                        {
                            // Prune the cache, rather arbitrarily
                            for (py const& item : iterate{ (B<"list">())(self.attr("ftpcache")) })
                            {
                                k = item;
                                if (k != key)
                                {
                                    v = self.attr("ftpcache")[k];
                                    del_item(self.attr("ftpcache"), k);
                                    v.method("close");
                                }
                            }
                        }
                        try
                        {
                            if (!self.attr("ftpcache").contains(key))
                            {
                                self.attr("ftpcache").set_item(key, (G<"urllib.request.ftpwrapper">())(user, passwd, host, port, dirs));
                            }
                            if (!file.truthy())
                            {
                                type = S<"D">();
                            }
                            else
                            {
                                type = S<"I">();
                            }
                            for (py const& item : iterate{ attrs })
                            {
                                attr = item;
                                auto const items136 = unpack((Mod<"urllib.parse">().attr("_splitvalue"))(attr), 2);
                                attr = items136[0];
                                value = items136[1];
                                if (((attr.method("lower") == (S<"type">())) && py_tuple({ S<"a">(), S<"A">(), S<"i">(), S<"I">(), S<"d">(), S<"D">() }).contains(value)))
                                {
                                    type = value.method("upper");
                                }
                            }
                            auto const items137 = unpack(self.attr("ftpcache")[key].method("retrfile", { file, type }), 2);
                            fp = items137[0];
                            retrlen = items137[1];
                            mtype = mimetypes.method("guess_type", { (S<"ftp:">()) + url })[I<0>()];
                            headers = S<"">();
                            if (mtype.truthy())
                            {
                                headers = inplace(NEOS_PY_ADD, headers, (S<"Content-Type: %s\n">()) % mtype);
                            }
                            if (((!retrlen.is_none()) && (retrlen >= (I<0>()))))
                            {
                                headers = inplace(NEOS_PY_ADD, headers, (S<"Content-Length: %d\n">()) % retrlen);
                            }
                            headers = (Mod<"email">().attr("message_from_string"))(headers);
                            return (Mod<"urllib.response">().attr("addinfourl"))(fp, headers, (S<"ftp:">()) + url);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler134;
                            if (matches(handler134.exception, (G<"urllib.request.ftperrors">())()))
                            {
                                exp = handler134.exception;
                                fn_raise_from((Mod<"urllib.error">().attr("URLError"))(S(std::string{ "ftp error: " } + exp.str())).get(), exp.get());
                                throw raised{};
                            }
                            else
                                py_reraise(handler134.exception);
                        }
                        return none();
                    }) },
                { "open_data", w.method("URLopener", "open_data", { py_positional("url"), py_positional("data", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py data = a[2];
                        py encoding;
                        py f;
                        py headers;
                        py msg;
                        py semi;
                        py type;
                        if (!isinstance(url, B<"str">()))
                        {
                            py_raise((Mod<"urllib.error">().attr("URLError"))(S<"data error: proxy support for data protocol currently not implemented">()));
                        }
                        // ignore POSTed data
                        // 
                        // syntax of data URLs:
                        // dataurl   := "data:" [ mediatype ] [ ";base64" ] "," data
                        // mediatype := [ type "/" subtype ] *( ";" parameter )
                        // data      := *urlchar
                        // parameter := attribute "=" value
                        try
                        {
                            auto const items139 = unpack(url.method("split", { S<",">(), I<1>() }), 2);
                            type = items139[0];
                            data = items139[1];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler138;
                            if (matches(handler138.exception, B<"ValueError">()))
                            {
                                py_raise((B<"OSError">())(S<"data error">(), S<"bad data URL">()));
                            }
                            else
                                py_reraise(handler138.exception);
                        }
                        if (!type.truthy())
                        {
                            type = S<"text/plain;charset=US-ASCII">();
                        }
                        semi = type.method("rfind", { S<";">() });
                        if (((semi >= (I<0>())) && (!type[B<"slice">()(semi, none(), none())].contains((S<"=">())))))
                        {
                            encoding = type[B<"slice">()(semi + (I<1>()), none(), none())];
                            type = type[B<"slice">()(none(), semi, none())];
                        }
                        else
                        {
                            encoding = S<"">();
                        }
                        msg = py_list();
                        msg.method("append", { (S<"Date: %s">()) % (Mod<"time">().attr("strftime"))(S<"%a, %d %b %Y %H:%M:%S GMT">(), (Mod<"time">().attr("gmtime"))((Mod<"time">().attr("time"))())) });
                        msg.method("append", { (S<"Content-type: %s">()) % type });
                        if (encoding == (S<"base64">()))
                        {
                            // XXX is this encoding/decoding ok?
                            data = (Mod<"base64">().attr("decodebytes"))(data.method("encode", { S<"ascii">() })).method("decode", { S<"latin-1">() });
                        }
                        else
                        {
                            data = (Mod<"urllib.parse">().attr("unquote"))(data);
                        }
                        msg.method("append", { (S<"Content-Length: %d">()) % py{ data.size() } });
                        msg.method("append", { S<"">() });
                        msg.method("append", { data });
                        msg = (S<"\n">()).method("join", { msg });
                        headers = (Mod<"email">().attr("message_from_string"))(msg);
                        f = (Mod<"io">().attr("StringIO"))(msg);
                        // f.fileno = None     # needed for addinfourl
                        return (Mod<"urllib.response">().attr("addinfourl"))(f, headers, url);
                    }) }, }));

            define("urllib.request", "FancyURLopener", w.make_class("FancyURLopener", { G<"urllib.request.URLopener">() }, {
                { "__init__", w.method("FancyURLopener", "__init__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py kwargs = a[2];
                        call_ex((G<"urllib.request.URLopener">()).attr("__init__"), { self }, args, kwargs);
                        self.set_attr("auth_cache", py_dict());
                        self.set_attr("tries", I<0>());
                        self.set_attr("maxtries", I<10>());
                        return none();
                    }, true, true) },
                { "http_error_default", w.method("FancyURLopener", "http_error_default", { py_positional("url"), py_positional("fp"), py_positional("errcode"), py_positional("errmsg"), py_positional("headers") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py fp = a[2];
                        py errcode = a[3];
                        py errmsg = a[4];
                        py headers = a[5];
                        return (Mod<"urllib.response">().attr("addinfourl"))(fp, headers, (S<"http:">()) + url, errcode);
                    }) },
                { "http_error_302", w.method("FancyURLopener", "http_error_302", { py_positional("url"), py_positional("fp"), py_positional("errcode"), py_positional("errmsg"), py_positional("headers"), py_positional("data", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py fp = a[2];
                        py errcode = a[3];
                        py errmsg = a[4];
                        py headers = a[5];
                        py data = a[6];
                        py meth;
                        py result;
                        {
                            py const target140 = self;
                            target140.set_attr("tries", inplace(NEOS_PY_ADD, target140.attr("tries"), I<1>()));
                        }
                        {
                            std::optional<py> pending141;
                            [[maybe_unused]] int flow141 = 0;
                            [[maybe_unused]] py result141;
                            try
                            {
                                if ((self.attr("maxtries").truthy() && (self.attr("tries") >= self.attr("maxtries"))))
                                {
                                    if (has_attr(self, "http_error_500"))
                                    {
                                        meth = self.attr("http_error_500");
                                    }
                                    else
                                    {
                                        meth = self.attr("http_error_default");
                                    }
                                    result141 = meth(url, fp, I<500>(), S<"Internal Server Error: Redirect Recursion">(), headers);
                                    flow141 = 1;
                                    goto finally141;
                                }
                                result = self.method("redirect_internal", { url, fp, errcode, errmsg, headers, data });
                                result141 = result;
                                flow141 = 1;
                                goto finally141;
                            }
                            catch (raised const&)
                            {
                                pending141 = py_take_exception();
                            }
                            finally141:
                            self.set_attr("tries", I<0>());
                            if (pending141)
                                py_reraise(*pending141);
                            if (flow141 == 1)
                                return result141;
                        }
                        return none();
                    }) },
                { "redirect_internal", w.method("FancyURLopener", "redirect_internal", { py_positional("url"), py_positional("fp"), py_positional("errcode"), py_positional("errmsg"), py_positional("headers"), py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py fp = a[2];
                        py errcode = a[3];
                        py errmsg = a[4];
                        py headers = a[5];
                        py data = a[6];
                        py newurl;
                        py urlparts;
                        if (headers.contains((S<"location">())))
                        {
                            newurl = headers[S<"location">()];
                        }
                        else if (headers.contains((S<"uri">())))
                        {
                            newurl = headers[S<"uri">()];
                        }
                        else
                        {
                            return none();
                        }
                        fp.method("close");
                        // In case the server sent a relative URL, join with original:
                        newurl = (Mod<"urllib.parse">().attr("urljoin"))((self.attr("type") + (S<":">())) + url, newurl);
                        urlparts = (Mod<"urllib.parse">().attr("urlparse"))(newurl);
                        // For security reasons, we don't allow redirection to anything other
                        // than http, https and ftp.
                        // We are using newer HTTPError with older redirect_internal method
                        // This older method will get deprecated in 3.3
                        if (!py_tuple({ S<"http">(), S<"https">(), S<"ftp">(), S<"">() }).contains(urlparts.attr("scheme")))
                        {
                            py_raise((Mod<"urllib.error">().attr("HTTPError"))(newurl, errcode, errmsg + ((S<" Redirection to url '%s' is not allowed.">()) % newurl), headers, fp));
                        }
                        return self.method("open", { newurl });
                    }) },
                { "http_error_301", w.method("FancyURLopener", "http_error_301", { py_positional("url"), py_positional("fp"), py_positional("errcode"), py_positional("errmsg"), py_positional("headers"), py_positional("data", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py fp = a[2];
                        py errcode = a[3];
                        py errmsg = a[4];
                        py headers = a[5];
                        py data = a[6];
                        return self.method("http_error_302", { url, fp, errcode, errmsg, headers, data });
                    }) },
                { "http_error_303", w.method("FancyURLopener", "http_error_303", { py_positional("url"), py_positional("fp"), py_positional("errcode"), py_positional("errmsg"), py_positional("headers"), py_positional("data", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py fp = a[2];
                        py errcode = a[3];
                        py errmsg = a[4];
                        py headers = a[5];
                        py data = a[6];
                        return self.method("http_error_302", { url, fp, errcode, errmsg, headers, data });
                    }) },
                { "http_error_307", w.method("FancyURLopener", "http_error_307", { py_positional("url"), py_positional("fp"), py_positional("errcode"), py_positional("errmsg"), py_positional("headers"), py_positional("data", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py fp = a[2];
                        py errcode = a[3];
                        py errmsg = a[4];
                        py headers = a[5];
                        py data = a[6];
                        if (data.is_none())
                        {
                            return self.method("http_error_302", { url, fp, errcode, errmsg, headers, data });
                        }
                        else
                        {
                            return self.method("http_error_default", { url, fp, errcode, errmsg, headers });
                        }
                        return none();
                    }) },
                { "http_error_308", w.method("FancyURLopener", "http_error_308", { py_positional("url"), py_positional("fp"), py_positional("errcode"), py_positional("errmsg"), py_positional("headers"), py_positional("data", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py fp = a[2];
                        py errcode = a[3];
                        py errmsg = a[4];
                        py headers = a[5];
                        py data = a[6];
                        if (data.is_none())
                        {
                            return self.method("http_error_301", { url, fp, errcode, errmsg, headers, data });
                        }
                        else
                        {
                            return self.method("http_error_default", { url, fp, errcode, errmsg, headers });
                        }
                        return none();
                    }) },
                { "http_error_401", w.method("FancyURLopener", "http_error_401", { py_positional("url"), py_positional("fp"), py_positional("errcode"), py_positional("errmsg"), py_positional("headers"), py_positional("data", none()), py_positional("retry", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py fp = a[2];
                        py errcode = a[3];
                        py errmsg = a[4];
                        py headers = a[5];
                        py data = a[6];
                        py retry = a[7];
                        py match;
                        py name;
                        py realm;
                        py scheme;
                        py stuff;
                        if (!headers.contains((S<"www-authenticate">())))
                        {
                            (G<"urllib.request.URLopener">()).method("http_error_default", { self, url, fp, errcode, errmsg, headers });
                        }
                        stuff = headers[S<"www-authenticate">()];
                        match = (Mod<"re">().attr("match"))(S<"[ \t]*([^ \t]+)[ \t]+realm=\"([^\"]*)\"">(), stuff);
                        if (!match.truthy())
                        {
                            (G<"urllib.request.URLopener">()).method("http_error_default", { self, url, fp, errcode, errmsg, headers });
                        }
                        auto const items142 = unpack(match.method("groups"), 2);
                        scheme = items142[0];
                        realm = items142[1];
                        if (scheme.method("lower") != (S<"basic">()))
                        {
                            (G<"urllib.request.URLopener">()).method("http_error_default", { self, url, fp, errcode, errmsg, headers });
                        }
                        if (!retry.truthy())
                        {
                            (G<"urllib.request.URLopener">()).method("http_error_default", { self, url, fp, errcode, errmsg, headers });
                        }
                        name = ((S<"retry_">()) + self.attr("type")) + (S<"_basic_auth">());
                        if (data.is_none())
                        {
                            return (B<"getattr">())(self, name)(url, realm);
                        }
                        else
                        {
                            return (B<"getattr">())(self, name)(url, realm, data);
                        }
                        return none();
                    }) },
                { "http_error_407", w.method("FancyURLopener", "http_error_407", { py_positional("url"), py_positional("fp"), py_positional("errcode"), py_positional("errmsg"), py_positional("headers"), py_positional("data", none()), py_positional("retry", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py fp = a[2];
                        py errcode = a[3];
                        py errmsg = a[4];
                        py headers = a[5];
                        py data = a[6];
                        py retry = a[7];
                        py match;
                        py name;
                        py realm;
                        py scheme;
                        py stuff;
                        if (!headers.contains((S<"proxy-authenticate">())))
                        {
                            (G<"urllib.request.URLopener">()).method("http_error_default", { self, url, fp, errcode, errmsg, headers });
                        }
                        stuff = headers[S<"proxy-authenticate">()];
                        match = (Mod<"re">().attr("match"))(S<"[ \t]*([^ \t]+)[ \t]+realm=\"([^\"]*)\"">(), stuff);
                        if (!match.truthy())
                        {
                            (G<"urllib.request.URLopener">()).method("http_error_default", { self, url, fp, errcode, errmsg, headers });
                        }
                        auto const items143 = unpack(match.method("groups"), 2);
                        scheme = items143[0];
                        realm = items143[1];
                        if (scheme.method("lower") != (S<"basic">()))
                        {
                            (G<"urllib.request.URLopener">()).method("http_error_default", { self, url, fp, errcode, errmsg, headers });
                        }
                        if (!retry.truthy())
                        {
                            (G<"urllib.request.URLopener">()).method("http_error_default", { self, url, fp, errcode, errmsg, headers });
                        }
                        name = ((S<"retry_proxy_">()) + self.attr("type")) + (S<"_basic_auth">());
                        if (data.is_none())
                        {
                            return (B<"getattr">())(self, name)(url, realm);
                        }
                        else
                        {
                            return (B<"getattr">())(self, name)(url, realm, data);
                        }
                        return none();
                    }) },
                { "retry_proxy_http_basic_auth", w.method("FancyURLopener", "retry_proxy_http_basic_auth", { py_positional("url"), py_positional("realm"), py_positional("data", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py realm = a[2];
                        py data = a[3];
                        py host;
                        py i;
                        py newurl;
                        py passwd;
                        py proxy;
                        py proxyhost;
                        py proxyselector;
                        py selector;
                        py urltype;
                        py user;
                        auto const items144 = unpack((Mod<"urllib.parse">().attr("_splithost"))(url), 2);
                        host = items144[0];
                        selector = items144[1];
                        newurl = ((S<"http://">()) + host) + selector;
                        proxy = self.attr("proxies")[S<"http">()];
                        auto const items145 = unpack((Mod<"urllib.parse">().attr("_splittype"))(proxy), 2);
                        urltype = items145[0];
                        proxyhost = items145[1];
                        auto const items146 = unpack((Mod<"urllib.parse">().attr("_splithost"))(proxyhost), 2);
                        proxyhost = items146[0];
                        proxyselector = items146[1];
                        i = proxyhost.method("find", { S<"@">() }) + (I<1>());
                        proxyhost = proxyhost[B<"slice">()(i, none(), none())];
                        auto const items147 = unpack(self.method("get_user_passwd", { proxyhost, realm, i }), 2);
                        user = items147[0];
                        passwd = items147[1];
                        if (!([&]() -> py { py const t = user; return t.truthy() ? t : passwd; }()).truthy())
                        {
                            return none();
                        }
                        proxyhost = (S<"%s:%s@%s">()) % py_tuple({ (Mod<"urllib.parse">().attr("quote")).call({ user }, { { "safe", S<"">() } }), (Mod<"urllib.parse">().attr("quote")).call({ passwd }, { { "safe", S<"">() } }), proxyhost });
                        self.attr("proxies").set_item(S<"http">(), ((S<"http://">()) + proxyhost) + proxyselector);
                        if (data.is_none())
                        {
                            return self.method("open", { newurl });
                        }
                        else
                        {
                            return self.method("open", { newurl, data });
                        }
                        return none();
                    }) },
                { "retry_proxy_https_basic_auth", w.method("FancyURLopener", "retry_proxy_https_basic_auth", { py_positional("url"), py_positional("realm"), py_positional("data", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py realm = a[2];
                        py data = a[3];
                        py host;
                        py i;
                        py newurl;
                        py passwd;
                        py proxy;
                        py proxyhost;
                        py proxyselector;
                        py selector;
                        py urltype;
                        py user;
                        auto const items148 = unpack((Mod<"urllib.parse">().attr("_splithost"))(url), 2);
                        host = items148[0];
                        selector = items148[1];
                        newurl = ((S<"https://">()) + host) + selector;
                        proxy = self.attr("proxies")[S<"https">()];
                        auto const items149 = unpack((Mod<"urllib.parse">().attr("_splittype"))(proxy), 2);
                        urltype = items149[0];
                        proxyhost = items149[1];
                        auto const items150 = unpack((Mod<"urllib.parse">().attr("_splithost"))(proxyhost), 2);
                        proxyhost = items150[0];
                        proxyselector = items150[1];
                        i = proxyhost.method("find", { S<"@">() }) + (I<1>());
                        proxyhost = proxyhost[B<"slice">()(i, none(), none())];
                        auto const items151 = unpack(self.method("get_user_passwd", { proxyhost, realm, i }), 2);
                        user = items151[0];
                        passwd = items151[1];
                        if (!([&]() -> py { py const t = user; return t.truthy() ? t : passwd; }()).truthy())
                        {
                            return none();
                        }
                        proxyhost = (S<"%s:%s@%s">()) % py_tuple({ (Mod<"urllib.parse">().attr("quote")).call({ user }, { { "safe", S<"">() } }), (Mod<"urllib.parse">().attr("quote")).call({ passwd }, { { "safe", S<"">() } }), proxyhost });
                        self.attr("proxies").set_item(S<"https">(), ((S<"https://">()) + proxyhost) + proxyselector);
                        if (data.is_none())
                        {
                            return self.method("open", { newurl });
                        }
                        else
                        {
                            return self.method("open", { newurl, data });
                        }
                        return none();
                    }) },
                { "retry_http_basic_auth", w.method("FancyURLopener", "retry_http_basic_auth", { py_positional("url"), py_positional("realm"), py_positional("data", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py realm = a[2];
                        py data = a[3];
                        py host;
                        py i;
                        py newurl;
                        py passwd;
                        py selector;
                        py user;
                        auto const items152 = unpack((Mod<"urllib.parse">().attr("_splithost"))(url), 2);
                        host = items152[0];
                        selector = items152[1];
                        i = host.method("find", { S<"@">() }) + (I<1>());
                        host = host[B<"slice">()(i, none(), none())];
                        auto const items153 = unpack(self.method("get_user_passwd", { host, realm, i }), 2);
                        user = items153[0];
                        passwd = items153[1];
                        if (!([&]() -> py { py const t = user; return t.truthy() ? t : passwd; }()).truthy())
                        {
                            return none();
                        }
                        host = (S<"%s:%s@%s">()) % py_tuple({ (Mod<"urllib.parse">().attr("quote")).call({ user }, { { "safe", S<"">() } }), (Mod<"urllib.parse">().attr("quote")).call({ passwd }, { { "safe", S<"">() } }), host });
                        newurl = ((S<"http://">()) + host) + selector;
                        if (data.is_none())
                        {
                            return self.method("open", { newurl });
                        }
                        else
                        {
                            return self.method("open", { newurl, data });
                        }
                        return none();
                    }) },
                { "retry_https_basic_auth", w.method("FancyURLopener", "retry_https_basic_auth", { py_positional("url"), py_positional("realm"), py_positional("data", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py url = a[1];
                        py realm = a[2];
                        py data = a[3];
                        py host;
                        py i;
                        py newurl;
                        py passwd;
                        py selector;
                        py user;
                        auto const items154 = unpack((Mod<"urllib.parse">().attr("_splithost"))(url), 2);
                        host = items154[0];
                        selector = items154[1];
                        i = host.method("find", { S<"@">() }) + (I<1>());
                        host = host[B<"slice">()(i, none(), none())];
                        auto const items155 = unpack(self.method("get_user_passwd", { host, realm, i }), 2);
                        user = items155[0];
                        passwd = items155[1];
                        if (!([&]() -> py { py const t = user; return t.truthy() ? t : passwd; }()).truthy())
                        {
                            return none();
                        }
                        host = (S<"%s:%s@%s">()) % py_tuple({ (Mod<"urllib.parse">().attr("quote")).call({ user }, { { "safe", S<"">() } }), (Mod<"urllib.parse">().attr("quote")).call({ passwd }, { { "safe", S<"">() } }), host });
                        newurl = ((S<"https://">()) + host) + selector;
                        if (data.is_none())
                        {
                            return self.method("open", { newurl });
                        }
                        else
                        {
                            return self.method("open", { newurl, data });
                        }
                        return none();
                    }) },
                { "get_user_passwd", w.method("FancyURLopener", "get_user_passwd", { py_positional("host"), py_positional("realm"), py_positional("clear_cache", I<0>()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py realm = a[2];
                        py clear_cache = a[3];
                        py key;
                        py passwd;
                        py user;
                        key = (realm + (S<"@">())) + host.method("lower");
                        if (self.attr("auth_cache").contains(key))
                        {
                            if (clear_cache.truthy())
                            {
                                del_item(self.attr("auth_cache"), key);
                            }
                            else
                            {
                                return self.attr("auth_cache")[key];
                            }
                        }
                        auto const items156 = unpack(self.method("prompt_user_passwd", { host, realm }), 2);
                        user = items156[0];
                        passwd = items156[1];
                        if ((user.truthy() || passwd.truthy()))
                        {
                            self.attr("auth_cache").set_item(key, py_tuple({ user, passwd }));
                        }
                        return py_tuple({ user, passwd });
                    }) },
                { "prompt_user_passwd", w.method("FancyURLopener", "prompt_user_passwd", { py_positional("host"), py_positional("realm") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py realm = a[2];
                        py getpass;
                        py passwd;
                        py user;
                        getpass = module_of("getpass");
                        try
                        {
                            user = (B<"input">())((S<"Enter username for %s at %s: ">()) % py_tuple({ realm, host }));
                            passwd = getpass.method("getpass", { (S<"Enter password for %s in %s at %s: ">()) % py_tuple({ user, realm, host }) });
                            return py_tuple({ user, passwd });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler157;
                            if (matches(handler157.exception, B<"KeyboardInterrupt">()))
                            {
                                (B<"print">())();
                                return py_tuple({ none(), none() });
                            }
                            else
                                py_reraise(handler157.exception);
                        }
                        return none();
                    }) }, }));

            // Utility functions
            define("urllib.request", "_localhost", none());
            define("urllib.request", "localhost", w.function("localhost", "localhost", {  }, [](arguments a) -> py
                {
                    if ((G<"urllib.request._localhost">()).is_none())
                    {
                        set_global("urllib.request", "_localhost", (Mod<"socket">().attr("gethostbyname"))(S<"localhost">()));
                    }
                    return G<"urllib.request._localhost">();
                }));

            define("urllib.request", "_thishost", none());
            define("urllib.request", "thishost", w.function("thishost", "thishost", {  }, [](arguments a) -> py
                {
                    if ((G<"urllib.request._thishost">()).is_none())
                    {
                        try
                        {
                            set_global("urllib.request", "_thishost", (B<"tuple">())((Mod<"socket">().attr("gethostbyname_ex"))((Mod<"socket">().attr("gethostname"))())[I<2>()]));
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler158;
                            if (matches(handler158.exception, Mod<"socket">().attr("gaierror")))
                            {
                                set_global("urllib.request", "_thishost", (B<"tuple">())((Mod<"socket">().attr("gethostbyname_ex"))(S<"localhost">())[I<2>()]));
                            }
                            else
                                py_reraise(handler158.exception);
                        }
                    }
                    return G<"urllib.request._thishost">();
                }));

            define("urllib.request", "_ftperrors", none());
            define("urllib.request", "ftperrors", w.function("ftperrors", "ftperrors", {  }, [](arguments a) -> py
                {
                    py ftplib;
                    if ((G<"urllib.request._ftperrors">()).is_none())
                    {
                        ftplib = module_of("ftplib");
                        set_global("urllib.request", "_ftperrors", ftplib.attr("all_errors"));
                    }
                    return G<"urllib.request._ftperrors">();
                }));

            define("urllib.request", "_noheaders", none());
            define("urllib.request", "noheaders", w.function("noheaders", "noheaders", {  }, [](arguments a) -> py
                {
                    if ((G<"urllib.request._noheaders">()).is_none())
                    {
                        set_global("urllib.request", "_noheaders", (Mod<"email">().attr("message_from_string"))(S<"">()));
                    }
                    return G<"urllib.request._noheaders">();
                }));

            // Utility classes
            define("urllib.request", "ftpwrapper", w.make_class("ftpwrapper", {  }, {
                { "__init__", w.method("ftpwrapper", "__init__", { py_positional("user"), py_positional("passwd"), py_positional("host"), py_positional("port"), py_positional("dirs"), py_positional("timeout", none()), py_positional("persistent", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py user = a[1];
                        py passwd = a[2];
                        py host = a[3];
                        py port = a[4];
                        py dirs = a[5];
                        py timeout = a[6];
                        py persistent = a[7];
                        self.set_attr("user", user);
                        self.set_attr("passwd", passwd);
                        self.set_attr("host", host);
                        self.set_attr("port", port);
                        self.set_attr("dirs", dirs);
                        self.set_attr("timeout", timeout);
                        self.set_attr("refcount", I<0>());
                        self.set_attr("keepalive", persistent);
                        try
                        {
                            self.method("init");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler159;
                            {
                                self.method("close");
                                py_reraise(handler159.exception);
                            }
                        }
                        return none();
                    }) },
                { "init", w.method("ftpwrapper", "init", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _target;
                        py ftplib;
                        ftplib = module_of("ftplib");
                        self.set_attr("busy", I<0>());
                        self.set_attr("ftp", ftplib.method("FTP"));
                        self.attr("ftp").method("connect", { self.attr("host"), self.attr("port"), self.attr("timeout") });
                        self.attr("ftp").method("login", { self.attr("user"), self.attr("passwd") });
                        _target = (S<"/">()).method("join", { self.attr("dirs") });
                        self.attr("ftp").method("cwd", { _target });
                        return none();
                    }) },
                { "retrfile", w.method("ftpwrapper", "retrfile", { py_positional("file"), py_positional("type") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        py type = a[2];
                        py cmd;
                        py conn;
                        py ftplib;
                        py ftpobj;
                        py isdir;
                        py pwd;
                        py reason;
                        py retrlen;
                        ftplib = module_of("ftplib");
                        self.method("endtransfer");
                        if (py_tuple({ S<"d">(), S<"D">() }).contains(type))
                        {
                            cmd = S<"TYPE A">();
                            isdir = I<1>();
                        }
                        else
                        {
                            cmd = (S<"TYPE ">()) + type;
                            isdir = I<0>();
                        }
                        try
                        {
                            self.attr("ftp").method("voidcmd", { cmd });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler160;
                            if (matches(handler160.exception, ftplib.attr("all_errors")))
                            {
                                self.method("init");
                                self.attr("ftp").method("voidcmd", { cmd });
                            }
                            else
                                py_reraise(handler160.exception);
                        }
                        conn = none();
                        if ((file.truthy() && (!isdir.truthy())))
                        {
                            // Try to retrieve as a file
                            try
                            {
                                cmd = (S<"RETR ">()) + file;
                                auto const items162 = unpack(self.attr("ftp").method("ntransfercmd", { cmd }), 2);
                                conn = items162[0];
                                retrlen = items162[1];
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler161;
                                if (matches(handler161.exception, ftplib.attr("error_perm")))
                                {
                                    reason = handler161.exception;
                                    if ((B<"str">())(reason)[B<"slice">()(none(), I<3>(), none())] != (S<"550">()))
                                    {
                                        fn_raise_from((Mod<"urllib.error">().attr("URLError"))(S(std::string{ "ftp error: " } + reason.str())).get(), reason.get());
                                        throw raised{};
                                    }
                                }
                                else
                                    py_reraise(handler161.exception);
                            }
                        }
                        if (!conn.truthy())
                        {
                            // Set transfer mode to ASCII!
                            self.attr("ftp").method("voidcmd", { S<"TYPE A">() });
                            // Try a directory listing. Verify that directory exists.
                            if (file.truthy())
                            {
                                pwd = self.attr("ftp").method("pwd");
                                {
                                    std::optional<py> pending163;
                                    [[maybe_unused]] int flow163 = 0;
                                    [[maybe_unused]] py result163;
                                    try
                                    {
                                        try
                                        {
                                            self.attr("ftp").method("cwd", { file });
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler164;
                                            if (matches(handler164.exception, ftplib.attr("error_perm")))
                                            {
                                                reason = handler164.exception;
                                                fn_raise_from((Mod<"urllib.error">().attr("URLError"))((S<"ftp error: %r">()) % reason).get(), reason.get());
                                                throw raised{};
                                            }
                                            else
                                                py_reraise(handler164.exception);
                                        }
                                    }
                                    catch (raised const&)
                                    {
                                        pending163 = py_take_exception();
                                    }
                                    finally163:
                                    self.attr("ftp").method("cwd", { pwd });
                                    if (pending163)
                                        py_reraise(*pending163);
                                    if (flow163 == 1)
                                        return result163;
                                }
                                cmd = (S<"LIST ">()) + file;
                            }
                            else
                            {
                                cmd = S<"LIST">();
                            }
                            auto const items165 = unpack(self.attr("ftp").method("ntransfercmd", { cmd }), 2);
                            conn = items165[0];
                            retrlen = items165[1];
                        }
                        self.set_attr("busy", I<1>());
                        ftpobj = (Mod<"urllib.response">().attr("addclosehook"))(conn.method("makefile", { S<"rb">() }), self.attr("file_close"));
                        {
                            py const target166 = self;
                            target166.set_attr("refcount", inplace(NEOS_PY_ADD, target166.attr("refcount"), I<1>()));
                        }
                        conn.method("close");
                        // Pass back both a suitably decorated object and a retrieval length
                        return py_tuple({ ftpobj, retrlen });
                    }) },
                { "endtransfer", w.method("ftpwrapper", "endtransfer", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (!self.attr("busy").truthy())
                        {
                            return none();
                        }
                        self.set_attr("busy", I<0>());
                        try
                        {
                            self.attr("ftp").method("voidresp");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler167;
                            if (matches(handler167.exception, (G<"urllib.request.ftperrors">())()))
                            {
                            }
                            else
                                py_reraise(handler167.exception);
                        }
                        return none();
                    }) },
                { "close", w.method("ftpwrapper", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("keepalive", boolean(false));
                        if (self.attr("refcount") <= (I<0>()))
                        {
                            self.method("real_close");
                        }
                        return none();
                    }) },
                { "file_close", w.method("ftpwrapper", "file_close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("endtransfer");
                        {
                            py const target168 = self;
                            target168.set_attr("refcount", inplace(NEOS_PY_SUBTRACT, target168.attr("refcount"), I<1>()));
                        }
                        if (((self.attr("refcount") <= (I<0>())) && (!self.attr("keepalive").truthy())))
                        {
                            self.method("real_close");
                        }
                        return none();
                    }) },
                { "real_close", w.method("ftpwrapper", "real_close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.method("endtransfer");
                        try
                        {
                            self.attr("ftp").method("close");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler169;
                            if (matches(handler169.exception, (G<"urllib.request.ftperrors">())()))
                            {
                            }
                            else
                                py_reraise(handler169.exception);
                        }
                        return none();
                    }) }, }));

            // Proxy handling
            define("urllib.request", "getproxies_environment", w.function("getproxies_environment", "getproxies_environment", {  }, [](arguments a) -> py
                {
                    py environment;
                    py name;
                    py proxies;
                    py proxy_name;
                    py value;
                    // in order to prefer lowercase variables, process environment in
                    // two passes: first matches any, second pass matches lowercase only
                    // select only environment variables which end in (after making lowercase) _proxy
                    proxies = py_dict();
                    environment = py_list();
                    for (py const& item : iterate{ Mod<"os">().attr("environ") })
                    {
                        name = item;
                        // fast screen underscore position before more expensive case-folding
                        if (((py{ name.size() } > (I<5>())) && (name[I<-6>()] == (S<"_">())) && (name[B<"slice">()(I<-5>(), none(), none())].method("lower") == (S<"proxy">()))))
                        {
                            value = (Mod<"os">().attr("environ"))[name];
                            proxy_name = name[B<"slice">()(none(), I<-6>(), none())].method("lower");
                            environment.method("append", { py_tuple({ name, value, proxy_name }) });
                            if (value.truthy())
                            {
                                proxies.set_item(proxy_name, value);
                            }
                        }
                    }
                    // CVE-2016-1000110 - If we are running as CGI script, forget HTTP_PROXY
                    // (non-all-lowercase) as it may be set from the web server by a "Proxy:"
                    // header from the client
                    // If "proxy" is lowercase, it will still be used thanks to the next block
                    if ((Mod<"os">().attr("environ")).contains((S<"REQUEST_METHOD">())))
                    {
                        proxies.method("pop", { S<"http">(), none() });
                    }
                    for (py const& item : iterate{ environment })
                    {
                        auto const items172 = unpack(item, 3);
                        name = items172[0];
                        value = items172[1];
                        proxy_name = items172[2];
                        // not case-folded, checking here for lower-case env vars only
                        if (name[B<"slice">()(I<-6>(), none(), none())] == (S<"_proxy">()))
                        {
                            if (value.truthy())
                            {
                                proxies.set_item(proxy_name, value);
                            }
                            else
                            {
                                proxies.method("pop", { proxy_name, none() });
                            }
                        }
                    }
                    return proxies;
                }));

            define("urllib.request", "proxy_bypass_environment", w.function("proxy_bypass_environment", "proxy_bypass_environment", { py_positional("host"), py_positional("proxies", none()) }, [](arguments a) -> py
                {
                    py host = a[0];
                    py proxies = a[1];
                    py hostonly;
                    py name;
                    py no_proxy;
                    py port;
                    if (proxies.is_none())
                    {
                        proxies = (G<"urllib.request.getproxies_environment">())();
                    }
                    // don't bypass, if no_proxy isn't specified
                    try
                    {
                        no_proxy = proxies[S<"no">()];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler173;
                        if (matches(handler173.exception, B<"KeyError">()))
                        {
                            return boolean(false);
                        }
                        else
                            py_reraise(handler173.exception);
                    }
                    // '*' is special case for always bypass
                    if (no_proxy == (S<"*">()))
                    {
                        return boolean(true);
                    }
                    host = host.method("lower");
                    // strip port off host
                    auto const items174 = unpack((Mod<"urllib.parse">().attr("_splitport"))(host), 2);
                    hostonly = items174[0];
                    port = items174[1];
                    // check if the host ends with any of the DNS suffixes
                    for (py const& item : iterate{ no_proxy.method("split", { S<",">() }) })
                    {
                        name = item;
                        name = name.method("strip");
                        if (name.truthy())
                        {
                            name = name.method("lstrip", { S<".">() });
                            name = name.method("lower");
                            if (((hostonly == name) || (host == name)))
                            {
                                return boolean(true);
                            }
                            name = (S<".">()) + name;
                            if ((hostonly.method("endswith", { name }).truthy() || host.method("endswith", { name }).truthy()))
                            {
                                return boolean(true);
                            }
                        }
                    }
                    // otherwise, don't bypass
                    return boolean(false);
                }));

            // This code tests an OSX specific data structure but is testable on all
            // platforms
            define("urllib.request", "_proxy_bypass_macosx_sysconf", w.function("_proxy_bypass_macosx_sysconf", "_proxy_bypass_macosx_sysconf", { py_positional("host"), py_positional("proxy_settings") }, [](arguments a) -> py
                {
                    py host = a[0];
                    py proxy_settings = a[1];
                    py AddressValueError;
                    py IPv4Address;
                    py base;
                    py fnmatch;
                    py hostIP;
                    py hostonly;
                    py ip2num;
                    py m;
                    py mask;
                    py port;
                    py value;
                    fnmatch = M("fnmatch", "fnmatch");
                    AddressValueError = M("ipaddress", "AddressValueError");
                    IPv4Address = M("ipaddress", "IPv4Address");
                    auto const items176 = unpack((Mod<"urllib.parse">().attr("_splitport"))(host), 2);
                    hostonly = items176[0];
                    port = items176[1];
                    ip2num = w.function("ip2num", "_proxy_bypass_macosx_sysconf.<locals>.ip2num", { py_positional("ipAddr") }, [](arguments a) -> py
    {
        py ipAddr = a[0];
        py parts;
        parts = ipAddr.method("split", { S<".">() });
        parts = (B<"list">())((B<"map">())(B<"int">(), parts));
        if (py{ parts.size() } != (I<4>()))
        {
            parts = (parts + py_list({ I<0>(), I<0>(), I<0>(), I<0>() }))[B<"slice">()(none(), I<4>(), none())];
        }
        return (((parts[I<0>()] << (I<24>())) | (parts[I<1>()] << (I<16>()))) | (parts[I<2>()] << (I<8>()))) | parts[I<3>()];
    });
                    // Check for simple host names:
                    if (!host.contains((S<".">())))
                    {
                        if (proxy_settings[S<"exclude_simple">()].truthy())
                        {
                            return boolean(true);
                        }
                    }
                    hostIP = none();
                    try
                    {
                        hostIP = (B<"int">())(IPv4Address(hostonly));
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler177;
                        if (matches(handler177.exception, AddressValueError))
                        {
                        }
                        else
                            py_reraise(handler177.exception);
                    }
                    for (py const& item : iterate{ proxy_settings.method("get", { S<"exceptions">(), py_tuple() }) })
                    {
                        value = item;
                        // Items in the list are strings like these: *.local, 169.254/16
                        if (!value.truthy())
                        {
                            continue;
                        }
                        m = (Mod<"re">().attr("match"))(S<"(\\d+(?:\\.\\d+)*)(/\\d+)?">(), value);
                        if (((!m.is_none()) && (!hostIP.is_none())))
                        {
                            base = ip2num(m.method("group", { I<1>() }));
                            mask = m.method("group", { I<2>() });
                            if (mask.is_none())
                            {
                                mask = (I<8>()) * (m.method("group", { I<1>() }).method("count", { S<".">() }) + (I<1>()));
                            }
                            else
                            {
                                mask = (B<"int">())(mask[B<"slice">()(I<1>(), none(), none())]);
                            }
                            if (((mask < (I<0>())) || (mask > (I<32>()))))
                            {
                                // System libraries ignore invalid prefix lengths
                                continue;
                            }
                            mask = (I<32>()) - mask;
                            if ((hostIP >> mask) == (base >> mask))
                            {
                                return boolean(true);
                            }
                        }
                        else if (fnmatch(host, value).truthy())
                        {
                            return boolean(true);
                        }
                    }
                    return boolean(false);
                }));

            // Same as _proxy_bypass_macosx_sysconf, testable on all platforms
            define("urllib.request", "_proxy_bypass_winreg_override", w.function("_proxy_bypass_winreg_override", "_proxy_bypass_winreg_override", { py_positional("host"), py_positional("override") }, [](arguments a) -> py
                {
                    py host = a[0];
                    py override = a[1];
                    py _;
                    py fnmatch;
                    py proxy_override;
                    py test;
                    fnmatch = M("fnmatch", "fnmatch");
                    auto const items179 = unpack((Mod<"urllib.parse">().attr("_splitport"))(host), 2);
                    host = items179[0];
                    _ = items179[1];
                    proxy_override = override.method("split", { S<";">() });
                    for (py const& item : iterate{ proxy_override })
                    {
                        test = item;
                        test = test.method("strip");
                        // "<local>" should bypass the proxy server for all intranet addresses
                        if (test == (S<"<local>">()))
                        {
                            if (!host.contains((S<".">())))
                            {
                                return boolean(true);
                            }
                        }
                        else if (fnmatch(host, test).truthy())
                        {
                            return boolean(true);
                        }
                    }
                    return boolean(false);
                }));

#if defined(__APPLE__)
            define("urllib.request", "proxy_bypass_macosx_sysconf", w.function("proxy_bypass_macosx_sysconf", "proxy_bypass_macosx_sysconf", { py_positional("host") }, [](arguments a) -> py
                {
                    py host = a[0];
                    py proxy_settings;
                    proxy_settings = (Mod<"_scproxy">().attr("_get_proxy_settings"))();
                    return (G<"urllib.request._proxy_bypass_macosx_sysconf">())(host, proxy_settings);
                }));

            define("urllib.request", "getproxies_macosx_sysconf", w.function("getproxies_macosx_sysconf", "getproxies_macosx_sysconf", {  }, [](arguments a) -> py
                {
                    return (Mod<"_scproxy">().attr("_get_proxies"))();
                }));

            define("urllib.request", "proxy_bypass", w.function("proxy_bypass", "proxy_bypass", { py_positional("host") }, [](arguments a) -> py
                {
                    py host = a[0];
                    py proxies;
                    proxies = (G<"urllib.request.getproxies_environment">())();
                    if (proxies.truthy())
                    {
                        return (G<"urllib.request.proxy_bypass_environment">())(host, proxies);
                    }
                    else
                    {
                        return (G<"urllib.request.proxy_bypass_macosx_sysconf">())(host);
                    }
                    return none();
                }));

            define("urllib.request", "getproxies", w.function("getproxies", "getproxies", {  }, [](arguments a) -> py
                {
                    return [&]() -> py { py const t = (G<"urllib.request.getproxies_environment">())(); return t.truthy() ? t : (G<"urllib.request.getproxies_macosx_sysconf">())(); }();
                }));

#else
#if defined(_WIN32)
            define("urllib.request", "getproxies_registry", w.function("getproxies_registry", "getproxies_registry", {  }, [](arguments a) -> py
                {
                    py address;
                    py internetSettings;
                    py p;
                    py protocol;
                    py proxies;
                    py proxyEnable;
                    py proxyServer;
                    py winreg;
                    proxies = py_dict();
                    try
                    {
                        winreg = module_of("winreg");
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler181;
                        if (matches(handler181.exception, B<"ImportError">()))
                        {
                            // Std module, so should be around - but you never know!
                            return proxies;
                        }
                        else
                            py_reraise(handler181.exception);
                    }
                    try
                    {
                        internetSettings = winreg.method("OpenKey", { winreg.attr("HKEY_CURRENT_USER"), S<"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings">() });
                        proxyEnable = winreg.method("QueryValueEx", { internetSettings, S<"ProxyEnable">() })[I<0>()];
                        if (proxyEnable.truthy())
                        {
                            // Returned as Unicode but problems if not converted to ASCII
                            proxyServer = (B<"str">())(winreg.method("QueryValueEx", { internetSettings, S<"ProxyServer">() })[I<0>()]);
                            if (((!proxyServer.contains((S<"=">()))) && (!proxyServer.contains((S<";">())))))
                            {
                                // Use one setting for all protocols.
                                proxyServer = (S<"http={0};https={0};ftp={0}">()).method("format", { proxyServer });
                            }
                            for (py const& item : iterate{ proxyServer.method("split", { S<";">() }) })
                            {
                                p = item;
                                auto const items184 = unpack(p.method("split", { S<"=">(), I<1>() }), 2);
                                protocol = items184[0];
                                address = items184[1];
                                // See if address has a type:// prefix
                                if (!(Mod<"re">().attr("match"))(S<"(?:[^/:]+)://">(), address).truthy())
                                {
                                    // Add type:// prefix to address without specifying type
                                    if (py_tuple({ S<"http">(), S<"https">(), S<"ftp">() }).contains(protocol))
                                    {
                                        // The default proxy type of Windows is HTTP
                                        address = (S<"http://">()) + address;
                                    }
                                    else if (protocol == (S<"socks">()))
                                    {
                                        address = (S<"socks://">()) + address;
                                    }
                                }
                                proxies.set_item(protocol, address);
                            }
                            // Use SOCKS proxy for HTTP(S) protocols
                            if (proxies.method("get", { S<"socks">() }).truthy())
                            {
                                // The default SOCKS proxy type of Windows is SOCKS4
                                address = (Mod<"re">().attr("sub"))(S<"^socks://">(), S<"socks4://">(), proxies[S<"socks">()]);
                                proxies.set_item(S<"http">(), [&]() -> py { py const t = proxies.method("get", { S<"http">() }); return t.truthy() ? t : address; }());
                                proxies.set_item(S<"https">(), [&]() -> py { py const t = proxies.method("get", { S<"https">() }); return t.truthy() ? t : address; }());
                            }
                        }
                        internetSettings.method("Close");
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler182;
                        if (matches(handler182.exception, py_tuple({ B<"OSError">(), B<"ValueError">(), B<"TypeError">() })))
                        {
                            // Either registry key not found etc, or the value in an
                            // unexpected format.
                            // proxies already set up to be empty so nothing to do
                        }
                        else
                            py_reraise(handler182.exception);
                    }
                    return proxies;
                }));

            define("urllib.request", "getproxies", w.function("getproxies", "getproxies", {  }, [](arguments a) -> py
                {
                    return [&]() -> py { py const t = (G<"urllib.request.getproxies_environment">())(); return t.truthy() ? t : (G<"urllib.request.getproxies_registry">())(); }();
                }));

            define("urllib.request", "proxy_bypass_registry", w.function("proxy_bypass_registry", "proxy_bypass_registry", { py_positional("host") }, [](arguments a) -> py
                {
                    py host = a[0];
                    py internetSettings;
                    py proxyEnable;
                    py proxyOverride;
                    py winreg;
                    try
                    {
                        winreg = module_of("winreg");
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler185;
                        if (matches(handler185.exception, B<"ImportError">()))
                        {
                            // Std modules, so should be around - but you never know!
                            return boolean(false);
                        }
                        else
                            py_reraise(handler185.exception);
                    }
                    try
                    {
                        internetSettings = winreg.method("OpenKey", { winreg.attr("HKEY_CURRENT_USER"), S<"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings">() });
                        proxyEnable = winreg.method("QueryValueEx", { internetSettings, S<"ProxyEnable">() })[I<0>()];
                        proxyOverride = (B<"str">())(winreg.method("QueryValueEx", { internetSettings, S<"ProxyOverride">() })[I<0>()]);
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler186;
                        // ^^^^ Returned as Unicode but problems if not converted to ASCII
                        if (matches(handler186.exception, B<"OSError">()))
                        {
                            return boolean(false);
                        }
                        else
                            py_reraise(handler186.exception);
                    }
                    if (((!proxyEnable.truthy()) || (!proxyOverride.truthy())))
                    {
                        return boolean(false);
                    }
                    return (G<"urllib.request._proxy_bypass_winreg_override">())(host, proxyOverride);
                }));

            define("urllib.request", "proxy_bypass", w.function("proxy_bypass", "proxy_bypass", { py_positional("host") }, [](arguments a) -> py
                {
                    py host = a[0];
                    py proxies;
                    proxies = (G<"urllib.request.getproxies_environment">())();
                    if (proxies.truthy())
                    {
                        return (G<"urllib.request.proxy_bypass_environment">())(host, proxies);
                    }
                    else
                    {
                        return (G<"urllib.request.proxy_bypass_registry">())(host);
                    }
                    return none();
                }));

#else
            define("urllib.request", "getproxies", G<"urllib.request.getproxies_environment">());
            define("urllib.request", "proxy_bypass", G<"urllib.request.proxy_bypass_environment">());
#endif
#endif
        }

        bool urllib_request_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_urllib_request();
                }
                if (auto const doc = urllib_request_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("base64", py{ fn_import("base64") }.release());
                aAttributes.emplace_back("bisect", py{ fn_import("bisect") }.release());
                aAttributes.emplace_back("email", py{ fn_import("email") }.release());
                aAttributes.emplace_back("hashlib", py{ fn_import("hashlib") }.release());
                aAttributes.emplace_back("http", py{ fn_import("http") }.release());
                aAttributes.emplace_back("io", py{ fn_import("io") }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("socket", py{ fn_import("socket") }.release());
                aAttributes.emplace_back("string", py{ fn_import("string") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("time", py{ fn_import("time") }.release());
                aAttributes.emplace_back("tempfile", py{ fn_import("tempfile") }.release());
                aAttributes.emplace_back("contextlib", py{ fn_import("contextlib") }.release());
                aAttributes.emplace_back("warnings", py{ fn_import("warnings") }.release());
                aAttributes.emplace_back("URLError", import_attribute("urllib.error", "URLError").release());
                aAttributes.emplace_back("HTTPError", import_attribute("urllib.error", "HTTPError").release());
                aAttributes.emplace_back("ContentTooShortError", import_attribute("urllib.error", "ContentTooShortError").release());
                aAttributes.emplace_back("urlparse", import_attribute("urllib.parse", "urlparse").release());
                aAttributes.emplace_back("urlsplit", import_attribute("urllib.parse", "urlsplit").release());
                aAttributes.emplace_back("urljoin", import_attribute("urllib.parse", "urljoin").release());
                aAttributes.emplace_back("unwrap", import_attribute("urllib.parse", "unwrap").release());
                aAttributes.emplace_back("quote", import_attribute("urllib.parse", "quote").release());
                aAttributes.emplace_back("unquote", import_attribute("urllib.parse", "unquote").release());
                aAttributes.emplace_back("_splittype", import_attribute("urllib.parse", "_splittype").release());
                aAttributes.emplace_back("_splithost", import_attribute("urllib.parse", "_splithost").release());
                aAttributes.emplace_back("_splitport", import_attribute("urllib.parse", "_splitport").release());
                aAttributes.emplace_back("_splituser", import_attribute("urllib.parse", "_splituser").release());
                aAttributes.emplace_back("_splitpasswd", import_attribute("urllib.parse", "_splitpasswd").release());
                aAttributes.emplace_back("_splitattr", import_attribute("urllib.parse", "_splitattr").release());
                aAttributes.emplace_back("_splitquery", import_attribute("urllib.parse", "_splitquery").release());
                aAttributes.emplace_back("_splitvalue", import_attribute("urllib.parse", "_splitvalue").release());
                aAttributes.emplace_back("_splittag", import_attribute("urllib.parse", "_splittag").release());
                aAttributes.emplace_back("_to_bytes", import_attribute("urllib.parse", "_to_bytes").release());
                aAttributes.emplace_back("unquote_to_bytes", import_attribute("urllib.parse", "unquote_to_bytes").release());
                aAttributes.emplace_back("urlunparse", import_attribute("urllib.parse", "urlunparse").release());
                aAttributes.emplace_back("addinfourl", import_attribute("urllib.response", "addinfourl").release());
                aAttributes.emplace_back("addclosehook", import_attribute("urllib.response", "addclosehook").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("urllib.request", urllib_request_module);
    }
}
