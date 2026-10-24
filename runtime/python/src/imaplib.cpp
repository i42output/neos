/*
  imaplib.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/imaplib.py), Copyright (c) 2001-2024 Python Software
  Foundation; All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module imaplib (CPython 3.13's Lib/imaplib.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "imaplib_docs.hpp"

namespace neos::python_runtime::detail::imaplib_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "imaplib.name"))
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
                py_raise("SystemError", std::string{ "imaplib: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"imaplib.name">()))
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
            if (modules_imported("imaplib"))
                return py{ fn_import("imaplib") }.attr("__dict__");
            py const d = py_dict();
            d.set_item(py{ std::string{ "__name__" } }, py{ std::string{ "imaplib" } });
            for (auto const& a : the_state().attributes)
                d.set_item(py{ a.first }, a.second);
            return d;
        }
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "imaplib", imaplib_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- imaplib (Lib/imaplib.py)

        void make_imaplib()
        {
            static module_writer const& w = writer("imaplib");
            // Author: Piers Lauder <piers@cs.su.oz.au> December 1997.
            // 
            // Authentication code contributed by Donn Cave <donn@u.washington.edu> June 1998.
            // String method conversion by ESR, February 2001.
            // GET/SETACL contributed by Anthony Baxter <anthony@interlink.com.au> April 2001.
            // IMAP4_SSL contributed by Tino Lange <Tino.Lange@isg.de> March 2002.
            // GET/SETQUOTA contributed by Andreas Zeidler <az@kreativkombinat.de> June 2002.
            // PROXYAUTH contributed by Rick Holbert <holbert.13@osu.edu> November 2002.
            // GET/SETANNOTATION contributed by Tomas Lindroos <skitta@abo.fi> June 2005.
            define("imaplib", "__version__", S<"2.58">());
            define("imaplib", "HAVE_SSL", boolean(false));
            define("imaplib", "__all__", py_list({ S<"IMAP4">(), S<"IMAP4_stream">(), S<"Internaldate2tuple">(), S<"Int2AP">(), S<"ParseFlags">(), S<"Time2Internaldate">() }));
            // Globals
            define("imaplib", "CRLF", py{ make_bytes(std::string{ "\x0d\x0a", 2 }) });
            define("imaplib", "Debug", I<0>());
            define("imaplib", "IMAP4_PORT", I<143>());
            define("imaplib", "IMAP4_SSL_PORT", I<993>());
            define("imaplib", "AllowedVersions", py_tuple({ S<"IMAP4REV1">(), S<"IMAP4">() }));
            // Maximal line length when calling readline(). This is to prevent
            // reading arbitrary length lines. RFC 3501 (IMAP 4rev1)
            // doesn't specify a line length. RFC 2683 suggests limiting client
            // command lines to 1000 octets and that servers should be prepared
            // to accept command lines up to 8000 octets, so we used to use 10K here.
            // In the modern world (eg: gmail) the response to, for example, a
            // search command can be quite large, so we now use 1M.
            define("imaplib", "_MAXLINE", I<1000000>());
            // Data larger than this will be read in chunks, to prevent extreme
            // overallocation.
            define("imaplib", "_SAFE_BUF_SIZE", (I<1>()) << (I<20>()));
            // Commands
            define("imaplib", "Commands", dict_of({ { S<"APPEND">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"AUTHENTICATE">(), py_tuple({ S<"NONAUTH">() }) }, { S<"CAPABILITY">(), py_tuple({ S<"NONAUTH">(), S<"AUTH">(), S<"SELECTED">(), S<"LOGOUT">() }) }, { S<"CHECK">(), py_tuple({ S<"SELECTED">() }) }, { S<"CLOSE">(), py_tuple({ S<"SELECTED">() }) }, { S<"COPY">(), py_tuple({ S<"SELECTED">() }) }, { S<"CREATE">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"DELETE">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"DELETEACL">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"ENABLE">(), py_tuple({ S<"AUTH">() }) }, { S<"EXAMINE">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"EXPUNGE">(), py_tuple({ S<"SELECTED">() }) }, { S<"FETCH">(), py_tuple({ S<"SELECTED">() }) }, { S<"GETACL">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"GETANNOTATION">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"GETQUOTA">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"GETQUOTAROOT">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"MYRIGHTS">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"LIST">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"LOGIN">(), py_tuple({ S<"NONAUTH">() }) }, { S<"LOGOUT">(), py_tuple({ S<"NONAUTH">(), S<"AUTH">(), S<"SELECTED">(), S<"LOGOUT">() }) }, { S<"LSUB">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"MOVE">(), py_tuple({ S<"SELECTED">() }) }, { S<"NAMESPACE">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"NOOP">(), py_tuple({ S<"NONAUTH">(), S<"AUTH">(), S<"SELECTED">(), S<"LOGOUT">() }) }, { S<"PARTIAL">(), py_tuple({ S<"SELECTED">() }) }, { S<"PROXYAUTH">(), py_tuple({ S<"AUTH">() }) }, { S<"RENAME">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"SEARCH">(), py_tuple({ S<"SELECTED">() }) }, { S<"SELECT">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"SETACL">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"SETANNOTATION">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"SETQUOTA">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"SORT">(), py_tuple({ S<"SELECTED">() }) }, { S<"STARTTLS">(), py_tuple({ S<"NONAUTH">() }) }, { S<"STATUS">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"STORE">(), py_tuple({ S<"SELECTED">() }) }, { S<"SUBSCRIBE">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"THREAD">(), py_tuple({ S<"SELECTED">() }) }, { S<"UID">(), py_tuple({ S<"SELECTED">() }) }, { S<"UNSUBSCRIBE">(), py_tuple({ S<"AUTH">(), S<"SELECTED">() }) }, { S<"UNSELECT">(), py_tuple({ S<"SELECTED">() }) } }));
            // name            valid states
            // Patterns to match server responses
            define("imaplib", "Continuation", (Mod<"re">()).method("compile", { py{ make_bytes(std::string{ "\x5c\x2b\x28\x20\x28\x3f\x50\x3c\x64\x61\x74\x61\x3e\x2e\x2a\x29\x29\x3f", 18 }) } }));
            define("imaplib", "Flags", (Mod<"re">()).method("compile", { py{ make_bytes(std::string{ "\x2e\x2a\x46\x4c\x41\x47\x53\x20\x5c\x28\x28\x3f\x50\x3c\x66\x6c\x61\x67\x73\x3e\x5b\x5e\x5c\x29\x5d\x2a\x29\x5c\x29", 29 }) } }));
            define("imaplib", "InternalDate", (Mod<"re">()).method("compile", { py{ make_bytes(std::string{ "\x2e\x2a\x49\x4e\x54\x45\x52\x4e\x41\x4c\x44\x41\x54\x45\x20\x22\x28\x3f\x50\x3c\x64\x61\x79\x3e\x5b\x20\x30\x31\x32\x33\x5d\x5b\x30\x2d\x39\x5d\x29\x2d\x28\x3f\x50\x3c\x6d\x6f\x6e\x3e\x5b\x41\x2d\x5a\x5d\x5b\x61\x2d\x7a\x5d\x5b\x61\x2d\x7a\x5d\x29\x2d\x28\x3f\x50\x3c\x79\x65\x61\x72\x3e\x5b\x30\x2d\x39\x5d\x5b\x30\x2d\x39\x5d\x5b\x30\x2d\x39\x5d\x5b\x30\x2d\x39\x5d\x29\x20\x28\x3f\x50\x3c\x68\x6f\x75\x72\x3e\x5b\x30\x2d\x39\x5d\x5b\x30\x2d\x39\x5d\x29\x3a\x28\x3f\x50\x3c\x6d\x69\x6e\x3e\x5b\x30\x2d\x39\x5d\x5b\x30\x2d\x39\x5d\x29\x3a\x28\x3f\x50\x3c\x73\x65\x63\x3e\x5b\x30\x2d\x39\x5d\x5b\x30\x2d\x39\x5d\x29\x20\x28\x3f\x50\x3c\x7a\x6f\x6e\x65\x6e\x3e\x5b\x2d\x2b\x5d\x29\x28\x3f\x50\x3c\x7a\x6f\x6e\x65\x68\x3e\x5b\x30\x2d\x39\x5d\x5b\x30\x2d\x39\x5d\x29\x28\x3f\x50\x3c\x7a\x6f\x6e\x65\x6d\x3e\x5b\x30\x2d\x39\x5d\x5b\x30\x2d\x39\x5d\x29\x22", 213 }) } }));
            // Literal is no longer used; kept for backward compatibility.
            define("imaplib", "Literal", (Mod<"re">()).method("compile", { py{ make_bytes(std::string{ "\x2e\x2a\x7b\x28\x3f\x50\x3c\x73\x69\x7a\x65\x3e\x5c\x64\x2b\x29\x7d\x24", 18 }) }, Mod<"re">().attr("ASCII") }));
            define("imaplib", "MapCRLF", (Mod<"re">()).method("compile", { py{ make_bytes(std::string{ "\x5c\x72\x5c\x6e\x7c\x5c\x72\x7c\x5c\x6e", 10 }) } }));
            // We no longer exclude the ']' character from the data portion of the response
            // code, even though it violates the RFC.  Popular IMAP servers such as Gmail
            // allow flags with ']', and there are programs (including imaplib!) that can
            // produce them.  The problem with this is if the 'text' portion of the response
            // includes a ']' we'll parse the response wrong (which is the point of the RFC
            // restriction).  However, that seems less likely to be a problem in practice
            // than being unable to correctly parse flags that include ']' chars, which
            // was reported as a real-world problem in issue #21815.
            define("imaplib", "Response_code", (Mod<"re">()).method("compile", { py{ make_bytes(std::string{ "\x5c\x5b\x28\x3f\x50\x3c\x74\x79\x70\x65\x3e\x5b\x41\x2d\x5a\x2d\x5d\x2b\x29\x28\x20\x28\x3f\x50\x3c\x64\x61\x74\x61\x3e\x2e\x2a\x29\x29\x3f\x5c\x5d", 37 }) } }));
            define("imaplib", "Untagged_response", (Mod<"re">()).method("compile", { py{ make_bytes(std::string{ "\x5c\x2a\x20\x28\x3f\x50\x3c\x74\x79\x70\x65\x3e\x5b\x41\x2d\x5a\x2d\x5d\x2b\x29\x28\x20\x28\x3f\x50\x3c\x64\x61\x74\x61\x3e\x2e\x2a\x29\x29\x3f", 36 }) } }));
            // Untagged_status is no longer used; kept for backward compatibility
            define("imaplib", "Untagged_status", (Mod<"re">()).method("compile", { py{ make_bytes(std::string{ "\x5c\x2a\x20\x28\x3f\x50\x3c\x64\x61\x74\x61\x3e\x5c\x64\x2b\x29\x20\x28\x3f\x50\x3c\x74\x79\x70\x65\x3e\x5b\x41\x2d\x5a\x2d\x5d\x2b\x29\x28\x20\x28\x3f\x50\x3c\x64\x61\x74\x61\x32\x3e\x2e\x2a\x29\x29\x3f", 51 }) }, Mod<"re">().attr("ASCII") }));
            // We compile these in _mode_xxx.
            define("imaplib", "_Literal", py{ make_bytes(std::string{ "\x2e\x2a\x7b\x28\x3f\x50\x3c\x73\x69\x7a\x65\x3e\x5c\x64\x2b\x29\x7d\x24", 18 }) });
            define("imaplib", "_Untagged_status", py{ make_bytes(std::string{ "\x5c\x2a\x20\x28\x3f\x50\x3c\x64\x61\x74\x61\x3e\x5c\x64\x2b\x29\x20\x28\x3f\x50\x3c\x74\x79\x70\x65\x3e\x5b\x41\x2d\x5a\x2d\x5d\x2b\x29\x28\x20\x28\x3f\x50\x3c\x64\x61\x74\x61\x32\x3e\x2e\x2a\x29\x29\x3f", 51 }) });
            // Only NUL, CR and LF are unsafe (they cannot be represented even in
            // a quoted string); other control characters are sent quoted.
            define("imaplib", "_control_chars", (Mod<"re">()).method("compile", { py{ make_bytes(std::string{ "\x5b\x00\x0d\x0a\x5d", 5 }) } }));
            define("imaplib", "_non_astring_char", (Mod<"re">()).method("compile", { py{ make_bytes(std::string{ "\x5b\x28\x29\x7b\x20\x5c\x78\x30\x30\x2d\x5c\x78\x31\x66\x5c\x78\x37\x66\x2d\x5c\x78\x66\x66\x25\x2a\x5c\x5c\x22\x5d", 29 }) } }));
            define("imaplib", "_non_list_char", (Mod<"re">()).method("compile", { py{ make_bytes(std::string{ "\x5b\x28\x29\x7b\x20\x5c\x78\x30\x30\x2d\x5c\x78\x31\x66\x5c\x78\x37\x66\x2d\x5c\x78\x66\x66\x5c\x5c\x22\x5d", 27 }) } }));
            define("imaplib", "_quoted", (Mod<"re">()).method("compile", { py{ make_bytes(std::string{ "\x22\x28\x3f\x3a\x5b\x5e\x22\x5c\x5c\x5d\x7c\x5c\x5c\x2e\x29\x2a\x2b\x22", 18 }) } }));
            define("imaplib", "_paren_depth", w.function("_paren_depth", "_paren_depth", { py_positional("data"), py_positional("depth", I<0>()) }, [](arguments a) -> py
                {
                    py data = a[0];
                    py depth = a[1];
                    data = (G<"imaplib._quoted">()).method("sub", { py{ make_bytes(std::string{ "", 0 }) }, data });
                    return (depth + data.method("count", { py{ make_bytes(std::string{ "\x28", 1 }) } })) - data.method("count", { py{ make_bytes(std::string{ "\x29", 1 }) } });
                }));

                // Overridable methods
                // Utility methods
                // IMAP4 commands
                // Private methods
                // For backward compatibility, an argument already enclosed in double
                // quotes is left unquoted, so that code which quotes arguments itself
                // keeps working.  New code should pass arguments unquoted and let the
                // module quote them as needed.
            define("imaplib", "IMAP4", w.make_class("IMAP4", {  }, {
                { "error", w.make_class("error", { B<"Exception">() }, {
 }, "IMAP4.error") },
                { "abort", w.make_class("abort", { B<"error">() }, {
 }, "IMAP4.abort") },
                { "readonly", w.make_class("readonly", { B<"abort">() }, {
 }, "IMAP4.readonly") },
                { "__init__", w.method("IMAP4", "__init__", { py_positional("host", S<"">()), py_positional("port", G<"imaplib.IMAP4_PORT">()), py_positional("timeout", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py port = a[2];
                        py timeout = a[3];
                        self.set_attr("debug", G<"imaplib.Debug">());
                        self.set_attr("state", S<"LOGOUT">());
                        self.set_attr("literal", none());
                        self.set_attr("tagged_commands", py_dict());
                        self.set_attr("untagged_responses", py_dict());
                        self.set_attr("continuation_response", S<"">());
                        self.set_attr("is_readonly", boolean(false));
                        self.set_attr("tagnum", I<0>());
                        self.set_attr("_tls_established", boolean(false));
                        self.method("_mode_ascii");
                        // Open socket to server.
                        self.method("open", { host, port, timeout });
                        try
                        {
                            self.method("_connect");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler1;
                            if (matches(handler1.exception, B<"Exception">()))
                            {
                                try
                                {
                                    self.method("shutdown");
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler2;
                                    if (matches(handler2.exception, B<"OSError">()))
                                    {
                                    }
                                    else
                                        py_reraise(handler2.exception);
                                }
                                py_reraise(handler1.exception);
                            }
                            else
                                py_reraise(handler1.exception);
                        }
                        return none();
                    }) },
                { "_mode_ascii", w.method("IMAP4", "_mode_ascii", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("utf8_enabled", boolean(false));
                        self.set_attr("_encoding", S<"ascii">());
                        self.set_attr("Literal", (Mod<"re">().attr("compile"))(G<"imaplib._Literal">(), Mod<"re">().attr("ASCII")));
                        self.set_attr("Untagged_status", (Mod<"re">().attr("compile"))(G<"imaplib._Untagged_status">(), Mod<"re">().attr("ASCII")));
                        return none();
                    }) },
                { "_mode_utf8", w.method("IMAP4", "_mode_utf8", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.set_attr("utf8_enabled", boolean(true));
                        self.set_attr("_encoding", S<"utf-8">());
                        self.set_attr("Literal", (Mod<"re">().attr("compile"))(G<"imaplib._Literal">()));
                        self.set_attr("Untagged_status", (Mod<"re">().attr("compile"))(G<"imaplib._Untagged_status">()));
                        return none();
                    }) },
                { "_connect", w.method("IMAP4", "_connect", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py greeting;
                        py version;
                        self.set_attr("tagpre", (G<"imaplib.Int2AP">())((Mod<"random">().attr("randint"))(I<4096>(), I<65535>())));
                        self.set_attr("tagre", (Mod<"re">().attr("compile"))((py{ make_bytes(std::string{ "\x28\x3f\x50\x3c\x74\x61\x67\x3e", 8 }) } + self.attr("tagpre")) + py{ make_bytes(std::string{ "\x5c\x64\x2b\x29\x20\x28\x3f\x50\x3c\x74\x79\x70\x65\x3e\x5b\x41\x2d\x5a\x5d\x2b\x29\x20\x28\x3f\x50\x3c\x64\x61\x74\x61\x3e\x2e\x2a\x29", 34 }) }, Mod<"re">().attr("ASCII")));
                        // Get server welcome message,
                        // request and store CAPABILITY response.
                        if ((B<"__debug__">()).truthy())
                        {
                            self.set_attr("_cmd_log_len", I<10>());
                            self.set_attr("_cmd_log_idx", I<0>());
                            self.set_attr("_cmd_log", py_dict());
                            if (self.attr("debug") >= (I<1>()))
                            {
                                self.method("_mesg", { (S<"imaplib version %s">()) % (G<"imaplib.__version__">()) });
                                self.method("_mesg", { (S<"new IMAP4 connection, tag=%s">()) % self.attr("tagpre") });
                            }
                        }
                        self.set_attr("welcome", self.method("_get_response"));
                        if (self.attr("untagged_responses").contains((S<"PREAUTH">())))
                        {
                            self.set_attr("state", S<"AUTH">());
                        }
                        else if (self.attr("untagged_responses").contains((S<"OK">())))
                        {
                            self.set_attr("state", S<"NONAUTH">());
                        }
                        else
                        {
                            // A continuation ('+') greeting is returned as None; report its
                            // raw line, still held by the last match (gh-108280).
                            greeting = ([&]() -> py { py const t = self.attr("welcome"); return t.truthy() ? t : self.attr("mo").attr("string"); }()).method("decode", { self.attr("_encoding"), S<"replace">() });
                            py_raise(self.method("error", { (S<"invalid greeting: ">()) + greeting }));
                        }
                        // The greeting is not a response to a command.
                        self.method("_refresh_capabilities", {  }, { { "consume", boolean(true) } });
                        if ((B<"__debug__">()).truthy())
                        {
                            if (self.attr("debug") >= (I<3>()))
                            {
                                self.method("_mesg", { (S<"CAPABILITIES: %r">()) % py_tuple({ self.attr("capabilities") }) });
                            }
                        }
                        for (py const& item : iterate{ G<"imaplib.AllowedVersions">() })
                        {
                            version = item;
                            if (!self.attr("capabilities").contains(version))
                            {
                                continue;
                            }
                            self.set_attr("PROTOCOL_VERSION", version);
                            return none();
                        }
                        py_raise(self.method("error", { S<"server not IMAP4 compliant">() }));
                    }) },
                { "__getattr__", w.method("IMAP4", "__getattr__", { py_positional("attr") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attr = a[1];
                        if ((G<"imaplib.Commands">()).contains(attr))
                        {
                            return (B<"getattr">())(self, attr.method("lower"));
                        }
                        py_raise((B<"AttributeError">())((S<"Unknown IMAP4 command: '%s'">()) % attr));
                    }) },
                { "__enter__", w.method("IMAP4", "__enter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__exit__", w.method("IMAP4", "__exit__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py args = a[1];
                        if (self.attr("state") == (S<"LOGOUT">()))
                        {
                            return none();
                        }
                        try
                        {
                            self.method("logout");
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler4;
                            if (matches(handler4.exception, B<"OSError">()))
                            {
                            }
                            else
                                py_reraise(handler4.exception);
                        }
                        return none();
                    }, true) },
                { "_create_socket", w.method("IMAP4", "_create_socket", { py_positional("timeout") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py timeout = a[1];
                        py address;
                        py host;
                        if (((!timeout.is_none()) && (!timeout.truthy())))
                        {
                            py_raise((B<"ValueError">())(S<"Non-blocking socket (timeout=0) is not supported">()));
                        }
                        host = (!self.attr("host").truthy() ? none() : self.attr("host"));
                        (Mod<"sys">().attr("audit"))(S<"imaplib.open">(), self, self.attr("host"), self.attr("port"));
                        address = py_tuple({ host, self.attr("port") });
                        if (!timeout.is_none())
                        {
                            return (Mod<"socket">().attr("create_connection"))(address, timeout);
                        }
                        return (Mod<"socket">().attr("create_connection"))(address);
                    }) },
                { "open", w.method("IMAP4", "open", { py_positional("host", S<"">()), py_positional("port", G<"imaplib.IMAP4_PORT">()), py_positional("timeout", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py port = a[2];
                        py timeout = a[3];
                        self.set_attr("host", host);
                        self.set_attr("port", port);
                        self.set_attr("sock", self.method("_create_socket", { timeout }));
                        self.set_attr("file", self.attr("sock").method("makefile", { S<"rb">() }));
                        return none();
                    }) },
                { "read", w.method("IMAP4", "read", { py_positional("size") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        py cursize;
                        py data;
                        py delta;
                        cursize = (B<"min">())(size, G<"imaplib._SAFE_BUF_SIZE">());
                        data = self.attr("file").method("read", { cursize });
                        while (((cursize < size) && (py{ data.size() } == cursize)))
                        {
                            delta = (B<"min">())(cursize, size - cursize);
                            data = inplace(NEOS_PY_ADD, data, self.attr("file").method("read", { delta }));
                            cursize = inplace(NEOS_PY_ADD, cursize, delta);
                        }
                        return data;
                    }) },
                { "readline", w.method("IMAP4", "readline", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line;
                        line = self.attr("file").method("readline", { (G<"imaplib._MAXLINE">()) + (I<1>()) });
                        if (py{ line.size() } > (G<"imaplib._MAXLINE">()))
                        {
                            py_raise(self.method("error", { (S<"got more than %d bytes">()) % (G<"imaplib._MAXLINE">()) }));
                        }
                        return line;
                    }) },
                { "send", w.method("IMAP4", "send", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        (Mod<"sys">().attr("audit"))(S<"imaplib.send">(), self, data);
                        self.attr("sock").method("sendall", { data });
                        return none();
                    }) },
                { "shutdown", w.method("IMAP4", "shutdown", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py exc;
                        self.attr("file").method("close");
                        {
                            std::optional<py> pending6;
                            [[maybe_unused]] int flow6 = 0;
                            [[maybe_unused]] py result6;
                            try
                            {
                                try
                                {
                                    self.attr("sock").method("shutdown", { Mod<"socket">().attr("SHUT_RDWR") });
                                }
                                catch (raised const&)
                                {
                                    pyclass::handler_scope handler6;
                                    if (matches(handler6.exception, B<"OSError">()))
                                    {
                                        exc = handler6.exception;
                                        // The server might already have closed the connection.
                                        // On Windows, this may result in WSAEINVAL (error 10022):
                                        // An invalid operation was attempted.
                                        if (((exc.attr("errno") != (Mod<"errno">().attr("ENOTCONN"))) && ((B<"getattr">())(exc, S<"winerror">(), I<0>()) != (I<10022>()))))
                                        {
                                            py_reraise(handler6.exception);
                                        }
                                    }
                                    else
                                        py_reraise(handler6.exception);
                                }
                            }
                            catch (raised const&)
                            {
                                pending6 = py_take_exception();
                            }
                            self.attr("sock").method("close");
                            if (pending6)
                                py_reraise(*pending6);
                            if (flow6 == 1)
                                return result6;
                        }
                        return none();
                    }) },
                { "socket", w.method("IMAP4", "socket", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("sock");
                    }) },
                { "recent", w.method("IMAP4", "recent", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dat;
                        py name;
                        py typ;
                        name = S<"RECENT">();
                        auto const items7 = unpack(self.method("_untagged_response", { S<"OK">(), py_list({ none() }), name }), 2);
                        typ = items7[0];
                        dat = items7[1];
                        if (dat[I<-1>()].truthy())
                        {
                            return py_tuple({ typ, dat });
                        }
                        auto const items8 = unpack(self.method("noop"), 2);
                        typ = items8[0];
                        dat = items8[1];
                        return self.method("_untagged_response", { typ, dat, name });
                    }) },
                { "response", w.method("IMAP4", "response", { py_positional("code") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py code = a[1];
                        return self.method("_untagged_response", { code, py_list({ none() }), code.method("upper") });
                    }) },
                { "append", w.method("IMAP4", "append", { py_positional("mailbox"), py_positional("flags"), py_positional("date_time"), py_positional("message") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mailbox = a[1];
                        py flags = a[2];
                        py date_time = a[3];
                        py message = a[4];
                        py literal;
                        py name;
                        name = S<"APPEND">();
                        if (!mailbox.truthy())
                        {
                            mailbox = S<"INBOX">();
                        }
                        if (flags.truthy())
                        {
                            flags = self.method("_set_quote", { flags });
                        }
                        else
                        {
                            flags = none();
                        }
                        if (date_time.truthy())
                        {
                            date_time = (G<"imaplib.Time2Internaldate">())(date_time);
                        }
                        else
                        {
                            date_time = none();
                        }
                        literal = (G<"imaplib.MapCRLF">()).method("sub", { G<"imaplib.CRLF">(), message });
                        self.set_attr("literal", literal);
                        return self.method("_simple_command", { name, self.method("_astring", { mailbox }), flags, date_time });
                    }) },
                { "authenticate", w.method("IMAP4", "authenticate", { py_positional("mechanism"), py_positional("authobject") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mechanism = a[1];
                        py authobject = a[2];
                        py dat;
                        py mech;
                        py typ;
                        mech = mechanism.method("upper");
                        // XXX: shouldn't this code be removed, not commented out?
                        // cap = 'AUTH=%s' % mech
                        // if not cap in self.capabilities:       # Let the server decide!
                        // raise self.error("Server doesn't allow %s authentication." % mech)
                        self.set_attr("literal", (G<"imaplib._Authenticator">())(authobject).attr("process"));
                        auto const items9 = unpack(self.method("_simple_command", { S<"AUTHENTICATE">(), self.method("_atom", { mech }) }), 2);
                        typ = items9[0];
                        dat = items9[1];
                        if (typ != (S<"OK">()))
                        {
                            py_raise(self.method("error", { dat[I<-1>()].method("decode", { S<"utf-8">(), S<"replace">() }) }));
                        }
                        self.set_attr("state", S<"AUTH">());
                        self.method("_refresh_capabilities");
                        return py_tuple({ typ, dat });
                    }) },
                { "capability", w.method("IMAP4", "capability", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dat;
                        py name;
                        py typ;
                        name = S<"CAPABILITY">();
                        auto const items10 = unpack(self.method("_simple_command", { name }), 2);
                        typ = items10[0];
                        dat = items10[1];
                        return self.method("_untagged_response", { typ, dat, name });
                    }) },
                { "check", w.method("IMAP4", "check", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.method("_simple_command", { S<"CHECK">() });
                    }) },
                { "close", w.method("IMAP4", "close", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dat;
                        py typ;
                        {
                            std::optional<py> pending11;
                            [[maybe_unused]] int flow11 = 0;
                            [[maybe_unused]] py result11;
                            try
                            {
                                auto const items12 = unpack(self.method("_simple_command", { S<"CLOSE">() }), 2);
                                typ = items12[0];
                                dat = items12[1];
                            }
                            catch (raised const&)
                            {
                                pending11 = py_take_exception();
                            }
                            self.set_attr("state", S<"AUTH">());
                            if (pending11)
                                py_reraise(*pending11);
                            if (flow11 == 1)
                                return result11;
                        }
                        return py_tuple({ typ, dat });
                    }) },
                { "copy", w.method("IMAP4", "copy", { py_positional("message_set"), py_positional("new_mailbox") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message_set = a[1];
                        py new_mailbox = a[2];
                        return self.method("_simple_command", { S<"COPY">(), self.method("_sequence_set", { message_set }), self.method("_astring", { new_mailbox }) });
                    }) },
                { "create", w.method("IMAP4", "create", { py_positional("mailbox") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mailbox = a[1];
                        return self.method("_simple_command", { S<"CREATE">(), self.method("_astring", { mailbox }) });
                    }) },
                { "delete", w.method("IMAP4", "delete", { py_positional("mailbox") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mailbox = a[1];
                        return self.method("_simple_command", { S<"DELETE">(), self.method("_astring", { mailbox }) });
                    }) },
                { "deleteacl", w.method("IMAP4", "deleteacl", { py_positional("mailbox"), py_positional("who") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mailbox = a[1];
                        py who = a[2];
                        return self.method("_simple_command", { S<"DELETEACL">(), self.method("_astring", { mailbox }), self.method("_astring", { who }) });
                    }) },
                { "enable", w.method("IMAP4", "enable", { py_positional("capability") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py capability = a[1];
                        py data;
                        py typ;
                        if (!self.attr("capabilities").contains((S<"ENABLE">())))
                        {
                            py_raise((G<"imaplib.IMAP4">()).method("error", { S<"Server does not support ENABLE">() }));
                        }
                        auto const items13 = unpack(self.method("_simple_command", { S<"ENABLE">(), capability }), 2);
                        typ = items13[0];
                        data = items13[1];
                        if (((typ == (S<"OK">())) && capability.method("upper").contains((S<"UTF8=ACCEPT">()))))
                        {
                            self.method("_mode_utf8");
                        }
                        return py_tuple({ typ, data });
                    }) },
                { "expunge", w.method("IMAP4", "expunge", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dat;
                        py name;
                        py typ;
                        name = S<"EXPUNGE">();
                        auto const items14 = unpack(self.method("_simple_command", { name }), 2);
                        typ = items14[0];
                        dat = items14[1];
                        return self.method("_untagged_response", { typ, dat, name });
                    }) },
                { "fetch", w.method("IMAP4", "fetch", { py_positional("message_set"), py_positional("message_parts") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message_set = a[1];
                        py message_parts = a[2];
                        py dat;
                        py name;
                        py typ;
                        name = S<"FETCH">();
                        auto const items15 = unpack(self.method("_simple_command", { name, self.method("_sequence_set", { message_set }), self.method("_fetch_parts", { message_parts }) }), 2);
                        typ = items15[0];
                        dat = items15[1];
                        return self.method("_untagged_response", { typ, dat, name });
                    }) },
                { "getacl", w.method("IMAP4", "getacl", { py_positional("mailbox") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mailbox = a[1];
                        py dat;
                        py typ;
                        auto const items16 = unpack(self.method("_simple_command", { S<"GETACL">(), self.method("_astring", { mailbox }) }), 2);
                        typ = items16[0];
                        dat = items16[1];
                        return self.method("_untagged_response", { typ, dat, S<"ACL">() });
                    }) },
                { "getannotation", w.method("IMAP4", "getannotation", { py_positional("mailbox"), py_positional("entry"), py_positional("attribute") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mailbox = a[1];
                        py entry = a[2];
                        py attribute = a[3];
                        py dat;
                        py typ;
                        auto const items17 = unpack(self.method("_simple_command", { S<"GETANNOTATION">(), self.method("_astring", { mailbox }), entry, attribute }), 2);
                        typ = items17[0];
                        dat = items17[1];
                        return self.method("_untagged_response", { typ, dat, S<"ANNOTATION">() });
                    }) },
                { "getquota", w.method("IMAP4", "getquota", { py_positional("root") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py root = a[1];
                        py dat;
                        py typ;
                        auto const items18 = unpack(self.method("_simple_command", { S<"GETQUOTA">(), self.method("_astring", { root }) }), 2);
                        typ = items18[0];
                        dat = items18[1];
                        return self.method("_untagged_response", { typ, dat, S<"QUOTA">() });
                    }) },
                { "getquotaroot", w.method("IMAP4", "getquotaroot", { py_positional("mailbox") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mailbox = a[1];
                        py dat;
                        py quota;
                        py quotaroot;
                        py typ;
                        auto const items19 = unpack(self.method("_simple_command", { S<"GETQUOTAROOT">(), self.method("_astring", { mailbox }) }), 2);
                        typ = items19[0];
                        dat = items19[1];
                        auto const items20 = unpack(self.method("_untagged_response", { typ, dat, S<"QUOTA">() }), 2);
                        typ = items20[0];
                        quota = items20[1];
                        auto const items21 = unpack(self.method("_untagged_response", { typ, dat, S<"QUOTAROOT">() }), 2);
                        typ = items21[0];
                        quotaroot = items21[1];
                        return py_tuple({ typ, py_list({ quotaroot, quota }) });
                    }) },
                { "list", w.method("IMAP4", "list", { py_positional("directory", S<"">()), py_positional("pattern", S<"*">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py directory = a[1];
                        py pattern = a[2];
                        py dat;
                        py name;
                        py typ;
                        name = S<"LIST">();
                        auto const items22 = unpack(self.method("_simple_command", { name, self.method("_astring", { directory }), self.method("_list_mailbox", { pattern }) }), 2);
                        typ = items22[0];
                        dat = items22[1];
                        return self.method("_untagged_response", { typ, dat, name });
                    }) },
                { "login", w.method("IMAP4", "login", { py_positional("user"), py_positional("password") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py user = a[1];
                        py password = a[2];
                        py dat;
                        py typ;
                        auto const items23 = unpack(self.method("_simple_command", { S<"LOGIN">(), self.method("_astring", { user }), self.method("_quote", { password }) }), 2);
                        typ = items23[0];
                        dat = items23[1];
                        if (typ != (S<"OK">()))
                        {
                            py_raise(self.method("error", { dat[I<-1>()].method("decode", { S<"UTF-8">(), S<"replace">() }) }));
                        }
                        self.set_attr("state", S<"AUTH">());
                        self.method("_refresh_capabilities");
                        return py_tuple({ typ, dat });
                    }) },
                { "login_cram_md5", w.method("IMAP4", "login_cram_md5", { py_positional("user"), py_positional("password") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py user = a[1];
                        py password = a[2];
                        auto const items24 = unpack(py_tuple({ user, password }), 2);
                        self.set_attr("user", items24[0]);
                        self.set_attr("password", items24[1]);
                        return self.method("authenticate", { S<"CRAM-MD5">(), self.attr("_CRAM_MD5_AUTH") });
                    }) },
                { "_CRAM_MD5_AUTH", w.method("IMAP4", "_CRAM_MD5_AUTH", { py_positional("challenge") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py challenge = a[1];
                        py authcode;
                        py hmac;
                        py password;
                        hmac = module_of("hmac");
                        if (isinstance(self.attr("password"), B<"str">()))
                        {
                            password = self.attr("password").method("encode", { S<"utf-8">() });
                        }
                        else
                        {
                            password = self.attr("password");
                        }
                        try
                        {
                            authcode = hmac.method("HMAC", { password, challenge, S<"md5">() });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler25;
                            if (matches(handler25.exception, B<"ValueError">()))
                            {
                                py_raise(self.method("error", { S<"CRAM-MD5 authentication is not supported">() }));
                            }
                            else
                                py_reraise(handler25.exception);
                        }
                        return S(self.attr("user").str() + " " + authcode.method("hexdigest").str());
                    }) },
                { "logout", w.method("IMAP4", "logout", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dat;
                        py typ;
                        self.set_attr("state", S<"LOGOUT">());
                        auto const items26 = unpack(self.method("_simple_command", { S<"LOGOUT">() }), 2);
                        typ = items26[0];
                        dat = items26[1];
                        self.method("shutdown");
                        return py_tuple({ typ, dat });
                    }) },
                { "lsub", w.method("IMAP4", "lsub", { py_positional("directory", S<"">()), py_positional("pattern", S<"*">()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py directory = a[1];
                        py pattern = a[2];
                        py dat;
                        py name;
                        py typ;
                        name = S<"LSUB">();
                        auto const items27 = unpack(self.method("_simple_command", { name, self.method("_astring", { directory }), self.method("_list_mailbox", { pattern }) }), 2);
                        typ = items27[0];
                        dat = items27[1];
                        return self.method("_untagged_response", { typ, dat, name });
                    }) },
                { "myrights", w.method("IMAP4", "myrights", { py_positional("mailbox") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mailbox = a[1];
                        py dat;
                        py typ;
                        auto const items28 = unpack(self.method("_simple_command", { S<"MYRIGHTS">(), self.method("_astring", { mailbox }) }), 2);
                        typ = items28[0];
                        dat = items28[1];
                        return self.method("_untagged_response", { typ, dat, S<"MYRIGHTS">() });
                    }) },
                { "namespace", w.method("IMAP4", "namespace", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dat;
                        py name;
                        py typ;
                        name = S<"NAMESPACE">();
                        auto const items29 = unpack(self.method("_simple_command", { name }), 2);
                        typ = items29[0];
                        dat = items29[1];
                        return self.method("_untagged_response", { typ, dat, name });
                    }) },
                { "noop", w.method("IMAP4", "noop", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        if ((B<"__debug__">()).truthy())
                        {
                            if (self.attr("debug") >= (I<3>()))
                            {
                                self.method("_dump_ur", { self.attr("untagged_responses") });
                            }
                        }
                        return self.method("_simple_command", { S<"NOOP">() });
                    }) },
                { "partial", w.method("IMAP4", "partial", { py_positional("message_num"), py_positional("message_part"), py_positional("start"), py_positional("length") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message_num = a[1];
                        py message_part = a[2];
                        py start = a[3];
                        py length = a[4];
                        py dat;
                        py name;
                        py typ;
                        name = S<"PARTIAL">();
                        auto const items30 = unpack(self.method("_simple_command", { name, message_num, message_part, start, length }), 2);
                        typ = items30[0];
                        dat = items30[1];
                        return self.method("_untagged_response", { typ, dat, S<"FETCH">() });
                    }) },
                { "proxyauth", w.method("IMAP4", "proxyauth", { py_positional("user") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py user = a[1];
                        py name;
                        name = S<"PROXYAUTH">();
                        return self.method("_simple_command", { name, self.method("_astring", { user }) });
                    }) },
                { "rename", w.method("IMAP4", "rename", { py_positional("oldmailbox"), py_positional("newmailbox") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py oldmailbox = a[1];
                        py newmailbox = a[2];
                        return self.method("_simple_command", { S<"RENAME">(), self.method("_astring", { oldmailbox }), self.method("_astring", { newmailbox }) });
                    }) },
                { "search", w.method("IMAP4", "search", { py_positional("charset") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py charset = a[1];
                        py criteria = a[2];
                        py dat;
                        py name;
                        py typ;
                        name = S<"SEARCH">();
                        if (!charset.is_none())
                        {
                            if (self.attr("utf8_enabled").truthy())
                            {
                                py_raise((G<"imaplib.IMAP4">()).method("error", { S<"Non-None charset not valid in UTF8 mode">() }));
                            }
                            auto const items31 = unpack(call_ex(self.attr("_simple_command"), { name, S<"CHARSET">(), self.method("_astring", { charset }) }, criteria, py{}), 2);
                            typ = items31[0];
                            dat = items31[1];
                        }
                        else
                        {
                            auto const items32 = unpack(call_ex(self.attr("_simple_command"), { name }, criteria, py{}), 2);
                            typ = items32[0];
                            dat = items32[1];
                        }
                        return self.method("_untagged_response", { typ, dat, name });
                    }, true) },
                { "select", w.method("IMAP4", "select", { py_positional("mailbox", S<"INBOX">()), py_positional("readonly", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mailbox = a[1];
                        py readonly = a[2];
                        py dat;
                        py name;
                        py typ;
                        self.set_attr("untagged_responses", py_dict());
                        self.set_attr("is_readonly", readonly);
                        if (readonly.truthy())
                        {
                            name = S<"EXAMINE">();
                        }
                        else
                        {
                            name = S<"SELECT">();
                        }
                        auto const items33 = unpack(self.method("_simple_command", { name, self.method("_astring", { mailbox }) }), 2);
                        typ = items33[0];
                        dat = items33[1];
                        if (typ != (S<"OK">()))
                        {
                            self.set_attr("state", S<"AUTH">());
                            return py_tuple({ typ, dat });
                        }
                        self.set_attr("state", S<"SELECTED">());
                        if ((self.attr("untagged_responses").contains((S<"READ-ONLY">())) && (!readonly.truthy())))
                        {
                            if ((B<"__debug__">()).truthy())
                            {
                                if (self.attr("debug") >= (I<1>()))
                                {
                                    self.method("_dump_ur", { self.attr("untagged_responses") });
                                }
                            }
                            py_raise(self.method("readonly", { (S<"%r is not writable">()) % py_tuple({ mailbox }) }));
                        }
                        return py_tuple({ typ, self.attr("untagged_responses").method("get", { S<"EXISTS">(), py_list({ none() }) }) });
                    }) },
                { "setacl", w.method("IMAP4", "setacl", { py_positional("mailbox"), py_positional("who"), py_positional("what") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mailbox = a[1];
                        py who = a[2];
                        py what = a[3];
                        return self.method("_simple_command", { S<"SETACL">(), self.method("_astring", { mailbox }), self.method("_astring", { who }), self.method("_astring", { what }) });
                    }) },
                { "setannotation", w.method("IMAP4", "setannotation", { py_positional("mailbox") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mailbox = a[1];
                        py args = a[2];
                        py dat;
                        py typ;
                        auto const items34 = unpack(call_ex(self.attr("_simple_command"), { S<"SETANNOTATION">(), self.method("_astring", { mailbox }) }, args, py{}), 2);
                        typ = items34[0];
                        dat = items34[1];
                        return self.method("_untagged_response", { typ, dat, S<"ANNOTATION">() });
                    }, true) },
                { "setquota", w.method("IMAP4", "setquota", { py_positional("root"), py_positional("limits") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py root = a[1];
                        py limits = a[2];
                        py dat;
                        py typ;
                        auto const items35 = unpack(self.method("_simple_command", { S<"SETQUOTA">(), self.method("_astring", { root }), self.method("_set_quote", { limits }) }), 2);
                        typ = items35[0];
                        dat = items35[1];
                        return self.method("_untagged_response", { typ, dat, S<"QUOTA">() });
                    }) },
                { "sort", w.method("IMAP4", "sort", { py_positional("sort_criteria"), py_positional("charset") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py sort_criteria = a[1];
                        py charset = a[2];
                        py search_criteria = a[3];
                        py dat;
                        py name;
                        py typ;
                        name = S<"SORT">();
                        // if not name in self.capabilities:      # Let the server decide!
                        // raise self.error('unimplemented extension command: %s' % name)
                        sort_criteria = self.method("_set_quote", { sort_criteria });
                        if (!charset.is_none())
                        {
                            charset = self.method("_astring", { charset });
                        }
                        auto const items36 = unpack(call_ex(self.attr("_simple_command"), { name, sort_criteria, charset }, search_criteria, py{}), 2);
                        typ = items36[0];
                        dat = items36[1];
                        return self.method("_untagged_response", { typ, dat, name });
                    }, true) },
                { "starttls", w.method("IMAP4", "starttls", { py_positional("ssl_context", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ssl_context = a[1];
                        py dat;
                        py name;
                        py typ;
                        name = S<"STARTTLS">();
                        if (!(G<"imaplib.HAVE_SSL">()).truthy())
                        {
                            py_raise(self.method("error", { S<"SSL support missing">() }));
                        }
                        if (self.attr("_tls_established").truthy())
                        {
                            py_raise(self.method("abort", { S<"TLS session already established">() }));
                        }
                        if (!self.attr("capabilities").contains(name))
                        {
                            py_raise(self.method("abort", { S<"TLS not supported by server">() }));
                        }
                        // Generate a default SSL context if none was passed.
                        if (ssl_context.is_none())
                        {
                            ssl_context = (Mod<"ssl">().attr("_create_stdlib_context"))();
                        }
                        auto const items37 = unpack(self.method("_simple_command", { name }), 2);
                        typ = items37[0];
                        dat = items37[1];
                        if (typ == (S<"OK">()))
                        {
                            self.set_attr("sock", ssl_context.method("wrap_socket", { self.attr("sock") }, { { "server_hostname", self.attr("host") } }));
                            self.set_attr("file", self.attr("sock").method("makefile", { S<"rb">() }));
                            self.set_attr("_tls_established", boolean(true));
                            self.method("_get_capabilities");
                        }
                        else
                        {
                            py_raise(self.method("error", { S<"Couldn't establish TLS session">() }));
                        }
                        return self.method("_untagged_response", { typ, dat, name });
                    }) },
                { "status", w.method("IMAP4", "status", { py_positional("mailbox"), py_positional("names") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mailbox = a[1];
                        py names = a[2];
                        py dat;
                        py name;
                        py typ;
                        name = S<"STATUS">();
                        // if self.PROTOCOL_VERSION == 'IMAP4':   # Let the server decide!
                        // raise self.error('%s unimplemented in IMAP4 (obtain IMAP4rev1 server, or re-code)' % name)
                        auto const items38 = unpack(self.method("_simple_command", { name, self.method("_astring", { mailbox }), self.method("_set_quote", { names }) }), 2);
                        typ = items38[0];
                        dat = items38[1];
                        return self.method("_untagged_response", { typ, dat, name });
                    }) },
                { "store", w.method("IMAP4", "store", { py_positional("message_set"), py_positional("command"), py_positional("flags") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py message_set = a[1];
                        py command = a[2];
                        py flags = a[3];
                        py dat;
                        py typ;
                        flags = self.method("_set_quote", { flags });
                        auto const items39 = unpack(self.method("_simple_command", { S<"STORE">(), self.method("_sequence_set", { message_set }), command, flags }), 2);
                        typ = items39[0];
                        dat = items39[1];
                        return self.method("_untagged_response", { typ, dat, S<"FETCH">() });
                    }) },
                { "subscribe", w.method("IMAP4", "subscribe", { py_positional("mailbox") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mailbox = a[1];
                        return self.method("_simple_command", { S<"SUBSCRIBE">(), self.method("_astring", { mailbox }) });
                    }) },
                { "thread", w.method("IMAP4", "thread", { py_positional("threading_algorithm"), py_positional("charset") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py threading_algorithm = a[1];
                        py charset = a[2];
                        py search_criteria = a[3];
                        py dat;
                        py name;
                        py typ;
                        name = S<"THREAD">();
                        if (!charset.is_none())
                        {
                            charset = self.method("_astring", { charset });
                        }
                        auto const items40 = unpack(call_ex(self.attr("_simple_command"), { name, self.method("_atom", { threading_algorithm }), charset }, search_criteria, py{}), 2);
                        typ = items40[0];
                        dat = items40[1];
                        return self.method("_untagged_response", { typ, dat, name });
                    }, true) },
                { "uid", w.method("IMAP4", "uid", { py_positional("command") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py command = a[1];
                        py args = a[2];
                        py charset;
                        py dat;
                        py flags;
                        py message_parts;
                        py message_set;
                        py name;
                        py new_mailbox;
                        py op;
                        py search_criteria;
                        py sort_criteria;
                        py threading_algorithm;
                        py typ;
                        command = command.method("upper");
                        if (!(G<"imaplib.Commands">()).contains(command))
                        {
                            py_raise(self.method("error", { (S<"Unknown IMAP4 UID command: %r">()) % py_tuple({ command }) }));
                        }
                        if (!(G<"imaplib.Commands">())[command].contains(self.attr("state")))
                        {
                            py_raise(self.method("error", { (S<"command %s illegal in state %s, only allowed in states %s">()) % py_tuple({ command, self.attr("state"), (S<", ">()).method("join", { (G<"imaplib.Commands">())[command] }) }) }));
                        }
                        name = S<"UID">();
                        if (command == (S<"COPY">()))
                        {
                            auto const items41 = unpack(args, 2);
                            message_set = items41[0];
                            new_mailbox = items41[1];
                            args = py_tuple({ self.method("_sequence_set", { message_set }), self.method("_astring", { new_mailbox }) });
                        }
                        else if (command == (S<"FETCH">()))
                        {
                            auto const items42 = unpack(args, 2);
                            message_set = items42[0];
                            message_parts = items42[1];
                            args = py_tuple({ self.method("_sequence_set", { message_set }), self.method("_fetch_parts", { message_parts }) });
                        }
                        else if (command == (S<"STORE">()))
                        {
                            auto const items43 = unpack(args, 3);
                            message_set = items43[0];
                            op = items43[1];
                            flags = items43[2];
                            args = py_tuple({ self.method("_sequence_set", { message_set }), op, self.method("_set_quote", { flags }) });
                        }
                        else if (command == (S<"SORT">()))
                        {
                            auto const items44 = B<"list">()(args).items();
                            if (items44.size() < 2u)
                                py_raise("ValueError", "not enough values to unpack (expected at least 2, got " + std::to_string(items44.size()) + ")");
                            sort_criteria = items44[0];
                            charset = items44[1];
                            search_criteria = py_list(std::vector<py>(items44.begin() + 2, items44.end() - 0));
                            if (!charset.is_none())
                            {
                                charset = self.method("_astring", { charset });
                            }
                            args = B<"tuple">()((((py_list() + py_list({ self.method("_set_quote", { sort_criteria }) })) + py_list({ charset })) + B<"list">()(search_criteria)));
                        }
                        else if (command == (S<"THREAD">()))
                        {
                            auto const items45 = B<"list">()(args).items();
                            if (items45.size() < 2u)
                                py_raise("ValueError", "not enough values to unpack (expected at least 2, got " + std::to_string(items45.size()) + ")");
                            threading_algorithm = items45[0];
                            charset = items45[1];
                            search_criteria = py_list(std::vector<py>(items45.begin() + 2, items45.end() - 0));
                            if (!charset.is_none())
                            {
                                charset = self.method("_astring", { charset });
                            }
                            args = B<"tuple">()((((py_list() + py_list({ self.method("_atom", { threading_algorithm }) })) + py_list({ charset })) + B<"list">()(search_criteria)));
                        }
                        auto const items46 = unpack(call_ex(self.attr("_simple_command"), { name, self.method("_atom", { command }) }, args, py{}), 2);
                        typ = items46[0];
                        dat = items46[1];
                        if (py_tuple({ S<"SEARCH">(), S<"SORT">(), S<"THREAD">() }).contains(command))
                        {
                            name = command;
                        }
                        else
                        {
                            name = S<"FETCH">();
                        }
                        return self.method("_untagged_response", { typ, dat, name });
                    }, true) },
                { "unsubscribe", w.method("IMAP4", "unsubscribe", { py_positional("mailbox") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mailbox = a[1];
                        return self.method("_simple_command", { S<"UNSUBSCRIBE">(), self.method("_astring", { mailbox }) });
                    }) },
                { "unselect", w.method("IMAP4", "unselect", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data;
                        py typ;
                        {
                            std::optional<py> pending47;
                            [[maybe_unused]] int flow47 = 0;
                            [[maybe_unused]] py result47;
                            try
                            {
                                auto const items48 = unpack(self.method("_simple_command", { S<"UNSELECT">() }), 2);
                                typ = items48[0];
                                data = items48[1];
                            }
                            catch (raised const&)
                            {
                                pending47 = py_take_exception();
                            }
                            self.set_attr("state", S<"AUTH">());
                            if (pending47)
                                py_reraise(*pending47);
                            if (flow47 == 1)
                                return result47;
                        }
                        return py_tuple({ typ, data });
                    }) },
                { "xatom", w.method("IMAP4", "xatom", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py args = a[2];
                        name = name.method("upper");
                        // if not name in self.capabilities:      # Let the server decide!
                        // raise self.error('unknown extension command: %s' % name)
                        if (!(G<"imaplib.Commands">()).contains(name))
                        {
                            (G<"imaplib.Commands">()).set_item(name, py_tuple({ self.attr("state") }));
                        }
                        return call_ex(self.attr("_simple_command"), { name }, args, py{});
                    }, true) },
                { "_append_untagged", w.method("IMAP4", "_append_untagged", { py_positional("typ"), py_positional("dat") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py typ = a[1];
                        py dat = a[2];
                        py ur;
                        if (dat.is_none())
                        {
                            dat = py{ make_bytes(std::string{ "", 0 }) };
                        }
                        ur = self.attr("untagged_responses");
                        if ((B<"__debug__">()).truthy())
                        {
                            if (self.attr("debug") >= (I<5>()))
                            {
                                self.method("_mesg", { (S<"untagged_responses[%s] %s += [\"%r\"]">()) % py_tuple({ typ, py{ ur.method("get", { typ, S<"">() }).size() }, dat }) });
                            }
                        }
                        if (ur.contains(typ))
                        {
                            ur[typ].method("append", { dat });
                        }
                        else
                        {
                            ur.set_item(typ, py_list({ dat }));
                        }
                        return none();
                    }) },
                { "_check_bye", w.method("IMAP4", "_check_bye", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py bye;
                        bye = self.attr("untagged_responses").method("get", { S<"BYE">() });
                        if (bye.truthy())
                        {
                            py_raise(self.method("abort", { bye[I<-1>()].method("decode", { self.attr("_encoding"), S<"replace">() }) }));
                        }
                        return none();
                    }) },
                { "_command", w.method("IMAP4", "_command", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py args = a[2];
                        py arg;
                        py data;
                        py literal;
                        py literator;
                        py tag;
                        py typ;
                        py val;
                        if (!(G<"imaplib.Commands">())[name].contains(self.attr("state")))
                        {
                            self.set_attr("literal", none());
                            py_raise(self.method("error", { (S<"command %s illegal in state %s, only allowed in states %s">()) % py_tuple({ name, self.attr("state"), (S<", ">()).method("join", { (G<"imaplib.Commands">())[name] }) }) }));
                        }
                        for (py const& item : iterate{ py_tuple({ S<"OK">(), S<"NO">(), S<"BAD">() }) })
                        {
                            typ = item;
                            if (self.attr("untagged_responses").contains(typ))
                            {
                                del_item(self.attr("untagged_responses"), typ);
                            }
                        }
                        if ((self.attr("untagged_responses").contains((S<"READ-ONLY">())) && (!self.attr("is_readonly").truthy())))
                        {
                            py_raise(self.method("readonly", { S<"mailbox status changed to READ-ONLY">() }));
                        }
                        tag = self.method("_new_tag");
                        name = (B<"bytes">())(name, self.attr("_encoding"));
                        data = (tag + py{ make_bytes(std::string{ "\x20", 1 }) }) + name;
                        for (py const& item : iterate{ args })
                        {
                            arg = item;
                            if (arg.is_none())
                            {
                                continue;
                            }
                            if (isinstance(arg, B<"str">()))
                            {
                                arg = (B<"bytes">())(arg, self.attr("_encoding"));
                            }
                            if ((G<"imaplib._control_chars">()).method("search", { arg }).truthy())
                            {
                                py_raise((B<"ValueError">())(S<"NUL, CR and LF not allowed in commands">()));
                            }
                            data = (data + py{ make_bytes(std::string{ "\x20", 1 }) }) + arg;
                        }
                        literal = self.attr("literal");
                        if (!literal.is_none())
                        {
                            self.set_attr("literal", none());
                            if ((B<"type">())(literal).is((B<"type">())(self.attr("_command"))))
                            {
                                literator = literal;
                            }
                            else
                            {
                                literator = none();
                                if (self.attr("utf8_enabled").truthy())
                                {
                                    data = data + (B<"bytes">())((S<" UTF8 (~{%s}">()) % py{ literal.size() }, self.attr("_encoding"));
                                    literal = literal + py{ make_bytes(std::string{ "\x29", 1 }) };
                                }
                                else
                                {
                                    data = data + (B<"bytes">())((S<" {%s}">()) % py{ literal.size() }, self.attr("_encoding"));
                                }
                            }
                        }
                        if ((B<"__debug__">()).truthy())
                        {
                            if (self.attr("debug") >= (I<4>()))
                            {
                                self.method("_mesg", { (S<"> %r">()) % data });
                            }
                            else
                            {
                                self.method("_log", { (S<"> %r">()) % data });
                            }
                        }
                        try
                        {
                            self.method("send", { data + (G<"imaplib.CRLF">()) });
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler51;
                            if (matches(handler51.exception, B<"OSError">()))
                            {
                                val = handler51.exception;
                                py_raise(self.method("abort", { (S<"socket error: %s">()) % val }));
                            }
                            else
                                py_reraise(handler51.exception);
                        }
                        if (literal.is_none())
                        {
                            return tag;
                        }
                        while ((I<1>()).truthy())
                        {
                            // Wait for continuation response
                            while (self.method("_get_response").truthy())
                            {
                                if (self.attr("tagged_commands")[tag].truthy())
                                {
                                    return tag;
                                }
                            }
                            // Send literal
                            if (literator.truthy())
                            {
                                literal = literator(self.attr("continuation_response"));
                            }
                            if ((B<"__debug__">()).truthy())
                            {
                                if (self.attr("debug") >= (I<4>()))
                                {
                                    self.method("_mesg", { (S<"write literal size %s">()) % py{ literal.size() } });
                                }
                            }
                            try
                            {
                                self.method("send", { literal });
                                self.method("send", { G<"imaplib.CRLF">() });
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler54;
                                if (matches(handler54.exception, B<"OSError">()))
                                {
                                    val = handler54.exception;
                                    py_raise(self.method("abort", { (S<"socket error: %s">()) % val }));
                                }
                                else
                                    py_reraise(handler54.exception);
                            }
                            if (!literator.truthy())
                            {
                                break;
                            }
                        }
                        return tag;
                    }, true) },
                { "_command_complete", w.method("IMAP4", "_command_complete", { py_positional("name"), py_positional("tag") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py tag = a[2];
                        py data;
                        py logout;
                        py typ;
                        py val;
                        logout = boolean(name == (S<"LOGOUT">()));
                        // BYE is expected after LOGOUT
                        if (!logout.truthy())
                        {
                            self.method("_check_bye");
                        }
                        try
                        {
                            auto const items56 = unpack(self.method("_get_tagged_response", { tag }, { { "expect_bye", logout } }), 2);
                            typ = items56[0];
                            data = items56[1];
                        }
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler55;
                            if (matches(handler55.exception, self.attr("abort")))
                            {
                                val = handler55.exception;
                                py_raise(self.method("abort", { (S<"command: %s => %s">()) % py_tuple({ name, val }) }));
                            }
                            else if (matches(handler55.exception, self.attr("error")))
                            {
                                val = handler55.exception;
                                py_raise(self.method("error", { (S<"command: %s => %s">()) % py_tuple({ name, val }) }));
                            }
                            else
                                py_reraise(handler55.exception);
                        }
                        if (!logout.truthy())
                        {
                            self.method("_check_bye");
                        }
                        if (typ == (S<"BAD">()))
                        {
                            py_raise(self.method("error", { (S<"%s command error: %s %s">()) % py_tuple({ name, typ, data }) }));
                        }
                        return py_tuple({ typ, data });
                    }) },
                { "_get_capabilities", w.method("IMAP4", "_get_capabilities", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dat;
                        py typ;
                        auto const items57 = unpack(self.method("capability"), 2);
                        typ = items57[0];
                        dat = items57[1];
                        if (dat == py_list({ none() }))
                        {
                            py_raise(self.method("error", { S<"no CAPABILITY response from server">() }));
                        }
                        dat = (B<"str">())(dat[I<-1>()], self.attr("_encoding"));
                        dat = dat.method("upper");
                        self.set_attr("capabilities", (B<"tuple">())(dat.method("split")));
                        return none();
                    }) },
                { "_refresh_capabilities", w.method("IMAP4", "_refresh_capabilities", { py_positional("consume", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py consume = a[1];
                        py dat;
                        if (self.attr("untagged_responses").contains((S<"CAPABILITY">())))
                        {
                            dat = self.attr("untagged_responses")[S<"CAPABILITY">()][I<-1>()];
                            if (consume.truthy())
                            {
                                del_item(self.attr("untagged_responses"), S<"CAPABILITY">());
                            }
                            self.set_attr("capabilities", (B<"tuple">())((B<"str">())(dat, self.attr("_encoding")).method("upper").method("split")));
                        }
                        else
                        {
                            self.method("_get_capabilities");
                        }
                        return none();
                    }) },
                { "_get_response", w.method("IMAP4", "_get_response", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py dat;
                        py dat2;
                        py data;
                        py depth;
                        py resp;
                        py size;
                        py tag;
                        py typ;
                        resp = self.method("_get_line");
                        // Skip spurious blank lines between responses (some servers send one
                        // after a literal that ends a response).
                        while (resp == py{ make_bytes(std::string{ "", 0 }) })
                        {
                            resp = self.method("_get_line");
                        }
                        // Command completion response?
                        if (self.method("_match", { self.attr("tagre"), resp }).truthy())
                        {
                            tag = self.attr("mo").method("group", { S<"tag">() });
                            if (!self.attr("tagged_commands").contains(tag))
                            {
                                py_raise(self.method("abort", { (S<"unexpected tagged response: %r">()) % resp }));
                            }
                            typ = self.attr("mo").method("group", { S<"type">() });
                            typ = (B<"str">())(typ, self.attr("_encoding"));
                            dat = self.attr("mo").method("group", { S<"data">() });
                            self.attr("tagged_commands").set_item(tag, py_tuple({ typ, py_list({ dat }) }));
                        }
                        else
                        {
                            dat2 = none();
                            // '*' (untagged) responses?
                            if (!self.method("_match", { G<"imaplib.Untagged_response">(), resp }).truthy())
                            {
                                if (self.method("_match", { self.attr("Untagged_status"), resp }).truthy())
                                {
                                    dat2 = self.attr("mo").method("group", { S<"data2">() });
                                }
                            }
                            if (self.attr("mo").is_none())
                            {
                                // Only other possibility is '+' (continuation) response...
                                if (self.method("_match", { G<"imaplib.Continuation">(), resp }).truthy())
                                {
                                    self.set_attr("continuation_response", self.attr("mo").method("group", { S<"data">() }));
                                    return none();
                                }
                                py_raise(self.method("abort", { (S<"unexpected response: %r">()) % resp }));
                            }
                            typ = self.attr("mo").method("group", { S<"type">() });
                            typ = (B<"str">())(typ, self.attr("_encoding"));
                            dat = self.attr("mo").method("group", { S<"data">() });
                            if (dat.is_none())
                            {
                                dat = py{ make_bytes(std::string{ "", 0 }) };
                            }
                            if (dat2.truthy())
                            {
                                dat = (dat + py{ make_bytes(std::string{ "\x20", 1 }) }) + dat2;
                            }
                            // Is there a literal to come?
                            depth = I<0>();
                            while (self.method("_match", { self.attr("Literal"), dat }).truthy())
                            {
                                // Read literal direct from connection.
                                size = (B<"int">())(self.attr("mo").method("group", { S<"size">() }));
                                if ((B<"__debug__">()).truthy())
                                {
                                    if (self.attr("debug") >= (I<4>()))
                                    {
                                        self.method("_mesg", { (S<"read literal size %s">()) % size });
                                    }
                                }
                                data = self.method("read", { size });
                                // Store response with literal as tuple
                                self.method("_append_untagged", { typ, py_tuple({ dat, data }) });
                                depth = (G<"imaplib._paren_depth">())(dat, depth);
                                // Read trailer - possibly containing another literal
                                dat = self.method("_get_line");
                                // Skip spurious blank lines after a literal, but only inside an
                                // unclosed parenthesis (at top level they end the response).
                                while (((dat == py{ make_bytes(std::string{ "", 0 }) }) && (depth > (I<0>()))))
                                {
                                    dat = self.method("_get_line");
                                }
                            }
                            self.method("_append_untagged", { typ, dat });
                        }
                        // Bracketed response information?
                        if ((py_tuple({ S<"OK">(), S<"NO">(), S<"BAD">() }).contains(typ) && self.method("_match", { G<"imaplib.Response_code">(), dat }).truthy()))
                        {
                            typ = self.attr("mo").method("group", { S<"type">() });
                            typ = (B<"str">())(typ, self.attr("_encoding"));
                            self.method("_append_untagged", { typ, self.attr("mo").method("group", { S<"data">() }) });
                        }
                        if ((B<"__debug__">()).truthy())
                        {
                            if (((self.attr("debug") >= (I<1>())) && py_tuple({ S<"NO">(), S<"BAD">(), S<"BYE">() }).contains(typ)))
                            {
                                self.method("_mesg", { (S<"%s response: %r">()) % py_tuple({ typ, dat }) });
                            }
                        }
                        return resp;
                    }) },
                { "_get_tagged_response", w.method("IMAP4", "_get_tagged_response", { py_positional("tag"), py_positional("expect_bye", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tag = a[1];
                        py expect_bye = a[2];
                        py bye;
                        py result;
                        py typ;
                        py val;
                        while ((I<1>()).truthy())
                        {
                            result = self.attr("tagged_commands")[tag];
                            if (!result.is_none())
                            {
                                del_item(self.attr("tagged_commands"), tag);
                                return result;
                            }
                            if (expect_bye.truthy())
                            {
                                typ = S<"BYE">();
                                bye = self.attr("untagged_responses").method("pop", { typ, none() });
                                if (!bye.is_none())
                                {
                                    // Server replies to the "LOGOUT" command with "BYE"
                                    return py_tuple({ typ, bye });
                                }
                            }
                            // If we've seen a BYE at this point, the socket will be
                            // closed, so report the BYE now.
                            self.method("_check_bye");
                            // Some have reported "unexpected response" exceptions.
                            // Note that ignoring them here causes loops.
                            // Instead, send me details of the unexpected response and
                            // I'll update the code in `_get_response()'.
                            try
                            {
                                self.method("_get_response");
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler62;
                                if (matches(handler62.exception, self.attr("abort")))
                                {
                                    val = handler62.exception;
                                    if ((B<"__debug__">()).truthy())
                                    {
                                        if (self.attr("debug") >= (I<1>()))
                                        {
                                            self.method("print_log");
                                        }
                                    }
                                    py_reraise(handler62.exception);
                                }
                                else
                                    py_reraise(handler62.exception);
                            }
                        }
                        return none();
                    }) },
                { "_get_line", w.method("IMAP4", "_get_line", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line;
                        line = self.method("readline");
                        if (!line.truthy())
                        {
                            py_raise(self.method("abort", { S<"socket error: EOF">() }));
                        }
                        // Protocol mandates all lines terminated by CRLF
                        if (!line.method("endswith", { py{ make_bytes(std::string{ "\x0d\x0a", 2 }) } }).truthy())
                        {
                            py_raise(self.method("abort", { (S<"socket error: unterminated line: %r">()) % line }));
                        }
                        line = line[B<"slice">()(none(), I<-2>(), none())];
                        if ((B<"__debug__">()).truthy())
                        {
                            if (self.attr("debug") >= (I<4>()))
                            {
                                self.method("_mesg", { (S<"< %r">()) % line });
                            }
                            else
                            {
                                self.method("_log", { (S<"< %r">()) % line });
                            }
                        }
                        return line;
                    }) },
                { "_match", w.method("IMAP4", "_match", { py_positional("cre"), py_positional("s") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py cre = a[1];
                        py s = a[2];
                        self.set_attr("mo", cre.method("match", { s }));
                        if ((B<"__debug__">()).truthy())
                        {
                            if (((!self.attr("mo").is_none()) && (self.attr("debug") >= (I<5>()))))
                            {
                                self.method("_mesg", { (S<"\tmatched %r => %r">()) % py_tuple({ cre.attr("pattern"), self.attr("mo").method("groups") }) });
                            }
                        }
                        return boolean(!self.attr("mo").is_none());
                    }) },
                { "_new_tag", w.method("IMAP4", "_new_tag", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tag;
                        tag = self.attr("tagpre") + (B<"bytes">())((B<"str">())(self.attr("tagnum")), self.attr("_encoding"));
                        self.set_attr("tagnum", self.attr("tagnum") + (I<1>()));
                        self.attr("tagged_commands").set_item(tag, none());
                        return tag;
                    }) },
                { "_atom", w.method("IMAP4", "_atom", { py_positional("arg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py arg = a[1];
                        return arg;
                    }) },
                { "_sequence_set", w.method("IMAP4", "_sequence_set", { py_positional("arg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py arg = a[1];
                        return arg;
                    }) },
                { "_set_quote", w.method("IMAP4", "_set_quote", { py_positional("arg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py arg = a[1];
                        if ((arg.truthy() && (arg[I<0>()] == (S<"(">())) && (arg[I<-1>()] == (S<")">()))))
                        {
                            return arg;
                        }
                        return ((S<"(">()) + arg) + (S<")">());
                    }) },
                { "_fetch_parts", w.method("IMAP4", "_fetch_parts", { py_positional("arg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py arg = a[1];
                        if (py_tuple({ S<"ALL">(), S<"FULL">(), S<"FAST">() }).contains(arg.method("upper")))
                        {
                            return arg;
                        }
                        return self.method("_set_quote", { arg });
                    }) },
                { "_quote", w.method("IMAP4", "_quote", { py_positional("arg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py arg = a[1];
                        if (isinstance(arg, B<"str">()))
                        {
                            arg = (B<"bytes">())(arg, self.attr("_encoding"));
                        }
                        arg = arg.method("replace", { py{ make_bytes(std::string{ "\x5c", 1 }) }, py{ make_bytes(std::string{ "\x5c\x5c", 2 }) } });
                        arg = arg.method("replace", { py{ make_bytes(std::string{ "\x22", 1 }) }, py{ make_bytes(std::string{ "\x5c\x22", 2 }) } });
                        return (py{ make_bytes(std::string{ "\x22", 1 }) } + arg) + py{ make_bytes(std::string{ "\x22", 1 }) };
                    }) },
                { "_astring", w.method("IMAP4", "_astring", { py_positional("arg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py arg = a[1];
                        if (isinstance(arg, B<"str">()))
                        {
                            arg = (B<"bytes">())(arg, self.attr("_encoding"));
                        }
                        if ((G<"imaplib._quoted">()).method("fullmatch", { arg }).truthy())
                        {
                            return arg;
                        }
                        if ((arg.truthy() && (G<"imaplib._non_astring_char">()).method("search", { arg }).is_none()))
                        {
                            return arg;
                        }
                        return self.method("_quote", { arg });
                    }) },
                { "_list_mailbox", w.method("IMAP4", "_list_mailbox", { py_positional("arg") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py arg = a[1];
                        if (isinstance(arg, B<"str">()))
                        {
                            arg = (B<"bytes">())(arg, self.attr("_encoding"));
                        }
                        if ((G<"imaplib._quoted">()).method("fullmatch", { arg }).truthy())
                        {
                            return arg;
                        }
                        if ((arg.truthy() && (G<"imaplib._non_list_char">()).method("search", { arg }).is_none()))
                        {
                            return arg;
                        }
                        return self.method("_quote", { arg });
                    }) },
                { "_simple_command", w.method("IMAP4", "_simple_command", { py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py name = a[1];
                        py args = a[2];
                        return self.method("_command_complete", { name, call_ex(self.attr("_command"), { name }, args, py{}) });
                    }, true) },
                { "_untagged_response", w.method("IMAP4", "_untagged_response", { py_positional("typ"), py_positional("dat"), py_positional("name") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py typ = a[1];
                        py dat = a[2];
                        py name = a[3];
                        py data;
                        if (typ == (S<"NO">()))
                        {
                            return py_tuple({ typ, dat });
                        }
                        if (!self.attr("untagged_responses").contains(name))
                        {
                            return py_tuple({ typ, py_list({ none() }) });
                        }
                        data = self.attr("untagged_responses").method("pop", { name });
                        if ((B<"__debug__">()).truthy())
                        {
                            if (self.attr("debug") >= (I<5>()))
                            {
                                self.method("_mesg", { (S<"untagged_responses[%s] => %s">()) % py_tuple({ name, data }) });
                            }
                        }
                        return py_tuple({ typ, data });
                    }) },
                { "_mesg", w.method("IMAP4", "_mesg", { py_positional("s"), py_positional("secs", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py s = a[1];
                        py secs = a[2];
                        py tm;
                        if (secs.is_none())
                        {
                            secs = (Mod<"time">().attr("time"))();
                        }
                        tm = (Mod<"time">().attr("strftime"))(S<"%M:%S">(), (Mod<"time">().attr("localtime"))(secs));
                        (Mod<"sys">().attr("stderr")).method("write", { (S<"  %s.%02d %s\n">()) % py_tuple({ tm, (secs * (I<100>())) % (I<100>()), s }) });
                        (Mod<"sys">().attr("stderr")).method("flush");
                        return none();
                    }) },
                { "_dump_ur", w.method("IMAP4", "_dump_ur", { py_positional("untagged_resp_dict") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py untagged_resp_dict = a[1];
                        py items;
                        if (!untagged_resp_dict.truthy())
                        {
                            return none();
                        }
                        items = [&]() -> py { py const comprehension63 = py_list(); py key; py value; for (py const& item : iterate{ untagged_resp_dict.method("items") }) { auto const items64 = unpack(item, 2); key = items64[0]; value = items64[1]; comprehension63.method("append", { S(key.str() + ": " + value.repr()) }); } return comprehension63; }();
                        self.method("_mesg", { (S<"untagged responses dump:">()) + (S<"\n\t\t">()).method("join", { items }) });
                        return none();
                    }) },
                { "_log", w.method("IMAP4", "_log", { py_positional("line") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py line = a[1];
                        self.attr("_cmd_log").set_item(self.attr("_cmd_log_idx"), py_tuple({ line, (Mod<"time">().attr("time"))() }));
                        {
                            py const target65 = self;
                            target65.set_attr("_cmd_log_idx", inplace(NEOS_PY_ADD, target65.attr("_cmd_log_idx"), I<1>()));
                        }
                        if (self.attr("_cmd_log_idx") >= self.attr("_cmd_log_len"))
                        {
                            self.set_attr("_cmd_log_idx", I<0>());
                        }
                        return none();
                    }) },
                { "print_log", w.method("IMAP4", "print_log", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py i;
                        py n;
                        self.method("_mesg", { (S<"last %d IMAP4 interactions:">()) % py{ self.attr("_cmd_log").size() } });
                        auto const items66 = unpack(py_tuple({ self.attr("_cmd_log_idx"), self.attr("_cmd_log_len") }), 2);
                        i = items66[0];
                        n = items66[1];
                        while (n.truthy())
                        {
                            try
                            {
                                call_ex(self.attr("_mesg"), {  }, self.attr("_cmd_log")[i], py{});
                            }
                            catch (raised const&)
                            {
                                pyclass::handler_scope handler68;
                                {
                                }
                            }
                            i = inplace(NEOS_PY_ADD, i, I<1>());
                            if (i >= self.attr("_cmd_log_len"))
                            {
                                i = I<0>();
                            }
                            n = inplace(NEOS_PY_SUBTRACT, n, I<1>());
                        }
                        return none();
                    }) }, }));

            if ((G<"imaplib.HAVE_SSL">()).truthy())
            {
                define("imaplib", "IMAP4_SSL", w.make_class("IMAP4_SSL", { G<"imaplib.IMAP4">() }, {
                    { "__init__", w.method("IMAP4_SSL", "__init__", { py_positional("host", S<"">()), py_positional("port", G<"imaplib.IMAP4_SSL_PORT">()), py_keyword("ssl_context", none()), py_keyword("timeout", none()) }, [](arguments a) -> py
                        {
                            py self = a[0];
                            py host = a[1];
                            py port = a[2];
                            py ssl_context = a[3];
                            py timeout = a[4];
                            if (ssl_context.is_none())
                            {
                                ssl_context = (Mod<"ssl">().attr("_create_stdlib_context"))();
                            }
                            self.set_attr("ssl_context", ssl_context);
                            (G<"imaplib.IMAP4">()).method("__init__", { self, host, port, timeout });
                            return none();
                        }) },
                    { "_create_socket", w.method("IMAP4_SSL", "_create_socket", { py_positional("timeout") }, [](arguments a) -> py
                        {
                            py self = a[0];
                            py timeout = a[1];
                            py sock;
                            sock = (G<"imaplib.IMAP4">()).method("_create_socket", { self, timeout });
                            return self.attr("ssl_context").method("wrap_socket", { sock }, { { "server_hostname", self.attr("host") } });
                        }) },
                    { "open", w.method("IMAP4_SSL", "open", { py_positional("host", S<"">()), py_positional("port", G<"imaplib.IMAP4_SSL_PORT">()), py_positional("timeout", none()) }, [](arguments a) -> py
                        {
                            py self = a[0];
                            py host = a[1];
                            py port = a[2];
                            py timeout = a[3];
                            (G<"imaplib.IMAP4">()).method("open", { self, host, port, timeout });
                            return none();
                        }) }, }));

                (w.function("<module>", "<module>", {  }, [](arguments a) -> py
                    {
                        (G<"imaplib.__all__">()).method("append", { S<"IMAP4_SSL">() });
                        return none();
                    }))();
            }
            define("imaplib", "IMAP4_stream", w.make_class("IMAP4_stream", { G<"imaplib.IMAP4">() }, {
                { "__init__", w.method("IMAP4_stream", "__init__", { py_positional("command") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py command = a[1];
                        self.set_attr("command", command);
                        (G<"imaplib.IMAP4">()).method("__init__", { self });
                        return none();
                    }) },
                { "open", w.method("IMAP4_stream", "open", { py_positional("host", none()), py_positional("port", none()), py_positional("timeout", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        py port = a[2];
                        py timeout = a[3];
                        self.set_attr("host", none());
                        self.set_attr("port", none());
                        self.set_attr("sock", none());
                        self.set_attr("file", none());
                        self.set_attr("process", (Mod<"subprocess">().attr("Popen")).call({ self.attr("command") }, { { "bufsize", Mod<"io">().attr("DEFAULT_BUFFER_SIZE") }, { "stdin", py{ -1 } /* subprocess.PIPE */ }, { "stdout", py{ -1 } /* subprocess.PIPE */ }, { "shell", boolean(true) }, { "close_fds", boolean(true) } }));
                        self.set_attr("writefile", self.attr("process").attr("stdin"));
                        self.set_attr("readfile", self.attr("process").attr("stdout"));
                        return none();
                    }) },
                { "read", w.method("IMAP4_stream", "read", { py_positional("size") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py size = a[1];
                        return self.attr("readfile").method("read", { size });
                    }) },
                { "readline", w.method("IMAP4_stream", "readline", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("readfile").method("readline");
                    }) },
                { "send", w.method("IMAP4_stream", "send", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        self.attr("writefile").method("write", { data });
                        self.attr("writefile").method("flush");
                        return none();
                    }) },
                { "shutdown", w.method("IMAP4_stream", "shutdown", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("readfile").method("close");
                        self.attr("writefile").method("close");
                        self.attr("process").method("wait");
                        return none();
                    }) }, }));

            define("imaplib", "_Authenticator", w.make_class("_Authenticator", {  }, {
                { "__init__", w.method("_Authenticator", "__init__", { py_positional("mechinst") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py mechinst = a[1];
                        self.set_attr("mech", mechinst);
                        return none();
                    }) },
                { "process", w.method("_Authenticator", "process", { py_positional("data") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py data = a[1];
                        py ret;
                        ret = self.method("mech", { self.method("decode", { data }) });
                        if (ret.is_none())
                        {
                            return py{ make_bytes(std::string{ "\x2a", 1 }) };
                        }
                        return self.method("encode", { ret });
                    }) },
                { "encode", w.method("_Authenticator", "encode", { py_positional("inp") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py inp = a[1];
                        py e;
                        py oup;
                        py t;
                        oup = py{ make_bytes(std::string{ "", 0 }) };
                        if (isinstance(inp, B<"str">()))
                        {
                            inp = inp.method("encode", { S<"utf-8">() });
                        }
                        while (inp.truthy())
                        {
                            if (py{ inp.size() } > (I<48>()))
                            {
                                t = inp[B<"slice">()(none(), I<48>(), none())];
                                inp = inp[B<"slice">()(I<48>(), none(), none())];
                            }
                            else
                            {
                                t = inp;
                                inp = py{ make_bytes(std::string{ "", 0 }) };
                            }
                            e = (Mod<"binascii">().attr("b2a_base64"))(t);
                            if (e.truthy())
                            {
                                oup = oup + e[B<"slice">()(none(), I<-1>(), none())];
                            }
                        }
                        return oup;
                    }) },
                { "decode", w.method("_Authenticator", "decode", { py_positional("inp") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py inp = a[1];
                        if (!inp.truthy())
                        {
                            return py{ make_bytes(std::string{ "", 0 }) };
                        }
                        return (Mod<"binascii">().attr("a2b_base64"))(inp);
                    }) }, }));

            define("imaplib", "Months", (S<" Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec">()).method("split", { S<" ">() }));
            define("imaplib", "Mon2num", [&]() -> py { py const comprehension70 = py_dict(); py n; py s; for (py const& item : iterate{ (B<"enumerate">())((G<"imaplib.Months">())[B<"slice">()(I<1>(), none(), none())]) }) { auto const items71 = unpack(item, 2); n = items71[0]; s = items71[1]; comprehension70.set_item(s.method("encode"), n + (I<1>())); } return comprehension70; }());
            define("imaplib", "Internaldate2tuple", w.function("Internaldate2tuple", "Internaldate2tuple", { py_positional("resp") }, [](arguments a) -> py
                {
                    py resp = a[0];
                    py day;
                    py hour;
                    py min_;
                    py mo;
                    py mon;
                    py sec;
                    py tt;
                    py utc;
                    py year;
                    py zone;
                    py zoneh;
                    py zonem;
                    py zonen;
                    mo = (G<"imaplib.InternalDate">()).method("match", { resp });
                    if (!mo.truthy())
                    {
                        return none();
                    }
                    mon = (G<"imaplib.Mon2num">())[mo.method("group", { S<"mon">() })];
                    zonen = mo.method("group", { S<"zonen">() });
                    day = (B<"int">())(mo.method("group", { S<"day">() }));
                    year = (B<"int">())(mo.method("group", { S<"year">() }));
                    hour = (B<"int">())(mo.method("group", { S<"hour">() }));
                    min_ = (B<"int">())(mo.method("group", { S<"min">() }));
                    sec = (B<"int">())(mo.method("group", { S<"sec">() }));
                    zoneh = (B<"int">())(mo.method("group", { S<"zoneh">() }));
                    zonem = (B<"int">())(mo.method("group", { S<"zonem">() }));
                    // INTERNALDATE timezone must be subtracted to get UT
                    zone = ((zoneh * (I<60>())) + zonem) * (I<60>());
                    if (zonen == py{ make_bytes(std::string{ "\x2d", 1 }) })
                    {
                        zone = -zone;
                    }
                    tt = py_tuple({ year, mon, day, hour, min_, sec, I<-1>(), I<-1>(), I<-1>() });
                    utc = (Mod<"calendar">().attr("timegm"))(tt) - zone;
                    return (Mod<"time">().attr("localtime"))(utc);
                }));

            define("imaplib", "Int2AP", w.function("Int2AP", "Int2AP", { py_positional("num") }, [](arguments a) -> py
                {
                    py num = a[0];
                    py AP;
                    py mod;
                    py val;
                    val = py{ make_bytes(std::string{ "", 0 }) };
                    AP = py{ make_bytes(std::string{ "\x41\x42\x43\x44\x45\x46\x47\x48\x49\x4a\x4b\x4c\x4d\x4e\x4f\x50", 16 }) };
                    num = (B<"int">())((B<"abs">())(num));
                    while (num.truthy())
                    {
                        auto const items73 = unpack((B<"divmod">())(num, I<16>()), 2);
                        num = items73[0];
                        mod = items73[1];
                        val = AP[B<"slice">()(mod, mod + (I<1>()), none())] + val;
                    }
                    return val;
                }));

            define("imaplib", "ParseFlags", w.function("ParseFlags", "ParseFlags", { py_positional("resp") }, [](arguments a) -> py
                {
                    py resp = a[0];
                    py mo;
                    mo = (G<"imaplib.Flags">()).method("match", { resp });
                    if (!mo.truthy())
                    {
                        return py_tuple();
                    }
                    return (B<"tuple">())(mo.method("group", { S<"flags">() }).method("split"));
                }));

            define("imaplib", "Time2Internaldate", w.function("Time2Internaldate", "Time2Internaldate", { py_positional("date_time") }, [](arguments a) -> py
                {
                    py date_time = a[0];
                    py delta;
                    py dst;
                    py dt;
                    py fmt;
                    py gmtoff;
                    if (isinstance(date_time, py_tuple({ B<"int">(), B<"float">() })))
                    {
                        dt = (Mod<"datetime">().attr("datetime")).method("fromtimestamp", { date_time, (Mod<"datetime">().attr("timezone")).attr("utc") }).method("astimezone");
                    }
                    else if (isinstance(date_time, B<"tuple">()))
                    {
                        gmtoff = (B<"getattr">())(date_time, S<"tm_gmtoff">(), none());
                        if (gmtoff.is_none())
                        {
                            if ((Mod<"time">().attr("daylight")).truthy())
                            {
                                dst = date_time[I<8>()];
                                if (dst == (I<-1>()))
                                {
                                    dst = (Mod<"time">().attr("localtime"))((Mod<"time">().attr("mktime"))(date_time))[I<8>()];
                                }
                                gmtoff = -py_tuple({ Mod<"time">().attr("timezone"), Mod<"time">().attr("altzone") })[dst];
                            }
                            else
                            {
                                gmtoff = -(Mod<"time">().attr("timezone"));
                            }
                        }
                        delta = (Mod<"datetime">().attr("timedelta")).call({  }, { { "seconds", gmtoff } });
                        dt = call_ex((Mod<"datetime">().attr("datetime")), {  }, date_time[B<"slice">()(none(), I<6>(), none())], py{}, { { "tzinfo", (Mod<"datetime">().attr("timezone"))(delta) } });
                    }
                    else if (isinstance(date_time, Mod<"datetime">().attr("datetime")))
                    {
                        if (date_time.attr("tzinfo").is_none())
                        {
                            py_raise((B<"ValueError">())(S<"date_time must be aware">()));
                        }
                        dt = date_time;
                    }
                    else if ((isinstance(date_time, B<"str">()) && (py_tuple({ date_time[I<0>()], date_time[I<-1>()] }) == py_tuple({ S<"\"">(), S<"\"">() }))))
                    {
                        return date_time;
                    }
                    else
                    {
                        py_raise((B<"ValueError">())(S<"date_time not of a known type">()));
                    }
                    fmt = (S<"\"%d-{}-%Y %H:%M:%S %z\"">()).method("format", { (G<"imaplib.Months">())[dt.attr("month")] });
                    return dt.method("strftime", { fmt });
                }));

            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool imaplib_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_imaplib();
                }
                if (auto const doc = imaplib_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("binascii", py{ fn_import("binascii") }.release());
                aAttributes.emplace_back("errno", py{ fn_import("errno") }.release());
                aAttributes.emplace_back("random", py{ fn_import("random") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("socket", py{ fn_import("socket") }.release());
                aAttributes.emplace_back("subprocess", py{ fn_import("subprocess") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("time", py{ fn_import("time") }.release());
                aAttributes.emplace_back("calendar", py{ fn_import("calendar") }.release());
                aAttributes.emplace_back("datetime", import_attribute("datetime", "datetime").release());
                aAttributes.emplace_back("timezone", import_attribute("datetime", "timezone").release());
                aAttributes.emplace_back("timedelta", import_attribute("datetime", "timedelta").release());
                aAttributes.emplace_back("DEFAULT_BUFFER_SIZE", import_attribute("io", "DEFAULT_BUFFER_SIZE").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("imaplib", imaplib_module);
    }
}
