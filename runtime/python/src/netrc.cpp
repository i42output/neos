/*
  netrc.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/netrc.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module netrc (CPython 3.13's Lib/netrc.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "netrc_docs.hpp"

namespace neos::python_runtime::detail::netrc_module
{
    namespace
    {
        using pyclass::S;
        using pyclass::I;
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
        // (the module's objects (made once, and never destroyed: they outlive the program's objects))
        struct state
        {
            std::vector<std::pair<std::string, py>> attributes;     ///< (the module's attributes, in order)
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "netrc.name"))
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
                py_raise("SystemError", std::string{ "netrc: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"netrc.name">()))
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
        module_writer const& writer(char const* aModule)
        {
            static module_writer const sWriter{ "netrc", netrc_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- netrc (Lib/netrc.py)

        void make_netrc()
        {
            static module_writer const& w = writer("netrc");
            // Module and documentation by Eric S. Raymond, 21 Dec 1998
            define("netrc", "__all__", py_list({ S<"netrc">(), S<"NetrcParseError">() }));
            define("netrc", "_can_security_check", w.function("_can_security_check", "_can_security_check", {  }, [](arguments a) -> py
                {
                    return [&]() -> py { py const t = boolean((Mod<"os">().attr("name")) == (S<"posix">())); return !t.truthy() ? t : boolean(has_attr(Mod<"os">(), "getuid")); }();
                }));

            define("netrc", "_getpwuid", w.function("_getpwuid", "_getpwuid", { py_positional("uid") }, [](arguments a) -> py
                {
                    py uid = a[0];
                    py pwd;
                    try
                    {
                        pwd = module_of("pwd");
                        return pwd.method("getpwuid", { uid })[I<0>()];
                    }
                    catch (raised const&)
                    {
                        pyclass::handler_scope handler1;
                        if (matches(handler1.exception, py_tuple({ B<"ImportError">(), B<"LookupError">() })))
                        {
                            return S(std::string{ "uid " } + uid.str());
                        }
                        else
                            py_reraise(handler1.exception);
                    }
                    return none();
                }));

            define("netrc", "NetrcParseError", w.make_class("NetrcParseError", { B<"Exception">() }, {
                { "__init__", w.method("NetrcParseError", "__init__", { py_positional("msg"), py_positional("filename", none()), py_positional("lineno", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py msg = a[1];
                        py filename = a[2];
                        py lineno = a[3];
                        self.set_attr("filename", filename);
                        self.set_attr("lineno", lineno);
                        self.set_attr("msg", msg);
                        (B<"Exception">()).method("__init__", { self, msg });
                        return none();
                    }) },
                { "__str__", w.method("NetrcParseError", "__str__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return (S<"%s (%s, line %s)">()) % py_tuple({ self.attr("msg"), self.attr("filename"), self.attr("lineno") });
                    }) }, }));

            define("netrc", "_netrclex", w.make_class("_netrclex", {  }, {
                { "__init__", w.method("_netrclex", "__init__", { py_positional("fp") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        self.set_attr("lineno", I<1>());
                        self.set_attr("instream", fp);
                        self.set_attr("whitespace", S<"\n\t\r ">());
                        self.set_attr("pushback", py_list());
                        return none();
                    }) },
                { "_read_char", w.method("_netrclex", "_read_char", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ch;
                        ch = self.attr("instream").method("read", { I<1>() });
                        if (ch == (S<"\n">()))
                        {
                            {
                                py const target2 = self;
                                target2.set_attr("lineno", inplace(NEOS_PY_ADD, target2.attr("lineno"), I<1>()));
                            }
                        }
                        return ch;
                    }) },
                { "get_token", w.method("_netrclex", "get_token", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py ch;
                        py fiter;
                        py token;
                        if (self.attr("pushback").truthy())
                        {
                            return self.attr("pushback").method("pop", { I<0>() });
                        }
                        token = none();
                        fiter = (B<"iter">())(self.attr("_read_char"), S<"">());
                        for (py const& item : iterate{ fiter })
                        {
                            ch = item;
                            if (self.attr("whitespace").contains(ch))
                            {
                                continue;
                            }
                            token = S<"">();
                            if (ch == (S<"\"">()))
                            {
                                for (py const& item : iterate{ fiter })
                                {
                                    ch = item;
                                    if (ch == (S<"\"">()))
                                    {
                                        return token;
                                    }
                                    else if (ch == (S<"\\">()))
                                    {
                                        ch = self.method("_read_char");
                                    }
                                    token = inplace(NEOS_PY_ADD, token, ch);
                                }
                            }
                            else
                            {
                                if (ch == (S<"\\">()))
                                {
                                    ch = self.method("_read_char");
                                }
                                token = inplace(NEOS_PY_ADD, token, ch);
                                for (py const& item : iterate{ fiter })
                                {
                                    ch = item;
                                    if (self.attr("whitespace").contains(ch))
                                    {
                                        return token;
                                    }
                                    else if (ch == (S<"\\">()))
                                    {
                                        ch = self.method("_read_char");
                                    }
                                    token = inplace(NEOS_PY_ADD, token, ch);
                                }
                            }
                        }
                        return token;
                    }) },
                { "push_token", w.method("_netrclex", "push_token", { py_positional("token") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py token = a[1];
                        self.attr("pushback").method("append", { token });
                        return none();
                    }) }, }));

            define("netrc", "netrc", w.make_class("netrc", {  }, {
                { "__init__", w.method("netrc", "__init__", { py_positional("file", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        py default_netrc;
                        py fp;
                        default_netrc = boolean(file.is_none());
                        if (file.is_none())
                        {
                            file = (Mod<"os">().attr("path")).method("join", { (Mod<"os">().attr("path")).method("expanduser", { S<"~">() }), S<".netrc">() });
                        }
                        self.set_attr("hosts", py_dict());
                        self.set_attr("macros", py_dict());
                        try
                        {
                            {
                                py const manager7 = (B<"open">()).call({ file }, { { "encoding", S<"utf-8">() } });
                                py const exit7 = pyclass::special_method(manager7, "__exit__");
                                py const value7 = pyclass::special_method(manager7, "__enter__")(manager7);
                                fp = value7;
                                [[maybe_unused]] int flow7 = 0;
                                [[maybe_unused]] py result7;
                                bool exited7 = false;
                                try
                                {
                                    self.method("_parse", { file, fp, default_netrc });
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
                        catch (raised const&)
                        {
                            pyclass::handler_scope handler6;
                            if (matches(handler6.exception, B<"UnicodeDecodeError">()))
                            {
                                {
                                    py const manager8 = (B<"open">()).call({ file }, { { "encoding", S<"locale">() } });
                                    py const exit8 = pyclass::special_method(manager8, "__exit__");
                                    py const value8 = pyclass::special_method(manager8, "__enter__")(manager8);
                                    fp = value8;
                                    [[maybe_unused]] int flow8 = 0;
                                    [[maybe_unused]] py result8;
                                    bool exited8 = false;
                                    try
                                    {
                                        self.method("_parse", { file, fp, default_netrc });
                                    }
                                    catch (raised const&)
                                    {
                                        pyclass::handler_scope handler8;
                                        exited8 = true;
                                        if (!exit8(manager8, type_of(handler8.exception), handler8.exception, handler8.exception.attr("__traceback__")).truthy())
                                            py_reraise(handler8.exception);
                                    }
                                    finally8:
                                    if (!exited8)
                                        exit8(manager8, none(), none(), none());
                                    if (flow8 == 1)
                                        return result8;
                                }
                            }
                            else
                                py_reraise(handler6.exception);
                        }
                        return none();
                    }) },
                { "_parse", w.method("netrc", "_parse", { py_positional("file"), py_positional("fp"), py_positional("default_netrc") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py file = a[1];
                        py fp = a[2];
                        py default_netrc = a[3];
                        py account;
                        py entryname;
                        py lexer;
                        py line;
                        py login;
                        py password;
                        py prev_lineno;
                        py saved_lineno;
                        py toplevel;
                        py tt;
                        lexer = (G<"netrc._netrclex">())(fp);
                        while ((I<1>()).truthy())
                        {
                            // Look for a machine, default, or macdef top-level keyword
                            saved_lineno = lexer.attr("lineno");
                            py const value10 = lexer.method("get_token");
                            toplevel = value10;
                            tt = value10;
                            if (tt.is_none())
                            {
                                break;
                            }
                            else if (tt.method("startswith", { S<"#">() }).truthy())
                            {
                                if (((lexer.attr("lineno") == saved_lineno) && (py{ tt.size() } == (I<1>()))))
                                {
                                    lexer.attr("instream").method("readline");
                                }
                                continue;
                            }
                            else if (tt == (S<"machine">()))
                            {
                                entryname = lexer.method("get_token");
                            }
                            else if (tt == (S<"default">()))
                            {
                                entryname = S<"default">();
                            }
                            else if (tt == (S<"macdef">()))
                            {
                                entryname = lexer.method("get_token");
                                self.attr("macros").set_item(entryname, py_list());
                                while ((I<1>()).truthy())
                                {
                                    line = lexer.attr("instream").method("readline");
                                    if (!line.truthy())
                                    {
                                        py_raise((G<"netrc.NetrcParseError">())(S<"Macro definition missing null line terminator.">(), file, lexer.attr("lineno")));
                                    }
                                    if (line == (S<"\n">()))
                                    {
                                        // a macro definition finished with consecutive new-line
                                        // characters. The first \n is encountered by the
                                        // readline() method and this is the second \n.
                                        break;
                                    }
                                    self.attr("macros")[entryname].method("append", { line });
                                }
                                continue;
                            }
                            else
                            {
                                py_raise((G<"netrc.NetrcParseError">())((S<"bad toplevel token %r">()) % tt, file, lexer.attr("lineno")));
                            }
                            if (!entryname.truthy())
                            {
                                py_raise((G<"netrc.NetrcParseError">())((S<"missing %r name">()) % tt, file, lexer.attr("lineno")));
                            }
                            // We're looking at start of an entry for a named machine or default.
                            py const value12 = S<"">();
                            login = value12;
                            account = value12;
                            password = value12;
                            self.attr("hosts").set_item(entryname, py_dict());
                            while ((I<1>()).truthy())
                            {
                                prev_lineno = lexer.attr("lineno");
                                tt = lexer.method("get_token");
                                if (((!tt.is_none()) && tt.method("startswith", { S<"#">() }).truthy()))
                                {
                                    if (lexer.attr("lineno") == prev_lineno)
                                    {
                                        lexer.attr("instream").method("readline");
                                    }
                                    continue;
                                }
                                if (set_of({ none(), S<"machine">(), S<"default">(), S<"macdef">() }).contains(tt))
                                {
                                    self.attr("hosts").set_item(entryname, py_tuple({ login, account, password }));
                                    lexer.method("push_token", { tt });
                                    break;
                                }
                                else if (((tt == (S<"login">())) || (tt == (S<"user">()))))
                                {
                                    login = [&]() -> py { py const t = lexer.method("get_token"); return t.truthy() ? t : S<"">(); }();
                                }
                                else if (tt == (S<"account">()))
                                {
                                    account = [&]() -> py { py const t = lexer.method("get_token"); return t.truthy() ? t : S<"">(); }();
                                }
                                else if (tt == (S<"password">()))
                                {
                                    password = [&]() -> py { py const t = lexer.method("get_token"); return t.truthy() ? t : S<"">(); }();
                                }
                                else
                                {
                                    py_raise((G<"netrc.NetrcParseError">())((S<"bad follower token %r">()) % tt, file, lexer.attr("lineno")));
                                }
                            }
                            self.method("_security_check", { fp, default_netrc, self.attr("hosts")[entryname][I<0>()] });
                        }
                        return none();
                    }) },
                { "_security_check", w.method("netrc", "_security_check", { py_positional("fp"), py_positional("default_netrc"), py_positional("login") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py fp = a[1];
                        py default_netrc = a[2];
                        py login = a[3];
                        py current_user_id;
                        py fowner;
                        py prop;
                        py user;
                        if (((G<"netrc._can_security_check">())().truthy() && default_netrc.truthy() && (login != (S<"anonymous">()))))
                        {
                            prop = (Mod<"os">().attr("fstat"))(fp.method("fileno"));
                            current_user_id = (Mod<"os">().attr("getuid"))();
                            if (prop.attr("st_uid") != current_user_id)
                            {
                                fowner = (G<"netrc._getpwuid">())(prop.attr("st_uid"));
                                user = (G<"netrc._getpwuid">())(current_user_id);
                                py_raise((G<"netrc.NetrcParseError">())(S(std::string{ "~/.netrc file owner (" } + fowner.str() + ", " + user.str() + ") does not match current user")));
                            }
                            if ((prop.attr("st_mode") & ((Mod<"stat">().attr("S_IRWXG")) | (Mod<"stat">().attr("S_IRWXO")))).truthy())
                            {
                                py_raise((G<"netrc.NetrcParseError">())(S<"~/.netrc access too permissive: access permissions must restrict access to only the owner">()));
                            }
                        }
                        return none();
                    }) },
                { "authenticators", w.method("netrc", "authenticators", { py_positional("host") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py host = a[1];
                        if (self.attr("hosts").contains(host))
                        {
                            return self.attr("hosts")[host];
                        }
                        else if (self.attr("hosts").contains((S<"default">())))
                        {
                            return self.attr("hosts")[S<"default">()];
                        }
                        else
                        {
                            return none();
                        }
                        return none();
                    }) },
                { "__repr__", w.method("netrc", "__repr__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py attrs;
                        py host;
                        py line;
                        py macro;
                        py rep;
                        rep = S<"">();
                        for (py const& item : iterate{ self.attr("hosts").method("keys") })
                        {
                            host = item;
                            attrs = self.attr("hosts")[host];
                            rep = inplace(NEOS_PY_ADD, rep, S(std::string{ "machine " } + host.str() + "\n\tlogin " + attrs[I<0>()].str() + "\n"));
                            if (attrs[I<1>()].truthy())
                            {
                                rep = inplace(NEOS_PY_ADD, rep, S(std::string{ "\taccount " } + attrs[I<1>()].str() + "\n"));
                            }
                            rep = inplace(NEOS_PY_ADD, rep, S(std::string{ "\tpassword " } + attrs[I<2>()].str() + "\n"));
                        }
                        for (py const& item : iterate{ self.attr("macros").method("keys") })
                        {
                            macro = item;
                            rep = inplace(NEOS_PY_ADD, rep, S(std::string{ "macdef " } + macro.str() + "\n"));
                            for (py const& item : iterate{ self.attr("macros")[macro] })
                            {
                                line = item;
                                rep = inplace(NEOS_PY_ADD, rep, line);
                            }
                            rep = inplace(NEOS_PY_ADD, rep, S<"\n">());
                        }
                        return rep;
                    }) }, }));

            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool netrc_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_netrc();
                }
                if (auto const doc = netrc_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("stat", py{ fn_import("stat") }.release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("netrc", netrc_module);
    }
}
