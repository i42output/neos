/*
  socketserver.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/socketserver.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module socketserver (CPython 3.13's Lib/socketserver.py): BaseServer, TCPServer, UDPServer, ForkingMixIn (if os has fork),
// ThreadingMixIn, the servers they make (the Unix ones if socket has AF_UNIX), BaseRequestHandler, StreamRequestHandler and
// DatagramRequestHandler; its classes are made (by type) of closures, so a program's subclasses override their methods. BaseServer's
// private attributes are as Python mangles them (_BaseServer__is_shut_down and _BaseServer__shutdown_request).

#include <functional>
#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "socketserver_docs.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        char const* const sModule = "socketserver";

        // (the module's objects: made once (and never destroyed: they outlive the program's objects))
        struct module_state
        {
            std::map<std::string, py, std::less<>> classes;     ///< (the module's classes, by name)
            std::vector<std::string> order;                     ///< (their names, as they are made)
            py serverSelector;                                  ///< (_ServerSelector)
            bool made = false;
        };
        module_state& state()
        {
            static module_state* sState = new module_state{};
            return *sState;
        }

        py cls(char const* aName) { return state().classes.at(aName); }
        py none() { return py::none(); }
        py boolean(bool aValue) { return py::boolean(aValue); }
        py S(char const* aText) { return py{ aText }; }
        py M(char const* aModule, char const* aName) { return import_attribute(aModule, aName); }
        bool has_attr(py const& aObject, char const* aName) { return builtin("hasattr")(aObject, S(aName)).truthy(); }
        char const* D(char const* aName)
        {
            auto const doc = socketserver_docs::doc(aName);
            return doc != nullptr ? doc : "";
        }
        // (time: time.monotonic)
        py now() { return M("time", "monotonic")(); }

        // (a closure as a function of the module)
        using body_type = std::function<py(std::vector<py> const&)>;
        py bound_function(std::string const& aName, std::string const& aQualname, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs,
            char const* aDoc)
        {
            return make_native_function(aName, aQualname, aDoc, sModule, [aQualname, aParameters, aBody, aVarArgs](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                py rest;
                auto a = py_bind(aQualname, aParameters, aArgs, aKwargs, aVarArgs ? &rest : nullptr, nullptr);
                if (aVarArgs)
                    a.push_back(rest);
                return aBody(a);
            });
        }
        // (a method: its parameters after self (aClass: its class's name); its docstring: socketserver.py's)
        py method(char const* aClass, char const* aName, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false)
        {
            aParameters.insert(aParameters.begin(), py_positional("self"));
            std::string const qualname = std::string{ aClass } + "." + aName;
            return bound_function(aName, qualname, std::move(aParameters), std::move(aBody), aVarArgs, D(qualname.c_str()));
        }

        using namespace_type = std::vector<std::pair<char const*, py>>;

        // (a class of the module: type(aName, aBases, namespace); its docstring: socketserver.py's)
        py make_server_class(char const* aName, std::vector<py> const& aBases, namespace_type const& aNamespace)
        {
            py const ns = py_dict();
            ns.set_item(S("__module__"), S(sModule));
            ns.set_item(S("__qualname__"), S(aName));
            if (auto const doc = socketserver_docs::doc(aName))
                ns.set_item(S("__doc__"), S(doc));
            for (auto const& [name, value] : aNamespace)
                ns.set_item(S(name), value);
            std::vector<py> const bases = aBases.empty() ? std::vector<py>{ builtin("object") } : aBases;
            py meta = builtin("type");
            for (auto const& b : bases)
            {
                py const t = type_of(b);
                if (!t.is(meta) && issubclass(t, meta))
                    meta = t;
            }
            py const result = meta(S(aName), py_tuple(bases), ns);
            state().classes[aName] = result;
            state().order.emplace_back(aName);
            return result;
        }

        // (try: aBody() except aClass (a class or a tuple of classes) as e: aHandler(e) (sys.exception() is e while it runs); another
        // exception remains raised)
        template <typename F, typename H>
        void try_except(F&& aBody, py const& aClass, H&& aHandler)
        {
            try
            {
                aBody();
            }
            catch (raised const&)
            {
                std::string const text = error_text();
                clear_error();
                ref const caughtException{ fn_caught(text.c_str()) };
                py const e = py::borrowed(caughtException.get());
                if (!isinstance(e, aClass))
                {
                    fn_handled();
                    py_reraise(e);
                }
                try
                {
                    aHandler(e);
                }
                catch (raised const&)
                {
                    fn_handled();
                    throw;
                }
                fn_handled();
            }
        }
        // (try: aBody() finally: aFinally())
        template <typename F, typename G>
        void try_finally(F&& aBody, G&& aFinally)
        {
            try
            {
                aBody();
            }
            catch (raised const&)
            {
                py const e = py_take_exception();
                aFinally();
                py_reraise(e);
            }
            aFinally();
        }
        // (with aManager as value: aBody(value))
        template <typename F>
        void with_(py const& aManager, F&& aBody)
        {
            py const value = aManager.method("__enter__");
            try
            {
                aBody(value);
            }
            catch (raised const&)
            {
                py const e = py_take_exception();
                if (!aManager.method("__exit__", { type_of(e), e, e.attr("__traceback__") }).truthy())
                    py_reraise(e);
                return;
            }
            aManager.method("__exit__", { none(), none(), none() });
        }
        // (a pair's items (a, b = value))
        std::pair<py, py> unpack2(py const& aValue)
        {
            auto const items = aValue.items();
            if (items.size() != 2u)
                py_raise("ValueError", items.size() < 2u ? "not enough values to unpack (expected 2, got " + std::to_string(items.size()) + ")" :
                    "too many values to unpack (expected 2)");
            return { items[0], items[1] };
        }
        // (super().aName(...) in a method of aClass)
        py super_(char const* aClass, py const& self) { return builtin("super")(cls(aClass), self); }

        void make_servers()
        {
            make_server_class("BaseServer", {}, {
                { "timeout", none() },
                { "__init__", method("BaseServer", "__init__", { py_positional("server_address"), py_positional("RequestHandlerClass") }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("server_address", a[1]);
                        a[0].set_attr("RequestHandlerClass", a[2]);
                        a[0].set_attr("_BaseServer__is_shut_down", M("threading", "Event")());
                        a[0].set_attr("_BaseServer__shutdown_request", boolean(false));
                        return none();
                    }) },
                { "server_activate", method("BaseServer", "server_activate", {}, [](std::vector<py> const&) { return none(); }) },
                { "serve_forever", method("BaseServer", "serve_forever", { py_positional("poll_interval", py{ 0.5 }) }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        self.attr("_BaseServer__is_shut_down").method("clear");
                        try_finally([&]()
                        {
                            // (polling (rather than another file descriptor, or a connection to the socket, waking it) makes it less responsive to
                            // a shutdown request and wastes CPU at all other times)
                            with_(state().serverSelector(), [&](py const& selector)
                            {
                                selector.method("register", { self, M("selectors", "EVENT_READ") });
                                while (!self.attr("_BaseServer__shutdown_request").truthy())
                                {
                                    py const ready = selector.method("select", { a[1] });
                                    // (bpo-35017: shutdown() called during select(): it exits immediately)
                                    if (self.attr("_BaseServer__shutdown_request").truthy())
                                        break;
                                    if (ready.truthy())
                                        self.method("_handle_request_noblock");
                                    self.method("service_actions");
                                }
                            });
                        }, [&]()
                        {
                            self.set_attr("_BaseServer__shutdown_request", boolean(false));
                            self.attr("_BaseServer__is_shut_down").method("set");
                        });
                        return none();
                    }) },
                { "shutdown", method("BaseServer", "shutdown", {}, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("_BaseServer__shutdown_request", boolean(true));
                        a[0].attr("_BaseServer__is_shut_down").method("wait");
                        return none();
                    }) },
                { "service_actions", method("BaseServer", "service_actions", {}, [](std::vector<py> const&) { return none(); }) },
                // (handle_request() is the top-level call: it calls selector.select(), get_request(), verify_request() and process_request();
                // get_request() differs for stream and datagram sockets; process_request() may fork a process or start a thread to finish the
                // request; finish_request() instantiates the request handler class, whose constructor handles the request)
                { "handle_request", method("BaseServer", "handle_request", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        // (socket.settimeout(), as used to escape handle_request before self.timeout was available)
                        py timeout = self.attr("socket").method("gettimeout");
                        if (timeout.is_none())
                            timeout = self.attr("timeout");
                        else if (!self.attr("timeout").is_none())
                            timeout = builtin("min")(timeout, self.attr("timeout"));
                        py deadline;
                        if (!timeout.is_none())
                            deadline = now() + timeout;
                        // (until a request arrives or the timeout expires (a loop: there can be early wakeups (EINTR)))
                        py result = none();
                        with_(state().serverSelector(), [&](py const& selector)
                        {
                            selector.method("register", { self, M("selectors", "EVENT_READ") });
                            for (;;)
                            {
                                if (selector.method("select", { timeout }).truthy())
                                {
                                    result = self.method("_handle_request_noblock");
                                    return;
                                }
                                if (!timeout.is_none())
                                {
                                    timeout = deadline - now();
                                    if (timeout < py{ 0 })
                                    {
                                        result = self.method("handle_timeout");
                                        return;
                                    }
                                }
                            }
                        });
                        return result;
                    }) },
                { "_handle_request_noblock", method("BaseServer", "_handle_request_noblock", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        // (selector.select() has returned that the socket is readable: get_request() should not block)
                        py request;
                        py clientAddress;
                        bool failed = false;
                        try_except([&]()
                        {
                            auto const [r, c] = unpack2(self.method("get_request"));
                            request = r;
                            clientAddress = c;
                        }, builtin("OSError"), [&](py const&) { failed = true; });
                        if (failed)
                            return none();
                        if (self.method("verify_request", { request, clientAddress }).truthy())
                            try_except([&]()
                            {
                                try_except([&]() { self.method("process_request", { request, clientAddress }); }, builtin("Exception"), [&](py const&)
                                {
                                    self.method("handle_error", { request, clientAddress });
                                    self.method("shutdown_request", { request });
                                });
                            }, builtin("BaseException"), [&](py const& e)
                            {
                                self.method("shutdown_request", { request });
                                py_reraise(e);
                            });
                        else
                            self.method("shutdown_request", { request });
                        return none();
                    }) },
                { "handle_timeout", method("BaseServer", "handle_timeout", {}, [](std::vector<py> const&) { return none(); }) },
                { "verify_request", method("BaseServer", "verify_request", { py_positional("request"), py_positional("client_address") }, [](std::vector<py> const&)
                    {
                        return boolean(true);
                    }) },
                { "process_request", method("BaseServer", "process_request", { py_positional("request"), py_positional("client_address") }, [](std::vector<py> const& a)
                    {
                        a[0].method("finish_request", { a[1], a[2] });
                        a[0].method("shutdown_request", { a[1] });
                        return none();
                    }) },
                { "server_close", method("BaseServer", "server_close", {}, [](std::vector<py> const&) { return none(); }) },
                { "finish_request", method("BaseServer", "finish_request", { py_positional("request"), py_positional("client_address") }, [](std::vector<py> const& a)
                    {
                        a[0].attr("RequestHandlerClass")(a[1], a[2], a[0]);
                        return none();
                    }) },
                { "shutdown_request", method("BaseServer", "shutdown_request", { py_positional("request") }, [](std::vector<py> const& a)
                    {
                        a[0].method("close_request", { a[1] });
                        return none();
                    }) },
                { "close_request", method("BaseServer", "close_request", { py_positional("request") }, [](std::vector<py> const&) { return none(); }) },
                { "handle_error", method("BaseServer", "handle_error", { py_positional("request"), py_positional("client_address") }, [](std::vector<py> const& a)
                    {
                        py const stderr_ = M("sys", "stderr");
                        builtin("print").call({ S("----------------------------------------") }, { { "file", stderr_ } });
                        builtin("print").call({ S("Exception occurred during processing of request from"), a[2] }, { { "file", stderr_ } });
                        M("traceback", "print_exc")();
                        builtin("print").call({ S("----------------------------------------") }, { { "file", stderr_ } });
                        return none();
                    }) },
                { "__enter__", method("BaseServer", "__enter__", {}, [](std::vector<py> const& a) { return a[0]; }) },
                { "__exit__", method("BaseServer", "__exit__", {}, [](std::vector<py> const& a)
                    {
                        a[0].method("server_close");
                        return none();
                    }, true) } });

            make_server_class("TCPServer", { cls("BaseServer") }, {
                { "address_family", M("socket", "AF_INET") },
                { "socket_type", M("socket", "SOCK_STREAM") },
                { "request_queue_size", py{ 5 } },
                { "allow_reuse_address", boolean(false) },
                { "allow_reuse_port", boolean(false) },
                { "__init__", method("TCPServer", "__init__", { py_positional("server_address"), py_positional("RequestHandlerClass"),
                    py_positional("bind_and_activate", boolean(true)) }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        cls("BaseServer").attr("__init__")(self, a[1], a[2]);
                        self.set_attr("socket", M("socket", "socket")(self.attr("address_family"), self.attr("socket_type")));
                        if (a[3].truthy())
                            try_except([&]()
                            {
                                self.method("server_bind");
                                self.method("server_activate");
                            }, builtin("BaseException"), [&](py const& e)
                            {
                                self.method("server_close");
                                py_reraise(e);
                            });
                        return none();
                    }) },
                { "server_bind", method("TCPServer", "server_bind", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const socket{ fn_import("socket") };
                        if (self.attr("allow_reuse_address").truthy() && has_attr(socket, "SO_REUSEADDR"))
                            self.attr("socket").method("setsockopt", { socket.attr("SOL_SOCKET"), socket.attr("SO_REUSEADDR"), py{ 1 } });
                        // (since Linux 6.12.9, SO_REUSEPORT is not allowed for address families other than AF_INET and AF_INET6)
                        if (self.attr("allow_reuse_port").truthy() && has_attr(socket, "SO_REUSEPORT") &&
                            py_tuple({ socket.attr("AF_INET"), socket.attr("AF_INET6") }).contains(self.attr("address_family")))
                            self.attr("socket").method("setsockopt", { socket.attr("SOL_SOCKET"), socket.attr("SO_REUSEPORT"), py{ 1 } });
                        self.attr("socket").method("bind", { self.attr("server_address") });
                        self.set_attr("server_address", self.attr("socket").method("getsockname"));
                        return none();
                    }) },
                { "server_activate", method("TCPServer", "server_activate", {}, [](std::vector<py> const& a)
                    {
                        a[0].attr("socket").method("listen", { a[0].attr("request_queue_size") });
                        return none();
                    }) },
                { "server_close", method("TCPServer", "server_close", {}, [](std::vector<py> const& a)
                    {
                        a[0].attr("socket").method("close");
                        return none();
                    }) },
                { "fileno", method("TCPServer", "fileno", {}, [](std::vector<py> const& a) { return a[0].attr("socket").method("fileno"); }) },
                { "get_request", method("TCPServer", "get_request", {}, [](std::vector<py> const& a) { return a[0].attr("socket").method("accept"); }) },
                { "shutdown_request", method("TCPServer", "shutdown_request", { py_positional("request") }, [](std::vector<py> const& a)
                    {
                        // (an explicit shutdown: socket.close() merely releases the socket (the actual close waits for the GC)); some platforms may
                        // raise ENOTCONN here
                        try_except([&]() { a[1].method("shutdown", { M("socket", "SHUT_WR") }); }, builtin("OSError"), [](py const&) {});
                        a[0].method("close_request", { a[1] });
                        return none();
                    }) },
                { "close_request", method("TCPServer", "close_request", { py_positional("request") }, [](std::vector<py> const& a)
                    {
                        a[1].method("close");
                        return none();
                    }) } });

            make_server_class("UDPServer", { cls("TCPServer") }, {
                { "allow_reuse_address", boolean(false) },
                { "allow_reuse_port", boolean(false) },
                { "socket_type", M("socket", "SOCK_DGRAM") },
                { "max_packet_size", py{ 8192 } },
                { "get_request", method("UDPServer", "get_request", {}, [](std::vector<py> const& a)
                    {
                        auto const [data, clientAddr] = unpack2(a[0].attr("socket").method("recvfrom", { a[0].attr("max_packet_size") }));
                        return py_tuple({ py_tuple({ data, a[0].attr("socket") }), clientAddr });
                    }) },
                // (listen() is not needed for UDP; nor is a shutdown, nor a close)
                { "server_activate", method("UDPServer", "server_activate", {}, [](std::vector<py> const&) { return none(); }) },
                { "shutdown_request", method("UDPServer", "shutdown_request", { py_positional("request") }, [](std::vector<py> const& a)
                    {
                        a[0].method("close_request", { a[1] });
                        return none();
                    }) },
                { "close_request", method("UDPServer", "close_request", { py_positional("request") }, [](std::vector<py> const&) { return none(); }) } });
        }

        void make_mixins()
        {
            if (has_attr(py{ fn_import("os") }, "fork"))
                make_server_class("ForkingMixIn", {}, {
                    { "timeout", py{ 300 } },
                    { "active_children", none() },
                    { "max_children", py{ 40 } },
                    // (if true, server_close() waits until all of the child processes complete)
                    { "block_on_close", boolean(true) },
                    { "collect_children", method("ForkingMixIn", "collect_children", { py_keyword("blocking", boolean(false)) }, [](std::vector<py> const& a)
                        {
                            auto const& self = a[0];
                            if (self.attr("active_children").is_none())
                                return none();
                            // (above the maximum number of children: they are waited for and reaped until it is below it (waitpid(-1): fewer
                            // syscalls, but it might reap children that it did not spawn (so only above max_children)))
                            while (py{ self.attr("active_children").size() } >= self.attr("max_children"))
                            {
                                bool stop = false;
                                try_except([&]()
                                {
                                    try_except([&]()
                                    {
                                        auto const [pid, status] = unpack2(M("os", "waitpid")(py{ -1 }, py{ 0 }));
                                        self.attr("active_children").method("discard", { pid });
                                    }, builtin("ChildProcessError"), [&](py const&)
                                    {
                                        self.attr("active_children").method("clear");   // (no children: done)
                                    });
                                }, builtin("OSError"), [&](py const&) { stop = true; });
                                if (stop)
                                    break;
                            }
                            // (all of the defunct children: reaped)
                            for (py const& pid : self.attr("active_children").method("copy").items())
                                try_except([&]()
                                {
                                    try_except([&]()
                                    {
                                        py const flags = a[1].truthy() ? py{ 0 } : M("os", "WNOHANG");
                                        auto const [reaped, status] = unpack2(M("os", "waitpid")(pid, flags));
                                        // (a child that has not exited: pid 0 (ignored by discard()))
                                        self.attr("active_children").method("discard", { reaped });
                                    }, builtin("ChildProcessError"), [&](py const&)
                                    {
                                        self.attr("active_children").method("discard", { pid });    // (someone else reaped it)
                                    });
                                }, builtin("OSError"), [](py const&) {});
                            return none();
                        }) },
                    { "handle_timeout", method("ForkingMixIn", "handle_timeout", {}, [](std::vector<py> const& a)
                        {
                            a[0].method("collect_children");
                            return none();
                        }) },
                    { "service_actions", method("ForkingMixIn", "service_actions", {}, [](std::vector<py> const& a)
                        {
                            a[0].method("collect_children");
                            return none();
                        }) },
                    { "process_request", method("ForkingMixIn", "process_request", { py_positional("request"), py_positional("client_address") }, [](std::vector<py> const& a)
                        {
                            auto const& self = a[0];
                            py const pid = M("os", "fork")();
                            if (pid.truthy())
                            {
                                // (the parent process)
                                if (self.attr("active_children").is_none())
                                    self.set_attr("active_children", builtin("set")());
                                self.attr("active_children").method("add", { pid });
                                self.method("close_request", { a[1] });
                                return none();
                            }
                            // (the child process: it must never return (os._exit()))
                            py status{ 1 };
                            try_finally([&]()
                            {
                                try_except([&]()
                                {
                                    self.method("finish_request", { a[1], a[2] });
                                    status = py{ 0 };
                                }, builtin("Exception"), [&](py const&) { self.method("handle_error", { a[1], a[2] }); });
                            }, [&]()
                            {
                                try_finally([&]() { self.method("shutdown_request", { a[1] }); }, [&]() { M("os", "_exit")(status); });
                            });
                            return none();
                        }) },
                    { "server_close", method("ForkingMixIn", "server_close", {}, [](std::vector<py> const& a)
                        {
                            super_("ForkingMixIn", a[0]).method("server_close");
                            a[0].method("collect_children", {}, { { "blocking", a[0].attr("block_on_close") } });
                            return none();
                        }) } });

            make_server_class("_Threads", { builtin("list") }, {
                { "append", method("_Threads", "append", { py_positional("thread") }, [](std::vector<py> const& a)
                    {
                        a[0].method("reap");
                        if (a[1].attr("daemon").truthy())
                            return none();
                        super_("_Threads", a[0]).method("append", { a[1] });
                        return none();
                    }) },
                { "pop_all", method("_Threads", "pop_all", {}, [](std::vector<py> const& a)
                    {
                        // (self[:], result = [], self[:])
                        py const all = py{ make_slice(none().get(), none().get(), nullptr) };
                        py const result = a[0][all];
                        a[0].set_item(all, py_list());
                        return result;
                    }) },
                { "join", method("_Threads", "join", {}, [](std::vector<py> const& a)
                    {
                        for (py const& thread : a[0].method("pop_all").items())
                            thread.method("join");
                        return none();
                    }) },
                { "reap", method("_Threads", "reap", {}, [](std::vector<py> const& a)
                    {
                        py const alive = py_list();
                        for (py const& thread : a[0].items())
                            if (thread.method("is_alive").truthy())
                                alive.method("append", { thread });
                        a[0].set_item(py{ make_slice(none().get(), none().get(), nullptr) }, alive);
                        return none();
                    }) } });
            make_server_class("_NoThreads", {}, {
                { "append", method("_NoThreads", "append", { py_positional("thread") }, [](std::vector<py> const&) { return none(); }) },
                { "join", method("_NoThreads", "join", {}, [](std::vector<py> const&) { return none(); }) } });

            make_server_class("ThreadingMixIn", {}, {
                // (how threads act when the main process ends)
                { "daemon_threads", boolean(false) },
                // (if true, server_close() waits until all of the non-daemon threads end)
                { "block_on_close", boolean(true) },
                // (the threads that server_close() waits for)
                { "_threads", cls("_NoThreads")() },
                { "process_request_thread", method("ThreadingMixIn", "process_request_thread", { py_positional("request"), py_positional("client_address") },
                    [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        try_finally([&]()
                        {
                            try_except([&]() { self.method("finish_request", { a[1], a[2] }); }, builtin("Exception"), [&](py const&)
                            {
                                self.method("handle_error", { a[1], a[2] });
                            });
                        }, [&]() { self.method("shutdown_request", { a[1] }); });
                        return none();
                    }) },
                { "process_request", method("ThreadingMixIn", "process_request", { py_positional("request"), py_positional("client_address") }, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        if (self.attr("block_on_close").truthy())
                            builtin("vars")(self).method("setdefault", { S("_threads"), cls("_Threads")() });
                        py const t = M("threading", "Thread").call({}, { { "target", self.attr("process_request_thread") }, { "args", py_tuple({ a[1], a[2] }) } });
                        t.set_attr("daemon", self.attr("daemon_threads"));
                        self.attr("_threads").method("append", { t });
                        t.method("start");
                        return none();
                    }) },
                { "server_close", method("ThreadingMixIn", "server_close", {}, [](std::vector<py> const& a)
                    {
                        super_("ThreadingMixIn", a[0]).method("server_close");
                        a[0].attr("_threads").method("join");
                        return none();
                    }) } });

            bool const fork = has_attr(py{ fn_import("os") }, "fork");
            if (fork)
            {
                make_server_class("ForkingUDPServer", { cls("ForkingMixIn"), cls("UDPServer") }, {});
                make_server_class("ForkingTCPServer", { cls("ForkingMixIn"), cls("TCPServer") }, {});
            }
            make_server_class("ThreadingUDPServer", { cls("ThreadingMixIn"), cls("UDPServer") }, {});
            make_server_class("ThreadingTCPServer", { cls("ThreadingMixIn"), cls("TCPServer") }, {});
            if (has_attr(py{ fn_import("socket") }, "AF_UNIX"))
            {
                make_server_class("UnixStreamServer", { cls("TCPServer") }, { { "address_family", M("socket", "AF_UNIX") } });
                make_server_class("UnixDatagramServer", { cls("UDPServer") }, { { "address_family", M("socket", "AF_UNIX") } });
                make_server_class("ThreadingUnixStreamServer", { cls("ThreadingMixIn"), cls("UnixStreamServer") }, {});
                make_server_class("ThreadingUnixDatagramServer", { cls("ThreadingMixIn"), cls("UnixDatagramServer") }, {});
                if (fork)
                {
                    make_server_class("ForkingUnixStreamServer", { cls("ForkingMixIn"), cls("UnixStreamServer") }, {});
                    make_server_class("ForkingUnixDatagramServer", { cls("ForkingMixIn"), cls("UnixDatagramServer") }, {});
                }
            }
        }

        void make_request_handlers()
        {
            make_server_class("BaseRequestHandler", {}, {
                { "__init__", method("BaseRequestHandler", "__init__", { py_positional("request"), py_positional("client_address"), py_positional("server") },
                    [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        self.set_attr("request", a[1]);
                        self.set_attr("client_address", a[2]);
                        self.set_attr("server", a[3]);
                        self.method("setup");
                        try_finally([&]() { self.method("handle"); }, [&]() { self.method("finish"); });
                        return none();
                    }) },
                { "setup", method("BaseRequestHandler", "setup", {}, [](std::vector<py> const&) { return none(); }) },
                { "handle", method("BaseRequestHandler", "handle", {}, [](std::vector<py> const&) { return none(); }) },
                { "finish", method("BaseRequestHandler", "finish", {}, [](std::vector<py> const&) { return none(); }) } });

            // (StreamRequestHandler and DatagramRequestHandler: the same service class works for stream and datagram servers: rfile (a file
            // object that the request is read from) and wfile (one that the reply is written to: flushed when handle() returns))
            make_server_class("_SocketWriter", { M("io", "BufferedIOBase") }, {
                { "__init__", method("_SocketWriter", "__init__", { py_positional("sock") }, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("_sock", a[1]);
                        return none();
                    }) },
                { "writable", method("_SocketWriter", "writable", {}, [](std::vector<py> const&) { return boolean(true); }) },
                { "write", method("_SocketWriter", "write", { py_positional("b") }, [](std::vector<py> const& a)
                    {
                        a[0].attr("_sock").method("sendall", { a[1] });
                        py nbytes;
                        with_(builtin("memoryview")(a[1]), [&](py const& view) { nbytes = view.attr("nbytes"); });
                        return nbytes;
                    }) },
                { "fileno", method("_SocketWriter", "fileno", {}, [](std::vector<py> const& a) { return a[0].attr("_sock").method("fileno"); }) } });

            make_server_class("StreamRequestHandler", { cls("BaseRequestHandler") }, {
                // (the default buffer sizes of rfile and wfile: rfile is buffered (unbuffered, large reads would be slow (a getc() per byte));
                // wfile is unbuffered (after a write, a read often needs the line flushed, and stdio typically optimises big writes to
                // unbuffered files))
                { "rbufsize", py{ -1 } },
                { "wbufsize", py{ 0 } },
                // (a timeout of the request's socket, if it is not None)
                { "timeout", none() },
                // (the Nagle algorithm, disabled for the socket if True (only when wbufsize != 0, to avoid small packets))
                { "disable_nagle_algorithm", boolean(false) },
                { "setup", method("StreamRequestHandler", "setup", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        self.set_attr("connection", self.attr("request"));
                        if (!self.attr("timeout").is_none())
                            self.attr("connection").method("settimeout", { self.attr("timeout") });
                        if (self.attr("disable_nagle_algorithm").truthy())
                            self.attr("connection").method("setsockopt", { M("socket", "IPPROTO_TCP"), M("socket", "TCP_NODELAY"), boolean(true) });
                        self.set_attr("rfile", self.attr("connection").method("makefile", { S("rb"), self.attr("rbufsize") }));
                        if (self.attr("wbufsize") == py{ 0 })
                            self.set_attr("wfile", cls("_SocketWriter")(self.attr("connection")));
                        else
                            self.set_attr("wfile", self.attr("connection").method("makefile", { S("wb"), self.attr("wbufsize") }));
                        return none();
                    }) },
                { "finish", method("StreamRequestHandler", "finish", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        if (!self.attr("wfile").attr("closed").truthy())
                            // (a final socket error may have occurred here, such as the local error ECONNABORTED)
                            try_except([&]() { self.attr("wfile").method("flush"); }, M("socket", "error"), [](py const&) {});
                        self.attr("wfile").method("close");
                        self.attr("rfile").method("close");
                        return none();
                    }) } });

            make_server_class("DatagramRequestHandler", { cls("BaseRequestHandler") }, {
                { "setup", method("DatagramRequestHandler", "setup", {}, [](std::vector<py> const& a)
                    {
                        auto const& self = a[0];
                        py const bytesIO = M("io", "BytesIO");
                        auto const [packet, socket] = unpack2(self.attr("request"));
                        self.set_attr("packet", packet);
                        self.set_attr("socket", socket);
                        self.set_attr("rfile", bytesIO(self.attr("packet")));
                        self.set_attr("wfile", bytesIO());
                        return none();
                    }) },
                { "finish", method("DatagramRequestHandler", "finish", {}, [](std::vector<py> const& a)
                    {
                        a[0].attr("socket").method("sendto", { a[0].attr("wfile").method("getvalue"), a[0].attr("client_address") });
                        return none();
                    }) } });
        }

        bool socketserver_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = state();
                if (!s.made)
                {
                    s.made = true;
                    // (poll and select need no extra file descriptor, unlike epoll and kqueue (and they need a single syscall))
                    py const selectors{ fn_import("selectors") };
                    s.serverSelector = has_attr(selectors, "PollSelector") ? selectors.attr("PollSelector") : selectors.attr("SelectSelector");
                    make_servers();
                    make_mixins();
                    make_request_handlers();
                }
                auto const add = [&](char const* aName, py const& aValue) { aAttributes.emplace_back(aName, aValue.reference()); };
                add("__doc__", S(D("")));
                add("__version__", S("0.4"));
                std::vector<py> all;
                for (auto n : { "BaseServer", "TCPServer", "UDPServer", "ThreadingUDPServer", "ThreadingTCPServer", "BaseRequestHandler", "StreamRequestHandler",
                    "DatagramRequestHandler", "ThreadingMixIn" })
                    all.emplace_back(n);
                bool const fork = has_attr(py{ fn_import("os") }, "fork");
                bool const unix = has_attr(py{ fn_import("socket") }, "AF_UNIX");
                if (fork)
                    for (auto n : { "ForkingUDPServer", "ForkingTCPServer", "ForkingMixIn" })
                        all.emplace_back(n);
                if (unix)
                {
                    for (auto n : { "UnixStreamServer", "UnixDatagramServer", "ThreadingUnixStreamServer", "ThreadingUnixDatagramServer" })
                        all.emplace_back(n);
                    if (fork)
                        for (auto n : { "ForkingUnixStreamServer", "ForkingUnixDatagramServer" })
                            all.emplace_back(n);
                }
                add("__all__", py_list(all));
                for (auto module : { "socket", "selectors", "os", "sys", "threading" })
                    add(module, py{ fn_import(module) });
                add("BufferedIOBase", M("io", "BufferedIOBase"));
                add("time", M("time", "monotonic"));
                add("_ServerSelector", s.serverSelector);
                for (auto const& name : s.order)
                    add(name.c_str(), s.classes.at(name));
                return none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sSocketserver = register_module("socketserver", socketserver_module);
    }
}
