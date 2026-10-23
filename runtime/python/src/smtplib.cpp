/*
  smtplib.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/smtplib.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module smtplib (CPython 3.13's Lib/smtplib.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "smtplib_docs.hpp"

namespace neos::python_runtime::detail::smtplib_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "smtplib.name"))
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
                py_raise("SystemError", std::string{ "smtplib: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"smtplib.name">()))
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
            if (modules_imported("smtplib"))
                return py{ fn_import("smtplib") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "smtplib" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "smtplib", smtplib_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- smtplib (Lib/smtplib.py)

        void make_smtplib()
        {
            static module_writer const& w = writer("smtplib");
            // ! /usr/bin/env python3
            // Author: The Dragon De Monsyne <dragondm@integral.org>
            // ESMTP support, test code and doc fixes added by
            // Eric S. Raymond <esr@thyrsus.com>
            // Better RFC 821 compliance (MAIL and RCPT, and CRLF in data)
            // by Carey Evans <c.evans@clear.net.nz>, for picky mail servers.
            // RFC 2554 (authentication) support by Gerhard Haering <gerhard@bigfoot.de>.
            // 
            // This was modified from the Python 1.5 library HTTP lib.
            define("smtplib", "__all__", py_list({ S<"SMTPException">(), S<"SMTPNotSupportedError">(), S<"SMTPServerDisconnected">(), S<"SMTPResponseException">(), S<"SMTPSenderRefused">(), S<"SMTPRecipientsRefused">(), S<"SMTPDataError">(), S<"SMTPConnectError">(), S<"SMTPHeloError">(), S<"SMTPAuthenticationError">(), S<"quoteaddr">(), S<"quotedata">(), S<"SMTP">() }));
            define("smtplib", "SMTP_PORT", I<25>());
            define("smtplib", "SMTP_SSL_PORT", I<465>());
            define("smtplib", "CRLF", S<"\r\n">());
            define("smtplib", "bCRLF", py{ make_bytes(std::string{ "\x0d\x0a", 2 }) });
            define("smtplib", "_MAXLINE", I<8192>());
            define("smtplib", "_MAXCHALLENGE", I<5>());
            define("smtplib", "OLDSTYLE_AUTH", (Mod<"re">()).method("compile", { S<"auth=(.*)">(), Mod<"re">().attr("I") }));
            // Exception classes used by this module.
            define("smtplib", "SMTPException", w.make_class("SMTPException", { B<"OSError">() }, {
 }));

            define("smtplib", "SMTPNotSupportedError", w.make_class("SMTPNotSupportedError", { G<"smtplib.SMTPException">() }, {
 }));

            define("smtplib", "SMTPServerDisconnected", w.make_class("SMTPServerDisconnected", { G<"smtplib.SMTPException">() }, {
 }));

            define("smtplib", "SMTPResponseException", w.make_class("SMTPResponseException", { G<"smtplib.SMTPException">() }, {
                { "__init__", w.method("SMTPResponseException", "__init__", { py_positional("code"), py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py code = a[1];
                        py msg = a[2];
                        self.set_attr("smtp_code", code);
                        self.set_attr("smtp_error", msg);
                        self.set_attr("args", py_tuple({ code, msg }));
                        return none();
                    }) }, }));

            define("smtplib", "SMTPSenderRefused", w.make_class("SMTPSenderRefused", { G<"smtplib.SMTPResponseException">() }, {
                { "__init__", w.method("SMTPSenderRefused", "__init__", { py_positional("code"), py_positional("msg"), py_positional("sender") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py code = a[1];
                        py msg = a[2];
                        py sender = a[3];
                        self.set_attr("smtp_code", code);
                        self.set_attr("smtp_error", msg);
                        self.set_attr("sender", sender);
                        self.set_attr("args", py_tuple({ code, msg, sender }));
                        return none();
                    }) }, }));

            define("smtplib", "SMTPRecipientsRefused", w.make_class("SMTPRecipientsRefused", { G<"smtplib.SMTPException">() }, {
                { "__init__", w.method("SMTPRecipientsRefused", "__init__", { py_positional("recipients") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py recipients = a[1];
                        self.set_attr("recipients", recipients);
                        self.set_attr("args", py_tuple({ recipients }));
                        return none();
                    }) }, }));

            define("smtplib", "SMTPDataError", w.make_class("SMTPDataError", { G<"smtplib.SMTPResponseException">() }, {
 }));

            define("smtplib", "SMTPConnectError", w.make_class("SMTPConnectError", { G<"smtplib.SMTPResponseException">() }, {
 }));

            define("smtplib", "SMTPHeloError", w.make_class("SMTPHeloError", { G<"smtplib.SMTPResponseException">() }, {
 }));

            define("smtplib", "SMTPAuthenticationError", w.make_class("SMTPAuthenticationError", { G<"smtplib.SMTPResponseException">() }, {
 }));

            define("smtplib", "quoteaddr", w.function("quoteaddr", "quoteaddr", { py_positional("addrstring") }, [](arguments a) -> py
                {
                    py addrstring = a[0];
                    py addr;
                    py displayname;
                    auto const items1 = unpack((Mod<"email.utils">().attr("parseaddr"))(addrstring), 2);
                    displayname = items1[0];
                    addr = items1[1];
                    if (py_tuple({ displayname, addr }) == py_tuple({ S<"">(), S<"">() }))
                    {
                        // parseaddr couldn't parse it, use it as is and hope for the best.
                        if (addrstring.method("strip").method("startswith", { S<"<">() }).truthy())
                        {
                            return addrstring;
                        }
                        return (S<"<%s>">()) % addrstring;
                    }
                    return (S<"<%s>">()) % addr;
                }));

            define("smtplib", "_addr_only", w.function("_addr_only", "_addr_only", { py_positional("addrstring") }, [](arguments a) -> py
                {
                    py addrstring = a[0];
                    py addr;
                    py displayname;
                    auto const items2 = unpack((Mod<"email.utils">().attr("parseaddr"))(addrstring), 2);
                    displayname = items2[0];
                    addr = items2[1];
                    if (py_tuple({ displayname, addr }) == py_tuple({ S<"">(), S<"">() }))
                    {
                        // parseaddr couldn't parse it, so use it as is.
                        return addrstring;
                    }
                    return addr;
                }));

            // Legacy method kept for backward compatibility.
            define("smtplib", "quotedata", w.function("quotedata", "quotedata", { py_positional("data") }, [](arguments a) -> py
                {
                    py data = a[0];
                    return (Mod<"re">().attr("sub"))(S<"(?m)^\\.">(), S<"..">(), (Mod<"re">().attr("sub"))(S<"(?:\\r\\n|\\n|\\r(?!\\n))">(), G<"smtplib.CRLF">(), data));
                }));

            define("smtplib", "_quote_periods", w.function("_quote_periods", "_quote_periods", { py_positional("bindata") }, [](arguments a) -> py
                {
                    py bindata = a[0];
                    return (Mod<"re">().attr("sub"))(py{ make_bytes(std::string{ "\x28\x3f\x6d\x29\x5e\x5c\x2e", 7 }) }, py{ make_bytes(std::string{ "\x2e\x2e", 2 }) }, bindata);
                }));

            define("smtplib", "_fix_eols", w.function("_fix_eols", "_fix_eols", { py_positional("data") }, [](arguments a) -> py
                {
                    py data = a[0];
                    return (Mod<"re">().attr("sub"))(S<"(?:\\r\\n|\\n|\\r(?!\\n))">(), G<"smtplib.CRLF">(), data);
                }));

            define("smtplib", "_have_cram_md5_support", boolean(true));
            define("smtplib", "_have_ssl", boolean(false));
                // std smtp commands
                // a.k.a.
                // some useful methods
            define("smtplib", "SMTP", w.make_class("SMTP", {  }, {
                { "debuglevel", I<0>() },
                { "sock", none() },
                { "file", none() },
                { "helo_resp", none() },
                { "ehlo_msg", S<"ehlo">() },
                { "ehlo_resp", none() },
                { "does_esmtp", boolean(false) },
                { "default_port", G<"smtplib.SMTP_PORT">() },
                { "__init__", w.method("SMTP", "__init__", { py_positional("host", S<"">()), py_positional("port", I<0>()), py_positional("local_hostname", none()), py_positional("timeout", Mod<"socket">().attr("_GLOBAL_DEFAULT_TIMEOUT")), py_positional("source_address", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py port = a[2];
                        py local_hostname = a[3];
                        py timeout = a[4];
                        py source_address = a[5];
                        py addr;
                        py code;
                        py fqdn;
                        py msg;
                        self.set_attr("timeout", timeout);
                        self.set_attr("esmtp_features", py_dict());
                        self.set_attr("command_encoding", S<"ascii">());
                        self.set_attr("source_address", source_address);
                        self.set_attr("_auth_challenge_count", I<0>());
                        if (host.truthy())
                        {
                            auto const items3 = unpack(self.method("connect", { host, port }), 2);
                            code = items3[0];
                            msg = items3[1];
                            if (code != (I<220>()))
                            {
                                self.method("close");
                                py_raise((G<"smtplib.SMTPConnectError">())(code, msg));
                            }
                        }
                        if (!local_hostname.is_none())
                        {
                            self.set_attr("local_hostname", local_hostname);
                        }
                        else
                        {
                            // RFC 2821 says we should use the fqdn in the EHLO/HELO verb, and
                            // if that can't be calculated, that we should use a domain literal
                            // instead (essentially an encoded IP address like [A.B.C.D]).
                            fqdn = (Mod<"socket">().attr("getfqdn"))();
                            if (fqdn.contains((S<".">())))
                            {
                                self.set_attr("local_hostname", fqdn);
                            }
                            else
                            {
                                // We can't find an fqdn hostname, so use a domain literal
                                addr = S<"127.0.0.1">();
                                try
                                {
                                    addr = (Mod<"socket">().attr("gethostbyname"))((Mod<"socket">().attr("gethostname"))());
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler4;
                                    if (matches(handler4.exception, Mod<"socket">().attr("gaierror")))
                                    {
                                    }
                                    else
                                        py_reraise(handler4.exception);
                                }
                                self.set_attr("local_hostname", (S<"[%s]">()) % addr);
                            }
                        }
                        return none();
                    }) },
                { "__enter__", w.method("SMTP", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__exit__", w.method("SMTP", "__exit__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        py code;
                        py message;
                        {
                            std::optional<py> pending5;
                            [[maybe_unused]] int flow5 = 0;
                            [[maybe_unused]] py result5;
                            try
                            {
                                try
                                {
                                    auto const items6 = unpack(self.method("docmd", { S<"QUIT">() }), 2);
                                    code = items6[0];
                                    message = items6[1];
                                    if (code != (I<221>()))
                                    {
                                        py_raise((G<"smtplib.SMTPResponseException">())(code, message));
                                    }
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler5;
                                    if (matches(handler5.exception, G<"smtplib.SMTPServerDisconnected">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler5.exception);
                                }
                            }
                            catch (raised const&)
                            {
                                pending5 = py_take_exception();
                            }
                            finally5:
                            self.method("close");
                            if (pending5)
                                py_reraise(*pending5);
                            if (flow5 == 1)
                                return result5;
                        }
                        return none();
                    }, true) },
                { "set_debuglevel", w.method("SMTP", "set_debuglevel", { py_positional("debuglevel") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py debuglevel = a[1];
                        self.set_attr("debuglevel", debuglevel);
                        return none();
                    }) },
                { "_print_debug", w.method("SMTP", "_print_debug", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        if (self.attr("debuglevel") > (I<1>()))
                        {
                            call_ex((B<"print">()), { (Mod<"datetime">().attr("datetime")).method("now").method("time") }, args, py{}, { { "file", Mod<"sys">().attr("stderr") } });
                        }
                        else
                        {
                            call_ex((B<"print">()), {  }, args, py{}, { { "file", Mod<"sys">().attr("stderr") } });
                        }
                        return none();
                    }, true) },
                { "_get_socket", w.method("SMTP", "_get_socket", { py_positional("host"), py_positional("port"), py_positional("timeout") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py port = a[2];
                        py timeout = a[3];
                        if (((!timeout.is_none()) && (!timeout.truthy())))
                        {
                            py_raise((B<"ValueError">())(S<"Non-blocking socket (timeout=0) is not supported">()));
                        }
                        if (self.attr("debuglevel") > (I<0>()))
                        {
                            self.method("_print_debug", { S<"connect: to">(), py_tuple({ host, port }), self.attr("source_address") });
                        }
                        return (Mod<"socket">().attr("create_connection"))(py_tuple({ host, port }), timeout, self.attr("source_address"));
                    }) },
                { "connect", w.method("SMTP", "connect", { py_positional("host", S<"localhost">()), py_positional("port", I<0>()), py_positional("source_address", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py port = a[2];
                        py source_address = a[3];
                        py code;
                        py i;
                        py msg;
                        if (source_address.truthy())
                        {
                            self.set_attr("source_address", source_address);
                        }
                        if (((!port.truthy()) && (host.method("find", { S<":">() }) == host.method("rfind", { S<":">() }))))
                        {
                            i = host.method("rfind", { S<":">() });
                            if (i >= (I<0>()))
                            {
                                auto const items7 = unpack(py_tuple({ host[B<"slice">()(none(), i, none())], host[B<"slice">()(i + (I<1>()), none(), none())] }), 2);
                                host = items7[0];
                                port = items7[1];
                                try
                                {
                                    port = (B<"int">())(port);
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler8;
                                    if (matches(handler8.exception, B<"ValueError">()))
                                    {
                                        py_raise((B<"OSError">())(S<"nonnumeric port">()));
                                    }
                                    else
                                        py_reraise(handler8.exception);
                                }
                            }
                        }
                        self.set_attr("_host", host);
                        if (!port.truthy())
                        {
                            port = self.attr("default_port");
                        }
                        (Mod<"sys">().attr("audit"))(S<"smtplib.connect">(), self, host, port);
                        self.set_attr("sock", self.method("_get_socket", { host, port, self.attr("timeout") }));
                        self.set_attr("file", none());
                        auto const items9 = unpack(self.method("getreply"), 2);
                        code = items9[0];
                        msg = items9[1];
                        if (self.attr("debuglevel") > (I<0>()))
                        {
                            self.method("_print_debug", { S<"connect:">(), S(msg.repr()) });
                        }
                        return py_tuple({ code, msg });
                    }) },
                { "send", w.method("SMTP", "send", { py_positional("s") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s = a[1];
                        if (self.attr("debuglevel") > (I<0>()))
                        {
                            self.method("_print_debug", { S<"send:">(), S(s.repr()) });
                        }
                        if (self.attr("sock").truthy())
                        {
                            if (isinstance(s, B<"str">()))
                            {
                                // send is used by the 'data' command, where command_encoding
                                // should not be used, but 'data' needs to convert the string to
                                // binary itself anyway, so that's not a problem.
                                s = s.method("encode", { self.attr("command_encoding") });
                            }
                            (Mod<"sys">().attr("audit"))(S<"smtplib.send">(), self, s);
                            try
                            {
                                self.attr("sock").method("sendall", { s });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler10;
                                if (matches(handler10.exception, B<"OSError">()))
                                {
                                    self.method("close");
                                    py_raise((G<"smtplib.SMTPServerDisconnected">())(S<"Server not connected">()));
                                }
                                else
                                    py_reraise(handler10.exception);
                            }
                        }
                        else
                        {
                            py_raise((G<"smtplib.SMTPServerDisconnected">())(S<"please run connect() first">()));
                        }
                        return none();
                    }) },
                { "putcmd", w.method("SMTP", "putcmd", { py_positional("cmd"), py_positional("args", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cmd = a[1];
                        py args = a[2];
                        py s;
                        if (args == (S<"">()))
                        {
                            s = cmd;
                        }
                        else
                        {
                            s = S(cmd.str() + " " + args.str());
                        }
                        if ((s.contains((S<"\r">())) || s.contains((S<"\n">()))))
                        {
                            s = s.method("replace", { S<"\n">(), S<"\\n">() }).method("replace", { S<"\r">(), S<"\\r">() });
                            py_raise((B<"ValueError">())(S(std::string{ "command and arguments contain prohibited newline characters: " } + s.str())));
                        }
                        self.method("send", { S(s.str() + (G<"smtplib.CRLF">()).str()) });
                        return none();
                    }) },
                { "getreply", w.method("SMTP", "getreply", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py code;
                        py e;
                        py errcode;
                        py errmsg;
                        py line;
                        py resp;
                        resp = py_list();
                        if (self.attr("file").is_none())
                        {
                            self.set_attr("file", self.attr("sock").method("makefile", { S<"rb">() }));
                        }
                        while ((I<1>()).truthy())
                        {
                            try
                            {
                                line = self.attr("file").method("readline", { (G<"smtplib._MAXLINE">()) + (I<1>()) });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler12;
                                if (matches(handler12.exception, B<"OSError">()))
                                {
                                    e = handler12.exception;
                                    self.method("close");
                                    py_raise((G<"smtplib.SMTPServerDisconnected">())((S<"Connection unexpectedly closed: ">()) + (B<"str">())(e)));
                                }
                                else
                                    py_reraise(handler12.exception);
                            }
                            if (!line.truthy())
                            {
                                self.method("close");
                                py_raise((G<"smtplib.SMTPServerDisconnected">())(S<"Connection unexpectedly closed">()));
                            }
                            if (self.attr("debuglevel") > (I<0>()))
                            {
                                self.method("_print_debug", { S<"reply:">(), S(line.repr()) });
                            }
                            if (py{ line.size() } > (G<"smtplib._MAXLINE">()))
                            {
                                self.method("close");
                                py_raise((G<"smtplib.SMTPResponseException">())(I<500>(), S<"Line too long.">()));
                            }
                            resp.method("append", { line[B<"slice">()(I<4>(), none(), none())].method("strip", { py{ make_bytes(std::string{ "\x20\x09\x0d\x0a", 4 }) } }) });
                            code = line[B<"slice">()(none(), I<3>(), none())];
                            // Check that the error code is syntactically correct.
                            // Don't attempt to read a continuation line if it is broken.
                            try
                            {
                                errcode = (B<"int">())(code);
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler13;
                                if (matches(handler13.exception, B<"ValueError">()))
                                {
                                    errcode = I<-1>();
                                    break;
                                }
                                else
                                    py_reraise(handler13.exception);
                            }
                            // Check if multiline response.
                            if (line[B<"slice">()(I<3>(), I<4>(), none())] != py{ make_bytes(std::string{ "\x2d", 1 }) })
                            {
                                break;
                            }
                        }
                        errmsg = py{ make_bytes(std::string{ "\x0a", 1 }) }.method("join", { resp });
                        if (self.attr("debuglevel") > (I<0>()))
                        {
                            self.method("_print_debug", { (S<"reply: retcode (%s); Msg: %a">()) % py_tuple({ errcode, errmsg }) });
                        }
                        return py_tuple({ errcode, errmsg });
                    }) },
                { "docmd", w.method("SMTP", "docmd", { py_positional("cmd"), py_positional("args", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cmd = a[1];
                        py args = a[2];
                        self.method("putcmd", { cmd, args });
                        return self.method("getreply");
                    }) },
                { "helo", w.method("SMTP", "helo", { py_positional("name", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py code;
                        py msg;
                        self.method("putcmd", { S<"helo">(), [&]() -> py { py const t = name; return t.truthy() ? t : self.attr("local_hostname"); }() });
                        auto const items14 = unpack(self.method("getreply"), 2);
                        code = items14[0];
                        msg = items14[1];
                        self.set_attr("helo_resp", msg);
                        return py_tuple({ code, msg });
                    }) },
                { "ehlo", w.method("SMTP", "ehlo", { py_positional("name", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py auth_match;
                        py code;
                        py each;
                        py feature;
                        py m;
                        py msg;
                        py params;
                        py resp;
                        self.set_attr("esmtp_features", py_dict());
                        self.method("putcmd", { self.attr("ehlo_msg"), [&]() -> py { py const t = name; return t.truthy() ? t : self.attr("local_hostname"); }() });
                        auto const items15 = unpack(self.method("getreply"), 2);
                        code = items15[0];
                        msg = items15[1];
                        // According to RFC1869 some (badly written)
                        // MTA's will disconnect on an ehlo. Toss an exception if
                        // that happens -ddm
                        if (((code == (I<-1>())) && (py{ msg.size() } == (I<0>()))))
                        {
                            self.method("close");
                            py_raise((G<"smtplib.SMTPServerDisconnected">())(S<"Server not connected">()));
                        }
                        self.set_attr("ehlo_resp", msg);
                        if (code != (I<250>()))
                        {
                            return py_tuple({ code, msg });
                        }
                        self.set_attr("does_esmtp", boolean(true));
                        // parse the ehlo response -ddm
                        if (!isinstance(self.attr("ehlo_resp"), B<"bytes">()))
                            py_raise(B<"AssertionError">()(S(self.attr("ehlo_resp").repr())));
                        resp = self.attr("ehlo_resp").method("decode", { S<"latin-1">() }).method("split", { S<"\n">() });
                        del_item(resp, I<0>());
                        for (py const& item : iterate{ resp })
                        {
                            each = item;
                            // To be able to communicate with as many SMTP servers as possible,
                            // we have to take the old-style auth advertisement into account,
                            // because:
                            // 1) Else our SMTP feature parser gets confused.
                            // 2) There are some servers that only advertise the auth methods we
                            // support using the old style.
                            auth_match = (G<"smtplib.OLDSTYLE_AUTH">()).method("match", { each });
                            if (auth_match.truthy())
                            {
                                // This doesn't remove duplicates, but that's no problem
                                self.attr("esmtp_features").set_item(S<"auth">(), (self.attr("esmtp_features").method("get", { S<"auth">(), S<"">() }) + (S<" ">())) + auth_match.method("groups", { I<0>() })[I<0>()]);
                                continue;
                            }
                            // RFC 1869 requires a space between ehlo keyword and parameters.
                            // It's actually stricter, in that only spaces are allowed between
                            // parameters, but were not going to check for that here.  Note
                            // that the space isn't present if there are no parameters.
                            m = (Mod<"re">().attr("match"))(S<"(?P<feature>[A-Za-z0-9][A-Za-z0-9\\-]*) ?">(), each);
                            if (m.truthy())
                            {
                                feature = m.method("group", { S<"feature">() }).method("lower");
                                params = m.attr("string")[B<"slice">()(m.method("end", { S<"feature">() }), none(), none())].method("strip");
                                if (feature == (S<"auth">()))
                                {
                                    self.attr("esmtp_features").set_item(feature, (self.attr("esmtp_features").method("get", { feature, S<"">() }) + (S<" ">())) + params);
                                }
                                else
                                {
                                    self.attr("esmtp_features").set_item(feature, params);
                                }
                            }
                        }
                        return py_tuple({ code, msg });
                    }) },
                { "has_extn", w.method("SMTP", "has_extn", { py_positional("opt") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py opt = a[1];
                        return boolean(self.attr("esmtp_features").contains(opt.method("lower")));
                    }) },
                { "help", w.method("SMTP", "help", { py_positional("args", S<"">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        self.method("putcmd", { S<"help">(), args });
                        return self.method("getreply")[I<1>()];
                    }) },
                { "rset", w.method("SMTP", "rset", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("command_encoding", S<"ascii">());
                        return self.method("docmd", { S<"rset">() });
                    }) },
                { "_rset", w.method("SMTP", "_rset", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        try
                        {
                            self.method("rset");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler17;
                            if (matches(handler17.exception, G<"smtplib.SMTPServerDisconnected">()))
                            {
                            }
                            else
                                py_reraise(handler17.exception);
                        }
                        return none();
                    }) },
                { "noop", w.method("SMTP", "noop", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("docmd", { S<"noop">() });
                    }) },
                { "mail", w.method("SMTP", "mail", { py_positional("sender"), py_positional("options", py_tuple()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sender = a[1];
                        py options = a[2];
                        py optionlist;
                        optionlist = S<"">();
                        if ((options.truthy() && self.attr("does_esmtp").truthy()))
                        {
                            if ((B<"any">())([&]() -> py { py const comprehension18 = py_list(); py x; for (py const& item : iterate{ options }) { x = item; comprehension18.method("append", { boolean(x.method("lower") == (S<"smtputf8">())) }); } return comprehension18; }()).truthy())
                            {
                                if (self.method("has_extn", { S<"smtputf8">() }).truthy())
                                {
                                    self.set_attr("command_encoding", S<"utf-8">());
                                }
                                else
                                {
                                    py_raise((G<"smtplib.SMTPNotSupportedError">())(S<"SMTPUTF8 not supported by server">()));
                                }
                            }
                            optionlist = (S<" ">()) + (S<" ">()).method("join", { options });
                        }
                        self.method("putcmd", { S<"mail">(), (S<"from:%s%s">()) % py_tuple({ (G<"smtplib.quoteaddr">())(sender), optionlist }) });
                        return self.method("getreply");
                    }) },
                { "rcpt", w.method("SMTP", "rcpt", { py_positional("recip"), py_positional("options", py_tuple()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py recip = a[1];
                        py options = a[2];
                        py optionlist;
                        optionlist = S<"">();
                        if ((options.truthy() && self.attr("does_esmtp").truthy()))
                        {
                            optionlist = (S<" ">()) + (S<" ">()).method("join", { options });
                        }
                        self.method("putcmd", { S<"rcpt">(), (S<"to:%s%s">()) % py_tuple({ (G<"smtplib.quoteaddr">())(recip), optionlist }) });
                        return self.method("getreply");
                    }) },
                { "data", w.method("SMTP", "data", { py_positional("msg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py code;
                        py q;
                        py repl;
                        self.method("putcmd", { S<"data">() });
                        auto const items19 = unpack(self.method("getreply"), 2);
                        code = items19[0];
                        repl = items19[1];
                        if (self.attr("debuglevel") > (I<0>()))
                        {
                            self.method("_print_debug", { S<"data:">(), py_tuple({ code, repl }) });
                        }
                        if (code != (I<354>()))
                        {
                            py_raise((G<"smtplib.SMTPDataError">())(code, repl));
                        }
                        else
                        {
                            if (isinstance(msg, B<"str">()))
                            {
                                msg = (G<"smtplib._fix_eols">())(msg).method("encode", { S<"ascii">() });
                            }
                            q = (G<"smtplib._quote_periods">())(msg);
                            if (q[B<"slice">()(I<-2>(), none(), none())] != (G<"smtplib.bCRLF">()))
                            {
                                q = q + (G<"smtplib.bCRLF">());
                            }
                            q = (q + py{ make_bytes(std::string{ "\x2e", 1 }) }) + (G<"smtplib.bCRLF">());
                            self.method("send", { q });
                            auto const items20 = unpack(self.method("getreply"), 2);
                            code = items20[0];
                            msg = items20[1];
                            if (self.attr("debuglevel") > (I<0>()))
                            {
                                self.method("_print_debug", { S<"data:">(), py_tuple({ code, msg }) });
                            }
                            return py_tuple({ code, msg });
                        }
                        return none();
                    }) },
                { "verify", w.method("SMTP", "verify", { py_positional("address") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py address = a[1];
                        self.method("putcmd", { S<"vrfy">(), (G<"smtplib._addr_only">())(address) });
                        return self.method("getreply");
                    }) },
                { "vrfy", w.method("SMTP", "verify", { py_positional("address") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py address = a[1];
                        self.method("putcmd", { S<"vrfy">(), (G<"smtplib._addr_only">())(address) });
                        return self.method("getreply");
                    }) },
                { "expn", w.method("SMTP", "expn", { py_positional("address") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py address = a[1];
                        self.method("putcmd", { S<"expn">(), (G<"smtplib._addr_only">())(address) });
                        return self.method("getreply");
                    }) },
                { "ehlo_or_helo_if_needed", w.method("SMTP", "ehlo_or_helo_if_needed", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py code;
                        py resp;
                        if ((self.attr("helo_resp").is_none() && self.attr("ehlo_resp").is_none()))
                        {
                            if (!(((I<200>()) <= self.method("ehlo")[I<0>()]) && (self.method("ehlo")[I<0>()] <= (I<299>()))))
                            {
                                auto const items21 = unpack(self.method("helo"), 2);
                                code = items21[0];
                                resp = items21[1];
                                if (!(((I<200>()) <= code) && (code <= (I<299>()))))
                                {
                                    py_raise((G<"smtplib.SMTPHeloError">())(code, resp));
                                }
                            }
                        }
                        return none();
                    }) },
                { "auth", w.method("SMTP", "auth", { py_positional("mechanism"), py_positional("authobject"), py_keyword("initial_response_ok", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mechanism = a[1];
                        py authobject = a[2];
                        py initial_response_ok = a[3];
                        py challenge;
                        py code;
                        py initial_response;
                        py resp;
                        py response;
                        // RFC 4954 allows auth methods to provide an initial response.  Not all
                        // methods support it.  By definition, if they return something other
                        // than None when challenge is None, then they do.  See issue #15014.
                        mechanism = mechanism.method("upper");
                        initial_response = (initial_response_ok.truthy() ? authobject() : none());
                        if (!initial_response.is_none())
                        {
                            response = (Mod<"email.base64mime">().attr("body_encode")).call({ initial_response.method("encode", { S<"ascii">() }) }, { { "eol", S<"">() } });
                            auto const items22 = unpack(self.method("docmd", { S<"AUTH">(), (mechanism + (S<" ">())) + response }), 2);
                            code = items22[0];
                            resp = items22[1];
                            self.set_attr("_auth_challenge_count", I<1>());
                        }
                        else
                        {
                            auto const items23 = unpack(self.method("docmd", { S<"AUTH">(), mechanism }), 2);
                            code = items23[0];
                            resp = items23[1];
                            self.set_attr("_auth_challenge_count", I<0>());
                        }
                        // If server responds with a challenge, send the response.
                        while (code == (I<334>()))
                        {
                            {
                                py const target25 = self;
                                target25.set_attr("_auth_challenge_count", inplace(NEOS_PY_ADD, target25.attr("_auth_challenge_count"), I<1>()));
                            }
                            challenge = (Mod<"base64">().attr("decodebytes"))(resp);
                            response = (Mod<"email.base64mime">().attr("body_encode")).call({ authobject(challenge).method("encode", { S<"ascii">() }) }, { { "eol", S<"">() } });
                            auto const items26 = unpack(self.method("docmd", { response }), 2);
                            code = items26[0];
                            resp = items26[1];
                            // If server keeps sending challenges, something is wrong.
                            if (self.attr("_auth_challenge_count") > (G<"smtplib._MAXCHALLENGE">()))
                            {
                                py_raise((G<"smtplib.SMTPException">())((S<"Server AUTH mechanism infinite loop. Last response: ">()) + S(py_tuple({ code, resp }).repr())));
                            }
                        }
                        if (py_tuple({ I<235>(), I<503>() }).contains(code))
                        {
                            return py_tuple({ code, resp });
                        }
                        py_raise((G<"smtplib.SMTPAuthenticationError">())(code, resp));
                    }) },
                { "auth_cram_md5", w.method("SMTP", "auth_cram_md5", { py_positional("challenge", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py challenge = a[1];
                        py authcode;
                        py password;
                        // CRAM-MD5 does not support initial-response.
                        if (challenge.is_none())
                        {
                            return none();
                        }
                        if (!(G<"smtplib._have_cram_md5_support">()).truthy())
                        {
                            py_raise((G<"smtplib.SMTPException">())(S<"CRAM-MD5 is not supported">()));
                        }
                        password = self.attr("password").method("encode", { S<"ascii">() });
                        authcode = (Mod<"hmac">().attr("HMAC"))(password, challenge, S<"md5">());
                        return S(self.attr("user").str() + " " + authcode.method("hexdigest").str());
                    }) },
                { "auth_plain", w.method("SMTP", "auth_plain", { py_positional("challenge", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py challenge = a[1];
                        return (S<"\u0000%s\u0000%s">()) % py_tuple({ self.attr("user"), self.attr("password") });
                    }) },
                { "auth_login", w.method("SMTP", "auth_login", { py_positional("challenge", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py challenge = a[1];
                        if ((challenge.is_none() || (self.attr("_auth_challenge_count") < (I<2>()))))
                        {
                            return self.attr("user");
                        }
                        else
                        {
                            return self.attr("password");
                        }
                        return none();
                    }) },
                { "login", w.method("SMTP", "login", { py_positional("user"), py_positional("password"), py_keyword("initial_response_ok", boolean(true)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py user = a[1];
                        py password = a[2];
                        py initial_response_ok = a[3];
                        py advertised_authlist;
                        py authlist;
                        py authmethod;
                        py code;
                        py e;
                        py last_exception;
                        py method_name;
                        py preferred_auths;
                        py resp;
                        self.method("ehlo_or_helo_if_needed");
                        if (!self.method("has_extn", { S<"auth">() }).truthy())
                        {
                            py_raise((G<"smtplib.SMTPNotSupportedError">())(S<"SMTP AUTH extension not supported by server.">()));
                        }
                        // Authentication methods the server claims to support
                        advertised_authlist = self.attr("esmtp_features")[S<"auth">()].method("split");
                        // Authentication methods we can handle in our preferred order:
                        if ((G<"smtplib._have_cram_md5_support">()).truthy())
                        {
                            preferred_auths = py_list({ S<"CRAM-MD5">(), S<"PLAIN">(), S<"LOGIN">() });
                        }
                        else
                        {
                            preferred_auths = py_list({ S<"PLAIN">(), S<"LOGIN">() });
                        }
                        // We try the supported authentications in our preferred order, if
                        // the server supports them.
                        authlist = [&]() -> py { py const comprehension27 = py_list(); py auth; for (py const& item : iterate{ preferred_auths }) { auth = item; if (!advertised_authlist.contains(auth)) continue; comprehension27.method("append", { auth }); } return comprehension27; }();
                        if (!authlist.truthy())
                        {
                            py_raise((G<"smtplib.SMTPException">())(S<"No suitable authentication method found.">()));
                        }
                        // Some servers advertise authentication methods they don't really
                        // support, so if authentication fails, we continue until we've tried
                        // all methods.
                        auto const items28 = unpack(py_tuple({ user, password }), 2);
                        self.set_attr("user", items28[0]);
                        self.set_attr("password", items28[1]);
                        for (py const& item : iterate{ authlist })
                        {
                            authmethod = item;
                            method_name = (S<"auth_">()) + authmethod.method("lower").method("replace", { S<"-">(), S<"_">() });
                            try
                            {
                                auto const items31 = unpack(self.method("auth", { authmethod, (B<"getattr">())(self, method_name) }, { { "initial_response_ok", initial_response_ok } }), 2);
                                code = items31[0];
                                resp = items31[1];
                                // 235 == 'Authentication successful'
                                // 503 == 'Error: already authenticated'
                                if (py_tuple({ I<235>(), I<503>() }).contains(code))
                                {
                                    return py_tuple({ code, resp });
                                }
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler30;
                                if (matches(handler30.exception, G<"smtplib.SMTPAuthenticationError">()))
                                {
                                    e = handler30.exception;
                                    last_exception = e;
                                }
                                else
                                    py_reraise(handler30.exception);
                            }
                        }
                        // We could not login successfully.  Return result of last attempt.
                        py_raise(last_exception);
                    }) },
                { "starttls", w.method("SMTP", "starttls", { py_keyword("context", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py context = a[1];
                        py reply;
                        py resp;
                        self.method("ehlo_or_helo_if_needed");
                        if (!self.method("has_extn", { S<"starttls">() }).truthy())
                        {
                            py_raise((G<"smtplib.SMTPNotSupportedError">())(S<"STARTTLS extension not supported by server.">()));
                        }
                        auto const items32 = unpack(self.method("docmd", { S<"STARTTLS">() }), 2);
                        resp = items32[0];
                        reply = items32[1];
                        if (resp == (I<220>()))
                        {
                            if (!(G<"smtplib._have_ssl">()).truthy())
                            {
                                py_raise((B<"RuntimeError">())(S<"No SSL support included in this Python">()));
                            }
                            if (context.is_none())
                            {
                                context = (Mod<"ssl">().attr("_create_stdlib_context"))();
                            }
                            self.set_attr("sock", context.method("wrap_socket", { self.attr("sock") }, { { "server_hostname", self.attr("_host") } }));
                            self.set_attr("file", none());
                            // RFC 3207:
                            // The client MUST discard any knowledge obtained from
                            // the server, such as the list of SMTP service extensions,
                            // which was not obtained from the TLS negotiation itself.
                            self.set_attr("helo_resp", none());
                            self.set_attr("ehlo_resp", none());
                            self.set_attr("esmtp_features", py_dict());
                            self.set_attr("does_esmtp", boolean(false));
                        }
                        else
                        {
                            // RFC 3207:
                            // 501 Syntax error (no parameters allowed)
                            // 454 TLS not available due to temporary reason
                            py_raise((G<"smtplib.SMTPResponseException">())(resp, reply));
                        }
                        return py_tuple({ resp, reply });
                    }) },
                { "sendmail", w.method("SMTP", "sendmail", { py_positional("from_addr"), py_positional("to_addrs"), py_positional("msg"), py_positional("mail_options", py_tuple()), py_positional("rcpt_options", py_tuple()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py from_addr = a[1];
                        py to_addrs = a[2];
                        py msg = a[3];
                        py mail_options = a[4];
                        py rcpt_options = a[5];
                        py code;
                        py each;
                        py esmtp_opts;
                        py option;
                        py resp;
                        py senderrs;
                        self.method("ehlo_or_helo_if_needed");
                        esmtp_opts = py_list();
                        if (isinstance(msg, B<"str">()))
                        {
                            msg = (G<"smtplib._fix_eols">())(msg).method("encode", { S<"ascii">() });
                        }
                        if (self.attr("does_esmtp").truthy())
                        {
                            if (self.method("has_extn", { S<"size">() }).truthy())
                            {
                                esmtp_opts.method("append", { (S<"size=%d">()) % py{ msg.size() } });
                            }
                            for (py const& item : iterate{ mail_options })
                            {
                                option = item;
                                esmtp_opts.method("append", { option });
                            }
                        }
                        auto const items34 = unpack(self.method("mail", { from_addr, esmtp_opts }), 2);
                        code = items34[0];
                        resp = items34[1];
                        if (code != (I<250>()))
                        {
                            if (code == (I<421>()))
                            {
                                self.method("close");
                            }
                            else
                            {
                                self.method("_rset");
                            }
                            py_raise((G<"smtplib.SMTPSenderRefused">())(code, resp, from_addr));
                        }
                        senderrs = py_dict();
                        if (isinstance(to_addrs, B<"str">()))
                        {
                            to_addrs = py_list({ to_addrs });
                        }
                        for (py const& item : iterate{ to_addrs })
                        {
                            each = item;
                            auto const items36 = unpack(self.method("rcpt", { each, rcpt_options }), 2);
                            code = items36[0];
                            resp = items36[1];
                            if (((code != (I<250>())) && (code != (I<251>()))))
                            {
                                senderrs.set_item(each, py_tuple({ code, resp }));
                            }
                            if (code == (I<421>()))
                            {
                                self.method("close");
                                py_raise((G<"smtplib.SMTPRecipientsRefused">())(senderrs));
                            }
                        }
                        if (py{ senderrs.size() } == py{ to_addrs.size() })
                        {
                            // the server refused all our recipients
                            self.method("_rset");
                            py_raise((G<"smtplib.SMTPRecipientsRefused">())(senderrs));
                        }
                        auto const items37 = unpack(self.method("data", { msg }), 2);
                        code = items37[0];
                        resp = items37[1];
                        if (code != (I<250>()))
                        {
                            if (code == (I<421>()))
                            {
                                self.method("close");
                            }
                            else
                            {
                                self.method("_rset");
                            }
                            py_raise((G<"smtplib.SMTPDataError">())(code, resp));
                        }
                        // if we got here then somebody got our mail
                        return senderrs;
                    }) },
                { "send_message", w.method("SMTP", "send_message", { py_positional("msg"), py_positional("from_addr", none()), py_positional("to_addrs", none()), py_positional("mail_options", py_tuple()), py_positional("rcpt_options", py_tuple()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py from_addr = a[2];
                        py to_addrs = a[3];
                        py mail_options = a[4];
                        py rcpt_options = a[5];
                        py addr_fields;
                        py bytesmsg;
                        py flatmsg;
                        py g;
                        py header_prefix;
                        py international;
                        py msg_copy;
                        py resent;
                        // 'Resent-Date' is a mandatory field if the Message is resent (RFC 5322
                        // Section 3.6.6). In such a case, we use the 'Resent-*' fields.  However,
                        // if there is more than one 'Resent-' block there's no way to
                        // unambiguously determine which one is the most recent in all cases,
                        // so rather than guess we raise a ValueError in that case.
                        // 
                        // TODO implement heuristics to guess the correct Resent-* block with an
                        // option allowing the user to enable the heuristics.  (It should be
                        // possible to guess correctly almost all of the time.)
                        self.method("ehlo_or_helo_if_needed");
                        resent = msg.method("get_all", { S<"Resent-Date">() });
                        if (resent.is_none())
                        {
                            header_prefix = S<"">();
                        }
                        else if (py{ resent.size() } == (I<1>()))
                        {
                            header_prefix = S<"Resent-">();
                        }
                        else
                        {
                            py_raise((B<"ValueError">())(S<"message has more than one 'Resent-' header block">()));
                        }
                        if (from_addr.is_none())
                        {
                            // Prefer the sender field per RFC 5322 section 3.6.2.
                            from_addr = (msg.contains((header_prefix + (S<"Sender">()))) ? msg[header_prefix + (S<"Sender">())] : msg[header_prefix + (S<"From">())]);
                            from_addr = (Mod<"email.utils">().attr("getaddresses"))(py_list({ from_addr }))[I<0>()][I<1>()];
                        }
                        if (to_addrs.is_none())
                        {
                            addr_fields = [&]() -> py { py const comprehension38 = py_list(); py f; for (py const& item : iterate{ py_tuple({ msg[header_prefix + (S<"To">())], msg[header_prefix + (S<"Bcc">())], msg[header_prefix + (S<"Cc">())] }) }) { f = item; if (f.is_none()) continue; comprehension38.method("append", { f }); } return comprehension38; }();
                            to_addrs = [&]() -> py { py const comprehension39 = py_list(); py a_; for (py const& item : iterate{ (Mod<"email.utils">().attr("getaddresses"))(addr_fields) }) { a_ = item; comprehension39.method("append", { a_[I<1>()] }); } return comprehension39; }();
                        }
                        // Make a local copy so we can delete the bcc headers.
                        msg_copy = (Mod<"copy">().attr("copy"))(msg);
                        del_item(msg_copy, S<"Bcc">());
                        del_item(msg_copy, S<"Resent-Bcc">());
                        international = boolean(false);
                        try
                        {
                            (S<"">()).method("join", { ((py_list() + py_list({ from_addr })) + B<"list">()(to_addrs)) }).method("encode", { S<"ascii">() });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler40;
                            if (matches(handler40.exception, B<"UnicodeEncodeError">()))
                            {
                                if (!self.method("has_extn", { S<"smtputf8">() }).truthy())
                                {
                                    py_raise((G<"smtplib.SMTPNotSupportedError">())(S<"One or more source or delivery addresses require internationalized email support, but the server does not advertise the required SMTPUTF8 capability">()));
                                }
                                international = boolean(true);
                            }
                            else
                                py_reraise(handler40.exception);
                        }
                        {
                            py const manager41 = (Mod<"io">().attr("BytesIO"))();
                            py const exit41 = pyclass::special_method(manager41, "__exit__");
                            py const value41 = pyclass::special_method(manager41, "__enter__")(manager41);
                            bytesmsg = value41;
                            [[maybe_unused]] int flow41 = 0;
                            [[maybe_unused]] py result41;
                            bool exited41 = false;
                            try
                            {
                                if (international.truthy())
                                {
                                    g = (Mod<"email.generator">().attr("BytesGenerator")).call({ bytesmsg }, { { "policy", msg.attr("policy").method("clone", {  }, { { "utf8", boolean(true) } }) } });
                                    mail_options = B<"tuple">()((((py_list() + B<"list">()(mail_options)) + py_list({ S<"SMTPUTF8">() })) + py_list({ S<"BODY=8BITMIME">() })));
                                }
                                else
                                {
                                    g = (Mod<"email.generator">().attr("BytesGenerator"))(bytesmsg);
                                }
                                g.method("flatten", { msg_copy }, { { "linesep", S<"\r\n">() } });
                                flatmsg = bytesmsg.method("getvalue");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler41;
                                exited41 = true;
                                if (!exit41(manager41, type_of(handler41.exception), handler41.exception, handler41.exception.attr("__traceback__")).truthy())
                                    py_reraise(handler41.exception);
                            }
                            finally41:
                            if (!exited41)
                                exit41(manager41, none(), none(), none());
                            if (flow41 == 1)
                                return result41;
                        }
                        return self.method("sendmail", { from_addr, to_addrs, flatmsg, mail_options, rcpt_options });
                    }) },
                { "close", w.method("SMTP", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file;
                        py sock;
                        {
                            std::optional<py> pending42;
                            [[maybe_unused]] int flow42 = 0;
                            [[maybe_unused]] py result42;
                            try
                            {
                                file = self.attr("file");
                                self.set_attr("file", none());
                                if (file.truthy())
                                {
                                    file.method("close");
                                }
                            }
                            catch (raised const&)
                            {
                                pending42 = py_take_exception();
                            }
                            finally42:
                            sock = self.attr("sock");
                            self.set_attr("sock", none());
                            if (sock.truthy())
                            {
                                sock.method("close");
                            }
                            if (pending42)
                                py_reraise(*pending42);
                            if (flow42 == 1)
                                return result42;
                        }
                        return none();
                    }) },
                { "quit", w.method("SMTP", "quit", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py res;
                        res = self.method("docmd", { S<"quit">() });
                        // A new EHLO is required after reconnecting with connect()
                        py const value43 = none();
                        self.set_attr("ehlo_resp", value43);
                        self.set_attr("helo_resp", value43);
                        self.set_attr("esmtp_features", py_dict());
                        self.set_attr("does_esmtp", boolean(false));
                        self.method("close");
                        return res;
                    }) }, }));

            if ((G<"smtplib._have_ssl">()).truthy())
            {
                define("smtplib", "SMTP_SSL", w.make_class("SMTP_SSL", { G<"smtplib.SMTP">() }, {
                    { "default_port", G<"smtplib.SMTP_SSL_PORT">() },
                    { "__init__", w.method("SMTP_SSL", "__init__", { py_positional("host", S<"">()), py_positional("port", I<0>()), py_positional("local_hostname", none()), py_keyword("timeout", Mod<"socket">().attr("_GLOBAL_DEFAULT_TIMEOUT")), py_keyword("source_address", none()), py_keyword("context", none()) }, [](arguments a) -> py
                        {
                            py self = a[0];
                            py host = a[1];
                            py port = a[2];
                            py local_hostname = a[3];
                            py timeout = a[4];
                            py source_address = a[5];
                            py context = a[6];
                            if (context.is_none())
                            {
                                context = (Mod<"ssl">().attr("_create_stdlib_context"))();
                            }
                            self.set_attr("context", context);
                            (G<"smtplib.SMTP">()).method("__init__", { self, host, port, local_hostname, timeout, source_address });
                            return none();
                        }) },
                    { "_get_socket", w.method("SMTP_SSL", "_get_socket", { py_positional("host"), py_positional("port"), py_positional("timeout") }, [](arguments a) -> py
                        {
                            py self = a[0];
                            py host = a[1];
                            py port = a[2];
                            py timeout = a[3];
                            py new_socket;
                            if (self.attr("debuglevel") > (I<0>()))
                            {
                                self.method("_print_debug", { S<"connect:">(), py_tuple({ host, port }) });
                            }
                            new_socket = (B<"super">()(G<"smtplib.SMTP_SSL">(), self)).method("_get_socket", { host, port, timeout });
                            new_socket = self.attr("context").method("wrap_socket", { new_socket }, { { "server_hostname", self.attr("_host") } });
                            return new_socket;
                        }) }, }));

                (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                    {
                        (G<"smtplib.__all__">()).method("append", { S<"SMTP_SSL">() });
                        return none();
                    }))();
            }
            // 
            // LMTP extension
            // 
            define("smtplib", "LMTP_PORT", I<2003>());
            define("smtplib", "LMTP", w.make_class("LMTP", { G<"smtplib.SMTP">() }, {
                { "ehlo_msg", S<"lhlo">() },
                { "__init__", w.method("LMTP", "__init__", { py_positional("host", S<"">()), py_positional("port", G<"smtplib.LMTP_PORT">()), py_positional("local_hostname", none()), py_positional("source_address", none()), py_positional("timeout", Mod<"socket">().attr("_GLOBAL_DEFAULT_TIMEOUT")) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py port = a[2];
                        py local_hostname = a[3];
                        py source_address = a[4];
                        py timeout = a[5];
                        (B<"super">()(G<"smtplib.LMTP">(), self)).method("__init__", { host, port }, { { "local_hostname", local_hostname }, { "source_address", source_address }, { "timeout", timeout } });
                        return none();
                    }) },
                { "connect", w.method("LMTP", "connect", { py_positional("host", S<"localhost">()), py_positional("port", I<0>()), py_positional("source_address", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py port = a[2];
                        py source_address = a[3];
                        py code;
                        py msg;
                        if (host[I<0>()] != (S<"/">()))
                        {
                            return (B<"super">()(G<"smtplib.LMTP">(), self)).method("connect", { host, port }, { { "source_address", source_address } });
                        }
                        if (((!self.attr("timeout").is_none()) && (!self.attr("timeout").truthy())))
                        {
                            py_raise((B<"ValueError">())(S<"Non-blocking socket (timeout=0) is not supported">()));
                        }
                        // Handle Unix-domain sockets.
                        try
                        {
                            self.set_attr("sock", (Mod<"socket">().attr("socket"))(Mod<"socket">().attr("AF_UNIX"), Mod<"socket">().attr("SOCK_STREAM")));
                            if (!self.attr("timeout").is((Mod<"socket">().attr("_GLOBAL_DEFAULT_TIMEOUT"))))
                            {
                                self.attr("sock").method("settimeout", { self.attr("timeout") });
                            }
                            self.set_attr("file", none());
                            self.attr("sock").method("connect", { host });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler44;
                            if (matches(handler44.exception, B<"OSError">()))
                            {
                                if (self.attr("debuglevel") > (I<0>()))
                                {
                                    self.method("_print_debug", { S<"connect fail:">(), host });
                                }
                                if (self.attr("sock").truthy())
                                {
                                    self.attr("sock").method("close");
                                }
                                self.set_attr("sock", none());
                                py_reraise(handler44.exception);
                            }
                            else
                                py_reraise(handler44.exception);
                        }
                        auto const items45 = unpack(self.method("getreply"), 2);
                        code = items45[0];
                        msg = items45[1];
                        if (self.attr("debuglevel") > (I<0>()))
                        {
                            self.method("_print_debug", { S<"connect:">(), msg });
                        }
                        return py_tuple({ code, msg });
                    }) }, }));

            // Test the sendmail method, which tests most of the others.
            // Note: This always sends to localhost.
            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool smtplib_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_smtplib();
                }
                if (auto const doc = smtplib_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("socket", py{ fn_import("socket") }.release());
                aAttributes.emplace_back("io", py{ fn_import("io") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("email", py{ fn_import("email") }.release());
                aAttributes.emplace_back("email", py{ fn_import("email") }.release());
                aAttributes.emplace_back("email", py{ fn_import("email") }.release());
                aAttributes.emplace_back("base64", py{ fn_import("base64") }.release());
                aAttributes.emplace_back("hmac", py{ fn_import("hmac") }.release());
                aAttributes.emplace_back("copy", py{ fn_import("copy") }.release());
                aAttributes.emplace_back("datetime", py{ fn_import("datetime") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("encode_base64", import_attribute("email.base64mime", "body_encode").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("smtplib", smtplib_module);
    }
}
