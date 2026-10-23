/*
  shlex.cpp

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

  Portions of this file are derived from CPython 3.13 (Lib/shlex.py), Copyright (c) 2001-2024 Python Software Foundation;
  All Rights Reserved. They are used under the Python Software Foundation License Version 2 (see
  runtime/python/LICENSE.PSF). Changes: translated from Python to C++ for the neos Python runtime plugin and adapted to
  its object model (see runtime/python/NOTICE).
*/

// The module shlex (CPython 3.13's Lib/shlex.py, translated from Python by py2neos, a Python-to-C++ translator): its classes are made (by type)
// of closures (pyclass.hpp), so a program's subclasses override their methods.

#include <map>
#include <string>
#include <vector>
#include "python.hpp"
#include "objects.hpp"
#include "values.hpp"
#include "pyclass.hpp"
#include "shlex_docs.hpp"

namespace neos::python_runtime::detail::shlex_module
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
            std::map<std::string, py, std::less<>> objects;         ///< (its objects, by qualified name (e.g. "shlex.name"))
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
                py_raise("SystemError", std::string{ "shlex: no object " } + aQualifiedName);
            return o->second;
        }
        // (an object of the module: its entry, found once (G<"shlex.name">()))
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
            static module_writer const sWriter{ "shlex", shlex_docs::doc };
            (void)aModule;
            return sWriter;
        }
        // --- shlex (Lib/shlex.py)

        void make_shlex()
        {
            static module_writer const& w = writer("shlex");
            // Module and documentation by Eric S. Raymond, 21 Dec 1998
            // Input stacking and error message cleanup added by ESR, March 2000
            // push_source() and pop_source() made explicit by ESR, January 2001.
            // Posix compliance, split(), string arguments, and
            // iterator interface by Gustavo Niemeyer, April 2003.
            // changes to tokenize more like Posix shells by Vinay Sajip, July 2016.
            define("shlex", "__all__", py_list({ S<"shlex">(), S<"split">(), S<"quote">(), S<"join">() }));
            define("shlex", "shlex", w.make_class("shlex", {  }, {
                { "__init__", w.method("shlex", "__init__", { py_positional("instream", none()), py_positional("infile", none()), py_positional("posix", boolean(false)), py_positional("punctuation_chars", boolean(false)) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py instream = a[1];
                        py infile = a[2];
                        py posix = a[3];
                        py punctuation_chars = a[4];
                        py t;
                        if (isinstance(instream, B<"str">()))
                        {
                            instream = (Mod<"io">().attr("StringIO"))(instream);
                        }
                        if (!instream.is_none())
                        {
                            self.set_attr("instream", instream);
                            self.set_attr("infile", infile);
                        }
                        else
                        {
                            self.set_attr("instream", Mod<"sys">().attr("stdin"));
                            self.set_attr("infile", none());
                        }
                        self.set_attr("posix", posix);
                        if (posix.truthy())
                        {
                            self.set_attr("eof", none());
                        }
                        else
                        {
                            self.set_attr("eof", S<"">());
                        }
                        self.set_attr("commenters", S<"#">());
                        self.set_attr("wordchars", S<"abcdfeghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_">());
                        if (self.attr("posix").truthy())
                        {
                            {
                                py const target1 = self;
                                target1.set_attr("wordchars", inplace(NEOS_PY_ADD, target1.attr("wordchars"), S<"ßàáâãäåæçèéêëìíîïðñòóôõöøùúûüýþÿÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÐÑÒÓÔÕÖØÙÚÛÜÝÞ">()));
                            }
                        }
                        self.set_attr("whitespace", S<" \t\r\n">());
                        self.set_attr("whitespace_split", boolean(false));
                        self.set_attr("quotes", S<"'\"">());
                        self.set_attr("escape", S<"\\">());
                        self.set_attr("escapedquotes", S<"\"">());
                        self.set_attr("state", S<" ">());
                        self.set_attr("pushback", (Mod<"collections">().attr("deque"))());
                        self.set_attr("lineno", I<1>());
                        self.set_attr("debug", I<0>());
                        self.set_attr("token", S<"">());
                        self.set_attr("filestack", (Mod<"collections">().attr("deque"))());
                        self.set_attr("source", none());
                        if (!punctuation_chars.truthy())
                        {
                            punctuation_chars = S<"">();
                        }
                        else if (punctuation_chars.is(boolean(true)))
                        {
                            punctuation_chars = S<"();<>|&">();
                        }
                        self.set_attr("_punctuation_chars", punctuation_chars);
                        if (punctuation_chars.truthy())
                        {
                            // _pushback_chars is a push back queue used by lookahead logic
                            self.set_attr("_pushback_chars", (Mod<"collections">().attr("deque"))());
                            // these chars added because allowed in file names, args, wildcards
                            {
                                py const target2 = self;
                                target2.set_attr("wordchars", inplace(NEOS_PY_ADD, target2.attr("wordchars"), S<"~-./*?=">()));
                            }
                            // remove any punctuation chars from wordchars
                            t = self.attr("wordchars").method("maketrans", { (B<"dict">()).method("fromkeys", { punctuation_chars }) });
                            self.set_attr("wordchars", self.attr("wordchars").method("translate", { t }));
                        }
                        return none();
                    }) },
                { "punctuation_chars", B<"property">()(w.method("shlex", "punctuation_chars", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self.attr("_punctuation_chars");
                    })) },
                { "push_token", w.method("shlex", "push_token", { py_positional("tok") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py tok = a[1];
                        if (self.attr("debug") >= (I<1>()))
                        {
                            (B<"print">())((S<"shlex: pushing token ">()) + S(tok.repr()));
                        }
                        self.attr("pushback").method("appendleft", { tok });
                        return none();
                    }) },
                { "push_source", w.method("shlex", "push_source", { py_positional("newstream"), py_positional("newfile", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py newstream = a[1];
                        py newfile = a[2];
                        if (isinstance(newstream, B<"str">()))
                        {
                            newstream = (Mod<"io">().attr("StringIO"))(newstream);
                        }
                        self.attr("filestack").method("appendleft", { py_tuple({ self.attr("infile"), self.attr("instream"), self.attr("lineno") }) });
                        self.set_attr("infile", newfile);
                        self.set_attr("instream", newstream);
                        self.set_attr("lineno", I<1>());
                        if (self.attr("debug").truthy())
                        {
                            if (!newfile.is_none())
                            {
                                (B<"print">())((S<"shlex: pushing to file %s">()) % py_tuple({ self.attr("infile") }));
                            }
                            else
                            {
                                (B<"print">())((S<"shlex: pushing to stream %s">()) % py_tuple({ self.attr("instream") }));
                            }
                        }
                        return none();
                    }) },
                { "pop_source", w.method("shlex", "pop_source", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        self.attr("instream").method("close");
                        auto const items3 = unpack(self.attr("filestack").method("popleft"), 3);
                        self.set_attr("infile", items3[0]);
                        self.set_attr("instream", items3[1]);
                        self.set_attr("lineno", items3[2]);
                        if (self.attr("debug").truthy())
                        {
                            (B<"print">())((S<"shlex: popping to %s, line %d">()) % py_tuple({ self.attr("instream"), self.attr("lineno") }));
                        }
                        self.set_attr("state", S<" ">());
                        return none();
                    }) },
                { "get_token", w.method("shlex", "get_token", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py newfile;
                        py newstream;
                        py raw;
                        py spec;
                        py tok;
                        if (self.attr("pushback").truthy())
                        {
                            tok = self.attr("pushback").method("popleft");
                            if (self.attr("debug") >= (I<1>()))
                            {
                                (B<"print">())((S<"shlex: popping token ">()) + S(tok.repr()));
                            }
                            return tok;
                        }
                        // No pushback.  Get a token.
                        raw = self.method("read_token");
                        // Handle inclusions
                        if (!self.attr("source").is_none())
                        {
                            while (raw == self.attr("source"))
                            {
                                spec = self.method("sourcehook", { self.method("read_token") });
                                if (spec.truthy())
                                {
                                    auto const items5 = unpack(spec, 2);
                                    newfile = items5[0];
                                    newstream = items5[1];
                                    self.method("push_source", { newstream, newfile });
                                }
                                raw = self.method("get_token");
                            }
                        }
                        // Maybe we got EOF instead?
                        while (raw == self.attr("eof"))
                        {
                            if (!self.attr("filestack").truthy())
                            {
                                return self.attr("eof");
                            }
                            else
                            {
                                self.method("pop_source");
                                raw = self.method("get_token");
                            }
                        }
                        // Neither inclusion nor EOF
                        if (self.attr("debug") >= (I<1>()))
                        {
                            if (raw != self.attr("eof"))
                            {
                                (B<"print">())((S<"shlex: token=">()) + S(raw.repr()));
                            }
                            else
                            {
                                (B<"print">())(S<"shlex: token=EOF">());
                            }
                        }
                        return raw;
                    }) },
                { "read_token", w.method("shlex", "read_token", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py escapedstate;
                        py nextchar;
                        py quoted;
                        py result;
                        quoted = boolean(false);
                        escapedstate = S<" ">();
                        while (boolean(true).truthy())
                        {
                            if ((self.attr("punctuation_chars").truthy() && self.attr("_pushback_chars").truthy()))
                            {
                                nextchar = self.attr("_pushback_chars").method("pop");
                            }
                            else
                            {
                                nextchar = self.attr("instream").method("read", { I<1>() });
                            }
                            if (nextchar == (S<"\n">()))
                            {
                                {
                                    py const target8 = self;
                                    target8.set_attr("lineno", inplace(NEOS_PY_ADD, target8.attr("lineno"), I<1>()));
                                }
                            }
                            if (self.attr("debug") >= (I<3>()))
                            {
                                (B<"print">())((S<"shlex: in state %r I see character: %r">()) % py_tuple({ self.attr("state"), nextchar }));
                            }
                            if (self.attr("state").is_none())
                            {
                                self.set_attr("token", S<"">());
                                break;
                            }
                            else if (self.attr("state") == (S<" ">()))
                            {
                                if (!nextchar.truthy())
                                {
                                    self.set_attr("state", none());
                                    break;
                                }
                                else if (self.attr("whitespace").contains(nextchar))
                                {
                                    if (self.attr("debug") >= (I<2>()))
                                    {
                                        (B<"print">())(S<"shlex: I see whitespace in whitespace state">());
                                    }
                                    if ((self.attr("token").truthy() || ([&]() -> py { py const t = self.attr("posix"); return !t.truthy() ? t : quoted; }()).truthy()))
                                    {
                                        break;
                                    }
                                    else
                                    {
                                        continue;
                                    }
                                }
                                else if (self.attr("commenters").contains(nextchar))
                                {
                                    self.attr("instream").method("readline");
                                    {
                                        py const target9 = self;
                                        target9.set_attr("lineno", inplace(NEOS_PY_ADD, target9.attr("lineno"), I<1>()));
                                    }
                                }
                                else if ((self.attr("posix").truthy() && self.attr("escape").contains(nextchar)))
                                {
                                    escapedstate = S<"a">();
                                    self.set_attr("state", nextchar);
                                }
                                else if (self.attr("wordchars").contains(nextchar))
                                {
                                    self.set_attr("token", nextchar);
                                    self.set_attr("state", S<"a">());
                                }
                                else if (self.attr("punctuation_chars").contains(nextchar))
                                {
                                    self.set_attr("token", nextchar);
                                    self.set_attr("state", S<"c">());
                                }
                                else if (self.attr("quotes").contains(nextchar))
                                {
                                    if (!self.attr("posix").truthy())
                                    {
                                        self.set_attr("token", nextchar);
                                    }
                                    self.set_attr("state", nextchar);
                                }
                                else if (self.attr("whitespace_split").truthy())
                                {
                                    self.set_attr("token", nextchar);
                                    self.set_attr("state", S<"a">());
                                }
                                else
                                {
                                    self.set_attr("token", nextchar);
                                    if ((self.attr("token").truthy() || ([&]() -> py { py const t = self.attr("posix"); return !t.truthy() ? t : quoted; }()).truthy()))
                                    {
                                        break;
                                    }
                                    else
                                    {
                                        continue;
                                    }
                                }
                            }
                            else if (self.attr("quotes").contains(self.attr("state")))
                            {
                                quoted = boolean(true);
                                if (!nextchar.truthy())
                                {
                                    if (self.attr("debug") >= (I<2>()))
                                    {
                                        (B<"print">())(S<"shlex: I see EOF in quotes state">());
                                    }
                                    // XXX what error should be raised here?
                                    py_raise((B<"ValueError">())(S<"No closing quotation">()));
                                }
                                if (nextchar == self.attr("state"))
                                {
                                    if (!self.attr("posix").truthy())
                                    {
                                        {
                                            py const target10 = self;
                                            target10.set_attr("token", inplace(NEOS_PY_ADD, target10.attr("token"), nextchar));
                                        }
                                        self.set_attr("state", S<" ">());
                                        break;
                                    }
                                    else
                                    {
                                        self.set_attr("state", S<"a">());
                                    }
                                }
                                else if ((self.attr("posix").truthy() && self.attr("escape").contains(nextchar) && self.attr("escapedquotes").contains(self.attr("state"))))
                                {
                                    escapedstate = self.attr("state");
                                    self.set_attr("state", nextchar);
                                }
                                else
                                {
                                    {
                                        py const target11 = self;
                                        target11.set_attr("token", inplace(NEOS_PY_ADD, target11.attr("token"), nextchar));
                                    }
                                }
                            }
                            else if (self.attr("escape").contains(self.attr("state")))
                            {
                                if (!nextchar.truthy())
                                {
                                    if (self.attr("debug") >= (I<2>()))
                                    {
                                        (B<"print">())(S<"shlex: I see EOF in escape state">());
                                    }
                                    // XXX what error should be raised here?
                                    py_raise((B<"ValueError">())(S<"No escaped character">()));
                                }
                                // In posix shells, only the quote itself or the escape
                                // character may be escaped within quotes.
                                if ((self.attr("quotes").contains(escapedstate) && (nextchar != self.attr("state")) && (nextchar != escapedstate)))
                                {
                                    {
                                        py const target12 = self;
                                        target12.set_attr("token", inplace(NEOS_PY_ADD, target12.attr("token"), self.attr("state")));
                                    }
                                }
                                {
                                    py const target13 = self;
                                    target13.set_attr("token", inplace(NEOS_PY_ADD, target13.attr("token"), nextchar));
                                }
                                self.set_attr("state", escapedstate);
                            }
                            else if (py_tuple({ S<"a">(), S<"c">() }).contains(self.attr("state")))
                            {
                                if (!nextchar.truthy())
                                {
                                    self.set_attr("state", none());
                                    break;
                                }
                                else if (self.attr("whitespace").contains(nextchar))
                                {
                                    if (self.attr("debug") >= (I<2>()))
                                    {
                                        (B<"print">())(S<"shlex: I see whitespace in word state">());
                                    }
                                    self.set_attr("state", S<" ">());
                                    if ((self.attr("token").truthy() || ([&]() -> py { py const t = self.attr("posix"); return !t.truthy() ? t : quoted; }()).truthy()))
                                    {
                                        break;
                                    }
                                    else
                                    {
                                        continue;
                                    }
                                }
                                else if (self.attr("commenters").contains(nextchar))
                                {
                                    self.attr("instream").method("readline");
                                    {
                                        py const target14 = self;
                                        target14.set_attr("lineno", inplace(NEOS_PY_ADD, target14.attr("lineno"), I<1>()));
                                    }
                                    if (self.attr("posix").truthy())
                                    {
                                        self.set_attr("state", S<" ">());
                                        if ((self.attr("token").truthy() || ([&]() -> py { py const t = self.attr("posix"); return !t.truthy() ? t : quoted; }()).truthy()))
                                        {
                                            break;
                                        }
                                        else
                                        {
                                            continue;
                                        }
                                    }
                                }
                                else if (self.attr("state") == (S<"c">()))
                                {
                                    if (self.attr("punctuation_chars").contains(nextchar))
                                    {
                                        {
                                            py const target15 = self;
                                            target15.set_attr("token", inplace(NEOS_PY_ADD, target15.attr("token"), nextchar));
                                        }
                                    }
                                    else
                                    {
                                        if (!self.attr("whitespace").contains(nextchar))
                                        {
                                            self.attr("_pushback_chars").method("append", { nextchar });
                                        }
                                        self.set_attr("state", S<" ">());
                                        break;
                                    }
                                }
                                else if ((self.attr("posix").truthy() && self.attr("quotes").contains(nextchar)))
                                {
                                    self.set_attr("state", nextchar);
                                }
                                else if ((self.attr("posix").truthy() && self.attr("escape").contains(nextchar)))
                                {
                                    escapedstate = S<"a">();
                                    self.set_attr("state", nextchar);
                                }
                                else if ((self.attr("wordchars").contains(nextchar) || self.attr("quotes").contains(nextchar) || ([&]() -> py { py const t = self.attr("whitespace_split"); return !t.truthy() ? t : boolean(!self.attr("punctuation_chars").contains(nextchar)); }()).truthy()))
                                {
                                    {
                                        py const target16 = self;
                                        target16.set_attr("token", inplace(NEOS_PY_ADD, target16.attr("token"), nextchar));
                                    }
                                }
                                else
                                {
                                    if (self.attr("punctuation_chars").truthy())
                                    {
                                        self.attr("_pushback_chars").method("append", { nextchar });
                                    }
                                    else
                                    {
                                        self.attr("pushback").method("appendleft", { nextchar });
                                    }
                                    if (self.attr("debug") >= (I<2>()))
                                    {
                                        (B<"print">())(S<"shlex: I see punctuation in word state">());
                                    }
                                    self.set_attr("state", S<" ">());
                                    if ((self.attr("token").truthy() || ([&]() -> py { py const t = self.attr("posix"); return !t.truthy() ? t : quoted; }()).truthy()))
                                    {
                                        break;
                                    }
                                    else
                                    {
                                        continue;
                                    }
                                }
                            }
                        }
                        result = self.attr("token");
                        self.set_attr("token", S<"">());
                        if ((self.attr("posix").truthy() && (!quoted.truthy()) && (result == (S<"">()))))
                        {
                            result = none();
                        }
                        if (self.attr("debug") > (I<1>()))
                        {
                            if (result.truthy())
                            {
                                (B<"print">())((S<"shlex: raw token=">()) + S(result.repr()));
                            }
                            else
                            {
                                (B<"print">())(S<"shlex: raw token=EOF">());
                            }
                        }
                        return result;
                    }) },
                { "sourcehook", w.method("shlex", "sourcehook", { py_positional("newfile") }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py newfile = a[1];
                        if (newfile[I<0>()] == (S<"\"">()))
                        {
                            newfile = newfile[B<"slice">()(I<1>(), I<-1>(), none())];
                        }
                        // This implements cpp-like semantics for relative-path inclusion.
                        if ((isinstance(self.attr("infile"), B<"str">()) && (!(Mod<"os">().attr("path")).method("isabs", { newfile }).truthy())))
                        {
                            newfile = (Mod<"os">().attr("path")).method("join", { (Mod<"os">().attr("path")).method("dirname", { self.attr("infile") }), newfile });
                        }
                        return py_tuple({ newfile, (B<"open">())(newfile, S<"r">()) });
                    }) },
                { "error_leader", w.method("shlex", "error_leader", { py_positional("infile", none()), py_positional("lineno", none()) }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py infile = a[1];
                        py lineno = a[2];
                        if (infile.is_none())
                        {
                            infile = self.attr("infile");
                        }
                        if (lineno.is_none())
                        {
                            lineno = self.attr("lineno");
                        }
                        return (S<"\"%s\", line %d: ">()) % py_tuple({ infile, lineno });
                    }) },
                { "__iter__", w.method("shlex", "__iter__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        return self;
                    }) },
                { "__next__", w.method("shlex", "__next__", {  }, [](arguments a) -> py
                    {
                        py self = a[0];
                        py token;
                        token = self.method("get_token");
                        if (token == self.attr("eof"))
                        {
                            py_raise(B<"StopIteration">());
                        }
                        return token;
                    }) }, }));

            define("shlex", "split", w.function("split", "split", { py_positional("s"), py_positional("comments", boolean(false)), py_positional("posix", boolean(true)) }, [](arguments a) -> py
                {
                    py s = a[0];
                    py comments = a[1];
                    py posix = a[2];
                    py lex;
                    if (s.is_none())
                    {
                        py_raise((B<"ValueError">())(S<"s argument must not be None">()));
                    }
                    lex = (G<"shlex.shlex">()).call({ s }, { { "posix", posix } });
                    lex.set_attr("whitespace_split", boolean(true));
                    if (!comments.truthy())
                    {
                        lex.set_attr("commenters", S<"">());
                    }
                    return (B<"list">())(lex);
                }));

            define("shlex", "join", w.function("join", "join", { py_positional("split_command") }, [](arguments a) -> py
                {
                    py split_command = a[0];
                    return (S<" ">()).method("join", { [&]() -> py { py const comprehension17 = py_list(); py arg; for (py const& item : iterate{ split_command }) { arg = item; comprehension17.method("append", { (G<"shlex.quote">())(arg) }); } return comprehension17; }() });
                }));

            define("shlex", "_find_unsafe", (Mod<"re">()).method("compile", { S<"[^\\w@%+=:,./-]">(), Mod<"re">().attr("ASCII") }).attr("search"));
            define("shlex", "quote", w.function("quote", "quote", { py_positional("s") }, [](arguments a) -> py
                {
                    py s = a[0];
                    if (!s.truthy())
                    {
                        return S<"''">();
                    }
                    if ((G<"shlex._find_unsafe">())(s).is_none())
                    {
                        return s;
                    }
                    // use single quotes, and put single quotes into double quotes
                    // the string $'b is then quoted as '$'"'"'b'
                    return ((S<"'">()) + s.method("replace", { S<"'">(), S<"'\"'\"'">() })) + (S<"'">());
                }));

            define("shlex", "_print_tokens", w.function("_print_tokens", "_print_tokens", { py_positional("lexer") }, [](arguments a) -> py
                {
                    py lexer = a[0];
                    py tt;
                    while (([&]() -> py { tt = lexer.method("get_token"); return tt; }()).truthy())
                    {
                        (B<"print">())((S<"Token: ">()) + S(tt.repr()));
                    }
                    return none();
                }));

            // (if __name__ == "__main__": the module is not run as the program)
        }

        bool shlex_module(std::vector<std::pair<std::string, object*>>& aAttributes)
        {
            auto const result = py_guarded([&]()
            {
                auto& s = the_state();
                if (!s.made)
                {
                    s.made = true;
                    make_shlex();
                }
                if (auto const doc = shlex_docs::doc(""))
                    aAttributes.emplace_back("__doc__", py{ doc }.release());
                aAttributes.emplace_back("os", py{ fn_import("os") }.release());
                aAttributes.emplace_back("re", py{ fn_import("re") }.release());
                aAttributes.emplace_back("sys", py{ fn_import("sys") }.release());
                aAttributes.emplace_back("deque", import_attribute("collections", "deque").release());
                aAttributes.emplace_back("StringIO", import_attribute("io", "StringIO").release());
                for (auto const& [name, value] : s.attributes)
                    aAttributes.emplace_back(name, value.reference());
                return py::none();
            });
            release(result);
            return result != nullptr;
        }

        inline bool const sModule = register_module("shlex", shlex_module);
    }
}
