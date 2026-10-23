/*
  ftplib.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/ftplib.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module ftplib (CPython 3.13's Lib/ftplib.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "ftplib_docs.hpp"

namespace neos::python_runtime::detail::ftplib_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "ftplib.name"))
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
                py_raise("SystemError", std::string{ "ftplib: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"ftplib.name">()))
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
            if (modules_imported("ftplib"))
                return py{ fn_import("ftplib") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "ftplib" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "ftplib", ftplib_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- ftplib (Lib/ftplib.py)

        void make_ftplib()
        {
            static module_writer const& w = writer("ftplib");
            // 
            // Changes and improvements suggested by Steve Majewski.
            // Modified by Jack to work on the mac.
            // Modified by Siebren to support docstrings and PASV.
            // Modified by Phil Schwartz to add storbinary and storlines callbacks.
            // Modified by Giampaolo Rodola' to add TLS support.
            // 
            define("ftplib", "__all__", py_list({ S<"FTP">(), S<"error_reply">(), S<"error_temp">(), S<"error_perm">(), S<"error_proto">(), S<"all_errors">() }));
            // Magic number from <socket.h>
            define("ftplib", "MSG_OOB", I<1>());
            // The standard FTP server control port
            define("ftplib", "FTP_PORT", I<21>());
            // The sizehint parameter passed to readline() calls
            define("ftplib", "MAXLINE", I<8192>());
            // Exception raised when an error or invalid response is received
            define("ftplib", "Error", w.make_class("Error", { B<"Exception">() }, {
 }));

            define("ftplib", "error_reply", w.make_class("error_reply", { G<"ftplib.Error">() }, {
 }));

            define("ftplib", "error_temp", w.make_class("error_temp", { G<"ftplib.Error">() }, {
 }));

            define("ftplib", "error_perm", w.make_class("error_perm", { G<"ftplib.Error">() }, {
 }));

            define("ftplib", "error_proto", w.make_class("error_proto", { G<"ftplib.Error">() }, {
 }));

            // All exceptions (hopefully) that may be raised here and that aren't
            // (always) programming errors on our side
            define("ftplib", "all_errors", py_tuple({ G<"ftplib.Error">(), B<"OSError">(), B<"EOFError">() }));
            // Line terminators (we always output CRLF, but accept any of CRLF, CR, LF)
            define("ftplib", "CRLF", S<"\r\n">());
            define("ftplib", "B_CRLF", py{ make_bytes(std::string{ "\x0d\x0a", 2 }) });
            // The class itself
                // Disables https://bugs.python.org/issue43285 security if set to True.
                // Context management protocol: try to quit() if active
                // Internal: "sanitize" a string for printing
                // Internal: send one line to the server, appending CRLF
                // Internal: send one command to the server (through putline())
                // Internal: return one line from the server, stripping CRLF.
                // Raise EOFError if the connection is closed
                // Internal: get a response from the server, which may possibly
                // consist of multiple lines.  Return a single string with no
                // trailing CRLF.  If the response consists of multiple lines,
                // these are separated by '\n' characters in the string
                // Internal: get a response from the server.
                // Raise various errors if the response indicates an error
            define("ftplib", "FTP", w.make_class("FTP", {  }, {
                { "debugging", I<0>() },
                { "host", S<"">() },
                { "port", G<"ftplib.FTP_PORT">() },
                { "maxline", G<"ftplib.MAXLINE">() },
                { "sock", none() },
                { "file", none() },
                { "welcome", none() },
                { "passiveserver", boolean(true) },
                { "trust_server_pasv_ipv4_address", boolean(false) },
                { "__init__", w.method("FTP", "__init__", { py_positional("host", S<"">()), py_positional("user", S<"">()), py_positional("passwd", S<"">()), py_positional("acct", S<"">()), py_positional("timeout", Mod<"socket">().attr("_GLOBAL_DEFAULT_TIMEOUT")), py_positional("source_address", none()), py_keyword("encoding", S<"utf-8">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py user = a[2];
                        py passwd = a[3];
                        py acct = a[4];
                        py timeout = a[5];
                        py source_address = a[6];
                        py encoding = a[7];
                        self.set_attr("encoding", encoding);
                        self.set_attr("source_address", source_address);
                        self.set_attr("timeout", timeout);
                        if (host.truthy())
                        {
                            self.method("connect", { host });
                            if (user.truthy())
                            {
                                self.method("login", { user, passwd, acct });
                            }
                        }
                        return none();
                    }) },
                { "__enter__", w.method("FTP", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__exit__", w.method("FTP", "__exit__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        if (!self.attr("sock").is_none())
                        {
                            {
                                std::optional<py> pending1;
                                [[maybe_unused]] int flow1 = 0;
                                [[maybe_unused]] py result1;
                                try
                                {
                                    try
                                    {
                                        self.method("quit");
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler1;
                                        if (matches(handler1.exception, py_tuple({ B<"OSError">(), B<"EOFError">() })))
                                        {
                                        }
                                        else
                                            py_reraise(handler1.exception);
                                    }
                                }
                                catch (raised const&)
                                {
                                    pending1 = py_take_exception();
                                }
                                finally1:
                                if (!self.attr("sock").is_none())
                                {
                                    self.method("close");
                                }
                                if (pending1)
                                    py_reraise(*pending1);
                                if (flow1 == 1)
                                    return result1;
                            }
                        }
                        return none();
                    }, true) },
                { "connect", w.method("FTP", "connect", { py_positional("host", S<"">()), py_positional("port", I<0>()), py_positional("timeout", I<-999>()), py_positional("source_address", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py port = a[2];
                        py timeout = a[3];
                        py source_address = a[4];
                        if (host != (S<"">()))
                        {
                            self.set_attr("host", host);
                        }
                        if (port > (I<0>()))
                        {
                            self.set_attr("port", port);
                        }
                        if (timeout != (I<-999>()))
                        {
                            self.set_attr("timeout", timeout);
                        }
                        if (((!self.attr("timeout").is_none()) && (!self.attr("timeout").truthy())))
                        {
                            py_raise((B<"ValueError">())(S<"Non-blocking socket (timeout=0) is not supported">()));
                        }
                        if (!source_address.is_none())
                        {
                            self.set_attr("source_address", source_address);
                        }
                        (Mod<"sys">().attr("audit"))(S<"ftplib.connect">(), self, self.attr("host"), self.attr("port"));
                        self.set_attr("sock", (Mod<"socket">().attr("create_connection")).call({ py_tuple({ self.attr("host"), self.attr("port") }), self.attr("timeout") }, { { "source_address", self.attr("source_address") } }));
                        self.set_attr("af", self.attr("sock").attr("family"));
                        self.set_attr("file", self.attr("sock").method("makefile", { S<"r">() }, { { "encoding", self.attr("encoding") } }));
                        self.set_attr("welcome", self.method("getresp"));
                        return self.attr("welcome");
                    }) },
                { "getwelcome", w.method("FTP", "getwelcome", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if (self.attr("debugging").truthy())
                        {
                            (B<"print">())(S<"*welcome*">(), self.method("sanitize", { self.attr("welcome") }));
                        }
                        return self.attr("welcome");
                    }) },
                { "set_debuglevel", w.method("FTP", "set_debuglevel", { py_positional("level") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py level = a[1];
                        self.set_attr("debugging", level);
                        return none();
                    }) },
                { "debug", w.method("FTP", "set_debuglevel", { py_positional("level") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py level = a[1];
                        self.set_attr("debugging", level);
                        return none();
                    }) },
                { "set_pasv", w.method("FTP", "set_pasv", { py_positional("val") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py val = a[1];
                        self.set_attr("passiveserver", val);
                        return none();
                    }) },
                { "sanitize", w.method("FTP", "sanitize", { py_positional("s") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s = a[1];
                        py i;
                        if (set_of({ S<"pass ">(), S<"PASS ">() }).contains(s[B<"slice">()(none(), I<5>(), none())]))
                        {
                            i = py{ s.method("rstrip", { S<"\r\n">() }).size() };
                            s = (s[B<"slice">()(none(), I<5>(), none())] + ((S<"*">()) * (i - (I<5>())))) + s[B<"slice">()(i, none(), none())];
                        }
                        return S(s.repr());
                    }) },
                { "putline", w.method("FTP", "putline", { py_positional("line") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line = a[1];
                        if ((line.contains((S<"\r">())) || line.contains((S<"\n">()))))
                        {
                            py_raise((B<"ValueError">())(S<"an illegal newline character should not be contained">()));
                        }
                        (Mod<"sys">().attr("audit"))(S<"ftplib.sendcmd">(), self, line);
                        line = line + (G<"ftplib.CRLF">());
                        if (self.attr("debugging") > (I<1>()))
                        {
                            (B<"print">())(S<"*put*">(), self.method("sanitize", { line }));
                        }
                        self.attr("sock").method("sendall", { line.method("encode", { self.attr("encoding") }) });
                        return none();
                    }) },
                { "putcmd", w.method("FTP", "putcmd", { py_positional("line") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line = a[1];
                        if (self.attr("debugging").truthy())
                        {
                            (B<"print">())(S<"*cmd*">(), self.method("sanitize", { line }));
                        }
                        self.method("putline", { line });
                        return none();
                    }) },
                { "getline", w.method("FTP", "getline", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line;
                        line = self.attr("file").method("readline", { self.attr("maxline") + (I<1>()) });
                        if (py{ line.size() } > self.attr("maxline"))
                        {
                            py_raise((G<"ftplib.Error">())((S<"got more than %d bytes">()) % self.attr("maxline")));
                        }
                        if (self.attr("debugging") > (I<1>()))
                        {
                            (B<"print">())(S<"*get*">(), self.method("sanitize", { line }));
                        }
                        if (!line.truthy())
                        {
                            py_raise(B<"EOFError">());
                        }
                        if (line[B<"slice">()(I<-2>(), none(), none())] == (G<"ftplib.CRLF">()))
                        {
                            line = line[B<"slice">()(none(), I<-2>(), none())];
                        }
                        else if ((G<"ftplib.CRLF">()).contains(line[B<"slice">()(I<-1>(), none(), none())]))
                        {
                            line = line[B<"slice">()(none(), I<-1>(), none())];
                        }
                        return line;
                    }) },
                { "getmultiline", w.method("FTP", "getmultiline", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py code;
                        py line;
                        py nextline;
                        line = self.method("getline");
                        if (line[B<"slice">()(I<3>(), I<4>(), none())] == (S<"-">()))
                        {
                            code = line[B<"slice">()(none(), I<3>(), none())];
                            while ((I<1>()).truthy())
                            {
                                nextline = self.method("getline");
                                line = line + ((S<"\n">()) + nextline);
                                if (((nextline[B<"slice">()(none(), I<3>(), none())] == code) && (nextline[B<"slice">()(I<3>(), I<4>(), none())] != (S<"-">()))))
                                {
                                    break;
                                }
                            }
                        }
                        return line;
                    }) },
                { "getresp", w.method("FTP", "getresp", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py c;
                        py resp;
                        resp = self.method("getmultiline");
                        if (self.attr("debugging").truthy())
                        {
                            (B<"print">())(S<"*resp*">(), self.method("sanitize", { resp }));
                        }
                        self.set_attr("lastresp", resp[B<"slice">()(none(), I<3>(), none())]);
                        c = resp[B<"slice">()(none(), I<1>(), none())];
                        if (set_of({ S<"1">(), S<"2">(), S<"3">() }).contains(c))
                        {
                            return resp;
                        }
                        if (c == (S<"4">()))
                        {
                            py_raise((G<"ftplib.error_temp">())(resp));
                        }
                        if (c == (S<"5">()))
                        {
                            py_raise((G<"ftplib.error_perm">())(resp));
                        }
                        py_raise((G<"ftplib.error_proto">())(resp));
                    }) },
                { "voidresp", w.method("FTP", "voidresp", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py resp;
                        resp = self.method("getresp");
                        if (resp[B<"slice">()(none(), I<1>(), none())] != (S<"2">()))
                        {
                            py_raise((G<"ftplib.error_reply">())(resp));
                        }
                        return resp;
                    }) },
                { "abort", w.method("FTP", "abort", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line;
                        py resp;
                        line = py{ make_bytes(std::string{ "\x41\x42\x4f\x52", 4 }) } + (G<"ftplib.B_CRLF">());
                        if (self.attr("debugging") > (I<1>()))
                        {
                            (B<"print">())(S<"*put urgent*">(), self.method("sanitize", { line }));
                        }
                        self.attr("sock").method("sendall", { line, G<"ftplib.MSG_OOB">() });
                        resp = self.method("getmultiline");
                        if (!set_of({ S<"426">(), S<"225">(), S<"226">() }).contains(resp[B<"slice">()(none(), I<3>(), none())]))
                        {
                            py_raise((G<"ftplib.error_proto">())(resp));
                        }
                        return resp;
                    }) },
                { "sendcmd", w.method("FTP", "sendcmd", { py_positional("cmd") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cmd = a[1];
                        self.method("putcmd", { cmd });
                        return self.method("getresp");
                    }) },
                { "voidcmd", w.method("FTP", "voidcmd", { py_positional("cmd") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cmd = a[1];
                        self.method("putcmd", { cmd });
                        return self.method("voidresp");
                    }) },
                { "sendport", w.method("FTP", "sendport", { py_positional("host"), py_positional("port") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py port = a[2];
                        py bytes;
                        py cmd;
                        py hbytes;
                        py pbytes;
                        hbytes = host.method("split", { S<".">() });
                        pbytes = py_list({ S(floordiv(port, (I<256>())).repr()), S((port % (I<256>())).repr()) });
                        bytes = hbytes + pbytes;
                        cmd = (S<"PORT ">()) + (S<",">()).method("join", { bytes });
                        return self.method("voidcmd", { cmd });
                    }) },
                { "sendeprt", w.method("FTP", "sendeprt", { py_positional("host"), py_positional("port") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py port = a[2];
                        py af;
                        py cmd;
                        py fields;
                        af = I<0>();
                        if (self.attr("af") == (Mod<"socket">().attr("AF_INET")))
                        {
                            af = I<1>();
                        }
                        if (self.attr("af") == (Mod<"socket">().attr("AF_INET6")))
                        {
                            af = I<2>();
                        }
                        if (af == (I<0>()))
                        {
                            py_raise((G<"ftplib.error_proto">())(S<"unsupported address family">()));
                        }
                        fields = py_list({ S<"">(), S(af.repr()), host, S(port.repr()), S<"">() });
                        cmd = (S<"EPRT ">()) + (S<"|">()).method("join", { fields });
                        return self.method("voidcmd", { cmd });
                    }) },
                { "makeport", w.method("FTP", "makeport", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host;
                        py port;
                        py resp;
                        py sock;
                        sock = (Mod<"socket">().attr("create_server")).call({ py_tuple({ S<"">(), I<0>() }) }, { { "family", self.attr("af") }, { "backlog", I<1>() } });
                        port = sock.method("getsockname")[I<1>()];
                        host = self.attr("sock").method("getsockname")[I<0>()];
                        if (self.attr("af") == (Mod<"socket">().attr("AF_INET")))
                        {
                            resp = self.method("sendport", { host, port });
                        }
                        else
                        {
                            resp = self.method("sendeprt", { host, port });
                        }
                        if (!self.attr("timeout").is((Mod<"socket">().attr("_GLOBAL_DEFAULT_TIMEOUT"))))
                        {
                            sock.method("settimeout", { self.attr("timeout") });
                        }
                        return sock;
                    }) },
                { "makepasv", w.method("FTP", "makepasv", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host;
                        py port;
                        py untrusted_host;
                        if (self.attr("af") == (Mod<"socket">().attr("AF_INET")))
                        {
                            auto const items3 = unpack((G<"ftplib.parse227">())(self.method("sendcmd", { S<"PASV">() })), 2);
                            untrusted_host = items3[0];
                            port = items3[1];
                            if (self.attr("trust_server_pasv_ipv4_address").truthy())
                            {
                                host = untrusted_host;
                            }
                            else
                            {
                                host = self.attr("sock").method("getpeername")[I<0>()];
                            }
                        }
                        else
                        {
                            auto const items4 = unpack((G<"ftplib.parse229">())(self.method("sendcmd", { S<"EPSV">() }), self.attr("sock").method("getpeername")), 2);
                            host = items4[0];
                            port = items4[1];
                        }
                        return py_tuple({ host, port });
                    }) },
                { "ntransfercmd", w.method("FTP", "ntransfercmd", { py_positional("cmd"), py_positional("rest", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cmd = a[1];
                        py rest = a[2];
                        py conn;
                        py host;
                        py port;
                        py resp;
                        py size;
                        py sock;
                        py sockaddr;
                        size = none();
                        if (self.attr("passiveserver").truthy())
                        {
                            auto const items5 = unpack(self.method("makepasv"), 2);
                            host = items5[0];
                            port = items5[1];
                            conn = (Mod<"socket">().attr("create_connection")).call({ py_tuple({ host, port }), self.attr("timeout") }, { { "source_address", self.attr("source_address") } });
                            try
                            {
                                if (!rest.is_none())
                                {
                                    self.method("sendcmd", { (S<"REST %s">()) % rest });
                                }
                                resp = self.method("sendcmd", { cmd });
                                // Some servers apparently send a 200 reply to
                                // a LIST or STOR command, before the 150 reply
                                // (and way before the 226 reply). This seems to
                                // be in violation of the protocol (which only allows
                                // 1xx or error messages for LIST), so we just discard
                                // this response.
                                if (resp[I<0>()] == (S<"2">()))
                                {
                                    resp = self.method("getresp");
                                }
                                if (resp[I<0>()] != (S<"1">()))
                                {
                                    py_raise((G<"ftplib.error_reply">())(resp));
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler6;
                                {
                                    conn.method("close");
                                    py_reraise(handler6.exception);
                                }
                            }
                        }
                        else
                        {
                            {
                                py const manager7 = self.method("makeport");
                                py const exit7 = pyclass::special_method(manager7, "__exit__");
                                py const value7 = pyclass::special_method(manager7, "__enter__")(manager7);
                                sock = value7;
                                [[maybe_unused]] int flow7 = 0;
                                [[maybe_unused]] py result7;
                                bool exited7 = false;
                                try
                                {
                                    if (!rest.is_none())
                                    {
                                        self.method("sendcmd", { (S<"REST %s">()) % rest });
                                    }
                                    resp = self.method("sendcmd", { cmd });
                                    // See above.
                                    if (resp[I<0>()] == (S<"2">()))
                                    {
                                        resp = self.method("getresp");
                                    }
                                    if (resp[I<0>()] != (S<"1">()))
                                    {
                                        py_raise((G<"ftplib.error_reply">())(resp));
                                    }
                                    auto const items8 = unpack(sock.method("accept"), 2);
                                    conn = items8[0];
                                    sockaddr = items8[1];
                                    if (!self.attr("timeout").is((Mod<"socket">().attr("_GLOBAL_DEFAULT_TIMEOUT"))))
                                    {
                                        conn.method("settimeout", { self.attr("timeout") });
                                    }
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler7;
                                    exited7 = true;
                                    if (!exit7(manager7, type_of(handler7.exception), handler7.exception, handler7.exception.attr("__traceback__")).truthy())
                                        py_reraise(handler7.exception);
                                }
                                finally7:
                                if (!exited7)
                                    exit7(manager7, none(), none(), none());
                                if (flow7 == 1)
                                    return result7;
                            }
                        }
                        if (resp[B<"slice">()(none(), I<3>(), none())] == (S<"150">()))
                        {
                            // this is conditional in case we received a 125
                            size = (G<"ftplib.parse150">())(resp);
                        }
                        return py_tuple({ conn, size });
                    }) },
                { "transfercmd", w.method("FTP", "transfercmd", { py_positional("cmd"), py_positional("rest", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cmd = a[1];
                        py rest = a[2];
                        return self.method("ntransfercmd", { cmd, rest })[I<0>()];
                    }) },
                { "login", w.method("FTP", "login", { py_positional("user", S<"">()), py_positional("passwd", S<"">()), py_positional("acct", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py user = a[1];
                        py passwd = a[2];
                        py acct = a[3];
                        py resp;
                        if (!user.truthy())
                        {
                            user = S<"anonymous">();
                        }
                        if (!passwd.truthy())
                        {
                            passwd = S<"">();
                        }
                        if (!acct.truthy())
                        {
                            acct = S<"">();
                        }
                        if (((user == (S<"anonymous">())) && set_of({ S<"">(), S<"-">() }).contains(passwd)))
                        {
                            // If there is no anonymous ftp password specified
                            // then we'll just use anonymous@
                            // We don't send any other thing because:
                            // - We want to remain anonymous
                            // - We want to stop SPAM
                            // - We don't want to let ftp sites to discriminate by the user,
                            // host or country.
                            passwd = passwd + (S<"anonymous@">());
                        }
                        resp = self.method("sendcmd", { (S<"USER ">()) + user });
                        if (resp[I<0>()] == (S<"3">()))
                        {
                            resp = self.method("sendcmd", { (S<"PASS ">()) + passwd });
                        }
                        if (resp[I<0>()] == (S<"3">()))
                        {
                            resp = self.method("sendcmd", { (S<"ACCT ">()) + acct });
                        }
                        if (resp[I<0>()] != (S<"2">()))
                        {
                            py_raise((G<"ftplib.error_reply">())(resp));
                        }
                        return resp;
                    }) },
                { "retrbinary", w.method("FTP", "retrbinary", { py_positional("cmd"), py_positional("callback"), py_positional("blocksize", I<8192>()), py_positional("rest", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cmd = a[1];
                        py callback = a[2];
                        py blocksize = a[3];
                        py rest = a[4];
                        py conn;
                        py data;
                        self.method("voidcmd", { S<"TYPE I">() });
                        {
                            py const manager9 = self.method("transfercmd", { cmd, rest });
                            py const exit9 = pyclass::special_method(manager9, "__exit__");
                            py const value9 = pyclass::special_method(manager9, "__enter__")(manager9);
                            conn = value9;
                            [[maybe_unused]] int flow9 = 0;
                            [[maybe_unused]] py result9;
                            bool exited9 = false;
                            try
                            {
                                while (([&]() -> py { data = conn.method("recv", { blocksize }); return data; }()).truthy())
                                {
                                    callback(data);
                                }
                                // shutdown ssl layer
                                if (((!(G<"ftplib._SSLSocket">()).is_none()) && isinstance(conn, G<"ftplib._SSLSocket">())))
                                {
                                    conn.method("unwrap");
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler9;
                                exited9 = true;
                                if (!exit9(manager9, type_of(handler9.exception), handler9.exception, handler9.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler9.exception);
                            }
                            finally9:
                            if (!exited9)
                                exit9(manager9, none(), none(), none());
                            if (flow9 == 1)
                                return result9;
                        }
                        return self.method("voidresp");
                    }) },
                { "retrlines", w.method("FTP", "retrlines", { py_positional("cmd"), py_positional("callback", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cmd = a[1];
                        py callback = a[2];
                        py conn;
                        py fp;
                        py line;
                        py resp;
                        if (callback.is_none())
                        {
                            callback = G<"ftplib.print_line">();
                        }
                        resp = self.method("sendcmd", { S<"TYPE A">() });
                        {
                            py const manager11 = self.method("transfercmd", { cmd });
                            py const exit11 = pyclass::special_method(manager11, "__exit__");
                            py const value11 = pyclass::special_method(manager11, "__enter__")(manager11);
                            conn = value11;
                            [[maybe_unused]] int flow11 = 0;
                            [[maybe_unused]] py result11;
                            bool exited11 = false;
                            try
                            {
                                {
                                    py const manager12 = conn.method("makefile", { S<"r">() }, { { "encoding", self.attr("encoding") } });
                                    py const exit12 = pyclass::special_method(manager12, "__exit__");
                                    py const value12 = pyclass::special_method(manager12, "__enter__")(manager12);
                                    fp = value12;
                                    [[maybe_unused]] int flow12 = 0;
                                    [[maybe_unused]] py result12;
                                    bool exited12 = false;
                                    try
                                    {
                                        while ((I<1>()).truthy())
                                        {
                                            line = fp.method("readline", { self.attr("maxline") + (I<1>()) });
                                            if (py{ line.size() } > self.attr("maxline"))
                                            {
                                                py_raise((G<"ftplib.Error">())((S<"got more than %d bytes">()) % self.attr("maxline")));
                                            }
                                            if (self.attr("debugging") > (I<2>()))
                                            {
                                                (B<"print">())(S<"*retr*">(), S(line.repr()));
                                            }
                                            if (!line.truthy())
                                            {
                                                break;
                                            }
                                            if (line[B<"slice">()(I<-2>(), none(), none())] == (G<"ftplib.CRLF">()))
                                            {
                                                line = line[B<"slice">()(none(), I<-2>(), none())];
                                            }
                                            else if (line[B<"slice">()(I<-1>(), none(), none())] == (S<"\n">()))
                                            {
                                                line = line[B<"slice">()(none(), I<-1>(), none())];
                                            }
                                            callback(line);
                                        }
                                        // shutdown ssl layer
                                        if (((!(G<"ftplib._SSLSocket">()).is_none()) && isinstance(conn, G<"ftplib._SSLSocket">())))
                                        {
                                            conn.method("unwrap");
                                        }
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler12;
                                        exited12 = true;
                                        if (!exit12(manager12, type_of(handler12.exception), handler12.exception, handler12.exception.attr("__traceback__")).truthy())
                                            py_reraise(handler12.exception);
                                    }
                                    finally12:
                                    if (!exited12)
                                        exit12(manager12, none(), none(), none());
                                    if (flow12 == 1)
                                        { result11 = result12; flow11 = 1; goto finally11; }
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler11;
                                exited11 = true;
                                if (!exit11(manager11, type_of(handler11.exception), handler11.exception, handler11.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler11.exception);
                            }
                            finally11:
                            if (!exited11)
                                exit11(manager11, none(), none(), none());
                            if (flow11 == 1)
                                return result11;
                        }
                        return self.method("voidresp");
                    }) },
                { "storbinary", w.method("FTP", "storbinary", { py_positional("cmd"), py_positional("fp"), py_positional("blocksize", I<8192>()), py_positional("callback", none()), py_positional("rest", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cmd = a[1];
                        py fp = a[2];
                        py blocksize = a[3];
                        py callback = a[4];
                        py rest = a[5];
                        py buf;
                        py conn;
                        self.method("voidcmd", { S<"TYPE I">() });
                        {
                            py const manager14 = self.method("transfercmd", { cmd, rest });
                            py const exit14 = pyclass::special_method(manager14, "__exit__");
                            py const value14 = pyclass::special_method(manager14, "__enter__")(manager14);
                            conn = value14;
                            [[maybe_unused]] int flow14 = 0;
                            [[maybe_unused]] py result14;
                            bool exited14 = false;
                            try
                            {
                                while (([&]() -> py { buf = fp.method("read", { blocksize }); return buf; }()).truthy())
                                {
                                    conn.method("sendall", { buf });
                                    if (callback.truthy())
                                    {
                                        callback(buf);
                                    }
                                }
                                // shutdown ssl layer
                                if (((!(G<"ftplib._SSLSocket">()).is_none()) && isinstance(conn, G<"ftplib._SSLSocket">())))
                                {
                                    conn.method("unwrap");
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler14;
                                exited14 = true;
                                if (!exit14(manager14, type_of(handler14.exception), handler14.exception, handler14.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler14.exception);
                            }
                            finally14:
                            if (!exited14)
                                exit14(manager14, none(), none(), none());
                            if (flow14 == 1)
                                return result14;
                        }
                        return self.method("voidresp");
                    }) },
                { "storlines", w.method("FTP", "storlines", { py_positional("cmd"), py_positional("fp"), py_positional("callback", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cmd = a[1];
                        py fp = a[2];
                        py callback = a[3];
                        py buf;
                        py conn;
                        self.method("voidcmd", { S<"TYPE A">() });
                        {
                            py const manager16 = self.method("transfercmd", { cmd });
                            py const exit16 = pyclass::special_method(manager16, "__exit__");
                            py const value16 = pyclass::special_method(manager16, "__enter__")(manager16);
                            conn = value16;
                            [[maybe_unused]] int flow16 = 0;
                            [[maybe_unused]] py result16;
                            bool exited16 = false;
                            try
                            {
                                while ((I<1>()).truthy())
                                {
                                    buf = fp.method("readline", { self.attr("maxline") + (I<1>()) });
                                    if (py{ buf.size() } > self.attr("maxline"))
                                    {
                                        py_raise((G<"ftplib.Error">())((S<"got more than %d bytes">()) % self.attr("maxline")));
                                    }
                                    if (!buf.truthy())
                                    {
                                        break;
                                    }
                                    if (buf[B<"slice">()(I<-2>(), none(), none())] != (G<"ftplib.B_CRLF">()))
                                    {
                                        if ((G<"ftplib.B_CRLF">()).contains(buf[I<-1>()]))
                                        {
                                            buf = buf[B<"slice">()(none(), I<-1>(), none())];
                                        }
                                        buf = buf + (G<"ftplib.B_CRLF">());
                                    }
                                    conn.method("sendall", { buf });
                                    if (callback.truthy())
                                    {
                                        callback(buf);
                                    }
                                }
                                // shutdown ssl layer
                                if (((!(G<"ftplib._SSLSocket">()).is_none()) && isinstance(conn, G<"ftplib._SSLSocket">())))
                                {
                                    conn.method("unwrap");
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler16;
                                exited16 = true;
                                if (!exit16(manager16, type_of(handler16.exception), handler16.exception, handler16.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler16.exception);
                            }
                            finally16:
                            if (!exited16)
                                exit16(manager16, none(), none(), none());
                            if (flow16 == 1)
                                return result16;
                        }
                        return self.method("voidresp");
                    }) },
                { "acct", w.method("FTP", "acct", { py_positional("password") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py password = a[1];
                        py cmd;
                        cmd = (S<"ACCT ">()) + password;
                        return self.method("voidcmd", { cmd });
                    }) },
                { "nlst", w.method("FTP", "nlst", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py arg;
                        py cmd;
                        py files;
                        cmd = S<"NLST">();
                        for (py const& item : iterate{ args })
                        {
                            arg = item;
                            cmd = cmd + ((S<" ">()) + arg);
                        }
                        files = py_list();
                        self.method("retrlines", { cmd, files.attr("append") });
                        return files;
                    }, true) },
                { "dir", w.method("FTP", "dir", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py arg;
                        py cmd;
                        py func;
                        cmd = S<"LIST">();
                        func = none();
                        if ((args[B<"slice">()(I<-1>(), none(), none())].truthy() && (!isinstance(args[I<-1>()], B<"str">()))))
                        {
                            auto const items19 = unpack(py_tuple({ args[B<"slice">()(none(), I<-1>(), none())], args[I<-1>()] }), 2);
                            args = items19[0];
                            func = items19[1];
                        }
                        for (py const& item : iterate{ args })
                        {
                            arg = item;
                            if (arg.truthy())
                            {
                                cmd = cmd + ((S<" ">()) + arg);
                            }
                        }
                        self.method("retrlines", { cmd, func });
                        return none();
                    }, true) },
                { "mlsd", w.method("FTP", "mlsd", { py_positional("path", S<"">()), py_positional("facts", py_list()) }, [](arguments a) -> py
                    {
                        return make_native_generator("mlsd", "FTP.mlsd", w.module, [a = std::vector<py>(a.begin(), a.end())]() -> py
                        {
                            py self = a[0];
                            py path = a[1];
                            py facts = a[2];
                            py _;
                            py cmd;
                            py entry;
                            py fact;
                            py facts_found;
                            py key;
                            py line;
                            py lines;
                            py name;
                            py value;
                            if (facts.truthy())
                            {
                                self.method("sendcmd", { ((S<"OPTS MLST ">()) + (S<";">()).method("join", { facts })) + (S<";">()) });
                            }
                            if (path.truthy())
                            {
                                cmd = (S<"MLSD %s">()) % path;
                            }
                            else
                            {
                                cmd = S<"MLSD">();
                            }
                            lines = py_list();
                            self.method("retrlines", { cmd, lines.attr("append") });
                            for (py const& item : iterate{ lines })
                            {
                                line = item;
                                auto const items22 = unpack(line.method("rstrip", { G<"ftplib.CRLF">() }).method("partition", { S<" ">() }), 3);
                                facts_found = items22[0];
                                _ = items22[1];
                                name = items22[2];
                                entry = py_dict();
                                for (py const& item : iterate{ facts_found[B<"slice">()(none(), I<-1>(), none())].method("split", { S<";">() }) })
                                {
                                    fact = item;
                                    auto const items24 = unpack(fact.method("partition", { S<"=">() }), 3);
                                    key = items24[0];
                                    _ = items24[1];
                                    value = items24[2];
                                    entry.set_item(key.method("lower"), value);
                                }
                                py_yield(py_tuple({ name, entry }));
                            }
                            return none();
                        });
                    }) },
                { "rename", w.method("FTP", "rename", { py_positional("fromname"), py_positional("toname") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fromname = a[1];
                        py toname = a[2];
                        py resp;
                        resp = self.method("sendcmd", { (S<"RNFR ">()) + fromname });
                        if (resp[I<0>()] != (S<"3">()))
                        {
                            py_raise((G<"ftplib.error_reply">())(resp));
                        }
                        return self.method("voidcmd", { (S<"RNTO ">()) + toname });
                    }) },
                { "delete", w.method("FTP", "delete", { py_positional("filename") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filename = a[1];
                        py resp;
                        resp = self.method("sendcmd", { (S<"DELE ">()) + filename });
                        if (set_of({ S<"250">(), S<"200">() }).contains(resp[B<"slice">()(none(), I<3>(), none())]))
                        {
                            return resp;
                        }
                        else
                        {
                            py_raise((G<"ftplib.error_reply">())(resp));
                        }
                        return none();
                    }) },
                { "cwd", w.method("FTP", "cwd", { py_positional("dirname") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dirname = a[1];
                        py cmd;
                        py msg;
                        if (dirname == (S<"..">()))
                        {
                            try
                            {
                                return self.method("voidcmd", { S<"CDUP">() });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler25;
                                if (matches(handler25.exception, G<"ftplib.error_perm">()))
                                {
                                    msg = handler25.exception;
                                    if (msg.attr("args")[I<0>()][B<"slice">()(none(), I<3>(), none())] != (S<"500">()))
                                    {
                                        py_reraise(handler25.exception);
                                    }
                                }
                                else
                                    py_reraise(handler25.exception);
                            }
                        }
                        else if (dirname == (S<"">()))
                        {
                            dirname = S<".">();
                        }
                        cmd = (S<"CWD ">()) + dirname;
                        return self.method("voidcmd", { cmd });
                    }) },
                { "size", w.method("FTP", "size", { py_positional("filename") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py filename = a[1];
                        py resp;
                        py s;
                        // The SIZE command is defined in RFC-3659
                        resp = self.method("sendcmd", { (S<"SIZE ">()) + filename });
                        if (resp[B<"slice">()(none(), I<3>(), none())] == (S<"213">()))
                        {
                            s = resp[B<"slice">()(I<3>(), none(), none())].method("strip");
                            return (B<"int">())(s);
                        }
                        return none();
                    }) },
                { "mkd", w.method("FTP", "mkd", { py_positional("dirname") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dirname = a[1];
                        py resp;
                        resp = self.method("voidcmd", { (S<"MKD ">()) + dirname });
                        // fix around non-compliant implementations such as IIS shipped
                        // with Windows server 2003
                        if (!resp.method("startswith", { S<"257">() }).truthy())
                        {
                            return S<"">();
                        }
                        return (G<"ftplib.parse257">())(resp);
                    }) },
                { "rmd", w.method("FTP", "rmd", { py_positional("dirname") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dirname = a[1];
                        return self.method("voidcmd", { (S<"RMD ">()) + dirname });
                    }) },
                { "pwd", w.method("FTP", "pwd", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py resp;
                        resp = self.method("voidcmd", { S<"PWD">() });
                        // fix around non-compliant implementations such as IIS shipped
                        // with Windows server 2003
                        if (!resp.method("startswith", { S<"257">() }).truthy())
                        {
                            return S<"">();
                        }
                        return (G<"ftplib.parse257">())(resp);
                    }) },
                { "quit", w.method("FTP", "quit", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py resp;
                        resp = self.method("voidcmd", { S<"QUIT">() });
                        self.method("close");
                        return resp;
                    }) },
                { "close", w.method("FTP", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file;
                        py sock;
                        {
                            std::optional<py> pending26;
                            [[maybe_unused]] int flow26 = 0;
                            [[maybe_unused]] py result26;
                            try
                            {
                                file = self.attr("file");
                                self.set_attr("file", none());
                                if (!file.is_none())
                                {
                                    file.method("close");
                                }
                            }
                            catch (raised const&)
                            {
                                pending26 = py_take_exception();
                            }
                            finally26:
                            sock = self.attr("sock");
                            self.set_attr("sock", none());
                            if (!sock.is_none())
                            {
                                sock.method("close");
                            }
                            if (pending26)
                                py_reraise(*pending26);
                            if (flow26 == 1)
                                return result26;
                        }
                        return none();
                    }) }, }));

            define("ftplib", "_SSLSocket", none());
            // PROT defines whether or not the data channel is to be protected.
            // Though RFC-2228 defines four possible protection levels,
            // RFC-4217 only recommends two, Clear and Private.
            // Clear (PROT C) means that no security is to be used on the
            // data-channel, Private (PROT P) means that the data-channel
            // should be protected by TLS.
            // PBSZ command MUST still be issued, but must have a parameter of
            // '0' to indicate that no buffering is taking place and the data
            // connection should not be encapsulated.
            // --- Overridden FTP methods
            // overridden as we can't pass MSG_OOB flag to sendall()
            define("ftplib", "_150_re", none());
            define("ftplib", "parse150", w.function("parse150", "parse150", { py_positional("resp") }, [](arguments a) -> py
                {
                    py resp = a[0];
                    py m;
                    py re;
                    if (resp[B<"slice">()(none(), I<3>(), none())] != (S<"150">()))
                    {
                        py_raise((G<"ftplib.error_reply">())(resp));
                    }
                    if ((G<"ftplib._150_re">()).is_none())
                    {
                        re = module_of("re");
                        set_global("ftplib", "_150_re", re.method("compile", { S<"150 .* \\((\\d+) bytes\\)">(), re.attr("IGNORECASE") | re.attr("ASCII") }));
                    }
                    m = (G<"ftplib._150_re">()).method("match", { resp });
                    if (!m.truthy())
                    {
                        return none();
                    }
                    return (B<"int">())(m.method("group", { I<1>() }));
                }));

            define("ftplib", "_227_re", none());
            define("ftplib", "parse227", w.function("parse227", "parse227", { py_positional("resp") }, [](arguments a) -> py
                {
                    py resp = a[0];
                    py host;
                    py m;
                    py numbers;
                    py port;
                    py re;
                    if (resp[B<"slice">()(none(), I<3>(), none())] != (S<"227">()))
                    {
                        py_raise((G<"ftplib.error_reply">())(resp));
                    }
                    if ((G<"ftplib._227_re">()).is_none())
                    {
                        re = module_of("re");
                        set_global("ftplib", "_227_re", re.method("compile", { S<"(\\d+),(\\d+),(\\d+),(\\d+),(\\d+),(\\d+)">(), re.attr("ASCII") }));
                    }
                    m = (G<"ftplib._227_re">()).method("search", { resp });
                    if (!m.truthy())
                    {
                        py_raise((G<"ftplib.error_proto">())(resp));
                    }
                    numbers = m.method("groups");
                    host = (S<".">()).method("join", { numbers[B<"slice">()(none(), I<4>(), none())] });
                    port = ((B<"int">())(numbers[I<4>()]) << (I<8>())) + (B<"int">())(numbers[I<5>()]);
                    return py_tuple({ host, port });
                }));

            define("ftplib", "parse229", w.function("parse229", "parse229", { py_positional("resp"), py_positional("peer") }, [](arguments a) -> py
                {
                    py resp = a[0];
                    py peer = a[1];
                    py host;
                    py left;
                    py parts;
                    py port;
                    py right;
                    if (resp[B<"slice">()(none(), I<3>(), none())] != (S<"229">()))
                    {
                        py_raise((G<"ftplib.error_reply">())(resp));
                    }
                    left = resp.method("find", { S<"(">() });
                    if (left < (I<0>()))
                    {
                        py_raise((G<"ftplib.error_proto">())(resp));
                    }
                    right = resp.method("find", { S<")">(), left + (I<1>()) });
                    if (right < (I<0>()))
                    {
                        py_raise((G<"ftplib.error_proto">())(resp));
                    }
                    if (resp[left + (I<1>())] != resp[right - (I<1>())])
                    {
                        py_raise((G<"ftplib.error_proto">())(resp));
                    }
                    parts = resp[B<"slice">()(left + (I<1>()), right, none())].method("split", { resp[left + (I<1>())] });
                    if (py{ parts.size() } != (I<5>()))
                    {
                        py_raise((G<"ftplib.error_proto">())(resp));
                    }
                    host = peer[I<0>()];
                    port = (B<"int">())(parts[I<3>()]);
                    return py_tuple({ host, port });
                }));

            define("ftplib", "parse257", w.function("parse257", "parse257", { py_positional("resp") }, [](arguments a) -> py
                {
                    py resp = a[0];
                    py c;
                    py dirname;
                    py i;
                    py n;
                    if (resp[B<"slice">()(none(), I<3>(), none())] != (S<"257">()))
                    {
                        py_raise((G<"ftplib.error_reply">())(resp));
                    }
                    if (resp[B<"slice">()(I<3>(), I<5>(), none())] != (S<" \"">()))
                    {
                        return S<"">();
                    }
                    dirname = S<"">();
                    i = I<5>();
                    n = py{ resp.size() };
                    while (i < n)
                    {
                        c = resp[i];
                        i = i + (I<1>());
                        if (c == (S<"\"">()))
                        {
                            if (((i >= n) || (resp[i] != (S<"\"">()))))
                            {
                                break;
                            }
                            i = i + (I<1>());
                        }
                        dirname = dirname + c;
                    }
                    return dirname;
                }));

            define("ftplib", "print_line", w.function("print_line", "print_line", { py_positional("line") }, [](arguments a) -> py
                {
                    py line = a[0];
                    (B<"print">())(line);
                    return none();
                }));

            define("ftplib", "ftpcp", w.function("ftpcp", "ftpcp", { py_positional("source"), py_positional("sourcename"), py_positional("target"), py_positional("targetname", S<"">()), py_positional("type", S<"I">()) }, [](arguments a) -> py
                {
                    py source = a[0];
                    py sourcename = a[1];
                    py target = a[2];
                    py targetname = a[3];
                    py type = a[4];
                    py sourcehost;
                    py sourceport;
                    py sreply;
                    py treply;
                    py untrusted_host;
                    if (!targetname.truthy())
                    {
                        targetname = sourcename;
                    }
                    type = (S<"TYPE ">()) + type;
                    source.method("voidcmd", { type });
                    target.method("voidcmd", { type });
                    // Don't trust the IPv4 address the source server advertises in its PASV
                    // reply: a malicious source could otherwise point the target's data
                    // connection at an arbitrary host (SSRF).  A caller that needs the old
                    // behavior can set trust_server_pasv_ipv4_address on the source FTP
                    // object.  See FTP.makepasv(), which applies the same rule.
                    auto const items28 = unpack((G<"ftplib.parse227">())(source.method("sendcmd", { S<"PASV">() })), 2);
                    untrusted_host = items28[0];
                    sourceport = items28[1];
                    if (source.attr("trust_server_pasv_ipv4_address").truthy())
                    {
                        sourcehost = untrusted_host;
                    }
                    else
                    {
                        sourcehost = source.attr("sock").method("getpeername")[I<0>()];
                    }
                    target.method("sendport", { sourcehost, sourceport });
                    // RFC 959: the user must "listen" [...] BEFORE sending the
                    // transfer request.
                    // So: STOR before RETR, because here the target is a "user".
                    treply = target.method("sendcmd", { (S<"STOR ">()) + targetname });
                    if (!set_of({ S<"125">(), S<"150">() }).contains(treply[B<"slice">()(none(), I<3>(), none())]))
                    {
                        py_raise(G<"ftplib.error_proto">());
                    }
                    sreply = source.method("sendcmd", { (S<"RETR ">()) + sourcename });
                    if (!set_of({ S<"125">(), S<"150">() }).contains(sreply[B<"slice">()(none(), I<3>(), none())]))
                    {
                        py_raise(G<"ftplib.error_proto">());
                    }
                    source.method("voidresp");
                    target.method("voidresp");
                    return none();
                }));

            define("ftplib", "test", w.function("test", "test", {  }, [](arguments a) -> py
                {
                    py acct;
                    py cmd;
                    py debugging;
                    py file;
                    py ftp;
                    py host;
                    py netrc;
                    py netrcobj;
                    py passwd;
                    py rcfile;
                    py resp;
                    py userid;
                    if (py{ (Mod<"sys">().attr("argv")).size() } < (I<2>()))
                    {
                        (B<"print">())((G<"ftplib.test">()).attr("__doc__"));
                        (Mod<"sys">().attr("exit"))(I<0>());
                    }
                    netrc = module_of("netrc");
                    debugging = I<0>();
                    rcfile = none();
                    while ((Mod<"sys">().attr("argv"))[I<1>()] == (S<"-d">()))
                    {
                        debugging = debugging + (I<1>());
                        del_item(Mod<"sys">().attr("argv"), I<1>());
                    }
                    if ((Mod<"sys">().attr("argv"))[I<1>()][B<"slice">()(none(), I<2>(), none())] == (S<"-r">()))
                    {
                        // get name of alternate ~/.netrc file:
                        rcfile = (Mod<"sys">().attr("argv"))[I<1>()][B<"slice">()(I<2>(), none(), none())];
                        del_item(Mod<"sys">().attr("argv"), I<1>());
                    }
                    host = (Mod<"sys">().attr("argv"))[I<1>()];
                    ftp = (G<"ftplib.FTP">())(host);
                    ftp.method("set_debuglevel", { debugging });
                    py const value30 = S<"">();
                    userid = value30;
                    passwd = value30;
                    acct = value30;
                    {
                        bool otherwise31 = false;
                        try
                        {
                            netrcobj = netrc.method("netrc", { rcfile });
                            otherwise31 = true;
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler31;
                            if (matches(handler31.exception, B<"OSError">()))
                            {
                                if (!rcfile.is_none())
                                {
                                    (B<"print">()).call({ S<"Could not open account file -- using anonymous login.">() }, { { "file", Mod<"sys">().attr("stderr") } });
                                }
                            }
                            else
                                py_reraise(handler31.exception);
                        }
                        if (otherwise31)
                        {
                            try
                            {
                                auto const items33 = unpack(netrcobj.method("authenticators", { host }), 3);
                                userid = items33[0];
                                acct = items33[1];
                                passwd = items33[2];
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler32;
                                if (matches(handler32.exception, py_tuple({ B<"KeyError">(), B<"TypeError">() })))
                                {
                                    // no account for host
                                    (B<"print">()).call({ S<"No account -- using anonymous login.">() }, { { "file", Mod<"sys">().attr("stderr") } });
                                }
                                else
                                    py_reraise(handler32.exception);
                            }
                        }
                    }
                    ftp.method("login", { userid, passwd, acct });
                    for (py const& item : iterate{ (Mod<"sys">().attr("argv"))[B<"slice">()(I<2>(), none(), none())] })
                    {
                        file = item;
                        if (file[B<"slice">()(none(), I<2>(), none())] == (S<"-l">()))
                        {
                            ftp.method("dir", { file[B<"slice">()(I<2>(), none(), none())] });
                        }
                        else if (file[B<"slice">()(none(), I<2>(), none())] == (S<"-d">()))
                        {
                            cmd = S<"CWD">();
                            if (file[B<"slice">()(I<2>(), none(), none())].truthy())
                            {
                                cmd = (cmd + (S<" ">())) + file[B<"slice">()(I<2>(), none(), none())];
                            }
                            resp = ftp.method("sendcmd", { cmd });
                        }
                        else if (file == (S<"-p">()))
                        {
                            ftp.method("set_pasv", { boolean(!ftp.attr("passiveserver").truthy()) });
                        }
                        else
                        {
                            ftp.method("retrbinary", { (S<"RETR ">()) + file, (Mod<"sys">().attr("stdout")).attr("buffer").attr("write"), I<1024>() });
                            (Mod<"sys">().attr("stdout")).attr("buffer").method("flush");
                        }
                        (Mod<"sys">().attr("stdout")).method("flush");
                    }
                    ftp.method("quit");
                    return none();
                }));

            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool ftplib_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_ftplib();
                }
                if (auto const doc = ftplib_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("socket", py{ fn_import("socket") }.release());
                aAttributes.emplace_back("_GLOBAL_DEFAULT_TIMEOUT", import_attribute("socket", "_GLOBAL_DEFAULT_TIMEOUT").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("ftplib", ftplib_module);
    }
}
