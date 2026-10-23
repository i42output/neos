/*
  poplib.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/poplib.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module poplib (CPython 3.13's Lib/poplib.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "poplib_docs.hpp"

namespace neos::python_runtime::detail::poplib_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "poplib.name"))
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
                py_raise("SystemError", std::string{ "poplib: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"poplib.name">()))
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
            if (modules_imported("poplib"))
                return py{ fn_import("poplib") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "poplib" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "poplib", poplib_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- poplib (Lib/poplib.py)

        void make_poplib()
        {
            static module_writer const& w = writer("poplib");
            // Author: David Ascher <david_ascher@brown.edu>
            // [heavily stealing from nntplib.py]
            // Updated: Piers Lauder <piers@cs.su.oz.au> [Jul '97]
            // String method conversion and test jig improvements by ESR, February 2001.
            // Added the POP3_SSL class. Methods loosely based on IMAP_SSL. Hector Urtubia <urtubia@mrbook.org> Aug 2003
            // Example (see the test function at the end of this file)
            // Imports
            define("poplib", "HAVE_SSL", boolean(false));
            define("poplib", "__all__", py_list({ S<"POP3">(), S<"error_proto">() }));
            // Exception raised when an error or invalid response is received:
            define("poplib", "error_proto", w.make_class("error_proto", { B<"Exception">() }, {
 }));

            // Standard Port
            define("poplib", "POP3_PORT", I<110>());
            // POP SSL PORT
            define("poplib", "POP3_SSL_PORT", I<995>());
            // Line terminators (we always output CRLF, but accept any of CRLF, LFCR, LF)
            define("poplib", "CR", py{ make_bytes(std::string{ "\x0d", 1 }) });
            define("poplib", "LF", py{ make_bytes(std::string{ "\x0a", 1 }) });
            define("poplib", "CRLF", (G<"poplib.CR">()) + (G<"poplib.LF">()));
            // maximal line length when calling readline(). This is to prevent
            // reading arbitrary length lines. RFC 1939 limits POP3 line length to
            // 512 characters, including CRLF. We have selected 2048 just to be on
            // the safe side.
            define("poplib", "_MAXLINE", I<2048>());
                // Internal: send one command to the server (through _putline())
                // Internal: return one line from the server, stripping CRLF.
                // This is where all the CPU time of this module is consumed.
                // Raise error_proto('-ERR EOF') if the connection is closed.
                // Internal: get a response from the server.
                // Raise 'error_proto' if the response doesn't start with '+'.
                // Internal: get a response plus following text from the server.
                // Internal: send a command and get the response
                // Internal: send a command and get the response plus following text
                // These can be useful:
                // Here are all the POP commands:
                // __del__ = quit
                // optional commands:
            define("poplib", "POP3", w.make_class("POP3", {  }, {
                { "encoding", S<"UTF-8">() },
                { "__init__", w.method("POP3", "__init__", { py_positional("host"), py_positional("port", G<"poplib.POP3_PORT">()), py_positional("timeout", Mod<"socket">().attr("_GLOBAL_DEFAULT_TIMEOUT")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py port = a[2];
                        py timeout = a[3];
                        self.set_attr("host", host);
                        self.set_attr("port", port);
                        self.set_attr("_tls_established", boolean(false));
                        (Mod<"sys">().attr("audit"))(S<"poplib.connect">(), self, host, port);
                        self.set_attr("sock", self.method("_create_socket", { timeout }));
                        self.set_attr("file", self.attr("sock").method("makefile", { S<"rb">() }));
                        self.set_attr("_debugging", I<0>());
                        self.set_attr("welcome", self.method("_getresp"));
                        return none();
                    }) },
                { "_create_socket", w.method("POP3", "_create_socket", { py_positional("timeout") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py timeout = a[1];
                        if (((!timeout.is_none()) && (!timeout.truthy())))
                        {
                            py_raise((B<"ValueError">())(S<"Non-blocking socket (timeout=0) is not supported">()));
                        }
                        return (Mod<"socket">().attr("create_connection"))(py_tuple({ self.attr("host"), self.attr("port") }), timeout);
                    }) },
                { "_putline", w.method("POP3", "_putline", { py_positional("line") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line = a[1];
                        if (self.attr("_debugging") > (I<1>()))
                        {
                            (B<"print">())(S<"*put*">(), S(line.repr()));
                        }
                        (Mod<"sys">().attr("audit"))(S<"poplib.putline">(), self, line);
                        self.attr("sock").method("sendall", { line + (G<"poplib.CRLF">()) });
                        return none();
                    }) },
                { "_putcmd", w.method("POP3", "_putcmd", { py_positional("line") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line = a[1];
                        if (self.attr("_debugging").truthy())
                        {
                            (B<"print">())(S<"*cmd*">(), S(line.repr()));
                        }
                        line = (B<"bytes">())(line, self.attr("encoding"));
                        self.method("_putline", { line });
                        return none();
                    }) },
                { "_getline", w.method("POP3", "_getline", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line;
                        py octets;
                        line = self.attr("file").method("readline", { (G<"poplib._MAXLINE">()) + (I<1>()) });
                        if (py{ line.size() } > (G<"poplib._MAXLINE">()))
                        {
                            py_raise((G<"poplib.error_proto">())(S<"line too long">()));
                        }
                        if (self.attr("_debugging") > (I<1>()))
                        {
                            (B<"print">())(S<"*get*">(), S(line.repr()));
                        }
                        if (!line.truthy())
                        {
                            py_raise((G<"poplib.error_proto">())(S<"-ERR EOF">()));
                        }
                        octets = py{ line.size() };
                        // server can send any combination of CR & LF
                        // however, 'readline()' returns lines ending in LF
                        // so only possibilities are ...LF, ...CRLF, CR...LF
                        if (line[B<"slice">()(I<-2>(), none(), none())] == (G<"poplib.CRLF">()))
                        {
                            return py_tuple({ line[B<"slice">()(none(), I<-2>(), none())], octets });
                        }
                        if (line[B<"slice">()(none(), I<1>(), none())] == (G<"poplib.CR">()))
                        {
                            return py_tuple({ line[B<"slice">()(I<1>(), I<-1>(), none())], octets });
                        }
                        return py_tuple({ line[B<"slice">()(none(), I<-1>(), none())], octets });
                    }) },
                { "_getresp", w.method("POP3", "_getresp", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py o;
                        py resp;
                        auto const items1 = unpack(self.method("_getline"), 2);
                        resp = items1[0];
                        o = items1[1];
                        if (self.attr("_debugging") > (I<1>()))
                        {
                            (B<"print">())(S<"*resp*">(), S(resp.repr()));
                        }
                        if (!resp.method("startswith", { py{ make_bytes(std::string{ "\x2b", 1 }) } }).truthy())
                        {
                            py_raise((G<"poplib.error_proto">())(resp));
                        }
                        return resp;
                    }) },
                { "_getlongresp", w.method("POP3", "_getlongresp", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line;
                        py list;
                        py o;
                        py octets;
                        py resp;
                        resp = self.method("_getresp");
                        list = py_list();
                        octets = I<0>();
                        auto const items2 = unpack(self.method("_getline"), 2);
                        line = items2[0];
                        o = items2[1];
                        while (line != py{ make_bytes(std::string{ "\x2e", 1 }) })
                        {
                            if (line.method("startswith", { py{ make_bytes(std::string{ "\x2e\x2e", 2 }) } }).truthy())
                            {
                                o = o - (I<1>());
                                line = line[B<"slice">()(I<1>(), none(), none())];
                            }
                            octets = octets + o;
                            list.method("append", { line });
                            auto const items4 = unpack(self.method("_getline"), 2);
                            line = items4[0];
                            o = items4[1];
                        }
                        return py_tuple({ resp, list, octets });
                    }) },
                { "_shortcmd", w.method("POP3", "_shortcmd", { py_positional("line") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line = a[1];
                        self.method("_putcmd", { line });
                        return self.method("_getresp");
                    }) },
                { "_longcmd", w.method("POP3", "_longcmd", { py_positional("line") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line = a[1];
                        self.method("_putcmd", { line });
                        return self.method("_getlongresp");
                    }) },
                { "getwelcome", w.method("POP3", "getwelcome", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("welcome");
                    }) },
                { "set_debuglevel", w.method("POP3", "set_debuglevel", { py_positional("level") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py level = a[1];
                        self.set_attr("_debugging", level);
                        return none();
                    }) },
                { "user", w.method("POP3", "user", { py_positional("user") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py user = a[1];
                        return self.method("_shortcmd", { (S<"USER %s">()) % user });
                    }) },
                { "pass_", w.method("POP3", "pass_", { py_positional("pswd") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py pswd = a[1];
                        return self.method("_shortcmd", { (S<"PASS %s">()) % pswd });
                    }) },
                { "stat", w.method("POP3", "stat", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py numMessages;
                        py rets;
                        py retval;
                        py sizeMessages;
                        retval = self.method("_shortcmd", { S<"STAT">() });
                        rets = retval.method("split");
                        if (self.attr("_debugging").truthy())
                        {
                            (B<"print">())(S<"*stat*">(), S(rets.repr()));
                        }
                        // Check if the response has enough elements
                        // RFC 1939 requires at least 3 elements (+OK, message count, mailbox size)
                        // but allows additional data after the required fields
                        if (py{ rets.size() } < (I<3>()))
                        {
                            py_raise((G<"poplib.error_proto">())(S<"Invalid STAT response format">()));
                        }
                        try
                        {
                            numMessages = (B<"int">())(rets[I<1>()]);
                            sizeMessages = (B<"int">())(rets[I<2>()]);
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler5;
                            if (matches(handler5.exception, B<"ValueError">()))
                            {
                                py_raise((G<"poplib.error_proto">())(S<"Invalid STAT response data: non-numeric values">()));
                            }
                            else
                                py_reraise(handler5.exception);
                        }
                        return py_tuple({ numMessages, sizeMessages });
                    }) },
                { "list", w.method("POP3", "list", { py_positional("which", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py which = a[1];
                        if (!which.is_none())
                        {
                            return self.method("_shortcmd", { (S<"LIST %s">()) % which });
                        }
                        return self.method("_longcmd", { S<"LIST">() });
                    }) },
                { "retr", w.method("POP3", "retr", { py_positional("which") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py which = a[1];
                        return self.method("_longcmd", { (S<"RETR %s">()) % which });
                    }) },
                { "dele", w.method("POP3", "dele", { py_positional("which") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py which = a[1];
                        return self.method("_shortcmd", { (S<"DELE %s">()) % which });
                    }) },
                { "noop", w.method("POP3", "noop", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("_shortcmd", { S<"NOOP">() });
                    }) },
                { "rset", w.method("POP3", "rset", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("_shortcmd", { S<"RSET">() });
                    }) },
                { "quit", w.method("POP3", "quit", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py resp;
                        resp = self.method("_shortcmd", { S<"QUIT">() });
                        self.method("close");
                        return resp;
                    }) },
                { "close", w.method("POP3", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc;
                        py file;
                        py sock;
                        {
                            std::optional<py> pending6;
                            [[maybe_unused]] int flow6 = 0;
                            [[maybe_unused]] py result6;
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
                                pending6 = py_take_exception();
                            }
                            finally6:
                            sock = self.attr("sock");
                            self.set_attr("sock", none());
                            if (!sock.is_none())
                            {
                                {
                                    std::optional<py> pending7;
                                    [[maybe_unused]] int flow7 = 0;
                                    [[maybe_unused]] py result7;
                                    try
                                    {
                                        try
                                        {
                                            sock.method("shutdown", { Mod<"socket">().attr("SHUT_RDWR") });
                                        }
                                        catch (raised const&)
                                        {
                                            pyclass::handler_scope handler7;
                                            if (matches(handler7.exception, B<"OSError">()))
                                            {
                                                exc = handler7.exception;
                                                // The server might already have closed the connection.
                                                // On Windows, this may result in WSAEINVAL (error 10022):
                                                // An invalid operation was attempted.
                                                if (((exc.attr("errno") != (Mod<"errno">().attr("ENOTCONN"))) && ((B<"getattr">())(exc, S<"winerror">(), I<0>()) != (I<10022>()))))
                                                {
                                                    py_reraise(handler7.exception);
                                                }
                                            }
                                            else
                                                py_reraise(handler7.exception);
                                        }
                                    }
                                    catch (raised const&)
                                    {
                                        pending7 = py_take_exception();
                                    }
                                    finally7:
                                    sock.method("close");
                                    if (pending7)
                                        py_reraise(*pending7);
                                    if (flow7 == 1)
                                        return result7;
                                }
                            }
                            if (pending6)
                                py_reraise(*pending6);
                            if (flow6 == 1)
                                return result6;
                        }
                        return none();
                    }) },
                { "rpop", w.method("POP3", "rpop", { py_positional("user") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py user = a[1];
                        return self.method("_shortcmd", { (S<"RPOP %s">()) % user });
                    }) },
                { "timestamp", (Mod<"re">()).method("compile", { py{ make_bytes(std::string{ "\x5c\x2b\x4f\x4b\x2e\x5b\x5e\x3c\x5d\x2a\x28\x3c\x2e\x2a\x3e\x29", 16 }) } }) },
                { "apop", w.method("POP3", "apop", { py_positional("user"), py_positional("password") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py user = a[1];
                        py password = a[2];
                        py digest;
                        py hashlib;
                        py m;
                        py secret;
                        secret = (B<"bytes">())(password, self.attr("encoding"));
                        m = self.attr("timestamp").method("match", { self.attr("welcome") });
                        if (!m.truthy())
                        {
                            py_raise((G<"poplib.error_proto">())(S<"-ERR APOP not supported by server">()));
                        }
                        hashlib = module_of("hashlib");
                        digest = m.method("group", { I<1>() }) + secret;
                        digest = hashlib.method("md5", { digest }).method("hexdigest");
                        return self.method("_shortcmd", { (S<"APOP %s %s">()) % py_tuple({ user, digest }) });
                    }) },
                { "top", w.method("POP3", "top", { py_positional("which"), py_positional("howmuch") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py which = a[1];
                        py howmuch = a[2];
                        return self.method("_longcmd", { (S<"TOP %s %s">()) % py_tuple({ which, howmuch }) });
                    }) },
                { "uidl", w.method("POP3", "uidl", { py_positional("which", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py which = a[1];
                        if (!which.is_none())
                        {
                            return self.method("_shortcmd", { (S<"UIDL %s">()) % which });
                        }
                        return self.method("_longcmd", { S<"UIDL">() });
                    }) },
                { "utf8", w.method("POP3", "utf8", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("_shortcmd", { S<"UTF8">() });
                    }) },
                { "capa", w.method("POP3", "capa", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py _parsecap;
                        py capargs;
                        py capline;
                        py capnm;
                        py caps;
                        py rawcaps;
                        py resp;
                        _parsecap = w.function("_parsecap", "POP3.capa.<locals>._parsecap", { py_positional("line") }, [](arguments a) -> py
    {
        py line = a[0];
        py lst;
        lst = line.method("decode", { S<"ascii">() }).method("split");
        return py_tuple({ lst[I<0>()], lst[B<"slice">()(I<1>(), none(), none())] });
    });
                        caps = py_dict();
                        try
                        {
                            resp = self.method("_longcmd", { S<"CAPA">() });
                            rawcaps = resp[I<1>()];
                            for (py const& item : iterate{ rawcaps })
                            {
                                capline = item;
                                auto const items10 = unpack(_parsecap(capline), 2);
                                capnm = items10[0];
                                capargs = items10[1];
                                caps.set_item(capnm, capargs);
                            }
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler8;
                            if (matches(handler8.exception, G<"poplib.error_proto">()))
                            {
                                py_raise((G<"poplib.error_proto">())(S<"-ERR CAPA not supported by server">()));
                            }
                            else
                                py_reraise(handler8.exception);
                        }
                        return caps;
                    }) },
                { "stls", w.method("POP3", "stls", { py_positional("context", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py context = a[1];
                        py caps;
                        py resp;
                        if (!(G<"poplib.HAVE_SSL">()).truthy())
                        {
                            py_raise((G<"poplib.error_proto">())(S<"-ERR TLS support missing">()));
                        }
                        if (self.attr("_tls_established").truthy())
                        {
                            py_raise((G<"poplib.error_proto">())(S<"-ERR TLS session already established">()));
                        }
                        caps = self.method("capa");
                        if (!caps.contains((S<"STLS">())))
                        {
                            py_raise((G<"poplib.error_proto">())(S<"-ERR STLS not supported by server">()));
                        }
                        if (context.is_none())
                        {
                            context = (Mod<"ssl">().attr("_create_stdlib_context"))();
                        }
                        resp = self.method("_shortcmd", { S<"STLS">() });
                        self.set_attr("sock", context.method("wrap_socket", { self.attr("sock") }, { { "server_hostname", self.attr("host") } }));
                        self.set_attr("file", self.attr("sock").method("makefile", { S<"rb">() }));
                        self.set_attr("_tls_established", boolean(true));
                        return resp;
                    }) }, }));

            if ((G<"poplib.HAVE_SSL">()).truthy())
            {
                define("poplib", "POP3_SSL", w.make_class("POP3_SSL", { G<"poplib.POP3">() }, {
                    { "__init__", w.method("POP3_SSL", "__init__", { py_positional("host"), py_positional("port", G<"poplib.POP3_SSL_PORT">()), py_keyword("timeout", Mod<"socket">().attr("_GLOBAL_DEFAULT_TIMEOUT")), py_keyword("context", none()) }, [](arguments a) -> py
                        {
                            py self = a[0];
                            py host = a[1];
                            py port = a[2];
                            py timeout = a[3];
                            py context = a[4];
                            if (context.is_none())
                            {
                                context = (Mod<"ssl">().attr("_create_stdlib_context"))();
                            }
                            self.set_attr("context", context);
                            (G<"poplib.POP3">()).method("__init__", { self, host, port, timeout });
                            return none();
                        }) },
                    { "_create_socket", w.method("POP3_SSL", "_create_socket", { py_positional("timeout") }, [](arguments a) -> py
                        {
                            py self = a[0];
                            py timeout = a[1];
                            py sock;
                            sock = (G<"poplib.POP3">()).method("_create_socket", { self, timeout });
                            sock = self.attr("context").method("wrap_socket", { sock }, { { "server_hostname", self.attr("host") } });
                            return sock;
                        }) },
                    { "stls", w.method("POP3_SSL", "stls", { py_positional("context", none()) }, [](arguments a) -> py
                        {
                            py self = a[0];
                            py context = a[1];
                            py_raise((G<"poplib.error_proto">())(S<"-ERR TLS session already established">()));
                        }) }, }));

                (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                    {
                        (G<"poplib.__all__">()).method("append", { S<"POP3_SSL">() });
                        return none();
                    }))();
            }
            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool poplib_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_poplib();
                }
                if (auto const doc = poplib_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("errno", py{ fn_import("errno") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("socket", py{ fn_import("socket") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("poplib", poplib_module);
    }
}
