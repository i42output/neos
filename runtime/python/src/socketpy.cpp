/*
  socketpy.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/socket.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see runtime/python/LICENSE.PSF).
  Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to its object model (see
  runtime/python/NOTICE).
*/

// The module socket (CPython 3.13's Lib/socket.py): the module _socket's names (socket.cpp), with its constants AF_*, SOCK_*, MSG_* and AI_*
// as members of the enums AddressFamily, SocketKind, MsgFlag and AddressInfo, the class socket (a subclass of _socket.socket: makefile(),
// accept(), sendfile(), ...), SocketIO (the raw I/O of makefile()), socketpair(), fromfd(), getfqdn(), create_connection(), create_server(),
// has_dualstack_ipv6() and getaddrinfo(). Its classes are made (by type, or abc.ABCMeta) of closures, so a program's subclasses override their
// methods. send_fds() and recv_fds() are defined where _socket's sendmsg() and recvmsg() are (not Windows); _socket has no share(), so
// fromshare() is not defined (as Lib/socket.py defines them only if they are).

#include <algorithm>
#include <functional>
#include <map>
#include <tuple>
#include <string>
#include <vector>
#include "objects.hpp"
#include "values.hpp"
#include "socket_docs.hpp"

namespace neos::python_runtime::detail
{
    namespace
    {
        char const* const sModule = "socket";

        py S(char const* aText) { return py{ aText }; }
        py S(std::string const& aText) { return py{ aText }; }
        py none() { return py::none(); }
        py boolean(bool aValue) { return py::boolean(aValue); }

        // (the module's objects: made once)
        struct module_state
        {
            std::map<std::string, py, std::less<>> classes;
            py globalDefaultTimeout;
            bool made = false;
        };
        module_state& state()
        {
            static module_state* sState = new module_state{};
            return *sState;
        }

        py cls(char const* aName) { return state().classes.at(aName); }
        // (a global of the module (looked up when it is used, as Python's functions look up their globals))
        py global(char const* aName) { return import_attribute(sModule, aName); }
        py _socket(char const* aName) { return import_attribute("_socket", aName); }

        // (the module's attributes while it is made: its globals (by name))
        std::map<std::string, py, std::less<>>& members()
        {
            static auto* sMembers = new std::map<std::string, py, std::less<>>{};
            return *sMembers;
        }
        // (a global of the module while it is made (e.g. a parameter's default))
        py global_member(char const* aName) { return members().at(aName); }

        char const* doc_of(std::string const& aQualname)
        {
            return socket_docs::doc((std::string{ sModule } + "." + aQualname).c_str());
        }

        using body_type = std::function<py(std::vector<py> const&)>;

        // (a function of its parameters (aVarArgs: *args, appended): its docstring is Lib/socket.py's)
        py bound_function(std::string const& aName, std::string const& aQualname, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false)
        {
            return make_native_function(aName, aQualname, doc_of(aQualname), sModule, [aQualname, aParameters, aBody, aVarArgs](std::vector<py> const& aArgs, object* aKwargs) -> py
            {
                py rest;
                auto a = py_bind(aQualname, aParameters, aArgs, aKwargs, aVarArgs ? &rest : nullptr, nullptr);
                if (aVarArgs)
                    a.push_back(rest);
                return aBody(a);
            });
        }
        // (a method: its parameters after self)
        py method(char const* aClass, char const* aName, std::vector<py_parameter> aParameters, body_type aBody, bool aVarArgs = false)
        {
            aParameters.insert(aParameters.begin(), py_positional("self"));
            return bound_function(aName, std::string{ aClass } + "." + aName, std::move(aParameters), std::move(aBody), aVarArgs);
        }
        py property(char const* aClass, char const* aName, body_type aGetter)
        {
            py const getter = method(aClass, aName, {}, std::move(aGetter));
            return builtin("property").call({ getter, none(), none(), getter.attr("__doc__") });
        }

        using namespace_type = std::vector<std::pair<char const*, py>>;

        // (a class of the module: aMetaclass (or its bases' metaclass)(aName, aBases, namespace))
        py make_class(char const* aName, std::vector<py> const& aBases, namespace_type const& aNamespace, py aMetaclass = {})
        {
            py const ns = py_dict();
            ns.set_item(S("__module__"), S(sModule));
            ns.set_item(S("__qualname__"), S(aName));
            char const* const doc = doc_of(aName);
            ns.set_item(S("__doc__"), *doc != '\0' ? S(doc) : none());
            for (auto const& [name, value] : aNamespace)
                ns.set_item(S(name), value);
            std::vector<py> const bases = aBases.empty() ? std::vector<py>{ builtin("object") } : aBases;
            py meta = aMetaclass.empty() ? builtin("type") : aMetaclass;
            for (auto const& b : bases)
            {
                py const t = type_of(b);
                if (!t.is(meta) && issubclass(t, meta))
                    meta = t;
            }
            py const result = meta(S(aName), py_tuple(bases), ns);
            state().classes[aName] = result;
            return result;
        }

        // (raise aException from None)
        [[noreturn]] void raise_from_none(py const& aException)
        {
            aException.set_attr("__cause__", none());
            aException.set_attr("__suppress_context__", boolean(true));
            py_raise(aException);
        }

        // (try: aBody() finally: aFinally())
        template <typename Body, typename Final>
        py try_finally(Body&& aBody, Final&& aFinally)
        {
            py result;
            try
            {
                result = aBody();
            }
            catch (raised const&)
            {
                py const e = py_take_exception();
                try
                {
                    aFinally();
                }
                catch (raised const&)
                {
                    py const other = py_take_exception();
                    if (other.attr("__context__").is_none() && !other.is(e))
                        other.set_attr("__context__", e);
                    py_reraise(other);
                }
                py_reraise(e);
            }
            aFinally();
            return result;
        }

        bool hasattr(py const& aObject, char const* aName) { return builtin("hasattr")(aObject, S(aName)).truthy(); }
        py len(py const& aObject) { return py{ aObject.size() }; }
        py format(char const* aFormat, std::vector<py> const& aValues) { return S(aFormat) % py_tuple(aValues); }

        // (the pending exception: taken if it is an instance of aClass (a class object); otherwise it remains raised (empty))
        py caught_instance(py const& aClass)
        {
            py const e = py_take_exception();
            if (isinstance(e, aClass))
                return e;
            py_reraise(e);
        }

        // (convert a numeric family value to an IntEnum member: if it's not a known member, return the numeric value itself
        // (_intenum_converter))
        py intenum_converter(py const& aValue, py const& aEnumClass)
        {
            try
            {
                return aEnumClass(aValue);
            }
            catch (raised const&)
            {
                if (!caught({ "ValueError" }))
                    throw;
                return aValue;
            }
        }

        // --- class socket

        void make_socket_class()
        {
            char const* const c = "socket";
            py const base = _socket("socket");
            py const os{ fn_import("os") };
            namespace_type ns = {
                { "__slots__", py_list({ S("__weakref__"), S("_io_refs"), S("_closed") }) },
                { "__init__", method(c, "__init__", { py_positional("family", py{ -1 }), py_positional("type", py{ -1 }), py_positional("proto", py{ -1 }),
                    py_positional("fileno", none()) }, [](std::vector<py> const& a)
                    {
                        // (for user code address family and type values are IntEnum members, but for the underlying _socket.socket they're just
                        // integers; the constructor of _socket.socket converts the given argument to an integer automatically)
                        py family = a[1], type = a[2], proto = a[3];
                        if (a[4].is_none())
                        {
                            if (family == py{ -1 })
                                family = global("AF_INET");
                            if (type == py{ -1 })
                                type = global("SOCK_STREAM");
                            if (proto == py{ -1 })
                                proto = py{ 0 };
                        }
                        _socket("socket").attr("__init__")(a[0], family, type, proto, a[4]);
                        a[0].set_attr("_io_refs", py{ 0 });
                        a[0].set_attr("_closed", boolean(false));
                        return none();
                    }) },
                { "__enter__", method(c, "__enter__", {}, [](std::vector<py> const& a) { return a[0]; }) },
                { "__exit__", method(c, "__exit__", {}, [](std::vector<py> const& a)
                    {
                        if (!a[0].attr("_closed").truthy())
                            a[0].method("close");
                        return none();
                    }, true) },
                { "__repr__", method(c, "__repr__", {}, [](std::vector<py> const& a)
                    {
                        // (wrap __repr__() to reveal the real class name and socket address(es))
                        py const& self = a[0];
                        bool const closed = builtin("getattr")(self, S("_closed"), boolean(false)).truthy();
                        py s = format("<%s.%s%s fd=%i, family=%s, type=%s, proto=%i", { self.attr("__class__").attr("__module__"),
                            self.attr("__class__").attr("__qualname__"), S(closed ? " [closed]" : ""), self.method("fileno"), self.attr("family"),
                            self.attr("type"), self.attr("proto") });
                        if (!closed)
                        {
                            // (getsockname and getpeername may not be available on WASI)
                            for (char const* m : { "getsockname", "getpeername" })
                            {
                                try
                                {
                                    py const addr = self.method(m);
                                    if (addr.truthy())
                                        s = s + format(std::string_view{ m } == "getsockname" ? ", laddr=%s" : ", raddr=%s", { builtin("str")(addr) });
                                }
                                catch (raised const&)
                                {
                                    if (!caught({ "OSError", "AttributeError" }))
                                        throw;
                                }
                            }
                        }
                        return s + S(">");
                    }) },
                { "__getstate__", method(c, "__getstate__", {}, [](std::vector<py> const& a) -> py
                    {
                        py_raise("TypeError", "cannot pickle " + a[0].attr("__class__").attr("__name__").repr() + " object");
                    }) },
                { "dup", method(c, "dup", {}, [](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        py const fd = global("dup")(self.method("fileno"));
                        py const sock = self.attr("__class__").call({ self.attr("family"), self.attr("type"), self.attr("proto") }, { { "fileno", fd } });
                        sock.method("settimeout", { self.method("gettimeout") });
                        return sock;
                    }) },
                { "accept", method(c, "accept", {}, [](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        auto const [fd, addr] = [&]()
                        {
                            auto const pair = self.method("_accept").items();
                            return std::pair<py, py>{ pair.at(0), pair.at(1) };
                        }();
                        py const sock = global("socket").call({ self.attr("family"), self.attr("type"), self.attr("proto") }, { { "fileno", fd } });
                        // (issue #7995: if no default timeout is set and the listening socket had a (non-zero) timeout, force the new socket in
                        // blocking mode to override platform-specific socket flags inheritance)
                        if (global("getdefaulttimeout")().is_none() && self.method("gettimeout").truthy())
                            sock.method("setblocking", { boolean(true) });
                        return py_tuple({ sock, addr });
                    }) },
                { "makefile", method(c, "makefile", { py_positional("mode", S("r")), py_positional("buffering", none()), py_keyword("encoding", none()),
                    py_keyword("errors", none()), py_keyword("newline", none()) }, [](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        py const& mode = a[1];
                        // (XXX refactor to share code?)
                        if (!builtin("set")(mode).method("issubset", { builtin("set")(S("rwb")) }).truthy())
                            py_raise("ValueError", format("invalid mode %r (only r, w, b allowed)", { mode }).str());
                        bool const writing = mode.contains(S("w"));
                        bool const reading = mode.contains(S("r")) || !writing;
                        bool const binary = mode.contains(S("b"));
                        std::string rawmode;
                        if (reading)
                            rawmode += "r";
                        if (writing)
                            rawmode += "w";
                        py const raw = global("SocketIO")(self, S(rawmode));
                        self.set_attr("_io_refs", self.attr("_io_refs") + py{ 1 });
                        py buffering = a[2].is_none() ? py{ -1 } : a[2];
                        if (buffering < py{ 0 })
                            buffering = import_attribute("io", "DEFAULT_BUFFER_SIZE");
                        if (buffering == py{ 0 })
                        {
                            if (!binary)
                                py_raise("ValueError", "unbuffered streams must be binary");
                            return raw;
                        }
                        py buffer;
                        if (reading && writing)
                            buffer = import_attribute("io", "BufferedRWPair")(raw, raw, buffering);
                        else if (reading)
                            buffer = import_attribute("io", "BufferedReader")(raw, buffering);
                        else
                            buffer = import_attribute("io", "BufferedWriter")(raw, buffering);
                        if (binary)
                            return buffer;
                        py const encoding = import_attribute("io", "text_encoding")(a[3]);
                        py const text = import_attribute("io", "TextIOWrapper")(buffer, encoding, a[4], a[5]);
                        text.set_attr("mode", mode);
                        return text;
                    }) },
                { "_sendfile_use_send", method(c, "_sendfile_use_send", { py_positional("file"), py_positional("offset", py{ 0 }), py_positional("count", none()) },
                    [](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        py const& file = a[1];
                        py const& offset = a[2];
                        py const& count = a[3];
                        self.method("_check_sendfile_params", { file, offset, count });
                        if (self.method("gettimeout") == py{ 0 })
                            py_raise("ValueError", "non-blocking sockets are not supported");
                        if (offset.truthy())
                            file.method("seek", { offset });
                        py blocksize = count.truthy() ? builtin("min")(count, py{ 8192 }) : py{ 8192 };
                        py totalSent{ 0 };
                        // (localize variable access to minimize overhead)
                        py const fileRead = file.attr("read");
                        py const sockSend = self.attr("send");
                        return try_finally([&]()
                        {
                            for (;;)
                            {
                                if (count.truthy())
                                {
                                    blocksize = builtin("min")(count - totalSent, blocksize);
                                    if (blocksize <= py{ 0 })
                                        break;
                                }
                                py data = builtin("memoryview")(fileRead(blocksize));
                                if (!data.truthy())
                                    break;  // (EOF)
                                for (;;)
                                {
                                    py sent;
                                    try
                                    {
                                        sent = sockSend(data);
                                    }
                                    catch (raised const&)
                                    {
                                        if (!caught({ "BlockingIOError" }))
                                            throw;
                                        continue;
                                    }
                                    totalSent = totalSent + sent;
                                    if (sent < len(data))
                                        data = data[builtin("slice")(sent, none())];
                                    else
                                        break;
                                }
                            }
                            return totalSent;
                        }, [&]()
                        {
                            if (totalSent > py{ 0 } && hasattr(file, "seek"))
                                file.method("seek", { offset + totalSent });
                        });
                    }) },
                { "_check_sendfile_params", method(c, "_check_sendfile_params", { py_positional("file"), py_positional("offset"), py_positional("count") },
                    [](std::vector<py> const& a)
                    {
                        if (!builtin("getattr")(a[1], S("mode"), S("b")).contains(S("b")))
                            py_raise("ValueError", "file should be opened in binary mode");
                        if (!(a[0].attr("type") & global("SOCK_STREAM")).truthy())
                            py_raise("ValueError", "only SOCK_STREAM type sockets are supported");
                        if (!a[3].is_none())
                        {
                            if (!isinstance(a[3], builtin("int")))
                                py_raise("TypeError", "count must be a positive integer (got " + a[3].repr() + ")");
                            if (a[3] <= py{ 0 })
                                py_raise("ValueError", "count must be a positive integer (got " + a[3].repr() + ")");
                        }
                        return none();
                    }) },
                { "sendfile", method(c, "sendfile", { py_positional("file"), py_positional("offset", py{ 0 }), py_positional("count", none()) },
                    [](std::vector<py> const& a)
                    {
                        try
                        {
                            return a[0].method("_sendfile_use_sendfile", { a[1], a[2], a[3] });
                        }
                        catch (raised const&)
                        {
                            caught_instance(global("_GiveupOnSendfile"));
                            return a[0].method("_sendfile_use_send", { a[1], a[2], a[3] });
                        }
                    }) },
                { "_decref_socketios", method(c, "_decref_socketios", {}, [](std::vector<py> const& a)
                    {
                        if (a[0].attr("_io_refs") > py{ 0 })
                            a[0].set_attr("_io_refs", a[0].attr("_io_refs") - py{ 1 });
                        if (a[0].attr("_closed").truthy())
                            a[0].method("close");
                        return none();
                    }) },
                // (this function should not reference any globals: see issue #808164)
                { "_real_close", method(c, "_real_close", { py_positional("_ss", base) }, [](std::vector<py> const& a)
                    {
                        a[1].attr("close")(a[0]);
                        return none();
                    }) },
                { "close", method(c, "close", {}, [](std::vector<py> const& a)
                    {
                        // (this function should not reference any globals: see issue #808164)
                        a[0].set_attr("_closed", boolean(true));
                        if (a[0].attr("_io_refs") <= py{ 0 })
                            a[0].method("_real_close");
                        return none();
                    }) },
                { "detach", method(c, "detach", {}, [](std::vector<py> const& a)
                    {
                        a[0].set_attr("_closed", boolean(true));
                        return builtin("super")(cls("socket"), a[0]).method("detach");
                    }) },
                { "family", property(c, "family", [](std::vector<py> const& a)
                    {
                        return intenum_converter(builtin("super")(cls("socket"), a[0]).attr("family"), global("AddressFamily"));
                    }) },
                { "type", property(c, "type", [](std::vector<py> const& a)
                    {
                        return intenum_converter(builtin("super")(cls("socket"), a[0]).attr("type"), global("SocketKind"));
                    }) }
            };
            if (hasattr(os, "sendfile"))
            {
                ns.emplace_back("_sendfile_use_sendfile", method(c, "_sendfile_use_sendfile", { py_positional("file"), py_positional("offset", py{ 0 }),
                    py_positional("count", none()) }, [](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        py const& file = a[1];
                        py offset = a[2];
                        py const& count = a[3];
                        self.method("_check_sendfile_params", { file, offset, count });
                        py const sockno = self.method("fileno");
                        py fileno;
                        try
                        {
                            fileno = file.method("fileno");
                        }
                        catch (raised const&)
                        {
                            py const err = py_take_exception();
                            if (!isinstance(err, builtin("AttributeError")) && !isinstance(err, import_attribute("io", "UnsupportedOperation")))
                                py_reraise(err);
                            py const giveup = global("_GiveupOnSendfile")(err);
                            giveup.set_attr("__context__", err);
                            py_raise(giveup);  // (not a regular file)
                        }
                        py fsize;
                        try
                        {
                            fsize = import_attribute("os", "fstat")(fileno).attr("st_size");
                        }
                        catch (raised const&)
                        {
                            py const err = caught_instance(builtin("OSError"));
                            py const giveup = global("_GiveupOnSendfile")(err);
                            giveup.set_attr("__context__", err);
                            py_raise(giveup);  // (not a regular file)
                        }
                        if (!fsize.truthy())
                            return py{ 0 };  // (empty file)
                        // (truncate to 1GiB to avoid OverflowError, see bpo-38319)
                        py blocksize = builtin("min")(count.truthy() ? count : fsize, py{ 1LL << 30 });
                        py const timeout = self.method("gettimeout");
                        if (timeout == py{ 0 })
                            py_raise("ValueError", "non-blocking sockets are not supported");
                        // (poll/select have the advantage of not requiring any extra file descriptor, contrarily to epoll/kqueue (also, they
                        // require a single syscall))
                        py const selectors{ fn_import("selectors") };
                        py const selector = hasattr(selectors, "PollSelector") ? selectors.attr("PollSelector")() : selectors.attr("SelectSelector")();
                        selector.method("register", { sockno, selectors.attr("EVENT_WRITE") });
                        py totalSent{ 0 };
                        // (localize variable access to minimize overhead)
                        py const selectorSelect = selector.attr("select");
                        py const osSendfile = import_attribute("os", "sendfile");
                        return try_finally([&]()
                        {
                            for (;;)
                            {
                                if (timeout.truthy() && !selectorSelect(timeout).truthy())
                                    py_raise("TimeoutError", "timed out");
                                if (count.truthy())
                                {
                                    blocksize = builtin("min")(count - totalSent, blocksize);
                                    if (blocksize <= py{ 0 })
                                        break;
                                }
                                py sent;
                                try
                                {
                                    sent = osSendfile(sockno, fileno, offset, blocksize);
                                }
                                catch (raised const&)
                                {
                                    py const err = py_take_exception();
                                    if (isinstance(err, builtin("BlockingIOError")))
                                    {
                                        if (!timeout.truthy())
                                        {
                                            // (block until the socket is ready to send some data; avoids hogging CPU resources)
                                            selectorSelect();
                                        }
                                        continue;
                                    }
                                    if (!isinstance(err, builtin("OSError")))
                                        py_reraise(err);
                                    if (totalSent == py{ 0 })
                                    {
                                        // (we can get here for different reasons, the main one being 'file' is not a regular mmap(2)-like file, in
                                        // which case we'll fall back on using plain send())
                                        py const giveup = global("_GiveupOnSendfile")(err);
                                        giveup.set_attr("__context__", err);
                                        py_raise(giveup);
                                    }
                                    raise_from_none(err);
                                }
                                if (sent == py{ 0 })
                                    break;  // (EOF)
                                offset = offset + sent;
                                totalSent = totalSent + sent;
                            }
                            return totalSent;
                        }, [&]()
                        {
                            if (totalSent > py{ 0 } && hasattr(file, "seek"))
                                file.method("seek", { offset });
                        });
                    }));
            }
            else
            {
                ns.emplace_back("_sendfile_use_sendfile", method(c, "_sendfile_use_sendfile", { py_positional("file"), py_positional("offset", py{ 0 }),
                    py_positional("count", none()) }, [](std::vector<py> const&) -> py
                    {
                        py_raise(global("_GiveupOnSendfile")(S("os.sendfile() not available on this platform")));
                    }));
            }
            bool const nt = os.attr("name") == S("nt");
            ns.emplace_back("get_inheritable", method(c, "get_inheritable", {}, [nt](std::vector<py> const& a)
            {
                return import_attribute("os", nt ? "get_handle_inheritable" : "get_inheritable")(a[0].method("fileno"));
            }));
            ns.emplace_back("set_inheritable", method(c, "set_inheritable", { py_positional("inheritable") }, [nt](std::vector<py> const& a)
            {
                import_attribute("os", nt ? "set_handle_inheritable" : "set_inheritable")(a[0].method("fileno"), a[1]);
                return none();
            }));
            py const result = make_class(c, { base }, ns);
            result.attr("get_inheritable").set_attr("__doc__", S("Get the inheritable flag of the socket"));
            result.attr("set_inheritable").set_attr("__doc__", S("Set the inheritable flag of the socket"));
        }

        // --- class SocketIO

        void make_socket_io_class()
        {
            char const* const c = "SocketIO";
            // (one might wonder why not let FileIO do the job instead; there are two main reasons why FileIO is not adapted: it wouldn't work under
            // Windows (where you can't used read() and write() on a socket handle); it wouldn't work with socket timeouts (FileIO would ignore the
            // timeout and consider the socket non-blocking))
            auto const isBlocking = [](py const& aError)
            {
                py const blockingErrnos = global("_blocking_errnos");
                return blockingErrnos.contains(aError.attr("errno"));
            };
            make_class(c, { import_attribute("io", "RawIOBase") }, {
                { "__init__", method(c, "__init__", { py_positional("sock"), py_positional("mode") }, [](std::vector<py> const& a)
                    {
                        py mode = a[2];
                        if (!py_tuple({ S("r"), S("w"), S("rw"), S("rb"), S("wb"), S("rwb") }).contains(mode))
                            py_raise("ValueError", format("invalid mode: %r", { mode }).str());
                        import_attribute("io", "RawIOBase").attr("__init__")(a[0]);
                        a[0].set_attr("_sock", a[1]);
                        if (!mode.contains(S("b")))
                            mode = mode + S("b");
                        a[0].set_attr("_mode", mode);
                        a[0].set_attr("_reading", boolean(mode.contains(S("r"))));
                        a[0].set_attr("_writing", boolean(mode.contains(S("w"))));
                        a[0].set_attr("_timeout_occurred", boolean(false));
                        return none();
                    }) },
                { "readinto", method(c, "readinto", { py_positional("b") }, [isBlocking](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        self.method("_checkClosed");
                        self.method("_checkReadable");
                        if (self.attr("_timeout_occurred").truthy())
                            py_raise("OSError", "cannot read from timed out object");
                        try
                        {
                            return self.attr("_sock").method("recv_into", { a[1] });
                        }
                        catch (raised const&)
                        {
                            py const e = py_take_exception();
                            if (isinstance(e, builtin("TimeoutError")))
                            {
                                self.set_attr("_timeout_occurred", boolean(true));
                                py_reraise(e);
                            }
                            if (isinstance(e, builtin("OSError")) && isBlocking(e))
                                return none();
                            py_reraise(e);
                        }
                    }) },
                { "write", method(c, "write", { py_positional("b") }, [isBlocking](std::vector<py> const& a)
                    {
                        py const& self = a[0];
                        self.method("_checkClosed");
                        self.method("_checkWritable");
                        try
                        {
                            return self.attr("_sock").method("send", { a[1] });
                        }
                        catch (raised const&)
                        {
                            // (XXX what about EINTR?)
                            py const e = py_take_exception();
                            if (isinstance(e, builtin("OSError")) && isBlocking(e))
                                return none();
                            py_reraise(e);
                        }
                    }) },
                { "readable", method(c, "readable", {}, [](std::vector<py> const& a)
                    {
                        if (a[0].attr("closed").truthy())
                            py_raise("ValueError", "I/O operation on closed socket.");
                        return a[0].attr("_reading");
                    }) },
                { "writable", method(c, "writable", {}, [](std::vector<py> const& a)
                    {
                        if (a[0].attr("closed").truthy())
                            py_raise("ValueError", "I/O operation on closed socket.");
                        return a[0].attr("_writing");
                    }) },
                { "seekable", method(c, "seekable", {}, [](std::vector<py> const& a)
                    {
                        if (a[0].attr("closed").truthy())
                            py_raise("ValueError", "I/O operation on closed socket.");
                        return builtin("super")(cls("SocketIO"), a[0]).method("seekable");
                    }) },
                { "fileno", method(c, "fileno", {}, [](std::vector<py> const& a)
                    {
                        a[0].method("_checkClosed");
                        return a[0].attr("_sock").method("fileno");
                    }) },
                { "name", property(c, "name", [](std::vector<py> const& a)
                    {
                        if (!a[0].attr("closed").truthy())
                            return a[0].method("fileno");
                        return py{ -1 };
                    }) },
                { "mode", property(c, "mode", [](std::vector<py> const& a) { return a[0].attr("_mode"); }) },
                { "close", method(c, "close", {}, [](std::vector<py> const& a)
                    {
                        if (a[0].attr("closed").truthy())
                            return none();
                        import_attribute("io", "RawIOBase").attr("close")(a[0]);
                        a[0].attr("_sock").method("_decref_socketios");
                        a[0].set_attr("_sock", none());
                        return none();
                    }) }
            });
        }

        // --- the module's functions

        // (the authenticated pair of connected sockets that socketpair() makes where _socket has no socketpair() (origin:
        // https://gist.github.com/4325783, by Geert Jansen; public domain) (_fallback_socketpair))
        py fallback_socketpair(std::vector<py> const& a)
        {
            py const& family = a[0];
            py const& type = a[1];
            py const& proto = a[2];
            py host;
            if (family == global("AF_INET"))
                host = global("_LOCALHOST");
            else if (family == global("AF_INET6"))
                host = global("_LOCALHOST_V6");
            else
                py_raise("ValueError", "Only AF_INET and AF_INET6 socket address families are supported");
            if (type != global("SOCK_STREAM"))
                py_raise("ValueError", "Only SOCK_STREAM socket type is supported");
            if (proto != py{ 0 })
                py_raise("ValueError", "Only protocol zero is supported");
            // (we create a connected TCP socket; note the trick with setblocking(False) that prevents us from having to create a thread)
            py const lsock = global("socket")(family, type, proto);
            py ssock, csock;
            try_finally([&]()
            {
                lsock.method("bind", { py_tuple({ host, py{ 0 } }) });
                lsock.method("listen");
                // (on IPv6, ignore flow_info and scope_id)
                auto const name = lsock.method("getsockname").items();
                py const addr = name.at(0);
                py const port = name.at(1);
                csock = global("socket")(family, type, proto);
                try
                {
                    csock.method("setblocking", { boolean(false) });
                    try
                    {
                        csock.method("connect", { py_tuple({ addr, port }) });
                    }
                    catch (raised const&)
                    {
                        if (!caught({ "BlockingIOError", "InterruptedError" }))
                            throw;
                    }
                    csock.method("setblocking", { boolean(true) });
                    ssock = lsock.method("accept").items().at(0);
                }
                catch (raised const&)
                {
                    py const e = py_take_exception();
                    csock.method("close");
                    py_reraise(e);
                }
                return none();
            }, [&]() { lsock.method("close"); });
            // (authenticating avoids using a connection from something else able to connect to {host}:{port} instead of us; we expect only
            // AF_INET and AF_INET6 families)
            try
            {
                if (ssock.method("getsockname") != csock.method("getpeername") || csock.method("getsockname") != ssock.method("getpeername"))
                    py_raise("ConnectionError", "Unexpected peer connection");
            }
            catch (raised const&)
            {
                // (getsockname() and getpeername() can fail if either socket isn't connected)
                py const e = py_take_exception();
                ssock.method("close");
                csock.method("close");
                py_reraise(e);
            }
            return py_tuple({ ssock, csock });
        }

        void add_functions(std::vector<std::pair<std::string, object*>>& aAttributes, py const& aAll)
        {
            auto const add = [&](char const* aName, py const& aValue)
            {
                for (auto& attribute : aAttributes)
                    if (attribute.first == aName)
                    {
                        release(attribute.second);
                        attribute.second = aValue.reference();
                        return;
                    }
                aAttributes.emplace_back(aName, aValue.reference());
            };
            add("_intenum_converter", bound_function("_intenum_converter", "_intenum_converter", { py_positional("value"), py_positional("enum_klass") },
                [](std::vector<py> const& a) { return intenum_converter(a[0], a[1]); }));
            add("fromfd", bound_function("fromfd", "fromfd", { py_positional("fd"), py_positional("family"), py_positional("type"), py_positional("proto", py{ 0 }) },
                [](std::vector<py> const& a)
                {
                    py const nfd = global("dup")(a[0]);
                    return global("socket")(a[1], a[2], a[3], nfd);
                }));
            py const fallback = bound_function("_fallback_socketpair", "_fallback_socketpair", { py_positional("family", global_member("AF_INET")),
                py_positional("type", global_member("SOCK_STREAM")), py_positional("proto", py{ 0 }) }, fallback_socketpair);
            add("_fallback_socketpair", fallback);
            py const socketClass = _socket("socket");
            if (hasattr(socketClass, "sendmsg"))
            {
                add("send_fds", bound_function("send_fds", "send_fds", { py_positional("sock"), py_positional("buffers"), py_positional("fds"),
                    py_positional("flags", py{ 0 }), py_positional("address", none()) }, [](std::vector<py> const& a)
                    {
                        py const fds = import_attribute("array", "array")(S("i"), a[2]);
                        return a[0].method("sendmsg", { a[1], py_list({ py_tuple({ _socket("SOL_SOCKET"), _socket("SCM_RIGHTS"), fds }) }) });
                    }));
                aAll.method("append", { S("send_fds") });
            }
            if (hasattr(socketClass, "recvmsg"))
            {
                add("recv_fds", bound_function("recv_fds", "recv_fds", { py_positional("sock"), py_positional("bufsize"), py_positional("maxfds"),
                    py_positional("flags", py{ 0 }) }, [](std::vector<py> const& a)
                    {
                        // (array of ints)
                        py const fds = import_attribute("array", "array")(S("i"));
                        py const itemsize = fds.attr("itemsize");
                        auto const result = a[0].method("recvmsg", { a[1], _socket("CMSG_LEN")(a[2] * itemsize) }).items();
                        for (auto const& cmsg : result.at(1).items())
                        {
                            auto const parts = cmsg.items();
                            if (parts.at(0) == _socket("SOL_SOCKET") && parts.at(1) == _socket("SCM_RIGHTS"))
                            {
                                py const data = parts.at(2);
                                fds.method("frombytes", { data[builtin("slice")(none(), len(data) - (len(data) % itemsize))] });
                            }
                        }
                        return py_tuple({ result.at(0), builtin("list")(fds), result.at(2), result.at(3) });
                    }));
                aAll.method("append", { S("recv_fds") });
            }
            py socketpair;
            if (hasattr(py{ fn_import("_socket") }, "socketpair"))
            {
                socketpair = bound_function("socketpair", "socketpair", { py_positional("family", none()), py_positional("type", global_member("SOCK_STREAM")),
                    py_positional("proto", py{ 0 }) }, [](std::vector<py> const& a)
                    {
                        py family = a[0];
                        if (family.is_none())
                        {
                            try
                            {
                                family = global("AF_UNIX");
                            }
                            catch (raised const&)
                            {
                                if (!caught({ "AttributeError" }))
                                    throw;
                                family = global("AF_INET");
                            }
                        }
                        auto const pair = _socket("socketpair")(family, a[1], a[2]).items();
                        py const sa = global("socket")(family, a[1], a[2], pair.at(0).method("detach"));
                        py const sb = global("socket")(family, a[1], a[2], pair.at(1).method("detach"));
                        return py_tuple({ sa, sb });
                    });
            }
            else
            {
                socketpair = fallback;
                aAll.method("append", { S("socketpair") });
            }
            add("socketpair", socketpair);
            add("getfqdn", bound_function("getfqdn", "getfqdn", { py_positional("name", S("")) }, [](std::vector<py> const& a)
            {
                py name = a[0].method("strip");
                if (!name.truthy() || py_tuple({ S("0.0.0.0"), S("::") }).contains(name))
                    name = global("gethostname")();
                std::vector<py> host;
                try
                {
                    host = global("gethostbyaddr")(name).items();
                }
                catch (raised const&)
                {
                    if (!caught({ "OSError" }))
                        throw;
                    return name;
                }
                py const hostname = host.at(0);
                py const aliases = host.at(1);
                aliases.method("insert", { py{ 0 }, hostname });
                for (auto const& n : aliases.items())
                    if (n.contains(S(".")))
                        return n;
                return hostname;
            }));
            add("create_connection", bound_function("create_connection", "create_connection", { py_positional("address"),
                py_positional("timeout", state().globalDefaultTimeout), py_positional("source_address", none()), py_keyword("all_errors", boolean(false)) },
                [](std::vector<py> const& a)
                {
                    auto const address = a[0].items();
                    if (address.size() != 2u)
                        py_raise("ValueError", address.size() < 2u ? "not enough values to unpack (expected 2, got " + std::to_string(address.size()) + ")" :
                            "too many values to unpack (expected 2)");
                    py const& timeout = a[1];
                    py const& sourceAddress = a[2];
                    bool const allErrors = a[3].truthy();
                    py const exceptions = py_list();
                    for (auto const& res : global("getaddrinfo")(address[0], address[1], py{ 0 }, global("SOCK_STREAM")).items())
                    {
                        auto const r = res.items();
                        py sock = none();
                        try
                        {
                            sock = global("socket")(r.at(0), r.at(1), r.at(2));
                            if (!timeout.is(state().globalDefaultTimeout))
                                sock.method("settimeout", { timeout });
                            if (sourceAddress.truthy())
                                sock.method("bind", { sourceAddress });
                            sock.method("connect", { r.at(4) });
                            // (break explicitly a reference cycle)
                            exceptions.method("clear");
                            return sock;
                        }
                        catch (raised const&)
                        {
                            py const exc = caught_instance(builtin("OSError"));
                            if (!allErrors)
                                exceptions.method("clear");     // (raise only the last error)
                            exceptions.method("append", { exc });
                            if (!sock.is_none())
                                sock.method("close");
                        }
                    }
                    if (exceptions.size() > 0)
                    {
                        return try_finally([&]() -> py
                        {
                            if (!allErrors)
                                py_raise(exceptions[py{ 0 }]);
                            py_raise(builtin("ExceptionGroup")(S("create_connection failed"), exceptions));
                        }, [&]()
                        {
                            // (break explicitly a reference cycle)
                            exceptions.method("clear");
                        });
                    }
                    py_raise(global("error")(S("getaddrinfo returns an empty list")));
                }));
            add("has_dualstack_ipv6", bound_function("has_dualstack_ipv6", "has_dualstack_ipv6", {}, [](std::vector<py> const&)
            {
                py const socketModule{ fn_import("_socket") };
                if (!global("has_ipv6").truthy() || !hasattr(socketModule, "IPPROTO_IPV6") || !hasattr(socketModule, "IPV6_V6ONLY"))
                    return boolean(false);
                try
                {
                    py const sock = global("socket")(global("AF_INET6"), global("SOCK_STREAM")).method("__enter__");
                    return try_finally([&]()
                    {
                        sock.method("setsockopt", { global("IPPROTO_IPV6"), global("IPV6_V6ONLY"), py{ 0 } });
                        // (on some platforms (e.g. DragonFly BSD) setting IPV6_V6ONLY to 0 silently has no effect, so check that it was actually
                        // cleared)
                        return boolean(sock.method("getsockopt", { global("IPPROTO_IPV6"), global("IPV6_V6ONLY") }) == py{ 0 });
                    }, [&]() { sock.method("__exit__", { none(), none(), none() }); });
                }
                catch (raised const&)
                {
                    if (!caught({ "OSError" }))
                        throw;
                    return boolean(false);
                }
            }));
            add("create_server", bound_function("create_server", "create_server", { py_positional("address"), py_keyword("family", global_member("AF_INET")),
                py_keyword("backlog", none()), py_keyword("reuse_port", boolean(false)), py_keyword("dualstack_ipv6", boolean(false)) },
                [](std::vector<py> const& a)
                {
                    py const& address = a[0];
                    py const& family = a[1];
                    py const& backlog = a[2];
                    bool const reusePort = a[3].truthy();
                    bool const dualstack = a[4].truthy();
                    py const socketModule{ fn_import("_socket") };
                    if (reusePort && !hasattr(socketModule, "SO_REUSEPORT"))
                        py_raise("ValueError", "SO_REUSEPORT not supported on this platform");
                    if (dualstack)
                    {
                        if (!global("has_dualstack_ipv6")().truthy())
                            py_raise("ValueError", "dualstack_ipv6 not supported on this platform");
                        if (family != global("AF_INET6"))
                            py_raise("ValueError", "dualstack_ipv6 requires AF_INET6 family");
                    }
                    py const sock = global("socket")(family, global("SOCK_STREAM"));
                    try
                    {
                        // (note about Windows: we don't set SO_REUSEADDR because: 1) it's unnecessary: bind() will succeed even in case of a previous
                        // closed socket on the same address and still in TIME_WAIT state; 2) if set, another socket is free to bind() on the same address,
                        // effectively preventing this one from accepting connections; also, it may set the process in a state where it'll no longer
                        // respond to any signals or graceful kills (see:
                        // https://learn.microsoft.com/windows/win32/winsock/using-so-reuseaddr-and-so-exclusiveaddruse))
                        py const osName = import_attribute("os", "name");
                        if (osName != S("nt") && osName != S("cygwin") && hasattr(socketModule, "SO_REUSEADDR"))
                        {
                            try
                            {
                                sock.method("setsockopt", { global("SOL_SOCKET"), global("SO_REUSEADDR"), py{ 1 } });
                            }
                            catch (raised const&)
                            {
                                // (fail later on bind(), for platforms which may not support this option)
                                if (!caught({ "OSError" }))
                                    throw;
                            }
                        }
                        // (since Linux 6.12.9, SO_REUSEPORT is not allowed on other address families than AF_INET/AF_INET6)
                        if (reusePort && (family == global("AF_INET") || family == global("AF_INET6")))
                            sock.method("setsockopt", { global("SOL_SOCKET"), global("SO_REUSEPORT"), py{ 1 } });
                        if (global("has_ipv6").truthy() && family == global("AF_INET6"))
                        {
                            if (dualstack)
                                sock.method("setsockopt", { global("IPPROTO_IPV6"), global("IPV6_V6ONLY"), py{ 0 } });
                            else if (hasattr(socketModule, "IPV6_V6ONLY") && hasattr(socketModule, "IPPROTO_IPV6"))
                                sock.method("setsockopt", { global("IPPROTO_IPV6"), global("IPV6_V6ONLY"), py{ 1 } });
                        }
                        try
                        {
                            sock.method("bind", { address });
                        }
                        catch (raised const&)
                        {
                            py const err = caught_instance(builtin("OSError"));
                            py const msg = format("%s (while attempting to bind on address %r)", { err.attr("strerror"), address });
                            raise_from_none(global("error")(err.attr("errno"), msg));
                        }
                        if (backlog.is_none())
                            sock.method("listen");
                        else
                            sock.method("listen", { backlog });
                        return sock;
                    }
                    catch (raised const&)
                    {
                        py const e = caught_instance(builtin("OSError"));
                        sock.method("close");
                        py_reraise(e);
                    }
                }));
            add("getaddrinfo", bound_function("getaddrinfo", "getaddrinfo", { py_positional("host"), py_positional("port"), py_positional("family", py{ 0 }),
                py_positional("type", py{ 0 }), py_positional("proto", py{ 0 }), py_positional("flags", py{ 0 }) }, [](std::vector<py> const& a)
                {
                    // (we override this function since we want to translate the numeric family and socket type values to enum constants)
                    py const addrlist = py_list();
                    for (auto const& res : _socket("getaddrinfo")(a[0], a[1], a[2], a[3], a[4], a[5]).items())
                    {
                        auto const r = res.items();
                        addrlist.method("append", { py_tuple({ intenum_converter(r.at(0), global("AddressFamily")), intenum_converter(r.at(1), global("SocketKind")),
                            r.at(2), r.at(3), r.at(4) }) });
                    }
                    return addrlist;
                }));
            socketpair.set_attr("__doc__", S("socketpair([family[, type[, proto]]]) -> (socket object, socket object)\n"
                "Create a pair of socket objects from the sockets returned by the platform\nsocketpair() function.\n"
                "The arguments are the same as for socket() except the default family is AF_UNIX\nif defined on the platform; otherwise, the default is AF_INET.\n"));
        }

        // (an enum of the constants whose names are filtered (IntEnum._convert_(name, 'socket', filter)): their members, sorted by value (then
        // name))
        py convert_constants(char const* aEnum, char const* aKind, std::vector<std::pair<std::string, py>> const& aConstants, char const* aPrefix)
        {
            std::vector<std::pair<std::string, py>> constants;
            for (auto const& [name, value] : aConstants)
            {
                bool upper = !name.empty();
                bool hasCased = false;
                for (char ch : name)
                {
                    if (ch >= 'a' && ch <= 'z')
                        upper = false;
                    if (ch >= 'A' && ch <= 'Z')
                        hasCased = true;
                }
                if (upper && hasCased && name.starts_with(aPrefix))
                    constants.emplace_back(name, value);
            }
            std::stable_sort(constants.begin(), constants.end(), [](auto const& aLhs, auto const& aRhs)
            {
                if (aLhs.second < aRhs.second)
                    return true;
                if (aRhs.second < aLhs.second)
                    return false;
                return aLhs.first < aRhs.first;
            });
            std::vector<py> pairs;
            for (auto const& [name, value] : constants)
                pairs.push_back(py_tuple({ S(name), value }));
            return import_attribute("enum", aKind).call({ S(aEnum), py_list(pairs) }, { { "module", S(sModule) }, { "boundary", import_attribute("enum", "KEEP") } });
        }

        bool socket_py_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            ref const result{ py_guarded([&]() -> py
            {
                auto& s = state();
                auto& m = members();
                m.clear();
                // (a global: a name bound again is rebound (e.g. socket, from _socket, then the class socket))
                auto const add = [&](std::string const& aName, py const& aValue)
                {
                    for (auto& attribute : aAttributes)
                        if (attribute.first == aName)
                        {
                            release(attribute.second);
                            attribute.second = aValue.reference();
                            m[aName] = aValue;
                            return;
                        }
                    aAttributes.emplace_back(aName, aValue.reference());
                    m[aName] = aValue;
                };
                py const socketModule{ fn_import("_socket") };
                add("__doc__", S(socket_docs::doc("socket")));
                add("_socket", socketModule);
                // (from _socket import *)
                std::vector<std::pair<std::string, py>> constants;
                py const names = builtin("dir")(socketModule);
                std::vector<py> exports;
                for (auto const& n : names.items())
                {
                    std::string const name = n.str();
                    if (name.starts_with("_"))
                        continue;
                    py const value = socketModule.attr(name.c_str());
                    add(name, value);
                    exports.push_back(n);
                    if (is_int_like(value.get()))
                        constants.emplace_back(name, value);
                }
                py const os{ fn_import("os") };
                add("os", os);
                add("sys", py{ fn_import("sys") });
                add("io", py{ fn_import("io") });
                add("selectors", py{ fn_import("selectors") });
                if (hasattr(py{ fn_import("_socket") }.attr("socket"), "sendmsg") || hasattr(py{ fn_import("_socket") }.attr("socket"), "recvmsg"))
                    add("array", py{ fn_import("array") });
                add("IntEnum", import_attribute("enum", "IntEnum"));
                add("IntFlag", import_attribute("enum", "IntFlag"));
                py const errnoModule{ fn_import("errno") };
                add("errno", errnoModule);
                add("EBADF", builtin("getattr")(errnoModule, S("EBADF"), py{ 9 }));
                add("EAGAIN", builtin("getattr")(errnoModule, S("EAGAIN"), py{ 11 }));
                add("EWOULDBLOCK", builtin("getattr")(errnoModule, S("EWOULDBLOCK"), py{ 11 }));
                py const all = py_list({ S("fromfd"), S("getfqdn"), S("create_connection"), S("create_server"), S("has_dualstack_ipv6"), S("AddressFamily"),
                    S("SocketKind") });
                // (os._get_exports_list(_socket): its names that are not private)
                all.method("extend", { py_list(exports) });
                // (set up the socket.AF_* socket.SOCK_* constants as members of IntEnums for nicer string representations; note that _socket only
                // knows about the integer values: the public interface in this module understands the enums and translates them back from
                // integers where needed (e.g. .family property of a socket object))
                for (auto const& [enumName, kind, prefix] : { std::tuple{ "AddressFamily", "IntEnum", "AF_" }, std::tuple{ "SocketKind", "IntEnum", "SOCK_" },
                    std::tuple{ "MsgFlag", "IntFlag", "MSG_" }, std::tuple{ "AddressInfo", "IntFlag", "AI_" } })
                {
                    py const e = convert_constants(enumName, kind, constants, prefix);
                    // (sys.modules[cls.__module__].__dict__.update(cls.__members__))
                    for (auto const& item : e.attr("__members__").method("items").items())
                    {
                        auto const pair = item.items();
                        py const& member = pair.at(1);
                        std::string const n = pair.at(0).str();
                        for (auto& attribute : aAttributes)
                            if (attribute.first == n)
                            {
                                release(attribute.second);
                                attribute.second = member.reference();
                            }
                        m[n] = member;
                    }
                    add(enumName, e);
                }
                add("_LOCALHOST", S("127.0.0.1"));
                add("_LOCALHOST_V6", S("::1"));
                // (WSA error codes)
                py const platform = import_attribute("sys", "platform");
                if (platform.method("lower").method("startswith", { S("win") }).truthy())
                {
                    static std::pair<int, char const*> const sErrors[] = {
                        { 6, "Specified event object handle is invalid." }, { 8, "Insufficient memory available." }, { 87, "One or more parameters are invalid." },
                        { 995, "Overlapped operation aborted." }, { 996, "Overlapped I/O event object not in signaled state." },
                        { 997, "Overlapped operation will complete later." }, { 10004, "The operation was interrupted." }, { 10009, "A bad file handle was passed." },
                        { 10013, "Permission denied." }, { 10014, "A fault occurred on the network??" }, { 10022, "An invalid operation was attempted." },
                        { 10024, "Too many open files." }, { 10035, "The socket operation would block." }, { 10036, "A blocking operation is already in progress." },
                        { 10037, "Operation already in progress." }, { 10038, "Socket operation on nonsocket." }, { 10039, "Destination address required." },
                        { 10040, "Message too long." }, { 10041, "Protocol wrong type for socket." }, { 10042, "Bad protocol option." },
                        { 10043, "Protocol not supported." }, { 10044, "Socket type not supported." }, { 10045, "Operation not supported." },
                        { 10046, "Protocol family not supported." }, { 10047, "Address family not supported by protocol family." },
                        { 10048, "The network address is in use." }, { 10049, "Cannot assign requested address." }, { 10050, "Network is down." },
                        { 10051, "Network is unreachable." }, { 10052, "Network dropped connection on reset." }, { 10053, "Software caused connection abort." },
                        { 10054, "The connection has been reset." }, { 10055, "No buffer space available." }, { 10056, "Socket is already connected." },
                        { 10057, "Socket is not connected." }, { 10058, "The network has been shut down." }, { 10059, "Too many references." },
                        { 10060, "The operation timed out." }, { 10061, "Connection refused." }, { 10062, "Cannot translate name." },
                        { 10063, "The name is too long." }, { 10064, "The host is down." }, { 10065, "The host is unreachable." },
                        { 10066, "Directory not empty." }, { 10067, "Too many processes." }, { 10068, "User quota exceeded." }, { 10069, "Disk quota exceeded." },
                        { 10070, "Stale file handle reference." }, { 10071, "Item is remote." }, { 10091, "Network subsystem is unavailable." },
                        { 10092, "Winsock.dll version out of range." }, { 10093, "Successful WSAStartup not yet performed." },
                        { 10101, "Graceful shutdown in progress." }, { 10102, "No more results from WSALookupServiceNext." }, { 10103, "Call has been canceled." },
                        { 10104, "Procedure call table is invalid." }, { 10105, "Service provider is invalid." },
                        { 10106, "Service provider failed to initialize." }, { 10107, "System call failure." }, { 10108, "Service not found." },
                        { 10109, "Class type not found." }, { 10110, "No more results from WSALookupServiceNext." }, { 10111, "Call was canceled." },
                        { 10112, "Database query was refused." }, { 11001, "Host not found." }, { 11002, "Nonauthoritative host not found." },
                        { 11003, "This is a nonrecoverable error." }, { 11004, "Valid name, no data record requested type." }, { 11005, "QoS receivers." },
                        { 11006, "QoS senders." }, { 11007, "No QoS senders." }, { 11008, "QoS no receivers." }, { 11009, "QoS request confirmed." },
                        { 11010, "QoS admission error." }, { 11011, "QoS policy failure." }, { 11012, "QoS bad style." }, { 11013, "QoS bad object." },
                        { 11014, "QoS traffic control error." }, { 11015, "QoS generic error." }, { 11016, "QoS service type error." },
                        { 11017, "QoS flowspec error." }, { 11018, "Invalid QoS provider buffer." }, { 11019, "Invalid QoS filter style." },
                        { 11020, "Invalid QoS filter style." }, { 11021, "Incorrect QoS filter count." }, { 11022, "Invalid QoS object length." },
                        { 11023, "Incorrect QoS flow count." }, { 11024, "Unrecognized QoS object." }, { 11025, "Invalid QoS policy object." },
                        { 11026, "Invalid QoS flow descriptor." }, { 11027, "Invalid QoS provider-specific flowspec." },
                        { 11028, "Invalid QoS provider-specific filterspec." }, { 11029, "Invalid QoS shape discard mode object." },
                        { 11030, "Invalid QoS shaping rate object." }, { 11031, "Reserved policy QoS element type." } };
                    py const errorTab = py_dict();
                    for (auto const& [code, message] : sErrors)
                        errorTab.set_item(py{ code }, S(message));
                    add("errorTab", errorTab);
                    all.method("append", { S("errorTab") });
                }
                if (!s.made)
                {
                    s.globalDefaultTimeout = builtin("object")();
                    py const ns = py_dict();
                    ns.set_item(S("__module__"), S(sModule));
                    ns.set_item(S("__qualname__"), S("_GiveupOnSendfile"));
                    s.classes["_GiveupOnSendfile"] = builtin("type")(S("_GiveupOnSendfile"), py_tuple({ builtin("Exception") }), ns);
                    make_socket_class();
                    make_socket_io_class();
                    s.made = true;
                }
                add("_GiveupOnSendfile", cls("_GiveupOnSendfile"));
                add("socket", cls("socket"));
                add("SocketIO", cls("SocketIO"));
                add("_GLOBAL_DEFAULT_TIMEOUT", s.globalDefaultTimeout);
                add("_blocking_errnos", builtin("set")(py_list({ global_member("EAGAIN"), global_member("EWOULDBLOCK") })));
                add_functions(aAttributes, all);
                for (auto const& [name, value] : aAttributes)
                    if (m.find(name) == m.end())
                        m[name] = py::borrowed(value);
                add("__all__", all);
                m.clear();
                return py::none();
            }) };
            return result.get() != nullptr;
        }

        inline bool const sSocketPy = register_module("socket", socket_py_module);
    }
}
